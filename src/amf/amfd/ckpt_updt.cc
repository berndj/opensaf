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

#include "base/logtrace.h"
#include "amf/amfd/amfd.h"
#include "amf/amfd/sg.h"
#include "amf/amfd/csi.h"
#include "amf/amfd/app.h"

static char *action_name[] = {
    const_cast<char *>("invalid"), const_cast<char *>("add"),
    const_cast<char *>("rmv"), const_cast<char *>("update")};

/****************************************************************************\
 * Function: avd_ckpt_node
 *
 * Purpose:  Add new AVND entry if action is ADD, remove node from the tree if
 *           action is to remove and update data if request is to update.
 *
 * Input: cb  - CB pointer.
 *        avnd - Decoded structur.
 *        action - ADD/RMV/UPDT
 *
 * Returns: NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE.
 *
 * NOTES:
 *
 *
\**************************************************************************/
uint32_t avd_ckpt_node(AVD_CL_CB *cb, AVD_AVND *ckpt_node,
                       NCS_MBCSV_ACT_TYPE action) {
  uint32_t rc = NCSCC_RC_SUCCESS;
  AVD_AVND *node;

  TRACE_ENTER2("%s - '%s'", action_name[action], ckpt_node->name.c_str());

  osafassert(action == NCS_MBCSV_ACT_UPDATE);

  if (nullptr == (node = avd_node_get(ckpt_node->name))) {
    LOG_WA("avd_node_get FAILED for '%s'", ckpt_node->name.c_str());
    rc = NCSCC_RC_FAILURE;
    goto done;
  }
  /* Update all runtime attributes */
  node->saAmfNodeAdminState = ckpt_node->saAmfNodeAdminState;
  node->saAmfNodeOperState = ckpt_node->saAmfNodeOperState;
  node->node_state = ckpt_node->node_state;
  node->rcv_msg_id = ckpt_node->rcv_msg_id;
  node->snd_msg_id = ckpt_node->snd_msg_id;
  node->node_info.member = ckpt_node->node_info.member;
  node->node_info.bootTimestamp = ckpt_node->node_info.bootTimestamp;
  node->node_info.initialViewNumber = ckpt_node->node_info.initialViewNumber;
  node->adest = ckpt_node->adest;
  node->node_info.nodeId = ckpt_node->node_info.nodeId;
  if (nullptr == avd_node_find_nodeid(ckpt_node->node_info.nodeId))
    avd_node_add_nodeid(node);

  avd_saImmOiRtObjectUpdate(node->name, "saAmfNodeOperState",
                            SA_IMM_ATTR_SAUINT32T, &node->saAmfNodeOperState);
  avd_saImmOiRtObjectUpdate(node->name, "saAmfNodeAdminState",
                              SA_IMM_ATTR_SAUINT32T,
                              &node->saAmfNodeAdminState);
done:
  TRACE_LEAVE2("%u", rc);
  return rc;
}

/****************************************************************************\
 * Function: avd_ckpt_app
 *
 * Purpose:  Add new APP entry if action is ADD, remove APP from the tree if
 *           action is to remove and update data if request is to update.
 *
 * Input: cb  - CB pointer.
 *        ckpt_app - Decoded structure.
 *        action - ADD/RMV/UPDT
 *
 * Returns: NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE.
 *
 * NOTES:
 *
 *
\**************************************************************************/
uint32_t avd_ckpt_app(AVD_CL_CB *cb, AVD_APP *ckpt_app,
                      NCS_MBCSV_ACT_TYPE action) {
  uint32_t rc = NCSCC_RC_SUCCESS;

  TRACE_ENTER2("%s - '%s'", action_name[action], ckpt_app->name.c_str());

  osafassert(action == NCS_MBCSV_ACT_UPDATE);

  AVD_APP *app = app_db->find(ckpt_app->name);
  if (app == nullptr) {
    TRACE("'%s' does not exist, creating it", ckpt_app->name.c_str());
    app = new AVD_APP(ckpt_app->name);
    app_db->insert(app->name, app);
    osafassert(rc == NCSCC_RC_SUCCESS);
    goto done;
  }
  /* Update all runtime attributes */
  app->saAmfApplicationAdminState = ckpt_app->saAmfApplicationAdminState;
  app->saAmfApplicationCurrNumSGs = ckpt_app->saAmfApplicationCurrNumSGs;

done:
  TRACE_LEAVE2("%u", rc);
  return rc;
}

