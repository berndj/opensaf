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
#include "experimental/immcpp/api/demo/omsearchnext.h"

#include <unistd.h>
#include <string>
#include <iomanip>
#include <iostream>
#include <vector>

#include "experimental/immcpp/api/demo/common.h"
#include "experimental/immcpp/api/include/om_handle.h"
#include "experimental/immcpp/api/include/om_search_handle.h"
#include "experimental/immcpp/api/include/om_search_next.h"
#include "base/saf_error.h"
#include "base/osaf_extended_name.h"

static SaVersionT imm_version = { 'A', 2, 11 };

//------------------------------------------------------------------------------
// Check GetMultiSingle() value on all data types
//------------------------------------------------------------------------------
static void GetMultipleValue(const immom::ImmOmSearchNext& obj,
                             const std::string& name) {
  auto it = mapdb.find(name);
  switch (it->second) {
    case SA_IMM_ATTR_SAINT32T: {
      auto output = obj.GetMultiValue<SaInt32T>(name);
      std::cout << name << ": ";
      for (const auto& v : output) {
        std::cout << v << " ";
      }
      std::cout << std::endl;
      break;
    }

    case SA_IMM_ATTR_SAUINT32T: {
      auto output = obj.GetMultiValue<SaUint32T>(name);
      std::cout << name << ": ";
      for (const auto& v : output) {
        std::cout << v << " ";
      }
      std::cout << std::endl;
      break;
    }

    case SA_IMM_ATTR_SAINT64T: {
      auto output = obj.GetMultiValue<SaInt64T>(name);
      std::cout << name << ": ";
      for (const auto& v : output) {
        std::cout << v << " ";
      }
      std::cout << std::endl;
      break;
    }

    case SA_IMM_ATTR_SAUINT64T: {
      auto output = obj.GetMultiValue<SaUint64T>(name);
      std::cout << name << ": ";
      for (const auto& v : output) {
        std::cout << v << " ";
      }
      std::cout << std::endl;
      break;
    }

    case SA_IMM_ATTR_SATIMET: {
      auto output = obj.GetMultiValue<SaTimeT>(name);
      std::cout << name << ": ";
      for (const auto& v : output) {
        std::cout << v << " ";
      }
      std::cout << std::endl;
      break;
    }

    case SA_IMM_ATTR_SANAMET: {
      std::vector<SaNameT> output = obj.GetMultiValue<SaNameT>(name);
      std::cout << name << ": ";
      for (const auto& v : output) {
        std::string tmp = osaf_extended_name_borrow(&v);
        std::cout << tmp << " ";
      }
      std::cout << std::endl;
      break;
    }

    case SA_IMM_ATTR_SAFLOATT: {
      auto output = obj.GetMultiValue<SaFloatT>(name);
      std::cout << name << ": ";
      for (const auto& v : output) {
        std::cout << v << " ";
      }
      std::cout << std::endl;
      break;
    }

    case SA_IMM_ATTR_SADOUBLET: {
     auto output = obj.GetMultiValue<SaDoubleT>(name);
     std::cout << name << ": ";
     for (const auto& v : output) {
       std::cout << v << " ";
     }
     std::cout << std::endl;
     break;
    }

    case SA_IMM_ATTR_SASTRINGT: {
     auto output = obj.GetMultiValue<SaStringT>(name);
     std::cout << name << ": ";
     for (const auto& v : output) {
       std::cout << v << " ";
     }
     std::cout << std::endl;
     break;
    }

    default:
      std::cerr << "Unknown type: " << name << std::endl;
      break;
  }
}

