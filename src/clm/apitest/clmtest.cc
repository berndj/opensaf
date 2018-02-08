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

#include <sys/time.h>
#include <unistd.h>

#include "clmtest.h"
#include "base/ncs_main_papi.h"

/* Highest supported version*/
#define CLM_HIGHEST_SUPPORTED_VERSION \
  { 'B', 0x04, 0x01 }
#define CLM_LOWEST_SUPPORTED_VERSION \
  { 'B', 0x01, 0x01 }
#define CLM_INVALID_VERSION \
  { 'B', 0x02, 0x03 }

SaNameT node_name;

void clm_init() {
  FILE *fp;
  // Command list to execute before tests start
  const char *command[] = {
      // Unlock PL-3
      "immadm -o 1 safNode=PL-3,safCluster=myClmCluster 2> /dev/null"};
  int command_list_size = 1;
  int i;

  fp = fopen("/etc/opensaf/node_name", "r");
  if (fp == nullptr) {
    printf("Error: can't open file");
    assert(0);
  }

  int cnt = fscanf(fp, "%s", node_name.value);
  if (cnt == 1) {
    node_name.length = strlen((char *)node_name.value);
  }
  fclose(fp);

  // Execute commands
  for (i = 0; i < command_list_size; ++i) {
    /* Return code is not important, and will be ignored.
     * To avoid warn_unused_result warning on some compilers,
     * system is executed within empty 'if' statement */
    if (system(command[i])) {
    }
  }
}

const SaVersionT refVersion = CLM_HIGHEST_SUPPORTED_VERSION;
SaVersionT clmVersion_4 = CLM_HIGHEST_SUPPORTED_VERSION;
SaVersionT clmVersion_1 = CLM_LOWEST_SUPPORTED_VERSION;
SaVersionT clmVersion = CLM_INVALID_VERSION;
SaAisErrorT rc;
SaClmHandleT clmHandle;
SaClmCallbacksT_4 clmCallbacks_4 = {nullptr, nullptr};
SaClmCallbacksT clmCallbacks_1 = {nullptr, nullptr};
SaSelectionObjectT selectionObject;

int main(int argc, char **argv) {
  int suite = ALL_SUITES, tcase = ALL_TESTS;

  srandom(getpid());
  if (ncs_leap_startup() != NCSCC_RC_SUCCESS) {
    printf("LEAP startup failed\n");
    return 1;
  }
  clm_init();

  if (argc > 1) {
    suite = atoi(argv[1]);
  }

  if (argc > 2) {
    tcase = atoi(argv[2]);
  }

  if (suite == 0) {
    test_list();
    return 0;
  }

  return test_run(suite, tcase);
}
