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

#include "experimental/immcpp/api/demo/oiruntimeobject.h"

#include <unistd.h>
#include <string>
#include <iomanip>
#include <vector>
#include <map>

#include "base/saf_error.h"
#include "ais/include/saAis.h"
#include "base/osaf_extended_name.h"
#include "experimental/immcpp/api/demo/common.h"
#include "experimental/immcpp/api/include/oi_handle.h"
#include "experimental/immcpp/api/include/oi_implementer.h"
#include "experimental/immcpp/api/include/oi_runtime_object_create.h"
#include "experimental/immcpp/api/include/oi_runtime_object_update.h"
#include "experimental/immcpp/api/include/oi_runtime_object_delete.h"

static SaConstStringT rdn = "immWrpTestRtObj=ImmcppTest";
static SaConstStringT dn = "immWrpTestRtObj=ImmcppTest,safApp=safLogService";

//>
// Use case: create runtime object with single/multiple attributes
//<
static void CreateRuntimeObj(
    SaImmOiHandleT handle, const char* parent = nullptr) {
  SaNameT name;
  SaInt32T int32val = -32;
  SaUint32T uint32val = 32;
  SaInt64T int64val = -64;
  SaUint64T uint64val = 64;
  SaFloatT floatval = 3.140000;
  SaDoubleT doubleval = 2.712200;
  std::string stringval = "test_string";
  std::string stringval2 = "test_string2";
  const std::vector<std::string*> vstring{&stringval, &stringval2};
  SaConstStringT valref = "helloworld";
  osaf_extended_name_lend(valref, &name);

  immoi::ImmOiRtObjectCreate createobj{handle, "ImmWrpTestRtObjConfig"};

  if (parent != nullptr) createobj.SetParentName(parent);
  createobj.SetAttributeValue("immWrpTestRtObj", &rdn);
  createobj.SetAttributeValue("SaUint64TSingle", &uint64val);
  createobj.SetAttributeValue("SaUint32TSingle", &uint32val);
  createobj.SetAttributeValue("SaStringSingle",  &stringval);
  createobj.SetAttributeValue("SaNameTSingle",   &name);
  createobj.SetAttributeValue("SaInt64TSingle",  &int64val);
  createobj.SetAttributeValue("SaInt32TSingle",  &int32val);
  createobj.SetAttributeValue("SaFloatSingle",   &floatval);
  createobj.SetAttributeValue("SaDoubleSingle",  &doubleval);

  SaUint64T mval11 = 11;
  createobj.SetAttributeValue("SaStringTMultiValue", vstring);
  createobj.SetAttributeValue("SaUint64TMultiValue", &mval11);
  if (createobj.CreateObject() == false) {
    std::cerr << "OI Create runtime object: CreateRtObject() failed: "
              <<  createobj.ais_error_string() << std::endl;
    exit(EXIT_FAILURE);
  }
}

//>
// Use case: replace existing values to other values
//<
static void UpdateReplaceRuntimeObj(SaImmOiHandleT handle) {
  SaConstStringT val1 = "TEST_UPDATE";
  SaInt64T val2 = -128;
  SaConstStringT mval1 = "hello";
  SaConstStringT mval2 = "world";
  SaUint64T mval11 = 11;
  SaUint64T mval22 = 22;

  const std::vector<SaConstStringT*> mvalchar = {&mval1, &mval2};
  const std::vector<SaUint64T*> mvalu64 = {&mval11, &mval22};

  immoi::ImmOiRtObjectUpdate updateobj{handle, dn};
  updateobj.ReplaceAttributeValue("SaStringSingle", &val1);
  updateobj.ReplaceAttributeValue("SaInt64TSingle", &val2);
  updateobj.ReplaceAttributeValue("SaStringTMultiValue", mvalchar);
  updateobj.ReplaceAttributeValue("SaUint64TMultiValue", mvalu64);
  if (updateobj.UpdateObject() == false) {
    std::cerr << "OI Update runtime object: UpdateRtObject() failed: "
              << updateobj.ais_error_string() << std::endl;
    exit(EXIT_FAILURE);
  }
}

//>
// Use case: Replace eixsting values to <Empty>
//<
static void UpdateReplaceEmptyRuntimeObj(SaImmOiHandleT handle) {
  immoi::ImmOiRtObjectUpdate updateobj{handle, dn};
  // Use explicitly typname T when using default value (NULL)
  updateobj.ReplaceAttributeValue<SaInt64T>("SaInt64TSingle");
  updateobj.ReplaceAttributeValue<SaStringT>("SaStringTMultiValue");
  updateobj.ReplaceAttributeValue<SaUint64T>("SaUint64TMultiValue");
  if (updateobj.UpdateObject() == false) {
    std::cerr << "OI Update runtime object: UpdateRtObject() failed: "
              << updateobj.ais_error_string() << std::endl;
    exit(EXIT_FAILURE);
  }
}

