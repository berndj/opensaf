/*      -*- OpenSAF  -*-
 *
 * (C) Copyright 2008 The OpenSAF Foundation
 * (C) Copyright 2017 Ericsson AB - All Rights Reserved
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

  This file contains the routines to manage the SU-SI assignments (& removals)
  and SU presence state FSM.

..............................................................................

  FUNCTIONS INCLUDED in this module:


******************************************************************************
*/

#include "amf/amfnd/avnd.h"
#include "amf/amfnd/imm.h"

/* static function declarations */
static uint32_t avnd_su_pres_uninst_suinst_hdler(AVND_CB *, AVND_SU *,
                                                 AVND_COMP *);
static uint32_t avnd_su_pres_insting_suterm_hdler(AVND_CB *, AVND_SU *,
                                                  AVND_COMP *);
static uint32_t avnd_su_pres_insting_surestart_hdler(AVND_CB *, AVND_SU *,
                                                     AVND_COMP *);
static uint32_t avnd_su_pres_insting_compinst_hdler(AVND_CB *, AVND_SU *,
                                                    AVND_COMP *);
static uint32_t avnd_su_pres_insting_compinstfail_hdler(AVND_CB *, AVND_SU *,
                                                        AVND_COMP *);
static uint32_t avnd_su_pres_inst_suterm_hdler(AVND_CB *, AVND_SU *,
                                               AVND_COMP *);
static uint32_t avnd_su_pres_inst_surestart_hdler(AVND_CB *, AVND_SU *,
                                                  AVND_COMP *);
static uint32_t avnd_su_pres_inst_comprestart_hdler(AVND_CB *, AVND_SU *,
                                                    AVND_COMP *);
static uint32_t avnd_su_pres_inst_compterming_hdler(AVND_CB *, AVND_SU *,
                                                    AVND_COMP *);
static uint32_t avnd_su_pres_inst_compinst_hdler(AVND_CB *cb, AVND_SU *su,
                                                 AVND_COMP *comp);
static uint32_t avnd_su_pres_terming_compinst_hdler(AVND_CB *, AVND_SU *,
                                                    AVND_COMP *);
static uint32_t avnd_su_pres_terming_comptermfail_hdler(AVND_CB *, AVND_SU *,
                                                        AVND_COMP *);
static uint32_t avnd_su_pres_terming_compuninst_hdler(AVND_CB *, AVND_SU *,
                                                      AVND_COMP *);
static uint32_t avnd_su_pres_terming_comprestart_hdler(AVND_CB *cb, AVND_SU *su,
                                                       AVND_COMP *comp);
static uint32_t avnd_su_pres_terming_suinst_hdler(AVND_CB *cb, AVND_SU *su,
                                                  AVND_COMP *comp);
static uint32_t avnd_su_pres_terming_surestart_hdler(AVND_CB *cb, AVND_SU *su,
                                                     AVND_COMP *comp);
static uint32_t avnd_su_pres_restart_suterm_hdler(AVND_CB *, AVND_SU *,
                                                  AVND_COMP *);
static uint32_t avnd_su_pres_restart_comprestart_hdler(AVND_CB *, AVND_SU *,
                                                       AVND_COMP *);
static uint32_t avnd_su_pres_restart_compinst_hdler(AVND_CB *, AVND_SU *,
                                                    AVND_COMP *);
static uint32_t avnd_su_pres_restart_compterming_hdler(AVND_CB *, AVND_SU *,
                                                       AVND_COMP *);
static uint32_t avnd_su_pres_inst_compinstfail_hdler(AVND_CB *, AVND_SU *,
                                                     AVND_COMP *);
static uint32_t avnd_su_pres_instfailed_compuninst(AVND_CB *, AVND_SU *,
                                                   AVND_COMP *);
static uint32_t avnd_su_pres_termfailed_comptermfail_or_compuninst(AVND_CB *,
                                                                   AVND_SU *,
                                                                   AVND_COMP *);

static uint32_t avnd_su_pres_st_chng_prc(AVND_CB *, AVND_SU *,
                                         SaAmfPresenceStateT,
                                         SaAmfPresenceStateT);

/****************************************************************************
 * S E R V I C E  U N I T  P R E S  F S M  M A T R I X  D E F I N I T I O N *
 ****************************************************************************/

/* evt handlers are named in this format: avnd_su_pres_<st>_<ev>_hdler() */
static AVND_SU_PRES_FSM_FN avnd_su_pres_fsm[][AVND_SU_PRES_FSM_EV_MAX - 1] = {
    /* SA_AMF_PRESENCE_UNINSTANTIATED */
    {
        avnd_su_pres_uninst_suinst_hdler, /* SU INST */
        0,                                /* SU TERM */
        0,                                /* SU RESTART */
        0,                                /* COMP INSTANTIATED */
        0,                                /* COMP INST_FAIL */
        0,                                /* COMP RESTARTING */
        0,                                /* COMP TERM_FAIL */
        0,                                /* COMP UNINSTANTIATED */
        0,                                /* COMP TERMINATING */
    },

    /* SA_AMF_PRESENCE_INSTANTIATING */
    {
        0,                                       /* SU INST */
        avnd_su_pres_insting_suterm_hdler,       /* SU TERM */
        avnd_su_pres_insting_surestart_hdler,    /* SU RESTART */
        avnd_su_pres_insting_compinst_hdler,     /* COMP INSTANTIATED */
        avnd_su_pres_insting_compinstfail_hdler, /* COMP INST_FAIL */
        0,                                       /* COMP RESTARTING */
        avnd_su_pres_terming_comptermfail_hdler, /* COMP TERM_FAIL */
        0,                                       /* COMP UNINSTANTIATED */
        0,                                       /* COMP TERMINATING */
    },

    /* SA_AMF_PRESENCE_INSTANTIATED */
    {
        0,                                       /* SU INST */
        avnd_su_pres_inst_suterm_hdler,          /* SU TERM */
        avnd_su_pres_inst_surestart_hdler,       /* SU RESTART */
        avnd_su_pres_inst_compinst_hdler,        /* COMP INSTANTIATED */
        avnd_su_pres_inst_compinstfail_hdler,    /* COMP INST_FAIL */
        avnd_su_pres_inst_comprestart_hdler,     /* COMP RESTARTING */
        avnd_su_pres_terming_comptermfail_hdler, /* COMP TERM_FAIL */
        0,                                       /* COMP UNINSTANTIATED */
        avnd_su_pres_inst_compterming_hdler,     /* COMP TERMINATING */
    },

    /* SA_AMF_PRESENCE_TERMINATING */
    {
        avnd_su_pres_terming_suinst_hdler,       /* SU INST */
        avnd_su_pres_restart_suterm_hdler,       /* SU TERM */
        avnd_su_pres_terming_surestart_hdler,    /* SU RESTART */
        avnd_su_pres_terming_compinst_hdler,     /* COMP INSTANTIATED */
        avnd_su_pres_inst_compinstfail_hdler,    /* COMP INST_FAIL */
        avnd_su_pres_terming_comprestart_hdler,  /* COMP RESTARTING */
        avnd_su_pres_terming_comptermfail_hdler, /* COMP TERM_FAIL */
        avnd_su_pres_terming_compuninst_hdler,   /* COMP UNINSTANTIATED */
        0,                                       /* COMP TERMINATING */
    },

    /* SA_AMF_PRESENCE_RESTARTING */
    {
        0,                                       /* SU INST */
        avnd_su_pres_restart_suterm_hdler,       /* SU TERM */
        0,                                       /* SU RESTART */
        avnd_su_pres_restart_compinst_hdler,     /* COMP INSTANTIATED */
        avnd_su_pres_inst_compinstfail_hdler,    /* COMP INST_FAIL */
        avnd_su_pres_restart_comprestart_hdler,  /* COMP RESTARTING */
        avnd_su_pres_terming_comptermfail_hdler, /* COMP TERM_FAIL */
        0,                                       /* COMP UNINSTANTIATED */
        avnd_su_pres_restart_compterming_hdler,  /* COMP TERMINATING */
    },

    /* SA_AMF_PRESENCE_INSTANTIATION_FAILED */
    {
        0,                                  /* SU INST */
        0,                                  /* SU TERM */
        0,                                  /* SU RESTART */
        0,                                  /* COMP INSTANTIATED */
        avnd_su_pres_instfailed_compuninst, /* COMP INST_FAIL */
        0,                                  /* COMP RESTARTING */
        0,                                  /* COMP TERM_FAIL */
        avnd_su_pres_instfailed_compuninst, /* COMP UNINSTANTIATED */
        0,                                  /* COMP TERMINATING */
    },

    /* SA_AMF_PRESENCE_TERMINATION_FAILED */
    {
        0, /* SU INST */
        0, /* SU TERM */
        0, /* SU RESTART */
        0, /* COMP INSTANTIATED */
        0, /* COMP INST_FAIL */
        0, /* COMP RESTARTING */
        avnd_su_pres_termfailed_comptermfail_or_compuninst, /* COMP TERM_FAIL */
        avnd_su_pres_termfailed_comptermfail_or_compuninst, /* COMP
                                                               UNINSTANTIATED */
        0, /* COMP TERMINATING */
    }};

/***************************************************************************
 * S U - S I  Q U E U E  M A N A G E M E N T   P O R T I O N   S T A R T S *
 ***************************************************************************/

/****************************************************************************
  Name          : avnd_su_siq_rec_buf

  Description   : This routine determines if the SU-SI assign message from
                  AvD is to be buffered. If the message has to be buffered,
                  it is added to the SIQ (maintained on SU). Else the message
                  is ready for assignment.

  Arguments     : cb    - ptr to AvND control block
                  su    - ptr to the AvND SU
                  param - ptr to the SI parameters

  Return Values : ptr to the siq record, if the message is buffered
                  0, otherwise

  Notes         : None
******************************************************************************/
AVND_SU_SIQ_REC *avnd_su_siq_rec_buf(AVND_CB *cb, AVND_SU *su,
                                     AVND_SU_SI_PARAM *param) {
  AVND_SU_SIQ_REC *siq = 0;
  AVND_SU_SI_REC *si_rec = 0;
  bool is_tob_buf = false;
  uint32_t rc = NCSCC_RC_SUCCESS;
  TRACE_ENTER2("'%s'", su->name.c_str());

  /* buffer the msg, if SU is inst-failed and all comps are not terminated */
  if (((su->pres == SA_AMF_PRESENCE_INSTANTIATION_FAILED) &&
       (!m_AVND_SU_IS_ALL_TERM(su))) ||
      m_AVND_IS_SHUTTING_DOWN(cb)) {
    siq = avnd_su_siq_rec_add(cb, su, param, &rc);
    TRACE_LEAVE();
    return siq;
  }

  /* dont buffer quiesced assignments during failure */
  if (m_AVND_SU_IS_FAILED(su) && ((SA_AMF_HA_QUIESCED == param->ha_state) ||
                                  (AVSV_SUSI_ACT_DEL == param->msg_act))) {
    TRACE_LEAVE();
    return 0;
  }

  /*
   * Determine if msg is to be buffered.
   */
  if (!m_AVSV_SA_NAME_IS_NULL(param->si_name)) { /* => msg is for a single si */
    /* determine if an (non-quiescing) assign / remove operation is on */
    si_rec = avnd_su_si_rec_get(cb, su->name, Amf::to_string(&param->si_name));
    if (si_rec && ((m_AVND_SU_SI_CURR_ASSIGN_STATE_IS_ASSIGNING(si_rec) &&
                    (SA_AMF_HA_QUIESCING != si_rec->curr_state)) ||
                   (m_AVND_SU_SI_CURR_ASSIGN_STATE_IS_REMOVING(si_rec))))
      is_tob_buf = true;
  } else { /* => msg is for all sis */
    /* determine if an (non-quiescing) assign / remove operation is on */
    for (si_rec = (AVND_SU_SI_REC *)m_NCS_DBLIST_FIND_FIRST(&su->si_list);
         si_rec && !((m_AVND_SU_SI_CURR_ASSIGN_STATE_IS_ASSIGNING(si_rec) &&
                      (SA_AMF_HA_QUIESCING != si_rec->curr_state)) ||
                     (m_AVND_SU_SI_CURR_ASSIGN_STATE_IS_REMOVING(si_rec)));
         si_rec =
             (AVND_SU_SI_REC *)m_NCS_DBLIST_FIND_NEXT(&si_rec->su_dll_node))
      ;
    if (si_rec) is_tob_buf = true;
  }

  /*
   * Buffer the msg (if reqd).
   */
  if (true == is_tob_buf) siq = avnd_su_siq_rec_add(cb, su, param, &rc);

  TRACE_LEAVE();
  return siq;
}

/****************************************************************************
  Name          : avnd_su_siq_prc

  Description   : This routine picks up the least-recent buffered message &
                  initiates the su-si assignment / removal.

  Arguments     : cb   - ptr to AvND control block
                  su   - ptr to the AvND SU

  Return Values : NCSCC_RC_SUCCESS / NCSCC_RC_FAILURE

  Notes         : None
******************************************************************************/
uint32_t avnd_su_siq_prc(AVND_CB *cb, AVND_SU *su) {
  AVND_SU_SIQ_REC *siq = 0;
  AVND_SU_SI_REC *si = 0;
  uint32_t rc = NCSCC_RC_SUCCESS;
  TRACE_ENTER2("SU '%s'", su->name.c_str());

  /* get the least-recent buffered msg, if any */
  siq = (AVND_SU_SIQ_REC *)m_NCS_DBLIST_FIND_LAST(&su->siq);
  if (!siq) {
    TRACE_LEAVE();
    return rc;
  }

  /* determine if an (non-quiescing) assign / remove operation is on */
  for (si = (AVND_SU_SI_REC *)m_NCS_DBLIST_FIND_FIRST(&su->si_list);
       si && !((m_AVND_SU_SI_CURR_ASSIGN_STATE_IS_ASSIGNING(si) &&
                (SA_AMF_HA_QUIESCING != si->curr_state)) ||
               (m_AVND_SU_SI_CURR_ASSIGN_STATE_IS_REMOVING(si)));
       si = (AVND_SU_SI_REC *)m_NCS_DBLIST_FIND_NEXT(&si->su_dll_node))
    ;
  if (si) {
    TRACE_LEAVE();
    return rc;
  }

  /* unlink the buffered msg from the queue */
  ncs_db_link_list_delink(&su->siq, &siq->su_dll_node);

  /* initiate si asignment / removal */
  rc = avnd_su_si_msg_prc(cb, su, &siq->info);

  /* delete the buffered msg */
  avnd_su_siq_rec_del(cb, su, siq);

  TRACE_LEAVE2("%u", rc);
  return rc;
}

/***************************************************************************
 *****  S U - S I   M A N A G E M E N T   P O R T I O N   S T A R T S  *****
 ***************************************************************************/

/****************************************************************************
  Name          : avnd_su_si_msg_prc

  Description   : This routine initiates the su-si assignment / removal. It
                  updates the database & then starts the assignment / removal.

  Arguments     : cb   - ptr to AvND control block
                  su   - ptr to the AvND SU
                  info - ptr to the si param

  Return Values : NCSCC_RC_SUCCESS / NCSCC_RC_FAILURE

  Notes         : None
******************************************************************************/
uint32_t avnd_su_si_msg_prc(AVND_CB *cb, AVND_SU *su, AVND_SU_SI_PARAM *info) {
  AVND_SU_SI_REC *si = 0;
  uint32_t rc = NCSCC_RC_SUCCESS;

  TRACE_ENTER2("'%s', act=%u, ha_state=%u, single_csi=%u", su->name.c_str(),
               info->msg_act, info->ha_state, info->single_csi);

  /* we have started the su si msg processing */
  m_AVND_SU_ASSIGN_PEND_SET(su);

  /* If the request targets all SIs, set flag once early for all cases */
  if (avsv_sa_name_is_null(&info->si_name)) m_AVND_SU_ALL_SI_SET(su);

  switch (info->msg_act) {
    case AVSV_SUSI_ACT_ASGN: /* new assign */
    {
      if (false == info->single_csi) {
        /* add to the database */
        si = avnd_su_si_rec_add(cb, su, info, &rc);
      } else {
        AVND_COMP_CSI_PARAM *csi_param;
        AVND_COMP_CSI_REC *csi;

        /* verify if su-si relationship already exists */
        if (0 == (si = avnd_su_si_rec_get(cb, Amf::to_string(&info->su_name),
                                          Amf::to_string(&info->si_name)))) {
          LOG_ER("No SUSI Rec exists");
          goto done;
        }

        osafassert((info->num_assigns == 1));
        csi_param = info->list;
        osafassert(csi_param);
        osafassert(!(csi_param->next));
        csi = avnd_su_si_csi_rec_add(cb, su, si, csi_param, &rc);
        si->single_csi_add_rem_in_si = AVSV_SUSI_ACT_ASGN;
        csi->single_csi_add_rem_in_si = AVSV_SUSI_ACT_ASGN;
        LOG_NO("Adding CSI '%s'",
               osaf_extended_name_borrow(&csi_param->csi_name));
      }

      /* initiate si assignment */
      if (si) rc = avnd_su_si_assign(cb, su, si);
    } break;

    case AVSV_SUSI_ACT_DEL: /* remove */
    {
      /* get the si */
      si = avnd_su_si_rec_get(cb, su->name, Amf::to_string(&info->si_name));
      if (!si && !m_AVSV_SA_NAME_IS_NULL(info->si_name)) goto done;

      /*
       * si may point to si-rec to be deleted or be 0 (signifying all).
       * initiate si removal.
       */
      if (true == info->single_csi) {
        AVND_COMP_CSI_PARAM *csi_param;
        AVND_COMP_CSI_REC *csi_rec;
        si->single_csi_add_rem_in_si = AVSV_SUSI_ACT_DEL;
        osafassert((info->num_assigns == 1));
        csi_param = info->list;
        osafassert(csi_param);
        osafassert(!(csi_param->next));
        if (nullptr == (csi_rec = avnd_compdb_csi_rec_get(
                            cb, Amf::to_string(&csi_param->comp_name),
                            Amf::to_string(&csi_param->csi_name)))) {
          LOG_ER("No CSI Rec exists for comp='%s'and csi=%s",
                 osaf_extended_name_borrow(&csi_param->comp_name),
                 osaf_extended_name_borrow(&csi_param->csi_name));
          goto done;
        }
        csi_rec->single_csi_add_rem_in_si = AVSV_SUSI_ACT_DEL;
        LOG_NO("Removing CSI '%s'",
               osaf_extended_name_borrow(&csi_param->csi_name));
      }

      rc = avnd_su_si_remove(cb, su, si);
    } break;

    case AVSV_SUSI_ACT_MOD: /* modify */
    {
      /* modify in the database */
      if (!m_AVSV_SA_NAME_IS_NULL(info->si_name)) /* modify a specific si-rec */
        si = avnd_su_si_rec_modify(cb, su, info, &rc);
      else
        /* modify all the comp-csi records that belong to this su */
        rc = avnd_su_si_all_modify(cb, su, info);

      /* initiate si modification */
      if (NCSCC_RC_SUCCESS == rc) rc = avnd_su_si_assign(cb, su, si);
    } break;

    default:
      osafassert(0);
  } /* switch */

done:
  TRACE_LEAVE2("%u", rc);
  return rc;
}
/**
 * @brief       Checks if all csi of same SI assigned to the component are in
 *assigning state
 *
 * @param [in]  cmp
 *              si
 *
 * @returns     true/false
 **/
static bool csi_of_same_si_in_assigning_state(const AVND_COMP *cmp,
                                              const AVND_SU_SI_REC *si) {
  AVND_COMP_CSI_REC *curr_csi;
  bool all_csi_assigned = true;
  TRACE_ENTER();
  for (curr_csi = m_AVND_CSI_REC_FROM_COMP_DLL_NODE_GET(
           m_NCS_DBLIST_FIND_FIRST(&cmp->csi_list));
       curr_csi; curr_csi = m_AVND_CSI_REC_FROM_COMP_DLL_NODE_GET(
                     m_NCS_DBLIST_FIND_NEXT(&curr_csi->comp_dll_node))) {
    TRACE(
        "curr_csi:'%s', prv_assign_state:%u curr_assign_state:%u, suspending_assignment:%u",
        curr_csi->name.c_str(), curr_csi->prv_assign_state,
        curr_csi->curr_assign_state, curr_csi->suspending_assignment);
    if ((curr_csi->si == si) &&
        !m_AVND_COMP_CSI_CURR_ASSIGN_STATE_IS_ASSIGNING(curr_csi)) {
      // ignore suspending_assignment
      if (!curr_csi->suspending_assignment) {
        all_csi_assigned = false;
        break;
      }
    }
  }
  TRACE_LEAVE();
  return all_csi_assigned;
}
/**
 * Assign a SI to an SU
 * @param si
 * @param su
 * @param single_csi if true just one CSI is assigned. if false all CSIs
 *                   are assigned in one shot.
 *
 * @return uns32
 */
