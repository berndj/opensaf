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
#include "experimental/immcpp/api/include/oi_runtime_object_update.h"
#include "base/osaf_extended_name.h"

//>
// To verify interfaces of ImmOiRtObjectUpdate class.
// Steps:
// 1) Create ImmOiRtObjectUpdate object
// 2) Invoke one of its interfaces
// 3) Check if the private data is formed as expected or not
//<
static const char objname[] = "test=helloworld";
TEST(ImmOiRtObjectUpdate, ImmOiRtObjectUpdate) {
  immoi::ImmOiRtObjectUpdate updateobj{10, objname};
  ASSERT_EQ(updateobj.oi_handle_, 10);
  ASSERT_EQ(updateobj.object_name_, objname);
  ASSERT_EQ(updateobj.attribute_mods_.empty(), true);
}

TEST(ImmOiRtObjectUpdate, ReplaceAttributeValue_NULL) {
  immoi::ImmOiRtObjectUpdate updateobj{10, objname};
  updateobj.ReplaceAttributeValue<SaStringT>("value", nullptr);

  ASSERT_EQ(updateobj.attribute_mods_.size(), 1);

  // Check if the private data `updateobj.attribute_mods`
  // is formed as expected or not.
  SaImmAttrModificationT_2 temp;
  updateobj.attribute_mods_[0]->FormAttrModificationT_2(&temp);
  SaImmAttrValuesT_2* modAttr = &temp.modAttr;
  SaImmAttrModificationTypeT modType = temp.modType;

  const char* attrname = modAttr->attrName;
  SaImmValueTypeT type = modAttr->attrValueType;
  SaUint32T number = modAttr->attrValuesNumber;
  void* value = modAttr->attrValues;

  ASSERT_EQ(std::string{attrname}, "value");
  ASSERT_EQ(type, SA_IMM_ATTR_SASTRINGT);
  ASSERT_EQ(number, 0);
  ASSERT_EQ(value, nullptr);
  ASSERT_EQ(modType, SA_IMM_ATTR_VALUES_REPLACE);

  updateobj.FreeAllocatedMemory();
}

TEST(ImmOiRtObjectUpdate, ReplaceAttributeValue_SaInt64T) {
  SaInt64T val = -1000;
  immoi::ImmOiRtObjectUpdate updateobj{10, objname};
  updateobj.ReplaceAttributeValue("value", &val);

  ASSERT_EQ(updateobj.attribute_mods_.size(), 1);

  // Check if the private data `updateobj.attribute_mods`
  // is formed as expected or not.
  SaImmAttrModificationT_2 temp;
  updateobj.attribute_mods_[0]->FormAttrModificationT_2(&temp);
  SaImmAttrValuesT_2* modAttr = &temp.modAttr;
  SaImmAttrModificationTypeT modType = temp.modType;

  const char* attrname = modAttr->attrName;
  SaImmValueTypeT type = modAttr->attrValueType;
  SaUint32T number = modAttr->attrValuesNumber;
  void* value = modAttr->attrValues[0];

  ASSERT_EQ(std::string{attrname}, "value");
  ASSERT_EQ(type, SA_IMM_ATTR_SAINT64T);
  ASSERT_EQ(number, 1);
  ASSERT_EQ(*(static_cast<SaInt64T*>(value)), val);
  ASSERT_EQ(modType, SA_IMM_ATTR_VALUES_REPLACE);

  updateobj.FreeAllocatedMemory();
}

TEST(ImmOiRtObjectUpdate, ReplaceAttributeValue_MultipleValue_SaDoubleT) {
  SaDoubleT val = -1000, val2 = -2000;
  const std::vector<SaDoubleT*> vval{&val, &val2};
  immoi::ImmOiRtObjectUpdate updateobj{10, objname};
  updateobj.ReplaceAttributeValue("value", vval);

  ASSERT_EQ(updateobj.attribute_mods_.size(), 1);

  // Check if the private data `updateobj.attribute_mods`
  // is formed as expected or not.
  SaImmAttrModificationT_2 temp;
  updateobj.attribute_mods_[0]->FormAttrModificationT_2(&temp);
  SaImmAttrValuesT_2* modAttr = &temp.modAttr;
  SaImmAttrModificationTypeT modType = temp.modType;

  const char* attrname = modAttr->attrName;
  SaImmValueTypeT type = modAttr->attrValueType;
  SaUint32T number = modAttr->attrValuesNumber;
  void* value1 = modAttr->attrValues[0];
  void* value2 = modAttr->attrValues[1];

  ASSERT_EQ(std::string{attrname}, "value");
  ASSERT_EQ(type, SA_IMM_ATTR_SADOUBLET);
  ASSERT_EQ(number, 2);
  ASSERT_EQ(*(static_cast<SaDoubleT*>(value1)), val);
  ASSERT_EQ(*(static_cast<SaDoubleT*>(value2)), val2);
  ASSERT_EQ(modType, SA_IMM_ATTR_VALUES_REPLACE);

  updateobj.FreeAllocatedMemory();
}

