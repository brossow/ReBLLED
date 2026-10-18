#ifndef _BLLEDWEB_SERVER
#define _BLLEDWEB_SERVER

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include "lights.h"
#include "filesystem.h"
#include "api.h"

AsyncWebServer webServer(80);
AsyncWebSocket ws("/ws");

#include "../www/www.h"

unsigned long lastWsPush = 0;
volatile bool configUploadOk = false;
volatile bool otaOk = false;
const unsigned long wsPushInterval = 1000; // alle 1000ms

bool isAuthorized(AsyncWebServerRequest *request)
{
    if (strlen(securityVariables.HTTPUser) == 0 || strlen(securityVariables.HTTPPass) == 0)
    {
        return true;
    }
    return request->authenticate(securityVariables.HTTPUser, securityVariables.HTTPPass);
}

void handleSetup(AsyncWebServerRequest *request)
{
    if (!isAuthorized(request))
    {
        return request->requestAuthentication();
    }
    AsyncWebServerResponse *response = request->beginResponse(200, setupPage_html_gz_mime, setupPage_html_gz, setupPage_html_gz_len);
    response->addHeader("Content-Encoding", "gzip");
    request->send(response);
}

void handleUpdatePage(AsyncWebServerRequest *request)
{
    if (!isAuthorized(request))
    {
        return request->requestAuthentication();
    }
    AsyncWebServerResponse *response = request->beginResponse(200, updatePage_html_gz_mime, updatePage_html_gz, updatePage_html_gz_len);
    response->addHeader("Content-Encoding", "gzip");
    request->send(response);
}

void handleGetfavicon(AsyncWebServerRequest *request)
{
    if (!isAuthorized(request))
    {
        return request->requestAuthentication();
    }
    AsyncWebServerResponse *response = request->beginResponse(200, favicon_png_gz_mime, favicon_png_gz, favicon_png_gz_len);
    response->addHeader("Content-Encoding", "gzip");
    request->send(response);
}

void handlePrinterConfigJson(AsyncWebServerRequest *request)
{
    if (!isAuthorized(request))
    {
        return request->requestAuthentication();
    }
    // Secrets (WiFi password, access code, WebUI password) are never sent back to the
    // browser - only whether one is stored. Blank fields on submit keep the stored value.
    JsonDocument doc;
    doc["ssid"] = globalVariables.SSID;
    doc["hasPass"] = strlen(globalVariables.APPW) > 0;
    doc["host"] = globalVariables.Host;
    doc["printerIP"] = printerConfig.printerIP;
    doc["printerSerial"] = printerConfig.serialNumber;
    doc["hasAccessCode"] = strlen(printerConfig.accessCode) > 0;
    doc["webUser"] = securityVariables.HTTPUser;
    doc["hasWebPass"] = strlen(securityVariables.HTTPPass) > 0;
    doc["isAPMode"] = (WiFi.getMode() & WIFI_AP);

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
}

// The stylesheet every page shares (src/www/ui.css).
void handleUiCss(AsyncWebServerRequest *request)
{
    if (!isAuthorized(request))
    {
        return request->requestAuthentication();
    }
    AsyncWebServerResponse *response = request->beginResponse(200, ui_css_gz_mime, ui_css_gz, ui_css_gz_len);
    response->addHeader("Content-Encoding", "gzip");
    request->send(response);
}

