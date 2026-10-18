#ifndef _BLLED_SETTINGS_V2
#define _BLLED_SETTINGS_V2

// Settings v2 (docs/REDESIGN.md): the model behind the redesigned setup page, its JSON form,
// and migration from the v1 config. No hardware dependencies, so it is tested on the host.

#include <Arduino.h>
#include <ArduinoJson.h>

namespace v2
{
    enum LightsMode : uint8_t { MODE_FOLLOW, MODE_ON, MODE_OFF };
    enum LookPreset : uint8_t { LOOK_STANDARD, LOOK_WHITE, LOOK_CUSTOM };
    enum ColourKind : uint8_t { CK_WARM, CK_NEUTRAL, CK_COOL, CK_CUSTOM, CK_OFF, CK_SAME };
    enum Rhythm : uint8_t { R_STEADY, R_PULSE, R_SLOW, R_BLINK };
    enum State : uint8_t { ST_PRINTING, ST_IDLE, ST_PREPARING, ST_ATTENTION, ST_ERROR, ST_FINISHED, ST_COUNT };
    enum PrepStep : uint8_t { PS_NOZZLE, PS_LEVEL, PS_EXTRUDE, PS_SCAN, PS_FIRSTLAYER, PS_LIDARCAL, PS_COUNT };
    enum AttnKind : uint8_t { AK_PAUSE, AK_FIRSTLAYER, AK_CLOG, AK_COUNT };
    enum ErrKind : uint8_t { EK_RUNOUT, EK_COVER, EK_NOZZLETEMP, EK_BEDTEMP, EK_HMS_SERIOUS, EK_HMS_FATAL, EK_COUNT };
    enum FinishEnds : uint8_t { FIN_DOOR, FIN_TIMER, FIN_EITHER };
    enum StripType : uint8_t { STRIP_RGBWW, STRIP_RGBW, STRIP_RGB, STRIP_WWCW, STRIP_W };
    // Strip lights, and the controller's five outputs (board labels R G B WW CW).
    enum Light : uint8_t { L_R, L_G, L_B, L_W, L_WW, L_CW, L_COUNT };
    enum Output : int8_t { OUT_NONE = -1, OUT_R = 0, OUT_G, OUT_B, OUT_WW, OUT_CW, OUT_COUNT };

    // A colour: one of the named whites, off, "same as the group" (sub-entries only), or a
    // custom RGB optionally mixed with warm/cool white (v1 configs can hold such mixes).
    struct LookColour
    {
        ColourKind kind = CK_OFF;
        uint8_t r = 0, g = 0, b = 0, ww = 0, cw = 0;
        bool operator==(const LookColour &o) const { return kind == o.kind && r == o.r && g == o.g && b == o.b && ww == o.ww && cw == o.cw; }
        bool operator!=(const LookColour &o) const { return !(*this == o); }
    };
    struct StateLook
    {
        LookColour colour;
        Rhythm rhythm = R_STEADY;
        StateLook() = default;
        StateLook(const LookColour &c, Rhythm r) : colour(c), rhythm(r) {} // the firmware builds as C++11
    };
    struct Looks
    {
        StateLook state[ST_COUNT];
        LookColour prep[PS_COUNT]; // CK_SAME = use the Preparing colour
        LookColour attn[AK_COUNT]; // CK_SAME = use the Needs attention colour
        LookColour err[EK_COUNT];  // CK_SAME = use the Error colour
    };

