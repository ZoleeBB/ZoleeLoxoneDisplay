#include "panel_energy_stats.h"
#include "app_energy_split.h"
#include "panel_config.h"
#include "zld_loxone.h"
#include "zld_update.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "esp_timer.h"
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef PANEL_DAILY_BASE
#define PANEL_DAILY_BASE "/sdcard/zld-daily"
#endif
#define BANK_MAGIC UINT32_C(0x5A444C31)
#define SPLIT_BIT 16U

typedef struct {
    uint64_t mapping;
    panel_daily_t totals;
} history_t;

typedef struct {
    uint32_t magic, sequence, day;
    history_t history[LX_HOME_MAX_PAGES];
    uint64_t checksum;
} bank_t;

typedef struct {
    uint64_t mapping;
    double watts_kw[4];
    unsigned valid;
    int64_t monotonic;
    time_t clock;
} sampling_t;

static portMUX_TYPE publish_lock=portMUX_INITIALIZER_UNLOCKED;
static bank_t bank, scratch_bank;
static sampling_t samples[LX_HOME_MAX_PAGES];
static uint32_t revision, published_revision;
static unsigned live_tabs;
static int64_t last_save;
static bool changed, loaded;

static uint64_t hash(uint64_t h,const void *bytes,size_t len)
{
    const uint8_t *p=bytes;
    for (size_t i=0;i<len;i++) h=(h^p[i])*UINT64_C(1099511628211);
    return h;
}

static uint64_t text_hash(uint64_t h,const cJSON *obj,const char *key)
{
    const char *s=cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(obj,key));
    return hash(h,s?s:"",s?strlen(s)+1:1);
}

static uint64_t mapping_id(const cJSON *lx,const cJSON *tab)
{
    const char *kind=cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(tab,"kind"));
    if (!kind || strcmp(kind,"energy")) return 0;
    const cJSON *energy=cJSON_GetObjectItemCaseSensitive(tab,"energy");
    uint64_t h=text_hash(UINT64_C(14695981039346656037),lx,"host");
    double port=cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(lx,"port"));
    h=hash(h,&port,sizeof(port));
    for (int i=0;i<4;i++) {
        const cJSON *source=cJSON_GetObjectItemCaseSensitive(energy,lx_fields[i].name);
        h=text_hash(h,source,"uuid");
        double scale=cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(source,"scale"));
        double offset=cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(source,"offset"));
        h=hash(hash(h,&scale,sizeof(scale)),&offset,sizeof(offset));
    }
    /* Az állapot 3 lenullázza a hálózatot, ezért e forrás cseréje is új naplót kezd. */
    const cJSON *status=cJSON_GetObjectItemCaseSensitive(energy,"Status");
    h=text_hash(h,status,"uuid");
    if (status) {
        double scale=cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(status,"scale"));
        double offset=cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(status,"offset"));
        h=hash(hash(h,&scale,sizeof(scale)),&offset,sizeof(offset));
    }
    return h?h:1;
}

static uint32_t day_id(time_t now)
{
    struct tm t;
    if (!localtime_r(&now,&t) || t.tm_year<120) return 0;
    return (uint32_t)((t.tm_year+1900)*10000+(t.tm_mon+1)*100+t.tm_mday);
}

static uint64_t checksum(const bank_t *b)
{
    return hash(UINT64_C(14695981039346656037),b,offsetof(bank_t,checksum));
}

static void load_bank(uint32_t day)
{
    bank.day=day;
    bank.magic=BANK_MAGIC;
    for (unsigned slot=0;slot<2;slot++) {
        char path[80];
        snprintf(path,sizeof(path),PANEL_DAILY_BASE ".%u",slot);
        FILE *f=fopen(path,"rb");
        if (!f) continue;
        bool valid=fread(&scratch_bank,1,sizeof(bank),f)==sizeof(bank) && fgetc(f)==EOF &&
                   scratch_bank.magic==BANK_MAGIC && scratch_bank.day==day && checksum(&scratch_bank)==scratch_bank.checksum;
        fclose(f);
        if (valid && scratch_bank.sequence>=bank.sequence) bank=scratch_bank;
    }
    loaded=true;
}

static void save_bank(void)
{
    bank_t *out=&scratch_bank;
    *out=bank;
    out->sequence++;
    out->checksum=checksum(out);
    char path[80];
    snprintf(path,sizeof(path),PANEL_DAILY_BASE ".%u",(unsigned)(out->sequence%2));
    FILE *f=fopen(path,"wb");
    if (!f) return;
    bool ok=fwrite(out,1,sizeof(*out),f)==sizeof(*out) && fflush(f)==0 && fsync(fileno(f))==0;
    if (fclose(f)!=0) ok=false;
    if (ok) {
        bank.sequence=out->sequence;
        changed=false;
    } else ESP_LOGW("daily","Az SD-ellenőrzőpont mentése sikertelen.");
}