static uint32_t assign_si_to_su(const AVND_CB *cb, AVND_SU_SI_REC *si,
                                AVND_SU *su, int single_csi) {
  uint32_t rc = NCSCC_RC_SUCCESS;
  AVND_COMP_CSI_REC *curr_csi;

  TRACE_ENTER2("'%s' si-state'%d' '%s' single_csi=%u", si->name.c_str(),
               si->curr_state, su->name.c_str(), single_csi);

  LOG_NO("Assigning '%s' %s to '%s'", si->name.c_str(),
         ha_state[si->curr_state], su->name.c_str());

  /* initiate the si assignment for pi su */
  if (m_AVND_SU_IS_PREINSTANTIABLE(su)) {
    uint32_t rank;

    if (SA_AMF_HA_ACTIVE == si->curr_state ||
        SA_AMF_HA_STANDBY == si->curr_state) {
      if (single_csi) {
        for (curr_csi =
                 (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_FIRST(&si->csi_list),
            rank = curr_csi->rank;
             (curr_csi != nullptr) &&
             ((curr_csi->rank == rank) ||
              (curr_csi->si->single_csi_add_rem_in_si == AVSV_SUSI_ACT_ASGN));
             curr_csi = (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_NEXT(
                 &curr_csi->si_dll_node)) {
          curr_csi->comp->assigned_flag = false;
        }
      }
      for (curr_csi =
               (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_FIRST(&si->csi_list),
          rank = curr_csi->rank;
           (curr_csi != nullptr) &&
           ((curr_csi->rank == rank) ||
            (curr_csi->si->single_csi_add_rem_in_si == AVSV_SUSI_ACT_ASGN));
           curr_csi = (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_NEXT(
               &curr_csi->si_dll_node)) {
        if (AVND_COMP_CSI_ASSIGN_STATE_ASSIGNED !=
            curr_csi->curr_assign_state) {
          if (false == curr_csi->comp->assigned_flag) {
            /* Dont assign, if already assignd */
            if (AVND_SU_SI_ASSIGN_STATE_ASSIGNED !=
                curr_csi->si->curr_assign_state) {
              rc = avnd_comp_csi_assign(avnd_cb, curr_csi->comp,
                                        (single_csi) ? curr_csi : nullptr);
              if (NCSCC_RC_SUCCESS != rc) goto done;
            }
            if ((false == curr_csi->comp->assigned_flag) &&
                (m_AVND_COMP_CSI_PRV_ASSIGN_STATE_IS_ASSIGNED(curr_csi))) {
              if (csi_of_same_si_in_assigning_state(curr_csi->comp, si))
                curr_csi->comp->assigned_flag = true;
            }
          } else {
            m_AVND_COMP_CSI_CURR_ASSIGN_STATE_SET(
                curr_csi, AVND_COMP_CSI_ASSIGN_STATE_ASSIGNED);
          }
        }
      }
    } else {
      if (single_csi) {
        for (curr_csi =
                 (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_LAST(&si->csi_list),
            rank = curr_csi->rank;
             (curr_csi != nullptr) &&
             ((curr_csi->rank == rank) ||
              (curr_csi->si->single_csi_add_rem_in_si == AVSV_SUSI_ACT_ASGN));
             curr_csi = (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_PREV(
                 &curr_csi->si_dll_node)) {
          curr_csi->comp->assigned_flag = false;
        }
      }
      for (curr_csi =
               (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_LAST(&si->csi_list),
          rank = curr_csi->rank;
           (curr_csi != nullptr) &&
           ((curr_csi->rank == rank) ||
            (curr_csi->si->single_csi_add_rem_in_si == AVSV_SUSI_ACT_ASGN));
           curr_csi = (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_PREV(
               &curr_csi->si_dll_node)) {
        /* We need to send only one csi set for a comp having  more than one CSI
           assignment for csi mod/rem.*/
        if (AVND_COMP_CSI_ASSIGN_STATE_ASSIGNED !=
            curr_csi->curr_assign_state) {
          if (false == curr_csi->comp->assigned_flag) {
            /* Dont assign, if already assignd */
            if (AVND_SU_SI_ASSIGN_STATE_ASSIGNED !=
                curr_csi->si->curr_assign_state) {
              rc = avnd_comp_csi_assign(avnd_cb, curr_csi->comp,
                                        (single_csi) ? curr_csi : nullptr);
              if (NCSCC_RC_SUCCESS != rc) goto done;
            }
            /*if ((!single_csi) && (false == curr_csi->comp->assigned_flag) &&
                                    (m_AVND_COMP_CSI_PRV_ASSIGN_STATE_IS_ASSIGNED(curr_csi)))*/
            if (false == curr_csi->comp->assigned_flag) {
              if (csi_of_same_si_in_assigning_state(curr_csi->comp, si)) {
                curr_csi->comp->assigned_flag = true;
              }
            }
          } else {
            // Don't move csi to ASSIGNED state if its assignment's suspending
            if (!curr_csi->suspending_assignment) {
              m_AVND_COMP_CSI_CURR_ASSIGN_STATE_SET(
                  curr_csi, AVND_COMP_CSI_ASSIGN_STATE_ASSIGNED);
            }
          }
        }
      }
    }
  }

  /* initiate the si assignment for npi su */
  if (!m_AVND_SU_IS_PREINSTANTIABLE(su)) {
    TRACE("SU is NPI");

    if ((single_csi == 1) &&
        (si->single_csi_add_rem_in_si == AVSV_SUSI_ACT_ASGN)) {
      // we are adding a CSI to an unlocked SU

      // first find the newly added unassigned CSI
      for (curr_csi =
               (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_FIRST(&si->csi_list);
           curr_csi != nullptr;
           curr_csi = (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_NEXT(
               &curr_csi->si_dll_node)) {
        if (curr_csi->single_csi_add_rem_in_si == AVSV_SUSI_ACT_ASGN) break;
      }

      osafassert(curr_csi);

      if (si->curr_state == SA_AMF_HA_ACTIVE) {
        avnd_su_pres_state_set(cb, su, SA_AMF_PRESENCE_INSTANTIATING);
        rc = avnd_comp_csi_assign(avnd_cb, curr_csi->comp, curr_csi);
      } else {
        curr_csi->single_csi_add_rem_in_si = AVSV_SUSI_ACT_BASE;
        rc = avnd_su_si_oper_done(avnd_cb, su, si);
      }

      goto done;
    }

    bool npi_prv_inst = true, npi_curr_inst = true;
    AVND_SU_PRES_FSM_EV su_ev = AVND_SU_PRES_FSM_EV_MAX;

    /* determine the instantiation state of npi su */
    if (SA_AMF_HA_ACTIVE != si->prv_state)
      // If previous state was not active, it means SU was not instantiated.
      npi_prv_inst = false;
    if (SA_AMF_HA_ACTIVE != si->curr_state)
      /*If current state, that has come from AMFD, is not active, then also this
        NPI SU is not going to be instantiated.*/
      npi_curr_inst = false;

    /* Quiesced while Quiescing */
    if (m_AVND_SU_SI_PRV_ASSIGN_STATE_IS_ASSIGNING(si) &&
        (SA_AMF_HA_QUIESCING == si->prv_state))
      /*I guess this condition is for the situation when shutdown operation is
         going on and lock is issued over it.*/
      npi_prv_inst = true;

    /* determine the event for the su fsm */
    if (m_AVND_SU_IS_RESTART(su) && (true == npi_curr_inst))
      /* This seems to be a doubtful if block.
         Active state has come when su restart is going on.
         This can never happen as NPI su gets only one SI.
       */
      su_ev = AVND_SU_PRES_FSM_EV_RESTART;
    else if (!m_AVND_SU_IS_RESTART(su) && (npi_prv_inst != npi_curr_inst))
      /* When surestart is not going on and quiesced assignments or active
         state has come. */
      su_ev = (true == npi_curr_inst) ? AVND_SU_PRES_FSM_EV_INST
                                      : AVND_SU_PRES_FSM_EV_TERM;
    else if (m_AVND_SU_IS_RESTART(su) &&
             (su_all_comps_restartable(*su) == false) &&
             (npi_curr_inst == false)) {
      /*suRestart is going on and SU has atleast one component
       * non-restartable.*/
      TRACE(
          "suRestart is going on for a non-restartable SU,"
          "  terminate all the components for quiesced state.");
      su_ev = AVND_SU_PRES_FSM_EV_RESTART;
    }

    /* we cant do anything on inst-failed SU or term-failed SU, so just resp
     * success for quiesced */
    if (su->pres == SA_AMF_PRESENCE_INSTANTIATION_FAILED ||
        su->pres == SA_AMF_PRESENCE_TERMINATION_FAILED)
      su_ev = AVND_SU_PRES_FSM_EV_MAX;

    /* trigger the su fsm */
    if (AVND_SU_PRES_FSM_EV_MAX != su_ev) {
      rc = avnd_su_pres_fsm_run(avnd_cb, su, 0, su_ev);
    } else
      rc = avnd_su_si_oper_done(avnd_cb, su, ((single_csi) ? si : nullptr));
    if (NCSCC_RC_SUCCESS != rc) goto done;
  }

done:
  TRACE_LEAVE2("%u", rc);
  return rc;
}

/****************************************************************************
  Name          : avnd_su_si_assign

  Description   : This routine triggers the su-si assignment.
                  For PI SU, it picks up the lowest/highest ranked csi from
                  the csi-list (maintained on si). It then invokes csi-set
                  callback for CSI belonging to PI components and triggers
                  comp FSM with InstEv/TermEv trigger for CSI belonging to
                  NPI comps.
                  For NPI SU, the SU FSM is triggered with SUInstEv/SUTerm
                  event.
                  The decision to pick the lowest/highest rank or generate
                  instantiate/terminate trigger depends on the current and
                  previous HA state.
                  If the SU is in restart state (i.e. su-restart recovery is
                  in progress), comp & SU FSM are triggered with Restart
                  events instead (in the above flow).

  Arguments     : cb - ptr to AvND control block
                  su - ptr to the AvND SU
                  si - ptr to the si record (if 0, all the CSIs corresponding
                       to a comp are assigned in one shot)

  Return Values : NCSCC_RC_SUCCESS / NCSCC_RC_FAILURE

  Notes         : None
******************************************************************************/
uint32_t avnd_su_si_assign(AVND_CB *cb, AVND_SU *su, AVND_SU_SI_REC *si) {
  uint32_t rc = NCSCC_RC_SUCCESS, rank;
  AVND_SU_SI_REC *curr_si;
  AVND_COMP_CSI_REC *curr_csi;

  TRACE_ENTER2("'%s' '%s'", su->name.c_str(), si ? si->name.c_str() : nullptr);

  /* mark the si(s) assigning and assign to su */
  if (si) {
    m_AVND_SU_SI_CURR_ASSIGN_STATE_SET(si, AVND_SU_SI_ASSIGN_STATE_ASSIGNING);
    rc = assign_si_to_su(cb, si, su, true);
  } else {
    for (curr_si = (AVND_SU_SI_REC *)m_NCS_DBLIST_FIND_FIRST(&su->si_list);
         curr_si != nullptr; curr_si = (AVND_SU_SI_REC *)m_NCS_DBLIST_FIND_NEXT(
                                 &curr_si->su_dll_node)) {
      if (SA_AMF_HA_ACTIVE == curr_si->curr_state ||
          SA_AMF_HA_STANDBY == curr_si->curr_state) {
        for (curr_csi = (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_FIRST(
                 &curr_si->csi_list),
            rank = curr_csi->rank;
             (curr_csi != nullptr) && (curr_csi->rank == rank);
             curr_csi = (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_NEXT(
                 &curr_csi->si_dll_node)) {
          curr_csi->comp->assigned_flag = false;
        }
      } else {
        for (curr_csi = (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_LAST(
                 &curr_si->csi_list),
            rank = curr_csi->rank;
             (curr_csi != nullptr) && (curr_csi->rank == rank);
             curr_csi = (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_PREV(
                 &curr_csi->si_dll_node)) {
          curr_csi->comp->assigned_flag = false;
        }
      }
      m_AVND_SU_SI_CURR_ASSIGN_STATE_SET(curr_si,
                                         AVND_SU_SI_ASSIGN_STATE_ASSIGNING);
    }
    /* if no si is specified, the action is aimed at all the sis... loop */
    for (curr_si = (AVND_SU_SI_REC *)m_NCS_DBLIST_FIND_FIRST(&su->si_list);
         curr_si != nullptr; curr_si = (AVND_SU_SI_REC *)m_NCS_DBLIST_FIND_NEXT(
                                 &curr_si->su_dll_node)) {
      rc = assign_si_to_su(cb, curr_si, su, false);
      if (NCSCC_RC_SUCCESS != rc) goto done;
    }
  }

done:
  TRACE_LEAVE2("%u", rc);
  return rc;
}

/****************************************************************************
  Name          : avnd_su_si_remove

  Description   : This routine triggers su-si assignment removal. For PI SU,
                  it picks up the highest ranked CSIs from the csi-list. It
                  then invokes csi-remove callback for CSIs belonging to PI
                  components and triggers comp FSM with TermEv for CSIs
                  belonging to NPI comp. For NPI SU, the SU FSM is triggered
                  with SUTermEv.

  Arguments     : cb - ptr to AvND control block
                  su - ptr to the AvND SU
                  si - ptr to the si record (if 0, all the CSIs corresponding
                       to a comp are removed in one shot)

  Return Values : NCSCC_RC_SUCCESS / NCSCC_RC_FAILURE

  Notes         : None
******************************************************************************/
uint32_t avnd_su_si_remove(AVND_CB *cb, AVND_SU *su, AVND_SU_SI_REC *si) {
  AVND_COMP_CSI_REC *curr_csi = 0;
  AVND_SU_SI_REC *curr_si = 0;
  uint32_t rc = NCSCC_RC_SUCCESS;

  TRACE_ENTER2("'%s' '%s'", su->name.c_str(), si ? si->name.c_str() : nullptr);

  /* mark the si(s) removing */
  if (si) {
    m_AVND_SU_SI_CURR_ASSIGN_STATE_SET(si, AVND_SU_SI_ASSIGN_STATE_REMOVING);
  } else {
    for (curr_si = (AVND_SU_SI_REC *)m_NCS_DBLIST_FIND_FIRST(&su->si_list);
         curr_si; curr_si = (AVND_SU_SI_REC *)m_NCS_DBLIST_FIND_NEXT(
                      &curr_si->su_dll_node)) {
      m_AVND_SU_SI_CURR_ASSIGN_STATE_SET(curr_si,
                                         AVND_SU_SI_ASSIGN_STATE_REMOVING);
    }
  }

  if ((su->si_list.n_nodes > 1) && (si == nullptr))
    LOG_NO("Removing 'all (%u) SIs' from '%s'", su->si_list.n_nodes,
           su->name.c_str());

  /* if no si is specified, the action is aimed at all the sis... pick up any si
   */
  curr_si = (si) ? si : (AVND_SU_SI_REC *)m_NCS_DBLIST_FIND_FIRST(&su->si_list);
  if (!curr_si) {
    // after headless, we may have a buffered susi remove msg
    // if the susi can't be found (already removed), reset flag
    LOG_NO("no SI found in '%s'", su->name.c_str());
    m_AVND_SU_ALL_SI_RESET(su);
    goto done;
  }

  /* initiate the si removal for pi su */
  if (m_AVND_SU_IS_PREINSTANTIABLE(su)) {
    if (si) {
      if (AVSV_SUSI_ACT_DEL == si->single_csi_add_rem_in_si) {
        AVND_COMP_CSI_REC *next_csi;

        /* We have to remove single csi. */
        for (curr_csi =
                 (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_FIRST(&si->csi_list);
             curr_csi;) {
          next_csi = (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_NEXT(
              &curr_csi->si_dll_node);
          if (AVSV_SUSI_ACT_DEL == curr_csi->single_csi_add_rem_in_si) {
            rc = avnd_comp_csi_remove(cb, curr_csi->comp, curr_csi);
            if (NCSCC_RC_SUCCESS != rc) goto done;
          }
          curr_csi = next_csi;
        }
      } else {
        /* pick up the last csi */
        curr_csi =
            (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_LAST(&curr_si->csi_list);

        /* remove the csi */
        if (curr_csi) {
          rc = avnd_comp_csi_remove(cb, curr_csi->comp, (si) ? curr_csi : 0);
          if (NCSCC_RC_SUCCESS != rc) goto done;
        }
      }
    } else {
      AVND_SU_SI_REC *next_si;

      /* removal for all SIs, do all in paralell */
      for (curr_si = (AVND_SU_SI_REC *)m_NCS_DBLIST_FIND_FIRST(&su->si_list);
           curr_si;) {
        /* Save next ptr here since it might get deleted deep down in
         * avnd_comp_csi_remove */
        next_si =
            (AVND_SU_SI_REC *)m_NCS_DBLIST_FIND_NEXT(&curr_si->su_dll_node);

        /* pick up the last csi */
        curr_csi =
            (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_LAST(&curr_si->csi_list);

        LOG_NO("Removing '%s' from '%s'", curr_si->name.c_str(),
               su->name.c_str());

        /* remove all the CSIs from this comp */
        if (curr_csi) {
          rc = avnd_comp_csi_remove(cb, curr_csi->comp, nullptr);
          if (NCSCC_RC_SUCCESS != rc || !su->si_list.n_nodes) goto done;
        }
        curr_si = next_si;
      }
    }
  }

  /* initiate the si removal for npi su */
  if (!m_AVND_SU_IS_PREINSTANTIABLE(su)) {
    if ((si != nullptr) &&
        (si->single_csi_add_rem_in_si == AVSV_SUSI_ACT_DEL) &&
        (si->curr_state == SA_AMF_HA_ACTIVE)) {
      /* we are removing a single CSI, first find it */
      for (curr_csi =
               (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_FIRST(&si->csi_list);
           curr_csi != nullptr;
           curr_csi = (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_NEXT(
               &curr_csi->si_dll_node)) {
        if (AVSV_SUSI_ACT_DEL == curr_csi->single_csi_add_rem_in_si) break;
      }

      osafassert(curr_csi != nullptr);
      rc = avnd_comp_csi_remove(cb, curr_csi->comp, curr_csi);
      if (rc == NCSCC_RC_SUCCESS)
        avnd_su_pres_state_set(cb, su, SA_AMF_PRESENCE_TERMINATING);
    } else {
      /* nothing to be done, termination already done in
         quiescing/quiesced state */
      if (su->pres == SA_AMF_PRESENCE_INSTANTIATED) {
        rc = avnd_su_pres_fsm_run(cb, su, 0, AVND_SU_PRES_FSM_EV_TERM);
        if (NCSCC_RC_SUCCESS != rc) goto done;
      } else {
        rc = avnd_su_si_oper_done(cb, su, si);
        if (NCSCC_RC_SUCCESS != rc) goto done;
      }
    }
  }

done:
  TRACE_LEAVE2("%u", rc);
  return rc;
}
/**
 * @brief       Checks if any susi operation(assigning or removing) is in
 *progress
 *
 * @param[in]   su
 *              si
 *
 * @return      true/false
 **/
static bool susi_operation_in_progress(AVND_SU *su, AVND_SU_SI_REC *si) {
  AVND_SU_SI_REC *curr_si, *t_si;
  AVND_COMP_CSI_REC *curr_csi, *t_csi;
  bool opr_done = true;

  TRACE_ENTER2("'%s' '%s'", su->name.c_str(), si ? si->name.c_str() : nullptr);

  for (curr_si = (si) ? si
                      : (AVND_SU_SI_REC *)m_NCS_DBLIST_FIND_FIRST(&su->si_list);
       curr_si && opr_done;
       curr_si = (si) ? 0
                      : (AVND_SU_SI_REC *)m_NCS_DBLIST_FIND_NEXT(
                            &curr_si->su_dll_node)) {
    for (curr_csi =
             (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_FIRST(&curr_si->csi_list);
         curr_csi; curr_csi = (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_NEXT(
                       &curr_csi->si_dll_node)) {
      if (m_AVND_COMP_CSI_CURR_ASSIGN_STATE_IS_ASSIGNING(curr_csi) ||
          m_AVND_COMP_CSI_CURR_ASSIGN_STATE_IS_REMOVING(curr_csi)) {
        opr_done = false;
        break;
      } else if (m_AVND_COMP_IS_FAILED(curr_csi->comp)) {
        for (t_si =
                 (si) ? si
                      : (AVND_SU_SI_REC *)m_NCS_DBLIST_FIND_FIRST(&su->si_list);
             t_si; t_si = (si) ? 0
                               : (AVND_SU_SI_REC *)m_NCS_DBLIST_FIND_NEXT(
                                     &t_si->su_dll_node)) {
          for (t_csi = (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_FIRST(
                   &t_si->csi_list);
               t_csi; t_csi = (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_NEXT(
                          &t_csi->si_dll_node)) {
            if (m_AVND_COMP_IS_FAILED(t_csi->comp) ||
                (su->pres == SA_AMF_PRESENCE_INSTANTIATION_FAILED) ||
                (su->pres == SA_AMF_PRESENCE_TERMINATION_FAILED))
              continue;
            else if (m_AVND_COMP_CSI_CURR_ASSIGN_STATE_IS_ASSIGNING(t_csi) ||
                     m_AVND_COMP_CSI_CURR_ASSIGN_STATE_IS_REMOVING(t_csi) ||
                     m_AVND_COMP_CSI_CURR_ASSIGN_STATE_IS_UNASSIGNED(t_csi)) {
              if (!t_csi->suspending_assignment) {
                opr_done = false;
                break;
              }
            }
          }
        }
      }
    }
  }

  // reset suspending_assignment flag if opr_done
  if (opr_done) {
    for (curr_si =
             (si) ? si
                  : (AVND_SU_SI_REC *)m_NCS_DBLIST_FIND_FIRST(&su->si_list);
         curr_si; curr_si = (si) ? 0
                                 : (AVND_SU_SI_REC *)m_NCS_DBLIST_FIND_NEXT(
                                       &curr_si->su_dll_node)) {
      for (curr_csi =
               (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_FIRST(&curr_si->csi_list);
           curr_csi; curr_csi = (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_NEXT(
                         &curr_csi->si_dll_node)) {
        if (curr_csi->suspending_assignment)
          curr_csi->suspending_assignment = false;
      }
    }
  }

  TRACE_LEAVE2("%u", opr_done);
  return opr_done;
}

/**
 * Get a higher ranked SI (if any)
 * @param si
 *
 * @return AVND_SU_SI_REC*
 */
static AVND_SU_SI_REC *get_higher_ranked_si(const AVND_SU_SI_REC *si) {
  AVND_SU_SI_REC *tmp = avnd_silist_getprev(si);

  if (tmp) {
    for (; tmp && (tmp->rank == si->rank); tmp = avnd_silist_getprev(tmp))
      ;
  }

  return tmp;
}

/**
 * Are all SIs of the same rank as the passed SI removed/unassigned?
 *
 * @param si
 *
 * @return bool
 */
static bool all_sis_atrank_removed(const AVND_SU_SI_REC *si) {
  const AVND_SU_SI_REC *tmp;

  /* search forwards */
  for (tmp = si; tmp && (tmp->rank == si->rank);
       tmp = avnd_silist_getnext(tmp)) {
    if (!m_AVND_SU_SI_CURR_ASSIGN_STATE_IS_REMOVED(tmp)) {
      TRACE("%s not removed", tmp->name.c_str());
      return false;
    }
  }

  /* search backwards */
  for (tmp = si; tmp && (tmp->rank == si->rank);
       tmp = avnd_silist_getprev(tmp)) {
    if (!m_AVND_SU_SI_CURR_ASSIGN_STATE_IS_REMOVED(tmp)) {
      TRACE("%s not removed", tmp->name.c_str());
      return false;
    }
  }

  TRACE("all SIs at rank %u removed", si->rank);
  return true;
}

/**
 * @brief  This function resets surestart escalation or restart admin op params
 *         for a restartable su. For a non restartable SU, it will resume
 * surestart escalation or recovery when assignments are gracefully removed.
 * @param  ptr to su.
 */
static void su_finish_suRestart_escalation_or_admin_op(AVND_SU *su) {
  TRACE_ENTER2("'%s'", su->name.c_str());
  if ((su_all_comps_restartable(*su) == true) ||
      ((is_any_non_restartable_comp_assigned(*su) == false) &&
       (!m_AVND_SU_IS_FAILED(su)))) {
    bool are_si_assigned;
    TRACE(
        "All the components restartable or non restartable comps are not assigned.");
    m_AVND_SU_ARE_ALL_SI_ASSIGNED(su, are_si_assigned);
    if (true == are_si_assigned) {
      TRACE(
          "All SIs are reassigned after suRestart escalation or admin op,"
          " so resetting the suRestart flag.");
      reset_suRestart_flag(su);
    }
  } else {
    TRACE(
        "SU has atleast one non-restartable (DisbaleRestart = 1) assigned component");
    if (su->si_list.n_nodes > 0) {
      TRACE(
          "Can't resume termination/clean up of components as some of them"
          " still have assignments.");
    } else {
      TRACE(
          "Graceful removal of assignments from components of this"
          " SU completed.");
      if (m_AVND_SU_IS_PREINSTANTIABLE(su)) {
        TRACE("PI SU");
        if (su->pres == SA_AMF_PRESENCE_TERMINATING) {
          TRACE(
              "PI SU: Resume termination/clean up of component honoring"
              " their instantiation level in reverse order.");
          avnd_su_pres_fsm_run(avnd_cb, su, 0, AVND_SU_PRES_FSM_EV_RESTART);
        }
        if (su->pres == SA_AMF_PRESENCE_UNINSTANTIATED) {
          TRACE("PI SU: Instantiate SU honoring instantiation level");
          m_AVND_SU_FAILED_RESET(su);
          reset_suRestart_flag(su);
          avnd_su_pres_fsm_run(avnd_cb, su, 0, AVND_SU_PRES_FSM_EV_INST);
        }
      } else {
        TRACE("NPI SU");
        m_AVND_SU_FAILED_RESET(su);
        reset_suRestart_flag(su);
        if (su->pres == SA_AMF_PRESENCE_UNINSTANTIATED)
          avnd_di_oper_send(avnd_cb, su, 0);
      }
    }
  }
  TRACE_LEAVE();
}
/****************************************************************************
  Name          : avnd_su_si_oper_done

  Description   : This routine is invoked to indicate that the SI state
                  assignment / removal that was pending on a SI is completed.
                  It is generated when all the CSIs are assigned / removed OR
                  when SU FSM transitions to INSTANTIATED / UNINSTANTIATED.
                  AvD is informed that this SU-SI was successfully assigned /
                  removed.

  Arguments     : cb - ptr to AvND control block
                  su - ptr to the AvND SU
                  si - ptr to the si record (if 0, it indicates that the
                       assign/remove operation was done for all the SIs
                       belonging to the SU)

  Return Values : NCSCC_RC_SUCCESS / NCSCC_RC_FAILURE

  Notes         : None
******************************************************************************/
uint32_t avnd_su_si_oper_done(AVND_CB *cb, AVND_SU *su, AVND_SU_SI_REC *si) {
  AVND_SU_SI_REC *curr_si = 0;
  AVND_COMP_CSI_REC *curr_csi = 0, *t_csi = 0;
  uint32_t rc = NCSCC_RC_SUCCESS;
  bool opr_done;

  TRACE_ENTER2("'%s' '%s'", su->name.c_str(), si ? si->name.c_str() : nullptr);

  /* mark the individual sis after operation is completed for all the SI's */
  opr_done = susi_operation_in_progress(su, si);

  for (curr_si = (si) ? si
                      : (AVND_SU_SI_REC *)m_NCS_DBLIST_FIND_FIRST(&su->si_list);
       curr_si; curr_si = (si) ? 0
                               : (AVND_SU_SI_REC *)m_NCS_DBLIST_FIND_NEXT(
                                     &curr_si->su_dll_node)) {
    /* mark the si assigned / removed */
    TRACE("SI '%s'", curr_si->name.c_str());

    if (opr_done) {
      if (m_AVND_SU_SI_CURR_ASSIGN_STATE_IS_ASSIGNING(curr_si)) {
        m_AVND_SU_SI_CURR_ASSIGN_STATE_SET(curr_si,
                                           AVND_SU_SI_ASSIGN_STATE_ASSIGNED);
        LOG_NO("Assigned '%s' %s to '%s'", curr_si->name.c_str(),
               ha_state[curr_si->curr_state], su->name.c_str());
      } else if (m_AVND_SU_SI_CURR_ASSIGN_STATE_IS_REMOVING(curr_si)) {
        m_AVND_SU_SI_CURR_ASSIGN_STATE_SET(curr_si,
                                           AVND_SU_SI_ASSIGN_STATE_REMOVED);
        LOG_NO("Removed '%s' from '%s'", curr_si->name.c_str(),
               su->name.c_str());
      } else {
        LOG_CR("current si name ='%s'", curr_si->name.c_str());
        LOG_CR(
            "SI: curr_assign_state = %u, prv_assign_state = %u, curr_state = %u, prv_state = %u",
            curr_si->curr_assign_state, curr_si->prv_assign_state,
            curr_si->curr_state, curr_si->prv_state);
        osafassert(0);
      }
    }
    /*
     * It is possible that the su fsm for npi su is never triggered (this
     * happens when the si-state change does not lead to su instantition or
     * termination). Mark the corresponding csis assigned/removed.
     */
    if (!m_AVND_SU_IS_PREINSTANTIABLE(su)) {
      TRACE("SU is NPI");
      for (curr_csi =
               (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_FIRST(&curr_si->csi_list);
           curr_csi; curr_csi = (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_NEXT(
                         &curr_csi->si_dll_node)) {
        TRACE("CSI '%s'", curr_csi->name.c_str());
        if (m_AVND_SU_SI_CURR_ASSIGN_STATE_IS_ASSIGNED(curr_si)) {
          TRACE("Setting CSI '%s' assigned", curr_csi->name.c_str());
          m_AVND_COMP_CSI_CURR_ASSIGN_STATE_SET(
              curr_csi, AVND_COMP_CSI_ASSIGN_STATE_ASSIGNED);
        } else if (m_AVND_SU_SI_CURR_ASSIGN_STATE_IS_REMOVED(curr_si))
          m_AVND_COMP_CSI_CURR_ASSIGN_STATE_SET(
              curr_csi, AVND_COMP_CSI_ASSIGN_STATE_REMOVED);
      }
    }
  } /* for */

  /* Inform AMFD when assign are over. During surestart only for non restartable
     SU we need to inform AMFD.*/
  if (opr_done &&
      ((!(m_AVND_SU_IS_RESTART(su))) ||
       (m_AVND_SU_IS_RESTART(su) && (su_all_comps_restartable(*su) == false) &&
        (is_any_non_restartable_comp_assigned(*su) == true))) &&
      (is_no_assignment_due_to_escalations(su) == false)) {
    rc = avnd_di_susi_resp_send(cb, su, m_AVND_SU_IS_ALL_SI(su) ? nullptr : si);
    if (NCSCC_RC_SUCCESS != rc) goto done;
  }

  if (si && (cb->term_state == AVND_TERM_STATE_OPENSAF_SHUTDOWN_INITIATED)) {
    (void)avnd_evt_last_step_term_evh(cb, nullptr);
  } else if (si &&
             (cb->term_state == AVND_TERM_STATE_OPENSAF_SHUTDOWN_STARTED) &&
             m_AVND_SU_SI_CURR_ASSIGN_STATE_IS_REMOVED(si)) {
    if (all_sis_atrank_removed(si)) {
      AVND_SU_SI_REC *tmp = get_higher_ranked_si(si);

      if (tmp != nullptr) {
        uint32_t sirank = tmp->rank;

        for (; tmp && (tmp->rank == sirank); tmp = avnd_silist_getprev(tmp)) {
          uint32_t rc = avnd_su_si_remove(cb, tmp->su, tmp);
          osafassert(rc == NCSCC_RC_SUCCESS);
        }
      } else {
        LOG_NO("Removed assignments from AMF components");
        if (m_NCS_NODE_ID_FROM_MDS_DEST(cb->active_avd_adest) != ncs_get_node_id()) {
          avnd_last_step_clean(cb);
        } else {
          avnd_di_node_down_msg_send(cb);
        }
      }
    }
  }

  /* Now correct si state if single csi was being removed. For the rest of csi
     assigned, si should be in Assigned state*/
  if ((si) && (AVSV_SUSI_ACT_DEL == si->single_csi_add_rem_in_si) &&
      (m_AVND_SU_SI_CURR_ASSIGN_STATE_IS_REMOVED(si))) {
    m_AVND_SU_SI_CURR_ASSIGN_STATE_SET(si, AVND_SU_SI_ASSIGN_STATE_ASSIGNED);
    /* Also, we need to remove links of si, csi and comp. */
    /* We have to remove single csi. */
    for (t_csi = (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_FIRST(&si->csi_list);
         t_csi; t_csi = (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_NEXT(
                    &t_csi->si_dll_node)) {
      if (AVSV_SUSI_ACT_DEL == t_csi->single_csi_add_rem_in_si) {
        break;
      }
    }
    osafassert(t_csi);
    // free the csi attributes
    // AVSV_ATTR_NAME_VAL variables
    // are malloc'ed, use free()
    free(t_csi->attrs.list);

    m_AVND_SU_SI_CSI_REC_REM(*si, *t_csi);
    m_AVND_COMPDB_REC_CSI_REM(*(t_csi->comp), *t_csi);
    delete t_csi;
  }
  /* Reset the single add/del */
  if (si) si->single_csi_add_rem_in_si = AVSV_SUSI_ACT_BASE;

  /* finally delete the si(s) if they are removed */
  curr_si = (si) ? si : (AVND_SU_SI_REC *)m_NCS_DBLIST_FIND_FIRST(&su->si_list);
  if ((curr_si != nullptr) &&
      m_AVND_SU_SI_CURR_ASSIGN_STATE_IS_REMOVED(curr_si)) {
    rc = (si) ? avnd_su_si_rec_del(cb, su->name, si->name)
              : avnd_su_si_del(cb, su->name);
    if (NCSCC_RC_SUCCESS != rc) goto done;

    /*
     * Removal signifies an end to the recovery phase. Initiate repair
     * unless a NODE level or su-failover recovery action is in progress.
     * Also repair must be done after informing to AMFD about comp-failover and
     * after removal of assignment.
     */
    if (m_AVND_SU_IS_FAILED(su) && !su->si_list.n_nodes &&
        (!m_AVND_SU_IS_FAILOVER(su)) && (su->sufailover == false) &&
        (cb->oper_state == SA_AMF_OPERATIONAL_ENABLED) &&
        (!m_AVND_SU_IS_RESTART(su)))
      rc = avnd_err_su_repair(cb, su);
  }

  if ((nullptr == si) &&
      (cb->term_state == AVND_TERM_STATE_OPENSAF_SHUTDOWN_INITIATED)) {
    (void)avnd_evt_last_step_term_evh(cb, nullptr);
  } else if ((nullptr == si) &&
             (cb->term_state == AVND_TERM_STATE_OPENSAF_SHUTDOWN_STARTED)) {
    AVND_SU_SI_REC *tmp = avnd_silist_getlast();

    if (tmp != nullptr) {
      uint32_t sirank = tmp->rank;

      for (; tmp && (tmp->rank == sirank); tmp = avnd_silist_getprev(tmp)) {
        uint32_t rc = avnd_su_si_remove(cb, tmp->su, tmp);
        osafassert(rc == NCSCC_RC_SUCCESS);
      }
    } else {
      LOG_NO("Removed assignments from AMF components");
      if (m_NCS_NODE_ID_FROM_MDS_DEST(cb->active_avd_adest) != ncs_get_node_id()) {
        avnd_last_step_clean(cb);
      } else {
        avnd_di_node_down_msg_send(cb);
      }
    }
  }

  /*
   * reset the su-restart flag if all the sis are in assigned
   * state (signifying the end of su-restart phase)
   */
  if (m_AVND_SU_IS_RESTART(su)) {
    TRACE("SU restart due to escalation or admin op going on.");
    su_finish_suRestart_escalation_or_admin_op(su);
  }
  /* finally initiate buffered assignments, if any */
  rc = avnd_su_siq_prc(cb, su);

done:
  TRACE_LEAVE2("%u", rc);
  return rc;
}

/****************************************************************************
  Name          : avnd_su_si_unmark

  Description   : This routine stops the SU-SI assignment & brings the all
                  the SIs (including the corresponding CSIs) to the initial
                  unassigned state.

  Arguments     : cb - ptr to the AvND control block
                  su - ptr to the comp

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avnd_su_si_unmark(AVND_CB *cb, AVND_SU *su) {
  AVND_SU_SI_REC *si = 0;
  uint32_t rc = NCSCC_RC_SUCCESS;
  TRACE_ENTER2("'%s'", su->name.c_str());

  /* scan the su-si list & unmark the sis */
  for (si = (AVND_SU_SI_REC *)m_NCS_DBLIST_FIND_FIRST(&su->si_list); si;
       si = (AVND_SU_SI_REC *)m_NCS_DBLIST_FIND_NEXT(&si->su_dll_node)) {
    rc = avnd_su_si_rec_unmark(cb, su, si);
    if (NCSCC_RC_SUCCESS != rc) break;
  } /* for */

  TRACE_LEAVE();
  return rc;
}

/****************************************************************************
  Name          : avnd_su_si_rec_unmark

  Description   : This routine unmarks a particular SU-SI assignment.

  Arguments     : cb - ptr to the AvND control block
                  su - ptr to the comp
                  si - ptr to the si rec

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avnd_su_si_rec_unmark(AVND_CB *cb, AVND_SU *su, AVND_SU_SI_REC *si) {
  AVND_COMP_CSI_REC *csi = 0;
  uint32_t rc = NCSCC_RC_SUCCESS;
  TRACE_ENTER2("'%s' : '%s'", su->name.c_str(), si->name.c_str());

  /* reset the prv state & update the new assign-state */
  si->prv_state = static_cast<SaAmfHAStateT>(0);
  si->prv_assign_state = static_cast<AVND_SU_SI_ASSIGN_STATE>(0);
  m_AVND_SU_SI_CURR_ASSIGN_STATE_SET(si, AVND_SU_SI_ASSIGN_STATE_UNASSIGNED);
  TRACE("Unmarking SU-SI assignment,SU ='%s' : SI=%s", si->su_name.c_str(),
        si->name.c_str());

  /* scan the si-csi list & unmark the csis */
  for (csi = (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_FIRST(&si->csi_list); csi;
       csi = (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_NEXT(&csi->si_dll_node)) {
    csi->prv_assign_state = static_cast<AVND_COMP_CSI_ASSIGN_STATE>(0);
    m_AVND_COMP_CSI_CURR_ASSIGN_STATE_SET(
        csi, AVND_COMP_CSI_ASSIGN_STATE_UNASSIGNED);
    TRACE("Unmarking CSI'%s' corresponding to the si'%s'", csi->name.c_str(),
          si->name.c_str());

    /* remove any pending callbacks for pi comps */
    if (m_AVND_COMP_TYPE_IS_PREINSTANTIABLE(csi->comp)) {
      TRACE("Deleting callbacks for the PI component:'%s'",
            csi->comp->name.c_str());
      avnd_comp_cbq_csi_rec_del(cb, csi->comp, csi->name);
    }
  } /* End for */

  TRACE_LEAVE();
  return rc;
}

/***************************************************************************
 ******  S U   P R E S E N C E   F S M   P O R T I O N   S T A R T S  ******
 ***************************************************************************/

/****************************************************************************
  Name          : avnd_evt_avd_su_pres_msg

  Description   : This routine processes SU instantiate or terminate trigger
                  as per the wishes of AvD.

  Arguments     : cb  - ptr to the AvND control block
                  evt - ptr to the AvND event

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avnd_evt_avd_su_pres_evh(AVND_CB *cb, AVND_EVT *evt) {
  AVSV_D2N_PRESENCE_SU_MSG_INFO *info = 0;
  AVND_SU *su = 0;
  uint32_t rc = NCSCC_RC_SUCCESS;

  TRACE_ENTER();

  /* dont process unless AvD is up */
  if (!m_AVND_CB_IS_AVD_UP(cb)) {
    TRACE("AVD is not yet up");
    goto done;
  }

  /* since AMFND is going down no need to process SU instantiate/terminate msg
   * because SU instantiate will cause component information to be read from
   * IMMND and IMMND might have been already terminated and in that case AMFND
   * will osafassert */
  if (m_AVND_IS_SHUTTING_DOWN(cb)) {
    TRACE("AMFND is in SHUTDOWN state");
    goto done;
  }

  info = &evt->info.avd->msg_info.d2n_prsc_su;

  avnd_msgid_assert(info->msg_id);
  cb->rcv_msg_id = info->msg_id;

  /* get the su */
  su = avnd_sudb_rec_get(cb->sudb, Amf::to_string(&info->su_name));
  if (!su) {
    TRACE("SU'%s', not found in DB", osaf_extended_name_borrow(&info->su_name));
    goto done;
  }

  if (info->term_state) { /* => terminate the su */
    /* Stop saAmfSGSuRestartProb timer if started */
    if (su->su_err_esc_level == AVND_ERR_ESC_LEVEL_1) {
      tmr_su_err_esc_stop(cb, su);
    }
    /* Stop saAmfSGCompRestartProb timer if started */
    else if (su->su_err_esc_level == AVND_ERR_ESC_LEVEL_0) {
      tmr_comp_err_esc_stop(cb, su);
    } else
      TRACE("su_err_esc_tmr is started in wrong su_err_esc_level(%d)",
            su->su_err_esc_level);

    TRACE("SU term state is set to true");
    /* no si must be assigned to the su */
    osafassert(!m_NCS_DBLIST_FIND_FIRST(&su->si_list));

    /* Mark SU as terminated by admn operation */
    TRACE("Marking SU as terminated by admin operation");
    m_AVND_SU_ADMN_TERM_SET(su);

    /* delete all the curr info on su & comp */
    rc = avnd_su_curr_info_del(cb, su);

    /* now terminate the su */
    if (m_AVND_SU_IS_PREINSTANTIABLE(su)) {
      /* trigger su termination for pi su */
      TRACE("SU termination for PI SU, running the SU presence state FSM");
      rc = avnd_su_pres_fsm_run(cb, su, 0, AVND_SU_PRES_FSM_EV_TERM);
      if (NCSCC_RC_SUCCESS != rc) goto done;
    }
  } else { /* => instantiate the su */
    // No need to wait for headless sync if there is no application SUs
    // initiated. This is known because we are receiving su_pres message
    // for NCS SUs
    if (su->is_ncs == true) cb->amfd_sync_required = false;

    AVND_EVT *evt_ir = 0;
    TRACE("Sending to Imm thread.");
    evt_ir = avnd_evt_create(cb, AVND_EVT_IR, 0, nullptr, &info->su_name, 0, 0);
    rc = m_NCS_IPC_SEND(&ir_cb.mbx, evt_ir, evt_ir->priority);
    if (NCSCC_RC_SUCCESS != rc)
      LOG_CR("AvND send event to IR mailbox failed, type = %u", evt_ir->type);
    /* if failure, free the event */
    if (NCSCC_RC_SUCCESS != rc && evt_ir) avnd_evt_destroy(evt_ir);
  }

done:
  TRACE_LEAVE2("%u", rc);
  return rc;
}

/**
 * Check if all components have been terminated in the su.
 * @param su
 * @return bool
 */
bool all_comps_terminated_in_su(const AVND_SU *su, bool all_final_pres_states) {
  AVND_COMP *comp;

  for (comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
           m_NCS_DBLIST_FIND_FIRST(&su->comp_list));
       comp; comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
                 m_NCS_DBLIST_FIND_NEXT(&comp->su_dll_node))) {
    if ((all_final_pres_states == false) &&
        (comp->pres != SA_AMF_PRESENCE_UNINSTANTIATED)) {
      TRACE("'%s' not terminated, pres.st=%u", comp->name.c_str(), comp->pres);
      return false;
    }
    if ((all_final_pres_states == true) &&
        (comp->pres != SA_AMF_PRESENCE_UNINSTANTIATED) &&
        (comp->pres != SA_AMF_PRESENCE_INSTANTIATION_FAILED) &&
        (comp->pres != SA_AMF_PRESENCE_TERMINATION_FAILED)) {
      TRACE("'%s' not terminated, pres.st=%u", comp->name.c_str(), comp->pres);
      return false;
    }
  }

  return true;
}

static void perform_pending_nodeswitchover() {
  bool nodeswitchover = true;

  /* Reverify if nodeswitchover is really pending */
  if ((avnd_cb->term_state != AVND_TERM_STATE_NODE_SWITCHOVER_STARTED) ||
      (avnd_cb->oper_state != SA_AMF_OPERATIONAL_DISABLED))
    return;

  AVND_COMP *comp;
  AVND_SU *su = avnd_cb->failed_su;
  for (comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
           m_NCS_DBLIST_FIND_FIRST(&su->comp_list));
       comp; comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
                 m_NCS_DBLIST_FIND_NEXT(&comp->su_dll_node))) {
    if ((comp->pres == SA_AMF_PRESENCE_INSTANTIATING) ||
        (comp->pres == SA_AMF_PRESENCE_TERMINATING) ||
        (comp->pres == SA_AMF_PRESENCE_RESTARTING) ||
        (comp->pres == SA_AMF_PRESENCE_TERMINATION_FAILED)) {
      nodeswitchover = false;
      break;
    }
  }

  if (nodeswitchover == true) {
    /* Now send nodeswitchover request to AMFD as cleanup of failed component is
       completed in faulted SU.
     */

    LOG_NO("Informing director of Nodeswitchover");
    if (su->sufailover == false) {
      uint32_t rc = avnd_di_oper_send(avnd_cb, su, SA_AMF_NODE_SWITCHOVER);
      osafassert(NCSCC_RC_SUCCESS == rc);
      avnd_cb->failed_su = nullptr;
    }
  }
}

/****************************************************************************
  Name          : avnd_su_pres_fsm_run

  Description   : This routine runs the service presence state FSM. It also
                  processes the change in su presence state.

  Arguments     : cb   - ptr to the AvND control block
                  su   - ptr to the su
                  comp - ptr to the comp that generated the ev (may be null)
                  ev   - fsm event

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avnd_su_pres_fsm_run(AVND_CB *cb, AVND_SU *su, AVND_COMP *comp,
                              AVND_SU_PRES_FSM_EV ev) {
  SaAmfPresenceStateT prv_st, final_st;
  uint32_t rc = NCSCC_RC_SUCCESS;
  TRACE_ENTER2("'%s'", su->name.c_str());

  /* get the prv presence state */
  prv_st = su->pres;

  TRACE_1(
      "Entering SU presence state FSM: current state: %u, event: %u, su name:%s",
      prv_st, ev, su->name.c_str());

  /* run the fsm */
  if (0 != avnd_su_pres_fsm[prv_st - 1][ev - 1]) {
    rc = avnd_su_pres_fsm[prv_st - 1][ev - 1](cb, su, comp);
    if (NCSCC_RC_SUCCESS != rc) goto done;
  }

  /* get the final presence state */
  final_st = su->pres;

  TRACE_1("Exited SU presence state FSM: New State = %u", final_st);

  /* TODO: This code related to sufailover handling will be moved to
     avnd_su_pres_st_chng_prc() once NPI SU support is more proper. */
  if (((sufailover_in_progress(su)) ||
       (sufailover_during_nodeswitchover(su))) &&
      (all_comps_terminated_in_su(su))) {
    /* Since all components got successfully terminated, finish sufailover at
       amfnd by deleting SUSIs at amfnd and informing amfd about sufailover.*/
    LOG_NO("Terminated all components in '%s'", su->name.c_str());
    if (cb->term_state == AVND_TERM_STATE_NODE_SWITCHOVER_STARTED) {
      LOG_NO("Informing director of Nodeswitchover");
      rc = avnd_di_oper_send(avnd_cb, su, SA_AMF_NODE_SWITCHOVER);
    } else {
      LOG_NO("Informing director of sufailover");
      rc = avnd_di_oper_send(avnd_cb, su, AVSV_ERR_RCVR_SU_FAILOVER);
    }
    osafassert(NCSCC_RC_SUCCESS == rc);
    avnd_su_si_del(avnd_cb, su->name);
    if (!m_AVND_SU_IS_PREINSTANTIABLE(su))
      avnd_su_pres_state_set(cb, su, SA_AMF_PRESENCE_UNINSTANTIATED);
    goto done;
  } else if ((cb->term_state == AVND_TERM_STATE_NODE_SWITCHOVER_STARTED) &&
             (cb->oper_state == SA_AMF_OPERATIONAL_DISABLED) &&
             (su->sufailover == false) && (cb->failed_su != nullptr)) {
    perform_pending_nodeswitchover();
  }

  /* process state change */
  if (prv_st != final_st)
    rc = avnd_su_pres_st_chng_prc(cb, su, prv_st, final_st);

done:
  TRACE_LEAVE2("%u", rc);
  return rc;
}

/**
 * @brief  Reset flags when a NPI SU moves from INSTANTIATING/RESTARTING
 *               to INSTANTIATED state.
 * @param  su
 * @return NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE
 */

static uint32_t npi_su_instantiating_to_instantiated(AVND_SU *su) {
  uint32_t rc = NCSCC_RC_SUCCESS;
  TRACE_ENTER();

  if (m_AVND_SU_IS_RESTART(su)) {
    m_AVND_SU_FAILED_RESET(su);
    reset_suRestart_flag(su);
    su->admin_op_Id = static_cast<SaAmfAdminOperationIdT>(0);
  } else {
    AVND_SU_SI_REC *si = 0;
    si = (AVND_SU_SI_REC *)m_NCS_DBLIST_FIND_FIRST(&su->si_list);
    osafassert(si);
    rc = avnd_su_si_oper_done(avnd_cb, su, m_AVND_SU_IS_ALL_SI(su) ? 0 : si);
    m_AVND_SU_ALL_SI_RESET(su);
    TRACE("SI Assignment succeeded, generating si-oper done indication");
  }
  TRACE_LEAVE2("rc:%d", rc);
  return rc;
}

/**
 * @brief  Reset flags when a PI SU moves from INSTANTIATING/RESTARTING
 *              to INSTANTIATED state.
 * @param  su
 * @return NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE
 */
static uint32_t pi_su_instantiating_to_instantiated(AVND_SU *su) {
  uint32_t rc = NCSCC_RC_SUCCESS;
  TRACE_ENTER();

  /* A SU can be restarted because all components faulted with component
     restart recovery policy or because of surestart escalation. */

  /* reset the su failed flag */
  if (m_AVND_SU_IS_FAILED(su)) {
    m_AVND_SU_FAILED_RESET(su);
  }
  if (m_AVND_SU_IS_RESTART(su)) {
    /* reset the su failed flag & set the oper state to enabled */
    m_AVND_SU_OPER_STATE_SET(su, SA_AMF_OPERATIONAL_ENABLED);
    TRACE("Setting the Oper state to Enabled");
    /*
     * reassign all the sis...
     * it's possible that the si was never assigned. send su-oper
     * enable msg instead.
     */
    if (su->si_list.n_nodes)
      rc = avnd_su_si_reassign(avnd_cb, su);
    else {
      rc = avnd_di_oper_send(avnd_cb, su, 0);
      reset_suRestart_flag(su);
    }
    su->admin_op_Id = static_cast<SaAmfAdminOperationIdT>(0);
  } else {
    bool is_en;
    /* determine the su oper state. if enabled, inform avd. */
    m_AVND_SU_IS_ENABLED(su, is_en);
    if (true == is_en) {
      TRACE("SU oper state is enabled");
      m_AVND_SU_OPER_STATE_SET(su, SA_AMF_OPERATIONAL_ENABLED);
      rc = avnd_di_oper_send(avnd_cb, su, 0);
    } else
      TRACE("SU oper state is disabled");
  }
  TRACE_LEAVE();
  return rc;
}
/****************************************************************************
  Name          : avnd_su_pres_st_chng_prc

  Description   : This routine processes the change in  the presence state
                  resulting due to running the SU presence state FSM.

  Arguments     : cb       - ptr to the AvND control block
                  su       - ptr to the su
                  prv_st   - previous presence state
                  final_st - final presence state

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avnd_su_pres_st_chng_prc(AVND_CB *cb, AVND_SU *su,
                                  SaAmfPresenceStateT prv_st,
                                  SaAmfPresenceStateT final_st) {
  AVND_SU_SI_REC *si = 0;
  bool is_en;
  uint32_t rc = NCSCC_RC_SUCCESS;

  TRACE_ENTER2("'%s' %s => %s", su->name.c_str(), presence_state[prv_st],
               presence_state[final_st]);

  /* pi su */
  if (m_AVND_SU_IS_PREINSTANTIABLE(su)) {
    TRACE("PI SU :'%s'", su->name.c_str());
    /* instantiating/restarting -> instantiated */
    if (((SA_AMF_PRESENCE_INSTANTIATING == prv_st) ||
         (SA_AMF_PRESENCE_RESTARTING == prv_st)) &&
        (SA_AMF_PRESENCE_INSTANTIATED == final_st) &&
        (su_all_pi_comps_instantiated(su) == true)) {
      rc = pi_su_instantiating_to_instantiated(su);
      if (NCSCC_RC_SUCCESS != rc) goto done;
    }
    /* terminating -> instantiated */
    if ((SA_AMF_PRESENCE_TERMINATING == prv_st) &&
        (SA_AMF_PRESENCE_INSTANTIATED == final_st)) {
      TRACE("SU Terminating -> Instantiated");
      /* reset the su failed flag */
      if (m_AVND_SU_IS_FAILED(su)) {
        m_AVND_SU_FAILED_RESET(su);
      }

      /* determine su oper state. if enabled, inform avd. */
      m_AVND_SU_IS_ENABLED(su, is_en);
      if (true == is_en) {
        TRACE("SU oper state is enabled");
        m_AVND_SU_OPER_STATE_SET(su, SA_AMF_OPERATIONAL_ENABLED);
        rc = avnd_di_oper_send(cb, su, 0);
        if (NCSCC_RC_SUCCESS != rc) goto done;
      } else
        TRACE("SU oper state is disabled");
    }

    /* terminating -> uninstantiated */
    if ((SA_AMF_PRESENCE_TERMINATING == prv_st) &&
        (SA_AMF_PRESENCE_UNINSTANTIATED == final_st)) {
      TRACE("SU Terminating -> Uninstantiated");
      if (sufailover_in_progress(su)) {
        /*Do not reset any flag, this will be done as a part of repair.*/
      } else {
        if (!m_AVND_SU_IS_RESTART(su) && m_AVND_SU_IS_FAILED(su) &&
            (su->si_list.n_nodes == 0)) {
          m_AVND_SU_FAILED_RESET(su);
        }
        if (m_AVND_SU_IS_RESTART(su) &&
            ((su_all_comps_restartable(*su) == true) ||
             ((su_all_comps_restartable(*su) == false) &&
              (is_any_non_restartable_comp_assigned(*su) == false)) ||
             ((m_AVND_SU_IS_FAILOVER(su)) && (su->si_list.n_nodes == 0)))) {
          /*
             It means all comps are terminated in surestart recovery or
             admin op. For non restartable SU with no non restartable comp
             assigned, instantiation of SU will started from here.
             Now instantiate SU honoring instantiation level.
           */
          rc = avnd_su_pres_fsm_run(cb, su, 0, AVND_SU_PRES_FSM_EV_INST);
          if (NCSCC_RC_SUCCESS != rc) goto done;
        }
      }
      goto done;
    }
    /* instantiating -> inst-failed */
    if ((SA_AMF_PRESENCE_INSTANTIATING == prv_st) &&
        (SA_AMF_PRESENCE_INSTANTIATION_FAILED == final_st)) {
      TRACE("SU Instantiating -> Instantiation Failed");
      /* send the su-oper state msg (to indicate that instantiation failed) */
      m_AVND_SU_OPER_STATE_SET(su, SA_AMF_OPERATIONAL_DISABLED);
      rc = avnd_di_oper_send(cb, su, SA_AMF_COMPONENT_FAILOVER);
      if (NCSCC_RC_SUCCESS != rc) goto done;
    }

    /* instantiating -> term-failed */
    if ((SA_AMF_PRESENCE_INSTANTIATING == prv_st) &&
        (SA_AMF_PRESENCE_TERMINATION_FAILED == final_st)) {
      TRACE("SU Instantiating -> Termination Failed");
      m_AVND_SU_OPER_STATE_SET(su, SA_AMF_OPERATIONAL_DISABLED);
      /* Don't send su-oper state msg, just update su oper state
       * AMF has lost control over this component and the operator needs
       * to repair this node. Failover is not possible in this state. */
      avnd_di_uns32_upd_send(AVSV_SA_AMF_SU, saAmfSUOperState_ID, su->name,
                             su->oper);
    }

    /* instantiated/restarting -> inst-failed */
    if (((SA_AMF_PRESENCE_INSTANTIATED == prv_st) ||
         (SA_AMF_PRESENCE_RESTARTING == prv_st)) &&
        (SA_AMF_PRESENCE_INSTANTIATION_FAILED == final_st)) {
      TRACE("SU Instantiated/Restarting -> Instantiation Failed");

      /* send the su-oper state msg (to indicate that instantiation failed) */
      if (m_AVND_SU_OPER_STATE_IS_ENABLED(su)) {
        TRACE("SU oper state is enabled");
        m_AVND_SU_OPER_STATE_SET(su, SA_AMF_OPERATIONAL_DISABLED);
        /*No re-assignments and further escalation happens in INST_FAILED state,
          so reset suRestart flag.*/
        if (isRestartSet(su)) reset_suRestart_flag(su);
        // Ask AMFD to remove assignments.
        rc = avnd_di_oper_send(cb, su, SA_AMF_COMPONENT_FAILOVER);
        if (NCSCC_RC_SUCCESS != rc) goto done;
      } else
        TRACE("SU oper state is disabled");
    }

    /* terminating/restarting -> term-failed */
    if (((prv_st == SA_AMF_PRESENCE_RESTARTING) ||
         (SA_AMF_PRESENCE_TERMINATING == prv_st)) &&
        (SA_AMF_PRESENCE_TERMINATION_FAILED == final_st)) {
      TRACE("Terminating/Restarting -> Termination Failed");
      if (sufailover_in_progress(su)) {
        /*Do not reset any flag, this will be done as a part of repair.*/
        rc = avnd_di_oper_send(cb, su, AVSV_ERR_RCVR_SU_FAILOVER);
        osafassert(NCSCC_RC_SUCCESS == rc);
        avnd_su_si_del(avnd_cb, su->name);
        goto done;
      }
      m_AVND_SU_OPER_STATE_SET(su, SA_AMF_OPERATIONAL_DISABLED);
      /* inform AvD about oper state change, in case prev state was TERMINATING.
         In RESTARTING case, comp FSM triggers comp-failover.*/
      if (prv_st == SA_AMF_PRESENCE_TERMINATING)
        rc = avnd_di_oper_send(cb, su, SA_AMF_COMPONENT_FAILOVER);
      if (NCSCC_RC_SUCCESS != rc) goto done;
    }
    /*instantiated-> term-failed*/
    if ((prv_st == SA_AMF_PRESENCE_INSTANTIATED) &&
        (final_st == SA_AMF_PRESENCE_TERMINATION_FAILED)) {
      TRACE("Instantiated -> Termination Failed");
      /*
         This state transition of SU can happen when:
         -its one NPI comp moves to TERM_FAILED state. There can
          be two subcases here: a)assigned NPI comp faults or b)it
          faults during fresh assignments during instantiation phase.
         -its restartable PI comp moves to TERM_FAILED state. There
          can be two subcases here:a)assigned PI comp fault or b)
          it faults during fresh assignments in CSI SET callback.
         In these cases SU moves directly from INSTANTIATED to TERM_FAILED
         state.

         AMFND should respond to AMFD for su-failover only when SU moves
         to TERM_FAILED state during fresh assignments.
       */
      if ((su->si_list.n_nodes != 0) && (m_AVND_SU_IS_ASSIGN_PEND(su)) &&
          (su->avnd_su_check_sis_previous_assign_state(
               AVND_SU_SI_ASSIGN_STATE_UNASSIGNED) == true)) {
        m_AVND_SU_OPER_STATE_SET(su, SA_AMF_OPERATIONAL_DISABLED);
        if (all_comps_terminated_in_su(su, true) == true) {
          TRACE_2("Informing AMFD of su-failover");
          rc = avnd_di_oper_send(cb, su, AVSV_ERR_RCVR_SU_FAILOVER);
          avnd_su_si_del(avnd_cb, su->name);
        } else {
          // Some PI comps are still terminating. Try to terminate NPIs.
          for (AVND_COMP *comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
                   m_NCS_DBLIST_FIND_FIRST(&su->comp_list));
               comp; comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
                         m_NCS_DBLIST_FIND_NEXT(&comp->su_dll_node))) {
            if (m_AVND_COMP_TYPE_IS_PREINSTANTIABLE(comp)) continue;
            rc = avnd_comp_clc_fsm_trigger(cb, comp,
                                           AVND_COMP_CLC_PRES_FSM_EV_TERM);
            if (NCSCC_RC_SUCCESS != rc) {
              LOG_ER("'%s' termination failed", comp->name.c_str());
              goto done;
            }
          }
        }
      }
    }
  }

  /* npi su */
  if (!m_AVND_SU_IS_PREINSTANTIABLE(su)) {
    TRACE("NPI SU :'%s'", su->name.c_str());
    /* get the only si */
    si = (AVND_SU_SI_REC *)m_NCS_DBLIST_FIND_FIRST(&su->si_list);
    osafassert(si);

    /* instantiating/restarting -> instantiated */
    if (((SA_AMF_PRESENCE_INSTANTIATING == prv_st) ||
         (SA_AMF_PRESENCE_RESTARTING == prv_st)) &&
        (SA_AMF_PRESENCE_INSTANTIATED == final_st) &&
        (all_csis_in_assigned_state(su) == true)) {
      rc = npi_su_instantiating_to_instantiated(su);
    }
    /* instantiating/instantiated/restarting -> inst-failed */
    if (((SA_AMF_PRESENCE_INSTANTIATING == prv_st) ||
         (SA_AMF_PRESENCE_INSTANTIATED == prv_st)) &&
        (SA_AMF_PRESENCE_INSTANTIATION_FAILED == final_st)) {
      TRACE("SU Instantiating/Instantiated -> Instantiation Failed");
      /*SU may fail with INST_FAILED state as a part of recovery
        like comp-restart and su-restart. Inform AMFD if
        assignments are pending from AMFD.*/
      if (m_AVND_SU_IS_ASSIGN_PEND(su)) {
        TRACE("SI-Assignment failed, Informing AVD");
        rc = avnd_di_susi_resp_send(cb, su, si);
        if (NCSCC_RC_SUCCESS != rc) goto done;
      }

      /* mark su as failed */
      m_AVND_SU_FAILED_SET(su);

      /* npi su is disabled in inst-fail state */
      m_AVND_SU_OPER_STATE_SET(su, SA_AMF_OPERATIONAL_DISABLED);

      /*No re-assignments and further escalation happens in INST_FAILED state,
        so reset suRestart flag.*/
      if (isRestartSet(su)) reset_suRestart_flag(su);
      // Ask AMFD to remove assignments.
      rc = avnd_di_oper_send(cb, su, SA_AMF_COMPONENT_FAILOVER);
      if (cb->is_avd_down == true) {
        LOG_WA("Director is down. Remove all SIs from '%s'", su->name.c_str());
        avnd_su_si_del(avnd_cb, su->name);
      }
    }
    if ((SA_AMF_PRESENCE_RESTARTING == prv_st) &&
        (SA_AMF_PRESENCE_INSTANTIATION_FAILED == final_st)) {
      TRACE("Restarting -> Instantiation Failed");
      if (sufailover_in_progress(su)) {
        /*Do not reset any flag, this will be done as a part of repair.*/
        rc = avnd_di_oper_send(cb, su, AVSV_ERR_RCVR_SU_FAILOVER);
        osafassert(NCSCC_RC_SUCCESS == rc);
        avnd_su_si_del(avnd_cb, su->name);
        goto done;
      }
    }

    /* terminating -> uninstantiated */
    if ((SA_AMF_PRESENCE_TERMINATING == prv_st) &&
        (SA_AMF_PRESENCE_UNINSTANTIATED == final_st)) {
      TRACE("Terminating -> UnInstantiated");
      if (sufailover_in_progress(su)) {
        /*Do not reset any flag, this will be done as a part of repair.*/
      } else {
        if (m_AVND_SU_IS_RESTART(su) &&
            (su_all_comps_restartable(*su) == true)) {
          /* npi su is enabled in uninstantiated state */
          m_AVND_SU_OPER_STATE_SET(su, SA_AMF_OPERATIONAL_ENABLED);
          /*
             It means all comps are terminated in surestart recovery or admin
             op. Now instantiate SU honoring instantiation level.
           */
          rc = avnd_su_pres_fsm_run(cb, su, 0, AVND_SU_PRES_FSM_EV_INST);
          if (NCSCC_RC_SUCCESS != rc) goto done;
        } else {
          if (m_AVND_SU_IS_FAILED(su)) m_AVND_SU_FAILED_RESET(su);

          /* si assignment/removal success.. generate si-oper done indication */
          rc = avnd_su_si_oper_done(cb, su, m_AVND_SU_IS_ALL_SI(su) ? 0 : si);
          m_AVND_SU_ALL_SI_RESET(su);

          /* npi su is enabled in uninstantiated state */
          m_AVND_SU_OPER_STATE_SET(su, SA_AMF_OPERATIONAL_ENABLED);
        }

        /* A NPI SU becomes uninstantiated, send SU oper state enabled event
           to AMFD if removal of assignments is also done.*/
        if (su->si_list.n_nodes == 0) {
          rc = avnd_di_oper_send(cb, su, 0);
        }
      }
    }

    /* terminating/instantiated/restarting -> term-failed */
    if (((SA_AMF_PRESENCE_TERMINATING == prv_st) ||
         (SA_AMF_PRESENCE_INSTANTIATED == prv_st) ||
         (SA_AMF_PRESENCE_RESTARTING == prv_st)) &&
        (SA_AMF_PRESENCE_TERMINATION_FAILED == final_st)) {
      TRACE("Terminating/Instantiated/Restarting -> Termination Failed");
      if (sufailover_in_progress(su)) {
        /*Do not reset any flag, this will be done as a part of repair.*/
        rc = avnd_di_oper_send(cb, su, AVSV_ERR_RCVR_SU_FAILOVER);
        osafassert(NCSCC_RC_SUCCESS == rc);
        avnd_su_si_del(avnd_cb, su->name);
        goto done;
      }
      m_AVND_SU_OPER_STATE_SET(su, SA_AMF_OPERATIONAL_DISABLED);
      /* inform AvD about oper state change */
      rc = avnd_di_oper_send(cb, su, SA_AMF_COMPONENT_FAILOVER);

      /* si assignment/removal failed.. inform AvD */
      /* Send response to Amfd only when there is a pending assignment. */
      if (m_AVND_SU_IS_ASSIGN_PEND(su))
        rc = avnd_di_susi_resp_send(cb, su, m_AVND_SU_IS_ALL_SI(su) ? 0 : si);
      /*
         During shutdown phase, all comps of NPI SU are terminated as a part of
         removal of assignments. If a SU enters in TERM_FAILED state then in
         order to complete shutdown sequence generate a si-oper done indication.
       */
      if ((si != nullptr) && (m_AVND_IS_SHUTTING_DOWN(cb)) &&
          (m_AVND_SU_SI_CURR_ASSIGN_STATE_IS_REMOVING(si)) &&
          (all_comps_terminated_in_su(su, true) == true)) {
        rc = avnd_su_si_oper_done(cb, su, si);
      }
    }

    /* instantiating -> term-failed */
    if ((prv_st == SA_AMF_PRESENCE_INSTANTIATING) &&
        (final_st == SA_AMF_PRESENCE_TERMINATION_FAILED)) {
      m_AVND_SU_OPER_STATE_SET(su, SA_AMF_OPERATIONAL_DISABLED);

      /* Don't send su-oper state msg, just update su oper state
       * AMF has lost control over this component and the operator needs
       * to repair this node. Failover is not possible in this state.
       *
       * There exists one case which is not related to fail-over situation
       * and related to fresh assignments in SU. When AMFD sends fresh
       * assignments for a SU to AMFND, a SU can go into term-failed state
       * during instantiation. In TERM-FAILED state, AMFND cleans up all the
       * comps of SU. So AMFND can send su-oper state message so that AMFD can
       * mark SG stable and make way for admin repair.
       */
      if ((si != nullptr) &&
          (si->prv_assign_state == AVND_SU_SI_ASSIGN_STATE_UNASSIGNED)) {
        rc = avnd_di_oper_send(cb, su, AVSV_ERR_RCVR_SU_FAILOVER);
        avnd_su_si_del(avnd_cb, su->name);
      } else {
        avnd_di_uns32_upd_send(AVSV_SA_AMF_SU, saAmfSUOperState_ID, su->name,
                               su->oper);
      }
    }

    if ((prv_st == SA_AMF_PRESENCE_INSTANTIATED) &&
        (final_st == SA_AMF_PRESENCE_UNINSTANTIATED) &&
        (cb->term_state == AVND_TERM_STATE_OPENSAF_SHUTDOWN_STARTED)) {
      /*
         During shutdown phase, all comps of SU may fault. In that case,
         SU FSM marks SU in TERMINAIING state and finally moves it to
         UNINSTANTIATED state. So generated the assignment done indication
         so that removal of lower rank SI can proceed.
       */
      rc = avnd_su_si_oper_done(cb, su, si);
      m_AVND_SU_ALL_SI_RESET(su);
    }
  }

done:
  TRACE_LEAVE2("%u", rc);
  return rc;
}

/****************************************************************************
  Name          : avnd_su_pres_uninst_suinst_hdler

  Description   : This routine processes the `SU Instantiate` event in
                  `Uninstantiated` state. For PI SU, this event is generated
                  when SU is to be instantiated. For NPI SU, this event is
                  generated when an active SI has to be assigned.

  Arguments     : cb   - ptr to the AvND control block
                  su   - ptr to the su
                  comp - ptr to the comp (can be nullptr)

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avnd_su_pres_uninst_suinst_hdler(AVND_CB *cb, AVND_SU *su,
                                          AVND_COMP *comp) {
  AVND_COMP *curr_comp = 0;
  AVND_SU_SI_REC *si = 0;
  AVND_COMP_CSI_REC *csi = 0;
  uint32_t rc = NCSCC_RC_SUCCESS;
  TRACE_ENTER2("SU Instantiate event in the Uninstantiated state: '%s'",
               su->name.c_str());

  /*
   * If pi su, pick the first pi comp & trigger it's FSM with InstEv.
   */
  if (m_AVND_SU_IS_PREINSTANTIABLE(su)) {
    TRACE("PI SU:'%s'", su->name.c_str());
    for (curr_comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
             m_NCS_DBLIST_FIND_FIRST(&su->comp_list));
         curr_comp; curr_comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
                        m_NCS_DBLIST_FIND_NEXT(&curr_comp->su_dll_node))) {
      /* instantiate the pi comp */
      TRACE("%s", curr_comp->name.c_str());
      if (m_AVND_COMP_TYPE_IS_PREINSTANTIABLE(curr_comp) &&
          (curr_comp->pres == SA_AMF_PRESENCE_UNINSTANTIATED)) {
        TRACE("Running the component CLC FSM ");
        rc = avnd_comp_clc_fsm_run(cb, curr_comp,
                                   AVND_COMP_CLC_PRES_FSM_EV_INST);
        if (NCSCC_RC_SUCCESS != rc) goto done;
        break;
      }
    } /* for */
  }

  /*
   * If npi su, it'll have only one si-rec in the si-list. Pick the
   * lowest ranked csi belonging to this si & trigger it's comp fsm.
   */
  if (!m_AVND_SU_IS_PREINSTANTIABLE(su)) {
    TRACE("NPI SU:'%s'", su->name.c_str());
    /* get the only si rec */
    si = (AVND_SU_SI_REC *)m_NCS_DBLIST_FIND_FIRST(&su->si_list);
    osafassert(si);

    csi = (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_FIRST(&si->csi_list);
    if (csi) {
      /* mark the csi state assigning */
      m_AVND_COMP_CSI_CURR_ASSIGN_STATE_SET(
          csi, AVND_COMP_CSI_ASSIGN_STATE_ASSIGNING);

      TRACE("Running the component CLC FSM ");
      /* instantiate the comp */
      rc = avnd_comp_clc_fsm_run(cb, csi->comp, AVND_COMP_CLC_PRES_FSM_EV_INST);
      if (NCSCC_RC_SUCCESS != rc) goto done;
    }
  }

  /* transition to instantiating state */
  avnd_su_pres_state_set(cb, su, SA_AMF_PRESENCE_INSTANTIATING);

done:
  if (rc == NCSCC_RC_FAILURE)
    avnd_su_pres_state_set(cb, su, SA_AMF_PRESENCE_INSTANTIATION_FAILED);
  TRACE_LEAVE2("%u", rc);
  return rc;
}

/****************************************************************************
  Name          : avnd_su_pres_insting_suterm_hdler

  Description   : This routine processes the `SU Terminate` event in
                  `Instantiating` state.

  Arguments     : cb   - ptr to the AvND control block
                  su   - ptr to the su
                  comp - ptr to the comp (can be nullptr)

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : Note that this event is only generated for PI SUs. SUTermEv
                  is generated for NPI SUs only during stable states i.e.
                  uninstantiated & instantiated states. This is done by
                  avoiding overlapping SI assignments.
******************************************************************************/
uint32_t avnd_su_pres_insting_suterm_hdler(AVND_CB *cb, AVND_SU *su,
                                           AVND_COMP *comp) {
  AVND_COMP *curr_comp = 0;
  uint32_t rc = NCSCC_RC_SUCCESS;
  TRACE_ENTER2("SU Terminate event in Instantiating state:'%s'",
               su->name.c_str());

  /*
   * If pi su, pick all the instantiated/instantiating pi comps &
   * trigger their FSM with TermEv.
   */
  for (curr_comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
           m_NCS_DBLIST_FIND_FIRST(&su->comp_list));
       curr_comp; curr_comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
                      m_NCS_DBLIST_FIND_NEXT(&curr_comp->su_dll_node))) {
    /*
     * skip the npi comps.. as the su is yet to be instantiated,
     * there are no SIs assigned.
     */
    if (!m_AVND_COMP_TYPE_IS_PREINSTANTIABLE(curr_comp)) continue;

    /* terminate the non-uninstantiated pi comp */
    if (!m_AVND_COMP_PRES_STATE_IS_UNINSTANTIATED(curr_comp)) {
      TRACE("Running the component clc FSM");
      rc = avnd_comp_clc_fsm_run(cb, curr_comp, AVND_COMP_CLC_PRES_FSM_EV_TERM);
      if (NCSCC_RC_SUCCESS != rc) goto done;
    }
  } /* for */

  /* transition to terminating state */
  avnd_su_pres_state_set(cb, su, SA_AMF_PRESENCE_TERMINATING);

done:
  TRACE_LEAVE2("%u", rc);
  return rc;
}

/****************************************************************************
  Name          : avnd_su_pres_insting_surestart_hdler

  Description   : This routine processes the `SU Restart` event in
                  `Instantiating` state.

  Arguments     : cb   - ptr to the AvND control block
                  su   - ptr to the su
                  comp - ptr to the comp (can be nullptr)

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avnd_su_pres_insting_surestart_hdler(AVND_CB *cb, AVND_SU *su,
                                              AVND_COMP *comp) { /* TBD */
  uint32_t rc = NCSCC_RC_SUCCESS;
  TRACE("SU Restart event in the Instantiating State, returning success:'%s'",
        su->name.c_str());
  return rc;
}

/****************************************************************************
  Name          : avnd_su_pres_insting_compinst_hdler

  Description   : This routine processes the `CompInstantiated` event in
                  `Instantiating` state.

  Arguments     : cb   - ptr to the AvND control block
                  su   - ptr to the su
                  comp - ptr to the comp (can be nullptr)

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avnd_su_pres_insting_compinst_hdler(AVND_CB *cb, AVND_SU *su,
                                             AVND_COMP *comp) {
  AVND_COMP *curr_comp = 0;
  AVND_COMP_CSI_REC *curr_csi = 0;
  bool is;
  uint32_t rc = NCSCC_RC_SUCCESS;
  const std::string compname = comp ? comp->name : "none";
  TRACE_ENTER2(
      "Component Instantiated event in the Instantiating state:'%s' : '%s'",
      su->name.c_str(), compname.c_str());

  /*
   * If pi su, pick the next pi comp & trigger it's FSM with InstEv.
   * If the component is marked failed (=> component has reinstantiated
   * after some failure), unmark it & determine the su presence state.
   */
  if (m_AVND_SU_IS_PREINSTANTIABLE(su)) {
    TRACE("PI SU");
    if (m_AVND_COMP_IS_FAILED(comp)) {
      m_AVND_COMP_FAILED_RESET(comp);
    } else {
      for (curr_comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
               m_NCS_DBLIST_FIND_NEXT(&comp->su_dll_node));
           curr_comp; curr_comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
                          m_NCS_DBLIST_FIND_NEXT(&curr_comp->su_dll_node))) {
        /* instantiate the pi comp */
        if (m_AVND_COMP_TYPE_IS_PREINSTANTIABLE(curr_comp)) {
          TRACE("Running the component clc FSM");
          rc = avnd_comp_clc_fsm_run(cb, curr_comp,
                                     AVND_COMP_CLC_PRES_FSM_EV_INST);
          if (NCSCC_RC_SUCCESS != rc) goto done;
          break;
        }
      } /* for */
    }

    /* determine su presence state */
    m_AVND_SU_IS_INSTANTIATED(su, is);
    if (true == is) {
      avnd_su_pres_state_set(cb, su, SA_AMF_PRESENCE_INSTANTIATED);
    }
  }

  /*
   * If npi su, pick the next csi & trigger it's comp fsm with InstEv.
   */
  if (!m_AVND_SU_IS_PREINSTANTIABLE(su)) {
    TRACE("NPI SU");
    /* get the only csi rec */
    curr_csi = m_AVND_CSI_REC_FROM_COMP_DLL_NODE_GET(
        m_NCS_DBLIST_FIND_FIRST(&comp->csi_list));
    osafassert(curr_csi);

    /* mark the csi state assigned */
    m_AVND_COMP_CSI_CURR_ASSIGN_STATE_SET(curr_csi,
                                          AVND_COMP_CSI_ASSIGN_STATE_ASSIGNED);

    if (curr_csi->single_csi_add_rem_in_si == AVSV_SUSI_ACT_ASGN) {
      // we are adding a single CSI, the comp is instantiated so now we're done
      curr_csi->single_csi_add_rem_in_si = AVSV_SUSI_ACT_BASE;
      avnd_su_pres_state_set(cb, su, SA_AMF_PRESENCE_INSTANTIATED);
      goto done;
    }

    /* get the next csi */
    curr_csi =
        (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_NEXT(&curr_csi->si_dll_node);
    if (curr_csi) {
      /* we have another csi. trigger the comp fsm with InstEv */
      TRACE(
          "There's another CSI:'%s', Running the component clc FSM for comp:'%s'",
          curr_csi->name.c_str(), curr_csi->comp->name.c_str());
      rc = avnd_comp_clc_fsm_trigger(cb, curr_csi->comp,
                                     AVND_COMP_CLC_PRES_FSM_EV_INST);
      if (NCSCC_RC_SUCCESS != rc) goto done;
    } else {
      /* => si assignment done */
      TRACE("SI Assignment done");
      avnd_su_pres_state_set(cb, su, SA_AMF_PRESENCE_INSTANTIATED);
    }
  }

done:
  TRACE_LEAVE2("%u", rc);
  return rc;
}

/****************************************************************************
  Name          : avnd_su_pres_insting_compinstfail_hdler

  Description   : This routine processes the `CompInstantiateFailed` event in
                  `Instantiating` state.
                  For PI SU, a failure in the instantiation of any component
                  means that SU instantiation has failed. Terminate all the
                  already instantiated (or instantiating) components &
                  transition to inst-failed state.
                  For NPI SU, a failure in the instantiation of any component
                  indicates that active SI assignment to this SU has failed.
                  Terminate all the components to whom the active CSI has
                  already been assigned (or assigning).

  Arguments     : cb   - ptr to the AvND control block
                  su   - ptr to the su
                  comp - ptr to the comp (can be nullptr)

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avnd_su_pres_insting_compinstfail_hdler(AVND_CB *cb, AVND_SU *su,
                                                 AVND_COMP *comp) {
  AVND_COMP *curr_comp = 0;
  AVND_SU_SI_REC *si = 0;
  AVND_COMP_CSI_REC *curr_csi = 0;
  uint32_t rc = NCSCC_RC_SUCCESS, comp_count = 0;
  const std::string compname = comp ? comp->name : "none";
  TRACE_ENTER2(
      "CompInstantiateFailed event in the Instantiate State: '%s' : '%s'",
      su->name.c_str(), compname.c_str());

  /* transition to inst-failed state */
  avnd_su_pres_state_set(cb, su, SA_AMF_PRESENCE_INSTANTIATION_FAILED);
  m_AVND_SU_ALL_TERM_RESET(su);

  /*
   * If pi su, pick all the instantiated/instantiating pi comps &
   * trigger their FSM with TermEv.
   */
  if (m_AVND_SU_IS_PREINSTANTIABLE(su)) {
    TRACE("PI SU:'%s'", su->name.c_str());
    for (curr_comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
             m_NCS_DBLIST_FIND_LAST(&su->comp_list));
         curr_comp; curr_comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
                        m_NCS_DBLIST_FIND_PREV(&curr_comp->su_dll_node))) {
      /* skip the npi comps */
      if (!m_AVND_COMP_TYPE_IS_PREINSTANTIABLE(curr_comp)) continue;

      /* terminate the non-uninstantiated pi comp */
      if (!m_AVND_COMP_PRES_STATE_IS_UNINSTANTIATED(curr_comp)) {
        TRACE("Running the component clc FSM");
        rc = avnd_comp_clc_fsm_run(cb, curr_comp,
                                   AVND_COMP_CLC_PRES_FSM_EV_TERM);
        if (NCSCC_RC_SUCCESS != rc) goto done;
        comp_count++;
      }
    } /* for */
    if (comp_count == 1) {
      /* If all comps are terminated then set term state and
         process the SUSI assignment.*/
      m_AVND_SU_ALL_TERM_SET(su);
      avnd_su_siq_prc(cb, su);
    }
  }

  /*
   * If npi su, pick all the assigned/assigning comps &
   * trigger their comp fsm with TermEv
   */
  if (!m_AVND_SU_IS_PREINSTANTIABLE(su)) {
    TRACE("NPI SU:'%s'", su->name.c_str());
    /* get the only si rec */
    si = (AVND_SU_SI_REC *)m_NCS_DBLIST_FIND_FIRST(&su->si_list);
    osafassert(si);

    for (curr_csi = (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_LAST(&si->csi_list);
         curr_csi; curr_csi = (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_PREV(
                       &curr_csi->si_dll_node)) {
      /* terminate the component containing non-unassigned csi */
      if (!m_AVND_COMP_CSI_CURR_ASSIGN_STATE_IS_UNASSIGNED(curr_csi) &&
          (curr_csi->comp->pres != SA_AMF_PRESENCE_INSTANTIATION_FAILED)) {
        TRACE("Running the component clc FSM");
        rc = avnd_comp_clc_fsm_run(cb, curr_csi->comp,
                                   AVND_COMP_CLC_PRES_FSM_EV_TERM);
        if (NCSCC_RC_SUCCESS != rc) goto done;
      }
    } /* for */
    m_AVND_SU_ALL_TERM_SET(su);
  }

done:
  TRACE_LEAVE2("%u", rc);
  return rc;
}

/**
 * @brief  Returns first assigned csi traversing from end.
 * @return Ptr to csi_rec.
 */
static AVND_COMP_CSI_REC *get_next_assigned_csi_from_end(
    const AVND_SU_SI_REC *si) {
  for (AVND_COMP_CSI_REC *csi =
           (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_LAST(&si->csi_list);
       (csi != nullptr);
       csi = (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_PREV(&csi->si_dll_node)) {
    if (m_AVND_COMP_CSI_CURR_ASSIGN_STATE_IS_ASSIGNED(csi) &&
        ((csi->comp != nullptr) &&
         (csi->comp->pres == SA_AMF_PRESENCE_INSTANTIATED)))
      return csi;
  }
  return nullptr;
}
/****************************************************************************
  Name          : avnd_su_pres_inst_suterm_hdler

  Description   : This routine processes the `SU Terminate` event in
                  `Instantiated` state.

  Arguments     : cb   - ptr to the AvND control block
                  su   - ptr to the su
                  comp - ptr to the comp (can be nullptr)

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avnd_su_pres_inst_suterm_hdler(AVND_CB *cb, AVND_SU *su,
                                        AVND_COMP *comp) {
  AVND_COMP *curr_comp = 0;
  AVND_SU_SI_REC *si = 0;
  AVND_COMP_CSI_REC *csi = 0;
  uint32_t rc = NCSCC_RC_SUCCESS;
  TRACE_ENTER2("SUTerminate event in the Instantiated state: '%s'",
               su->name.c_str());

  /*
   * If pi su, pick the last pi comp & trigger it's FSM with TermEv.
   */
  if (m_AVND_SU_IS_PREINSTANTIABLE(su)) {
    TRACE("PI SU:'%s'", su->name.c_str());
    for (curr_comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
             m_NCS_DBLIST_FIND_LAST(&su->comp_list));
         curr_comp; curr_comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
                        m_NCS_DBLIST_FIND_PREV(&curr_comp->su_dll_node))) {
      if (curr_comp->pres == SA_AMF_PRESENCE_UNINSTANTIATED) continue;

      /* terminate the pi comp */
      if (m_AVND_COMP_TYPE_IS_PREINSTANTIABLE(curr_comp)) {
        TRACE("Running the component clc FSM, terminate the component");
        rc = avnd_comp_clc_fsm_run(cb, curr_comp,
                                   AVND_COMP_CLC_PRES_FSM_EV_TERM);
        if (NCSCC_RC_SUCCESS != rc) goto done;
        break;
      }
    } /* for */
  }

  /*
   * If npi su, it'll have only one si-rec in the si-list. Pick the
   * highest ranked csi belonging to this si & trigger it's comp fsm.
   */
  if (!m_AVND_SU_IS_PREINSTANTIABLE(su)) {
    TRACE("NPI SU:'%s'", su->name.c_str());
    /* get the only si rec */
    si = (AVND_SU_SI_REC *)m_NCS_DBLIST_FIND_FIRST(&su->si_list);
    osafassert(si);
    csi = (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_LAST(&si->csi_list);
    osafassert(csi);

    /* mark the csi state assigning/removing */
    if (m_AVND_SU_SI_CURR_ASSIGN_STATE_IS_REMOVING(si))
      m_AVND_COMP_CSI_CURR_ASSIGN_STATE_SET(
          csi, AVND_COMP_CSI_ASSIGN_STATE_REMOVING);
    else
      m_AVND_COMP_CSI_CURR_ASSIGN_STATE_SET(
          csi, AVND_COMP_CSI_ASSIGN_STATE_ASSIGNING);

    TRACE("Running the component clc FSM, terminate the component");
    /* terminate the comp */
    rc = avnd_comp_clc_fsm_run(cb, csi->comp,
                               (m_AVND_COMP_IS_FAILED(csi->comp))
                                   ? AVND_COMP_CLC_PRES_FSM_EV_CLEANUP
                                   : AVND_COMP_CLC_PRES_FSM_EV_TERM);
    if (NCSCC_RC_SUCCESS != rc) goto done;

    /*
       During shutdown phase if a component faults, it will be cleaned up by
       AMFND irrespective of recovery policy. This component will move to
       UNINSTANTIATED after successful clean up. When amfnd starts removing SI
       from SU of this comp, it will have to skip the CSI of cleaned up
       component.
    */
    if ((csi->comp->pres == SA_AMF_PRESENCE_UNINSTANTIATED) &&
        (cb->term_state == AVND_TERM_STATE_OPENSAF_SHUTDOWN_STARTED)) {
      m_AVND_COMP_CSI_CURR_ASSIGN_STATE_SET(csi,
                                            AVND_COMP_CSI_ASSIGN_STATE_REMOVED);
      avnd_su_pres_state_set(cb, su, SA_AMF_PRESENCE_TERMINATING);
      AVND_COMP_CSI_REC *assigned_csi = get_next_assigned_csi_from_end(si);
      if (assigned_csi == nullptr) {
        // Components of all the CSIs in SI are cleaned up.
        avnd_su_pres_state_set(cb, su, SA_AMF_PRESENCE_UNINSTANTIATED);
        goto done;
      } else {
        // One CSI is still assigned.
        m_AVND_COMP_CSI_CURR_ASSIGN_STATE_SET(
            assigned_csi, AVND_COMP_CSI_ASSIGN_STATE_REMOVING);
        rc = avnd_comp_clc_fsm_trigger(
            cb, assigned_csi->comp,
            (m_AVND_COMP_IS_FAILED(assigned_csi->comp))
                ? AVND_COMP_CLC_PRES_FSM_EV_CLEANUP
                : AVND_COMP_CLC_PRES_FSM_EV_TERM);
      }
    }
  }

  /* transition to terminating state */
  if (su->pres != SA_AMF_PRESENCE_TERMINATING)
    avnd_su_pres_state_set(cb, su, SA_AMF_PRESENCE_TERMINATING);

done:
  TRACE_LEAVE2("%u", rc);
  return rc;
}

/**
 * @brief  Return true if all pi comps of SU are in restarting state.
 *         It will be used during restart admin operation on su.
 * @param  ptr to su.
 * @return  true/false.
 */
bool su_evaluate_restarting_state(AVND_SU *su) {
  for (AVND_COMP *comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
           m_NCS_DBLIST_FIND_FIRST(&su->comp_list));
       comp; comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
                 m_NCS_DBLIST_FIND_NEXT(&comp->su_dll_node))) {
    if (!m_AVND_COMP_TYPE_IS_PREINSTANTIABLE(comp)) continue;
    if (comp->pres != SA_AMF_PRESENCE_RESTARTING) {
      return false;
    }
  }

  return true;
}
/**
 * @brief       Checks if all csis of all the sis in this su are in restarting
 state. Also performs same check by excluding the CSI passed in the default arg.
 * @param [in]  cmp
 * @param [in]  exclude_csi (default value nullptr)
 * @returns     true/false
 **/
bool all_csis_in_restarting_state(const AVND_SU *su,
                                  AVND_COMP_CSI_REC *exclude_csi) {
  AVND_COMP_CSI_REC *curr_csi;
  AVND_SU_SI_REC *curr_si;

  for (curr_si = (AVND_SU_SI_REC *)m_NCS_DBLIST_FIND_FIRST(&su->si_list);
       curr_si; curr_si = (AVND_SU_SI_REC *)m_NCS_DBLIST_FIND_NEXT(
                    &curr_si->su_dll_node)) {
    for (curr_csi =
             (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_FIRST(&curr_si->csi_list);
         curr_csi; curr_csi = (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_NEXT(
                       &curr_csi->si_dll_node)) {
      if (curr_csi == exclude_csi) continue;
      if (!m_AVND_COMP_CSI_CURR_ASSIGN_STATE_IS_RESTARTING(curr_csi)) {
        return false;
      }
    }
  }
  return true;
}
/****************************************************************************
  Name          : avnd_su_pres_inst_surestart_hdler

  Description   : This routine processes the `SU Restart` event in
                  `Instantiated` state.

  Arguments     : cb   - ptr to the AvND control block
                  su   - ptr to the su
                  comp - ptr to the comp (can be nullptr)

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avnd_su_pres_inst_surestart_hdler(AVND_CB *cb, AVND_SU *su,
                                           AVND_COMP *comp) {
  AVND_COMP *curr_comp = 0;
  AVND_SU_SI_REC *si = 0;
  AVND_COMP_CSI_REC *csi = 0;
  uint32_t rc = NCSCC_RC_SUCCESS;
  TRACE_ENTER2("SURestart event in the Instantiated state: '%s'",
               su->name.c_str());

  /*
   * If pi su, pick the first pi comp & trigger it's FSM with RestartEv.
   */
  if (m_AVND_SU_IS_PREINSTANTIABLE(su)) {
    TRACE("PI SU:'%s'", su->name.c_str());
    for (curr_comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
             m_NCS_DBLIST_FIND_LAST(&su->comp_list));
         curr_comp; curr_comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
                        m_NCS_DBLIST_FIND_PREV(&curr_comp->su_dll_node))) {
      if ((curr_comp->pres == SA_AMF_PRESENCE_RESTARTING) ||
          (curr_comp->pres == SA_AMF_PRESENCE_UNINSTANTIATED))
        continue;
      if (m_AVND_COMP_TYPE_IS_PREINSTANTIABLE(curr_comp)) {
        TRACE("Running the component clc FSM, restart the component");
        if (m_AVND_SU_IS_RESTART(su) && m_AVND_SU_IS_FAILED(su))
          rc = avnd_comp_clc_fsm_run(cb, curr_comp,
                                     AVND_COMP_CLC_PRES_FSM_EV_CLEANUP);
        else
          rc = avnd_comp_clc_fsm_run(cb, curr_comp,
                                     AVND_COMP_CLC_PRES_FSM_EV_RESTART);
        if (NCSCC_RC_SUCCESS != rc) goto done;
        break;
      } else {
        /*
           For a NPI comp in PI SU, component FSM is always triggered at the
           time of assignments. If this component is non-restartable then start
           reassginment from the whole SU now, it will take care if its
           termination/clean up.
         */
        if (m_AVND_COMP_IS_RESTART_DIS(curr_comp) &&
            (curr_comp->csi_list.n_nodes > 0)) {
          TRACE(
              "Start reassignment to different SU as '%s' is"
              " not restartable",
              curr_comp->name.c_str());
          su_send_suRestart_recovery_msg(su);
          goto done;
        } else {
          if (m_AVND_SU_IS_RESTART(su) && m_AVND_SU_IS_FAILED(su))
            rc = avnd_comp_clc_fsm_run(cb, curr_comp,
                                       AVND_COMP_CLC_PRES_FSM_EV_CLEANUP);
          else
            rc = avnd_comp_clc_fsm_run(cb, curr_comp,
                                       AVND_COMP_CLC_PRES_FSM_EV_RESTART);
          if (curr_comp->pres == SA_AMF_PRESENCE_TERMINATING)
            avnd_su_pres_state_set(cb, su, SA_AMF_PRESENCE_TERMINATING);
          break;
        }
      }
    } /* for */
    if ((su_evaluate_restarting_state(su) == true) &&
        (!m_AVND_SU_IS_FAILED(su))) {
      TRACE("Mark su restarting");
      avnd_su_pres_state_set(cb, su, SA_AMF_PRESENCE_RESTARTING);
    }
  }

  /*
   * If npi su, it'll have only one si-rec in the si-list. Pick the
   * lowest ranked csi belonging to this si & trigger it's comp fsm.
   */
  if (!m_AVND_SU_IS_PREINSTANTIABLE(su)) {
    TRACE("NPI SU:'%s'", su->name.c_str());
    /* get the only si rec */
    si = (AVND_SU_SI_REC *)m_NCS_DBLIST_FIND_FIRST(&su->si_list);
    osafassert(si);

    csi = (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_LAST(&si->csi_list);
    if (csi) {
      TRACE("Running the component clc FSM for csi:'%s', comp:%s",
            csi->name.c_str(), csi->comp->name.c_str());
      if (m_AVND_SU_IS_RESTART(su) && m_AVND_SU_IS_FAILED(su)) {
        if (m_AVND_SU_SI_CURR_ASSIGN_STATE_IS_REMOVING(si))
          m_AVND_COMP_CSI_CURR_ASSIGN_STATE_SET(
              csi, AVND_COMP_CSI_ASSIGN_STATE_REMOVING);
        else
          m_AVND_COMP_CSI_CURR_ASSIGN_STATE_SET(
              csi, AVND_COMP_CSI_ASSIGN_STATE_ASSIGNING);
        rc = avnd_comp_clc_fsm_run(cb, csi->comp,
                                   AVND_COMP_CLC_PRES_FSM_EV_CLEANUP);
      } else {
        m_AVND_COMP_CSI_CURR_ASSIGN_STATE_SET(
            csi, AVND_COMP_CSI_ASSIGN_STATE_RESTARTING);
        rc = avnd_comp_clc_fsm_run(cb, csi->comp,
                                   AVND_COMP_CLC_PRES_FSM_EV_RESTART);
      }

      if (NCSCC_RC_SUCCESS != rc) goto done;
    }
    if ((all_csis_in_restarting_state(su) == true) &&
        (!m_AVND_SU_IS_FAILED(su))) {
      TRACE("All CSIs are in restarting state, so marking SU restarting");
      avnd_su_pres_state_set(cb, su, SA_AMF_PRESENCE_RESTARTING);
    }
  }

done:
  TRACE_LEAVE2("%u", rc);
  return rc;
}

/****************************************************************************
  Name          : avnd_su_pres_inst_comprestart_hdler

  Description   : This routine processes the `Comp Restart` event in
                  `Instantiated` state.

  Arguments     : cb   - ptr to the AvND control block
                  su   - ptr to the su
                  comp - ptr to the comp (can be nullptr)

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avnd_su_pres_inst_comprestart_hdler(AVND_CB *cb, AVND_SU *su,
                                             AVND_COMP *comp) { /* TBD */
  uint32_t rc = NCSCC_RC_SUCCESS;
  AVND_COMP_CSI_REC *curr_csi = 0;
  const std::string compname = comp ? comp->name : "none";
  TRACE_ENTER2("Component restart event in the Instantiated state, '%s' : '%s'",
               su->name.c_str(), compname.c_str());
  if (m_AVND_SU_IS_PREINSTANTIABLE(su)) {
    TRACE("PI SU");
    for (AVND_COMP *curr_comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
             m_NCS_DBLIST_FIND_PREV(&comp->su_dll_node));
         curr_comp; curr_comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
                        m_NCS_DBLIST_FIND_PREV(&curr_comp->su_dll_node))) {
      if (curr_comp->pres == SA_AMF_PRESENCE_RESTARTING) continue;
      if (m_AVND_COMP_TYPE_IS_PREINSTANTIABLE(curr_comp)) {
        TRACE("Running the component clc FSM");
        rc = avnd_comp_clc_fsm_run(cb, curr_comp,
                                   AVND_COMP_CLC_PRES_FSM_EV_RESTART);
        if (NCSCC_RC_SUCCESS != rc) goto done;
        break;
      } else {
        /*
           For a NPI comp in SU, component FSM is always triggered
           at the time of assignments. If this component is
           non-restartable then start reassginment from the
           whole SU now.
         */
        if (m_AVND_COMP_IS_RESTART_DIS(curr_comp) &&
            (curr_comp->csi_list.n_nodes > 0)) {
          TRACE(
              "Start reassignment to different SU as '%s' is"
              " not restartable",
              curr_comp->name.c_str());
          su_send_suRestart_recovery_msg(su);
          goto done;
        } else {
          if (m_AVND_SU_IS_RESTART(su) && m_AVND_SU_IS_FAILED(su))
            rc = avnd_comp_clc_fsm_run(cb, curr_comp,
                                       AVND_COMP_CLC_PRES_FSM_EV_CLEANUP);
          else
            rc = avnd_comp_clc_fsm_run(cb, curr_comp,
                                       AVND_COMP_CLC_PRES_FSM_EV_RESTART);
          if (curr_comp->pres == SA_AMF_PRESENCE_TERMINATING)
            avnd_su_pres_state_set(cb, su, SA_AMF_PRESENCE_TERMINATING);
          break;
        }
      }
    }
    if (su_evaluate_restarting_state(su) == true)
      avnd_su_pres_state_set(cb, su, SA_AMF_PRESENCE_RESTARTING);
  }

  if (!m_AVND_SU_IS_PREINSTANTIABLE(su)) {
    TRACE_1("NPI SU");
    /* get the only csi rec */
    curr_csi = m_AVND_CSI_REC_FROM_COMP_DLL_NODE_GET(
        m_NCS_DBLIST_FIND_FIRST(&comp->csi_list));
    osafassert(curr_csi);
    /* Typically we mark the CSI state here. But initially we had marked
       CSI state Restarting. It will be marked
       AVND_COMP_CSI_ASSIGN_STATE_ASSIGNED when component will be instantiated.
     */
    curr_csi =
        (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_PREV(&curr_csi->si_dll_node);
    if (curr_csi) {
      m_AVND_COMP_CSI_CURR_ASSIGN_STATE_SET(
          curr_csi, AVND_COMP_CSI_ASSIGN_STATE_RESTARTING);
      TRACE_2("Running the component clc FSM for csi:'%s', comp:%s",
              curr_csi->name.c_str(), curr_csi->comp->name.c_str());
      rc = avnd_comp_clc_fsm_run(cb, curr_csi->comp,
                                 AVND_COMP_CLC_PRES_FSM_EV_RESTART);
      if (NCSCC_RC_SUCCESS != rc) goto done;
    }
    if (all_csis_in_restarting_state(su) == true) {
      TRACE_2("All CSIs are in restarting state, so marking SU restarting");
      avnd_su_pres_state_set(cb, su, SA_AMF_PRESENCE_RESTARTING);
    }
  }
done:
  TRACE_LEAVE();
  return rc;
}

/****************************************************************************
  Name          : avnd_su_pres_inst_compterming_hdler

  Description   : This routine processes the `Comp Terminating` event in
                  `Instantiated` state.

  Arguments     : cb   - ptr to the AvND control block
                  su   - ptr to the su
                  comp - ptr to the comp (can be nullptr)

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avnd_su_pres_inst_compterming_hdler(AVND_CB *cb, AVND_SU *su,
                                             AVND_COMP *comp) {
  uint32_t rc = NCSCC_RC_SUCCESS;
  const std::string compname = comp ? comp->name : "none";
  TRACE_ENTER2("CompTerminating event in the Instantiated state:'%s' : '%s'",
               su->name.c_str(), compname.c_str());
  // A SU enters in TERMINATING state when any component is terminating.
  if (((comp != nullptr) && (comp->admin_oper == true)) ||
      m_AVND_SU_IS_FAILED(su) || (su->admin_op_Id == SA_AMF_ADMIN_RESTART)) {
    avnd_su_pres_state_set(cb, su, SA_AMF_PRESENCE_TERMINATING);
  }

  TRACE_LEAVE2("%u", rc);
  return rc;
}

/****************************************************************************
  Name          : avnd_su_pres_terming_compinst_hdler

  Description   : This routine processes the `Comp Instantiate` event in
                  `Terminating` state.

  Arguments     : cb   - ptr to the AvND control block
                  su   - ptr to the su
                  comp - ptr to the comp (can be nullptr)

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avnd_su_pres_terming_compinst_hdler(AVND_CB *cb, AVND_SU *su,
                                             AVND_COMP *comp) {
  uint32_t rc = NCSCC_RC_SUCCESS;
  const std::string compname = comp ? comp->name : "none";
  TRACE_ENTER2(
      "ComponentInstantiate event in the terminating state:'%s' : '%s'",
      su->name.c_str(), compname.c_str());

  if (m_AVND_SU_IS_PREINSTANTIABLE(su)) {
    bool is;
    if (m_AVND_COMP_IS_FAILED(comp)) {
      m_AVND_COMP_FAILED_RESET(comp);
    }

    /* determine if su can be transitioned to instantiated state */
    m_AVND_SU_IS_INSTANTIATED(su, is);
    if (true == is) {
      avnd_su_pres_state_set(cb, su, SA_AMF_PRESENCE_INSTANTIATED);
    }
    if (m_AVND_SU_IS_RESTART(su)) {
      if (su->admin_op_Id == SA_AMF_ADMIN_RESTART)
        /*This can happen when SU has both restartable and non restartable
           comps.Go for further instantiation.*/
        rc = avnd_su_pres_fsm_run(cb, su, 0, AVND_SU_PRES_FSM_EV_INST);
      else if (m_AVND_SU_IS_FAILED(su)) {
        /*Before going for surestart recovery from comp restart recovery, a
          comp was in instantiating state, cleanup it now.*/
        rc = avnd_comp_clc_fsm_run(cb, comp, AVND_COMP_CLC_PRES_FSM_EV_CLEANUP);
      }
    }
  }
  TRACE_LEAVE2("%u", rc);
  return rc;
}

/****************************************************************************
  Name          : avnd_su_pres_terming_comptermfail_hdler

  Description   : This routine processes the `Comp Termination Failed` event in
                  `Terminating` state.
                  For PI SU, a failure in the termination of any component
                  means that SU termination has failed. Terminate all the
                  already instantiated (or instantiating) components &
                  transition to term-failed state.
                  For NPI SU, a failure in the termination of any component
                  indicates that active SI assignment to this SU has failed.
                  Terminate all the components to whom the active CSI has
                  already been assigned (or assigning).

  Arguments     : cb   - ptr to the AvND control block
                  su   - ptr to the su
                  comp - ptr to the comp (can be nullptr)

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : This function is reused for all state changes into termfail
******************************************************************************/
uint32_t avnd_su_pres_terming_comptermfail_hdler(AVND_CB *cb, AVND_SU *su,
                                                 AVND_COMP *comp) {
  AVND_COMP *curr_comp = 0;
  AVND_SU_SI_REC *si = 0;
  AVND_COMP_CSI_REC *curr_csi = 0;
  uint32_t rc = NCSCC_RC_SUCCESS;
  const std::string compname = comp ? comp->name : "none";
  TRACE_ENTER2(
      "Component Termination failed event in the Terminating state,'%s': '%s'",
      su->name.c_str(), compname.c_str());

  /*
   * If pi su, pick all the instantiated/instantiating pi comps &
   * trigger their FSM with TermEv.
   */
  if (m_AVND_SU_IS_PREINSTANTIABLE(su)) {
    TRACE("PI SU");
    for (curr_comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
             m_NCS_DBLIST_FIND_FIRST(&su->comp_list));
         curr_comp; curr_comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
                        m_NCS_DBLIST_FIND_NEXT(&curr_comp->su_dll_node))) {
      /* skip the npi comps */
      if (!m_AVND_COMP_TYPE_IS_PREINSTANTIABLE(curr_comp)) continue;

      /* terminate the non-uninstantiated pi comp */
      if (!m_AVND_COMP_PRES_STATE_IS_UNINSTANTIATED(curr_comp)) {
        TRACE(
            "Running the component clc FSM, terminate the non-uninstantiated pi comp");
        if (m_AVND_COMP_PRES_STATE_IS_INSTANTIATED(curr_comp) &&
            (!m_AVND_COMP_IS_FAILED(curr_comp)))
          rc = avnd_comp_clc_fsm_trigger(cb, curr_comp,
                                         AVND_COMP_CLC_PRES_FSM_EV_TERM);
        /* During shutdown phase AMFND cleans up all the components */
        else if (cb->term_state != AVND_TERM_STATE_OPENSAF_SHUTDOWN_STARTED)
          rc = avnd_comp_clc_fsm_trigger(cb, curr_comp,
                                         AVND_COMP_CLC_PRES_FSM_EV_CLEANUP);

        if (NCSCC_RC_SUCCESS != rc) goto done;
      }
    } /* for */
  }

  /*
   * If npi su, pick all the assigned/assigning comps &
   * trigger their comp fsm with TermEv
   */
  if (!m_AVND_SU_IS_PREINSTANTIABLE(su)) {
    TRACE("NPI SU");
    /* get the only si rec */
    si = (AVND_SU_SI_REC *)m_NCS_DBLIST_FIND_FIRST(&su->si_list);
    osafassert(si);

    for (curr_csi = (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_FIRST(&si->csi_list);
         curr_csi; curr_csi = (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_NEXT(
                       &curr_csi->si_dll_node)) {
      TRACE(
          "Running the component clc FSM, terminate the component containing non-unassigned csi");
      /* terminate the component containing non-unassigned csi */
      if (!m_AVND_COMP_CSI_CURR_ASSIGN_STATE_IS_UNASSIGNED(curr_csi)) {
        rc = avnd_comp_clc_fsm_trigger(cb, curr_csi->comp,
                                       AVND_COMP_CLC_PRES_FSM_EV_TERM);
        if (NCSCC_RC_SUCCESS != rc) goto done;
      }
    } /* for */
  }

  /* transition to term-failed state */
  avnd_su_pres_state_set(cb, su, SA_AMF_PRESENCE_TERMINATION_FAILED);

  if (true == su->is_ncs) {
    std::string reason = "SU '" + su->name + "' Termination-failed";
    if (su->suMaintenanceCampaign.empty()) {
      opensaf_reboot(
          avnd_cb->node_info.nodeId,
          osaf_extended_name_borrow(&avnd_cb->node_info.executionEnvironment),
          reason.c_str());
    } else {
      LOG_ER("%s", reason.c_str());
      LOG_NO("not rebooting because su maintenance campaign is set: %s",
             su->suMaintenanceCampaign.c_str());
    }
  }

done:
  TRACE_LEAVE2("%u", rc);
  return rc;
}

/**
 * @brief       Checks if all csis of all the sis in this su are in removed
 *state
 *
 * @param [in]  cmp
 *
 * @returns     true/false
 **/
bool all_csis_in_assigned_state(const AVND_SU *su) {
  TRACE_ENTER2("'%s'", su->name.c_str());
  AVND_COMP_CSI_REC *curr_csi;
  AVND_SU_SI_REC *curr_si;
  bool all_csi_assigned = true;

  for (curr_si = (AVND_SU_SI_REC *)m_NCS_DBLIST_FIND_FIRST(&su->si_list);
       curr_si && all_csi_assigned;
       curr_si =
           (AVND_SU_SI_REC *)m_NCS_DBLIST_FIND_NEXT(&curr_si->su_dll_node)) {
    for (curr_csi =
             (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_FIRST(&curr_si->csi_list);
         curr_csi; curr_csi = (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_NEXT(
                       &curr_csi->si_dll_node)) {
      if (!m_AVND_COMP_CSI_CURR_ASSIGN_STATE_IS_ASSIGNED(curr_csi)) {
        all_csi_assigned = false;
        break;
      }
    }
  }
  TRACE_LEAVE2("all_csi_assigned:%u", all_csi_assigned);
  return all_csi_assigned;
}

/****************************************************************************
  Name          : avnd_su_pres_terming_compuninst_hdler

  Description   : This routine processes the `Comp Uninstantiated` event in
                  `Terminating` state.

  Arguments     : cb   - ptr to the AvND control block
                  su   - ptr to the su
                  comp - ptr to the comp (can be nullptr)

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avnd_su_pres_terming_compuninst_hdler(AVND_CB *cb, AVND_SU *su,
                                               AVND_COMP *comp) {
  AVND_COMP *curr_comp = 0;
  AVND_COMP_CSI_REC *curr_csi = 0;
  uint32_t rc = NCSCC_RC_SUCCESS;
  const std::string compname = comp ? comp->name : "none";
  TRACE_ENTER2(
      "Component Uninstantiated event in the Terminating state:'%s' : '%s'",
      su->name.c_str(), compname.c_str());
  // TODO: write whole if block into a separate function for PI SUand call it
  // here.
  if (m_AVND_SU_IS_PREINSTANTIABLE(su)) {
    TRACE("PI SU");
    if (m_AVND_SU_IS_FAILED(su)) {
      TRACE("SU is in Failed state");
      if (pi_su_all_comps_uninstantiated(*su) == true)
        avnd_su_pres_state_set(cb, su, SA_AMF_PRESENCE_UNINSTANTIATED);

      if (m_AVND_SU_IS_RESTART(su)) {
        for (curr_comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
                 m_NCS_DBLIST_FIND_LAST(&su->comp_list));
             curr_comp; curr_comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
                            m_NCS_DBLIST_FIND_PREV(&curr_comp->su_dll_node))) {
          if (curr_comp->pres == SA_AMF_PRESENCE_UNINSTANTIATED) continue;
          // Will pick up when terminating comp will move to uninstantiated
          // state.
          if ((curr_comp->pres == SA_AMF_PRESENCE_TERMINATING) ||
              (curr_comp->pres == SA_AMF_PRESENCE_RESTARTING))
            break;
          if (m_AVND_COMP_TYPE_IS_PREINSTANTIABLE(curr_comp)) {
            rc = avnd_comp_clc_fsm_run(cb, curr_comp,
                                       AVND_COMP_CLC_PRES_FSM_EV_CLEANUP);
            if (NCSCC_RC_SUCCESS != rc) goto done;
            break;
          } else {
            /*
               For a NPI comp in SU, component FSM is always triggered
               at the time of assignments. If this component is
               non-restartable then start reassginment from the
               whole SU now.
             */
            if (m_AVND_COMP_IS_RESTART_DIS(curr_comp) &&
                (curr_comp->csi_list.n_nodes > 0)) {
              TRACE(
                  "Start reassignment to different SU as '%s' is"
                  " not restartable",
                  curr_comp->name.c_str());
              su_send_suRestart_recovery_msg(su);
              goto done;
            } else {
              if (m_AVND_SU_IS_RESTART(su) && m_AVND_SU_IS_FAILED(su))
                rc = avnd_comp_clc_fsm_run(cb, curr_comp,
                                           AVND_COMP_CLC_PRES_FSM_EV_CLEANUP);
              else
                rc = avnd_comp_clc_fsm_run(cb, curr_comp,
                                           AVND_COMP_CLC_PRES_FSM_EV_RESTART);
              if (curr_comp->pres == SA_AMF_PRESENCE_TERMINATING)
                avnd_su_pres_state_set(cb, su, SA_AMF_PRESENCE_TERMINATING);
              break;
            }
          }
        }
      }
    } else if ((comp != nullptr) && (comp->admin_oper == true) &&
               (cb->term_state != AVND_TERM_STATE_OPENSAF_SHUTDOWN_STARTED) &&
               (m_AVND_COMP_IS_RESTART_DIS(comp))) {
      TRACE("Admin operation on component");
      if (pi_su_all_comps_uninstantiated(*su) == true)
        avnd_su_pres_state_set(cb, su, SA_AMF_PRESENCE_UNINSTANTIATED);
      avnd_comp_clc_fsm_run(cb, comp, AVND_COMP_CLC_PRES_FSM_EV_INST);
      avnd_su_pres_state_set(cb, su, SA_AMF_PRESENCE_INSTANTIATING);
    } else {
      TRACE("Admin operation on SU");
      for (curr_comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
               m_NCS_DBLIST_FIND_PREV(&comp->su_dll_node));
           curr_comp; curr_comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
                          m_NCS_DBLIST_FIND_PREV(&curr_comp->su_dll_node))) {
        TRACE_1("comp:'%s', Pres state:%u", curr_comp->name.c_str(),
                curr_comp->pres);
        if ((curr_comp->pres == SA_AMF_PRESENCE_RESTARTING) ||
            (curr_comp->pres == SA_AMF_PRESENCE_UNINSTANTIATED))
          continue;

        if (m_AVND_COMP_TYPE_IS_PREINSTANTIABLE(curr_comp)) {
          TRACE("Running the component clc FSM");
          if (su->admin_op_Id == SA_AMF_ADMIN_RESTART)
            rc = avnd_comp_clc_fsm_run(cb, curr_comp,
                                       AVND_COMP_CLC_PRES_FSM_EV_RESTART);
          else
            rc = avnd_comp_clc_fsm_run(cb, curr_comp,
                                       AVND_COMP_CLC_PRES_FSM_EV_TERM);
          if (NCSCC_RC_SUCCESS != rc) goto done;
          break;
        } else {
          /*
             For a NPI comp in PI SU, component FSM is always triggered
             at the time of assignments. If this component is non-restartable
             then start reassginment from the whole SU now, it will take care
             if its termination/clean up.
           */
          if ((nonrestartable(curr_comp) == true) &&
              (csi_count(curr_comp) > 0)) {
            TRACE(
                "Start reassignment to different SU as '%s' is"
                " not restartable",
                curr_comp->name.c_str());
            su_send_suRestart_recovery_msg(su);
            goto done;
          } else if (isAdminRestarted(su) == true) {
            rc = avnd_comp_clc_fsm_run(cb, curr_comp,
                                       AVND_COMP_CLC_PRES_FSM_EV_RESTART);
            if (curr_comp->pres == SA_AMF_PRESENCE_RESTARTING) goto done;
          }
        }
      }
      if (pi_su_all_comps_uninstantiated(*su) == true)
        avnd_su_pres_state_set(cb, su, SA_AMF_PRESENCE_UNINSTANTIATED);
      else if ((curr_comp == nullptr) &&
               (su->admin_op_Id == SA_AMF_ADMIN_RESTART)) {
        /*
           It means it is a SU comprising of assigned non restartable comps and
           restartable comps and it is restart admin op on su.
           Now instantiate SU honoring instantiation level.
         */
        rc = avnd_su_pres_fsm_run(cb, su, 0, AVND_SU_PRES_FSM_EV_INST);
        if (NCSCC_RC_SUCCESS != rc) goto done;
      }
    }
  }

  /*
   * If npi su, pick the prv csi & trigger it's comp fsm with TermEv.
   */
  if (!m_AVND_SU_IS_PREINSTANTIABLE(su) &&
      (!m_AVND_SU_IS_FAILED(su) || m_AVND_SU_IS_RESTART(su))) {
    TRACE("NPI SU");
    /* get the only csi rec */
    curr_csi = m_AVND_CSI_REC_FROM_COMP_DLL_NODE_GET(
        m_NCS_DBLIST_FIND_FIRST(&comp->csi_list));
    osafassert(curr_csi);

    /* mark the csi state assigned/removed */
    if (m_AVND_SU_SI_CURR_ASSIGN_STATE_IS_REMOVING(curr_csi->si))
      m_AVND_COMP_CSI_CURR_ASSIGN_STATE_SET(curr_csi,
                                            AVND_COMP_CSI_ASSIGN_STATE_REMOVED);
    else
      m_AVND_COMP_CSI_CURR_ASSIGN_STATE_SET(
          curr_csi, AVND_COMP_CSI_ASSIGN_STATE_ASSIGNED);

    if (curr_csi->single_csi_add_rem_in_si == AVSV_SUSI_ACT_DEL) {
      /* get here when a CSI is removed from a component in an NPI SU */
      assert(curr_csi->si->single_csi_add_rem_in_si == AVSV_SUSI_ACT_DEL);
      rc = avnd_su_si_oper_done(cb, su, curr_csi->si);
      avnd_su_pres_state_set(cb, su, SA_AMF_PRESENCE_INSTANTIATED);
      goto done;
    }

    /* get the prv csi */
    curr_csi =
        (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_PREV(&curr_csi->si_dll_node);
    if (curr_csi) {
      /* we have another csi. trigger the comp fsm with TermEv */
      TRACE("There's another CSI, Running the component clc FSM");
      if (m_AVND_SU_IS_RESTART(su) && m_AVND_SU_IS_FAILED(su))
        rc = avnd_comp_clc_fsm_trigger(cb, curr_csi->comp,
                                       AVND_COMP_CLC_PRES_FSM_EV_CLEANUP);
      else
        rc = avnd_comp_clc_fsm_trigger(cb, curr_csi->comp,
                                       (m_AVND_COMP_IS_FAILED(curr_csi->comp))
                                           ? AVND_COMP_CLC_PRES_FSM_EV_CLEANUP
                                           : AVND_COMP_CLC_PRES_FSM_EV_TERM);
      if (NCSCC_RC_SUCCESS != rc) goto done;
    }

    if (all_csis_in_assigned_state(su) || all_csis_in_removed_state(su)) {
      TRACE("SI Assignment done");
      avnd_su_pres_state_set(cb, su, SA_AMF_PRESENCE_UNINSTANTIATED);
      goto done;
    }

    /*
       During shutdown phase if a component faults, it will be cleaned up by
       AMFND irrespective of recovery policy. This component will move to
       UNINSTANTIATED after successful clean up. When amfnd starts removing SI
       from SU of this comp, it will have to skip the CSI of cleaned up
       component.
    */
    if ((curr_csi != nullptr) &&
        (curr_csi->comp->pres == SA_AMF_PRESENCE_UNINSTANTIATED) &&
        (cb->term_state == AVND_TERM_STATE_OPENSAF_SHUTDOWN_STARTED)) {
      m_AVND_COMP_CSI_CURR_ASSIGN_STATE_SET(curr_csi,
                                            AVND_COMP_CSI_ASSIGN_STATE_REMOVED);
      AVND_COMP_CSI_REC *assigned_csi =
          get_next_assigned_csi_from_end(curr_csi->si);
      m_AVND_COMP_CSI_CURR_ASSIGN_STATE_SET(
          assigned_csi, AVND_COMP_CSI_ASSIGN_STATE_REMOVING);
      rc = avnd_comp_clc_fsm_trigger(cb, assigned_csi->comp,
                                     (m_AVND_COMP_IS_FAILED(assigned_csi->comp))
                                         ? AVND_COMP_CLC_PRES_FSM_EV_CLEANUP
                                         : AVND_COMP_CLC_PRES_FSM_EV_TERM);
    }
  }

done:
  TRACE_LEAVE2("%u", rc);
  return rc;
}

/****************************************************************************
  Name          : avnd_su_pres_restart_suterm_hdler

  Description   : This routine processes the `SU Terminate` event in
                  `Restarting` state.

  Arguments     : cb   - ptr to the AvND control block
                  su   - ptr to the su
                  comp - ptr to the comp (can be nullptr)

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avnd_su_pres_restart_suterm_hdler(AVND_CB *cb, AVND_SU *su,
                                           AVND_COMP *comp) {
  AVND_COMP *curr_comp = 0;
  uint32_t rc = NCSCC_RC_SUCCESS;
  const std::string compname = comp ? comp->name : "none";
  TRACE_ENTER2("SU Terminate event in the Restarting state:'%s' : '%s'",
               su->name.c_str(), compname.c_str());

  /*
   * If pi su, pick all the instantiated/instantiating pi comps &
   * trigger their FSM with CleanupEv.
   */
  for (curr_comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
           m_NCS_DBLIST_FIND_FIRST(&su->comp_list));
       curr_comp; curr_comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
                      m_NCS_DBLIST_FIND_NEXT(&curr_comp->su_dll_node))) {
    /* terminate the non-uninstantiated pi comp */
    if ((!m_AVND_COMP_PRES_STATE_IS_UNINSTANTIATED(curr_comp)) &&
        (m_AVND_COMP_TYPE_IS_PREINSTANTIABLE(curr_comp))) {
      /* mark the comp failed */
      m_AVND_COMP_FAILED_SET(curr_comp);

      /* update comp oper state */
      m_AVND_COMP_OPER_STATE_SET(curr_comp, SA_AMF_OPERATIONAL_DISABLED);
      rc = avnd_comp_oper_state_avd_sync(cb, curr_comp);
      if (NCSCC_RC_SUCCESS != rc) goto done;

      rc = avnd_comp_clc_fsm_run(cb, curr_comp,
                                 AVND_COMP_CLC_PRES_FSM_EV_CLEANUP);
      if (NCSCC_RC_SUCCESS != rc) goto done;
    }
  } /* for */

  /* transition to terminating state */
  avnd_su_pres_state_set(cb, su, SA_AMF_PRESENCE_TERMINATING);

