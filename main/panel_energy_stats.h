#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    double production, consumption, imported, exported, charged, discharged;
    double pv_home, pv_stored, bat_home;
    unsigned seen; /* 0 hálózat, 1 akku, 2 PV, 3 ház, 4 megoszlás */
} panel_daily_t;

void panel_energy_stats_tick(void);
bool panel_energy_stats_read(unsigned tab, unsigned generation, panel_daily_t *out, bool *live);
