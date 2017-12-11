/*      -*- OpenSAF  -*-
 *
 * (C) Copyright 2016 The OpenSAF Foundation
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

#include "base/unix_client_socket.h"
#include <sys/socket.h>
#include <cerrno>

namespace base {

UnixClientSocket::UnixClientSocket(const std::string& path, Mode mode)
    : UnixSocket{path, mode} {}

UnixClientSocket::UnixClientSocket(const sockaddr_un& addr, socklen_t addrlen,
                                   Mode mode)
    : UnixSocket{addr, addrlen, mode} {}

UnixClientSocket::~UnixClientSocket() {}

bool UnixClientSocket::OpenHook(int sock) {
  int result;
  do {
    result = connect(sock, addr(), addrlen());
  } while (result != 0 && errno == EINTR);
  return result == 0;
}

}  // namespace base
