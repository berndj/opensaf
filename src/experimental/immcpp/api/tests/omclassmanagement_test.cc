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
#include "experimental/immcpp/api/include/om_class_description_get.h"
#include "ais/include/saAis.h"
#include "ais/include/saImm.h"
#include "base/osaf_extended_name.h"
#include "experimental/immcpp/api/tests/common.h"

//>
// Unit Test for interfaces of ImmOmClassDescriptionGet class.
//
// Looking at the `saImmOmClassDescriptionGet_2()` interface:
// saImmOmClassDescriptionGet_2(
//     SaImmHandleT immHandle,
//     const SaImmClassNameT className,
//     SaImmClassCategoryT *classCategory,       // output
//     SaImmAttrDefinitionT_2 ***attrDefinitions // output
// );
//
// We suppose `saImmOmClassDescriptionGet_2` returns SA_AIS_OK, means
// `GetClassDescription` returns true, and we have outputs `classCategory`
//  and `attrDefinitions` in place.
//
// The test focuses on verifying if `GetDefaultValue()`/`GetClassCategory`
// interfaces work as expected or not.
//
// So, the Unit Test includes 02 main steps:
// 1) Prepare outputs data which have the same format as returned by IMM
// 2) Pass the input to `Get` methods, and check returned value as expected.
//
// The test will go through all AIS data types.
//<

//>
// Prepare data to form the output `SaImmAttrDefinitionT_2*** attrDefinitions`
//<

static SaInt32T  v_SaInt32T   = 11;
static SaUint32T v_SaUint32T  = 22;
static SaInt64T  v_SaInt64T   = 333;
static SaUint64T v_SaUint64T  = 44;
static SaFloatT  v_SaFloatT   = 55.555;
static SaDoubleT v_SaDoubleT  = 666.6666;
static char      str[]        = "777777";
static SaStringT v_SaStringT  = str;

#ifndef SA_EXTENDED_NAME_SOURCE
static SaNameT v_SaNameT     = {11, "1234567890"};
#else
static char longDn[kOsafMaxDnLength] = {0};
// @v_SaNameT will be initialized in @common_constructor
static SaNameT v_SaNameT;
#endif

//>
// Simulated IMM Model attributes
// NOTE: Attribute name MUST follow the rule `cfg_attr_TYPE`
//<

// {SaImmAttrNameT, SaImmValueTypeT, SaImmAttrFlagsT, SaImmValueTypeT}
static SaImmAttrDefinitionT_2 cfg_attr_SaInt32T = {
  const_cast<char *>(ATTR_NAME(SaInt32T)),
  SA_IMM_ATTR_SAINT32T,
  SA_IMM_ATTR_CONFIG,
  &ATTR_VALUE(SaInt32T)
};

static SaImmAttrDefinitionT_2 cfg_attr_SaUint32T = {
  const_cast<char *>(ATTR_NAME(SaUint32T)),
  SA_IMM_ATTR_SAUINT32T,
  SA_IMM_ATTR_CONFIG,
  &ATTR_VALUE(SaUint32T)
};

static SaImmAttrDefinitionT_2 cfg_attr_SaInt64T = {
  const_cast<char *>(ATTR_NAME(SaInt64T)),
  SA_IMM_ATTR_SAINT64T,
  SA_IMM_ATTR_CONFIG,
  &ATTR_VALUE(SaInt64T)
};

static SaImmAttrDefinitionT_2 cfg_attr_SaUint64T = {
  const_cast<char *>(ATTR_NAME(SaUint64T)),
  SA_IMM_ATTR_SAUINT64T,
  SA_IMM_ATTR_CONFIG,
  &ATTR_VALUE(SaUint64T)
};

static SaImmAttrDefinitionT_2 cfg_attr_SaFloatT = {
  const_cast<char *>(ATTR_NAME(SaFloatT)),
  SA_IMM_ATTR_SAFLOATT,
  SA_IMM_ATTR_CONFIG,
  &ATTR_VALUE(SaFloatT)
};

static SaImmAttrDefinitionT_2 cfg_attr_SaDoubleT = {
  const_cast<char *>(ATTR_NAME(SaDoubleT)),
  SA_IMM_ATTR_SADOUBLET,
  SA_IMM_ATTR_CONFIG,
  &ATTR_VALUE(SaDoubleT)
};

static SaImmAttrDefinitionT_2 cfg_attr_SaNameT = {
  const_cast<char *>(ATTR_NAME(SaNameT)),
  SA_IMM_ATTR_SANAMET,
  SA_IMM_ATTR_CONFIG,
  &ATTR_VALUE(SaNameT)
};

