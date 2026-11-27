#include "panel_energy_summary.h"
#include "panel_energy_stats.h"
#include "panel_config.h"
#include "panel_ui_style.h"
#include <math.h>
#include <stdio.h>

static lv_obj_t *summary,*title,*status,*tiles[6],*names[6],*values[6],*details[6],*hint;
static int last_night;
static uint32_t last;
static bool showing;
static const char *const captions[]={"Termelés","Fogyasztás","Vételezés","Visszatáplálás","Akkumulátor","Megtakarítás"};
static const uint32_t day_accent[]={0x087E12,0xC94800,0xC94800,0x087E12,0x1D63A8,0x087E12};
static const uint32_t night_accent[]={0x58E93C,0xFFC44D,0xFFC44D,0x58E93C,0x7DC3FF,0x58E93C};

static void gesture(lv_event_t *ev)
{
    (void)ev;
    lv_indev_t *input=lv_indev_active();
    if (!input) return;
    lv_dir_t dir=lv_indev_get_gesture_dir(input);
    if (dir!=LV_DIR_LEFT && dir!=LV_DIR_RIGHT) return;
    showing=!showing;
    if (showing) lv_obj_remove_flag(summary,LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(summary,LV_OBJ_FLAG_HIDDEN);
    last=lv_tick_get()-1000;
    lv_indev_wait_release(input);
}

void panel_summary_attach(lv_obj_t *parent)
{
    showing=false;
    last_night=-1;
    last=lv_tick_get()-1000;
    lv_obj_add_flag(parent,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(parent,LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(parent,gesture,LV_EVENT_GESTURE,NULL);
    summary=panel_box(parent,0,0,640,640,0,0);
    lv_obj_add_flag(summary,LV_OBJ_FLAG_HIDDEN);
    title=panel_label(summary,"Mai összesítő",32,20,&app_font_28,0);
    status=panel_label(summary,"",32,66,&app_font_20,0);
    for (int i=0;i<6;i++) {
        tiles[i]=panel_box(summary,i%2?331:32,100+(i/2)*153,277,138,0,16);
        names[i]=panel_label(tiles[i],captions[i],17,12,&app_font_24,0);
        values[i]=panel_label(tiles[i],"—",17,46,&app_font_28,0);
        details[i]=panel_label(tiles[i],"",17,94,&app_font_20,0);
    }
    hint=panel_label(summary,"Húzd el oldalra az energiaáramláshoz",0,566,&app_font_20,0);
    lv_obj_set_width(hint,640);
    lv_obj_set_style_text_align(hint,LV_TEXT_ALIGN_CENTER,0);
}

bool panel_summary_visible(void)
{
    return showing;
}

void panel_summary_sync(unsigned tab,bool night)
{
    if (!showing) return;
    bool theme_changed=last_night!=(int)night;
    if (theme_changed) {
        last_night=night;
        lv_color_t ink=lv_color_hex(night ? 0xF5FAFF : 0x00183D);
        lv_color_t muted=lv_color_hex(night ? 0x9FB6CC : 0x4B6478);
        lv_obj_set_style_bg_color(summary,lv_color_hex(night ? 0x051A35 : 0xE6EFF4),0);
        lv_obj_set_style_text_color(title,ink,0);
        lv_obj_set_style_text_color(status,muted,0);
        lv_obj_set_style_text_color(hint,muted,0);
        for (unsigned i=0;i<6;i++) {
            lv_obj_set_style_bg_color(tiles[i],lv_color_hex(night ? 0x10294A : 0xFFFFFF),0);
            lv_obj_set_style_text_color(names[i],ink,0);
            lv_obj_set_style_text_color(details[i],muted,0);
            lv_obj_set_style_text_color(values[i],lv_color_hex(night ? night_accent[i] : day_accent[i]),0);
        }
    }
    if (!theme_changed && lv_tick_elaps(last)<1000) return;
    last=lv_tick_get();
    panel_daily_t t={0};
    bool live=false;
    bool ok=panel_energy_stats_read(tab,panel_config_generation(),&t,&live);
    panel_text(status, !ok ? "A mai első mérési időszakra vár..." :
                live ? "A kijelző által ma mért időszakok" : "Utolsó ismert mai adat · megszakadt a kapcsolat");
    char s[72],d[76];
    double local=t.pv_home+t.bat_home;
    double buy=t.imported*65, sell=t.exported*5;
    const double energy[]={t.production,t.consumption,t.imported,t.exported};
    const unsigned flags[]={4,8,1,1,2,17};
    for (unsigned i=0;i<6;i++) {
        d[0]=0;
        if (!ok || (t.seen&flags[i])!=flags[i]) snprintf(s,sizeof(s),"—");
        else if (i<4) {
            snprintf(s,sizeof(s),"%.1f kWh",energy[i]);
            if (i==0 && (t.seen&16) && t.production>0)
                snprintf(d,sizeof(d),"Saját felhasználás %.0f%%",fmin(100,100*(t.pv_home+t.pv_stored)/t.production));
            if (i==1 && (t.seen&16) && t.consumption>0)
                snprintf(d,sizeof(d),"Önellátás %.0f%%",fmin(100,100*local/t.consumption));
            if (i==2) snprintf(d,sizeof(d),"Költség %.0f Ft",buy);
            if (i==3) snprintf(d,sizeof(d),"Bevétel %.0f Ft",sell);
        } else if (i==4) {
            snprintf(s,sizeof(s),"%.1f / %.1f kWh",t.charged,t.discharged);
            snprintf(d,sizeof(d),"töltés / leadás");
        } else {
            snprintf(s,sizeof(s),"%.0f Ft",local*65+sell);
            snprintf(d,sizeof(d),"Nettó költség %.0f Ft",buy-sell);
        }
        panel_text(values[i],s);
        panel_text(details[i],d);
    }
}
