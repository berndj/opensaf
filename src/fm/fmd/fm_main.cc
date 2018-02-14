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
DESCRIPTION:

This file contains the main() routine for FM.

******************************************************************************/

#include <stdbool.h>
#include <stdlib.h>
#include "base/daemon.h"
#include "base/logtrace.h"
#include "base/osaf_extended_name.h"
#include "base/osaf_poll.h"
#include "base/osaf_time.h"
#include "fm/fmd/fm.h"
#include "nid/agent/nid_api.h"
#include "osaf/configmake.h"
#include "osaf/consensus/consensus.h"

#define FM_CLM_API_TIMEOUT 10000000000LL

static SaVersionT clm_version = {'B', 4, 1};
static const SaClmCallbacksT_4 clm_callbacks = {0, 0};

enum { FD_TERM = 0, FD_AMF = 1, FD_MBX };

FM_CB *fm_cb = NULL;
const char *role_string[] = {"UNDEFINED", "ACTIVE", "STANDBY", "QUIESCED",
                             "QUIESCING"};

/*****************************************************************
 *                                                               *
 *         Prototypes for static functions                       *
 *                                                               *
 *****************************************************************/

static uint32_t fm_agents_startup(void);
static uint32_t fm_get_args(FM_CB *);
static uint32_t fms_fms_exchange_node_info(FM_CB *);
static uint32_t fms_fms_inform_terminating(FM_CB *fm_cb);
static uint32_t fm_nid_notify(uint32_t);
static uint32_t fm_tmr_start(FM_TMR *, SaTimeT);
static SaAisErrorT get_peer_clm_node_name(NODE_ID);
static SaAisErrorT fm_clm_init();
static void fm_mbx_msg_handler(FM_CB *, FM_EVT *);
static void fm_evt_proc_rda_callback(FM_CB *, FM_EVT *);
static void fm_tmr_exp(void *);
void handle_mbx_event(void);
extern uint32_t fm_amf_init(FM_AMF_CB *fm_amf_cb);
void rda_cb(uint32_t cb_hdl, PCS_RDA_CB_INFO *cb_info,
            PCSRDA_RETURN_CODE error_code);
uint32_t gl_fm_hdl;
static NCS_SEL_OBJ usr1_sel_obj;

/**
 * USR1 signal is used when AMF wants instantiate us as a
 * component. Wake up the main thread so it can register with
 * AMF.
 *
 * @param i_sig_num
 */
static void sigusr1_handler(int sig) {
  (void)sig;
  signal(SIGUSR1, SIG_IGN);
  ncs_sel_obj_ind(&usr1_sel_obj);
}

/**
 * Callback from RDA. Post a message/event to the FM mailbox.
 * @param cb_hdl
 * @param cb_info
 * @param error_code
 */
void rda_cb(uint32_t cb_hdl, PCS_RDA_CB_INFO *cb_info,
            PCSRDA_RETURN_CODE error_code) {
  uint32_t rc;
  FM_EVT *evt = NULL;

  TRACE_ENTER();

  evt = static_cast<FM_EVT *>(calloc(1, sizeof(FM_EVT)));
  if (NULL == evt) {
    LOG_ER("calloc failed");
    goto done;
  }

  evt->evt_code = FM_EVT_RDA_ROLE;
  evt->info.rda_info.role = cb_info->info.io_role;

  rc = ncs_ipc_send(&fm_cb->mbx, (NCS_IPC_MSG *)evt, NCS_IPC_PRIORITY_HIGH);
  if (rc != NCSCC_RC_SUCCESS) {
    syslog(LOG_ERR, "IPC send failed %u", rc);
    free(evt);
  }

done:
  TRACE_LEAVE();
}