static void GetSingleValue(const immom::ImmOmSearchNext& obj,
                           const std::string& name) {
  auto it = smapdb.find(name);
  switch (it->second) {
    case SA_IMM_ATTR_SAINT32T: {
      auto output = obj.GetSingleValue<SaInt32T>(name);
      if (output == nullptr) return;
      std::cout << name << ": " << *output << std::endl;
      break;
    }

    case SA_IMM_ATTR_SAUINT32T: {
      auto output = obj.GetSingleValue<SaUint32T>(name);
      if (output == nullptr) return;
      std::cout << name << ": " << *output << std::endl;
      break;
    }

    case SA_IMM_ATTR_SAINT64T: {
      auto output = obj.GetSingleValue<SaInt64T>(name);
      if (output == nullptr) return;
      std::cout << name << ": " << *output << std::endl;
      break;
    }

    case SA_IMM_ATTR_SAUINT64T: {
      auto output = obj.GetSingleValue<SaUint64T>(name);
      if (output == nullptr) return;
      std::cout << name << ": " << *output << std::endl;
      break;
    }

    case SA_IMM_ATTR_SATIMET: {
      auto output = obj.GetSingleValue<SaTimeT>(name);
      if (output == nullptr) return;
      std::cout << name << ": " << *output << std::endl;
      break;
    }

    case SA_IMM_ATTR_SANAMET: {
      auto output = obj.GetSingleValue<SaNameT>(name);
      if (output == nullptr) return;
      std::string tmp = osaf_extended_name_borrow(output);
      std::cout << name << ": " << tmp << std::endl;
      break;
    }

    case SA_IMM_ATTR_SAFLOATT: {
      auto output = obj.GetSingleValue<SaFloatT>(name);
      if (output == nullptr) return;
      std::cout << name << ": " << std::setprecision(5) << *output << std::endl;
      break;
    }

    case SA_IMM_ATTR_SADOUBLET: {
     auto output = obj.GetSingleValue<SaDoubleT>(name);
     if (output == nullptr) return;
     std::cout << name << ": " << std::setprecision(5) << *output << std::endl;
     break;
    }

    case SA_IMM_ATTR_SASTRINGT: {
     auto output = obj.GetSingleValue<SaStringT>(name);
     if (output == nullptr) return;
     std::cout << name << ": " << *output << std::endl;
     break;
    }

    default:
      std::cerr << "Unknown type: " << name << std::endl;
      break;
  }
}

void TestOmSearchNextOnMultipleValuesClass() {
  auto start = CLOCK_START;

  immom::ImmOmHandle omhandle{imm_version};
  if (omhandle.InitializeHandle() == false) {
    std::cerr << "OM handle: InitHandle() failed: "
              << saf_error(omhandle.ais_error()) << std::endl;
    return;
  }

  immom::ImmOmSearchCriteria setting{};
  setting.FetchAttributes();
  const std::string classname = "ImmWrpTestMultipleConfig";
  setting.SetSearchClassName(classname);

  immom::ImmOmSearchHandle searchobj{omhandle.GetHandle(), setting};
  if (searchobj.InitializeHandle() == false) {
    std::cerr << "Search Obj: InitializeHandle() failed: "
              << saf_error(searchobj.ais_error()) << std::endl;
    return;
  }

  immom::ImmOmSearchNext searchnext{searchobj.GetHandle()};
  if (searchnext.SearchNext() == false) {
    std::cerr << "SearchNext() failed: " <<
        saf_error(searchnext.ais_error()) << std::endl;
    return;
  }

  // Get values of multiple-value attributes
  std::string dn = searchnext.GetObjectName();
  std::cout << "Attribute value of DN: " << dn << std::endl;
  std::cout << "---------------------------" << std::endl;
  for (const auto& attr : mapdb) {
    GetMultipleValue(searchnext, attr.first);
  }

  auto end = CLOCK_END;
  TIME_ELAPSED(start, end);
}

void TestOmSearchNextOnSingleValuesClass() {
  auto start = CLOCK_START;

  immom::ImmOmHandle omhandle{imm_version};
  if (omhandle.InitializeHandle() == false) {
    std::cerr << "OM handle: InitializeHandle() failed: "
              << saf_error(omhandle.ais_error()) << std::endl;
    return;
  }

  immom::ImmOmSearchCriteria setting{};
  setting.FetchAttributes();
  const std::string classname = "ImmWrpTestConfig";
  setting.SetSearchClassName(classname);

  immom::ImmOmSearchHandle searchobj{omhandle.GetHandle(), setting};
  if (searchobj.InitializeHandle() == false) {
    std::cerr << "Search Obj: InitializeHandle() failed: "
              << saf_error(searchobj.ais_error()) << std::endl;
    return;
  }

  immom::ImmOmSearchNext searchnext{searchobj.GetHandle()};
  if (searchnext.SearchNext() == false) {
    std::cerr << "SearchNext() failed: "
              << saf_error(searchnext.ais_error()) << std::endl;
    return;
  }

  // Get value of single-value attributes
  std::string dn = searchnext.GetObjectName();
  std::cout << "Attribute value of DN: " << dn << std::endl;
  std::cout << "---------------------------" << std::endl;
  for (const auto& attr : smapdb) {
    GetSingleValue(searchnext, attr.first);
  }

  auto end = CLOCK_END;
  TIME_ELAPSED(start, end);
}