/****************************************************************************\
 * Function: avd_ckpt_sg
 *
 * Purpose:  Add new SG entry if action is ADD, remove SG from the tree if
 *           action is to remove and update data if request is to update.
 *
 * Input: cb  - CB pointer.
 *        sg - Decoded structur.
 *        action - ADD/RMV/UPDT
 *
 * Returns: NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE.
 *
 * NOTES:
 *
 *
\**************************************************************************/
uint32_t avd_ckpt_sg(AVD_CL_CB *cb, AVD_SG *ckpt_sg,
                     NCS_MBCSV_ACT_TYPE action) {
  uint32_t rc = NCSCC_RC_SUCCESS;
  AVD_SG *sg;

  TRACE_ENTER2("%s - '%s'", action_name[action], ckpt_sg->name.c_str());

  osafassert(action == NCS_MBCSV_ACT_UPDATE);

  if (nullptr == (sg = sg_db->find(ckpt_sg->name))) {
    LOG_WA("sg_db->find() FAILED for '%s'", ckpt_sg->name.c_str());
    rc = NCSCC_RC_FAILURE;
    goto done;
  }
  /* Update all runtime attributes */
  sg->saAmfSGAdminState = ckpt_sg->saAmfSGAdminState;
  sg->saAmfSGNumCurrAssignedSUs = ckpt_sg->saAmfSGNumCurrAssignedSUs;
  sg->saAmfSGNumCurrInstantiatedSpareSUs =
      ckpt_sg->saAmfSGNumCurrInstantiatedSpareSUs;
  sg->saAmfSGNumCurrNonInstantiatedSpareSUs =
      ckpt_sg->saAmfSGNumCurrNonInstantiatedSpareSUs;
  sg->adjust_state = ckpt_sg->adjust_state;
  sg->sg_fsm_state = ckpt_sg->sg_fsm_state;

done:
  TRACE_LEAVE2("%u", rc);
  return rc;
}

/****************************************************************************\
 * Function: avd_ckpt_su
 *
 * Purpose:  Add new SU entry if action is ADD, remove SU from the tree if
 *           action is to remove and update data if request is to update.
 *
 * Input: cb  - CB pointer.
 *        su - Decoded structur.
 *        action - ADD/RMV/UPDT
 *
 * Returns: NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE.
 *
 * NOTES:
 *
 *
\**************************************************************************/
uint32_t avd_ckpt_su(AVD_CL_CB *cb, AVD_SU *ckpt_su,
                     NCS_MBCSV_ACT_TYPE action) {
  uint32_t rc = NCSCC_RC_SUCCESS;
  AVD_SU *su;

  TRACE_ENTER2("%s - '%s'", action_name[action], ckpt_su->name.c_str());

  osafassert(action == NCS_MBCSV_ACT_UPDATE);

  if (nullptr == (su = su_db->find(ckpt_su->name))) {
    LOG_WA("su_db->find FAILED for '%s'", ckpt_su->name.c_str());
    rc = NCSCC_RC_FAILURE;
    goto done;
  }
  /* Update all runtime attributes */
  su->saAmfSUPreInstantiable = ckpt_su->saAmfSUPreInstantiable;
  su->saAmfSUOperState = ckpt_su->saAmfSUOperState;
  su->saAmfSUAdminState = ckpt_su->saAmfSUAdminState;
  su->saAmfSuReadinessState = ckpt_su->saAmfSuReadinessState;
  su->saAmfSUPresenceState = ckpt_su->saAmfSUPresenceState;
  su->saAmfSUNumCurrActiveSIs = ckpt_su->saAmfSUNumCurrActiveSIs;
  su->saAmfSUNumCurrStandbySIs = ckpt_su->saAmfSUNumCurrStandbySIs;
  su->saAmfSUHostedByNode = ckpt_su->saAmfSUHostedByNode;
  su->term_state = ckpt_su->term_state;
  su->su_switch = ckpt_su->su_switch;
  su->saAmfSURestartCount = ckpt_su->saAmfSURestartCount;

  avd_saImmOiRtObjectUpdate(su->name, "saAmfSUOperState", SA_IMM_ATTR_SAUINT32T,
                            &su->saAmfSUOperState);
  avd_saImmOiRtObjectUpdate(su->name, "saAmfSUAdminState",
                            SA_IMM_ATTR_SAUINT32T, &su->saAmfSUAdminState);
  avd_saImmOiRtObjectUpdate(su->name, "saAmfSUReadinessState",
                            SA_IMM_ATTR_SAUINT32T, &su->saAmfSuReadinessState);
  avd_saImmOiRtObjectUpdate(su->name, "saAmfSUPresenceState",
                            SA_IMM_ATTR_SAUINT32T, &su->saAmfSUPresenceState);
done:
  TRACE_LEAVE2("%u", rc);
  return rc;
}

