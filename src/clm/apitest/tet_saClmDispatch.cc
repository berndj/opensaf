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

#include "clmtest.h"
#include "base/ncs_main_papi.h"

static void nodeGetCallBack1(SaInvocationT invocation,
                             const SaClmClusterNodeT *clusterNode,
                             SaAisErrorT error) {
  printf("Inside nodeGetCallBack1");
  printf("error= %d", error);
}

SaClmCallbacksT clmCallbacks = {nodeGetCallBack1, nullptr};
static SaClmNodeIdT nodeId;
static SaInvocationT invocation;

void saClmDispatch_01() {
  struct pollfd fds[1];
  int ret;
  nodeId = ncs_get_node_id(); /*node does not exist*/
  invocation = 600;
  SaAisErrorT rc;

  safassert(ClmTest::saClmInitialize(&clmHandle, &clmCallbacks, &clmVersion_1),
            SA_AIS_OK);
  safassert(ClmTest::saClmSelectionObjectGet(clmHandle, &selectionObject),
            SA_AIS_OK);
  safassert(ClmTest::saClmClusterNodeGetAsync(clmHandle, invocation, nodeId),
            SA_AIS_OK);

  fds[0].fd = (int)selectionObject;
  fds[0].events = POLLIN;
  ret = poll(fds, 1, 1000);
  assert(ret == 1);

  rc = ClmTest::saClmDispatch(clmHandle, SA_DISPATCH_ALL);
  safassert(ClmTest::saClmFinalize(clmHandle), SA_AIS_OK);
  test_validate(rc, SA_AIS_OK);
}

void saClmDispatch_02() {
  rc = ClmTest::saClmDispatch(0, SA_DISPATCH_ALL);
  test_validate(rc, SA_AIS_ERR_BAD_HANDLE);
}

void saClmDispatch_03() {
  safassert(ClmTest::saClmInitialize(&clmHandle, &clmCallbacks, &clmVersion_1),
            SA_AIS_OK);
  rc = ClmTest::saClmDispatch(clmHandle, static_cast<SaDispatchFlagsT>(0));
  safassert(ClmTest::saClmFinalize(clmHandle), SA_AIS_OK);
  test_validate(rc, SA_AIS_ERR_INVALID_PARAM);
}

void saClmDispatch_04() {
  struct pollfd fds[1];
  int ret;
  nodeId = ncs_get_node_id(); /*node does not exist*/
  invocation = 600;
  SaAisErrorT rc;

  safassert(ClmTest::saClmInitialize(&clmHandle, &clmCallbacks, &clmVersion_1),
            SA_AIS_OK);
  safassert(ClmTest::saClmSelectionObjectGet(clmHandle, &selectionObject),
            SA_AIS_OK);
  safassert(ClmTest::saClmClusterNodeGetAsync(clmHandle, invocation, nodeId),
            SA_AIS_OK);

  fds[0].fd = (int)selectionObject;
  fds[0].events = POLLIN;
  ret = poll(fds, 1, 1000);
  assert(ret == 1);

  rc = ClmTest::saClmDispatch(clmHandle, SA_DISPATCH_ONE);
  safassert(ClmTest::saClmFinalize(clmHandle), SA_AIS_OK);
  test_validate(rc, SA_AIS_OK);
}

__attribute__((constructor)) static void saClmDispatch_constructor() {
  test_suite_add(3, "Life Cykel API 4");
  test_case_add(3, saClmDispatch_01,
                "saClmDispatch - SA_AIS_OK SA_DISPATCH_ALL");
  test_case_add(3, saClmDispatch_02,
                "saClmDispatch - invalid handle SA_AIS_ERR_BAD_HANDLE");
  test_case_add(3, saClmDispatch_03,
                "saClmDispatch - zero flag SA_AIS_ERR_INVALID_PARAM");
  test_case_add(3, saClmDispatch_04,
                "saClmDispatch - SA_AIS_OK SA_DISPATCH_ONE");
}
