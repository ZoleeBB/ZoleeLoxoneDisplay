#include "panel_config.h"
#include "panel_schema.h"
#include "panel_secret.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mbedtls/platform_util.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef PROFILE_PATH
#define PROFILE_PATH "/sdcard/panel.json"
#endif
#define GET(o,k) cJSON_GetObjectItemCaseSensitive(o,k)
#define WIPE(data) mbedtls_platform_zeroize(&(data), sizeof(data))
static const char *const secret_keys[]={"loxonePassword","cameraPassword","adminPassword"};
typedef struct { char value[3][PANEL_SECRET_CAP]; } secrets_t;
static SemaphoreHandle_t lock;
static cJSON *profile;
static secrets_t saved;
static bool primary_valid;
static const char defaults[] =
    "{\"schema\":1,\"revision\":1,\"adminUser\":\"\","
    "\"loxone\":{\"host\":\"\",\"port\":80,\"user\":\"\",\"tls\":false,\"ca_pem\":\"\"},"
    "\"display\":{\"dayBrightness\":80,\"nightBrightness\":10,\"saverSeconds\":0,"
    "\"nightMode\":\"timer\",\"nightStart\":\"22:00\",\"nightEnd\":\"06:00\",\"nightUuid\":\"\"},"
    "\"intercom\":{\"enabled\":false,\"uuid\":\"\",\"gateUuid\":\"\",\"cameraUrl\":\"\","
    "\"cameraUser\":\"\",\"speakerVolume\":55,\"microphoneGain\":80,\"ringtone\":\"tubular-chime.wav\",\"ringtoneRepeat\":false},"
    "\"tabs\":[]}";

/* Schema v1 profiles written before Touch existed are upgraded in memory.
 * A save from a client which omits the new section preserves the active mapping. */
#define EMPTY_CORNER "{\"type\":\"\",\"uuid\":\"\",\"action\":\"\",\"sceneId\":0}"
static const char touch_defaults[] =
    "{\"enabled\":false,\"tapWindowMs\":400,\"corners\":["
    EMPTY_CORNER "," EMPTY_CORNER "," EMPTY_CORNER "," EMPTY_CORNER "],\"center\":{\"uuid\":\"\",\"inputUuid\":\"\"}}";
#undef EMPTY_CORNER

static bool ensure_touch(cJSON *p,const cJSON *existing) {
    if(!cJSON_IsObject(p)) return false;
    if(GET(p,"touch")) return true;
    cJSON *touch=existing?cJSON_Duplicate(existing,true):cJSON_Parse(touch_defaults);
    if(touch && cJSON_AddItemToObject(p,"touch",touch)) return true;
    cJSON_Delete(touch);
    return false;
}

static const char *string(const cJSON *obj,const char *key) {
    const char *s=cJSON_GetStringValue(GET(obj,key));
    return s?s:"";
}
static void message(char *dst,size_t size,const char *s) {
    if (size) snprintf(dst,size,"%s",s);
}

