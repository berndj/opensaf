/*      -*- OpenSAF  -*-
 *
 * Copyright Ericsson AB 2017 - All Rights Reserved.
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

#include <cerrno>
#include <cstdint>
#include "base/osaf_time.h"
#include "gtest/gtest.h"

static const uint64_t NANOS_PER_SEC_U64 = kNanosPerSec;
static const long NANOS_PER_SEC_LONG = kNanosPerSec;

TEST(OsafTimespecAverage, Zero) {
  struct timespec time1 = {0, 0};
  struct timespec time2 = {0, 0};
  struct timespec average = {1, 1};
  osaf_timespec_average(&time1, &time2, &average);
  EXPECT_EQ(average.tv_sec, 0);
  EXPECT_EQ(average.tv_nsec, 0);
}

TEST(OsafTimespecAverage, OneSecond) {
  struct timespec time1 = {1, 0};
  struct timespec time2 = {0, 0};
  struct timespec average = {1, 1};
  osaf_timespec_average(&time1, &time2, &average);
  EXPECT_EQ(average.tv_sec, 0);
  EXPECT_EQ(average.tv_nsec, NANOS_PER_SEC_LONG / 2);
}

TEST(OsafTimespecAverage, AlmostOneSecond) {
  struct timespec time1 = {0, NANOS_PER_SEC_LONG - 1};
  struct timespec time2 = {0, NANOS_PER_SEC_LONG - 1};
  struct timespec average = {1, 1};
  osaf_timespec_average(&time1, &time2, &average);
  EXPECT_EQ(average.tv_sec, 0);
  EXPECT_EQ(average.tv_nsec, NANOS_PER_SEC_LONG - 1);
}

TEST(OsafTimespecAverage, AlmostTwoSeconds) {
  struct timespec time1 = {1, NANOS_PER_SEC_LONG - 1};
  struct timespec time2 = {0, NANOS_PER_SEC_LONG - 1};
  struct timespec average = {1, 1};
  osaf_timespec_average(&time1, &time2, &average);
  uint64_t expected_ns = (NANOS_PER_SEC_U64 + 2 * (NANOS_PER_SEC_U64 - 1)) / 2;
  struct timespec expected;
  osaf_nanos_to_timespec(expected_ns, &expected);
  EXPECT_EQ(average.tv_sec, expected.tv_sec);
  EXPECT_EQ(average.tv_nsec, expected.tv_nsec);
}

TEST(OsafTimespecAverage, SimpleCase) {
  struct timespec time1 = {222, 1234};
  struct timespec time2 = {111, 5678};
  struct timespec average = {1, 1};
  osaf_timespec_average(&time1, &time2, &average);
  uint64_t expected_ns =
      (222 * NANOS_PER_SEC_U64 + 111 * NANOS_PER_SEC_U64 + 1234 + 5678) / 2;
  struct timespec expected;
  osaf_nanos_to_timespec(expected_ns, &expected);
  EXPECT_EQ(average.tv_sec, expected.tv_sec);
  EXPECT_EQ(average.tv_nsec, expected.tv_nsec);
}

TEST(OsafTimespecAverage, NanosOverflow) {
  struct timespec time1 = {10, NANOS_PER_SEC_LONG / 2};
  struct timespec time2 = {11, NANOS_PER_SEC_LONG / 2};
  struct timespec average = {1, 1};
  osaf_timespec_average(&time1, &time2, &average);
  uint64_t expected_ns =
      (10 * NANOS_PER_SEC_U64 + 11 * NANOS_PER_SEC_U64 + NANOS_PER_SEC_U64) / 2;
  struct timespec expected;
  osaf_nanos_to_timespec(expected_ns, &expected);
  EXPECT_EQ(average.tv_sec, expected.tv_sec);
  EXPECT_EQ(average.tv_nsec, expected.tv_nsec);
}

TEST(OsafTimespecAverage, Max) {
  time_t max_time = INT32_MAX;
  if (sizeof(time_t) == 8) max_time = INT64_MAX;
  struct timespec time1 = {max_time, NANOS_PER_SEC_LONG - 1};
  struct timespec time2 = {max_time, NANOS_PER_SEC_LONG - 1};
  struct timespec average = {1, 1};
  osaf_timespec_average(&time1, &time2, &average);
  EXPECT_EQ(average.tv_sec, max_time);
  EXPECT_EQ(average.tv_nsec, NANOS_PER_SEC_LONG - 1);
}

TEST(OsafTimespecAverage, MaxAndZero) {
  time_t max_time = INT32_MAX;
  if (sizeof(time_t) == 8) max_time = INT64_MAX;
  struct timespec time1 = {max_time, NANOS_PER_SEC_LONG - 1};
  struct timespec time2 = {0, 0};
  struct timespec average = {1, 1};
  osaf_timespec_average(&time1, &time2, &average);
  EXPECT_EQ(average.tv_sec, max_time / 2);
  EXPECT_EQ(average.tv_nsec, NANOS_PER_SEC_LONG - 1);
}

TEST(OsafTimespecAverage, MaxAndOneNanoSecond) {
  time_t max_time = INT32_MAX;
  if (sizeof(time_t) == 8) max_time = INT64_MAX;
  struct timespec time1 = {max_time, NANOS_PER_SEC_LONG - 1};
  struct timespec time2 = {0, 1};
  struct timespec average = {1, 1};
  osaf_timespec_average(&time1, &time2, &average);
  EXPECT_EQ(average.tv_sec, max_time / 2 + 1);
  EXPECT_EQ(average.tv_nsec, 0);
}

TEST(OsafTimespecAverage, MaxAndTwoNanoSeconds) {
  time_t max_time = INT32_MAX;
  if (sizeof(time_t) == 8) max_time = INT64_MAX;
  struct timespec time1 = {max_time, NANOS_PER_SEC_LONG - 1};
  struct timespec time2 = {0, 2};
  struct timespec average = {1, 1};
  osaf_timespec_average(&time1, &time2, &average);
  EXPECT_EQ(average.tv_sec, max_time / 2 + 1);
  EXPECT_EQ(average.tv_nsec, 0);
}

TEST(OsafTimespecAverage, MaxAndOneSecond) {
  time_t max_time = INT32_MAX;
  if (sizeof(time_t) == 8) max_time = INT64_MAX;
  struct timespec time1 = {max_time, NANOS_PER_SEC_LONG - 1};
  struct timespec time2 = {1, 0};
  struct timespec average = {1, 1};
  osaf_timespec_average(&time1, &time2, &average);
  EXPECT_EQ(average.tv_sec, max_time / 2 + 1);
  EXPECT_EQ(average.tv_nsec, NANOS_PER_SEC_LONG / 2 - 1);
}

TEST(OsafTimespecAverage, MaxAndTwoSeconds) {
  time_t max_time = INT32_MAX;
  if (sizeof(time_t) == 8) max_time = INT64_MAX;
  struct timespec time1 = {max_time, NANOS_PER_SEC_LONG - 1};
  struct timespec time2 = {2, 0};
  struct timespec average = {1, 1};
  osaf_timespec_average(&time1, &time2, &average);
  EXPECT_EQ(average.tv_sec, max_time / 2 + 1);
  EXPECT_EQ(average.tv_nsec, NANOS_PER_SEC_LONG - 1);
}