/****************************************************************************\
 * Function: avd_ckpt_si
 *
 * Purpose:  Add new SI entry if action is ADD, remove SI from the tree if
 *           action is to remove and update data if request is to update.
 *
 * Input: cb - CB pointer.
 *        si - Decoded structur.
 *        action - ADD/RMV/UPDT
 *
 * Returns: NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE.
 *
 * NOTES:
 *
\**************************************************************************/
uint32_t avd_ckpt_si(AVD_CL_CB *cb, AVD_SI *ckpt_si,
                     NCS_MBCSV_ACT_TYPE action) {
  uint32_t rc = NCSCC_RC_FAILURE;
  AVD_SI *si;

  TRACE_ENTER2("%s - '%s'", action_name[action], ckpt_si->name.c_str());

  osafassert(action == NCS_MBCSV_ACT_UPDATE);

  if (nullptr == (si = avd_si_get(ckpt_si->name))) {
    LOG_WA("%s: avd_si_get FAILED", __FUNCTION__);
    goto done;
  }
  /* Update all runtime attributes */
  si->saAmfSINumCurrActiveAssignments =
      ckpt_si->saAmfSINumCurrActiveAssignments;
  si->saAmfSINumCurrStandbyAssignments =
      ckpt_si->saAmfSINumCurrStandbyAssignments;
  si->si_switch = ckpt_si->si_switch;
  si->saAmfSIAdminState = ckpt_si->saAmfSIAdminState;
  si->saAmfSIAssignmentState = ckpt_si->saAmfSIAssignmentState;
  si->saAmfSIProtectedbySG = ckpt_si->saAmfSIProtectedbySG;
  si->alarm_sent = ckpt_si->alarm_sent;
  si->sg_of_si = sg_db->find(si->saAmfSIProtectedbySG);

  rc = NCSCC_RC_SUCCESS;
done:
  TRACE_LEAVE2("%u", rc);
  return rc;
}

/****************************************************************************\
 * Function: avd_ckpt_suoperlist
 *
 * Purpose:  Add or Remove SU from SG operation list.
 *
 * Input: cb  - CB pointer.
 *        dec - Data to be decoded
 *
 * Returns: NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE.
 *
 * NOTES:
 *
 *
\**************************************************************************/
uint32_t avd_ckpt_su_oper_list(const SaNameT *name, NCS_MBCSV_ACT_TYPE action) {
  TRACE_ENTER2("'%s'", osaf_extended_name_borrow(name));

  AVD_SU *su = su_db->find(Amf::to_string(name));
  osafassert(su);

  if (NCS_MBCSV_ACT_ADD == action)
    avd_sg_su_oper_list_add(avd_cb, su, true);
  else if (NCS_MBCSV_ACT_RMV == action)
    avd_sg_su_oper_list_del(avd_cb, su, true);
  else
    osafassert(0);

  return NCSCC_RC_SUCCESS;
}

/****************************************************************************\
 * Function: avd_update_sg_admin_si
 *
 * Purpose:  Update SG Admin SI.
 *
 * Input: cb  - CB pointer.
 *        dec - Data to be decoded
 *
 * Returns: NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE.
 *
 * NOTES:
 *
 *
\**************************************************************************/
uint32_t avd_ckpt_sg_admin_si(AVD_CL_CB *cb, NCS_UBAID *uba,
                              NCS_MBCSV_ACT_TYPE action) {
  SaNameT name;

  TRACE_ENTER2("action %u", action);

  osaf_decode_sanamet(uba, &name);

  AVD_SI *si = si_db->find(Amf::to_string(&name));
  osafassert(si != nullptr);

  switch (action) {
    case NCS_MBCSV_ACT_ADD:
      si->sg_of_si->admin_si = si;
      break;
    case NCS_MBCSV_ACT_RMV:
      si->sg_of_si->admin_si = nullptr;
      break;
    default:
      osafassert(0);
  }

  TRACE_LEAVE2("'%s'", osaf_extended_name_borrow(&name));
  osaf_extended_name_free(&name);
  return NCSCC_RC_SUCCESS;
}

