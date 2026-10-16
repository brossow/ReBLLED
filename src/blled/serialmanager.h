#ifndef _BLLEDSERIAL_MANAGER
#define _BLLEDSERIAL_MANAGER

#include <Arduino.h>
#include <ArduinoJson.h> 

#include "filesystem.h"
#include "types.h"

void setupSerial(){
   while (!Serial);
}

void serialLoop(){
    if (Serial.available() > 0) {
        String input = Serial.readStringUntil('\n');
        JsonDocument doc;
        deserializeJson(doc, input);
        if (doc["ssid"].is<const char*>() && doc["pass"].is<const char*>()) {
            Serial.print(F("SSID "));
            Serial.println(doc["ssid"].as<String>());
            Serial.print(F("PASS "));
            Serial.println(doc["pass"].as<String>());

            Serial.println(doc["printerip"].as<String>());
            Serial.println(doc["printercode"].as<String>());
            Serial.println(doc["printerserial"].as<String>());

            // Bounded copies (strcpy overflowed on long values and crashed on missing keys).
            strlcpy(globalVariables.SSID, doc["ssid"] | "", sizeof(globalVariables.SSID));
            strlcpy(globalVariables.APPW, doc["pass"] | "", sizeof(globalVariables.APPW));

            strlcpy(printerConfig.printerIP, doc["printerip"] | "", sizeof(printerConfig.printerIP));
            strlcpy(printerConfig.accessCode, doc["printercode"] | "", sizeof(printerConfig.accessCode));
            strlcpy(printerConfig.serialNumber, doc["printerserial"] | "", sizeof(printerConfig.serialNumber));

            saveFileSystem();
            Serial.println(F("Restarting Device"));
            delay(1000);
            ESP.restart();
       }
      
    };
    delay(10);
}

#endif