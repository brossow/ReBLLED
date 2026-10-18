// Settings v2: JSON round trips, robustness against bad values, and migration from v1
// (including a sanitized copy of a real v1 config).
#include <unity.h>
#include "Arduino.h"
#include <ArduinoJson.h>
#include <fstream>
#include <sstream>
#include "../../src/blled/settings.h"

using namespace v2;

void setUp() {}
void tearDown() {}

static std::string toText(const Settings &s)
{
    JsonDocument d; toJson(s, d.to<JsonObject>());
    std::string out; serializeJson(d, out); return out;
}
static Settings fromText(const char *json)
{
    JsonDocument d; TEST_ASSERT_FALSE(deserializeJson(d, json));
    return load(d.as<JsonVariantConst>());
}
static std::string readFile(const char *path)
{
    std::ifstream f(path); std::stringstream ss; ss << f.rdbuf(); return ss.str();
}
static void assertColour(const LookColour &c, ColourKind k, int r = -1, int g = -1, int b = -1, int ww = 0, int cw = 0)
{
    TEST_ASSERT_EQUAL_MESSAGE(k, c.kind, "kind");
    if (r >= 0) { TEST_ASSERT_EQUAL(r, c.r); TEST_ASSERT_EQUAL(g, c.g); TEST_ASSERT_EQUAL(b, c.b); }
    TEST_ASSERT_EQUAL_MESSAGE(ww, c.ww, "ww"); TEST_ASSERT_EQUAL_MESSAGE(cw, c.cw, "cw");
}

void test_defaults_are_standard_look() {
    Settings s = defaults();
    TEST_ASSERT_EQUAL(LOOK_STANDARD, s.look);
    TEST_ASSERT_EQUAL(MODE_FOLLOW, s.mode);
    Looks L = effectiveLooks(s);
    assertColour(L.state[ST_ERROR].colour, CK_CUSTOM, 255, 0, 0);
    TEST_ASSERT_EQUAL(R_BLINK, L.state[ST_ERROR].rhythm);
    assertColour(L.prep[PS_LEVEL], CK_CUSTOM, 0, 0, 0x55);
}

void test_white_preset() {
    Settings s = defaults(); s.look = LOOK_WHITE;
    Looks L = effectiveLooks(s);
    assertColour(L.state[ST_ERROR].colour, CK_NEUTRAL);
    TEST_ASSERT_EQUAL(R_BLINK, L.state[ST_ERROR].rhythm);
    TEST_ASSERT_EQUAL(R_SLOW, L.state[ST_FINISHED].rhythm);
}

void test_custom_set_is_kept_while_a_preset_is_chosen() {
    Settings s = defaults();
    s.custom.state[ST_ERROR].colour = rgb(0xc0, 0, 0xff);
    s.look = LOOK_STANDARD;
    assertColour(effectiveLooks(s).state[ST_ERROR].colour, CK_CUSTOM, 255, 0, 0);   // preset in effect
    Settings back = fromText(toText(s).c_str());
    assertColour(back.custom.state[ST_ERROR].colour, CK_CUSTOM, 0xc0, 0, 0xff);       // custom survives
    back.look = LOOK_CUSTOM;
    assertColour(effectiveLooks(back).state[ST_ERROR].colour, CK_CUSTOM, 0xc0, 0, 0xff);
}

