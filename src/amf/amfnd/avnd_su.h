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

  This module is the include file for handling Availability Node Directors
  service unit structure.

******************************************************************************
*/

/*
 * Module Inclusion Control...
 */
#ifndef AMF_AMFND_AVND_SU_H_
#define AMF_AMFND_AVND_SU_H_

struct avnd_cb_tag;

/***************************************************************************
 **********  S T R U C T U R E / E N U M  D E F I N I T I O N S  ***********
 ***************************************************************************/

/*##########################################################################
                      SERVICE INSTANCE (SI) DEFINITIONS
 ##########################################################################*/

/*
 * SU-SI Assign Message contains some message specific parameters that are
 * not a part of SU-SI parameters. Concealing those params thru this typedef.
 */
typedef AVSV_D2N_INFO_SU_SI_ASSIGN_MSG_INFO AVND_SU_SI_PARAM;
typedef AVSV_D2N_COMPCSI_ASSIGN_MSG_INFO AVND_COMP_CSI_PARAMS_INFO;

/* declaration clc event handler */
typedef uint32_t (*AVND_SU_PRES_FSM_FN)(struct avnd_cb_tag *,
                                        struct avnd_su_tag *, AVND_COMP *);

/* su presence state fsm events */
typedef enum avnd_su_pres_fsm_ev {
  AVND_SU_PRES_FSM_EV_INST = 1,
  AVND_SU_PRES_FSM_EV_TERM,
  AVND_SU_PRES_FSM_EV_RESTART,
  AVND_SU_PRES_FSM_EV_COMP_INSTANTIATED,
  AVND_SU_PRES_FSM_EV_COMP_INST_FAIL,
  AVND_SU_PRES_FSM_EV_COMP_RESTARTING,
  AVND_SU_PRES_FSM_EV_COMP_TERM_FAIL,
  AVND_SU_PRES_FSM_EV_COMP_UNINSTANTIATED,
  AVND_SU_PRES_FSM_EV_COMP_TERMINATING,
  AVND_SU_PRES_FSM_EV_MAX
} AVND_SU_PRES_FSM_EV;

/* SI assignment state type definition */
typedef enum avnd_su_si_assign_state {
  AVND_SU_SI_ASSIGN_STATE_UNASSIGNED = 1,
  AVND_SU_SI_ASSIGN_STATE_ASSIGNING = 2,
  AVND_SU_SI_ASSIGN_STATE_ASSIGNED = 3,
  AVND_SU_SI_ASSIGN_STATE_REMOVING = 4,
  AVND_SU_SI_ASSIGN_STATE_REMOVED = 5,
  AVND_SU_SI_ASSIGN_STATE_MAX,
} AVND_SU_SI_ASSIGN_STATE;

/* SI definition */
typedef struct avnd_su_si_rec {
  NCS_DB_LINK_LIST_NODE su_dll_node; /* node in the su-si dll */
  NCS_DB_LINK_LIST_NODE cb_dll_node; /* node in the su-si dll */
  std::string name;                  /* si name */
  uint32_t rank;

  SaAmfHAStateT curr_state; /* current si ha state */
  SaAmfHAStateT prv_state;  /* prv si ha state */

  AVND_SU_SI_ASSIGN_STATE curr_assign_state; /* si assignment state wrt
                                                current ha state */
  AVND_SU_SI_ASSIGN_STATE prv_assign_state;  /* si assignment state wrt
                                                prv ha state */

  NCS_DB_LINK_LIST csi_list; /* ordered csi list (based on csi rank) */

  /* links to other entities */
  struct avnd_su_tag *su;                 /* bk ptr to su */
  std::string su_name;                    /* For checkpointing su name */
  AVSV_SUSI_ACT single_csi_add_rem_in_si; /* To detect whether single csi
                                             addition/removal is going on.*/
} AVND_SU_SI_REC;

/* SU-SI buffer record definition */
typedef struct avnd_su_siq_rec {
  NCS_DB_LINK_LIST_NODE su_dll_node; /* node in the siq dll */
  AVND_SU_SI_PARAM info;             /* su-si param */
} AVND_SU_SIQ_REC;

/*##########################################################################
                      SERVICE UNIT DEFINITION (TOP LEVEL)
 ##########################################################################*/

typedef AVSV_SU_INFO_MSG AVND_SU_PARAM;

