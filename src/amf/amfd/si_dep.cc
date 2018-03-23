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
 *            Ericsson
 *
 */

/*****************************************************************************
..............................................................................

..............................................................................

  DESCRIPTION:

  This module deals with the creation, accessing and deletion of the SI
  dependency database.

  THE MEANING OF SPONSOR AND DEPENDENT:
  In various sections below terms like sponsor(s) and dependent(s) is used
  frequently. These two terms are used for SIs. A dependent SI or simply a
  dependent means a SI which will be assigned active HA state only when the SI
  on which it depends is either fully assigned or partially assigned. The SI
  on which it depends for active HA state is called sponsor SI or simply
sponsor. A dependent may depend on more than one sponsors. Similarly a SI can be
a sponsor for more than one dependents. For more details on SI dependency
section 3.8.1.1 of specification SA-AIS-AMF-B.04.01 can be referred.

  RULES FOR si_dep_state IN ANY SI:
  In any SI si_dep_state represents state of SI from SI dependency
  perpespective. In any SI, si_dep_state can take values from enum
  AVD_SI_DEP_STATE based on following rules:

  1)A SI which is not participating in SI dependency can take
    AVD_SI_NO_DEPENDENCY for si_dep_state.
    Such a SI will have si->spons_list as nullptr and si->num_dependents
    as zero.
  2)A SI with only dependents and no sponsors can take following values
    for si_dep_state:

    AVD_SI_NO_DEPENDENCY,
    AVD_SI_ASSIGNED.
    Such a SI will have si->spons_list as nullptr.

  3)A SI having sponsors as well as dependents can take all values for
    si_dep_states except AVD_SI_NO_DEPENDENCY.


  DESCRIPTION OF ALL SI DEPENDENCY STATES:
  The meanings all values in enum AVD_SI_DEP_STATE :
  1)AVD_SI_NO_DEPENDENCY:
        A SI is moved to this state:
        1)If it has no sponsor and no dependents irrespective of it is assigned
or unassigned. 2)If SI has dependent and no sponsor then its si_dep_state will
be AVD_SI_NO_DEPENDENCY when unassigned and AVD_SI_ASSIGNED when assigned.
  2)AVD_SI_SPONSOR_UNASSIGNED:
         Only a dependent SI can have this state. A SI has to satisfy
                all the following conditions to be in this state:
         1)If it has atleast one sponsor.
         2)Atleast one sponsor is unassigned.
         3)Tolerance timer has expired(if configured non zero).
         4)and this dependent SI is unassigned.
  3)AVD_SI_ASSIGNED:
         A SI can have this dep state in any of the following conditions:
                1)If it is sponsor and assigned.
                2)If it is a dependent SI, assigned and all its sponsors are
assigned. 3)If it is a sponsor as well as a dependent, assigned and all sponsors
assigned. 4)AVD_SI_TOL_TIMER_RUNNING: Only a dependent SI can have this state. A
SI has to satisfy all the following conditions to be in this state: 1)If it has
atleast one sponsor. 2)Atleast one sponsor is unassigned. 3)It has
saAmfToleranceTime value nonzero for the SI-dependency in which sponsor is
unassigned. 4)Tolerance timer is running for the dependency mentioned in 3) in
this section.

  5)AVD_SI_READY_TO_UNASSIGN:
        Only applicable to a dependent SI. A dependent SI can have this state
when its sponsor is unassigned and this dependent SI is in assigned state.

  6)AVD_SI_UNASSIGNING_DUE_TO_DEP:
        Only applicable to dependent SI. A dependent si is set to this state
         before removing assignment from it.

  7)AVD_SI_FAILOVER_UNDER_PROGRESS:
        Only applicable to a SI which has atleast one sponsor. A dependent SI is
        set to this state when its sponsor is undergoing failover. It means,
        failover for this dependent SI is delayed untill the completion of
        sponsor's failover.

  8)AVD_SI_READY_TO_ASSIGN:
        Only applicable to a dependent SI. A dependent SI can have this state
when all its sponsors are assigned and this dependent SI itself is unassigned.



  SI-DEPENDENCY SI STATE TRASITION DIAGRAM :
                                -------------------      (4)
   ****************************>|                 |>***********************
   *       ********************<| AVD_SI_ASSIGNED |<*******************   *
Triggering actions: *       *                    -------------------     (2),(6)
*   *       (1)DEP_CONFIG *       *
*   *       (2)SPONSOR_ASSIGNED *       *            (6)
---------------------------    *   *       (3)SI_ASSIGNED *       *
**************<|                         |    *   *       (4)SPONSOR_UNASSGINED
   *       *            *  ***********>|AVD_SI_SPONSOR_UNASSIGNED|    *   *
(5)SI_UNASSGINED *       *            *  *(4)        ---------------------------
*   *       (6)DEP_UNCOFIGURED *       *(5)         *  *              ^  v    ^
*   *       (7)TOL_TIMER_EXPIRY *       *            v  ^              *  *
*(5)                ^   *       (8)TOL_TIMER > 0 *       *
----------------------    *  *   -----------------------  *       (9)TOL_TIMER
== 0 *       *    |AVD_SI_NO_DEPENDENCY|    *  *   |AVD_SI_UNASSIGNING   |  *
(4) *       *    |                    | (4)*  *   |DUE_TO_DEP           |  * *
*    ----------------------    *  *   -----------------------  * *       *
v                  *  *    *       ^               * *       *           *
*  *    *       * (9)           * *       *           *                  *  *
*       ^               v
   *(3)    *           *(1)               *  *    * --------------------------
   *       *           *                  *  *    * |AVD_SI_READY_TO_UNASSIGN|
   *       v           v                  *  *    * --------------------------
   *    ------------------------          *  *    *              V
   ****<|AVD_SI_READY_TO_ASSIGN|>**********  *    *              *
        |                      |<*************    *              *(8)
        ------------------------    (2)           *              *
                                                  * (7)          *
                                                  ^              v
                                              --------------------------
                                              |AVD_SI_TOL_TIMER_RUNNING|
                                              --------------------------

..............................................................................

  FUNCTIONS INCLUDED in this module:

******************************************************************************
*/

/*
 * Module Inclusion Control...
 */

#include "amf/amfd/util.h"
#include <saImmOm.h>
#include "osaf/immutil/immutil.h"
#include "base/logtrace.h"

#include "amf/amfd/imm.h"
#include "amf/amfd/si_dep.h"
#include "amf/amfd/susi.h"
#include "amf/amfd/proc.h"

AmfDb<std::pair<std::string, std::string>, AVD_SI_DEP> *sidep_db = nullptr;
extern void avd_sg_npm_stdbysu_role_change(AVD_SU *su);
extern uint32_t avd_sg_get_curr_act_cnt(AVD_SG *sg);

/* static function prototypes */
static bool avd_sidep_all_sponsors_active(AVD_SI *si);
static void sidep_update_si_dep_state_for_spons_unassign(
    AVD_CL_CB *cb, AVD_SI *dep_si, AVD_SI_DEP *si_dep_rec);
static uint32_t sidep_unassign_dependent(AVD_CL_CB *cb, AVD_SI *si);
static bool sidep_is_si_active(AVD_SI *si);
static uint32_t sidep_sg_red_si_process_assignment(AVD_CL_CB *cb, AVD_SI *si);
static uint32_t sidep_si_dep_state_evt_send(AVD_CL_CB *cb, AVD_SI *si,
                                            AVD_EVT_TYPE evt_type);
static uint32_t sidep_cyclic_dep_find(AVD_SI_DEP *sidep);
static void sidep_si_dep_start_unassign(AVD_CL_CB *cb, AVD_EVT *evt);
static void sidep_dependentsi_role_failover(AVD_SI *si);

static const char *depstatename[] = {"",
                                     "NO_DEPENDENCY",
                                     "SPONSOR_UNASSIGNED",
                                     "ASSIGNED",
                                     "TOL_TIMER_RUNNING",
                                     "READY_TO_UNASSIGN",
                                     "UNASSIGNING_DUE_TO_DEP",
                                     "FAILOVER_UNDER_PROGRESS",
                                     "READY_TO_ASSIGN"};

/**
 * @brief       This function updates the si_dep_state in SI.
 *              Also if the amfd is standby then it will
 *              take action on dependents.
 *
 * @param[in]   si - pointer to AVD_SI struct.
 * @param[in]   state - any state of AVD_SI_DEP_STATE type.
 *
 * @return      Nothing
 *
 **/
void avd_sidep_si_dep_state_set(AVD_SI *si, AVD_SI_DEP_STATE state) {
  AVD_SI_DEP_STATE old_state = si->si_dep_state;
  if (old_state == state) return;
  si->si_dep_state = state;
  TRACE("'%s' si_dep_state %s => %s", si->name.c_str(), depstatename[old_state],
        depstatename[state]);
  m_AVSV_SEND_CKPT_UPDT_ASYNC_UPDT(avd_cb, si, AVSV_CKPT_SI_DEP_STATE);

  if (avd_cb->avail_state_avd != SA_AMF_HA_ACTIVE) {
    TRACE("si->num_dependents:%u", si->num_dependents);
    /* If the Sponsor SI is Unasigned then iterate through all its dependents
       and start Tolerance timer for each dependent */
    if ((si->num_dependents) &&
        ((si->si_dep_state == AVD_SI_SPONSOR_UNASSIGNED) ||
         (si->si_dep_state == AVD_SI_NO_DEPENDENCY) ||
         (si->si_dep_state == AVD_SI_READY_TO_ASSIGN))) {
      /*Check if this SI is a sponsor SI for some other SI then
        take appropriate action on dependents.*/
      sidep_take_action_on_dependents(si);
    }
    /*If a dependent si moves to AVD_SI_READY_TO_UNASSIGN state
      then start the tolerance timer.*/
    if (si->si_dep_state == AVD_SI_READY_TO_UNASSIGN)
      sidep_process_ready_to_unassign_depstate(si);

    /*
       This SI is no longer in TOL_TIMER state, it means active controller
       has acted on this SI after tolerance timer expiry. This SI was added
       in the list avd_cb->sis_in_Tolerance_Timer_state, maintained at standby,
       when tolerance timer had expired for it on standby controller. Now
       remove it from the list.
    */
    if (old_state == AVD_SI_TOL_TIMER_RUNNING) {
      std::list<AVD_SI *>::iterator it1;
      for (it1 = avd_cb->sis_in_Tolerance_Timer_state.begin();
           it1 != avd_cb->sis_in_Tolerance_Timer_state.end(); ++it1) {
        if (*it1 == si) {
          avd_cb->sis_in_Tolerance_Timer_state.erase(it1);
          TRACE_1(
              "Deleted '%s' from tol timer list,"
              " size after deletion:%zu",
              si->name.c_str(), avd_cb->sis_in_Tolerance_Timer_state.size());
          break;
        }
      }
    }
  }
}

/*****************************************************************************
 * Function: sidep_is_si_active
 *
 * Purpose:  This function checks if si is active from si-si dependency
 *           perspective.Such an si will have atleast one sisu in active or
 *           quiescing state and its si_dep_state will not be
 *           AVD_SI_FAILOVER_UNDER_PROGRESS.
 *
 * Input:   si - Pointer to AVD_SI struct
 *
 * Returns: true/false.
 *
 * NOTES:
 *
 **************************************************************************/
bool sidep_is_si_active(AVD_SI *si) {
  AVD_SU_SI_REL *susi = nullptr;
  bool si_active = false;

  TRACE_ENTER2("'%s'", si->name.c_str());

  for (susi = si->list_of_sisu; susi != AVD_SU_SI_REL_NULL;
       susi = susi->si_next) {
    if (((susi->state == SA_AMF_HA_ACTIVE) ||
         (susi->state == SA_AMF_HA_QUIESCING)) &&
        (susi->fsm == AVD_SU_SI_STATE_ASGND) &&
        (susi->si->si_dep_state != AVD_SI_FAILOVER_UNDER_PROGRESS)) {
      si_active = true;
      break;
    }
  }

  TRACE_LEAVE2("%u", si_active);
  return si_active;
}

/*****************************************************************************
 * Function: sidep_spons_list_del
 *
 * Purpose:  This function deletes the spons-SI node from the spons-list of
 *           dependent-SI.
 *
 * Input:  cb - ptr to AVD control block
 *         si_dep_rec - ptr to AVD_SI_DEP struct
 *
 * Returns:
 *
 * NOTES:
 *
 **************************************************************************/