// ---- v2 JSON API (api.h) ----
void sendJson(AsyncWebServerRequest *request, int code, JsonDocument &doc)
{
    String json;
    serializeJson(doc, json);
    request->send(code, "application/json", json);
}
void handleApiGetSettings(AsyncWebServerRequest *request)
{
    if (!isAuthorized(request)) return request->requestAuthentication();
    JsonDocument doc;
    api::getSettings(doc.to<JsonObject>());
    sendJson(request, 200, doc);
}
void handleApiStatus(AsyncWebServerRequest *request)
{
    if (!isAuthorized(request)) return request->requestAuthentication();
    JsonDocument doc;
    api::status(doc.to<JsonObject>());
    sendJson(request, 200, doc);
}
// Every printer heard announcing itself (up to 10), for choosing which one to follow.
void handleApiPrinters(AsyncWebServerRequest *request)
{
    if (!isAuthorized(request)) return request->requestAuthentication();
    JsonDocument doc;
    JsonArray arr = doc["printers"].to<JsonArray>();
    StateLock lock; // discovery updates the list
    for (int i = 0; i < bblKnownPrinterCount; i++)
    {
        JsonObject o = arr.add<JsonObject>();
        o["ip"] = bblLastKnownPrinters[i].ip.toString();
        o["serial"] = bblLastKnownPrinters[i].usn;
        if (bblLastKnownPrinters[i].model[0]) o["model"] = bblLastKnownPrinters[i].model;
        if (bblLastKnownPrinters[i].name[0]) o["name"] = bblLastKnownPrinters[i].name;
        o["current"] = strcmp(bblLastKnownPrinters[i].usn, printerConfig.serialNumber) == 0;
    }
    doc["followingSerial"] = printerConfig.serialNumber;
    doc["followingIp"] = printerConfig.printerIP;
    sendJson(request, 200, doc);
}
// POST handlers with a JSON body. The body is parsed before this runs, but nothing acts on
// it without the login.
typedef bool (*ApiBodyFn)(JsonVariantConst, JsonObject);
void apiPost(AsyncWebServerRequest *request, JsonVariant &json, ApiBodyFn fn)
{
    if (!isAuthorized(request)) return request->requestAuthentication();
    JsonDocument doc;
    bool ok = fn(json, doc.to<JsonObject>());
    sendJson(request, ok ? 200 : 400, doc);
}
bool apiPatch(JsonVariantConst body, JsonObject out)
{
    if (!body.is<JsonObjectConst>()) { out["error"] = "expected a JSON object"; return false; }
    api::patchSettings(body, out);
    return true;
}
void setupApiRoutes()
{
    webServer.on("/api/settings", HTTP_GET, handleApiGetSettings);
    webServer.on("/api/status", HTTP_GET, handleApiStatus);
    webServer.on("/api/printers", HTTP_GET, handleApiPrinters);
    webServer.on("/api/lookup", HTTP_GET, [](AsyncWebServerRequest *r) {
        if (!isAuthorized(r)) return r->requestAuthentication();
        JsonDocument doc;
        int code = api::lookup(r->hasParam("ip") ? r->getParam("ip")->value() : String(), doc.to<JsonObject>());
        sendJson(r, code, doc);
    });
    webServer.on("/api/settings", HTTP_POST, [](AsyncWebServerRequest *r, JsonVariant &j) { apiPost(r, j, apiPatch); })
        .setMaxContentLength(4096);
    webServer.on("/api/tool", HTTP_POST, [](AsyncWebServerRequest *r, JsonVariant &j) { apiPost(r, j, api::tool); })
        .setMaxContentLength(512);
    // Which printer to follow (setup step 1). Restarts to connect when it changed.
    webServer.on("/api/printer", HTTP_POST, [](AsyncWebServerRequest *r, JsonVariant &j) {
        if (!isAuthorized(r)) return r->requestAuthentication();
        JsonDocument doc;
        bool changed = false;
        bool ok = api::setPrinter(j, doc.to<JsonObject>(), changed);
        if (ok && changed)
        {
            api::flush();
            saveFileSystem();
            shouldRestart = true;
            restartRequestTime = millis();
        }
        sendJson(r, ok ? 200 : 400, doc);
    }).setMaxContentLength(256);
    // Re-pick the strongest access point of the configured network (Advanced).
    webServer.on("/api/wifi-rescan", HTTP_POST, [](AsyncWebServerRequest *r) {
        if (!isAuthorized(r)) return r->requestAuthentication();
        printerConfig.rescanWiFiNetwork = true; // the loop scans, then reconnects
        r->send(200, "application/json", "{}");
    });
    webServer.on("/api/check", HTTP_POST, [](AsyncWebServerRequest *r, JsonVariant &j) { apiPost(r, j, api::check); })
        .setMaxContentLength(256);
}

void sendJsonToAll(JsonDocument &doc)
{
    String jsonString;
    serializeJson(doc, jsonString);
    ws.textAll(jsonString);
}

