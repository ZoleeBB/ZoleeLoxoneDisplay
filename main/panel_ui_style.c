#include "panel_ui_style.h"
#include "zld_update.h"
#include <string.h>

lv_obj_t *panel_box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color, int radius)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

lv_obj_t *panel_label(lv_obj_t *parent, const char *text, int x, int y,
                      const lv_font_t *font, uint32_t ink)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_label_set_text(obj, text);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(ink), 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

void panel_control_surface(lv_obj_t *obj, const panel_palette_t *colors)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(colors->surface), 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(colors->surface_ink), 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(colors->pressed), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(obj, lv_color_hex(colors->surface_muted), LV_STATE_DISABLED);
    lv_obj_set_style_bg_color(obj, lv_color_hex(colors->selected), LV_STATE_CHECKED);
    lv_obj_set_style_text_color(obj, lv_color_hex(colors->selected_ink), LV_STATE_CHECKED);
    lv_obj_set_style_text_color(obj, lv_color_hex(colors->selected_ink), LV_STATE_CHECKED | LV_STATE_DISABLED);
    lv_obj_set_style_border_side(obj, LV_BORDER_SIDE_LEFT, LV_STATE_CHECKED);
    lv_obj_set_style_border_width(obj, 5, LV_STATE_CHECKED);
    lv_obj_set_style_border_color(obj, lv_color_hex(colors->selected_ink), LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(obj, lv_color_hex(colors->selected_pressed), LV_STATE_CHECKED | LV_STATE_PRESSED);
}

void panel_control_slider(lv_obj_t *obj, const panel_palette_t *colors)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(colors->surface), LV_PART_MAIN);
    lv_obj_set_style_bg_color(obj, lv_color_hex(colors->selected), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(obj, lv_color_hex(colors->surface_ink), LV_PART_KNOB);
    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, lv_color_hex(colors->surface_muted), LV_PART_MAIN);
}

lv_obj_t *panel_button(lv_obj_t *parent, const char *text, int x, int y, int w, int h,
                       const panel_palette_t *colors)
{
    lv_obj_t *button = panel_box(parent, x, y, w, h, colors->surface, 14);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    panel_control_surface(button, colors);
    lv_obj_t *label = panel_label(button, text, 0, 0, &app_font_24, colors->surface_ink);
    lv_obj_remove_local_style_prop(label, LV_STYLE_TEXT_COLOR, 0);
    lv_obj_set_width(label, w - 12);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_center(label);
    return button;
}

void panel_text(lv_obj_t *label, const char *text)
{
    if (label && strcmp(lv_label_get_text(label), text)) lv_label_set_text(label, text);
}

bool panel_image(lv_obj_t *image, const char *logical)
{
    char path[256] = "S:";
    if (!zld_asset_path(logical, path + 2, sizeof(path) - 2)) return false;
    lv_image_set_src(image, path);
    return true;
}
