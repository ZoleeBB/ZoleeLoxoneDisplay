#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define BOARD_AUDIO_RATE 24000
/* Called once before LVGL begins dispatching touches. */
esp_err_t board_sound_start(void);
void board_sound_touch(void);

/* The intercom owns the stream between open and close. Touches mix into
 * its frames at the current volume, without a second writer or codec reset. */
esp_err_t board_sound_prepare(void);
esp_err_t board_sound_media_open(unsigned volume);
esp_err_t board_sound_media_write(int16_t *stereo, size_t frames);
void board_sound_media_close(void);
