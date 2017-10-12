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

#include <string>
#include "gtest/gtest.h"
#include "experimental/immcpp/api/common/imm_attribute.h"

//>
// To verify public methods of `AttributeProperty`, AttributeModification
// and AttributeDefinition classes work as expected.
//
// Here are major steps:
// 1) Construct the object, or call a specific method.
// 2) Check if the object's attributes have expected values or not
//<

//
// AttributeProperty
//
TEST(AttributeProperty, AttributeProperty) {
  const std::string name{"name"};
  AttributeProperty attribute{name};

  EXPECT_EQ(attribute.attribute_name_, name);
  EXPECT_EQ(attribute.attribute_values_, nullptr);
  EXPECT_EQ(attribute.num_of_values_, 0);
}

// Verify AttributeProperty::set_value on all AIS data types
// including extended std::string
TEST(AttributeProperty, set_value) {
  const std::string name{"name"};
  AttributeProperty attribute{name};
  SaUint32T value = 10;
  attribute.set_value(&value);

  EXPECT_EQ(attribute.attribute_name_, name);
  EXPECT_EQ(*(static_cast<SaUint32T*>(attribute.attribute_values_[0])), value);
  EXPECT_EQ(attribute.num_of_values_, 1);
  EXPECT_EQ(attribute.attribute_type_, SA_IMM_ATTR_SAUINT32T);
}

TEST(AttributeProperty, set_value_int) {
  const std::string name{"name"};
  AttributeProperty attribute{name};
  int value = 10;
  attribute.set_value(&value);

  EXPECT_EQ(attribute.attribute_name_, name);
  EXPECT_EQ(*(static_cast<int*>(attribute.attribute_values_[0])), value);
  EXPECT_EQ(attribute.num_of_values_, 1);
  EXPECT_EQ(attribute.attribute_type_, SA_IMM_ATTR_SAINT32T);
}

TEST(AttributeProperty, set_value_long_long) {
  const std::string name{"name"};
  AttributeProperty attribute{name};
  long long value = -9873459834;
  attribute.set_value(&value);

  EXPECT_EQ(attribute.attribute_name_, name);
  EXPECT_EQ(*(static_cast<long long*>(attribute.attribute_values_[0])), value);
  EXPECT_EQ(attribute.num_of_values_, 1);
  EXPECT_EQ(attribute.attribute_type_, SA_IMM_ATTR_SAINT64T);
}

TEST(AttributeProperty, set_value_float) {
  const std::string name{"name"};
  AttributeProperty attribute{name};
  float value = 1.98334f;
  attribute.set_value(&value);

  EXPECT_EQ(attribute.attribute_name_, name);
  EXPECT_EQ(*(static_cast<float*>(attribute.attribute_values_[0])), value);
  EXPECT_EQ(attribute.num_of_values_, 1);
  EXPECT_EQ(attribute.attribute_type_, SA_IMM_ATTR_SAFLOATT);
}

TEST(AttributeProperty, set_value_double) {
  const std::string name{"name"};
  AttributeProperty attribute{name};
  double value = 1.0/81;
  attribute.set_value(&value);

  EXPECT_EQ(attribute.attribute_name_, name);
  EXPECT_EQ(*(static_cast<double*>(attribute.attribute_values_[0])), value);
  EXPECT_EQ(attribute.num_of_values_, 1);
  EXPECT_EQ(attribute.attribute_type_, SA_IMM_ATTR_SADOUBLET);
}

TEST(AttributeProperty, set_value_char) {
  const std::string name{"name"};
  AttributeProperty attribute{name};
  char* value(new char[13]());
  memcpy(value, "hello world", 12);
  attribute.set_value(&value);

  EXPECT_EQ(attribute.attribute_name_, name);
  EXPECT_EQ(std::string{*(static_cast<char**>(attribute.attribute_values_[0]))},
            std::string{value});
  EXPECT_EQ(attribute.num_of_values_, 1);
  EXPECT_EQ(attribute.attribute_type_, SA_IMM_ATTR_SASTRINGT);
  delete[] value;
}

TEST(AttributeProperty, set_value_cpp_string) {
  const std::string name{"name"};
  AttributeProperty attribute{name};
  std::string value{"hello world"};
  attribute.set_value(&value);

  EXPECT_EQ(attribute.attribute_name_, name);
  EXPECT_EQ(std::string{*(static_cast<char**>(attribute.attribute_values_[0]))},
            value);
  EXPECT_EQ(attribute.num_of_values_, 1);
  EXPECT_EQ(attribute.attribute_type_, SA_IMM_ATTR_SASTRINGT);
}

