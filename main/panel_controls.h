#pragma once
#include "lvgl.h"
#include "panel_palette.h"
#include "cJSON.h"
#include <stdbool.h>
void panel_controls_open(lv_obj_t *parent, const cJSON *blocks, const panel_palette_t *colors,
                         unsigned panel_generation);
void panel_controls_close(void);
void panel_controls_update(void);
