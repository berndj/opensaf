/*      -*- OpenSAF  -*-
 *
 * (C) Copyright 2013 The OpenSAF Foundation
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

#ifndef NTF_NTFIMCND_NTFIMCN_IMM_H_
#define NTF_NTFIMCND_NTFIMCN_IMM_H_

#include "ntfimcn_main.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the OI interface, get a selection object and become applier
 *
 * @global_param max_waiting_time_ms: Wait max time for each operation.
 * @global_param applier_name: The name of the "configuration change" applier
 * @param *cb[out]
 *
 * @return (-1) if init fail
 */
int ntfimcn_imm_init(ntfimcn_cb_t *cb);

#ifdef __cplusplus
}
#endif

#endif  // NTF_NTFIMCND_NTFIMCN_IMM_H_
