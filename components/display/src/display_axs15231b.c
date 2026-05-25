/*
 * AXS15231B QSPI display backend.
 *
 * Wraps Espressif's managed component `espressif/esp_lcd_axs15231b`
 * (declared in components/display/idf_component.yml). We provide a
 * Waveshare-specific vendor init sequence (the managed component's
 * default init targets a 320x480 panel and leaves the Waveshare
 * 3.49" 172x640 panel black), a longer hardware reset pulse, and
 * a 90 deg CW software rotation in the flush path because the
 * AXS15231B silently ignores the MADCTL MV bit on this panel.
 *
 * The framebuffer is allocated in PSRAM at the *logical* landscape
 * resolution (s_w x s_h, e.g. 640x172). On flush we transpose into
 * a panel-native portrait buffer (s_h x s_w) and stream it to the
 * panel with a single esp_lcd_panel_draw_bitmap() call addressing
 * the panel's native coords. CASET-only addressing (the panel
 * ignores RASET in QSPI mode) makes partial-rectangle flushing
 * unreliable in rotation mode, so we always flush the full panel
 * when swap_xy is in effect -- the same approach used by the
 * proven clackups/draftling driver.
 *
 * Backlight is driven by LEDC PWM. The Waveshare
 * ESP32-S3-Touch-LCD-3.49 wires the BL pin active-LOW, so we
 * invert the duty when board.display.bl_active_low is set
 * (matches Waveshare's lcd_bl_pwm_bsp.c reference firmware). The
 * BL GPIO is pre-driven to its "on" level before the LEDC channel
 * takes it over -- the on-board BL boost circuit on some Waveshare
 * 3.49 revisions latches into a permanently-off state if it sees
 * an indeterminate level during the brief power-on / LEDC handoff
 * window, which used to leave the panel dark for the whole
 * session.
 */

#include "sdkconfig.h"

#if CONFIG_BOARD_HAS_DISPLAY_AXS15231B

#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_check.h>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <driver/gpio.h>
#include <driver/ledc.h>
#include <driver/spi_master.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>

#include "esp_lcd_axs15231b.h"

#include "board.h"
#include "display.h"

static const char *TAG = "display_axs";

/* LEDC PWM configuration for the backlight (mirrors Waveshare's
 * lcd_bl_pwm_bsp.c reference firmware: 50 kHz, 8-bit, RC_FAST).
 * RC_FAST (~17.5 MHz internal oscillator) survives APB-clock
 * changes when BLE / Wi-Fi enable dynamic frequency scaling;
 * AUTO_CLK previously picked APB and produced an unreliable PWM
 * that left the BL effectively dark. */
#define BL_LEDC_TIMER       LEDC_TIMER_3
#define BL_LEDC_MODE        LEDC_LOW_SPEED_MODE
#define BL_LEDC_CHANNEL     LEDC_CHANNEL_1
#define BL_LEDC_DUTY_RES    LEDC_TIMER_8_BIT
#define BL_LEDC_DUTY_MAX    ((1 << 8) - 1)
#define BL_LEDC_FREQ_HZ     50000

/* QSPI clock to the panel. 40 MHz is the figure Waveshare's
 * sample firmware uses; the controller is rated up to 80 MHz
 * but the panel flex circuit on the 3.49" board doesn't always
 * tolerate it cleanly. */
#define LCD_PIXEL_CLOCK_HZ  (40 * 1000 * 1000)

static int                    s_bl_pin       = -1;
static bool                   s_bl_active_lo = false;
static esp_lcd_panel_handle_t s_panel_handle = NULL;
static uint16_t              *s_fb           = NULL;  /* logical landscape */
static uint16_t              *s_panel_buf    = NULL;  /* native portrait, transposed */
static int                    s_w            = 0;     /* logical width  */
static int                    s_h            = 0;     /* logical height */
static bool                   s_swap_xy      = false;

/* Forward declarations of the framebuffer integration hooks
 * implemented in display.c. */
