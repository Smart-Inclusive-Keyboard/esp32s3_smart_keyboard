/*
 * SPI gamepad driver and polling task.
 *
 * The device is the SPI host; the gamepad is the SPI slave.
 * The driver issues a single full-duplex 4-byte transaction
 * each poll interval and parses the MISO bytes into the same
 * simplified-HID report format used by gamepad_i2c (see
 * gamepad_i2c.h for byte semantics).
 *
 * Edge events are emitted on a FreeRTOS queue identical in
 * shape and contents to the one produced by gamepad_i2c, so
 * input_router doesn't need to know which transport is active.
 */

#include "gamepad_spi.h"

#include "sdkconfig.h"

#if CONFIG_SK_GAMEPAD_TRANSPORT_SPI

#include <string.h>

#include <esp_check.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <driver/spi_master.h>
#include <driver/gpio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board.h"
#include "sdkconfig.h"

static const char *TAG = "gamepad_spi";

static QueueHandle_t        s_queue;
static spi_device_handle_t  s_dev;

/* Previous frame bitmap (bit positions match gamepad_button_t). */
static uint32_t s_prev_state;

/* Decode a 4-byte report into a flat button bitmap. Kept
 * structurally identical to the I2C parser so a single source
 * of truth for the wire format stays in gamepad_i2c.h. */
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

    if (face & 0x01) s |= 1u << GP_BTN_1;
    if (face & 0x02) s |= 1u << GP_BTN_2;
    if (face & 0x04) s |= 1u << GP_BTN_3;
    if (face & 0x08) s |= 1u << GP_BTN_4;

    if (aux & 0x01) s |= 1u << GP_BTN_5;
    if (aux & 0x02) s |= 1u << GP_BTN_6;
    if (aux & 0x04) s |= 1u << GP_BTN_7;
    if (aux & 0x08) s |= 1u << GP_BTN_8;

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
    uint8_t tx[4] = { 0, 0, 0, 0 };  /* dummy command byte(s) */
    uint8_t rx[4] = { 0, 0, 0, 0 };

    while (1) {
        spi_transaction_t t = {
            .length    = 8 * sizeof(rx),    /* in bits         */
            .rxlength  = 8 * sizeof(rx),
            .tx_buffer = tx,
            .rx_buffer = rx,
        };
        esp_err_t err = spi_device_transmit(s_dev, &t);
        if (err == ESP_OK) {
            uint32_t cur = gamepad_parse_report(rx);
            if (cur != s_prev_state) {
                emit_edges(s_prev_state, cur);
                s_prev_state = cur;
            }
        } else {
            /* Spam-limit the log: one line per ~64 polls. */
            static int err_count;
            if ((++err_count & 0x3F) == 0) {
                ESP_LOGW(TAG, "SPI transfer failed: %s", esp_err_to_name(err));
            }
        }
        vTaskDelay(period);
    }
}

QueueHandle_t gamepad_spi_start(void)
{
    if (s_queue) return s_queue;

    const board_t *b = board_get();

    /* 1. SPI bus. */
    spi_bus_config_t bus_cfg = {
        .sclk_io_num     = b->spi_sclk,
        .mosi_io_num     = b->spi_mosi,
        .miso_io_num     = b->spi_miso,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 64,
    };
    spi_host_device_t host = (spi_host_device_t)b->spi_host;
    esp_err_t err = spi_bus_initialize(host, &bus_cfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        /* INVALID_STATE = bus already initialised by another
         * component (e.g. display sharing the same host). That
         * is fine -- we just add our device on top. */
        ESP_LOGE(TAG, "spi_bus_initialize(host=%d) failed: %s",
                 (int)host, esp_err_to_name(err));
        return NULL;
    }

    /* 2. Gamepad device. */
    spi_device_interface_config_t dev_cfg = {
        .mode           = b->spi_mode,
        .clock_speed_hz = b->spi_freq_hz,
        .spics_io_num   = b->spi_cs,
        .queue_size     = 1,
    };
    if (spi_bus_add_device(host, &dev_cfg, &s_dev) != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device failed");
        return NULL;
    }

    /* 3. Event queue + polling task. */
    s_queue = xQueueCreate(16, sizeof(gamepad_event_t));
    if (!s_queue) return NULL;

    BaseType_t ok = xTaskCreatePinnedToCore(poll_task, "gamepad",
                                            3072, NULL, 5, NULL, 0);
    if (ok != pdPASS) return NULL;

    ESP_LOGI(TAG, "Polling every %d ms on SPI%d (SCLK=%d, MOSI=%d, MISO=%d, CS=%d) @ %d Hz mode %d",
             CONFIG_SK_GAMEPAD_POLL_MS,
             (int)host, b->spi_sclk, b->spi_mosi, b->spi_miso, b->spi_cs,
             b->spi_freq_hz, b->spi_mode);
    return s_queue;
}

#else  /* CONFIG_SK_GAMEPAD_TRANSPORT_SPI */

QueueHandle_t gamepad_spi_start(void)
{
    /* SPI transport disabled at compile time. */
    return NULL;
}

#endif