    struct Settings
    {
        uint8_t version = 2;
        bool setupDone = false;
        // Lights
        LightsMode mode = MODE_FOLLOW;
        uint8_t brightness = 100;
        LookPreset look = LOOK_STANDARD;
        Looks custom;              // the Custom set (also kept while a preset is chosen)
        bool followLight = true;   // off whenever the printer's own light is off
        // Timing
        uint16_t idleOffMin = 10;  // 0 = never
        FinishEnds finishEnds = FIN_DOOR;
        uint16_t finishMin = 10;
        uint16_t offlineOffSec = 30; // 0 = never
        // The printer's own light
        bool lightOnPrintStart = false, lightOnDoor = false, lightOffIdle = false, lightOffFinishTimer = false;
        // LED strip
        StripType strip = STRIP_RGBWW;
        int8_t wiring[L_COUNT];    // light -> output, set by the wiring check; OUT_NONE = board label
        // Printer
        char model[12] = "auto";   // "auto" = what the printer reports
        // Advanced
        String hmsIgnore;
        bool debugLog = false, mqttLog = false;
        Settings() { for (auto &w : wiring) w = OUT_NONE; }
    };

    inline LookColour colour(ColourKind k) { LookColour c; c.kind = k; return c; }
    inline LookColour rgb(uint8_t r, uint8_t g, uint8_t b, uint8_t ww = 0, uint8_t cw = 0)
    {
        LookColour c; c.kind = CK_CUSTOM; c.r = r; c.g = g; c.b = b; c.ww = ww; c.cw = cw; return c;
    }

    // The two fixed presets (Custom starts as a copy of Standard).
    inline Looks presetLooks(LookPreset p)
    {
        Looks L;
        const bool white = p == LOOK_WHITE;
        L.state[ST_PRINTING] = {colour(CK_NEUTRAL), R_STEADY};
        L.state[ST_IDLE] = {colour(CK_NEUTRAL), R_STEADY};
        L.state[ST_PREPARING] = {colour(CK_OFF), R_STEADY};
        L.state[ST_ATTENTION] = {white ? colour(CK_NEUTRAL) : rgb(0, 0, 255), R_PULSE};
        L.state[ST_ERROR] = {white ? colour(CK_NEUTRAL) : rgb(255, 0, 0), R_BLINK};
        L.state[ST_FINISHED] = {white ? colour(CK_NEUTRAL) : rgb(0, 255, 0), R_SLOW};
        for (auto &c : L.prep) c = colour(CK_SAME);
        L.prep[PS_LEVEL] = rgb(0, 0, 0x55); // dim blue while levelling (upstream default)
        for (auto &c : L.attn) c = colour(CK_SAME);
        for (auto &c : L.err) c = colour(CK_SAME);
        return L;
    }
    inline Settings defaults()
    {
        Settings s;
        s.custom = presetLooks(LOOK_STANDARD);
        return s;
    }
    // What is in effect: the preset's looks, or the Custom set.
    inline Looks effectiveLooks(const Settings &s) { return s.look == LOOK_CUSTOM ? s.custom : presetLooks(s.look); }

    // ---------------- JSON (v2) ----------------
    static const char *const MODE_NAMES[] = {"follow", "on", "off"};
    static const char *const LOOK_NAMES[] = {"standard", "white", "custom"};
    static const char *const KIND_NAMES[] = {"warm", "neutral", "cool", "custom", "off", "same"};
    static const char *const RHYTHM_NAMES[] = {"steady", "pulse", "slow", "blink"};
    static const char *const STATE_NAMES[] = {"printing", "idle", "preparing", "attention", "error", "finished"};
    static const char *const PREP_NAMES[] = {"nozzle", "level", "extrude", "scan", "firstlayer", "lidarcal"};
    static const char *const ATTN_NAMES[] = {"pause", "firstlayer", "clog"};
    static const char *const ERR_NAMES[] = {"runout", "cover", "nozzleTemp", "bedTemp", "hmsSerious", "hmsFatal"};
    static const char *const FINISH_NAMES[] = {"door", "timer", "either"};
    static const char *const STRIP_NAMES[] = {"rgbww", "rgbw", "rgb", "wwcw", "w"};
    static const char *const LIGHT_NAMES[] = {"r", "g", "b", "w", "ww", "cw"};
    static const char *const OUTPUT_NAMES[] = {"R", "G", "B", "WW", "CW"};

