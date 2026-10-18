#ifndef _BLLED_API
#define _BLLED_API

// The setup page's JSON API (docs/REDESIGN.md), kept free of the web server so it can be
// tested on the host; web-server.h only routes requests here. Handlers run on the web
// server's task: they change state under the lock and leave the LEDs to the loop.

#include <Arduino.h>
#include <ArduinoJson.h>
#include "lights.h"
void saveFileSystem(); // filesystem.h

namespace api
{
    // Autosave: every change applies at once; the file is written once changes stop for a
    // moment, so dragging a slider doesn't rewrite flash dozens of times.
    const unsigned long SAVE_DELAY_MS = 1500;
    bool dirty = false;
    bool frozen = false;            // a restore or factory reset is about to restart: keep its file
    unsigned long dirtyAt = 0;

    void markDirty()
    {
        if (frozen) return;
        dirty = true;
        dirtyAt = millis();
    }
    void flush()
    {
        StateLock lock;
        if (!dirty) return;
        dirty = false;
        saveFileSystem();
    }
    void loop()
    {
        if (dirty && millis() - dirtyAt >= SAVE_DELAY_MS) flush();
    }
    // Before a restart that replaced or deleted the config file.
    void freeze()
    {
        StateLock lock;
        dirty = false;
        frozen = true;
    }

    // GET /api/settings
    void getSettings(JsonObject out)
    {
        StateLock lock;
        v2::toJson(settings, out);
    }

    // POST /api/settings: a partial update (only the keys sent change). Replies with the
    // settings as now in effect, so the page shows what was actually accepted.
    void patchSettings(JsonVariantConst body, JsonObject out)
    {
        StateLock lock;
        settings = v2::applyPatch(settings, body);
        lightsSettingsChanged();
        markDirty();
        v2::toJson(settings, out);
    }

    // GET /api/status, and the web socket push: what the strip shows and why. The page
    // turns the names into a sentence ("Off: idle for 10 min, open the door to wake").
    void status(JsonObject o)
    {
        StateLock lock;
        const v2::Decision &d = lastDecision;
        o["reason"] = v2::REASON_NAMES[d.reason];
        o["show"] = v2::SHOW_NAMES[d.show];
        if (d.state >= 0) o["state"] = v2::STATE_NAMES[d.state];
        if (d.detail >= 0 && d.state == v2::ST_PREPARING) o["detail"] = v2::PREP_NAMES[d.detail];
        if (d.detail >= 0 && d.state == v2::ST_ATTENTION) o["detail"] = v2::ATTN_NAMES[d.detail];
        if (d.detail >= 0 && d.state == v2::ST_ERROR) o["detail"] = v2::ERR_NAMES[d.detail];
        if (d.show == v2::SHOW_COLOUR)
        {
            v2::colourToJson(d.colour, o["colour"].to<JsonObject>());
            o["rhythm"] = v2::RHYTHM_NAMES[d.rhythm];
        }
        if (d.show == v2::SHOW_WIFI) v2::colourToJson(wifiColour(), o["colour"].to<JsonObject>());
        o["rssi"] = WiFi.RSSI();
        JsonArray outs = o["outputs"].to<JsonArray>(); // as shown, before brightness
        for (uint8_t v : shown.out) outs.add(v);
        o["brightness"] = settings.brightness;
        o["mode"] = v2::MODE_NAMES[settings.mode];
        o["firmware"] = globalVariables.FWVersion;
        o["tool"] = v2::TOOL_NAMES[activeTool];
        if (activeTool != v2::TOOL_NONE && toolEndsMs) o["preview"] = true;
        if (checkOutput >= 0) o["check"] = v2::OUTPUT_NAMES[checkOutput];

        // Countdowns the sentence can mention (seconds; absent when not running).
        const unsigned long now = millis();
        if (settings.idleOffMin && d.reason == v2::RS_IDLE)
        {
            unsigned long limit = (unsigned long)settings.idleOffMin * 60000UL, idle = now - lastActivityMs;
            o["idleOffIn"] = idle < limit ? (limit - idle) / 1000 : 0;
        }
        if (finishShowing && finishEnds() != v2::FIN_DOOR)
        {
            unsigned long limit = (unsigned long)settings.finishMin * 60000UL, gone = now - finishStartMs;
            o["finishEndsIn"] = gone < limit ? (limit - gone) / 1000 : 0;
        }
        if (d.reason == v2::RS_IDLE_OFF) o["idleFor"] = (now - lastActivityMs) / 1000;

        JsonObject p = o["printer"].to<JsonObject>();
        p["connected"] = printerVariables.online;
        p["accessRejected"] = printerVariables.accessRejected;
        p["gcodeState"] = printerVariables.gcodeState;
        p["stage"] = printerVariables.stage;
        p["door"] = printerVariables.doorOpen;
        p["light"] = printerVariables.printerledstate;
        p["detectedModel"] = printerVariables.detectedModel;
        p["model"] = strcmp(settings.model, "auto") == 0 ? printerVariables.detectedModel : settings.model;
        p["hasLidar"] = modelHasLidar();
    }

