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

  DESCRIPTION:
   This files contains routines & macros for component processing (registration,
   unregistration etc.).

..............................................................................

  FUNCTIONS:


******************************************************************************
*/
#include <cinttypes>

#include "amf/amfnd/avnd.h"
#include "osaf/immutil/immutil.h"

/*** Static function declarations ***/

static void avnd_comp_reg_val(AVND_CB *, AVSV_AMF_COMP_REG_PARAM *,
                              AVND_COMP **, AVND_COMP **, SaAisErrorT *, bool *,
                              bool);

static void avnd_comp_unreg_val(AVND_CB *, AVSV_AMF_COMP_UNREG_PARAM *,
                                AVND_COMP **, AVND_COMP **, SaAisErrorT *);

static uint32_t avnd_comp_reg_prc(AVND_CB *, AVND_COMP *, AVND_COMP *,
                                  AVSV_AMF_COMP_REG_PARAM *, MDS_DEST *);

static void avnd_comp_hdl_finalize(AVND_CB *, AVND_COMP *,
                                   AVSV_AMF_FINALIZE_PARAM *,
                                   AVSV_AMF_API_INFO *);
static SaAisErrorT avnd_validate_comp_and_createdb(AVND_CB *cb,
                                                   const std::string &comp_dn);

/****************************************************************************
  Name          : avnd_evt_ava_finalize

  Description   : This routine processes the AMF Finalize message from AvA.
                  It removes any association with the specified AMF hdl. If
                  the component is registered using this handle, it is
                  unregistered.

  Arguments     : cb  - ptr to the AvND control block
                  evt - ptr to the AvND event

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avnd_evt_ava_finalize_evh(AVND_CB *cb, AVND_EVT *evt) {
  AVSV_AMF_API_INFO *api_info = &evt->info.ava.msg->info.api_info;
  AVSV_AMF_FINALIZE_PARAM *fin = &api_info->param.finalize;
  AVND_COMP *comp = 0;
  AVND_COMP_PXIED_REC *rec = 0, *curr_rec = 0;
  uint32_t rc = NCSCC_RC_SUCCESS;

  TRACE_ENTER();

  if (cb->is_avd_down == true) {
    LOG_ER("AVD is down. Component finalization not available.");
    goto done;
  }

  /*
   * See appendix B. Non registered processes can use parts of the API.
   * For such processes finalize is OK, AMF has no allocated resources.
   */
  comp = avnd_compdb_rec_get(cb->compdb, Amf::to_string(&fin->comp_name));
  if (!comp) {
    TRACE("Comp DB record lookup failed: '%s'",
          osaf_extended_name_borrow(&fin->comp_name));
    goto done;
  }

  /* If Amf was waiting for down event to come regarding term cbq, but
     finalize has come, so reset the variable (irrespective of Sa-Aware
     PI or Proxied PI) and proceed for finalize. */

  if (comp->term_cbq_inv_value != 0) {
    AVND_COMP_CBK *cbk_rec;
    /* Amf was waiting for this down event. Get the matching
       entry from the cbk list and delete the cbq */
    m_AVND_COMP_CBQ_INV_GET(comp, comp->term_cbq_inv_value, cbk_rec);
    comp->term_cbq_inv_value = 0;
    rc = avnd_comp_clc_fsm_run(cb, comp, AVND_COMP_CLC_PRES_FSM_EV_TERM_SUCC);
    if (cbk_rec) avnd_comp_cbq_rec_pop_and_del(cb, comp, cbk_rec, false);
  }

  /* npi comps dont interact with amf */
  if (!m_AVND_COMP_TYPE_IS_PREINSTANTIABLE(comp) &&
      !m_AVND_COMP_TYPE_IS_PROXIED(comp))
    goto done;

  /*
   * Remove all the records that use this handle. This includes the records
   * present in hc-list, pm-list, pg-list & cbk-list. For proxy component
   * check its its list of proxied components to see if anybody is using
   * this handle
   */

  avnd_pg_finalize(cb, fin->hdl, &api_info->dest);

  if (m_AVND_COMP_TYPE_IS_PROXY(comp)) {
    /* look in each proxied for this handle */
    rec = (AVND_COMP_PXIED_REC *)m_NCS_DBLIST_FIND_FIRST(&comp->pxied_list);
    while (rec) {
      curr_rec = rec;
      rec = (AVND_COMP_PXIED_REC *)m_NCS_DBLIST_FIND_NEXT(&rec->comp_dll_node);

      /* if we are finalizing the reg hdl of proxy, unreg all its proxied.
       * if the hdl matches the reg-hdl of proxied, unreg that proxied
       */
      if (curr_rec->pxied_comp->reg_hdl == fin->hdl ||
          comp->reg_hdl == fin->hdl)
        rc = avnd_comp_unreg_prc(cb, curr_rec->pxied_comp, comp);

      if (NCSCC_RC_SUCCESS != rc) goto done;
    } /* while */
  }

  /* unregister the component if the reg-hdl is being finalized */
  if (comp->reg_hdl == fin->hdl) {
    if (m_AVND_COMP_TYPE_IS_PROXIED(comp))
      rc = avnd_comp_unreg_prc(cb, comp, comp->pxy_comp);
    else
      rc = avnd_comp_unreg_prc(cb, comp, nullptr);
  }
  /* Now do the same for the normal component */
  avnd_comp_hdl_finalize(cb, comp, fin, api_info);

done:
  if (NCSCC_RC_SUCCESS != rc) {
    LOG_ER("avnd_evt_ava_finalize():'%s' and Hdl= %llx",
           osaf_extended_name_borrow(&fin->comp_name), fin->hdl);
  }

  TRACE_LEAVE();
  return rc;
}

/****************************************************************************
  Name          : avnd_evt_ava_comp_reg

  Description   : This routine processes the component register message from
                  AvA.

  Arguments     : cb  - ptr to the AvND control block
                  evt - ptr to the AvND event

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avnd_evt_ava_comp_reg_evh(AVND_CB *cb, AVND_EVT *evt) {
  AVSV_AMF_API_INFO *api_info = &evt->info.ava.msg->info.api_info;
  AVSV_AMF_COMP_REG_PARAM *reg = &api_info->param.reg;
  AVND_COMP *comp = 0;
  AVND_COMP *pxy_comp = 0;
  uint32_t rc = NCSCC_RC_SUCCESS;
  SaAisErrorT amf_rc = SA_AIS_OK;
  bool msg_from_avnd = false, int_ext_comp_flag = false;
  NODE_ID node_id;

  TRACE_ENTER();

  if (AVND_EVT_AVND_AVND_MSG == evt->type) {
    /* This means that the message has come from proxy AvND to this AvND. */
    msg_from_avnd = true;
  }

  /* validate the register message */
  avnd_comp_reg_val(cb, reg, &comp, &pxy_comp, &amf_rc, &int_ext_comp_flag,
                    msg_from_avnd);

  if ((true == int_ext_comp_flag) && (false == msg_from_avnd)) {
    /* We need to send a validation message to AvD for this component and after
       getting response from AvD, we can send response to AvA. */
    rc = avnd_evt_ava_comp_val_req(cb, evt);
    return rc;
  }

  if ((true == int_ext_comp_flag) && (true == msg_from_avnd)) {
    /* We need to add proxy information here and then proceed for registration.
     */
    /* Here node id of the AvND (where the proxy is running and from where the
       req has come) is being drived from MDS DEST of proxy
       component */
    node_id = m_NCS_NODE_ID_FROM_MDS_DEST(api_info->dest);
    pxy_comp =
        avnd_internode_comp_add(cb, Amf::to_string(&(reg->proxy_comp_name)),
                                node_id, &rc, comp->su->su_is_external, true);

    if (nullptr == pxy_comp)
      amf_rc = static_cast<SaAisErrorT>(rc);
    else {
      /* We need to set amf_rc to OK as this has been changed to INVLD. See
         "*o_amf_rc = SA_AIS_ERR_INVALID_PARAM;" in avnd_comp_reg_val(). */
      amf_rc = SA_AIS_OK;
    }
  }

  /* send the response back to AvA */
  rc = avnd_amf_resp_send(cb, AVSV_AMF_COMP_REG, amf_rc, 0, &api_info->dest,
                          &evt->mds_ctxt, comp, msg_from_avnd);

  /* now register the component */
  if ((SA_AIS_OK == amf_rc) && (NCSCC_RC_SUCCESS == rc))
    rc = avnd_comp_reg_prc(cb, comp, pxy_comp, reg, &api_info->dest);

  TRACE_LEAVE();
  return rc;
}

/****************************************************************************
  Name          : avnd_evt_ava_comp_unreg

  Description   : This routine processes the component unregister message
                  from AvA.

  Arguments     : cb  - ptr to the AvND control block
                  evt - ptr to the AvND event

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avnd_evt_ava_comp_unreg_evh(AVND_CB *cb, AVND_EVT *evt) {
  AVSV_AMF_API_INFO *api_info = &evt->info.ava.msg->info.api_info;
  AVSV_AMF_COMP_UNREG_PARAM *unreg = &api_info->param.unreg;
  AVND_COMP *comp = 0;
  AVND_COMP *pxy_comp = 0;
  uint32_t rc = NCSCC_RC_SUCCESS;
  SaAisErrorT amf_rc = SA_AIS_OK;
  bool msg_from_avnd = false, int_ext_comp = false;

  TRACE_ENTER();

  /* return error if amfd is down */
  if (cb->is_avd_down == true) {
    LOG_ER("AVD is down. Component unregistration not available.");
    goto done;
  }

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

  /* validate the unregister message */
  avnd_comp_unreg_val(cb, unreg, &comp, &pxy_comp, &amf_rc);

  /* send the response back to AvA */
  rc = avnd_amf_resp_send(cb, AVSV_AMF_COMP_UNREG, amf_rc, 0, &api_info->dest,
                          &evt->mds_ctxt, comp, msg_from_avnd);

  /* now unregister the component */
  if ((SA_AIS_OK == amf_rc) && (NCSCC_RC_SUCCESS == rc))
    rc = avnd_comp_unreg_prc(cb, comp, pxy_comp);

done:

  if (NCSCC_RC_SUCCESS != rc) {
    LOG_ER("avnd_evt_ava_comp_unreg():'%s' and Hdl='%llx'",
           osaf_extended_name_borrow(&unreg->comp_name), unreg->hdl);
  }

  TRACE_LEAVE();
  return rc;
}

/****************************************************************************
  Name          : avnd_evt_ava_ha_get

  Description   : This routine processes the ha state get message from AvA.

  Arguments     : cb  - ptr to the AvND control block
                  evt - ptr to the AvND event

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avnd_evt_ava_ha_get_evh(AVND_CB *cb, AVND_EVT *evt) {
  AVSV_AMF_API_INFO *api_info = &evt->info.ava.msg->info.api_info;
  AVSV_AMF_HA_GET_PARAM *ha_get = &api_info->param.ha_get;
  AVND_COMP *comp = 0;
  AVND_COMP_CSI_REC *csi_rec = 0;
  uint32_t rc = NCSCC_RC_SUCCESS;
  SaAisErrorT amf_rc = SA_AIS_OK;
  SaAmfHAStateT *ha_rc = &ha_get->ha;
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

  /* get the comp & csi records */
  comp = avnd_compdb_rec_get(cb->compdb, Amf::to_string(&ha_get->comp_name));
  if (comp)
    csi_rec = m_AVND_COMPDB_REC_CSI_GET(
        *comp, Amf::to_string(&ha_get->csi_name).c_str());

  /* determine the error code, if any */
  if (!comp) amf_rc = SA_AIS_ERR_INVALID_PARAM;
  if (comp && !m_AVND_COMP_TYPE_IS_PREINSTANTIABLE(comp))
    amf_rc = SA_AIS_ERR_BAD_OPERATION;
  if ((comp && !m_AVND_COMP_IS_REG(comp)) || !csi_rec)
    amf_rc = SA_AIS_ERR_NOT_EXIST;

  // get the actual ha_state of csi
  if (!csi_rec) {
    *ha_rc = (SaAmfHAStateT)0;
  } else {
    *ha_rc = csi_rec->si->curr_state;
    if (csi_rec->si->curr_state == SA_AMF_HA_QUIESCING &&
        (csi_rec->curr_assign_state == AVND_COMP_CSI_ASSIGN_STATE_ASSIGNED ||
         csi_rec->curr_assign_state == AVND_COMP_CSI_ASSIGN_STATE_REMOVING)) {
      *ha_rc = SA_AMF_HA_QUIESCED;
    }
  }
  /* send the response back to AvA */
  rc = avnd_amf_resp_send(cb, AVSV_AMF_HA_STATE_GET, amf_rc, (uint8_t *)ha_rc,
                          &api_info->dest, &evt->mds_ctxt, comp, msg_from_avnd);

done:
  if (NCSCC_RC_SUCCESS != rc) {
    LOG_ER("avnd_evt_ava_ha_get():'%s' Hdl:%llx HA:%u",
           osaf_extended_name_borrow(&ha_get->comp_name), ha_get->hdl,
           ha_get->ha);
  }

  TRACE_LEAVE();
  return rc;
}

/****************************************************************************
  Name          : avnd_evt_mds_ava_dn

  Description   : This routine processes the AvA down event from MDS. It
                  picks a component that is registered with the same mds-dest
                  & starts the error processing for it. Note that if
                  monitoring (active/passive) is on with this mds-dest, an
                  error is subsequently reported.

  Arguments     : cb  - ptr to the AvND control block
                  evt - ptr to the AvND event

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avnd_evt_mds_ava_dn_evh(AVND_CB *cb, AVND_EVT *evt) {
  AVND_MDS_EVT *mds_evt = &evt->info.mds;
  AVND_ERR_INFO err_info;
  AVND_COMP *comp = 0;
  uint32_t rc = NCSCC_RC_SUCCESS;
  std::string name;

  TRACE_ENTER();

  // update MDS version db.
  agent_mds_ver_db.erase(mds_evt->mds_dest);
  /* get the matching registered comp (if any) */
  for (comp = avnd_compdb_rec_get_next(cb->compdb, ""); comp != nullptr;
       comp = avnd_compdb_rec_get_next(cb->compdb, name)) {
    name = comp->name;
    if (0 == memcmp(&comp->reg_dest, &mds_evt->mds_dest, sizeof(MDS_DEST))) {
      /* proxied component can't have mds down event */
      if (m_AVND_COMP_TYPE_IS_PROXIED(comp))
        continue;
      else
        break;
    } else {
      comp = nullptr;
    }
  } /* for */

  if (comp) {
    if (comp->term_cbq_inv_value != 0) {
      AVND_COMP_CBK *cbk_rec;
      /* Amf was waiting for this down event. Get the matching
         entry from the cbk list and delete the cbq */
      m_AVND_COMP_CBQ_INV_GET(comp, comp->term_cbq_inv_value, cbk_rec);
      comp->term_cbq_inv_value = 0;
      rc = avnd_comp_clc_fsm_run(cb, comp, AVND_COMP_CLC_PRES_FSM_EV_TERM_SUCC);
      if (cbk_rec) avnd_comp_cbq_rec_pop_and_del(cb, comp, cbk_rec, false);
      goto done;
    }
    /* found the matching comp; trigger error processing */
    err_info.src = AVND_ERR_SRC_AVA_DN;
    err_info.rec_rcvr.avsv_ext =
        static_cast<AVSV_ERR_RCVR>(comp->err_info.def_rec);
    rc = avnd_err_process(cb, comp, &err_info);
  }

done:
  /* pg tracking may be started by this ava... delete those traces */
  avnd_pg_finalize(cb, 0, &mds_evt->mds_dest);

  TRACE_LEAVE();
  return rc;
}

