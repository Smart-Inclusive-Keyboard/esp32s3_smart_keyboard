/*
 * I2C gamepad driver and polling task.
 *
 * Reads a 4-byte simplified-HID report from the configured
 * slave address, applies a configurable dead-zone to the
 * analog axes, and emits edge events on a FreeRTOS queue.
 *
 * The parser (gamepad_parse_report) is intentionally factored
 * out so adding a new controller protocol is a one-function
 * change. To replace it wholesale, set CONFIG_SK_GAMEPAD_*
 * to match your wiring and edit the parser.
 */

#include "gamepad_i2c.h"

#include <string.h>

#include <esp_log.h>

#include "sdkconfig.h"

static const char *s_names[GP_BTN_COUNT] = {
    [GP_BTN_UP]     = "UP",
    [GP_BTN_DOWN]   = "DOWN",
    [GP_BTN_LEFT]   = "LEFT",
    [GP_BTN_RIGHT]  = "RIGHT",
    [GP_BTN_A]      = "A",
    [GP_BTN_B]      = "B",
    [GP_BTN_X]      = "X",
    [GP_BTN_Y]      = "Y",
    [GP_BTN_L]      = "L",
    [GP_BTN_R]      = "R",
    [GP_BTN_SELECT] = "SELECT",
    [GP_BTN_START]  = "START",
};

const char *gamepad_button_name(gamepad_button_t b)
{
    if ((int)b < 0 || (int)b >= GP_BTN_COUNT) return "?";
    return s_names[b];
}

#if CONFIG_SK_GAMEPAD_TRANSPORT_I2C

#include <esp_check.h>
#include <esp_timer.h>
#include <driver/i2c_master.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board.h"
#include "sdkconfig.h"

static const char *TAG = "gamepad_i2c";

static QueueHandle_t        s_queue;
static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;

/* The previous frame's logical button bitmap, used for edge
 * detection. Bit positions match the gamepad_button_t enum. */
static uint32_t s_prev_state;

/* Decode a 4-byte report into a flat bitmap whose bit positions
 * match gamepad_button_t. */
static uint32_t gamepad_parse_report(const uint8_t *r)
{
    int8_t x = (int8_t)r[0];
    int8_t y = (int8_t)r[1];
    uint8_t face = r[2];
    uint8_t aux  = r[3];

    int dz = CONFIG_SK_GAMEPAD_AXIS_DEADZONE;
    uint32_t s = 0;

    if (x >  dz) s |= 1u << GP_BTN_RIGHT;
    if (x < -dz) s |= 1u << GP_BTN_LEFT;
    if (y >  dz) s |= 1u << GP_BTN_DOWN;
    if (y < -dz) s |= 1u << GP_BTN_UP;

    if (face & 0x01) s |= 1u << GP_BTN_A;
    if (face & 0x02) s |= 1u << GP_BTN_B;
    if (face & 0x04) s |= 1u << GP_BTN_X;
    if (face & 0x08) s |= 1u << GP_BTN_Y;

    if (aux & 0x01) s |= 1u << GP_BTN_L;
    if (aux & 0x02) s |= 1u << GP_BTN_R;
    if (aux & 0x04) s |= 1u << GP_BTN_SELECT;
    if (aux & 0x08) s |= 1u << GP_BTN_START;

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

static void poll_task(void *arg)
{
    (void)arg;
    const TickType_t period = pdMS_TO_TICKS(CONFIG_SK_GAMEPAD_POLL_MS);
    uint8_t buf[4];

    while (1) {
        esp_err_t err = i2c_master_receive(s_dev, buf, sizeof(buf),
                                           pdMS_TO_TICKS(20));
        if (err == ESP_OK) {
            uint32_t cur = gamepad_parse_report(buf);
            if (cur != s_prev_state) {
                emit_edges(s_prev_state, cur);
                s_prev_state = cur;
            }
        } else {
            /* Periodically log so the user knows the I2C bus is
             * not seeing the controller. Spam-limited because
             * this fires every poll interval otherwise. */
            static int err_count;
            if ((++err_count & 0x3F) == 0) {
                ESP_LOGW(TAG, "I2C read failed: %s", esp_err_to_name(err));
            }
        }
        vTaskDelay(period);
    }
}

QueueHandle_t gamepad_i2c_start(void)
{
    if (s_queue) return s_queue;

    const board_t *b = board_get();

    /* 1. I2C master bus. */
    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port   = b->i2c_port,
        .sda_io_num = b->i2c_sda,
        .scl_io_num = b->i2c_scl,
        .glitch_ignore_cnt = 7,
        .flags = {
            .enable_internal_pullup = true,
        },
    };
    if (i2c_new_master_bus(&bus_cfg, &s_bus) != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed");
        return NULL;
    }

    /* 2. Gamepad device. */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = CONFIG_SK_GAMEPAD_I2C_ADDR,
        .scl_speed_hz    = b->i2c_freq_hz,
    };
    if (i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev) != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_bus_add_device failed");
        return NULL;
    }

    /* 3. Event queue + polling task. */
    s_queue = xQueueCreate(16, sizeof(gamepad_event_t));
    if (!s_queue) return NULL;

    BaseType_t ok = xTaskCreatePinnedToCore(poll_task, "gamepad",
                                            3072, NULL, 5, NULL, 0);
    if (ok != pdPASS) return NULL;

    ESP_LOGI(TAG, "Polling 0x%02X every %d ms on I2C%d (SDA=%d, SCL=%d)",
             CONFIG_SK_GAMEPAD_I2C_ADDR,
             CONFIG_SK_GAMEPAD_POLL_MS,
             b->i2c_port, b->i2c_sda, b->i2c_scl);
    return s_queue;
}

#else  /* CONFIG_SK_GAMEPAD_TRANSPORT_I2C */

QueueHandle_t gamepad_i2c_start(void)
{
    /* I2C transport disabled at compile time. */
    return NULL;
}

#endif
