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

#include <fcntl.h>
#include <getopt.h>
#include <poll.h>
#include <sched.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <list>
#include <memory>
#include <random>
#include <string>
#include "base/time.h"
#include "base/unix_server_socket.h"
#include "dtm/common/osaflog_protocol.h"
#include "osaf/configmake.h"

namespace {

void PrintUsage(const char* program_name);
bool Flush();
bool MaxTraceFileSize(size_t max_file_size);
bool NoOfBackupFiles(size_t number_of_backups);
base::UnixServerSocket* CreateSocket();
uint64_t Random64Bits(uint64_t seed);
bool PrettyPrint(const std::string& log_stream);
std::list<int> OpenLogFiles(const std::string& log_stream);
std::string PathName(const std::string& log_stream, int suffix);
uint64_t GetInode(int fd);
bool PrettyPrint(FILE* stream);
bool PrettyPrint(const char* line, size_t size);

char buf[65 * 1024];

}  // namespace

int main(int argc, char** argv) {
  struct option long_options[] = {{"max-file-size", required_argument, 0, 'm'},
                                  {"max-backups", required_argument, 0, 'b'},
                                  {"flush", no_argument, 0, 'f'},
                                  {"print", required_argument, nullptr, 'p'},
                                  {0, 0, 0, 0}};

  size_t max_file_size = 0;
  size_t max_backups = 0;
  char *pretty_print_argument = NULL;
  int option = 0;

  int long_index = 0;
  bool flush_result =  true;
  bool print_result =  true;
  bool max_file_size_result = true;
  bool number_of_backups_result = true;
  bool flush_set = false;
  bool pretty_print_set = false;
  bool max_file_size_set = false;
  bool max_backups_set = false;

  if (argc == 1) {
     PrintUsage(argv[0]);
     exit(EXIT_FAILURE);
  }

  while ((option = getopt_long(argc, argv, "m:b:p:f",
                   long_options, &long_index)) != -1) {
        switch (option) {
             case 'p':
                   pretty_print_argument = optarg;
                   pretty_print_set = true;
                   flush_set = true;
                 break;
             case 'f':
                   flush_set = true;
                 break;
             case 'm':
                   max_file_size_set = true;
                   max_file_size = atoi(optarg);
                 break;
             case 'b':
                   max_backups_set = true;
                   max_backups = atoi(optarg);
                 break;
             default: PrintUsage(argv[0]);
                 exit(EXIT_FAILURE);
        }
  }

  if (argc - optind == 1) {
     flush_result = Flush();
     flush_set = false;
     print_result = PrettyPrint(argv[optind]);
     pretty_print_set = false;
  } else if (argc - optind > 1) {
     PrintUsage(argv[0]);
     exit(EXIT_FAILURE);
  }

  if (flush_set == true) {
     flush_result = Flush();
  }
  if (pretty_print_set == true) {
     print_result = PrettyPrint(pretty_print_argument);
  }
  if (max_backups_set == true) {
     number_of_backups_result = NoOfBackupFiles(max_backups);
  }
  if (max_file_size_set == true) {
     max_file_size_result = MaxTraceFileSize(max_file_size);
  }
  if (flush_result && print_result && max_file_size_result &&
                                          number_of_backups_result)
     exit(EXIT_SUCCESS);
  exit(EXIT_FAILURE);
}

