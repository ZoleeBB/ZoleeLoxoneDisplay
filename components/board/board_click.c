#include "board_click.h"
#include <limits.h>

/* Original 6 ms mechanical impulse at 24 kHz. Two damped resonances and a
 * filtered transient; the peak is 1320/32768 (-27.9 dBFS). No SD or decoding. */
static const int16_t impulse[] = {
    0, 464, 1299, 180, 627, 1320, -698, -77, 468, -829, 510, -1042,
    -1241, 20, -738, -332, 1151, 710, 654, 346, 311, -693, 401, -295,
    -293, 33, -366, -180, -267, -213, -84, 364, 318, 441, 221, -24,
    -143, -194, 43, -157, 93, -254, 36, -272, 86, -24, 157, 132,
    116, 83, 42, -103, -61, -6, -90, -8, -17, -38, -60, 34,
    6, 29, 82, 38, 41, -19, -21, -19, -20, -32, -2, -7,
    -9, 1, -23, 29, 18, 37, 17, -8, -4, 1, -27, -7,
    -16, 5, -4, -2, -9, 13, 5, 9, 10, 4, 2, 3,
    -12, -8, -2, -7, 4, -5, 5, -4, 0, 4, 6, 1,
    6, 2, -5, -2, -5, -2, 0, -1, 0, 0, 2, -1,
    2, 2, 1, 2, -1, 0, -2, -3, -1, -1, 0, 1,
    0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

unsigned board_click_contacts(uint16_t *previous, uint16_t active)
{
    uint16_t added = active & ~*previous;
    *previous = active;
    return (unsigned)__builtin_popcount((unsigned)added);
}

/* remaining belongs only to the single output writer. The touch sampler
 * increments pending without the codec lock, including during media writes. */
bool board_click_pending(const board_click_mixer_t *m)
{
    return m->remaining || atomic_load(&m->pending);
}

bool board_click_mix(board_click_mixer_t *m, int16_t *stereo, size_t frames)
{
    const unsigned length = sizeof(impulse) / sizeof(impulse[0]);
    bool mixed = false;
    for (size_t i = 0; i < frames; ++i) {
        if (!m->remaining) {
            if (!atomic_load(&m->pending)) break;
            atomic_fetch_sub(&m->pending, 1);
            m->remaining = length;
        }
        int16_t pulse = impulse[length - m->remaining--];
        for (unsigned channel = 0; channel < 2; ++channel) {
            int value = stereo[2*i + channel] + pulse;
            stereo[2*i + channel] = value > INT16_MAX ? INT16_MAX :
                                     value < INT16_MIN ? INT16_MIN : (int16_t)value;
        }
        mixed = true;
    }
    return mixed;
}
