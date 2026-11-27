#include "tones.h"
#include "tone_store.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <stdlib.h>

enum { IDLE, BUSY, DONE, FAILED };
static atomic_int state;
static atomic_uint failure;
static zld_update_authorize_t allow;
bool zld_tones_busy(void) {return atomic_load(&state)==BUSY;}
bool zld_ringtone_path(const char *name,char *path,size_t cap) {return tone_resolve(name,path,cap);}
static esp_err_t reply(httpd_req_t *r,const char *status,const char *msg) {
    httpd_resp_set_status(r,status);httpd_resp_set_type(r,"text/plain; charset=utf-8");return httpd_resp_sendstr(r,msg);
}
static void worker(void *unused) {
    (void)unused;unsigned reason=TONE_CHECK;
    bool ok=tone_source_download(&reason);
    atomic_store(&failure,reason);atomic_store(&state,ok?DONE:FAILED);
    zld_update_leave();vTaskDelete(NULL);
}
static esp_err_t start(httpd_req_t *r) {
    if(!allow(r,true)) return ESP_OK;
    int old=atomic_load(&state);
    if(old==BUSY || !atomic_compare_exchange_strong(&state,&old,BUSY))
        return reply(r,"409 Conflict","A csengőhangcsomag letöltése folyamatban van.");
    if(!zld_update_enter()) {atomic_store(&state,old);return reply(r,"409 Conflict","A kijelző frissítése folyamatban van.");}
    atomic_store(&tone_done_bytes,0);atomic_store(&tone_total_bytes,0);
    if(xTaskCreate(worker,"ringtone_sync",12288,NULL,3,NULL)!=pdPASS) {
        zld_update_leave();atomic_store(&failure,TONE_WRITE);atomic_store(&state,FAILED);
        return reply(r,"503 Service Unavailable","A letöltéshez jelenleg nincs elegendő szabad memória.");
    }
    return reply(r,"202 Accepted","OK");
}
static esp_err_t status(httpd_req_t *r) {
    if(!allow(r,false)) return ESP_OK;
    char tag[32];cJSON *items=tone_list(tag),*doc=cJSON_CreateObject();
    if(!items || !doc || !cJSON_AddItemToObject(doc,"items",items)) {
        cJSON_Delete(items);cJSON_Delete(doc);return reply(r,"503 Service Unavailable","Nincs elég memória.");
    }
    int mode=atomic_load(&state);
    const char *msg="";
    if(mode==FAILED) {
        switch(atomic_load(&failure)) {
        case TONE_NETWORK:msg="A GitHub kiadása vagy letöltése nem érhető el. Ellenőrizd a hálózatot, majd próbáld újra.";break;
        case TONE_CHECKSUM:msg="A kiadás SHA-256 jegyzéke hiányzik vagy hibás.";break;
        case TONE_ARCHIVE:msg="A csomag ellenőrzése vagy telepítése sikertelen. A korábbi csomag használható.";break;
        default:msg="A csomag nem írható. Ellenőrizd a kártyát és a szabad helyet.";break;
        }
    }
    cJSON_AddStringToObject(doc,"status",mode==BUSY?"downloading":mode==FAILED?"error":(mode==DONE||*tag)?"ready":"idle");
    cJSON_AddStringToObject(doc,"message",msg);cJSON_AddStringToObject(doc,"version",tag);
    cJSON_AddNumberToObject(doc,"completed",atomic_load(&tone_done_bytes));
    cJSON_AddNumberToObject(doc,"total",atomic_load(&tone_total_bytes));
    char *s=cJSON_PrintUnformatted(doc);cJSON_Delete(doc);
    if(!s) return reply(r,"503 Service Unavailable","Nincs elég memória.");
    httpd_resp_set_type(r,"application/json; charset=utf-8");httpd_resp_set_hdr(r,"Cache-Control","no-store");
    esp_err_t rc=httpd_resp_sendstr(r,s);cJSON_free(s);return rc;
}
esp_err_t zld_tones_register(httpd_handle_t server,zld_update_authorize_t fn) {
    allow=fn;
    const httpd_uri_t get={.uri="/api/ringtones",.method=HTTP_GET,.handler=status};
    const httpd_uri_t post={.uri="/api/ringtones/download",.method=HTTP_POST,.handler=start};
    esp_err_t e=httpd_register_uri_handler(server,&get);
    if(e!=ESP_OK) return e;
    e=httpd_register_uri_handler(server,&post);
    if(e!=ESP_OK) httpd_unregister_uri_handler(server,get.uri,get.method);
    return e;
}
