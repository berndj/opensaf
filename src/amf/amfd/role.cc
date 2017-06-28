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
 * Author(s): Emerson Network Power
 *
 */

/*****************************************************************************
..............................................................................

..............................................................................

  DESCRIPTION:This module deals with the switch operations requested by
  Administator.

..............................................................................

  FUNCTIONS INCLUDED in this module:

******************************************************************************
*/

/*
 * Module Inclusion Control...
 */

#include "osaf/immutil/immutil.h"
#include "base/logtrace.h"
#include "rde/agent/rda_papi.h"

#include "amf/amfd/amfd.h"
#include "amf/amfd/imm.h"
#include "amf/amfd/app.h"
#include "amf/amfd/cluster.h"
#include "amf/amfd/clm.h"
#include "amf/amfd/si_dep.h"
#include "base/osaf_utility.h"
#include "role.h"
#include "nid/agent/nid_api.h"

extern pthread_mutex_t imm_reinit_mutex;

static uint32_t avd_role_failover(AVD_CL_CB *cb, SaAmfHAStateT role);
static uint32_t avd_role_failover_qsd_actv(AVD_CL_CB *cb, SaAmfHAStateT role);
static uint32_t avd_rde_set_role(SaAmfHAStateT role);

/****************************************************************************\
 * Function: avd_role_change
 *
 * Purpose:  AVSV function to handle AVD role change event received.
 *
 * Input: cb - AVD control block pointer.
 *
 * Returns: None.
 *
 * NOTES:
 *
 *
\**************************************************************************/
void avd_role_change_evh(AVD_CL_CB *cb, AVD_EVT *evt) {
  uint32_t status = NCSCC_RC_SUCCESS;
  AVD_D2D_MSG *msg = evt->info.avd_msg;
  AVD_ROLE_CHG_CAUSE_T cause = msg->msg_info.d2d_chg_role_req.cause;
  SaAmfHAStateT role = msg->msg_info.d2d_chg_role_req.role;

  TRACE_ENTER2("cause=%u, role=%u, current_role=%u", cause, role,
               cb->avail_state_avd);

  if ((status = initialize_for_assignment(cb, role)) != NCSCC_RC_SUCCESS) {
    LOG_ER("initialize_for_assignment FAILED %u", (unsigned)status);
    _exit(EXIT_FAILURE);
  }

  if (cb->avail_state_avd == role) {
    goto done;
  }

  /* First validate that this is correct role switch. */
  if (((cb->avail_state_avd == SA_AMF_HA_ACTIVE) &&
       (role == SA_AMF_HA_STANDBY)) ||
      ((cb->avail_state_avd == SA_AMF_HA_STANDBY) &&
       (role == SA_AMF_HA_QUIESCED))) {
    LOG_ER("avd_role_change Invalid role change Active -> Standby");
    status = NCSCC_RC_FAILURE;
    goto done;
  }

  if ((cause == AVD_SWITCH_OVER) && (cb->avail_state_avd == SA_AMF_HA_ACTIVE) &&
      (role == SA_AMF_HA_QUIESCED)) {
    if (true == cb->swap_switch) {
      /* swap resulted Switch Active -> Quiesced */
      amfd_switch_actv_qsd(cb);
      status = NCSCC_RC_SUCCESS;
    }
    goto done;
  }

  if ((cause == AVD_SWITCH_OVER) && (role == SA_AMF_HA_ACTIVE) &&
      (cb->avail_state_avd == SA_AMF_HA_QUIESCED)) {
    if (true == cb->swap_switch) {
      /* swap resulted Switch  Quiesced -> Active */
      amfd_switch_qsd_actv(cb);
      status = NCSCC_RC_SUCCESS;
    }
    goto done;
  }

  if ((cause == AVD_SWITCH_OVER) && (role == SA_AMF_HA_STANDBY) &&
      (cb->avail_state_avd == SA_AMF_HA_QUIESCED)) {
    if (true == cb->swap_switch) {
      /* swap resulted Switch  Quiesced -> standby */
      amfd_switch_qsd_stdby(cb);
      status = NCSCC_RC_SUCCESS;
    }
    goto done;
  }

  if ((cause == AVD_SWITCH_OVER) && (role == SA_AMF_HA_ACTIVE) &&
      (cb->avail_state_avd == SA_AMF_HA_STANDBY)) {
    if (true == cb->swap_switch) {
      /* swap resulted Switch  standby -> Active */
      LOG_NO("Switching StandBy --> Active State");
      status = amfd_switch_stdby_actv(cb);
      if (status == NCSCC_RC_SUCCESS) {
        LOG_NO("Controller switch over done");
        saflog(LOG_NOTICE, amfSvcUsrName, "Controller switch over done at %x",
               cb->node_id_avd);
      }
      goto done;
    }
  }

  if ((cause == AVD_FAIL_OVER) && (cb->avail_state_avd == SA_AMF_HA_QUIESCED) &&
      (role == SA_AMF_HA_STANDBY)) {
    /* Fail-over Quiesced to standby (spare controller role change) */
    status = NCSCC_RC_SUCCESS;
    goto done;
  }

  if ((cause == AVD_FAIL_OVER) && (cb->avail_state_avd == SA_AMF_HA_QUIESCED) &&
      (role == SA_AMF_HA_ACTIVE)) {
    /* Fail-over Quiesced to Active */
    status = avd_role_failover_qsd_actv(cb, role);
    goto done;
  }

  if ((cause == AVD_FAIL_OVER) && (cb->avail_state_avd == SA_AMF_HA_STANDBY) &&
      (role == SA_AMF_HA_ACTIVE)) {
    /* Fail-over Standby to Active */
    status = avd_role_failover(cb, role);
  } else {
    LOG_ER("avd_role_change Invalid role change");
    status = NCSCC_RC_FAILURE;
  }

done:
  if (NCSCC_RC_SUCCESS != status) {
    LOG_ER("avd_role_change role change failure");
  }

  if (msg) avsv_d2d_msg_free(msg);
  TRACE_LEAVE2("%u", status);
  return;
}

