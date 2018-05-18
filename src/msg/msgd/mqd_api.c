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

  DESCRIPTION: This file includes following routines:

   mqd_lib_req.....................MQD LIB request routine
   mqd_lib_init....................MQD LIB init routine
   mqd_lib_destroy.................MQD LIB destroy routine
   mqd_main_process................MQD event handler routine
   mqd_cb_init.....................MQD instance init routine
   mqd_cb_shut.....................MQD instance shut routine
   mqd_lm_init.....................MQD layer management init routine
   mqd_lm_shut.....................MQD layer management shut routine
   mqd_amf_init....................MQD AMF init routine
   mqd_amf_init....................MQD AMF shut routine
   mqd_asapi_bind..................MQD ASAPi bind routine
   mqd_asapi_unbind................MQD ASAPi unbind routine
   mqd_clear_mbx...................Cleans all the pending event from MBX

******************************************************************************
*/

/*
 * Module Inclusion Control...
 */
#define NCS_2_0 1

#include <poll.h>
#include <stdlib.h>

#include "msg/msgd/mqd.h"
#include "mqd_imm.h"
#include "mqd_ntf.h"

MQDLIB_INFO gl_mqdinfo;

enum { FD_TERM = 0, FD_AMF, FD_MBCSV, FD_MBX, FD_CLM, FD_NTF, NUM_FD };

static struct pollfd fds[NUM_FD];
static nfds_t nfds = NUM_FD;

/******************************** LOCAL ROUTINES *****************************/
static uint32_t mqd_lib_init(void);
static void mqd_lib_destroy(void);
void mqd_main_process(uint32_t);
static uint32_t mqd_cb_init(MQD_CB *);
static void mqd_cb_shut(MQD_CB *);
static uint32_t mqd_lm_init(MQD_CB *);
static void mqd_lm_shut(MQD_CB *);
static uint32_t mqd_amf_init(MQD_CB *);
static void mqd_asapi_bind(MQD_CB *);
static void mqd_asapi_unbind(void);
static bool mqd_clear_mbx(NCSCONTEXT arg, NCSCONTEXT msg);
/*****************************************************************************/

/****************************************************************************\
  PROCEDURE NAME : mqd_lib_req

  DESCRIPTION    : This is the NCS SE API which is used to init/destroy or
		   Create/destroy PWE's. This will be called by SBOM.

  ARGUMENTS      : info  - This is the pointer to the input information which
		   SBOM gives.

  RETURNS        : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE..
\*****************************************************************************/

uint32_t mqd_lib_req(NCS_LIB_REQ_INFO *info)
{
	uint32_t rc = NCSCC_RC_SUCCESS;
	TRACE_ENTER();

	switch (info->i_op) {
	case NCS_LIB_REQ_CREATE:
		rc = mqd_lib_init();
		break;

	case NCS_LIB_REQ_DESTROY:
		mqd_lib_destroy();
		break;

	default:
		break;
	}
	if (rc != NCSCC_RC_SUCCESS)
		LOG_CR("Library initialization failed");
	TRACE_LEAVE();
	return rc;
} /* End of mqd_lib_req() */