static bool update_profile(void)
{
    unsigned gen=panel_config_generation();
    if (revision==gen) return true;
    cJSON *p=panel_config_snapshot();
    if (!p) return false;
    const cJSON *tabs=cJSON_GetObjectItemCaseSensitive(p,"tabs");
    const cJSON *lx=cJSON_GetObjectItemCaseSensitive(p,"loxone");
    const cJSON *r=cJSON_GetObjectItemCaseSensitive(p,"revision");
    if (!cJSON_IsNumber(r) || (unsigned)r->valueint!=gen) { cJSON_Delete(p); return false; }
    scratch_bank=bank;
    for (unsigned i=0;i<LX_HOME_MAX_PAGES;i++) {
        uint64_t id=mapping_id(lx,cJSON_GetArrayItem(tabs,(int)i));
        samples[i]=(sampling_t){.mapping=id};
        history_t next={.mapping=id};
        for (unsigned old=0;id && old<LX_HOME_MAX_PAGES;old++) {
            if (scratch_bank.history[old].mapping==id) { next=scratch_bank.history[old]; break; }
        }
        portENTER_CRITICAL(&publish_lock);
        published_revision=0;
        bank.history[i]=next;
        portEXIT_CRITICAL(&publish_lock);
    }
    revision=gen;
    cJSON_Delete(p);
    return true;
}

static bool accumulate(panel_daily_t *totals,sampling_t *s,const lx_values_t *v,bool online,
                       int64_t monotonic,time_t now)
{
    double power[4];
    unsigned valid=0;
    for (unsigned i=0;i<4;i++) {
        power[i]=online && v->seen[i] ? v->value[i] : 0;
        if (online && v->seen[i] && isfinite(power[i])) valid|=1U<<i;
    }
    if (v->seen[5] && v->value[5]==3) power[0]=0;
    int64_t us=monotonic-s->monotonic;
    double sec=us/1.e6;
    unsigned both=valid & s->valid;
    bool contiguous=s->monotonic && us>0 && us<=10000000 && fabs(difftime(now,s->clock)-sec)<=1.5;
    if (both && contiguous) {
        double hours=sec/3600.;
        /* Trapezoid disjoint directions separately, including sign changes. */
        for (unsigned end=0;end<2;end++) {
            const double *a=end ? power : s->watts_kw;
            double half=hours/2;
            if (both&1U) {
                totals->imported+=fmax(0,a[0])*half;
                totals->exported+=fmax(0,-a[0])*half;
            }
            if (both&2U) {
                totals->charged+=fmax(0,-a[1])*half;
                totals->discharged+=fmax(0,a[1])*half;
            }
            if (both&4U) totals->production+=fmax(0,a[2])*half;
            if (both&8U) totals->consumption+=fmax(0,a[3])*half;
            if ((both&14U)==14U) {
                app_energy_split_t split=app_energy_split(a[2],a[3],a[1]);
                totals->pv_home+=split.pv_home*half;
                totals->pv_stored+=split.pv_stored*half;
                totals->bat_home+=split.battery_home*half;
                totals->seen|=SPLIT_BIT;
            }
        }
        totals->seen|=both;
    }
    s->clock=now;
    s->monotonic=monotonic;
    s->valid=valid;
    memcpy(s->watts_kw,power,sizeof(power));
    return both && contiguous;
}

void panel_energy_stats_tick(void)
{
    time_t now=time(NULL);
    uint32_t day=day_id(now);
    int64_t monotonic=esp_timer_get_time();
    if (!day || !zld_assets_ready()) {
        for (unsigned i=0;i<LX_HOME_MAX_PAGES;i++) samples[i].valid=0;
        portENTER_CRITICAL(&publish_lock);
        live_tabs=0;
        portEXIT_CRITICAL(&publish_lock);
        return;
    }
    if (!loaded) load_bank(day);
    if (!update_profile()) return;
    if (bank.day!=day) {
        portENTER_CRITICAL(&publish_lock);
        bank.day=day;
        for (unsigned i=0;i<LX_HOME_MAX_PAGES;++i) {
            bank.history[i].totals=(panel_daily_t){0};
            samples[i].valid=0;
        }
        portEXIT_CRITICAL(&publish_lock);
        changed=true;
    }
    unsigned live=0;
    for (unsigned i=0;i<LX_HOME_MAX_PAGES;i++) {
        if (!samples[i].mapping) continue;
        lx_values_t v={0};
        bool online=zld_loxone_energy_at(i,revision,&v);
        panel_daily_t totals=bank.history[i].totals;
        if (accumulate(&totals,&samples[i],&v,online,monotonic,now)) {
            portENTER_CRITICAL(&publish_lock);
            bank.history[i].totals=totals;
            portEXIT_CRITICAL(&publish_lock);
            changed=true;
        }
        if (online && samples[i].valid) live|=1U<<i;
    }
    portENTER_CRITICAL(&publish_lock);
    live_tabs=live;
    published_revision=revision;
    portEXIT_CRITICAL(&publish_lock);
    if (changed && monotonic-last_save>=300000000LL) {
        last_save=monotonic;
        save_bank();
    }
}

bool panel_energy_stats_read(unsigned tab,unsigned gen,panel_daily_t *out,bool *live)
{
    if (tab>=LX_HOME_MAX_PAGES || !out || !live) return false;
    portENTER_CRITICAL(&publish_lock);
    bool ok=gen && gen==published_revision && bank.history[tab].mapping;
    *out=ok ? bank.history[tab].totals : (panel_daily_t){0};
    *live=ok && (live_tabs&(1U<<tab));
    portEXIT_CRITICAL(&publish_lock);
    return ok && out->seen!=0;
}
