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

  This file contains routines used by MQA library for MDS interaction.
..............................................................................

  FUNCTIONS INCLUDED in this module:


******************************************************************************
*/

#include "mqa.h"

extern uint32_t mqa_mds_callback(struct ncsmds_callback_info *info);

static uint32_t mqa_mds_cpy(MQA_CB *cb, MDS_CALLBACK_COPY_INFO *cpy);
static uint32_t mqa_mds_enc(MQA_CB *cb, MDS_CALLBACK_ENC_INFO *info);
static uint32_t mqa_mds_dec(MQA_CB *cb, MDS_CALLBACK_DEC_INFO *info);
static uint32_t mqa_mds_rcv(MQA_CB *cb, MDS_CALLBACK_RECEIVE_INFO *rcv_info);
static uint32_t mqa_mds_svc_evt(MQA_CB *cb,
				MDS_CALLBACK_SVC_EVENT_INFO *svc_evt);
static uint32_t mqa_mds_get_handle(MQA_CB *cb);

MSG_FRMT_VER mqa_mqnd_msg_fmt_table[MQA_WRT_MQND_SUBPART_VER_RANGE] = {
    0, 2}; /*With version 1 it is not backward compatible */
MSG_FRMT_VER mqa_mqd_msg_fmt_table[MQA_WRT_MQD_SUBPART_VER_RANGE] = {
    0, 2}; /*With version 1 it is not backward compatible */

extern uint32_t mqa_mqa_msg_fmt_table[];
/****************************************************************************
 * Name          : mqa_mds_get_handle
 *
 * Description   : This function Gets the Handles of local MDS destination
 *
 * Arguments     : cb   : MQA control Block pointer.
 *
 * Return Values : NCSCC_RC_SUCCESS/Error Code.
 *
 * Notes         : None.
 *****************************************************************************/

uint32_t mqa_mds_get_handle(MQA_CB *cb)
{
	NCSADA_INFO arg;
	uint32_t rc;

	memset(&arg, 0, sizeof(NCSADA_INFO));
	arg.req = NCSADA_GET_HDLS;
	rc = ncsada_api(&arg);

	if (rc != NCSCC_RC_SUCCESS) {
		TRACE_2("MDS registration failed with returncode %u", rc);
		return rc;
	}
	cb->mqa_mds_hdl = arg.info.adest_get_hdls.o_mds_pwe1_hdl;
	TRACE_1("MDS registration Success");
	return rc;
}

/****************************************************************************
  Name          : mqa_mds_register

  Description   : This routine registers the MQA Service with MDS.

  Arguments     : mqa_cb - ptr to the MQA control block

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/

uint32_t mqa_mds_register(MQA_CB *cb)
{
	NCSMDS_INFO svc_info;
	MDS_SVC_ID subs_id[2] = {NCSMDS_SVC_ID_MQND, NCSMDS_SVC_ID_MQD};
	uint32_t rc = NCSCC_RC_SUCCESS;
	TRACE_ENTER();

	/* STEP1: Get the MDS Handle */
	if (mqa_mds_get_handle(cb) != NCSCC_RC_SUCCESS)
		return NCSCC_RC_FAILURE;

	/* memset the svc_info */
	memset(&svc_info, 0, sizeof(NCSMDS_INFO));

	/* STEP 2 : Install on ADEST with MDS with service ID NCSMDS_SVC_ID_MQA.
	 */
	svc_info.i_mds_hdl = cb->mqa_mds_hdl;
	svc_info.i_svc_id = NCSMDS_SVC_ID_MQA;
	svc_info.i_op = MDS_INSTALL;

	svc_info.info.svc_install.i_yr_svc_hdl = cb->agent_handle_id;
	svc_info.info.svc_install.i_install_scope =
	    NCSMDS_SCOPE_NONE; /* node specific */
	svc_info.info.svc_install.i_svc_cb = mqa_mds_callback; /* callback */
	svc_info.info.svc_install.i_mds_q_ownership =
	    false; /* MQA owns the mds queue */
	svc_info.info.svc_install.i_mds_svc_pvt_ver =
	    MQA_PVT_SUBPART_VERSION; /* Private Subpart Version of MQA */

	if ((rc = ncsmds_api(&svc_info)) != NCSCC_RC_SUCCESS) {
		TRACE_2("FAILURE: MDS Service installation Failed");
		return NCSCC_RC_FAILURE;
	}
	cb->mqa_mds_dest = svc_info.info.svc_install.o_dest;

	/* STEP 3: Subscribe to MQND up/down events */
	svc_info.i_op = MDS_SUBSCRIBE;
	svc_info.info.svc_subscribe.i_num_svcs = 1;
	svc_info.info.svc_subscribe.i_scope = NCSMDS_SCOPE_NONE;
	svc_info.info.svc_subscribe.i_svc_ids = &subs_id[0];

	if ((rc = ncsmds_api(&svc_info)) == NCSCC_RC_FAILURE) {
		TRACE_2(
		    "FAILURE: MDS Subscription to MQND up and down events failed");
		goto error;
	}

	svc_info.i_op = MDS_SUBSCRIBE;
	svc_info.info.svc_subscribe.i_num_svcs = 1;
	svc_info.info.svc_subscribe.i_scope = NCSMDS_SCOPE_NONE;
	svc_info.info.svc_subscribe.i_svc_ids = &subs_id[1];

	if ((rc = ncsmds_api(&svc_info)) == NCSCC_RC_FAILURE) {
		TRACE_2(
		    "FAILURE: MDS Subscription to MQD up and down events failed");
		goto error;
	}

	TRACE_LEAVE();
	return NCSCC_RC_SUCCESS;

error:
	/* Uninstall with the mds */
	svc_info.i_op = MDS_UNINSTALL;
	ncsmds_api(&svc_info);

	return NCSCC_RC_FAILURE;
}

/****************************************************************************
 * Name          : mqa_mds_unreg
 *
 * Description   : This function un-registers the MQA Service with MDS.
 *
 * Arguments     : cb   : MQA control Block pointer.
 *
 * Return Values : None
 *
 * Notes         : None.
 *****************************************************************************/