void sidep_spons_list_del(AVD_SI_DEP *si_dep_rec) {
  AVD_SPONS_SI_NODE *del_spons_si_node = nullptr;

  TRACE_ENTER();

  /* Dependent SI should be active, if not return error */
  AVD_SI *dep_si = avd_si_get(si_dep_rec->dep_name);
  osafassert(dep_si != nullptr);

  /* SI doesn't depend on any other SIs */
  osafassert(dep_si->spons_si_list != nullptr);

  AVD_SPONS_SI_NODE *spons_si_node = dep_si->spons_si_list;

  /* Check if the sponsor is the first node of the list */
  if (dep_si->spons_si_list->si->name.compare(si_dep_rec->spons_si->name) ==
      0) {
    dep_si->spons_si_list = spons_si_node->next;
    /* decrement the dependent SI count in sponsor SI */
    spons_si_node->si->num_dependents--;
    delete spons_si_node;
  } else {
    while (spons_si_node->next != nullptr) {
      if (spons_si_node->next->si->name.compare(si_dep_rec->spons_si->name) !=
          0) {
        spons_si_node = spons_si_node->next;
        continue;
      }

      /* Matched spons si node found, then delete */
      del_spons_si_node = spons_si_node->next;
      spons_si_node->next = spons_si_node->next->next;
      del_spons_si_node->si->num_dependents--;
      delete del_spons_si_node;
      break;
    }
  }

  TRACE_LEAVE();
}

static bool rec_already_in_sponsor_list(const AVD_SI *si,
                                        const AVD_SI_DEP *rec) {
  AVD_SPONS_SI_NODE *node;

  for (node = si->spons_si_list; node; node = node->next)
    if (node->sidep_rec == rec) return true;

  return false;
}

/*****************************************************************************
 * Function: avd_si_dep_spons_list_add
 *
 * Purpose:  This function adds the spons-SI node in the spons-list of
 *           dependent-SI if not already exist.
 *
 * Input:  dep_si - ptr to AVD_SI struct (dependent-SI node).
 *         spons_si - ptr to AVD_SI struct (sponsor-SI node).
 *         rec
 *
 **************************************************************************/
void avd_si_dep_spons_list_add(AVD_SI *dep_si, AVD_SI *spons_si,
                               AVD_SI_DEP *rec) {
  AVD_SPONS_SI_NODE *spons_si_node;

  if (rec_already_in_sponsor_list(dep_si, rec)) return;

  spons_si_node = new AVD_SPONS_SI_NODE();

  /* increment number of dependents in sponsor SI as well */
  spons_si->num_dependents++;

  spons_si_node->si = spons_si;
  spons_si_node->sidep_rec = rec;

  spons_si_node->next = dep_si->spons_si_list;
  dep_si->spons_si_list = spons_si_node;
}

static AVD_SI_DEP *sidep_db_find(const std::string &sponsor,
                                 const std::string &dependent) {
  return sidep_db->find(make_pair(sponsor, dependent));
}

/*****************************************************************************
 * Function: sidep_stop_tol_timer
 *
 * Purpose:  This function is been called when SI was unassigned, checks
 *           whether tolerance timers are running for this SI (because
 *           of its sponsor-SI unassignment), if yes just stop them.
 *
 * Input:  cb - ptr to AVD control block
 *         si - ptr to AVD_SI struct.
 *
 * Returns:
 *
 * NOTES:
 *
 **************************************************************************/
void sidep_stop_tol_timer(AVD_CL_CB *cb, AVD_SI *dep_si) {
  AVD_SI_DEP *rec;
  AVD_SPONS_SI_NODE *spons_si_node = dep_si->spons_si_list;

  while (spons_si_node) {
    if ((rec = sidep_db_find(spons_si_node->si->name, dep_si->name)) !=
        nullptr) {
      if (rec->si_dep_timer.is_active == true) {
        avd_stop_tmr(cb, &rec->si_dep_timer);
        TRACE("Tolerance timer stopped for '%s'", dep_si->name.c_str());

        if (dep_si->tol_timer_count > 0) {
          dep_si->tol_timer_count--;
        }
      }
    }

    spons_si_node = spons_si_node->next;
  }
}

/*****************************************************************************
 * Function: sidep_unassign_dependent
 *
 * Purpose:  This function removes the active and the quiescing HA states for
 *           SI from all service units and if si is a dependent si then moves
 *           SI dependency state to SPONSOR_UNASSIGNED state.For a si with no
 *           sponsor si_dp_state is set to AND_SI_NO_DEPENDENCY.
 *
 * Input: cb - The AVD control block
 *        si - Pointer to AVD_SI struct
 *
 * Returns: NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE.
 *
 * NOTES:
 *
 **************************************************************************/
uint32_t sidep_unassign_dependent(AVD_CL_CB *cb, AVD_SI *si) {
  uint32_t rc = NCSCC_RC_FAILURE;

  TRACE_ENTER2("'%s'", si->name.c_str());

  AVD_SU_SI_REL *susi = si->list_of_sisu;
  while (susi != AVD_SU_SI_REL_NULL && susi->fsm != AVD_SU_SI_STATE_UNASGN) {
    if ((rc = avd_susi_del_send(susi)) != NCSCC_RC_SUCCESS) goto done;

    /* add the su to su-oper list */
    avd_sg_su_oper_list_add(cb, susi->su, false);

    susi = susi->si_next;
  }
  if (si->spons_si_list == nullptr)
    avd_sidep_si_dep_state_set(si, AVD_SI_NO_DEPENDENCY);
  else
    avd_sidep_si_dep_state_set(si, AVD_SI_SPONSOR_UNASSIGNED);

  /* transition to sg-realign state */
  si->sg_of_si->set_fsm_state(AVD_SG_FSM_SG_REALIGN);

done:
  TRACE_LEAVE2("rc:%u", rc);
  return rc;
}

/*****************************************************************************
 * Function: sidep_sg_red_si_process_assignment
 *
 * Purpose:  This function process SI assignment process on a redundancy model
 *           that was associated with the SI.
 *
 * Input: cb - The AVD control block
 *        si - Pointer to AVD_SI struct
 *
 * Returns: NCSCC_RC_SUCCESS/NCSCC_RC_INV_VAL
 *
 * NOTES:
 *
 **************************************************************************/
uint32_t sidep_sg_red_si_process_assignment(AVD_CL_CB *cb, AVD_SI *si) {
  uint32_t rc = NCSCC_RC_FAILURE;

  TRACE_ENTER2("'%s'", si->name.c_str());

  if (si->sg_of_si->sg_fsm_state != AVD_SG_FSM_STABLE) {
    TRACE("sg unstable, so defering sidep action on si:'%s'", si->name.c_str());
    goto done;
  }

  if ((si->saAmfSIAdminState == SA_AMF_ADMIN_UNLOCKED) &&
      (cb->init_state == AVD_APP_STATE)) {
    LOG_NO("Assigning due to dep '%s'", si->name.c_str());
    if (si->sg_of_si->si_assign(cb, si) != NCSCC_RC_SUCCESS) {
      goto done;
    }

    if (sidep_is_si_active(si)) {
      avd_sidep_si_dep_state_set(si, AVD_SI_ASSIGNED);
      rc = NCSCC_RC_SUCCESS;
    }
  }

done:
  TRACE_LEAVE2("rc:%u", rc);
  return rc;
}

/*****************************************************************************************
 * Function: sidep_si_dep_state_evt_send
 *
 * Purpose:  This function prepares the event to send either
 *AVD_EVT_UNASSIGN_SI_DEP_STATE event or AVD_EVT_ASSIGN_SI_DEP_STATE event.
 *AVD_EVT_UNASSIGN_SI_DEP_STATE event is sent on expiry of tolerance
 *timer or the sponsor SI was unassigned and tolerance timer is "0".
 *AVD_EVT_ASSIGN_SI_DEP_STATE event is sent to assign a dependent si.
 *
 * Input:  cb - ptr to AVD control block
 *         si - ptr to AVD_SI struct.
 *
 * Returns:
 *
 * NOTES:
 *
 *****************************************************************************************/
uint32_t sidep_si_dep_state_evt_send(AVD_CL_CB *cb, AVD_SI *si,
                                     AVD_EVT_TYPE evt_type) {
  uint32_t rc = NCSCC_RC_FAILURE;

  TRACE_ENTER2("si:'%s' evt_type:%u", si->name.c_str(), evt_type);

  AVD_EVT *evt = new AVD_EVT();

  /*Update evt struct, using tmr field even though this field is not
   * relevant for this event, but it accommodates the required data.
   */
  evt->rcv_evt = evt_type;
  evt->info.tmr.dep_si_name = si->name;

  /*For unassignment mark the si_dep_state t- AVD_SI_READY_TO_UNASSIGN*/
  if (evt_type == AVD_EVT_UNASSIGN_SI_DEP_STATE)
    avd_sidep_si_dep_state_set(si, AVD_SI_READY_TO_UNASSIGN);

  if (m_NCS_IPC_SEND(&cb->avd_mbx, evt, NCS_IPC_PRIORITY_HIGH) !=
      NCSCC_RC_SUCCESS) {
    LOG_ER("%s: ipc send %u failed", __FUNCTION__, evt->rcv_evt);
    delete evt;
    goto done;
  }

  rc = NCSCC_RC_SUCCESS;
done:
  TRACE_LEAVE2("rc:%u", rc);
  return rc;
}

/**
 * @brief       This function handles tolerance timer expiry event for a
 *dependent SI at standby AMFD. If SI is still in TOL_TIMER_RUNNING state then
 *              it is added in the list avd_cb->sis_in_Tolerance_Timer_state.
 * @param[in]   spons_si: ptr to sponosor SI.
 * @param[in]   dep_si: ptr to dependent SI.
 *
 **/
void avd_sidep_stdby_amfd_tol_timer_expiry(AVD_SI *spons_si, AVD_SI *dep_si) {
  TRACE_ENTER();

  /* If SI is still in TOL_TIMER state, add it in list
     avd_cb->sis_in_Tolerance_Timer_state, When active AMFD will checkpoint
     new si_dep state for this SI, this SI will be removed from the list.
     Before checkpoiting new state, if failover/swotichover of controller
     occurs then action will be taken on the SIs of this list by
     the new active controller.
   */
  if (dep_si->si_dep_state == AVD_SI_TOL_TIMER_RUNNING) {
    avd_cb->sis_in_Tolerance_Timer_state.push_back(dep_si);
    TRACE_1("Added '%s' to tol_timer list, size after addition:%zu",
            dep_si->name.c_str(), avd_cb->sis_in_Tolerance_Timer_state.size());
  }
  TRACE_LEAVE();
}

/*
 * @brief:   On expiry of tolerance timer in SI-SI dependency context, this
 *           function acts on the dependent SI based on the role of AMFD.
 *           sponsor moved to unassigned state.
 * @param[in]  cb - ptr to AVD control block
 * @param[in]  evt - ptr to AVD_EVT struct.
 *
 */
void avd_sidep_tol_tmr_evh(AVD_CL_CB *cb, AVD_EVT *evt) {
  TRACE_ENTER();

  osafassert(evt->info.tmr.type == AVD_TMR_SI_DEP_TOL);

  AVD_SI *si = avd_si_get(evt->info.tmr.dep_si_name);
  AVD_SI *spons_si = avd_si_get(evt->info.tmr.spons_si_name);

  if ((si == nullptr) || (spons_si == nullptr)) {
    /* Nothing to do here as SI/spons-SI itself lost their existence */
    TRACE("Sponsor SI or Dependent SI not exists");
    goto done;
  }

  LOG_NO("expiry of tolerance timer '%s'", si->name.c_str());

  /* Since the tol-timer is been expired, can decrement tol_timer_count of
   * the SI.
   */
  if (si->tol_timer_count > 0) si->tol_timer_count--;

  if (cb->avail_state_avd == SA_AMF_HA_ACTIVE)
    avd_sidep_activ_amfd_tol_timer_expiry(spons_si, si);
  else
    avd_sidep_stdby_amfd_tol_timer_expiry(spons_si, si);

done:
  TRACE_LEAVE();
}

/**
 * @brief       This function handles tolerance timer expiry event for a
 *dependent SI at active AMFD. If atleast one sponsor of SI is still unassigned
 *              then AMFD starts unassignment of the SI.
 * @param[in]   spons_si: ptr to sponosor SI.
 * @param[in]   dep_si: ptr to dependent SI.
 *
 **/
