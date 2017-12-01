/*      OpenSAF
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
 * This file implements the AMF interface for SMFD.
 * The interface exist of one exported function: smfd_amf_init().
 * The AMF callback functions except a number of exported functions from
 * other modules.
 */

#include "smf/smfd/smfd.h"
#include "smf/smfd/smfd_long_dn.h"

/****************************************************************************
 * Name          : amf_active_state_handler
 *
 * Description   : This function is called upon receving an active state
 *                 assignment from AMF.
 *
 * Arguments     : invocation - Designates a particular invocation.
 *                 cb         - A pointer to the SMFD control block.
 *
 * Return Values : None
 *
 * Notes         : None
 *****************************************************************************/
static SaAisErrorT amf_active_state_handler(smfd_cb_t *cb,
                                            SaInvocationT invocation) {
  SaAisErrorT rc = SA_AIS_OK;
  TRACE_ENTER2("HA ACTIVE request");
  cb->mds_role = V_DEST_RL_ACTIVE;

  // Initialize the OI handle and get the selection object
  if (campaign_oi_init(cb) != NCSCC_RC_SUCCESS) {
    LOG_ER("campaign_oi_init FAIL");
    rc = SA_AIS_ERR_FAILED_OPERATION;
  }

  // Read SMF configuration data and set cb data structure
  if (read_config_and_set_control_block(cb) != NCSCC_RC_SUCCESS) {
    LOG_ER("read_config_and_set_control_block FAIL");
    rc = SA_AIS_ERR_FAILED_OPERATION;
  }

  // Set class implementers, recreate campaign objects and start callbackUtil
  // thread
  if (campaign_oi_activate(cb) != NCSCC_RC_SUCCESS) {
    LOG_ER("amf_active_state_handler oi activate FAIL");
    rc = SA_AIS_ERR_FAILED_OPERATION;
  }

  // Start the long Dn monotoring applier
  TRACE("%s: SmfLongDnInfo->Start()", __FUNCTION__);
  SmfLongDnInfo->Start();

  TRACE_LEAVE();
  return rc;
}

/****************************************************************************
 * Name          : amf_standby_state_handler
 *
 * Description   : This function is called upon receving an standby state
 *                 assignment from AMF.
 *
 * Arguments     : invocation - Designates a particular invocation.
 *                 cb         - A pointer to the SMFD control block.
 *
 * Return Values : None
 *
 * Notes         : None
 *****************************************************************************/
static SaAisErrorT amf_standby_state_handler(smfd_cb_t *cb,
                                             SaInvocationT invocation) {
  SaAisErrorT result = SA_AIS_OK;

  TRACE_ENTER2("HA STANDBY request");

  cb->mds_role = V_DEST_RL_STANDBY;
  goto done;

done:
  TRACE_LEAVE();
  return result;
}

/****************************************************************************
 * Name          : amf_quiescing_state_handler
 *
 * Description   : This function is called upon receving an Quiescing state
 *                 assignment from AMF.
 *
 * Arguments     : invocation - Designates a particular invocation.
 *                 cb         - A pointer to the SMFD control block.
 *
 * Return Values : None
 *
 * Notes         : None
 *****************************************************************************/
static SaAisErrorT amf_quiescing_state_handler(smfd_cb_t *cb,
                                               SaInvocationT invocation) {
  TRACE_ENTER2("HA QUIESCING request");

  return saAmfCSIQuiescingComplete(cb->amf_hdl, invocation, SA_AIS_OK);
}

/****************************************************************************
 * Name          : amf_quiesced_state_handler
 *
 * Description   : This function is called upon receving an Quiesced state
 *                 assignment from AMF.
 *
 * Arguments     : invocation - Designates a particular invocation.
 *                 cb         - A pointer to the SMFD control block.
 *
 * Return Values : None
 *
 * Notes         : None
 *****************************************************************************/
static SaAisErrorT amf_quiesced_state_handler(smfd_cb_t *cb,
                                              SaInvocationT invocation) {
  TRACE_ENTER();
  V_DEST_RL mds_role;
  SaAisErrorT ais_rc = SA_AIS_OK;
  uint32_t ncscc_rc = NCSCC_RC_SUCCESS;

  /* Terminate threads and finalize the OI handle */
  if (campaign_oi_deactivate(cb) != NCSCC_RC_SUCCESS) {
    LOG_NO("amf_quiesced_state_handler oi deactivate FAILED, continue");
  }

  // Stop the long Dn monitoring applier
  TRACE("%s: SmfLongDnInfo->Stop()", __FUNCTION__);
  SmfLongDnInfo->Stop();

  /*
   ** Change the MDS VDSET role to Quiesced. Wait for MDS callback with type
   ** MDS_CALLBACK_QUIESCED_ACK. Don't change cb->ha_state now.
   */
  mds_role = cb->mds_role;
  cb->mds_role = V_DEST_RL_QUIESCED;
  LOG_NO("MDS %s: smfd_mds_change_role()", __FUNCTION__);
  if ((ncscc_rc = smfd_mds_change_role(cb)) != NCSCC_RC_SUCCESS) {
    LOG_ER("smfd_mds_change_role [V_DEST_RL_QUIESCED] FAILED");
    ais_rc = SA_AIS_ERR_FAILED_OPERATION;
    cb->mds_role = mds_role;
    goto done;
  }

  cb->amf_invocation_id = invocation;
  cb->is_quiesced_set = true;
done:
  TRACE_LEAVE();
  return ais_rc;
}