uint32_t initialize_for_assignment(cl_cb_tag *cb, SaAmfHAStateT ha_state) {
  TRACE_ENTER2("ha_state = %d", static_cast<int>(ha_state));
  uint32_t rc = NCSCC_RC_SUCCESS;

  if (cb->fully_initialized) goto done;
  cb->avail_state_avd = ha_state;
  if (ha_state == SA_AMF_HA_QUIESCED) {
    if ((rc = nid_notify(const_cast<char *>("AMFD"), NCSCC_RC_SUCCESS,
                         nullptr)) != NCSCC_RC_SUCCESS) {
      LOG_ER("nid_notify failed");
    }
    goto done;
  }
  if ((rc = avd_mds_init(cb)) != NCSCC_RC_SUCCESS) {
    LOG_ER("avd_mds_init FAILED");
    goto done;
  }
  if ((rc = avsv_mbcsv_register(cb)) != NCSCC_RC_SUCCESS) {
    LOG_ER("avsv_mbcsv_register FAILED");
    goto done;
  }
  // Initialize CLM handle in thread
  if (avd_start_clm_init_bg() != SA_AIS_OK) {
    LOG_EM("avd_clm_init FAILED");
    rc = NCSCC_RC_FAILURE;
    goto done;
  }

  if (avd_imm_init(cb) != SA_AIS_OK) {
    LOG_ER("avd_imm_init FAILED");
    rc = NCSCC_RC_FAILURE;
    goto done;
  }

  // Initialize NTF handle in thread
  if (avd_start_ntf_init_bg() != SA_AIS_OK) {
    LOG_EM("avd_start_ntf_init_bg FAILED");
    goto done;
  }

  if ((rc = avd_mds_set_vdest_role(cb, ha_state)) != NCSCC_RC_SUCCESS) {
    LOG_ER("avd_mds_set_vdest_role FAILED");
    goto done;
  }
  if ((rc = avsv_set_ckpt_role(cb, ha_state)) != NCSCC_RC_SUCCESS) {
    LOG_ER("avsv_set_ckpt_role FAILED");
    goto done;
  }
  if (ha_state == SA_AMF_HA_ACTIVE) {
    rc = avd_active_role_initialization(cb, ha_state);
    if (rc != NCSCC_RC_SUCCESS) {
      LOG_ER("avd_active_role_initialization FAILED");
      goto done;
    }

  } else if (ha_state == SA_AMF_HA_STANDBY) {
    rc = avd_standby_role_initialization(cb);
    if (rc != NCSCC_RC_SUCCESS) {
      LOG_ER("avd_standby_role_initialization FAILED");
      goto done;
    }
  }
  cb->fully_initialized = true;
done:
  TRACE_LEAVE2("rc = %u", rc);
  return rc;
}

/**************************************************************************** \
 * Function: avd_init_role_set
 *
 * Purpose:  AVSV function to handle AVD's initial role setting.
 *
 * Input: cb - AVD control block pointer.
 *        role - Role to be set.
 *
 * Returns: NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE.
 *
 * NOTES:
 *
 *
\**************************************************************************/
uint32_t avd_active_role_initialization(AVD_CL_CB *cb, SaAmfHAStateT role) {
  uint32_t status = NCSCC_RC_FAILURE;

  TRACE_ENTER();

  if (avd_imm_impl_set() != SA_AIS_OK) {
    LOG_ER("avd_imm_impl_set FAILED");
    goto done;
  }

  if (avd_imm_config_get() != NCSCC_RC_SUCCESS) {
    LOG_ER("avd_imm_config_get FAILED, AMF will not start.");
    goto done;
  }

  cb->init_state = AVD_CFG_DONE;

  avd_imm_update_runtime_attrs();

  status = NCSCC_RC_SUCCESS;
done:
  TRACE_LEAVE();
  return status;
}

/****************************************************************************\
 * @brief       AVSV function to handle AVD's initial standby role setting.
 *
 * @param[in]   cb - AVD control block pointer.
 *
 * @return      NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE.
 *
\**************************************************************************/
uint32_t avd_standby_role_initialization(AVD_CL_CB *cb) {
  uint32_t status = NCSCC_RC_FAILURE;

  TRACE_ENTER();

  if (avd_imm_applier_set() != SA_AIS_OK) {
    LOG_ER("avd_imm_applier_set FAILED");
    goto done;
  }

  if (avd_imm_config_get() != NCSCC_RC_SUCCESS) {
    LOG_ER("avd_imm_config_get FAILED, AMF will not start.");
    goto done;
  }

  cb->init_state = AVD_CFG_DONE;

  status = NCSCC_RC_SUCCESS;
done:
  TRACE_LEAVE();
  return status;
}

/**
 * @brief  This function is related to SI dep feature and meant for a dependent
 * SIs. During contoller failover or switchover, tolerance timer may expire for
 *         a dependent SIs. During the role change phase of controller itself,
 * active controller may not act on the dependent SI for which tolerance timer
 * has expired. Standby AMFD tracks all those dependent SIs, in the list
 *         avd_cb->sis_in_Tolerance_Timer_state, for which tolerance timer has
 *         expired but active AMFD has still not acted on them. So when standby
 *         controller becomes active it should act on all the SIs which are in
 * the list and still in the TOLERANCE_TIMER_RUNNING state.
 */

