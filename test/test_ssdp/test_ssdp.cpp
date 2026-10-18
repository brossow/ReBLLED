// Reading the printer's discovery message (printerModels.h).
#include <unity.h>
#include "../../src/blled/printerModels.h"

// Shaped like an X1 Carbon's NOTIFY (invented serial, documentation address).
static const char *NOTIFY =
  "NOTIFY * HTTP/1.1\r\nHOST: 239.255.255.250:1990\r\nServer: UPnP/1.0\r\nLocation: 192.0.2.10\r\n"
  "NT: urn:bambulab-com:device:3dprinter:1\r\nUSN: 01S00A0000X4821\r\nCache-Control: max-age=1800\r\n"
  "DevModel.bambu.com: 3DPrinter-X1-Carbon\r\nDevName.bambu.com: Workshop X1C  \r\nDevSignal.bambu.com: -44\r\n"
  "DevConnect.bambu.com: lan\r\nDevBind.bambu.com: free\r\nDevVersion.bambu.com: 01.08.02.00\r\n\r\n";

void setUp() {}
void tearDown() {}

void test_reads_serial_model_and_name() {
  char v[40];
  TEST_ASSERT_TRUE(bblssdp::header(NOTIFY, "USN", v, sizeof v));
  TEST_ASSERT_EQUAL_STRING("01S00A0000X4821", v);
  TEST_ASSERT_TRUE(bblssdp::header(NOTIFY, "DevModel.bambu.com", v, sizeof v));
  TEST_ASSERT_EQUAL_STRING("3DPrinter-X1-Carbon", v);
  TEST_ASSERT_TRUE(bblssdp::header(NOTIFY, "DevName.bambu.com", v, sizeof v));
  TEST_ASSERT_EQUAL_STRING("Workshop X1C", v);              // trailing spaces trimmed
}
void test_header_names_are_case_insensitive_and_whole() {
  char v[40];
  TEST_ASSERT_TRUE(bblssdp::header(NOTIFY, "devmodel.BAMBU.com", v, sizeof v));
  TEST_ASSERT_FALSE(bblssdp::header(NOTIFY, "DevModel", v, sizeof v));   // a prefix isn't a match
  TEST_ASSERT_FALSE(bblssdp::header(NOTIFY, "Missing", v, sizeof v));
  TEST_ASSERT_EQUAL_STRING("", v);
}
void test_long_values_are_cut_without_splitting_a_character() {
  const char *msg = "DevName.bambu.com: ab\xC3\xA9\xC3\xA9\r\n";   // "abéé"
  char v[5];
  bblssdp::header(msg, "DevName.bambu.com", v, sizeof v);
  TEST_ASSERT_EQUAL_STRING("ab\xC3\xA9", v);               // fits exactly
  char w[4];
  bblssdp::header(msg, "DevName.bambu.com", w, sizeof w);
  TEST_ASSERT_EQUAL_STRING("ab", w);                        // not "ab\xC3" (half an é)
}
void test_last_line_without_a_line_break() {
  char v[16];
  TEST_ASSERT_TRUE(bblssdp::header("USN: ABC", "USN", v, sizeof v));
  TEST_ASSERT_EQUAL_STRING("ABC", v);
}
void test_model_codes() {
  char k[12];
  bblssdp::modelKey("BL-P001", k, sizeof k); TEST_ASSERT_EQUAL_STRING("X1C", k);              // what an X1 Carbon sends
  bblssdp::modelKey("3DPrinter-X1-Carbon", k, sizeof k); TEST_ASSERT_EQUAL_STRING("X1C", k);
  bblssdp::modelKey("3DPrinter-X1", k, sizeof k); TEST_ASSERT_EQUAL_STRING("X1", k);
  bblssdp::modelKey("C12", k, sizeof k); TEST_ASSERT_EQUAL_STRING("P1S", k);
  bblssdp::modelKey("N1", k, sizeof k); TEST_ASSERT_EQUAL_STRING("A1M", k);
  bblssdp::modelKey("Z9 <b>", k, sizeof k); TEST_ASSERT_EQUAL_STRING("Z9b", k);      // unknown: passed through, cleaned
  bblssdp::modelKey("A-very-long-model-code", k, sizeof k); TEST_ASSERT_EQUAL_STRING("A-very-long", k);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_reads_serial_model_and_name);
  RUN_TEST(test_header_names_are_case_insensitive_and_whole);
  RUN_TEST(test_long_values_are_cut_without_splitting_a_character);
  RUN_TEST(test_last_line_without_a_line_break);
  RUN_TEST(test_model_codes);
  return UNITY_END();
}
