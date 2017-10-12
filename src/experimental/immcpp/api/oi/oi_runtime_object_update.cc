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

#include "experimental/immcpp/api/include/oi_runtime_object_update.h"
#include <string.h>
#include "base/osaf_extended_name.h"
#include "base/logtrace.h"

namespace immoi {

//------------------------------------------------------------------------------
// ImmOiRtObjectUpdate class
//------------------------------------------------------------------------------
ImmOiRtObjectUpdate::ImmOiRtObjectUpdate(
    const SaImmOiHandleT& oi_handle,
    const std::string& object_name)
    : ImmBase(),
      oi_handle_{oi_handle},
      object_name_{object_name},
      attribute_mods_{} { }

ImmOiRtObjectUpdate::~ImmOiRtObjectUpdate() {
  FreeAllocatedMemory();
}

bool ImmOiRtObjectUpdate::UpdateObject() {
  TRACE_ENTER();
  if (object_name_.empty() == true) {
    ais_error_ = SA_AIS_ERR_INVALID_PARAM;
    return false;
  }

  unsigned i = 0;
  size_t size = attribute_mods_.size();
  SaImmAttrModificationT_2 temp[size];
  const SaImmAttrModificationT_2* attribute_modifications[size + 1];
  for (auto& attribute : attribute_mods_) {
    attribute_modifications[i] = &temp[i];
    attribute->FormAttrModificationT_2(&temp[i++]);
  }
  attribute_modifications[i] = nullptr;

  SaNameT obj_name;
  base::Timer wtime(retry_ctrl_.timeout);
  osaf_extended_name_lend(object_name_.c_str(), &obj_name);
  while (wtime.is_timeout() == false) {
    ais_error_ = saImmOiRtObjectUpdate_2(
        oi_handle_,
        &obj_name,
        attribute_modifications);
    if (ais_error_ == SA_AIS_ERR_TRY_AGAIN) {
      base::Sleep(retry_ctrl_.interval);
      continue;
    }
    break;
  }
  return (ais_error_ == SA_AIS_OK);
}

void ImmOiRtObjectUpdate::FreeAllocatedMemory() {
  TRACE_ENTER();
  for (auto& attribute : attribute_mods_) {
    if (attribute == nullptr) continue;
    delete attribute;
    attribute = nullptr;
  }
}

}  // namespace immoi
