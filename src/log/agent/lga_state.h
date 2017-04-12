/*      -*- OpenSAF  -*-
 *
 * Copyright Ericsson AB 2017 - All Rights Reserved.
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

#ifndef SRC_LOG_AGENT_LGA_STATE_H_
#define SRC_LOG_AGENT_LGA_STATE_H_

#include "log/agent/lga.h"

void lga_no_server_state_set(void);
void lga_serv_recov1state_set(void);
int lga_recover_one_client(lga_client_hdl_rec_t *p_client);
void lga_recovery2_lock(void);
void lga_recovery2_unlock(void);

void set_lga_state(lga_state_t state);
bool is_lga_state(lga_state_t state);
void recovery2_lock(bool *is_locked);
void recovery2_unlock(bool *is_locked);

#endif  // SRC_LOG_AGENT_LGA_STATE_H_
