/* Exercise the same persisted profile, public snapshot and revision checks as the web editor. */
#include "panel_config.h"
#include "panel_schema.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define GET(o,k) cJSON_GetObjectItemCaseSensitive(o,k)
static const char *const target="12345678-1234-1234-1234567890abcdef";
static const char *const m_plus="87654321-1234-1234-1234567890abcdef";
static const char *const default_ink="#8B8C8E";
static const char *const selected_ink="#3bC892";
static const char *str(const cJSON *o,const char *key) { return cJSON_GetStringValue(GET(o,key)); }

static void assert_empty(const cJSON *touch) {
    assert(cJSON_IsFalse(GET(touch,"enabled")));
    assert(!strcmp(str(touch,"dayTextColor"),default_ink));
    assert(cJSON_GetNumberValue(GET(touch,"tapWindowMs"))==400);
    assert(!*str(GET(touch,"center"),"uuid"));
    assert(cJSON_GetArraySize(GET(touch,"corners"))==4);
    const cJSON *c;
    cJSON_ArrayForEach(c,GET(touch,"corners")) {
        assert(!*str(c,"type") && !*str(c,"action") && !*str(c,"uuid"));
        assert(cJSON_GetNumberValue(GET(c,"sceneId"))==0);
    }
}
static void expect_rejected(cJSON *p) {
    uint32_t rev=panel_config_generation();
    char error[256]="";
    assert(panel_config_save(p,error,sizeof(error))==ESP_ERR_INVALID_ARG);
    assert(*error && rev==panel_config_generation());
}
static void day_colors(cJSON *p) {
    cJSON *touch=GET(p,"touch"), *ink=GET(touch,"dayTextColor");
    const char *invalid[]={"", "#fff", "#11223344", "#12z456", "123456", "red"};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);++i) {
        cJSON_SetValuestring(ink,invalid[i]);
        expect_rejected(p);
    }
    assert(cJSON_ReplaceItemInObjectCaseSensitive(touch,"dayTextColor",cJSON_CreateNull()));
    expect_rejected(p);
    assert(cJSON_ReplaceItemInObjectCaseSensitive(touch,"dayTextColor",cJSON_CreateNumber(0xffffff)));
    expect_rejected(p);
    assert(cJSON_ReplaceItemInObjectCaseSensitive(touch,"dayTextColor",cJSON_CreateString("#000000")));
    assert(panel_schema_validate(p,NULL,0));
    cJSON_SetValuestring(GET(touch,"dayTextColor"),"#FFFFFF");
    assert(panel_schema_validate(p,NULL,0));
    cJSON_SetValuestring(GET(touch,"dayTextColor"),selected_ink);
}
static void allowed_actions(cJSON *p) {
    cJSON *touch=GET(p,"touch"), *corner=cJSON_GetArrayItem(GET(touch,"corners"),0);
    static const char *actions[][2]={
        {"pulse","trigger"},{"switch","trigger"},{"shading","up"},{"shading","down"},
        {"audio","volumeUp"},{"audio","volumeDown"},{"audio","next"},{"audio","previous"},
        {"audio","playPause"},{"lighting","next"},{"lighting","previous"},{"lighting","scene"}
    };
    cJSON_SetValuestring(GET(corner,"uuid"),target);
    for(unsigned i=0;i<sizeof(actions)/sizeof(*actions);++i) {
        cJSON_SetValuestring(GET(corner,"type"),actions[i][0]);
        cJSON_SetValuestring(GET(corner,"action"),actions[i][1]);
        assert(panel_schema_validate(p,NULL,0));
    }
    cJSON_SetValuestring(GET(corner,"type"),"switch");
    cJSON_SetValuestring(GET(corner,"action"),"trigger");
    cJSON_AddStringToObject(corner,"inputUuid",target);expect_rejected(p);
    cJSON_SetValuestring(GET(corner,"inputUuid"),m_plus);
    assert(panel_schema_validate(p,NULL,0));
    cJSON_SetValuestring(GET(corner,"type"),"pulse");expect_rejected(p);
    cJSON_SetValuestring(GET(corner,"inputUuid"),"");assert(panel_schema_validate(p,NULL,0));
    cJSON_DeleteItemFromObjectCaseSensitive(corner,"inputUuid");
    cJSON_SetValuestring(GET(corner,"type"),"lighting");
    cJSON_SetValuestring(GET(corner,"action"),"scene");
    cJSON *switch_corner=cJSON_GetArrayItem(GET(touch,"corners"),1);
    cJSON_SetValuestring(GET(switch_corner,"type"),"switch");
    cJSON_SetValuestring(GET(switch_corner,"action"),"trigger");
    cJSON_SetValuestring(GET(switch_corner,"uuid"),target);
    cJSON_AddStringToObject(switch_corner,"inputUuid",m_plus);
    cJSON_SetNumberValue(GET(corner,"sceneId"),2147483647);
    assert(panel_schema_validate(p,NULL,0));
    cJSON_SetNumberValue(GET(corner,"sceneId"),2147483648.0);
    expect_rejected(p);
    cJSON_SetNumberValue(GET(corner,"sceneId"),1.5);expect_rejected(p);
    cJSON_SetNumberValue(GET(corner,"sceneId"),7);
    cJSON_SetValuestring(GET(corner,"action"),"next");expect_rejected(p);
    cJSON_SetNumberValue(GET(corner,"sceneId"),0);
    cJSON_SetValuestring(GET(corner,"action"),"trigger");expect_rejected(p);
    cJSON_SetValuestring(GET(corner,"action"),"scene");
    cJSON_SetValuestring(GET(corner,"uuid"),"bad");expect_rejected(p);
    cJSON *center=GET(touch,"center");
    cJSON_SetValuestring(GET(corner,"uuid"),target);cJSON_SetNumberValue(GET(corner,"sceneId"),104);
    cJSON_SetValuestring(GET(center,"inputUuid"),m_plus);expect_rejected(p);
    cJSON_SetValuestring(GET(center,"uuid"),m_plus);expect_rejected(p);
    cJSON_SetValuestring(GET(center,"uuid"),target);
    assert(panel_schema_validate(p,NULL,0));
    cJSON_DeleteItemFromObjectCaseSensitive(center,"inputUuid");
    assert(panel_schema_validate(p,NULL,0));
    cJSON_AddStringToObject(center,"inputUuid",m_plus);
    const int allowed[]={150,400,1000};
    for(unsigned i=0;i<3;i++) {
        cJSON_SetNumberValue(GET(touch,"tapWindowMs"),allowed[i]);
        assert(panel_schema_validate(p,NULL,0));
    }
    const double invalid[]={149,1001,399.5};
    for(unsigned i=0;i<3;i++) {
        cJSON_SetNumberValue(GET(touch,"tapWindowMs"),invalid[i]);
        expect_rejected(p);
    }
    cJSON_SetNumberValue(GET(touch,"tapWindowMs"),375);
    cJSON_AddStringToObject(touch,"surprise","");expect_rejected(p);
    cJSON_DeleteItemFromObjectCaseSensitive(touch,"surprise");
    cJSON *list=GET(touch,"corners"),*last=cJSON_DetachItemFromArray(list,3);
    expect_rejected(p);cJSON_AddItemToArray(list,last);
}
static void assert_configured(cJSON *p,const char *expected_ink) {
    const cJSON *t=GET(p,"touch"),*c=cJSON_GetArrayItem(GET(t,"corners"),0);
    assert(cJSON_IsTrue(GET(t,"enabled")) && GET(t,"tapWindowMs")->valueint==375);
    assert(!strcmp(str(t,"dayTextColor"),expected_ink));
    assert(!strcmp(str(c,"type"),"lighting") && !strcmp(str(c,"action"),"scene"));
    assert(GET(c,"sceneId")->valueint==104 && !strcmp(str(c,"uuid"),target));
    const cJSON *sw=cJSON_GetArrayItem(GET(t,"corners"),1);
    assert(!strcmp(str(sw,"type"),"switch") && !strcmp(str(sw,"inputUuid"),m_plus));
    assert(!strcmp(str(GET(t,"center"),"uuid"),target) && !strcmp(str(GET(t,"center"),"inputUuid"),m_plus));
}
/* Same on-disk envelope as older firmware. The caller retains its snapshot. */
static void write_fixture(cJSON *p) {
    cJSON *root=cJSON_CreateObject();
    assert(cJSON_AddItemReferenceToObject(root,"profile",p));
    cJSON_AddObjectToObject(root,"credentials");
    char *body=cJSON_PrintUnformatted(root);FILE *f=fopen("panel.json","w");
    assert(body && f && fputs(body,f)>=0 && !fclose(f));
    cJSON_free(body);cJSON_Delete(root);
}
void panel_test_touch(const char *stage) {
    assert(panel_config_init()==ESP_OK);
    cJSON *p=panel_config_snapshot();
    char msg[256];
    if(!strcmp(stage,"touch-old-file")) {
        assert_empty(GET(p,"touch"));
        cJSON_DeleteItemFromObjectCaseSensitive(p,"touch");
        write_fixture(p);
    } else if(!strcmp(stage,"touch-migrate")) {
        assert(panel_config_generation()==1);assert_empty(GET(p,"touch"));
        assert(cJSON_ReplaceItemInObjectCaseSensitive(GET(p,"touch"),"enabled",cJSON_CreateTrue()));
        assert(panel_schema_validate(p,NULL,0)); /* optional even in Touch mode */
        day_colors(p);
        allowed_actions(p);
        cJSON *stale=cJSON_Duplicate(p,true);
        assert(panel_config_save(p,msg,sizeof(msg))==ESP_OK && panel_config_generation()==2);
        assert(panel_config_save(stale,msg,sizeof(msg))==ESP_ERR_INVALID_STATE);
        cJSON_Delete(stale);
    } else if(!strcmp(stage,"touch-legacy-client")) {
        assert_configured(p,selected_ink);
        cJSON_DeleteItemFromObjectCaseSensitive(p,"touch");
        cJSON_SetNumberValue(GET(GET(p,"display"),"dayBrightness"),64);
        assert(panel_config_save(p,msg,sizeof(msg))==ESP_OK && panel_config_generation()==3);
        cJSON *live=panel_config_snapshot();assert_configured(live,selected_ink);cJSON_Delete(live);
    } else if(!strcmp(stage,"touch-reload")) {
        assert(panel_config_generation()==3);assert_configured(p,selected_ink);
        assert(cJSON_GetNumberValue(GET(GET(p,"display"),"dayBrightness"))==64);
        /* A client from the previous Touch release knows the zones but omits the color. */
        cJSON_DeleteItemFromObjectCaseSensitive(GET(p,"touch"),"dayTextColor");
        assert(panel_config_save(p,msg,sizeof(msg))==ESP_OK && panel_config_generation()==4);
        assert(!GET(GET(p,"touch"),"dayTextColor"));
        cJSON *live=panel_config_snapshot();assert_configured(live,selected_ink);cJSON_Delete(live);
        assert(cJSON_ReplaceItemInObjectCaseSensitive(p,"touch",cJSON_CreateNull()));expect_rejected(p);
    } else if(!strcmp(stage,"touch-old-color-file")) {
        assert(panel_config_generation()==4);assert_configured(p,selected_ink);
        cJSON_DeleteItemFromObjectCaseSensitive(GET(p,"touch"),"dayTextColor");
        write_fixture(p);
    } else {
        assert(!strcmp(stage,"touch-color-migrate") && panel_config_generation()==4);
        assert_configured(p,default_ink);
    }
    cJSON_Delete(p);
}
