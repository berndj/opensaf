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

// To be able to access directly private attributes of class
#define protected public
#define private public

#include <stdlib.h>
#include <string>
#include <vector>
#include "gtest/gtest.h"
#include "experimental/immcpp/api/include/oi_runtime_object_create.h"
#include "base/osaf_extended_name.h"

//>
// To verify interfaces of ImmOiRtObjectCreate class.
// Steps:
// 1) Create ImmOiRtObjectCreate object
// 2) Invoke one of its interfaces
// 3) Check if the private data is formed as expected or not
//<
static const char classname[] = "hello";
TEST(ImmOiRtObjectCreate, ImmOiRtObjectCreate) {
  immoi::ImmOiRtObjectCreate createobj{10, classname};
  ASSERT_EQ(createobj.oi_handle_, 10);
  ASSERT_EQ(createobj.class_name_, classname);
  ASSERT_EQ(createobj.parent_object_.empty(), true);
  ASSERT_EQ(createobj.list_of_attribute_properties_.empty(), true);
}

TEST(ImmOiRtObjectCreate, SetParentName) {
  const std::string parentobject = "a=b";
  immoi::ImmOiRtObjectCreate createobj{10, classname};
  createobj.SetParentName(parentobject);
  ASSERT_EQ(createobj.parent_object_, parentobject);
}

TEST(ImmOiRtObjectCreate, SetAttributeValue_NULL) {
  immoi::ImmOiRtObjectCreate createobj{10, classname};
  createobj.SetAttributeValue<SaStringT>("value", nullptr);

  ASSERT_EQ(createobj.list_of_attribute_properties_.size(), 1);

  // Check if the private data `createobj.attribute_values_`
  // is formed as expected or not.
  SaImmAttrValuesT_2 temp;
  createobj.list_of_attribute_properties_[0]->FormAttrValuesT_2(&temp);
  const char* attrname = temp.attrName;
  SaImmValueTypeT type = temp.attrValueType;
  SaUint32T number = temp.attrValuesNumber;
  void* value = temp.attrValues;

  ASSERT_EQ(std::string{attrname}, "value");
  ASSERT_EQ(type, SA_IMM_ATTR_SASTRINGT);
  ASSERT_EQ(number, 0);
  ASSERT_EQ(value, nullptr);

  createobj.FreeAllocatedMemory();
}

TEST(ImmOiRtObjectCreate, SetAttributeValue_SaUint32T) {
  SaUint32T val = 100;
  immoi::ImmOiRtObjectCreate createobj{10, classname};
  createobj.SetAttributeValue("value", &val);

  ASSERT_EQ(createobj.list_of_attribute_properties_.size(), 1);

  // Check if the private data `createobj.list_of_attribute_properties_`
  // is formed as expected or not.
  SaImmAttrValuesT_2 temp;
  createobj.list_of_attribute_properties_[0]->FormAttrValuesT_2(&temp);
  const char* attrname = temp.attrName;
  SaImmValueTypeT type = temp.attrValueType;
  SaUint32T number = temp.attrValuesNumber;
  void* value = temp.attrValues[0];

  ASSERT_EQ(std::string{attrname}, "value");
  ASSERT_EQ(type, SA_IMM_ATTR_SAUINT32T);
  ASSERT_EQ(number, 1);
  ASSERT_EQ(*(static_cast<SaUint32T*>(value)), val);

  createobj.FreeAllocatedMemory();
}

TEST(ImmOiRtObjectCreate, SetAttributeValue_SaFloatT) {
  SaFloatT val = 100;
  immoi::ImmOiRtObjectCreate createobj{10, classname};
  createobj.SetAttributeValue("value", &val);

  ASSERT_EQ(createobj.list_of_attribute_properties_.size(), 1);

  // Check if the private data `createobj.list_of_attribute_properties_`
  // is formed as expected or not.
  SaImmAttrValuesT_2 temp;
  createobj.list_of_attribute_properties_[0]->FormAttrValuesT_2(&temp);
  const char* attrname = temp.attrName;
  SaImmValueTypeT type = temp.attrValueType;
  SaUint32T number = temp.attrValuesNumber;
  void* value = temp.attrValues[0];

  ASSERT_EQ(std::string{attrname}, "value");
  ASSERT_EQ(type, SA_IMM_ATTR_SAFLOATT);
  ASSERT_EQ(number, 1);
  ASSERT_EQ(*(static_cast<SaFloatT*>(value)), val);

  createobj.FreeAllocatedMemory();
}

TEST(ImmOiRtObjectCreate, SetAttributeValue_SaStringT) {
  SaConstStringT val = "SaStringT";
  immoi::ImmOiRtObjectCreate createobj{10, classname};
  createobj.SetAttributeValue("value", &val);

  ASSERT_EQ(createobj.list_of_attribute_properties_.size(), 1);

  // Check if the private data `createobj.list_of_attribute_properties_`
  // is formed as expected or not.
  SaImmAttrValuesT_2 temp;
  createobj.list_of_attribute_properties_[0]->FormAttrValuesT_2(&temp);
  const char* attrname = temp.attrName;
  SaImmValueTypeT type = temp.attrValueType;
  SaUint32T number = temp.attrValuesNumber;
  void* value = temp.attrValues[0];

  ASSERT_EQ(std::string{attrname}, "value");
  ASSERT_EQ(type, SA_IMM_ATTR_SASTRINGT);
  ASSERT_EQ(number, 1);
  ASSERT_EQ(std::string{*(static_cast<SaStringT*>(value))}, std::string{val});

  createobj.FreeAllocatedMemory();
}

