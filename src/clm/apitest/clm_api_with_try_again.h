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
 */

#ifndef CLM_APITEST_CLM_API_WITH_TRY_AGAIN_H_
#define CLM_APITEST_CLM_API_WITH_TRY_AGAIN_H_

#include <saClm.h>
#include "ais/try_again_decorator.h"

class ClmTest {
 public:
  static SaAisErrorT saClmInitialize(SaClmHandleT* handle,
                                     const SaClmCallbacksT* cbs,
                                     SaVersionT* version) {
    return ais::make_decorator(::saClmInitialize)(handle, cbs, version);
  }

  static SaAisErrorT saClmInitialize_4(SaClmHandleT* handle,
                                       const SaClmCallbacksT_4* cbs,
                                       SaVersionT* version) {
    return ais::make_decorator(::saClmInitialize_4)(handle, cbs, version);
  }

  static SaAisErrorT saClmSelectionObjectGet(SaClmHandleT handle,
                                             SaSelectionObjectT* fd) {
    return ais::make_decorator(::saClmSelectionObjectGet)(handle, fd);
  }

  static SaAisErrorT saClmDispatch(SaClmHandleT handle,
                                   SaDispatchFlagsT flags) {
    return ais::make_decorator(::saClmDispatch)(handle, flags);
  }

  static SaAisErrorT saClmFinalize(SaClmHandleT handle) {
    return ais::make_decorator(::saClmFinalize)(handle);
  }

  static SaAisErrorT saClmClusterTrack(
      SaClmHandleT handle, SaUint8T trackFlags,
      SaClmClusterNotificationBufferT* buffer) {
    return ais::make_decorator(::saClmClusterTrack)(handle, trackFlags, buffer);
  }

  static SaAisErrorT saClmClusterNodeGet(SaClmHandleT handle,
                                         SaClmNodeIdT nodeId, SaTimeT timeout,
                                         SaClmClusterNodeT* clusterNode) {
    return ais::make_decorator(::saClmClusterNodeGet)(handle, nodeId, timeout,
                                                      clusterNode);
  }

  static SaAisErrorT saClmClusterTrack_4(
      SaClmHandleT handle, SaUint8T trackFlags,
      SaClmClusterNotificationBufferT_4* buffer) {
    return ais::make_decorator(::saClmClusterTrack_4)(handle, trackFlags,
                                                      buffer);
  }

  static SaAisErrorT saClmClusterTrackStop(SaClmHandleT handle) {
    return ais::make_decorator(::saClmClusterTrackStop)(handle);
  }

  static SaAisErrorT saClmClusterNotificationFree_4(
      SaClmHandleT handle, SaClmClusterNotificationT_4* notification) {
    return ais::make_decorator(::saClmClusterNotificationFree_4)(handle,
                                                                 notification);
  }

  static SaAisErrorT saClmClusterNodeGet_4(SaClmHandleT handle,
                                           SaClmNodeIdT nodeId, SaTimeT timeout,
                                           SaClmClusterNodeT_4* clusterNode) {
    return ais::make_decorator(::saClmClusterNodeGet_4)(handle, nodeId, timeout,
                                                        clusterNode);
  }

  static SaAisErrorT saClmClusterNodeGetAsync(SaClmHandleT handle,
                                              SaInvocationT invocation,
                                              SaClmNodeIdT nodeId) {
    return ais::make_decorator(::saClmClusterNodeGetAsync)(handle, invocation,
                                                           nodeId);
  }

  static SaAisErrorT saClmResponse_4(SaClmHandleT handle,
                                     SaInvocationT invocation,
                                     SaClmResponseT response) {
    return ais::make_decorator(::saClmResponse_4)(handle, invocation, response);
  }
};

#endif  // CLM_APITEST_CLM_API_WITH_TRY_AGAIN_H_