    // Index of `s` in `names`, or `fallback` when absent/unknown (so a bad value can't crash).
    template <size_t N>
    inline int lookup(const char *const (&names)[N], const char *s, int fallback)
    {
        if (!s) return fallback;
        for (size_t i = 0; i < N; i++)
            if (strcmp(names[i], s) == 0) return (int)i;
        return fallback;
    }
    inline void hexOut(char *buf, uint8_t r, uint8_t g, uint8_t b) { snprintf(buf, 8, "#%02x%02x%02x", r, g, b); }
    inline bool hexIn(const char *s, uint8_t &r, uint8_t &g, uint8_t &b)
    {
        if (!s || s[0] != '#' || strlen(s) != 7) return false;
        for (int i = 1; i < 7; i++) if (!isxdigit((unsigned char)s[i])) return false;
        long v = strtol(s + 1, nullptr, 16);
        r = (v >> 16) & 255; g = (v >> 8) & 255; b = v & 255;
        return true;
    }

    inline void colourToJson(const LookColour &c, JsonObject o)
    {
        o["kind"] = KIND_NAMES[c.kind];
        if (c.kind == CK_CUSTOM)
        {
            char hex[8]; hexOut(hex, c.r, c.g, c.b);
            o["hex"] = hex;
            if (c.ww) o["ww"] = c.ww;
            if (c.cw) o["cw"] = c.cw;
        }
    }
    inline LookColour colourFromJson(JsonVariantConst v, const LookColour &fallback)
    {
        if (!v.is<JsonObjectConst>()) return fallback;
        LookColour c;
        c.kind = (ColourKind)lookup(KIND_NAMES, v["kind"] | (const char *)nullptr, fallback.kind);
        if (c.kind == CK_CUSTOM)
        {
            if (!hexIn(v["hex"] | (const char *)nullptr, c.r, c.g, c.b)) return fallback;
            c.ww = v["ww"] | 0; c.cw = v["cw"] | 0;
        }
        return c;
    }
    inline void looksToJson(const Looks &L, JsonObject o)
    {
        for (int i = 0; i < ST_COUNT; i++)
        {
            JsonObject st = o[STATE_NAMES[i]].to<JsonObject>();
            colourToJson(L.state[i].colour, st["colour"].to<JsonObject>());
            st["rhythm"] = RHYTHM_NAMES[L.state[i].rhythm];
        }
        JsonObject p = o["prep"].to<JsonObject>();
        for (int i = 0; i < PS_COUNT; i++) colourToJson(L.prep[i], p[PREP_NAMES[i]].to<JsonObject>());
        JsonObject a = o["attn"].to<JsonObject>();
        for (int i = 0; i < AK_COUNT; i++) colourToJson(L.attn[i], a[ATTN_NAMES[i]].to<JsonObject>());
        JsonObject e = o["err"].to<JsonObject>();
        for (int i = 0; i < EK_COUNT; i++) colourToJson(L.err[i], e[ERR_NAMES[i]].to<JsonObject>());
    }
    inline Looks looksFromJson(JsonVariantConst o, const Looks &fallback)
    {
        Looks L = fallback;
        if (!o.is<JsonObjectConst>()) return L;
        for (int i = 0; i < ST_COUNT; i++)
        {
            JsonVariantConst st = o[STATE_NAMES[i]];
            L.state[i].colour = colourFromJson(st["colour"], fallback.state[i].colour);
            L.state[i].rhythm = (Rhythm)lookup(RHYTHM_NAMES, st["rhythm"] | (const char *)nullptr, fallback.state[i].rhythm);
        }
        for (int i = 0; i < PS_COUNT; i++) L.prep[i] = colourFromJson(o["prep"][PREP_NAMES[i]], fallback.prep[i]);
        for (int i = 0; i < AK_COUNT; i++) L.attn[i] = colourFromJson(o["attn"][ATTN_NAMES[i]], fallback.attn[i]);
        for (int i = 0; i < EK_COUNT; i++) L.err[i] = colourFromJson(o["err"][ERR_NAMES[i]], fallback.err[i]);
        return L;
    }

