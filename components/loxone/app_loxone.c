/* Loxone Direct, adapted from ZoleeEnergyFlowDisplay. The worker is the
 * sole WS owner. Configuration, pages and credentials switch together. */
#include "panel_profile.h"
#include "zld_loxone.h"
#include "audio_schedule.h"
#include "app_intercom_audio.h"
#include "esp_heap_caps.h"
#define intercom_held(revision) app_intercom_audio_active_for(revision)
#include "esp_websocket_client.h"
#include "esp_random.h"
#include "esp_http_client.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbedtls/aes.h"
#include "mbedtls/base64.h"
#include "mbedtls/md.h"
#include "mbedtls/pk.h"
#include "mbedtls/platform_util.h"
#include <math.h>
#include <limits.h>
#include <stdatomic.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LX_TIMEOUT_MS 12000
#define LX_QUEUE_DEPTH 8
#define LX_KEEPALIVE_US 15000000LL
#define LX_MAX_RX (LX_STRUCTURE_MAX + 1024U)
typedef struct { int type; size_t length; unsigned char *data; } message_t;

static _Atomic(SemaphoreHandle_t) s_lock;
static atomic_flag s_starting = ATOMIC_FLAG_INIT;
enum { CATALOG_IDLE, CATALOG_REQUESTED, CATALOG_LOADING };
static atomic_int s_refresh;
static void finish_refresh(void)
{
    int loading = CATALOG_LOADING;
    atomic_compare_exchange_strong(&s_refresh, &loading, CATALOG_IDLE);
}
static QueueHandle_t s_messages, s_requests, s_home_commands;
static panel_profile_t *s_profile;
static bool (*s_network_ready)(void);
static _Atomic(lx_home_config_t *) s_home;
static bool s_home_busy;
static unsigned s_home_revision;
#ifdef ESP_PLATFORM
#include "esp_attr.h"
static EXT_RAM_BSS_ATTR lx_home_values_t s_home_values;
#else
static lx_home_values_t s_home_values;
#endif
static bool s_home_live, s_command_pending;
/* Kapcsolatvesztés nem 0 állapot: a már megnyitott kaputelefon megmarad. */
static bool s_intercom_ringing;
static unsigned s_intercom_generation;

static unsigned s_home_session;
static int64_t s_command_deadline, s_home_response;
static char s_command_status[LX_HOME_ERROR_SIZE];
typedef struct {
    char id[LX_HOME_ID_SIZE];
    lx_home_command_t action;
    unsigned revision, session, intercom_revision, visibility_revision, audio_edit_generation;
    double position_percent;
    int64_t queued_us;
} home_command_t;
static home_command_t s_pending_command;
static bool update_values(int type, const uint8_t *data, size_t size);
static void home_command_poll(int64_t now);

static cJSON *s_catalog;
static bool home_resolve_icons_locked(void);
static char s_status[192]="A webfelületen add meg a Loxone beállításait.";
/* A hiba csak lépésnevet és kódot tartalmazhat, parancsot/tokent soha. */
static char s_stage[80],s_failure[192];
static esp_websocket_client_handle_t s_ws;
/* A fogadópuffert kizárólag a websocket feladat éri el, destroy után a worker szabadítja. */
static unsigned char *s_rx;
static size_t s_rx_len,s_rx_cap;
static bool s_rx_bad;
static int s_message_type=-1;
static atomic_bool s_dead;
/* A fogadó callback csak hibakódot ír; a felhasználói szöveg a workerben készül. */
static atomic_int s_rx_failure;
enum { LX_RX_OK, LX_RX_TOO_LARGE, LX_RX_NO_MEMORY, LX_RX_BAD_HEADER, LX_RX_QUEUE_FULL };
static unsigned char s_aes_key[32],s_iv[16];
static char s_salt[17];
static char *s_public_key;

static lx_values_t s_energy[LX_HOME_MAX_PAGES], s_night_values;
static const lx_config_t *s_session_config;
static int64_t s_last_response,s_token_refresh_us;

