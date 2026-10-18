// The setup page's JSON API (api.h): settings autosave, the status it explains itself with,
// tools and the wiring check.
#include <unity.h>
#include "../harness.h"

static JsonDocument body(const char *json) { JsonDocument d; deserializeJson(d, json); return d; }
static JsonDocument patch(const char *json) {
  JsonDocument in = body(json), out;
  api::patchSettings(in.as<JsonVariantConst>(), out.to<JsonObject>());
  return out;
}
static JsonDocument status() { JsonDocument d; api::status(d.to<JsonObject>()); return d; }

void setUp() { freshDevice(); }
void tearDown() {}

// ---- settings and autosave ----
void test_patch_replies_with_what_is_in_effect() {
  JsonDocument r = patch(R"({"idleOffMin":30,"brightness":250,"mode":"party"})");
  TEST_ASSERT_EQUAL(30, r["idleOffMin"].as<int>());
  TEST_ASSERT_EQUAL(100, r["brightness"].as<int>());          // clamped
  TEST_ASSERT_EQUAL_STRING("follow", r["mode"]);               // unknown value ignored
  TEST_ASSERT_EQUAL(30, settings.idleOffMin);
}
void test_patch_leaves_the_fade_to_the_loop() {
  unsigned long t = millis();
  patch(R"({"look":"custom","custom":{"idle":{"colour":{"kind":"custom","hex":"#ff0000"}}}})");
  TEST_ASSERT_EQUAL(t, millis());                             // no 500 ms fade on the web task
  TEST_ASSERT_TRUE(ledsWhite());
  lightsLoop();
  TEST_ASSERT_TRUE(ledsAre(255, 0, 0, 0, 0));
}
void test_brightness_applies_at_once_for_the_slider() {
  patch(R"({"brightness":40})");
  TEST_ASSERT_TRUE(ledsAre(0, 0, 0, 102, 102));
}
void test_autosave_waits_for_changes_to_stop() {
  for (int i = 0; i < 10; i++) { patch(R"({"brightness":50})"); advance(200); api::loop(); }
  TEST_ASSERT_EQUAL(0, fake::saves);                          // still dragging
  advance(1600); api::loop();
  TEST_ASSERT_EQUAL(1, fake::saves);
  advance(5000); api::loop();
  TEST_ASSERT_EQUAL(1, fake::saves);                          // nothing new to save
}
void test_restart_saves_pending_changes_but_not_after_a_restore() {
  patch(R"({"idleOffMin":5})");
  api::flush();
  TEST_ASSERT_EQUAL(1, fake::saves);
  patch(R"({"idleOffMin":6})");
  api::freeze();                                              // restore wrote a new file
  patch(R"({"idleOffMin":7})");
  api::flush(); advance(5000); api::loop();
  TEST_ASSERT_EQUAL(1, fake::saves);
}

// ---- status ----
void test_status_idle_with_countdown() {
  advance(4 * 60000); lightsLoop();
  JsonDocument s = status();
  TEST_ASSERT_EQUAL_STRING("idle", s["reason"]);
  TEST_ASSERT_EQUAL_STRING("colour", s["show"]);
  TEST_ASSERT_EQUAL_STRING("neutral", s["colour"]["kind"]);
  TEST_ASSERT_EQUAL_STRING("steady", s["rhythm"]);
  TEST_ASSERT_INT_WITHIN(2, 6 * 60, s["idleOffIn"].as<int>());
  TEST_ASSERT_EQUAL(255, s["outputs"][3].as<int>());
  TEST_ASSERT_TRUE(s["printer"]["connected"].as<bool>());
}
void test_status_idle_off_says_for_how_long() {
  loopFor(12 * 60000);
  JsonDocument s = status();
  TEST_ASSERT_EQUAL_STRING("idle_off", s["reason"]);
  TEST_ASSERT_EQUAL_STRING("off", s["show"]);
  TEST_ASSERT_INT_WITHIN(2, 12 * 60, s["idleFor"].as<int>());
  TEST_ASSERT_TRUE(s["idleOffIn"].isNull());
}
void test_status_names_the_error() {
  msg("{\"print\":{\"hms\":[{\"attr\":50336256,\"code\":131073}]}}"); // front cover
  JsonDocument s = status();
  TEST_ASSERT_EQUAL_STRING("error", s["reason"]);
  TEST_ASSERT_EQUAL_STRING("error", s["state"]);
  TEST_ASSERT_EQUAL_STRING("cover", s["detail"]);
  TEST_ASSERT_EQUAL_STRING("blink", s["rhythm"]);
}
void test_status_finish_timer_countdown() {
  patch(R"({"finishEnds":"timer","finishMin":5})");
  gcode("RUNNING", 0); gcode("FINISH", -1);
  advance(60000); lightsLoop();
  JsonDocument s = status();
  TEST_ASSERT_EQUAL_STRING("finished", s["reason"]);
  TEST_ASSERT_INT_WITHIN(2, 4 * 60, s["finishEndsIn"].as<int>());
}

// ---- tools and the wiring check ----
void test_test_colour_tool_and_stop() {
  JsonDocument in = body(R"({"tool":"test","colour":{"kind":"custom","hex":"#3f3cfb"}})"), out;
  TEST_ASSERT_TRUE(api::tool(in.as<JsonVariantConst>(), out.to<JsonObject>()));
  lightsLoop();
  TEST_ASSERT_TRUE(ledsAre(0x3f, 0x3c, 0xfb, 0, 0));
  JsonDocument s = status();
  TEST_ASSERT_EQUAL_STRING("test", s["tool"]);
  TEST_ASSERT_EQUAL_STRING("#3f3cfb", s["colour"]["hex"]);
  JsonDocument stop = body(R"({"tool":"none"})"), o2;
  api::tool(stop.as<JsonVariantConst>(), o2.to<JsonObject>());
  lightsLoop();
  TEST_ASSERT_TRUE(ledsWhite());
  TEST_ASSERT_EQUAL(0, fake::saves);                          // tools are never saved
}
void test_tools_leave_the_fade_to_the_loop() {
  unsigned long t = millis();
  JsonDocument in = body(R"({"tool":"test","colour":{"kind":"custom","hex":"#ff0000"}})"), out;
  api::tool(in.as<JsonVariantConst>(), out.to<JsonObject>());
  JsonDocument c = body(R"({"output":"B"})"), o2;
  api::check(c.as<JsonVariantConst>(), o2.to<JsonObject>());
  TEST_ASSERT_EQUAL(t, millis());
  TEST_ASSERT_TRUE(ledsWhite());
}
void test_test_colour_same_keeps_the_last_colour() {
  JsonDocument a = body(R"({"tool":"test","colour":{"kind":"custom","hex":"#00ff00"}})"), o1;
  api::tool(a.as<JsonVariantConst>(), o1.to<JsonObject>());
  JsonDocument b = body(R"({"tool":"test","colour":{"kind":"same"}})"), o2;
  api::tool(b.as<JsonVariantConst>(), o2.to<JsonObject>());
  lightsLoop();
  TEST_ASSERT_TRUE(ledsAre(0, 255, 0, 0, 0));
}
void test_unknown_tool_is_refused() {
  JsonDocument in = body(R"({"tool":"disco"})"), out;
  TEST_ASSERT_FALSE(api::tool(in.as<JsonVariantConst>(), out.to<JsonObject>()));
  TEST_ASSERT_EQUAL_STRING("unknown tool", out["error"]);
  TEST_ASSERT_EQUAL(v2::TOOL_NONE, activeTool);
}
void test_wiring_check_lights_one_output_and_ends() {
  JsonDocument in = body(R"({"output":"WW"})"), out;
  TEST_ASSERT_TRUE(api::check(in.as<JsonVariantConst>(), out.to<JsonObject>()));
  lightsLoop();
  TEST_ASSERT_TRUE(ledsAre(0, 0, 0, 255, 0));
  TEST_ASSERT_EQUAL_STRING("WW", status()["check"]);
  JsonDocument end = body(R"({"output":null})"), o2;
  TEST_ASSERT_TRUE(api::check(end.as<JsonVariantConst>(), o2.to<JsonObject>()));
  lightsLoop();
  TEST_ASSERT_TRUE(ledsWhite());
  JsonDocument bad = body(R"({"output":"X"})"), o3;
  TEST_ASSERT_FALSE(api::check(bad.as<JsonVariantConst>(), o3.to<JsonObject>()));
}
void test_saving_the_wiring_answer_applies_it() {
  patch(R"({"strip":"rgbw","wiring":{"w":"CW"}})");
  lightsLoop();
  TEST_ASSERT_TRUE(ledsAre(0, 0, 0, 0, 255));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_patch_replies_with_what_is_in_effect);
  RUN_TEST(test_patch_leaves_the_fade_to_the_loop);
  RUN_TEST(test_brightness_applies_at_once_for_the_slider);
  RUN_TEST(test_autosave_waits_for_changes_to_stop);
  RUN_TEST(test_restart_saves_pending_changes_but_not_after_a_restore);
  RUN_TEST(test_status_idle_with_countdown);
  RUN_TEST(test_status_idle_off_says_for_how_long);
  RUN_TEST(test_status_names_the_error);
  RUN_TEST(test_status_finish_timer_countdown);
  RUN_TEST(test_test_colour_tool_and_stop);
  RUN_TEST(test_tools_leave_the_fade_to_the_loop);
  RUN_TEST(test_test_colour_same_keeps_the_last_colour);
  RUN_TEST(test_unknown_tool_is_refused);
  RUN_TEST(test_wiring_check_lights_one_output_and_ends);
  RUN_TEST(test_saving_the_wiring_answer_applies_it);
  return UNITY_END();
}
