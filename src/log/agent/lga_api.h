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

#ifndef SRC_LOG_AGENT_LGA_API_H_
#define SRC_LOG_AGENT_LGA_API_H_

#include "osaf/saf/saAis.h"
#include "log/agent/lga.h"

class LogAgent {
 public:
  static SaAisErrorT saLogInitialize(
      SaLogHandleT*,
      const SaLogCallbacksT*,
      SaVersionT*);

  static SaAisErrorT saLogSelectionObjectGet(
      SaLogHandleT,
      SaSelectionObjectT*);

  static SaAisErrorT saLogStreamOpen_2(
      SaLogHandleT,
      const SaNameT*,
      const SaLogFileCreateAttributesT_2*,
      SaLogStreamOpenFlagsT,
      SaTimeT,
      SaLogStreamHandleT*);

  static SaAisErrorT saLogStreamOpenAsync_2(
      SaLogHandleT,
      const SaNameT*,
      const SaLogFileCreateAttributesT_2*,
      SaLogStreamOpenFlagsT, SaInvocationT);

  static SaAisErrorT saLogWriteLog(
      SaLogStreamHandleT,
      SaTimeT,
      const SaLogRecordT*);

  static SaAisErrorT saLogWriteLogAsync(
      SaLogStreamHandleT,
      SaInvocationT,
      SaLogAckFlagsT,
      const SaLogRecordT*);

  static SaAisErrorT saLogLimitGet(
      SaLogHandleT,
      SaLogLimitIdT,
      SaLimitValueT*);

  static SaAisErrorT saLogStreamClose(SaLogStreamHandleT);
  static SaAisErrorT saLogDispatch(SaLogHandleT, SaDispatchFlagsT);
  static SaAisErrorT saLogFinalize(SaLogHandleT);
};

#endif  // SRC_LOG_AGENT_LGA_API_H_