void mqa_mds_unregister(MQA_CB *cb)
{
	NCSMDS_INFO arg;
	uint32_t rc = NCSCC_RC_SUCCESS;

	/* Un-install your service into MDS.
	   No need to cancel the services that are subscribed */
	memset(&arg, 0, sizeof(NCSMDS_INFO));

	arg.i_mds_hdl = cb->mqa_mds_hdl;
	arg.i_svc_id = NCSMDS_SVC_ID_MQA;
	arg.i_op = MDS_UNINSTALL;

	if ((rc = ncsmds_api(&arg)) != NCSCC_RC_SUCCESS) {
		TRACE_2("FAILURE: MDS Deregistration Failed");
	}
	TRACE_1("MDS Deregistration Success");
	return;
}

/****************************************************************************
  Name          : mqa_mds_callback

  Description   : This callback routine will be called by MDS on event arrival

  Arguments     : info - pointer to the mds callback info

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t mqa_mds_callback(struct ncsmds_callback_info *info)
{
	MQA_CB *mqa_cb = NULL;
	uint32_t rc = NCSCC_RC_SUCCESS;

	if (info == NULL) {
		TRACE_2("MDS event is NULL");
		return rc;
	}

	mqa_cb = m_MQSV_MQA_RETRIEVE_MQA_CB;
	if (!mqa_cb) {
		TRACE_2("Control block retrieval failed");
		return m_LEAP_DBG_SINK(rc);
	}

	switch (info->i_op) {
	case MDS_CALLBACK_COPY:
		rc = mqa_mds_cpy(mqa_cb, &info->info.cpy);
		break;
	case MDS_CALLBACK_ENC:
		rc = mqa_mds_enc(mqa_cb, &info->info.enc);
		break;
	case MDS_CALLBACK_DEC:
		rc = mqa_mds_dec(mqa_cb, &info->info.dec);
		break;
	case MDS_CALLBACK_ENC_FLAT:
		rc = mqa_mds_enc(mqa_cb, &info->info.enc_flat);
		break;

	case MDS_CALLBACK_DEC_FLAT:
		rc = mqa_mds_dec(mqa_cb, &info->info.dec_flat);
		break;
	case MDS_CALLBACK_RECEIVE:
		rc = mqa_mds_rcv(mqa_cb, &info->info.receive);
		break;

	case MDS_CALLBACK_SVC_EVENT:
		rc = mqa_mds_svc_evt(mqa_cb, &info->info.svc_evt);
		break;

	default:
		TRACE_2("MDS callback unknown operation");
		break;
	}

	if (rc != NCSCC_RC_SUCCESS) {
		TRACE_2("FAILURE: MDS callback failed");
	}
	TRACE_1("MDS callback Success");

	m_MQSV_MQA_GIVEUP_MQA_CB;

	return rc;
}

/****************************************************************************\
 PROCEDURE NAME : mqa_mds_cpy

 DESCRIPTION    : This rountine is invoked when MQND sender & receiver both on
		  the lies in the same process space.

 ARGUMENTS      : cb : MQA control Block.
		  cpy  : copy info.

 RETURNS        : None

 NOTES          :
\*****************************************************************************/

static uint32_t mqa_mds_cpy(MQA_CB *cb, MDS_CALLBACK_COPY_INFO *cpy)
{
	MQSV_EVT *pEvt = 0;
	uint32_t rc = NCSCC_RC_SUCCESS;
	TRACE_ENTER();

	pEvt = m_MMGR_ALLOC_MQSV_EVT(NCS_SERVICE_ID_MQA);

	if (pEvt) {
		memcpy(pEvt, cpy->i_msg, sizeof(MQSV_EVT));
		if (MQSV_EVT_ASAPI == pEvt->type) {
			pEvt->msg.asapi
			    ->usg_cnt++; /* Increment the use count */
		}
	} else {
		TRACE_4("FAILURE: Event database creation failed");
		rc = NCSCC_RC_FAILURE;
	}

	cpy->o_msg_fmt_ver = MQA_PVT_SUBPART_VERSION;
	cpy->o_cpy = pEvt;

	TRACE_LEAVE();
	return rc;
}

/****************************************************************************
  Name          : mqa_mds_enc

  Description   : This function encodes an events sent from MQA.

  Arguments     : cb    : MQA control Block.
		  enc_info  : Info for encoding

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
static uint32_t mqa_mds_enc(MQA_CB *cb, MDS_CALLBACK_ENC_INFO *enc_info)
{
	MQSV_EVT *msg_ptr;
	EDU_ERR ederror = 0;
	uint32_t rc = NCSCC_RC_SUCCESS;
	TRACE_ENTER();

	msg_ptr = (MQSV_EVT *)enc_info->i_msg;

	/* Get the Msg Format version from the SERVICE_ID &
	 * RMT_SVC_PVT_SUBPART_VERSION */
	if (enc_info->i_to_svc_id == NCSMDS_SVC_ID_MQND) {
		enc_info->o_msg_fmt_ver = m_NCS_ENC_MSG_FMT_GET(
		    enc_info->i_rem_svc_pvt_ver,
		    MQA_WRT_MQND_SUBPART_VER_AT_MIN_MSG_FMT,
		    MQA_WRT_MQND_SUBPART_VER_AT_MAX_MSG_FMT,
		    mqa_mqnd_msg_fmt_table);
	} else if (enc_info->i_to_svc_id == NCSMDS_SVC_ID_MQD) {
		enc_info->o_msg_fmt_ver = m_NCS_ENC_MSG_FMT_GET(
		    enc_info->i_rem_svc_pvt_ver,
		    MQA_WRT_MQD_SUBPART_VER_AT_MIN_MSG_FMT,
		    MQA_WRT_MQD_SUBPART_VER_AT_MAX_MSG_FMT,
		    mqa_mqd_msg_fmt_table);
	} else if (enc_info->i_to_svc_id == NCSMDS_SVC_ID_MQA) {
		enc_info->o_msg_fmt_ver = m_NCS_ENC_MSG_FMT_GET(
		    enc_info->i_rem_svc_pvt_ver,
		    MQA_WRT_MQA_SUBPART_VER_AT_MIN_MSG_FMT,
		    MQA_WRT_MQA_SUBPART_VER_AT_MAX_MSG_FMT,
		    mqa_mqa_msg_fmt_table);
	}

	if (enc_info->o_msg_fmt_ver) {
		rc = (m_NCS_EDU_EXEC(&cb->edu_hdl, mqsv_edp_mqsv_evt,
				     enc_info->io_uba, EDP_OP_TYPE_ENC, msg_ptr,
				     &ederror));
		if (rc != NCSCC_RC_SUCCESS)
			TRACE_2("FAILURE: MDS Encoding Failed");
		TRACE_LEAVE2(" return code %d", rc);
		return rc;
	} else {
		/* Drop The Message */
		TRACE_2("FAILURE: Message Format version Invalid %u",
			enc_info->o_msg_fmt_ver);
		return NCSCC_RC_FAILURE;
	}
}

