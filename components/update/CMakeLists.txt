idf_component_register(SRCS "zld_update.c" "tones/tones.c" "tones/tone_source.c" "tones/tone_store.c"
         "tones/app_ringtone_zip.c" "tones/app_ringtone_wav.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "tones"
    REQUIRES esp_http_server
    PRIV_REQUIRES json espressif__zlib esp_http_client app_update esp_app_format esp_timer mbedtls board fatfs
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

# The one-click seed must point at the exact tree used to build this firmware.
execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY "${card}/.."
    OUTPUT_VARIABLE revision OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
execute_process(COMMAND git rev-parse --absolute-git-dir WORKING_DIRECTORY "${card}/.."
    OUTPUT_VARIABLE gitdir OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
execute_process(COMMAND git symbolic-ref -q HEAD WORKING_DIRECTORY "${card}/.."
    OUTPUT_VARIABLE headref OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(gitdir)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${gitdir}/HEAD")
    if(headref AND EXISTS "${gitdir}/${headref}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${gitdir}/${headref}")
    endif()
endif()
if(NOT revision AND DEFINED ENV{GITHUB_SHA})
    set(revision "$ENV{GITHUB_SHA}")
endif()
string(LENGTH "${revision}" revision_length)
if(NOT revision_length EQUAL 40 OR NOT revision MATCHES "^[a-f0-9]+$")
    set(revision "")
endif()
target_compile_definitions(${COMPONENT_LIB} PRIVATE ZLD_SOURCE_REV="${revision}")
