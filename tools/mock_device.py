# Mock of the BLLED device web API, to test the setup pages in a browser without the
# hardware. Usage: python3 tools/mock_device.py src/www 8765, then open http://localhost:8765
# (WiFi setup page) or http://localhost:8765/setup (main LED setup page). The v2 API
# (/api/...) mirrors src/blled/api.h; POST /_sim {"printer":"error","detail":"cover"} changes
# what the simulated printer is doing, GET /_state shows everything the page sent.
# There's no /ws here, so the page falls back to polling /api/status.
import http.server, json, sys, urllib.parse
WWW = sys.argv[1]
PORT = int(sys.argv[2])
import copy, re, threading, time

def colour(kind, hexv=None):
    return {"kind": kind, "hex": hexv} if hexv else {"kind": kind}

def preset_looks(name):
    white = name == "white"
    n = colour("neutral")
    L = {"printing": {"colour": n, "rhythm": "steady"}, "idle": {"colour": n, "rhythm": "steady"},
         "preparing": {"colour": colour("off"), "rhythm": "steady"},
         "attention": {"colour": n if white else colour("custom", "#0000ff"), "rhythm": "pulse"},
         "error": {"colour": n if white else colour("custom", "#ff0000"), "rhythm": "blink"},
         "finished": {"colour": n if white else colour("custom", "#00ff00"), "rhythm": "slow"},
         "prep": {k: colour("same") for k in ["nozzle", "level", "extrude", "scan", "firstlayer", "lidarcal"]},
         "attn": {k: colour("same") for k in ["pause", "firstlayer", "clog"]},
         "err": {k: colour("same") for k in ["runout", "cover", "nozzleTemp", "bedTemp", "hmsSerious", "hmsFatal"]}}
    L["prep"]["level"] = colour("custom", "#000055")
    return copy.deepcopy(L)

# Same defaults as v2::defaults() (settings.h). setupDone false so the setup dialog opens.
SETTINGS = {"version": 2, "setupDone": False, "mode": "follow", "brightness": 100, "look": "standard",
            "custom": preset_looks("standard"), "followLight": True, "idleOffMin": 10, "finishEnds": "door",
            "finishMin": 10, "offlineOffSec": 30,
            "printerLight": {"onPrintStart": False, "onDoor": False, "offIdle": False, "offFinishTimer": False},
            "strip": "rgbww", "wiring": {}, "model": "auto", "hmsIgnore": "", "debugLog": False, "mqttLog": False, "checkPrinterCert": True}
ENUMS = {"mode": ["follow", "on", "off"], "look": ["standard", "white", "custom"], "finishEnds": ["door", "timer", "either"],
         "strip": ["rgbww", "rgbw", "rgb", "wwcw", "w"], "model": None}
# Example printers on documentation-only addresses (192.0.2.x), never real ones.
PRINTERS = [{"ip": "192.0.2.10", "serial": "01S00A0000X4821", "name": "Workshop X1C", "model": "X1C"},
            {"ip": "192.0.2.11", "serial": "0SX00B0000Y7730", "name": "Workshop X2D", "model": "X2D"}]
state = {"scan_calls": 0, "submits": [], "settings": SETTINGS, "posts": [], "tool": "none", "toolColour": None,
         "toolRhythm": "steady", "toolEnds": 0, "check": None, "downUntil": 0,
         "conn": {"ssid": "HomeNet", "printerIP": "", "printerSerial": "", "hasAccessCode": False},
         # what the simulated printer is doing (POST /_sim to change it)
         "sim": {"printer": "idle", "detail": None, "light": True, "detectedModel": "X1C", "rssi": -58, "idleFor": 120,
                 "connected": True, "accessRejected": False, "certRejected": False},
         "lookups": {}}
lock = threading.Lock()

def merge(dst, src):
    for k, v in src.items():
        if isinstance(v, dict) and isinstance(dst.get(k), dict): merge(dst[k], v)
        elif v is None: dst.pop(k, None)
        else: dst[k] = copy.deepcopy(v)

def validate(s):
    # The firmware falls back or clamps (settings.h fromJson); mimic the visible parts.
    d = SETTINGS
    for k, vals in ENUMS.items():
        if vals and s.get(k) not in vals: s[k] = d[k]
    s["brightness"] = max(0, min(100, int(s.get("brightness", 100))))
    if not s.get("finishMin"): s["finishMin"] = 10
    s["version"] = 2

def looks(s):
    return s["custom"] if s["look"] == "custom" else preset_looks(s["look"])