/*****************************************************************************

PROCEDURE NAME:       main

DESCRIPTION:          Main routine for FM

*****************************************************************************/
int main(int argc, char *argv[]) {
  NCS_SEL_OBJ mbx_sel_obj;
  nfds_t nfds = 3;
  struct pollfd fds[nfds];
  int ret = 0;
  int rc = NCSCC_RC_FAILURE;
  int term_fd;
  bool nid_started = false;
  char *control_tipc = NULL;

  opensaf_reboot_prepare();

  daemonize(argc, argv);

  /* Determine how this process was started, by NID or AMF */
  if (getenv("SA_AMF_COMPONENT_NAME") == NULL) nid_started = true;

  if (fm_agents_startup() != NCSCC_RC_SUCCESS) {
    goto fm_init_failed;
  }

  /* Allocate memory for FM_CB. */
  fm_cb = new FM_CB();

  fm_cb->fm_amf_cb.nid_started = nid_started;
  fm_cb->fm_amf_cb.amf_fd = -1;

  /* Variable to control whether FM will trigger failover immediately
   * upon recieving down event of critical services or will wait
   * for node_downevent. By default, OPENSAF_MANAGE_TIPC = yes,
   * If this variable is set to yes, OpenSAF will rmmod tipc which means
   * NODE_down will happen immediately and there is no need to act on
   * service down. If this variable is set to no, user will only do
   * /etc/init.d/opensafd stop and no rmmod is done, which means no
   * NODE_DOWN event will arrive, and therefore FM has to act on service
   * down events of critical service, In the short term, FM will use
   * service down events of AMFND, IMMD and IMMND to trigger failover.
   */
  fm_cb->control_tipc = true; /* Default behaviour */

  /* Create CB handle */
  gl_fm_hdl = ncshm_create_hdl(NCS_HM_POOL_ID_COMMON, NCS_SERVICE_ID_GFM,
                               (NCSCONTEXT)fm_cb);

  /* Take CB handle */
  ncshm_take_hdl(NCS_SERVICE_ID_GFM, gl_fm_hdl);

  if (fm_get_args(fm_cb) != NCSCC_RC_SUCCESS) {
    /* notify the NID */
    syslog(LOG_ERR, "fm_get args failed.");
    goto fm_init_failed;
  }

  /* Create MBX. */
  if (m_NCS_IPC_CREATE(&fm_cb->mbx) != NCSCC_RC_SUCCESS) {
    syslog(LOG_ERR, "m_NCS_IPC_CREATE() failed.");
    goto fm_init_failed;
  }

  /* Attach MBX */
  if (m_NCS_IPC_ATTACH(&fm_cb->mbx) != NCSCC_RC_SUCCESS) {
    syslog(LOG_ERR, "m_NCS_IPC_ATTACH() failed.");
    goto fm_init_failed;
  }

  if (fm_rda_init(fm_cb) != NCSCC_RC_SUCCESS) {
    goto fm_init_failed;
  }
  if (fm_cb->role == PCS_RDA_ACTIVE &&
      fm_cb->activation_supervision_tmr_val != 0) {
    LOG_NO("Starting initial activation supervision: %" PRIu64 "ms",
           10 * (uint64_t)fm_cb->activation_supervision_tmr_val);
    fm_tmr_start(&fm_cb->activation_supervision_tmr,
                 fm_cb->activation_supervision_tmr_val);
  }
  if ((control_tipc = getenv("OPENSAF_MANAGE_TIPC")) == NULL)
    fm_cb->control_tipc = false;
  else if (strncmp(control_tipc, "yes", 3) == 0)
    fm_cb->control_tipc = true;
  else
    fm_cb->control_tipc = false;

  if (nid_started &&
      (rc = ncs_sel_obj_create(&usr1_sel_obj)) != NCSCC_RC_SUCCESS) {
    LOG_ER("ncs_sel_obj_create FAILED");
    goto fm_init_failed;
  }

  if (nid_started && signal(SIGUSR1, sigusr1_handler) == SIG_ERR) {
    LOG_ER("signal USR1 FAILED: %s", strerror(errno));
    goto fm_init_failed;
  }

  if (!nid_started && fm_amf_init(&fm_cb->fm_amf_cb) != NCSCC_RC_SUCCESS)
    goto fm_init_failed;

  if ((rc = initialize_for_assignment(fm_cb, (SaAmfHAStateT)fm_cb->role)) !=
      NCSCC_RC_SUCCESS) {
    LOG_ER("initialize_for_assignment FAILED %u", (unsigned)rc);
    goto fm_init_failed;
  }

  /* Get mailbox selection object */
  mbx_sel_obj = m_NCS_IPC_GET_SEL_OBJ(&fm_cb->mbx);

  /* Give CB hdl */
  ncshm_give_hdl(gl_fm_hdl);

  daemon_sigterm_install(&term_fd);

  fds[FD_TERM].fd = term_fd;
  fds[FD_TERM].events = POLLIN;

  /* USR1/AMF fd */
  fds[FD_AMF].fd = nid_started ? usr1_sel_obj.rmv_obj : fm_cb->fm_amf_cb.amf_fd;
  fds[FD_AMF].events = POLLIN;

  /* Mailbox */
  fds[FD_MBX].fd = mbx_sel_obj.rmv_obj;
  fds[FD_MBX].events = POLLIN;

  /* notify the NID */
  if (nid_started) fm_nid_notify((uint32_t)NCSCC_RC_SUCCESS);

  while (1) {
    ret = poll(fds, nfds, -1);

    if (ret == -1) {
      if (errno == EINTR) continue;
      LOG_ER("poll failed - %s", strerror(errno));
      break;
    }

    if (fds[FD_TERM].revents & POLLIN) {
      fms_fms_inform_terminating(fm_cb);
      daemon_exit();
    }

    if (fds[FD_AMF].revents & POLLIN) {
      if (fm_cb->fm_amf_cb.amf_hdl != 0) {
        SaAisErrorT error;
        TRACE("AMF event rec");
        if ((error = saAmfDispatch(fm_cb->fm_amf_cb.amf_hdl,
                                   SA_DISPATCH_ALL)) != SA_AIS_OK) {
          LOG_ER("saAmfDispatch failed: %u", error);
          goto done;
        }
      } else {
        TRACE("SIGUSR1 event rec");
        ncs_sel_obj_destroy(&usr1_sel_obj);
        if (fm_amf_init(&fm_cb->fm_amf_cb) != NCSCC_RC_SUCCESS) goto done;
        fds[FD_AMF].fd = fm_cb->fm_amf_cb.amf_fd;
      }
    }

    if (fds[FD_MBX].revents & POLLIN) handle_mbx_event();
  }

fm_init_failed:
  if (nid_started) fm_nid_notify((uint32_t)NCSCC_RC_FAILURE);
done:
  syslog(LOG_ERR, "Exiting...");
  exit(1);
}

