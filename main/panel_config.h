#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cJSON.h"
#include "esp_err.h"

#define PANEL_SECRET_CAP 128
#define PANEL_CONFIG_MAX_BYTES (64 * 1024)

esp_err_t panel_config_init(void);
uint32_t panel_config_generation(void);
/* Caller owns the snapshot. Passwords and their encrypted representations are absent. */
cJSON *panel_config_snapshot(void);
esp_err_t panel_config_read_secret(const char *key, char *out, size_t size);
/* A consistent, small credential read for HTTP requests that stream assets. */
bool panel_config_web_login(char *user, size_t user_size, char pass[PANEL_SECRET_CAP]);
/* Accepts a public snapshot with matching revision and optional top-level passwords.
 * Failure leaves the caller's JSON and the current settings intact. */
esp_err_t panel_config_save(const cJSON *input, char *error, size_t error_size);
cJSON *panel_parse_document(const char *body, size_t length);
