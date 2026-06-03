/*
 * AXS5106 / AXS15231B-family capacitive touchscreen driver.
 *
 * Polls the controller over I2C every TOUCH_POLL_MS and
 * dispatches the down-edge of every tap to the
 * touchscreen_tap_cb_t supplied by the caller. Coordinates are
 * mapped from the controller's panel-native space to the
 * logical framebuffer using the (mirror_x / mirror_y / swap_xy
 * / native_w / native_h) fields in board_t::touch.
 *
 * Protocol reference: same magic-packet handshake used by the
 * upstream LovyanGFX / LilyGo / Tactility drivers for this
 * controller family. The host writes an 8-byte vendor preamble
 * (0xB5 0xAB 0xA5 0x5A 0 0 0 0x08) and the controller replies
 * with 8 bytes:
 *   [0]    gesture id (0 = none, else a vendor-defined code)
 *   [1]    bits[3:0] = touch-point count (0..5)
 *   [2]    bits[3:0] = X high nibble
 *   [3]    X low byte
 *   [4]    bits[3:0] = Y high nibble
 *   [5]    Y low byte
 *   [6..7] weight / area, ignored
 *
 * "Finger up" is recognised either by an explicit zero point
 * count or by an all-zero response packet across several
 * consecutive polls (the controller does not always populate the
 * point-count byte, matching the upstream drivers' behaviour).
 *
 * On boards without CONFIG_BOARD_HAS_TOUCH the entire body of
 * this file compiles to no-op stubs so the link graph stays the
 * same regardless of board choice.
 */

#include "touchscreen.h"

#include "sdkconfig.h"
#include "board.h"

#include <esp_log.h>

#if CONFIG_BOARD_HAS_TOUCH

#include <string.h>

#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "display.h"

static const char *TAG = "touchscreen";

#define TOUCH_POLL_MS 20

/* Number of consecutive "no point" polls required to register a
 * finger-up edge. Filters out the brief gaps the AXS controller
 * sometimes shows mid-tap when the user drags slowly. */
#define RELEASE_DEBOUNCE 2

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;
static bool                    s_inited;
static touchscreen_tap_cb_t    s_tap_cb;

/* AXS15231B touch read command. Matches the espressif
 * esp-arduino-libs / xiaozhi-library implementations for this
 * controller: 11-byte preamble where bytes [6:7] encode the
 * expected response length (big-endian) -- here 8 bytes (one
 * touch point: 2 header + 6 data). The earlier 8-byte preamble
 * the firmware used caused the controller to occasionally hand
 * back stale GRAM with bogus "finger down" flags, which left
 * our software touch state stuck pressed and prevented all
 * subsequent taps from registering. */
static const uint8_t AXS_READ_CMD[11] = {
    0xB5, 0xAB, 0xA5, 0x5A, 0x00, 0x00, 0x00, 0x08,
    0x00, 0x00, 0x00,
};

/* Map a raw controller point to logical framebuffer pixels using
 * the board's mirror_* / swap_xy / native_w / native_h. Returns
 * coords clamped to the framebuffer bounds. */
static void native_to_logical(int nx, int ny, int *ox, int *oy)
{
    const board_touch_t *t = &board_get()->touch;
    int nw = t->native_w > 0 ? t->native_w : display_width();
    int nh = t->native_h > 0 ? t->native_h : display_height();

    if (t->mirror_x) nx = (nw - 1) - nx;
    if (t->mirror_y) ny = (nh - 1) - ny;
    if (t->swap_xy) {
        int tmp = nx; nx = ny; ny = tmp;
        tmp = nw; nw = nh; nh = tmp;
    }

    int lw = display_width();
    int lh = display_height();
    if (nw < 1) nw = 1;
    if (nh < 1) nh = 1;
    int lx = (int)((long long)nx * lw / nw);
    int ly = (int)((long long)ny * lh / nh);
    if (lx < 0) lx = 0;
    if (lx > lw - 1) lx = lw - 1;
    if (ly < 0) ly = 0;
    if (ly > lh - 1) ly = lh - 1;
    *ox = lx;
    *oy = ly;
}

/* One I2C transaction. Fills *out_x / *out_y on a valid touch
 * sample (returns true). Returns false on I2C error or finger-up. */
static bool poll_once(int *out_x, int *out_y)
{
    if (!s_dev) return false;
    uint8_t resp[8] = { 0 };
    /* Use a write-then-read pair (not transmit_receive with a
     * repeated-start) -- the AXS controller occasionally NAKs
     * the restart on the audio-codec shared bus on the 3.5B.
     * Two independent transactions matches the upstream
     * esp_lcd_touch_axs15231b driver. */
    esp_err_t err = i2c_master_transmit(
        s_dev, AXS_READ_CMD, sizeof(AXS_READ_CMD), 50 /* ms */);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "i2c write failed: %s", esp_err_to_name(err));
        return false;
    }
    err = i2c_master_receive(s_dev, resp, sizeof(resp), 50 /* ms */);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "i2c read failed: %s", esp_err_to_name(err));
        return false;
    }

    uint8_t points = resp[1] & 0x0F;
    /* Trust only the controller's explicit point count. The
     * older "gesture==0 && coords!=0" heuristic mis-classifies
     * the AXS controller's idle response (which can carry
     * stale, non-zero coordinates between real touch frames)
     * as a held-down finger, which permanently latches our
     * pressed-state and makes every tap after the first
     * appear to do nothing. */
    if (points < 1 || points > 5) return false;

    int nx = ((int)(resp[2] & 0x0F) << 8) | resp[3];
    int ny = ((int)(resp[4] & 0x0F) << 8) | resp[5];
    native_to_logical(nx, ny, out_x, out_y);
    return true;
}

