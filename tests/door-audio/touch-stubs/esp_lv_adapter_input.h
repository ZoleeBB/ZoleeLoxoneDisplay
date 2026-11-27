#pragma once
#include "esp_lcd_touch.h"
#include "lvgl.h"
typedef struct {
    esp_err_t (*custom_touch_read)(esp_lcd_touch_handle_t,esp_lcd_touch_point_data_t*,uint8_t*,uint8_t,void*);
} esp_lv_adapter_touch_callbacks_t;
esp_err_t esp_lv_adapter_set_touch_callbacks(lv_indev_t *dev,const esp_lv_adapter_touch_callbacks_t *cb);
