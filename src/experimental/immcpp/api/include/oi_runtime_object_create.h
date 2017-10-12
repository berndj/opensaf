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

#ifndef SRC_LOG_IMMWRP_IMMOI_OI_RUNTIME_OBJECT_CREATE_H_
#define SRC_LOG_IMMWRP_IMMOI_OI_RUNTIME_OBJECT_CREATE_H_

#include <string.h>
#include <string>
#include <vector>

#include "ais/include/saImmOi.h"
#include "experimental/immcpp/api/common/common.h"
#include "experimental/immcpp/api/common/imm_attribute.h"
#include "base/logtrace.h"

namespace immoi {

//>
// Create a IMM runtime object which has given class name.
//
// Set class name, parent name (optional) and
// list_of_ptr_to_values for that class's attributes via Set methods. Invoke
// `CreateObject()` to create runtime object.
//
// Usage example:
// ImmOiRtObjectCreate createobj{oihandle, "classname"};
// createobj.SetParentName("safApp=safLogService");
//
// SaUint64T filesize = 1000;
// createobj.SetAttributeValue("logStreamMaxLogFileSize", &filesize);
//
// if (createobj.CreateRtObject() == false) {
//   cerr << "Failed to create Rt object: " << saf_error(createobj.ais_error());
//   return;
// }
//<
class ImmOiRtObjectCreate : public ImmBase {
 public:
  // Construct the object with given class name (mandatory).
  explicit ImmOiRtObjectCreate(const SaImmOiHandleT& oi_handle,
                               const std::string& class_name);
  ~ImmOiRtObjectCreate();

  ImmOiRtObjectCreate& SetParentName(const std::string& parent_object);

  // Set single or multiple value(s) for given attribute name.
  // NOTE: must explicitly give typename T if passing value NULL.
  // E.g:
  // SaConstStringT ptr_to_value = "hello";
  // SetAttributeValue("name", &ptr_to_value);
  // SaUint64T val1 = 1000, val2 = 2000;
  // const std::vector<SaUint64T*> vvalue{&val1, &val2};
  // SetAttributeValue("name", vvalue);
  template<typename T> ImmOiRtObjectCreate&
  SetAttributeValue(const std::string& name, T* ptr_to_value = nullptr);
  template<typename T> ImmOiRtObjectCreate&
  SetAttributeValue(const std::string& name,
                    const std::vector<T*>& list_of_ptr_to_values);

  // Return true if creating the object successfully.
  // Invoke ais_error() to get returned AIS code.
  bool CreateObject();

 private:
  void FreeAllocatedMemory();

 private:
  SaImmOiHandleT oi_handle_;
  std::string class_name_;
  std::string parent_object_;
  ListOfAttributePropertyPtr list_of_attribute_properties_;

  DELETE_COPY_AND_MOVE_OPERATORS(ImmOiRtObjectCreate);
};

inline ImmOiRtObjectCreate&
ImmOiRtObjectCreate::SetParentName(const std::string& parent_object) {
  parent_object_ = parent_object;
  return *this;
}

template<typename T> ImmOiRtObjectCreate&
ImmOiRtObjectCreate::SetAttributeValue(const std::string& name,
                                       T* ptr_to_value) {
  TRACE_ENTER();
  // typename T must be AIS data types or std::string. Not support SaInt8T,
  // and SaInt16T types as no SaImmValueTypeT goes with them.
  SA_AIS_TYPE_CHECK(T);
  AttributeProperty* attribute = new AttributeProperty(name);
  assert(attribute != nullptr);
  attribute->set_value<T>(ptr_to_value);
  list_of_attribute_properties_.push_back(attribute);
  return *this;
}

template<typename T> ImmOiRtObjectCreate&
ImmOiRtObjectCreate::SetAttributeValue(
    const std::string& name, const std::vector<T*>& list_of_ptr_to_values) {
  TRACE_ENTER();
  // typename T must be one of AIS data types or std::string. Not support
  // SaInt8T, and SaInt16T types as no SaImmValueTypeT goes with them.
  SA_AIS_TYPE_CHECK(T);
  AttributeProperty* attribute = new AttributeProperty(name);
  assert(attribute != nullptr);
  attribute->set_value<T>(list_of_ptr_to_values);
  list_of_attribute_properties_.push_back(attribute);
  return *this;
}

}  // namespace immoi

#endif  // SRC_LOG_IMMWRP_IMMOI_OI_RUNTIME_OBJECT_CREATE_H_
