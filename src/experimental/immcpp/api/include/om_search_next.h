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

#ifndef SRC_LOG_IMMWRP_IMMOM_OM_SEARCH_NEXT_H_
#define SRC_LOG_IMMWRP_IMMOM_OM_SEARCH_NEXT_H_

#include <string>
#include <vector>
#include "ais/include/saImmOm.h"
#include "experimental/immcpp/api/common/common.h"
#include "base/logtrace.h"

namespace immom {

//>
// IMM OM Search iterator - which is used to obtain the next object
// matching the search criteria (refer to ~/om_search_handle.h)
//
// Note: The search result can be iterated once only
//
// Use GetObjectName(), GetSingleValue() or GetMultiValue() methods
// to get object name and attribute values of given attribute names.
// These methods will return empty value if SearchNext() returns false.
//
// Example:
// ImmOmSearchNext searchnextobj{searchhandle};
//
// while (searchnextobj.SearchNext() == true) {
//   // Error handling...
// }
//<
class ImmOmSearchNext : public ImmBase {
 public:
  explicit ImmOmSearchNext(const SaImmSearchHandleT& search_handle);
  ~ImmOmSearchNext();

  // Perform saImmOmSearchNext_2() with giving IMM OM search handle.
  // Return true if the searching operation is successful.
  // Use ais_error() to get SA AIS error code.
  bool SearchNext();

  // Return found object name. Return empty string if SearchNext() gets false.
  const std::string& GetObjectName() const;

  // Return pointer to value of giving attribute name, nullptr
  // if SearchNext() returns false or not found the giving attribute name.
  // typename T MUST be AIS data type such as SaInt32T, SaNameT, SaStringT,...
  // or std::string.
  // Example:
  // SaStringT* value = GetSingleValue<SaStringT>("name");
  template <typename T> T* GetSingleValue(const std::string& name) const;

  // Return list of values of giving multiple attribute name.
  // Empty list if SearchNext() returns false or the giving name have not found.
  // typename T MUST be AIS data type such as SaInt32T, SaNameT, SaStringT,...
  // or std::string.
  // Example:
  // auto multiplevalue = GetMultipleValue<SaUint64T>("multipleattributename");
  template <typename T> std::vector<T>
  GetMultiValue(const std::string& name) const;

  // Return nullptr if given attribute name does not exist.
  const SaImmValueTypeT* GetAttributeValueType(const std::string& name) const;
  const SaUint32T* GetAttributeValueNumber(const std::string& name) const;

  // Return properties of given attribute `name` such as attribute value type,
  // attribute value number, attribute value, etc.
  // Return nullptr if the giving attribute does not exist.
  // Use this method if user has no idea about giving attribute's properties
  // such as data type, multiple value attribute or not, etc.
  const SaImmAttrValuesT_2* GetAttributeProperty(const std::string& name) const;

 private:
  // Hold a copy of IMM OM search handle
  SaImmSearchHandleT search_handle_;
  std::string object_name_;
  SaImmAttrValuesT_2** attribute_values_;

  DELETE_COPY_AND_MOVE_OPERATORS(ImmOmSearchNext);
};

inline const std::string& ImmOmSearchNext::GetObjectName() const {
  return object_name_;
}

}  // namespace immom

#endif  // SRC_LOG_IMMWRP_IMMOM_OM_SEARCH_NEXT_H_
