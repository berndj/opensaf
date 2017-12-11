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

#include "base/unix_socket.h"
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include "base/file_descriptor.h"
#include "base/osaf_utility.h"
#include "base/time.h"

namespace base {

UnixSocket::UnixSocket(const std::string& path, Mode mode)
    : fd_{-1},
      addr_{},
      addrlen_{},
      last_failed_open_{},
      saved_errno_{},
      mode_{mode} {
  addrlen_ = SetAddress(path, &addr_);
}

UnixSocket::UnixSocket(const sockaddr_un& addr, socklen_t addrlen, Mode mode)
    : fd_{-1},
      addr_{},
      addrlen_{addrlen},
      last_failed_open_{},
      saved_errno_{},
      mode_{mode} {
  memcpy(&addr_, &addr, addrlen);
}

int UnixSocket::Open() {
  int sock = fd_;
  if (sock < 0) {
    if (addrlen_ != 0) {
      struct timespec current_time = ReadMonotonicClock();
      if (TimespecToMillis(current_time - last_failed_open_) != 0) {
        sock = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        if (sock >= 0 && !OpenHook(sock)) {
          int e = errno;
          close(sock);
          sock = -1;
          errno = e;
        }
        fd_ = sock;
        if (sock >= 0 && mode_ == kNonblocking && !MakeFdNonblocking(sock)) {
          Close();
        }
        if (fd_ < 0) {
          last_failed_open_ = current_time;
          saved_errno_ = errno;
        }
      } else {
        errno = saved_errno_;
      }
    } else {
      errno = ENAMETOOLONG;
    }
  }
  return sock;
}

UnixSocket::~UnixSocket() {
  if (fd_ >= 0) close(fd_);
  fd_ = -1;
  addrlen_ = 0;
}

void UnixSocket::Close() {
  int sock = fd_;
  if (sock >= 0) {
    int e = errno;
    close(sock);
    fd_ = -1;
    CloseHook();
    errno = e;
  }
}

bool UnixSocket::OpenHook(int) { return true; }

void UnixSocket::CloseHook() {}

}  // namespace base
