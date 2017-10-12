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

#ifndef LOG_IMMWRP_DEMO_IMMWRPCFG_H_
#define LOG_IMMWRP_DEMO_IMMWRPCFG_H_

#include <string>
#include <vector>
#include <utility>
#include "experimental/immcpp/api/include/om_handle.h"
#include "experimental/immcpp/api/include/om_admin_owner_handle.h"
#include "experimental/immcpp/api/include/om_ccb_object_create.h"
#include "experimental/immcpp/api/include/om_ccb_object_modify.h"
#include "experimental/immcpp/api/include/om_class_description_get.h"


//>
// The command-line utility is to configure attributes for an IMM object.
// This cloned from 'imm/tools/imm_cfg.c' then use C++ IMM interfaces to do
// the same utilities as `immcfg` command do.
// For example:
// Create object(s): immcppcfg -c class_name object1_dn [object2_dn]
//               -a attr_name1 [-a attr_name2]
// Modify attribute(s): immcppcfg  object1_dn [object2_dn]
//               -a attr_name1[+|-]=attr_value1 [-a attr_name2[+|-]=attr_value2]
// Delete object(s): immcppcfg -d object1_dn [object2_dn]
// The user use '-v' option to verbose print to get detail information of
// object's name and attribute's info that parsed from input command-line
//<

enum CfgModifyAttributeT {
  KModifiedAddedAttribute = 1,
  KModifiedReplacedAttribute = 2,
  KModifiedDeletedAttribute = 3
};

enum CfgOperationT {
  KInvalidOperation = 0,
  KCreateObject = 1,
  KDeleteObject = 2,
  KModifyObject = 3,
  KDeleteClass = 4,
  KChangeClass = 5,
  KAdminOnwerClear = 6,
//    KLoadFile = 7,
//    KValidateImmFile = 8,
};

struct AttributeInfoT {
    std::string name;
    std::vector<void*> value;
    SaImmValueTypeT type;
    CfgModifyAttributeT modify_type;  // It just useful for modifying object
};

struct ObjectInfoT {
  AttributeInfoT* rdn;
  std::string parent;
};

class ImmCppCfg {
 public:
  // Construct.
  ImmCppCfg();
  ~ImmCppCfg();

  // Create new object(s) of the specified class, initialize attributes with
  //  values from input command-line.
  bool CreateObject(
      const std::vector<std::string> &objects_str,
      const std::string &class_name,
      const std::vector<std::string> &attributes_str);

  // Modify object(s) with the attributes and its value specified from the
  // input command-line
  bool ModifyObject(
      const std::vector<std::string> &object_names,
      const std::vector<std::string> &attributes_str);

  // Delete object(s) specified in the input command-line
  bool DeleteObject(
      const std::vector<std::string> &object_names);

  // Delete class(es) specified in the input command-line
  bool DeleteClass(
      const std::vector<std::string> &class_names);

  // Clear admin owner specified in the input command-line
  bool ClearAdminOwner(
      const std::vector<std::string> &object_names);

  // Initialize om handle and admin owner handle
  bool InitHandle(bool use_admin_owner, std::string admin_owner_name);

 private:
  // List of all attributes's info (including name, value, type) that
  // is got from command-line
  std::vector<AttributeInfoT*> attributes_list_;
  // List of all objects info (rdn and parent) that fetch from objects name
  // string
  std::vector<ObjectInfoT*> objects_info_list_;

  immom::ImmOmHandle *om_handle_;
  immom::ImmOmAdminOwnerHandle *admin_owner_handle_;

  // Set attributes for ccb object create
  void SetNewAttribute(
      immom::ImmOmCcbObjectCreate *ccb_object, AttributeInfoT *attribute);
  // Set attribute for ccb object modify. The setting depends on type of
  // modification and it can be "ReplaceAttributeValue, AddAttributeValue,
  // or DeleteAttributeValue)
  void SetModifyAttribute(
      immom::ImmOmCcbObjectModify *ccb_obj, AttributeInfoT *attribute);
  template<typename T>
  void SetModifyAttributeValue(
      immom::ImmOmCcbObjectModify *ccb_object,
      const std::string &attribute_name,
      const std::vector<T*> &attribute_value,
      const CfgModifyAttributeT &modify_type) const;

  void FreeAttribute(AttributeInfoT *attribute);
  void FreeAttributeValue(void *value, SaImmValueTypeT type);

  // Get class's name of object
  SaImmClassNameT GetClassName(const std::string &object_name);

  // Allocate memory for attribute's value
  void *AllocateAttributeValue(
      const SaImmValueTypeT &attrValueType, const std::string &value_str);

  // Fetch and get attribute's info value from input string
  // for creating new object with the specified initialized attributes
  AttributeInfoT* FetchNewAttributeInfo(
      const immom::ImmOmClassDescriptionGet &class_desctiption,
      const std::string &name_attr,
      bool is_rdn);
  bool GetNewAttributeInfo(
      const immom::ImmOmClassDescriptionGet& class_description,
      const std::vector<std::string> &attributes_str);
  // Get the RDN and parent from object name string
  bool GetNewRdnAndParent(
      const immom::ImmOmClassDescriptionGet& class_description,
      const std::vector<std::string> &object_str);

  // Fetch and get attribute's info from input string
  // for modifying the specified attributes of objects
  AttributeInfoT* FetchModifiedAttributeInfo(
      const immom::ImmOmClassDescriptionGet &class_desctiption,
      const std::string &attributes_str);
  bool GetModifiedAttributeInfo(
      const immom::ImmOmClassDescriptionGet& class_description,
      const std::vector<std::string> &attributes_str);

  DELETE_COPY_AND_MOVE_OPERATORS(ImmCppCfg);
};


#endif  // LOG_IMMWRP_DEMO_IMMWRPCFG_H_