static SaAisErrorT mqd_clm_init(MQD_CB *cb)
{
	SaAisErrorT saErr = SA_AIS_OK;

	do {
		SaVersionT clm_version;
		SaClmCallbacksT_4 mqd_clm_cbk;

		m_MQSV_GET_CLM_VER(clm_version);
		mqd_clm_cbk.saClmClusterNodeGetCallback = NULL;
		mqd_clm_cbk.saClmClusterTrackCallback =
		    mqd_clm_cluster_track_callback;

		saErr =
		    saClmInitialize_4(&cb->clm_hdl, &mqd_clm_cbk, &clm_version);
		if (saErr != SA_AIS_OK) {
			LOG_ER("saClmInitialize_4 failed with error %u",
			       (unsigned)saErr);
			break;
		}
		TRACE_1("saClmInitialize success");

		saErr = saClmSelectionObjectGet(cb->clm_hdl, &cb->clm_sel_obj);
		if (SA_AIS_OK != saErr) {
			LOG_ER("saClmSelectionObjectGet failed with error %u",
			       (unsigned)saErr);
			break;
		}
		TRACE_1("saClmSelectionObjectGet success");

		saErr = saClmClusterTrack_4(cb->clm_hdl,
				SA_TRACK_CHANGES_ONLY,
				NULL);
		if (SA_AIS_OK != saErr) {
			LOG_ER("saClmClusterTrack_4 failed with error %u",
			       (unsigned)saErr);
			break;
		}
		TRACE_1("saClmClusterTrack success");
	} while (false);

	if (saErr != SA_AIS_OK && cb->clm_hdl)
		saClmFinalize(cb->clm_hdl);

	return saErr;
}

static SaAisErrorT mqd_ntf_init(MQD_CB *cb)
{
	SaAisErrorT rc = SA_AIS_OK;
	TRACE_ENTER();

	do {
		SaVersionT ntfVersion = { 'A', 1, 1 };
		SaNtfCallbacksT callbacks = {
			mqdNtfNotificationCallback,
			0
		};

		rc = saNtfInitialize(&cb->ntfHandle, &callbacks, &ntfVersion);
		if (rc != SA_AIS_OK) {
			LOG_ER("saNtfInitialize failed with error %u", rc);
			break;
		}

		rc = saNtfSelectionObjectGet(cb->ntfHandle, &cb->ntf_sel_obj);
		if (SA_AIS_OK != rc) {
			LOG_ER("saNtfSelectionObjectGet failed with error %u",
			       rc);
			break;
		}

		rc = mqdInitNtfSubscriptions(cb->ntfHandle);
		if (rc != SA_AIS_OK)
			break;
	} while (false);

	if (rc != SA_AIS_OK && cb->ntfHandle)
		saNtfFinalize(cb->ntfHandle);

	TRACE_LEAVE();

	return rc;
}

