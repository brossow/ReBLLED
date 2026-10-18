// The v2 LED runtime end to end: printer reports in through the real parser, PWM duties out.
// decide()'s rules are covered rule by rule in test_decide; these check the wiring between
// events, timers, settings and the hardware layer.
#include <unity.h>
#include "../harness.h"

// Levels being shown (after brightness, before the rhythm scales them).
static bool showing(int r, int g, int b, int ww, int cw) {
  return currentRed == r && currentGreen == g && currentBlue == b && currentWarm == ww && currentCold == cw;
}
static bool showingWhite() { return showing(0, 0, 0, 255, 255); }
static bool showingOff() { return showing(0, 0, 0, 0, 0); }

void setUp() { freshDevice(); }
void tearDown() {}

// ---- basics ----
void test_idle_shows_neutral_white() {
  TEST_ASSERT_TRUE(ledsWhite());
  TEST_ASSERT_EQUAL(v2::RS_IDLE, lastDecision.reason);
}
void test_nothing_is_rewritten_when_nothing_changed() {
  loopFor(5000);
  TEST_ASSERT_EQUAL(0, fake::writes);
}
void test_printing_and_preparing_colours() {
  gcode("RUNNING", 0);
  TEST_ASSERT_TRUE(showingWhite());
  TEST_ASSERT_EQUAL(v2::RS_PRINTING, lastDecision.reason);
  gcode("RUNNING", 1);                         // auto bed levelling: dim blue by default
  TEST_ASSERT_TRUE(showing(0, 0, 0x55, 0, 0));
  TEST_ASSERT_EQUAL(v2::RS_PREPARING, lastDecision.reason);
}
void test_preparing_without_lidar_keeps_the_printing_colour() {
  v2::Settings s = v2::defaults(); strcpy(s.model, "P1S");
  freshDevice(s);
  gcode("RUNNING", 1);
  TEST_ASSERT_TRUE(showingWhite());
}
void test_preparing_on_an_unknown_model_assumes_lidar() {
  strcpy(printerVariables.detectedModel, "Z9");               // a code nobody has mapped yet
  gcode("RUNNING", 1);
  TEST_ASSERT_TRUE(showing(0, 0, 0x55, 0, 0));                // the levelling colour, not printing
  strcpy(printerVariables.detectedModel, "A1M");
  gcode("RUNNING", 8); gcode("RUNNING", 1);
  TEST_ASSERT_TRUE(showingWhite());                           // known: no lidar
}
void test_brightness_scales_and_applies_live() {
  settings.brightness = 50; lightsSettingsChanged(); lightsLoop();
  TEST_ASSERT_TRUE(ledsAre(0, 0, 0, 127, 127));
  settings.brightness = 100; setBrightness(100);
  TEST_ASSERT_TRUE(ledsWhite());
}

// ---- strip types and the wiring check ----
void test_rgbw_single_white_drives_both_white_outputs_until_pinned() {
  settings.strip = v2::STRIP_RGBW; lightsSettingsChanged(); lightsLoop();
  TEST_ASSERT_TRUE(ledsAre(0, 0, 0, 255, 255));
  settings.wiring[v2::L_W] = v2::OUT_WW; lightsSettingsChanged(); lightsLoop();
  TEST_ASSERT_TRUE(ledsAre(0, 0, 0, 255, 0));
}
void test_wiring_lets_a_swapped_red_and_green_show_true_colours() {
  settings.wiring[v2::L_R] = v2::OUT_G; settings.wiring[v2::L_G] = v2::OUT_R;
  lightsSettingsChanged(); lightsLoop();
  v2::LookColour red = v2::rgb(255, 0, 0);
  lightsSetTool(v2::TOOL_TEST, &red); lightsLoop();
  TEST_ASSERT_TRUE(ledsAre(0, 255, 0, 0, 0)); // red light is on the G output
}
void test_wiring_check_lights_one_output_then_restores() {
  lightsCheckOutput(v2::OUT_B); lightsLoop();
  TEST_ASSERT_TRUE(ledsAre(0, 0, 255, 0, 0));
  lightsCheckOutput(v2::OUT_CW); lightsLoop();
  TEST_ASSERT_TRUE(ledsAre(0, 0, 0, 0, 255));
  lightsCheckOutput(-1); lightsLoop();
  TEST_ASSERT_TRUE(ledsWhite());
}

