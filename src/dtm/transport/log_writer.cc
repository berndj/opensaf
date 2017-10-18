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

#include "dtm/transport/log_writer.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include "base/getenv.h"
#include "osaf/configmake.h"

LogWriter::LogWriter(const std::string& log_name)
    : log_file_{base::GetEnv<std::string>("pkglogdir", PKGLOGDIR) + "/" +
                log_name},
      old_log_file_{log_file_ + ".1"},
      fd_{-1},
      current_file_size_{0},
      current_buffer_size_{0},
      buffer_{new char[kBufferSize]} {}

LogWriter::~LogWriter() {
  Flush();
  Close();
  delete[] buffer_;
}

void LogWriter::Open() {
  if (fd_ < 0) {
    int fd;
    do {
      fd = open(log_file(), O_WRONLY | O_CLOEXEC | O_CREAT,
                S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    } while (fd == -1 && errno == EINTR);
    if (fd >= 0) {
      off_t seek_result = lseek(fd, 0, SEEK_END);
      if (seek_result >= 0) current_file_size_ = seek_result;
      fd_ = fd;
    }
  }
}

void LogWriter::Close() {
  int fd = fd_;
  if (fd >= 0) {
    close(fd);
    fd_ = -1;
    current_file_size_ = 0;
  }
}

void LogWriter::RotateLog() {
  Close();
  unlink(old_log_file());
  if (rename(log_file(), old_log_file()) != 0) {
    unlink(log_file());
  }
}

void LogWriter::Write(size_t size) {
  current_buffer_size_ += size;
  if (current_buffer_size_ > kBufferSize - kMaxMessageSize) Flush();
}

void LogWriter::Flush() {
  size_t size = current_buffer_size_;
  current_buffer_size_ = 0;
  if (size == 0) return;
  if (fd_ < 0) Open();
  if (fd_ < 0) return;
  if (current_file_size_ >= kMaxFileSize) {
    RotateLog();
    if (fd_ < 0) Open();
    if (fd_ < 0) return;
  }
  size_t bytes_written = 0;
  while (bytes_written < size) {
    ssize_t result = write(fd_, buffer_ + bytes_written, size - bytes_written);
    if (result < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;
      break;
    }
    bytes_written += result;
  }
  current_file_size_ += bytes_written;
}