/****************************************************************************\
  PROCEDURE NAME : mqd_lib_init

  DESCRIPTION    : This routine initializes the MQD by doing following
		   - Create MQD instance
		   - Create MBX & Threads

  ARGUMENTS      : none

  RETURNS        : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE..
\*****************************************************************************/
static uint32_t mqd_lib_init(void)
{
	uint32_t rc = NCSCC_RC_SUCCESS;
	MQD_CB *pMqd = 0;
#if NCS_2_0 /* Required for NCS 2.0 */
	SaAisErrorT saErr = SA_AIS_OK;
	SaNameT sname;
#endif
	SaAmfHealthcheckKeyT healthy;
	char *health_key = NULL;
	SaAisErrorT amf_error;
	TRACE_ENTER();

	/* Create the MQD Control Block */
	pMqd = m_MMGR_ALLOC_MQD_CB;
	if (!pMqd) {
		LOG_CR("Instance Creation Failed");
		return NCSCC_RC_FAILURE;
	}
	TRACE_1("Instance Creation Success");

	/* Initalize the Control block */
	rc = mqd_cb_init(pMqd);
	if (NCSCC_RC_SUCCESS != rc) { /* Handle failure */
		LOG_ER("Instance Initialization Failed");
		m_MMGR_FREE_MQD_CB(pMqd);
		return rc;
	}
	TRACE_1("Instance Initialization Success");

	/* Register MQD with the handle manager */
	pMqd->hdl =
	    ncshm_create_hdl(pMqd->hmpool, pMqd->my_svc_id, (NCSCONTEXT)pMqd);
	if (!pMqd->hdl) { /* Handle failure */
		LOG_ER("Handle Registration Failed");
		mqd_cb_shut(pMqd);
		return NCSCC_RC_FAILURE;
	}
	gl_mqdinfo.inst_hdl = pMqd->hdl;
	TRACE_1("Handle Registration Success");

	/* Initailize all AVSv callback */
	rc = mqd_amf_init(pMqd);
	if (NCSCC_RC_SUCCESS != rc) { /* Handle failure */
		TRACE_4("AMF Registration Failed");
		mqd_cb_shut(pMqd);
		return rc;
	}
	TRACE_1("Initialization With AMF Successfull");
	/* Create the mail box */
	rc = m_NCS_IPC_CREATE(&pMqd->mbx);
	if (NCSCC_RC_SUCCESS != rc) {
		LOG_ER("IPC creation Failed");
#if NCS_2_0 /* Required for NCS 2.0 */
		saAmfFinalize(pMqd->amf_hdl);
#endif
		mqd_cb_shut(pMqd);
		return rc;
	}

	/* Attach IPC */
	rc = m_NCS_IPC_ATTACH(&pMqd->mbx);
	if (NCSCC_RC_SUCCESS != rc) {
		m_NCS_IPC_RELEASE(&pMqd->mbx, 0);
		LOG_ER("IPC attach Failed");
#if NCS_2_0 /* Required for NCS 2.0 */
		saAmfFinalize(pMqd->amf_hdl);
#endif
		mqd_cb_shut(pMqd);
		return rc;
	}

	strcpy((char *)pMqd->safSpecVer.value, "B.03.01");
	pMqd->safSpecVer.length = strlen("B.03.01");
	strcpy((char *)pMqd->safAgtVen.value, "OpenSAF");
	pMqd->safAgtVen.length = strlen("OpenSAF");
	pMqd->safAgtVenPro = 2;
	pMqd->serv_enabled = false;
	pMqd->serv_state = 1;

	/* Initilize the Layer management variables */
	rc = mqd_lm_init(pMqd);
	if (NCSCC_RC_SUCCESS != rc) { /* Handle failure */
		TRACE_2("Layer Management Initialization Failed");
#if NCS_2_0 /* Required for NCS 2.0 */
		saAmfFinalize(pMqd->amf_hdl);
#endif
		mqd_cb_shut(pMqd);
		return rc;
	}

	/* EDU initialisation */
	m_NCS_EDU_HDL_INIT(&pMqd->edu_hdl);
	TRACE_1("EDU Bind Success");

#if NCS_2_0 /* Not needed for NCS ver(1.0) */
	/* Register MQSv - MQD component with AvSv */
	sname.length = strlen(MQD_COMP_NAME);
	strcpy((char *)sname.value, MQD_COMP_NAME);

	saErr = saAmfComponentRegister(pMqd->amf_hdl, &pMqd->comp_name,
				       (SaNameT *)0);
	if (SA_AIS_OK != saErr) { /* Handle failure */
		LOG_ER("saAmfComponentRegister Failed with error %d", saErr);
		saAmfFinalize(pMqd->amf_hdl);
		ncshm_give_hdl(pMqd->hdl);
		mqd_cb_shut(pMqd);
		return NCSCC_RC_FAILURE;
	}
#endif
	TRACE_1("saAmfComponentRegister Success");

	/* start the AMF Health Check */
	memset(&healthy, 0, sizeof(SaAmfHealthcheckKeyT));
	health_key = getenv("MQSV_ENV_HEALTHCHECK_KEY");
	if (health_key == NULL) {
		strcpy((char *)healthy.key, "E5F6");
	} else {
		strncpy((char *)healthy.key, health_key,
			SA_AMF_HEALTHCHECK_KEY_MAX - 1);
	}
	healthy.keyLen = strlen((char *)healthy.key);

	amf_error = saAmfHealthcheckStart(
	    pMqd->amf_hdl, &pMqd->comp_name, &healthy,
	    SA_AMF_HEALTHCHECK_AMF_INVOKED, SA_AMF_COMPONENT_FAILOVER);
	if (amf_error != SA_AIS_OK) {
		LOG_ER("saAmfHealthcheckStart Failed with error %d", amf_error);
		rc = NCSCC_RC_FAILURE;
		mqd_asapi_unbind();
		saAmfFinalize(pMqd->amf_hdl);
		ncshm_give_hdl(pMqd->hdl);
		mqd_cb_shut(pMqd);
		return rc;
	}

	if (mqd_clm_init(pMqd) != SA_AIS_OK) {
		mqd_asapi_unbind();
		saAmfFinalize(pMqd->amf_hdl);
		ncshm_give_hdl(pMqd->hdl);
		mqd_cb_shut(pMqd);
		return NCSCC_RC_FAILURE;
	}

	if (mqd_ntf_init(pMqd) != SA_AIS_OK) {
		mqd_asapi_unbind();
		saAmfFinalize(pMqd->amf_hdl);
		saClmFinalize(pMqd->clm_hdl);
		ncshm_give_hdl(pMqd->hdl);
		mqd_cb_shut(pMqd);
		return NCSCC_RC_FAILURE;
	}

	if ((rc = initialize_for_assignment(pMqd, pMqd->ha_state)) !=
	    NCSCC_RC_SUCCESS) {
		LOG_ER("initialize_for_assignment FAILED %u", (unsigned)rc);
		exit(EXIT_FAILURE);
	}

	TRACE_1("saAmfHealthcheckStart Success");
	TRACE_LEAVE();
	return rc;
} /* End of mqd_lib_init() */

