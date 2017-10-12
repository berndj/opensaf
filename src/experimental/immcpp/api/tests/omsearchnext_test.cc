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
#include "experimental/immcpp/api/include/om_search_next.h"
#include "experimental/immcpp/api/tests/common.h"
#include "base/osaf_extended_name.h"

//>
// Unit Test for interfaces of ImmOmSearchNext class.
//
// Looking at the `saImmOmSearchNext_2()` interface:
//   saImmOmSearchNext_2(
//      SaImmSearchHandleT searchHandle,
//      SaNameT* objectName,               // output
//      SaImmAttrValuesT_2*** attributes); // output
//
// We suppose `saImmOmSearchNext_2` returns SA_AIS_OK, means `SearchNext()`
// returns true, and we have outputs `objectName` and `attributes` in place.
//
// The test focuses on verifying if `GetSingleValue()`/`GetMultiValue()`,
// `GetObjectName()`, interfaces work as expected or not.
//
// So, the Unit Test includes 02 main steps:
// 1) Prepare outputs data which have the same format as returned by IMM
// 2) Pass the input to `Get` methods, and check returned value as expected.
//
// The test will go through all AIS data types.
//<

//>
// Prepare data to form the output `SaImmAttrValuesT_2*** attributes`
//<

//
// Step #1: `SaImmAttrValueT attrDefaultValue`
// Each variable name has common format `v_<AIS data type>`,
// this form will help us creating test cases easier, more readability.
// Suffix `v` means `variable`, and it holds <AIS data type> format.
//
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
// Additional values for multiple-value attributes
//<
static SaInt32T  v_SaInt32T_1   = 111;
static SaUint32T v_SaUint32T_1  = 222;
static SaInt64T  v_SaInt64T_1   = 3333;
static SaUint64T v_SaUint64T_1  = 444;
static SaFloatT  v_SaFloatT_1   = 555.555;
static SaDoubleT v_SaDoubleT_1  = 6666.6666;
static char      str_1[]        = "7777777";
static SaStringT v_SaStringT_1  = str_1;

#ifndef SA_EXTENDED_NAME_SOURCE
static SaNameT v_SaNameT_1     = {22, "12345678901234567890"};
#else
static char longDn_1[kOsafMaxDnLength] = {0};
// @v_SaNameT will be initialized in @common_constructor
static SaNameT v_SaNameT_1;
#endif

//
// Step #2: Form `SaImmAttrValuesT_2` for multiple-value attribute
// To have multiple-value, we have to create pointer to pointer data.
// The variable name format is `v_<AIS data type>`
// The prefix `m` stands for `multiple`.
// `m_SaInt32T` means multiple value for SaInt32T attribute type.
//
static SaInt32T* m_SaInt32T[] = {
  &ATTR_VALUE(SaInt32T),   // Help macro to get `attribute value` of `SaInt32T`
  &ATTR_VALUE1(SaInt32T),  // Refer to `common.h` for infos on the help macros.
  nullptr
};

// Form 1st element of `SaImmAttrValuesT_2` output
static SaImmAttrValuesT_2 m_cfg_attr_SaInt32T = {
  const_cast<char *>(ATTR_NAME(SaInt32T)),            // `attrName`
  SA_IMM_ATTR_SAINT32T,                               // `attrValueType`
  2,                                                  // `attrValuesNumber`
  reinterpret_cast<void**>(MULTIPLE_VALUE(SaInt32T))  // `attrValues`
};


static SaUint32T* m_SaUint32T[] = {
  &ATTR_VALUE(SaUint32T),
  &ATTR_VALUE1(SaUint32T),
  nullptr
};

// Form 2nd element of `SaImmAttrValuesT_2` output
static SaImmAttrValuesT_2 m_cfg_attr_SaUint32T = {
  const_cast<char *>(ATTR_NAME(SaUint32T)),
  SA_IMM_ATTR_SAUINT32T,
  2,
  reinterpret_cast<void**>(MULTIPLE_VALUE(SaUint32T))
};

static SaInt64T* m_SaInt64T[] = {
  &ATTR_VALUE(SaInt64T),
  &ATTR_VALUE1(SaInt64T),
  nullptr
};

static SaImmAttrValuesT_2 m_cfg_attr_SaInt64T = {
  const_cast<char *>(ATTR_NAME(SaInt64T)),
  SA_IMM_ATTR_SAINT64T,
  2,
  reinterpret_cast<void**>(MULTIPLE_VALUE(SaInt64T))
};

