#include "panel_ui.h"
#include "app_loxone_wordmark.h"
#include "fixture.h"
#include "lvgl.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SIDE 720
static uint32_t framebuffer[SIDE * SIDE], draw_buffer[SIDE * 64];
static lv_indev_data_t pointer;
static const char *output;

static void draw(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    size_t width = area->x2 - area->x1 + 1;
    for (int y = area->y1; y <= area->y2; y++) {
        memcpy(framebuffer + y * SIDE + area->x1, pixels, sizeof(uint32_t) * width);
        pixels += width * sizeof(uint32_t);
    }
    lv_display_flush_ready(display);
}

static void read_pointer(lv_indev_t *dev, lv_indev_data_t *out)
{
    (void)dev;
    out->point = pointer.point;
    out->state = pointer.state;
}

static void step(unsigned ms)
{
    for (unsigned t = 0; t < ms; t += 10) {
        lv_tick_inc(10);
        lv_timer_handler();
    }
}

static void tap(int x, int y)
{
    pointer.point = (lv_point_t){x, y};
    pointer.state = LV_INDEV_STATE_PRESSED;
    step(75);
    pointer.state = LV_INDEV_STATE_RELEASED;
    step(350);
}

static bool has_label(lv_obj_t *parent, const char *text)
{
    if (lv_obj_has_flag(parent, LV_OBJ_FLAG_HIDDEN))
        return false;
    if (lv_obj_check_type(parent, &lv_label_class) && !strcmp(lv_label_get_text(parent), text))
        return true;
    for (unsigned i = 0; i < lv_obj_get_child_count(parent); i++) {
        if (has_label(lv_obj_get_child(parent, i), text))
            return true;
    }
    return false;
}

static void assert_standby(const char *time_text, bool night)
{
    lv_obj_t *layer = lv_obj_get_child(lv_screen_active(), -1);
    lv_obj_t *footer = lv_obj_get_child(layer, -1);
    assert(fixture_page == -1 && fixture_brightness == 10);
    assert(has_label(layer, time_text));
    assert(has_label(footer, "Zolee") && has_label(footer, "Display"));
    assert(lv_obj_get_child_count(footer) == 3);
    lv_obj_t *mark = lv_obj_get_child(footer, 1);
    assert(lv_obj_check_type(mark, &lv_image_class) && lv_image_get_src(mark) == &loxone_wordmark);
    lv_area_t area;
    lv_obj_get_coords(footer, &area);
    assert(abs(area.x1 + area.x2 + 1 - SIDE) <= 2 && area.y1 >= 620 && area.y2 < SIDE - 10);

    uint32_t expected = night ? 0xA04E4E : 0x8B8C8E;
    unsigned solid_clock = 0;
    for (int y = 260; y < 460; y++) {
        for (int x = 0; x < SIDE; x++) {
            if ((framebuffer[y * SIDE + x] & 0xFFFFFF) == expected) solid_clock++;
        }
    }
    /* Az ismert 07:13 felirat vékony termosztátfonttal nem éri el ezt a területet. */
    assert(solid_clock > (!strcmp(time_text, "07:13") ? 8500u : 1000u));
    /* Csak az órát színezzük. Az ikonsor a fekete háttéren közel szürke. */
    for (int i = 0; i < 80 * SIDE; ++i) {
        int red = (framebuffer[i] >> 16) & 255;
        int blue = framebuffer[i] & 255;
        assert(abs(red - blue) < 5);
    }

    /* A valódi PNG az átfestés után a két felirattal azonos tintaárnyalatot ad. */
    lv_color_t ink = lv_obj_get_style_text_color(lv_obj_get_child(footer, 0), 0);
    assert(lv_color_eq(ink, lv_obj_get_style_text_color(lv_obj_get_child(footer, 2), 0)));
    uint32_t color = lv_color_to_u32(ink) & 0xffffff;
    assert(color == 0x8B8C8E);
    lv_obj_get_coords(mark, &area);
    unsigned solid = 0;
    for (int y = area.y1; y <= area.y2; y++)
        for (int x = area.x1; x <= area.x2; x++)
            if ((framebuffer[y * SIDE + x] & 0xffffff) == color) solid++;
    assert(solid > 150);
}

static void snapshot(const char *name)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.ppm", output, name);
    FILE *f = fopen(path, "wb");
    assert(f);
    fprintf(f, "P6\n%d %d\n255\n", SIDE, SIDE);
    for (size_t i = 0; i < SIDE * SIDE; i++) {
        uint32_t p = framebuffer[i];
        uint8_t rgb[3] = {(uint8_t)(p >> 16), (uint8_t)(p >> 8), (uint8_t)p};
        assert(fwrite(rgb, 1, 3, f) == 3);
    }
    assert(fclose(f) == 0);
}