cJSON *panel_parse_document(const char *body,size_t n) {
    if (!body || !n || n>PANEL_CONFIG_MAX_BYTES || memchr(body,0,n) || body[n]!=0) return NULL;
    /* Reject deeply nested input before the recursive parser uses the HTTP task stack. */
    int depth=0;
    bool quoted=false,escaped=false;
    for(size_t i=0;i<n;++i) {
        char ch=body[i];
        if(quoted) {
            if(escaped) {
                /* cJSON decodes U+0000 to a C-string terminator. */
                if(ch=='u' && n-i>=5 && !memcmp(body+i+1,"0000",4)) return NULL;
                escaped=false;
            }
            else if(ch=='\\') escaped=true;
            else if(ch=='"') quoted=false;
        } else if(ch=='"') quoted=true;
        else if(ch=='{' || ch=='[') { if(++depth>12) return NULL; }
        else if(ch=='}' || ch==']') { if(--depth<0) return NULL; }
    }
    return !depth && !quoted ? cJSON_ParseWithLengthOpts(body,n+1,NULL,true):NULL;
}
static void associated(const cJSON *p,int i,char out[480]) {
    const cJSON *lx=GET(p,"loxone"), *ic=GET(p,"intercom");
    if(i==0) snprintf(out,480,"panel-lx|%s|%d|%d|%s",string(lx,"host"),
                      GET(lx,"port")->valueint,cJSON_IsTrue(GET(lx,"tls")),string(lx,"user"));
    else if(i==1) snprintf(out,480,"panel-camera|%s|%s",string(ic,"cameraUrl"),string(ic,"cameraUser"));
    else snprintf(out,480,"panel-admin|%s",string(p,"adminUser"));
}
static bool transfer_secrets(const cJSON *p,cJSON *vault,secrets_t *s,bool encrypt) {
    for(int i=0;i<3;++i) {
        if(encrypt && !s->value[i][0]) continue;
        cJSON *entry=encrypt?cJSON_AddObjectToObject(vault,secret_keys[i]):GET(vault,secret_keys[i]);
        if(!entry && !encrypt) continue;
        char context[480];
        associated(p,i,context);
        if(!entry || !panel_secret_crypt(s->value[i],context,entry,encrypt)) return false;
    }
    /* Never expose an authenticated panel if its admin secret fails to load. */
    return (*string(p,"adminUser") != 0) == (s->value[2][0] != 0);
}
static cJSON *load_file(const char *path,secrets_t *s) {
    FILE *f=fopen(path,"rb");
    if(!f) return NULL;
    char *body=NULL;
    cJSON *root=NULL, *p=NULL;
    if(fseek(f,0,SEEK_END)!=0) goto done;
    long len=ftell(f);
    if(len<=0 || len>PANEL_CONFIG_MAX_BYTES || fseek(f,0,SEEK_SET)!=0) goto done;
    body=malloc((size_t)len+1);
    if(!body || fread(body,1,(size_t)len,f)!=(size_t)len) goto done;
    body[len]=0;
    root=panel_parse_document(body,(size_t)len);
    cJSON *candidate=GET(root,"profile"), *vault=GET(root,"credentials");
    if(!ensure_touch(candidate,NULL) || !panel_schema_validate(candidate,NULL,0) || !cJSON_IsObject(vault) ||
       !transfer_secrets(candidate,vault,s,false)) goto done;
    p=cJSON_DetachItemFromObjectCaseSensitive(root,"profile");
done:
    if(!p) mbedtls_platform_zeroize(s,sizeof(*s));
    cJSON_Delete(root);
    free(body);
    fclose(f);
    return p;
}
esp_err_t panel_config_init(void) {
    if(lock) return ESP_ERR_INVALID_STATE;
    lock=xSemaphoreCreateMutex();
    if(!lock) return ESP_ERR_NO_MEM;
    profile=load_file(PROFILE_PATH,&saved);
    primary_valid=profile!=NULL;
    if(!profile) profile=load_file(PROFILE_PATH ".bak",&saved);
    /* Refuse to open the admin anonymously if a protected profile was damaged or
       came from a different device. Preserve both files for recovery. */
    if(!profile && access(PROFILE_PATH,F_OK)!=0 && errno==ENOENT &&
        access(PROFILE_PATH ".bak",F_OK)!=0 && errno==ENOENT)
        profile=cJSON_Parse(defaults);
    if(profile && !ensure_touch(profile,NULL)) { cJSON_Delete(profile); profile=NULL; return ESP_ERR_NO_MEM; }
    return profile?ESP_OK:ESP_ERR_INVALID_STATE;
}
cJSON *panel_config_snapshot(void) {
    if(!lock) return NULL;
    xSemaphoreTake(lock,portMAX_DELAY);
    cJSON *copy=cJSON_Duplicate(profile,true);
    xSemaphoreGive(lock);
    return copy;
}
uint32_t panel_config_generation(void) {
    if(!lock) return 0;
    xSemaphoreTake(lock,portMAX_DELAY);
    const cJSON *rev=GET(profile,"revision");
    uint32_t v=rev?(uint32_t)rev->valueint:0;
    xSemaphoreGive(lock);
    return v;
}
esp_err_t panel_config_read_secret(const char *key,char *out,size_t size) {
    if(!key || !out || !size || !lock) return ESP_ERR_INVALID_ARG;
    out[0]=0;
    esp_err_t err=ESP_ERR_NOT_FOUND;
    xSemaphoreTake(lock,portMAX_DELAY);
    for(int i=0;i<3;++i) if(!strcmp(key,secret_keys[i])) {
        size_t len=strlen(saved.value[i])+1;
        if(len<=size) { memcpy(out,saved.value[i],len); err=ESP_OK; }
        else err=ESP_ERR_INVALID_SIZE;
        break;
    }
    xSemaphoreGive(lock);
    return err;
}
bool panel_config_web_login(char *user,size_t user_size,char pass[PANEL_SECRET_CAP]) {
    if(!lock || !user || !user_size || !pass) return false;
    xSemaphoreTake(lock,portMAX_DELAY);
    const char *u=string(profile,"adminUser");
    bool ok=profile && strlen(u)<user_size;
    if(ok) {
        memcpy(user,u,strlen(u)+1);
        memcpy(pass,saved.value[2],PANEL_SECRET_CAP);
    }
    xSemaphoreGive(lock);
    return ok;
}
static bool write_file(cJSON *next,secrets_t *secrets) {
    bool ok=false;
    cJSON *root=cJSON_CreateObject();
    char *body=NULL;
    FILE *f=NULL;
    if(!root || !cJSON_AddItemReferenceToObject(root,"profile",next)) goto done;
    cJSON *vault=cJSON_AddObjectToObject(root,"credentials");
    if(!vault || !transfer_secrets(next,vault,secrets,true)) goto done;
    body=cJSON_PrintUnformatted(root);
    if(!body || strlen(body)>PANEL_CONFIG_MAX_BYTES) goto done;
    f=fopen(PROFILE_PATH ".tmp","wb");
    if(!f) goto done;
    size_t n=strlen(body);
    bool synced=fwrite(body,1,n,f)==n && fflush(f)==0 && fsync(fileno(f))==0;
    int rc=fclose(f);
    f=NULL;
    if(!synced || rc) goto done;
    if(primary_valid) {
        if(unlink(PROFILE_PATH ".bak")!=0 && errno!=ENOENT) goto done;
        if(rename(PROFILE_PATH,PROFILE_PATH ".bak")!=0) goto done;
        primary_valid=false;
    }
    else if(unlink(PROFILE_PATH)!=0 && errno!=ENOENT) goto done;
    if(rename(PROFILE_PATH ".tmp",PROFILE_PATH)!=0) goto done;
    primary_valid=true;
    ok=true;
done:
    if(f) fclose(f);
    if(!ok) unlink(PROFILE_PATH ".tmp");
    cJSON_free(body);
    cJSON_Delete(root);
    return ok;
}
esp_err_t panel_config_save(const cJSON *input,char *error,size_t n) {
    if(!lock || !input || !cJSON_IsObject(input)) return ESP_ERR_INVALID_ARG;
    cJSON *next=cJSON_Duplicate(input,true);
    if(!next) return ESP_ERR_NO_MEM;
    for(int i=0;i<3;++i) cJSON_DeleteItemFromObjectCaseSensitive(next,secret_keys[i]);
    esp_err_t err=ESP_FAIL;
    secrets_t fresh={0};
    xSemaphoreTake(lock,portMAX_DELAY);
    if(!profile) { message(error,n,"A konfiguráció nem tölthető be."); goto done; }
    if(!ensure_touch(next,GET(profile,"touch"))) {
        message(error,n,"Nincs elegendő memória az érintőzónák beállításához.");
        err=ESP_ERR_NO_MEM; goto done;
    }
    if(!panel_schema_validate(next,error,n)) { err=ESP_ERR_INVALID_ARG; goto done; }
    int rev=GET(profile,"revision")->valueint;
    if(GET(next,"revision")->valueint!=rev) {
        message(error,n,"A beállítások közben megváltoztak. Töltsd újra az oldalt.");
        err=ESP_ERR_INVALID_STATE; goto done;
    }
    for(int i=0;i<3;++i) {
        const cJSON *v=GET(input,secret_keys[i]);
        if(v && (!cJSON_IsString(v) || strlen(v->valuestring)>=PANEL_SECRET_CAP)) {
            message(error,n,"A jelszó legfeljebb 127 bájt lehet."); err=ESP_ERR_INVALID_ARG; goto done;
        }
        char before[480],after[480];
        associated(profile,i,before);
        associated(next,i,after);
        if(v && v->valuestring[0]) strcpy(fresh.value[i],v->valuestring);
        else if(!strcmp(before,after)) memcpy(fresh.value[i],saved.value[i],PANEL_SECRET_CAP);
    }
    if(*string(GET(next,"loxone"),"host") && !fresh.value[0][0]) {
        message(error,n,"Ehhez a Miniserverhez és felhasználóhoz adj meg jelszót."); err=ESP_ERR_INVALID_ARG; goto done;
    }
    if(cJSON_IsTrue(GET(GET(next,"intercom"),"enabled")) && !fresh.value[1][0]) {
        message(error,n,"Az engedélyezett kaputelefonhoz adj meg kamera-jelszót."); err=ESP_ERR_INVALID_ARG; goto done;
    }
    if(!*string(next,"adminUser")) WIPE(fresh.value[2]);
    if(*string(next,"adminUser") && !fresh.value[2][0]) {
        message(error,n,"A webes felhasználónévhez adj meg jelszót."); err=ESP_ERR_INVALID_ARG; goto done;
    }
    cJSON_SetNumberValue(GET(next,"revision"), rev==2147483647?1:rev+1);
    if(!write_file(next,&fresh)) { message(error,n,"Az SD-kártyára mentés nem sikerült."); goto done; }
    cJSON_Delete(profile);
    profile=next;
    next=NULL;
    memcpy(&saved,&fresh,sizeof(saved));
    err=ESP_OK;
done:
    xSemaphoreGive(lock);
    WIPE(fresh);
    cJSON_Delete(next);
    return err;
}
