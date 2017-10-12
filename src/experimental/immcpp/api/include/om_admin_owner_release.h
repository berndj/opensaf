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

#ifndef SRC_LOG_IMMWRP_IMMOM_OM_ADMIN_OWNER_RELEASE_H_
#define SRC_LOG_IMMWRP_IMMOM_OM_ADMIN_OWNER_RELEASE_H_

#include <vector>
#include <string>
#include "ais/include/saImmOm.h"
#include "experimental/immcpp/api/common/common.h"
#include "base/logtrace.h"

namespace immom {

//>
// Release Administrative Owner of given object names.
// The default operation scope is SUBTREE level.
//
// Use this class to release an admin owner that is not released by an object
// of the ImmOmAdminOwnerSet class

// Example:
// Release admin ownership of two objects
//
// ImmOmAdminOwnerRelease adminrelease{adminhandle, {dn1, dn2}};
// if (adminrelease.ReleaseAdminOwner() == false) {
//   // ERR handling
// }
//
//<
class ImmOmAdminOwnerRelease : public ImmBase {
 public:
  // Construct the object with one object name or a list of object names
  explicit ImmOmAdminOwnerRelease(
      const SaImmAdminOwnerHandleT& admin_owner_handle,
      const std::string& object_name);
  explicit ImmOmAdminOwnerRelease(
      const SaImmAdminOwnerHandleT& admin_owner_handle,
      const std::vector<std::string>& list_of_object_names);
  ~ImmOmAdminOwnerRelease();

  bool ReleaseAdminOwner(SaImmScopeT scope = SA_IMM_SUBTREE);

 private:
  std::vector<std::string> object_names_;
  SaImmAdminOwnerHandleT admin_owner_handle_;

  DELETE_COPY_AND_MOVE_OPERATORS(ImmOmAdminOwnerRelease);
};

}  // namespace immom

#endif  // SRC_LOG_IMMWRP_IMMOM_OM_ADMIN_OWNER_RELEASE_H_
