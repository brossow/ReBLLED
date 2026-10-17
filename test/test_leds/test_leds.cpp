// Pins down how updateleds() maps printer state to LED colour. These describe the
// behaviour as of 2.3.0-br.1; a change that breaks one should be deliberate.
#include <unity.h>
#include "../harness.h"

void setUp() { freshDevice(); }
void tearDown() {}

void test_boot_call_is_a_no_op() {
  printerVariables = PrinterVariables{};
  fake::writes = 0;
  updateleds();
  TEST_ASSERT_TRUE(printerVariables.initalisedLEDs);
  TEST_ASSERT_EQUAL(0, fake::writes);
}

void test_idle_with_chamber_light_on_shows_running_white() {
  mqttChange();
  TEST_ASSERT_TRUE(ledsAre(0, 0, 0, 255, 255));
}

void test_replicate_turns_leds_off_with_chamber_light() {
  mqttChange();
  printerVariables.printerledstate = false;
  LogSerial.out.clear();
  mqttChange();
  TEST_ASSERT_TRUE_MESSAGE(ledsOff(), LogSerial.out.c_str());
}

void test_replicate_off_ignored_for_1s_after_a_door_event() {
  mqttChange();
  printerVariables.lastdoorClosems = millis() - 500; // door just closed
  printerVariables.printerledstate = false;
  mqttChange();
  TEST_ASSERT_TRUE(ledsAre(0, 0, 0, 255, 255)); // stays on: idle branch, not replicate-off
}

void test_printing_shows_running_colour() {
  printerVariables.gcodeState = "RUNNING";
  printerVariables.stage = 0;
  mqttChange();
  TEST_ASSERT_TRUE(ledsAre(0, 0, 0, 255, 255));
}

void test_brightness_scales_output() {
  printerConfig.brightness = 20;
  mqttChange();
  TEST_ASSERT_TRUE(ledsAre(0, 0, 0, 51, 51));
}

void test_finish_shows_finish_colour_until_door() {
  printerVariables.gcodeState = "FINISH";
  printerVariables.finished = true;
  printerVariables.waitingForDoor = true;
  printerConfig.finish_check = true;
  printerConfig.finishStartms = millis();
  mqttChange();
  TEST_ASSERT_TRUE(ledsAre(0, 255, 0, 0, 0));
}

void test_pause_is_blue() {
  printerVariables.gcodeState = "PAUSE";
  printerVariables.stage = 16;
  mqttChange();
  TEST_ASSERT_TRUE(ledsAre(0, 0, 255, 0, 0));
}

void test_filament_runout_is_red() {
  printerVariables.gcodeState = "PAUSE";
  printerVariables.stage = 6;
  mqttChange();
  TEST_ASSERT_TRUE(ledsAre(255, 0, 0, 0, 0));
}

void test_serious_hms_is_red() {
  printerVariables.parsedHMSlevel = "Serious";
  mqttChange();
  TEST_ASSERT_TRUE(ledsAre(255, 0, 0, 0, 0));
}

void test_hms_override_stage_shows_its_colour() {
  printerVariables.overridestage = 17; // front cover removed
  mqttChange();
  TEST_ASSERT_TRUE(ledsAre(255, 0, 0, 0, 0));
}

void test_error_detection_off_ignores_errors() {
  printerConfig.errordetection = false;
  printerVariables.parsedHMSlevel = "Serious";
  mqttChange();
  TEST_ASSERT_TRUE(ledsAre(0, 0, 0, 255, 255));
}

void test_printer_offline_30s_turns_leds_off() {
  mqttChange();
  printerVariables.online = false;
  printerVariables.disconnectMQTTms = millis() - 31000;
  updateleds();
  TEST_ASSERT_TRUE(ledsOff());
}

void test_printer_offline_under_30s_keeps_leds() {
  mqttChange();
  printerVariables.online = false;
  printerVariables.disconnectMQTTms = millis() - 5000;
  updateleds();
  TEST_ASSERT_TRUE(ledsAre(0, 0, 0, 255, 255));
}

void test_idle_timeout_turns_off_leds_and_chamber_light() {
  mqttChange();
  fake::now_ms += printerConfig.inactivityTimeOut + 1000;
  ledsloop();
  TEST_ASSERT_TRUE(ledsOff());
  TEST_ASSERT_TRUE(printerConfig.isIdleOFFActive);
  TEST_ASSERT_EQUAL(0, fake::chamberLightLast);
}

void test_idle_timeout_disabled_keeps_leds_on() {
  printerConfig.inactivityEnabled = false;
  mqttChange();
  fake::now_ms += printerConfig.inactivityTimeOut + 1000;
  ledsloop();
  TEST_ASSERT_TRUE(ledsAre(0, 0, 0, 255, 255));
}

void test_door_toggle_flips_the_state_from_before_the_door_opened() {
  mqttChange();
  TEST_ASSERT_TRUE(ledsAre(0, 0, 0, 255, 255));
  printerVariables.ledsOnBeforeDoor = true;   // recorded when the door opened
  printerVariables.doorSwitchTriggered = true;
  updateleds();
  TEST_ASSERT_TRUE(ledsOff());
  TEST_ASSERT_FALSE(printerVariables.doorSwitchTriggered);
  printerVariables.ledsOnBeforeDoor = false;
  printerVariables.doorSwitchTriggered = true;
  updateleds();
  TEST_ASSERT_TRUE(ledsAre(0, 0, 0, 255, 255));
}

