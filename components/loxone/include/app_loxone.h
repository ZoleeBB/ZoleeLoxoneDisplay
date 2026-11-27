#pragma once
#include "esp_err.h"
#include "app_loxone_home_runtime.h"
/* UI-híváskor a pillanatkép memóriáját a hívó biztosítja, SD/hálózat nélkül. */
bool app_loxone_home_snapshot(lx_home_config_t *config, lx_home_values_t *values,
                              unsigned *revision, bool *connected,
                              bool *command_pending, char status[LX_HOME_ERROR_SIZE]);
esp_err_t app_loxone_home_command(const char *card_id, lx_home_command_t action);

typedef struct
{
    lx_intercom_config_t config;
    bool ringing, connected, gate_supported, command_pending;
    bool audio_active;
    bool audio_elsewhere, ringtone_silenced;
    unsigned revision;
    char status[LX_HOME_ERROR_SIZE];
} app_intercom_snapshot_t;
bool app_loxone_intercom_snapshot(app_intercom_snapshot_t *out);
/* Védett Slave RPC: a generációellenőrzés és a sorba helyezés atomi. */
esp_err_t app_loxone_intercom_gate_request(unsigned revision);
/* A teszthang real-time ciklusát nem várakoztatja a Loxone mutexére.
 * Foglalt állapotnál a következő blokkban újra ellenőrizhető a leállítás. */
bool app_loxone_intercom_call_permitted(unsigned revision, bool active);
/* Kizárólag a helyi videófeladatnak, a webes válaszba soha nem kerül jelszó. */
bool app_loxone_intercom_credentials(unsigned revision, char username[96], char password[LX_PASSWORD_MAX]);

#include "app_loxone_lighting_service.h"
#include "app_loxone_irc_service.h"
#include "app_loxone_shading_service.h"
