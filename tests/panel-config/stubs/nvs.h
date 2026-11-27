#pragma once
#include <stddef.h>
#include "esp_err.h"
#define NVS_READONLY 1
#define NVS_READWRITE 2
typedef int nvs_handle_t;
esp_err_t nvs_open(const char *space,int mode,nvs_handle_t *handle);
esp_err_t nvs_get_blob(nvs_handle_t handle,const char *key,void *out,size_t *n);
esp_err_t nvs_set_blob(nvs_handle_t handle,const char *key,const void *data,size_t n);
esp_err_t nvs_commit(nvs_handle_t handle);
void nvs_close(nvs_handle_t handle);
