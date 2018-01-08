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
 * Author(s): Emerson Network Power
 *
 */

/*****************************************************************************
..............................................................................

..............................................................................

  DESCRIPTION:

  This module is the include file for Availability Director's control block.

******************************************************************************
*/

/*
 * Module Inclusion Control...
 */
#ifndef AMF_AMFD_CB_H_
#define AMF_AMFD_CB_H_

#include <cstdint>
#include <saImmOi.h>
#include <saClm.h>

#include "base/ncssysf_lck.h"
#include "mds/mds_papi.h"
#include "mbc/mbcsv_papi.h"
#include "base/ncs_edu_pub.h"

#include "amf/amfd/ckpt.h"
#include "amf/amfd/timer.h"

#include <list>
#include <queue>
#include <atomic>

class AVD_SI;
class AVD_AVND;

typedef enum {
  AVD_INIT_BGN = 1,
  AVD_CFG_DONE,  /* Configuration successfully read from IMM */
  AVD_INIT_DONE, /* OpenSAF SUs assigned on active controller */
  AVD_APP_STATE  /* Cluster startup timer has expired */
} AVD_INIT_STATE;

typedef enum {
  AVD_IMM_INIT_BASE = 1,
  AVD_IMM_INIT_ONGOING = 2,
  AVD_IMM_INIT_DONE = 3,
  AVD_IMM_TERMINATING = 4,
} AVD_IMM_INIT_STATUS;
/*
 * Sync state of the Standby.
 */
typedef enum {
  AVD_STBY_OUT_OF_SYNC,
  AVD_STBY_IN_SYNC,
  AVD_STBY_SYNC_STATE_MAX
} AVD_STBY_SYNC_STATE;

/*
 * Message Queue for holding the AVND messages
 * which we are going to send after processing the
 * entire event.
 */
typedef struct avsv_nd_msg_queue {
  NCSMDS_INFO snd_msg;
  struct avsv_nd_msg_queue *next;
} AVSV_ND_MSG_QUEUE;

/* For external components, we need to have a list of health check information
   as the health check is assotiated with node. But we wouldn't have health
   check assoiated with any cluster node, so we have to have this information
   somewhere else and so it would be in CB. We also want some information like
   node_state, rcv_msg_id, snd_msg_id of hosting node, so we also have a
   pointer to local AVD_AVND data structure. */
typedef struct avd_ext_comp_info {
  AVD_AVND *local_avnd_node;
  AVD_AVND *ext_comp_hlt_check;
} AVD_EXT_COMP_INFO;

/*
 * Message Queue for holding the AVD events
 * during fail over.
 */
typedef struct avd_evt_queue {
  struct AVD_EVT *evt;
  struct avd_evt_queue *next;
} AVD_EVT_QUEUE;

/* AVD IMM Admin Operation Callback */
typedef struct admin_oper_cbk {
  SaAmfAdminOperationIdT admin_oper;
  SaInvocationT invocation;
} AVD_ADMIN_OPER_CBK;

/* Cluster Control Block (Cl_CB): This data structure lives
 * in the AvD and is the anchor structure for all internal data structures.
 */
