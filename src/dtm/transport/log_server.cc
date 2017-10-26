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
#include <cstring>
#include "base/getenv.h"
#include "base/osaf_poll.h"
#include "base/time.h"
#include "osaf/configmake.h"

LogServer::LogServer(int term_fd)
    : term_fd_{term_fd},
      log_socket_{
          base::GetEnv<std::string>("pkglocalstatedir", PKGLOCALSTATEDIR) +
          "/osaf_log.sock"},
      log_streams_{},
      current_stream_{new LogStream{"mds.log", 1}},
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
      ssize_t result = log_socket_.Recv(buffer, LogWriter::kMaxMessageSize);
      if (result < 0) break;
      while (result != 0 && buffer[result - 1] == '\n') --result;
      if (static_cast<size_t>(result) != LogWriter::kMaxMessageSize) {
        buffer[result++] = '\n';
      } else {
        buffer[result - 1] = '\n';
      }
      size_t msg_id_size;
      const char* msg_id = GetField(buffer, result, 5, &msg_id_size);
      if (msg_id == nullptr) continue;
      LogStream* stream = GetStream(msg_id, msg_id_size);
      if (stream == nullptr) continue;
      if (stream != current_stream_) {
        memcpy(stream->current_buffer_position(), buffer, result);
        current_stream_ = stream;
      }
      current_stream_->Write(result);
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

const char* LogServer::GetField(const char* buf, size_t size, int field_no,
                                size_t* field_size) {
  while (field_no != 0) {
    const char* pos = static_cast<const char*>(memchr(buf, ' ', size));
    if (pos == nullptr) return nullptr;
    ++pos;
    size -= pos - buf;
    buf = pos;
    --field_no;
  }
  const char* pos = static_cast<const char*>(memchr(buf, ' ', size));
  *field_size = (pos != nullptr) ? (pos - buf) : size;
  return buf;
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
  LogStream* stream = new LogStream{log_name, 9};
  auto result = log_streams_.insert(
      std::map<std::string, LogStream*>::value_type{log_name, stream});
  if (!result.second) osaf_abort(msg_id_size);
  ++no_of_log_streams_;
  return stream;
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

LogServer::LogStream::LogStream(const std::string& log_name,
                                size_t no_of_backups)
    : log_name_{log_name}, last_flush_{}, log_writer_{log_name, no_of_backups} {
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
