#pragma once
#include "lvgl.h"
#include "panel_palette.h"
#include <stdbool.h>
void panel_lighting_open(lv_obj_t *screen, const char *id, const char *name, unsigned revision, const panel_palette_t *colors);
void panel_lighting_update(void);
void panel_lighting_close(void);
bool panel_lighting_is_open(void);
