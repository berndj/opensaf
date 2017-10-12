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
static const char classname[] = "CfgCcbCreateClass";
static SaConstStringT rdn = "rdn=testCcbConfigChanges";

static void TestOmCcbCreate(immom::ImmOmCcbHandle* ccbhandleobj) {
  std::string astring = "world";
  SaUint32T au32val = 12345;
  SaUint64T au64val = 987654321, au64val1 = 123456789;
  const std::vector<SaUint64T*> vu64val = {&au64val, &au64val1};

  immom::ImmOmCcbObjectCreate ccbcreateobj{ccbhandleobj->GetHandle(),
        classname};

  ccbcreateobj.SetAttributeValue("rdn", &rdn);
  ccbcreateobj.SetAttributeValue("attr2", &au32val);
  ccbcreateobj.SetAttributeValue("attr3", &astring);
  ccbcreateobj.SetAttributeValue("attr4", vu64val);
  if (ccbcreateobj.AddObjectCreateToCcb() == false) {
    SaAisErrorT ais_error = ccbcreateobj.ais_error();
    if (ais_error == SA_AIS_ERR_EXIST) return;
    std::cerr << "CCB AddObjectCreateToCcb failed: "
              << ccbcreateobj.ais_error_string() << std::endl;
    exit(EXIT_FAILURE);
  }

  //
  // 5) Apply CCB create
  //
  if (ccbhandleobj->ApplyCcb() == false) {
    std::cerr << "ApplyCcb failed: "
              << ccbhandleobj->ais_error_string() << std::endl;
    exit(EXIT_FAILURE);
  }

  std::cout << "Create IMM object successfully" << std::endl;
  std::cout << "Enter to continue ..." << std::endl; getchar();
}

static void TestOmCcbUpdateReplace(immom::ImmOmCcbHandle* ccbhandleobj) {
  SaConstStringT astring = "updated world";
  SaUint32T au32val = 323232;
  SaUint64T au64val = 64646464, au64val1 = 65656565;
  const std::vector<SaUint64T*> vu64val = {&au64val, &au64val1};

  immom::ImmOmCcbObjectModify replaceobj{ccbhandleobj->GetHandle(), rdn};

  replaceobj.ReplaceAttributeValue("attr2", &au32val);
  replaceobj.ReplaceAttributeValue("attr3", &astring);
  replaceobj.ReplaceAttributeValue("attr4", vu64val);
  if (replaceobj.AddObjectModifyToCcb() == false) {
    std::cerr << "CCB Modify(replace) failed: "
              << replaceobj.ais_error_string() << std::endl;
    exit(EXIT_FAILURE);
  }

  //
  // 5) Apply CCB create
  //
  if (ccbhandleobj->ApplyCcb() == false) {
    std::cerr << "ApplyCcb failed: "
              << ccbhandleobj->ais_error_string() << std::endl;
    exit(EXIT_FAILURE);
  }

  std::cout << "Replace values to existing attribute values successfully"
            << std::endl;
  std::cout << "Enter to continue ..." << std::endl; getchar();
}

static void TestOmCcbUpdateAdd(immom::ImmOmCcbHandle* ccbhandleobj) {
  SaUint64T au64val = 74747474;
  immom::ImmOmCcbObjectModify addobj{ccbhandleobj->GetHandle(), rdn};

  addobj.AddAttributeValue("attr4", &au64val);
  if (addobj.AddObjectModifyToCcb() == false) {
    std::cerr << "CCB Modify(add) failed: "
              << addobj.ais_error_string() << std::endl;
    exit(EXIT_FAILURE);
  }

  //
  // 5) Apply CCB create
  //
  if (ccbhandleobj->ApplyCcb() == false) {
    std::cerr << "ApplyCcb failed: "
              << ccbhandleobj->ais_error_string() << std::endl;
    exit(EXIT_FAILURE);
  }

  std::cout << "Add more attribute values successfully" << std::endl;
  std::cout << "Enter to continue ..." << std::endl; getchar();
}

static void TestOmCcbUpdateDelete(immom::ImmOmCcbHandle* ccbhandleobj) {
  SaUint64T au64val = 74747474;
  immom::ImmOmCcbObjectModify delobj{ccbhandleobj->GetHandle(), rdn};

  delobj.DeleteAttributeValue("attr4", &au64val);
  if (delobj.AddObjectModifyToCcb() == false) {
    std::cerr << "CCB Modify(Delete) failed: "
              << delobj.ais_error_string() << std::endl;
    exit(EXIT_FAILURE);
  }

  //
  // 5) Apply CCB create
  //
  if (ccbhandleobj->ApplyCcb() == false) {
    std::cerr << "ApplyCcb failed: "
              << ccbhandleobj->ais_error_string() << std::endl;
    exit(EXIT_FAILURE);
  }

  std::cout << "Add IMM object successfully" << std::endl;
  std::cout << "Enter to continue ..." << std::endl; getchar();
}