/****************************************************************************
  Name          : mqa_mds_dec

  Description   : This function decodes an events sent to MQA.

  Arguments     : cb    : MQA control Block.
		  dec_info  : Info for decoding

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
static uint32_t mqa_mds_dec(MQA_CB *cb, MDS_CALLBACK_DEC_INFO *dec_info)
{
	MQSV_EVT *msg_ptr;
	EDU_ERR ederror = 0;
	uint32_t rc = NCSCC_RC_SUCCESS;
	bool is_valid_msg_fmt = false;
	TRACE_ENTER();

	if (dec_info->i_fr_svc_id == NCSMDS_SVC_ID_MQND) {
		is_valid_msg_fmt = m_NCS_MSG_FORMAT_IS_VALID(
		    dec_info->i_msg_fmt_ver,
		    MQA_WRT_MQND_SUBPART_VER_AT_MIN_MSG_FMT,
		    MQA_WRT_MQND_SUBPART_VER_AT_MAX_MSG_FMT,
		    mqa_mqnd_msg_fmt_table);
	} else if (dec_info->i_fr_svc_id == NCSMDS_SVC_ID_MQD) {
		is_valid_msg_fmt = m_NCS_MSG_FORMAT_IS_VALID(
		    dec_info->i_msg_fmt_ver,
		    MQA_WRT_MQD_SUBPART_VER_AT_MIN_MSG_FMT,
		    MQA_WRT_MQD_SUBPART_VER_AT_MAX_MSG_FMT,
		    mqa_mqd_msg_fmt_table);
	} else if (dec_info->i_fr_svc_id == NCSMDS_SVC_ID_MQA) {
		is_valid_msg_fmt = m_NCS_MSG_FORMAT_IS_VALID(
		    dec_info->i_msg_fmt_ver,
		    MQA_WRT_MQA_SUBPART_VER_AT_MIN_MSG_FMT,
		    MQA_WRT_MQA_SUBPART_VER_AT_MAX_MSG_FMT,
		    mqa_mqa_msg_fmt_table);
	}

	if (is_valid_msg_fmt && (dec_info->i_msg_fmt_ver != 1)) {
		msg_ptr = m_MMGR_ALLOC_MQA_EVT;
		if (!msg_ptr) {
			TRACE_4("FAILURE: Event database creation failed");
			rc = NCSCC_RC_FAILURE;
			return rc;
		}

		memset(msg_ptr, 0, sizeof(MQSV_EVT));
		dec_info->o_msg = (NCSCONTEXT)msg_ptr;

		rc = m_NCS_EDU_EXEC(&cb->edu_hdl, mqsv_edp_mqsv_evt,
				    dec_info->io_uba, EDP_OP_TYPE_DEC,
				    (MQSV_EVT **)&dec_info->o_msg, &ederror);
		if (rc != NCSCC_RC_SUCCESS) {
			TRACE_2("FAILURE: MDS Decoding Failed");
			m_MMGR_FREE_MQA_EVT(dec_info->o_msg);
		}
		TRACE_LEAVE2(" return code %d", rc);
		return rc;
	} else {
		/* Drop The Message */
		TRACE_2("FAILURE: Message Format version Invalid %u",
			is_valid_msg_fmt);
		return NCSCC_RC_FAILURE;
	}
}

/****************************************************************************
 * Name          : mqa_mds_rcv
 *
 * Description   : MDS will call this function on receiving MQND/ASAPi messages.
 *
 * Arguments     : cb - MQA Control Block
 *                 rcv_info - MDS Receive information.
 *
 * Return Values : NCSCC_RC_SUCCESS/Error Code.
 *
 * Notes         : None.
 *****************************************************************************/

static uint32_t mqa_mds_rcv(MQA_CB *cb, MDS_CALLBACK_RECEIVE_INFO *rcv_info)
{
	uint32_t rc = NCSCC_RC_SUCCESS;

	MQSV_EVT *evt = (MQSV_EVT *)rcv_info->i_msg;
	MQP_ASYNC_RSP_MSG *mqa_callbk_info;

	evt->sinfo.ctxt = rcv_info->i_msg_ctxt;
	evt->sinfo.dest = rcv_info->i_fr_dest;
	evt->sinfo.to_svc = rcv_info->i_fr_svc_id;
	if (rcv_info->i_rsp_reqd) {
		evt->sinfo.stype = MDS_SENDTYPE_RSP;
	}

	/* check the event type */
	if (evt->type == MQSV_EVT_MQA_CALLBACK) {
		mqa_callbk_info = m_MMGR_ALLOC_MQP_ASYNC_RSP_MSG;
		if (!mqa_callbk_info) {
			TRACE_4(
			    "FAILURE: MQP Async Rsp Message Allocation Failed");
			return NCSCC_RC_FAILURE;
		}
		memcpy(mqa_callbk_info, &evt->msg.mqp_async_rsp,
		       sizeof(MQP_ASYNC_RSP_MSG));

		/* Stop the timer that was started for this request by matching
		 * the invocation in the timer table.
		 */
		if ((rc = mqa_stop_and_delete_timer(mqa_callbk_info)) ==
		    NCSCC_RC_SUCCESS) {
			/* Put it in place it in the Queue */
			rc = mqsv_mqa_callback_queue_write(
			    cb, evt->msg.mqp_async_rsp.messageHandle,
			    mqa_callbk_info);
			TRACE_1("Stop and Delete Tmr Success");
		} else {
			TRACE_2("FAILURE: Stop and Delete Tmr Failed");
			if (mqa_callbk_info)
				m_MMGR_FREE_MQP_ASYNC_RSP_MSG(mqa_callbk_info);
		}
		if (evt)
			m_MMGR_FREE_MQA_EVT(evt);
		TRACE("mqa_mds_rcv is returned with returncode %u", rc);
		return rc;
	} else if (evt->type == MQSV_EVT_ASAPI) {
		ASAPi_OPR_INFO opr;

		opr.type = ASAPi_OPR_MSG;
		opr.info.msg.opr = ASAPI_MSG_RECEIVE;
		opr.info.msg.resp = ((MQSV_EVT *)rcv_info->i_msg)->msg.asapi;

		rc = asapi_opr_hdlr(&opr);
		if (rc != NCSCC_RC_SUCCESS)
			TRACE_2(
			    "ASAPi Message Receive Failed with the returnvalue %d",
			    rc);
		if (evt)
			m_MMGR_FREE_MQA_EVT(evt);
		TRACE("mqa_mds_rcv is returned with returncode %u", rc);
		return rc;

	} else if (evt->type == MQSV_EVT_MQP_REQ) {
		cb->clm_node_joined =
		    evt->msg.mqp_req.info.clmNotify.node_joined;

		if (!cb->clm_node_joined) {
			/* tell all clients they are stale now */
                        MQA_CLIENT_INFO *client_info;
                        SaMsgHandleT msgHandle = 0;
                        for (client_info =
                                 mqa_client_tree_find_next(cb, msgHandle);
                             client_info;
                             client_info = mqa_client_tree_find_next(
                                 cb, client_info->msgHandle)) {
                                client_info->isStale = true;
                        }
                }

		return rc;
	}

	if (evt)
		m_MMGR_FREE_MQA_EVT(evt);

	TRACE_2("mqa_mds_rcv returned - FAILURE");
	return NCSCC_RC_FAILURE;
}