void test_round_trip_everything() {
    Settings s = defaults();
    s.setupDone = true; s.mode = MODE_OFF; s.brightness = 42; s.look = LOOK_CUSTOM;
    s.custom.state[ST_FINISHED].colour = rgb(0, 255, 0, 255, 255);   // colour + both whites
    s.custom.err[EK_RUNOUT] = rgb(0xc0, 0, 0xff);
    s.followLight = false; s.idleOffMin = 0; s.finishEnds = FIN_EITHER; s.finishMin = 25; s.offlineOffSec = 300;
    s.lightOnPrintStart = true; s.lightOffIdle = true;
    s.strip = STRIP_RGBW; s.wiring[L_R] = OUT_G; s.wiring[L_G] = OUT_R; s.wiring[L_W] = OUT_CW;
    strlcpy(s.model, "X2D", sizeof s.model);
    s.hmsIgnore = "HMS_0C00_0300_0002_0002"; s.debugLog = true;
    std::string a = toText(s);
    std::string b = toText(fromText(a.c_str()));
    TEST_ASSERT_EQUAL_STRING(a.c_str(), b.c_str());
    TEST_ASSERT_LESS_THAN(3000, (int)a.size());   // small enough for the settings file
}

void test_bad_values_fall_back_instead_of_crashing() {
    Settings s = fromText(R"({"version":2,"mode":"party","brightness":999,"look":"neon","strip":"laser",
        "finishEnds":7,"finishMin":0,"wiring":{"r":"X","g":"CW"},
        "custom":{"error":{"colour":{"kind":"custom","hex":"red"},"rhythm":"wobble"},
                  "idle":{"colour":{"kind":"warm"}}},"model":"A-very-long-model-name-indeed"})");
    TEST_ASSERT_EQUAL(MODE_FOLLOW, s.mode);
    TEST_ASSERT_EQUAL(100, s.brightness);
    TEST_ASSERT_EQUAL(LOOK_STANDARD, s.look);
    TEST_ASSERT_EQUAL(STRIP_RGBWW, s.strip);
    TEST_ASSERT_EQUAL(FIN_DOOR, s.finishEnds);
    TEST_ASSERT_EQUAL(10, s.finishMin);
    TEST_ASSERT_EQUAL(OUT_NONE, s.wiring[L_R]);
    TEST_ASSERT_EQUAL(OUT_CW, s.wiring[L_G]);
    assertColour(s.custom.state[ST_ERROR].colour, CK_CUSTOM, 255, 0, 0);   // bad hex -> default
    TEST_ASSERT_EQUAL(R_BLINK, s.custom.state[ST_ERROR].rhythm);           // bad rhythm -> default
    assertColour(s.custom.state[ST_IDLE].colour, CK_WARM);                  // good value kept
    TEST_ASSERT_TRUE(strlen(s.model) < sizeof s.model);                     // truncated, terminated
}

void test_migrate_a_real_config() {
    std::string json = readFile("test/fixtures/v1_real_sanitized.json");
    TEST_ASSERT_TRUE_MESSAGE(json.size() > 100, "fixture not found (run from the project root)");
    bool migrated = false;
    JsonDocument d; TEST_ASSERT_FALSE(deserializeJson(d, json));
    Settings s = load(d.as<JsonVariantConst>(), &migrated);
    TEST_ASSERT_TRUE(migrated);
    TEST_ASSERT_TRUE(s.setupDone);
    TEST_ASSERT_EQUAL(MODE_FOLLOW, s.mode);
    TEST_ASSERT_TRUE(s.followLight);
    TEST_ASSERT_EQUAL(100, s.brightness);
    TEST_ASSERT_EQUAL(LOOK_CUSTOM, s.look);                      // his colours differ from Standard
    const Looks &L = s.custom;
    assertColour(L.state[ST_PRINTING].colour, CK_NEUTRAL);         // WW + CW 255
    assertColour(L.state[ST_ATTENTION].colour, CK_CUSTOM, 0x00, 0x80, 0xff);
    assertColour(L.attn[AK_FIRSTLAYER], CK_CUSTOM, 0xff, 0x00, 0xff);
    assertColour(L.attn[AK_CLOG], CK_CUSTOM, 0xff, 0x00, 0xff);
    assertColour(L.state[ST_ERROR].colour, CK_CUSTOM, 255, 0, 0);
    assertColour(L.err[EK_RUNOUT], CK_CUSTOM, 0xc0, 0x00, 0xff);   // purple runout kept
    TEST_ASSERT_EQUAL(CK_SAME, L.err[EK_COVER].kind);
    TEST_ASSERT_EQUAL(CK_SAME, L.err[EK_HMS_FATAL].kind);
    assertColour(L.state[ST_FINISHED].colour, CK_CUSTOM, 0, 255, 0, 255, 255);   // green + both whites
    TEST_ASSERT_EQUAL(R_SLOW, L.state[ST_FINISHED].rhythm);
    assertColour(L.state[ST_PREPARING].colour, CK_OFF);
    assertColour(L.prep[PS_LEVEL], CK_CUSTOM, 0, 0, 0x55);
    TEST_ASSERT_EQUAL(CK_SAME, L.prep[PS_SCAN].kind);
    TEST_ASSERT_EQUAL(10, s.idleOffMin);
    TEST_ASSERT_EQUAL(FIN_DOOR, s.finishEnds);
    TEST_ASSERT_EQUAL(10, s.finishMin);
    TEST_ASSERT_FALSE(s.lightOnPrintStart || s.lightOnDoor || s.lightOffIdle || s.lightOffFinishTimer);
    TEST_ASSERT_EQUAL(STRIP_RGBWW, s.strip);
    TEST_ASSERT_EQUAL(5, (int)std::count(s.hmsIgnore.c_str(), s.hmsIgnore.c_str() + s.hmsIgnore.length(), '\n') + 1);
    TEST_ASSERT_FALSE(s.debugLog);
}

