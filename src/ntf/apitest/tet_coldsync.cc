/*      -*- OpenSAF  -*-
 *
 * (C) Copyright 2018 The OpenSAF Foundation
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

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <wait.h>
#include "osaf/apitest/utest.h"
#include "osaf/apitest/util.h"
#include "ntf/apitest/tet_ntf.h"
#include "ntf/apitest/tet_ntf_common.h"
#include "ntf/apitest/ntf_api_with_try_again.h"

extern int verbose;

void free_notif_2(SaNtfSubscriptionIdT subscriptionId,
    const SaNtfNotificationsT *notification) {
  SaNtfNotificationHandleT notificationHandle = 0;
  switch (notification->notificationType) {
  case SA_NTF_TYPE_OBJECT_CREATE_DELETE:
    notificationHandle =
        notification->notification.objectCreateDeleteNotification
      .notificationHandle;
    break;

  case SA_NTF_TYPE_ATTRIBUTE_CHANGE:
    notificationHandle =
        notification->notification.attributeChangeNotification
      .notificationHandle;
    break;

  case SA_NTF_TYPE_STATE_CHANGE:
    notificationHandle =
        notification->notification.stateChangeNotification
      .notificationHandle;
    break;

  case SA_NTF_TYPE_ALARM:
    notificationHandle = notification->notification
           .alarmNotification.notificationHandle;
    break;

  case SA_NTF_TYPE_SECURITY_ALARM:
    notificationHandle =
        notification->notification.securityAlarmNotification
      .notificationHandle;
    break;

  default:
    assert(0);
    break;
  }
  if (notificationHandle != 0) {
    safassert(saNtfNotificationFree(notificationHandle), SA_AIS_OK);
  }
}

/**
 * This test function is to verify the cached alarms to be
 * cold sync to standby.
 * Steps:
 * - Send alarms
 * - Reboot the standby, the alarms should be cold sync
 * - switch over to make the standby become active
 * - Read alarms, reader is successful to read alarms from the new active
 */