static void touch_task(void *arg)
{
    (void)arg;
    bool pressed = false;
    int  release_streak = 0;
    while (1) {
        int x = 0, y = 0;
        bool now = poll_once(&x, &y);
        if (now) {
            release_streak = 0;
            if (!pressed) {
                pressed = true;
                if (s_tap_cb) s_tap_cb(x, y);
            }
        } else {
            if (pressed) {
                if (++release_streak >= RELEASE_DEBOUNCE) {
                    pressed = false;
                    release_streak = 0;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(TOUCH_POLL_MS));
    }
}

bool touchscreen_init(void)
{
    if (s_inited) return true;
    const board_touch_t *t = &board_get()->touch;
    if (t->sda < 0 || t->scl < 0) {
        ESP_LOGI(TAG, "no touch pins on this board; touchscreen disabled");
        return false;
    }

    /* Optional reset pulse. */
    if (t->rst >= 0) {
        gpio_config_t g = {
            .intr_type    = GPIO_INTR_DISABLE,
            .mode         = GPIO_MODE_OUTPUT,
            .pin_bit_mask = (1ULL << t->rst),
        };
        gpio_config(&g);
        gpio_set_level((gpio_num_t)t->rst, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level((gpio_num_t)t->rst, 1);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    /* INT line kept as plain input. The AXS controllers do not
     * hold INT asserted while a finger is down -- they emit a
     * brief per-frame pulse -- so we poll over I2C and only use
     * INT (when wired) as a hint / wake source for callers. */
    if (t->intr >= 0) {
        gpio_config_t g = {
            .intr_type    = GPIO_INTR_DISABLE,
            .mode         = GPIO_MODE_INPUT,
            .pin_bit_mask = (1ULL << t->intr),
            .pull_up_en   = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
        };
        gpio_config(&g);
    }

    i2c_master_bus_config_t bus_cfg = {
        .clk_source        = I2C_CLK_SRC_DEFAULT,
        .i2c_port          = t->i2c_port,
        .sda_io_num        = t->sda,
        .scl_io_num        = t->scl,
        .glitch_ignore_cnt = 7,
        .flags             = { .enable_internal_pullup = true },
    };
    /* Reuse an already-initialized bus on the same port if one
     * exists (e.g. the audio component brought up the I2C bus
     * to talk to the ES8311 codec, which shares SDA/SCL with
     * the touch controller on the Waveshare 3.5B). Creating a
     * second bus on the same port fails with INVALID_STATE. */
    if (i2c_master_get_bus_handle(t->i2c_port, &s_bus) != ESP_OK ||
        s_bus == NULL) {
        if (i2c_new_master_bus(&bus_cfg, &s_bus) != ESP_OK) {
            ESP_LOGE(TAG, "i2c_new_master_bus failed");
            return false;
        }
    } else {
        ESP_LOGI(TAG, "reusing existing I2C%d master bus", t->i2c_port);
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = t->addr,
        .scl_speed_hz    = t->freq_hz ? t->freq_hz : 400000,
    };
    if (i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev) != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_bus_add_device failed");
        i2c_del_master_bus(s_bus);
        s_bus = NULL;
        return false;
    }

    /* Probe: ignore the result. The AXS controllers do not always
     * ACK a bare address byte without the magic-packet preamble,
     * so a failed probe does not mean the chip is absent. The
     * polling task will simply log debug errors if the bus is
     * truly empty. */
    (void)i2c_master_probe(s_bus, t->addr, 50);

    s_inited = true;
    ESP_LOGI(TAG, "touchscreen ready (I2C%d sda=%d scl=%d addr=0x%02X "
                  "native=%dx%d mirror=%d,%d swap=%d)",
             t->i2c_port, t->sda, t->scl, t->addr,
             t->native_w, t->native_h,
             (int)t->mirror_x, (int)t->mirror_y, (int)t->swap_xy);
    return true;
}

void touchscreen_start(touchscreen_tap_cb_t cb)
{
    if (!s_inited) return;
    s_tap_cb = cb;
    xTaskCreatePinnedToCore(touch_task, "touch", 4096, NULL, 4, NULL, 0);
}

bool touchscreen_is_available(void) { return s_inited; }

#else /* !CONFIG_BOARD_HAS_TOUCH */

bool touchscreen_init(void) { return false; }
void touchscreen_start(touchscreen_tap_cb_t cb) { (void)cb; }
bool touchscreen_is_available(void) { return false; }

#endif /* CONFIG_BOARD_HAS_TOUCH */
