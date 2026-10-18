// LED decision logic v2 (src/blled/decide.h): every rule in docs/REDESIGN.md "Behaviour
// rules", plus colour -> strip lights -> controller outputs for each strip type and wiring.
#include <unity.h>
#include "Arduino.h"
#include <ArduinoJson.h>
#include "../../src/blled/decide.h"

using namespace v2;

void setUp() {}
void tearDown() {}

static Settings S0() { return defaults(); }
static Snapshot idle() { return Snapshot(); }
static Snapshot printingNow() { Snapshot p; p.gcodeState = "RUNNING"; p.stage = 0; return p; }

static void assertLit(const Decision &d, ColourKind k, Rhythm r, Reason why)
{
    TEST_ASSERT_EQUAL_MESSAGE(SHOW_COLOUR, d.show, "show");
    TEST_ASSERT_EQUAL_MESSAGE(k, d.colour.kind, "kind");
    TEST_ASSERT_EQUAL_MESSAGE(r, d.rhythm, "rhythm");
    TEST_ASSERT_EQUAL_MESSAGE(why, d.reason, "reason");
}
static void assertRgb(const Decision &d, int r, int g, int b)
{
    TEST_ASSERT_EQUAL(CK_CUSTOM, d.colour.kind);
    TEST_ASSERT_EQUAL(r, d.colour.r); TEST_ASSERT_EQUAL(g, d.colour.g); TEST_ASSERT_EQUAL(b, d.colour.b);
}
static void assertOff(const Decision &d, Reason why)
{
    TEST_ASSERT_EQUAL_MESSAGE(SHOW_OFF, d.show, "show");
    TEST_ASSERT_EQUAL_MESSAGE(why, d.reason, "reason");
}

// ---- modes and tools ----
void test_idle_and_printing_show_white() {
    assertLit(decide(S0(), idle()), CK_NEUTRAL, R_STEADY, RS_IDLE);
    assertLit(decide(S0(), printingNow()), CK_NEUTRAL, R_STEADY, RS_PRINTING);
    Snapshot heat; heat.gcodeState = "RUNNING"; heat.stage = 2;        // preheating counts as printing
    assertLit(decide(S0(), heat), CK_NEUTRAL, R_STEADY, RS_PRINTING);
}
void test_mode_off_and_on_override_the_printer() {
    Settings s = S0(); Snapshot err = idle(); err.hms = HMS_FATAL;
    s.mode = MODE_OFF; assertOff(decide(s, err), RS_MODE_OFF);
    s.mode = MODE_ON;  assertLit(decide(s, err), CK_NEUTRAL, R_STEADY, RS_MODE_ON);
}
void test_tools_override_everything_and_are_not_settings() {
    Settings s = S0(); s.mode = MODE_OFF;
    TEST_ASSERT_EQUAL(RS_TOOL_TEST, decide(s, idle(), TOOL_TEST).reason);
    TEST_ASSERT_EQUAL(SHOW_CYCLE, decide(s, idle(), TOOL_CYCLE).show);
    TEST_ASSERT_EQUAL(SHOW_WIFI, decide(s, idle(), TOOL_WIFI).show);
}

// ---- unreachable ----
void test_unreachable_turns_off_after_the_set_time_only() {
    Settings s = S0(); Snapshot p = idle(); p.connected = false;
    p.unreachableMs = 29000; assertLit(decide(s, p), CK_NEUTRAL, R_STEADY, RS_IDLE);
    p.unreachableMs = 30000; assertOff(decide(s, p), RS_UNREACHABLE);
    s.offlineOffSec = 0; p.unreachableMs = 10UL * 3600 * 1000; TEST_ASSERT_NOT_EQUAL(RS_UNREACHABLE, decide(s, p).reason);
}

void test_rejected_access_code_shows_dim_red() {
    Snapshot p = idle(); p.connected = false; p.accessRejected = true;
    Decision d = decide(S0(), p);
    assertLit(d, CK_CUSTOM, R_STEADY, RS_ACCESS_REJECTED); assertRgb(d, 128, 0, 0);
    Settings s = S0(); s.mode = MODE_OFF; TEST_ASSERT_EQUAL(RS_MODE_OFF, decide(s, p).reason);   // Off still wins
}