done:
  TRACE_LEAVE2("%u", rc);
  return rc;
}

/**
 * @brief  handler for a component restart event when SU is in restarting state.
 *         This will be invoked when  either a comp is instantiated or
 * terminated restarting state. It will be used during restart admin operation
 * on su.
 * @param  ptr to avnd_cb.
 * @param  ptr to su.
 * @param  ptr to comp.
 * @return  NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE.
 */
uint32_t avnd_su_pres_restart_comprestart_hdler(AVND_CB *cb, AVND_SU *su,
                                                AVND_COMP *comp) {
  AVND_COMP *curr_comp = 0;
  AVND_SU_SI_REC *si = 0;
  AVND_COMP_CSI_REC *csi = 0;
  uint32_t rc = NCSCC_RC_SUCCESS;
  TRACE_ENTER2("Comp restart event while su is restarting: '%s'",
               su->name.c_str());

  /* This event comes when a component in restarting state  is successfully
     terminated or instantiated.
   */
  if (m_AVND_SU_IS_PREINSTANTIABLE(su)) {
    TRACE("PI SU:'%s'", su->name.c_str());
    /*All restartable PI comps are terminated when SU remains in instantiated
      state. After restarting all the PI comps in restarting state, su will be
      marked restarting. Some NPI comps may remain instantiated, terminate them
      now.
     */
    for (curr_comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
             m_NCS_DBLIST_FIND_LAST(&su->comp_list));
         curr_comp; curr_comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
                        m_NCS_DBLIST_FIND_PREV(&curr_comp->su_dll_node))) {
      TRACE("%s", curr_comp->name.c_str());
      if (m_AVND_COMP_TYPE_IS_PREINSTANTIABLE(curr_comp)) continue;
      if ((!m_AVND_COMP_TYPE_IS_PREINSTANTIABLE(curr_comp)) &&
          (curr_comp->pres == SA_AMF_PRESENCE_INSTANTIATED)) {
        rc = avnd_comp_clc_fsm_run(cb, curr_comp,
                                   AVND_COMP_CLC_PRES_FSM_EV_RESTART);
        goto done;
      }
    }
    /* It means last pi component got terminated,now instantiate the first
     * comp.*/
    for (curr_comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
             m_NCS_DBLIST_FIND_FIRST(&su->comp_list));
         curr_comp; curr_comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
                        m_NCS_DBLIST_FIND_NEXT(&curr_comp->su_dll_node))) {
      TRACE("%s", curr_comp->name.c_str());
      if (m_AVND_COMP_TYPE_IS_PREINSTANTIABLE(curr_comp) &&
          (curr_comp->pres == SA_AMF_PRESENCE_RESTARTING)) {
        rc = avnd_comp_clc_fsm_run(cb, curr_comp,
                                   AVND_COMP_CLC_PRES_FSM_EV_INST);
        if (NCSCC_RC_SUCCESS != rc) goto done;
        break;
      }
    }
  }

  /*
   * If npi su, it'll have only one si-rec in the si-list. Pick the
   * lowest ranked csi belonging to this si & trigger it's comp fsm.
   */
  if (!m_AVND_SU_IS_PREINSTANTIABLE(su)) {
    TRACE("NPI SU:'%s'", su->name.c_str());
    /* get the only si rec */
    si = (AVND_SU_SI_REC *)m_NCS_DBLIST_FIND_FIRST(&su->si_list);
    osafassert(si);
    csi = (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_FIRST(&si->csi_list);
    if (csi) {
      /* This CSI must be in restarting state. We will mark it
         AVND_COMP_CSI_ASSIGN_STATE_ASSIGNED after the instantiation
         of associated component.
       */
      TRACE("Running the component CLC FSM for csi:%s, comp:%s",
            csi->name.c_str(), comp->name.c_str());
      rc = avnd_comp_clc_fsm_run(cb, csi->comp, AVND_COMP_CLC_PRES_FSM_EV_INST);
      if (NCSCC_RC_SUCCESS != rc) goto done;
    }
  }
done:
  TRACE_LEAVE();
  return rc;
}
/****************************************************************************
  Name          : avnd_su_pres_restart_compinst_hdler

  Description   : This routine processes the `CompInstantiated` event in
                  `Restarting` state.

  Arguments     : cb   - ptr to the AvND control block
                  su   - ptr to the su
                  comp - ptr to the comp (can be nullptr)

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avnd_su_pres_restart_compinst_hdler(AVND_CB *cb, AVND_SU *su,
                                             AVND_COMP *comp) {
  AVND_COMP *curr_comp = 0;
  AVND_COMP_CSI_REC *curr_csi = 0;
  uint32_t rc = NCSCC_RC_SUCCESS;
  const std::string compname = comp ? comp->name : "none";
  TRACE_ENTER2(
      "ComponentInstantiated event in the Restarting state:'%s' : '%s'",
      su->name.c_str(), compname.c_str());
  SaAmfPresenceStateT pres_init = su->pres;
  /*
   * If pi su, pick the next pi comp & trigger it's FSM with Inst Event.
   */
  if (m_AVND_SU_IS_PREINSTANTIABLE(su)) {
    TRACE("PI SU:'%s'", su->name.c_str());
    /* Mark SU instantiated if atleast one PI comp is in instantiated state.*/
    for (curr_comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
             m_NCS_DBLIST_FIND_FIRST(&su->comp_list));
         curr_comp && (su->pres != SA_AMF_PRESENCE_INSTANTIATED);
         curr_comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
             m_NCS_DBLIST_FIND_NEXT(&curr_comp->su_dll_node))) {
      if ((curr_comp->pres == SA_AMF_PRESENCE_INSTANTIATED) &&
          (m_AVND_COMP_TYPE_IS_PREINSTANTIABLE(curr_comp)))
        avnd_su_pres_state_set(cb, su, SA_AMF_PRESENCE_INSTANTIATED);
    }
    for (curr_comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
             m_NCS_DBLIST_FIND_NEXT(&comp->su_dll_node));
         curr_comp; curr_comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
                        m_NCS_DBLIST_FIND_NEXT(&curr_comp->su_dll_node))) {
      if (curr_comp->pres == SA_AMF_PRESENCE_INSTANTIATED) continue;
      if (m_AVND_COMP_TYPE_IS_PREINSTANTIABLE(curr_comp) &&
          (curr_comp->pres == SA_AMF_PRESENCE_RESTARTING) &&
          (m_AVND_SU_IS_RESTART(su))) {
        rc = avnd_comp_clc_fsm_run(cb, curr_comp,
                                   AVND_COMP_CLC_PRES_FSM_EV_INST);
        if (NCSCC_RC_SUCCESS != rc) goto done;
        break;
      }
    } /* for */

    /*If all comps are instantiated then reassign the SU execpt in a single comp
     * case.*/
    if ((pres_init == SA_AMF_PRESENCE_INSTANTIATED) &&
        (su_all_pi_comps_instantiated(su) == true))
      rc = pi_su_instantiating_to_instantiated(su);
  }

  /*
   * If npi su, pick the next csi & trigger it's comp fsm with RestartEv.
   */
  if (!m_AVND_SU_IS_PREINSTANTIABLE(su)) {
    TRACE("NPI SU:'%s'", su->name.c_str());
    /* get the only csi rec */
    curr_csi = m_AVND_CSI_REC_FROM_COMP_DLL_NODE_GET(
        m_NCS_DBLIST_FIND_FIRST(&comp->csi_list));
    osafassert(curr_csi);

    /* mark the csi state assigned */
    m_AVND_COMP_CSI_CURR_ASSIGN_STATE_SET(curr_csi,
                                          AVND_COMP_CSI_ASSIGN_STATE_ASSIGNED);
    if (su->pres != SA_AMF_PRESENCE_INSTANTIATED)
      avnd_su_pres_state_set(cb, su, SA_AMF_PRESENCE_INSTANTIATED);

    /* get the next csi */
    curr_csi =
        (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_NEXT(&curr_csi->si_dll_node);

    /* Restart next component associated with unassigned CSI and if the
       component is not already in RESTARTING state.
     */
    if ((curr_csi != nullptr) &&
        (m_AVND_COMP_CSI_CURR_ASSIGN_STATE_IS_RESTARTING(curr_csi) == true) &&
        (m_AVND_SU_IS_RESTART(su))) {
      /* we have another csi. trigger the comp fsm with Inst event*/
      rc = avnd_comp_clc_fsm_trigger(cb, curr_csi->comp,
                                     AVND_COMP_CLC_PRES_FSM_EV_INST);
      if (NCSCC_RC_SUCCESS != rc) goto done;
    }
    /*If all comps are instantiated then reset SU params.*/
    if ((pres_init == SA_AMF_PRESENCE_INSTANTIATED) &&
        (all_csis_in_assigned_state(su) == true))
      rc = npi_su_instantiating_to_instantiated(su);
  }

done:
  TRACE_LEAVE2("%u", rc);
  return rc;
}