static void avd_act_on_sis_in_tol_timer_state() {
  TRACE_ENTER();
  std::list<AVD_SI *>::iterator it1;
  TRACE_1("Size of list :%zu", avd_cb->sis_in_Tolerance_Timer_state.size());
  for (it1 = avd_cb->sis_in_Tolerance_Timer_state.begin();
       it1 != avd_cb->sis_in_Tolerance_Timer_state.end(); ++it1) {
    AVD_SI *dep_si = *it1;
    if (dep_si->si_dep_state == AVD_SI_TOL_TIMER_RUNNING)
      avd_sidep_activ_amfd_tol_timer_expiry(nullptr, dep_si);
  }
  avd_cb->sis_in_Tolerance_Timer_state.clear();
  TRACE_LEAVE();
}
/****************************************************************************\
 * Function: avd_role_failover
 *
 * Purpose:  AVSV function to handle AVD's fail-over.
 *
 * Input: cb - AVD control block pointer.
 *        role - Role to be set.
 *
 * Returns: NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE.
 *
 * NOTES:
 *
 *
\**************************************************************************/
static uint32_t avd_role_failover(AVD_CL_CB *cb, SaAmfHAStateT role) {
  uint32_t status = NCSCC_RC_FAILURE;
  AVD_AVND *my_node = nullptr, *failed_node;
  SaAisErrorT rc;

  TRACE_ENTER();
  LOG_NO("FAILOVER StandBy --> Active");

  /* If we are in the middle of admin switch, ignore it */
  if (cb->swap_switch == true) {
    cb->swap_switch = false;
  }

  /*
   * Check whether Standby is in sync with Active. If yes then
   * proceed further. Else return failure.
   */
  if (AVD_STBY_OUT_OF_SYNC == cb->stby_sync_state) {
    LOG_ER("FAILOVER StandBy --> Active FAILED, Standby OUT OF SYNC");
    goto done;
  }

  if (nullptr == (my_node = avd_node_find_nodeid(cb->node_id_avd))) {
    LOG_ER("FAILOVER StandBy --> Active FAILED, node %x not found",
           cb->node_id_avd);
    goto done;
  }

  if (nullptr == (failed_node = avd_node_find_nodeid(cb->node_id_avd_other))) {
    LOG_ER("FAILOVER StandBy --> Active FAILED, node %x not found",
           cb->node_id_avd_other);
    goto done;
  }

  /* check the node state */
  if (my_node->node_state != AVD_AVND_STATE_PRESENT) {
    LOG_ER("FAILOVER StandBy --> Active FAILED, stdby not in good state");
    goto done;
  }

  if (NCSCC_RC_SUCCESS != (status = avsv_set_ckpt_role(cb, SA_AMF_HA_ACTIVE))) {
    LOG_ER("FAILOVER StandBy --> Active, ckpt set role failed");
    goto done;
  }

  /* Now Dispatch all the messages from the MBCSv mail-box */
  if (NCSCC_RC_SUCCESS != (status = avsv_mbcsv_dispatch(cb, SA_DISPATCH_ALL))) {
    LOG_ER("FAILOVER StandBy --> Active FAILED, MBCSV DISPATCH FAILED");
    goto done;
  }

  /*
   * We might be having some async update messages in the
   * Queue to be processed, now drop all of them.
   */
  avsv_dequeue_async_update_msgs(cb, false);

  /* Take mutex before changing role as it may impact logic
     in avd_imm_reinit_bg_thread. If mutex is taken for imm
     initialization, then wait for its completion before changing role.*/
  osaf_mutex_lock_ordie(&imm_reinit_mutex);
  cb->avail_state_avd = role;
  osaf_mutex_unlock_ordie(&imm_reinit_mutex);

  /* Declare this standby as Active. Set Vdest role and MBCSv role */
  if (NCSCC_RC_SUCCESS != (status = avd_mds_set_vdest_role(cb, role))) {
    LOG_ER("FAILOVER StandBy --> Active, VDEST Change role failed ");
    goto done;
  }
  /* There is no need to take mutex in ImplClear because if imm
     initialization were undergoing, above mutex took care. */
  /* Give up our IMM OI Applier role */
  if ((rc = immutil_saImmOiImplementerClear(cb->immOiHandle)) != SA_AIS_OK) {
    LOG_ER("FAILOVER StandBy --> Active FAILED, ImplementerClear failed %u",
           rc);
    /* If it fails with BAD HANDLE, reinit imm intf and continue.
       Let imm intf reinit take care of setting Impl/Applr based
       on Avd role. */
    if (rc == SA_AIS_ERR_BAD_HANDLE) {
      avd_imm_reinit_bg();
    } else
      goto done;
  }

  /* Time to send fail-over messages to all the AVND's */
  avd_fail_over_event(cb);

  /* We need to send the role to AvND. */
  status = avd_avnd_send_role_change(cb, cb->node_id_avd, cb->avail_state_avd);
  if (NCSCC_RC_SUCCESS != status) {
    LOG_ER("%s: avd_avnd_send_role_change failed", __FUNCTION__);
  } else {
    avd_d2n_msg_dequeue(cb);
  }

  /* We have successfully changed role to Active. */
  cb->node_id_avd_other = 0;
  /* Take mutex here, to sync with above avd_imm_reinit_bg call. */
  osaf_mutex_lock_ordie(&imm_reinit_mutex);
  if ((rc = immutil_saImmOiImplementerSet(avd_cb->immOiHandle,
                                          const_cast<SaImmOiImplementerNameT>(
                                              "safAmfService"))) != SA_AIS_OK) {
    osaf_mutex_unlock_ordie(&imm_reinit_mutex);
    LOG_ER("FAILOVER StandBy --> Active FAILED, ImplementerSet failed %u", rc);
    if (rc == SA_AIS_ERR_BAD_HANDLE) {
      avd_imm_reinit_bg();
    } else if (rc == SA_AIS_ERR_EXIST) {
      /* This may arise if immutil_saImmOiImplementerClear
         failed and amf reinitializes imm interface and
         set impl in avd_imm_reinit_bg_thread.*/
    } else
      goto done;
  } else
    osaf_mutex_unlock_ordie(&imm_reinit_mutex);

  avd_cb->is_implementer = true;

  avd_node_failover(failed_node);
  avd_act_on_sis_in_tol_timer_state();

  LOG_NO("FAILOVER StandBy --> Active DONE!");
  status = NCSCC_RC_SUCCESS;

done:
  if (NCSCC_RC_SUCCESS != status)
    opensaf_reboot(my_node != nullptr ? my_node->node_info.nodeId : 0,
                   my_node != nullptr
                       ? osaf_extended_name_borrow(
                             &my_node->node_info.executionEnvironment)
                       : nullptr,
                   "FAILOVER failed");

  TRACE_LEAVE();
  return status;
}

