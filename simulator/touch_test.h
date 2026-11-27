#pragma once
#include "lvgl.h"
#include <stdbool.h>

/* A gazdagép virtuális mutatója ugyanazt az LVGL bemeneti utat járja be, mint az érintőpanel. */
void sim_contact(int x, int y, lv_indev_state_t state);
void step(unsigned ms);
void snapshot(const char *name);
void assert_standby(const char *clock, bool night);
void assert_awake_tabs(void);
void test_touch_saver(void);
