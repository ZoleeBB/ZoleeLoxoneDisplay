#pragma once
#include <stdbool.h>
#include <time.h>
#include <stdint.h>
extern bool fixture_online, fixture_assets, fixture_complete;
extern unsigned fixture_generation, fixture_brightness, fixture_calls;
extern int fixture_page;
extern bool fixture_matched;
extern const char *fixture_profile, *fixture_sd;
extern time_t fixture_time;
extern bool fixture_use_night_value, fixture_night_value, fixture_night_valid;

extern bool fixture_touch, fixture_doorbell;
extern unsigned fixture_tap_window;
extern uint32_t fixture_session;
typedef struct {
    unsigned zone, taps;
    uint32_t session, at_ms;
} fixture_touch_event_t;
extern fixture_touch_event_t fixture_touch_trace[48];
extern unsigned fixture_touch_count;
extern unsigned fixture_touch_cancels;
extern bool fixture_has_touch_section;
