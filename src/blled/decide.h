#ifndef _BLLED_DECIDE_V2
#define _BLLED_DECIDE_V2

// LED decision logic v2: printer state + settings in, what the strip shows (and why) out.
// Pure functions, no globals or hardware, so every rule is tested on the host.

#include "settings.h"

namespace v2
{
    enum Hms : uint8_t { HMS_NONE, HMS_SERIOUS, HMS_FATAL };
    enum Tool : uint8_t { TOOL_NONE, TOOL_TEST, TOOL_CYCLE, TOOL_WIFI };

    // What the printer is doing, as the controller sees it.
    struct Snapshot
    {
        bool connected = true;
        bool accessRejected = false;
        bool certRejected = false;         // its certificate didn't check out (code not sent)
        bool everConnected = true;         // false until the first connection since boot       // the printer refused the access code
        unsigned long unreachableMs = 0;   // how long the printer has been unreachable
        String gcodeState = "IDLE";        // IDLE, PREPARE, RUNNING, PAUSE, FINISH, FAILED
        int stage = -1;                    // stg_cur
        int overrideStage = 999;           // stage implied by certain HMS codes
        Hms hms = HMS_NONE;
        bool printerLightOn = true;
        bool finishShowing = false;        // print finished and its ending hasn't happened yet
        unsigned long idleMs = 0;          // time since the last activity
        bool hasLidar = true;              // from the printer model
    };

    enum Reason : uint8_t
    {
        RS_MODE_OFF, RS_MODE_ON, RS_TOOL_TEST, RS_TOOL_CYCLE, RS_TOOL_WIFI,
        RS_UNREACHABLE, RS_ACCESS_REJECTED, RS_CERT_REJECTED, RS_CONNECTING, RS_ERROR, RS_ATTENTION, RS_FINISHED, RS_PREPARING, RS_PREPARING_NO_LIDAR,
        RS_PRINTING, RS_LIGHT_OFF, RS_IDLE_OFF, RS_IDLE,
    };
    enum Show : uint8_t { SHOW_OFF, SHOW_COLOUR, SHOW_CYCLE, SHOW_WIFI };
    // Names for the web API (the page turns them into sentences, in its own language).
    static const char *const REASON_NAMES[] = {"mode_off", "mode_on", "tool_test", "tool_cycle", "tool_wifi",
                                               "unreachable", "access_rejected", "cert_rejected", "connecting", "error", "attention", "finished", "preparing", "preparing_no_lidar",
                                               "printing", "light_off", "idle_off", "idle"};
    static_assert(sizeof(REASON_NAMES) / sizeof(REASON_NAMES[0]) == RS_IDLE + 1, "REASON_NAMES out of step with Reason");
    static const char *const SHOW_NAMES[] = {"off", "colour", "cycle", "wifi"};
    static const char *const TOOL_NAMES[] = {"none", "test", "cycle", "wifi"};

    struct Decision
    {
        Show show = SHOW_OFF;
        LookColour colour;          // when SHOW_COLOUR (subs already resolved)
        Rhythm rhythm = R_STEADY;
        Reason reason = RS_IDLE;
        int8_t state = -1;          // State, when the colour comes from a printer state
        int8_t detail = -1;         // PrepStep / AttnKind / ErrKind within that state
    };

    inline LookColour resolve(const LookColour &sub, const LookColour &group) { return sub.kind == CK_SAME ? group : sub; }

    inline Decision lit(const LookColour &c, Rhythm r, Reason why, int state = -1, int detail = -1)
    {
        Decision d;
        d.show = c.kind == CK_OFF ? SHOW_OFF : SHOW_COLOUR;
        d.colour = c; d.rhythm = d.show == SHOW_OFF ? R_STEADY : r;
        d.reason = why; d.state = state; d.detail = detail;
        return d;
    }
    inline Decision off(Reason why) { Decision d; d.reason = why; return d; }

