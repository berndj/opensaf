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
#include "experimental/immcpp/api/include/om_class_create.h"
#include "experimental/immcpp/api/common/imm_attribute.h"
#include "base/osaf_extended_name.h"

//>
// Unit Test for interfaces of ImmOmRtClassCreate and ImmOmCfgClassCreate class.
// Steps:
// 1) Create object
// 2) Invoke one of its interfaces
// 3) Check if the private data is formed as expected or not
//<

TEST(ImmOmRtClassCreate, ImmOmRtClassCreate) {
  const std::string classname{"classname"};
  immom::ImmOmRtClassCreate createobj{10, classname};
  ASSERT_EQ(createobj.om_handle_, 10);
  ASSERT_EQ(createobj.class_name_, classname);
  ASSERT_EQ(createobj.category_, SA_IMM_CLASS_RUNTIME);
  ASSERT_EQ(createobj.list_of_attribute_definitions_.empty(), true);
}

TEST(ImmOmRtClassCreate, SetAttributeProperties_Rdn) {
  const std::string classname{"classname"};
  immom::ImmOmRtClassCreate createobj{10, classname};

  SaImmAttrFlagsT flags = SA_IMM_ATTR_RDN | SA_IMM_ATTR_CACHED;
  createobj.SetAttributeProperties<SaStringT>("rdn", flags, nullptr);

  ASSERT_EQ(createobj.list_of_attribute_definitions_.size(), 1);

  SaImmAttrDefinitionT_2 attr_def;
  createobj.list_of_attribute_definitions_[0]->FormAttrDefinitionT_2(&attr_def);
  SaImmAttrNameT attrName          = attr_def.attrName;
  SaImmValueTypeT attrValueType    = attr_def.attrValueType;
  SaImmAttrFlagsT attrFlags        = attr_def.attrFlags;
  SaImmAttrValueT attrDefaultValue = attr_def.attrDefaultValue;

  ASSERT_EQ(std::string{attrName}, "rdn");
  ASSERT_EQ(attrValueType, SA_IMM_ATTR_SASTRINGT);
  ASSERT_EQ(attrFlags, flags | SA_IMM_ATTR_RUNTIME);
  ASSERT_EQ(attrDefaultValue, nullptr);

  createobj.FreeAllocatedMemory();
}

TEST(ImmOmRtClassCreate, SetAttributeProperties_DefaultFlags) {
  const std::string classname{"classname"};
  immom::ImmOmRtClassCreate createobj{10, classname};
  SaUint64T val = 10000;

  createobj.SetAttributeProperties("attr1", &val);
  ASSERT_EQ(createobj.list_of_attribute_definitions_.size(), 1);

  SaImmAttrDefinitionT_2 attr_def;
  createobj.list_of_attribute_definitions_[0]->FormAttrDefinitionT_2(&attr_def);
  SaImmAttrNameT attrName          = attr_def.attrName;
  SaImmValueTypeT attrValueType    = attr_def.attrValueType;
  SaImmAttrFlagsT attrFlags        = attr_def.attrFlags;
  SaImmAttrValueT attrDefaultValue = attr_def.attrDefaultValue;

  ASSERT_EQ(std::string{attrName}, "attr1");
  ASSERT_EQ(attrValueType, SA_IMM_ATTR_SAUINT64T);
  ASSERT_EQ(attrFlags, SA_IMM_ATTR_CACHED | SA_IMM_ATTR_RUNTIME);
  ASSERT_EQ(*(static_cast<SaUint64T*>(attrDefaultValue)), val);

  createobj.FreeAllocatedMemory();
}