/****************************************************************************
  Name          : avnd_su_pres_restart_compterming_hdler

  Description   : This routine processes the `SU Terminate` event in
                  `Instantiating` state.

  Arguments     : cb   - ptr to the AvND control block
                  su   - ptr to the su
                  comp - ptr to the comp (can be nullptr)

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : Note that this event is only generated for PI SUs. SUTermEv
                  is generated for NPI SUs only during stable states i.e.
                  uninstantiated & instantiated states. This is done by
                  avoiding overlapping SI assignments.
******************************************************************************/
uint32_t avnd_su_pres_restart_compterming_hdler(AVND_CB *cb, AVND_SU *su,
                                                AVND_COMP *comp) {
  AVND_COMP *curr_comp = 0;
  uint32_t rc = NCSCC_RC_SUCCESS;
  const std::string compname = comp ? comp->name : "none";
  TRACE_ENTER2("SUTerminate event in the Instantiating state:'%s' : '%s'",
               su->name.c_str(), compname.c_str());

  if (m_AVND_SU_IS_ADMN_TERM(su)) {
    /* This case will be hit, when an SU is already restarting and admin
     * is trying to terminate it. As a part of admn term in SU restart
     * we would clean up all the comp in SU. while this cleanup is happening
     * for each componet, they will trigger SU FSM and this particular case
     * will be hit.
     * we need not do anything here.
     */
    goto done;
  }

  /*
   * If pi su, pick all the instantiated/instantiating pi comps &
   * trigger their FSM with TermEv.
   */
  for (curr_comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
           m_NCS_DBLIST_FIND_FIRST(&su->comp_list));
       curr_comp; curr_comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
                      m_NCS_DBLIST_FIND_NEXT(&curr_comp->su_dll_node))) {
    /* skip the npi comps */
    if (!m_AVND_COMP_TYPE_IS_PREINSTANTIABLE(curr_comp)) continue;

    /* terminate the non-uninstantiated pi comp */
    if (!m_AVND_COMP_PRES_STATE_IS_UNINSTANTIATED(curr_comp) &&
        (!m_AVND_COMP_PRES_STATE_IS_TERMINATING(curr_comp))) {
      /* mark the comp failed */
      m_AVND_COMP_FAILED_SET(curr_comp);

      /* update comp oper state */
      m_AVND_COMP_OPER_STATE_SET(curr_comp, SA_AMF_OPERATIONAL_DISABLED);
      rc = avnd_comp_oper_state_avd_sync(cb, curr_comp);
      if (NCSCC_RC_SUCCESS != rc) goto done;

      rc = avnd_comp_clc_fsm_run(cb, curr_comp,
                                 AVND_COMP_CLC_PRES_FSM_EV_CLEANUP);
      if (NCSCC_RC_SUCCESS != rc) goto done;
    }
  } /* for */

  /* transition to terminating state */
  avnd_su_pres_state_set(cb, su, SA_AMF_PRESENCE_TERMINATING);

