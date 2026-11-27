idf_component_register(
    SRCS "app_intercom.c" "app_intercom_stream.c" "app_intercom_i2s.c"
         "app_intercom_audio.c" "app_intercom_audio_protocol.c" "app_ringtone_wav.c" "door_ringtone.c"
    INCLUDE_DIRS "include"
    REQUIRES loxone
    PRIV_REQUIRES update esp_driver_jpeg esp_driver_i2s esp_timer esp_http_client
                  waveshare__esp32_p4_wifi6_touch_lcd_4b espressif__esp-sr
                  espressif__esp_codec_dev lwip mbedtls esp_hw_support)
