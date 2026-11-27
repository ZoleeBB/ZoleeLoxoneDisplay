#pragma once
#include <stdbool.h>
#include <time.h>
extern bool fixture_online, fixture_assets, fixture_complete;
extern unsigned fixture_generation, fixture_brightness, fixture_calls;
extern int fixture_page;
extern bool fixture_matched;
extern const char *fixture_profile, *fixture_sd;
extern time_t fixture_time;
extern bool fixture_use_night_value, fixture_night_value, fixture_night_valid;
