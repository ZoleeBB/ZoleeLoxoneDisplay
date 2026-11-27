#include "board.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_check.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#if CONFIG_PANEL_ETHERNET
#include "esp_eth.h"
#endif

static const char *TAG = "board_network";
static _Atomic uint32_t station_ip;
static _Atomic uint32_t ethernet_ip;
static esp_netif_t *ethernet;
static SemaphoreHandle_t station_lock;
static esp_timer_handle_t retry_timer;
static atomic_bool wifi_available;
static atomic_bool connect_failed;
static atomic_bool has_credentials;
static atomic_bool pending_save;

typedef struct {
    char ssid[33];
    char password[65];
} credentials_t;

static credentials_t credentials;
ESP_EVENT_DEFINE_BASE(BOARD_RECONNECT_EVENT);

static bool read_credentials(void)
{
    nvs_handle_t handle;
    if (nvs_open("panel_wifi", NVS_READONLY, &handle) != ESP_OK)
        return false;
    size_t size = sizeof(credentials);
    esp_err_t error = nvs_get_blob(handle, "station_v1", &credentials, &size);
    nvs_close(handle);
    return error == ESP_OK && size == sizeof(credentials) && credentials.ssid[0] &&
           !credentials.ssid[32] && !credentials.password[64];
}

static esp_err_t save_credentials(void)
{
    nvs_handle_t handle;
    esp_err_t error = nvs_open("panel_wifi", NVS_READWRITE, &handle);
    if (error != ESP_OK)
        return error;
    error = nvs_set_blob(handle, "station_v1", &credentials, sizeof(credentials));
    if (error == ESP_OK)
        error = nvs_commit(handle);
    nvs_close(handle);
    return error;
}

static esp_err_t apply_credentials(const credentials_t *value)
{
    wifi_config_t config = {0};
    memcpy(config.sta.ssid, value->ssid, strlen(value->ssid));
    memcpy(config.sta.password, value->password, strlen(value->password));
    return esp_wifi_set_config(WIFI_IF_STA, &config);
}

static void post_retry(void *arg)
{
    if (has_credentials && !station_ip &&
        esp_event_post(BOARD_RECONNECT_EVENT, 0, NULL, 0, 0) != ESP_OK)
        esp_timer_start_once(retry_timer, 15000000);
}

static void schedule_retry(void)
{
    esp_timer_stop(retry_timer);
    esp_timer_start_once(retry_timer, 15000000);
}

static void on_network_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == BOARD_RECONNECT_EVENT) {
        if (station_ip)
            return;
        if (has_credentials && xSemaphoreTake(station_lock, 0) == pdTRUE) {
            if (esp_wifi_connect() != ESP_OK)
                schedule_retry();
            xSemaphoreGive(station_lock);
        } else if (has_credentials) {
            schedule_retry();
        }
    } else if (base == WIFI_EVENT) {
        station_ip = 0;
        const wifi_event_sta_disconnected_t *event = data;
        if (event->reason == WIFI_REASON_ASSOC_LEAVE)
            return;
        connect_failed = true;
        if (has_credentials)
            schedule_retry();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP) {
        station_ip = 0;
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = data;
        station_ip = event->ip_info.ip.addr;
        esp_timer_stop(retry_timer);
        connect_failed = false;
        /* Ha közben másik SSID-t kért a felhasználó, a korábbi DHCP-esemény
         * ne mentse el próba nélkül annak a jelszavát. */
        if (pending_save && xSemaphoreTake(station_lock, portMAX_DELAY) == pdTRUE) {
            wifi_ap_record_t ap;
            if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK &&
                strcmp((const char *)ap.ssid, credentials.ssid) == 0) {
                esp_err_t error = save_credentials();
                pending_save = error != ESP_OK;
                if (error != ESP_OK)
                    ESP_LOGW(TAG, "Wi-Fi NVS: %s", esp_err_to_name(error));
            }
            xSemaphoreGive(station_lock);
        }
    }
}

#if CONFIG_PANEL_ETHERNET
static void on_lan_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == IP_EVENT && id == IP_EVENT_ETH_GOT_IP) {
        const ip_event_got_ip_t *event = data;
        ethernet_ip = event->ip_info.ip.addr;
    } else {
        ethernet_ip = 0;
    }
}

static esp_err_t start_ethernet(void)
{
    /* A P4 alapértelmezett RMII-kiosztása (MDC31, MDIO52, REF_CLK50)
     * megfelel a 31570 kapcsolási rajzának. */
    eth_esp32_emac_config_t emac = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    eth_mac_config_t mac_cfg = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_cfg = ETH_PHY_DEFAULT_CONFIG();
    phy_cfg.reset_gpio_num = 51;
    phy_cfg.phy_addr = 1;
    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&emac, &mac_cfg);
    esp_eth_phy_t *phy = esp_eth_phy_new_ip101(&phy_cfg);
    esp_eth_handle_t driver = NULL;
    esp_eth_netif_glue_handle_t glue = NULL;
    esp_netif_inherent_config_t base = ESP_NETIF_INHERENT_DEFAULT_ETH();
    base.route_prio = 120;
    esp_netif_config_t cfg = {.base = &base, .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH};
    esp_err_t error = ESP_ERR_NO_MEM;
    if (!mac || !phy)
        goto fail;
    esp_eth_config_t eth_cfg = ETH_DEFAULT_CONFIG(mac, phy);
    error = esp_eth_driver_install(&eth_cfg, &driver);
    if (error != ESP_OK)
        goto fail;
    glue = esp_eth_new_netif_glue(driver);
    ethernet = esp_netif_new(&cfg);
    if (!ethernet || !glue) {
        error = ESP_ERR_NO_MEM;
        goto fail;
    }
    error = esp_netif_attach(ethernet, glue);
    if (error == ESP_OK)
        error = esp_eth_start(driver);
    if (error == ESP_OK)
        return ESP_OK;