/****************************************************************************\
 * Function: avd_role_failover_qsd_actv
 *
 * Purpose:  AVSV function to handle AVD's fail-over.
 *
 * Input: cb - AVD control block pointer.
 *        role - Role to be set.
 *
 * Returns: NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE.
 *
 * NOTES:
 *
 *
\**************************************************************************/
static uint32_t avd_role_failover_qsd_actv(AVD_CL_CB *cb, SaAmfHAStateT role) {
  uint32_t status = NCSCC_RC_SUCCESS;
  AVD_AVND *avnd = nullptr;
  AVD_AVND *avnd_other = nullptr;
  AVD_EVT *evt = AVD_EVT_NULL;
  NCSMDS_INFO svc_to_mds_info;
  SaAisErrorT rc;

  TRACE_ENTER();
  LOG_NO("FAILOVER Quiesced --> Active");

  /* If we are in the middle of admin switch, ignore it */
  if (cb->swap_switch == true) {
    cb->swap_switch = false;
  }

  /*
   * Check whether Standby is in sync with Active. If yes then
   * proceed further. Else return failure.
   */
  if (AVD_STBY_OUT_OF_SYNC == cb->stby_sync_state) {
    LOG_ER("FAILOVER Quiesced --> Active FAILED, Stanby OUT OF SYNC");
    return NCSCC_RC_FAILURE;
  }

  if (nullptr == (avnd = avd_node_find_nodeid(cb->node_id_avd))) {
    LOG_ER("FAILOVER Quiesced --> Active FAILED, DB not found");
    return NCSCC_RC_FAILURE;
  }

  /* check the node state */
  if (avnd->node_state != AVD_AVND_STATE_PRESENT) {
    LOG_ER("FAILOVER Quiesced --> Active FAILED, stdby not in good state");
    return NCSCC_RC_FAILURE;
  }

  /* Section to check whether AvD was doing a  role change before getting into
   * failover */
  svc_to_mds_info.i_mds_hdl = cb->vaddr_pwe_hdl;
  svc_to_mds_info.i_svc_id = NCSMDS_SVC_ID_AVD;
  svc_to_mds_info.i_op = MDS_QUERY_DEST;
  svc_to_mds_info.info.query_dest.i_dest = cb->vaddr;
  svc_to_mds_info.info.query_dest.i_svc_id = NCSMDS_SVC_ID_AVD;
  svc_to_mds_info.info.query_dest.i_query_for_role = true;
  svc_to_mds_info.info.query_dest.info.query_for_role.i_anc = 0;  // TODO?

  if (ncsmds_api(&svc_to_mds_info) == NCSCC_RC_SUCCESS) {
    if (svc_to_mds_info.info.query_dest.info.query_for_role.o_vdest_rl ==
        V_DEST_RL_ACTIVE) {
      /* We were in middle of switch, but we had not progresses much with role
       * switch functionality. its ok to just change the NCS SU's who are
       * already quiesced, back to Active. Post an evt on mailbox to set active
       * role to all NCS SU
       *
       */
      AVD_EVT evt = {};
      evt.rcv_evt = AVD_EVT_SWITCH_NCS_SU;

      /* set cb state to active */
      cb->avail_state_avd = role;
      avd_role_switch_ncs_su_evh(cb, &evt);

      if (nullptr !=
          (avnd_other = avd_node_find_nodeid(cb->node_id_avd_other))) {
        /* We are successfully changed role to Active.
           do node down processing for other node */
        avd_node_mark_absent(avnd_other);
      } else {
        LOG_EM("%s:%u: %u", __FILE__, __LINE__, NCSCC_RC_FAILURE);
      }

      return NCSCC_RC_SUCCESS;
      /* END OF THIS FLOW */
    }
  }

  /* We are not in middle of role switch functionality, carry on with normal
   * failover flow */
  avsv_set_ckpt_role(cb, SA_AMF_HA_ACTIVE);

  /* Now Dispatch all the messages from the MBCSv mail-box */
  if (NCSCC_RC_SUCCESS != (status = avsv_mbcsv_dispatch(cb, SA_DISPATCH_ALL))) {
    LOG_ER("FAILOVER Quiesced --> Active FAILED, MBCSV DISPATCH FAILED");
    return NCSCC_RC_FAILURE;
  }

  /*
   * We might be having some async update messages in the
   * Queue to be processed, now drop all of them.
   */
  avsv_dequeue_async_update_msgs(cb, false);

  cb->avail_state_avd = role;

  /* Declare this standby as Active. Set Vdest role and MBCSv role */
  if (NCSCC_RC_SUCCESS != (status = avd_mds_set_vdest_role(cb, role))) {
    LOG_ER("%s: avd_mds_set_vdest_role failed", __FUNCTION__);
  }

  /* Time to send fail-over messages to all the AVND's */
  avd_fail_over_event(cb);

  /* We need to send the role to AvND. */
  status = avd_avnd_send_role_change(cb, cb->node_id_avd, cb->avail_state_avd);
  if (NCSCC_RC_SUCCESS != status) {
    LOG_ER("%s: avd_avnd_send_role_change failed", __FUNCTION__);
  } else {
    avd_d2n_msg_dequeue(cb);
  }

  /* Post an evt on mailbox to set active role to all NCS SU */
  /* create the message event */
  evt = new AVD_EVT();

  evt->rcv_evt = AVD_EVT_SWITCH_NCS_SU;

  if (m_NCS_IPC_SEND(&cb->avd_mbx, evt, NCS_IPC_PRIORITY_HIGH) !=
      NCSCC_RC_SUCCESS) {
    LOG_ER("FAILOVER Quiesced --> Active FAILED, IPC SEND FAILED");
    delete evt;
    return NCSCC_RC_FAILURE;
  }

  /* We are successfully changed role to Active. Gen a reset
   * responce for the other card. TODO */

  cb->node_id_avd_other = 0;

  /* Take mutex to be in sync with imm intf initialization.*/
  osaf_mutex_lock_ordie(&imm_reinit_mutex);
  /* Give up our IMM OI applier role */
  if ((rc = immutil_saImmOiImplementerClear(cb->immOiHandle)) != SA_AIS_OK) {
    osaf_mutex_unlock_ordie(&imm_reinit_mutex);
    LOG_ER("FAILOVER Quiesced --> Active FAILED, ImplementerClear failed %u",
           rc);
    if (rc == SA_AIS_ERR_BAD_HANDLE) {
      avd_imm_reinit_bg();
    } else
      return NCSCC_RC_FAILURE;
  } else
    osaf_mutex_unlock_ordie(&imm_reinit_mutex);

  avd_imm_impl_set_task_create();

  TRACE_LEAVE();
  return NCSCC_RC_SUCCESS;
}

/****************************************************************************\
 * Function: avd_role_switch_ncs_su
 *
 * Purpose:  AVSV function to handle the situation where the AvD has succesfully
transitioned *           to the active role. This function will make all the NCS
SUs as active.
 *
 * Input: cb - AVD control block pointer.
 *        role - Role to be set.
 *
 * Returns: None.
 *
 * NOTES:   None.
 *
 *
\**************************************************************************/
void avd_role_switch_ncs_su_evh(AVD_CL_CB *cb, AVD_EVT *evt) {
  AVD_AVND *avnd = nullptr, *other_avnd = nullptr;

  TRACE_ENTER();

  /* get the avnd from node_id */
  if (nullptr == (avnd = avd_node_find_nodeid(cb->node_id_avd))) {
    LOG_EM("%s:%u: %u", __FILE__, __LINE__, cb->node_id_avd);
    return;
  }
  other_avnd = avd_node_find_nodeid(cb->node_id_avd_other);

  for (const auto &i_su : avnd->list_of_ncs_su) {
    if ((i_su->list_of_susi != 0) &&
        (i_su->sg_of_su->sg_redundancy_model == SA_AMF_2N_REDUNDANCY_MODEL) &&
        (i_su->list_of_susi->state != SA_AMF_HA_ACTIVE)) {
      if (nullptr != other_avnd) {
        avd_sg_su_si_mod_snd(cb, i_su, SA_AMF_HA_ACTIVE);
      } else {
        avd_sg_su_oper_list_add(cb, i_su, false);
        i_su->set_su_switch(AVSV_SI_TOGGLE_SWITCH);
        i_su->sg_of_su->set_fsm_state(AVD_SG_FSM_SU_OPER);
      }
    }
  }

  TRACE_LEAVE();
}
/**
 * @brief  Resets admin op params for all admin op capable AMF entities during
 *         when active AMFD gives implementer role during controller switchover.
 */
