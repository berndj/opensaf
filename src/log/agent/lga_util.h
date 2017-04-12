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

struct lga_cb_t;
struct lgsv_msg_t;
struct lga_client_hdl_rec_t;
struct lga_log_stream_hdl_rec_t;

unsigned int lga_startup(lga_cb_t *cb);
unsigned int lga_shutdown_after_last_client(void);
unsigned int lga_force_shutdown(void);

SaAisErrorT lga_hdl_cbk_dispatch(lga_cb_t *, lga_client_hdl_rec_t *,
                                 SaDispatchFlagsT);

lga_client_hdl_rec_t *lga_hdl_rec_add(lga_cb_t *lga_cb,
                                      const SaLogCallbacksT *reg_cbks,
                                      uint32_t client_id);

lga_log_stream_hdl_rec_t *lga_log_stream_hdl_rec_add(
    lga_client_hdl_rec_t **hdl_rec,
    uint32_t log_stream_id,
    uint32_t log_stream_open_flags,
    const char *logStreamName, uint32_t log_header_type);

void lga_hdl_list_del(lga_client_hdl_rec_t **);
uint32_t lga_hdl_rec_del(lga_client_hdl_rec_t **, lga_client_hdl_rec_t *);
uint32_t lga_log_stream_hdl_rec_del(
    lga_log_stream_hdl_rec_t **,
    lga_log_stream_hdl_rec_t *);
lga_client_hdl_rec_t *lga_find_hdl_rec_by_regid(
    lga_cb_t *lga_cb, uint32_t client_id);
bool lga_is_extended_name_valid(const SaNameT* name);
lga_log_stream_hdl_rec_t *lga_find_stream_hdl_rec_by_regid(
    lga_cb_t *lga_cb,
    uint32_t client_id,
    uint32_t stream_id);

#endif  // SRC_LOG_AGENT_LGA_UTIL_H_
