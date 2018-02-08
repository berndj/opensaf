/*      -*- OpenSAF  -*-
 *
 * (C) Copyright 2008 The OpenSAF Foundation
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
 * Author(s): Emerson Network Power
 *
 */

#include <pthread.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "clm/apitest/clmtest.h"

static SaClmClusterNotificationBufferT notificationBuffer_1;
static SaClmClusterNotificationBufferT_4 notificationBuffer_4;
static SaUint8T trackFlags;
static SaInvocationT lock_inv;
static const char *s_node_name = "safNode=PL-3,safCluster=myClmCluster";

static int clm_node_lock(const char *nodeName, int ignoreOutput) {
  char command[256];

  if (ignoreOutput) {
    sprintf(command, "immadm -o 2 %s 2> /dev/null", nodeName);
  } else {
    sprintf(command, "immadm -o 2 %s", nodeName);
  }
  return system(command);
}

static int clm_node_unlock(const char *nodeName, int ignoreOutput) {
  char command[256];

  if (ignoreOutput) {
    sprintf(command, "immadm -o 1 %s 2> /dev/null", nodeName);
  } else {
    sprintf(command, "immadm -o 1 %s", nodeName);
  }
  return system(command);
}

static int clm_node_shutdown(const char *nodeName, int ignoreOutput) {
  char command[256];

  if (ignoreOutput) {
    sprintf(command, "immadm -o 3 %s 2> /dev/null", nodeName);
  } else {
    sprintf(command, "immadm -o 3 %s", nodeName);
  }
  return system(command);
}

static void *admin_lock(void *dummy) {
  assert(clm_node_lock(s_node_name, 0) != -1);
  /*test_validate(WEXITSTATUS(rc), 0);*/
  return nullptr;
}

static void *admin_unlock(void *dummy) {
  assert(clm_node_unlock(s_node_name, 0) != -1);
  /*test_validate(WEXITSTATUS(rc), 0);*/
  return nullptr;
}

static void *admin_shutdown(void *dummy) {
  assert(clm_node_shutdown(s_node_name, 0) != -1);
  /*test_validate(WEXITSTATUS(rc), 0);*/
  return nullptr;
}

static void saClmadmin_lock1() {
  int rc;
  char command[256];
  char name[] = "safNode=PL-3,safCluster=myClmCluster";

  // Lock node
  clm_node_lock(name, 1);

  sprintf(command, "immadm -o 2 %s", name);
  assert((rc = system(command)) != -1);
  test_validate(WEXITSTATUS(rc), 1);

  // Reset CLM state
  clm_node_unlock(name, 1);
}

static void saClmadmin_unlock1() {
  int rc;
  char command[256];
  char name[] = "safNode=PL-3,safCluster=myClmCluster";

  sprintf(command, "immadm -o 1 %s", name);
  assert((rc = system(command)) != -1);
  test_validate(WEXITSTATUS(rc), 1);
}

static void saClmadmin_shutdown1() {
  int rc;
  char command[256];
  char name[] = "safNode=PL-3,safCluster=myClmCluster";

  // Shutdown node
  clm_node_shutdown(name, 1);

  sprintf(command, "immadm -o 3 %s", name);
  assert((rc = system(command)) != -1);
  test_validate(WEXITSTATUS(rc), 1);

  // Reset CLM state
  clm_node_unlock(name, 1);
}

static void *plm_admin_trylock(void *dummy) {
  int rc;
  char command[256];
  char name[] = "safEE=Linux_os_hosting_clm_node,safDomain=domain_1";

  sprintf(command,
          "immadm -o 1 -p SA_PLM_ADMIN_LOCK_OPTION:SA_STRING_T:trylock  %s",
          name);
  assert((rc = system(command)) != -1);
  return nullptr;
}

static void saClmPlm_unlock() {
  int rc;
  char command[256];
  char name[] = "safEE=Linux_os_hosting_clm_node,safDomain=domain_1";

  sprintf(command, "immadm -o 2 %s", name);
  assert((rc = system(command)) != -1);
}

/*Print the Values in SaClmClusterNotificationBufferT_4*/
static void clmTrackbuf_4(
    SaClmClusterNotificationBufferT_4 *notificationBuffer) {
  unsigned i;
  printf("No of items = %d\n", notificationBuffer->numberOfItems);

  for (i = 0; i < notificationBuffer->numberOfItems; i++) {
    printf("\nValue of i = %d\n", i);
    printf("Cluster Change = %d\n",
           notificationBuffer->notification[i].clusterChange);
    printf("Node Name length = %d, value = %s\n",
           notificationBuffer->notification[i].clusterNode.nodeName.length,
           notificationBuffer->notification[i].clusterNode.nodeName.value);

    printf("Node Member = %d\n",
           notificationBuffer->notification[i].clusterNode.member);

    printf("Node  view number  = %llu\n",
           notificationBuffer->notification[i].clusterNode.initialViewNumber);

    printf("Node  eename length = %d,value  = %s\n",
           notificationBuffer->notification[i]
               .clusterNode.executionEnvironment.length,
           notificationBuffer->notification[0]
               .clusterNode.executionEnvironment.value);

    printf("Node  boottimestamp  = %llu\n",
           notificationBuffer->notification[i].clusterNode.bootTimestamp);

    printf(
        "Node  nodeAddress family  = %d,node address length = %d,"
        " node address value = %s\n",
        notificationBuffer->notification[i].clusterNode.nodeAddress.family,
        notificationBuffer->notification[i].clusterNode.nodeAddress.length,
        notificationBuffer->notification[i].clusterNode.nodeAddress.value);

    printf("Node  nodeid  = %u\n",
           notificationBuffer->notification[i].clusterNode.nodeId);
  }
}

