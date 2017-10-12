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

#include "experimental/immcpp/api/include/om_class_description_get.h"
#include "base/time.h"

namespace immom {

//------------------------------------------------------------------------------
// ImmOmClassDescriptionGet class
//------------------------------------------------------------------------------
ImmOmClassDescriptionGet::ImmOmClassDescriptionGet(
    const SaImmHandleT& om_handle, const std::string& class_name)
    : ImmBase(),
      om_handle_{om_handle} {
  class_name_ = class_name;
  class_category_ = SA_IMM_CLASS_CONFIG;
  attribute_definition_ = nullptr;
}

ImmOmClassDescriptionGet::~ImmOmClassDescriptionGet() {
  FreeClassDescription();
}

bool ImmOmClassDescriptionGet::GetClassDescription() {
  base::Timer wtime(retry_ctrl_.timeout);
  char* classname = const_cast<char*>(class_name_.c_str());
  while (wtime.is_timeout() == false) {
    ais_error_ = saImmOmClassDescriptionGet_2(
        om_handle_,
        classname,
        &class_category_,
        &attribute_definition_);
    if (ais_error_ == SA_AIS_ERR_TRY_AGAIN) {
      base::Sleep(retry_ctrl_.interval);
      continue;
    }
    break;
  }
  return (ais_error_ == SA_AIS_OK);
}

const std::vector<SaImmAttrDefinitionT_2*>
ImmOmClassDescriptionGet::GetAllAttributeDefinition() const {
  TRACE_ENTER();
  if (attribute_definition_ == nullptr) return {};

  unsigned i = 0;
  SaImmAttrDefinitionT_2* attribute;
  std::vector<SaImmAttrDefinitionT_2*> out{};
  while ((attribute = attribute_definition_[i++]) != nullptr) {
    out.push_back(attribute);
  }
  return out;
}

bool ImmOmClassDescriptionGet::FreeClassDescription() {
  ais_error_ = SA_AIS_OK;
  if (attribute_definition_ == nullptr) return true;

  base::Timer wtime(retry_ctrl_.timeout);
  while (wtime.is_timeout() == false) {
    ais_error_ = saImmOmClassDescriptionMemoryFree_2(
        om_handle_, attribute_definition_);
    if (ais_error_ == SA_AIS_ERR_TRY_AGAIN) {
      base::Sleep(retry_ctrl_.interval);
      continue;
    }
    break;
  }

  if (ais_error_ != SA_AIS_OK) attribute_definition_ = nullptr;
  return (ais_error_ == SA_AIS_OK);
}

const SaImmValueTypeT* ImmOmClassDescriptionGet::GetAttributeValueType(
    const std::string& name) const {
  TRACE_ENTER();
  if (attribute_definition_ == nullptr) return nullptr;

  unsigned i = 0;
  SaImmAttrDefinitionT_2* attribute;
  while ((attribute = attribute_definition_[i++]) != nullptr) {
    // Have attribute name in the list, then return its attribute type.
    if (name == attribute->attrName) return &attribute->attrValueType;
  }
  return nullptr;
}

const SaImmAttrFlagsT* ImmOmClassDescriptionGet::GetAttributeFlags(
    const std::string& name) const {
  TRACE_ENTER();
  if (attribute_definition_ == nullptr) return nullptr;

  unsigned i = 0;
  SaImmAttrDefinitionT_2* attribute;
  while ((attribute = attribute_definition_[i++]) != nullptr) {
    // Have attribute name in the list, then return its attribute flags.
    if (name == attribute->attrName) return &attribute->attrFlags;
  }
  return nullptr;
}

template <typename T>
T* ImmOmClassDescriptionGet::GetDefaultValue(const std::string& name) const {
  TRACE_ENTER();
  // typename T should be one of AIS data types or std::string. Not support
  // SaInt8T, and SaInt16T types as no SaImmValueTypeT goes with them.
  SA_AIS_TYPE_CHECK(T);
  if (attribute_definition_ == nullptr) return nullptr;

  unsigned i = 0;
  SaImmAttrDefinitionT_2* attribute;
  while ((attribute = attribute_definition_[i++]) != nullptr) {
    if (name != attribute->attrName) continue;
    // Found attribute name in the list.
    if (attribute->attrDefaultValue == nullptr) return nullptr;
    return static_cast<T*>(attribute->attrDefaultValue);
  }
  return nullptr;
}

template SaInt32T*
ImmOmClassDescriptionGet::GetDefaultValue(const std::string& name) const;

template SaUint32T*
ImmOmClassDescriptionGet::GetDefaultValue(const std::string& name) const;

template SaInt64T*
ImmOmClassDescriptionGet::GetDefaultValue(const std::string& name) const;

template SaUint64T*
ImmOmClassDescriptionGet::GetDefaultValue(const std::string& name) const;

template SaNameT*
ImmOmClassDescriptionGet::GetDefaultValue(const std::string& name) const;

template SaFloatT*
ImmOmClassDescriptionGet::GetDefaultValue(const std::string& name) const;

template SaDoubleT*
ImmOmClassDescriptionGet::GetDefaultValue(const std::string& name) const;

template SaStringT*
ImmOmClassDescriptionGet::GetDefaultValue(const std::string& name) const;

template SaConstStringT*
ImmOmClassDescriptionGet::GetDefaultValue(const std::string& name) const;

template SaAnyT*
ImmOmClassDescriptionGet::GetDefaultValue(const std::string& name) const;

}  // namespace immom
