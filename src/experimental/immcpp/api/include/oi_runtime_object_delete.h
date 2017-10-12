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

#ifndef SRC_LOG_IMMWRP_IMMOI_OI_RUNTIME_OBJECT_DELETE_H_
#define SRC_LOG_IMMWRP_IMMOI_OI_RUNTIME_OBJECT_DELETE_H_


#include <string>

#include "ais/include/saImmOi.h"
#include "experimental/immcpp/api/common/common.h"
#include "base/logtrace.h"

namespace immoi {

//>
// Delete IMM runtime object of given object name.
//
// Usage example:
// ImmOiRtObjectDelete deleteobj{oihandle};
//
// if (deleteobj.DeleteObject(dn) == false) {
//   cerr << "Failed to delete given object: " << deleteobj.ais_error() << endl;
//   return;
//}
//<
class ImmOiRtObjectDelete : public ImmBase {
 public:
  explicit ImmOiRtObjectDelete(const SaImmOiHandleT& oi_handle);
  ~ImmOiRtObjectDelete();

  // Return true if the runtime object is deleted successfully.
  // Invoke ais_error() to get returned AIS code.
  bool DeleteObject(const std::string& object_name);

 private:
  SaImmOiHandleT oi_handle_;

  DELETE_COPY_AND_MOVE_OPERATORS(ImmOiRtObjectDelete);
};

}  // namespace immoi

#endif  // SRC_LOG_IMMWRP_IMMOI_OI_RUNTIME_OBJECT_DELETE_H_
