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

#ifndef SRC_LOG_IMMWRP_IMMOM_OM_ADMIN_OWNER_SET_H_
#define SRC_LOG_IMMWRP_IMMOM_OM_ADMIN_OWNER_SET_H_

#include <vector>
#include <string>
#include "ais/include/saImmOm.h"
#include "experimental/immcpp/api/common/common.h"
#include "base/logtrace.h"

namespace immom {

//>
// Set Administration Owner to given object names.
// The default operation scope is SA_IMM_SUBTREE level.
//
// When the object goes out of scope, the administrative ownership is released
// on registered objects using the scope used when admin ownership was set.
//
// Use `ImmOmAdminOwnerRelease` to release admin ownership on specific object(s)
// or on different admin handle. Refer to ~/om_admin_owner_release.h
//
// Example:
// Become admin owner of two objects do something and explicitly release the
// admin ownership again
//
// ImmOmAdminOwnerSet adminset{adminhandle, {dn1, dn2}};
// if (adminset.SetAdminOwner() == false) {
//   // ERR handling
// }
//
// Do something...
//
// adminset.ReleaseAdminOwner();
//<
class ImmOmAdminOwnerSet : public ImmBase {
 public:
  // Construct the object with one object name or list of object names
  explicit ImmOmAdminOwnerSet(const SaImmAdminOwnerHandleT& admin_owner_handle,
                              const std::string& object_name);
  explicit ImmOmAdminOwnerSet(
      const SaImmAdminOwnerHandleT& admin_owner_handle,
      const std::vector<std::string>& list_of_object_names);
  ~ImmOmAdminOwnerSet();

  // Set Administrative owner to object names on given scope.
  bool SetAdminOwner(SaImmScopeT scope = SA_IMM_SUBTREE);

  // Release administrative owner for registered objects with SAME scope
  // which has been set previously by SetAdminOwner().
  bool ReleaseAdminOwner();

 private:
  std::vector<std::string> object_names_;
  SaImmScopeT scope_;
  bool is_set_admin_owner_invoked_;
  SaImmAdminOwnerHandleT admin_owner_handle_;

  DELETE_COPY_AND_MOVE_OPERATORS(ImmOmAdminOwnerSet);
};

}  // namespace immom

#endif  // SRC_LOG_IMMWRP_IMMOM_OM_ADMIN_OWNER_SET_H_