static void clmTrackCallback4(
    const SaClmClusterNotificationBufferT_4 *notificationBuffer,
    SaUint32T numberOfMembers, SaInvocationT invocation,
    const SaNameT *rootCauseEntity, const SaNtfCorrelationIdsT *correlationIds,
    SaClmChangeStepT step, SaTimeT timeSupervision, SaAisErrorT error) {
  printf("\n");
  printf("Inside TrackCallback4\n");
  printf("invocation : %llu\n", invocation);
  printf("Step : %d\n", step);
  printf("error = %d\n", error);
  printf("numberOfMembers = %d\n", numberOfMembers);
  clmTrackbuf_4((SaClmClusterNotificationBufferT_4 *)notificationBuffer);
  lock_inv = invocation;
  printf("\n");
}

static void clmTrackCallback1(
    const SaClmClusterNotificationBufferT *notificationBuffer,
    SaUint32T numberOfMembers, SaAisErrorT error) {
  printf("\n");
  printf("\n Inside TrackCallback");
  printf("\n No of items = %d", notificationBuffer->numberOfItems);
  printf("\n rc = %d", error);
  printf("\n");
}

static void nodeGetCallBack1(SaInvocationT invocation,
                             const SaClmClusterNodeT *clusterNode,
                             SaAisErrorT error) {
  printf("\n inside of node get callback1");
}

static void nodeGetCallBack4(SaInvocationT invocation,
                             const SaClmClusterNodeT_4 *clusterNode,
                             SaAisErrorT error) {
  printf("\n inside of node get callback4");
}

SaClmCallbacksT_4 clmCallback4 = {nodeGetCallBack4, clmTrackCallback4};
SaClmCallbacksT clmCallback1 = {nodeGetCallBack1, clmTrackCallback1};

void saClmClusterTrack_01() {
  trackFlags = SA_TRACK_CURRENT;
  notificationBuffer_1.numberOfItems = 10;
  notificationBuffer_1.notification = (SaClmClusterNotificationT *)malloc(
      sizeof(SaClmClusterNotificationT) * notificationBuffer_1.numberOfItems);

  safassert(
      ClmTest::saClmInitialize(&clmHandle, &clmCallbacks_1, &clmVersion_1),
      SA_AIS_OK);
  rc = ClmTest::saClmClusterTrack(clmHandle, trackFlags, &notificationBuffer_1);
  printf("rc = %u", rc);
  safassert(ClmTest::saClmClusterTrackStop(clmHandle), SA_AIS_ERR_NOT_EXIST);
  free(notificationBuffer_1.notification);
  safassert(ClmTest::saClmFinalize(clmHandle), SA_AIS_OK);
  test_validate(rc, SA_AIS_OK);
}

void saClmClusterTrack_02() {
  trackFlags = SA_TRACK_CURRENT;
  notificationBuffer_4.numberOfItems = 10;
  notificationBuffer_4.notification = (SaClmClusterNotificationT_4 *)malloc(
      sizeof(SaClmClusterNotificationT_4) * notificationBuffer_4.numberOfItems);

  safassert(
      ClmTest::saClmInitialize_4(&clmHandle, &clmCallbacks_4, &clmVersion_4),
      SA_AIS_OK);
  rc = ClmTest::saClmClusterTrack_4(clmHandle, trackFlags,
                                    &notificationBuffer_4);
  clmTrackbuf_4(&notificationBuffer_4);
  safassert(ClmTest::saClmClusterTrackStop(clmHandle), SA_AIS_ERR_NOT_EXIST);
  safassert(ClmTest::saClmClusterNotificationFree_4(
                clmHandle, notificationBuffer_4.notification),
            SA_AIS_OK);
  safassert(ClmTest::saClmFinalize(clmHandle), SA_AIS_OK);
  test_validate(rc, SA_AIS_OK);
}

void saClmClusterTrack_03() {
  trackFlags = (SA_TRACK_CURRENT | SA_TRACK_LOCAL);
  notificationBuffer_1.numberOfItems = 1;
  notificationBuffer_1.notification = (SaClmClusterNotificationT *)malloc(
      sizeof(SaClmClusterNotificationT) * notificationBuffer_1.numberOfItems);

  safassert(
      ClmTest::saClmInitialize(&clmHandle, &clmCallbacks_1, &clmVersion_1),
      SA_AIS_OK);
  rc = ClmTest::saClmClusterTrack(clmHandle, trackFlags, &notificationBuffer_1);
  safassert(ClmTest::saClmClusterTrackStop(clmHandle), SA_AIS_ERR_NOT_EXIST);
  free(notificationBuffer_1.notification);
  safassert(ClmTest::saClmFinalize(clmHandle), SA_AIS_OK);
  test_validate(rc, SA_AIS_OK);
}

void saClmClusterTrack_04() {
  trackFlags = (SA_TRACK_CURRENT | SA_TRACK_LOCAL);
  notificationBuffer_4.numberOfItems = 1;
  notificationBuffer_4.notification = (SaClmClusterNotificationT_4 *)malloc(
      sizeof(SaClmClusterNotificationT_4) * notificationBuffer_4.numberOfItems);

  safassert(
      ClmTest::saClmInitialize_4(&clmHandle, &clmCallbacks_4, &clmVersion_4),
      SA_AIS_OK);
  rc = ClmTest::saClmClusterTrack_4(clmHandle, trackFlags,
                                    &notificationBuffer_4);
  clmTrackbuf_4(&notificationBuffer_4);
  safassert(ClmTest::saClmClusterTrackStop(clmHandle), SA_AIS_ERR_NOT_EXIST);
  safassert(ClmTest::saClmClusterNotificationFree_4(
                clmHandle, notificationBuffer_4.notification),
            SA_AIS_OK);
  safassert(ClmTest::saClmFinalize(clmHandle), SA_AIS_OK);
  test_validate(rc, SA_AIS_OK);
}

