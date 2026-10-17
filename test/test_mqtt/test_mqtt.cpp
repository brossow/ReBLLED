// Feeds real-shaped printer MQTT messages through ParseCallback() and checks the state it
// derives and what the LEDs end up doing.
#include <unity.h>
#define HARNESS_REAL_CHAMBER_LIGHT
#include "../harness.h"
#include "../../src/blled/mqttmanager.h"

static void msg(const char *json) {
  ParseCallback((char *)"device/TEST/report", (byte *)json, strlen(json));
}
static void advance(unsigned long ms) { fake::now_ms += ms; }

void setUp() {
  freshDevice();
  fake::published.clear();
  fake::mqttConnected = true;
  lastMQTTupdate = 0;
  pendingChamberLight = -1;
}
void tearDown() {}

void test_door_bit_23_opens_and_closes() {
  msg("{\"print\":{\"home_flag\":8388608}}"); // bit 23
  TEST_ASSERT_TRUE(printerVariables.doorOpen);
  advance(5000);
  msg("{\"print\":{\"home_flag\":0}}");
  TEST_ASSERT_FALSE(printerVariables.doorOpen);
  TEST_ASSERT_FALSE(printerVariables.doorSwitchTriggered); // 5 s open: not a quick toggle
}

static void doorOpen() { msg("{\"print\":{\"home_flag\":8388608}}"); }
static void doorClose() { msg("{\"print\":{\"home_flag\":0}}"); }

void test_quick_tap_turns_leds_off_and_it_sticks() {
  printerConfig.doorToggleWindowMs = 4000;  // opt-in setting
  mqttChange();
  TEST_ASSERT_TRUE(ledsAre(0, 0, 0, 255, 255));
  doorOpen(); advance(1000); doorClose();
  TEST_ASSERT_TRUE(ledsOff());              // 2.3.0: back on within the same message
  advance(1000); ledsloop();
  TEST_ASSERT_TRUE(ledsOff());              // and stays off
}

void test_quick_tap_turns_leds_back_on() {
  printerConfig.doorToggleWindowMs = 4000;  // opt-in setting
  mqttChange();
  doorOpen(); advance(1000); doorClose();
  TEST_ASSERT_TRUE(ledsOff());
  advance(5000);
  doorOpen(); advance(1000); doorClose();
  TEST_ASSERT_TRUE(ledsAre(0, 0, 0, 255, 255));
  advance(60000); ledsloop();               // the idle timer restarted, so no instant re-off
  TEST_ASSERT_TRUE(ledsAre(0, 0, 0, 255, 255));
}

void test_quick_tap_window_is_configurable() {
  mqttChange();
  printerConfig.doorToggleWindowMs = 4000;
  doorOpen(); advance(3000); doorClose();   // 3 s: inside the default 4 s window
  TEST_ASSERT_TRUE(ledsOff());
  advance(5000);
  printerConfig.doorToggleWindowMs = 0;     // 0 = shortcut disabled
  doorOpen(); advance(500); doorClose();
  TEST_ASSERT_TRUE(ledsAre(0, 0, 0, 255, 255)); // normal door use relights
}

void test_slow_door_use_relights_after_a_tap_off() {
  printerConfig.doorToggleWindowMs = 4000;  // opt-in setting
  mqttChange();
  doorOpen(); advance(1000); doorClose();
  TEST_ASSERT_TRUE(ledsOff());
  advance(5000);
  doorOpen();                               // a normal open turns them on
  TEST_ASSERT_TRUE(ledsAre(0, 0, 0, 255, 255));
}

static void finishPrint() {
  printerVariables.gcodeState = "RUNNING";
  advance(4000);
  msg("{\"print\":{\"gcode_state\":\"FINISH\",\"stg_cur\":-1}}");
  TEST_ASSERT_TRUE(ledsAre(0, 255, 0, 0, 0));
}

void test_finish_door_mode_ends_on_door_only() {
  printerConfig.finishExit = true; printerConfig.finishExitTimer = false;
  printerConfig.finishTimeOut = 600000;
  finishPrint();
  advance(700000); ledsloop();
  TEST_ASSERT_TRUE(ledsAre(0, 255, 0, 0, 0)); // timer ignored
  doorOpen(); advance(5000); doorClose(); ledsloop();
  TEST_ASSERT_TRUE(ledsAre(0, 0, 0, 255, 255));
}