/****************************************************************************
 * Name          : mqa_mds_svc_evt
 *
 * Description   : MQA is informed when MDS events occurr that he has
 *                 subscribed to
 *
 * Arguments     :
 *   cb          : MQA control Block.
 *   svc_evt    : Svc evt info.
 *
 * Return Values : None
 *
 * Notes         : None.
 *****************************************************************************/

static uint32_t mqa_mds_svc_evt(MQA_CB *cb,
				MDS_CALLBACK_SVC_EVENT_INFO *svc_evt)
{
	uint32_t to_dest_slotid, o_msg_fmt_ver;
	TRACE_ENTER();

	/* TBD: The MQND and MQD restarts are to be implemented post April
	 * release */
	switch (svc_evt->i_change) {
	case NCSMDS_DOWN:
		switch (svc_evt->i_svc_id) {
		case NCSMDS_SVC_ID_MQND:

			cb->ver_mqnd[mqsv_get_phy_slot_id(svc_evt->i_dest)] = 0;

			TRACE_2("MQND is down with nodeid %" PRIx64,
				svc_evt->i_dest);
			if (m_NCS_NODE_ID_FROM_MDS_DEST(cb->mqa_mds_dest) ==
			    m_NCS_NODE_ID_FROM_MDS_DEST(svc_evt->i_dest)) {
				cb->is_mqnd_up = false;
			}
			break;
		case NCSMDS_SVC_ID_MQD:
			cb->is_mqd_up = false;
			TRACE_2("MQD is down");
			break;

		default:
			break;
		}

		break;
	case NCSMDS_UP:
		switch (svc_evt->i_svc_id) {
		case NCSMDS_SVC_ID_MQND:
			to_dest_slotid = mqsv_get_phy_slot_id(svc_evt->i_dest);
			cb->ver_mqnd[to_dest_slotid] =
			    svc_evt->i_rem_svc_pvt_ver;

			o_msg_fmt_ver = m_NCS_ENC_MSG_FMT_GET(
			    svc_evt->i_rem_svc_pvt_ver,
			    MQA_WRT_MQND_SUBPART_VER_AT_MIN_MSG_FMT,
			    MQA_WRT_MQND_SUBPART_VER_AT_MAX_MSG_FMT,
			    mqa_mqnd_msg_fmt_table);

			if (!o_msg_fmt_ver)
				/*Log informing the existence of Non compatible
				 * MQND version, Slot id being logged */
				TRACE_2("Message Format version Invalid %u",
					o_msg_fmt_ver);

			if (m_NCS_NODE_ID_FROM_MDS_DEST(cb->mqa_mds_dest) ==
			    m_NCS_NODE_ID_FROM_MDS_DEST(svc_evt->i_dest)) {
				m_NCS_LOCK(&cb->mqnd_sync_lock, NCS_LOCK_WRITE);

				cb->mqnd_mds_dest = svc_evt->i_dest;
				cb->is_mqnd_up = true;

				if (cb->mqnd_sync_awaited == true) {
					m_NCS_SEL_OBJ_IND(&cb->mqnd_sync_sel);
				}

				m_NCS_UNLOCK(&cb->mqnd_sync_lock,
					     NCS_LOCK_WRITE);
			}
			TRACE_1("MQND is up on the nodeid %" PRIx64,
				svc_evt->i_dest);
			break;

		case NCSMDS_SVC_ID_MQD:
			m_NCS_LOCK(&cb->mqd_sync_lock, NCS_LOCK_WRITE);

			to_dest_slotid = mqsv_get_phy_slot_id(svc_evt->i_dest);

			o_msg_fmt_ver = m_NCS_ENC_MSG_FMT_GET(
			    svc_evt->i_rem_svc_pvt_ver,
			    MQA_WRT_MQD_SUBPART_VER_AT_MIN_MSG_FMT,
			    MQA_WRT_MQD_SUBPART_VER_AT_MAX_MSG_FMT,
			    mqa_mqd_msg_fmt_table);

			if (!o_msg_fmt_ver)
				/*Log informing the existence of Non compatible
				 * MQD version, Slot id being logged */
				TRACE_2("Message Format version Invalid %u",
					o_msg_fmt_ver);

			cb->mqd_mds_dest = svc_evt->i_dest;
			cb->is_mqd_up = true;

			if (cb->mqd_sync_awaited == true) {
				m_NCS_SEL_OBJ_IND(&cb->mqd_sync_sel);
			}
			TRACE_1("MQD is up");

			m_NCS_UNLOCK(&cb->mqd_sync_lock, NCS_LOCK_WRITE);
			break;

		default:
			break;
		}
		break;
	default:
		break;
	}

	TRACE_LEAVE();
	return NCSCC_RC_SUCCESS;
}

