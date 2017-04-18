/*      -*- OpenSAF  -*-
 *
 * (C) Copyright 2008 The OpenSAF Foundation
 * Copyright (C) 2017, Oracle and/or its affiliates. All rights reserved.
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
 * Author(s): Emerson Network Power
 *
 */

/*****************************************************************************

  This module contains:
  * the main() routine for the AMF director process
  * initialization logic
  * an array of function pointers, indexed by the events for both active and
  standby director.
  * main event loop

  Events are created for received MDS messages, MDS events and timer expiration.
  CLM, IMM and MBCSV events are handled directly (not using the array of
  function pointers).

  The AMF director process is mainly single threaded. But in the event of
  getting a BAD_HANDLE return from saImmOiDispatch(), a temporary detached
  thread is created to handle re-initialization as an IMM implementer. That
  thread will return/exit once re-initialization is done.

  There also exist a couple of threads in the core library (mds, timer, ...).

******************************************************************************
*/

#include <new>
#include <poll.h>

#include "base/daemon.h"
#include "base/logtrace.h"
#include "nid/agent/nid_api.h"
#include "rde/agent/rda_papi.h"

#include "amf/amfd/amfd.h"
#include "amf/amfd/imm.h"
#include "amf/amfd/cluster.h"
#include "amf/amfd/si_dep.h"
#include "amf/amfd/hlt.h"
#include "amf/amfd/clm.h"
#include "amf/amfd/sgtype.h"
#include "amf/amfd/sutcomptype.h"
#include "amf/amfd/sutype.h"
#include "amf/amfd/su.h"
#include "base/osaf_utility.h"
#include "base/getenv.h"

static const char *internal_version_id_ __attribute__((used)) =
    "@(#) $Id: " INTERNAL_VERSION_ID " $";

enum {
  FD_TERM = 0,
  FD_MBX,
  FD_MBCSV,
  FD_CLM,
  FD_IMM  // must be last
};

// Singleton Control Block. Statically allocated
static AVD_CL_CB _control_block;

// Global reference to the control block
AVD_CL_CB *avd_cb = &_control_block;

static nfds_t nfds = FD_IMM + 1;
static struct pollfd fds[FD_IMM + 1];

static void process_event(AVD_CL_CB *cb_now, AVD_EVT *evt);
static void invalid_evh(AVD_CL_CB *cb, AVD_EVT *evt);
static void standby_invalid_evh(AVD_CL_CB *cb, AVD_EVT *evt);
static void qsd_invalid_evh(AVD_CL_CB *cb, AVD_EVT *evt);
static void qsd_ignore_evh(AVD_CL_CB *cb, AVD_EVT *evt);

/* list of all the function pointers related to handling the events
 * for active director.
 */
static const AVD_EVT_HDLR g_actv_list[AVD_EVT_MAX] = {
    invalid_evh, /* AVD_EVT_INVALID */

    /* active AvD message events processing */
    avd_node_up_evh,               /* AVD_EVT_NODE_UP_MSG */
    avd_reg_su_evh,                /* AVD_EVT_REG_SU_MSG */
    invalid_evh,                   /* AVD_EVT_REG_COMP_MSG */
    avd_su_oper_state_evh,         /* AVD_EVT_OPERATION_STATE_MSG */
    avd_su_si_assign_evh,          /* AVD_EVT_INFO_SU_SI_ASSIGN_MSG */
    avd_pg_trk_act_evh,            /* AVD_EVT_PG_TRACK_ACT_MSG */
    avd_oper_req_evh,              /* AVD_EVT_OPERATION_REQUEST_MSG */
    avd_data_update_req_evh,       /* AVD_EVT_DATA_REQUEST_MSG */
    avd_node_down_evh,             /* AVD_EVT_NODE_DOWN_MSG */
    avd_ack_nack_evh,              /* AVD_EVT_VERIFY_ACK_NACK_MSG */
    avd_comp_validation_evh,       /* AVD_EVT_COMP_VALIDATION_MSG */
    avd_nd_sisu_state_info_evh,    /* AVD_EVT_ND_SISU_STATE_INFO_MSG */
    avd_nd_compcsi_state_info_evh, /* AVD_EVT_ND_COMPCSI_STATE_INFO_MSG */

    /* active AvD timer events processing */
    avd_tmr_snd_hb_evh,       /* AVD_EVT_TMR_SND_HB */
    avd_cluster_tmr_init_evh, /* AVD_EVT_TMR_CL_INIT */
    avd_sidep_tol_tmr_evh,    /* AVD_EVT_TMR_SI_DEP_TOL */
    avd_node_sync_tmr_evh,    /* AVD_EVT_TMR_ALL_NODE_UP */

    /* active AvD MDS events processing */
    avd_mds_avd_up_evh,    /* AVD_EVT_MDS_AVD_UP */
    avd_mds_avd_down_evh,  /* AVD_EVT_MDS_AVD_DOWN */
    avd_mds_avnd_up_evh,   /* AVD_EVT_MDS_AVND_UP */
    avd_mds_avnd_down_evh, /* AVD_EVT_MDS_AVND_DOWN */
    avd_mds_qsd_role_evh,  /* AVD_EVT_MDS_QSD_ACK */

    /* Role change Event processing */
    avd_role_change_evh,        /* AVD_EVT_ROLE_CHANGE */
    avd_role_switch_ncs_su_evh, /* AVD_EVT_SWITCH_NCS_SU */
    avd_sidep_assign_evh,       /* AVD_EVT_ASSIGN_SI_DEP_STATE */
    invalid_evh,                /* AVD_EVT_INVALID */
    avd_sidep_unassign_evh,     /* AVD_EVT_UNASSIGN_SI_DEP_STATE */
    avd_avnd_mds_info_evh,      /* AVD_EVT_ND_MDS_VER_INFO*/
};

