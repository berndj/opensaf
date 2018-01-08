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

#define private public
#define protected public

#include "experimental/immcpp/api/demo/omconfigurationchanges.h"
#include <unistd.h>
#include <string>
#include <iomanip>
#include <iostream>
#include <vector>
#include "experimental/immcpp/api/demo/common.h"
#include "experimental/immcpp/api/include/om_handle.h"
#include "experimental/immcpp/api/include/om_class_create.h"
#include "experimental/immcpp/api/include/om_class_delete.h"
#include "experimental/immcpp/api/include/om_admin_owner_handle.h"
#include "experimental/immcpp/api/include/om_admin_owner_set.h"
#include "experimental/immcpp/api/include/om_ccb_handle.h"
#include "experimental/immcpp/api/include/om_ccb_object_modify.h"
#include "experimental/immcpp/api/include/om_ccb_object_delete.h"
#include "experimental/immcpp/api/include/om_ccb_object_create.h"
#include "base/saf_error.h"
#include "base/osaf_extended_name.h"

static SaVersionT imm_version = { 'A', 2, 11 };
static const char classname[] = "CfgHoldingInfoCreateClass";
static SaConstStringT rdn = "rdn=holingAttributeValues";

using namespace immom;

static ImmOmCcbHandle sCcbhandleobj;
static ImmOmCcbObjectCreate sCcbcreateobj;

static void SetupCcbCreate() {
  std::string astring = "world";
  SaUint32T au32val = 12345;
  SaUint64T au64val = 987654321, au64val1 = 123456789;
  const std::vector<SaUint64T*> vu64val = {&au64val, &au64val1};

  sCcbcreateobj.SetCcbHandle(sCcbhandleobj.GetHandle()).SetClassName(classname);

  SaNameT name;
  SaNameT name2;
  SaConstStringT valref = "multi_val1";
  SaConstStringT valref2 = "multi_val2";
  osaf_extended_name_lend(valref, &name);
  osaf_extended_name_lend(valref2, &name2);
  std::vector<SaNameT*> vname {&name, &name2};

  CppSaTimeT time = 98765432;
  CppSaTimeT time1 = 12345;
  std::vector<CppSaTimeT*> vtime {&time, &time1};

  SaAnyT any;
  const char* a = "AnyValue1";
  any.bufferSize = strlen(a) + 1;
  any.bufferAddr = new SaUint8T[strlen(a) + 1];
  memcpy((char*)any.bufferAddr, a, strlen(a) + 1);

  SaAnyT any2;
  a = "AnyValue2";
  any2.bufferSize = strlen(a) + 1;
  any2.bufferAddr = new SaUint8T[strlen(a) + 1];
  memcpy((char*)any2.bufferAddr, a, strlen(a) + 1);
  std::vector<SaAnyT*> vany {&any, &any2};

  SaFloatT f1 = 45.23, f2 = 56.6789, f3 = 909882.0928;
  std::vector<SaFloatT*> vf {&f1, &f2, &f3};

  SaDoubleT d = 1.23456789, d1 = 0.000787345632;
  std::vector<SaDoubleT*> vd {&d, &d1};

  // Single values
  sCcbcreateobj.SetAttributeValue("rdn", &rdn);
  sCcbcreateobj.SetAttributeValue("u32", &au32val);
  sCcbcreateobj.SetAttributeValue("string", &astring);
  sCcbcreateobj.SetAttributeValue("u64", vu64val);

  // Multiple values
  sCcbcreateobj.SetAttributeValue("float", vf);
  sCcbcreateobj.SetAttributeValue("double", vd);

  sCcbcreateobj.SetAttributeValue("namet", vname);
  sCcbcreateobj.SetAttributeValue("anyt", vany);
  sCcbcreateobj.SetAttributeValue("timet", vtime);
}

