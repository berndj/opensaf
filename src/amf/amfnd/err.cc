/*      -*- OpenSAF  -*-
 *
 * (C) Copyright 2008 The OpenSAF Foundation
 * Copyright (C) 2017, Oracle and/or its affiliates. All rights reserved.
 * Copyright (C) 2017, Ericsson AB. All rights reserved.
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
 *            Ericsson
 *
 */

/*****************************************************************************
..............................................................................

..............................................................................

  DESCRIPTION:

  This file contains routines to process the component errors and execute
  various recoveries for those errors.

  The following table enumerates various sources of component error detection,
  the detection responsibility & the source of recommended recovery.
 +--------------------------------------------------------------------------+
 |    Error Source    |  Error Detection  |       Recommended Recovery      |
 |                    |   Responsibility  |                                 |
 +--------------------+-------------------+---------------------------------+
 | a) Error Report    | Component         | Specified by component as a part|
 |                    |                   | of error report.                |
 +--------------------+-------------------+---------------------------------+
 | b) Healthcheck     | AMF & Component   | Specified by component as a part|
 |    Failure         |                   | of healthcheck start.           |
 +--------------------+-------------------+---------------------------------+
 | c) Callback Resp   | AMF               | Same as b) for healtcheck       |
 |    Timer Expiry    |                   | callback failure.               |
 |                    |                   | Else default comp recovery      |
 |                    |                   | specified as a part of          |
 |                    |                   | configuration.                  |
 +--------------------+-------------------+---------------------------------+
 | d) Callback Failed | Component         | same as c)                      |
 |    Response (thru  |                   |                                 |
 |    saAmfResponse)  |                   |                                 |
 +--------------------+-------------------+---------------------------------+
..............................................................................

  FUNCTIONS INCLUDED in this module:


******************************************************************************
*/

#include "amf/amfnd/avnd.h"

/* static function declarations */

static uint32_t avnd_err_escalate(AVND_CB *, AVND_SU *, AVND_COMP *,
                                  uint32_t *);

static uint32_t avnd_err_recover(AVND_CB *, AVND_SU *, AVND_COMP *, uint32_t);

static uint32_t avnd_err_rcvr_comp_restart(AVND_CB *, AVND_COMP *);
static uint32_t avnd_err_rcvr_su_restart(AVND_CB *, AVND_SU *, AVND_COMP *);
static uint32_t avnd_err_rcvr_comp_failover(AVND_CB *, AVND_COMP *);
static uint32_t avnd_err_rcvr_su_failover(AVND_CB *, AVND_SU *, AVND_COMP *);
static uint32_t avnd_err_rcvr_node_switchover(AVND_CB *, AVND_SU *,
                                              AVND_COMP *);
static uint32_t avnd_err_rcvr_node_failover(AVND_CB *, AVND_SU *, AVND_COMP *);
static uint32_t avnd_err_rcvr_node_failfast(AVND_CB *, AVND_SU *, AVND_COMP *);

static uint32_t avnd_err_esc_comp_restart(AVND_CB *, AVND_SU *,
                                          AVSV_ERR_RCVR *);
static AVSV_ERR_RCVR avnd_err_esc_su_failover(AVND_CB *, AVND_SU *,
                                              AVSV_ERR_RCVR *);
static uint32_t avnd_err_restart_esc_level_0(AVND_CB *, AVND_SU *,
                                             AVND_ERR_ESC_LEVEL *,
                                             AVSV_ERR_RCVR *);
static uint32_t avnd_err_restart_esc_level_1(AVND_CB *, AVND_SU *,
                                             AVND_ERR_ESC_LEVEL *,
                                             AVSV_ERR_RCVR *);
static uint32_t avnd_err_restart_esc_level_2(AVND_CB *, AVND_SU *,
                                             AVND_ERR_ESC_LEVEL *,
                                             AVSV_ERR_RCVR *);

/* LSB Changes. Strings to represent source of component Error */

static const char *g_comp_err[] = {"noError",
                                   "errorReport",
                                   "healthCheckFailed",
                                   "passiveMonitorFailed",
                                   "activeMonitorFailed",
                                   "cLCCommandFailed",
                                   "healthCheckcallbackTimeout",
                                   "healthCheckcallbackFailed",
                                   "avaDown",
                                   "prxRegTimeout",
                                   "csiSetcallbackTimeout",
                                   "csiRemovecallbackTimeout",
                                   "csiSetcallbackFailed",
                                   "csiRemovecallbackFailed",
                                   "qscingCompleteTimeout",
                                   "csiAttributeChangeCallbackTimeout",
                                   "csiAttributeChangeCallbackFailed"};

static const char *g_comp_rcvr[] = {
    "noRecommendation", "componentRestart",   "componentFailover",
    "nodeSwitchover",   "nodeFailover",       "nodeFailfast",
    "clusterReset",     "applicationRestart", "containerRestart",
    "suRestart",        "suFailover"};

static void log_recovery_escalation(const AVND_COMP &comp,
                                    const uint32_t previous_esc_rcvr,
                                    const uint32_t esc_rcvr) {
  if (esc_rcvr != previous_esc_rcvr) {
    LOG_NO("'%s' recovery action escalated from '%s' to '%s'",
           comp.name.c_str(), g_comp_rcvr[previous_esc_rcvr - 1],
           g_comp_rcvr[esc_rcvr - 1]);
  }
}

/****************************************************************************
  Name          : avnd_evt_ava_err_rep

  Description   : This routine processes the AMF Error Report message from
                  AvA. It validates the message, updates the component record
                  with the error info & starts the component error processing.

  Arguments     : cb  - ptr to the AvND control block
                  evt - ptr to the AvND event

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avnd_evt_ava_err_rep_evh(AVND_CB *cb, AVND_EVT *evt) {
  AVSV_AMF_API_INFO *api_info = &evt->info.ava.msg->info.api_info;
  AVSV_AMF_ERR_REP_PARAM *err_rep = &api_info->param.err_rep;
  AVND_COMP *comp = 0;
  AVND_ERR_INFO err;
  uint32_t rc = NCSCC_RC_SUCCESS;
  SaAisErrorT amf_rc = SA_AIS_OK;
  bool msg_from_avnd = false, int_ext_comp = false;

  TRACE_ENTER();

  if (AVND_EVT_AVND_AVND_MSG == evt->type) {
    /* This means that the message has come from proxy AvND to this AvND. */
    msg_from_avnd = true;
  }

  if (false == msg_from_avnd) {
    /* Check for internode or external coomponent first
       If it is, then forward it to the respective AvND. */
    rc = avnd_int_ext_comp_hdlr(cb, api_info, &evt->mds_ctxt, &amf_rc,
                                &int_ext_comp);
    if (true == int_ext_comp) {
      goto done;
    }
  }

  /* check if component exists on local AvND node */
  comp = avnd_compdb_rec_get(cb->compdb, Amf::to_string(&err_rep->err_comp));
  if (!comp) amf_rc = SA_AIS_ERR_NOT_EXIST;

  /* determine other error codes, if any */

  /* We need not entertain errors when comp is not in shape */
  if (comp && (m_AVND_COMP_PRES_STATE_IS_UNINSTANTIATED(comp) ||
               m_AVND_COMP_PRES_STATE_IS_INSTANTIATIONFAILED(comp) ||
               m_AVND_COMP_PRES_STATE_IS_TERMINATIONFAILED(comp)))
    amf_rc = SA_AIS_ERR_INVALID_PARAM;

  if (comp && (true == comp->su->su_is_external) && ((true == msg_from_avnd))) {
    /* This request has come from other AvND and it is for external component.
       Recommended recovery SA_AMF_NODE_SWITCHOVER,
       SA_AMF_NODE_FAILOVER, SA_AMF_NODE_FAILFAST or SA_AMF_CLUSTER_RESET
       are not allowed for external component. */
    if ((SA_AMF_NODE_SWITCHOVER == err_rep->rec_rcvr.saf_amf) ||
        (SA_AMF_NODE_FAILOVER == err_rep->rec_rcvr.saf_amf) ||
        (SA_AMF_NODE_FAILFAST == err_rep->rec_rcvr.saf_amf) ||
        (SA_AMF_CLUSTER_RESET == err_rep->rec_rcvr.saf_amf))
      amf_rc = SA_AIS_ERR_INVALID_PARAM;
  }

  if (comp && ((err_rep->rec_rcvr.saf_amf == SA_AMF_APPLICATION_RESTART) ||
               (err_rep->rec_rcvr.saf_amf == SA_AMF_CONTAINER_RESTART)))
    amf_rc = SA_AIS_ERR_NOT_SUPPORTED;

  if (comp && (comp->su->is_ncs == true) &&
      (err_rep->rec_rcvr.saf_amf == SA_AMF_CLUSTER_RESET)) {
    LOG_NO("Cluster Reset recovery not supported for MW components '%s'",
           comp->name.c_str());
    amf_rc = SA_AIS_ERR_NOT_SUPPORTED;
  }
  /* send the response back to AvA */
  rc = avnd_amf_resp_send(cb, AVSV_AMF_ERR_REP, amf_rc, 0, &api_info->dest,
                          &evt->mds_ctxt, comp, msg_from_avnd);

  /* thru with validation.. process the rest */
  if ((SA_AIS_OK == amf_rc) && (NCSCC_RC_SUCCESS == rc)) {
    /* if time's not specified, use current time TBD */
    if (err_rep->detect_time > 0)
      comp->err_info.detect_time = err_rep->detect_time;
    else
      m_GET_TIME_STAMP(comp->err_info.detect_time);

    /*** process the error ***/
    err.src = AVND_ERR_SRC_REP;
    err.rec_rcvr.raw = err_rep->rec_rcvr.raw;
    rc = avnd_err_process(cb, comp, &err);
  }