// The upstream v1 defaults (main.cpp defaultcolors) are exactly the Standard look.
static const char *V1_DEFAULTS = R"({"brightness":20,"replicatestate":true,
  "runningRGB":"#000000","runningWW":255,"runningCW":255,"finishindication":true,"finishColor":"#00ff00","finishWW":0,"finishCW":0,
  "stage14RGB":"#000000","stage1RGB":"#000055","stage8RGB":"#000000","stage9RGB":"#000000","stage10RGB":"#000000",
  "pauseRGB":"#0000ff","firstlayerRGB":"#0000ff","nozzleclogRGB":"#0000ff",
  "hmsSeriousRGB":"#ff0000","hmsFatalRGB":"#ff0000","filamentRunoutRGB":"#ff0000","frontCoverRGB":"#ff0000",
  "nozzleTempRGB":"#ff0000","bedTempRGB":"#ff0000","errordetection":true,"inactivityEnabled":true,
  "inactivityTimeOut":3600000,"finishExit":true,"finishTimerMins":600000})";

void test_migrate_upstream_defaults_gives_standard_look() {
    Settings s = fromText(V1_DEFAULTS);
    TEST_ASSERT_EQUAL(LOOK_STANDARD, s.look);
    TEST_ASSERT_EQUAL(60, s.idleOffMin);
    TEST_ASSERT_EQUAL(20, s.brightness);
}

void test_migrate_behaviour_switches() {
    Settings a = fromText(R"({"maintMode":true,"replicatestate":false,"inactivityEnabled":false,
        "finishExit":false,"finishTimerMins":300000,"controlChamberLight":true,"errordetection":false,
        "hmsSeriousRGB":"#ff0000","runningRGB":"#000000","runningWW":255,"runningCW":0})");
    TEST_ASSERT_EQUAL(MODE_ON, a.mode);
    TEST_ASSERT_FALSE(a.followLight);
    TEST_ASSERT_EQUAL(0, a.idleOffMin);
    TEST_ASSERT_EQUAL(FIN_TIMER, a.finishEnds);
    TEST_ASSERT_EQUAL(5, a.finishMin);
    TEST_ASSERT_TRUE(a.lightOnPrintStart && a.lightOnDoor && a.lightOffIdle && a.lightOffFinishTimer);
    assertColour(a.custom.state[ST_ERROR].colour, CK_WARM);   // error detection off: running colour
    TEST_ASSERT_EQUAL(R_STEADY, a.custom.state[ST_ERROR].rhythm);

    Settings b = fromText(R"({"finishExit":true,"finishExitTimer":true,"statusAnimations":false,"finishindication":false})");
    TEST_ASSERT_EQUAL(FIN_EITHER, b.finishEnds);
    TEST_ASSERT_EQUAL(R_STEADY, b.custom.state[ST_ATTENTION].rhythm);   // rhythms were turned off
    TEST_ASSERT_EQUAL(R_STEADY, b.custom.state[ST_FINISHED].rhythm);
}