static SaImmAttrDefinitionT_2 cfg_attr_SaStringT = {
  const_cast<char *>(ATTR_NAME(SaStringT)),
  SA_IMM_ATTR_SASTRINGT,
  SA_IMM_ATTR_CONFIG,
  &ATTR_VALUE(SaStringT)
};

static SaImmAttrDefinitionT_2* cfg_attrs_t[] = {
  &ATTR_DEFINE_VALUE(SaInt32T),
  &ATTR_DEFINE_VALUE(SaUint32T),
  &ATTR_DEFINE_VALUE(SaInt64T),
  &ATTR_DEFINE_VALUE(SaUint64T),
  &ATTR_DEFINE_VALUE(SaFloatT),
  &ATTR_DEFINE_VALUE(SaDoubleT),
  &ATTR_DEFINE_VALUE(SaNameT),
  &ATTR_DEFINE_VALUE(SaStringT),
  nullptr
};

// NOTE: No need to test Rt attribute objects as in term of GetResult usage,
// it is based on Result data, and it has no different
// b/w rt/cfg attribute.

static const SaImmHandleT h = 1;
static const char classname[] = "CfgClass";
static immom::ImmOmClassDescriptionGet classobj(h, classname);

// Setup data for ImmOmClassDescriptionGet class which has the same format
// as one returned by C IMM API.
static void SetupData() {
  classobj.class_category_       = SA_IMM_CLASS_CONFIG;
  classobj.attribute_definition_ = cfg_attrs_t;
}

//==============================================================================
// GetValue() interface
//==============================================================================
TEST(ImmOmClassDescriptionGet_GetValue, SingleSaInt32TValue_CfgObj) {
  // Setup once
  SetupData();
  auto* out = classobj.GetDefaultValue<SaInt32T>(ATTR_NAME(SaInt32T));
  ASSERT_EQ(*out, ATTR_VALUE(SaInt32T));
}

TEST(ImmOmClassDescriptionGet_GetDefaultValue, SingleSaUint32TValue_CfgObj) {
  auto* out = classobj.GetDefaultValue<SaUint32T>(ATTR_NAME(SaUint32T));
  ASSERT_EQ(*out, ATTR_VALUE(SaUint32T));
}

TEST(ImmOmClassDescriptionGet_GetDefaultValue, SingleSaInt64TValue_CfgObj) {
  auto* out = classobj.GetDefaultValue<SaInt64T>(ATTR_NAME(SaInt64T));
  ASSERT_EQ(*out, ATTR_VALUE(SaInt64T));
}

TEST(ImmOmClassDescriptionGet_GetDefaultValue, SingleSaUint64TValue_CfgObj) {
  auto* out = classobj.GetDefaultValue<SaUint64T>(ATTR_NAME(SaUint64T));
  ASSERT_EQ(*out, ATTR_VALUE(SaUint64T));
}

TEST(ImmOmClassDescriptionGet_GetDefaultValue, SingleSaFloatTValue_CfgObj) {
  auto* out = classobj.GetDefaultValue<SaFloatT>(ATTR_NAME(SaFloatT));
  ASSERT_EQ(*out, ATTR_VALUE(SaFloatT));
}

TEST(ImmOmClassDescriptionGet_GetDefaultValue, SingleSaDoubleTValue_CfgObj) {
  auto* out = classobj.GetDefaultValue<SaDoubleT>(ATTR_NAME(SaDoubleT));
  ASSERT_EQ(*out, ATTR_VALUE(SaDoubleT));
}

TEST(ImmOmClassDescriptionGet_GetDefaultValue, SingleSaNameTValue_CfgObj) {
  SaNameT* out = classobj.GetDefaultValue<SaNameT>(ATTR_NAME(SaNameT));
  std::string outstr{};

  setenv("SA_ENABLE_EXTENDED_NAMES", "1", 1);

  if (out != nullptr) {
    outstr = osaf_extended_name_borrow(out);
  }

  SaNameT evalue = ATTR_VALUE(SaNameT);
  std::string expstr = osaf_extended_name_borrow(&evalue);

  ASSERT_EQ(outstr, expstr);
}

TEST(ImmOmClassDescriptionGet_GetDefaultValue, SingleSaStringTValue_CfgObj) {
  auto* out = classobj.GetDefaultValue<SaStringT>(ATTR_NAME(SaStringT));
  ASSERT_EQ(*out, ATTR_VALUE(SaStringT));
}

__attribute__((constructor)) static void omclassmanangement_constructor(void) {
#ifdef SA_EXTENDED_NAME_SOURCE
  memset(longDn, 'A', kOsafMaxDnLength);
  osaf_extended_name_lend(longDn, &v_SaNameT);
#endif
}
