#include "tone_store.h"
#include "zld_update.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define API "https://api.github.com/repos/ZoleeBB/ZoleeEnergyFlowDisplay/releases/latest"
#define URL_CAP 2304
#define RELEASE_LIMIT (256*1024)
atomic_uint tone_done_bytes, tone_total_bytes;

static bool allowed(const char *url) {
    static const char *const hosts[]={"https://github.com/","https://api.github.com/",
        "https://release-assets.githubusercontent.com/","https://objects.githubusercontent.com/"};
    for(unsigned i=0;i<sizeof(hosts)/sizeof(*hosts);++i)
        if(!strncmp(url,hosts[i],strlen(hosts[i]))) return true;
    return false;
}
static esp_err_t on_header(esp_http_client_event_t *event) {
    if(event->event_id==HTTP_EVENT_ON_HEADER && event->header_key && !strcasecmp(event->header_key,"Location")) {
        char *redirect=event->user_data;
        if(event->header_value && strlen(event->header_value)<URL_CAP) strcpy(redirect,event->header_value);
    }
    return ESP_OK;
}
/* Only the ZIP is written to SD. JSON/checksums use independently bounded transient buffers. */
static bool fetch(const char *url,size_t limit,char **text,FILE *file,size_t expected) {
    char *current=malloc(URL_CAP),*next=malloc(URL_CAP),*body=NULL;
    uint8_t *buf=malloc(4096);
    bool ok=false;
    if(!current || !next || !buf || strlen(url)>=URL_CAP) goto done;
    strcpy(current,url);
    for(unsigned hop=0;hop<5 && allowed(current);++hop) {
        next[0]=0;
        const esp_http_client_config_t cfg={.url=current,.crt_bundle_attach=esp_crt_bundle_attach,
            .timeout_ms=12000,.buffer_size=4096,.buffer_size_tx=URL_CAP+256,
            .disable_auto_redirect=true,.event_handler=on_header,.user_data=next,
            .user_agent="ZoleeLoxoneDisplay-ringtones"};
        esp_http_client_handle_t c=esp_http_client_init(&cfg);
        if(!c) break;
        bool opened=esp_http_client_open(c,0)==ESP_OK && esp_http_client_fetch_headers(c)>=0;
        int status=opened?esp_http_client_get_status_code(c):0;
        if(status==301 || status==302 || status==303 || status==307 || status==308) {
            esp_http_client_cleanup(c);
            if(!*next || !allowed(next)) break;
            strcpy(current,next);continue;
        }
        int64_t length=opened?esp_http_client_get_content_length(c):0;
        bool stream=status==200 && (length<=0 || (uint64_t)length<=limit);
        size_t used=0,cap=0;
        if(expected && length>0 && (uint64_t)length!=expected) stream=false;
        while(stream) {
            int n=esp_http_client_read(c,(char*)buf,4096);
            if(n==0) break;
            if(n<0 || (size_t)n>limit-used) {stream=false;break;}
            if(file) {
                if(!zld_card_online() || fwrite(buf,1,n,file)!=(size_t)n) {stream=false;break;}
                atomic_store(&tone_done_bytes,used+n);
            } else {
                size_t want=used+n+1;
                if(want>cap) {
                    size_t more=cap?cap*2:4097;
                    if(more<want) more=want;
                    if(more>limit+1) more=limit+1;
                    char *grown=realloc(body,more);
                    if(!grown) {stream=false;break;} body=grown;cap=more;
                }
                memcpy(body+used,buf,n);
            }
            used+=n;
        }
        ok=stream && used && (!expected || used==expected) &&
           esp_http_client_is_complete_data_received(c);
        esp_http_client_cleanup(c);
        if(ok && text) {
            if(memchr(body,0,used)) {ok=false;break;}
            body[used]=0; *text=body;body=NULL;
        }
        break;
    }
done:
    free(buf);free(body);free(next);free(current);return ok;
}
static bool stable(const char *tag) {
    if(!tag || tag[0]!='v' || strlen(tag)>31) return false;
    const char *p=tag+1;
    for(int i=0;i<3;i++) {
        if(!isdigit((unsigned char)*p)) return false;
        const char *start=p;
        while(isdigit((unsigned char)*p)) ++p;
        if(p-start>1 && *start=='0') return false;
        if(i<2 && *p++!='.') return false;
    }
    return !*p;
}
static const char *field(const cJSON *o,const char *k) {return cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(o,k));}
bool tone_release_info(const char *json,char tag[32],size_t *size) {
    cJSON *doc=cJSON_Parse(json),*assets=cJSON_GetObjectItemCaseSensitive(doc,"assets");
    const char *v=field(doc,"tag_name");
    bool zip=false,sums=false;
    if(!stable(v) || !cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(doc,"draft")) ||
       !cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(doc,"prerelease")) || !cJSON_IsArray(assets)) goto done;
    char name[112],url[256],sumurl[256];snprintf(name,sizeof(name),"ZoleeEnergyFlowDisplay-%s-ringtones.zip",v);
    snprintf(url,sizeof(url),TONE_ORIGIN "/download/%s/%s",v,name);
    snprintf(sumurl,sizeof(sumurl),TONE_ORIGIN "/download/%s/SHA256SUMS.txt",v);
    const cJSON *item;
    cJSON_ArrayForEach(item,assets) {
        const char *n=field(item,"name"),*u=field(item,"browser_download_url"),*s=field(item,"state");
        if(!n || !u || !s || strcmp(s,"uploaded")) continue;
        double b=cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(item,"size"));
        if(!strcmp(n,name) && !strcmp(u,url)) {
            if(zip || !(b>=22 && b<=TONE_ZIP_LIMIT) || b!=(size_t)b) {zip=false;goto done;}
            zip=true;*size=(size_t)b;
        } else if(!strcmp(n,"SHA256SUMS.txt") && !strcmp(u,sumurl)) {
            if(sums || !(b>=65 && b<=16384)) {sums=false;goto done;}
            sums=true;
        }
    }
    if(zip && sums) strcpy(tag,v);