def status():
    s, sim = state["settings"], state["sim"]
    if state["toolEnds"] and time.time() >= state["toolEnds"]:
        state["tool"], state["toolEnds"] = "none", 0
    L = looks(s)
    def lit(c, r, reason, st=None, detail=None):
        out = {"reason": reason, "show": "off" if c["kind"] == "off" else "colour"}
        if st: out["state"] = st
        if detail: out["detail"] = detail
        if out["show"] == "colour": out["colour"], out["rhythm"] = c, r
        return out
    def sub(group, key, st):
        c = L[group].get(key, colour("same"))
        return L[st]["colour"] if c["kind"] == "same" else c
    p = sim["printer"]; d = sim["detail"]
    if state["tool"] == "test": o = lit(state["toolColour"], state["toolRhythm"], "tool_test")
    elif state["tool"] == "cycle": o = {"reason": "tool_cycle", "show": "cycle"}
    elif state["tool"] == "wifi": o = {"reason": "tool_wifi", "show": "wifi", "colour": colour("custom", "#ffff00")}
    elif s["mode"] == "off": o = {"reason": "mode_off", "show": "off"}
    elif s["mode"] == "on": o = lit(L["printing"]["colour"], "steady", "mode_on", "printing")
    elif sim["accessRejected"]: o = lit(colour("custom", "#800000"), "steady", "access_rejected")
    elif sim["certRejected"]: o = lit(colour("custom", "#800000"), "steady", "cert_rejected")
    elif not sim["connected"]: o = {"reason": "unreachable", "show": "off"}
    elif p == "error": o = lit(sub("err", d or "runout", "error"), L["error"]["rhythm"], "error", "error", d or "runout")
    elif p == "attention": o = lit(sub("attn", d or "pause", "attention"), L["attention"]["rhythm"], "attention", "attention", d or "pause")
    elif p == "finished": o = lit(L["finished"]["colour"], L["finished"]["rhythm"], "finished", "finished"); o["finishEndsIn"] = 420
    elif p == "preparing": o = lit(sub("prep", d or "level", "preparing"), L["preparing"]["rhythm"], "preparing", "preparing", d or "level")
    elif s["followLight"] and not sim["light"]: o = {"reason": "light_off", "show": "off"}
    elif p == "printing": o = lit(L["printing"]["colour"], L["printing"]["rhythm"], "printing", "printing")
    elif p == "idle_off": o = {"reason": "idle_off", "show": "off", "idleFor": 900}
    else: o = lit(L["idle"]["colour"], L["idle"]["rhythm"], "idle", "idle"); o["idleOffIn"] = max(0, s["idleOffMin"] * 60 - sim["idleFor"])
    if state["check"]: o["check"] = state["check"]
    o.update({"outputs": [255 if state["check"] == x else 0 for x in ["R", "G", "B", "WW", "CW"]] if state["check"] else [0, 0, 0, 255, 255],
              "brightness": s["brightness"], "mode": s["mode"], "tool": state["tool"], "rssi": sim["rssi"], "firmware": "mock",
              "printer": {"connected": sim["connected"], "accessRejected": sim["accessRejected"], "certRejected": sim["certRejected"], "gcodeState": "IDLE", "stage": -1,
                          "door": False, "light": sim["light"], "detectedModel": sim["detectedModel"],
                          "model": s["model"] if s["model"] != "auto" else sim["detectedModel"], "hasLidar": True}})
    if state["toolEnds"]: o["preview"] = True
    return o

