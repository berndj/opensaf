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

#include "base/string_parse.h"

#include <cctype>
#include <cerrno>
#include <cinttypes>
#include <cstdlib>

namespace {

int64_t ParseSuffix(char** endptr) {
  switch (**endptr) {
    case 'k':
      ++(*endptr);
      return 1024;
    case 'M':
      ++(*endptr);
      return 1024 * 1024;
    case 'G':
      ++(*endptr);
      return 1024 * 1024 * 1024;
    default:
      return 1;
  }
}

}  // namespace

namespace base {

int64_t StrToInt64(const char* str, bool* success) {
  str = TrimLeadingWhitespace(str);
  errno = 0;
  char* endptr;
  int64_t val = strtoll(str, &endptr, 0);
  int64_t multiplier = ParseSuffix(&endptr);
  endptr = TrimLeadingWhitespace(endptr);
  *success = *str != '\0' && errno == 0 && *endptr == '\0' &&
             (val >= 0 ? val <= (INT64_MAX / multiplier)
                       : val >= (INT64_MIN / multiplier));
  return val * multiplier;
}

uint64_t StrToUint64(const char* str, bool* success) {
  str = TrimLeadingWhitespace(str);
  errno = 0;
  char* endptr;
  uint64_t val = strtoull(str, &endptr, 0);
  uint64_t multiplier = ParseSuffix(&endptr);
  endptr = TrimLeadingWhitespace(endptr);
  *success = *str != '\0' && *str != '-' && errno == 0 && *endptr == '\0' &&
             val <= (~static_cast<uint64_t>(0) / multiplier);
  return val * multiplier;
}

const char* TrimLeadingWhitespace(const char* str) {
  while (isspace(*str)) ++str;
  return str;
}

char* TrimLeadingWhitespace(char* str) {
  while (isspace(*str)) ++str;
  return str;
}

}  // namespace base
