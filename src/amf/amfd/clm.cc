/*      -*- OpenSAF  -*-
 *
 * (C) Copyright 2010 The OpenSAF Foundation
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

#include <saAis.h>
#include "base/logtrace.h"
#include "amf/amfd/cb.h"
#include "amf/amfd/amfd.h"
#include "amf/amfd/clm.h"
#include "amf/amfd/node.h"
#include "base/osaf_time.h"

static void clm_node_join_complete(AVD_AVND *node) {
  TRACE_ENTER();
  /* Enable the node in any case. */
  avd_node_oper_state_set(node, SA_AMF_OPERATIONAL_ENABLED);

  /* For each of the SUs calculate the readiness state.
   ** call the SG FSM with the new readiness state.
   */

  if (node->saAmfNodeAdminState == SA_AMF_ADMIN_LOCKED_INSTANTIATION) {
    LOG_NO("Amf Node is in locked-inst state, amf admin state is %u",
           node->saAmfNodeAdminState);
    goto done;
  }

  for (const auto &su : node->list_of_su) {
    /* For non-preinstantiable SU unlock-inst will not lead to its inst until
     * unlock. */
    if (su->saAmfSUPreInstantiable == false) {
      /* Skip the instantiation. */
    } else {
      if ((node->node_state == AVD_AVND_STATE_PRESENT) ||
          (node->node_state == AVD_AVND_STATE_NO_CONFIG) ||
          (node->node_state == AVD_AVND_STATE_NCS_INIT)) {
        if ((su->sg_of_su->saAmfSGAdminState !=
             SA_AMF_ADMIN_LOCKED_INSTANTIATION) &&
            (su->saAmfSUOperState == SA_AMF_OPERATIONAL_ENABLED) &&
            (su->saAmfSUAdminState != SA_AMF_ADMIN_LOCKED_INSTANTIATION) &&
            (su->saAmfSUPresenceState == SA_AMF_PRESENCE_UNINSTANTIATED)) {
          /* When the SU will instantiate then prescence state change message
             will come and so store the callback parameters to send response
             later on. */
          if (su->sg_of_su->pref_inservice_sus() >
              (sg_instantiated_su_count(su->sg_of_su) +
               su->sg_of_su->try_inst_counter)) {
            if (avd_snd_presence_msg(avd_cb, su, false) == NCSCC_RC_SUCCESS) {
              su->set_term_state(false);
              su->sg_of_su->try_inst_counter++;
            } else {
              LOG_ER(
                  "Internal error, could not send SU'%s' pres message to avnd '%x'",
                  su->name.c_str(), su->su_on_node->node_info.nodeId);
            }
          }
        }
      }
    }
  }

  node_reset_su_try_inst_counter(node);

done:
  TRACE_LEAVE();
}

/* validating this node for a graceful exit */
static void clm_node_exit_validate(AVD_AVND *node) {
  AVD_SU_SI_REL *susi;
  bool reject = false;
  SaAisErrorT rc = SA_AIS_OK;

  TRACE_ENTER();

  /*
   * Reject validate step on self node as this is active controller
   */
  if (node->node_info.nodeId == avd_cb->node_id_avd) {
    reject = true;
    LOG_NO("Validate Step on Active Controller %d", avd_cb->node_id_avd);
    goto done;
  }

  /* now go through each SU to determine whether
   any SI assigned becomes unassigned due to node exit*/
  for (const auto &su : node->list_of_su) {
    susi = su->list_of_susi;
    /* now evalutate each SI that is assigned to this SU */
    while (susi != nullptr) {
      if ((susi->state == SA_AMF_HA_ACTIVE) &&
          (susi->si->saAmfSINumCurrActiveAssignments == 1) &&
          (susi->si->saAmfSINumCurrStandbyAssignments == 0) &&
          (su->sg_of_su->sg_redundancy_model != SA_AMF_NO_REDUNDANCY_MODEL)) {
        /* there is only one active assignment w.r.t this SUSI */
        reject = true;
        goto done;
      }
      susi = susi->su_next;
    }
  }

done:
  if (reject == false) {
    rc = saClmResponse_4(avd_cb->clmHandle, node->clm_pend_inv,
                         SA_CLM_CALLBACK_RESPONSE_OK);
    LOG_NO("CLM track callback VALIDATE::ACCEPTED %u", rc);
  } else {
    rc = saClmResponse_4(avd_cb->clmHandle, node->clm_pend_inv,
                         SA_CLM_CALLBACK_RESPONSE_REJECTED);
    LOG_NO("CLM track callback VALIDATE::REJECTED %u", rc);
  }

  node->clm_pend_inv = 0;
  TRACE_LEAVE();
}