typedef struct avnd_su_tag {
  std::string name; /* su name */

  uint32_t su_hdl; /* hdl returned by hdl-mngr */

  /* su attributes */
  uint32_t flag; /* su attributes */

  /* Update received flag, which will normally be false and will be
   * true if updates are received from the AVD on fail-over.*/
  bool avd_updt_flag;

  /* error recovery escalation params */
  AVND_ERR_ESC_LEVEL su_err_esc_level; /* curr escalation level of this su */
  SaTimeT comp_restart_prob; /* comp restart probation period (config) */
  uint32_t comp_restart_max; /* max comp restart count (config) */
  SaTimeT su_restart_prob;   /* su restart probation period (config) */
  uint32_t su_restart_max;   /* max su restart count (config) */
  uint32_t comp_restart_cnt; /* comp restart counts within comp-prob period */
  uint32_t su_restart_cnt;   /* su restart counts within su-prob period */
  AVND_TMR su_err_esc_tmr;   /* su err esc tmr */

  /* su states */
  SaAmfOperationalStateT oper; /* oper state of the su */
  SaAmfPresenceStateT pres;    /* presence state of the su */

  /* statistical info */
  uint32_t si_active_cnt;  /* no of active SIs assigned to this su */
  uint32_t si_standby_cnt; /* no of standby SIs assigned to this su */

  std::string suMaintenanceCampaign;

  /*
   * Ordered comp list (based on inst level). Note that as the
   * lexicographic key (comp-name) & the keys used for
   * comparision (inst-level) are different, only a limited number
   * of DLL APIs can be used.
   */
  NCS_DB_LINK_LIST comp_list;

  NCS_DB_LINK_LIST si_list; /* si list (si-name is the index) */
  NCS_DB_LINK_LIST siq;     /* susi msg buf (no index) maintains fifo order */

  /* To have the knowledge in AvND if this su belongs to NCS_SG */
  bool is_ncs;
  bool su_is_external; /*indicates if this SU is external */

  /* To maintain saAmfSUFailover attribute of SU classs at Amfnd. */
  bool sufailover; /* sufailover is enabled or not for the SU. */
  SaAmfAdminOperationIdT admin_op_Id;  // flag to track admin operation on su.
  bool avnd_su_check_sis_previous_assign_state(
      const AVND_SU_SI_ASSIGN_STATE prev_state) const;

} AVND_SU;

#define AVND_SU_NULL ((AVND_SU *)0)

/***************************************************************************
 ******************  M A C R O   D E F I N I T I O N S  ********************
 ***************************************************************************/

/* macros for su si assignment state */
#define m_AVND_SU_SI_CURR_ASSIGN_STATE_IS_UNASSIGNED(x) \
  ((AVND_SU_SI_ASSIGN_STATE_UNASSIGNED == (x)->curr_assign_state))
#define m_AVND_SU_SI_CURR_ASSIGN_STATE_IS_ASSIGNING(x) \
  ((AVND_SU_SI_ASSIGN_STATE_ASSIGNING == (x)->curr_assign_state))
#define m_AVND_SU_SI_CURR_ASSIGN_STATE_IS_ASSIGNED(x) \
  ((AVND_SU_SI_ASSIGN_STATE_ASSIGNED == (x)->curr_assign_state))
#define m_AVND_SU_SI_CURR_ASSIGN_STATE_IS_REMOVING(x) \
  ((AVND_SU_SI_ASSIGN_STATE_REMOVING == (x)->curr_assign_state))
#define m_AVND_SU_SI_CURR_ASSIGN_STATE_IS_REMOVED(x) \
  ((AVND_SU_SI_ASSIGN_STATE_REMOVED == (x)->curr_assign_state))
#define m_AVND_SU_SI_CURR_ASSIGN_STATE_SET(x, val) \
  (((x)->curr_assign_state = val))

#define m_AVND_SU_SI_PRV_ASSIGN_STATE_IS_UNASSIGNED(x) \
  ((AVND_SU_SI_ASSIGN_STATE_UNASSIGNED == (x)->prv_assign_state))
#define m_AVND_SU_SI_PRV_ASSIGN_STATE_IS_ASSIGNING(x) \
  ((AVND_SU_SI_ASSIGN_STATE_ASSIGNING == (x)->prv_assign_state))