static SaUint64T* m_SaUint64T[] = {
  &ATTR_VALUE(SaUint64T),
  &ATTR_VALUE1(SaUint64T),
  nullptr
};

static SaImmAttrValuesT_2 m_cfg_attr_SaUint64T = {
  const_cast<char *>(ATTR_NAME(SaUint64T)),
  SA_IMM_ATTR_SAUINT64T,
  2,
  reinterpret_cast<void**>(MULTIPLE_VALUE(SaUint64T))
};

static SaFloatT* m_SaFloatT[] = {
  &ATTR_VALUE(SaFloatT),
  &ATTR_VALUE1(SaFloatT),
  nullptr
};

static SaImmAttrValuesT_2 m_cfg_attr_SaFloatT = {
  const_cast<char *>(ATTR_NAME(SaFloatT)),
  SA_IMM_ATTR_SAFLOATT,
  2,
  reinterpret_cast<void**>(MULTIPLE_VALUE(SaFloatT))
};

static SaDoubleT* m_SaDoubleT[] = {
  &ATTR_VALUE(SaDoubleT),
  &ATTR_VALUE1(SaDoubleT),
  nullptr
};

static SaImmAttrValuesT_2 m_cfg_attr_SaDoubleT = {
  const_cast<char *>(ATTR_NAME(SaDoubleT)),
  SA_IMM_ATTR_SADOUBLET,
  2,
  reinterpret_cast<void**>(MULTIPLE_VALUE(SaDoubleT))
};

static SaNameT* m_SaNameT[] = {
  &ATTR_VALUE(SaNameT),
  &ATTR_VALUE1(SaNameT),
  nullptr
};

static SaImmAttrValuesT_2 m_cfg_attr_SaNameT = {
  const_cast<char *>(ATTR_NAME(SaNameT)),
  SA_IMM_ATTR_SANAMET,
  2,
  reinterpret_cast<void**>(MULTIPLE_VALUE(SaNameT))
};

static SaStringT* m_SaStringT[] = {
  &ATTR_VALUE(SaStringT),
  &ATTR_VALUE1(SaStringT),
  nullptr
};

static SaImmAttrValuesT_2 m_cfg_attr_SaStringT = {
  const_cast<char *>(ATTR_NAME(SaStringT)),
  SA_IMM_ATTR_SASTRINGT,
  2,
  reinterpret_cast<void**>(MULTIPLE_VALUE(SaStringT))
};

//
// Step 3: Form the `SaImmAttrValuesT_2` output exactly as returned by IMM
//
static SaImmAttrValuesT_2* m_cfg_attrs_t[] = {
  &M_ATTR_DEFINE_VALUE(SaInt32T),   // Help macro to get `m_cfg_attr_SaInt32T`
  &M_ATTR_DEFINE_VALUE(SaUint32T),
  &M_ATTR_DEFINE_VALUE(SaInt64T),
  &M_ATTR_DEFINE_VALUE(SaUint64T),
  &M_ATTR_DEFINE_VALUE(SaFloatT),
  &M_ATTR_DEFINE_VALUE(SaDoubleT),
  &M_ATTR_DEFINE_VALUE(SaNameT),
  &M_ATTR_DEFINE_VALUE(SaStringT),
  nullptr
};

static const SaImmHandleT h = 1;
static const SaImmSearchHandleT sh = 2;
static const char objectname[] = "test=multipleTest";
static immom::ImmOmSearchNext searchobj(sh);

//
// Step #4: Setup all outputs exatly as ones returned by
// C IMM API `saImmOmSearchNext_2()`
//
static void SetupData() {
  searchobj.object_name_      = objectname;
  searchobj.attribute_values_ = m_cfg_attrs_t;
}

//>
// Verifying interface `GetMultiValue()` which has 03 main steps:
// 1) Invoke `GetMultiValue()` with given attribute name
// 2) Prepare expected output
// 3) Verify if output in #1 equals to expected output
//<

//==============================================================================
// GetObjectName() interface
//==============================================================================
TEST(ImmOmSearchNext_GetObjectName, GetObjectName_CfgObj) {
  // Simulate outputs which have same formats as ones returned by IMM C API.
  SetupData();

  // 1) Invoke `GetMultiValue()` with given attribute name
  auto out = searchobj.GetObjectName();
  // 2) Prepare expected output
  const std::string expected{objectname};
  // 3) Verify if output in #1 equals to expected output
  ASSERT_EQ(out, expected);
}

