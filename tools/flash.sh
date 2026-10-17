#!/bin/bash
# Over-the-air update of a BLLED controller from this Mac.
#
#   tools/flash.sh <device-ip-or-host> [firmware.bin.ota]
#
# Uploads the newest .firmware/*.bin.ota (or the file given) to the device's /update
# endpoint, then waits for it to come back and reports the version it runs.
#
# WebUI login: read from the macOS Keychain, never from the command line or a file in the
# repo. Store it once (you'll be asked for the password; it isn't echoed):
#
#   security add-generic-password -s blled-webui -a <webui-username> -w
#
# If there is no Keychain item, the upload is sent without a login (devices without one).
# The password goes to curl on stdin, so it doesn't show up in `ps`.
set -euo pipefail

HOST="${1:-${BLLED_HOST:-}}"
if [ -z "$HOST" ]; then
  echo "usage: tools/flash.sh <device-ip-or-host> [firmware.bin.ota]   (or set BLLED_HOST)" >&2
  exit 2
fi
cd "$(dirname "$0")/.."

FW="${2:-$(ls -t .firmware/*.bin.ota 2>/dev/null | head -1 || true)}"
if [ -z "$FW" ] || [ ! -f "$FW" ]; then
  echo "No firmware found. Build first: pio run -e esp32dev" >&2
  exit 1
fi
if [ "$(head -c1 "$FW" | xxd -p)" != "e9" ]; then
  echo "$FW doesn't look like an ESP32 app image (first byte isn't 0xE9). Refusing." >&2
  exit 1
fi

AUTH=""
if USER_NAME=$(security find-generic-password -s blled-webui 2>/dev/null | sed -n 's/.*"acct"<blob>="\(.*\)"/\1/p') && [ -n "$USER_NAME" ]; then
  PASS=$(security find-generic-password -s blled-webui -w)
  # curl config strings treat \ and " as escapes inside quotes
  CRED="${USER_NAME}:${PASS}"
  CRED="${CRED//\\/\\\\}"
  CRED="${CRED//\"/\\\"}"
  AUTH="user = \"${CRED}\""
  unset PASS CRED
  echo "Using the WebUI login for '${USER_NAME}' from the Keychain."
else
  echo "No 'blled-webui' Keychain item: uploading without a login."
fi

curl_auth() { printf '%s\n' "$AUTH" | curl -K - "$@"; }

before=$(curl_auth -s --max-time 5 "http://$HOST/getConfig" | python3 -c 'import json,sys; print(json.load(sys.stdin).get("firmwareversion","?"))' 2>/dev/null || echo "?")
echo "Device $HOST runs: $before"
echo "Uploading $FW ($(stat -f %z "$FW") bytes)..."

code=$(curl_auth -s -o /tmp/blled-flash.$$ -w '%{http_code}' --max-time 180 \
  -F "firmware=@${FW};filename=$(basename "$FW")" "http://$HOST/update" || echo 000)
body=$(cat /tmp/blled-flash.$$ 2>/dev/null || true); rm -f /tmp/blled-flash.$$
if [ "$code" != "200" ]; then
  echo "Upload refused or failed (HTTP $code): $body" >&2
  echo "The device keeps running its current firmware." >&2
  exit 1
fi
echo "Upload accepted. Waiting for the device to restart..."

sleep 5
for _ in $(seq 1 45); do
  after=$(curl_auth -s --max-time 3 "http://$HOST/getConfig" | python3 -c 'import json,sys; print(json.load(sys.stdin).get("firmwareversion",""))' 2>/dev/null || true)
  if [ -n "$after" ]; then
    echo "Device $HOST is back, running: $after (was $before)"
    exit 0
  fi
  sleep 2
done
echo "Device didn't answer within ~95 s. Check it, or see TESTING.md 'If something goes wrong'." >&2
exit 1
