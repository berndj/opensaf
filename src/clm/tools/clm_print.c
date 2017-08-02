/*      -*- OpenSAF  -*-
 *
 * Copyright (C) 2017, Oracle and/or its affiliates. All rights reserved.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <poll.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdint.h>

#include <base/saf_error.h>
#include <saAis.h>
#include <saClm.h>

#define SIZE_NOTIFICATIONS 100
#define TIME_OUT          ((SaTimeT)15 * SA_TIME_ONE_SECOND)
#define INVOCATION_ID  1111

static SaClmHandleT clm_handle;
static SaSelectionObjectT clm_selobject;
struct pollfd clm_fds[1];
static SaVersionT clm_version = { 'B', 4, 1 };
static bool is_cbk_received;
static int timeout = -1;
char mychar = '\0';


static char *clm_change[] =  {
	"INVALID_CHANGE",
	"SA_CLM_NODE_NO_CHANGE",
	"SA_CLM_NODE_JOINED",
	"SA_CLM_NODE_LEFT",
	"SA_CLM_NODE_RECONFIGURED",
};

static char *clm_step[] = {
	"INVALID_STEP",
	"SA_CLM_CHANGE_VALIDATE",
	"SA_CLM_CHANGE_START",
	"SA_CLM_CHANGE_ABORTED",
	"SA_CLM_CHANGE_COMPLETED",
};

static void print_node(const SaClmClusterNodeT_4 *clusterNode)
{
	printf("====Node Info for nodeId: %u========================>\n",
			clusterNode->nodeId);
	printf("  Node Id                    : %u(%x)\n",
			clusterNode->nodeId, clusterNode->nodeId);
	printf("  Address Family             : %s\n",
			clusterNode->nodeAddress.family == 1
			? "SA_CLM_AF_INET" : "SA_CLM_AF_INET6");
	printf("  Address value              : '%s'\n",
			clusterNode->nodeAddress.value);
	printf("  Address length             : '%d'\n",
			clusterNode->nodeAddress.length);
	printf("  CLM Node Name value        : '%s'\n",
			saAisNameBorrow(&clusterNode->nodeName));
	printf("  CLM Node Name length       : '%zu'\n",
			strlen(saAisNameBorrow(&clusterNode->nodeName)));
	printf("  EE Name value              : '%s'\n",
			saAisNameBorrow(&clusterNode->executionEnvironment));
	printf("  EE Name length             : '%zu'\n",
		strlen(saAisNameBorrow(&clusterNode->executionEnvironment)));
	printf("  Member                     : %s\n",
			clusterNode->member == SA_TRUE ? "TRUE":"FALSE");
	printf("  BootTimestamp              : %llu nanoseconds\n",
			clusterNode->bootTimestamp);
	printf("  Initial View Number        : %llu\n",
			clusterNode->initialViewNumber);
}

static void  clm_node_get_callback(SaInvocationT invocation,
		const SaClmClusterNodeT_4 *clusterNode,
		SaAisErrorT error)
{
	is_cbk_received = true;
	printf("\n===============CLM NODE GET CALLBACK STARTS==========\n");
	printf("Error: %s\n", saf_error(error));
	printf("Invocation: %llu\n", invocation);
	if (invocation !=  INVOCATION_ID) {
		fprintf(stderr,
			"error - InvocationId wrong,expected: %u, received: %llu\n",
			INVOCATION_ID,
			invocation);
	}
	if (error == SA_AIS_OK)
		print_node(clusterNode);
	printf("===============CLM NODE GET CALLBACK ENDS==========\n");
}


static void clm_track_callback(const SaClmClusterNotificationBufferT_4
		*notificationBuffer,
		SaUint32T numberOfMembers,
		SaInvocationT invocation,
		const SaNameT *rootCauseEntity,
		const SaNtfCorrelationIdsT *correlationIds,
		SaClmChangeStepT step,
		SaTimeT timeSupervision,
		SaAisErrorT error)
{
	int i;
	SaAisErrorT rc = 0;

	is_cbk_received = true;

	printf("\n===============CLM TRACK CALLBACK STARTS==========\n");
	printf("Error: %d\n", error);
	printf("Invocation: %llu\n", invocation);
	printf("Step: %s\n", clm_step[step]);
	printf("viewNumber:%llu\n", notificationBuffer->viewNumber);
	printf("numberOfItems: %d\n", notificationBuffer->numberOfItems);
	printf("numberOfMembers:%d\n\n", numberOfMembers);

	for (i = 0; i < notificationBuffer->numberOfItems; i++) {
		print_node(&notificationBuffer->notification[i].clusterNode);
		printf("  Change                     : %s\n",
		clm_change[notificationBuffer->notification[i].clusterChange]);
		printf("<================================================\n\n");
	}

	if ((step == SA_CLM_CHANGE_VALIDATE) || (step == SA_CLM_CHANGE_START)) {
		rc = saClmResponse_4(clm_handle, invocation,
				SA_CLM_CALLBACK_RESPONSE_OK);
		if (rc != SA_AIS_OK) {
			fprintf(stderr,
			"error - saClmResponse_4 failed with rc = %d\n", rc);
			saClmFinalize(clm_handle);
		}
	}
	printf("===============CLM TRACK CALLBACK ENDS==========\n");
}

static void usage(char *progname)
{
	printf("\nNAME\n");
	printf("\t%s\n", progname);

	printf("\nSYNOPSIS\n");
	printf("\t%s [options]\n", progname);

	printf("\nDESCRIPTION\n");
	printf("\t%s - calls CLM APIs and prints result.\n", progname);
	printf("\nOPTIONS\n");
	printf("\t-n or --nodeget       for saClmClusterNodeGet_4(), also takes nodeid\n");
	printf("\t                         as optional argument (default SA_CLM_LOCAL_NODE_ID)\n");
	printf("\t-a or --asyncget      for saClmClusterNodeGetAsync(), also takes nodeid\n");
	printf("\t                         as optional argument (default SA_CLM_LOCAL_NODE_ID)\n");
	printf("\t-m or --track         for saClmClusterTrack_4() using callback, also takes\n");
	printf("\t                         track flags as optional argument. Regarding track\n");
	printf("\t                         flags use, \",\" for Oring like \"current,changes.\"\n");
	printf("\t                         use current  for SA_TRACK_CURRENT,\n");
	printf("\t                             changes  for SA_TRACK_CHANGES\n");
	printf("\t                             only     for SA_TRACK_CHANGES_ONLY\n");
	printf("\t                             local    for SA_TRACK_LOCAL\n");
	printf("\t                             start    for SA_TRACK_START_STEP\n");
	printf("\t                             validate for SA_TRACK_VALIDATE_STEP\n");
	printf("\t-b or --btrack        for saClmClusterTrack_4() using buffer.\n");
	printf("\t-t or --timeout       timeout (in seconds) for poll that waits for callback.\n");
	printf("\t-h or --help          prints this usage and exits.\n");
	printf("\nEXAMPLES\n");
	printf("\tclmprint -n\n");
	printf("\tclmprint -n 0x2010f\n");
	printf("\tclmprint -a -t 10\n");
	printf("\tclmprint -a 0x2010f -t 10\n");
	printf("\tclmprint -m\n");
	printf("\tclmprint -m current,changes -t 10\n");
}

static void clm_dispatch(void)
{
	SaAisErrorT rc;

	for (;;) {
		rc = poll(clm_fds, 1, timeout);
		if (rc == -1) {
			if (errno == EINTR) {
				continue;
			} else {
				fprintf(stderr,
				"error - poll FAILED - %s", strerror(errno));
				exit(EXIT_FAILURE);
			}
		}
		if (rc == 0) {
			if (is_cbk_received == false)
				printf("poll timeout, no callback received\n");
			break;
		}
		if (clm_fds[0].revents & POLLIN) {
			rc = saClmDispatch(clm_handle, SA_DISPATCH_ALL);
			if (SA_AIS_OK != rc) {
				fprintf(stderr,
				"error - saClmDispatch failed with '%d'\n", rc);
				break;
			}
		}
		/*Exit aftet getting callback for NodeAsyncGet()*/
		if (mychar == 'a')
			exit(EXIT_SUCCESS);
	}
}
int main(int argc, char *argv[])
{
	SaClmClusterNotificationT_4 clusterNotification[SIZE_NOTIFICATIONS];
	SaClmClusterNotificationBufferT_4 clusterNotificationBuffer;
	SaClmClusterNodeT_4 clusterNode;
	SaClmCallbacksT_4 clm_callbacks;
	SaAisErrorT rc;
	SaUint8T trackFlags;
	int c;
	char *ptr = NULL, *tmp_optarg = NULL;
	SaClmNodeIdT node_id = SA_CLM_LOCAL_NODE_ID;

	is_cbk_received = false;

	clusterNotificationBuffer.notification  = clusterNotification;
	clm_callbacks.saClmClusterTrackCallback   = clm_track_callback;
	clm_callbacks.saClmClusterNodeGetCallback = clm_node_get_callback;
	trackFlags = (SA_TRACK_CURRENT | SA_TRACK_CHANGES_ONLY |
			SA_TRACK_START_STEP);
	struct option long_options[] = {
		{"help",      no_argument,       0, 'h'},
		{"nodeget",   optional_argument, 0, 'n'},
		{"asyncget",  optional_argument, 0, 'a'},
		{"track",     optional_argument, 0, 'm'},
		{"btrack",    no_argument,       0, 'b'},
		{"timeout",   required_argument, 0, 't'},
		{0, 0, 0, 0}
	};

	while (1) {
		c = getopt_long(argc, argv, "n::a::m::bht:", long_options, NULL);
		if (c == -1)
			break;

		switch (c) {
		case 'n':
		case 'a':
			mychar = c;
			if (optarg == NULL) {
				if ((argv[optind] != NULL) &&
						(argv[optind][0] != '-')) {
					node_id = strtoul(argv[optind], &ptr, 0);
					++optind;
				}
			} else
				node_id = strtoul(optarg, &ptr, 0);
			printf("node_id:%u(%x)\n", node_id, node_id);
			break;
		case 'b':
			mychar = c;
			break;
		case 'm':
			mychar = c;
			if (optarg == NULL) {
				if ((argv[optind] != NULL) &&
						(argv[optind][0] != '-')) {
					tmp_optarg = argv[optind];
					++optind;
					trackFlags = 0;
				}
			} else {
				tmp_optarg = optarg;
				trackFlags = 0;
			}
			ptr = strtok(tmp_optarg, ",");
			while  (ptr != NULL) {
				if (strcmp(ptr, "current") == 0) {
					trackFlags |= SA_TRACK_CURRENT;
				} else if (strcmp(ptr, "changes") == 0) {
					trackFlags |= SA_TRACK_CHANGES;
				} else if (strcmp(ptr, "only") == 0) {
					trackFlags |= SA_TRACK_CHANGES_ONLY;
				} else if (strcmp(ptr, "local") == 0) {
					trackFlags |= SA_TRACK_LOCAL;
				} else if (strcmp(ptr, "start") == 0) {
					trackFlags |= SA_TRACK_START_STEP;
				} else if (strcmp(ptr, "validate") == 0) {
					trackFlags |= SA_TRACK_VALIDATE_STEP;
				} else {
					fprintf(stderr, "error - Incorrect Track flag passed.\n");
					exit(EXIT_FAILURE);
				}
				ptr = strtok(NULL, ",");
			}
			if (!((trackFlags & SA_TRACK_CURRENT) || (trackFlags & SA_TRACK_CHANGES) ||
						(trackFlags & SA_TRACK_CHANGES_ONLY))) {
				fprintf(stderr, "error - Basic flags not passed.\n");
				exit(EXIT_FAILURE);
			}
			if ((trackFlags & SA_TRACK_CHANGES_ONLY) && (trackFlags & SA_TRACK_CHANGES)) {
				fprintf(stderr, "error - changes_only and changes cannot be passed simultaneously.\n");
				exit(EXIT_FAILURE);
			}
			break;
		case 't':
			timeout = atoi(optarg) * 1000;
			break;
		case 'h':
			usage(basename(argv[0]));
			exit(EXIT_SUCCESS);
			break;
		default:
			printf("Try '%s --help' for more information\n", argv[0]);
			exit(EXIT_FAILURE);
			break;
		}
	}
	rc = saClmInitialize_4(&clm_handle, &clm_callbacks, &clm_version);
	if (rc != SA_AIS_OK) {
		fprintf(stderr, "error - clmprint:: saClmInitialize_4 failed, rc = %d\n", rc);
		exit(EXIT_FAILURE);
	}

	rc = saClmSelectionObjectGet(clm_handle, &clm_selobject);
	if (rc != SA_AIS_OK) {
		fprintf(stderr, "error - clmprint:: saClmSelectionObjectGet failed, rc = %d\n", rc);
		exit(EXIT_FAILURE);
	}
	clm_fds[0].fd = clm_selobject;
	clm_fds[0].events = POLLIN;
	switch (mychar) {
	case 'n':
		rc = saClmClusterNodeGet_4(clm_handle, node_id,
				((timeout == -1) ? (TIME_OUT) : timeout), &clusterNode);
		if (rc != SA_AIS_OK) {
			fprintf(stderr, "error - clmprint:: saClmClusterNodeGet_4 failed, rc = %d\n", rc);
		} else  {
			printf("\n");
			print_node(&clusterNode);
			printf("<=======================================================\n\n");
		}
		break;
	case 'a':
		rc = saClmClusterNodeGetAsync(clm_handle, INVOCATION_ID, node_id);
		if (rc != SA_AIS_OK)
			fprintf(stderr, "error - clmprint :: saClmClusterNodeGetAsync failed, rc = %d\n", rc);
		else
			clm_dispatch();
		break;
	case 'b':
		clusterNotificationBuffer.numberOfItems = SIZE_NOTIFICATIONS;
		rc = saClmClusterTrack_4(clm_handle, SA_TRACK_CURRENT, &clusterNotificationBuffer);
		if (rc != SA_AIS_OK) {
			fprintf(stderr, "error - clmprint:: saClmClusterTrack_4 failed, rc = %d\n", rc);
		} else {
			printf("\nnumberOfItems = %d\n\n", clusterNotificationBuffer.numberOfItems);
			for (uint32_t i = 0; i < clusterNotificationBuffer.numberOfItems; i++) {
				print_node(&clusterNotificationBuffer.notification[i].clusterNode);
				printf("  Change                     : %s\n",
						clm_change[clusterNotificationBuffer.notification[i].clusterChange]);
				printf("<===================================================\n\n");
			}
		}
		break;
	case 'm':
		rc = saClmClusterTrack_4(clm_handle, trackFlags, NULL);
		if (rc != SA_AIS_OK)
			fprintf(stderr, "error - clmprint:: saClmClusterTrack_4 failed, rc = %d\n", rc);
		else
			clm_dispatch();
		break;
	default:
		usage(basename(argv[0]));
		break;
	}
	rc = saClmFinalize(clm_handle);
	if (rc != SA_AIS_OK) {
		fprintf(stderr, "error - clmprint:: saClmFinalize failed, rc = %d\n", rc);
		exit(EXIT_FAILURE);
	}
	return 0;
}

