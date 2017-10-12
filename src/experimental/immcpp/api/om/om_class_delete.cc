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

#include "experimental/immcpp/api/include/om_class_delete.h"
#include "base/logtrace.h"

namespace immom {

//------------------------------------------------------------------------------
// ImmOmClassDelete class
//------------------------------------------------------------------------------
ImmOmClassDelete::ImmOmClassDelete(const SaImmHandleT& om_handle)
    : ImmBase(),
      om_handle_{om_handle} { }

ImmOmClassDelete::~ImmOmClassDelete() {}

bool ImmOmClassDelete::DeleteClass(const std::string& classname) {
  TRACE_ENTER();
  char* name = const_cast<char*>(classname.c_str());
  base::Timer wtime(retry_ctrl_.timeout);
  while (wtime.is_timeout() == false) {
    ais_error_ = saImmOmClassDelete(om_handle_, name);
    if (ais_error_ == SA_AIS_ERR_TRY_AGAIN) {
      base::Sleep(retry_ctrl_.interval);
      continue;
    }
    break;
  }
  return (ais_error_ == SA_AIS_OK);
}

}  // namespace immom