void avd_sidep_activ_amfd_tol_timer_expiry(AVD_SI *spons_si, AVD_SI *dep_si) {
  AVD_SI *si = dep_si;
  AVD_CL_CB *cb = avd_cb;
  TRACE_ENTER();
  avd_sidep_si_dep_state_set(si, AVD_SI_UNASSIGNING_DUE_TO_DEP);
  /* Do not take action on dependent si if its sg fsm is unstable.
     When sg becomes stable, based on updated
     si_dep_state action is taken.
   */
  if (si->sg_of_si->sg_fsm_state != AVD_SG_FSM_STABLE) {
    TRACE("sg unstable, so defering sidep action on si:'%s'", si->name.c_str());
    goto done;
  }

  /* Before starting unassignment process of SI, check once again whether
   * sponsor SIs are assigned back,if so move the SI state to ASSIGNED state
   */
  if (avd_sidep_all_sponsors_active(si)) {
    /*Since all the sponsors got assigned we can assign this dependent.
      If dependent si is already assigned then mark dep state to AVD_SI_ASSGINED
      and stop other tolerance timers.
     */
    if (sidep_is_si_active(si))
      avd_sidep_si_dep_state_set(si, AVD_SI_ASSIGNED);
    else
      sidep_sg_red_si_process_assignment(cb, si);

  } else {
    /*Atleast one sponsor is unassigned. If dependent is already unassigned then
      mark its dep state to SPONSOR_UNASSGINED otherwise unassign it.
     */
    if (sidep_is_si_active(si)) {
      LOG_NO("Unassigning due to dep'%s'", si->name.c_str());
      sidep_unassign_dependent(cb, si);
    } else
      avd_sidep_si_dep_state_set(si, AVD_SI_SPONSOR_UNASSIGNED);
  }

done:
  TRACE_LEAVE();
}
/*****************************************************************************
 * Function: avd_sidep_all_sponsors_active
 *
 * Purpose:  This function checks whether sponsor SIs of an SI are in enabled /
 *           disabled state.
 *
 * Input:    si - Pointer to AVD_SI struct
 *
 * Returns: true - if sponsor-SI is in enabled state
 *          false- if sponsor-SI is in disabled state
 *
 * NOTES:
 *
 **************************************************************************/
bool avd_sidep_all_sponsors_active(AVD_SI *si) {
  bool spons_state = true;
  AVD_SPONS_SI_NODE *spons_si_node = nullptr;

  TRACE_ENTER2("'%s'", si->name.c_str());

  for (spons_si_node = si->spons_si_list; spons_si_node != nullptr;
       spons_si_node = spons_si_node->next) {
    if (!sidep_is_si_active(spons_si_node->si)) {
      spons_state = false;
      break;
    }
  }

  TRACE_LEAVE2("spons_state:%u", spons_state);
  return spons_state;
}
/*****************************************************************************
 * Function: avd_sidep_update_si_dep_state_for_all_sis
 *
 * Purpose:  This function screens SI-SI dependencies of the all sis in SG.
 *           It will update si_dep_state of si based on its sponsors assignment
 *state.
 *
 * Input: sg - Pointer to AVD_SG struct
 *
 * Returns:
 *
 * NOTES:
 *
 **************************************************************************/
void avd_sidep_update_si_dep_state_for_all_sis(AVD_SG *sg) {
  TRACE_ENTER2("'%s'", sg->name.c_str());

  for (const auto &si : sg->list_of_si) {
    /*Avoid screening if si is neither a sponsor si nor a dependent si*/
    if ((si->spons_si_list != nullptr) || (si->num_dependents > 0))
      sidep_si_screen_si_dependencies(si);
  }
  TRACE_LEAVE();
}

/*****************************************************************************
 * Function: sidep_si_screen_si_dependencies
 *
 * Purpose:  This function checks whether the sponsor SIs are in ASSIGNED
 *           state. If they are in assigned state, SI changes its
 *           own si_dep state and its dependents si_dep_state accordingly.
 *           Action will be taken on the is or dependents based on the value of
 *           start_assignment.
 *
 * Input: cb - The AVD control block
 *        si - Pointer to AVD_SI struct
 *
 * Returns:
 *
 * NOTES:
 *
 **************************************************************************/
void sidep_si_screen_si_dependencies(AVD_SI *si) {
  TRACE_ENTER2("%s", si->name.c_str());

  /*update the si_dep_state of si based on assignement states of its sponsors*/
  sidep_update_si_self_dep_state(si);

  /*update si_dep_state of dependent sis of si.*/
  if (si->num_dependents) sidep_update_dependents_states(si);

  TRACE_LEAVE();
}

/**
 * @brief       This function assigns dependent si as its all
 *              sponsor sis are assigned (SI dependency logics).
 *
 * @param[in]   cb - pointer to avd_control block.
 * @param[in]   evt - pointer to AVD_EVT struct.
 *
 * @return      Nothing
 *
 **/
void avd_sidep_assign_evh(AVD_CL_CB *cb, AVD_EVT *evt) {
  AVD_SI *dep_si = nullptr;
  bool role_failover_pending = false;

  TRACE_ENTER();

  if (evt == nullptr) {
    LOG_NO("Got nullptr event");
    goto done;
  }

  osafassert(evt->rcv_evt == AVD_EVT_ASSIGN_SI_DEP_STATE);

  dep_si = avd_si_get(evt->info.tmr.dep_si_name);
  if (dep_si == nullptr) goto done;

  /* Take action only when dependent sg fsm is stable.  When sg becomes
     stable, based on updated si_dep_state action is taken.
   */
  if (dep_si->sg_of_si->sg_fsm_state != AVD_SG_FSM_STABLE) {
    TRACE("sg unstable, so defering sidep action on si:'%s'",
          dep_si->name.c_str());
    goto done;
  }

  /* Check if we are yet to do SI role failover for the dependent SI
   */
  if (dep_si->si_dep_state == AVD_SI_FAILOVER_UNDER_PROGRESS)
    role_failover_pending = true;

  if ((dep_si->list_of_sisu) &&
      (dep_si->list_of_sisu->state == SA_AMF_HA_STANDBY) &&
      (dep_si->list_of_sisu->fsm != AVD_SU_SI_STATE_UNASGN) &&
      (dep_si->list_of_sisu->si_next == nullptr)) {
    role_failover_pending = true;
  }

  if (role_failover_pending == true) {
    sidep_dependentsi_role_failover(dep_si);
  } else {
    /*Check sponsors state once agian then take action*/
    sidep_update_si_self_dep_state(dep_si);
    if (dep_si->si_dep_state == AVD_SI_READY_TO_ASSIGN ||
        (dep_si->si_dep_state == AVD_SI_ASSIGNED &&
            dep_si->saAmfSINumCurrActiveAssignments <
            dep_si->pref_active_assignments())) {
      if ((sidep_sg_red_si_process_assignment(avd_cb, dep_si) ==
           NCSCC_RC_FAILURE) &&
          (dep_si->num_dependents != 0)) {
        sidep_take_action_on_dependents(dep_si);
      }
    } else
      sidep_si_take_action(dep_si);
  }

done:
  TRACE_LEAVE();
}

/*****************************************************************************
 * Function: sidep_si_dep_start_unassign
 *
 * Purpose:  This function starts SI unassignment process due to sponsor was
 *           unassigned (SI-SI dependency logics).
 *
 * Input: cb - The AVD control block
 *        evt - Pointer to AVD_EVT struct
 *
 * Returns:
 *
 * NOTES:
 *
 **************************************************************************/
void sidep_si_dep_start_unassign(AVD_CL_CB *cb, AVD_EVT *evt) {
  TRACE_ENTER();
  AVD_SI *si = avd_si_get(evt->info.tmr.dep_si_name);

  if (!si) {
    LOG_ER("Received si nullptr");
    goto done;
  }

  if (si->si_dep_state != AVD_SI_UNASSIGNING_DUE_TO_DEP) {
    LOG_ER("wrong si_dep_state:%u of si:'%s'", si->si_dep_state,
           si->name.c_str());
    goto done;
  }

  /* Before starting unassignment process of SI, check again whether all its
   * sponsors are moved back to assigned state, if so it is not required to
   * initiate the unassignment process for the (dependent) SI.
   */
  if (avd_sidep_all_sponsors_active(si)) {
    avd_sidep_si_dep_state_set(si, AVD_SI_ASSIGNED);
    goto done;
  }

  /* If the SI is already been unassigned, nothing to proceed for
   * unassignment
   */
  if (si->list_of_sisu == AVD_SU_SI_REL_NULL) {
    avd_sidep_si_dep_state_set(si, AVD_SI_SPONSOR_UNASSIGNED);
    goto done;
  }

  if (sidep_unassign_dependent(cb, si) != NCSCC_RC_SUCCESS) {
    LOG_ER("'%s' unassignment failed", si->name.c_str());
  }

done:
  TRACE_LEAVE();
}

/*****************************************************************************
 * Function: sidep_update_si_dep_state_for_spons_unassign
 *
 * Purpose:  Upon sponsor SI is unassigned, this function updates the SI
 *           dependent states either to AVD_SI_READY_TO_UNASSIGN /
 *           AVD_SI_TOL_TIMER_RUNNING states.
 *
 * Input:  cb - ptr to AVD control block
 *         dep_si - ptr to AVD_SI struct (dependent SI).
 *         si_dep_rec - ptr to AVD_SI_DEP struct
 *
 * Returns:
 *
 * NOTES:
 *
 **************************************************************************/
void sidep_update_si_dep_state_for_spons_unassign(AVD_CL_CB *cb, AVD_SI *dep_si,
                                                  AVD_SI_DEP *si_dep_rec) {
  TRACE_ENTER2("si:'%s', si_dep_state:'%s'", dep_si->name.c_str(),
               depstatename[dep_si->si_dep_state]);

  AVD_SI *spons_si = avd_si_get(si_dep_rec->spons_name);
  osafassert(spons_si != nullptr);

  /* Take action only when both sponsor and dependent belongs to same sg
     or if dependent sg fsm is stable.  When sg becomes stable,
     based on updated si_dep_state action is taken.
   */
  if ((dep_si->sg_of_si->sg_fsm_state != AVD_SG_FSM_STABLE) &&
      (dep_si->sg_of_si != spons_si->sg_of_si)) {
    TRACE("sg unstable, so defering sidep action on si:'%s'",
          dep_si->name.c_str());
    goto done;
  }

  if (si_dep_rec->saAmfToleranceTime > 0) {
    if (si_dep_rec->si_dep_timer.is_active != true) {
      /* Start the tolerance timer since it not running */
      m_AVD_SI_DEP_TOL_TMR_START(cb, si_dep_rec);
      /* Increment tol_timer_count. tol_timer_count>1 means more
         than 1 sponsors are in unassigned state for this SI.
       */
      dep_si->tol_timer_count++;
      LOG_NO("Tolerance timer started, sponsor si:'%s', dependent si:%s",
             spons_si->name.c_str(), dep_si->name.c_str());
    }
    avd_sidep_si_dep_state_set(dep_si, AVD_SI_TOL_TIMER_RUNNING);
  } else if (cb->avail_state_avd == SA_AMF_HA_ACTIVE) {
    /*Tolerance time is not configured for this si-si dependency.
      If active amfd then send an event to start SI unassignment process.
     */
    if (dep_si->si_dep_state != AVD_SI_UNASSIGNING_DUE_TO_DEP) {
      avd_sidep_si_dep_state_set(dep_si, AVD_SI_READY_TO_UNASSIGN);
      sidep_si_dep_state_evt_send(cb, dep_si, AVD_EVT_UNASSIGN_SI_DEP_STATE);
    }
  }

done:
  TRACE_LEAVE();
}

/*****************************************************************************
 * Function: sidep_take_action_on_dependents
 *
 * Purpose:  Upon SI is assigned/unassigned and if this SI turns out to be
 *           sponsor SI for some of the SIs (dependents),then update the states
 *           of dependent SIs accordingly (either to AVD_SI_READY_TO_UNASSIGN /
 *           AVD_SI_TOL_TIMER_RUNNING states).
 *
 * Input:    si - ptr to AVD_SI struct (sponsor SI).
 *
 * Returns:  Nothing
 *
 * NOTES:
 *
 **************************************************************************/
void sidep_take_action_on_dependents(AVD_SI *si) {
  TRACE_ENTER();

  for (const auto &value : *sidep_db) {
    const AVD_SI_DEP *sidep = value.second;

    if (sidep->spons_si != si) continue;

    AVD_SI *dep_si = avd_si_get(sidep->dep_name);
    osafassert(dep_si != nullptr);

    /* Take action only when both sponsor and dependent belongs to same sg
       or if dependent sg fsm is stable.  When sg becomes stable,
       based on updated si_dep_state action is taken.
     */
    if ((dep_si->sg_of_si->sg_fsm_state != AVD_SG_FSM_STABLE) &&
        (dep_si->sg_of_si != si->sg_of_si)) {
      if ((dep_si->si_dep_state == AVD_SI_FAILOVER_UNDER_PROGRESS) &&
          (avd_sidep_all_sponsors_active(dep_si))) {
        avd_sidep_si_dep_state_set(dep_si, AVD_SI_READY_TO_ASSIGN);
        sidep_dependentsi_role_failover(dep_si);
      }
      continue;
    }

    if (dep_si->si_dep_state == AVD_SI_FAILOVER_UNDER_PROGRESS) {
      /* If the dependent SI is under AVD_SI_FAILOVER_UNDER_PROGRESS state
       * update its dep_state to AVD_SI_READY_TO_UNASSIGN
       */
      TRACE("Send an event to start SI unassignment process");
      avd_sidep_si_dep_state_set(dep_si, AVD_SI_READY_TO_UNASSIGN);
    }

    if (dep_si->si_dep_state == AVD_SI_READY_TO_UNASSIGN) {
      sidep_process_ready_to_unassign_depstate(dep_si);
    } else if (dep_si->si_dep_state == AVD_SI_READY_TO_ASSIGN) {
      sidep_si_dep_state_evt_send(avd_cb, dep_si, AVD_EVT_ASSIGN_SI_DEP_STATE);
    } else if (dep_si->si_dep_state == AVD_SI_ASSIGNED &&
        dep_si->sg_of_si->sg_fsm_state == AVD_SG_FSM_STABLE &&
        dep_si->saAmfSINumCurrActiveAssignments <
        dep_si->pref_active_assignments()) {
      sidep_si_dep_state_evt_send(avd_cb, dep_si, AVD_EVT_ASSIGN_SI_DEP_STATE);
    }
  }

  TRACE_LEAVE();
}