static void TestOmCcbDelete(immom::ImmOmCcbHandle* ccbhandleobj) {
  immom::ImmOmCcbObjectDelete ccbdeleteobj{ccbhandleobj->GetHandle()};
  if (ccbdeleteobj.AddObjectDeleteToCcb(rdn) == false) {
    std::cerr << "AddObjectDeleteToCcb failed: "
              << ccbdeleteobj.ais_error_string() << std::endl;
    exit(EXIT_FAILURE);
  }

  if (ccbhandleobj->ApplyCcb() == false) {
    std::cerr << "CCB Apply delete failed: "
              << ccbhandleobj->ais_error_string() << std::endl;
    exit(EXIT_FAILURE);
  }

  std::cout << "Delete attribute value successfully" << std::endl;
  std::cout << "Enter to continue ..." << std::endl; getchar();
}

static void TestOmSetAdminOwner(immom::ImmOmAdminOwnerSet* adminset) {
  if (adminset->SetAdminOwner() == false) {
    std::cerr << "Set admin owner failed: "
              << adminset->ais_error_string() << std::endl;
    exit(EXIT_FAILURE);
  }

  std::cout << " Set admin owner successfully" << std::endl;
}

void TestOmCcbCreate() {
  auto start = CLOCK_START;

  //
  // Acquire IMM OM handle
  //
  immom::ImmOmHandle omhandle{imm_version};
  if (omhandle.InitializeHandle() == false) {
    std::cerr << "OM handle: InitHandle() failed: "
              << omhandle.ais_error_string() << std::endl;
    exit(EXIT_FAILURE);
  }

  //
  // Create object class `CfgCcbCreateClass` with 03 attributes
  // including RDN.
  //
  immom::ImmOmCfgClassCreate createclass{omhandle.GetHandle(),
        "CfgCcbCreateClass"};
  SaUint32T uin32val = 10;
  SaConstStringT str = "hello";
  SaUint64T uin64val = 12345678;

  SaImmAttrFlagsT flags = SA_IMM_ATTR_RDN | SA_IMM_ATTR_INITIALIZED;
  createclass.SetAttributeProperties<SaStringT>("rdn", flags, nullptr);

  createclass.SetAttributeProperties("attr2", &uin32val);
  createclass.SetAttributeProperties("attr3", &str);

  flags = SA_IMM_ATTR_WRITABLE | SA_IMM_ATTR_MULTI_VALUE;
  createclass.SetAttributeProperties("attr4", flags, &uin64val);

  if (createclass.CreateClass() == false) {
    SaAisErrorT ais_error = createclass.ais_error();
    if (ais_error != SA_AIS_ERR_EXIST) {
      std::cerr << "CreateClass failed: "
                << saf_error(ais_error) << std::endl;
      exit(EXIT_FAILURE);
    }
  }

  std::cout << "Create IMM object class `CfgCcbCreateClass` successfully"
            << std::endl;
  std::cout << "Enter to continue ..." << std::endl; getchar();

  //
  // Acquire Admin Ownership handle
  //
  immom::ImmOmAdminOwnerHandle adminobj{omhandle.GetHandle(),
        "OmCcbCreateTest"};
  if (adminobj.InitializeHandle() == false) {
    std::cerr << "Initialize admin handle failed: "
              << adminobj.ais_error_string() << std::endl;
    exit(EXIT_FAILURE);
  }


  //
  // Acquire Ccb Handle
  //

  // As no OI implementer for `CfgCcbCreateClass` class, then
  // have to construct the ccb handle object using SaImmCcbFlagsT flags = 0.
  // Means, we don't want OI to validate the this change.
  //
  // If using default one, means flags = SA_IMM_CCB_REGISTERED_OI,
  // we will get trouble.
  immom::ImmOmCcbHandle ccbhandleobj{adminobj.GetHandle(), 0};
  if (ccbhandleobj.InitializeHandle() == false) {
    std::cerr << "Initialize CCB handle failed: "
              << ccbhandleobj.ais_error_string() << std::endl;
    exit(EXIT_FAILURE);
  }

  //
  // CCB Create/Modify/Delete
  //
  TestOmCcbCreate(&ccbhandleobj);
  // Test set admin owner
  immom::ImmOmAdminOwnerSet adminset{adminobj.GetHandle(), rdn};
  TestOmSetAdminOwner(&adminset);
  TestOmCcbUpdateReplace(&ccbhandleobj);
  TestOmCcbUpdateAdd(&ccbhandleobj);
  TestOmCcbUpdateDelete(&ccbhandleobj);
  TestOmCcbDelete(&ccbhandleobj);

  immom::ImmOmClassDelete deleteclassobj{omhandle.GetHandle()};
  if (deleteclassobj.DeleteClass("CfgCcbCreateClass") == false) {
    std::cerr << "DeleteClass failed: "
              << deleteclassobj.ais_error_string() << std::endl;
    exit(EXIT_FAILURE);
  }

  auto end = CLOCK_END;
  TIME_ELAPSED(start, end);
}
