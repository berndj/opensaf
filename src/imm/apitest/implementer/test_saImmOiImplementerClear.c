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

void saImmOiImplementerClear_01(void)
{
	SaImmOiImplementerNameT implementerName =
	    (SaImmOiImplementerNameT) __FUNCTION__;

	safassert(
	    immutil_saImmOiInitialize_2(&immOiHandle, &immOiCallbacks, &immVersion),
	    SA_AIS_OK);
	safassert(immutil_saImmOiImplementerSet(immOiHandle, implementerName),
		  SA_AIS_OK);
	rc = immutil_saImmOiImplementerClear(immOiHandle);
	test_validate(rc, SA_AIS_OK);
	safassert(immutil_saImmOiFinalize(immOiHandle), SA_AIS_OK);
}

void saImmOiImplementerClear_02(void)
{
	safassert(
	    immutil_saImmOiInitialize_2(&immOiHandle, &immOiCallbacks, &immVersion),
	    SA_AIS_OK);

	/* not associated with an implementer name. */
	if ((rc = immutil_saImmOiImplementerClear(immOiHandle)) !=
	    SA_AIS_ERR_BAD_HANDLE)
		goto done;

	/* invalid */
	rc = immutil_saImmOiImplementerClear(-1);

done:
	safassert(immutil_saImmOiFinalize(immOiHandle), SA_AIS_OK);
	test_validate(rc, SA_AIS_ERR_BAD_HANDLE);
}