void test_cyan_while_first_connecting_then_off() {
    Snapshot p = idle(); p.connected = false; p.everConnected = false; p.unreachableMs = 5000;
    Decision d = decide(S0(), p);
    assertLit(d, CK_CUSTOM, R_STEADY, RS_CONNECTING); assertRgb(d, 0, 255, 255);
    p.unreachableMs = 31000;
    TEST_ASSERT_EQUAL(RS_UNREACHABLE, decide(S0(), p).reason);  // not cyan all night with the printer off
    p.unreachableMs = 5000; p.everConnected = true;
    TEST_ASSERT_EQUAL(RS_IDLE, decide(S0(), p).reason);         // a short drop later isn't "connecting"
}
void test_unverified_printer_shows_dim_red_too() {
    Snapshot p = idle(); p.connected = false; p.certRejected = true;
    Decision d = decide(S0(), p);
    assertLit(d, CK_CUSTOM, R_STEADY, RS_CERT_REJECTED); assertRgb(d, 128, 0, 0);
    p.unreachableMs = 600000;                                   // not "unreachable": it answered
    TEST_ASSERT_EQUAL(RS_CERT_REJECTED, decide(S0(), p).reason);
}

// ---- errors ----
void test_errors_blink_red_and_resolve_per_kind() {
    Settings s = S0(); Snapshot p = printingNow();
    p.stage = 6; Decision d = decide(s, p);
    assertLit(d, CK_CUSTOM, R_BLINK, RS_ERROR); assertRgb(d, 255, 0, 0); TEST_ASSERT_EQUAL(EK_RUNOUT, d.detail);
    s.look = LOOK_CUSTOM; s.custom.err[EK_RUNOUT] = rgb(0xc0, 0, 0xff);   // a purple runout
    assertRgb(decide(s, p), 0xc0, 0, 0xff);
    Snapshot cover = printingNow(); cover.overrideStage = 17;               // from an HMS code
    TEST_ASSERT_EQUAL(EK_COVER, decide(s, cover).detail);
    assertRgb(decide(s, cover), 255, 0, 0);                                 // "same as group"
    Snapshot hms = idle(); hms.hms = HMS_SERIOUS;
    TEST_ASSERT_EQUAL(EK_HMS_SERIOUS, decide(s, hms).detail);
}
void test_errors_show_even_when_the_printers_light_is_off() {
    Snapshot p = idle(); p.printerLightOn = false; p.hms = HMS_FATAL;
    TEST_ASSERT_EQUAL(RS_ERROR, decide(S0(), p).reason);
}

// ---- needs attention ----
void test_pause_pulses_blue() {
    Snapshot p = printingNow(); p.gcodeState = "PAUSE"; p.stage = 16;
    Decision d = decide(S0(), p);
    assertLit(d, CK_CUSTOM, R_PULSE, RS_ATTENTION); assertRgb(d, 0, 0, 255);
}
void test_specific_attention_beats_a_plain_pause() {
    Settings s = S0(); s.look = LOOK_CUSTOM; s.custom.attn[AK_FIRSTLAYER] = rgb(255, 0, 255);
    Snapshot p = printingNow(); p.gcodeState = "PAUSE"; p.stage = 34;   // printer also says PAUSE
    Decision d = decide(s, p);
    TEST_ASSERT_EQUAL(AK_FIRSTLAYER, d.detail); assertRgb(d, 255, 0, 255);
}

// ---- finished ----
void test_finished_pulses_green_and_shows_with_the_light_off() {
    Snapshot p = idle(); p.gcodeState = "FINISH"; p.finishShowing = true; p.printerLightOn = false;
    Decision d = decide(S0(), p);
    assertLit(d, CK_CUSTOM, R_SLOW, RS_FINISHED); assertRgb(d, 0, 255, 0);
    p.finishShowing = false;                     // ended (door / timer): light-off rule applies again
    assertOff(decide(S0(), p), RS_LIGHT_OFF);
}