/****************************************************************************
  Name          : avnd_comp_reg_val

  Description   : This routine validates the component register parameters.

  Arguments     : cb       - ptr to the AvND control block
                  reg      - ptr to the register params
                  o_comp   - double ptr to the comp being registered(o/p)
                  o_pxy_comp - double ptr to the registering comp (o/p)
                  o_amf_rc - double ptr to the amf-rc (o/p)

  Return Values : None

  Notes         : None
******************************************************************************/
void avnd_comp_reg_val(AVND_CB *cb, AVSV_AMF_COMP_REG_PARAM *reg,
                       AVND_COMP **o_comp, AVND_COMP **o_pxy_comp,
                       SaAisErrorT *o_amf_rc, bool *int_ext_comp_flag,
                       bool msg_from_avnd) {
  *o_amf_rc = SA_AIS_OK;

  /***************************************************************************
   *
   *         Validation for proxied components starts here.
   *
   ***************************************************************************/
  /* get the comp */
  if ((*o_comp = m_AVND_INT_EXT_COMPDB_REC_GET(
           cb->internode_avail_comp_db, Amf::to_string(&reg->comp_name))) ==
          nullptr &&
      (false == msg_from_avnd)) {
    if ((*o_comp = avnd_compdb_rec_get(
             cb->compdb, Amf::to_string(&reg->comp_name))) == nullptr) {
      /* Proxied Component may belong to the same node but not available right
         now because they might not have got instantiated(In SU presence command
         only we read all comp of SUs). We need to construct data base if comp
         belong to the same node for the registration to proceed.*/
      if (SA_AIS_OK == avnd_validate_comp_and_createdb(
                           cb, Amf::to_string(&reg->comp_name))) {
        *o_comp =
            avnd_compdb_rec_get(cb->compdb, Amf::to_string(&reg->comp_name));
        goto proceed;
      }

      /* It might be an internode or external component. */
      *o_amf_rc = SA_AIS_ERR_INVALID_PARAM;
      *int_ext_comp_flag = true;

      return;
    } else {
      /* Just check that whether it is an external component. If yes, then we
         need to treat it as internode component and send validation request
         to AvD. This is done to make process in sync with that of the
         internode proxy-proxied support.
         This will happen when a cluster component (on controller) registers
         an external component. */
      if (true == (*o_comp)->su->su_is_external) {
        *int_ext_comp_flag = true;
        return;
      }
    }
  }
proceed:
  if ((*o_comp = avnd_compdb_rec_get(
           cb->compdb, Amf::to_string(&reg->comp_name))) == nullptr &&
      msg_from_avnd == true) {
    /* This is case when msg_from_avnd is true i.e. internode proxied comp.
       Proxied Component may belong to this node but not available right now
       because they might not have got instantiated(In SU presence command only
       we read all comp of SUs). We need to construct data base if comp belong
       to the node for the registration to proceed.*/
    if (SA_AIS_OK ==
        avnd_validate_comp_and_createdb(cb, Amf::to_string(&reg->comp_name))) {
      *o_comp =
          avnd_compdb_rec_get(cb->compdb, Amf::to_string(&reg->comp_name));
      goto proceed_next;
    }
    /* It might be an internode or external component. */
    *o_amf_rc = SA_AIS_ERR_INVALID_PARAM;
    return;
  }

proceed_next:

  if (nullptr == (*o_comp)) {
    *o_amf_rc = SA_AIS_ERR_INVALID_PARAM;
    return;
  }

  if ((m_AVND_COMP_TYPE_IS_INTER_NODE(*o_comp)) &&
      (true == (*o_comp)->reg_resp_pending)) {
    *o_amf_rc = SA_AIS_ERR_EXIST;
    return;
  }

  /* verify if the comp is not already registered */
  if (m_AVND_COMP_IS_REG(*o_comp) && msg_from_avnd) {
    *o_amf_rc = SA_AIS_ERR_TRY_AGAIN;
    return;
  }

  /* verify if the comp is not already registered */
  if (m_AVND_COMP_IS_REG(*o_comp)) {
    *o_amf_rc = SA_AIS_ERR_EXIST;
    return;
  }

  /* verify if a non-proxied component has supplied a proxy name */
  if (!m_AVND_COMP_TYPE_IS_PROXIED(*o_comp) &&
      osaf_extended_name_length(&reg->proxy_comp_name)) {
    *o_amf_rc = SA_AIS_ERR_INVALID_PARAM;
    return;
  }

  /* Non Proxied comp should not be in any other state other than instantiating
     while its registering with AMF
   */
  if (!m_AVND_COMP_TYPE_IS_PROXIED(*o_comp) &&
      ((*o_comp)->pres != SA_AMF_PRESENCE_INSTANTIATING) &&
      ((*o_comp)->pres != SA_AMF_PRESENCE_INSTANTIATED) &&
      ((*o_comp)->pres != SA_AMF_PRESENCE_RESTARTING)) {
    *o_amf_rc = SA_AIS_ERR_BAD_OPERATION;
    return;
  }

  /* npi comps dont interact with amf (proxied is an exception,
     proxy will interact with amf on behalf of proxied ) */
  if (!m_AVND_COMP_TYPE_IS_PREINSTANTIABLE(*o_comp) &&
      !m_AVND_COMP_TYPE_IS_PROXIED(*o_comp)) {
    *o_amf_rc = SA_AIS_ERR_BAD_OPERATION;
    return;
  }

  /***************************************************************************
   *
   *         Validation for proxied components ends here. Add all new
   *         validations for proxied above this.
   *
   ***************************************************************************/

  /***************************************************************************
   *
   *         Validation for proxy components starts here.
   *         Add new validation of proxy components below than that of proxied
   *         components. If there is a case of internode proxy component, then
   *we are returning from there, so if you add validation of proxied component
   *below the validation of proxied component then that would be
   *missed.
   *
   ***************************************************************************/
  /* check for proxy, while registering proxied component */
  if (m_AVND_COMP_TYPE_IS_PROXIED(*o_comp)) {
    /* get proxy comp */
    if ((*o_pxy_comp = avnd_compdb_rec_get(
             cb->compdb, Amf::to_string(&reg->proxy_comp_name))) == nullptr) {
      /*  Check whether this is proxied registration message from proxy AvND. */
      if (true == msg_from_avnd) {
        /* We are returning from here for Internode proxy component and we
           are not going to validate its registration below. We need not
           check registration for Internode as internode
           component is not suppoed to register to another node. */
        *int_ext_comp_flag = true;
      }

      *o_amf_rc = SA_AIS_ERR_INVALID_PARAM;
      return;
    } else {
      /* If we found it and the request is coming from AvND then it is a
         case when a cluster component on controller is trying to register
         an external component. So, we will treat it as an internode
         component. */
      if (true == msg_from_avnd) {
        *int_ext_comp_flag = true;
      }
    }

    /* has proxy registered itself before registering its proxied */
    /* We need not check registration for Internode as internode component is
       not suppoed to register to another node. */
    if (!m_AVND_COMP_IS_REG(*o_pxy_comp)) {
      *o_amf_rc = SA_AIS_ERR_NOT_EXIST;
      return;
    }
  }

  return;
}

/****************************************************************************
  Name          : avnd_comp_unreg_val

  Description   : This routine validates the component unregister parameters.

  Arguments     : cb       - ptr to the AvND control block
                  unreg    - ptr to the unregister params
                  o_comp   - double ptr to the unregistering comp (o/p)
                  o_amf_rc - double ptr to the amf-rc (o/p)

  Return Values : None

  Notes         : None
******************************************************************************/
void avnd_comp_unreg_val(AVND_CB *cb, AVSV_AMF_COMP_UNREG_PARAM *unreg,
                         AVND_COMP **o_comp, AVND_COMP **o_pxy_comp,
                         SaAisErrorT *o_amf_rc) {
  *o_amf_rc = SA_AIS_OK;

  /* get the comp */
  if ((*o_comp = avnd_compdb_rec_get(
           cb->compdb, Amf::to_string(&unreg->comp_name))) == nullptr) {
    *o_amf_rc = SA_AIS_ERR_INVALID_PARAM;
    return;
  }

  /* verify if the comp is registered */
  if (!m_AVND_COMP_IS_REG(*o_comp)) {
    *o_amf_rc = SA_AIS_ERR_NOT_EXIST;
    return;
  }

  /* verify if the comp was registered with the same hdl value */
  if ((*o_comp)->reg_hdl != unreg->hdl) {
    *o_amf_rc = SA_AIS_ERR_BAD_HANDLE;
    return;
  }

  /* verify if the non-proxied comp has supplied proxy-name */
  if (!m_AVND_COMP_TYPE_IS_PROXIED(*o_comp) &&
      osaf_extended_name_length(&unreg->proxy_comp_name)) {
    *o_amf_rc = SA_AIS_ERR_INVALID_PARAM;
    return;
  }

  /* npi comps dont interact with amf (proxied being an exception) */
  if (!m_AVND_COMP_TYPE_IS_PREINSTANTIABLE(*o_comp) &&
      !m_AVND_COMP_TYPE_IS_PROXIED(*o_comp)) {
    *o_amf_rc = SA_AIS_ERR_BAD_OPERATION;
    return;
  }

  /*verify that this comp is not acting as proxy for anybody */
  if ((*o_comp)->pxied_list.n_nodes != 0) {
    *o_amf_rc = SA_AIS_ERR_BAD_OPERATION;
    return;
  }

  if (m_AVND_COMP_TYPE_IS_PROXIED(*o_comp)) {
    /* get the proxy comp */
    *o_pxy_comp = (*o_comp)->pxy_comp;

    /* verify if the proxy comp is registered */
    /* If component is internode then there is no need of validation as
       internode doesn't register. */
    if ((!m_AVND_COMP_TYPE_IS_INTER_NODE(*o_pxy_comp)) &&
        (!m_AVND_COMP_IS_REG(*o_pxy_comp))) {
      *o_amf_rc = SA_AIS_ERR_NOT_EXIST;
      return;
    }

    /*verify if pxy name maches */
    if ((*o_pxy_comp)->name.compare(Amf::to_string(&unreg->proxy_comp_name))) {
      *o_amf_rc = SA_AIS_ERR_BAD_OPERATION;
      return;
    }
  }

  /* keep adding new validation checks */

  return;
}

/****************************************************************************
  Name          : avnd_comp_reg_prc

  Description   : This routine registers the specified component. It updates
                  the component with the register parameters. It then updates
                  the component & service unit operational states if the
                  presence state becomes 'instantiated'. If the presence state
                  is instantiating, component FSM is triggered.

  Arguments     : cb   - ptr to the AvND control block
                  comp - ptr to the registering comp
                  reg  - ptr to the register params
                  dest - ptr to the mds-dest

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avnd_comp_reg_prc(AVND_CB *cb, AVND_COMP *comp, AVND_COMP *pxy_comp,
                           AVSV_AMF_COMP_REG_PARAM *reg, MDS_DEST *dest) {
  bool su_is_enabled;
  uint32_t rc = NCSCC_RC_SUCCESS;
  TRACE_ENTER2("comp: '%s'", comp->name.c_str());

  if (pxy_comp) TRACE("proxy comp = '%s'", pxy_comp->name.c_str());

  /* update the comp reg params */
  comp->reg_hdl = reg->hdl;
  comp->reg_dest = *dest;
  comp->version = reg->version;
  TRACE("SAF version '%c'.'%u'.'%u'", comp->version.releaseCode,
        comp->version.majorVersion, comp->version.minorVersion);
  m_AVND_COMP_REG_SET(comp);

  /* if proxied comp, do add to the pxied_list of pxy */
  if (m_AVND_COMP_TYPE_IS_PROXIED(comp))
    rc = avnd_comp_proxied_add(cb, comp, pxy_comp, true);

  if (rc != NCSCC_RC_SUCCESS) goto done;

  /* process comp registration */
  if (m_AVND_COMP_PRES_STATE_IS_INSTANTIATING(comp) ||
      m_AVND_COMP_PRES_STATE_IS_RESTARTING(comp)) {
    /* if inst-cmd has already finished successfully, trigger comp fsm */
    if (m_AVND_COMP_IS_INST_CMD_SUCC(comp) &&
        !m_AVND_COMP_TYPE_IS_PROXIED(comp))
      rc = avnd_comp_clc_fsm_run(cb, comp, AVND_COMP_CLC_PRES_FSM_EV_INST_SUCC);

    /* proxy comp will instantiate only after reg, now we are done with reg,
       trigger the fsm with InstEv so that we can invoke callback */
    if (m_AVND_COMP_TYPE_IS_PROXIED(comp))
      rc = avnd_comp_clc_fsm_run(cb, comp, AVND_COMP_CLC_PRES_FSM_EV_INST);

  } else if (m_AVND_COMP_PRES_STATE_IS_INSTANTIATED(comp)) {
    /* update comp oper state */
    m_AVND_COMP_OPER_STATE_SET(comp, SA_AMF_OPERATIONAL_ENABLED);
    rc = avnd_comp_oper_state_avd_sync(cb, comp);
    if (NCSCC_RC_SUCCESS != rc) goto done;

    /* update su oper state */
    m_AVND_SU_IS_ENABLED(comp->su, su_is_enabled);
    if (true == su_is_enabled) {
      m_AVND_SU_OPER_STATE_SET(comp->su, SA_AMF_OPERATIONAL_ENABLED);

      /* inform AvD */
      rc = avnd_di_oper_send(cb, comp->su, 0);
    }
  } else if (m_AVND_COMP_PRES_STATE_IS_ORPHANED(comp))
    rc = avnd_comp_clc_fsm_run(cb, comp, AVND_COMP_CLC_PRES_FSM_EV_INST_SUCC);

/* we need not do anything for uninstantiated state, proxy can register
   the proxied component before the instantiation of the proxied.
   we just need to mark the comp as registered and update the database. */
done:
  TRACE_LEAVE2("%u", rc);
  return rc;
}

/****************************************************************************
  Name          : avnd_comp_unreg_prc

  Description   : This routine unregisters the specified component. It resets
                  the component register parameters. It then updates the
                  component & service unit operational states if the presence
                  state in 'instantiated'. If the presence state is
                  instantiating, nothing is done (note that the comp can
                  register / unregister multiple times before inst-cmd
                  succeeds).

  Arguments     : cb   - ptr to the AvND control block
                  comp - ptr to the registering comp

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avnd_comp_unreg_prc(AVND_CB *cb, AVND_COMP *comp,
                             AVND_COMP *pxy_comp) {
  uint32_t rc = NCSCC_RC_SUCCESS;
  TRACE_ENTER2("comp: '%s' : proxy_comp: '%p'", comp->name.c_str(), pxy_comp);

  /* Check if this component is an internode/ext component. */
  if (m_AVND_COMP_TYPE_IS_INTER_NODE(comp)) {
    /* This is a case when a proxy component is unregistering the proxied
       component, which is an internode/ext component. This will also
       happen in finalization of proxy component, avnd_comp_unreg_prc is
       called from avnd_evt_ava_finalize. */
    AVSV_AMF_API_INFO api_info;
    MDS_SYNC_SND_CTXT ctxt;
    SaAisErrorT amf_rc = SA_AIS_OK;
    bool int_ext_comp = false;

    memset(&ctxt, 0, sizeof(MDS_SYNC_SND_CTXT));
    memset(&api_info, 0, sizeof(AVSV_AMF_API_INFO));

    api_info.type = AVSV_AMF_COMP_UNREG;
    api_info.dest = comp->reg_dest;
    api_info.param.unreg.hdl = comp->reg_hdl;
    osaf_extended_name_alloc(comp->name.c_str(),
                             &api_info.param.unreg.comp_name);
    osaf_extended_name_alloc(pxy_comp->name.c_str(),
                             &api_info.param.unreg.proxy_comp_name);

    rc = avnd_int_ext_comp_hdlr(cb, &api_info, &ctxt, &amf_rc, &int_ext_comp);
    /* Since there is no Mds Context information being sent so, no response
       will come. So, we need to delete the proxied information. */

    if (NCSCC_RC_SUCCESS != rc) {
      LOG_ER("avnd_int_ext_comp_hdlr fail:'%s' Type=%u Hdl=%llx Dest=%" PRIu64,
             comp->name.c_str(), api_info.type, api_info.param.reg.hdl,
             api_info.dest);
      goto err;
    }

    rc = avnd_comp_proxied_del(cb, comp, comp->pxy_comp, false, nullptr);

    if (NCSCC_RC_SUCCESS != rc) {
      LOG_ER("avnd_comp_proxied_del fail:'%s' Type=%u Hdl='%llx' Dest=%" PRId64,
             comp->name.c_str(), api_info.type, api_info.param.reg.hdl,
             api_info.dest);
      goto err;
    }
    rc = avnd_internode_comp_del(cb, comp->name);

    if (NCSCC_RC_SUCCESS != rc) {
      LOG_ER(
          "avnd_internode_comp_del fail:'%s' Type=%u, Hdl='%llx' Dest=%" PRId64,
          comp->name.c_str(), api_info.type, api_info.param.reg.hdl,
          api_info.dest);
      goto err;
    }

  err:
    return rc;
  }

  /* reset the comp register params */
  m_AVND_COMP_REG_PARAM_RESET(cb, comp);

  if ((comp->su->is_ncs == true) &&
      (m_AVND_COMP_PRES_STATE_IS_INSTANTIATED(comp)) &&
      (!m_AVND_COMP_IS_FAILED(comp)) && (!m_AVND_COMP_TYPE_IS_PROXIED(comp))) {
    syslog(LOG_ERR, "'%s'unregistered", comp->name.c_str());
  }

  if (m_AVND_COMP_TYPE_IS_PROXIED(comp)) {
    if (m_AVND_COMP_PRES_STATE_IS_INSTANTIATED(comp)) {
      m_AVND_COMP_PROXY_STATUS_SET(comp, SA_AMF_PROXY_STATUS_UNPROXIED);
      rc = avnd_comp_proxy_status_avd_sync(cb, comp);
      if (NCSCC_RC_SUCCESS != rc) goto done;
    }

    /*remove the component from the list of proxied of its proxy */
    rc = avnd_comp_proxied_del(cb, comp, pxy_comp, true, nullptr);

    /* process proxied comp unregistration */
    if (m_AVND_COMP_PRES_STATE_IS_INSTANTIATED(comp))
      rc = avnd_comp_clc_fsm_run(cb, comp, AVND_COMP_CLC_PRES_FSM_EV_ORPH);
    goto done;
  }

  /* process comp unregistration */
  if (m_AVND_COMP_PRES_STATE_IS_INSTANTIATED(comp)) {
    /* finish off csi assignment */
    avnd_comp_unreg_cbk_process(cb, comp);

    /* update comp oper state */
    m_AVND_COMP_OPER_STATE_SET(comp, SA_AMF_OPERATIONAL_DISABLED);
    rc = avnd_comp_oper_state_avd_sync(cb, comp);
    if (NCSCC_RC_SUCCESS != rc) goto done;

    /* update su oper state */
    if (m_AVND_SU_OPER_STATE_IS_ENABLED(comp->su)) {
      m_AVND_SU_OPER_STATE_SET(comp->su, SA_AMF_OPERATIONAL_DISABLED);

      /* inform AvD */
      rc = avnd_di_oper_send(cb, comp->su, SA_AMF_COMPONENT_FAILOVER);
    }
  }