void saClmClusterTrack_05() {
  trackFlags = (SA_TRACK_CURRENT | SA_TRACK_LOCAL);
  notificationBuffer_1.numberOfItems = 1;
  notificationBuffer_1.notification = (SaClmClusterNotificationT *)malloc(
      sizeof(SaClmClusterNotificationT) * notificationBuffer_1.numberOfItems);

  safassert(
      ClmTest::saClmInitialize(&clmHandle, &clmCallbacks_1, &clmVersion_1),
      SA_AIS_OK);
  rc = ClmTest::saClmClusterTrack(0, trackFlags, &notificationBuffer_1);
  /*safassert(ClmTest::saClmClusterTrackStop(clmHandle), SA_AIS_OK);*/
  free(notificationBuffer_1.notification);
  safassert(ClmTest::saClmFinalize(clmHandle), SA_AIS_OK);
  test_validate(rc, SA_AIS_ERR_BAD_HANDLE);

  trackFlags = (SA_TRACK_CURRENT | SA_TRACK_LOCAL);
  notificationBuffer_1.numberOfItems = 1;
  notificationBuffer_1.notification = (SaClmClusterNotificationT *)malloc(
      sizeof(SaClmClusterNotificationT) * notificationBuffer_1.numberOfItems);

  safassert(
      ClmTest::saClmInitialize(&clmHandle, &clmCallbacks_1, &clmVersion_1),
      SA_AIS_OK);
  rc = ClmTest::saClmClusterTrack(-1, trackFlags, &notificationBuffer_1);
  /*safassert(ClmTest::saClmClusterTrackStop(clmHandle), SA_AIS_OK);*/
  free(notificationBuffer_1.notification);
  safassert(ClmTest::saClmFinalize(clmHandle), SA_AIS_OK);
  test_validate(rc, SA_AIS_ERR_BAD_HANDLE);
}

void saClmClusterTrack_06() {
  trackFlags = (SA_TRACK_CURRENT | SA_TRACK_LOCAL);
  notificationBuffer_1.numberOfItems = 1;
  notificationBuffer_1.notification = (SaClmClusterNotificationT *)malloc(
      sizeof(SaClmClusterNotificationT) * notificationBuffer_1.numberOfItems);

  safassert(
      ClmTest::saClmInitialize(&clmHandle, &clmCallbacks_1, &clmVersion_1),
      SA_AIS_OK);
  rc = ClmTest::saClmClusterTrack(clmHandle, 0, &notificationBuffer_1);
  /*safassert(ClmTest::saClmClusterTrackStop(clmHandle), SA_AIS_OK);*/
  free(notificationBuffer_1.notification);
  safassert(ClmTest::saClmFinalize(clmHandle), SA_AIS_OK);
  test_validate(rc, SA_AIS_ERR_BAD_FLAGS);
}

void saClmClusterTrack_07() {
  trackFlags = SA_TRACK_CURRENT;
  notificationBuffer_1.numberOfItems = 1;
  notificationBuffer_1.notification = (SaClmClusterNotificationT *)malloc(
      sizeof(SaClmClusterNotificationT) * notificationBuffer_1.numberOfItems);

  /*safassert(ClmTest::saClmInitialize(&clmHandle, &clmCallbacks_1,
   * &clmVersion_1), SA_AIS_OK);*/
  safassert(ClmTest::saClmInitialize(&clmHandle, nullptr, &clmVersion_1),
            SA_AIS_OK);
  /*rc = ClmTest::saClmClusterTrack(clmHandle, trackFlags,
   * &notificationBuffer_1);
   */
  rc = ClmTest::saClmClusterTrack(clmHandle, trackFlags, nullptr);
  /*safassert(ClmTest::saClmClusterTrackStop(clmHandle), SA_AIS_OK);*/
  free(notificationBuffer_1.notification);
  safassert(ClmTest::saClmFinalize(clmHandle), SA_AIS_OK);
  test_validate(rc, SA_AIS_ERR_INIT);
}

void saClmClusterTrack_08() {
  trackFlags = (SA_TRACK_CURRENT | SA_TRACK_LOCAL);
  notificationBuffer_4.numberOfItems = 1;
  notificationBuffer_4.notification = (SaClmClusterNotificationT_4 *)malloc(
      sizeof(SaClmClusterNotificationT_4) * notificationBuffer_4.numberOfItems);

  safassert(
      ClmTest::saClmInitialize_4(&clmHandle, &clmCallbacks_4, &clmVersion_4),
      SA_AIS_OK);
  rc = ClmTest::saClmClusterTrack_4(0, trackFlags, &notificationBuffer_4);
  /*safassert(ClmTest::saClmClusterTrackStop(clmHandle), SA_AIS_OK);*/
  safassert(ClmTest::saClmClusterNotificationFree_4(
                clmHandle, notificationBuffer_4.notification),
            SA_AIS_OK);
  safassert(ClmTest::saClmFinalize(clmHandle), SA_AIS_OK);
  test_validate(rc, SA_AIS_ERR_BAD_HANDLE);

  trackFlags = (SA_TRACK_CURRENT | SA_TRACK_LOCAL);
  notificationBuffer_4.numberOfItems = 1;
  notificationBuffer_4.notification = (SaClmClusterNotificationT_4 *)malloc(
      sizeof(SaClmClusterNotificationT_4) * notificationBuffer_4.numberOfItems);

  safassert(
      ClmTest::saClmInitialize_4(&clmHandle, &clmCallbacks_4, &clmVersion_4),
      SA_AIS_OK);
  rc = ClmTest::saClmClusterTrack_4(-1, trackFlags, &notificationBuffer_4);
  /*safassert(ClmTest::saClmClusterTrackStop(clmHandle), SA_AIS_OK);*/
  safassert(ClmTest::saClmClusterNotificationFree_4(
                clmHandle, notificationBuffer_4.notification),
            SA_AIS_OK);
  safassert(ClmTest::saClmFinalize(clmHandle), SA_AIS_OK);
  test_validate(rc, SA_AIS_ERR_BAD_HANDLE);
}

