#include "panel_palette.h"
#include <math.h>

/* Relative luminance uses linear-light sRGB; a saturated blue and a saturated
 * yellow of equal peak intensity need opposite label colors. Run on tab open. */
static double linear_channel(uint32_t byte)
{
    double v = byte / 255.0;
    return v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4);
}

static double luminance(uint32_t color)
{
    return 0.2126 * linear_channel((color >> 16) & 255) +
           0.7152 * linear_channel((color >> 8) & 255) + 0.0722 * linear_channel(color & 255);
}

static double contrast(double a, double b)
{
    return (fmax(a,b) + 0.05) / (fmin(a,b) + 0.05);
}

static uint32_t blend(uint32_t from, uint32_t to, unsigned percent)
{
    uint32_t rgb = 0;
    for (unsigned shift = 0; shift <= 16; shift += 8) {
        unsigned a = (from >> shift) & 255, b = (to >> shift) & 255;
        rgb |= ((a * (100 - percent) + b * percent + 50) / 100) << shift;
    }
    return rgb;
}

/* A shared ink keeps row captions and their icons readable while pressed.
 * Use the true endpoints in the narrow midtone region where required. */
static uint32_t text_color(double background)
{
    bool dark_ink = contrast(background,0) > contrast(background,1);
    uint32_t soft = dark_ink ? 0x111315 : 0xF8F9FA;
    if (contrast(background,luminance(soft)) >= 4.6) return soft;
    return dark_ink ? 0 : 0xFFFFFF;
}

static uint32_t secondary(uint32_t background, uint32_t ink)
{
    double base = luminance(background);
    for (unsigned weight = 70; weight < 100; weight += 5) {
        uint32_t candidate = blend(background, ink, weight);
        if (contrast(base,luminance(candidate)) >= 4.6) return candidate;
    }
    return ink;
}

panel_palette_t panel_palette_make(uint32_t background, bool photo)
{
    panel_palette_t p;
    /* Photo tabs have a black scrim and opaque neutral cards. */
    p.page = photo ? 0x172122 : background;
    double y = luminance(p.page);
    p.ink = text_color(y);
    p.muted = photo ? p.ink : secondary(p.page,p.ink);
    p.surface = photo ? 0x2A3436 : y < 0.08 ? blend(p.page,0xFFFFFF,18) : blend(p.page,0,17);
    double surface_y = luminance(p.surface);
    p.surface_ink = text_color(surface_y);
    p.surface_muted = secondary(p.surface,p.surface_ink);
    bool dark_ink = luminance(p.surface_ink) < surface_y;
    p.pressed = blend(p.surface, dark_ink ? 0xFFFFFF : 0, 28);
    /* An active scene uses inverted contrast, plus a leading stripe; a
     * checked switch and slider thumb use the same high-contrast color. */
    p.selected = p.surface_muted;
    double selected_y = luminance(p.selected);
    p.selected_ink = text_color(selected_y);
    p.selected_pressed = blend(p.selected, luminance(p.selected_ink) < selected_y ? 0xFFFFFF : 0, 22);
    return p;
}
