#ifndef _MQTTMANAGER
#define _MQTTMANAGER

#define TASK_RAM 20480 // 20kB for MQTT Task Stack Size

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

#include "mqttparsingutility.h"
#include "AutoGrowBufferStream.h"
#include "types.h"
#include "lights.h"
#include "printerCert.h"

// WiFiClientSecure with two guards (stage 6 review):
// - stop() leaves the socket at -1. The library zeroes its whole context on stop, so the socket
//   number reads 0 afterwards, and a second stop() (or the destructor) then closes file
//   descriptor 0, which may by then be an open config file (core 2.0.17, WiFiClientSecure.cpp).
// - connect() only when allowed. With the certificate check on, PubSubClient must reuse the
//   checked connection; if that has dropped, it would otherwise open its own, unchecked, and
//   send the access code to whatever answers.
class GuardedTlsClient : public WiFiClientSecure
{
public:
    bool allowConnect = true;
    using WiFiClientSecure::connect;
    int connect(IPAddress ip, uint16_t port) override { return allowConnect ? WiFiClientSecure::connect(ip, port) : 0; }
    int connect(const char *host, uint16_t port) override { return allowConnect ? WiFiClientSecure::connect(host, port) : 0; }
    void stop() override
    {
        WiFiClientSecure::stop();
        sslclient->socket = -1; // every library path that stops the client comes through here
    }
};

GuardedTlsClient wifiSecureClient;
PubSubClient mqttClient(wifiSecureClient);

String device_topic;
String report_topic;
String clientId = "BLLED-";

AutoGrowBufferStream stream;

#define MQTT_RETRY_MIN_MS 3000UL
#define MQTT_RETRY_MAX_MS 30000UL
unsigned long mqttattempt = 0;
unsigned long mqttRetryDelay = 0; // first attempt immediately
unsigned long lastMQTTupdate = millis();

TaskHandle_t mqttTaskHandle = NULL;
bool mqttTaskRunning = false;
volatile bool mqttConnectInProgress = false;

// Chamber light requests can come from any task (loop, web server, MQTT callback), but
// PubSubClient/WiFiClientSecure aren't thread-safe, so only the MQTT task publishes.
volatile int8_t pendingChamberLight = -1; // -1 none, 0 off, 1 on
void publishChamberLight(bool on);

// With a Default BLLED
// Expected information when viewing MQTT status messages

// gcode_state	stg_cur	    BLLED LED control	    Comments
//------------------------------------------------------------------------
// IDLE	        -1	        White	                Printer just powered on
// RUNNING	    -1	        White	                Printer sent print file
// RUNNING	     2	        White	                PREHEATING BED
// RUNNING	    14	        OFF (for Lidar)	        CLEANING NOZZLE
// RUNNING	     1	        OFF (for Lidar)	        BED LEVELING
// RUNNING	     8	        OFF (for Lidar)	        CALIBRATING EXTRUSION
// RUNNING	     0	        White	                All the printing happens here
// FINISH	    -1	        Green	                After bed is lowered and filament retracted
// FINISH	    -1	        Green	                BLLED logic waits for a door interaction
// FINISH	    -1	        White	                After door interaction
// FINISH	    -1	        OFF                     Inactivity after 30mins