/********************************************************************
 * @brief  updates si parameters in corresponding sg
 * @param[in] cb
 * @param[in] si_trans_ckpt
 * @param[in] action
 *******************************************************************/
uint32_t avd_ckpt_si_trans(AVD_CL_CB *cb, AVSV_SI_TRANS_CKPT_MSG *si_trans_ckpt,
                           NCS_MBCSV_ACT_TYPE action) {
  uint32_t status = NCSCC_RC_SUCCESS;
  AVD_SG *sg_ptr;

  TRACE_ENTER2("'%s'", osaf_extended_name_borrow(&si_trans_ckpt->sg_name));

  sg_ptr = sg_db->find(Amf::to_string(&si_trans_ckpt->sg_name));
  osafassert(sg_ptr);

  switch (action) {
    case NCS_MBCSV_ACT_ADD:
      sg_ptr->si_tobe_redistributed =
          avd_si_get(Amf::to_string(&si_trans_ckpt->si_name));
      sg_ptr->min_assigned_su =
          su_db->find(Amf::to_string(&si_trans_ckpt->min_su_name));
      sg_ptr->max_assigned_su =
          su_db->find(Amf::to_string(&si_trans_ckpt->max_su_name));
      break;

    case NCS_MBCSV_ACT_RMV:
      sg_ptr->si_tobe_redistributed = nullptr;
      sg_ptr->min_assigned_su = nullptr;
      sg_ptr->max_assigned_su = nullptr;
      break;

    default:
      osafassert(0);
  }

  osaf_extended_name_free(&si_trans_ckpt->sg_name);
  osaf_extended_name_free(&si_trans_ckpt->si_name);
  osaf_extended_name_free(&si_trans_ckpt->min_su_name);
  osaf_extended_name_free(&si_trans_ckpt->max_su_name);

  TRACE_LEAVE2("status '%u'", status);
  return status;
}

