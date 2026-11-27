#pragma once
#include "lvgl.h"
#include <stdbool.h>
void panel_lighting_open(lv_obj_t *screen, const char *id, const char *name, unsigned revision, bool night);
void panel_lighting_update(void);
void panel_lighting_close(void);
bool panel_lighting_is_open(void);
