#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

typedef struct {
    atomic_uint pending;
    unsigned remaining;
} board_click_mixer_t;

unsigned board_click_contacts(uint16_t *previous, uint16_t active);
bool board_click_pending(const board_click_mixer_t *m);
bool board_click_mix(board_click_mixer_t *m, int16_t *stereo, size_t frames);