static void reset_admin_op_params_after_impl_clear() {
  TRACE_ENTER();
  // For ng.
  for (const auto &value : *nodegroup_db) {
    AVD_AMF_NG *ng = value.second;
    if (ng->admin_ng_pend_cbk.invocation != 0) {
      ng->admin_ng_pend_cbk.invocation = 0;
      ng->admin_ng_pend_cbk.admin_oper = static_cast<SaAmfAdminOperationIdT>(0);
      for (std::set<std::string>::const_iterator iter =
               ng->saAmfNGNodeList.begin();
           iter != ng->saAmfNGNodeList.end(); ++iter) {
        AVD_AVND *node = avd_node_get(*iter);
        node->admin_ng = nullptr;
      }
    }
  }
  // For Node.
  for (const auto &value : *node_id_db) {
    AVD_AVND *node = value.second;
    node->admin_node_pend_cbk.invocation = 0;
    node->admin_node_pend_cbk.admin_oper =
        static_cast<SaAmfAdminOperationIdT>(0);
  }
  // For SI.
  for (const auto &value : *si_db) {
    AVD_SI *si = value.second;
    si->invocation = 0;
  }
  // For SG.
  for (const auto &value : *sg_db) {
    AVD_SG *sg = value.second;
    sg->adminOp_invocationId = 0;
    sg->adminOp = static_cast<SaAmfAdminOperationIdT>(0);
  }
  // For SU.
  for (const auto &value : *su_db) {
    AVD_SU *su = value.second;
    su->pend_cbk.invocation = 0;
    su->pend_cbk.admin_oper = static_cast<SaAmfAdminOperationIdT>(0);
  }
  TRACE_LEAVE();
}
/*****************************************************************************
 * Function: avd_mds_qsd_role_func
 *
 * Purpose:  This function is the handler for the MDS event indicating that the
 * the role  of the AvD is now quiesced.
 *
 * Input: cb - the AVD control block
 *        evt - The event information.
 *
 * Returns: None.
 *
 * NOTES: None.
 *
 *
 **************************************************************************/

void avd_mds_qsd_role_evh(AVD_CL_CB *cb, AVD_EVT *evt) {
  uint32_t status = NCSCC_RC_SUCCESS;
  uint32_t rc = NCSCC_RC_SUCCESS;

  TRACE_ENTER();
  /* Only accept this event in controller switch-over state, in other
   * states it is invalid and indicates severe cluster problems.
   */
  if (cb->swap_switch == false) {
    LOG_NO("%s: MDS unexpectedly changed role to QUIESCED", __FUNCTION__);
    LOG_CR("Duplicate ACTIVE detected, exiting");
    _exit(EXIT_FAILURE);  // should never get here...
  }

try_again:
  /* Execute admin op jobs before calling saImmOiImplementerClear to avoid
   * SA_AIS_ERR_TIMEOUT
   */
  Fifo::executeAdminResp(cb);

  /* Take mutex here to sync with imm reinit thread.*/
  osaf_mutex_lock_ordie(&imm_reinit_mutex);
  /* Give up IMM OI implementer role */
  if ((rc = immutil_saImmOiImplementerClear(cb->immOiHandle)) != SA_AIS_OK) {
    osaf_mutex_unlock_ordie(&imm_reinit_mutex);
    LOG_ER("FAILOVER Active --> Quiesced FAILED, ImplementerClear failed %u",
           rc);
    if (rc == SA_AIS_ERR_BAD_HANDLE) {
      avd_imm_reinit_bg();
    } else if (rc == SA_AIS_ERR_TIMEOUT) {
      goto try_again;
    } else
      osafassert(0);
  } else
    osaf_mutex_unlock_ordie(&imm_reinit_mutex);

  cb->is_implementer = false;

  /* Throw away all pending IMM updates, no longer implementer */
  Fifo::empty();

  /* Take mutex here, to sync with above avd_imm_reinit_bg call. */
  osaf_mutex_lock_ordie(&imm_reinit_mutex);
  if ((rc = avd_imm_applier_set()) != SA_AIS_OK) {
    osaf_mutex_unlock_ordie(&imm_reinit_mutex);
    LOG_ER("avd_imm_applier_set FAILED, %u", rc);
    if (rc == SA_AIS_ERR_BAD_HANDLE) {
      avd_imm_reinit_bg();
    } else if ((rc == SA_AIS_ERR_EXIST) || (rc == SA_AIS_ERR_INVALID_PARAM)) {
      /* This may arise if immutil_saImmOiImplementerClear
         failed and amf reinitializes imm interface and
         set applier in avd_imm_reinit_bg_thread. Imm may
         return ERR_EXIST or INVALID_PARAM. */
      TRACE("ERR_EXIST or INVALID_PARAM");
    } else if (rc == SA_AIS_ERR_TIMEOUT) {
      /* Let it proceed as there may be a case of Immd not
         reachable i.e. node might have gone down during
         switchover. */
      TRACE("SA_AIS_ERR_TIMEOUT");
    } else
      osafassert(0);
  } else
    osaf_mutex_unlock_ordie(&imm_reinit_mutex);

  /* Now set the MBCSv role to quiesced, */
  if (NCSCC_RC_SUCCESS !=
      (status = avsv_set_ckpt_role(cb, SA_AMF_HA_QUIESCED))) {
    /* Log error */
    LOG_EM("%s:%u: %u", __FILE__, __LINE__, status);
  }

  /* Now Dispatch all the messages from the MBCSv mail-box */
  if (NCSCC_RC_SUCCESS != (rc = avsv_mbcsv_dispatch(cb, SA_DISPATCH_ALL))) {
    LOG_EM("%s:%u: %u", __FILE__, __LINE__, cb->node_id_avd_other);
    cb->swap_switch = false;
    return;
  }

  /* Send the message to the other AVD to go active */
  if (NCSCC_RC_SUCCESS !=
      (rc = avd_d2d_chg_role_req(cb, AVD_SWITCH_OVER, SA_AMF_HA_ACTIVE))) {
    /* Log error */
    LOG_ER(
        "Unable to send the AMFD change role request from Standby to Active");
    cb->swap_switch = false;
    amfd_switch_qsd_actv(cb);
  }
  /*
     Since this AMFD has given up implementor role, clear admin operation params
     for admin operation on AMF entities.
   */
  reset_admin_op_params_after_impl_clear();
  TRACE_LEAVE();
}

