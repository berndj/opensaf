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
 * Author(s): Ericsson AB
 *
 */

#ifndef SRC_LOG_AGENT_LGA_STREAM_H_
#define SRC_LOG_AGENT_LGA_STREAM_H_

#include <string>
#include <atomic>
#include <saLog.h>
#include "base/mutex.h"
#include "log/agent/lga_common.h"
#include "log/agent/lga_ref_counter.h"

//<
// @LogStreamInfo class
//
// Below is simple composition, showing relations b/w objects.
// [LogAgent (1)] ------> [LogClient (0..n)] -------> [LogStreamInfo (0..n)]
// Note: (n)/(n..m) - show the number of objects
//
// !!! Refer to @LogAgent and @LogClient class description to see more !!!
//
// After adding @this LogStreamInfo object to the database of @LogClient,
// The proper way to retrieve @this object is via SearchLogStreamInfoByHandle(),
// Then should fetch and update ref counter of this object.
//
// The class contains necessary informations about log stream which
// is created after successfully calling saLogStreamOpen().
// Such as client handle @client_handle_ that owns this log stream,
// log stream name, etc.
//
// Each @LogStreamInfo object belongs to its specific @LogClient owner.
// Even two or more @LogClient open the same log stream, they have its
// own @LogStreamInfo memory, no memory share among clients.
//
// When writing log record to this log stream, @LogAgent need to retrieve the
// @LogClient object which owns the log stream to see that @LogClient is
// existing or not based on @client_handle_.
// As @LogStreamInfo belongs to a specific @LogClient, this class is friend of
// @LogClient.
//
// To avoid @this LogStreamInfo object deleted while it is being used by other
// thread, such as one client thread is performing writing log record, in the
// meantime, other client thread is closing that that log stream or closing the
// owning @LogClient. (! refer to @LogAgent description for more) , one special
// object `RefCounter` is included in. Each @LogStreamInfo object owns one
// ref_counter_object_, that object should be updated when this object is
// using, modified or deleted.  FetchAnd<xxx>RefCounter()/RestoreRefCounter()
// are methods to update ref_counter_object_ object.
//
// When SC restarts from headless, @this object will be notified to do
// recover itself for specific @LogClient Id.
//>
class LogStreamInfo {
 public:
  LogStreamInfo(const std::string&, uint32_t);
  ~LogStreamInfo();

  // Get log stream handle @handle_ which is allocated after
  // successfully calling saLogStreamOpen().
  SaLogStreamHandleT GetHandle() const { return handle_; }

  // Get @client_handle_ of @LogClient which is the owner of @this stream.
  SaLogHandleT GetMyClientHandle() const { return client_handle_; }

  // Get stream id @stream_id_ which is allocated by LOG server
  // after successfully calling saLogStreamOpen()
  uint32_t GetStreamId() const { return stream_id_; }

  // Get stream header type @header_type_ of @this log stream
  SaLogHeaderTypeT GetHeaderType() const { return header_type_; }

  // Get stream name @stream_name_ of this log stream
  const std::string& GetStreamName() const { return stream_name_; }

  // Fetch and increase reference counter.
  int32_t FetchAndIncreaseRefCounter(const char* caller, bool* updated) {
    return ref_counter_object_.FetchAndIncreaseRefCounter(caller, updated);
  }

  // Fetch and decrease reference counter.
  int32_t FetchAndDecreaseRefCounter(const char* caller, bool* updated) {
    return ref_counter_object_.FetchAndDecreaseRefCounter(caller, updated);
  }

  // Restore the reference counter back.
  void RestoreRefCounter(const char* caller, RefCounter::Degree value,
                         bool updated) {
    return ref_counter_object_.RestoreRefCounter(caller, value, updated);
  }

 private:
  // Set stream open flags @open_flags_
  LogStreamInfo& SetOpenFlags(SaLogStreamOpenFlagsT);

  // Set stream header type @header_type_
  LogStreamInfo& SetHeaderType(SaLogHeaderTypeT);

  // Set recovery flag @recovered_flag_
  void SetRecoveryFlag(bool);

  // Set @client_handle_ for @this stream obj
  LogStreamInfo& SetClientHandle(uint32_t h);

  // Recover @this log stream
  bool RecoverMeForClientId(uint32_t);

  // Send open log stream msg Log server
  bool SendOpenStreamMsg(uint32_t);

 private:
  // Log stream name mentioned during open log stream
  std::string stream_name_;

  // Hold information how many thread are referring to this object
  // Refer to `RefCounter` class for more info.
  RefCounter ref_counter_object_;

  // Log stream open flags as defined in AIS.02.01
  SaLogStreamOpenFlagsT open_flags_;

  // Log header type
  SaLogHeaderTypeT header_type_;

  // Server reference for this log stream
  uint32_t stream_id_;

  // Client handle in which this @stream belongs to
  SaLogHandleT client_handle_;

  // Log stream HDL from handle mgr
  SaLogStreamHandleT handle_;

  // This flag is used with recovery handling. It is valid only
  // after server down has happened (will be initiated when server down
  // event occurs). It's not valid in LGA_NORMAL state.
  std::atomic<bool> recovered_flag_;

  friend class LogClient;
  DELETE_COPY_AND_MOVE_OPERATORS(LogStreamInfo);
};

// Set stream open flags @open_flags_
inline LogStreamInfo& LogStreamInfo::SetOpenFlags(SaLogStreamOpenFlagsT flags) {
  open_flags_ = flags;
  return *this;
}

// Set stream header type @header_type_
inline LogStreamInfo& LogStreamInfo::SetHeaderType(SaLogHeaderTypeT type) {
  header_type_ = type;
  return *this;
}

inline LogStreamInfo& LogStreamInfo::SetClientHandle(uint32_t h) {
  client_handle_ = h;
  return *this;
}

// Set recovery flag @recovered_flag_
inline void LogStreamInfo::SetRecoveryFlag(bool flag) {
  recovered_flag_ = flag;
}

#endif  // SRC_LOG_AGENT_LGA_STREAM_H_