/* void connectMqtt()
{
    if (WiFi.status() != WL_CONNECTED  || WiFi.getMode() != WIFI_MODE_STA)
    {
        // Abort MQTT connection attempt when no Wifi
        return;
    }
    if (strlen(printerConfig.printerIP) == 0 || strlen(printerConfig.accessCode) == 0)
    {
        LogSerial.println(F("[MQTT] Abort connect: printerIP oder accessCode wrong or empty"));
        return;
    }
    if (!mqttClient.connected() && (millis() - mqttattempt) >= 3000)
    {
        tweenToColor(10, 10, 10, 10, 10);
        LogSerial.println(F("Connecting to mqtt..."));
        if (mqttClient.connect(clientId.c_str(), "bblp", printerConfig.accessCode))
        {
            LogSerial.print(F("[MQTT] connected, subscribing to MQTT Topic:  "));
            LogSerial.println(report_topic);
            mqttClient.subscribe(report_topic.c_str());
            printerVariables.online = true;
            printerVariables.disconnectMQTTms = 0;
            // LogSerial.println(F("Updating LEDs from MQTT connect"));
            // updateleds();
        }
        else
        {
            LogSerial.println(F("Failed to connect with error code: "));
            LogSerial.print(mqttClient.state());
            LogSerial.print(F("  "));
            ParseMQTTState(mqttClient.state());
            if (mqttClient.state() == 5)
            {
                //delay(500);
                tweenToColor(127, 0, 0, 0, 0); // light red, indicating not authorized
                //delay(500);
                mqttattempt = (millis() - 3000);
                //                LogSerial.println(F("Restarting Device"));
                //                delay(1000);
                //                ESP.restart();
            }
        }
    }
} */

// After a failed attempt: back off (3 s, 6 s, 12 s ... 30 s) instead of retrying the TLS
// handshake and login continuously, e.g. after the access code changed on the printer.
void mqttBackOff()
{
    mqttRetryDelay = (mqttRetryDelay == 0) ? MQTT_RETRY_MIN_MS : min(mqttRetryDelay * 2, MQTT_RETRY_MAX_MS);
    LogSerial.printf("[MQTT] next attempt in %lu s\n", mqttRetryDelay / 1000); // also in Web Serial
}

// Before the access code is sent: is this really the printer? Its certificate must be signed by
// Bambu Lab and issued to the configured serial (printerCert.h). Leaves the TLS connection open
// for PubSubClient to use when it is.
bool connectToVerifiedPrinter()
{
    IPAddress ip;
    wifiSecureClient.stop(); // nothing left over from a dropped connection
    wifiSecureClient.allowConnect = true;
    const bool answered = ip.fromString(printerConfig.printerIP) && wifiSecureClient.connect(ip, 8883);
    wifiSecureClient.allowConnect = false; // PubSubClient may only use this, checked, connection
    if (!answered)
    {
        LogSerial.println(F("[MQTT] The printer didn't answer"));
        return false;
    }
    bblcert::Result r = bblcert::check(wifiSecureClient.getPeerCertificate());
    bool ok = bblcert::isPrinter(r, printerConfig.serialNumber);
    printerVariables.certRejected = !ok;
    if (!ok)
    {
        wifiSecureClient.stop();
        LogSerial.println(!r.trusted ? F("[MQTT] Access code not sent: the certificate at the printer's address isn't signed by Bambu Lab")
                                     : F("[MQTT] Access code not sent: the certificate at the printer's address belongs to a different printer"));
    }
    return ok;
}

// Setup's "Look up": connect to an address and read the name on its certificate. Runs on this
// task, which owns TLS; the web server only asks (api::lookup).
void runPrinterLookup()
{
    IPAddress ip;
    char addr[16];
    {
        StateLock lock;
        strlcpy(addr, printerLookup.ip, sizeof addr);
        printerLookup.state = 2;
    }
    GuardedTlsClient probe;
    probe.setInsecure(); // the certificate is checked below, against Bambu Lab's authority
    probe.setTimeout(5);
    probe.setHandshakeTimeout(10); // something that accepts TCP but never speaks TLS
    bool answered = ip.fromString(addr) && probe.connect(ip, 8883);
    bblcert::Result r;
    if (answered) r = bblcert::check(probe.getPeerCertificate());
    probe.stop();
    StateLock lock;
    if (strcmp(addr, printerLookup.ip) != 0) return; // asked about another address meanwhile
    printerLookup.answered = answered;
    printerLookup.trusted = r.trusted;
    strlcpy(printerLookup.serial, r.cn, sizeof(printerLookup.serial));
    printerLookup.doneMs = millis();
    printerLookup.state = 3;
    LogSerial.printf("[Lookup] %s: %s\n", addr, !answered ? "no answer" : r.trusted ? "a Bambu Lab printer" : "not a Bambu Lab certificate");
}

