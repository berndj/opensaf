/*      -*- OpenSAF  -*-
 *
 * (C) Copyright 2016 The OpenSAF Foundation
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

#include <pthread.h>
#include <saAis.h>
#include <saImm.h>
#include <saImmOi.h>
#include <saImmOm.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include "clm/apitest/clmtest.h"

// Node name that will be compared in
static const char *s_clmTrackCallback_node;
// clmTrackCallback callback
static SaAisErrorT
    s_clmTrackCallback_err;  // Error code from clmTrackCallback callback
static int s_existTestNode;  // 0 = not tested, -1 = doesn't exist, 1 = exists
static int s_longDnAllowed;  // 0 = not tested, -1 = not allowed, 1 = allowed
static const char *s_testNodeName =
    "safNode=PL-123456789012345678901234567890"
    "123456789012345678901234567890,safCluster=myClmCluster";

static int testNodeExist() {
  SaAisErrorT rc;
  SaImmHandleT immHandle;
  SaImmAccessorHandleT accessorHandle;
  SaVersionT version = {'A', 2, 15};
  const SaImmAttrNameT attributeNames[1] = {nullptr};

  if (s_existTestNode) {
    return s_existTestNode;
  }

  safassert(saImmOmInitialize(&immHandle, nullptr, &version), SA_AIS_OK);
  safassert(saImmOmAccessorInitialize(immHandle, &accessorHandle), SA_AIS_OK);

  rc = saImmOmAccessorGet_o3(accessorHandle, s_testNodeName, attributeNames,
                             nullptr);
  assert(rc == SA_AIS_OK || rc == SA_AIS_ERR_NOT_EXIST);
  if (rc == SA_AIS_ERR_NOT_EXIST) {
    s_existTestNode = -1;
  } else if (rc == SA_AIS_OK) {
    s_existTestNode = 1;
  }

  saImmOmAccessorFinalize(accessorHandle);
  saImmOmFinalize(immHandle);

  return s_existTestNode;
}

static SaClmNodeIdT getClmNodeId(const char *nodeName) {
  SaImmHandleT immHandle;
  SaImmAccessorHandleT accessorHandle;
  SaVersionT version = {'A', 2, 15};
  SaImmAttrNameT attrNodeId = const_cast<SaImmAttrNameT>("saClmNodeID");
  SaImmAttrNameT attrNames[2] = {attrNodeId, nullptr};
  SaImmAttrValuesT_2 **attributes = nullptr;
  SaClmNodeIdT ret = 0;
  int i;

  safassert(saImmOmInitialize(&immHandle, nullptr, &version), SA_AIS_OK);
  safassert(saImmOmAccessorInitialize(immHandle, &accessorHandle), SA_AIS_OK);
  safassert(saImmOmAccessorGet_o3(accessorHandle, (SaConstStringT)nodeName,
                                  attrNames, &attributes),
            SA_AIS_OK);

  for (i = 0; attributes[i]; ++i) {
    if (!strcmp(attrNodeId, attributes[i]->attrName) &&
        attributes[i]->attrValuesNumber == 1 &&
        attributes[i]->attrValueType == SA_IMM_ATTR_SAUINT32T) {
      ret = *(SaClmNodeIdT *)attributes[i]->attrValues[0];
      break;
    }
  }

  saImmOmAccessorFinalize(accessorHandle);
  saImmOmFinalize(immHandle);

  return ret;
}

static SaClmNodeIdT isLongDNAllowed() {
  SaImmHandleT immHandle;
  SaImmAccessorHandleT accessorHandle;
  SaVersionT version = {'A', 2, 15};
  SaImmAttrNameT attrName = const_cast<SaImmAttrNameT>("longDnsAllowed");
  SaImmAttrNameT attrNames[2] = {attrName, nullptr};
  SaImmAttrValuesT_2 **attributes = nullptr;
  SaConstStringT immObjectName = "opensafImm=opensafImm,safApp=safImmService";
  int i;

  if (s_longDnAllowed) {
    return s_longDnAllowed;
  }

  safassert(saImmOmInitialize(&immHandle, nullptr, &version), SA_AIS_OK);
  safassert(saImmOmAccessorInitialize(immHandle, &accessorHandle), SA_AIS_OK);
  safassert(saImmOmAccessorGet_o3(accessorHandle, immObjectName, attrNames,
                                  &attributes),
            SA_AIS_OK);

  for (i = 0; attributes[i]; ++i) {
    if (!strcmp(attrName, attributes[i]->attrName) &&
        attributes[i]->attrValuesNumber == 1 &&
        attributes[i]->attrValueType == SA_IMM_ATTR_SAUINT32T) {
      s_longDnAllowed = *(SaClmNodeIdT *)attributes[i]->attrValues[0];
      break;
    }
  }

  saImmOmAccessorFinalize(accessorHandle);
  saImmOmFinalize(immHandle);

  return s_longDnAllowed;
}

static void skipTest() { printf("       SKIPPED"); }

static void nodeGetCallBack(SaInvocationT invocation,
                            const SaClmClusterNodeT *clusterNode,
                            SaAisErrorT error) {}

static void nodeGetCallBack4(SaInvocationT invocation,
                             const SaClmClusterNodeT_4 *clusterNode,
                             SaAisErrorT error) {}

static void clmTrackCallback(
    const SaClmClusterNotificationBufferT *notificationBuffer,
    SaUint32T numberOfMembers, SaAisErrorT error) {
  SaUint32T i;
  char nodename[1024];

  for (i = 0; i < notificationBuffer->numberOfItems; i++) {
    memcpy(nodename,
           notificationBuffer->notification[i].clusterNode.nodeName.value,
           notificationBuffer->notification[i].clusterNode.nodeName.length);
    nodename[notificationBuffer->notification[i].clusterNode.nodeName.length] =
        0;

    // Found node name
    if (!strcmp(nodename, s_clmTrackCallback_node)) {
      s_clmTrackCallback_err = SA_AIS_OK;
      break;
    }
  }
}

static void clmTrackCallback4(
    const SaClmClusterNotificationBufferT_4 *notificationBuffer,
    SaUint32T numberOfMembers, SaInvocationT invocation,
    const SaNameT *rootCauseEntity, const SaNtfCorrelationIdsT *correlationIds,
    SaClmChangeStepT step, SaTimeT timeSupervision, SaAisErrorT error) {
  SaUint32T i;
  char nodename[1024];

  for (i = 0; i < notificationBuffer->numberOfItems; i++) {
    memcpy(nodename,
           notificationBuffer->notification[i].clusterNode.nodeName.value,
           notificationBuffer->notification[i].clusterNode.nodeName.length);
    nodename[notificationBuffer->notification[i].clusterNode.nodeName.length] =
        0;

    // Found node name
    if (!strcmp(nodename, s_clmTrackCallback_node)) {
      s_clmTrackCallback_err = SA_AIS_OK;
      break;
    }
  }
}

static SaClmCallbacksT_4 clmCallback4 = {nodeGetCallBack4, clmTrackCallback4};
static SaClmCallbacksT clmCallback = {nodeGetCallBack, clmTrackCallback};

static void unlock_node(const char *nodename) {
  int rc;
  char command[1024];

  // Unlock the node
  snprintf(command, sizeof(command), "immadm -o 1 %s", nodename);
  rc = system(command);
  assert(rc != -1);
}

static void lock_node(const char *nodename) {
  int rc;
  char command[1024];
  // Lock the node
  snprintf(command, sizeof(command), "immadm -o 2 %s", nodename);
  rc = system(command);
  assert(rc != -1);
}

static void remove_node(const char *nodename) {
  int rc;
  char command[1024];

  // Lock the node
  snprintf(command, sizeof(command), "immadm -o 2 %s", nodename);
  rc = system(command);
  assert(rc != -1);

  // Remove the node
  snprintf(command, sizeof(command), "immcfg -d %s", nodename);
  rc = system(command);
  assert(rc != -1);
}

static void saClmLongRdn_01() {
  SaImmHandleT immHandle;
  SaImmAdminOwnerHandleT ownerHandle;
  SaImmCcbHandleT ccbHandle;
  SaImmAdminOwnerNameT ownerName = (SaImmAdminOwnerNameT) __FUNCTION__;
  SaConstStringT parent = "safCluster=myClmCluster";
  SaConstStringT parentNames[] = {parent, nullptr};
  // hostname is 63 character long
  SaConstStringT nodeName =
      "safNode=PL-ABCDEFGHIJ12345678901234567890123456789012345678901234567890,"
      "safCluster=myClmCluster";
  SaVersionT version = {'A', 2, 15};
  int rc;

  if (isLongDNAllowed() != 1) {
    skipTest();
    return;
  }

  safassert(saImmOmInitialize(&immHandle, nullptr, &version), SA_AIS_OK);
  safassert(
      saImmOmAdminOwnerInitialize(immHandle, ownerName, SA_TRUE, &ownerHandle),
      SA_AIS_OK);
  safassert(saImmOmAdminOwnerSet_o3(ownerHandle, parentNames, SA_IMM_ONE),
            SA_AIS_OK);
  safassert(saImmOmCcbInitialize(ownerHandle, 0, &ccbHandle), SA_AIS_OK);

  // Create a node with long RDN
  safassert(saImmOmCcbObjectCreate_o3(ccbHandle,
                                      const_cast<SaImmClassNameT>("SaClmNode"),
                                      nodeName, nullptr),
            SA_AIS_OK);

  rc = saImmOmCcbApply(ccbHandle);
  test_validate(rc, SA_AIS_OK);

  saImmOmCcbFinalize(ccbHandle);
  saImmOmAdminOwnerFinalize(ownerHandle);
  saImmOmFinalize(immHandle);

  // Remove new created node
  remove_node((char *)nodeName);
}

void saClmLongRdn_02() {
  struct pollfd fds[1];
  int rc;
  const char *nodeName = s_testNodeName;
  SaUint8T trackFlags = SA_TRACK_CHANGES;

  if (isLongDNAllowed() != 1) {
    skipTest();
    return;
  }

  // Test node does not exist. Skip the test
  if (testNodeExist() != 1) {
    skipTest();
    return;
  }

  safassert(
      ClmTest::saClmInitialize_4(&clmHandle, &clmCallback4, &clmVersion_4),
      SA_AIS_OK);
  safassert(ClmTest::saClmSelectionObjectGet(clmHandle, &selectionObject),
            SA_AIS_OK);
  rc = ClmTest::saClmClusterTrack_4(clmHandle, trackFlags, nullptr);
  if (rc != SA_AIS_OK) {
    safassert(ClmTest::saClmFinalize(clmHandle), SA_AIS_OK);
    // Failed at saClmClusterTrack_4
    test_validate(rc, SA_AIS_OK);
    return;
  }

  fds[0].fd = (int)selectionObject;
  fds[0].events = POLLIN;

  // Set failed error code
  s_clmTrackCallback_err = SA_AIS_ERR_NOT_EXIST;
  // Set node name that will be compared in CLM callback
  s_clmTrackCallback_node = nodeName;

  lock_node(nodeName);

  while (1) {
    rc = poll(fds, 1, 2000);
    assert(rc == 1);
    if (fds[0].revents & POLLIN) break;
  }

  safassert(ClmTest::saClmDispatch(clmHandle, SA_DISPATCH_ALL), SA_AIS_OK);
  safassert(ClmTest::saClmClusterTrackStop(clmHandle), SA_AIS_OK);
  safassert(ClmTest::saClmFinalize(clmHandle), SA_AIS_OK);
  test_validate(s_clmTrackCallback_err, SA_AIS_OK);

  unlock_node(nodeName);
}

void saClmLongRdn_03() {
  struct pollfd fds[1];
  int rc;
  const char *nodeName = s_testNodeName;
  SaUint8T trackFlags = SA_TRACK_CHANGES;

  if (isLongDNAllowed() != 1) {
    skipTest();
    return;
  }

  // Test node does not exist. Skip the test
  if (testNodeExist() != 1) {
    skipTest();
    return;
  }

  safassert(ClmTest::saClmInitialize(&clmHandle, &clmCallback, &clmVersion_1),
            SA_AIS_OK);
  safassert(ClmTest::saClmSelectionObjectGet(clmHandle, &selectionObject),
            SA_AIS_OK);
  rc = ClmTest::saClmClusterTrack(clmHandle, trackFlags, nullptr);
  if (rc != SA_AIS_OK) {
    safassert(ClmTest::saClmFinalize(clmHandle), SA_AIS_OK);
    // Failed at saClmClusterTrack
    test_validate(rc, SA_AIS_OK);
    return;
  }

  fds[0].fd = (int)selectionObject;
  fds[0].events = POLLIN;

  // Set failed error code
  s_clmTrackCallback_err = SA_AIS_ERR_NOT_EXIST;
  // Set node name that will be compared in CLM callback
  s_clmTrackCallback_node = nodeName;

  lock_node(nodeName);

  while (1) {
    rc = poll(fds, 1, 2000);
    assert(rc == 1);
    if (fds[0].revents & POLLIN) break;
  }

  safassert(ClmTest::saClmDispatch(clmHandle, SA_DISPATCH_ALL), SA_AIS_OK);
  safassert(ClmTest::saClmClusterTrackStop(clmHandle), SA_AIS_OK);
  safassert(ClmTest::saClmFinalize(clmHandle), SA_AIS_OK);
  test_validate(s_clmTrackCallback_err, SA_AIS_OK);

  unlock_node(nodeName);
}

void saClmLongRdn_04() {
  SaImmHandleT immHandle;
  SaImmAdminOwnerHandleT ownerHandle;
  SaImmCcbHandleT ccbHandle;
  SaImmAdminOwnerNameT ownerName = (SaImmAdminOwnerNameT) __FUNCTION__;
  int rc;
  SaConstStringT parent = "safCluster=myClmCluster";
  SaConstStringT parentNames[] = {parent, nullptr};
  SaVersionT version = {'A', 2, 15};
  // Length of nodeName == 256
  const char *nodeName =
      "safNode=PL-123456789012345678901234567890123456789012345678901234567890"
      "123456789012345678901234567890123456789012345678901234567890"
      "123456789012345678901234567890123456789012345678901234567890"
      "12345678901234567890123456789012345678901,safCluster=myClmCluster";

  if (isLongDNAllowed() != 1) {
    skipTest();
    return;
  }

  // Create a test node
  safassert(saImmOmInitialize(&immHandle, nullptr, &version), SA_AIS_OK);
  safassert(
      saImmOmAdminOwnerInitialize(immHandle, ownerName, SA_TRUE, &ownerHandle),
      SA_AIS_OK);
  safassert(saImmOmAdminOwnerSet_o3(ownerHandle, parentNames, SA_IMM_ONE),
            SA_AIS_OK);
  safassert(saImmOmCcbInitialize(ownerHandle, 0, &ccbHandle), SA_AIS_OK);

  // Create a node with long RDN
  // If long DN is enabled on client side, ccbObjectCreate will return
  // SA_AIS_ERR_FAILED_OPERATION Otherwise ccbObjectCreate will return
  // SA_AIS_ERR_INVALID_PARAM
  rc = saImmOmCcbObjectCreate_o3(
      ccbHandle, const_cast<SaImmClassNameT>("SaClmNode"), nodeName, nullptr);
  if (rc == SA_AIS_ERR_INVALID_PARAM) {
    test_validate(rc, SA_AIS_ERR_INVALID_PARAM);
  } else {
    test_validate(rc, SA_AIS_ERR_FAILED_OPERATION);
  }

  saImmOmCcbFinalize(ccbHandle);
  saImmOmAdminOwnerFinalize(ownerHandle);
  saImmOmFinalize(immHandle);
}

void saClmLongRdn_05() {
  int rc;
  const char *nodeName = s_testNodeName;
  SaClmNodeIdT nodeId;
  SaClmClusterNodeT_4 clusterNode;

  if (isLongDNAllowed() != 1) {
    skipTest();
    return;
  }

  // Test node does not exist. Skip the test
  if (testNodeExist() != 1) {
    skipTest();
    return;
  }

  nodeId = getClmNodeId(nodeName);
  assert(nodeId != 0);

  safassert(ClmTest::saClmInitialize_4(&clmHandle, nullptr, &clmVersion_4),
            SA_AIS_OK);

  rc = ClmTest::saClmClusterNodeGet_4(clmHandle, nodeId, 10000000000ll,
                                      &clusterNode);

  // SaNameT value cannot be longer than 255, so we don't need to use
  // saAisNameBorrow
  if (strcmp((char *)clusterNode.nodeName.value, nodeName)) {
    printf(" (node name is not the same) ");
    rc = SA_AIS_ERR_FAILED_OPERATION;
  }

  test_validate(rc, SA_AIS_OK);

  saClmFinalize(clmHandle);
}

void saClmLongRdn_06() {
  int rc;
  const char *nodeName = s_testNodeName;
  SaClmNodeIdT nodeId;
  SaClmClusterNodeT clusterNode;

  if (isLongDNAllowed() != 1) {
    skipTest();
    return;
  }

  // Test node does not exist. Skip the test
  if (testNodeExist() != 1) {
    skipTest();
    return;
  }

  nodeId = getClmNodeId(nodeName);
  assert(nodeId != 0);

  safassert(ClmTest::saClmInitialize(&clmHandle, nullptr, &clmVersion_1),
            SA_AIS_OK);

  rc = saClmClusterNodeGet(clmHandle, nodeId, 10000000000ll, &clusterNode);

  // SaNameT value cannot be longer than 255, so we don't need to use
  // saAisNameBorrow
  if (strcmp((char *)clusterNode.nodeName.value, nodeName)) {
    printf(" (node name is not the same) ");
    rc = SA_AIS_ERR_FAILED_OPERATION;
  }

  test_validate(rc, SA_AIS_OK);

  saClmFinalize(clmHandle);
}

__attribute__((constructor)) static void saClmLongRdn_constructor() {
  test_suite_add(11, "CLM Long RDN (long DN support must be allowed)");
  test_case_add(11, saClmLongRdn_01,
                "SA_AIS_OK - Create CLM node with long RDN");
  test_case_add(11, saClmLongRdn_02,
                "SA_AIS_OK - saClmClusterTrack_4 and callback with long RDN");
  test_case_add(11, saClmLongRdn_03,
                "SA_AIS_OK - saClmClusterTrack and callback with long RDN");
  test_case_add(11, saClmLongRdn_04,
                "SA_AIS_ERR_INVALID_PARAM or SA_AIS_ERR_FAILED_OPERATION"
                " - Long DN is not supported");
  test_case_add(11, saClmLongRdn_05,
                "SA_AIS_OK - saClmClusterNodeGet_4 with long DN");
  test_case_add(11, saClmLongRdn_06,
                "SA_AIS_OK - saClmClusterNodeGet with long DN");
}
