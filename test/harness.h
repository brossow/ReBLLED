// Shared setup for host tests: pulls in the real firmware headers (MQTT parsing, the LED
// runtime and hardware layer) with test stand-ins for the hardware, and gives helpers to
// feed printer messages and read what the LED outputs were set to.
#pragma once
#define STRVERSION "test"
#include "Arduino.h"
#include "WiFi.h"

// The firmware logs through LogSerial (WebSerial + USB); here it just captures text.
inline CaptureSerial LogSerial;

#include "../src/blled/mqttmanager.h" // brings in lights.h and leds.h
// The config file isn't on the host; count the saves instead.
namespace fake { inline int saves = 0; }
void saveFileSystem() { fake::saves++; }
#include "../src/blled/api.h"

// Output duties as last written, board-label order (R, G, B, WW, CW).
struct Duty { uint32_t r, g, b, ww, cw; };
inline Duty leds() { return { fake::duty[redChannel], fake::duty[greenChannel], fake::duty[blueChannel], fake::duty[warmChannel], fake::duty[coldChannel] }; }
inline bool ledsAre(uint32_t r, uint32_t g, uint32_t b, uint32_t ww, uint32_t cw) {
  Duty d = leds(); return d.r == r && d.g == g && d.b == b && d.ww == ww && d.cw == cw;
}
inline bool ledsOff() { return ledsAre(0, 0, 0, 0, 0); }
inline bool ledsWhite() { return ledsAre(0, 0, 0, 255, 255); } // neutral white on RGBWW

inline void advance(unsigned long ms) { fake::now_ms += ms; }

// A printer report through the real parser.
inline void msg(const char *json) { ParseCallback((char *)"device/TEST/report", (byte *)json, strlen(json)); }
// gcode_state and lights_report are ignored for 3 s after a command; step past that.
inline void gcode(const char *state, int stage = -1) {
  advance(3500);
  char b[96]; snprintf(b, sizeof b, "{\"print\":{\"gcode_state\":\"%s\",\"stg_cur\":%d}}", state, stage);
  msg(b);
}
inline void doorOpen() { msg("{\"print\":{\"home_flag\":8388608}}"); } // bit 23
inline void doorClose() { msg("{\"print\":{\"home_flag\":0}}"); }
inline void printerLight(bool on) {
  advance(3500);
  msg(on ? "{\"print\":{\"lights_report\":[{\"node\":\"chamber_light\",\"mode\":\"on\"}]}}"
         : "{\"print\":{\"lights_report\":[{\"node\":\"chamber_light\",\"mode\":\"off\"}]}}");
}
// Run the main loop for a while (timers, re-evaluation, rhythms).
inline void loopFor(unsigned long ms) { unsigned long end = fake::now_ms + ms; while (fake::now_ms < end) lightsLoop(); }

// Fresh device: default v2 settings on an RGBWW strip, printer online and idle with its own
// light on, LEDs showing idle white, clock well past start-up.
inline void freshDevice(v2::Settings s = v2::defaults()) {
  printerVariables = PrinterVariables{};
  printerConfig = PrinterConfig{};
  settings = s;
  activeTool = v2::TOOL_NONE; checkOutput = -1; testRhythm = v2::R_STEADY; toolEndsMs = 0; lastDecision = v2::Decision{};
  finishShowing = false; finishStartMs = 0; printerLightOnByUs = false; idleOffActive = false;
  shown = Shown{};
  for (auto &d : fake::duty) d = 0;
  currentRed = currentGreen = currentBlue = currentWarm = currentCold = 0;
  currentEffect = EFFECT_SOLID; ledCycleActive = false;
  LogSerial.out.clear();
  fake::published.clear(); fake::mqttConnected = true;
  pendingChamberLight = -1;
  fake::saves = 0; api::dirty = false; api::frozen = false;
  fake::now_ms = 1000000;              // far from 0 so "time since" checks behave
  lastMQTTupdate = 0;
  printerVariables.online = true;
  printerVariables.gcodeState = "IDLE";
  printerVariables.printerledstate = true;
  lightsBegin();
  lightsUpdate(true);
  fake::writes = 0;
}
