/*      -*- OpenSAF  -*-
 *
 * Copyright Ericsson 2017 - All Rights Reserved.
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

#ifndef SRC_LOG_AGENT_LGA_UTIL_H_
#define SRC_LOG_AGENT_LGA_UTIL_H_

#include <stdint.h>
#include "osaf/saf/saAis.h"
#include "log/saf/saLog.h"

unsigned int lga_startup();
unsigned int lga_shutdown_after_last_client(void);
unsigned int lga_force_shutdown(void);

bool lga_is_extended_name_valid(const SaNameT* name);

#endif  // SRC_LOG_AGENT_LGA_UTIL_H_