/* perform required operations on this node for a graceful exit */
static void clm_node_exit_start(AVD_AVND *node, SaClmClusterChangesT change) {
  TRACE_ENTER();

  if (node->saAmfNodeOperState == SA_AMF_OPERATIONAL_DISABLED) {
    saClmResponse_4(avd_cb->clmHandle, node->clm_pend_inv,
                    SA_CLM_CALLBACK_RESPONSE_OK);
    node->clm_pend_inv = 0;
    goto done;
  }

  /* According to AMF B.04.01 spec: Section 3.2.6.2:
     "If a node is enabled and not in the locked-instantiation administrative
     state when it leaves the cluster membership, the node becomes disabled
     while it is out of the cluster and becomes enabled again when it rejoins
     the cluster."
   */
  if ((node->saAmfNodeOperState == SA_AMF_OPERATIONAL_ENABLED) &&
      (node->saAmfNodeAdminState != SA_AMF_ADMIN_LOCKED_INSTANTIATION))
    avd_node_oper_state_set(node, SA_AMF_OPERATIONAL_DISABLED);

  if (node->saAmfNodeAdminState != SA_AMF_ADMIN_UNLOCKED) {
    LOG_NO("Amf Node is not in unlocked state, amf admin state is '%u'",
           node->saAmfNodeAdminState);
    if (node->saAmfNodeAdminState == SA_AMF_ADMIN_LOCKED) {
      clm_node_terminate(node);
      goto done;
    }
    if (node->saAmfNodeAdminState == SA_AMF_ADMIN_LOCKED_INSTANTIATION) {
      saClmResponse_4(avd_cb->clmHandle, node->clm_pend_inv,
                      SA_CLM_CALLBACK_RESPONSE_OK);
      node->clm_pend_inv = 0;
      goto done;
    }
  }

  if (change == SA_CLM_NODE_SHUTDOWN) {
    /* call with nullptr invocation to differentiate it with AMF node shutdown.
     */
    avd_node_admin_lock_unlock_shutdown(node, 0, SA_AMF_ADMIN_SHUTDOWN);
  } else { /* SA_CLM_NODE_LEFT case */
    avd_node_admin_lock_unlock_shutdown(node, 0, SA_AMF_ADMIN_LOCK);
  }

  if (node->su_cnt_admin_oper == 0 && node->clm_pend_inv != 0) {
    clm_node_terminate(node);
  }
/* else wait for the su's on this node that are undergoing the required
 * operation */
done:
  TRACE_LEAVE();
}

static void clm_node_exit_complete(SaClmNodeIdT nodeId) {
  AVD_AVND *node = avd_node_find_nodeid(nodeId);

  TRACE_ENTER2("%x", nodeId);

  if (node == nullptr) {
    LOG_IN("Node %x left but is not an AMF cluster member", nodeId);
    goto done;
  }

  node->node_info.member = SA_FALSE;

  if (((node->node_state == AVD_AVND_STATE_ABSENT) ||
       (node->node_state == AVD_AVND_STATE_GO_DOWN) ||
       (AVD_AVND_STATE_SHUTTING_DOWN == node->node_state)) &&
      (nodeId == avd_cb->node_id_avd)) {
    TRACE("Invalid node state %u", node->node_state);
    goto done;
  }

  avd_node_failover(node);
  avd_node_delete_nodeid(node);
  m_AVSV_SEND_CKPT_UPDT_ASYNC_UPDT(avd_cb, node, AVSV_CKPT_AVD_NODE_CONFIG);
  node->clm_change_start_preceded = false;

done:
  TRACE_LEAVE();
}

