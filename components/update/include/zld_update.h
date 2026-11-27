#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Establish identity after SD mount and before loading settings. */
bool zld_card_session_start(void);
/* Fast cached availability used by render/network workers. */
bool zld_card_online(void);
/* Verify the session marker on the actual card before accessing protected data. */
bool zld_card_verify(void);
/* Call in app_main at ~2 s intervals. A true return reserves a safe restart. */
bool zld_card_needs_restart(void);

/* Call in task context after mounting /sdcard, or again after remount. */
bool zld_assets_check(void);
bool zld_assets_ready(void);
/* Lookup immutable, absolute POSIX path for firmware's logical file. */
bool zld_asset_path(const char *logical, char *out, size_t capacity);
typedef bool (*zld_update_authorize_t)(httpd_req_t *req, bool write);
esp_err_t zld_update_register(httpd_handle_t server, zld_update_authorize_t authorize);

#ifdef __cplusplus
}
#endif
