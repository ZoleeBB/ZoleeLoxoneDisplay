#include "door_ringtone.h"
#include "zld_update.h"
#include "esp_heap_caps.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

uint8_t *door_ringtone_load(const char *name, size_t *size, app_ringtone_load_status_t *status)
{
    char path[256];
    *status = APP_RINGTONE_LOAD_INVALID;
    *size = 0;
    if (!app_ringtone_name(name)) return NULL;
    if (!zld_ringtone_path(name, path, sizeof(path))) {
        *status = APP_RINGTONE_LOAD_MISSING;
        return NULL;
    }
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        *status = APP_RINGTONE_LOAD_IO;
        return NULL;
    }
    struct stat info;
    uint8_t *data = NULL;
    if (fstat(fd, &info) == 0 && info.st_size >= 44 && info.st_size <= APP_RINGTONE_MAX) {
        const size_t length = (size_t)info.st_size;
        data = heap_caps_aligned_alloc(128, length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        *status = data ? APP_RINGTONE_LOAD_OK : APP_RINGTONE_LOAD_MEMORY;
        while (data && *size < length) {
            ssize_t n = read(fd, data + *size, length - *size);
            if (n <= 0) {
                *status = APP_RINGTONE_LOAD_IO;
                free(data);
                data = NULL;
                *size = 0;
                break;
            }
            *size += (size_t)n;
        }
    }
    close(fd);
    return data;
}
