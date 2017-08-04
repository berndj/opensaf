/*      -*- OpenSAF  -*-
 *
 * (C) Copyright 2013 The OpenSAF Foundation
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
 * Author(s): Ericsson AB
 *
 */

/** @file
 *
 * This file contains utility functions related to time and time conversion.
 * Many functions operate on timespec structures, and require that the structure
 * is normalized. By this is meant that the tv_nsec member of the structure has
 * a value in the range [0, 999999999]. The osaf_normalize_timespec() function
 * can be used to convert a timespec strucure to normalized form, when
 * necessary. Negative times are not supported.
 *
 * The definitions in this file are for internal use within OpenSAF only. For
 * performace reasons, which may be important e.g. when doing time measurements,
 * most functions are declared as inline.
 */

#ifndef BASE_OSAF_TIME_H_
#define BASE_OSAF_TIME_H_

#include <stdint.h>
#include <time.h>
#include <sys/time.h>
#include <stdbool.h>
#include "base/osaf_utility.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
  /**
   *  Number of nanoseconds per second.
   */
  kNanosPerSec = 1000000000,
  /**
   *  Number of microseconds per second.
   */
  kMicrosPerSec = 1000000,
  /**
   *  Number of milliseconds per second.
   */
  kMillisPerSec = 1000
};

extern const struct timespec kZeroSeconds;
extern const struct timespec kTenMilliseconds;
extern const struct timespec kHundredMilliseconds;
extern const struct timespec kOneSecond;
extern const struct timespec kTwoSeconds;
extern const struct timespec kFiveSeconds;
extern const struct timespec kTenSeconds;
extern const struct timespec kFifteenSeconds;
extern const struct timespec kOneMinute;
extern const struct timespec kOneHour;

/**
 * @brief Sleep for the specified time
 *
 * This is a convenience function that behaves exactly like the POSIX function
 * nanosleep(3P), except that it will abort the process instead of returning an
 * error code in case of a failure. Also, in case the thread was interrupted by
 * a signal, this function will call nanosleep() again to sleep the remaining
 * time. Thus, this function will never return earlier than the requested
 * time-out.
 */
extern void osaf_nanosleep(const struct timespec* sleep_duration);

/**
 * @brief Get the time when the node booted
 *
 * This function gets the time stamp of the CLOCK_REALTIME clock when the node
 * booted, and stores the result in @a boot_time. Zero is returned in case of an
 * error. Note that since the boot time is calculated by reading the current
 * time and the node's uptime, you may not get the exact same result when
 * calling this function multiple times.
 */
extern void osaf_get_boot_time(struct timespec* boot_time);

/**
 * @brief Get the time
 *
 * This is a convenience function that behaves exactly like the POSIX function
 * clock_gettime(3P), except that it will abort the process instead of returning
 * an error code in case of a failure.
 */
static inline void osaf_clock_gettime(clockid_t i_clk_id,
                                      struct timespec* o_ts);

/**
 * @brief Normalize a timespec structure.
 *
 * This function normalizes the timespec structure @a i_ts and stores the result
 * in the output parameter @a o_nrm. The tv_nsec member of the normalized
 * structure is guaranteed to be in the range [0, 999999999]. The parameters are
 * allowed to point to the same memory address.
 */
static inline void osaf_normalize_timespec(const struct timespec* i_ts,
                                           struct timespec* o_nrm);

/**
 * @brief Calculate the sum of two timespec structures.
 *
 * This function adds the two timespec structures @a i_time1 and @a i_time2, and
 * stores the result in the output parameter @a o_sum. The input parameters @a
 * i_time1 and @a i_time2 must be normalized, and the output parameter @a o_sum
 * is guaranteed to also be normalized. The parameters are allowed to point to
 * the same memory address. Negative times are not supported.
 */
static inline void osaf_timespec_add(const struct timespec* i_time1,
                                     const struct timespec* i_time2,
                                     struct timespec* o_sum);

/**
 * @brief Calculate the difference between two timespec structures.
 *
 * This function subtracts the timespec structure @a i_start from the timespec
 * structure @a i_end, and stores the result in the output parameter @a
 * o_difference. The input parameters @a i_end and @a i_start must be
 * normalized, and the output parameter @a o_difference is guaranteed to also be
 * normalized. The parameters are allowed to point to the same memory
 * address. Negative times are not supported. In particular, this means that the
 * caller must ensure that @a i_end is greater than or equal to @a i_start. The
 * function osaf_timespec_compare() can be used to check this.
 */
