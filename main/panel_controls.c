#include "panel_controls.h"
#include "panel_lighting.h"
#include "panel_ui_style.h"
#include "zld_loxone.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LIST_ROWS 6
#define VISIBLE_ROWS LX_HOME_MAX_CARDS

typedef enum {
    VALUE, PULSE, SWITCH, LIGHTING, IRC, SHADING, DIMMER, CENTRAL, SLIDER,
    TIMED, MONITOR, GATE, WINDOW, AUDIO
} kind_t;
typedef enum {
    ON, OFF, TRIGGER, LOWER, HIGHER, UP, STOP, DOWN, LIGHT_DETAIL, MUSIC_PREV,
    MUSIC_PLAY, MUSIC_NEXT, MUSIC_FAVORITES, NEXT_PAGE, PREV_PAGE, LIST_CHOICE
} action_t;
typedef struct {
    lv_obj_t *button;
    action_t action;
} command_t;
static const char *const types[] = {"value","pulse","switch","lighting","irc","shading",
    "dimmer","central-lighting","slider","timed-switch","window-monitor","gate","window","audio"};
static const char *const icons[] = {"sensor","light","light","light","thermostat","blinds",
    "light","light","settings","clock","home","gate","blinds","audio"};
static lv_obj_t *container, *view, *primary, *secondary, *feedback, *range, *range_label;
static lv_obj_t *items[VISIBLE_ROWS], *choice[LIST_ROWS], *choice_labels[LIST_ROWS], *pager[2];
static const cJSON *blocks, *block;
static int selected;
static uint32_t ink, feedback_tick;
static unsigned rev, page, ids[LIST_ROWS], action_count;
static command_t actions[8];
static bool is_night, audio_favorites, choice_valid[LIST_ROWS], choice_active[LIST_ROWS], editing;
static lx_audio_favorites_t *favorites;
static double low, high;
static kind_t kind;

static const char *str(const cJSON *o, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o,key);
    return cJSON_IsString(v) ? v->valuestring : "";
}

static kind_t block_kind(const cJSON *b)
{
    for (unsigned i=0; i<sizeof(types)/sizeof(*types); ++i)
        if (!strcmp(str(b,"type"),types[i])) return (kind_t)i;
    return VALUE;
}

static void enabled(lv_obj_t *o, bool on)
{
    if (on) lv_obj_remove_state(o, LV_STATE_DISABLED);
    else lv_obj_add_state(o, LV_STATE_DISABLED);
}

