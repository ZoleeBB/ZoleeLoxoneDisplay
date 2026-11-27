#pragma once
#include <stdio.h>
#define ESP_LOGW(tag,msg) fprintf(stderr,"%s: %s\n",tag,msg)
