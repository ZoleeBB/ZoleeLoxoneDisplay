#pragma once
#include "app_loxone.h"
#include "app_loxone_controls_service.h"
#include "app_loxone_audio_service.h"

esp_err_t zld_loxone_start(bool (*network_ready)(void));
/* A kérés a konfigurációt mélyen másolja. A hívó a visszatéréskor azonnal
 * törölheti a két jelszót és a JSON-t. Foglalt sornál a hívó újrapróbál. */
esp_err_t zld_loxone_configure(const cJSON *panel, const char *password, const char *camera_password);
cJSON *zld_loxone_catalog(void);
esp_err_t zld_loxone_refresh(void);
bool zld_loxone_refreshing(void);
bool zld_loxone_status(char *out, size_t size);
unsigned zld_loxone_revision(unsigned panel_generation);
bool zld_loxone_card(const char *id, lx_home_value_t *out, bool *supported);
bool zld_loxone_night(bool *night);
bool zld_loxone_energy(unsigned tab, lx_values_t *out);
bool zld_loxone_energy_at(unsigned tab, unsigned generation, lx_values_t *out);
bool zld_loxone_mood(const char *id, unsigned revision, unsigned index, lx_light_mood_t *out, bool *active);
esp_err_t zld_loxone_select_mood(const char *id, unsigned revision, unsigned mood_id);

void zld_loxone_pause_media(bool paused);
