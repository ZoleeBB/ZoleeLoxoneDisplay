#pragma once
#include "lvgl.h"
#include <stdbool.h>
void panel_energy_open(lv_obj_t *parent, unsigned config_revision);
void panel_energy_update(unsigned tab, bool night, const char *clock);