uint32_t initialize_for_assignment(FM_CB *cb, SaAmfHAStateT ha_state) {
  TRACE_ENTER2("ha_state = %d", (int)ha_state);
  uint32_t rc = NCSCC_RC_SUCCESS;
  if (cb->fully_initialized || ha_state == SA_AMF_HA_QUIESCED) goto done;
  cb->role = (PCS_RDA_ROLE)ha_state;
  if ((rc = fm_mds_init(cb)) != NCSCC_RC_SUCCESS) {
    LOG_ER("immd_mds_register FAILED %d", rc);
    goto done;
  }

  cb->clm_hdl = 0;
  cb->fully_initialized = true;
done:
  TRACE_LEAVE2("rc = %u", rc);
  return rc;
}

/****************************************************************************
 * Name          : handle_mbx_event
 *
 * Description   : Processes the mail box message.
 *
 * Arguments     :  void.
 *
 * Return Values : void.
 *
 * Notes         : None.
 *****************************************************************************/
void handle_mbx_event(void) {
  FM_EVT *fm_mbx_evt = NULL;
  TRACE_ENTER();
  ncshm_take_hdl(NCS_SERVICE_ID_GFM, gl_fm_hdl);
  fm_mbx_evt = (FM_EVT *)m_NCS_IPC_NON_BLK_RECEIVE(&fm_cb->mbx, msg);
  if (fm_mbx_evt) fm_mbx_msg_handler(fm_cb, fm_mbx_evt);

  ncshm_give_hdl(gl_fm_hdl);
  TRACE_LEAVE();
}

/****************************************************************************
 * Name          : fm_agents_startup
 *
 * Description   : Starts NCS agents.
 *
 * Arguments     :  None.
 *
 * Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE.
 *
 * Notes         : None.
 *****************************************************************************/
static uint32_t fm_agents_startup(void) {
  uint32_t rc = NCSCC_RC_SUCCESS;
  TRACE_ENTER();
  /* Start agents */
  rc = ncs_agents_startup();
  if (rc != NCSCC_RC_SUCCESS) {
    syslog(LOG_ERR, "ncs_agents_startup failed");
    return rc;
  }

  return rc;
}

/****************************************************************************
 * Name          : fm_get_args
 *
 * Description   :  Parses configuration and store the values in Data str.
 *
 * Arguments     : Pointer to Control block
 *
 * Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE.
 *
 * Notes         : None.
 *****************************************************************************/
