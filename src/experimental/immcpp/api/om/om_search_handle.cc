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

#include "experimental/immcpp/api/include/om_search_handle.h"
#include <memory>
#include "base/osaf_extended_name.h"

namespace immom {

//------------------------------------------------------------------------------
// ImmOmSearchCriteria class
//------------------------------------------------------------------------------
ImmOmSearchCriteria::ImmOmSearchCriteria()
    : attribute_name_{},
      attribute_value_{},
      search_root_{},
      scope_{SA_IMM_SUBTREE},
      option_{SA_IMM_SEARCH_GET_NO_ATTR},
      attributes_{} {
  ResetSearchParam();
}

ImmOmSearchCriteria::~ImmOmSearchCriteria() { }

//------------------------------------------------------------------------------
// ImmOmSearchHandle class
//------------------------------------------------------------------------------
ImmOmSearchHandle::ImmOmSearchHandle(const SaImmHandleT& om_handle)
    : ImmBase(),
      om_handle_{om_handle},
      setting_{nullptr},
      search_handle_{0} { }

ImmOmSearchHandle::ImmOmSearchHandle(
    const SaImmHandleT& om_handle, const ImmOmSearchCriteria& setting)
    : ImmBase(),
      om_handle_{om_handle},
      setting_{&setting},
      search_handle_{0} { }

ImmOmSearchHandle::~ImmOmSearchHandle() {
  TRACE_ENTER();
  FinalizeHandle();
}

// Initialize @search_handle_ from IMM
bool ImmOmSearchHandle::InitializeHandle() {
  TRACE_ENTER();
  ais_error_ = SA_AIS_OK;

  // Search handle is already initialized.
  if (search_handle_ > 0) return true;

  SaNameT root;
  SaImmAttrNameT* attribute_name = nullptr;
  ImmOmSearchCriteria setting;
  if (setting_ == nullptr) setting_ = &setting;
  if (setting_->search_root_.empty() != true) {
    osaf_extended_name_lend(setting_->search_root_.c_str(), &root);
  }

  if (setting_->attributes_.empty() != true) {
    unsigned i = 0;
    size_t size = setting_->attributes_.size();
    attribute_name = new SaImmAttrNameT[size + 1];
    for (const auto& a : setting_->attributes_) {
      attribute_name[i++] = const_cast<char*>(a.c_str());
    }
    attribute_name[i] = nullptr;
  }

  const SaImmSearchParametersT_2* search_param = nullptr;
  if (setting_->option_ & SA_IMM_SEARCH_ONE_ATTR) {
    search_param = &setting_->search_param_;
  }

  // Acquire @search_handle_ from IMM
  base::Timer wtime(retry_ctrl_.timeout);
  while (wtime.is_timeout() == false) {
    ais_error_ = saImmOmSearchInitialize_2(
        om_handle_,
        (setting_->search_root_.empty() == true) ? nullptr : &root,
        setting_->scope_,
        setting_->option_,
        search_param,
        attribute_name,
        &search_handle_);
    if (ais_error_ == SA_AIS_ERR_TRY_AGAIN) {
      base::Sleep(retry_ctrl_.interval);
      continue;
    }
    break;
  }

  delete[] attribute_name;
  return (ais_error_ == SA_AIS_OK);
}

bool ImmOmSearchHandle::FinalizeHandle() {
  TRACE_ENTER();
  ais_error_ = SA_AIS_OK;
  if (search_handle_ == 0) return true;

  base::Timer wtime(retry_ctrl_.timeout);
  while (wtime.is_timeout() == false) {
    ais_error_ = saImmOmSearchFinalize(search_handle_);
    if (ais_error_ == SA_AIS_ERR_TRY_AGAIN) {
      base::Sleep(retry_ctrl_.interval);
      continue;
    }
    break;
  }

  if (ais_error_ == SA_AIS_OK) search_handle_ = 0;
  return (ais_error_ == SA_AIS_OK);
}

SaImmSearchHandleT ImmOmSearchHandle::GetHandle() {
  TRACE_ENTER();
  // Implicitly initialize handle
  if (search_handle_ == 0) InitializeHandle();
  return search_handle_;
}

}  // namespace immom
