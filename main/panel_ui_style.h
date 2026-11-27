#pragma once
#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

LV_FONT_DECLARE(app_font_20);
LV_FONT_DECLARE(app_font_24);
LV_FONT_DECLARE(app_font_28);
LV_FONT_DECLARE(app_font_34);
LV_FONT_DECLARE(app_font_numbers_96);
LV_FONT_DECLARE(app_font_numbers_132);

lv_obj_t *panel_box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color, int radius);
lv_obj_t *panel_label(lv_obj_t *parent, const char *text, int x, int y,
                      const lv_font_t *font, uint32_t ink);
lv_obj_t *panel_button(lv_obj_t *parent, const char *text, int x, int y, int w, int h);
void panel_text(lv_obj_t *label, const char *text);
bool panel_image(lv_obj_t *image, const char *logical);
