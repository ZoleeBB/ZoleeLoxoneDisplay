idf_component_register(SRC_DIRS "." "fonts"
    INCLUDE_DIRS "." "fonts"
    EMBED_TXTFILES "web/index.html" "web/admin.css" "web/admin.js"
    REQUIRES board loxone update door_media json esp_http_server esp_timer
             nvs_flash esp_netif app_update lvgl__lvgl fatfs mbedtls esp_hw_support)
target_link_options(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=i2s_new_channel" "-Wl,-u,__wrap_i2s_new_channel")