void test_finish_timer_mode_ends_after_timeout() {
  printerConfig.finishExit = false; printerConfig.finishExitTimer = true;
  printerConfig.finishTimeOut = 600000;
  finishPrint();
  advance(300000); ledsloop();
  TEST_ASSERT_TRUE(ledsAre(0, 255, 0, 0, 0));
  advance(301000); ledsloop();
  TEST_ASSERT_TRUE(ledsAre(0, 0, 0, 255, 255));
}

void test_finish_either_mode_door_first() {
  printerConfig.finishExit = true; printerConfig.finishExitTimer = true;
  printerConfig.finishTimeOut = 600000;
  printerConfig.controlChamberLight = true;
  finishPrint();
  advance(60000);
  doorOpen(); advance(5000); doorClose(); ledsloop();
  TEST_ASSERT_TRUE(ledsAre(0, 0, 0, 255, 255));
  pendingChamberLight = -1;
  advance(600000); ledsloop();              // timer must not fire later (no surprise light-off)
  TEST_ASSERT_EQUAL(-1, pendingChamberLight);
}

void test_finish_either_mode_timer_first() {
  printerConfig.finishExit = true; printerConfig.finishExitTimer = true;
  printerConfig.finishTimeOut = 600000;
  finishPrint();
  advance(601000); ledsloop();
  TEST_ASSERT_TRUE(ledsAre(0, 0, 0, 255, 255));
  TEST_ASSERT_FALSE(printerVariables.waitingForDoor);
}

void test_timer_mode_not_active_after_boot_without_a_print() {
  printerConfig.finishExit = false; printerConfig.finishExitTimer = true;
  printerConfig.finishTimeOut = 600000;
  printerConfig.finishStartms = 0;           // never finished since boot
  mqttChange();
  TEST_ASSERT_TRUE(ledsAre(0, 0, 0, 255, 255)); // idle white, not blocked by a phantom finish
}

void test_gcode_finish_sets_finished_and_shows_finish_colour() {
  printerVariables.gcodeState = "RUNNING";
  advance(4000);
  msg("{\"print\":{\"gcode_state\":\"FINISH\",\"stg_cur\":-1}}");
  TEST_ASSERT_TRUE(printerVariables.waitingForDoor);
  TEST_ASSERT_TRUE(ledsAre(0, 255, 0, 0, 0));
}

void test_hms_override_clears_with_debug_logging_off() {
  printerConfig.debuging = false;
  printerConfig.debugingchange = false;
  // front cover removed: HMS_0300_1200_0002_0001 -> override stage 17 (red)
  msg("{\"print\":{\"hms\":[{\"attr\":50336256,\"code\":131073}]}}");
  TEST_ASSERT_EQUAL(17, printerVariables.overridestage);
  TEST_ASSERT_TRUE(ledsAre(255, 0, 0, 0, 0));
  msg("{\"print\":{\"hms\":[]}}");
  TEST_ASSERT_EQUAL(999, printerVariables.overridestage); // was stuck at 17 in 2.3.0
  TEST_ASSERT_FALSE(ledsAre(255, 0, 0, 0, 0));
}

void test_hms_ignore_list_is_respected() {
  printerConfig.hmsIgnoreList = "HMS_0300-1200-0002-0001";
  msg("{\"print\":{\"hms\":[{\"attr\":50336256,\"code\":131073}]}}");
  TEST_ASSERT_EQUAL(999, printerVariables.overridestage);
  TEST_ASSERT_EQUAL_STRING("", printerVariables.parsedHMSlevel.c_str());
}

void test_chamber_light_report_drives_replicate() {
  advance(4000);
  msg("{\"print\":{\"lights_report\":[{\"node\":\"chamber_light\",\"mode\":\"off\"}]}}");
  TEST_ASSERT_FALSE(printerVariables.printerledstate);
  TEST_ASSERT_TRUE(ledsOff());
  advance(4000);
  msg("{\"print\":{\"lights_report\":[{\"node\":\"chamber_light\",\"mode\":\"on\"}]}}");
  TEST_ASSERT_TRUE(ledsAre(0, 0, 0, 255, 255));
}

