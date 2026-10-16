#ifndef _BLLEDSTATELOCK
#define _BLLEDSTATELOCK

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// printerConfig / printerVariables and the LED outputs are used from three tasks: the
// Arduino loop, the MQTT task and the AsyncTCP (web server) task. The two Arduino-side
// tasks share core 1 at the same priority, so they preempt each other mid-update (e.g.
// while a String like gcodeState is being reassigned). Hold a StateLock while reading or
// changing that state.
//
// Recursive, so nested calls (updateleds -> tweenToColor, ParseCallback -> updateleds)
// are fine. Never wait on another task while holding it; MQTT publishes are handed to the
// MQTT task instead of being sent from here.
static SemaphoreHandle_t stateMutex = NULL;

class StateLock
{
public:
    StateLock()
    {
        // First use is in setup(), before the MQTT task and web server exist.
        if (stateMutex == NULL)
            stateMutex = xSemaphoreCreateRecursiveMutex();
        xSemaphoreTakeRecursive(stateMutex, portMAX_DELAY);
    }
    ~StateLock()
    {
        xSemaphoreGiveRecursive(stateMutex);
    }
    StateLock(const StateLock &) = delete;
    StateLock &operator=(const StateLock &) = delete;
};

#endif