void connectMqtt()
{
    if (mqttConnectInProgress)
        return;

    mqttConnectInProgress = true;

    if (WiFi.status() != WL_CONNECTED || WiFi.getMode() != WIFI_MODE_STA)
    {
        mqttConnectInProgress = false;
        return;
    }

    if (strlen(printerConfig.printerIP) == 0 || strlen(printerConfig.accessCode) == 0)
    {
        Serial.println(F("[MQTT] Abort connect: printerIP oder accessCode wrong or empty"));
        mqttConnectInProgress = false;
        return;
    }

    if (!mqttClient.connected() && (millis() - mqttattempt) >= mqttRetryDelay)
    {
        // tweenToColor(10, 10, 10, 10, 10);
        mqttattempt = millis();
        Serial.println(F("Connecting to mqtt..."));

        wifiSecureClient.allowConnect = !settings.checkPrinterCert;
        if (settings.checkPrinterCert && !connectToVerifiedPrinter())
        {
            printerVariables.accessRejected = false;
            mqttBackOff();
            mqttConnectInProgress = false;
            return;
        }
        if (!settings.checkPrinterCert)
            printerVariables.certRejected = false;

        if (mqttClient.connect(clientId.c_str(), "bblp", printerConfig.accessCode))
        {
            Serial.print(F("[MQTT] connected, subscribing to MQTT Topic:  "));
            Serial.println(report_topic);
            mqttClient.subscribe(report_topic.c_str());
            printerVariables.online = true;
            printerVariables.everConnected = true;
            printerVariables.accessRejected = false;
            printerVariables.disconnectMQTTms = 0;
            mqttRetryDelay = MQTT_RETRY_MIN_MS;
        }
        else
        {
            Serial.println(F("Failed to connect with error code: "));
            Serial.print(mqttClient.state());
            Serial.print(F("  "));
            ParseMQTTState(mqttClient.state());

            printerVariables.accessRejected = mqttClient.state() == 5 || mqttClient.state() == 4; // dim red via decide()
            mqttBackOff();
        }
    }

    mqttConnectInProgress = false;
}