    // Which error, if any (most specific first).
    inline int errorKind(const Snapshot &p)
    {
        auto is = [&](int st) { return p.stage == st || p.overrideStage == st; };
        if (is(6)) return EK_RUNOUT;
        if (is(17)) return EK_COVER;
        if (is(20)) return EK_NOZZLETEMP;
        if (is(21)) return EK_BEDTEMP;
        if (p.hms == HMS_FATAL) return EK_HMS_FATAL;
        if (p.hms == HMS_SERIOUS) return EK_HMS_SERIOUS;
        return -1;
    }
    // Which "needs attention" case, if any (specific causes before a plain pause).
    inline int attentionKind(const Snapshot &p)
    {
        if (p.stage == 34) return AK_FIRSTLAYER;
        if (p.stage == 35) return AK_CLOG;
        if (p.stage == 16 || p.stage == 30 || p.gcodeState == "PAUSE") return AK_PAUSE;
        return -1;
    }
    inline int prepStep(const Snapshot &p)
    {
        switch (p.stage)
        {
        case 14: return PS_NOZZLE;
        case 1: return PS_LEVEL;
        case 8: return PS_EXTRUDE;
        case 9: return PS_SCAN;
        case 10: return PS_FIRSTLAYER;
        case 12: return PS_LIDARCAL;
        }
        return p.overrideStage == 10 ? PS_FIRSTLAYER : -1;
    }
    inline bool printing(const Snapshot &p)
    {
        return p.gcodeState == "RUNNING" || p.gcodeState == "PREPARE" || p.gcodeState == "PAUSE" || p.stage == 0 || p.stage == 2;
    }

    // The order is the priority (docs/REDESIGN.md, "Behaviour rules").
    inline Decision decide(const Settings &s, const Snapshot &p, Tool tool = TOOL_NONE)
    {
        const Looks L = effectiveLooks(s);
        if (tool == TOOL_TEST) return lit(rgb(255, 255, 255), R_STEADY, RS_TOOL_TEST); // colour set by the caller
        if (tool == TOOL_CYCLE) { Decision d; d.show = SHOW_CYCLE; d.reason = RS_TOOL_CYCLE; return d; }
        if (tool == TOOL_WIFI) { Decision d; d.show = SHOW_WIFI; d.reason = RS_TOOL_WIFI; return d; }
        if (s.mode == MODE_OFF) return off(RS_MODE_OFF);
        if (s.mode == MODE_ON) return lit(L.state[ST_PRINTING].colour, R_STEADY, RS_MODE_ON, ST_PRINTING);

        // Can't talk to the printer: dim red if it refused the access code (fix the code), else off.
        if (!p.connected && p.accessRejected) return lit(rgb(128, 0, 0), R_STEADY, RS_ACCESS_REJECTED);
        if (!p.connected && p.certRejected) return lit(rgb(128, 0, 0), R_STEADY, RS_CERT_REJECTED);
        if (!p.connected && s.offlineOffSec && p.unreachableMs >= (unsigned long)s.offlineOffSec * 1000UL)
            return off(RS_UNREACHABLE);
        // Cyan while it first connects after boot (the start-up colours in Help), then off
        // once that has taken longer than the unreachable delay.
        if (!p.connected && !p.everConnected) return lit(rgb(0, 255, 255), R_STEADY, RS_CONNECTING);

        int k = errorKind(p);
        if (k >= 0) return lit(resolve(L.err[k], L.state[ST_ERROR].colour), L.state[ST_ERROR].rhythm, RS_ERROR, ST_ERROR, k);
        k = attentionKind(p);
        if (k >= 0) return lit(resolve(L.attn[k], L.state[ST_ATTENTION].colour), L.state[ST_ATTENTION].rhythm, RS_ATTENTION, ST_ATTENTION, k);
        if (p.finishShowing) return lit(L.state[ST_FINISHED].colour, L.state[ST_FINISHED].rhythm, RS_FINISHED, ST_FINISHED);
        k = prepStep(p);
        if (k >= 0)
        {
            if (!p.hasLidar) return lit(L.state[ST_PRINTING].colour, L.state[ST_PRINTING].rhythm, RS_PREPARING_NO_LIDAR, ST_PRINTING);
            return lit(resolve(L.prep[k], L.state[ST_PREPARING].colour), L.state[ST_PREPARING].rhythm, RS_PREPARING, ST_PREPARING, k);
        }
        const bool busy = printing(p);
        if (s.followLight && !p.printerLightOn) return off(RS_LIGHT_OFF);
        if (!busy && s.idleOffMin && p.idleMs >= (unsigned long)s.idleOffMin * 60000UL) return off(RS_IDLE_OFF);
        if (busy) return lit(L.state[ST_PRINTING].colour, L.state[ST_PRINTING].rhythm, RS_PRINTING, ST_PRINTING);
        return lit(L.state[ST_IDLE].colour, L.state[ST_IDLE].rhythm, RS_IDLE, ST_IDLE);
    }