extern void display_register_backend(uint16_t *fb, int w, int h,
                                     void (*flush)(int, int, int, int),
                                     void (*set_backlight)(int));

/* ---------------- Backlight ---------------- */

static void backlight_init(void)
{
    if (s_bl_pin < 0) return;

    /* Pre-configure the BL pin as a plain GPIO output and drive it
     * to its "on" level BEFORE ledc_channel_config() routes it
     * through the LEDC peripheral. Waveshare's reference
     * `lcd_bl_pwm_bsp.c` for the ESP32-S3-Touch-LCD-3.49 does this
     * same gpio_config() step first; without it, the BL line can
     * sit in an indeterminate state during the brief window between
     * power-on and ledc_channel_config()'s first PWM cycle (long
     * enough on cold boot for the on-board BL driver to latch into
     * a permanently-off state on some board revisions, so the panel
     * stayed black through the whole session). */
    gpio_config_t g = {
        .intr_type    = GPIO_INTR_DISABLE,
        .mode         = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << s_bl_pin),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&g));
    gpio_set_level((gpio_num_t)s_bl_pin, s_bl_active_lo ? 0 : 1);

    ledc_timer_config_t tcfg = {
        .speed_mode      = BL_LEDC_MODE,
        .duty_resolution = BL_LEDC_DUTY_RES,
        .timer_num       = BL_LEDC_TIMER,
        .freq_hz         = BL_LEDC_FREQ_HZ,
        .clk_cfg         = LEDC_USE_RC_FAST_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&tcfg));

    /* Start at 100 % brightness; the UI may dim it later via
     * display_set_backlight(). */
    uint32_t duty = s_bl_active_lo ? 0 : BL_LEDC_DUTY_MAX;

    ledc_channel_config_t ccfg = {
        .speed_mode = BL_LEDC_MODE,
        .channel    = BL_LEDC_CHANNEL,
        .timer_sel  = BL_LEDC_TIMER,
        .intr_type  = LEDC_INTR_DISABLE,
        .gpio_num   = s_bl_pin,
        .duty       = duty,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ccfg));
}

static void backlight_set(int percent)
{
    if (s_bl_pin < 0) return;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    uint32_t on_duty = (percent * BL_LEDC_DUTY_MAX) / 100;
    uint32_t duty    = s_bl_active_lo ? (BL_LEDC_DUTY_MAX - on_duty) : on_duty;

    ledc_set_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL, duty);
    ledc_update_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL);
}

/* ---------------- Reset ---------------- */

/* Active-low hardware reset for the Waveshare ESP32-S3-Touch-LCD-3.49.
 *
 * Pre-condition: RST is already configured as an output and driven
 * HIGH by the caller; this lets the panel VCC stabilise before we
 * pulse reset.
 *
 * Two LOW pulses of 250 ms each with a 30 ms HIGH gap, followed by
 * a 30 ms settle. Matches the proven clackups/draftling driver.
 * The official Waveshare ESP-IDF reference firmware uses a 250 ms
 * pulse; a 10 ms pulse (the default in the managed component) is
 * sufficient on warm reset but too short on cold boot -- the
 * AXS15231B's internal POR does not complete and the subsequent
 * vendor-register writes do not latch, leaving the panel black
 * until the user presses RESET. A second pulse with a HIGH gap in
 * between makes the wake/reset path independent of whatever state
 * the previous session left the controller in. The cost is ~280 ms
 * of extra boot latency. */
static void hw_reset(int rst_pin)
{
    if (rst_pin < 0) return;
    vTaskDelay(pdMS_TO_TICKS(30));
    gpio_set_level((gpio_num_t)rst_pin, 0);
    vTaskDelay(pdMS_TO_TICKS(250));
    gpio_set_level((gpio_num_t)rst_pin, 1);
    vTaskDelay(pdMS_TO_TICKS(30));
    gpio_set_level((gpio_num_t)rst_pin, 0);
    vTaskDelay(pdMS_TO_TICKS(250));
    gpio_set_level((gpio_num_t)rst_pin, 1);
    vTaskDelay(pdMS_TO_TICKS(30));
}

