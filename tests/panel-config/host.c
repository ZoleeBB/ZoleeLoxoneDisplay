/* Only NVS/random and the RTOS are host shims. The test uses the production
   AES-GCM implementation, JSON parser, SD transaction and POSIX filesystem. */
#include "nvs.h"
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
esp_err_t nvs_open(const char *space,int mode,nvs_handle_t *handle) {
    (void)space;(void)mode;*handle=1;return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t handle,const char *key,void *out,size_t *n) {
    (void)handle;(void)key;
    FILE *f=fopen("device.key","rb");
    if(!f)return ESP_ERR_NVS_NOT_FOUND;
    size_t received=fread(out,1,*n,f);
    fclose(f);*n=received;return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t handle,const char *key,const void *data,size_t n) {
    (void)handle;(void)key;
    FILE *f=fopen("device.key","wb");
    if(!f)return ESP_FAIL;
    size_t put=fwrite(data,1,n,f);fclose(f);
    return put==n?ESP_OK:ESP_FAIL;
}
esp_err_t nvs_commit(nvs_handle_t handle){(void)handle;return ESP_OK;}
void nvs_close(nvs_handle_t handle){(void)handle;}
void esp_fill_random(void *out,size_t n){
    FILE *f=fopen("/dev/urandom","rb");
    assert(f && fread(out,1,n,f)==n);
    fclose(f);
}
