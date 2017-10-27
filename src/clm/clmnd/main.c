/*      -*- OpenSAF  -*-
 *
 * (C) Copyright 2010,2015 The OpenSAF Foundation
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
 * Author(s):  Emerson Network Power
 *             Ericsson AB
 *
 */
#include <stdio.h>
#include <errno.h>
#include <inttypes.h>
#include <saClm.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "base/logtrace.h"
#include "base/ncs_hdl_pub.h"
#include "base/ncs_main_papi.h"
#include "base/ncs_mda_pvt.h"
#include "base/ncs_util.h"
#include "base/ncsencdec_pub.h"
#include "base/ncsgl_defs.h"
#include "base/osaf_poll.h"
#include "base/osaf_time.h"
#include "base/osaf_extended_name.h"
#include "clm/clmnd/cb.h"
#include "clm/clmnd/clmna.h"
#include "clm/clmnd/election_starter_wrapper.h"
#include "clm/clmnd/evt.h"
#include "clm/common/clmsv_enc_dec.h"
#include "clm/common/clmsv_msg.h"
#include "mds/mds_papi.h"
#include "osaf/configmake.h"

enum { FD_TERM = 0, FD_AMF, FD_MBX, NUM_FD };

static struct pollfd fds[NUM_FD];
static nfds_t nfds = NUM_FD;
static NCS_SEL_OBJ usr1_sel_obj;

/*TBD : Add nodeaddress as well for future use*/

#define CLMNA_MDS_SUB_PART_VERSION 1
#define CLMNA_SCALE_OUT_RETRY_TIME 100
#define CLMNA_JOIN_RETRY_TIME 3000
#define CLMNA_SVC_PVT_SUBPART_VERSION 1
#define CLMNA_WRT_CLMS_SUBPART_VER_AT_MIN_MSG_FMT 1
#define CLMNA_WRT_CLMS_SUBPART_VER_AT_MAX_MSG_FMT 1
#define CLMNA_WRT_CLMS_SUBPART_VER_RANGE                                       \
	(CLMNA_WRT_CLMS_SUBPART_VER_AT_MAX_MSG_FMT -                           \
	 CLMNA_WRT_CLMS_SUBPART_VER_AT_MIN_MSG_FMT + 1)

static CLMNA_CB _clmna_cb;
CLMNA_CB *clmna_cb = &_clmna_cb;

/*msg format version for CLMNA subpart version 1 */
static MDS_CLIENT_MSG_FORMAT_VER
    CLMNA_WRT_CLMS_MSG_FMT_ARRAY[CLMNA_WRT_CLMS_SUBPART_VER_RANGE] = {1};

static uint32_t clmna_mds_enc(struct ncsmds_callback_info *info);
static uint32_t clmna_mds_callback(struct ncsmds_callback_info *info);
static void clmna_process_dummyup_msg(void);
static void clmna_handle_join_response(SaAisErrorT error,
				       const SaNameT *node_name);
static void start_scale_out_retry_tmr(int64_t timeout);
static void stop_scale_out_retry_tmr(void);
static uint32_t clmna_mds_msg_send(CLMSV_MSG *i_msg);
static void clmna_execute_action(const char *action);

static uint32_t clmna_mds_cpy(struct ncsmds_callback_info *info)
{
	return NCSCC_RC_SUCCESS;
}

