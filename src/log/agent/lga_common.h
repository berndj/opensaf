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

#ifndef SRC_LOG_AGENT_LGA_COMMON_H_
#define SRC_LOG_AGENT_LGA_COMMON_H_

#include <pthread.h>
#include "base/osaf_utility.h"
#include "base/macros.h"

// LOG server state. The state changes according to getting MDS events.
enum class LogServerState {
  // No Active LOG server. Could happen a short time during failover/switchover.
  kNoActiveLogServer = 0,
  // No LOG server at all, including standby LOG server. Both SCs are absent.
  kNoLogServer,
  // Has Active LOG server.
  kHasActiveLogServer
};

//>
// Introduce its own scope lock mutex because @base::Lock does not support
// RECURSIVE MUTEX.
//<
class ScopeLock {
 public:
  explicit ScopeLock(pthread_mutex_t& m) : mutex_{&m} { Lock(); };
  ~ScopeLock() { UnLock(); }

 private:
  void Lock() { osaf_mutex_lock_ordie(mutex_); }
  void UnLock() { osaf_mutex_unlock_ordie(mutex_); }

 private:
  pthread_mutex_t* mutex_;

  DELETE_COPY_AND_MOVE_OPERATORS(ScopeLock);
};

#endif  // SRC_LOG_AGENT_LGA_COMMON_H_
