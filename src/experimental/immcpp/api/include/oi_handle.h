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

#ifndef SRC_LOG_IMMWRP_IMMOI_OI_HANDLE_H_
#define SRC_LOG_IMMWRP_IMMOI_OI_HANDLE_H_

#include "ais/include/saImmOi.h"
#include "experimental/immcpp/api/common/common.h"
#include "base/logtrace.h"

namespace immoi {

//>
//>
// Manage IMM OI handle - the handle provides access to the Object Implementer
// APIs of IMM service. So, the method InitializeHandle() must be called
// before using any other IMM OI abstracts.
//
// The handle can be explicitly finalized using FinalizeHandle()
// but it is also finalized when object goes out of scope.
//
// Acquire IMM OI handle in two ways:
// 1) Implicitly invoke InitializeHandle(), then get the handle via GetHandle().
// 2) Invoke GetHandle(), then the handle is implicitly acquired.
//
// Usage example:
// ImmOiHandle handle{};
// if (handle.InitializeHandle() == false) {
//    // ERR handling
// }
//
// if (fds[FD_IMM].revents & POLLIN) {
//    handle.DispatchAll();
// }
//
//<
class ImmOiHandle : public ImmBase {
 public:
  // Construct the object with default IMM version and given callbacks
  explicit ImmOiHandle(const SaImmOiCallbacksT_2* callbacks = nullptr);
  // Construct the object with given IMM version and given callbacks
  explicit ImmOiHandle(const SaVersionT& version,
                       const SaImmOiCallbacksT_2* callbacks = nullptr);

  ~ImmOiHandle();

  // Return true if the handle is successfully initialized/finalized.
  // Use ais_error() to get AIS return code.
  bool InitializeHandle();
  bool FinalizeHandle();

  // Fetch the operating system handle associated with the handle OI handle.
  // Use ais_error() to get AIS return code.
  bool FetchSelectionObject();

  bool DispatchOne();
  bool DispatchAll();
  bool DispatchBlocking();

  // Get the IMM OI handle and initialize the handle if not yet initialized.
  // Use ais_error() to get AIS return code.
  SaImmOiHandleT GetHandle();

  // Get selection object and implicitly acquire the selection object from IMM
  // if needed.
  SaSelectionObjectT fd();

  // Get the version actually supported by the IMM Service.
  // Should call this method in case of getting SA_AIS_ERR_VERSION
  // to know what version is supported by IMM.
  const SaVersionT GetSupportedVersion() const { return supported_version_; }

 private:
  bool Dispatch(SaDispatchFlagsT flag);

 private:
  SaSelectionObjectT fd_;
  const SaImmOiCallbacksT_2* callbacks_;
  SaVersionT version_;
  SaVersionT supported_version_;
  SaImmOiHandleT oi_handle_;
};

}  // namespace immoi

#endif  // SRC_LOG_IMMWRP_IMMOI_OI_HANDLE_H_
