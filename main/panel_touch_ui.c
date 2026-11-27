#include "panel_touch_ui.h"
#include "panel_config.h"
#include "panel_intercom_ui.h"
#include "panel_ui_style.h"
#include "zld_loxone.h"
#include <stdlib.h>

enum {
    SIDE = 720, MID = SIDE / 2,
    UNLOCK_HEIGHT = 52, UNLOCK_HALF_WIDTH = 112, UNLOCK_RISE = 90, UNLOCK_DRIFT = 60,
    TAP_DRIFT = 24, TAP_HOLD_MS = 600
};
enum { TOP_LEFT, TOP_RIGHT, BOTTOM_LEFT, BOTTOM_RIGHT, CENTER };
typedef enum { IDLE, KEY_DOWN, SWIPE, CANCELED } contact_t;

static struct {
    unsigned generation, window_ms, zone, taps;
    uint32_t session, down_at, last_release;
    lv_point_t origin;
    lv_indev_t *finger;
    contact_t contact;
    void (*unlock)(void);
} touch;

static bool on_unlock_rail(lv_point_t p)
{
    return p.y >= SIDE - UNLOCK_HEIGHT && p.x >= MID - UNLOCK_HALF_WIDTH && p.x <= MID + UNLOCK_HALF_WIDTH;
}

static unsigned zone_at(lv_point_t p)
{
    if (abs(p.x - MID) + abs(p.y - MID) <= MID)
        return CENTER;
    if (p.y < MID)
        return p.x < MID ? TOP_LEFT : TOP_RIGHT;
    return p.x < MID ? BOTTOM_LEFT : BOTTOM_RIGHT;
}

static lv_obj_t *draw_boundaries(lv_obj_t *layer)
{
    static const lv_point_precise_t diamond[] = {
        {MID, 0}, {SIDE, MID}, {MID, SIDE}, {0, MID}, {MID, 0}
    };
    lv_obj_t *stroke = lv_line_create(layer);
    lv_line_set_points(stroke, diamond, sizeof(diamond) / sizeof(diamond[0]));
    lv_obj_set_style_line_width(stroke, 3, 0);
    lv_obj_remove_flag(stroke, LV_OBJ_FLAG_CLICKABLE);
    panel_box(layer, MID - 52, 704, 104, 5, 0x515356, 3);
    return stroke;
}

static bool current_view(void)
{
    return touch.generation == panel_config_generation() && !panel_intercom_active();
}

static void submit(unsigned zone, unsigned taps)
{
    if (touch.session && current_view() && zld_touch_session(touch.generation) == touch.session)
        zld_touch_press(touch.generation, touch.session, zone, taps);
}

static void finish_center(void)
{
    if (touch.taps)
        submit(CENTER, touch.taps);
    touch.taps = 0;
}

static void begin(lv_point_t p, lv_indev_t *finger, uint32_t now)
{
    touch.finger = finger;
    touch.origin = p;
    touch.down_at = now;
    /* Az alsó jelzésről indított húzás kapcsolat nélkül is feloldhatja a képernyőt. */
    if (on_unlock_rail(p)) {
        touch.taps = 0;
        touch.contact = SWIPE;
        return;
    }

    uint32_t session = current_view() ? zld_touch_session(touch.generation) : 0;
    if (session != touch.session)
        touch.taps = 0;
    touch.session = session;
    touch.zone = zone_at(p);
    if (touch.taps && now - touch.last_release > touch.window_ms)
        finish_center();
    touch.contact = session ? KEY_DOWN : CANCELED;
}

static void motion(lv_point_t p)
{
    int dx = p.x - touch.origin.x, dy = p.y - touch.origin.y;
    if (p.x < 0 || p.y < 0 || p.x >= SIDE || p.y >= SIDE)
        touch.contact = CANCELED;
    if (touch.contact == SWIPE && (abs(dx) > UNLOCK_DRIFT || dy > TAP_DRIFT))
        touch.contact = CANCELED;
    if (touch.contact == KEY_DOWN && (abs(dx) > TAP_DRIFT || abs(dy) > TAP_DRIFT ||
        dx * dx + dy * dy > TAP_DRIFT * TAP_DRIFT || on_unlock_rail(p) || zone_at(p) != touch.zone))
        touch.contact = CANCELED;
    if (touch.contact == CANCELED)
        touch.taps = 0;
}

static void end(lv_point_t p, uint32_t now)
{
    contact_t contact = touch.contact;
    touch.finger = NULL;
    touch.contact = IDLE;
    if (contact == SWIPE && touch.origin.y - p.y >= UNLOCK_RISE && current_view()) {
        touch.unlock();
        return;
    }
    if (contact != KEY_DOWN || now - touch.down_at > TAP_HOLD_MS || !current_view() ||
        touch.session != zld_touch_session(touch.generation)) {
        touch.taps = 0;
        return;
    }
    if (touch.zone != CENTER) {
        finish_center();
        submit(touch.zone, 1);
        return;
    }
    touch.last_release = now;
    if (++touch.taps == 3)
        finish_center();
}

static void input(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    lv_indev_t *finger = lv_indev_active();
    if (!finger || lv_indev_get_type(finger) != LV_INDEV_TYPE_POINTER)
        return;
    lv_point_t point;
    lv_indev_get_point(finger, &point);
    uint32_t now = lv_tick_get();
    if (code == LV_EVENT_PRESSED) {
        begin(point, finger, now);
    } else if (touch.finger == finger) {
        if (code == LV_EVENT_PRESS_LOST) {
            touch.taps = 0;
            touch.contact = IDLE;
            touch.finger = NULL;
        } else {
            motion(point);
            if (code == LV_EVENT_RELEASED)
                end(point, now);
        }
    }
}

lv_obj_t *panel_touch_open(lv_obj_t *layer, unsigned generation, unsigned tap_window_ms, void (*unlock)(void))
{
    touch.generation = generation;
    touch.window_ms = tap_window_ms;
    touch.unlock = unlock;
    lv_obj_add_flag(layer, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(layer, input, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(layer, input, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(layer, input, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(layer, input, LV_EVENT_PRESS_LOST, NULL);
    return draw_boundaries(layer);
}

void panel_touch_close(void)
{
    if (!touch.unlock) return;
    touch.unlock = NULL;
    zld_touch_cancel();
    if (touch.finger)
        lv_indev_wait_release(touch.finger);
    touch.finger = NULL;
    touch.contact = IDLE;
    touch.taps = 0;
    touch.session = 0;
}

void panel_touch_tick(void)
{
    /* A második érintés még az időablakban kezdődhetett, de később érhet véget. */
    if (touch.taps && touch.contact == IDLE && lv_tick_elaps(touch.last_release) > touch.window_ms)
        finish_center();
}
