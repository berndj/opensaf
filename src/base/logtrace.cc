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
#include <signal.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include "base/getenv.h"
#include "base/logtrace_client.h"
#include "base/ncsgl_defs.h"

namespace global {

int category_mask;
const char *const prefix_name[] = {"EM", "AL", "CR", "ER", "WA", "NO", "IN",
                                   "DB", "TR", "T1", "T2", "T3", "T4", "T5",
                                   "T6", "T7", "T8", ">>", "<<"};
char *msg_id;
int logmask;
const char* osaf_log_file = "osaf.log";
bool enable_osaf_log = false;

}  // namespace global

TraceLog* gl_trace = nullptr;
TraceLog* gl_osaflog = nullptr;

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

void trace_output(const char *file, unsigned line, unsigned priority,
                     unsigned category, const char *format, va_list ap) {
  char preamble[288];

  assert(priority <= LOG_DEBUG && category < CAT_MAX);

  if (strncmp(file, "src/", 4) == 0) file += 4;
  snprintf(preamble, sizeof(preamble), "%d:%s:%u %s %s", gettid(), file, line,
           global::prefix_name[priority + category], format);
  TraceLog::Log(gl_trace, static_cast<base::LogMessage::Severity>(priority),
      preamble, ap);
}

void log_output(const char *file, unsigned line, unsigned priority,
                     unsigned category, const char *format, va_list ap) {
  char preamble[288];

  assert(priority <= LOG_DEBUG && category < CAT_MAX);

  if (strncmp(file, "src/", 4) == 0) file += 4;
  snprintf(preamble, sizeof(preamble), "%d:%s:%u %s %s", gettid(), file, line,
           global::prefix_name[priority + category], format);
  TraceLog::Log(gl_osaflog, static_cast<base::LogMessage::Severity>(priority),
      preamble, ap);
}

void logtrace_log(const char *file, unsigned line, int priority,
                  const char *format, ...) {
  va_list ap;
  va_list ap2;
  va_list ap3;

  va_start(ap, format);
  va_copy(ap2, ap);
  va_copy(ap3, ap);

  /* Only log to syslog if the env var OSAF_LOCAL_NODE_LOG is not set
   * Or with equal or higher priority than LOG_WARNING,
   */
  if (global::enable_osaf_log == false ||
      (global::enable_osaf_log && priority <= LOG_WARNING)) {
    char *tmp_str = nullptr;
    int tmp_str_len = 0;
    if ((tmp_str_len = asprintf(&tmp_str, "%s %s",
        global::prefix_name[priority], format)) < 0) {
      vsyslog(priority, format, ap);
    } else {
      vsyslog(priority, tmp_str, ap);
      free(tmp_str);
    }
  }
  /* Log to osaf_local_node_log file */
  if (global::enable_osaf_log == true) {
    log_output(file, line, priority, CAT_LOG, format, ap2);
  }
  /* Only output to file if configured to */
  if (global::category_mask & (1 << CAT_LOG)) {
    trace_output(file, line, priority, CAT_LOG, format, ap3);
  }
  va_end(ap);
  va_end(ap2);
  va_end(ap3);
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
  trace_output(file, line, LOG_DEBUG, category, format, ap);
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
    if (!gl_trace) gl_trace = new TraceLog();
    result = gl_trace->Init(global::msg_id, TraceLog::kBlocking);
  }
  if (base::GetEnv("OSAF_LOCAL_NODE_LOG", uint32_t{0}) == 1) {
    global::enable_osaf_log = true;
    if (!gl_osaflog) gl_osaflog = new TraceLog();
    gl_osaflog->Init(global::osaf_log_file, TraceLog::kBlocking);
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
    if (!gl_trace) gl_trace = new TraceLog();
    gl_trace->Init(global::msg_id, TraceLog::kBlocking);
    syslog(LOG_INFO, "logtrace: trace enabled to file %s, mask=0x%x",
           global::msg_id, global::category_mask);
  }

  return 0;
}

unsigned trace_category_get(void) { return global::category_mask; }