/****************************************************************************
  Name          : mqa_mds_msg_sync_send

  Description   : This routine sends the MQA message to MQND.

  Arguments     :
		  NCSCONTEXT mqa_mds_hdl Handle of MQA
		  MDS_DEST  *destination - destintion to send to
		  MQSV_EVT   *i_evt - MQSV_EVT pointer
		  MQSV_EVT   **o_evt - MQSV_EVT pointer to result data
		  timeout - timeout value in 10 ms

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t mqa_mds_msg_sync_send(uint32_t mqa_mds_hdl, MDS_DEST *destination,
			       MQSV_EVT *i_evt, MQSV_EVT **o_evt,
			       SaTimeT timeout)
{

	NCSMDS_INFO mds_info;
	uint32_t rc;
	MQA_CB *mqa_cb;
	TRACE_ENTER();

	if (!i_evt) {
		TRACE_2("FAILURE: MQSV_EVT is NULL");
		return NCSCC_RC_FAILURE;
	}

	/* retrieve MQA CB */
	mqa_cb = (MQA_CB *)m_MQSV_MQA_RETRIEVE_MQA_CB;

	if (!mqa_cb) {
		TRACE_2("FAILURE: Control block retrieval failed");
		return NCSCC_RC_FAILURE;
	}

	/* Before entering any mds send function, the API locks the control
	 * block. unlock the control block before send and lock it after we
	 * receive the reply
	 */

	/* get the client_info */
	if (m_NCS_UNLOCK(&mqa_cb->cb_lock, NCS_LOCK_WRITE) !=
	    NCSCC_RC_SUCCESS) {
		TRACE_4("FAILURE: Lock failed for control block write");
		m_MQSV_MQA_GIVEUP_MQA_CB;
		return NCSCC_RC_FAILURE;
	}

	memset(&mds_info, 0, sizeof(NCSMDS_INFO));
	mds_info.i_mds_hdl = mqa_mds_hdl;
	mds_info.i_svc_id = NCSMDS_SVC_ID_MQA;
	mds_info.i_op = MDS_SEND;

	/* fill the send structure */
	mds_info.info.svc_send.i_msg = (NCSCONTEXT)i_evt;
	mds_info.info.svc_send.i_priority = MDS_SEND_PRIORITY_MEDIUM;
	mds_info.info.svc_send.i_to_svc = NCSMDS_SVC_ID_MQND;
	mds_info.info.svc_send.i_sendtype = MDS_SENDTYPE_SNDRSP;

	/* fill the send rsp strcuture */
	mds_info.info.svc_send.info.sndrsp.i_time_to_wait =
	    timeout; /* timeto wait in 10ms */
	mds_info.info.svc_send.info.sndrsp.i_to_dest = *destination;

	/* send the message */
	rc = ncsmds_api(&mds_info);
	m_NCS_LOCK(&mqa_cb->cb_lock, NCS_LOCK_WRITE);

	if (rc == NCSCC_RC_SUCCESS)
		*o_evt = mds_info.info.svc_send.info.sndrsp.o_rsp;
	else
		TRACE_2("FAILURE: Message Send through MDS Failure");

	m_MQSV_MQA_GIVEUP_MQA_CB;
	TRACE_LEAVE();
	return rc;
}

