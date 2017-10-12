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

#ifndef SRC_LOG_IMMWRP_IMMOM_OM_CLASS_DELETE_H_
#define SRC_LOG_IMMWRP_IMMOM_OM_CLASS_DELETE_H_


#include <string>
#include "ais/include/saImmOm.h"
#include "experimental/immcpp/api/common/common.h"
#include "base/logtrace.h"

namespace immom {

//>
// Delete an IMM object class.
//
// Example:
// Delete object class that name is "classname"
//
// ImmOmClassDelete deleteclass{omhandle};
// if (deleteclass.DeleteClass(classname) == false) {
//   // Error handling...
//   return;
// }
//<
class ImmOmClassDelete : public ImmBase {
 public:
  explicit ImmOmClassDelete(const SaImmHandleT& om_handle);
  ~ImmOmClassDelete();

  // Returns false if Fail
  // Use ais_error() to get returned AIS code.
  bool DeleteClass(const std::string& class_name);

 private:
  SaImmHandleT om_handle_;

  DELETE_COPY_AND_MOVE_OPERATORS(ImmOmClassDelete);
};

}  // namespace immom

#endif  // SRC_LOG_IMMWRP_IMMOM_OM_CLASS_DELETE_H_
