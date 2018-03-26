/*      -*- OpenSAF  -*-
 *
 * Copyright Ericsson AB 2018 - All Rights Reserved.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. This file and program are licensed
 * under the GNU Lesser General Public License Version 2.1, February 1999.
 * The complete license can be accessed from the following location:
 * http://opensource.org/licenses/lgpl-license.php
 * See the Copying file included with the OpenSAF distribution for full
 * licensing terms.
 *
 */

#include <cinttypes>
#include <cstdint>
#include <string>

#include "base/string_parse.h"
#include "gtest/gtest.h"

struct TestVector {
  const char* str;
  bool unsigned_success;
  uint64_t unsigned_value;
  bool signed_success;
  int64_t signed_value;
};

static constexpr int64_t kKilo = 1024;
static constexpr int64_t kMega = 1024 * 1024;
static constexpr int64_t kGiga = 1024 * 1024 * 1024;
static constexpr uint64_t kKiloU = kKilo;
static constexpr uint64_t kMegaU = kMega;
static constexpr uint64_t kGigaU = kGiga;

static const TestVector test_vectors[] = {
    {"0", true, 0, true, 0},
    {"1", true, 1, true, 1},
    {"-1", false, 0, true, -1},
    {"-0xf", false, 0, true, -15},
    {"-010", false, 0, true, -8},
    {"18446744073709551614", true, UINT64_MAX - 1, false, 0},
    {"18446744073709551615", true, UINT64_MAX, false, 0},
    {"18446744073709551616", false, 0, false, 0},
    {"9223372036854775806", true, INT64_MAX - 1, true, INT64_MAX - 1},
    {"9223372036854775807", true, INT64_MAX, true, INT64_MAX},
    {"9223372036854775808", true, 9223372036854775808ull, false, 0},
    {"-9223372036854775807", false, 0, true, INT64_MIN + 1},
    {"-9223372036854775808", false, 0, true, INT64_MIN},
    {"-9223372036854775809", false, 0, false, 0},
    {"0x7fffffffffffffff", true, 0x7fffffffffffffff, true, 0x7fffffffffffffff},
    {"0x8000000000000000", true, 0x8000000000000000, false, 0},
    {"0666", true, 0666, true, 0666},
    {"0xbeef", true, 0xbeef, true, 0xbeef},
    {"0XBEEF", true, 0xbeef, true, 0xbeef},
    {"0x3fffffffffffffk", true, 0xfffffffffffffc00ull, false, 0},
    {"0x40000000000000k", false, 0, false, 0},
    {"0x1fffffffffffffk", true, 0x7ffffffffffffc00ull, true,
     0x7ffffffffffffc00ull},
    {"0x20000000000000k", true, 0x8000000000000000ull, false, 0},
    {"-0x20000000000000k", false, 0, true, INT64_MIN},
    {"10k", true, 10 * kKiloU, true, 10 * kKilo},
    {"10K", false, 0, false, 0},
    {"-10k", false, 0, true, -10 * 1024},
    {"15M", true, 15 * kMegaU, true, 15 * kMega},
    {"-15M", false, 0, true, -15 * kMega},
    {" -15M ", false, 0, true, -15 * kMega},
    {" -15 M ", false, 0, false, 0},
    {"0x42G", true, 0x42 * kGigaU, true, 0x42 * kGiga},
    {"0x42H", false, 0, false, 0},
    {"0xffffffffffG", false, 0, false, 0},
    {"-1099511627775G", false, 0, false, 0},
    {"0xffffffffffM", true, 0xffffffffff * kMegaU, true, 0xffffffffff * kMega},
    {"", false, 0, false, 0},
    {" ", false, 0, false, 0},
    {".", false, 0, false, 0},
    {"0x", false, 0, false, 0},
    {"x", false, 0, false, 0},
    {"     ", false, 0, false, 0},
    {"  abc   ", false, 0, false, 0}};

TEST(BaseString, TestVectors) {
  for (size_t i = 0; i < sizeof(test_vectors) / sizeof(test_vectors[0]); ++i) {
    bool unsigned_success = false;
    uint64_t unsigned_value =
        base::StrToUint64(test_vectors[i].str, &unsigned_success);
    bool signed_success = false;
    int64_t signed_value =
        base::StrToInt64(test_vectors[i].str, &signed_success);
    EXPECT_EQ(unsigned_success, test_vectors[i].unsigned_success);
    EXPECT_EQ(signed_success, test_vectors[i].signed_success);
    if (unsigned_success) {
      EXPECT_EQ(unsigned_value, test_vectors[i].unsigned_value);
    }
    if (signed_success) {
      EXPECT_EQ(signed_value, test_vectors[i].signed_value);
    }
  }
}

TEST(BaseString, StdString) {
  std::string str{"  4711k    "};
  bool unsigned_success = false;
  uint64_t unsigned_value = base::StrToUint64(str, &unsigned_success);
  bool signed_success = false;
  int64_t signed_value = base::StrToInt64(str, &signed_success);
  EXPECT_TRUE(unsigned_success);
  EXPECT_TRUE(signed_success);
  EXPECT_EQ(unsigned_value, 4711 * kKiloU);
  EXPECT_EQ(signed_value, 4711 * kKilo);
}

TEST(BaseString, TrimWhitespace) {
  const char* str1 = "hej";
  const char* str2 = "  abc  ";
  const char* str3 = "";
  char str4[] = "\t\r\n";
  char str5[] = "1234";
  char str6[] = "    .";

  EXPECT_EQ(base::TrimLeadingWhitespace(str1), str1);
  EXPECT_EQ(base::TrimLeadingWhitespace(str2), str2 + 2);
  EXPECT_EQ(base::TrimLeadingWhitespace(str3), str3);
  EXPECT_EQ(base::TrimLeadingWhitespace(str4), str4 + 3);
  EXPECT_EQ(base::TrimLeadingWhitespace(str5), str5);
  EXPECT_EQ(base::TrimLeadingWhitespace(str6), str6 + 4);
}
