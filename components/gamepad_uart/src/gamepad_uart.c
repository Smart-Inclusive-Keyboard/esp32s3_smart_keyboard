/*
 * UART gamepad driver and receive task.
 *
 * Reads fixed 6-byte HID reports from a receive-only UART link,
 * applies a configurable dead-zone to the analog axes, and emits
 * edge events on a FreeRTOS queue. The wire format matches the
 * report streamed by the companion gamepad firmware
 * (https://github.com/clackups/esp32s3_dual_foc_gp): two button
 * bytes (10 buttons + padding) followed by two signed 16-bit
 * little-endian axes. See gamepad_uart.h for the byte layout.
 */

#include "gamepad_uart.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <driver/uart.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board.h"
#include "sdkconfig.h"

static const char *s_names[GP_BTN_COUNT] = {
    [GP_BTN_UP]    = "UP",
    [GP_BTN_DOWN]  = "DOWN",
    [GP_BTN_LEFT]  = "LEFT",
    [GP_BTN_RIGHT] = "RIGHT",
    [GP_BTN_0]     = "0",
    [GP_BTN_1]     = "1",
    [GP_BTN_2]     = "2",
    [GP_BTN_3]     = "3",
    [GP_BTN_4]     = "4",
    [GP_BTN_5]     = "5",
    [GP_BTN_6]     = "6",
    [GP_BTN_7]     = "7",
    [GP_BTN_8]     = "8",
    [GP_BTN_9]     = "9",
};

const char *gamepad_button_name(gamepad_button_t b)
{
    if ((int)b < 0 || (int)b >= GP_BTN_COUNT) return "?";
    return s_names[b];
}

static const char *TAG = "gamepad_uart";

/* Wire frame size: 2 button bytes + 2 signed 16-bit LE axes. */
#define GP_UART_FRAME_LEN 6

/* RX ring buffer for the UART driver. Must exceed the hardware
 * FIFO (128 bytes); a couple of frames of slack is plenty. */
#define GP_UART_RX_BUF_SIZE 256

static QueueHandle_t s_queue;
static uart_port_t   s_port;

/* The previous frame's logical button bitmap, used for edge
 * detection. Bit positions match the gamepad_button_t enum. */
static uint32_t s_prev_state;

/* Decode a 6-byte report into a flat bitmap whose bit positions
 * match gamepad_button_t. */
static uint32_t gamepad_parse_report(const uint8_t *r)
{
    uint16_t buttons = (uint16_t)r[0] | ((uint16_t)(r[1] & 0x03) << 8);
    int16_t  x = (int16_t)((uint16_t)r[2] | ((uint16_t)r[3] << 8));
    int16_t  y = (int16_t)((uint16_t)r[4] | ((uint16_t)r[5] << 8));

    int dz = CONFIG_SK_GAMEPAD_AXIS_DEADZONE;
    uint32_t s = 0;

    if (x >  dz) s |= 1u << GP_BTN_RIGHT;
    if (x < -dz) s |= 1u << GP_BTN_LEFT;
    if (y >  dz) s |= 1u << GP_BTN_DOWN;
    if (y < -dz) s |= 1u << GP_BTN_UP;

    for (int i = 0; i < 10; ++i) {
        if (buttons & (1u << i)) {
            s |= 1u << (GP_BTN_0 + i);
        }
    }

    return s;
}

static void emit_edges(uint32_t prev, uint32_t cur)
{
    uint32_t down_edges = cur  & ~prev;
    uint32_t up_edges   = prev & ~cur;
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);

    for (int b = 0; b < GP_BTN_COUNT; ++b) {
        uint32_t mask = 1u << b;
        if (down_edges & mask) {
            gamepad_event_t ev = {
                .button = (gamepad_button_t)b,
                .pressed = true,
                .time_ms = now,
            };
            xQueueSend(s_queue, &ev, 0);
        } else if (up_edges & mask) {
            gamepad_event_t ev = {
                .button = (gamepad_button_t)b,
                .pressed = false,
                .time_ms = now,
            };
            xQueueSend(s_queue, &ev, 0);
        }
    }
}

static void rx_task(void *arg)
{
    (void)arg;
    uint8_t buf[GP_UART_FRAME_LEN];

    while (1) {
        /* Block until a full frame arrives. The link carries
         * nothing but back-to-back 6-byte reports, so a fixed
         * read keeps the two sides frame-aligned. */
        int n = uart_read_bytes(s_port, buf, sizeof(buf), portMAX_DELAY);
        if (n == (int)sizeof(buf)) {
            uint32_t cur = gamepad_parse_report(buf);
            if (cur != s_prev_state) {
                emit_edges(s_prev_state, cur);
                s_prev_state = cur;
            }
        } else if (n < 0) {
            /* Spam-limit the log: one line per ~64 errors. */
            static int err_count;
            if ((++err_count & 0x3F) == 0) {
                ESP_LOGW(TAG, "uart_read_bytes returned %d", n);
            }
        }
    }
}

QueueHandle_t gamepad_uart_start(void)
{
    if (s_queue) return s_queue;

    const board_t *b = board_get();

    if (b->uart_rx < 0) {
        ESP_LOGE(TAG, "board has no gamepad UART RX pin configured");
        return NULL;
    }

    s_port = (uart_port_t)b->uart_port;

    /* 1. UART driver + parameters. tx_buffer_size 0 means the
     * (unused) TX path writes straight to the FIFO; we never
     * transmit. */
    const uart_config_t cfg = {
        .baud_rate  = b->uart_baud,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_driver_install(s_port, GP_UART_RX_BUF_SIZE,
                                        0, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
        return NULL;
    }

    err = uart_param_config(s_port, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config failed: %s", esp_err_to_name(err));
        return NULL;
    }

    /* Receive only: assign the RX pin, leave TX / RTS / CTS
     * unchanged (unconnected). */
    err = uart_set_pin(s_port, UART_PIN_NO_CHANGE, b->uart_rx,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin failed: %s", esp_err_to_name(err));
        return NULL;
    }

    /* 2. Event queue + RX task. */
    s_queue = xQueueCreate(16, sizeof(gamepad_event_t));
    if (!s_queue) return NULL;

    BaseType_t ok = xTaskCreatePinnedToCore(rx_task, "gamepad",
                                            3072, NULL, 5, NULL, 0);
    if (ok != pdPASS) return NULL;

    ESP_LOGI(TAG, "Receiving on UART%d (RX=%d) %d baud 8-N-1, RX only",
             (int)s_port, b->uart_rx, b->uart_baud);
    return s_queue;
}