// ---- rhythms ----
void test_error_blinks_on_and_off() {
  // front cover removed: HMS_0300_1200_0002_0001
  msg("{\"print\":{\"hms\":[{\"attr\":50336256,\"code\":131073}]}}");
  TEST_ASSERT_TRUE(showing(255, 0, 0, 0, 0));
  uint32_t lo = 255, hi = 0;
  for (int i = 0; i < 200; i++) { lightsLoop(); lo = std::min(lo, leds().r); hi = std::max(hi, leds().r); }
  TEST_ASSERT_EQUAL(0, lo);
  TEST_ASSERT_EQUAL(255, hi);
}
void test_a_new_rhythm_starts_from_its_beginning() {
  effectStartms = millis() - 300;              // a stale start that would land in the dark half
  msg("{\"print\":{\"hms\":[{\"attr\":50336256,\"code\":131073}]}}");
  lightsLoop();
  TEST_ASSERT_EQUAL(255, leds().r);            // an error is first seen lit, not dark
}
void test_pause_pulses_but_never_goes_dark() {
  gcode("RUNNING", 0);
  gcode("PAUSE", 0);
  TEST_ASSERT_TRUE(showing(0, 0, 255, 0, 0));
  uint32_t lo = 255, hi = 0;
  for (int i = 0; i < 300; i++) { lightsLoop(); lo = std::min(lo, leds().b); hi = std::max(hi, leds().b); }
  TEST_ASSERT_TRUE(lo > 50);
  TEST_ASSERT_TRUE(hi > 240);
}
void test_brightness_change_keeps_the_rhythm() {
  gcode("RUNNING", 0);
  gcode("PAUSE", 0);
  setBrightness(50);
  TEST_ASSERT_EQUAL(EFFECT_PULSE, currentEffect);
  TEST_ASSERT_TRUE(showing(0, 0, 127, 0, 0));
}
void test_error_clearing_returns_to_steady_full_white() {
  msg("{\"print\":{\"hms\":[{\"attr\":50336256,\"code\":131073}]}}");
  loopFor(700);                                // somewhere in the blink
  msg("{\"print\":{\"hms\":[]}}");
  TEST_ASSERT_EQUAL(EFFECT_SOLID, currentEffect);
  loopFor(100);
  TEST_ASSERT_TRUE(ledsWhite());
}

// ---- finishing ----
static void finishPrint() { gcode("RUNNING", 0); gcode("FINISH", -1); }

void test_finish_shows_until_the_door_by_default() {
  finishPrint();
  TEST_ASSERT_TRUE(showing(0, 255, 0, 0, 0));
  TEST_ASSERT_EQUAL(EFFECT_PULSE_SLOW, currentEffect);
  loopFor(15 * 60000);                          // past the 10 min timer, which door mode doesn't use
  TEST_ASSERT_TRUE(showing(0, 255, 0, 0, 0));
  doorOpen();
  TEST_ASSERT_TRUE(showingWhite());
}
void test_finish_timer_mode_ignores_the_door_and_can_turn_the_printer_light_off() {
  settings.finishEnds = v2::FIN_TIMER; settings.finishMin = 5; settings.lightOffFinishTimer = true;
  finishPrint();
  doorOpen(); advance(2000); doorClose();
  TEST_ASSERT_TRUE(showing(0, 255, 0, 0, 0));
  loopFor(5 * 60000);
  TEST_ASSERT_EQUAL(0, pendingChamberLight);   // printer's light off queued
  TEST_ASSERT_TRUE(showingOff());              // and the strip follows it
}
void test_finish_either_door_first_means_no_later_light_off() {
  settings.finishEnds = v2::FIN_EITHER; settings.lightOffFinishTimer = true;
  finishPrint();
  doorOpen();
  TEST_ASSERT_TRUE(showingWhite());
  loopFor(9 * 60000);
  TEST_ASSERT_EQUAL(-1, pendingChamberLight);
}
void test_finish_uses_the_timer_on_a_printer_without_a_door() {
  v2::Settings st = v2::defaults(); strcpy(st.model, "P1S"); st.finishMin = 5;   // finishEnds stays "door"
  freshDevice(st);
  finishPrint();
  loopFor(4 * 60000);
  TEST_ASSERT_TRUE(showing(0, 255, 0, 0, 0));
  loopFor(61000);
  TEST_ASSERT_TRUE(showingWhite());
}
void test_a_new_print_clears_the_finish() {
  finishPrint();
  gcode("PREPARE", 0);
  gcode("RUNNING", 0);
  TEST_ASSERT_TRUE(showingWhite());
  TEST_ASSERT_FALSE(finishShowing);
}
void test_no_finish_colour_after_boot_without_a_print() {
  printerVariables.gcodeState = "FINISH"; lightsUpdate();
  TEST_ASSERT_TRUE(showingWhite());
}