static void clm_track_cb(
    const SaClmClusterNotificationBufferT_4 *notificationBuffer,
    SaUint32T numberOfMembers, SaInvocationT invocation,
    const SaNameT *rootCauseEntity, const SaNtfCorrelationIdsT *correlationIds,
    SaClmChangeStepT step, SaTimeT timeSupervision, SaAisErrorT error) {
  unsigned int i;
  SaClmClusterNotificationT_4 *notifItem;
  AVD_AVND *node;

  TRACE_ENTER2("'%llu' '%u' '%u'", invocation, step, error);

  if (error != SA_AIS_OK) {
    LOG_ER("ClmTrackCallback received in error");
    goto done;
  }
  if (avd_cb->avail_state_avd != SA_AMF_HA_ACTIVE) {
    LOG_WA("Receive clm track cb with AMFD state(%d)", avd_cb->avail_state_avd);
    saClmResponse_4(avd_cb->clmHandle, invocation, SA_CLM_CALLBACK_RESPONSE_OK);
    goto done;
  }
  /*
  ** The CLM cluster can be larger than the AMF cluster thus it is not an
  ** error if the corresponding AMF node cannot be found.
  */
  TRACE("numberOfMembers:'%u', numberOfItems:'%u'", numberOfMembers,
        notificationBuffer->numberOfItems);
  for (i = 0; i < notificationBuffer->numberOfItems; i++) {
    notifItem = &notificationBuffer->notification[i];
    const std::string node_name(
        Amf::to_string(&notifItem->clusterNode.nodeName));
    TRACE("i = %u, node:'%s', clusterChange:%u", i, node_name.c_str(),
          notifItem->clusterChange);
    switch (step) {
      case SA_CLM_CHANGE_VALIDATE:
        if (notifItem->clusterChange == SA_CLM_NODE_LEFT) {
          node = avd_node_find_nodeid(notifItem->clusterNode.nodeId);
          if (node == nullptr || node->node_state == AVD_AVND_STATE_ABSENT) {
            LOG_IN("%s: CLM node '%s' is not an AMF cluster member",
                   __FUNCTION__, node_name.c_str());
            goto done;
          }
          /* store the invocation for clm response */
          node->clm_pend_inv = invocation;
          /* Validate the node leaving cluster if not acceptable then
             reject it through saClmResponse_4 */
          clm_node_exit_validate(node);
        } else /* All other Cluster changes are not valid in this step */
          LOG_ER("Invalid Cluster change in VALIDATE step");
        break;

      case SA_CLM_CHANGE_START:
        node = avd_node_find_nodeid(notifItem->clusterNode.nodeId);
        if (node == nullptr || node->node_state == AVD_AVND_STATE_ABSENT) {
          LOG_IN("%s: CLM node '%s' is not an AMF cluster member", __FUNCTION__,
                 node_name.c_str());
          goto done;
        }
        if (notifItem->clusterChange == SA_CLM_NODE_LEFT ||
            notifItem->clusterChange == SA_CLM_NODE_SHUTDOWN) {
          if (node->clm_change_start_preceded == true) {
            TRACE_3("Already got callback for start of this change.");
            continue;
          }

          if (strncmp(osaf_extended_name_borrow(rootCauseEntity),
                      "safEE=",
                      sizeof("safEE=") - 1) == 0 ||
              strncmp(osaf_extended_name_borrow(rootCauseEntity),
                      "safHE=",
                      sizeof("safHE=") - 1) == 0) {
            // PLM will take care of calling opensafd stop
            TRACE("rootCause: %s from PLM operation so skipping %u",
                  osaf_extended_name_borrow(rootCauseEntity),
                  notifItem->clusterNode.nodeId);

            SaAisErrorT rc{saClmResponse_4(avd_cb->clmHandle,
                                           invocation,
                                           SA_CLM_CALLBACK_RESPONSE_OK)};
            if (rc != SA_AIS_OK)
              LOG_ER("saClmResponse_4 failed: %i", rc);

            continue;
          }

          /* invocation to be used by pending clm response */
          node->clm_pend_inv = invocation;
          clm_node_exit_start(node, notifItem->clusterChange);
          if (strncmp(osaf_extended_name_borrow(rootCauseEntity),
                      "safNode=", 8) == 0) {
            /* We need to differentiate between CLM lock and error scenario.*/
            node->clm_change_start_preceded = true;
          }
        } else
          LOG_ER("Invalid Cluster change in START step");
        break;

      case SA_CLM_CHANGE_COMPLETED:
        if ((notifItem->clusterChange == SA_CLM_NODE_LEFT) ||
            (notifItem->clusterChange == SA_CLM_NODE_SHUTDOWN)) {
          node = avd_node_find_nodeid(notifItem->clusterNode.nodeId);
          if (node == nullptr) {
            LOG_IN("%s: CLM node '%s' is not an AMF cluster member",
                   __FUNCTION__, node_name.c_str());
            goto done;
          } else if (node->node_state == AVD_AVND_STATE_ABSENT) {
            LOG_IN("%s: CLM node '%s' is not an AMF cluster member; MDS down received",
                   __FUNCTION__, node_name.c_str());
            avd_node_delete_nodeid(node);
            goto done;
          }
          TRACE(" Node Left: rootCauseEntity %s for node %u",
                osaf_extended_name_borrow(rootCauseEntity),
                notifItem->clusterNode.nodeId);
          if (strncmp(osaf_extended_name_borrow(rootCauseEntity),
                      "safEE=",
		      sizeof("safEE") - 1) == 0 ||
              strncmp(osaf_extended_name_borrow(rootCauseEntity),
                      "safHE=",
		      sizeof("safHE") - 1) == 0) {
            /* This callback is because of operation on PLM, so we need to mark
               the node absent, because PLCD will anyway call opensafd stop.*/
            AVD_AVND *node =
                avd_node_find_nodeid(notifItem->clusterNode.nodeId);
            if (node->node_info.nodeId == avd_cb->node_id_avd) {
              /* Self  Termination, wait for getting terminated by opensafd stop
               * script.*/
              LOG_NO("Node Left is self: ignoring");
              break;
            }
            clm_node_exit_complete(notifItem->clusterNode.nodeId);
          } else if (strncmp(osaf_extended_name_borrow(rootCauseEntity),
                             "safNode=", 8) == 0) {
            const std::string rootCause_clm_node(
                Amf::to_string(rootCauseEntity));
            /* This callback is because of operation on CLM.*/
            if (true == node->clm_change_start_preceded) {
              /* We have got a completed callback with start cbk step before, so
                 already locking applications might have been done. So, no
                 action is needed. Completed callback for a node going CLM admin
                 operation may come with completed callabck when some other CLM
                 node leaves cluster membership becuase of OpenSAF stop or one
                 more node is CLM locked and this second nodes exits first. Act
                 only when callback comes for this admin op node.
               */
              if (rootCause_clm_node.compare(node->saAmfNodeClmNode) == 0) {
                node->clm_change_start_preceded = false;
                node->node_info.member = SA_FALSE;
                m_AVSV_SEND_CKPT_UPDT_ASYNC_UPDT(avd_cb, node,
                                                 AVSV_CKPT_AVD_NODE_CONFIG);
              } else {
                TRACE("'%s' not mapped to rootCauseEntity '%s'.",
                      node->name.c_str(), rootCause_clm_node.c_str());
                continue;
              }

            } else {
              /* We have encountered a completed callback without start step,
                 there seems error condition, node would have gone down
                 suddenly. */
              clm_node_exit_complete(notifItem->clusterNode.nodeId);
            }
          } else {
            /* We shouldn't get into this situation.*/
            LOG_ER("Wrong rootCauseEntity %s",
                   osaf_extended_name_borrow(rootCauseEntity));
            osafassert(0);
          }
        } else if (notifItem->clusterChange == SA_CLM_NODE_RECONFIGURED) {
          /* delete, reconfigure, re-add to the node-id db
             incase if the node id has changed */
          node = avd_node_find_nodeid(notifItem->clusterNode.nodeId);
          if (node == nullptr) {
            LOG_NO("Node not a member");
            goto done;
          }
          avd_node_delete_nodeid(node);
          memcpy(&(node->node_info), &(notifItem->clusterNode),
                 sizeof(SaClmClusterNodeT_4));
          avd_node_add_nodeid(node);
        } else if (notifItem->clusterChange == SA_CLM_NODE_JOINED ||
                   notifItem->clusterChange == SA_CLM_NODE_NO_CHANGE) {
          /* This may be due to track flags set to
             SA_TRACK_CURRENT|CHANGES_ONLY and supply no buffer
             in saClmClusterTrack call so update the local database */
          /* get the first node */
          node = nullptr;
          for (const auto &value : *node_name_db) {
            node = value.second;
            if (node->saAmfNodeClmNode.compare(node_name) == 0) {
              break;
            }
          }
          if (node != nullptr) {
            memcpy(&(node->node_info), &(notifItem->clusterNode),
                   sizeof(SaClmClusterNodeT_4));
            avd_node_add_nodeid(node);
            TRACE("Node Joined '%s' '%zu'", node_name.c_str(),
                  node_name.length());

            node->node_info.member = SA_TRUE;
            if (avd_cb->avd_peer_ver < AVD_MBCSV_SUB_PART_VERSION_4) {
              m_AVSV_SEND_CKPT_UPDT_ASYNC_ADD(avd_cb, node,
                                              AVSV_CKPT_AVD_NODE_CONFIG);
            } else {
              m_AVSV_SEND_CKPT_UPDT_ASYNC_UPDT(avd_cb, node,
                                               AVSV_CKPT_AVD_NODE_CONFIG);
            }
            if (node->node_state == AVD_AVND_STATE_PRESENT) {
              TRACE("Node already up and configured");
              /* now try to instantiate all the SUs that need to be */
              clm_node_join_complete(node);
            }
          } else {
            LOG_IN("AMF-node not configured on this CLM-node '%s'",
                   node_name.c_str());
          }
        } else
          LOG_NO("clmTrackCallback in COMPLETED::UNLOCK");
        break;

      case SA_CLM_CHANGE_ABORTED:
        /* Do nothing */
        LOG_NO("ClmTrackCallback ABORTED_STEP");
        break;
      default:
        osafassert(0);
    }
  }
done:
  TRACE_LEAVE();
}