/* list of all the function pointers related to handling the events
 * for standby director.
 */
static const AVD_EVT_HDLR g_stndby_list[AVD_EVT_MAX] = {
    invalid_evh, /* AVD_EVT_INVALID */

    /* standby AvD message events processing */
    standby_invalid_evh, /* AVD_EVT_NODE_UP_MSG */
    standby_invalid_evh, /* AVD_EVT_REG_SU_MSG */
    standby_invalid_evh, /* AVD_EVT_REG_COMP_MSG */
    standby_invalid_evh, /* AVD_EVT_OPERATION_STATE_MSG */
    standby_invalid_evh, /* AVD_EVT_INFO_SU_SI_ASSIGN_MSG */
    standby_invalid_evh, /* AVD_EVT_PG_TRACK_ACT_MSG */
    standby_invalid_evh, /* AVD_EVT_OPERATION_REQUEST_MSG */
    standby_invalid_evh, /* AVD_EVT_DATA_REQUEST_MSG */
    standby_invalid_evh, /* AVD_EVT_NODE_DOWN_MSG */
    standby_invalid_evh, /* AVD_EVT_VERIFY_ACK_NACK_MSG */
    standby_invalid_evh, /* AVD_EVT_COMP_VALIDATION_MSG */
    standby_invalid_evh, /* AVD_EVT_ND_SUSI_STATE_INFO_MSG */
    standby_invalid_evh, /* AVD_EVT_ND_COMPCSI_STATE_INFO_MSG */

    /* standby AvD timer events processing */
    avd_tmr_snd_hb_evh,    /* AVD_EVT_TMR_SND_HB */
    standby_invalid_evh,   /* AVD_EVT_TMR_CL_INIT */
    avd_sidep_tol_tmr_evh, /* AVD_EVT_TMR_SI_DEP_TOL */
    standby_invalid_evh,   /* AVD_EVT_TMR_ALL_NODE_UP */

    /* standby AvD MDS events processing */
    avd_mds_avd_up_evh,       /* AVD_EVT_MDS_AVD_UP */
    avd_standby_avd_down_evh, /* AVD_EVT_MDS_AVD_DOWN */
    avd_mds_avnd_up_evh,      /* AVD_EVT_MDS_AVND_UP */
    avd_mds_avnd_down_evh,    /* AVD_EVT_MDS_AVND_DOWN */
    standby_invalid_evh,      /* AVD_EVT_MDS_QSD_ACK */

    /* Role change Event processing */
    avd_role_change_evh,  /* AVD_EVT_ROLE_CHANGE */
    standby_invalid_evh,  /* AVD_EVT_SWITCH_NCS_SU */
    standby_invalid_evh,  /* AVD_EVT_ASSIGN_SI_DEP_STATE*/
    standby_invalid_evh,  /* AVD_EVT_INVALID */
    standby_invalid_evh,  /* AVD_EVT_UNASSIGN_SI_DEP_STATE*/
    avd_avnd_mds_info_evh /* AVD_EVT_ND_MDS_VER_INFO*/
};

