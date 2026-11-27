#pragma once
#include <stdbool.h>
#include "lvgl.h"

/* A médiafeladatokat az alkalmazás, a widgeteket az LVGL-feladat kezeli. */
void panel_intercom_media_tick(bool configured);
bool panel_intercom_active(void);
void panel_intercom_ui_tick(lv_obj_t *screen);