/****************************************************************************
  Name          : mqa_mds_msg_sync_send_direct

  Description   : This routine sends the MQA message to MQND.

  Arguments     :
		  NCSCONTEXT mqa_mds_hdl Handle of MQA
		  MDS_DEST  *destination - destintion to send to
		  MQSV_EVT   *i_evt - MQSV_EVT pointer
		  MQSV_EVT   **o_evt - MQSV_EVT pointer to result data
		  timeout - timeout value in 10 ms

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t mqa_mds_msg_sync_send_direct(uint32_t mqa_mds_hdl,
				      MDS_DEST *destination,
				      MQSV_DSEND_EVT *i_evt,
				      MQSV_DSEND_EVT **o_evt, SaTimeT timeout,
				      uint32_t length)
{
	NCSMDS_INFO mds_info;
	uint32_t rc;
	MQA_CB *mqa_cb;
	MQSV_DSEND_EVT *pEvt = NULL;
	bool endianness = machineEndianness(), is_valid_msg_fmt = false;
	TRACE_ENTER();

	if (!i_evt) {
		TRACE_2("FAILURE: MQSV_DSEND_EVT is NULL");
		return NCSCC_RC_FAILURE;
	}

	/* retrieve MQA CB */
	mqa_cb = (MQA_CB *)m_MQSV_MQA_RETRIEVE_MQA_CB;

	if (!mqa_cb) {
		TRACE_2("FAILURE: Control block retrieval failed");
		mds_free_direct_buff((MDS_DIRECT_BUFF)i_evt);
		return NCSCC_RC_FAILURE;
	}

	/* Before entering any mds send function, the API locks the control
	 * block. unlock the control block before send and lock it after we
	 * receive the reply */
	if (m_NCS_UNLOCK(&mqa_cb->cb_lock, NCS_LOCK_WRITE) !=
	    NCSCC_RC_SUCCESS) {
		TRACE_4("FAILURE: Lock failed for control block write");
		m_MQSV_MQA_GIVEUP_MQA_CB;
		mds_free_direct_buff((MDS_DIRECT_BUFF)i_evt);
		return NCSCC_RC_FAILURE;
	}

	memset(&mds_info, 0, sizeof(NCSMDS_INFO));
	mds_info.i_mds_hdl = mqa_mds_hdl;
	mds_info.i_svc_id = NCSMDS_SVC_ID_MQA;
	mds_info.i_op = MDS_DIRECT_SEND;

	/* fill the send structure */
	mds_info.info.svc_direct_send.i_direct_buff = (NCSCONTEXT)i_evt;
	mds_info.info.svc_direct_send.i_direct_buff_len = length;

	mds_info.info.svc_direct_send.i_priority = MDS_SEND_PRIORITY_MEDIUM;
	mds_info.info.svc_direct_send.i_to_svc = NCSMDS_SVC_ID_MQND;
	mds_info.info.svc_direct_send.i_sendtype = MDS_SENDTYPE_SNDRSP;
	mds_info.info.svc_direct_send.i_msg_fmt_ver = i_evt->msg_fmt_version;
	/* fill the sendinfo  strcuture */
	mds_info.info.svc_direct_send.info.sndrsp.i_to_dest = *destination;
	mds_info.info.svc_direct_send.info.sndrsp.i_time_to_wait =
	    timeout; /* timeto wait in 10ms */

	/* send the message */
	rc = ncsmds_api(&mds_info);

	m_NCS_LOCK(&mqa_cb->cb_lock, NCS_LOCK_WRITE);

	if (rc == NCSCC_RC_SUCCESS) {

		is_valid_msg_fmt = m_NCS_MSG_FORMAT_IS_VALID(
		    mds_info.info.svc_direct_send.i_msg_fmt_ver,
		    MQA_WRT_MQND_SUBPART_VER_AT_MIN_MSG_FMT,
		    MQA_WRT_MQND_SUBPART_VER_AT_MAX_MSG_FMT,
		    mqa_mqnd_msg_fmt_table);

		if (!is_valid_msg_fmt ||
		    (mds_info.info.svc_direct_send.i_msg_fmt_ver == 1)) {
			/* Drop The Message */
			TRACE_2("FAILURE: Message Format version Invalid");
			m_MQSV_MQA_GIVEUP_MQA_CB;
			return NCSCC_RC_FAILURE;
		}

		pEvt = (MQSV_DSEND_EVT *)
			   mds_info.info.svc_direct_send.info.sndrsp.buff;

		if (pEvt->endianness != endianness) {

			pEvt->type.rsp_type =
			    m_MQSV_REVERSE_ENDIAN_L(&pEvt->type, endianness);
			if (pEvt->type.rsp_type == MQP_EVT_SEND_MSG_RSP) {
				/* saMsgMessageSend response from MQND */
				pEvt->info.sendMsgRsp.error =
				    m_MQSV_REVERSE_ENDIAN_L(
					&pEvt->info.sendMsgRsp.error,
					endianness);
				pEvt->info.sendMsgRsp.msgHandle =
				    m_MQSV_REVERSE_ENDIAN_LL(
					&pEvt->info.sendMsgRsp.msgHandle,
					endianness);
			} else {
				/* Reply message from
				 * saMsgMessageReply/ReplyAsync */

				pEvt->agent_mds_dest = m_MQSV_REVERSE_ENDIAN_LL(
				    &pEvt->agent_mds_dest, endianness);

				switch (pEvt->type.req_type) {

				case MQP_EVT_REPLY_MSG: {
					pEvt->info.replyMsg.ackFlags =
					    m_MQSV_REVERSE_ENDIAN_L(
						&pEvt->info.replyMsg.ackFlags,
						endianness);

					pEvt->info.replyMsg.message
					    .type = m_MQSV_REVERSE_ENDIAN_L(
					    &pEvt->info.replyMsg.message.type,
					    endianness);

					pEvt->info.replyMsg.message.version =
					    m_MQSV_REVERSE_ENDIAN_L(
						&pEvt->info.replyMsg.message
						     .version,
						endianness);

					pEvt->info.replyMsg.message
					    .size = m_MQSV_REVERSE_ENDIAN_LL(
					    &pEvt->info.replyMsg.message.size,
					    endianness);

					pEvt->info.replyMsg.message.senderName
					    .length = m_MQSV_REVERSE_ENDIAN_S(
					    &pEvt->info.replyMsg.message
						 .senderName.length,
					    endianness);

					pEvt->info.replyMsg.msgHandle =
					    m_MQSV_REVERSE_ENDIAN_LL(
						&pEvt->info.replyMsg.msgHandle,
						endianness);

					pEvt->info.replyMsg.messageInfo
					    .sendReceive =
					    m_MQSV_REVERSE_ENDIAN_L(
						&pEvt->info.replyMsg.messageInfo
						     .sendReceive,
						endianness);

					pEvt->info.replyMsg.messageInfo.sender
					    .senderId =
					    m_MQSV_REVERSE_ENDIAN_LL(
						&pEvt->info.replyMsg.messageInfo
						     .sender.senderId,
						endianness);

					pEvt->info.replyMsg.messageInfo
					    .sendTime =
					    m_MQSV_REVERSE_ENDIAN_LL(
						&pEvt->info.replyMsg.messageInfo
						     .sendTime,
						endianness);
				} break;

				case MQP_EVT_REPLY_MSG_ASYNC: {
					pEvt->info.replyAsyncMsg.reply
					    .ackFlags = m_MQSV_REVERSE_ENDIAN_L(
					    &pEvt->info.replyAsyncMsg.reply
						 .ackFlags,
					    endianness);

					pEvt->info.replyAsyncMsg.invocation =
					    m_MQSV_REVERSE_ENDIAN_LL(
						&pEvt->info.replyAsyncMsg
						     .invocation,
						endianness);

					pEvt->info.replyAsyncMsg.reply.message
					    .type = m_MQSV_REVERSE_ENDIAN_L(
					    &pEvt->info.replyAsyncMsg.reply
						 .message.type,
					    endianness);

					pEvt->info.replyAsyncMsg.reply.message
					    .version = m_MQSV_REVERSE_ENDIAN_L(
					    &pEvt->info.replyAsyncMsg.reply
						 .message.version,
					    endianness);

					pEvt->info.replyAsyncMsg.reply.message
					    .size = m_MQSV_REVERSE_ENDIAN_LL(
					    &pEvt->info.replyAsyncMsg.reply
						 .message.size,
					    endianness);

					pEvt->info.replyAsyncMsg.reply.message
					    .senderName.length =
					    m_MQSV_REVERSE_ENDIAN_S(
						&pEvt->info.replyAsyncMsg.reply
						     .message.senderName.length,
						endianness);

					pEvt->info.replyAsyncMsg.reply
					    .msgHandle =
					    m_MQSV_REVERSE_ENDIAN_LL(
						&pEvt->info.replyAsyncMsg.reply
						     .msgHandle,
						endianness);

					pEvt->info.replyAsyncMsg.reply
					    .messageInfo.sendReceive =
					    m_MQSV_REVERSE_ENDIAN_LL(
						&pEvt->info.replyAsyncMsg.reply
						     .messageInfo.sendReceive,
						endianness);

					pEvt->info.replyAsyncMsg.reply
					    .messageInfo.sender.senderId =
					    m_MQSV_REVERSE_ENDIAN_LL(
						&pEvt->info.replyAsyncMsg.reply
						     .messageInfo.sender
						     .senderId,
						endianness);

					pEvt->info.replyAsyncMsg.reply
					    .messageInfo.sendTime =
					    m_MQSV_REVERSE_ENDIAN_LL(
						&pEvt->info.replyAsyncMsg.reply
						     .messageInfo.sendTime,
						endianness);
				} break;
				default:
					return NCSCC_RC_FAILURE;
				}
			}
		}

		*o_evt = (MQSV_DSEND_EVT *)
			     mds_info.info.svc_direct_send.info.sndrsp.buff;
	} else
		TRACE_2("FAILURE: Message Send through MDS Failure");

	m_MQSV_MQA_GIVEUP_MQA_CB;
	TRACE_LEAVE();
	return rc;
}

