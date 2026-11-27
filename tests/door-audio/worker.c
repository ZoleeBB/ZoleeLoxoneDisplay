/* Drive the production worker at task boundaries with fake SD and codecs.
 * The scheduled answer is cancelled before SIP opens a socket. */
#include "app_intercom_audio.h"
#include "app_loxone.h"
#include "door_ringtone.h"
#include "esp_aec.h"
#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "freertos/idf_additions.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static app_intercom_snapshot_t bell;
static jmp_buf suspended;
static void (*scheduled)(void *);
static bool muted = true, speaker_open, next_call, stop_in_loader, fail_write;
static unsigned created, deleted, closes, volume, writes, loads, cancel_after, answer_after, disconnect_after;
static unsigned answer_on_snapshot;
static uint8_t *loaded;
static const char *expected_name;

/* Catch accidental allocation of the SIP, jitter or AEC session on a bell. */
void *__wrap_calloc(size_t n, size_t size) { (void)n; (void)size; abort(); }
void __real_free(void *buffer);
void __wrap_free(void *buffer)
{
    if (buffer == loaded) loaded = NULL;
    __real_free(buffer);
}

bool app_loxone_intercom_snapshot(app_intercom_snapshot_t *out)
{
    *out = bell;
    out->ringtone_silenced = panel_door_silenced(bell.revision);
    if (bell.ringing && answer_on_snapshot && --answer_on_snapshot == 0)
        assert(app_intercom_audio_request(true, bell.revision) == ESP_OK);
    return true;
}
bool app_loxone_intercom_call_permitted(unsigned revision, bool active)
{
    return bell.config.enabled && bell.connected && bell.revision == revision && (bell.ringing || active);
}
int xTaskCreatePinnedToCoreWithCaps(void (*fn)(void *), const char *name, unsigned stack, void *arg,
                                    unsigned priority, void *out, unsigned core, unsigned caps)
{
    assert(!scheduled && !arg && !strcmp(name, "intercom_audio") && core == 0 && priority == 7);
    if (next_call) {
        assert(!speaker_open && !loaded);
        assert(stack == 16384 && (caps & MALLOC_CAP_SPIRAM));
        next_call = false;
    } else {
        assert(stack == 8192 && (caps & MALLOC_CAP_INTERNAL));
    }
    scheduled = fn;
    *(TaskHandle_t *)out = (void *)1;
    ++created;
    return pdPASS;
}
void vTaskSuspend(TaskHandle_t handle) { assert(!handle); longjmp(suspended, 1); }
void vTaskDeleteWithCaps(void *handle) { assert(handle == (void *)1); ++deleted; }
void vTaskDelay(unsigned ticks) { (void)ticks; abort(); }
int64_t esp_timer_get_time(void) { return 50000; }
uint32_t esp_random(void) { abort(); }

uint8_t *door_ringtone_load(const char *name, size_t *size, app_ringtone_load_status_t *result)
{
    assert(expected_name && !strcmp(name, expected_name) && !loaded);
    /* 320 signed samples at 16 kHz become 480 stereo frames in two codec writes. */
    uint8_t header[44] = {'R','I','F','F',0xa4,2,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,
        1,0,1,0,0x80,0x3e,0,0,0,0x7d,0,0,2,0,16,0,'d','a','t','a',0x80,2,0,0};
    *size = sizeof header + 640;
    loaded = malloc(*size);
    assert(loaded);
    memcpy(loaded, header, sizeof header);
    for (unsigned i = sizeof header; i < *size; i += 2) {
        loaded[i] = 0; loaded[i+1] = 16;
    }
    *result = APP_RINGTONE_LOAD_OK;
    ++loads;
    if (stop_in_loader) app_intercom_audio_gate_opened(bell.revision);
    return loaded;
}
esp_err_t bsp_i2c_init(void) { return ESP_OK; }
esp_err_t bsp_audio_init_voice_24k(void) { return ESP_OK; }
esp_codec_dev_handle_t bsp_audio_codec_speaker_init(void) { return (void *)1; }
int esp_codec_dev_open(esp_codec_dev_handle_t h, esp_codec_dev_sample_info_t *format)
{
    assert(h == (void *)1 && !speaker_open && format->sample_rate == 24000 && format->channel == 2);
    speaker_open = true;
    return ESP_OK;
}
int esp_codec_dev_close(esp_codec_dev_handle_t h)
{
    assert(h == (void *)1 && muted && speaker_open);
    speaker_open = false;
    ++closes;
    return ESP_OK;
}
int esp_codec_dev_set_out_mute(esp_codec_dev_handle_t h, bool mute)
{
    assert(h == (void *)1); muted = mute; return ESP_OK;
}
int esp_codec_dev_set_out_vol(esp_codec_dev_handle_t h, int v)
{
    assert(h == (void *)1 && (unsigned)v == bell.config.speaker_volume);
    volume = v; return ESP_OK;
}
int esp_codec_dev_write(esp_codec_dev_handle_t h, void *samples, int size)
{
    assert(h == (void *)1 && !muted && volume && speaker_open && loaded && size % 4 == 0);
    for (int i = 0; i < size / 2; ++i) assert(((int16_t *)samples)[i] == 4096);
    ++writes;
    if (answer_after == writes) assert(app_intercom_audio_request(true, bell.revision) == ESP_OK);
    if (cancel_after == writes) app_intercom_audio_gate_opened(bell.revision);
    if (disconnect_after == writes) bell.connected = false;
    return fail_write ? ESP_FAIL : ESP_OK;
}
size_t heap_caps_get_free_size(unsigned caps) { (void)caps; return 8192; }
size_t heap_caps_get_largest_free_block(unsigned caps) { (void)caps; return 4096; }
/* No microphone, DSP, capture or allocation during ringtone playback. */
esp_codec_dev_handle_t bsp_audio_codec_microphone_init(void) { abort(); }
int esp_codec_dev_set_in_mute(esp_codec_dev_handle_t h, bool b) { (void)h; (void)b; abort(); }
int esp_codec_dev_set_in_gain(esp_codec_dev_handle_t h, float g) { (void)h; (void)g; abort(); }
int esp_codec_dev_read(esp_codec_dev_handle_t h, void *b, int n) { (void)h; (void)b; (void)n; abort(); }
void *heap_caps_aligned_calloc(size_t a,size_t n,size_t z,unsigned c) { (void)a;(void)n;(void)z;(void)c;abort(); }
aec_handle_t *aec_create_from_config(aec_config_t *c) { (void)c; abort(); }
int aec_get_chunksize(aec_handle_t *h) { (void)h; abort(); }
void aec_destroy(aec_handle_t *h) { (void)h; abort(); }
void aec_process(aec_handle_t *h,int16_t *a,int16_t *b,int16_t *c) { (void)h;(void)a;(void)b;(void)c;abort(); }