AVD_SI_DEP *get_sidep_with_same_dep(AVD_SI_DEP *sidep) {
  for (const auto &value : *sidep_db) {
    AVD_SI_DEP *tmp_sidep = value.second;
    if (sidep == tmp_sidep) continue;
    if (tmp_sidep->dep_name.compare(sidep->dep_name) == 0) return tmp_sidep;
  }
  return nullptr;
}

/*****************************************************************************
 * Function: sidep_cyclic_dep_find
 *
 * Purpose:  This function will use to evalute that is new record will because
 *           of cyclic dependency exist in SIs or not.
 *
 * Input: cb   - The AVD control block
 *        indx - Index of the new rec needs to be evaluated.
 *
 * Returns: NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE
 *
 * NOTES: Return value NCSCC_RC_SUCCESS means it found a cyclic dependency or
 *        buffer is not sufficient to process this request.
 *
 **************************************************************************/
uint32_t sidep_cyclic_dep_find(AVD_SI_DEP *sidep) {
  AVD_SI_DEP_NAME_LIST *start = nullptr;
  AVD_SI_DEP_NAME_LIST *temp = nullptr;
  AVD_SI_DEP_NAME_LIST *last = nullptr;
  uint32_t rc = NCSCC_RC_FAILURE;
  AVD_SI_DEP *tmp_sidep = nullptr;

  TRACE_ENTER();

  if (sidep->spons_name.compare(sidep->dep_name) == 0) {
    /* dependent SI and Sponsor SI can not be same
       Cyclic dependency found return sucess
     */
    rc = NCSCC_RC_SUCCESS;
    goto done;
  }

  start = new AVD_SI_DEP_NAME_LIST;
  start->si_name = sidep->spons_name;
  start->next = nullptr;
  last = start;
  LOG_NO("got si :%s", start->si_name.c_str());

  while (last) {
    for (const auto &value : *sidep_db) {
      tmp_sidep = value.second;
      if (tmp_sidep->dep_name.compare(last->si_name) == 0) break;
    }

    while ((tmp_sidep != nullptr) &&
           (tmp_sidep->dep_name.compare(last->si_name) == 0)) {
      if (sidep->dep_name.compare(tmp_sidep->spons_name) == 0) {
        /* Cyclic dependency found */
        rc = NCSCC_RC_SUCCESS;
        break;
      }

      /* Search if this SI name already exist in the list */
      temp = start;
      if (temp->si_name.compare(tmp_sidep->spons_name) != 0) {
        while ((temp->next != nullptr) &&
               (temp->next->si_name.compare(tmp_sidep->spons_name) != 0)) {
          temp = temp->next;
        }

        /* SI Name not found in the list, add it */
        if (temp->next == nullptr) {
          temp->next = new AVD_SI_DEP_NAME_LIST;
          temp->next->si_name = tmp_sidep->spons_name;
          temp->next->next = nullptr;
        }
      }
      tmp_sidep = get_sidep_with_same_dep(tmp_sidep);
    }

    if (rc == NCSCC_RC_SUCCESS) {
      break;
    } else {
      last = last->next;
    }
  }

  /* Free the allocated SI name list */
  while (start) {
    temp = start->next;
    delete start;
    start = temp;
  }

done:
  TRACE_LEAVE();
  return rc;
}

static int avd_sidep_indx_init(const std::string &sidep_name,
                               AVD_SI_DEP *sidep_obj) {
  get_child_dn_from_ass_dn(sidep_name, sidep_obj->spons_name);
  get_parent_dn_from_ass_dn(sidep_name, sidep_obj->dep_name);

  /* Set SI pointers */
  sidep_obj->spons_si = si_db->find(sidep_obj->spons_name);
  sidep_obj->dep_si = si_db->find(sidep_obj->dep_name);

  return true;
}

static uint32_t is_config_valid(const std::string &sidep_name,
                                CcbUtilOperationData_t *opdata) {
  AVD_SI_DEP sidep;
  AVD_SI *spons_si, *dep_si;
  CcbUtilOperationData_t *tmp;
  uint32_t dep_saAmfSIRank, spons_saAmfSIRank;
  uint32_t rc = false;
  bool dependent_si_is_assigned = false;

  TRACE_ENTER();

  if (!avd_sidep_indx_init(sidep_name, &sidep)) {
    report_ccb_validation_error(opdata,
                                "SI dep validation: Bad DN for SI Dependency");
    return false;
  }

  const SaNameTWrapper sponsor(sidep.spons_name);
  const SaNameTWrapper dependent(sidep.dep_name);

  /* Sponsor SI need to exist */
  if ((spons_si = avd_si_get(sidep.spons_name)) == nullptr) {
    if (opdata == nullptr) {
      report_ccb_validation_error(
          opdata,
          "SI dep validation: '%s' does not exist in model and '%s'"
          " depends on it",
          sidep.spons_name.c_str(), sidep.dep_name.c_str());
      goto done;
    }

    /* SI does not exist in current model, check CCB */
    if ((tmp = ccbutil_getCcbOpDataByDN(opdata->ccbId, sponsor)) == nullptr) {
      report_ccb_validation_error(
          opdata,
          "SI dep validation: '%s' does not exist in existing model or"
          " in CCB and '%s' depends on it",
          sidep.spons_name.c_str(), sidep.dep_name.c_str());
      goto done;
    }

    if (immutil_getAttr(const_cast<SaImmAttrNameT>("saAmfSIRank"),
                        tmp->param.create.attrValues, 0,
                        &spons_saAmfSIRank) != SA_AIS_OK)
      spons_saAmfSIRank = 0; /* default value is zero (lowest rank) if empty */
  } else {
    /* sponsor SI exist in current model, get rank */
    spons_saAmfSIRank = spons_si->saAmfSIRank;
  }

  if (spons_saAmfSIRank == 0)
    spons_saAmfSIRank = ~0U; /* zero means lowest possible rank */

  if ((dep_si = avd_si_get(sidep.dep_name)) == nullptr) {
    if (opdata == nullptr) {
      report_ccb_validation_error(
          opdata, "SI dep validation: '%s' does not exist in model",
          sidep.dep_name.c_str());
      goto done;
    }

    /* SI does not exist in current model, check CCB */
    if ((tmp = ccbutil_getCcbOpDataByDN(opdata->ccbId, dependent)) == nullptr) {
      report_ccb_validation_error(
          opdata,
          "SI dep validation: '%s' does not exist in existing"
          " model or in CCB",
          sidep.dep_name.c_str());
      goto done;
    }

    if (immutil_getAttr(const_cast<SaImmAttrNameT>("saAmfSIRank"),
                        tmp->param.create.attrValues, 0,
                        &dep_saAmfSIRank) != SA_AIS_OK)
      dep_saAmfSIRank = 0; /* default value is zero (lowest rank) if empty */
  } else {
    /* dependent SI exist in current model, get rank */
    if (dep_si->list_of_sisu) dependent_si_is_assigned = true;
    dep_saAmfSIRank = dep_si->saAmfSIRank;
  }

  /* don't allow to dynamically create a dependency from an assigned SI already
   * in the model */
  if ((opdata != nullptr) && dependent_si_is_assigned) {
    report_ccb_validation_error(
        opdata,
        "SI dep validation: adding dependency from existing SI '%s'"
        " to SI '%s' is not allowed",
        sidep.spons_name.c_str(), sidep.dep_name.c_str());
    goto done;
  }

  if (dep_saAmfSIRank == 0)
    dep_saAmfSIRank = ~0U; /* zero means lowest possible rank */

  /* higher number => lower rank, see 3.8.1.1 */
  if (spons_saAmfSIRank > dep_saAmfSIRank) {
    report_ccb_validation_error(
        opdata,
        "SI dep validation: Sponsor SI '%s' has lower rank than"
        " dependent SI '%s'",
        sidep.spons_name.c_str(), sidep.dep_name.c_str());
    goto done;
  }

  if (sidep_cyclic_dep_find(&sidep) == NCSCC_RC_SUCCESS) {
    /* Return value that record cannot be added due to cyclic dependency */
    report_ccb_validation_error(opdata,
                                "SI dep validation: cyclic dependency for '%s'",
                                sidep.dep_name.c_str());
    goto done;
  }

  rc = true;

done:
  TRACE_LEAVE();
  return rc;
}

static AVD_SI_DEP *sidep_new(const std::string &sidep_name,
                             const SaImmAttrValuesT_2 **attributes) {
  AVD_SI_DEP tmp_sidep, *sidep = nullptr;

  TRACE_ENTER2("%s", sidep_name.c_str());

  avd_sidep_indx_init(sidep_name, &tmp_sidep);
  if ((sidep = sidep_db_find(tmp_sidep.spons_name, tmp_sidep.dep_name)) !=
      nullptr) {
    TRACE("SI dependency already exists");
  }

  sidep = new AVD_SI_DEP();
  avd_sidep_indx_init(sidep_name, sidep);
  sidep->name = sidep_name;
  osafassert(sidep->dep_si != nullptr);
  osafassert(sidep->spons_si != nullptr);
  sidep_db->insert(make_pair(sidep->spons_name, sidep->dep_name), sidep);

  /* Allocate a new block structure for imm rec now */

  if (immutil_getAttr(const_cast<SaImmAttrNameT>("saAmfToleranceTime"),
                      attributes, 0, &sidep->saAmfToleranceTime) != SA_AIS_OK) {
    /* Empty, assign default value */
    sidep->saAmfToleranceTime = 0;
  }

  /* Add to dependent's sponsors list */
  avd_si_dep_spons_list_add(sidep->dep_si, sidep->spons_si, sidep);

  if (avd_cb->avail_state_avd == SA_AMF_HA_ACTIVE) {
    /* While configuring the dependencies, set the dependent dep state
      considering all its sponsors assignment status. Possibly a call to
      *_chosen_asgn() after this may be made and thus it will help in avoiding
      re-screening in *_chosen_asgn().
     */
    if (avd_sidep_all_sponsors_active(sidep->dep_si))
      avd_sidep_si_dep_state_set(sidep->dep_si, AVD_SI_READY_TO_ASSIGN);
    else
      avd_sidep_si_dep_state_set(sidep->dep_si, AVD_SI_SPONSOR_UNASSIGNED);
  }
  TRACE_LEAVE();
  return sidep;
}

/**
 * Get configuration for all AMF SI dependency objects from IMM
 * and create AVD internal objects.
 *
 * @return int
 */
SaAisErrorT avd_sidep_config_get(void) {
  SaAisErrorT error;
  SaImmSearchHandleT searchHandle;
  SaImmSearchParametersT_2 searchParam;
  SaNameT sidep_name;
  const SaImmAttrValuesT_2 **attributes;
  const char *className = "SaAmfSIDependency";

  TRACE_ENTER();

  searchParam.searchOneAttr.attrName =
      const_cast<SaImmAttrNameT>("SaImmAttrClassName");
  searchParam.searchOneAttr.attrValueType = SA_IMM_ATTR_SASTRINGT;
  searchParam.searchOneAttr.attrValue = &className;

  error = immutil_saImmOmSearchInitialize_o2(
      avd_cb->immOmHandle, nullptr, SA_IMM_SUBTREE,
      SA_IMM_SEARCH_ONE_ATTR | SA_IMM_SEARCH_GET_ALL_ATTR, &searchParam,
      nullptr, &searchHandle);

  if (SA_AIS_OK != error) {
    LOG_ER("%s: saImmOmSearchInitialize failed: %u", __FUNCTION__, error);
    goto done1;
  }

  while (immutil_saImmOmSearchNext_2(searchHandle, &sidep_name,
                                     (SaImmAttrValuesT_2 ***)&attributes) ==
         SA_AIS_OK) {
    const std::string sidep(Amf::to_string(&sidep_name));
    if (!is_config_valid(sidep, nullptr)) {
      error = SA_AIS_ERR_BAD_OPERATION;
      goto done2;
    }
    if (sidep_new(sidep, attributes) == nullptr) {
      error = SA_AIS_ERR_BAD_OPERATION;
      goto done2;
    }
  }

  error = SA_AIS_OK;

done2:
  (void)immutil_saImmOmSearchFinalize(searchHandle);
done1:
  TRACE_LEAVE2("%u", error);
  return error;
}