/****************************************************************************\
 * Function: avd_ckpt_siass
 *
 * Purpose:  Add new SU_SI_REL entry if action is ADD, remove SU_SI_REL from
 *           the tree if action is to remove and update data if request is to
 *           update.
 *
 * Input: cb  - CB pointer.
 *        si - Decoded structur.
 *        action - ADD/RMV/UPDT
 *
 * Returns: NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE.
 *
 * NOTES:
 *
 *
\**************************************************************************/
uint32_t avd_ckpt_siass(AVD_CL_CB *cb, AVSV_SU_SI_REL_CKPT_MSG *su_si_ckpt,
                        NCS_MBCSV_CB_DEC *dec) {
  uint32_t status = NCSCC_RC_SUCCESS;
  AVD_SU_SI_REL *su_si_rel_ptr;
  AVD_SU *su_ptr;
  AVD_SI *si_ptr_up;
  AVD_COMP *comp_ptr;
  AVD_CSI *csi_ptr;
  NCS_MBCSV_ACT_TYPE action = dec->i_action;

  TRACE_ENTER2("'%s' '%s'", osaf_extended_name_borrow(&su_si_ckpt->si_name),
               osaf_extended_name_borrow(&su_si_ckpt->su_name));

  su_si_rel_ptr = avd_susi_find(cb, Amf::to_string(&su_si_ckpt->su_name),
                                Amf::to_string(&su_si_ckpt->si_name));

  su_ptr = su_db->find(Amf::to_string(&su_si_ckpt->su_name));
  osafassert(su_ptr);
  si_ptr_up = avd_si_get(Amf::to_string(&su_si_ckpt->si_name));
  osafassert(si_ptr_up);

  /* Since csi_add_rem flag is not memset in older versions, make sure it is set
   * here. */
  if (dec->i_peer_version <= AVD_MBCSV_SUB_PART_VERSION_4)
    su_si_ckpt->csi_add_rem = static_cast<SaBoolT>(0);

  switch (action) {
    case NCS_MBCSV_ACT_ADD:
      if (nullptr == su_si_rel_ptr) {
        if (NCSCC_RC_SUCCESS != avd_new_assgn_susi(cb, su_ptr, si_ptr_up,
                                                   su_si_ckpt->state, true,
                                                   &su_si_rel_ptr)) {
          LOG_ER("%s: avd_new_assgn_susi failed", __FUNCTION__);
          return NCSCC_RC_FAILURE;
        }
        // update fsm
        TRACE("Update fsm: %u", su_si_ckpt->fsm);
        su_si_rel_ptr->fsm = static_cast<AVD_SU_SI_STATE>(su_si_ckpt->fsm);
      }

    /*
     * Don't break...continue updating data of this SU SI Relation
     * as done during normal update request.
     */

    case NCS_MBCSV_ACT_UPDATE:
      if (nullptr != su_si_rel_ptr) {
        su_si_rel_ptr->fsm = static_cast<AVD_SU_SI_STATE>(su_si_ckpt->fsm);
        su_si_rel_ptr->state = su_si_ckpt->state;
        su_si_rel_ptr->csi_add_rem = su_si_ckpt->csi_add_rem;
        if (su_si_rel_ptr->csi_add_rem) {
          su_si_rel_ptr->comp_name = Amf::to_string(&su_si_ckpt->comp_name);
          su_si_rel_ptr->csi_name = Amf::to_string(&su_si_ckpt->csi_name);
          TRACE("compcsi create for '%s' '%s'",
                su_si_rel_ptr->comp_name.c_str(),
                su_si_rel_ptr->csi_name.c_str());
          if ((comp_ptr = comp_db->find(su_si_rel_ptr->comp_name)) == nullptr) {
            LOG_ER("comp_db->find() FAILED for '%s'",
                   su_si_rel_ptr->comp_name.c_str());
            return NCSCC_RC_FAILURE;
          }
          if ((csi_ptr = csi_db->find(su_si_rel_ptr->csi_name)) == nullptr) {
            /* This condition will arise if there is some delay in the ccb apply
             * callback So create csi and add it to the csi_db here, later in
             * the ccb apply callback attributes will be updated and csi will be
             * added to the model
             */
            csi_ptr = csi_create(su_si_rel_ptr->csi_name);
            osafassert(csi_ptr);
          }
          if ((avd_compcsi_create(su_si_rel_ptr, csi_ptr, comp_ptr, false)) ==
              nullptr) {
            LOG_ER("avd_compcsi_create FAILED for csi '%s' comp '%s'",
                   su_si_rel_ptr->csi_name.c_str(),
                   su_si_rel_ptr->comp_name.c_str());
            return NCSCC_RC_FAILURE;
          }
        } else {
          su_si_rel_ptr->comp_name = "";
          su_si_rel_ptr->csi_name = "";
          // This is update of SUSI because of modification of assignment.
          if (action == NCS_MBCSV_ACT_UPDATE) {
            avd_susi_update(su_si_rel_ptr, su_si_rel_ptr->state);
          }
        }
      } else {
        LOG_ER("%s:%u", __FUNCTION__, __LINE__);
        return NCSCC_RC_FAILURE;
      }
      break;
    case NCS_MBCSV_ACT_RMV:
      if (nullptr != su_si_rel_ptr) {
        if (su_si_ckpt->csi_add_rem) {
          TRACE("compcsi remove for '%s' '%s'",
                osaf_extended_name_borrow(&su_si_ckpt->comp_name),
                osaf_extended_name_borrow(&su_si_ckpt->csi_name));
          if ((comp_ptr = comp_db->find(
                   Amf::to_string(&(su_si_ckpt->comp_name)))) == nullptr) {
            LOG_ER("comp_db->find() FAILED for '%s'",
                   osaf_extended_name_borrow(&su_si_ckpt->comp_name));
            return NCSCC_RC_FAILURE;
          }

          if ((csi_ptr = csi_db->find(
                   Amf::to_string(&(su_si_ckpt->csi_name)))) == nullptr) {
            LOG_ER("csi_db->find() FAILED for '%s'",
                   osaf_extended_name_borrow(&su_si_ckpt->csi_name));
            return NCSCC_RC_FAILURE;
          }
          /* Find the relevant comp-csi to send susi delete. */
          AVD_COMP_CSI_REL *t_csicomp;
          for (t_csicomp = su_si_rel_ptr->list_of_csicomp; t_csicomp;
               t_csicomp = t_csicomp->susi_csicomp_next) {
            if ((t_csicomp->csi == csi_ptr) && (t_csicomp->comp == comp_ptr))
              break;
          }
          if (!t_csicomp) {
            LOG_ER("csicomp not found csi:'%s' comp:'%s'",
                   osaf_extended_name_borrow(&su_si_ckpt->csi_name),
                   osaf_extended_name_borrow(&su_si_ckpt->comp_name));
            return NCSCC_RC_FAILURE;
          }

          /* Delete comp-csi. */
          avd_compcsi_from_csi_and_susi_delete(su_si_rel_ptr, t_csicomp, true);

          if (csi_ptr->list_compcsi == nullptr) {
            /* delete the pg-node list */
            avd_pg_csi_node_del_all(avd_cb, csi_ptr);

            /* free memory and remove from DB */
            avd_csi_delete(csi_ptr);
          }
        } else {
          avd_compcsi_delete(cb, su_si_rel_ptr, true);
          avd_susi_delete(cb, su_si_rel_ptr, true);
        }
      } else {
        LOG_ER("%s: %s %s does not exist", __FUNCTION__,
               osaf_extended_name_borrow(&su_si_ckpt->su_name),
               osaf_extended_name_borrow(&su_si_ckpt->si_name));
        return NCSCC_RC_FAILURE;
      }
      break;
    default:
      osafassert(0);
  }

  TRACE_LEAVE2("status '%u'", status);
  return status;
}

