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

#ifndef SRC_LOG_IMMWRP_IMMOM_OM_SEARCH_HANDLE_H_
#define SRC_LOG_IMMWRP_IMMOM_OM_SEARCH_HANDLE_H_

#include <string.h>
#include <string>
#include <vector>
#include "ais/include/saImmOm.h"
#include "base/logtrace.h"
#include "experimental/immcpp/api/common/common.h"

//------------------------------------------------------------------------------
// Create an IMM OM Search Iterator handle
//
// To create a search iterator handle the ImmOmSearchHandle class is used. The
// constructor can take an object of type ImmOmSearchCriteria as input.
// ImmOmSearchCriteria is used to define how to search and what to search for.
// The actual search happens when the handle is initialized. To iterate over
// the result the ImmOmSearchNext API shall be used.
//
// Example:
// Here an example of a complete search sequence could be given including
// filling in search criteria, initialize the handle and get the result.
// Examples in the following structure and class descriptions may then be
// omitted
//
//------------------------------------------------------------------------------

namespace immom {
//>
// Setup search parameters and attribute value fetch options.
//
// This class is used to create an object for setting and storing search
// criteria. Search criteria consists of two parts:
// 1) Search parameters
//    - Search root - defining where in the model to start the search
//    - Scope of search - defining how the model shall be traversed
//    - Search information - used to define what objects to search for
// 2) Attribute value fetch parameters (will be applied on all found objects).
//    Has three options: fetch all attribute, fetch some attribute,
//    or fetch nothing.
//
// Example:
// ImmOmSearchCriteria setting;
// setting.SetSearchClassName("SaLogStream");
//<
class ImmOmSearchHandle;
class ImmOmSearchCriteria {
 public:
  ImmOmSearchCriteria();
  ~ImmOmSearchCriteria();

  // Setup where to start searching. Default is searching from root.
  ImmOmSearchCriteria& SetSearchRoot(const std::string& search_root);

  // Indicate that the scope of the operation is targeted to a single object.
  ImmOmSearchCriteria& UseOneScope();

  // Indicate that the scope of the operation is targeted to one object
  // and its direct children.
  ImmOmSearchCriteria& UseSubLevelScope();

  // Indicates that the scope of the operation is targeted to one object
  // and the entire subtree rooted at that object.
  ImmOmSearchCriteria& UseSubTreeScope();

  // Search for IMM objects which belongs to given class name
  ImmOmSearchCriteria& SetSearchClassName(const std::string& class_name);

  // Search object name for given attribute name with specific value
  // typename T must be AIS data type such as SaInt32T, SaNameT, SaStringT,...
  // or std::string. Get compiling error if using other data types.
  // Example:
  // SaConstStringT value = "hello";
  // SetSearchParam("name", &value);
  // SetSearchParam<SaStringT>("stringattr", nullptr);
  template <typename T> ImmOmSearchCriteria&
  SetSearchParam(const std::string& name, T* ptr_to_value);

  // Fetch all attributes
  ImmOmSearchCriteria& FetchAttributes();
  // Only fetch some or none.
  ImmOmSearchCriteria& FetchAttributes(const std::string& name);
  ImmOmSearchCriteria&
  FetchAttributes(const std::vector<std::string>& list_of_names);
  // Introduce this method to let users change `ImmOmSearchCriteria` object
  // back to `Fetch no attribute` option from `Fetch all` or `Fetch some` ones.
  // If no `Fetchxxx` method is invoked, default is `Fetch No Attribute`.
  ImmOmSearchCriteria& FetchNoAttribute();

 private:
  // Set default value to @search_param_
  void ResetSearchParam();

 private:
  friend class ImmOmSearchHandle;

  // Hold a copy of attribute name and attribute value
  // giving to SetSearchParam() to avoid mishandling
  // (c_str() points to invalid memory) in case of user
  // passes rvalue instead of lvalue.
  // e.g: SetSearchParam("attributeName", "value");
  std::string attribute_name_;
  std::string attribute_value_;
  SaConstStringT ptr_to_value_;

  std::string search_root_;
  SaImmScopeT scope_;
  SaImmSearchOptionsT option_;
  SaImmSearchParametersT_2 search_param_;
  std::vector<std::string> attributes_;