void test_coldsync_saNtfNotificationReadNext_01(void) {
  saNotificationAllocationParamsT myNotificationAllocationParams;
  saNotificationFilterAllocationParamsT
      myNotificationFilterAllocationParams;
  saNotificationParamsT myNotificationParams;

  SaNtfSearchCriteriaT searchCriteria = {SA_NTF_SEARCH_ONLY_FILTER, 0, 0};
  SaNtfAlarmNotificationFilterT myAlarmFilter;
  SaNtfNotificationTypeFilterHandlesT myNotificationFilterHandles = {
      0, 0, 0, 0, 0};
  SaNtfReadHandleT readHandle;
  SaNtfHandleT ntfHandle;
  SaNtfNotificationsT returnedNotification;
  SaNtfAlarmNotificationT myNotification;
  SaAisErrorT errorCode;
  SaUint32T readCounter = 0;

  fillInDefaultValues(&myNotificationAllocationParams,
          &myNotificationFilterAllocationParams,
          &myNotificationParams);

  safassert(NtfTest::saNtfInitialize(&ntfHandle, &ntfCallbacks, &ntfVersion),
      SA_AIS_OK);

  safassert(ntftest_saNtfAlarmNotificationFilterAllocate(
    ntfHandle, /* handle to Notification Service instance */
    &myAlarmFilter, /* put filter here */
    /* number of event types */
    myNotificationFilterAllocationParams.numEventTypes,
    /* number of notification objects */
    myNotificationFilterAllocationParams.numNotificationObjects,
    /* number of notifying objects */
    myNotificationFilterAllocationParams.numNotifyingObjects,
    /* number of notification class ids */
    myNotificationFilterAllocationParams.numNotificationClassIds,
    /* number of probable causes */
    myNotificationFilterAllocationParams.numProbableCauses,
    /* number of perceived severities */
    myNotificationFilterAllocationParams.numPerceivedSeverities,
    /* number of trend indications */
    myNotificationFilterAllocationParams.numTrends),
    SA_AIS_OK);

  myNotificationFilterHandles.alarmFilterHandle =
    myAlarmFilter.notificationFilterHandle;
  myAlarmFilter.perceivedSeverities[0] = SA_NTF_SEVERITY_WARNING;
  myAlarmFilter.perceivedSeverities[1] = SA_NTF_SEVERITY_CLEARED;

  /* Send one alarm notification */
  safassert(ntftest_saNtfAlarmNotificationAllocate(
    ntfHandle, /* handle to Notification Service instance */
    &myNotification,
    /* number of correlated notifications */
    myNotificationAllocationParams.numCorrelatedNotifications,
    /* length of additional text */
    myNotificationAllocationParams.lengthAdditionalText,
    /* number of additional info items*/
    myNotificationAllocationParams.numAdditionalInfo,
    /* number of specific problems */
    myNotificationAllocationParams.numSpecificProblems,
    /* number of monitored attributes */
    myNotificationAllocationParams.numMonitoredAttributes,
    /* number of proposed repair actions */
    myNotificationAllocationParams.numProposedRepairActions,
    /* use default allocation size */
    myNotificationAllocationParams.variableDataSize),
    SA_AIS_OK);

  myNotificationParams.eventType = myNotificationParams.alarmEventType;

  fill_header_part(
      &myNotification.notificationHeader,
      reinterpret_cast<saNotificationParamsT *>(&myNotificationParams),
      myNotificationAllocationParams.lengthAdditionalText);

  /* determine perceived severity */
  *(myNotification.perceivedSeverity) =
      myNotificationParams.perceivedSeverity;

  /* set probable cause*/
  *(myNotification.probableCause) = myNotificationParams.probableCause;

  safassert(NtfTest::saNtfNotificationSend(myNotification.notificationHandle),
            SA_AIS_OK);

  // reboot standby, switchover
  wait_controllers(3);
  wait_controllers(4);

  /* Read initialize here to get the notification above */
  safassert(NtfTest::saNtfNotificationReadInitialize(searchCriteria,
    &myNotificationFilterHandles, &readHandle),
    SA_AIS_OK);

  /* read as many matching notifications as exist for the time period
   between the last received one and now */
  for (; (errorCode = NtfTest::saNtfNotificationReadNext(
        readHandle, SA_NTF_SEARCH_YOUNGER,
        &returnedNotification)) == SA_AIS_OK;) {
    safassert(errorCode, SA_AIS_OK);
    readCounter++;

    if (verbose) {
      newNotification(69, &returnedNotification);
    } else {
      free_notif_2(0, &returnedNotification);
    }
  }
  if (verbose) {
    (void)printf("\n errorcode to break loop: %d\n",
                 static_cast<int>(errorCode));
  }
  if (readCounter == 0) {
    errorCode = SA_AIS_ERR_FAILED_OPERATION;
  }

  // No more...
  safassert(NtfTest::saNtfNotificationReadFinalize(readHandle), SA_AIS_OK);
  safassert(ntftest_saNtfNotificationFilterFree(
      myAlarmFilter.notificationFilterHandle), SA_AIS_OK);
  free(myNotificationParams.additionalText);
  safassert(ntftest_saNtfNotificationFree(myNotification.notificationHandle),
      SA_AIS_OK);
  safassert(NtfTest::saNtfFinalize(ntfHandle), SA_AIS_OK);
  test_validate(errorCode, SA_AIS_ERR_NOT_EXIST); /* read all notifications!! */
}
/**
 * This test function is to verify the reader Id to be
 * cold sync to standby.
 * Steps:
 * - Send alarms
 * - Call read initialize
 * - Reboot the standby, the alarms should be cold sync
 * - switch over to make the standby become active
 * - Read alarms, reader is successful to read alarms from the new active
 */