/* list of all the function pointers related to handling the events
 * for quiesced director.
 */
static const AVD_EVT_HDLR g_quiesc_list[AVD_EVT_MAX] = {
    /* invalid event */
    invalid_evh, /* AVD_EVT_INVALID */

    /* active AvD message events processing */
    qsd_ignore_evh,          /* AVD_EVT_NODE_UP_MSG */
    avd_reg_su_evh,          /* AVD_EVT_REG_SU_MSG */
    invalid_evh,             /* AVD_EVT_REG_COMP_MSG */
    avd_su_oper_state_evh,   /* AVD_EVT_OPERATION_STATE_MSG */
    avd_su_si_assign_evh,    /* AVD_EVT_INFO_SU_SI_ASSIGN_MSG */
    qsd_ignore_evh,          /* AVD_EVT_PG_TRACK_ACT_MSG */
    avd_oper_req_evh,        /* AVD_EVT_OPERATION_REQUEST_MSG */
    avd_data_update_req_evh, /* AVD_EVT_DATA_REQUEST_MSG */
    invalid_evh,             /* AVD_EVT_NODE_DOWN_MSG */
    qsd_invalid_evh,         /* AVD_EVT_VERIFY_ACK_NACK_MSG */
    avd_comp_validation_evh, /* AVD_EVT_COMP_VALIDATION_MSG */
    qsd_invalid_evh,         /* AVD_EVT_ND_SISU_STATE_INFO_MSG */
    qsd_invalid_evh,         /* AVD_EVT_ND_COMPCSI_STATE_INFO_MSG */

    /* active AvD timer events processing */
    avd_tmr_snd_hb_evh,    /* AVD_EVT_TMR_SND_HB */
    qsd_ignore_evh,        /* AVD_EVT_TMR_CL_INIT */
    avd_sidep_tol_tmr_evh, /* AVD_EVT_TMR_SI_DEP_TOL */
    qsd_ignore_evh,        /* AVD_EVT_TMR_ALL_NODE_UP */

    /* active AvD MDS events processing */
    avd_mds_avd_up_evh,    /* AVD_EVT_MDS_AVD_UP */
    avd_mds_avd_down_evh,  /* AVD_EVT_MDS_AVD_DOWN */
    avd_mds_avnd_up_evh,   /* AVD_EVT_MDS_AVND_UP */
    avd_mds_avnd_down_evh, /* AVD_EVT_MDS_AVND_DOWN */
    avd_mds_qsd_role_evh,  /* AVD_EVT_MDS_QSD_ACK */

    /* Role change Event processing */
    avd_role_change_evh,  /* AVD_EVT_ROLE_CHANGE */
    qsd_invalid_evh,      /* AVD_EVT_SWITCH_NCS_SU */
    qsd_invalid_evh,      /* AVD_EVT_TMR_SI_DEP_TOL */
    qsd_invalid_evh,      /* AVD_EVT_INVALID */
    qsd_invalid_evh,      /* AVD_EVT_UNASSIGN_SI_DEP_STATE*/
    avd_avnd_mds_info_evh /* AVD_EVT_ND_MDS_VER_INFO*/
};

/*****************************************************************************
 * Purpose:  This function is the handler for invalid events. It just
 * dumps the event to the debug log and returns.
 *
 * Input: cb - the control block
 *        evt - The event information.
 *
 * Returns: None.
 *
 **************************************************************************/
static void invalid_evh(AVD_CL_CB *cb, AVD_EVT *evt) {
  /* This function should never be called
   * log the event to the debug log and return
   */

  /* we need not send sync update to stanby */
  cb->sync_required = false;

  LOG_NO("avd_invalid_evh: %u", evt->rcv_evt);
}

/*****************************************************************************
 * Purpose:  This function is the handler for invalid events in standby state.
 * This function might be called during system trauma. It just dumps the
 * event to the debug log at a information level and returns.
 *
 * Input: cb - the control block
 *        evt - The event information.
 *
 * Returns: None.
 *
 **************************************************************************/
