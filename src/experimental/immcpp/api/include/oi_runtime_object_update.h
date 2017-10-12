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

#ifndef SRC_LOG_IMMWRP_IMMOI_OI_RUNTIME_OBJECT_UPDATE_H_
#define SRC_LOG_IMMWRP_IMMOI_OI_RUNTIME_OBJECT_UPDATE_H_


#include <string>
#include <vector>

#include "ais/include/saImmOi.h"
#include "experimental/immcpp/api/common/common.h"
#include "experimental/immcpp/api/common/imm_attribute.h"
#include "base/logtrace.h"

namespace immoi {

//>
// Update attribute list_of_ptr_to_values on given IMM object.
//
// The update could be replacing existing list_of_ptr_to_values to new ones or
// delete some list_of_ptr_to_values or all from existing list_of_ptr_to_values or
// add one or some list_of_ptr_to_values to existing ones.
//
// Usage example:
// ImmOiRtObjectUpdate update{handle, dn};
//
// SaUint64T filesize = 1000;
// update.ReplaceAttributeValue("filesize", &filesize);
//
// SaConstStringT dest1 = "dest1";
// SaConstStringT dest2 = "dest2";
// std::vector<SaConstStringT> dest = {&dest1, &dest2};
// update.ReplaceAttributeValue("saDestination", dest);
//
// if (update.UpdateObject() == false) {
//   // ERR handling.
//}
//<
class ImmOiRtObjectUpdate : public ImmBase {
 public:
  // Construct the object with given object name
  explicit ImmOiRtObjectUpdate(
      const SaImmOiHandleT& oi_handle,
      const std::string& object_name);
  ~ImmOiRtObjectUpdate();

  // Replace/delete/add value(s) to existing one(s).
  // T must be AIS data types or std::string.
  // NOTE: must explicitly give typename T if passing value NULL.
  // E.g:
  // SaStringT str{"hello"};
  // ReplaceAttributeValue("name", &str);
  // SaUint64T u64 = 10, u64_2 = 20;
  // std::vector<SaUint64T*> v{&u64, &u64_2};
  // ReplaceAttributeValue("name2", v);
  template<typename T> ImmOiRtObjectUpdate&
  ReplaceAttributeValue(const std::string& name, T* ptr_to_value = nullptr);

  template<typename T> ImmOiRtObjectUpdate&
  ReplaceAttributeValue(const std::string& name,
                        const std::vector<T*>& list_of_ptr_to_values);

  template<typename T> ImmOiRtObjectUpdate&
  DeleteAttributeValue(const std::string& name, T* ptr_to_value);

  template<typename T> ImmOiRtObjectUpdate&
  DeleteAttributeValue(const std::string& name,
                       const std::vector<T*>& list_of_ptr_to_values);

  template<typename T> ImmOiRtObjectUpdate&
  AddAttributeValue(const std::string& name, T* ptr_to_value);

  template<typename T> ImmOiRtObjectUpdate&
  AddAttributeValue(const std::string& name,
                    const std::vector<T*>& list_of_ptr_to_values);

  // Return true if updating attributes successfully.
  // Invoke ais_error() to get returned AIS code.
  bool UpdateObject();

 private:
  template<typename T> ImmOiRtObjectUpdate&
  ModifyAttributeValue(const std::string&,
                       const std::vector<T*>&,
                       SaImmAttrModificationTypeT);
  void FreeAllocatedMemory();

 private:
  SaImmOiHandleT oi_handle_;
  std::string object_name_;
  std::vector<AttributeModification*> attribute_mods_;

  DELETE_COPY_AND_MOVE_OPERATORS(ImmOiRtObjectUpdate);
};

template<typename T>
ImmOiRtObjectUpdate& ImmOiRtObjectUpdate::ModifyAttributeValue(
    const std::string& name,
    const std::vector<T*>& list_of_ptr_to_values,
    SaImmAttrModificationTypeT type) {
  TRACE_ENTER();
  // typename T must be one of AIS data types or std::string. Not support
  // SaInt8T, and SaInt16T types as no SaImmValueTypeT goes with them.
  SA_AIS_TYPE_CHECK(T);
  AttributeModification* attribute = new AttributeModification(name, type);
  assert(attribute != nullptr);
  attribute->set_value<T>(list_of_ptr_to_values);
  attribute_mods_.push_back(attribute);
  return *this;
}

template<typename T>
ImmOiRtObjectUpdate& ImmOiRtObjectUpdate::ReplaceAttributeValue(
    const std::string& name, const std::vector<T*>& list_of_ptr_to_values) {
  return ModifyAttributeValue<T>(name, list_of_ptr_to_values,
                                 SA_IMM_ATTR_VALUES_REPLACE);
}

template<typename T>
ImmOiRtObjectUpdate& ImmOiRtObjectUpdate::ReplaceAttributeValue(
    const std::string& name, T* ptr_to_value) {
  if (ptr_to_value != nullptr) {
    return ReplaceAttributeValue<T>(name, std::vector<T*>{ptr_to_value});
  } else {
    return ReplaceAttributeValue<T>(name, std::vector<T*>{});
  }
}

template<typename T>
ImmOiRtObjectUpdate& ImmOiRtObjectUpdate::AddAttributeValue(
    const std::string& name, const std::vector<T*>& list_of_ptr_to_values) {
  return ModifyAttributeValue<T>(name, list_of_ptr_to_values,
                                 SA_IMM_ATTR_VALUES_ADD);
}

template<typename T>
ImmOiRtObjectUpdate& ImmOiRtObjectUpdate::AddAttributeValue(
    const std::string& name, T* ptr_to_value) {
  if (ptr_to_value != nullptr) {
    return AddAttributeValue<T>(name, std::vector<T*>{ptr_to_value});
  } else {
    return AddAttributeValue<T>(name, std::vector<T*>{});
  }
}

template<typename T>
ImmOiRtObjectUpdate& ImmOiRtObjectUpdate::DeleteAttributeValue(
    const std::string& name, const std::vector<T*>& list_of_ptr_to_values) {
  return ModifyAttributeValue<T>(name, list_of_ptr_to_values,
                                 SA_IMM_ATTR_VALUES_DELETE);
}

template<typename T>
ImmOiRtObjectUpdate& ImmOiRtObjectUpdate::DeleteAttributeValue(
    const std::string& name, T* ptr_to_value) {
  if (ptr_to_value != nullptr) {
    return DeleteAttributeValue<T>(name, std::vector<T*>{ptr_to_value});
  } else {
    return DeleteAttributeValue<T>(name, std::vector<T*>{});
  }
}

}  // namespace immoi

#endif  // SRC_LOG_IMMWRP_IMMOI_OI_RUNTIME_OBJECT_UPDATE_H_
