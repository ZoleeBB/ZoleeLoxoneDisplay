#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

esp_err_t app_intercom_start(void);
/* Az első MJPEG-adatfolyamkor foglalja a JPEG-dekóder DMA-erőforrásait. */
esp_err_t app_intercom_prepare_video(void);
/* A médiafeladat csengetéskor, LVGL-lock nélkül indítja a rövid életű videófeladatot. */
void app_intercom_poll(void);
/* Csak a teljes médiafelszabadítás után tölthető vissza a háttér. */
bool app_intercom_idle(void);
/* A lezárás diagnosztikája megkülönbözteti a videó- és hangfeladatot. */
bool app_intercom_video_busy(void);
/* Már dekódolt RGB565 kép másolása a legfrissebbképes postaládába, LVGL-lock
 * nélkül. A UI saját lock alatt cseréli a képpuffereket. A revision kizárja
 * a régi beállításhoz tartozó későn beérkező képet. */
bool app_intercom_ui_frame(const uint8_t *pixels, unsigned width, unsigned height,
                           unsigned stride, unsigned revision);
void app_intercom_ui_status(const char *message, unsigned revision);