static void standby_invalid_evh(AVD_CL_CB *cb, AVD_EVT *evt) {
  /* This function should generally never be called
   * log the event to the debug log at information level and return
   */
  LOG_IN("avd_standby_invalid_evh: %u", evt->rcv_evt);

  if ((evt->rcv_evt >= AVD_EVT_NODE_UP_MSG) &&
      (evt->rcv_evt <= AVD_EVT_VERIFY_ACK_NACK_MSG)) {
    if (evt->info.avnd_msg == nullptr) {
      LOG_ER("avd_standby_invalid_evh: no msg");
      return;
    }

    AVD_DND_MSG *n2d_msg = evt->info.avnd_msg;
    avsv_dnd_msg_free(n2d_msg);
    evt->info.avnd_msg = nullptr;
  }
}

/*****************************************************************************
 * Purpose:  This function is the handler for invalid events in quiesced state.
 * This function might be called during system trauma. It just dumps the
 * event to the debug log at a information level and returns.
 *
 *
 * Input: cb - the control block
 *        evt - The event information.
 *
 * Returns: None.
 *
 **************************************************************************/
static void qsd_invalid_evh(AVD_CL_CB *cb, AVD_EVT *evt) {
  /* This function should generally never be called
   * log the event to the debug log at information level and return
   */
  LOG_IN("avd_qsd_invalid_evh: %u", evt->rcv_evt);

  /* we need not send sync update to stanby */
  cb->sync_required = false;

  if ((evt->rcv_evt >= AVD_EVT_NODE_UP_MSG) &&
      (evt->rcv_evt <= AVD_EVT_VERIFY_ACK_NACK_MSG)) {
    if (evt->info.avnd_msg == nullptr) {
      LOG_ER("avd_qsd_invalid_evh: no msg");
      return;
    }

    AVD_DND_MSG *n2d_msg = evt->info.avnd_msg;
    avsv_dnd_msg_free(n2d_msg);
    evt->info.avnd_msg = nullptr;
  }
}

/*****************************************************************************
 * Purpose:  This function is the handler for events in quiesced state which
 * which are to be ignored.
 *
 *
 * Input: cb - the control block
 *        evt - The event information.
 *
 * Returns: None.
 *
 **************************************************************************/
static void qsd_ignore_evh(AVD_CL_CB *cb, AVD_EVT *evt) {
  /* Ignore this Event. Free this msg */
  LOG_IN("avd_qsd_ignore_evh: %u", evt->rcv_evt);
  /* we need not send sync update to stanby */
  cb->sync_required = false;

  if ((evt->rcv_evt >= AVD_EVT_NODE_UP_MSG) &&
      (evt->rcv_evt <= AVD_EVT_VERIFY_ACK_NACK_MSG)) {
    if (evt->info.avnd_msg == nullptr) {
      LOG_ER("avd_qsd_ignore_evh: no msg");
      return;
    }

    AVD_DND_MSG *n2d_msg = evt->info.avnd_msg;
    avsv_dnd_msg_free(n2d_msg);
    evt->info.avnd_msg = nullptr;
  }
}

/**
 * Calculate new poll timeout based on return value from job queue
 * execution
 *
 * @param job_exec_result
 *
 * @return int
 */
static int retval_to_polltmo(AvdJobDequeueResultT job_exec_result) {
  int polltmo = -1;

  switch (job_exec_result) {
    case JOB_EXECUTED:
      polltmo = 0;  // Try again asap assuming there are more jobs waiting
      break;
    case JOB_ETRYAGAIN:
      polltmo = 500;  // Try again soon
      break;
    case JOB_ENOTEXIST:
    case JOB_EINVH:
    case JOB_ERR:
      polltmo = -1;  // Infinite timeout
      break;
    default:
      osafassert(0);
      break;
  }

  return polltmo;
}

/**
 * Handle events during failover state
 * @param evt
 */
