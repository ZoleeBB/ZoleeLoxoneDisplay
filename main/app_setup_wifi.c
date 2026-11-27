#include "app_setup_wifi.h"
#include "board.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static app_setup_wifi_state_t state = {.phase = APP_SETUP_SELECT};
static bool busy;

typedef struct {
    char ssid[APP_WIFI_SSID_MAX], password[APP_WIFI_PASSWORD_MAX];
} join_t;

static void run_scan(void *unused)
{
    (void)unused;
    board_ap_t *aps = calloc(APP_SETUP_WIFI_MAX_APS, sizeof(*aps));
    size_t count = 0;
    esp_err_t result = aps ? board_wifi_scan(aps, APP_SETUP_WIFI_MAX_APS, &count) : ESP_ERR_NO_MEM;
    taskENTER_CRITICAL(&lock);
    state.count = 0;
    if (result == ESP_OK) {
        for (unsigned i = 0; i < count && i < APP_SETUP_WIFI_MAX_APS; ++i) {
            if (!aps[i].ssid[0]) continue;
            app_setup_ap_t *row = &state.aps[state.count++];
            memcpy(row->ssid, aps[i].ssid, sizeof(row->ssid));
            row->secured = aps[i].secured;
        }
    }
    state.phase = result == ESP_OK ? APP_SETUP_SELECT : APP_SETUP_FAILED;
    busy = false;
    taskEXIT_CRITICAL(&lock);
    free(aps);
    vTaskDelete(NULL);
}

static void run_join(void *arg)
{
    join_t *join = arg;
    esp_err_t result = board_wifi_connect(join->ssid, join->password);
    volatile char *secret = join->password;
    for (unsigned i = 0; i < sizeof(join->password); i++) secret[i] = 0;
    free(join);
    char ip[16];
    bool online = false;
    for (int i = 0; result == ESP_OK && i < 100; ++i) {
        if ((online = board_network_address(ip, sizeof(ip))) || board_wifi_connect_failed()) break;
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    taskENTER_CRITICAL(&lock);
    state.phase = online ? APP_SETUP_WEB : APP_SETUP_FAILED;
    busy = false;
    taskEXIT_CRITICAL(&lock);
    vTaskDelete(NULL);
}

static bool start_worker(join_t *join)
{
    taskENTER_CRITICAL(&lock);
    bool allowed = !busy;
    if (allowed) {
        busy = true;
        state.phase = join ? APP_SETUP_CONNECTING : APP_SETUP_SCANNING;
        if (join) memcpy(state.selected_ssid, join->ssid, sizeof(state.selected_ssid));
    }
    taskEXIT_CRITICAL(&lock);
    if (allowed && xTaskCreate(join ? run_join : run_scan, "panel_wifi", 4096, join, 4, NULL) == pdPASS)
        return true;
    if (allowed) {
        taskENTER_CRITICAL(&lock);
        state.phase = APP_SETUP_FAILED;
        busy = false;
        taskEXIT_CRITICAL(&lock);
    }
    if (join) {
        volatile char *secret = join->password;
        for (unsigned i = 0; i < sizeof(join->password); ++i) secret[i] = 0;
        free(join);
    }
    return false;
}

bool app_setup_wifi_scan(void)
{
    return start_worker(NULL);
}

bool app_setup_wifi_connect(const char *ssid, const char *password)
{
    join_t *join = calloc(1, sizeof(*join));
    if (!join) return false;
    snprintf(join->ssid, sizeof(join->ssid), "%s", ssid);
    snprintf(join->password, sizeof(join->password), "%s", password);
    return start_worker(join);
}

void app_setup_wifi_snapshot(app_setup_wifi_state_t *out)
{
    taskENTER_CRITICAL(&lock);
    *out = state;
    taskEXIT_CRITICAL(&lock);
}