    // The LED settings only; Wi-Fi, printer connection and login stay where they are (v1 keys).
    inline void toJson(const Settings &s, JsonObject o)
    {
        o["version"] = s.version;
        o["setupDone"] = s.setupDone;
        o["mode"] = MODE_NAMES[s.mode];
        o["brightness"] = s.brightness;
        o["look"] = LOOK_NAMES[s.look];
        looksToJson(s.custom, o["custom"].to<JsonObject>());
        o["followLight"] = s.followLight;
        o["idleOffMin"] = s.idleOffMin;
        o["finishEnds"] = FINISH_NAMES[s.finishEnds];
        o["finishMin"] = s.finishMin;
        o["offlineOffSec"] = s.offlineOffSec;
        JsonObject pl = o["printerLight"].to<JsonObject>();
        pl["onPrintStart"] = s.lightOnPrintStart; pl["onDoor"] = s.lightOnDoor;
        pl["offIdle"] = s.lightOffIdle; pl["offFinishTimer"] = s.lightOffFinishTimer;
        o["strip"] = STRIP_NAMES[s.strip];
        JsonObject w = o["wiring"].to<JsonObject>();
        for (int i = 0; i < L_COUNT; i++)
            if (s.wiring[i] != OUT_NONE) w[LIGHT_NAMES[i]] = OUTPUT_NAMES[s.wiring[i]];
        o["model"] = s.model;
        o["hmsIgnore"] = s.hmsIgnore;
        o["debugLog"] = s.debugLog;
        o["mqttLog"] = s.mqttLog;
    }
    inline Settings fromJson(JsonVariantConst o)
    {
        Settings d = defaults(), s = d;
        s.setupDone = o["setupDone"] | false;
        s.mode = (LightsMode)lookup(MODE_NAMES, o["mode"] | (const char *)nullptr, d.mode);
        s.brightness = constrain((int)(o["brightness"] | (int)d.brightness), 0, 100);
        s.look = (LookPreset)lookup(LOOK_NAMES, o["look"] | (const char *)nullptr, d.look);
        s.custom = looksFromJson(o["custom"], d.custom);
        s.followLight = o["followLight"] | d.followLight;
        s.idleOffMin = o["idleOffMin"] | d.idleOffMin;
        s.finishEnds = (FinishEnds)lookup(FINISH_NAMES, o["finishEnds"] | (const char *)nullptr, d.finishEnds);
        s.finishMin = o["finishMin"] | d.finishMin;
        if (s.finishMin == 0) s.finishMin = d.finishMin;
        s.offlineOffSec = o["offlineOffSec"] | d.offlineOffSec;
        JsonVariantConst pl = o["printerLight"];
        s.lightOnPrintStart = pl["onPrintStart"] | false; s.lightOnDoor = pl["onDoor"] | false;
        s.lightOffIdle = pl["offIdle"] | false; s.lightOffFinishTimer = pl["offFinishTimer"] | false;
        s.strip = (StripType)lookup(STRIP_NAMES, o["strip"] | (const char *)nullptr, d.strip);
        for (int i = 0; i < L_COUNT; i++)
            s.wiring[i] = (int8_t)lookup(OUTPUT_NAMES, o["wiring"][LIGHT_NAMES[i]] | (const char *)nullptr, OUT_NONE);
        strlcpy(s.model, o["model"] | "auto", sizeof(s.model));
        s.hmsIgnore = o["hmsIgnore"] | "";
        s.debugLog = o["debugLog"] | false;
        s.mqttLog = o["mqttLog"] | false;
        return s;
    }

