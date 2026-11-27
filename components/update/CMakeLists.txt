idf_component_register(SRCS "zld_update.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server
    PRIV_REQUIRES app_update esp_app_format esp_timer mbedtls board fatfs
    EMBED_TXTFILES "update.html")

set(card "${CMAKE_CURRENT_LIST_DIR}/../../sdcard")
file(GLOB_RECURSE card_files CONFIGURE_DEPENDS LIST_DIRECTORIES false "${card}/*")
set(packager "${CMAKE_CURRENT_LIST_DIR}/../../tools/package_update.py")
set(index "${CMAKE_CURRENT_BINARY_DIR}/zld_asset_index.inc")
idf_build_get_property(idf_python PYTHON)
add_custom_command(OUTPUT "${index}"
    COMMAND "${idf_python}" "${packager}" --assets "${card}" --index "${index}"
    DEPENDS "${packager}" ${card_files}
    VERBATIM)
add_custom_target(zld_sd_index DEPENDS "${index}")
add_dependencies(${COMPONENT_LIB} zld_sd_index)
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
