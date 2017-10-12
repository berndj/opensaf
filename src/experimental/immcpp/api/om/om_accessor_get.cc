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

#include "experimental/immcpp/api/include/om_accessor_get.h"
#include "base/osaf_extended_name.h"


namespace immom {

//------------------------------------------------------------------------------
// ImmOmAccessorGet class
//------------------------------------------------------------------------------
ImmOmAccessorGet::ImmOmAccessorGet(
    const SaImmAccessorHandleT& accessor_handle,
    const std::string& objname)
    : ImmBase(),
      object_name_{objname},
      attributes_{},
      accessor_handle_{accessor_handle},
      attribute_values_{nullptr} { }

ImmOmAccessorGet::~ImmOmAccessorGet() { }

bool ImmOmAccessorGet::FetchAttributeValue() {
  TRACE_ENTER();

  SaNameT obj_name;
  if (object_name_.empty() == true) {
    TRACE_2("Incorrect object name");
    ais_error_ = SA_AIS_ERR_INVALID_PARAM;
    return false;
  }

  osaf_extended_name_lend(object_name_.c_str(), &obj_name);

  SaImmAttrNameT* attribute = nullptr;
  if (attributes_.empty() != true) {
    unsigned i = 0;
    size_t size = attributes_.size();
    attribute = new SaImmAttrNameT[size + 1];
    for (const auto& a : attributes_) {
      attribute[i++] = const_cast<char*>(a.c_str());
    }
    attribute[i] = nullptr;
  }

  base::Timer wtime(retry_ctrl_.timeout);
  while (wtime.is_timeout() == false) {
    ais_error_ = saImmOmAccessorGet_2(
        accessor_handle_,
        &obj_name,
        attribute,
        &attribute_values_);
    if (ais_error_ == SA_AIS_ERR_TRY_AGAIN) {
      base::Sleep(retry_ctrl_.interval);
      continue;
    }
    break;
  }

  delete[] attribute;

  if (ais_error_ != SA_AIS_OK) attribute_values_ = nullptr;
  return (ais_error_ == SA_AIS_OK);
}

template <typename T>
T* ImmOmAccessorGet::GetSingleValue(const std::string& name) const {
  TRACE_ENTER();
  // typename T should be one of AIS data types or std::string. Not support
  // SaInt8T, and SaInt16T types as no SaImmValueTypeT goes with them.
  SA_AIS_TYPE_CHECK(T);
  if (attribute_values_ == nullptr) return nullptr;

  int i = 0;
  SaImmAttrValuesT_2* value = nullptr;
  while ((value = attribute_values_[i++]) != nullptr) {
    if (name != value->attrName || value->attrValuesNumber != 1) continue;
    // Found attribute name in the list.
    return static_cast<T*>(value->attrValues[0]);
  }
  return nullptr;
}

template <typename T>
std::vector<T> ImmOmAccessorGet::GetMultiValue(const std::string& name) const {
  TRACE_ENTER();
  // typename T should be one of AIS data types or std::string. Not support
  // SaInt8T, and SaInt16T types as no SaImmValueTypeT goes with them.
  SA_AIS_TYPE_CHECK(T);
  if (attribute_values_ == nullptr) return {};

  int i = 0;
  SaImmAttrValuesT_2* value = nullptr;
  std::vector<T> out{};
  while ((value = attribute_values_[i++]) != nullptr) {
    if (name != value->attrName) continue;

    // Found attribute name in the list
    T fvalue;
    for (unsigned ii = 0; ii < value->attrValuesNumber; ii++) {
      fvalue = *(static_cast<T*>(value->attrValues[ii]));
      out.push_back(fvalue);
    }
    return out;
  }

  return out;
}

const std::vector<SaImmAttrValuesT_2*>
ImmOmAccessorGet::GetAllAttributeProperties() const {
  TRACE_ENTER();
  if (attribute_values_ == nullptr) return {};

  unsigned i = 0;
  SaImmAttrValuesT_2* attribute;
  std::vector<SaImmAttrValuesT_2*> out{};
  while ((attribute = attribute_values_[i++]) != nullptr) {
    out.push_back(attribute);
  }
  return out;
}

const SaImmValueTypeT*
ImmOmAccessorGet::GetAttributeValueType(const std::string& name) const {
  TRACE_ENTER();
  if (attribute_values_ == nullptr) return nullptr;

  unsigned i = 0;
  SaImmAttrValuesT_2* attribute;
  while ((attribute = attribute_values_[i++]) != nullptr) {
    // Have attribute `name` in the list, then return its attribute value type.
    if (name == attribute->attrName) return &attribute->attrValueType;
  }
  return nullptr;
}

const SaUint32T*
ImmOmAccessorGet::GetAttributeValuesNumber(const std::string& name) const {
  TRACE_ENTER();
  if (attribute_values_ == nullptr) return nullptr;

  unsigned i = 0;
  SaImmAttrValuesT_2* attribute;
  while ((attribute = attribute_values_[i++]) != nullptr) {
    // Have attribute `name` in the list, then return its value number.
    if (name == attribute->attrName) return &attribute->attrValuesNumber;
  }
  return nullptr;
}

template SaInt32T*
ImmOmAccessorGet::GetSingleValue(const std::string& name) const;

template SaUint32T*
ImmOmAccessorGet::GetSingleValue(const std::string& name) const;

template SaInt64T*
ImmOmAccessorGet::GetSingleValue(const std::string& name) const;

template SaUint64T*
ImmOmAccessorGet::GetSingleValue(const std::string& name) const;

template SaNameT*
ImmOmAccessorGet::GetSingleValue(const std::string& name) const;

template SaFloatT*
ImmOmAccessorGet::GetSingleValue(const std::string& name) const;

template SaDoubleT*
ImmOmAccessorGet::GetSingleValue(const std::string& name) const;

template SaStringT*
ImmOmAccessorGet::GetSingleValue(const std::string& name) const;

template SaConstStringT*
ImmOmAccessorGet::GetSingleValue(const std::string& name) const;

template SaAnyT*
ImmOmAccessorGet::GetSingleValue(const std::string& name) const;

template std::vector<SaInt32T>
ImmOmAccessorGet::GetMultiValue(const std::string& name) const;

template std::vector<SaUint32T>
ImmOmAccessorGet::GetMultiValue(const std::string& name) const;

template std::vector<SaInt64T>
ImmOmAccessorGet::GetMultiValue(const std::string& name) const;

template std::vector<SaUint64T>
ImmOmAccessorGet::GetMultiValue(const std::string& name) const;

template std::vector<SaNameT>
ImmOmAccessorGet::GetMultiValue(const std::string& name) const;

template std::vector<SaFloatT>
ImmOmAccessorGet::GetMultiValue(const std::string& name) const;

template std::vector<SaDoubleT>
ImmOmAccessorGet::GetMultiValue(const std::string& name) const;

template std::vector<SaStringT>
ImmOmAccessorGet::GetMultiValue(const std::string& name) const;

template std::vector<SaConstStringT>
ImmOmAccessorGet::GetMultiValue(const std::string& name) const;

template std::vector<SaAnyT>
ImmOmAccessorGet::GetMultiValue(const std::string& name) const;

}  // namespace immom