void test_coldsync_saNtfNotificationReadInitialize_01(void) {
  saNotificationAllocationParamsT myNotificationAllocationParams;
  saNotificationFilterAllocationParamsT
      myNotificationFilterAllocationParams;
  saNotificationParamsT myNotificationParams;

  SaNtfSearchCriteriaT searchCriteria = {SA_NTF_SEARCH_ONLY_FILTER, 0, 0};
  SaNtfAlarmNotificationFilterT myAlarmFilter;
  SaNtfNotificationTypeFilterHandlesT myNotificationFilterHandles = {
      0, 0, 0, 0, 0};
  SaNtfReadHandleT readHandle;
  SaNtfHandleT ntfHandle;
  SaNtfNotificationsT returnedNotification;
  SaNtfAlarmNotificationT myNotification;
  SaAisErrorT errorCode;
  SaUint32T readCounter = 0;

  fillInDefaultValues(&myNotificationAllocationParams,
          &myNotificationFilterAllocationParams,
          &myNotificationParams);

  safassert(NtfTest::saNtfInitialize(&ntfHandle, &ntfCallbacks, &ntfVersion),
      SA_AIS_OK);

  safassert(ntftest_saNtfAlarmNotificationFilterAllocate(
    ntfHandle, /* handle to Notification Service instance */
    &myAlarmFilter, /* put filter here */
    /* number of event types */
    myNotificationFilterAllocationParams.numEventTypes,
    /* number of notification objects */
    myNotificationFilterAllocationParams.numNotificationObjects,
    /* number of notifying objects */
    myNotificationFilterAllocationParams.numNotifyingObjects,
    /* number of notification class ids */
    myNotificationFilterAllocationParams.numNotificationClassIds,
    /* number of probable causes */
    myNotificationFilterAllocationParams.numProbableCauses,
    /* number of perceived severities */
    myNotificationFilterAllocationParams.numPerceivedSeverities,
    /* number of trend indications */
    myNotificationFilterAllocationParams.numTrends),
    SA_AIS_OK);

  myNotificationFilterHandles.alarmFilterHandle =
    myAlarmFilter.notificationFilterHandle;
  myAlarmFilter.perceivedSeverities[0] = SA_NTF_SEVERITY_WARNING;
  myAlarmFilter.perceivedSeverities[1] = SA_NTF_SEVERITY_CLEARED;

  /* Send one alarm notification */
  safassert(ntftest_saNtfAlarmNotificationAllocate(
    ntfHandle, /* handle to Notification Service instance */
    &myNotification,
    /* number of correlated notifications */
    myNotificationAllocationParams.numCorrelatedNotifications,
    /* length of additional text */
    myNotificationAllocationParams.lengthAdditionalText,
    /* number of additional info items*/
    myNotificationAllocationParams.numAdditionalInfo,
    /* number of specific problems */
    myNotificationAllocationParams.numSpecificProblems,
    /* number of monitored attributes */
    myNotificationAllocationParams.numMonitoredAttributes,
    /* number of proposed repair actions */
    myNotificationAllocationParams.numProposedRepairActions,
    /* use default allocation size */
    myNotificationAllocationParams.variableDataSize),
    SA_AIS_OK);

  myNotificationParams.eventType = myNotificationParams.alarmEventType;

  fill_header_part(
      &myNotification.notificationHeader,
      reinterpret_cast<saNotificationParamsT *>(&myNotificationParams),
      myNotificationAllocationParams.lengthAdditionalText);

  /* determine perceived severity */
  *(myNotification.perceivedSeverity) =
      myNotificationParams.perceivedSeverity;

  /* set probable cause*/
  *(myNotification.probableCause) = myNotificationParams.probableCause;

  safassert(NtfTest::saNtfNotificationSend(myNotification.notificationHandle),
            SA_AIS_OK);

  /* Read initialize here to get the notification above */
  safassert(NtfTest::saNtfNotificationReadInitialize(searchCriteria,
    &myNotificationFilterHandles, &readHandle),
    SA_AIS_OK);
  // reboot standby, switchover
  wait_controllers(3);
  wait_controllers(4);
  /* read as many matching notifications as exist for the time period
   between the last received one and now */
  for (; (errorCode = NtfTest::saNtfNotificationReadNext(
        readHandle, SA_NTF_SEARCH_YOUNGER,
        &returnedNotification)) == SA_AIS_OK;) {
    safassert(errorCode, SA_AIS_OK);
    readCounter++;

    if (verbose) {
      newNotification(69, &returnedNotification);
    } else {
      free_notif_2(0, &returnedNotification);
    }
  }
  if (verbose) {
    (void)printf("\n errorcode to break loop: %d\n",
                 static_cast<int>(errorCode));
  }
  if (readCounter == 0) {
    errorCode = SA_AIS_ERR_FAILED_OPERATION;
  }

  // No more...
  safassert(NtfTest::saNtfNotificationReadFinalize(readHandle), SA_AIS_OK);
  safassert(ntftest_saNtfNotificationFilterFree(
      myAlarmFilter.notificationFilterHandle),
    SA_AIS_OK);
  free(myNotificationParams.additionalText);
  safassert(ntftest_saNtfNotificationFree(myNotification.notificationHandle),
      SA_AIS_OK);
  safassert(NtfTest::saNtfFinalize(ntfHandle), SA_AIS_OK);
  test_validate(errorCode, SA_AIS_ERR_NOT_EXIST); /* read all notifications!! */
}
/**
 * This test function is to verify the reader Id, read iteration to be
 * cold sync to standby.
 * Steps:
 * - Send alarms
 * - Call read initialize
 * - Call read next
 * - Reboot the standby, the alarms should be cold sync
 * - switch over to make the standby become active
 * - Read alarms, reader is successful to read alarms from the new active
 */
