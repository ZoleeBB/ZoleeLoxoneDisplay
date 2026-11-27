#include "host.h"
#include "zld_update.h"
#include "tone_store.h"
#include "door_ringtone.h"
#include <assert.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

struct test_transport { esp_http_client_config_t config; FILE *f; int code; long length,pos; };
static int network_fault, zip_requests;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *cfg) {
    struct test_transport *c=calloc(1,sizeof(*c)); assert(c); c->config=*cfg;return c;
}
int esp_http_client_open(esp_http_client_handle_t c,int n) {
    const char *u=c->config.url,*file=NULL,*loc=NULL;
    if(!strcmp(u,"https://api.github.com/repos/ZoleeBB/ZoleeEnergyFlowDisplay/releases/latest")) file="release.json";
    else if(strstr(u,"/download/v4.0.0/SHA256SUMS.txt")) loc="https://release-assets.githubusercontent.com/sha";
    else if(strstr(u,"/download/v4.0.0/ZoleeEnergyFlowDisplay-v4.0.0-ringtones.zip"))
        loc=network_fault==1?"http://untrusted.invalid/payload":"https://release-assets.githubusercontent.com/zip";
    else if(!strcmp(u,"https://release-assets.githubusercontent.com/sha")) file="SHA256SUMS.txt";
    else if(!strcmp(u,"https://release-assets.githubusercontent.com/zip")) {file="tones.zip";zip_requests++;}
    else assert(!"Untrusted destination");
    if(loc) {
        c->code=302;esp_http_client_event_t e={.event_id=HTTP_EVENT_ON_HEADER,.user_data=c->config.user_data,
            .header_key="Location",.header_value=(char*)loc};
        c->config.event_handler(&e);return 0;
    }
    c->f=fopen(file,"rb");if(!c->f) return -1;
    fseek(c->f,0,SEEK_END);c->length=ftell(c->f);rewind(c->f);c->code=200;return 0;
}
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t c) {return c->length;}
int64_t esp_http_client_get_content_length(esp_http_client_handle_t c) {return c->length;}
int esp_http_client_get_status_code(esp_http_client_handle_t c) {return c->code;}
int esp_http_client_read(esp_http_client_handle_t c,char *p,int n) {
    if(network_fault==2 && c->length>100 && strstr(c->config.url,"/zip") && c->pos>0) return -1;
    if(n>331) n=331;
    int got=fread(p,1,n,c->f);c->pos+=got;return got;
}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t c) {return c->pos==c->length;}
void esp_http_client_cleanup(esp_http_client_handle_t c) {if(c->f)fclose(c->f);free(c);}
bool zld_card_verify(void) {return true;}
bool zld_card_online(void) {return true;}
bool zld_asset_path(const char *n,char *out,size_t cap) {
    if(strcmp(n,"ringtones/tubular-chime.wav")) return false;
    return snprintf(out,cap,"bundled.wav")<(int)cap;
}
bool zld_ringtone_path(const char *name,char *out,size_t cap) {return tone_resolve(name,out,cap);}
static void list_count(int expected,bool installed) {
    char tag[32];cJSON *list=tone_list(tag);
    assert(list && cJSON_GetArraySize(list)==expected);
    assert(installed?!strcmp(tag,"v4.0.0"):!tag[0]);cJSON_Delete(list);
}
static void listen(const char *selected) {
    size_t n=0,offset,length;
    app_ringtone_load_status_t status;
    uint8_t *data=door_ringtone_load(selected,&n,&status);
    assert(status==APP_RINGTONE_LOAD_OK && data && app_ringtone_wav(data,n,&offset,&length));
    assert(length>0 && offset<n);
    FILE *reference=fopen("selected.wav","rb");assert(reference);
    for(size_t i=0;i<n;i++) assert(fgetc(reference)==data[i]);
    assert(fgetc(reference)==EOF);fclose(reference);free(data);
}
int main(int argc,char **argv) {
    assert(argc==4);
    const char *selected=argv[3];
    int expected=atoi(argv[2]);unsigned reason=0;
    size_t size;app_ringtone_load_status_t status;
    if(!strcmp(argv[1],"reload")) {
        list_count(expected,true);listen(selected);return 0;
    }
    assert(!mkdir("card",0777));assert(!mkdir("card/zld",0777));
    list_count(1,false);
    assert(!door_ringtone_load("",&size,&status) && size==0);
    assert(!door_ringtone_load("../ring.wav",&size,&status));
    network_fault=1;assert(!tone_source_download(&reason));assert(zip_requests==0);list_count(1,false);
    network_fault=2;assert(!tone_source_download(&reason));assert(zip_requests==1);list_count(1,false);
    network_fault=0;assert(tone_source_download(&reason));assert(zip_requests==2);
    list_count(expected,true);listen(selected);
    assert(tone_source_download(&reason));assert(zip_requests==2); /* same release: no ZIP GET */
    char path[256];assert(zld_ringtone_path(selected,path,sizeof(path)));
    FILE *f=fopen(path,"r+b");assert(f);assert(!fseek(f,-1,SEEK_END));int old=fgetc(f);
    assert(!fseek(f,-1,SEEK_END));fputc(old^0x22,f);fclose(f);
    assert(tone_source_download(&reason));assert(zip_requests==3);listen(selected);
    /* Corrupt/incomplete replacement never changes playable generation. */
    FILE *sum=fopen("SHA256SUMS.txt","w");assert(sum);
    fprintf(sum,"%064d  ZoleeEnergyFlowDisplay-v4.0.0-ringtones.zip\n",1);fclose(sum);
    assert(!tone_source_download(&reason));assert(zip_requests==4);list_count(expected,true);listen(selected);
    puts("Official release shape, TLS redirect allowlist, checksum, WAV, interruption, delta, repair, fallback and native load verified.");
}
