# Mock of the BLLED device web API, to test the setup pages in a browser without the
# hardware. Usage: python3 tools/mock_device.py src/www 8765, then open http://localhost:8765
# (WiFi setup page) or http://localhost:8765/setup (main LED setup page)
import http.server, json, sys, urllib.parse
WWW = sys.argv[1]
PORT = int(sys.argv[2])
state = {"scan_calls": 0, "submits": [], "configSubmits": [], "brightness": [],
         "config": {"firmwareversion": "mock", "brightness": 100, "maintMode": False, "discoMode": False,
                    "replicateled": True, "showtestcolor": False, "debugwifi": False, "runningRGB": "#000000",
                    "runningWW": 255, "runningCW": 255, "testRGB": "#ffffff", "testWW": 255, "testCW": 255,
                    "finishindication": True, "finishColor": "#00ff00", "finishWW": 0, "finishCW": 0,
                    "finishExit": True, "finishExitTimer": False, "finishTimerMins": 10, "doorToggleSecs": 4,
                    "statusAnimations": True, "inactivityEnabled": True, "inactivityMins": 10,
                    "errordetection": True, "p1Printer": False, "doorSwitch": True, "host": "BLLED"}}

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
        if p == "/setup":
            return self._send(200, open(f"{WWW}/setupPage.html", "rb").read(), "text/html")
        if p == "/getConfig":
            return self._send(200, json.dumps(state["config"]))
        if p == "/fwupdate":
            return self._send(200, open(f"{WWW}/updatePage.html", "rb").read(), "text/html")
        if p == "/style.css":
            return self._send(200, open(f"{WWW}/style.css", "rb").read(), "text/css")
        if p == "/config.json":
            return self._send(200, json.dumps({"ssid": "HomeNet", "hasPass": True, "host": "BLLED",
                "printerIP": "192.0.2.10", "printerSerial": "SERIAL123", "hasAccessCode": True,
                "webUser": "", "hasWebPass": False, "isAPMode": False}))
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
        if p == "/submitConfig":
            state["configSubmits"].append(urllib.parse.parse_qs(body.decode(), keep_blank_values=True))
            return self._send(200, "OK", "text/plain")
        if p == "/setBrightness":
            state["brightness"].append(urllib.parse.parse_qs(body.decode()))
            return self._send(200, "OK", "text/plain")
        if p == "/update":
            return self._send(500, "Update failed - see the Web Serial log. The current firmware is still installed.", "text/plain")
        return self._send(404, "", "text/plain")

http.server.ThreadingHTTPServer(("127.0.0.1", PORT), H).serve_forever()