/****************************************************************************
 * Name          : amf_health_chk_callback
 *
 * Description   : This is the callback function which will be called
 *                 when the AMF framework initiates a health check for the
 *component.
 *
 * Arguments     : invocation - Designates a particular invocation.
 *                 compName       - A pointer to the name of the component
 *                                  whose readiness stae the Availability
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
  saAmfResponse(smfd_cb->amf_hdl, invocation, SA_AIS_OK);
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
 *                                  responds to the Avilability Management
 *                                  FrameWork using the saAmfResponse()
 *                                  function.
 *                 compName       - A pointer to the name of the component
 *                                  whose readiness stae the Availability
 *                                  Management Framework is setting.
 *                 haState        - The new HA state to be assumeb by the
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
  prev_haState = smfd_cb->ha_state;

  if ((rc = initialize_for_assignment(smfd_cb, new_haState)) !=
      NCSCC_RC_SUCCESS) {
    LOG_ER("initialize_for_assignment FAILED %u", (unsigned)rc);
    error = SA_AIS_ERR_FAILED_OPERATION;
    goto response;
  }

  /* Invoke the appropriate state handler routine */
  switch (new_haState) {
    case SA_AMF_HA_ACTIVE:
      error = amf_active_state_handler(smfd_cb, invocation);
      break;
    case SA_AMF_HA_STANDBY:
      error = amf_standby_state_handler(smfd_cb, invocation);
      break;
    case SA_AMF_HA_QUIESCED:
      error = amf_quiesced_state_handler(smfd_cb, invocation);
      break;
    case SA_AMF_HA_QUIESCING:
      error = amf_quiescing_state_handler(smfd_cb, invocation);
      break;
    default:
      LOG_WA("invalid state: %d ", new_haState);
      error = SA_AIS_ERR_BAD_OPERATION;
      break;
  }

  if (error != SA_AIS_OK) goto response;

  if (new_haState == SA_AMF_HA_QUIESCED)
    /* AMF response will be done later when MDS quiesced ack has been received
     */
    goto done;

  /* Update control block */
  smfd_cb->ha_state = new_haState;

  /* Handle active to active role change. */
  if ((prev_haState == SA_AMF_HA_ACTIVE) && (new_haState == SA_AMF_HA_ACTIVE))
    role_change = false;

  /* Handle Stby to Stby role change. */
  if ((prev_haState == SA_AMF_HA_STANDBY) && (new_haState == SA_AMF_HA_STANDBY))
    role_change = false;

  if (role_change == true) {
    LOG_NO("MDS %s: smfd_mds_change_role()", __FUNCTION__);
    if ((rc = smfd_mds_change_role(smfd_cb)) != NCSCC_RC_SUCCESS) {
      TRACE("smfd_mds_change_role FAILED");
      error = SA_AIS_ERR_FAILED_OPERATION;
      goto response;
    }
  }
response:
  saAmfResponse(smfd_cb->amf_hdl, invocation, error);
done:
  TRACE_LEAVE();
}

/****************************************************************************
 * Name          : amf_comp_terminate_callback
 *
 * Description   : This is the callback function which will be called
 *                 when the AMF framework want to terminate SMFD. This does
 *                 all required to destroy SMFD(except to unregister from AMF)
 *
 * Arguments     : invocation     - This parameter designated a particular
 *                                  invocation of this callback function. The
 *                                  invoke process return invocation when it
 *                                  responds to the Avilability Management
 *                                  FrameWork using the saAmfResponse()
 *                                  function.
 *                 compName       - A pointer to the name of the component
 *                                  whose readiness stae the Availability
 *                                  Management Framework is setting.
 *
 * Return Values : None
 *
 * Notes         : None
 *****************************************************************************/
