#include "panel_config.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define GET(o,k) cJSON_GetObjectItemCaseSensitive(o,k)
static char detail[192];
static esp_err_t save(cJSON *doc){return panel_config_save(doc,detail,sizeof(detail));}
static void secret_is(const char *key,const char *value) {
    char secret[PANEL_SECRET_CAP];
    assert(panel_config_read_secret(key,secret,sizeof(secret))==ESP_OK);
    assert(strcmp(value,secret)==0);
}
static void initial_save(void){
    assert(panel_config_init()==ESP_OK && panel_config_generation()==1);
    cJSON *d=panel_config_snapshot(),*old=cJSON_Duplicate(d,true);
    assert(strcmp(cJSON_GetStringValue(GET(GET(d,"intercom"),"ringtone")),"tubular-chime.wav")==0);
    cJSON_SetValuestring(GET(d,"adminUser"),"tester");
    cJSON_AddStringToObject(d,"adminPassword","secret-admin");
    cJSON_SetValuestring(GET(GET(d,"loxone"),"host"),"miniserver.local");
    cJSON_SetValuestring(GET(GET(d,"loxone"),"user"),"panel");
    cJSON_AddStringToObject(d,"loxonePassword","secret-connection");
    assert(save(d)==ESP_OK && panel_config_generation()==2);
    assert(save(old)==ESP_ERR_INVALID_STATE);
    cJSON_Delete(d);cJSON_Delete(old);
    d=panel_config_snapshot();
    assert(!GET(d,"credentials") && !GET(d,"loxonePassword") && !GET(d,"adminPassword"));
    cJSON_SetNumberValue(GET(GET(d,"display"),"saverSeconds"),120);
    assert(save(d)==ESP_OK && panel_config_generation()==3);
    cJSON_Delete(d);
    d=panel_config_snapshot();
    cJSON_AddStringToObject(GET(d,"intercom"),"cameraPassword","unsafe");
    assert(save(d)==ESP_ERR_INVALID_ARG && panel_config_generation()==3);
    cJSON_Delete(d);
    d=panel_config_snapshot();
    cJSON *tab=cJSON_Parse("{\"kind\":\"controls\",\"name\":\"Nappali\",\"icon\":\"light\",\"background\":{\"type\":\"color\",\"value\":\"#112233\"},\"blocks\":[{\"id\":\"b-1\",\"name\":\"Lámpa\",\"type\":\"switch\",\"uuid\":\"12345678-1234-1234-1234567890abcdef\",\"stateUuid\":\"\",\"unit\":\"\"}]}");
    assert(cJSON_AddItemToArray(GET(d,"tabs"),tab));
    assert(mkdir("panel.json.tmp",0700)==0);
    assert(save(d)!=ESP_OK && panel_config_generation()==3);
    assert(rmdir("panel.json.tmp")==0 && save(d)==ESP_OK && panel_config_generation()==4);
    cJSON_Delete(d);
    secret_is("adminPassword","secret-admin");secret_is("loxonePassword","secret-connection");
    const char nested[]="[[[[[[[[[[[[[0]]]]]]]]]]]]]";
    assert(!panel_parse_document(nested,strlen(nested)));
    const char nul[]="{\"adminUser\":\"x\\u0000y\"}";
    assert(!panel_parse_document(nul,strlen(nul)));
}
static void reload(void){
    assert(panel_config_init()==ESP_OK && panel_config_generation()==4);
    secret_is("adminPassword","secret-admin");secret_is("loxonePassword","secret-connection");
    cJSON *d=panel_config_snapshot();
    assert(cJSON_GetArraySize(GET(d,"tabs"))==1);
    cJSON_SetValuestring(GET(GET(d,"loxone"),"host"),"other.local");
    assert(save(d)==ESP_ERR_INVALID_ARG && panel_config_generation()==4);
    secret_is("loxonePassword","secret-connection");
    cJSON_AddStringToObject(d,"loxonePassword","other-secret");
    assert(save(d)==ESP_OK && panel_config_generation()==5);
    secret_is("loxonePassword","other-secret");secret_is("adminPassword","secret-admin");
    cJSON_Delete(d);
}
static void restore_backup(void){
    assert(panel_config_init()==ESP_OK && panel_config_generation()==4);
    secret_is("loxonePassword","secret-connection");
    cJSON *d=panel_config_snapshot();
    assert(save(d)==ESP_OK && panel_config_generation()==5);
    cJSON_Delete(d);
}
int main(int argc,char **argv){
    assert(argc==2);
    if(!strcmp(argv[1],"init")) initial_save();
    else if(!strcmp(argv[1],"reload")) reload();
    else if(!strcmp(argv[1],"recover")) restore_backup();
    else if(!strcmp(argv[1],"fail-closed")) {
        char u[64],p[PANEL_SECRET_CAP];
        assert(panel_config_init()==ESP_ERR_INVALID_STATE && !panel_config_web_login(u,sizeof(u),p));
    }else return 1;
    return 0;
}
