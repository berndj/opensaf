/*      -*- OpenSAF  -*-
 *
 * (C) Copyright 2008 The OpenSAF Foundation
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

#include "imm/apitest/immtest.h"

void saImmOmAdminOwnerRelease_01(void)
{
	const SaImmAdminOwnerNameT adminOwnerName =
	    (SaImmAdminOwnerNameT) __FUNCTION__;
	SaImmAdminOwnerHandleT ownerHandle;
	const SaNameT *objectNames[] = {&rootObj, NULL};

	safassert(immutil_saImmOmInitialize(&immOmHandle, &immOmCallbacks, &immVersion),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerInitialize(immOmHandle, adminOwnerName,
					      SA_TRUE, &ownerHandle),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerSet(ownerHandle, objectNames, SA_IMM_ONE),
		  SA_AIS_OK);
	test_validate(
	    immutil_saImmOmAdminOwnerRelease(ownerHandle, objectNames, SA_IMM_ONE),
	    SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerFinalize(ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);
}

void saImmOmAdminOwnerRelease_02(void)
{
	const SaImmAdminOwnerNameT adminOwnerName =
	    (SaImmAdminOwnerNameT) __FUNCTION__;
	SaImmAdminOwnerHandleT ownerHandle;
	const SaNameT *objectNames[] = {&rootObj, NULL};

	safassert(immutil_saImmOmInitialize(&immOmHandle, &immOmCallbacks, &immVersion),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerInitialize(immOmHandle, adminOwnerName,
					      SA_TRUE, &ownerHandle),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerSet(ownerHandle, objectNames, SA_IMM_ONE),
		  SA_AIS_OK);
	test_validate(immutil_saImmOmAdminOwnerRelease(-1, objectNames, SA_IMM_ONE),
		      SA_AIS_ERR_BAD_HANDLE);
	safassert(immutil_saImmOmAdminOwnerFinalize(ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);
}

void saImmOmAdminOwnerRelease_03(void)
{
	const SaImmAdminOwnerNameT adminOwnerName =
	    (SaImmAdminOwnerNameT) __FUNCTION__;
	SaImmAdminOwnerHandleT ownerHandle;
	const SaNameT *objectNames[] = {&rootObj, NULL};

	safassert(immutil_saImmOmInitialize(&immOmHandle, &immOmCallbacks, &immVersion),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerInitialize(immOmHandle, adminOwnerName,
					      SA_TRUE, &ownerHandle),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerSet(ownerHandle, objectNames, SA_IMM_ONE),
		  SA_AIS_OK);
	test_validate(immutil_saImmOmAdminOwnerRelease(ownerHandle, NULL, SA_IMM_ONE),
		      SA_AIS_ERR_INVALID_PARAM);
	safassert(immutil_saImmOmAdminOwnerFinalize(ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);
}

void saImmOmAdminOwnerRelease_04(void)
{
	const SaImmAdminOwnerNameT adminOwnerName =
	    (SaImmAdminOwnerNameT) __FUNCTION__;
	SaImmAdminOwnerHandleT ownerHandle;
	const SaNameT *objectNames[] = {&rootObj, NULL};

	safassert(immutil_saImmOmInitialize(&immOmHandle, &immOmCallbacks, &immVersion),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerInitialize(immOmHandle, adminOwnerName,
					      SA_TRUE, &ownerHandle),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerSet(ownerHandle, objectNames, SA_IMM_ONE),
		  SA_AIS_OK);
	test_validate(immutil_saImmOmAdminOwnerRelease(ownerHandle, objectNames, -1),
		      SA_AIS_ERR_INVALID_PARAM);
	safassert(immutil_saImmOmAdminOwnerFinalize(ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);
}

void saImmOmAdminOwnerRelease_05(void)
{
	const SaImmAdminOwnerNameT adminOwnerName =
	    (SaImmAdminOwnerNameT) __FUNCTION__;
	SaImmAdminOwnerHandleT ownerHandle;
	const SaNameT objectName = {strlen("nonExistingObject"),
				    "nonExistingObject"};
	const SaNameT *objectNames[] = {&rootObj, NULL};
	const SaNameT *objectNames1[] = {&objectName, NULL};
	const SaNameT *objectNames2[] = {&rootObj, &objectName, NULL};
	const SaNameT *objectNames3[] = {&objectName, &rootObj, NULL};

	safassert(immutil_saImmOmInitialize(&immOmHandle, &immOmCallbacks, &immVersion),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerInitialize(immOmHandle, adminOwnerName,
					      SA_TRUE, &ownerHandle),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerSet(ownerHandle, objectNames, SA_IMM_ONE),
		  SA_AIS_OK);

	/*
	** "At least one of the names specified by objectNames is
	** not the name of an existing object"
	*/
	rc = immutil_saImmOmAdminOwnerRelease(ownerHandle, objectNames1, SA_IMM_ONE);
	if (rc != SA_AIS_ERR_NOT_EXIST) {
		TRACE_ENTER();
		goto done;
	}

	/* Check some permutations */
	rc = immutil_saImmOmAdminOwnerRelease(ownerHandle, objectNames2, SA_IMM_ONE);
	if (rc != SA_AIS_ERR_NOT_EXIST) {
		TRACE_ENTER();
		goto done;
	}

	rc = immutil_saImmOmAdminOwnerRelease(ownerHandle, objectNames3, SA_IMM_ONE);
	if (rc != SA_AIS_ERR_NOT_EXIST) {
		TRACE_ENTER();
		goto done;
	}

	safassert(
	    immutil_saImmOmAdminOwnerRelease(ownerHandle, objectNames, SA_IMM_ONE),
	    SA_AIS_OK);

	/*
	** "at least one of the objects targeted by this operation is not owned
	** by the administrative owner whose name was used to initialize
	** ownerHandle.
	*/
	rc = immutil_saImmOmAdminOwnerRelease(ownerHandle, objectNames, SA_IMM_ONE);

done:
	test_validate(rc, SA_AIS_ERR_NOT_EXIST);
	safassert(immutil_saImmOmAdminOwnerFinalize(ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);
}
