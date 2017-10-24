/*      -*- OpenSAF  -*-
 *
 * (C) Copyright 2017 The OpenSAF Foundation
 * (C) Copyright 2017 Ericsson AB - All Rights Reserved.
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

#ifndef BASE_OSAF_GCOV_H_
#define BASE_OSAF_GCOV_H_

#ifdef HAVE_CONFIG_H
#include "osaf/config.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef ENABLE_GCOV

void create_gcov_flush_thread(void);

#else // gcov not enabled

static inline void create_gcov_flush_thread(void) {}

#endif

#ifdef __cplusplus
}
#endif

#endif  // BASE_OSAF_GCOV_H_
