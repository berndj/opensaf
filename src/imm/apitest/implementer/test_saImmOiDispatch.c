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
#include "osaf/immutil/immutil.h"

void saImmOiDispatch_01(void)
{
	safassert(
	    immutil_saImmOiInitialize_2(&immOiHandle, &immOiCallbacks, &immVersion),
	    SA_AIS_OK);
	test_validate(saImmOiDispatch(immOiHandle, SA_DISPATCH_ALL), SA_AIS_OK);
	safassert(immutil_saImmOiFinalize(immOiHandle), SA_AIS_OK);
}

void saImmOiDispatch_02(void)
{
	safassert(
	    immutil_saImmOiInitialize_2(&immOiHandle, &immOiCallbacks, &immVersion),
	    SA_AIS_OK);
	test_validate(saImmOiDispatch(immOiHandle, SA_DISPATCH_ONE), SA_AIS_OK);
	safassert(immutil_saImmOiFinalize(immOiHandle), SA_AIS_OK);
}

void saImmOiDispatch_03(void)
{
	safassert(
	    immutil_saImmOiInitialize_2(&immOiHandle, &immOiCallbacks, &immVersion),
	    SA_AIS_OK);
	test_validate(saImmOiDispatch(-1, SA_DISPATCH_ONE),
		      SA_AIS_ERR_BAD_HANDLE);
	safassert(immutil_saImmOiFinalize(immOiHandle), SA_AIS_OK);
}

void saImmOiDispatch_04(void)
{
	safassert(
	    immutil_saImmOiInitialize_2(&immOiHandle, &immOiCallbacks, &immVersion),
	    SA_AIS_OK);
	test_validate(saImmOiDispatch(immOiHandle, -1),
		      SA_AIS_ERR_INVALID_PARAM);
	safassert(immutil_saImmOiFinalize(immOiHandle), SA_AIS_OK);
}