fail:
    if (glue)
        esp_eth_del_netif_glue(glue);
    if (ethernet)
        esp_netif_destroy(ethernet);
    ethernet = NULL;
    if (driver)
        esp_eth_driver_uninstall(driver);
    if (phy)
        phy->del(phy);
    if (mac)
        mac->del(mac);
    return error;
}
#endif

static esp_err_t start_wifi(void)
{
    station_lock = xSemaphoreCreateMutex();
    esp_netif_t *station = esp_netif_create_default_wifi_sta();
    if (!station || !station_lock)
        return ESP_ERR_NO_MEM;
    esp_timer_create_args_t timer = {.callback = post_retry, .name = "wifi_retry"};
    ESP_RETURN_ON_ERROR(esp_timer_create(&timer, &retry_timer), TAG, "timer");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(BOARD_RECONNECT_EVENT, 0, on_network_event, NULL), TAG, "retry");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, on_network_event, NULL), TAG, "STA");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_network_event, NULL), TAG, "IPv4");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, on_network_event, NULL), TAG, "IPv4");
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "C6");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "STA storage");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "STA mode");
    has_credentials = read_credentials();
    if (has_credentials)
        ESP_RETURN_ON_ERROR(apply_credentials(&credentials), TAG, "STA configuration");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "STA start");
    wifi_available = true;
    if (has_credentials && esp_wifi_connect() != ESP_OK)
        schedule_retry();
    return ESP_OK;
}

esp_err_t board_network_init(void)
{
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "events");
#if CONFIG_PANEL_ETHERNET
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, on_lan_event, NULL), TAG, "LAN IPv4");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_LOST_IP, on_lan_event, NULL), TAG, "LAN IPv4");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(ETH_EVENT, ETHERNET_EVENT_DISCONNECTED, on_lan_event, NULL), TAG, "LAN link");
    esp_err_t lan = start_ethernet();
    if (lan != ESP_OK)
        ESP_LOGW(TAG, "Ethernet: %s", esp_err_to_name(lan));
#endif
    esp_err_t wifi = start_wifi();
    return ethernet ? ESP_OK : wifi;
}

bool board_network_address(char *out, size_t capacity)
{
    if (!out || !capacity)
        return false;
    out[0] = '\0';
    /* A UI és a Loxone kliens sűrűn kér állapotot. A DHCP-eseményben eltett
     * cím olvasásakor nincs lwIP IPC vagy SDIO-forgalom. */
    esp_ip4_addr_t ip = {.addr = atomic_load(&ethernet_ip)};
    if (!ip.addr)
        ip.addr = atomic_load(&station_ip);
    if (!ip.addr)
        return false;
    int length = snprintf(out, capacity, IPSTR, IP2STR(&ip));
    return length > 0 && (size_t)length < capacity;
}

esp_err_t board_wifi_scan(board_ap_t *out, size_t capacity, size_t *count)
{
    if (!out || !count || !capacity || capacity > 64)
        return ESP_ERR_INVALID_ARG;
    *count = 0;
    if (!wifi_available || xSemaphoreTake(station_lock, 0) != pdTRUE)
        return ESP_ERR_INVALID_STATE;
    wifi_ap_record_t *records = calloc(capacity, sizeof(*records));
    esp_err_t error = records ? esp_wifi_scan_start(NULL, true) : ESP_ERR_NO_MEM;
    if (error == ESP_OK) {
        uint16_t total = capacity;
        error = esp_wifi_scan_get_ap_records(&total, records);
        if (error != ESP_OK)
            esp_wifi_clear_ap_list();
        for (size_t i = 0; error == ESP_OK && i < total; i++) {
            if (!records[i].ssid[0])
                continue;
            board_ap_t *ap = &out[(*count)++];
            memcpy(ap->ssid, records[i].ssid, 32);
            ap->ssid[32] = '\0';
            ap->rssi = records[i].rssi;
            ap->secured = records[i].authmode != WIFI_AUTH_OPEN;
        }
    }
    free(records);
    xSemaphoreGive(station_lock);
    return error;
}

esp_err_t board_wifi_connect(const char *ssid, const char *password)
{
    if (!ssid || !ssid[0] || strlen(ssid) > 32 || !password || strlen(password) > 64)
        return ESP_ERR_INVALID_ARG;
    if (!wifi_available || xSemaphoreTake(station_lock, 0) != pdTRUE)
        return ESP_ERR_INVALID_STATE;
    esp_timer_stop(retry_timer);
    esp_wifi_disconnect();
    credentials_t next = {0};
    strlcpy(next.ssid, ssid, sizeof(next.ssid));
    strlcpy(next.password, password, sizeof(next.password));
    esp_err_t error = apply_credentials(&next);
    if (error == ESP_OK) {
        credentials = next;
        has_credentials = true;
        pending_save = true;
        error = esp_wifi_connect();
    }
    connect_failed = error != ESP_OK;
    xSemaphoreGive(station_lock);
    return error;
}

bool board_wifi_connect_failed(void)
{
    return connect_failed;
}
