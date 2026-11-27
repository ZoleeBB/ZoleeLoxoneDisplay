#pragma once

#include "lvgl.h"

void panel_touch_open(lv_obj_t *layer, unsigned generation, unsigned tap_window_ms, void (*unlock)(void));
void panel_touch_close(void);
void panel_touch_tick(void);
