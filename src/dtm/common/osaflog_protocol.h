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

#ifndef DTM_COMMON_OSAFLOG_PROTOCOL_H_
#define DTM_COMMON_OSAFLOG_PROTOCOL_H_

#include <sys/socket.h>
#include <cstdint>
#include "osaf/configmake.h"

namespace Osaflog {

static constexpr const char* kServerSocketPath =
    PKGLOCALSTATEDIR "/osaf_log.sock";

struct __attribute__((__packed__)) ClientAddressConstantPrefix {
  sa_family_t family = AF_UNIX;
  char abstract = '\0';
  uint64_t magic = UINT64_C(0xfe3a94accd2c701e);
};

struct __attribute__((__packed__)) ClientAddress {
  const struct sockaddr_un& sockaddr() const {
    return *reinterpret_cast<const struct sockaddr_un*>(this);
  }
  static constexpr socklen_t sockaddr_length() { return sizeof(ClientAddress); }
  const char* path() const { return sockaddr().sun_path; }
  static constexpr socklen_t path_length() {
    return sizeof(ClientAddress) - sizeof(sa_family_t);
  }
  ClientAddressConstantPrefix header;
  uint64_t random;
  uint32_t pid;
};

}  // namespace Osaflog

#endif  // DTM_COMMON_OSAFLOG_PROTOCOL_H_