TEST(ImmOmRtClassCreate, SetAttributeProperties_Multiple_Flags_Attributes) {
  const std::string classname{"classname"};
  immom::ImmOmRtClassCreate createobj{10, classname};
  SaConstStringT str = "helloworld";
  SaUint64T val = 10000;
  SaImmAttrFlagsT flags = SA_IMM_ATTR_RUNTIME | SA_IMM_ATTR_CACHED
                          | SA_IMM_ATTR_MULTI_VALUE;

  createobj.SetAttributeProperties("attr", flags, &val);
  createobj.SetAttributeProperties("attr1", &str);
  ASSERT_EQ(createobj.list_of_attribute_definitions_.size(), 2);

  SaImmAttrDefinitionT_2 attr_def;
  createobj.list_of_attribute_definitions_[0]->FormAttrDefinitionT_2(&attr_def);
  SaImmAttrNameT attrName          = attr_def.attrName;
  SaImmValueTypeT attrValueType    = attr_def.attrValueType;
  SaImmAttrFlagsT attrFlags        = attr_def.attrFlags;
  SaImmAttrValueT attrDefaultValue = attr_def.attrDefaultValue;

  ASSERT_EQ(std::string{attrName}, "attr");
  ASSERT_EQ(attrValueType, SA_IMM_ATTR_SAUINT64T);
  ASSERT_EQ(attrFlags, flags);
  ASSERT_EQ(*(static_cast<SaUint64T*>(attrDefaultValue)), val);

  SaImmAttrDefinitionT_2 attr_def1;
  createobj.list_of_attribute_definitions_[1]->FormAttrDefinitionT_2(
      &attr_def1);
  SaImmAttrNameT attrName1          = attr_def1.attrName;
  SaImmValueTypeT attrValueType1    = attr_def1.attrValueType;
  SaImmAttrFlagsT attrFlags1        = attr_def1.attrFlags;
  SaImmAttrValueT attrDefaultValue1 = attr_def1.attrDefaultValue;

  ASSERT_EQ(std::string{attrName1}, "attr1");
  ASSERT_EQ(attrValueType1, SA_IMM_ATTR_SASTRINGT);
  // Default flags
  ASSERT_EQ(attrFlags1, SA_IMM_ATTR_CACHED | SA_IMM_ATTR_RUNTIME);
  ASSERT_EQ(*(static_cast<SaStringT*>(attrDefaultValue1)), std::string{str});

  createobj.FreeAllocatedMemory();
}

// Verify ImmOmCfgClassCreate
TEST(ImmOmCfgClassCreate, ImmOmCfgClassCreate) {
  const std::string classname{"classname"};
  immom::ImmOmCfgClassCreate createobj{10, classname};
  ASSERT_EQ(createobj.om_handle_, 10);
  ASSERT_EQ(createobj.class_name_, classname);
  ASSERT_EQ(createobj.category_, SA_IMM_CLASS_CONFIG);
  ASSERT_EQ(createobj.list_of_attribute_definitions_.empty(), true);
}

TEST(ImmOmCfgClassCreate, SetAttributeProperties_Rdn) {
  const std::string classname{"classname"};
  immom::ImmOmCfgClassCreate createobj{10, classname};
  SaConstStringT rdn = "abc=d";

  SaImmAttrFlagsT flags = SA_IMM_ATTR_RDN | SA_IMM_ATTR_INITIALIZED;
  createobj.SetAttributeProperties("rdn", flags, &rdn);

  ASSERT_EQ(createobj.list_of_attribute_definitions_.size(), 1);

  SaImmAttrDefinitionT_2 attr_def;
  createobj.list_of_attribute_definitions_[0]->FormAttrDefinitionT_2(&attr_def);
  SaImmAttrNameT attrName          = attr_def.attrName;
  SaImmValueTypeT attrValueType    = attr_def.attrValueType;
  SaImmAttrFlagsT attrFlags        = attr_def.attrFlags;
  SaImmAttrValueT attrDefaultValue = attr_def.attrDefaultValue;

  ASSERT_EQ(std::string{attrName}, "rdn");
  ASSERT_EQ(attrValueType, SA_IMM_ATTR_SASTRINGT);
  ASSERT_EQ(attrFlags, flags | SA_IMM_ATTR_CONFIG);
  ASSERT_EQ(*(static_cast<SaStringT*>(attrDefaultValue)), std::string{rdn});

  createobj.FreeAllocatedMemory();
}