static void handle_event_in_failover_state(AVD_EVT *evt) {
  AVD_CL_CB *cb = avd_cb;

  osafassert(cb->avd_fover_state == true);

  TRACE_ENTER();

  /*
   * We are in fail-over, so process only
   * verify ack/nack and timer expiry events and
   * MDS down events. Enqueue all other events.
   */
  if ((evt->rcv_evt == AVD_EVT_VERIFY_ACK_NACK_MSG) ||
      (evt->rcv_evt == AVD_EVT_MDS_AVND_DOWN) ||
      (evt->rcv_evt == AVD_EVT_TMR_SND_HB)) {
    process_event(cb, evt);
  } else {
    AVD_EVT_QUEUE *queue_evt;
    /* Enqueue this event */
    queue_evt = new AVD_EVT_QUEUE();
    queue_evt->evt = evt;
    cb->evt_queue.push(queue_evt);
  }

  std::map<uint32_t, AVD_FAIL_OVER_NODE *>::const_iterator it =
      node_list_db->begin();
  if (it == node_list_db->end()) {
    /* We have received the info from all the nodes. */
    cb->avd_fover_state = false;

    /* Dequeue, all the messages from the queue
       and process them now */

    while (!cb->evt_queue.empty()) {
      AVD_EVT_QUEUE *queue_evt = cb->evt_queue.front();
      cb->evt_queue.pop();
      process_event(cb, queue_evt->evt);
      delete queue_evt;
    }

    /* Walk through all the nodes to check if any of the nodes state is
     * AVD_AVND_STATE_ABSENT, if so means that node(payload) went down
     * during the failover state and the susi failover is differed till
     * failover completes
     */
    for (std::map<uint32_t, AVD_AVND *>::const_iterator it =
             node_id_db->begin();
         it != node_id_db->end();) {
      AVD_AVND *node = it->second;
      ++it;
      if (AVD_AVND_STATE_ABSENT == node->node_state) {
        bool fover_done = false;
        /* Check whether this node failover has been
           performed or not. */
        for (const auto &i_su : node->list_of_ncs_su) {
          if ((i_su->sg_of_su->sg_redundancy_model ==
               SA_AMF_NO_REDUNDANCY_MODEL) &&
              (i_su->list_of_susi == nullptr)) {
            fover_done = true;
            break;
          }
        }
        if (fover_done == false) avd_node_failover(node);
      }
    }
    /* Since we are sending lots of async update to its peer from
       avd_node_failover, let us send commit message from here.
       Otherwise, if warm sync message comes at this point of time,
       Standby Amfd will crash. */
    if (cb->sync_required == true)
      m_AVSV_SEND_CKPT_UPDT_SYNC(cb, NCS_MBCSV_ACT_UPDATE, 0);
  }

  TRACE_LEAVE();
}

/**
 * Callback from RDA. Post a message to the AVD mailbox.
 * @param notused
 * @param cb_info
 * @param error_code
 */
static void rda_cb(uint32_t notused, PCS_RDA_CB_INFO *cb_info,
                   PCSRDA_RETURN_CODE error_code) {
  (void)notused;

  TRACE_ENTER();

  if (((avd_cb->avail_state_avd == SA_AMF_HA_STANDBY) ||
       (avd_cb->avail_state_avd == SA_AMF_HA_QUIESCED)) &&
      (cb_info->info.io_role == PCS_RDA_ACTIVE ||
       cb_info->info.io_role == PCS_RDA_STANDBY)) {
    uint32_t rc;
    AVD_EVT *evt;

    evt = new AVD_EVT;
    evt->rcv_evt = AVD_EVT_ROLE_CHANGE;
    evt->info.avd_msg = new AVD_D2D_MSG;
    evt->info.avd_msg->msg_type = AVD_D2D_CHANGE_ROLE_REQ;
    evt->info.avd_msg->msg_info.d2d_chg_role_req.cause = AVD_FAIL_OVER;
    evt->info.avd_msg->msg_info.d2d_chg_role_req.role =
        (SaAmfHAStateT)cb_info->info.io_role;

    rc = ncs_ipc_send(&avd_cb->avd_mbx, (NCS_IPC_MSG *)evt,
                      (NCS_IPC_PRIORITY)MDS_SEND_PRIORITY_HIGH);
    osafassert(rc == NCSCC_RC_SUCCESS);
  } else
    TRACE("Ignoring change from %u to %u", avd_cb->avail_state_avd,
          cb_info->info.io_role);

  TRACE_LEAVE();
}

/**
 * Initialize everything...
 *
 * @return uint32_t NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE
 */
