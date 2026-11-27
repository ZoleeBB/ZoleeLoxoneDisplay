#pragma once
#include <stdbool.h>
#include "esp_http_server.h"

httpd_handle_t panel_web_start(void);
/* Sends a denial if authentication or the same-origin write check fails. */
bool panel_web_authorize(httpd_req_t *req, bool write);
