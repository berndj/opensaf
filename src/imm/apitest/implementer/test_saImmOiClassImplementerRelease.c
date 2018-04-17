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

void saImmOiClassImplementerRelease_01(void)
{
	const SaImmOiImplementerNameT implementerName =
	    (SaImmOiImplementerNameT) __FUNCTION__;
	safassert(immutil_saImmOmInitialize(&immOmHandle, NULL, &immVersion),
		  SA_AIS_OK);
	safassert(
	    immutil_saImmOiInitialize_2(&immOiHandle, &immOiCallbacks, &immVersion),
	    SA_AIS_OK);
	safassert(immutil_saImmOiImplementerSet(immOiHandle, implementerName),
		  SA_AIS_OK);
	safassert(immutil_saImmOiClassImplementerSet(immOiHandle, configClassName),
		  SA_AIS_OK);
	rc = immutil_saImmOiClassImplementerRelease(immOiHandle, configClassName);
	test_validate(rc, SA_AIS_OK);
	safassert(immutil_saImmOiFinalize(immOiHandle), SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);
}

void saImmOiClassImplementerRelease_02(void)
{
	const SaImmOiImplementerNameT implementerName =
	    (SaImmOiImplementerNameT) __FUNCTION__;
	safassert(immutil_saImmOmInitialize(&immOmHandle, NULL, &immVersion),
		  SA_AIS_OK);
	safassert(
	    immutil_saImmOiInitialize_2(&immOiHandle, &immOiCallbacks, &immVersion),
	    SA_AIS_OK);
	safassert(immutil_saImmOiImplementerSet(immOiHandle, implementerName),
		  SA_AIS_OK);
	safassert(immutil_saImmOiClassImplementerSet(immOiHandle, configClassName),
		  SA_AIS_OK);
	rc = immutil_saImmOiClassImplementerRelease(-1, configClassName);
	safassert(immutil_saImmOiClassImplementerRelease(immOiHandle, configClassName),
		  SA_AIS_OK);
	safassert(immutil_saImmOiFinalize(immOiHandle), SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);

	test_validate(rc, SA_AIS_ERR_BAD_HANDLE);
}

void saImmOiClassImplementerRelease_03(void)
{
	const SaImmOiImplementerNameT implementerName =
	    (SaImmOiImplementerNameT) __FUNCTION__;
	SaImmClassNameT nonExistingClassName =
	    (SaImmClassNameT) "nonExistingClassName";
	safassert(immutil_saImmOmInitialize(&immOmHandle, NULL, &immVersion),
		  SA_AIS_OK);
	safassert(
	    immutil_saImmOiInitialize_2(&immOiHandle, &immOiCallbacks, &immVersion),
	    SA_AIS_OK);
	safassert(immutil_saImmOiImplementerSet(immOiHandle, implementerName),
		  SA_AIS_OK);
	safassert(immutil_saImmOiClassImplementerSet(immOiHandle, configClassName),
		  SA_AIS_OK);
	rc = immutil_saImmOiClassImplementerRelease(immOiHandle, nonExistingClassName);
	safassert(immutil_saImmOiClassImplementerRelease(immOiHandle, configClassName),
		  SA_AIS_OK);
	safassert(immutil_saImmOiFinalize(immOiHandle), SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);

	test_validate(rc, SA_AIS_ERR_NOT_EXIST);
}
