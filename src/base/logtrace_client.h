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

#ifndef BASE_LOGTRACE_CLIENT_H_
#define BASE_LOGTRACE_CLIENT_H_

#include "base/log_message.h"
#include "base/unix_client_socket.h"
#include "base/buffer.h"
#include "base/conf.h"
#include "base/macros.h"
#include "base/mutex.h"

// A class implements trace/log client that communicates to the log server
// running in osaftransportd.
class TraceLog {
 public:
  enum WriteMode {
    kBlocking = base::UnixSocket::Mode::kBlocking,
    kNonblocking = base::UnixSocket::Mode::kNonblocking,
  };
  bool Init(const char *msg_id, WriteMode mode);
  void Log(base::LogMessage::Severity severity, const char *fmt,
                  va_list ap);
  TraceLog();
  ~TraceLog();

 private:
  void LogInternal(base::LogMessage::Severity severity, const char *fmt,
                     va_list ap);
  static constexpr const uint32_t kMaxSequenceId = uint32_t{0x7fffffff};
  base::LogMessage::HostName fqdn_{""};
  base::LogMessage::AppName app_name_{""};
  base::LogMessage::ProcId proc_id_{""};
  base::LogMessage::MsgId msg_id_{""};
  uint32_t sequence_id_;
  base::UnixClientSocket* log_socket_;
  base::Buffer<512> buffer_;
  base::Mutex* mutex_;
  DELETE_COPY_AND_MOVE_OPERATORS(TraceLog);
};

#endif  // BASE_LOGTRACE_CLIENT_H_