static void status(const char *message)
{
    xSemaphoreTake(s_lock,portMAX_DELAY);snprintf(s_status,sizeof(s_status),"%s",message);xSemaphoreGive(s_lock);
}
static void phase(const char *label)
{
    snprintf(s_stage,sizeof(s_stage),"%s",label);s_failure[0]=0;status(label);
}
static bool failed(const char *reason,int code)
{
    if(code)snprintf(s_failure,sizeof(s_failure),"%.70s: %.85s (%d)",s_stage,reason,code);
    else snprintf(s_failure,sizeof(s_failure),"%.70s: %.110s",s_stage,reason);
    return false;
}
static void report_failure(const char *fallback)
{status(s_failure[0]?s_failure:fallback);}
static const char *string(const cJSON *o,const char *k)
{const cJSON *v=cJSON_GetObjectItemCaseSensitive(o,k);return cJSON_IsString(v)?v->valuestring:"";}
static void hex(const unsigned char *in,size_t n,char *out)
{static const char h[]="0123456789abcdef";for(size_t i=0;i<n;i++){out[2*i]=h[in[i]>>4];out[2*i+1]=h[in[i]&15];}out[2*n]=0;}
static bool unhex(const char *in,unsigned char *out,size_t max,size_t *n)
{
    size_t len=strlen(in);if(!len||len%2||len/2>max)return false;
    for(size_t i=0;i<len;i++)if(!strchr("0123456789abcdefABCDEF",in[i]))return false;
    for(size_t i=0;i<len/2;i++){unsigned v;if(sscanf(in+2*i,"%2x",&v)!=1)return false;out[i]=v;}
    *n=len/2;return true;
}
static char *urlencode(const char *in)
{
    size_t n=strlen(in);char *out=malloc(n*3+1);if(!out)return NULL;
    char *p=out;for(size_t i=0;i<n;i++) {
        unsigned char c=in[i];
        if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||strchr("-_.~",c))*p++=c;
        else {snprintf(p,4,"%%%02X",c);p+=3;}
    }*p=0;return out;
}
static void free_message(message_t *m){free(m->data);m->data=NULL;}
static void receive_event(void *arg,esp_event_base_t base,int32_t event,void *data)
{
    (void)arg;(void)base;
    if(event==WEBSOCKET_EVENT_DISCONNECTED||event==WEBSOCKET_EVENT_ERROR){s_dead=true;return;}
    if(event!=WEBSOCKET_EVENT_DATA||s_dead)return;
    esp_websocket_event_data_t *e=data;
    if(e->op_code==8){s_dead=true;return;}
    if(e->op_code!=0&&e->op_code!=1&&e->op_code!=2)return;
    if(e->data_len<0||e->payload_offset<0||e->payload_len<0){s_dead=true;return;}
    if(e->payload_offset==0&&e->op_code!=0) {s_rx_len=0;s_rx_bad=false;}
    if((size_t)e->data_len>LX_MAX_RX-s_rx_len){s_rx_failure=LX_RX_TOO_LARGE;s_rx_bad=true;s_dead=true;return;}
    size_t need=s_rx_len+e->data_len+1;
    if(need>s_rx_cap) {
        size_t cap=s_rx_cap?s_rx_cap:4096;
        while(cap<need){if(cap>(LX_MAX_RX+1)/2){cap=LX_MAX_RX+1;break;}cap*=2;}
        unsigned char *next=realloc(s_rx,cap);
        if(!next){s_rx_failure=LX_RX_NO_MEMORY;s_rx_bad=true;s_dead=true;return;}
        s_rx=next;s_rx_cap=cap;
    }
    memcpy(s_rx+s_rx_len,e->data_ptr,e->data_len);s_rx_len+=e->data_len;s_rx[s_rx_len]=0;
    if(e->payload_offset+e->data_len!=e->payload_len||!e->fin)return;
    if(s_rx_bad)return;
    /* A Loxone fejléc önálló WS-üzenet, az utána következő payload külön üzenet. */
    if(s_message_type<0) {
        if(s_rx_len!=8||s_rx[0]!=3){s_rx_failure=LX_RX_BAD_HEADER;s_dead=true;return;}
        /* A 0x80-as estimated fejléc után még PONTOS fejléc jön, nem payload.
         * A becslésből nem foglalunk memóriát és nem következtetünk üzenettípusra. */
        if(s_rx[2]&0x80){s_rx_len=0;return;}
        unsigned declared=(unsigned)s_rx[4]|(unsigned)s_rx[5]<<8|(unsigned)s_rx[6]<<16|(unsigned)s_rx[7]<<24;
        if(declared>LX_MAX_RX){s_rx_failure=LX_RX_TOO_LARGE;s_dead=true;return;}
        int type=s_rx[1];
        if(type==6) {
            message_t m={.type=6};if(xQueueSend(s_messages,&m,0)!=pdTRUE){s_rx_failure=LX_RX_QUEUE_FULL;s_dead=true;}
        }else if(type==5) {s_dead=true;return;}
        else s_message_type=type;
        s_rx_len=0;return;
    }
    message_t m={.type=s_message_type,.length=s_rx_len,.data=s_rx};s_message_type=-1;
    s_rx=NULL;s_rx_len=0;s_rx_cap=0;
    if(xQueueSend(s_messages,&m,0)!=pdTRUE){free_message(&m);s_rx_failure=LX_RX_QUEUE_FULL;s_dead=true;}
}
static void mark_response(void)
{
    s_last_response = esp_timer_get_time();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_home_response = s_last_response;
    xSemaphoreGive(s_lock);
}
static bool send_plain(const char *command)
{return !s_dead&&esp_websocket_client_send_text(s_ws,command,strlen(command),pdMS_TO_TICKS(LX_TIMEOUT_MS))==(int)strlen(command);}
static bool send_encrypted(const char *command)
{
    unsigned char salt[8],iv[16];char sh[17];esp_fill_random(salt,sizeof(salt));hex(salt,sizeof(salt),sh);
    size_t n=strlen(command)+strlen(sh)+strlen(s_salt)+16,padded=(n+15)&~(size_t)15;
    unsigned char *input=calloc(1,padded+1),*cipher=malloc(padded),*base=malloc(padded*2+8);
    if(!input||!cipher||!base){free(input);free(cipher);free(base);return false;}
    int actual=s_salt[0]?snprintf((char*)input,padded+1,"nextSalt/%s/%s/%s",s_salt,sh,command):
        snprintf((char*)input,padded+1,"salt/%s/%s",sh,command);
    bool ok=actual>0&&(size_t)actual<=padded;size_t b64=0;mbedtls_aes_context aes;mbedtls_aes_init(&aes);
    memcpy(iv,s_iv,sizeof(iv));
    if(ok)ok=mbedtls_aes_setkey_enc(&aes,s_aes_key,256)==0&&
        mbedtls_aes_crypt_cbc(&aes,MBEDTLS_AES_ENCRYPT,padded,iv,input,cipher)==0&&
        mbedtls_base64_encode(base,padded*2+8,&b64,cipher,padded)==0;
    char *escaped=NULL,*wrapped=NULL;
    if(ok){base[b64]=0;escaped=urlencode((char*)base);if(escaped){wrapped=malloc(strlen(escaped)+16);if(wrapped)sprintf(wrapped,"jdev/sys/enc/%s",escaped);}ok=wrapped&&send_plain(wrapped);}
    if(ok)snprintf(s_salt,sizeof(s_salt),"%s",sh);
    mbedtls_aes_free(&aes);if(input)mbedtls_platform_zeroize(input,padded);free(input);free(cipher);free(base);free(escaped);free(wrapped);return ok;
}
static cJSON *reply(const char *command,bool encrypt)
{
    if(!(encrypt?send_encrypted(command):send_plain(command))){failed("A kérés nem küldhető el.",0);return NULL;}
    int64_t deadline=esp_timer_get_time()+LX_TIMEOUT_MS*1000LL;
    while(!s_dead&&esp_timer_get_time()<deadline) {
        message_t m;if(xQueueReceive(s_messages,&m,pdMS_TO_TICKS(100))!=pdTRUE)continue;
        mark_response();
        if(m.type!=0){if(s_session_config&&!update_values(m.type,m.data,m.length))s_dead=true;free_message(&m);continue;}
        cJSON *root=cJSON_ParseWithLength((char*)m.data,m.length);free_message(&m);
        cJSON *ll=cJSON_GetObjectItemCaseSensitive(root,"LL");
        cJSON *code=cJSON_GetObjectItemCaseSensitive(ll,"Code");
        if(!code)code=cJSON_GetObjectItemCaseSensitive(ll,"code");
        bool ok=(cJSON_IsNumber(code)&&code->valueint==200)||(cJSON_IsString(code)&&strcmp(code->valuestring,"200")==0);
        if(!ok) {
            int response_code=cJSON_IsNumber(code)?code->valueint:cJSON_IsString(code)?atoi(code->valuestring):0;
            failed(response_code?"A Miniserver elutasította a kérést.":"Érvénytelen Miniserver-válasz.",response_code);
        }
        const cJSON *returned=cJSON_GetObjectItemCaseSensitive(ll,"value");
        /* A parancsnyugta 200 mellett érték nélkül is érvényes. A kapcsoló
         * sikerét ettől függetlenül kizárólag a bináris állapot igazolja. */
        cJSON *value=ok?(returned?cJSON_Duplicate(returned,true):cJSON_CreateNull()):NULL;
        cJSON_Delete(root);return value;
    }failed(s_dead?"A kapcsolat megszakadt.":"12 másodpercen belül nem érkezett válasz.",0);return NULL;
}
static int rng(void *ctx,unsigned char *out,size_t len){(void)ctx;esp_fill_random(out,len);return 0;}
static bool key_exchange(void)
{
    phase("Loxone titkosítási kulcscsere");
    if(!s_public_key)return failed("Hiányzó nyilvános kulcs.",0);
    /* Loxone egyes verziói SPKI kulcsot CERTIFICATE fejléccel küldenek. */
    const char *body=strstr(s_public_key,"-----BEGIN CERTIFICATE-----");
    char *pem=NULL;
    if(body) {
        body+=27;const char *end=strstr(body,"-----END CERTIFICATE-----");
        if(end){size_t n=end-body;pem=malloc(n+64);if(pem)snprintf(pem,n+64,"-----BEGIN PUBLIC KEY-----\n%.*s\n-----END PUBLIC KEY-----\n",(int)n,body);}
    }else pem=strdup(s_public_key);
    if(!pem)return failed("A nyilvános kulcs formátuma nem értelmezhető.",0);
    mbedtls_pk_context pk;mbedtls_pk_init(&pk);
    int pk_result=mbedtls_pk_parse_public_key(&pk,(unsigned char*)pem,strlen(pem)+1);
    bool ok=pk_result==0;free(pem);
    if(!ok)failed("A nyilvános kulcs feldolgozása sikertelen.",pk_result);
    unsigned char encrypted[512],base[704];char key[65],iv[33],plain[100];size_t n=0,b64=0;
    esp_fill_random(s_aes_key,sizeof(s_aes_key));esp_fill_random(s_iv,sizeof(s_iv));hex(s_aes_key,32,key);hex(s_iv,16,iv);
    snprintf(plain,sizeof(plain),"%s:%s",key,iv);
    if(ok){int result=mbedtls_pk_encrypt(&pk,(unsigned char*)plain,strlen(plain),encrypted,&n,sizeof(encrypted),rng,NULL);ok=result==0;if(!ok)failed("Az RSA kulcscsere előkészítése sikertelen.",result);}
    mbedtls_pk_free(&pk);mbedtls_platform_zeroize(plain,sizeof(plain));mbedtls_platform_zeroize(key,sizeof(key));
    if(ok)ok=mbedtls_base64_encode(base,sizeof(base)-1,&b64,encrypted,n)==0;
    if(!ok)return false;
    /* A kulcscsere nyers Base64-t vár; csak az AES parancsok URL-kódoltak. */
    base[b64]=0;
    char command[sizeof(base)+32];snprintf(command,sizeof(command),"jdev/sys/keyexchange/%s",base);
    cJSON *value=reply(command,false);ok=value!=NULL;cJSON_Delete(value);return ok;
}
static bool hash_hmac(const cJSON *key,const char *input,char out[65])
{
    const char *algorithm=string(key,"hashAlg");
    mbedtls_md_type_t type=strcmp(algorithm,"SHA256")==0?MBEDTLS_MD_SHA256:strcmp(algorithm,"SHA1")==0?MBEDTLS_MD_SHA1:MBEDTLS_MD_NONE;
    const mbedtls_md_info_t *md=mbedtls_md_info_from_type(type);if(!md)return false;
    unsigned char k[64],digest[32];size_t n;
    if(!unhex(string(key,"key"),k,sizeof(k),&n))return false;
    bool ok=mbedtls_md_hmac(md,k,n,(unsigned char*)input,strlen(input),digest)==0;
    if(ok)hex(digest,mbedtls_md_get_size(md),out);
    mbedtls_platform_zeroize(k,sizeof(k));return ok;
}
static void schedule_token_refresh(const cJSON *value){
    const cJSON *until=cJSON_GetObjectItemCaseSensitive(value,"validUntil");
    int64_t valid_until=cJSON_IsNumber(until)&&until->valuedouble>0&&until->valuedouble<1e12?(int64_t)until->valuedouble:0;
    s_token_refresh_us=esp_timer_get_time()+lx_token_refresh_delay_us(valid_until,(int64_t)time(NULL));
}
static bool acquire_token(const lx_config_t *cfg,char *token,size_t token_size)
{
    phase("Loxone felhasználói hitelesítési kulcs");
    char *user=urlencode(cfg->username);if(!user)return false;
    char command[1536];snprintf(command,sizeof(command),"jdev/sys/getkey2/%s",user);
    cJSON *key=reply(command,false);if(!key){free(user);return false;}
    const char *algorithm=string(key,"hashAlg");
    const mbedtls_md_info_t *md=mbedtls_md_info_from_type(strcmp(algorithm,"SHA256")==0?MBEDTLS_MD_SHA256:strcmp(algorithm,"SHA1")==0?MBEDTLS_MD_SHA1:MBEDTLS_MD_NONE);
    char salted[512],password_hash[65],input[256],hmac[65];unsigned char digest[32];
    int n=snprintf(salted,sizeof(salted),"%s:%s",cfg->password,string(key,"salt"));
    bool ok=md&&n>0&&n<(int)sizeof(salted)&&mbedtls_md(md,(unsigned char*)salted,n,digest)==0;
    if(ok) {
        hex(digest,mbedtls_md_get_size(md),password_hash);
        for(char *p=password_hash;*p;p++)if(*p>='a'&&*p<='f')*p-=32;
        snprintf(input,sizeof(input),"%s:%s",cfg->username,password_hash);ok=hash_hmac(key,input,hmac);
    }
    cJSON_Delete(key);mbedtls_platform_zeroize(salted,sizeof(salted));mbedtls_platform_zeroize(password_hash,sizeof(password_hash));mbedtls_platform_zeroize(input,sizeof(input));
    if(ok) {
        unsigned char mac[6];if(esp_efuse_mac_get_default(mac)!=ESP_OK){free(user);return false;}
        snprintf(command,sizeof(command),"jdev/sys/getjwt/%s/%s/4/7f30a221-5e2d-4100-918a%02x%02x%02x%02x%02x%02x/ZoleeLoxoneDisplay",hmac,user,
            mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
        phase("Loxone felhasználói bejelentkezés");
        cJSON *v=reply(command,true);const char *t=string(v,"token");
        ok=*t&&strlen(t)<token_size;if(ok){snprintf(token,token_size,"%s",t);schedule_token_refresh(v);}cJSON_Delete(v);
    }free(user);return ok;
}
static bool token_command(const lx_config_t *cfg,char *token,const char *operation)
{
    cJSON *key=reply("jdev/sys/getkey",false);char hmac[65];
    /* getkey régebbi firmware esetén egyszerű hex string, SHA1 alapú. */
    if(cJSON_IsString(key)) {cJSON *o=cJSON_CreateObject();cJSON_AddStringToObject(o,"key",key->valuestring);cJSON_AddStringToObject(o,"hashAlg","SHA1");cJSON_Delete(key);key=o;}
    bool ok=key&&hash_hmac(key,token,hmac);cJSON_Delete(key);if(!ok)return false;
    char *user=urlencode(cfg->username);if(!user)return false;
    char cmd[768];snprintf(cmd,sizeof(cmd),"%s%s/%s/%s",strcmp(operation,"authwithtoken")==0?"":"jdev/sys/",operation,hmac,user);free(user);
    cJSON *v=reply(cmd,true);ok=v!=NULL;
    if(ok&&strcmp(operation,"refreshjwt")==0){const char *t=string(v,"token");ok=*t&&strlen(t)<1024;if(ok)snprintf(token,1024,"%s",t);}
    if(ok&&strcmp(operation,"killtoken")!=0)schedule_token_refresh(v);
    cJSON_Delete(v);return ok;
}
static bool renew_token(const lx_config_t *cfg,char token[1024]){
    if(token_command(cfg,token,"refreshjwt"))return true;
    return !s_dead&&acquire_token(cfg,token,1024);
}
static bool download_catalog(int *response_code)
{
    *response_code=0;
    phase("Loxone adatlista letöltése");
    /* LoxAPP3.json esetén enc támogatott, fenc (titkosított válasz) nem. */
    if(!send_encrypted("data/LoxAPP3.json"))return failed("Az adatlista kérése nem küldhető el.",0);
    int64_t deadline=esp_timer_get_time()+LX_TIMEOUT_MS*1000LL;
    while(!s_dead&&esp_timer_get_time()<deadline) {
        message_t m;if(xQueueReceive(s_messages,&m,pdMS_TO_TICKS(100))!=pdTRUE)continue;
        if(m.type!=0){free_message(&m);continue;}
        cJSON *catalog=lx_catalog_parse((char*)m.data,m.length);
        if(!catalog) {
            /* A hibaválasz nem katalógus: ne nyeljük el időtúllépésként. */
            cJSON *root=cJSON_ParseWithLength((char*)m.data,m.length);
            const cJSON *ll=cJSON_GetObjectItemCaseSensitive(root,"LL");
            const cJSON *code=cJSON_GetObjectItemCaseSensitive(ll,"Code");
            if(!code)code=cJSON_GetObjectItemCaseSensitive(ll,"code");
            int result=cJSON_IsNumber(code)?code->valueint:cJSON_IsString(code)?atoi(code->valuestring):0;
            *response_code=result;
            if(ll)failed(result==403?"Az adatlista hozzáférése tiltott. Ellenőrizd a felhasználói jogosultságot.":"A Miniserver adatlista helyett parancsválaszt küldött.",result);
            else if(m.length>LX_STRUCTURE_MAX)failed("Az adatlista meghaladja az 512 KiB méretkorlátot.",0);
            else if(!root)failed("A válasz nem érvényes JSON, hiányos, vagy nincs elég memória.",0);
            else if(!cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(root,"controls")))failed("A válaszból hiányzik a controls adatlista.",0);
            else failed("Az állapotlista feldolgozása sikertelen (512 állapot, mélység- vagy memóriakorlát).",0);
            cJSON_Delete(root);free_message(&m);return false;
        }
        free_message(&m);
        xSemaphoreTake(s_lock,portMAX_DELAY);cJSON_Delete(s_catalog);s_catalog=catalog;
        if (home_resolve_icons_locked()) ++s_home_revision;
        xSemaphoreGive(s_lock);return true;
    }
    if(s_rx_failure==LX_RX_TOO_LARGE)return failed("A fogadott üzenet meghaladja az 513 KiB vételi korlátot.",0);
    if(s_rx_failure==LX_RX_NO_MEMORY)return failed("Nincs elég memória az adatlista fogadásához.",0);
    if(s_rx_failure==LX_RX_BAD_HEADER)return failed("Érvénytelen Loxone üzenetfejléc érkezett.",0);
    if(s_rx_failure==LX_RX_QUEUE_FULL)return failed("A fogadott üzenetek sora megtelt.",0);
    return failed(s_dead?"A kapcsolat megszakadt az adatlista fogadása közben.":"12 másodpercen belül nem érkezett adatlista.",0);
}
/* A HTTP fájlútvonal egyes Miniszervereken elérhető akkor is, ha a WS 404-et ad.
 * Csak 404-re váltunk át: a jogosultsági hibákat nem kerüljük meg. */