void test_coldsync_saNtfNotificationReadNext_02(void) {
  saNotificationAllocationParamsT myNotificationAllocationParams;
  saNotificationFilterAllocationParamsT
      myNotificationFilterAllocationParams;
  saNotificationParamsT myNotificationParams;

  SaNtfSearchCriteriaT searchCriteria = {SA_NTF_SEARCH_ONLY_FILTER, 0, 0};
  SaNtfAlarmNotificationFilterT myAlarmFilter;
  SaNtfNotificationTypeFilterHandlesT myNotificationFilterHandles = {
      0, 0, 0, 0, 0};
  SaNtfReadHandleT readHandle;
  SaNtfHandleT ntfHandle;
  SaNtfNotificationsT returnedNotification;
  SaNtfAlarmNotificationT myNotification;
  SaAisErrorT errorCode;
  SaUint32T readCounter = 0;

  fillInDefaultValues(&myNotificationAllocationParams,
          &myNotificationFilterAllocationParams,
          &myNotificationParams);

  safassert(NtfTest::saNtfInitialize(&ntfHandle, &ntfCallbacks, &ntfVersion),
      SA_AIS_OK);

  safassert(ntftest_saNtfAlarmNotificationFilterAllocate(
    ntfHandle, /* handle to Notification Service instance */
    &myAlarmFilter, /* put filter here */
    /* number of event types */
    myNotificationFilterAllocationParams.numEventTypes,
    /* number of notification objects */
    myNotificationFilterAllocationParams.numNotificationObjects,
    /* number of notifying objects */
    myNotificationFilterAllocationParams.numNotifyingObjects,
    /* number of notification class ids */
    myNotificationFilterAllocationParams.numNotificationClassIds,
    /* number of probable causes */
    myNotificationFilterAllocationParams.numProbableCauses,
    /* number of perceived severities */
    myNotificationFilterAllocationParams.numPerceivedSeverities,
    /* number of trend indications */
    myNotificationFilterAllocationParams.numTrends),
    SA_AIS_OK);

  myNotificationFilterHandles.alarmFilterHandle =
    myAlarmFilter.notificationFilterHandle;
  myAlarmFilter.perceivedSeverities[0] = SA_NTF_SEVERITY_WARNING;
  myAlarmFilter.perceivedSeverities[1] = SA_NTF_SEVERITY_CLEARED;

  /* Send one alarm notification */
  safassert(ntftest_saNtfAlarmNotificationAllocate(
    ntfHandle, /* handle to Notification Service instance */
    &myNotification,
    /* number of correlated notifications */
    myNotificationAllocationParams.numCorrelatedNotifications,
    /* length of additional text */
    myNotificationAllocationParams.lengthAdditionalText,
    /* number of additional info items*/
    myNotificationAllocationParams.numAdditionalInfo,
    /* number of specific problems */
    myNotificationAllocationParams.numSpecificProblems,
    /* number of monitored attributes */
    myNotificationAllocationParams.numMonitoredAttributes,
    /* number of proposed repair actions */
    myNotificationAllocationParams.numProposedRepairActions,
    /* use default allocation size */
    myNotificationAllocationParams.variableDataSize),
    SA_AIS_OK);

  myNotificationParams.eventType = myNotificationParams.alarmEventType;

  fill_header_part(
      &myNotification.notificationHeader,
      reinterpret_cast<saNotificationParamsT *>(&myNotificationParams),
      myNotificationAllocationParams.lengthAdditionalText);

  /* determine perceived severity */
  *(myNotification.perceivedSeverity) =
      myNotificationParams.perceivedSeverity;

  /* set probable cause*/
  *(myNotification.probableCause) = myNotificationParams.probableCause;

  safassert(NtfTest::saNtfNotificationSend(myNotification.notificationHandle),
            SA_AIS_OK);
  safassert(NtfTest::saNtfNotificationSend(myNotification.notificationHandle),
            SA_AIS_OK);

  /* Read initialize here to get the notification above */
  safassert(NtfTest::saNtfNotificationReadInitialize(searchCriteria,
    &myNotificationFilterHandles, &readHandle),
    SA_AIS_OK);

  /* read as many matching notifications as exist for the time period
   between the last received one and now */
  for (; (errorCode = NtfTest::saNtfNotificationReadNext(
        readHandle, SA_NTF_SEARCH_YOUNGER,
        &returnedNotification)) == SA_AIS_OK;) {
    safassert(errorCode, SA_AIS_OK);
    if (readCounter == 0) {
      // reboot standby, switchover
      wait_controllers(3);
      wait_controllers(4);
    }
    readCounter++;

    if (verbose) {
      newNotification(69, &returnedNotification);
    } else {
      free_notif_2(0, &returnedNotification);
    }
  }
  if (verbose) {
    (void)printf("\n errorcode to break loop: %d\n",
                 static_cast<int>(errorCode));
  }
  if (readCounter == 0) {
    errorCode = SA_AIS_ERR_FAILED_OPERATION;
  }

  // No more...
  safassert(NtfTest::saNtfNotificationReadFinalize(readHandle), SA_AIS_OK);
  safassert(ntftest_saNtfNotificationFilterFree(
      myAlarmFilter.notificationFilterHandle), SA_AIS_OK);
  free(myNotificationParams.additionalText);
  safassert(ntftest_saNtfNotificationFree(myNotification.notificationHandle),
      SA_AIS_OK);
  safassert(NtfTest::saNtfFinalize(ntfHandle), SA_AIS_OK);
  test_validate(errorCode, SA_AIS_ERR_NOT_EXIST); /* read all notifications!! */
}

