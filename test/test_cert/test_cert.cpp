// The printer certificate check (printerCert.h): issued to the configured serial and signed by
// the authority, or no access code. Test certificates in test/fixtures/certs (README there).
#include <unity.h>
#include <fstream>
#include <sstream>
#include <string>
#if __has_include(<psa/crypto.h>)
#include <psa/crypto.h>
#endif
#include "../../src/blled/printerCert.h"

static std::string readFile(const char *p) { std::ifstream f(p); std::stringstream s; s << f.rdbuf(); return s.str(); }
static std::string fx(const char *name) { return readFile((std::string("test/fixtures/certs/") + name).c_str()); }

// A chain as the printer sends it: the leaf, then (optionally) the authority.
struct Chain {
  mbedtls_x509_crt crt;
  Chain(const std::string &leaf, const std::string &more = "") {
    mbedtls_x509_crt_init(&crt);
    TEST_ASSERT_EQUAL(0, mbedtls_x509_crt_parse(&crt, (const unsigned char *)leaf.c_str(), leaf.size() + 1));
    if (!more.empty()) TEST_ASSERT_EQUAL(0, mbedtls_x509_crt_parse(&crt, (const unsigned char *)more.c_str(), more.size() + 1));
  }
  ~Chain() { mbedtls_x509_crt_free(&crt); }
};
static std::string CA;

void setUp() {}
void tearDown() {}

void test_printer_signed_by_the_authority_is_trusted() {
  Chain c(fx("leaf.pem"), CA);                  // the printer sends the authority too
  bblcert::Result r = bblcert::check(&c.crt, CA.c_str());
  TEST_ASSERT_TRUE(r.trusted);
  TEST_ASSERT_EQUAL_STRING("TESTSERIAL0001", r.cn);
  TEST_ASSERT_TRUE(bblcert::isPrinter(r, "TESTSERIAL0001"));
}
void test_leaf_alone_is_enough() {
  Chain c(fx("leaf.pem"));
  TEST_ASSERT_TRUE(bblcert::check(&c.crt, CA.c_str()).trusted);
}
void test_a_different_serial_is_not_this_printer() {
  Chain c(fx("leaf.pem"), CA);
  bblcert::Result r = bblcert::check(&c.crt, CA.c_str());
  TEST_ASSERT_FALSE(bblcert::isPrinter(r, "TESTSERIAL0002"));
  TEST_ASSERT_FALSE(bblcert::isPrinter(r, ""));
}
void test_dates_are_ignored() {
  Chain c(fx("leaf_expired.pem"), CA);          // the controller has no clock
  TEST_ASSERT_TRUE(bblcert::check(&c.crt, CA.c_str()).trusted);
}
void test_another_authority_is_not_trusted() {
  Chain c(fx("leaf_other_ca.pem"), fx("other_ca.pem"));
  bblcert::Result r = bblcert::check(&c.crt, CA.c_str());
  TEST_ASSERT_FALSE(r.trusted);
  TEST_ASSERT_EQUAL_STRING("TESTSERIAL0001", r.cn);   // the name is still read (setup says it's not verified)
  TEST_ASSERT_FALSE(bblcert::isPrinter(r, "TESTSERIAL0001"));
}
void test_self_signed_with_the_right_name_is_not_trusted() {
  Chain c(fx("self_signed.pem"));
  TEST_ASSERT_FALSE(bblcert::isPrinter(bblcert::check(&c.crt, CA.c_str()), "TESTSERIAL0001"));
}
void test_sending_the_authority_itself_is_not_a_printer() {
  // The public CA certificate verifies (it's the authority), but it's issued to "BBL CA", never
  // to a serial (serials are letters and digits only). In a real connection the TLS handshake
  // also needs the private key of whatever certificate is presented, which only Bambu has.
  Chain c(CA);
  TEST_ASSERT_FALSE(bblcert::isPrinter(bblcert::check(&c.crt, CA.c_str()), "TESTSERIAL0001"));
}
void test_no_certificate() {
  TEST_ASSERT_FALSE(bblcert::check(nullptr, CA.c_str()).trusted);
  mbedtls_x509_crt empty; mbedtls_x509_crt_init(&empty);
  TEST_ASSERT_FALSE(bblcert::check(&empty, CA.c_str()).trusted);
}
void test_the_embedded_bambu_authority_parses() {
  mbedtls_x509_crt ca; mbedtls_x509_crt_init(&ca);
  TEST_ASSERT_EQUAL(0, mbedtls_x509_crt_parse(&ca, (const unsigned char *)bblcert::BBL_CA_PEM, sizeof bblcert::BBL_CA_PEM));
  char cn[40]; bblcert::subjectCN(&ca, cn, sizeof cn);
  TEST_ASSERT_EQUAL_STRING("BBL CA", cn);
  mbedtls_x509_crt_free(&ca);
  Chain c(fx("leaf.pem"), CA);                  // and the default authority isn't the test one
  TEST_ASSERT_FALSE(bblcert::check(&c.crt).trusted);
}

int main() {
#if __has_include(<psa/crypto.h>)
  psa_crypto_init();                            // mbedtls 3.x/4.x on the host; the ESP32 has 2.28
#endif
  CA = fx("test_ca.pem");
  UNITY_BEGIN();
  RUN_TEST(test_printer_signed_by_the_authority_is_trusted);
  RUN_TEST(test_leaf_alone_is_enough);
  RUN_TEST(test_a_different_serial_is_not_this_printer);
  RUN_TEST(test_dates_are_ignored);
  RUN_TEST(test_another_authority_is_not_trusted);
  RUN_TEST(test_self_signed_with_the_right_name_is_not_trusted);
  RUN_TEST(test_sending_the_authority_itself_is_not_a_printer);
  RUN_TEST(test_no_certificate);
  RUN_TEST(test_the_embedded_bambu_authority_parses);
  return UNITY_END();
}