  DELETE_COPY_AND_MOVE_OPERATORS(ImmOmSearchCriteria);
};

//>
// Manage an IMM OM Search Iterator handle
//
// The search handle is used with the ImmOmSearchNext API to iterate through
// all found objects that match the search criteria.
//
// Search criteria can be given when creating the handle object.
// An object can also be created without giving any search criteria. This will
// give the names of all objects in the IMM model.

// If no search criteria is provided, it means do search all IMM objects
// from root without fetching attribute values.
//
// Example:
// ImmOmSearchHandle searchhandleobj{omhandle};
// if (searchhandleobj.InitializeHandle() == false) {
//   // ER handling
// }
// SaImmSearchHandleT searchhandle = searchhandleobj.GetHandle();
//<
class ImmOmSearchHandle : public ImmBase {
 public:
  // Create search handle with default search setting - search all IMM objects.
  explicit ImmOmSearchHandle(const SaImmHandleT& om_handle);
  // Create search handle with given search setting
  explicit ImmOmSearchHandle(const SaImmHandleT& om_handle,
                             const ImmOmSearchCriteria& search_criteria);
  ~ImmOmSearchHandle();

  // Returns false if fail
  // Use the ais_error() to get returned AIS error code.
  bool InitializeHandle();
  bool FinalizeHandle();

  // Get the IMM OM search handle. Will be implicitly initialized if needed
  // Returns 0 if no valid handle
  // Use ais_error() to get AIS return code from initialize operation.
  SaImmSearchHandleT GetHandle();

 private:
  SaImmHandleT om_handle_;
  const ImmOmSearchCriteria* setting_;
  SaImmSearchHandleT search_handle_;

  DELETE_COPY_AND_MOVE_OPERATORS(ImmOmSearchHandle);
};

//------------------------------------------------------------------------------
// ImmOmSearchCriteria inline methods
//------------------------------------------------------------------------------
inline void ImmOmSearchCriteria::ResetSearchParam() {
  TRACE_ENTER();
  search_param_.searchOneAttr.attrName      = nullptr;
  search_param_.searchOneAttr.attrValueType = SA_IMM_ATTR_SAINT32T;
  search_param_.searchOneAttr.attrValue     = nullptr;
}

inline ImmOmSearchCriteria&
ImmOmSearchCriteria::SetSearchRoot(const std::string& search_root) {
  TRACE_ENTER();
  search_root_ = search_root;
  return *this;
}

inline ImmOmSearchCriteria& ImmOmSearchCriteria::UseOneScope() {
  TRACE_ENTER();
  scope_ = SA_IMM_ONE;
  return *this;
}

inline ImmOmSearchCriteria& ImmOmSearchCriteria::UseSubLevelScope() {
  TRACE_ENTER();
  scope_ = SA_IMM_SUBLEVEL;
  return *this;
}

inline ImmOmSearchCriteria& ImmOmSearchCriteria::UseSubTreeScope() {
  TRACE_ENTER();
  scope_ = SA_IMM_SUBTREE;
  return *this;
}

inline ImmOmSearchCriteria& ImmOmSearchCriteria::FetchNoAttribute() {
  TRACE_ENTER();
  option_ = SA_IMM_SEARCH_GET_NO_ATTR;
  attributes_.clear();
  return *this;
}

inline ImmOmSearchCriteria& ImmOmSearchCriteria::FetchAttributes() {
  TRACE_ENTER();
  option_ = SA_IMM_SEARCH_GET_ALL_ATTR;
  attributes_.clear();
  return *this;
}

inline ImmOmSearchCriteria&
ImmOmSearchCriteria::FetchAttributes(const std::string& name) {
  TRACE_ENTER();
  option_ = SA_IMM_SEARCH_GET_SOME_ATTR;
  attributes_.push_back(name);
  return *this;
}

inline ImmOmSearchCriteria&
ImmOmSearchCriteria::FetchAttributes(
    const std::vector<std::string>& list_of_names) {
  TRACE_ENTER();
  option_     = SA_IMM_SEARCH_GET_SOME_ATTR;
  attributes_ = list_of_names;
  return *this;
}

template <typename T> ImmOmSearchCriteria&
ImmOmSearchCriteria::SetSearchParam(
    const std::string& name, T* ptr_to_value) {
  TRACE_ENTER();
  // typename T must be one of AIS data types. or std::string.  Not support
  // SaInt8T, and SaInt16T types as no SaImmValueTypeT goes with them.
  SA_AIS_TYPE_CHECK(T);
  attribute_name_ = name;
  option_ |= SA_IMM_SEARCH_ONE_ATTR;
  SaImmSearchOneAttrT_2* search_one = &search_param_.searchOneAttr;
  search_one->attrName = const_cast<char*>(attribute_name_.c_str());
  search_one->attrValue = ptr_to_value;
  search_one->attrValueType = ImmBase::GetAttributeValueType<T>();
  return *this;
}

inline ImmOmSearchCriteria&
ImmOmSearchCriteria::SetSearchClassName(const std::string& class_name) {
  TRACE_ENTER();
  attribute_value_ = class_name;
  ptr_to_value_    = attribute_value_.c_str();
  return SetSearchParam("SaImmAttrClassName", &ptr_to_value_);
}

}  // namespace immom

#endif  // SRC_LOG_IMMWRP_IMMOM_OM_SEARCH_HANDLE_H_
