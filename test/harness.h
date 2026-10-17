// Shared setup for host tests: pulls in the real firmware headers with test stand-ins for
// the hardware, and gives helpers to set printer state and read what the LEDs were set to.
#pragma once
#define STRVERSION "test"
#include "Arduino.h"
#include "WiFi.h"

// The firmware logs through LogSerial (WebSerial + USB); here it just captures text.
inline CaptureSerial LogSerial;

// leds.h calls controlChamberLight() (defined in mqttmanager.h on the device). Tests of
// leds.h alone get a recorder; tests that include mqttmanager.h use the real one.
namespace fake { inline int chamberLightCalls = 0; inline int chamberLightLast = -1; }
#ifndef HARNESS_REAL_CHAMBER_LIGHT
void controlChamberLight(bool on) { fake::chamberLightCalls++; fake::chamberLightLast = on ? 1 : 0; }
#endif

#include "../src/blled/leds.h"

// LED channel duties as last written (r, g, b, warm white, cold white).
struct Duty { uint32_t r, g, b, ww, cw; };
inline Duty leds() { return { fake::duty[redChannel], fake::duty[greenChannel], fake::duty[blueChannel], fake::duty[warmChannel], fake::duty[coldChannel] }; }
inline bool ledsAre(uint32_t r, uint32_t g, uint32_t b, uint32_t ww, uint32_t cw) {
  Duty d = leds(); return d.r == r && d.g == g && d.b == b && d.ww == ww && d.cw == cw;
}
inline bool ledsOff() { return ledsAre(0, 0, 0, 0, 0); }

// Same defaults as defaultcolors() in main.cpp.
inline void loadDefaultColors() {
  printerConfig.runningColor = hex2rgb("#000000", 255, 255);
  printerConfig.testColor = hex2rgb("#3F3CFB");
  printerConfig.finishColor = hex2rgb("#00FF00");
  printerConfig.stage14Color = hex2rgb("#000000");
  printerConfig.stage1Color = hex2rgb("#000055");
  printerConfig.stage8Color = hex2rgb("#000000");
  printerConfig.stage9Color = hex2rgb("#000000");
  printerConfig.stage10Color = hex2rgb("#000000");
  printerConfig.wifiRGB = hex2rgb("#FFA500");
  printerConfig.pauseRGB = hex2rgb("#0000FF");
  printerConfig.firstlayerRGB = hex2rgb("#0000FF");
  printerConfig.nozzleclogRGB = hex2rgb("#0000FF");
  printerConfig.hmsSeriousRGB = hex2rgb("#FF0000");
  printerConfig.hmsFatalRGB = hex2rgb("#FF0000");
  printerConfig.filamentRunoutRGB = hex2rgb("#FF0000");
  printerConfig.frontCoverRGB = hex2rgb("#FF0000");
  printerConfig.nozzleTempRGB = hex2rgb("#FF0000");
  printerConfig.bedTempRGB = hex2rgb("#FF0000");
}

// Fresh device: default settings, printer online and idle with its chamber light on,
// LEDs off, boot-time updateleds() already done, clock well past start-up.
inline void freshDevice() {
  // (debugingchange on so failing tests show the LED decision log)
  printerVariables = PrinterVariables{};
  printerConfig = PrinterConfig{};
  loadDefaultColors();
  printerConfig.brightness = 100;
  printerConfig.debugingchange = true;
  for (auto &d : fake::duty) d = 0;
  fake::writes = 0;
  currentRed = currentGreen = currentBlue = currentWarm = currentCold = 0;
  fake::chamberLightCalls = 0; fake::chamberLightLast = -1;
  LogSerial.out.clear();
  fake::now_ms = 1000000;              // far from 0 so "time since" checks behave
  printerVariables.online = true;
  printerVariables.gcodeState = "IDLE";
  printerVariables.stage = -1;
  printerVariables.printerledstate = true;
  printerVariables.lastdoorOpenms = 0;
  printerVariables.lastdoorClosems = 0;
  printerConfig.inactivityStartms = fake::now_ms;
  updateleds();                        // first call is the "Initial Boot" no-op
  // Boot stamps lastdoorClosems, and replicate-OFF is ignored for 1 s after a door event.
  fake::now_ms += 2000;
  printerConfig.inactivityStartms = fake::now_ms;
}

// Mimic the flags ParseCallback sets on any MQTT change before it calls updateleds().
inline void mqttChange() {
  printerConfig.inactivityStartms = millis();
  printerConfig.isIdleOFFActive = false;
  printerConfig.maintMode_update = true;
  printerConfig.discoMode_update = true;
  printerConfig.replicate_update = true;
  printerConfig.testcolor_update = true;
  updateleds();
}