static SaAisErrorT sidep_ccb_completed_cb(CcbUtilOperationData_t *opdata) {
  SaAisErrorT rc = SA_AIS_OK;
  AVD_SI_DEP sidep;

  TRACE_ENTER2("CCB ID %llu, '%s'", opdata->ccbId,
               osaf_extended_name_borrow(&opdata->objectName));

  switch (opdata->operationType) {
    case CCBUTIL_CREATE:
      if (!is_config_valid(Amf::to_string(&opdata->objectName), opdata))
        rc = SA_AIS_ERR_BAD_OPERATION;
      break;
    case CCBUTIL_DELETE:
    case CCBUTIL_MODIFY:
      avd_sidep_indx_init(Amf::to_string(&opdata->objectName), &sidep);
      if (sidep_db_find(sidep.spons_name, sidep.dep_name) == nullptr)
        rc = SA_AIS_ERR_BAD_OPERATION;
      break;
    default:
      osafassert(0);
      break;
  }

  TRACE_LEAVE();
  return rc;
}

static void sidep_ccb_apply_cb(CcbUtilOperationData_t *opdata) {
  AVD_SI_DEP *sidep, tmp_sidep;
  AVD_SI *dep_si = nullptr;
  const SaImmAttrModificationT_2 *attr_mod;
  int i = 0;

  TRACE_ENTER2("CCB ID %llu, '%s'", opdata->ccbId,
               osaf_extended_name_borrow(&opdata->objectName));

  switch (opdata->operationType) {
    case CCBUTIL_CREATE:
      sidep = sidep_new(Amf::to_string(&opdata->objectName),
                        opdata->param.create.attrValues);
      break;

    case CCBUTIL_DELETE:
      avd_sidep_indx_init(Amf::to_string(&opdata->objectName), &tmp_sidep);
      sidep = sidep_db_find(tmp_sidep.spons_name, tmp_sidep.dep_name);
      if (sidep == nullptr && avd_cb->is_active() == false) {
        break;
      }
      osafassert(sidep != nullptr);
      dep_si = avd_si_get(sidep->dep_name);
      osafassert(dep_si != nullptr);
      /* If SI is in tolerance timer running state because of this
       * SI-SI dep cfg, then stop the tolerance timer.
       */
      if (sidep->si_dep_timer.is_active == true) {
        avd_stop_tmr(avd_cb, &sidep->si_dep_timer);

        if (dep_si->tol_timer_count > 0) dep_si->tol_timer_count--;
      }
      sidep_spons_list_del(sidep);
      sidep_db->erase(make_pair(sidep->spons_name, sidep->dep_name));
      delete sidep;
      /* Update the dependent SI according to its existing sponsors state
       * if this dependent SI is not in this delete ccb
       */
      SaNameT depSiDn;
      osaf_extended_name_lend(dep_si->name.c_str(), &depSiDn);
      if (ccbutil_getCcbOpDataByDN(opdata->ccbId, &depSiDn) == nullptr) {
        if (avd_cb->avail_state_avd == SA_AMF_HA_ACTIVE) {
          sidep_si_screen_si_dependencies(dep_si);
          sidep_si_take_action(dep_si);
        }
      }
      break;

    case CCBUTIL_MODIFY:
      avd_sidep_indx_init(Amf::to_string(&opdata->objectName), &tmp_sidep);
      sidep = sidep_db_find(tmp_sidep.spons_name, tmp_sidep.dep_name);
      while ((attr_mod = opdata->param.modify.attrMods[i++]) != nullptr) {
        if (!strcmp(attr_mod->modAttr.attrName, "saAmfToleranceTime")) {
          TRACE("saAmfToleranceTime modified from '%llu' to '%llu'",
                sidep->saAmfToleranceTime,
                *((SaTimeT *)attr_mod->modAttr.attrValues[0]));
          sidep->saAmfToleranceTime =
              *((SaTimeT *)attr_mod->modAttr.attrValues[0]);
        }
      }
      break;

    default:
      osafassert(0);
      break;
  }

  TRACE_LEAVE();
}

void avd_sidep_constructor(void) {
  sidep_db = new AmfDb<std::pair<std::string, std::string>, AVD_SI_DEP>;
  avd_class_impl_set("SaAmfSIDependency", nullptr, nullptr,
                     sidep_ccb_completed_cb, sidep_ccb_apply_cb);
}

void avd_sidep_start_tolerance_timer_for_dependant(AVD_SI *dep_si,
                                                   AVD_SI *spons_si) {
  AVD_SI_DEP *si_dep_rec;

  TRACE_ENTER();
  TRACE("dep_si:%s spons_si:%s", dep_si->name.c_str(), spons_si->name.c_str());

  si_dep_rec = sidep_db_find(spons_si->name, dep_si->name);
  if (si_dep_rec != nullptr) {
    sidep_update_si_dep_state_for_spons_unassign(avd_cb, dep_si, si_dep_rec);
  } else {
    TRACE("si_dep_rec is nullptr");
  }

  TRACE_LEAVE();
}
/**
 * @brief      Remove all the assignments of an SI
 *
 * @param[in]  si
 **/
void avd_si_unassign(AVD_SI *si) {
  AVD_SU_SI_REL *sisu;

  for (sisu = si->list_of_sisu; sisu; sisu = sisu->si_next) {
    if (sisu->fsm != AVD_SU_SI_STATE_UNASGN) avd_susi_del_send(sisu);
  }
}
/**
 * @brief       If role failover is not possible for the Sponsor SI, this
 *routine iterates through all of its dependantsi and 1) removes the SI
 *assignments, if the dependant has Active assignment on the which
 *went down 2) starts tolerance timer if the dependant SI has Active assignment
 *on some other node
 *
 * @param[in]  si
 **/
void avd_sidep_unassign_dependents(AVD_SI *si, AVD_SU *su) {
  AVD_SI *dep_si;
  AVD_SU_SI_REL *sisu;

  TRACE_ENTER2(": '%s'", si->name.c_str());

  for (const auto &value : *sidep_db) {
    const AVD_SI_DEP *sidep = value.second;
    if (sidep->spons_si->name.compare(si->name) != 0) continue;

    dep_si = si_db->find(sidep->dep_name);
    osafassert(dep_si != nullptr);

    /* Get the Active susi */
    for (sisu = dep_si->list_of_sisu; sisu; sisu = sisu->si_next) {
      if (((sisu->state == SA_AMF_HA_ACTIVE) ||
           (sisu->state == SA_AMF_HA_QUIESCING)) &&
          (sisu->fsm != AVD_SU_SI_STATE_UNASGN)) {
        break;
      }
    }

    if (sisu == nullptr) continue;

    /* if dependant SI has Active assignment on the node which went down, remove
     * the assignments else start Tolerance timer
     */
    if (m_NCS_MDS_DEST_EQUAL(&sisu->su->su_on_node->adest,
                             &su->su_on_node->adest)) {
      avd_si_unassign(dep_si);
    } else {
      /* Don't start tol timer if dep state are either in running or unassigned.
       */
      if (!((dep_si->si_dep_state == AVD_SI_TOL_TIMER_RUNNING) ||
            (dep_si->si_dep_state == AVD_SI_READY_TO_UNASSIGN))) {
        avd_sidep_start_tolerance_timer_for_dependant(dep_si, si);
      }
    }
    /* If this dependent SI is sponsor too, then unassign its dependents also */
    if (dep_si->num_dependents > 0) avd_sidep_unassign_dependents(dep_si, su);
  }
  TRACE_LEAVE();
}
/**
 * @brief       Checks the following assignment status of an SI during failover
 *              1) Whether SI has Active assignment
 *              2) Whether SI has valid standby assignments which can take
 *Active role
 *
 * @param[in]  si -- SI for which assignment state needs to be checked
 * @param[in]  su -- SU which faulted
 * @param[out] active_sisu -- active susi assignments
 * @param[out] valid_stdby_sisu -- valid standby assignment
 *
 * @return  true/false
 **/
bool si_assignment_check_during_failover(AVD_SI *si, AVD_SU *su,
                                         AVD_SU_SI_REL **active_sisu,
                                         AVD_SU_SI_REL **valid_stdby_sisu) {
  AVD_SU_SI_REL *sisu;
  bool assignmemt_status = false;

  for (sisu = si->list_of_sisu; sisu; sisu = sisu->si_next) {
    if ((sisu->su != su) && (*valid_stdby_sisu == nullptr)) {
      if (((sisu->state == SA_AMF_HA_STANDBY) ||
           (sisu->state == SA_AMF_HA_QUIESCED)) &&
          (sisu->fsm == AVD_SU_SI_STATE_ASGND) &&
          (sisu->su->saAmfSuReadinessState !=
           SA_AMF_READINESS_OUT_OF_SERVICE)) {
        *valid_stdby_sisu = sisu;
        if ((*active_sisu) && (*valid_stdby_sisu))
          break;
        else
          continue;
      }
    }
    if (*active_sisu == nullptr) {
      if (((sisu->state == SA_AMF_HA_ACTIVE) ||
           (sisu->state == SA_AMF_HA_QUIESCING) ||
           (sisu->state == SA_AMF_HA_QUIESCED)) &&
          (sisu->fsm != AVD_SU_SI_STATE_UNASGN)) {
        *active_sisu = sisu;
        assignmemt_status = true;

        if ((*active_sisu) && (*valid_stdby_sisu))
          break;
        else
          continue;
      }
    }
  }

  return assignmemt_status;
}
/**
 * @brief      Checks if role failover can be performed for this SI,
 *             role failover can not be done if the SI has dependency on
 *             an SI which has Active assignments on the same node
 *
 * @param[in]  si   -
 *
 * @return      true/false
 *
 **/
