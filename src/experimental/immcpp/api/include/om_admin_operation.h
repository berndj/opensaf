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

#ifndef SRC_LOG_IMMWRP_IMMOM_OM_ADMIN_OPERATION_H_
#define SRC_LOG_IMMWRP_IMMOM_OM_ADMIN_OPERATION_H_


#include <string>
#include <vector>

#include "ais/include/saImmOm.h"
#include "experimental/immcpp/api/common/common.h"
#include "experimental/immcpp/api/common/imm_attribute.h"
#include "base/logtrace.h"

namespace immom {
//>
// Manage (invoke, continue or clear) admin operation on given IMM object.
//
// Invoke administration operation requests the implementer of the object
// `object_name` perform an administrative operation characterized by
// operationId on that object. Administrative operations can be performed on
// configuration or runtime objects.
//
// The class provides help methods to setup admin operation including
// continuation id or operation id, or admin parameters, etc.
// and methods to perform admin operations (invoke/continue/clear).
//
// Example:
// ImmOmAdminOperation adminopobj{adminhandle, dn};
// adminopobj.SetContinuationId(cid);
// adminopobj.SetAdminOperationId(aid);
// adminopobj.SetTimeout(timeout);
//
// SaAisErrorT opcode;
// if (adminopobj.InvokeAdminOperation(&opcode) == false) {
//   // ERR handling
// }
//
// if (adminopobj.ContinueAdminOperation(&opcode) == false) {
//   // ERR handling
//}
//<
class ImmOmAdminOperation : public ImmBase {
 public:
  explicit ImmOmAdminOperation(const SaImmAdminOwnerHandleT& admin_owner_handle,
                               const std::string& object_name);
  ~ImmOmAdminOperation();

  // Help methods to setup continuation id, admin operation id, or timeout.
  // Default value of these ones are zero (0) if not set.
  ImmOmAdminOperation& SetContinuationId(SaImmContinuationIdT continuation_id);
  ImmOmAdminOperation& SetAdminOperationId(SaImmAdminOperationIdT admin_op_id);
  ImmOmAdminOperation& SetTimeout(SaTimeT timeout);

  // Setup admin operation parameters.
  // T must be AIS data types, or std::string
  // Example:
  // SaUint32T u32 = 1000;
  // SetParameter("name", &u32);
  template<typename T> ImmOmAdminOperation&
  SetParameter(const std::string& name, T* ptr_to_value);

  // Return true if the operation is successfully performed.
  // Invoke ais_error() to get returned AIS code.
  bool InvokeAdminOperation(SaAisErrorT* opcode);
  bool InvokeAdminOperationAsync(SaInvocationT invocation);
  bool ContinueAdminOperation(SaAisErrorT* opcode);
  bool ContinueAdminOperationAsync(SaInvocationT invocation);
  bool ClearAdminOperationContinuation();

 private:
  void FreeAllocatedMemory();

 private:
  std::string object_name_;
  SaImmAdminOwnerHandleT admin_owner_handle_;
  SaImmContinuationIdT continuation_id_;
  SaImmAdminOperationIdT admin_op_id_;
  SaTimeT timeout_;
  ListOfAttributePropertyPtr list_of_attribute_properties_;

  DELETE_COPY_AND_MOVE_OPERATORS(ImmOmAdminOperation);
};

inline ImmOmAdminOperation&
ImmOmAdminOperation::SetContinuationId(SaImmContinuationIdT continuation_id) {
  continuation_id_ = continuation_id;
  return *this;
}

inline ImmOmAdminOperation&
ImmOmAdminOperation::SetAdminOperationId(SaImmAdminOperationIdT admin_op_id) {
  admin_op_id_ = admin_op_id;
  return *this;
}

inline ImmOmAdminOperation&
ImmOmAdminOperation::SetTimeout(SaTimeT timeout) {
  timeout_ = timeout;
  return *this;
}

template<typename T> ImmOmAdminOperation&
ImmOmAdminOperation::SetParameter(const std::string& name, T* ptr_to_value) {
  TRACE_ENTER();
  // typename T must be one of AIS data types or std::string. Not support
  // SaInt8T, and SaInt16T types as no SaImmValueTypeT goes with them.
  SA_AIS_TYPE_CHECK(T);
  AttributeProperty* attribute = new AttributeProperty(name);
  assert(attribute != nullptr);
  attribute->set_value<T>(ptr_to_value);
  list_of_attribute_properties_.push_back(attribute);
  return *this;
}

}  // namespace immom

#endif  // SRC_LOG_IMMWRP_IMMOM_OM_ADMIN_OPERATION_H_