static inline void osaf_timespec_subtract(const struct timespec* i_end,
                                          const struct timespec* i_start,
                                          struct timespec* o_difference);

/**
 * @brief Calculate the average of two timespec structures.
 *
 * This function calculates the average of the two timespec structures @a
 * i_time1 and @a i_time2, and stores the result in the output parameter @a
 * o_average. The input parameters must be normalized, and the output parameter
 * @a o_average is guaranteed to also be normalized. The parameters are allowed
 * to point to the same memory address. Negative times are not supported.
 */
static inline void osaf_timespec_average(const struct timespec* i_time1,
                                         const struct timespec* i_time2,
                                         struct timespec* o_average);

/**
 * @brief Compare two timespec structures.
 *
 * This function compares the two timespec structures @a i_end and @a i_start.
 * The return value will be -1, 0 or 1, if @a i_end is less than, equal to, or
 * greater than @a i_start, respectively. The input parameters @a i_end and @a
 * i_start must be normalized.
 */
static inline int osaf_timespec_compare(const struct timespec* i_end,
                                        const struct timespec* i_start);

/**
 * @brief Convert a timespec structure to a timeval structure.
 *
 * This function converts the timespec structure @a i_ts to a timeval structure,
 * and stores the result in the output parameter @a o_tv. The input parameter @a
 * i_ts does not have to be normalized. The output parameter @a o_tv will be
 * normalized if the input parameter @a i_ts was normalized. The parameters are
 * allowed to point to the same memory address.
 */
static inline void osaf_timespec_to_timeval(const struct timespec* i_ts,
                                            struct timeval* o_tv);

/**
 * @brief Convert a timeval structure to a timespec structure.
 *
 * This function converts the timeval structure @a i_tv to a timespec structure,
 * and stores the result in the output parameter @a o_ts. The input parameter @a
 * i_tv does not have to be normalized. The output parameter @a o_ts will be
 * normalized if the input parameter @a i_tv was normalized. The parameters are
 * allowed to point to the same memory address.
 */
static inline void osaf_timeval_to_timespec(const struct timeval* i_tv,
                                            struct timespec* o_ts);

/**
 * @brief Convert an integer representing time in milliseconds to a timespec
 * structure.
 *
 * This function converts the integer @a i_millis to a timespec structure, and
 * stores the result in the output parameter @a o_ts. The unit of @a i_millis is
 * milliseconds. The output parameter @a o_ts is guaranteed to be normalized.
 */
static inline void osaf_millis_to_timespec(uint64_t i_millis,
                                           struct timespec* o_ts);

/**
 * @brief Convert an integer representing time in microseconds to a timespec
 * structure.
 *
 * This function converts the integer @a i_micros to a timespec structure, and
 * stores the result in the output parameter @a o_ts. The unit of @a i_micros is
 * microseconds. The output parameter @a o_ts is guaranteed to be normalized.
 */
static inline void osaf_micros_to_timespec(uint64_t i_micros,
                                           struct timespec* o_ts);

/**
 * @brief Convert a double representing time in seconds to a timespec structure.
 *
 * This function converts the double precision floating point number @a
 * i_seconds to a timespec structure, and stores the result in the output
 * parameter @a o_ts. The unit of @a i_seconds is microseconds. The output
 * parameter @a o_ts is guaranteed to be normalized.
 */
static inline void osaf_double_to_timespec(double i_seconds,
                                           struct timespec* o_ts);

/**
 * @brief Convert an integer representing time in nanoseconds to a timespec
 * structure.
 *
 * This function converts the integer @a i_nanos to a timespec structure, and
 * stores the result in the output parameter @a o_ts. The unit of @a i_nanos is
 * nanoseconds. The output parameter @a o_ts is guaranteed to be normalized.
 */
static inline void osaf_nanos_to_timespec(uint64_t i_nanos,
                                          struct timespec* o_ts);

/**
 * @brief Convert a timespec structure to an integer representing time
 * in milliseconds.
 *
 * This function converts the timespec structure @a i_ts to an integer
 * representing time in milliseconds. The result will be rounded down. Note that
 * the returned unsigned 64-bit integer has a smaller range than what is
 * possible to represent in a timespec structure.
 */
static inline uint64_t osaf_timespec_to_millis(const struct timespec* i_ts);