static uint32_t fm_get_args(FM_CB *fm_cb) {
  char *use_remote_fencing = NULL;
  char *value;
  TRACE_ENTER();

  fm_cb->use_remote_fencing = false;
  use_remote_fencing = getenv("FMS_USE_REMOTE_FENCING");
  if (use_remote_fencing != NULL) {
    int use_fencing = strtol(use_remote_fencing, NULL, 10);
    if (use_fencing == 1) {
      fm_cb->use_remote_fencing = true;
      LOG_NO("Remote fencing is enabled");
    } else {
      LOG_NO("Remote fencing is disabled");
    }
  }

  value = getenv("EE_ID");
  if (value != NULL) {
    fm_cb->node_name.length = strlen(value);
    memcpy(fm_cb->node_name.value, value, fm_cb->node_name.length);
    LOG_NO("EE_ID : %s", fm_cb->node_name.value);
  } else {
    fm_cb->node_name.length = 0;
  }

  /* Update fm_cb configuration fields */
  fm_cb->node_id = m_NCS_GET_NODE_ID;

  fm_cb->active_promote_tmr_val = atoi(getenv("FMS_PROMOTE_ACTIVE_TIMER"));
  char *activation_supervision_tmr_val =
      getenv("FMS_ACTIVATION_SUPERVISION_TIMER");
  if (activation_supervision_tmr_val != NULL) {
    fm_cb->activation_supervision_tmr_val =
        atoi(activation_supervision_tmr_val);
  } else {
    fm_cb->activation_supervision_tmr_val = 30000;
  }

  /* Set timer variables */
  fm_cb->promote_active_tmr.type = FM_TMR_PROMOTE_ACTIVE;
  fm_cb->activation_supervision_tmr.type = FM_TMR_ACTIVATION_SUPERVISION;

  char *node_isolation_timeout = getenv("FMS_NODE_ISOLATION_TIMEOUT");
  if (node_isolation_timeout != NULL) {
    osaf_millis_to_timespec(atoi(node_isolation_timeout),
                            &fm_cb->node_isolation_timeout);
  } else {
    fm_cb->node_isolation_timeout.tv_sec = 10;
    fm_cb->node_isolation_timeout.tv_nsec = 0;
  }
  TRACE("NODE_ISOLATION_TIMEOUT = %" PRId64 ".%09ld",
        (int64_t)fm_cb->node_isolation_timeout.tv_sec,
        fm_cb->node_isolation_timeout.tv_nsec);

  TRACE_LEAVE();
  return NCSCC_RC_SUCCESS;
}

/****************************************************************************
 * Name          : fm_clm_init
 *
 * Description   : Initialize CLM.
 *
 * Arguments     : None.
 *
 * Return Values : None.
 *
 * Notes         : None.
 *****************************************************************************/
static SaAisErrorT get_peer_clm_node_name(NODE_ID node_id) {
  SaAisErrorT rc = SA_AIS_OK;
  SaClmClusterNodeT_4 cluster_node;

  if ((rc = fm_clm_init()) != SA_AIS_OK) {
    LOG_ER("clm init FAILED %d", rc);
  } else {
    LOG_NO("clm init OK");
  }

  if ((rc = saClmClusterNodeGet_4(fm_cb->clm_hdl, node_id, FM_CLM_API_TIMEOUT,
                                  &cluster_node)) == SA_AIS_OK) {
    // Extract peer clm node name, e.g SC-2 from
    // "safNode=SC-2,safCluster=myClmCluster" The peer clm node name
    // will be passed to opensaf_reboot script to support remote
    // fencing. The peer clm node name should correspond to the name
    // of the virtual machine for that node.
    char *node = NULL;
    strtok((char *)cluster_node.nodeName.value, "=");
    node = strtok(NULL, ",");
    strncpy((char *)fm_cb->peer_clm_node_name.value, node,
            cluster_node.nodeName.length);
    LOG_NO("Peer clm node name: %s", fm_cb->peer_clm_node_name.value);
  } else {
    LOG_WA("saClmClusterNodeGet_4 returned %u", (unsigned)rc);
  }

  if ((rc = saClmFinalize(fm_cb->clm_hdl)) != SA_AIS_OK) {
    LOG_ER("clm finalize FAILED %d", rc);
  }

  return rc;
}