    // ---------------- migration from the v1 config ----------------
    // v1 colours: "<key>RGB" hex + "<key>WW" / "<key>CW" levels.
    inline LookColour v1Colour(JsonVariantConst o, const char *key)
    {
        char k[24];
        uint8_t r = 0, g = 0, b = 0;
        snprintf(k, sizeof k, "%s%s", key, strcmp(key, "finish") == 0 ? "Color" : "RGB");
        hexIn(o[k] | "#000000", r, g, b);
        snprintf(k, sizeof k, "%sWW", key); uint8_t ww = o[k] | 0;
        snprintf(k, sizeof k, "%sCW", key); uint8_t cw = o[k] | 0;
        if (!r && !g && !b && !ww && !cw) return colour(CK_OFF);
        if (!r && !g && !b && ww == 255 && cw == 255) return colour(CK_NEUTRAL);
        if (!r && !g && !b && ww == 255 && !cw) return colour(CK_WARM);
        if (!r && !g && !b && !ww && cw == 255) return colour(CK_COOL);
        return rgb(r, g, b, ww, cw);
    }

    inline Settings migrateV1(JsonVariantConst o)
    {
        Settings s = defaults();
        s.setupDone = true; // it was configured
        s.brightness = constrain((int)(o["brightness"] | 100), 0, 100);
        // v1 LED behaviour: maintenance mode = always on; replicate = follow the printer's light.
        // Colour test, RGB cycle and Wi-Fi strength become temporary tools, so they map to follow.
        s.mode = (o["maintMode"] | false) ? MODE_ON : MODE_FOLLOW;
        s.followLight = o["replicatestate"] | true;

        Looks L = presetLooks(LOOK_STANDARD);
        const bool rhythms = o["statusAnimations"] | true;
        LookColour running = v1Colour(o, "running");
        L.state[ST_PRINTING] = {running, R_STEADY};
        L.state[ST_IDLE] = {running, R_STEADY};
        // Group colours from the main v1 colour of each group; the rest become per-kind entries.
        L.state[ST_ATTENTION] = {v1Colour(o, "pause"), rhythms ? R_PULSE : R_STEADY};
        L.state[ST_ERROR] = {v1Colour(o, "hmsSerious"), rhythms ? R_BLINK : R_STEADY};
        L.state[ST_FINISHED] = {(o["finishindication"] | true) ? v1Colour(o, "finish") : running,
                                (o["finishindication"] | true) && rhythms ? R_SLOW : R_STEADY};
        L.state[ST_PREPARING] = {v1Colour(o, "stage14"), R_STEADY};
        auto sub = [](const LookColour &c, const LookColour &group) { return c == group ? colour(CK_SAME) : c; };
        const LookColour &prep = L.state[ST_PREPARING].colour;
        L.prep[PS_NOZZLE] = sub(v1Colour(o, "stage14"), prep);
        L.prep[PS_LEVEL] = sub(v1Colour(o, "stage1"), prep);
        L.prep[PS_EXTRUDE] = sub(v1Colour(o, "stage8"), prep);
        L.prep[PS_SCAN] = sub(v1Colour(o, "stage9"), prep);
        L.prep[PS_FIRSTLAYER] = sub(v1Colour(o, "stage10"), prep);
        L.prep[PS_LIDARCAL] = sub(v1Colour(o, "stage10"), prep); // v1 reused stage 10's colour
        const LookColour &attn = L.state[ST_ATTENTION].colour;
        L.attn[AK_PAUSE] = colour(CK_SAME);
        L.attn[AK_FIRSTLAYER] = sub(v1Colour(o, "firstlayer"), attn);
        L.attn[AK_CLOG] = sub(v1Colour(o, "nozzleclog"), attn);
        const LookColour &err = L.state[ST_ERROR].colour;
        L.err[EK_RUNOUT] = sub(v1Colour(o, "filamentRunout"), err);
        L.err[EK_COVER] = sub(v1Colour(o, "frontCover"), err);
        L.err[EK_NOZZLETEMP] = sub(v1Colour(o, "nozzleTemp"), err);
        L.err[EK_BEDTEMP] = sub(v1Colour(o, "bedTemp"), err);
        L.err[EK_HMS_SERIOUS] = colour(CK_SAME);
        L.err[EK_HMS_FATAL] = sub(v1Colour(o, "hmsFatal"), err);
        if (!(o["errordetection"] | true))
        { // v1 "error detection" off: errors showed the running colour
            L.state[ST_ERROR] = {running, R_STEADY};
            for (auto &c : L.err) c = colour(CK_SAME);
        }
        s.custom = L;
        // Matches a preset exactly? Then use the preset; otherwise keep it as Custom.
        auto sameLooks = [](const Looks &a, const Looks &b) {
            for (int i = 0; i < ST_COUNT; i++) if (a.state[i].colour != b.state[i].colour || a.state[i].rhythm != b.state[i].rhythm) return false;
            for (int i = 0; i < PS_COUNT; i++) if (a.prep[i] != b.prep[i]) return false;
            for (int i = 0; i < AK_COUNT; i++) if (a.attn[i] != b.attn[i]) return false;
            for (int i = 0; i < EK_COUNT; i++) if (a.err[i] != b.err[i]) return false;
            return true;
        };
        s.look = sameLooks(L, presetLooks(LOOK_STANDARD)) ? LOOK_STANDARD : LOOK_CUSTOM;

        s.idleOffMin = (o["inactivityEnabled"] | true) ? (uint16_t)((o["inactivityTimeOut"] | 600000L) / 60000L) : 0;
        const bool door = o["finishExit"] | true;
        const bool timer = o["finishExitTimer"] | !door;
        s.finishEnds = door && timer ? FIN_EITHER : (timer ? FIN_TIMER : FIN_DOOR);
        long finishMs = o["finishTimerMins"] | 600000L; // v1 stored milliseconds under this name
        s.finishMin = finishMs >= 60000 ? (uint16_t)(finishMs / 60000) : 10;
        // v1 "Control Chamber Light" bundled four behaviours; keep all four.
        const bool chamber = o["controlChamberLight"] | false;
        s.lightOnPrintStart = s.lightOnDoor = s.lightOffIdle = s.lightOffFinishTimer = chamber;
        s.strip = STRIP_RGBWW; // v1 drove all five outputs; the strip type can be set afterwards
        s.hmsIgnore = o["hmsIgnoreList"] | "";
        s.debugLog = (o["debuging"] | false) || (o["debugingchange"] | false);
        s.mqttLog = o["mqttdebug"] | false;
        return s;
    }

