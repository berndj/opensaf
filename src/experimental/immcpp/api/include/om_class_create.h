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

#ifndef SRC_LOG_IMMWRP_IMMOM_OM_CLASS_CREATE_H_
#define SRC_LOG_IMMWRP_IMMOM_OM_CLASS_CREATE_H_

#include <string.h>
#include <string>
#include <vector>

#include "ais/include/saImmOm.h"
#include "experimental/immcpp/api/common/common.h"
#include "experimental/immcpp/api/common/imm_attribute.h"
#include "base/logtrace.h"

namespace immom {

//
// This file provides classes for creating config/runtime IMM object class.
//
// To create configurable class, use ImmOmCfgClassCreate class,
// otherwise use ImmOmRtClassCreate.
//

//>
// Non-standalone(base) class which provides common methods for
// ImmOmRtClassCreate and ImmOmCfgClassCreate to create configurable/runtime
// class.
//
// Use SetAttributeProperties() to setup object class layout. Please note that,
// default attribute flags have different values depending on the call comes
// from ImmOmRtClassCreate or ImmOmCfgClassCreate class object.
//
// If the user want to use other flags value than default ones, provide that
// flags value to SetAttributeProperties() method.
//<
class ImmOmClassCreate : public ImmBase {
 public:
  virtual ~ImmOmClassCreate();

  // Set properties for given attribute name. When value is nullptr,
  // need to specify the typename T. T must be AIS data types or std::string.
  // Note: Have to explicitly give typename T in case of passing NULL value.
  // E.g:
  // SaConstStringT value = "hello";
  // SaImmAttrFlagsT flags = SA_IMM_ATTR_CACHED;
  // SetAttributeProperties("name", &value, flags);
  // SetAttributeProperties<SaStringT>("rdn", flags);
  template<typename T> ImmOmClassCreate&
  SetAttributeProperties(const std::string& name,
                         SaImmAttrFlagsT flags,
                         T* ptr_to_value = nullptr);

  // Use this method for default flags - detect based on class type.
  // T must be AIS data types or std::string.
  // Note: Have to explicitly give typename T in case of passing NULL value.
  // Default flags value is:
  // With `configurable`: flags = SA_IMM_ATTR_WRITABLE | SA_IMM_ATTR_CONFIG
  // With `runtime`     : flags = SA_IMM_ATTR_CACHED   | SA_IMM_ATTR_RUNTIME
  template<typename T> ImmOmClassCreate&
  SetAttributeProperties(const std::string& name, T* ptr_to_value = nullptr);

  // Return true if creating the object successfully.
  // Invoke ais_error() to get returned AIS code.
  bool CreateClass();

 protected:
  // Not allow to construct the object alone.
  // It has to be construct via derived classes.
  explicit ImmOmClassCreate(const SaImmHandleT& om_handle,
                            const std::string& class_name,
                            bool configurable);
  void FreeAllocatedMemory();

 protected:
  SaImmHandleT om_handle_;
  std::string class_name_;
  SaImmClassCategoryT category_;
  ListOfAttributeDefinitionPtr list_of_attribute_definitions_;

  DELETE_COPY_AND_MOVE_OPERATORS(ImmOmClassCreate);
};


//>
// Create a runtime Object Class with class name given.
//
// Example:
// ImmOmRtClassCreate createobj{omhandle, "SaLogStream"};
// createobj.SetAttributeProperties<SaStringT>("rdn", SA_IMM_ATTR_RDN);
//
// SaUint64T val = 100;
// createobj.SetAttributeProperties("attr1", &val); // RUNTIME | CACHED
//
// Note: User does not need to set SA_IMM_ATTR_RUNTIME to flags
// as it is automatically enabled inside the method SetAttributeProperties().
//<
class ImmOmRtClassCreate : public ImmOmClassCreate {
 public:
  explicit ImmOmRtClassCreate(const SaImmHandleT& om_handle,
                              const std::string& class_name)
      : ImmOmClassCreate(om_handle, class_name, false) {}
  ~ImmOmRtClassCreate() {}

  DELETE_COPY_AND_MOVE_OPERATORS(ImmOmRtClassCreate);
};


//>
// Create a configurable Object Class with class name given.
//
// Example:
// ImmOmCfgClassCreate createcfgobj{omhandle, "SaLogStreamConfig"};
// createobj.SetAttributeProperties<SaStringT>("rdn", SA_IMM_ATTR_RDN);
//
// SaUint64T val = 100;
// createobj.SetAttributeProperties("attr1", &val);  // Writable attribute
//
// Note: User does not need to set SA_IMM_ATTR_CONFIG
// to flags as it will automatically enable within SetAttributeProperties().
//<
class ImmOmCfgClassCreate : public ImmOmClassCreate {
 public:
  explicit ImmOmCfgClassCreate(const SaImmHandleT& om_handle,
                               const std::string& class_name)
      : ImmOmClassCreate(om_handle, class_name, true) {}
  ~ImmOmCfgClassCreate() {}

  DELETE_COPY_AND_MOVE_OPERATORS(ImmOmCfgClassCreate);
};

template<typename T> ImmOmClassCreate&
ImmOmClassCreate::SetAttributeProperties(
    const std::string& name,
    SaImmAttrFlagsT flags,
    T* ptr_to_value) {
  TRACE_ENTER();
  // typename T must be one of AIS data types or std::string. Not support
  // SaInt8T, and SaInt16T types as no SaImmValueTypeT goes with them.
  SA_AIS_TYPE_CHECK(T);

  if (category_ == SA_IMM_CLASS_CONFIG) {
    flags |= SA_IMM_ATTR_CONFIG;
  } else {
    flags |= SA_IMM_ATTR_RUNTIME;
  }

  AttributeDefinition* attribute = new AttributeDefinition(name, flags);
  assert(attribute != nullptr);
  attribute->set_value<T>(ptr_to_value);
  list_of_attribute_definitions_.push_back(attribute);
  return *this;
}

template<typename T> ImmOmClassCreate&
ImmOmClassCreate::SetAttributeProperties(const std::string& name,
                                         T* ptr_to_value) {
  SaImmAttrFlagsT flag;
  if (category_ == SA_IMM_CLASS_CONFIG) {
    flag = SA_IMM_ATTR_WRITABLE;
  } else {
    flag = SA_IMM_ATTR_CACHED;
  }
  return SetAttributeProperties<T>(name, flag, ptr_to_value);
}

}  // namespace immom

#endif  // SRC_LOG_IMMWRP_IMMOM_OM_CLASS_CREATE_H_
