#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/sha.h>
#define ESP_OK 0
#define ESP_FAIL -1
typedef int esp_err_t;
typedef void *httpd_handle_t;
typedef struct {int unused;} httpd_req_t;
typedef SHA256_CTX mbedtls_sha256_context;
#define mbedtls_sha256_init(h) ((void)(h))
#define mbedtls_sha256_starts(h,b) (SHA256_Init(h) != 1)
#define mbedtls_sha256_update(h,b,n) (SHA256_Update(h,b,n) != 1)
#define mbedtls_sha256_finish(h,b) (SHA256_Final(b,h) != 1)
#define mbedtls_sha256_free(h) ((void)(h))
#define mbedtls_sha256(data,n,digest,mode) (SHA256(data,n,digest)==NULL)
#define HTTP_EVENT_ON_HEADER 1
typedef struct {int event_id; char *header_key,*header_value; void *user_data;} esp_http_client_event_t;
typedef struct {
    const char *url,*user_agent;
    void *crt_bundle_attach,*user_data;
    bool disable_auto_redirect;
    unsigned timeout_ms,buffer_size,buffer_size_tx;
    esp_err_t (*event_handler)(esp_http_client_event_t*);
} esp_http_client_config_t;
typedef struct test_transport *esp_http_client_handle_t;
#define esp_crt_bundle_attach NULL
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *cfg);
int esp_http_client_open(esp_http_client_handle_t c,int n);
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t c);
int64_t esp_http_client_get_content_length(esp_http_client_handle_t c);
int esp_http_client_get_status_code(esp_http_client_handle_t c);
int esp_http_client_read(esp_http_client_handle_t c,char *p,int n);
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t c);
void esp_http_client_cleanup(esp_http_client_handle_t c);
#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_8BIT 0
#define heap_caps_aligned_alloc(a,n,c) malloc(n)