static uint32_t clmna_mds_dec(struct ncsmds_callback_info *info)
{
	uint8_t *p8;
	CLMSV_MSG *msg;
	NCS_UBAID *uba = info->info.dec.io_uba;
	uint8_t local_data[20];
	uint32_t total_bytes = 0;
	TRACE_ENTER();

	if (0 ==
	    m_NCS_MSG_FORMAT_IS_VALID(info->info.dec.i_msg_fmt_ver,
				      CLMNA_WRT_CLMS_SUBPART_VER_AT_MIN_MSG_FMT,
				      CLMNA_WRT_CLMS_SUBPART_VER_AT_MAX_MSG_FMT,
				      CLMNA_WRT_CLMS_MSG_FMT_ARRAY)) {
		TRACE("Invalid message format!!!\n");
		TRACE_LEAVE();
		return NCSCC_RC_FAILURE;
	}

	/** Allocate a new msg in both sync/async cases
	 **/
	if (NULL == (msg = calloc(1, sizeof(CLMSV_MSG)))) {
		TRACE("calloc failed\n");
		return NCSCC_RC_FAILURE;
	}

	info->info.dec.o_msg = (uint8_t *)msg;

	p8 = ncs_dec_flatten_space(uba, local_data, 4);
	msg->evt_type = ncs_decode_32bit(&p8);
	ncs_dec_skip_space(uba, 4);
	total_bytes += 4;

	switch (msg->evt_type) {
	case CLMSV_CLMS_TO_CLMNA_REBOOT_MSG: {
		p8 = ncs_dec_flatten_space(uba, local_data, 4);
		msg->info.reboot_info.node_id = ncs_decode_32bit(&p8);
		ncs_dec_skip_space(uba, 4);
		total_bytes += 4;
		// Reboot will be performed by CLMS for this node.
		if (clmna_cb->node_info.node_id !=
		    msg->info.reboot_info.node_id) {
			osaf_safe_reboot();
		}
		break;
	}
	case CLMSV_CLMS_TO_CLMNA_NODE_REBOOT_MSG: {
		p8 = ncs_dec_flatten_space(uba, local_data, 4);
		msg->info.reboot_info.node_id = ncs_decode_32bit(&p8);
		ncs_dec_skip_space(uba, 4);
		total_bytes += 4;
		// Reboot will be performed by CLMS for this node.
		if (clmna_cb->node_info.node_id ==
		    msg->info.reboot_info.node_id) {
			LOG_IN("Node will be rebooted");
			osaf_safe_reboot();
		} else {
			LOG_IN("Node %u is going to be rebooted",
					msg->info.reboot_info.node_id);
		}
		break;
	}
	case CLMSV_CLMS_TO_CLMA_API_RESP_MSG: {
		p8 = ncs_dec_flatten_space(uba, local_data, 8);
		msg->info.api_resp_info.type = ncs_decode_32bit(&p8);
		msg->info.api_resp_info.rc = ncs_decode_32bit(&p8);
		ncs_dec_skip_space(uba, 8);
		total_bytes += 8;
		TRACE_2("CLMSV_CLMA_API_RESP_MSG for Node_Up rc = %d",
			(int)msg->info.api_resp_info.rc);

		switch (msg->info.api_resp_info.type) {
		case CLMSV_CLUSTER_JOIN_RESP:
			total_bytes += clmsv_decodeSaNameT(
			    uba, &(msg->info.api_resp_info.param.node_name));
			break;
		default:
			TRACE_2("Unknown API RSP type %d",
				msg->info.api_resp_info.type);
			free(msg);
			return NCSCC_RC_FAILURE;
		}
	} break;
	case CLMSV_CLMS_TO_CLMNA_ACTION_MSG: {
		const char *action;

		p8 = ncs_dec_flatten_space(uba, local_data, 4);
		msg->info.action_info.node_id = ncs_decode_32bit(&p8);
		ncs_dec_skip_space(uba, 4);
		total_bytes += 4;

		if (msg->info.action_info.node_id == 0
				|| clmna_cb->node_info.node_id ==
					msg->info.reboot_info.node_id) {
			total_bytes += clmsv_decodeSaNameT(
					uba, &(msg->info.action_info.action));

			action = osaf_extended_name_borrow(
					&(msg->info.action_info.action));
			TRACE_2("Executing action: %s", action);
			clmna_execute_action(action);
		}
	}
	break;
	default:
		TRACE("Unknown MSG type %d", msg->evt_type);
		free(msg);
		return NCSCC_RC_FAILURE;
	}

	TRACE_LEAVE();
	return NCSCC_RC_SUCCESS;
}

static uint32_t clmna_mds_dec_flat(struct ncsmds_callback_info *info)
{
	uint32_t rc = NCSCC_RC_SUCCESS;
	/* Retrieve info from the dec_flat */
	MDS_CALLBACK_DEC_INFO dec = info->info.dec_flat;
	/* Modify the MDS_INFO to populate dec */
	info->info.dec = dec;
	/* Invoke the regular mds_dec routine */
	rc = clmna_mds_dec(info);
	if (rc != NCSCC_RC_SUCCESS) {
		TRACE("mds_dec FAILED ");
	}
	return rc;
}

static uint32_t clmna_mds_rcv(struct ncsmds_callback_info *mds_cb_info)
{
	CLMSV_MSG *msg = (CLMSV_MSG *)mds_cb_info->info.receive.i_msg;
	CLMNA_EVT *evt = calloc(1, sizeof(CLMNA_EVT));
	evt->type = CLMNA_EVT_JOIN_RESPONSE;
	evt->join_response.rc = msg->info.api_resp_info.rc;
	evt->join_response.node_name = msg->info.api_resp_info.param.node_name;
	free(msg);
	if (m_NCS_IPC_SEND(&clmna_cb->mbx, evt, NCS_IPC_PRIORITY_VERY_HIGH) !=
	    NCSCC_RC_SUCCESS) {
		LOG_ER("IPC send to mailbox failed: %s", __FUNCTION__);
	}
	return NCSCC_RC_SUCCESS;
}

