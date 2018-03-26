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

#include "dtm/transport/log_server.h"
#include <signal.h>
#include <syslog.h>
#include <cstdlib>
#include <cstring>
#include "base/osaf_poll.h"
#include "base/time.h"
#include "dtm/common/osaflog_protocol.h"
#include "osaf/configmake.h"

const Osaflog::ClientAddressConstantPrefix LogServer::address_header_{};

LogServer::LogServer(int term_fd)
    : term_fd_{term_fd},
      no_of_backups_{9},
      max_file_size_{5 * 1024 * 1024},
      log_socket_{Osaflog::kServerSocketPath, base::UnixSocket::kNonblocking},
      log_streams_{},
      current_stream_{new LogStream{"mds.log", 1, 5 * 1024 * 1024}},
      no_of_log_streams_{1} {
  log_streams_["mds.log"] = current_stream_;
  }

LogServer::~LogServer() {
  for (const auto& s : log_streams_) delete s.second;
}

void LogServer::Run() {
  struct pollfd pfd[2] = {{term_fd_, POLLIN, 0}, {log_socket_.fd(), POLLIN, 0}};
  do {
    for (int i = 0; i < 256; ++i) {
      char* buffer = current_stream_->current_buffer_position();
      struct sockaddr_un src_addr;
      socklen_t addrlen = sizeof(src_addr);
      ssize_t result = log_socket_.RecvFrom(buffer, LogWriter::kMaxMessageSize,
                                            &src_addr, &addrlen);
      if (result < 0) break;
      if (result == 0 || buffer[0] != '?') {
        while (result != 0 && buffer[result - 1] == '\n') --result;
        if (static_cast<size_t>(result) != LogWriter::kMaxMessageSize) {
          buffer[result++] = '\n';
        } else {
          buffer[result - 1] = '\n';
        }
        size_t msg_id_size;
        const char* msg_id = Osaflog::GetField(buffer, result, 5, &msg_id_size);
        if (msg_id == nullptr) continue;
        LogStream* stream = GetStream(msg_id, msg_id_size);
        if (stream == nullptr) continue;
        if (stream != current_stream_) {
          memcpy(stream->current_buffer_position(), buffer, result);
          current_stream_ = stream;
        }
        current_stream_->Write(result);
      } else {
        ExecuteCommand(buffer, result, src_addr, addrlen);
      }
    }
    struct timespec current = base::ReadMonotonicClock();
    struct timespec last_flush = current;
    bool empty = true;
    for (const auto& s : log_streams_) {
      LogStream* stream = s.second;
      struct timespec flush = stream->last_flush();
      if ((current - flush) >= base::kFifteenSeconds) {
        stream->Flush();
      } else {
        if (flush < last_flush) last_flush = flush;
      }
      if (!stream->empty()) empty = false;
    }
    struct timespec timeout = (last_flush + base::kFifteenSeconds) - current;
    pfd[1].fd = log_socket_.fd();
    osaf_ppoll(pfd, 2, empty ? nullptr : &timeout, nullptr);
  } while ((pfd[0].revents & POLLIN) == 0);
}

LogServer::LogStream* LogServer::GetStream(const char* msg_id,
                                           size_t msg_id_size) {
  if (msg_id_size == current_stream_->log_name_size() &&
      memcmp(msg_id, current_stream_->log_name_data(), msg_id_size) == 0) {
    return current_stream_;
  }
  std::string log_name{msg_id, msg_id_size};
  auto iter = log_streams_.find(log_name);
  if (iter != log_streams_.end()) return iter->second;
  if (no_of_log_streams_ >= kMaxNoOfStreams) return nullptr;
  if (!ValidateLogName(msg_id, msg_id_size)) return nullptr;
  LogStream* stream = new LogStream{log_name, no_of_backups_, max_file_size_};
  auto result = log_streams_.insert(
      std::map<std::string, LogStream*>::value_type{log_name, stream});
  if (!result.second) osaf_abort(msg_id_size);
  ++no_of_log_streams_;
  return stream;
}