/*****************************************************************************
 * Function: admin_switch
 *
 * Purpose:
 *
 * Input: cb - the AVD control block
 *
 * Returns:
 *
 * NOTES: None.
 *
 *
 **************************************************************************/

void amfd_switch(AVD_CL_CB *cb) {
  TRACE_ENTER();

  LOG_NO("Controller switch over initiated");
  saflog(LOG_NOTICE, amfSvcUsrName, "Controller switch over initiated");

  /* First check if there are any other SI's that are any other active */
  /* get the avnd from node_id */
  AVD_AVND *avnd = avd_node_find_nodeid(cb->node_id_avd);

  for (const auto &i_su : avnd->list_of_ncs_su) {
    if ((i_su->list_of_susi != 0) &&
        (i_su->sg_of_su->sg_redundancy_model == SA_AMF_2N_REDUNDANCY_MODEL) &&
        (i_su->list_of_susi->state == SA_AMF_HA_ACTIVE)) {
      /* Still some SI on the local SU are ACTIVE, Dont switch the AMFD */
      return;
    }
  }

  cb->swap_switch = true;
  /* Post a evt to the AVD mailbox for the admin switch role change */
  if (NCSCC_RC_SUCCESS !=
      avd_post_amfd_switch_role_change_evt(cb, SA_AMF_HA_QUIESCED)) {
    LOG_ER("unable to post to mailbox for amfd switch");
    cb->swap_switch = false;
    return;
  }

  TRACE_LEAVE();
}

/*****************************************************************************
 * Function: avd_post_amfd_switch_role_change_evt
 *
 * Purpose:
 *
 * Input: cb - the AVD control block
 *
 * Returns:
 *
 * NOTES: None.
 *
 *
 **************************************************************************/

uint32_t avd_post_amfd_switch_role_change_evt(AVD_CL_CB *cb,
                                              SaAmfHAStateT role) {
  AVD_EVT *evt;

  evt = new AVD_EVT;

  evt->rcv_evt = AVD_EVT_ROLE_CHANGE;
  evt->info.avd_msg = new AVD_D2D_MSG;
  evt->info.avd_msg->msg_type = AVD_D2D_CHANGE_ROLE_REQ;
  evt->info.avd_msg->msg_info.d2d_chg_role_req.cause = AVD_SWITCH_OVER;
  evt->info.avd_msg->msg_info.d2d_chg_role_req.role = role;

  uint32_t rc =
      ncs_ipc_send(&avd_cb->avd_mbx, (NCS_IPC_MSG *)evt,
                   static_cast<NCS_IPC_PRIORITY>(MDS_SEND_PRIORITY_HIGH));
  osafassert(rc == NCSCC_RC_SUCCESS);
  return rc;
}

/*****************************************************************************
 * Function: avd_d2d_chg_role_req
 *
 * Purpose:
 *
 * Input: cb - the AVD control block
 *
 * Returns:
 *
 * NOTES: None.
 *
 *
 **************************************************************************/

uint32_t avd_d2d_chg_role_req(AVD_CL_CB *cb, AVD_ROLE_CHG_CAUSE_T cause,
                              SaAmfHAStateT role) {
  AVD_D2D_MSG d2d_msg;
  TRACE_ENTER();

  if ((cb->node_id_avd_other == 0) || (cb->other_avd_adest == 0)) {
    return NCSCC_RC_FAILURE;
  }

  /* prepare the message. */

  d2d_msg.msg_type = AVD_D2D_CHANGE_ROLE_REQ;
  d2d_msg.msg_info.d2d_chg_role_req.role = role;
  d2d_msg.msg_info.d2d_chg_role_req.cause = cause;

  /* send the message */
  if (avd_d2d_msg_snd(cb, &d2d_msg) != NCSCC_RC_SUCCESS) {
    /* log error that the director is not able to send the message */
    LOG_ER("MDS Send failed for AMFD Role Change REQ");
    return NCSCC_RC_FAILURE;
  }

  TRACE_LEAVE();

  return NCSCC_RC_SUCCESS;
}

/*****************************************************************************
 * Function: avd_d2d_chg_role_rsp
 *
 * Purpose:
 *
 * Input: cb - the AVD control block
 *
 * Returns:
 *
 * NOTES: None.
 *
 *
 **************************************************************************/

uint32_t avd_d2d_chg_role_rsp(AVD_CL_CB *cb, uint32_t status,
                              SaAmfHAStateT role) {
  AVD_D2D_MSG d2d_msg;

  TRACE_ENTER();

  if ((cb->node_id_avd_other == 0) || (cb->other_avd_adest == 0)) {
    return NCSCC_RC_FAILURE;
  }

  /* prepare the message. */
  d2d_msg.msg_type = AVD_D2D_CHANGE_ROLE_RSP;
  d2d_msg.msg_info.d2d_chg_role_rsp.role = role;
  d2d_msg.msg_info.d2d_chg_role_rsp.status = status;

  /* send the message */
  if (avd_d2d_msg_snd(cb, &d2d_msg) != NCSCC_RC_SUCCESS) {
    /* log error that the director is not able to send the message */
    LOG_ER("MDS Send failed for AFMD Role Change RSP");
    return NCSCC_RC_FAILURE;
  }

  TRACE_LEAVE();
  return NCSCC_RC_SUCCESS;
}

/****************************************************************************\
 * Function: amfd_switch_actv_qsd
 *
 * Purpose:  function to handle AVD's role
 *           change from Active to Quiesced in liue of SI SWAP Function
 *
 * Input: cb - AVD control block pointer.
 *
 * Returns: NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE.
 *
 * NOTES:
 *
 *
 **************************************************************************/

