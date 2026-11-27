/* A 4B Slave azonos elrendezése és energiaút-modellje, a fülsor alatti négyzetre vetítve. */
#include "panel_energy.h"
#include "panel_energy_summary.h"
#include "panel_ui_style.h"
#include "app_flow_model.h"
#include "zld_loxone.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    lv_obj_t *ring, *path, *dots[3], *name, *value;
    int x, y;
    float phase;
} node_t;
static const int centres[][2] = {{360,130}, {130,360}, {590,360}, {360,590}};
static const char *const names[] = {"Hálózat","Akkumulátor","Napelem","Ház"};
static node_t nodes[4];
static lv_obj_t *root, *background, *soc, *detail, *temperature, *wind, *humidity;
static lv_obj_t *weather, *wind_icon, *humidity_icon, *clock_label, *message;
static int last_night, last_weather;
static uint32_t last_tick, last_read;
static lx_values_t values;
static bool available;

static int scaled(int value)
{
    return (value * 640 + 360) / 720;
}

static void show(lv_obj_t *obj, bool visible)
{
    if (visible) lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

static void tint(lv_obj_t *obj, lv_style_prop_t prop, uint32_t color, lv_style_selector_t part)
{
    lv_style_value_t old;
    lv_color_t next = lv_color_hex(color);
    if (lv_obj_get_local_style_prop(obj, prop, &old, part) == LV_STYLE_RES_FOUND &&
        lv_color_eq(old.color, next)) return;
    lv_style_value_t value = {.color=next};
    lv_obj_set_local_style_prop(obj, prop, value, part);
}

static lv_obj_t *icon(const char *path, int x, int y, int size)
{
    lv_obj_t *obj = lv_image_create(root);
    lv_obj_set_pos(obj, scaled(x), scaled(y));
    lv_obj_set_size(obj, scaled(size), scaled(size));
    lv_image_set_inner_align(obj, LV_IMAGE_ALIGN_STRETCH);
    if (path) panel_image(obj, path);
    return obj;
}

static void text_shadow(lv_event_t *event)
{
    /* A betűket rajzoljuk újra 1 pixeles eltolással, nem a felirat dobozát.
     * A szín a tényleges szövegszínt követi, így a teljesítményértékek
     * színváltása és a nappali/éjjeli mód sem igényel második feliratot. */
    lv_obj_t *object = lv_event_get_current_target(event);
    bool opaque_background = lv_obj_get_style_bg_opa(object, LV_PART_MAIN) != LV_OPA_TRANSP;
    bool after_main = lv_event_get_code(event) == LV_EVENT_DRAW_MAIN_END;
    if (opaque_background != after_main)
        return;
    lv_draw_label_dsc_t shadow;
    lv_draw_label_dsc_init(&shadow);
    shadow.base.layer = lv_event_get_layer(event);
    lv_obj_init_draw_label_dsc(object, LV_PART_MAIN, &shadow);
    shadow.text = lv_label_get_text(object);
    shadow.flag = LV_TEXT_FLAG_EXPAND;
    lv_area_t area;
    lv_obj_get_content_coords(object, &area);
    lv_text_get_size(&shadow.text_size, shadow.text, shadow.font,
                     shadow.letter_space, shadow.line_space,
                     lv_area_get_width(&area), shadow.flag);
    lv_color_t foreground = shadow.color;
    lv_opa_t foreground_opa = shadow.opa;
    shadow.color = lv_color_hex(lv_color_luminance(shadow.color) < 130
                                  ? 0xF4FAFF : 0x001020);
    shadow.opa = (lv_opa_t)((unsigned)shadow.opa * 85 / 100);
    area.x1++;
    area.x2++;
    area.y1++;
    area.y2++;
    lv_draw_label(shadow.base.layer, &shadow, &area);
    shadow.color = foreground;
    shadow.opa = foreground_opa;
    area.y1--;
    area.y2--;
    /* Szintetikus félkövér: a betűk még egyszer, 1 képponttal jobbra, a saját
     * színükkel. Így a névnek és a teljesítményértéknek nem kell külön
     * félkövér fontot a flashbe tenni, és a szélesség sem változik. */
    if (lv_event_get_user_data(event))
        lv_draw_label(shadow.base.layer, &shadow, &area);
    if (after_main)
    {
        /* Saját hátterű diagnosztikai feliratnál a háttér után rajzolunk,
         * majd visszatesszük a fő betűréteget az árnyék elé. */
        area.x1--;
        area.x2--;
        lv_draw_label(shadow.base.layer, &shadow, &area);
    }
}

static lv_obj_t *label(const char *text, int x, int y, int width, const lv_font_t *font)
{
    lv_obj_t *obj = panel_label(root, text, scaled(x), scaled(y), font, 0x00183D);
    lv_obj_set_width(obj, scaled(width));
    lv_label_set_long_mode(obj, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_add_event_cb(obj, text_shadow, LV_EVENT_DRAW_MAIN_BEGIN, NULL);
    return obj;
}

void panel_energy_open(lv_obj_t *parent)
{
    root = panel_box(parent, 40, 0, 640, 640, 0x071D32, 0);
    background = icon(NULL, 0, 0, 720);
    for (int i = 0; i < 4; ++i) {
        node_t *n = &nodes[i];
        n->phase = 0;
        n->x = centres[i][0];
        n->y = centres[i][1];
        n->path = panel_box(root, 0, 0, 1, 1, 0, 0);
        n->ring = panel_box(root, scaled(n->x-60), scaled(n->y-60), scaled(120), scaled(120), 0, LV_RADIUS_CIRCLE);
        lv_obj_set_style_bg_opa(n->ring, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(n->ring, scaled(6), 0);
        for (int j = 0; j < 3; ++j) {
            n->dots[j] = panel_box(root, 0, 0, scaled(14), scaled(14), 0xFFFCC1, LV_RADIUS_CIRCLE);
            lv_obj_set_style_border_width(n->dots[j], 2, 0);
        }
    }
    icon("energy/flow_hub.png", 339, 339, 42);
    for (int i = 0; i < 4; i++) {
        node_t *n = &nodes[i];
        bool vertical = n->x == 360;
        int x = vertical ? n->x+72 : n->x-92;
        n->name = label(names[i], x, vertical ? n->y-37 : n->y+66, 185, &app_font_20);
        n->value = label("—", x, vertical ? n->y-7 : n->y+98, 185, &app_font_28);
        if (!vertical) {
            lv_obj_set_style_text_align(n->name, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_text_align(n->value, LV_TEXT_ALIGN_CENTER, 0);
        }
    }
    label("Zolee Energy", 26, 27, 180, &app_font_24);
    weather = icon(NULL, 206, 20, 44);
    temperature = label("", 254, 32, 90, &app_font_20);
    humidity_icon = icon("energy/weather_humidity.png", 342, 25, 34);
    humidity = label("", 380, 32, 70, &app_font_20);
    wind_icon = icon("energy/wind.png", 446, 25, 34);
    wind = label("", 484, 32, 118, &app_font_20);
    clock_label = label("", 604, 25, 112, &app_font_24);
    detail = label("", 27, 504, 205, &app_font_20);
    lv_obj_set_style_text_align(detail, LV_TEXT_ALIGN_CENTER, 0);
    soc = lv_bar_create(root);
    lv_obj_set_pos(soc, scaled(108), scaled(332));
    lv_obj_set_size(soc, 5, scaled(62));
    lv_bar_set_orientation(soc, LV_BAR_ORIENTATION_VERTICAL);
    lv_bar_set_range(soc, 0, 100);
    message = label("", 20, 668, 680, &app_font_20);
    lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, 0);
    panel_summary_attach(root);
    last_night = last_weather = -1;
    last_tick = lv_tick_get();
    last_read = last_tick - 500;
}

static void flow(node_t *n, double kw, bool inward, uint32_t tone, bool night, float dt)
{
    bool active = fabs(kw) >= .0005;
    uint32_t color = active ? tone : night ? 0x87A9D4 : 0x506C84;
    tint(n->ring, LV_STYLE_BORDER_COLOR, color, 0);
    tint(n->path, LV_STYLE_BG_COLOR, color, 0);
    tint(n->value, LV_STYLE_TEXT_COLOR, color, 0);
    bool vertical = n->x == 360;
    int end_x = vertical ? 360 : n->x + (n->x < 360 ? 60 : -60);
    int end_y = vertical ? n->y + (n->y < 360 ? 60 : -60) : 360;
    int distance = vertical ? abs(end_y - 360) : abs(end_x - 360);
    lv_obj_set_pos(n->path, scaled(vertical ? 356 : (end_x < 360 ? end_x : 360)),
                            scaled(vertical ? (end_y < 360 ? end_y : 360) : 356));
    lv_obj_set_size(n->path, scaled(vertical ? 8 : distance), scaled(vertical ? distance : 8));
    n->phase = app_flow_advance(n->phase, kw, inward, dt);
    for (int j = 0; j < 3; ++j) {
        show(n->dots[j], active);
        if (!active) continue;
        float f = fmodf(n->phase + j/3.f, 1.f);
        lv_obj_set_pos(n->dots[j], scaled((int)(end_x+(360-end_x)*f)-7),
                                   scaled((int)(end_y+(360-end_y)*f)-7));
        tint(n->dots[j], LV_STYLE_BORDER_COLOR, color, 0);
    }
}

void panel_energy_update(unsigned tab, bool night, const char *clock)
{
    uint32_t now = lv_tick_get();
    float dt = (now-last_tick)/1000.f;
    last_tick = now;
    if (panel_summary_visible()) {
        panel_summary_sync(tab,night);
        return;
    }
    if (now-last_read >= 400) {
        last_read = now;
        available = zld_loxone_energy(tab, &values);
    }
    if (last_night != (int)night) {
        last_night = night;
        panel_image(background, night ? "energy/night.png" : "energy/day.png");
        for (unsigned i = 0; i < lv_obj_get_child_count(root); i++) {
            lv_obj_t *child = lv_obj_get_child(root, i);
            if (lv_obj_check_type(child, &lv_label_class))
                tint(child, LV_STYLE_TEXT_COLOR, night ? 0xF5FAFF : 0x00183D, 0);
        }
    }
    char text[64];
    for (int i = 0; i < 4; ++i) {
        bool valid = available && values.seen[i];
        double kw = valid ? values.value[i] : 0;
        if (i == 0 && values.seen[5] && values.value[5] == 3) kw = 0;
        uint32_t green = night ? 0x58E93C : 0x087E12, orange = night ? 0xFFC44D : 0xC94800;
        flow(&nodes[i], kw, i<2 ? kw>0 : i==2, (i<2 && kw<0) || i==2 ? green : orange, night, dt);
        if (!valid) snprintf(text,sizeof(text),"—");
        else if (fabs(kw)<1) snprintf(text,sizeof(text),"%.0f W",fabs(kw)*1000);
        else snprintf(text,sizeof(text),"%.1f kW",fabs(kw));
        panel_text(nodes[i].value, text);
    }
    bool has_soc = available && values.seen[4], has_temp = available && values.seen[7];
    text[0] = 0;
    if (has_soc && has_temp) snprintf(text,sizeof(text),"%.0f%% · %.0f °C",values.value[4],values.value[7]);
    else if (has_soc) snprintf(text,sizeof(text),"%.0f%%",values.value[4]);
    else if (has_temp) snprintf(text,sizeof(text),"%.0f °C",values.value[7]);
    panel_text(detail, text);
    show(soc, has_soc);
    if (has_soc) {
        int pct = (int)lround(fmax(0,fmin(100,values.value[4])));
        if (lv_bar_get_value(soc)!=pct) lv_bar_set_value(soc,pct,LV_ANIM_OFF);
        tint(soc, LV_STYLE_BG_COLOR, pct < 20 ? 0xEF443D : pct < 40 ? 0xFFAF32 : 0x5BF237, LV_PART_INDICATOR);
    }
    const int field[] = {8,9,18};
    lv_obj_t *labels[] = {temperature,wind,humidity};
    const char *formats[] = {"%.1f °C","%.0f km/h","%.0f %%"};
    for (int i=0; i<3; ++i) {
        bool valid = available && values.seen[field[i]];
        show(labels[i], valid);
        if (valid) {
            snprintf(text, sizeof(text), formats[i], values.value[field[i]]);
            panel_text(labels[i], text);
        }
    }
    show(wind_icon, available && values.seen[9]);
    show(humidity_icon, available && values.seen[18]);
    show(weather, available && values.seen[11]);
    int icon_id = night ? 0 : 1;
    if (values.seen[11] && values.value[11]) icon_id = values.seen[8] && values.value[8] < -4 ? 2 : 3;
    else if (!night && values.seen[10] && values.value[10]) icon_id = 4;
    else if (!night && values.seen[2] && values.value[2]>1) icon_id = 5;
    static const char *const icons[]={"energy/weather_moon.png","energy/weather_cloud.png","energy/weather_snow.png",
                                      "energy/weather_rain.png","energy/weather_sun.png","energy/weather_partly.png"};
    if (icon_id != last_weather) { panel_image(weather, icons[icon_id]); last_weather = icon_id; }
    panel_text(clock_label, clock);
    panel_text(message, available ? "" : "Miniserver-adatokra vár…");
}
