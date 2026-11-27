#pragma once
#include "app_loxone_home_model.h"
typedef struct {
    lx_mapping_t fields[LX_FIELD_COUNT];
    bool enabled;
} panel_energy_mapping_t;

typedef struct {
    lx_config_t connection;
    lx_home_config_t home;
    panel_energy_mapping_t energy[LX_HOME_MAX_PAGES];
} panel_profile_t;

bool panel_profile_read(panel_profile_t *out, const cJSON *panel,
                        const char *password, const char *camera_password);
