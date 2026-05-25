/*
 * AXS15231B QSPI display backend (raw-QSPI implementation).
 *
 * Ported from the proven clackups/draftling driver
 * (components/display/display_axs15231b.cpp) -- the only AXS15231B
 * implementation we have verified on real hardware (Waveshare
 * ESP32-S3-Touch-LCD-3.49, 172w x 640h native, mounted landscape
 * 640x172). The earlier attempt to use Espressif's managed
 * `esp_lcd_axs15231b` component left the panel black at boot
 * because that component's draw path emits one esp_lcd transaction
 * per call, which pulses CS in between; the AXS15231B's QSPI
 * memory-write protocol requires CS LOW across the whole pixel
 * burst (the controller's column/page pointer resets on every CS
 * rising edge, and only the first chunk lands at the CASET/RAMWR
 * origin) and it expects QSPI-specific preambles
 * (`0x02 / 0x00<cmd>00` for vendor commands, `0x32 / 0x003C00`
 * for pixel writes) on all four data lines.
 *
 * Architecture
 * ------------
 *   - One QSPI bus on SPI2_HOST, 40 MHz, half-duplex.
 *   - CS pin is driven manually (`spics_io_num = -1` to the spi
 *     driver). spi_send_cmd() drops it for one vendor frame at a
 *     time; display_flush() drops it for the whole pixel burst.
 *   - Vendor / DCS commands: cmd byte 0x02, 24-bit addr
 *     0x00 <reg> 0x00, both phases sent in 4-line mode
 *     (SPI_TRANS_MULTILINE_CMD|_ADDR). Optional parameter bytes go
 *     on a single line.
 *   - Pixel writes: cmd byte 0x32, 24-bit addr 0x00 <operand> 0x00,
 *     data in 4-line mode. operand = 0x2C (RAMWR) anchors the
 *     write pointer at the CASET origin -- used for the first
 *     chunk; subsequent chunks of the same burst use operand 0x3C
 *     (RAMWRC) with zero cmd/addr/dummy bits (VARIABLE_*) so the
 *     panel keeps streaming from the current pointer position.
 *   - Per-row DMA scratch buffer in *internal* DMA-capable RAM
 *     (MALLOC_CAP_DMA | MALLOC_CAP_8BIT). The host framebuffer
 *     stays in PSRAM; we copy + byte-swap one panel row at a time
 *     into the scratch before each spi_send. The ESP32-S3 SPI DMA
 *     can read PSRAM via EDMA but only at certain alignments /
 *     bus configurations; the per-row copy keeps the wire format
 *     deterministic and matches the reference.
 *
 * 90 deg CW software rotation
 * ---------------------------
 * The AXS15231B silently ignores the MADCTL MV bit on this panel
 * and RASET (0x2B) is silently ignored over QSPI (the row pointer
 * is tracked implicitly). So a logical landscape framebuffer
 * (640 x 172) is rotated CW into the panel's native portrait
 * orientation (172 x 640) at flush time, and the dirty bbox is
 * used only as an "anything to flush?" gate -- the streamed
 * payload is always the full panel. Cost: 640*172*2 = 220 KB at
 * 40 MHz QSPI is ~12 ms, well below the UI's flush cadence.
 *
 * Backlight
 * ---------
 * LEDC PWM, 50 kHz, 8-bit, RC_FAST clock (survives APB-clock
 * changes when BLE / Wi-Fi enable DFS). The BL pin is
 * pre-configured as a plain GPIO output and driven to its "on"
 * level BEFORE LEDC takes over, so the on-board boost circuit
 * cannot latch off during the power-on handoff window. The
 * Waveshare 3.49 wires BL active-LOW (board.display.bl_active_low
 * is true), so the duty is inverted in that mode.
 */

#include "sdkconfig.h"

#if CONFIG_BOARD_HAS_DISPLAY_AXS15231B

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_check.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <driver/gpio.h>
#include <driver/ledc.h>
#include <driver/spi_master.h>

#include "board.h"
#include "display.h"

static const char *TAG = "display_axs";

/* ---------------- Backlight (LEDC PWM) ---------------- */

#define BL_LEDC_TIMER       LEDC_TIMER_3
#define BL_LEDC_MODE        LEDC_LOW_SPEED_MODE
#define BL_LEDC_CHANNEL     LEDC_CHANNEL_1
#define BL_LEDC_DUTY_RES    LEDC_TIMER_8_BIT
#define BL_LEDC_DUTY_MAX    ((1 << 8) - 1)
#define BL_LEDC_FREQ_HZ     50000

