#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "cJSON.h"

/* Validates public settings. No unknown keys (including credentials) can be persisted. */
bool panel_schema_validate(const cJSON *profile, char *error, size_t error_size);