done:
  if (NCSCC_RC_SUCCESS != rc) {
    LOG_ER("avnd_evt_ava_err_rep():%s:Hdl=%llx, rec_rcvr:%u",
           osaf_extended_name_borrow(&err_rep->err_comp), err_rep->hdl,
           err_rep->rec_rcvr.raw);
  }

  TRACE_LEAVE();
  return rc;
}

/****************************************************************************
  Name          : avnd_evt_ava_err_clear

  Description   : This routine processes the AMF Error Clear message from
                  AvA. It clears the component error info.

  Arguments     : cb  - ptr to the AvND control block
                  evt - ptr to the AvND event

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avnd_evt_ava_err_clear_evh(AVND_CB *cb, AVND_EVT *evt) {
  AVSV_AMF_API_INFO *api_info = &evt->info.ava.msg->info.api_info;
  AVSV_AMF_ERR_CLEAR_PARAM *err_clear = &api_info->param.err_clear;
  AVND_COMP *comp = 0;
  AVND_CERR_INFO *err_info;
  uint32_t rc = NCSCC_RC_SUCCESS;
  SaAisErrorT amf_rc = SA_AIS_OK;
  bool msg_from_avnd = false, int_ext_comp = false;

  TRACE_ENTER();

  if (AVND_EVT_AVND_AVND_MSG == evt->type) {
    /* This means that the message has come from proxy AvND to this AvND. */
    msg_from_avnd = true;
  }

  if (false == msg_from_avnd) {
    /* Check for internode or external coomponent first
       If it is, then forward it to the respective AvND. */
    rc = avnd_int_ext_comp_hdlr(cb, api_info, &evt->mds_ctxt, &amf_rc,
                                &int_ext_comp);
    if (true == int_ext_comp) {
      goto done;
    }
  }

  /* check if component exists on local AvND node */
  comp = avnd_compdb_rec_get(cb->compdb, Amf::to_string(&err_clear->comp_name));
  if (!comp) amf_rc = SA_AIS_ERR_NOT_EXIST;

  /* determine other error codes, if any */

  if ((comp) && (!m_AVND_COMP_IS_REG(comp) ||
      (!m_AVND_COMP_TYPE_IS_PREINSTANTIABLE(comp) &&
       !m_AVND_COMP_TYPE_IS_PROXIED(comp))))
    amf_rc = SA_AIS_ERR_BAD_OPERATION;

  if ((comp) && m_AVND_COMP_OPER_STATE_IS_ENABLED(comp))
    amf_rc = SA_AIS_ERR_NO_OP;

  /* send the response back to AvA */
  rc = avnd_amf_resp_send(cb, AVSV_AMF_ERR_CLEAR, amf_rc, 0, &api_info->dest,
                          &evt->mds_ctxt, comp, msg_from_avnd);

  /* thru with validation.. process the rest */
  if ((SA_AIS_OK == amf_rc) && (NCSCC_RC_SUCCESS == rc)) {
    /*** clear the err params ***/
    err_info = &comp->err_info;
    err_info->src = static_cast<AVND_ERR_SRC>(0);
    err_info->detect_time = 0;

    /* Inform AMFD to generate ErrorClear() notification */
    clear_error_report_alarm(comp);
  }

done:
  if (NCSCC_RC_SUCCESS != rc) {
    LOG_ER("avnd_evt_ava_err_clear():%s: Hdl=%llx",
           osaf_extended_name_borrow(&err_clear->comp_name), err_clear->hdl);
  }

  TRACE_LEAVE();
  return rc;
}

/**
 * @brief Performs cluster reset recovery action.
 *
 * @param cb: ptr to AvND control block.
 * @param su: ptr to the SU which contains the failed component.
 * @param comp: ptr to failed component.
 *
 * @return NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE.
 */
static uint32_t avnd_err_rcvr_cluster_reset(AVND_CB *cb, AVND_SU *failed_su,
                                            AVND_COMP *failed_comp) {
  TRACE_ENTER();

  m_AVND_COMP_FAILED_SET(failed_comp);
  m_AVND_SU_FAILED_SET(failed_su);

  m_AVND_COMP_OPER_STATE_SET(failed_comp, SA_AMF_OPERATIONAL_DISABLED);
  uint32_t rc = avnd_comp_oper_state_avd_sync(cb, failed_comp);
  if (NCSCC_RC_SUCCESS != rc) goto done;

  rc = avnd_comp_curr_info_del(cb, failed_comp);
  if (NCSCC_RC_SUCCESS != rc) goto done;

  // AMFD will not send any assignments, so clean up PI/NPI comp.
  rc =
      avnd_comp_clc_fsm_run(cb, failed_comp, AVND_COMP_CLC_PRES_FSM_EV_CLEANUP);
  if (NCSCC_RC_SUCCESS != rc) goto done;

  cb->oper_state = SA_AMF_OPERATIONAL_DISABLED;
  m_AVND_SU_OPER_STATE_SET(failed_su, SA_AMF_OPERATIONAL_DISABLED);
  rc = avnd_di_oper_send(cb, failed_su, SA_AMF_CLUSTER_RESET);

done:
  TRACE_LEAVE2("%u", rc);
  return rc;
}

