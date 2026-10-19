# Logs printer stage (stg_cur) and door changes from the device's /ws status, with wall-clock
# times, so a print start can be lined up against what the camera shows. Read-only; no login.
# Usage: python3 tools/stage_log.py <device-ip> [minutes]   (Ctrl-C to stop)
import base64, json, os, socket, struct, sys, time
NAMES = {-1: "idle", 0: "printing", 1: "bed levelling", 2: "preheating bed", 8: "calibrating extrusion",
         9: "scanning bed surface", 10: "first-layer inspection", 12: "calibrating lidar",
         13: "homing", 14: "cleaning nozzle", 255: "idle"}
host = sys.argv[1]
end = time.time() + 60 * float(sys.argv[2]) if len(sys.argv) > 2 else float("inf")
s = socket.create_connection((host, 80), timeout=15)
key = base64.b64encode(os.urandom(16)).decode()
s.sendall((f"GET /ws HTTP/1.1\r\nHost: {host}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
           f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n").encode())
hdr = b""
while b"\r\n\r\n" not in hdr:
    hdr += s.recv(1)
assert b" 101 " in hdr.split(b"\r\n")[0], hdr[:80]
def recvn(n):
    b = b""
    while len(b) < n:
        c = s.recv(n - len(b))
        if not c: raise EOFError
        b += c
    return b
last = None
while time.time() < end:
    try:
        b1, b2 = recvn(2)
    except socket.timeout:
        continue
    ln = b2 & 0x7F
    if ln == 126: ln = struct.unpack(">H", recvn(2))[0]
    elif ln == 127: ln = struct.unpack(">Q", recvn(8))[0]
    payload = recvn(ln)
    if b1 & 0x0F != 1:
        continue
    d = json.loads(payload)
    now = (d.get("stg_cur"), d.get("doorOpen"), d.get("printerConnection"))
    if now != last:
        stg = d.get("stg_cur")
        print(f"{time.strftime('%H:%M:%S')}  stage {stg:>3} {NAMES.get(stg, '?'):<24}"
              f" door {'open' if d.get('doorOpen') else 'closed'}"
              f"{'' if d.get('printerConnection') else '  PRINTER OFFLINE'}", flush=True)
        last = now
