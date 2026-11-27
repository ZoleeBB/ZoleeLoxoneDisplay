#include "board_touch.h"
#include "board_click.h"
#include "board_sound.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "esp_lv_adapter_input.h"
#include <string.h>

/* Both panel SKUs poll the GT911 (INT is not connected). Bit 7 at 0x814E
 * distinguishes "no new report" from a fresh report with zero contacts.
 * The driver consumes the report, so check readiness before its normal read. */
#define GT911_REPORT_STATUS 0x814e
#define GT911_CONTACTS 5
static esp_lcd_touch_point_data_t last_points[GT911_CONTACTS];
static uint8_t last_count;
static uint16_t contact_mask;

static esp_err_t read_touch(esp_lcd_touch_handle_t tp, esp_lcd_touch_point_data_t *points,
                            uint8_t *count, uint8_t max_count, void *context)
{
    (void)context;
    uint8_t status;
    *count=0;
    esp_err_t err=esp_lcd_panel_io_rx_param(tp->io,GT911_REPORT_STATUS,&status,sizeof(status));
    if (err!=ESP_OK) return err;
    if (status & 0x80) {
        uint8_t n=0;
        err=esp_lcd_touch_read_data(tp);
        if (err!=ESP_OK) return err;
        err=esp_lcd_touch_get_data(tp,last_points,&n,GT911_CONTACTS);
        if (err!=ESP_OK) return err;
        last_count=n;
        uint16_t mask=0;
        for (unsigned i=0;i<n;++i)
            mask |= (uint16_t)(1u << (last_points[i].track_id & 0x0f));
        unsigned presses=board_click_contacts(&contact_mask,mask);
        for (unsigned i=0;i<presses;++i) board_sound_touch();
    }
    *count = last_count < max_count ? last_count : max_count;
    memcpy(points,last_points,(size_t)*count*sizeof(*points));
    return ESP_OK;
}

esp_err_t board_touch_feedback(lv_indev_t *pointer)
{
    const esp_lv_adapter_touch_callbacks_t callbacks={.custom_touch_read=read_touch};
    return esp_lv_adapter_set_touch_callbacks(pointer,&callbacks);
}
