#ifndef _BLLED_LIGHTS
#define _BLLED_LIGHTS

// The LED runtime (docs/REDESIGN.md): holds the v2 settings and the few timers the rules
// need, builds a snapshot of the printer, asks decide() what the strip should show, and
// shows it. The strip always reflects decide(current state): lightsUpdate() runs every loop
// and only touches the LEDs when the answer changes, so timeouts need no special handling.

#include <Arduino.h>
#include <WiFi.h>
#include "types.h"
#include "settings.h"
#include "decide.h"
#include "leds.h"

v2::Settings settings = v2::defaults();
v2::Tool activeTool = v2::TOOL_NONE;        // temporary, never saved
v2::LookColour testColour = v2::rgb(255, 0, 0);
v2::Rhythm testRhythm = v2::R_STEADY;       // a Status colours preview shows the rhythm too
unsigned long toolEndsMs = 0;               // 0 = runs until Stop
v2::Decision lastDecision;
int8_t checkOutput = -1;                    // wiring check: the one output lit, or -1
// Set by the web server's task; the loop applies it. A fade takes 500 ms and must not run
// on the web server's task (it would stall every request and hold the lock).
volatile bool lightsRefresh = false;

unsigned long lastActivityMs = 0;           // door, printer events, a print running
bool finishShowing = false;                 // a print finished and its ending hasn't happened
unsigned long finishStartMs = 0;
bool printerLightOnByUs = false;            // we turned the printer's light on (door rule)
bool idleOffActive = false;

// Shown colour, to detect a change (decision + strip settings + brightness-independent).
struct Shown { uint8_t show = 255, rhythm = 0; uint8_t out[5] = {0, 0, 0, 0, 0}; int8_t check = -1; uint8_t tool = 0; };
Shown shown;

// Runtime mirrors of settings used by older code (logging, HMS ignore list).
void lightsSyncMirrors()
{
    printerConfig.debuging = settings.debugLog;
    printerConfig.debugingchange = settings.debugLog;
    printerConfig.mqttdebug = settings.mqttLog;
    printerConfig.hmsIgnoreList = settings.hmsIgnore;
    ledBrightness = settings.brightness;
}

// Model capabilities (docs/REDESIGN.md). Only lidar matters to decide(); unknown = assume it.
// Models known to have no lidar (the page's MODELS table); unknown codes = assume one.
bool modelHasLidar()
{
    const char *m = strcmp(settings.model, "auto") == 0 ? printerVariables.detectedModel : settings.model;
    static const char *const noLidar[] = {"P1P", "P1S", "P2S", "A1", "A1M", "A2L", "H2D", "H2DPRO", "H2S", "H2C", "X2D"};
    for (const char *x : noLidar)
        if (strcmp(m, x) == 0) return false;
    return true;
}

// Models without a door sensor (some have a door, e.g. the P1S); unknown = assume one.
bool modelHasDoorSensor()
{
    const char *m = strcmp(settings.model, "auto") == 0 ? printerVariables.detectedModel : settings.model;
    return !(strcmp(m, "P1P") == 0 || strcmp(m, "P1S") == 0 || strcmp(m, "A1") == 0 || strcmp(m, "A1M") == 0 || strcmp(m, "A2L") == 0);
}
// How the finished colour ends on this printer: without a door sensor, "door" means the timer.
v2::FinishEnds finishEnds()
{
    return !modelHasDoorSensor() && settings.finishEnds == v2::FIN_DOOR ? v2::FIN_TIMER : settings.finishEnds;
}

