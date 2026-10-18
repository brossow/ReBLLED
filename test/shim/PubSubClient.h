#pragma once
// Records what the firmware publishes; never connects anywhere.
#include "Arduino.h"
#include "WiFiClientSecure.h"
#include <functional>
#include <vector>
struct FakePublish { std::string topic, payload; };
namespace fake { inline std::vector<FakePublish> published; inline bool mqttConnected = true; inline int mqttLogins = 0; }
class PubSubClient {
public:
  typedef void (*Callback)(char *, uint8_t *, unsigned int);
  explicit PubSubClient(WiFiClientSecure &) {}
  bool connect(const char *, const char *, const char *) { fake::mqttLogins++; return fake::mqttConnected; }  // sends the access code
  bool connected() { return fake::mqttConnected; }
  bool loop() { return true; }
  bool subscribe(const char *) { return true; }
  bool publish(const char *t, const char *p) { fake::published.push_back({t, p}); return true; }
  PubSubClient &setServer(const char *, uint16_t) { return *this; }
  PubSubClient &setCallback(Callback) { return *this; }
  PubSubClient &setStream(Stream &) { return *this; }
  bool setBufferSize(uint16_t) { return true; }
  PubSubClient &setSocketTimeout(uint16_t) { return *this; }
  int state() { return 0; }
};
