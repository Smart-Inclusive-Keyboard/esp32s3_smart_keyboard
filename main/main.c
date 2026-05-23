/*
 * esp32-smart-keyboard / main
 *
 * Boot sequence:
 *   1. nvs_flash_init()  -- needed by BLE bonding + UI prefs
 *   2. board_init()      -- panel reset, capability surface
 *   3. display_init()    -- framebuffer + AXS15231B QSPI driver
 *   4. Splash screen
 *   5. keyboard_ui_init() + start UI task
 *   6. ble_hid_init()    -- NimBLE host + GATT
 *   7. narrator_init()   -- audio backend if available
 *   8. gamepad_i2c_start()
 *   9. input_router_start()
 */

#include <stdio.h>

#include <esp_log.h>
#include <esp_err.h>
#include <nvs_flash.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "board.h"
#include "display.h"
#include "theme.h"
#include "fonts.h"
#include "kb_layout.h"
#include "keyboard_ui.h"
#include "gamepad_i2c.h"
#include "input_router.h"
#include "ble_hid.h"
#include "narrator.h"

static const char *TAG = "main";

#define FW_VERSION "0.1.0-dev"

static void splash(void)
{
    const theme_t *th = theme_active();
    int w = display_width();
    int h = display_height();
    display_clear(th->win_bg);

    const board_t *b = board_get();
    const char *line1 = "SmartKeyboard";
    const char *line3 = "BLE: pairing...";
    char        line2[40];
    snprintf(line2, sizeof(line2), "fw %s", FW_VERSION);

    int s = 3;  /* 3x = 24-px tall text */
    int gw = 8 * s;
    int y = (h - 3 * gw - 2 * 6) / 2;

    int l1x = (w - (int)strlen(line1) * gw) / 2;
    int l2x = (w - (int)strlen(line2) * 8) / 2;
    int l3x = (w - (int)strlen(line3) * 8) / 2;

    display_draw_string(l1x, y,                line1, s, th->key_label, th->win_bg, true);
    display_draw_string(l2x, y + gw + 8,        line2, 1, th->status_ind_fg, th->win_bg, true);
    display_draw_string(l3x, y + gw + 8 + 16,   line3, 1, th->status_ind_fg, th->win_bg, true);

    /* Board name as a tiny footer. */
    int len = (int)strlen(b->name);
    display_draw_string((w - len * 8) / 2, h - 12, b->name, 1,
                        th->status_ind_fg, th->win_bg, true);
    display_flush();
}

static void on_ble_status(const char *text, bool connected)
{
    ESP_LOGI(TAG, "%s", text);
    keyboard_ui_set_ble_status(text, connected);
}

void app_main(void)
{
    /* 1. NVS. */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    /* 2. Board + display. */
    board_init();
    display_init();
    display_set_backlight(80);

    /* 3. Splash. */
    splash();
    vTaskDelay(pdMS_TO_TICKS(1200));

    /* 4. UI. */
    keyboard_ui_init();
    keyboard_ui_start_task();

    /* 5. BLE. */
    ble_hid_init(on_ble_status);

    /* 6. Audio + narrator (no-ops on boards without speaker). */
    narrator_init();

    /* 7. Gamepad + router. */
    QueueHandle_t q = gamepad_i2c_start();
    input_router_start(q);

    ESP_LOGI(TAG, "boot complete; firmware %s on %s",
             FW_VERSION, board_get()->name);
}
