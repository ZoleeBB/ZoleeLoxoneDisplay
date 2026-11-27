#pragma once
#include "lvgl.h"
#include "cJSON.h"
#include <stdbool.h>
void panel_controls_open(lv_obj_t *parent, const cJSON *blocks, uint32_t foreground, bool night);
void panel_controls_close(void);
void panel_controls_update(void);
