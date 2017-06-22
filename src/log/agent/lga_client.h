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

#ifndef SRC_LOG_AGENT_LGA_CLIENT_H_
#define SRC_LOG_AGENT_LGA_CLIENT_H_

#include <stdint.h>
#include <vector>
#include <atomic>
#include "base/mutex.h"
#include "mds/mds_papi.h"
#include <saLog.h>
#include "log/agent/lga_stream.h"
#include "log/common/lgsv_msg.h"
#include "log/common/lgsv_defs.h"
#include "log/agent/lga_state.h"

//>
// @LogClient class
//
// Below is simple composition, showing relations b/w objects.
// [LogAgent (1)] ------> [LogClient (0..n)] -------> [LogStreamInfo (0..n)]
// Note: (n)/(n..m) - show the number of objects
//
// !!! Refer to @LogAgent class description to see more !!!
//
// @LogClient object is created after successfully calling saLogInitialize()
// And it is added to the list of clients database managed by @LogAgent.
//
// After adding @this LogClient object to the database of @LogAgent,
// The proper way to retrieve @this object is via SearchClientById()
// or SeachClientByHandle(), and the next call MUST be
// FetchAndUpdateObjectState() on that object to update its ref counter.
//
// This object contains lot of important information about the log client,
// such as the mailbox @mailbox_ for putting all MDS msg which belong to
// @this client, the callbacks registered to @this client,
// or flag to know whether @this client is recovered after headless, or
// @this client is on cluster membership node or not, etc.
//
// Each @LogClient object will hold a database of its opened log streams.
// Log stream object will be added to the database after calling
// @saLogStreamOpen successfully, and removed from the database
// when @saLogStreamClose is called successfully.
//
// And each @LogClient object has one special attribute, called @ref_counter_,
// that attribute should be updated when it is refered, modify or deleting.
// FetchAndUpdateObjectState()/RestoreObjectState() are methods to
// increase/restore the @ref_counter_
//
// @LogClient object can be deleted ONLY if no log stream which it owns
// are in "being use" state (@ref_counter > 0).
// When @this client is freed, all resources allocated to @this client
// will be freed including mailbox, log stream objects and other ones.
//
// @RecoverMe() method is provided to let LOG client thread or Recovery thread
// do recovery for @this client. @LogClient only recover if not recovered yet.
//
// Once getting @NCSMDS_DOWN event from MDS, @LogClient should be received that
// information from @LogAgent via the method @NoLogServer().
//<
class LogClient {
 public:
  LogClient(const SaLogCallbacksT*, uint32_t, SaVersionT);
  ~LogClient();

  // Get LOG client handle @handle_ of @this client
  // which is allocated after calling @saLogInitialize() API.
  SaLogHandleT GetHandle() const { return handle_; }

  // Get LOG client ID @client_id_ of @this client
  // which is provided by LOG server.
  uint32_t GetClientId() const { return client_id_; }

  // Do recovery for @this log client after SC restarts from headless.
  // This method should be called in LOG client thread if recovery state
  // is in kRecover1, and in Recovery thread if the recovery state
  // is kRecovery2.
  bool RecoverMe();

  // This method is used to tell @this client that SC is absent,
  // so that, @this client knows what to do on its data.
  void NoLogServer();

  // To check if any log stream which belong to @this client is being used.
  // Return true if it does, otherwise false.
  // Retrieve this information whenever having attemp deleting @this client,
  // to avoid deleting client data while its owning log stream is being used.
  bool HaveLogStreamInUse();

  // Add @stream from @stream_list_ and should be called
  // when getting @saLogStreamOpen()
  // Get @mbx selection object of @this client
  SaSelectionObjectT GetSelectionObject();

  // Add a log stream with its additional data to @stream_list_.
  // This action is invoked right after successful saLogStreamOpen()
  // on @this log client.
  void AddLogStreamInfo(
      LogStreamInfo*,
      SaLogStreamOpenFlagsT,
      SaLogHeaderTypeT);

  // Remove log stream @stream from the @stream_list_.
  // The method is called after getting @saLogStreamClose() or @saLogFinalize().
  // Using pointer to pointer parameter, so that resetting the pointer to
  // nullptr also take effective to the caller.
  void RemoveLogStreamInfo(LogStreamInfo**);

  // Dispatch message from the @this client mailbox.
  // This method should be called if having event on its selection object (fd).
  SaAisErrorT Dispatch(SaDispatchFlagsT flag);

  // Get pointer to registered callbacks @callbacks_ of @this client
  const SaLogCallbacksT* GetCallback() const { return &callbacks_; }

  // Send the just received msg to @this client @mailbox_
  // with specific priority.
  uint32_t SendMsgToMbx(lgsv_msg_t*, MDS_SEND_PRIORITY_TYPE);

  // True if @this client is initialized.
  // Note that, when headless, the flag @initialized_flag will be set to false.
  bool is_client_initialized() const;

  // When the node which @this client is operating on leave the cluster
  // this flag will be set to true if the client version is interested in CLM.
  void atomic_set_stale_flag(bool value);

  // Get the flag @stale_flag of @this client to see whether this client
  // is cluster membership or not.
  // Most of LOG APIs will return SA_AIS_ERR_UNAVAILABLE if the flag is true.
  bool is_stale_client() const;

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