static char *http_body(const lx_config_t *cfg,const char *path,size_t limit,size_t *length)
{
    size_t url_size=strlen(path)+strlen(cfg->host)+32;
    char *url=malloc(url_size);if(!url){failed("Nincs elég memória a HTTP kéréshez.",0);return NULL;}
    snprintf(url,url_size,"%s://%s:%u/%s",cfg->tls?"https":"http",cfg->host,(unsigned)cfg->port,path);
    esp_http_client_config_t options={.url=url,.timeout_ms=LX_TIMEOUT_MS,
        .cert_pem=cfg->tls?cfg->ca_pem:NULL,.disable_auto_redirect=true};
    esp_http_client_handle_t http=esp_http_client_init(&options);
    if(!http){free(url);failed("A HTTP kliens nem indítható.",0);return NULL;}
    int error=esp_http_client_open(http,0);bool ok=error==ESP_OK;
    if(!ok)failed("A HTTP kapcsolat nem épült fel.",error);
    if(ok){
        int64_t size=esp_http_client_fetch_headers(http);int code=esp_http_client_get_status_code(http);
        ok=size>=0&&code==200;
        if(!ok)failed("A HTTP lekérés sikertelen (átirányítás nem követhető).",size<0?(int)size:code);
        else if((uint64_t)size>limit){ok=false;failed("A HTTP válasz meghaladja a megengedett méretet.",0);}
    }
    size_t used=0,capacity=limit<4096?limit:4096;
    char *body=ok?malloc(capacity+1):NULL;
    if(ok&&!body){ok=false;failed("Nincs elég memória a HTTP válaszhoz.",0);}
    while(ok){
        if(used==capacity){
            if(capacity==limit){
                char extra;int count=esp_http_client_read(http,&extra,1);
                if(count!=0){ok=false;failed("A HTTP válasz túl nagy vagy hiányos.",0);}break;
            }
            size_t next=capacity>limit/2?limit:capacity*2;
            char *larger=realloc(body,next+1);
            if(!larger){ok=false;failed("Nincs elég memória a HTTP válasz bővítéséhez.",0);break;}
            body=larger;capacity=next;
        }
        int count=esp_http_client_read(http,body+used,capacity-used);
        if(count<0){ok=false;failed("A HTTP válasz olvasása megszakadt.",count);break;}
        if(count==0)break;
        used+=count;
    }
    if(ok&&!esp_http_client_is_complete_data_received(http)){ok=false;failed("Hiányos HTTP válasz érkezett.",0);}
    esp_http_client_close(http);esp_http_client_cleanup(http);free(url);
    if(!ok){free(body);return NULL;}
    body[used]=0;*length=used;return body;
}
static bool download_catalog_with_fallback(const lx_config_t *cfg,const char *token)
{
    int code=0;if(download_catalog(&code))return true;
    if(code!=404||s_dead)return false;
    phase("Loxone adatlista / HTTP tokenhitelesítés");
    size_t length=0;char *body=http_body(cfg,"jdev/sys/getkey",8192,&length);
    if(!body)return false;
    cJSON *root=cJSON_ParseWithLength(body,length);free(body);
    const cJSON *ll=cJSON_GetObjectItemCaseSensitive(root,"LL");
    const cJSON *status_code=cJSON_GetObjectItemCaseSensitive(ll,"Code");
    if(!status_code)status_code=cJSON_GetObjectItemCaseSensitive(ll,"code");
    int result=cJSON_IsNumber(status_code)?status_code->valueint:cJSON_IsString(status_code)?atoi(status_code->valuestring):0;
    cJSON *key=result==200?cJSON_Duplicate(cJSON_GetObjectItemCaseSensitive(ll,"value"),true):NULL;cJSON_Delete(root);
    if(!key)return failed("A HTTP hitelesítési kulcs nem érkezett meg.",result);
    if(cJSON_IsString(key)){
        cJSON *object=cJSON_CreateObject();
        if(object){cJSON_AddStringToObject(object,"key",key->valuestring);cJSON_AddStringToObject(object,"hashAlg","SHA1");}
        cJSON_Delete(key);key=object;
    }
    char hash[65];bool ok=key&&hash_hmac(key,token,hash);cJSON_Delete(key);
    if(!ok)return failed("A HTTP tokenhitelesítés nem készíthető elő.",0);
    char *user=urlencode(cfg->username);if(!user)return failed("Nincs elég memória a felhasználónévhez.",0);
    char path[512];snprintf(path,sizeof(path),"data/LoxAPP3.json?autht=%s&user=%s",hash,user);free(user);
    phase("Loxone adatlista / HTTP letöltés");
    body=http_body(cfg,path,LX_STRUCTURE_MAX,&length);mbedtls_platform_zeroize(path,sizeof(path));
    if(!body)return false;
    cJSON *catalog=lx_catalog_parse(body,length);
    if(!catalog){
        root=cJSON_ParseWithLength(body,length);ll=cJSON_GetObjectItemCaseSensitive(root,"LL");
        status_code=cJSON_GetObjectItemCaseSensitive(ll,"Code");
        if(!status_code)status_code=cJSON_GetObjectItemCaseSensitive(ll,"code");
        result=cJSON_IsNumber(status_code)?status_code->valueint:cJSON_IsString(status_code)?atoi(status_code->valuestring):0;
        cJSON_Delete(root);free(body);
        return failed(result?"A Miniserver a HTTP adatlista kérését elutasította.":"A HTTP adatlista nem dolgozható fel (JSON, állapot- vagy memóriakorlát).",result);
    }
    free(body);
    if(s_dead){cJSON_Delete(catalog);return failed("A WebSocket kapcsolat megszakadt a HTTP letöltés közben.",0);}
    xSemaphoreTake(s_lock,portMAX_DELAY);cJSON_Delete(s_catalog);s_catalog=catalog;
        if (home_resolve_icons_locked()) ++s_home_revision;
        xSemaphoreGive(s_lock);
    return true;
}
static void close_connection(void)
{
    if(s_ws){esp_websocket_client_stop(s_ws);esp_websocket_client_destroy(s_ws);s_ws=NULL;}
    free(s_public_key);s_public_key=NULL;s_salt[0]=0;
    free(s_rx);s_rx=NULL;s_rx_len=s_rx_cap=0;s_message_type=-1;
    message_t m;while(xQueueReceive(s_messages,&m,0)==pdTRUE)free_message(&m);
    mbedtls_platform_zeroize(s_aes_key,sizeof(s_aes_key));mbedtls_platform_zeroize(s_iv,sizeof(s_iv));
}
/* A nyilvános kulcsot még a rövid WS hitelesítési határidő ELŐTT kérjük le. */
static bool fetch_public_key(const lx_config_t *cfg)
{
    phase("Miniserver elérése / nyilvános kulcs");
    char url[192];snprintf(url,sizeof(url),"%s://%s:%u/jdev/sys/getPublicKey",cfg->tls?"https":"http",cfg->host,(unsigned)cfg->port);
    esp_http_client_config_t options={.url=url,.timeout_ms=LX_TIMEOUT_MS,
        .cert_pem=cfg->tls?cfg->ca_pem:NULL,.disable_auto_redirect=true};
    esp_http_client_handle_t http=esp_http_client_init(&options);if(!http)return failed("A HTTP kliens nem indítható (memóriahiba).",0);
    char *body=calloc(1,8193);int http_result=body?esp_http_client_open(http,0):ESP_ERR_NO_MEM;
    bool ok=http_result==ESP_OK;size_t used=0;
    if(!ok)failed("Nem épült fel a HTTP kapcsolat. Ellenőrizd az IP-címet és a portot.",http_result);
    if(ok){
        int64_t headers=esp_http_client_fetch_headers(http);int http_status=esp_http_client_get_status_code(http);
        ok=headers>=0&&http_status==200;
        if(!ok)failed("A HTTP kérés sikertelen. Ellenőrizd a portot és a WS/WSS módot.",headers<0?(int)headers:http_status);
    }
    while(ok&&used<8192) {
        int n=esp_http_client_read(http,body+used,8192-used);
        if(n<0){failed("A HTTP válasz olvasása megszakadt.",n);ok=false;break;}if(n==0)break;used+=n;
    }
    if(ok&&(used==8192||!esp_http_client_is_complete_data_received(http))){failed("Hiányos vagy túl nagy nyilvánoskulcs-válasz.",0);ok=false;}
    esp_http_client_close(http);esp_http_client_cleanup(http);
    cJSON *root=ok?cJSON_Parse(body):NULL;free(body);
    const char *key=string(cJSON_GetObjectItemCaseSensitive(root,"LL"),"value");
    s_public_key=*key?strdup(key):NULL;cJSON_Delete(root);
    if(!s_public_key&&ok)failed("A HTTP válasz nem tartalmaz Loxone nyilvános kulcsot.",0);
    return s_public_key!=NULL;
}
static bool connect_server(const lx_config_t *cfg)
{
    if(!fetch_public_key(cfg))return false;
    phase("Loxone WebSocket kapcsolat felépítése");
    char uri[192];snprintf(uri,sizeof(uri),"%s://%s:%u/ws/rfc6455",cfg->tls?"wss":"ws",cfg->host,(unsigned)cfg->port);
    esp_websocket_client_config_t ws={.uri=uri,.subprotocol="remotecontrol",.disable_auto_reconnect=true,
        .buffer_size=2048,.task_stack=5120,.network_timeout_ms=LX_TIMEOUT_MS,
        .cert_pem=cfg->tls?cfg->ca_pem:NULL};
    s_rx_failure=LX_RX_OK;s_dead=false;s_ws=esp_websocket_client_init(&ws);if(!s_ws)return failed("A WebSocket kliens nem indítható (memóriahiba).",0);
    if(esp_websocket_register_events(s_ws,WEBSOCKET_EVENT_ANY,receive_event,NULL)!=ESP_OK||esp_websocket_client_start(s_ws)!=ESP_OK)return failed("A WebSocket feladat nem indítható.",0);
    int64_t until=esp_timer_get_time()+LX_TIMEOUT_MS*1000LL;
    while(!s_dead&&!esp_websocket_client_is_connected(s_ws)&&esp_timer_get_time()<until)vTaskDelay(pdMS_TO_TICKS(50));
    if(s_dead||!esp_websocket_client_is_connected(s_ws))return failed("A WebSocket nem kapcsolódott 12 másodpercen belül.",0);
    return key_exchange();
}
#include "app_loxone_lighting_service.inc"