/**
 * @brief Convert a timespec structure to an integer representing time
 * in microseconds.
 *
 * This function converts the timespec structure @a i_ts to an integer
 * representing time in microseconds. The result will be rounded down. Note that
 * the returned unsigned 64-bit integer has a smaller range than what is
 * possible to represent in a timespec structure.
 */
static inline uint64_t osaf_timespec_to_micros(const struct timespec* i_ts);

/**
 * @brief Convert a timespec structure to an integer representing time
 * in nanoseconds.
 *
 * This function converts the timespec structure @a i_ts to an integer
 * representing time in nanoseconds. Note that the returned unsigned 64-bit
 * integer has a smaller range than what is possible to represent in a timespec
 * structure.
 */
static inline uint64_t osaf_timespec_to_nanos(const struct timespec* i_ts);

/**
 * @brief Convert a timespec structure to a double representing time in
 * seconds.
 *
 * This function converts the timespec structure @a i_ts to a double precision
 * floating point number. The input parameter @a i_ts does not have to be
 * normalized. The unit of the returned value is seconds. Note you will lose
 * precision when converting to a double.
 */
static inline double osaf_timespec_to_double(const struct timespec* i_ts);

/**
 * @brief Set a time @a i_millis in the future. To be used with
 * osaf_is_timeout()
 *
 * Read current time and add @a i_millis in @a o_ts
 */
static inline void osaf_set_millis_timeout(uint64_t i_millis,
                                           struct timespec* o_ts);
/**
 * @brief Inform if the time given in @a i_ts is passed
 *
 * Read current time and compare with the given timeout time.
 * The return value will be -1, 0 or 1, if @a timeout time not passed,
 * time equal to @a timeout time, or greater than @a timeout time, respectively.
 */
static inline bool osaf_is_timeout(const struct timespec* i_ts);

static inline void osaf_clock_gettime(clockid_t i_clk_id,
                                      struct timespec* o_ts) {
  if (clock_gettime(i_clk_id, o_ts) != 0) osaf_abort(i_clk_id);
}

static inline void osaf_normalize_timespec(const struct timespec* i_ts,
                                           struct timespec* o_nrm) {
  time_t sec = i_ts->tv_sec + i_ts->tv_nsec / kNanosPerSec;
  long nsec = i_ts->tv_nsec % kNanosPerSec;
  if (nsec < 0) {
    sec -= 1;
    nsec += kNanosPerSec;
  }
  o_nrm->tv_sec = sec;
  o_nrm->tv_nsec = nsec;
}

static inline void osaf_timespec_add(const struct timespec* i_time1,
                                     const struct timespec* i_time2,
                                     struct timespec* o_sum) {
  time_t sec = i_time1->tv_sec + i_time2->tv_sec;
  long nsec = i_time1->tv_nsec + i_time2->tv_nsec;
  if (nsec >= kNanosPerSec) {
    sec += 1;
    nsec -= kNanosPerSec;
  }
  o_sum->tv_sec = sec;
  o_sum->tv_nsec = nsec;
}

static inline void osaf_timespec_subtract(const struct timespec* i_end,
                                          const struct timespec* i_start,
                                          struct timespec* o_difference) {
  time_t sec = i_end->tv_sec - i_start->tv_sec;
  long nsec = i_end->tv_nsec - i_start->tv_nsec;
  if (nsec < 0) {
    sec -= 1;
    nsec += kNanosPerSec;
  }
  o_difference->tv_sec = sec;
  o_difference->tv_nsec = nsec;
}

static inline void osaf_timespec_average(const struct timespec* i_time1,
                                         const struct timespec* i_time2,
                                         struct timespec* o_average) {
  // Use uint64_t instead of time_t, to avoid overflow (inputs are guaranteed to
  // be non-negative so tv_sec is in the range [0, INT64_MAX].
  uint64_t sec = (uint64_t)i_time1->tv_sec + (uint64_t)i_time2->tv_sec;
  long nsec = i_time1->tv_nsec + i_time2->tv_nsec;
  if (nsec >= kNanosPerSec) {
    sec += 1;
    nsec -= kNanosPerSec;
  }
  o_average->tv_sec = sec / 2;
  o_average->tv_nsec = (kNanosPerSec * (sec & 1) + nsec) / 2;
}

static inline int osaf_timespec_compare(const struct timespec* i_end,
                                        const struct timespec* i_start) {
  if (i_end->tv_sec == i_start->tv_sec) {
    if (i_end->tv_nsec < i_start->tv_nsec) return -1;
    return i_end->tv_nsec != i_start->tv_nsec ? 1 : 0;
  }
  return i_end->tv_sec > i_start->tv_sec ? 1 : -1;
}