done:
  TRACE_LEAVE2("%u", rc);
  return rc;
}

/****************************************************************************
  Name          : avnd_su_pres_inst_compinstfail_hdler

  Description   : This routine processes the `CompInstantiateFailed` event in
                  `Instantiated` state.

  Arguments     : cb   - ptr to the AvND control block
                  su   - ptr to the su
                  comp - ptr to the comp (can be nullptr)

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : This function is reused for the following su presence
                  state transitions also
                  INSTANTIATED -> INSTANTIATIONFAIL
                  TERMINATING  -> INSTANTIATIONFAIL
                  RESTARTING   -> INSTANTIATIONFAIL
******************************************************************************/
uint32_t avnd_su_pres_inst_compinstfail_hdler(AVND_CB *cb, AVND_SU *su,
                                              AVND_COMP *comp) {
  AVND_COMP *curr_comp = 0;
  AVND_SU_SI_REC *si = 0;
  AVND_COMP_CSI_REC *curr_csi = 0;
  uint32_t rc = NCSCC_RC_SUCCESS;
  uint32_t comp_count = 0;
  const std::string compname = comp ? comp->name : "none";
  TRACE_ENTER2(
      "Component Instantiation failed event in the Instantiated state: '%s' : '%s'",
      su->name.c_str(), compname.c_str());

  /* transition to inst-failed state */
  avnd_su_pres_state_set(cb, su, SA_AMF_PRESENCE_INSTANTIATION_FAILED);
  m_AVND_SU_ALL_TERM_RESET(su);

  /*
   * If pi su, pick all the instantiated/instantiating pi comps &
   * trigger their FSM with TermEv.
   */
  if (m_AVND_SU_IS_PREINSTANTIABLE(su)) {
    for (curr_comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
             m_NCS_DBLIST_FIND_LAST(&su->comp_list));
         curr_comp; curr_comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
                        m_NCS_DBLIST_FIND_PREV(&curr_comp->su_dll_node))) {
      /* skip the npi comps */
      if (!m_AVND_COMP_TYPE_IS_PREINSTANTIABLE(curr_comp)) continue;

      comp_count++;
      /* terminate the non-uninstantiated pi healthy comp && clean the faulty
       * comps */
      if ((!m_AVND_COMP_PRES_STATE_IS_UNINSTANTIATED(curr_comp)) &&
          (!m_AVND_COMP_IS_FAILED(curr_comp))) {
        /* if this comp was getting assigned, mark it done */
        avnd_comp_cmplete_all_assignment(cb, curr_comp);
        avnd_comp_cmplete_all_csi_rec(cb, curr_comp);
        rc = avnd_comp_clc_fsm_run(cb, curr_comp,
                                   AVND_COMP_CLC_PRES_FSM_EV_TERM);
        if (NCSCC_RC_SUCCESS != rc) goto done;
      } else if ((!m_AVND_COMP_PRES_STATE_IS_UNINSTANTIATED(curr_comp)) &&
                 (m_AVND_COMP_IS_FAILED(curr_comp))) {
        /* if this comp was getting assigned, mark it done */
        avnd_comp_cmplete_all_assignment(cb, curr_comp);
        avnd_comp_cmplete_all_csi_rec(cb, curr_comp);
        rc = avnd_comp_clc_fsm_run(cb, curr_comp,
                                   AVND_COMP_CLC_PRES_FSM_EV_CLEANUP);
        if (NCSCC_RC_SUCCESS != rc) goto done;
      }
    } /* for */
    if (1 == comp_count) {
      /* If the componenet was alone then we need to set SU to term state and
         process the SUSI assignment.*/
      m_AVND_SU_ALL_TERM_SET(su);
      avnd_su_siq_prc(cb, su);
    }
  }

  /*
   * If npi su, pick all the assigned/assigning comps &
   * trigger their comp fsm with TermEv
   */
  if (!m_AVND_SU_IS_PREINSTANTIABLE(su)) {
    /* get the only si rec */
    si = (AVND_SU_SI_REC *)m_NCS_DBLIST_FIND_FIRST(&su->si_list);
    osafassert(si);

    for (curr_csi = (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_FIRST(&si->csi_list);
         curr_csi; curr_csi = (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_NEXT(
                       &curr_csi->si_dll_node)) {
      /* terminate the component containing non-unassigned csi */
      if (!m_AVND_COMP_CSI_CURR_ASSIGN_STATE_IS_UNASSIGNED(curr_csi)) {
        rc = avnd_comp_clc_fsm_run(cb, curr_csi->comp,
                                   AVND_COMP_CLC_PRES_FSM_EV_TERM);
        if (NCSCC_RC_SUCCESS != rc) goto done;
      }
    } /* for */
    m_AVND_SU_ALL_TERM_SET(su);
  }

done:
  TRACE_LEAVE2("%u", rc);
  return rc;
}

/****************************************************************************
  Name          : avnd_su_pres_instfailed_compuninst

  Description   : This routine processes the `CompInstantiateFailed` event &
                  `CompUnInstantiated` event in `Instantiationfailed` state.

  Arguments     : cb   - ptr to the AvND control block
                  su   - ptr to the su
                  comp - ptr to the comp (can be nullptr)

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avnd_su_pres_instfailed_compuninst(AVND_CB *cb, AVND_SU *su,
                                            AVND_COMP *comp) {
  AVND_COMP *curr_comp = 0;
  AVND_SU_SIQ_REC *siq = 0;
  uint32_t rc = NCSCC_RC_SUCCESS;
  const std::string compname = comp ? comp->name : "none";
  TRACE_ENTER2(
      "CompInstantiateFailed/CompUnInstantiated event in the InstantiationFailed state:'%s', '%s'",
      su->name.c_str(), compname.c_str());

  /* check whether all pi comps are terminated  */
  if (m_AVND_SU_IS_PREINSTANTIABLE(su)) {
    for (curr_comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
             m_NCS_DBLIST_FIND_FIRST(&su->comp_list));
         curr_comp; curr_comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
                        m_NCS_DBLIST_FIND_NEXT(&curr_comp->su_dll_node))) {
      /* skip the npi comps */
      if (!m_AVND_COMP_TYPE_IS_PREINSTANTIABLE(curr_comp)) continue;

      if (!m_AVND_COMP_PRES_STATE_IS_UNINSTANTIATED(curr_comp) &&
          !m_AVND_COMP_PRES_STATE_IS_INSTANTIATIONFAILED(curr_comp)) {
        m_AVND_SU_ALL_TERM_RESET(su);

        if (m_AVND_COMP_PRES_STATE_IS_TERMINATIONFAILED(curr_comp)) {
          /* why waste memory -free entire queue */
          siq = (AVND_SU_SIQ_REC *)m_NCS_DBLIST_FIND_LAST(&su->siq);
          while (siq) {
            /* unlink the buffered msg from the queue */
            ncs_db_link_list_delink(&su->siq, &siq->su_dll_node);

            /* delete the buffered msg */
            avnd_su_siq_rec_del(cb, su, siq);

            siq = (AVND_SU_SIQ_REC *)m_NCS_DBLIST_FIND_LAST(&su->siq);
          }
        }
        return rc;
      }

    } /* for */

    m_AVND_SU_ALL_TERM_SET(su);
    avnd_su_siq_prc(cb, su);
  }

  TRACE_LEAVE2("%u", rc);
  return rc;
}

