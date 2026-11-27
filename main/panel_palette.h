#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t page, ink, muted;
    uint32_t surface, surface_ink, surface_muted, pressed;
    uint32_t selected, selected_ink, selected_pressed;
} panel_palette_t;

panel_palette_t panel_palette_make(uint32_t background, bool photo);
