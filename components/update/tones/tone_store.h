#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>
#include "cJSON.h"

#ifndef ZLD_CARD_DIR
#define ZLD_CARD_DIR "/sdcard"
#endif
#define TONE_ROOT ZLD_CARD_DIR "/zld/tones"
#define TONE_ARCHIVE_PATH TONE_ROOT "/download.zip"
#define TONE_ZIP_LIMIT (8U * 1024 * 1024)
#define TONE_ORIGIN "https://github.com/ZoleeBB/ZoleeEnergyFlowDisplay/releases"
bool tone_prepare(void);
bool tone_install(const char *archive, const char *tag, const char *digest, char error[160]);
bool tone_current_verified(const char *digest);
bool tone_set_version(const char *tag,const char *digest);
bool tone_install_local(const char *archive,char error[160]);
bool tone_resolve(const char *name, char *path, size_t cap);
cJSON *tone_list(char version[32]);
enum { TONE_CHECK, TONE_NETWORK, TONE_CHECKSUM, TONE_ARCHIVE, TONE_WRITE, TONE_DONE };
extern atomic_uint tone_done_bytes, tone_total_bytes;
bool tone_source_download(unsigned *reason);
bool tone_release_info(const char *json, char tag[32], size_t *size);
bool tone_checksum(const char *list,const char *name,char sha[65]);
