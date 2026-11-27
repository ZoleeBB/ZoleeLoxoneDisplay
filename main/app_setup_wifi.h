#pragma once
#include <stdbool.h>
#include <stdint.h>
#define APP_SETUP_WIFI_MAX_APS 16
#define APP_WIFI_SSID_MAX 33
#define APP_WIFI_PASSWORD_MAX 65

typedef enum {
    APP_SETUP_DONE, APP_SETUP_SCANNING, APP_SETUP_SELECT, APP_SETUP_CONNECTING,
    APP_SETUP_FAILED, APP_SETUP_WEB
} app_setup_phase_t;

typedef struct {
    char ssid[APP_WIFI_SSID_MAX];
    bool secured;
} app_setup_ap_t;

typedef struct {
    app_setup_phase_t phase;
    app_setup_ap_t aps[APP_SETUP_WIFI_MAX_APS];
    unsigned count;
    char ip[16], selected_ssid[APP_WIFI_SSID_MAX];
    bool assets_missing;
} app_setup_wifi_state_t;

bool app_setup_wifi_scan(void);
bool app_setup_wifi_connect(const char *ssid, const char *password);
void app_setup_wifi_snapshot(app_setup_wifi_state_t *out);
