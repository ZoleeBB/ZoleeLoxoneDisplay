#include "panel_palette.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static double component(unsigned n)
{
    double srgb = (n & 255u) / 255.0;
    return srgb <= 0.04045 ? srgb / 12.92 : pow((srgb + 0.055) / 1.055, 2.4);
}

static double brightness(uint32_t rgb)
{
    return component(rgb >> 16) * 0.2126 + component(rgb >> 8) * 0.7152 + component(rgb) * 0.0722;
}

static void readable(uint32_t background, uint32_t foreground)
{
    double a = brightness(background) + 0.05, b = brightness(foreground) + 0.05;
    assert((a >= b ? a / b : b / a) >= 4.5);
}

static void check_colors(panel_palette_t p)
{
    readable(p.page, p.ink);
    readable(p.page, p.muted);
    readable(p.surface, p.surface_ink);
    readable(p.surface, p.surface_muted);
    readable(p.pressed, p.surface_ink);
    readable(p.pressed, p.surface_muted);
    readable(p.surface, p.selected);
    readable(p.selected, p.selected_ink);
    readable(p.selected_pressed, p.selected_ink);
    assert(p.surface != p.pressed);
}

int main(void)
{
    const uint32_t dark[] = {0x000000, 0x11202E, 0x0000FF};
    const uint32_t light[] = {0xFFFFFF, 0xDFD3C7, 0xAD4C56, 0xFF0000, 0x00FF00, 0xFF00FF, 0xFFFF00};
    for (unsigned i = 0; i < sizeof dark / sizeof *dark; ++i) {
        panel_palette_t p = panel_palette_make(dark[i], false);
        assert(p.page == dark[i] && brightness(p.surface) > brightness(p.page));
        check_colors(p);
    }
    for (unsigned i = 0; i < sizeof light / sizeof *light; ++i) {
        panel_palette_t p = panel_palette_make(light[i], false);
        assert(p.page == light[i] && brightness(p.surface) < brightness(p.page));
        check_colors(p);
    }
    for (unsigned r = 0; r <= 255; r += 17)
        for (unsigned g = 0; g <= 255; g += 17)
            for (unsigned b = 0; b <= 255; b += 17)
                check_colors(panel_palette_make((r << 16) | (g << 8) | b, false));

    panel_palette_t photo = panel_palette_make(0xFFFFFF, true);
    assert(photo.surface == panel_palette_make(0, true).surface);
    /* 60% black scrim over even a pure white photo leaves ~40% sRGB. */
    readable(0x666666, photo.ink);
    readable(0x666666, photo.muted);
    check_colors(photo);
    puts("Adaptive palette and text contrast passed for 4096 colors and photo tabs");
    return 0;
}