void handleWiFiScan(AsyncWebServerRequest *request)
{
    if (!isAuthorized(request))
    {
        return request->requestAuthentication();
    }
    // Asynchronous scan: a blocking WiFi.scanNetworks() here stalls the AsyncTCP task
    // for several seconds. The page polls until results are ready.
    int16_t n = WiFi.scanComplete();
    if (n == WIFI_SCAN_FAILED)
    {
        // Returns WIFI_SCAN_FAILED if the radio is busy (e.g. a WiFi connection attempt in
        // progress); tell the page to try again rather than claiming a scan is running.
        if (WiFi.scanNetworks(true) == WIFI_SCAN_FAILED)
        {
            request->send(503, "application/json", "{\"retry\":true}");
            return;
        }
        n = WIFI_SCAN_RUNNING;
    }
    if (n == WIFI_SCAN_RUNNING)
    {
        request->send(202, "application/json", "{\"scanning\":true}");
        return;
    }

    JsonDocument doc;
    JsonArray networks = doc["networks"].to<JsonArray>();
    for (int i = 0; i < n; ++i)
    {
        JsonObject net = networks.add<JsonObject>();
        net["ssid"] = WiFi.SSID(i);
        net["bssid"] = WiFi.BSSIDstr(i);
        net["rssi"] = WiFi.RSSI(i);
        net["enc"] = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? false : true;
    }
    WiFi.scanDelete(); // next request starts a fresh scan

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
}

void handleWiFiSetupPage(AsyncWebServerRequest *request)
{
    if (!isAuthorized(request))
    {
        return request->requestAuthentication();
    }
    AsyncWebServerResponse *response = request->beginResponse(200, wifiSetup_html_gz_mime, wifiSetup_html_gz, wifiSetup_html_gz_len);
    response->addHeader("Content-Encoding", "gzip");
    request->send(response);
}

void handleSubmitWiFi(AsyncWebServerRequest *request)
{
    if (!isAuthorized(request))
    {
        return request->requestAuthentication();
    }

    auto param = [request](const char *name) -> String
    {
        String value = request->hasParam(name, true) ? request->getParam(name, true)->value() : "";
        value.trim();
        return value;
    };

    // Blank fields keep the stored value, because /config.json no longer sends secrets
    // back to the page.
    String ssid = param("ssid");
    String pass = param("pass");
    String bssid = param("bssid");
    // open=1: the page says the chosen network has no password.
    const bool openNetwork = param("open") == "1";
    if (ssid.length() > 0 && ssid != globalVariables.SSID && pass.length() == 0 && !openNetwork)
    {
        // The stored password belongs to the old network; don't silently reuse it.
        request->send(400, "text/plain", "Enter the WiFi password for the new network.");
        return;
    }
    if (ssid.length() > 0)
    {
        LogSerial.print(F("[WiFiSetup] SSID: "));
        LogSerial.println(ssid);
        strlcpy(globalVariables.SSID, ssid.c_str(), sizeof(globalVariables.SSID));
    }
    if (openNetwork && pass.length() == 0)
        globalVariables.APPW[0] = '\0';
    else if (pass.length() > 0)
    {
        LogSerial.println(F("[WiFiSetup] WiFi password updated"));
        strlcpy(globalVariables.APPW, pass.c_str(), sizeof(globalVariables.APPW));
    }
    if (bssid.length() > 0)
        strlcpy(printerConfig.BSSID, bssid.c_str(), sizeof(printerConfig.BSSID));

    String host = param("host");
    if (host.length() > 0)
        globalVariables.Host = host;

    String printerIP = param("printerIP");
    String printerSerial = param("printerSerial");
    String accessCode = param("accessCode");
    if (printerIP.length() > 0)
        strlcpy(printerConfig.printerIP, printerIP.c_str(), sizeof(printerConfig.printerIP));
    if (printerSerial.length() > 0)
        strlcpy(printerConfig.serialNumber, printerSerial.c_str(), sizeof(printerConfig.serialNumber));
    if (accessCode.length() > 0)
        strlcpy(printerConfig.accessCode, accessCode.c_str(), sizeof(printerConfig.accessCode));

    // WebUI login: an empty username turns protection off; an empty password keeps the
    // stored one.
    String webUser = param("webUser");
    String webPass = param("webPass");
    strlcpy(securityVariables.HTTPUser, webUser.c_str(), sizeof(securityVariables.HTTPUser));
    if (webUser.length() == 0)
        securityVariables.HTTPPass[0] = '\0';
    else if (webPass.length() > 0)
        strlcpy(securityVariables.HTTPPass, webPass.c_str(), sizeof(securityVariables.HTTPPass));

    saveFileSystem();

    request->send(200, "text/plain", "Settings saved, restarting...");
    shouldRestart = true;
    restartRequestTime = millis();
}