/****************************************************************************
  Name          : avnd_err_process

  Description   : This routine is a top-level routine to process all the
                  component errors. It runs the recovery escalation algorithm
                  & determines the escalated recovery. This escalated recovery
                  is then executed.

  Arguments     : cb       - ptr to the AvND control block
                  comp     - ptr to the component
                  err_info - ptr to error-info

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avnd_err_process(AVND_CB *cb, AVND_COMP *comp,
                          AVND_ERR_INFO *err_info) {
  uint32_t esc_rcvr = err_info->rec_rcvr.raw;
  uint32_t rc = NCSCC_RC_SUCCESS;
  const uint32_t previous_esc_rcvr = esc_rcvr;
  TRACE_ENTER();

  if (!comp) goto done;

  TRACE("Comp:'%s' esc_rcvr:'%u'", comp->name.c_str(), esc_rcvr);

  /* If Amf was waiting for down event to come regarding term cbq, but
     error has occurred, so reset the variable (irrespective of Sa-Aware
     PI or Proxied PI)*/
  if (comp->term_cbq_inv_value != 0) {
    AVND_COMP_CBK *cbk_rec;
    /* get the matching entry from the cbk list */
    m_AVND_COMP_CBQ_INV_GET(comp, comp->term_cbq_inv_value, cbk_rec);
    comp->term_cbq_inv_value = 0;
    if (cbk_rec) avnd_comp_cbq_rec_pop_and_del(cb, comp, cbk_rec, false);
  }

  // Handle errors differently when shutdown has started
  if (AVND_TERM_STATE_OPENSAF_SHUTDOWN_STARTED == cb->term_state) {
    LOG_NO("'%s' faulted due to '%s'", comp->name.c_str(),
           g_comp_err[err_info->src]);
    avnd_comp_cleanup_launch(comp);
    goto done;
  }

  /* when undergoing admin oper do not process any component errors */
  if (comp->admin_oper == true) goto done;

  /* Level3 escalation is node failover. we dont expect any
   ** more errors from application as the node is already failed over.
   ** Except AVND_ERR_SRC_AVA_DN, AvSv will ignore any error which happens
   ** in level3 escalation
   */
  /* (AVND_ERR_SRC_AVA_DN != err_info->src) check is added so that comp state is
   * marked as failed and error cleanup (Clearing pending assignments, deleting
   * dynamic info associated with the component) is done for components which
   * goes down after AVND_ERR_ESC_LEVEL_3 is set
   */
  if ((cb->node_err_esc_level == AVND_ERR_ESC_LEVEL_3) &&
      (comp->su->is_ncs == false) && (esc_rcvr != SA_AMF_NODE_FAILFAST) &&
      (AVND_ERR_SRC_AVA_DN != err_info->src)) {
    /* For external component, comp->su->is_ncs is false, so no need to
       put a check here for external component explicitely. */
    goto done;
  } else if ((cb->node_err_esc_level == AVND_ERR_ESC_LEVEL_3) &&
             (esc_rcvr != SA_AMF_NODE_FAILFAST)) {
    esc_rcvr = SA_AMF_NODE_FAILOVER;
  }

  /* We need not entertain errors when comp is not in shape. */
  if ((m_AVND_COMP_PRES_STATE_IS_UNINSTANTIATED(comp) ||
       m_AVND_COMP_PRES_STATE_IS_INSTANTIATIONFAILED(comp) ||
       m_AVND_COMP_PRES_STATE_IS_TERMINATIONFAILED(comp)))
    goto done;

  if (m_AVND_COMP_PRES_STATE_IS_TERMINATING(comp)) {
    if (err_info->src == AVND_ERR_SRC_HC) {
      LOG_NO("healthcheck failure ignored as '%s' is terminating",
             comp->name.c_str());
      // ignore if healthcheck failed during terminating state
      goto done;
    }
    // special treat failures while terminating, no escalation just cleanup
    LOG_NO("'%s' faulted due to '%s' : Recovery is 'cleanup'",
           comp->name.c_str(), g_comp_err[err_info->src]);
    rc = avnd_comp_clc_fsm_run(cb, comp, AVND_COMP_CLC_PRES_FSM_EV_CLEANUP);
    if (NCSCC_RC_SUCCESS != rc) {
      // this is bad situation, what can we do?
      LOG_ER("%s: '%s' termination failed", __FUNCTION__, comp->name.c_str());
    }
    goto done;
  }

  /* update the err params */
  comp->err_info.src = err_info->src;

  /* if time's not specified, use current time TBD */
  if (err_info->src != AVND_ERR_SRC_REP)
    m_GET_TIME_STAMP(comp->err_info.detect_time);
  else if (comp->error_report_sent == false) {
    /* Inform AMFD to generate ErrorReport() notification */
    avnd_di_uns32_upd_send(AVSV_SA_AMF_COMP, saAmfCompRecoveryOnError_ID,
                           comp->name, err_info->rec_rcvr.raw);
    comp->error_report_sent = true;
  }

  /* determine the escalated recovery */
  rc = avnd_err_escalate(cb, comp->su, comp, &esc_rcvr);
  if (NCSCC_RC_SUCCESS != rc) goto done;

  // add an entry to syslog if recovery method has changed
  log_recovery_escalation(*comp, previous_esc_rcvr, esc_rcvr);

  if (((comp->su->is_ncs == true) && (esc_rcvr != SA_AMF_COMPONENT_RESTART)) ||
      esc_rcvr == SA_AMF_NODE_FAILFAST) {
    LOG_ER("%s Faulted due to:%s Recovery is:%s", comp->name.c_str(),
           g_comp_err[comp->err_info.src], g_comp_rcvr[esc_rcvr - 1]);
    /* do the local node reboot for node_failfast or ncs component failure*/
    if (comp->su->suMaintenanceCampaign.empty()) {
      opensaf_reboot(
          avnd_cb->node_info.nodeId,
          osaf_extended_name_borrow(&avnd_cb->node_info.executionEnvironment),
          "Component faulted: recovery is node failfast");
    } else {
      LOG_NO("not rebooting because maintenance campaign is set: %s",
             comp->su->suMaintenanceCampaign.c_str());
      goto done;
    }
  }

  LOG_NO("'%s' faulted due to '%s' : Recovery is '%s'", comp->name.c_str(),
         g_comp_err[err_info->src], g_comp_rcvr[esc_rcvr - 1]);

  /* execute the recovery */
  rc = avnd_err_recover(cb, comp->su, comp, esc_rcvr);

done:
  TRACE_LEAVE2("Return value:'%u'", rc);
  return rc;
}

/****************************************************************************
  Name          : avnd_err_escalate

  Description   : This routine runs the recovery escalation algorithm &
                  determines the escalated recovery.

  Arguments     : cb          - ptr to the AvND control block
                  su          - ptr to the SU to which the comp belongs
                  comp        - ptr to the component
                  io_esc_rcvr - escalated recovery

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avnd_err_escalate(AVND_CB *cb, AVND_SU *su, AVND_COMP *comp,
                           uint32_t *io_esc_rcvr) {
  uint32_t rc = NCSCC_RC_SUCCESS;
  if (comp->pres == SA_AMF_PRESENCE_UNINSTANTIATED ||
      comp->pres == SA_AMF_PRESENCE_INSTANTIATION_FAILED ||
      comp->pres == SA_AMF_PRESENCE_TERMINATION_FAILED)
    return NCSCC_RC_FAILURE;

  /* use default recovery if nothing is recommended */
  if (*io_esc_rcvr == SA_AMF_NO_RECOMMENDATION)
    *io_esc_rcvr = comp->err_info.def_rec;

  if (*io_esc_rcvr == SA_AMF_COMPONENT_RESTART) {
    if (m_AVND_COMP_IS_RESTART_DIS(comp) && (!su->is_ncs)) {
      LOG_NO("saAmfCompDisableRestart is true for '%s'", comp->name.c_str());
      LOG_NO("recovery action 'comp restart' escalated to 'comp failover'");
      *io_esc_rcvr = SA_AMF_COMPONENT_FAILOVER;
    } else if (comp_has_quiesced_assignment(comp) == true) {
      /* Cannot re-assign QUIESCED, escalate to failover */
      LOG_NO("component with QUIESCED/QUIESCING assignment failed");
      LOG_NO("recovery action 'comp restart' escalated to 'comp failover'");
      *io_esc_rcvr = SA_AMF_COMPONENT_FAILOVER;
    }
  }

  if ((SA_AMF_COMPONENT_FAILOVER == *io_esc_rcvr) && (su->sufailover) &&
      (!su->is_ncs)) {
    LOG_NO("saAmfSUFailover is true for '%s'", comp->su->name.c_str());
    *io_esc_rcvr = AVSV_ERR_RCVR_SU_FAILOVER;
  }

  switch (*io_esc_rcvr) {
    case SA_AMF_COMPONENT_FAILOVER: /* treat it as su failover */
    case AVSV_ERR_RCVR_SU_FAILOVER:
      rc = avnd_err_esc_su_failover(
          cb, su, reinterpret_cast<AVSV_ERR_RCVR *>(io_esc_rcvr));
      break;

    case SA_AMF_COMPONENT_RESTART:
      rc = avnd_err_esc_comp_restart(
          cb, su, reinterpret_cast<AVSV_ERR_RCVR *>(io_esc_rcvr));
      break;

    case SA_AMF_NODE_SWITCHOVER:
    case SA_AMF_NODE_FAILOVER:
    case SA_AMF_CLUSTER_RESET:
      break;

    case SA_AMF_NODE_FAILFAST:
      // this is still supported in headless mode
      break;

    case AVSV_ERR_RCVR_SU_RESTART:
    default:
      osafassert(0);
      break;
  } /* switch */

  return rc;
}

