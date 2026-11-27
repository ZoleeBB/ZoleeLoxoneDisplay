/* Real structure parser, initial state cache, profile/command model and serialized
 * worker. Mock only wall time, semaphore and the authenticated transport reply. */
#include "panel_profile.h"
#include "zld_loxone.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <stdlib.h>

#define ID(i) "000000" i "-0000-0000-0000000000000000"
#define LIGHT ID("01")
#define VI ID("02")
#define SWITCH ID("03")
#define BUTTON ID("04")
#define SHADE ID("05")
#define AUDIO ID("06")
#define SECOND_LIGHT ID("10")
#define OTHER_MOODS ID("16")
#define LOCKED ID("07")
#define CHILD ID("08")
#define ALIAS ID("09")
#define MOODS ID("11")
#define ACTIVE ID("12")
#define MODE ID("13")
#define LOCK ID("14")
#define SAFE ID("15")

static const char structure[] =
    "{\"mediaServer\":{\"ms\":{\"type\":2,\"localIP\":\"192.0.2.8\"}},\"controls\":{"
    "\"" LIGHT "\":{\"name\":\"Light\",\"type\":\"LightControllerV2\",\"states\":{\"moodList\":\"" MOODS "\",\"jLocked\":\"" LOCK "\"}},"
    "\"" SECOND_LIGHT "\":{\"type\":\"LightControllerV2\",\"states\":{\"moodList\":\"" OTHER_MOODS "\"}},"
    "\"" VI "\":{\"type\":\"Pushbutton\",\"uuidAction\":\"" ALIAS "\",\"details\":{\"type\":71}},"
    "\"" SWITCH "\":{\"type\":\"Switch\",\"states\":{\"active\":\"" ACTIVE "\"}},"
    "\"" BUTTON "\":{\"type\":\"Pushbutton\"},"
    "\"" SHADE "\":{\"type\":\"Jalousie\",\"details\":{\"isAutomatic\":true},\"states\":{\"safetyActive\":\"" SAFE "\"}},"
    "\"" AUDIO "\":{\"type\":\"AudioZoneV2\",\"details\":{\"server\":\"ms\",\"playerid\":1},\"states\":{\"playState\":\"" MODE "\"}},"
    "\"" LOCKED "\":{\"type\":\"Switch\",\"restrictions\":2,\"subControls\":{"
    "\"" CHILD "\":{\"type\":\"Pushbutton\",\"details\":{\"type\":71}}}}}}";
static panel_profile_t profile, *s_profile = &profile;
static lx_home_config_t *s_home = &profile.home;
static lx_home_values_t s_home_values;
static bool s_intercom_ringing, inside_call;
static unsigned s_intercom_generation, published_generation=11;
static uint32_t s_config_generation(void) { return published_generation; }
static bool intercom_held(unsigned revision) { (void)revision; return inside_call; }
static cJSON *s_catalog;
static int s_lock = 1;
static bool s_home_live = true, s_dead, s_home_busy;
static int s_refresh;
static unsigned s_home_session = 41, nsend;
#define LX_KEEPALIVE_US 15000000LL
#define CATALOG_IDLE 0
static int64_t clock_us = 1100000, s_home_response = 1000000;
static bool s_network_ready(void) { return true; }
static int64_t esp_timer_get_time(void) { return clock_us; }
#define portMAX_DELAY 1
#define xSemaphoreTake(lock,ticks) ((void)(lock),(void)(ticks))
#define xSemaphoreGive(lock) ((void)(lock))
#define pdMS_TO_TICKS(ms) (ms)
static bool ring_during_sleep;
static unsigned incoming;
static void vTaskDelay(unsigned ms) {
    clock_us += 1000 * ms;
    if (ring_during_sleep) incoming = 1;
}
#define LX_QUEUE_DEPTH 8
#define pdTRUE 1
static int s_messages;
typedef struct { int type; uint8_t *data; size_t length; } message_t;
static void mark_response(void) { s_home_response = clock_us; }
static void free_message(message_t *m) { (void)m; }
static unsigned uxQueueMessagesWaiting(int q) { (void)q; return incoming; }
static int xQueueReceive(int q, message_t *m, unsigned wait)
{
    (void)q; (void)wait;
    if (!incoming) return 0;
    --incoming; *m=(message_t){0}; return pdTRUE;
}
static bool update_values(int type, const uint8_t *bytes, size_t size)
{
    (void)type; (void)bytes; (void)size; s_intercom_ringing=true;
    zld_touch_cancel();
    return true;
}
#include "app_loxone_lighting_cache.inc"
static int abort_on_ack, delay_on_ack, drop_on_ack, replace_on_ack, ring_on_ack;
static char commands[32][96];
static int64_t timestamps[32];
static cJSON *receive_reply(const char *command, bool encrypted, bool match_io)
{
    assert(encrypted && match_io && nsend < 32);
    timestamps[nsend] = clock_us;
    strcpy(commands[nsend++], command);
    clock_us += delay_on_ack ? delay_on_ack : 20000;
    if (replace_on_ack) ++published_generation;
    if (ring_on_ack) s_intercom_ringing = true;
    if (abort_on_ack) zld_touch_cancel();
    return drop_on_ack ? NULL : cJSON_CreateNull();
}
#include "panel_touch_service.inc"

