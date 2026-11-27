idf_component_register(SRCS board.c network.c board_psram.c board_sound.c board_click.c board_touch.c
    INCLUDE_DIRS include
    PRIV_REQUIRES waveshare__esp32_p4_wifi6_touch_lcd_4b espressif__esp_lvgl_adapter
                  espressif__esp_codec_dev espressif__esp_lcd_touch esp_lcd esp_eth esp_event esp_netif esp_timer esp_wifi nvs_flash fatfs sdmmc)
target_link_options(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=esp_lv_adapter_register_display"
    "-Wl,-u,__wrap_esp_lv_adapter_register_display")
