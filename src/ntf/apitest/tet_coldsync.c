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
#include "tet_ntf.h"
#include "tet_ntf_common.h"

extern int verbose;

void free_notif_2(SaNtfSubscriptionIdT subscriptionId,
		const SaNtfNotificationsT *notification)
{
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
void test_coldsync_saNtfNotificationReadNext_01(void)
{
	saNotificationAllocationParamsT myNotificationAllocationParams;
	saNotificationFilterAllocationParamsT
			myNotificationFilterAllocationParams;
	saNotificationParamsT myNotificationParams;

	SaNtfSearchCriteriaT searchCriteria;
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

	safassert(ntftest_saNtfInitialize(&ntfHandle, &ntfCallbacks, &ntfVersion),
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

	fill_header_part(&myNotification.notificationHeader,
			 (saNotificationParamsT *)&myNotificationParams,
			 myNotificationAllocationParams.lengthAdditionalText);

	/* determine perceived severity */
	*(myNotification.perceivedSeverity) =
			myNotificationParams.perceivedSeverity;

	/* set probable cause*/
	*(myNotification.probableCause) = myNotificationParams.probableCause;

	safassert(ntftest_saNtfNotificationSend(myNotification.notificationHandle),
			SA_AIS_OK);

	// reboot standby, switchover
	wait_controllers(3);
	wait_controllers(4);

	/* Read initialize here to get the notification above */
	safassert(ntftest_saNtfNotificationReadInitialize(searchCriteria,
		&myNotificationFilterHandles, &readHandle),
		SA_AIS_OK);

	/* read as many matching notifications as exist for the time period
	 between the last received one and now */
	for (; (errorCode = ntftest_saNtfNotificationReadNext(
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
		(void)printf("\n errorcode to break loop: %d\n", (int)errorCode);
	}
	if (readCounter == 0) {
		errorCode = SA_AIS_ERR_FAILED_OPERATION;
	}

	// No more...
	safassert(ntftest_saNtfNotificationReadFinalize(readHandle), SA_AIS_OK);
	safassert(ntftest_saNtfNotificationFilterFree(myAlarmFilter.notificationFilterHandle),
		SA_AIS_OK);
	free(myNotificationParams.additionalText);
	safassert(ntftest_saNtfNotificationFree(myNotification.notificationHandle),
			SA_AIS_OK);
	safassert(saNtfFinalize(ntfHandle), SA_AIS_OK);
	test_validate(errorCode, SA_AIS_ERR_NOT_EXIST); /* read all notifications!! */
}

void add_coldsync_test(void)
{
	install_sigusr2();

	test_suite_add(41, "Cold sync - Reader");
	test_case_add(41, test_coldsync_saNtfNotificationReadNext_01,
	    "saNtfNotificationReadNext after rebooting standby SC and switch over");
}
