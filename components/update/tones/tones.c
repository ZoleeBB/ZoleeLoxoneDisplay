#include "tones.h"
#include "tone_store.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

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
static esp_err_t upload(httpd_req_t *r) {
    if(!allow(r,true)) return ESP_OK;
    if(r->content_len<22 || r->content_len>TONE_ZIP_LIMIT)
        return reply(r,"413 Content Too Large","A csengőhang ZIP mérete 22 bájt és 8 MiB közé eshet.");
    if(!zld_update_enter())
        return reply(r,"409 Conflict","A kijelzőn már folyamatban van egy frissítés.");
    int old=atomic_load(&state);
    if(old==BUSY || !atomic_compare_exchange_strong(&state,&old,BUSY)) {
        zld_update_leave();return reply(r,"409 Conflict","A csengőhangcsomag frissítése folyamatban van.");
    }
    atomic_store(&tone_total_bytes,(unsigned)r->content_len);atomic_store(&tone_done_bytes,0);
    FILE *f=tone_prepare()?fopen(TONE_ARCHIVE_PATH,"wb"):NULL;
    bool ok=f!=NULL; size_t left=r->content_len;
    char *buf=malloc(2048);
    if(!buf) ok=false;
    while(ok && left) {
        int n=httpd_req_recv(r,buf,left>2048?2048:left);
        if(n<=0 || !zld_card_online() || fwrite(buf,1,n,f)!=(size_t)n) {ok=false;break;}
        left-=n;atomic_store(&tone_done_bytes,(unsigned)(r->content_len-left));
    }
    free(buf);
    if(f && fclose(f)) ok=false;
    unsigned reason=TONE_WRITE;
    char error[160]="";
    if(ok) {reason=TONE_ARCHIVE;ok=tone_install_local(TONE_ARCHIVE_PATH,error);}
    unlink(TONE_ARCHIVE_PATH);
    atomic_store(&failure,reason);atomic_store(&state,ok?DONE:FAILED);
    zld_update_leave();
    if(ok) return reply(r,"200 OK","A helyi csengőhangcsomag telepítve.");
    return reply(r,reason==TONE_ARCHIVE?"400 Bad Request":"503 Service Unavailable",
        *error?error:reason==TONE_ARCHIVE?"A ZIP, a névjegyzék vagy a hangfájl sérült vagy nem támogatott.":
                                    "A feltöltés megszakadt vagy az SD-kártya nem írható.");
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
    if(e==ESP_OK) {
        const httpd_uri_t local={.uri="/api/ringtones/upload",.method=HTTP_POST,.handler=upload};
        e=httpd_register_uri_handler(server,&local);
        if(e!=ESP_OK) httpd_unregister_uri_handler(server,post.uri,post.method);
    }
    if(e!=ESP_OK) httpd_unregister_uri_handler(server,get.uri,get.method);
    return e;
}
