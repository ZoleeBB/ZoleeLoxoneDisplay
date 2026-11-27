#pragma once
#include "panel_config.h"

/* The v1 AES-GCM format originates in ZoleeEnergyFlowDisplay/app_loxone_store.c. */
bool panel_secret_crypt(char password[PANEL_SECRET_CAP], const char *associated,
                        cJSON *root, bool encrypt);
