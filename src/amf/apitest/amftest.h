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
 */

#ifndef AMF_APITEST_AMFTEST_H_
#define AMF_APITEST_AMFTEST_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>

#include <saAmf.h>
#include "osaf/apitest/utest.h"
#include "osaf/apitest/util.h"

extern SaAisErrorT rc;
extern SaAmfHandleT amfHandle;
extern SaSelectionObjectT selectionObject;
extern SaVersionT amfVersion_B41;
extern SaAmfCallbacksT_4 amfCallbacks_4;

#endif  // AMF_APITEST_AMFTEST_H_
