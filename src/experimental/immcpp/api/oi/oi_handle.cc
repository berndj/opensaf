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

#include "experimental/immcpp/api/include/oi_handle.h"

namespace immoi {

//------------------------------------------------------------------------------
// ImmOiHandle class
//------------------------------------------------------------------------------
ImmOiHandle::ImmOiHandle(const SaVersionT& version,
                         const SaImmOiCallbacksT_2* callbacks)
    : ImmBase(), fd_{0}, callbacks_{callbacks}, oi_handle_{0} {
  version_ = supported_version_ = version;
}

ImmOiHandle::ImmOiHandle(const SaImmOiCallbacksT_2* callbacks)
    : ImmOiHandle{ImmBase::GetDefaultImmVersion(), callbacks} {}

ImmOiHandle::~ImmOiHandle() {
  FinalizeHandle();
}

// Initialize @oi_handle_ from IMM
bool ImmOiHandle::InitializeHandle() {
  ais_error_ = SA_AIS_OK;
  if (oi_handle_ > 0) return true;

  SaVersionT version = version_;
  base::Timer wtime(retry_ctrl_.timeout);
  while (wtime.is_timeout() == false) {
    ais_error_ = saImmOiInitialize_2(&oi_handle_, callbacks_, &version);
    if (ais_error_ == SA_AIS_ERR_TRY_AGAIN) {
      version = version_;
      base::Sleep(retry_ctrl_.interval);
      continue;
    }
    break;
  }
  supported_version_ = version;
  return (ais_error_ == SA_AIS_OK);
}

bool ImmOiHandle::FinalizeHandle() {
  ais_error_ = SA_AIS_OK;
  if (oi_handle_ == 0) return true;

  base::Timer wtime(retry_ctrl_.timeout);
  while (wtime.is_timeout() == false) {
    ais_error_ = saImmOiFinalize(oi_handle_);
    if (ais_error_ == SA_AIS_ERR_TRY_AGAIN) {
      base::Sleep(retry_ctrl_.interval);
      continue;
    }
    break;
  }

  if (ais_error_ == SA_AIS_OK) fd_ = oi_handle_ = 0;
  return (ais_error_ == SA_AIS_OK);
}

SaImmOiHandleT ImmOiHandle::GetHandle() {
  if (oi_handle_ == 0) InitializeHandle();
  return oi_handle_;
}

bool ImmOiHandle::FetchSelectionObject() {
  ais_error_ = SA_AIS_OK;
  if (fd_ > 0) return true;

  base::Timer wtime(retry_ctrl_.timeout);
  while (wtime.is_timeout() == false) {
    ais_error_ = saImmOiSelectionObjectGet(oi_handle_, &fd_);
    if (ais_error_ == SA_AIS_ERR_TRY_AGAIN) {
      base::Sleep(retry_ctrl_.interval);
      continue;
    }
    break;
  }
  return (ais_error_ == SA_AIS_OK);
}

SaSelectionObjectT ImmOiHandle::fd() {
  if (fd_ == 0) FetchSelectionObject();
  return fd_;
}

bool ImmOiHandle::Dispatch(SaDispatchFlagsT flag) {
  base::Timer wtime(retry_ctrl_.timeout);
  while (wtime.is_timeout() == false) {
    ais_error_ = saImmOiDispatch(oi_handle_, flag);
    if (ais_error_ == SA_AIS_ERR_TRY_AGAIN) {
      base::Sleep(retry_ctrl_.interval);
      continue;
    }
    break;
  }
  return (ais_error_ == SA_AIS_OK);
}

bool ImmOiHandle::DispatchOne() {
  return Dispatch(SA_DISPATCH_ONE);
}

bool ImmOiHandle::DispatchAll() {
  return Dispatch(SA_DISPATCH_ALL);
}

bool ImmOiHandle::DispatchBlocking() {
  return Dispatch(SA_DISPATCH_BLOCKING);
}

// To enable tracing early when the library is loaded.
__attribute__((constructor))
static void logtrace_init_constructor(void) {
  char* value;
  // Initialize trace system first of all so we can see what is going.
  if ((value = getenv("IMMOICPP_TRACE_PATHNAME")) != nullptr) {
    if (logtrace_init("immoicpp", value, CATEGORY_ALL) != 0) {
      // Error, we cannot do anything
      return;
    }
  }
}

}  // namespace immoi