/**
 * This test function is to verify the ReaderInitialize_2 to be checkpointed
 * to the standby.
 * Steps:
 * - Send alarms
 * - ReadInitialize,
 * - switch over to make the standby become active
 * - ReadNext
 */
void test_async_saNtfNotificationReadInitialize_01(void) {
  saNotificationAllocationParamsT myNotificationAllocationParams;
  saNotificationFilterAllocationParamsT
      myNotificationFilterAllocationParams;
  saNotificationParamsT myNotificationParams;

  SaNtfSearchCriteriaT searchCriteria = {SA_NTF_SEARCH_ONLY_FILTER, 0, 0};
  SaNtfAlarmNotificationFilterT myAlarmFilter;
  SaNtfNotificationTypeFilterHandlesT myNotificationFilterHandles = {
      0, 0, 0, 0, 0};
  SaNtfReadHandleT readHandle;
  SaNtfHandleT ntfHandle;
  SaNtfNotificationsT returnedNotification;
  SaNtfAlarmNotificationT myNotification;
  SaAisErrorT errorCode;
  SaUint32T readCounter = 0;

  fillInDefaultValues(&myNotificationAllocationParams,
          &myNotificationFilterAllocationParams,
          &myNotificationParams);

  safassert(NtfTest::saNtfInitialize(&ntfHandle, &ntfCallbacks, &ntfVersion),
            SA_AIS_OK);

  safassert(ntftest_saNtfAlarmNotificationFilterAllocate(
    ntfHandle, /* handle to Notification Service instance */
    &myAlarmFilter, /* put filter here */
    /* number of event types */
    myNotificationFilterAllocationParams.numEventTypes,
    /* number of notification objects */
    myNotificationFilterAllocationParams.numNotificationObjects,
    /* number of notifying objects */
    myNotificationFilterAllocationParams.numNotifyingObjects,
    /* number of notification class ids */
    myNotificationFilterAllocationParams.numNotificationClassIds,
    /* number of probable causes */
    myNotificationFilterAllocationParams.numProbableCauses,
    /* number of perceived severities */
    myNotificationFilterAllocationParams.numPerceivedSeverities,
    /* number of trend indications */
    myNotificationFilterAllocationParams.numTrends),
    SA_AIS_OK);

  myNotificationFilterHandles.alarmFilterHandle =
    myAlarmFilter.notificationFilterHandle;
  myAlarmFilter.perceivedSeverities[0] = SA_NTF_SEVERITY_WARNING;
  myAlarmFilter.perceivedSeverities[1] = SA_NTF_SEVERITY_CLEARED;

  /* Send one alarm notification */
  safassert(ntftest_saNtfAlarmNotificationAllocate(
    ntfHandle, /* handle to Notification Service instance */
    &myNotification,
    /* number of correlated notifications */
    myNotificationAllocationParams.numCorrelatedNotifications,
    /* length of additional text */
    myNotificationAllocationParams.lengthAdditionalText,
    /* number of additional info items*/
    myNotificationAllocationParams.numAdditionalInfo,
    /* number of specific problems */
    myNotificationAllocationParams.numSpecificProblems,
    /* number of monitored attributes */
    myNotificationAllocationParams.numMonitoredAttributes,
    /* number of proposed repair actions */
    myNotificationAllocationParams.numProposedRepairActions,
    /* use default allocation size */
    myNotificationAllocationParams.variableDataSize),
    SA_AIS_OK);

  myNotificationParams.eventType = myNotificationParams.alarmEventType;

  fill_header_part(
      &myNotification.notificationHeader,
      reinterpret_cast<saNotificationParamsT *>(&myNotificationParams),
      myNotificationAllocationParams.lengthAdditionalText);

  /* determine perceived severity */
  *(myNotification.perceivedSeverity) =
      myNotificationParams.perceivedSeverity;

  /* set probable cause*/
  *(myNotification.probableCause) = myNotificationParams.probableCause;

  safassert(NtfTest::saNtfNotificationSend(myNotification.notificationHandle),
            SA_AIS_OK);

  /* Read initialize here to get the notification above */
  safassert(NtfTest::saNtfNotificationReadInitialize(searchCriteria,
    &myNotificationFilterHandles, &readHandle),
    SA_AIS_OK);
  wait_controllers(4);
  /* read as many matching notifications as exist for the time period
   between the last received one and now */
  for (; (errorCode = NtfTest::saNtfNotificationReadNext(
        readHandle, SA_NTF_SEARCH_YOUNGER,
        &returnedNotification)) == SA_AIS_OK;) {
    safassert(errorCode, SA_AIS_OK);
    readCounter++;

    if (verbose) {
      newNotification(69, &returnedNotification);
    } else {
      free_notif_2(0, &returnedNotification);
    }
  }
  if (verbose) {
    (void)printf("\n errorcode to break loop: %d\n",
                 static_cast<int>(errorCode));
  }
  if (readCounter == 0) {
    errorCode = SA_AIS_ERR_FAILED_OPERATION;
  }

  // No more...
  safassert(NtfTest::saNtfNotificationReadFinalize(readHandle), SA_AIS_OK);
  safassert(ntftest_saNtfNotificationFilterFree(
    myAlarmFilter.notificationFilterHandle), SA_AIS_OK);
  free(myNotificationParams.additionalText);
  safassert(ntftest_saNtfNotificationFree(myNotification.notificationHandle),
      SA_AIS_OK);
  safassert(NtfTest::saNtfFinalize(ntfHandle), SA_AIS_OK);
  test_validate(errorCode, SA_AIS_ERR_NOT_EXIST);
}
/**
 * This test function is to verify the reader iteration to be checkpointed
 * to the standby.
 * Steps:
 * - Send alarms
 * - ReadInitialize, ReadNext
 * - switch over to make the standby become active
 * - ReadNext
 */
