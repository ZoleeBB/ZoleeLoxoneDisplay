#include "panel_schema.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#define GET(o,k) cJSON_GetObjectItemCaseSensitive(o,k)
static const char *str(const cJSON *o, const char *k) {
    return cJSON_GetStringValue(GET(o,k));
}
static bool one_of(const char *text, const char *choices) {
    if (!text || !*text || strchr(text,' ')) return false;
    size_t len = strlen(text);
    for (const char *p = choices; *p;) {
        size_t n = strcspn(p," ");
        if (n == len && !strncmp(p,text,n)) return true;
        p += n;
        if (*p) ++p;
    }
    return false;
}
static bool fields(const cJSON *obj, const char *keys) {
    if (!cJSON_IsObject(obj)) return false;
    const cJSON *child;
    cJSON_ArrayForEach(child,obj) {
        if (!one_of(child->string,keys) || GET(obj,child->string) != child) return false;
    }
    return true;
}
static bool string_field(const cJSON *o,const char *key,size_t max,bool empty) {
    const char *s = str(o,key);
    return s && (empty || *s) && strlen(s) <= max;
}
static bool integer(const cJSON *o,const char *k,int low,int high) {
    const cJSON *v=GET(o,k);
    return cJSON_IsNumber(v) && v->valuedouble >= low && v->valuedouble <= high &&
           v->valuedouble == v->valueint;
}
static bool identifier(const char *s) {
    if (!s || !*s || strlen(s)>32) return false;
    for (;*s;++s) if (!(*s>='a' && *s<='z') && !(*s>='0' && *s<='9') && *s!='-') return false;
    return true;
}
static bool uuid(const char *s,bool empty) {
    if (!s) return false;
    if (!*s) return empty;
    if (strlen(s)!=35) return false;
    for (size_t i=0;i<35;++i) {
        if (i==8 || i==13 || i==18) { if(s[i]!='-') return false; }
        else if (!isxdigit((unsigned char)s[i])) return false;
    }
    return true;
}
static bool clock_time(const char *s) {
    return s && strlen(s)==5 && isdigit((unsigned char)s[0]) && isdigit((unsigned char)s[1]) &&
        s[2]==':' && s[3]>='0' && s[3]<='5' && isdigit((unsigned char)s[4]) &&
        (s[0]<'2' || (s[0]=='2' && s[1]<'4'));
}
static bool color(const char *s) {
    return s && strlen(s)==7 && *s=='#' && strspn(s+1,"0123456789abcdefABCDEF")==6;
}
static bool connection(const cJSON *o) {
    const char *h=str(o,"host");
    return fields(o,"host port tls user ca_pem") && string_field(o,"host",127,true) &&
        strspn(h,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-")==strlen(h) &&
        integer(o,"port",1,65535) && cJSON_IsBool(GET(o,"tls")) && string_field(o,"user",95,!*h) &&
        string_field(o,"ca_pem",4095,true) && (!cJSON_IsTrue(GET(o,"tls")) || !*h ||
        strstr(str(o,"ca_pem"),"-----BEGIN CERTIFICATE-----"));
}
static bool display(const cJSON *o) {
    return fields(o,"dayBrightness nightBrightness saverSeconds nightMode nightStart nightEnd nightUuid") &&
        integer(o,"dayBrightness",1,100) && integer(o,"nightBrightness",1,100) &&
        integer(o,"saverSeconds",0,86400) && one_of(str(o,"nightMode"),"timer value") &&
        clock_time(str(o,"nightStart")) && clock_time(str(o,"nightEnd")) &&
        uuid(str(o,"nightUuid"),strcmp(str(o,"nightMode"),"value")!=0);
}
static bool touch_input(const cJSON *mapping,const char *target,bool allowed) {
    if (!GET(mapping,"inputUuid")) return true;
    const char *input=str(mapping,"inputUuid");
    return uuid(input,true) && (!*input || (allowed && *target && strcasecmp(input,target)));
}

static bool touch_corner(const cJSON *corner) {
    if (!fields(corner,"type uuid action sceneId inputUuid") ||
        !string_field(corner,"type",16,true) || !string_field(corner,"action",16,true) ||
        !uuid(str(corner,"uuid"),true) || !integer(corner,"sceneId",0,2147483647)) return false;
    const char *type=str(corner,"type"), *action=str(corner,"action"), *id=str(corner,"uuid");
    int scene=GET(corner,"sceneId")->valueint;
    if (!touch_input(corner,id,!strcmp(type,"switch"))) return false;
    if (!*type) return !*action && !*id && scene==0;
    if (!*id || (scene!=0 && strcmp(action,"scene"))) return false;
    if (one_of(type,"pulse switch")) return !strcmp(action,"trigger");
    if (!strcmp(type,"shading")) return one_of(action,"up down");
    if (!strcmp(type,"audio")) return one_of(action,"volumeUp volumeDown next previous playPause");
    return !strcmp(type,"lighting") && one_of(action,"next previous scene");
}

static bool touch(const cJSON *section) {
    if (!fields(section,"enabled tapWindowMs dayTextColor corners center") || !cJSON_IsBool(GET(section,"enabled")) ||
        !integer(section,"tapWindowMs",150,1000)) return false;
    const cJSON *corners=GET(section,"corners"), *center=GET(section,"center"), *corner;
    if (!cJSON_IsArray(corners) || cJSON_GetArraySize(corners)!=4 ||
        !fields(center,"uuid inputUuid") || !uuid(str(center,"uuid"),true)) return false;
    if (!touch_input(center,str(center,"uuid"),true)) return false;
    cJSON_ArrayForEach(corner,corners) if (!touch_corner(corner)) return false;
    return true;
}

static bool camera_url(const char *s) {
    if (!s || !*s) return true;
    if (strncmp(s,"http://",7) || strpbrk(s," \t\r\n@#")) return false;
    const char *host=s+7, *path=strchr(host,'/');
    return path && path>host && path[1];
}
static bool intercom(const cJSON *o) {
    const char *url=str(o,"cameraUrl");
    bool enabled=cJSON_IsTrue(GET(o,"enabled"));
    return fields(o,"enabled uuid gateUuid cameraUrl cameraUser speakerVolume microphoneGain ringtone ringtoneRepeat") &&
        cJSON_IsBool(GET(o,"enabled")) && uuid(str(o,"uuid"),!enabled) && uuid(str(o,"gateUuid"),true) &&
        string_field(o,"cameraUrl",256,!enabled) && camera_url(url) && string_field(o,"cameraUser",95,!enabled) &&
        integer(o,"speakerVolume",0,100) && integer(o,"microphoneGain",0,100) &&
        string_field(o,"ringtone",64,true) && strspn(str(o,"ringtone"),"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.") == strlen(str(o,"ringtone")) &&
        !strstr(str(o,"ringtone"),"..") && cJSON_IsBool(GET(o,"ringtoneRepeat"));
}
static bool energy(const cJSON *o) {
    if (!fields(o,"PowerGrid PowerBat PowerPV PowerFull SocBat Status BatteryTemperature Temperature WindSpeed HasSunRadiation HasPrecipitation SunAzimuth SunAltitude Sunrise Sunset RelativeHumidity")) return false;
    const cJSON *v;
    cJSON_ArrayForEach(v,o) {
        if (!fields(v,"uuid scale offset") || !uuid(str(v,"uuid"),false)) return false;
        const cJSON *scale=GET(v,"scale"), *offset=GET(v,"offset");
        if (!cJSON_IsNumber(scale) || !cJSON_IsNumber(offset) || !isfinite(scale->valuedouble) ||
            !isfinite(offset->valuedouble) || fabs(scale->valuedouble)>1e6 || fabs(offset->valuedouble)>1e6) return false;
    }
    return true;
}
static bool bounded_number(const cJSON *v) {
    return !v || (cJSON_IsNumber(v) && isfinite(v->valuedouble) && fabs(v->valuedouble)<=1e6);
}
static bool block(const cJSON *b) {
    const char *t=str(b,"type");
    return fields(b,"id name type uuid stateUuid unit decimals scale offset allowPermanent") && identifier(str(b,"id")) && strcmp(str(b,"id"),"intercom-gate") &&
        string_field(b,"name",80,false) && one_of(t,"value pulse switch lighting irc shading dimmer central-lighting slider timed-switch window-monitor gate window audio") &&
        uuid(str(b,"uuid"),!strcmp(t,"value")) && uuid(str(b,"stateUuid"),strcmp(t,"value")!=0) &&
        string_field(b,"unit",16,true) && (!GET(b,"allowPermanent") ||
        (!strcmp(t,"timed-switch") && cJSON_IsBool(GET(b,"allowPermanent")))) &&
        (!GET(b,"decimals") || (!strcmp(t,"value") && integer(b,"decimals",0,3))) &&
        bounded_number(GET(b,"scale")) && bounded_number(GET(b,"offset")) &&
        (!strcmp(t,"value") || (!GET(b,"scale") && !GET(b,"offset")));
}
bool panel_schema_validate(const cJSON *p,char *error,size_t n) {
    const char *why="Érvénytelen konfiguráció vagy sémaverzió.";
    if (!fields(p,"schema revision loxone display tabs intercom adminUser touch") ||
        !integer(p,"schema",1,1) || !integer(p,"revision",1,2147483647) ||
        !string_field(p,"adminUser",63,true) || strpbrk(str(p,"adminUser"),":\r\n")) goto fail;
    why="Ellenőrizd a Miniserver címét, portját és TLS esetén a CA-tanúsítványt.";
    if (!connection(GET(p,"loxone"))) goto fail;
    why="Érvénytelen időpont, fényerő, időzítés vagy éjszakai állapot-UUID.";
    if (!display(GET(p,"display"))) goto fail;
    why="Ellenőrizd a Touch négy sarkának típusát és műveletét, a középső világítás UUID-jét és a 150–1000 ms-os érintésközt.";
    if (!touch(GET(p,"touch"))) goto fail;
    why="A Touch nappali szövegszínét #RRGGBB formátumban add meg.";
    if (!color(str(GET(p,"touch"),"dayTextColor"))) goto fail;
    why="A kaputelefonhoz helyi HTTP/MJPEG-videócím és csengetésállapot szükséges.";
    if (!intercom(GET(p,"intercom"))) goto fail;
    const cJSON *tabs=GET(p,"tabs"), *tab;
    why="Legfeljebb nyolc lapfül használható.";
    if (!cJSON_IsArray(tabs) || cJSON_GetArraySize(tabs)>8) goto fail;
    const char *ids[96];
    size_t count=0;
    cJSON_ArrayForEach(tab,tabs) {
        why="A lapfül neve, típusa vagy ikonja érvénytelen.";
        const char *kind=str(tab,"kind");
        if (!string_field(tab,"name",80,false) || !identifier(str(tab,"icon")) || !one_of(kind,"controls energy")) goto fail;
        if (!strcmp(kind,"energy")) {
            why="Érvénytelen energia-hozzárendelés (UUID, szorzó vagy eltolás).";
            if (!fields(tab,"name icon kind energy") || !energy(GET(tab,"energy"))) goto fail;
            continue;
        }
        const cJSON *bg=GET(tab,"background"),*blocks=GET(tab,"blocks"),*b;
        why="Érvénytelen háttér vagy háttérszín.";
        if (!fields(tab,"name icon kind background blocks") || !fields(bg,"type value") ||
            !one_of(str(bg,"type"),"image color") ||
            (!strcmp(str(bg,"type"),"image")?!identifier(str(bg,"value")):!color(str(bg,"value")))) goto fail;
        why="Laponként legfeljebb 12 elem használható.";
        if (!cJSON_IsArray(blocks) || cJSON_GetArraySize(blocks)>12) goto fail;
        cJSON_ArrayForEach(b,blocks) {
            why="Az elem neve, típusa, azonosítója vagy Loxone UUID-je érvénytelen.";
            if (!block(b)) goto fail;
            const char *id=str(b,"id");
            for(size_t i=0;i<count;i++) if (!strcmp(ids[i],id)) goto fail;
            ids[count++]=id;
        }
    }
    return true;
fail:
    if (n) snprintf(error,n,"%s",why);
    return false;
}
