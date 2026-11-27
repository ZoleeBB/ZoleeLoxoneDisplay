#include "touch_test.h"
#include "fixture.h"
#include <assert.h>
#include <stdio.h>

enum { TL, TR, BL, BR, CENTER };
static unsigned consumed;

static void down(int x, int y) { sim_contact(x, y, LV_INDEV_STATE_PRESSED); }
static void up(int x, int y) { sim_contact(x, y, LV_INDEV_STATE_RELEASED); }

static void contact(int x, int y)
{
    down(x, y);
    step(60);
    up(x, y);
}

static void idle(void)
{
    step(9200);
    assert(fixture_page == -1 && fixture_brightness == 10);
}

static void quiet(void)
{
    assert(fixture_touch_count == consumed);
}

static void expect(unsigned zone, unsigned taps)
{
    assert(fixture_touch_count == consumed + 1);
    const fixture_touch_event_t *event = &fixture_touch_trace[consumed++];
    assert(event->zone == zone && event->taps == taps && event->session == fixture_session);
    assert(fixture_page == -1 && fixture_brightness == 10);
}

static void expect_at(int x, int y, unsigned zone)
{
    contact(x, y);
    if (zone == CENTER) step(fixture_tap_window + 60);
    expect(zone, 1);
}

static void unlock(void)
{
    down(360, 698);
    step(40);
    down(355, 620);
    step(40);
    down(350, 530);
    assert(fixture_page == -1);
    up(350, 530);
    step(500);
    assert(fixture_page == 3);
    assert_awake_tabs();
    quiet();
}

static void zones_and_edges(void)
{
    expect_at(100, 130, TL);
    expect_at(600, 130, TR);
    expect_at(110, 530, BL);
    expect_at(600, 530, BR);
    /* Mindkét oldalon pontosan a ferde határvonal két szomszédos képpontja. */
    expect_at(179, 180, TL);
    expect_at(180, 180, CENTER);
    expect_at(540, 180, CENTER);
    expect_at(541, 180, TR);
    expect_at(0, 359, TL);
    expect_at(0, 360, CENTER);
    expect_at(0, 361, BL);
    expect_at(719, 358, TR);
    expect_at(719, 359, CENTER);
    expect_at(719, 360, CENTER);
    expect_at(719, 362, BR);
    expect_at(359, 0, TL);
    expect_at(360, 0, CENTER);
    expect_at(361, 0, TR);
    expect_at(360, 667, CENTER);
    expect_at(100, 667, BL);
    expect_at(100, 668, BL);
    expect_at(40, 700, BL);
    expect_at(680, 700, BR);
    expect_at(247, 700, BL);
    expect_at(473, 700, BR);
    contact(248, 668);
    contact(472, 668);
    quiet();
    assert(fixture_page == -1);
}

static void groups(unsigned window)
{
    fixture_tap_window = window;
    fixture_generation++;
    idle();

    contact(360, 360);
    uint32_t released = lv_tick_get();
    step(window);
    quiet(); /* A határidőig a következő érintés még ugyanebbe a csoportba tartozik. */
    step(60);
    expect(CENTER, 1);
    uint32_t elapsed = fixture_touch_trace[consumed - 1].at_ms - released;
    assert(elapsed > window && elapsed <= window + 60);

    contact(360, 360);
    step(window);
    down(360, 360);
    step(120);
    quiet();
    up(360, 360);
    step(window + 60);
    expect(CENTER, 2);

    for (unsigned i = 0; i < 3; ++i) {
        contact(360, 360);
        if (i != 2) {
            step(window - 10);
            quiet();
        }
    }
    expect(CENTER, 3);
    step(window + 60);
    quiet();

    /* Két, az ablakon kívül eső érintés két önálló parancs. */
    contact(360, 360);
    step(window + 60);
    expect(CENTER, 1);
    contact(360, 360);
    step(window + 60);
    expect(CENTER, 1);
}

static void between_zones(void)
{
    contact(360, 360);
    step(30);
    contact(100, 130);
    assert(fixture_touch_count == consumed + 2);
    const fixture_touch_event_t *center = &fixture_touch_trace[consumed++];
    assert(center->zone == CENTER && center->taps == 1 && center->session == fixture_session);
    expect(TL, 1);
    contact(100, 130);
    expect(TL, 1);
    step(fixture_tap_window + 60);
    quiet();
}

