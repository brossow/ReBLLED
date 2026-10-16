#ifndef LOGSERIAL_H
#define LOGSERIAL_H

#include <Arduino.h>
#include <MycilaWebSerial.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class AsyncWebServer;

class LogSerialClass : public Stream {
private:
WebSerial webSerial;
// MycilaWebSerial has no locking and appends to a shared buffer; the loop, MQTT and web
// server tasks all log, so writes are serialized here. Held only for the write itself.
SemaphoreHandle_t writeMutex = NULL;
void lock() {
  if (writeMutex == NULL) writeMutex = xSemaphoreCreateRecursiveMutex(); // first log is in setup()
  xSemaphoreTakeRecursive(writeMutex, portMAX_DELAY);
}
void unlock() { xSemaphoreGiveRecursive(writeMutex); }
public:
  void begin(unsigned long baud = 115200) {
    Serial.begin(baud);
  }

  void begin(AsyncWebServer* server, unsigned long baud = 115200, size_t bufferSize = 100) {
    Serial.begin(baud);
    webSerial.begin(server);
    webSerial.setBuffer(bufferSize);
    webSerial.onMessage([](const std::string& msg) {
      Serial.print("[WebSerial RX] ");
      Serial.println(msg.c_str());
    });
  }

  void onMessage(std::function<void(const std::string&)> cb) {
    webSerial.onMessage(cb);
  }

  // Must be called before begin(): protects the log WebSocket with the WebUI login.
  void setAuthentication(const char* user, const char* pass) {
    webSerial.setAuthentication(user, pass);
  }

  void setBuffer(size_t size) {
    webSerial.setBuffer(size);
  }

  int available() override { return Serial.available(); }
  int read() override { return Serial.read(); }
  int peek() override { return Serial.peek(); }
  void flush() override { Serial.flush(); }

  size_t write(uint8_t b) override {
    lock();
    Serial.write(b);
    webSerial.write(&b, 1);
    unlock();
    return 1;
  }

  size_t write(const uint8_t* buffer, size_t size) override {
    lock();
    Serial.write(buffer, size);
    webSerial.write(buffer, size);
    unlock();
    return size;
  }

  using Print::write;
  operator bool() { return (bool)Serial; }


};

LogSerialClass LogSerial;

#endif // LOGSERIAL_H