static void amf_comp_terminate_callback(SaInvocationT invocation,
                                        const SaNameT *compName) {
  TRACE_ENTER();

  saAmfResponse(smfd_cb->amf_hdl, invocation, SA_AIS_OK);

  /* Detach from IPC */
  m_NCS_IPC_DETACH(&smfd_cb->mbx, NULL, smfd_cb);

  /* Disconnect from MDS */
  smfd_mds_finalize(smfd_cb);
  sleep(1);
  LOG_NO("Received AMF component terminate callback, exiting");
  exit(EXIT_SUCCESS);
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
 *                                  responds to the Avilability Management
 *                                  FrameWork using the saAmfResponse()
 *                                  function.
 *                 compName       - A pointer to the name of the component
 *                                  whose readiness stae the Availability
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
  saAmfResponse(smfd_cb->amf_hdl, invocation, SA_AIS_OK);
  TRACE_LEAVE();
}

/*****************************************************************************\
 *  Name:          amf_healthcheck_start                           *
 *                                                                            *
 *  Description:   To start the health check                                  *
 *                                                                            *
 *  Arguments:     SMFD_CB* - Control Block                                   *
 *                                                                            *
 *  Returns:       SA_AIS_OK    - everything is OK                            *
 *                 SA_AIS_ERR_* -  failure                                    *
 *  NOTE:                                                                     *
\******************************************************************************/
static SaAisErrorT amf_healthcheck_start(smfd_cb_t *cb) {
  SaAisErrorT result;
  SaAmfHealthcheckKeyT healthy;
  char *health_key;

  TRACE_ENTER();

  /** start the AMF health check **/
  memset(&healthy, 0, sizeof(healthy));
  health_key = getenv("SMFSV_ENV_HEALTHCHECK_KEY");

  if (health_key == NULL) {
    strncpy((char *)healthy.key, "A1B2", SA_AMF_HEALTHCHECK_KEY_MAX);
  } else {
    if (strlen(health_key) > SA_AMF_HEALTHCHECK_KEY_MAX) {
      LOG_ER("amf_healthcheck_start(): Helthcheck key to long");
      return SA_AIS_ERR_NAME_TOO_LONG;
    }
    strncpy((char *)healthy.key, health_key,
            strlen(health_key));  // The key does not need to be null terminated
  }

  healthy.keyLen = strlen((const char *)healthy.key);

  result = saAmfHealthcheckStart(cb->amf_hdl, &cb->comp_name, &healthy,
                                 SA_AMF_HEALTHCHECK_AMF_INVOKED,
                                 SA_AMF_COMPONENT_FAILOVER);

  if (result != SA_AIS_OK) LOG_ER("saAmfHealthcheckStart FAILED: %u", result);

  TRACE_LEAVE();
  return result;
}

/**************************************************************************
 Function: smfd_amf_init

 Purpose:  Function which initializes SMFD with AMF.

 Input:    None

 Returns:  SA_AIS_OK    - everything is OK
           SA_AIS_ERR_* - failure

**************************************************************************/
SaAisErrorT smfd_amf_init(smfd_cb_t *cb) {
  SaAmfCallbacksT amfCallbacks;
  SaVersionT amf_version;
  SaAisErrorT result;

  TRACE_ENTER();

  /* Initialize AMF callbacks */
  memset(&amfCallbacks, 0, sizeof(SaAmfCallbacksT));
  amfCallbacks.saAmfHealthcheckCallback = amf_health_chk_callback;
  amfCallbacks.saAmfCSISetCallback = amf_csi_set_callback;
  amfCallbacks.saAmfComponentTerminateCallback = amf_comp_terminate_callback;
  amfCallbacks.saAmfCSIRemoveCallback = amf_csi_rmv_callback;

  amf_version.releaseCode = 'B';
  amf_version.majorVersion = 0x01;
  amf_version.minorVersion = 0x01;

  /* Initialize the AMF library */
  result = saAmfInitialize(&cb->amf_hdl, &amfCallbacks, &amf_version);
  if (result != SA_AIS_OK) {
    LOG_ER("saAmfInitialize() FAILED: %u", result);
    goto done;
  }

  /* Obtain the AMF selection object to wait for AMF events */
  result = saAmfSelectionObjectGet(cb->amf_hdl, &cb->amfSelectionObject);
  if (result != SA_AIS_OK) {
    LOG_ER("saAmfSelectionObjectGet() FAILED: %u", result);
    goto done;
  }

  /* Get the component name */
  result = saAmfComponentNameGet(cb->amf_hdl, &cb->comp_name);
  if (result != SA_AIS_OK) {
    LOG_ER("saAmfComponentNameGet() FAILED: %u", result);
    goto done;
  }

  /* Register component with AMF */
  result = saAmfComponentRegister(cb->amf_hdl, &cb->comp_name, (SaNameT *)NULL);
  if (result != SA_AIS_OK) {
    LOG_ER("saAmfComponentRegister() FAILED");
    goto done;
  }

  /* Start AMF healthchecks */
  if ((result = amf_healthcheck_start(cb)) != SA_AIS_OK) {
    LOG_ER("health check start FAILED");
    goto done;
  }

done:
  TRACE_LEAVE();
  return result;
}
