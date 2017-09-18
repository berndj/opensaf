/*      -*- OpenSAF  -*-
 *
 * (C) Copyright 2008 The OpenSAF Foundation
 * Copyright Ericsson AB 2008, 2017 - All Rights Reserved.
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

#include "logtest.h"

void saLogInitialize_01(void)
{
	SaVersionT log_version = kLogVersion;
	rc = saLogInitialize(&logHandle, &logCallbacks, &log_version);
	logFinalize();
	test_validate(rc, SA_AIS_OK);
}

void saLogInitialize_02(void)
{
	SaVersionT log_version = kLogVersion;
	rc = saLogInitialize(NULL, &logCallbacks, &log_version);
	test_validate(rc, SA_AIS_ERR_INVALID_PARAM);
}

void saLogInitialize_03(void)
{
	SaVersionT log_version = kLogVersion;
	rc = saLogInitialize(&logHandle, NULL, &log_version);
	logFinalize();
	test_validate(rc, SA_AIS_OK);
}

void saLogInitialize_04(void)
{
	rc = saLogInitialize(&logHandle, NULL, NULL);
	test_validate(rc, SA_AIS_ERR_INVALID_PARAM);
}

void saLogInitialize_05(void)
{
	SaVersionT log_version = kLogVersion;
	rc = saLogInitialize(0, &logCallbacks, &log_version);
	test_validate(rc, SA_AIS_ERR_INVALID_PARAM);
}

void saLogInitialize_06(void)
{
	SaVersionT log_version = {0, 0, 0};

	rc = saLogInitialize(&logHandle, &logCallbacks, &log_version);
	test_validate(rc, SA_AIS_ERR_VERSION);
}

void saLogInitialize_07(void)
{
	SaVersionT log_version = {'B', 1, 1};

	rc = saLogInitialize(&logHandle, &logCallbacks, &log_version);
	test_validate(rc, SA_AIS_ERR_VERSION);
}

void saLogInitialize_08(void)
{
	SaVersionT log_version = {'A', 2, 1};

	rc = saLogInitialize(&logHandle, &logCallbacks, &log_version);
	logFinalize();
	test_validate(rc, SA_AIS_OK);
}

void saLogInitialize_09(void)
{
	SaVersionT log_version = {'A', 3, 0};

	rc = saLogInitialize(&logHandle, &logCallbacks, &log_version);
	test_validate(rc, SA_AIS_ERR_VERSION);
}

void saLogInitialize_10(void)
{
	SaVersionT log_version = kLogVersion;

	log_version.minorVersion += 1;
	rc = saLogInitialize(&logHandle, &logCallbacks, &log_version);
	logFinalize();

	test_validate(rc, SA_AIS_ERR_VERSION);
}

void saLogInitialize_11(void)
{
	SaVersionT log_version = {'A', 2};

	rc = saLogInitialize(&logHandle, &logCallbacks, &log_version);
	logFinalize();

	test_validate(rc, SA_AIS_OK);
}

extern void saLogSelectionObjectGet_01(void);
extern void saLogSelectionObjectGet_02(void);
extern void saLogFinalize_01(void);
extern void saLogFinalize_02(void);
extern void saLogDispatch_01(void);

__attribute__((constructor)) static void saLibraryLifeCycle_constructor(void)
{
	test_suite_add(1, "Library Life Cycle");
	test_case_add(1, saLogInitialize_01, "saLogInitialize() OK");
	test_case_add(1, saLogInitialize_02,
		      "saLogInitialize() with NULL pointer to handle");
	test_case_add(1, saLogInitialize_03,
		      "saLogInitialize() with NULL pointer to callbacks");
	test_case_add(1, saLogInitialize_04,
		      "saLogInitialize() with NULL callbacks AND version");
	test_case_add(1, saLogInitialize_05,
		      "saLogInitialize() with uninitialized handle");
	test_case_add(1, saLogInitialize_06,
		      "saLogInitialize() with uninitialized version");
	test_case_add(1, saLogInitialize_07,
		      "saLogInitialize() with too high release level");
	test_case_add(1, saLogInitialize_08,
		      "saLogInitialize() with minor version set to 1");
	test_case_add(1, saLogInitialize_09,
		      "saLogInitialize() with major version set to 3");
	test_case_add(
	    1, saLogInitialize_10,
	    "saLogInitialize() with minor version is set bigger than supported version");
	test_case_add(1, saLogInitialize_11,
		      "saLogInitialize() with minor version is not set");
	test_case_add(1, saLogSelectionObjectGet_01,
		      "saLogSelectionObjectGet() OK");
	test_case_add(1, saLogSelectionObjectGet_02,
		      "saLogSelectionObjectGet() with NULL log handle");
	test_case_add(1, saLogDispatch_01, "saLogDispatch() OK");
	test_case_add(1, saLogFinalize_01, "saLogFinalize() OK");
	test_case_add(1, saLogFinalize_02,
		      "saLogFinalize() with NULL log handle");
}
