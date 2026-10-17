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