/****************************************************************************\
 * Function: avd_ckpt_comp
 *
 * Purpose:  Add new COMP entry if action is ADD, remove COMP from the tree if
 *           action is to remove and update data if request is to update.
 *
 * Input: cb  - CB pointer.
 *        comp - Decoded structur.
 *        action - ADD/RMV/UPDT
 *
 * Returns: NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE.
 *
 * NOTES:
 *
 *
\**************************************************************************/
uint32_t avd_ckpt_comp(AVD_CL_CB *cb, AVD_COMP *ckpt_comp,
                       NCS_MBCSV_ACT_TYPE action) {
  uint32_t rc = NCSCC_RC_FAILURE;
  AVD_COMP *comp;
  const std::string &dn = Amf::to_string(&ckpt_comp->comp_info.name);

  TRACE_ENTER2("%s - '%s'", action_name[action], dn.c_str());

  osafassert(action == NCS_MBCSV_ACT_UPDATE);

  if (nullptr == (comp = comp_db->find(dn))) {
    LOG_ER("comp_db->find() FAILED for '%s'", dn.c_str());
    goto done;
  }
  comp->saAmfCompOperState = ckpt_comp->saAmfCompOperState;
  comp->saAmfCompPresenceState = ckpt_comp->saAmfCompPresenceState;
  comp->saAmfCompRestartCount = ckpt_comp->saAmfCompRestartCount;
  comp->saAmfCompReadinessState = ckpt_comp->saAmfCompReadinessState;
  /* SaNameT struct copy */
  comp->saAmfCompCurrProxyName = ckpt_comp->saAmfCompCurrProxyName;
  avd_saImmOiRtObjectUpdate(dn, "saAmfCompOperState", SA_IMM_ATTR_SAUINT32T,
                            &comp->saAmfCompOperState);
  avd_saImmOiRtObjectUpdate(dn, "saAmfCompReadinessState",
                            SA_IMM_ATTR_SAUINT32T,
                            &comp->saAmfCompReadinessState);
  avd_saImmOiRtObjectUpdate(dn, "saAmfCompPresenceState", SA_IMM_ATTR_SAUINT32T,
                            &comp->saAmfCompPresenceState);

  rc = NCSCC_RC_SUCCESS;
done:
  osaf_extended_name_free(&ckpt_comp->comp_info.name);
  TRACE_LEAVE2("%u", rc);
  return rc;
}

/****************************************************************************\
 * Function: avd_ckpt_compcstype
 *
 * Purpose:  Add new COMP_CS_TYPE entry if action is ADD, remove COMP_CS_TYPE
 *           from the tree if action is to remove and update data if request is
to update.
 *
 * Input: cb  - CB pointer.
 *        comp_cs_type - Decoded structure.
 *        action - ADD/RMV/UPDT
 *
 * Returns: NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE.
 *
 * NOTES:
 *
 *
\**************************************************************************/
uint32_t avd_ckpt_compcstype(AVD_CL_CB *cb, AVD_COMPCS_TYPE *ckpt_compcstype,
                             NCS_MBCSV_ACT_TYPE action) {
  uint32_t rc = NCSCC_RC_FAILURE;
  AVD_COMPCS_TYPE *ccst;
  const std::string &dn = ckpt_compcstype->name;

  TRACE_ENTER2("%s - '%s'", action_name[action], dn.c_str());

  osafassert(action == NCS_MBCSV_ACT_UPDATE);

  if (nullptr == (ccst = compcstype_db->find(dn))) {
    LOG_WA("compcstype_db->find()FAILED for '%s'", dn.c_str());
    goto done;
  }
  ccst->saAmfCompNumCurrActiveCSIs =
      ckpt_compcstype->saAmfCompNumCurrActiveCSIs;
  ccst->saAmfCompNumCurrStandbyCSIs =
      ckpt_compcstype->saAmfCompNumCurrStandbyCSIs;

  rc = NCSCC_RC_SUCCESS;
done:
  TRACE_LEAVE2("%u", rc);
  return rc;
}

