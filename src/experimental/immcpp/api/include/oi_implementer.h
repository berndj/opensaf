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

#ifndef SRC_LOG_IMMWRP_IMMOI_OI_IMPLEMENTER_H_
#define SRC_LOG_IMMWRP_IMMOI_OI_IMPLEMENTER_H_

#include <string>
#include <vector>
#include "ais/include/saImmOi.h"
#include "experimental/immcpp/api/common/common.h"
#include "base/logtrace.h"

namespace immoi {

//>
// Set/clear IMM OI Implementer, class and object implementers.
// The OI implementer is implicitly cleared when running out of scope.
//
// The mandatory inputs for constructors are:
// 1) IMM OI oi_handle
// 2) What is implementer name?
// 3) What class names would like to attach to that implementer?
//
// User can set/clear OI and all registered class implementers name in one go
// or do it one by one.
//
// Usage example:
// ImmOiObjectImplementer objimplementer{
//   oi_handle,
//   "safLogService",
//   {"SaLogStreamConfig", "OpenSafLogConfig"}
// };
//
// if (objimplementer.SetImplementers() == false) {
//   LOG_ER("Failed to set implementers: %d", objimplementer.ais_error());
//   return;
// }
//<
class ImmOiObjectImplementer : public ImmBase {
 public:
  // Construct the object with given implementer name.
  // Use this constructor if the user only has runtime object.
  explicit ImmOiObjectImplementer(const SaImmOiHandleT& oi_handle,
                                  const std::string& implementer_name);
  // Construct the object with given implementer name and
  // has one configurable class.
  explicit ImmOiObjectImplementer(const SaImmOiHandleT& oi_handle,
                                  const std::string& implementer_name,
                                  const std::string& class_name);
  // Construct the object with given implementer name and list of
  // configurable classes.
  explicit ImmOiObjectImplementer(
      const SaImmOiHandleT& oi_handle,
      const std::string& implementer_name,
      const std::vector<std::string>& list_of_class_names);

  ~ImmOiObjectImplementer();

  // Do set/clear object implementer and class implementers in one go.
  // Return true if set all implementers successfully.
  // Use ais_error() to get returned AIS code.
  bool SetImplementers();
  bool ClearImplementers();

  // Set/Clear all registered class implementer names in one go.
  // Use ais_error() to get returned AIS code.
  bool SetClassImplementers();
  bool ClearClassImplementers();

  // To do Set/Clear OI implementer only, not class implementers.
  // Use ais_error() to get returned AIS code.
  bool SetOiImplementer();
  bool ClearOiImplementer();

  // To set/clear given class implementers
  // Use ais_error() to get returned AIS code.
  bool SetClassImplementer(const std::string& class_name);
  bool ClearClassImplementer(const std::string& class_name);

  // To set/release given object implementer on given scope.
  // Default scope of operation is targeted to a single object.
  // Use ais_error() to get returned AIS code.
  bool SetObjectImplementer(const std::string& object_name,
                            SaImmScopeT scope = SA_IMM_ONE);
  bool ReleaseObjectImplementer(const std::string& object_name,
                                SaImmScopeT scope = SA_IMM_ONE);

 private:
  SaImmOiHandleT oi_handle_;
  bool is_set_oi_implementer_invoked_;
  std::string implementer_name_;
  std::vector<std::string> list_of_class_names_;

  DELETE_COPY_AND_MOVE_OPERATORS(ImmOiObjectImplementer);
};

}  // namespace immoi

#endif  // SRC_LOG_IMMWRP_IMMOI_OI_IMPLEMENTER_H_
