#include "board.h"
#include "panel_config.h"
#include "panel_energy_stats.h"
#include "panel_intercom_ui.h"
#include "panel_ui.h"
#include "panel_ui_style.h"
#include "panel_web.h"
#include "zld_loxone.h"
#include "zld_update.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/platform_util.h"
#include "nvs_flash.h"
#include <stdlib.h>
#include <time.h>

static const char *TAG = "zld";
static lv_obj_t *boot_label;

static void boot_message(const char *message)
{
    ESP_LOGI(TAG, "%s", message);
    if (!board_display_lock(1000)) return;
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    if (!boot_label) {
        boot_label = lv_label_create(screen);
        lv_obj_set_width(boot_label, 640);
        lv_obj_set_style_text_font(boot_label, &app_font_24, 0);
        lv_obj_set_style_text_color(boot_label, lv_color_white(), 0);
        lv_obj_set_style_text_align(boot_label, LV_TEXT_ALIGN_CENTER, 0);
    }
    lv_label_set_text(boot_label, message);
    lv_obj_center(boot_label);
    board_display_unlock();
}

static bool network_ready(void)
{
    char address[16];
    return board_network_address(address, sizeof(address));
}

static bool pending_ota(void)
{
    esp_ota_img_states_t state;
    return esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK &&
           state == ESP_OTA_IMG_PENDING_VERIFY;
}

static void boot_failed(const char *message)
{
    boot_message(message);
    if (pending_ota()) {
        vTaskDelay(pdMS_TO_TICKS(2500));
        ESP_LOGE(TAG, "Az új kép öntesztje sikertelen");
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_ota_mark_app_invalid_rollback_and_reboot());
    }
}

static bool apply_profile(uint32_t generation, bool *intercom)
{
    cJSON *profile = panel_config_snapshot();
    if (!profile) return false;
    char password[PANEL_SECRET_CAP] = {0}, camera_password[PANEL_SECRET_CAP] = {0};
    esp_err_t err = panel_config_read_secret("loxonePassword", password, sizeof(password));
    if (err == ESP_OK)
        err = panel_config_read_secret("cameraPassword", camera_password, sizeof(camera_password));
    if (err == ESP_OK && panel_config_generation() == generation) {
        err = zld_loxone_configure(profile, password, camera_password);
        if (err == ESP_OK) {
            const cJSON *door = cJSON_GetObjectItemCaseSensitive(profile, "intercom");
            *intercom = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(door, "enabled"));
        }
    } else if (err == ESP_OK) {
        err = ESP_ERR_INVALID_STATE;
    }
    mbedtls_platform_zeroize(password, sizeof(password));
    mbedtls_platform_zeroize(camera_password, sizeof(camera_password));
    cJSON_Delete(profile);
    return err == ESP_OK;
}

void app_main(void)
{
    if (board_display_start() != ESP_OK) {
        ESP_LOGE(TAG, "Kijelző inicializálási hiba");
        if (pending_ota()) ESP_ERROR_CHECK_WITHOUT_ABORT(esp_ota_mark_app_invalid_rollback_and_reboot());
        return;
    }
    boot_message("ZoleeLoxoneDisplay");
    if (nvs_flash_init() != ESP_OK) {
        boot_failed("A belső beállítások nem olvashatók. Ellenőrizd a soros naplót.");
        return;
    }
    if (board_mount_sd() != ESP_OK) {
        boot_message("A működéshez microSD-kártya szükséges. Helyezz be FAT32-re formázott kártyát.");
        bool trial = pending_ota();
        unsigned attempts = 0;
        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(3000));
            if (board_mount_sd() == ESP_OK) break;
            if (trial && ++attempts >= 5) {
                boot_failed("Az új firmware nem éri el az SD-kártyát. Visszaállítás.");
                return;
            }
        }
    }
    if (panel_config_init() != ESP_OK) {
        boot_failed("Az SD-profil nem olvasható. Ellenőrizd a panel.json és panel.json.bak fájlokat, majd indítsd újra a kijelzőt.");
        return;
    }
    if (board_network_init() != ESP_OK) {
        boot_failed("A hálózati illesztő indítása sikertelen. Ellenőrizd a panel típusát és a soros naplót.");
        return;
    }
    setenv("TZ", "CET-1CEST,M3.5.0/2,M10.5.0/3", 1);
    tzset();
    esp_sntp_config_t ntp = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_sntp_init(&ntp));

    if (zld_loxone_start(network_ready) != ESP_OK) {
        boot_failed("Nincs elegendő memória a Loxone-kapcsolathoz.");
        return;
    }
    httpd_handle_t web = panel_web_start();
    if (!web || zld_update_register(web, panel_web_authorize) != ESP_OK) {
        boot_failed("A webes beállítás vagy a frissítési felület indítása sikertelen.");
        return;
    }
    bool assets_ok = zld_assets_check();
    if (pending_ota() && !assets_ok) {
        boot_failed("Az új firmware SD-csomagja hiányos. Visszatérés az előző verzióhoz.");
        return;
    }
    if (board_display_lock(1000)) {
        if (boot_label) lv_obj_delete(boot_label);
        boot_label = NULL;
        board_display_unlock();
    }
    if (panel_ui_start() != ESP_OK) {
        boot_failed("A kezelőfelület indítása sikertelen.");
        return;
    }
    if (pending_ota() && esp_ota_mark_app_valid_cancel_rollback() != ESP_OK) {
        boot_failed("A sikeres próbaindítás nem menthető. Visszaállítás az előző verzióra.");
        return;
    }

    uint32_t applied = 0;
    bool intercom = false;
    int64_t next_sample = 0;
    for (;;) {
        uint32_t current = panel_config_generation();
        if (current != applied && apply_profile(current, &intercom)) applied = current;
        bool ready = zld_assets_ready();
        panel_intercom_media_tick(intercom && ready);
        int64_t now = esp_timer_get_time();
        if (now >= next_sample) {
            next_sample = now + 1000000;
            panel_energy_stats_tick();
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