/* ---------------- Vendor init sequence ----------------
 *
 * Waveshare ESP32-S3-Touch-LCD-3.49 specific AXS15231B vendor
 * register block. Ported verbatim from the proven
 * clackups/draftling driver (display_axs15231b.cpp,
 * axs15231b_init_sequence()), which itself cross-references the
 * IDF-native driver at thingsapart/esp32_lcd_controllers and the
 * Arduino_GFX AXS15231B Type-1 reference and corrects known panel-
 * specific quirks.
 *
 * The critical first command is the `0xBB` UNLOCK with magic
 * bytes `0x5A 0xA5`: the AXS15231B's vendor registers (0xA0,
 * 0xA2, 0xD0, ...) are write-protected after reset and silently
 * drop every write until this unlock has been received. The
 * Arduino_GFX Type-1 sequence omits this unlock, which is what
 * causes panels to power up with all vendor regs at their
 * defaults -- producing a brief flash of garbage on display-on,
 * then nothing. The all-zero `0xBB` issued near the end of this
 * block re-locks the registers once the panel is configured.
 *
 * MADCTL is set to 0x00: not every AXS15231B silicon revision
 * honours the MV (row/column swap) bit. Boards mounted in
 * portrait whose application wants landscape opt in to software
 * rotation via board_display_t.swap_xy, which the flush path
 * honours by transposing per-row into the panel scratch buffer
 * and addressing the panel in its native portrait coords.
 *
 * COLMOD is intentionally NOT sent: the vendor 0xA0 block above
 * already sets the panel to 16 bpp RGB565 in QSPI mode, and
 * issuing a MIPI COLMOD after the vendor regs were configured
 * was observed to leave the panel in an intermediate state where
 * it accepted display-on but did not latch streamed pixel data.
 */

static const uint8_t init_bb_unlock[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5A, 0xA5
};

