// The printer-report parser: what state it derives from real-shaped MQTT messages.
// (What the strip then does is covered in test_runtime.)
#include <unity.h>
#include "../harness.h"

void setUp() { freshDevice(); }
void tearDown() {}

void test_door_bit_23_opens_and_closes() {
  doorOpen();
  TEST_ASSERT_TRUE(printerVariables.doorOpen);
  advance(5000);
  doorClose();
  TEST_ASSERT_FALSE(printerVariables.doorOpen);
}
void test_gcode_state_is_ignored_for_3s_after_a_command() {
  msg("{\"print\":{\"command\":\"pause\"}}");
  TEST_ASSERT_EQUAL_STRING("PAUSE", printerVariables.gcodeState.c_str());
  advance(1000);
  msg("{\"print\":{\"gcode_state\":\"RUNNING\"}}");    // the slower status report lags
  TEST_ASSERT_EQUAL_STRING("PAUSE", printerVariables.gcodeState.c_str());
  gcode("RUNNING", 0);
  TEST_ASSERT_EQUAL_STRING("RUNNING", printerVariables.gcodeState.c_str());
}
void test_hms_override_clears_with_debug_logging_off() {
  // front cover removed: HMS_0300_1200_0002_0001 -> override stage 17
  msg("{\"print\":{\"hms\":[{\"attr\":50336256,\"code\":131073}]}}");
  TEST_ASSERT_EQUAL(17, printerVariables.overridestage);
  msg("{\"print\":{\"hms\":[]}}");
  TEST_ASSERT_EQUAL(999, printerVariables.overridestage); // was stuck at 17 in 2.3.0
}
void test_hms_ignore_list_from_the_settings_is_respected() {
  settings.hmsIgnore = "HMS_0300-1200-0002-0001"; lightsSettingsChanged();
  msg("{\"print\":{\"hms\":[{\"attr\":50336256,\"code\":131073}]}}");
  TEST_ASSERT_EQUAL(999, printerVariables.overridestage);
  TEST_ASSERT_EQUAL_STRING("", printerVariables.parsedHMSlevel.c_str());
}
void test_chamber_light_report_and_system_command() {
  printerLight(false);
  TEST_ASSERT_FALSE(printerVariables.printerledstate);
  msg("{\"system\":{\"command\":\"ledctrl\",\"led_mode\":\"on\"}}");
  TEST_ASSERT_TRUE(printerVariables.printerledstate);
}
void test_chamber_light_publish_is_queued_for_the_mqtt_task() {
  controlChamberLight(true);
  TEST_ASSERT_EQUAL(1, pendingChamberLight);
  TEST_ASSERT_EQUAL(0, (int)fake::published.size()); // not sent from this task
  publishChamberLight(true);
  TEST_ASSERT_EQUAL(1, (int)fake::published.size());
  TEST_ASSERT_NOT_EQUAL(-1, (int)fake::published[0].payload.find("\"led_mode\":\"on\""));
}
void test_ignored_command_messages_change_nothing() {
  msg("{\"print\":{\"command\":\"gcode_line\"}}");
  TEST_ASSERT_EQUAL(0, fake::writes);
}
void test_printer_changes_count_as_activity() {
  advance(5 * 60000);
  gcode("IDLE", 2);                                   // stage change only
  TEST_ASSERT_EQUAL(millis(), lastActivityMs);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_door_bit_23_opens_and_closes);
  RUN_TEST(test_gcode_state_is_ignored_for_3s_after_a_command);
  RUN_TEST(test_hms_override_clears_with_debug_logging_off);
  RUN_TEST(test_hms_ignore_list_from_the_settings_is_respected);
  RUN_TEST(test_chamber_light_report_and_system_command);
  RUN_TEST(test_chamber_light_publish_is_queued_for_the_mqtt_task);
  RUN_TEST(test_ignored_command_messages_change_nothing);
  RUN_TEST(test_printer_changes_count_as_activity);
  return UNITY_END();
}