static inline void osaf_timespec_to_timeval(const struct timespec* i_ts,
                                            struct timeval* o_tv) {
  time_t sec = i_ts->tv_sec;
  suseconds_t usec = i_ts->tv_nsec / (kNanosPerSec / kMicrosPerSec);
  o_tv->tv_sec = sec;
  o_tv->tv_usec = usec;
}

static inline void osaf_timeval_to_timespec(const struct timeval* i_tv,
                                            struct timespec* o_ts) {
  time_t sec = i_tv->tv_sec;
  long nsec = ((long)i_tv->tv_usec) * (kNanosPerSec / kMicrosPerSec);
  o_ts->tv_sec = sec;
  o_ts->tv_nsec = nsec;
}

static inline void osaf_millis_to_timespec(uint64_t i_millis,
                                           struct timespec* o_ts) {
  o_ts->tv_sec = i_millis / kMillisPerSec;
  o_ts->tv_nsec = (i_millis % kMillisPerSec) * (kNanosPerSec / kMillisPerSec);
}

static inline void osaf_micros_to_timespec(uint64_t i_micros,
                                           struct timespec* o_ts) {
  o_ts->tv_sec = i_micros / kMicrosPerSec;
  o_ts->tv_nsec = (i_micros % kMicrosPerSec) * (kNanosPerSec / kMicrosPerSec);
}

static inline void osaf_nanos_to_timespec(uint64_t i_nanos,
                                          struct timespec* o_ts) {
  o_ts->tv_sec = i_nanos / kNanosPerSec;
  o_ts->tv_nsec = i_nanos % kNanosPerSec;
}

static inline void osaf_double_to_timespec(double i_seconds,
                                           struct timespec* o_ts) {
  o_ts->tv_sec = i_seconds;
  o_ts->tv_nsec = (i_seconds - o_ts->tv_sec) * kNanosPerSec;
  /*
   * Since floating point numbers are inexact, there is a risk that the
   * result is not properly normalized. Therefore, we call
   * osaf_normalize_timespec() to guarantee a normalized result.
   */
  osaf_normalize_timespec(o_ts, o_ts);
}

static inline uint64_t osaf_timespec_to_millis(const struct timespec* i_ts) {
  return i_ts->tv_sec * (uint64_t)kMillisPerSec +
         i_ts->tv_nsec / (kNanosPerSec / kMillisPerSec);
}

static inline uint64_t osaf_timespec_to_micros(const struct timespec* i_ts) {
  return i_ts->tv_sec * (uint64_t)kMicrosPerSec +
         i_ts->tv_nsec / (kNanosPerSec / kMicrosPerSec);
}

static inline uint64_t osaf_timespec_to_nanos(const struct timespec* i_ts) {
  return i_ts->tv_sec * (uint64_t)kNanosPerSec + i_ts->tv_nsec;
}

static inline double osaf_timespec_to_double(const struct timespec* i_ts) {
  return i_ts->tv_sec + i_ts->tv_nsec / (double)kNanosPerSec;
}

static inline void osaf_set_millis_timeout(uint64_t i_millis,
                                           struct timespec* o_ts) {
  struct timespec current_ts;
  struct timespec millis_to_add_ts;
  osaf_clock_gettime(CLOCK_MONOTONIC, &current_ts);
  osaf_millis_to_timespec(i_millis, &millis_to_add_ts);
  osaf_timespec_add(&current_ts, &millis_to_add_ts, o_ts);
}

static inline bool osaf_is_timeout(const struct timespec* i_ts) {
  struct timespec current_ts;
  osaf_clock_gettime(CLOCK_MONOTONIC, &current_ts);
  if (osaf_timespec_compare(&current_ts, i_ts) < 0)
    return false;
  else
    return true;
}

static inline uint64_t osaf_timeout_time_left(const struct timespec* i_ts) {
  struct timespec current_ts;
  uint64_t time_left = 0;
  struct timespec diff_ts;
  osaf_clock_gettime(CLOCK_MONOTONIC, &current_ts);
  if (osaf_timespec_compare(&current_ts, i_ts) < 0) {
    osaf_timespec_subtract(i_ts, &current_ts, &diff_ts);
    time_left = osaf_timespec_to_millis(&diff_ts);
  } else {
    time_left = 0;
  }

  return time_left;
}

#ifdef __cplusplus
}
#endif

#endif  // BASE_OSAF_TIME_H_