uint32_t initialize_for_assignment(MQD_CB *cb, SaAmfHAStateT ha_state)
{
	TRACE_ENTER2("ha_state = %d", (int)ha_state);
	uint32_t rc = NCSCC_RC_SUCCESS;
	SaAisErrorT saErr = SA_AIS_OK;

	if (cb->fully_initialized || ha_state == SA_AMF_HA_QUIESCED) {
		goto done;
	}

	rc = mqd_mds_init(cb);
	if (NCSCC_RC_SUCCESS != rc) { /* Handle failure */
		LOG_ER("MDS Initialization Failed");
		goto done;
	}
	TRACE_1("MDS Initialization Success");

	/* Register with MBCSV initialise and open a session and do selection
	 * object*/
	rc = mqd_mbcsv_register(cb);
	if (NCSCC_RC_SUCCESS != rc) {
		LOG_ER("MBCSv registration failed");
		mqd_mds_shut(cb);
		goto done;
	}

	/* MQD Imm Initialization */
	saErr = mqd_imm_initialize(cb);
	if (saErr != SA_AIS_OK) {
		TRACE_2("Imm Initialization Failed %u", saErr);
		mqd_mbcsv_finalize(cb);
		if (mqd_mds_shut(cb) != NCSCC_RC_SUCCESS) {
			TRACE_2("MDS Deregistration Failed");
		}
		saClmFinalize(cb->clm_hdl);
		rc = NCSCC_RC_FAILURE;
		goto done;
	}

	/* Bind with ASAPi Layer */
	mqd_asapi_bind(cb);
	TRACE_1("Initialization Success");

	cb->fully_initialized = true;
done:
	TRACE_LEAVE2("rc = %u", rc);
	return rc;
}

