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
 * Author(s): Ericsson AB
 *
 * This file implements the AMF interface for LGS.
 * The interface exist of one exported function: lgs_amf_init().
 * The AMF callback functions except a number of exported functions from
 * other modules.
 */

#include "nid/agent/nid_start_util.h"
#include "osaf/immutil/immutil.h"
#include "log/logd/lgs.h"
#include "log/logd/lgs_config.h"
#include "log/logd/lgs_oi_admin.h"

static void close_all_files() {
  log_stream_t *stream;
  SaBoolT endloop = SA_FALSE, jstart = SA_TRUE;

  // Iterate all existing log streams in cluster.
  while ((stream = iterate_all_streams(endloop, jstart)) && !endloop) {
    jstart = SA_FALSE;
    if (log_stream_file_close(stream) != 0)
      LOG_WA("Could not close file for stream %s", stream->name.c_str());
  }
}

/****************************************************************************
 * Name          : amf_active_state_handler
 *
 * Description   : This function is called upon receiving an active state
 *                 assignment from AMF.
 *
 * Arguments     : invocation - Designates a particular invocation.
 *                 cb         - A pointer to the LGS control block.
 *
 * Return Values : None
 *
 * Notes         : None
 *****************************************************************************/
static SaAisErrorT amf_active_state_handler(lgs_cb_t *cb,
                                            SaInvocationT invocation) {
  log_stream_t *stream;
  SaAisErrorT error = SA_AIS_OK;
  SaBoolT endloop = SA_FALSE, jstart = SA_TRUE;
  uint32_t count = 0;

  TRACE_ENTER2("HA ACTIVE request");

  if (cb->ha_state == SA_AMF_HA_ACTIVE) {
    /* State change was already processed in RDA callback */
    goto done;
  }

  lgsOiStart();
  lgs_start_gcfg_applier();

  // Iterate all existing log streams in cluster.
  while ((stream = iterate_all_streams(endloop, jstart)) && !endloop) {
    jstart = SA_FALSE;
    *stream->p_fd = -1; /* First Initialize fd */
    count++;
  }

  if (count == 0) LOG_ER("No streams exist!");

done:
  /* Update role independent of stream processing */
  lgs_cb->mds_role = V_DEST_RL_ACTIVE;
  TRACE_LEAVE();
  return error;
}

/****************************************************************************
 * Name          : amf_standby_state_handler
 *
 * Description   : This function is called upon receving an standby state
 *                 assignment from AMF.
 *
 * Arguments     : invocation - Designates a particular invocation.
 *                 cb         - A pointer to the LGS control block.
 *
 * Return Values : None
 *
 * Notes         : None
 *****************************************************************************/
static SaAisErrorT amf_standby_state_handler(lgs_cb_t *cb,
                                             SaInvocationT invocation) {
  SaAisErrorT error = SA_AIS_OK;

  TRACE_ENTER2("HA STANDBY request");

  cb->ha_state = SA_AMF_HA_STANDBY;
  cb->mds_role = V_DEST_RL_STANDBY;

  TRACE_LEAVE();
  return error;
}

/****************************************************************************
 * Name          : amf_quiescing_state_handler
 *
 * Description   : This function is called upon receving an Quiescing state
 *                 assignment from AMF.
 *
 * Arguments     : invocation - Designates a particular invocation.
 *                 cb         - A pointer to the LGS control block.
 *
 * Return Values : None
 *
 * Notes         : None
 *****************************************************************************/
static SaAisErrorT amf_quiescing_state_handler(lgs_cb_t *cb,
                                               SaInvocationT invocation) {
  TRACE_ENTER2("HA QUIESCING request");
  close_all_files();

  // Give up our IMM OI implementer role and the OpensafConfig class applier
  lgsOiStop();
  lgs_stop_gcfg_applier();

  return saAmfCSIQuiescingComplete(cb->amf_hdl, invocation, SA_AIS_OK);
}

/****************************************************************************
 * Name          : amf_quiesced_state_handler
 *
 * Description   : This function is called upon receving an Quiesced state
 *                 assignment from AMF.
 *
 * Arguments     : invocation - Designates a particular invocation.
 *                 cb         - A pointer to the LGS control block.
 *
 * Return Values : None
 *
 * Notes         : None
 *****************************************************************************/