/* A kapu képe az aktuális Loxone metaadatból származik. Csak struktúra-
 * vagy profilváltáskor oldjuk fel, sohasem a 200 ms-os képernyőciklusban. */
static bool home_resolve_icons_locked(void)
{
    if (!s_home)
        return false;
    bool changed = false;
    for (unsigned i = 0; i < s_home->card_count; ++i)
    {
        lx_home_card_t *card = &s_home->cards[i];
        if (card->type == LX_HOME_CARD_SWITCH && !card->source.uuid[0]) {
            const cJSON *entry;
            cJSON_ArrayForEach(entry, s_catalog) {
                const char *uuid = string(entry, "uuid");
                if (!strcmp(card->control_uuid, string(entry, "control_uuid")) &&
                    !strcmp(string(entry, "state"), "active") && lx_uuid_valid(uuid)) {
                    snprintf(card->source.uuid, sizeof(card->source.uuid), "%s", uuid);
                    changed = true;
                    break;
                }
            }
        }
        if (card->type != LX_HOME_CARD_GATE)
            continue;
        const cJSON *entry = lx_controls_catalog_find(s_catalog, card->control_uuid, card->type);
        const cJSON *animation = cJSON_GetObjectItemCaseSensitive(entry, "gate_animation");
        int type = cJSON_IsNumber(animation) && animation->valuedouble >= 0 &&
            animation->valuedouble <= 5 && animation->valuedouble == floor(animation->valuedouble) ?
            (int)animation->valuedouble : -1;
        const char *icon = lx_controls_gate_icon(type);
        if (strcmp(card->icon, icon))
        {
            snprintf(card->icon, sizeof(card->icon), "%s", icon);
            changed = true;
        }
    }
    return changed;
}

