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

#include <poll.h>
#include <sched.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <random>
#include "base/time.h"
#include "base/unix_server_socket.h"
#include "dtm/common/osaflog_protocol.h"

namespace {

uint64_t Random64Bits(uint64_t seed) {
  std::mt19937_64 generator{base::TimespecToNanos(base::ReadRealtimeClock()) *
                            seed};
  return generator();
}

void PrintUsage(const char* program_name) {
  printf(
      "Usage: %s <OPTION>\n"
      "Send a command to the node-local internal OpenSAF log server\n"
      "\n"
      "Opions:\n"
      "  --flush    Flush all buffered log messages from memory to disk\n",
      program_name);
}

base::UnixServerSocket* CreateSocket() {
  base::UnixServerSocket* sock = nullptr;
  Osaflog::ClientAddress addr{};
  addr.pid = getpid();
  for (uint64_t i = 1; i <= 1000; ++i) {
    addr.random = Random64Bits(i);
    sock = new base::UnixServerSocket(addr.sockaddr(), addr.sockaddr_length(),
                                      base::UnixSocket::kNonblocking);
    if (sock->fd() >= 0 || errno != EADDRINUSE) break;
    delete sock;
    sock = nullptr;
    sched_yield();
  }
  if (sock != nullptr && sock->fd() < 0) {
    delete sock;
    sock = nullptr;
  }
  return sock;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2 || strcmp(argv[1], "--flush") != 0) {
    PrintUsage(argv[0]);
    exit(EXIT_FAILURE);
  }
  auto sock = std::unique_ptr<base::UnixServerSocket>(CreateSocket());

  if (!sock) {
    fprintf(stderr, "Failed to create UNIX domain socket\n");
    exit(EXIT_FAILURE);
  }

  struct sockaddr_un osaftransportd_addr;
  socklen_t addrlen = base::UnixSocket::SetAddress(Osaflog::kServerSocketPath,
                                                   &osaftransportd_addr);
  const char flush_command[] = "?flush";
  ssize_t result = sock->SendTo(flush_command, sizeof(flush_command) - 1,
                                &osaftransportd_addr, addrlen);
  if (result < 0) {
    perror("Failed to send message to osaftransportd");
    exit(EXIT_FAILURE);
  } else if (static_cast<size_t>(result) != (sizeof(flush_command) - 1)) {
    fprintf(stderr, "Failed to send message to osaftransportd\n");
    exit(EXIT_FAILURE);
  }
  char buf[256];
  static const char expected_reply[] = "!flush";
  struct timespec end_time = base::ReadMonotonicClock() + base::kTenSeconds;
  for (;;) {
    struct pollfd fds {
      sock->fd(), POLLIN, 0
    };
    struct timespec current_time = base::ReadMonotonicClock();
    result = 0;
    if (current_time >= end_time) {
      fprintf(stderr, "Timeout\n");
      exit(EXIT_FAILURE);
    }
    struct timespec timeout = end_time - current_time;
    result = ppoll(&fds, 1, &timeout, NULL);
    if (result < 0) {
      perror("Failed to wait for reply from osaftransportd");
      exit(EXIT_FAILURE);
    } else if (result == 0) {
      fprintf(stderr, "Timeout\n");
      exit(EXIT_FAILURE);
    }
    struct sockaddr_un sender_addr;
    socklen_t sender_addrlen = sizeof(sender_addr);
    result = sock->RecvFrom(buf, sizeof(buf), &sender_addr, &sender_addrlen);
    if (result < 0) break;
    if (sender_addrlen == addrlen &&
        memcmp(&osaftransportd_addr, &sender_addr, addrlen) == 0)
      break;
  }
  if (result < 0) {
    perror("Failed to receive reply from osaftransportd");
    exit(EXIT_FAILURE);
  } else if (static_cast<size_t>(result) != (sizeof(expected_reply) - 1) ||
             memcmp(buf, expected_reply, result) != 0) {
    fprintf(stderr, "Received unexpected reply from osaftransportd\n");
    exit(EXIT_FAILURE);
  }
  exit(EXIT_SUCCESS);
}
