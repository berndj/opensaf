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

#ifndef SRC_LOG_IMMWRP_IMMOM_OM_ACCESSOR_HANDLE_H_
#define SRC_LOG_IMMWRP_IMMOM_OM_ACCESSOR_HANDLE_H_

#include "ais/include/saImmOm.h"
#include "experimental/immcpp/api/common/common.h"
#include "base/logtrace.h"

namespace immom {

//>
// Manage (initialize/finalize) IMM OM accessor handle.
//
// IMM OM accessor handle can be acquired in two ways:
// 1) Explicitly invoke InitializeHandle(), then get the handle via GetHandle().
// 2) Invoke GetHandle(), then the handle is implicitly acquired.
//
// Note:
// The handle is implicitly finalized when object goes out of scope. This is the
// case also if the handle is explicitly created
//
// Example:
// Explicitly create and get the handle.
//
// ImmOmAccessorHandle accesshandle{omhandle};
// if (accesshandle.InitializeHandle() == false) {
//   // ER handling
// }
//
// SaImmAccessorHandleT h = accesshandle.GetHandle();
//<
class ImmOmAccessorHandle : public ImmBase {
 public:
  explicit ImmOmAccessorHandle(const SaImmHandleT& om_handle);
  // The IMM OM accessor handle will be finalized inside the destructor
  // if the handle is not yet finalized.
  ~ImmOmAccessorHandle();

  // Initialize the IMM OM accessor handle. Return true if the handle
  // is successfully initialized.
  // Use the ais_error() to get AIS error code.
  bool InitializeHandle();

  // Finalize the IMM OM accessor handle. Return true if the handle
  // is successfully finalized.
  // Use the ais_error() to get AIS error code.
  bool FinalizeHandle();

  // Get the IMM OM accessor handle and implicitly do initializing
  // inside the method if the handle has not been yet initialized.
  // Use the ais_error() to get AIS error code.
  SaImmAccessorHandleT GetHandle();

 private:
  SaImmHandleT om_handle_;
  SaImmAccessorHandleT access_handle_;

  DELETE_COPY_AND_MOVE_OPERATORS(ImmOmAccessorHandle);
};

}  // namespace immom

#endif  // SRC_LOG_IMMWRP_IMMOM_OM_ACCESSOR_HANDLE_H_