TEST(ImmOmCfgClassCreate, SetAttributeProperties_DefaultFlags) {
  const std::string classname{"classname"};
  immom::ImmOmCfgClassCreate createobj{10, classname};
  SaUint64T val = 10000;

  createobj.SetAttributeProperties("attr1", &val);
  ASSERT_EQ(createobj.list_of_attribute_definitions_.size(), 1);

  SaImmAttrDefinitionT_2 attr_def;
  createobj.list_of_attribute_definitions_[0]->FormAttrDefinitionT_2(&attr_def);
  SaImmAttrNameT attrName          = attr_def.attrName;
  SaImmValueTypeT attrValueType    = attr_def.attrValueType;
  SaImmAttrFlagsT attrFlags        = attr_def.attrFlags;
  SaImmAttrValueT attrDefaultValue = attr_def.attrDefaultValue;

  ASSERT_EQ(std::string{attrName}, "attr1");
  ASSERT_EQ(attrValueType, SA_IMM_ATTR_SAUINT64T);
  ASSERT_EQ(attrFlags, SA_IMM_ATTR_CONFIG | SA_IMM_ATTR_WRITABLE);
  ASSERT_EQ(*(static_cast<SaUint64T*>(attrDefaultValue)), val);

  createobj.FreeAllocatedMemory();
}

TEST(ImmOmCfgClassCreate, SetAttributeProperties_Multiple_Flags_Attributes) {
  const std::string classname{"classname"};
  immom::ImmOmCfgClassCreate createobj{10, classname};
  SaConstStringT str = "helloworld";
  SaUint64T val = 10000;
  SaImmAttrFlagsT flags = SA_IMM_ATTR_CONFIG | SA_IMM_ATTR_WRITABLE
                          | SA_IMM_ATTR_MULTI_VALUE;

  createobj.SetAttributeProperties("attr", flags, &val);
  createobj.SetAttributeProperties("attr1", &str);
  ASSERT_EQ(createobj.list_of_attribute_definitions_.size(), 2);

  SaImmAttrDefinitionT_2 attr_def;
  createobj.list_of_attribute_definitions_[0]->FormAttrDefinitionT_2(&attr_def);
  SaImmAttrNameT attrName          = attr_def.attrName;
  SaImmValueTypeT attrValueType    = attr_def.attrValueType;
  SaImmAttrFlagsT attrFlags        = attr_def.attrFlags;
  SaImmAttrValueT attrDefaultValue = attr_def.attrDefaultValue;

  ASSERT_EQ(std::string{attrName}, "attr");
  ASSERT_EQ(attrValueType, SA_IMM_ATTR_SAUINT64T);
  ASSERT_EQ(attrFlags, flags);
  ASSERT_EQ(*(static_cast<SaUint64T*>(attrDefaultValue)), val);

  SaImmAttrDefinitionT_2 attr_def1;
  createobj.list_of_attribute_definitions_[1]->FormAttrDefinitionT_2(
      &attr_def1);
  SaImmAttrNameT attrName1          = attr_def1.attrName;
  SaImmValueTypeT attrValueType1    = attr_def1.attrValueType;
  SaImmAttrFlagsT attrFlags1        = attr_def1.attrFlags;
  SaImmAttrValueT attrDefaultValue1 = attr_def1.attrDefaultValue;

  ASSERT_EQ(std::string{attrName1}, "attr1");
  ASSERT_EQ(attrValueType1, SA_IMM_ATTR_SASTRINGT);
  // Default flags
  ASSERT_EQ(attrFlags1, SA_IMM_ATTR_CONFIG | SA_IMM_ATTR_WRITABLE);
  ASSERT_EQ(*(static_cast<SaStringT*>(attrDefaultValue1)), std::string{str});

  createobj.FreeAllocatedMemory();
}
