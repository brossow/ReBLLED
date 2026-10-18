#pragma once
#include "WiFi.h"
#include <mbedtls/x509_crt.h>
// tlsAnswers: whether something accepts the TLS connection. Its certificate never verifies here
// (the mbedtls stand-in can't), which is the case these tests need.
namespace fake { inline bool tlsAnswers = true; inline mbedtls_x509_crt someCert = [] { mbedtls_x509_crt c; c.raw.len = 1; return c; }(); }
class WiFiClientSecure {
public:
  void setInsecure() {}
  void setTimeout(int) {}
  int connect(IPAddress, uint16_t) { return fake::tlsAnswers ? 1 : 0; }
  void stop() {}
  const mbedtls_x509_crt *getPeerCertificate() { return &fake::someCert; }
};