void saClmClusterTrack_09() {
  trackFlags = (SA_TRACK_CURRENT | SA_TRACK_LOCAL);
  notificationBuffer_4.numberOfItems = 1;
  notificationBuffer_4.notification = (SaClmClusterNotificationT_4 *)malloc(
      sizeof(SaClmClusterNotificationT_4) * notificationBuffer_4.numberOfItems);

  safassert(
      ClmTest::saClmInitialize_4(&clmHandle, &clmCallbacks_4, &clmVersion_4),
      SA_AIS_OK);
  rc = ClmTest::saClmClusterTrack_4(clmHandle, 0, &notificationBuffer_4);
  /*safassert(ClmTest::saClmClusterTrackStop(clmHandle), SA_AIS_OK);*/
  safassert(ClmTest::saClmClusterNotificationFree_4(
                clmHandle, notificationBuffer_4.notification),
            SA_AIS_OK);
  safassert(ClmTest::saClmFinalize(clmHandle), SA_AIS_OK);
  test_validate(rc, SA_AIS_ERR_BAD_FLAGS);
}

void saClmClusterTrack_10() {
  trackFlags = (SA_TRACK_CURRENT | SA_TRACK_LOCAL);
  notificationBuffer_4.numberOfItems = 1;
  notificationBuffer_4.notification = (SaClmClusterNotificationT_4 *)malloc(
      sizeof(SaClmClusterNotificationT_4) * notificationBuffer_4.numberOfItems);

  safassert(
      ClmTest::saClmInitialize_4(&clmHandle, &clmCallbacks_4, &clmVersion_4),
      SA_AIS_OK);
  rc = ClmTest::saClmClusterTrack_4(clmHandle, trackFlags, nullptr);
  /*safassert(ClmTest::saClmClusterTrackStop(clmHandle), SA_AIS_OK);*/
  safassert(ClmTest::saClmClusterNotificationFree_4(
                clmHandle, notificationBuffer_4.notification),
            SA_AIS_OK);
  safassert(ClmTest::saClmFinalize(clmHandle), SA_AIS_OK);
  test_validate(rc, SA_AIS_ERR_INIT);
}

void saClmClusterTrack_11() {
  trackFlags = SA_TRACK_CURRENT;
  notificationBuffer_4.numberOfItems = 10;
  notificationBuffer_4.notification = (SaClmClusterNotificationT_4 *)malloc(
      sizeof(SaClmClusterNotificationT_4) * notificationBuffer_1.numberOfItems);

  safassert(
      ClmTest::saClmInitialize(&clmHandle, &clmCallbacks_1, &clmVersion_1),
      SA_AIS_OK);
  rc = ClmTest::saClmClusterTrack_4(clmHandle, trackFlags,
                                    &notificationBuffer_4);
  /*safassert(ClmTest::saClmClusterTrackStop(clmHandle), SA_AIS_OK);*/
  free(notificationBuffer_4.notification);
  safassert(ClmTest::saClmFinalize(clmHandle), SA_AIS_OK);
  test_validate(rc, SA_AIS_ERR_VERSION);
}

void saClmClusterTrack_12() {
  trackFlags = SA_TRACK_CURRENT;
  notificationBuffer_1.numberOfItems = 10;
  notificationBuffer_1.notification = (SaClmClusterNotificationT *)malloc(
      sizeof(SaClmClusterNotificationT) * notificationBuffer_1.numberOfItems);

  safassert(
      ClmTest::saClmInitialize_4(&clmHandle, &clmCallbacks_4, &clmVersion_4),
      SA_AIS_OK);
  rc = ClmTest::saClmClusterTrack(clmHandle, trackFlags, &notificationBuffer_1);
  /*safassert(ClmTest::saClmClusterTrackStop(clmHandle), SA_AIS_OK);*/
  free(notificationBuffer_1.notification);
  safassert(ClmTest::saClmFinalize(clmHandle), SA_AIS_OK);
  test_validate(rc, SA_AIS_ERR_VERSION);
}

void saClmClusterTrack_13() {
  trackFlags = SA_TRACK_CURRENT;
  safassert(ClmTest::saClmInitialize(&clmHandle, &clmCallback1, &clmVersion_1),
            SA_AIS_OK);
  rc = ClmTest::saClmClusterTrack(clmHandle, trackFlags, nullptr);
  safassert(ClmTest::saClmDispatch(clmHandle, SA_DISPATCH_ALL), SA_AIS_OK);
  safassert(ClmTest::saClmClusterTrackStop(clmHandle), SA_AIS_ERR_NOT_EXIST);
  safassert(ClmTest::saClmFinalize(clmHandle), SA_AIS_OK);
  test_validate(rc, SA_AIS_OK);
}

void saClmClusterTrack_14() {
  trackFlags = SA_TRACK_CURRENT;
  struct pollfd fds[1];
  int ret;

  safassert(
      ClmTest::saClmInitialize_4(&clmHandle, &clmCallback4, &clmVersion_4),
      SA_AIS_OK);
  safassert(ClmTest::saClmSelectionObjectGet(clmHandle, &selectionObject),
            SA_AIS_OK);
  rc = ClmTest::saClmClusterTrack_4(clmHandle, trackFlags, nullptr);

  fds[0].fd = (int)selectionObject;
  fds[0].events = POLLIN;
  ret = poll(fds, 1, 60000);
  assert(ret == 1);

  safassert(ClmTest::saClmDispatch(clmHandle, SA_DISPATCH_ALL), SA_AIS_OK);
  safassert(ClmTest::saClmClusterTrackStop(clmHandle), SA_AIS_ERR_NOT_EXIST);
  safassert(ClmTest::saClmFinalize(clmHandle), SA_AIS_OK);
  test_validate(rc, SA_AIS_OK);
}