//>
// Use case: Add muliple values to existing attribute values
//<
static void UpdateAddRuntimeObj(SaImmOiHandleT handle) {
  SaConstStringT mval3 = "there!";
  SaUint64T mval33 = 33;

  immoi::ImmOiRtObjectUpdate updateobj{handle, dn};
  updateobj.AddAttributeValue("SaStringTMultiValue", &mval3);
  updateobj.AddAttributeValue("SaUint64TMultiValue", &mval33);
  if (updateobj.UpdateObject() == false) {
    std::cerr << "OI Update runtime object: UpdateRtObject() failed: "
              << updateobj.ais_error_string() << std::endl;
    exit(EXIT_FAILURE);
  }
}

//>
// Use case: Delete single/multiple values from existing attribute values
//<
static void UpdateDeleteRuntimeObj(SaImmOiHandleT handle) {
  SaConstStringT mval1 = "hello";
  SaConstStringT mval2 = "world";

  SaUint64T mval11 = 11;
  SaUint64T mval22 = 22;

  const std::vector<SaConstStringT*> mvalchar = { &mval1, &mval2 };
  const std::vector<SaUint64T*> mvalu64 = {&mval11, &mval22};

  immoi::ImmOiRtObjectUpdate updateobj{handle, dn};
  updateobj.DeleteAttributeValue("SaStringTMultiValue", mvalchar);
  updateobj.DeleteAttributeValue("SaUint64TMultiValue", mvalu64);
  if (updateobj.UpdateObject() == false) {
    std::cerr << "OI Update runtime object: UpdateRtObject() failed: "
              << updateobj.ais_error_string() << std::endl;
    exit(EXIT_FAILURE);
  }
}

//>
// Show sample code doing:
// 1) Create IMM runtime object
// 2) Update(add/replace/delete) single/multiple attributes
// 3) Delete IMM runtime object
//<
void TestOiRuntimeObject() {
  auto start = CLOCK_START;

  immoi::ImmOiHandle oihandle{};
  if (oihandle.InitializeHandle() == false) {
    std::cerr << "OI handle: InitHandle() failed: "
              << oihandle.ais_error_string() << std::endl;
    exit(EXIT_FAILURE);
  }

  // No configurable IMM object, then no class implementer is registered.
  immoi::ImmOiObjectImplementer impl{oihandle.GetHandle(), "implementerTest"};
  if (impl.SetImplementers() == false) {
    std::cerr << "OI: SetImplementer() failed: "
              << impl.ais_error_string() << std::endl;
    exit(EXIT_FAILURE);
  }

  std::cout << "Enter to continue ..." << std::endl; getchar();

  // Create new runtime object
  CreateRuntimeObj(oihandle.GetHandle());

  // Delete runtime object
  immoi::ImmOiRtObjectDelete deleteobj(oihandle.GetHandle());
  if (deleteobj.DeleteObject(rdn) == false) {
    std::cerr << "Delete runtime object 1st: DeleteObject() failed: "
              << deleteobj.ais_error_string() << std::endl;
    exit(EXIT_FAILURE);
  }

  // Create runtime object with parent object
  CreateRuntimeObj(oihandle.GetHandle(), "safApp=safLogService");
  std::cout << "Create the object `" << dn << "` successfully!"
            << std::endl;
  std::cout << "Enter to continue ..." << std::endl; getchar();

  // Update runtime object with replace value.
  UpdateReplaceRuntimeObj(oihandle.GetHandle());
  std::cout << "Update (replacing) attributes of `" << dn
            << "` successfully" << std::endl;
  std::cout << "Enter to continue ..." << std::endl; getchar();

  UpdateAddRuntimeObj(oihandle.GetHandle());
  std::cout << "Update (adding) attributes of `" << dn
            << "` successfully" << std::endl;
  std::cout << "Enter to continue ..." << std::endl; getchar();

  UpdateDeleteRuntimeObj(oihandle.GetHandle());
  std::cout << "Update (deleting) attributes of `" << dn
            << "` successfully" << std::endl;
  std::cout << "Enter to continue ..." << std::endl; getchar();

  UpdateReplaceEmptyRuntimeObj(oihandle.GetHandle());
  std::cout << "Update (delete all) attributes of `" << dn
            << "` successfully" << std::endl;
  std::cout << "Enter to continue ..." << std::endl; getchar();

  if (deleteobj.DeleteObject(dn) == false) {
    std::cerr << "OI Delete runtime object 2nd: DeleteObject() failed: "
              << deleteobj.ais_error_string() << std::endl;
  }

  auto end = CLOCK_END;
  TIME_ELAPSED(start, end);
}