#define m_AVND_SU_SI_PRV_ASSIGN_STATE_IS_ASSIGNED(x) \
  ((AVND_SU_SI_ASSIGN_STATE_ASSIGNED == (x)->prv_assign_state))
#define m_AVND_SU_SI_PRV_ASSIGN_STATE_IS_REMOVING(x) \
  ((AVND_SU_SI_ASSIGN_STATE_REMOVING == (x)->prv_assign_state))
#define m_AVND_SU_SI_PRV_ASSIGN_STATE_IS_REMOVED(x) \
  ((AVND_SU_SI_ASSIGN_STATE_REMOVED == (x)->prv_assign_state))
#define m_AVND_SU_SI_PRV_ASSIGN_STATE_SET(x, val) \
  (((x)->prv_assign_state = val))

/* macro to determine if all the SIs that belong to su are assigned */
#define m_AVND_SU_ARE_ALL_SI_ASSIGNED(su, are)                                \
  {                                                                           \
    AVND_SU_SI_REC *curr = 0;                                                 \
    (are) = true;                                                             \
    for (curr = (AVND_SU_SI_REC *)m_NCS_DBLIST_FIND_FIRST(&su->si_list);      \
         curr && m_AVND_SU_SI_CURR_ASSIGN_STATE_IS_ASSIGNED(curr);            \
         curr = (AVND_SU_SI_REC *)m_NCS_DBLIST_FIND_NEXT(&curr->su_dll_node)) \
      ;                                                                       \
    if (curr) (are) = false;                                                  \
  }

/* macros for su oper state */
#define m_AVND_SU_OPER_STATE_IS_ENABLED(x) \
  ((SA_AMF_OPERATIONAL_ENABLED == (x)->oper))
#define m_AVND_SU_OPER_STATE_IS_DISABLED(x) \
  ((SA_AMF_OPERATIONAL_DISABLED == (x)->oper))
#define m_AVND_SU_OPER_STATE_SET(x, val) (((x)->oper = val))

/* macro to determine if all the comps in an su are operationally enabled */
#define m_AVND_SU_IS_ENABLED(su, is)                      \
  {                                                       \
    AVND_COMP *curr = 0;                                  \
    (is) = true;                                          \
    for (curr = m_AVND_COMP_FROM_SU_DLL_NODE_GET(         \
             m_NCS_DBLIST_FIND_FIRST(&su->comp_list));    \
         curr && m_AVND_COMP_OPER_STATE_IS_ENABLED(curr); \
         curr = m_AVND_COMP_FROM_SU_DLL_NODE_GET(         \
             m_NCS_DBLIST_FIND_NEXT(&curr->su_dll_node))) \
      ;                                                   \
    if (curr) (is) = false;                               \
  }

/* macro to determine if all the pi comps in an su are instantiated */
#define m_AVND_SU_IS_INSTANTIATED(su, is)                         \
  {                                                               \
    AVND_COMP *curr = 0;                                          \
    (is) = true;                                                  \
    for (curr = m_AVND_COMP_FROM_SU_DLL_NODE_GET(                 \
             m_NCS_DBLIST_FIND_FIRST(&su->comp_list));            \
         curr; curr = m_AVND_COMP_FROM_SU_DLL_NODE_GET(           \
                   m_NCS_DBLIST_FIND_NEXT(&curr->su_dll_node))) { \
      if (m_AVND_COMP_TYPE_IS_PREINSTANTIABLE(curr) &&            \
          !m_AVND_COMP_PRES_STATE_IS_INSTANTIATED(curr)) {        \
        (is) = false;                                             \
        break;                                                    \
      }                                                           \
    }                                                             \
  }

/* macros to manage the presence state */
#define m_AVND_SU_PRES_STATE_IS_INSTANTIATED(x) \
  (SA_AMF_PRESENCE_INSTANTIATED == (x)->pres)
#define m_AVND_SU_PRES_STATE_IS_INSTANTIATING(x) \
  (SA_AMF_PRESENCE_INSTANTIATING == (x)->pres)

/* pre-configured su params */
#define AVND_SU_FLAG_RESTART_DIS 0x00000001
#define AVND_SU_FLAG_PREINSTANTIABLE 0x00000002

/* su state (restart, failed etc.) values */
#define AVND_SU_FLAG_RESTART 0x00000100
#define AVND_SU_FLAG_FAILOVER 0x00000200
#define AVND_SU_FLAG_FAILED 0x00000400