/****************************************************************************
  Name          : avnd_err_recover

  Description   : This routine executes the component failure recovery.

  Arguments     : cb   - ptr to the AvND control block
                  su   - ptr to the SU to which the comp belongs
                  comp - ptr to the component
                  rcvr - recovery to be executed

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avnd_err_recover(AVND_CB *cb, AVND_SU *su, AVND_COMP *comp,
                          uint32_t rcvr) {
  uint32_t rc = NCSCC_RC_SUCCESS;
  TRACE_ENTER2("SU:%s Comp:%s", su->name.c_str(), comp->name.c_str());

  if (comp->pres == SA_AMF_PRESENCE_INSTANTIATING) {
    /* mark the comp failed */
    m_AVND_COMP_FAILED_SET(comp);

    /* update comp oper state */
    m_AVND_COMP_OPER_STATE_SET(comp, SA_AMF_OPERATIONAL_DISABLED);
    rc = avnd_comp_oper_state_avd_sync(cb, comp);
    if (NCSCC_RC_SUCCESS != rc) return rc;

    rc = avnd_comp_clc_fsm_run(cb, comp, AVND_COMP_CLC_PRES_FSM_EV_INST_FAIL);
    return rc;
  }

  /* When SU is in TERMINATING state, higher level recovery
     (SA_AMF_NODE_FAILOVER, SA_AMF_NODE_FAILFAST and SA_AMF_NODE_SWITCHOVER)
     should be processed because higher level recovery will terminate the
     component. If the faulted component has recovery other than these, then
     clean the component and don't process any recovery for that component. */
  if ((su->pres == SA_AMF_PRESENCE_TERMINATING) &&
      (rcvr != SA_AMF_NODE_FAILOVER) && (rcvr != SA_AMF_NODE_FAILFAST) &&
      (rcvr != SA_AMF_NODE_SWITCHOVER) && (rcvr != AVSV_ERR_RCVR_SU_FAILOVER)) {
    /* mark the comp failed */
    m_AVND_COMP_FAILED_SET(comp);

    /* update comp oper state */
    m_AVND_COMP_OPER_STATE_SET(comp, SA_AMF_OPERATIONAL_DISABLED);
    rc = avnd_comp_oper_state_avd_sync(cb, comp);
    if (NCSCC_RC_SUCCESS != rc) return rc;

    /* clean up the comp */
    rc = avnd_comp_clc_fsm_run(cb, comp, AVND_COMP_CLC_PRES_FSM_EV_CLEANUP);

    return rc;
  }

  switch (rcvr) {
    case SA_AMF_NO_RECOMMENDATION:
    /*m_AVSV_ASSERT(0);
       break; */

    case SA_AMF_COMPONENT_RESTART:
      rc = avnd_err_rcvr_comp_restart(cb, comp);
      break;

    case SA_AMF_COMPONENT_FAILOVER:
      rc = avnd_err_rcvr_comp_failover(cb, comp);
      break;

    case SA_AMF_NODE_SWITCHOVER:
      rc = avnd_err_rcvr_node_switchover(cb, su, comp);
      break;

    case SA_AMF_NODE_FAILOVER:
      rc = avnd_err_rcvr_node_failover(cb, su, comp);
      break;

    case SA_AMF_NODE_FAILFAST:
      rc = avnd_err_rcvr_node_failfast(cb, su, comp);
      break;

    case SA_AMF_CLUSTER_RESET:
      rc = avnd_err_rcvr_cluster_reset(cb, su, comp);
      break;

    case AVSV_ERR_RCVR_SU_RESTART:
      rc = avnd_err_rcvr_su_restart(cb, su, comp);
      break;

    case AVSV_ERR_RCVR_SU_FAILOVER:
      rc = avnd_err_rcvr_su_failover(cb, su, comp);
      break;

    default:
      osafassert(0);
      break;
  }
  TRACE_LEAVE();
  return rc;
}

/****************************************************************************
  Name          : avnd_err_rcvr_comp_restart

  Description   : This routine executes component restart recovery.

  Arguments     : cb   - ptr to the AvND control block
                  comp - ptr to the comp

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE.

  Notes         : None.
******************************************************************************/
static uint32_t avnd_err_rcvr_comp_restart(AVND_CB *cb, AVND_COMP *comp) {
  TRACE_ENTER();
  /* mark the comp failed */
  m_AVND_COMP_FAILED_SET(comp);

  uint32_t rc = comp_restart_initiate(comp);

  TRACE_LEAVE2("%u", rc);
  return rc;
}

/****************************************************************************
  Name          : avnd_err_rcvr_su_restart

  Description   : This routine executes SU restart recovery.

  Arguments     : cb          - ptr to the AvND control block
                  su          - ptr to the SU to which the comp belongs
                  failed_comp - ptr to the failed comp that triggered this
                                recovery

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE.

  Notes         : None.
******************************************************************************/
uint32_t avnd_err_rcvr_su_restart(AVND_CB *cb, AVND_SU *su,
                                  AVND_COMP *failed_comp) {
  TRACE_ENTER();

  /* mark the su & comp failed */
  m_AVND_SU_FAILED_SET(su);
  m_AVND_COMP_FAILED_SET(failed_comp);

  /* change the comp & su oper state to disabled */
  m_AVND_SU_OPER_STATE_SET(su, SA_AMF_OPERATIONAL_DISABLED);
  m_AVND_COMP_OPER_STATE_SET(failed_comp, SA_AMF_OPERATIONAL_DISABLED);
  uint32_t rc = avnd_comp_oper_state_avd_sync(cb, failed_comp);
  if (NCSCC_RC_SUCCESS != rc) goto done;

  /* Keep SURestart recovery not to always report OperState to amfd
     as legacy recovery. Only report OperState if SU is under SMF maintenance
     campaign
   */
  if (!su->suMaintenanceCampaign.empty()) {
    avnd_di_uns32_upd_send(AVSV_SA_AMF_SU, saAmfSUOperState_ID, su->name,
        su->oper);
  }

  set_suRestart_flag(su);

  if (su_all_comps_restartable(*su) == true) {
    /* Case 1: All components in SU are restartable
               (saAmfCompDisableRestart is false for all).
       In this case surestart recovery involves two steps:
       a) First, all components in the SU are abruptly terminated in the
          reverse order of their instantiation-levels.
       b) In a second step, all components in the SU are instantiated
          in the order dictated by their instantiation-levels.

       Also if the components have assignments, same assignments will be
       reassigned to respective components after successful restart of SU.
       In this case, only restarting state of the component will be observed.

    */
    // So prepare su-sis for fresh assignment.
    rc = avnd_su_curr_info_del(cb, su);
    if (NCSCC_RC_SUCCESS != rc) goto done;
    rc = avnd_su_si_unmark(cb, su);
    if (NCSCC_RC_SUCCESS != rc) goto done;

    if (!su->suMaintenanceCampaign.empty()) {
      LOG_NO("not restarting su because maintenance campaign is set: %s",
             su->suMaintenanceCampaign.c_str());
      goto done;
    }

    rc = avnd_su_pres_fsm_run(cb, su, 0, AVND_SU_PRES_FSM_EV_RESTART);
    if (NCSCC_RC_SUCCESS != rc) goto done;
  } else {
    /* Case 2: Atleast one component in SU is non restartable
               (saAmfCompDisableRestart is true for atleast one comp in SU).
    In this case, all components in the SU are abruptly terminated in the
    reverse order of their instantiation-levels. Since aleast one of the
    components is non restartable, first reassign the CSIs currently assigned
    to non restartable component to another component before
    terminating/instantiating the components. Since a component may be serving
    any redundancy model, the process of reassignment depends upon the
    redundancy model characteristics.

    At the same time, reassignment of CSIs from this non-restartable component
    may lead to reassignment of CSIs from other components (non-restartable and
    restartable) in this SU as the same SI may have assignments in those
    components. In current OpenSAF implementation, such a case of comp-failover
    is implemented as switchover of all the assignments from the whole SU
    irrespective of redundancy model. As of now, this case will work like
    OpenSAF comp-failover only.

    TODO:In future when AMF supports comp-failover in spec compliance then this
            case should be alligned with that.
    */
    if (!su->suMaintenanceCampaign.empty()) {
      LOG_NO("not restarting su because maintenance campaign is set: %s",
             su->suMaintenanceCampaign.c_str());
      goto done;
    }

    if (m_AVND_SU_IS_PREINSTANTIABLE(su)) {
      if (m_AVND_COMP_TYPE_IS_PREINSTANTIABLE(failed_comp))
        rc = avnd_comp_clc_fsm_run(cb, failed_comp,
                                   AVND_COMP_CLC_PRES_FSM_EV_CLEANUP);
      else
        rc = avnd_su_pres_fsm_run(cb, su, 0, AVND_SU_PRES_FSM_EV_RESTART);
      if (NCSCC_RC_SUCCESS != rc) goto done;
    } else {
      /*In NPI SU only one SI is assigned to the whole SU. Since SU has atleast
        one component non-restartable then request AMFD to start switchover of
        this SU. As a part of quieced assignment, clean up will be done.
       */
      su_send_suRestart_recovery_msg(su);
    }
  }
done:
  TRACE_LEAVE2("%u", rc);
  return rc;
}

/**
 * This function performs component failover recovery action.
 *
 * @param cb: ptr to AvND contol block.
 * @param comp: ptr to failed component.
 *
 * @return NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE.
 */
