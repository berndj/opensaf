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
#include "log/saf/saLog.h"
#include "base/mutex.h"
#include "log/agent/lga_common.h"

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
// thread, such as one client thread is performing writing log record,
// in the meantime, other client thread is closing that that log stream
// or closing the owning @LogClient. (! refer to @LogAgent description for more)
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
  // Increase one if @this object is not being deleted by other thread.
  // @updated will be set to true if there is an increase, false otherwise.
  int32_t FetchAndIncreaseRefCounter(bool* updated);

  // Fetch and decrease reference counter.
  // Decrease one if @this object is not being used or deleted by other thread.
  // @updated will be set to true if there is an decrease, false otherwise.
  int32_t FetchAndDecreaseRefCounter(bool* updated);

  // Restore the reference counter back.
  // Passing @value is either (1) [increase] or (-1) [decrease]
  void RestoreRefCounter(RefCounterDegree value, bool updated);

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

  // To protect @ref_counter_
  // E.g: if @this object is being used in @saLogStreamWrite
  // and other thread trying to delete it, will get TRY_AGAIN.
  base::Mutex ref_counter_mutex_;

  // Hold information how many thread are referring to this object
  // If the object is being deleted, the value is (-1).
  // If the object is not being deleted/used, the value is (0)
  // If there are N thread referring to this object, the counter will be N.
  int32_t ref_counter_;

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
inline LogStreamInfo&
LogStreamInfo::SetOpenFlags(SaLogStreamOpenFlagsT flags) {
  open_flags_ = flags;
  return *this;
}

// Set stream header type @header_type_
inline LogStreamInfo&
LogStreamInfo::SetHeaderType(SaLogHeaderTypeT type) {
  header_type_ = type;
  return *this;
}

inline LogStreamInfo&
LogStreamInfo::SetClientHandle(uint32_t h) {
  client_handle_ = h;
  return *this;
}

// Set recovery flag @recovered_flag_
inline void LogStreamInfo::SetRecoveryFlag(bool flag) {
  recovered_flag_ = flag;
}

#endif  // SRC_LOG_AGENT_LGA_STREAM_H_
