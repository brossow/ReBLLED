#ifndef _BLLEDFILESYSTEM
#define _BLLEDFILESYSTEM

#include <WiFi.h>
#include "FS.h"
#include <ArduinoJson.h>
#include <Arduino.h>
#include <LittleFS.h>
#include "types.h"
#include "lights.h"   // v2 settings

const char *configPath = "/blledconfig.json";
const char *configUploadPath = "/blledconfig.upload.json";
const char *configV1BackupPath = "/blledconfig.v1.json";   // the v1 file, kept when first migrated

// A restored backup must at least be a JSON object with the WiFi SSID in it; anything
// else is rejected before it can replace the working config.
bool isValidConfigFile(const char *path)
{
    File f = LittleFS.open(path, "r");
    if (!f)
        return false;
    JsonDocument json;
    DeserializationError err = deserializeJson(json, f);
    f.close();
    return !err && json.is<JsonObject>() && json["ssid"].is<const char *>();
}

char *generateRandomString(int length)
{
    static const char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    int charsetLength = strlen(charset);

    char *randomString = new char[length + 1];
    for (int i = 0; i < length; i++)
    {
        int randomIndex = random(0, charsetLength);
        randomString[i] = charset[randomIndex];
    }
    randomString[length] = '\0';

    return randomString;
}

void saveFileSystem()
{
    LogSerial.println(F("[Filesystem] Saving config"));

    JsonDocument json;
    json["ssid"] = globalVariables.SSID;
    json["appw"] = globalVariables.APPW;
    json["HTTPUser"] = securityVariables.HTTPUser;
    json["HTTPPass"] = securityVariables.HTTPPass;
    json["host"] = globalVariables.Host;
    json["printerIp"] = printerConfig.printerIP;
    json["accessCode"] = printerConfig.accessCode;
    json["serialNumber"] = printerConfig.serialNumber;
    // json["webpagePassword"] = printerConfig.webpagePassword;
    json["bssi"] = printerConfig.BSSID;
    // LED settings (v2, docs/REDESIGN.md). Connection keys above keep their v1 names.
    v2::toJson(settings, json["leds"].to<JsonObject>());

    File configFile = LittleFS.open(configPath, "w");
    if (!configFile)
    {
        LogSerial.println(F("[Filesystem] Failed to save config"));
        return;
    }
    serializeJson(json, configFile);
    configFile.close();
    LogSerial.println(F("[Filesystem] Config Saved"));
}

void loadFileSystem()
{
    LogSerial.println(F("[Filesystem] Loading config"));

    File configFile;
    int attempts = 0;
    while (attempts < 2)
    {
        configFile = LittleFS.open(configPath, "r");
        if (configFile)
        {
            break;
        }
        attempts++;
        LogSerial.println(F("[Filesystem] Failed to open config file, retrying.."));
        delay(2000);
    }
    if (!configFile)
    {
        LogSerial.print(F("[Filesystem] Failed to open config file after "));
        LogSerial.print(attempts);
        LogSerial.println(F(" retries"));

        LogSerial.println(F("[Filesystem] Clearing config"));
        saveFileSystem();
        return;
    }
    JsonDocument json;
    auto deserializeError = deserializeJson(json, configFile);

    if (!deserializeError)
    {
        // strlcpy + "| \"\"": a missing key or an over-long value must not crash or
        // overflow at boot (that would be a boot loop needing a USB reflash).
        strlcpy(globalVariables.SSID, json["ssid"] | "", sizeof(globalVariables.SSID));
        strlcpy(globalVariables.APPW, json["appw"] | "", sizeof(globalVariables.APPW));
        const char *hostValue = json["host"] | "BLLED";
        globalVariables.Host = strlen(hostValue) > 0 ? hostValue : "BLLED";

        strlcpy(securityVariables.HTTPUser, json["HTTPUser"] | "", sizeof(securityVariables.HTTPUser));
        strlcpy(securityVariables.HTTPPass, json["HTTPPass"] | "", sizeof(securityVariables.HTTPPass));
        strlcpy(printerConfig.printerIP, json["printerIp"] | "", sizeof(printerConfig.printerIP));
        strlcpy(printerConfig.accessCode, json["accessCode"] | "", sizeof(printerConfig.accessCode));
        strlcpy(printerConfig.serialNumber, json["serialNumber"] | "", sizeof(printerConfig.serialNumber));
        strlcpy(printerConfig.BSSID, json["bssi"] | "", sizeof(printerConfig.BSSID));
        // LED settings: v2 under "leds", or a v1 config to migrate (first boot after updating).
        bool migrated = false;
        JsonVariantConst leds = json["leds"];
        settings = leds.is<JsonObjectConst>() ? v2::load(leds, &migrated) : v2::load(json.as<JsonVariantConst>(), &migrated);
        if (migrated)
        {
            configFile.close();
            if (!LittleFS.exists(configV1BackupPath))
            {
                File src = LittleFS.open(configPath, "r"), dst = LittleFS.open(configV1BackupPath, "w");
                if (src && dst) { uint8_t buf[256]; size_t n; while ((n = src.read(buf, sizeof buf)) > 0) dst.write(buf, n); }
                if (src) src.close();
                if (dst) dst.close();
            }
            LogSerial.println(F("[Filesystem] Migrated LED settings from v1 (original kept as blledconfig.v1.json)"));
            saveFileSystem();
        }

        LogSerial.println(F("[Filesystem] Loaded config"));
    }
    else
    {
        LogSerial.println(F("[Filesystem] Failed loading config"));
        LogSerial.println(F("[Filesystem] Clearing config"));
        LittleFS.remove(configPath);

        // LogSerial.println(F("Generating new password"));
        // char* pw = generateRandomString(8);
        // strcpy(printerConfig.webpagePassword, pw);
    }

    configFile.close();
}

void deleteFileSystem()
{
    LogSerial.println(F("[Filesystem] Deleting LittleFS"));
    LittleFS.remove(configPath);
}

bool hasFileSystem()
{
    return LittleFS.exists(configPath);
}

void setupFileSystem()
{
    LogSerial.println(F("[Filesystem] Mounting LittleFS"));
    if (!LittleFS.begin())
    {
        LogSerial.println(F("[Filesystem] Failed to mount LittleFS"));
        LittleFS.format();
        LogSerial.println(F("[Filesystem] Formatting LittleFS"));
        LogSerial.println(F("[Filesystem] Restarting Device"));
        delay(1000);
        ESP.restart();
    }
    LogSerial.println(F("[Filesystem] Mounted LittleFS"));
};

#endif