uint32_t avnd_err_rcvr_comp_failover(AVND_CB *cb, AVND_COMP *failed_comp) {
  TRACE_ENTER2("'%s'", failed_comp->name.c_str());
  AVND_SU *su = failed_comp->su;
  /* mark the comp failed */
  m_AVND_COMP_FAILED_SET(failed_comp);

  /* update comp oper state */
  m_AVND_COMP_OPER_STATE_SET(failed_comp, SA_AMF_OPERATIONAL_DISABLED);
  uint32_t rc = avnd_comp_oper_state_avd_sync(cb, failed_comp);
  if (NCSCC_RC_SUCCESS != rc) goto done;

  /* mark the su failed */
  if (!m_AVND_SU_IS_FAILED(su)) {
    m_AVND_SU_FAILED_SET(su);
  }
  // Remember component-failover/su-failover context.
  m_AVND_SU_FAILOVER_SET(failed_comp->su);

  /* update su oper state */
  m_AVND_SU_OPER_STATE_SET(su, SA_AMF_OPERATIONAL_DISABLED);

  /* We are now in the context of failover, forget the reset restart admin op
   * id*/
  if (m_AVND_SU_IS_RESTART(su))
    su->admin_op_Id = static_cast<SaAmfAdminOperationIdT>(0);

  // TODO: there should be no difference between PI/NPI comps
  if (m_AVND_SU_IS_PREINSTANTIABLE(su)) {
    /* clean the failed comp */
    rc = avnd_comp_clc_fsm_run(cb, failed_comp,
                               AVND_COMP_CLC_PRES_FSM_EV_CLEANUP);
    if (NCSCC_RC_SUCCESS != rc) {
      LOG_ER("cleanup of '%s' failed", failed_comp->name.c_str());
      goto done;
    }

  } else {
    /* request director to orchestrate component failover */
    rc = avnd_di_oper_send(cb, failed_comp->su, AVSV_ERR_RCVR_SU_FAILOVER);

    // for now, just terminate all components in the SU
    if (cb->is_avd_down == true) {
      AVND_COMP *comp;
      LOG_NO("Terminating components of '%s'(abruptly & unordered)",
             su->name.c_str());
      for (comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
               m_NCS_DBLIST_FIND_FIRST(&su->comp_list));
           comp; comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
                     m_NCS_DBLIST_FIND_NEXT(&comp->su_dll_node))) {
        if (comp->su->su_is_external) continue;
        rc = avnd_comp_clc_fsm_run(cb, comp, AVND_COMP_CLC_PRES_FSM_EV_CLEANUP);
        if (NCSCC_RC_SUCCESS != rc) {
          LOG_ER("'%s' termination failed", comp->name.c_str());
          goto done;
        }
        avnd_su_pres_state_set(cb, comp->su, SA_AMF_PRESENCE_TERMINATING);
      }
    }
  }

done:
  TRACE_LEAVE2("retval=%u", rc);
  return rc;
}

/**
 * This function performs SU failover recovery action.
 *
 * @param cb: ptr to AvND contol block.
 * @param su: ptr to the SU which contains the failed component.
 * @param comp: ptr to failed component.
 *
 * @return NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE.
 */
uint32_t avnd_err_rcvr_su_failover(AVND_CB *cb, AVND_SU *su,
                                   AVND_COMP *failed_comp) {
  AVND_COMP *comp;
  uint32_t rc = NCSCC_RC_SUCCESS;

  TRACE_ENTER2("'%s' '%s'", su->name.c_str(), failed_comp->name.c_str());
  m_AVND_COMP_FAILED_SET(failed_comp);
  m_AVND_COMP_OPER_STATE_SET(failed_comp, SA_AMF_OPERATIONAL_DISABLED);
  m_AVND_SU_FAILED_SET(su);
  m_AVND_SU_OPER_STATE_SET(su, SA_AMF_OPERATIONAL_DISABLED);

  /* We are now in the context of failover, forget the restart */
  if (m_AVND_SU_IS_RESTART(su)) {
    reset_suRestart_flag(su);
    su->admin_op_Id = static_cast<SaAmfAdminOperationIdT>(0);
  }
  /* If SU faulted during assignments, reset its pending assignment flag. AMFD
     will perform fail-over as a part of recovery request.*/
  if (m_AVND_SU_IS_ALL_SI(su)) {
    TRACE_1("Reset pending assignment flag in su.");
    m_AVND_SU_ALL_SI_RESET(su);
  }

  // Remember component-failover/su-failover context.
  m_AVND_SU_FAILOVER_SET(failed_comp->su);
  LOG_NO("Terminating components of '%s'(abruptly & unordered)",
         su->name.c_str());
  /* Unordered cleanup of components of failed SU */
  for (comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
           m_NCS_DBLIST_FIND_FIRST(&su->comp_list));
       comp; comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
                 m_NCS_DBLIST_FIND_NEXT(&comp->su_dll_node))) {
    if (comp->su->su_is_external) continue;

    rc = avnd_comp_clc_fsm_run(cb, comp, AVND_COMP_CLC_PRES_FSM_EV_CLEANUP);
    if (NCSCC_RC_SUCCESS != rc) {
      LOG_ER("'%s' termination failed", comp->name.c_str());
      goto done;
    }
    avnd_su_pres_state_set(cb, comp->su, SA_AMF_PRESENCE_TERMINATING);
  }
done:

  TRACE_LEAVE2("%u", rc);
  return rc;
}

/****************************************************************************
  Name          : avnd_err_rcvr_node_switchover

  Description   : This routine executes Node switchover recovery.

  Arguments     : cb          - ptr to the AvND control block
                  failed_su   - ptr to the failed su
                  failed_comp - ptr to the failed comp

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE.

  Notes         : None.

******************************************************************************/
uint32_t avnd_err_rcvr_node_switchover(AVND_CB *cb, AVND_SU *failed_su,
                                       AVND_COMP *failed_comp) {
  TRACE_ENTER();
  AVND_COMP *comp;
  /* increase log level to info */
  setlogmask(LOG_UPTO(LOG_INFO));

  /* mark the comp failed */
  m_AVND_COMP_FAILED_SET(failed_comp);

  /* update comp oper state */
  m_AVND_COMP_OPER_STATE_SET(failed_comp, SA_AMF_OPERATIONAL_DISABLED);
  uint32_t rc = avnd_comp_oper_state_avd_sync(cb, failed_comp);
  if (NCSCC_RC_SUCCESS != rc) goto done;

  /* mark the su failed */
  if (!m_AVND_SU_IS_FAILED(failed_su)) {
    m_AVND_SU_FAILED_SET(failed_su);
  }

  cb->term_state = AVND_TERM_STATE_NODE_SWITCHOVER_STARTED;
  cb->failed_su = failed_su;

  /* transition the su oper state to disabled */
  m_AVND_SU_OPER_STATE_SET(failed_su, SA_AMF_OPERATIONAL_DISABLED);

  /* If the oper_state is already set to SA_AMF_OPERATIONAL_DISABLED, that means
     Node failover is already informed to AVD, so no need to resend again */
  if (SA_AMF_OPERATIONAL_DISABLED != cb->oper_state) {
    /* transition the node oper state to disabled */
    cb->oper_state = SA_AMF_OPERATIONAL_DISABLED;
  }

  /* We are now in the context of failover, forget the reset restart admin op
   * id*/
  if (m_AVND_SU_IS_RESTART(failed_su))
    failed_su->admin_op_Id = static_cast<SaAmfAdminOperationIdT>(0);

  /* If SU faulted during assignments, reset its pending assignment flag. AMFD
     will perform fail-over as a part of recovery request.*/
  if ((m_AVND_SU_IS_ALL_SI(failed_su)) &&
      (failed_comp->su->sufailover == true)) {
    TRACE_1("Reset pending assignment flag in su.");
    m_AVND_SU_ALL_SI_RESET(failed_su);
  }
  /* In nodeswitchover context:
     a)If saAmfSUFailover is set for the faulted SU then this SU will be
     failed-over as a single entity. b)If saAmfSUFailover is not set for the
     faulted SU then assignments of healthy components of the SU will be
     siwtched-over and assignments of only failed component will be failed-over.
     For other non faulted SUs hosted on this node, assignments will be
     switched-over. (SAI-AIS-AMF-B.04.01 Section 3.11.1.3.2 of
     SAI-AIS-AMF-B.04.01 spec.)

     So if saAmfSUFailover is set for the faulted SU then all components of this
     SU will be abruptly terminated. And if saAmfSUFailover is not set then
     perform clean up of only failed component.
   */
  if (m_AVND_SU_IS_FAILED(failed_comp->su) && (failed_comp->su->sufailover)) {
    reset_suRestart_flag(failed_su);
    failed_su->admin_op_Id = static_cast<SaAmfAdminOperationIdT>(0);
    // Remember su-failover context.
    m_AVND_SU_FAILOVER_SET(failed_comp->su);

    LOG_NO("Terminating components of '%s'(abruptly & unordered)",
           failed_su->name.c_str());
    /* Unordered cleanup of components of failed SU */
    for (comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
             m_NCS_DBLIST_FIND_FIRST(&failed_su->comp_list));
         comp; comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
                   m_NCS_DBLIST_FIND_NEXT(&comp->su_dll_node))) {
      if (comp->su->su_is_external) continue;

      rc = avnd_comp_clc_fsm_run(cb, comp, AVND_COMP_CLC_PRES_FSM_EV_CLEANUP);
      if (NCSCC_RC_SUCCESS != rc) {
        LOG_ER("'%s' termination failed", comp->name.c_str());
        goto done;
      }
      avnd_su_pres_state_set(cb, failed_comp->su, SA_AMF_PRESENCE_TERMINATING);
    }
  } else {
    /* terminate the failed comp */
    if (m_AVND_SU_IS_PREINSTANTIABLE(failed_su)) {
      rc = avnd_comp_clc_fsm_run(cb, failed_comp,
                                 AVND_COMP_CLC_PRES_FSM_EV_CLEANUP);
      if (NCSCC_RC_SUCCESS != rc) goto done;
    }
  }