void test_async_saNtfNotificationReadNext_01(void) {
  saNotificationAllocationParamsT myNotificationAllocationParams;
  saNotificationFilterAllocationParamsT
      myNotificationFilterAllocationParams;
  saNotificationParamsT myNotificationParams;

  SaNtfSearchCriteriaT searchCriteria = {SA_NTF_SEARCH_ONLY_FILTER, 0, 0};
  SaNtfAlarmNotificationFilterT myAlarmFilter;
  SaNtfNotificationTypeFilterHandlesT myNotificationFilterHandles = {
      0, 0, 0, 0, 0};
  SaNtfReadHandleT readHandle;
  SaNtfHandleT ntfHandle;
  SaNtfNotificationsT returnedNotification;
  SaNtfAlarmNotificationT myNotification;
  SaAisErrorT errorCode;
  SaUint32T readCounter = 0;

  fillInDefaultValues(&myNotificationAllocationParams,
          &myNotificationFilterAllocationParams,
          &myNotificationParams);

  safassert(NtfTest::saNtfInitialize(&ntfHandle, &ntfCallbacks, &ntfVersion),
            SA_AIS_OK);

  safassert(ntftest_saNtfAlarmNotificationFilterAllocate(
    ntfHandle, /* handle to Notification Service instance */
    &myAlarmFilter, /* put filter here */
    /* number of event types */
    myNotificationFilterAllocationParams.numEventTypes,
    /* number of notification objects */
    myNotificationFilterAllocationParams.numNotificationObjects,
    /* number of notifying objects */
    myNotificationFilterAllocationParams.numNotifyingObjects,
    /* number of notification class ids */
    myNotificationFilterAllocationParams.numNotificationClassIds,
    /* number of probable causes */
    myNotificationFilterAllocationParams.numProbableCauses,
    /* number of perceived severities */
    myNotificationFilterAllocationParams.numPerceivedSeverities,
    /* number of trend indications */
    myNotificationFilterAllocationParams.numTrends),
    SA_AIS_OK);

  myNotificationFilterHandles.alarmFilterHandle =
    myAlarmFilter.notificationFilterHandle;
  myAlarmFilter.perceivedSeverities[0] = SA_NTF_SEVERITY_WARNING;
  myAlarmFilter.perceivedSeverities[1] = SA_NTF_SEVERITY_CLEARED;

  /* Send one alarm notification */
  safassert(ntftest_saNtfAlarmNotificationAllocate(
    ntfHandle, /* handle to Notification Service instance */
    &myNotification,
    /* number of correlated notifications */
    myNotificationAllocationParams.numCorrelatedNotifications,
    /* length of additional text */
    myNotificationAllocationParams.lengthAdditionalText,
    /* number of additional info items*/
    myNotificationAllocationParams.numAdditionalInfo,
    /* number of specific problems */
    myNotificationAllocationParams.numSpecificProblems,
    /* number of monitored attributes */
    myNotificationAllocationParams.numMonitoredAttributes,
    /* number of proposed repair actions */
    myNotificationAllocationParams.numProposedRepairActions,
    /* use default allocation size */
    myNotificationAllocationParams.variableDataSize),
    SA_AIS_OK);

  myNotificationParams.eventType = myNotificationParams.alarmEventType;

  fill_header_part(
      &myNotification.notificationHeader,
      reinterpret_cast<saNotificationParamsT *>(&myNotificationParams),
      myNotificationAllocationParams.lengthAdditionalText);

  /* determine perceived severity */
  *(myNotification.perceivedSeverity) =
      myNotificationParams.perceivedSeverity;

  /* set probable cause*/
  *(myNotification.probableCause) = myNotificationParams.probableCause;

  safassert(NtfTest::saNtfNotificationSend(myNotification.notificationHandle),
            SA_AIS_OK);

  safassert(NtfTest::saNtfNotificationSend(myNotification.notificationHandle),
            SA_AIS_OK);

  /* Read initialize here to get the notification above */
  safassert(NtfTest::saNtfNotificationReadInitialize(searchCriteria,
    &myNotificationFilterHandles, &readHandle),
    SA_AIS_OK);
  /* read as many matching notifications as exist for the time period
   between the last received one and now */
  for (; (errorCode = NtfTest::saNtfNotificationReadNext(
        readHandle, SA_NTF_SEARCH_YOUNGER,
        &returnedNotification)) == SA_AIS_OK;) {
    safassert(errorCode, SA_AIS_OK);
    if (readCounter == 0)
      wait_controllers(4);

    readCounter++;

    if (verbose) {
      newNotification(69, &returnedNotification);
    } else {
      free_notif_2(0, &returnedNotification);
    }
  }
  if (verbose) {
    (void)printf("\n errorcode to break loop: %d\n",
                 static_cast<int>(errorCode));
  }
  if (readCounter == 0) {
    errorCode = SA_AIS_ERR_FAILED_OPERATION;
  }

  // No more...
  safassert(NtfTest::saNtfNotificationReadFinalize(readHandle), SA_AIS_OK);
  safassert(ntftest_saNtfNotificationFilterFree(
    myAlarmFilter.notificationFilterHandle), SA_AIS_OK);
  free(myNotificationParams.additionalText);
  safassert(ntftest_saNtfNotificationFree(myNotification.notificationHandle),
      SA_AIS_OK);
  safassert(NtfTest::saNtfFinalize(ntfHandle), SA_AIS_OK);
  test_validate(errorCode, SA_AIS_ERR_NOT_EXIST);
}
/**
 * This test function is to verify the reader finalize to be checkpointed
 * to the standby.
 * Steps:
 * - Send alarms
 * - ReadInitialize, ReadNext, ReadFinalize
 * - switch over to make the standby become active
 */
