idf_component_register(
    SRCS "app_loxone.c" "app_loxone_model.c" "app_loxone_home_runtime.c"
         "app_loxone_audio_auth.c" "app_loxone_audio_runtime.c" "app_loxone_audio_media.c"
         "app_loxone_audio_image.c" "panel_profile.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES json
    PRIV_REQUIRES esp_event esp_http_client esp_timer esp_hw_support esp_system mbedtls)