/*Bad flags - only start step without changes/changes only*/
void saClmClusterTrack_15() {
  trackFlags = SA_TRACK_START_STEP;
  safassert(
      ClmTest::saClmInitialize_4(&clmHandle, &clmCallback4, &clmVersion_4),
      SA_AIS_OK);
  rc = ClmTest::saClmClusterTrack_4(clmHandle, trackFlags, nullptr);
  /*safassert(ClmTest::saClmClusterTrackStop(clmHandle), SA_AIS_OK);*/
  safassert(ClmTest::saClmFinalize(clmHandle), SA_AIS_OK);
  test_validate(rc, SA_AIS_ERR_BAD_FLAGS);
}

/*Bad flags - only validate step without changes/changes only*/
void saClmClusterTrack_16() {
  trackFlags = SA_TRACK_VALIDATE_STEP;
  safassert(
      ClmTest::saClmInitialize_4(&clmHandle, &clmCallback4, &clmVersion_4),
      SA_AIS_OK);
  rc = ClmTest::saClmClusterTrack_4(clmHandle, trackFlags, nullptr);
  /*safassert(ClmTest::saClmClusterTrackStop(clmHandle), SA_AIS_OK);*/
  safassert(ClmTest::saClmFinalize(clmHandle), SA_AIS_OK);
  test_validate(rc, SA_AIS_ERR_BAD_FLAGS);
}

/*Bad flags - both changes and changes only set */
void saClmClusterTrack_17() {
  trackFlags = SA_TRACK_CHANGES | SA_TRACK_CHANGES_ONLY;
  safassert(
      ClmTest::saClmInitialize_4(&clmHandle, &clmCallback4, &clmVersion_4),
      SA_AIS_OK);
  rc = ClmTest::saClmClusterTrack_4(clmHandle, trackFlags, nullptr);
  /*safassert(ClmTest::saClmClusterTrackStop(clmHandle), SA_AIS_OK);*/
  safassert(ClmTest::saClmFinalize(clmHandle), SA_AIS_OK);
  test_validate(rc, SA_AIS_ERR_BAD_FLAGS);
}
/*Bad Flags - only start step without changes/changes only for old API's*/
void saClmClusterTrack_18() {
  trackFlags = SA_TRACK_START_STEP;
  safassert(ClmTest::saClmInitialize(&clmHandle, &clmCallback1, &clmVersion_1),
            SA_AIS_OK);
  rc = ClmTest::saClmClusterTrack(clmHandle, trackFlags, nullptr);
  /*safassert(ClmTest::saClmClusterTrackStop(clmHandle), SA_AIS_OK);*/
  safassert(ClmTest::saClmFinalize(clmHandle), SA_AIS_OK);
  test_validate(rc, SA_AIS_ERR_BAD_FLAGS);
}

/*Bad flags - only validate step without changes/changes only for old API's*/
void saClmClusterTrack_19() {
  trackFlags = SA_TRACK_VALIDATE_STEP;
  safassert(ClmTest::saClmInitialize(&clmHandle, &clmCallback1, &clmVersion_1),
            SA_AIS_OK);
  rc = ClmTest::saClmClusterTrack(clmHandle, trackFlags, nullptr);
  /*safassert(ClmTest::saClmClusterTrackStop(clmHandle), SA_AIS_OK);*/
  safassert(ClmTest::saClmFinalize(clmHandle), SA_AIS_OK);
  test_validate(rc, SA_AIS_ERR_BAD_FLAGS);
}

/*Bad flags - both changes and changes only set for old API's*/
void saClmClusterTrack_20() {
  trackFlags = SA_TRACK_CHANGES | SA_TRACK_CHANGES_ONLY;
  safassert(ClmTest::saClmInitialize(&clmHandle, &clmCallback1, &clmVersion_1),
            SA_AIS_OK);
  rc = ClmTest::saClmClusterTrack(clmHandle, trackFlags, nullptr);
  /*safassert(ClmTest::saClmClusterTrackStop(clmHandle), SA_AIS_OK);*/
  safassert(ClmTest::saClmFinalize(clmHandle), SA_AIS_OK);
  test_validate(rc, SA_AIS_ERR_BAD_FLAGS);
}

void saClmClusterTrack_21() {
  struct pollfd fds[1];
  int ret;
  pthread_t thread;

  trackFlags = SA_TRACK_CHANGES_ONLY;

  safassert(
      ClmTest::saClmInitialize_4(&clmHandle, &clmCallback4, &clmVersion_4),
      SA_AIS_OK);
  safassert(ClmTest::saClmSelectionObjectGet(clmHandle, &selectionObject),
            SA_AIS_OK);
  rc = ClmTest::saClmClusterTrack_4(clmHandle, trackFlags, nullptr);

  fds[0].fd = (int)selectionObject;
  fds[0].events = POLLIN;

  if (pthread_create(&thread, nullptr, admin_lock, nullptr) != 0) {
    printf("thread creation failed");
  }

  while (1) {
    printf("waiting on poll");
    ret = poll(fds, 1, 60000);
    assert(ret == 1);
    if (fds[0].revents & POLLIN) break;
  }

  safassert(ClmTest::saClmDispatch(clmHandle, SA_DISPATCH_ALL), SA_AIS_OK);
  safassert(ClmTest::saClmClusterTrackStop(clmHandle), SA_AIS_OK);
  safassert(ClmTest::saClmFinalize(clmHandle), SA_AIS_OK);
  test_validate(rc, SA_AIS_OK);

  // Reset CLM state
  clm_node_unlock(s_node_name, 1);
}