/****************************************************************************\
 * Function: avd_data_clean_up
 *
 * Purpose:  Function is used for cleaning the entire AVD data structure.
 *           Will be called from the standby AVD on failure of warm sync.
 *
 * Input: cb  - CB pointer.
 *
 * Returns: NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE.
 *
 * NOTES:
 *
 *
\**************************************************************************/
uint32_t avd_data_clean_up(AVD_CL_CB *cb) {
#if 0
        AVD_HLT *hlt_chk = nullptr;
        AVSV_HLT_KEY hlt_name;

        AVD_SU *su;
        SaNameT su_name;
        /*AVD_SU_SI_REL *rel; */

        AVD_CSI *csi;
        SaNameT csi_name;

        AVD_COMP *comp;
        SaNameT comp_name;

        SaNameT sg_name;
        AVD_SG *sg;

        AVD_SI *si;
        SaNameT si_name;

        SaClmNodeIdT node_id = 0;
        AVD_AVND *avnd;

        AVD_SUS_PER_SI_RANK *su_si_rank = AVD_SU_PER_SI_RANK_NULL;
        AVD_SUS_PER_SI_RANK_INDX su_si_rank_indx;

        AVD_COMPCS_TYPE *comp_cs = AVD_COMP_CS_TYPE_NULL;
        AVD_COMP_CS_TYPE_INDX comp_cs_indx;
#if 0
        AVD_CS_TYPE_PARAM *cs_param = nullptr;
        AVD_CS_TYPE_PARAM_INDX cs_param_indx;
#endif
        TRACE_ENTER();

        /*
         * Walk through the entire HLT list.
         * Delete all HLT entries.
         */
        memset(&hlt_name, '\0', sizeof(AVSV_HLT_KEY));
        for (hlt_chk = avd_hlt_struc_find_next(cb, hlt_name); hlt_chk != nullptr;
             hlt_chk = avd_hlt_struc_find_next(cb, hlt_name)) {
                /*
                 * Remove this HLT from the list.
                 */
                if (NCSCC_RC_SUCCESS != avd_hlt_struc_del(cb, hlt_chk))
                        return NCSCC_RC_FAILURE;
        }

        /*
         * Walk through the entire SU list and find SU_SI_REL.
         * Delete all SU SI relationship entries.
         */
        su_name.length = 0;
        for (su = avd_su_struc_find_next(cb, su_name, false); su != nullptr;
             su = avd_su_struc_find_next(cb, su_name, false)) {
                while (su->list_of_susi != AVD_SU_SI_REL_NULL) {
                        avd_compcsi_list_del(cb, su->list_of_susi, true);
                        avd_susi_struc_del(cb, su->list_of_susi, true);
                }
                su_name = su->name;
        }

        /*
         * Walk through the entire CSI list
         * Delete all CSI entries.
         */
        csi_name.length = 0;
        for (csi = avd_csi_struc_find_next(cb, csi_name, false); csi != nullptr;
             csi = avd_csi_struc_find_next(cb, csi_name, false)) {
                csi_name = csi->name;
                avd_remove_csi(cb, csi);
        }

        /*
         * Walk through the entire COMP list.
         * Delete all component entries.
         */
        comp_name.length = 0;
        for (comp = avd_comp_struc_find_next(cb, comp_name, false); comp != nullptr;
             comp = avd_comp_struc_find_next(cb, comp_name, false)) {
                comp_name = comp->comp_info.name;
                /* remove the COMP from SU list.
                 */

                if (comp->su)
                        comp->su->curr_num_comp--;

                avd_comp_del_su_list(cb, comp);

                /*
                 * Remove this COMP from the list.
                 */
                if (NCSCC_RC_SUCCESS != avd_comp_struc_del(cb, comp))
                        return NCSCC_RC_FAILURE;
        }

        /*
         * Delete entire operation SU's.
         */
        su_name.length = 0;
        for (su = avd_su_struc_find_next(cb, su_name, false); su != nullptr;
             su = avd_su_struc_find_next(cb, su_name, false)) {
                /* HACK FOR TESTING SHULD BE CHANGED LATER */
                if (su->sg_of_su == 0) {
                        su_name = su->name;
                        continue;
                }

                if (su->sg_of_su->su_oper_list.su != nullptr)
                        avd_sg_su_oper_list_del(cb, su, true);

                su_name = su->name;
        }

        /*
         * Walk through the entire SI list
         * Delete all SI entries.
         */
        si_name.length = 0;
        for (si = avd_si_struc_find_next(cb, si_name, false); si != nullptr;
             si = avd_si_struc_find_next(cb, si_name, false)) {
                si_name = si->name;
                /* remove the SI from the SG list.
                 */
                avd_si_del_sg_list(cb, si);

                /*
                 * Remove this SI from the list.
                 */
                avd_si_struc_del(si, DB_REMOVE_FROM);
        }

        /*
         * Walk through the entire SU list..
         * Delete all SU entries.
         */
        su_name.length = 0;
        for (su = avd_su_struc_find_next(cb, su_name, false); su != nullptr;
             su = avd_su_struc_find_next(cb, su_name, false)) {
                su_name = su->name;
                /* remove the SU from both the SG list and the
                 * AVND list if present.
                 */
                avd_su_del_sg_list(cb, su);
                avd_su_del_avnd_list(cb, su);

                /*
                 * Remove this SU from the list.
                 */
                avd_su_delete(su);
        }

        /*
         * Walk through the entire SG list
         * Delete all the SG entries.
         */
        sg_name.length = 0;
        for (sg = avd_sg_struc_find_next(cb, sg_name, false);
             sg != nullptr; sg = avd_sg_struc_find_next(cb, sg_name, false)) {
                sg_name = sg->name;
                /*
                 * Remove this SG from the list.
                 */
                avd_sg_struc_del(sg, SA_TRUE);
        }

        /*
         * Walk through the entire AVND list.
         * Delete all the AVND entries.
         */
        node_id = 0;
        while (nullptr != (avnd = avd_avnd_struc_find_next_nodeid(node_id))) {
                /*
                 * Remove this AVND from the list.
                 */
                if (NCSCC_RC_SUCCESS != avd_avnd_struc_rmv_nodeid(avnd))
                        return NCSCC_RC_FAILURE;

                if (NCSCC_RC_SUCCESS != avd_avnd_struc_del(avnd))
                        return NCSCC_RC_FAILURE;
        }

        /*
         * Walk through the entire SUS_PER_SI_RANK list.
         * Delete all entries.
         */
        memset(&su_si_rank_indx, '\0', sizeof(AVD_SUS_PER_SI_RANK_INDX));
        for (su_si_rank = avd_sus_per_si_rank_struc_find_next(cb, su_si_rank_indx); su_si_rank != nullptr;
             su_si_rank = avd_sus_per_si_rank_struc_find_next(cb, su_si_rank_indx)) {
                /*
                 * Remove this entry from the list.
                 */
                if (NCSCC_RC_SUCCESS != avd_sus_per_si_rank_struc_del(cb, su_si_rank))
                        return NCSCC_RC_FAILURE;
        }

        /*
         * Walk through the entire COMP_CS_TYPE list.
         * Delete all entries.
         */
        memset(&comp_cs_indx, '\0', sizeof(AVD_COMP_CS_TYPE_INDX));
        for (comp_cs = avd_compcstype_struc_find_next(cb, comp_cs_indx); comp_cs != nullptr;
             comp_cs = avd_compcstype_struc_find_next(cb, comp_cs_indx)) {
                /*
                 * Remove this entry from the list.
                 */
                avd_compcstype_struc_del(comp_cs);
        }

        /*
         * Walk through the entire CS_TYPE_PARAM list.
         * Delete all entries.
         */
#if 0
        /*  TODO */
        memset(&cs_param_indx, '\0', sizeof(AVD_CS_TYPE_PARAM_INDX));
        for (cs_param = avd_cs_type_param_struc_find_next(cb, cs_param_indx); cs_param != nullptr;
             cs_param = avd_cs_type_param_struc_find_next(cb, cs_param_indx)) {
                /*
                 * Remove this entry from the list.
                 */
                if (NCSCC_RC_SUCCESS != avd_cs_type_param_struc_del(cb, cs_param))
                        return NCSCC_RC_FAILURE;
        }
#endif

#endif
  /* Reset the cold sync done counter */
  cb->synced_reo_type = 0;
  return NCSCC_RC_SUCCESS;
}
