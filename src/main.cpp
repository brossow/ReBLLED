#include <Arduino.h>
bool shouldRestart = false;
unsigned long restartRequestTime = 0;
#include "./blled/logSerial.h"
#include "./blled/types.h"
#include "./blled/lights.h"
#include "./blled/filesystem.h"
#include "./blled/bblPrinterDiscovery.h"
#include "./blled/web-server.h"
#include "./blled/mqttmanager.h"
#include "./blled/serialmanager.h"
#include "./blled/wifi-manager.h"
#include "./blled/ssdp.h"


int wifi_reconnect_count = 0;
unsigned long lastWifiReconnectAttempt = 0;

// Set when saved WiFi credentials exist but the network wasn't reachable at boot (e.g. the
// router was still starting after a power cut). The AP stays up for setup, and the saved
// network is tried for a short window once a minute; once it connects we restart into
// normal mode. Outside those windows the STA is idle, so the setup page's WiFi scan works
// (the ESP32 can't scan while a connection attempt is running).
bool apFallbackRetry = false;
bool apFallbackAttemptActive = false;
unsigned long lastApFallbackAttempt = 0;
const unsigned long apFallbackInterval = 60000;
const unsigned long apFallbackAttemptWindow = 15000;

void startApFallbackRetry()
{
    // The core's auto-reconnect would otherwise retry back-to-back forever.
    WiFi.setAutoReconnect(false);
    apFallbackRetry = true;
    lastApFallbackAttempt = millis();
}

void apFallbackLoop()
{
    if (!apFallbackRetry)
        return;
    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.println(F("[WiFiManager] Saved network is back - restarting into normal mode"));
        delay(500);
        ESP.restart();
    }
    if (apFallbackAttemptActive && millis() - lastApFallbackAttempt > apFallbackAttemptWindow)
    {
        apFallbackAttemptActive = false;
        WiFi.disconnect(); // STA only; the AP keeps running
    }
    if (!apFallbackAttemptActive && millis() - lastApFallbackAttempt > apFallbackInterval &&
        WiFi.scanComplete() != WIFI_SCAN_RUNNING)
    {
        lastApFallbackAttempt = millis();
        apFallbackAttemptActive = true;
        Serial.println(F("[WiFiManager] AP mode: trying saved network"));
        WiFi.mode(WIFI_AP_STA); // keep the setup AP running while trying
        WiFi.begin(globalVariables.SSID, globalVariables.APPW);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(100);
    Serial.println(F("Initializing"));
    Serial.println(ESP.getFreeHeap());
    Serial.println("");
    Serial.print(F("** Using firmware version: "));
    Serial.print(globalVariables.FWVersion);
    Serial.println(F(" **"));
    Serial.println("");
    setupLeds();
    showStatusColour(100, 100, 100, 100, 100); // all outputs on: starting
    Serial.println(F(""));

    lightsStatusColour(255, 0, 0); // red: loading settings
    setupFileSystem();
    loadFileSystem();
    lightsBegin();
    Serial.println(F(""));

    lightsStatusColour(255, 165, 0); // orange: joining Wi-Fi
    setupSerial();

    if (strlen(globalVariables.SSID) == 0 || strlen(globalVariables.APPW) == 0)
    {
        Serial.println(F("SSID or password is missing. Please configure both by going to: https://dutchdevelop.com/blled-configuration-setup/"));
        lightsStatusColour(255, 0, 255); // pink: no Wi-Fi configured, setup hotspot on
        startAPMode();
        setupWebserver();
        return;
    }
    else

        if (!connectToWifi())
    {
        Serial.println(F("[WiFiManager] Not connected → AP Mode"));
        startAPMode();
        setupWebserver();
        startApFallbackRetry();
        return;
    }
    else
    {
        Serial.println(F("[WiFiManager] connected. Starting webUI."));
        lightsStatusColour(0, 0, 255); // blue: Wi-Fi connected
        setupWebserver();
    }

    start_ssdp();

    lightsStatusColour(0, 255, 255); // cyan: connecting to the printer
    setupMqtt();
    // >>> Fix: prevent false offline after 30s
    printerVariables.disconnectMQTTms = millis();
    Serial.println();
    Serial.print(F("** BLLED Controller started "));
    Serial.print(F("using firmware version: "));
    Serial.print(globalVariables.FWVersion);
    Serial.println(F(" **"));
    Serial.println();
    globalVariables.started = true;
    Serial.println(F("Updating LEDs from Setup"));
    lightsUpdate(true);
}

void loop()
{
    serialLoop();
    if (WiFi.getMode() & WIFI_AP)
    {
        // Captive-portal DNS; must also run in setup (AP) mode, where 'started' is false.
        dnsServer.processNextRequest();
    }
    apFallbackLoop();
    if (globalVariables.started)
    {
        websocketLoop();
        lightsLoop();
        api::loop(); // autosave

        // Paced so a dropped connection doesn't hammer disconnect/reconnect every loop pass.
        if (WiFi.status() != WL_CONNECTED && millis() - lastWifiReconnectAttempt > 10000)
        {
            lastWifiReconnectAttempt = millis();
            LogSerial.print(F("Wifi connection dropped.  "));
            LogSerial.print(F("Wifi Status: "));
            LogSerial.println(wl_status_to_string(WiFi.status()));
            LogSerial.println(F("Attempting to reconnect to WiFi..."));
            wifi_reconnect_count += 1;
            if (wifi_reconnect_count <= 2)
            {
                WiFi.disconnect();
                delay(100);
                WiFi.reconnect();
            }
            else
            {
                // Not connecting after 2 simple disconnect / reconnects
                // Do something more drastic in case needing to switch to new AP
                scanNetwork();
                connectToWifi();
                wifi_reconnect_count = 0;
            }
        }
        if (WiFi.status() == WL_CONNECTED)
        {
            wifi_reconnect_count = 0;
        }
        if(WiFi.status() == WL_CONNECTED && !(WiFi.getMode() & WIFI_AP))
        {
            bblSearchPrinters();
        }
    }
    if (printerConfig.rescanWiFiNetwork)
    {
        LogSerial.println(F("Web submitted refresh of Wifi Scan (assigning Strongest AP)"));
        lightsStatusColour(255, 165, 0); // orange: scanning Wi-Fi
        scanNetwork();                       // Sets the MAC address for following connection attempt
        printerConfig.rescanWiFiNetwork = false;
        lightsUpdate(true);
    }
    if (shouldRestart && millis() - restartRequestTime > 1500)
    {
        api::flush(); // unsaved changes (no-op after a restore or factory reset)
        LogSerial.println(F("[WiFiSetup] Restarting now..."));
        ESP.restart();
    }
}
