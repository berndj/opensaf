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

#include "experimental/immcpp/api/include/om_accessor_handle.h"

namespace immom {

//------------------------------------------------------------------------------
// ImmOmAccessorHandle class
//------------------------------------------------------------------------------
ImmOmAccessorHandle::ImmOmAccessorHandle(const SaImmHandleT& om_handle)
    : ImmBase(),
      om_handle_{om_handle},
      access_handle_{0} {}

ImmOmAccessorHandle::~ImmOmAccessorHandle() {
  TRACE_ENTER();
  FinalizeHandle();
}

// Initialize @accessorHandle_ from IMM
bool ImmOmAccessorHandle::InitializeHandle() {
  TRACE_ENTER();
  ais_error_ = SA_AIS_OK;

  // The accessor handle is already initialized.
  if (access_handle_ > 0) return true;

  base::Timer wtime(retry_ctrl_.timeout);
  while (wtime.is_timeout() == false) {
    ais_error_ = saImmOmAccessorInitialize(om_handle_, &access_handle_);
    if (ais_error_ == SA_AIS_ERR_TRY_AGAIN) {
      base::Sleep(retry_ctrl_.interval);
      continue;
    }
    break;
  }
  return (ais_error_ == SA_AIS_OK);
}

bool ImmOmAccessorHandle::FinalizeHandle() {
  TRACE_ENTER();
  ais_error_ = SA_AIS_OK;
  if (access_handle_ == 0) return true;

  base::Timer wtime(retry_ctrl_.timeout);
  while (wtime.is_timeout() == false) {
    ais_error_ = saImmOmAccessorFinalize(access_handle_);
    if (ais_error_ == SA_AIS_ERR_TRY_AGAIN) {
      base::Sleep(retry_ctrl_.interval);
      continue;
    }
    break;
  }

  if (ais_error_ == SA_AIS_OK) access_handle_ = 0;
  return (ais_error_ == SA_AIS_OK);
}

SaImmAccessorHandleT ImmOmAccessorHandle::GetHandle() {
  TRACE_ENTER();
  // Handle has not been explicitly initialized
  if (access_handle_ == 0) InitializeHandle();
  return access_handle_;
}

}  // namespace immom
