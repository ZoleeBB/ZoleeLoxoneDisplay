#include "panel_web.h"
#include "panel_config.h"
#include "zld_loxone.h"
#include "zld_update.h"
#include "mbedtls/base64.h"
#include "mbedtls/platform_util.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const char index_start[] asm("_binary_index_html_start");
extern const char css_start[] asm("_binary_admin_css_start");
extern const char js_start[] asm("_binary_admin_js_start");

static esp_err_t reply(httpd_req_t *r,const char *status,const char *msg) {
    httpd_resp_set_status(r,status);
    httpd_resp_set_type(r,"text/plain; charset=utf-8");
    return httpd_resp_sendstr(r,msg);
}
static bool login(httpd_req_t *r) {
    char user[64]="",pass[PANEL_SECRET_CAP]="",header[280]="";
    unsigned char decoded[200]={0};
    bool ok=false;
    if(!panel_config_web_login(user,sizeof(user),pass)) goto done;
    if(!user[0]) { ok=true; goto done; }
    if(httpd_req_get_hdr_value_str(r,"Authorization",header,sizeof(header))!=ESP_OK ||
       strncmp(header,"Basic ",6)) goto done;
    size_t n=0;
    if(mbedtls_base64_decode(decoded,sizeof(decoded),&n,(unsigned char *)header+6,strlen(header+6))!=0) goto done;
    size_t u=strlen(user), p=strlen(pass);
    if(n!=u+1+p || memcmp(decoded,user,u) || decoded[u]!=':') goto done;
    unsigned diff=0;
    for(size_t i=0;i<p;i++) diff|=(unsigned char)pass[i]^decoded[u+1+i];
    ok=diff==0;
done:
    mbedtls_platform_zeroize(pass,sizeof(pass));
    mbedtls_platform_zeroize(decoded,sizeof(decoded));
    mbedtls_platform_zeroize(header,sizeof(header));
    return ok;
}
bool panel_web_authorize(httpd_req_t *r,bool write) {
    /* Protected profile/catalog reads and mutations require the mounted
       session's actual media. Thumbnails use the cheap periodic health state. */
    bool check=write || !strncmp(r->uri,"/api/",5);
    if(!(check?zld_card_verify():zld_card_online())) {
        reply(r,"503 Service Unavailable","Az induláskor használt microSD-kártya nem érhető el.");
        return false;
    }
    if(!login(r)) {
        httpd_resp_set_hdr(r,"WWW-Authenticate","Basic realm=\"ZoleeLoxoneDisplay\", charset=\"UTF-8\"");
        reply(r,"401 Unauthorized","Bejelentkezés szükséges.");
        return false;
    }
    if(write) {
        char proof[4];
        if(httpd_req_get_hdr_value_str(r,"X-Panel-Request",proof,sizeof(proof))!=ESP_OK || strcmp(proof,"1")) {
            reply(r,"403 Forbidden","Hiányzik a helyi kezelőfelület fejléce.");
            return false;
        }
    }
    httpd_resp_set_hdr(r,"X-Content-Type-Options","nosniff");
    return true;
}
static esp_err_t page(httpd_req_t *r) {
    if(!panel_web_authorize(r,false)) return ESP_OK;
    const char *type="text/html; charset=utf-8", *body=index_start;
    if(!strcmp(r->uri,"/admin.css")) { type="text/css; charset=utf-8"; body=css_start; }
    if(!strcmp(r->uri,"/admin.js")) { type="application/javascript; charset=utf-8"; body=js_start; }
    httpd_resp_set_type(r,type);
    httpd_resp_set_hdr(r,"Cache-Control","no-cache");
    httpd_resp_set_hdr(r,"Content-Security-Policy","default-src 'self'; style-src 'self' 'unsafe-inline'; object-src 'none'; frame-ancestors 'none'; base-uri 'none'");
    return httpd_resp_sendstr(r,body);
}
static esp_err_t json_reply(httpd_req_t *r,cJSON *json) {
    char *body=json?cJSON_PrintUnformatted(json):NULL;
    cJSON_Delete(json);
    if(!body) return reply(r,"503 Service Unavailable","Nincs elég szabad memória.");
    httpd_resp_set_type(r,"application/json; charset=utf-8");
    httpd_resp_set_hdr(r,"Cache-Control","no-store");
    esp_err_t result=httpd_resp_sendstr(r,body);
    cJSON_free(body);
    return result;
}
static esp_err_t config_get(httpd_req_t *r) {
    if(!panel_web_authorize(r,false)) return ESP_OK;
    return json_reply(r,panel_config_snapshot());
}
static esp_err_t config_post(httpd_req_t *r) {
    if(!panel_web_authorize(r,true)) return ESP_OK;
    size_t n=r->content_len;
    if(!n || n>PANEL_CONFIG_MAX_BYTES) return reply(r,"413 Content Too Large","Túl nagy vagy üres kérés.");
    char *body=malloc(n+1);
    if(!body) return reply(r,"503 Service Unavailable","Nincs elég szabad memória.");
    size_t used=0;
    while(used<n) {
        int count=httpd_req_recv(r,body+used,n-used);
        if(count<=0) { mbedtls_platform_zeroize(body,n+1); free(body); return ESP_FAIL; }
        used+=(size_t)count;
    }
    body[n]=0;
    cJSON *doc=panel_parse_document(body,n);
    mbedtls_platform_zeroize(body,n+1);
    free(body);
    char error[192]="Érvénytelen JSON-dokumentum.";
    if(!zld_card_verify()) {
        cJSON_Delete(doc);
        return reply(r,"503 Service Unavailable","Mentés közben a microSD-kártya elérhetetlenné vált.");
    }
    esp_err_t rc=doc?panel_config_save(doc,error,sizeof(error)):ESP_ERR_INVALID_ARG;
    cJSON_Delete(doc);
    if(rc==ESP_OK) return json_reply(r,panel_config_snapshot());
    const char *status=rc==ESP_ERR_INVALID_ARG?"400 Bad Request":
                       rc==ESP_ERR_INVALID_STATE?"409 Conflict":"503 Service Unavailable";
    return reply(r,status,error);
}
static esp_err_t controls_get(httpd_req_t *r) {
    if(!panel_web_authorize(r,false)) return ESP_OK;
    cJSON *result=cJSON_CreateObject();
    cJSON *rows=zld_loxone_catalog();
    if(!rows) rows=cJSON_CreateArray();
    if(!result || !rows || !cJSON_AddItemToObject(result,"controls",rows)) {
        cJSON_Delete(result); cJSON_Delete(rows); return reply(r,"503 Service Unavailable","Nincs elég memória.");
    }
    char status[192]="";
    bool connected=zld_loxone_status(status,sizeof(status));
    cJSON_AddBoolToObject(result,"connected",connected);
    cJSON_AddStringToObject(result,"status",status);
    return json_reply(r,result);
}
static esp_err_t controls_refresh(httpd_req_t *r) {
    bool write=r->method==HTTP_POST;
    if(!panel_web_authorize(r,write)) return ESP_OK;
    if(write && zld_loxone_refresh()!=ESP_OK && !zld_loxone_refreshing())
        return reply(r,"409 Conflict","Az adatlista nem frissíthető. Ellenőrizd a Miniserver-kapcsolatot.");
    httpd_resp_set_type(r,"application/json");
    httpd_resp_set_hdr(r,"Cache-Control","no-store");
    return httpd_resp_sendstr(r,zld_loxone_refreshing()?"{\"refreshing\":true}":"{\"refreshing\":false}");
}
static bool asset_name(const char *s,size_t n) {
    if(!n || n>32) return false;
    for(size_t i=0;i<n;i++)
        if(!(s[i]>='a'&&s[i]<='z') && !(s[i]>='0'&&s[i]<='9') && s[i]!='-') return false;
    return true;
}
static esp_err_t asset_get(httpd_req_t *r) {
    if(!panel_web_authorize(r,false)) return ESP_OK;
    const char *logical=r->uri+strlen("/assets/");
    const char *type="application/json";
    if(strcmp(logical,"catalog.json")) {
        bool icon=!strncmp(logical,"icons/",6), thumb=!strncmp(logical,"thumbs/",7);
        const char *name=icon?logical+6:thumb?logical+7:"", *dot=strrchr(name,'.');
        if(!dot || !asset_name(name,(size_t)(dot-name)) || strcmp(dot,icon?".png":".jpg"))
            return reply(r,"404 Not Found","Ismeretlen erőforrás.");
        type=icon?"image/png":"image/jpeg";
    }
    char path[256];
    if(!zld_asset_path(logical,path,sizeof(path))) return reply(r,"404 Not Found","A képcsomag még nincs telepítve.");
    FILE *f=fopen(path,"rb");
    if(!f) return reply(r,"404 Not Found","A fájl nem olvasható.");
    char chunk[1536];
    httpd_resp_set_type(r,type);
    httpd_resp_set_hdr(r,"Cache-Control","no-cache");
    esp_err_t rc=ESP_OK;
    size_t n;
    while((n=fread(chunk,1,sizeof(chunk),f))>0 && rc==ESP_OK)
        rc=httpd_resp_send_chunk(r,chunk,(ssize_t)n);
    if(ferror(f)) rc=ESP_FAIL;
    fclose(f);
    return rc==ESP_OK?httpd_resp_send_chunk(r,NULL,0):rc;
}
httpd_handle_t panel_web_start(void) {
    httpd_config_t config=HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers=24;
    config.max_req_hdr_len=2048;
    config.stack_size=12288;
    config.lru_purge_enable=true;
    config.uri_match_fn=httpd_uri_match_wildcard;
    httpd_handle_t server=NULL;
    if(httpd_start(&server,&config)!=ESP_OK) return NULL;
    const httpd_uri_t routes[]={
        {.uri="/",.method=HTTP_GET,.handler=page},
        {.uri="/admin.css",.method=HTTP_GET,.handler=page},
        {.uri="/admin.js",.method=HTTP_GET,.handler=page},
        {.uri="/api/config",.method=HTTP_GET,.handler=config_get},
        {.uri="/api/config",.method=HTTP_POST,.handler=config_post},
        {.uri="/api/controls",.method=HTTP_GET,.handler=controls_get},
        {.uri="/api/controls/refresh",.method=HTTP_POST,.handler=controls_refresh},
        {.uri="/api/controls/refresh",.method=HTTP_GET,.handler=controls_refresh},
        {.uri="/assets/*",.method=HTTP_GET,.handler=asset_get},
    };
    for(size_t i=0;i<sizeof(routes)/sizeof(*routes);i++) {
        if(httpd_register_uri_handler(server,&routes[i])!=ESP_OK) { httpd_stop(server); return NULL; }
    }
    return server;
}
