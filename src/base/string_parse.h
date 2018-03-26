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

#ifndef BASE_STRING_PARSE_H_
#define BASE_STRING_PARSE_H_

#include <cstdint>
#include <string>

namespace base {

// Parse the string @a str as a signed (StrToInt64) or unsigned (StrToUint6t4)
// integer. Leading and trailing whitespace is ignored. Returns the parsed value
// and sets the output parameter @a success to true if the whole string was
// successfully parsed. Parsing will fail if no valid integer was found, if an
// underflow or overflow occurred, or if not the whole string was consumed.
// Standard C/C++ base prefixes (0 for octal and 0x or 0X for hexadecimal) can
// be used, as well as the suffixes k (1024), M (1024 ** 2) and G (1024 ** 3).
int64_t StrToInt64(const char* str, bool* success);
uint64_t StrToUint64(const char* str, bool* success);
static inline int64_t StrToInt64(const std::string& str, bool* success) {
  return StrToInt64(str.c_str(), success);
}
static inline uint64_t StrToUint64(const std::string& str, bool* success) {
  return StrToUint64(str.c_str(), success);
}

// Returns the substring of @a str starting at the first non-whitespace
// character.
const char* TrimLeadingWhitespace(const char* str);
char* TrimLeadingWhitespace(char* str);

}  // namespace base

#endif  // BASE_STRING_PARSE_H_