done:

  TRACE_LEAVE2("%u", rc);
  return rc;
}

/****************************************************************************
  Name          : avnd_err_rcvr_node_failover

  Description   : This routine executes Node failover recovery.

  Arguments     : cb          - ptr to the AvND control block
                  failed_su   - ptr to the failed su
                  failed_comp - ptr to the failed comp

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE.

  Notes         : None.

******************************************************************************/
uint32_t avnd_err_rcvr_node_failover(AVND_CB *cb, AVND_SU *failed_su,
                                     AVND_COMP *failed_comp) {
  AVND_COMP *comp;
  uint32_t rc = NCSCC_RC_SUCCESS;

  TRACE_ENTER2("'%s' '%s'", failed_su->name.c_str(), failed_comp->name.c_str());

  LOG_NO("Terminating all application components (abruptly & unordered)");

  /* increase log level to info */
  setlogmask(LOG_UPTO(LOG_INFO));

  cb->term_state = AVND_TERM_STATE_NODE_FAILOVER_TERMINATING;
  cb->oper_state = SA_AMF_OPERATIONAL_DISABLED;
  cb->failed_su = failed_su;

  m_AVND_COMP_FAILED_SET(failed_comp);
  m_AVND_COMP_OPER_STATE_SET(failed_comp, SA_AMF_OPERATIONAL_DISABLED);
  m_AVND_SU_FAILED_SET(failed_su);
  m_AVND_SU_OPER_STATE_SET(failed_su, SA_AMF_OPERATIONAL_DISABLED);

  /* We are now in the context of failover, forget the restart */
  if (m_AVND_SU_IS_RESTART(failed_su)) {
    reset_suRestart_flag(failed_su);
    failed_su->admin_op_Id = static_cast<SaAmfAdminOperationIdT>(0);
  }
  /* Unordered cleanup of all local application components */
  for (comp = avnd_compdb_rec_get_next(cb->compdb, ""); comp != nullptr;
       comp = avnd_compdb_rec_get_next(cb->compdb, comp->name)) {
    if (comp->su->is_ncs || comp->su->su_is_external) continue;

    rc = avnd_comp_clc_fsm_run(cb, comp, AVND_COMP_CLC_PRES_FSM_EV_CLEANUP);
    if (rc != NCSCC_RC_SUCCESS) {
      LOG_ER("'%s' termination failed", comp->name.c_str());
      if (comp->su->suMaintenanceCampaign.empty()) {
        opensaf_reboot(
            avnd_cb->node_info.nodeId,
            osaf_extended_name_borrow(&avnd_cb->node_info.executionEnvironment),
            "Component termination failed at node failover");
        LOG_ER("Exiting (due to comp term failed) to aid fast node reboot");
        exit(1);
      } else {
        LOG_NO("not rebooting because maintenance campaign is set: %s",
               comp->su->suMaintenanceCampaign.c_str());
        continue;
      }
    }
    avnd_su_pres_state_set(cb, comp->su, SA_AMF_PRESENCE_TERMINATING);
  }

  TRACE_LEAVE2("%u", rc);
  return rc;
}

/****************************************************************************
  Name          : avnd_err_su_repair

  Description   : This routine executes the failed SU repair. It picks up the
                  failed components & instantiates them.

  Arguments     : cb   - ptr to the AvND control block
                  su   - ptr to the SU

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avnd_err_su_repair(AVND_CB *cb, AVND_SU *su) {
  AVND_COMP *comp = 0;
  bool is_en, is_uninst = false, is_comp_insting = false;
  uint32_t rc = NCSCC_RC_SUCCESS;
  TRACE_ENTER();

  osafassert(m_AVND_SU_IS_FAILED(su));

  if (m_AVND_IS_SHUTTING_DOWN(cb)) goto done;

  /* If the SU is in inst-failed state, do nothing */
  if (su->pres == SA_AMF_PRESENCE_INSTANTIATION_FAILED) return rc;

  if (all_comps_terminated_in_su(su) == true) is_uninst = true;

  /* scan & instantiate failed pi comps */
  for (comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
           m_NCS_DBLIST_FIND_FIRST(&su->comp_list));
       comp; comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
                 m_NCS_DBLIST_FIND_NEXT(&comp->su_dll_node))) {
    if (m_AVND_COMP_IS_FAILED(comp)) {
      if (m_AVND_COMP_TYPE_IS_PREINSTANTIABLE(comp)) {
        /* trigger comp-fsm */
        rc = avnd_comp_clc_fsm_run(cb, comp, AVND_COMP_CLC_PRES_FSM_EV_INST);
        if (NCSCC_RC_SUCCESS != rc) goto done;
      }

      if (!m_AVND_SU_IS_PREINSTANTIABLE(su)) {
        m_AVND_COMP_FAILED_RESET(comp);
      }
    }
    if (comp->pres == SA_AMF_PRESENCE_INSTANTIATING) is_comp_insting = true;

  } /* for */

  if (!m_AVND_SU_IS_PREINSTANTIABLE(su)) {
    m_AVND_SU_FAILED_RESET(su);
  }

  /* if a mix pi su has all the comps enabled, inform AvD */
  if (m_AVND_SU_IS_PREINSTANTIABLE(su)) {
    m_AVND_SU_IS_ENABLED(su, is_en);
    if (true == is_en) {
      m_AVND_SU_OPER_STATE_SET(su, SA_AMF_OPERATIONAL_ENABLED);
      rc = avnd_di_oper_send(cb, su, 0);
      if (NCSCC_RC_SUCCESS != rc) goto done;
    }
  }

  /* This is the case when SU is in uninstantiated state and one of the
     components is moved to instantiating state. Mark SU
     SA_AMF_PRESENCE_INSTANTIATING as there is no such event handler and event
     in SU FSM.
   */
  if ((is_uninst == true) && (is_comp_insting == true))
    avnd_su_pres_state_set(cb, su, SA_AMF_PRESENCE_INSTANTIATING);
done:
  TRACE_LEAVE2("retval=%u", rc);
  return rc;
}

