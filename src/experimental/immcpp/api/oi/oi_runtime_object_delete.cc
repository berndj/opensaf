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

#include "experimental/immcpp/api/include/oi_runtime_object_delete.h"
#include "base/osaf_extended_name.h"
#include "base/logtrace.h"

namespace immoi {

//------------------------------------------------------------------------------
// ImmOiRtObjectDelete class
//------------------------------------------------------------------------------
ImmOiRtObjectDelete::ImmOiRtObjectDelete(const SaImmOiHandleT& oi_handle)
    : ImmBase(), oi_handle_{oi_handle} { }

ImmOiRtObjectDelete::~ImmOiRtObjectDelete() {}

// Delete the object designated by name and the entire subtree of object
// rooted at the object
bool ImmOiRtObjectDelete::DeleteObject(const std::string& dn) {
  TRACE_ENTER();

  if (dn.empty() == true) {
    ais_error_ = SA_AIS_ERR_INVALID_PARAM;
    return false;
  }

  SaNameT object_name;
  base::Timer wtime(retry_ctrl_.timeout);
  osaf_extended_name_lend(dn.c_str(), &object_name);
  while (wtime.is_timeout() == false) {
    ais_error_ = saImmOiRtObjectDelete(oi_handle_, &object_name);
    if (ais_error_ == SA_AIS_ERR_TRY_AGAIN) {
      base::Sleep(retry_ctrl_.interval);
      continue;
    }
    break;
  }
  return (ais_error_ == SA_AIS_OK);
}

}  // namespace immoi