/**
 * This function checks if the sufailover is going on.
 * @param su: ptr to the SU .
 *
 * @return true/false.
 */
bool sufailover_in_progress(const AVND_SU *su) {
  if (m_AVND_SU_IS_FAILED(su) && (su->sufailover) &&
      (!m_AVND_SU_IS_RESTART(su)) &&
      (avnd_cb->oper_state != SA_AMF_OPERATIONAL_DISABLED) && (!su->is_ncs) &&
      m_AVND_SU_IS_FAILOVER(su))
    return true;
  return false;
}

/**
 * This function checks if the sufailover and node switchover are going on.
 * @param su: ptr to the SU .
 *
 * @return true/false.
 */
bool sufailover_during_nodeswitchover(const AVND_SU *su) {
  if ((m_AVND_SU_IS_FAILED(su) && (su->sufailover) &&
       (!m_AVND_SU_IS_RESTART(su)) &&
       (avnd_cb->term_state == AVND_TERM_STATE_NODE_SWITCHOVER_STARTED) &&
       (!su->is_ncs) && (m_AVND_SU_IS_FAILOVER(su))))
    return true;

  return false;
}

/**
 * @brief  handler for restart recovery or admin op on su
 *         when a SU is in terminating state.
 * @param  ptr to avnd_cb.
 * @param  ptr to su.
 * @param  ptr to comp.
 * @return  NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE.
 */