void test_chamber_light_publish_is_queued_for_the_mqtt_task() {
  printerConfig.controlChamberLight = true;
  controlChamberLight(true);
  TEST_ASSERT_EQUAL(1, pendingChamberLight);
  TEST_ASSERT_EQUAL(0, (int)fake::published.size()); // not sent from this task
  publishChamberLight(true);
  TEST_ASSERT_EQUAL(1, (int)fake::published.size());
  TEST_ASSERT_NOT_EQUAL(-1, (int)fake::published[0].payload.find("\"led_mode\":\"on\""));
}

void test_ignored_command_messages_change_nothing() {
  fake::writes = 0;
  msg("{\"print\":{\"command\":\"gcode_line\"}}");
  TEST_ASSERT_EQUAL(0, fake::writes);
}

void test_quick_tap_during_finish_acknowledges_instead_of_toggling() {
  printerConfig.doorToggleWindowMs = 4000;  // opt-in setting
  printerConfig.finishExit = true; printerConfig.finishExitTimer = false;
  finishPrint();                                  // green
  doorOpen(); advance(1500); doorClose();
  TEST_ASSERT_FALSE(ledsOff());                   // no toggle-off flicker before ledsloop runs
  ledsloop();
  TEST_ASSERT_TRUE(ledsAre(0, 0, 0, 255, 255));   // back to white, not off
}

void test_tap_queued_in_maintenance_mode_does_not_fire_later() {
  printerConfig.doorToggleWindowMs = 4000;  // opt-in setting
  printerConfig.maintMode = true;
  mqttChange();
  doorOpen(); advance(1000); doorClose();         // toggle can't run in maintenance mode
  advance(5000);
  printerConfig.maintMode = false;
  mqttChange();                                   // back to replicate/idle
  TEST_ASSERT_TRUE(ledsAre(0, 0, 0, 255, 255));   // no surprise flip to off
  doorOpen();                                     // the next door open clears anything stale
  TEST_ASSERT_FALSE(printerVariables.doorSwitchTriggered);
}

void test_door_quick_tap_does_nothing_by_default() {
  mqttChange();
  TEST_ASSERT_EQUAL(0, printerConfig.doorToggleWindowMs); // off unless turned on
  doorOpen(); advance(1000); doorClose();
  advance(1000); ledsloop();
  TEST_ASSERT_TRUE(ledsAre(0, 0, 0, 255, 255));          // strip stays on
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_door_bit_23_opens_and_closes);
  RUN_TEST(test_quick_tap_turns_leds_off_and_it_sticks);
  RUN_TEST(test_quick_tap_turns_leds_back_on);
  RUN_TEST(test_quick_tap_window_is_configurable);
  RUN_TEST(test_slow_door_use_relights_after_a_tap_off);
  RUN_TEST(test_finish_door_mode_ends_on_door_only);
  RUN_TEST(test_finish_timer_mode_ends_after_timeout);
  RUN_TEST(test_finish_either_mode_door_first);
  RUN_TEST(test_finish_either_mode_timer_first);
  RUN_TEST(test_timer_mode_not_active_after_boot_without_a_print);
  RUN_TEST(test_gcode_finish_sets_finished_and_shows_finish_colour);
  RUN_TEST(test_hms_override_clears_with_debug_logging_off);
  RUN_TEST(test_hms_ignore_list_is_respected);
  RUN_TEST(test_chamber_light_report_drives_replicate);
  RUN_TEST(test_chamber_light_publish_is_queued_for_the_mqtt_task);
  RUN_TEST(test_ignored_command_messages_change_nothing);
  RUN_TEST(test_quick_tap_during_finish_acknowledges_instead_of_toggling);
  RUN_TEST(test_tap_queued_in_maintenance_mode_does_not_fire_later);
  RUN_TEST(test_door_quick_tap_does_nothing_by_default);
  return UNITY_END();
}