static uint32_t initialize(void) {
  AVD_CL_CB *cb = avd_cb;
  int rc = NCSCC_RC_FAILURE;
  SaAmfHAStateT role;
  char *val;

  TRACE_ENTER();

  osaf_extended_name_init();
  osaf_extended_name_lend("safApp=safAmfService", &_amfSvcUsrName);

  if (ncs_agents_startup() != NCSCC_RC_SUCCESS) {
    LOG_ER("ncs_agents_startup FAILED");
    goto done;
  }

  /* run the class constructors */
  avd_apptype_constructor();
  avd_app_constructor();
  avd_compglobalattrs_constructor();
  avd_compcstype_constructor();
  avd_comp_constructor();
  avd_ctcstype_constructor();
  avd_comptype_constructor();
  avd_cluster_constructor();
  avd_cstype_constructor();
  avd_csi_constructor();
  avd_csiattr_constructor();
  avd_hctype_constructor();
  avd_hc_constructor();
  avd_node_constructor();
  avd_ng_constructor();
  avd_nodeswbundle_constructor();
  avd_sgtype_constructor();
  avd_sg_constructor();
  avd_svctypecstypes_constructor();
  avd_svctype_constructor();
  avd_si_constructor();
  avd_sirankedsu_constructor();
  avd_sidep_constructor();
  avd_sutcomptype_constructor();
  avd_su_constructor();
  avd_sutype_constructor();

  if (ncs_ipc_create(&cb->avd_mbx) != NCSCC_RC_SUCCESS) {
    LOG_ER("ncs_ipc_create FAILED");
    goto done;
  }

  if (ncs_ipc_attach(&cb->avd_mbx) != NCSCC_RC_SUCCESS) {
    LOG_ER("ncs_ipc_attach FAILED");
    goto done;
  }

  cb->init_state = AVD_INIT_BGN;
  cb->mbcsv_sel_obj = -1;
  cb->imm_sel_obj = -1;
  cb->clm_sel_obj = -1;
  cb->fully_initialized = false;
  cb->swap_switch = false;
  cb->active_services_exist = true;
  cb->stby_sync_state = AVD_STBY_OUT_OF_SYNC;
  cb->sync_required = true;

  cb->heartbeat_tmr.is_active = false;
  cb->heartbeat_tmr.type = AVD_TMR_SND_HB;
  cb->heartbeat_tmr_period = AVSV_DEF_HB_PERIOD;
  cb->all_nodes_synced = false;
  cb->node_sync_window_closed = false;
  cb->scs_absence_max_duration = 0;
  cb->avd_imm_status = AVD_IMM_INIT_BASE;

  if ((val = getenv("AVSV_HB_PERIOD")) != nullptr) {
    cb->heartbeat_tmr_period = strtoll(val, nullptr, 0);
    if (cb->heartbeat_tmr_period == 0) {
      /* no value or non convertable value, revert to default */
      cb->heartbeat_tmr_period = AVSV_DEF_HB_PERIOD;
    }
  }
  cb->minimum_cluster_size =
      base::GetEnv("OSAF_AMF_MIN_CLUSTER_SIZE", uint32_t{2});

  node_list_db = new AmfDb<uint32_t, AVD_FAIL_OVER_NODE>;
  amfnd_svc_db = new std::set<uint32_t>;
  /* get the node id of the node on which the AVD is running. */
  cb->node_id_avd = m_NCS_GET_NODE_ID;

  if ((rc = rda_get_role(&role)) != NCSCC_RC_SUCCESS) {
    LOG_ER("rda_get_role FAILED");
    goto done;
  }

  if ((rc = rda_register_callback(0, rda_cb)) != NCSCC_RC_SUCCESS) {
    LOG_ER("rda_register_callback FAILED %u", rc);
    goto done;
  }

  if ((rc = initialize_for_assignment(cb, role)) != NCSCC_RC_SUCCESS) {
    LOG_ER("initialize_for_assignment FAILED %u", (unsigned)rc);
    goto done;
  }

  rc = NCSCC_RC_SUCCESS;
done:
  TRACE_LEAVE();
  return rc;
}

// c++ new handler will be called if new fails.
static void new_handler() {
  syslog(LOG_ERR, "new failed, calling abort");
  abort();
}

/*****************************************************************************
 * This is the main infinite loop in which both the active and
 * standby waiting for events to happen. When woken up
 * due to an event, based on the HA state it moves to either the active
 * or standby processing modules. Even in Init state the same arrays are used.
 *
 **************************************************************************/
