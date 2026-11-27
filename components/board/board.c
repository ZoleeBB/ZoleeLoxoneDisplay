#include "board.h"
#include "board_touch.h"
#include "board_sound.h"
#include "bsp/esp-bsp.h"
#include "driver/sdmmc_host.h"
#include "esp_vfs_fat.h"
#include "esp_log.h"

esp_err_t board_display_start(void)
{
    bsp_display_cfg_t display = {
        .lv_adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG(),
        .rotation = ESP_LV_ADAPTER_ROTATE_0,
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL,
    };
    display.lv_adapter_cfg.task_stack_size = 32768;
    display.lv_adapter_cfg.stack_in_psram = true;
    if (!bsp_display_start_with_config(&display))
        return ESP_FAIL;
    esp_err_t feedback=board_sound_start();
    if (feedback==ESP_OK) {
        if (bsp_display_lock(-1)) {
            feedback=board_touch_feedback(bsp_display_get_input_dev());
            bsp_display_unlock();
        } else feedback=ESP_FAIL;
    }
    if (feedback!=ESP_OK) ESP_LOGW("touch", "Az érintési hang nem indult el (%d).", feedback);
    return bsp_display_backlight_on();
}

bool board_display_lock(uint32_t timeout_ms)
{
    return bsp_display_lock((int32_t)timeout_ms);
}

void board_display_unlock(void)
{
    bsp_display_unlock();
}

esp_err_t board_set_brightness(unsigned percent)
{
    if (percent > 100)
        return ESP_ERR_INVALID_ARG;
    /* Az elődprojekt 4B paneljén a PWM 48%-tól adott látható fényt. */
    int pwm = percent ? 48 + (percent - 1) * 52 / 99 : 0;
    return bsp_display_brightness_set(pwm);
}

esp_err_t board_mount_sd(void)
{
    static sdmmc_card_t *card;
    if (card)
        return ESP_OK;
    /* A kijelző BSP már 3,3 V-ra állította a VO4-et. A BSP külön SD
     * rutinja újra lefoglalná ezt az LDO-t minden csatolási próbán. */
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    const esp_vfs_fat_sdmmc_mount_config_t mount = {
        .format_if_mount_failed = false,
        .max_files = 12,
        .allocation_unit_size = 16 * 1024,
    };
    return esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot, &mount, &card);
}