void saClmClusterTrack_22() {
  struct pollfd fds[1];
  int ret;
  pthread_t thread1;

  // Lock CLM
  clm_node_lock(s_node_name, 1);

  trackFlags = SA_TRACK_CHANGES_ONLY;

  safassert(
      ClmTest::saClmInitialize_4(&clmHandle, &clmCallback4, &clmVersion_4),
      SA_AIS_OK);
  safassert(ClmTest::saClmSelectionObjectGet(clmHandle, &selectionObject),
            SA_AIS_OK);
  rc = ClmTest::saClmClusterTrack_4(clmHandle, trackFlags, nullptr);

  fds[0].fd = (int)selectionObject;
  fds[0].events = POLLIN;

  if (pthread_create(&thread1, nullptr, admin_unlock, nullptr) != 0) {
    printf("thread creation failed");
  }

  while (1) {
    ret = poll(fds, 1, 60000);
    assert(ret == 1);
    if (fds[0].revents & POLLIN) break;
  }

  safassert(ClmTest::saClmDispatch(clmHandle, SA_DISPATCH_ALL), SA_AIS_OK);
  safassert(ClmTest::saClmClusterTrackStop(clmHandle), SA_AIS_OK);
  safassert(ClmTest::saClmFinalize(clmHandle), SA_AIS_OK);
  test_validate(rc, SA_AIS_OK);
}

void saClmClusterTrack_23() {
  struct pollfd fds[1];
  int ret;
  pthread_t thread2;

  trackFlags = SA_TRACK_CHANGES;

  safassert(
      ClmTest::saClmInitialize_4(&clmHandle, &clmCallback4, &clmVersion_4),
      SA_AIS_OK);
  safassert(ClmTest::saClmSelectionObjectGet(clmHandle, &selectionObject),
            SA_AIS_OK);
  rc = ClmTest::saClmClusterTrack_4(clmHandle, trackFlags, nullptr);

  fds[0].fd = (int)selectionObject;
  fds[0].events = POLLIN;

  if (pthread_create(&thread2, nullptr, admin_lock, nullptr) != 0) {
    printf("thread creation failed");
  }

  while (1) {
    ret = poll(fds, 1, 60000);
    assert(ret == 1);
    if (fds[0].revents & POLLIN) break;
  }

  safassert(ClmTest::saClmDispatch(clmHandle, SA_DISPATCH_ALL), SA_AIS_OK);
  safassert(ClmTest::saClmClusterTrackStop(clmHandle), SA_AIS_OK);
  safassert(ClmTest::saClmFinalize(clmHandle), SA_AIS_OK);
  test_validate(rc, SA_AIS_OK);

  // Reset CLM state
  clm_node_unlock(s_node_name, 1);
}

void saClmClusterTrack_24() {
  struct pollfd fds[1];
  int ret;
  pthread_t thread4;

  trackFlags = SA_TRACK_CHANGES | SA_TRACK_START_STEP;

  safassert(
      ClmTest::saClmInitialize_4(&clmHandle, &clmCallback4, &clmVersion_4),
      SA_AIS_OK);
  safassert(ClmTest::saClmSelectionObjectGet(clmHandle, &selectionObject),
            SA_AIS_OK);
  rc = ClmTest::saClmClusterTrack_4(clmHandle, trackFlags, nullptr);

  fds[0].fd = (int)selectionObject;
  fds[0].events = POLLIN;

  if (pthread_create(&thread4, nullptr, admin_lock, nullptr) != 0) {
    printf("thread creation failed");
  }
  while (1) {
    ret = poll(fds, 1, 60000);
    assert(ret == 1);
    if (fds[0].revents & POLLIN) {
      break;
    }
  }
  safassert(ClmTest::saClmDispatch(clmHandle, SA_DISPATCH_ALL), SA_AIS_OK);
  safassert(saClmResponse_4(clmHandle, lock_inv, SA_CLM_CALLBACK_RESPONSE_OK),
            SA_AIS_OK);

  fds[0].fd = (int)selectionObject;
  fds[0].events = POLLIN;
  while (1) {
    ret = poll(fds, 1, 60000);
    assert(ret == 1);
    if (fds[0].revents & POLLIN) {
      break;
    }
  }

  safassert(ClmTest::saClmDispatch(clmHandle, SA_DISPATCH_ALL), SA_AIS_OK);

  safassert(ClmTest::saClmClusterTrackStop(clmHandle), SA_AIS_OK);
  safassert(ClmTest::saClmFinalize(clmHandle), SA_AIS_OK);
  test_validate(rc, SA_AIS_OK);

  // Reset CLM state
  clm_node_unlock(s_node_name, 1);
}

void saClmClusterTrack_25() {
  struct pollfd fds[1];
  int ret;
  pthread_t thread6;

  trackFlags = SA_TRACK_CHANGES_ONLY | SA_TRACK_START_STEP;

  safassert(
      ClmTest::saClmInitialize_4(&clmHandle, &clmCallback4, &clmVersion_4),
      SA_AIS_OK);
  safassert(ClmTest::saClmSelectionObjectGet(clmHandle, &selectionObject),
            SA_AIS_OK);
  rc = ClmTest::saClmClusterTrack_4(clmHandle, trackFlags, nullptr);

  fds[0].fd = (int)selectionObject;
  fds[0].events = POLLIN;

  if (pthread_create(&thread6, nullptr, admin_lock, nullptr) != 0) {
    printf("thread creation failed");
  }

  while (1) {
    ret = poll(fds, 1, 60000);
    assert(ret == 1);
    if (fds[0].revents & POLLIN) {
      break;
    }
  }
  safassert(ClmTest::saClmDispatch(clmHandle, SA_DISPATCH_ALL), SA_AIS_OK);
  safassert(saClmResponse_4(clmHandle, lock_inv, SA_CLM_CALLBACK_RESPONSE_OK),
            SA_AIS_OK);

  fds[0].fd = (int)selectionObject;
  fds[0].events = POLLIN;
  while (1) {
    ret = poll(fds, 1, 60000);
    assert(ret == 1);
    if (fds[0].revents & POLLIN) {
      break;
    }
  }

  safassert(ClmTest::saClmDispatch(clmHandle, SA_DISPATCH_ALL), SA_AIS_OK);

  safassert(ClmTest::saClmClusterTrackStop(clmHandle), SA_AIS_OK);
  safassert(ClmTest::saClmFinalize(clmHandle), SA_AIS_OK);
  test_validate(rc, SA_AIS_OK);

  // Reset CLM state
  clm_node_unlock(s_node_name, 1);
}

