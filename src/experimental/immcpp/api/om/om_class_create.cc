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

#include "experimental/immcpp/api/include/om_class_create.h"
#include <string.h>
#include <vector>
#include "base/saf_error.h"
#include "base/osaf_extended_name.h"

namespace immom {

//-----------------------------------------------------------------------------
// ImmOmClassCreate class
//-----------------------------------------------------------------------------
ImmOmClassCreate::ImmOmClassCreate(
    const SaImmHandleT& h,
    const std::string& class_name,
    bool configurable)
    : ImmBase(),
      om_handle_{h},
      class_name_{class_name},
      list_of_attribute_definitions_{} {
  category_ = configurable ? SA_IMM_CLASS_CONFIG : SA_IMM_CLASS_RUNTIME;
}

ImmOmClassCreate::~ImmOmClassCreate() {
  FreeAllocatedMemory();
}

// Creates attribute new IMM Service runtime object
bool ImmOmClassCreate::CreateClass() {
  TRACE_ENTER();
  size_t size = list_of_attribute_definitions_.size();
  SaImmAttrDefinitionT_2 temp[size];
  const SaImmAttrDefinitionT_2* attribute_definitions[size + 1];

  // Form `attrNames`
  unsigned i = 0;
  for (auto& attribute : list_of_attribute_definitions_) {
    attribute_definitions[i] = &temp[i];
    attribute->FormAttrDefinitionT_2(&temp[i++]);
  }
  attribute_definitions[i] = nullptr;

  base::Timer wtime(retry_ctrl_.timeout);
  while (wtime.is_timeout() == false) {
    ais_error_ = saImmOmClassCreate_2(
        om_handle_,
        const_cast<char*>(class_name_.c_str()),
        category_,
        attribute_definitions);
    if (ais_error_ == SA_AIS_ERR_TRY_AGAIN) {
      base::Sleep(retry_ctrl_.interval);
      continue;
    }
    break;
  }

  return (ais_error_ == SA_AIS_OK);
}

void ImmOmClassCreate::FreeAllocatedMemory() {
  TRACE_ENTER();
  for (auto& attribute : list_of_attribute_definitions_) {
    if (attribute == nullptr) continue;
    delete attribute;
    attribute = nullptr;
  }
}

}  // namespace immom