static void visible(lv_obj_t *o, bool on)
{
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *text_line(lv_obj_t *parent, const char *s, int x, int y, int w,
                           const lv_font_t *font, uint32_t color, lv_text_align_t align)
{
    lv_obj_t *o = panel_label(parent,s,x,y,font,color);
    lv_obj_set_width(o,w);
    lv_obj_set_style_text_align(o, align, 0);
    lv_label_set_long_mode(o, LV_LABEL_LONG_MODE_CLIP);
    return o;
}

static void status(esp_err_t result)
{
    panel_text(feedback,result == ESP_OK ? "Kérés elküldve" : "A művelet most nem érhető el.");
    feedback_tick = lv_tick_get();
}

static void close_detail(void)
{
    panel_lighting_close();
    if (audio_favorites) app_loxone_audio_detail_close();
    free(favorites);
    favorites=NULL;
    audio_favorites=false;
    action_count=0;
    memset(actions,0,sizeof(actions));
    memset(choice,0,sizeof(choice));
    memset(items,0,sizeof(items));
    memset(choice_valid,0,sizeof(choice_valid));
    memset(choice_active,0,sizeof(choice_active));
    pager[0]=pager[1]=NULL;
    primary=secondary=range=range_label=feedback=NULL;
    editing=false;
    if (view) lv_obj_delete(view);
    view=NULL;
}

void panel_controls_close(void)
{
    close_detail();
    block=blocks=NULL;
    container=NULL;
}

static void rebuild(void);
static void select_event(lv_event_t *event)
{
    selected=(int)(intptr_t)lv_event_get_user_data(event);
    page=0;
    rebuild();
}

static void range_event(lv_event_t *event)
{
    lv_event_code_t code=lv_event_get_code(event);
    if (code==LV_EVENT_PRESSED) editing=true;
    if (code==LV_EVENT_PRESS_LOST) editing=false;
    if (code!=LV_EVENT_VALUE_CHANGED && code!=LV_EVENT_RELEASED) return;
    if (!editing || !block || rev!=zld_loxone_revision()) return;
    double value=low+(high-low)*lv_slider_get_value(range)/1000.;
    char s[64];
    snprintf(s,sizeof(s),"%.1f",value);
    panel_text(range_label,s);
    if (code!=LV_EVENT_RELEASED) return;
    editing=false;
    const char *id=str(block,"id");
    if (kind==SHADING) status(app_loxone_shading_position(id,rev,(unsigned)lround(value)));
    else if (kind==AUDIO) status(app_loxone_audio_command(id,rev,LX_AUDIO_VOLUME,(unsigned)lround(value),true));
    else status(app_loxone_controls_set(id,rev,value));
}

static void slider(int y)
{
    range=lv_slider_create(view);
    lv_obj_set_pos(range,57,y);
    lv_obj_set_size(range,497,22);
    lv_slider_set_range(range,0,1000);
    lv_obj_set_ext_click_area(range,16);
    lv_obj_add_event_cb(range,range_event,LV_EVENT_ALL,NULL);
    lv_obj_set_style_bg_color(range,lv_color_hex(0x8CBEB5),LV_PART_INDICATOR);
    range_label=text_line(view,"—",567,y-5,115,&app_font_24,ink,LV_TEXT_ALIGN_CENTER);
}

static void sync_range(bool valid, bool allowed, double min, double max, double current)
{
    if (!range) return;
    visible(range,valid);
    visible(range_label,valid);
    enabled(range,valid && allowed);
    if (!valid || editing || max<=min) return;
    low=min; high=max;
    int pos=(int)lround(fmin(1.,fmax(0.,(current-min)/(max-min)))*1000.);
    if (lv_slider_get_value(range)!=pos) lv_slider_set_value(range,pos,LV_ANIM_OFF);
    char s[48];
    snprintf(s,sizeof(s),"%.1f",current);
    panel_text(range_label,s);
}

static void act(lv_event_t *event)
{
    action_t action=(action_t)(intptr_t)lv_event_get_user_data(event);
    if (!block || rev!=zld_loxone_revision()) return;
    const char *id=str(block,"id");
    esp_err_t result=ESP_ERR_INVALID_STATE;
    if (action==LIGHT_DETAIL) {
        panel_lighting_open(lv_screen_active(),id,str(block,"name"),rev,is_night);
        if (!panel_lighting_is_open()) status(result);
        return;
    }
    if (action==MUSIC_FAVORITES) {
        if (!audio_favorites) {
            favorites=calloc(1,sizeof(*favorites));
            if (!favorites) { status(ESP_ERR_NO_MEM); return; }
            audio_favorites=true;
            page=0;
            result=app_loxone_audio_favorites_request(id,rev,0);
        } else {
            free(favorites); favorites=NULL; audio_favorites=false; page=0;
            app_loxone_audio_detail_close();
            result=ESP_OK;
        }
        status(result);
        panel_controls_update();
        return;
    }
    if (action==NEXT_PAGE || action==PREV_PAGE) {
        page=action==NEXT_PAGE ? page+1 : page ? page-1 : 0;
        if (audio_favorites && (page*LIST_ROWS/LX_AUDIO_FAVORITE_PAGE !=
                               favorites->start/LX_AUDIO_FAVORITE_PAGE))
            status(app_loxone_audio_favorites_request(id,rev,page*LIST_ROWS));
        panel_controls_update();
        return;
    }
    switch (kind) {
    case SWITCH:
    case PULSE:
        result=app_loxone_home_command(id,kind==PULSE ? LX_HOME_COMMAND_PULSE :
                   action==ON ? LX_HOME_COMMAND_ON : LX_HOME_COMMAND_OFF);
        break;
    case IRC:
        result=app_loxone_home_command(id,action==HIGHER ? LX_HOME_COMMAND_IRC_INCREASE : LX_HOME_COMMAND_IRC_DECREASE);
        break;
    case SHADING:
        result=app_loxone_shading_command(id,rev,action==UP ? LX_HOME_COMMAND_SHADING_UP :
                               action==DOWN ? LX_HOME_COMMAND_SHADING_DOWN : LX_HOME_COMMAND_SHADING_STOP);
        break;
    case AUDIO: {
        lx_audio_summary_t summary={0};
        if (app_loxone_audio_summary(id,rev,&summary)) {
            lx_audio_action_t a=action==MUSIC_PREV ? LX_AUDIO_PREVIOUS : action==MUSIC_NEXT ? LX_AUDIO_NEXT :
                     summary.track.mode==LX_AUDIO_MODE_PLAY ? LX_AUDIO_PAUSE : LX_AUDIO_PLAY;
            result=app_loxone_audio_command(id,rev,a,0,true);
        }
        break;
    }
    default:
        result=app_loxone_controls_command(id,rev,action==ON ? LX_CONTROLS_ON : action==OFF ? LX_CONTROLS_OFF :
             action==UP ? LX_CONTROLS_OPEN : action==STOP ? LX_CONTROLS_STOP :
             action==DOWN ? LX_CONTROLS_CLOSE : LX_CONTROLS_PULSE);
        break;
    }
    status(result);
}

static lv_obj_t *button(const char *s, int x, int y, int w, int h, action_t a)
{
    lv_obj_t *b=panel_button(view,s,x,y,w,h);
    lv_obj_set_style_bg_opa(b,LV_OPA_80,0);
    lv_obj_set_style_opa(b,LV_OPA_50,LV_STATE_DISABLED);
    lv_obj_add_event_cb(b,act,LV_EVENT_CLICKED,(void *)(intptr_t)a);
    return b;
}

static void command(const char *s,int x,int y,int w,int h,action_t a)
{
    actions[action_count++]=(command_t){button(s,x,y,w,h,a),a};
}

static void choose(lv_event_t *ev)
{
    unsigned i=(unsigned)(uintptr_t)lv_event_get_user_data(ev);
    if (!block || i>=LIST_ROWS || !choice_valid[i] || rev!=zld_loxone_revision()) return;
    if (kind==LIGHTING) status(zld_loxone_select_mood(str(block,"id"),rev,ids[i]));
    if (kind==AUDIO && favorites) status(app_loxone_audio_favorite(str(block,"id"),rev,ids[i],favorites->generation));
}

static void list_widgets(int y, int pitch)
{
    for (unsigned i=0;i<LIST_ROWS;++i) {
        lv_obj_t *b=panel_box(view,24,y+(int)i*pitch,672,pitch-5,0x353C40,11);
        lv_obj_add_flag(b,LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_opa(b,LV_OPA_80,0);
        lv_obj_add_event_cb(b,choose,LV_EVENT_CLICKED,(void *)(uintptr_t)i);
        choice[i]=b;
        choice_labels[i]=text_line(b,"",17,8,630,&app_font_24,0xFFFFFF,LV_TEXT_ALIGN_LEFT);
        visible(b,false);
    }
    pager[0]=button("Előző",22,546,130,53,PREV_PAGE);
    pager[1]=button("Tovább",566,546,130,53,NEXT_PAGE);
}

static void update_choice(unsigned i, bool valid, const char *text, bool active, bool allowed)
{
    choice_valid[i]=valid;
    visible(choice[i],valid);
    if (!valid) return;
    panel_text(choice_labels[i],text);
    enabled(choice[i],allowed);
    if (choice_active[i]!=active) {
        choice_active[i]=active;
        lv_obj_set_style_bg_color(choice[i],lv_color_hex(active ? 0x287163 : 0x353C40),0);
        lv_obj_set_style_border_width(choice[i],active ? 3 : 0,0);
        lv_obj_set_style_border_color(choice[i],lv_color_hex(0xB7E7C4),0);
    }
}

static void build_list(void)
{
    lv_obj_t *list=panel_box(view,12,12,696,616,0,0);
    lv_obj_set_style_bg_opa(list,LV_OPA_TRANSP,0);
    lv_obj_add_flag(list,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list,LV_DIR_VER);
    for (int i=0;i<cJSON_GetArraySize(blocks) && i<VISIBLE_ROWS;++i) {
        const cJSON *b=cJSON_GetArrayItem(blocks,i);
        lv_obj_t *row=panel_box(list,6,i*88,678,80,0x2A3033,12);
        lv_obj_add_flag(row,LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_opa(row,LV_OPA_80,0);
        lv_obj_add_event_cb(row,select_event,LV_EVENT_CLICKED,(void *)(intptr_t)i);
        char path[80]; snprintf(path,sizeof(path),"icons/%s.png",icons[block_kind(b)]);
        lv_obj_t *im=lv_image_create(row);
        panel_image(im,path);
        lv_obj_set_size(im,36,36); lv_image_set_inner_align(im,LV_IMAGE_ALIGN_STRETCH);
        lv_obj_set_pos(im,17,21);
        text_line(row,str(b,"name"),70,19,370,&app_font_28,0xFFFFFF,LV_TEXT_ALIGN_LEFT);
        items[i]=text_line(row,"",446,22,176,&app_font_24,0xD6DDD9,LV_TEXT_ALIGN_RIGHT);
        text_line(row,">",633,19,26,&app_font_28,0xD6DDD9,LV_TEXT_ALIGN_CENTER);
    }
}

static void rebuild(void)
{
    close_detail();
    view=panel_box(container,0,0,720,640,0,0);
    lv_obj_set_style_bg_opa(view,LV_OPA_TRANSP,0);
    if (selected<0) { build_list(); return; }
    block=cJSON_GetArrayItem(blocks,selected);
    if (!block) return;
    kind=block_kind(block);
    int title_x=26;
    if (cJSON_GetArraySize(blocks)>1) {
        lv_obj_t *b=panel_button(view,"<",15,10,65,55);
        lv_obj_add_event_cb(b,select_event,LV_EVENT_CLICKED,(void *)(intptr_t)-1);
        title_x=93;
    }
    text_line(view,str(block,"name"),title_x,20,kind==LIGHTING ? 460 : 700-title_x,
              &app_font_28,ink,LV_TEXT_ALIGN_LEFT);
    if (kind==LIGHTING) {
        command("Részletek",552,10,150,58,LIGHT_DETAIL);
        list_widgets(106,70);
    } else if (kind==MONITOR) {
        list_widgets(113,68);
    } else if (kind==AUDIO) {
        primary=text_line(view,"",20,90,680,&app_font_34,ink,LV_TEXT_ALIGN_CENTER);
        secondary=text_line(view,"",20,150,680,&app_font_24,ink,LV_TEXT_ALIGN_CENTER);
        command("Előző",28,229,205,72,MUSIC_PREV);
        command("Lejátszás",250,229,220,72,MUSIC_PLAY);
        command("Következő",487,229,205,72,MUSIC_NEXT);
        slider(368);
        command("Kedvencek",227,545,266,54,MUSIC_FAVORITES);
        list_widgets(110,69);
    } else {
        primary=text_line(view,"—",20,185,680,
             kind==IRC || kind==VALUE ? &app_font_numbers_96 : &app_font_34,ink,LV_TEXT_ALIGN_CENTER);
        secondary=text_line(view,"",26,315,668,&app_font_28,ink,LV_TEXT_ALIGN_CENTER);
        if (kind==IRC) {
            command("−",174,480,168,92,LOWER);
            command("+",378,480,168,92,HIGHER);
        } else if (kind==SHADING || kind==GATE || kind==WINDOW) {
            command(kind==SHADING ? "Fel" : "Nyitás",25,415,210,78,UP);
            command("Stop",255,415,210,78,STOP);
            command(kind==SHADING ? "Le" : "Zárás",485,415,210,78,DOWN);
            slider(541);
        } else if (kind==SWITCH || kind==DIMMER || kind==CENTRAL || kind==TIMED) {
            command("Be",36,490,kind==TIMED ? 200 : 304,76,ON);
            command("Ki",kind==TIMED ? 482 : 380,490,kind==TIMED ? 200 : 304,76,OFF);
            if (kind==TIMED) command("Impulzus",258,490,204,76,TRIGGER);
            if (kind==DIMMER) slider(398);
        } else if (kind==SLIDER) slider(477);
        else if (kind==PULSE) command("Aktiválás",160,439,400,102,TRIGGER);
    }
    feedback=text_line(view,"",20,604,680,&app_font_20,ink,LV_TEXT_ALIGN_CENTER);
    panel_controls_update();
}

void panel_controls_open(lv_obj_t *parent,const cJSON *cards,uint32_t foreground,bool night)
{
    container=parent; blocks=cards; ink=foreground; is_night=night;
    selected=cJSON_GetArraySize(cards)==1 ? 0 : -1;
    rev=zld_loxone_revision();
    page=0;
    rebuild();
}

static bool describe(const cJSON *b,kind_t type,char *summary,size_t cap,char *extra,size_t extra_cap,
                     bool full)
{
    const char *id=str(b,"id");
    bool allowed=false;
    snprintf(summary,cap,"—");
    *extra=0;
    if (type==IRC) {
        lx_irc_summary_t s={0};
        if (!app_loxone_irc_summary(id,rev,&s)) return false;
        if (s.actual_valid) snprintf(summary,cap,"%.1f°",s.actual_c);
        const char *mode=s.direction==LX_IRC_DIRECTION_HEATING ? "Fűtés" :
                         s.direction==LX_IRC_DIRECTION_COOLING ? "Hűtés" : "Szobahőmérséklet";
        if (s.target_valid) snprintf(extra,extra_cap,"%s  ·  Célhőmérséklet %.1f °C",mode,s.target_c);
        else snprintf(extra,extra_cap,"%s",mode);
        return s.controllable;
    }
    if (type==LIGHTING) {
        lx_light_summary_t s={0};
        if (!app_loxone_light_summary(id,rev,&s)) return false;
        const char *m=s.label==LX_LIGHT_ONE_MOOD ? s.name : s.label==LX_LIGHT_OFF ? "Kikapcsolva" :
                      s.label==LX_LIGHT_CUSTOM ? "Egyedi" : "—";
        if (s.label==LX_LIGHT_MANY_MOODS) snprintf(summary,cap,"%u aktív kép",s.active_count);
        else snprintf(summary,cap,"%s",m);
        if (full) {
            bool next=false;
            for (unsigned i=0;i<=LIST_ROWS;++i) {
                lx_light_mood_t mood={0};
                bool active=false;
                bool valid=zld_loxone_mood(id,rev,page*LIST_ROWS+i,&mood,&active);
                if (i==LIST_ROWS) next=valid;
                else {
                    ids[i]=mood.id;
                    update_choice(i,valid,mood.name,active,s.controllable);
                }
            }
            visible(pager[0],page>0); visible(pager[1],next);
            if (!choice_valid[0] && page==0 && !*lv_label_get_text(feedback))
                panel_text(feedback,"Világítási képek betöltése…");
        }
        return s.available;
    }
    if (type==SHADING) {
        lx_shading_summary_t s={0};
        bool valid=app_loxone_shading_summary(id,rev,&s);
        if (s.position_valid) snprintf(summary,cap,"%.0f %%",s.position_percent);
        snprintf(extra,extra_cap,"%s",s.safety_active ? "Biztonsági tiltás" : s.moving_up ? "Felfelé" :
                    s.moving_down ? "Lefelé" : s.central ? "Központi árnyékolás" : "");
        if (full) sync_range(valid && !s.central && s.position_valid,s.controllable,0,100,s.position_percent);
        return s.controllable;
    }
    if (type==AUDIO) {
        lx_audio_summary_t *s=calloc(1,sizeof(*s));
        if (!s) return false;
        bool valid=app_loxone_audio_summary(id,rev,s);
        if (valid) {
            snprintf(summary,cap,"%s",*s->track.title ? s->track.title :
                     s->track.mode==LX_AUDIO_MODE_PLAY ? "Lejátszás" : "Szünet");
            snprintf(extra,extra_cap,"%s",*s->track.artist ? s->track.artist : s->track.station);
        }
        if (full) {
            visible(primary,!audio_favorites); visible(secondary,!audio_favorites);
            sync_range(!audio_favorites && valid && s->track.volume_known && s->volume_max,
                       s->controllable,0,s->volume_max,s->track.volume);
            for (unsigned i=0;i<action_count;++i) {
                action_t a=actions[i].action;
                visible(actions[i].button,!audio_favorites || a==MUSIC_FAVORITES);
                if (a==MUSIC_PLAY) panel_text(lv_obj_get_child(actions[i].button,0),
                    s->track.mode==LX_AUDIO_MODE_PLAY ? "Szünet" : "Lejátszás");
                if (a==MUSIC_FAVORITES) panel_text(lv_obj_get_child(actions[i].button,0),
                    audio_favorites ? "Vissza" : "Kedvencek");
            }
            bool list=audio_favorites && favorites && app_loxone_audio_favorites(id,rev,favorites) && favorites->ready;
            for (unsigned i=0;i<LIST_ROWS;++i) {
                unsigned n=page*LIST_ROWS+i;
                bool exists=list && n>=favorites->start && n-favorites->start<favorites->count;
                const lx_audio_favorite_t *f=exists ? &favorites->items[n-favorites->start] : NULL;
                ids[i]=f ? f->id : 0;
                update_choice(i,exists,f ? f->name : "",false,s->controllable);
            }
            visible(pager[0],audio_favorites && page>0);
            visible(pager[1],list && (page+1)*LIST_ROWS<favorites->total);
        }
        allowed=s->controllable;
        free(s);
        return allowed;
    }
    if (type>=DIMMER && type<=WINDOW) {
        lx_controls_summary_t s={0};
        bool ok=app_loxone_controls_summary(id,rev,&s);
        if (s.value_valid) snprintf(summary,cap,"%.*f %s",(int)(s.decimals<4?s.decimals:3),s.value,s.unit);
        else if (type==MONITOR && s.counts_known) snprintf(summary,cap,"%u nyitva",s.counts[0]);
        if (s.timer_valid && s.timer_running) snprintf(extra,extra_cap,"Hátralévő idő %.0f mp",s.timer_seconds);
        else if (s.reason[0]) snprintf(extra,extra_cap,"%s",s.reason);
        if (full) {
            sync_range(ok && s.value_valid && s.range_valid,s.controllable,s.minimum,s.maximum,s.value);
            if (type==MONITOR) {
                for (unsigned i=0;i<LIST_ROWS;++i) {
                    lx_controls_window_t w={0};
                    bool exists=s.list_valid && app_loxone_controls_window(id,rev,page*LIST_ROWS+i,&w);
                    char row[170];
                    snprintf(row,sizeof(row),"%s   ·   %s",w.name,!w.valid ? "Ismeretlen" :
                        (w.state&4U) ? "Nyitva" : (w.state&2U) ? "Billentve" : "Zárva");
                    update_choice(i,exists,row,false,false);
                    if (exists) lv_obj_remove_flag(choice[i],LV_OBJ_FLAG_CLICKABLE);
                }
                visible(pager[0],page>0); visible(pager[1],s.window_count>(page+1)*LIST_ROWS);
            }
        }
        return s.controllable;
    }
    lx_home_value_t v={0};
    bool ok=zld_loxone_card(id,&v,&allowed);
    if (ok && v.valid) {
        if (type==SWITCH) snprintf(summary,cap,"%s",v.value!=0 ? "Bekapcsolva" : "Kikapcsolva");
        else {
            snprintf(summary,cap,"%.1f",v.value);
            snprintf(extra,extra_cap,"%s",str(b,"unit"));
        }
    } else if (type==PULSE) snprintf(summary,cap,"Nyomógomb");
    return allowed;
}

void panel_controls_update(void)
{
    if (!view || !blocks) return;
    unsigned current=zld_loxone_revision();
    if (rev!=current) {
        rev=current;
        rebuild();
        return;
    }
    if (panel_lighting_is_open()) { panel_lighting_update(); return; }
    char value[180], extra[180];
    if (selected<0) {
        for (int i=0;i<cJSON_GetArraySize(blocks) && i<VISIBLE_ROWS;++i) {
            const cJSON *row=cJSON_GetArrayItem(blocks,i);
            describe(row,block_kind(row),value,sizeof(value),extra,sizeof(extra),false);
            panel_text(items[i],value);
        }
        return;
    }
    if (!block) return;
    if (feedback_tick && lv_tick_elaps(feedback_tick)>3000) {
        feedback_tick=0;
        panel_text(feedback,"");
    }
    bool allowed=describe(block,kind,value,sizeof(value),extra,sizeof(extra),true);
    panel_text(primary,value);
    panel_text(secondary,extra);
    for (unsigned i=0;i<action_count;++i)
        enabled(actions[i].button,allowed || (audio_favorites && actions[i].action==MUSIC_FAVORITES));
}