static const uint8_t init_a0[] = {
    0xC0, 0x10, 0x00, 0x02, 0x00, 0x00, 0x04, 0x3F,
    0x20, 0x05, 0x3F, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const uint8_t init_a2[] = {
    0x30, 0x3C, 0x24, 0x14, 0xD0, 0x20, 0xFF, 0xE0,
    0x40, 0x19, 0x80, 0x80, 0x80, 0x20, 0xF9, 0x10,
    0x02, 0xFF, 0xFF, 0xF0, 0x90, 0x01, 0x32, 0xA0,
    0x91, 0xE0, 0x20, 0x7F, 0xFF, 0x00, 0x5A
};

static const uint8_t init_d0[] = {
    0xE0, 0x40, 0x51, 0x24, 0x08, 0x05, 0x10, 0x01,
    0x20, 0x15, 0x42, 0xC2, 0x22, 0x22, 0xAA, 0x03,
    0x10, 0x12, 0x60, 0x14, 0x1E, 0x51, 0x15, 0x00,
    0x8A, 0x20, 0x00, 0x03, 0x3A, 0x12
};

static const uint8_t init_a3[] = {
    0xA0, 0x06, 0xAA, 0x00, 0x08, 0x02, 0x0A, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x00, 0x55, 0x55
};

static const uint8_t init_c1[] = {
    0x31, 0x04, 0x02, 0x02, 0x71, 0x05, 0x24, 0x55,
    0x02, 0x00, 0x41, 0x00, 0x53, 0xFF, 0xFF, 0xFF,
    0x4F, 0x52, 0x00, 0x4F, 0x52, 0x00, 0x45, 0x3B,
    0x0B, 0x02, 0x0D, 0x00, 0xFF, 0x40
};

static const uint8_t init_c3[] = {
    0x00, 0x00, 0x00, 0x50, 0x03, 0x00, 0x00, 0x00,
    0x01, 0x80, 0x01
};

static const uint8_t init_c4[] = {
    0x00, 0x24, 0x33, 0x80, 0x00, 0xEA, 0x64, 0x32,
    0xC8, 0x64, 0xC8, 0x32, 0x90, 0x90, 0x11, 0x06,
    0xDC, 0xFA, 0x00, 0x00, 0x80, 0xFE, 0x10, 0x10,
    0x00, 0x0A, 0x0A, 0x44, 0x50
};

static const uint8_t init_c5[] = {
    0x18, 0x00, 0x00, 0x03, 0xFE, 0x3A, 0x4A, 0x20,
    0x30, 0x10, 0x88, 0xDE, 0x0D, 0x08, 0x0F, 0x0F,
    0x01, 0x3A, 0x4A, 0x20, 0x10, 0x10, 0x00
};

static const uint8_t init_c6[] = {
    0x05, 0x0A, 0x05, 0x0A, 0x00, 0xE0, 0x2E, 0x0B,
    0x12, 0x22, 0x12, 0x22, 0x01, 0x03, 0x00, 0x3F,
    0x6A, 0x18, 0xC8, 0x22
};

static const uint8_t init_c7[] = {
    0x50, 0x32, 0x28, 0x00, 0xA2, 0x80, 0x8F, 0x00,
    0x80, 0xFF, 0x07, 0x11, 0x9C, 0x67, 0xFF, 0x24,
    0x0C, 0x0D, 0x0E, 0x0F
};

static const uint8_t init_c9[] = { 0x33, 0x44, 0x44, 0x01 };

static const uint8_t init_cf[] = {
    0x2C, 0x1E, 0x88, 0x58, 0x13, 0x18, 0x56, 0x18,
    0x1E, 0x68, 0x88, 0x00, 0x65, 0x09, 0x22, 0xC4,
    0x0C, 0x77, 0x22, 0x44, 0xAA, 0x55, 0x08, 0x08,
    0x12, 0xA0, 0x08
};

static const uint8_t init_d5[] = {
    0x40, 0x8E, 0x8D, 0x01, 0x35, 0x04, 0x92, 0x74,
    0x04, 0x92, 0x74, 0x04, 0x08, 0x6A, 0x04, 0x46,
    0x03, 0x03, 0x03, 0x03, 0x82, 0x01, 0x03, 0x00,
    0xE0, 0x51, 0xA1, 0x00, 0x00, 0x00
};

static const uint8_t init_d6[] = {
    0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE,
    0x93, 0x00, 0x01, 0x83, 0x07, 0x07, 0x00, 0x07,
    0x07, 0x00, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    0x00, 0x84, 0x00, 0x20, 0x01, 0x00
};

static const uint8_t init_d7[] = {
    0x03, 0x01, 0x0B, 0x09, 0x0F, 0x0D, 0x1E, 0x1F,
    0x18, 0x1D, 0x1F, 0x19, 0x40, 0x8E, 0x04, 0x00,
    0x20, 0xA0, 0x1F
};

static const uint8_t init_d8[] = {
    0x02, 0x00, 0x0A, 0x08, 0x0E, 0x0C, 0x1E, 0x1F,
    0x18, 0x1D, 0x1F, 0x19
};

static const uint8_t init_d9[] = {
    0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F,
    0x1F, 0x1F, 0x1F, 0x1F
};

static const uint8_t init_dd[] = {
    0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F,
    0x1F, 0x1F, 0x1F, 0x1F
};

static const uint8_t init_df[] = {
    0x44, 0x73, 0x4B, 0x69, 0x00, 0x0A, 0x02, 0x90
};

static const uint8_t init_e0[] = {
    0x3B, 0x28, 0x10, 0x16, 0x0C, 0x06, 0x11, 0x28,
    0x5C, 0x21, 0x0D, 0x35, 0x13, 0x2C, 0x33, 0x28, 0x0D
};

static const uint8_t init_e1[] = {
    0x37, 0x28, 0x10, 0x16, 0x0B, 0x06, 0x11, 0x28,
    0x5C, 0x21, 0x0D, 0x35, 0x14, 0x2C, 0x33, 0x28, 0x0F
};

static const uint8_t init_e2[] = {
    0x3B, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x35,
    0x44, 0x32, 0x0C, 0x14, 0x14, 0x36, 0x3A, 0x2F, 0x0D
};

static const uint8_t init_e3[] = {
    0x37, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x35,
    0x44, 0x32, 0x0C, 0x14, 0x14, 0x36, 0x32, 0x2F, 0x0F
};

static const uint8_t init_e4[] = {
    0x3B, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x39,
    0x44, 0x2E, 0x0C, 0x14, 0x14, 0x36, 0x3A, 0x2F, 0x0D
};

static const uint8_t init_e5[] = {
    0x37, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x39,
    0x44, 0x2E, 0x0C, 0x14, 0x14, 0x36, 0x3A, 0x2F, 0x0F
};

static const uint8_t init_a4_1[] = {
    0x85, 0x85, 0x95, 0x82, 0xAF, 0xAA, 0xAA, 0x80,
    0x10, 0x30, 0x40, 0x40, 0x20, 0xFF, 0x60, 0x30
};

static const uint8_t init_a4_2[] = { 0x85, 0x85, 0x95, 0x85 };

static const uint8_t init_bb_lock[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const uint8_t init_madctl[]  = { 0x00 };
static const uint8_t init_te[]      = { 0x00 };

/* Waveshare 3.49" vendor init table. The managed component sends
 * SLPOUT, MADCTL and COLMOD itself before walking this table; we
 * deliberately override MADCTL here (the warning the component
 * logs is expected) and rely on its COLMOD as-is. */
static const axs15231b_lcd_init_cmd_t waveshare_349_init_cmds[] = {
    { 0xBB, (void *)init_bb_unlock, sizeof(init_bb_unlock), 0 },
    { 0xA0, (void *)init_a0,        sizeof(init_a0),        0 },
    { 0xA2, (void *)init_a2,        sizeof(init_a2),        0 },
    { 0xD0, (void *)init_d0,        sizeof(init_d0),        0 },
    { 0xA3, (void *)init_a3,        sizeof(init_a3),        0 },
    { 0xC1, (void *)init_c1,        sizeof(init_c1),        0 },
    { 0xC3, (void *)init_c3,        sizeof(init_c3),        0 },
    { 0xC4, (void *)init_c4,        sizeof(init_c4),        0 },
    { 0xC5, (void *)init_c5,        sizeof(init_c5),        0 },
    { 0xC6, (void *)init_c6,        sizeof(init_c6),        0 },
    { 0xC7, (void *)init_c7,        sizeof(init_c7),        0 },
    { 0xC9, (void *)init_c9,        sizeof(init_c9),        0 },
    { 0xCF, (void *)init_cf,        sizeof(init_cf),        0 },
    { 0xD5, (void *)init_d5,        sizeof(init_d5),        0 },
    { 0xD6, (void *)init_d6,        sizeof(init_d6),        0 },
    { 0xD7, (void *)init_d7,        sizeof(init_d7),        0 },
    { 0xD8, (void *)init_d8,        sizeof(init_d8),        0 },
    { 0xD9, (void *)init_d9,        sizeof(init_d9),        0 },
    { 0xDD, (void *)init_dd,        sizeof(init_dd),        0 },
    { 0xDF, (void *)init_df,        sizeof(init_df),        0 },
    { 0xE0, (void *)init_e0,        sizeof(init_e0),        0 },
    { 0xE1, (void *)init_e1,        sizeof(init_e1),        0 },
    { 0xE2, (void *)init_e2,        sizeof(init_e2),        0 },
    { 0xE3, (void *)init_e3,        sizeof(init_e3),        0 },
    { 0xE4, (void *)init_e4,        sizeof(init_e4),        0 },
    { 0xE5, (void *)init_e5,        sizeof(init_e5),        0 },
    { 0xA4, (void *)init_a4_1,      sizeof(init_a4_1),      0 },
    { 0xA4, (void *)init_a4_2,      sizeof(init_a4_2),      0 },
    { 0xBB, (void *)init_bb_lock,   sizeof(init_bb_lock),   0 },
    /* MADCTL: 0x00 (RGB, no mirror/swap); MV bit is unreliable on
     * this panel so rotation is done in software. */
    { 0x36, (void *)init_madctl,    sizeof(init_madctl),    0 },
    /* TE on, V-blank only. */
    { 0x35, (void *)init_te,        sizeof(init_te),        0 },
    /* Normal Display Mode On. */
    { 0x13, NULL,                   0,                      0 },
};

/* ---------------- Flush ---------------- */

static void flush_bbox(int x0, int y0, int x1, int y1)
{
    if (!s_panel_handle || !s_fb) return;
    if (x0 > x1 || y0 > y1) return;

    if (s_swap_xy) {
        /* 90 deg CW software rotation. Logical (lx, ly) maps to
         * panel (px, py) where:
         *      px = (s_h - 1) - ly      (0 .. s_h - 1)
         *      py = lx                  (0 .. s_w - 1)
         *
         * The AXS15231B silently ignores RASET over QSPI and the
         * implicit row pointer is undefined right after init, so
         * partial-rectangle flushes are unreliable in this mode.
         * Always flush the full panel; the cost (s_w * s_h * 2
         * bytes at 40 MHz QSPI) is below the UI's flush cadence
         * and matches the proven clackups/draftling driver. */
        (void)x0; (void)y0; (void)x1; (void)y1;

        const int pw = s_h;   /* panel native width  (e.g. 172) */
        const int ph = s_w;   /* panel native height (e.g. 640) */

        /* Transpose s_fb (s_w x s_h, landscape) -> s_panel_buf
         * (pw x ph, portrait) with a 90 deg CW rotation. */
        for (int prow = 0; prow < ph; ++prow) {
            int lx = prow;                          /* logical column */
            const uint16_t *col_base = s_fb + lx;
            uint16_t *dst = s_panel_buf + (size_t)prow * pw;
            for (int pcol = 0; pcol < pw; ++pcol) {
                int ly = (s_h - 1) - pcol;          /* logical row */
                dst[pcol] = col_base[(size_t)ly * s_w];
            }
        }

        /* Single full-panel draw. The managed component sees
         * y_start == 0 and emits RAMWR (0x2C), which anchors the
         * panel's implicit row pointer at row 0 -- correct on the
         * first flush after init and harmless on every subsequent
         * flush. CASET addresses the panel's full column range. */
        esp_lcd_panel_draw_bitmap(s_panel_handle, 0, 0, pw, ph, s_panel_buf);
        return;
    }

    /* Direct (no rotation) path. Kept for any future natively-
     * landscape AXS panel. */
    int w = x1 - x0 + 1;
    int h = y1 - y0 + 1;

    if (x0 == 0 && y0 == 0 && w == s_w && h == s_h) {
        esp_lcd_panel_draw_bitmap(s_panel_handle, 0, 0, s_w, s_h, s_fb);
        return;
    }

    static uint16_t *scratch = NULL;
    static int       scratch_w = 0;
    if (scratch_w < w) {
        free(scratch);
        scratch = heap_caps_malloc((size_t)w * sizeof(uint16_t),
                                   MALLOC_CAP_DMA);
        scratch_w = scratch ? w : 0;
    }
    if (!scratch) {
        esp_lcd_panel_draw_bitmap(s_panel_handle, 0, 0, s_w, s_h, s_fb);
        return;
    }
    for (int yy = 0; yy < h; ++yy) {
        const uint16_t *src = &s_fb[(y0 + yy) * s_w + x0];
        memcpy(scratch, src, (size_t)w * sizeof(uint16_t));
        esp_lcd_panel_draw_bitmap(s_panel_handle,
                                  x0, y0 + yy,
                                  x0 + w, y0 + yy + 1,
                                  scratch);
    }
}

/* ---------------- Init ---------------- */

void display_axs15231b_init(void)
{
    const board_t *b = board_get();
    s_w            = b->display.width;
    s_h            = b->display.height;
    s_bl_pin       = b->display.bl;
    s_bl_active_lo = b->display.bl_active_low;
    s_swap_xy      = b->display.swap_xy;

    /* 1. Reset GPIO -- configure and pre-drive HIGH BEFORE the SPI
     * bus / panel come up. Without this, on cold boot the GPIO
     * output register defaults to 0, so RST would be held LOW for
     * the entire SPI-bus / heap setup that follows (~tens of ms) --
     * holding the panel in reset across the moment its VCC
     * stabilises. */
    if (b->display.rst >= 0) {
        gpio_config_t g = {
            .intr_type    = GPIO_INTR_DISABLE,
            .mode         = GPIO_MODE_OUTPUT,
            .pin_bit_mask = (1ULL << b->display.rst),
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .pull_up_en   = GPIO_PULLUP_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&g));
        gpio_set_level((gpio_num_t)b->display.rst, 1);
    }

    /* 2. QSPI bus. max_transfer_sz is sized to the larger of the
     * two axes because in rotation mode the panel is addressed in
     * native portrait orientation (s_h x s_w pixels). */
    spi_bus_config_t buscfg = AXS15231B_PANEL_BUS_QSPI_CONFIG(
        b->display.sck,
        b->display.d0,
        b->display.d1,
        b->display.d2,
        b->display.d3,
        s_w * s_h * sizeof(uint16_t));
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    /* 3. Panel IO. */
    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_spi_config_t io_cfg = AXS15231B_PANEL_IO_QSPI_CONFIG(
        b->display.cs, NULL, NULL);
    io_cfg.pclk_hz = LCD_PIXEL_CLOCK_HZ;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_cfg, &io));

    /* 4. Long hardware reset BEFORE handing the panel to the
     * managed component. */
    hw_reset(b->display.rst);

    /* 5. Panel handle. We pass our Waveshare-specific vendor init
     * table and set reset_gpio_num=-1 so the managed component
     * skips its own (too short, 10 ms) hardware reset on top of
     * the one we just did. esp_lcd_panel_init() will still issue
     * SLPOUT and the vendor init below. */
    axs15231b_vendor_config_t vendor = {
        .init_cmds      = waveshare_349_init_cmds,
        .init_cmds_size = sizeof(waveshare_349_init_cmds) /
                          sizeof(waveshare_349_init_cmds[0]),
        .flags = {
            .use_qspi_interface = 1,
        },
    };
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = -1,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config  = &vendor,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_axs15231b(io, &panel_cfg, &s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel_handle));   /* SWRESET via bus */
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel_handle));    /* runs vendor init */
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel_handle, true));

    /* 6. Framebuffers in PSRAM. s_fb is the logical landscape
     * buffer the display front-end draws into; s_panel_buf is the
     * panel-native portrait buffer we transpose into on every
     * flush. Both have the same pixel count (s_w * s_h) but
     * differ in orientation. */
    size_t fb_bytes = (size_t)s_w * s_h * sizeof(uint16_t);
    s_fb        = heap_caps_malloc(fb_bytes, MALLOC_CAP_SPIRAM);
    s_panel_buf = heap_caps_malloc(fb_bytes, MALLOC_CAP_SPIRAM);
    if (!s_fb || !s_panel_buf) {
        ESP_LOGE(TAG, "Failed to allocate %u-byte framebuffer(s) in PSRAM",
                 (unsigned)fb_bytes);
        abort();
    }
    memset(s_fb, 0, fb_bytes);
    memset(s_panel_buf, 0, fb_bytes);

    /* 7. Backlight. */
    backlight_init();

    /* 8. Hand the buffer to the front-end. */
    display_register_backend(s_fb, s_w, s_h, flush_bbox, backlight_set);

    ESP_LOGI(TAG, "AXS15231B up: %dx%d (swap_xy=%d), fb=%u B in PSRAM (x2)",
             s_w, s_h, (int)s_swap_xy, (unsigned)fb_bytes);
}

#endif /* CONFIG_BOARD_HAS_DISPLAY_AXS15231B */