uint32_t amfd_switch_actv_qsd(AVD_CL_CB *cb) {
  AVD_AVND *avnd = nullptr;
  AVD_AVND *avnd_other = nullptr;
  uint32_t rc = NCSCC_RC_SUCCESS;

  TRACE_ENTER();
  LOG_NO("ROLE SWITCH Active --> Quiesced");

  if (cb->init_state != AVD_APP_STATE) {
    LOG_ER("ROLE SWITCH Active --> Quiesced, AMFD not in APP State");
    cb->swap_switch = false;
    return NCSCC_RC_FAILURE;
  }

  /* get the avnd from node_id */
  if (nullptr == (avnd = avd_node_find_nodeid(cb->node_id_avd))) {
    LOG_ER("ROLE SWITCH Active --> Quiesced, Local Node not found");
    cb->swap_switch = false;
    return NCSCC_RC_FAILURE;
  }

  /* get the avnd from node_id */
  if (nullptr == (avnd_other = avd_node_find_nodeid(cb->node_id_avd_other))) {
    LOG_ER("ROLE SWITCH Active --> Quiesced, remote Node not found");
    cb->swap_switch = false;
    return NCSCC_RC_FAILURE;
  }

  /* check if the other AVD is up and has standby role. verify that
   * the AvND on the node is in present state.
   */
  if ((cb->node_id_avd_other == 0) ||
      (avnd->node_state != AVD_AVND_STATE_PRESENT) ||
      (avnd_other->node_state != AVD_AVND_STATE_PRESENT)) {
    LOG_NO("Peer controller not available for switch over");
    cb->swap_switch = false;
    return NCSCC_RC_FAILURE;
  }

  /*  Mark AVD as Quiesced. */
  cb->avail_state_avd = SA_AMF_HA_QUIESCED;

  avd_clm_track_stop();

  /* Go ahead and set mds role as already the NCS SU has been switched */
  if (NCSCC_RC_SUCCESS !=
      (rc = avd_mds_set_vdest_role(cb, SA_AMF_HA_QUIESCED))) {
    LOG_ER("ROLE SWITCH Active --> Quiesced, vdest role set failed");
    cb->swap_switch = false;
    return NCSCC_RC_FAILURE;
  }

  /* We need to send the role to AvND. */
  rc = avd_avnd_send_role_change(cb, cb->node_id_avd, cb->avail_state_avd);
  if (NCSCC_RC_SUCCESS != rc) {
    LOG_ER("%s: avd_avnd_send_role_change failed", __FUNCTION__);
  } else {
    avd_d2n_msg_dequeue(cb);
  }

  TRACE_LEAVE();
  return NCSCC_RC_SUCCESS;
}

/****************************************************************************\
 * Function: amfd_switch_qsd_stdby
 *
 * Purpose:  function to handle AVD's role switch from Quiesced to
 *           Standby.
 *
 * Input: cb - AVD control block pointer.
 *
 * Returns: NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE.
 *
 * NOTES:
 *
 *
 **************************************************************************/

uint32_t amfd_switch_qsd_stdby(AVD_CL_CB *cb) {
  uint32_t status = NCSCC_RC_SUCCESS;
  SaAisErrorT ais_rc;
  TRACE_ENTER();
  LOG_NO("Switching Quiesced --> StandBy");

  cb->swap_switch = false;

  if (NCSCC_RC_SUCCESS !=
      (status = avd_mds_set_vdest_role(cb, SA_AMF_HA_STANDBY))) {
    LOG_ER("Switch Quiesced --> StandBy, Vdest set role failed");
    return NCSCC_RC_FAILURE;
  }

  /* Change MBCSv role to Standby */
  if (NCSCC_RC_SUCCESS !=
      (status = avsv_set_ckpt_role(cb, SA_AMF_HA_STANDBY))) {
    LOG_ER("Switch Quiesced --> StandBy, MBCSv set role failed");
    return NCSCC_RC_FAILURE;
  }

  /*Stop sending ckpt updates by changing avd role to standby.*/
  cb->avail_state_avd = SA_AMF_HA_STANDBY;
  /* Now Dispatch all the messages from the MBCSv mail-box */
  if (NCSCC_RC_SUCCESS != (status = avsv_mbcsv_dispatch(cb, SA_DISPATCH_ALL))) {
    LOG_ER("Switch Quiesced --> StandBy, MBCSv dispatch failed");
    return NCSCC_RC_FAILURE;
  }

  /* Walk through all the nodes and  free PG records. */
  for (const auto &value : *node_id_db) {
    AVD_AVND *avnd = value.second;
    avd_pg_node_csi_del_all(cb, avnd);
  }

  if (cb->is_clm_track_started == true) {
    ais_rc = avd_clm_track_stop();
    if (ais_rc != SA_AIS_OK && ais_rc != SA_AIS_ERR_NOT_EXIST) {
      LOG_ER("Failed to stop cluster tracking after switch over");
    }
  }

  LOG_NO("Controller switch over done");
  saflog(LOG_NOTICE, amfSvcUsrName, "Controller switch over done at %x",
         cb->node_id_avd);

  TRACE_LEAVE();
  return NCSCC_RC_SUCCESS;
}

/****************************************************************************\
 * Function: amfd_switch_stdby_actv
 *
 * Purpose:  function to handle AVD's role
 *           change from standby to active in liue of SI SWAP Action
 *
 * Input: cb - AVD control block pointer.
 *
 * Returns: NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE.
 *
 * NOTES:
 *
 *
 **************************************************************************/