void saClmClusterTrack_27() {
  struct pollfd fds[1];
  int ret;
  pthread_t thread8;

  trackFlags = SA_TRACK_CHANGES_ONLY | SA_TRACK_START_STEP;

  safassert(
      ClmTest::saClmInitialize_4(&clmHandle, &clmCallback4, &clmVersion_4),
      SA_AIS_OK);
  safassert(ClmTest::saClmSelectionObjectGet(clmHandle, &selectionObject),
            SA_AIS_OK);
  rc = ClmTest::saClmClusterTrack_4(clmHandle, trackFlags, nullptr);

  fds[0].fd = (int)selectionObject;
  fds[0].events = POLLIN;

  if (pthread_create(&thread8, nullptr, admin_shutdown, nullptr) != 0) {
    printf("thread creation failed");
  }

  while (1) {
    ret = poll(fds, 1, 60000);
    assert(ret == 1);
    if (fds[0].revents & POLLIN) {
      break;
    }
  }
  safassert(ClmTest::saClmDispatch(clmHandle, SA_DISPATCH_ALL), SA_AIS_OK);
  safassert(saClmResponse_4(clmHandle, lock_inv, SA_CLM_CALLBACK_RESPONSE_OK),
            SA_AIS_OK);

  fds[0].fd = (int)selectionObject;
  fds[0].events = POLLIN;
  while (1) {
    ret = poll(fds, 1, 60000);
    assert(ret == 1);
    if (fds[0].revents & POLLIN) {
      break;
    }
  }

  safassert(ClmTest::saClmDispatch(clmHandle, SA_DISPATCH_ALL), SA_AIS_OK);

  safassert(ClmTest::saClmClusterTrackStop(clmHandle), SA_AIS_OK);
  safassert(ClmTest::saClmFinalize(clmHandle), SA_AIS_OK);
  test_validate(rc, SA_AIS_OK);

  // Reset CLM state
  clm_node_unlock(s_node_name, 1);
}
void saClmClusterTrack_28() {
  struct pollfd fds[1];
  int ret;
  pthread_t thread8;

  trackFlags = SA_TRACK_CHANGES | SA_TRACK_START_STEP;

  safassert(
      ClmTest::saClmInitialize_4(&clmHandle, &clmCallback4, &clmVersion_4),
      SA_AIS_OK);
  safassert(ClmTest::saClmSelectionObjectGet(clmHandle, &selectionObject),
            SA_AIS_OK);
  rc = ClmTest::saClmClusterTrack_4(clmHandle, trackFlags, nullptr);

  fds[0].fd = (int)selectionObject;
  fds[0].events = POLLIN;

  if (pthread_create(&thread8, nullptr, admin_shutdown, nullptr) != 0) {
    printf("thread creation failed");
  }

  while (1) {
    ret = poll(fds, 1, 60000);
    assert(ret == 1);
    if (fds[0].revents & POLLIN) {
      break;
    }
  }
  safassert(ClmTest::saClmDispatch(clmHandle, SA_DISPATCH_ALL), SA_AIS_OK);
  safassert(saClmResponse_4(clmHandle, lock_inv, SA_CLM_CALLBACK_RESPONSE_OK),
            SA_AIS_OK);

  fds[0].fd = (int)selectionObject;
  fds[0].events = POLLIN;
  while (1) {
    ret = poll(fds, 1, 60000);
    assert(ret == 1);
    if (fds[0].revents & POLLIN) {
      break;
    }
  }

  safassert(ClmTest::saClmDispatch(clmHandle, SA_DISPATCH_ALL), SA_AIS_OK);

  safassert(ClmTest::saClmClusterTrackStop(clmHandle), SA_AIS_OK);
  safassert(ClmTest::saClmFinalize(clmHandle), SA_AIS_OK);
  test_validate(rc, SA_AIS_OK);

  // Reset CLM state
  clm_node_unlock(s_node_name, 1);
}

/*plm admin trylock*/
void saClmClusterTrack_31() {
  struct pollfd fds[1];
  int ret;
  pthread_t thread4;

  trackFlags = SA_TRACK_CHANGES | SA_TRACK_START_STEP | SA_TRACK_VALIDATE_STEP;

  printf("This test case can run only if PLM is enabled in the system\n");
  fflush(stdout);
  return;
  safassert(
      ClmTest::saClmInitialize_4(&clmHandle, &clmCallback4, &clmVersion_4),
      SA_AIS_OK);
  safassert(ClmTest::saClmSelectionObjectGet(clmHandle, &selectionObject),
            SA_AIS_OK);
  rc = ClmTest::saClmClusterTrack_4(clmHandle, trackFlags, nullptr);

  fds[0].fd = (int)selectionObject;
  fds[0].events = POLLIN;

  if (pthread_create(&thread4, nullptr, plm_admin_trylock, nullptr) != 0) {
    printf("thread creation failed");
  }
  while (1) {
    ret = poll(fds, 1, 60000);
    if (ret == 1) {
      printf("test timed out\n");
      fflush(stdout);
      break;
    }
    if (fds[0].revents & POLLIN) {
      break;
    }
  }
  safassert(ClmTest::saClmDispatch(clmHandle, SA_DISPATCH_ALL), SA_AIS_OK);
  safassert(saClmResponse_4(clmHandle, lock_inv, SA_CLM_CALLBACK_RESPONSE_OK),
            SA_AIS_OK);

  fds[0].fd = (int)selectionObject;
  fds[0].events = POLLIN;
  while (1) {
    ret = poll(fds, 1, 60000);
    if (ret == 1) {
      printf("test timed out\n");
      fflush(stdout);
      break;
    }
    if (fds[0].revents & POLLIN) {
      break;
    }
  }

  safassert(ClmTest::saClmDispatch(clmHandle, SA_DISPATCH_ALL), SA_AIS_OK);
  safassert(saClmResponse_4(clmHandle, lock_inv, SA_CLM_CALLBACK_RESPONSE_OK),
            SA_AIS_OK);

  fds[0].fd = (int)selectionObject;
  fds[0].events = POLLIN;
  while (1) {
    ret = poll(fds, 1, 60000);
    if (ret == 1) {
      printf("test timed out\n");
      fflush(stdout);
      break;
    }
    if (fds[0].revents & POLLIN) {
      break;
    }
  }
  safassert(ClmTest::saClmDispatch(clmHandle, SA_DISPATCH_ALL), SA_AIS_OK);
  safassert(ClmTest::saClmClusterTrackStop(clmHandle), SA_AIS_OK);
  safassert(ClmTest::saClmFinalize(clmHandle), SA_AIS_OK);
  test_validate(rc, SA_AIS_OK);
}