static void main_loop(void) {
  AVD_CL_CB *cb = avd_cb;
  AVD_EVT *evt;
  NCS_SEL_OBJ mbx_fd;
  SaAisErrorT error = SA_AIS_OK;
  AVD_STBY_SYNC_STATE old_sync_state = cb->stby_sync_state;
  int polltmo = -1;
  int term_fd;

  // function to be called if new fails. The alternative of using catch of
  // std::bad_alloc will unwind the stack and thus no call chain will be
  // available.
  std::set_new_handler(new_handler);

  mbx_fd = ncs_ipc_get_sel_obj(&cb->avd_mbx);
  daemon_sigterm_install(&term_fd);

  fds[FD_TERM].fd = term_fd;
  fds[FD_TERM].events = POLLIN;
  fds[FD_MBX].fd = mbx_fd.rmv_obj;
  fds[FD_MBX].events = POLLIN;
  while (1) {
    fds[FD_MBCSV].fd = cb->mbcsv_sel_obj;
    fds[FD_MBCSV].events = POLLIN;
    fds[FD_IMM].fd = cb->imm_sel_obj;  // IMM fd must be last in array
    fds[FD_IMM].events = POLLIN;

    if (cb->clmHandle != 0) {
      fds[FD_CLM].fd = cb->clm_sel_obj;
      fds[FD_CLM].events = POLLIN;
    }

    if (cb->immOiHandle != 0 && cb->avd_imm_status == AVD_IMM_INIT_DONE) {
      fds[FD_IMM].fd = cb->imm_sel_obj;
      fds[FD_IMM].events = POLLIN;
      nfds = FD_IMM + 1;
    } else {
      nfds = FD_IMM;
    }

    int pollretval = poll(fds, nfds, polltmo);

    if (pollretval == -1) {
      if (errno == EINTR) continue;

      LOG_ER("main: poll FAILED - %s", strerror(errno));
      break;
    }

    if (pollretval == 0) {
      // poll time out, submit some jobs (if any)
      polltmo = retval_to_polltmo(Fifo::execute(cb));
      continue;
    }

    if (fds[FD_TERM].revents & POLLIN) {
      daemon_exit();
    }

    if (fds[FD_MBX].revents & POLLIN) {
      evt = (AVD_EVT *)ncs_ipc_non_blk_recv(&cb->avd_mbx);

      if (evt == nullptr) {
        LOG_ER("main: nullptr evt");
        continue;
      }

      if ((AVD_EVT_INVALID <= evt->rcv_evt) && (evt->rcv_evt >= AVD_EVT_MAX)) {
        LOG_ER("main: invalid evt %u", evt->rcv_evt);
        continue;
      }

      if (evt->rcv_evt == AVD_IMM_REINITIALIZED) {
        cb->avd_imm_status = AVD_IMM_INIT_DONE;
        TRACE("Received IMM reinit msg");
        polltmo = retval_to_polltmo(Fifo::execute(cb));
        continue;
      }

      if (cb->avd_fover_state) {
        handle_event_in_failover_state(evt);
      } else if (false == cb->avd_fover_state) {
        process_event(cb, evt);
      } else
        osafassert(0);
    }

    if (fds[FD_MBCSV].revents & POLLIN) {
      old_sync_state = cb->stby_sync_state;
      if (NCSCC_RC_SUCCESS != avsv_mbcsv_dispatch(cb, SA_DISPATCH_ALL))
        LOG_ER("avsv_mbcsv_dispatch FAILED");
    }

    if (fds[FD_CLM].revents & POLLIN) {
      TRACE("CLM event rec");
      error = saClmDispatch(cb->clmHandle, SA_DISPATCH_ONE);

      if (error != SA_AIS_OK) LOG_ER("main: saClmDispatch FAILED %u", error);

      /* commit async updated possibly sent in the callback */
      m_AVSV_SEND_CKPT_UPDT_SYNC(cb, NCS_MBCSV_ACT_UPDATE, 0);

      /* flush messages possibly queued in the callback */
      avd_d2n_msg_dequeue(cb);
    }

    if (cb->immOiHandle && fds[FD_IMM].revents & POLLIN) {
      TRACE("IMM event rec");
      error = saImmOiDispatch(cb->immOiHandle, SA_DISPATCH_ALL);

      /*
       ** BAD_HANDLE is interpreted as an IMM service restart. Try
       ** reinitialize the IMM OI API in a background thread and let
       ** this thread do business as usual especially handling write
       ** requests.
       **
       ** All other errors are treated as non-recoverable (fatal) and will
       ** cause an exit of the process.
       */
      if (error == SA_AIS_ERR_BAD_HANDLE) {
        TRACE("main: saImmOiDispatch returned BAD_HANDLE");

        /* Initiate IMM reinitializtion in the background */
        avd_imm_reinit_bg();
      } else if (error != SA_AIS_OK) {
        LOG_ER("main: saImmOiDispatch FAILED %u", error);
        break;
      } else {
        /* commit async updated possibly sent in the callback */
        m_AVSV_SEND_CKPT_UPDT_SYNC(cb, NCS_MBCSV_ACT_UPDATE, 0);
        /* flush messages possibly queued in the callback */
        avd_d2n_msg_dequeue(cb);
      }
    }

    // Standby COLD_SYNC completed. Ask STANDBY to flush its job queue if size
    // is 0 on active.
    if ((Fifo::size() == 0) && (old_sync_state == AVD_STBY_OUT_OF_SYNC) &&
        (cb->stby_sync_state == AVD_STBY_IN_SYNC)) {
      ckpt_job_queue_size();
      old_sync_state = cb->stby_sync_state;
    }
    // submit some jobs (if any)
    polltmo = retval_to_polltmo(Fifo::execute(cb));
  }

  syslog(LOG_CRIT, "AVD Thread Failed");
  sleep(3); /* Allow logs to be printed */
  exit(EXIT_FAILURE);
}

