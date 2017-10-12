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

#ifndef SRC_LOG_IMMWRP_APITEST_COMMON_H_
#define SRC_LOG_IMMWRP_APITEST_COMMON_H_

#include <string>
#include <iostream>
#include <map>
#include <chrono>

#include "experimental/immcpp/api/include/om_handle.h"

//>
// For performent measurement
//<
#define CLOCK_START std::chrono::high_resolution_clock::now()
#define CLOCK_END   std::chrono::high_resolution_clock::now()
#define TIME_ELAPSED(start, end)                    {       \
    std::chrono::duration<double> elapsed = end - start;    \
    std::cout <<"-> " << __func__ << " @Elapsed time: " <<  \
        elapsed.count() << std::endl;                       \
  }

//<

using MapDb = std::map<std::string, SaImmValueTypeT>;

// Multiple-value attributes map
const MapDb mapdb = {
  {"SaUint32TMultiple", SA_IMM_ATTR_SAUINT32T},
  {"SaInt32TMultiple",  SA_IMM_ATTR_SAINT32T},
  {"SaUint64TMultiple", SA_IMM_ATTR_SAUINT64T},
  {"SaInt64TMultiple",  SA_IMM_ATTR_SAINT64T},
  {"SaFloatMultiple",   SA_IMM_ATTR_SAFLOATT},
  {"SaDoubleMultiple",  SA_IMM_ATTR_SADOUBLET},
  {"SaStringMultiple",  SA_IMM_ATTR_SASTRINGT},
  {"SaNameTMultiple",   SA_IMM_ATTR_SANAMET}
};

// Single-value attribute map
const MapDb smapdb = {
  {"SaUint32TSingle", SA_IMM_ATTR_SAUINT32T},
  {"SaInt32TSingle",  SA_IMM_ATTR_SAINT32T},
  {"SaUint64TSingle", SA_IMM_ATTR_SAUINT64T},
  {"SaInt64TSingle",  SA_IMM_ATTR_SAINT64T},
  {"SaFloatSingle",   SA_IMM_ATTR_SAFLOATT},
  {"SaDoubleSingle",  SA_IMM_ATTR_SADOUBLET},
  {"SaStringSingle",  SA_IMM_ATTR_SASTRINGT},
  {"SaNameTSingle",   SA_IMM_ATTR_SANAMET}
};

#endif  // SRC_LOG_IMMWRP_APITEST_COMMON_H_
