// Host-side stand-in for the parts of the Arduino core the firmware headers use, so the
// real LED/MQTT logic can be compiled and tested on a Mac (`pio test -e native`).
// Not a full Arduino implementation - only what src/blled/*.h touches.
#pragma once
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <algorithm>

typedef uint8_t byte;
#define HEX 16
#define DEC 10
#define PI 3.1415926535897932384626433832795
#define F(s) (s)
#define PROGMEM
#ifndef constrain
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#endif
#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#include <type_traits>
template <class A, class B> typename std::common_type<A, B>::type min(A a, B b) { return a < b ? a : b; }
template <class A, class B> typename std::common_type<A, B>::type max(A a, B b) { return a > b ? a : b; }

// ---- fake clock: delay() advances it, tests can set it ----
namespace fake { inline unsigned long now_ms = 0; }
inline unsigned long millis() { return fake::now_ms; }
inline void delay(unsigned long ms) { fake::now_ms += ms; }
inline long random(long max) { return max ? std::rand() % max : 0; }
inline long random(long min, long max) { return min + random(max - min); }

// ---- String ----
class String {
  std::string s;
public:
  String() {}
  String(const char *c) : s(c ? c : "") {}
  String(const std::string &x) : s(x) {}
  String(char c) : s(1, c) {}
  String(int v) : s(std::to_string(v)) {}
  String(unsigned int v) : s(std::to_string(v)) {}
  String(long v) : s(std::to_string(v)) {}
  String(unsigned long v) : s(std::to_string(v)) {}
  String(long v, int base) { char b[40]; snprintf(b, sizeof b, base == 16 ? "%lx" : "%ld", v); s = b; }
  String(int v, int base) : String((long)v, base) {}
  const char *c_str() const { return s.c_str(); }
  unsigned int length() const { return (unsigned)s.size(); }
  char charAt(unsigned i) const { return i < s.size() ? s[i] : 0; }
  void remove(unsigned i) { if (i < s.size()) s.erase(i); }
  void remove(unsigned i, unsigned n) { if (i < s.size()) s.erase(i, n); }
  int indexOf(const String &x, unsigned from = 0) const { auto p = s.find(x.s, from); return p == std::string::npos ? -1 : (int)p; }
  int indexOf(const char *x, unsigned from = 0) const { return indexOf(String(x), from); }
  void replace(const String &a, const String &b) {
    if (a.s.empty()) return; size_t p = 0;
    while ((p = s.find(a.s, p)) != std::string::npos) { s.replace(p, a.s.size(), b.s); p += b.s.size(); }
  }
  String substring(unsigned from) const { return from < s.size() ? String(s.substr(from)) : String(); }
  String substring(unsigned from, unsigned to) const { if (from >= s.size() || to <= from) return String(); return String(s.substr(from, to - from)); }
  void trim() { auto b = s.find_first_not_of(" \t\r\n"); auto e = s.find_last_not_of(" \t\r\n"); s = b == std::string::npos ? "" : s.substr(b, e - b + 1); }
  long toInt() const { return std::strtol(s.c_str(), nullptr, 10); }
  void toUpperCase() { for (auto &c : s) c = (char)toupper((unsigned char)c); }
  String &operator+=(const String &o) { s += o.s; return *this; }
  String &operator+=(const char *o) { s += o; return *this; }
  String &operator+=(char c) { s += c; return *this; }
  bool operator==(const String &o) const { return s == o.s; }
  bool operator==(const char *o) const { return s == (o ? o : ""); }
  bool operator!=(const String &o) const { return s != o.s; }
  bool operator!=(const char *o) const { return !(*this == o); }
  friend String operator+(const String &a, const String &b) { return String(a.s + b.s); }
  friend String operator+(const String &a, const char *b) { return String(a.s + b); }
  friend String operator+(const char *a, const String &b) { return String(std::string(a) + b.s); }
  // ArduinoJson uses these to read/write Arduino Strings
  bool concat(const char *c, unsigned n) { s.append(c, n); return true; }
  bool concat(char c) { s += c; return true; }
  bool concat(const char *c) { s += c; return true; }
  unsigned char reserve(unsigned n) { s.reserve(n); return 1; }
  operator std::string() const { return s; }
};

// ---- Print / Stream (logging goes to a capture buffer tests can inspect) ----
class Print {
public:
  virtual ~Print() {}
  virtual size_t write(uint8_t c) = 0;
  virtual size_t write(const uint8_t *b, size_t n) { for (size_t i = 0; i < n; i++) write(b[i]); return n; }
  size_t write(const char *s) { return write((const uint8_t *)s, strlen(s)); }
  size_t print(const char *s) { return write(s); }
  size_t print(const String &s) { return write(s.c_str()); }
  size_t print(char c) { return write((uint8_t)c); }
  size_t print(int v) { return printf("%d", v); }
  size_t print(unsigned v) { return printf("%u", v); }
  size_t print(long v) { return printf("%ld", v); }
  size_t print(unsigned long v) { return printf("%lu", v); }
  size_t print(double v) { return printf("%.2f", v); }
  size_t print(long v, int base) { return base == 16 ? printf("%lX", v) : print(v); }
  template <class T> size_t println(const T &v) { size_t n = print(v); return n + write("\n"); }
  size_t println() { return write("\n"); }
  size_t printf(const char *fmt, ...) __attribute__((format(printf, 2, 3))) {
    char buf[512]; va_list ap; va_start(ap, fmt); int n = vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    write((const uint8_t *)buf, strlen(buf)); return n > 0 ? n : 0;
  }
};
class Stream : public Print {
public:
  virtual int available() = 0;
  virtual int read() = 0;
  virtual int peek() = 0;
  virtual void flush() {}
};

class CaptureSerial : public Stream {
public:
  std::string out;
  size_t write(uint8_t c) override { out += (char)c; return 1; }
  using Print::write;
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void begin(unsigned long) {}
  operator bool() const { return true; }
};
inline CaptureSerial Serial;

// ---- LEDC (PWM) - records the last duty written per channel ----
namespace fake { inline uint32_t duty[16] = {0}; inline int writes = 0; }
inline void ledcSetup(int, int, int) {}
inline void ledcAttachPin(int, int) {}
inline void ledcWrite(int ch, uint32_t d) { if (ch >= 0 && ch < 16) fake::duty[ch] = d; fake::writes++; }

// ---- ESP chip object ----
namespace fake { inline int restarts = 0; }
struct FakeESP { uint32_t getFreeHeap() { return 200000; } void restart() { fake::restarts++; } };
inline FakeESP ESP;