void websocketLoop()
{
    if (ws.count() == 0)
        return;
    if (millis() - lastWsPush > wsPushInterval)
    {
        lastWsPush = millis();

        JsonDocument doc;
        doc["wifi_rssi"] = WiFi.RSSI();
        doc["ip"] = WiFi.localIP().toString();
        doc["uptime"] = millis() / 1000;
        doc["doorOpen"] = printerVariables.doorOpen;
        doc["printerConnection"] = printerVariables.online;
        doc["clients"] = ws.count();
        doc["stg_cur"] = printerVariables.stage;
        api::status(doc["lights"].to<JsonObject>()); // what the strip shows and why
        sendJsonToAll(doc);
    }
}

void handleConfigPage(AsyncWebServerRequest *request)
{
    if (!isAuthorized(request))
        return request->requestAuthentication();
    AsyncWebServerResponse *response = request->beginResponse(200, backupRestore_html_gz_mime, backupRestore_html_gz, backupRestore_html_gz_len);
    response->addHeader("Content-Encoding", "gzip");
    request->send(response);
}

void handleDownloadConfigFile(AsyncWebServerRequest *request)
{
    if (!isAuthorized(request))
    {
        return request->requestAuthentication();
    }

    if (!LittleFS.exists(configPath))
    {
        request->send(404, "text/plain", "Config file not found");
        return;
    }

    File configFile = LittleFS.open(configPath, "r");
    if (!configFile)
    {
        request->send(500, "text/plain", "Failed to open config file");
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, configFile);
    configFile.close();

    if (error)
    {
        request->send(500, "text/plain", "Failed to parse config file");
        return;
    }

    String jsonString;
    serializeJsonPretty(doc, jsonString);

    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", jsonString);
    response->addHeader("Content-Disposition", "attachment; filename=\"blledconfig.json\"");
    request->send(response);
}

void handleWebSerialPage(AsyncWebServerRequest *request)
{
    if (!isAuthorized(request))
        return request->requestAuthentication();
    AsyncWebServerResponse *response = request->beginResponse(200, webSerialPage_html_gz_mime, webSerialPage_html_gz, webSerialPage_html_gz_len);
    response->addHeader("Content-Encoding", "gzip");
    request->send(response);
}

void handlePrinterList(AsyncWebServerRequest *request)
{
    if (!isAuthorized(request))
    {
        return request->requestAuthentication();
    }
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();

    for (int i = 0; i < bblKnownPrinterCount; i++)
    {
        JsonObject obj = arr.add<JsonObject>();
        obj["ip"] = bblLastKnownPrinters[i].ip.toString();
        obj["usn"] = bblLastKnownPrinters[i].usn;
    }

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
}

void handleFactoryReset(AsyncWebServerRequest *request)
{
    if (!isAuthorized(request))
        return request->requestAuthentication();

    LogSerial.println(F("[FactoryReset] Performing full reset..."));

    api::freeze();      // a pending autosave must not recreate it
    deleteFileSystem(); // delete LittleFS config
    request->send(200, "text/plain", "Factory reset complete. Restarting...");

    shouldRestart = true;
    restartRequestTime = millis();
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
               void *arg, uint8_t *data, size_t len)
{
    switch (type)
    {
    case WS_EVT_CONNECT:
        LogSerial.printf("[WS] Client connected: %u\n", client->id());
        websocketLoop();
        break;
    case WS_EVT_DISCONNECT:
        LogSerial.printf("[WS] Client disconnected: %u\n", client->id());
        ws.cleanupClients();
        break;
    case WS_EVT_DATA:
        LogSerial.printf("[WS] Data received from client %u\n", client->id());
        break;
    case WS_EVT_PONG:
        LogSerial.printf("[WS] Pong received from %u\n", client->id());
        break;
    case WS_EVT_ERROR:
        LogSerial.printf("[WS] Error on connection %u\n", client->id());
        ws.cleanupClients();
        break;
    }
}