/****************************************************************************
  Name          : avnd_err_esc_comp_restart

  Description   : This routine executes the escalation for component restart.

  Arguments     : cb       - ptr to the AvND control block
                  su       - ptr to the SU
                  err_rcvr - ptr to AVSV_ERR_RCVR

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avnd_err_esc_comp_restart(AVND_CB *cb, AVND_SU *su,
                                   AVSV_ERR_RCVR *esc_rcvr) {
  AVND_ERR_ESC_LEVEL *esc_level = &su->su_err_esc_level;
  uint32_t rc = NCSCC_RC_SUCCESS;
  TRACE_ENTER();

  /* taking care of a case where comp_restart_max is zero */
  if (*esc_level == AVND_ERR_ESC_LEVEL_0 && su->comp_restart_cnt == 0)
    m_AVND_ERR_ESC_LVL_GET(su, *esc_level);

  /* Due to fall thru logic, esc level might have reached level3 */
  if (*esc_level == AVND_ERR_ESC_LEVEL_3) cb->node_err_esc_level = *esc_level;

  switch (*esc_level) {
    case AVND_ERR_ESC_LEVEL_0:
      rc = avnd_err_restart_esc_level_0(cb, su, esc_level, esc_rcvr);
      break;

    case AVND_ERR_ESC_LEVEL_1:
      rc = avnd_err_restart_esc_level_1(cb, su, esc_level, esc_rcvr);
      break;

    case AVND_ERR_ESC_LEVEL_2:
      rc = avnd_err_restart_esc_level_2(cb, su, esc_level, esc_rcvr);
      break;

    case AVND_ERR_ESC_LEVEL_3:
      *esc_rcvr = static_cast<AVSV_ERR_RCVR>(SA_AMF_NODE_FAILOVER);
      break;

    default:
      osafassert(0);
  }

  TRACE_LEAVE2("retval=%u", rc);
  return rc;
}

/****************************************************************************
  Name          : avnd_err_restart_esc_level_0

  Description   : This routine executes the escalation for level0.

  Arguments     : cb        - ptr to the AvND control block
                  su        - ptr to the SU
                  esc_level - ptr to AVND_ERR_ESC_LEVEL
                  err_rcvr  - ptr to AVSV_ERR_RCVR

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avnd_err_restart_esc_level_0(AVND_CB *cb, AVND_SU *su,
                                      AVND_ERR_ESC_LEVEL *esc_level,
                                      AVSV_ERR_RCVR *esc_rcvr) {
  uint32_t rc = NCSCC_RC_SUCCESS;
  *esc_rcvr = static_cast<AVSV_ERR_RCVR>(SA_AMF_COMPONENT_RESTART);
  TRACE_ENTER();

  /* first time in this level */
  if (su->comp_restart_cnt == 0) {
    /*start timer */
    rc = tmr_comp_err_esc_start(cb, su);
    if (NCSCC_RC_SUCCESS != rc) goto done;

    su_increment_comp_restart_count(*su);
    goto done;
  }

  if (su->comp_restart_cnt < su->comp_restart_max) {
    su_increment_comp_restart_count(*su);
    goto done;
  }

  /* ok! go to next level */
  if (su->comp_restart_cnt >= su->comp_restart_max) {
    LOG_NO("'%s' component restarts have reached configured limit of %u",
           su->name.c_str(), su->comp_restart_max);

    /*stop the comp-err-esc-timer */
    tmr_comp_err_esc_stop(cb, su);
    su->comp_restart_cnt = 0;
    su_reset_restart_count_in_comps(cb, su);

    /* go to the next possible level, is su restart capable? */
    if (su->su_restart_max != 0 && !m_AVND_SU_IS_SU_RESTART_DIS(su)) {
      *esc_level = AVND_ERR_ESC_LEVEL_1;
      rc = avnd_err_restart_esc_level_1(cb, su, esc_level, esc_rcvr);
      goto done;
    }

    if ((cb->su_failover_max != 0) || (true == su->su_is_external)) {
      /* External component should not contribute to NODE FAILOVER of cluster
         component. Maximum it can go to SU FAILOVER. */
      *esc_level = AVND_ERR_ESC_LEVEL_2;
      rc = avnd_err_restart_esc_level_2(cb, su, esc_level, esc_rcvr);
      goto done;
    }

    /* level 3 esc level */
    cb->node_err_esc_level = AVND_ERR_ESC_LEVEL_3;
    *esc_rcvr = static_cast<AVSV_ERR_RCVR>(SA_AMF_NODE_FAILOVER);
  }

done:
  TRACE_LEAVE2("retval=%u", rc);
  return rc;
}

/****************************************************************************
  Name          : avnd_err_restart_esc_level_1

  Description   : This routine executes the escalation for level1.

  Arguments     : cb        - ptr to the AvND control block
                  su        - ptr to the SU
                  esc_level - ptr to AVND_ERR_ESC_LEVEL
                  err_rcvr  - ptr to AVSV_ERR_RCVR

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avnd_err_restart_esc_level_1(AVND_CB *cb, AVND_SU *su,
                                      AVND_ERR_ESC_LEVEL *esc_level,
                                      AVSV_ERR_RCVR *esc_rcvr) {
  uint32_t rc = NCSCC_RC_SUCCESS;
  TRACE_ENTER();

  /* If the SU is still instantiating, do jump to next level */
  if (su->pres == SA_AMF_PRESENCE_INSTANTIATING ||
      su->pres == SA_AMF_PRESENCE_RESTARTING || m_AVND_SU_IS_ASSIGN_PEND(su)) {
    TRACE("Further escalating surestart recovery");
    /* go to the next possible level, get escalated recovery and modify count */
    if ((cb->su_failover_max != 0) || (true == su->su_is_external)) {
      /* External component should not contribute to NODE FAILOVER of cluster
         component. Maximum it can go to SU FAILOVER. */
      *esc_level = AVND_ERR_ESC_LEVEL_2;
      rc = avnd_err_restart_esc_level_2(cb, su, esc_level, esc_rcvr);
      goto done;
    }

    /* level 3 esc level */
    cb->node_err_esc_level = AVND_ERR_ESC_LEVEL_3;
    *esc_rcvr = static_cast<AVSV_ERR_RCVR>(SA_AMF_NODE_FAILOVER);
    goto done;
  }

  /* first time in this level */
  *esc_rcvr = AVSV_ERR_RCVR_SU_RESTART;

  if (su->su_restart_cnt < su->su_restart_max) {
    if (su->su_restart_cnt == 0) {
      rc = tmr_su_err_esc_start(cb, su);
      if (NCSCC_RC_SUCCESS != rc) goto done;
    }
    su_increment_su_restart_count(*su);
    goto done;
  }

  /* reached max count */
  if (su->su_restart_cnt >= su->su_restart_max) {
    LOG_NO("'%s' restarts have reached configured limit of %u",
           su->name.c_str(), su->su_restart_max);

    /* stop timer */
    tmr_su_err_esc_stop(cb, su);
    su->su_restart_cnt = 0;
    su_reset_restart_count_in_comps(cb, su);

    /* go to the next possible level, get escalated recovery and modify count */
    if ((cb->su_failover_max != 0) || (true == su->su_is_external)) {
      /* External component should not contribute to NODE FAILOVER of cluster
         component. Maximum it can go to SU FAILOVER. */
      *esc_level = AVND_ERR_ESC_LEVEL_2;
      rc = avnd_err_restart_esc_level_2(cb, su, esc_level, esc_rcvr);
      goto done;
    }

    /* level 3 esc level */
    cb->node_err_esc_level = AVND_ERR_ESC_LEVEL_3;
    *esc_rcvr = static_cast<AVSV_ERR_RCVR>(SA_AMF_NODE_FAILOVER);
  }

done:
  if (cb->is_avd_down == false) {
    avnd_di_uns32_upd_send(AVSV_SA_AMF_SU, saAmfSURestartCount_ID, su->name,
                           su->su_restart_cnt);
  }

  TRACE_LEAVE2("retval=%u", rc);
  return rc;
}

/****************************************************************************
  Name          : avnd_err_restart_esc_level_2

  Description   : This routine executes the escalation for level2.

  Arguments     : cb        - ptr to the AvND control block
                  su        - ptr to the SU
                  esc_level - ptr to AVND_ERR_ESC_LEVEL
                  err_rcvr  - ptr to AVSV_ERR_RCVR

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avnd_err_restart_esc_level_2(AVND_CB *cb, AVND_SU *su,
                                      AVND_ERR_ESC_LEVEL *esc_level,
                                      AVSV_ERR_RCVR *esc_rcvr) {
  uint32_t rc = NCSCC_RC_SUCCESS;
  TRACE_ENTER();

  /* first time in this level */
  if (su->sufailover)
    *esc_rcvr = AVSV_ERR_RCVR_SU_FAILOVER;
  else
    *esc_rcvr = static_cast<AVSV_ERR_RCVR>(SA_AMF_COMPONENT_FAILOVER);

  /* External components are not supposed to escalate SU Failover of
     cluster components. For Ext component, SU Failover will be limited to
     them only. */
  if (true == su->su_is_external) {
    /* Once it had been escalted to AVND_ERR_ESC_LEVEL_2, then reset to
       AVND_ERR_ESC_LEVEL_0  */
    if (AVND_ERR_ESC_LEVEL_2 == su->su_err_esc_level)
      su->su_err_esc_level = AVND_ERR_ESC_LEVEL_0;
    goto done;
  }

  if (cb->su_failover_cnt == 0) {
    rc = tmr_node_err_esc_start(cb);
    if (NCSCC_RC_SUCCESS != rc) goto done;

    cb_increment_su_failover_count(*cb, *su);
    goto done;
  }

  if (cb->su_failover_cnt < cb->su_failover_max) {
    cb_increment_su_failover_count(*cb, *su);
    goto done;
  }

  /* reached max count */
  if (cb->su_failover_cnt >= cb->su_failover_max) {
    LOG_NO("SU failovers have reached configured limit of %u",
           cb->su_failover_max);

    /* stop timer */
    tmr_node_err_esc_stop(cb);
    cb->su_failover_cnt = 0;
    cb->node_err_esc_level = AVND_ERR_ESC_LEVEL_3;
    *esc_rcvr = static_cast<AVSV_ERR_RCVR>(SA_AMF_NODE_FAILOVER);
  }

