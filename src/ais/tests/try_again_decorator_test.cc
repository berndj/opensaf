/*      -*- OpenSAF  -*-
 *
 * (C) Copyright 2017 The OpenSAF Foundation
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
 * Author(s): Ericsson AB
 *
 */

#include "ais/try_again_decorator.h"
#include <cstdlib>
#include "gtest/gtest.h"

namespace {
int test_counter = 0;
};

extern "C" SaAisErrorT TestMethod() {
  ++test_counter;
  return SA_AIS_ERR_TRY_AGAIN;
}

extern "C" SaAisErrorT TestOtherMethod() {
  return (++test_counter % 2 ) ? SA_AIS_ERR_TRY_AGAIN : SA_AIS_ERR_UNAVAILABLE;
}

TEST(make_decorator, DefaultControl) {
  // This test case takes more than one minute!
  // Only run if `OSAF_SLOW_UNITTESTS` is defined.
  if (getenv("OSAF_SLOW_UNITTESTS") != nullptr) {
    auto DecorTestMethod = ais::make_decorator(::TestMethod);
    EXPECT_EQ(DecorTestMethod(), SA_AIS_ERR_TRY_AGAIN);
    EXPECT_GE(test_counter, 400);
    EXPECT_LE(test_counter, 601);
    test_counter = 0;
  }
}

class MyTryAgain {
 public:
  constexpr static bool is_ais_code_accepted(SaAisErrorT code) {
    return (code != SA_AIS_ERR_TRY_AGAIN && code != SA_AIS_ERR_UNAVAILABLE);
  }

  // The interval at leat should be 100ms, but this is just for testing.
  constexpr static uint64_t kIntervalMs = 10;
  constexpr static uint64_t kTimeoutMs  = 100;
};

TEST(make_decorator, GivenRetryControl) {
  using MyPolicy = ais::UseMyPolicy<MyTryAgain>;
  auto DecorTestMethod = MyPolicy::make_decorator(::TestOtherMethod);
  test_counter = 0;

  SaAisErrorT ais_ret = DecorTestMethod();
  if (test_counter % 2) {
    EXPECT_EQ(ais_ret, SA_AIS_ERR_TRY_AGAIN);
  } else {
    EXPECT_EQ(ais_ret, SA_AIS_ERR_UNAVAILABLE);
  }

  EXPECT_GE(test_counter, 5);
  EXPECT_LE(test_counter, 10);
  test_counter = 0;
}