    inline bool fail(JsonObject out, const char *why)
    {
        out["error"] = why;
        return false;
    }

    // POST /api/tool {"tool":"test","colour":{...},"rhythm":"pulse","seconds":5} |
    // {"tool":"cycle"|"wifi"|"none"}. Tools are temporary: never saved, they run until Stop,
    // a restart, or `seconds` (1-60, for a preview).
    bool tool(JsonVariantConst body, JsonObject out)
    {
        int t = v2::lookup(v2::TOOL_NAMES, body["tool"].as<const char *>(), -1);
        if (t < 0) return fail(out, "unknown tool");
        int secs = body["seconds"] | 0;
        secs = secs < 0 ? 0 : secs > 60 ? 60 : secs;
        if (t == v2::TOOL_TEST)
        {
            v2::LookColour c = v2::colourFromJson(body["colour"], testColour);
            if (c.kind == v2::CK_SAME) c = testColour;
            v2::Rhythm r = (v2::Rhythm)v2::lookup(v2::RHYTHM_NAMES, body["rhythm"] | (const char *)nullptr, v2::R_STEADY);
            lightsSetTool(v2::TOOL_TEST, &c, r, secs);
        }
        else
            lightsSetTool((v2::Tool)t, nullptr, v2::R_STEADY, secs);
        out["tool"] = v2::TOOL_NAMES[t];
        return true;
    }

    // POST /api/printer {"ip":"192.0.2.10","serial":"…","accessCode":"12345678"}: which
    // printer to follow (setup step 1). A blank access code keeps the stored one. Returns
    // false with an error for bad input; `changed` says whether a restart is needed.
    bool setPrinter(JsonVariantConst body, JsonObject out, bool &changed)
    {
        changed = false;
        String ip = body["ip"] | "", serial = body["serial"] | "", code = body["accessCode"] | "";
        ip.trim(); serial.trim(); code.trim();
        IPAddress addr;
        if (!addr.fromString(ip) || ip.length() > 15) return fail(out, "ip");
        if (serial.length() == 0 || serial.length() >= sizeof(printerConfig.serialNumber)) return fail(out, "serial");
        for (unsigned i = 0; i < serial.length(); i++)
            if (!isalnum((unsigned char)serial.charAt(i))) return fail(out, "serial");
        if (code.length() && code.length() != 8) return fail(out, "accessCode");
        if (!code.length() && !strlen(printerConfig.accessCode)) return fail(out, "accessCode");
        StateLock lock;
        changed = ip != printerConfig.printerIP || serial != printerConfig.serialNumber ||
                  (code.length() && code != printerConfig.accessCode);
        strlcpy(printerConfig.printerIP, ip.c_str(), sizeof(printerConfig.printerIP));
        strlcpy(printerConfig.serialNumber, serial.c_str(), sizeof(printerConfig.serialNumber));
        if (code.length()) strlcpy(printerConfig.accessCode, code.c_str(), sizeof(printerConfig.accessCode));
        out["restart"] = changed;
        return true;
    }

    // POST /api/check {"output":"R"|"G"|"B"|"WW"|"CW"} lights just that output;
    // {"output":null} ends the wiring check. The page saves the answer through /api/settings.
    bool check(JsonVariantConst body, JsonObject out)
    {
        if (body["output"].isNull())
        {
            lightsCheckOutput(-1);
            return true;
        }
        int o = v2::lookup(v2::OUTPUT_NAMES, body["output"].as<const char *>(), -1);
        if (o < 0) return fail(out, "unknown output");
        lightsCheckOutput(o);
        out["check"] = v2::OUTPUT_NAMES[o];
        return true;
    }
}

#endif
