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

#include "experimental/immcpp/api/demo/omclasscreate.h"
#include <unistd.h>
#include <string>
#include <iomanip>
#include <iostream>
#include <vector>
#include "experimental/immcpp/api/demo/common.h"
#include "experimental/immcpp/api/include/om_handle.h"
#include "experimental/immcpp/api/include/om_class_create.h"
#include "experimental/immcpp/api/include/om_class_delete.h"
#include "base/saf_error.h"
#include "base/osaf_extended_name.h"

static SaVersionT imm_version = { 'A', 2, 11 };

void TestOmClassCreateConfigurable() {
  auto start = CLOCK_START;

  immom::ImmOmHandle omhandle{imm_version};
  if (omhandle.InitializeHandle() == false) {
    std::cerr << "OM handle: InitHandle() failed: "
              << omhandle.ais_error_string() << std::endl;
    exit(EXIT_FAILURE);
  }

  immom::ImmOmCfgClassCreate createclass{
    omhandle.GetHandle(), "CfgCreateClass"};
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
      std::cerr << "CreateClass failed: " << saf_error(ais_error) << std::endl;
      exit(EXIT_FAILURE);
    }
  }

  immom::ImmOmClassDelete deleteclassobj{omhandle.GetHandle()};
  if (deleteclassobj.DeleteClass("CfgCreateClass") == false) {
    std::cerr << "DeleteClass failed: "
              << deleteclassobj.ais_error_string() << std::endl;
    exit(EXIT_FAILURE);
  }

  auto end = CLOCK_END;
  TIME_ELAPSED(start, end);
}

void TestOmClassCreateRuntime() {
  auto start = CLOCK_START;

  immom::ImmOmHandle omhandle{imm_version};
  if (omhandle.InitializeHandle() == false) {
    std::cerr << "OM handle: InitHandle() failed: "
              << omhandle.ais_error_string() << std::endl;
    exit(EXIT_FAILURE);
  }

  immom::ImmOmRtClassCreate createclass{omhandle.GetHandle(), "RtCreateClass"};
  SaUint32T uin32val = 10;
  SaConstStringT str = "hello";
  SaUint64T uin64val = 12345678;

  SaImmAttrFlagsT flags = SA_IMM_ATTR_RDN | SA_IMM_ATTR_CACHED;
  createclass.SetAttributeProperties<SaStringT>("rdn", flags, nullptr);

  createclass.SetAttributeProperties("attr2", &uin32val);
  createclass.SetAttributeProperties("attr3", &str);

  flags = SA_IMM_ATTR_MULTI_VALUE | SA_IMM_ATTR_CACHED;
  createclass.SetAttributeProperties("attr4", flags, &uin64val);

  if (createclass.CreateClass() == false) {
    SaAisErrorT ais_error = createclass.ais_error();
    if (ais_error != SA_AIS_ERR_EXIST) {
      std::cerr << "CreateClass failed: "
                << saf_error(ais_error) << std::endl;
      exit(EXIT_FAILURE);
    }
  }

  immom::ImmOmClassDelete deleteclassobj{omhandle.GetHandle()};
  if (deleteclassobj.DeleteClass("RtCreateClass") == false) {
    std::cerr << "DeleteClass failed: "
              << deleteclassobj.ais_error_string() << std::endl;
    exit(EXIT_FAILURE);
  }

  auto end = CLOCK_END;
  TIME_ELAPSED(start, end);
}
