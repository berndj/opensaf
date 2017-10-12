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

#include "experimental/immcpp/api/demo/omclassmanagement.h"

#include <string>
#include <iomanip>
#include <vector>
#include <map>

#include "experimental/immcpp/api/demo/common.h"
#include "experimental/immcpp/api/include/om_handle.h"
#include "experimental/immcpp/api/include/om_class_description_get.h"

#include "base/saf_error.h"
#include "base/osaf_extended_name.h"

static SaVersionT imm_version = { 'A', 2, 11 };

//------------------------------------------------------------------------------
// Check GetSingle() value on all data types
//------------------------------------------------------------------------------
static void GetSingleValue(const immom::ImmOmClassDescriptionGet& obj,
                           const std::string& name, bool isMultiple = true) {
  auto it = (isMultiple == true) ? mapdb.find(name) : smapdb.find(name);

  switch (it->second) {
    case SA_IMM_ATTR_SAINT32T: {
      auto output = obj.GetDefaultValue<SaInt32T>(name);
      if (output == nullptr) return;
      std::cout << name << ": " << *output << std::endl;
      break;
    }

    case SA_IMM_ATTR_SAUINT32T: {
      auto output = obj.GetDefaultValue<SaUint32T>(name);
      if (output == nullptr) return;
      std::cout << name << ": " << *output << std::endl;
      break;
    }

    case SA_IMM_ATTR_SAINT64T: {
      auto output = obj.GetDefaultValue<SaInt64T>(name);
      if (output == nullptr) return;
      std::cout << name << ": " << *output << std::endl;
      break;
    }

    case SA_IMM_ATTR_SAUINT64T: {
      auto output = obj.GetDefaultValue<SaUint64T>(name);
      if (output == nullptr) return;
      std::cout << name << ": " << *output << std::endl;
      break;
    }

    case SA_IMM_ATTR_SATIMET: {
      auto output = obj.GetDefaultValue<SaTimeT>(name);
      if (output == nullptr) return;
      std::cout << name << ": " << *output << std::endl;
      break;
    }

    case SA_IMM_ATTR_SANAMET: {
      auto output = obj.GetDefaultValue<SaNameT>(name);
      if (output == nullptr) return;
      std::string tmp = osaf_extended_name_borrow(output);
      std::cout << name << ": " << tmp << std::endl;
      break;
    }

    case SA_IMM_ATTR_SAFLOATT: {
      auto output = obj.GetDefaultValue<SaFloatT>(name);
      if (output == nullptr) return;
      std::cout << name << ": " << std::setprecision(5) << *output << std::endl;
      break;
    }

    case SA_IMM_ATTR_SADOUBLET: {
     auto output = obj.GetDefaultValue<SaDoubleT>(name);
     if (output == nullptr) return;
     std::cout << name << ": " << std::setprecision(5) << *output << std::endl;
     break;
    }

    case SA_IMM_ATTR_SASTRINGT: {
     auto output = obj.GetDefaultValue<SaStringT>(name);
     if (output == nullptr) return;
     std::cout << name << ": " << *output << std::endl;
     break;
    }

    default:
      std::cerr << "Unknown type: " << name << std::endl;
      break;
  }
}

void TestOmClassDescriptionGetOnSingleValueClass() {
  auto start = CLOCK_START;

  immom::ImmOmHandle omhandle{imm_version};
  if (omhandle.InitializeHandle() == false) {
    std::cerr << "OM handle: InitializeHandle() failed: "
              << omhandle.ais_error_string() << std::endl;
    return;
  }

  const std::string classnames = "ImmWrpTestConfig";
  immom::ImmOmClassDescriptionGet classhandle{omhandle.GetHandle(), classnames};
  if (classhandle.GetClassDescription() == false) {
    std::cerr << "GetClassDescription() failed: "
              << classhandle.ais_error_string() << std::endl;
    return;
  }

  // Get initialized value of single-value attributes
  SaImmClassCategoryT categorys = classhandle.GetClassCategory();
  std::cout << "Class ImmWrpTestConfig, Type: " << categorys << std::endl;
  std::cout << "---------------------------" << std::endl;
  for (const auto& attr : smapdb) {
    GetSingleValue(classhandle, attr.first, false);
  }

  auto end = CLOCK_END;
  TIME_ELAPSED(start, end);
}

void TestOmClassDescriptionGetOnMultipleValueClass() {
  auto start = CLOCK_START;

  immom::ImmOmHandle omhandle{imm_version};
  if (omhandle.InitializeHandle() == false) {
    std::cerr << "OM handle: InitializeHandle() failed: "
              << omhandle.ais_error_string() << std::endl;
    return;
  }

  const std::string classname = "ImmWrpTestMultipleConfig";
  immom::ImmOmClassDescriptionGet classhandle{omhandle.GetHandle(), classname};
  if (classhandle.GetClassDescription() == false) {
    std::cerr << "Class Description Get: GetClassDescription() failed: "
              << classhandle.ais_error_string() << std::endl;
    return;
  }

  // Get initialized values of multiple-value attributes
  SaImmClassCategoryT category = classhandle.GetClassCategory();
  std::cout << "Class ImmWrpMultipleTestConfig, Type: "
            << category << std::endl;
  std::cout << "---------------------------" << std::endl;
  for (const auto& attr : mapdb) GetSingleValue(classhandle, attr.first);

  auto end = CLOCK_END;
  TIME_ELAPSED(start, end);
}