// ---- idle and the door ----
void test_idle_off_after_the_timeout_and_the_door_wakes_it() {
  loopFor(9 * 60000);
  TEST_ASSERT_TRUE(showingWhite());
  loopFor(61000);
  TEST_ASSERT_TRUE(showingOff());
  TEST_ASSERT_EQUAL(-1, pendingChamberLight);  // the printer's light is left alone by default
  doorOpen();
  TEST_ASSERT_TRUE(showingWhite());
}
void test_never_idle_off_during_a_print() {
  gcode("RUNNING", 0);
  loopFor(30 * 60000);
  TEST_ASSERT_TRUE(showingWhite());
}
void test_idle_off_can_turn_the_printer_light_off() {
  settings.lightOffIdle = true;
  loopFor(10 * 60000 + 1000);
  TEST_ASSERT_EQUAL(0, pendingChamberLight);
}
void test_door_never_changes_the_mode() {
  // v1's quick open/close gesture toggled the strip off; v2 has no hidden gestures.
  for (int i = 0; i < 3; i++) { doorOpen(); advance(800); doorClose(); advance(800); }
  loopFor(2000);
  TEST_ASSERT_TRUE(showingWhite());
  TEST_ASSERT_EQUAL(v2::MODE_FOLLOW, settings.mode);
}
void test_door_can_turn_the_printer_light_on_and_idle_turns_it_back_off() {
  settings.lightOnDoor = true;
  printerLight(false);
  TEST_ASSERT_TRUE(showingOff());              // following the printer's light
  doorOpen();
  TEST_ASSERT_EQUAL(1, pendingChamberLight);
  TEST_ASSERT_TRUE(showingWhite());            // no flicker while the report catches up
  pendingChamberLight = -1;
  loopFor(10 * 60000 + 1000);
  TEST_ASSERT_EQUAL(0, pendingChamberLight);   // we turned it on, so we turn it off
}
void test_printer_light_on_at_print_start() {
  settings.lightOnPrintStart = true;
  printerLight(false);
  gcode("RUNNING", 0);
  TEST_ASSERT_EQUAL(1, pendingChamberLight);
}

// ---- following the printer's light, modes ----
void test_follows_the_printers_light() {
  printerLight(false);
  TEST_ASSERT_TRUE(showingOff());
  printerLight(true);
  TEST_ASSERT_TRUE(showingWhite());
}
void test_not_following_keeps_the_strip_on() {
  settings.followLight = false;
  printerLight(false);
  TEST_ASSERT_TRUE(showingWhite());
}
void test_modes_on_and_off_override_the_printer() {
  settings.mode = v2::MODE_OFF; lightsSettingsChanged(); lightsLoop();
  gcode("RUNNING", 0);
  TEST_ASSERT_TRUE(showingOff());
  settings.mode = v2::MODE_ON; lightsSettingsChanged(); lightsLoop();
  printerLight(false);
  TEST_ASSERT_TRUE(showingWhite());
}

// ---- connection ----
void test_unreachable_printer_turns_off_after_the_delay() {
  printerVariables.online = false; printerVariables.disconnectMQTTms = millis();
  loopFor(29000);
  TEST_ASSERT_TRUE(showingWhite());
  loopFor(2000);
  TEST_ASSERT_TRUE(showingOff());
  TEST_ASSERT_EQUAL(v2::RS_UNREACHABLE, lastDecision.reason);
}
void test_rejected_access_code_shows_dim_red() {
  printerVariables.online = false; printerVariables.accessRejected = true;
  lightsUpdate();
  TEST_ASSERT_TRUE(showing(128, 0, 0, 0, 0));
}

