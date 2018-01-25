/*      -*- OpenSAF  -*-
 *
 * (C) Copyright 2018 Ericsson AB 2018 - All Rights Reserved.
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
#include "osaf/consensus/consensus.h"
#include <unistd.h>
#include <climits>
#include <thread>
#include "base/conf.h"
#include "base/getenv.h"
#include "base/logtrace.h"
#include "base/ncssysf_def.h"

SaAisErrorT Consensus::PromoteThisNode() {
  TRACE_ENTER();
  SaAisErrorT rc;

  if (use_consensus_ == false) {
    return SA_AIS_OK;
  }

  uint32_t retries = 0;
  rc = KeyValue::Lock(base::Conf::NodeName(), kLockTimeout);
  while (rc == SA_AIS_ERR_TRY_AGAIN && retries < kMaxRetry) {
    TRACE("Waiting for lock");
    ++retries;
    std::this_thread::sleep_for(kSleepInterval);
    rc = KeyValue::Lock(base::Conf::NodeName(), kLockTimeout);
  }

  if (rc == SA_AIS_ERR_EXIST) {
    // get the current active controller
    std::string current_active("");
    retries = 0;
    rc = KeyValue::LockOwner(current_active);
    while (rc != SA_AIS_OK && retries < kMaxRetry) {
      ++retries;
      std::this_thread::sleep_for(kSleepInterval);
      rc = KeyValue::LockOwner(current_active);
    }
    if (rc != SA_AIS_OK) {
      LOG_ER("Failed to get current lock owner. Will attempt to lock anyway");
    }

    LOG_NO("Current active controller is %s", current_active.c_str());

    // there's a chance the lock has been released since the lock attempt
    if (current_active.empty() == false) {
      // remove current active controller's lock and fence it
      retries = 0;
      rc = KeyValue::Unlock(current_active);
      while (rc == SA_AIS_ERR_TRY_AGAIN && retries < kMaxRetry) {
        LOG_IN("Trying to unlock");
        ++retries;
        std::this_thread::sleep_for(kSleepInterval);
        rc = KeyValue::Unlock(current_active);
      }

      if (rc == SA_AIS_OK) {
        FenceNode(current_active);
      } else {
        LOG_WA("Unlock failed (%u)", rc);
      }
    }

    // previous lock has been released, try locking again
    retries = 0;
    rc = KeyValue::Lock(base::Conf::NodeName(), kLockTimeout);
    while (rc == SA_AIS_ERR_TRY_AGAIN && retries < kMaxRetry) {
      TRACE("Waiting for lock");
      ++retries;
      std::this_thread::sleep_for(kSleepInterval);
      rc = KeyValue::Lock(base::Conf::NodeName(), kLockTimeout);
    }
  }

  if (rc == SA_AIS_OK) {
    LOG_NO("Active controller set to %s", base::Conf::NodeName().c_str());
  } else {
    LOG_ER("Failed to promote this node (%u)", rc);
  }

  return rc;
}

SaAisErrorT Consensus::Demote(const std::string& node = "") {
  TRACE_ENTER();
  if (use_consensus_ == false) {
    return SA_AIS_OK;
  }

  SaAisErrorT rc = SA_AIS_ERR_FAILED_OPERATION;
  uint32_t retries = 0;

  // check current active node
  std::string current_active;
  rc = KeyValue::LockOwner(current_active);
  while (rc != SA_AIS_OK && retries < kMaxRetry) {
    ++retries;
    std::this_thread::sleep_for(kSleepInterval);
    rc = KeyValue::LockOwner(current_active);
  }

  if (rc != SA_AIS_OK) {
    LOG_ER("Failed to get lock owner");
    return rc;
  }

  LOG_NO("Demoting %s as active controller", current_active.c_str());

  if (node.empty() == false && node != current_active) {
    // node is not the current active controller!
    osafassert(false);
  }

  retries = 0;
  rc = KeyValue::Unlock(current_active);
  while (rc == SA_AIS_ERR_TRY_AGAIN && retries < kMaxRetry) {
    LOG_IN("Trying to unlock");
    ++retries;
    std::this_thread::sleep_for(kSleepInterval);
    rc = KeyValue::Unlock(current_active);
  }

  if (rc != SA_AIS_OK) {
    LOG_ER("Unlock failed (%u)", rc);
    return rc;
  }

  LOG_IN("Released lock");
  return rc;
}

SaAisErrorT Consensus::DemoteCurrentActive() {
  TRACE_ENTER();
  return Demote();
}

SaAisErrorT Consensus::DemoteThisNode() {
  TRACE_ENTER();
  return Demote(base::Conf::NodeName());
}

bool Consensus::IsEnabled() const {
  return use_consensus_;
}

bool Consensus::IsWritable() const {
  TRACE_ENTER();
  if (use_consensus_ == false) {
    return true;
  }

  SaAisErrorT rc;
  rc = KeyValue::Set(kTestKeyname, base::Conf::NodeName());
  if (rc == SA_AIS_OK) {
    return true;
  } else {
    return false;
  }
}

bool Consensus::IsRemoteFencingEnabled() const {
  return use_remote_fencing_;
}

std::string Consensus::CurrentActive() const {
  TRACE_ENTER();
  if (use_consensus_ == false) {
    return "";
  }

  SaAisErrorT rc = SA_AIS_ERR_FAILED_OPERATION;
  uint32_t retries = 0;
  std::string owner;

  rc = KeyValue::LockOwner(owner);
  while (rc != SA_AIS_OK && retries < kMaxRetry) {
    ++retries;
    std::this_thread::sleep_for(kSleepInterval);
    rc = KeyValue::LockOwner(owner);
  }

  if (rc == SA_AIS_OK) {
    return owner;
  } else {
    LOG_ER("Failed to get lock owner");
    return "";
  }
}

Consensus::Consensus() {
  TRACE_ENTER();

  uint32_t split_brain_enable = base::GetEnv("FMS_SPLIT_BRAIN_PREVENTION", 0);
  std::string kv_store_cmd = base::GetEnv("FMS_KEYVALUE_STORE_PLUGIN_CMD", "");
  uint32_t use_remote_fencing = base::GetEnv("FMS_USE_REMOTE_FENCING" , 0);

  if (split_brain_enable == 1 && kv_store_cmd.empty() == false) {
    use_consensus_ = true;
  } else {
    use_consensus_ = false;
  }

  if (use_remote_fencing == 1) {
    use_remote_fencing_ = true;
  }

  // needed for base::Conf::NodeName() later
  base::Conf::InitNodeName();
}

Consensus::~Consensus() {
}

bool Consensus::FenceNode(const std::string& node) {
  if (use_remote_fencing_ == true) {
    LOG_WA("Fencing remote node %s", node.c_str());
    // @todo currently passing UINT_MAX as node ID, since
    // we can't always obtain a valid node ID?
    opensaf_reboot(UINT_MAX, node.c_str(),
      "Fencing remote node");

    return true;
  } else {
    LOG_WA("Fencing is not enabled. Node %s will not be fenced", node.c_str());
    return false;
  }
}

void Consensus::MonitorLock(ConsensusCallback callback,
  const uint32_t user_defined) {
  TRACE_ENTER();
  if (use_consensus_ == false) {
    return;
  }

  KeyValue::WatchLock(callback, user_defined);
}