static void canceled_drags(void)
{
    /* Egy korábbi érvényes középső koppintás se fusson le a megszakított sor végén. */
    contact(360, 360);
    step(40);
    down(360, 360);
    step(40);
    down(391, 360);
    down(360, 360);
    up(360, 360);
    step(fixture_tap_window + 60);
    quiet();
    down(179, 180); /* Átlépi a zónahatárt, de a mozgás csak két képpont. */
    down(181, 180);
    up(181, 180);
    down(100, 130);
    step(650);
    up(100, 130);
    contact(360, 360);
    down(360, 360);
    step(650);
    up(360, 360);
    step(fixture_tap_window + 60);
    quiet();
    down(360, 698);
    down(290, 675);
    down(360, 550);
    up(360, 550);
    down(360, 698);
    down(365, 620); /* A 78 képpontos emelés rövid. */
    up(365, 620);
    down(245, 700); /* A zónából a feloldósávba átlépve a közeli koppintás is megszakad. */
    down(250, 700);
    up(250, 700);
    down(248, 698);
    down(160, 670);
    up(150, 555);
    down(40, 700); /* A sarokból indított húzás nem old fel. */
    down(40, 560);
    up(40, 560);
    down(360, 657); /* A feloldás csak az alsó sávban indulhat. */
    down(360, 530);
    up(360, 530);
    step(fixture_tap_window + 60);
    quiet();
    assert(fixture_page == -1);
}

static void interruptions(void)
{
    contact(360, 360);
    fixture_online = false;
    step(fixture_tap_window + 60);
    contact(100, 130);
    quiet();
    fixture_online = true;
    fixture_session++;
    contact(360, 360);
    fixture_session++; /* Újracsatlakozás két UI-frissítés között. */
    step(fixture_tap_window + 60);
    quiet();
    contact(360, 360);
    fixture_matched = false;
    step(fixture_tap_window + 60);
    quiet();
    fixture_matched = true;

    unsigned cancels = fixture_touch_cancels;
    contact(360, 360);
    down(360, 360);
    fixture_generation++;
    step(700);
    up(360, 360);
    step(fixture_tap_window + 60);
    quiet();
    assert(fixture_page == 3 && fixture_touch_cancels > cancels);
    idle();

    cancels = fixture_touch_cancels;
    contact(360, 360);
    fixture_doorbell = true;
    step(1000);
    quiet();
    assert(fixture_page == 3 && fixture_touch_cancels > cancels);
    fixture_doorbell = false;
    idle();
}

void test_touch_saver(void)
{
    unsigned page_commands = fixture_calls;
    fixture_touch = fixture_has_touch_section = true;
    fixture_generation++;
    idle();
    snapshot("standby-touch");
    assert_standby("07:13", false);
    fixture_touch_day_ink = "#A6B8C9";
    fixture_generation++;
    step(800);
    assert_standby("07:13", false);
    snapshot("standby-touch-custom");
    fixture_time += 15 * 3600;
    fixture_night_value = true;
    step(800);
    assert_standby("22:13", true);
    snapshot("standby-touch-night");
    fixture_touch_day_ink = "#7CCB9A";
    fixture_generation++;
    step(800);
    assert_standby("22:13", true); /* A nappali árnyalat mentése éjszaka is zárolva és vörösen hagyja a panelt. */
    fixture_time -= 15 * 3600;
    fixture_night_value = false;
    step(800);

    assert_standby("07:13", false);

    zones_and_edges();
    groups(150);
    groups(400);
    groups(1000);
    between_zones();
    canceled_drags();
    interruptions();

    /* A függő gesztust és a már átadott ismétlések küldését is leállítja a feloldás. */
    for (int i = 0; i < 3; i++) contact(360, 360);
    expect(CENTER, 3);
    contact(360, 360);
    unsigned cancels = fixture_touch_cancels;
    unlock();
    assert(fixture_touch_cancels > cancels);
    step(fixture_tap_window + 60);
    quiet();
    assert(fixture_calls == page_commands && fixture_brightness == 80);
    idle();
    fixture_online = false;
    unlock(); /* Miniserver nélkül is működik a feloldás. */
    fixture_online = true;

    fixture_touch = false; /* A mentett, kikapcsolt beállítás hagyományos ébresztést ad. */
    fixture_generation++;
    idle();
    assert_standby("07:13", false);
    fixture_night_value = true;
    step(800);
    assert_standby("07:13", true);
    fixture_night_value = false;
    step(800);
    contact(100, 130);
    step(fixture_tap_window + 60);
    quiet();
    assert(fixture_calls == page_commands && fixture_page == 3);
    printf("Touch: %u zóna-/koppintásparancs; csúsztatás, profilcsere, csengetés és kapcsolatváltás ellenőrizve.\n", consumed);
}