done:
  TRACE_LEAVE2("%u", rc);
  return rc;
}

/****************************************************************************
  Name          : avnd_comp_csi_assign

  Description   : This routine assigns the CSI to the component. It is
                  invoked as a part of SU-SI assignment.

  Arguments     : cb   - ptr to the AvND control block
                  comp - ptr to the comp
                  csi  - ptr to csi record (if 0, all the CSIs are assigned
                         in one shot)

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : In orphaned state, proxied component is unregisted, but still
                  can take csi assignments. callbacks will be cached in orphan
state
******************************************************************************/
uint32_t avnd_comp_csi_assign(AVND_CB *cb, AVND_COMP *comp,
                              AVND_COMP_CSI_REC *csi) {
  /* flags to indicate the prv & curr inst states of an npi comp */
  bool npi_prv_inst = true, npi_curr_inst = true;
  AVND_COMP_CSI_REC *curr_csi = 0;
  AVND_COMP_CSI_REC *t_csi = 0;
  AVND_COMP_CLC_PRES_FSM_EV comp_ev = AVND_COMP_CLC_PRES_FSM_EV_MAX;
  bool mark_csi = false;
  uint32_t rc = NCSCC_RC_SUCCESS;
  const std::string csiname = csi ? csi->name : "all CSIs";

  curr_csi = (csi) ? csi
                   : m_AVND_CSI_REC_FROM_COMP_DLL_NODE_GET(
                         m_NCS_DBLIST_FIND_FIRST(&comp->csi_list));

  TRACE_ENTER2("comp:'%s'", comp->name.c_str());
  LOG_IN("Assigning '%s' %s to '%s'", csiname.c_str(),
         ha_state[curr_csi->si->curr_state], comp->name.c_str());

  /* skip assignments to unqualified comp */
  if (!m_AVND_SU_IS_RESTART(comp->su) && !IsCompQualifiedAssignment(comp)) {
    TRACE("Not suRestart context and comp is failed or unregistered");
    /* dont skip restarting components. wait till restart is complete */
    if ((comp->pres == SA_AMF_PRESENCE_RESTARTING) &&
        m_AVND_COMP_TYPE_IS_PREINSTANTIABLE(
            comp)) { /* mark the csi(s) assigned */
      if (csi) {
        TRACE("'%s'", csi->name.c_str());
        /* after restart we should go ahead with next csi, so mark the curr_csi
         * as assigning */
        m_AVND_COMP_CSI_CURR_ASSIGN_STATE_SET(
            csi, AVND_COMP_CSI_ASSIGN_STATE_ASSIGNING);
      } else {
        m_AVND_COMP_ALL_CSI_SET(comp);
        for (curr_csi = m_AVND_CSI_REC_FROM_COMP_DLL_NODE_GET(
                 m_NCS_DBLIST_FIND_FIRST(&comp->csi_list));
             curr_csi; curr_csi = m_AVND_CSI_REC_FROM_COMP_DLL_NODE_GET(
                           m_NCS_DBLIST_FIND_NEXT(&curr_csi->comp_dll_node))) {
          /* after restart we should go ahead with next csi, so mark the
           * curr_csi as assigning */
          m_AVND_COMP_CSI_CURR_ASSIGN_STATE_SET(
              curr_csi, AVND_COMP_CSI_ASSIGN_STATE_ASSIGNING);
        } /* for */
      }

      goto done;
    } else if ((comp->pres == SA_AMF_PRESENCE_RESTARTING) &&
               !m_AVND_COMP_TYPE_IS_PREINSTANTIABLE(comp)) {
      /* while restarting only quiesced assignment can expected,both will lead
         to cleanup of component, so we can ignore the error */
      m_AVND_COMP_FAILED_RESET(comp);

      /* fall down down to assignment flow */
    } else {
      rc = avnd_comp_csi_assign_done(cb, comp, csi);
      goto done;
    }
  } else if (m_AVND_SU_IS_RESTART(comp->su) &&
             (su_all_comps_restartable(*comp->su) == false) &&
             (m_AVND_COMP_IS_FAILED(comp) ||
              (comp->pres == SA_AMF_PRESENCE_UNINSTANTIATED))) {
    /* In surestart recovery of non restartable su, assignment (quiesced) done
       indication will be done for a failed component or for the component which
       is already cleaned up.
     */
    TRACE("In suRestart recovery or admin op context");
    if (csi) {
      TRACE("'%s'", csi->name.c_str());
      m_AVND_COMP_CSI_CURR_ASSIGN_STATE_SET(
          csi, AVND_COMP_CSI_ASSIGN_STATE_ASSIGNING);
    } else {
      m_AVND_COMP_ALL_CSI_SET(comp);
      for (curr_csi = m_AVND_CSI_REC_FROM_COMP_DLL_NODE_GET(
               m_NCS_DBLIST_FIND_FIRST(&comp->csi_list));
           curr_csi; curr_csi = m_AVND_CSI_REC_FROM_COMP_DLL_NODE_GET(
                         m_NCS_DBLIST_FIND_NEXT(&curr_csi->comp_dll_node))) {
        m_AVND_COMP_CSI_CURR_ASSIGN_STATE_SET(
            curr_csi, AVND_COMP_CSI_ASSIGN_STATE_ASSIGNING);
      }
    }
    rc = avnd_comp_csi_assign_done(cb, comp, csi);
    goto done;
  }

  /* skip standby assignment to x_active or 1_active capable comp */
  curr_csi = (csi) ? csi
                   : m_AVND_CSI_REC_FROM_COMP_DLL_NODE_GET(
                         m_NCS_DBLIST_FIND_FIRST(&comp->csi_list));
  if (curr_csi && (SA_AMF_HA_STANDBY == curr_csi->si->curr_state) &&
      ((curr_csi->capability == SA_AMF_COMP_X_ACTIVE) ||
       (curr_csi->capability == SA_AMF_COMP_1_ACTIVE))) {
    m_AVND_COMP_CSI_CURR_ASSIGN_STATE_SET(curr_csi,
                                          AVND_COMP_CSI_ASSIGN_STATE_ASSIGNED);
    rc = avnd_comp_csi_assign_done(cb, comp, csi);
    goto done;
  }

  /* initiate csi assignment to pi comp */
  if (m_AVND_COMP_TYPE_IS_PREINSTANTIABLE(comp)) {
    /* assign all csis in one shot */
    if (!csi) {
      m_AVND_COMP_ALL_CSI_SET(comp);
      curr_csi = m_AVND_CSI_REC_FROM_COMP_DLL_NODE_GET(
          m_NCS_DBLIST_FIND_FIRST(&comp->csi_list));
      if (!m_AVND_COMP_CSI_PRV_ASSIGN_STATE_IS_ASSIGNED(curr_csi)) {
        /*
         * => prv si assignment did not complete
         * (possibly due to comp error)
         */
        if (((SA_AMF_HA_QUIESCED == curr_csi->si->curr_state) ||
             (SA_AMF_HA_QUIESCING == curr_csi->si->curr_state)) &&
            (SA_AMF_HA_QUIESCING != curr_csi->si->prv_state) &&
            (SA_AMF_HA_ACTIVE != curr_csi->si->prv_state)) {
          /* no need to assign this csi.. generate csi-oper done indication */
          rc = avnd_comp_csi_assign_done(cb, comp, 0);
          if (NCSCC_RC_SUCCESS != rc) goto done;
        } else {
          if (curr_csi->curr_assign_state ==
                  AVND_COMP_CSI_ASSIGN_STATE_UNASSIGNED &&
              curr_csi->prv_assign_state ==
                  AVND_COMP_CSI_ASSIGN_STATE_UNASSIGNED) {
            // Mark suspending_assignment for all unassigned csi(s) which are
            // going to be assigned to *curr_csi->comp*
            for (t_csi = m_AVND_CSI_REC_FROM_COMP_DLL_NODE_GET(
                     m_NCS_DBLIST_FIND_FIRST(&curr_csi->comp->csi_list));
                 t_csi; t_csi = m_AVND_CSI_REC_FROM_COMP_DLL_NODE_GET(
                            m_NCS_DBLIST_FIND_NEXT(&t_csi->comp_dll_node))) {
              if (t_csi->curr_assign_state ==
                      AVND_COMP_CSI_ASSIGN_STATE_UNASSIGNED &&
                  t_csi->prv_assign_state ==
                      AVND_COMP_CSI_ASSIGN_STATE_UNASSIGNED) {
                t_csi->suspending_assignment = true;
              }
            }
            // just generate csi-oper done for unassigned csi
            rc = avnd_comp_csi_assign_done(cb, comp, 0);
            if (NCSCC_RC_SUCCESS != rc) goto done;
          } else {
            /* active/standby can be directly assigned */
            rc = avnd_comp_cbk_send(cb, comp, AVSV_AMF_CSI_SET, 0, 0);
            if (NCSCC_RC_SUCCESS != rc) goto done;
            mark_csi = true;
          }
        }
      } else {
        /* assign the csi as the comp is aware of atleast one csi */
        rc = avnd_comp_cbk_send(cb, comp, AVSV_AMF_CSI_SET, 0, 0);
        if (NCSCC_RC_SUCCESS != rc) goto done;
        mark_csi = true;
      }

      /* mark the csis */
      if (true == mark_csi) {
        for (curr_csi = m_AVND_CSI_REC_FROM_COMP_DLL_NODE_GET(
                 m_NCS_DBLIST_FIND_FIRST(&comp->csi_list));
             curr_csi; curr_csi = m_AVND_CSI_REC_FROM_COMP_DLL_NODE_GET(
                           m_NCS_DBLIST_FIND_NEXT(&curr_csi->comp_dll_node))) {
          m_AVND_COMP_CSI_CURR_ASSIGN_STATE_SET(
              curr_csi, AVND_COMP_CSI_ASSIGN_STATE_ASSIGNING);
        } /* for */
      }
    }

    /* assign csis one at a time */
    if (csi) {
      if (!m_AVND_COMP_CSI_PRV_ASSIGN_STATE_IS_ASSIGNED(csi)) {
        /*
         * => prv si assignment did not complete
         * (possibly due to comp error)
         */
        if (((SA_AMF_HA_QUIESCED == csi->si->curr_state) ||
             (SA_AMF_HA_QUIESCING == csi->si->curr_state)) &&
            (SA_AMF_HA_QUIESCING != csi->si->prv_state) &&
            (SA_AMF_HA_ACTIVE != curr_csi->si->prv_state)) {
          /* no need to assign this csi.. generate csi-oper done indication */
          rc = avnd_comp_csi_assign_done(cb, comp, csi);
          if (NCSCC_RC_SUCCESS != rc) goto done;
        } else {
          /* active/standby can be directly assigned */
          rc = avnd_comp_cbk_send(cb, comp, AVSV_AMF_CSI_SET, 0, csi);
          if (NCSCC_RC_SUCCESS != rc) goto done;
          m_AVND_COMP_CSI_CURR_ASSIGN_STATE_SET(
              csi, AVND_COMP_CSI_ASSIGN_STATE_ASSIGNING);
        }
      } else {
        /* => prv si assignment was completed.. assign the csi */
        rc = avnd_comp_cbk_send(cb, comp, AVSV_AMF_CSI_SET, 0, csi);
        if (NCSCC_RC_SUCCESS != rc) goto done;
        m_AVND_COMP_CSI_CURR_ASSIGN_STATE_SET(
            csi, AVND_COMP_CSI_ASSIGN_STATE_ASSIGNING);
      }
    }
  }

  /* initiate csi assignment to npi comp */
  if (!m_AVND_COMP_TYPE_IS_PREINSTANTIABLE(comp)) {
    /* get the only csi record */
    curr_csi = csi;
    if (!curr_csi)
      curr_csi = m_AVND_CSI_REC_FROM_COMP_DLL_NODE_GET(
          m_NCS_DBLIST_FIND_FIRST(&comp->csi_list));

    /* determine the instantiation state of npi comp */
    if (!m_AVND_COMP_CSI_PRV_ASSIGN_STATE_IS_ASSIGNED(curr_csi) ||
        (m_AVND_COMP_CSI_PRV_ASSIGN_STATE_IS_ASSIGNED(curr_csi) &&
         (SA_AMF_HA_ACTIVE != curr_csi->si->prv_state)))
      npi_prv_inst = false;
    if (SA_AMF_HA_ACTIVE != curr_csi->si->curr_state) npi_curr_inst = false;

    /* There is a possibility that we are getting quiesced due to SU failover
       after an SU restart. In this case we would have changed the csi state to
       unassigned. But the presence state will be still in instantiated state.
       so use the presence state to distinguish. ideally we should avoid using
       presence state while dealing with csi's. so this section has to be
       changed hope this wont break anything other way of doing it is to change
       the csi to assign state in failover error processing after restart.
     */
    TRACE("npi_prv_inst:%d,npi_curr_inst:%d", npi_prv_inst, npi_curr_inst);
    /* Active Assigning --> quiesced,  quiescing --> quiesced */
    if (!m_AVND_COMP_CSI_PRV_ASSIGN_STATE_IS_ASSIGNED(curr_csi) &&
        ((comp->pres == SA_AMF_PRESENCE_INSTANTIATED) ||
         (comp->pres == SA_AMF_PRESENCE_INSTANTIATING) ||
         (comp->pres == SA_AMF_PRESENCE_TERMINATING) ||
         (comp->pres == SA_AMF_PRESENCE_RESTARTING)))
      npi_prv_inst = true;
    TRACE("npi_prv_inst:%d,npi_curr_inst:%d", npi_prv_inst, npi_curr_inst);

    /* determine the event for comp fsm */
    if (m_AVND_SU_IS_RESTART(comp->su) && (true == npi_curr_inst))
      comp_ev = AVND_COMP_CLC_PRES_FSM_EV_INST;
    else if (!m_AVND_SU_IS_RESTART(comp->su) && (npi_prv_inst != npi_curr_inst))
      comp_ev = (true == npi_curr_inst) ? AVND_COMP_CLC_PRES_FSM_EV_INST
                                        : AVND_COMP_CLC_PRES_FSM_EV_TERM;
    else if (m_AVND_SU_IS_RESTART(comp->su) &&
             (su_all_comps_restartable(*comp->su) == false) &&
             (npi_curr_inst == false)) {
      /*suRestart is going on and SU has atleast one component
       * non-restartable.*/
      TRACE(
          "suRestart is going on for a non-restartable SU,"
          "  terminate this component for quiesced state.");
      comp_ev = AVND_COMP_CLC_PRES_FSM_EV_TERM;
    }

    /* mark the csi state assigning */
    m_AVND_COMP_CSI_CURR_ASSIGN_STATE_SET(curr_csi,
                                          AVND_COMP_CSI_ASSIGN_STATE_ASSIGNING);

    if (AVND_COMP_CLC_PRES_FSM_EV_MAX != comp_ev) {
      /* trigger comp fsm */
      if (!csi) {
        m_AVND_COMP_ALL_CSI_SET(comp);
      } else {
        m_AVND_COMP_ALL_CSI_RESET(comp);
      }

      rc = avnd_comp_clc_fsm_run(cb, comp, comp_ev);
    } else
      /* this csi assignment is over.. process it */
      rc = avnd_comp_csi_assign_done(cb, comp, (csi) ? csi : 0);
  }

done:
  TRACE_LEAVE2("%u", rc);
  return rc;
}
/**
 * @brief       Checks if any csi of same SI assigned to the component is in
 *removing state
 *
 * @param [in]  cmp
 *              si
 *
 * @returns     true/false
 **/