/* ---------------- SPI / QSPI constants ---------------- */

#define AXS_SPI_HOST            SPI2_HOST
#define AXS_SPI_CLOCK_HZ        (40 * 1000 * 1000)

/* QSPI command preamble for vendor / DCS writes:
 *   cmd  = 0x02
 *   addr = 0x00 <reg> 0x00  (24 bits, big-endian as ESP-IDF emits)
 * Both phases MUST be sent on all four data lines or the panel
 * silently drops the frame. */
#define AXS_CMD_PREAMBLE        0x02

/* QSPI preamble for pixel ("memory write") bursts:
 *   cmd  = 0x32
 *   addr = 0x00 <operand> 0x00
 *   data in QIO mode
 *
 * operand 0x2C -> RAMWR : anchors implicit pointer at CASET origin
 * operand 0x3C -> RAMWRC: continues from current pointer
 *
 * RAMWR is mandatory on the first chunk of every burst (the row
 * pointer is undefined after init and would otherwise be wherever
 * the previous frame left it). RAMWRC is used for continuation
 * chunks of the same CS-low burst. */
#define AXS_DATA_PREAMBLE_BYTE  0x32
#define AXS_QSPI_MEMWR_RAMWR    0x2C
#define AXS_QSPI_MEMWR_RAMWRC   0x3C

/* ---------------- Driver state ---------------- */

static int                    s_bl_pin       = -1;
static bool                   s_bl_active_lo = false;
static int                    s_cs_pin       = -1;
static int                    s_rst_pin      = -1;
static spi_device_handle_t    s_spi          = NULL;
static int                    s_w            = 0;     /* logical width  */
static int                    s_h            = 0;     /* logical height */
static bool                   s_swap_xy      = false;
static uint16_t              *s_fb           = NULL;  /* logical landscape, PSRAM */
static uint8_t               *s_row_buf      = NULL;  /* DMA scratch, internal RAM */

/* Forward declaration of the framebuffer integration hook
 * implemented in display.c. */
extern void display_register_backend(uint16_t *fb, int w, int h,
                                     void (*flush)(int, int, int, int),
                                     void (*set_backlight)(int));

/* ---------------- Low-level SPI helpers ---------------- */

static inline void cs_low(void)
{
    if (s_cs_pin >= 0) gpio_set_level((gpio_num_t)s_cs_pin, 0);
}

static inline void cs_high(void)
{
    if (s_cs_pin >= 0) gpio_set_level((gpio_num_t)s_cs_pin, 1);
}

/* Send a vendor / DCS command (with optional parameter bytes).
 *
 * Frame layout on the wire (within one CS-low pulse):
 *   cmd  phase ( 8 bits, QSPI mode): 0x02
 *   addr phase (24 bits, QSPI mode): 0x00, reg, 0x00
 *   data phase (n_params*8 bits, single-line): parameter bytes
 *
 * Marking cmd/addr as MULTILINE (4-line) is mandatory: the
 * AXS15231B does not accept these phases on a single line and the
 * frame is silently ignored if MULTILINE is not set. */
static void spi_send_cmd(uint8_t cmd, const uint8_t *params, size_t n_params)
{
    cs_low();

    spi_transaction_ext_t tx = {0};
    tx.base.flags = SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR;
    tx.base.cmd   = AXS_CMD_PREAMBLE;
    tx.base.addr  = ((uint32_t)cmd) << 8;

    if (n_params > 0) {
        tx.base.length    = n_params * 8;
        tx.base.tx_buffer = params;
    }

    ESP_ERROR_CHECK(spi_device_polling_transmit(s_spi, (spi_transaction_t *)&tx));

    cs_high();
}

/* First chunk of a pixel-write burst. Carries the QSPI memory-write
 * preamble (cmd 0x32, addr 0x00 <operand> 0x00) and switches the
 * data phase into 4-line mode. Caller is responsible for driving
 * CS LOW before this call and keeping it LOW until the last
 * continuation chunk has been sent. */
static void spi_send_pixels_first(uint8_t operand, const uint8_t *data,
                                  size_t n_bytes)
{
    spi_transaction_ext_t tx = {0};
    tx.base.flags     = SPI_TRANS_MODE_QIO;
    tx.base.cmd       = AXS_DATA_PREAMBLE_BYTE;
    tx.base.addr      = ((uint32_t)operand) << 8;
    tx.base.length    = n_bytes * 8;
    tx.base.tx_buffer = data;

    ESP_ERROR_CHECK(spi_device_polling_transmit(s_spi, (spi_transaction_t *)&tx));
}