// ---- preparing ----
void test_preparing_steps_resolve_and_default_off() {
    Snapshot p = printingNow();
    p.stage = 14; assertOff(decide(S0(), p), RS_PREPARING);           // off so the lidar isn't disturbed
    p.stage = 1;  Decision d = decide(S0(), p); assertRgb(d, 0, 0, 0x55); TEST_ASSERT_EQUAL(PS_LEVEL, d.detail);
    p.stage = 12; TEST_ASSERT_EQUAL(PS_LIDARCAL, decide(S0(), p).detail);
    Snapshot ov = printingNow(); ov.overrideStage = 10; TEST_ASSERT_EQUAL(PS_FIRSTLAYER, decide(S0(), ov).detail);
}
void test_preparing_without_lidar_uses_the_printing_colour() {
    Snapshot p = printingNow(); p.stage = 14; p.hasLidar = false;
    assertLit(decide(S0(), p), CK_NEUTRAL, R_STEADY, RS_PREPARING_NO_LIDAR);
}

// ---- following the printer's light, idle-off ----
void test_follow_light_turns_off_idle_and_printing() {
    Snapshot p = idle(); p.printerLightOn = false;
    assertOff(decide(S0(), p), RS_LIGHT_OFF);
    Snapshot q = printingNow(); q.printerLightOn = false;
    assertOff(decide(S0(), q), RS_LIGHT_OFF);
    Settings s = S0(); s.followLight = false;
    assertLit(decide(s, p), CK_NEUTRAL, R_STEADY, RS_IDLE);
}
void test_idle_off_after_the_set_time_never_during_a_print() {
    Settings s = S0(); Snapshot p = idle();
    p.idleMs = 9UL * 60000; TEST_ASSERT_EQUAL(RS_IDLE, decide(s, p).reason);
    p.idleMs = 10UL * 60000; assertOff(decide(s, p), RS_IDLE_OFF);
    Snapshot q = printingNow(); q.idleMs = 5UL * 3600000; TEST_ASSERT_EQUAL(RS_PRINTING, decide(s, q).reason);
    Snapshot paused = printingNow(); paused.gcodeState = "PAUSE"; paused.stage = 0; paused.idleMs = 5UL * 3600000;
    TEST_ASSERT_EQUAL(RS_ATTENTION, decide(s, paused).reason);
    s.idleOffMin = 0; p.idleMs = 48UL * 3600000; TEST_ASSERT_EQUAL(RS_IDLE, decide(s, p).reason);
}

// ---- presets ----
void test_white_preset_tells_states_apart_by_rhythm() {
    Settings s = S0(); s.look = LOOK_WHITE;
    Snapshot e = idle(); e.hms = HMS_SERIOUS;
    assertLit(decide(s, e), CK_NEUTRAL, R_BLINK, RS_ERROR);
    Snapshot f = idle(); f.finishShowing = true;
    assertLit(decide(s, f), CK_NEUTRAL, R_SLOW, RS_FINISHED);
}

// ---- colour -> strip lights ----
void test_whites_on_each_strip_type() {
    LookColour warm = colour(CK_WARM), neutral = colour(CK_NEUTRAL), cool = colour(CK_COOL);
    Levels a = toLights(warm, STRIP_RGBWW); TEST_ASSERT_EQUAL(255, a.v[L_WW]); TEST_ASSERT_EQUAL(0, a.v[L_CW]);
    Levels b = toLights(neutral, STRIP_RGBWW); TEST_ASSERT_EQUAL(255, b.v[L_WW]); TEST_ASSERT_EQUAL(255, b.v[L_CW]);
    Levels c = toLights(cool, STRIP_WWCW); TEST_ASSERT_EQUAL(0, c.v[L_WW]); TEST_ASSERT_EQUAL(255, c.v[L_CW]);
    Levels d = toLights(neutral, STRIP_RGBW); TEST_ASSERT_EQUAL(255, d.v[L_W]); TEST_ASSERT_EQUAL(0, d.v[L_R]);
    Levels e = toLights(warm, STRIP_RGBW); TEST_ASSERT_EQUAL(255, e.v[L_W]); TEST_ASSERT_GREATER_THAN(0, e.v[L_R]);
    Levels f = toLights(neutral, STRIP_RGB); TEST_ASSERT_EQUAL(255, f.v[L_R]); TEST_ASSERT_GREATER_THAN(200, f.v[L_B]);
    Levels g = toLights(cool, STRIP_W); TEST_ASSERT_EQUAL(255, g.v[L_W]);
}
void test_colours_on_strips_without_rgb_become_white() {
    LookColour red = rgb(255, 0, 0);
    Levels a = toLights(red, STRIP_W); TEST_ASSERT_EQUAL(255, a.v[L_W]); TEST_ASSERT_EQUAL(0, a.v[L_R]);
    Levels b = toLights(red, STRIP_WWCW); TEST_ASSERT_EQUAL(255, b.v[L_WW]); TEST_ASSERT_EQUAL(255, b.v[L_CW]);
}
void test_colour_mixed_with_white_is_kept() {
    LookColour greenWhite = rgb(0, 255, 0, 255, 255);   // a v1 finish colour
    Levels a = toLights(greenWhite, STRIP_RGBWW);
    TEST_ASSERT_EQUAL(255, a.v[L_G]); TEST_ASSERT_EQUAL(255, a.v[L_WW]); TEST_ASSERT_EQUAL(255, a.v[L_CW]);
    Levels b = toLights(greenWhite, STRIP_RGBW);
    TEST_ASSERT_EQUAL(255, b.v[L_G]); TEST_ASSERT_EQUAL(255, b.v[L_W]);
    TEST_ASSERT_EQUAL(0, toLights(colour(CK_OFF), STRIP_RGBWW).v[L_WW]);
}

