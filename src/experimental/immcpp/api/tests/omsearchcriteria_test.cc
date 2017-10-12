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
#include "experimental/immcpp/api/include/om_search_handle.h"
#include "base/osaf_extended_name.h"

//>
// To verify interfaces of ImmOmSearchCriteria class.
// Steps:
// 1) Create ImmOmSearchCriteria object
// 2) Invoke one of its interfaces
// 3) Check if the private data is formed as expected or not
//<

TEST(ImmOmSearchCriteria, ImmOmSearchCriteria) {
  immom::ImmOmSearchCriteria setting{};
  ASSERT_EQ(setting.search_root_.empty(), true);
  ASSERT_EQ(setting.option_, SA_IMM_SEARCH_GET_NO_ATTR);
  ASSERT_EQ(setting.scope_, SA_IMM_SUBTREE);
}

TEST(ImmOmSearchCriteria, SetSearchRoot) {
  const char searchroot[] = "search=root";
  immom::ImmOmSearchCriteria setting{};
  setting.SetSearchRoot(searchroot);
  ASSERT_EQ(setting.search_root_, std::string{searchroot});
}

TEST(ImmOmSearchCriteria, UseOneScope) {
  immom::ImmOmSearchCriteria setting{};
  setting.UseOneScope();
  ASSERT_EQ(setting.scope_, SA_IMM_ONE);
}

TEST(ImmOmSearchCriteria, UseSubLevelScope) {
  immom::ImmOmSearchCriteria setting{};
  setting.UseSubLevelScope();
  ASSERT_EQ(setting.scope_, SA_IMM_SUBLEVEL);
}

TEST(ImmOmSearchCriteria, UseSubTreeScope) {
  immom::ImmOmSearchCriteria setting{};
  setting.UseSubTreeScope();
  ASSERT_EQ(setting.scope_, SA_IMM_SUBTREE);
}

TEST(ImmOmSearchCriteria, FetchNoAttribute) {
  immom::ImmOmSearchCriteria setting{};
  setting.FetchNoAttribute();
  ASSERT_EQ(setting.option_, SA_IMM_SEARCH_GET_NO_ATTR);
  ASSERT_EQ(setting.attributes_.empty(), true);
}

TEST(ImmOmSearchCriteria, FetchAttributes_AllAttributes) {
  immom::ImmOmSearchCriteria setting{};
  setting.FetchAttributes();
  ASSERT_EQ(setting.option_, SA_IMM_SEARCH_GET_ALL_ATTR);
  ASSERT_EQ(setting.attributes_.empty(), true);
}

TEST(ImmOmSearchCriteria, FetchAttributes_SomeAttribute) {
  const std::vector<std::string> attrs{"one", "two", "three"};
  immom::ImmOmSearchCriteria setting{};
  setting.FetchAttributes(attrs);
  ASSERT_EQ(setting.option_, SA_IMM_SEARCH_GET_SOME_ATTR);
  ASSERT_EQ(setting.attributes_, attrs);
}

TEST(ImmOmSearchCriteria, FetchAttributes_OneAttribute) {
  const std::string attr{"one"};
  immom::ImmOmSearchCriteria setting{};
  setting.FetchAttributes(attr);
  ASSERT_EQ(setting.option_, SA_IMM_SEARCH_GET_SOME_ATTR);
  ASSERT_EQ(setting.attributes_, std::vector<std::string>{attr});
}

TEST(ImmOmSearchCriteria, SetSearchParam_NULLvalue) {
  immom::ImmOmSearchCriteria setting{};
  setting.SetSearchParam<SaStringT>("name", nullptr);

  const char* attrname = setting.search_param_.searchOneAttr.attrName;
  void* value = setting.search_param_.searchOneAttr.attrValue;
  SaImmValueTypeT type = setting.search_param_.searchOneAttr.attrValueType;

  ASSERT_EQ(setting.option_ & SA_IMM_SEARCH_ONE_ATTR, SA_IMM_SEARCH_ONE_ATTR);
  ASSERT_EQ(std::string{attrname}, "name");
  ASSERT_EQ(value, nullptr);
  ASSERT_EQ(type, SA_IMM_ATTR_SASTRINGT);

  setting.ResetSearchParam();
}

TEST(ImmOmSearchCriteria, SetSearchParam_Value) {
  SaUint32T u32val = 100;
  immom::ImmOmSearchCriteria setting{};
  setting.SetSearchParam("name", &u32val);

  const char* attrname = setting.search_param_.searchOneAttr.attrName;
  void* value = setting.search_param_.searchOneAttr.attrValue;
  SaImmValueTypeT type = setting.search_param_.searchOneAttr.attrValueType;

  ASSERT_EQ(setting.option_ & SA_IMM_SEARCH_ONE_ATTR, SA_IMM_SEARCH_ONE_ATTR);
  ASSERT_EQ(std::string{attrname}, "name");
  ASSERT_EQ(*static_cast<SaUint32T*>(value), u32val);
  ASSERT_EQ(type, SA_IMM_ATTR_SAUINT32T);
  setting.ResetSearchParam();
}

TEST(ImmOmSearchCriteria, SetSearchClassName) {
  immom::ImmOmSearchCriteria setting{};
  setting.SetSearchClassName("classname");

  const char* attrname = setting.search_param_.searchOneAttr.attrName;
  void* value = setting.search_param_.searchOneAttr.attrValue;
  SaImmValueTypeT type = setting.search_param_.searchOneAttr.attrValueType;

  ASSERT_EQ(setting.option_ & SA_IMM_SEARCH_ONE_ATTR, SA_IMM_SEARCH_ONE_ATTR);
  ASSERT_EQ(std::string{attrname}, "SaImmAttrClassName");
  ASSERT_EQ(std::string{*static_cast<SaStringT*>(value)}, "classname");
  ASSERT_EQ(type, SA_IMM_ATTR_SASTRINGT);
  setting.ResetSearchParam();
}
