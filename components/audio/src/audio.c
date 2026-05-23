/*
 * I2S WAV player.
 *
 * Uses the IDF v5 I2S Standard mode driver (the legacy
 * driver/i2s.h API was removed in v5.0). One TX channel is
 * opened on the port / pins from board_get()->i2s; a worker
 * task decodes the RIFF header and streams the PCM payload to
 * the channel in ~4 KB chunks.
 *
 * The "preempting mixer" semantics required by the narrator
 * are implemented by a single playback slot guarded by a
 * mutex: a new audio_play_wav() call atomically swaps the
 * active clip pointer and signals the worker via a binary
 * semaphore. The worker then aborts the previous clip mid-
 * frame and starts the new one from sample 0.
 */

#include "sdkconfig.h"

#if CONFIG_BOARD_HAS_PSRAM && CONFIG_BOARD_HAS_SPEAKER

#include "audio.h"

#include <string.h>
#include <stdlib.h>

#include <esp_check.h>
#include <esp_log.h>
#include <driver/i2s_std.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#include "board.h"

static const char *TAG = "audio";

#define DEFAULT_SAMPLE_RATE 22050

static i2s_chan_handle_t s_tx;
static SemaphoreHandle_t s_mutex;
static SemaphoreHandle_t s_kick;
static volatile bool      s_playing;
static int                s_volume_pct = CONFIG_AUDIO_VOLUME;

/* Active clip slot. The worker takes a local snapshot of this
 * under the mutex on every wake-up so a midway preemption only
 * has to abort the inner write loop. */
typedef struct {
    const uint8_t *data;
    size_t         len;
    uint32_t       gen;     /* increments on every play -- worker
                             * compares to detect preemption */
} clip_t;

static clip_t s_active;

/* ----- RIFF WAVE header parsing ----- */

typedef struct {
    uint16_t format;
    uint16_t channels;
    uint32_t sample_rate;
    uint16_t bits;
    const uint8_t *pcm;
    size_t   pcm_len;
} wav_info_t;

static bool parse_wav(const uint8_t *d, size_t n, wav_info_t *info)
{
    if (n < 44) return false;
    if (memcmp(d, "RIFF", 4) != 0 || memcmp(d + 8, "WAVE", 4) != 0) {
        return false;
    }
    size_t i = 12;
    bool got_fmt = false;
    while (i + 8 <= n) {
        const char *id = (const char *)(d + i);
        uint32_t sz = (uint32_t)d[i + 4]
                    | ((uint32_t)d[i + 5] << 8)
                    | ((uint32_t)d[i + 6] << 16)
                    | ((uint32_t)d[i + 7] << 24);
        i += 8;
        if (i + sz > n) return false;
        if (memcmp(id, "fmt ", 4) == 0 && sz >= 16) {
            info->format     = d[i] | (d[i + 1] << 8);
            info->channels   = d[i + 2] | (d[i + 3] << 8);
            info->sample_rate = (uint32_t)d[i + 4]
                              | ((uint32_t)d[i + 5] << 8)
                              | ((uint32_t)d[i + 6] << 16)
                              | ((uint32_t)d[i + 7] << 24);
            info->bits = d[i + 14] | (d[i + 15] << 8);
            got_fmt = true;
        } else if (memcmp(id, "data", 4) == 0 && got_fmt) {
            info->pcm = d + i;
            info->pcm_len = sz;
            return info->format == 1 /* PCM */ && info->bits == 16;
        }
        i += sz;
    }
    return false;
}

static void apply_volume(int16_t *samples, size_t n_samples)
{
    if (s_volume_pct >= 100) return;
    int32_t gain = (s_volume_pct * 256) / 100;
    for (size_t i = 0; i < n_samples; ++i) {
        samples[i] = (int16_t)((samples[i] * gain) >> 8);
    }
}

static esp_err_t reconfigure_clock(uint32_t sample_rate)
{
    i2s_std_clk_config_t clk = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate);
    return i2s_channel_reconfig_std_clock(s_tx, &clk);
}

static void worker(void *arg)
{
    (void)arg;
    /* 4 KB DMA-friendly chunk in internal RAM. */
    int16_t *buf = malloc(4096);
    if (!buf) {
        ESP_LOGE(TAG, "OOM allocating play buffer");
        vTaskDelete(NULL);
        return;
    }

    while (1) {
        xSemaphoreTake(s_kick, portMAX_DELAY);

        clip_t snap;
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        snap = s_active;
        xSemaphoreGive(s_mutex);

        if (!snap.data) {
            s_playing = false;
            continue;
        }

        wav_info_t w = { 0 };
        if (!parse_wav(snap.data, snap.len, &w)) {
            ESP_LOGW(TAG, "WAV header not understood (%u bytes)",
                     (unsigned)snap.len);
            s_playing = false;
            continue;
        }
        reconfigure_clock(w.sample_rate);

        s_playing = true;

        size_t offset = 0;
        while (offset < w.pcm_len) {
            /* Check for preemption: a newer generation means the
             * caller has handed us a new clip. */
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            bool preempted = (s_active.gen != snap.gen);
            xSemaphoreGive(s_mutex);
            if (preempted) break;

            size_t chunk = w.pcm_len - offset;
            if (chunk > 4096) chunk = 4096;
            memcpy(buf, w.pcm + offset, chunk);
            apply_volume(buf, chunk / 2);

            size_t written = 0;
            i2s_channel_write(s_tx, buf, chunk, &written,
                              pdMS_TO_TICKS(100));
            offset += written ? written : chunk;
        }
        s_playing = false;
    }
}

int audio_init(void)
{
    if (s_tx) return 0;

    const board_t *b = board_get();
    if (b->i2s.bclk < 0) {
        ESP_LOGW(TAG, "No I2S pins configured");
        return -1;
    }

    s_mutex = xSemaphoreCreateMutex();
    s_kick  = xSemaphoreCreateBinary();
    if (!s_mutex || !s_kick) return -1;

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(
        (i2s_port_t)b->i2s.port, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num  = 6;
    chan_cfg.dma_frame_num = 240;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &s_tx, NULL));

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(DEFAULT_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                        I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = (b->i2s.mclk < 0) ? I2S_GPIO_UNUSED : b->i2s.mclk,
            .bclk = b->i2s.bclk,
            .ws   = b->i2s.lrck,
            .dout = b->i2s.dout,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = { 0 },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_tx, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(s_tx));

    xTaskCreatePinnedToCore(worker, "audio", 4096, NULL, 6, NULL, 1);

    ESP_LOGI(TAG, "I2S up on port %d (MCLK=%d BCLK=%d LRCK=%d DOUT=%d)",
             b->i2s.port, b->i2s.mclk, b->i2s.bclk, b->i2s.lrck, b->i2s.dout);
    return 0;
}

int audio_play_wav(const void *data, size_t len)
{
    if (!s_tx) return -1;
    if (!data || len == 0) return -1;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_active.data = data;
    s_active.len  = len;
    s_active.gen++;
    xSemaphoreGive(s_mutex);
    xSemaphoreGive(s_kick);
    return 0;
}

void audio_stop(void)
{
    if (!s_tx) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_active.data = NULL;
    s_active.len  = 0;
    s_active.gen++;
    xSemaphoreGive(s_mutex);
}

void audio_set_volume(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    s_volume_pct = percent;
}

bool audio_is_playing(void) { return s_playing; }

#endif /* PSRAM && SPEAKER */