static bool csi_of_same_si_in_removing_state(const AVND_COMP *cmp,
                                             const AVND_SU_SI_REC *si) {
  AVND_COMP_CSI_REC *curr_csi;
  bool any_removing = false;

  for (curr_csi = m_AVND_CSI_REC_FROM_COMP_DLL_NODE_GET(
           m_NCS_DBLIST_FIND_FIRST(&cmp->csi_list));
       curr_csi; curr_csi = m_AVND_CSI_REC_FROM_COMP_DLL_NODE_GET(
                     m_NCS_DBLIST_FIND_NEXT(&curr_csi->comp_dll_node))) {
    if ((curr_csi->si == si) &&
        m_AVND_COMP_CSI_CURR_ASSIGN_STATE_IS_REMOVING(curr_csi)) {
      any_removing = true;
      break;
    }
  }

  return any_removing;
}

/****************************************************************************
  Name          : avnd_comp_csi_remove

  Description   : This routine removes the CSI from the component. It is
                  invoked as a part of SU-SI removal.

  Arguments     : cb   - ptr to the AvND control block
                  comp - ptr to the comp
                  csi  - ptr to csi record (if 0, all the CSIs are removed
                         in one shot)

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avnd_comp_csi_remove(AVND_CB *cb, AVND_COMP *comp,
                              AVND_COMP_CSI_REC *csi) {
  AVND_COMP_CSI_REC *curr_csi = 0;
  bool is_assigned = false;
  uint32_t rc = NCSCC_RC_SUCCESS;
  const std::string csiname = csi ? csi->name : "all CSIs";

  TRACE_ENTER2("comp: '%s' : csi: '%p'", comp->name.c_str(), csi);
  LOG_IN("Removing '%s' from '%s'", csiname.c_str(), comp->name.c_str());

  /* skip removal from unqualified comp */
  if (!IsCompQualifiedAssignment(comp)) {
    if (csi) {
      /* after restart we should go ahead with next csi, so mark the curr_csi as
       * assigning */
      m_AVND_COMP_CSI_CURR_ASSIGN_STATE_SET(
          csi, AVND_COMP_CSI_ASSIGN_STATE_REMOVING);
    } else {
      m_AVND_COMP_ALL_CSI_SET(comp);
      for (curr_csi = m_AVND_CSI_REC_FROM_COMP_DLL_NODE_GET(
               m_NCS_DBLIST_FIND_FIRST(&comp->csi_list));
           curr_csi; curr_csi = m_AVND_CSI_REC_FROM_COMP_DLL_NODE_GET(
                         m_NCS_DBLIST_FIND_NEXT(&curr_csi->comp_dll_node))) {
        /* after restart we should go ahead with next csi, so mark the curr_csi
         * as assigning */
        m_AVND_COMP_CSI_CURR_ASSIGN_STATE_SET(
            curr_csi, AVND_COMP_CSI_ASSIGN_STATE_REMOVING);
      } /* for */
    }

    rc = avnd_comp_csi_remove_done(cb, comp, csi);
    goto done;
  }

  /* initiate csi removal from pi comp */
  if (m_AVND_COMP_TYPE_IS_PREINSTANTIABLE(comp)) {
    /* remove all csis in one shot */
    if (!csi) {
      m_AVND_COMP_ALL_CSI_SET(comp);
      /* mark the curr assigned csis removing */
      for (curr_csi = m_AVND_CSI_REC_FROM_COMP_DLL_NODE_GET(
               m_NCS_DBLIST_FIND_FIRST(&comp->csi_list));
           curr_csi; curr_csi = m_AVND_CSI_REC_FROM_COMP_DLL_NODE_GET(
                         m_NCS_DBLIST_FIND_NEXT(&curr_csi->comp_dll_node))) {
        // silently removing csi, don't issue callback
        if (m_AVND_COMP_CSI_CURR_ASSIGN_STATE_IS_UNASSIGNED(curr_csi)) {
          m_AVND_COMP_CSI_CURR_ASSIGN_STATE_SET(
              curr_csi, AVND_COMP_CSI_ASSIGN_STATE_REMOVING);
          rc = avnd_comp_csi_remove_done(cb, comp, csi);
          goto done;
        } else if ((m_AVND_COMP_CSI_CURR_ASSIGN_STATE_IS_ASSIGNED(curr_csi)) ||
                   (m_AVND_COMP_CSI_PRV_ASSIGN_STATE_IS_ASSIGNED(curr_csi)) ||
                   (m_AVND_COMP_CSI_CURR_ASSIGN_STATE_IS_ASSIGNING(curr_csi))) {
          if (!csi_of_same_si_in_removing_state(curr_csi->comp, curr_csi->si))
            is_assigned = true;
          m_AVND_COMP_CSI_CURR_ASSIGN_STATE_SET(
              curr_csi, AVND_COMP_CSI_ASSIGN_STATE_REMOVING);
        }
      } /* for */

      if (true == is_assigned) {
        /* remove the csis as the comp is aware of atleast one csi */
        rc = avnd_comp_cbk_send(cb, comp, AVSV_AMF_CSI_REM, 0, 0);
        if (NCSCC_RC_SUCCESS != rc) goto done;
      }
    }

    /* remove csis one at a time */
    if (csi) {
      if ((m_AVND_COMP_CSI_CURR_ASSIGN_STATE_IS_ASSIGNED(csi)) ||
          (m_AVND_COMP_CSI_PRV_ASSIGN_STATE_IS_ASSIGNED(csi))) {
        /* remove this csi */
        rc = avnd_comp_cbk_send(cb, comp, AVSV_AMF_CSI_REM, 0, csi);
        m_AVND_COMP_CSI_CURR_ASSIGN_STATE_SET(
            csi, AVND_COMP_CSI_ASSIGN_STATE_REMOVING);
        if (NCSCC_RC_SUCCESS != rc) {
          if (m_AVND_IS_SHUTTING_DOWN(cb)) {
            /* Csi remove failure may be because of component crash during
               shutting down. */
            rc = avnd_comp_csi_remove_done(cb, comp, csi);
            if (NCSCC_RC_SUCCESS != rc) goto done;
          } else
            goto done;
        }

      } else {
        /* generate csi-removed indication */
        rc = avnd_comp_csi_remove_done(cb, comp, csi);
        if (NCSCC_RC_SUCCESS != rc) goto done;
      }
    }
  }

  /* initiate CSI removal for NPI comp */
  if (!m_AVND_COMP_TYPE_IS_PREINSTANTIABLE(comp)) {
    /* get the only csi record */
    curr_csi = csi;
    if (!curr_csi)
      curr_csi = m_AVND_CSI_REC_FROM_COMP_DLL_NODE_GET(
          m_NCS_DBLIST_FIND_FIRST(&comp->csi_list));

    /* mark the csi state removing */
    m_AVND_COMP_CSI_CURR_ASSIGN_STATE_SET(curr_csi,
                                          AVND_COMP_CSI_ASSIGN_STATE_REMOVING);

    if (comp->pres == SA_AMF_PRESENCE_INSTANTIATED) {
      rc = avnd_comp_clc_fsm_run(cb, comp, AVND_COMP_CLC_PRES_FSM_EV_TERM);
    } else {
      /* termination is done in qsd/qsing state */
      rc = avnd_comp_csi_remove_done(cb, comp, (csi) ? csi : 0);
    }
  }

done:
  TRACE_LEAVE2("%u", rc);
  return rc;
}

/****************************************************************************
  Name          : avnd_comp_csi_reassign

  Description   : This routine reassigns all the CSIs in the comp-csi list.
                  It is invoked when the component reinstantiates as a part
                  of component restart recovery.

  Arguments     : cb   - ptr to the AvND control block
                  comp - ptr to the comp

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : This routine is invoked only for PI component.
******************************************************************************/
uint32_t avnd_comp_csi_reassign(AVND_CB *cb, AVND_COMP *comp) {
  AVND_COMP_CSI_REC *curr = 0, *prv = 0;
  uint32_t rc = NCSCC_RC_SUCCESS;
  TRACE_ENTER2("Comp '%s'", comp->name.c_str());

  osafassert(m_AVND_COMP_TYPE_IS_PREINSTANTIABLE(comp));

  /* reassign needs to be done with ADD_ONE flag */
  m_AVND_COMP_ALL_CSI_RESET(comp);

  /* scan the comp-csi list & reassign the csis */
  curr = m_AVND_CSI_REC_FROM_COMP_DLL_NODE_GET(
      m_NCS_DBLIST_FIND_FIRST(&comp->csi_list));
  while (curr) {
    prv = m_AVND_CSI_REC_FROM_COMP_DLL_NODE_GET(
        m_NCS_DBLIST_FIND_PREV(&curr->comp_dll_node));

    /*
     * csi assign state may be one of the following:
     * unassigned -> comp-restart recovery executed even before the csi
     *               is assigned; dont assign it
     * Note:-  (we were assigning it in PRE 06A_BUILD_12_05 Labels).
     * assigning -> comp-restart recovery executed during assignment phase
     *              (most likely csi-set failed); reassign it.
     * assigned -> comp-restart recovery executed after assignment;
     *             reassign it.
     * removing -> comp-restart recovery executed during removal phase (most
     *             likely csi-rem failed); generate csi-done indication.
     * removed -> comp-restart recovery executed after removal phase;
     *            do nothing.
     * unassigned-> means HA state of the SI changed during component restart
     *              recovery. This can occur when SI dependency is configured
     *              and fault of any other component of the same SU leads to
     *              component failover.
     */
    if ((m_AVND_COMP_CSI_CURR_ASSIGN_STATE_IS_ASSIGNING(curr) ||
         m_AVND_COMP_CSI_CURR_ASSIGN_STATE_IS_ASSIGNED(curr) ||
         m_AVND_COMP_CSI_CURR_ASSIGN_STATE_IS_RESTARTING(curr)) ||
        (m_AVND_COMP_CSI_CURR_ASSIGN_STATE_IS_UNASSIGNED(curr) &&
         (SA_AMF_HA_QUIESCED == curr->si->curr_state))) {
      /* mark the csi state assigning */
      m_AVND_COMP_CSI_CURR_ASSIGN_STATE_SET(
          curr, AVND_COMP_CSI_ASSIGN_STATE_ASSIGNING);

      /* invoke the callback */
      rc = avnd_comp_cbk_send(cb, curr->comp, AVSV_AMF_CSI_SET, 0, curr);
    }

    if (m_AVND_COMP_CSI_CURR_ASSIGN_STATE_IS_REMOVING(curr))
      /* generate csi-remove-done event... csi may be deleted */
      rc = avnd_comp_csi_remove_done(cb, comp, curr);

    if (0 == m_AVND_COMPDB_REC_CSI_GET(*comp, curr->name.c_str())) {
      curr = (prv) ? m_AVND_CSI_REC_FROM_COMP_DLL_NODE_GET(
                         m_NCS_DBLIST_FIND_NEXT(&prv->comp_dll_node))
                   : m_AVND_CSI_REC_FROM_COMP_DLL_NODE_GET(
                         m_NCS_DBLIST_FIND_FIRST(&comp->csi_list));
    } else
      curr = m_AVND_CSI_REC_FROM_COMP_DLL_NODE_GET(
          m_NCS_DBLIST_FIND_NEXT(&curr->comp_dll_node));
  } /* while */

  TRACE_LEAVE2("%u", rc);
  return rc;
}

/**
 * Check whether a component is qualified for assignment
 * @param comp
 *
 * @return bool
 */
bool IsCompQualifiedAssignment(const AVND_COMP *comp) {
  bool rc = true;
  TRACE_ENTER2();
  if (m_AVND_COMP_IS_FAILED(comp)) {
    LOG_IN("Ignoring Failed comp:'%s', comp_flag %x", comp->name.c_str(),
           comp->flag);
    rc = false;
  } else if (m_AVND_COMP_TYPE_IS_PREINSTANTIABLE(comp)) {
    if (!m_AVND_COMP_IS_REG(comp) &&
        !m_AVND_COMP_PRES_STATE_IS_ORPHANED(comp)) {
      LOG_IN("Ignoring Unregistered comp:'%s'", comp->name.c_str());
      rc = false;
    } else if (!m_AVND_COMP_PRES_STATE_IS_INSTANTIATED(comp) &&
               comp->su->pres == SA_AMF_PRESENCE_INSTANTIATION_FAILED &&
               !m_AVND_COMP_PRES_STATE_IS_ORPHANED(comp)) {
      LOG_IN(
          "Ignoring comp with invalid presence state:'%s', comp_flag %x, comp_pres=%u, su_pres=%u",
          comp->name.c_str(), comp->flag, comp->pres, comp->su->pres);
      rc = false;
    }
  }
  TRACE_LEAVE2("rc:%u", rc);
  return rc;
}

/**
 * Are all CSIs at the specified rank assigned for this SI?
 * @param si
 * @param rank
 *
 * @return bool
 */