void test_tools_modes_migrate_to_follow() {
    TEST_ASSERT_EQUAL(MODE_FOLLOW, fromText(R"({"discoMode":true})").mode);
    TEST_ASSERT_EQUAL(MODE_FOLLOW, fromText(R"({"showtestcolor":true})").mode);
    TEST_ASSERT_EQUAL(MODE_FOLLOW, fromText(R"({"debugwifi":true})").mode);
}

void test_v2_is_not_migrated_again() {
    bool migrated = true;
    JsonDocument d; deserializeJson(d, R"({"version":2,"mode":"off","maintMode":true})");
    Settings s = load(d.as<JsonVariantConst>(), &migrated);
    TEST_ASSERT_FALSE(migrated);
    TEST_ASSERT_EQUAL(MODE_OFF, s.mode);   // v1 keys ignored
}

void test_patch_changes_only_what_is_sent() {
    Settings s = defaults(); s.brightness = 40; s.custom.err[EK_RUNOUT] = rgb(0xc0, 0, 0xff);
    JsonDocument p; deserializeJson(p, R"({"idleOffMin":30,"custom":{"error":{"rhythm":"pulse"}}})");
    Settings n = applyPatch(s, p.as<JsonVariantConst>());
    TEST_ASSERT_EQUAL(30, n.idleOffMin);
    TEST_ASSERT_EQUAL(R_PULSE, n.custom.state[ST_ERROR].rhythm);
    assertColour(n.custom.state[ST_ERROR].colour, CK_CUSTOM, 255, 0, 0);   // sibling key kept
    assertColour(n.custom.err[EK_RUNOUT], CK_CUSTOM, 0xc0, 0, 0xff);       // untouched branch kept
    TEST_ASSERT_EQUAL(40, n.brightness);
}
void test_patch_rejects_bad_values_and_stays_v2() {
    Settings s = defaults();
    JsonDocument p; deserializeJson(p, R"({"version":1,"mode":"party","brightness":-5,"wiring":{"r":"G"}})");
    Settings n = applyPatch(s, p.as<JsonVariantConst>());
    TEST_ASSERT_EQUAL(MODE_FOLLOW, n.mode);
    TEST_ASSERT_EQUAL(0, n.brightness);           // clamped
    TEST_ASSERT_EQUAL(OUT_G, n.wiring[L_R]);
    JsonDocument q; deserializeJson(q, "[1,2,3]");
    TEST_ASSERT_EQUAL(s.brightness, applyPatch(s, q.as<JsonVariantConst>()).brightness);   // not an object
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_defaults_are_standard_look);
    RUN_TEST(test_white_preset);
    RUN_TEST(test_custom_set_is_kept_while_a_preset_is_chosen);
    RUN_TEST(test_round_trip_everything);
    RUN_TEST(test_bad_values_fall_back_instead_of_crashing);
    RUN_TEST(test_migrate_a_real_config);
    RUN_TEST(test_migrate_upstream_defaults_gives_standard_look);
    RUN_TEST(test_migrate_behaviour_switches);
    RUN_TEST(test_tools_modes_migrate_to_follow);
    RUN_TEST(test_v2_is_not_migrated_again);
    RUN_TEST(test_patch_changes_only_what_is_sent);
    RUN_TEST(test_patch_rejects_bad_values_and_stays_v2);
    return UNITY_END();
}
