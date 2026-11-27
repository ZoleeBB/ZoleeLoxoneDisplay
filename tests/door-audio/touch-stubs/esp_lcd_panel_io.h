#pragma once
#include "esp_err.h"
#include <stddef.h>
esp_err_t esp_lcd_panel_io_rx_param(void *io, int reg, void *data, size_t len);