    // ---------------- colour -> strip lights -> controller outputs ----------------
    struct Levels { uint8_t v[L_COUNT] = {0, 0, 0, 0, 0, 0}; };   // per strip light (Light)

    // What a colour turns into on the configured strip type (docs/REDESIGN.md, LED strip).
    inline Levels toLights(const LookColour &c, StripType t)
    {
        Levels out; uint8_t *v = out.v;
        if (c.kind == CK_OFF || c.kind == CK_SAME) return out;
        const bool hasRgb = t == STRIP_RGBWW || t == STRIP_RGBW || t == STRIP_RGB;
        if (c.kind == CK_CUSTOM)
        {
            if (hasRgb) { v[L_R] = c.r; v[L_G] = c.g; v[L_B] = c.b; }
            switch (t)
            {
            case STRIP_RGBWW: v[L_WW] = c.ww; v[L_CW] = c.cw; break;
            case STRIP_RGBW: v[L_W] = c.ww > c.cw ? c.ww : c.cw; break;
            case STRIP_RGB:   // mix any white part from RGB
                if (c.ww || c.cw) { uint8_t w = c.ww > c.cw ? c.ww : c.cw; v[L_R] = v[L_R] > w ? v[L_R] : w; v[L_G] = v[L_G] > (w * 235 / 255) ? v[L_G] : w * 235 / 255; v[L_B] = v[L_B] > (w * 210 / 255) ? v[L_B] : w * 210 / 255; }
                break;
            case STRIP_WWCW: v[L_WW] = 255; v[L_CW] = 255; break;   // no colours: neutral white
            case STRIP_W: v[L_W] = 255; break;
            }
            return out;
        }
        // named whites
        switch (t)
        {
        case STRIP_RGBWW: case STRIP_WWCW:
            if (c.kind != CK_COOL) v[L_WW] = 255;
            if (c.kind != CK_WARM) v[L_CW] = 255;
            break;
        case STRIP_RGBW:
            v[L_W] = 255;
            if (c.kind == CK_WARM) { v[L_R] = 90; v[L_G] = 35; }
            if (c.kind == CK_COOL) v[L_B] = 70;
            break;
        case STRIP_RGB:
            if (c.kind == CK_WARM) { v[L_R] = 255; v[L_G] = 170; v[L_B] = 90; }
            else if (c.kind == CK_COOL) { v[L_R] = 200; v[L_G] = 220; v[L_B] = 255; }
            else { v[L_R] = 255; v[L_G] = 235; v[L_B] = 210; }
            break;
        case STRIP_W: v[L_W] = 255; break;
        }
        return out;
    }

    // Board-label outputs for each light; a single white goes to both white outputs, so it
    // works whichever one it's connected to. The wiring check's result overrides.
    inline void outputsFor(const Settings &s, int light, int8_t outs[2])
    {
        outs[0] = outs[1] = OUT_NONE;
        if (s.wiring[light] != OUT_NONE) { outs[0] = s.wiring[light]; return; }
        switch (light)
        {
        case L_R: outs[0] = OUT_R; break;
        case L_G: outs[0] = OUT_G; break;
        case L_B: outs[0] = OUT_B; break;
        case L_WW: outs[0] = OUT_WW; break;
        case L_CW: outs[0] = OUT_CW; break;
        case L_W: outs[0] = OUT_WW; outs[1] = OUT_CW; break;
        }
    }
    struct OutputLevels { uint8_t v[OUT_COUNT] = {0, 0, 0, 0, 0}; };
    inline OutputLevels toOutputs(const Settings &s, const Levels &lv)
    {
        OutputLevels o;
        for (int l = 0; l < L_COUNT; l++)
        {
            if (!lv.v[l]) continue;
            int8_t outs[2]; outputsFor(s, l, outs);
            for (int8_t x : outs)
                if (x != OUT_NONE && lv.v[l] > o.v[x]) o.v[x] = lv.v[l];
        }
        return o;
    }
}

#endif