void HoldValueCcbCreate() {
  auto start = CLOCK_START;

  immom::ImmOmHandle omhandle{imm_version};
  omhandle.InitializeHandle();

  immom::ImmOmCfgClassCreate createclass{omhandle.GetHandle(), "CfgHoldingInfoCreateClass"};
  SaUint32T uin32val = 10;
  SaConstStringT str = "hello";
  SaUint64T uin64val = 12345678;
  SaNameT name;
  SaConstStringT valref = "123_winhvu";
  osaf_extended_name_lend(valref, &name);
  CppSaTimeT time = 98765432;
  SaAnyT any;
  const char* a = "for testing saAnyT type";
  any.bufferSize = strlen(a) + 1;
  any.bufferAddr = new SaUint8T[strlen(a) + 1];
  memcpy((char*)any.bufferAddr, a, strlen(a) + 1);
  SaImmAttrFlagsT flags = SA_IMM_ATTR_RDN | SA_IMM_ATTR_INITIALIZED;

  createclass.SetAttributeProperties<SaStringT>("rdn", flags, nullptr);
  createclass.SetAttributeProperties("u32", &uin32val);
  createclass.SetAttributeProperties("string", &str);

  flags = SA_IMM_ATTR_WRITABLE | SA_IMM_ATTR_MULTI_VALUE;
  createclass.SetAttributeProperties("u64", flags, &uin64val);

  SaFloatT f = 45.23;
  SaDoubleT d = 1.23456789;

  createclass.SetAttributeProperties("float", flags, &f);
  createclass.SetAttributeProperties("double", flags, &d);
  createclass.SetAttributeProperties("namet", flags, &name);
  createclass.SetAttributeProperties("anyt", flags, &any);
  createclass.SetAttributeProperties("timet", flags, &time);
  createclass.CreateClass();

  std::cout << "Create IMM object class `CfgHoldingInfoCreateClass` successfully" << std::endl;
  std::cout << "Enter to continue ..." << std::endl; getchar();

  immom::ImmOmAdminOwnerHandle adminobj{omhandle.GetHandle(), "OmCcbCreateTest"};
  adminobj.InitializeHandle();


  sCcbhandleobj.SetAdminOwnerHandle(adminobj.GetHandle()).SetCcbFlags(0);
  sCcbhandleobj.InitializeHandle();

  SetupCcbCreate();

  if (sCcbcreateobj.AddObjectCreateToCcb() == false) {
    SaAisErrorT ais_error = sCcbcreateobj.ais_error();
    if (ais_error == SA_AIS_ERR_EXIST) return;
    std::cerr << "CCB AddObjectCreateToCcb failed: "
              << sCcbcreateobj.ais_error_string() << std::endl;
    exit(EXIT_FAILURE);
  }

  std::cout << "SHOW DATA OF CCB CREATE (before apply) " << std::endl;
  for (const auto& item : sCcbcreateobj.list_of_attribute_properties_) {
    std::cout << "name: " << item->attribute_name_ << std::endl;
    for (const auto& i : item->values_) {
      i->display();
    }
  }

  // Apply the 2nd CCB create
  if (sCcbhandleobj.ApplyCcb() == false) {
    std::cerr << "ApplyCcb failed: "
              << sCcbhandleobj.ais_error_string() << std::endl;
    exit(EXIT_FAILURE);
  }

  std::cout << "SHOW DATA OF CCB CREATE (after apply) " << std::endl;
  for (const auto& item : sCcbcreateobj.list_of_attribute_properties_) {
    std::cout << "name: " << item->attribute_name_ << std::endl;
    for (const auto& i : item->values_) {
      i->display();
    }
  }

  std::cout << "Create IMM object successfully" << std::endl;
  std::cout << "Enter to continue ..." << std::endl; getchar();

  std::cout << std::endl;
  std::cout << std::endl;
  std::cout << "CREATE OTHER OF CCB CREATE, USING PREVIOUS DATA BUT WITH OTHER DN " << std::endl;

  std::string rdn2 = "rdn=inherit_from_previous_ccb";
  sCcbcreateobj.SetAttributeValue("rdn", &rdn2);

  sCcbcreateobj.AddObjectCreateToCcb();

  std::cout << std::endl;
  std::cout << "SHOW DATA OF CCB CREATE (before apply) " << std::endl;
  for (const auto& item : sCcbcreateobj.list_of_attribute_properties_) {
    std::cout << "name: " << item->attribute_name_ << std::endl;
    for (const auto& i : item->values_) {
      i->display();
    }
  }

  if (sCcbhandleobj.ApplyCcb() == false) {
    std::cerr << "ApplyCcb failed: "
              << sCcbhandleobj.ais_error_string() << std::endl;
    exit(EXIT_FAILURE);
  }

  auto end = CLOCK_END;
  TIME_ELAPSED(start, end);
}