uint32_t amfd_switch_stdby_actv(AVD_CL_CB *cb) {
  uint32_t status = NCSCC_RC_SUCCESS;
  SaAisErrorT rc;

  TRACE_ENTER();

  /*
   * Check whether Standby is in sync with Active. If yes then
   * proceed further. Else return failure.
   */
  if (AVD_STBY_OUT_OF_SYNC == cb->stby_sync_state) {
    LOG_ER("Switch Standby --> Active FAILED, Standby OUT OF SYNC");
    cb->swap_switch = false;
    avd_d2d_chg_role_rsp(cb, NCSCC_RC_FAILURE, SA_AMF_HA_ACTIVE);
    return NCSCC_RC_FAILURE;
  }

  if (NCSCC_RC_SUCCESS != (status = avsv_set_ckpt_role(cb, SA_AMF_HA_ACTIVE))) {
    LOG_ER("Switch Standby --> Active FAILED, Mbcsv role set failed");
    cb->swap_switch = false;
    avd_d2d_chg_role_rsp(cb, NCSCC_RC_FAILURE, SA_AMF_HA_ACTIVE);
    return NCSCC_RC_FAILURE;
  }

  /* Now Dispatch all the messages from the MBCSv mail-box */
  if (NCSCC_RC_SUCCESS != (status = avsv_mbcsv_dispatch(cb, SA_DISPATCH_ALL))) {
    LOG_ER("Switch Standby --> Active FAILED, Mbcsv Dispatch failed");
    cb->swap_switch = false;
    avd_d2d_chg_role_rsp(cb, NCSCC_RC_FAILURE, SA_AMF_HA_ACTIVE);
    return NCSCC_RC_FAILURE;
  }

  /*
   * We might be having some async update messages in the
   * Queue to be processed, now drop all of them.
   */
  avsv_dequeue_async_update_msgs(cb, false);

  /* Take mutex before changing role as it may impact logic
     in avd_imm_reinit_bg_thread. If mutex is taken for imm
     initialization, then wait for its completion before changing role.*/
  osaf_mutex_lock_ordie(&imm_reinit_mutex);
  cb->avail_state_avd = SA_AMF_HA_ACTIVE;
  osaf_mutex_unlock_ordie(&imm_reinit_mutex);

  /* Declare this standby as Active. Set Vdest role role */
  if (NCSCC_RC_SUCCESS !=
      (status = avd_mds_set_vdest_role(cb, SA_AMF_HA_ACTIVE))) {
    LOG_ER("Switch Standby --> Active FAILED, MDS role set failed");
    cb->swap_switch = false;
    avd_d2d_chg_role_rsp(cb, NCSCC_RC_FAILURE, SA_AMF_HA_ACTIVE);
    return NCSCC_RC_FAILURE;
  }

  /* Time to send fail-over messages to all the AVND's */
  avd_fail_over_event(cb);

  /* We need to send the role to AvND. */
  status = avd_avnd_send_role_change(cb, cb->node_id_avd, cb->avail_state_avd);
  if (NCSCC_RC_SUCCESS != status) {
    LOG_ER("%s: avd_avnd_send_role_change failed", __FUNCTION__);
  } else {
    avd_d2n_msg_dequeue(cb);
  }
  cb->swap_switch = false;

  /* Give up our IMM OI Applier role */
  /* There is no need to take mutex in ImplClear because if imm
     initialization were undergoing, above mutex took care. */
  if ((rc = immutil_saImmOiImplementerClear(cb->immOiHandle)) != SA_AIS_OK) {
    LOG_ER("Switch Standby --> Active FAILED, ImplementerClear failed %u", rc);
    if (rc == SA_AIS_ERR_BAD_HANDLE) {
      avd_imm_reinit_bg();
    } else {
      cb->swap_switch = false;
      avd_d2d_chg_role_rsp(cb, NCSCC_RC_FAILURE, SA_AMF_HA_ACTIVE);
      return NCSCC_RC_FAILURE;
    }
  }

  /* Take mutex here, to sync with above avd_imm_reinit_bg call. */
  osaf_mutex_lock_ordie(&imm_reinit_mutex);
  if ((rc = immutil_saImmOiImplementerSet(avd_cb->immOiHandle,
                                          const_cast<SaImmOiImplementerNameT>(
                                              "safAmfService"))) != SA_AIS_OK) {
    LOG_ER("Switch Standby --> Active, ImplementerSet failed %u", rc);
    osaf_mutex_unlock_ordie(&imm_reinit_mutex);
    if (rc == SA_AIS_ERR_BAD_HANDLE) {
      avd_imm_reinit_bg();
    } else if (rc == SA_AIS_ERR_EXIST) {
      /* This may arise if immutil_saImmOiImplementerClear
         failed and amf reinit imm interface and set impl
         in avd_imm_reinit_bg_thread.*/
    } else {
      avd_d2d_chg_role_rsp(cb, NCSCC_RC_FAILURE, SA_AMF_HA_ACTIVE);
      return NCSCC_RC_FAILURE;
    }
  } else
    osaf_mutex_unlock_ordie(&imm_reinit_mutex);

  avd_cb->is_implementer = true;
  avd_cb->active_services_exist = true;

  if (NCSCC_RC_SUCCESS != avd_rde_set_role(SA_AMF_HA_ACTIVE)) {
    LOG_ER("rde role change failed from stdy -> Active");
  }

  if (avd_clm_track_start() != SA_AIS_OK) {
    LOG_ER("Switch Standby --> Active, clm track start failed");
    avd_d2d_chg_role_rsp(cb, NCSCC_RC_FAILURE, SA_AMF_HA_ACTIVE);
    return NCSCC_RC_FAILURE;
  }

  /* Send the message to other avd for role change rsp as success */
  avd_d2d_chg_role_rsp(cb, NCSCC_RC_SUCCESS, SA_AMF_HA_ACTIVE);

  avd_act_on_sis_in_tol_timer_state();
  /* Screen through all the SG's in the sg database and update si's si_dep_state
   */
  for (const auto &value : *sg_db) {
    AVD_SG *sg = value.second;
    if (sg->sg_fsm_state == AVD_SG_FSM_STABLE) {
      avd_sidep_update_si_dep_state_for_all_sis(sg);
      avd_sidep_sg_take_action(sg);
    }
  }

  TRACE_LEAVE();
  return NCSCC_RC_SUCCESS;
}

/****************************************************************************\
 * Function: amfd_switch_qsd_actv
 *
 * Purpose:  function to handle AVD's role switch from Quiesced to
 *           Active.
 *
 * Input: cb - AVD control block pointer.
 *
 * Returns: NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE.
 *
 * NOTES:
 *
 *
 **************************************************************************/

uint32_t amfd_switch_qsd_actv(AVD_CL_CB *cb) {
  LOG_NO("ROLE SWITCH Quiesced --> Active");
  cb->swap_switch = false;
  if (NCSCC_RC_SUCCESS != avd_rde_set_role(SA_AMF_HA_ACTIVE)) {
    LOG_ER("rde role change failed from qsd -> actv");
  }
  return amfd_switch_stdby_actv(cb);
}

/****************************************************************************\
 * Function: avd_rde_set_role
 *
 * Purpose:  set the role of RDE
 *
 * Input: role - Role to be set of RDE
 *
 * Returns: NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE.
 *
 * NOTES:
 *
 *
 **************************************************************************/

uint32_t avd_rde_set_role(SaAmfHAStateT role) {
  uint32_t rc = NCSCC_RC_SUCCESS;
  PCS_RDA_REQ pcs_rda_req;

  pcs_rda_req.req_type = PCS_RDA_SET_ROLE;

  pcs_rda_req.info.io_role = static_cast<PCS_RDA_ROLE>(role);

  if (PCSRDA_RC_SUCCESS != pcs_rda_request(&pcs_rda_req)) {
    return NCSCC_RC_FAILURE;
  }
  return rc;
}
