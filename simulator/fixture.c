/* Kizárólag a hardvert és az élő állapotforrásokat helyettesítő helyi adatok. */
#include "fixture.h"
#include "board.h"
#include "app_setup_wifi.h"
#include "panel_config.h"
#include "panel_energy_stats.h"
#include "panel_intercom_ui.h"
#include "zld_loxone.h"
#include "zld_update.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

bool fixture_online = true, fixture_assets = true, fixture_complete = true;
unsigned fixture_generation = 1, fixture_brightness, fixture_calls;
int fixture_page = -1;
bool fixture_matched = true;
const char *fixture_profile, *fixture_sd;
bool fixture_touch, fixture_doorbell;
unsigned fixture_tap_window = 400;
uint32_t fixture_session = 1;
fixture_touch_event_t fixture_touch_trace[48];
unsigned fixture_touch_count, fixture_touch_cancels;
bool fixture_has_touch_section;
static unsigned active_scene = 2;
static double target = 22;
static char selected_ssid[33];

time_t fixture_time = 1795763580;
bool fixture_use_night_value, fixture_night_value, fixture_night_valid;

time_t __wrap_time(time_t *out)
{
    if (out)
        *out = fixture_time;
    return fixture_time;
}
bool board_display_lock(uint32_t timeout_ms)
{
    (void)timeout_ms;
    return true;
}
void board_display_unlock(void)
{
}
esp_err_t board_set_brightness(unsigned percent)
{
    fixture_brightness = percent;
    return ESP_OK;
}
bool board_network_address(char *out, size_t cap)
{
    snprintf(out, cap, "192.168.1.42");
    return fixture_online;
}
bool zld_assets_ready(void)
{
    return fixture_assets;
}
bool zld_asset_path(const char *name, char *out, size_t n)
{
    int used = snprintf(out, n, "%s/%s", fixture_sd, name);
    return used > 0 && (size_t)used < n;
}
bool panel_intercom_active(void)
{
    return fixture_doorbell;
}
void panel_intercom_ui_tick(lv_obj_t *screen)
{
    (void)screen;
}
bool app_setup_wifi_scan(void)
{
    return true;
}
bool app_setup_wifi_connect(const char *ssid, const char *password)
{
    snprintf(selected_ssid, sizeof(selected_ssid), "%s", ssid);
    fixture_online = password && *password;
    return fixture_online;
}
void app_setup_wifi_snapshot(app_setup_wifi_state_t *out)
{
    *out = (app_setup_wifi_state_t){
        .phase = APP_SETUP_SELECT, .count = 2, .aps = {{"Otthon", true}, {"Vendéghálózat", true}}};
}
uint32_t panel_config_generation(void)
{
    return fixture_generation;
}
cJSON *panel_config_snapshot(void)
{
    FILE *file = fopen(fixture_profile, "rb");
    if (!file)
        return NULL;
    char input[8192];
    size_t n = fread(input, 1, sizeof(input) - 1, file);
    fclose(file);
    input[n] = 0;
    cJSON *profile = cJSON_Parse(input);
    if (fixture_touch) {
        cJSON *touch = cJSON_GetObjectItemCaseSensitive(profile, "touch");
        cJSON_SetBoolValue(cJSON_GetObjectItemCaseSensitive(touch, "enabled"), true);
        cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(touch, "tapWindowMs"), fixture_tap_window);
    } else if (!fixture_has_touch_section) {
        /* Régi, Touch szakasz nélkül mentett profil. */
        cJSON_DeleteItemFromObjectCaseSensitive(profile, "touch");
    }
    if (fixture_use_night_value) {
        cJSON *settings = cJSON_GetObjectItemCaseSensitive(profile, "display");
        cJSON_SetValuestring(cJSON_GetObjectItemCaseSensitive(settings, "nightMode"), "value");
    }
    if (!fixture_complete)
        cJSON_DeleteItemFromObject(profile, "tabs");
    return profile;
}
uint32_t zld_touch_session(unsigned generation)
{
    return fixture_online && fixture_matched && generation == fixture_generation ? fixture_session : 0;
}
esp_err_t zld_touch_press(unsigned generation, uint32_t session, unsigned zone, unsigned taps)
{
    /* A teszt minden átadási kísérletet rögzít, így egy elutasított kérés sem marad láthatatlan. */
    if (fixture_touch_count >= sizeof(fixture_touch_trace) / sizeof(fixture_touch_trace[0])) abort();
    fixture_touch_trace[fixture_touch_count++] = (fixture_touch_event_t){zone, taps, session, lv_tick_get()};
    return session && session == zld_touch_session(generation) ? ESP_OK : ESP_ERR_INVALID_STATE;
}
void zld_touch_cancel(void)
{
    fixture_touch_cancels++;
}
unsigned zld_loxone_revision(unsigned gen)
{
    return fixture_matched && gen == fixture_generation ? gen : 0;
}
bool zld_loxone_night(unsigned generation, bool *night)
{
    if (generation != fixture_generation || !fixture_matched || !fixture_night_valid || !fixture_online)
        return false;
    *night = fixture_night_value;
    return true;
}
void app_loxone_home_visible(int page)
{
    fixture_page = page;
}
bool zld_loxone_card(const char *id, lx_home_value_t *out, bool *supported)
{
    out->value = !strcmp(id, "room") ? 21.5 : 1;
    out->valid = true;
    *supported = true;
    return true;
}
esp_err_t app_loxone_home_command(const char *id, unsigned revision, lx_home_command_t action)
{
    (void)id;
    if (!fixture_matched || revision != fixture_generation)
        return ESP_ERR_INVALID_STATE;
    fixture_calls++;
    if (action == LX_HOME_COMMAND_IRC_INCREASE)
        target += .5;
    if (action == LX_HOME_COMMAND_IRC_DECREASE)
        target -= .5;
    return ESP_OK;
}
bool app_loxone_irc_summary(const char *id, unsigned revision, lx_irc_summary_t *out)
{
    (void)id;
    (void)revision;
    *out = (lx_irc_summary_t){.target_c = target,
                              .actual_c = 19.3,
                              .target_valid = true,
                              .actual_valid = true,
                              .available = true,
                              .controllable = true,
                              .direction = LX_IRC_DIRECTION_HEATING};
    return true;
}
bool app_loxone_light_summary(const char *id, unsigned revision, lx_light_summary_t *out)
{
    (void)id;
    (void)revision;
    *out = (lx_light_summary_t){
        .available = true, .controllable = true, .label = LX_LIGHT_ONE_MOOD, .active_count = 1};
    return true;
}
bool zld_loxone_mood(const char *id, unsigned revision, unsigned index, lx_light_mood_t *out, bool *active)
{
    (void)id;
    (void)revision;
    const char *items[] = {"Kikapcsolva", "Nappali fény",  "Olvasás",  "Filmnézés",
                           "Vacsora",     "Éjszakai fény", "Vendégség"};
    if (index >= sizeof(items) / sizeof(*items))
        return false;
    snprintf(out->name, sizeof(out->name), "%s", items[index]);
    out->id = index;
    *active = index == active_scene;
    return true;
}
esp_err_t zld_loxone_select_mood(const char *id, unsigned revision, unsigned mood)
{
    (void)id;
    (void)revision;
    fixture_calls++;
    active_scene = mood;
    return ESP_OK;
}
unsigned app_loxone_light_open(const char *id, unsigned revision)
{
    (void)id;
    (void)revision;
    return 1;
}
void app_loxone_light_close(unsigned handle)
{
    (void)handle;
}
bool app_loxone_light_info(unsigned handle, app_loxone_light_info_t *out)
{
    (void)handle;
    *out = (app_loxone_light_info_t){.ready = true, .connected = true, .circuit_count = 3};
    return true;
}
bool app_loxone_light_circuit(unsigned handle, unsigned i, lx_light_circuit_t *out)
{
    (void)handle;
    const char *names[] = {"Fali olvasólámpa", "Mennyezeti dimmer", "RGBW hangulatfény"};
    if (i >= 3)
        return false;
    *out = (lx_light_circuit_t){.type = (lx_light_type_t)(1 + i),
                                .rgb = true,
                                .white = true,
                                .lock_known = true,
                                .min_kelvin = 2600,
                                .max_kelvin = 6500,
                                .actual = {.valid = true,
                                           .mode = LX_LIGHT_MODE_HSV,
                                           .hue = 170,
                                           .brightness = 68,
                                           .saturation = 64,
                                           .kelvin = 3200}};
    snprintf(out->name, sizeof(out->name), "%s", names[i]);
    return true;
}
esp_err_t app_loxone_light_set(unsigned handle, unsigned i, const lx_light_value_t *v)
{
    (void)handle;
    (void)i;
    (void)v;
    fixture_calls++;
    return ESP_OK;
}
bool zld_loxone_energy_at(unsigned tab, unsigned revision, lx_values_t *v)
{
    (void)tab;
    (void)revision;
    *v = (lx_values_t){.value = {-.7, -1.3, 3.8, 1.8, 78, 1, 0, 25, 15.4, 8, 1, 0}};
    for (int i = 0; i < 12; i++)
        v->seen[i] = true;
    v->seen[18] = true;
    v->value[18] = 60;
    return true;
}
bool app_loxone_shading_summary(const char *id, unsigned revision, lx_shading_summary_t *v)
{
    (void)id;
    (void)revision;
    *v = (lx_shading_summary_t){
        .position_valid = true, .available = true, .controllable = true, .position_percent = 25};
    return true;
}
esp_err_t app_loxone_shading_position(const char *id, unsigned rev, unsigned val)
{
    (void)id;
    (void)rev;
    (void)val;
    fixture_calls++;
    return ESP_OK;
}
esp_err_t app_loxone_shading_command(const char *id, unsigned rev, lx_home_command_t action)
{
    (void)id;
    (void)rev;
    (void)action;
    fixture_calls++;
    return ESP_OK;
}
bool app_loxone_controls_summary(const char *id, unsigned rev, lx_controls_summary_t *v)
{
    (void)id;
    (void)rev;
    *v = (lx_controls_summary_t){0};
    return false;
}
bool app_loxone_controls_window(const char *id, unsigned rev, unsigned i, lx_controls_window_t *v)
{
    (void)id;
    (void)rev;
    (void)i;
    (void)v;
    return false;
}
esp_err_t app_loxone_controls_command(const char *id, unsigned rev, lx_controls_action_t a)
{
    (void)id;
    (void)rev;
    (void)a;
    fixture_calls++;
    return ESP_OK;
}
esp_err_t app_loxone_controls_set(const char *id, unsigned rev, double val)
{
    (void)id;
    (void)rev;
    (void)val;
    fixture_calls++;
    return ESP_OK;
}
bool app_loxone_audio_summary(const char *id, unsigned rev, lx_audio_summary_t *v)
{
    (void)id;
    (void)rev;
    *v = (lx_audio_summary_t){.controllable = true,
                              .available = true,
                              .native_ready = true,
                              .volume_max = 100,
                              .track = {.title = "Reggeli jazz",
                                        .artist = "Kedvencek",
                                        .mode = LX_AUDIO_MODE_PLAY,
                                        .volume_known = true,
                                        .volume = 38}};
    return true;
}
bool app_loxone_audio_allowed(const char *id, unsigned rev, lx_audio_action_t a)
{
    (void)id;
    (void)rev;
    (void)a;
    return true;
}
esp_err_t app_loxone_audio_favorites_request(const char *id, unsigned rev, unsigned start)
{
    (void)id;
    (void)rev;
    (void)start;
    return ESP_OK;
}
bool app_loxone_audio_favorites(const char *id, unsigned rev, lx_audio_favorites_t *f)
{
    (void)id;
    (void)rev;
    (void)f;
    return false;
}
esp_err_t app_loxone_audio_favorite(const char *id, unsigned rev, unsigned v, unsigned gen)
{
    (void)id;
    (void)rev;
    (void)v;
    (void)gen;
    fixture_calls++;
    return ESP_OK;
}
esp_err_t app_loxone_audio_command(const char *id, unsigned rev, lx_audio_action_t a, unsigned v, bool final)
{
    (void)id;
    (void)rev;
    (void)a;
    (void)v;
    (void) final;
    fixture_calls++;
    return ESP_OK;
}
void app_loxone_audio_detail_close(void)
{
}

bool panel_energy_stats_read(unsigned tab, unsigned revision, panel_daily_t *out, bool *live)
{
    (void)tab;
    (void)revision;
    *live = true;
    *out = (panel_daily_t){.production = 12.4,
                           .consumption = 8.9,
                           .imported = 2.2,
                           .exported = 4.1,
                           .charged = 3.8,
                           .discharged = 2.1,
                           .pv_home = 4.6,
                           .pv_stored = 3.5,
                           .bat_home = 2.1,
                           .seen = 31};
    return true;
}
