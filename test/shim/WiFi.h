#pragma once
#include "Arduino.h"
typedef enum { WL_NO_SHIELD = 255, WL_IDLE_STATUS = 0, WL_NO_SSID_AVAIL = 1, WL_SCAN_COMPLETED = 2,
               WL_CONNECTED = 3, WL_CONNECT_FAILED = 4, WL_CONNECTION_LOST = 5, WL_DISCONNECTED = 6 } wl_status_t;
#define WIFI_MODE_STA 1
#define WIFI_AP 2
struct FakeWiFi {
  wl_status_t st = WL_CONNECTED; long rssi = -50; int mode = WIFI_MODE_STA;
  wl_status_t status() { return st; }
  long RSSI() { return rssi; }
  int getMode() { return mode; }
};
inline FakeWiFi WiFi;
// Like the ESP32 core's IPAddress::fromString: four dot-separated numbers, each 0-255.
struct IPAddress {
  uint8_t b[4] = {0, 0, 0, 0};
  bool fromString(const String &str) {
    const char *p = str.c_str(); int part = 0, v = -1;
    for (;; p++) {
      if (*p >= '0' && *p <= '9') { v = (v < 0 ? 0 : v) * 10 + (*p - '0'); if (v > 255) return false; }
      else if (*p == '.' || *p == 0) { if (v < 0 || part > 3) return false; b[part++] = (uint8_t)v; v = -1; if (!*p) break; }
      else return false;
    }
    return part == 4;
  }
};
