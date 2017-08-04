/*      -*- OpenSAF  -*-
 *
 * (C) Copyright 2010,2015 The OpenSAF Foundation
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
 * Author(s):  GoAhead Software
 *             Ericsson AB
 *
 */

#ifndef CLM_CLMND_CB_H_
#define CLM_CLMND_CB_H_

#include <assert.h>
#include <poll.h>
#include <saClm.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "base/daemon.h"
#include "base/ncssysf_ipc.h"
#include "base/ncssysf_tmr.h"
#include "mds/mds_papi.h"
#include "nid/agent/nid_api.h"
#include "osaf/configmake.h"

/* Self node information */
typedef struct node_detail_t {
  SaUint32T node_id;
  SaNameT node_name;
  SaTimeT boot_time;
  SaUint16T no_of_addresses;
  SaClmNodeAddressT address;
} NODE_INFO;

/* CLM Server control block */
typedef struct clmna_cb_t {
  /* CLMS CLMNA sync params */
  int clms_sync_awaited;
  NCS_SEL_OBJ clms_sync_sel;
  MDS_HDL mds_hdl;
  MDS_DEST clms_mds_dest; /* CLMS absolute/virtual address */
  SYSF_MBX mbx;           /* CLMNA's Mail Box */
  NCS_SEL_OBJ mbx_fd;
  SaNameT comp_name;              /* My AMF name */
  SaAmfHandleT amf_hdl;           /* Handle obtained from AMF */
  SaSelectionObjectT amf_sel_obj; /* AMF provided selection object */
  SaAmfHAStateT ha_state;         /* My current AMF HA state */
  SaBoolT server_synced;
  NODE_INFO node_info;
  tmr_t scale_out_retry_tmr;
  bool is_scale_out_retry_tmr_running;
  bool try_again_received;
  bool nid_started; /**< true if started by NID */
  void* election_starter;
} CLMNA_CB;

#endif  // CLM_CLMND_CB_H_