/****************************************************************************
  Name          : mqa_mds_msg_sync_reply_direct

  Description   : This routine sends the MQA message to MQND.

  Arguments     :
		  uint32_t mqa_mds_hdl Handle of MQA
		  MDS_DEST  *destination - destintion to send to
		  MQSV_EVT   *i_evt - MQSV_EVT pointer
		  timeout - timeout value in 10 ms
		 MDS_SYNC_SND_CTXT *context - context of MDS

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t mqa_mds_msg_sync_reply_direct(uint32_t mqa_mds_hdl,
				       MDS_DEST *destination,
				       MQSV_DSEND_EVT *i_evt, SaTimeT timeout,
				       MDS_SYNC_SND_CTXT *context,
				       uint32_t length)
{
	NCSMDS_INFO mds_info;
	uint32_t rc;
	MQA_CB *mqa_cb;
	TRACE_ENTER();

	if (!i_evt) {
		TRACE_2("FAILURE: MQSV_DSEND_EVT is NULL");
		return NCSCC_RC_FAILURE;
	}

	/* retrieve MQA CB */
	mqa_cb = (MQA_CB *)m_MQSV_MQA_RETRIEVE_MQA_CB;

	if (!mqa_cb) {
		TRACE_2("FAILURE: Control block retrieval failed");
		mds_free_direct_buff((MDS_DIRECT_BUFF)i_evt);
		return NCSCC_RC_FAILURE;
	}

	/* Before entering any mds send function, the caller locks the control
	   block with LOCK_WRITE. Unlock the control block before MDS send and
	   lock it after we receive the reply * from MDS.  */
	if (m_NCS_UNLOCK(&mqa_cb->cb_lock, NCS_LOCK_WRITE) !=
	    NCSCC_RC_SUCCESS) {
		TRACE_4("FAILURE: Lock failed for control block write");
		m_MQSV_MQA_GIVEUP_MQA_CB;
		mds_free_direct_buff((MDS_DIRECT_BUFF)i_evt);
		return NCSCC_RC_FAILURE;
	}

	memset(&mds_info, 0, sizeof(NCSMDS_INFO));
	mds_info.i_mds_hdl = mqa_mds_hdl;
	mds_info.i_svc_id = NCSMDS_SVC_ID_MQA;
	mds_info.i_op = MDS_DIRECT_SEND;

	/* fill the send structure */
	mds_info.info.svc_direct_send.i_direct_buff = (NCSCONTEXT)i_evt;
	mds_info.info.svc_direct_send.i_direct_buff_len = length;

	mds_info.info.svc_direct_send.i_priority = MDS_SEND_PRIORITY_MEDIUM;
	mds_info.info.svc_direct_send.i_to_svc = NCSMDS_SVC_ID_MQA;
	mds_info.info.svc_direct_send.i_msg_fmt_ver = i_evt->msg_fmt_version;

	mds_info.info.svc_direct_send.i_sendtype = MDS_SENDTYPE_SNDRACK;

	/* fill the sendinfo  strcuture */
	mds_info.info.svc_direct_send.info.sndrack.i_msg_ctxt = *context;
	mds_info.info.svc_direct_send.info.sndrack.i_sender_dest = *destination;
	mds_info.info.svc_direct_send.info.sndrack.i_time_to_wait =
	    timeout; /* timeto wait in 10ms */

	/* send the message. If MDS returns successfully, we assume that
	 * the reply message has been successfully delivered to the sender.  */
	rc = ncsmds_api(&mds_info);

	if (rc != NCSCC_RC_SUCCESS)
		TRACE_2("FAILURE: Message Send through MDS Failure");

	m_NCS_LOCK(&mqa_cb->cb_lock, NCS_LOCK_WRITE);
	m_MQSV_MQA_GIVEUP_MQA_CB;
	TRACE_LEAVE();
	return rc;
}

/****************************************************************************
  Name          : mqa_mds_msg_async_send

  Description   : This routine sends the MQA message to MQND.

  Arguments     : uint32_t mqa_mds_hdl Handle of MQA
		  MDS_DEST  *destination - destintion to send to
		  MQSV_EVT   *i_evt - MQSV_EVT pointer

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t mqa_mds_msg_async_send(uint32_t mqa_mds_hdl, MDS_DEST *destination,
				MQSV_EVT *i_evt, uint32_t to_svc)
{

	NCSMDS_INFO mds_info;
	uint32_t rc;
	MQA_CB *mqa_cb;
	TRACE_ENTER();

	if (!i_evt) {
		TRACE_2("FAILURE: MQSV_EVT is NULL");
		return NCSCC_RC_FAILURE;
	}

	/* retrieve MQA CB */
	mqa_cb = (MQA_CB *)m_MQSV_MQA_RETRIEVE_MQA_CB;

	if (!mqa_cb) {
		TRACE_2("FAILURE: Control block retrieval failed");
		return NCSCC_RC_FAILURE;
	}

	/* Before entering any mds send function, the API locks the control
	 * block. unlock the control block before send and lock it after we
	 * receive the reply
	 */

	/* get the client_info */
	if (m_NCS_UNLOCK(&mqa_cb->cb_lock, NCS_LOCK_WRITE) !=
	    NCSCC_RC_SUCCESS) {
		TRACE_4("FAILURE: Lock failed for control block write");
		m_MQSV_MQA_GIVEUP_MQA_CB;
		return NCSCC_RC_FAILURE;
	}

	memset(&mds_info, 0, sizeof(NCSMDS_INFO));
	mds_info.i_mds_hdl = mqa_mds_hdl;
	mds_info.i_svc_id = NCSMDS_SVC_ID_MQA;
	mds_info.i_op = MDS_SEND;

	/* fill the send structure */
	mds_info.info.svc_send.i_msg = (NCSCONTEXT)i_evt;
	mds_info.info.svc_send.i_priority = MDS_SEND_PRIORITY_MEDIUM;
	mds_info.info.svc_send.i_to_svc = to_svc;
	mds_info.info.svc_send.i_sendtype = MDS_SENDTYPE_SND;

	/* fill the send rsp strcuture */
	mds_info.info.svc_send.info.snd.i_to_dest = *destination;

	/* send the message */
	rc = ncsmds_api(&mds_info);

	if (rc != NCSCC_RC_SUCCESS)
		TRACE_2("FAILURE: Message Send through MDS Failure");

	m_NCS_LOCK(&mqa_cb->cb_lock, NCS_LOCK_WRITE);
	m_MQSV_MQA_GIVEUP_MQA_CB;
	TRACE_LEAVE();
	return rc;
}