static SaAisErrorT amf_quiesced_state_handler(lgs_cb_t *cb,
                                              SaInvocationT invocation) {
  V_DEST_RL mds_role;
  TRACE_ENTER2("HA AMF QUIESCED STATE request");
  close_all_files();

  /* Give up our IMM OI implementer role */
  lgsOiStop();
  lgs_stop_gcfg_applier();

  /*
  ** Change the MDS VDSET role to Quiesced. Wait for MDS callback with type
  ** MDS_CALLBACK_QUIESCED_ACK. Then change MBCSv role. Don't change
  ** cb->ha_state now.
  */

  mds_role = cb->mds_role;
  cb->mds_role = V_DEST_RL_QUIESCED;
  SaAisErrorT ais_rc = SA_AIS_OK;
  if (lgs_mds_change_role(cb) != NCSCC_RC_SUCCESS) {
    LOG_ER("lgs_mds_change_role FAILED");
    ais_rc = SA_AIS_ERR_FAILED_OPERATION;
    cb->mds_role = mds_role;
    goto done;
  }

  cb->amf_invocation_id = invocation;
  cb->is_quiesced_set = true;
done:
  return ais_rc;
}

/****************************************************************************
 * Name          : amf_health_chk_callback
 *
 * Description   : This is the callback function which will be called
 *                 when the AMF framework needs to health check for the
 *component.
 *
 * Arguments     : invocation - Designates a particular invocation.
 *                 compName       - A pointer to the name of the component
 *                                  whose readiness state the Availability
 *                                  Management Framework is setting.
 *                 checkType      - The type of healthcheck to be executed.
 *
 * Return Values : None
 *
 * Notes         : None
 *****************************************************************************/
static void amf_health_chk_callback(SaInvocationT invocation,
                                    const SaNameT *compName,
                                    SaAmfHealthcheckKeyT *checkType) {
  saAmfResponse(lgs_cb->amf_hdl, invocation, SA_AIS_OK);
}

/****************************************************************************
 * Name          : amf_csi_set_callback
 *
 * Description   : AMF callback function called
 *                 when there is any change in the HA state.
 *
 * Arguments     : invocation     - This parameter designated a particular
 *                                  invocation of this callback function. The
 *                                  invoke process return invocation when it
 *                                  responds to the Availability Management
 *                                  FrameWork using the saAmfResponse()
 *                                  function.
 *                 compName       - A pointer to the name of the component
 *                                  whose readiness state the Availability
 *                                  Management Framework is setting.
 *                 haState        - The new HA state to be assumed by the
 *                                  component service instance identified by
 *                                  csiName.
 *                 csiDescriptor - This will indicate whether or not the
 *                                  component service instance for
 *                                  ativeCompName went through quiescing.
 *
 * Return Values : None.
 *
 * Notes         : None.
 *****************************************************************************/
