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

#include "base/osaf_time.h"
#include "base/tests/mock_clock_gettime.h"
#include "gtest/gtest.h"

TEST(OsafGetBootTime, GetBootTime) {
  time_t sec = 123456789;
  long nsec = 987654321;
  realtime_clock = {sec, nsec};
  boottime_clock = {8888, 9999};
  mock_clock_gettime.execution_time.tv_sec = 1234;
  mock_clock_gettime.execution_time.tv_nsec = 4321;
  mock_clock_gettime.return_value = 0;
  mock_clock_gettime.errno_value = 0;
  mock_clock_gettime.disable_mock = false;

  uint64_t gettime_exec_ns = 1234 * static_cast<uint64_t>(kNanosPerSec) + 4321;
  uint64_t boottime_before_ns =
      8888 * static_cast<uint64_t>(kNanosPerSec) + 9999;
  uint64_t realtime_ns =
      sec * static_cast<uint64_t>(kNanosPerSec) + nsec + gettime_exec_ns;
  uint64_t boottime_after_ns =
      8888 * static_cast<uint64_t>(kNanosPerSec) + 9999 + 2 * gettime_exec_ns;
  uint64_t boottime_average = (boottime_before_ns + boottime_after_ns) / 2;
  struct timespec expected = {0, 0};
  osaf_nanos_to_timespec(realtime_ns - boottime_average, &expected);
  struct timespec ts = {0, 0};
  osaf_get_boot_time(&ts);

  EXPECT_EQ(ts.tv_sec, expected.tv_sec);
  EXPECT_EQ(ts.tv_nsec, expected.tv_nsec);
  mock_clock_gettime.execution_time.tv_sec = 0;
  mock_clock_gettime.execution_time.tv_nsec = 0;
}

TEST(OsafGetBootTime, GetTimeFails) {
  mock_clock_gettime.return_value = -1;
  mock_clock_gettime.errno_value = EINVAL;
  mock_clock_gettime.disable_mock = false;
  struct timespec ts = {1111, 2222};
  osaf_get_boot_time(&ts);

  EXPECT_EQ(ts.tv_sec, 0);
  EXPECT_EQ(ts.tv_nsec, 0);
  mock_clock_gettime.return_value = 0;
  mock_clock_gettime.errno_value = 0;
}

TEST(OsafGetBootTime, BootTimeBeforeEpoc) {
  time_t sec = 123456789;
  long nsec = 987654321;
  realtime_clock = {sec, nsec};
  boottime_clock = {sec, nsec + 1};
  mock_clock_gettime.execution_time.tv_sec = 0;
  mock_clock_gettime.execution_time.tv_nsec = 0;
  mock_clock_gettime.return_value = 0;
  mock_clock_gettime.errno_value = 0;
  mock_clock_gettime.disable_mock = false;
  struct timespec ts = {1111, 2222};
  osaf_get_boot_time(&ts);
  EXPECT_EQ(ts.tv_sec, 0);
  EXPECT_EQ(ts.tv_nsec, 0);
}
