#pragma once
#include "esp_err.h"
#include <stdint.h>
typedef struct { void *io; } *esp_lcd_touch_handle_t;
typedef struct { uint16_t x,y; uint8_t track_id; } esp_lcd_touch_point_data_t;
esp_err_t esp_lcd_touch_read_data(esp_lcd_touch_handle_t tp);
esp_err_t esp_lcd_touch_get_data(esp_lcd_touch_handle_t tp, esp_lcd_touch_point_data_t *points,
                                 uint8_t *count,uint8_t max_count);