/** Set when a SUSI req targeting ALL SIs is received, reset when resp sent
 * The purpose of this flag is to remember if the request targeted all SIs,
 * this is needed when sending the response. */
#define AVND_SU_FLAG_ALL_SI 0x00000800

#define AVND_SU_ADMN_TERM \
  0x00001000 /* set; when a admn term req for an SU is received */
#define AVND_SU_ASSIGN_PEND 0x00002000 /* set; when su si assign starts */
#define AVND_SU_ALL_TERM                                                                    \
  0x00004000                          /* set; when all resp for term has come for all comps \
                                         used only in inst failed state */
#define AVND_SU_REG_FAILED 0x00008000 /* set; when su registration fails */

/* macros for checking the su states */
#define m_AVND_SU_IS_RESTART(x) (((x)->flag) & AVND_SU_FLAG_RESTART)
#define m_AVND_SU_IS_FAILOVER(x) (((x)->flag) & AVND_SU_FLAG_FAILOVER)
#define m_AVND_SU_IS_FAILED(x) (((x)->flag) & AVND_SU_FLAG_FAILED)
#define m_AVND_SU_IS_ALL_SI(x) (((x)->flag) & AVND_SU_FLAG_ALL_SI)
#define m_AVND_SU_IS_ADMN_TERM(x) (((x)->flag) & AVND_SU_ADMN_TERM)
#define m_AVND_SU_IS_ASSIGN_PEND(x) (((x)->flag) & AVND_SU_ASSIGN_PEND)
#define m_AVND_SU_IS_ALL_TERM(x) (((x)->flag) & AVND_SU_ALL_TERM)

/* macros for setting the su states */
#define m_AVND_SU_RESTART_SET(x) (((x)->flag) |= AVND_SU_FLAG_RESTART)
#define m_AVND_SU_FAILOVER_SET(x) (((x)->flag) |= AVND_SU_FLAG_FAILOVER)
#define m_AVND_SU_FAILED_SET(x) (((x)->flag) |= AVND_SU_FLAG_FAILED)
#define m_AVND_SU_ALL_SI_SET(x) (((x)->flag) |= AVND_SU_FLAG_ALL_SI)
#define m_AVND_SU_ADMN_TERM_SET(x) (((x)->flag) |= AVND_SU_ADMN_TERM)
#define m_AVND_SU_ASSIGN_PEND_SET(x) (((x)->flag) |= AVND_SU_ASSIGN_PEND)
#define m_AVND_SU_ALL_TERM_SET(x) (((x)->flag) |= AVND_SU_ALL_TERM)

/* macros for resetting the su states */
#define m_AVND_SU_RESTART_RESET(x) (((x)->flag) &= ~AVND_SU_FLAG_RESTART)
#define m_AVND_SU_FAILOVER_RESET(x) (((x)->flag) &= ~AVND_SU_FLAG_FAILOVER)
#define m_AVND_SU_FAILED_RESET(x) (((x)->flag) &= ~AVND_SU_FLAG_FAILED)
#define m_AVND_SU_ALL_SI_RESET(x) (((x)->flag) &= ~AVND_SU_FLAG_ALL_SI)
#define m_AVND_SU_ADMN_TERM_RESET(x) (((x)->flag) &= ~AVND_SU_ADMN_TERM)
#define m_AVND_SU_ASSIGN_PEND_RESET(x) (((x)->flag) &= ~AVND_SU_ASSIGN_PEND)
#define m_AVND_SU_ALL_TERM_RESET(x) (((x)->flag) &= ~AVND_SU_ALL_TERM)
#define m_AVND_SU_STATE_RESET(x) (((x)->flag) &= 0xff)

/* macros for checking the su params */
#define m_AVND_SU_IS_SU_RESTART_DIS(x) (((x)->flag) & AVND_SU_FLAG_RESTART_DIS)
#define m_AVND_SU_IS_PREINSTANTIABLE(x) \
  (((x)->flag) & AVND_SU_FLAG_PREINSTANTIABLE)
#define m_AVND_SU_IS_REG_FAILED(x) (((x)->flag) & AVND_SU_REG_FAILED)

/* macros for setting the su params */
#define m_AVND_SU_RESTART_DIS_SET(x) (((x)->flag) |= AVND_SU_FLAG_RESTART_DIS)
#define m_AVND_SU_PREINSTANTIABLE_SET(x) \
  (((x)->flag) |= AVND_SU_FLAG_PREINSTANTIABLE)
