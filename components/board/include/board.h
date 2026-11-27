#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "sdkconfig.h"

#if CONFIG_PANEL_ETHERNET
#define BOARD_SKU 31570
#else
#define BOARD_SKU 31416
#endif

#if CONFIG_ESP32P4_SELECTS_REV_LESS_V3
#define BOARD_SILICON "rev1"
#else
#define BOARD_SILICON "rev3"
#endif

typedef struct {
    char ssid[33];
    int rssi;
    bool secured;
} board_ap_t;

esp_err_t board_display_start(void);
bool board_display_lock(uint32_t timeout_ms);
void board_display_unlock(void);
esp_err_t board_set_brightness(unsigned percent);
esp_err_t board_mount_sd(void);

/* A hálózat inicializálása előtt az alkalmazás az NVS-t már megnyitotta. */
esp_err_t board_network_init(void);
bool board_network_address(char *out, size_t capacity);
/* A keresés blokkoló művelet, az LVGL-szálon kívül futtassuk. */
esp_err_t board_wifi_scan(board_ap_t *out, size_t capacity, size_t *count);
esp_err_t board_wifi_connect(const char *ssid, const char *password);
bool board_wifi_connect_failed(void);
