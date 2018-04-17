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
#include <sstream>
#include <thread>
#include "base/conf.h"
#include "base/getenv.h"
#include "base/logtrace.h"
#include "base/ncssysf_def.h"

const std::string Consensus::kTakeoverRequestKeyname = "takeover_request";

SaAisErrorT Consensus::PromoteThisNode(const bool graceful_takeover,
    const uint64_t cluster_size) {
  TRACE_ENTER();
  SaAisErrorT rc;

  if (use_consensus_ == false) {
    return SA_AIS_OK;
  }

  // check if there is an existing takeover requests, we cannot
  // attempt to lock until that is complete
  CheckForExistingTakeoverRequest();

  uint32_t retries = 0;
  rc = KeyValue::Lock(base::Conf::NodeName(), kLockTimeout);
  while (rc == SA_AIS_ERR_TRY_AGAIN && retries < kMaxRetry) {
    TRACE("Waiting for lock");
    ++retries;
    std::this_thread::sleep_for(kSleepInterval);
    rc = KeyValue::Lock(base::Conf::NodeName(), kLockTimeout);
  }

  if (rc == SA_AIS_ERR_EXIST) {
    // there's a chance the lock has been released since the lock attempt
    // get the current active controller
    std::string current_active = CurrentActive();

    if (current_active.empty() == true) {
      LOG_WA("Failed to get current lock owner. Will attempt to lock anyway");
    }

    bool take_over_request_created = false;
    LOG_NO("Current active controller is %s", current_active.c_str());

    if (current_active.empty() == false) {
      if (graceful_takeover == true) {
        rc = CreateTakeoverRequest(current_active, base::Conf::NodeName(),
                                   cluster_size);
        if (rc != SA_AIS_OK) {
          LOG_WA("Takeover request failed (%d)", rc);
          return rc;
        }
        take_over_request_created = true;
      }

      // remove current active controller's lock and fence it
      rc = Demote(current_active);
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

    if (take_over_request_created == true) {
      SaAisErrorT rc1;

      // remove takeover request
      rc1 = KeyValue::Erase(kTakeoverRequestKeyname);
      retries = 0;
      while (rc1 != SA_AIS_OK && retries < kMaxRetry) {
        ++retries;
        std::this_thread::sleep_for(kSleepInterval);
        rc1 = KeyValue::Erase(kTakeoverRequestKeyname);
      }

      if (rc1 != SA_AIS_OK) {
        LOG_WA("Could not remove takeover request");
      }
    }
  }

  if (rc == SA_AIS_OK) {
    LOG_NO("Active controller set to %s", base::Conf::NodeName().c_str());
  } else {
    LOG_ER("Failed to promote this node (%u)", rc);
  }

  return rc;
}

SaAisErrorT Consensus::Demote(const std::string& node) {
  TRACE_ENTER();
  if (use_consensus_ == false) {
    return SA_AIS_OK;
  }

  osafassert(node.empty() == false);

  SaAisErrorT rc;
  uint32_t retries = 0;

  rc = KeyValue::Unlock(node);
  while (rc == SA_AIS_ERR_TRY_AGAIN && retries < kMaxRetry) {
    LOG_IN("Trying to unlock");
    ++retries;
    std::this_thread::sleep_for(kSleepInterval);
    rc = KeyValue::Unlock(node);
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

  // check current active node
  std::string current_active = CurrentActive();
  if (current_active.empty() == true) {
    LOG_ER("Failed to get lock owner");
    return SA_AIS_ERR_FAILED_OPERATION;
  }

  LOG_NO("Demoting %s as active controller", current_active.c_str());

  return Demote(current_active);
}

SaAisErrorT Consensus::DemoteThisNode() {
  TRACE_ENTER();
  return Demote(base::Conf::NodeName());
}

bool Consensus::IsEnabled() const { return use_consensus_; }

bool Consensus::IsWritable() const {
  TRACE_ENTER();
  if (use_consensus_ == false) {
    return true;
  }

  SaAisErrorT rc;
  uint32_t retries = 0;
  constexpr uint32_t kMaxTestRetry = 3;
  rc = KeyValue::Set(kTestKeyname, base::Conf::NodeName());
  while (rc != SA_AIS_OK && retries < kMaxTestRetry) {
    ++retries;
    std::this_thread::sleep_for(kSleepInterval);
    rc = KeyValue::Set(kTestKeyname, base::Conf::NodeName());
  }

  if (rc == SA_AIS_OK) {
    return true;
  } else {
    return false;
  }
}

bool Consensus::IsRemoteFencingEnabled() const { return use_remote_fencing_; }

std::string Consensus::CurrentActive() const {
  TRACE_ENTER();
  if (use_consensus_ == false) {
    return "";
  }

  SaAisErrorT rc;
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
  uint32_t use_remote_fencing = base::GetEnv("FMS_USE_REMOTE_FENCING", 0);

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

Consensus::~Consensus() {}

bool Consensus::FenceNode(const std::string& node) {
  if (use_remote_fencing_ == true) {
    LOG_WA("Fencing remote node %s", node.c_str());
    // @todo currently passing UINT_MAX as node ID, since
    // we can't always obtain a valid node ID?
    opensaf_reboot(UINT_MAX, node.c_str(), "Fencing remote node");

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

void Consensus::MonitorTakeoverRequest(ConsensusCallback callback,
                                       const uint32_t user_defined) {
  TRACE_ENTER();
  if (use_consensus_ == false) {
    return;
  }

  KeyValue::Watch(kTakeoverRequestKeyname, callback, user_defined);
}

void Consensus::CheckForExistingTakeoverRequest() {
  SaAisErrorT rc;
  std::vector<std::string> tokens;
  rc = ReadTakeoverRequest(tokens);

  if (rc != SA_AIS_OK) {
    return;
  }

  // get expiration
  const uint64_t expiration_timestamp = strtoull(
      tokens[static_cast<std::uint8_t>(TakeoverElements::TIMESTAMP)].c_str(),
      0, 10);

  // wait until expiration is over
  int64_t expiration = expiration_timestamp - CurrentTime();
  if (expiration > 0 && expiration <= kTakeoverValidTime) {
    // @todo check if the takeover request has been deleted already?
    LOG_NO("A takeover request is in progress"
           " (expiring in %" PRId64 " seconds)",
           expiration);
    std::chrono::seconds sleep_duration(expiration);
    std::this_thread::sleep_for(sleep_duration);
  } else {
    LOG_WA("Invalid expiration time (%" PRIu64 ")", expiration_timestamp);
  }
}

SaAisErrorT Consensus::CreateTakeoverRequest(const std::string& current_owner,
                                             const std::string& proposed_owner,
                                             const uint64_t cluster_size) {
  TRACE_ENTER();

  // Format of takeover request:
  // "expiration_time<space>current_owner<space>proposed_owner<space>
  // proposed_owner_cluster_size<space>status"
  // status := [UNDEFINED, NEW, REJECTED, ACCEPTED]

  std::string takeover_request;
  // request to expire in 20 seconds
  uint64_t timestamp = CurrentTime() + kTakeoverValidTime;

  takeover_request =
      std::to_string(timestamp) + " " + current_owner + " " +
      base::Conf::NodeName() + " " + std::to_string(cluster_size) + " " +
      TakeoverStateStr[static_cast<std::uint8_t>(TakeoverState::NEW)];

  TRACE("Takeover request: \"%s\"", takeover_request.c_str());

  SaAisErrorT rc;
  uint32_t retries = 0;
  rc = KeyValue::Create(kTakeoverRequestKeyname, takeover_request);
  while (rc == SA_AIS_ERR_FAILED_OPERATION && retries < kMaxRetry) {
    ++retries;
    std::this_thread::sleep_for(kSleepInterval);
    rc = KeyValue::Create(kTakeoverRequestKeyname, takeover_request);
  }

  if (rc == SA_AIS_ERR_EXIST) {
    LOG_NO("Existing takeover request found");

    // retrieve takeover request
    std::vector<std::string> tokens;
    retries = 0;
    rc = ReadTakeoverRequest(tokens);
    while (rc == SA_AIS_ERR_FAILED_OPERATION && retries < kMaxRetry) {
      ++retries;
      std::this_thread::sleep_for(kSleepInterval);
      rc = ReadTakeoverRequest(tokens);
    }

    if (rc == SA_AIS_OK) {
      // get expiration
      const uint64_t expiration_timestamp = strtoull(
        tokens[static_cast<std::uint8_t>(TakeoverElements::TIMESTAMP)].c_str(),
        0, 10);

      // wait until expiration is over
      int64_t expiration = expiration_timestamp - CurrentTime();
      if (expiration > 0 && expiration <= kTakeoverValidTime) {
        LOG_NO("A takeover request is in progress"
               " (expiring in %" PRId64 " seconds)",
               expiration);
        std::chrono::seconds sleep_duration(expiration);
        std::this_thread::sleep_for(sleep_duration);
      } else {
        LOG_WA("Invalid expiration time (%" PRIu64 ")", expiration_timestamp);
      }
    }  // else remove it anyway

    LOG_NO("Remove expired takeover request");

    // remove expired request
    retries = 0;
    rc = KeyValue::Erase(kTakeoverRequestKeyname);
    while (rc != SA_AIS_OK && retries < kMaxRetry) {
      ++retries;
      std::this_thread::sleep_for(kSleepInterval);
      rc = KeyValue::Erase(kTakeoverRequestKeyname);
    }

    if (rc == SA_AIS_OK) {
      return CreateTakeoverRequest(current_owner, proposed_owner, cluster_size);
    } else {
      LOG_ER("Could not remove existing takeover request");
      return SA_AIS_ERR_EXIST;
    }
  }

  // wait up to 10s for request to be answered
  retries = 0;
  while (retries < kMaxTakeoverRetry) {
    std::vector<std::string> tokens;
    if (ReadTakeoverRequest(tokens) == SA_AIS_OK) {
      const std::string state =
          tokens[static_cast<std::uint8_t>(TakeoverElements::STATE)];
      const std::string _proposed_owner =
          tokens[static_cast<std::uint8_t>(TakeoverElements::PROPOSED_OWNER)];

      if (proposed_owner != _proposed_owner) {
        LOG_ER("Takeover request was not created by us! (%s)",
               _proposed_owner.c_str());
        rc = SA_AIS_ERR_EXIST;
        break;
      }

      if (state == TakeoverStateStr[static_cast<std::uint8_t>(
                       TakeoverState::REJECTED)]) {
        LOG_NO("Takeover request rejected");
        rc = SA_AIS_ERR_EXIST;
        break;
      } else if (state == TakeoverStateStr[static_cast<std::uint8_t>(
                              TakeoverState::ACCEPTED)]) {
        LOG_NO("Takeover request accepted");
        rc = SA_AIS_OK;
        break;
      } else if (state == TakeoverStateStr[static_cast<std::uint8_t>(
                              TakeoverState::NEW)]) {
        TRACE("Waiting for response to takeover request");
        // set result to OK, in case we do not get a reply
        // within the allocated period. This will allow the lock to
        // removed by this node. Note: do not break out of the loop here!
        rc = SA_AIS_OK;
      }
    }
    ++retries;
    std::this_thread::sleep_for(kSleepInterval);
  }

  LOG_NO("Result: %d", rc);
  return rc;
}

SaAisErrorT Consensus::WriteTakeoverResult(
    const std::string& timestamp, const std::string& current_owner,
    const std::string& proposed_owner, const std::string& proposed_cluster_size,
    const TakeoverState result) {
  TRACE_ENTER();

  const std::string takeover_request =
      timestamp + " " + current_owner + " " + proposed_owner + " " +
      proposed_cluster_size + " " +
      TakeoverStateStr[static_cast<std::uint8_t>(TakeoverState::NEW)];

  const std::string takeover_result =
      timestamp + " " + current_owner + " " + proposed_owner + " " +
      proposed_cluster_size + " " +
      TakeoverStateStr[static_cast<std::uint8_t>(result)];

  LOG_NO("TakeoverResult: %s", takeover_result.c_str());

  SaAisErrorT rc;

  // previous value must match
  rc =
      KeyValue::Set(kTakeoverRequestKeyname, takeover_result, takeover_request);

  return rc;
}

SaAisErrorT Consensus::ReadTakeoverRequest(std::vector<std::string>& tokens) {
  TRACE_ENTER();

  std::string request;
  SaAisErrorT rc;

  rc = KeyValue::Get(kTakeoverRequestKeyname, request);
  if (rc != SA_AIS_OK) {
    // it doesn't always exist, don't log an error
    LOG_NO("Could not read takeover request (%d)", rc);
    return SA_AIS_ERR_FAILED_OPERATION;
  }

  if (request.empty() == true) {
    // on node shutdown, this could be empty
    return SA_AIS_ERR_UNAVAILABLE;
  }

  Split(request, tokens);
  if (tokens.size() != 5) {
    LOG_ER("Invalid takeover request");
    return SA_AIS_ERR_LIBRARY;
  }

  return SA_AIS_OK;
}

Consensus::TakeoverState Consensus::HandleTakeoverRequest(
    const uint64_t cluster_size) {
  TRACE_ENTER();

  if (use_consensus_ == false) {
    return TakeoverState::UNDEFINED;
  }

  SaAisErrorT rc;
  uint32_t retries = 0;
  std::vector<std::string> tokens;

  // get request from KV store
  rc = ReadTakeoverRequest(tokens);
  while (rc == SA_AIS_ERR_FAILED_OPERATION && retries < kMaxRetry) {
    ++retries;
    std::this_thread::sleep_for(kSleepInterval);
    rc = ReadTakeoverRequest(tokens);
  }

  if (rc != SA_AIS_OK) {
    return TakeoverState::UNDEFINED;
  }

  // request is a space delimited string with 5 elements
  osafassert(tokens.size() == 5);

  // check the owner is this node
  if (tokens[1] != base::Conf::NodeName()) {
    LOG_ER("We do not own the lock. Ignoring takeover request");
    return TakeoverState::UNDEFINED;
  }

  // expiration timestamp
  const uint64_t expiration = strtoull(
      tokens[static_cast<std::uint8_t>(TakeoverElements::TIMESTAMP)].c_str(), 0,
      10);

  // size of the other network partition
  const uint64_t proposed_cluster_size = strtoull(
      tokens[static_cast<std::uint8_t>(TakeoverElements::PROPOSED_NETWORK_SIZE)]
          .c_str(),
      0, 10);

  LOG_NO("Other network size: %" PRIu64 ", our network size: %" PRIu64,
         proposed_cluster_size, cluster_size);

  TakeoverState result;
  if (CurrentTime() <= expiration && proposed_cluster_size > cluster_size) {
    result = TakeoverState::ACCEPTED;
  } else {
    result = TakeoverState::REJECTED;
  }

  rc = WriteTakeoverResult(
      tokens[static_cast<std::uint8_t>(TakeoverElements::TIMESTAMP)],
      tokens[static_cast<std::uint8_t>(TakeoverElements::CURRENT_OWNER)],
      tokens[static_cast<std::uint8_t>(TakeoverElements::PROPOSED_OWNER)],
      tokens[static_cast<std::uint8_t>(
          TakeoverElements::PROPOSED_NETWORK_SIZE)],
      result);
  if (rc != SA_AIS_OK) {
    LOG_ER("Unable to write takeover result (%d)", rc);
    return TakeoverState::UNDEFINED;
  }

  return result;
}

// separate space delimited elements in a string
void Consensus::Split(const std::string& str,
                      std::vector<std::string>& tokens) const {
  std::stringstream stream(str);
  std::string buffer;

  while (stream >> buffer) {
    tokens.push_back(buffer);
  }
}

// seconds after epoch
uint64_t Consensus::CurrentTime() const {
  auto now = std::chrono::system_clock::now();

  // seconds since epoch
  auto timestamp =
      std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch())
          .count();

  return timestamp;
}