typedef struct cl_cb_tag {
  SYSF_MBX avd_mbx; /* mailbox on which AvD waits */

  /* for HB thread */
  SYSF_MBX avd_hb_mbx; /* mailbox on which AvD waits */

  NCSCONTEXT mds_handle; /* The handle returned by MDS
                          * when initialized
                          */

  AVD_INIT_STATE init_state; /* when the AvD is comming up for
                              * the first time it will come up
                              * as down in this mode the HA
                              * state for the AvD will be
                              * determined.
                              * Checkpointing - Sent as a one time update.
                              */

  bool avd_fover_state; /* true if avd is trying to
                         * recover from the fail-over */

  SaAmfHAStateT avail_state_avd; /* the redundancy state for
                                  * Availability director
                                  */
  bool is_active() const {return avail_state_avd == SA_AMF_HA_ACTIVE;};

  MDS_HDL vaddr_pwe_hdl; /* The pwe handle returned when
                          * vdest address is created.
                          */
  MDS_HDL vaddr_hdl;     /* The handle returned by mds
                          * for vaddr creation
                          */
  MDS_HDL adest_hdl;     /* The handle returned by mds
                          * for adest creation
                          */
  MDS_DEST vaddr;        /* vcard address of the this AvD
                          */
  MDS_DEST other_avd_adest;  /* ADEST of other AvD */
  MDS_DEST local_avnd_adest; /* ADEST of local AvND */

  /*
   * Message queue to hold messages to be sent to the ND.
   * This is a FIFO queue.
   */
  std::queue<AVSV_ND_MSG_QUEUE *> nd_msg_queue_list{};

  /* Event Queue to hold the events during fail-over */
  std::queue<AVD_EVT_QUEUE *> evt_queue{};
  /*
   * MBCSv related variables.
   */
  NCS_MBCSV_HDL mbcsv_hdl;
  uint32_t ckpt_hdl;
  SaSelectionObjectT mbcsv_sel_obj;
  AVD_STBY_SYNC_STATE stby_sync_state;

  uint32_t synced_reo_type; /* Count till which sync is done */
  AVSV_ASYNC_UPDT_CNT
      async_updt_cnt; /* Update counts for different async updates */
  bool sync_required; /* if true, we need to send SYNC send to the standby
                         after mailbox processing */

  /* Queue for keeping async update messages  on Standby */
  AVSV_ASYNC_UPDT_MSG_QUEUE_LIST async_updt_msgs;

  EDU_HDL edu_hdl;           /* EDU handle used for check pointing */
  EDU_HDL mds_edu_hdl;       /* EDU handle used in MDS callbacks */
  SaTimeT cluster_init_time; /* The time when the firstnode joined the cluster.
                              * Checkpointing - Sent as a one time update.
                              */

  SaClmNodeIdT node_id_avd;       /* node id on which this AvD is  running */
  SaClmNodeIdT node_id_avd_other; /* node id on which the other AvD is
                                   * running
                                   */
  SaClmNodeIdT node_avd_failed;   /* node id where AVD is down */

  AVD_TMR amf_init_tmr; /* The timer for amf initialisation. */
  AVD_TMR
      node_sync_tmr; /* The timer for reception of all node_up from all PLs. */
  AVD_TMR heartbeat_tmr; /* The timer for sending heart beats to nd. */
  SaTimeT heartbeat_tmr_period;
  uint32_t minimum_cluster_size;

  uint32_t nodes_exit_cnt; /* The counter to identifies the number
                      of nodes that have exited the membership
                      since the cluster boot time */

  /********** NTF related params      ***********/
  std::atomic<SaNtfHandleT> ntfHandle;

  /********Peer AvD related*********************/
  AVD_EXT_COMP_INFO ext_comp_info;
  uint16_t peer_msg_fmt_ver;
  uint16_t avd_peer_ver;

  /******** IMM related ********/
  SaImmOiHandleT immOiHandle;
  SaImmOiHandleT immOmHandle;
  SaSelectionObjectT imm_sel_obj;
  bool is_implementer;

  /* Clm stuff */
  std::atomic<SaClmHandleT> clmHandle;
  std::atomic<SaSelectionObjectT> clm_sel_obj;
  bool is_clm_track_started;

  bool fully_initialized;
  bool swap_switch; /* true - In middle of role switch. */

  /** true when active services (IMM, LOG, NTF, etc.) exist
   * Set to true when leaving the no-active state
   * Set to false at switch-over entering the no-active state
   * Used to skip usage of dependent services in the no-active state
   */
  bool active_services_exist;
  bool all_nodes_synced;
  bool node_sync_window_closed;

  /*
  A list of those SIs for which SI dep tolerance timer is running.
  This is required because during controller switch-over or fail-over
  active controller may not take action if tolerance timer expires and
  controller itself is in role change phase. This list will help
  standby controller to know for which SIs tolerance timer has expired
  and active controller could not take the action. After becoming active
  controller, SI dep action will be taken on the SIs in this list and in
  TOLERANCE_TIMER_RUNNING state.
   */
  std::list<AVD_SI *> sis_in_Tolerance_Timer_state;

  /* The duration that amfd should tolerate the absence of SCs */
  uint32_t scs_absence_max_duration;
  AVD_IMM_INIT_STATUS avd_imm_status;
} AVD_CL_CB;

extern AVD_CL_CB *avd_cb;

struct AVD_EVT;

#endif  // AMF_AMFD_CB_H_