static void clmna_handle_mds_change_evt(bool caused_by_timer_expiry,
					NCSMDS_CHG change, NODE_ID node_id,
					MDS_SVC_ID svc_id)
{
	if ((change == NCSMDS_NEW_ACTIVE || change == NCSMDS_UP) &&
	    svc_id == NCSMDS_SVC_ID_CLMS && clmna_cb->server_synced == false) {
		clmna_process_dummyup_msg();
	}

	if (caused_by_timer_expiry == false &&
	    (change == NCSMDS_UP || change == NCSMDS_DOWN) &&
	    (svc_id == NCSMDS_SVC_ID_CLMNA || svc_id == NCSMDS_SVC_ID_RDE)) {
		if (change == NCSMDS_UP) {
			ElectionStarterUpEvent(
			    clmna_cb->election_starter, node_id,
			    svc_id == NCSMDS_SVC_ID_CLMNA
				? ElectionStarterServiceNode
				: ElectionStarterServiceController);
		} else {
			ElectionStarterDownEvent(
			    clmna_cb->election_starter, node_id,
			    svc_id == NCSMDS_SVC_ID_CLMNA
				? ElectionStarterServiceNode
				: ElectionStarterServiceController);
		}
	}
}

static void clmna_handle_join_response(SaAisErrorT error,
				       const SaNameT *node_name)
{
	if (error == SA_AIS_ERR_NOT_EXIST) {
		LOG_ER("%s is not a configured node", node_name->value);
	} else if (error == SA_AIS_ERR_EXIST) {
		LOG_ER(
		    "%s is already up. Specify a unique name in" PKGSYSCONFDIR
		    "/node_name",
		    node_name->value);
	} else if (error == SA_AIS_ERR_TRY_AGAIN) {
		if (clmna_cb->try_again_received) {
			LOG_IN("Re-trying to scale out %s", node_name->value);
		} else {
			// Avoid spamming the log with more than one message at
			// NOTICE priority.
			clmna_cb->try_again_received = true;
			LOG_NO("%s has been queued for scale-out",
			       node_name->value);
		}
		stop_scale_out_retry_tmr();
		start_scale_out_retry_tmr(CLMNA_SCALE_OUT_RETRY_TIME);
	} else if (error == SA_AIS_OK) {
		stop_scale_out_retry_tmr();
		if (clmna_cb->server_synced == false) {
			NODE_INFO self_node = clmna_cb->node_info;
			clmna_cb->server_synced = true;
			LOG_NO("%s Joined cluster, nodeid=%x", node_name->value,
			       self_node.node_id);
			if (clmna_cb->nid_started &&
			    nid_notify("CLMNA", NCSCC_RC_SUCCESS, NULL) !=
				NCSCC_RC_SUCCESS) {
				LOG_ER("nid notify failed");
			}
		}
	} else {
		LOG_ER("Received unexpected join response error code %d",
		       (int)error);
	}
}

