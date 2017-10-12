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

#ifndef SRC_LOG_IMMWRP_IMMOM_OM_CLASS_DESCRIPTION_GET_H_
#define SRC_LOG_IMMWRP_IMMOM_OM_CLASS_DESCRIPTION_GET_H_

#include <string>
#include <vector>

#include "ais/include/saImmOm.h"
#include "experimental/immcpp/api/common/common.h"
#include "base/logtrace.h"

namespace immom {

//>
// Get IMM class description of given IMM class name
//
// Example:
// ImmOmClassDescriptionGet classdefinition{omhandle, "SaLogStreamConfig"}
// if (classdefinition.GetClassDescription() == false) {
//   // Error handling...
//   return;
// }
//
// SaUinT64T value = classdefinition.GetDefaultValue<SaUinT64T>("name");
//<
class ImmOmClassDescriptionGet : public ImmBase {
 public:
  // Construct the object with giving IMM OM handle and class name.
  explicit ImmOmClassDescriptionGet(const SaImmHandleT& om_handle,
                                    const std::string& class_name);
  ~ImmOmClassDescriptionGet();

  bool GetClassDescription();
  const SaImmClassCategoryT GetClassCategory() const;

  bool is_configurable_class() const {
    return class_category_ == SA_IMM_CLASS_CONFIG;
  }

  // Return list of attribute definitions, include attribute names,
  // attribute value types, attribute flags and its default attribute values.
  // Empty list if `GetClassCategory` fails.
  // Use this method when user has no idea about attribute name as well as
  // its attribute data type (e.g: immlist -c "classname")
  const std::vector<SaImmAttrDefinitionT_2*> GetAllAttributeDefinition() const;

  // Return nullptr if not found or the giving attribute has empty value.
  // typename T must be AIS data type such as SaInt32T, SaNameT, SaStringT,...
  // or std::string.
  // Use this method when user knows for sure about data type for given name.
  template <typename T> T* GetDefaultValue(const std::string& name) const;

  // Use these methods if user has no idea about attribute's data type
  const SaImmValueTypeT* GetAttributeValueType(const std::string& name) const;

  const SaImmAttrFlagsT* GetAttributeFlags(const std::string& name) const;

 private:
  bool FreeClassDescription();

 private:
  SaImmHandleT om_handle_;
  std::string class_name_;
  SaImmClassCategoryT class_category_;
  SaImmAttrDefinitionT_2** attribute_definition_;

  DELETE_COPY_AND_MOVE_OPERATORS(ImmOmClassDescriptionGet);
};

inline const SaImmClassCategoryT
ImmOmClassDescriptionGet::GetClassCategory() const {
  TRACE_ENTER();
  return class_category_;
}

}  // namespace immom

#endif  // SRC_LOG_IMMWRP_IMMOM_OM_CLASS_DESCRIPTION_GET_H_