done:
    cJSON_Delete(doc);return zip && sums;
}
bool tone_checksum(const char *list,const char *name,char sha[65]) {
    bool found=false;
    const char *p=list;
    while(*p) {
        const char *end=strchr(p,'\n'); if(!end) end=p+strlen(p);
        size_t n=end-p;if(n && p[n-1]=='\r') --n;
        if(n==66+strlen(name) && p[64]==' ' && (p[65]==' ' || p[65]=='*') && !memcmp(p+66,name,strlen(name))) {
            if(found) return false;
            for(unsigned i=0;i<64;i++) {
                if(!isxdigit((unsigned char)p[i])) return false;
                sha[i]=(char)tolower((unsigned char)p[i]);
            }
            sha[64]=0;found=true;
        }
        p=*end?end+1:end;
    }
    return found;
}
bool tone_source_download(unsigned *reason) {
    char tag[32]="",name[112],url[256],digest[65],error[160]="";
    char *body=NULL;
    size_t bytes=0;bool ok=false;
    *reason=TONE_NETWORK;
    if(!fetch(API,RELEASE_LIMIT,&body,NULL,0) || !tone_release_info(body,tag,&bytes)) goto done;
    free(body);body=NULL;
    snprintf(name,sizeof(name),"ZoleeEnergyFlowDisplay-%s-ringtones.zip",tag);
    snprintf(url,sizeof(url),TONE_ORIGIN "/download/%s/SHA256SUMS.txt",tag);
    *reason=TONE_CHECKSUM;
    if(!fetch(url,16384,&body,NULL,0) || !tone_checksum(body,name,digest)) goto done;
    free(body);body=NULL;
    if(tone_current_verified(digest)) {ok=true;goto done;}
    *reason=TONE_WRITE;
    if(!tone_prepare()) goto done;
    atomic_store(&tone_total_bytes,(unsigned)bytes);
    snprintf(url,sizeof(url),TONE_ORIGIN "/download/%s/%s",tag,name);
    FILE *f=fopen(TONE_ARCHIVE_PATH,"wb"); if(!f) goto done;
    *reason=TONE_NETWORK;
    ok=fetch(url,TONE_ZIP_LIMIT,NULL,f,bytes);
    if(fclose(f)) ok=false;
    if(ok) {
        *reason=TONE_ARCHIVE;
        ok=tone_install(TONE_ARCHIVE_PATH,tag,digest,error);
    }
    unlink(TONE_ARCHIVE_PATH);
done:
    free(body);return ok;
}