TEST(ImmOiRtObjectCreate, SetAttributeValue_SaNameT) {
  SaNameT val;
  SaConstStringT valref = "hello world";
  osaf_extended_name_lend(valref, &val);

  immoi::ImmOiRtObjectCreate createobj{10, classname};
  createobj.SetAttributeValue("value", &val);

  ASSERT_EQ(createobj.list_of_attribute_properties_.size(), 1);

  // Check if the private data `createobj.list_of_attribute_properties_`
  // is formed as expected or not.
  SaImmAttrValuesT_2 temp;
  createobj.list_of_attribute_properties_[0]->FormAttrValuesT_2(&temp);
  const char* attrname = temp.attrName;
  SaImmValueTypeT type = temp.attrValueType;
  SaUint32T number = temp.attrValuesNumber;
  void* value = temp.attrValues[0];

  ASSERT_EQ(std::string{attrname}, "value");
  ASSERT_EQ(type, SA_IMM_ATTR_SANAMET);
  ASSERT_EQ(number, 1);

  SaNameT* sanamet = static_cast<SaNameT*>(value);
  SaConstStringT outvalue = osaf_extended_name_borrow(sanamet);
  ASSERT_EQ(std::string{outvalue}, std::string{valref});

  createobj.FreeAllocatedMemory();
}

TEST(ImmOiRtObjectCreate, SetAttributeValue_SaStringT_SaUint64T) {
  SaConstStringT val = "SaStringT";
  SaUint64T val2 = 123456789;
  immoi::ImmOiRtObjectCreate createobj{10, classname};
  createobj.SetAttributeValue("value", &val);
  createobj.SetAttributeValue("value1", &val2);

  ASSERT_EQ(createobj.list_of_attribute_properties_.size(), 2);

  // Check if the private data `createobj.list_of_attribute_properties_`
  // is formed as expected or not.
  SaImmAttrValuesT_2 temp;
  createobj.list_of_attribute_properties_[0]->FormAttrValuesT_2(&temp);
  const char* attrname = temp.attrName;
  SaImmValueTypeT type = temp.attrValueType;
  SaUint32T number = temp.attrValuesNumber;
  void* value = temp.attrValues[0];

  ASSERT_EQ(std::string{attrname}, "value");
  ASSERT_EQ(type, SA_IMM_ATTR_SASTRINGT);
  ASSERT_EQ(number, 1);
  ASSERT_EQ(std::string{*(static_cast<SaStringT*>(value))}, std::string{val});

  // Check element #2
  SaImmAttrValuesT_2 temp2;
  createobj.list_of_attribute_properties_[1]->FormAttrValuesT_2(&temp2);
  const char* attrname1 = temp2.attrName;
  SaImmValueTypeT type1 = temp2.attrValueType;
  SaUint32T number1 = temp2.attrValuesNumber;
  void* value1 = temp2.attrValues[0];

  ASSERT_EQ(std::string{attrname1}, "value1");
  ASSERT_EQ(type1, SA_IMM_ATTR_SAUINT64T);
  ASSERT_EQ(number1, 1);
  ASSERT_EQ(*(static_cast<SaUint64T*>(value1)), val2);

  createobj.FreeAllocatedMemory();
}

TEST(ImmOiRtObjectCreate, SetAttributeValue_MultipleValue_SaUint64T) {
  SaUint64T val = 123456789, val1 = 987654321;
  const std::vector<SaUint64T*> vval{&val, &val1};
  immoi::ImmOiRtObjectCreate createobj{10, classname};
  createobj.SetAttributeValue("value", vval);

  ASSERT_EQ(createobj.list_of_attribute_properties_.size(), 1);

  // Check if the private data `createobj.list_of_attribute_properties_`
  // is formed as expected or not.
  SaImmAttrValuesT_2 temp;
  createobj.list_of_attribute_properties_[0]->FormAttrValuesT_2(&temp);
  const char* attrname = temp.attrName;
  SaImmValueTypeT type = temp.attrValueType;
  SaUint32T number = temp.attrValuesNumber;
  void* value = temp.attrValues[0];
  void* value1 = temp.attrValues[1];

  ASSERT_EQ(std::string{attrname}, "value");
  ASSERT_EQ(type, SA_IMM_ATTR_SAUINT64T);
  ASSERT_EQ(number, 2);
  ASSERT_EQ(*(static_cast<SaUint64T*>(value)), val);
  ASSERT_EQ(*(static_cast<SaUint64T*>(value1)), val1);

  createobj.FreeAllocatedMemory();
}
