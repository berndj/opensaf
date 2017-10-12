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

#ifndef SRC_LOG_IMMWRP_IMMOI_OI_ADMIN_OPERATION_RESULT_H_
#define SRC_LOG_IMMWRP_IMMOI_OI_ADMIN_OPERATION_RESULT_H_

#include <string>
#include "ais/include/saImmOi.h"
#include "experimental/immcpp/api/common/common.h"
#include "base/logtrace.h"

namespace immoi {
//>
// Inform the IMM Service about the result of the execution of
// an administrative operation requested by the IMM Service.
//
// Usage example:
// ImmOiAdminOperationResult adminop{handle};
// SaInvocationT in;
// SaAisErrorT result;
//
// if (adminop.InformResult(in, result) == false) {
//   cerr << "SA AIS error code: " << adminop.ais_error() << endl;
//   return;
// }
//<
class ImmOiAdminOperationResult : public ImmBase {
 public:
  explicit ImmOiAdminOperationResult(const SaImmOiHandleT& oi_handle);
  ~ImmOiAdminOperationResult();

  // Perform actual C IMM API to inform the IMM Service about the result of the
  // execution of an administrative operation requested by IMM Service
  bool InformResult(SaInvocationT invocation, SaAisErrorT ais_code);
  bool InformResult(SaInvocationT invocation, SaAisErrorT ais_code,
                    const std::string& message);

 private:
  SaImmOiHandleT oi_handle_;

  DELETE_COPY_AND_MOVE_OPERATORS(ImmOiAdminOperationResult);
};

}  // namespace immoi

#endif  // SRC_LOG_IMMWRP_IMMOI_OI_ADMIN_OPERATION_RESULT_H_