static void amf_csi_set_callback(SaInvocationT invocation,
                                 const SaNameT *compName,
                                 SaAmfHAStateT new_haState,
                                 SaAmfCSIDescriptorT csiDescriptor) {
  SaAisErrorT error = SA_AIS_OK;
  SaAmfHAStateT prev_haState;
  bool role_change = true;
  uint32_t rc = NCSCC_RC_SUCCESS;

  TRACE_ENTER();

  /*
   *  Handle Active to Active role change.
   */
  prev_haState = lgs_cb->ha_state;

  if ((rc = initialize_for_assignment(lgs_cb, new_haState)) !=
      NCSCC_RC_SUCCESS) {
    TRACE("initialize_for_assignment FAILED %u", (unsigned)rc);
    error = SA_AIS_ERR_FAILED_OPERATION;
    goto response;
  }

  /* Invoke the appropriate state handler routine */
  switch (new_haState) {
    case SA_AMF_HA_ACTIVE:
      error = amf_active_state_handler(lgs_cb, invocation);
      break;
    case SA_AMF_HA_STANDBY:
      error = amf_standby_state_handler(lgs_cb, invocation);
      break;
    case SA_AMF_HA_QUIESCED:
      /* switch-over */
      error = amf_quiesced_state_handler(lgs_cb, invocation);
      break;
    case SA_AMF_HA_QUIESCING:
      /* shutdown admin op */
      error = amf_quiescing_state_handler(lgs_cb, invocation);
      break;
    default:
      LOG_WA("invalid state: %d ", new_haState);
      error = SA_AIS_ERR_FAILED_OPERATION;
      break;
  }

  if (error != SA_AIS_OK) goto response;

  if (new_haState == SA_AMF_HA_QUIESCED) {
    /* AMF response will be done later when MDS quiesced ack has been received
     */
    goto done;
  }

  /* Update control block */
  lgs_cb->ha_state = new_haState;

  if (new_haState == SA_AMF_HA_ACTIVE || new_haState == SA_AMF_HA_STANDBY) {
    lgs_cb->ckpt_state = COLD_SYNC_IDLE;
  }

  /* Handle active to active role change. */
  if ((prev_haState == SA_AMF_HA_ACTIVE) && (new_haState == SA_AMF_HA_ACTIVE))
    role_change = false;

  /* Handle Stby to Stby role change. */
  if ((prev_haState == SA_AMF_HA_STANDBY) && (new_haState == SA_AMF_HA_STANDBY))
    role_change = false;

  if (role_change == true) {
    if ((rc = lgs_mds_change_role(lgs_cb)) != NCSCC_RC_SUCCESS) {
      LOG_ER("lgs_mds_change_role FAILED");
      error = SA_AIS_ERR_FAILED_OPERATION;
      goto response;
    }

    /* Inform MBCSV of HA state change */
    if (NCSCC_RC_SUCCESS != lgs_mbcsv_change_HA_state(lgs_cb, new_haState))
      error = SA_AIS_ERR_FAILED_OPERATION;
  }

response:
  saAmfResponse(lgs_cb->amf_hdl, invocation, error);
done:
  TRACE_LEAVE();
}

/****************************************************************************
 * Name          : amf_comp_terminate_callback
 *
 * Description   : This is the callback function which will be called
 *                 when the AMF framework needs to terminate LGS. This does
 *                 all required to destroy LGS(except to unregister from AMF)
 *
 * Arguments     : invocation     - This parameter designated a particular
 *                                  invocation of this callback function. The
 *                                  invoke process return invocation when it
 *                                  responds to the Availability Management
 *                                  FrameWork using the saAmfResponse()
 *                                  function.
 *                 compName       - A pointer to the name of the component
 *                                  whose readiness state the Availability
 *                                  Management Framework is setting.
 *
 * Return Values : None
 *
 * Notes         : None
 *****************************************************************************/
static void amf_comp_terminate_callback(SaInvocationT invocation,
                                        const SaNameT *compName) {
  TRACE_ENTER();

  saAmfResponse(lgs_cb->amf_hdl, invocation, SA_AIS_OK);

  /* Detach from IPC */
  m_NCS_IPC_DETACH(&lgs_mbx, NULL, lgs_cb);

  /* Disconnect from MDS */
  lgs_mds_finalize(lgs_cb);
  sleep(1);
  LOG_NO("Received AMF component terminate callback, exiting");
  exit(0);
}

/****************************************************************************
 * Name          : amf_csi_rmv_callback
 *
 * Description   : This callback routine is invoked by AMF during a
 *                 CSI set removal operation.
 *
 * Arguments     : invocation     - This parameter designated a particular
 *                                  invocation of this callback function. The
 *                                  invoke process return invocation when it
 *                                  responds to the Availability Management
 *                                  FrameWork using the saAmfResponse()
 *                                  function.
 *                 compName       - A pointer to the name of the component
 *                                  whose readiness state the Availability
 *                                  Management Framework is setting.
 *                 csiName        - A const pointer to csiName
 *                 csiFlags       - csi Flags
 * Return Values : None
 *****************************************************************************/
static void amf_csi_rmv_callback(SaInvocationT invocation,
                                 const SaNameT *compName,
                                 const SaNameT *csiName,
                                 const SaAmfCSIFlagsT csiFlags) {
  TRACE_ENTER();
  saAmfResponse(lgs_cb->amf_hdl, invocation, SA_AIS_OK);
  TRACE_LEAVE();
}