static void invalidate(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    memset(s_energy, 0, sizeof(s_energy));
    memset(&s_night_values, 0, sizeof(s_night_values));
    s_home_live = false;
    app_loxone_audio_session(NULL, NULL, false);
    lighting_invalidate_locked();
    s_home_session++;
    memset(&s_home_values, 0, sizeof(s_home_values));
    if (s_command_pending)
        snprintf(s_command_status, sizeof(s_command_status), "%s",
                 "A kapcsolat megszakadt; a művelet eredménye nem igazolható.");
    s_command_pending = false;
    s_command_deadline = 0;
    home_command_t discarded;
    if (s_home_commands)
        while (xQueueReceive(s_home_commands, &discarded, 0) == pdTRUE) {}
    xSemaphoreGive(s_lock);
}
static void lighting_audio_recover(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    unsigned generation = s_light_generation, revision = s_home_revision;
    int page = s_light_page;
    bool live = s_home_live && s_home && s_catalog && !s_dead;
    xSemaphoreGive(s_lock);
    if (live)
        lighting_audio_rebuild(page, generation, revision);
}

static void audio_session_retry(const lx_config_t *active, const char *token, bool *pending)
{
    if (!*pending || s_dead || !app_loxone_audio_session(active->username, token, true))
        return;
    *pending = false;
    /* A bootkori Audio OOM nem állítja le a Miniservert. A sikeres késői
     * átadás egyszer visszajátssza a már felépített oldal kezdőállapotait. */
    lighting_audio_recover();
}

static void release_profile(panel_profile_t *profile)
{
    if (profile) {
        mbedtls_platform_zeroize(profile, sizeof(*profile));
        free(profile);
    }
}

static void worker(void *arg)
{
    (void)arg;
    char token[1024] = {0};
    unsigned failures = 0;
    for (;;) {
        panel_profile_t *next;
        if (xQueueReceive(s_requests, &next, s_profile ? 0 : portMAX_DELAY) == pdTRUE) {
            invalidate();
            xSemaphoreTake(s_lock, portMAX_DELAY);
            panel_profile_t *previous = s_profile;
            /* Presentation-only saves may reuse the current JWT; changing
             * server, TLS identity, user or password always discards it. */
            bool same_login = previous && previous->connection.port == next->connection.port &&
                previous->connection.tls == next->connection.tls &&
                !strcmp(previous->connection.host, next->connection.host) &&
                !strcmp(previous->connection.username, next->connection.username) &&
                !strcmp(previous->connection.password, next->connection.password) &&
                !strcmp(previous->connection.ca_pem, next->connection.ca_pem);
            s_profile = next;
            s_home = &next->home;
            s_light_page = atomic_load(&s_requested_page);
            s_home_busy = uxQueueMessagesWaiting(s_requests) != 0;
            s_home_revision++;
            s_intercom_generation++;
            s_intercom_ringing = false;
            cJSON_Delete(s_catalog);
            s_catalog = NULL;
            xSemaphoreGive(s_lock);
            release_profile(previous);
            if (!same_login) mbedtls_platform_zeroize(token, sizeof(token));
            failures = 0;
        }
        int requested = CATALOG_REQUESTED;
        atomic_compare_exchange_strong(&s_refresh, &requested, CATALOG_LOADING);
        const lx_config_t *active = &s_profile->connection;
        if (!active->host[0] || !active->username[0] || !active->password[0] || !s_network_ready()) {
            status("A hálózati kapcsolatra vagy a Miniserver beállításaira vár.");
            finish_refresh();
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }
        s_session_config = active;
        if(!connect_server(active)) {report_failure("A Miniserver nem érhető el, vagy a biztonságos kapcsolat nem épült fel.");goto retry;}
        status("Loxone bejelentkezés...");
        if(token[0]&&!token_command(active,token,"authwithtoken"))token[0]=0;
        if(!token[0]&&!acquire_token(active,token,sizeof(token))) {report_failure("A Loxone bejelentkezés sikertelen. Ellenőrizd a felhasználót, jelszót és jogosultságokat.");goto retry;}
        if(!download_catalog_with_fallback(active,token)) {report_failure("Az adatlista nem tölthető be, vagy meghaladja az eszköz korlátját.");goto retry;}

        cJSON *ack = reply("jdev/sps/enablebinstatusupdate",false);
        if (!ack) { status("Az állapotfrissítés nem engedélyezhető."); goto retry; }
        cJSON_Delete(ack);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_home_live = true;
        xSemaphoreGive(s_lock);
        bool has_audio = false;
        for (unsigned i=0; i<s_home->card_count; ++i)
            has_audio |= s_home->cards[i].type == LX_HOME_CARD_AUDIO;
        bool audio_pending = has_audio && !app_loxone_audio_session(active->username, token, true);
        status("Kapcsolódva a Miniserverhez.");
        finish_refresh();
        mark_response();
        int64_t heartbeat = s_last_response;
        failures = 0;
        while (!s_dead && s_network_ready() && !uxQueueMessagesWaiting(s_requests) &&
               atomic_load(&s_refresh) != CATALOG_REQUESTED) {
            message_t m;int64_t now=esp_timer_get_time();
            if(xQueueReceive(s_messages,&m,pdMS_TO_TICKS(100))==pdTRUE) {
                mark_response();
                if(!update_values(m.type,m.data,m.length))s_dead=true;
                free_message(&m);
            }
            /* A hangerő a Miniserver alapvezérlése. A külön Audioserver
             * kapcsolódása vagy hitelesítése nem késleltetheti a húzást. */
            audio_session_retry(active, token, &audio_pending);
            if (!s_dead) app_loxone_audio_poll(esp_timer_get_time());
            home_command_poll(now);
            if (!s_dead) lighting_poll(esp_timer_get_time());
            if (!s_dead) controls_poll(esp_timer_get_time());
            if(now-heartbeat>=LX_KEEPALIVE_US){if(!send_plain("keepalive"))break;heartbeat=now;}
            if(now-s_last_response>=LX_KEEPALIVE_US*3)break;
            if(now>=s_token_refresh_us) {
                /* A válaszban kapott új JWT lesz a következő kapcsolódás hitelesítője. */
                if(!renew_token(active,token))break;
                audio_pending = has_audio && !app_loxone_audio_session(active->username, token, true);
                status("Kapcsolódva. A token megújítva.");
            }

        }
        status("Megszakadt a Loxone adatkapcsolat. Újracsatlakozás...");
retry:
        finish_refresh();
        invalidate();
        close_connection();
        s_session_config = NULL;
        if (failures < 5) ++failures;
        unsigned delay = 1U << failures;
        if (delay > 30) delay = 30;
        for (unsigned i=0; i<delay*10 && !uxQueueMessagesWaiting(s_requests) &&
             atomic_load(&s_refresh) != CATALOG_REQUESTED; ++i)
            vTaskDelay(pdMS_TO_TICKS(100));
    }
}

esp_err_t zld_loxone_start(bool (*network_ready)(void))
{
    if (!network_ready) return ESP_ERR_INVALID_ARG;
    if (atomic_flag_test_and_set(&s_starting)) return ESP_ERR_INVALID_STATE;
    if (s_lock) { atomic_flag_clear(&s_starting); return ESP_OK; }
    SemaphoreHandle_t lock = xSemaphoreCreateMutex();
    s_messages = xQueueCreate(LX_QUEUE_DEPTH, sizeof(message_t));
    s_requests = xQueueCreate(1, sizeof(panel_profile_t *));
    s_home_commands = xQueueCreate(1, sizeof(home_command_t));
    s_network_ready = network_ready;
    if (lock && s_messages && s_requests && s_home_commands &&
        xTaskCreatePinnedToCore(worker, "loxone", 12288, NULL, 3, NULL, 0) == pdPASS) {
        atomic_store(&s_lock, lock);
        atomic_flag_clear(&s_starting);
        return ESP_OK;
    }
    if (lock) vSemaphoreDelete(lock);
    if (s_messages) vQueueDelete(s_messages);
    if (s_requests) vQueueDelete(s_requests);
    if (s_home_commands) vQueueDelete(s_home_commands);
    atomic_flag_clear(&s_starting);
    return ESP_ERR_NO_MEM;
}

