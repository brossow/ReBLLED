# Measures how regularly the device pushes its 1-second status over /ws. Gaps of ~6 s mean
# the main loop is freezing (2.3.0: printer discovery blocked ~5.9 s every 10 s).
# Usage: python3 tools/ws_timing.py <device-ip> 60
import base64, json, os, socket, struct, sys, time
host, seconds = sys.argv[1], float(sys.argv[2])
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
times, last = [], None
end = time.time() + seconds
while time.time() < end:
    try:
        b1, b2 = recvn(2)
    except socket.timeout:
        continue
    ln = b2 & 0x7F
    if ln == 126: ln = struct.unpack(">H", recvn(2))[0]
    elif ln == 127: ln = struct.unpack(">Q", recvn(8))[0]
    payload = recvn(ln)
    if b1 & 0x0F == 1:
        times.append(time.time()); last = json.loads(payload)
gaps = [round(b - a, 2) for a, b in zip(times, times[1:])]
print(json.dumps({"messages": len(times), "max_gap_s": max(gaps) if gaps else None,
                  "gaps_over_2s": sum(g > 2 for g in gaps), "gaps": gaps, "last": last}))