/****************************************************************************
  Name          : mqa_mds_msg_async_send_direct

  Description   : This routine sends the MQA message to MQND.

  Arguments     : uint32_t mqa_mds_hdl Handle of MQA
		  MDS_DEST  *destination - destintion to send to
		  MQSV_EVT   *i_evt - MQSV_EVT pointer

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/

uint32_t mqa_mds_msg_async_send_direct(uint32_t mqa_mds_hdl,
				       MDS_DEST *destination,
				       MQSV_DSEND_EVT *i_evt, uint32_t to_svc,
				       MDS_SEND_PRIORITY_TYPE priority,
				       uint32_t length)
{
	NCSMDS_INFO mds_info;
	uint32_t rc;
	MQA_CB *mqa_cb;
	TRACE_ENTER();

	if (!i_evt) {
		TRACE_2("FAILURE: MQSV_DSEND_EVT is NULL");
		return NCSCC_RC_FAILURE;
	}

	/* retrieve MQA CB */
	mqa_cb = (MQA_CB *)m_MQSV_MQA_RETRIEVE_MQA_CB;

	if (!mqa_cb) {
		TRACE_2("FAILURE: Control block retrieval failed");
		mds_free_direct_buff((MDS_DIRECT_BUFF)i_evt);
		return NCSCC_RC_FAILURE;
	}

	/* Before entering any mds send function, the API locks the control
	 * block. unlock the control block before send and lock it after we
	 * receive the reply */
	if (m_NCS_UNLOCK(&mqa_cb->cb_lock, NCS_LOCK_WRITE) !=
	    NCSCC_RC_SUCCESS) {
		TRACE_4("FAILURE: Lock failed for control block write");
		m_MQSV_MQA_GIVEUP_MQA_CB;
		mds_free_direct_buff((MDS_DIRECT_BUFF)i_evt);
		return NCSCC_RC_FAILURE;
	}

	memset(&mds_info, 0, sizeof(NCSMDS_INFO));
	mds_info.i_mds_hdl = mqa_mds_hdl;
	mds_info.i_svc_id = NCSMDS_SVC_ID_MQA;
	mds_info.i_op = MDS_DIRECT_SEND;

	/* fill the send structure */
	mds_info.info.svc_direct_send.i_direct_buff = (NCSCONTEXT)i_evt;
	mds_info.info.svc_direct_send.i_direct_buff_len = length;

	mds_info.info.svc_direct_send.i_priority = priority;
	mds_info.info.svc_direct_send.i_to_svc = to_svc;
	mds_info.info.svc_direct_send.i_msg_fmt_ver = i_evt->msg_fmt_version;
	mds_info.info.svc_direct_send.i_sendtype = MDS_SENDTYPE_SND;

	/* fill the sendinfo  strcuture */
	mds_info.info.svc_direct_send.info.snd.i_to_dest = *destination;

	/* send the message */
	rc = ncsmds_api(&mds_info);
	if (rc != NCSCC_RC_SUCCESS)
		TRACE_2("FAILURE: Message Send through MDS Failure");

	m_NCS_LOCK(&mqa_cb->cb_lock, NCS_LOCK_WRITE);
	m_MQSV_MQA_GIVEUP_MQA_CB;
	TRACE_LEAVE();
	return rc;
}

/****************************************************************************
  Name          : mqa_mds_msg_async_reply_direct

  Description   : This routine sends the MQA message to MQND.

  Arguments     :
		  uint32_t mqa_mds_hdl Handle of MQA
		  MDS_DEST  *destination - destintion to send to
		  MQSV_EVT   *i_evt - MQSV_EVT pointer
		 MDS_SYNC_SND_CTXT *context - context of MDS

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t mqa_mds_msg_async_reply_direct(uint32_t mqa_mds_hdl,
					MDS_DEST *destination,
					MQSV_DSEND_EVT *i_evt,
					MDS_SYNC_SND_CTXT *context,
					uint32_t length)
{
	NCSMDS_INFO mds_info;
	uint32_t rc;
	MQA_CB *mqa_cb;
	TRACE_ENTER();

	if (!i_evt) {
		TRACE_2("FAILURE: MQSV_DSEND_EVT is NULL");
		return NCSCC_RC_FAILURE;
	}

	/* retrieve MQA CB */
	mqa_cb = (MQA_CB *)m_MQSV_MQA_RETRIEVE_MQA_CB;

	if (!mqa_cb) {
		TRACE_2("FAILURE: Control block retrieval failed");
		mds_free_direct_buff((MDS_DIRECT_BUFF)i_evt);
		return NCSCC_RC_FAILURE;
	}

	/* Before entering any mds send function, the API locks the control
	 * block. unlock the control block before send and lock it after we
	 * receive the reply */
	if (m_NCS_UNLOCK(&mqa_cb->cb_lock, NCS_LOCK_WRITE) !=
	    NCSCC_RC_SUCCESS) {
		TRACE_4("FAILURE: Lock failed for control block write");
		m_MQSV_MQA_GIVEUP_MQA_CB;
		mds_free_direct_buff((MDS_DIRECT_BUFF)i_evt);
		return NCSCC_RC_FAILURE;
	}

	memset(&mds_info, 0, sizeof(NCSMDS_INFO));
	mds_info.i_mds_hdl = mqa_mds_hdl;
	mds_info.i_svc_id = NCSMDS_SVC_ID_MQA;
	mds_info.i_op = MDS_DIRECT_SEND;

	/* fill the send structure */
	mds_info.info.svc_direct_send.i_direct_buff = (NCSCONTEXT)i_evt;
	mds_info.info.svc_direct_send.i_direct_buff_len = length;

	mds_info.info.svc_direct_send.i_priority = MDS_SEND_PRIORITY_MEDIUM;
	mds_info.info.svc_direct_send.i_to_svc = NCSMDS_SVC_ID_MQA;
	mds_info.info.svc_direct_send.i_msg_fmt_ver = i_evt->msg_fmt_version;
	mds_info.info.svc_direct_send.i_sendtype = MDS_SENDTYPE_RSP;

	/* fill the sendinfo  strcuture */
	mds_info.info.svc_direct_send.info.rsp.i_msg_ctxt = *context;
	mds_info.info.svc_direct_send.info.rsp.i_sender_dest = *destination;

	/* send the message */
	rc = ncsmds_api(&mds_info);

	if (rc != NCSCC_RC_SUCCESS)
		TRACE_2("FAILURE: Message Send through MDS Failure");

	m_NCS_LOCK(&mqa_cb->cb_lock, NCS_LOCK_WRITE);
	m_MQSV_MQA_GIVEUP_MQA_CB;
	TRACE_LEAVE();
	return rc;
}