  // true if @this client does NOT recovery succcessfully.
  // false, otherwise.
  bool is_recovery_failed() const;

  // true if the client is successfully done recovery.
  // or the client has just borned.
  // Introduce this method to avoid locking the successful recovered client
  // while the recovery thread is going at RecoveryState::kRecovery2.
  bool is_recovery_done() const;

 private:
  // Search for @LogStreamInfo object from @stream_list_ based on @stream_id
  LogStreamInfo* SearchLogStreamInfoById(uint32_t);

  // True if @this client is initialized with version
  // which have CLM supported. Otherwise, false.
  bool is_interested_in_clm() const;

  // This method is used in headless mode.
  // To send client initialization to log server.
  bool SendInitializeMsg();

  // Do recovery for @this client
  bool InitializeClient();

  // Dispatch one for message coming to @this client mailbox
  SaAisErrorT DispatchOne();

  // Dispatch all for message coming to @this client mailbox
  SaAisErrorT DispatchAll();

  // Dispatch blocking for message coming to @this client mailbox
  SaAisErrorT DispatchBlocking();

  // Invoke the registered callback
  void InvokeCallback(const lgsv_msg_t* msg);

  // Delete all messages from the mailbox
  static bool ClearMailBox(NCSCONTEXT, NCSCONTEXT);

 private:
  // Handle value returned by LGS for this client
  uint32_t client_id_;

  // Callbacks registered by the application
  SaLogCallbacksT callbacks_;

  // Priority q mbx b/w MDS & Library
  SYSF_MBX mailbox_;

  //>
  // These atomic flags are used with recovery handling. They are valid only
  // after server down has happened (will be initiated when server down
  // event occurs). They are not valid in kNormal state
  //<
  struct AtomicData {
    // Status of client based on the CLM status of node.
    // As client can initialize with different version, then
    // this flag should go with @LogClient object.
    std::atomic<bool> stale_flag;

    // Used with "headless" recovery handling. Set when client is initialized.
    std::atomic<bool> initialized_flag;

    // false - means the headless has been occured and @this client
    // need to do recovery after LOG server is up from headless.
    // true - means this client has been recovered successfully.
    std::atomic<bool> recovered_flag;

    AtomicData() : stale_flag{false}
                 , initialized_flag{true}
                 , recovered_flag{true} {}
  };

  // Hold all atomic data
  AtomicData atomic_data_;

  // Introduce this flag to centralize deleting @this object
  // in one place - LOG application thread.
  // Originally, @this object would be deleted in Recovery thread
  // if failed to recover. Do so could have race condition with
  // LOG application thread.
  bool fail_to_recover_;

  // The API version is being used by client, used for CLM status
  SaVersionT version_;

  // Used to synchronize adding/removing to/from @stream_list_
  // MUST use this mutex whenever accessing to stream from @stream_list_
  // Using native mutex as base::Mutex does not support RECURSIVE.
  pthread_mutex_t mutex_;

  // Hold information how many thread are referring to this object
  // If the object is being deleted, the value is (-1).
  // If the object is not being deleted/used, the value is (0)
  // If there are N thread referring to this object, the counter will be N.
  int32_t ref_counter_;

  // Dedicate an own mutex to protect @ref_counter_.
  // The reason not sharing the @mutex_ is to avoid deadlock with MDS thread
  // E.g: lock(mutex_) --> send MDS sync --> receiving ack from MDS --> deadlock
  pthread_mutex_t ref_counter_mutex_;

  // Hold all log streams belong to @this client
  std::vector<LogStreamInfo*> stream_list_;

  // LOG handle (derived from hdl-mngr)
  SaLogHandleT handle_;

  DELETE_COPY_AND_MOVE_OPERATORS(LogClient);
};

//------------------------------------------------------------------------------
// LogClient inline methods
//------------------------------------------------------------------------------

// Set @stale_flag for @this client
inline void LogClient::atomic_set_stale_flag(bool value) {
  // ONLY change the stale if this client version has interested in CLM
  if (is_interested_in_clm() == false) return;

  atomic_data_.stale_flag = value;
}

// Get @stale_flag of @this client
inline bool LogClient::is_stale_client() const {
  return atomic_data_.stale_flag.load();
}

// Get @initialized_flag
inline bool LogClient::is_client_initialized() const {
  return atomic_data_.initialized_flag;
}

inline bool LogClient::is_interested_in_clm() const {
  return (!(
      (version_.releaseCode  == LOG_RELEASE_CODE_0) &&
      (version_.majorVersion == LOG_MAJOR_VERSION_0) &&
      (version_.minorVersion == LOG_MINOR_VERSION_0)));
}

// There is no recovery process going at the moment, and the recovery flag
// is false, it tells us that the recovery for this client failed.
// The failed recovery client is going to be delete by LOG client
// thread if any request to this @LogClient.
inline bool LogClient::is_recovery_failed() const {
  return ((atomic_data_.recovered_flag == false) &&
          (is_lga_recovery_state(RecoveryState::kNormal)));
}

inline bool LogClient::is_recovery_done() const {
  return atomic_data_.recovered_flag;
}

#endif  // SRC_LOG_AGENT_LGA_CLIENT_H_