void mqttTask(void *parameter)
{
    mqttTaskRunning = true;

    while (true)
    {
        if (WiFi.status() != WL_CONNECTED || WiFi.getMode() != WIFI_MODE_STA)
        {
            printerVariables.online = false;
            pendingChamberLight = -1; // as before: no MQTT, request dropped
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        if (printerLookup.state == 1)
            runPrinterLookup();

        if (!mqttClient.connected())
        {
            printerVariables.online = false;
            if (pendingChamberLight != -1)
            {
                pendingChamberLight = -1;
                LogSerial.println(F("[MQTT] Skipped chamber_light control – MQTT not connected"));
            }

            if (printerVariables.disconnectMQTTms == 0)
            {
                printerVariables.disconnectMQTTms = millis();
                LogSerial.println(F("[MQTT Task] Disconnected"));
                ParseMQTTState(mqttClient.state());
            }

            connectMqtt();
            vTaskDelay(pdMS_TO_TICKS(32));
        }
        else
        {
            printerVariables.disconnectMQTTms = 0;
            mqttClient.loop();
            if (pendingChamberLight != -1 && mqttClient.connected())
            {
                // Atomic take, so a request queued between the read and the clear isn't lost.
                int8_t chamberLight = __atomic_exchange_n(&pendingChamberLight, (int8_t)-1, __ATOMIC_SEQ_CST);
                if (chamberLight != -1)
                    publishChamberLight(chamberLight == 1);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    mqttTaskRunning = false;
    vTaskDelete(NULL);
    LogSerial.printf("[MQTT Task] HighWaterMark: %d bytes\n", uxTaskGetStackHighWaterMark(NULL));
}

void ParseCallback(char *topic, byte *payload, unsigned int length)
{
    StateLock lock;
    JsonDocument messageobject;
    JsonDocument filter;
    // Rather than showing the entire message to Serial - grabbing only the pertinent bits for BLLED.
    // Device Status
    filter["print"]["command"] = true;
    filter["print"]["fail_reason"] = true;
    filter["print"]["gcode_state"] = true;
    filter["print"]["print_gcode_action"] = true;
    filter["print"]["print_real_action"] = true;
    filter["print"]["hms"] = true;
    filter["print"]["home_flag"] = true;
    filter["print"]["lights_report"] = true;
    filter["print"]["stg_cur"] = true;
    filter["print"]["print_error"] = true;
    filter["print"]["wifi_signal"] = true;
    filter["system"]["command"] = true;
    filter["system"]["led_mode"] = true;

    auto deserializeError = deserializeJson(messageobject, payload, length, DeserializationOption::Filter(filter));
    if (!deserializeError)
    {
        if (printerConfig.debuging)
        {
            LogSerial.println(F("Mqtt message received."));
            LogSerial.print(F("FreeHeap: "));
            LogSerial.println(ESP.getFreeHeap());
        }

        bool Changed = false;

        if (!messageobject["print"]["command"].isNull())
        {
            if (messageobject["print"]["command"] == "gcode_line"           // gcode_line used a lot during print initialisations - Skip these
                || messageobject["print"]["command"] == "project_prepare"   // 1 message per print
                || messageobject["print"]["command"] == "project_file"      // 1 message per print
                || messageobject["print"]["command"] == "clean_print_error" // During error (no info)
                || messageobject["print"]["command"] == "resume"            // After error or pause
                || messageobject["print"]["command"] == "get_accessories"   // After error or pause
                || messageobject["print"]["command"] == "prepare" || messageobject["print"]["command"] == "extrusion_cali_get")
            { // 1 message per print
                return;
            }
        }
        if (messageobject.size() == 0)
        {
            // Null or Filtered message that are not 'Print' or 'System' payload - Ignore these
            return;
        }

        // Output Filtered MQTT message
        if (printerConfig.mqttdebug)
        {
            LogSerial.print(F("(Filtered) MQTT payload, ["));
            LogSerial.print(stream.current_length());
            LogSerial.print(F("], "));
            serializeJson(messageobject, LogSerial);
            LogSerial.println();
        }

        // Door (bit 23 of home_flag). The door wakes the lights and acknowledges a finished
        // print; it never changes what the lights do (docs/REDESIGN.md). See lights.h.
        if (!messageobject["print"]["home_flag"].isNull())
        {
            long homeFlag = messageobject["print"]["home_flag"];
            bool doorState = bitRead(homeFlag, 23);
            if (printerVariables.doorOpen != doorState)
            {
                printerVariables.doorOpen = doorState;
                if (doorState)
                    printerVariables.lastdoorOpenms = millis();
                else
                    printerVariables.lastdoorClosems = millis();
                if (printerConfig.debugingchange)
                    LogSerial.println(doorState ? F("[MQTT] Door opened") : F("[MQTT] Door closed"));
                lightsOnDoor(doorState);
                Changed = true;
            }
        }

        // Check BBLP Stage
        if (!messageobject["print"]["stg_cur"].isNull())
        {
            if (printerVariables.stage != messageobject["print"]["stg_cur"].as<int>())
            {
                printerVariables.stage = messageobject["print"]["stg_cur"];

                if (printerConfig.debugingchange || printerConfig.debuging)
                {
                    LogSerial.print(F("[MQTT] update - stg_cur now: "));
                    LogSerial.println(printerVariables.stage);
                }
                Changed = true;
            }
        }

        // Check BBLP GCode State
        if (!messageobject["print"]["gcode_state"].isNull() && ((millis() - lastMQTTupdate) > 3000))
        {
            String mqttgcodeState = messageobject["print"]["gcode_state"].as<String>();

            // Onchange of gcodeState...
            if (printerVariables.gcodeState != mqttgcodeState)
            {
                if (mqttgcodeState == "RUNNING")
                {
                    printerVariables.overridestage = 999; // Reset after special HMS override
                }

                lightsOnGcodeState(printerVariables.gcodeState, mqttgcodeState); // finish colour, light at print start
                printerVariables.gcodeState = mqttgcodeState;

                if (printerConfig.debugingchange || printerConfig.debuging)
                {
                    LogSerial.print(F("[MQTT] update - gcode_state now: "));
                    LogSerial.println(printerVariables.gcodeState);
                }
                Changed = true;
            }
        }

        // Pause Command - quicker, but Only for user generated pause - error & code pauses don't trigger this.
        if (!messageobject["print"]["command"].isNull())
        {
            if (messageobject["print"]["command"] == "pause")
            {
                lastMQTTupdate = millis();
                LogSerial.println(F("[MQTT] update - manual PAUSE"));
                printerVariables.gcodeState = "PAUSE";
                Changed = true;
            }
        }

        // Added a delay so the slower MQTT status message doesn't reverse the "system" commands
        if (!messageobject["print"]["lights_report"].isNull() && ((millis() - lastMQTTupdate) > 3000))
        {
            JsonArray lightsReport = messageobject["print"]["lights_report"];
            for (JsonObject light : lightsReport)
            {
                if (light["node"] == "chamber_light")
                {

                    if (printerVariables.printerledstate != (light["mode"] == "on"))
                    {
                        printerVariables.printerledstate = (light["mode"] == "on");
                        if (printerConfig.debugingchange || printerConfig.debuging)
                        {
                            LogSerial.print(F("[MQTT] chamber_light now: "));
                            LogSerial.println(printerVariables.printerledstate);
                        }
                        Changed = true;
                    }
                }
            }
        }
        // System Commands are sent quicker than the push_status
        // Message only sent onChange
        if (!messageobject["system"]["command"].isNull())
        {
            if (messageobject["system"]["command"] == "ledctrl")
            {
                // Ignore Printer sending attempts to turn light on when already on.
                if (printerVariables.printerledstate != (messageobject["system"]["led_mode"] == "on"))
                {
                    printerVariables.printerledstate = (messageobject["system"]["led_mode"] == "on");
                    lastMQTTupdate = millis();
                    if (printerConfig.debugingchange || printerConfig.debuging)
                    {
                        LogSerial.print(F("[MQTT] led_mode now: "));
                        LogSerial.println(printerVariables.printerledstate);
                    }
                    Changed = true;
                }
            }
        }

        // Bambu Health Management System (HMS)
        if (!messageobject["print"]["hms"].isNull())
        {
            String oldHMSlevel = "";
            oldHMSlevel = printerVariables.parsedHMSlevel;

            printerVariables.hmsstate = false;
            printerVariables.parsedHMSlevel = "";
            /*             for (const auto &hms : messageobject["print"]["hms"].as<JsonArray>())
                        {
                            if (ParseHMSSeverity(hms["code"]) != "")
                            {
                                printerVariables.hmsstate = true;
                                printerVariables.parsedHMSlevel = ParseHMSSeverity(hms["code"]);
                                printerVariables.parsedHMScode = ((uint64_t)hms["attr"] << 32) + (uint64_t)hms["code"];
                            }
                        } */
            for (const auto &hms : messageobject["print"]["hms"].as<JsonArray>())
            {
                uint64_t code = ((uint64_t)hms["attr"] << 32) + (uint64_t)hms["code"];

                int chunk1 = (code >> 48);
                int chunk2 = (code >> 32) & 0xFFFF;
                int chunk3 = (code >> 16) & 0xFFFF;
                int chunk4 = code & 0xFFFF;

                char strHMScode[32];
                sprintf(strHMScode, "HMS_%04X_%04X_%04X_%04X", chunk1, chunk2, chunk3, chunk4);

                // Normalize input string (replace '-' with '_', remove whitespace)
                String normalizedIgnoreList = printerConfig.hmsIgnoreList;
                normalizedIgnoreList.replace("-", "_");
                normalizedIgnoreList.replace(" ", "");
                normalizedIgnoreList.replace("\r", "");
                normalizedIgnoreList.replace("\n", ",");

                // Check if code is in ignore list
                if (("," + normalizedIgnoreList + ",").indexOf("," + String(strHMScode) + ",") >= 0)
                {
                    LogSerial.print(F("[MQTT] Ignored HMS Code: "));
                    LogSerial.println(strHMScode);
                    continue;
                }

                // Ignore if present in ignore list
                if (printerConfig.hmsIgnoreList.indexOf(strHMScode) >= 0)
                {
                    LogSerial.print(F("[MQTT] HMS code ignored: "));
                    LogSerial.println(strHMScode);
                    continue;
                }

                if (ParseHMSSeverity(hms["code"]) != "")
                {
                    printerVariables.hmsstate = true;
                    printerVariables.parsedHMSlevel = ParseHMSSeverity(hms["code"]);
                    printerVariables.parsedHMScode = code;
                }
            }

            if (oldHMSlevel != printerVariables.parsedHMSlevel)
            {
                // example, cap fall off
                // Error Code: 0300_1200_0002_0001 **
                //"hms":[{"attr":50336256,"code":131073}]
                if (printerVariables.parsedHMScode == 0x0C0003000003000B)
                    printerVariables.overridestage = 10;
                if (printerVariables.parsedHMScode == 0x0300120000020001)
                    printerVariables.overridestage = 17;
                if (printerVariables.parsedHMScode == 0x0700200000030001)
                    printerVariables.overridestage = 6;
                if (printerVariables.parsedHMScode == 0x0300020000010001)
                    printerVariables.overridestage = 20;
                if (printerVariables.parsedHMScode == 0x0300010000010007)
                    printerVariables.overridestage = 21;

                if (printerConfig.debuging || printerConfig.debugingchange)
                {
                    LogSerial.print(F("[MQTT] update - parsedHMSlevel now: "));
                    if (printerVariables.parsedHMSlevel.length() > 0)
                    {
                        LogSerial.print(printerVariables.parsedHMSlevel);
                        LogSerial.print(F("      Error Code: HMS_"));
                        // LogSerial.println(F("https://wiki.bambulab.com/en/x1/troubleshooting/how-to-enter-the-specific-code-page"));
                        int chunk1 = (printerVariables.parsedHMScode >> 48);
                        int chunk2 = (printerVariables.parsedHMScode >> 32) & 0xFFFF;
                        int chunk3 = (printerVariables.parsedHMScode >> 16) & 0xFFFF;
                        int chunk4 = printerVariables.parsedHMScode & 0xFFFF;
                        char strHMScode[20];
                        sprintf(strHMScode, "%04X_%04X_%04X_%04X", chunk1, chunk2, chunk3, chunk4);
                        LogSerial.print(strHMScode);
                        if (printerVariables.overridestage != printerVariables.stage)
                        {
                            LogSerial.println(F(" **"));
                        }
                        else
                        {
                            LogSerial.println(F(""));
                        }
                    }
                    else
                    {
                        LogSerial.println(F("NULL"));
                    }
                }
                if (printerVariables.parsedHMSlevel.length() == 0)
                {
                    // HMS cleared. (This reset used to sit inside the debug-logging branch,
                    // so with debug logging off an HMS colour stayed until the next print.)
                    printerVariables.overridestage = 999;
                }
                Changed = true;
            }
        }

        // Any change from the printer counts as activity (idle timer); then let the strip
        // reflect the new state.
        if (Changed)
            lightsActivity();
        // The loop shows the change on its next pass (lightsLoop). Fading here would hold the
        // lock on this task for 500 ms.
    }
    else
    {
        LogSerial.println(F("Deserialize error while parsing mqtt"));
        return;
    }
}

void mqttCallback(char *topic, byte *payload, unsigned int length)
{
    ParseCallback(topic, (byte *)stream.get_buffer(), stream.current_length());
    stream.flush();
}

// Switch the printer's own light. Only called when one of the "printer's own light" settings
// asks for it (lights.h).
void controlChamberLight(bool on)
{
    if (printerConfig.debugingchange)
        LogSerial.printf("[LED] Printer's light %s\n", on ? "on" : "off");
    printerVariables.printerledstate = on; // so following the light doesn't flicker before the report
    pendingChamberLight = on ? 1 : 0;
}

// MQTT task only.
void publishChamberLight(bool on)
{
    JsonDocument doc;
    JsonObject system = doc["system"].to<JsonObject>();
    system["command"] = "ledctrl";
    system["sequence_id"] = "blled_auto";
    system["led_node"] = "chamber_light";
    system["led_mode"] = on ? "on" : "off";
    system["led_on_time"] = 500;
    system["led_off_time"] = 500;
    system["loop_times"] = 0;
    system["interval_time"] = 1000;

    String payload;
    serializeJson(doc, payload);

    String topic = String("device/") + printerConfig.serialNumber + "/request";
    mqttClient.publish(topic.c_str(), payload.c_str());

    LogSerial.print(F("[MQTT] Chamber Light "));
    LogSerial.print(on ? F("ON") : F("OFF"));
    LogSerial.print(F(" sent to topic: "));
    LogSerial.println(topic);
}

void setupMqtt()
{
    clientId += String(random(0xffff), HEX);
    LogSerial.print(F("Setting up MQTT with Bambu Lab Printer IP address: "));
    LogSerial.println(printerConfig.printerIP);

    device_topic = String("device/") + printerConfig.serialNumber;
    report_topic = device_topic + String("/report");

    wifiSecureClient.setInsecure();
    wifiSecureClient.setTimeout(15);
    wifiSecureClient.setHandshakeTimeout(10); // keeps a stuck attempt from holding up setup's Look up
    mqttClient.setSocketTimeout(17);
    mqttClient.setBufferSize(1024);
    mqttClient.setServer(printerConfig.printerIP, 8883);
    mqttClient.setStream(stream);
    mqttClient.setCallback(mqttCallback);

    LogSerial.println(F("Finished setting up MQTT"));

    if (mqttTaskHandle == NULL)
    {
        BaseType_t result;

#if CONFIG_FREERTOS_UNICORE
        result = xTaskCreate(
            mqttTask,
            "mqttTask",
            TASK_RAM,
            NULL,
            1,
            &mqttTaskHandle);
#else
        result = xTaskCreatePinnedToCore(
            mqttTask,
            "mqttTask",
            TASK_RAM,
            NULL,
            1,
            &mqttTaskHandle,
            1 // Core 1 (App Core)
        );
#endif

        if (result == pdPASS)
        {
            LogSerial.println(F("[MQTT] task successfully started"));
        }
        else
        {
            LogSerial.println(F("Failed to create MQTT task!"));
        }
    }
}

void mqttloop()
{
    if (WiFi.status() != WL_CONNECTED || WiFi.getMode() != WIFI_MODE_STA)
    {
        // Abort MQTT connection attempt when no Wifi
        return;
    }
    if (!mqttClient.connected())
    {
        printerVariables.online = false;
        // Only sent the timer from the first instance of a MQTT disconnect
        if (printerVariables.disconnectMQTTms == 0)
        {
            printerVariables.disconnectMQTTms = millis();
            // Record last time MQTT dropped connection
            LogSerial.println(F("[MQTT] dropped during mqttloop"));
            ParseMQTTState(mqttClient.state());
        }
        // delay(500);
        connectMqtt();
        delay(32);
        return;
    }
    else
    {
        printerVariables.disconnectMQTTms = 0;
    }
    mqttClient.loop();
    delay(10);
}

#endif
