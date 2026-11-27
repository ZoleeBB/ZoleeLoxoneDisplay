#pragma once
#include "lvgl.h"
#include <stdbool.h>
void panel_summary_attach(lv_obj_t *energy);
void panel_summary_sync(unsigned tab, bool night);
bool panel_summary_visible(void);