static const SaClmCallbacksT_4 clm_callbacks = {
    0,
    /*.saClmClusterTrackCallback =*/clm_track_cb};

SaAisErrorT avd_clm_init(AVD_CL_CB *cb) {
  SaAisErrorT error = SA_AIS_OK;
  SaClmHandleT clm_handle = 0;
  SaSelectionObjectT sel_obj = 0;

  cb->clmHandle = 0;
  cb->clm_sel_obj = 0;
  cb->is_clm_track_started = false;
  TRACE_ENTER();
  /*
   * TODO: This CLM initialization thread can be re-factored
   * after having osaf dedicated thread, so that all APIs calls
   * to external service can be automatically retried with result
   * code (TRY_AGAIN, TIMEOUT, UNAVAILABLE), or reinitialized within
   * BAD_HANDLE. Also, duplicated codes in initialization thread
   * will be moved to osaf dedicated thread
   */
  bool has_logged_clm_error = false;
  for (;;) {
    SaVersionT Version = {'B', 4, 1};
    error = saClmInitialize_4(&clm_handle, &clm_callbacks, &Version);
    if (error == SA_AIS_ERR_TRY_AGAIN || error == SA_AIS_ERR_TIMEOUT ||
        error == SA_AIS_ERR_UNAVAILABLE) {
      if (error != SA_AIS_ERR_TRY_AGAIN && !has_logged_clm_error) {
        LOG_WA("saClmInitialize_4 returned %u", static_cast<unsigned>(error));
        has_logged_clm_error = true;
      }
      osaf_nanosleep(&kHundredMilliseconds);
      continue;
    }
    if (error == SA_AIS_OK) {
      break;
    } else {
      LOG_ER("Failed to Initialize with CLM: %u", error);
      goto done;
    }
  }
  cb->clmHandle = clm_handle;
  error = saClmSelectionObjectGet(cb->clmHandle, &sel_obj);
  if (error != SA_AIS_OK) {
    LOG_ER("Failed to get selection object from CLM %u", error);
    cb->clmHandle = 0;
    goto done;
  }
  cb->clm_sel_obj = sel_obj;

  TRACE("Successfully initialized CLM");

done:
  TRACE_LEAVE();
  return error;
}