TEST(AttributeProperty, set_value_multiple) {
  const std::string name{"name"};
  AttributeProperty attribute{name};
  SaConstStringT string1 = "hello", string2 = "world";
  std::vector<SaConstStringT*> value{&string1, &string2};
  attribute.set_value(value);

  void* value1 = attribute.attribute_values_[0];
  void* value2 = attribute.attribute_values_[1];

  EXPECT_EQ(attribute.attribute_name_, name);
  EXPECT_EQ(*(static_cast<SaConstStringT*>(value1)), string1);
  EXPECT_EQ(*(static_cast<SaConstStringT*>(value2)), string2);
  EXPECT_EQ(attribute.num_of_values_, 2);
  EXPECT_EQ(attribute.attribute_type_, SA_IMM_ATTR_SASTRINGT);
}

TEST(AttributeProperty, FormAttrValuesT_2) {
  const std::string name{"name"};
  AttributeProperty attribute{name};
  SaUint32T value = 10;
  attribute.set_value(&value);
  SaImmAttrValuesT_2 attr_value;
  attribute.FormAttrValuesT_2(&attr_value);

  EXPECT_EQ(std::string{attr_value.attrName}, name);
  EXPECT_EQ(*(static_cast<SaUint32T*>(attr_value.attrValues[0])), value);
  EXPECT_EQ(attr_value.attrValuesNumber, 1);
  EXPECT_EQ(attr_value.attrValueType, SA_IMM_ATTR_SAUINT32T);
}

TEST(AttributeProperty, FormAdminOperationParamsT_2) {
  const std::string name{"name"};
  AttributeProperty attribute{name};
  SaUint32T value = 10;
  attribute.set_value(&value);
  SaImmAdminOperationParamsT_2 admin_op;
  attribute.FormAdminOperationParamsT_2(&admin_op);

  EXPECT_EQ(std::string{admin_op.paramName}, name);
  EXPECT_EQ(*(static_cast<SaUint32T*>(admin_op.paramBuffer)), value);
  EXPECT_EQ(admin_op.paramType, SA_IMM_ATTR_SAUINT32T);
}

//
// AttributeDefinition
//
TEST(AttributeDefinition, AttributeDefinition) {
  const std::string name{"name"};
  SaImmAttrFlagsT flags = SA_IMM_ATTR_RDN | SA_IMM_ATTR_RUNTIME;
  AttributeDefinition attribute{name, flags};

  EXPECT_EQ(attribute.attribute_name_, name);
  EXPECT_EQ(attribute.attribute_values_, nullptr);
  EXPECT_EQ(attribute.num_of_values_, 0);
  EXPECT_EQ(attribute.attribute_flags_, flags);
}

TEST(AttributeDefinition, FormAttrDefinitionT_2) {
  const std::string name{"name"};
  SaImmAttrFlagsT flags = SA_IMM_ATTR_RDN | SA_IMM_ATTR_RUNTIME;
  AttributeDefinition attribute{name, flags};
  SaUint32T value = 10;
  attribute.set_value(&value);
  SaImmAttrDefinitionT_2 definition;
  attribute.FormAttrDefinitionT_2(&definition);

  EXPECT_EQ(std::string{definition.attrName}, name);
  EXPECT_EQ(*(static_cast<SaUint32T*>(definition.attrDefaultValue)), value);
  EXPECT_EQ(definition.attrValueType, SA_IMM_ATTR_SAUINT32T);
  EXPECT_EQ(definition.attrFlags, flags);
}

//
// AttributeModification
//
TEST(AttributeModification, AttributeModification) {
  const std::string name{"name"};
  SaImmAttrModificationTypeT type = SA_IMM_ATTR_VALUES_DELETE;
  AttributeModification attribute{name, type};

  EXPECT_EQ(attribute.attribute_name_, name);
  EXPECT_EQ(attribute.attribute_values_, nullptr);
  EXPECT_EQ(attribute.num_of_values_, 0);
  EXPECT_EQ(attribute.modification_type_, type);
}

TEST(AttributeDefinition, FormAttrModificationT_2) {
  const std::string name{"name"};
  SaImmAttrModificationTypeT type = SA_IMM_ATTR_VALUES_DELETE;
  AttributeModification attribute{name, type};
  SaUint32T value = 10;
  attribute.set_value(&value);
  SaImmAttrModificationT_2 modify;
  attribute.FormAttrModificationT_2(&modify);
  SaImmAttrValuesT_2* attr_value = &modify.modAttr;

  EXPECT_EQ(std::string{attr_value->attrName}, name);
  EXPECT_EQ(*(static_cast<SaUint32T*>(attr_value->attrValues[0])), value);
  EXPECT_EQ(attr_value->attrValueType, SA_IMM_ATTR_SAUINT32T);
  EXPECT_EQ(attr_value->attrValuesNumber, 1);
  EXPECT_EQ(modify.modType, type);
}