bool avd_sidep_is_si_failover_possible(AVD_SI *si, AVD_SU *su) {
  AVD_SPONS_SI_NODE *spons_si_node;
  AVD_SU_SI_REL *sisu, *tmp_sisu;
  AVD_SU_SI_REL *active_sisu = nullptr, *valid_stdby_sisu = nullptr;
  bool failover_possible = true;
  bool assignmemt_status = false;
  bool valid_standby = false;

  TRACE_ENTER2("SI: '%s', SU %s", si->name.c_str(), su->name.c_str());

  /* Role fail-over triggered because of node going down or role switch-over
     because of node lock.*/
  if ((su->su_on_node->saAmfNodeOperState == SA_AMF_OPERATIONAL_DISABLED) ||
      (su->su_on_node->saAmfNodeAdminState == SA_AMF_ADMIN_LOCKED)) {
    assignmemt_status = si_assignment_check_during_failover(
        si, su, &active_sisu, &valid_stdby_sisu);
    if (si->spons_si_list == nullptr) {
      /* Check if the susi is in unassigned state
       * 1) Beacuse of admin operation
       * 2) SI has Active assignments on the node which went down and there is
       * no Standby assignment for the SI
       */
      if ((si->saAmfSIAdminState == SA_AMF_ADMIN_LOCKED) ||
          (si->sg_of_si->saAmfSGAdminState == SA_AMF_ADMIN_LOCKED) ||
          ((si->list_of_sisu) &&
           (si->list_of_sisu->si_next == AVD_SU_SI_REL_NULL) &&
           (m_NCS_MDS_DEST_EQUAL(&si->list_of_sisu->su->su_on_node->adest,
                                 &su->su_on_node->adest))) ||
          (valid_stdby_sisu == nullptr)) {
        avd_si_unassign(si);
        failover_possible = false;
        if (si->num_dependents > 0) avd_sidep_unassign_dependents(si, su);
      }
      goto done;
    }

    if (si->si_dep_state == AVD_SI_TOL_TIMER_RUNNING) {
      /* Dependant SI and Tolerance timer is running
       * No need to to do role failover stop the Tolerance timer and delete the
       * assignments
       */
      sidep_stop_tol_timer(avd_cb, si);
      avd_si_unassign(si);

      failover_possible = false;
      if (si->num_dependents > 0) avd_sidep_unassign_dependents(si, su);
      goto done;
    }
    /* Case of dependant SI
     * Failover is possible for the dependant only in the following case
     * 1) None of its sponsors have Active assignment on the node which went
     * down 2) None of its sponsors SI is undergoing admin lock operation
     */
    for (spons_si_node = si->spons_si_list; spons_si_node;
         spons_si_node = spons_si_node->next) {
      active_sisu = nullptr;
      valid_stdby_sisu = nullptr;

      assignmemt_status = si_assignment_check_during_failover(
          spons_si_node->si, su, &active_sisu, &valid_stdby_sisu);

      if ((spons_si_node->si->saAmfSIAdminState == SA_AMF_ADMIN_LOCKED) ||
          (spons_si_node->si->sg_of_si->saAmfSGAdminState ==
           SA_AMF_ADMIN_LOCKED) ||
          ((spons_si_node->si->list_of_sisu) &&
           (spons_si_node->si->list_of_sisu->si_next == AVD_SU_SI_REL_NULL) &&
           (m_NCS_MDS_DEST_EQUAL(
               &spons_si_node->si->list_of_sisu->su->su_on_node->adest,
               &su->su_on_node->adest))) ||
          ((assignmemt_status == false) && (valid_stdby_sisu == nullptr))) {
        avd_si_unassign(si);
        failover_possible = false;
        goto done;
      }
      if ((spons_si_node->si->si_dep_state == AVD_SI_FAILOVER_UNDER_PROGRESS) ||
          ((active_sisu) &&
           (m_NCS_MDS_DEST_EQUAL(&active_sisu->su->su_on_node->adest,
                                 &su->su_on_node->adest)))) {
        failover_possible = false;
      }
    }
  } else {
    if (si->spons_si_list == nullptr) {
      TRACE("SI doesnot have any dependencies");
      /* Check whether valid standby SU is there for performing role failover */
      for (sisu = si->list_of_sisu; sisu != nullptr; sisu = sisu->si_next) {
        if (sisu->su != su) {
          if (((sisu->state == SA_AMF_HA_STANDBY) ||
               (sisu->state == SA_AMF_HA_QUIESCED)) &&
              (sisu->fsm == AVD_SU_SI_STATE_ASGND) &&
              (sisu->su->saAmfSuReadinessState !=
               SA_AMF_READINESS_OUT_OF_SERVICE)) {
            valid_standby = true;
          }
        }
      }

      if ((si->saAmfSIAdminState == SA_AMF_ADMIN_LOCKED) || (!valid_standby)) {
        failover_possible = false;
        avd_si_unassign(si);
      }
      goto done;
    }
    bool sponsor_in_modify_state = false;
    for (spons_si_node = si->spons_si_list; spons_si_node;
         spons_si_node = spons_si_node->next) {
      failover_possible = false;
      valid_standby = false;
      for (sisu = spons_si_node->si->list_of_sisu; sisu; sisu = sisu->si_next) {
        TRACE("sisu->si:%s state:%u fsm:%u", sisu->si->name.c_str(),
              sisu->state, sisu->fsm);
        if (((sisu->state == SA_AMF_HA_ACTIVE) ||
             (sisu->state == SA_AMF_HA_QUIESCING)) &&
            (sisu->fsm != AVD_SU_SI_STATE_UNASGN)) {
          failover_possible = true;
          if (sisu->fsm == AVD_SU_SI_STATE_MODIFY)
            sponsor_in_modify_state = true;
          if ((sisu->su == su) && (sisu->fsm == AVD_SU_SI_STATE_ASGND)) {
            sponsor_in_modify_state = true;
            TRACE("Sponsor '%s' active in same SU", sisu->si->name.c_str());
          }
          break;
        } else if ((sisu->state == SA_AMF_HA_QUIESCED) &&
                   (sisu->fsm == AVD_SU_SI_STATE_ASGND) && (sisu->su == su) &&
                   !sidep_is_si_active(sisu->si)) {
          /* If sponsor is quiesced in the same SU and not active in
             any other SU, it means failover of sponsor is still pending.
           */
          sponsor_in_modify_state = true;
          TRACE("Sponsor '%s' quiescd assigned in same SU",
                sisu->si->name.c_str());

        } else if ((spons_si_node->si->sg_of_si == si->sg_of_si) &&
                   (sisu->su != su) && (!valid_standby)) {
          /* Sponsor also belongs to the same SG
           * Check if it has valid standby susi for failover
           */
          if (((sisu->state == SA_AMF_HA_STANDBY) ||
               (sisu->state == SA_AMF_HA_QUIESCED)) &&
              (sisu->fsm == AVD_SU_SI_STATE_ASGND) &&
              (sisu->su->saAmfSuReadinessState !=
               SA_AMF_READINESS_OUT_OF_SERVICE)) {
            valid_standby = true;
          }
        }
      }
      if ((failover_possible == false) && (valid_standby == false)) {
        /* There is no Active assignment and no valid Standby which can take
         * Active assignment */
        if (si->si_dep_state == AVD_SI_TOL_TIMER_RUNNING)
          sidep_stop_tol_timer(avd_cb, si);
        for (tmp_sisu = si->list_of_sisu; tmp_sisu != nullptr;
             tmp_sisu = tmp_sisu->si_next)
          if (tmp_sisu->fsm != AVD_SU_SI_STATE_UNASGN)
            avd_susi_del_send(tmp_sisu);
        goto done;
      }
    }
    if (sponsor_in_modify_state == true) {
      avd_sidep_si_dep_state_set(si, AVD_SI_FAILOVER_UNDER_PROGRESS);
      failover_possible = false;
      goto done;
    }
  }
done:
  TRACE_LEAVE2("return value: %d", failover_possible);
  return failover_possible;
}
/**
 * @brief      Checks if role failover can be performed for a SU. Role failover
 *             can not be done if any of the SI's having Active assignments on
 *             this SU has Active assignments on the same node
 *
 * @param[in]  su
 *
 * @return      true/flase
 *
 **/
bool avd_sidep_is_su_failover_possible(AVD_SU *su) {
  AVD_SU_SI_REL *susi;
  bool flag = true;

  TRACE_ENTER2("SU:'%s' node_state:%d", su->name.c_str(),
               su->su_on_node->node_state);

  for (susi = su->list_of_susi; susi != nullptr; susi = susi->su_next) {
    if (susi->si->spons_si_list) {
      TRACE(":susi:%s si_dep_state:%u state:%u fsm:%u", susi->si->name.c_str(),
            susi->si->si_dep_state, susi->state, susi->fsm);
      if (susi->si->si_dep_state == AVD_SI_FAILOVER_UNDER_PROGRESS) {
        TRACE(
            "Role failover is deferred as sponsors role failover is under going");
        flag = false;
        goto done;
      }
      /* Check if susi->si has any dependency on si having Active asisgnments
       * on the other SU on the same node
       */
      flag = avd_sidep_is_si_failover_possible(susi->si, su);
      if (flag == false) {
        TRACE(
            "Role failover is deferred as sponsors role failover is under going");
        avd_sidep_si_dep_state_set(susi->si, AVD_SI_FAILOVER_UNDER_PROGRESS);
        goto done;
      }
    }
  }
done:
  TRACE_LEAVE2("return value: %d", flag);
  return flag;
}

/**
 * @brief       Checks whether SI has valid standby assignments which can take
 *Active role
 *
 * @param[in]   susi
 *
 * @return      true/false
 **/
bool valid_standby_susi(AVD_SU_SI_REL *sisu) {
  AVD_SU_SI_REL *tmp_sisu;

  for (tmp_sisu = sisu->si->list_of_sisu; tmp_sisu != nullptr;
       tmp_sisu = tmp_sisu->si_next) {
    if (tmp_sisu->su != sisu->su) {
      if (((tmp_sisu->state == SA_AMF_HA_STANDBY) ||
           (tmp_sisu->state == SA_AMF_HA_QUIESCED)) &&
          (tmp_sisu->fsm == AVD_SU_SI_STATE_ASGND) &&
          (tmp_sisu->su->saAmfSuReadinessState !=
           SA_AMF_READINESS_OUT_OF_SERVICE)) {
        return true;
      }
    }
  }
  return false;
}
/**
 * @brief       Updates dependents si_dep_state during SI role failover.
 *              a) If the dependent SI has Active assignment on the the same
 *node then sets the si_dep_state to AVD_SI_FAILOVER_UNDER_PROGRESS b) If the
 *dependent SI has Active assignments on some other node then if
 *the tolerence timer is configured sets dep_state to
 *AVD_SI_TOL_TIMER_RUNNING else sets si_dep_satte to AVD_SI_READY_TO_UNASSIGN
 *
 *
 * @param[in]  su   -
 *
 * @return
 *
 **/
void avd_sidep_update_depstate_si_failover(AVD_SI *si, AVD_SU *su) {
  AVD_SPONS_SI_NODE *spons_si_node;

  TRACE_ENTER2("si:%s", si->name.c_str());

  for (const auto &value : *sidep_db) {
    const AVD_SI_DEP *sidep = value.second;
    if (sidep->spons_si->name.compare(si->name) != 0) continue;

    AVD_SI *dep_si = avd_si_get(sidep->dep_name);
    osafassert(dep_si != nullptr);

    for (AVD_SU_SI_REL *sisu = dep_si->list_of_sisu; sisu;
         sisu = sisu->si_next) {
      TRACE("sisu si:%s su:%s state:%d fsm_state:%d", sisu->si->name.c_str(),
            sisu->su->name.c_str(), sisu->state, sisu->fsm);
      if (((sisu->state == SA_AMF_HA_ACTIVE) ||
           (sisu->state == SA_AMF_HA_QUIESCING) ||
           (sisu->state == SA_AMF_HA_QUIESCED)) &&
          (sisu->fsm != AVD_SU_SI_STATE_UNASGN)) {
        if (su->su_on_node->saAmfNodeOperState == SA_AMF_OPERATIONAL_DISABLED) {
          if ((m_NCS_MDS_DEST_EQUAL(&sisu->su->su_on_node->adest,
                                    &su->su_on_node->adest))) {
            if ((dep_si->si_dep_states_check() == false) &&
                (avd_sidep_sponsors_assignment_states(dep_si))) {
              avd_sidep_si_dep_state_set(dep_si,
                                         AVD_SI_FAILOVER_UNDER_PROGRESS);
              if (dep_si->num_dependents > 0) {
                /* This SI also has dependents under it, update their state also
                 */
                avd_sidep_update_depstate_si_failover(dep_si, su);
              }
            }
          }
        } else if (dep_si->sg_of_si == si->sg_of_si) {
          if (dep_si->si_dep_states_check() == false) {
            avd_sidep_si_dep_state_set(dep_si, AVD_SI_FAILOVER_UNDER_PROGRESS);
            if (dep_si->num_dependents > 0) {
              /* This SI also has dependents under it, update their state also
               */
              avd_sidep_update_depstate_si_failover(dep_si, su);
            }
          }
        }
      } else {
        /* In the case of su fault all the assignmemts on the Active SU will be
         * removed So we need to consider Standby assignment only for doing role
         * failover
         */
        if (su->su_on_node->saAmfNodeOperState != SA_AMF_OPERATIONAL_DISABLED) {
          if ((((sisu->state == SA_AMF_HA_ACTIVE) ||
                (sisu->state == SA_AMF_HA_QUIESCED)) &&
               (sisu->fsm == AVD_SU_SI_STATE_UNASGN)) &&
              (valid_standby_susi(sisu))) {
            /* Before updating the si_dep_state, check the assignment state of
             * all sponsors */
            bool sponsor_assignments_state = true;
            for (spons_si_node = dep_si->spons_si_list;
                 spons_si_node && sponsor_assignments_state == true;
                 spons_si_node = spons_si_node->next) {
              if (spons_si_node->si->si_dep_state ==
                  AVD_SI_FAILOVER_UNDER_PROGRESS)
                continue;
              for (sisu = spons_si_node->si->list_of_sisu; sisu;
                   sisu = sisu->si_next) {
                TRACE("sisu->si:%s state:%u fsm:%u", sisu->si->name.c_str(),
                      sisu->state, sisu->fsm);
                if (((sisu->state == SA_AMF_HA_ACTIVE) ||
                     (sisu->state == SA_AMF_HA_QUIESCING) ||
                     (sisu->state == SA_AMF_HA_QUIESCED)) &&
                    (sisu->fsm != AVD_SU_SI_STATE_UNASGN)) {
                  break;
                }
                if (sisu->si_next == nullptr) {
                  sponsor_assignments_state = false;
                  break;
                }
              }
            }
            if (sponsor_assignments_state == true) {
              if (dep_si->si_dep_states_check() == false) {
                avd_sidep_si_dep_state_set(dep_si,
                                           AVD_SI_FAILOVER_UNDER_PROGRESS);
                if (dep_si->num_dependents > 0) {
                  avd_sidep_update_depstate_si_failover(dep_si, su);
                }
              }
            }
          }
        }
      }
    }
  }
  TRACE_LEAVE();
}

/**
 * @brief       Update the si_dep_state for all the dependent SI's
 *              that are depending on SI's which have active assignments
 *              on this SU
 *
 * @param[in]  su   -
 *
 * @return
 *
 **/
