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

#ifndef BASE_UNIX_SOCKET_H_
#define BASE_UNIX_SOCKET_H_

#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <cerrno>
#include <cstring>
#include <string>
#include "base/macros.h"

namespace base {

// A class implementing operations on a UNIX domain socket. This class is not
// thread-safe and must be protected by a mutex if shared between multiple
// threads. You can select to use either blocking or non-blocking mode.
class UnixSocket {
 public:
  // I/O mode for Send and Receive socket operations.
  enum Mode { kNonblocking, kBlocking };
  // Close the socket.
  virtual ~UnixSocket();
  // Returns the current file descriptor for this UNIX socket, or -1 if the
  // socket could not be opened. Note that the Send() and Recv() methods may
  // open and/or close the socket, and the file descriptor can therefore
  // potentially be change as a result of calling any of these two methods. If a
  // UnixSocket instance is shared between multiple threads (and therefore
  // protected with a mutex), this means that the result from the fd() method is
  // only valid for as long as the mutex protecting the instance is held.
  int fd() {
    int sock = fd_;
    if (sock < 0) sock = Open();
    return sock;
  }
  // Send a message in blocking or non-blocking mode to the specified
  // destination. This call will open the socket if it was not already open. The
  // EINTR error code from the sendto() libc function is handled by retrying the
  // sendto() call in a loop. In case of other errors, -1 is returned and errno
  // contains one of the codes listed in the sendto() libc function. The socket
  // will be closed in case the error was not EINTR, EAGAIN or EWOULDBLOCK.
  ssize_t SendTo(const void* buffer, size_t length,
                 const struct sockaddr_un* dest_addr, socklen_t addrlen) {
    int sock = fd();
    ssize_t result = -1;
    if (sock >= 0) {
      do {
        result = sendto(sock, buffer, length, MSG_NOSIGNAL,
                        reinterpret_cast<const struct sockaddr*>(dest_addr),
                        addrlen);
      } while (result < 0 && errno == EINTR);
      if (result < 0 && errno != EAGAIN && errno != EWOULDBLOCK) Close();
    }
    return result;
  }
  // Send a message in blocking or non-blocking mode. This call will open the
  // socket if it was not already open. The EINTR error code from the send()
  // libc function is handled by retrying the send() call in a loop. In case of
  // other errors, -1 is returned and errno contains one of the codes listed in
  // the send() libc function. The socket will be closed in case the error was
  // not EINTR, EAGAIN or EWOULDBLOCK.
  ssize_t Send(const void* buffer, size_t length) {
    int sock = fd();
    ssize_t result = -1;
    if (sock >= 0) {
      do {
        result = send(sock, buffer, length, MSG_NOSIGNAL);
      } while (result < 0 && errno == EINTR);
      if (result < 0 && errno != EAGAIN && errno != EWOULDBLOCK) Close();
    }
    return result;
  }
  // Receive a message in blocking or non-blocking mode and return the source
  // address. This call will open the socket if it was not already open. The
  // EINTR error code from the recvfrom() libc function is handled by retrying
  // the recvfrom() call in a loop. In case of other errors, -1 is returned and
  // errno contains one of the codes listed in the recvfrom() libc function. The
  // socket will be closed in case the error was not EINTR, EAGAIN or
  // EWOULDBLOCK.
  ssize_t RecvFrom(void* buffer, size_t length, struct sockaddr_un* src_addr,
                   socklen_t* addrlen) {
    int sock = fd();
    ssize_t result = -1;
    socklen_t len;
    if (sock >= 0) {
      do {
        len = *addrlen;
        result = recvfrom(sock, buffer, length, 0,
                          reinterpret_cast<struct sockaddr*>(src_addr), &len);
      } while (result < 0 && errno == EINTR);
      if (result < 0 && errno != EAGAIN && errno != EWOULDBLOCK) Close();
    }
    *addrlen = result >= 0 ? len : 0;
    return result;
  }
  // Receive a message in blocking or non-blocking mode. This call will open the
  // socket if it was not already open. The EINTR error code from the recv()
  // libc function is handled by retrying the recv() call in a loop. In case of
  // other errors, -1 is returned and errno contains one of the codes listed in
  // the recv() libc function. The socket will be closed in case the error was
  // not EINTR, EAGAIN or EWOULDBLOCK.
  ssize_t Recv(void* buffer, size_t length) {
    int sock = fd();
    ssize_t result = -1;
    if (sock >= 0) {
      do {
        result = recv(sock, buffer, length, 0);
      } while (result < 0 && errno == EINTR);
      if (result < 0 && errno != EAGAIN && errno != EWOULDBLOCK) Close();
    }
    return result;
  }

