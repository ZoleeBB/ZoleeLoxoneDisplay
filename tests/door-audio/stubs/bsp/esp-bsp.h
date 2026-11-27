#pragma once
#include "esp_err.h"
#include "esp_codec_dev.h"
#define BSP_AUDIO_TDM_SLOT_COUNT 4
#define BSP_AUDIO_TDM_SLOT_MASK_FL 1
esp_err_t bsp_i2c_init(void);
esp_err_t bsp_audio_init_voice_24k(void);
esp_codec_dev_handle_t bsp_audio_codec_speaker_init(void);
esp_codec_dev_handle_t bsp_audio_codec_microphone_init(void);