static bool all_csis_at_rank_assigned(struct avnd_su_si_rec *si,
                                      uint32_t rank) {
  AVND_COMP_CSI_REC *csi;
  TRACE_ENTER2("'%s'rank=%u", si->name.c_str(), rank);

  for (csi = (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_FIRST(&si->csi_list);
       csi != nullptr;
       csi = (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_NEXT(&csi->si_dll_node)) {
    if ((csi->rank == rank) &&
        (csi->curr_assign_state != AVND_COMP_CSI_ASSIGN_STATE_ASSIGNED)) {
      /* Ignore unqualified comp. */
      if (!m_AVND_SU_IS_RESTART(csi->comp->su) &&
          !IsCompQualifiedAssignment(csi->comp)) {
        LOG_IN("Ignoring comp'%s' which is not qualified for assignment",
               csi->comp->name.c_str());
      } else if (m_AVND_COMP_TYPE_IS_PREINSTANTIABLE(csi->comp) &&
                 csi->suspending_assignment) {
        LOG_IN("Ignoring quiescing/quiesced assignment on unassigned comp'%s'",
               csi->comp->name.c_str());
      } else {
        TRACE_LEAVE2("false");
        return false;
      }
    }
  }

  TRACE_LEAVE2("true");
  return true;
}

/**
 * Assign all CSIs at the specified rank for the SI
 * @param si
 * @param rank
 * @param single_csi
 *
 * @return int NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE
 */
static int assign_all_csis_at_rank(struct avnd_su_si_rec *si, uint32_t rank,
                                   bool single_csi) {
  AVND_COMP_CSI_REC *csi;
  uint32_t rc = NCSCC_RC_FAILURE;

  TRACE_ENTER2("'%s' rank=%u", si->name.c_str(), rank);

  for (csi = (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_FIRST(&si->csi_list);
       csi != nullptr;
       csi = (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_NEXT(&csi->si_dll_node)) {
    if (csi->rank == rank &&
        (csi->si->curr_assign_state != AVND_SU_SI_ASSIGN_STATE_ASSIGNED) &&
        (csi->si->curr_assign_state != AVND_SU_SI_ASSIGN_STATE_REMOVED)) {
      rc = avnd_comp_csi_assign(avnd_cb, csi->comp,
                                (single_csi) ? csi : nullptr);
      if (NCSCC_RC_SUCCESS != rc) break;
    }
  }

  TRACE_LEAVE();
  return rc;
}

/**
 * Find any CSI at the specified rank
 * @param si
 * @param rank
 *
 * @return AVND_COMP_CSI_REC*
 */
static AVND_COMP_CSI_REC *find_unassigned_csi_at_rank(struct avnd_su_si_rec *si,
                                                      uint32_t rank) {
  AVND_COMP_CSI_REC *csi = nullptr;

  for (csi = (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_FIRST(&si->csi_list);
       csi != nullptr;
       csi = (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_NEXT(&csi->si_dll_node)) {
    if ((csi->rank == rank) &&
        (csi->curr_assign_state == AVND_COMP_CSI_ASSIGN_STATE_UNASSIGNED))
      return csi;
  }

  return csi;
}

/****************************************************************************
  Name          : avnd_comp_csi_assign_done

  Description   : This routine is invoked to indicate that the CSI state
                  assignment that was pending on a CSI is completed. It is
                  generated when the component informs AMF of successful
                  assignment through saAmfResponse() or CLC response. It
                  picks up the next set of CSIs and the same procedure
                  (assignment) is repeated.

  Arguments     : cb   - ptr to AvND control block
                  comp - ptr to the component
                  csi  - ptr to the csi record (if 0, it indicates that all
                        the csis belonging to a comp are assigned)

  Return Values : NCSCC_RC_SUCCESS / NCSCC_RC_FAILURE

  Notes         : None
******************************************************************************/
uint32_t avnd_comp_csi_assign_done(AVND_CB *cb, AVND_COMP *comp,
                                   AVND_COMP_CSI_REC *csi) {
  AVND_COMP_CSI_REC *curr_csi;
  uint32_t rc = NCSCC_RC_SUCCESS;
  std::string csiname;

  TRACE_ENTER2("'%s', %p", comp->name.c_str(), csi);

  if (csi != nullptr) {
    csi->single_csi_add_rem_in_si = AVSV_SUSI_ACT_BASE;
    curr_csi = csi;
    csiname = csi->name;
  } else {
    curr_csi = m_AVND_CSI_REC_FROM_COMP_DLL_NODE_GET(
        m_NCS_DBLIST_FIND_FIRST(&comp->csi_list));
    csiname = "all CSIs";
  }

  LOG_IN("Assigned '%s' %s to '%s'", csiname.c_str(),
         ha_state[curr_csi->si->curr_state], comp->name.c_str());

  /*
   * csi-done indication is only generated for pi su..
   * for npi su, su fsm takes care of the individual csi assignments.
   */
  osafassert(m_AVND_SU_IS_PREINSTANTIABLE(comp->su));

  /* delete any pending cbk rec for csi assignment / removal */
  avnd_comp_cbq_csi_rec_del(cb, comp, (csi) ? csi->name : "");

  if (!csi && m_AVND_COMP_IS_ALL_CSI(comp)) {
    m_AVND_COMP_ALL_CSI_RESET(comp);
  }
  /* mark the csi(s) assigned */
  if (csi) {
    if (m_AVND_COMP_CSI_CURR_ASSIGN_STATE_IS_ASSIGNING(csi) ||
        m_AVND_COMP_CSI_CURR_ASSIGN_STATE_IS_RESTARTING(csi)) {
      m_AVND_COMP_CSI_CURR_ASSIGN_STATE_SET(
          csi, AVND_COMP_CSI_ASSIGN_STATE_ASSIGNED);
    }
  } else {
    for (curr_csi = m_AVND_CSI_REC_FROM_COMP_DLL_NODE_GET(
             m_NCS_DBLIST_FIND_FIRST(&comp->csi_list));
         curr_csi; curr_csi = m_AVND_CSI_REC_FROM_COMP_DLL_NODE_GET(
                       m_NCS_DBLIST_FIND_NEXT(&curr_csi->comp_dll_node))) {
      if (m_AVND_COMP_CSI_CURR_ASSIGN_STATE_IS_ASSIGNING(curr_csi)) {
        m_AVND_COMP_CSI_CURR_ASSIGN_STATE_SET(
            curr_csi, AVND_COMP_CSI_ASSIGN_STATE_ASSIGNED);
      }
    } /* for */
  }

  /*
   * csi-done indication may be generated for already assigned si.
   * This happens when comp-csis are reassigned as a part of comp-restart
   * recovery. Ignore such indications.
   */
  if (csi && m_AVND_SU_SI_CURR_ASSIGN_STATE_IS_ASSIGNED(csi->si)) {
    TRACE("We have already send response");
    goto done;
  }

  /*
   * pick up the next/prv csi(s) & assign
   */
  if (csi) {
    if (all_csis_at_rank_assigned(csi->si, csi->rank)) {
      uint32_t rank = (SA_AMF_HA_ACTIVE == csi->si->curr_state ||
                       SA_AMF_HA_STANDBY == csi->si->curr_state)
                          ? csi->rank + 1
                          : csi->rank - 1;

      if (find_unassigned_csi_at_rank(csi->si, rank) != nullptr) {
        rc = assign_all_csis_at_rank(csi->si, rank, true);
      } else if (csi->pending_removal == false) {
        /* all csis belonging to the si are assigned */
        rc = avnd_su_si_oper_done(
            cb, comp->su, m_AVND_SU_IS_ALL_SI(comp->su) ? nullptr : csi->si);
      }
    }

    /*
       If this CSI was in assigning state due to component restart and that time
       removal request came from AMFD, AMFND waits for the completion of
       assignment. After successful assignment, AMFND will start removal of CSI.
     */
    if ((csi->pending_removal == true) &&
        (csi->si->curr_assign_state == AVND_SU_SI_ASSIGN_STATE_REMOVING)) {
      rc = avnd_comp_csi_remove(cb, csi->comp, csi);
      csi->pending_removal = false;
    }
  } else { /* assign all the csis belonging to the next rank in one shot */
    /* get the first csi-record for this comp */
    curr_csi = m_AVND_CSI_REC_FROM_COMP_DLL_NODE_GET(
        m_NCS_DBLIST_FIND_FIRST(&comp->csi_list));

    if (all_csis_at_rank_assigned(curr_csi->si, curr_csi->rank)) {
      uint32_t rank = (SA_AMF_HA_ACTIVE == curr_csi->si->curr_state ||
                       SA_AMF_HA_STANDBY == curr_csi->si->curr_state)
                          ? curr_csi->rank + 1
                          : curr_csi->rank - 1;

      if (find_unassigned_csi_at_rank(curr_csi->si, rank) != nullptr) {
        rc = assign_all_csis_at_rank(curr_csi->si, rank, false);
      } else {
        /* all csis belonging to the si are assigned */
        rc = avnd_su_si_oper_done(cb, comp->su, nullptr);
      }
    }
  }

done:
  TRACE_LEAVE2("%u", rc);
  return rc;
}

static bool all_csis_in_si_removed(const AVND_SU_SI_REC *si) {
  AVND_COMP_CSI_REC *csi;
  for (csi = (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_FIRST(&si->csi_list); csi;
       csi = (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_NEXT(&csi->si_dll_node)) {
    if (csi->curr_assign_state != AVND_COMP_CSI_ASSIGN_STATE_REMOVED)
      return false;
  }
  return true;
}
/**
 * @brief       Checks if all csis of all the sis in this su are in removed
 *state
 *
 * @param [in]  cmp
 *
 * @returns     true/false
 **/
bool all_csis_in_removed_state(const AVND_SU *su) {
  AVND_COMP_CSI_REC *curr_csi;
  AVND_SU_SI_REC *curr_si;
  bool all_csi_removed = true;
  TRACE_ENTER2("'%s'", su->name.c_str());

  for (curr_si = (AVND_SU_SI_REC *)m_NCS_DBLIST_FIND_FIRST(&su->si_list);
       curr_si && all_csi_removed;
       curr_si =
           (AVND_SU_SI_REC *)m_NCS_DBLIST_FIND_NEXT(&curr_si->su_dll_node)) {
    for (curr_csi =
             (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_FIRST(&curr_si->csi_list);
         curr_csi; curr_csi = (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_NEXT(
                       &curr_csi->si_dll_node)) {
      if (!m_AVND_COMP_CSI_CURR_ASSIGN_STATE_IS_REMOVED(curr_csi)) {
        all_csi_removed = false;
        break;
      }
    }
  }
  TRACE_LEAVE2("%u", all_csi_removed);
  return all_csi_removed;
}
/****************************************************************************
  Name          : avnd_comp_csi_remove_done

  Description   : This routine is invoked to indicate that the CSI state
                  removal that was pending on a CSI is completed. It is
                  generated when the component informs AMF of successful
                  assignment through saAmfResponse() or CLC response. It
                  picks up the next set of CSIs and the same procedure
                  (removal) is repeated.

  Arguments     : cb   - ptr to AvND control block
                  comp - ptr to the component
                  csi  - ptr to the csi record (if 0, it indicates that all
                        the csis belonging to a comp are removed)

  Return Values : NCSCC_RC_SUCCESS / NCSCC_RC_FAILURE

  Notes         : None
******************************************************************************/
uint32_t avnd_comp_csi_remove_done(AVND_CB *cb, AVND_COMP *comp,
                                   AVND_COMP_CSI_REC *csi) {
  AVND_COMP_CSI_REC *curr_csi = 0;
  uint32_t rc = NCSCC_RC_SUCCESS;
  const std::string csiname = csi ? csi->name : "all CSIs";

  TRACE_ENTER2("'%s' '%s'", comp->name.c_str(),
               csi ? csi->name.c_str() : nullptr);
  LOG_IN("Removed '%s' from '%s'", csiname.c_str(), comp->name.c_str());

  /*
   * csi-remove indication is only generated for pi su..
   * for npi su, su fsm takes care of the individual csi removal.
   */
  osafassert(m_AVND_SU_IS_PREINSTANTIABLE(comp->su));

  /* ok, time to reset CSi_ALL flag */
  if (!csi && m_AVND_COMP_IS_ALL_CSI(comp)) {
    m_AVND_COMP_ALL_CSI_RESET(comp);
  }

  /* mark the csi(s) removed */
  if (csi) {
    if (m_AVND_COMP_CSI_CURR_ASSIGN_STATE_IS_REMOVING(csi)) {
      m_AVND_COMP_CSI_CURR_ASSIGN_STATE_SET(csi,
                                            AVND_COMP_CSI_ASSIGN_STATE_REMOVED);
    }
  } else {
    for (curr_csi = m_AVND_CSI_REC_FROM_COMP_DLL_NODE_GET(
             m_NCS_DBLIST_FIND_FIRST(&comp->csi_list));
         curr_csi; curr_csi = m_AVND_CSI_REC_FROM_COMP_DLL_NODE_GET(
                       m_NCS_DBLIST_FIND_NEXT(&curr_csi->comp_dll_node))) {
      if (m_AVND_COMP_CSI_CURR_ASSIGN_STATE_IS_REMOVING(curr_csi)) {
        m_AVND_COMP_CSI_CURR_ASSIGN_STATE_SET(
            curr_csi, AVND_COMP_CSI_ASSIGN_STATE_REMOVED);
      }
    } /* for */
  }

  /*
   * pick up the prv csi(s) & remove
   */
  if (csi) {
    if (AVSV_SUSI_ACT_DEL == csi->single_csi_add_rem_in_si) {
      avnd_comp_cbq_csi_rec_del(cb, comp, csi->name);
      /* csi belonging to the si are removed */
      rc = avnd_su_si_oper_done(cb, comp->su, csi->si);

      if (NCSCC_RC_SUCCESS != rc) goto done;
    } else {
      /* Delete cbk info if csi is not ASSIGNING state, @pending_removal will be
       * true */
      if (m_AVND_COMP_CSI_CURR_ASSIGN_STATE_IS_ASSIGNING(csi) == false) {
        avnd_comp_cbq_csi_rec_del(cb, comp, csi->name);
      }

      for (curr_csi =
               (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_LAST(&csi->si->csi_list);
           curr_csi; curr_csi = (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_PREV(
                         &curr_csi->si_dll_node)) {
        if (m_AVND_COMP_CSI_CURR_ASSIGN_STATE_IS_REMOVED(curr_csi))
          continue;
        else if (m_AVND_COMP_CSI_CURR_ASSIGN_STATE_IS_REMOVING(curr_csi))
          break;
        else if (m_AVND_COMP_CSI_CURR_ASSIGN_STATE_IS_ASSIGNING(curr_csi)) {
          LOG_WA("'%s' is getting assigned, remove it after assignment",
                 curr_csi->name.c_str());
          curr_csi->pending_removal = true;
          break;
        } else {
          rc = avnd_comp_csi_remove(cb, curr_csi->comp, curr_csi);
          break;
        }
      }

      /* all csis belonging to the si are removed */
      if ((all_csis_in_si_removed(csi->si) == true) &&
          (m_AVND_SU_SI_CURR_ASSIGN_STATE_IS_REMOVING(csi->si)))
        rc = avnd_su_si_oper_done(
            cb, comp->su, m_AVND_SU_IS_ALL_SI(comp->su) ? nullptr : csi->si);
    }
  } else {
    avnd_comp_cbq_csi_rec_del(cb, comp, "");
    /* Issue remove callback with TARGET_ALL for CSIs belonging to prv rank.*/
    for (curr_csi = m_AVND_CSI_REC_FROM_COMP_DLL_NODE_GET(
             m_NCS_DBLIST_FIND_FIRST(&comp->csi_list));
         curr_csi; curr_csi = m_AVND_CSI_REC_FROM_COMP_DLL_NODE_GET(
                       m_NCS_DBLIST_FIND_NEXT(&curr_csi->comp_dll_node))) {
      for (AVND_COMP_CSI_REC *csi = (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_PREV(
               &curr_csi->si_dll_node);
           csi; csi = (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_PREV(
                    &csi->si_dll_node)) {
        if (m_AVND_COMP_CSI_CURR_ASSIGN_STATE_IS_REMOVED(csi))
          continue;
        else {
          /* remove all the csis belonging to this comp */
          rc = avnd_comp_csi_remove(cb, csi->comp, 0);
          break;
        }
      }

      /* When AMFND responds to AMFD for removal of assignments for the SIs in
         any SU, it also deletes all the SUSI and COMPCSI records. In such a
         case component will not have any CSI in its csi_list. If removal is
         completed break the loop.
       */
      if (comp->csi_list.n_nodes == 0) break;
    }

    /* This is removal with TARGET_ALL. So if all CSIs in all SIs of SU are
       moved to removed state, mark all SIs removed and inform AMF director.*/
    if (all_csis_in_removed_state(comp->su) && m_AVND_SU_IS_ALL_SI(comp->su)) {
      rc = avnd_su_si_oper_done(cb, comp->su, 0);
    }
  }

done:
  TRACE_LEAVE2("%u", rc);
  return rc;
}

/****************************************************************************
  Name          : avnd_comp_curr_info_del

  Description   : This routine deletes the dynamic info associated with this
                  component. If the component is marked failed, the error
                  escalation parameters & oper state is retained.

  Arguments     : cb   - ptr to the AvND control block
                  comp - ptr to the comp

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : CSIs associated with this component are not deleted.
                  The CLC parameters (instantiate-params, comp-reg params &
                  terminate cbk) deletion is handled as a part of component
                  FSM.
******************************************************************************/
uint32_t avnd_comp_curr_info_del(AVND_CB *cb, AVND_COMP *comp) {
  AVND_COMP_CSI_REC *curr_csi = 0;
  uint32_t rc = NCSCC_RC_SUCCESS;
  TRACE_ENTER2("Comp '%s'", comp->name.c_str());

  /* unmark the previous csi assigned state of this compoonent */
  for (curr_csi = m_AVND_CSI_REC_FROM_COMP_DLL_NODE_GET(
           m_NCS_DBLIST_FIND_FIRST(&comp->csi_list));
       curr_csi; curr_csi = m_AVND_CSI_REC_FROM_COMP_DLL_NODE_GET(
                     m_NCS_DBLIST_FIND_NEXT(&curr_csi->comp_dll_node))) {
    m_AVND_COMP_CSI_PRV_ASSIGN_STATE_SET(curr_csi,
                                         AVND_COMP_CSI_ASSIGN_STATE_UNASSIGNED);
  } /* for */

  /* reset err-esc param & oper state (if comp & su are healthy) */
  if (!m_AVND_COMP_IS_FAILED(comp) && !m_AVND_SU_IS_FAILED(comp->su)) {
    /* reset err params */
    comp->err_info.src = static_cast<AVND_ERR_SRC>(0);
    comp->err_info.detect_time = 0;
    comp->err_info.restart_cnt = 0;

    /* disable the oper state (if pi comp) */
    if (m_AVND_COMP_TYPE_IS_PREINSTANTIABLE(comp) &&
        (m_AVND_COMP_PRES_STATE_IS_INSTANTIATIONFAILED(comp) ||
         m_AVND_COMP_PRES_STATE_IS_TERMINATIONFAILED(comp))) {
      m_AVND_COMP_OPER_STATE_SET(comp, SA_AMF_OPERATIONAL_DISABLED);
      rc = avnd_comp_oper_state_avd_sync(cb, comp);
      if (NCSCC_RC_SUCCESS != rc) goto done;
    }
  }

  /* reset retry count */
  comp->clc_info.inst_retry_cnt = 0;
  comp->clc_info.am_start_retry_cnt = 0;

  /* Stop the qscing complete timer if started any */
  if (m_AVND_TMR_IS_ACTIVE(comp->qscing_tmr)) {
    m_AVND_TMR_COMP_QSCING_CMPL_STOP(cb, comp);
  }

  /* delete hc-list, cbk-list, pg-list & pm-list */
  avnd_comp_hc_rec_del_all(cb, comp);
  avnd_comp_cbq_del(cb, comp, true);

  /* re-using the funtion to stop all PM started by this comp */
  avnd_comp_pm_finalize(cb, comp, comp->reg_hdl);
  avnd_comp_pm_rec_del_all(cb, comp); /*if at all anythnig is left behind */

done:
  TRACE_LEAVE2("%u", rc);
  return rc;
}

/**
 * @brief Prepares CSI attribute change callback message for amfa with updated
 * values. Values are updated in csi_rec when AMFND gets it from AMFD.
 *
 * @param  cbk_info (ptr to AVSV_AMF_CBK_INFO)
 * @param  comp (ptr to AVND_COMP)
 * @param  csi_rec(ptr to AVND_COMP_CSI_REC)
 *
 * @return true/false
 */
static void set_params_for_csi_attr_change_cbk(AVSV_AMF_CBK_INFO *cbk_info,
                                               AVND_COMP *comp,
                                               AVND_COMP_CSI_REC *csi_rec) {
  AVSV_CSI_ATTRS attr;
  SaNameT csi_name;

  /* copy the attributes */
  memset(&attr, 0, sizeof(AVSV_CSI_ATTRS));
  if (csi_rec->attrs.number != 0) {
    attr.list = static_cast<AVSV_ATTR_NAME_VAL *>(
        calloc(csi_rec->attrs.number, sizeof(AVSV_ATTR_NAME_VAL)));
    osafassert(attr.list != nullptr);
    amfnd_copy_csi_attrs(&csi_rec->attrs, &attr);
    attr.number = csi_rec->attrs.number;
  }
  /* fill the callback params */
  cbk_info->type = AVSV_AMF_CSI_ATTR_CHANGE;
  osaf_extended_name_alloc(csi_rec->name.c_str(), &csi_name);
  cbk_info->param.csi_attr_change.csi_name = csi_name;
  cbk_info->param.csi_attr_change.attrs = attr;
  /* reset the attr */
  attr.number = 0;
  attr.list = 0;
}

/****************************************************************************
  Name          : avnd_comp_cbk_send

  Description   : This is a top-level routine that is used to send the
                  callback parameters to the component.

  Arguments     : cb        - ptr to the AvND control block
                  comp      - ptr to the comp
                  type      - callback type
                  hc_rec    - ptr to the healthcheck record (0 for non hc cbk)
                  csi_rec   - ptr to csi record (0 for non csi cbk /
                              csi cbk with target-all flag set)

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE.

  Notes         : None.
******************************************************************************/
uint32_t avnd_comp_cbk_send(AVND_CB *cb, AVND_COMP *comp,
                            AVSV_AMF_CBK_TYPE type, AVND_COMP_HC_REC *hc_rec,
                            AVND_COMP_CSI_REC *csi_rec) {
  SaAmfCSIDescriptorT csi_desc;
  SaAmfCSIFlagsT csi_flag;
  std::string csi_name;
  AVSV_AMF_CBK_INFO *cbk_info = 0;
  AVND_COMP_CSI_REC *curr_csi = 0;
  AVSV_CSI_ATTRS attr;
  MDS_DEST *dest = 0;
  SaAmfHandleT hdl = 0;
  SaTimeT per = 0;
  uint32_t rc = NCSCC_RC_SUCCESS;

  TRACE_ENTER2("'%s' %u", comp->name.c_str(), type);
  /*
   * callbacks are sent only to registered comps (healtcheck
   * cbk is an exception)
   */
  if ((AVSV_AMF_HC != type) && !m_AVND_COMP_IS_REG(comp) &&
      !m_AVND_COMP_PRES_STATE_IS_ORPHANED(comp))
    goto done;

  /* allocate cbk-info memory */
  cbk_info =
      static_cast<AVSV_AMF_CBK_INFO *>(calloc(1, sizeof(AVSV_AMF_CBK_INFO)));

  /* fill the callback params */
  switch (type) {
    case AVSV_AMF_HC:
      avnd_amf_hc_cbk_fill(cbk_info, comp->name, hc_rec->key);
      per = hc_rec->max_dur;
      break;

    case AVSV_AMF_COMP_TERM:
      avnd_amf_comp_term_cbk_fill(cbk_info, comp->name);
      per = comp->term_cbk_timeout;
      break;

    case AVSV_AMF_CSI_SET: {
      curr_csi = (csi_rec) ? csi_rec
                           : m_AVND_CSI_REC_FROM_COMP_DLL_NODE_GET(
                                 m_NCS_DBLIST_FIND_FIRST(&comp->csi_list));
      if (!curr_csi) {
        rc = NCSCC_RC_FAILURE;
        goto done;
      }

      /*
       * Populate the csi-desc structure.
       */
      memset(&csi_desc, 0, sizeof(SaAmfCSIDescriptorT));

      csi_desc.csiFlags = (!csi_rec) ? SA_AMF_CSI_TARGET_ALL
                                     : ((csi_rec->prv_assign_state == 0) ||
                                        (csi_rec->prv_assign_state ==
                                         AVND_COMP_CSI_ASSIGN_STATE_UNASSIGNED))
                                           ? SA_AMF_CSI_ADD_ONE
                                           : SA_AMF_CSI_TARGET_ONE;

      if (csi_rec)
        osaf_extended_name_alloc(csi_rec->name.c_str(), &csi_desc.csiName);

      /* for proxied non-preinstantiable components, we have only one csi and
         the only possible ha state is active. */
      if (m_AVND_COMP_TYPE_IS_PROXIED(comp) &&
          !m_AVND_COMP_TYPE_IS_PREINSTANTIABLE(comp)) {
        /* populate the params for the lone csi */
        csi_desc.csiFlags = SA_AMF_CSI_ADD_ONE;
        osaf_extended_name_alloc(curr_csi->name.c_str(), &csi_desc.csiName);
      }

      if (SA_AMF_HA_ACTIVE == curr_csi->si->curr_state) {
        csi_desc.csiStateDescriptor.activeDescriptor.transitionDescriptor =
            curr_csi->trans_desc;

        if (SA_AMF_CSI_NEW_ASSIGN !=
            csi_desc.csiStateDescriptor.activeDescriptor.transitionDescriptor)
          osaf_extended_name_alloc(
              curr_csi->act_comp_name.c_str(),
              &csi_desc.csiStateDescriptor.activeDescriptor.activeCompName);
      }

      if (SA_AMF_HA_STANDBY == curr_csi->si->curr_state) {
        osaf_extended_name_alloc(
            curr_csi->act_comp_name.c_str(),
            &csi_desc.csiStateDescriptor.standbyDescriptor.activeCompName);

        csi_desc.csiStateDescriptor.standbyDescriptor.standbyRank =
            curr_csi->standby_rank;
      }

      /* copy the attributes */
      memset(&attr, 0, sizeof(AVSV_CSI_ATTRS));
      if ((SA_AMF_CSI_ADD_ONE == csi_desc.csiFlags) &&
          (curr_csi->attrs.number != 0)) {
        attr.list = static_cast<AVSV_ATTR_NAME_VAL *>(
            calloc(curr_csi->attrs.number, sizeof(AVSV_ATTR_NAME_VAL)));
        osafassert(attr.list != nullptr);
        amfnd_copy_csi_attrs(&csi_rec->attrs, &attr);
        attr.number = curr_csi->attrs.number;
      }

      /* fill the callback params */
      avnd_amf_csi_set_cbk_fill(cbk_info, comp->name, curr_csi->si->curr_state,
                                csi_desc, attr);

      /* reset the attr */
      attr.number = 0;
      attr.list = 0;
      per = comp->csi_set_cbk_timeout;
    } break;

    case AVSV_AMF_CSI_REM: {
      curr_csi = (csi_rec) ? csi_rec
                           : m_AVND_CSI_REC_FROM_COMP_DLL_NODE_GET(
                                 m_NCS_DBLIST_FIND_FIRST(&comp->csi_list));

      /* determine csi-flag */
      csi_flag = (csi_rec) ? SA_AMF_CSI_TARGET_ONE : SA_AMF_CSI_TARGET_ALL;

      /* determine csi name */
      if (SA_AMF_CSI_TARGET_ALL != csi_flag)
        csi_name = csi_rec->name.c_str();
      else
        csi_name = "";

      /* fill the callback params */
      avnd_amf_csi_rem_cbk_fill(cbk_info, comp->name, csi_name, csi_flag);
      per = comp->csi_rmv_cbk_timeout;
    } break;

    case AVSV_AMF_PXIED_COMP_INST:
      avnd_amf_pxied_comp_inst_cbk_fill(cbk_info, comp->name);
      per = comp->pxied_inst_cbk_timeout;
      break;
    case AVSV_AMF_PXIED_COMP_CLEAN:
      avnd_amf_pxied_comp_clean_cbk_fill(cbk_info, comp->name);
      per = comp->pxied_clean_cbk_timeout;
      break;
    case AVSV_AMF_CSI_ATTR_CHANGE:
      set_params_for_csi_attr_change_cbk(cbk_info, comp, csi_rec);
      per = comp->csi_set_cbk_timeout;
      break;
    case AVSV_AMF_PG_TRACK:
    default:
      osafassert(0);
  }

  /* determine the send params (mds-dest, hdl & per) */
  if (hc_rec) {
    dest = &hc_rec->dest;
    hdl = hc_rec->req_hdl;
  }

  /* send the callbk */
  rc = avnd_comp_cbq_send(cb, comp, dest, hdl, cbk_info, per);

done:
  if ((NCSCC_RC_SUCCESS != rc) && cbk_info) avsv_amf_cbk_free(cbk_info);

  TRACE_LEAVE2("%u", rc);
  return rc;
}

/****************************************************************************
  Name          : avnd_amf_resp_send

  Description   : This routine sends the response to an AMF API. The response
                  may indicate whether the API was processed properly. It
                  also sends the value in response to a get-operation.

  Arguments     : cb      - ptr to the AvND control block
                  type    - API type
                  amf_rc  - API processing status
                  get_val - value for a get operation
                  dest    - ptr to the mds-dest
                  ctxt    - mds context on which AvA is waiting
                  comp    - Ptr to the component structure, used in reg resp
                            only needed when msg has to be forwarded to AvND
                            i.e. when msg_to_avnd is true. Else nullptr
                  msg_to_avnd - If the req msg has come from AvND then
                                the resp should go to AvND.

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avnd_amf_resp_send(AVND_CB *cb, AVSV_AMF_API_TYPE type,
                            SaAisErrorT amf_rc, uint8_t *get_val,
                            MDS_DEST *dest, MDS_SYNC_SND_CTXT *ctxt,
                            AVND_COMP *comp, bool msg_to_avnd) {
  AVND_MSG msg;
  AVSV_ND2ND_AVND_MSG *avnd_msg;
  uint32_t rc = NCSCC_RC_SUCCESS;
  MDS_DEST i_to_dest;
  AVSV_NDA_AVA_MSG *temp_ptr = nullptr;
  NODE_ID node_id = 0;
  MDS_SYNC_SND_CTXT temp_ctxt;
  TRACE_ENTER();

  /* Check if the response has to be sent or not. */
  memset(&temp_ctxt, 0, sizeof(MDS_SYNC_SND_CTXT));
  if (0 == memcmp(ctxt, &temp_ctxt, sizeof(MDS_SYNC_SND_CTXT))) {
    /* This means that the response is not supposed to be sent. */
    return rc;
  }

  memset(&msg, 0, sizeof(AVND_MSG));

  msg.info.ava =
      static_cast<AVSV_NDA_AVA_MSG *>(calloc(1, sizeof(AVSV_NDA_AVA_MSG)));

  /* populate the response */
  if (AVSV_AMF_HA_STATE_GET == type) {
    m_AVND_AMF_HA_STATE_GET_RSP_MSG_FILL(
        msg, (get_val) ? *((SaAmfHAStateT *)get_val) : 0, amf_rc);
  } else
    m_AVND_AMF_API_RSP_MSG_FILL(msg, type, amf_rc);

  if (true == msg_to_avnd) {
    /* Fill informations.  */
    avnd_msg = static_cast<AVSV_ND2ND_AVND_MSG *>(
        calloc(1, sizeof(AVSV_ND2ND_AVND_MSG)));

    osaf_extended_name_alloc(comp->name.c_str(), &avnd_msg->comp_name);
    avnd_msg->mds_ctxt = *ctxt;
    temp_ptr = msg.info.ava;
    msg.info.avnd = avnd_msg;
    /* Hijack the message of AvA and make it a part of AvND. */
    msg.info.avnd->info.msg = temp_ptr;
    msg.info.avnd->type = AVND_AVND_AVA_MSG;
    msg.type = AVND_MSG_AVND;
    /* Send it to AvND */
    node_id = m_NCS_NODE_ID_FROM_MDS_DEST(*dest);
    i_to_dest = avnd_get_mds_dest_from_nodeid(cb, node_id);
    rc = avnd_avnd_mds_send(cb, i_to_dest, &msg);
  } else {
    /* now send the response */
    rc = avnd_mds_send(cb, &msg, dest, ctxt);
    if (NCSCC_RC_SUCCESS == rc) msg.info.ava = 0;
  }

  /* free the contents of avnd message */
  avnd_msg_content_free(cb, &msg);
  TRACE_LEAVE();
  return rc;
}

/****************************************************************************
  Name          : avnd_pxied_list_init

  Description   : This routine initializes the pxied_list.

  Arguments     : cb  - ptr to the AvND control block

  Return Values : None

  Notes         : This list is maintained in AVND_COMP structure. Each element
                  of this list represent a proxied of this proxy component and
                  will contain a pointer to the proxied component. Key for this
                  list is component name stored in AVND_COMP structure of pxied.
******************************************************************************/
void avnd_pxied_list_init(AVND_COMP *comp) {
  NCS_DB_LINK_LIST *pxied_list = &comp->pxied_list;

  /* initialize the pm_list dll  */
  pxied_list->order = NCS_DBLIST_ANY_ORDER;
  pxied_list->cmp_cookie = avsv_dblist_sastring_cmp;
  pxied_list->free_cookie = 0;
}

/****************************************************************************
  Name          : avnd_comp_proxied_add

  Description   : This routine add an element to proxied list and is called
                  when a proxied component is registered. This routine will
                  mark a component as proxy when it registers a proxied for
                  the first time. It also updates the back pointer to proxy.

  Arguments     : cb   - ptr to the AvND control block
                  comp - ptr to the registering comp
                  pxy_comp - ptr to the proxy of reg comp
                  avd_upd_needed - avd update message tobe sent

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE/NCSCC_RC_OUT_OF_MEM

  Notes         : None.
******************************************************************************/
uint32_t avnd_comp_proxied_add(AVND_CB *cb, AVND_COMP *comp,
                               AVND_COMP *pxy_comp, bool avd_upd_needed) {
  AVND_COMP_PXIED_REC *rec;
  AVSV_PARAM_INFO param;
  TRACE_ENTER2("'%s' : '%s'", comp->name.c_str(), pxy_comp->name.c_str());

  /* allocate memory for rec** */
  rec = new AVND_COMP_PXIED_REC();

  /* fill the params */
  rec->pxied_comp = comp;
  rec->comp_dll_node.key = (uint8_t *)comp->name.c_str();

  /* add rec to link list */
  uint32_t rc =
      ncs_db_link_list_add(&pxy_comp->pxied_list, &rec->comp_dll_node);
  if (NCSCC_RC_SUCCESS != rc) goto done;

  /* set the back pointer to proxy for the proxied comp */
  comp->pxy_comp = pxy_comp;
  comp->proxy_comp_name = pxy_comp->name;

  if (true == avd_upd_needed) {
    /* inform avd of the change in proxy */
    memset(&param, 0, sizeof(AVSV_PARAM_INFO));
    param.class_id = AVSV_SA_AMF_COMP;
    param.attr_id = saAmfCompCurrProxyName_ID;
    osaf_extended_name_alloc(comp->name.c_str(), &param.name);
    param.act = AVSV_OBJ_OPR_MOD;
    strncpy(param.value, comp->pxy_comp->name.c_str(),
            AVSV_MISC_STR_MAX_SIZE - 1);
    param.value_len = comp->pxy_comp->name.length();

    rc = avnd_di_object_upd_send(cb, &param);
    if (NCSCC_RC_SUCCESS != rc) goto done;
  }
  /* change status to SA_AMF_PROXY_STATUS_PROXIED */
  m_AVND_COMP_PROXY_STATUS_SET(comp, SA_AMF_PROXY_STATUS_PROXIED);
  rc = avnd_comp_proxy_status_avd_sync(cb, comp);
  if (NCSCC_RC_SUCCESS != rc) goto done;

  /* mark the proxy component as proxy */
  m_AVND_COMP_TYPE_PROXY_SET(pxy_comp);
  return rc;
done:
  /* free mem */
  if (rec) delete rec;
  TRACE_LEAVE2("%u", rc);
  return rc;
}

/****************************************************************************
  Name          : avnd_comp_proxied_del

  Description   : This routine deletes an entry corresponding to a particular
                  proxied specified by the param from the proxied list. This
                  is called as a part of unregistration of the specified proxied
                  component. It marks a proxy component as normal component on
                  deletion of the last entry. It also updates the back ptr to
                  proxy to null.

  Arguments     : cb   - ptr to the AvND control block
                  comp - ptr to the registering comp
                  pxy_comp - ptr to the proxy of reg comp
                  dest - ptr to the mds-dest
                  avd_upd_needed - avd update message tobe sent
                  rec_to_be_deleted - Record to be deleted.

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avnd_comp_proxied_del(AVND_CB *cb, AVND_COMP *comp,
                               AVND_COMP *pxy_comp, bool avd_upd_needed,
                               AVND_COMP_PXIED_REC *rec_to_be_deleted) {
  uint32_t rc = NCSCC_RC_SUCCESS;
  AVND_COMP_PXIED_REC *rec;
  AVSV_PARAM_INFO param;

  TRACE_ENTER2("'%s': nodeid: %u comp_type: %u", comp->name.c_str(),
               comp->node_id, comp->comp_type);
  TRACE("pxy_comp:'%s': nodeid:%u comp_type: %u", pxy_comp->name.c_str(),
        pxy_comp->node_id, pxy_comp->comp_type);

  if (nullptr == rec_to_be_deleted) {
    /* unlink the rec */
    rec = (AVND_COMP_PXIED_REC *)ncs_db_link_list_remove(
        &pxy_comp->pxied_list, (uint8_t *)comp->name.c_str());
  } else
    rec = rec_to_be_deleted;
  /* rec has to be there */
  osafassert(rec);

  /*remove the association between proxy and proxied */
  comp->pxy_comp = 0;
  comp->proxy_comp_name = "";
  /* mark the proxied as unregistered.
     No need to send Async Update here as the same thing happens on the STDBY,
     as avnd_comp_proxied_del is called on STDBY. */
  m_AVND_COMP_REG_RESET(comp);

  /* mark the proxy comp as normal comp, its not proxying anybody */
  if (pxy_comp->pxied_list.n_nodes == 0) {
    m_AVND_COMP_TYPE_PROXY_RESET(pxy_comp);
    /* Check whether it is a local proxy or internode/extnode proxy. */
    if (m_AVND_COMP_TYPE_IS_INTER_NODE(pxy_comp)) {
      /* Since this is an internode proxy component and it is not
         serving any proxied comp, so , better remove this from
         the data base. */
      rc = avnd_internode_comp_del(cb, pxy_comp->name);
    }
  }

  if (true == avd_upd_needed) {
    /* inform avd of the change in proxy */
    memset(&param, 0, sizeof(AVSV_PARAM_INFO));
    param.class_id = AVSV_SA_AMF_COMP;
    param.attr_id = saAmfCompCurrProxyName_ID;
    osaf_extended_name_alloc(comp->name.c_str(), &param.name);
    param.act = AVSV_OBJ_OPR_MOD;
    strcpy(param.value, "");
    param.value_len = 0;

    rc = avnd_di_object_upd_send(cb, &param);
  }
  /* free the rec */
  /*if(rec) */
  delete rec;

  TRACE_LEAVE2("%u", rc);
  return rc;
}

/****************************************************************************
  Name          : avnd_comp_proxy_unreg

  Description   : This routine unregisters all the proxied component of this
                  proxy component.

  Arguments     : cb   - ptr to the AvND control block
                  comp - ptr to the proxy of reg comp

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avnd_comp_proxy_unreg(AVND_CB *cb, AVND_COMP *comp) {
  uint32_t rc = NCSCC_RC_SUCCESS;
  uint32_t rc_send = NCSCC_RC_SUCCESS;
  AVND_COMP_PXIED_REC *rec = 0;
  AVND_COMP *pxd_comp = nullptr;
  TRACE_ENTER2("Comp '%s'", comp->name.c_str());

  /* parse thru all proxied comp of this proxy */
  while (0 != (rec = (AVND_COMP_PXIED_REC *)ncs_db_link_list_pop(
                   &comp->pxied_list))) {
    /*************************   Section  1 Starts Here
     * **************************/
    pxd_comp = rec->pxied_comp;
    /* Check if this component is an internode/ext component. */
    if (m_AVND_COMP_TYPE_IS_INTER_NODE(pxd_comp)) {
      /* This is a case when a proxy component is unregistering the proxied
         component, which is an internode/ext component.
         This will happen in case proxy component has gone down. */
      AVSV_AMF_API_INFO api_info;
      MDS_SYNC_SND_CTXT ctxt;
      SaAisErrorT amf_rc = SA_AIS_OK;
      bool int_ext_comp = false;

      memset(&ctxt, 0, sizeof(MDS_SYNC_SND_CTXT));
      memset(&api_info, 0, sizeof(AVSV_AMF_API_INFO));

      api_info.type = AVSV_AMF_COMP_UNREG;
      api_info.dest = pxd_comp->reg_dest;
      api_info.param.unreg.hdl = pxd_comp->reg_hdl;
      osaf_extended_name_alloc(pxd_comp->name.c_str(),
                               &api_info.param.unreg.comp_name);
      osaf_extended_name_alloc(pxd_comp->pxy_comp->name.c_str(),
                               &api_info.param.unreg.proxy_comp_name);

      rc = avnd_int_ext_comp_hdlr(cb, &api_info, &ctxt, &amf_rc, &int_ext_comp);
      /* Since there is no Mds Context information being sent so, no response
         will come. So, we need to delete the proxied information. */

      if (NCSCC_RC_SUCCESS != rc) {
        LOG_ER(
            "avnd_int_ext_comp_hdlr failed:'%s' Type=%u Hdl=%llx Dest=%" PRId64,
            pxd_comp->name.c_str(), api_info.type, api_info.param.reg.hdl,
            api_info.dest);
        goto err;
      }

      rc = avnd_comp_proxied_del(cb, pxd_comp, pxd_comp->pxy_comp, false, rec);

      if (NCSCC_RC_SUCCESS != rc) {
        LOG_ER(
            "avnd_comp_proxied_del failed:'%s' Type=%u Hdl=%llx Dest=%" PRId64,
            pxd_comp->name.c_str(), api_info.type, api_info.param.reg.hdl,
            api_info.dest);
        goto err;
      }
      rc = avnd_internode_comp_del(cb, pxd_comp->name);

      if (NCSCC_RC_SUCCESS != rc) {
        LOG_ER(
            "avnd_internode_comp_del failed:'%s' Type=%u Hdl=%llx Dest=%" PRId64,
            pxd_comp->name.c_str(), api_info.type, api_info.param.reg.hdl,
            api_info.dest);
        goto err;
      }

    err:
      continue;
    }

    /* if(m_AVND_COMP_TYPE_IS_INTER_NODE)  */
    /*************************   Section  1 Ends Here
     * **************************/
    if (m_AVND_COMP_PRES_STATE_IS_INSTANTIATED(rec->pxied_comp)) {
      m_AVND_COMP_PROXY_STATUS_SET(rec->pxied_comp,
                                   SA_AMF_PROXY_STATUS_UNPROXIED);
      rc_send = avnd_comp_proxy_status_avd_sync(cb, rec->pxied_comp);
    }

    /* process proxied comp unregistration */
    if (m_AVND_COMP_PRES_STATE_IS_INSTANTIATED(rec->pxied_comp))
      rc = avnd_comp_clc_fsm_run(cb, rec->pxied_comp,
                                 AVND_COMP_CLC_PRES_FSM_EV_ORPH);

    osafassert(rec->pxied_comp);

    /* remove the association between proxy and proxied */
    rec->pxied_comp->pxy_comp = 0;

    /* mark the proxied as unregistered */
    m_AVND_COMP_REG_RESET(rec->pxied_comp);

    /* mark the proxy comp as normal comp, its not proxying anybody */
    if (comp->pxied_list.n_nodes != 0) {
      m_AVND_COMP_TYPE_PROXY_RESET(comp);
    }

    /* now free the rec */
    delete rec;
  }

  /* case of fault during avnd_di_object_upd_send*/
  if (rc_send != NCSCC_RC_SUCCESS) rc = rc_send;
  TRACE_LEAVE2("%u", rc);
  return rc;
}

/****************************************************************************
  Name          : avnd_comp_hdl_finalize

  Description   : This routine deletes the hc-list, cbd-list & pm-list which
                  uses the hdl being finalized.

  Arguments     : cb   - ptr to the AvND control block
                  comp - ptr to the proxy of reg comp
                  fin  - ptr to finalize param
                  api_info - ptr to api_info struct

  Return Values : None.

  Notes         : None.
******************************************************************************/

static void avnd_comp_hdl_finalize(AVND_CB *cb, AVND_COMP *comp,
                                   AVSV_AMF_FINALIZE_PARAM *fin,
                                   AVSV_AMF_API_INFO *api_info) {
  avnd_comp_hc_finalize(cb, comp, fin->hdl, &api_info->dest);
  avnd_comp_cbq_finalize(cb, comp, fin->hdl, &api_info->dest);
  avnd_comp_pm_finalize(cb, comp, fin->hdl);
}

/****************************************************************************
  Name          : avnd_comp_cmplete_all_assignment

  Description   : This routine parses thru comp's callback list and marks any
                  pending assignments as done.

  Arguments     : cb   - ptr to the AvND control block
                  comp - ptr to the proxy of reg comp

  Return Values : None.

  Notes         : This function will be called only in error flows and not in
                  normal flows.
                  Here we take care of the following cases
                  1) Target-All & Target-One given for same SI and resp comes
                     for Target-one.
                  2) Target-All Active & Target-All Quiesced are given and
                     resp comes for Tartget-All Active.
                  3) Target-All & Target-One cbk's are present in order and
                     resp comes for Target-All. This will end up deleting
                     Target-One cbk rec, we should not crash in this case also
******************************************************************************/

void avnd_comp_cmplete_all_assignment(AVND_CB *cb, AVND_COMP *comp) {
  AVND_COMP_CBK *cbk = 0, *temp_cbk_list = 0, *head = 0;
  AVND_COMP_CSI_REC *csi = 0, *temp_csi = 0;
  uint32_t rc = NCSCC_RC_SUCCESS, found = 0;
  TRACE_ENTER2("Comp '%s'", comp->name.c_str());

  /*
   *  su-sis may be in assigning/removing state. signal csi
   * assign/remove done so that su-si assignment/removal algo can proceed.
   */
  while ((comp->cbk_list != nullptr) && (comp->cbk_list != cbk)) {
    cbk = comp->cbk_list;

    if (AVSV_AMF_CSI_SET == cbk->cbk_info->type) {
      csi = m_AVND_COMPDB_REC_CSI_GET(
          *comp, Amf::to_string(&cbk->cbk_info->param.csi_set.csi_desc.csiName)
                     .c_str());

      /* check, if the older assignment was overriden by new one */
      if (!csi) {
        /* Say Active-All and Quiesced-all were given and we
         * get a response for Active-all, we need not process it
         */
        temp_csi = m_AVND_COMPDB_REC_CSI_GET_FIRST(*comp);

        if (cbk->cbk_info->param.csi_set.ha != temp_csi->si->curr_state) {
          avnd_comp_cbq_rec_pop_and_del(cb, comp, cbk, true);
          continue;
        }
      } else if (cbk->cbk_info->param.csi_set.ha != csi->si->curr_state) {
        /* if assignment was overriden by new one */
        avnd_comp_cbq_rec_pop_and_del(cb, comp, cbk, true);
        continue;
      } else if (m_AVND_COMP_IS_ALL_CSI(comp)) {
        /* if both target all and target one operation are
         * pending, we need not respond for target one
         */
        avnd_comp_cbq_rec_pop_and_del(cb, comp, cbk, true);
        continue;
      }

      /* finally we mark it done */
      rc = avnd_comp_csi_assign_done(cb, comp, csi);
      if ((!csi) || (NCSCC_RC_SUCCESS != rc)) break;

    } else if (AVSV_AMF_CSI_REM == cbk->cbk_info->type) {
      csi = m_AVND_COMPDB_REC_CSI_GET(
          *comp,
          Amf::to_string(&cbk->cbk_info->param.csi_rem.csi_name).c_str());
      if (comp->csi_list.n_nodes) {
        rc = avnd_comp_csi_remove_done(cb, comp, csi);
        if ((!csi) || (NCSCC_RC_SUCCESS != rc)) break;
      } else {
        avnd_comp_cbq_rec_pop_and_del(cb, comp, cbk, true);
      }
    } else {
      /* pop this rec */
      m_AVND_COMP_CBQ_REC_POP(comp, cbk, found);
      if (!found) {
        LOG_ER("Comp callback record not found: '%s'", comp->name.c_str());
        break;
      }
      cbk->next = nullptr;

      /*  add this rec on to temp_cbk_list */
      if (head == 0) {
        head = cbk;
        temp_cbk_list = cbk;
      } else {
        temp_cbk_list->next = cbk;
        temp_cbk_list = cbk;
      }

    } /* else */
  }   /* while */

  if (head != nullptr) {
    /* copy other callback's back to cbk_list */
    temp_cbk_list->next = comp->cbk_list;
    comp->cbk_list = head;
  }
  TRACE_LEAVE();
}

/****************************************************************************
  Name          : avnd_comp_cmplete_all_csi_rec

  Description   : This routine parses thru comp's csi list and marks any
                  any csi's in assigning/removing as done.

  Arguments     : cb   - ptr to the AvND control block
                  comp - ptr to the comp

  Return Values : None.

  Notes         : This function will be called only in error flows and not in
                  normal flows.
                  Here we take care of the following cases
******************************************************************************/

void avnd_comp_cmplete_all_csi_rec(AVND_CB *cb, AVND_COMP *comp) {
  AVND_COMP_CSI_REC *curr = 0, *prv = 0;
  TRACE_ENTER2("Comp '%s'", comp->name.c_str());
  /* go and look for all csi's in assigning state and complete the assignment.
   * take care of assign-one and assign-all flags
   */

  if (m_AVND_COMP_IS_ALL_CSI(comp)) {
    curr = m_AVND_CSI_REC_FROM_COMP_DLL_NODE_GET(
        m_NCS_DBLIST_FIND_FIRST(&comp->csi_list));
    if (curr && m_AVND_COMP_CSI_CURR_ASSIGN_STATE_IS_ASSIGNING(curr))
      (void)avnd_comp_csi_assign_done(cb, curr->comp, 0);
    else if (curr && m_AVND_COMP_CSI_CURR_ASSIGN_STATE_IS_REMOVING(curr))
      /* generate csi-remove-done event... csi may be deleted */
      (void)avnd_comp_csi_remove_done(cb, curr->comp, 0);
  } else {
    /* scan the comp-csi list & reassign the csis */
    curr = m_AVND_CSI_REC_FROM_COMP_DLL_NODE_GET(
        m_NCS_DBLIST_FIND_FIRST(&comp->csi_list));
    while (curr) {
      prv = m_AVND_CSI_REC_FROM_COMP_DLL_NODE_GET(
          m_NCS_DBLIST_FIND_PREV(&curr->comp_dll_node));

      if (m_AVND_COMP_CSI_CURR_ASSIGN_STATE_IS_ASSIGNING(curr))
        (void)avnd_comp_csi_assign_done(cb, comp, curr);
      else if (m_AVND_COMP_CSI_CURR_ASSIGN_STATE_IS_REMOVING(curr))
        /* generate csi-remove-done event... csi may be deleted */
        (void)avnd_comp_csi_remove_done(cb, comp, curr);

      /* Avoid nullptr access. */
      if (curr == nullptr) break;

      if (0 == m_AVND_COMPDB_REC_CSI_GET(*comp, curr->name.c_str())) {
        curr = (prv) ? m_AVND_CSI_REC_FROM_COMP_DLL_NODE_GET(
                           m_NCS_DBLIST_FIND_NEXT(&prv->comp_dll_node))
                     : m_AVND_CSI_REC_FROM_COMP_DLL_NODE_GET(
                           m_NCS_DBLIST_FIND_FIRST(&comp->csi_list));
      } else
        curr = m_AVND_CSI_REC_FROM_COMP_DLL_NODE_GET(
            m_NCS_DBLIST_FIND_NEXT(&curr->comp_dll_node));
    } /* while */
  }

  TRACE_LEAVE();
}

uint32_t comp_restart_initiate(AVND_COMP *comp) {
  AVND_CB *cb = avnd_cb;
  uint32_t rc;

  TRACE_ENTER2("%s", comp->name.c_str());

  rc = avnd_comp_curr_info_del(cb, comp);
  if (NCSCC_RC_SUCCESS != rc) goto done;

  if (!comp->su->suMaintenanceCampaign.empty() && !comp->admin_oper) {
    LOG_NO("not restarting comp because maintenance campaign is set: %s",
           comp->su->suMaintenanceCampaign.c_str());

    m_AVND_COMP_OPER_STATE_SET(comp, SA_AMF_OPERATIONAL_DISABLED);
    rc = avnd_comp_oper_state_avd_sync(cb, comp);

    /* update su oper state */
    if (m_AVND_SU_OPER_STATE_IS_ENABLED(comp->su)) {
      m_AVND_SU_OPER_STATE_SET(comp->su, SA_AMF_OPERATIONAL_DISABLED);
      avnd_di_uns32_upd_send(AVSV_SA_AMF_SU, saAmfSUOperState_ID,
                             comp->su->name, comp->su->oper);
    }
    goto done;
  }

  rc = avnd_comp_clc_fsm_run(cb, comp, AVND_COMP_CLC_PRES_FSM_EV_RESTART);
  if (NCSCC_RC_SUCCESS != rc) goto done;

  /* for npi comp mark the CSI restarting/assigning */
  if (!m_AVND_COMP_TYPE_IS_PREINSTANTIABLE(comp)) {
    AVND_COMP_CSI_REC *csi = m_AVND_CSI_REC_FROM_COMP_DLL_NODE_GET(
        m_NCS_DBLIST_FIND_FIRST(&comp->csi_list));
    if (m_AVND_COMP_CSI_CURR_ASSIGN_STATE_IS_ASSIGNED(csi) ||
        m_AVND_COMP_CSI_CURR_ASSIGN_STATE_IS_RESTARTING(csi)) {
      m_AVND_COMP_CSI_CURR_ASSIGN_STATE_SET(
          csi, AVND_COMP_CSI_ASSIGN_STATE_RESTARTING);
    } else if (m_AVND_COMP_CSI_CURR_ASSIGN_STATE_IS_UNASSIGNED(csi)) {
      /* we need not change the csi state. let it be in unassigned state.
       * The instantiation success will not trigger any csi assignment done.
       * If this component is assigned afterwards before completing restart,
       * the csi will move to assinging.
       */
    } else {
      m_AVND_COMP_CSI_CURR_ASSIGN_STATE_SET(
          csi, AVND_COMP_CSI_ASSIGN_STATE_ASSIGNING);
    }
  }

done:
  TRACE_LEAVE2("%u", rc);
  return rc;
}

/**
 * Process component admin operation request from director
 *
 * @param cb
 * @param evt
 */
uint32_t avnd_evt_comp_admin_op_req(AVND_CB *cb, AVND_EVT *evt) {
  AVSV_D2N_ADMIN_OP_REQ_MSG_INFO *info =
      &evt->info.avd->msg_info.d2n_admin_op_req_info;
  AVND_COMP *comp;
  uint32_t rc = NCSCC_RC_SUCCESS;
  const std::string info_dn = Amf::to_string(&info->dn);

  TRACE_ENTER2("'%s' op=%u", info_dn.c_str(), info->oper_id);

  avnd_msgid_assert(info->msg_id);
  cb->rcv_msg_id = info->msg_id;

  comp = avnd_compdb_rec_get(cb->compdb, info_dn);
  osafassert(comp != nullptr);

  switch (info->oper_id) {
    case SA_AMF_ADMIN_RESTART:
      if (comp->pres == SA_AMF_PRESENCE_INSTANTIATED) {
        /* Now trigger the component admin restart */
        comp->admin_oper = true;
        LOG_NO("Admin restart requested for '%s'", info_dn.c_str());
        rc = comp_restart_initiate(comp);
      } else {
        LOG_NO("Admin restart failed '%s' Presence state '%d'", info_dn.c_str(),
               comp->pres);
      }
      break;
    case SA_AMF_ADMIN_EAM_START:
    case SA_AMF_ADMIN_EAM_STOP:
    default:
      LOG_NO("'%s':unsupported adm op %u", __FUNCTION__, info->oper_id);
      rc = NCSCC_RC_FAILURE;
      break;
  }

  TRACE_LEAVE();
  return rc;
}

/**
 * If comp belong to this node, create comp db.
 *
 * @param cb ptr to the AvND control block
 * @param comp_dn ptr to comp dn name
 *
 * @return SaAisErrorT SA_AIS_OK when OK
 */
static SaAisErrorT avnd_validate_comp_and_createdb(AVND_CB *cb,
                                                   const std::string &comp_dn) {
  std::string su_dn;
  AVND_SU *su;
  std::size_t pos;

  pos = comp_dn.find("safSu");
  if (pos == std::string::npos) return SA_AIS_ERR_INVALID_PARAM;

  su_dn = comp_dn.substr(pos);

  /* We got the name of SU, check whether this SU exists or not in our DB. */
  if ((su = avnd_sudb_rec_get(cb->sudb, su_dn)) == nullptr)
    return SA_AIS_ERR_NOT_EXIST;

  /* We got it, so create comp db. */
  if (avnd_comp_config_get_su(su) != NCSCC_RC_SUCCESS) {
    return SA_AIS_ERR_NOT_EXIST;
  }
  return SA_AIS_OK;
}

/**
 * Set the new Component presence state to 'newState'. Update AMF director.
 * Checkpoint. Syslog at INFO level.
 * @param comp
 * @param newstate
 */
void avnd_comp_pres_state_set(const AVND_CB *cb, AVND_COMP *comp,
                              SaAmfPresenceStateT newstate) {
  SaAmfPresenceStateT prv_st = comp->pres;

  osafassert(newstate <= SA_AMF_PRESENCE_ORPHANED);
  if (newstate == SA_AMF_PRESENCE_TERMINATION_FAILED ||
      newstate == SA_AMF_PRESENCE_INSTANTIATION_FAILED)
    LOG_WA("'%s' Presence State %s => %s", comp->name.c_str(),
           presence_state[comp->pres], presence_state[newstate]);
  else
    LOG_IN("'%s' Presence State %s => %s", comp->name.c_str(),
           presence_state[comp->pres], presence_state[newstate]);
  comp->pres = newstate;

  /* inform avd of the change in presence state for all
   * Component Presence state trasitions except
   * INSTANTIATED -> ORPHANED -> INSTANTIATED */
  if ((SA_AMF_PRESENCE_ORPHANED != newstate) &&
      (!((SA_AMF_PRESENCE_INSTANTIATED == newstate) &&
         (SA_AMF_PRESENCE_ORPHANED == prv_st)))) {
    if (cb->is_avd_down == false) {
      avnd_di_uns32_upd_send(AVSV_SA_AMF_COMP, saAmfCompPresenceState_ID,
                             comp->name.c_str(), comp->pres);
    }
  }

  /* create failed state file meaning system restart/cleanup needed */
  if (comp->pres == SA_AMF_PRESENCE_TERMINATION_FAILED)
    avnd_failed_state_file_create();

  /* Inform AMFD to generate ErrorClear() notification */
  clear_error_report_alarm(comp);
}

/**
 * Returns true if the HA state for any CSI assignment is QUIESCED/QUIESCING
 * @param su
 */
bool comp_has_quiesced_assignment(const AVND_COMP *comp) {
  const AVND_COMP_CSI_REC *csi;

  for (csi = m_AVND_CSI_REC_FROM_COMP_DLL_NODE_GET(
           m_NCS_DBLIST_FIND_FIRST(&comp->csi_list));
       csi != nullptr; csi = m_AVND_CSI_REC_FROM_COMP_DLL_NODE_GET(
                           m_NCS_DBLIST_FIND_NEXT(&csi->comp_dll_node))) {
    if ((csi->si->curr_state == SA_AMF_HA_QUIESCED) ||
        (csi->si->curr_state == SA_AMF_HA_QUIESCING))
      return true;
  }

  return false;
}

/**
 * @brief Resets component restart count.
 * @param comp
 */
void comp_reset_restart_count(const AVND_CB *cb, AVND_COMP *comp) {
  if (comp->err_info.restart_cnt != 0) {
    comp->err_info.restart_cnt = 0;
    if (cb->is_avd_down == false) {
      avnd_di_uns32_upd_send(AVSV_SA_AMF_COMP, saAmfCompRestartCount_ID,
                             comp->name, comp->err_info.restart_cnt);
    }
  }
}
/**
 * @brief Inform AMFD to clear error report alarm.
 * @param comp
 */

void clear_error_report_alarm(AVND_COMP *comp) {
  if ((comp->error_report_sent == true) &&
      ((comp->pres == SA_AMF_PRESENCE_UNINSTANTIATED) ||
       (comp->pres == SA_AMF_PRESENCE_INSTANTIATED)) &&
      (comp->oper == SA_AMF_OPERATIONAL_ENABLED)) {
    avnd_di_uns32_upd_send(AVSV_SA_AMF_COMP, saAmfCompRecoveryOnError_ID,
                           comp->name, 0);
    comp->error_report_sent = false;
  }
}

/**
 * @brief  Checks if comp is nonrestartable (DisableRestart=1).
 * @param  comp
 * @return true/false
 */
bool nonrestartable(const AVND_COMP *comp) {
  TRACE("comp nonrestartable:%u", m_AVND_COMP_IS_RESTART_DIS(comp));
  return (m_AVND_COMP_IS_RESTART_DIS(comp));
}

/**
 * @brief  Count no. of CSIs this comp is handling.
 * @param  comp
 * @return uint32_t
 */
uint32_t csi_count(const AVND_COMP *comp) { return (comp->csi_list.n_nodes); }

// Refactor for macros
void avnd_comp_reg_msg_fill(AVSV_AMF_API_INFO *m, MDS_DEST dst, SaAmfHandleT hd,
                            const std::string &cn, const std::string &pcn) {
  m->type = AVSV_AMF_COMP_REG;
  m->dest = dst;
  m->param.reg.hdl = hd;
  osaf_extended_name_alloc(cn.c_str(), &m->param.reg.comp_name);
  osaf_extended_name_alloc(pcn.c_str(), &m->param.reg.proxy_comp_name);
}

uint32_t avnd_comp_oper_state_avd_sync(AVND_CB *cb, AVND_COMP *comp) {
  AVSV_PARAM_INFO param;
  uint32_t rc = NCSCC_RC_SUCCESS;
  TRACE_ENTER();

  if (cb->is_avd_down == true) {
    // pretend it's successful
    return rc;
  }

  memset(&param, 0, sizeof(AVSV_PARAM_INFO));
  param.class_id = AVSV_SA_AMF_COMP;
  param.attr_id = saAmfCompOperState_ID;
  osaf_extended_name_alloc(comp->name.c_str(), &param.name);
  param.act = AVSV_OBJ_OPR_MOD;
  *((uint32_t *)param.value) = m_NCS_OS_HTONL((comp)->oper);
  param.value_len = sizeof(uint32_t);
  rc = avnd_di_object_upd_send((cb), &param);

  TRACE_LEAVE2("%u", rc);
  return rc;
}

uint32_t avnd_comp_proxy_status_avd_sync(AVND_CB *cb, AVND_COMP *comp) {
  AVSV_PARAM_INFO param;
  TRACE_ENTER();

  memset(&param, 0, sizeof(AVSV_PARAM_INFO));
  param.class_id = AVSV_SA_AMF_COMP;
  param.attr_id = saAmfCompProxyStatus_ID;
  osaf_extended_name_alloc(comp->name.c_str(), &param.name);
  param.act = AVSV_OBJ_OPR_MOD;
  *((uint32_t *)param.value) = m_NCS_OS_HTONL((comp)->proxy_status);
  param.value_len = sizeof(uint32_t);
  uint32_t rc = avnd_di_object_upd_send((cb), &param);

  TRACE_LEAVE2("%u", rc);
  return rc;
}

void avnd_amf_hc_cbk_fill(AVSV_AMF_CBK_INFO *cbk, const std::string &cn,
                          SaAmfHealthcheckKeyT hck) {
  cbk->type = AVSV_AMF_HC;
  osaf_extended_name_alloc(cn.c_str(), &cbk->param.hc.comp_name);
  cbk->param.hc.hc_key = hck;
}

void avnd_amf_comp_term_cbk_fill(AVSV_AMF_CBK_INFO *cbk,
                                 const std::string &cn) {
  cbk->type = AVSV_AMF_COMP_TERM;
  osaf_extended_name_alloc(cn.c_str(), &cbk->param.comp_term.comp_name);
}

void avnd_amf_csi_set_cbk_fill(AVSV_AMF_CBK_INFO *cbk, const std::string &cn,
                               SaAmfHAStateT has, SaAmfCSIDescriptorT csid,
                               AVSV_CSI_ATTRS at) {
  cbk->type = AVSV_AMF_CSI_SET;
  osaf_extended_name_alloc(cn.c_str(), &cbk->param.csi_set.comp_name);
  cbk->param.csi_set.ha = has;
  cbk->param.csi_set.csi_desc = csid;
  cbk->param.csi_set.attrs = at;
}

void avnd_amf_csi_rem_cbk_fill(AVSV_AMF_CBK_INFO *cbk, const std::string &cn,
                               const std::string &csn, SaAmfCSIFlagsT csf) {
  cbk->type = AVSV_AMF_CSI_REM;
  osaf_extended_name_alloc(cn.c_str(), &cbk->param.csi_rem.comp_name);
  osaf_extended_name_alloc(csn.c_str(), &cbk->param.csi_rem.csi_name);
  cbk->param.csi_rem.csi_flags = csf;
}

void avnd_amf_pxied_comp_inst_cbk_fill(AVSV_AMF_CBK_INFO *cbk,
                                       const std::string &cn) {
  cbk->type = AVSV_AMF_PXIED_COMP_INST;
  osaf_extended_name_alloc(cn.c_str(), &cbk->param.comp_term.comp_name);
}

void avnd_amf_pxied_comp_clean_cbk_fill(AVSV_AMF_CBK_INFO *cbk,
                                        const std::string &cn) {
  cbk->type = AVSV_AMF_PXIED_COMP_CLEAN;
  osaf_extended_name_alloc(cn.c_str(), &cbk->param.comp_term.comp_name);
}

/**
 * @brief  This event is for remembering MDS dest and version of agent.
 * @param  ptr to AVND_CB
 * @param  ptr to AVND_EVT
 * @return uint32_t
 */
uint32_t avnd_amfa_mds_info_evh(AVND_CB *cb, AVND_EVT *evt) {
  TRACE_ENTER2("mds_dest :%" PRIx64 ", MDS version:%d",
               evt->info.amfa_mds_info.mds_dest,
               evt->info.amfa_mds_info.mds_version);
  agent_mds_ver_db[evt->info.amfa_mds_info.mds_dest] =
      evt->info.amfa_mds_info.mds_version;
  TRACE_LEAVE();
  return NCSCC_RC_SUCCESS;
}
