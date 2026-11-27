#pragma once

#include "lvgl.h"

/* Returns the divider so its color can follow the clock while the panel is locked. */
lv_obj_t *panel_touch_open(lv_obj_t *layer, unsigned generation, unsigned tap_window_ms, void (*unlock)(void));
void panel_touch_close(void);
void panel_touch_tick(void);
