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

#include "experimental/immcpp/api/include/om_ccb_object_modify.h"
#include <string.h>
#include "base/osaf_extended_name.h"
#include "base/logtrace.h"

namespace immom {

//------------------------------------------------------------------------------
// ImmOmCcbObjectModify class
//------------------------------------------------------------------------------
ImmOmCcbObjectModify::ImmOmCcbObjectModify(
    const SaImmCcbHandleT& ccb_handle,
    const std::string& objname)
    : ImmBase(),
      object_name_{objname},
      list_of_attribute_modifications_{},
      ccb_handle_{ccb_handle} { }

ImmOmCcbObjectModify::~ImmOmCcbObjectModify() {
  FreeAllocatedMemory();
}

bool ImmOmCcbObjectModify::AddObjectModifyToCcb() {
  TRACE_ENTER();

  if (object_name_.empty() == true) {
    TRACE_2("Incorrect object name");
    ais_error_ = SA_AIS_ERR_INVALID_PARAM;
    return false;
  }

  // Create the object name parameter
  SaNameT obj_name;
  osaf_extended_name_lend(object_name_.c_str(), &obj_name);

  unsigned i = 0;
  size_t size = list_of_attribute_modifications_.size();
  SaImmAttrModificationT_2 temp[size];
  const SaImmAttrModificationT_2* attribute_modifications[size + 1];
  for (auto& attribute : list_of_attribute_modifications_) {
    attribute_modifications[i] = &temp[i];
    attribute->FormAttrModificationT_2(&temp[i++]);
  }
  attribute_modifications[i] = nullptr;

  base::Timer wtime(retry_ctrl_.timeout);
  while (wtime.is_timeout() == false) {
    ais_error_ = saImmOmCcbObjectModify_2(
        ccb_handle_,
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

void ImmOmCcbObjectModify::FreeAllocatedMemory() {
  TRACE_ENTER();
  for (auto& attribute : list_of_attribute_modifications_) {
    if (attribute == nullptr) continue;
    delete attribute;
    attribute = nullptr;
  }
}

}  // namespace immom