/****************************************************************************
 * Name          : fm_clm_init
 *
 * Description   : Initialize CLM.
 *
 * Arguments     : None.
 *
 * Return Values : None.
 *
 * Notes         : None.
 *****************************************************************************/
static SaAisErrorT fm_clm_init() {
  SaAisErrorT rc = SA_AIS_OK;

  for (;;) {
    rc = saClmInitialize_4(&fm_cb->clm_hdl, &clm_callbacks, &clm_version);
    if (rc == SA_AIS_ERR_TRY_AGAIN || rc == SA_AIS_ERR_TIMEOUT ||
        rc == SA_AIS_ERR_UNAVAILABLE) {
      LOG_WA("saClmInitialize_4 returned %u", (unsigned)rc);

      if (rc != SA_AIS_ERR_TRY_AGAIN) {
        LOG_WA("saClmInitialize_4 returned %u", (unsigned)rc);
      }
      osaf_nanosleep(&kHundredMilliseconds);
      continue;
    }
    if (rc == SA_AIS_OK) break;
    LOG_ER("Failed to Initialize with CLM: %u", rc);
    goto done;
  }
done:
  return rc;
}

/****************************************************************************
 * Name          : fm_mbx_msg_handler
 *
 * Description   : Processes Mail box messages between FM.
 *
 * Arguments     :  Pointer to Control block and Mail box event
 *
 * Return Values : None.
 *
 * Notes         : None.
 *****************************************************************************/
