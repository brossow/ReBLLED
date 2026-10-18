#pragma once
#include "WiFi.h"
#include <mbedtls/x509_crt.h>
// tlsAnswers: something accepts the TLS connection. certOk: its certificate verifies, issued to
// TESTSERIAL0001 (see the mbedtls stand-in). dropsAfterHandshake: it closes straight after.
namespace fake {
inline bool tlsAnswers = true, dropsAfterHandshake = false;
inline int tlsConnects = 0, socketCloses = 0;
inline mbedtls_x509_crt someCert = [] {
  mbedtls_x509_crt c; c.raw.len = 1;
  c.subject.oid.len = 3; c.subject.oid.p = (unsigned char *)"\x55\x04\x03";
  c.subject.val.len = 14; c.subject.val.p = (unsigned char *)"TESTSERIAL0001";
  return c; }();
}
class WiFiClientSecure {
protected:
  struct Ctx { int socket = -1; } ctx;
  Ctx *sslclient = &ctx;
  bool _connected = false;
public:
  virtual ~WiFiClientSecure() { stop(); }
  void setInsecure() {}
  void setTimeout(int) {}
  void setHandshakeTimeout(unsigned long) {}
  virtual int connect(IPAddress, uint16_t) { return open(); }
  virtual int connect(const char *, uint16_t) { return open(); }
  // Like core 2.0.17: closes the socket if it's >= 0, then zeroes the context (socket reads 0).
  virtual void stop() { if (sslclient->socket >= 0) fake::socketCloses++; _connected = false; sslclient->socket = 0; }
  bool connected() { return _connected; }
  const mbedtls_x509_crt *getPeerCertificate() { return &fake::someCert; }
private:
  int open() { fake::tlsConnects++; if (!fake::tlsAnswers) return 0; sslclient->socket = 3; _connected = !fake::dropsAfterHandshake; return 1; }
};