static uint32_t clmna_mds_svc_evt(struct ncsmds_callback_info *mds_cb_info)
{
	TRACE_ENTER2("%d", mds_cb_info->info.svc_evt.i_change);
	CLMNA_EVT *evt;

	switch (mds_cb_info->info.svc_evt.i_change) {
	case NCSMDS_NEW_ACTIVE:
	case NCSMDS_UP:
		switch (mds_cb_info->info.svc_evt.i_svc_id) {
		case NCSMDS_SVC_ID_CLMS:
			clmna_cb->clms_mds_dest =
			    mds_cb_info->info.svc_evt.i_dest;
			TRACE("subpart version: %u",
			      mds_cb_info->info.svc_evt.i_rem_svc_pvt_ver);
			TRACE("svc_id %d", mds_cb_info->info.svc_evt.i_svc_id);
			break;
		default:
			break;
		}
		break;
	case NCSMDS_DOWN:
		switch (mds_cb_info->info.svc_evt.i_svc_id) {
		case NCSMDS_SVC_ID_CLMS:
			// if CLMS dies, then we have to send nodeup again
			clmna_cb->server_synced = false;
			clmna_cb->clms_mds_dest = 0;
			clmna_cb->try_again_received = false;
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}

	evt = calloc(1, sizeof(CLMNA_EVT));
	evt->type = CLMNA_EVT_CHANGE_MSG;
	evt->change.caused_by_timer_expiry = false;
	evt->change.change = mds_cb_info->info.svc_evt.i_change;
	evt->change.node_id = mds_cb_info->info.svc_evt.i_node_id;
	evt->change.svc_id = mds_cb_info->info.svc_evt.i_svc_id;
	if (m_NCS_IPC_SEND(&clmna_cb->mbx, evt, NCS_IPC_PRIORITY_VERY_HIGH) !=
	    NCSCC_RC_SUCCESS) {
		LOG_ER("IPC send to mailbox failed: %s", __FUNCTION__);
	}

	TRACE_LEAVE();
	return NCSCC_RC_SUCCESS;
}

static uint32_t clmna_mds_enc_flat(struct ncsmds_callback_info *info)
{
	/* Modify the MDS_INFO to populate enc */
	info->info.enc = info->info.enc_flat;
	/* Invoke the regular mds_enc routine  */
	return clmna_mds_enc(info);
}

static uint32_t clmna_mds_enc(struct ncsmds_callback_info *info)
{
	CLMSV_MSG *msg;
	NCS_UBAID *uba;
	uint8_t *p8;
	uint32_t total_bytes = 0;

	TRACE_ENTER();

	MDS_CLIENT_MSG_FORMAT_VER msg_fmt_version;

	msg_fmt_version =
	    m_NCS_ENC_MSG_FMT_GET(info->info.enc.i_rem_svc_pvt_ver,
				  CLMNA_WRT_CLMS_SUBPART_VER_AT_MIN_MSG_FMT,
				  CLMNA_WRT_CLMS_SUBPART_VER_AT_MAX_MSG_FMT,
				  CLMNA_WRT_CLMS_MSG_FMT_ARRAY);
	if (0 == msg_fmt_version) {
		LOG_ER("Wrong msg_fmt_version!!");
		TRACE_LEAVE();
		return NCSCC_RC_FAILURE;
	}

	info->info.enc.o_msg_fmt_ver = msg_fmt_version;

	msg = (CLMSV_MSG *)info->info.enc.i_msg;
	uba = info->info.enc.io_uba;

	TRACE_2("msgtype: %d", msg->evt_type);

	if (uba == NULL) {
		LOG_ER("uba == NULL");
		return NCSCC_RC_FAILURE;
	}

	/** encode the type of message **/
	p8 = ncs_enc_reserve_space(uba, 4);
	if (!p8) {
		LOG_ER("NULL pointer");
		return NCSCC_RC_FAILURE;
	}
	ncs_encode_32bit(&p8, msg->evt_type);
	ncs_enc_claim_space(uba, 4);
	total_bytes += 4;

	if (CLMSV_CLMA_TO_CLMS_API_MSG == msg->evt_type) {

		/* encode the nodeupinfo */
		p8 = ncs_enc_reserve_space(uba, 4);
		if (!p8) {
			LOG_ER("NULL pointer");
			TRACE_LEAVE();
			return NCSCC_RC_FAILURE;
		}
		ncs_encode_32bit(&p8, msg->info.api_info.type);
		ncs_enc_claim_space(uba, 4);
		total_bytes += 4;

		TRACE_2("api_info.type: %d", msg->info.api_info.type);

		if (msg->info.api_info.type == CLMSV_CLUSTER_JOIN_REQ) {
			p8 = ncs_enc_reserve_space(uba, 4);
			if (!p8) {
				LOG_ER("NULL pointer");
				TRACE_LEAVE();
				return NCSCC_RC_FAILURE;
			}

			ncs_encode_32bit(
			    &p8, msg->info.api_info.param.nodeup_info.node_id);
			ncs_enc_claim_space(uba, 4);
			total_bytes += 4;
			total_bytes += clmsv_encodeSaNameT(
			    uba,
			    &(msg->info.api_info.param.nodeup_info.node_name));

			total_bytes += clmsv_encodeSaNameT(
			    uba,
			    &(msg->info.api_info.param.nodeup_info.user_data));

			p8 = ncs_enc_reserve_space(uba, 1);
			ncs_encode_8bit(
			    &p8,
			    msg->info.api_info.param.nodeup_info.ifs);
			ncs_enc_claim_space(uba, 1);
			total_bytes += 1;
			TRACE("Encoded CLM_IFS: [%c] ",
			      msg->info.api_info.param.nodeup_info.ifs);

			p8 = ncs_enc_reserve_space(uba, 8);
			ncs_encode_64bit(
			    &p8,
			    msg->info.api_info.param.nodeup_info.boot_time);
			ncs_enc_claim_space(uba, 8);
			total_bytes += 8;
			TRACE("Encoded boot time: %" PRIu64,
			      (uint64_t)msg->info.api_info.param.nodeup_info
				  .boot_time);
			uint16_t no_of_addresses =
			    msg->info.api_info.param.nodeup_info
				.no_of_addresses;
			p8 = ncs_enc_reserve_space(uba, 2);
			ncs_encode_16bit(&p8, no_of_addresses);
			ncs_enc_claim_space(uba, 2);
			total_bytes += 2;
			if (no_of_addresses != 0) {
				osafassert(no_of_addresses == 1);
				total_bytes += clmsv_encodeNodeAddressT(
				    uba, &(msg->info.api_info.param.nodeup_info
					       .address));
			}
		}
	}

	TRACE_LEAVE();
	return NCSCC_RC_SUCCESS;
}

static uint32_t clmna_mds_callback(struct ncsmds_callback_info *info)
{
	uint32_t rc;

	static NCSMDS_CALLBACK_API cb_set[MDS_CALLBACK_SVC_MAX] = {
	    clmna_mds_cpy,      /* MDS_CALLBACK_COPY      0 */
	    clmna_mds_enc,      /* MDS_CALLBACK_ENC       1 */
	    clmna_mds_dec,      /* MDS_CALLBACK_DEC       2 */
	    clmna_mds_enc_flat, /* MDS_CALLBACK_ENC_FLAT  3 */
	    clmna_mds_dec_flat, /* MDS_CALLBACK_DEC_FLAT  4 */
	    clmna_mds_rcv,      /* MDS_CALLBACK_RECEIVE   5 */
	    clmna_mds_svc_evt};

	if (info->i_op <= MDS_CALLBACK_SVC_EVENT) {
		rc = (*cb_set[info->i_op])(info);
		if (rc != NCSCC_RC_SUCCESS)
			LOG_ER("MDS_CALLBACK_SVC_EVENT not in range");

		TRACE_LEAVE();
	}

	return NCSCC_RC_SUCCESS;
}

static uint32_t clmna_mds_init(void)
{
	NCSADA_INFO ada_info;
	NCSMDS_INFO mds_info;
	uint32_t rc = NCSCC_RC_SUCCESS;
	MDS_SVC_ID svc[] = {NCSMDS_SVC_ID_CLMS, NCSMDS_SVC_ID_CLMNA,
			    NCSMDS_SVC_ID_RDE};

	TRACE_ENTER();

	/** Create the ADEST for CLMNA and get the pwe hdl**/
	memset(&ada_info, '\0', sizeof(ada_info));
	ada_info.req = NCSADA_GET_HDLS;

	if (NCSCC_RC_SUCCESS != (rc = ncsada_api(&ada_info))) {
		LOG_ER("NCSADA_GET_HDLS failed, rc = %d", rc);
		TRACE_LEAVE();
		return NCSCC_RC_FAILURE;
	}

	/** Store the info obtained from MDS ADEST creation  **/
	clmna_cb->mds_hdl = ada_info.info.adest_get_hdls.o_mds_pwe1_hdl;

	/** Now install into mds **/
	memset(&mds_info, '\0', sizeof(NCSMDS_INFO));
	mds_info.i_mds_hdl = clmna_cb->mds_hdl;
	mds_info.i_svc_id = NCSMDS_SVC_ID_CLMNA;
	mds_info.i_op = MDS_INSTALL;

	mds_info.info.svc_install.i_yr_svc_hdl = 0;
	mds_info.info.svc_install.i_install_scope =
	    NCSMDS_SCOPE_NONE;					 /* PWE scope */
	mds_info.info.svc_install.i_svc_cb = clmna_mds_callback; /* callback */
	mds_info.info.svc_install.i_mds_q_ownership =
	    false; /* CLMNA doesn't own the mds queue */
	mds_info.info.svc_install.i_mds_svc_pvt_ver =
	    CLMNA_SVC_PVT_SUBPART_VERSION;

	if ((rc = ncsmds_api(&mds_info)) != NCSCC_RC_SUCCESS) {
		LOG_ER("mds api call failed");
		TRACE_LEAVE();
		return NCSCC_RC_FAILURE;
	}

	/* Now subscribe for events that will be generated by MDS */
	memset(&mds_info, '\0', sizeof(NCSMDS_INFO));

	mds_info.i_mds_hdl = clmna_cb->mds_hdl;
	mds_info.i_svc_id = NCSMDS_SVC_ID_CLMNA;
	mds_info.i_op = MDS_SUBSCRIBE;

	mds_info.info.svc_subscribe.i_scope = NCSMDS_SCOPE_NONE;
	mds_info.info.svc_subscribe.i_num_svcs = 3;
	mds_info.info.svc_subscribe.i_svc_ids = svc;

	rc = ncsmds_api(&mds_info);
	if (rc != NCSCC_RC_SUCCESS) {
		LOG_ER("mds api call failed");
		TRACE_LEAVE();
		return rc;
	}

	TRACE_LEAVE();
	return rc;
}

static void get_user_data(NODE_INFO *node)
{
	FILE *fp;

	fp = fopen(PKGSYSCONFDIR "/clm_user_data", "r");
	if (fp == NULL) {
		LOG_IN("Could not open file %s - %s", PKGSYSCONFDIR "/clm_user_data",
		       strerror(errno));
		return;
	}

	if (fgets((char*) node->user_data.value, sizeof(node->user_data.value),
		fp) != NULL) {
		node->user_data.length = strnlen((char *)node->user_data.value,
			sizeof(node->user_data.value));
	} else {
		LOG_WA("Could not read file %s", PKGSYSCONFDIR "/clm_user_data");
	}

	if (memchr(node->user_data.value, node->ifs, node->user_data.length) != NULL) {
		LOG_WA("CLM IFS [%c] was found in %s", node->ifs, node->user_data.value);
		memset(&node->user_data, 0, sizeof(SaNameT));
	}

	fclose(fp);

}

static int get_node_info(NODE_INFO *node)
{
	FILE *fp;
	const char *ifs;

	if ((ifs = getenv("CLMNA_IFS")) == NULL) {
		node->ifs = ',';  // default field separator
	} else {
		node->ifs = *ifs;
	}

	get_user_data(node);

	fp = fopen(PKGSYSCONFDIR "/node_name", "r");
	if (fp == NULL) {
		LOG_ER("Could not open file %s - %s", PKGSYSCONFDIR "node_name",
		       strerror(errno));
		return -1;
	}

	if (EOF == fscanf(fp, "%s", node->node_name.value)) {
		fclose(fp);
		LOG_ER("Could not get node name - %s", strerror(errno));
		return -1;
	}
	fclose(fp);
	node->node_name.length = strlen((char *)node->node_name.value);
	TRACE("node name: '%s'", node->node_name.value);

	if (memchr(node->node_name.value, node->ifs, node->node_name.length) != NULL) {
		LOG_ER("CLM IFS [%c] was found in %s", node->ifs, node->node_name.value);
		return -1;
	}

	fp = fopen(PKGLOCALSTATEDIR "/node_id", "r");
	if (fp == NULL) {
		LOG_ER("Could not open file %s - %s",
		       PKGLOCALSTATEDIR "node_id", strerror(errno));
		return -1;
	}

	if (EOF == fscanf(fp, "%x", &node->node_id)) {
		fclose(fp);
		LOG_ER("Could not get node id - %s", strerror(errno));
		return -1;
	}
	fclose(fp);
	TRACE("node id: 0x%" PRIx32, node->node_id);
	struct timespec boot_time;
	osaf_get_boot_time(&boot_time);
	// Round boot time to 10 microseconds, so that multiple readings are
	// likely to result in the same value.
	uint64_t boot_time_10us =
	    (osaf_timespec_to_nanos(&boot_time) + 5000) / 10000;
	node->boot_time = boot_time_10us * 10000;
	TRACE("Boot time: %" PRIu64, (uint64_t)node->boot_time);

	long family_val = 0;
	char *family = getenv("CLMNA_ADDR_FAMILY");
	char *value = getenv("CLMNA_ADDR_VALUE");
	if (family != NULL) {
		char *endptr = family;
		errno = 0;
		family_val = strtol(family, &endptr, 0);
		if (errno != 0 || *endptr != '\0' || *family == '\0')
			family = NULL;
	}
	if (family != NULL && value != NULL &&
		memchr(value, node->ifs, strlen(value)) == NULL) {
		size_t len = strlen(value);
		if (len > SA_CLM_MAX_ADDRESS_LENGTH)
			len = SA_CLM_MAX_ADDRESS_LENGTH;
		node->address.family = family_val;
		node->address.length = len;
		memcpy(node->address.value, value, len);
		node->no_of_addresses = 1;
	} else {
		node->no_of_addresses = 0;
		memset(&node->address, 0, sizeof(node->address));
	}
	return 0;
}

static uint32_t clmna_mds_msg_send(CLMSV_MSG *i_msg)
{
	NCSMDS_INFO mds_info;
	uint32_t rc = NCSCC_RC_SUCCESS;
	TRACE_ENTER();

	memset(&mds_info, '\0', sizeof(NCSMDS_INFO));
	mds_info.i_mds_hdl = clmna_cb->mds_hdl;
	mds_info.i_svc_id = NCSMDS_SVC_ID_CLMNA;
	mds_info.i_op = MDS_SEND;

	/* Fill the send structure */
	mds_info.info.svc_send.i_msg = (NCSCONTEXT)i_msg;
	mds_info.info.svc_send.i_to_svc = NCSMDS_SVC_ID_CLMS;
	mds_info.info.svc_send.i_sendtype = MDS_SENDTYPE_SND;
	mds_info.info.svc_send.i_priority = MDS_SEND_PRIORITY_HIGH;
	/* fill the sub send strcuture */
	mds_info.info.svc_send.info.snd.i_to_dest = clmna_cb->clms_mds_dest;

	/* send the message */
	if (NCSCC_RC_SUCCESS != (rc = ncsmds_api(&mds_info)))
		TRACE("clma_mds_msg_send FAILED");

	TRACE_LEAVE();
	return rc;
}

static void clmna_execute_action(const char *action) {
	char str[1024];
	int size;

	if (!action) {
		syslog(LOG_ERR, "Executing CLM action is NULL");
		return;
	}

	if (strchr(action, '/')) {
		syslog(LOG_ERR,
				"Executing CLM action '%s' contains not allowed sign '/'",
				action);
		return;
	}

	size = snprintf(str, sizeof(str), PKGCLMSCRIPTDIR "/osafclm_%s", action);
	if (size > sizeof(str)) {
		syslog(LOG_ERR,
				"Path for executing CLM script '%s' is longer than %ld",
				action, sizeof(str));
		return;
	}

	syslog(LOG_NOTICE, "Executing CLM script: %s", str);

	int rc = system(str);
	if (rc == -1) {
		syslog(LOG_CRIT, "Executing CLM script failed with reason: %s",
		       strerror(errno));
	} else {
		if (WIFEXITED(rc) && WEXITSTATUS(rc) == 0) {
			syslog(LOG_NOTICE, "CLM script: %s successfully executed",
			       str);
		} else {
			syslog(LOG_CRIT, "CLM script: %s failed, rc = %d", str,
			       rc);
		}
	}
}

static void scale_out_tmr_exp(void *arg)
{
	TRACE_ENTER();
	(void)arg;
	if (clmna_cb->is_scale_out_retry_tmr_running == true) {
		CLMNA_EVT *evt = calloc(1, sizeof(CLMNA_EVT));
		if (evt != NULL) {
			evt->type = CLMNA_EVT_CHANGE_MSG;
			evt->change.caused_by_timer_expiry = true;
			evt->change.change = NCSMDS_UP;
			evt->change.node_id = 0;
			evt->change.svc_id = NCSMDS_SVC_ID_CLMS;
			if (m_NCS_IPC_SEND(&clmna_cb->mbx, evt,
					   NCS_IPC_PRIORITY_VERY_HIGH) !=
			    NCSCC_RC_SUCCESS)
				LOG_ER("IPC send to mailbox failed: %s",
				       __FUNCTION__);
		} else {
			LOG_ER("Could not allocate IPC event: %s",
			       __FUNCTION__);
		}
	} else {
		LOG_ER("Unexpected scale out timer expiration: %s",
		       __FUNCTION__);
	}
	clmna_cb->is_scale_out_retry_tmr_running = false;
	TRACE_LEAVE();
}

static void start_scale_out_retry_tmr(int64_t timeout)
{
	TRACE_ENTER();
	if (clmna_cb->scale_out_retry_tmr == NULL) {
		m_NCS_TMR_CREATE(clmna_cb->scale_out_retry_tmr,
				 CLMNA_SCALE_OUT_RETRY_TIME, scale_out_tmr_exp,
				 NULL);
	}

	if (clmna_cb->scale_out_retry_tmr != NULL &&
	    clmna_cb->is_scale_out_retry_tmr_running == false) {
		m_NCS_TMR_START(clmna_cb->scale_out_retry_tmr, timeout,
				scale_out_tmr_exp, NULL);
		if (clmna_cb->scale_out_retry_tmr != NULL) {
			clmna_cb->is_scale_out_retry_tmr_running = true;
		}
	}
	TRACE_LEAVE();
}

static void stop_scale_out_retry_tmr(void)
{
	TRACE_ENTER();
	if (clmna_cb->scale_out_retry_tmr != NULL &&
	    clmna_cb->is_scale_out_retry_tmr_running == true) {
		m_NCS_TMR_STOP(clmna_cb->scale_out_retry_tmr);
		clmna_cb->is_scale_out_retry_tmr_running = false;
	}
	TRACE_LEAVE();
}

static void clmna_process_dummyup_msg(void)
{
	if (clmna_cb->clms_mds_dest != 0) {
		CLMSV_MSG msg;
		NODE_INFO self_node = clmna_cb->node_info;
		msg.evt_type = CLMSV_CLMA_TO_CLMS_API_MSG;
		msg.info.api_info.type = CLMSV_CLUSTER_JOIN_REQ;
		msg.info.api_info.param.nodeup_info.node_id = self_node.node_id;
		msg.info.api_info.param.nodeup_info.node_name =
		    self_node.node_name;
		msg.info.api_info.param.nodeup_info.boot_time =
		    self_node.boot_time;
		msg.info.api_info.param.nodeup_info.no_of_addresses =
		    self_node.no_of_addresses;
		msg.info.api_info.param.nodeup_info.address = self_node.address;
		msg.info.api_info.param.nodeup_info.user_data = self_node.user_data;
		msg.info.api_info.param.nodeup_info.ifs = self_node.ifs;

		stop_scale_out_retry_tmr();
		start_scale_out_retry_tmr(CLMNA_JOIN_RETRY_TIME);
		uint32_t rc = clmna_mds_msg_send(&msg);
		switch (rc) {
		case NCSCC_RC_SUCCESS:
			break;
		case NCSCC_RC_REQ_TIMOUT:
			LOG_ER(
			    "Send timed out. Check status(network, process) of ACTIVE controller");
			break;
		default:
			LOG_ER("Send failed: %" PRIu32
			       " Check network connectivity",
			       rc);
			break;
		}
	}
}

void clmna_process_mbx(SYSF_MBX *mbx)
{
	CLMNA_EVT *msg;
	TRACE_ENTER();

	msg = (CLMNA_EVT *)ncs_ipc_non_blk_recv(mbx);
	if (msg == NULL) {
		TRACE_LEAVE2("No mailbox message although fd is set!");
		goto done;
	}
	switch (msg->type) {
	case CLMNA_EVT_CHANGE_MSG:
		clmna_handle_mds_change_evt(
		    msg->change.caused_by_timer_expiry, msg->change.change,
		    msg->change.node_id, msg->change.svc_id);
		break;
	case CLMNA_EVT_JOIN_RESPONSE:
		clmna_handle_join_response(msg->join_response.rc,
					   &msg->join_response.node_name);
		break;
	default:
		TRACE("Invalid message type");
		break;
	}
done:
	if (msg)
		free(msg);
	TRACE_LEAVE();
}

/**
 * USR1 signal is used when AMF wants to instantiate us as a
 * component. Wakes up the main thread to register with
 * AMF.
 *
 * @param i_sig_num
 */
static void sigusr1_handler(int sig)
{
	(void)sig;
	signal(SIGUSR1, SIG_IGN);
	ncs_sel_obj_ind(&usr1_sel_obj);
}

int main(int argc, char *argv[])
{
	uint32_t rc = NCSCC_RC_SUCCESS;
	int ret;
	SaAisErrorT error;
	int term_fd;

	daemonize(argc, argv);

	/* Initialize some basic stuff */
	clmna_cb->amf_hdl = 0;
	clmna_cb->server_synced = false;
	clmna_cb->scale_out_retry_tmr = NULL;
	clmna_cb->is_scale_out_retry_tmr_running = false;
	clmna_cb->try_again_received = false;

	/* Determine how this process was started, by NID or AMF */
	if (getenv("SA_AMF_COMPONENT_NAME") == NULL)
		clmna_cb->nid_started = true;

	if (get_node_info(&clmna_cb->node_info) != 0) {
		rc = NCSCC_RC_FAILURE;
		goto done;
	}

	if ((rc = ncs_agents_startup()) != NCSCC_RC_SUCCESS) {
		LOG_ER("ncs_agents_startup FAILED");
		goto done;
	}

	/* Create the mailbox used for communication with CLMS */
	if ((rc = m_NCS_IPC_CREATE(&clmna_cb->mbx)) != NCSCC_RC_SUCCESS) {
		LOG_ER("m_NCS_IPC_CREATE FAILED %d", rc);
		goto done;
	}

	/* Attach mailbox to this thread */
	if ((rc = m_NCS_IPC_ATTACH(&clmna_cb->mbx) != NCSCC_RC_SUCCESS)) {
		LOG_ER("m_NCS_IPC_ATTACH FAILED %d", rc);
		goto done;
	}

	if (clmna_mds_init() != NCSCC_RC_SUCCESS)
		goto done;

	/* Create a selection object */
	if (clmna_cb->nid_started &&
	    (rc = ncs_sel_obj_create(&usr1_sel_obj)) != NCSCC_RC_SUCCESS) {
		LOG_ER("ncs_sel_obj_create failed");
		goto done;
	}

	/*
	 ** Initialize a signal handler that will use the selection object.
	 ** The signal is sent from our script when AMF does instantiate.
	 */
	if (clmna_cb->nid_started &&
	    signal(SIGUSR1, sigusr1_handler) == SIG_ERR) {
		LOG_ER("signal USR1 failed: %s", strerror(errno));
		goto done;
	}

	clmna_cb->mbx_fd = ncs_ipc_get_sel_obj(&clmna_cb->mbx);
	daemon_sigterm_install(&term_fd);

	/* If AMF started register immediately */
	if (!clmna_cb->nid_started &&
	    (rc = clmna_amf_init(clmna_cb)) != NCSCC_RC_SUCCESS) {
		goto done;
	}

	clmna_cb->election_starter = ElectionStarterConstructor(
	    clmna_cb->nid_started, clmna_cb->node_info.node_id);

	if (clmna_cb->nid_started &&
	    nid_notify("CLMNA", rc, NULL) != NCSCC_RC_SUCCESS) {
		LOG_ER("nid notify failed");
	}

	fds[FD_TERM].fd = term_fd;
	fds[FD_TERM].events = POLLIN;
	fds[FD_AMF].fd = clmna_cb->nid_started ? usr1_sel_obj.rmv_obj
					       : clmna_cb->amf_sel_obj;
	fds[FD_AMF].events = POLLIN;
	fds[FD_MBX].fd = clmna_cb->mbx_fd.rmv_obj;
	fds[FD_MBX].events = POLLIN;

	while (1) {
		struct timespec timeout =
		    ElectionStarterPoll(clmna_cb->election_starter);
		ret = osaf_ppoll(fds, nfds, &timeout, NULL);

		if (ret == 0) {
			continue;
		}

		if (fds[FD_TERM].revents & POLLIN) {
			daemon_exit();
		}

		if (fds[FD_AMF].revents & POLLIN) {
			if (clmna_cb->amf_hdl != 0) {
				if ((error = saAmfDispatch(clmna_cb->amf_hdl,
							   SA_DISPATCH_ALL)) !=
				    SA_AIS_OK) {
					LOG_ER("saAmfDispatch failed: %u",
					       error);
					break;
				}
			} else {
				TRACE("SIGUSR1 event rec");
				ncs_sel_obj_rmv_ind(&usr1_sel_obj, true, true);
				ncs_sel_obj_destroy(&usr1_sel_obj);

				if ((error = clmna_amf_init(clmna_cb)) !=
				    NCSCC_RC_SUCCESS) {
					LOG_ER("AMF Initialization failed. %u",
					       error);
					break;
				}

				TRACE("AMF Initialization success");
				fds[FD_AMF].fd = clmna_cb->amf_sel_obj;
			}
		}

		if (fds[FD_MBX].revents & POLLIN) {
			clmna_process_mbx(&clmna_cb->mbx);
		}

	} /* End while (1) loop */

done:
	return 0;
}