SaAisErrorT avd_clm_track_start(AVD_CL_CB* cb) {
  SaUint8T trackFlags = SA_TRACK_CURRENT | SA_TRACK_CHANGES_ONLY |
                        SA_TRACK_VALIDATE_STEP | SA_TRACK_START_STEP;

  TRACE_ENTER();
  SaAisErrorT error = SA_AIS_OK;

  if (cb->is_clm_track_started == true) {
    // abort all pending and unsuccessful jobs that stop tracking
    // because at this moment, amfd wants to start cluster tracking
    Fifo::remove(cb, JOB_TYPE_CLM);
  }

  error = saClmClusterTrack_4(cb->clmHandle, trackFlags, nullptr);
  if (error != SA_AIS_OK) {
    if (error == SA_AIS_ERR_TRY_AGAIN || error == SA_AIS_ERR_TIMEOUT ||
        error == SA_AIS_ERR_UNAVAILABLE) {
      LOG_WA("Failed to start cluster tracking %u", error);
      error = SA_AIS_ERR_TRY_AGAIN;
    } else {
      LOG_ER("Failed to start cluster tracking %u", error);
    }
  } else {
    cb->is_clm_track_started = true;
  }
  TRACE_LEAVE();
  return error;
}

SaAisErrorT avd_clm_track_stop(AVD_CL_CB* cb) {
  TRACE_ENTER();
  SaAisErrorT error = SA_AIS_OK;

  if (cb->is_clm_track_started == false) {
    // abort all pending and unsuccessful jobs that start tracking
    // because at this moment, amfd wants to sttop cluster tracking
    Fifo::remove(cb, JOB_TYPE_CLM);
  }

  error = saClmClusterTrackStop(cb->clmHandle);
  if (error != SA_AIS_OK) {
    if (error == SA_AIS_ERR_TRY_AGAIN || error == SA_AIS_ERR_TIMEOUT ||
        error == SA_AIS_ERR_UNAVAILABLE) {
      LOG_WA("Failed to stop cluster tracking %u", error);
      error = SA_AIS_ERR_TRY_AGAIN;
    } else if (error == SA_AIS_ERR_NOT_EXIST) {
      /* track changes was not started or stopped successfully */
      LOG_WA("Failed to stop cluster tracking %u", error);
      cb->is_clm_track_started = false;
      error = SA_AIS_OK;
    } else {
      LOG_ER("Failed to stop cluster tracking %u", error);
    }
  } else {
    TRACE("Sucessfully stops cluster tracking");
    cb->is_clm_track_started = false;
  }
  TRACE_LEAVE();
  return error;
}