TEST(ImmOiRtObjectUpdate, ReplaceAttributeValue_MultipleValue_SingleValue) {
  SaDoubleT val = -1000;
  SaConstStringT str1 = "hello";
  SaConstStringT str2 = "world";
  const std::vector<SaConstStringT*> vstr{&str1, &str2};
  immoi::ImmOiRtObjectUpdate updateobj{10, objname};
  updateobj.ReplaceAttributeValue("value", vstr);
  updateobj.ReplaceAttributeValue("value", &val);

  ASSERT_EQ(updateobj.attribute_mods_.size(), 2);

  // Check if the private data `updateobj.attribute_mods`
  // is formed as expected or not.
  SaImmAttrModificationT_2 temp;
  updateobj.attribute_mods_[0]->FormAttrModificationT_2(&temp);
  SaImmAttrValuesT_2* modAttr = &temp.modAttr;
  SaImmAttrModificationTypeT modType = temp.modType;

  const char* attrname = modAttr->attrName;
  SaImmValueTypeT type = modAttr->attrValueType;
  SaUint32T number = modAttr->attrValuesNumber;
  void* value1 = modAttr->attrValues[0];
  void* value2 = modAttr->attrValues[1];

  ASSERT_EQ(std::string{attrname}, "value");
  ASSERT_EQ(type, SA_IMM_ATTR_SASTRINGT);
  ASSERT_EQ(number, 2);
  ASSERT_EQ(std::string{*(static_cast<SaStringT*>(value1))}, str1);
  ASSERT_EQ(std::string{*(static_cast<SaStringT*>(value2))}, str2);
  ASSERT_EQ(modType, SA_IMM_ATTR_VALUES_REPLACE);

  SaImmAttrModificationT_2 temp1;
  updateobj.attribute_mods_[1]->FormAttrModificationT_2(&temp1);
  SaImmAttrValuesT_2* modAttr1 = &temp1.modAttr;
  SaImmAttrModificationTypeT modType1 = temp1.modType;

  const char* attrname1 = modAttr1->attrName;
  SaImmValueTypeT type1 = modAttr1->attrValueType;
  SaUint32T number1 = modAttr1->attrValuesNumber;
  void* value11 = modAttr1->attrValues[0];

  ASSERT_EQ(std::string{attrname1}, "value");
  ASSERT_EQ(type1, SA_IMM_ATTR_SADOUBLET);
  ASSERT_EQ(number1, 1);
  ASSERT_EQ(*(static_cast<SaDoubleT*>(value11)), val);
  ASSERT_EQ(modType1, SA_IMM_ATTR_VALUES_REPLACE);

  updateobj.FreeAllocatedMemory();
}

TEST(ImmOiRtObjectUpdate, DeleteAttributeValue_SaNameT) {
  SaNameT val;
  SaConstStringT valref = "hello world";
  osaf_extended_name_lend(valref, &val);
  immoi::ImmOiRtObjectUpdate updateobj{10, objname};
  updateobj.DeleteAttributeValue("value", &val);

  ASSERT_EQ(updateobj.attribute_mods_.size(), 1);

  // Check if the private data `updateobj.attribute_mods`
  // is formed as expected or not.
  SaImmAttrModificationT_2 temp;
  updateobj.attribute_mods_[0]->FormAttrModificationT_2(&temp);
  SaImmAttrValuesT_2* modAttr = &temp.modAttr;
  SaImmAttrModificationTypeT modType = temp.modType;

  const char* attrname = modAttr->attrName;
  SaImmValueTypeT type = modAttr->attrValueType;
  SaUint32T number = modAttr->attrValuesNumber;
  void* value = modAttr->attrValues[0];

  ASSERT_EQ(std::string{attrname}, "value");
  ASSERT_EQ(type, SA_IMM_ATTR_SANAMET);
  ASSERT_EQ(number, 1);

  SaNameT* sanamet = static_cast<SaNameT*>(value);
  SaConstStringT outvalue = osaf_extended_name_borrow(sanamet);

  ASSERT_EQ(std::string{outvalue}, std::string{valref});
  ASSERT_EQ(modType, SA_IMM_ATTR_VALUES_DELETE);

  updateobj.FreeAllocatedMemory();
}

TEST(ImmOiRtObjectUpdate, AddAttributeValue_SaStringT) {
  SaConstStringT val = "hello world";
  immoi::ImmOiRtObjectUpdate updateobj{10, objname};
  updateobj.AddAttributeValue("value", &val);

  ASSERT_EQ(updateobj.attribute_mods_.size(), 1);

  // Check if the private data `updateobj.attribute_mods`
  // is formed as expected or not.
  SaImmAttrModificationT_2 temp;
  updateobj.attribute_mods_[0]->FormAttrModificationT_2(&temp);
  SaImmAttrValuesT_2* modAttr = &temp.modAttr;
  SaImmAttrModificationTypeT modType = temp.modType;

  const char* attrname = modAttr->attrName;
  SaImmValueTypeT type = modAttr->attrValueType;
  SaUint32T number = modAttr->attrValuesNumber;
  void* value = modAttr->attrValues[0];

  ASSERT_EQ(std::string{attrname}, "value");
  ASSERT_EQ(type, SA_IMM_ATTR_SASTRINGT);
  ASSERT_EQ(number, 1);
  ASSERT_EQ(std::string{*(static_cast<SaStringT*>(value))}, val);
  ASSERT_EQ(modType, SA_IMM_ATTR_VALUES_ADD);

  updateobj.FreeAllocatedMemory();
}