v2::Snapshot printerSnapshot()
{
    v2::Snapshot p;
    p.connected = printerVariables.online;
    p.accessRejected = printerVariables.accessRejected;
    p.certRejected = printerVariables.certRejected;
    p.everConnected = printerVariables.everConnected;
    p.unreachableMs = (!printerVariables.online && printerVariables.disconnectMQTTms) ? millis() - printerVariables.disconnectMQTTms : 0;
    p.gcodeState = printerVariables.gcodeState;
    p.stage = printerVariables.stage;
    p.overrideStage = printerVariables.overridestage;
    p.hms = printerVariables.parsedHMSlevel == "Fatal" ? v2::HMS_FATAL : printerVariables.parsedHMSlevel == "Serious" ? v2::HMS_SERIOUS : v2::HMS_NONE;
    p.printerLightOn = printerVariables.printerledstate;
    p.finishShowing = finishShowing;
    p.idleMs = millis() - lastActivityMs;
    p.hasLidar = modelHasLidar();
    return p;
}

// Outputs (R, G, B, WW, CW) for a colour on this strip with this wiring.
v2::OutputLevels outputsFor(const v2::LookColour &c)
{
    return v2::toOutputs(settings, v2::toLights(c, settings.strip));
}

LedEffect effectFor(v2::Rhythm r)
{
    switch (r)
    {
    case v2::R_BLINK: return EFFECT_BLINK;
    case v2::R_PULSE: return EFFECT_PULSE;
    case v2::R_SLOW: return EFFECT_PULSE_SLOW;
    default: return EFFECT_SOLID;
    }
}

v2::LookColour wifiColour()
{
    long rssi = WiFi.RSSI();
    if (rssi >= -50) return v2::rgb(0, 255, 0);
    if (rssi >= -60) return v2::rgb(128, 255, 0);
    if (rssi >= -70) return v2::rgb(255, 255, 0);
    if (rssi >= -80) return v2::rgb(255, 128, 0);
    return v2::rgb(255, 0, 0);
}

void lightsActivity() { lastActivityMs = millis(); }

// Start-up and connection colours (docs/REDESIGN.md, Help), through this strip's mapping so a
// white-only strip still shows something.
void lightsStatusColour(uint8_t r, uint8_t g, uint8_t b)
{
    v2::OutputLevels o = outputsFor(v2::rgb(r, g, b));
    showStatusColour(o.v[0], o.v[1], o.v[2], o.v[3], o.v[4]);
}

// Re-evaluate and, if the answer changed, show it. Cheap when nothing changed.
void lightsUpdate(bool force = false)
{
    StateLock lock;
    v2::Decision d = v2::decide(settings, printerSnapshot(), activeTool);
    if (activeTool == v2::TOOL_TEST)
    {
        d.colour = testColour;
        d.show = testColour.kind == v2::CK_OFF ? v2::SHOW_OFF : v2::SHOW_COLOUR;
        d.rhythm = d.show == v2::SHOW_OFF ? v2::R_STEADY : testRhythm;
    }

    // Idle-off just started: the printer's light rules that hang off it.
    const bool idleOff = d.reason == v2::RS_IDLE_OFF;
    if (idleOff && !idleOffActive && (settings.lightOffIdle || printerLightOnByUs))
    {
        controlChamberLight(false);
        printerLightOnByUs = false;
    }
    idleOffActive = idleOff;

    Shown next;
    next.show = d.show; next.rhythm = d.rhythm; next.check = checkOutput; next.tool = activeTool;
    v2::OutputLevels o;
    if (checkOutput >= 0) o.v[checkOutput] = 255;                         // wiring check
    else if (d.show == v2::SHOW_COLOUR) o = outputsFor(d.colour);
    else if (d.show == v2::SHOW_WIFI) o = outputsFor(wifiColour());
    memcpy(next.out, o.v, 5);

    const bool reasonChanged = d.reason != lastDecision.reason;
    lastDecision = d;
    if (!force && memcmp(&next, &shown, sizeof next) == 0)
        return;
    shown = next;

    ledCycleActive = d.show == v2::SHOW_CYCLE && checkOutput < 0;
    if (ledCycleActive)
        return; // RGBCycle() drives the outputs from the loop
    setEffect(checkOutput >= 0 ? EFFECT_SOLID : effectFor(d.rhythm));
    tweenToColor(o.v[0], o.v[1], o.v[2], o.v[3], o.v[4]);
    if (settings.debugLog && reasonChanged)
        LogSerial.printf("[LED] reason %d, show %d, out %d %d %d %d %d\n", d.reason, d.show, o.v[0], o.v[1], o.v[2], o.v[3], o.v[4]);
}

