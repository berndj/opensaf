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

#include "experimental/immcpp/api/include/oi_implementer.h"
#include <string>
#include "base/osaf_extended_name.h"

namespace immoi {

//-----------------------------------------------------------------------------
// ImmOiObjectImplementer class
//-----------------------------------------------------------------------------
ImmOiObjectImplementer::ImmOiObjectImplementer(
    const SaImmOiHandleT& oi_handle,
    const std::string& implementor_name,
    const std::string& name)
    : ImmOiObjectImplementer(oi_handle, implementor_name,
                             std::vector<std::string>{name}) {}

ImmOiObjectImplementer::ImmOiObjectImplementer(
    const SaImmOiHandleT& oi_handle,
    const std::string& implementor_name)
    : ImmOiObjectImplementer(oi_handle, implementor_name,
                             std::vector<std::string>{}) {}

ImmOiObjectImplementer::ImmOiObjectImplementer(
    const SaImmOiHandleT& oi_handle,
    const std::string& implementor_name,
    const std::vector<std::string>& list_of_class_names)
    : ImmBase(),
      oi_handle_{oi_handle},
      is_set_oi_implementer_invoked_{false},
      implementer_name_{implementor_name},
      list_of_class_names_{list_of_class_names} {}

ImmOiObjectImplementer::~ImmOiObjectImplementer() {
  // No need to clear class implementers as it will automatically
  // bind to the renew OI implementer name whenever the OI is re-connected.
  ClearOiImplementer();
}

bool ImmOiObjectImplementer::SetImplementers() {
  TRACE_ENTER();
  if (SetOiImplementer() == false) return false;
  // Clear implementer name and stop the progress if any fault happens.
  for (const auto& class_name : list_of_class_names_) {
    SaAisErrorT backup_ais_code;
    if (SetClassImplementer(class_name) == false) {
      backup_ais_code = ais_error_;
      ClearOiImplementer();
      ais_error_ = backup_ais_code;
      return false;
    }
  }
  return true;
}

bool ImmOiObjectImplementer::ClearImplementers() {
  TRACE_ENTER();
  // LOG warning and continue its jobs if any fault happens with
  // class implementer clear.
  for (const auto& class_name : list_of_class_names_) {
    if (ClearClassImplementer(class_name) == false &&
        ais_error_ != SA_AIS_ERR_EXIST) {
      LOG_WA("Failed to clear class implementer name (%s)", class_name.c_str());
    }
  }
  if (ClearOiImplementer() == false) return false;
  return true;
}

bool ImmOiObjectImplementer::SetOiImplementer() {
  TRACE_ENTER();
  ais_error_ = SA_AIS_OK;
  if (implementer_name_.empty() == true) return true;

  char* implementor_name = const_cast<char*>(implementer_name_.c_str());
  base::Timer wtime(retry_ctrl_.timeout);
  while (wtime.is_timeout() == false) {
    ais_error_ = saImmOiImplementerSet(oi_handle_, implementor_name);
    if (ais_error_ == SA_AIS_ERR_TRY_AGAIN) {
      base::Sleep(retry_ctrl_.interval);
      continue;
    }
    break;
  }

  if (ais_error_ == SA_AIS_OK) is_set_oi_implementer_invoked_ = true;
  return (ais_error_ == SA_AIS_OK);
}

bool ImmOiObjectImplementer::ClearOiImplementer() {
  TRACE_ENTER();
  // NOTE: What is proper returned AIS code of `Clear`
  // OI implementer if Set OI implementer not yet invoked?
  ais_error_ = SA_AIS_OK;
  if (is_set_oi_implementer_invoked_ == false) return true;

  base::Timer wtime(retry_ctrl_.timeout);
  while (wtime.is_timeout() == false) {
    ais_error_ = saImmOiImplementerClear(oi_handle_);
    if (ais_error_ == SA_AIS_ERR_TRY_AGAIN) {
      base::Sleep(retry_ctrl_.interval);
      continue;
    }
    break;
  }

  if (ais_error_ == SA_AIS_OK) is_set_oi_implementer_invoked_ = false;
  return (ais_error_ == SA_AIS_OK);
}


bool ImmOiObjectImplementer::SetClassImplementers() {
  TRACE_ENTER();
  // Clear implementer name and stop the progress if any fault happens.
  for (const auto& class_name : list_of_class_names_) {
    SaAisErrorT backup_ais_code;
    if (SetClassImplementer(class_name) == false &&
        ais_error_ != SA_AIS_ERR_EXIST) {
      backup_ais_code = ais_error_;
      ClearOiImplementer();
      ais_error_ = backup_ais_code;
      return false;
    }
  }
  return true;
}

// Clear all class implementers in one go.
bool ImmOiObjectImplementer::ClearClassImplementers() {
  bool ret = true;
  TRACE_ENTER();
  // LOG warning and continue its jobs if any fault happens with
  // class implementer clear.
  for (const auto& class_name : list_of_class_names_) {
    if (ClearClassImplementer(class_name) == false) {
      LOG_WA("Failed to clear class implementer name (%s)", class_name.c_str());
      ret = false;
    }
  }
  return ret;
}

bool ImmOiObjectImplementer::SetClassImplementer(
    const std::string& class_name) {
  TRACE_ENTER();
  char* name = const_cast<char*>(class_name.c_str());
  base::Timer wtime(retry_ctrl_.timeout);
  while (wtime.is_timeout() == false) {
    ais_error_ = saImmOiClassImplementerSet(oi_handle_, name);
    if (ais_error_ == SA_AIS_ERR_TRY_AGAIN) {
      base::Sleep(retry_ctrl_.interval);
      continue;
    }
    break;
  }
  return (ais_error_ == SA_AIS_OK);
}

bool ImmOiObjectImplementer::ClearClassImplementer(
    const std::string& class_name) {
  TRACE_ENTER();
  char* name = const_cast<char*>(class_name.c_str());
  base::Timer wtime(retry_ctrl_.timeout);
  while (wtime.is_timeout() == false) {
    ais_error_ = saImmOiClassImplementerRelease(oi_handle_, name);
    if (ais_error_ == SA_AIS_ERR_TRY_AGAIN) {
      base::Sleep(retry_ctrl_.interval);
      continue;
    }
    break;
  }
  return (ais_error_ == SA_AIS_OK);
}

bool ImmOiObjectImplementer::SetObjectImplementer(
    const std::string& object_name,
    SaImmScopeT scope) {
  SaNameT dn;
  osaf_extended_name_lend(object_name.c_str(), &dn);
  base::Timer wtime(retry_ctrl_.timeout);
  while (wtime.is_timeout() == false) {
    ais_error_ = saImmOiObjectImplementerSet(oi_handle_, &dn, scope);
    if (ais_error_ == SA_AIS_ERR_TRY_AGAIN) {
      base::Sleep(retry_ctrl_.interval);
      continue;
    }
    break;
  }
  return (ais_error_ != SA_AIS_OK);
}

bool ImmOiObjectImplementer::ReleaseObjectImplementer(
    const std::string& object_name,
    SaImmScopeT scope) {
  SaNameT dn;
  osaf_extended_name_lend(object_name.c_str(), &dn);
  base::Timer wtime(retry_ctrl_.timeout);
  while (wtime.is_timeout() == false) {
    ais_error_ = saImmOiObjectImplementerRelease(oi_handle_, &dn, scope);
    if (ais_error_ == SA_AIS_ERR_TRY_AGAIN) {
      base::Sleep(retry_ctrl_.interval);
      continue;
    }
    break;
  }
  return (ais_error_ == SA_AIS_OK);
}

}  // namespace immoi