/*****************************************************************************\
 *  Name:          lgs_healthcheck_start                           *
 *                                                                            *
 *  Description:   To start the health check                                  *
 *                                                                            *
 *  Arguments:     NCSSA_CB* - Control Block                                  *
 *                                                                            *
 *  Returns:       SA_AIS_OK    - everything is OK                            *
 *                 SA_AIS_ERR_* -  failure                                    *
 *  NOTE:                                                                     *
\******************************************************************************/
static SaAisErrorT amf_healthcheck_start(lgs_cb_t *lgs_cb) {
  SaAisErrorT error;
  SaAmfHealthcheckKeyT healthy;
  char *health_key;

  TRACE_ENTER();

  /** start the AMF health check **/
  memset(&healthy, 0, sizeof(healthy));
  health_key = getenv("LGSV_ENV_HEALTHCHECK_KEY");

  if (health_key == nullptr)
    snprintf(reinterpret_cast<char *>(healthy.key),
             SA_AMF_HEALTHCHECK_KEY_MAX, "F1B2");
  else
    snprintf(reinterpret_cast<char *>(healthy.key),
             SA_AMF_HEALTHCHECK_KEY_MAX, "%s", health_key);

  healthy.keyLen = strlen(reinterpret_cast<char *>(healthy.key));

  error = saAmfHealthcheckStart(lgs_cb->amf_hdl, &lgs_cb->comp_name, &healthy,
                                SA_AMF_HEALTHCHECK_AMF_INVOKED,
                                SA_AMF_COMPONENT_FAILOVER);

  if (error != SA_AIS_OK) LOG_ER("saAmfHealthcheckStart FAILED: %u", error);

  TRACE_LEAVE();
  return error;
}

/**************************************************************************
 Function: lgs_amf_register

 Purpose:  Function which registers LGS with AMF.

 Input:    None

 Returns:  SA_AIS_OK    - everything is OK
           SA_AIS_ERR_* -  failure

**************************************************************************/
SaAisErrorT lgs_amf_init(lgs_cb_t *cb) {
  SaAmfCallbacksT amfCallbacks;
  SaVersionT amf_version = kAmfVersion;
  SaAisErrorT error;

  TRACE_ENTER();

  if (cb->nid_started &&
      amf_comp_name_get_set_from_file("LOGD_COMP_NAME_FILE", &cb->comp_name) !=
          NCSCC_RC_SUCCESS) {
    error = SA_AIS_ERR_NOT_EXIST;
    goto done;
  }

  /* Initialize AMF callbacks */
  memset(&amfCallbacks, 0, sizeof(SaAmfCallbacksT));
  amfCallbacks.saAmfHealthcheckCallback = amf_health_chk_callback;
  amfCallbacks.saAmfCSISetCallback = amf_csi_set_callback;
  amfCallbacks.saAmfComponentTerminateCallback = amf_comp_terminate_callback;
  amfCallbacks.saAmfCSIRemoveCallback = amf_csi_rmv_callback;

  /* Initialize the AMF library */
  error = saAmfInitialize(&cb->amf_hdl, &amfCallbacks, &amf_version);
  if (error != SA_AIS_OK) {
    LOG_ER("saAmfInitialize() FAILED: %u", error);
    goto done;
  }

  /* Obtain the AMF selection object to wait for AMF events */
  error = saAmfSelectionObjectGet(cb->amf_hdl, &cb->amfSelectionObject);
  if (error != SA_AIS_OK) {
    LOG_ER("saAmfSelectionObjectGet() FAILED: %u", error);
    goto done;
  }

  /* Get the component name */
  error = saAmfComponentNameGet(cb->amf_hdl, &cb->comp_name);
  if (error != SA_AIS_OK) {
    LOG_ER("saAmfComponentNameGet() FAILED: %u", error);
    goto done;
  }

  /* Register component with AMF */
  error = saAmfComponentRegister(cb->amf_hdl, &cb->comp_name, nullptr);
  if (error != SA_AIS_OK) {
    LOG_ER("saAmfComponentRegister() FAILED");
    goto done;
  }

  /* Start AMF healthchecks */
  if ((error = amf_healthcheck_start(cb)) != SA_AIS_OK) {
    LOG_ER("amf_healthcheck_start() failed with error %u", error);
    goto done;
  }

done:
  TRACE_LEAVE();
  return error;
}
