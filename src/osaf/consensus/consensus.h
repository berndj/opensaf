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
#ifndef OSAF_CONSENSUS_CONSENSUS_H_
#define OSAF_CONSENSUS_CONSENSUS_H_

#include <chrono>
#include <string>
#include "saAis.h"
#include "base/macros.h"
#include "osaf/consensus/key_value.h"

class Consensus {
 public:
  // Set active controller to this node
  SaAisErrorT PromoteThisNode();

  // Clear current active controller by releasing lock
  SaAisErrorT DemoteCurrentActive();

  // Clear this node as active controller by releasing lock
  SaAisErrorT DemoteThisNode();

  // Returns active controller as known by the consensus service
  std::string CurrentActive() const;

  // If the active controller is changed as known by the consensus service,
  // then callback will be run from a new thread, with <user_defined> returned
  // in the callback
  void MonitorLock(ConsensusCallback callback, const uint32_t user_defined);

  // Is consensus service enabled?
  bool IsEnabled() const;

  // Is the key-value store writable?
  bool IsWritable() const;

  // Is remote fencing enabled?
  bool IsRemoteFencingEnabled() const;

  Consensus();
  virtual ~Consensus();

 private:
  bool use_consensus_ = false;
  bool use_remote_fencing_ = false;
  const std::string kTestKeyname = "opensaf_write_test";
  const std::chrono::milliseconds kSleepInterval =
    std::chrono::milliseconds(100);  // in ms
  static constexpr uint32_t kLockTimeout = 0;  // lock is persistent by default
  static constexpr uint32_t kMaxRetry = 600;
  SaAisErrorT Demote(const std::string& node);
  bool FenceNode(const std::string& node);

  DELETE_COPY_AND_MOVE_OPERATORS(Consensus);
};

#endif  // OSAF_CONSENSUS_CONSENSUS_H_
