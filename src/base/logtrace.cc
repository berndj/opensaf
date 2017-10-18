/*      -*- OpenSAF  -*-
 *
 * (C) Copyright 2008-2010 The OpenSAF Foundation
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
 *            Wind River Systems
 *
 */

#include "base/logtrace.h"
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include <cassert>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "base/buffer.h"
#include "base/conf.h"
#include "base/log_message.h"
#include "base/macros.h"
#include "base/mutex.h"
#include "base/ncsgl_defs.h"
#include "base/osaf_utility.h"
#include "base/time.h"
#include "base/unix_client_socket.h"
#include "osaf/configmake.h"

namespace global {

int category_mask;
const char *const prefix_name[] = {"EM", "AL", "CR", "ER", "WA", "NO", "IN",
                                   "DB", "TR", "T1", "T2", "T3", "T4", "T5",
                                   "T6", "T7", "T8", ">>", "<<"};
char *msg_id;
int logmask;

}  // namespace global

class TraceLog {
 public:
  static bool Init();
  static void Log(base::LogMessage::Severity severity, const char *fmt,
                  va_list ap);

 private:
  TraceLog(const std::string &fqdn, const std::string &app_name,
           uint32_t proc_id, const std::string &msg_id,
           const std::string &socket_name);
  void LogInternal(base::LogMessage::Severity severity, const char *fmt,
                   va_list ap);
  static constexpr const uint32_t kMaxSequenceId = uint32_t{0x7fffffff};
  static TraceLog *instance_;
  const base::LogMessage::HostName fqdn_;
  const base::LogMessage::AppName app_name_;
  const base::LogMessage::ProcId proc_id_;
  const base::LogMessage::MsgId msg_id_;
  uint32_t sequence_id_;
  base::UnixClientSocket log_socket_;
  base::Buffer<512> buffer_;
  base::Mutex mutex_;

  DELETE_COPY_AND_MOVE_OPERATORS(TraceLog);
};

TraceLog *TraceLog::instance_ = nullptr;

TraceLog::TraceLog(const std::string &fqdn, const std::string &app_name,
                   uint32_t proc_id, const std::string &msg_id,
                   const std::string &socket_name)
    : fqdn_{base::LogMessage::HostName{fqdn}},
      app_name_{base::LogMessage::AppName{app_name}},
      proc_id_{base::LogMessage::ProcId{std::to_string(proc_id)}},
      msg_id_{base::LogMessage::MsgId{msg_id}},
      sequence_id_{1},
      log_socket_{socket_name},
      buffer_{},
      mutex_{} {}

bool TraceLog::Init() {
  if (instance_ != nullptr) return false;
  char app_name[49];
  char pid_path[1024];
  uint32_t process_id = getpid();
  char *token, *saveptr;
  char *pid_name = nullptr;

  memset(app_name, 0, 49);
  memset(pid_path, 0, 1024);

  snprintf(pid_path, sizeof(pid_path), "/proc/%" PRIu32 "/cmdline", process_id);
  FILE *f = fopen(pid_path, "r");
  if (f != nullptr) {
    size_t size;
    size = fread(pid_path, sizeof(char), 1024, f);
    if (size > 0) {
      if ('\n' == pid_path[size - 1]) pid_path[size - 1] = '\0';
    }
    fclose(f);
  } else {
    pid_path[0] = '\0';
  }
  token = strtok_r(pid_path, "/", &saveptr);
  while (token != nullptr) {
    pid_name = token;
    token = strtok_r(nullptr, "/", &saveptr);
  }
  if (snprintf(app_name, sizeof(app_name), "%s", pid_name) < 0) {
    app_name[0] = '\0';
  }
  base::Conf::InitFullyQualifiedDomainName();
  const std::string &fqdn = base::Conf::FullyQualifiedDomainName();
  instance_ = new TraceLog{fqdn, app_name, process_id, global::msg_id,
                           PKGLOCALSTATEDIR "/osaf_log.sock"};
  return instance_ != nullptr;
}

void TraceLog::Log(base::LogMessage::Severity severity, const char *fmt,
                   va_list ap) {
  if (instance_ != nullptr) instance_->LogInternal(severity, fmt, ap);
}

void TraceLog::LogInternal(base::LogMessage::Severity severity, const char *fmt,
                           va_list ap) {
  base::Lock lock(mutex_);
  uint32_t id = sequence_id_;
  sequence_id_ = id < kMaxSequenceId ? id + 1 : 1;
  buffer_.clear();
  base::LogMessage::Write(
      base::LogMessage::Facility::kLocal1, severity, base::ReadRealtimeClock(),
      fqdn_, app_name_, proc_id_, msg_id_,
      {{base::LogMessage::SdName{"meta"},
        {base::LogMessage::Parameter{base::LogMessage::SdName{"sequenceId"},
                                     std::to_string(id)}}}},
      fmt, ap, &buffer_);
  log_socket_.Send(buffer_.data(), buffer_.size());
}