// ---- tools ----
void test_test_colour_overrides_the_printer_and_stops() {
  v2::LookColour c = v2::rgb(0x3f, 0x3c, 0xfb);
  lightsSetTool(v2::TOOL_TEST, &c); lightsLoop();
  gcode("RUNNING", 0);
  TEST_ASSERT_TRUE(showing(0x3f, 0x3c, 0xfb, 0, 0));
  lightsSetTool(v2::TOOL_NONE); lightsLoop();
  TEST_ASSERT_TRUE(showingWhite());
}
void test_colour_cycle_uses_the_rgb_outputs_and_respects_brightness() {
  settings.brightness = 50; lightsSettingsChanged(); lightsLoop();
  lightsSetTool(v2::TOOL_CYCLE); lightsLoop();
  for (int i = 0; i < 500; i++) {
    lightsLoop();
    Duty d = leds();
    TEST_ASSERT_TRUE(d.r <= 127 && d.g <= 127 && d.b <= 127);
    TEST_ASSERT_EQUAL(0, d.ww); TEST_ASSERT_EQUAL(0, d.cw);
  }
  lightsSetTool(v2::TOOL_NONE); lightsLoop();
  TEST_ASSERT_TRUE(ledsAre(0, 0, 0, 127, 127));
}
void test_colour_cycle_follows_the_wiring() {
  settings.wiring[v2::L_R] = v2::OUT_G; settings.wiring[v2::L_G] = v2::OUT_R;
  lightsSettingsChanged(); lightsLoop();
  hue = 0; lightsSetTool(v2::TOOL_CYCLE); lightsLoop();   // hue ~0: red
  TEST_ASSERT_TRUE(leds().g > 250);
  TEST_ASSERT_EQUAL(0, leds().r);
}
void test_steady_after_a_rhythm_in_the_same_colour_is_not_left_dark() {
  msg("{\"print\":{\"hms\":[{\"attr\":50336256,\"code\":131073}]}}"); // red blink
  for (int i = 0; i < 200 && leds().r != 0; i++) lightsLoop();
  TEST_ASSERT_EQUAL(0, leds().r);              // caught in the dark half
  v2::LookColour red = v2::rgb(255, 0, 0);
  lightsSetTool(v2::TOOL_TEST, &red); lightsLoop();          // same colour, steady: the fade has nothing to do
  TEST_ASSERT_TRUE(ledsAre(255, 0, 0, 0, 0));
}
void test_wifi_tool_shows_signal_strength() {
  WiFi.rssi = -75;
  lightsSetTool(v2::TOOL_WIFI); lightsLoop();
  TEST_ASSERT_TRUE(showing(255, 128, 0, 0, 0));
  WiFi.rssi = -45; lightsUpdate();
  TEST_ASSERT_TRUE(showing(0, 255, 0, 0, 0));
  WiFi.rssi = -50;
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_idle_shows_neutral_white);
  RUN_TEST(test_nothing_is_rewritten_when_nothing_changed);
  RUN_TEST(test_printing_and_preparing_colours);
  RUN_TEST(test_preparing_without_lidar_keeps_the_printing_colour);
  RUN_TEST(test_preparing_on_an_unknown_model_assumes_lidar);
  RUN_TEST(test_brightness_scales_and_applies_live);
  RUN_TEST(test_rgbw_single_white_drives_both_white_outputs_until_pinned);
  RUN_TEST(test_wiring_lets_a_swapped_red_and_green_show_true_colours);
  RUN_TEST(test_wiring_check_lights_one_output_then_restores);
  RUN_TEST(test_error_blinks_on_and_off);
  RUN_TEST(test_a_new_rhythm_starts_from_its_beginning);
  RUN_TEST(test_pause_pulses_but_never_goes_dark);
  RUN_TEST(test_brightness_change_keeps_the_rhythm);
  RUN_TEST(test_error_clearing_returns_to_steady_full_white);
  RUN_TEST(test_finish_shows_until_the_door_by_default);
  RUN_TEST(test_finish_timer_mode_ignores_the_door_and_can_turn_the_printer_light_off);
  RUN_TEST(test_finish_either_door_first_means_no_later_light_off);
  RUN_TEST(test_finish_uses_the_timer_on_a_printer_without_a_door);
  RUN_TEST(test_a_new_print_clears_the_finish);
  RUN_TEST(test_no_finish_colour_after_boot_without_a_print);
  RUN_TEST(test_idle_off_after_the_timeout_and_the_door_wakes_it);
  RUN_TEST(test_never_idle_off_during_a_print);
  RUN_TEST(test_idle_off_can_turn_the_printer_light_off);
  RUN_TEST(test_door_never_changes_the_mode);
  RUN_TEST(test_door_can_turn_the_printer_light_on_and_idle_turns_it_back_off);
  RUN_TEST(test_printer_light_on_at_print_start);
  RUN_TEST(test_follows_the_printers_light);
  RUN_TEST(test_not_following_keeps_the_strip_on);
  RUN_TEST(test_modes_on_and_off_override_the_printer);
  RUN_TEST(test_unreachable_printer_turns_off_after_the_delay);
  RUN_TEST(test_rejected_access_code_shows_dim_red);
  RUN_TEST(test_test_colour_overrides_the_printer_and_stops);
  RUN_TEST(test_colour_cycle_uses_the_rgb_outputs_and_respects_brightness);
  RUN_TEST(test_colour_cycle_follows_the_wiring);
  RUN_TEST(test_steady_after_a_rhythm_in_the_same_colour_is_not_left_dark);
  RUN_TEST(test_wifi_tool_shows_signal_strength);
  return UNITY_END();
}