void setupWebserver()
{
    if (!MDNS.begin(globalVariables.Host.c_str()))
    {
        LogSerial.println(F("Error setting up MDNS responder!"));
        while (1)
            delay(500);
    }

    LogSerial.println(F("Setting up webserver"));

    webServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
                 {
    if (WiFi.getMode() & WIFI_AP) {
        LogSerial.println(F("[WebServer] Captive Portal activ – redirect to /wifi"));
        request->redirect("/wifi");
    } else {
        handleSetup(request);
    } });
    webServer.on("/fwupdate", HTTP_GET, handleUpdatePage);
    setupApiRoutes();
    webServer.on("/favicon.ico", HTTP_GET, handleGetfavicon);
    webServer.on("/config.json", HTTP_GET, handlePrinterConfigJson);
    webServer.on("/wifi", HTTP_GET, handleWiFiSetupPage);
    webServer.on("/wifiScan", HTTP_GET, handleWiFiScan);
    webServer.on("/submitWiFi", HTTP_POST, handleSubmitWiFi);
    webServer.on("/ui.css", HTTP_GET, handleUiCss);
    webServer.on("/backuprestore", HTTP_GET, handleConfigPage);
    webServer.on("/configfile.json", HTTP_GET, handleDownloadConfigFile);
    webServer.on("/webserial", HTTP_GET, handleWebSerialPage);
    webServer.on("/printerList", HTTP_GET, handlePrinterList);
    webServer.on("/factoryreset", HTTP_GET, handleFactoryReset);
    // Uploads: the upload callback runs before the completion handler, so the login has
    // to be checked there too or the data is written before anyone is authenticated.
    webServer.on("/configrestore", HTTP_POST, [](AsyncWebServerRequest *request)
                 {
        if (!isAuthorized(request)) {
            return request->requestAuthentication();
        }
        if (!configUploadOk) {
            request->send(400, "text/plain", "Config file rejected (not valid BLLED JSON). Nothing was changed.");
            return;
        }
        request->send(200, "text/plain", "Config uploaded. Restarting...");
        shouldRestart = true;
        restartRequestTime = millis(); }, [](AsyncWebServerRequest *request, const String &filename, size_t index, uint8_t *data, size_t len, bool final)
                 {
        static File uploadFile;
        static bool uploadAuthorized = false;

        if (!index) {
            configUploadOk = false;
            uploadAuthorized = isAuthorized(request);
            if (!uploadAuthorized) {
                LogSerial.println(F("[ConfigUpload] Rejected: not authorized"));
                return;
            }
            LogSerial.printf("[ConfigUpload] Start: %s\n", filename.c_str());
            // Write to a temporary file; it only replaces the real config once it parses.
            uploadFile = LittleFS.open(configUploadPath, "w");
        }
        if (!uploadAuthorized) {
            return;
        }
        if (uploadFile) {
            uploadFile.write(data, len);
        }
        if (final) {
            if (uploadFile) {
                uploadFile.close();
            }
            configUploadOk = isValidConfigFile(configUploadPath);
            if (configUploadOk) {
                api::freeze(); // a pending autosave must not overwrite the restored file
                LittleFS.remove(configPath);
                configUploadOk = LittleFS.rename(configUploadPath, configPath);
            } else {
                LittleFS.remove(configUploadPath);
            }
            LogSerial.println(configUploadOk ? F("[ConfigUpload] Finished") : F("[ConfigUpload] Invalid file - discarded"));
        } });

    webServer.on("/update", HTTP_POST, [](AsyncWebServerRequest *request)
                 {
        if (!isAuthorized(request)) {
            return request->requestAuthentication();
        }
        if (!otaOk) {
            request->send(500, "text/plain", "Update failed - see the Web Serial log. The current firmware is still installed.");
            return;
        }
        request->send(200, "text/plain", "OK");
        LogSerial.println(F("OTA Upload done. Marking for restart."));
        shouldRestart = true;
        restartRequestTime = millis(); }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
                 {
        static bool otaAuthorized = false;

        if (!index) {
            otaOk = false;
            otaAuthorized = isAuthorized(request);
            if (!otaAuthorized) {
                LogSerial.println(F("[OTA] Rejected: not authorized"));
                return;
            }
            LogSerial.printf("[OTA] Start: %s\n", filename.c_str());
            if (Update.isRunning()) {
                Update.abort(); // left over from an interrupted upload
            }
            if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                Update.printError(LogSerial);
            }
        }
        if (!otaAuthorized || Update.hasError()) {
            return;
        }

        if (Update.write(data, len) != len) {
            Update.printError(LogSerial);
        }

        if (final) {
            if (Update.end(true)) {
                otaOk = true;
                LogSerial.printf("[OTA] Success (%u bytes). Awaiting reboot...\n", index + len);
            } else {
                Update.printError(LogSerial);
            }
        } });

    // The log can contain the printer IP/serial; protect it like the other pages.
    LogSerial.setAuthentication(securityVariables.HTTPUser, securityVariables.HTTPPass);
    LogSerial.begin(&webServer);

    ws.onEvent(onWsEvent);
    webServer.addHandler(&ws);

    webServer.begin();

    LogSerial.println(F("Webserver started"));
}

#endif
