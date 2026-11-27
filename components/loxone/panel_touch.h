#pragma once
#include "app_loxone_model.h"

#define LX_TOUCH_CORNERS 4
#define LX_TOUCH_CENTER 4

typedef struct {
    char type[12], action[16], uuid[LX_UUID_LEN], input_uuid[LX_UUID_LEN];
    unsigned scene_id;
} lx_touch_mapping_t;

typedef struct {
    bool enabled;
    lx_touch_mapping_t zones[LX_TOUCH_CORNERS + 1];
} lx_touch_config_t;

typedef struct {
    bool numeric;
    double number;
    const char *text;
    size_t length;
} lx_touch_value_t;

typedef bool (*lx_touch_lookup_t)(const char *uuid, lx_touch_value_t *out);
bool lx_touch_attach(cJSON *entry, const cJSON *control, bool secured);
bool lx_touch_command(const cJSON *catalog, const lx_touch_mapping_t *target, lx_touch_lookup_t lookup, char out[96]);
cJSON *lx_touch_mood_list(const cJSON *catalog, const char *uuid, lx_touch_lookup_t lookup, const char **status);

bool lx_touch_ack_matches(const char *command, const char *control);