// ---- events from the printer (mqttmanager.h) ----
void lightsOnGcodeState(const String &before, const String &now)
{
    StateLock lock;
    if (now == "FINISH" && before != "FINISH")
    {
        finishShowing = true;
        finishStartMs = millis();
    }
    if (now == "RUNNING" || now == "PREPARE")
        finishShowing = false; // a new print
    if (now == "RUNNING" && before != "RUNNING" && settings.lightOnPrintStart && !printerVariables.printerledstate)
        controlChamberLight(true);
}

// Door: wakes the lights and acknowledges a finished print. Nothing else (docs/REDESIGN.md).
void lightsOnDoor(bool open)
{
    StateLock lock;
    lightsActivity();
    if (finishShowing && settings.finishEnds != v2::FIN_TIMER)
        finishShowing = false;
    if (open && settings.lightOnDoor && !printerVariables.printerledstate)
    {
        controlChamberLight(true);
        printerLightOnByUs = true;
    }
}

// Timers that end things (the finish colour). Called from the loop.
void lightsTimers()
{
    StateLock lock;
    if (toolEndsMs && (long)(millis() - toolEndsMs) >= 0)
    {
        activeTool = v2::TOOL_NONE; // a preview ran its time
        toolEndsMs = 0;
        lightsRefresh = true;
    }
    if (printerVariables.gcodeState == "RUNNING" || printerVariables.gcodeState == "PAUSE")
        lightsActivity(); // never idle during a print
    if (finishShowing && finishEnds() != v2::FIN_DOOR &&
        millis() - finishStartMs >= (unsigned long)settings.finishMin * 60000UL)
    {
        finishShowing = false;
        if (settings.lightOffFinishTimer)
            controlChamberLight(false);
    }
}

// ---- tools and the wiring check (setup page; web server task) ----
// `seconds` > 0 ends the tool by itself (a preview); 0 runs it until Stop.
void lightsSetTool(v2::Tool t, const v2::LookColour *colour = nullptr, v2::Rhythm rhythm = v2::R_STEADY, unsigned seconds = 0)
{
    StateLock lock;
    activeTool = t;
    if (colour) testColour = *colour;
    testRhythm = rhythm;
    toolEndsMs = seconds ? millis() + seconds * 1000UL : 0;
    if (!toolEndsMs && seconds) toolEndsMs = 1; // (millis() wrapped to exactly 0)
    lightsRefresh = true;
}
void lightsCheckOutput(int8_t output)
{
    StateLock lock;
    checkOutput = output >= 0 && output < v2::OUT_COUNT ? output : -1;
    lightsRefresh = true;
}

// After settings change (setup page, restore). Brightness applies at once (it's only a
// rescale); anything else is shown by the loop.
void lightsSettingsChanged()
{
    StateLock lock;
    const bool brightnessChanged = ledBrightness != settings.brightness;
    lightsSyncMirrors();
    if (brightnessChanged) setBrightness(settings.brightness);
    lightsRefresh = true;
}

void lightsLoop()
{
    {
        StateLock lock;
        lightsTimers();
        const bool force = lightsRefresh;
        lightsRefresh = false;
        lightsUpdate(force);
        // red/green/blue outputs for the colour cycle, honouring the wiring check
        int8_t rgbOut[3];
        for (int i = 0; i < 3; i++) { int8_t outs[2]; v2::outputsFor(settings, i, outs); rgbOut[i] = outs[0]; }
        RGBCycle(rgbOut);
        applyEffect();
    }
    delay(10); // outside the lock
}

void lightsBegin()
{
    lightsSyncMirrors();
    lightsRefresh = false;
    lastActivityMs = millis();
}

#endif
