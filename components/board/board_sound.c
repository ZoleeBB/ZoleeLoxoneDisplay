#include "board_sound.h"
#include "board_click.h"
#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <string.h>

#define CLICK_VOLUME 54
#define CLICK_BLOCK 160

static SemaphoreHandle_t lock;
static StaticSemaphore_t lock_memory;
static TaskHandle_t worker;
static board_click_mixer_t mixer;
static esp_codec_dev_handle_t speaker;
static bool prepared, opened, media, mute_media;
static esp_err_t i2s_result;

/* Call with lock held. The BSP shares TX/RX clocks and rejects
 * a conflicting mode. The pinned BSP rolls back failed allocations, so a later
 * interaction may retry. A touch never opens the microphone or allocates AEC. */
static esp_err_t prepare(void)
{
    if (!prepared) {
        esp_err_t rc = bsp_i2c_init();
        if (rc != ESP_OK) return rc;
        i2s_result = bsp_audio_init_voice_24k();
        prepared = i2s_result == ESP_OK || i2s_result == ESP_ERR_INVALID_STATE;
    }
    if (i2s_result != ESP_OK) return i2s_result;
    if (!speaker) speaker = bsp_audio_codec_speaker_init();
    return speaker ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t board_sound_prepare(void)
{
    if (!lock) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(lock, portMAX_DELAY);
    esp_err_t err = prepare();
    xSemaphoreGive(lock);
    return err;
}

static void close_output(void)
{
    if (opened) {
        esp_codec_dev_set_out_mute(speaker, true);
        esp_codec_dev_close(speaker);
        opened = false;
    }
}

static esp_err_t open_output(unsigned volume)
{
    esp_err_t err = prepare();
    if (err != ESP_OK) return err;
    if (!opened) {
        esp_codec_dev_sample_info_t fmt = {.sample_rate=BOARD_AUDIO_RATE, .channel=2, .bits_per_sample=16};
        opened = true; /* unwind even a partially completed codec open */
        err = esp_codec_dev_open(speaker, &fmt);
    }
    if (err == ESP_OK) err = esp_codec_dev_set_out_vol(speaker, volume);
    if (err == ESP_OK) err = esp_codec_dev_set_out_mute(speaker, volume == 0);
    if (err != ESP_OK) close_output();
    return err;
}

esp_err_t board_sound_media_open(unsigned volume)
{
    if (!lock) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(lock, portMAX_DELAY);
    /* A muted intercom still allows the independent UI feedback. Drop the
     * incoming PCM below, and run the DAC at the quiet touch-only volume. */
    esp_err_t err = media ? ESP_ERR_INVALID_STATE : open_output(volume ? volume : CLICK_VOLUME);
    if (err == ESP_OK) {
        media = true;
        mute_media = volume == 0;
    }
    xSemaphoreGive(lock);
    return err;
}

esp_err_t board_sound_media_write(int16_t *stereo, size_t frames)
{
    /* The single media task opens, writes and closes this stream in order.
     * While media is set, the click worker never touches the mixer or codec. */
    if (mute_media) memset(stereo, 0, frames * 2 * sizeof(*stereo));
    board_click_mix(&mixer, stereo, frames);
    return esp_codec_dev_write(speaker, stereo, frames * 2 * sizeof(*stereo));
}

void board_sound_media_close(void)
{
    if (!lock) return;
    xSemaphoreTake(lock, portMAX_DELAY);
    close_output();
    media = false;
    if (board_click_pending(&mixer) && worker) xTaskNotifyGive(worker);
    xSemaphoreGive(lock);
}

static void play_clicks(void *unused)
{
    (void)unused;
    int16_t pcm[CLICK_BLOCK*2];
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        xSemaphoreTake(lock, portMAX_DELAY);
        if (!media && board_click_pending(&mixer)) {
            bool cold = !opened;
            esp_err_t err = cold ? open_output(CLICK_VOLUME) : ESP_OK;
            /* The BSP's three 160-frame descriptors form a 20 ms DMA ring. On
             * a cold open, give the DAC/PA silence to settle before the impulse.
             * A final silent descriptor commits the tail; TX auto-clears on underrun. */
            memset(pcm, 0, sizeof(pcm));
            for (unsigned i=0; cold && err==ESP_OK && i<3; ++i)
                err=esp_codec_dev_write(speaker,pcm,sizeof(pcm));
            while (err==ESP_OK && board_click_pending(&mixer)) {
                memset(pcm, 0, sizeof(pcm));
                board_click_mix(&mixer, pcm, CLICK_BLOCK);
                err=esp_codec_dev_write(speaker,pcm,sizeof(pcm));
            }
            if (err==ESP_OK) {
                memset(pcm,0,sizeof(pcm));
                err=esp_codec_dev_write(speaker,pcm,sizeof(pcm));
            }
            if (err!=ESP_OK) {
                close_output();
                mixer.remaining=0;
                atomic_store(&mixer.pending,0);
            }
        }
        xSemaphoreGive(lock);
    }
}

esp_err_t board_sound_start(void)
{
    lock = xSemaphoreCreateMutexStatic(&lock_memory);
    if (!lock) return ESP_ERR_NO_MEM;
    /* Worker stack in PSRAM; retain only the speaker for low-latency idle taps. */
    return xTaskCreatePinnedToCoreWithCaps(play_clicks, "touch_sound", 4096, NULL, 4,
               &worker, tskNO_AFFINITY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) == pdPASS
           ? ESP_OK : ESP_ERR_NO_MEM;
}

void board_sound_touch(void)
{
    if (worker) {
        atomic_fetch_add(&mixer.pending, 1);
        xTaskNotifyGive(worker);
    }
}
