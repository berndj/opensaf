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

#ifndef SRC_LOG_IMMWRP_IMMOM_OM_ACCESSOR_GET_H_
#define SRC_LOG_IMMWRP_IMMOM_OM_ACCESSOR_GET_H_

#include <string>
#include <vector>

#include "ais/include/saImmOm.h"
#include "experimental/immcpp/api/common/common.h"
#include "base/logtrace.h"

namespace immom {

//>
// Object accessor to obtain the values assigned to some attributes
// of an IMM object. If no attribute name is provided, the values
// of all attributes of the object are returned.
//
// Example:
// Get values for attributes logFileSysConfig and logRootDirectory in object
// "logConfig=1,safApp=safLogService"
//
// std::string dn = "logConfig=1,safApp=safLogService";
// ImmOmAccessorGet accessget{accesshandle, dn};
//
// accessget.SetAttributeNames({"logFileSysConfig", "logRootDirectory"});
//
// if (accessget.FetchAttributeValue() == false) {
//   // Error handling...
//   return;
// }
//
// SaStringT* dirname = accessget.GetSingleValue<SaStringT>("logRootDirectory");
// SaUint32T* config = accessget.GetSingleValue<SaUint32T>("logFileSysConfig");
//<
class ImmOmAccessorGet : public ImmBase {
 public:
  explicit ImmOmAccessorGet(const SaImmAccessorHandleT& accessor_handle,
                            const std::string& object_name);
  ~ImmOmAccessorGet();

  // Set up accessor criteria, including object name and attribute names.
  ImmOmAccessorGet& SetAttributeNames(const std::string& name);
  ImmOmAccessorGet&
  SetAttributeNames(const std::vector<std::string>& list_of_names);

  // Return true if fetching attribute values successfully.
  // Use the ais_error() to get AIS error code.
  bool FetchAttributeValue();

  // Return pointer to value of given attribute name,
  // nullptr if FetchAttributeValue() returns false or the given name does not
  // belong to given object name.
  // typename T must be AIS data type such as SaInt32T, SaNameT, SaStringT,...
  // or std::string.
  // Example:
  // SaStringT* value = GetSingleValue<SaStringT>("name");
  template <typename T> T* GetSingleValue(const std::string& name) const;

  // Return list of values of given multiple attribute name.
  // Empty list if FetchAttributeValue() fails or the given name does not
  // belong to given object name.
  // typename T must be AIS data type such as SaInt32T, SaNameT, SaStringT,...
  // or std::string
  template <typename T> std::vector<T>
  GetMultiValue(const std::string& name) const;

  // Return nullptr if given attribute name does not exist.
  const SaImmValueTypeT* GetAttributeValueType(const std::string& name) const;
  const SaUint32T* GetAttributeValuesNumber(const std::string& name) const;

  // Return all attributes - including its properties. See AIS for information
  // about SaImmAttrValuesT_2
  const std::vector<SaImmAttrValuesT_2*> GetAllAttributeProperties() const;

 private:
  std::string object_name_;
  std::vector<std::string> attributes_;
  SaImmAccessorHandleT accessor_handle_;
  SaImmAttrValuesT_2** attribute_values_;

  DELETE_COPY_AND_MOVE_OPERATORS(ImmOmAccessorGet);
};

inline ImmOmAccessorGet&
ImmOmAccessorGet::SetAttributeNames(const std::string& name) {
  TRACE_ENTER();
  attributes_.push_back(name);
  return *this;
}

inline ImmOmAccessorGet&
ImmOmAccessorGet::SetAttributeNames(
    const std::vector<std::string>& list_of_names) {
  TRACE_ENTER();
  attributes_ = list_of_names;
  return *this;
}


}  // namespace immom

#endif  // SRC_LOG_IMMWRP_IMMOM_OM_ACCESSOR_GET_H_
