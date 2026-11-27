#include "panel_ui.h"
#include "app_loxone_wordmark.h"
#include "panel_ui_style.h"
#include "panel_config.h"
#include "panel_controls.h"
#include "panel_energy.h"
#include "panel_intercom_ui.h"
#include "zld_loxone.h"
#include "zld_update.h"
#include "app_setup_ui.h"
#include "board.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static lv_obj_t *screen, *tabs_bar, *body, *saver_layer, *clock_label;
static lv_obj_t *tab_buttons[LX_HOME_MAX_PAGES];
static cJSON *profile;
static const cJSON *tabs, *display;
static unsigned generation, active_tab;
static uint32_t last_service, last_widgets;
static int brightness = -1;
static bool loaded, ready, runtime, saver, energy_page, scan_sent, night;
static char ip[16], clock_text[8];

static const cJSON *get(const cJSON *o, const char *k)
{
    return cJSON_GetObjectItemCaseSensitive(o, k);
}

static const char *str(const cJSON *o, const char *k)
{
    const cJSON *v = get(o, k);
    return cJSON_IsString(v) ? v->valuestring : "";
}

static int number(const cJSON *o, const char *k, int fallback)
{
    const cJSON *v = get(o, k);
    return cJSON_IsNumber(v) ? v->valueint : fallback;
}

static void illuminate(int value)
{
    if (brightness != value && board_set_brightness((unsigned)value) == ESP_OK)
        brightness = value;
}

static void delete_view(void)
{
    panel_controls_close();
    if (saver_layer)
        lv_obj_delete(saver_layer);
    if (body)
        lv_obj_delete(body);
    if (tabs_bar)
        lv_obj_delete(tabs_bar);
    body = tabs_bar = saver_layer = clock_label = NULL;
    memset(tab_buttons, 0, sizeof(tab_buttons));
    saver = false;
    energy_page = false;
}

static void open_tab(unsigned index);
static void tab_event(lv_event_t *event)
{
    open_tab((unsigned)(uintptr_t)lv_event_get_user_data(event));
}