#define m_AVND_SU_REG_FAILED_SET(x) (((x)->flag) |= AVND_SU_REG_FAILED)

/* macros for resetting the su params */
#define m_AVND_SU_RESTART_DIS_RESET(x) \
  (((x)->flag) &= ~AVND_SU_FLAG_RESTART_DIS)
#define m_AVND_SU_PREINSTANTIABLE_RESET(x) \
  (((x)->flag) &= ~AVND_SU_FLAG_PREINSTANTIABLE)

/*
 * Macros for managing the error escalation levels
 */
#define m_AVND_SU_ERR_ESC_LEVEL_IS_0(x) \
  ((x)->su_err_esc_level == AVND_ERR_ESC_LEVEL_0)

#define m_AVND_SU_ERR_ESC_LEVEL_IS_1(x) \
  ((x)->su_err_esc_level == AVND_ERR_ESC_LEVEL_1)

#define m_AVND_SU_ERR_ESC_LEVEL_IS_2(x) \
  ((x)->su_err_esc_level == AVND_ERR_ESC_LEVEL_2)

#define m_AVND_SU_ERR_ESC_LEVEL_SET(x, val) ((x)->su_err_esc_level = (val))

/* macro to add a component to the su-comp list */
#define m_AVND_SUDB_REC_COMP_ADD(su, comp, rc) \
  sudb_rec_comp_add(&su, &comp, &rc)

/* macro to remove a component from the su-comp list */
#define m_AVND_SUDB_REC_COMP_REM(su, comp) \
  ncs_db_link_list_delink(&(su).comp_list, &(comp).su_dll_node)

/* macro to add a si record to the su-si list */
#define m_AVND_SUDB_REC_SI_ADD(su, si, rc)                         \
  {                                                                \
    (si).su_dll_node.key = (uint8_t *)(si).name.c_str();           \
    (rc) = ncs_db_link_list_add(&(su).si_list, &(si).su_dll_node); \
  };

/* macro to remove a si record from the su-si list */
#define m_AVND_SUDB_REC_SI_REM(su, si) \
  ncs_db_link_list_delink(&(su).si_list, &(si).su_dll_node)

/* macro to remove a csi-record from the si-csi list */
#define m_AVND_SU_SI_CSI_REC_REM(si, csi) \
  ncs_db_link_list_delink(&(si).csi_list, &(csi).si_dll_node)

/***************************************************************************
 ******  E X T E R N A L   F U N C T I O N   D E C L A R A T I O N S  ******
 ***************************************************************************/

struct avnd_cb_tag;

uint32_t avnd_su_pres_fsm_run(struct avnd_cb_tag *, AVND_SU *, AVND_COMP *,
                              AVND_SU_PRES_FSM_EV);

AVND_SU *avnd_sudb_rec_add(struct avnd_cb_tag *, AVND_SU_PARAM *, uint32_t *);
uint32_t avnd_sudb_rec_del(struct avnd_cb_tag *, const std::string &);

AVND_SU_SI_REC *avnd_su_si_rec_modify(struct avnd_cb_tag *, AVND_SU *,
                                      AVND_SU_SI_PARAM *, uint32_t *);
uint32_t avnd_su_si_all_modify(struct avnd_cb_tag *, AVND_SU *,
                               AVND_SU_SI_PARAM *);
AVND_SU_SI_REC *avnd_su_si_rec_add(struct avnd_cb_tag *cb, AVND_SU *su,
                                   AVND_SU_SI_PARAM *param, uint32_t *rc);
uint32_t avnd_su_si_rec_del(struct avnd_cb_tag *, const std::string &,
                            const std::string &);
uint32_t avnd_su_si_del(struct avnd_cb_tag *, const std::string &);
AVND_SU_SI_REC *avnd_su_si_rec_get(struct avnd_cb_tag *, const std::string &,
                                   const std::string &);
uint32_t avnd_su_si_msg_prc(struct avnd_cb_tag *, AVND_SU *,
                            AVND_SU_SI_PARAM *);

AVND_SU_SIQ_REC *avnd_su_siq_rec_add(struct avnd_cb_tag *, AVND_SU *,
                                     AVND_SU_SI_PARAM *, uint32_t *);