/* Continuation chunk of a pixel-write burst. No cmd / addr /
 * dummy phases (the VARIABLE_* flags + the corresponding _bits=0
 * suppress the device-default 8-bit cmd / 24-bit addr that
 * spi_send_pixels_first emitted on the first chunk). */
static void spi_send_pixels_cont(const uint8_t *data, size_t n_bytes)
{
    spi_transaction_ext_t tx = {0};
    tx.base.flags = SPI_TRANS_MODE_QIO |
                    SPI_TRANS_VARIABLE_CMD |
                    SPI_TRANS_VARIABLE_ADDR |
                    SPI_TRANS_VARIABLE_DUMMY;
    tx.command_bits   = 0;
    tx.address_bits   = 0;
    tx.dummy_bits     = 0;
    tx.base.length    = n_bytes * 8;
    tx.base.tx_buffer = data;

    ESP_ERROR_CHECK(spi_device_polling_transmit(s_spi, (spi_transaction_t *)&tx));
}

/* ---------------- Reset ---------------- */

/* Two LOW pulses of 250 ms each with a 30 ms HIGH gap, followed by
 * a 30 ms settle. Matches the proven clackups/draftling driver.
 *
 * The official Waveshare ESP-IDF reference uses a single 250 ms
 * LOW pulse; a 10 ms pulse (the default in most managed components)
 * is sufficient on warm reset but too short on cold boot -- the
 * AXS15231B's internal POR does not complete and the subsequent
 * vendor-register writes do not latch, leaving the panel black
 * until the user presses RESET. The second pulse with a HIGH gap
 * makes the path independent of whatever state the previous
 * session left the controller in (notably the "stuck after deep
 * sleep" case where only a USB power cycle would otherwise
 * recover). Cost: ~280 ms of extra boot latency. */
static void hw_reset(void)
{
    if (s_rst_pin < 0) return;
    vTaskDelay(pdMS_TO_TICKS(30));
    gpio_set_level((gpio_num_t)s_rst_pin, 0);
    vTaskDelay(pdMS_TO_TICKS(250));
    gpio_set_level((gpio_num_t)s_rst_pin, 1);
    vTaskDelay(pdMS_TO_TICKS(30));
    gpio_set_level((gpio_num_t)s_rst_pin, 0);
    vTaskDelay(pdMS_TO_TICKS(250));
    gpio_set_level((gpio_num_t)s_rst_pin, 1);
    vTaskDelay(pdMS_TO_TICKS(30));
}

/* ---------------- Vendor init sequence ----------------
 *
 * Ported verbatim from the proven clackups/draftling driver
 * (axs15231b_init_sequence()).
 *
 * Key invariants:
 *   - SWRESET (0x01) FIRST: brings the controller's state machine
 *     back to POR defaults regardless of prior session state.
 *   - SLPOUT (0x11) before any vendor-register writes: on cold
 *     power-up the controller boots into sleep with analog rails
 *     unpowered; writes to power-related vendor regs (0xA0, 0xD0,
 *     0xC1, ...) ACK on the bus but do NOT latch in the analog
 *     domain until SLPOUT brings up the internal regulators.
 *   - 0xBB unlock with magic 0x5A 0xA5: the AXS15231B's vendor
 *     register window is write-protected after reset and silently
 *     drops every write until this unlock has been received.
 *   - 0xBB re-lock at the end (eight zero bytes, no magic key)
 *     closes the vendor window once the panel is configured.
 *   - MADCTL = 0x00: not every AXS15231B silicon revision honours
 *     the MV (row/column swap) bit; software rotation is used
 *     instead (see flush_bbox below).
 *   - COLMOD (0x3A) is INTENTIONALLY OMITTED: the vendor 0xA0
 *     block sets 16 bpp RGB565 in QSPI mode already, and issuing a
 *     MIPI COLMOD after the vendor block was observed to leave the
 *     panel in an intermediate state where DISPON works but pixel
 *     data does not latch.
 *   - The init terminates with a dummy 4-zero-byte 0x2C
 *     (Memory Write) which primes the panel's write pointer.
 */
