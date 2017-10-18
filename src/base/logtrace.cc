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
#include "osaf/configmake.h"

static int trace_fd = -1;
static int category_mask;
static const char *prefix_name[] = {"EM", "AL", "CR", "ER", "WA", "NO", "IN",
                                    "DB", "TR", "T1", "T2", "T3", "T4", "T5",
                                    "T6", "T7", "T8", ">>", "<<"};

static const char *ident;
static const char *pathname;
static int logmask;

static pid_t gettid() { return syscall(SYS_gettid); }

/**
 * USR2 signal handler to enable/disable trace (toggle)
 * @param sig
 */
static void sigusr2_handler(int sig) {
  unsigned trace_mask;

  if (category_mask == 0)
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
  if ((logmask & LOG_MASK(LOG_INFO)) & LOG_MASK(LOG_INFO)) {
    logmask = LOG_UPTO(LOG_NOTICE);
    syslog(LOG_NOTICE, "logtrace: info level disabled");
  } else {
    logmask = LOG_UPTO(LOG_INFO);
    syslog(LOG_NOTICE, "logtrace: info level enabled");
  }

  setlogmask(logmask);
}

void logtrace_output(const char *file, unsigned line, int priority,
                     int category, const char *format, va_list ap) {
  int i;
  struct timeval tv;
  char preamble[512];
  char log_string[1024];
  struct tm *tstamp_data, tm_info;

  assert(priority <= LOG_DEBUG && category < CAT_MAX);

  /* Create a nice syslog looking date string */
  gettimeofday(&tv, nullptr);
  tstamp_data = localtime_r(&tv.tv_sec, &tm_info);
  osafassert(tstamp_data);

  strftime(log_string, sizeof(log_string), "%b %e %k:%M:%S", tstamp_data);
  i = snprintf(preamble, sizeof(preamble), "%s.%06ld %s ", log_string,
               tv.tv_usec, ident);

  snprintf(&preamble[i], sizeof(preamble) - i, "[%d:%d:%s:%04u] %s %s",
           getpid(), gettid(), file, line, prefix_name[priority + category],
           format);
  i = vsnprintf(log_string, sizeof(log_string), preamble, ap);

  /* Check if the logtrace user had passed message length >= logtrace
   * array limit of 1023. If so, prepare/add space for line feed and
   * truncation character 'T'.
   */
  if (i >= 1023) {
    i = 1023;
    log_string[i - 2] = 'T';
    log_string[i - 1] = '\n';
    log_string[i] = '\0';  //
  } else {
    /* Add line feed if not there already */
    if (log_string[i - 1] != '\n') {
      log_string[i] = '\n';
      log_string[i + 1] = '\0';
      i++;
    }
  }

  /* If we got here without a file descriptor, trace was enabled in
   * runtime, open the file */
  if (trace_fd == -1) {
    trace_fd = open(pathname, O_WRONLY | O_APPEND | O_CREAT,
                    S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (trace_fd < 0) {
      syslog(LOG_ERR, "logtrace: open failed, file=%s (%s)", pathname,
             strerror(errno));
      return;
    }
  }

write_retry:
  i = write(trace_fd, log_string, i);
  if (i == -1) {
    if (errno == EAGAIN)
      goto write_retry;
    else
      syslog(LOG_ERR, "logtrace: write failed, %s", strerror(errno));
  }
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

  if ((tmp_str_len =
           asprintf(&tmp_str, "%s %s", prefix_name[priority], format)) < 0) {
    vsyslog(priority, format, ap);
  } else {
    vsyslog(priority, tmp_str, ap);
    free(tmp_str);
  }

  /* Only output to file if configured to */
  if (!(category_mask & (1 << CAT_LOG))) goto done;

  logtrace_output(file, line, priority, CAT_LOG, format, ap2);

done:
  va_end(ap);
  va_end(ap2);
}

bool is_logtrace_enabled(unsigned category) {
  /* Filter on category */
  return (category_mask & (1 << category)) != 0;
}

void logtrace_trace(const char *file, unsigned line, unsigned category,
                    const char *format, ...) {
  va_list ap;

  if (is_logtrace_enabled(category) == false) return;

  va_start(ap, format);
  logtrace_output(file, line, LOG_DEBUG, category, format, ap);
  va_end(ap);
}

int logtrace_init(const char *_ident, const char *_pathname, unsigned _mask) {
  ident = _ident;
  pathname = strdup(_pathname);
  category_mask = _mask;

  tzset();

  if (_mask != 0) {
    trace_fd = open(pathname, O_WRONLY | O_APPEND | O_CREAT,
                    S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (trace_fd < 0) {
      syslog(LOG_ERR, "logtrace: open failed, file=%s (%s)", pathname,
             strerror(errno));
      return -1;
    }

    syslog(LOG_INFO, "logtrace: trace enabled to file %s, mask=0x%x", pathname,
           category_mask);
  }

  return 0;
}

int logtrace_init_daemon(const char *_ident, const char *_pathname,
                         unsigned _tracemask, int _logmask) {
  if (signal(SIGUSR2, sigusr2_handler) == SIG_ERR) {
    syslog(LOG_ERR, "logtrace: registering SIGUSR2 failed, (%s)",
           strerror(errno));
    return -1;
  }

  setlogmask(_logmask);

  if (signal(SIGHUP, sighup_handler) == SIG_ERR) {
    syslog(LOG_ERR, "logtrace: registering SIGHUP failed, (%s)",
           strerror(errno));
    return -1;
  }

  logmask = _logmask;

  return logtrace_init(_ident, _pathname, _tracemask);
}

int trace_category_set(unsigned mask) {
  category_mask = mask;

  if (category_mask == 0) {
    if (trace_fd != -1) {
      close(trace_fd);
      trace_fd = -1;
    }
    syslog(LOG_INFO, "logtrace: trace disabled");
  } else
    syslog(LOG_INFO, "logtrace: trace enabled to file %s, mask=0x%x", pathname,
           category_mask);

  return 0;
}

unsigned trace_category_get(void) { return category_mask; }