// ---- strip lights -> controller outputs ----
void test_board_labels_and_single_white_on_both_outputs() {
    Settings s = S0(); s.strip = STRIP_RGBW;
    OutputLevels o = toOutputs(s, toLights(colour(CK_NEUTRAL), STRIP_RGBW));
    TEST_ASSERT_EQUAL(255, o.v[OUT_WW]); TEST_ASSERT_EQUAL(255, o.v[OUT_CW]);   // works on either output
    OutputLevels r = toOutputs(s, toLights(rgb(255, 0, 0), STRIP_RGBW));
    TEST_ASSERT_EQUAL(255, r.v[OUT_R]); TEST_ASSERT_EQUAL(0, r.v[OUT_G]);
}
void test_wiring_check_result_moves_lights_to_other_outputs() {
    Settings s = S0(); s.strip = STRIP_RGBW;
    s.wiring[L_R] = OUT_G; s.wiring[L_G] = OUT_R; s.wiring[L_W] = OUT_CW;   // red/green swapped, white on CW
    OutputLevels r = toOutputs(s, toLights(rgb(255, 0, 0), STRIP_RGBW));
    TEST_ASSERT_EQUAL(0, r.v[OUT_R]); TEST_ASSERT_EQUAL(255, r.v[OUT_G]);
    OutputLevels w = toOutputs(s, toLights(colour(CK_NEUTRAL), STRIP_RGBW));
    TEST_ASSERT_EQUAL(0, w.v[OUT_WW]); TEST_ASSERT_EQUAL(255, w.v[OUT_CW]);  // pinned now
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_idle_and_printing_show_white);
    RUN_TEST(test_mode_off_and_on_override_the_printer);
    RUN_TEST(test_tools_override_everything_and_are_not_settings);
    RUN_TEST(test_unreachable_turns_off_after_the_set_time_only);
    RUN_TEST(test_rejected_access_code_shows_dim_red);
    RUN_TEST(test_cyan_while_first_connecting_then_off);
    RUN_TEST(test_unverified_printer_shows_dim_red_too);
    RUN_TEST(test_errors_blink_red_and_resolve_per_kind);
    RUN_TEST(test_errors_show_even_when_the_printers_light_is_off);
    RUN_TEST(test_pause_pulses_blue);
    RUN_TEST(test_specific_attention_beats_a_plain_pause);
    RUN_TEST(test_finished_pulses_green_and_shows_with_the_light_off);
    RUN_TEST(test_preparing_steps_resolve_and_default_off);
    RUN_TEST(test_preparing_without_lidar_uses_the_printing_colour);
    RUN_TEST(test_follow_light_turns_off_idle_and_printing);
    RUN_TEST(test_idle_off_after_the_set_time_never_during_a_print);
    RUN_TEST(test_white_preset_tells_states_apart_by_rhythm);
    RUN_TEST(test_whites_on_each_strip_type);
    RUN_TEST(test_colours_on_strips_without_rgb_become_white);
    RUN_TEST(test_colour_mixed_with_white_is_kept);
    RUN_TEST(test_board_labels_and_single_white_on_both_outputs);
    RUN_TEST(test_wiring_check_result_moves_lights_to_other_outputs);
    return UNITY_END();
}