void test_async_saNtfNotificationReadFinalize_01(void) {
  saNotificationAllocationParamsT myNotificationAllocationParams;
  saNotificationFilterAllocationParamsT
      myNotificationFilterAllocationParams;
  saNotificationParamsT myNotificationParams;

  SaNtfSearchCriteriaT searchCriteria = {SA_NTF_SEARCH_ONLY_FILTER, 0, 0};
  SaNtfAlarmNotificationFilterT myAlarmFilter;
  SaNtfNotificationTypeFilterHandlesT myNotificationFilterHandles = {
      0, 0, 0, 0, 0};
  SaNtfReadHandleT readHandle;
  SaNtfHandleT ntfHandle;
  SaNtfNotificationsT returnedNotification;
  SaNtfAlarmNotificationT myNotification;
  searchCriteria.searchMode = SA_NTF_SEARCH_ONLY_FILTER;
  SaAisErrorT errorCode;
  SaUint32T readCounter = 0;

  fillInDefaultValues(&myNotificationAllocationParams,
          &myNotificationFilterAllocationParams,
          &myNotificationParams);

  safassert(NtfTest::saNtfInitialize(&ntfHandle, &ntfCallbacks, &ntfVersion),
            SA_AIS_OK);

  safassert(ntftest_saNtfAlarmNotificationFilterAllocate(
    ntfHandle, /* handle to Notification Service instance */
    &myAlarmFilter, /* put filter here */
    /* number of event types */
    myNotificationFilterAllocationParams.numEventTypes,
    /* number of notification objects */
    myNotificationFilterAllocationParams.numNotificationObjects,
    /* number of notifying objects */
    myNotificationFilterAllocationParams.numNotifyingObjects,
    /* number of notification class ids */
    myNotificationFilterAllocationParams.numNotificationClassIds,
    /* number of probable causes */
    myNotificationFilterAllocationParams.numProbableCauses,
    /* number of perceived severities */
    myNotificationFilterAllocationParams.numPerceivedSeverities,
    /* number of trend indications */
    myNotificationFilterAllocationParams.numTrends),
    SA_AIS_OK);

  myNotificationFilterHandles.alarmFilterHandle =
    myAlarmFilter.notificationFilterHandle;
  myAlarmFilter.perceivedSeverities[0] = SA_NTF_SEVERITY_WARNING;
  myAlarmFilter.perceivedSeverities[1] = SA_NTF_SEVERITY_CLEARED;

  /* Send one alarm notification */
  safassert(ntftest_saNtfAlarmNotificationAllocate(
    ntfHandle, /* handle to Notification Service instance */
    &myNotification,
    /* number of correlated notifications */
    myNotificationAllocationParams.numCorrelatedNotifications,
    /* length of additional text */
    myNotificationAllocationParams.lengthAdditionalText,
    /* number of additional info items*/
    myNotificationAllocationParams.numAdditionalInfo,
    /* number of specific problems */
    myNotificationAllocationParams.numSpecificProblems,
    /* number of monitored attributes */
    myNotificationAllocationParams.numMonitoredAttributes,
    /* number of proposed repair actions */
    myNotificationAllocationParams.numProposedRepairActions,
    /* use default allocation size */
    myNotificationAllocationParams.variableDataSize),
    SA_AIS_OK);

  myNotificationParams.eventType = myNotificationParams.alarmEventType;

  fill_header_part(
      &myNotification.notificationHeader,
      reinterpret_cast<saNotificationParamsT *>(&myNotificationParams),
      myNotificationAllocationParams.lengthAdditionalText);

  /* determine perceived severity */
  *(myNotification.perceivedSeverity) =
      myNotificationParams.perceivedSeverity;

  /* set probable cause*/
  *(myNotification.probableCause) = myNotificationParams.probableCause;

  safassert(NtfTest::saNtfNotificationSend(myNotification.notificationHandle),
            SA_AIS_OK);

  /* Read initialize here to get the notification above */
  safassert(NtfTest::saNtfNotificationReadInitialize(searchCriteria,
    &myNotificationFilterHandles, &readHandle),
    SA_AIS_OK);
  /* read as many matching notifications as exist for the time period
   between the last received one and now */
  for (; (errorCode = NtfTest::saNtfNotificationReadNext(
        readHandle, SA_NTF_SEARCH_YOUNGER,
        &returnedNotification)) == SA_AIS_OK;) {
    safassert(errorCode, SA_AIS_OK);
    readCounter++;

    if (verbose) {
      newNotification(69, &returnedNotification);
    } else {
      free_notif_2(0, &returnedNotification);
    }
  }
  if (verbose) {
    (void)printf("\n errorcode to break loop: %d\n",
                 static_cast<int>(errorCode));
  }
  if (readCounter == 0) {
    errorCode = SA_AIS_ERR_FAILED_OPERATION;
  }

  // No more...
  safassert(NtfTest::saNtfNotificationReadFinalize(readHandle), SA_AIS_OK);
  wait_controllers(4);
  safassert(ntftest_saNtfNotificationFilterFree(
    myAlarmFilter.notificationFilterHandle), SA_AIS_OK);
  free(myNotificationParams.additionalText);
  safassert(ntftest_saNtfNotificationFree(myNotification.notificationHandle),
      SA_AIS_OK);
  safassert(NtfTest::saNtfFinalize(ntfHandle), SA_AIS_OK);
  test_validate(errorCode, SA_AIS_ERR_NOT_EXIST);
}

void add_coldsync_test(void) {
  install_sigusr2();

  test_suite_add(41, "Synchronization - Reader");
  test_case_add(41, test_coldsync_saNtfNotificationReadNext_01,
      "saNtfNotificationReadNext: Test cold sync of cached alarm");
  test_case_add(41, test_coldsync_saNtfNotificationReadInitialize_01,
      "saNtfNotificationReadNext: Test cold sync of reader Id, reader filter");
  test_case_add(41, test_coldsync_saNtfNotificationReadNext_02,
      "saNtfNotificationReadNext: Test cold sync of current reader iteration");
  test_case_add(41, test_async_saNtfNotificationReadInitialize_01,
      "saNtfNotificationReadInitialize: "
      "Test reader Id, reader filter to be checkpoint");
  test_case_add(41, test_async_saNtfNotificationReadNext_01,
      "saNtfNotificationReadNext: "
      "Test current reader iteration to be checkpoint");
  test_case_add(41, test_async_saNtfNotificationReadFinalize_01,
      "saNtfNotificationReadFinalize: Test reader finalize to be checkpoint");
}