static void begin_bell(const char *name, unsigned percent, bool repeat)
{
    assert(!loaded && !speaker_open && !scheduled && created == deleted);
    bell.ringing = false;
    app_intercom_audio_poll();
    ++bell.revision;
    bell.connected = bell.config.enabled = bell.ringing = true;
    expected_name = name;
    snprintf(bell.config.ringtone, sizeof bell.config.ringtone, "%s", name);
    bell.config.speaker_volume = percent;
    bell.config.ringtone_repeat = repeat;
    stop_in_loader = fail_write = false;
    loads = writes = closes = cancel_after = answer_after = disconnect_after = 0;
    app_intercom_audio_poll();
}
static void finish_task(void)
{
    assert(scheduled);
    void (*fn)(void *) = scheduled;
    scheduled = NULL;
    if (setjmp(suspended) == 0) fn(NULL);
    assert(!app_intercom_audio_busy() && created == deleted && !speaker_open && !loaded);
}
static void check_once(unsigned expected_writes)
{
    finish_task();
    assert(writes == expected_writes && loads == 1);
    app_intercom_audio_poll();
    assert(!scheduled);
}
int main(void)
{
    assert(app_intercom_audio_start() == ESP_OK);
    app_intercom_audio_poll();
    assert(!created && !loaded);
    begin_bell("", 65, true);
    assert(!created && !loads);
    begin_bell("silent-volume.wav", 0, true);
    assert(!created && !loads);
    begin_bell("selected-tone.wav", 41, false);
    check_once(2);
    assert(closes == 1 && volume == 41);
    begin_bell("another-tone.wav", 83, true);
    cancel_after = 5;
    check_once(5);
    assert(closes == 1 && volume == 83);
    begin_bell("disconnect.wav", 55, true);
    disconnect_after = 4;
    check_once(4);
    assert(closes == 1);
    begin_bell("during-load.wav", 55, true);
    stop_in_loader = true;
    check_once(0);
    assert(!closes);
    begin_bell("decoder-output.wav", 65, true);
    fail_write = true;
    check_once(1);
    app_intercom_audio_status_t status;
    app_intercom_audio_snapshot(&status);
    assert(closes == 1 && status.state == APP_INTERCOM_AUDIO_ERROR);

    /* A touch before the first sample, and one during playback, both hand
     * the speaker over only after the ringtone task and PCM have been reaped. */
    for (unsigned immediate = 0; immediate < 2; ++immediate) {
        begin_bell("answer.wav", 60, true);
        if (immediate) assert(app_intercom_audio_request(true, bell.revision) == ESP_OK);
        else answer_after = 1;
        finish_task();
        assert(writes == (immediate ? 0 : 1) && closes == writes && loads == writes);
        next_call = true;
        app_intercom_audio_poll();
        assert(scheduled && !next_call);
        assert(app_intercom_audio_request(false, bell.revision) == ESP_OK);
        finish_task();
        app_intercom_audio_poll();
        assert(!scheduled);
    }
    /* Interleave a real touch with the poller's second state check, just
     * before the automatic ring can claim a worker. The touch must start SIP. */
    next_call = true;
    answer_on_snapshot = 2;
    begin_bell("race-with-answer.wav", 57, true);
    assert(!next_call && !answer_on_snapshot && !loads);
    assert(app_intercom_audio_request(false, bell.revision) == ESP_OK);
    finish_task();
    app_intercom_audio_poll();
    assert(!scheduled);
    puts("Ringtone worker: selected file, mute, volume, repeat, answer, silence and release passed");
    return 0;
}
