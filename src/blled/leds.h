#ifndef _LED
#define _LED

// The LED hardware layer: five PWM outputs (board labels R, G, B, WW, CW), fades, the status
// rhythms, live brightness and the colour cycle. WHAT to show is decided in lights.h
// (via decide.h); this file only shows it.

#include <Arduino.h>
#include "mqttparsingutility.h"
#include "statelock.h"
void controlChamberLight(bool on); // mqttmanager.h

const int redPin = 19;
const int greenPin = 18;
const int bluePin = 21;
const int warmPin = 22;
const int coldPin = 23;

const int redChannel = 0;
const int greenChannel = 1;
const int blueChannel = 2;
const int warmChannel = 3;
const int coldChannel = 4;

const int pwmFreq = 5000;
const int pwmResolution = 8; // 8-bit PWM = 0-255

// Output levels as written (after brightness), in board-label order R G B WW CW.
int currentRed = 0;
int currentGreen = 0;
int currentBlue = 0;
int currentWarm = 0;
int currentCold = 0;

int ledBrightness = 100;        // percent; set from the settings by lights.h
bool ledAnimations = true;      // status rhythms on/off (always on in v2; kept for tests)
bool ledCycleActive = false;    // colour cycle tool running

// ---- Status rhythms ----
// Errors blink, "needs attention" pulses, "finished" pulses slowly, everything else is steady,
// so a state can be read without telling red from green. 1 blink/s is well under the
// 3 flashes/s photosensitivity threshold.
enum LedEffect { EFFECT_SOLID, EFFECT_BLINK, EFFECT_PULSE, EFFECT_PULSE_SLOW };
LedEffect currentEffect = EFFECT_SOLID;
unsigned long effectStartms = 0;

void writeCurrentColor()
{
    ledcWrite(redChannel, currentRed);
    ledcWrite(greenChannel, currentGreen);
    ledcWrite(blueChannel, currentBlue);
    ledcWrite(warmChannel, currentWarm);
    ledcWrite(coldChannel, currentCold);
}

void setEffect(LedEffect e)
{
    if (e != currentEffect)
    {
        currentEffect = e;
        effectStartms = millis(); // restart the rhythm only when the status changes
        // The last animation frame may have left the PWM dimmed or dark; if the new status
        // has the same colour, tweenToColor() skipped writing it ("already at color").
        writeCurrentColor();
    }
}

float effectLevel(unsigned long now)
{
    if (!ledAnimations)
        return 1.0f;
    unsigned long t = now - effectStartms;
    switch (currentEffect)
    {
    case EFFECT_BLINK:
        return (t % 1000) < 600 ? 1.0f : 0.0f;
    case EFFECT_PULSE:
        return 0.25f + 0.75f * (0.5f + 0.5f * cosf(2.0f * (float)PI * (t % 1500) / 1500.0f));
    case EFFECT_PULSE_SLOW:
        return 0.35f + 0.65f * (0.5f + 0.5f * cosf(2.0f * (float)PI * (t % 4000) / 4000.0f));
    default:
        return 1.0f;
    }
}

// Last requested levels before brightness, so a brightness change can re-apply them.
int lastTargetRed = 0, lastTargetGreen = 0, lastTargetBlue = 0, lastTargetWarm = 0, lastTargetCold = 0;