void avnd_su_siq_rec_del(struct avnd_cb_tag *, AVND_SU *, AVND_SU_SIQ_REC *);
AVND_SU_SIQ_REC *avnd_su_siq_rec_buf(struct avnd_cb_tag *, AVND_SU *,
                                     AVND_SU_SI_PARAM *);
uint32_t avnd_su_siq_prc(struct avnd_cb_tag *, AVND_SU *);

uint32_t avnd_su_si_assign(struct avnd_cb_tag *, AVND_SU *, AVND_SU_SI_REC *);
uint32_t avnd_su_si_remove(struct avnd_cb_tag *, AVND_SU *, AVND_SU_SI_REC *);
uint32_t avnd_su_si_oper_done(struct avnd_cb_tag *, AVND_SU *,
                              AVND_SU_SI_REC *);

uint32_t avnd_su_si_unmark(struct avnd_cb_tag *, AVND_SU *);
uint32_t avnd_su_si_rec_unmark(struct avnd_cb_tag *, AVND_SU *,
                               AVND_SU_SI_REC *);
uint32_t avnd_su_si_reassign(struct avnd_cb_tag *, AVND_SU *);
uint32_t avnd_su_curr_info_del(struct avnd_cb_tag *, AVND_SU *);

uint32_t avnd_su_oper_req(struct avnd_cb_tag *cb, AVSV_PARAM_INFO *param);
extern uint32_t avnd_evt_su_admin_op_req(struct avnd_cb_tag *cb,
                                         struct avnd_evt_tag *evt);
extern struct avnd_comp_csi_rec *avnd_su_si_csi_rec_add(struct avnd_cb_tag *,
                                                        AVND_SU *,
                                                        struct avnd_su_si_rec *,
                                                        struct avsv_susi_asgn *,
                                                        uint32_t *);
extern void avnd_su_pres_state_set(const struct avnd_cb_tag *cb, AVND_SU *su,
                                   SaAmfPresenceStateT newstate);

extern void avnd_silist_init(struct avnd_cb_tag *cb);
extern struct avnd_su_si_rec *avnd_silist_getfirst(void);
extern struct avnd_su_si_rec *avnd_silist_getnext(
    const struct avnd_su_si_rec *);
extern struct avnd_su_si_rec *avnd_silist_getprev(
    const struct avnd_su_si_rec *);
extern struct avnd_su_si_rec *avnd_silist_getlast(void);
extern bool sufailover_in_progress(const AVND_SU *su);
extern bool sufailover_during_nodeswitchover(const AVND_SU *su);
extern bool all_csis_removable_from_su(const AVND_SU *su);
extern void su_reset_restart_count_in_comps(const struct avnd_cb_tag *cb,
                                            const AVND_SU *su);
extern bool all_comps_terminated_in_su(const AVND_SU *su,
                                       bool all_final_pres_states = false);

void su_increment_su_restart_count(AVND_SU &su);
void su_increment_comp_restart_count(AVND_SU &su);
void set_suRestart_flag(AVND_SU *su);
void reset_suRestart_flag(AVND_SU *su);
bool su_all_comps_restartable(const AVND_SU &su);
void su_send_suRestart_recovery_msg(AVND_SU *su);
bool pi_su_all_comps_uninstantiated(const AVND_SU &su);
bool is_any_non_restartable_comp_assigned(const AVND_SU &su);
bool su_all_pi_comps_instantiated(const AVND_SU *su);
bool all_csis_in_assigned_state(const AVND_SU *su);
bool isAdminRestarted(const AVND_SU *su);
bool isFailed(const AVND_SU *su);
bool isRestartSet(const AVND_SU *su);
bool su_evaluate_restarting_state(AVND_SU *su);
bool all_csis_in_restarting_state(const AVND_SU *su,
                                  AVND_COMP_CSI_REC *exclude_csi = nullptr);
extern AVND_SU *avnd_sudb_rec_get(AmfDb<std::string, AVND_SU> &sudb,
                                  const std::string &name);
extern AVND_SU *avnd_sudb_rec_get_next(AmfDb<std::string, AVND_SU> &sudb,
                                       const std::string &name);
extern void sudb_rec_comp_add(AVND_SU *su, AVND_COMP *comp, uint32_t *rc);
uint32_t avnd_evt_avd_compcsi_evh(struct avnd_cb_tag *cb,
                                  struct avnd_evt_tag *evt);

#endif  // AMF_AMFND_AVND_SU_H_
