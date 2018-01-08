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

#ifndef SRC_LOG_IMMWRP_COMMON_IMM_ATTRIBUTE_H_
#define SRC_LOG_IMMWRP_COMMON_IMM_ATTRIBUTE_H_

#include <string.h>
#include <string>
#include <vector>
#include "ais/include/saImm.h"
#include "experimental/immcpp/api/common/common.h"
#include "imm_cpp_type.h"

//>
// These below 03 classes represent C IMM data structures
// SaImmAttrDefinitionT_2, SaImmSearchOneAttrT_2, SaImmAttrValuesT_2
// and SaImmAttrModificationT_2, SaImmAdminOperationParamsT_2.
//
// In these C IMM data structures, lot of duplicated fields such as
// attrName, attrValueType, attrValuesNumber, etc. By introducing
// these 03 classes, that duplicated data are removed.
//
// Here is simple class diagram, showing relationship among them.
//                      +---------------------+
//                      |  AttributeProperty  |
//                      +---------+-----------+
//                                |
//                                |
//                                |
//            +-------------------+-----------------+
//            |                                     |
//            |                                     |
// +----------+------------+           +------------+----------+
// | AttributeModification |           | AttributeDefinition   |
// +-----------------------+           +-----------------------+
//
// AttributeProperty class holds generic information of IMM attribute, include
// attribute name, attribute value type, attribute value(s) and number
// of attribute values. Besides, AttributeProperty class provides help methods
// to convert itself to other C IMM data structures it represent for.
//
// AttributeModification and AttributeModification class represent
// SaImmAttrDefinitionT_2, and SaImmAttrModificationT_2 respectively.
// They also have help methods to convert themself to C IMM data structures.
//
// Example:
// AttributeProperty* attribute = new AttributeProperty(name);
// attribute->set_value<SaUint32T>(value);
//
// SaImmAttrValuesT_2 immattr;
// attribute->FormAttrValuesT_2(&immattr);
//<


class AttributeProperty {
 public:
  explicit AttributeProperty(const std::string& name)
      : attribute_name_{name},
        attribute_values_{nullptr},
        num_of_values_{0}  {}

  virtual ~AttributeProperty() {
    for (auto& item : values_) {
      delete item;
    }
    FreeMemory();
  }

  template <typename T>
  AttributeProperty& set_value(T* value) {
    attribute_type_ = ImmBase::GetAttributeValueType<T>();
    if (value != nullptr) {
      attribute_values_ = reinterpret_cast<void**>(new T*[1]());
      assert(attribute_values_ != nullptr);
      values_.push_back(GenerateCppType<T>(value));
      attribute_values_[0] = values_[0]->data();
      num_of_values_ = 1;
    }
    return *this;
  }

  template <typename T>
  AttributeProperty& set_value(const std::vector<T*>& values) {
    attribute_type_ = ImmBase::GetAttributeValueType<T>();
    if (values.empty() == false) {
      size_t size = values.size();
      attribute_values_ = reinterpret_cast<void**>(new T*[size]());
      assert(attribute_values_ != nullptr);
      unsigned i = 0;
      for (auto& value : values) {
        values_.push_back(GenerateCppType<T>(value));
        attribute_values_[i] = values_[i]->data();
        i++;
      }
      num_of_values_ = size;
    }
    return *this;
  }

  const std::string& name() { return attribute_name_; }

  // // Convert this class object to given `output` C IMM data structure
  void FormAttrValuesT_2(SaImmAttrValuesT_2* output) const;
  void FormSearchOneAttrT_2(SaImmSearchOneAttrT_2* output) const;
  void FormAdminOperationParamsT_2(SaImmAdminOperationParamsT_2* output) const;

 private:
  void FreeMemory();
  template <typename T> base_t* GenerateCppType(T* value);

 protected:
  std::string attribute_name_;
  void** attribute_values_;
  SaUint32T num_of_values_;
  SaImmValueTypeT attribute_type_;
  std::vector<base_t*> values_;
};

class AttributeDefinition : public AttributeProperty {
 public:
  explicit AttributeDefinition(const std::string& name,
                               SaImmAttrFlagsT flags)
      : AttributeProperty{name}, attribute_flags_{flags} {}

  ~AttributeDefinition() {}

  // Convert this class object to given `output` C IMM data structure
  void FormAttrDefinitionT_2(SaImmAttrDefinitionT_2* output) const;

 private:
  SaImmAttrFlagsT attribute_flags_;
};

class AttributeModification : public AttributeProperty {
 public:
  explicit AttributeModification(const std::string& name,
                                 SaImmAttrModificationTypeT type)
      : AttributeProperty{name}, modification_type_{type} {}

  ~AttributeModification() {}

  // Convert this class object to given `output` C IMM data structure
  void FormAttrModificationT_2(SaImmAttrModificationT_2* output) const;

 private:
  SaImmAttrModificationTypeT modification_type_;
};

template <>
inline base_t* AttributeProperty::GenerateCppType<SaInt32T>(SaInt32T* value) {
  return new i32_t(*value);
}

template <>
inline base_t* AttributeProperty::GenerateCppType<SaUint32T>(SaUint32T* value) {
  return new u32_t(*value);
}

template <>
inline base_t* AttributeProperty::GenerateCppType<SaInt64T>(SaInt64T* value) {
  return new i64_t(*value);
}

template <>
inline base_t* AttributeProperty::GenerateCppType<SaUint64T>(SaUint64T* value) {
  return new u64_t(*value);
}

template <>
inline base_t* AttributeProperty::GenerateCppType<CppSaTimeT>(CppSaTimeT* value) {
  return new t64_t(cpptime_t(value->time));
}

template <>
inline base_t* AttributeProperty::GenerateCppType<SaFloatT>(SaFloatT* value) {
  return new f64_t(*value);
}

template <>
inline base_t* AttributeProperty::GenerateCppType<SaDoubleT>(SaDoubleT* value) {
  return new d64_t(*value);
}

template <>
inline base_t* AttributeProperty::GenerateCppType<SaNameT>(SaNameT* value) {
  return new name_t(*value);
}

template <>
inline base_t* AttributeProperty::GenerateCppType<SaStringT>(SaStringT* value) {
  return new string_t(*value);
}

template <>
inline base_t* AttributeProperty::GenerateCppType<SaConstStringT>(SaConstStringT* value) {
  return new string_t(*value);
}

template <>
inline base_t* AttributeProperty::GenerateCppType<std::string>(std::string* value) {
  return new string_t(*value);
}

template <>
inline base_t* AttributeProperty::GenerateCppType<SaAnyT>(SaAnyT* value) {
  return new any_t(*value);
}

// For internal use only
using ListOfAttributePropertyPtr     = std::vector<AttributeProperty*>;
using ListOfAttributeModificationPtr = std::vector<AttributeModification*>;
using ListOfAttributeDefinitionPtr   = std::vector<AttributeDefinition*>;

#endif  // SRC_LOG_IMMWRP_COMMON_IMM_ATTRIBUTE_H_