void avd_sidep_update_depstate_su_rolefailover(AVD_SU *su) {
  AVD_SU_SI_REL *susi;

  TRACE_ENTER2(" for su '%s'", su->name.c_str());

  for (susi = su->list_of_susi; susi != nullptr; susi = susi->su_next) {
    if (susi->si->num_dependents > 0) {
      /* This is a Sponsor SI update its dependent states */
      avd_sidep_update_depstate_si_failover(susi->si, su);
    }
  }

  TRACE_LEAVE();
}
/**
 * @brief       Does SI role failover for the dependent SI.
 *
 * @param[in]   si - Dependent SI, for which role failover has to be done
 *
 * @return      Returns nothing
 **/
static void sidep_dependentsi_role_failover(AVD_SI *si) {
  AVD_SU *stdby_su = nullptr, *actv_su = nullptr;
  AVD_SU_SI_REL *susi, *std_susi = nullptr;
  uint32_t curr_pref_active_sus = 0;

  TRACE_ENTER2(" for SI '%s'", si->name.c_str());

  switch (si->sg_of_si->sg_redundancy_model) {
    case SA_AMF_2N_REDUNDANCY_MODEL:
      for (susi = si->list_of_sisu; susi != nullptr; susi = susi->si_next) {
        if (susi->state == SA_AMF_HA_STANDBY)
          stdby_su = susi->su;
        else
          actv_su = susi->su;
      }
      if (stdby_su) {
        if (avd_sidep_si_dependency_exists_within_su(stdby_su)) {
          for (susi = stdby_su->list_of_susi; susi != nullptr;
               susi = susi->su_next) {
            if (actv_su &&
                avd_susi_role_failover(susi, actv_su) == NCSCC_RC_FAILURE) {
              LOG_NO(" %s: %u: Active role modification failed for  %s ",
                     __FILE__, __LINE__, susi->su->name.c_str());
              goto done;
            }
          }

        } else {
          /* Check if there are any dependent SI's on this SU, if so check
             if their sponsor SI's * are in enable state or not
           */
          for (susi = stdby_su->list_of_susi; susi != nullptr;
               susi = susi->su_next) {
            if (susi->si->spons_si_list) {
              if (!avd_sidep_all_sponsors_active(si)) {
                TRACE("SI's sponsors are not yet assigned");
                goto done;
              }
            }
          }
          /* All of the dependent's Sponsors's are in assigned state
           * So performing role modification for the stdby_su
           */
          avd_sg_su_si_mod_snd(avd_cb, stdby_su, SA_AMF_HA_ACTIVE);

          avd_sidep_si_dep_state_set(si, AVD_SI_ASSIGNED);
        }
        if (si->sg_of_si->su_oper_list.empty() == true) {
          /* add the SU to the operation list and change the SG FSM to SG
           * realign. */
          avd_sg_su_oper_list_add(avd_cb, stdby_su, false);
          stdby_su->sg_of_su->set_fsm_state(AVD_SG_FSM_SG_REALIGN);
        }
      }
      break;
    case SA_AMF_NPM_REDUNDANCY_MODEL:
      for (susi = si->list_of_sisu; susi != nullptr; susi = susi->si_next) {
        if (susi->state == SA_AMF_HA_STANDBY) {
          stdby_su = susi->su;
          std_susi = susi;
          break;
        }
      }
      if (stdby_su) {
        /* Check if there are any dependent SI's on this SU, if so check if
         * their sponsor SI's are in enable state or not
         */
        for (susi = stdby_su->list_of_susi; susi != nullptr;
             susi = susi->su_next) {
          if (susi->si->spons_si_list) {
            if (!avd_sidep_all_sponsors_active(si)) {
              TRACE("SI's sponsors are not yet assigned");
              goto done;
            }
          }
        }
        /* All of the dependent's Sponsors's are in assigned state
         * So performing role modification for the stdby_su
         */

        curr_pref_active_sus = avd_sg_get_curr_act_cnt(stdby_su->sg_of_su);
        /* Check the possibility of assigning Active role to stdby_su */
        if (curr_pref_active_sus >=
            stdby_su->sg_of_su->saAmfSGNumPrefActiveSUs) {
          /* Send a D2N-INFO_SU_SI_ASSIGN with remove all to the standby SU */
          if (avd_sg_su_si_del_snd(avd_cb, stdby_su) == NCSCC_RC_FAILURE) {
            LOG_ER("SU del failed :%s :%u :%s", __FILE__, __LINE__,
                   stdby_su->name.c_str());
            goto done;
          }
        } else {
          AVD_SU_SI_REL *susi;
          for (susi = stdby_su->list_of_susi; susi != AVD_SU_SI_REL_NULL;
               susi = susi->su_next) {
            if (susi == std_susi) {
              continue;
            }
            avd_susi_del_send(susi);
          } /* for (susi = std_susi->su->list_of_susi; susi !=
               AVD_SU_SI_REL_NULL; susi = susi->su_next) */

          /* Now send active for all the remaining SUSIs */
          avd_sg_su_si_mod_snd(avd_cb, std_susi->su, SA_AMF_HA_ACTIVE);
        }

        /* Add Standby SU to SU operlist */
        avd_sg_su_oper_list_add(avd_cb, stdby_su, false);

        /*Change state to SG_realign. */
        stdby_su->sg_of_su->set_fsm_state(AVD_SG_FSM_SG_REALIGN);

        avd_sidep_si_dep_state_set(si, AVD_SI_ASSIGNED);
        if (si->sg_of_si->su_oper_list.empty() == true) {
          /* add the SU to the operation list and change the SG FSM to SG
           * realign. */
          avd_sg_su_oper_list_add(avd_cb, stdby_su, false);
          stdby_su->sg_of_su->set_fsm_state(AVD_SG_FSM_SG_REALIGN);
        }
      }
      break;
    case SA_AMF_N_WAY_REDUNDANCY_MODEL:
      if (avd_sidep_all_sponsors_active(si)) {
        /* identify the most preferred standby su for this si */
        susi = avd_find_preferred_standby_susi(si);
        if (susi) {
          avd_susi_mod_send(susi, SA_AMF_HA_ACTIVE);
          si->sg_of_si->set_fsm_state(AVD_SG_FSM_SG_REALIGN);
          avd_sidep_si_dep_state_set(si, AVD_SI_ASSIGNED);
        }
      }
      break;
    default:
      break;
  }
done:
  TRACE_LEAVE();
}
/**
 * @brief       While node failover is under progress, if su fault happens
 *              this routine resets the si_dep_state to
 *AVD_SI_SPONSOR_UNASSIGNED
 *
 * @param[in]   si
 *
 * @return      Returns nothing
 **/
void avd_sidep_reset_dependents_depstate_in_sufault(AVD_SI *si) {
  TRACE_ENTER2(" SI: '%s'", si->name.c_str());

  for (const auto &value : *sidep_db) {
    const AVD_SI_DEP *sidep = value.second;

    if (sidep->spons_si->name.compare(si->name) != 0) continue;

    AVD_SI *dep_si = avd_si_get(sidep->dep_name);
    osafassert(dep_si != nullptr);

    if (dep_si->si_dep_state == AVD_SI_FAILOVER_UNDER_PROGRESS)
      avd_sidep_si_dep_state_set(dep_si, AVD_SI_SPONSOR_UNASSIGNED);
  }

  TRACE_LEAVE();
}

/**
 * @brief       Checks whether failover can be done at SU level or SI level
 *              Per SU level failover cannot be done in the following two cases
 *              1)SI-SI dependency exists across the SI's within SU
 *              2)One of the SI's of SG_1 depends on SI of SG_2 and viceversa
 *
 * @param[in]   su
 *
 * @return      true/false
 **/
bool avd_sidep_si_dependency_exists_within_su(const AVD_SU *su) {
  AVD_SU_SI_REL *susi;
  bool spons_exist = false;
  bool dep_exist = false;
  TRACE_ENTER();

  for (susi = su->list_of_susi; susi != nullptr; susi = susi->su_next) {
    if (susi->si->num_dependents) {
      spons_exist = true;
    }
    if (susi->si->spons_si_list) {
      dep_exist = true;
    }

    if (spons_exist && dep_exist) return true;
  }

  return false;
}
/**
 * @brief       Iterates through all the dependants and for each one if all of
 *its sponsors are assigned sends Active role modification to it
 *
 * @param[in]   si
 *
 * @returns     nothing
 **/
void avd_sidep_send_active_to_dependents(const AVD_SI *si) {
  AVD_SU_SI_REL *sisu;
  AVD_SU *active_su = nullptr;

  TRACE_ENTER2(": '%s'", si->name.c_str());

  /* determine the active su */
  for (sisu = si->list_of_sisu; sisu != nullptr; sisu = sisu->si_next) {
    if ((sisu->state == SA_AMF_HA_ACTIVE) &&
        (sisu->fsm != AVD_SU_SI_STATE_UNASGN)) {
      active_su = sisu->su;
      break;
    }
  }

  for (const auto &value : *sidep_db) {
    const AVD_SI_DEP *sidep = value.second;
    if (sidep->spons_si->name.compare(si->name) != 0) continue;

    AVD_SI *dep_si = avd_si_get(sidep->dep_name);
    osafassert(dep_si != nullptr);

    TRACE("dependent si:%s dep_si->si_dep_state:%d", dep_si->name.c_str(),
          dep_si->si_dep_state);
    if (dep_si->si_dep_state == AVD_SI_FAILOVER_UNDER_PROGRESS) {
      if (!avd_sidep_all_sponsors_active(dep_si))
        /* Some of the sponsors are not yet in Active state */
        continue;
      AVD_SU_SI_REL *sisu;
      switch (si->sg_of_si->sg_redundancy_model) {
        case SA_AMF_NPM_REDUNDANCY_MODEL:
        case SA_AMF_N_WAY_ACTIVE_REDUNDANCY_MODEL:
        case SA_AMF_NO_REDUNDANCY_MODEL:
        case SA_AMF_2N_REDUNDANCY_MODEL:
          for (sisu = dep_si->list_of_sisu; sisu != nullptr;
               sisu = sisu->si_next) {
            if (sisu->su == active_su) {
              if (((sisu->state == SA_AMF_HA_STANDBY) ||
                   (sisu->state == SA_AMF_HA_QUIESCED)) &&
                  (sisu->fsm != AVD_SU_SI_STATE_UNASGN)) {
                avd_susi_mod_send(sisu, SA_AMF_HA_ACTIVE);
                break;
              }
            }
          }
          break;
        case SA_AMF_N_WAY_REDUNDANCY_MODEL:
          /* identify the most preferred standby su for this si */
          sisu = avd_find_preferred_standby_susi(dep_si);
          if (sisu) {
            avd_susi_mod_send(sisu, SA_AMF_HA_ACTIVE);
            avd_sidep_si_dep_state_set(dep_si, AVD_SI_ASSIGNED);
          } else
            /* As susi failover is not possible, delete all the
               assignments */
            dep_si->delete_assignments(avd_cb);
          break;
        default:
          break;
      }
      avd_sidep_si_dep_state_set(dep_si, AVD_SI_ASSIGNED);
    }
  }
  TRACE_LEAVE();
}
/**
 * @brief       Checks if quiesced response came for all of its dependents
 *
 * @param[in]   su
 *
 * @return      true/false
 **/
bool avd_sidep_quiesced_done_for_all_dependents(const AVD_SI *si,
                                                const AVD_SU *su) {
  bool quiesced = true;

  TRACE_ENTER2(": '%s'", si->name.c_str());

  for (const auto &value : *sidep_db) {
    const AVD_SI_DEP *sidep = value.second;
    if (sidep->spons_si->name.compare(si->name) != 0) continue;

    AVD_SI *dep_si = avd_si_get(sidep->dep_name);
    osafassert(dep_si != nullptr);

    for (AVD_SU_SI_REL *sisu = dep_si->list_of_sisu; sisu;
         sisu = sisu->si_next) {
      if ((sisu->su == su) && (((sisu->state != SA_AMF_HA_STANDBY) &&
                                (sisu->state != SA_AMF_HA_QUIESCED)) ||
                               (sisu->fsm != AVD_SU_SI_STATE_ASGND))) {
        quiesced = false;
        goto done;
      }
    }
  }

done:
  TRACE_LEAVE2(" :%u", quiesced);
  return quiesced;
}
/**
 * @brief       Checks whether all sponsors of an SI are in assigned state or
 *not Even if any one of the sponsor is in unassigned state, this routine
 *              returns NCSCC_RC_FAILURE else NCSCC_RC_SUCCESS
 *
 * @param[in]   SI
 *
 * @return      NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE
 *
 **/
bool avd_sidep_sponsors_assignment_states(AVD_SI *si) {
  bool all_assigned_active = true;
  AVD_SPONS_SI_NODE *spons_si;

  TRACE_ENTER2("SI:%s", si->name.c_str());

  if (si->spons_si_list == nullptr) {
    goto done;
  }

  for (spons_si = si->spons_si_list; spons_si; spons_si = spons_si->next) {
    if (si_assignment_state_check(spons_si->si) == false) {
      all_assigned_active = false;
      goto done;
    }
  }

done:
  TRACE_LEAVE2("spons actv:%u", all_assigned_active);
  return all_assigned_active;
}