static void test_night_clock(void)
{
    const time_t day = fixture_time;
    unsigned commands = fixture_calls;
    fixture_time = day + 15 * 3600;
    step(800);
    assert_standby("22:13", true);
    snapshot("standby-night");
    fixture_time = day + 17 * 3600;
    step(800);
    assert_standby("00:13", true);
    fixture_time = 0;
    step(800);
    assert_standby("--:--", false);
    fixture_time = day + 24 * 3600;
    step(800);
    assert_standby("07:13", false);

    fixture_use_night_value = true;
    fixture_generation++;
    step(9500);
    assert_standby("07:13", false);
    fixture_night_value = true;
    fixture_night_valid = true;
    step(800);
    assert_standby("07:13", true);
    fixture_online = false;
    step(800);
    assert_standby("07:13", false);
    fixture_online = true;
    step(800);
    assert_standby("07:13", true);
    fixture_matched = false;
    step(800);
    assert_standby("07:13", false);
    fixture_matched = true;
    step(800);
    assert_standby("07:13", true);
    tap(SIDE / 2, 677);
    assert(fixture_brightness == 10 && fixture_page == 3 && fixture_calls == commands);
    step(9000);
    assert_standby("07:13", true);
    fixture_night_value = false;
    step(800);
    assert_standby("07:13", false);
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "panel-preview SD_KÖNYVTÁR FIXTURE_JSON KIMENET\n");
        return 2;
    }
    fixture_sd = argv[1];
    fixture_profile = argv[2];
    output = argv[3];
    setenv("TZ", "UTC0", 1);
    tzset();
    lv_init();
    lv_display_t *d = lv_display_create(SIDE, SIDE);
    lv_display_set_color_format(d, LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(d, draw_buffer, NULL, sizeof(draw_buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d, draw);
    lv_indev_t *input = lv_indev_create();
    lv_indev_set_type(input, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(input, read_pointer);
    fixture_complete = false;
    fixture_online = false;
    fixture_assets = false;
    assert(panel_ui_start() == ESP_OK);
    step(1000);
    snapshot("wifi-networks");
    tap(200, 224);
    step(650);
    assert(has_label(lv_screen_active(), "Wi-Fi-jelszó"));
    snapshot("wifi-password");
    fixture_online = true;
    step(600);
    assert(has_label(lv_screen_active(), "http://192.168.1.42"));
    snapshot("setup-ip");
    fixture_complete = true;
    fixture_assets = true;
    fixture_generation++;
    step(800);
    assert(fixture_page == 0 && fixture_brightness == 80);
    snapshot("thermostat");
    tap(450, 610);
    assert(fixture_calls == 1);
    fixture_matched = false;
    tap(450, 610);
    assert(fixture_calls == 1);
    fixture_matched = true;
    step(600);
    tap(270, 40);
    assert(fixture_page == 1);
    snapshot("lighting");
    tap(100, 210);
    assert(fixture_calls == 2);
    tap(615, 115);
    step(300);
    tap(180, 361);
    step(300);
    assert(has_label(lv_screen_active(), "RGBW hangulatfény"));
    snapshot("lighting-outputs");
    tap(669, 59);
    tap(450, 42);
    assert(fixture_page == 2);
    snapshot("controls");
    tap(642, 41);
    assert(fixture_page == 3);
    step(1100);
    snapshot("energy");
    pointer.point = (lv_point_t){490, 400};
    pointer.state = LV_INDEV_STATE_PRESSED;
    step(100);
    for (int x = 480; x >= 220; x -= 15) {
        pointer.point.x = x;
        step(30);
    }
    pointer.state = LV_INDEV_STATE_RELEASED;
    step(650);
    assert(has_label(lv_screen_active(), "Mai összesítő"));
    snapshot("energy-daily");
    step(9000);
    assert(fixture_brightness == 10 && fixture_page == -1);
    assert_standby("07:13", false);
    snapshot("standby");
    test_night_clock();
    unsigned before = fixture_calls;
    tap(270, 42);
    assert(fixture_brightness == 80 && fixture_page == 3 && fixture_calls == before);
    step(9000);
    assert(fixture_page == -1);
    tap(SIDE / 2, 677);
    assert(fixture_brightness == 80 && fixture_page == 3 && fixture_calls == before);
    puts("Rendben: Wi-Fi varázsló, webes átadás, lapváltás, IRC és jelenetparancs, kimenetek, energia, "
         "képernyőkímélő és ébresztés.");
    return 0;
}
