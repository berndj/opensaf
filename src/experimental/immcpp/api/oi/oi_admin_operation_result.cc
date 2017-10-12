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

#include "experimental/immcpp/api/include/oi_admin_operation_result.h"
#include <string.h>
#include <string>
#include "base/osaf_extended_name.h"
#include "ais/include/saImmOm.h"
#include "base/logtrace.h"

namespace immoi {

//-----------------------------------------------------------------------------
// ImmOiAdminOperationResult class
//-----------------------------------------------------------------------------
ImmOiAdminOperationResult::ImmOiAdminOperationResult(
    const SaImmOiHandleT& oi_handle) : ImmBase(), oi_handle_{oi_handle} { }

ImmOiAdminOperationResult::~ImmOiAdminOperationResult() {}

// Inform the IMM Service about the result of the execution of an
// administrative operation requested by IMM Service
bool ImmOiAdminOperationResult::InformResult(
    SaInvocationT invocation, SaAisErrorT result) {
  TRACE_ENTER();
  base::Timer wtime(retry_ctrl_.timeout);
  while (wtime.is_timeout() == false) {
    ais_error_ = saImmOiAdminOperationResult(oi_handle_, invocation, result);
    if (ais_error_ == SA_AIS_ERR_TRY_AGAIN) {
      base::Sleep(retry_ctrl_.interval);
      continue;
    }
    break;
  }

  return (ais_error_ == SA_AIS_OK);
}

bool ImmOiAdminOperationResult::InformResult(
    SaInvocationT invocation,
    SaAisErrorT result,
    const std::string& msg) {
  TRACE_ENTER();
  const char* c_msg = msg.c_str();
  const std::string admop_error = SA_IMM_PARAM_ADMOP_ERROR;
  SaImmAdminOperationParamsT_2 admop_error_param = {
    const_cast<char*>(admop_error.c_str()),
    SA_IMM_ATTR_SASTRINGT,
    &c_msg
  };
  const SaImmAdminOperationParamsT_2* admop_error_params[2] = {
    &admop_error_param,
    nullptr
  };

  base::Timer wtime(retry_ctrl_.timeout);
  while (wtime.is_timeout() == false) {
    ais_error_ = saImmOiAdminOperationResult_o2(
        oi_handle_,
        invocation,
        result,
        admop_error_params);
    if (ais_error_ == SA_AIS_ERR_TRY_AGAIN) {
      base::Sleep(retry_ctrl_.interval);
      continue;
    }
    break;
  }
  return (ais_error_ == SA_AIS_OK);
}

}  // namespace immoi
