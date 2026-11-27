/* POSIX test substitutes for HTTP transport, inactive flash and timers. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <openssl/sha.h>

#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG -2
typedef int esp_err_t;
#define BOARD_SKU 31416
#define BOARD_SILICON "rev1"
#define ZLD_SOURCE_REV "1111111111111111111111111111111111111111"
#define ZLD_CARD_DIR "card"
static void esp_fill_random(void *p,size_t n) { memset(p,0x7D,n); }

typedef SHA256_CTX mbedtls_sha256_context;
#define mbedtls_sha256_init(h) ((void)(h))
#define mbedtls_sha256_starts(h,b) (SHA256_Init(h) != 1)
#define mbedtls_sha256_update(h,b,n) (SHA256_Update(h,b,n) != 1)
#define mbedtls_sha256_finish(h,b) (SHA256_Final(b,h) != 1)
#define mbedtls_sha256_free(h) ((void)(h))

typedef void *httpd_handle_t;
typedef struct {
    size_t content_len, available, offset;
    const unsigned char *body;
    void *user_ctx;
    const char *generation, *target, *index, *sha;
    char response[2048], status[96];
} httpd_req_t;
typedef struct { const char *uri; int method; esp_err_t (*handler)(httpd_req_t *); void *user_ctx; } httpd_uri_t;
static bool zld_tones_busy(void) {return false;}
#define zld_tones_register(a,b) (0)
#define HTTP_GET 1
#define HTTP_POST 2
static int httpd_req_get_hdr_value_str(httpd_req_t *r, const char *key, char *value, size_t cap) {
    const char *v = !strcmp(key,"X-Pack-Manifest") ? r->generation : !strcmp(key,"X-Pack-Target") ?
        r->target : !strcmp(key,"X-Pack-Index") ? r->index : r->sha;
    if (!v || strlen(v) >= cap) return -1;
    strcpy(value,v); return 0;
}
static int httpd_req_recv(httpd_req_t *r,char *buf,size_t n) {
    size_t left = r->available - r->offset;
    if (n>left) n=left;
    if (n>127) n=127;
    memcpy(buf,r->body+r->offset,n); r->offset+=n; return (int)n;
}
static int httpd_resp_set_status(httpd_req_t *r,const char *status) { strcpy(r->status,status); return 0; }
static int httpd_resp_sendstr(httpd_req_t *r,const char *text) { strcpy(r->response,text); return 0; }
static int httpd_resp_send_chunk(httpd_req_t *r,const char *p,int n) {
    if (p) strncat(r->response,p,n);
    return 0;
}
#define httpd_register_uri_handler(s,r) (0)
#define httpd_unregister_uri_handler(s,r,m) ((void)0)
#define httpd_resp_set_type(r,t) ((void)0)
#define httpd_resp_set_hdr(r,k,v) ((void)0)
const char _binary_update_html_start[] = "";

typedef int esp_ota_handle_t;
typedef struct { size_t size; } esp_partition_t;
typedef struct { char project_name[32]; } esp_app_desc_t;
static const esp_app_desc_t test_app = {"test-app"};
static const esp_partition_t test_part = {16384};
static unsigned char test_flash[16384];
static size_t test_wrote;
static bool test_switched;
#define esp_ota_get_next_update_partition(p) (&test_part)
#define esp_app_get_description() (&test_app)
static int esp_ota_begin(const esp_partition_t *p,size_t n,esp_ota_handle_t *h) {
    assert(n <= p->size); test_wrote=0; *h=1; return 0;
}
static int esp_ota_write(esp_ota_handle_t h,const void *data,size_t n) {
    memcpy(test_flash+test_wrote,data,n); test_wrote+=n; return 0;
}
#define esp_ota_abort(h) ((void)0)
static void (*test_after_write)(void);
static int esp_ota_end(esp_ota_handle_t h) {
    if (test_after_write) test_after_write();
    return 0;
}
static int esp_ota_get_partition_description(const esp_partition_t *p,esp_app_desc_t *d) {
    *d=test_app; return 0;
}
static int esp_partition_read(const esp_partition_t *p,size_t off,void *buf,size_t n) {
    assert(off+n<=test_wrote); memcpy(buf,test_flash+off,n); return 0;
}
static int esp_ota_set_boot_partition(const esp_partition_t *p) { test_switched=true; return 0; }
typedef void *esp_timer_handle_t;
typedef struct { void (*callback)(void*); const char *name; } esp_timer_create_args_t;
#define esp_timer_create(c,t) (0)
#define esp_timer_delete(t) ((void)0)
#define esp_timer_start_once(t,us) (0)
#define esp_restart() ((void)0)

typedef void *esp_http_client_handle_t;
typedef struct { int event_id,data_len; void *user_data,*data; } esp_http_client_event_t;
typedef struct {
    const char *url;
    void *crt_bundle_attach;
    bool disable_auto_redirect;
    int timeout_ms,buffer_size;
    esp_err_t (*event_handler)(esp_http_client_event_t*);
    void *user_data;
} esp_http_client_config_t;
#define HTTP_EVENT_ON_DATA 1
#define esp_crt_bundle_attach NULL
static esp_http_client_config_t test_client;
static unsigned test_download_count;
static bool test_bad_remote;
static void *esp_http_client_init(const esp_http_client_config_t *cfg) { test_client=*cfg; return &test_client; }
static int esp_http_client_set_url(void *c,const char *url) {
    assert(strstr(url,"raw.githubusercontent.com/ZoleeBB/ZoleeLoxoneDisplay/" ZLD_SOURCE_REV "/sdcard/icons/a.png"));
    return 0;
}
static int esp_http_client_perform(void *c) {
    ++test_download_count;
    esp_http_client_event_t event={.event_id=HTTP_EVENT_ON_DATA,.data_len=1,
        .data=test_bad_remote?"X":"A",.user_data=test_client.user_data};
    return test_client.event_handler(&event);
}
#define esp_http_client_get_status_code(c) (200)
#define esp_http_client_cleanup(c) ((void)0)
#define pdMS_TO_TICKS(ms) (ms)
#define vTaskDelay(t) ((void)0)
#define pdPASS 1
#define xTaskCreate(f,n,s,a,p,h) ((f)(a),pdPASS)
#define vTaskDelete(t) ((void)0)
