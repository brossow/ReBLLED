#ifndef _BLLED_PRINTER_CERT
#define _BLLED_PRINTER_CERT

// Is the thing at the printer's address really the printer? (docs/REDESIGN.md, stage 5)
// A Bambu Lab printer's MQTT certificate is issued to its serial number and signed by Bambu
// Lab's own authority, "BBL CA" (seen on an X1 Carbon; valid 2022-2032). The controller checks
// both before it sends the access code, so a device answering at the printer's address (or a
// spoofed discovery message) can't collect it. The same check reads the serial for setup's
// "Look up". Plain mbedtls, so it builds on the ESP32 (2.28) and in host tests.

#include <string.h>
#include <mbedtls/x509_crt.h>

namespace bblcert
{
    // Bambu Lab's public certificate authority (public, not a secret).
    static const char BBL_CA_PEM[] = R"(-----BEGIN CERTIFICATE-----
MIIDZTCCAk2gAwIBAgIUV1FckwXElyek1onFnQ9kL7Bk4N8wDQYJKoZIhvcNAQEL
BQAwQjELMAkGA1UEBhMCQ04xIjAgBgNVBAoMGUJCTCBUZWNobm9sb2dpZXMgQ28u
LCBMdGQxDzANBgNVBAMMBkJCTCBDQTAeFw0yMjA0MDQwMzQyMTFaFw0zMjA0MDEw
MzQyMTFaMEIxCzAJBgNVBAYTAkNOMSIwIAYDVQQKDBlCQkwgVGVjaG5vbG9naWVz
IENvLiwgTHRkMQ8wDQYDVQQDDAZCQkwgQ0EwggEiMA0GCSqGSIb3DQEBAQUAA4IB
DwAwggEKAoIBAQDL3pnDdxGOk5Z6vugiT4dpM0ju+3Xatxz09UY7mbj4tkIdby4H
oeEdiYSZjc5LJngJuCHwtEbBJt1BriRdSVrF6M9D2UaBDyamEo0dxwSaVxZiDVWC
eeCPdELpFZdEhSNTaT4O7zgvcnFsfHMa/0vMAkvE7i0qp3mjEzYLfz60axcDoJLk
p7n6xKXI+cJbA4IlToFjpSldPmC+ynOo7YAOsXt7AYKY6Glz0BwUVzSJxU+/+VFy
/QrmYGNwlrQtdREHeRi0SNK32x1+bOndfJP0sojuIrDjKsdCLye5CSZIvqnbowwW
1jRwZgTBR29Zp2nzCoxJYcU9TSQp/4KZuWNVAgMBAAGjUzBRMB0GA1UdDgQWBBSP
NEJo3GdOj8QinsV8SeWr3US+HjAfBgNVHSMEGDAWgBSPNEJo3GdOj8QinsV8SeWr
3US+HjAPBgNVHRMBAf8EBTADAQH/MA0GCSqGSIb3DQEBCwUAA4IBAQABlBIT5ZeG
fgcK1LOh1CN9sTzxMCLbtTPFF1NGGA13mApu6j1h5YELbSKcUqfXzMnVeAb06Htu
3CoCoe+wj7LONTFO++vBm2/if6Jt/DUw1CAEcNyqeh6ES0NX8LJRVSe0qdTxPJuA
BdOoo96iX89rRPoxeed1cpq5hZwbeka3+CJGV76itWp35Up5rmmUqrlyQOr/Wax6
itosIzG0MfhgUzU51A2P/hSnD3NDMXv+wUY/AvqgIL7u7fbDKnku1GzEKIkfH8hm
Rs6d8SCU89xyrwzQ0PR853irHas3WrHVqab3P+qNwR0YirL0Qk7Xt/q3O1griNg2
Blbjg3obpHo9
-----END CERTIFICATE-----
)";

    struct Result
    {
        bool trusted = false;   // chains to the authority (dates ignored: the controller has no clock)
        char cn[40] = "";       // the name it was issued to: the printer's serial
    };

    // Common name (OID 2.5.4.3) of a certificate's subject.
    inline void subjectCN(const mbedtls_x509_crt *c, char *out, size_t n)
    {
        out[0] = 0;
        for (const mbedtls_x509_name *nm = &c->subject; nm; nm = nm->next)
            if (nm->oid.len == 3 && memcmp(nm->oid.p, "\x55\x04\x03", 3) == 0)
            {
                size_t l = nm->val.len < n - 1 ? nm->val.len : n - 1;
                memcpy(out, nm->val.p, l);
                out[l] = 0;
                return;
            }
    }

    // `peer` is the chain the printer sent (leaf first), as WiFiClientSecure::getPeerCertificate()
    // returns it.
    inline Result check(const mbedtls_x509_crt *peer, const char *caPem = BBL_CA_PEM)
    {
        Result r;
        if (!peer || !peer->raw.len) return r;
        subjectCN(peer, r.cn, sizeof r.cn);
        mbedtls_x509_crt ca;
        mbedtls_x509_crt_init(&ca);
        if (mbedtls_x509_crt_parse(&ca, (const unsigned char *)caPem, strlen(caPem) + 1) == 0)
        {
            uint32_t flags = 0;
            int rc = mbedtls_x509_crt_verify(const_cast<mbedtls_x509_crt *>(peer), &ca, NULL, NULL, &flags, NULL, NULL);
            flags &= ~(uint32_t)(MBEDTLS_X509_BADCERT_EXPIRED | MBEDTLS_X509_BADCERT_FUTURE);
            r.trusted = (rc == 0 || rc == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED) && flags == 0;
        }
        mbedtls_x509_crt_free(&ca);
        return r;
    }

    // Safe to send the access code to it?
    inline bool isPrinter(const Result &r, const char *serial)
    {
        return r.trusted && serial && serial[0] && strcmp(r.cn, serial) == 0;
    }
}

#endif