static void fm_mbx_msg_handler(FM_CB *fm_cb, FM_EVT *fm_mbx_evt) {
  TRACE_ENTER();
  switch (fm_mbx_evt->evt_code) {
    case FM_EVT_NODE_DOWN: {
      Consensus consensus_service;
      LOG_NO("Current role: %s", role_string[fm_cb->role]);
      if ((fm_mbx_evt->node_id == fm_cb->peer_node_id)) {
        /* Check whether node(AMF) initialization is done */
        if (fm_cb->csi_assigned == false) {
          opensaf_reboot(0, NULL,
                         "Failover occurred, but this node is not yet ready");
        }
        /* Start Promote active timer */
        if ((fm_cb->role == PCS_RDA_STANDBY) &&
            (fm_cb->active_promote_tmr_val != 0)) {
          fm_tmr_start(&fm_cb->promote_active_tmr,
                       fm_cb->active_promote_tmr_val);
          LOG_NO("Promote active timer started");
        } else {
          TRACE("rda role: %s, amf_state: %u", role_string[fm_cb->role],
                fm_cb->amf_state);
          /* The local node is Standby, Quiesced or
           *Active. Reboot the peer node. Note: If local
           *node is Active, there are two
           *interpretations. - Normal scenario where the
           *Standby went down - Standby went down in the
           *middle of a swtichover and AMF has
           *	  transitioned CSI state, but not the
           *RDA state. In both the cases, this node should
           *be set to ACTIVE.
           */
          if ((SaAmfHAStateT)fm_cb->role != fm_cb->amf_state)
            TRACE("Failover processing trigerred in the middle of role change");
          /* The previous log message in the above has
           * been changed because the log "Failover
           * occurred in the middle of switchover" can be
           * misleading in the case when it is not a
           * switchover but just that failover has been
           * trigerred quicker than the node_down event
           * has been received.
           */
          if (fm_cb->role == PCS_RDA_STANDBY) {
            const std::string current_active =
                consensus_service.CurrentActive();
            if (current_active.compare(osaf_extended_name_borrow(
                    &fm_cb->peer_clm_node_name)) == 0) {
              // update consensus service, before fencing old active controller
              consensus_service.DemoteCurrentActive();
            }
          }

          if (fm_cb->use_remote_fencing) {
            if (fm_cb->peer_node_terminated == false) {
              // if peer_sc_up is true then
              // the node has come up already
              if (fm_cb->peer_sc_up == false && fm_cb->immnd_down == true) {
                opensaf_reboot(fm_cb->peer_node_id,
                               (char *)fm_cb->peer_clm_node_name.value,
                               "Received Node Down for peer controller");
              }
            } else {
              LOG_NO(
                  "Peer node %s is terminated, fencing will not be performed",
                  fm_cb->peer_clm_node_name.value);
            }
          } else {
            std::string peer_node_name;
            fm_cb->mutex_.Lock();
            peer_node_name = fm_cb->peer_node_name;
            fm_cb->mutex_.Unlock();
            opensaf_reboot(fm_cb->peer_node_id,
                           peer_node_name.c_str(),
                           "Received Node Down for peer controller");
          }
          if (!((fm_cb->role == PCS_RDA_ACTIVE) &&
                (fm_cb->amf_state == (SaAmfHAStateT)PCS_RDA_ACTIVE))) {
            fm_cb->role = PCS_RDA_ACTIVE;
            LOG_NO("Controller Failover: Setting role to ACTIVE");
            fm_rda_set_role(fm_cb, PCS_RDA_ACTIVE);
          }
        }
      }
    } break;

    case FM_EVT_PEER_UP:
      /* Weird situation in a cluster, where the new-Active controller
       * node founds the peer node (old-Active) is still in the
       * progress of shutdown (i.e., amfd/immd is still alive).
       */
      if ((fm_cb->role == PCS_RDA_ACTIVE) && (fm_cb->csi_assigned == false)) {
        LOG_WA(
            "Two active controllers observed in a cluster, newActive: %x and "
            "old-Active: %x",
            unsigned(fm_cb->node_id), unsigned(fm_cb->peer_node_id));
        opensaf_reboot(0, NULL,
                       "Received svc up from peer node (old-active is not "
                       "fully DOWN), hence rebooting the new Active");
      }

      /* Peer fm came up so sending ee_id of this node */
      if (fm_cb->node_name.length != 0) fms_fms_exchange_node_info(fm_cb);

      if (fm_cb->use_remote_fencing) {
        get_peer_clm_node_name(fm_mbx_evt->node_id);
      }
      break;

    case FM_EVT_TMR_EXP:
      /* Timer Expiry event posted */
      if (fm_mbx_evt->info.fm_tmr->type == FM_TMR_PROMOTE_ACTIVE) {
        /* Check whether node(AMF) initialization is done */
        if (fm_cb->csi_assigned == false) {
          opensaf_reboot(0, NULL,
                         "Failover occurred, but this node is not yet ready");
        }

        Consensus consensus_service;
        const std::string current_active = consensus_service.CurrentActive();
        if (current_active.compare(
                osaf_extended_name_borrow(&fm_cb->peer_clm_node_name)) == 0) {
          // update consensus service, before fencing old active controller
          consensus_service.DemoteCurrentActive();
        }

        /* Now. Try resetting other blade */
        fm_cb->role = PCS_RDA_ACTIVE;

        LOG_NO("Reseting peer controller node id: %x",
               unsigned(fm_cb->peer_node_id));
        if (fm_cb->use_remote_fencing) {
          if (fm_cb->peer_node_terminated == false) {
            opensaf_reboot(fm_cb->peer_node_id,
                           (char *)fm_cb->peer_clm_node_name.value,
                           "Received Node Down for peer controller");
          } else {
            LOG_NO("Peer node %s is terminated, fencing will not be performed",
                   fm_cb->peer_clm_node_name.value);
          }
        } else {
          std::string peer_node_name;
          fm_cb->mutex_.Lock();
          peer_node_name = fm_cb->peer_node_name;
          fm_cb->mutex_.Unlock();
          opensaf_reboot(fm_cb->peer_node_id,
                         peer_node_name.c_str(),
                         "Received Node Down for Active peer");
        }
        fm_rda_set_role(fm_cb, PCS_RDA_ACTIVE);
      } else if (fm_mbx_evt->info.fm_tmr->type ==
                 FM_TMR_ACTIVATION_SUPERVISION) {
        opensaf_reboot(0, NULL,
                       "Activation timer supervision "
                       "expired: no ACTIVE assignment received "
                       "within the time limit");
      }
      break;

    case FM_EVT_RDA_ROLE:
      fm_evt_proc_rda_callback(fm_cb, fm_mbx_evt);
      break;

    default:
      break;
  }

  /* Free the event. */
  if (fm_mbx_evt != NULL) {
    m_MMGR_FREE_FM_EVT(fm_mbx_evt);
    fm_mbx_evt = NULL;
  }
  TRACE_LEAVE();
  return;
}