esp_err_t zld_loxone_configure(const cJSON *panel, const char *password, const char *camera_password)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    panel_profile_t *next = heap_caps_calloc(1, sizeof(*next), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!next) return ESP_ERR_NO_MEM;
    if (!panel_profile_read(next, panel, password, camera_password)) {
        release_profile(next);
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool ok = xQueueSend(s_requests, &next, 0) == pdTRUE;
    if (ok) s_home_busy = true;
    xSemaphoreGive(s_lock);
    if (!ok) release_profile(next);
    return ok ? ESP_OK : ESP_ERR_TIMEOUT;
}

static bool update_values(int type, const uint8_t *data, size_t size)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool valid = lx_values_update(&s_night_values, s_session_config, type, data, size);
    for (unsigned i = 0; valid && i < s_home->page_count; ++i) {
        if (s_profile->energy[i].enabled)
            valid = lx_values_update_fields(&s_energy[i], s_profile->energy[i].fields, false,
                                            type, data, size);
    }
    if (!valid) { xSemaphoreGive(s_lock); return false; }
    int visible_page = s_light_page;
    unsigned visible_first, visible_count;
    if (visible_page >= 0 && !lx_home_page_range(s_home, (unsigned)visible_page, &visible_first, &visible_count))
        visible_page = -1;
    bool ok = !s_home || lx_home_values_update_visible(&s_home_values, s_home, visible_page, type, data, size);
    if (ok) ok = lighting_events_locked(type, data, size);
    if (ok && s_home)
    {
        bool was_ringing = s_intercom_ringing;
        ok = lx_intercom_bell_update(&s_intercom_ringing, &s_home->intercom, type, data, size);
        /* Felépült beszélgetés alatt a csengő éle nem cseréli le a hívást. */
        if (was_ringing != s_intercom_ringing && !intercom_held(s_intercom_generation))
            s_intercom_generation++;
    }
    xSemaphoreGive(s_lock);
    return ok;
}

bool app_loxone_home_snapshot(lx_home_config_t *config, lx_home_values_t *values,
                              unsigned *revision, bool *connected,
                              bool *command_pending, char message[LX_HOME_ERROR_SIZE])
{
    if (!s_lock || !s_home || !config || !values || !revision || !connected ||
        !command_pending || !message)
        return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *config = *s_home;
    /* A tartós GUI-pillanatképnek nincs szüksége a kamera hitelesítőjére. */
    mbedtls_platform_zeroize(config->intercom.video_password, sizeof(config->intercom.video_password));
    *values = s_home_values;
    char allowed_command[96];
    memset(values->command_supported, 0, sizeof(values->command_supported));
    unsigned first = 0, count = 0;
    if (s_light_page >= 0)
        (void)lx_home_page_range(s_home, (unsigned)s_light_page, &first, &count);
    for (unsigned i = first; i < first + count; ++i)
    {
        /* A típusos vezérlők saját summary-val döntenek az engedélyezésről.
         * Itt a régi On/Off parancs keresése csak újra bejárná a katalógust. */
        if (lx_home_card_is_controls(s_home->cards[i].type) ||
            s_home->cards[i].type == LX_HOME_CARD_AUDIO)
            continue;
        lx_home_command_t action = s_home->cards[i].type == LX_HOME_CARD_PULSE ? LX_HOME_COMMAND_PULSE :
            s_home->cards[i].type == LX_HOME_CARD_LIGHTING ? LX_HOME_COMMAND_LIGHT_NEXT :
            s_home->cards[i].type == LX_HOME_CARD_IRC ? LX_HOME_COMMAND_IRC_INCREASE :
            s_home->cards[i].type == LX_HOME_CARD_SHADING ? LX_HOME_COMMAND_SHADING_UP : LX_HOME_COMMAND_ON;
        values->command_supported[i] = lighting_home_command_allowed_locked(s_home->cards[i].id) &&
            lx_home_command_build(s_home, s_catalog, s_home->cards[i].id, action, allowed_command);
    }
    *revision = s_home_revision;
    *connected = s_home_live && !s_dead &&
                 esp_timer_get_time() - s_home_response < LX_KEEPALIVE_US * 3;
    *command_pending = s_command_pending;
    snprintf(message, LX_HOME_ERROR_SIZE, "%s", s_command_status);
    xSemaphoreGive(s_lock);
    return true;
}

/* Az abszolút pozíció külön, korlátos érték. A kártya szabad szöveget vagy
 * parancsvéget nem adhat át; a katalógus és az élő modell újraellenőrzött. */
static bool home_command_build_locked(const char *card_id, lx_home_command_t action,
                                      double position_percent, unsigned audio_edit_generation,
                                      char command[96])
{
    if (action == LX_HOME_COMMAND_SELECT_MOOD) {
        int slot = lighting_slot_locked(card_id);
        if (slot < 0 || position_percent < 0 || position_percent > UINT_MAX ||
            floor(position_percent) != position_percent) return false;
        lx_light_block_t *light = s_light_context->blocks[slot];
        if (!light || !lx_light_action_valid(light->action)) return false;
        for (unsigned i = 0; i < light->mood_count; ++i)
            if (light->moods[i].id == (unsigned)position_percent) {
                int n = snprintf(command, 96, "jdev/sps/io/%s/changeTo/%u", light->action, (unsigned)position_percent);
                return n > 0 && n < 96;
            }
        return false;
    }
    if (action >= LX_HOME_COMMAND_AUDIO_PLAY && action <= LX_HOME_COMMAND_AUDIO_VOLUME_DOWN)
    {
        int slot = lighting_slot_locked(card_id);
        if (slot < 0 || !isfinite(position_percent) || position_percent < 0 || position_percent > 100 ||
            position_percent != floor(position_percent))
            return false;
        const lx_home_card_t *card = &s_home->cards[s_light_context->first + (unsigned)slot];
        lx_audio_action_t audio_action = (lx_audio_action_t)(action - LX_HOME_COMMAND_AUDIO_PLAY);
        return card->type == LX_HOME_CARD_AUDIO &&
            app_loxone_audio_queued_allowed(card_id, s_home_revision, audio_action,
                                            (unsigned)position_percent, audio_edit_generation) &&
            lx_audio_command_build(lx_audio_catalog_find(s_catalog, card->control_uuid),
                                     audio_action, (unsigned)position_percent, command);
    }
    if (action >= LX_HOME_COMMAND_CONTROL_ON && action <= LX_HOME_COMMAND_CONTROL_SET)
    {
        int slot = lighting_slot_locked(card_id);
        if (slot < 0)
            return false;
        const lx_home_card_t *card = &s_home->cards[s_light_context->first + (unsigned)slot];
        if (card->type == LX_HOME_CARD_TIMED_SWITCH && action == LX_HOME_COMMAND_CONTROL_ON &&
            !card->allow_permanent)
            return false;
        return lx_controls_command(s_light_context->controls_blocks[slot],
            (lx_controls_action_t)(action - LX_HOME_COMMAND_CONTROL_ON), position_percent, command);
    }
    if (action != LX_HOME_COMMAND_SHADING_POSITION)
        return lx_home_command_build(s_home, s_catalog, card_id, action, command);
    int slot = lighting_slot_locked(card_id);
    if (slot < 0)
        return false;
    lx_shading_summary_t summary;
    lx_shading_summary(s_light_context->shading_blocks[slot], &summary);
    return summary.available && summary.controllable && !summary.central &&
           summary.position_valid && lx_home_shading_position_build(
               s_home, s_catalog, card_id, (unsigned)position_percent, command);
}

