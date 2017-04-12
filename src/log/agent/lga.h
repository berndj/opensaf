/*      -*- OpenSAF  -*-
 *
 * (C) Copyright 2008 The OpenSAF Foundation
 * Copyright Ericsson AB 2008, 2017 - All Rights Reserved.
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

#ifndef SRC_LOG_AGENT_LGA_H_
#define SRC_LOG_AGENT_LGA_H_

#include "mds/mds_papi.h"
#include "base/osaf_time.h"
#include "log/common/lgsv_msg.h"
#include "log/common/lgsv_defs.h"

struct lga_client_hdl_rec_t;

// Log Stream Handle Definition
struct lga_log_stream_hdl_rec_t {
  // Log stream HDL from handle mgr
  unsigned int log_stream_hdl;
  // Log stream name mentioned during open log stream
  char *log_stream_name;
  // Log stream open flags as defined in AIS.02.01
  unsigned int open_flags;
  // Log header type
  unsigned int log_header_type;
  // Server reference for this log stream
  unsigned int lgs_log_stream_id;
  // next pointer for the list in lga_client_hdl_rec_t
  lga_log_stream_hdl_rec_t *next;
  // Back Pointer to the client instantiation
  lga_client_hdl_rec_t *parent_hdl;
  // This flag is used with recovery handling. It is valid only
  // after server down has happened (will be initiated when server down
  // event occurs). It's not valid in LGA_NORMAL state.
  bool recovered_flag;
};

// LGA client record
struct lga_client_hdl_rec_t {
  // Handle value returned by LGS for this client
  unsigned int lgs_client_id;
  // LOG handle (derived from hdl-mngr)
  unsigned int local_hdl;
  // Callbacks registered by the application
  SaLogCallbacksT reg_cbk;
  // List of open streams per client
  lga_log_stream_hdl_rec_t *stream_list;
  // Priority q mbx b/w MDS & Library
  SYSF_MBX mbx;
  // Next pointer for the list in lga_cb_t
  lga_client_hdl_rec_t *next;
  //>
  // These flags are used with recovery handling. They are valid only
  // after server down has happened (will be initiated when server down
  // event occurs). They are not valid in LGA_NORMAL state
  //<
  // Used with "headless" recovery handling. Set when client is initialized.
  // Streams may not have been recovered
  bool initialized_flag;
  // Used with "headless" recovery handling. Set when client is initialized
  // an all streams are recovered
  bool recovered_flag;
  // Status of client based on the CLM status of node
  bool is_stale_client;
  // The API version is being used by client, used for CLM status
  SaVersionT version;
};

// States of the server
enum lgs_state_t {
  // The state before agent is started
  LGS_START,
  // Server is down (headless)
  LGS_DOWN,
  // No active server (switch/fail - over)
  LGS_NO_ACTIVE,
  // Server is up
  LGS_UP
};

// Agent internal states
enum lga_state_t {
  // Server is up and no recovery is ongoing
  LGA_NORMAL,
  // No Server (Server down) state
  LGA_NO_SERVER,
  // Server is up. Recover clients and streams when request from client.
  // Recovery1 timer is running
  LGA_RECOVERY1,
  // Auto recover remaining clients and streams after recovery1 timeout
  LGA_RECOVERY2
};

// The LGA control block is the master anchor structure for all LGA
// instantiations within a process.
struct lga_cb_t {
  // CB lock
  pthread_mutex_t cb_lock;
  // LGA client handle database
  lga_client_hdl_rec_t *client_list;
  // MDS handle
  MDS_HDL mds_hdl;
  // LGS absolute/virtual address
  MDS_DEST lgs_mds_dest;
  // Indicate current server MDS state
  lgs_state_t lgs_state;
  // LGS LGA sync params
  int lgs_sync_awaited;
  NCS_SEL_OBJ lgs_sync_sel;
  // Reflects CLM status of this node(for future use)
  SaClmClusterChangesT clm_node_state;

  lga_cb_t() {
    cb_lock = PTHREAD_MUTEX_INITIALIZER;
    lgs_state = LGS_START;
    mds_hdl = 0;
    client_list = nullptr;
  }
};

// Global log agent control block
extern lga_cb_t lga_cb;

#endif  // SRC_LOG_AGENT_LGA_H_
