// Status rhythms (blink / pulse) so states are readable without telling colours apart.
#include <unity.h>
#include "../harness.h"

void setUp() { freshDevice(); }
void tearDown() {}

// Run ledsloop() for `ms`, recording the min and max duty seen on one channel.
struct Range { uint32_t lo = 0xFFFFFFFF, hi = 0; };
static Range watch(int channel, unsigned long ms) {
  Range r;
  unsigned long end = millis() + ms;
  while (millis() < end) {
    ledsloop();                  // advances the fake clock by its 10 ms delay
    uint32_t d = fake::duty[channel];
    r.lo = d < r.lo ? d : r.lo;
    r.hi = d > r.hi ? d : r.hi;
  }
  return r;
}

void test_error_blinks_on_and_off() {
  printerVariables.parsedHMSlevel = "Serious";
  mqttChange();
  Range r = watch(redChannel, 3000);
  TEST_ASSERT_EQUAL(0, r.lo);
  TEST_ASSERT_EQUAL(255, r.hi);
}

void test_pause_pulses_but_never_goes_dark() {
  printerVariables.gcodeState = "PAUSE"; printerVariables.stage = 16;
  mqttChange();
  Range r = watch(blueChannel, 3000);
  TEST_ASSERT_EQUAL(255, r.hi);
  TEST_ASSERT_GREATER_THAN(40, r.lo);
  TEST_ASSERT_LESS_THAN(120, r.lo);
}

void test_finish_pulses_slowly() {
  printerVariables.gcodeState = "FINISH";
  printerVariables.finished = true;
  printerVariables.waitingForDoor = true;
  printerConfig.finish_check = true;
  printerConfig.finishStartms = millis();
  mqttChange();
  Range fast = watch(greenChannel, 1000);   // within 1 s it shouldn't cover the full swing
  Range full = watch(greenChannel, 4500);
  TEST_ASSERT_LESS_THAN(full.hi - full.lo, fast.hi - fast.lo);
  TEST_ASSERT_EQUAL(255, full.hi);
  TEST_ASSERT_GREATER_THAN(60, full.lo);
}

void test_printing_white_is_steady() {
  printerVariables.gcodeState = "RUNNING"; printerVariables.stage = 0;
  mqttChange();
  Range r = watch(warmChannel, 3000);
  TEST_ASSERT_EQUAL(255, r.lo);
  TEST_ASSERT_EQUAL(255, r.hi);
}

void test_animations_can_be_turned_off() {
  printerConfig.statusAnimations = false;
  printerVariables.parsedHMSlevel = "Serious";
  mqttChange();
  Range r = watch(redChannel, 3000);
  TEST_ASSERT_EQUAL(255, r.lo);
  TEST_ASSERT_EQUAL(255, r.hi);
}

void test_error_clearing_returns_to_steady_white() {
  printerVariables.parsedHMSlevel = "Serious";
  mqttChange();
  watch(redChannel, 1500);
  printerVariables.parsedHMSlevel = "";
  mqttChange();
  Range r = watch(warmChannel, 2000);
  TEST_ASSERT_EQUAL(255, r.lo);
  TEST_ASSERT_EQUAL(0, fake::duty[redChannel]);
}

void test_blink_respects_brightness() {
  printerConfig.brightness = 40;
  printerVariables.parsedHMSlevel = "Serious";
  mqttChange();
  Range r = watch(redChannel, 3000);
  TEST_ASSERT_EQUAL(102, r.hi);
  TEST_ASSERT_EQUAL(0, r.lo);
}

void test_brightness_change_keeps_the_effect() {
  printerVariables.parsedHMSlevel = "Serious";
  mqttChange();
  setBrightness(50);
  Range r = watch(redChannel, 3000);
  TEST_ASSERT_EQUAL(127, r.hi);
  TEST_ASSERT_EQUAL(0, r.lo);   // still blinking
}

void test_blink_keeps_its_rhythm_through_frequent_updates() {
  printerVariables.parsedHMSlevel = "Serious";
  mqttChange();
  Range r;
  for (int i = 0; i < 10; i++) {          // an MQTT update every 300 ms for 3 s
    mqttChange();
    Range part = watch(redChannel, 300);
    r.lo = part.lo < r.lo ? part.lo : r.lo;
    r.hi = part.hi > r.hi ? part.hi : r.hi;
  }
  TEST_ASSERT_EQUAL(0, r.lo);              // still reaches the "off" half of the blink
  TEST_ASSERT_EQUAL(255, r.hi);
}

// Review finding: a pulse/blink giving way to a STEADY state of the same colour left the
// PWM at whatever level the last animation frame wrote.
void test_effect_to_steady_same_colour_restores_full_level() {
  printerConfig.pauseRGB = printerConfig.runningColor;   // same colour for pause and printing
  printerVariables.gcodeState = "RUNNING"; printerVariables.stage = 0;
  mqttChange();
  printerVariables.gcodeState = "PAUSE"; printerVariables.stage = 16;
  mqttChange();
  watch(warmChannel, 700);                                // stop mid-pulse
  printerVariables.gcodeState = "RUNNING"; printerVariables.stage = 0;
  mqttChange();
  Range r = watch(warmChannel, 1000);
  TEST_ASSERT_EQUAL(255, r.lo);
  TEST_ASSERT_EQUAL(255, r.hi);
}

void test_blink_to_steady_same_colour_not_left_dark() {
  printerConfig.hmsSeriousRGB = hex2rgb("#00FF00");
  printerConfig.finishColor = hex2rgb("#00FF00");
  printerVariables.parsedHMSlevel = "Serious";
  mqttChange();
  watch(greenChannel, 700);                               // ends in the blink's off half
  printerVariables.parsedHMSlevel = "";
  printerVariables.gcodeState = "FINISH";
  printerVariables.finished = true; printerVariables.waitingForDoor = true;
  printerConfig.finish_check = true; printerConfig.finishStartms = millis();
  printerConfig.statusAnimations = false;                 // finish shown steady
  mqttChange();
  TEST_ASSERT_EQUAL(255, fake::duty[greenChannel]);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_error_blinks_on_and_off);
  RUN_TEST(test_pause_pulses_but_never_goes_dark);
  RUN_TEST(test_finish_pulses_slowly);
  RUN_TEST(test_printing_white_is_steady);
  RUN_TEST(test_animations_can_be_turned_off);
  RUN_TEST(test_error_clearing_returns_to_steady_white);
  RUN_TEST(test_blink_respects_brightness);
  RUN_TEST(test_brightness_change_keeps_the_effect);
  RUN_TEST(test_blink_keeps_its_rhythm_through_frequent_updates);
  RUN_TEST(test_effect_to_steady_same_colour_restores_full_level);
  RUN_TEST(test_blink_to_steady_same_colour_not_left_dark);
  return UNITY_END();
}
