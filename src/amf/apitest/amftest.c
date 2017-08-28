/*      -*- OpenSAF  -*-
 *
 * (C) Copyright 2008 The OpenSAF Foundation
 * Copyright (C) 2017, Oracle and/or its affiliates. All rights reserved.
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
 *
 */

#include "amftest.h"

SaAisErrorT rc;
SaAmfHandleT amfHandle;
SaSelectionObjectT selectionObject;
SaVersionT amfVersion_B41 = { 'B', 0x04, 0x01 };
SaAmfCallbacksT_4 amfCallbacks_4 = { NULL, NULL };

int main(int argc, char **argv)
{
	int test_case = ALL_TESTS, test_suite = ALL_SUITES;

	if (argc > 1)
		test_suite = atoi(argv[1]);
	if (argc > 2)
		test_case = atoi(argv[2]);
	if (test_suite == 0) {
		test_list();
		return 0;
	}
	return test_run(test_suite, test_case);
}
