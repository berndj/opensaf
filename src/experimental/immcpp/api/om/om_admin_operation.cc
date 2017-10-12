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

#include "experimental/immcpp/api/include/om_admin_operation.h"
#include <string.h>
#include <vector>
#include "base/saf_error.h"
#include "base/osaf_extended_name.h"

namespace immom {
//-----------------------------------------------------------------------------
// ImmOmAdminOperation class
//-----------------------------------------------------------------------------
ImmOmAdminOperation::ImmOmAdminOperation(
    const SaImmAdminOwnerHandleT& admin_owner_handle,
    const std::string& object_name)
    : ImmBase(),
      object_name_{object_name},
      admin_owner_handle_{admin_owner_handle},
      continuation_id_{0},
      admin_op_id_{0},
      timeout_{0},
      list_of_attribute_properties_{} {}

ImmOmAdminOperation::~ImmOmAdminOperation() {
  FreeAllocatedMemory();
}

bool ImmOmAdminOperation::InvokeAdminOperation(SaAisErrorT* opcode) {
  TRACE_ENTER();
  SaNameT name;
  size_t size = list_of_attribute_properties_.size();
  SaImmAdminOperationParamsT_2 temp[size];
  const SaImmAdminOperationParamsT_2* admin_params[size + 1];

  osaf_extended_name_lend(object_name_.c_str(), &name);

  unsigned i = 0;
  for (auto& attribute : list_of_attribute_properties_) {
    admin_params[i] = &temp[i];
    attribute->FormAdminOperationParamsT_2(&temp[i++]);
  }
  admin_params[i] = nullptr;

  const SaNameT* const_sanamet = &name;
  base::Timer wtime(retry_ctrl_.timeout);
  while (wtime.is_timeout() == false) {
    ais_error_ = saImmOmAdminOperationInvoke_2(
        admin_owner_handle_,
        const_sanamet,
        continuation_id_,
        admin_op_id_,
        admin_params,
        opcode,
        timeout_);
    if (ais_error_ == SA_AIS_ERR_TRY_AGAIN) {
      base::Sleep(retry_ctrl_.interval);
      continue;
    }
    break;
  }
  return (ais_error_ == SA_AIS_OK);
}

bool ImmOmAdminOperation::InvokeAdminOperationAsync(
    SaInvocationT invocation) {
  TRACE_ENTER();
  SaNameT name;
  size_t size = list_of_attribute_properties_.size();
  SaImmAdminOperationParamsT_2 temp[size];
  const SaImmAdminOperationParamsT_2* admin_params[size + 1];

  osaf_extended_name_lend(object_name_.c_str(), &name);

  unsigned i = 0;
  for (auto& attribute : list_of_attribute_properties_) {
    admin_params[i] = &temp[i];
    attribute->FormAdminOperationParamsT_2(&temp[i++]);
  }
  admin_params[i] = nullptr;

  const SaNameT* const_sanamet = &name;
  base::Timer wtime(retry_ctrl_.timeout);
  while (wtime.is_timeout() == false) {
    ais_error_ = saImmOmAdminOperationInvokeAsync_2(
        admin_owner_handle_,
        invocation,
        const_sanamet,
        continuation_id_,
        admin_op_id_,
        admin_params);
    if (ais_error_ == SA_AIS_ERR_TRY_AGAIN) {
      base::Sleep(retry_ctrl_.interval);
      continue;
    }
    break;
  }
  return (ais_error_ == SA_AIS_OK);
}

bool ImmOmAdminOperation::ContinueAdminOperation(SaAisErrorT* opcode) {
  TRACE_ENTER();

  SaNameT name;
  osaf_extended_name_lend(object_name_.c_str(), &name);

  const SaNameT* const_sanamet = &name;
  base::Timer wtime(retry_ctrl_.timeout);
  while (wtime.is_timeout() == false) {
    ais_error_ = saImmOmAdminOperationContinue(
        admin_owner_handle_,
        const_sanamet,
        continuation_id_,
        opcode);
    if (ais_error_ == SA_AIS_ERR_TRY_AGAIN) {
      base::Sleep(retry_ctrl_.interval);
      continue;
    }
    break;
  }
  return (ais_error_ == SA_AIS_OK);
}

bool ImmOmAdminOperation::ContinueAdminOperationAsync(
    SaInvocationT invocation) {
  TRACE_ENTER();
  SaNameT name;

  osaf_extended_name_lend(object_name_.c_str(), &name);
  const SaNameT* const_sanamet = &name;
  base::Timer wtime(retry_ctrl_.timeout);
  while (wtime.is_timeout() == false) {
    ais_error_ = saImmOmAdminOperationContinueAsync(
        admin_owner_handle_,
        invocation,
        const_sanamet,
        continuation_id_);
    if (ais_error_ == SA_AIS_ERR_TRY_AGAIN) {
      base::Sleep(retry_ctrl_.interval);
      continue;
    }
    break;
  }
  return (ais_error_ == SA_AIS_OK);
}

bool ImmOmAdminOperation::ClearAdminOperationContinuation() {
  TRACE_ENTER();
  SaNameT name;

  osaf_extended_name_lend(object_name_.c_str(), &name);
  const SaNameT* const_sanamet = &name;
  base::Timer wtime(retry_ctrl_.timeout);
  while (wtime.is_timeout() == false) {
    ais_error_ = saImmOmAdminOperationContinuationClear(
        admin_owner_handle_,
        const_sanamet,
        continuation_id_);
    if (ais_error_ == SA_AIS_ERR_TRY_AGAIN) {
      base::Sleep(retry_ctrl_.interval);
      continue;
    }
    break;
  }
  return (ais_error_ == SA_AIS_OK);
}

void ImmOmAdminOperation::FreeAllocatedMemory() {
  TRACE_ENTER();
  for (auto& attribute : list_of_attribute_properties_) {
    if (attribute == nullptr) continue;
    delete attribute;
    attribute = nullptr;
  }
}

}  // namespace immom