/**************************************************************************** \
  PROCEDURE NAME : mqd_lib_destroy

  DESCRIPTION    : This is the function which destroy the MQD libarary.
		   This function releases the Task and the IPX mail Box.
		   This function unregisters with AMF, destroies handle
		   manager, CB and cleans up all the component specific
		   databases

  ARGUMENTS      : none

  RETURNS        : none..
\*****************************************************************************/
static void mqd_lib_destroy(void)
{
	MQD_CB *pMqd = 0;
	uint32_t rc = NCSCC_RC_SUCCESS;
	TRACE_ENTER();

#if NCS_2_0 /* Required for NCS 2.0 */
	SaNameT sname;
#endif

	pMqd = ncshm_take_hdl(NCS_SERVICE_ID_MQD, gl_mqdinfo.inst_hdl);
	if (pMqd) {
#if NCS_2_0 /* Required for NCS 2.0 */
		sname.length = strlen(MQD_COMP_NAME);
		strcpy((char *)sname.value, MQD_COMP_NAME);

		/* Deregister MQSv - MQD component from AVSv */
		saAmfComponentUnregister(pMqd->amf_hdl, &sname, (SaNameT *)0);
		TRACE_1("saAmfComponentUnregister Success");
		saAmfFinalize(pMqd->amf_hdl);
		TRACE_1("saAmfFinalize Success");
#endif

		/* Deactivate the component */
		pMqd->active = false;

		/* Shut off the MDS */
		rc = mqd_mds_shut(pMqd);
		if (NCSCC_RC_SUCCESS != rc) {
			TRACE_2("MDS Deregistration Failed");
		} else {
			TRACE_1("MDS Deregistration Success");
		}

		/* Release all LM resources */
		mqd_lm_shut(pMqd);
		TRACE_1("Layer Management Destroy Success");

		/* Unbind from ASAPi */
		mqd_asapi_unbind();
		TRACE_1("ASAPi Unbind Success");

		/* Unbind from EDU */
		m_NCS_EDU_HDL_FLUSH(&pMqd->edu_hdl);
		TRACE_1("EDU Unbind Success");

		/* Shut down the MQD Insatnce */
		ncshm_give_hdl(pMqd->hdl);
		mqd_cb_shut(pMqd);
		TRACE_1("Instance Destroy Success");
	} else {
		LOG_ER("%s:%u: Instance Doesn't Exist", __FILE__, __LINE__);
	}

	gl_mqdinfo.inst_hdl = 0; /* MQD Instance destroyed */
	TRACE_LEAVE();
	return;
} /* End of mqd_lib_destroy() */