void clm_node_terminate(AVD_AVND *node) {
  SaAisErrorT rc = SA_AIS_OK;

  if (NCSCC_RC_SUCCESS != avd_node_admin_lock_instantiation(node)) {
    rc = saClmResponse_4(avd_cb->clmHandle, node->clm_pend_inv,
                         SA_CLM_CALLBACK_RESPONSE_ERROR);
    LOG_NO("Pending Response sent for CLM track callback::ERROR '%u'", rc);
    node->clm_pend_inv = 0;
  } /* NCSCC_RC_SUCCESS means that either you had no SUs terminate or you sent
       all the SUs on this node the termination message */
  else if (node->su_cnt_admin_oper == 0) {
    rc = saClmResponse_4(avd_cb->clmHandle, node->clm_pend_inv,
                         SA_CLM_CALLBACK_RESPONSE_OK);
    LOG_NO("Pending Response sent for CLM track callback::OK '%u'", rc);
    node->clm_pend_inv = 0;
  } else
    TRACE("Waiting for the pending SU presence state updates");
}

static void *avd_clm_init_thread(void *arg) {
  TRACE_ENTER();
  AVD_CL_CB *cb = static_cast<AVD_CL_CB *>(arg);
  SaAisErrorT error = SA_AIS_OK;

  if (avd_clm_init(cb) != SA_AIS_OK) {
    LOG_ER("avd_clm_init FAILED");
    goto done;
  }

  if (cb->avail_state_avd == SA_AMF_HA_ACTIVE) {
    for (;;) {
      error = avd_clm_track_start(cb);
      if (error == SA_AIS_ERR_TRY_AGAIN || error == SA_AIS_ERR_TIMEOUT ||
          error == SA_AIS_ERR_UNAVAILABLE) {
        osaf_nanosleep(&kHundredMilliseconds);
        continue;
      }
      if (error == SA_AIS_OK) {
        break;
      } else {
        LOG_ER("avd_clm_track_start FAILED, error: %u", error);
        goto done;
      }
    }
  }

done:
  TRACE_LEAVE();
  return nullptr;
}