static void axs15231b_init_sequence(void)
{
    /* SWRESET, then 120 ms (MIPI mandates 5 ms recovery + 120 ms
     * before SLPOUT). */
    spi_send_cmd(0x01, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(120));

    /* Early SLPOUT to bring the analog stages up before vendor
     * power-related regs are written. */
    spi_send_cmd(0x11, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(120));

    static const uint8_t init_bb_unlock[] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5A, 0xA5
    };
    spi_send_cmd(0xBB, init_bb_unlock, sizeof(init_bb_unlock));

    static const uint8_t init_a0[] = {
        0xC0, 0x10, 0x00, 0x02, 0x00, 0x00, 0x04, 0x3F,
        0x20, 0x05, 0x3F, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    spi_send_cmd(0xA0, init_a0, sizeof(init_a0));

    static const uint8_t init_a2[] = {
        0x30, 0x3C, 0x24, 0x14, 0xD0, 0x20, 0xFF, 0xE0,
        0x40, 0x19, 0x80, 0x80, 0x80, 0x20, 0xF9, 0x10,
        0x02, 0xFF, 0xFF, 0xF0, 0x90, 0x01, 0x32, 0xA0,
        0x91, 0xE0, 0x20, 0x7F, 0xFF, 0x00, 0x5A
    };
    spi_send_cmd(0xA2, init_a2, sizeof(init_a2));

    static const uint8_t init_d0[] = {
        0xE0, 0x40, 0x51, 0x24, 0x08, 0x05, 0x10, 0x01,
        0x20, 0x15, 0x42, 0xC2, 0x22, 0x22, 0xAA, 0x03,
        0x10, 0x12, 0x60, 0x14, 0x1E, 0x51, 0x15, 0x00,
        0x8A, 0x20, 0x00, 0x03, 0x3A, 0x12
    };
    spi_send_cmd(0xD0, init_d0, sizeof(init_d0));

    static const uint8_t init_a3[] = {
        0xA0, 0x06, 0xAA, 0x00, 0x08, 0x02, 0x0A, 0x04,
        0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
        0x04, 0x04, 0x04, 0x00, 0x55, 0x55
    };
    spi_send_cmd(0xA3, init_a3, sizeof(init_a3));

    static const uint8_t init_c1[] = {
        0x31, 0x04, 0x02, 0x02, 0x71, 0x05, 0x24, 0x55,
        0x02, 0x00, 0x41, 0x00, 0x53, 0xFF, 0xFF, 0xFF,
        0x4F, 0x52, 0x00, 0x4F, 0x52, 0x00, 0x45, 0x3B,
        0x0B, 0x02, 0x0D, 0x00, 0xFF, 0x40
    };
    spi_send_cmd(0xC1, init_c1, sizeof(init_c1));

    static const uint8_t init_c3[] = {
        0x00, 0x00, 0x00, 0x50, 0x03, 0x00, 0x00, 0x00,
        0x01, 0x80, 0x01
    };
    spi_send_cmd(0xC3, init_c3, sizeof(init_c3));

    static const uint8_t init_c4[] = {
        0x00, 0x24, 0x33, 0x80, 0x00, 0xEA, 0x64, 0x32,
        0xC8, 0x64, 0xC8, 0x32, 0x90, 0x90, 0x11, 0x06,
        0xDC, 0xFA, 0x00, 0x00, 0x80, 0xFE, 0x10, 0x10,
        0x00, 0x0A, 0x0A, 0x44, 0x50
    };
    spi_send_cmd(0xC4, init_c4, sizeof(init_c4));

    static const uint8_t init_c5[] = {
        0x18, 0x00, 0x00, 0x03, 0xFE, 0x3A, 0x4A, 0x20,
        0x30, 0x10, 0x88, 0xDE, 0x0D, 0x08, 0x0F, 0x0F,
        0x01, 0x3A, 0x4A, 0x20, 0x10, 0x10, 0x00
    };
    spi_send_cmd(0xC5, init_c5, sizeof(init_c5));

    static const uint8_t init_c6[] = {
        0x05, 0x0A, 0x05, 0x0A, 0x00, 0xE0, 0x2E, 0x0B,
        0x12, 0x22, 0x12, 0x22, 0x01, 0x03, 0x00, 0x3F,
        0x6A, 0x18, 0xC8, 0x22
    };
    spi_send_cmd(0xC6, init_c6, sizeof(init_c6));

    static const uint8_t init_c7[] = {
        0x50, 0x32, 0x28, 0x00, 0xA2, 0x80, 0x8F, 0x00,
        0x80, 0xFF, 0x07, 0x11, 0x9C, 0x67, 0xFF, 0x24,
        0x0C, 0x0D, 0x0E, 0x0F
    };
    spi_send_cmd(0xC7, init_c7, sizeof(init_c7));

    static const uint8_t init_c9[] = { 0x33, 0x44, 0x44, 0x01 };
    spi_send_cmd(0xC9, init_c9, sizeof(init_c9));

    static const uint8_t init_cf[] = {
        0x2C, 0x1E, 0x88, 0x58, 0x13, 0x18, 0x56, 0x18,
        0x1E, 0x68, 0x88, 0x00, 0x65, 0x09, 0x22, 0xC4,
        0x0C, 0x77, 0x22, 0x44, 0xAA, 0x55, 0x08, 0x08,
        0x12, 0xA0, 0x08
    };
    spi_send_cmd(0xCF, init_cf, sizeof(init_cf));

    static const uint8_t init_d5[] = {
        0x40, 0x8E, 0x8D, 0x01, 0x35, 0x04, 0x92, 0x74,
        0x04, 0x92, 0x74, 0x04, 0x08, 0x6A, 0x04, 0x46,
        0x03, 0x03, 0x03, 0x03, 0x82, 0x01, 0x03, 0x00,
        0xE0, 0x51, 0xA1, 0x00, 0x00, 0x00
    };
    spi_send_cmd(0xD5, init_d5, sizeof(init_d5));

    static const uint8_t init_d6[] = {
        0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE,
        0x93, 0x00, 0x01, 0x83, 0x07, 0x07, 0x00, 0x07,
        0x07, 0x00, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
        0x00, 0x84, 0x00, 0x20, 0x01, 0x00
    };
    spi_send_cmd(0xD6, init_d6, sizeof(init_d6));

    static const uint8_t init_d7[] = {
        0x03, 0x01, 0x0B, 0x09, 0x0F, 0x0D, 0x1E, 0x1F,
        0x18, 0x1D, 0x1F, 0x19, 0x40, 0x8E, 0x04, 0x00,
        0x20, 0xA0, 0x1F
    };
    spi_send_cmd(0xD7, init_d7, sizeof(init_d7));

    static const uint8_t init_d8[] = {
        0x02, 0x00, 0x0A, 0x08, 0x0E, 0x0C, 0x1E, 0x1F,
        0x18, 0x1D, 0x1F, 0x19
    };
    spi_send_cmd(0xD8, init_d8, sizeof(init_d8));

    static const uint8_t init_d9[] = {
        0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F,
        0x1F, 0x1F, 0x1F, 0x1F
    };
    spi_send_cmd(0xD9, init_d9, sizeof(init_d9));

    static const uint8_t init_dd[] = {
        0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F,
        0x1F, 0x1F, 0x1F, 0x1F
    };
    spi_send_cmd(0xDD, init_dd, sizeof(init_dd));

    static const uint8_t init_df[] = {
        0x44, 0x73, 0x4B, 0x69, 0x00, 0x0A, 0x02, 0x90
    };
    spi_send_cmd(0xDF, init_df, sizeof(init_df));

    static const uint8_t init_e0[] = {
        0x3B, 0x28, 0x10, 0x16, 0x0C, 0x06, 0x11, 0x28,
        0x5C, 0x21, 0x0D, 0x35, 0x13, 0x2C, 0x33, 0x28, 0x0D
    };
    spi_send_cmd(0xE0, init_e0, sizeof(init_e0));

    static const uint8_t init_e1[] = {
        0x37, 0x28, 0x10, 0x16, 0x0B, 0x06, 0x11, 0x28,
        0x5C, 0x21, 0x0D, 0x35, 0x14, 0x2C, 0x33, 0x28, 0x0F
    };
    spi_send_cmd(0xE1, init_e1, sizeof(init_e1));

    static const uint8_t init_e2[] = {
        0x3B, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x35,
        0x44, 0x32, 0x0C, 0x14, 0x14, 0x36, 0x3A, 0x2F, 0x0D
    };
    spi_send_cmd(0xE2, init_e2, sizeof(init_e2));

    static const uint8_t init_e3[] = {
        0x37, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x35,
        0x44, 0x32, 0x0C, 0x14, 0x14, 0x36, 0x32, 0x2F, 0x0F
    };
    spi_send_cmd(0xE3, init_e3, sizeof(init_e3));

    static const uint8_t init_e4[] = {
        0x3B, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x39,
        0x44, 0x2E, 0x0C, 0x14, 0x14, 0x36, 0x3A, 0x2F, 0x0D
    };
    spi_send_cmd(0xE4, init_e4, sizeof(init_e4));

    static const uint8_t init_e5[] = {
        0x37, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x39,
        0x44, 0x2E, 0x0C, 0x14, 0x14, 0x36, 0x3A, 0x2F, 0x0F
    };
    spi_send_cmd(0xE5, init_e5, sizeof(init_e5));

    static const uint8_t init_a4_1[] = {
        0x85, 0x85, 0x95, 0x82, 0xAF, 0xAA, 0xAA, 0x80,
        0x10, 0x30, 0x40, 0x40, 0x20, 0xFF, 0x60, 0x30
    };
    spi_send_cmd(0xA4, init_a4_1, sizeof(init_a4_1));

    static const uint8_t init_a4_2[] = { 0x85, 0x85, 0x95, 0x85 };
    spi_send_cmd(0xA4, init_a4_2, sizeof(init_a4_2));

    static const uint8_t init_bb_lock[] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    spi_send_cmd(0xBB, init_bb_lock, sizeof(init_bb_lock));

    /* MADCTL = 0x00 (RGB, no mirror/swap). MV bit is unreliable on
     * this panel so rotation is done in software. */
    static const uint8_t madctl[] = { 0x00 };
    spi_send_cmd(0x36, madctl, sizeof(madctl));

    /* TE on (V-blank only). */
    static const uint8_t te[] = { 0x00 };
    spi_send_cmd(0x35, te, sizeof(te));

    /* Normal Display Mode On. */
    spi_send_cmd(0x13, NULL, 0);

    /* Redundant SLPOUT (no-op on the already-awake panel), then
     * DISPON. */
    spi_send_cmd(0x11, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    spi_send_cmd(0x29, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(100));

    /* Prime the panel write pointer with a single dummy memory-write
     * (4 zero bytes). Matches Arduino_GFX's init terminator. */
    static const uint8_t init_2c[] = { 0x00, 0x00, 0x00, 0x00 };
    spi_send_cmd(0x2C, init_2c, sizeof(init_2c));
}

/* ---------------- Address window ---------------- */

/* CASET (column address set). RASET (0x2B) is intentionally NOT
 * sent: the AXS15231B silently ignores it over QSPI and tracking
 * it ourselves corrupts the implicit row pointer used by RAMWR /
 * RAMWRC. */
static void set_caset_full(int x0, int x1)
{
    uint8_t caset[] = {
        (uint8_t)((x0 >> 8) & 0xFF), (uint8_t)(x0 & 0xFF),
        (uint8_t)((x1 >> 8) & 0xFF), (uint8_t)(x1 & 0xFF)
    };
    spi_send_cmd(0x2A, caset, sizeof(caset));
}

/* ---------------- Backlight ---------------- */

static void backlight_init(void)
{
    if (s_bl_pin < 0) return;

    /* Pre-configure the BL pin as a plain GPIO output and drive it
     * to its "on" level BEFORE LEDC takes over. On some Waveshare
     * 3.49 revisions the on-board BL boost circuit latches into a
     * permanently-off state if it sees an indeterminate level
     * during the brief power-on / LEDC handoff window. */
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

    /* Start at 100 % brightness so the panel is lit the moment the
     * first frame reaches the framebuffer. */
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

/* ---------------- Flush ---------------- */

static void flush_bbox(int x0, int y0, int x1, int y1)
{
    if (!s_spi || !s_fb || !s_row_buf) return;
    if (x0 > x1 || y0 > y1) return;

    /* Acquire the bus for the whole flush so the spi_device driver
     * does not interleave any other transaction between our
     * pixel-stream chunks. CASET is sent CS-pulsed; the pixel
     * write that follows is a single CS-low burst whose first
     * chunk carries the QSPI Memory-Write preamble and subsequent
     * chunks send raw RGB565 bytes. */
    ESP_ERROR_CHECK(spi_device_acquire_bus(s_spi, portMAX_DELAY));

    if (s_swap_xy) {
        /* 90 deg CW software rotation. RASET is ignored on this
         * panel over QSPI, so partial-rect flushes are unreliable
         * in rotation mode -- always stream the full panel. The
         * dirty bbox is used only as an "anything to flush?" gate
         * by the caller in display.c. */
        (void)x0; (void)y0; (void)x1; (void)y1;

        const int pw = s_h;   /* panel native width  (e.g. 172) */
        const int ph = s_w;   /* panel native height (e.g. 640) */

        /* CASET only -- panel column range 0 .. pw-1. */
        set_caset_full(0, pw - 1);

        cs_low();
        for (int prow = 0; prow < ph; ++prow) {
            /* Logical (lx, ly) -> panel (px, py):
             *   px = (s_h - 1) - ly
             *   py = lx
             * For panel row `prow` (i.e. py == prow), the logical
             * column is fixed (lx == prow) and the logical row
             * walks from s_h-1 down to 0 as panel col `pcol`
             * walks from 0 up to s_h-1. */
            int lx = prow;
            const uint16_t *col_base = s_fb + lx;
            uint8_t *dst = s_row_buf;
            for (int pcol = 0; pcol < pw; ++pcol) {
                int ly = (s_h - 1) - pcol;
                uint16_t px = col_base[(size_t)ly * s_w];
                /* RGB565 little-endian in framebuffer -> big-endian
                 * on the wire (the AXS15231B expects MSB first). */
                *dst++ = (uint8_t)(px >> 8);
                *dst++ = (uint8_t)(px & 0xFF);
            }
            if (prow == 0) {
                spi_send_pixels_first(AXS_QSPI_MEMWR_RAMWR,
                                      s_row_buf, (size_t)pw * 2);
            } else {
                spi_send_pixels_cont(s_row_buf, (size_t)pw * 2);
            }
        }
        cs_high();
    } else {
        /* Direct (no-rotation) path. Kept for any future natively-
         * landscape AXS panel. The Waveshare 3.49 uses the rotation
         * path above. */
        int w = x1 - x0 + 1;
        int h = y1 - y0 + 1;
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 >= s_w) { x1 = s_w - 1; w = x1 - x0 + 1; }
        if (y1 >= s_h) { y1 = s_h - 1; h = y1 - y0 + 1; }
        if (w <= 0 || h <= 0) {
            spi_device_release_bus(s_spi);
            return;
        }
        set_caset_full(x0, x1);

        cs_low();
        for (int row = 0; row < h; ++row) {
            const uint16_t *src = s_fb + (size_t)(y0 + row) * s_w + x0;
            uint8_t *dst = s_row_buf;
            for (int col = 0; col < w; ++col) {
                uint16_t px = src[col];
                *dst++ = (uint8_t)(px >> 8);
                *dst++ = (uint8_t)(px & 0xFF);
            }
            if (row == 0) {
                spi_send_pixels_first(AXS_QSPI_MEMWR_RAMWR,
                                      s_row_buf, (size_t)w * 2);
            } else {
                spi_send_pixels_cont(s_row_buf, (size_t)w * 2);
            }
        }
        cs_high();
    }

    spi_device_release_bus(s_spi);
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
    s_cs_pin       = b->display.cs;
    s_rst_pin      = b->display.rst;

    /* 1. Reset GPIO -- configure and pre-drive HIGH BEFORE the SPI
     * bus comes up. Without this, on cold boot the GPIO output
     * register defaults to 0, so RST would be held LOW for the
     * entire SPI-bus / heap setup that follows, holding the panel
     * in reset across the moment its VCC stabilises. Warm reset
     * masks the bug because the GPIO output register retains its
     * last-written HIGH value from the previous hw_reset(). */
    if (s_rst_pin >= 0) {
        gpio_config_t g = {
            .intr_type    = GPIO_INTR_DISABLE,
            .mode         = GPIO_MODE_OUTPUT,
            .pin_bit_mask = (1ULL << s_rst_pin),
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .pull_up_en   = GPIO_PULLUP_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&g));
        gpio_set_level((gpio_num_t)s_rst_pin, 1);
    }

    /* 2. TE pin (optional, currently read-only -- we do not wait
     * on it from the flush path but the panel-side TE-enable
     * vendor reg is set in axs15231b_init_sequence(), so configure
     * the MCU side as a plain input so it is not driven by some
     * other peripheral). */
    if (b->display.te >= 0) {
        gpio_config_t g = {
            .intr_type    = GPIO_INTR_DISABLE,
            .mode         = GPIO_MODE_INPUT,
            .pin_bit_mask = (1ULL << b->display.te),
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .pull_up_en   = GPIO_PULLUP_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&g));
    }

    /* 3. CS pin: drive it ourselves rather than letting the SPI
     * peripheral toggle it per-transaction. The AXS15231B's QSPI
     * memory-write protocol expects the cmd/addr preamble to be
     * sent once at the start of a write burst and then raw pixel
     * chunks streamed without CS pulsing in between -- otherwise
     * the column/page pointer is reset and only the first chunk
     * lands at the CASET/RAMWR origin. spics_io_num is set to -1
     * below so the SPI driver leaves the pin alone; we drive it
     * from cs_low() / cs_high(). */
    if (s_cs_pin >= 0) {
        gpio_config_t g = {
            .intr_type    = GPIO_INTR_DISABLE,
            .mode         = GPIO_MODE_OUTPUT,
            .pin_bit_mask = (1ULL << s_cs_pin),
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .pull_up_en   = GPIO_PULLUP_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&g));
        gpio_set_level((gpio_num_t)s_cs_pin, 1);
    }

    /* 4. QSPI bus: 4 data lines (D0..D3) + SCK. max_transfer_sz is
     * sized to one panel row's worth of bytes (whichever axis is
     * longer -- in rotation mode the panel is addressed in native
     * portrait, so a row carries s_h pixels rather than s_w). */
    int max_row_pixels = (s_w > s_h) ? s_w : s_h;
    spi_bus_config_t bus_cfg = {
        .data0_io_num    = b->display.d0,
        .data1_io_num    = b->display.d1,
        .data2_io_num    = b->display.d2,
        .data3_io_num    = b->display.d3,
        .sclk_io_num     = b->display.sck,
        .max_transfer_sz = max_row_pixels * 2 + 16,
        .flags           = SPICOMMON_BUSFLAG_QUAD,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(AXS_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t dev_cfg = {
        .command_bits   = 8,
        .address_bits   = 24,
        .mode           = 0,
        .clock_speed_hz = AXS_SPI_CLOCK_HZ,
        .spics_io_num   = -1,                /* CS driven manually */
        .queue_size     = 7,
        .flags          = SPI_DEVICE_HALFDUPLEX,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(AXS_SPI_HOST, &dev_cfg, &s_spi));

    /* 5. Framebuffer (logical landscape, in PSRAM) and per-row DMA
     * scratch (must be DMA-capable INTERNAL RAM -- the SPI driver
     * cannot stream pixels directly from PSRAM at 40 MHz QSPI). */
    size_t fb_bytes = (size_t)s_w * s_h * sizeof(uint16_t);
    s_fb = heap_caps_malloc(fb_bytes, MALLOC_CAP_SPIRAM);
    s_row_buf = heap_caps_malloc((size_t)max_row_pixels * 2,
                                 MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (!s_fb || !s_row_buf) {
        ESP_LOGE(TAG, "Failed to allocate framebuffer (%u B PSRAM) or "
                      "row scratch (%u B internal DMA)",
                 (unsigned)fb_bytes, (unsigned)(max_row_pixels * 2));
        abort();
    }
    memset(s_fb, 0, fb_bytes);

    /* 6. Hardware reset + vendor init. Both phases now talk over a
     * fully-configured bus, with CS managed by us. */
    hw_reset();
    axs15231b_init_sequence();

    /* 6a. Clear the panel's on-chip GRAM BEFORE turning on the
     * backlight. axs15231b_init_sequence() ends with DISPON, so the
     * controller is now scanning out whatever random bits were in
     * GRAM at power-on. If we let backlight_init() raise the BL to
     * 100% next, the user sees a brief burst of garbage pixels
     * before the first UI flush arrives. The framebuffer was just
     * memset to 0 above, so streaming it now paints the panel
     * black. */
    flush_bbox(0, 0, s_w - 1, s_h - 1);

    /* 7. Backlight (LEDC PWM at 100 % from boot). */
    backlight_init();

    /* 8. Hand the framebuffer to the front-end. */
    display_register_backend(s_fb, s_w, s_h, flush_bbox, backlight_set);

    ESP_LOGI(TAG, "AXS15231B up: %dx%d (swap_xy=%d), fb=%u B in PSRAM",
             s_w, s_h, (int)s_swap_xy, (unsigned)fb_bytes);
}

#endif /* CONFIG_BOARD_HAS_DISPLAY_AXS15231B */
