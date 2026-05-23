/*
 * AXS15231B QSPI display backend.
 *
 * Wraps Espressif's managed component `espressif/esp_lcd_axs15231b`
 * (declared in components/display/idf_component.yml). We allocate
 * an RGB565 framebuffer in PSRAM and use the panel handle's
 * draw_bitmap entry point as our flush primitive.
 *
 * Backlight is driven by LEDC PWM. The Waveshare
 * ESP32-S3-Touch-LCD-3.49 wires the BL pin active-LOW, so we
 * invert the duty when board.display.bl_active_low is set
 * (matches Waveshare's lcd_bl_pwm_bsp.c reference firmware).
 */

#include "sdkconfig.h"

#if CONFIG_BOARD_HAS_DISPLAY_AXS15231B

#include <string.h>

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
 * lcd_bl_pwm_bsp.c reference firmware: 50 kHz, 8-bit, RC_FAST). */
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

static int                   s_bl_pin       = -1;
static bool                  s_bl_active_lo = false;
static esp_lcd_panel_handle_t s_panel_handle = NULL;
static uint16_t              *s_fb           = NULL;
static int                    s_w            = 0;
static int                    s_h            = 0;

/* Forward declarations of the framebuffer integration hooks
 * implemented in display.c. */
extern void display_register_backend(uint16_t *fb, int w, int h,
                                     void (*flush)(int, int, int, int),
                                     void (*set_backlight)(int));

static void backlight_init(void)
{
    if (s_bl_pin < 0) return;

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

static void flush_bbox(int x0, int y0, int x1, int y1)
{
    if (!s_panel_handle || !s_fb) return;
    if (x0 > x1 || y0 > y1) return;

    int w = x1 - x0 + 1;
    int h = y1 - y0 + 1;

    /* Fast path: the dirty bbox is the whole screen. */
    if (x0 == 0 && y0 == 0 && w == s_w && h == s_h) {
        esp_lcd_panel_draw_bitmap(s_panel_handle, 0, 0, s_w, s_h, s_fb);
        return;
    }

    /* Otherwise pack the dirty rectangle row-by-row into a
     * scratch line buffer and push it. We DMA the full rows so
     * we avoid per-row allocation cost; this is fine for the
     * UI's typical refresh sizes (a few key cells per frame). */
    static uint16_t *scratch = NULL;
    static int       scratch_w = 0;
    if (scratch_w < w) {
        free(scratch);
        scratch = heap_caps_malloc((size_t)w * sizeof(uint16_t),
                                   MALLOC_CAP_DMA);
        scratch_w = scratch ? w : 0;
    }
    if (!scratch) {
        /* Out of internal DMA RAM -- fall back to a whole-frame push. */
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

void display_axs15231b_init(void)
{
    const board_t *b = board_get();
    s_w = b->display.width;
    s_h = b->display.height;
    s_bl_pin       = b->display.bl;
    s_bl_active_lo = b->display.bl_active_low;

    /* 1. QSPI bus. */
    spi_bus_config_t buscfg = AXS15231B_PANEL_BUS_QSPI_CONFIG(
        b->display.sck,
        b->display.d0,
        b->display.d1,
        b->display.d2,
        b->display.d3,
        s_w * s_h * sizeof(uint16_t));
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    /* 2. Panel IO. */
    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_spi_config_t io_cfg = AXS15231B_PANEL_IO_QSPI_CONFIG(
        b->display.cs, NULL, NULL);
    io_cfg.pclk_hz = LCD_PIXEL_CLOCK_HZ;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_cfg, &io));

    /* 3. Panel handle. */
    axs15231b_vendor_config_t vendor = {
        .flags = {
            .use_qspi_interface = 1,
        },
    };
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = b->display.rst,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config  = &vendor,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_axs15231b(io, &panel_cfg, &s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel_handle, true));
    if (b->display.swap_xy) {
        esp_lcd_panel_swap_xy(s_panel_handle, true);
    }

    /* 4. Framebuffer in PSRAM. */
    size_t fb_bytes = (size_t)s_w * s_h * sizeof(uint16_t);
    s_fb = heap_caps_malloc(fb_bytes, MALLOC_CAP_SPIRAM);
    if (!s_fb) {
        ESP_LOGE(TAG, "Failed to allocate %u-byte framebuffer in PSRAM",
                 (unsigned)fb_bytes);
        abort();
    }
    memset(s_fb, 0, fb_bytes);

    /* 5. Backlight. */
    backlight_init();

    /* 6. Hand the buffer to the front-end. */
    display_register_backend(s_fb, s_w, s_h, flush_bbox, backlight_set);

    ESP_LOGI(TAG, "AXS15231B up: %dx%d, fb=%u B in PSRAM",
             s_w, s_h, (unsigned)fb_bytes);
}

#endif /* CONFIG_BOARD_HAS_DISPLAY_AXS15231B */