uint32_t avnd_su_pres_terming_surestart_hdler(AVND_CB *cb, AVND_SU *su,
                                              AVND_COMP *comp) {
  AVND_COMP *curr_comp = 0;
  AVND_SU_SI_REC *si = 0;
  AVND_COMP_CSI_REC *csi = 0;
  uint32_t rc = NCSCC_RC_SUCCESS;
  TRACE_ENTER2("SURestart event in SU terminating state: '%s'",
               su->name.c_str());

  if (m_AVND_SU_IS_PREINSTANTIABLE(su)) {
    TRACE("PI SU:'%s'", su->name.c_str());
    for (curr_comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
             m_NCS_DBLIST_FIND_LAST(&su->comp_list));
         curr_comp; curr_comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
                        m_NCS_DBLIST_FIND_PREV(&curr_comp->su_dll_node))) {
      if ((curr_comp->pres == SA_AMF_PRESENCE_RESTARTING) ||
          (curr_comp->pres == SA_AMF_PRESENCE_UNINSTANTIATED))
        continue;
      if (m_AVND_COMP_TYPE_IS_PREINSTANTIABLE(curr_comp)) {
        if (m_AVND_SU_IS_RESTART(su) && m_AVND_SU_IS_FAILED(su))
          rc = avnd_comp_clc_fsm_run(cb, curr_comp,
                                     AVND_COMP_CLC_PRES_FSM_EV_CLEANUP);
        else
          rc = avnd_comp_clc_fsm_run(cb, curr_comp,
                                     AVND_COMP_CLC_PRES_FSM_EV_RESTART);
        if (NCSCC_RC_SUCCESS != rc) goto done;
        break;
      } else {
        /*
           For a NPI comp in SU, component FSM is always triggered at the time
           of assignments. If this component is non-restartable then start
           reassginment from the whole SU now.
         */
        if (m_AVND_COMP_IS_RESTART_DIS(curr_comp) &&
            (curr_comp->csi_list.n_nodes > 0)) {
          TRACE(
              "Start reassignment to different SU as '%s' is"
              " not restartable",
              curr_comp->name.c_str());
          su_send_suRestart_recovery_msg(su);
          goto done;
        } else {
          // NPI comp in PI SU, clean it up now.
          rc = avnd_comp_clc_fsm_run(cb, curr_comp,
                                     AVND_COMP_CLC_PRES_FSM_EV_CLEANUP);
          break;
        }
      }

    } /* for */
  }

  /*TODO_SURESTART:Will relook for NPI SU as there seems a rare possbility for
    surestart for NPI SU in terminating state .*/
  /*
   * If npi su, it'll have only one si-rec in the si-list. Pick the
   * lowest ranked csi belonging to this si & trigger it's comp fsm.
   */
  if (!m_AVND_SU_IS_PREINSTANTIABLE(su)) {
    TRACE("NPI SU:'%s'", su->name.c_str());
    /* get the only si rec */
    si = (AVND_SU_SI_REC *)m_NCS_DBLIST_FIND_FIRST(&su->si_list);
    osafassert(si);

    csi = (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_LAST(&si->csi_list);
    if (csi) {
      m_AVND_COMP_CSI_CURR_ASSIGN_STATE_SET(
          csi, AVND_COMP_CSI_ASSIGN_STATE_RESTARTING);

      TRACE("Running the component clc FSM for csi:'%s', comp:%s",
            csi->name.c_str(), csi->comp->name.c_str());
      /* restart the comp */
      rc = avnd_comp_clc_fsm_run(cb, csi->comp,
                                 AVND_COMP_CLC_PRES_FSM_EV_RESTART);
      if (NCSCC_RC_SUCCESS != rc) goto done;
    }
    if (all_csis_in_restarting_state(su) == true) {
      TRACE("All CSIs are in restarting state, so marking SU restarting");
      avnd_su_pres_state_set(cb, su, SA_AMF_PRESENCE_RESTARTING);
    }
  }
done:
  TRACE_LEAVE2("%u", rc);
  return rc;
}