static esp_err_t home_command_request(const char *card_id, lx_home_command_t action,
                                       bool check_generation, unsigned generation,
                                       bool check_revision, unsigned revision,
                                       double position_percent, unsigned audio_edit_generation)
{
    if (!s_lock || !s_home || !s_home_commands || !card_id || strlen(card_id) >= LX_HOME_ID_SIZE)
        return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    char command[96];
    bool door = !strcmp(card_id, LX_INTERCOM_GATE_ID);
    bool allowed = lighting_home_command_allowed_locked(card_id) &&
                   (!check_generation || s_intercom_generation == generation) &&
                   (!check_revision || s_home_revision == revision) &&
                   (!door || s_intercom_ringing || intercom_held(s_intercom_generation)) &&
                   s_home_live && !s_dead && !s_command_pending && !s_home_busy &&
                   esp_timer_get_time() - s_home_response < LX_KEEPALIVE_US * 3 &&
                   home_command_build_locked(card_id, action, position_percent, audio_edit_generation, command);
    for (unsigned i = 0; allowed && i < s_home->card_count; i++)
    {
        if (!strcmp(s_home->cards[i].id, card_id) && s_home->cards[i].type == LX_HOME_CARD_SWITCH)
            allowed = s_home_values.cards[i].valid &&
                      s_home_values.cards[i].value == (action == LX_HOME_COMMAND_ON ? 0 : 1);
    }
    home_command_t request = {
        .action = action, .revision = s_home_revision, .session = s_home_session,
        .position_percent = position_percent, .queued_us = esp_timer_get_time(),
        .audio_edit_generation = audio_edit_generation
    };
    request.intercom_revision = s_intercom_generation;
    request.visibility_revision = s_light_generation;
    snprintf(request.id, sizeof(request.id), "%s", card_id);
    allowed = allowed && xQueueSend(s_home_commands, &request, 0) == pdTRUE;
    if (allowed)
    {
        /* A Be/Ki felülírja az ugyanahhoz a dimmerhez várakozó húzást.
         * Régi csúszkaérték nem kapcsolhatja vissza a kézzel kikapcsolt fényt. */
        int slot = lighting_slot_locked(card_id);
        if (slot >= 0 && (action == LX_HOME_COMMAND_CONTROL_ON || action == LX_HOME_COMMAND_CONTROL_OFF) &&
            s_light_context->controls_blocks[slot])
            s_light_context->controls_blocks[slot]->dirty = false;
        s_command_pending = true;
        s_command_deadline = 0;
        snprintf(s_command_status, sizeof(s_command_status), "%s", "Kérés küldése...");
    }
    xSemaphoreGive(s_lock);
    /* A helyben elfogadott kapunyitás indítja a bontást: a Miniserver
     * nyugtája késhet vagy elmaradhat, a hang akkor sem maradhat nyitva. */
    if (allowed && door && action == LX_HOME_COMMAND_PULSE) {
        app_intercom_audio_gate_opened(request.intercom_revision);
    }
    return allowed ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t app_loxone_home_command(const char *card_id, unsigned revision, lx_home_command_t action)
{
    /* Pozíciót csak a külön, revízióval és értékkel ellátott API fogadhat. */
    if (action >= LX_HOME_COMMAND_SHADING_POSITION)
        return ESP_ERR_INVALID_ARG;
    return home_command_request(card_id, action, false, 0, true, revision, 0, 0);
}

esp_err_t app_loxone_shading_command(const char *card_id, unsigned revision,
                                    lx_home_command_t action)
{
    if (action != LX_HOME_COMMAND_SHADING_UP && action != LX_HOME_COMMAND_SHADING_DOWN &&
        action != LX_HOME_COMMAND_SHADING_STOP)
        return ESP_ERR_INVALID_ARG;
    return home_command_request(card_id, action, false, 0, true, revision, 0, 0);
}

esp_err_t app_loxone_shading_position(const char *card_id, unsigned revision,
                                     unsigned percent)
{
    if (percent > 100)
        return ESP_ERR_INVALID_ARG;
    return home_command_request(card_id, LX_HOME_COMMAND_SHADING_POSITION,
                                 false, 0, true, revision, percent, 0);
}

esp_err_t app_loxone_audio_transport(const char *id, unsigned revision,
                                    lx_audio_action_t action, unsigned value, unsigned edit_generation)
{
    if (action < LX_AUDIO_PLAY || action >= LX_AUDIO_FAVORITE || value > 100)
        return ESP_ERR_INVALID_ARG;
    return home_command_request(id, (lx_home_command_t)(LX_HOME_COMMAND_AUDIO_PLAY + action),
                                 false, 0, true, revision, value, edit_generation);
}

esp_err_t app_loxone_controls_command(const char *card_id, unsigned revision,
                                      lx_controls_action_t action)
{
    if (action < LX_CONTROLS_ON || action >= LX_CONTROLS_SET)
        return ESP_ERR_INVALID_ARG;
    return home_command_request(card_id,
        (lx_home_command_t)(LX_HOME_COMMAND_CONTROL_ON + action), false, 0, true, revision, 0, 0);
}

esp_err_t app_loxone_controls_set(const char *card_id, unsigned revision, double value)
{
    if (!s_lock || !card_id || !isfinite(value))
        return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int slot = revision == s_home_revision ? lighting_slot_locked(card_id) : -1;
    lx_controls_block_t *block = slot >= 0 ? s_light_context->controls_blocks[slot] : NULL;
    bool dimmer = block && block->type == LX_HOME_CARD_DIMMER;
    bool queued = dimmer && lighting_live_locked() && s_light_initial_ready &&
                  lx_controls_queue(block, value, esp_timer_get_time());
    xSemaphoreGive(s_lock);
    if (dimmer)
        return queued ? ESP_OK : ESP_ERR_INVALID_STATE;
    return home_command_request(card_id, LX_HOME_COMMAND_CONTROL_SET,
                                 false, 0, true, revision, value, 0);
}

/* A Slave régi gombnyomása nem használhat közben megváltozott kapunyitó
 * beállítást. A generáció és a sorba helyezés ugyanazon Loxone-zár alatt áll. */
esp_err_t app_loxone_intercom_gate_request(unsigned revision)
{
    return home_command_request(LX_INTERCOM_GATE_ID, LX_HOME_COMMAND_PULSE,
                                 true, revision, false, 0, 0, 0);
}

/* Kizárólag a Loxone-worker küldhet a WebSocketre. A kivett parancsot soha
 * nem tesszük vissza a sorba, akkor sem, ha az eredmény a hálózati hiba miatt bizonytalan. */
static void home_command_poll(int64_t now)
{
    home_command_t request;
    if (s_home_commands && xQueueReceive(s_home_commands, &request, 0) == pdTRUE)
    {
        char command[96];
        xSemaphoreTake(s_lock, portMAX_DELAY);
        /* A kérés az üzenetfogadás várakozása közben is érkezhetett.
         * A ciklus eleji idő ilyenkor régebbi a gombnyomásnál. */
        now = esp_timer_get_time();
        bool door = !strcmp(request.id, LX_INTERCOM_GATE_ID);
        bool valid = (!door || ((s_intercom_ringing || intercom_held(s_intercom_generation)) &&
                              request.intercom_revision == s_intercom_generation)) &&
                     lighting_live_locked() && request.revision == s_home_revision &&
                     request.session == s_home_session &&
                     (door || request.visibility_revision == s_light_generation) &&
                     lighting_home_command_allowed_locked(request.id) && now >= request.queued_us &&
                     now - request.queued_us <= 2000000LL &&
                     home_command_build_locked(request.id, request.action,
                                                request.position_percent, request.audio_edit_generation, command);
        xSemaphoreGive(s_lock);
        cJSON *answer = valid ? reply(command, true) : NULL;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (request.revision != s_home_revision || request.session != s_home_session ||
            (!door && request.visibility_revision != s_light_generation))
        {
            /* A nyugta közben elhagyott lap nem tarthat fenn visszajelzésre
             * várást. A már elküldött kérés nem vonható vissza. */
            s_command_pending = false;
            s_command_deadline = 0;
            s_command_status[0] = 0;
        }
        else if (!answer)
        {
            s_command_pending = false;
            snprintf(s_command_status, sizeof(s_command_status), "%s",
                     "A művelet nem igazolható. Ellenőrizd a Loxone állapotát.");
        }
        else if (request.action == LX_HOME_COMMAND_IRC_DECREASE ||
                 request.action == LX_HOME_COMMAND_IRC_INCREASE)
        {
            /* A nyugta csak a léptetési kérés elfogadását jelzi. Célhőfokot
             * kizárólag a Miniserver állapotfolyama írhat; nincs ismétlés. */
            s_command_pending = false;
            s_command_deadline = 0;
            snprintf(s_command_status, sizeof(s_command_status), "%s", "Hőfokléptetés elküldve.");
        }
        else if (request.action >= LX_HOME_COMMAND_SHADING_UP &&
                 request.action <= LX_HOME_COMMAND_SHADING_POSITION)
        {
            /* A nyugta után a Stop ismét használható; nem várjuk meg a
             * teljes mozgás végét. Pozíciót kizárólag a valódi delta írhat. */
            s_command_pending = false;
            s_command_deadline = 0;
            snprintf(s_command_status, sizeof(s_command_status), "%s", "Redőnyparancs elküldve.");
        }
        else if (request.action >= LX_HOME_COMMAND_CONTROL_ON &&
                 request.action <= LX_HOME_COMMAND_AUDIO_VOLUME_DOWN)
        {
            /* A kérés nyugtája nem kész mozgás vagy új mért érték. A Stop
             * már a nyugta után elérhető; nincs bizonytalan kérésismétlés. */
            s_command_pending = false;
            s_command_deadline = 0;
            snprintf(s_command_status, sizeof(s_command_status), "%s", "Vezérlési kérés elküldve.");
        }
        else if (request.action == LX_HOME_COMMAND_SELECT_MOOD || request.action == LX_HOME_COMMAND_PULSE ||
                 (request.action >= LX_HOME_COMMAND_LIGHT_PREV &&
                  request.action <= LX_HOME_COMMAND_LIGHT_OFF))
        {
            s_command_pending = false;
            snprintf(s_command_status, sizeof(s_command_status), "%s", "Impulzus elküldve.");
        }
        else
        {
            s_pending_command = request;
            s_command_deadline = esp_timer_get_time() + 5000000LL;
            snprintf(s_command_status, sizeof(s_command_status), "%s", "Állapot-visszajelzésre várunk...");
        }
        xSemaphoreGive(s_lock);
        cJSON_Delete(answer);
    }
    now = esp_timer_get_time();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_command_pending && s_command_deadline)
    {
        bool hidden = s_pending_command.visibility_revision != s_light_generation;
        bool confirmed = false;
        for (unsigned i = 0; i < s_home->card_count; i++)
            if (!strcmp(s_home->cards[i].id, s_pending_command.id) && s_home_values.cards[i].valid &&
                s_home_values.cards[i].value == (s_pending_command.action == LX_HOME_COMMAND_ON ? 1 : 0))
                confirmed = true;
        if (hidden || confirmed || now >= s_command_deadline)
        {
            s_command_pending = false;
            s_command_deadline = 0;
            snprintf(s_command_status, sizeof(s_command_status), "%s", confirmed ?
                     "Loxone-állapot visszaigazolva." : "Nem érkezett megfelelő Loxone-állapot.");
            if (hidden) s_command_status[0] = 0;
        }
    }
    xSemaphoreGive(s_lock);
}