done:
  TRACE_LEAVE2("retval=%u", rc);
  return rc;
}

/****************************************************************************
  Name          : avnd_err_esc_comp_failover

  Description   : This routine executes the escalation for component failover.

  Arguments     : cb       - ptr to the AvND control block
                  su       - ptr to the SU
                  esc_rcvr - ptr to AVSV_ERR_RCVR

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
AVSV_ERR_RCVR avnd_err_esc_su_failover(AVND_CB *cb, AVND_SU *su,
                                       AVSV_ERR_RCVR *esc_rcvr) {
  AVND_ERR_ESC_LEVEL *esc_level = &cb->node_err_esc_level;
  uint32_t rc = NCSCC_RC_SUCCESS;
  TRACE_ENTER();

  /* initialize */
  if (su->sufailover)
    *esc_rcvr = AVSV_ERR_RCVR_SU_FAILOVER;
  else
    *esc_rcvr = static_cast<AVSV_ERR_RCVR>(SA_AMF_COMPONENT_FAILOVER);

  if (true == su->su_is_external) {
    /* External component should not contribute to NODE FAILOVER of cluster
       component. Maximum it can go to SU FAILOVER. */
    return static_cast<AVSV_ERR_RCVR>(rc);
  }

  if (cb->su_failover_max == 0) { /* fall thru */
    *esc_rcvr = static_cast<AVSV_ERR_RCVR>(SA_AMF_NODE_FAILOVER);
    *esc_level = AVND_ERR_ESC_LEVEL_3;
    goto done;
  }

  if (cb->su_failover_cnt == 0) {
    /* This could be because of component reporting error
       SA_AMF_COMPONENT_FAILOVER OR because of component failing, whose SU has
       SUFailover attr set to true. */
    su->su_err_esc_level = AVND_ERR_ESC_LEVEL_2;
    /* start timer */
    rc = tmr_node_err_esc_start(cb);
    if (NCSCC_RC_SUCCESS != rc) goto done;

    cb_increment_su_failover_count(*cb, *su);
    goto done;
  }

  if (cb->su_failover_cnt < cb->su_failover_max) {
    /* This means NODE_ERR_ESC may be already running because of other SUs
     * escalations.*/
    su->su_err_esc_level = AVND_ERR_ESC_LEVEL_2;
    cb_increment_su_failover_count(*cb, *su);
    goto done;
  }

  if (cb->su_failover_cnt >= cb->su_failover_max) {
    LOG_NO("SU failovers have reached configured limit of %u",
           cb->su_failover_max);

    /* stop timer */
    tmr_node_err_esc_stop(cb);
    cb->su_failover_cnt = 0;
    *esc_level = AVND_ERR_ESC_LEVEL_3;
    *esc_rcvr = static_cast<AVSV_ERR_RCVR>(SA_AMF_NODE_FAILOVER);
  }

done:
  TRACE_LEAVE2("retval=%u", rc);
  return static_cast<AVSV_ERR_RCVR>(rc);
}

/****************************************************************************
  Name          : avnd_evt_tmr_node_err_esc

  Description   : This routine handles the the expiry of the 'node error
                  escalation' timer. It indicates the end of the su failover
                  probation period for the node.

  Arguments     : cb  - ptr to the AvND control block
                  evt - ptr to the AvND event

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avnd_evt_tmr_node_err_esc_evh(AVND_CB *cb, AVND_EVT *evt) {
  uint32_t rc = NCSCC_RC_SUCCESS;
  AVND_SU *su;

  TRACE_ENTER();

  LOG_NO("SU failover probation timer expired");

  for (su = avnd_sudb_rec_get_next(cb->sudb, ""); su != nullptr;
       su = avnd_sudb_rec_get_next(cb->sudb, su->name)) {
    /* Only reset to those Sus, which have affected the node err esc.*/
    if (su->su_err_esc_level == AVND_ERR_ESC_LEVEL_2) {
      su->comp_restart_cnt = 0;
      su->su_restart_cnt = 0;
      su_reset_restart_count_in_comps(cb, su);
      avnd_di_uns32_upd_send(AVSV_SA_AMF_SU, saAmfSURestartCount_ID, su->name,
                             su->su_restart_cnt);
      su->su_err_esc_level = AVND_ERR_ESC_LEVEL_0;
    }
  }

  /* reset all parameters */
  cb->su_failover_cnt = 0;
  cb->node_err_esc_level = AVND_ERR_ESC_LEVEL_0;

  TRACE_LEAVE2("returning success");
  return rc;
}

/****************************************************************************
  Name          : avnd_err_rcvr_node_failfast

  Description   : This routine executes Node failfast recovery.

  Arguments     : cb          - ptr to the AvND control block
                  failed_su   - ptr to the failed su
                  failed_comp - ptr to the failed comp

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE.

  Notes         : None.

******************************************************************************/
uint32_t avnd_err_rcvr_node_failfast(AVND_CB *cb, AVND_SU *failed_su,
                                     AVND_COMP *failed_comp) {
  TRACE_ENTER();

  /* mark the comp & su failed */
  m_AVND_COMP_FAILED_SET(failed_comp);
  m_AVND_SU_FAILED_SET(failed_su);

  /* update comp oper state */
  m_AVND_COMP_OPER_STATE_SET(failed_comp, SA_AMF_OPERATIONAL_DISABLED);
  uint32_t rc = avnd_comp_oper_state_avd_sync(cb, failed_comp);
  if (NCSCC_RC_SUCCESS != rc) goto done;

  /* delete curr info of the failed comp */
  rc = avnd_comp_curr_info_del(cb, failed_comp);
  if (NCSCC_RC_SUCCESS != rc) goto done;

  /* terminate the failed comp */
  if (m_AVND_SU_IS_PREINSTANTIABLE(failed_su)) {
    rc = avnd_comp_clc_fsm_run(cb, failed_comp,
                               AVND_COMP_CLC_PRES_FSM_EV_CLEANUP);
    if (NCSCC_RC_SUCCESS != rc) goto done;
  }

  /* transition the su & node oper state to disabled */
  cb->oper_state = SA_AMF_OPERATIONAL_DISABLED;
  m_AVND_SU_OPER_STATE_SET(failed_su, SA_AMF_OPERATIONAL_DISABLED);

  /* inform avd */
  rc = avnd_di_oper_send(cb, failed_su, SA_AMF_NODE_FAILFAST);

done:
  TRACE_LEAVE2("%u", rc);
  return rc;
}

/**
 * @brief  In escalation of level 2 (sufailover), level 3(nodefailover) and
 *         nodeswitchover when failed su is sufailover capable, cleanup of
 *         all components of su or node will be done.
 *         A a exception, assignments are responded when a SU enters in
 *         inst_failed state.
 *         This function return true if such a escalation exists or failed su
 *         is in inst_failed state.
 * @param  ptr to su.
 * @return true/false.
 */
bool is_no_assignment_due_to_escalations(AVND_SU *su) {
  TRACE_ENTER();
  if ((sufailover_in_progress(su) == true) ||
      (sufailover_during_nodeswitchover(su) == true) ||
      (avnd_cb->term_state == AVND_TERM_STATE_NODE_FAILOVER_TERMINATING) ||
      (avnd_cb->term_state == AVND_TERM_STATE_NODE_FAILOVER_TERMINATED)) {
    TRACE_LEAVE2("true");
    return true;
  }
  if (su->pres == SA_AMF_PRESENCE_INSTANTIATION_FAILED) {
    TRACE_LEAVE2("false");
    return false;
  }
  TRACE_LEAVE2("false");
  return false;
}