static void fm_evt_proc_rda_callback(FM_CB *cb, FM_EVT *evt) {
  uint32_t rc = NCSCC_RC_SUCCESS;

  TRACE_ENTER2("%d", (int)evt->info.rda_info.role);
  if (evt->info.rda_info.role != PCS_RDA_ACTIVE &&
      cb->activation_supervision_tmr.status == FM_TMR_RUNNING) {
    fm_tmr_stop(&cb->activation_supervision_tmr);
    LOG_NO("Stopped activation supervision due to new role %u",
           (unsigned)evt->info.rda_info.role);
  }
  if (evt->info.rda_info.role == PCS_RDA_ACTIVE && cb->role != PCS_RDA_ACTIVE &&
      cb->amf_state != SA_AMF_HA_ACTIVE &&
      cb->activation_supervision_tmr_val != 0 &&
      cb->activation_supervision_tmr.status != FM_TMR_RUNNING) {
    LOG_NO("Starting activation supervision: %" PRIu64 "ms",
           10 * (uint64_t)cb->activation_supervision_tmr_val);
    fm_tmr_start(&cb->activation_supervision_tmr,
                 cb->activation_supervision_tmr_val);
  }
  if ((rc = initialize_for_assignment(
           cb, (SaAmfHAStateT)evt->info.rda_info.role)) != NCSCC_RC_SUCCESS) {
    LOG_ER("initialize_for_assignment FAILED %u", (unsigned)rc);
    opensaf_reboot(0, NULL, "FM service initialization failed");
  }
  cb->role = evt->info.rda_info.role;
  syslog(LOG_INFO, "RDA role for this controller node: %s",
         role_string[cb->role]);
  TRACE_LEAVE();
}

/****************************************************************************
 * Name          : fm_tmr_start
 *
 * Description   : Starts timer with the given period
 *
 * Arguments     :  Pointer to TImer Data Str
 *
 * Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE.
 *
 * Notes         : None.
 *****************************************************************************/
uint32_t fm_tmr_start(FM_TMR *tmr, SaTimeT period) {
  uint32_t tmr_period;
  TRACE_ENTER();
  tmr_period = (uint32_t)period;

  if (tmr->tmr_id == NULL) {
    tmr->tmr_id = ncs_tmr_alloc(const_cast<char *>(__FILE__), __LINE__);
  }

  if (tmr->status == FM_TMR_RUNNING) {
    return NCSCC_RC_FAILURE;
  }

  tmr->tmr_id = ncs_tmr_start(tmr->tmr_id, tmr_period, fm_tmr_exp, tmr,
                              const_cast<char *>(__FILE__), __LINE__);
  tmr->status = FM_TMR_RUNNING;

  if (TMR_T_NULL == tmr->tmr_id) {
    return NCSCC_RC_FAILURE;
  }
  TRACE_LEAVE();
  return NCSCC_RC_SUCCESS;
}

void fm_tmr_stop(FM_TMR *tmr) {
  TRACE_ENTER();
  if (tmr->tmr_id != NULL) {
    if (tmr->status == FM_TMR_RUNNING) {
      m_NCS_TMR_STOP(tmr->tmr_id);
    }
    m_NCS_TMR_DESTROY(tmr->tmr_id);
    tmr->tmr_id = NULL;
  }
  tmr->status = FM_TMR_STOPPED;
  TRACE_LEAVE();
  return;
}

/****************************************************************************
 * Name          : fm_tmr_exp
 *
 * Description   : Timer Expiry function
 *
 * Arguments     : Pointer to timer data structure
 *
 * Return Values : None.
 *
 * Notes         : None.
 *****************************************************************************/
void fm_tmr_exp(void *fm_tmr) {
  FM_CB *fm_cb = NULL;
  FM_TMR *tmr = (FM_TMR *)fm_tmr;
  FM_EVT *evt = NULL;

  TRACE_ENTER();
  if (tmr == NULL) {
    return;
  }

  /* Take handle */
  fm_cb = static_cast<FM_CB *>(ncshm_take_hdl(NCS_SERVICE_ID_GFM, gl_fm_hdl));

  if (fm_cb == NULL) {
    syslog(LOG_ERR, "Taking handle failed in timer expiry ");
    return;
  }

  if (FM_TMR_STOPPED == tmr->status) {
    return;
  }
  tmr->status = FM_TMR_STOPPED;

  /* Create & send the timer event to the FM MBX. */
  evt = m_MMGR_ALLOC_FM_EVT;

  if (evt != NULL) {
    memset(evt, '\0', sizeof(FM_EVT));
    evt->evt_code = FM_EVT_TMR_EXP;
    evt->info.fm_tmr = tmr;

    if (m_NCS_IPC_SEND(&fm_cb->mbx, evt, NCS_IPC_PRIORITY_HIGH) !=
        NCSCC_RC_SUCCESS) {
      syslog(LOG_ERR, "IPC send failed in timer expiry ");
      m_MMGR_FREE_FM_EVT(evt);
    }
  }

  /* Give handle */
  ncshm_give_hdl(gl_fm_hdl);
  fm_cb = NULL;
  TRACE_LEAVE();
  return;
}