bool LogServer::ReadConfig(const char *transport_config_file) {
  FILE *transport_conf_file;
  char line[256];
  size_t max_file_size = 0;
  size_t number_of_backup_files = 0;
  int i, n, comment_line, tag_len = 0;

  /* Open transportd.conf config file. */
  transport_conf_file = fopen(transport_config_file, "r");
  if (transport_conf_file == nullptr) {
    syslog(LOG_ERR, "Not able to read transportd.conf: %s", strerror(errno));
    return false;
  }


  /* Read file. */
  while (fgets(line, 256, transport_conf_file)) {
    /* If blank line, skip it - and set tag back to 0. */
    if (strcmp(line, "\n") == 0) {
      continue;
    }

    /* If a comment line, skip it. */
    n = strlen(line);
    comment_line = 0;
    for (i = 0; i < n; i++) {
      if ((line[i] == ' ') || (line[i] == '\t')) {
        continue;
      } else if (line[i] == '#') {
        comment_line = 1;
        break;
      } else {
        break;
      }
    }
    if (comment_line) continue;
    line[n - 1] = 0;

    if (strncmp(line, "TRANSPORT_MAX_LOG_FILESIZE=",
                  strlen("TRANSPORT_MAX_LOG_FILESIZE=")) == 0) {
      tag_len = strlen("TRANSPORT_MAX_LOG_FILESIZE=");
      max_file_size = atoi(&line[tag_len]);

      if (max_file_size > 1) {
        max_file_size_ = max_file_size;
      }
    }

    if (strncmp(line, "TRANSPORT_NO_OF_BACKUP_LOG_FILES=",
                  strlen("TRANSPORT_NO_OF_BACKUP_LOG_FILES=")) == 0) {
      tag_len = strlen("TRANSPORT_NO_OF_BACKUP_LOG_FILES=");
      number_of_backup_files = atoi(&line[tag_len]);

      if (number_of_backup_files > 1) {
         no_of_backups_ = number_of_backup_files;
      }
    }
  }

  /* Close file. */
  fclose(transport_conf_file);

  return true;
}

bool LogServer::ValidateLogName(const char* msg_id, size_t msg_id_size) {
  if (msg_id_size < 1 || msg_id_size > LogStream::kMaxLogNameSize) return false;
  if (msg_id[0] == '.') return false;
  size_t no_of_dots = 0;
  for (size_t i = 0; i != msg_id_size; ++i) {
    char c = msg_id[i];
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
          (c >= '0' && c <= '9') || (c == '.' || c == '_')))
      return false;
    if (c == '.') ++no_of_dots;
  }
  return no_of_dots < 2;
}

void LogServer::ExecuteCommand(const char* request, size_t size,
                               const struct sockaddr_un& addr,
                               socklen_t addrlen) {
  if (ValidateAddress(addr, addrlen)) {
    size_t command_size;
    size_t argument_size;
    const char* command = Osaflog::GetField(request, size, 0, &command_size);
    const char* argument = Osaflog::GetField(request, size, 1, &argument_size);
    std::string reply = ExecuteCommand(command != nullptr
                                       ? std::string{command, command_size}
                                       : std::string{},
                                       argument != nullptr
                                       ? std::string{argument, argument_size}
                                       : std::string{});
    log_socket_.SendTo(reply.data(), reply.size(), &addr, addrlen);
  }
}

bool LogServer::ValidateAddress(const struct sockaddr_un& addr,
                                socklen_t addrlen) {
  if (addrlen == sizeof(Osaflog::ClientAddress)) {
    return memcmp(&addr, &address_header_, sizeof(address_header_)) == 0;
  } else {
    return false;
  }
}

std::string LogServer::ExecuteCommand(const std::string& command,
                                      const std::string& argument) {
  if (command == "?max-file-size") {
    max_file_size_ = atoi(argument.c_str());
    return std::string{"!max-file-size " + std::to_string(max_file_size_)};
  } else if (command == "?max-backups") {
    no_of_backups_ = atoi(argument.c_str());
    return std::string{"!max-backups " + std::to_string(no_of_backups_)};
  } else if (command == "?flush") {
    for (const auto& s : log_streams_) {
      LogStream* stream = s.second;
      stream->Flush();
    }
    return std::string{"!flush"};
  } else {
    return std::string{"!not_supported"};
  }
}

LogServer::LogStream::LogStream(const std::string& log_name,
                                size_t no_of_backups_, size_t max_file_size_)
    : log_name_{log_name}, last_flush_{}, log_writer_{log_name, no_of_backups_,
                                                             max_file_size_} {
  if (log_name.size() > kMaxLogNameSize) osaf_abort(log_name.size());
}

void LogServer::LogStream::Write(size_t size) {
  if (log_writer_.empty()) last_flush_ = base::ReadMonotonicClock();
  log_writer_.Write(size);
}

void LogServer::LogStream::Flush() {
  log_writer_.Flush();
  last_flush_ = base::ReadMonotonicClock();
}