/* Kis pillanatkép, a teljes Otthon-katalógus másolása és hálózati várakozás nélkül. */
bool app_loxone_intercom_call_permitted(unsigned revision, bool active)
{
    if (!s_lock || !s_home) return false;
    if (xSemaphoreTake(s_lock, 0) != pdTRUE) return true;
    bool allowed = s_home->intercom.enabled && s_home_live && !s_dead &&
                   esp_timer_get_time() - s_home_response < LX_KEEPALIVE_US * 3 &&
                   s_intercom_generation == revision && (s_intercom_ringing || active);
    xSemaphoreGive(s_lock);
    return allowed;
}

bool app_loxone_intercom_snapshot(app_intercom_snapshot_t *out)
{
    if (!out || !s_lock || !s_home)
        return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    out->config = s_home->intercom;
    mbedtls_platform_zeroize(out->config.video_password, sizeof(out->config.video_password));
    out->ringing = s_intercom_ringing && out->config.enabled;
    out->connected = s_home_live && !s_dead &&
        esp_timer_get_time() - s_home_response < LX_KEEPALIVE_US * 3;
    out->revision = s_intercom_generation;
    out->audio_active = out->config.enabled && out->connected && app_intercom_audio_active_for(out->revision);
    out->audio_elsewhere = false;
    out->ringtone_silenced = out->config.enabled && panel_door_silenced(out->revision);
    char command[96];
    out->gate_supported = lx_home_command_build(s_home, s_catalog, LX_INTERCOM_GATE_ID,
                                                LX_HOME_COMMAND_PULSE, command);
    out->command_pending = s_command_pending;
    snprintf(out->status, sizeof(out->status), "%s", s_command_status);
    xSemaphoreGive(s_lock);
    return true;
}

bool app_loxone_intercom_credentials(unsigned revision, char username[96], char password[LX_PASSWORD_MAX])
{
    if (!s_lock || !s_home || !username || !password)
        return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (revision != s_intercom_generation) {
        xSemaphoreGive(s_lock);
        return false;
    }
    snprintf(username, 96, "%s", s_home->intercom.video_username);
    snprintf(password, LX_PASSWORD_MAX, "%s", s_home->intercom.video_password);
    bool valid = username[0] && password[0];
    xSemaphoreGive(s_lock);
    return valid;
}


unsigned zld_loxone_revision(unsigned panel_generation)
{
    if (!s_lock) return 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    unsigned revision = s_profile && s_profile->generation == panel_generation ? s_home_revision : 0;
    xSemaphoreGive(s_lock);
    return revision;
}

bool zld_loxone_status(char *out, size_t size)
{
    if (!s_lock) {
        if (size && out) snprintf(out, size, "%s", "A Loxone kliens indul.");
        return false;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (size && out) snprintf(out, size, "%s", s_status);
    bool connected = lighting_live_locked();
    xSemaphoreGive(s_lock);
    return connected;
}

cJSON *zld_loxone_catalog(void)
{
    cJSON *result = cJSON_CreateArray();
    if (!s_lock || !result) return result;
    /* Only the editor's display fields are exported. Native metadata and
     * potentially large nested block snapshots remain in the worker. */
    static const char *const keys[] = {"uuid", "control_uuid", "name", "type", "state",
                                        "room", "category", "action_uuid", "secured", "lighting_blocked", "irc_blocked",
                                        "shading_blocked", "controls_type", "controls_blocked"};
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const cJSON *row;
    cJSON_ArrayForEach(row, s_catalog) {
        cJSON *entry = cJSON_CreateObject();
        if (!entry || !cJSON_AddItemToArray(result, entry)) {
            cJSON_Delete(entry);
            cJSON_Delete(result);
            result = NULL;
            break;
        }
        for (unsigned i=0; i<sizeof(keys)/sizeof(keys[0]); ++i) {
            const cJSON *field = cJSON_GetObjectItemCaseSensitive(row, keys[i]);
            if (!field) continue;
            cJSON *copy = cJSON_Duplicate(field, false);
            if (!copy || !cJSON_AddItemToObject(entry, keys[i], copy)) {
                cJSON_Delete(copy);
                cJSON_Delete(result);
                result = NULL;
                break;
            }
        }
        if (!result) break;
    }
    xSemaphoreGive(s_lock);
    return result;
}

bool zld_loxone_refreshing(void)
{
    return atomic_load(&s_refresh) != CATALOG_IDLE;
}

esp_err_t zld_loxone_refresh(void)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool ready = s_profile && s_profile->connection.host[0] &&
                 s_profile->connection.username[0] && s_profile->connection.password[0];
    int idle = CATALOG_IDLE;
    if (ready) atomic_compare_exchange_strong(&s_refresh, &idle, CATALOG_REQUESTED);
    xSemaphoreGive(s_lock);
    return ready ? ESP_OK : ESP_ERR_INVALID_STATE;
}

bool zld_loxone_energy_at(unsigned tab, unsigned generation, lx_values_t *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!s_lock || tab >= LX_HOME_MAX_PAGES) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool ok = s_profile && (!generation || s_profile->generation == generation) &&
              s_profile->energy[tab].enabled && lighting_live_locked();
    if (ok) *out = s_energy[tab];
    xSemaphoreGive(s_lock);
    return ok;
}

bool zld_loxone_energy(unsigned tab, lx_values_t *out)
{
    return zld_loxone_energy_at(tab, 0, out);
}

bool zld_loxone_night(bool *night)
{
    if (!s_lock || !night) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool valid = lighting_live_locked() && s_night_values.seen[6] &&
                 (s_night_values.value[6] == 0 || s_night_values.value[6] == 1);
    if (valid) *night = s_night_values.value[6] == 1;
    xSemaphoreGive(s_lock);
    return valid;
}

bool zld_loxone_card(const char *id, lx_home_value_t *out, bool *supported)
{
    if (!id || !out || !supported) return false;
    *out = (lx_home_value_t){0};
    *supported = false;
    if (!s_lock) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int slot = lighting_slot_locked(id);
    bool valid = slot >= 0 && lighting_live_locked();
    if (valid) {
        unsigned i = s_light_context->first + slot;
        *out = s_home_values.cards[i];
        lx_home_command_t action = s_home->cards[i].type == LX_HOME_CARD_PULSE ? LX_HOME_COMMAND_PULSE : LX_HOME_COMMAND_ON;
        char command[96];
        *supported = !s_command_pending && lighting_home_command_allowed_locked(id) &&
                     lx_home_command_build(s_home, s_catalog, id, action, command);
    }
    xSemaphoreGive(s_lock);
    return valid;
}

bool zld_loxone_mood(const char *id, unsigned revision, unsigned index, lx_light_mood_t *out, bool *active)
{
    if (!id || !out || !active || !s_lock) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int slot = revision == s_home_revision && lighting_live_locked() ? lighting_slot_locked(id) : -1;
    lx_light_block_t *block = slot < 0 ? NULL : s_light_context->blocks[slot];
    bool valid = block && index < block->mood_count;
    if (valid) {
        *out = block->moods[index];
        *active = false;
        for (unsigned i=0; i<block->active_count; ++i)
            if (block->active_ids[i] == out->id) { *active = true; break; }
    }
    xSemaphoreGive(s_lock);
    return valid;
}

esp_err_t zld_loxone_select_mood(const char *id, unsigned revision, unsigned mood_id)
{
    return home_command_request(id, LX_HOME_COMMAND_SELECT_MOOD, false, 0, true, revision, mood_id, 0);
}

static atomic_bool s_pause_media;
void zld_loxone_pause_media(bool paused) { atomic_store(&s_pause_media, paused); }
bool zld_media_paused(void) { return atomic_load(&s_pause_media); }