// Fade the five outputs (board-label order, 0-255 before brightness) to a target.
// Leaves the rhythm alone: callers set it with setEffect().
void tweenToColor(int targetRed, int targetGreen, int targetBlue, int targetWarm, int targetCold, int duration = 500)
{
    StateLock lock;
    lastTargetRed = targetRed; lastTargetGreen = targetGreen; lastTargetBlue = targetBlue;
    lastTargetWarm = targetWarm; lastTargetCold = targetCold;

    float brightness = (float)constrain(ledBrightness, 0, 100) / 100.0;
    int brightenedRed = (int)(constrain(targetRed, 0, 255) * brightness);
    int brightenedGreen = (int)(constrain(targetGreen, 0, 255) * brightness);
    int brightenedBlue = (int)(constrain(targetBlue, 0, 255) * brightness);
    int brightenedWarm = (int)(constrain(targetWarm, 0, 255) * brightness);
    int brightenedCold = (int)(constrain(targetCold, 0, 255) * brightness);

    if (brightenedRed == currentRed && brightenedGreen == currentGreen && brightenedBlue == currentBlue && brightenedWarm == currentWarm && brightenedCold == currentCold)
        return; // already there

    // Interpolate from the start colour (2.3.0's per-step increment rounded to 0 and jumped).
    const int steps = duration > 0 ? 50 : 1;
    int stepDelay = duration / steps;
    int startRed = currentRed, startGreen = currentGreen, startBlue = currentBlue, startWarm = currentWarm, startCold = currentCold;
    for (int i = 1; i <= steps; i++)
    {
        currentRed = startRed + (brightenedRed - startRed) * i / steps;
        currentGreen = startGreen + (brightenedGreen - startGreen) * i / steps;
        currentBlue = startBlue + (brightenedBlue - startBlue) * i / steps;
        currentWarm = startWarm + (brightenedWarm - startWarm) * i / steps;
        currentCold = startCold + (brightenedCold - startCold) * i / steps;
        writeCurrentColor();
        delay(stepDelay);
    }
}

// Boot and connection colours (main.cpp, mqttmanager.h): steady, set directly.
void showStatusColour(int r, int g, int b, int ww, int cw)
{
    StateLock lock;
    setEffect(EFFECT_SOLID);
    tweenToColor(r, g, b, ww, cw);
}

// Live brightness change (setup page slider): re-apply the current colour at once.
void setBrightness(int percent)
{
    StateLock lock;
    ledBrightness = constrain(percent, 0, 100);
    tweenToColor(lastTargetRed, lastTargetGreen, lastTargetBlue, lastTargetWarm, lastTargetCold, 0); // leaves the rhythm running
}

// Called from the loop: scales the current colour by the rhythm's level.
void applyEffect()
{
    if (currentEffect == EFFECT_SOLID || ledCycleActive)
        return;
    float k = effectLevel(millis());
    ledcWrite(redChannel, (uint32_t)(currentRed * k));
    ledcWrite(greenChannel, (uint32_t)(currentGreen * k));
    ledcWrite(blueChannel, (uint32_t)(currentBlue * k));
    ledcWrite(warmChannel, (uint32_t)(currentWarm * k));
    ledcWrite(coldChannel, (uint32_t)(currentCold * k));
}

// Colour cycle tool. `outputs` maps red/green/blue to the outputs the wiring check found.
float hue = 0.0;
void RGBCycle(const int8_t outputs[3])
{
    if (!ledCycleActive)
        return;
    hue += 0.1;
    if (hue > 360.0)
        hue = 0.0;
    // cos() is -1..1: clip the negative half so colours stay saturated; apply brightness.
    float brightness = (float)constrain(ledBrightness, 0, 100) / 100.0;
    int lv[3] = {(int)(max(0.0, cos(hue * PI / 180.0)) * 255 * brightness),
                 (int)(max(0.0, cos((hue + 120.0) * PI / 180.0)) * 255 * brightness),
                 (int)(max(0.0, cos((hue + 240.0) * PI / 180.0)) * 255 * brightness)};
    int out[5] = {0, 0, 0, 0, 0};
    for (int i = 0; i < 3; i++)
        if (outputs[i] >= 0 && outputs[i] < 5) out[outputs[i]] = lv[i];
    currentRed = out[0]; currentGreen = out[1]; currentBlue = out[2]; currentWarm = out[3]; currentCold = out[4];
    writeCurrentColor();
}

void setupLeds()
{
    ledcSetup(redChannel, pwmFreq, pwmResolution);
    ledcSetup(greenChannel, pwmFreq, pwmResolution);
    ledcSetup(blueChannel, pwmFreq, pwmResolution);
    ledcSetup(warmChannel, pwmFreq, pwmResolution);
    ledcSetup(coldChannel, pwmFreq, pwmResolution);

    ledcAttachPin(redPin, redChannel);
    ledcAttachPin(greenPin, greenChannel);
    ledcAttachPin(bluePin, blueChannel);
    ledcAttachPin(warmPin, warmChannel);
    ledcAttachPin(coldPin, coldChannel);
}

#endif
