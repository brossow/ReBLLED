#pragma once
// Stand-in for mbedtls in the host tests that don't test certificates themselves (the MQTT
// code includes printerCert.h). fake::certOk decides whether a chain verifies. The real check
// is tested against mbedtls in test_cert (pio test -e native_tls).
#include <stddef.h>
#include <stdint.h>
struct mbedtls_x509_buf { int tag = 0; size_t len = 0; unsigned char *p = nullptr; };
struct mbedtls_x509_name { mbedtls_x509_buf oid, val; mbedtls_x509_name *next = nullptr; };
struct mbedtls_x509_crt { mbedtls_x509_buf raw; mbedtls_x509_name subject; };
namespace fake { inline bool certOk = false; }
#define MBEDTLS_X509_BADCERT_EXPIRED 0x01
#define MBEDTLS_X509_BADCERT_FUTURE 0x0200
#define MBEDTLS_ERR_X509_CERT_VERIFY_FAILED -0x2700
inline void mbedtls_x509_crt_init(mbedtls_x509_crt *) {}
inline void mbedtls_x509_crt_free(mbedtls_x509_crt *) {}
inline int mbedtls_x509_crt_parse(mbedtls_x509_crt *, const unsigned char *, size_t) { return 0; }
inline int mbedtls_x509_crt_verify(mbedtls_x509_crt *, mbedtls_x509_crt *, void *, const char *, uint32_t *flags, void *, void *) {
  *flags = fake::certOk ? 0 : 0x08; return fake::certOk ? 0 : MBEDTLS_ERR_X509_CERT_VERIFY_FAILED; }