namespace {

void PrintUsage(const char* program_name) {
  fprintf(stderr,
          "Usage: %s [OPTION] [LOGSTREAM]\n"
          "\n"
          "print the messages stored on disk for the specified\n"
          "LOGSTREAM. When a LOGSTREAM argument is specified, the option\n"
          "--flush is implied.\n"
          "\n"
          "Opions:\n"
          "\n"
          "--flush          Flush all buffered messages in the log server to\n"
          "                 disk even when no LOGSTREAM is specified\n"
          "--print          print the messages stored on disk for the \n"
          "                 specified LOGSTREAM.This option is default\n"
          "                 when no option is specified.\n"
          "--max-file-size  Set the maximum size (in bytes) of the log file\n"
          "                 before the log is rotated.\n"
          "--max-backups    Set the maximum number of backup files to keep\n"
          "                 when rotating the log.\n",
          program_name);
}

bool SendCommand(const std::string& command) {
  std::string request{std::string{"?"} + command};
  std::string expected_reply{std::string{"!"} + command};
  auto sock = std::unique_ptr<base::UnixServerSocket>(CreateSocket());

  if (!sock) {
    fprintf(stderr, "Failed to create UNIX domain socket\n");
    return false;
  }

  struct sockaddr_un osaftransportd_addr;
  socklen_t addrlen = base::UnixSocket::SetAddress(Osaflog::kServerSocketPath,
                                                   &osaftransportd_addr);

  ssize_t result = sock->SendTo(request.data(), request.size(),
                                                &osaftransportd_addr, addrlen);
  if (result < 0) {
    perror("Failed to send message to osaftransportd");
    return false;
  } else if (static_cast<size_t>(result) != request.size()) {
    fprintf(stderr, "Failed to send message to osaftransportd\n");
    return false;
  }

  struct timespec end_time = base::ReadMonotonicClock() + base::kTenSeconds;
  for (;;) {
    struct pollfd fds {
      sock->fd(), POLLIN, 0
    };
    struct timespec current_time = base::ReadMonotonicClock();
    result = 0;
    if (current_time >= end_time) {
      fprintf(stderr, "Timeout\n");
      return false;
    }
    struct timespec timeout = end_time - current_time;
    result = ppoll(&fds, 1, &timeout, NULL);
    if (result < 0) {
      perror("Failed to wait for reply from osaftransportd");
      return false;
    } else if (result == 0) {
      fprintf(stderr, "Timeout\n");
      return false;
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
    return false;
  } else if (static_cast<size_t>(result) != expected_reply.size() ||
             memcmp(buf, expected_reply.data(), result) != 0) {
    fprintf(stderr, "ERROR: osaftransportd replied '%s'\n",
            std::string{buf, static_cast<size_t>(result)}.c_str());
    return false;
  }
  return true;
}

bool MaxTraceFileSize(size_t max_file_size) {
  return SendCommand(std::string{"max-file-size "} +
                     std::to_string(max_file_size));
}

bool NoOfBackupFiles(size_t max_backups) {
  return SendCommand(std::string{"max-backups "} + std::to_string(max_backups));
}

bool Flush() {
  return SendCommand(std::string{"flush"});
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

uint64_t Random64Bits(uint64_t seed) {
  std::mt19937_64 generator{base::TimespecToNanos(base::ReadRealtimeClock()) *
                            seed};
  return generator();
}

bool PrettyPrint(const std::string& log_stream) {
  std::list<int> fds = OpenLogFiles(log_stream);
  uint64_t last_inode = ~UINT64_C(0);
  bool result = true;
  for (const auto& fd : fds) {
    uint64_t inode = GetInode(fd);
    if (inode == last_inode) {
      close(fd);
      continue;
    }
    last_inode = inode;
    FILE* stream = fdopen(fd, "r");
    if (stream != nullptr) {
      if (!PrettyPrint(stream)) result = false;
      fclose(stream);
    } else {
      result = false;
      close(fd);
    }
  }
  return result;
}

std::list<int> OpenLogFiles(const std::string& log_stream) {
  std::list<int> result{};
  bool last_open_failed = false;
  for (int suffix = 0; suffix < 100; ++suffix) {
    std::string path_name = PathName(log_stream, suffix);
    std::string next_path_name = PathName(log_stream, suffix + 1);
    int fd;
    do {
      fd = open(path_name.c_str(), O_RDONLY);
    } while (fd < 0 && errno == EINTR);
    if (fd >= 0) {
      result.push_front(fd);
      last_open_failed = false;
    } else {
      if (errno != ENOENT) perror("Could not open log file");
      if (last_open_failed || errno != ENOENT) break;
      last_open_failed = true;
    }
  }
  return result;
}

std::string PathName(const std::string& log_stream, int suffix) {
  std::string path_name{PKGLOGDIR "/"};
  path_name += log_stream;
  if (suffix != 0) path_name += std::string{"."} + std::to_string(suffix);
  return path_name;
}

uint64_t GetInode(int fd) {
  struct stat buf;
  int result = fstat(fd, &buf);
  return result == 0 ? buf.st_ino : ~UINT64_C(0);
}

bool PrettyPrint(FILE* stream) {
  bool result = true;
  for (;;) {
    char* s = fgets(buf, sizeof(buf), stream);
    if (s == nullptr) break;
    if (!PrettyPrint(s, strlen(buf))) result = false;
  }
  if (!feof(stream)) {
    fprintf(stderr, "Error reading from file\n");
    result = false;
  }
  return result;
}

bool PrettyPrint(const char* line, size_t size) {
  size_t date_size = 0;
  const char* date_field = Osaflog::GetField(line, size, 1, &date_size);
  size_t msg_size = 0;
  const char* msg_field = Osaflog::GetField(line, size, 8, &msg_size);
  char pretty_date[33];
  if (date_field == nullptr || date_size < 19 ||
      date_size >= sizeof(pretty_date) || date_field[10] != 'T' ||
      msg_field == nullptr || msg_size == 0)
    return false;
  memcpy(pretty_date, date_field, date_size);
  memset(pretty_date + date_size, '0', sizeof(pretty_date) - 1 - date_size);
  pretty_date[10] = ' ';
  pretty_date[19] = '.';
  pretty_date[23] = '\0';
  printf("%s %s", pretty_date, msg_field);
  return true;
}

}  // namespace
