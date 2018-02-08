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

#include "clm/apitest/clmtest.h"

void saClmFinalize_01() {
  safassert(
      ClmTest::saClmInitialize(&clmHandle, &clmCallbacks_1, &clmVersion_1),
      SA_AIS_OK);
  rc = ClmTest::saClmFinalize(clmHandle);
  test_validate(rc, SA_AIS_OK);
  safassert(
      ClmTest::saClmInitialize_4(&clmHandle, &clmCallbacks_4, &clmVersion_4),
      SA_AIS_OK);
  rc = ClmTest::saClmFinalize(clmHandle);
  test_validate(rc, SA_AIS_OK);
}

void saClmFinalize_02() {
  rc = ClmTest::saClmFinalize(-1);
  test_validate(rc, SA_AIS_ERR_BAD_HANDLE);
}

void saClmFinalize_03() {
  safassert(
      ClmTest::saClmInitialize(&clmHandle, &clmCallbacks_1, &clmVersion_1),
      SA_AIS_OK);
  safassert(ClmTest::saClmFinalize(clmHandle), SA_AIS_OK);
  rc = ClmTest::saClmFinalize(clmHandle);
  test_validate(rc, SA_AIS_ERR_BAD_HANDLE);
}

__attribute__((constructor)) static void saClmFinalize_constructor() {
  test_suite_add(2, "Test case for saClmFinalize");
  test_case_add(2, saClmFinalize_01, "saClmFinalize SA_AIS_OK");
  test_case_add(2, saClmFinalize_02,
                "saClmFinalize SA_AIS_ERR_BAD_HANDLE - invalid handle");
  test_case_add(
      2, saClmFinalize_03,
      "saClmFinalize SA_AIS_ERR_BAD_HANDLE - handle already returned");
}
