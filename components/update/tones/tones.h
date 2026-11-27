#pragma once
#include "zld_update.h"
esp_err_t zld_tones_register(httpd_handle_t server,zld_update_authorize_t authorize);
bool zld_update_enter(void);
void zld_update_leave(void);
bool zld_tones_busy(void);