/****************************************************************************\
 PROCEDURE NAME : mqd_main_process

 DESCRIPTION    : This is the function which is given as a input to the
		  MQD task.
		  This function will be select of both the FD's (AMF FD and
		  Mail Box FD), depending on which FD has been selected, it
		  will call the corresponding routines.

 ARGUMENTS      : info - MQD Controll block pointer

 RETURNS        : None.
\*****************************************************************************/
void mqd_main_process(uint32_t hdl)
{
	MQD_CB *pMqd = NULL;
	NCS_SEL_OBJ mbxFd;
	MQSV_EVT *pEvt = NULL;
	SaAisErrorT err = SA_AIS_OK;
	NCS_MBCSV_ARG mbcsv_arg;
	SaSelectionObjectT amfSelObj;
	int term_fd;

	TRACE_ENTER();
	/* Get the controll block */
	pMqd = ncshm_take_hdl(NCS_SERVICE_ID_MQD, hdl);
	if (!pMqd) {
		LOG_ER("%s:%u: Instance Doesn't Exist", __FILE__, __LINE__);
		return;
	}

	mbxFd = ncs_ipc_get_sel_obj(&pMqd->mbx);

	err = saAmfSelectionObjectGet(pMqd->amf_hdl, &amfSelObj);
	if (SA_AIS_OK != err) {
		LOG_ER("saAmfSelectionObjectGet failed with error %d", err);
		ncshm_give_hdl(pMqd->hdl);
		return;
	}

	daemon_sigterm_install(&term_fd);

	/* Set up all file descriptors to listen to */
	fds[FD_TERM].fd = term_fd;
	fds[FD_TERM].events = POLLIN;
	fds[FD_AMF].fd = amfSelObj;
	fds[FD_AMF].events = POLLIN;
	fds[FD_MBX].fd = mbxFd.rmv_obj;
	fds[FD_MBX].events = POLLIN;

	while (1) {
		fds[FD_CLM].fd = pMqd->clm_sel_obj;
		fds[FD_CLM].events = POLLIN;
		fds[FD_MBCSV].fd = pMqd->mbcsv_sel_obj;
		fds[FD_MBCSV].events = POLLIN;
		fds[FD_NTF].fd = pMqd->ntf_sel_obj;
		fds[FD_NTF].events = POLLIN;

		int ret = poll(fds, nfds, -1);
		if (ret == -1) {
			if (errno == EINTR)
				continue;

			LOG_ER("poll failed - %s", strerror(errno));
			break;
		}

		if (fds[FD_TERM].revents & POLLIN) {
			daemon_exit();
		}

		/* Dispatch all the AMF pending function */
		if (fds[FD_AMF].revents & POLLIN) {
			err = saAmfDispatch(pMqd->amf_hdl, SA_DISPATCH_ALL);
			if (SA_AIS_OK != err)
				/* Log Error */
				LOG_ER("saAmfDispatch FAILED : %u", err);
		}
		/* Process all Clm Messages */
		if (fds[FD_CLM].revents & POLLIN) {
			/* dispatch all the CLM pending function */
			err = saClmDispatch(pMqd->clm_hdl, SA_DISPATCH_ALL);
			if (err != SA_AIS_OK) {
				LOG_ER("saClmDispatch failed: %u", err);
			}
		}

		if (fds[FD_NTF].revents & POLLIN) {
			/* dispatch all the NTF pending function */
			err = saNtfDispatch(pMqd->ntfHandle, SA_DISPATCH_ALL);
			if (err != SA_AIS_OK) {
				LOG_ER("saNtfDispatch failed: %u", err);
			}
		}

		/* dispatch all the MBCSV pending callbacks */
		if (fds[FD_MBCSV].revents & POLLIN) {
			mbcsv_arg.i_op = NCS_MBCSV_OP_DISPATCH;
			mbcsv_arg.i_mbcsv_hdl = pMqd->mbcsv_hdl;
			mbcsv_arg.info.dispatch.i_disp_flags = SA_DISPATCH_ALL;
			if (ncs_mbcsv_svc(&mbcsv_arg) != SA_AIS_OK) {
				LOG_ER(
				    "Dispatching all the MBCSV pending callbacks failed");
			}
		}

		/* process the MQSv Mail box */
		/* Now got the IPC mail box event */
		if (fds[FD_MBX].revents & POLLIN) {
			if (0 != (pEvt = (MQSV_EVT *)m_NCS_IPC_NON_BLK_RECEIVE(
				      &pMqd->mbx, pEvt))) {
				if ((pEvt->type >= MQSV_EVT_BASE) &&
				    (pEvt->type <= MQSV_EVT_MAX)) {
					/* Process Event */
					mqd_evt_process(pEvt);
				} else {
					LOG_ER(
					    "Invalid MQSV_EVT_TYPE Event type");
					/* Freee Event */
				}
			}
		}
	}
	sleep(1);
	TRACE_LEAVE();
	exit(EXIT_FAILURE);
	ncshm_give_hdl(pMqd->hdl);
	return;
} /* End of mqd_main_process() */