static pid_t gettid() { return syscall(SYS_gettid); }

/**
 * USR2 signal handler to enable/disable trace (toggle)
 * @param sig
 */
static void sigusr2_handler(int sig) {
  unsigned trace_mask;

  if (global::category_mask == 0)
    trace_mask = CATEGORY_ALL;
  else
    trace_mask = 0;

  trace_category_set(trace_mask);
}

/**
 * HUP signal handler to toggle info log level on/off
 * @param sig
 */
static void sighup_handler(int sig) {
  if ((global::logmask & LOG_MASK(LOG_INFO)) & LOG_MASK(LOG_INFO)) {
    global::logmask = LOG_UPTO(LOG_NOTICE);
    syslog(LOG_NOTICE, "logtrace: info level disabled");
  } else {
    global::logmask = LOG_UPTO(LOG_INFO);
    syslog(LOG_NOTICE, "logtrace: info level enabled");
  }

  setlogmask(global::logmask);
}

void logtrace_output(const char *file, unsigned line, unsigned priority,
                     unsigned category, const char *format, va_list ap) {
  char preamble[288];

  assert(priority <= LOG_DEBUG && category < CAT_MAX);

  snprintf(preamble, sizeof(preamble), "[%d:%s:%04u] %s %s", gettid(), file,
           line, global::prefix_name[priority + category], format);
  TraceLog::Log(static_cast<base::LogMessage::Severity>(priority), preamble,
                ap);
}

void logtrace_log(const char *file, unsigned line, int priority,
                  const char *format, ...) {
  va_list ap;
  va_list ap2;

  /* Uncondionally send to syslog */
  va_start(ap, format);
  va_copy(ap2, ap);

  char *tmp_str = nullptr;
  int tmp_str_len = 0;

  if ((tmp_str_len = asprintf(&tmp_str, "%s %s", global::prefix_name[priority],
                              format)) < 0) {
    vsyslog(priority, format, ap);
  } else {
    vsyslog(priority, tmp_str, ap);
    free(tmp_str);
  }

  /* Only output to file if configured to */
  if (!(global::category_mask & (1 << CAT_LOG))) goto done;

  logtrace_output(file, line, priority, CAT_LOG, format, ap2);

done:
  va_end(ap);
  va_end(ap2);
}

bool is_logtrace_enabled(unsigned category) {
  /* Filter on category */
  return (global::category_mask & (1 << category)) != 0;
}

void logtrace_trace(const char *file, unsigned line, unsigned category,
                    const char *format, ...) {
  va_list ap;

  if (is_logtrace_enabled(category) == false) return;

  va_start(ap, format);
  logtrace_output(file, line, LOG_DEBUG, category, format, ap);
  va_end(ap);
}

int logtrace_init(const char *, const char *pathname, unsigned mask) {
  if (global::msg_id != nullptr) return -1;
  char *tmp = strdup(pathname);
  if (tmp != nullptr) {
    global::msg_id = strdup(basename(tmp));
    free(tmp);
  }
  global::category_mask = mask;

  tzset();

  bool result = global::msg_id != nullptr;
  if (!result) {
    free(global::msg_id);
    global::msg_id = nullptr;
  }
  if (result && mask != 0) {
    result = TraceLog::Init();
  }

  if (result) {
    syslog(LOG_INFO, "logtrace: trace enabled to file '%s', mask=0x%x",
           global::msg_id, global::category_mask);
  } else {
    syslog(LOG_ERR,
           "logtrace: failed to enable logtrace to file '%s', mask=0x%x",
           global::msg_id, global::category_mask);
  }

  return result ? 0 : -1;
}

int logtrace_init_daemon(const char *ident, const char *pathname,
                         unsigned tracemask, int logmask) {
  if (signal(SIGUSR2, sigusr2_handler) == SIG_ERR) {
    syslog(LOG_ERR, "logtrace: registering SIGUSR2 failed, (%s)",
           strerror(errno));
    return -1;
  }

  setlogmask(logmask);

  if (signal(SIGHUP, sighup_handler) == SIG_ERR) {
    syslog(LOG_ERR, "logtrace: registering SIGHUP failed, (%s)",
           strerror(errno));
    return -1;
  }

  global::logmask = logmask;

  return logtrace_init(ident, pathname, tracemask);
}

int trace_category_set(unsigned mask) {
  global::category_mask = mask;

  if (global::category_mask == 0) {
    syslog(LOG_INFO, "logtrace: trace disabled");
  } else {
    TraceLog::Init();
    syslog(LOG_INFO, "logtrace: trace enabled to file %s, mask=0x%x",
           global::msg_id, global::category_mask);
  }

  return 0;
}

unsigned trace_category_get(void) { return global::category_mask; }