class H(http.server.BaseHTTPRequestHandler):
    def _send(self, code, body, ctype="application/json"):
        b = body.encode() if isinstance(body, str) else body
        self.send_response(code); self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(b))); self.end_headers(); self.wfile.write(b)
    def log_message(self, *a): pass
    def do_GET(self):
        p = urllib.parse.urlparse(self.path).path
        if p in ("/", "/wifi"):
            return self._send(200, open(f"{WWW}/wifiSetup.html", "rb").read(), "text/html")
        q = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query)
        if time.time() < state["downUntil"] and p.startswith("/api/"):   # restarting
            self.close_connection = True; return
        if p == "/api/settings":
            with lock: return self._send(200, json.dumps(state["settings"]))
        if p == "/api/status":
            with lock: return self._send(200, json.dumps(status()))
        if p == "/api/printers":
            cur = state["conn"]["printerSerial"]
            return self._send(200, json.dumps({"printers": [dict(x, current=x["serial"] == cur) for x in PRINTERS],
                                               "followingSerial": cur, "followingIp": state["conn"]["printerIP"]}))
        if p == "/api/lookup":
            # Like api::lookup: 202 while the "MQTT task" connects, then the answer. 192.0.2.99
            # answers with a certificate that isn't Bambu Lab's.
            ip = (q.get("ip") or [""])[0].strip()
            if not re.fullmatch(r"(25[0-5]|2[0-4]\d|1?\d?\d)(\.(25[0-5]|2[0-4]\d|1?\d?\d)){3}", ip): return self._send(400, '{"error":"ip"}')
            started = state["lookups"].setdefault(ip, time.time())
            if time.time() - started < 1.5: return self._send(202, '{"looking":true}')
            hit = next((x for x in PRINTERS if x["ip"] == ip), None)
            if hit: return self._send(200, json.dumps({"answered": True, "trusted": True, "serial": hit["serial"]}))
            return self._send(200, json.dumps({"answered": ip == "192.0.2.99", "trusted": False} | ({"serial": "FAKE0001"} if ip == "192.0.2.99" else {})))
        if p == "/factoryreset":
            return self._send(200, "Factory reset complete. Restarting...", "text/plain")
        if p == "/setup":
            return self._send(200, open(f"{WWW}/setupPage.html", "rb").read(), "text/html")
        if p == "/fwupdate":
            return self._send(200, open(f"{WWW}/updatePage.html", "rb").read(), "text/html")
        if p == "/style.css":
            return self._send(200, open(f"{WWW}/style.css", "rb").read(), "text/css")
        if p == "/config.json":
            return self._send(200, json.dumps(dict(state["conn"], hasPass=True, host="BLLED", webUser="", hasWebPass=False, isAPMode=False)))
        if p == "/wifiScan":
            state["scan_calls"] += 1
            if state["scan_calls"] == 1:  # radio busy (e.g. WiFi reconnect attempt)
                return self._send(503, '{"retry":true}')
            if state["scan_calls"] <= 3:
                return self._send(202, '{"scanning":true}')
            return self._send(200, json.dumps({"networks": [
                {"ssid": "HomeNet", "bssid": "02:00:00:00:00:01", "rssi": -48, "enc": True},
                {"ssid": "<img src=x onerror=\"window.__xss=1\">", "bssid": "02:00:00:00:00:02", "rssi": -70, "enc": False}]}))
        if p == "/printerList":
            return self._send(200, "[]")
        if p == "/_state":
            return self._send(200, json.dumps(state))
        return self._send(404, "not found", "text/plain")
    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0)); body = self.rfile.read(n)
        p = urllib.parse.urlparse(self.path).path
        if p == "/submitWiFi":
            state["submits"].append(urllib.parse.parse_qs(body.decode(), keep_blank_values=True))
            return self._send(200, "Settings saved, restarting...", "text/plain")
        if p.startswith("/api/"):
            try: j = json.loads(body or b"{}")
            except ValueError: return self._send(400, '{"error":"bad json"}')
            with lock:
                state["posts"].append([p, j])
                if p == "/api/settings":
                    if not isinstance(j, dict): return self._send(400, '{"error":"expected a JSON object"}')
                    merge(state["settings"], j); validate(state["settings"])
                    return self._send(200, json.dumps(state["settings"]))
                if p == "/api/tool":
                    t = j.get("tool")
                    if t not in ("none", "test", "cycle", "wifi"): return self._send(400, '{"error":"unknown tool"}')
                    state["tool"] = t; state["toolColour"] = j.get("colour") or colour("custom", "#ff0000")
                    state["toolRhythm"] = j.get("rhythm") or "steady"
                    secs = max(0, min(60, int(j.get("seconds") or 0)))
                    state["toolEnds"] = time.time() + secs if secs else 0
                    return self._send(200, json.dumps({"tool": t}))
                if p == "/api/check":
                    o = j.get("output")
                    if o is not None and o not in ("R", "G", "B", "WW", "CW"): return self._send(400, '{"error":"unknown output"}')
                    state["check"] = o
                    return self._send(200, json.dumps({"check": o} if o else {}))
                if p == "/api/printer":
                    import re
                    ip, serial, code = (j.get("ip") or "").strip(), (j.get("serial") or "").strip(), (j.get("accessCode") or "").strip()
                    if not re.fullmatch(r"(25[0-5]|2[0-4]\d|1?\d?\d)(\.(25[0-5]|2[0-4]\d|1?\d?\d)){3}", ip): return self._send(400, '{"error":"ip"}')
                    if not re.fullmatch(r"[A-Za-z0-9]{1,15}", serial): return self._send(400, '{"error":"serial"}')
                    if (code and len(code) != 8) or (not code and not state["conn"]["hasAccessCode"]): return self._send(400, '{"error":"accessCode"}')
                    c = state["conn"]; changed = ip != c["printerIP"] or serial != c["printerSerial"] or bool(code)
                    c.update(printerIP=ip, printerSerial=serial, hasAccessCode=True)
                    if changed: state["downUntil"] = time.time() + 6   # restarting
                    return self._send(200, json.dumps({"restart": changed}))
                if p == "/api/wifi-rescan":
                    return self._send(200, "{}")
        if p == "/_sim":
            j = json.loads(body or b"{}")
            with lock:
                for k in ("tool", "check"):
                    if k in j: state[k] = j.pop(k)
                state["sim"].update(j)
            return self._send(200, json.dumps(state["sim"]))
        if p == "/update":
            return self._send(500, "Update failed - see the Web Serial log. The current firmware is still installed.", "text/plain")
        return self._send(404, "", "text/plain")

http.server.ThreadingHTTPServer(("127.0.0.1", PORT), H).serve_forever()