SaAisErrorT avd_start_clm_init_bg(void) {
  pthread_t thread;
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

  if (pthread_create(&thread, &attr, avd_clm_init_thread, avd_cb) != 0) {
    LOG_ER("pthread_create FAILED: %s", strerror(errno));
    exit(EXIT_FAILURE);
  }

  pthread_attr_destroy(&attr);
  return SA_AIS_OK;
}

AvdJobDequeueResultT ClmTrackStart::exec(const AVD_CL_CB* cb) {
  AvdJobDequeueResultT res;
  TRACE_ENTER();

  SaAisErrorT rc = avd_clm_track_start(const_cast<AVD_CL_CB*>(cb));
  if (rc == SA_AIS_OK) {
    delete Fifo::dequeue();
    res = JOB_EXECUTED;
  } else if (rc == SA_AIS_ERR_TRY_AGAIN) {
    TRACE("TRY-AGAIN");
    res = JOB_ETRYAGAIN;
  } else {
    delete Fifo::dequeue();
    LOG_ER("%s: ClmTrackStart FAILED %u", __FUNCTION__, rc);
    res = JOB_ERR;
  }

  TRACE_LEAVE();
  return res;
}

AvdJobDequeueResultT ClmTrackStop::exec(const AVD_CL_CB* cb) {
  AvdJobDequeueResultT res;
  TRACE_ENTER();

  SaAisErrorT rc = avd_clm_track_stop(const_cast<AVD_CL_CB*>(cb));
  if (rc == SA_AIS_OK) {
    delete Fifo::dequeue();
    res = JOB_EXECUTED;
  } else if (rc == SA_AIS_ERR_TRY_AGAIN) {
    TRACE("TRY-AGAIN");
    res = JOB_ETRYAGAIN;
  } else {
    delete Fifo::dequeue();
    LOG_ER("%s: ClmTrackStop FAILED %u", __FUNCTION__, rc);
    res = JOB_ERR;
  }

  TRACE_LEAVE();
  return res;
}
