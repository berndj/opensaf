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
 * Author(s): Ericsson AB
 *
 */

#include <sys/types.h>
#include <sys/wait.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include "clm/apitest/clmtest.h"

/* Modify saClmNodeLockCallbackTimeout1 attribute*/
void saClmOi_01() {
  int rc;
  char command[384];

  snprintf(command, sizeof(command),
           "immcfg -a saClmNodeLockCallbackTimeout=4000000000 safNode=%s,"
           "safCluster=myClmCluster",
           node_name.value);

  rc = system(command);
  assert(rc != -1);
  test_validate(WEXITSTATUS(rc), 0);
}

/*Modify saClmNodeDisableReboot1 attribute*/
void saClmOi_02() {
  int rc;
  char command[384];

  snprintf(
      command, sizeof(command),
      "immcfg -a saClmNodeDisableReboot=1 safNode=%s,safCluster=myClmCluster",
      node_name.value);
  rc = system(command);
  assert(rc != -1);
  test_validate(WEXITSTATUS(rc), 0);
}

/*Modify saClmNodeAddressFamily1 attribute */
void saClmOi_03() {
  int rc;
  char command[384];

  snprintf(
      command, sizeof(command),
      "immcfg -a saClmNodeAddressFamily=2 safNode=%s,safCluster=myClmCluster",
      node_name.value);
  rc = system(command);
  assert(rc != -1);
  test_validate(WEXITSTATUS(rc), 1);
}

/* Modify saClmNodeAddress1 attribute */
void saClmOi_04() {
  int rc;
  char command[384];

  snprintf(
      command, sizeof(command),
      "immcfg -a  saClmNodeAddress=10.130.100.186 safNode=%s,safCluster=myClmCluster",
      node_name.value);
  rc = system(command);
  assert(rc != -1);
  test_validate(WEXITSTATUS(rc), 1);
}

/*Modify saClmNodeEE1 attribute */
void saClmOi_05() {
  int rc;
  char command[384];
  char new_eename[] = "NewEEName";

  snprintf(command, sizeof(command),
           "immcfg -a saClmNodeEE=%s safNode=%s,safCluster=myClmCluster",
           new_eename, node_name.value);
  rc = system(command);
  assert(rc != -1);
  test_validate(WEXITSTATUS(rc), 1);
}

/*Delete the member which is a member*/
void saClmOi_06() {
  int rc;
  char command[384];
  snprintf(command, sizeof(command),
           "immcfg -d  safNode=%s,safCluster=myClmCluster", node_name.value);
  rc = system(command);
  assert(rc != -1);
  test_validate(WEXITSTATUS(rc), 1);
}

/*Delete the Object which is not member and locked */
void saClmOi_07() {
  int rc;

  char command[256];
  char command1[256];
  char new_obj_name[] = "safNode=node,safCluster=myClmCluster";
  /*Lets first create the object- for this member will be false*/

  snprintf(command, sizeof(command), "immcfg -c SaClmNode safNode=%s",
           new_obj_name);
  snprintf(command1, sizeof(command1), "immcfg -d %s", new_obj_name);
  rc = system(command);
  assert(rc != -1);
  rc = system(command1);
  assert(rc != -1);
  test_validate(WEXITSTATUS(rc), 1);
}

/*Create object*/
void saClmOi_08() {
  int rc;

  char command[256];
  char new_obj_name[] = "safNode=new_node,safCluster=myClmCluster";

  snprintf(command, sizeof(command), "immcfg -c SaClmNode safNode=%s",
           new_obj_name);
  rc = system(command);
  assert(rc != -1);
  test_validate(WEXITSTATUS(rc), 0);
}

__attribute__((constructor)) static void saClmOiOperations_constructor() {
  test_suite_add(4, "CLM OI tests");
  test_case_add(4, saClmOi_01,
                "CCB Object Modify saClmNodeLockCallbackTimeout");
  test_case_add(4, saClmOi_02, "CCB Object Modify saClmNodeDisableReboot");
  test_case_add(4, saClmOi_03, "CCB Object Modify saClmNodeAddressFamily");
  test_case_add(4, saClmOi_04, "CCB Object Modify saClmNodeAddress");
  test_case_add(4, saClmOi_05, "CCB Object Modify saClmNodeEE");
  test_case_add(4, saClmOi_06, "CCB Object Delete the member node");
  /*test_case_add(4, saClmOi_07, "CCB Object Delete the non-member node
    but unlocked"); test_case_add(4, saClmOi_08, "CCB Object Create
    Object");*/
  /*One more test case needs to be added after the node is locked*/
}