/**
 * @brief  handler for restart admin op on su
 *         when a SU is in terminating state. Comp FSM will
 *         invoke this event when a restartable component is terminated
 *         during restart admin op on su.
 * @param  ptr to avnd_cb.
 * @param  ptr to su.
 * @param  ptr to comp.
 * @return  NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE.
 */
uint32_t avnd_su_pres_terming_comprestart_hdler(AVND_CB *cb, AVND_SU *su,
                                                AVND_COMP *comp) {
  uint32_t rc = NCSCC_RC_SUCCESS;
  const std::string compname = comp ? comp->name : "none";
  TRACE_ENTER2("Component restart event in the Instantiated state, '%s' : '%s'",
               su->name.c_str(), compname.c_str());
  if (m_AVND_SU_IS_PREINSTANTIABLE(su)) {
    TRACE("PI SU");
    AVND_COMP *curr_comp = nullptr;
    for (curr_comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
             m_NCS_DBLIST_FIND_PREV(&comp->su_dll_node));
         curr_comp; curr_comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
                        m_NCS_DBLIST_FIND_PREV(&curr_comp->su_dll_node))) {
      if ((curr_comp->pres == SA_AMF_PRESENCE_RESTARTING) ||
          (curr_comp->pres == SA_AMF_PRESENCE_UNINSTANTIATED))
        continue;
      if (m_AVND_COMP_TYPE_IS_PREINSTANTIABLE(curr_comp)) {
        TRACE("Running the component clc FSM");
        rc = avnd_comp_clc_fsm_run(cb, curr_comp,
                                   AVND_COMP_CLC_PRES_FSM_EV_RESTART);
        if (NCSCC_RC_SUCCESS != rc) goto done;
        break;
      }
    }
    if (!curr_comp) {
      /*
         It means all comps are terminated in surestart admin op.
         Now instantiate SU honoring instantiation level.
      */
      rc = avnd_su_pres_fsm_run(cb, su, 0, AVND_SU_PRES_FSM_EV_INST);
      if (NCSCC_RC_SUCCESS != rc) goto done;
    }
  }
done:
  TRACE_LEAVE();
  return rc;
}

/**
 * @brief  handler for instantiating a SU when it is in terminating state.
 *         SU FSM will invoke this handler when all components are terminated.
 *         It will start instantiating comps in SU.
 * @param  ptr to avnd_cb.
 * @param  ptr to su.
 * @param  ptr to comp.
 * @return  NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE.
 */
uint32_t avnd_su_pres_terming_suinst_hdler(AVND_CB *cb, AVND_SU *su,
                                           AVND_COMP *comp) {
  AVND_COMP *curr_comp = 0;
  AVND_SU_SI_REC *si = 0;
  AVND_COMP_CSI_REC *csi = 0;
  uint32_t rc = NCSCC_RC_SUCCESS;
  TRACE_ENTER2("SU Instantiate event in Terminating state: '%s'",
               su->name.c_str());

  /*
   * If pi su, pick the first pi comp & trigger it's FSM with InstEv.
   */
  if (m_AVND_SU_IS_PREINSTANTIABLE(su)) {
    TRACE("PI SU:'%s'", su->name.c_str());
    for (curr_comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
             m_NCS_DBLIST_FIND_FIRST(&su->comp_list));
         curr_comp; curr_comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
                        m_NCS_DBLIST_FIND_NEXT(&curr_comp->su_dll_node))) {
      /* instantiate the pi comp */
      TRACE("%s", curr_comp->name.c_str());
      if (m_AVND_COMP_TYPE_IS_PREINSTANTIABLE(curr_comp) &&
          ((curr_comp->pres == SA_AMF_PRESENCE_RESTARTING) ||
           (curr_comp->pres == SA_AMF_PRESENCE_UNINSTANTIATED))) {
        TRACE("Running the component CLC FSM ");
        rc = avnd_comp_clc_fsm_run(cb, curr_comp,
                                   AVND_COMP_CLC_PRES_FSM_EV_INST);
        if (NCSCC_RC_SUCCESS != rc) goto done;
        break;
      }
    } /* for */
    if ((curr_comp) && (curr_comp->pres == SA_AMF_PRESENCE_INSTANTIATING) &&
        (su->pres == SA_AMF_PRESENCE_TERMINATING))
      avnd_su_pres_state_set(cb, su, SA_AMF_PRESENCE_INSTANTIATING);
  }

  /*TODO_SURESTART:Will relook for NPI SU as there seems a rare possbility for
    su instantiate event for NPI SU in terminating state .*/
  /*
   * If npi su, it'll have only one si-rec in the si-list. Pick the
   * lowest ranked csi belonging to this si & trigger it's comp fsm.
   */
  if (!m_AVND_SU_IS_PREINSTANTIABLE(su)) {
    TRACE("NPI SU:'%s'", su->name.c_str());
    si = (AVND_SU_SI_REC *)m_NCS_DBLIST_FIND_FIRST(&su->si_list);
    osafassert(si);

    csi = (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_FIRST(&si->csi_list);
    if ((csi) && (csi->comp)) {
      /* mark the csi state assigning */
      m_AVND_COMP_CSI_CURR_ASSIGN_STATE_SET(
          csi, AVND_COMP_CSI_ASSIGN_STATE_ASSIGNING);

      TRACE("Running the component CLC FSM ");
      /* instantiate the comp */
      rc = avnd_comp_clc_fsm_run(cb, csi->comp, AVND_COMP_CLC_PRES_FSM_EV_INST);
      if (NCSCC_RC_SUCCESS != rc) goto done;
      if ((csi->comp->pres == SA_AMF_PRESENCE_INSTANTIATING) &&
          (su->pres == SA_AMF_PRESENCE_TERMINATING))
        avnd_su_pres_state_set(cb, su, SA_AMF_PRESENCE_INSTANTIATING);
    }
  }

done:
  if (rc == NCSCC_RC_FAILURE)
    avnd_su_pres_state_set(cb, su, SA_AMF_PRESENCE_INSTANTIATION_FAILED);
  TRACE_LEAVE2("%u", rc);
  return rc;
}

/**
 * @brief  During restart admin op on SU, a SU enters into INSTANTIATED state
 *         as soon as the first component is succesfully instantiated. For all
 * other components in SU, this handler will take care of their instantiation
 * honoring instantiation-level.
 * @param  ptr to avnd_cb.
 * @param  ptr to su.
 * @param  ptr to comp.
 * @return  NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE.
 */
uint32_t avnd_su_pres_inst_compinst_hdler(AVND_CB *cb, AVND_SU *su,
                                          AVND_COMP *comp) {
  AVND_COMP *curr_comp = 0;
  AVND_COMP_CSI_REC *curr_csi = 0;
  uint32_t rc = NCSCC_RC_SUCCESS;
  const std::string compname = comp ? comp->name : "none";
  TRACE_ENTER2(
      "Component Instantiated event in the Instantiated state:'%s' : '%s'",
      su->name.c_str(), compname.c_str());

  if (m_AVND_SU_IS_PREINSTANTIABLE(su)) {
    TRACE("PI SU");
    for (curr_comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
             m_NCS_DBLIST_FIND_NEXT(&comp->su_dll_node));
         curr_comp; curr_comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
                        m_NCS_DBLIST_FIND_NEXT(&curr_comp->su_dll_node))) {
      if (curr_comp->pres == SA_AMF_PRESENCE_INSTANTIATED) continue;
      if (m_AVND_COMP_TYPE_IS_PREINSTANTIABLE(curr_comp) &&
          (curr_comp->pres == SA_AMF_PRESENCE_RESTARTING) &&
          (m_AVND_SU_IS_RESTART(su))) {
        TRACE("Running the component clc FSM for '%s'",
              curr_comp->name.c_str());
        rc = avnd_comp_clc_fsm_run(cb, curr_comp,
                                   AVND_COMP_CLC_PRES_FSM_EV_INST);
        if (NCSCC_RC_SUCCESS != rc) goto done;
        break;
      }
    }
    if (su_all_pi_comps_instantiated(su) == true)
      rc = pi_su_instantiating_to_instantiated(su);
  }

  if (!m_AVND_SU_IS_PREINSTANTIABLE(su)) {
    TRACE("NPI SU");
    /* get the only csi rec */
    curr_csi = m_AVND_CSI_REC_FROM_COMP_DLL_NODE_GET(
        m_NCS_DBLIST_FIND_FIRST(&comp->csi_list));
    osafassert(curr_csi);

    m_AVND_COMP_CSI_CURR_ASSIGN_STATE_SET(curr_csi,
                                          AVND_COMP_CSI_ASSIGN_STATE_ASSIGNED);

    if (su->pres != SA_AMF_PRESENCE_INSTANTIATED)
      avnd_su_pres_state_set(cb, su, SA_AMF_PRESENCE_INSTANTIATED);

    /* get the next csi */
    curr_csi =
        (AVND_COMP_CSI_REC *)m_NCS_DBLIST_FIND_NEXT(&curr_csi->si_dll_node);
    if ((curr_csi != nullptr) &&
        (m_AVND_COMP_CSI_CURR_ASSIGN_STATE_IS_RESTARTING(curr_csi) == true) &&
        (m_AVND_SU_IS_RESTART(su))) {
      /* we have another csi. trigger the comp fsm with InstEv */
      TRACE(
          "There's another CSI:'%s', Running the component clc FSM for comp:'%s'",
          curr_csi->name.c_str(), curr_csi->comp->name.c_str());
      rc = avnd_comp_clc_fsm_trigger(cb, curr_csi->comp,
                                     AVND_COMP_CLC_PRES_FSM_EV_INST);
      if (NCSCC_RC_SUCCESS != rc) goto done;
    }
    /*If all comps are instantiated then reset SU params.*/
    if (all_csis_in_assigned_state(su) == true) {
      rc = npi_su_instantiating_to_instantiated(su);
    }
  }
done:
  TRACE_LEAVE2("%u", rc);
  return rc;
}

/**
 * @brief       This function processes events from IR thread.
 *
 * @param       ptr to the AvND control block
 * @param       ptr to the AvND event
 *
 * @return      NCSCC_RC_FAILURE/NCSCC_RC_SUCCESS
 */
uint32_t avnd_evt_ir_evh(struct avnd_cb_tag *cb, struct avnd_evt_tag *evt) {
  uint32_t rc = NCSCC_RC_SUCCESS;
  AVND_SU *su = 0;
  TRACE_ENTER2("su name '%s'",
               osaf_extended_name_borrow(&evt->info.ir_evt.su_name));

  /* get the su */
  su = avnd_sudb_rec_get(cb->sudb, Amf::to_string(&evt->info.ir_evt.su_name));
  if (!su) {
    TRACE("SU'%s', not found in DB",
          osaf_extended_name_borrow(&evt->info.ir_evt.su_name));
    goto done;
  }

  TRACE("SU term state is set to false");
  /* Reset admn term operation flag */
  m_AVND_SU_ADMN_TERM_RESET(su);
  /* Add components belonging to this SU if components were not added before.
     This can happen in runtime when SU is first added and then comp. When SU is
     added amfd will send SU info to amfnd, at this point of time no component
     exists in IMM DB, so SU list of comp is empty. When comp are added later,
     it is not sent to amfnd as amfnd reads comp info from IMM DB. Now when
     unlock-inst is done then avnd_evt_avd_su_pres_msg is called. At this point
     of time, we are not having Comp info in SU list, so need to add it.

     If component exists as it would be in case controller is coming up with all
     entities configured, then avnd_comp_config_get_su() will not read anything,
     it will return success.

     Trying to read config info of openSAF SUs may result in a deadlock
     condition when IMMND calls AMF register sync call(after AMF invokes
     instantiate script in NoRed SU instantiation sequence) and amfnd reads
     immsv database using search_initializie sync api(AMF reads database for
     middleware 2N SUs during 2N Red SU instantiation). This is purely dependent
     on timing when immnd is registering with AMF and AMF is reading config from
     immnd during controller coming up. Fix for the above problem is that
     Middleware Components wont be dynamically added in the case of openSAF SUs,
     so don't refresh config info if it is openSAF SU. */

  if ((false == su->is_ncs) && (evt->info.ir_evt.status == false)) {
    if (cb->scs_absence_max_duration == 0) {
      m_AVND_SU_REG_FAILED_SET(su);
      /* Will transition to instantiation-failed when instantiated */
      LOG_ER("'%s':FAILED", __FUNCTION__);
      rc = NCSCC_RC_FAILURE;
      goto done;
    } else {
      // @TODO(garylee) this is a temporary workaround: IMM is not accepting OM
      // connections and a component needs to be restarted.
      LOG_CR(
          "'%s': failed to refresh components in SU. Attempt to reuse old config",
          __FUNCTION__);
    }
  }
  /* trigger su instantiation for pi su */
  if (m_AVND_SU_IS_PREINSTANTIABLE(su)) {
    /* Update the comp oper state. */
    for (AVND_COMP *comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
             m_NCS_DBLIST_FIND_FIRST(&su->comp_list));
         comp; comp = m_AVND_COMP_FROM_SU_DLL_NODE_GET(
                   m_NCS_DBLIST_FIND_NEXT(&comp->su_dll_node))) {
      rc = avnd_comp_oper_state_avd_sync(avnd_cb, comp);
    }
    TRACE("SU instantiation for PI SUs, running the SU presence state FSM:'%s'",
          su->name.c_str());
    rc = avnd_su_pres_fsm_run(cb, su, 0, AVND_SU_PRES_FSM_EV_INST);
  } else {
    if (m_AVND_SU_IS_REG_FAILED(su)) {
      /* The SU configuration is bad, we cannot do much other transition to
       * failed state */
      TRACE_2("SU Configuration is bad");
      avnd_su_pres_state_set(cb, su, SA_AMF_PRESENCE_INSTANTIATION_FAILED);
      m_AVND_SU_ALL_TERM_RESET(su);
    } else
      osafassert(0);
  }

done:
  TRACE_LEAVE();
  return rc;
}

static uint32_t avnd_su_pres_termfailed_comptermfail_or_compuninst(
    AVND_CB *cb, AVND_SU *su, AVND_COMP *comp) {
  uint32_t rc = NCSCC_RC_SUCCESS;
  const std::string compname = comp ? comp->name : "none";
  TRACE_ENTER2(
      "CompTermFailed/CompUnInstantiated event in the TermFailed state:'%s', '%s'",
      su->name.c_str(), compname.c_str());

  // PI SU case.
  if (m_AVND_SU_IS_PREINSTANTIABLE(su)) {
    TRACE_1("PI SU");
    if ((all_comps_terminated_in_su(su, true) == true) &&
        (su->si_list.n_nodes != 0) && (m_AVND_SU_IS_ASSIGN_PEND(su)) &&
        (su->avnd_su_check_sis_previous_assign_state(
             AVND_SU_SI_ASSIGN_STATE_UNASSIGNED) == true)) {
      TRACE_2("Informing AMFD of su-failover");
      rc = avnd_di_oper_send(cb, su, AVSV_ERR_RCVR_SU_FAILOVER);
      avnd_su_si_del(cb, su->name);
    }
  }

  // NPI SU case.
  if (!m_AVND_SU_IS_PREINSTANTIABLE(su)) {
    TRACE_1("NPI SU");
    AVND_SU_SI_REC *si =
        (AVND_SU_SI_REC *)m_NCS_DBLIST_FIND_FIRST(&su->si_list);

    /*
       During shutdown phase, all comps of NPI SU are terminated as a part of
       removal of assignments. If a SU enters in TERM_FAILED state then in order
       to complete shutdown sequence generate a si-oper done indication.
     */
    if ((si != nullptr) && (m_AVND_IS_SHUTTING_DOWN(cb)) &&
        (m_AVND_SU_SI_CURR_ASSIGN_STATE_IS_REMOVING(si)) &&
        (all_comps_terminated_in_su(su, true) == true)) {
      rc = avnd_su_si_oper_done(cb, su, si);
    }
  }

  TRACE_LEAVE();
  return rc;
}