  // Set the path in a sockaddr_un structure. Takes a C++ string @a path and a
  // pointer to the sockarddr_un sturcture which is to be updated. Returns the
  // resulting number of used bytes in the structure, which is <=
  // sizeof(structaddr_un), or zero in case of an error (i.e. the @a path did
  // not fit into the structure). The pointer to the sockaddr_un structure
  // together with the returned size can be used as input to the SendTo method.
  //
  // Abstract unix domain addresses are supported, by setting the first byte in
  // @a path to '\0'.
  static socklen_t SetAddress(const std::string& path,
                              struct sockaddr_un* addr) {
    socklen_t addrlen;
    size_t size = path.size();
    if (size != 0 && path[0] != '\0') ++size;
    addr->sun_family = AF_UNIX;
    if (size <= sizeof(addr->sun_path)) {
      memcpy(addr->sun_path, path.c_str(), size);
      addrlen = sizeof(addr->sun_family) + size;
    } else {
      addr->sun_path[0] = '\0';
      addrlen = 0;
    }
    return addrlen;
  }

  // Returns true if the UNIX socket address @a addr is abstract, i.e. it is an
  // address which is not represented by a file in the file system.
  static bool IsAbstract(const struct sockaddr_un& addr) {
    return addr.sun_path[0] == '\0';
  }

  // Returns true if this UNIX socket has an abstract address, i.e. the address
  // is not represented by a file in the file system.
  bool IsAbstract() const { return IsAbstract(addr_); }

  // Get the path from a sockaddr_un structure. Takes a pointer to a
  // sockarddr_un sturcture and the size of the structure, and returns a C++
  // string containing the path name, or an abstract address starting with '\0'.
  // Returns an empty string in case of an error.
  static std::string GetAddress(const struct sockaddr_un& addr,
                                socklen_t addrlen) {
    if (addrlen > sizeof(addr.sun_family) && addrlen <= sizeof(addr) &&
        addr.sun_family == AF_UNIX) {
      size_t size =
          IsAbstract(addr)
              ? (addrlen - sizeof(addr.sun_family))
              : strnlen(addr.sun_path, addrlen - sizeof(addr.sun_family));
      return std::string(addr.sun_path, size);
    } else {
      return std::string{};
    }
  }

 protected:
  UnixSocket(const std::string& path, Mode mode);
  UnixSocket(const sockaddr_un& addr, socklen_t addrlen, Mode mode);
  virtual bool OpenHook(int sock);
  virtual void CloseHook();
  const struct sockaddr* addr() const {
    return reinterpret_cast<const struct sockaddr*>(&addr_);
  }
  socklen_t addrlen() { return addrlen_; }
  int get_fd() const { return fd_; }
  const char* path() const { return addr_.sun_path; }

 private:
  int Open();
  void Close();

  int fd_;
  struct sockaddr_un addr_;
  socklen_t addrlen_;
  struct timespec last_failed_open_;
  int saved_errno_;
  Mode mode_;

  DELETE_COPY_AND_MOVE_OPERATORS(UnixSocket);
};

}  // namespace base

#endif  // BASE_UNIX_SOCKET_H_