/**
 * @brief       Updates si_dep_state of dependent SIs. Since a dependent SI
 *              can have more than one sponsors, si_dep_state is updated
 *              based on the assignment states of all its sponsors.
 *
 * @param[in]   si - pointer to SI.
 *
 * @return      Nothing
 *
 **/
void sidep_update_dependents_states(AVD_SI *si) {
  if (si->num_dependents == 0) return;

  /* Update the si_dep_state for all dependents
   * SIs of the sponsor SI.
   */
  TRACE("sponsor si:'%s', dep state:'%s'", si->name.c_str(),
        depstatename[si->si_dep_state]);

  for (const auto &value : *sidep_db) {
    const AVD_SI_DEP *sidep = value.second;
    if (sidep->spons_si->name.compare(si->name) != 0) continue;

    AVD_SI *dep_si = avd_si_get(sidep->dep_name);
    osafassert(dep_si != nullptr);

    /* update si_dep_state of dependent if its SG fsm is
       stable or if both sponsor and dependent belongs to same SG.
     */
    if ((dep_si->sg_of_si->sg_fsm_state == AVD_SG_FSM_STABLE) ||
        (dep_si->sg_of_si == si->sg_of_si))
      sidep_update_si_self_dep_state(dep_si);
  }
}

/**
 * @brief       An SI updates its own si_dep_state based on the
 *              on the assignment states of all its sponsors.
 *
 * @param[in]   si - pointer to SI.
 *
 * @return      Nothing
 *
 **/
void sidep_update_si_self_dep_state(AVD_SI *si) {
  bool all_sponsors_assgnd = false;

  TRACE_ENTER2("sponsor si:'%s'", si->name.c_str());

  /*Any dependent SI is never expcted in this state when screening is going on.
  In such situation do not update si_dep_state. It will be taken care during
  failover*/
  if (si->si_dep_state == AVD_SI_FAILOVER_UNDER_PROGRESS) {
    AVD_SPONS_SI_NODE *spons;
    /*If atleast one sponsor is unassigned, unassign the dependent*/
    for (spons = si->spons_si_list; spons; spons = spons->next) {
      if (spons->si->list_of_sisu == nullptr) {
        avd_sidep_si_dep_state_set(si, AVD_SI_READY_TO_UNASSIGN);
        goto done;
      }
    }
    goto done;
  }

  /*Check assignment states and si_dep_states of all sponsors*/
  all_sponsors_assgnd = avd_sidep_all_sponsors_active(si);

  if (all_sponsors_assgnd) {
    /*All the sponsors of this SI are assigned or no sponsors at all.*/

    if (sidep_is_si_active(si)) {
      /* Since SI is assigned set its si_dep_state to AVD_SI_ASSIGNED.*/
      avd_sidep_si_dep_state_set(si, AVD_SI_ASSIGNED);
      if (si->tol_timer_count > 0) /*Stop any toleracne timer if running */
        sidep_stop_tol_timer(avd_cb, si);
    } else {
      /*SI is unassigned. Modify its si_dep_state to
        AVD_SI_NO_DEPENDENCY if it does not have any sponsor
        otherwise modify it to AVD_SI_READY_TO_ASSIGN.
       */
      if (si->spons_si_list == nullptr)
        avd_sidep_si_dep_state_set(si, AVD_SI_NO_DEPENDENCY);
      else
        avd_sidep_si_dep_state_set(si, AVD_SI_READY_TO_ASSIGN);
    }
  } else {
    /*It means atleast one sponsor is unassigned.*/

    if (sidep_is_si_active(si)) {
      /*Since SI is assigned, set dep state to AVD_SI_READY_TO_UASSIGN.
        Later while taking action, start the tolerance timer or unassign SI
        depending upon the value of saAmfToleranceTime for unassigned sponsors
       */
      if (si->si_dep_state != AVD_SI_UNASSIGNING_DUE_TO_DEP)
        avd_sidep_si_dep_state_set(si, AVD_SI_READY_TO_UNASSIGN);
    } else
      avd_sidep_si_dep_state_set(si, AVD_SI_SPONSOR_UNASSIGNED);
  }
done:
  TRACE_LEAVE();
}

/**
 * @brief       Takes action on the SI from SI dependency perpspective
 *              depending upon the si_dep_state.
 *
 * @param[in]   si - pointer to SI.
 *
 * @return      Nothing
 *
 **/
void sidep_si_take_action(AVD_SI *si) {
  TRACE_ENTER2("si:'%s', si_dep_state:'%s'", si->name.c_str(),
               depstatename[si->si_dep_state]);

  switch (si->si_dep_state) {
    case AVD_SI_ASSIGNED:
      /* SI is assigned. Assign all the unassigned dependents*/
      if (si->num_dependents != 0) sidep_take_action_on_dependents(si);
      break;
    case AVD_SI_READY_TO_ASSIGN:
      /*If this SI is an sponsor, action will be taken on its
        dependents post SG stable screening.
       */
      if (si->spons_si_list == nullptr) {
        sidep_sg_red_si_process_assignment(avd_cb, si);
      } else
        sidep_si_dep_state_evt_send(avd_cb, si, AVD_EVT_ASSIGN_SI_DEP_STATE);

      break;
    case AVD_SI_READY_TO_UNASSIGN:
      /*Unassign this dependent because atleast one sponsor is
        unassigned.*/
      if (si->spons_si_list == nullptr) /*If sponsor SI then unassigned it.*/
        sidep_unassign_dependent(avd_cb, si);
      else
        /*For dependent SI start the tolerance timer or unassign it*/
        sidep_process_ready_to_unassign_depstate(si);

      break;
    case AVD_SI_SPONSOR_UNASSIGNED:
      /*This SI is unassigned so take action on dependents*/
      if (si->num_dependents) sidep_take_action_on_dependents(si);
      break;
    case AVD_SI_TOL_TIMER_RUNNING:
      /*Action will be taken at the expiry of tolerance timer.*/
      break;
    case AVD_SI_UNASSIGNING_DUE_TO_DEP:
      sidep_unassign_dependent(avd_cb, si);
      break;
    case AVD_SI_FAILOVER_UNDER_PROGRESS:
      /*No action action will taken during failover.*/
      break;
    case AVD_SI_NO_DEPENDENCY:
      /*a SI not participating in SI dependency or
        a SI having dependent(s) and no sponsor*/

      if ((!sidep_is_si_active(si)) &&
          (si->saAmfSIAdminState == SA_AMF_ADMIN_UNLOCKED)) {
        /*This is a SI with no sponsor. Assign it.*/
        sidep_sg_red_si_process_assignment(avd_cb, si);
      }
      if (si->num_dependents) {
        /*This is an unassigned SI with dependents but no sponsors.
          Take action on dependents.*/
        sidep_take_action_on_dependents(si);
      }
      break;
    default:
      osafassert(0);
      break;
  }
  TRACE_LEAVE();
}
/**
 * @brief       Take action on each SI or on its dependents in sg->list_of_si.
 *              Here it is assumed that all si_dep_states of all SIs
 *              are updated in SG.
 *
 * @param[in]   sg - pointer to SG.
 *
 * @return      Nothing
 *
 **/
void avd_sidep_sg_take_action(AVD_SG *sg) {
  TRACE_ENTER2("'%s'", sg->name.c_str());

  for (const auto &si : sg->list_of_si) {
    if ((si->spons_si_list != nullptr) || (si->num_dependents > 0))
      sidep_si_take_action(si);
  }
  TRACE_LEAVE();
}

/**
 * @brief       Starts tolerance timers as many as the no of unassigned
 *sponsors. For any SI dependency if sponsor is unassigned and
 *saAmfToleranceTime is 0 then dependent SI will be unassigned.
 *
 * @param[in]   si - pointer to dependent SI.
 *
 * @return      Nothing
 *
 **/
void sidep_process_ready_to_unassign_depstate(AVD_SI *dep_si) {
  AVD_SI *spons_si = nullptr;
  AVD_SI_DEP *si_dep_rec;
  AVD_SPONS_SI_NODE *temp_spons_list = nullptr;
  bool all_spon_assigned = true;

  TRACE_ENTER2("dep si:'%s'", dep_si->name.c_str());

  for (temp_spons_list = dep_si->spons_si_list; temp_spons_list != nullptr;
       temp_spons_list = temp_spons_list->next) {
    spons_si = temp_spons_list->si;
    TRACE("spons si:'%s'", spons_si->name.c_str());

    si_dep_rec = sidep_db_find(spons_si->name, dep_si->name);
    if (si_dep_rec == nullptr) goto done;

    if (sidep_is_si_active(spons_si)) {
      if (si_dep_rec->si_dep_timer.is_active == true) {
        avd_stop_tmr(avd_cb, &si_dep_rec->si_dep_timer);
        if (dep_si->tol_timer_count > 0) dep_si->tol_timer_count--;
      }
    } else {
      all_spon_assigned = false;
      if (si_dep_rec->saAmfToleranceTime) {
        /*start the tolerance timer and set si_dep_state to
         * AVD_SI_TOL_TIMER_RUNNING*/
        sidep_update_si_dep_state_for_spons_unassign(avd_cb, dep_si,
                                                     si_dep_rec);
      } else {
        /*since we are going to unassign the dependent stop other timers*/
        if (dep_si->tol_timer_count) sidep_stop_tol_timer(avd_cb, dep_si);

        if ((avd_cb->avail_state_avd == SA_AMF_HA_ACTIVE) &&
            (dep_si->si_dep_state != AVD_SI_UNASSIGNING_DUE_TO_DEP))
          sidep_si_dep_state_evt_send(avd_cb, dep_si,
                                      AVD_EVT_UNASSIGN_SI_DEP_STATE);
        break;
      }
    }
  }
  if (all_spon_assigned) {
    /* Reassign dependent or failover it */
    sidep_si_dep_state_evt_send(avd_cb, dep_si, AVD_EVT_ASSIGN_SI_DEP_STATE);
  }
done:
  TRACE_LEAVE();
}

/*
 * @brief Searching in sidep_db for all dependent si which having sponsor si as
 *        @spons_si_name
 * @param [in] @spons_si_name: Name of sponsor si
 * @param [out] @depsi_list: list of dependent si
 */
void get_dependent_si_list(const std::string &spons_si_name,
                           std::list<AVD_SI *> &depsi_list) {
  for (const auto &value : *sidep_db) {
    const AVD_SI_DEP *sidep = value.second;
    if (sidep->spons_si->name.compare(spons_si_name) != 0) continue;
    depsi_list.push_back(sidep->dep_si);
  }
}

/**
 * @brief       This function performs the unassignment of dependent SI as
 *              sponsor is unassigned (SI dependency logics).
 *
 * @param[in]   cb - pointer to avd_control block.
 * @param[in]   evt - pointer to AVD_EVT struct.
 *
 * @return      Nothing
 *
 **/
void avd_sidep_unassign_evh(AVD_CL_CB *cb, AVD_EVT *evt) {
  AVD_SI *dep_si = nullptr;

  TRACE_ENTER();

  if (evt == nullptr) {
    LOG_NO("Got nullptr event");
    goto done;
  }

  osafassert(evt->rcv_evt == AVD_EVT_UNASSIGN_SI_DEP_STATE);

  dep_si = avd_si_get(evt->info.tmr.dep_si_name);
  osafassert(dep_si != nullptr);

  avd_sidep_si_dep_state_set(dep_si, AVD_SI_UNASSIGNING_DUE_TO_DEP);

  /* Take action only when dependent sg fsm is stable.  When SG becomes
     stable, based on updated si_dep_state action is taken.
   */
  if (dep_si->sg_of_si->sg_fsm_state != AVD_SG_FSM_STABLE) {
    TRACE("sg unstable, so defering sidep action on si:'%s'",
          dep_si->name.c_str());
    goto done;
  }

  /* Process UNASSIGN evt */
  LOG_NO("Unassigning due to dep'%s'", dep_si->name.c_str());
  sidep_si_dep_start_unassign(cb, evt);

done:
  TRACE_LEAVE();
}

/**
 * @brief Checks if si_dep_state is either in AVD_SI_TOL_TIMER_RUNNING,
 * AVD_SI_READY_TO_UNASSIGN, AVD_SI_FAILOVER_UNDER_PROGRESS.
 * @return true if si_dep_state is either one of the above state else false.
 */
bool AVD_SI::si_dep_states_check() {
  TRACE_ENTER2("%u", si_dep_state);
  if ((si_dep_state == AVD_SI_TOL_TIMER_RUNNING) ||
      (si_dep_state == AVD_SI_READY_TO_UNASSIGN) ||
      (si_dep_state == AVD_SI_FAILOVER_UNDER_PROGRESS)) {
    return true;
  }
  TRACE_LEAVE();
  return false;
}