/****************************************************************************\
 PROCEDURE NAME : mqd_cb_init

 DESCRIPTION    : This routines initializes the MQD control block

 ARGUMENTS      : pMqd  - MQD Control block

 RETURNS        : NCSCC_RC_FAILURE/NCSCC_RC_SUCCESS.
\*****************************************************************************/
static uint32_t mqd_cb_init(MQD_CB *pMqd)
{
	NCS_PATRICIA_PARAMS params;
	NCS_PATRICIA_PARAMS params_nodedb;
	uint32_t rc = NCSCC_RC_SUCCESS;
	TRACE_ENTER();

	/* Initilize the constants variables */
	memset(pMqd, 0, sizeof(MQD_CB));
	memset(&params, 0, sizeof(params));
	memset(&params_nodedb, 0, sizeof(params_nodedb));

	pMqd->hmpool = NCS_HM_POOL_ID_COMMON;
	pMqd->my_svc_id = NCS_SERVICE_ID_MQD;
	strcpy(pMqd->my_name, MQD_COMP_NAME);

	/* Initilaze the database tree */
	params.key_size = sizeof(SaNameT);
	rc = ncs_patricia_tree_init(&pMqd->qdb, &params);
	if (rc != NCSCC_RC_SUCCESS) {
		TRACE_2("Initilazation of the database tree failed");
		return rc;
	}
	pMqd->qdb_up = true;
	/* Initialize the Node database */
	params_nodedb.key_size = sizeof(NODE_ID);
	rc = ncs_patricia_tree_init(&pMqd->node_db, &params_nodedb);
	if (rc != NCSCC_RC_SUCCESS) {
		TRACE_2("Initilazation of the Node database failed");
		return rc;
	}
	pMqd->node_db_up = true;
	pMqd->ha_state = SA_AMF_HA_QUIESCED;
	pMqd->mbcsv_sel_obj = -1;
	pMqd->imm_sel_obj = -1;
	pMqd->clm_sel_obj = -1;
	pMqd->fully_initialized = false;

	TRACE_LEAVE();
	return rc;
} /* End of mqd_cb_init() */

/****************************************************************************\
 PROCEDURE NAME : mqd_cb_shut

 DESCRIPTION    : This routines destroys the MQD control block

 ARGUMENTS      : pMqd  - MQD Control block

 RETURNS        : none.
\*****************************************************************************/
static void mqd_cb_shut(MQD_CB *pMqd)
{
	ncs_patricia_tree_destroy(&pMqd->qdb);
	ncs_patricia_tree_destroy(&pMqd->node_db);
	if (pMqd->hdl)
		ncshm_destroy_hdl(pMqd->my_svc_id, pMqd->hdl);
	m_MMGR_FREE_MQD_CB(pMqd);
} /* End of mqd_cb_shut() */

/****************************************************************************\
 PROCEDURE NAME : mqd_lm_init

 DESCRIPTION    : This routines creates task for the MQD control block and does
		  the layer management initialization

 ARGUMENTS      : pMqd  - MQD Control block

 RETURNS        : NCSCC_RC_FAILURE/NCSCC_RC_SUCCESS.
\*****************************************************************************/
static uint32_t mqd_lm_init(MQD_CB *pMqd)
{
	uint32_t rc = NCSCC_RC_SUCCESS;

	return rc;
} /* End of mqd_lm_init() */

/****************************************************************************\
 PROCEDURE NAME : mqd_lm_shut

 DESCRIPTION    : This routines deletes the task of the MQD control block and
		  does the layer management cleanup

 ARGUMENTS      : pMqd  - MQD Control block

 RETURNS        : None.
\*****************************************************************************/
static void mqd_lm_shut(MQD_CB *pMqd)
{

	/* Free up all the resources */
	m_NCS_IPC_DETACH(&pMqd->mbx, mqd_clear_mbx, pMqd);
	m_NCS_IPC_RELEASE(&pMqd->mbx, 0);
	return;
} /* End of mqd_lm_shut() */