__attribute__((constructor)) static void saClmClusterTrack_constructor() {
  test_suite_add(7,
                 "Test case for saClmClusterTrack. ** For all tests to pass,"
                 " Run a payload with node_name PL-3 **\n");
  test_case_add(7, saClmClusterTrack_01, "saClmClusterTrack with valid param");
  test_case_add(7, saClmClusterTrack_02,
                "saClmClusterTrack_4 with valid param");
  test_case_add(7, saClmClusterTrack_03,
                "saClmClusterTrack with (SA_TRACK_CURRENT | SA_TRACK_LOCAL)");
  test_case_add(7, saClmClusterTrack_04,
                "saClmClusterTrack_4 with (SA_TRACK_CURRENT | SA_TRACK_LOCAL)");
  test_case_add(7, saClmClusterTrack_05,
                "saClmClusterTrack with null handle or invalid handle)");
  test_case_add(7, saClmClusterTrack_06, "saClmClusterTrack with null flags)");
  test_case_add(7, saClmClusterTrack_07,
                "saClmClusterTrack with null buffer & null callbacks)");
  test_case_add(7, saClmClusterTrack_08,
                "saClmClusterTrack_4 with null handle or invalid handle)");
  test_case_add(7, saClmClusterTrack_09, "saClmClusterTrack_4 with null flags");
  test_case_add(7, saClmClusterTrack_10,
                "saClmClusterTrack_4 with null buffer & null callbacks)");
  test_case_add(7, saClmClusterTrack_11,
                "saClmClusterTrack_4 with handle of B.01.01");
  test_case_add(7, saClmClusterTrack_12,
                "saClmClusterTrack with handle of B.04.01");
  test_case_add(7, saClmClusterTrack_13,
                "saClmClusterTrack with valid param & NULL buffer");
  test_case_add(7, saClmClusterTrack_14,
                "saClmClusterTrack_4 with valid param & NULL buffer");
  test_case_add(7, saClmClusterTrack_15,
                "saClmClusterTrack_4 with bad flags - only start tarckflag");
  test_case_add(
      7, saClmClusterTrack_16,
      "saClmClusterTrack_4 with bad flags - only validate track flag");
  test_case_add(7, saClmClusterTrack_17,
                "saClmClusterTrack_4 with SA_TRACK_CHANGES"
                " and SA_TRACK_CHANGES_ONLY track flags");
  test_case_add(7, saClmClusterTrack_18,
                "saClmClusterTrack with bad flags - only start tarckflag");
  test_case_add(
      7, saClmClusterTrack_19,
      "saClmClusterTrack with with bad flags - only validate track flag");
  test_case_add(7, saClmClusterTrack_20,
                "saClmClusterTrack with SA_TRACK_CHANGES"
                " and SA_TRACK_CHANGES_ONLY track flags");
  test_case_add(
      7, saClmClusterTrack_21,
      "saClmClusterTrack_4 with SA_TRACK_CHANGES_ONLY track flags - admin lock");
  test_case_add(7, saClmClusterTrack_22,
                "saClmClusterTrack_4 with SA_TRACK_CHANGES_ONLY track flags"
                " - admin unlock");
  test_case_add(
      7, saClmClusterTrack_23,
      "saClmClusterTrack_4 with SA_TRACK_CHANGES track flags - admin lock");
  test_case_add(
      7, saClmClusterTrack_24,
      "saClmClusterTrack_4 with SA_TRACK_CHANGES and START STEP track flags"
      " - admin lock");
  test_case_add(
      7, saClmClusterTrack_25,
      "saClmClusterTrack_4 with SA_TRACK_CHANGES_ONLY and START STEP track flags"
      " -admin lock");
  test_case_add(7, saClmadmin_lock1, "admin lock of the already lock node");
  test_case_add(
      7, saClmClusterTrack_27,
      "saClmClusterTrack_4 with SA_TRACK_CHANGES_ONLY and START STEP track flags"
      " - adminshutdown ");
  test_case_add(
      7, saClmClusterTrack_28,
      "saClmClusterTrack_4 with SA_TRACK_CHANGES track flags - admin shutdown");
  test_case_add(7, saClmadmin_shutdown1, "admin shutdown of already shut node");
  test_case_add(7, saClmadmin_unlock1, "unlock already unlocked node");
  /* Uncomment these two test cases if PLM is enabled */
  test_case_add(7, saClmClusterTrack_31,
                "saClmClusterTrack_4 with START STEP,VALIDATE STEP and changes"
                " - PLM admin trylock");
  test_case_add(7, saClmPlm_unlock, "PLM admin Unlock");
}
