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

#include "experimental/immcpp/api/include/om_search_next.h"
#include "base/osaf_extended_name.h"
#include "base/time.h"

namespace immom {

//------------------------------------------------------------------------------
// ImmOmSearchNext class
//------------------------------------------------------------------------------
ImmOmSearchNext::ImmOmSearchNext(const SaImmSearchHandleT& search_handle)
    : ImmBase(),
      search_handle_{search_handle},
      object_name_{},
      attribute_values_{nullptr} {}

ImmOmSearchNext::~ImmOmSearchNext() {}

bool ImmOmSearchNext::SearchNext() {
  TRACE_ENTER();
  SaNameT dn;
  base::Timer wtime(retry_ctrl_.timeout);
  while (wtime.is_timeout() == false) {
    ais_error_ = saImmOmSearchNext_2(search_handle_, &dn, &attribute_values_);
    if (ais_error_ == SA_AIS_ERR_TRY_AGAIN) {
      base::Sleep(retry_ctrl_.interval);
      continue;
    }
    break;
  }

  if (ais_error_ != SA_AIS_OK) {
    // Reset the output if getting error
    object_name_      = "";
    attribute_values_ = nullptr;
    return false;
  }

  object_name_ = osaf_extended_name_borrow(&dn);
  return true;
}


template <typename T> T*
ImmOmSearchNext::GetSingleValue(const std::string& name) const {
  TRACE_ENTER();
  // typename T should be one of AIS data types or std::string. Not support
  // SaInt8T, and SaInt16T types as no SaImmValueTypeT goes with them.
  SA_AIS_TYPE_CHECK(T);
  if (attribute_values_ == nullptr) return nullptr;

  unsigned i = 0;
  SaImmAttrValuesT_2* attribute;
  while ((attribute = attribute_values_[i++]) != nullptr) {
    // Continue if does not match or not single value type
    if (name != attribute->attrName ||
        attribute->attrValuesNumber != 1) continue;

    // Found the name in the list.
    if (attribute->attrValues == nullptr) return nullptr;
    return static_cast<T*>(attribute->attrValues[0]);
  }
  return nullptr;
}

template <typename T> std::vector<T>
ImmOmSearchNext::GetMultiValue(const std::string& name) const {
  TRACE_ENTER();
  // typename T should be one of AIS data types or std::string. Not support
  // SaInt8T, and SaInt16T types as no SaImmValueTypeT goes with them.
  SA_AIS_TYPE_CHECK(T);
  if (attribute_values_ == nullptr) return {};

  unsigned i = 0;
  SaImmAttrValuesT_2* attribute;
  std::vector<T> out{};
  while ((attribute = attribute_values_[i++]) != nullptr) {
    if (name != attribute->attrName) continue;

    // Found the given name in the list.
    unsigned len = attribute->attrValuesNumber;
    T value;
    for (unsigned ii = 0; ii < len ; ii++) {
      value = *(static_cast<T*>(attribute->attrValues[ii]));
      out.push_back(value);
    }
    return out;
  }
  return out;
}

const SaImmAttrValuesT_2*
ImmOmSearchNext::GetAttributeProperty(const std::string& name) const {
  TRACE_ENTER();
  if (attribute_values_ == nullptr) return nullptr;

  unsigned i = 0;
  SaImmAttrValuesT_2* attribute;
  while ((attribute = attribute_values_[i++]) != nullptr) {
    // Have name in the list, then return its property
    if (name == attribute->attrName) return attribute;
  }
  return nullptr;
}

const SaImmValueTypeT*
ImmOmSearchNext::GetAttributeValueType(const std::string& name) const {
  TRACE_ENTER();
  if (attribute_values_ == nullptr) return nullptr;

  unsigned i = 0;
  SaImmAttrValuesT_2* attribute;
  while ((attribute = attribute_values_[i++]) != nullptr) {
    // Have attribute name in the list, then return its attribute value type.
    if (name == attribute->attrName) return &attribute->attrValueType;
  }
  return nullptr;
}

const SaUint32T*
ImmOmSearchNext::GetAttributeValueNumber(const std::string& name) const {
  TRACE_ENTER();
  if (attribute_values_ == nullptr) return nullptr;

  unsigned i = 0;
  SaImmAttrValuesT_2* attribute;
  while ((attribute = attribute_values_[i++]) != nullptr) {
    // Have attribute name in the list, then return its attribute value number
    if (name == attribute->attrName) return &attribute->attrValuesNumber;
  }
  return nullptr;
}

template SaInt32T*
ImmOmSearchNext::GetSingleValue(const std::string& name) const;

template SaUint32T*
ImmOmSearchNext::GetSingleValue(const std::string& name) const;

template SaInt64T*
ImmOmSearchNext::GetSingleValue(const std::string& name) const;

template SaUint64T*
ImmOmSearchNext::GetSingleValue(const std::string& name) const;

template SaNameT*
ImmOmSearchNext::GetSingleValue(const std::string& name) const;

template SaFloatT*
ImmOmSearchNext::GetSingleValue(const std::string& name) const;

template SaDoubleT*
ImmOmSearchNext::GetSingleValue(const std::string& name) const;

template SaStringT*
ImmOmSearchNext::GetSingleValue(const std::string& name) const;

template SaConstStringT*
ImmOmSearchNext::GetSingleValue(const std::string& name) const;

template SaAnyT*
ImmOmSearchNext::GetSingleValue(const std::string& name) const;


template std::vector<SaInt32T>
ImmOmSearchNext::GetMultiValue(const std::string& name) const;

template std::vector<SaUint32T>
ImmOmSearchNext::GetMultiValue(const std::string& name) const;

template std::vector<SaInt64T>
ImmOmSearchNext::GetMultiValue(const std::string& name) const;

template std::vector<SaUint64T>
ImmOmSearchNext::GetMultiValue(const std::string& name) const;

template std::vector<SaNameT>
ImmOmSearchNext::GetMultiValue(const std::string& name) const;

template std::vector<SaFloatT>
ImmOmSearchNext::GetMultiValue(const std::string& name) const;

template std::vector<SaDoubleT>
ImmOmSearchNext::GetMultiValue(const std::string& name) const;

template std::vector<SaStringT>
ImmOmSearchNext::GetMultiValue(const std::string& name) const;

template std::vector<SaConstStringT>
ImmOmSearchNext::GetMultiValue(const std::string& name) const;

template std::vector<SaAnyT>
ImmOmSearchNext::GetMultiValue(const std::string& name) const;

}  // namespace immom