static void mapping(unsigned zone, const char *type, const char *action, const char *id)
{
    lx_touch_mapping_t *m = &profile.touch.zones[zone];
    memset(m,0,sizeof(*m));
    strcpy(m->type,type); strcpy(m->action,action); strcpy(m->uuid,id);
}
static void check(unsigned zone, const char *wanted)
{
    char out[96];
    bool valid = lx_touch_command(s_catalog, &profile.touch.zones[zone], touch_value_locked, out);
    assert(valid == (wanted != NULL));
    if (valid) assert(!strcmp(out,wanted));
}
static void dispatch(unsigned taps)
{
    assert(zld_touch_press(11, 41, LX_TOUCH_CENTER, taps) == ESP_OK);
    touch_poll();
}

int main(void)
{
    (void)lighting_cache_replay_block; (void)lighting_cache_replay_irc;
    (void)lighting_cache_replay_shading; (void)lighting_cache_replay_controls; (void)lighting_cache_replay_page;
    cJSON *settings = cJSON_Parse("{\"revision\":11,\"touch\":{\"enabled\":true,\"corners\":["
        "{\"type\":\"switch\",\"uuid\":\"" SWITCH "\",\"action\":\"trigger\",\"inputUuid\":\"" VI "\"},"
        "{\"type\":\"pulse\",\"uuid\":\"" BUTTON "\",\"action\":\"trigger\"},"
        "{\"type\":\"shading\",\"uuid\":\"" SHADE "\",\"action\":\"up\"},"
        "{\"type\":\"audio\",\"uuid\":\"" AUDIO "\",\"action\":\"playPause\"}],"
        "\"center\":{\"uuid\":\"" LIGHT "\"}}}");
    assert(settings && panel_profile_read(&profile,settings,"",""));
    cJSON_Delete(settings);
    assert(!profile.home.card_count && !profile.home.page_count);
    s_catalog = lx_catalog_parse(structure,sizeof(structure)-1);
    assert(s_catalog);
    lighting_cache_prepare();
    const char *raw = "[{\"id\":0,\"name\":\"Off\"},{\"id\":17,\"name\":\"Reading\"}]";
    lighting_cache_store(MOODS,false,0,raw,strlen(raw));
    lighting_cache_store(LOCK,false,0,NULL,0);
    lighting_cache_store(ACTIVE,true,1,NULL,0);
    lighting_cache_store(MODE,true,2,NULL,0);
    lighting_cache_store(SAFE,true,0,NULL,0);
    cJSON *moods = zld_touch_moods(LIGHT);
    assert(!strcmp(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(moods,"status")),"ready"));
    assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(moods,"moods"))==2);
    cJSON_Delete(moods);
    lighting_cache_store(OTHER_MOODS,false,0,raw,strlen(raw));
    moods=zld_touch_moods(SECOND_LIGHT);
    assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(moods,"moods"))==2);
    cJSON_Delete(moods);

    check(0,"jdev/sps/io/" ALIAS "/pulse");
    profile.touch.zones[0].input_uuid[0] = 0;
    check(0,"jdev/sps/io/" SWITCH "/off");
    lighting_cache_store(ACTIVE,true,0,NULL,0);
    check(0,"jdev/sps/io/" SWITCH "/on");
    check(1,"jdev/sps/io/" BUTTON "/pulse");
    check(2,"jdev/sps/io/" SHADE "/FullUp");
    strcpy(profile.touch.zones[2].action,"down");
    check(2,"jdev/sps/io/" SHADE "/FullDown");
    lighting_cache_store(SAFE,true,1,NULL,0);
    check(2,NULL);
    check(3,"jdev/sps/io/" AUDIO "/Pause");
    lighting_cache_store(MODE,true,0,NULL,0);
    check(3,"jdev/sps/io/" AUDIO "/play");
    const char *const options[][2] = {{"volumeUp","volUp"},{"volumeDown","volDown"},{"next","next"},{"previous","prev"}};
    for (unsigned i=0; i<4; ++i) {
        char path[96];
        snprintf(path,sizeof(path),"jdev/sps/io/%s/%s",AUDIO,options[i][1]);
        strcpy(profile.touch.zones[3].action,options[i][0]); check(3,path);
    }
    mapping(0,"lighting","previous",LIGHT);
    check(0,"jdev/sps/io/" LIGHT "/minus");
    strcpy(profile.touch.zones[0].action,"scene"); profile.touch.zones[0].scene_id=17;
    check(0,"jdev/sps/io/" LIGHT "/changeTo/17");
    profile.touch.zones[0].scene_id=18; check(0,NULL);
    strcpy(profile.touch.zones[4].input_uuid,CHILD); check(4,NULL);
    strcpy(profile.touch.zones[4].input_uuid,BUTTON); check(4,NULL);
    strcpy(profile.touch.zones[4].input_uuid,VI);
    dispatch(3);
    assert(nsend==3);
    for (unsigned i=0;i<3;++i) {
        assert(!strcmp(commands[i],"jdev/sps/io/" ALIAS "/pulse"));
        if (i) assert(timestamps[i]-timestamps[i-1]>=140000 && timestamps[i]-timestamps[i-1]<=250000);
    }
    profile.touch.zones[4].input_uuid[0]=0;
    dispatch(1); dispatch(2);
    assert(nsend==6 && !strcmp(commands[5],"jdev/sps/io/" LIGHT "/plus"));
    assert(!zld_touch_session(10) && zld_touch_session(11)==41);
    assert(zld_touch_press(11,40,4,1)==ESP_ERR_INVALID_STATE);
    assert(zld_touch_press(11,41,2,2)==ESP_ERR_INVALID_ARG);
    assert(zld_touch_press(11,41,1,1)==ESP_OK);
    zld_touch_cancel(); touch_poll(); assert(nsend==6);
    abort_on_ack=1; dispatch(3); assert(nsend==7); abort_on_ack=0;
    delay_on_ack=300000; dispatch(3); assert(nsend==8); delay_on_ack=0;
    lighting_cache_store(LOCK,false,0,"{\"locked\":1}",12);
    assert(zld_touch_press(11,41,4,3)==ESP_ERR_INVALID_STATE && nsend==8);
    lighting_cache_store(LOCK,false,0,NULL,0);
    replace_on_ack=1; dispatch(3); assert(nsend==9 && !zld_touch_session(11));
    replace_on_ack=0; published_generation=11;
    ring_on_ack=1; dispatch(3); assert(nsend==10 && !zld_touch_session(11));
    ring_on_ack=s_intercom_ringing=0;
    ring_during_sleep=true; dispatch(3); assert(nsend==11 && !zld_touch_session(11));
    ring_during_sleep=s_intercom_ringing=false;
    drop_on_ack=1; dispatch(3); assert(s_dead && nsend==12 && !zld_touch_session(11));
    assert(lx_touch_ack_matches("jdev/sps/io/" VI "/pulse","/dev/sps/io/" VI "/pulse"));
    assert(lx_touch_ack_matches("jdev/sps/io/" VI "/pulse","dev/sps/io/" VI));
    assert(!lx_touch_ack_matches("jdev/sps/io/" VI "/pulse","dev/sps/io/" BUTTON "/pulse"));
    assert(!lx_touch_ack_matches("jdev/sps/io/" VI "/pulse","dev/sps/io/" VI "/on"));
    lighting_cache_clear(); cJSON_Delete(s_catalog);
    puts("Touch commands passed, including card-free scenes, fresh locks, authenticated pulse order and cancellation");
}