void test_test_colour_overrides_printer_state() {
  printerConfig.testcolorEnabled = true;
  printerConfig.testColor = hex2rgb("#FF0000");
  printerVariables.gcodeState = "RUNNING"; printerVariables.stage = 0;
  mqttChange();
  TEST_ASSERT_TRUE(ledsAre(255, 0, 0, 0, 0));
}

void test_maintenance_mode_is_full_white() {
  printerConfig.maintMode = true;
  printerConfig.brightness = 50;
  mqttChange();
  TEST_ASSERT_TRUE(ledsAre(0, 0, 0, 127, 127));
}

void test_fade_steps_through_intermediate_values_and_ends_on_target() {
  unsigned long t0 = millis();
  tweenToColor(255, 0, 0, 0, 0);
  TEST_ASSERT_TRUE(ledsAre(255, 0, 0, 0, 0));
  TEST_ASSERT_GREATER_THAN(5 * 10, fake::writes);           // 50 steps x 5 channels
  TEST_ASSERT_UINT32_WITHIN(20, 500, millis() - t0);         // ~0.5 s
}

void test_hex2rgb_parses_and_tolerates_bad_input() {
  COLOR c = hex2rgb("#3F3CFB");
  TEST_ASSERT_EQUAL(0x3F, c.r); TEST_ASSERT_EQUAL(0x3C, c.g); TEST_ASSERT_EQUAL(0xFB, c.b);
  COLOR shortc = hex2rgb("#FF");
  TEST_ASSERT_EQUAL(0xFF, shortc.r); TEST_ASSERT_EQUAL(0, shortc.g);
  COLOR longc = hex2rgb("#112233445566778899"); // used to loop forever
  TEST_ASSERT_EQUAL(0x11, longc.r); TEST_ASSERT_EQUAL(0x33, longc.b);
  TEST_ASSERT_TRUE(strlen(longc.RGBhex) < sizeof(longc.RGBhex));
}

void test_rgb_cycle_is_saturated_and_respects_brightness() {
  printerConfig.discoMode = true;
  hue = 0.0;
  RGBCycle(); // hue 0.1: red near full, others clipped to 0
  TEST_ASSERT_GREATER_THAN(250, fake::duty[redChannel]);
  TEST_ASSERT_EQUAL(0, fake::duty[greenChannel]);
  TEST_ASSERT_EQUAL(0, fake::duty[blueChannel]);
  printerConfig.brightness = 20;
  hue = 0.0;
  RGBCycle();
  TEST_ASSERT_UINT32_WITHIN(1, 51, fake::duty[redChannel]);
}

void test_live_brightness_reapplies_current_colour_instantly() {
  mqttChange();
  TEST_ASSERT_TRUE(ledsAre(0, 0, 0, 255, 255));
  unsigned long t0 = millis();
  setBrightness(20);
  TEST_ASSERT_TRUE(ledsAre(0, 0, 0, 51, 51));
  TEST_ASSERT_EQUAL(20, printerConfig.brightness);
  TEST_ASSERT_UINT32_WITHIN(5, 0, millis() - t0);   // no 0.5 s fade while dragging
  setBrightness(150);                                 // clamped
  TEST_ASSERT_EQUAL(100, printerConfig.brightness);
  TEST_ASSERT_TRUE(ledsAre(0, 0, 0, 255, 255));
}

void test_live_brightness_keeps_leds_off_when_off() {
  printerVariables.printerledstate = false;
  mqttChange();
  TEST_ASSERT_TRUE(ledsOff());
  setBrightness(60);
  TEST_ASSERT_TRUE(ledsOff());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_boot_call_is_a_no_op);
  RUN_TEST(test_idle_with_chamber_light_on_shows_running_white);
  RUN_TEST(test_replicate_turns_leds_off_with_chamber_light);
  RUN_TEST(test_replicate_off_ignored_for_1s_after_a_door_event);
  RUN_TEST(test_printing_shows_running_colour);
  RUN_TEST(test_brightness_scales_output);
  RUN_TEST(test_finish_shows_finish_colour_until_door);
  RUN_TEST(test_pause_is_blue);
  RUN_TEST(test_filament_runout_is_red);
  RUN_TEST(test_serious_hms_is_red);
  RUN_TEST(test_hms_override_stage_shows_its_colour);
  RUN_TEST(test_error_detection_off_ignores_errors);
  RUN_TEST(test_printer_offline_30s_turns_leds_off);
  RUN_TEST(test_printer_offline_under_30s_keeps_leds);
  RUN_TEST(test_idle_timeout_turns_off_leds_and_chamber_light);
  RUN_TEST(test_idle_timeout_disabled_keeps_leds_on);
  RUN_TEST(test_door_toggle_flips_the_state_from_before_the_door_opened);
  RUN_TEST(test_test_colour_overrides_printer_state);
  RUN_TEST(test_maintenance_mode_is_full_white);
  RUN_TEST(test_fade_steps_through_intermediate_values_and_ends_on_target);
  RUN_TEST(test_hex2rgb_parses_and_tolerates_bad_input);
  RUN_TEST(test_rgb_cycle_is_saturated_and_respects_brightness);
  RUN_TEST(test_live_brightness_reapplies_current_colour_instantly);
  RUN_TEST(test_live_brightness_keeps_leds_off_when_off);
  return UNITY_END();
}