/****************************************************************************
 * Name          : fms_fms_exchange_node_info
 *
 * Description   : sends ee_id to peer .
 *
 * Arguments     : Pointer to Control Block.
 *
 * Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE.
 *
 * Notes         : None.
 *****************************************************************************/
static uint32_t fms_fms_exchange_node_info(FM_CB *fm_cb) {
  GFM_GFM_MSG gfm_msg;
  TRACE_ENTER();
  if (fm_cb->peer_adest != 0) {
    /* peer fms present */
    memset(&gfm_msg, 0, sizeof(GFM_GFM_MSG));
    gfm_msg.msg_type = GFM_GFM_EVT_NODE_INFO_EXCHANGE;
    gfm_msg.info.node_info.node_id = fm_cb->node_id;
    gfm_msg.info.node_info.node_name.length = fm_cb->node_name.length;
    memcpy(gfm_msg.info.node_info.node_name.value, fm_cb->node_name.value,
           fm_cb->node_name.length);

    if (NCSCC_RC_SUCCESS !=
        fm_mds_async_send(fm_cb, (NCSCONTEXT)&gfm_msg, NCSMDS_SVC_ID_GFM,
                          MDS_SEND_PRIORITY_MEDIUM, MDS_SENDTYPE_SND,
                          fm_cb->peer_adest, NCSMDS_SCOPE_NONE)) {
      syslog(LOG_ERR, "Sending node-info message to peer fms failed");
      return NCSCC_RC_FAILURE;
    }

    return NCSCC_RC_SUCCESS;
  }
  TRACE_LEAVE();
  return NCSCC_RC_FAILURE;
}

/****************************************************************************
 * Name          : fms_fms_inform_terminating
 *
 * Description   : sends information to peer that terminating is undergoing.
 *
 * Arguments     : Pointer to Control Block.
 *
 * Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE.
 *
 * Notes         : None.
 *****************************************************************************/
static uint32_t fms_fms_inform_terminating(FM_CB *fm_cb) {
  GFM_GFM_MSG gfm_msg;
  TRACE_ENTER();
  if (fm_cb->peer_adest != 0) {
    /* peer fms present */
    memset(&gfm_msg, 0, sizeof(GFM_GFM_MSG));
    gfm_msg.msg_type = GFM_GFM_EVT_PEER_IS_TERMINATING;

    if (NCSCC_RC_SUCCESS !=
        fm_mds_async_send(fm_cb, (NCSCONTEXT)&gfm_msg, NCSMDS_SVC_ID_GFM,
                          MDS_SEND_PRIORITY_VERY_HIGH, MDS_SENDTYPE_SND,
                          fm_cb->peer_adest, NCSMDS_SCOPE_NONE)) {
      syslog(LOG_ERR, "Sending node-info message to peer fms failed");
      return NCSCC_RC_FAILURE;
    }

    return NCSCC_RC_SUCCESS;
  }
  TRACE_LEAVE();
  return NCSCC_RC_FAILURE;
}

/****************************************************************************
 * Name          : fm_nid_notify
 *
 * Description   : Sends notification to NID
 *
 * Arguments     :  Error Type
 *
 * Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE.
 *
 * Notes         : None.
 *****************************************************************************/
static uint32_t fm_nid_notify(uint32_t nid_err) {
  uint32_t error;
  uint32_t nid_stat_code;

  TRACE_ENTER();
  if (nid_err > NCSCC_RC_SUCCESS) nid_err = NCSCC_RC_FAILURE;

  nid_stat_code = nid_err;
  TRACE_LEAVE();
  return nid_notify("HLFM", nid_stat_code, &error);
}
