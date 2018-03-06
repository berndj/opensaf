/*      -*- OpenSAF  -*-
 *
 * Copyright Ericsson AB 2018 - All Rights Reserved.
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
#ifndef NTF_APITEST_NTF_API_WITH_TRY_AGAIN_H_
#define NTF_APITEST_NTF_API_WITH_TRY_AGAIN_H_

#include "base/osaf_time.h"
#include "ais/try_again_decorator.h"

class NtfTest {
 public:
  static SaAisErrorT saNtfInitialize(
      SaNtfHandleT* handle,
      const SaNtfCallbacksT* cbs,
      SaVersionT* version) {
    return ais::make_decorator(::saNtfInitialize)(handle, cbs, version);
  }

  static SaAisErrorT saNtfFinalize(SaNtfHandleT handle) {
    return ais::make_decorator(::saNtfFinalize)(handle);
  }

  static SaAisErrorT saNtfDispatch(
      SaNtfHandleT handle, SaDispatchFlagsT flags) {
    return ais::make_decorator(::saNtfDispatch)(handle, flags);
  }

  static SaAisErrorT saNtfNotificationSend(
      SaNtfNotificationHandleT notificationHandle) {
    return ais::make_decorator(::saNtfNotificationSend)(notificationHandle);
  }

  static SaAisErrorT saNtfNotificationSubscribe(
      const SaNtfNotificationTypeFilterHandlesT *filter_handle,
      SaNtfSubscriptionIdT id) {
    return ais::make_decorator(::saNtfNotificationSubscribe)(filter_handle, id);
  }

  static SaAisErrorT saNtfNotificationUnsubscribe(SaNtfSubscriptionIdT id) {
    return ais::make_decorator(::saNtfNotificationUnsubscribe)(id);
  }

  static SaAisErrorT saNtfNotificationReadInitialize(
      SaNtfSearchCriteriaT search_criteria,
      const SaNtfNotificationTypeFilterHandlesT *filter_handle,
      SaNtfReadHandleT *read_handle) {
    return ais::make_decorator(::saNtfNotificationReadInitialize)(
        search_criteria, filter_handle, read_handle);
  }

  static SaAisErrorT saNtfNotificationReadFinalize(
      SaNtfReadHandleT read_handle) {
    return ais::make_decorator(::saNtfNotificationReadFinalize)(read_handle);
  }

  static SaAisErrorT saNtfNotificationReadNext(
      SaNtfReadHandleT read_handle,
      SaNtfSearchDirectionT search_direction,
      SaNtfNotificationsT *notification) {
    return ais::make_decorator(::saNtfNotificationReadNext)(
        read_handle, search_direction, notification);
  }
};

#endif  // NTF_APITEST_NTF_API_WITH_TRY_AGAIN_H_