/****************************************************************************\
 PROCEDURE NAME : mqd_amf_init

 DESCRIPTION    : MQD initializes AMF for involking process and registers
		  the various callback functions

 ARGUMENTS      : pMqd  - MQD Control block

 RETURNS        : NCSCC_RC_FAILURE/NCSCC_RC_SUCCESS.
\*****************************************************************************/
static uint32_t mqd_amf_init(MQD_CB *pMqd)
{
	uint32_t rc = NCSCC_RC_SUCCESS;
#if NCS_2_0 /* Required for NCS 2.0 */
	SaAisErrorT saErr = SA_AIS_OK;
	SaAmfCallbacksT amfCallbacks;
	SaVersionT amfVersion;
	TRACE_ENTER();

	memset(&amfCallbacks, 0, sizeof(SaAmfCallbacksT));

	amfCallbacks.saAmfHealthcheckCallback = mqd_saf_hlth_chk_cb;
	amfCallbacks.saAmfCSISetCallback = mqd_saf_csi_set_cb;
	amfCallbacks.saAmfComponentTerminateCallback =
	    mqd_amf_comp_terminate_callback;
	amfCallbacks.saAmfCSIRemoveCallback = mqd_amf_csi_rmv_callback;

	m_MQSV_GET_AMF_VER(amfVersion);

	saErr = saAmfInitialize(&pMqd->amf_hdl, &amfCallbacks, &amfVersion);
	if (SA_AIS_OK != saErr) {
		LOG_ER("saAmfInitialize Failed with error %d", saErr);
		return NCSCC_RC_FAILURE;
	}
	/* get the component name */
	saErr = saAmfComponentNameGet(pMqd->amf_hdl, &pMqd->comp_name);
	if (saErr != SA_AIS_OK) {
		LOG_ER("saAmfComponentNameGet Failed with error %d", saErr);
		return NCSCC_RC_FAILURE;
	}
#else
	pMqd->ready_state = SA_AMF_IN_SERVICE;
	pMqd->ha_state = SA_AMF_ACTIVE;
	pMqd->active = true;
#endif
	TRACE_LEAVE();
	return rc;
} /* End of mqd_amf_init() */

/****************************************************************************\
 PROCEDURE NAME : mqd_asapi_bind

 DESCRIPTION    : This routines binds the MQD with the ASAPi layer

 ARGUMENTS      : pMqd  - MQD Control block

 RETURNS        : none.
\*****************************************************************************/
static void mqd_asapi_bind(MQD_CB *pMqd)
{
	ASAPi_OPR_INFO opr;

	opr.type = ASAPi_OPR_BIND;
	opr.info.bind.i_indhdlr = 0;
	opr.info.bind.i_mds_hdl = pMqd->my_mds_hdl;
	opr.info.bind.i_mds_id = NCSMDS_SVC_ID_MQD;
	opr.info.bind.i_my_id = pMqd->my_svc_id;
	opr.info.bind.i_mydest = pMqd->my_dest;

	asapi_opr_hdlr(&opr);
	return;
} /* End of mqd_asapi_bind() */

/****************************************************************************\
 PROCEDURE NAME : mqd_asapi_unbind

 DESCRIPTION    : This routines unbinds the MQD from the ASAPi layer

 ARGUMENTS      : pMqd  - MQD Control block

 RETURNS        : none.
\*****************************************************************************/
static void mqd_asapi_unbind(void)
{
	ASAPi_OPR_INFO opr;

	opr.type = ASAPi_OPR_UNBIND;
	asapi_opr_hdlr(&opr);
	return;
} /* End of mqd_asapi_unbind() */

/****************************************************************************\
 PROCEDURE NAME : mqd_clear_mbx

 DESCRIPTION    : This is the function which deletes all the messages from
		  the mail box.

 ARGUMENTS      : arg - argument to be passed.
		  msg - Event pointer.

 RETURNS        : true/false
\*****************************************************************************/
static bool mqd_clear_mbx(NCSCONTEXT arg, NCSCONTEXT msg)
{
	MQSV_EVT *pEvt = (MQSV_EVT *)msg;
	MQSV_EVT *pNext = NULL;

	for (; pEvt;) {
		pNext = (MQSV_EVT *)NCS_INT64_TO_PTR_CAST(pEvt->hook_for_ipc);

		/* Detroy the event */

		pEvt = pNext;
	}
	return true;
} /* End of mqd_clear_mbx() */

/****************************************************************************\
 PROCEDURE NAME : mqd_cb_init

 DESCRIPTION    : This routines does CLM initialization

 ARGUMENTS      : pMqd  - MQD Control block

 RETURNS        : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

\*****************************************************************************/