//==============================================================================
// GetMultiValue() interface
//==============================================================================
TEST(ImmOmSearchNext_GetMultiValue, MultipleSaInt32TValue_CfgObj) {
  // 1) Invoke `GetMultiValue()` with given attribute name
  auto out = searchobj.GetMultiValue<SaInt32T>(ATTR_NAME(SaInt32T));
  // 2) Prepare expected output
  std::vector<SaInt32T> expected = {
    ATTR_VALUE(SaInt32T),
    ATTR_VALUE1(SaInt32T)
  };
  // 3) Verify if output in #1 equals to expected output
  ASSERT_EQ(out, expected);
}

TEST(ImmOmSearchNext_GetMultiValue, MultipleSaUint32TValue_CfgObj) {
  auto out = searchobj.GetMultiValue<SaUint32T>(ATTR_NAME(SaUint32T));
  std::vector<SaUint32T> expected = {
    ATTR_VALUE(SaUint32T),
    ATTR_VALUE1(SaUint32T)
  };
  ASSERT_EQ(out, expected);
}

TEST(ImmOmSearchNext_GetMultiValue, MultipleSaInt64TValue_CfgObj) {
  auto out = searchobj.GetMultiValue<SaInt64T>(ATTR_NAME(SaInt64T));
  std::vector<SaInt64T> expected = {
    ATTR_VALUE(SaInt64T),
    ATTR_VALUE1(SaInt64T)
  };
  ASSERT_EQ(out, expected);
}

TEST(ImmOmSearchNext_GetMultiValue, MultipleSaUint64TValue_CfgObj) {
  auto out = searchobj.GetMultiValue<SaUint64T>(ATTR_NAME(SaUint64T));
  std::vector<SaUint64T> expected = {
    ATTR_VALUE(SaUint64T),
    ATTR_VALUE1(SaUint64T)
  };
  ASSERT_EQ(out, expected);
}

TEST(ImmOmSearchNext_GetMultiValue, MultipleSaFloatTValue_CfgObj) {
  auto out = searchobj.GetMultiValue<SaFloatT>(ATTR_NAME(SaFloatT));
  std::vector<SaFloatT> expected = {
    ATTR_VALUE(SaFloatT),
    ATTR_VALUE1(SaFloatT)
  };
  ASSERT_EQ(out, expected);
}

TEST(ImmOmSearchNext_GetMultiValue, MultipleSaDoubleTValue_CfgObj) {
  auto out = searchobj.GetMultiValue<SaDoubleT>(ATTR_NAME(SaDoubleT));
  std::vector<SaDoubleT> expected = {
    ATTR_VALUE(SaDoubleT),
    ATTR_VALUE1(SaDoubleT)
  };
  ASSERT_EQ(out, expected);
}

TEST(ImmOmSearchNext_GetMultiValue, MultipleSaNameTValue_CfgObj) {
  auto out = searchobj.GetMultiValue<SaNameT>(ATTR_NAME(SaNameT));
  std::string outstr{};
  std::vector<std::string> vout{};

  setenv("SA_ENABLE_EXTENDED_NAMES", "1", 1);
  for (const auto v : out) {
    outstr = osaf_extended_name_borrow(&v);
    vout.push_back(outstr);
  }

  SaNameT evalue = ATTR_VALUE(SaNameT);
  std::string expstr = osaf_extended_name_borrow(&evalue);
  SaNameT evalue1 = ATTR_VALUE1(SaNameT);
  std::string expstr1 = osaf_extended_name_borrow(&evalue1);
  std::vector<std::string> expected = {expstr, expstr1};

  ASSERT_EQ(vout, expected);
}

TEST(ImmOmSearchNext_GetMultiValue, MultipleSaStringTValue_CfgObj) {
  auto out = searchobj.GetMultiValue<SaStringT>(ATTR_NAME(SaStringT));
  std::vector<SaStringT> expected = {
    ATTR_VALUE(SaStringT),
    ATTR_VALUE1(SaStringT)
  };
  ASSERT_EQ(out, expected);
}

__attribute__((constructor)) static void omsearchnext_constructor(void) {
#ifdef SA_EXTENDED_NAME_SOURCE
  memset(longDn, 'A', kOsafMaxDnLength);
  osaf_extended_name_lend(longDn, &v_SaNameT);

  memset(longDn_1, 'B', kOsafMaxDnLength);
  osaf_extended_name_lend(longDn_1, &v_SaNameT_1);
#endif
}