/*****************************************************************************
 * Purpose: This function executes the event handler for the current state.
 *
 * Input: cb  - control block
 *        evt - ptr to event from mailbox
 *
 * Returns: NONE.
 *
 **************************************************************************/
static void process_event(AVD_CL_CB *cb_now, AVD_EVT *evt) {
  TRACE_ENTER2("evt->rcv_evt %u", evt->rcv_evt);

  /* check the HA state */
  if (cb_now->avail_state_avd == SA_AMF_HA_ACTIVE) {
    /* if active call g_avd_actv_list functions */
    g_actv_list[evt->rcv_evt](cb_now, evt);

    /*
     * Just processed the event.
     * Time to send sync send the standby and then
     * all the AVND messages we have queued in our queue.
     */
    if (cb_now->sync_required == true)
      m_AVSV_SEND_CKPT_UPDT_SYNC(cb_now, NCS_MBCSV_ACT_UPDATE, 0);

    avd_d2n_msg_dequeue(cb_now);
  } else if (cb_now->avail_state_avd == SA_AMF_HA_STANDBY) {
    /* if standby call g_avd_stndby_list functions */
    g_stndby_list[evt->rcv_evt](cb_now, evt);

    /* Now it might have become standby to active in avd_role_change_evh()
       during switchover, so just sent updates to quisced */
    if ((cb_now->avail_state_avd == SA_AMF_HA_ACTIVE) &&
        (cb_now->sync_required == true))
      m_AVSV_SEND_CKPT_UPDT_SYNC(cb_now, NCS_MBCSV_ACT_UPDATE, 0);

    avd_d2n_msg_dequeue(cb_now);
  } else if (cb_now->avail_state_avd == SA_AMF_HA_QUIESCED) {
    /* if quiesced call g_avd_quiesc_list functions */
    g_quiesc_list[evt->rcv_evt](cb_now, evt);
    /*
     * Just processed the event.
     * Time to send sync send the standby and then
     * all the AVND messages we have queued in our queue.
     */
    if (cb_now->sync_required == true)
      m_AVSV_SEND_CKPT_UPDT_SYNC(cb_now, NCS_MBCSV_ACT_UPDATE, 0);

    avd_d2n_msg_dequeue(cb_now);

  } else
    osafassert(0);

  /* reset the sync falg */
  cb_now->sync_required = true;

  delete evt;

  TRACE_LEAVE();
}

/**
 * Entry point for the AMF Director process
 * @param argc
 * @param argv
 * @return
 */
int main(int argc, char *argv[]) {
  daemonize(argc, argv);

  // Enable long DN
  if (setenv("SA_ENABLE_EXTENDED_NAMES", "1", 1) != 0) {
    LOG_ER("failed to set SA_ENABLE_EXTENDED_NAMES");
    exit(EXIT_FAILURE);
  }

  if (initialize() != NCSCC_RC_SUCCESS) {
    (void)nid_notify(const_cast<char *>("AMFD"), NCSCC_RC_FAILURE, nullptr);
    LOG_ER("initialize failed, exiting");
    exit(EXIT_FAILURE);
  }

  /* Act Amfd need to inform nid after it has initialized itself, while
     Stanby Amfd need to inform nid after it has done cold sync. */
  if (avd_cb->avail_state_avd == SA_AMF_HA_ACTIVE) {
    (void)nid_notify(const_cast<char *>("AMFD"), NCSCC_RC_SUCCESS, nullptr);
  }

  main_loop();

  /* should normally never get here */
  LOG_ER("main loop returned, exiting");

  return 1;
}