    // A partial update from the setup page (autosave): only the keys sent change. Objects
    // merge recursively (e.g. {"custom":{"error":{"rhythm":"pulse"}}}); anything else replaces.
    inline void mergeJson(JsonObject dst, JsonObjectConst src)
    {
        for (JsonPairConst kv : src)
        {
            if (kv.value().is<JsonObjectConst>() && dst[kv.key()].is<JsonObject>())
                mergeJson(dst[kv.key()].as<JsonObject>(), kv.value().as<JsonObjectConst>());
            else
                dst[kv.key()] = kv.value();
        }
    }
    inline Settings applyPatch(const Settings &current, JsonVariantConst patch)
    {
        if (!patch.is<JsonObjectConst>()) return current;
        JsonDocument d;
        toJson(current, d.to<JsonObject>());
        mergeJson(d.as<JsonObject>(), patch.as<JsonObjectConst>());
        d["version"] = 2; // a patch can't turn the settings back into a v1 migration
        return fromJson(d.as<JsonVariantConst>());
    }

    // Load whatever the config file holds: v2 settings, or a v1 config to migrate.
    inline Settings load(JsonVariantConst o, bool *migrated = nullptr)
    {
        const bool isV2 = (o["version"] | 0) >= 2;
        if (migrated) *migrated = !isV2;
        return isV2 ? fromJson(o) : migrateV1(o);
    }
}

#endif