static void build_tabs(void)
{
    tabs_bar = panel_box(screen, 0, 0, 720, 80, 0x08090A, 0);
    lv_obj_add_flag(tabs_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(tabs_bar, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(tabs_bar, LV_SCROLLBAR_MODE_OFF);
    int count = cJSON_GetArraySize(tabs);
    int width = count > 5 ? 130 : 720 / (count ? count : 1);
    for (int i = 0; i < count && i < LX_HOME_MAX_PAGES; ++i) {
        const cJSON *t = cJSON_GetArrayItem(tabs, i);
        lv_obj_t *b = panel_box(tabs_bar, i * width, 0, width, 80, 0x08090A, 0);
        tab_buttons[i] = b;
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(b, tab_event, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        lv_obj_t *icon = lv_image_create(b);
        char path[96];
        snprintf(path, sizeof(path), "icons/%s.png", str(t, "icon"));
        panel_image(icon, path);
        lv_obj_set_size(icon, 43, 43);
        lv_image_set_inner_align(icon, LV_IMAGE_ALIGN_STRETCH);
        lv_obj_center(icon);
    }
}

static void open_tab(unsigned index)
{
    if (saver || index >= (unsigned)cJSON_GetArraySize(tabs))
        return;
    panel_controls_close();
    if (body)
        lv_obj_delete(body);
    active_tab = index;
    app_loxone_home_visible((int)index);
    for (unsigned i = 0; i < LX_HOME_MAX_PAGES && tab_buttons[i]; ++i)
        lv_obj_set_style_opa(tab_buttons[i], i == index ? LV_OPA_COVER : LV_OPA_40, 0);
    lv_obj_scroll_to_view(tab_buttons[index], LV_ANIM_OFF);
    const cJSON *t = cJSON_GetArrayItem(tabs, (int)index);
    energy_page = !strcmp(str(t, "kind"), "energy");
    body = panel_box(screen, 0, 80, 720, 640, 0x101316, 0);
    if (energy_page) {
        panel_energy_open(body, generation);
        panel_energy_update(index, night, clock_text);
        return;
    }
    const cJSON *bg = get(t, "background");
    bool image = !strcmp(str(bg, "type"), "image");
    uint32_t color = 0x293239;
    if (!image) {
        const char *s = str(bg, "value");
        if (*s == '#')
            color = (uint32_t)strtoul(s + 1, NULL, 16);
        lv_obj_set_style_bg_color(body, lv_color_hex(color), 0);
    } else {
        lv_obj_t *wall = lv_image_create(body);
        char path[100];
        snprintf(path, sizeof(path), "backgrounds/%s.jpg", str(bg, "value"));
        panel_image(wall, path);
        lv_obj_set_pos(wall, 0, -40);
        lv_obj_t *shade = panel_box(body, 0, 0, 720, 640, 0, 0);
        lv_obj_set_style_bg_opa(shade, LV_OPA_50, 0);
    }
    uint32_t foreground = !image && lv_color_luminance(lv_color_hex(color)) > 148 ? 0x11171C : 0xF4F6F7;
    const cJSON *blocks = get(t, "blocks");
    if (cJSON_GetArraySize(blocks) == 0) {
        lv_obj_t *l = panel_label(body, "Ez a lap még üres.", 0, 0, &app_font_28, foreground);
        lv_obj_center(l);
    } else
        panel_controls_open(body, blocks, foreground, night, generation);
}

static void wake(void)
{
    if (!saver)
        return;
    lv_obj_delete(saver_layer);
    saver_layer = clock_label = NULL;
    saver = false;
    lv_obj_set_style_opa(tabs_bar, LV_OPA_COVER, 0);
    lv_display_trigger_activity(lv_display_get_default());
    open_tab(active_tab);
    illuminate(number(display, night ? "nightBrightness" : "dayBrightness", night ? 10 : 80));
}

static void wake_event(lv_event_t *event)
{
    lv_indev_t *indev = lv_indev_active();
    if (indev)
        lv_indev_wait_release(indev);
    lv_event_stop_bubbling(event);
    wake();
}

LV_FONT_DECLARE(app_font_standby_132);
static const uint32_t standby_ink = 0x8B8C8E;

static void standby_brand(lv_obj_t *layer)
{
    lv_obj_t *line = lv_obj_create(layer);
    lv_obj_remove_style_all(line);
    lv_obj_remove_flag(line, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(line, LV_SIZE_CONTENT, 35);
    lv_obj_set_flex_flow(line, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(line, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(line, 7, 0);
    lv_obj_align(line, LV_ALIGN_BOTTOM_MID, 0, -26);

    panel_label(line, "Zolee", 0, 0, &app_font_24, standby_ink);
    lv_obj_t *mark = lv_image_create(line);
    lv_image_set_src(mark, &loxone_wordmark);
    /* A 400 × 88-as szójel nagybetűi a kísérőszöveg 18 pixeles nagybetűivel egy magasságúak. */
    lv_obj_set_size(mark, 82, 18);
    lv_image_set_inner_align(mark, LV_IMAGE_ALIGN_STRETCH);
    lv_obj_set_style_image_recolor(mark, lv_color_hex(standby_ink), 0);
    lv_obj_set_style_image_recolor_opa(mark, LV_OPA_COVER, 0);
    panel_label(line, "Display", 0, 0, &app_font_24, standby_ink);
}

static void rest(void)
{
    saver = true;
    panel_controls_close();
    app_loxone_home_visible(-1);
    if (body)
        lv_obj_delete(body);
    body = NULL;
    energy_page = false;
    lv_obj_set_style_opa(tabs_bar, LV_OPA_30, 0);
    saver_layer = panel_box(screen, 0, 0, 720, 720, 0, 0);
    lv_obj_set_style_bg_opa(saver_layer, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(saver_layer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(saver_layer, wake_event, LV_EVENT_PRESSED, NULL);
    clock_label = panel_label(saver_layer, clock_text, 0, 0, &app_font_standby_132, standby_ink);
    lv_obj_center(clock_label);
    standby_brand(saver_layer);
    illuminate(number(display, "nightBrightness", 10));
}

static int minutes(const char *s)
{
    if (strlen(s) != 5)
        return 0;
    return 60 * ((s[0] - '0') * 10 + s[1] - '0') + (s[3] - '0') * 10 + s[4] - '0';
}

static void update_time(void)
{
    time_t now = time(NULL);
    struct tm local;
    bool valid = localtime_r(&now, &local) && local.tm_year >= 120;
    if (valid)
        strftime(clock_text, sizeof(clock_text), "%H:%M", &local);
    else
        snprintf(clock_text, sizeof(clock_text), "--:--");
    bool next = night;
    if (!strcmp(str(display, "nightMode"), "value"))
        zld_loxone_night(&next);
    else if (valid) {
        int start = minutes(str(display, "nightStart")), end = minutes(str(display, "nightEnd"));
        int current = local.tm_hour * 60 + local.tm_min;
        next = start > end ? current >= start || current < end : current >= start && current < end;
    }
    night = next;
}

static void refresh_config(void)
{
    unsigned current = panel_config_generation();
    if (profile && generation == current)
        return;
    cJSON *next = panel_config_snapshot();
    if (!next)
        return;
    if (loaded) {
        app_loxone_home_visible(-1);
        delete_view();
        loaded = false;
    }
    cJSON_Delete(profile);
    profile = next;
    generation = current;
    display = get(profile, "display");
    tabs = get(profile, "tabs");
    if (active_tab >= (unsigned)cJSON_GetArraySize(tabs))
        active_tab = 0;
}

static void service(void)
{
    refresh_config();
    update_time();
    const cJSON *lx = get(profile, "loxone");
    bool online = board_network_address(ip, sizeof(ip));
    ready = zld_assets_ready() && *str(lx, "host") && *str(lx, "user") && cJSON_GetArraySize(tabs) > 0;
    if (!ready || (!runtime && !online)) {
        if (loaded) {
            app_loxone_home_visible(-1);
            delete_view();
            loaded = false;
        }
        illuminate(number(display, "dayBrightness", 80));
        app_setup_wifi_state_t state;
        app_setup_wifi_snapshot(&state);
        if (online) {
            state.phase = APP_SETUP_WEB;
            snprintf(state.ip, sizeof(state.ip), "%s", ip);
            state.assets_missing = !zld_assets_ready();
        } else if (!scan_sent) {
            scan_sent = true;
            app_setup_wifi_scan();
            state.phase = APP_SETUP_SCANNING;
        } else if (state.phase == APP_SETUP_WEB)
            state.phase = APP_SETUP_SELECT;
        app_setup_ui_update(screen, 720, 720, &state);
        return;
    }
    runtime = true;
    if (!loaded) {
        app_setup_ui_close();
        build_tabs();
        open_tab(active_tab);
        lv_display_trigger_activity(lv_display_get_default());
        loaded = true;
    }
    if (panel_intercom_active()) {
        wake();
        lv_display_trigger_activity(lv_display_get_default());
    } else if (!saver) {
        int seconds = number(display, "saverSeconds", 0);
        if (seconds > 0 && lv_display_get_inactive_time(lv_display_get_default()) / 1000 >= (unsigned)seconds)
            rest();
    }
    if (saver)
        panel_text(clock_label, clock_text);
    illuminate(
        number(display, saver || night ? "nightBrightness" : "dayBrightness", saver || night ? 10 : 80));
}

static void tick(lv_timer_t *timer)
{
    (void)timer;
    uint32_t now = lv_tick_get();
    if (now - last_service >= 350) {
        last_service = now;
        service();
    }
    if (loaded && !saver) {
        if (energy_page)
            panel_energy_update(active_tab, night, clock_text);
        else if (now - last_widgets >= 250) {
            last_widgets = now;
            panel_controls_update();
        }
    }
    panel_intercom_ui_tick(screen);
}

esp_err_t panel_ui_start(void)
{
    if (!board_display_lock(3000))
        return ESP_ERR_TIMEOUT;
    screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    last_service = lv_tick_get() - 400;
    lv_timer_t *timer = lv_timer_create(tick, 50, NULL);
    board_display_unlock();
    return timer ? ESP_OK : ESP_ERR_NO_MEM;
}
