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
 * Author(s): Ericsson AB
 *
 */

/*****************************************************************************
  FILE NAME: immcn_evt.c

  DESCRIPTION: IMMND Event handling routines

  FUNCTIONS INCLUDED in this module:
  immnd_process_evt .........IMMND Event processing routine.
******************************************************************************/

#include <sys/types.h>
#include <pwd.h>
#include "base/osaf_secutil.h"
#include "immnd.h"
#include "imm/common/immsv_api.h"
#include "base/ncssysf_mem.h"
#include "mds/mds_papi.h"
#include "base/osaf_extended_name.h"

/* Adjust to 90% of MDS_DIRECT_BUF_MAXSIZE  */
#define IMMND_SEARCH_BUNDLE_SIZE ((MDS_DIRECT_BUF_MAXSIZE / 100) * 90)
#define IMMND_MAX_SEARCH_RESULT (IMMND_SEARCH_BUNDLE_SIZE / 300)

static SaAisErrorT immnd_fevs_local_checks(IMMND_CB *cb, IMMSV_FEVS *fevsReq,
					   const IMMSV_SEND_INFO *sinfo);
static uint32_t immnd_evt_proc_cb_dump(IMMND_CB *cb);
static uint32_t immnd_evt_proc_imm_init(IMMND_CB *cb, IMMND_EVT *evt,
					IMMSV_SEND_INFO *sinfo, bool isOm);
static uint32_t immnd_evt_proc_imm_finalize(IMMND_CB *cb, IMMND_EVT *evt,
					    IMMSV_SEND_INFO *sinfo, bool isOm);
static uint32_t immnd_evt_proc_imm_resurrect(IMMND_CB *cb, IMMND_EVT *evt,
					     IMMSV_SEND_INFO *sinfo, bool isOm);
static uint32_t immnd_evt_proc_imm_client_high(IMMND_CB *cb, IMMND_EVT *evt,
					       IMMSV_SEND_INFO *sinfo,
					       bool isOm);
static uint32_t immnd_evt_proc_recover_ccb_result(IMMND_CB *cb, IMMND_EVT *evt,
						  IMMSV_SEND_INFO *sinfo);
static uint32_t immnd_evt_proc_cl_imma_timeout(IMMND_CB *cb, IMMND_EVT *evt);

static uint32_t immnd_evt_proc_sync_finalize(IMMND_CB *cb, IMMND_EVT *evt,
					     IMMSV_SEND_INFO *sinfo);

static uint32_t immnd_evt_proc_admowner_init(IMMND_CB *cb, IMMND_EVT *evt,
					     IMMSV_SEND_INFO *sinfo);
static uint32_t immnd_evt_proc_impl_set(IMMND_CB *cb, IMMND_EVT *evt,
					IMMSV_SEND_INFO *sinfo);
static uint32_t immnd_evt_proc_ccb_init(IMMND_CB *cb, IMMND_EVT *evt,
					IMMSV_SEND_INFO *sinfo);

static uint32_t immnd_evt_proc_rt_update(IMMND_CB *cb, IMMND_EVT *evt,
					 IMMSV_SEND_INFO *sinfo);

static void immnd_evt_proc_discard_impl(IMMND_CB *cb, IMMND_EVT *evt,
					bool originatedAtThisNd,
					SaImmHandleT clnt_hdl,
					MDS_DEST reply_dest);

static void immnd_evt_proc_discard_node(IMMND_CB *cb, IMMND_EVT *evt,
					bool originatedAtThisNd,
					SaImmHandleT clnt_hdl,
					MDS_DEST reply_dest);

static void immnd_evt_proc_adminit_rsp(IMMND_CB *cb, IMMND_EVT *evt,
				       bool originatedAtThisNd,
				       SaImmHandleT clnt_hdl,
				       MDS_DEST reply_dest);

static void immnd_evt_proc_admo_finalize(IMMND_CB *cb, IMMND_EVT *evt,
					 bool originatedAtThisNd,
					 SaImmHandleT clnt_hdl,
					 MDS_DEST reply_dest);

// static void immnd_evt_proc_admo_hard_finalize(IMMND_CB *cb,
//					      IMMND_EVT *evt,
//					      bool originatedAtThisNd, SaImmHandleT
//clnt_hdl, MDS_DEST reply_dest);

static void immnd_evt_proc_admo_set(IMMND_CB *cb, IMMND_EVT *evt,
				    bool originatedAtThisNd,
				    SaImmHandleT clnt_hdl, MDS_DEST reply_dest);

static void immnd_evt_proc_admo_release(IMMND_CB *cb, IMMND_EVT *evt,
					bool originatedAtThisNd,
					SaImmHandleT clnt_hdl,
					MDS_DEST reply_dest);

static void immnd_evt_proc_admo_clear(IMMND_CB *cb, IMMND_EVT *evt,
				      bool originatedAtThisNd,
				      SaImmHandleT clnt_hdl,
				      MDS_DEST reply_dest);

static void immnd_evt_proc_finalize_sync(IMMND_CB *cb, IMMND_EVT *evt,
					 bool originatedAtThisNd,
					 SaImmHandleT clnt_hdl,
					 MDS_DEST reply_dest);

static void immnd_evt_proc_impl_set_rsp(IMMND_CB *cb, IMMND_EVT *evt,
					bool originatedAtThisNd,
					SaImmHandleT clnt_hdl,
					MDS_DEST reply_dest);

static void immnd_evt_proc_impl_clr(IMMND_CB *cb, IMMND_EVT *evt,
				    bool originatedAtThisNd,
				    SaImmHandleT clnt_hdl, MDS_DEST reply_dest);

static void immnd_evt_proc_cl_impl_set(IMMND_CB *cb, IMMND_EVT *evt,
				       bool originatedAtThisNd,
				       SaImmHandleT clnt_hdl,
				       MDS_DEST reply_dest);

static void immnd_evt_proc_cl_impl_rel(IMMND_CB *cb, IMMND_EVT *evt,
				       bool originatedAtThisNd,
				       SaImmHandleT clnt_hdl,
				       MDS_DEST reply_dest);

static void immnd_evt_proc_obj_impl_set(IMMND_CB *cb, IMMND_EVT *evt,
					bool originatedAtThisNd,
					SaImmHandleT clnt_hdl,
					MDS_DEST reply_dest);

static void immnd_evt_proc_obj_impl_rel(IMMND_CB *cb, IMMND_EVT *evt,
					bool originatedAtThisNd,
					SaImmHandleT clnt_hdl,
					MDS_DEST reply_dest);

static void immnd_evt_proc_ccbinit_rsp(IMMND_CB *cb, IMMND_EVT *evt,
				       bool originatedAtThisNd,
				       SaImmHandleT clnt_hdl,
				       MDS_DEST reply_dest);

static void immnd_evt_ccb_augment_init(IMMND_CB *cb, IMMND_EVT *evt,
				       bool originatedAtThisNd,
				       SaImmHandleT clnt_hdl,
				       MDS_DEST reply_dest);

static void immnd_evt_ccb_augment_admo(IMMND_CB *cb, IMMND_EVT *evt,
				       bool originatedAtThisNd,
				       SaImmHandleT clnt_hdl,
				       MDS_DEST reply_dest);

static void immnd_evt_proc_admop(IMMND_CB *cb, IMMND_EVT *evt,
				 bool originatedAtThisNd, SaImmHandleT clnt_hdl,
				 MDS_DEST reply_dest);

static void immnd_evt_proc_class_create(IMMND_CB *cb, IMMND_EVT *evt,
					bool originatedAtThisNd,
					SaImmHandleT clnt_hdl,
					MDS_DEST reply_dest);

static void immnd_evt_proc_class_delete(IMMND_CB *cb, IMMND_EVT *evt,
					bool originatedAtThisNd,
					SaImmHandleT clnt_hdl,
					MDS_DEST reply_dest);

static void immnd_evt_proc_object_create(IMMND_CB *cb, IMMND_EVT *evt,
					 bool originatedAtThisNd,
					 SaImmHandleT clnt_hdl,
					 MDS_DEST reply_dest);

static void immnd_evt_proc_rt_object_create(IMMND_CB *cb, IMMND_EVT *evt,
					    bool originatedAtThisNd,
					    SaImmHandleT clnt_hdl,
					    MDS_DEST reply_dest);

static void immnd_evt_proc_object_modify(IMMND_CB *cb, IMMND_EVT *evt,
					 bool originatedAtThisNd,
					 SaImmHandleT clnt_hdl,
					 MDS_DEST reply_dest);

static void immnd_evt_proc_rt_object_modify(IMMND_CB *cb, IMMND_EVT *evt,
					    bool originatedAtThisNd,
					    SaImmHandleT clnt_hdl,
					    MDS_DEST reply_dest,
					    SaUint64T msgNo);

static void immnd_evt_proc_object_delete(IMMND_CB *cb, IMMND_EVT *evt,
					 bool originatedAtThisNd,
					 SaImmHandleT clnt_hdl,
					 MDS_DEST reply_dest);

static void immnd_evt_proc_rt_object_delete(IMMND_CB *cb, IMMND_EVT *evt,
					    bool originatedAtThisNd,
					    SaImmHandleT clnt_hdl,
					    MDS_DEST reply_dest);

static void immnd_evt_pbe_admop_rsp(IMMND_CB *cb, IMMND_EVT *evt,
				    bool originatedAtThisNd,
				    SaImmHandleT clnt_hdl, MDS_DEST reply_dest);

static void immnd_evt_proc_object_sync(IMMND_CB *cb, IMMND_EVT *evt,
				       bool originatedAtThisNd,
				       SaImmHandleT clnt_hdl,
				       MDS_DEST reply_dest, SaUint64T msgNo);

static void immnd_evt_proc_ccb_apply(IMMND_CB *cb, IMMND_EVT *evt,
				     bool originatedAtThisNd,
				     SaImmHandleT clnt_hdl, MDS_DEST reply_dest,
				     bool validateOnly);

static void immnd_evt_proc_ccb_compl_rsp(IMMND_CB *cb, IMMND_EVT *evt,
					 bool originatedAtThisNd,
					 SaImmHandleT clnt_hdl,
					 MDS_DEST reply_dest);

static uint32_t immnd_evt_proc_admop_rsp(IMMND_CB *cb, IMMND_EVT *evt,
					 IMMSV_SEND_INFO *sinfo, bool async,
					 bool local);

static uint32_t immnd_evt_proc_fevs_forward(IMMND_CB *cb, IMMND_EVT *evt,
					    IMMSV_SEND_INFO *sinfo,
					    bool onStack, bool newMsg);
static uint32_t immnd_evt_proc_fevs_rcv(IMMND_CB *cb, IMMND_EVT *evt,
					IMMSV_SEND_INFO *sinfo);

static uint32_t immnd_evt_proc_intro_rsp(IMMND_CB *cb, IMMND_EVT *evt,
					 IMMSV_SEND_INFO *sinfo);

static uint32_t immnd_evt_proc_sync_req(IMMND_CB *cb, IMMND_EVT *evt,
					IMMSV_SEND_INFO *sinfo);

static uint32_t immnd_evt_proc_loading_ok(IMMND_CB *cb, IMMND_EVT *evt,
					  IMMSV_SEND_INFO *sinfo);

static uint32_t immnd_evt_proc_dump_ok(IMMND_CB *cb, IMMND_EVT *evt,
				       IMMSV_SEND_INFO *sinfo);

static uint32_t immnd_evt_proc_start_sync(IMMND_CB *cb, IMMND_EVT *evt,
					  IMMSV_SEND_INFO *sinfo);

static uint32_t immnd_evt_proc_abort_sync(IMMND_CB *cb, IMMND_EVT *evt,
					  IMMSV_SEND_INFO *sinfo);
static uint32_t immnd_evt_proc_pbe_prto_purge_mutations(IMMND_CB *cb,
							IMMND_EVT *evt,
							IMMSV_SEND_INFO *sinfo);

static uint32_t immnd_evt_proc_class_desc_get(IMMND_CB *cb, IMMND_EVT *evt,
					      IMMSV_SEND_INFO *sinfo);
static uint32_t immnd_evt_proc_search_init(IMMND_CB *cb, IMMND_EVT *evt,
					   IMMSV_SEND_INFO *sinfo);
static uint32_t immnd_evt_proc_search_next(IMMND_CB *cb, IMMND_EVT *evt,
					   IMMSV_SEND_INFO *sinfo);

static uint32_t immnd_evt_proc_rt_update_pull(IMMND_CB *cb, IMMND_EVT *evt,
					      IMMSV_SEND_INFO *sinfo);

static uint32_t immnd_evt_proc_oi_att_pull_rpl(IMMND_CB *cb, IMMND_EVT *evt,
					       IMMSV_SEND_INFO *sinfo);

static uint32_t immnd_evt_proc_remote_search_rsp(IMMND_CB *cb, IMMND_EVT *evt,
						 IMMSV_SEND_INFO *sinfo);

static uint32_t immnd_evt_proc_search_finalize(IMMND_CB *cb, IMMND_EVT *evt,
					       IMMSV_SEND_INFO *sinfo);

static uint32_t immnd_evt_proc_accessor_get(IMMND_CB *cb, IMMND_EVT *evt,
					    IMMSV_SEND_INFO *sinfo);

static uint32_t immnd_evt_proc_safe_read(IMMND_CB *cb, IMMND_EVT *evt,
					 IMMSV_SEND_INFO *sinfo);

static uint32_t immnd_evt_proc_mds_evt(IMMND_CB *cb, IMMND_EVT *evt);

static void immnd_evt_ccb_abort(IMMND_CB *cb, SaUint32T ccbId,
				SaUint32T **clientArr, SaUint32T *clArrSize,
				SaUint32T *nodeId);

static uint32_t immnd_evt_proc_reset(IMMND_CB *cb, IMMND_EVT *evt,
				     IMMSV_SEND_INFO *sinfo);

#if 0 /* Only for debug */
static void printImmValue(SaImmValueTypeT t, IMMSV_EDU_ATTR_VAL *v)
{
	switch (t) {
	case SA_IMM_ATTR_SAINT32T:
		TRACE_2("INT32: %i", v->val.saint32);
		break;
	case SA_IMM_ATTR_SAUINT32T:
		TRACE_2("UINT32: %u", v->val.sauint32);
		break;
	case SA_IMM_ATTR_SAINT64T:
		TRACE_2("INT64: %lli", v->val.saint64);
		break;
	case SA_IMM_ATTR_SAUINT64T:
		TRACE_2("UINT64: %llu", v->val.sauint64);
		break;
	case SA_IMM_ATTR_SATIMET:
		TRACE_2("TIME: %llu", v->val.satime);
		break;
	case SA_IMM_ATTR_SANAMET:
		TRACE_2("NAME: %u/%s", v->val.x.size, v->val.x.buf);
		break;
	case SA_IMM_ATTR_SAFLOATT:
		TRACE_2("FLOAT: %f", v->val.safloat);
		break;
	case SA_IMM_ATTR_SADOUBLET:
		TRACE_2("DOUBLE: %f", v->val.sadouble);
		break;
	case SA_IMM_ATTR_SASTRINGT:
		TRACE_2("STRING: %u/%s", v->val.x.size, v->val.x.buf);
		break;
	case SA_IMM_ATTR_SAANYT:
		TRACE_2("ANYT: %u/%s", v->val.x.size, v->val.x.buf);
		break;
	default:
		TRACE_2("ERRONEOUS VALUE TAG: %i", t);
		break;
	}
}
#endif

/****************************************************************************
 * Name          : immnd_evt_destroy
 *
 * Description   : Function to process the
 *                 from Applications.
 *
 * Arguments     : IMMSV_EVT *evt - Received Event structure
 *                 bool onheap - false => dont deallocate root
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 * Notes         : None.
 *****************************************************************************/

uint32_t immnd_evt_destroy(IMMSV_EVT *evt, bool onheap, uint32_t line)
{
	uint32_t rc = NCSCC_RC_SUCCESS;

	if (evt == NULL) {
		/* LOG */
		return NCSCC_RC_SUCCESS;
	}

	if (evt->info.immnd.dont_free_me)
		return NCSCC_RC_SUCCESS;

	if (evt->type != IMMSV_EVT_TYPE_IMMND)
		return NCSCC_RC_SUCCESS;

	if ((evt->info.immnd.type == IMMND_EVT_A2ND_IMM_ADMOP) ||
	    (evt->info.immnd.type == IMMND_EVT_A2ND_IMM_ADMOP_ASYNC)) {
		/*TODO See TODO 12345 code repeated (almost) in imma_om_api.c
		   free-1 */
		free(evt->info.immnd.info.admOpReq.objectName.buf);
		evt->info.immnd.info.admOpReq.objectName.buf = NULL;
		while (evt->info.immnd.info.admOpReq.params) {
			IMMSV_ADMIN_OPERATION_PARAM *p =
			    evt->info.immnd.info.admOpReq.params;
			evt->info.immnd.info.admOpReq.params = p->next;

			if (p->paramName.buf) { /*free-3 */
				free(p->paramName.buf);
				p->paramName.buf = NULL;
				p->paramName.size = 0;
			}
			immsv_evt_free_att_val(&(p->paramBuffer),
					       p->paramType); /*free-4 */
			p->next = NULL;
			free(p); /*free-2 */
		}
	} else if ((evt->info.immnd.type == IMMND_EVT_A2ND_ADMOP_RSP_2) ||
		   (evt->info.immnd.type == IMMND_EVT_A2ND_ASYNC_ADMOP_RSP_2) ||
		   (evt->info.immnd.type == IMMND_EVT_ND2ND_ADMOP_RSP_2) ||
		   (evt->info.immnd.type ==
		    IMMND_EVT_ND2ND_ASYNC_ADMOP_RSP_2)) {
		while (evt->info.immnd.info.admOpRsp.parms) {
			IMMSV_ADMIN_OPERATION_PARAM *p =
			    evt->info.immnd.info.admOpRsp.parms;
			evt->info.immnd.info.admOpRsp.parms = p->next;

			if (p->paramName.buf) { /*free-b */
				free(p->paramName.buf);
				p->paramName.buf = NULL;
				p->paramName.size = 0;
			}
			immsv_evt_free_att_val(&(p->paramBuffer),
					       p->paramType); /*free-c */
			p->next = NULL;
			free(p); /*free-a */
		}
	} else if ((evt->info.immnd.type == IMMND_EVT_D2ND_GLOB_FEVS_REQ) ||
		   (evt->info.immnd.type == IMMND_EVT_A2ND_IMM_FEVS) ||
		   (evt->info.immnd.type == IMMND_EVT_D2ND_GLOB_FEVS_REQ_2) ||
		   (evt->info.immnd.type == IMMND_EVT_A2ND_IMM_FEVS_2)) {
		if (evt->info.immnd.info.fevsReq.msg.buf != NULL) {
			free(evt->info.immnd.info.fevsReq.msg.buf);
			evt->info.immnd.info.fevsReq.msg.buf = NULL;
			evt->info.immnd.info.fevsReq.msg.size = 0;
		}
	} else if (evt->info.immnd.type == IMMND_EVT_A2ND_RT_ATT_UPPD_RSP) {
		free(evt->info.immnd.info.rtAttUpdRpl.sr.objectName.buf);
		evt->info.immnd.info.rtAttUpdRpl.sr.objectName.buf = NULL;
		evt->info.immnd.info.rtAttUpdRpl.sr.objectName.size = 0;
		immsv_evt_free_attrNames(
		    evt->info.immnd.info.rtAttUpdRpl.sr.attributeNames);
		evt->info.immnd.info.rtAttUpdRpl.sr.attributeNames = NULL;
	} else if (evt->info.immnd.type == IMMND_EVT_ND2ND_SEARCH_REMOTE) {
		free(evt->info.immnd.info.searchRemote.objectName.buf);
		evt->info.immnd.info.searchRemote.objectName.buf = NULL;
		evt->info.immnd.info.searchRemote.objectName.size = 0;
		immsv_evt_free_attrNames(
		    evt->info.immnd.info.searchRemote.attributeNames);
		evt->info.immnd.info.searchRemote.attributeNames = NULL;
	} else if (evt->info.immnd.type == IMMND_EVT_ND2ND_SEARCH_REMOTE_RSP) {
		freeSearchNext(&evt->info.immnd.info.rspSrchRmte.runtimeAttrs,
			       false);
	} else if ((evt->info.immnd.type == IMMND_EVT_A2ND_SEARCHINIT) ||
		   (evt->info.immnd.type == IMMND_EVT_A2ND_ACCESSOR_GET) ||
		   (evt->info.immnd.type == IMMND_EVT_A2ND_OBJ_SAFE_READ)) {
		free(evt->info.immnd.info.searchInit.rootName.buf);
		evt->info.immnd.info.searchInit.rootName.buf = NULL;
		evt->info.immnd.info.searchInit.rootName.size = 0;

		if (evt->info.immnd.info.searchInit.searchParam.present ==
			ImmOmSearchParameter_PR_oneAttrParam ||
		    evt->info.immnd.info.searchInit.searchParam.present ==
			ImmOmSearchParameter_PR_nonExtendedName_oneAttrParam) {
			free(evt->info.immnd.info.searchInit.searchParam.choice
				 .oneAttrParam.attrName.buf);
			evt->info.immnd.info.searchInit.searchParam.choice
			    .oneAttrParam.attrName.buf = NULL;
			evt->info.immnd.info.searchInit.searchParam.choice
			    .oneAttrParam.attrName.size = 0;
			immsv_evt_free_att_val(
			    &(evt->info.immnd.info.searchInit.searchParam.choice
				  .oneAttrParam.attrValue),
			    evt->info.immnd.info.searchInit.searchParam.choice
				.oneAttrParam.attrValueType);
		}

		immsv_evt_free_attrNames(
		    evt->info.immnd.info.searchInit.attributeNames);
		evt->info.immnd.info.searchInit.attributeNames = NULL;
	} else if ((evt->info.immnd.type == IMMND_EVT_ND2ND_SYNC_FINALIZE) ||
		   (evt->info.immnd.type == IMMND_EVT_ND2ND_SYNC_FINALIZE_2)) {
		immsv_evt_free_admo(evt->info.immnd.info.finSync.adminOwners);
		evt->info.immnd.info.finSync.adminOwners = NULL;
		immsv_evt_free_impl(evt->info.immnd.info.finSync.implementers);
		evt->info.immnd.info.finSync.implementers = NULL;
		immsv_evt_free_classList(evt->info.immnd.info.finSync.classes);
		evt->info.immnd.info.finSync.classes = NULL;
		if (evt->info.immnd.type == IMMND_EVT_ND2ND_SYNC_FINALIZE_2) {
			immsv_evt_free_ccbOutcomeList(
			    evt->info.immnd.info.finSync.ccbResults);
			evt->info.immnd.info.finSync.ccbResults = NULL;
		}
	} else if ((evt->info.immnd.type == IMMND_EVT_A2ND_OBJ_CREATE) ||
		   (evt->info.immnd.type == IMMND_EVT_A2ND_OI_OBJ_CREATE) ||
		   (evt->info.immnd.type == IMMND_EVT_A2ND_OBJ_CREATE_2) ||
		   (evt->info.immnd.type == IMMND_EVT_A2ND_OI_OBJ_CREATE_2)) {
		free(evt->info.immnd.info.objCreate.className.buf);
		evt->info.immnd.info.objCreate.className.buf = NULL;
		evt->info.immnd.info.objCreate.className.size = 0;

		free(evt->info.immnd.info.objCreate.parentOrObjectDn.buf);
		evt->info.immnd.info.objCreate.parentOrObjectDn.buf = NULL;
		evt->info.immnd.info.objCreate.parentOrObjectDn.size = 0;

		immsv_free_attrvalues_list(
		    evt->info.immnd.info.objCreate.attrValues);
		evt->info.immnd.info.objCreate.attrValues = NULL;
	} else if ((evt->info.immnd.type == IMMND_EVT_A2ND_OBJ_MODIFY) ||
		   (evt->info.immnd.type == IMMND_EVT_A2ND_OI_OBJ_MODIFY)) {
		free(evt->info.immnd.info.objModify.objectName.buf);
		evt->info.immnd.info.objModify.objectName.buf = NULL;
		evt->info.immnd.info.objModify.objectName.size = 0;

		immsv_free_attrmods(evt->info.immnd.info.objModify.attrMods);
		evt->info.immnd.info.objModify.attrMods = NULL;
	} else if ((evt->info.immnd.type == IMMND_EVT_A2ND_OBJ_DELETE) ||
		   (evt->info.immnd.type == IMMND_EVT_A2ND_OI_OBJ_DELETE)) {
		free(evt->info.immnd.info.objDelete.objectName.buf);
		evt->info.immnd.info.objDelete.objectName.buf = NULL;
		evt->info.immnd.info.objDelete.objectName.size = 0;
	} else if ((evt->info.immnd.type == IMMND_EVT_A2ND_OBJ_SYNC) ||
		   (evt->info.immnd.type == IMMND_EVT_A2ND_OBJ_SYNC_2)) {
		IMMSV_OM_OBJECT_SYNC *obj_sync =
		    &(evt->info.immnd.info.obj_sync);
		IMMSV_OM_OBJECT_SYNC *tmp = NULL;
		bool top = true;

		while (obj_sync) {
			free(obj_sync->className.buf);
			obj_sync->className.buf = NULL;
			obj_sync->className.size = 0;

			free(obj_sync->objectName.buf);
			obj_sync->objectName.buf = NULL;
			obj_sync->objectName.size = 0;

			immsv_free_attrvalues_list(obj_sync->attrValues);
			obj_sync->attrValues = NULL;

			if (evt->info.immnd.type == IMMND_EVT_A2ND_OBJ_SYNC) {
				obj_sync = NULL;
			} else {
				tmp = obj_sync;
				obj_sync = obj_sync->next;
				tmp->next = NULL;
				if (top) { /* Top object resides in evt on
					      stack. */
					top = false;
				} else {
					free(tmp);
				}
			}
		}

	} else if ((evt->info.immnd.type == IMMND_EVT_A2ND_CLASS_CREATE) ||
		   (evt->info.immnd.type == IMMND_EVT_A2ND_CLASS_DESCR_GET) ||
		   (evt->info.immnd.type == IMMND_EVT_A2ND_CLASS_DELETE)) {
		free(evt->info.immnd.info.classDescr.className.buf);
		evt->info.immnd.info.classDescr.className.buf = NULL;
		evt->info.immnd.info.classDescr.className.size = 0;

		immsv_free_attrdefs_list(
		    evt->info.immnd.info.classDescr.attrDefinitions);
		evt->info.immnd.info.classDescr.attrDefinitions = NULL;

	} else if ((evt->info.immnd.type == IMMND_EVT_A2ND_OI_IMPL_SET) ||
		   (evt->info.immnd.type == IMMND_EVT_A2ND_OI_IMPL_SET_2) ||
		   (evt->info.immnd.type == IMMND_EVT_D2ND_IMPLSET_RSP) ||
		   (evt->info.immnd.type == IMMND_EVT_D2ND_IMPLSET_RSP_2) ||
		   (evt->info.immnd.type == IMMND_EVT_A2ND_OI_CL_IMPL_SET) ||
		   (evt->info.immnd.type == IMMND_EVT_A2ND_OI_CL_IMPL_REL) ||
		   (evt->info.immnd.type == IMMND_EVT_A2ND_OI_OBJ_IMPL_SET) ||
		   (evt->info.immnd.type == IMMND_EVT_A2ND_OI_OBJ_IMPL_REL)) {
		free(evt->info.immnd.info.implSet.impl_name.buf);
		evt->info.immnd.info.implSet.impl_name.buf = NULL;
		evt->info.immnd.info.implSet.impl_name.size = 0;
	} else if ((evt->info.immnd.type == IMMND_EVT_A2ND_ADMO_SET) ||
		   (evt->info.immnd.type == IMMND_EVT_A2ND_ADMO_RELEASE) ||
		   (evt->info.immnd.type == IMMND_EVT_A2ND_ADMO_CLEAR)) {
		immsv_evt_free_name_list(
		    evt->info.immnd.info.admReq.objectNames);
		evt->info.immnd.info.admReq.objectNames = NULL;
	} else if ((evt->info.immnd.type ==
		    IMMND_EVT_A2ND_CCB_OBJ_CREATE_RSP_2) ||
		   (evt->info.immnd.type ==
		    IMMND_EVT_A2ND_CCB_OBJ_DELETE_RSP_2) ||
		   (evt->info.immnd.type ==
		    IMMND_EVT_A2ND_CCB_OBJ_MODIFY_RSP_2) ||
		   (evt->info.immnd.type ==
		    IMMND_EVT_A2ND_CCB_COMPLETED_RSP_2)) {
		free(evt->info.immnd.info.ccbUpcallRsp.errorString.buf);
		evt->info.immnd.info.ccbUpcallRsp.errorString.buf = NULL;
		evt->info.immnd.info.ccbUpcallRsp.errorString.size = 0;
	}

	if (onheap) {
		free(evt);
	}
	return rc;
}

void immnd_process_evt(void)
{
	IMMND_CB *cb = immnd_cb;
	uint32_t rc = NCSCC_RC_SUCCESS;

	IMMSV_EVT *evt;

	evt = (IMMSV_EVT *)ncs_ipc_non_blk_recv(&immnd_cb->immnd_mbx);

	if (evt == NULL) {
		LOG_ER("No mbx message although indicated in fd!");
		TRACE_LEAVE();
		return;
	}

	if (evt->type != IMMSV_EVT_TYPE_IMMND) {
		LOG_ER("IMMND - Unknown Event");
		immnd_evt_destroy(evt, true, __LINE__);
		return;
	}

	if ((evt->info.immnd.type != IMMND_EVT_D2ND_GLOB_FEVS_REQ) &&
	    (evt->info.immnd.type != IMMND_EVT_D2ND_GLOB_FEVS_REQ_2))
		immsv_msg_trace_rec(evt->sinfo.dest, evt);

	switch (evt->info.immnd.type) {
	case IMMND_EVT_MDS_INFO:
		rc = immnd_evt_proc_mds_evt(cb, &evt->info.immnd);
		break;

	case IMMND_EVT_CB_DUMP:
		rc = immnd_evt_proc_cb_dump(cb);
		break;

	/* Agent to ND */
	case IMMND_EVT_A2ND_IMM_INIT:
		rc = immnd_evt_proc_imm_init(cb, &evt->info.immnd, &evt->sinfo,
					     true);
		break;

	case IMMND_EVT_A2ND_IMM_OI_INIT:
		rc = immnd_evt_proc_imm_init(cb, &evt->info.immnd, &evt->sinfo,
					     false);
		break;

	case IMMND_EVT_A2ND_IMM_FINALIZE:
		rc = immnd_evt_proc_imm_finalize(cb, &evt->info.immnd,
						 &evt->sinfo, true);
		break;

	case IMMND_EVT_A2ND_IMM_OI_FINALIZE:
		rc = immnd_evt_proc_imm_finalize(cb, &evt->info.immnd,
						 &evt->sinfo, false);
		break;

	case IMMND_EVT_A2ND_CL_TIMEOUT:
		rc = immnd_evt_proc_cl_imma_timeout(cb, &evt->info.immnd);
		break;

	case IMMND_EVT_A2ND_IMM_OM_RESURRECT:
		rc = immnd_evt_proc_imm_resurrect(cb, &evt->info.immnd,
						  &evt->sinfo, true);
		break;

	case IMMND_EVT_A2ND_IMM_OI_RESURRECT:
		rc = immnd_evt_proc_imm_resurrect(cb, &evt->info.immnd,
						  &evt->sinfo, false);
		break;

	case IMMND_EVT_A2ND_IMM_CLIENTHIGH:
		LOG_WA(
		    "Deprecated message type IMMND_EVT_A2ND_IMM_CLIENTHIGH - ignoring");
		break;

	case IMMND_EVT_A2ND_IMM_OM_CLIENTHIGH:
		rc = immnd_evt_proc_imm_client_high(cb, &evt->info.immnd,
						    &evt->sinfo, true);
		break;

	case IMMND_EVT_A2ND_IMM_OI_CLIENTHIGH:
		rc = immnd_evt_proc_imm_client_high(cb, &evt->info.immnd,
						    &evt->sinfo, false);
		break;

	case IMMND_EVT_A2ND_RECOVER_CCB_OUTCOME:
		rc = immnd_evt_proc_recover_ccb_result(cb, &evt->info.immnd,
						       &evt->sinfo);
		break;

	case IMMND_EVT_A2ND_SYNC_FINALIZE:
		rc = immnd_evt_proc_sync_finalize(cb, &evt->info.immnd,
						  &evt->sinfo);
		break;

	case IMMND_EVT_A2ND_IMM_ADMINIT:
		rc = immnd_evt_proc_admowner_init(cb, &evt->info.immnd,
						  &evt->sinfo);
		break;

	case IMMND_EVT_A2ND_OI_IMPL_SET:
	case IMMND_EVT_A2ND_OI_IMPL_SET_2:
		rc = immnd_evt_proc_impl_set(cb, &evt->info.immnd, &evt->sinfo);
		break;

	case IMMND_EVT_A2ND_CCBINIT:
		rc = immnd_evt_proc_ccb_init(cb, &evt->info.immnd, &evt->sinfo);
		break;

	case IMMND_EVT_A2ND_OI_OBJ_MODIFY:
		rc =
		    immnd_evt_proc_rt_update(cb, &evt->info.immnd, &evt->sinfo);
		break;

	case IMMND_EVT_A2ND_IMM_FEVS:
	case IMMND_EVT_A2ND_IMM_FEVS_2:
		rc = immnd_evt_proc_fevs_forward(cb, &evt->info.immnd,
						 &evt->sinfo, false, true);
		break;

	case IMMND_EVT_A2ND_CLASS_DESCR_GET:
		rc = immnd_evt_proc_class_desc_get(cb, &evt->info.immnd,
						   &evt->sinfo);
		break;

	case IMMND_EVT_A2ND_SEARCHINIT:
		rc = immnd_evt_proc_search_init(cb, &evt->info.immnd,
						&evt->sinfo);
		break;

	case IMMND_EVT_A2ND_SEARCHNEXT:
		rc = immnd_evt_proc_search_next(cb, &evt->info.immnd,
						&evt->sinfo);
		break;

	case IMMND_EVT_A2ND_OBJ_SAFE_READ:
		rc =
		    immnd_evt_proc_safe_read(cb, &evt->info.immnd, &evt->sinfo);
		break;

	case IMMND_EVT_A2ND_ACCESSOR_GET:
		rc = immnd_evt_proc_accessor_get(cb, &evt->info.immnd,
						 &evt->sinfo);
		break;

	case IMMND_EVT_A2ND_RT_ATT_UPPD_RSP:
		rc = immnd_evt_proc_oi_att_pull_rpl(cb, &evt->info.immnd,
						    &evt->sinfo);
		break;

	case IMMND_EVT_A2ND_SEARCHFINALIZE:
		rc = immnd_evt_proc_search_finalize(cb, &evt->info.immnd,
						    &evt->sinfo);
		break;

	case IMMND_EVT_A2ND_ADMOP_RSP:
	case IMMND_EVT_A2ND_ADMOP_RSP_2:
		rc = immnd_evt_proc_admop_rsp(cb, &evt->info.immnd, &evt->sinfo,
					      false, true);
		break;

	case IMMND_EVT_A2ND_ASYNC_ADMOP_RSP:
	case IMMND_EVT_A2ND_ASYNC_ADMOP_RSP_2:
		rc = immnd_evt_proc_admop_rsp(cb, &evt->info.immnd, &evt->sinfo,
					      true, true);
		break;

	case IMMND_EVT_ND2ND_ADMOP_RSP:
	case IMMND_EVT_ND2ND_ADMOP_RSP_2:
		rc = immnd_evt_proc_admop_rsp(cb, &evt->info.immnd, &evt->sinfo,
					      false, false);
		break;

	case IMMND_EVT_ND2ND_ASYNC_ADMOP_RSP:
	case IMMND_EVT_ND2ND_ASYNC_ADMOP_RSP_2:
		rc = immnd_evt_proc_admop_rsp(cb, &evt->info.immnd, &evt->sinfo,
					      true, false);
		break;

	case IMMND_EVT_ND2ND_SEARCH_REMOTE:
		rc = immnd_evt_proc_rt_update_pull(cb, &evt->info.immnd,
						   &evt->sinfo);
		break;

	case IMMND_EVT_ND2ND_SEARCH_REMOTE_RSP:
		rc = immnd_evt_proc_remote_search_rsp(cb, &evt->info.immnd,
						      &evt->sinfo);
		break;

	/* D to ND */

	case IMMND_EVT_D2ND_INTRO_RSP:
		rc =
		    immnd_evt_proc_intro_rsp(cb, &evt->info.immnd, &evt->sinfo);
		break;

	case IMMND_EVT_D2ND_SYNC_REQ:
		rc = immnd_evt_proc_sync_req(cb, &evt->info.immnd, &evt->sinfo);
		break;

	case IMMND_EVT_D2ND_LOADING_OK:
		rc = immnd_evt_proc_loading_ok(cb, &evt->info.immnd,
					       &evt->sinfo);
		break;

	case IMMND_EVT_D2ND_DUMP_OK:
		rc = immnd_evt_proc_dump_ok(cb, &evt->info.immnd, &evt->sinfo);
		break;

	case IMMND_EVT_D2ND_SYNC_START:
		rc = immnd_evt_proc_start_sync(cb, &evt->info.immnd,
					       &evt->sinfo);
		break;

	case IMMND_EVT_D2ND_SYNC_ABORT:
		rc = immnd_evt_proc_abort_sync(cb, &evt->info.immnd,
					       &evt->sinfo);
		break;

	case IMMND_EVT_D2ND_PBE_PRTO_PURGE_MUTATIONS:
		rc = immnd_evt_proc_pbe_prto_purge_mutations(
		    cb, &evt->info.immnd, &evt->sinfo);
		break;

	case IMMND_EVT_D2ND_GLOB_FEVS_REQ:
	case IMMND_EVT_D2ND_GLOB_FEVS_REQ_2:
		rc = immnd_evt_proc_fevs_rcv(cb, &evt->info.immnd, &evt->sinfo);
		break;

	case IMMND_EVT_D2ND_RESET:
		rc = immnd_evt_proc_reset(cb, &evt->info.immnd, &evt->sinfo);
		break;

	default:
		LOG_ER("UNPACK FAILURE, unrecognized message type: %u",
		       evt->info.immnd.type);
		rc = NCSCC_RC_FAILURE;
		break;
	}

	if (rc != NCSCC_RC_SUCCESS) {
		LOG_WA("Error code %u returned for message type %u - ignoring",
		       rc, evt->info.immnd.type);
	}

	/* Free the Event */
	immnd_evt_destroy(evt, true, __LINE__);

	return;
}

/****************************************************************************
 * Name          : immnd_evt_proc_imm_init
 *
 * Description   : Function to process the SaImmOmInitialize and
 *                 SaImmOiInitialize calls from IMM Agents.
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMND_EVT *evt - Received Event structure
 *                 IMMSV_SEND_INFO *sinfo - sender info
 *                 bool isOm - true => OM, false => OI
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 * Notes         : None.
 *****************************************************************************/
static uint32_t immnd_evt_proc_imm_init(IMMND_CB *cb, IMMND_EVT *evt,
					IMMSV_SEND_INFO *sinfo, bool isOm)
{
	IMMSV_EVT send_evt;
	SaAisErrorT error;
	IMMND_IMM_CLIENT_NODE *cl_node = NULL;
	SaUint64T clientId = 0LL;
	memset(&send_evt, '\0', sizeof(IMMSV_EVT));

	int load_pid = immModel_getLoader(cb);
	int sync_pid = (!load_pid && (cb->syncPid > 0)) ? (cb->syncPid) : 0;
	int pbe_pid = (!load_pid && (cb->pbePid > 0)) ? (cb->pbePid) : 0;

	if (sinfo->pid == 0) {
		LOG_WA("%s: PID 0 (%d) for %" PRIx64 ", MDS problem?",
		       __FUNCTION__, evt->info.initReq.client_pid, sinfo->dest);
		error = SA_AIS_ERR_LIBRARY;
		goto agent_rsp;
	}

	if (load_pid > 0) {
		if (sinfo->pid == load_pid) {
			TRACE_2("Loader attached, pid: %u", load_pid);
		} else {
			TRACE_2(
			    "Rejecting OM client attach during loading, pid %u != %u",
			    sinfo->pid, load_pid);
			error = SA_AIS_ERR_TRY_AGAIN;
			goto agent_rsp;
		}
	} else if (sinfo->pid == cb->preLoadPid) {
		LOG_IN("2PBE Pre-loader attached");
	} else if (load_pid < 0) {
		TRACE_2(
		    "Rejecting OM client attach. Waiting for loading or sync to complete");
		error = SA_AIS_ERR_TRY_AGAIN;
		goto agent_rsp;
	}

	OsafImmAccessControlModeT mode = immModel_accessControlMode(immnd_cb);
	if (mode != ACCESS_CONTROL_DISABLED) {
		/* allow access using white list approach */
		if (sinfo->uid == 0) {
			TRACE("superuser");
		} else if (getgid() == sinfo->gid) {
			TRACE("same group");
		} else {
			const char *authorized_group =
			    immModel_authorizedGroup(immnd_cb);
			if ((authorized_group != NULL) &&
			    (osaf_user_is_member_of_group(sinfo->uid,
							  authorized_group))) {
				TRACE("configured group");
			} else {
				if (mode == ACCESS_CONTROL_PERMISSIVE) {
					struct passwd *pwd =
					    getpwuid(sinfo->uid);
					if (pwd != NULL)
						syslog(
						    LOG_AUTH,
						    "access violation by %s(uid=%d)",
						    pwd->pw_name, sinfo->uid);
					TRACE_2(
					    "access violation, uid:%d, pid:%d, group_name:%s",
					    sinfo->uid, sinfo->pid,
					    authorized_group);
				} else {
					// mode ENFORCING
					struct passwd *pwd =
					    getpwuid(sinfo->uid);
					if (pwd != NULL)
						syslog(
						    LOG_AUTH,
						    "access denied for %s(uid=%d)",
						    pwd->pw_name, sinfo->uid);
					TRACE_2(
					    "access denied, uid:%d, pid:%d, group_name:%s",
					    sinfo->uid, sinfo->pid,
					    authorized_group);
					error = SA_AIS_ERR_ACCESS_DENIED;
					goto agent_rsp;
				}
			}
		}
	}

	if (evt->info.initReq.version.minorVersion >= 0x12 &&
	    evt->info.initReq.version.majorVersion == 0x2 &&
	    evt->info.initReq.version.releaseCode == 'A') {
		if (!cb->isClmNodeJoined && cb->mIntroduced != 2) {
			error = SA_AIS_ERR_UNAVAILABLE;
			LOG_ER("CLM node went down nodeJoined=%x",
			       cb->isClmNodeJoined);
			goto clm_left;
		}
	}

	cl_node = calloc(1, sizeof(IMMND_IMM_CLIENT_NODE));
	if (cl_node == NULL) {
		LOG_ER("IMMND - Client Alloc Failed");
		error = SA_AIS_ERR_NO_MEMORY;
		goto agent_rsp;
	}

	clientId = cb->cli_id_gen++;
	if (cb->cli_id_gen == 0xffffffff) {
		cb->cli_id_gen = 1;
		/* Counter wrap arround causes risk (low) for collisions.
		   Symtom would be that this client node_add would fail below.
		   This is why we change the error code to try-again.
		 */
	}

	/*The id generated by the ND is only valid as long as this ND process
	   survives => restart of ND means loss of all client connections.
	   Return ND pid in reply to use in handshake ??
	 */

	cl_node->imm_app_hdl = m_IMMSV_PACK_HANDLE(clientId, cb->node_id);

	cl_node->agent_mds_dest = sinfo->dest;
	cl_node->version = evt->info.initReq.version;
	cl_node->sv_id = (isOm) ? NCSMDS_SVC_ID_IMMA_OM : NCSMDS_SVC_ID_IMMA_OI;

	if (immnd_client_node_add(cb, cl_node) != NCSCC_RC_SUCCESS) {
		LOG_WA(
		    "IMMND - Client Tree Add Failed client id wrap-arround?");
		free(cl_node);
		error = SA_AIS_ERR_TRY_AGAIN; /* cl-id wraparround collision? */
		goto agent_rsp;
	}

	TRACE_2("Added client with id: %llx <node:%x, count:%u>",
		cl_node->imm_app_hdl, cb->node_id, (SaUint32T)clientId);

	if (sync_pid && (sinfo->pid == sync_pid)) {
		TRACE_2("Sync agent attached, pid: %u", sync_pid);
		cl_node->mIsSync = 1;
	} else if (pbe_pid && (sinfo->pid == pbe_pid) && !isOm &&
		   !(cl_node->mIsPbe)) {
		LOG_NO("Persistent Back End OI attached, pid: %u", pbe_pid);
		cl_node->mIsPbe = 1;
	}

	send_evt.info.imma.info.initRsp.immHandle = cl_node->imm_app_hdl;
	error = SA_AIS_OK;

clm_left:
agent_rsp:
	send_evt.type = IMMSV_EVT_TYPE_IMMA;
	send_evt.info.imma.type = IMMA_EVT_ND2A_IMM_INIT_RSP;
	send_evt.info.imma.info.initRsp.error = error;

	return immnd_mds_send_rsp(cb, sinfo, &send_evt);
}

/****************************************************************************
 * Name          : immnd_evt_proc_search_init
 *
 * Description   : Function to process the SaImmOmSearchInitialize call.
 *                 Note that this is a read, local to the ND (does not go
 *                 over FEVS).
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMND_EVT *evt - Received Event structure
 *                 IMMSV_SEND_INFO *sinfo - sender info
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 * Notes         : None.
 *****************************************************************************/
static uint32_t immnd_evt_proc_search_init(IMMND_CB *cb, IMMND_EVT *evt,
					   IMMSV_SEND_INFO *sinfo)
{
	IMMSV_EVT send_evt;
	uint32_t rc = NCSCC_RC_SUCCESS;
	SaAisErrorT error = SA_AIS_OK;
	void *searchOp = NULL;
	IMMND_IMM_CLIENT_NODE *cl_node = NULL;
	IMMND_OM_SEARCH_NODE *sn = NULL;
	uint16_t searchOpCount = 0;

	/* Official DUMP/BACKUP assumed when search is global for only
	 * persistent attrs and done at SC. */
	bool officialDump =
	    cb->mCanBeCoord &&
	    (((SaImmSearchOptionsT)evt->info.searchInit.searchOptions) &
	     SA_IMM_SEARCH_PERSISTENT_ATTRS) &&
	    (evt->info.searchInit.searchParam.present ==
	     ImmOmSearchParameter_PR_NOTHING) &&
	    (evt->info.searchInit.rootName.size == 0);

	bool isSync =
	    cb->mIsCoord && (cb->syncPid > 0) &&
	    (((SaImmSearchOptionsT)evt->info.searchInit.searchOptions) &
	     SA_IMM_SEARCH_SYNC_CACHED_ATTRS);

	memset(&send_evt, '\0', sizeof(IMMSV_EVT));
	send_evt.type = IMMSV_EVT_TYPE_IMMA;
	send_evt.info.imma.type = IMMA_EVT_ND2A_SEARCHINIT_RSP;
	TRACE_2("SEARCH INIT for root:%s", evt->info.searchInit.rootName.buf);

	/*Look up client-node */
	immnd_client_node_get(cb, evt->info.searchInit.client_hdl, &cl_node);
	if (cl_node == NULL || cl_node->mIsStale) {
		LOG_WA("IMMND - Client Node Get Failed for client handle: %llu",
		       evt->info.searchInit.client_hdl);

		send_evt.info.imma.info.searchInitRsp.error =
		    SA_AIS_ERR_BAD_HANDLE;
		goto agent_rsp;
	}

	if (officialDump) {
		if (immModel_immNotWritable(cb) ||
		    (cb->mState != IMM_SERVER_READY)) {
			LOG_WA(
			    "Cannot allow official dump/backup when imm-sync is in progress");
			error = SA_AIS_ERR_TRY_AGAIN;
			goto agent_rsp;
		}

		if (cb->m2Pbe) {
			if (cb->mIsOtherScUp &&
			    immModel_oneSafe2PBEAllowed(cb)) {
				LOG_ER(
				    "ERR_NO_RESOURCES: Cannot allow official dump/backup "
				    "when 1-safe2PBE is allowed!!");
				/* This is simply a guard. Only reason it is not
				   an assert is that this does not signify
				   imm-ram inconsistency, but a grave risk of
				   causing an inconsistent joining PBE in 2PBE.
				   We should never end up here. */
				error = SA_AIS_ERR_NO_RESOURCES;
				goto agent_rsp;
			}

			if ((!cb->mIsCoord) && immModel_pbeNotWritable(cb) &&
			    !immModel_ccbsTerminated(cb, true)) {
				LOG_WA(
				    "ERR_NO_RESOURCES: Active Ccbs still exist in the system");
				/*Not sure this is a problem though. These ccbs
				  are dommed. They will not be allowed to apply
				  untill we are 2-safe. And any ccbs that
				  created ops before 2-safe will fail in the
				  apply because object count does not match at
				  slave. May need a way to get out of this
				  state, such as starting a sync despite that we
				  dont really need a sync.
				*/
				error = SA_AIS_ERR_NO_RESOURCES;
				goto agent_rsp;
			}
		}
	}

	if (isSync) {
		/* Special check only for sync iterator. */
		if (!immnd_is_immd_up(cb)) {
			LOG_WA(
			    "ERR_TRY_AGAIN: IMMD is down can not progress with sync");
			error = SA_AIS_ERR_TRY_AGAIN;
			goto agent_rsp;
		}
	}

	sn = cl_node->searchOpList;
	while (sn) {
		++searchOpCount;
		sn = sn->next;
	}
	if (searchOpCount >= 5000) {
		LOG_WA(
		    "ERR_NO_RESOURCES: Too many search operations (%u) on OM handle -"
		    " probable resource leak.",
		    searchOpCount);
		error = SA_AIS_ERR_NO_RESOURCES;
		goto agent_rsp;
	}

	error = immModel_searchInitialize(cb, &(evt->info.searchInit),
					  &searchOp, isSync, false);

	if ((error == SA_AIS_OK) && isSync) {
		/* Special processing only for sync iterator. */
		IMMSV_EVT send_evt2;
		memset(&send_evt2, '\0', sizeof(IMMSV_EVT));
		send_evt2.type = IMMSV_EVT_TYPE_IMMD;
		send_evt2.info.immd.type = IMMD_EVT_ND2D_SYNC_FEVS_BASE;
		send_evt2.info.immd.info.syncFevsBase.fevsBase =
		    cb->highestProcessed;
		send_evt2.info.immd.info.syncFevsBase.client_hdl =
		    evt->info.searchInit.client_hdl;

		cb->fevs_replies_pending++; /*flow control */
		if (cb->fevs_replies_pending > 1) {
			TRACE("Messages pending:%u", cb->fevs_replies_pending);
		}

		rc = immnd_mds_msg_send(cb, NCSMDS_SVC_ID_IMMD,
					cb->immd_mdest_id, &send_evt2);

		if (rc != NCSCC_RC_SUCCESS) {
			LOG_ER("IMMND - Send of SYNC_FEVS_BASE message failed");
			error = SA_AIS_ERR_TRY_AGAIN;
			cb->fevs_replies_pending--;
		}
	}

	/*Generate search-id */
	if (error == SA_AIS_OK) {
		sn = calloc(1, sizeof(IMMND_OM_SEARCH_NODE));
		if (sn == NULL) {
			send_evt.info.imma.info.searchInitRsp.error =
			    SA_AIS_ERR_NO_MEMORY;
			goto agent_rsp;
		}

		sn->searchId = cb->cli_id_gen++;
		if (cb->cli_id_gen == 0xffffffff) { /* handle wrap arround */
			cb->cli_id_gen = 1;
		}
		sn->searchOp = searchOp;
		sn->next = cl_node->searchOpList;
		cl_node->searchOpList = sn;
		send_evt.info.imma.info.searchInitRsp.searchId = sn->searchId;

		if (officialDump) { /*=>DUMP/BACKUP*/
			osafassert(!immModel_immNotWritable(cb));
			TRACE_2("SEARCH INIT tentatively adj epoch to "
				":%u & adnnounce dump",
				cb->mMyEpoch + 1);
			immnd_announceDump(cb);
		}
	}

agent_rsp:
	if (error != SA_AIS_OK) {
		immModel_deleteSearchOp(searchOp);
		searchOp = NULL;
	}
	send_evt.info.imma.info.searchInitRsp.error = error;
	rc = immnd_mds_send_rsp(cb, sinfo, &send_evt);
	return rc;
}

void search_req_continue(IMMND_CB *cb, IMMSV_OM_RSP_SEARCH_REMOTE *reply,
			 SaUint32T reqConn)
{
	IMMSV_EVT send_evt;
	IMMND_IMM_CLIENT_NODE *cl_node = NULL;
	IMMSV_OM_RSP_SEARCH_NEXT *rsp = NULL;
	IMMSV_ATTR_VALUES_LIST *oldRsp, *fetchedRsp;
	IMMND_OM_SEARCH_NODE *sn = NULL;
	SaAisErrorT err = reply->result;
	bool isAccessor = false;
	bool nonExtendedName = false;
	TRACE_ENTER();
	osafassert(reply->requestNodeId == cb->node_id);
	memset(&send_evt, '\0', sizeof(IMMSV_EVT));
	send_evt.type = IMMSV_EVT_TYPE_IMMA;
	SaImmHandleT tmp_hdl = m_IMMSV_PACK_HANDLE(reqConn, cb->node_id);

	/*Look up client-node */
	immnd_client_node_get(cb, tmp_hdl, &cl_node);
	if (cl_node == NULL || cl_node->mIsStale) {
		LOG_WA("IMMND - Client Node Get Failed for cli_hdl:%llx",
		       tmp_hdl);
		/*Cant do anything */
		goto leave;
	}

	TRACE_2("SEARCH NEXT, Look for id:%u", reply->searchId);

	sn = cl_node->searchOpList;
	while (sn) {
		if (sn->searchId == reply->searchId) {
			break;
		}
		sn = sn->next;
	}

	if (sn) {
		/* Don't move to the next iteration in searchOp
		 * if OI resturns ERR_NO_MEMORY or ERR_NO_RESOURCES */
		if (err != SA_AIS_ERR_NO_MEMORY &&
		    err != SA_AIS_ERR_NO_RESOURCES) {
			immModel_popLastResult(sn->searchOp);
		}
		immModel_fetchLastResult(sn->searchOp, &rsp);
		immModel_clearLastResult(sn->searchOp);
		isAccessor = immModel_isSearchOpAccessor(sn->searchOp);
		nonExtendedName =
		    immModel_isSearchOpNonExtendedNameSet(sn->searchOp);
	} else {
		LOG_ER("Could not find search node for search-ID:%u",
		       reply->searchId);
		if (err == SA_AIS_OK) {
			err = SA_AIS_ERR_LIBRARY;
		}
	}

	if (err != SA_AIS_OK) {
		goto agent_rsp;
	}

	if (!rsp) {
		LOG_ER("Part of continuation lost!");
		err = SA_AIS_ERR_LIBRARY; /*not the proper error code.. */
		goto agent_rsp;
	}

	TRACE_2("Last result found, add current runtime values");
	osafassert(strncmp((const char *)rsp->objectName.buf,
			   (const char *)reply->runtimeAttrs.objectName.buf,
			   rsp->objectName.size) == 0);
	/* Iterate through reply->runtimeAttrs.attrValuesList */
	/* Update value in rsp->attrValuesList */

	if (nonExtendedName) {
		IMMSV_ATTR_VALUES *att;
		IMMSV_EDU_ATTR_VAL_LIST *attrList;

		if (reply->runtimeAttrs.objectName.size >=
		    SA_MAX_UNEXTENDED_NAME_LENGTH) {
			err = SA_AIS_ERR_NAME_TOO_LONG;
			goto agent_rsp;
		}

		fetchedRsp = reply->runtimeAttrs.attrValuesList;
		while (fetchedRsp) {
			att = &(fetchedRsp->n);
			if (att->attrValueType == SA_IMM_ATTR_SANAMET &&
			    att->attrValuesNumber > 0) {
				if (att->attrValue.val.x.size >=
				    SA_MAX_UNEXTENDED_NAME_LENGTH) {
					err = SA_AIS_ERR_NAME_TOO_LONG;
					goto agent_rsp;
				}
				if (att->attrValuesNumber > 1) {
					attrList = att->attrMoreValues;
					while (attrList) {
						if (attrList->n.val.x.size >=
						    SA_MAX_UNEXTENDED_NAME_LENGTH) {
							err =
							    SA_AIS_ERR_NAME_TOO_LONG;
							goto agent_rsp;
						}
						attrList = attrList->next;
					}
				}
			}
			fetchedRsp = fetchedRsp->next;
		}
	}

	fetchedRsp = reply->runtimeAttrs.attrValuesList;
	while (fetchedRsp) { // For each fetched rt-attr-value
		IMMSV_ATTR_VALUES *att = &(fetchedRsp->n);
		TRACE_2("Looking for attribute:%s", att->attrName.buf);

		oldRsp = rsp->attrValuesList;
		while (oldRsp) { // Find the old value
			IMMSV_ATTR_VALUES *att2 = &(oldRsp->n);
			/*TRACE_2("Mattching against:%s", att2->attrName.buf);*/
			if (att->attrName.size == att2->attrName.size &&
			    (strncmp((const char *)att->attrName.buf,
				     (const char *)att2->attrName.buf,
				     att->attrName.size) == 0)) {
				TRACE_2("MATCH FOUND nrof old values:%lu",
					att2->attrValuesNumber);
				osafassert(att->attrValueType ==
					   att2->attrValueType);
				/* delete the current old value & set
				 * attrValuesNumber to zero. */
				/* if reply value has attrValuesNumber > 0 then
				 * assign this fetched value. */
				/* Note: dont forget AttrMoreValues ! */
				if (att2->attrValuesNumber) {
					immsv_evt_free_att_val(
					    &att2->attrValue,
					    att2->attrValueType);

					if (att2->attrValuesNumber > 1) {
						IMMSV_EDU_ATTR_VAL_LIST
						    *att2multi =
							att2->attrMoreValues;
						do {
							IMMSV_EDU_ATTR_VAL_LIST
							    *tmp;
							osafassert(
							    att2multi); // must
									// be at
									// least
									// one
									// extra
									// value
							immsv_evt_free_att_val(
							    &att2multi->n,
							    att2->attrValueType);
							tmp = att2multi;
							att2multi =
							    att2multi->next;
							tmp->next = NULL;
							free(tmp);
						} while (att2multi);
						att2->attrValuesNumber = 0;
						att2->attrMoreValues = NULL;
					} // multis deleted

				} // old value in rsp deleted.

				TRACE_2(
				    "OLD VALUES DELETED nrof new values:%lu",
				    att->attrValuesNumber);

				if (att->attrValuesNumber) { // there is a new
							     // value, copy it.
					att2->attrValue =
					    att->attrValue; // steals any
							    // buffer.
					att2->attrValuesNumber =
					    att->attrValuesNumber;
					att->attrValue.val.x.buf =
					    NULL; // ensure stolen buffer is not
						  // freed twice.
					att->attrValue.val.x.size = 0;
				}

				if (att->attrValuesNumber >
				    1) { // multivalued attribute
					att2->attrMoreValues =
					    att->attrMoreValues; // steal the
								 // whole list
					att->attrMoreValues = NULL;
				}
				att->attrValuesNumber = 0;

				break; // out of inner loop, since we already
				       // matched this fetched value
			}
			oldRsp = oldRsp->next;
		}
		fetchedRsp = fetchedRsp->next;
	}

agent_rsp:
	if (err == SA_AIS_OK) {
		send_evt.info.imma.type = isAccessor
					      ? IMMA_EVT_ND2A_ACCESSOR_GET_RSP
					      : IMMA_EVT_ND2A_SEARCHNEXT_RSP;
		send_evt.info.imma.info.searchNextRsp = rsp;
	} else { /*err != SA_AIS_OK */
		send_evt.info.imma.type = IMMA_EVT_ND2A_IMM_ERROR;
		send_evt.info.imma.info.errRsp.error = err;
	}

	if (immnd_mds_send_rsp(cb, &(cl_node->tmpSinfo), &send_evt) !=
	    NCSCC_RC_SUCCESS) {
		LOG_WA(
		    "Could not send reply to agent for search-next continuaton");
	}

	if (isAccessor || /* Accessor case */
	    (err != SA_AIS_OK && err != SA_AIS_ERR_NO_MEMORY &&
	     err != SA_AIS_ERR_NO_RESOURCES)) { /* Search case */
		if (!isAccessor) {
			/* Finalize search node when it's not ERR_NO_MEMORY or
			 * ERR_NO_RESOURCES */
			TRACE("Finalizing search node, err = %u", err);
		}
		if (sn && cl_node) {
			IMMND_OM_SEARCH_NODE **prev = &(cl_node->searchOpList);
			IMMND_OM_SEARCH_NODE *n = cl_node->searchOpList;
			while (n) {
				if (n == sn) {
					*prev = n->next;
					break;
				}
				prev = &(n->next);
				n = n->next;
			}
		}
		if (sn) {
			if (sn->searchOp)
				immModel_deleteSearchOp(sn->searchOp);
			free(sn);
		}
	}

	if (rsp) {
		freeSearchNext(rsp, true);
	}

leave:
	TRACE_LEAVE();
}

/****************************************************************************
 * Name          : immnd_evt_proc_oi_att_pull_rpl
 *
 * Description   : Function to process the reply from the invocation
 *                 of the SaImmOiRtAttrUpdateCallbackT.
 *                 Note that this is a call local to the ND (does not arrive
 *                 over FEVS).
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMND_EVT *evt - Received Event structure
 *                 IMMSV_SEND_INFO *sinfo - sender info
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 * Notes         : None.
 *****************************************************************************/
static uint32_t immnd_evt_proc_oi_att_pull_rpl(IMMND_CB *cb, IMMND_EVT *evt,
					       IMMSV_SEND_INFO *sinfo)
{
	IMMSV_EVT send_evt;
	SaAisErrorT err;
	uint32_t rc = NCSCC_RC_SUCCESS;
	bool isLocal;
	void *searchOp = NULL;
	TRACE_ENTER();
	memset(&send_evt, '\0', sizeof(IMMSV_EVT));
	IMMSV_OM_RSP_SEARCH_REMOTE *rspo =
	    &send_evt.info.immnd.info.rspSrchRmte;
	rspo->remoteNodeId = evt->info.rtAttUpdRpl.sr.remoteNodeId;
	rspo->searchId = evt->info.rtAttUpdRpl.sr.searchId;
	rspo->requestNodeId = evt->info.rtAttUpdRpl.sr.requestNodeId;
	rspo->runtimeAttrs.objectName =
	    evt->info.rtAttUpdRpl.sr.objectName; /* borrowing buf */
	err = evt->info.rtAttUpdRpl.result;

	osafassert(evt->info.rtAttUpdRpl.sr.remoteNodeId == cb->node_id);
	isLocal = evt->info.rtAttUpdRpl.sr.requestNodeId == cb->node_id;
	/* Note stack allocated search-initialize struct with members */
	/* shallow copied to refer into the search-remote struct. */
	/* No heap allocations! Dont explicitly delete anything of this struct
	 */
	/* ImmsvOmSearchInit reqo; */
	if (err == SA_AIS_OK) {
		/* Fetch latest runtime values from the object. Use
		 * search/accessor. */
		IMMSV_OM_SEARCH_INIT reqo;
		memset(&reqo, '\0', sizeof(IMMSV_OM_SEARCH_INIT));
		reqo.rootName.buf =
		    evt->info.rtAttUpdRpl.sr.objectName.buf; /*borrowing */
		reqo.rootName.size = evt->info.rtAttUpdRpl.sr.objectName.size;
		reqo.scope = SA_IMM_ONE;
		reqo.searchOptions =
		    SA_IMM_SEARCH_ONE_ATTR | SA_IMM_SEARCH_GET_SOME_ATTR;
		reqo.searchParam.present = ImmOmSearchParameter_PR_NOTHING;
		reqo.attributeNames =
		    evt->info.rtAttUpdRpl.sr.attributeNames; /*borrowing. */
		TRACE_2(
		    "Fetching the folowing rt attributes after implementer has updated");
		while (reqo.attributeNames) {
			TRACE_2("attrname: %s", reqo.attributeNames->name.buf);
			reqo.attributeNames = reqo.attributeNames->next;
		}
		reqo.attributeNames =
		    evt->info.rtAttUpdRpl.sr.attributeNames; /*borrowing. */

		TRACE_2("oi_att_pull_rpl Before searchInit");
		err = immModel_searchInitialize(cb, &reqo, &searchOp, false,
						true);
		if (err == SA_AIS_OK) {
			TRACE_2(
			    "oi_att_pull_rpl searchInit returned OK, calling searchNext");
			IMMSV_OM_RSP_SEARCH_NEXT *rsp = 0;
			err =
			    immModel_nextResult(cb, searchOp, &rsp, NULL, NULL,
						NULL, NULL, false, NULL);
			if (err == SA_AIS_OK) {
				rspo->runtimeAttrs.attrValuesList =
				    rsp->attrValuesList;
				/*STEALING*/ rsp->attrValuesList = NULL;

				immModel_clearLastResult(searchOp);
				freeSearchNext(rsp, true);
			} else {
				LOG_WA(
				    "Internal IMM server problem - failure from internal nextResult: %u",
				    err);
				err = SA_AIS_ERR_NO_RESOURCES;
			}
		} else {
			LOG_ER(
			    "Internal IMM server problem - failure from internal searchInit: %u",
			    err);
			err = SA_AIS_ERR_NO_RESOURCES;
		}
	}
	if (err == SA_AIS_ERR_FAILED_OPERATION) {
		/* We can get FAILED_OPERATION here as response from
		   implementer, which is not an allowed return code towards
		   client/agent in the searchNext.. So what to convert it to ?
		   We will go for NO RESOURCES in the lack of anything better.
		 */
		err = SA_AIS_ERR_NO_RESOURCES;
	}

	rspo->result = err;

	TRACE_2("oi_att_pull_rpl searchNext OK");
	/* We have the updated values fetched in "rsp", or an error indication.
	 */

	if (isLocal) {
		TRACE_2("Originating client is local");
		/* Case (B) Fetch request continuation locally. */
		SaUint32T reqConn;
		SaInvocationT invoc =
		    m_IMMSV_PACK_HANDLE(evt->info.rtAttUpdRpl.sr.remoteNodeId,
					evt->info.rtAttUpdRpl.sr.searchId);

		immModel_fetchSearchReqContinuation(cb, invoc, &reqConn);
		TRACE_2("FETCHED SEARCH REQ CONTINUATION FOR %u|%u->%u",
			(SaUint32T)m_IMMSV_UNPACK_HANDLE_LOW(invoc),
			(SaUint32T)m_IMMSV_UNPACK_HANDLE_HIGH(invoc), reqConn);

		if (!reqConn) {
			LOG_WA(
			    "Failed to retrieve search continuation, client died ?");
			/* Cant do anything but just drop it. */
			goto deleteSearchOp;
		}
		TRACE_2("Build local reply to agent");
		search_req_continue(cb, rspo, reqConn);
	} else {
		TRACE_2("Originating client is remote");
		/* Case (C) Fetch implementer continuation to get reply_dest. */
		MDS_DEST reply_dest;
		immModel_fetchSearchImplContinuation(
		    cb, rspo->searchId, rspo->requestNodeId, &reply_dest);
		if (!reply_dest) {
			LOG_WA(
			    "Continuation for remote rt attribute fetch was lost, can not complete fetch");
			/* Just drop it. The search should have been, or will
			 * be, aborted by a time-out. */
			goto deleteSearchOp;
		} else {
			TRACE_2("Send the fetched result back to request node");
			send_evt.type = IMMSV_EVT_TYPE_IMMND;
			send_evt.info.immnd.type =
			    IMMND_EVT_ND2ND_SEARCH_REMOTE_RSP;

			rc = immnd_mds_msg_send(cb, NCSMDS_SVC_ID_IMMND,
						reply_dest, &send_evt);
			if (rc != NCSCC_RC_SUCCESS) {
				LOG_ER(
				    "Problem in replying to peer IMMND over MDS. "
				    "Abandoning searchNext fetch remote rt attrs.");
				goto deleteSearchOp;
			}
		}
	}

deleteSearchOp:
	if (searchOp) {
		/* immModel_nextResult(cb, searchOp, &rsp, NULL, NULL, NULL,
		 * NULL, false); */
		immModel_deleteSearchOp(searchOp);
	}

	if (rspo->runtimeAttrs.attrValuesList) {
		rspo->runtimeAttrs.objectName.buf = NULL; /* was borrowed. */
		rspo->runtimeAttrs.objectName.size = 0;
		freeSearchNext(&rspo->runtimeAttrs, false);
	}
	TRACE_2("oi_att_pull_rpl finalize of internal search OK");
	TRACE_LEAVE();
	return rc;
}

void freeSearchNext(IMMSV_OM_RSP_SEARCH_NEXT *rsp, bool freeTop)
{
	TRACE_ENTER();

	TRACE_2("objectName:%s", rsp->objectName.buf);
	free(rsp->objectName.buf);
	rsp->objectName.buf = NULL;

	IMMSV_ATTR_VALUES_LIST *al = rsp->attrValuesList;
	while (al) {
		free(al->n.attrName.buf);
		al->n.attrName.buf = NULL;
		al->n.attrName.size = 0;
		if (al->n.attrValuesNumber) {
			immsv_evt_free_att_val(&al->n.attrValue,
					       al->n.attrValueType);
			if (al->n.attrValuesNumber > 1) {
				IMMSV_EDU_ATTR_VAL_LIST *att2multi =
				    al->n.attrMoreValues;
				do {
					IMMSV_EDU_ATTR_VAL_LIST *tmp;
					osafassert(att2multi); /* must be at
								  least one
								  extra value */
					immsv_evt_free_att_val(
					    &att2multi->n, al->n.attrValueType);
					tmp = att2multi;
					att2multi = att2multi->next;
					tmp->next = NULL;
					free(tmp);
				} while (att2multi);
				al->n.attrValuesNumber = 0;
				al->n.attrMoreValues = NULL;
			} // multis deleted
		}
		IMMSV_ATTR_VALUES_LIST *tmp2 = al;
		al = al->next;
		tmp2->next = NULL;
		free(tmp2);
	}
	rsp->attrValuesList = NULL;
	if (freeTop) { /*Top object was also heap allocated. */
		free(rsp);
	}
	TRACE_LEAVE();
}

static uint32_t search_result_size(IMMSV_OM_RSP_SEARCH_NEXT *rsp)
{
	uint32_t size = strnlen(rsp->objectName.buf, rsp->objectName.size);
	IMMSV_ATTR_VALUES_LIST *attrList = rsp->attrValuesList;

	while (attrList) {
		size += strnlen(attrList->n.attrName.buf,
				attrList->n.attrName.size);
		size += 4; /* type */
		switch (attrList->n.attrValueType) {
		case SA_IMM_ATTR_SAINT32T:
		case SA_IMM_ATTR_SAUINT32T:
			size += 4 * attrList->n.attrValuesNumber;
			break;
		case SA_IMM_ATTR_SAINT64T:
		case SA_IMM_ATTR_SAUINT64T:
		case SA_IMM_ATTR_SATIMET:
			size += 8 * attrList->n.attrValuesNumber;
			break;
		case SA_IMM_ATTR_SAFLOATT:
			size += sizeof(SaFloatT) * attrList->n.attrValuesNumber;
			break;
		case SA_IMM_ATTR_SADOUBLET:
			size +=
			    sizeof(SaDoubleT) * attrList->n.attrValuesNumber;
			break;

		case SA_IMM_ATTR_SANAMET:
		case SA_IMM_ATTR_SASTRINGT:
		case SA_IMM_ATTR_SAANYT:
			size += attrList->n.attrValue.val.x.size *
				attrList->n.attrValuesNumber;
			break;

		default:
			TRACE_1("Incorrect value for SaImmValueTypeT: %ld. ",
				attrList->n.attrValueType);
			osafassert(0);
			break;
		}

		attrList = attrList->next;
	}

	return size;
}

/****************************************************************************
 * Name          : immnd_evt_proc_search_next
 *
 * Description   : Function to process the SaImmOmSearchNext call.
 *                 Note that this is a read, local to the ND (does not go
 *                 over FEVS).
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMND_EVT *evt - Received Event structure
 *                 IMMSV_SEND_INFO *sinfo - sender info
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 * Notes         : None.
 *****************************************************************************/
static uint32_t immnd_evt_proc_search_next(IMMND_CB *cb, IMMND_EVT *evt,
					   IMMSV_SEND_INFO *sinfo)
{
	IMMSV_EVT send_evt;
	uint32_t rc = NCSCC_RC_SUCCESS;
	SaAisErrorT error;
	IMMND_IMM_CLIENT_NODE *cl_node = NULL;
	IMMND_IMM_CLIENT_NODE *oi_cl_node = NULL;
	IMMND_OM_SEARCH_NODE *sn = NULL;
	SaImmOiHandleT implHandle = 0LL;
	IMMSV_ATTR_NAME_LIST *rtAttrsToFetch = NULL;
	SaUint32T implConn = 0;
	SaUint32T implNodeId = 0;
	IMMSV_OM_RSP_SEARCH_NEXT *rsp = NULL;
	IMMSV_OM_RSP_SEARCH_NEXT *rsp1 = NULL;
	IMMSV_OM_RSP_SEARCH_NEXT **rspList = NULL;
	MDS_DEST implDest = 0LL;
	bool retardSync =
	    ((cb->fevs_replies_pending >= IMMSV_DEFAULT_FEVS_MAX_PENDING) &&
	     cb->mIsCoord && (cb->syncPid > 0));
	SaUint32T resultSize = 0;
	IMMSV_OM_RSP_SEARCH_BUNDLE_NEXT bundleSearch = {0, NULL};
	int ix;
	bool isAccessor = false;
	SaUint32T oiTimeout = 0;

	TRACE_ENTER();

	memset(&send_evt, '\0', sizeof(IMMSV_EVT));

	/*Look up client-node */
	immnd_client_node_get(cb, evt->info.searchOp.client_hdl, &cl_node);
	if (cl_node == NULL || cl_node->mIsStale) {
		LOG_WA("IMMND - Client Node Get Failed for cli_hdl:%llu",
		       evt->info.searchOp.client_hdl);
		error = SA_AIS_ERR_BAD_HANDLE;
		goto agent_rsp;
	}
	TRACE_2("SEARCH NEXT, Look for id:%u", evt->info.searchOp.searchId);

	sn = cl_node->searchOpList;
	while (sn) {
		if (sn->searchId == evt->info.searchOp.searchId) {
			break;
		}
		sn = sn->next;
	}

	if (!sn) {
		LOG_ER("Could not find search node for search-ID:%u",
		       evt->info.searchOp.searchId);
		error = SA_AIS_ERR_BAD_HANDLE;
		goto agent_rsp;
	}

	error = immnd_mds_client_not_busy(&(cl_node->tmpSinfo));
	if (error != SA_AIS_OK) {
		if (error == SA_AIS_ERR_BAD_HANDLE) {
			/* The connection appears to be hung indefinitely
			   waiting for a reply on a previous syncronous call.
			   Discard the connection and return BAD_HANDLE to allow
			   client to recover and make progress.
			 */
			immnd_proc_imma_discard_connection(cb, cl_node, false);
			rc = immnd_client_node_del(cb, cl_node);
			osafassert(rc == NCSCC_RC_SUCCESS);
			free(cl_node);
		}
		goto agent_rsp;
	}

	error = immModel_nextResult(cb, sn->searchOp, &rsp, &implConn,
				    &implNodeId, &rtAttrsToFetch, &implDest,
				    retardSync, &oiTimeout);
	if (error != SA_AIS_OK) {
		goto agent_rsp;
	}

	/*Three cases:
	   A) No runtime attributes to fetch.
	   Reply directly with this call.
	   B) Runtime attributes to fetch and the implementer is local.
	   => (store continuation?) and call on implementer.
	   C) Runtime attributes to fetch, but the implementer is remote
	   => store continuation and use request remote invoc over
	   either FEVS or direct adressing to peer ND.
	 */

	if (implNodeId) { /* Case B or C */
		TRACE_2("There is an implementer and there are pure rt's !");
		osafassert(rtAttrsToFetch);
		/*rsp kept in ImmSearchOp to be used in continuation.
		   rtAttrsToFetch is expected to be deleted by this layer after
		   used.

		   rsp needs to be appended by the replies.
		   ((attrsToFetch needs to be checked/matched when replies
		   received.)) If the (non cached runtime) attribute is ALSO
		   persistent, then the value should be stored (exactly as
		   cached). The only difference with respect to officially
		   cached is how the attribute is fetched.
		 */

		if (implConn) {
			TRACE_2("The implementer is local- case B");
			/*Case B Invoke rtUpdateCallback directly. */
			osafassert(implNodeId == cb->node_id);
			send_evt.type = IMMSV_EVT_TYPE_IMMA;
			send_evt.info.imma.type = IMMA_EVT_ND2A_SEARCH_REMOTE;
			IMMSV_OM_SEARCH_REMOTE *rreq =
			    &send_evt.info.imma.info.searchRemote;
			rreq->remoteNodeId = implNodeId;
			rreq->searchId = sn->searchId;
			rreq->requestNodeId =
			    cb->node_id; /* NOTE: obsolete ? */
			rreq->objectName =
			    rsp->objectName; /* borrow objectName.buf */
			rreq->attributeNames =
			    rtAttrsToFetch; /* borrow this structure */

			implHandle = m_IMMSV_PACK_HANDLE(implConn, implNodeId);
			/*Fetch client node for OI ! */
			immnd_client_node_get(cb, implHandle, &oi_cl_node);
			if (oi_cl_node == NULL || oi_cl_node->mIsStale) {
				LOG_WA(
				    "ERR_NO_RESOURCES: SearchNext: Implementer died during fetch of pure RTA");
				error = SA_AIS_ERR_NO_RESOURCES;
				goto agent_rsp;
			} else {
				rreq->client_hdl = implHandle;
				TRACE_2(
				    "MAKING OI-IMPLEMENTER rtUpdate upcall towards agent");
				if (immnd_mds_msg_send(
					cb, NCSMDS_SVC_ID_IMMA_OI,
					oi_cl_node->agent_mds_dest,
					&send_evt) != NCSCC_RC_SUCCESS) {
					LOG_WA(
					    "ERR_NO_RESOURCES: SearchNext - Agent upcall over MDS for rtUpdate failed");
					error = SA_AIS_ERR_NO_RESOURCES;
					goto agent_rsp;
				}
			}
		} else {
			TRACE_2("The implementer is remote - case C");
			/*Case C Send the message directly to nd where
			 * implementer resides. */
			send_evt.type = IMMSV_EVT_TYPE_IMMND;
			send_evt.info.immnd.type =
			    IMMND_EVT_ND2ND_SEARCH_REMOTE;
			IMMSV_OM_SEARCH_REMOTE *rreq =
			    &send_evt.info.immnd.info.searchRemote;
			rreq->remoteNodeId = implNodeId;
			rreq->searchId = sn->searchId;
			rreq->requestNodeId =
			    cb->node_id; /* NOTE: obsolete ? */
			rreq->objectName = rsp->objectName; /* borrowed buf; */
			rreq->attributeNames =
			    rtAttrsToFetch; /* borrowed attrList */

			TRACE_2("FORWARDING TO OTHER ND!");

			rc = immnd_mds_msg_send(cb, NCSMDS_SVC_ID_IMMND,
						implDest, &send_evt);
			if (rc != NCSCC_RC_SUCCESS) {
				LOG_ER(
				    "ERR_NO_RESOURCES: SearchNext - Problem in sending to peer IMMND over MDS. Aborting searchNext.");
				error = SA_AIS_ERR_NO_RESOURCES;
				goto agent_rsp;
			}
		}

		/*Register request continuation ! */
		SaInvocationT invoc =
		    m_IMMSV_PACK_HANDLE(implNodeId, sn->searchId);
		SaUint32T clientId =
		    m_IMMSV_UNPACK_HANDLE_HIGH(evt->info.searchOp.client_hdl);

		TRACE_2("SETTING SEARCH REQ CONTINUATION FOR %u|%x->%u",
			sn->searchId, implNodeId, clientId);

		immModel_setSearchReqContinuation(cb, invoc, clientId,
						  oiTimeout);

		cl_node->tmpSinfo =
		    *sinfo; // TODO should be part of continuation?

		osafassert(error == SA_AIS_OK);
		if (rtAttrsToFetch) {
			immsv_evt_free_attrNames(rtAttrsToFetch);
		}
		TRACE_LEAVE();
		return NCSCC_RC_SUCCESS;
	} else if (!rtAttrsToFetch) { /*Case A */
		IMMSV_ATTR_NAME_LIST *rtAttrs;
		SaAisErrorT err;
		bool bRtAttrs;
		uint32_t size = search_result_size(rsp);
		resultSize = 1;
		isAccessor = immModel_isSearchOpAccessor(sn->searchOp);

		/* Repeat to the maximum search result,
		 * or the size os search results becomes bigger then
		 * IMMND_SEARCH_BUNDLE_SIZE, or till the object with at least
		 * one pure runtime attribute */
		while (resultSize < IMMND_MAX_SEARCH_RESULT &&
		       size < IMMND_SEARCH_BUNDLE_SIZE) {
			err = immModel_testTopResult(sn->searchOp, &implNodeId,
						     &bRtAttrs);
			if ((err != SA_AIS_OK) || bRtAttrs)
				break;

			err = immModel_nextResult(
			    cb, sn->searchOp, &rsp1, &implConn, &implNodeId,
			    &rtAttrs, &implDest, retardSync, NULL);
			if (err != SA_AIS_OK) {
				osafassert(err == SA_AIS_ERR_NOT_EXIST);
				break;
			}

			if (resultSize == 1) {
				rspList = (IMMSV_OM_RSP_SEARCH_NEXT **)calloc(
				    IMMND_MAX_SEARCH_RESULT,
				    sizeof(IMMSV_OM_RSP_SEARCH_NEXT *));
				rspList[0] = rsp;
				rsp = NULL;
			}

			rspList[resultSize++] = rsp1;

			size += search_result_size(rsp1);
		}

		immModel_clearLastResult(sn->searchOp);
	}

agent_rsp:

	send_evt.type = IMMSV_EVT_TYPE_IMMA;

	if (error == SA_AIS_OK) {
		if (resultSize == 1) {
			send_evt.info.imma.type =
			    isAccessor ? IMMA_EVT_ND2A_ACCESSOR_GET_RSP
				       : IMMA_EVT_ND2A_SEARCHNEXT_RSP;
			send_evt.info.imma.info.searchNextRsp = rsp;
		} else {
			osafassert(rspList);
			bundleSearch.resultSize = resultSize;
			bundleSearch.searchResult =
			    (IMMSV_OM_RSP_SEARCH_NEXT **)malloc(
				sizeof(IMMSV_OM_RSP_SEARCH_NEXT *) *
				resultSize);
			for (ix = 0; ix < resultSize; ix++)
				bundleSearch.searchResult[ix] = rspList[ix];

			send_evt.info.imma.type =
			    IMMA_EVT_ND2A_SEARCHBUNDLENEXT_RSP;
			send_evt.info.imma.info.searchBundleNextRsp =
			    &bundleSearch;
		}
	} else {
		send_evt.info.imma.type = IMMA_EVT_ND2A_IMM_ERROR;
		send_evt.info.imma.info.errRsp.error = error;
		send_evt.info.imma.info.errRsp.errStrings = NULL;
	}

	rc = immnd_mds_send_rsp(cb, sinfo, &send_evt);

	if (rsp) {
		freeSearchNext(rsp, true);
	}

	if (rspList) {
		for (ix = 0; ix < IMMND_MAX_SEARCH_RESULT; ix++) {
			if (rspList[ix]) {
				freeSearchNext(rspList[ix], true);
			}
		}
		free(rspList);

		immModel_clearLastResult(sn->searchOp);
	}

	if (bundleSearch.searchResult) {
		free(bundleSearch.searchResult);
	}

	if (rtAttrsToFetch) {
		immsv_evt_free_attrNames(rtAttrsToFetch);
	}

	if (error == SA_AIS_ERR_NOT_EXIST || isAccessor) {
		if (immnd_evt_proc_search_finalize(cb, evt, NULL) !=
		    NCSCC_RC_SUCCESS) {
			LOG_WA(
			    "Failed in finalizing consumed iterator/accessor");
		}
	}

	TRACE_LEAVE();
	return rc;
}

/****************************************************************************
 * Name          : immnd_evt_proc_search_finalize
 *
 * Description   : Function to process the SaImmOmSearchFinalize call.
 *                 Note that this is a read, local to the ND (does not go
 *                 over FEVS).
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMND_EVT *evt - Received Event structure
 *                 IMMSV_SEND_INFO *sinfo - sender info
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 * Notes         : None.
 *****************************************************************************/
static uint32_t immnd_evt_proc_search_finalize(IMMND_CB *cb, IMMND_EVT *evt,
					       IMMSV_SEND_INFO *sinfo)
{
	IMMSV_EVT send_evt;
	uint32_t rc = NCSCC_RC_SUCCESS;
	IMMND_IMM_CLIENT_NODE *cl_node = NULL;
	IMMND_OM_SEARCH_NODE *sn = NULL;
	IMMND_OM_SEARCH_NODE **prev = NULL;
	IMMSV_OM_RSP_SEARCH_NEXT *rsp = NULL;
	TRACE_ENTER();

	memset(&send_evt, '\0', sizeof(IMMSV_EVT));
	send_evt.type = IMMSV_EVT_TYPE_IMMA;
	send_evt.info.imma.type = IMMA_EVT_ND2A_IMM_ERROR;

	/*Look up client-node */
	immnd_client_node_get(cb, evt->info.searchOp.client_hdl, &cl_node);
	if (cl_node == NULL || cl_node->mIsStale) {
		LOG_WA("IMMND - %llu Client Died",
		       evt->info.searchOp.client_hdl);
		send_evt.info.imma.info.errRsp.error = SA_AIS_ERR_BAD_HANDLE;
		goto agent_rsp;
	}
	TRACE_2("SEARCH FINALIZE, Look for id:%u", evt->info.searchOp.searchId);

	sn = cl_node->searchOpList;
	prev = &(cl_node->searchOpList);
	while (sn) {
		if (sn->searchId == evt->info.searchOp.searchId) {
			*prev = sn->next;
			break;
		}
		prev = &(sn->next);
		sn = sn->next;
	}

	if (!sn) {
		TRACE("Search node for search-ID:%u already finalized",
		      evt->info.searchOp.searchId);
		send_evt.info.imma.info.errRsp.error = SA_AIS_OK;
		goto agent_rsp;
	}

	immModel_fetchLastResult(sn->searchOp, &rsp);
	immModel_clearLastResult(sn->searchOp);
	if (rsp) {
		freeSearchNext(rsp, true);
	}

	immModel_deleteSearchOp(sn->searchOp);
	sn->searchOp = NULL;
	sn->next = NULL;
	free(sn);
	send_evt.info.imma.info.errRsp.error = SA_AIS_OK;
agent_rsp:

	if (sinfo) {
		rc = immnd_mds_send_rsp(cb, sinfo, &send_evt);
	}
	TRACE_LEAVE();
	return rc;
}

/****************************************************************************
 * Name          : immnd_evt_proc_safe_read
 *
 * Description   : Function to process the saImmOmCcbObjectRead call.
 *                 This call can in some cases be resolved with success locally.
 *                 This will be the case if the object to be accessed is already
 *                 locked previously by this CCB.
 *
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMND_EVT *evt - Received Event structure
 *                 IMMSV_SEND_INFO *sinfo - sender info
 *****************************************************************************/
static uint32_t immnd_evt_proc_safe_read(IMMND_CB *cb, IMMND_EVT *evt,
					 IMMSV_SEND_INFO *sinfo)
{
	IMMSV_EVT send_evt;
	uint32_t rc = NCSCC_RC_SUCCESS;
	SaAisErrorT err = SA_AIS_OK;
	TRACE_ENTER();
	memset(&send_evt, 0, sizeof(IMMSV_EVT));

	if (evt->info.searchInit.ccbId == 0) {
		err = SA_AIS_ERR_LIBRARY;
		LOG_WA("ERR_LIBRARY: Received zero ccb-id for safe-read");
		goto error;
	}

	/* Dive into ImmModel to do locking checks.
	   If already locked in this ccb continue with accessor.
	   If locked by other ccb then reject with ERR_BUSY
	   If not locked, then generate fevs message for locking AND read.
	 */

	err = immModel_objectIsLockedByCcb(cb, &(evt->info.searchInit));

	switch (err) {
	case SA_AIS_OK:
		TRACE(
		    "Safe read: Object is locked by this CCB(%u). Go ahead and safe-read",
		    evt->info.searchInit.ccbId);
		/* Invoke accessor_get which will send the reply. */
		break;

	case SA_AIS_ERR_BUSY:
		TRACE(
		    "Object is locked by some other CCB than this CCB(%u). Reply with BUSY",
		    evt->info.searchInit.ccbId);
		goto error;

	case SA_AIS_ERR_INTERRUPT:
		TRACE(
		    "Object not locked. Ccb (%u) needs to lock it over fevs. Reply with INTERRUPT",
		    evt->info.searchInit.ccbId);
		/* Should result in fevs message to read-lock object. */
		goto error;

	default:
		TRACE("Unusual error from immModel_objectIsLockedByCcb: %u",
		      err);
		goto error;
	}

	rc = immnd_evt_proc_accessor_get(cb, evt, sinfo);
	goto done;

error:
	send_evt.type = IMMSV_EVT_TYPE_IMMA;
	send_evt.info.imma.type = IMMA_EVT_ND2A_IMM_ERROR;
	send_evt.info.imma.info.errRsp.error = err;
	send_evt.info.imma.info.errRsp.errStrings = NULL;

	rc = immnd_mds_send_rsp(cb, sinfo, &send_evt);

done:
	TRACE_LEAVE();
	return rc;
}

/****************************************************************************
 * Name          : immnd_evt_proc_accessor_get
 *
 * Description   : Function to process the saImmOmAccessorGet call.
 *                 Note that this is a read, local to the ND (does not go
 *                 over FEVS).
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMND_EVT *evt - Received Event structure
 *                 IMMSV_SEND_INFO *sinfo - sender info
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 * Notes         : None.
 *****************************************************************************/
static uint32_t immnd_evt_proc_accessor_get(IMMND_CB *cb, IMMND_EVT *evt,
					    IMMSV_SEND_INFO *sinfo)
{
	IMMSV_EVT send_evt;
	uint32_t rc = NCSCC_RC_SUCCESS;
	SaAisErrorT error = SA_AIS_OK;
	void *searchOp = NULL;
	IMMND_IMM_CLIENT_NODE *cl_node = NULL;
	IMMND_OM_SEARCH_NODE *sn = NULL;
	TRACE_ENTER();

	/* Search Init */

	memset(&send_evt, 0, sizeof(IMMSV_EVT));

	if (evt->info.searchInit.ccbId != 0) {
		TRACE_2("SAFE READ :%s CcbId: %u",
			evt->info.searchInit.rootName.buf,
			evt->info.searchInit.ccbId);
	} else {
		TRACE_2("ACCESSOR GET:%s", evt->info.searchInit.rootName.buf);
	}

	/*Look up client-node */
	immnd_client_node_get(cb, evt->info.searchInit.client_hdl, &cl_node);
	if (cl_node == NULL || cl_node->mIsStale) {
		LOG_WA("IMMND - Client Node Get Failed for cli_hdl");
		TRACE_2("Client Node get failed for handle:%llu",
			evt->info.searchInit.client_hdl);
		error = SA_AIS_ERR_BAD_HANDLE;
		goto search_init_err;
	}

	error = immModel_searchInitialize(cb, &(evt->info.searchInit),
					  &searchOp, false, true);

	if (error != SA_AIS_OK) {
		goto search_init_err;
	}

	/*Generate search-id */
	sn = calloc(1, sizeof(IMMND_OM_SEARCH_NODE));
	if (sn == NULL) {
		error = SA_AIS_ERR_NO_MEMORY;
		goto search_init_err;
	}

	sn->searchId = cb->cli_id_gen++;
	if (cb->cli_id_gen == 0xffffffff) { /* handle wrap arround */
		cb->cli_id_gen = 1;
	}
	sn->searchOp = searchOp;
	sn->next = cl_node->searchOpList;
	cl_node->searchOpList = sn;

	send_evt.info.immnd.type = IMMND_EVT_A2ND_SEARCHNEXT;
	send_evt.info.immnd.info.searchOp.searchId = sn->searchId;
	send_evt.info.immnd.info.searchOp.client_hdl =
	    evt->info.searchInit.client_hdl;

	/*  Search Next handles the rest, sends reply, error or not. */
	rc = immnd_evt_proc_search_next(cb, &(send_evt.info.immnd), sinfo);
	goto done;

search_init_err:
	osafassert(error != SA_AIS_OK);

	send_evt.type = IMMSV_EVT_TYPE_IMMA;
	send_evt.info.imma.type = IMMA_EVT_ND2A_IMM_ERROR;
	send_evt.info.imma.info.errRsp.error = error;
	send_evt.info.imma.info.errRsp.errStrings = NULL;

	rc = immnd_mds_send_rsp(cb, sinfo, &send_evt);

	if (searchOp) {
		immModel_deleteSearchOp(searchOp);
	}

done:
	TRACE_LEAVE();
	return rc;
}

/****************************************************************************
 * Name          : immnd_evt_proc_class_desc_get
 *
 * Description   : Function to process the SaImmOmClassDescriptionGet call.
 *                 Note that this is a read, local to the ND (does not go
 *                 over FEVS).
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMND_EVT *evt - Received Event structure
 *                 IMMSV_SEND_INFO *sinfo - sender info
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 * Notes         : None.
 *****************************************************************************/
static uint32_t immnd_evt_proc_class_desc_get(IMMND_CB *cb, IMMND_EVT *evt,
					      IMMSV_SEND_INFO *sinfo)
{
	IMMSV_EVT send_evt;
	uint32_t rc = NCSCC_RC_SUCCESS;
	SaAisErrorT error;

	memset(&send_evt, '\0', sizeof(IMMSV_EVT));

	TRACE_2("className:%s", evt->info.classDescr.className.buf);
	send_evt.type = IMMSV_EVT_TYPE_IMMA;

	error =
	    immModel_classDescriptionGet(cb, &(evt->info.classDescr.className),
					 &(send_evt.info.imma.info.classDescr));
	if (error == SA_AIS_OK) {
		send_evt.info.imma.type = IMMA_EVT_ND2A_CLASS_DESCR_GET_RSP;
	} else {
		send_evt.info.imma.type = IMMA_EVT_ND2A_IMM_ERROR;
		send_evt.info.imma.info.errRsp.error = error;
	}

	rc = immnd_mds_send_rsp(cb, sinfo, &send_evt);
	/*Deallocate the extras on the send_message */
	IMMSV_ATTR_DEF_LIST *p =
	    send_evt.info.imma.info.classDescr.attrDefinitions;
	while (p) {
		IMMSV_ATTR_DEF_LIST *prev = p;
		if (p->d.attrName.size) {
			free(p->d.attrName.buf); /*Free-Y */
			p->d.attrName.buf = NULL;
		}
		if (p->d.attrDefaultValue) {
			if ((p->d.attrValueType == SA_IMM_ATTR_SASTRINGT) ||
			    (p->d.attrValueType == SA_IMM_ATTR_SAANYT) ||
			    (p->d.attrValueType == SA_IMM_ATTR_SANAMET)) {
				if (p->d.attrDefaultValue->val.x
					.size) { /*Free-ZZ */
					free(p->d.attrDefaultValue->val.x.buf);
					p->d.attrDefaultValue->val.x.buf = NULL;
					p->d.attrDefaultValue->val.x.size = 0;
				}
			}
			free(p->d.attrDefaultValue); /*Free-Z */
			p->d.attrDefaultValue = NULL;
		}
		p = p->next;
		free(prev); /*Free-X */
	}
	send_evt.info.imma.info.classDescr.attrDefinitions = NULL;
	return rc;
}

/****************************************************************************
 * Name          : immnd_evt_proc_imm_finalize
 *
 * Description   : Function to process the SaImmOmFinalize and SaImmOiFinalize
 *                 from IMM Agents.
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *                 bool isOm - true=> OM finalize, false => OI finalize.
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 * Notes         : None.
 *****************************************************************************/
static uint32_t immnd_evt_proc_imm_finalize(IMMND_CB *cb, IMMND_EVT *evt,
					    IMMSV_SEND_INFO *sinfo, bool isOm)
{
	/* Note: parameter isOm is ignored, should be noted in cl_node */
	IMMSV_EVT send_evt;
	uint32_t rc = NCSCC_RC_SUCCESS;
	IMMND_IMM_CLIENT_NODE *cl_node = NULL;

	memset(&send_evt, '\0', sizeof(IMMSV_EVT));

	TRACE_2("finalize for handle: %llx", evt->info.finReq.client_hdl);
	immnd_client_node_get(cb, evt->info.finReq.client_hdl, &cl_node);
	/* Skipping check for stale client for this case (finalize client call)
	 */
	if (cl_node == NULL) {
		LOG_WA("IMMND - Client Node Get Failed for cli_hdl %llu",
		       evt->info.finReq.client_hdl);
		send_evt.info.imma.info.errRsp.error = SA_AIS_ERR_BAD_HANDLE;
		goto agent_rsp;
	}

	immnd_proc_imma_discard_connection(cb, cl_node, false);

	rc = immnd_client_node_del(cb, cl_node);
	if (rc == NCSCC_RC_FAILURE) {
		LOG_ER("IMMND - Client Tree Del Failed , cli_hdl");
		send_evt.info.imma.info.errRsp.error = SA_AIS_ERR_LIBRARY;
	}

	free(cl_node);

	send_evt.info.imma.info.errRsp.error = SA_AIS_OK;

agent_rsp:
	send_evt.type = IMMSV_EVT_TYPE_IMMA;
	send_evt.info.imma.type = IMMA_EVT_ND2A_IMM_FINALIZE_RSP;
	rc = immnd_mds_send_rsp(cb, sinfo, &send_evt);
	return rc;
}

/****************************************************************************
 * Name          : immnd_evt_proc_cl_imma_timeout
 *
 * Description   : Function that receives message from an imma client that
 *                 the client handle/node has timed out in the library on
 *                 a syncronous downcall. This means that unless the reply
 *                 (such as from an OI) does arrive to clear the call, then
 *                 the handle/node is doomed in the current implementation.
 *                 This is handled by reducing the number of TRY_AGAINs
 *                 (rejected new syncronous calls) allowed on the blocked
 *                 cl_node.
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *                 bool isOm - true=> OM finalize, false => OI finalize.
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 * Notes         : None.
 *****************************************************************************/
static uint32_t immnd_evt_proc_cl_imma_timeout(IMMND_CB *cb, IMMND_EVT *evt)
{
	IMMND_IMM_CLIENT_NODE *cl_node = NULL;
	TRACE_ENTER();

	TRACE_2("timeout in imma library for handle: %llx",
		evt->info.finReq.client_hdl);
	immnd_client_node_get(cb, evt->info.finReq.client_hdl, &cl_node);
	if (!cl_node) {
		goto done;
	}

	SaUint32T clientId =
	    m_IMMSV_UNPACK_HANDLE_HIGH(evt->info.finReq.client_hdl);
	if (immModel_purgeSyncRequest(cb, clientId)) {
		/* One and only one request has been purged => no reply message
		   send will be attempted for *that* request. We can safely
		   clear the MDS reply info. This will also clear the handle for
		   new use in sending syncronous requests.
		*/
		immnd_proc_imma_discard_connection(cb, cl_node, false);
		osafassert(immnd_client_node_del(cb, cl_node) ==
			   NCSCC_RC_SUCCESS);
		free(cl_node);
	} else {
		/* The request could not be purged (depends on request type),
		   or the reply has already been arrived but came too late
		   to reach a client that has timedout.
		*/
		if (immnd_mds_client_not_busy(&(cl_node->tmpSinfo)) ==
		    SA_AIS_ERR_TRY_AGAIN) {
			/* Reply has not arrived, i.e. the request type was not
			   possible to purge. Force any subsequent syncronous
			   calls to invalidate the handle.
			*/
			cl_node->tmpSinfo.mSynReqCount = 255;
		}
	}

done:
	TRACE_LEAVE();
	return NCSCC_RC_SUCCESS;
}

/****************************************************************************
 * Name          : immnd_evt_proc_imm_resurrect
 *
 * Description   : Function to attempt to resurrect an IMMA client_id after an
 *                 IMMND restart. This to try to make the restart transparent
 *                 towards the clients.
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *                 bool isOm - true=> OM resurrect, false => OI resurrect.
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 * Notes         : None.
 *****************************************************************************/
static uint32_t immnd_evt_proc_imm_resurrect(IMMND_CB *cb, IMMND_EVT *evt,
					     IMMSV_SEND_INFO *sinfo, bool isOm)
{
	/* Note: parameter isOm is ignored, should be noted in cl_node*/
	IMMSV_EVT send_evt;
	uint32_t rc = NCSCC_RC_SUCCESS;
	SaAisErrorT error = SA_AIS_OK;
	IMMND_IMM_CLIENT_NODE *cl_node = NULL;
	SaUint32T clientId =
	    m_IMMSV_UNPACK_HANDLE_HIGH(evt->info.finReq.client_hdl);
	SaUint32T nodeId =
	    m_IMMSV_UNPACK_HANDLE_LOW(evt->info.finReq.client_hdl);

	memset(&send_evt, '\0', sizeof(IMMSV_EVT));

	TRACE_2("resurect for handle: %llx", evt->info.finReq.client_hdl);

	/* First check that no one else grabbed the old handle value before us.
	 */
	immnd_client_node_get(cb, evt->info.finReq.client_hdl, &cl_node);
	if (cl_node != NULL) {
		if (cl_node->mIsResurrect) {
			/* The temporary client node is still present
			   => this immnd is still being synced. */
			TRACE_2(
			    "Rejecting OM client resurrect, sync not complete");
			error = SA_AIS_ERR_TRY_AGAIN;
		} else {
			LOG_ER(
			    "IMMND - Client Node already occupied for handle %llu",
			    evt->info.finReq.client_hdl);
			error = SA_AIS_ERR_EXIST;
		}
		goto agent_rsp;
	}

	int load_pid = immModel_getLoader(cb);

	if (load_pid != 0) {
		TRACE_2("Rejecting OM client resurrect if sync not complete");
		error = SA_AIS_ERR_TRY_AGAIN;
		goto agent_rsp;
	}

	/* Go ahead and create the client object using the old handle value. */

	cl_node = calloc(1, sizeof(IMMND_IMM_CLIENT_NODE));
	if (cl_node == NULL) {
		LOG_ER("IMMND - Client Alloc Failed");
		error = SA_AIS_ERR_NO_MEMORY;
		goto agent_rsp;
	}

	if ((cb->cli_id_gen <= clientId) && (clientId < 0x000f0000)) {
		cb->cli_id_gen = clientId + 1;
	}

	if (cb->node_id != nodeId) {
		LOG_ER("Rejecting OM client resurrect from wrong node! %x",
		       nodeId);
		free(cl_node);
		error = SA_AIS_ERR_FAILED_OPERATION;
		goto agent_rsp;
	}

	cl_node->imm_app_hdl = m_IMMSV_PACK_HANDLE(clientId, cb->node_id);
	cl_node->agent_mds_dest = sinfo->dest;
	/*cl_node->version= .. TODO correct version (not used today)*/
	cl_node->sv_id = (isOm) ? NCSMDS_SVC_ID_IMMA_OM : NCSMDS_SVC_ID_IMMA_OI;

	if (immnd_client_node_add(cb, cl_node) != NCSCC_RC_SUCCESS) {
		LOG_ER("IMMND - Client Tree Add Failed , cli_hdl");
		free(cl_node);
		error = SA_AIS_ERR_FAILED_OPERATION;
		goto agent_rsp;
	}

	TRACE_2("Added client with id: %llx <node:%x, count:%u>",
		cl_node->imm_app_hdl, cb->node_id, (SaUint32T)clientId);

	error = SA_AIS_OK;

agent_rsp:
	send_evt.type = IMMSV_EVT_TYPE_IMMA;
	send_evt.info.imma.type = IMMA_EVT_ND2A_IMM_RESURRECT_RSP;
	send_evt.info.imma.info.errRsp.error = error;
	rc = immnd_mds_send_rsp(cb, sinfo, &send_evt);
	return rc;
}

/****************************************************************************
 * Name          : immnd_evt_proc_imm_client_high
 *
 * Description   : Function to bump up the clientId counter after
 *                 IMMND restart. The clientId would be bumped up by
 *                 client_high messages from all pre-existing IMMAgents.
 *                 This is an attempt to increase the chances of successfull
 *                 client resurrections, when client_high is a low number.
 *
 *                 The counter does wrap arround, but a restarted IMMND will
 *                 restart the counter from zero, causing high risk for
 *                 collision and failed resurrects.
 *                 We skip bumping up the counter if the clientHigh is too
 *                 high.
 *
 *****************************************************************************/
static uint32_t immnd_evt_proc_imm_client_high(IMMND_CB *cb, IMMND_EVT *evt,
					       IMMSV_SEND_INFO *sinfo,
					       bool isOm)
{
	SaUint32T clientHigh = evt->info.initReq.client_pid;
	IMMND_IMM_CLIENT_NODE *cl_node = NULL;

	TRACE_2("Client high received: %x", clientHigh);

	if ((cb->cli_id_gen <= clientHigh) && (clientHigh < 0x000f0000)) {
		cb->cli_id_gen = evt->info.initReq.client_pid + 1;
		TRACE_2("Client high bumped up to: %x, IMMND just restarted?",
			cb->cli_id_gen);
	}

	/* Create a temporary client place holder for resurrect.*/
	cl_node = calloc(1, sizeof(IMMND_IMM_CLIENT_NODE));
	memset(cl_node, '\0', sizeof(IMMND_IMM_CLIENT_NODE));
	osafassert(cl_node);
	cl_node->imm_app_hdl = m_IMMSV_PACK_HANDLE(clientHigh, cb->node_id);
	cl_node->agent_mds_dest = sinfo->dest;
	cl_node->sv_id = (isOm) ? NCSMDS_SVC_ID_IMMA_OM : NCSMDS_SVC_ID_IMMA_OI;
	cl_node->mIsResurrect = 0x1;

	if (immnd_client_node_add(cb, cl_node) != NCSCC_RC_SUCCESS) {
#if 0 // CLOUD-PROTO  clients should be discarded !!!!
	    LOG_ER("IMMND - Adding temporary imma client Failed.");
	    /*free(cl_node);*/
	    abort();
#endif
	}

	TRACE_2("Added client with id: %llx <node:%x, count:%u>",
		cl_node->imm_app_hdl, cb->node_id, clientHigh);

	return NCSCC_RC_SUCCESS;
}

/****************************************************************************
 * Name          : immnd_evt_proc_recover_ccb_result
 *
 * Description   : Function attempts to obtain the result of a terminated CCB.
 *                 This is used in IMMA resurrect (both OM and OI side) to hide
 *                 the effects of losing contact with IMMND during a CCB in
 *progress. Of particular importance is resolving the outcome of a ccb-apply.
 *
 *                 The possible replies to IMMA are:
 *                 SA_AIS_ERR_TRY_AGAIN - If this IMMND is not in sync (not sent
 *from here). SA_AIS_OK - The CCB id was commited. SA_AIS_ERR_FAILED_OPERATION -
 *The CCB id was aborted. SA_AIS_ERR_NO_RESOURCES - The CCB id
 *is unknown (too old) or in progress (should not be possible).
 *
 *****************************************************************************/
static uint32_t immnd_evt_proc_recover_ccb_result(IMMND_CB *cb, IMMND_EVT *evt,
						  IMMSV_SEND_INFO *sinfo)
{
	IMMSV_EVT send_evt;
	uint32_t rc = NCSCC_RC_SUCCESS;
	TRACE_ENTER();

	memset(&send_evt, '\0', sizeof(IMMSV_EVT));
	send_evt.type = IMMSV_EVT_TYPE_IMMA;
	send_evt.info.imma.type = IMMA_EVT_ND2A_IMM_ERROR;

	int load_pid = immModel_getLoader(cb);

	if (load_pid != 0) {
		TRACE_2("Rejecting OM client resurrect if sync not complete");
		send_evt.info.imma.info.errRsp.error = SA_AIS_ERR_TRY_AGAIN;
	} else {
		send_evt.info.imma.info.errRsp.error =
		    immModel_ccbResult(cb, evt->info.ccbId);
	}

	rc = immnd_mds_send_rsp(cb, sinfo, &send_evt);

	if (rc != NCSCC_RC_SUCCESS) {
		LOG_WA("Failed to send response to IMMA over MDS rc:%u", rc);
	}

	TRACE_LEAVE();
	return NCSCC_RC_SUCCESS;
}

/****************************************************************************
 * Name          : immnd_evt_proc_admowner_init
 *
 * Description   : Function to process the SaImmOmAdminOwnerInitialize
 *                 from Applications
 *
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 * Notes         : None.
 *****************************************************************************/
static uint32_t immnd_evt_proc_admowner_init(IMMND_CB *cb, IMMND_EVT *evt,
					     IMMSV_SEND_INFO *sinfo)
{
	IMMSV_EVT send_evt;
	uint32_t rc = NCSCC_RC_SUCCESS;
	IMMND_IMM_CLIENT_NODE *cl_node = NULL;
	SaImmHandleT client_hdl;

	memset(&send_evt, '\0', sizeof(IMMSV_EVT));

	client_hdl = evt->info.adminitReq.client_hdl;
	immnd_client_node_get(cb, client_hdl, &cl_node);
	if (cl_node == NULL || cl_node->mIsStale) {
		LOG_WA("ERR_BAD_HANDLE: Client %llu not found in server",
		       client_hdl);
		send_evt.info.imma.info.admInitRsp.error =
		    SA_AIS_ERR_BAD_HANDLE;
		goto agent_rsp;
	}

	if (!immnd_is_immd_up(cb)) {
		send_evt.info.imma.info.admInitRsp.error = SA_AIS_ERR_TRY_AGAIN;
		goto agent_rsp;
	}

	if (immModel_immNotWritable(cb) ||
	    (cb->mSyncFinalizing && cb->fevs_out_count)) {
		/*Avoid broadcasting doomed requests. */
		send_evt.info.imma.info.admInitRsp.error = SA_AIS_ERR_TRY_AGAIN;
		goto agent_rsp;
	}

	if (cb->fevs_replies_pending >= IMMSV_DEFAULT_FEVS_MAX_PENDING) {
		TRACE_2(
		    "ERR_TRY_AGAIN: Too many pending incoming fevs messages (> %u) rejecting admo_init request",
		    IMMSV_DEFAULT_FEVS_MAX_PENDING);
		send_evt.info.imma.info.admInitRsp.error = SA_AIS_ERR_TRY_AGAIN;
		goto agent_rsp;
	}

	send_evt.info.imma.info.admInitRsp.error =
	    immnd_mds_client_not_busy(&(cl_node->tmpSinfo));

	if (send_evt.info.imma.info.admInitRsp.error != SA_AIS_OK) {
		if (send_evt.info.imma.info.admInitRsp.error ==
		    SA_AIS_ERR_BAD_HANDLE) {
			/* The connection appears to be hung indefinitely
			   waiting for a reply on a previous syncronous call.
			   Discard the connection and return BAD_HANDLE to allow
			   client to recover and make progress.
			 */
			immnd_proc_imma_discard_connection(cb, cl_node, false);
			rc = immnd_client_node_del(cb, cl_node);
			osafassert(rc == NCSCC_RC_SUCCESS);
			free(cl_node);
		}
		goto agent_rsp;
	}

	/*aquire a ND sender count and send the fevs to ND (without waiting) */
	cb->fevs_replies_pending++; /*flow control */
	if (cb->fevs_replies_pending > 1) {
		TRACE("Messages pending:%u", cb->fevs_replies_pending);
	}

	send_evt.type = IMMSV_EVT_TYPE_IMMD;
	send_evt.info.immd.type = IMMD_EVT_ND2D_ADMINIT_REQ;
	send_evt.info.immd.info.admown_init.client_hdl =
	    evt->info.adminitReq.client_hdl;

	send_evt.info.immd.info.admown_init.i = evt->info.adminitReq.i;

	/* send the request to the IMMD, reply comes back over fevs. */

	rc = immnd_mds_msg_send(cb, NCSMDS_SVC_ID_IMMD, cb->immd_mdest_id,
				&send_evt);

	if (rc != NCSCC_RC_SUCCESS) {
		LOG_ER("IMMND - AdminOwner Initialize Failed");
		send_evt.info.imma.info.admInitRsp.error = SA_AIS_ERR_TRY_AGAIN;
		cb->fevs_replies_pending--;
		goto agent_rsp;
	}

	/*Save sinfo in continuation.
	   Note: should set up a wait time for the continuation roughly in line
	   with IMMSV_WAIT_TIME. */
	cl_node->tmpSinfo = *sinfo; // TODO should be part of continuation ?

	return rc;

agent_rsp:
	send_evt.type = IMMSV_EVT_TYPE_IMMA;
	send_evt.info.imma.type = IMMA_EVT_ND2A_IMM_ADMINIT_RSP;

	TRACE_2("immnd_evt:imm_adminOwner_init: SENDRSP FAIL %u ",
		send_evt.info.imma.info.admInitRsp.error);
	rc = immnd_mds_send_rsp(cb, sinfo, &send_evt);

	return rc;
}

/****************************************************************************
 * Name          : immnd_evt_proc_impl_set
 *
 * Description   : Function to process the SaImmOiImplementerSet
 *                 from Applications
 *
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 * Notes         : None.
 *****************************************************************************/
static uint32_t immnd_evt_proc_impl_set(IMMND_CB *cb, IMMND_EVT *evt,
					IMMSV_SEND_INFO *sinfo)
{
	IMMSV_EVT send_evt;
	uint32_t rc = NCSCC_RC_SUCCESS;
	IMMND_IMM_CLIENT_NODE *cl_node = NULL;
	SaImmHandleT client_hdl;

	memset(&send_evt, '\0', sizeof(IMMSV_EVT));

	client_hdl = evt->info.implSet.client_hdl;
	immnd_client_node_get(cb, client_hdl, &cl_node);
	if (cl_node == NULL || cl_node->mIsStale) {
		LOG_WA("ERR_BAD_HANDLE: Client %llu not found in server",
		       client_hdl);
		send_evt.info.imma.info.implSetRsp.error =
		    SA_AIS_ERR_BAD_HANDLE;
		goto agent_rsp;
	}

	if (!immnd_is_immd_up(cb)) {
		send_evt.info.imma.info.implSetRsp.error = SA_AIS_ERR_TRY_AGAIN;
		goto agent_rsp;
	}

	if (!immModel_protocol43Allowed(cb) &&
	    (immModel_immNotWritable(cb) ||
	     (cb->mSyncFinalizing && cb->fevs_out_count))) {
		/*Avoid broadcasting doomed requests. */
		send_evt.info.imma.info.implSetRsp.error = SA_AIS_ERR_TRY_AGAIN;
		goto agent_rsp;
	}

	if (evt->type == IMMND_EVT_A2ND_OI_IMPL_SET_2 &&
	    !immModel_protocol45Allowed(cb)) {
		LOG_WA(
		    "Failed to set OI implementer (%u) with OI callback timeout "
		    "(OpenSAF 4.5 features are disabled)",
		    evt->info.implSet.impl_id);
		send_evt.info.imma.info.implSetRsp.error =
		    SA_AIS_ERR_NO_RESOURCES;
		goto agent_rsp;
	}

	if (cb->fevs_replies_pending >= IMMSV_DEFAULT_FEVS_MAX_PENDING) {
		TRACE_2(
		    "ERR_TRY_AGAIN: Too many pending incoming fevs messages (> %u) rejecting impl_set request",
		    IMMSV_DEFAULT_FEVS_MAX_PENDING);
		send_evt.info.imma.info.implSetRsp.error = SA_AIS_ERR_TRY_AGAIN;
		goto agent_rsp;
	}

	if (strncmp(evt->info.implSet.impl_name.buf, OPENSAF_IMM_PBE_IMPL_NAME,
		    evt->info.implSet.impl_name.size) == 0) {
		if (cl_node->mIsPbe) {
			TRACE("Persistent Back End OI %s is attaching",
			      OPENSAF_IMM_PBE_IMPL_NAME);
		} else {
			LOG_WA(
			    "Will not allow Pbe implementer %s to attach, client was not forked by this IMMND",
			    OPENSAF_IMM_PBE_IMPL_NAME);
			send_evt.info.imma.info.implSetRsp.error =
			    SA_AIS_ERR_BAD_HANDLE;
			goto agent_rsp;
		}
	}

	send_evt.info.imma.info.implSetRsp.error =
	    immnd_mds_client_not_busy(&(cl_node->tmpSinfo));

	if (send_evt.info.imma.info.implSetRsp.error != SA_AIS_OK) {
		if (send_evt.info.imma.info.implSetRsp.error ==
		    SA_AIS_ERR_BAD_HANDLE) {
			/* The connection appears to be hung indefinitely
			   waiting for a reply on a previous syncronous call.
			   Discard the connection and return BAD_HANDLE to allow
			   client to recover and make progress.
			 */
			immnd_proc_imma_discard_connection(cb, cl_node, false);
			rc = immnd_client_node_del(cb, cl_node);
			osafassert(rc == NCSCC_RC_SUCCESS);
			free(cl_node);
		}
		goto agent_rsp;
	}

	/* Avoid sending the implSet request if the implementer is currently
	   occupied locally. This greatly reduces the risk of a an incorrect
	   create of implementer at sync clients, where the create is rejected
	   at veteran nodes. It can still happen if the implementer is free here
	   and now, but gets occupied soon after in a subsequent implementerSet
	   over fevs arriving before this implementerSet arrives back over fevs.
	   See finalizeSync #1871.
	*/
	SaUint32T impl_id;
	send_evt.info.imma.info.implSetRsp.error =
	    immModel_implIsFree(cb, &evt->info.implSet, &impl_id);

	if (send_evt.info.imma.info.implSetRsp.error != SA_AIS_OK) {
		if (impl_id && send_evt.info.imma.info.implSetRsp.error ==
				   SA_AIS_ERR_EXIST) {
			/* Immediately respond OK to agent */
			send_evt.info.imma.info.implSetRsp.error = SA_AIS_OK;
			send_evt.info.imma.info.implSetRsp.implId = impl_id;
		}
		goto agent_rsp;
	}

	cb->fevs_replies_pending++; /*flow control */
	if (cb->fevs_replies_pending > 1) {
		TRACE("Messages pending:%u", cb->fevs_replies_pending);
	}

	send_evt.type = IMMSV_EVT_TYPE_IMMD;
	send_evt.info.immd.info.impl_set.r.client_hdl = client_hdl;
	send_evt.info.immd.info.impl_set.r.impl_name.size =
	    evt->info.implSet.impl_name.size;
	send_evt.info.immd.info.impl_set.r.impl_name.buf =
	    evt->info.implSet.impl_name
		.buf; /*Warning re-using buffer, no copy. */
	send_evt.info.immd.info.impl_set.reply_dest = cb->immnd_mdest_id;

	if (evt->type == IMMND_EVT_A2ND_OI_IMPL_SET_2) {
		send_evt.info.immd.info.impl_set.r.oi_timeout =
		    evt->info.implSet.oi_timeout;
		send_evt.info.immd.type = IMMD_EVT_ND2D_IMPLSET_REQ_2;
	} else {
		send_evt.info.immd.type = IMMD_EVT_ND2D_IMPLSET_REQ;
	}

	/* send the request to the IMMD, reply comes back over fevs. */

	rc = immnd_mds_msg_send(cb, NCSMDS_SVC_ID_IMMD, cb->immd_mdest_id,
				&send_evt);

	send_evt.info.immd.info.impl_set.r.impl_name.size = 0;
	send_evt.info.immd.info.impl_set.r.impl_name.buf =
	    NULL; /*precaution. */

	if (rc != NCSCC_RC_SUCCESS) {
		LOG_ER("Problem in sending to IMMD over MDS");
		send_evt.info.imma.info.implSetRsp.error = SA_AIS_ERR_TRY_AGAIN;
		cb->fevs_replies_pending--;
		goto agent_rsp;
	}

	/*Save sinfo in continuation.
	   Note should set up a wait time for the continuation roughly in line
	   with IMMSV_WAIT_TIME.
	 */
	cl_node->tmpSinfo = *sinfo; // TODO: should be part of continuation?

	return rc;

agent_rsp:
	send_evt.type = IMMSV_EVT_TYPE_IMMA;
	send_evt.info.imma.type = IMMA_EVT_ND2A_IMPLSET_RSP;

	TRACE_2("SENDRSP FAIL %u ", send_evt.info.imma.info.implSetRsp.error);
	rc = immnd_mds_send_rsp(cb, sinfo, &send_evt);

	return rc;
}

/****************************************************************************
 * Name          : immnd_evt_proc_ccb_init
 *
 * Description   : Function to process the SaImmOmCcbInitialize
 *                 from Applications
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 * Notes         : None.
 *****************************************************************************/
static uint32_t immnd_evt_proc_ccb_init(IMMND_CB *cb, IMMND_EVT *evt,
					IMMSV_SEND_INFO *sinfo)
{
	IMMSV_EVT send_evt;
	uint32_t rc = NCSCC_RC_SUCCESS;
	IMMND_IMM_CLIENT_NODE *cl_node = NULL;
	SaImmHandleT client_hdl;
	TRACE_ENTER();

	memset(&send_evt, '\0', sizeof(IMMSV_EVT));

	client_hdl = evt->info.ccbinitReq.client_hdl;
	immnd_client_node_get(cb, client_hdl, &cl_node);
	if (cl_node == NULL || cl_node->mIsStale) {
		LOG_WA("ERR_BAD_HANDLE: Client %llu not found in server",
		       client_hdl);
		send_evt.info.imma.info.ccbInitRsp.error =
		    SA_AIS_ERR_BAD_HANDLE;
		goto agent_rsp;
	}

	if (!immnd_is_immd_up(cb)) {
		send_evt.info.imma.info.ccbInitRsp.error = SA_AIS_ERR_TRY_AGAIN;
		goto agent_rsp;
	}

	if (immModel_immNotWritable(cb) ||
	    (cb->mSyncFinalizing && cb->fevs_out_count)) {
		/*Avoid broadcasting doomed requests. */
		send_evt.info.imma.info.ccbInitRsp.error = SA_AIS_ERR_TRY_AGAIN;
		goto agent_rsp;
	}

	if (cb->fevs_replies_pending >= IMMSV_DEFAULT_FEVS_MAX_PENDING) {
		TRACE_2(
		    "ERR_TRY_AGAIN: Too many pending incoming fevs messages (> %u) rejecting ccb_init request",
		    IMMSV_DEFAULT_FEVS_MAX_PENDING);
		send_evt.info.imma.info.ccbInitRsp.error = SA_AIS_ERR_TRY_AGAIN;
		goto agent_rsp;
	}

	send_evt.info.imma.info.ccbInitRsp.error =
	    immnd_mds_client_not_busy(&(cl_node->tmpSinfo));

	if (send_evt.info.imma.info.ccbInitRsp.error != SA_AIS_OK) {
		if (send_evt.info.imma.info.ccbInitRsp.error ==
		    SA_AIS_ERR_BAD_HANDLE) {
			/* The connection appears to be hung indefinitely
			   waiting for a reply on a previous syncronous call.
			   Discard the connection and return BAD_HANDLE to allow
			   client to recover and make progress.
			 */
			immnd_proc_imma_discard_connection(cb, cl_node, false);
			rc = immnd_client_node_del(cb, cl_node);
			osafassert(rc == NCSCC_RC_SUCCESS);
			free(cl_node);
		}
		goto agent_rsp;
	}

	cb->fevs_replies_pending++; /*flow control */
	if (cb->fevs_replies_pending > 1) {
		TRACE("Messages pending:%u", cb->fevs_replies_pending);
	}

	send_evt.type = IMMSV_EVT_TYPE_IMMD;
	send_evt.info.immd.type = IMMD_EVT_ND2D_CCBINIT_REQ;
	send_evt.info.immd.info.ccb_init = evt->info.ccbinitReq;

	/* send the request to the IMMD, reply comes back over fevs. */

	TRACE_2("Sending CCB init msg to IMMD AO:%u FL:%llx client:%llu",
		send_evt.info.immd.info.ccb_init.adminOwnerId,
		send_evt.info.immd.info.ccb_init.ccbFlags,
		send_evt.info.immd.info.ccb_init.client_hdl);
	rc = immnd_mds_msg_send(cb, NCSMDS_SVC_ID_IMMD, cb->immd_mdest_id,
				&send_evt);

	if (rc != NCSCC_RC_SUCCESS) {
		LOG_ER("Problem in sending ro IMMD over MDS");
		send_evt.info.imma.info.ccbInitRsp.error = SA_AIS_ERR_TRY_AGAIN;
		cb->fevs_replies_pending--;
		goto agent_rsp;
	}

	/*Save sinfo in continuation.
	   Note should set up a wait time for the continuation roughly in line
	   with IMMSV_WAIT_TIME. */
	cl_node->tmpSinfo = *sinfo; // TODO should be part of continuation?
	TRACE_LEAVE();
	return rc;

agent_rsp:
	send_evt.type = IMMSV_EVT_TYPE_IMMA;
	send_evt.info.imma.type = IMMA_EVT_ND2A_CCBINIT_RSP;

	TRACE_2("SENDRSP FAIL %u ", send_evt.info.imma.info.ccbInitRsp.error);
	rc = immnd_mds_send_rsp(cb, sinfo, &send_evt);

	TRACE_LEAVE();
	return rc;
}

/****************************************************************************
 * Name          : immnd_evt_proc_rt_update
 *
 * Description   : Function to process the saImmOiRtObjectUpdate/_2
 *                 from local agent. Only pure local rtattrs are
 *                 updated here. Cached and Persistent rtattrs are first
 *                 forwarded over fevs.
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 * Notes         : None.
 *****************************************************************************/

static uint32_t immnd_evt_proc_rt_update(IMMND_CB *cb, IMMND_EVT *evt,
					 IMMSV_SEND_INFO *sinfo)
{
	IMMSV_EVT send_evt;
	uint32_t rc = NCSCC_RC_SUCCESS;
	SaAisErrorT err = SA_AIS_OK;
	IMMND_IMM_CLIENT_NODE *cl_node = NULL;
	SaImmHandleT client_hdl;
	SaUint32T clientId;
	SaUint32T clientNode;
	SaUint32T dummyPbeConn = 0;
	SaUint32T dummyPbe2BConn = 0;
	NCS_NODE_ID *dummyPbeNodeIdPtr = NULL;
	SaUint32T dummyContinuationId = 0;
	SaUint32T spApplConn = 0; /* Special applier locally connected. */
	TRACE_ENTER();
	unsigned int isPureLocal = 1;

	client_hdl = evt->info.objModify.immHandle;
	immnd_client_node_get(cb, client_hdl, &cl_node);
	if (cl_node == NULL || cl_node->mIsStale) {
		LOG_WA("ERR_BAD_HANDLE: Client %llu not found in server",
		       client_hdl);
		err = SA_AIS_ERR_BAD_HANDLE;
		goto agent_rsp;
	}

	/* Do broadcast checks BEFORE updating model because we dont want
	   to do the update and then fail to propagate it if it should be
	   propagated. Dowsnide: Even pure local rtattrs can not be updated if
	   we can not communicate with the IMMD. */

	if (!immnd_is_immd_up(cb)) {
		err = SA_AIS_ERR_TRY_AGAIN;
		goto agent_rsp;
	}

	err = immnd_mds_client_not_busy(&(cl_node->tmpSinfo));
	if (err != SA_AIS_OK) {
		if (err == SA_AIS_ERR_BAD_HANDLE) {
			/* The connection appears to be hung indefinitely
			   waiting for a reply on a previous syncronous call.
			   Discard the connection and return BAD_HANDLE to allow
			   client to recover and make progress.
			 */
			immnd_proc_imma_discard_connection(cb, cl_node, false);
			rc = immnd_client_node_del(cb, cl_node);
			osafassert(rc == NCSCC_RC_SUCCESS);
			free(cl_node);
		}
		goto agent_rsp;
	}

	clientId = m_IMMSV_UNPACK_HANDLE_HIGH(client_hdl);
	clientNode = m_IMMSV_UNPACK_HANDLE_LOW(client_hdl);

	err = immModel_rtObjectUpdate(
	    cb, &(evt->info.objModify), clientId, clientNode, &isPureLocal,
	    &dummyContinuationId, &dummyPbeConn, dummyPbeNodeIdPtr, &spApplConn,
	    &dummyPbe2BConn);

	osafassert(!dummyContinuationId && !dummyPbeConn && !spApplConn &&
		   !dummyPbe2BConn); /* Only relevant for cached (fevs first) */

	if (!isPureLocal && (err == SA_AIS_OK)) {
		TRACE_2(
		    "immnd_evt_proc_rt_update was not pure local, i.e. cached RT attrs");
		/*This message is allowed even when imm not writable. But
		   isPureLocal should never be false if we are not writable,
		   thus we should never get to this code branch if imm is not
		   writbale.
		 */

		if (cb->fevs_replies_pending >=
		    IMMSV_DEFAULT_FEVS_MAX_PENDING) {
			TRACE_2(
			    "ERR_TRY_AGAIN: Too many pending incoming fevs messages (> %u) rejecting rt_update request",
			    IMMSV_DEFAULT_FEVS_MAX_PENDING);
			err = SA_AIS_ERR_TRY_AGAIN;
			goto agent_rsp;
		}

		cb->fevs_replies_pending++; /*flow control */
		if (cb->fevs_replies_pending > 1) {
			TRACE("Messages pending:%u", cb->fevs_replies_pending);
		}

		memset(&send_evt, '\0', sizeof(IMMSV_EVT));
		send_evt.type = IMMSV_EVT_TYPE_IMMD;
		send_evt.info.immd.type = IMMD_EVT_ND2D_OI_OBJ_MODIFY;
		send_evt.info.immd.info.objModify = evt->info.objModify;
		/* Borrow pointer structures. */
		/* send the request to the IMMD, reply comes back over fevs. */

		if (!immnd_is_immd_up(cb)) {
			err = SA_AIS_ERR_TRY_AGAIN;
			cb->fevs_replies_pending--;
			goto agent_rsp;
		}

		rc = immnd_mds_msg_send(cb, NCSMDS_SVC_ID_IMMD,
					cb->immd_mdest_id, &send_evt);

		if (rc != NCSCC_RC_SUCCESS) {
			LOG_ER("Problem in sending to IMMD over MDS");
			err = SA_AIS_ERR_TRY_AGAIN;
			cb->fevs_replies_pending--;
			goto agent_rsp;
		}

		/*Save sinfo in continuation.
		   Note should set up a wait time for the continuation roughly
		   in line with IMMSV_WAIT_TIME. */
		cl_node->tmpSinfo =
		    *sinfo; // TODO should be part of continuation?
		goto done;
	}

	TRACE_2("immnd_evt_proc_rt_update was pure local, i.e. pure RT attrs");
/* Here is where we should end up if the saImmOiRtObjectUpdate_2 call was
   made because of an SaImmOiRtAttrUpdateCallbackT upcall. In that case
   we are pulling values for pure (i.e. local) runtime attributes.
   If the saImmOiRtObjectUpdate_2 call is a push call from the implementer
   then the attributes are expected to be cached, i.e. non-local. */

agent_rsp:

	memset(&send_evt, '\0', sizeof(IMMSV_EVT));
	TRACE_2("send immediate reply to client/agent");
	send_evt.type = IMMSV_EVT_TYPE_IMMA;
	send_evt.info.imma.info.errRsp.error = err;
	send_evt.info.imma.type = IMMA_EVT_ND2A_IMM_ERROR;
	TRACE_2("SENDRSP RESULT %u ", err);
	rc = immnd_mds_send_rsp(cb, sinfo, &send_evt);

done:
	free(evt->info.objModify.objectName.buf);
	evt->info.objModify.objectName.buf = NULL;
	evt->info.objModify.objectName.size = 0;
	immsv_free_attrmods(evt->info.objModify.attrMods);
	evt->info.objModify.attrMods = NULL;
	TRACE_LEAVE();
	return rc;
}

#if 0 /*Only for debug */
static void dump_usrbuf(USRBUF *ub)
{
	if (ub) {
		TRACE_2("IMMND dump_usrbuf: ");
		TRACE_2("next:%p ", ub->next);
		TRACE_2("link:%p ", ub->link);
		TRACE_2("count:%u ", ub->count);
		TRACE_2("start:%u ", ub->start);
		TRACE_2("pool_ops:%p ", ub->pool_ops);
		TRACE_2("specific.opaque:%u ", ub->specific.opaque);
		TRACE_2("payload:%p", ub->payload);
	} else {
		TRACE_2("EMPTY", ub->start);
	}
}
#endif

/****************************************************************************
 * Name          : immnd_evt_proc_fevs_forward
 *
 * Description   : Function to handle (forward) fevs messages.
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *                 sinfo    - Reply address for syncronous call.
 *                 onStack - true => Message resides on stack.
 *                           false => message resides on heap.
 *                 newMsg  - true => If the fevs out-queue is not empty,
 *                                      New messages can be forced to go via
 *                                      the out-queue even when there is no
 *                                      fevs overflow. This to maintain sender
 *                                      order and not only IMMD fevs order.
 *                                      Sender order is currently used during
 *                                      finalizeSync when the finalizeSync has
 *                                      been sent from coord, until it has been
 *                                      received (cb->mSyncFinalizing is true).
 *                           false =>Message has already passed queue.
 *                                      Fevs order through IMMD is enough.
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *****************************************************************************/
static uint32_t immnd_evt_proc_fevs_forward(IMMND_CB *cb, IMMND_EVT *evt,
					    IMMSV_SEND_INFO *sinfo,
					    bool onStack, bool newMsg)
{
	IMMSV_EVT send_evt;
	uint32_t rc = NCSCC_RC_SUCCESS;
	SaAisErrorT error;
	IMMND_IMM_CLIENT_NODE *cl_node = NULL;
	SaImmHandleT client_hdl;
	bool asyncReq = (!sinfo || sinfo->stype != MDS_SENDTYPE_SNDRSP);

	TRACE_2("sender_count: %llu size: %u ", evt->info.fevsReq.sender_count,
		evt->info.fevsReq.msg.size);

	client_hdl = evt->info.fevsReq.client_hdl;
	immnd_client_node_get(cb, client_hdl, &cl_node);
	if (cl_node == NULL || cl_node->mIsStale) {
		if (asyncReq) {
			TRACE(
			    "IMMND - Client %llu went down on asyncronous request, still sending it",
			    client_hdl);
		} else {
			LOG_WA(
			    "IMMND - Client %llu went down on syncronous request, discarding request",
			    client_hdl);
			error = SA_AIS_ERR_BAD_HANDLE;
			goto agent_rsp;
		}
	}

	memset(&send_evt, '\0', sizeof(IMMSV_EVT));

	if (!immnd_is_immd_up(cb)) {
		if (asyncReq) {
			/* We could enqueue async messages (typically replies)
			   but there is a risk that a failover would be followed
			   by a "burst" of old replies, which could provoke more
			   problems than it solves.
			*/
			if (cb->mState == IMM_SERVER_SYNC_SERVER) {
				LOG_IN(
				    "Asyncronous FEVS request received when IMMD is down during sync, will buffer");
			} else {
				LOG_WA(
				    "IMMND - Director Service Is Down. Dropping asyncronous FEVS request.");
				return NCSCC_RC_FAILURE;
			}
		} else {
			LOG_WA(
			    "IMMND - Director Service Is Down. Rejecting FEVS request.");
			error = SA_AIS_ERR_TRY_AGAIN;
			goto agent_rsp;
		}
	}

	if (!asyncReq) {
		error = immnd_mds_client_not_busy(&(cl_node->tmpSinfo));
		if (error != SA_AIS_OK) {
			if (error == SA_AIS_ERR_BAD_HANDLE) {
				/* The connection appears to be hung
				   indefinitely waiting for a reply on a
				   previous syncronous call. Discard the
				   connection and return BAD_HANDLE to allow
				   client to recover and make progress.
				*/
				immnd_proc_imma_discard_connection(cb, cl_node,
								   false);
				rc = immnd_client_node_del(cb, cl_node);
				osafassert(rc == NCSCC_RC_SUCCESS);
				free(cl_node);
			}
			goto agent_rsp;
		}
	}

	if (newMsg) {
		error =
		    immnd_fevs_local_checks(cb, &(evt->info.fevsReq), sinfo);
		if (error != SA_AIS_OK) {
			/*Fevs request will NOT be forwarded to IMMD.
			  Return directly with error or OK for idempotent
			  requests.
			 */
			if (error == SA_AIS_ERR_NO_BINDINGS) {
				/* Bogus error code indicating that SA_AIS_OK
				   should be returned immediately on grounds of
				   idempotency. That is the state desired by the
				   request is already in place. This short
				   circuit of fevs due to idempotency detected
				   *locally* can only be done during on-going
				   sync, where *no* other interfering mutating
				   requests are allowed, i.e. such mutations
				   could not already be in the buffered fevs
				   backlog.
				 */
				osafassert(immModel_immNotWritable(cb));
				error = SA_AIS_OK;
			}

			if (asyncReq) {
				LOG_WA(
				    "Asyncronous FEVS message failed verification - dropping message!");
				return NCSCC_RC_FAILURE;
			}
			goto agent_rsp; // Fevs request is not forwarded to IMMD
		}
	}

	/* Special handling before forwarding. */
	if ((evt->info.fevsReq.sender_count == 0x1) &&
	    !(cl_node && cl_node->mIsSync) &&
	    (immModel_immNotWritable(cb) ||
	     (cb->mSyncFinalizing && cb->fevs_out_count))) {
		/* sender_count set to 1 if we are to check locally for
		  writability before sending to IMMD. This to avoid broadcasting
		  requests that are doomed anyway. The clause '!(cl_node &&
		  cl_node->mIsSync)' is there to allow the immsync process to
		  use saImmOmClassCreate. The local check is skipped for immsync
		  resulting in the class-creaet message to be broadcast over
		  fevs. The class create will be rejected at all normal nodes
		  with TRY_AGAIN, but accepted at sync clients. See #2754.

		  Immnd coord opens for writability when finalizeSync message is
		  generated. But finalizeSync is asyncronous since 4.1 and may
		  get enqueued into the out queue, instead of being sent
		  immediately. While finalizeSync is in the out queue,
					  syncronous fevs messages that require
		  writability, could bypass finalizeSync. This is BAD because
		  such messages can arrive back here at coord and be accepted,
		  but will be rejected at other nodes until they receive
		  finalizeSync and open for writes.

		  Such bypass is prevented here by also screening on
		  mSyncFinalizing, which is true starting when coord generates
		  finalize sync and ends when coord receives its own finalize
		  sync. This is actually more restrivtive than necessary. An
		  optimization is to detect when finalizeSync is actually sent
		  from coord, instead of waiting until it is received. As soon
		  as it is has been sent from the coord, any subsequent sends
		  from coord can not bypass the finalizeSync. We dont quite
		  capture the send of finalizeSync, but if the out-queue is
		  empty, then we know finalizeSync must have been sent.

		  Note that fevs messages that do not require IMMND writability,
		  such as runtime-attribute-updates, will not be affected by
		  this.
		*/

		if (asyncReq) {
			LOG_IN(
			    "Rare case (?) of enqueueing async & write message due to on-going sync.");
			immnd_enqueue_outgoing_fevs_msg(
			    cb, client_hdl, &(evt->info.fevsReq.msg));
			return NCSCC_RC_SUCCESS;
		} else {
			error = SA_AIS_ERR_TRY_AGAIN;
			goto agent_rsp;
		}
	}

	/* If overflow .....OR IMMD is down.....OR sync is on-going AND
	   out-queue is not empty => go via out-queue. The sync should be
	   throttled by immnd_evt_proc_search_next. */
	if ((cb->fevs_replies_pending >= IMMSV_DEFAULT_FEVS_MAX_PENDING) ||
	    !immnd_is_immd_up(cb) ||
	    ((cb->mState == IMM_SERVER_SYNC_SERVER) && cb->fevs_out_count &&
	     asyncReq && newMsg)) {
		if (asyncReq) {

			if (onStack) {
				/* Delaying the send => message must be copied
				 * to heap. */
				char *buf = malloc(evt->info.fevsReq.msg.size);
				memcpy(buf, evt->info.fevsReq.msg.buf,
				       evt->info.fevsReq.msg.size);
				evt->info.fevsReq.msg.buf = buf;
			}
			unsigned int backlog = immnd_enqueue_outgoing_fevs_msg(
			    cb, client_hdl, &(evt->info.fevsReq.msg));

			if (backlog % 50) {
				TRACE_2(
				    "Too many pending incoming FEVS messages (> %u) "
				    "enqueueing async message. Backlog:%u",
				    IMMSV_DEFAULT_FEVS_MAX_PENDING, backlog);
			} else {
				LOG_IN(
				    "Too many pending incoming FEVS messages (> %u) "
				    "enqueueing async message. Backlog:%u",
				    IMMSV_DEFAULT_FEVS_MAX_PENDING, backlog);
			}

			return NCSCC_RC_SUCCESS;
		} else {
			/* Syncronous message, never goes to out-queue, instead
			   push back to user. Should only be for the overflow
			   case. */
			TRACE_2(
			    "ERR_TRY_AGAIN: Too many pending FEVS message replies (> %u) rejecting request",
			    IMMSV_DEFAULT_FEVS_MAX_PENDING);
			error = SA_AIS_ERR_TRY_AGAIN;
			goto agent_rsp;
		}
	}

	cb->fevs_replies_pending++; /*flow control */
	if (cb->fevs_replies_pending > 1) {
		TRACE("Replies pending:%u", cb->fevs_replies_pending);
	}

	send_evt.type = IMMSV_EVT_TYPE_IMMD;
	if ((evt->type == IMMND_EVT_A2ND_IMM_FEVS_2) &&
	    immModel_protocol41Allowed(cb)) {
		send_evt.info.immd.type = IMMD_EVT_ND2D_FEVS_REQ_2;
	} else {
		send_evt.info.immd.type = IMMD_EVT_ND2D_FEVS_REQ;
	}
	send_evt.info.immd.info.fevsReq.reply_dest = cb->immnd_mdest_id;
	send_evt.info.immd.info.fevsReq.client_hdl = client_hdl;
	send_evt.info.immd.info.fevsReq.msg.size = evt->info.fevsReq.msg.size;
	/*Borrow the octet buffer from the input message instead of copying */
	send_evt.info.immd.info.fevsReq.msg.buf = evt->info.fevsReq.msg.buf;
	send_evt.info.immd.info.fevsReq.isObjSync =
	    (evt->type == IMMND_EVT_A2ND_IMM_FEVS_2)
		? (uint8_t)evt->info.fevsReq.isObjSync
		: 0x0;

	/* send the request to the IMMD */

	TRACE_2("SENDING FEVS TO IMMD");
	rc = immnd_mds_msg_send(cb, NCSMDS_SVC_ID_IMMD, cb->immd_mdest_id,
				&send_evt);

	if (rc != NCSCC_RC_SUCCESS) {
		cb->fevs_replies_pending--;
		if (asyncReq) {
			LOG_ER(
			    "Problem in sending asyncronous FEVS message over MDS - dropping message!");
			return NCSCC_RC_FAILURE;
		} else {
			LOG_WA("Problem in sending FEVS message over MDS");
			error = SA_AIS_ERR_TRY_AGAIN;
			goto agent_rsp;
		}
	}

	/*Save sinfo in continuation.
	   Note should set up a wait time for the continuation roughly in line
	   with IMMSV_WAIT_TIME. */
	osafassert(rc == NCSCC_RC_SUCCESS);

	if (evt->info.fevsReq.sender_count > 0x1) {
		/* sender_count greater than 1 if this is an admop message.
		 This is to generate a request continuation at the client
		 node BEFORE sending the request over fevs. Otherwise there
		 is a risk that the reply may arrive back at the request node,
		 before the request arrives over fevs. This can happen because
		 the reply is not sent over the fevs "bottleneck".
		 */
		SaUint32T conn = m_IMMSV_UNPACK_HANDLE_HIGH(client_hdl);
		SaInvocationT saInv =
		    (SaInvocationT)evt->info.fevsReq.sender_count;

		immModel_setAdmReqContinuation(cb, saInv, conn);
	}

	if (cl_node && sinfo && !asyncReq) {
		cl_node->tmpSinfo =
		    *sinfo; // TODO: should be part of continuation?
	}

	/* Only a single continuation per client
	   possible, but not when op is async!
	   This is where the ND sender count is needed.
	   But we should be able to use the agents
	   count/continuation-id .. ? */

	if (cl_node && cl_node->mIsSync) {
		osafassert(!cl_node->mSyncBlocked);
		if (!asyncReq) {
			cl_node->mSyncBlocked = true;
		}
	}

	return rc; /*Normal return here => asyncronous reply in fevs_rcv. */

agent_rsp:

	send_evt.type = IMMSV_EVT_TYPE_IMMA;
	send_evt.info.imma.type = IMMA_EVT_ND2A_IMM_ERROR;
	send_evt.info.imma.info.errRsp.error = error;

	osafassert(!asyncReq);

	TRACE_2("SENDRSP FAIL %u ", send_evt.info.imma.info.admInitRsp.error);
	rc = immnd_mds_send_rsp(cb, sinfo, &send_evt);

	return rc;
}

/*
  Function for performing immnd local checks on fevs packed messages.
  Normally they pass the checks and are forwarded to the IMMD.
  The intention is to prevent a bogus message from a bad client to
  propagate to all IMMNDs. The intention is also to capture any IMMND
  local handling that can or should be done before forwarding.

  For example, saImmOiClassImplementerSet and saImmOiObjectImplementerSet
  are not allowed during imm-sync. But the idempotent case of trying to
  set class/object implementer in a way that is already set should be
  allowed. Such an idempotency case must be resolved locally at veteran
  nodes and not propagated over fevs, because sync clients may not yet
  have synced the implementer setting and thus reject the idempotent case.

  sinfo - only valid for synchronous calls, can be NULL

*/
static SaAisErrorT immnd_fevs_local_checks(IMMND_CB *cb, IMMSV_FEVS *fevsReq,
					   const IMMSV_SEND_INFO *sinfo)
{
	SaAisErrorT error = SA_AIS_OK;
	osafassert(fevsReq);
	IMMSV_OCTET_STRING *msg = &fevsReq->msg;
	SaImmHandleT clnt_hdl = fevsReq->client_hdl;
	NCS_NODE_ID nodeId = m_IMMSV_UNPACK_HANDLE_LOW(clnt_hdl);
	SaUint32T conn = m_IMMSV_UNPACK_HANDLE_HIGH(clnt_hdl);
	IMMND_IMM_CLIENT_NODE *cl_node = NULL;
	bool isLoading =
	    (cb->mState == IMM_SERVER_LOADING_SERVER) && immModel_getLoader(cb);
	NCS_UBAID uba;
	TRACE_ENTER();
	uba.start = NULL;
	IMMSV_EVT frwrd_evt;
	memset(&frwrd_evt, '\0', sizeof(IMMSV_EVT));

	immnd_client_node_get(cb, clnt_hdl, &cl_node);
	if (cl_node == NULL || cl_node->mIsStale) {
		error = SA_AIS_ERR_BAD_HANDLE;
		goto client_down;
	}

	/*Unpack the embedded message */
	if (ncs_enc_init_space_pp(&uba, 0, 0) != NCSCC_RC_SUCCESS) {
		LOG_ER("Failed init ubaid");
		error = SA_AIS_ERR_NO_RESOURCES;
		goto unpack_failure;
	}

	if (ncs_encode_n_octets_in_uba(&uba, (uint8_t *)msg->buf, msg->size) !=
	    NCSCC_RC_SUCCESS) {
		LOG_ER("Failed buffer copy");
		error = SA_AIS_ERR_NO_RESOURCES;
		goto unpack_failure;
	}

	ncs_dec_init_space(&uba, uba.start);
	uba.bufp = NULL;

	/* Decode non flat. */
	if (immsv_evt_dec(&uba, &frwrd_evt) != NCSCC_RC_SUCCESS) {
		LOG_ER("Edu decode Failed");
		error = SA_AIS_ERR_LIBRARY;
		goto unpack_failure;
	}

	if (frwrd_evt.type != IMMSV_EVT_TYPE_IMMND) {
		LOG_ER("IMMND - Wrong Event Type: %u", frwrd_evt.type);
		error = SA_AIS_ERR_LIBRARY;
		goto unpack_failure;
	}

	switch (frwrd_evt.info.immnd.type) {

	case IMMND_EVT_A2ND_OBJ_MODIFY:
		if ((sinfo != NULL) &&
		    ((strcmp(frwrd_evt.info.immnd.info.objModify.objectName.buf,
			     OPENSAF_IMM_OBJECT_DN) == 0) ||
		     (strcmp(frwrd_evt.info.immnd.info.objModify.objectName.buf,
			     "safRdn=immManagement,safApp=safImmService") ==
		      0))) {
			/* Modifications to IMM service objects are only allowed
			 * for root users and same group as me. Except for
			 * access control settings which are only allowed by
			 * root.
			 */
			if ((sinfo->uid > 0) && (sinfo->gid != getgid())) {
				struct passwd *pwd = getpwuid(sinfo->uid);
				if (pwd != NULL) {
					syslog(
					    LOG_AUTH,
					    "Modifications to imm service objects denied for %s(uid=%d)",
					    pwd->pw_name, sinfo->uid);
				}
				error = SA_AIS_ERR_ACCESS_DENIED;
				break; /* out of switch */
			} else if (sinfo->uid > 0) {
				// non root and same group as me, disallow
				// access control changes
				bool ac_failed = false;
				const IMMSV_ATTR_MODS_LIST *attrMod =
				    frwrd_evt.info.immnd.info.objModify
					.attrMods;
				while (attrMod != NULL) {
					if ((strcmp(
						 attrMod->attrValue.attrName
						     .buf,
						 OPENSAF_IMM_ACCESS_CONTROL_MODE) ==
					     0) ||
					    (strcmp(
						 attrMod->attrValue.attrName
						     .buf,
						 OPENSAF_IMM_AUTHORIZED_GROUP) ==
					     0)) {
						struct passwd *pwd =
						    getpwuid(sinfo->uid);
						if (pwd != NULL)
							syslog(
							    LOG_AUTH,
							    "change of %s denied for %s(uid=%d)",
							    attrMod->attrValue
								.attrName.buf,
							    pwd->pw_name,
							    sinfo->uid);
						error =
						    SA_AIS_ERR_ACCESS_DENIED;
						ac_failed = true;
						break; /* out of while */
					}
					attrMod = attrMod->next;
				}
				if (ac_failed) {
					break;
				} /* out of switch */
			} else
				; // modifications by root are OK
		}
	/* intentional fall through. */
	case IMMND_EVT_A2ND_OBJ_CREATE:
	case IMMND_EVT_A2ND_OBJ_DELETE:
	case IMMND_EVT_A2ND_CCB_FINALIZE:
	case IMMND_EVT_A2ND_OI_CCB_AUG_INIT:
	case IMMND_EVT_A2ND_AUG_ADMO:
		if (immModel_pbeNotWritable(cb)) {
			error = SA_AIS_ERR_TRY_AGAIN;
		}
		break;

	case IMMND_EVT_A2ND_OBJ_SAFE_READ:
		TRACE(
		    "IMMND_EVT_A2ND_OBJ_SAFE_READ noted in fevs_local_checks");
		if (!immModel_protocol50Allowed(cb) ||
		    immModel_pbeNotWritable(cb)) {
			error = SA_AIS_ERR_TRY_AGAIN;
		}
		break;

	case IMMND_EVT_A2ND_OBJ_CREATE_2:
		if (!immModel_protocol46Allowed(cb) ||
		    immModel_pbeNotWritable(cb)) {
			error = SA_AIS_ERR_TRY_AGAIN;
		}
		break;

	case IMMND_EVT_A2ND_CCB_VALIDATE:
		if (!immModel_protocol45Allowed(cb)) {
			LOG_NO(
			    "saImmOmCcbValidate rejected during upgrade to 4.5 (OPENSAF_IMM_FLAG_PRT45_ALLOW is false)");
			error = SA_AIS_ERR_NO_RESOURCES;
			break;
		}
	/* intentional fallthrough. */
	case IMMND_EVT_A2ND_CCB_APPLY:
		if (cb->mPbeDisableCritical) {
			LOG_WA(
			    "ERR_TRY_AGAIN: Disable of PBE has been initiated, waiting for the reply from the PBE");
			error = SA_AIS_ERR_TRY_AGAIN;
			break;
		}
		if (immModel_pbeNotWritable(cb) ||
		    (cb->fevs_replies_pending >=
		     IMMSV_DEFAULT_FEVS_MAX_PENDING) ||
		    !immnd_is_immd_up(cb)) {
			/* NO_RESOURCES is here imm internal proxy for
			   TRY_AGAIN. The library code for saImmOmCcbApply will
			   translate NO_RESOURCES to TRY_AGAIN towards the user.
			   That library code (for ccbApply) treats TRY_AGAIN
			   from IMMND as an indication of handle resurrect. So
			   we need to take this detour to really communicate
			   TRY_AGAIN towards that particular library code.
			 */
			error = SA_AIS_ERR_NO_RESOURCES;
			if (cb->fevs_replies_pending >=
			    IMMSV_DEFAULT_FEVS_MAX_PENDING) {
				TRACE_2(
				    "ERR_TRY_AGAIN: Too many pending FEVS message replies (> %u) rejecting request"
				    "for CcbApply",
				    IMMSV_DEFAULT_FEVS_MAX_PENDING);
			}
		}
		break;

	case IMMND_EVT_A2ND_OBJ_SYNC:
	case IMMND_EVT_A2ND_OBJ_SYNC_2:
		if (fevsReq->sender_count != 0x0) {
			LOG_WA(
			    "ERR_LIBRARY: IMMND_EVT_A2ND_OBJ_SYNC fevsReq->sender_count != 0x0");
			error = SA_AIS_ERR_LIBRARY;
		}

		if (!(cl_node->mIsSync)) {
			LOG_WA(
			    "ERR_LIBRARY: IMMND_EVT_A2ND_OBJ_SYNC can only arrive from sync client");
			error = SA_AIS_ERR_LIBRARY;
		}
		break;

	case IMMND_EVT_A2ND_IMM_ADMOP:
	case IMMND_EVT_A2ND_IMM_ADMOP_ASYNC:
		/* No restrictions at cluster level. */
		break;

	case IMMND_EVT_A2ND_CLASS_CREATE:
		if (fevsReq->sender_count != 0x1) {
			LOG_WA(
			    "ERR_LIBRARY: IMMND_EVT_A2ND_CLASS_CREATE fevsReq->sender_count != 0x1");
			error = SA_AIS_ERR_LIBRARY;
		} else if (!(cl_node->mIsSync) &&
			   (immModel_immNotWritable(cb) ||
			    cb->mSyncFinalizing)) {
			/* Regular class create attempted during sync. */
			error = SA_AIS_ERR_TRY_AGAIN;
		}
		if (!immModel_protocol47Allowed(cb) &&
		    !immModel_readyForLoading(cb)) {
			/* IMM supports creating classes with unknown flags.
			 * When the upgrade process is not completed, a
			 * class-create request (with DEFAULT_REMOVED flag) may
			 * be accepted on nodes with old version and rejected on
			 * nodes with new version. This check will prevent the
			 * inconsistency between nodes. */
			IMMSV_ATTR_DEF_LIST *list =
			    frwrd_evt.info.immnd.info.classDescr
				.attrDefinitions;
			while (list) {
				if (list->d.attrFlags &
				    SA_IMM_ATTR_DEFAULT_REMOVED) {
					LOG_WA(
					    "ERR_TRY_AGAIN: Can not create class with SA_IMM_ATTR_DEFAULT_REMOVED "
					    "when proto47 is not enabled");
					error = SA_AIS_ERR_TRY_AGAIN;
					break; /* while */
				}
				list = list->next;
			}
		}
		if (!immModel_protocol50Allowed(cb)) {
			IMMSV_ATTR_DEF_LIST *list =
			    frwrd_evt.info.immnd.info.classDescr
				.attrDefinitions;
			while (list) {
				if (list->d.attrFlags &
				    SA_IMM_ATTR_STRONG_DEFAULT) {
					LOG_WA(
					    "ERR_TRY_AGAIN: Can not create class with SA_IMM_ATTR_STRONG_DEFAULT "
					    "when proto50 is not enabled");
					error = SA_AIS_ERR_TRY_AGAIN;
					break; /* while */
				}
				list = list->next;
			}
		}

		break;

	case IMMND_EVT_A2ND_CLASS_DELETE:
		if (fevsReq->sender_count != 0x1) {
			LOG_WA(
			    "ERR_LIBRARY: IMMND_EVT_A2ND_CLASS_DELETE fevsReq->sender_count != 0x1");
			error = SA_AIS_ERR_LIBRARY;
		} else if (immModel_immNotWritable(cb) || cb->mSyncFinalizing) {
			error = SA_AIS_ERR_TRY_AGAIN;
		}

		break;

	case IMMND_EVT_D2ND_DISCARD_IMPL:
		LOG_WA(
		    "ERR_LIBRARY: IMMND_EVT_D2ND_DISCARD_IMPL can not arrive from client lib");
		error = SA_AIS_ERR_LIBRARY;
		break;

	case IMMND_EVT_D2ND_DISCARD_NODE:
		LOG_WA(
		    "ERR_LIBRARY: IMMND_EVT_D2ND_DISCARD_NODE can not arrive from client lib");
		error = SA_AIS_ERR_LIBRARY;
		break;

	case IMMND_EVT_D2ND_ADMINIT:
		LOG_WA(
		    "ERR_LIBRARY: IMMND_EVT_D2ND_ADMINIT can not arrive from client lib");
		error = SA_AIS_ERR_LIBRARY;
		break;

	case IMMND_EVT_D2ND_IMPLSET_RSP:
		LOG_WA(
		    "ERR_LIBRARY: IMMND_EVT_D2ND_IMPLSET_RSP can not arrive from client lib");
		error = SA_AIS_ERR_LIBRARY;
		break;

	case IMMND_EVT_D2ND_IMPLSET_RSP_2:
		LOG_WA(
		    "ERR_LIBRARY: IMMND_EVT_D2ND_IMPLSET_RSP_2 can not arrive from client lib");
		error = SA_AIS_ERR_LIBRARY;
		break;

	case IMMND_EVT_D2ND_CCBINIT:
		LOG_WA(
		    "ERR_LIBRARY: IMMND_EVT_D2ND_CCBINIT can not arrive from client lib");
		error = SA_AIS_ERR_LIBRARY;
		break;

	case IMMND_EVT_D2ND_ABORT_CCB:
		LOG_WA(
		    "ERR_LIBRARY: IMMND_EVT_D2ND_ABORT_CCB can not arrive from client lib");
		error = SA_AIS_ERR_LIBRARY;
		break;

	case IMMND_EVT_A2ND_OI_CL_IMPL_SET:

		if (fevsReq->sender_count != 0x1) {
			LOG_WA(
			    "ERR_LIBRARY: IMMND_EVT_A2ND_OI_CL_IMPL_SET fevsReq->sender_count != 0x1");
			error = SA_AIS_ERR_LIBRARY;
		} else if (immModel_immNotWritable(cb)) { // sync is on-going
			SaUint32T ccbId = 0;
			error = immModel_classImplementerSet(
			    cb, &(frwrd_evt.info.immnd.info.implSet), conn,
			    nodeId, &ccbId);
			osafassert(error != SA_AIS_OK); /* OK is impossible
							   since we are not
							   writable */
			if (error == SA_AIS_ERR_NO_BINDINGS) {
				TRACE(
				    "idempotent classImplementerSet locally detected, impl_id:%u",
				    frwrd_evt.info.immnd.info.implSet.impl_id);
				osafassert(!ccbId);
			} else if (error == SA_AIS_ERR_TRY_AGAIN && ccbId) {
				/* Yes there can be an active non critical ccb
				   even though imsmv is not writable. A sync
				   could be just starting and still waiting for
				   ccbs to terminate (period of grace). Here we
				   turn ungracefull on this ccb to give
				   class-implementer-set priority over the ccb
				   doing un-protected (un-validated) changes to
				   instances of the class.
				*/
				immnd_proc_global_abort_ccb(cb, ccbId);
			}
		} else if (cb->mSyncFinalizing) {
			// Writable, but sync is finalizing at coord.
			error = SA_AIS_ERR_TRY_AGAIN;
		}
		/* Else when imm is writable, idempotency can not be trusted
		   locally. OK will be returned, which will trigger forwarding
		   over fevs. */

		break;

	case IMMND_EVT_A2ND_OI_OBJ_IMPL_SET:

		if (fevsReq->sender_count != 0x1) {
			LOG_WA(
			    "ERR_LIBRARY: IMMND_EVT_A2ND_OI_OBJ_IMPL_SET fevsReq->sender_count != 0x1");
			error = SA_AIS_ERR_LIBRARY;
		} else if (immModel_immNotWritable(cb)) { // sync is on-going
			SaUint32T ccbId = 0;
			error = immModel_objectImplementerSet(
			    cb, &(frwrd_evt.info.immnd.info.implSet), conn,
			    nodeId, &ccbId);
			osafassert(error != SA_AIS_OK); /* OK is impossible
							   since we are not
							   writable */
			if (error == SA_AIS_ERR_NO_BINDINGS) {
				TRACE(
				    "idempotent objectImplementerSet locally detected, impl_id:%u",
				    frwrd_evt.info.immnd.info.implSet.impl_id);
				osafassert(!ccbId);
			} else if (error == SA_AIS_ERR_TRY_AGAIN && ccbId) {
				/* Yes there can be an active non critical ccb
				   even though imsmv is not writable. A sync
				   could be just starting and still waiting for
				   ccbs to terminate (period of grace). Here we
				   turn ungracefull on this ccb to give
				   object-implementer-set priority over the ccb
				   doing un-protected (un-validated) changes to
				   this object.
				*/
				immnd_proc_global_abort_ccb(cb, ccbId);
			}
		} else if (cb->mSyncFinalizing) {
			// Writable, but sync is finalizing at coord.
			error = SA_AIS_ERR_TRY_AGAIN;
		}
		/* Else when imm is writable, idempotency can not be trusted
		   locally. OK will be returned, which will trigger forwarding
		   over fevs. */
		break;

	case IMMND_EVT_A2ND_OI_IMPL_CLR:
		if (fevsReq->sender_count != 0x0) {
			LOG_WA(
			    "ERR_LIBRARY: IMMND_EVT_A2ND_OI_IMPL_CLR fevsReq->sender_count != 0x0");
			error = SA_AIS_ERR_LIBRARY;
		}
		break;

	case IMMND_EVT_A2ND_OI_CL_IMPL_REL:
		if (fevsReq->sender_count != 0x1) {
			LOG_WA(
			    "ERR_LIBRARY: IMMND_EVT_A2ND_OI_CL_IMPL_REL fevsReq->sender_count != 0x1");
			error = SA_AIS_ERR_LIBRARY;
		}
		break;

	case IMMND_EVT_A2ND_OI_OBJ_IMPL_REL:
		if (fevsReq->sender_count != 0x1) {
			LOG_WA(
			    "ERR_LIBRARY: IMMND_EVT_A2ND_OI_OBJ_IMPL_REL fevsReq->sender_count != 0x1");
			error = SA_AIS_ERR_LIBRARY;
		}
		break;

	case IMMND_EVT_A2ND_OI_OBJ_CREATE:
		if (fevsReq->sender_count != 0x1) {
			LOG_WA(
			    "ERR_LIBRARY: IMMND_EVT_A2ND_OI_OBJ_CREATE fevsReq->sender_count != 0x1");
			error = SA_AIS_ERR_LIBRARY;
		}
		break;

	case IMMND_EVT_A2ND_OI_OBJ_CREATE_2:
		if (!immModel_protocol46Allowed(cb)) {
			error = SA_AIS_ERR_TRY_AGAIN;
		} else if (fevsReq->sender_count != 0x1) {
			LOG_WA(
			    "ERR_LIBRARY: IMMND_EVT_A2ND_OI_OBJ_CREATE_2 fevsReq->sender_count != 0x1");
			error = SA_AIS_ERR_LIBRARY;
		}
		break;

	case IMMND_EVT_A2ND_OI_OBJ_MODIFY:
		/* rtObjectUpdate is not encoded by imma client as fevs. */
		LOG_WA(
		    "ERR_LIBRARY: IMMND_EVT_A2ND_OI_OBJ_MODIFY can not arrive from client as fevs");
		error = SA_AIS_ERR_LIBRARY;
		break;

	case IMMND_EVT_A2ND_OI_OBJ_DELETE:
		if (fevsReq->sender_count != 0x1) {
			LOG_WA(
			    "ERR_LIBRARY: IMMND_EVT_A2ND_OI_OBJ_DELETE fevsReq->sender_count != 0x1");
			error = SA_AIS_ERR_LIBRARY;
		}
		break;

	case IMMND_EVT_D2ND_ADMO_HARD_FINALIZE:
		LOG_WA(
		    "ERR_LIBRARY: IMMND_EVT_D2ND_ADMO_HARD_FIMNALIZE can not arrive from client lib");
		error = SA_AIS_ERR_LIBRARY;
		break;

	case IMMND_EVT_A2ND_ADMO_CLEAR:
		if ((immModel_accessControlMode(cb) ==
		     ACCESS_CONTROL_ENFORCING)) {
			/*
			  The om API downcall 'saImmOmAdminOwnerClear(...)' is
			  special in that it forces the removal of
			  adminownership set up by some other user/handle. It is
			  only needed to 'clean up' after an application has
			  terminated without releaseing admin-owner and with
			  releaseOnFinalize set to false. Because of the very
			  special and powerful nature of this operation, only
			  root users should be allowed to use it, when acces
			  control is enabled.
			*/
			if ((sinfo != NULL) && (sinfo->uid > 0)) {
				struct passwd *pwd = getpwuid(sinfo->uid);
				if (pwd != NULL) {
					syslog(
					    LOG_AUTH,
					    "saImmOmAdminOwnerClear denied for %s(uid=%d)",
					    pwd->pw_name, sinfo->uid);
				}
				error = SA_AIS_ERR_ACCESS_DENIED;
				break;
			}
		}
	/* intentional fall through. */
	case IMMND_EVT_A2ND_ADMO_SET:
	case IMMND_EVT_A2ND_ADMO_RELEASE:
		if (fevsReq->sender_count != 0x1) {
			LOG_WA(
			    "ERR_LIBRARY: IMMND_EVT_A2ND_ADMO_XXX(%u) fevsReq->sender_count != 0x1",
			    frwrd_evt.info.immnd.type);
			error = SA_AIS_ERR_LIBRARY;
			break;
		}
	/* Intentional fall through. */

	case IMMND_EVT_A2ND_ADMO_FINALIZE:
		if (immModel_immNotWritable(cb) || cb->mSyncFinalizing) {
			TRACE("PRECHECK ADMO_FINALIZE->TRY_AGAIN");
			error = SA_AIS_ERR_TRY_AGAIN;
		}

		if (isLoading && (error == SA_AIS_OK)) {
			IMMSV_EVT send_evt;
			memset(&send_evt, '\0', sizeof(IMMSV_EVT));

			send_evt.type = IMMSV_EVT_TYPE_IMMD;
			send_evt.info.immd.type =
			    IMMD_EVT_ND2D_LOADING_COMPLETED;
			send_evt.info.immd.info.ctrl_msg.ndExecPid = cb->mMyPid;
			send_evt.info.immd.info.ctrl_msg.epoch = cb->mMyEpoch;
			send_evt.info.immd.info.ctrl_msg
			    .pbeEnabled = /*see immsv_d2nd_control in
					     immsv_ev.h*/
			    (cb->mPbeFile)
				? ((cb->mRim == SA_IMM_KEEP_REPOSITORY) ? 4 : 3)
				: 2;

			error =
			    immnd_mds_msg_send(cb, NCSMDS_SVC_ID_IMMD,
					       cb->immd_mdest_id, &send_evt);

			if (error != NCSCC_RC_SUCCESS) {
				LOG_ER(
				    "Coord failed to send 'loading completed' message to IMMD");
				error = SA_AIS_ERR_LIBRARY;
			}
		}

		break;

	case IMMND_EVT_ND2ND_SYNC_FINALIZE:
		if (!(cl_node->mIsSync)) {
			LOG_WA(
			    "ERR_LIBRARY: IMMND_EVT_ND2ND_SYNC_FINALIZE can only arrive from sync client");
			error = SA_AIS_ERR_LIBRARY;
		}
		break;

	case IMMND_EVT_ND2ND_SYNC_FINALIZE_2:
		if (!(cl_node->mIsSync)) {
			LOG_WA(
			    "ERR_LIBRARY: IMMND_EVT_ND2ND_SYNC_FINALIZE_2 can not arrive from client lib");
			error = SA_AIS_ERR_LIBRARY;
		}
		break;

	case IMMND_EVT_A2ND_CCB_COMPLETED_RSP:
	case IMMND_EVT_A2ND_CCB_COMPLETED_RSP_2:
	case IMMND_EVT_A2ND_PBE_PRTO_DELETES_COMPLETED_RSP:
	case IMMND_EVT_A2ND_CCB_OBJ_CREATE_RSP:
	case IMMND_EVT_A2ND_CCB_OBJ_CREATE_RSP_2:
	case IMMND_EVT_A2ND_PBE_PRT_OBJ_CREATE_RSP:
	case IMMND_EVT_A2ND_CCB_OBJ_MODIFY_RSP:
	case IMMND_EVT_A2ND_CCB_OBJ_MODIFY_RSP_2:
	case IMMND_EVT_A2ND_PBE_PRT_ATTR_UPDATE_RSP:
	case IMMND_EVT_A2ND_CCB_OBJ_DELETE_RSP:
	case IMMND_EVT_A2ND_CCB_OBJ_DELETE_RSP_2:
		if (fevsReq->sender_count != 0x0) {
			LOG_WA(
			    "ERR_LIBRARY:  fevsReq->sender_count != 0x0 for OI response type:%u",
			    frwrd_evt.info.immnd.type);
			error = SA_AIS_ERR_LIBRARY;
		}
		break;

	case IMMND_EVT_A2ND_PBE_ADMOP_RSP:
		if (fevsReq->sender_count != 0x0) {
			LOG_WA(
			    "ERR_LIBRARY: IMMND_EVT_A2ND_PBE_ADMOP_RSP fevsReq->sender_count != 0x0");
			error = SA_AIS_ERR_LIBRARY;
		}
		break;

	case IMMND_EVT_D2ND_SYNC_FEVS_BASE:
		LOG_WA(
		    "ERR_LIBRARY: IMMND_EVT_D2ND_SYNC_FEVS_BASE can not arrive from client lib");
		error = SA_AIS_ERR_LIBRARY;
		break;

	default:
		LOG_ER(
		    "UNPACK FAILURE, unrecognized message type: %u caught in fevs_local_checks",
		    frwrd_evt.info.immnd.type);
		error = SA_AIS_ERR_LIBRARY;
		break;
	}

unpack_failure:

	if (uba.start) {
		m_MMGR_FREE_BUFR_LIST(uba.start);
	}

	if ((error != SA_AIS_OK) && (error != SA_AIS_ERR_NO_BINDINGS) &&
	    (error != SA_AIS_ERR_TRY_AGAIN)) {
		LOG_NO(
		    "Precheck of fevs message of type <%u> failed with ERROR:%u",
		    frwrd_evt.info.immnd.type, error);
	}

	immnd_evt_destroy(&frwrd_evt, false, __LINE__);

client_down:

	TRACE_LEAVE();
	return error;
}

/****************************************************************************
 * Name          : immnd_evt_proc_ccb_obj_modify_rsp
 *
 * Description   : Function to process the reply from the
 *                 SaImmOiCcbObjectModifyCallbackT upcall.
 *                 Note this call arrived over fevs, to ALL IMMNDs.
 *                 This is because ALL must receive ACK/NACK from EACH
 *                 implementer.
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *
 * Return Values : None.
 *
 *****************************************************************************/
static void immnd_evt_proc_ccb_obj_modify_rsp(IMMND_CB *cb, IMMND_EVT *evt,
					      bool originatedAtThisNd,
					      SaImmHandleT clnt_hdl,
					      MDS_DEST reply_dest)
{
	uint32_t rc = NCSCC_RC_SUCCESS;
	IMMSV_EVT send_evt;
	SaUint32T reqConn = 0;
	IMMND_IMM_CLIENT_NODE *cl_node = NULL;
	TRACE_ENTER();

	immModel_ccbObjModifyContinuation(
	    cb, evt->info.ccbUpcallRsp.ccbId, evt->info.ccbUpcallRsp.inv,
	    evt->info.ccbUpcallRsp.result, &reqConn);

	if (reqConn) {
		SaImmHandleT tmp_hdl =
		    m_IMMSV_PACK_HANDLE(reqConn, cb->node_id);

		immnd_client_node_get(cb, tmp_hdl, &cl_node);
		if (cl_node == NULL || cl_node->mIsStale) {
			LOG_WA("IMMND - Client went down so no response");
			TRACE_LEAVE();
			return; /*Note, this means that regardles of ccb
				   outcome, we can not reply to the process that
				   started the ccb. */
		}

		TRACE_2("SENDRSP %u", evt->info.ccbUpcallRsp.result);

		memset(&send_evt, '\0', sizeof(IMMSV_EVT));
		send_evt.type = IMMSV_EVT_TYPE_IMMA;
		send_evt.info.imma.type = IMMA_EVT_ND2A_IMM_ERROR;
		IMMSV_ATTR_NAME_LIST strList;
		IMMSV_ATTR_NAME_LIST *errStrList = immModel_ccbGrabErrStrings(
		    cb, evt->info.ccbUpcallRsp.ccbId);

		if (evt->info.ccbUpcallRsp.result != SA_AIS_OK) {
			evt->info.ccbUpcallRsp.result =
			    SA_AIS_ERR_FAILED_OPERATION;

			if (evt->info.ccbUpcallRsp.errorString.size) {
				osafassert(evt->type ==
					   IMMND_EVT_A2ND_CCB_OBJ_MODIFY_RSP_2);

				strList.next = errStrList;
				strList.name = evt->info.ccbUpcallRsp
						   .errorString; /*borrow*/
				send_evt.info.imma.info.errRsp.errStrings =
				    &(strList);
				send_evt.info.imma.type =
				    IMMA_EVT_ND2A_IMM_ERROR_2;
			} else if (errStrList) {
				osafassert(evt->type ==
					   IMMND_EVT_A2ND_CCB_OBJ_MODIFY_RSP);
				send_evt.info.imma.info.errRsp.errStrings =
				    errStrList;
				send_evt.info.imma.type =
				    IMMA_EVT_ND2A_IMM_ERROR_2;
			}
		}

		/* Dont move this line up. Error return code may have been
		 * adjusted above.*/
		send_evt.info.imma.info.errRsp.error =
		    evt->info.ccbUpcallRsp.result;

		rc = immnd_mds_send_rsp(cb, &(cl_node->tmpSinfo), &send_evt);
		if (rc != NCSCC_RC_SUCCESS) {
			LOG_WA(
			    "Failed to send response to agent/client over MDS rc:%u",
			    rc);
		}

		immsv_evt_free_attrNames(errStrList);
	}

	TRACE_LEAVE();
}

/****************************************************************************
 * Name          : immnd_evt_proc_ccb_obj_create_rsp
 *
 * Description   : Function to process the reply from the
 *                 SaImmOiCcbObjectCreateCallbackT upcall.
 *                 Note this call arrived over fevs, to ALL IMMNDs.
 *                 This is because ALL must receive ACK/NACK from EACH
 *                 implementer.
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *
 * Return Values : None.
 *
 *****************************************************************************/
static void immnd_evt_proc_ccb_obj_create_rsp(IMMND_CB *cb, IMMND_EVT *evt,
					      bool originatedAtThisNd,
					      SaImmHandleT clnt_hdl,
					      MDS_DEST reply_dest)
{
	uint32_t rc = NCSCC_RC_SUCCESS;
	IMMSV_EVT send_evt;
	SaUint32T reqConn = 0;
	IMMND_IMM_CLIENT_NODE *cl_node = NULL;
	TRACE_ENTER();

	immModel_ccbObjCreateContinuation(
	    cb, evt->info.ccbUpcallRsp.ccbId, evt->info.ccbUpcallRsp.inv,
	    evt->info.ccbUpcallRsp.result, &reqConn);

	if (reqConn) {
		SaImmHandleT tmp_hdl =
		    m_IMMSV_PACK_HANDLE(reqConn, cb->node_id);

		immnd_client_node_get(cb, tmp_hdl, &cl_node);
		if (cl_node == NULL || cl_node->mIsStale) {
			LOG_WA("IMMND - Client went down so no response");
			TRACE_LEAVE();
			return; /*Note, this means that regardles of ccb
				   outcome, we can not reply to the process that
				   started the ccb. */
		}

		TRACE_2("SENDRSP %u", evt->info.ccbUpcallRsp.result);

		memset(&send_evt, '\0', sizeof(IMMSV_EVT));
		send_evt.type = IMMSV_EVT_TYPE_IMMA;
		send_evt.info.imma.type = IMMA_EVT_ND2A_IMM_ERROR;
		IMMSV_ATTR_NAME_LIST strList;
		IMMSV_ATTR_NAME_LIST *errStrList = immModel_ccbGrabErrStrings(
		    cb, evt->info.ccbUpcallRsp.ccbId);

		if (evt->info.ccbUpcallRsp.result != SA_AIS_OK) {
			evt->info.ccbUpcallRsp.result =
			    SA_AIS_ERR_FAILED_OPERATION;

			if (evt->info.ccbUpcallRsp.errorString.size) {
				osafassert(evt->type ==
					   IMMND_EVT_A2ND_CCB_OBJ_CREATE_RSP_2);

				strList.next = errStrList;
				strList.name = evt->info.ccbUpcallRsp
						   .errorString; /*borrow*/
				send_evt.info.imma.info.errRsp.errStrings =
				    &(strList);
				send_evt.info.imma.type =
				    IMMA_EVT_ND2A_IMM_ERROR_2;
			} else if (errStrList) {
				osafassert(evt->type ==
					   IMMND_EVT_A2ND_CCB_OBJ_CREATE_RSP);
				send_evt.info.imma.info.errRsp.errStrings =
				    errStrList;
				send_evt.info.imma.type =
				    IMMA_EVT_ND2A_IMM_ERROR_2;
			}
		}

		/* Dont move this line up. Error return code may have been
		 * adjusted above.*/
		send_evt.info.imma.info.errRsp.error =
		    evt->info.ccbUpcallRsp.result;

		rc = immnd_mds_send_rsp(cb, &(cl_node->tmpSinfo), &send_evt);
		if (rc != NCSCC_RC_SUCCESS) {
			LOG_WA(
			    "Failed to send response to agent/client over MDS rc:%u",
			    rc);
		}

		immsv_evt_free_attrNames(errStrList);
	}

	TRACE_LEAVE();
}

/****************************************************************************
 * Name          : immnd_evt_proc_ccb_obj_delete_rsp
 *
 * Description   : Function to process the reply from the
 *                 SaImmOiCcbObjectDeleteCallbackT upcall.
 *                 Note this call arrived over fevs, to ALL IMMNDs.
 *                 This is because ALL must receive ACK/NACK from EACH
 *                 implementer.
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *
 * Return Values : None.
 *
 *****************************************************************************/
static void immnd_evt_proc_ccb_obj_delete_rsp(IMMND_CB *cb, IMMND_EVT *evt,
					      bool originatedAtThisNd,
					      SaImmHandleT clnt_hdl,
					      MDS_DEST reply_dest)
{
	uint32_t rc = NCSCC_RC_SUCCESS;
	IMMSV_EVT send_evt;
	SaUint32T reqConn = 0;
	IMMND_IMM_CLIENT_NODE *cl_node = NULL;
	bool augDelete = false;
	TRACE_ENTER();

	immModel_ccbObjDelContinuation(cb, &(evt->info.ccbUpcallRsp), &reqConn,
				       &augDelete);

	SaAisErrorT err = SA_AIS_OK;

	if (!immModel_ccbWaitForDeleteImplAck(cb, evt->info.ccbUpcallRsp.ccbId,
					      &err, augDelete) &&
	    reqConn) {
		SaImmHandleT tmp_hdl =
		    m_IMMSV_PACK_HANDLE(reqConn, cb->node_id);

		immnd_client_node_get(cb, tmp_hdl, &cl_node);
		if (cl_node == NULL || cl_node->mIsStale) {
			LOG_WA("IMMND - Client went down so no response");
			TRACE_LEAVE();
			return; /*Note, this means that regardles of ccb
				   outcome, we can not reply to the process that
				   started the ccb. */
		}

		TRACE_2("SENDRSP %u", evt->info.ccbUpcallRsp.result);

		memset(&send_evt, '\0', sizeof(IMMSV_EVT));
		send_evt.type = IMMSV_EVT_TYPE_IMMA;
		send_evt.info.imma.info.errRsp.error = err;
		send_evt.info.imma.type = IMMA_EVT_ND2A_IMM_ERROR;
		if (err != SA_AIS_OK) {
			send_evt.info.imma.info.errRsp.errStrings =
			    immModel_ccbGrabErrStrings(
				cb, evt->info.ccbUpcallRsp.ccbId);

			if (send_evt.info.imma.info.errRsp.errStrings) {
				send_evt.info.imma.type =
				    IMMA_EVT_ND2A_IMM_ERROR_2;
			}
		}

		rc = immnd_mds_send_rsp(cb, &(cl_node->tmpSinfo), &send_evt);
		if (rc != NCSCC_RC_SUCCESS) {
			LOG_WA(
			    "Failed to send response to agent/client over MDS rc:%u",
			    rc);
		}
		immsv_evt_free_attrNames(
		    send_evt.info.imma.info.errRsp.errStrings);
	}

	TRACE_LEAVE();
}

/****************************************************************************
 * Name          : immnd_evt_proc_ccb_compl_rsp
 *
 * Description   : Function to process the reply from the
 *                 SaImmOiCcbCompletedCallbackT upcall.
 *                 Note this call arrived over fevs, to ALL IMMNDs.
 *                 This is because ALL must receive ACK/NACK from EACH
 *                 implementer.
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *
 * Return Values : None.
 *
 *****************************************************************************/
static void immnd_evt_proc_ccb_compl_rsp(IMMND_CB *cb, IMMND_EVT *evt,
					 bool originatedAtThisNd,
					 SaImmHandleT clnt_hdl,
					 MDS_DEST reply_dest)
{
	uint32_t rc = NCSCC_RC_SUCCESS;
	IMMSV_EVT send_evt;
	IMMND_IMM_CLIENT_NODE *cl_node = NULL;
	IMMND_IMM_CLIENT_NODE *oi_cl_node = NULL;
	SaAisErrorT err = SA_AIS_OK;
	SaUint32T reqConn = 0;
	SaUint32T pbeConn = 0;
	NCS_NODE_ID pbeNodeId = 0;
	NCS_NODE_ID *pbeNodeIdPtr = NULL; /* Defaults to NULL for no PBE. */
	SaUint32T pbeId = 0;
	SaUint32T pbeCtn = 0;
	IMMSV_ATTR_NAME_LIST *errStrings = NULL;
	TRACE_ENTER();

	immModel_ccbCompletedContinuation(cb, &(evt->info.ccbUpcallRsp),
					  &reqConn);
	if (cb->mPbeFile && (cb->mRim == SA_IMM_KEEP_REPOSITORY)) {
		pbeNodeIdPtr = &pbeNodeId;

		/* If pbeNodeIdPtr is NULL then ccbWaitForCompletedAck skips the
		   lookup of the pbe implementer and does not make us wait for
		   it.
		 */
	}
	if (immModel_ccbWaitForCompletedAck(cb, evt->info.ccbUpcallRsp.ccbId,
					    &err, &pbeConn, pbeNodeIdPtr,
					    &pbeId, &pbeCtn)) {
		/* We still need to wait for some implementer replies, possibly
		 * also the PBE.*/
		if (pbeNodeId) {
			/* There is be a PBE. */
			osafassert(err ==
				   SA_AIS_OK); /* If not OK then we should not
						  have arrived here. */
			TRACE_5("Wait for PBE commit decision for ccb %u",
				evt->info.ccbUpcallRsp.ccbId);
			if (pbeConn) {
				TRACE_5(
				    "PBE is LOCAL - send completed upcall for %u",
				    evt->info.ccbUpcallRsp.ccbId);
				/* The PBE is connected at THIS node. Make the
				   completed upcall. If the PBE decides to
				   commit it does so immediately. It does not
				   wait for the apply upcall.
				 */
				SaImmOiHandleT implHandle =
				    m_IMMSV_PACK_HANDLE(pbeConn, pbeNodeId);
				/*Fetch client node for OI ! */
				immnd_client_node_get(cb, implHandle,
						      &oi_cl_node);
				osafassert(oi_cl_node);
				if (oi_cl_node->mIsStale) {
					LOG_WA("PBE went down");
					/* This is a bad case. The immnds have
					   just delegated the decision to commit
					   or abort this ccb to the PBE, yet it
					   has just detached. We know here that
					   we have not sent the completed call
					   to the pbe, so in principle the abort
					   should be simple. But the Ccb has
					   just entered the critical state in
					   ImmModel, so we would need to undo
					   that critical state. Problem is it
					   would have to be undone at all IMMNDs
					   which is complicated. Instead of
					   optimizing this error case with
					   complex logic and messaging, we unify
					   it with the case of having sent
					   completed to the PBE, not getting any
					   response and let ccb-recovery sort it
					   out.
					 */
					goto skip_send;
				}

				memset(&send_evt, '\0', sizeof(IMMSV_EVT));
				send_evt.type = IMMSV_EVT_TYPE_IMMA;
				send_evt.info.imma.type =
				    IMMA_EVT_ND2A_OI_CCB_COMPLETED_UC;
				send_evt.info.imma.info.ccbCompl.ccbId =
				    evt->info.ccbUpcallRsp.ccbId;
				send_evt.info.imma.info.ccbCompl.immHandle =
				    implHandle;
				send_evt.info.imma.info.ccbCompl.implId = pbeId;
				send_evt.info.imma.info.ccbCompl.invocation =
				    pbeCtn;

				TRACE_2(
				    "MAKING PBE-IMPLEMENTER CCB COMPLETED upcall");
				if (immnd_mds_msg_send(
					cb, NCSMDS_SVC_ID_IMMA_OI,
					oi_cl_node->agent_mds_dest,
					&send_evt) != NCSCC_RC_SUCCESS) {
					LOG_WA(
					    "CCB COMPLETED UPCALL SEND TO PBE FAILED");
					/* If the send to PBE fails in Mds then
					   we *dont* know for sure that the
					   message did not reach the PBE.
					   Therefore act as if send succceeded
					   and let ccb-recovery sort it out.
					*/
				} else {
					TRACE_5(
					    "IMMND UPCALL TO PBE for ccb %u, SEND SUCCEEDED",
					    evt->info.ccbUpcallRsp.ccbId);
				}
			}
			if (cb->mPbeDisableCcbId ==
			    evt->info.ccbUpcallRsp.ccbId) {
				TRACE(
				    " Disable of PBE is sent to PBE for ACK, in ccb:%u",
				    evt->info.ccbId);
				cb->mPbeDisableCritical = true;
			}
		skip_send:
			reqConn =
			    0; /* Ensure we dont reply to OM client yet. */
		}
	} else {
		bool validateOnly = false;
		if (err == SA_AIS_OK) { /*Proceed with commit */
			TRACE(
			    "Finished waiting for completed Acks from implementers (and PBE)");
			/*If we arrive here, the assumption is that all
			   implementors have agreed to commit and all immnds are
			   prepared to commit this ccb. Fevs must guarantee that
			   all have seen the same replies from implementers. If
			   there is a PBE then it has also decided to commit.
			   Appliers now receive completed upcall (any replies
			   from appliers are ignored). Then we do the internal
			   commit, then send the apply to implementers then the
			   apply to appliers.
			*/
			SaUint32T applCtn = 0;
			SaUint32T *applConnArr = NULL;
			SaUint32T applArrSize = immModel_getLocalAppliersForCcb(
			    cb, evt->info.ccbUpcallRsp.ccbId, &applConnArr,
			    &applCtn);

			if (applArrSize) {
				memset(&send_evt, '\0', sizeof(IMMSV_EVT));
				send_evt.type = IMMSV_EVT_TYPE_IMMA;
				send_evt.info.imma.type =
				    IMMA_EVT_ND2A_OI_CCB_COMPLETED_UC;
				send_evt.info.imma.info.ccbCompl.ccbId =
				    evt->info.ccbUpcallRsp.ccbId;
				send_evt.info.imma.info.ccbCompl.implId = 0;
				send_evt.info.imma.info.ccbCompl.invocation =
				    applCtn;
				int ix = 0;
				for (; ix < applArrSize && err == SA_AIS_OK;
				     ++ix) {
					SaImmOiHandleT implHandle =
					    m_IMMSV_PACK_HANDLE(applConnArr[ix],
								cb->node_id);
					send_evt.info.imma.info.ccbCompl
					    .immHandle = implHandle;

					/*Fetch client node for Applier OI ! */
					immnd_client_node_get(cb, implHandle,
							      &oi_cl_node);
					osafassert(oi_cl_node != NULL);
					if (oi_cl_node->mIsStale) {
						LOG_WA(
						    "Applier client went down so completed upcall not sent");
						continue;
					} else if (immnd_mds_msg_send(
						       cb,
						       NCSMDS_SVC_ID_IMMA_OI,
						       oi_cl_node
							   ->agent_mds_dest,
						       &send_evt) !=
						   NCSCC_RC_SUCCESS) {
						LOG_WA(
						    "Completed upcall for applier failed");
					}
				}
			}

			TRACE_2("DELAYED COMMIT TAKING EFFECT");
			SaUint32T *implConnArr = NULL;
			SaUint32T arrSize = 0;

			if (immModel_ccbCommit(cb, evt->info.ccbUpcallRsp.ccbId,
					       &arrSize, &implConnArr)) {
				osafassert(cb->mPbeDisableCcbId ==
					   evt->info.ccbUpcallRsp.ccbId);
				cb->mPbeDisableCcbId = 0;
				cb->mPbeDisableCritical = false;
				SaImmRepositoryInitModeT oldRim = cb->mRim;
				cb->mRim = immModel_getRepositoryInitMode(cb);
				if (oldRim != cb->mRim) {
					/* Reset mPbeVeteran to force
					   re-generation of db-file when mRim is
					   changed again. We can not continue
					   using any existing db-file because we
					   would get a gap in the persistent
					   history.
					 */
					cb->mPbeVeteran = false;
					cb->mPbeVeteranB = false;

					TRACE_2(
					    "Repository init mode changed to: %s",
					    (cb->mRim == SA_IMM_INIT_FROM_FILE)
						? "INIT_FROM_FILE"
						: "KEEP_REPOSITORY");
					if (cb->mIsCoord && cb->mPbeFile &&
					    (cb->mRim ==
					     SA_IMM_INIT_FROM_FILE)) {
						cb->mBlockPbeEnable = 0x1;
						/* Prevent PBE re-enable until
						 * current PBE has terminated.
						 */
						immnd_announceDump(cb);
						/* Communicates RIM to IMMD.
						   Needed to decide if reload
						   must cause cluster restart.
						*/
					}
				}
			}

			if (arrSize) {
				int ix;
				memset(&send_evt, '\0', sizeof(IMMSV_EVT));
				send_evt.type = IMMSV_EVT_TYPE_IMMA;
				send_evt.info.imma.type =
				    IMMA_EVT_ND2A_OI_CCB_APPLY_UC;

				for (ix = 0; ix < arrSize && err == SA_AIS_OK;
				     ++ix) {
					/* Send apply callbacks for all
					   implementers at this node involved
					   with the ccb. Re-use evt */
					/*Look up the client node for the
					 * implementer, using implConn */
					SaImmHandleT tmp_hdl =
					    m_IMMSV_PACK_HANDLE(implConnArr[ix],
								cb->node_id);

					/*Fetch client node for OI ! */
					immnd_client_node_get(cb, tmp_hdl,
							      &oi_cl_node);
					if (oi_cl_node == NULL ||
					    oi_cl_node->mIsStale) {
						/*Implementer client died ? */
						LOG_WA(
						    "IMMND - Client went down so can not send apply - ignoring!");
					} else {
						/*GENERATE apply message to
						 * implementer. */
						send_evt.info.imma.info.ccbCompl
						    .ccbId =
						    evt->info.ccbUpcallRsp
							.ccbId;
						send_evt.info.imma.info.ccbCompl
						    .implId =
						    evt->info.ccbUpcallRsp
							.implId;
						send_evt.info.imma.info.ccbCompl
						    .invocation =
						    evt->info.ccbUpcallRsp.inv;
						send_evt.info.imma.info.ccbCompl
						    .immHandle = tmp_hdl;
						TRACE_2(
						    "MAKING IMPLEMENTER CCB APPLY upcall");
						if (immnd_mds_msg_send(
							cb,
							NCSMDS_SVC_ID_IMMA_OI,
							oi_cl_node
							    ->agent_mds_dest,
							&send_evt) !=
						    NCSCC_RC_SUCCESS) {
							LOG_WA(
							    "IMMND CCB APPLY: CCB APPLY UPCALL FAILED - ignoring.");
						}
					}
				} // for
				free(implConnArr);
			} // if(arrSize

			if (applArrSize) {
				memset(&send_evt, '\0', sizeof(IMMSV_EVT));
				send_evt.type = IMMSV_EVT_TYPE_IMMA;
				send_evt.info.imma.type =
				    IMMA_EVT_ND2A_OI_CCB_APPLY_UC;
				send_evt.info.imma.info.ccbCompl.ccbId =
				    evt->info.ccbUpcallRsp.ccbId;
				send_evt.info.imma.info.ccbCompl.implId = 0;
				send_evt.info.imma.info.ccbCompl.invocation = 0;
				int ix = 0;
				for (; ix < applArrSize && err == SA_AIS_OK;
				     ++ix) {
					SaImmOiHandleT implHandle =
					    m_IMMSV_PACK_HANDLE(applConnArr[ix],
								cb->node_id);
					send_evt.info.imma.info.ccbCompl
					    .immHandle = implHandle;

					/*Fetch client node for Applier OI ! */
					immnd_client_node_get(cb, implHandle,
							      &oi_cl_node);
					osafassert(oi_cl_node != NULL);
					if (oi_cl_node->mIsStale) {
						LOG_WA(
						    "Applier client went down so completed upcall not sent");
						continue;
					} else if (immnd_mds_msg_send(
						       cb,
						       NCSMDS_SVC_ID_IMMA_OI,
						       oi_cl_node
							   ->agent_mds_dest,
						       &send_evt) !=
						   NCSCC_RC_SUCCESS) {
						LOG_WA(
						    "Completed upcall for applier failed");
					}
				}

				free(applConnArr);
				applConnArr = NULL;
			}
		} else if (err == SA_AIS_ERR_INTERRUPT) {
			/* ERR_INTERRUPT => validateOnly. */
			TRACE(
			    "Explicit validation finished OK with completed Acks from OIs "
			    "(not includingPBE)");

			err = SA_AIS_OK;
			validateOnly = true;
		} else {
			TRACE(
			    "Abort in immnd_evt_proc_ccb_compl_rsp reqConn: %u",
			    reqConn);
			/*err != SA_AIS_OK => generate SaImmOiCcbAbortCallbackT
			   upcall for all local implementers involved in the Ccb
			 */
			immnd_evt_ccb_abort(cb, evt->info.ccbUpcallRsp.ccbId,
					    NULL, NULL, NULL);
			errStrings = immModel_ccbGrabErrStrings(
			    cb, evt->info.ccbUpcallRsp.ccbId);
		}
		/* Either commit or abort has been decided. Ccb is now done.
		   If we are at originating request node, then we ALWAYS reply
		   here. No acks to wait for from apply callbacks and we know we
		   have no more completed callback replyes pending.
		 */

		if (reqConn) {

			SaImmHandleT tmp_hdl =
			    m_IMMSV_PACK_HANDLE(reqConn, cb->node_id);
			immnd_client_node_get(cb, tmp_hdl, &cl_node);
			if (cl_node == NULL || cl_node->mIsStale) {
				LOG_WA(
				    "IMMND - Client went down so no response");
				/* If the client went down, then object must be
				   either commited or aborted. If the
				   immModel_ccbFinalize is skipped over (below)
				   then the CCB will be left suspended at this
				   node indefinitely (opensaf Ticket #2010), */
				goto finalize_ccb;
			}

			memset(&send_evt, '\0', sizeof(IMMSV_EVT));
			send_evt.type = IMMSV_EVT_TYPE_IMMA;
			send_evt.info.imma.info.errRsp.error = err;
			send_evt.info.imma.type = IMMA_EVT_ND2A_IMM_ERROR;
			if (err != SA_AIS_OK) {
				send_evt.info.imma.info.errRsp.errStrings =
				    errStrings;

				if (send_evt.info.imma.info.errRsp.errStrings) {
					send_evt.info.imma.type =
					    IMMA_EVT_ND2A_IMM_ERROR_2;
				}
			}

			rc = immnd_mds_send_rsp(cb, &(cl_node->tmpSinfo),
						&send_evt);
			if (rc != NCSCC_RC_SUCCESS) {
				LOG_WA(
				    "Failed to send response to agent/client over MDS rc:%u",
				    rc);
			}
		}
	finalize_ccb:
		if (validateOnly) {
			goto done;
		}

		immsv_evt_free_attrNames(errStrings);
		TRACE_2("CCB COMPLETED: TERMINATING CCB:%u",
			evt->info.ccbUpcallRsp.ccbId);
		err = immModel_ccbFinalize(cb, evt->info.ccbUpcallRsp.ccbId);
		if (err != SA_AIS_OK) {
			LOG_WA("Failure in termination of CCB:%u - ignoring!",
			       evt->info.ccbUpcallRsp.ccbId);
			/* There is really not much we can do here. */
		}
	}
done:
	TRACE_LEAVE();
}

/****************************************************************************
 * Name          : immnd_evt_pbe_rt_obj_create_rsp
 *
 * Description   : Function to process the reply from the
 *                 SaImmOiCcbObjectCreateCallbackT upcall for the PRTO case.
 *                 Note this call arrived over fevs, to ALL IMMNDs.
 *                 This is because ALL must receive ACK/NACK from EACH
 *                 implementer.
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *
 * Return Values : None.
 *
 *****************************************************************************/
static void immnd_evt_pbe_rt_obj_create_rsp(IMMND_CB *cb, IMMND_EVT *evt,
					    bool originatedAtThisNd,
					    SaImmHandleT clnt_hdl,
					    MDS_DEST reply_dest)
{
	uint32_t rc = NCSCC_RC_SUCCESS;
	IMMSV_EVT send_evt;
	SaUint32T reqConn = 0;
	SaUint32T spApplConn = 0;
	IMMND_IMM_CLIENT_NODE *cl_node = NULL;
	TRACE_ENTER();
	memset(&send_evt, '\0', sizeof(IMMSV_EVT));

	immModel_pbePrtObjCreateContinuation(
	    cb, evt->info.ccbUpcallRsp.inv, evt->info.ccbUpcallRsp.result,
	    cb->node_id, &reqConn, &spApplConn,
	    &(send_evt.info.imma.info.objCreate));
	TRACE(
	    "Returned from pbePrtObjCreateContinuation err: %u reqConn:%x applConn:%x",
	    evt->info.ccbUpcallRsp.result, reqConn, spApplConn);

	if (evt->info.ccbUpcallRsp.result != SA_AIS_OK) {
		/* If 2PBE-slave restarts in close proximity to PRT problems,
		   then it should regenerate the pbe-file.*/
		cb->mPbeVeteranB = false;
	}

	if (spApplConn) {
		SaImmHandleT implHandle =
		    m_IMMSV_PACK_HANDLE(spApplConn, cb->node_id);
		cl_node = 0LL;
		immnd_client_node_get(cb, implHandle, &cl_node);
		osafassert(cl_node != NULL);
		send_evt.type = IMMSV_EVT_TYPE_IMMA;
		/* Assuming special applier is long DN capable. */
		send_evt.info.imma.type = IMMA_EVT_ND2A_OI_OBJ_CREATE_UC;
		send_evt.info.imma.info.objCreate.adminOwnerId = 0;
		send_evt.info.imma.info.objCreate.immHandle = implHandle;
		send_evt.info.imma.info.objCreate.ccbId = 0;
		osafassert(send_evt.info.imma.info.objCreate.attrValues);
		TRACE("Sending special applier PRTO create");
		if (cl_node->mIsStale) {
			LOG_WA(
			    "Special applier client went down so PRTO create upcall not sent");
		} else if (immnd_mds_msg_send(cb, NCSMDS_SVC_ID_IMMA_OI,
					      cl_node->agent_mds_dest,
					      &send_evt) != NCSCC_RC_SUCCESS) {
			LOG_WA("PRTO Create upcall for special applier failed");
			/* This is an unhandled message loss case!*/
		}
		immsv_free_attrvalues_list(
		    send_evt.info.imma.info.objCreate.attrValues);
		send_evt.info.imma.info.objCreate.attrValues = NULL;

		free(send_evt.info.imma.info.objCreate.className.buf);
		send_evt.info.imma.info.objCreate.className.buf = NULL;
		send_evt.info.imma.info.objCreate.className.size = 0;

		if (send_evt.info.imma.info.objCreate.parentOrObjectDn.buf) {
			free(send_evt.info.imma.info.objCreate.parentOrObjectDn
				 .buf);
			send_evt.info.imma.info.objCreate.parentOrObjectDn.buf =
			    NULL;
			send_evt.info.imma.info.objCreate.parentOrObjectDn
			    .size = 0;
		}
	}

	if (reqConn) {
		SaImmHandleT tmp_hdl =
		    m_IMMSV_PACK_HANDLE(reqConn, cb->node_id);

		immnd_client_node_get(cb, tmp_hdl, &cl_node);
		if (cl_node == NULL || cl_node->mIsStale) {
			LOG_WA("IMMND - Client went down so no response");
			TRACE_LEAVE();
			return; /*Note, this means that regardles of ccb
				   outcome, we can not reply to the process that
				   started the ccb. */
		}

		TRACE_2("SENDRSP %u", evt->info.ccbUpcallRsp.result);

		memset(&send_evt, '\0', sizeof(IMMSV_EVT));
		send_evt.type = IMMSV_EVT_TYPE_IMMA;
		send_evt.info.imma.info.errRsp.error =
		    evt->info.ccbUpcallRsp.result;
		send_evt.info.imma.type = IMMA_EVT_ND2A_IMM_ERROR;

		rc = immnd_mds_send_rsp(cb, &(cl_node->tmpSinfo), &send_evt);
		if (rc != NCSCC_RC_SUCCESS) {
			LOG_WA(
			    "Failed to send response to agent/client over MDS rc:%u",
			    rc);
		}
	}

	TRACE_LEAVE();
}

/****************************************************************************
 * Name          : immnd_evt_pbe_rt_attr_update_rsp
 *
 * Description   : Function to process the reply from the
 *                 SaImmOiCcbObjectModifyCallbackT upcall for the PRT attr case.
 *                 Note this call arrived over fevs, to ALL IMMNDs.
 *                 This is because ALL must receive ACK/NACK from EACH
 *                 implementer.
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *
 * Return Values : None.
 *
 *****************************************************************************/
static void immnd_evt_pbe_rt_attr_update_rsp(IMMND_CB *cb, IMMND_EVT *evt,
					     bool originatedAtThisNd,
					     SaImmHandleT clnt_hdl,
					     MDS_DEST reply_dest)
{
	uint32_t rc = NCSCC_RC_SUCCESS;
	IMMSV_EVT send_evt;
	SaUint32T reqConn = 0;
	SaUint32T spApplConn = 0;
	IMMND_IMM_CLIENT_NODE *cl_node = NULL;
	TRACE_ENTER();
	memset(&send_evt, '\0', sizeof(IMMSV_EVT));

	immModel_pbePrtAttrUpdateContinuation(
	    cb, evt->info.ccbUpcallRsp.inv, evt->info.ccbUpcallRsp.result,
	    cb->node_id, &reqConn, &spApplConn,
	    &(send_evt.info.imma.info.objModify));
	TRACE(
	    "Returned from pbePrtAttrUpdateContinuation err: %u reqConn:%x applConn:%x",
	    evt->info.ccbUpcallRsp.result, reqConn, spApplConn);

	if (evt->info.ccbUpcallRsp.result != SA_AIS_OK) {
		/* If 2PBE-slave restarts in close proximity to PRT problems,
		   then it should regenerate the pbe-file.*/
		cb->mPbeVeteranB = false;
	}

	if (spApplConn) {
		SaImmHandleT implHandle =
		    m_IMMSV_PACK_HANDLE(spApplConn, cb->node_id);
		cl_node = 0LL;
		immnd_client_node_get(cb, implHandle, &cl_node);
		osafassert(cl_node != NULL);
		send_evt.type = IMMSV_EVT_TYPE_IMMA;
		/* Assuming special applier is long DN capable. */
		send_evt.info.imma.type = IMMA_EVT_ND2A_OI_OBJ_MODIFY_UC;
		send_evt.info.imma.info.objModify.adminOwnerId = 0;
		send_evt.info.imma.info.objModify.immHandle = implHandle;
		send_evt.info.imma.info.objModify.ccbId = 0;
		osafassert(send_evt.info.imma.info.objModify.attrMods);
		TRACE("Sending special applier PRTA update");
		if (cl_node->mIsStale) {
			LOG_WA(
			    "Special applier client went down so RTA update upcall not sent");
		} else if (immnd_mds_msg_send(cb, NCSMDS_SVC_ID_IMMA_OI,
					      cl_node->agent_mds_dest,
					      &send_evt) != NCSCC_RC_SUCCESS) {
			LOG_WA("RTA update upcall for special applier failed");
			/* This is an unhandled message loss case!*/
		}
		immsv_free_attrmods(send_evt.info.imma.info.objModify.attrMods);
		send_evt.info.imma.info.objModify.attrMods = NULL;

		free(send_evt.info.imma.info.objModify.objectName.buf);
		send_evt.info.imma.info.objModify.objectName.buf = NULL;
		send_evt.info.imma.info.objModify.objectName.size = 0;
	}

	if (reqConn) {
		SaImmHandleT tmp_hdl =
		    m_IMMSV_PACK_HANDLE(reqConn, cb->node_id);

		immnd_client_node_get(cb, tmp_hdl, &cl_node);
		if (cl_node == NULL || cl_node->mIsStale) {
			LOG_WA("IMMND - Client went down so no response");
			TRACE_LEAVE();
			return; /*Note, this means that regardles of ccb
				   outcome, we can not reply to the process that
				   started the ccb. */
		}

		TRACE_2("SENDRSP %u", evt->info.ccbUpcallRsp.result);

		memset(&send_evt, '\0', sizeof(IMMSV_EVT));
		send_evt.type = IMMSV_EVT_TYPE_IMMA;
		send_evt.info.imma.info.errRsp.error =
		    evt->info.ccbUpcallRsp.result;
		send_evt.info.imma.type = IMMA_EVT_ND2A_IMM_ERROR;

		rc = immnd_mds_send_rsp(cb, &(cl_node->tmpSinfo), &send_evt);
		if (rc != NCSCC_RC_SUCCESS) {
			LOG_WA(
			    "Failed to send response to agent/client over MDS rc:%u",
			    rc);
		}
	}

	TRACE_LEAVE();
}

/****************************************************************************
 * Name          : immnd_evt_sync_fevs_base
 *
 * Description   : Function to receive the sync_fevs_base.
 *                 This is the fevs coutn at the coord at the time when
 *                 the searchinitialize for the sync is generated.
 *                 This count is used to discard any RTA updates that
 *                 arrived at the sync client before the sync base was
 *generated. Any RTA updates arriving after the sync-base iterator initialize,
 *must be applied at the sync client as soon as the sync
 *message for the object arrives.
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *
 * Return Values : None.
 *
 *****************************************************************************/
static void immnd_evt_sync_fevs_base(IMMND_CB *cb, IMMND_EVT *evt,
				     bool originatedAtThisNd,
				     SaImmHandleT clnt_hdl, MDS_DEST reply_dest)
{
	TRACE_ENTER2("Received syncFevsBase: %llu", evt->info.syncFevsBase);
	if (cb->mSync) {
		cb->syncFevsBase = evt->info.syncFevsBase;
	}
}

static void immnd_evt_safe_read_lock(IMMND_CB *cb, IMMND_EVT *evt,
				     bool originatedAtThisNd,
				     SaImmHandleT clnt_hdl, MDS_DEST reply_dest)
{
	IMMSV_EVT send_evt;
	IMMND_IMM_CLIENT_NODE *cl_node = NULL;
	TRACE_ENTER();

	SaAisErrorT err =
	    immModel_objectIsLockedByCcb(cb, &(evt->info.searchInit));

	switch (err) {
	case SA_AIS_OK:
		TRACE(
		    "safe_read_lock: Object is already locked by this CCB(%u)!!",
		    evt->info.searchInit.ccbId);
		break;

	case SA_AIS_ERR_BUSY:
		TRACE(
		    "Object is locked by some other CCB than this CCB(%u). Reply with BUSY",
		    evt->info.searchInit.ccbId);
		break;

	case SA_AIS_ERR_INTERRUPT:
		TRACE("Object not locked. Ccb (%u) will now lock it",
		      evt->info.searchInit.ccbId);
		err = immModel_ccbReadLockObject(cb, &(evt->info.searchInit));
		break;

	default:
		TRACE("Unusual error from immModel_objectIsLockedByCcb: %u",
		      err);
	}

	if (originatedAtThisNd) {
		immnd_client_node_get(cb, clnt_hdl, &cl_node);
		if (cl_node == NULL || cl_node->mIsStale) {
			LOG_WA("IMMND - Client went down so no response");
			return;
		}
		TRACE_2("Send immediate reply to client");

		memset(&send_evt, '\0', sizeof(IMMSV_EVT));
		send_evt.type = IMMSV_EVT_TYPE_IMMA;
		send_evt.info.imma.info.errRsp.error = err;
		send_evt.info.imma.type = IMMA_EVT_ND2A_IMM_ERROR;

		if (immnd_mds_send_rsp(cb, &(cl_node->tmpSinfo), &send_evt) !=
		    NCSCC_RC_SUCCESS) {
			LOG_WA(
			    "immnd_evt_class_delete: SENDRSP FAILED TO SEND");
		}
	}

	TRACE_LEAVE();
}

/****************************************************************************
 * Name          : immnd_evt_pbe_admop_rsp
 *
 * Description   : Function to process the reply from the special
 *                 admop to persistify class create.
 *                 Note this call arrived over fevs, to ALL IMMNDs.
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *
 * Return Values : None.
 *
 *****************************************************************************/
static void immnd_evt_pbe_admop_rsp(IMMND_CB *cb, IMMND_EVT *evt,
				    bool originatedAtThisNd,
				    SaImmHandleT clnt_hdl, MDS_DEST reply_dest)
{
	uint32_t rc = NCSCC_RC_SUCCESS;
	IMMSV_EVT send_evt;
	SaUint32T reqConn = 0;
	IMMND_IMM_CLIENT_NODE *cl_node = NULL;
	TRACE_ENTER();
	SaUint32T admoId =
	    m_IMMSV_UNPACK_HANDLE_HIGH(evt->info.admOpRsp.invocation);
	osafassert(admoId == 0);
	SaUint32T invoc =
	    m_IMMSV_UNPACK_HANDLE_LOW(evt->info.admOpRsp.invocation);

	if (evt->info.admOpRsp.error != SA_AIS_OK) {
		LOG_ER(
		    "Received error result %u from PBE CLASS CREATE - dropping "
		    "continuation. PBE should get restarted",
		    evt->info.admOpRsp.error);
		return;
	}

	if (evt->info.admOpRsp.result == SA_AIS_ERR_REPAIR_PENDING) {
		/* OK result on OPENSAF_IMM_PBE_CLASS_CREATE */
		immModel_pbeClassCreateContinuation(cb, invoc, cb->node_id,
						    &reqConn);
		TRACE("Returned from pbeClassCreateContinuation invoc:%x",
		      invoc);
	} else if (evt->info.admOpRsp.result == SA_AIS_ERR_NO_SPACE) {
		/* OK result on OPENSAF_IMM_PBE_CLASS_DELETE */
		immModel_pbeClassDeleteContinuation(cb, invoc, cb->node_id,
						    &reqConn);
		TRACE("Returned from pbeClassDeleteContinuation invoc:%x",
		      invoc);
	} else if (evt->info.admOpRsp.result ==
		   SA_AIS_ERR_QUEUE_NOT_AVAILABLE) {
		/* OK result on OPENSAF_IMM_PBE_UPDATE_EPOCH
		   No reqConn since this is no client for this call. */
		immModel_pbeUpdateEpochContinuation(cb, invoc, cb->node_id);
		TRACE("Returned from pbeUpdateEpochContinuation invoc:%x",
		      invoc);
	} else {
		LOG_ER("Unexpected PBE admop result %u",
		       evt->info.admOpRsp.result);
		/* reqConn will be zero. */
	}

	if (reqConn) {
		SaImmHandleT tmp_hdl =
		    m_IMMSV_PACK_HANDLE(reqConn, cb->node_id);

		immnd_client_node_get(cb, tmp_hdl, &cl_node);
		if (cl_node == NULL || cl_node->mIsStale) {
			LOG_WA("IMMND - Client went down so no response");
			TRACE_LEAVE();
			return;
		}

		memset(&send_evt, '\0', sizeof(IMMSV_EVT));
		send_evt.type = IMMSV_EVT_TYPE_IMMA;
		send_evt.info.imma.info.errRsp.error = SA_AIS_OK;

		send_evt.info.imma.type = IMMA_EVT_ND2A_IMM_ERROR;

		rc = immnd_mds_send_rsp(cb, &(cl_node->tmpSinfo), &send_evt);
		if (rc != NCSCC_RC_SUCCESS) {
			LOG_WA(
			    "Failed to send response to agent/client over MDS rc:%u",
			    rc);
		}
	}
	TRACE_LEAVE();
}

/****************************************************************************
 * Name          : immnd_evt_pbe_rt_obj_deletes_rsp
 *
 * Description   : Function to process the reply from the
 *                 deletes completed upcall for the special PRTO deletes case.
 *                 Note this call arrived over fevs, to ALL IMMNDs.
 *                 This is because ALL must receive ACK/NACK from EACH
 *                 implementer.
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *
 * Return Values : None.
 *
 *****************************************************************************/
static void immnd_evt_pbe_rt_obj_deletes_rsp(IMMND_CB *cb, IMMND_EVT *evt,
					     bool originatedAtThisNd,
					     SaImmHandleT clnt_hdl,
					     MDS_DEST reply_dest)
{
	uint32_t rc = NCSCC_RC_SUCCESS;
	IMMSV_EVT send_evt;
	SaUint32T reqConn = 0;
	IMMND_IMM_CLIENT_NODE *cl_node = NULL;
	SaStringT *objNameArr = NULL;
	SaUint32T arrSize = 0;
	SaUint32T spApplConn = 0;
	SaUint32T pbe2BConn = 0;
	SaInt32T numOps = 0;
	TRACE_ENTER();

	numOps = immModel_pbePrtObjDeletesContinuation(
	    cb, evt->info.ccbUpcallRsp.inv, evt->info.ccbUpcallRsp.result,
	    cb->node_id, &reqConn, &objNameArr, &arrSize, &spApplConn,
	    &pbe2BConn);
	TRACE("Returned from pbePrtObjDeletesContinuation err: %u reqConn:%x",
	      evt->info.ccbUpcallRsp.result, reqConn);

	if (evt->info.ccbUpcallRsp.result != SA_AIS_OK) {
		/* If 2PBE-slave restarts in close proximity to PRT problems,
		   then it should regenerate the pbe-file.*/
		cb->mPbeVeteranB = false;
	}

	if (pbe2BConn && (evt->info.ccbUpcallRsp.result == SA_AIS_OK)) {
		SaImmHandleT tmp_hdl =
		    m_IMMSV_PACK_HANDLE(pbe2BConn, cb->node_id);
		osafassert(numOps);
		send_evt.info.imma.type = IMMA_EVT_ND2A_OI_CCB_COMPLETED_UC;
		send_evt.info.imma.info.ccbCompl.ccbId = 0;
		send_evt.info.imma.info.ccbCompl.immHandle = tmp_hdl;
		send_evt.info.imma.info.ccbCompl.implId = numOps;
		/* ^^Hack: Use implId to store objCount */
		send_evt.info.imma.info.ccbCompl.invocation =
		    evt->info.ccbUpcallRsp.inv;

		immnd_client_node_get(cb, tmp_hdl, &cl_node);
		if (cl_node == NULL || cl_node->mIsStale) {
			LOG_WA(
			    "IMMND - PBE slave client went down detected in immnd_evt_pbe_rt_obj_deletes_rsp");
			goto noreply; /*Note, this means that regardles of ccb
					outcome, we can not reply to the process
					that started the ccb. */
		}

		TRACE_2("MAKING PBE PRTO DELETE COMPLETED upcall to PBE Slave");
		if (immnd_mds_msg_send(cb, NCSMDS_SVC_ID_IMMA_OI,
				       cl_node->agent_mds_dest,
				       &send_evt) != NCSCC_RC_SUCCESS) {
			LOG_WA("Upcall over MDS for persistent rt obj deletes "
			       "completed, to PBE slave failed!");
			goto done;
		}

	} else if (pbe2BConn) {
		/* Generate abort callbac to slave. */
	}

	if (reqConn) {
		SaImmHandleT tmp_hdl =
		    m_IMMSV_PACK_HANDLE(reqConn, cb->node_id);

		immnd_client_node_get(cb, tmp_hdl, &cl_node);
		if (cl_node == NULL || cl_node->mIsStale) {
			LOG_WA("IMMND - Client went down so no response");
			TRACE_LEAVE();
			goto noreply; /*Note, this means that regardles of ccb
					outcome, we can not reply to the process
					that started the ccb. */
		}

		TRACE_2("SENDRSP %u", evt->info.ccbUpcallRsp.result);

		memset(&send_evt, '\0', sizeof(IMMSV_EVT));
		send_evt.type = IMMSV_EVT_TYPE_IMMA;
		send_evt.info.imma.info.errRsp.error =
		    evt->info.ccbUpcallRsp.result;
		send_evt.info.imma.type = IMMA_EVT_ND2A_IMM_ERROR;
		rc = immnd_mds_send_rsp(cb, &(cl_node->tmpSinfo), &send_evt);
		if (rc != NCSCC_RC_SUCCESS) {
			LOG_WA(
			    "Failed to send response to agent/client over MDS rc:%u",
			    rc);
		}
	}

noreply:
	if (spApplConn && (evt->info.ccbUpcallRsp.result == SA_AIS_OK)) {
		int ix = 0;
		SaImmHandleT tmp_hdl =
		    m_IMMSV_PACK_HANDLE(spApplConn, cb->node_id);
		/*Fetch client node for Special applier OI */
		cl_node = 0LL;
		immnd_client_node_get(cb, tmp_hdl, &cl_node);
		osafassert(cl_node != NULL);
		if (cl_node->mIsStale) {
			LOG_WA(
			    "Special applier is down => notify of PrtObj delete is dropped");
			goto done;
		}

		TRACE_2("Special applier needs to be notified of RTO delete.");
		memset(&send_evt, '\0', sizeof(IMMSV_EVT));
		send_evt.type = IMMSV_EVT_TYPE_IMMA;
		send_evt.info.imma.type = IMMA_EVT_ND2A_OI_OBJ_DELETE_UC;
		send_evt.info.imma.info.objDelete.ccbId = 0;
		send_evt.info.imma.info.objDelete.adminOwnerId = 0;
		send_evt.info.imma.info.objDelete.immHandle = tmp_hdl;

		for (; ix < arrSize; ++ix) {
			send_evt.info.imma.info.objDelete.objectName.size =
			    (SaUint32T)strlen(objNameArr[ix]) + 1;
			send_evt.info.imma.info.objDelete.objectName.buf =
			    objNameArr[ix];

			TRACE_2(
			    "MAKING PBE-IMPLEMENTER PERSISTENT RT-OBJ DELETE upcalls");
			if (immnd_mds_msg_send(cb, NCSMDS_SVC_ID_IMMA_OI,
					       cl_node->agent_mds_dest,
					       &send_evt) != NCSCC_RC_SUCCESS) {
				LOG_WA(
				    "RTO delete Upcall special applier failed!");
				/* This is an unhandled message loss case!*/
			}
		}
	}

done:
	if (arrSize) {
		int ix;
		for (ix = 0; ix < arrSize; ++ix) {
			free(objNameArr[ix]);
		}
		free(objNameArr);
		objNameArr = NULL;
		arrSize = 0;
	}

	TRACE_LEAVE();
}

/****************************************************************************
 * Name          : immnd_evt_proc_rt_update_pull
 *
 * Description   : Function to process a remote uppcall
 *                 of SaImmOiRtAttrUpdateCallbackT
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 *****************************************************************************/
static uint32_t immnd_evt_proc_rt_update_pull(IMMND_CB *cb, IMMND_EVT *evt,
					      IMMSV_SEND_INFO *sinfo)
{
	uint32_t rc = NCSCC_RC_SUCCESS;
	SaUint32T implConn = 0;
	SaAisErrorT err = SA_AIS_OK;
	IMMSV_EVT send_evt;
	IMMND_IMM_CLIENT_NODE *oi_cl_node = NULL;
	SaImmOiHandleT implHandle = 0LL;

	TRACE_ENTER();
	memset(&send_evt, '\0', sizeof(IMMSV_EVT));
	send_evt.type = IMMSV_EVT_TYPE_IMMA;

	IMMSV_OM_SEARCH_REMOTE *req = &evt->info.searchRemote;
	osafassert(cb->node_id == req->remoteNodeId);
	implConn =
	    immModel_findConnForImplementerOfObject(cb, &req->objectName);

	if (implConn) {
		/*Send the upcall to imma */
		send_evt.info.imma.type = IMMA_EVT_ND2A_SEARCH_REMOTE;
		IMMSV_OM_SEARCH_REMOTE *rreq =
		    &send_evt.info.imma.info.searchRemote;
		rreq->remoteNodeId = req->remoteNodeId;
		rreq->searchId = req->searchId;
		rreq->requestNodeId = req->requestNodeId;
		rreq->objectName = req->objectName; /* borrow, objectname.buf
						       freed in
						       immnd_evt_destroy */
		rreq->attributeNames =
		    req->attributeNames; /* borrow, freed in immnd_evt_destroy
					  */

		implHandle = m_IMMSV_PACK_HANDLE(
		    implConn, cb->node_id); /* == req->remoteNodeId */

		/*Fetch client node for OI ! */
		immnd_client_node_get(cb, implHandle, &oi_cl_node);
		if (oi_cl_node == NULL || oi_cl_node->mIsStale) {
			LOG_WA(
			    "Implementer exists in ImmModel but not in client tree.");
			err = SA_AIS_ERR_TRY_AGAIN;
		} else {
			rreq->client_hdl = implHandle;
			TRACE_2(
			    "MAKING OI-IMPLEMENTER rtUpdate upcall towards agent");
			if (immnd_mds_msg_send(cb, NCSMDS_SVC_ID_IMMA_OI,
					       oi_cl_node->agent_mds_dest,
					       &send_evt) != NCSCC_RC_SUCCESS) {
				LOG_WA(
				    "Agent upcall over MDS for rtUpdate failed");
				err = SA_AIS_ERR_NO_RESOURCES;
			} else {
				/*The upcall was sent. Register a search
				 * implementer continuation! */
				immModel_setSearchImplContinuation(
				    cb, req->searchId, req->requestNodeId,
				    sinfo->dest);
			}
		}
	} else {
		LOG_WA("Could not locate implementer for %s, detached?",
		       req->objectName.buf);
		err = SA_AIS_ERR_TRY_AGAIN; /*Perhaps not the clearest return
					       value */
	}

	if (err != SA_AIS_OK) {
		/*
		   send_evt.info.imma.type = IMMA_EVT_ND2A_IMM_ERROR;
		   send_evt.info.imma.info.errRsp.error = err;
		 */

		LOG_WA(
		    "Missing implementation of error case, error:%u, handled by timeout instead",
		    err);

		/* TODO: something was wrong => immediate reply to avoid block &
		   timeout of client. See pull reply.
		 */
		/*
		   char buffer[BUFSZ];
		   size_t resLength = sizeof(struct res_header);
		   struct res_header* tmpMessage = (struct res_header *) buffer;
		   tmpMessage->error = err;
		   tmpMessage->id = MESSAGE_EVS_RT_ATTRUPDATE_REPLY;

		   ImmOmRspSearchRemote rspo;
		   rspo.remoteNodeId = req->remoteNodeId;
		   rspo.searchId = req->searchId;
		   rspo.requestNodeId = req->requestNodeId;
		   rspo.runtimeAttrs.objectName.buf = req->objectName.buf;
		   rspo.runtimeAttrs.objectName.size = req->objectName.size;
		   rspo.runtimeAttrs.attrValuesList.list.count = 0;
		   rspo.runtimeAttrs.attrValuesList.list.size = 0;
		   rspo.runtimeAttrs.attrValuesList.list.free = 0;

		   osafassert(multicastMessage(conn, tmpMessage,
		   tmpMessage->size) == SA_AIS_OK);
		 */
	}

	return rc;
}

/****************************************************************************
 * Name          : immnd_evt_proc_remote_search_rsp
 *
 * Description   : Function to process the result of a remote search
 *                 for fetching rt attrs from remote implementer
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 *****************************************************************************/
static uint32_t immnd_evt_proc_remote_search_rsp(IMMND_CB *cb, IMMND_EVT *evt,
						 IMMSV_SEND_INFO *sinfo)
{
	TRACE_ENTER();
	IMMSV_OM_RSP_SEARCH_REMOTE *rspo = &evt->info.rspSrchRmte;
	osafassert(cb->node_id == rspo->requestNodeId);
	/*Fetch originating request continuation */
	SaInvocationT invoc =
	    m_IMMSV_PACK_HANDLE(rspo->remoteNodeId, rspo->searchId);

	SaUint32T reqConn;
	immModel_fetchSearchReqContinuation(cb, invoc, &reqConn);

	TRACE_2("FETCHED SEARCH REQ CONTINUATION FOR %u|%u->%u", rspo->searchId,
		rspo->remoteNodeId, reqConn);

	if (!reqConn) {
		LOG_WA(
		    "Failed to retrieve search continuation for %llu, client died ?",
		    *((SaUint64T *)&invoc));
		/* Cant do anything but just drop it. */
		goto leave;
	}

	TRACE_2("Build reply to agent");
	search_req_continue(cb, rspo, reqConn);

leave:
	TRACE_LEAVE();
	return NCSCC_RC_SUCCESS;
}

/****************************************************************************
 * Name          : immnd_evt_proc_admop_rsp
 *
 * Description   : Function to process the saImmOiAdminOperationResult
 *                 for both sync and async invocations
 *
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *                 bool  async - true=> original client call was async
 *                 bool  local - true=>local upcall from OI,
 *                                  false=>remote forward ND->ND.
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 *****************************************************************************/
static uint32_t immnd_evt_proc_admop_rsp(IMMND_CB *cb, IMMND_EVT *evt,
					 IMMSV_SEND_INFO *sinfo, bool async,
					 bool local)
{
	IMMSV_EVT send_evt;
	IMMND_IMM_CLIENT_NODE *oi_cl_node = NULL;
	IMMND_IMM_CLIENT_NODE *cl_node = NULL;
	uint32_t rc = NCSCC_RC_SUCCESS;
	SaImmHandleT oi_client_hdl;
	SaUint32T implConn = 0;
	SaUint32T reqConn = 0;
	SaUint64T reply_dest = 0LL;

	memset(&send_evt, '\0', sizeof(IMMSV_EVT));

	if (local) {
		oi_client_hdl = evt->info.admOpRsp.oi_client_hdl;
		immnd_client_node_get(cb, oi_client_hdl, &oi_cl_node);
		if (oi_cl_node == NULL || oi_cl_node->mIsStale) {
			LOG_WA("IMMND - Client %llu went down so no response",
			       oi_client_hdl);
			return rc; /*Note: Error is ignored. Nothing else we can
				      do since OI reply call was async and we
				      cant find original OM call.
				    */
		}
	}

	/*Removed this since code changed to generate direct call ND->ND
	   if(!immnd_is_immd_up(cb)) {
	   send_evt.info.imma.info.errRsp.error= SA_AIS_ERR_TRY_AGAIN;
	   return rc;
	   }
	 */

	immModel_fetchAdmOpContinuations(cb, evt->info.admOpRsp.invocation,
					 local, &implConn, &reqConn,
					 &reply_dest);

	TRACE_2(
	    "invocation:%llu, result:%u\n impl:%u req:%u dest:%llu me:%" PRIu64,
	    evt->info.admOpRsp.invocation, evt->info.admOpRsp.result, implConn,
	    reqConn, reply_dest, cb->immnd_mdest_id);

	if (local && !implConn) {
		/*Continuation with implementer lost => connection probably
		   lost. What to do? Do I really need this check? Yes because a
		   faulty application may invoke this call when it should not.
		   I need a timer associated with the implementer continuation
		   anyway..
		 */
		LOG_WA(
		    "Received reply from implementer yet continuation lost - ignoring");
		return rc;
	}

	if (reqConn) {
		/*Original Request was local, send reply. */
		SaImmHandleT tmp_hdl =
		    m_IMMSV_PACK_HANDLE(reqConn, cb->node_id);

		immnd_client_node_get(cb, tmp_hdl, &cl_node);
		if (cl_node == NULL || cl_node->mIsStale) {
			LOG_WA("IMMND - Client went down so no response");
			return rc; /*Note: Error is ignored! should cause
				      bad-handle..if local */
		}

		/*displayRes is used when the adminoperation with operation name
		  display is called. displayRes will be true, when
		  admin-operation has OperationName as display and directed
		  towards opensafImm=opensafImm,safApp=safImmService object.
		  Implementer for opensafImm=opensafImm,safApp=safImmService is
		  PBE.
		*/
		bool displayRes = false;
		IMMSV_ADMIN_OPERATION_PARAM *rparams = NULL;
		IMMSV_ADMIN_OPERATION_PARAM *p;
		struct ImmsvAdminOperationParam *params =
		    evt->info.admOpRsp.parms;
		SaAisErrorT err = evt->info.admOpRsp.error;
		SaAisErrorT result = evt->info.admOpRsp.result;

		while (params) {
			if ((strcmp(params->paramName.buf,
				    SA_IMM_PARAM_ADMOP_NAME) == 0) &&
			    (params->paramType == SA_IMM_ATTR_SASTRINGT)) {
				if (strncmp(params->paramBuffer.val.x.buf,
					    "display", 7) == 0) {
					displayRes = true;
				} else {
					LOG_WA(
					    "Invalid OperationName %s for dumping IMM resource"
					    "is not set properly",
					    params->paramBuffer.val.x.buf);
					err = SA_AIS_ERR_INVALID_PARAM;
				}
				break;
			}
			params = params->next;
		}

		if (displayRes) {
			result = immModel_resourceDisplay(
			    cb, evt->info.admOpRsp.parms, &rparams);
		}

		send_evt.type = IMMSV_EVT_TYPE_IMMA;
		send_evt.info.imma.type = (evt->info.admOpRsp.parms)
					      ? IMMA_EVT_ND2A_ADMOP_RSP_2
					      : IMMA_EVT_ND2A_ADMOP_RSP;
		send_evt.info.imma.info.admOpRsp.invocation =
		    evt->info.admOpRsp.invocation;
		send_evt.info.imma.info.admOpRsp.result = result;
		send_evt.info.imma.info.admOpRsp.error = err;
		send_evt.info.imma.info.admOpRsp.parms =
		    (displayRes) ? rparams : evt->info.admOpRsp.parms;

		if (async) {
			TRACE_2("NORMAL ASYNCRONOUS reply %llu %u %u to OM",
				send_evt.info.imma.info.admOpRsp.invocation,
				send_evt.info.imma.info.admOpRsp.result,
				send_evt.info.imma.info.admOpRsp.error);
			rc = immnd_mds_msg_send(cb, NCSMDS_SVC_ID_IMMA_OM,
						cl_node->agent_mds_dest,
						&send_evt);
			TRACE_2("ASYNC REPLY SENT rc:%u", rc);
		} else {
			TRACE_2("NORMAL syncronous reply %llu %u to OM",
				send_evt.info.imma.info.admOpRsp.invocation,
				send_evt.info.imma.info.admOpRsp.result);
			rc = immnd_mds_send_rsp(cb, &(cl_node->tmpSinfo),
						&send_evt);
			TRACE_2("SYNC REPLY SENT rc:%u", rc);
		}

		// Free memory
		while (rparams) {
			p = rparams;
			rparams = p->next;
			if (p->paramName.buf) {
				free(p->paramName.buf);
				p->paramName.buf = NULL;
				p->paramName.size = 0;
			}
			immsv_evt_free_att_val(&(p->paramBuffer), p->paramType);
			p->next = NULL;
			free(p);
		}

		if (rc != NCSCC_RC_SUCCESS) {
			LOG_ER(
			    "Failure in sending reply for admin-op over MDS");
		}
	} else if (reply_dest && reply_dest != (SaUint64T)cb->immnd_mdest_id) {
		/*Forward reply to relevant ND. */
		send_evt.type = IMMSV_EVT_TYPE_IMMND;
		if (evt->info.admOpRsp.parms) {
			send_evt.info.immnd.type =
			    (async) ? IMMND_EVT_ND2ND_ASYNC_ADMOP_RSP_2
				    : IMMND_EVT_ND2ND_ADMOP_RSP_2;
		} else {
			send_evt.info.immnd.type =
			    (async) ? IMMND_EVT_ND2ND_ASYNC_ADMOP_RSP
				    : IMMND_EVT_ND2ND_ADMOP_RSP;
		}
		send_evt.info.immnd.info.admOpRsp.invocation =
		    evt->info.admOpRsp.invocation;
		send_evt.info.immnd.info.admOpRsp.result =
		    evt->info.admOpRsp.result;
		send_evt.info.immnd.info.admOpRsp.error =
		    evt->info.admOpRsp.error;
		send_evt.info.immnd.info.admOpRsp.oi_client_hdl = 0LL;
		send_evt.info.immnd.info.admOpRsp.parms =
		    evt->info.admOpRsp.parms;

		TRACE_2("FORWARDING TO OTHER ND!");

		rc = immnd_mds_msg_send(cb, NCSMDS_SVC_ID_IMMND, reply_dest,
					&send_evt);

		if (rc != NCSCC_RC_SUCCESS) {
			LOG_ER(
			    "Problem in sending to peer IMMND over MDS. Discarding admin op reply.");
		}
	} else {
		LOG_WA(
		    "Continuation for AdminOp was lost, probably due to timeout");
	}

	return rc;
}

static void
immnd_extract_preload_params(IMMND_CB *cb,
			     const struct ImmsvOmAdminOperationInvoke *req)
{
	IMMSV_EVT send_evt;
	uint32_t rc = NCSCC_RC_SUCCESS;
	IMMSV_ADMIN_OPERATION_PARAM *param = req->params;
	/* Successfully preloaded. */
	cb->m2Pbe = 1;

	memset(&send_evt, '\0', sizeof(IMMSV_EVT));
	send_evt.type = IMMSV_EVT_TYPE_IMMD;
	send_evt.info.immd.type = IMMD_EVT_ND2D_2PBE_PRELOAD;

	do {
		osafassert(param->paramType == SA_IMM_ATTR_SAUINT32T ||
			   param->paramType == SA_IMM_ATTR_SAUINT64T);
		TRACE("Param:%s ", param->paramName.buf);
		if (!strncmp(param->paramName.buf, "epoch",
			     param->paramName.size)) {
			send_evt.info.immd.info.pbe2.epoch =
			    param->paramBuffer.val.sauint32;
			TRACE("Assigned epoch %u",
			      send_evt.info.immd.info.pbe2.epoch);
			/*cb->mMyEpoch = send_evt.info.immd.info.pbe2.epoch;*/
		} else if (!strncmp(param->paramName.buf, "commit_time",
				    param->paramName.size)) {
			send_evt.info.immd.info.pbe2.maxCommitTime =
			    param->paramBuffer.val.sauint32;
			TRACE("Assigned commit_time %u",
			      send_evt.info.immd.info.pbe2.maxCommitTime);
		} else if (!strncmp(param->paramName.buf, "ccb_id",
				    param->paramName.size)) {
			send_evt.info.immd.info.pbe2.maxCcbId =
			    param->paramBuffer.val.sauint32;
			TRACE("Assigned ccb_id %u",
			      send_evt.info.immd.info.pbe2.maxCcbId);
		} else if (!strncmp(param->paramName.buf, "weak_commit_time",
				    param->paramName.size)) {
			send_evt.info.immd.info.pbe2.maxWeakCommitTime =
			    param->paramBuffer.val.sauint32;
			TRACE("Assigned weak_commit_time %u",
			      send_evt.info.immd.info.pbe2.maxWeakCommitTime);
		} else if (!strncmp(param->paramName.buf, "weak_ccb_id",
				    param->paramName.size)) {
			send_evt.info.immd.info.pbe2.maxWeakCcbId =
			    param->paramBuffer.val.sauint64;
			TRACE("Assigned weakccb_id %llu",
			      send_evt.info.immd.info.pbe2.maxWeakCcbId);
		}
		param = param->next;
	} while (param);

	rc = immnd_mds_msg_send(cb, NCSMDS_SVC_ID_IMMD, cb->immd_mdest_id,
				&send_evt);

	/* Check against immd down not needed. Mds handles reduncancy. */

	if (rc != NCSCC_RC_SUCCESS) {
		LOG_ER(
		    "Problem in sending RDND_PBE to IMMD over MDS IMMD UP?:%u",
		    immnd_is_immd_up(cb));
	}
}

/****************************************************************************
 * Name          : immnd_evt_proc_admop
 *
 * Description   : Function to process the saImmOmAdminOperationInvoke
 *                 or saImmAdminOperationInvokeAsync operation
 *                 from agent.
 *
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *                 bool originatedAtThisNode - Did it come from this node?
 *                 SaImmHandleT clnt_hdl - The client handle (only relevant if
 *                                         originatedAtThisNode is true).
 *                 IMM_DEST reply_dest - The dest of the ND to where reply
 *                                         is to be sent (only relevant if
 *                                       originatedAtThisNode is false).
 * Return Values : None
 *
 *****************************************************************************/
static void immnd_evt_proc_admop(IMMND_CB *cb, IMMND_EVT *evt,
				 bool originatedAtThisNd, SaImmHandleT clnt_hdl,
				 MDS_DEST reply_dest)
{
	SaAisErrorT error = SA_AIS_OK;
	IMMSV_EVT send_evt;
	IMMND_IMM_CLIENT_NODE *cl_node = NULL;
	IMMND_IMM_CLIENT_NODE *oi_cl_node = NULL;
	SaImmOiHandleT implHandle = 0LL;
	SaUint32T conn = m_IMMSV_UNPACK_HANDLE_HIGH(clnt_hdl);

	SaUint32T implConn = 0;
	NCS_NODE_ID implNodeId = 0;
	/*displayRes is used for admin-operation which has OperationName as
	  display and directed towards
	  opensafImm=opensafImm,safApp=safImmService object.
	  SA_AIS_ERR_REPAIR_PENDING will be returned if there is no PBE and
	  OperationName is display.
	*/
	bool displayRes = false;
	bool pbeExpected = cb->mPbeFile && (cb->mRim == SA_IMM_KEEP_REPOSITORY);

	bool async = (evt->type == IMMND_EVT_A2ND_IMM_ADMOP_ASYNC);
	osafassert(evt->type == IMMND_EVT_A2ND_IMM_ADMOP || async);
	TRACE_ENTER();
	TRACE_1(async ? "ASYNC ADMOP" : "SYNC ADMOP");

	/*
	   Global uniqueness for the invocation is guaranteed by the globally
	   unique adminOwnerId which is owned by ONE client connection.
	   The client side increments an invocation counter unique for that
	   process (thread safe).
	 */

	SaInvocationT saInv = m_IMMSV_PACK_HANDLE(
	    evt->info.admOpReq.adminOwnerId, evt->info.admOpReq.invocation);

	error = immModel_adminOperationInvoke(
	    cb, &(evt->info.admOpReq), originatedAtThisNd ? conn : 0,
	    (SaUint64T)reply_dest, saInv, &implConn, &implNodeId, pbeExpected,
	    &displayRes);

	/*Check if we have an implementer, if so forward the message.
	   If there is no implementer then implNodeId is zero.
	   in that case 'error' has been set to SA_AIS_ERR_NOT_EXIST.
	 */

	if (error == SA_AIS_OK && implConn) {
		/*Implementer exists and is local, make the up-call! */
		osafassert(implNodeId == cb->node_id);
		implHandle = m_IMMSV_PACK_HANDLE(implConn, implNodeId);

		/*Fetch client node for OI ! */
		immnd_client_node_get(cb, implHandle, &oi_cl_node);
		if (oi_cl_node == NULL || oi_cl_node->mIsStale) {
			LOG_ER("IMMND - Client %llu went down so no response",
			       implHandle);
			error = SA_AIS_ERR_NOT_EXIST;
		} else {
			if (cb->mIsOtherScUp &&
			    (evt->info.admOpReq.operationId ==
			     OPENSAF_IMM_NOST_FLAG_ON) &&
			    (evt->info.admOpReq.params != NULL) &&
			    (evt->info.admOpReq.params->paramType ==
			     SA_IMM_ATTR_SAUINT32T) &&
			    (evt->info.admOpReq.params->paramBuffer.val
				 .sauint32 == OPENSAF_IMM_FLAG_2PBE1_ALLOW) &&
			    (strcmp(evt->info.admOpReq.objectName.buf,
				    OPENSAF_IMM_OBJECT_DN) == 0) &&
			    (strcmp(evt->info.admOpReq.params->paramName.buf,
				    OPENSAF_IMM_ATTR_NOSTD_FLAGS) == 0)) {
				/* Caught the special case of attempt to togle
				   ON noStdFlags value 0x8
				   (OPENSAF_IMM_FLAG_2PBE1_ALLOW). This is not
				   allowed as long as the other SC is up,
				   regardless of whether the slave PBE is up. We
				   "know" that >>here<< we are on the SC running
				   the IMMND coord and thus the primary PBE,
				   because this is the code branch for invoking
				   callback on the implementer (implCon) for the
				   admin-op and the implementer for
				   OPENSAF_IMM_OBJECT_DN (if it exists) must be
				   the PBE.
				*/
				LOG_WA(
				    "Rejecting attempt to togle *ON* value x%x for %s in %s when both SCs are up",
				    OPENSAF_IMM_FLAG_2PBE1_ALLOW,
				    OPENSAF_IMM_ATTR_NOSTD_FLAGS,
				    OPENSAF_IMM_OBJECT_DN);
				evt->info.admOpReq.operationId =
				    OPENSAF_IMM_BAD_OP_BOUNCE;
			}

			memset(&send_evt, '\0', sizeof(IMMSV_EVT));
			send_evt.type = IMMSV_EVT_TYPE_IMMA;
			send_evt.info.imma.type = IMMA_EVT_ND2A_IMM_ADMOP;
			send_evt.info.imma.info.admOpReq.adminOwnerId =
			    evt->info.admOpReq.adminOwnerId;
			send_evt.info.imma.info.admOpReq.operationId =
			    evt->info.admOpReq.operationId;

			/* TODO: This is a bit ugly, using the continuationId to
			   transport the immOiHandle. */
			send_evt.info.imma.info.admOpReq.continuationId =
			    implHandle;

			send_evt.info.imma.info.admOpReq.invocation =
			    evt->info.admOpReq.invocation;

			send_evt.info.imma.info.admOpReq.timeout =
			    evt->info.admOpReq
				.timeout; /*why send tmout to client ?? */

			send_evt.info.imma.info.admOpReq.objectName.size =
			    evt->info.admOpReq.objectName.size;

			/*Warning, borowing pointer to string, dont delete at
			 * this end! */
			send_evt.info.imma.info.admOpReq.objectName.buf =
			    evt->info.admOpReq.objectName.buf;

			/*Warning, borowing pointer to params list, dont delete
			 * at this end! */
			send_evt.info.imma.info.admOpReq.params =
			    evt->info.admOpReq.params;

			TRACE_2("IMMND sending Agent upcall");
			if (immnd_mds_msg_send(cb, NCSMDS_SVC_ID_IMMA_OI,
					       oi_cl_node->agent_mds_dest,
					       &send_evt) != NCSCC_RC_SUCCESS) {
				LOG_ER("MDS SEND FAILED");
				error = SA_AIS_ERR_LIBRARY;
			} else {
				TRACE_2("IMMND UPCALL TO AGENT SEND SUCCEEDED");
			}

			if (evt->info.admOpReq.operationId ==
			    OPENSAF_IMM_PBE_CCB_PREPARE) {
				/*
				   Intercepted 2PBE CCB prepare call towards
				   slave PBE. Ensure PBE-slave restarts with
				   regenerated imm.db.x if it crashes in
				   critical. That is, crashes in prepare or
				   after prepare but before apply. Only primary
				   PBE can be enquired on ccb-outcome after a
				   PBE crash. This will also cover the case of
				   slave PBE crashing in prepare and then a
				   failover.
				*/
				cb->mPbeVeteranB = false;
			}
		}
	}

	/*Take care of possible immediate reply, for errors and special
	 * admin-ops */

	/* Special non-error case added for handling admin-ops directed at
	   safRdn=immManagement,safApp=safImmService. */
	if (error == SA_AIS_ERR_INTERRUPT) {
		SaImmRepositoryInitModeT oldRim = cb->mRim;
		cb->mRim = immModel_getRepositoryInitMode(cb);
		if (oldRim != cb->mRim) {
			/* Reset mPbeVeteran to force re-generation of db-file
			   when mRim is changed again.
			   We can not continue using any existing db-file
			   because we would get a gap in the persistent history.
			*/
			cb->mPbeVeteran = false;
			cb->mPbeVeteranB = false;

			TRACE_2("Repository init mode changed to: %s",
				(cb->mRim == SA_IMM_INIT_FROM_FILE)
				    ? "INIT_FROM_FILE"
				    : "KEEP_REPOSITORY");
			if (cb->mIsCoord && cb->mPbeFile &&
			    (cb->mRim == SA_IMM_INIT_FROM_FILE)) {
				cb->mBlockPbeEnable = 0x1;
				/* Prevent PBE re-enable until current PBE has
				 * terminated. */
				immnd_announceDump(cb);
				/* Communicates RIM to IMMD. Needed to decide if
				   reload must cause cluster restart.
				*/
			}
		}
		/* Force direct OK reply (no implementer to wait for). */
		error = SA_AIS_ERR_REPAIR_PENDING;
		pbeExpected = 0;
	}

	/* Special non-error case added for handling admin-ops directed at
	   opensafImm=opensafImm,safApp=safImmService.
	   If PBE is enabled, such admin-ops are handled by the PBE-OI.
	   But when there is no PBE, then we handle the adminOp inside the
	   immnds and reply directly. An error reply from such an internally
	   handled adminOp will be handled identically to an error reply for a
	   normal adminOp where the error is caught early in
	   ImmModel::adminOperationInvoke().

	   But an OK reply for an internally handled adminOp is encoded as
	   SA_AIS_ERR_REPAIR_PENDING, to avoid confusion with the normal OK
	   reponse in the early processing. That error code is not used anywhere
	   in the imm spec, so there is no risk of confusing it with a normal
	   error response from early processing either.
	   The ok/SA_AIS_ERR_REPAIR_PENDING is converted back to SA_AIS_OK in
	   the code block below.
	*/
	if (originatedAtThisNd) {
		immnd_client_node_get(cb, clnt_hdl, &cl_node);
		if (cl_node == NULL || cl_node->mIsStale) {
			LOG_WA("IMMND - Client went down so no response");
			return;
		}

		memset(&send_evt, '\0', sizeof(IMMSV_EVT));
		send_evt.type = IMMSV_EVT_TYPE_IMMA;

		if (error == SA_AIS_ERR_REPAIR_PENDING) {
			uint32_t rc = NCSCC_RC_SUCCESS;
			SaAisErrorT result = SA_AIS_OK;
			IMMSV_ADMIN_OPERATION_PARAM *rparams = NULL;
			IMMSV_ADMIN_OPERATION_PARAM *p;
			osafassert(!pbeExpected);
			TRACE(
			    "Ok reply for internally handled adminOp when PBE not configured");

			if (displayRes) {
				result = immModel_resourceDisplay(
				    cb, evt->info.admOpReq.params, &rparams);
			}

			send_evt.info.imma.type =
			    rparams ? IMMA_EVT_ND2A_ADMOP_RSP_2
				    : IMMA_EVT_ND2A_ADMOP_RSP;
			send_evt.info.imma.info.admOpRsp.invocation = saInv;
			send_evt.info.imma.info.admOpRsp.result = result;
			send_evt.info.imma.info.admOpRsp.error = SA_AIS_OK;
			send_evt.info.imma.info.admOpRsp.parms = rparams;
			if (async) {
				TRACE_2(
				    "ASYNCRONOUS special reply %llu %u %u to OM",
				    send_evt.info.imma.info.admOpRsp.invocation,
				    send_evt.info.imma.info.admOpRsp.result,
				    send_evt.info.imma.info.admOpRsp.error);
				rc = immnd_mds_msg_send(
				    cb, NCSMDS_SVC_ID_IMMA_OM,
				    cl_node->agent_mds_dest, &send_evt);
				TRACE_2("ASYNC REPLY SENT rc:%u", rc);
			} else {
				TRACE_2(
				    "NORMAL syncronous reply %llu %u to OM",
				    send_evt.info.imma.info.admOpRsp.invocation,
				    send_evt.info.imma.info.admOpRsp.result);
				rc = immnd_mds_send_rsp(
				    cb, &(cl_node->tmpSinfo), &send_evt);
				TRACE_2("SYNC REPLY SENT rc:%u", rc);
				if ((cb->mState ==
				     IMM_SERVER_CLUSTER_WAITING) &&
				    (cb->m2Pbe == 2) &&
				    (evt->info.admOpReq.operationId ==
				     OPENSAF_IMM_2PBE_PRELOAD_STAT)) {
					LOG_IN(
					    "2PBE Preload case: sending persistency stats to IMMD");
					/* process the params structure in the
					 * incoming message.*/
					immnd_extract_preload_params(
					    cb, &(evt->info.admOpReq));
				}
			}

			// Free memory
			while (rparams) {
				p = rparams;
				rparams = p->next;
				if (p->paramName.buf) {
					free(p->paramName.buf);
					p->paramName.buf = NULL;
					p->paramName.size = 0;
				}
				immsv_evt_free_att_val(&(p->paramBuffer),
						       p->paramType);
				p->next = NULL;
				free(p);
			}

			if (rc != NCSCC_RC_SUCCESS) {
				LOG_ER(
				    "Failure in sending reply for admin-op over MDS");
			}
		} else if (error != SA_AIS_OK) {
			if (async) {
				saInv = m_IMMSV_PACK_HANDLE(
				    evt->info.admOpReq.adminOwnerId,
				    evt->info.admOpReq.invocation);

				send_evt.info.imma.type =
				    IMMA_EVT_ND2A_ADMOP_RSP;
				send_evt.info.imma.info.admOpRsp.invocation =
				    saInv;
				send_evt.info.imma.info.admOpRsp.error = error;

				TRACE_2(
				    "IMMND sending error response to adminOperationInvokeAsync");
				if (immnd_mds_msg_send(
					cb, NCSMDS_SVC_ID_IMMA_OM,
					cl_node->agent_mds_dest,
					&send_evt) != NCSCC_RC_SUCCESS) {
					LOG_ER(
					    "MDS SEND FAILED on response to AdminOpInvokeAsync");
				}
			} else {
				send_evt.info.imma.type =
				    IMMA_EVT_ND2A_IMM_ERROR;
				send_evt.info.imma.info.errRsp.error = error;

				if (immnd_mds_send_rsp(cb, &(cl_node->tmpSinfo),
						       &send_evt) !=
				    NCSCC_RC_SUCCESS) {
					LOG_ER("MDS SEND FAILED");
				}
			}
		} else {
			TRACE_2(
			    "Delayed reply, wait for reply from implementer");
			/*Implementer may be on another node. */
		}
	}
	TRACE_LEAVE();
}

/****************************************************************************
 * Name          : immnd_evt_proc_class_create
 *
 * Description   : Function to process the saImmOmClassCreate/_2
 *                 operation from agent.
 *
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *                 bool originatedAtThisNode - Did it come from this node?
 *                 SaImmHandleT clnt_hdl - The client handle (only relevant if
 *                                         originatedAtThisNode is true).
 *                 IMM_DEST reply_dest - The dest of the ND to where reply
 *                                         is to be sent (only relevant if
 *                                       originatedAtThisNode is false).
 * Return Values : None
 *
 *****************************************************************************/
static void immnd_evt_proc_class_create(IMMND_CB *cb, IMMND_EVT *evt,
					bool originatedAtThisNd,
					SaImmHandleT clnt_hdl,
					MDS_DEST reply_dest)
{
	SaAisErrorT error = SA_AIS_OK;
	IMMSV_EVT send_evt;
	IMMND_IMM_CLIENT_NODE *cl_node = NULL;
	IMMND_IMM_CLIENT_NODE *pbe_cl_node = NULL;
	SaImmOiHandleT implHandle = 0LL;
	SaUint32T reqConn = m_IMMSV_UNPACK_HANDLE_HIGH(clnt_hdl);
	SaUint32T nodeId = m_IMMSV_UNPACK_HANDLE_LOW(clnt_hdl);
	bool delayedReply = false;
	SaUint32T pbeConn = 0;
	NCS_NODE_ID pbeNodeId = 0;
	NCS_NODE_ID *pbeNodeIdPtr = NULL;
	SaUint32T continuationId = 0;

#if 0  /*ABT DEBUG PRINTOUTS START */
	TRACE_2("ABT immnd_evt_proc_class_create:%s", evt->info.classDescr.className.buf);
	TRACE_2("ABT classCategory: %u ", evt->info.classDescr.classCategory);
	TRACE_2("ABT attrDefs:%p ", evt->info.classDescr.attrDefinitions);

	IMMSV_ATTR_DEF_LIST *p = evt->info.classDescr.attrDefinitions;

	while (p) {
		TRACE_2("ABT ATTRDEF, NAME: %s type:%u", p->d.attrName.buf, p->d.attrValueType);
		if (p->d.attrDefaultValue) {
			printImmValue(p->d.attrValueType, p->d.attrDefaultValue);
		}
		p = p->next;
	}
#endif /*ABT DEBUG PRINTOUTS STOP */

	if (cb->mPbeFile && (cb->mRim == SA_IMM_KEEP_REPOSITORY)) {
		pbeNodeIdPtr = &pbeNodeId;
		TRACE("We expect there to be a PBE");
		/* If pbeNodeIdPtr is NULL then classCreate skips the lookup
		   of the pbe implementer.
		*/
	}

	error = immModel_classCreate(cb, &(evt->info.classDescr),
				     originatedAtThisNd ? reqConn : 0, nodeId,
				     &continuationId, &pbeConn, pbeNodeIdPtr);

	if (pbeNodeId && error == SA_AIS_OK) {
		/*The persistent back-end is present => wait for reply. */
		delayedReply = true;
		if (pbeConn) {
			const char *osafImmDn = OPENSAF_IMM_OBJECT_DN;
			/*The persistent back-end is executing at THIS node. */
			osafassert(cb->mIsCoord);
			osafassert(pbeNodeId);
			osafassert(pbeNodeId == cb->node_id);
			implHandle = m_IMMSV_PACK_HANDLE(pbeConn, pbeNodeId);

			/*Fetch client node for PBE */
			immnd_client_node_get(cb, implHandle, &pbe_cl_node);
			osafassert(pbe_cl_node);
			if (pbe_cl_node->mIsStale) {
				LOG_WA(
				    "PBE is down => class create is delayed!");
				/* ****
				   An Mutation record should have been created
				   in ImmModel to reflect the class create. This
				   will bar the PBE being restarted with
				   --recover. TODO: Liveness check for PBE.
				*/
			} else {
				IMMSV_ADMIN_OPERATION_PARAM param;
				memset(&param, '\0',
				       sizeof(IMMSV_ADMIN_OPERATION_PARAM));
				memset(&send_evt, '\0', sizeof(IMMSV_EVT));
				send_evt.type = IMMSV_EVT_TYPE_IMMA;
				send_evt.info.imma.type =
				    IMMA_EVT_ND2A_IMM_PBE_ADMOP;
				send_evt.info.imma.info.admOpReq.adminOwnerId =
				    0; /* Only allowed for PBE. */
				send_evt.info.imma.info.admOpReq.operationId =
				    OPENSAF_IMM_PBE_CLASS_CREATE;

				/* TODO: This is a bit ugly, using the
				   continuationId to transport the immOiHandle.
				 */
				send_evt.info.imma.info.admOpReq
				    .continuationId = implHandle;
				send_evt.info.imma.info.admOpReq.invocation =
				    continuationId;
				send_evt.info.imma.info.admOpReq.timeout = 0;
				send_evt.info.imma.info.admOpReq.objectName
				    .size = (SaUint32T)strlen(osafImmDn) + 1;
				send_evt.info.imma.info.admOpReq.objectName
				    .buf = (char *)osafImmDn;
				send_evt.info.imma.info.admOpReq.params =
				    immnd_getOsafImmPbeAdmopParam(
					OPENSAF_IMM_PBE_CLASS_CREATE,
					&(evt->info.classDescr), &param);

				TRACE_2(
				    "MAKING PBE-IMPLEMENTER PERSISTENT CLASS CREATE upcall");
				if (immnd_mds_msg_send(
					cb, NCSMDS_SVC_ID_IMMA_OI,
					pbe_cl_node->agent_mds_dest,
					&send_evt) != NCSCC_RC_SUCCESS) {
					LOG_WA(
					    "Upcall over MDS for persistent class create "
					    "to PBE failed!");
					/* See comment **** above. */
				}
			}
			pbe_cl_node = NULL;
		}
	}

	if (originatedAtThisNd && !delayedReply) {
		immnd_client_node_get(cb, clnt_hdl, &cl_node);
		if (cl_node == NULL || cl_node->mIsStale) {
			LOG_WA("IMMND - Client %llu went down so no response",
			       clnt_hdl);
			return;
		}

		TRACE_2("Send immediate reply to client");
		if (error == SA_AIS_ERR_EXIST ||
		    error == SA_AIS_ERR_TRY_AGAIN) {
			/*Check if this is from a sync-agent and if so reverse
			 * logic in reply */
			if (cl_node->mIsSync) {
				error = SA_AIS_OK;
			}
		}

		if (cl_node->mIsSync) {
			if (cl_node->mSyncBlocked) {
				cl_node->mSyncBlocked = false;
			} else {
				LOG_ER(
				    "Unexpected class-create reply arrived destined for sync agent - discarding");
				return;
			}
		}

		memset(&send_evt, '\0', sizeof(IMMSV_EVT));
		send_evt.type = IMMSV_EVT_TYPE_IMMA;
		send_evt.info.imma.info.errRsp.error = error;
		send_evt.info.imma.type = IMMA_EVT_ND2A_IMM_ERROR;

		if (immnd_mds_send_rsp(cb, &(cl_node->tmpSinfo), &send_evt) !=
		    NCSCC_RC_SUCCESS) {
			TRACE_2("immnd_evt_class_create: SENDRSP FAIL");
		}
	}
	TRACE_LEAVE();
}

/****************************************************************************
 * Name          : immnd_evt_proc_class_delete
 *
 * Description   : Function to process the saImmOmClassDelete
 *                 operation from agent.
 *
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *                 bool originatedAtThisNode - Did it come from this node?
 *                 SaImmHandleT clnt_hdl - The client handle (only relevant if
 *                                         originatedAtThisNode is true).
 *                 IMM_DEST reply_dest - The dest of the ND to where reply
 *                                         is to be sent (only relevant if
 *                                         originatedAtThisNode is false).
 * Return Values : None
 *
 *****************************************************************************/
static void immnd_evt_proc_class_delete(IMMND_CB *cb, IMMND_EVT *evt,
					bool originatedAtThisNd,
					SaImmHandleT clnt_hdl,
					MDS_DEST reply_dest)
{
	SaAisErrorT error = SA_AIS_OK;
	IMMSV_EVT send_evt;
	IMMND_IMM_CLIENT_NODE *cl_node = NULL;
	IMMND_IMM_CLIENT_NODE *pbe_cl_node = NULL;
	SaImmOiHandleT implHandle = 0LL;
	SaUint32T reqConn = m_IMMSV_UNPACK_HANDLE_HIGH(clnt_hdl);
	SaUint32T nodeId = m_IMMSV_UNPACK_HANDLE_LOW(clnt_hdl);
	bool delayedReply = false;
	SaUint32T pbeConn = 0;
	NCS_NODE_ID pbeNodeId = 0;
	NCS_NODE_ID *pbeNodeIdPtr = NULL;
	SaUint32T continuationId = 0;

	TRACE_ENTER();

	if (cb->mPbeFile && (cb->mRim == SA_IMM_KEEP_REPOSITORY)) {
		pbeNodeIdPtr = &pbeNodeId;
		TRACE("We expect there to be a PBE");
		/* If pbeNodeIdPtr is NULL then classCreate skips the lookup
		   of the pbe implementer.
		*/
	}

	TRACE_2("immnd_evt_proc_class_delete:%s",
		evt->info.classDescr.className.buf);
	error = immModel_classDelete(cb, &(evt->info.classDescr),
				     originatedAtThisNd ? reqConn : 0, nodeId,
				     &continuationId, &pbeConn, pbeNodeIdPtr);

	if (pbeNodeId && error == SA_AIS_OK) {
		/*The persistent back-end is present => wait for reply. */
		delayedReply = true;
		if (pbeConn) {
			const char *osafImmDn = OPENSAF_IMM_OBJECT_DN;
			/*The persistent back-end is executing at THIS node. */
			osafassert(cb->mIsCoord);
			osafassert(pbeNodeId);
			osafassert(pbeNodeId == cb->node_id);
			implHandle = m_IMMSV_PACK_HANDLE(pbeConn, pbeNodeId);

			/*Fetch client node for PBE */
			immnd_client_node_get(cb, implHandle, &pbe_cl_node);
			osafassert(pbe_cl_node);
			if (pbe_cl_node->mIsStale) {
				LOG_WA(
				    "PBE is down => class delete is delayed!");
				/* ****
				   An Mutation record should have been created
				   in ImmModel to reflect the class create. This
				   will bar the PBE being restarted with
				   --recover. TODO: Liveness check for PBE.
				*/
			} else {
				IMMSV_ADMIN_OPERATION_PARAM param;
				memset(&param, '\0',
				       sizeof(IMMSV_ADMIN_OPERATION_PARAM));
				memset(&send_evt, '\0', sizeof(IMMSV_EVT));
				send_evt.type = IMMSV_EVT_TYPE_IMMA;
				send_evt.info.imma.type =
				    IMMA_EVT_ND2A_IMM_PBE_ADMOP;
				send_evt.info.imma.info.admOpReq.adminOwnerId =
				    0; /* Only allowed for PBE. */
				send_evt.info.imma.info.admOpReq.operationId =
				    OPENSAF_IMM_PBE_CLASS_DELETE;

				/* TODO: This is a bit ugly, using the
				   continuationId to transport the immOiHandle.
				 */
				send_evt.info.imma.info.admOpReq
				    .continuationId = implHandle;
				send_evt.info.imma.info.admOpReq.invocation =
				    continuationId;
				send_evt.info.imma.info.admOpReq.timeout = 0;
				send_evt.info.imma.info.admOpReq.objectName
				    .size = (SaUint32T)strlen(osafImmDn) + 1;
				send_evt.info.imma.info.admOpReq.objectName
				    .buf = (char *)osafImmDn;
				send_evt.info.imma.info.admOpReq.params =
				    immnd_getOsafImmPbeAdmopParam(
					OPENSAF_IMM_PBE_CLASS_DELETE,
					&(evt->info.classDescr), &param);

				TRACE_2(
				    "MAKING PBE-IMPLEMENTER PERSISTENT CLASS DELETE upcall");
				if (immnd_mds_msg_send(
					cb, NCSMDS_SVC_ID_IMMA_OI,
					pbe_cl_node->agent_mds_dest,
					&send_evt) != NCSCC_RC_SUCCESS) {
					LOG_WA(
					    "Upcall over MDS for persistent class delete "
					    "to PBE failed!");
					/* See comment **** above. */
				}
			}
			pbe_cl_node = NULL;
		}
	}

	if (originatedAtThisNd && !delayedReply) {
		immnd_client_node_get(cb, clnt_hdl, &cl_node);
		if (cl_node == NULL || cl_node->mIsStale) {
			LOG_WA("IMMND - Client %llu went down so no response",
			       clnt_hdl);
			return;
		}

		TRACE_2("Send immediate reply to client");

		memset(&send_evt, '\0', sizeof(IMMSV_EVT));
		send_evt.type = IMMSV_EVT_TYPE_IMMA;
		send_evt.info.imma.info.errRsp.error = error;
		send_evt.info.imma.type = IMMA_EVT_ND2A_IMM_ERROR;

		if (immnd_mds_send_rsp(cb, &(cl_node->tmpSinfo), &send_evt) !=
		    NCSCC_RC_SUCCESS) {
			LOG_WA(
			    "immnd_evt_class_delete: SENDRSP FAILED TO SEND");
		}
	}
	TRACE_LEAVE();
}

/****************************************************************************
 * Name          : immnd_evt_proc_sync_finalize
 *
 * Description   : Function to process the ImmOmObjectFinalizeSync call
 *                 operation from sync-process(loader)
 *
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *                 IMMSV_SEND_INFO *sinfo - sender info
 * Return Values : None
 *
 *****************************************************************************/

static uint32_t immnd_evt_proc_sync_finalize(IMMND_CB *cb, IMMND_EVT *evt,
					     IMMSV_SEND_INFO *sinfo)
{
	SaAisErrorT err = SA_AIS_OK;
	IMMSV_EVT send_evt;
	IMMND_IMM_CLIENT_NODE *cl_node = NULL;
	SaImmHandleT client_hdl;
	uint32_t proc_rc = NCSCC_RC_SUCCESS;
	char *tmpData = NULL;
	NCS_UBAID uba;
	uba.start = NULL;
	TRACE_ENTER();

	client_hdl = evt->info.finReq.client_hdl;
	immnd_client_node_get(cb, client_hdl, &cl_node);
	if (cl_node == NULL || cl_node->mIsStale) {
		LOG_WA("IMMND - Client went down so no response");
		return NCSCC_RC_SUCCESS; /* no problem for IMMND. */
	}

	if (cl_node->mIsSync) {
		osafassert(cb->mIsCoord);
		osafassert(!(cb->mSyncFinalizing));
		memset(&send_evt, 0,
		       sizeof(IMMSV_EVT)); /*No pointers=>no leak */
		send_evt.type = IMMSV_EVT_TYPE_IMMND;
		send_evt.info.immnd.type = IMMND_EVT_ND2ND_SYNC_FINALIZE_2;

		if (!immnd_is_immd_up(cb)) {
			LOG_WA(
			    "ERR_TRY_AGAIN: IMMD is down can not progress with finalizeSync");
			err = SA_AIS_ERR_TRY_AGAIN;
			goto fail;
		}
#if 0 /* Enable this code only to test failure of sync-process during sync */
		if (cb->mMyEpoch == 4) {
			LOG_NO("ABT FAULT INJECTION finalize sync fails");
			err = SA_AIS_ERR_BAD_OPERATION;
		} else
#endif

		err = immModel_finalizeSync(
		    cb, &send_evt.info.immnd.info.finSync, true, false);

		if (err != SA_AIS_OK) {
			LOG_ER("Failed to encode IMMND finalize sync message");
			/*If we fail in sync then restart the IMMND sync client.
			   This assumes that ImmModel::finalizeSync(,,T,F) does
			   not alter model state (to fully available) *if* if
			   returns error.
			 */

		} else {
			/*Pack message for fevs. */
			proc_rc = ncs_enc_init_space(&uba);
			if (proc_rc != NCSCC_RC_SUCCESS) {
				TRACE_2("Failed init ubaid");
				err = SA_AIS_ERR_NO_RESOURCES;
				goto fail;
			}

			proc_rc = immsv_evt_enc(&send_evt, &uba);
			if (proc_rc != NCSCC_RC_SUCCESS) {
				TRACE_2("Failed encode fevs");
				err = SA_AIS_ERR_NO_RESOURCES;
				goto fail;
			}

			int32_t size = uba.ttl;
			/* TODO: check against "payload max-size" ? */
			tmpData = malloc(size);
			osafassert(tmpData);
			char *data =
			    m_MMGR_DATA_AT_START(uba.start, size, tmpData);

			immnd_evt_destroy(&send_evt, false, __LINE__);

			memset(&send_evt, 0,
			       sizeof(IMMSV_EVT)); /*No pointers=>no leak */
			send_evt.type = IMMSV_EVT_TYPE_IMMND;
			send_evt.info.immnd.type = 0;
			send_evt.info.immnd.info.fevsReq.client_hdl =
			    client_hdl;
			send_evt.info.immnd.info.fevsReq.reply_dest =
			    cb->immnd_mdest_id;
			send_evt.info.immnd.info.fevsReq.msg.size = size;
			send_evt.info.immnd.info.fevsReq.msg.buf = data;

			proc_rc = immnd_evt_proc_fevs_forward(
			    cb, &send_evt.info.immnd, NULL, true, true);
			if (proc_rc != NCSCC_RC_SUCCESS) {
				TRACE_2("Failed send fevs message"); /*Error
									already
									logged
									in
									fevs_fo
								      */
				err = SA_AIS_ERR_NO_RESOURCES;
			}

			free(tmpData);
			cb->mSyncFinalizing = 0x1;
		}
	} else {
		LOG_ER(
		    "Will not allow sync messages from any process except sync process");
		err = SA_AIS_ERR_BAD_OPERATION;
	}

fail:
	memset(&send_evt, '\0', sizeof(IMMSV_EVT));
	send_evt.type = IMMSV_EVT_TYPE_IMMA;
	send_evt.info.imma.type = IMMA_EVT_ND2A_IMM_ERROR;
	send_evt.info.imma.info.errRsp.error = err;

	if (immnd_mds_send_rsp(cb, sinfo, &send_evt) != NCSCC_RC_SUCCESS) {
		LOG_ER("Send response over MDS failed");
	}

	if (uba.start) {
		m_MMGR_FREE_BUFR_LIST(uba.start);
	}

	TRACE_LEAVE();
	return proc_rc;
}

/****************************************************************************
 * Name          : immnd_evt_proc_object_sync
 *
 * Description   : Function to process the ImmOmObjectSync call
 *                 operation from sync-process(loader)
 *
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMND_EVT *evt - Received Event structure
 *                 bool originatedAtThisNode - Did it come from this node?
 *                 SaImmHandleT clnt_hdl - The client handle (only relevant if
 *                                         originatedAtThisNode is true).
 *                 IMM_DEST reply_dest - The dest of the ND to where reply
 *                                       is to be sent (only relevant if
 *                                       originatedAtThisNode is false).
 *                                       Return Values : None
 *
 *****************************************************************************/
static void immnd_evt_proc_object_sync(IMMND_CB *cb, IMMND_EVT *evt,
				       bool originatedAtThisNd,
				       SaImmHandleT clnt_hdl,
				       MDS_DEST reply_dest, SaUint64T msgNo)
{
	SaAisErrorT err = SA_AIS_OK;
	IMMSV_OM_CCB_OBJECT_MODIFY objModify;
	unsigned int isLocal = 0;
	TRACE_ENTER();

	if (cb
		->mSync) { /*This node is being synced => Accept the sync
			      message. */
		IMMSV_OM_OBJECT_SYNC *obj_sync = &(evt->info.obj_sync);
		do {
			err = immModel_objectSync(cb, obj_sync);
			if (err != SA_AIS_OK) {
				/*If we fail in sync then restart the IMMND sync
				 * client. */
				immnd_ackToNid(NCSCC_RC_FAILURE);
				if (err == SA_AIS_ERR_FAILED_OPERATION) {
					LOG_ER(
					    "Local error in objectSync for sync client - aborting");
					abort();
				} else {
					LOG_ER(
					    "Non local error %u in objectSync for sync client - exiting",
					    err);
					exit(1); /* Dont core dump as this was
						    not a local error */
				}
			}

			memset(&objModify, '\0',
			       sizeof(IMMSV_OM_CCB_OBJECT_MODIFY));
			while (immModel_fetchRtUpdate(cb, obj_sync, &objModify,
						      cb->syncFevsBase)) {
				LOG_IN(
				    "Applying deferred RTA update for object %s",
				    objModify.objectName.buf);
				err = immModel_rtObjectUpdate(
				    cb, &objModify, 0, 0, &isLocal, NULL, NULL,
				    NULL, NULL, NULL);
				// free(objModify.objectName.buf);
				immsv_free_attrmods(objModify.attrMods);
				memset(&objModify, '\0',
				       sizeof(IMMSV_OM_CCB_OBJECT_MODIFY));
				if (err != SA_AIS_OK) {
					LOG_ER(
					    "Failed to apply RTA update on object '%s' at sync client - aborting",
					    objModify.objectName.buf);
					immnd_ackToNid(NCSCC_RC_FAILURE);
					abort();
				}
			}
			obj_sync = obj_sync->next;
		} while (obj_sync);
	} /*else {TODO: I should verify that the class & object exists,
		   at least the object. }*/

	TRACE_LEAVE();
}

/****************************************************************************
 * Name          : immnd_evt_proc_rt_object_create
 *
 * Description   : Function to process the saImmOiRtObjectCreate/_2
 *                 operation received over fevs (cached/persisent RTAs).
 *
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *                 bool originatedAtThisNode - Did it come from this node?
 *                 SaImmHandleT clnt_hdl - The client handle (only relevant if
 *                                         originatedAtThisNode is true).
 *                 IMM_DEST reply_dest - The dest of the ND to where reply
 *                                       is to be sent (only relevant if
 *                                       originatedAtThisNode is false).
 * Return Values : None
 *
 *****************************************************************************/
static void immnd_evt_proc_rt_object_create(IMMND_CB *cb, IMMND_EVT *evt,
					    bool originatedAtThisNd,
					    SaImmHandleT clnt_hdl,
					    MDS_DEST reply_dest)
{
	SaAisErrorT err = SA_AIS_OK;
	IMMSV_EVT send_evt;
	IMMND_IMM_CLIENT_NODE *cl_node = NULL;
	IMMND_IMM_CLIENT_NODE *pbe_cl_node = NULL;
	SaImmOiHandleT implHandle = 0LL;
	SaUint32T reqConn = m_IMMSV_UNPACK_HANDLE_HIGH(clnt_hdl);
	SaUint32T nodeId = m_IMMSV_UNPACK_HANDLE_LOW(clnt_hdl);
	bool delayedReply = false;
	SaUint32T pbeConn = 0;
	SaUint32T pbe2BConn = 0;
	NCS_NODE_ID pbeNodeId = 0;
	NCS_NODE_ID *pbeNodeIdPtr = NULL;
	SaUint32T continuationId = 0;
	SaUint32T spApplConn = 0; /* Special applier locally connected. */
	TRACE_ENTER();

	if (cb->mPbeFile && (cb->mRim == SA_IMM_KEEP_REPOSITORY)) {
		pbeNodeIdPtr = &pbeNodeId;
		TRACE("We expect there to be a PBE");
		/* If pbeNodeIdPtr is NULL then rtObjectCreate skips the lookup
		   of the pbe implementer.
		 */
	}

	if (originatedAtThisNd) {
		err = immModel_rtObjectCreate(
		    cb, &(evt->info.objCreate), reqConn, nodeId,
		    &continuationId, &pbeConn, pbeNodeIdPtr, &spApplConn,
		    &pbe2BConn, evt->type == IMMND_EVT_A2ND_OI_OBJ_CREATE_2);
	} else {
		err = immModel_rtObjectCreate(
		    cb, &(evt->info.objCreate), 0, nodeId, &continuationId,
		    &pbeConn, pbeNodeIdPtr, &spApplConn, &pbe2BConn,
		    evt->type == IMMND_EVT_A2ND_OI_OBJ_CREATE_2);
	}

	if (pbeNodeId && err == SA_AIS_OK) {
		/*The persistent back-end is present => wait for reply. */
		delayedReply = true;
		if (pbeConn) {
			/*The persistent back-end is executing at THIS node. */
			osafassert(!pbe2BConn);
			osafassert(cb->mIsCoord);
			osafassert(pbeNodeId == cb->node_id);
			implHandle = m_IMMSV_PACK_HANDLE(pbeConn, pbeNodeId);

			/*Fetch client node for PBE */
			immnd_client_node_get(cb, implHandle, &pbe_cl_node);
			osafassert(pbe_cl_node);
			if (pbe_cl_node->mIsStale) {
				LOG_WA(
				    "PBE is down => persistify of rtObj create is delayed!");
				/* ****
				   TODO: send a fevs faked reply from PBE
				   indicating failure to update. This will
				   rollback the rtObjCreate. Currently it should
				   get cleaned up by cleanTheBasement, but that
				   will typically lead to ERR_TIMEOUT for the OI
				   client.
				*/
			} else {
				memset(&send_evt, '\0', sizeof(IMMSV_EVT));
				send_evt.type = IMMSV_EVT_TYPE_IMMA;
				/* PBE is internal => can handle long DNs */
				send_evt.info.imma.type =
				    IMMA_EVT_ND2A_OI_OBJ_CREATE_UC;
				send_evt.info.imma.info.objCreate =
				    evt->info.objCreate;
				send_evt.info.imma.info.objCreate.adminOwnerId =
				    continuationId;
				/*We re-use the adminOwner member of the
				  ccbCreate message to hold the continuation id.
				*/
				send_evt.info.imma.info.objCreate.immHandle =
				    implHandle;
				osafassert(evt->info.objCreate.ccbId == 0);

				TRACE_2(
				    "MAKING PBE-IMPLEMENTER PERSISTENT RT-OBJ CREATE upcall");
				if (immnd_mds_msg_send(
					cb, NCSMDS_SVC_ID_IMMA_OI,
					pbe_cl_node->agent_mds_dest,
					&send_evt) != NCSCC_RC_SUCCESS) {
					LOG_WA(
					    "Upcall over MDS for persistent rt obj create "
					    "to PBE failed!");
					/* See comment **** above. */
				}
			}
			implHandle = 0LL;
			pbe_cl_node = NULL;

		} else if (pbe2BConn) {
			osafassert(!(cb->mIsCoord));
			osafassert(pbeNodeId != cb->node_id);
			TRACE("PBE SLAVE at THIS node.");
			implHandle =
			    m_IMMSV_PACK_HANDLE(pbe2BConn, cb->node_id);

			/*Fetch client node for Slave PBE */
			immnd_client_node_get(cb, implHandle, &pbe_cl_node);
			osafassert(pbe_cl_node);
			if (pbe_cl_node->mIsStale) {
				LOG_WA(
				    "SLAVE PBE is down => persistify of rtObj create is delayed!");
				/* ****
				   TODO: send a fevs faked reply from PBE SLAVE
				   indicating failure to update. This will
				   rollback the rtObjCreate. Currently it should
				   get cleaned up by cleanTheBasement, but that
				   will typically lead to ERR_TIMEOUT for the OI
				   client. The primary PBE will fail to get ack
				   from slave and also fail.
				*/
			} else {
				memset(&send_evt, '\0', sizeof(IMMSV_EVT));
				send_evt.type = IMMSV_EVT_TYPE_IMMA;
				/* PBE2B is internal => can handle long DNs */
				send_evt.info.imma.type =
				    IMMA_EVT_ND2A_OI_OBJ_CREATE_UC;
				send_evt.info.imma.info.objCreate =
				    evt->info.objCreate;
				send_evt.info.imma.info.objCreate.adminOwnerId =
				    continuationId;
				/*We re-use the adminOwner member of the
				  ccbCreate message to hold the continuation id.
				*/
				send_evt.info.imma.info.objCreate.immHandle =
				    implHandle;
				osafassert(evt->info.objCreate.ccbId == 0);

				TRACE_2(
				    "MAKING PBE-SLAVE PERSISTENT RT-OBJ CREATE upcall");
				if (immnd_mds_msg_send(
					cb, NCSMDS_SVC_ID_IMMA_OI,
					pbe_cl_node->agent_mds_dest,
					&send_evt) != NCSCC_RC_SUCCESS) {
					LOG_WA(
					    "Upcall over MDS for persistent rt obj create "
					    "to Slave PBE failed!");
					/* See comment **** above. */
				}
			}
			implHandle = 0LL;
			pbe_cl_node = NULL;
		}

		if (spApplConn) {
			/* If we get here then there is a special applier
			   attached locally at this node, but this RTO create
			   was actually a PRTO create, PBE is enabled and
			   running on some node. The PBE has just been notified
			   of this PRTO create. The special applier callback has
			   to wait for the reply on the successfull commit of
			   the PRTO create from PBE. We need to save the pending
			   special applier callback data with the PRTO create
			   continuation.
			 */
			evt->info.objCreate.attrValues =
			    immModel_specialApplierTrimCreate(
				cb, spApplConn, &(evt->info.objCreate));
			immModel_specialApplierSavePrtoCreateAttrs(
			    cb, &(evt->info.objCreate), continuationId);
		}
	}

	if (spApplConn && (err == SA_AIS_OK) && !delayedReply) {
		/* Indicates object is marked with SA_IMM_ATTR_NOTIFY and
		   special applier is present at this node and we dont need to
		   wait for ack from PBE (non persistent RTO or PBE not
		   enabled).*/
		implHandle = m_IMMSV_PACK_HANDLE(spApplConn, cb->node_id);
		/*Fetch client node for Special applier OI ! */
		cl_node = 0LL;
		immnd_client_node_get(cb, implHandle, &cl_node);
		osafassert(cl_node != NULL);

		TRACE_2("Special applier needs to be notified of RTO create.");
		memset(&send_evt, '\0', sizeof(IMMSV_EVT));
		send_evt.type = IMMSV_EVT_TYPE_IMMA;
		/* Assuming special applier is long DN capable. */
		send_evt.info.imma.type = IMMA_EVT_ND2A_OI_OBJ_CREATE_UC;
		send_evt.info.imma.info.objCreate = evt->info.objCreate;
		send_evt.info.imma.info.objCreate.adminOwnerId = 0;
		send_evt.info.imma.info.objCreate.immHandle = implHandle;
		osafassert(evt->info.objCreate.ccbId == 0);
		evt->info.objCreate.attrValues =
		    send_evt.info.imma.info.objCreate.attrValues =
			immModel_specialApplierTrimCreate(
			    cb, spApplConn, &(evt->info.objCreate));

		if (cl_node->mIsStale) {
			LOG_WA(
			    "Special applier client went down so create upcall not sent");
		} else if (immnd_mds_msg_send(cb, NCSMDS_SVC_ID_IMMA_OI,
					      cl_node->agent_mds_dest,
					      &send_evt) != NCSCC_RC_SUCCESS) {
			LOG_WA("RTO Create upcall for special applier failed");
			/* This is an unhandled message loss case!*/
		}
	}

	if (originatedAtThisNd && !delayedReply) {
		immnd_client_node_get(cb, clnt_hdl, &cl_node);
		if (cl_node == NULL || cl_node->mIsStale) {
			LOG_WA("IMMND - Client went down so no response");
			return;
		}

		TRACE_2("send immediate reply to client/agent");
		memset(&send_evt, '\0', sizeof(IMMSV_EVT));
		send_evt.type = IMMSV_EVT_TYPE_IMMA;
		send_evt.info.imma.info.errRsp.error = err;
		send_evt.info.imma.type = IMMA_EVT_ND2A_IMM_ERROR;

		if (immnd_mds_send_rsp(cb, &(cl_node->tmpSinfo), &send_evt) !=
		    NCSCC_RC_SUCCESS) {
			LOG_WA("Failed to send result to implementer over MDS");
		}
	}
	TRACE_LEAVE();
}

/****************************************************************************
 * Name          : immnd_evt_proc_object_create
 *
 * Description   : Function to process the saImmOmCcbObjectCreate/_2
 *                 operation from agent.
 *
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *                 bool originatedAtThisNode - Did it come from this node?
 *                 SaImmHandleT clnt_hdl - The client handle (only relevant if
 *                                         originatedAtThisNode is true).
 *                 IMM_DEST reply_dest - The dest of the ND to where reply
 *                                         is to be sent (only relevant if
 *                                       originatedAtThisNode is false).
 * Return Values : None
 *
 *****************************************************************************/
static void immnd_evt_proc_object_create(IMMND_CB *cb, IMMND_EVT *evt,
					 bool originatedAtThisNd,
					 SaImmHandleT clnt_hdl,
					 MDS_DEST reply_dest)
{
	SaAisErrorT err = SA_AIS_OK;
	IMMSV_EVT send_evt;
	IMMND_IMM_CLIENT_NODE *cl_node = NULL;

	IMMND_IMM_CLIENT_NODE *oi_cl_node = NULL;
	SaImmOiHandleT implHandle = 0LL;
	SaUint32T implConn = 0;
	NCS_NODE_ID implNodeId = 0;
	SaUint32T continuationId = 0;
	bool delayedReply = false;
	SaUint32T pbeConn = 0;
	NCS_NODE_ID pbeNodeId = 0;
	NCS_NODE_ID *pbeNodeIdPtr = NULL;
	SaNameT objName;
	osaf_extended_name_clear(&objName);
	bool dnOrRdnIsLong = false;
	TRACE_ENTER();

#if 0  /*ABT DEBUG PRINTOUTS START */
	TRACE_2("ABT immnd_evt_proc_object_create class:%s", evt->info.objCreate.className.buf);
	TRACE_2("ABT immnd_evt_proc_object_create parent:%s", evt->info.objCreate.parentName.buf);
	TRACE_2("ABT immnd_evt_proc_object_create CCB:%u ADMOWN:%u",
		evt->info.objCreate.ccbId, evt->info.objCreate.adminOwnerId);

	TRACE_2("ABT attrvalues:%p ", evt->info.objCreate.attrValues);

	IMMSV_ATTR_VALUES_LIST *p = evt->info.objCreate.attrValues;

	while (p) {
		TRACE_2("ABT FOUND AN ATTRIBUTE, NAME: %s type:%u vlaues:%u",
			p->n.attrName.buf, p->n.attrValueType, p->n.attrValuesNumber);
		if (p->n.attrValuesNumber) {
			printImmValue(p->n.attrValueType, &(p->n.attrValue));
		}
		p = p->next;
	}
#endif /*ABT DEBUG PRINTOUTS STOP */
	if (cb->mPbeFile && (cb->mRim == SA_IMM_KEEP_REPOSITORY)) {
		pbeNodeIdPtr = &pbeNodeId;
		/* If pbeNodeIdPtr is NULL then ccbObjectCreate skips the lookup
		   of the pbe implementer.
		 */
	}

	err = immModel_ccbObjectCreate(
	    cb, &(evt->info.objCreate), &implConn, &implNodeId, &continuationId,
	    &pbeConn, pbeNodeIdPtr, &objName, &dnOrRdnIsLong,
	    evt->type == IMMND_EVT_A2ND_OBJ_CREATE_2);

	if (pbeNodeIdPtr && pbeConn && err == SA_AIS_OK) {
		/*The persistent back-end is present and executing at THIS node.
		 */
		osafassert(cb->mIsCoord);
		osafassert(pbeNodeId);
		osafassert(pbeNodeId == cb->node_id);
		implHandle = m_IMMSV_PACK_HANDLE(pbeConn, pbeNodeId);

		/*Fetch client node for PBE */
		immnd_client_node_get(cb, implHandle, &oi_cl_node);
		osafassert(oi_cl_node);
		if (oi_cl_node->mIsStale) {
			LOG_WA("PBE is down => ccb %u fails",
			       evt->info.objCreate.ccbId);
			/* This is an asymetric failure, dangerous because we
			   are not waiting on reply for the modify UC to PBE.
			   But the op count plus forced wait on PBE-completed,
			   should prevent any apply to succeed.
			*/
			err = SA_AIS_ERR_FAILED_OPERATION;
			immModel_setCcbErrorString(
			    cb, evt->info.objCreate.ccbId,
			    IMM_RESOURCE_ABORT "PBE is down");
			immnd_proc_global_abort_ccb(cb,
						    evt->info.objCreate.ccbId);
		} else {
			memset(&send_evt, '\0', sizeof(IMMSV_EVT));
			send_evt.type = IMMSV_EVT_TYPE_IMMA;
			/* PBE is internal => can handle long DNs */
			send_evt.info.imma.type =
			    IMMA_EVT_ND2A_OI_OBJ_CREATE_UC;
			send_evt.info.imma.info.objCreate = evt->info.objCreate;
			send_evt.info.imma.info.objCreate.adminOwnerId = 0;
			/*We re-use the adminOwner member of the ccbCreate
			  message to hold the invocation id. In this case, 0 =>
			  no reply is expected. */

			send_evt.info.imma.info.objCreate.immHandle =
			    implHandle;

			TRACE_2("MAKING PBE-IMPLEMENTER OBJ CREATE upcall");
			if (immnd_mds_msg_send(cb, NCSMDS_SVC_ID_IMMA_OI,
					       oi_cl_node->agent_mds_dest,
					       &send_evt) != NCSCC_RC_SUCCESS) {
				LOG_ER("Upcall over MDS for ccbObjectCreate "
				       "to PBE failed! - aborting");
				err = SA_AIS_ERR_FAILED_OPERATION;
				immModel_setCcbErrorString(
				    cb, evt->info.objCreate.ccbId,
				    IMM_RESOURCE_ABORT
				    "Upcall over MDS to PBE failed");
				immnd_proc_global_abort_ccb(
				    cb, evt->info.objCreate.ccbId);
			}
			implHandle = 0LL;
			oi_cl_node = NULL;
		}
	}

	if (err == SA_AIS_OK && implNodeId) {
		/*We have an implementer (somewhere) */
		delayedReply = true;
		if (implConn) {
			/*The implementer is local, make the up-call */
			osafassert(implNodeId == cb->node_id);
			implHandle = m_IMMSV_PACK_HANDLE(implConn, implNodeId);

			/*Fetch client node for OI ! */
			immnd_client_node_get(cb, implHandle, &oi_cl_node);
			if (oi_cl_node == NULL || oi_cl_node->mIsStale) {
				LOG_WA("Client died");
				err = SA_AIS_ERR_FAILED_OPERATION;
				delayedReply = false;
				immModel_setCcbErrorString(
				    cb, evt->info.objCreate.ccbId,
				    IMM_RESOURCE_ABORT "Client died");
			} else {
				memset(&send_evt, '\0', sizeof(IMMSV_EVT));
				send_evt.type = IMMSV_EVT_TYPE_IMMA;
				send_evt.info.imma.type =
				    dnOrRdnIsLong
					? IMMA_EVT_ND2A_OI_OBJ_CREATE_LONG_UC
					: IMMA_EVT_ND2A_OI_OBJ_CREATE_UC;
				send_evt.info.imma.info.objCreate =
				    evt->info.objCreate;
				send_evt.info.imma.info.objCreate.adminOwnerId =
				    continuationId;
				/*We re-use the adminOwner member of the
				   ccbCreate message to hold the invocation id.
				 */

				/*immHandle needed on agent side to find correct
				   node. This to send the message over process
				   internal IPC (sigh). */
				send_evt.info.imma.info.objCreate.immHandle =
				    implHandle;

				TRACE_2(
				    "MAKING OI-IMPLEMENTER OBJ CREATE upcall towards agent");
				if (immnd_mds_msg_send(
					cb, NCSMDS_SVC_ID_IMMA_OI,
					oi_cl_node->agent_mds_dest,
					&send_evt) != NCSCC_RC_SUCCESS) {
					LOG_ER(
					    "Agent upcall over MDS for ccbObjectCreate failed");
					err = SA_AIS_ERR_FAILED_OPERATION;
					immModel_setCcbErrorString(
					    cb, evt->info.objCreate.ccbId,
					    IMM_RESOURCE_ABORT
					    "Agent upcall over MDS failed");
				}
			}
		}
	}

	if (!osaf_is_extended_name_empty(&objName) && (err == SA_AIS_OK)) {
		/* Generate applier upcalls for the object create */
		SaUint32T *applConnArr = NULL;
		SaUint32T arrSize = immModel_getLocalAppliersForObj(
		    cb, &objName, evt->info.objCreate.ccbId, &applConnArr,
		    false);

		if (arrSize) {
			memset(&send_evt, '\0', sizeof(IMMSV_EVT));
			send_evt.type = IMMSV_EVT_TYPE_IMMA;
			send_evt.info.imma.type =
			    dnOrRdnIsLong ? IMMA_EVT_ND2A_OI_OBJ_CREATE_LONG_UC
					  : IMMA_EVT_ND2A_OI_OBJ_CREATE_UC;
			send_evt.info.imma.info.objCreate = evt->info.objCreate;
			send_evt.info.imma.info.objCreate.adminOwnerId = 0;
			/* Re-use the adminOwner member of the ccbCreate message
			  to hold the invocation id. In this case, 0 => no reply
			  is expected. */
			int ix = 0;
			for (; ix < arrSize && err == SA_AIS_OK; ++ix) {
				implHandle = m_IMMSV_PACK_HANDLE(
				    applConnArr[ix], cb->node_id);
				send_evt.info.imma.info.objCreate.immHandle =
				    implHandle;

				if (ix == (arrSize - 1)) {
					/* Last local applier may be a special
					   applier. If so then attribute-list
					   will be trimmed for special applier.
					   If not special applier, then
					   attribute-list will be untouched
					   since the last applier is then a
					   regular applier.
					 */
					evt->info.objCreate
					    .attrValues = send_evt.info.imma
							      .info.objCreate
							      .attrValues =
					    immModel_specialApplierTrimCreate(
						cb, applConnArr[ix],
						&(evt->info.objCreate));
				}

				/*Fetch client node for Applier OI ! */
				immnd_client_node_get(cb, implHandle,
						      &oi_cl_node);
				osafassert(oi_cl_node != NULL);
				if (oi_cl_node->mIsStale) {
					LOG_WA(
					    "Applier client went down so create upcall not sent");
					continue;
				} else if (immnd_mds_msg_send(
					       cb, NCSMDS_SVC_ID_IMMA_OI,
					       oi_cl_node->agent_mds_dest,
					       &send_evt) != NCSCC_RC_SUCCESS) {
					LOG_WA(
					    "Create upcall for applier failed");
				}
			}

			free(applConnArr);
			applConnArr = NULL;
		}
	}

	if (originatedAtThisNd && !delayedReply) {
		immnd_client_node_get(cb, clnt_hdl, &cl_node);
		if (cl_node == NULL || cl_node->mIsStale) {
			LOG_WA("IMMND - Client went down so no response");
			osaf_extended_name_free(&objName);
			return;
		}

		TRACE_2("send immediate reply to client/agent");
		memset(&send_evt, '\0', sizeof(IMMSV_EVT));
		send_evt.type = IMMSV_EVT_TYPE_IMMA;
		send_evt.info.imma.info.errRsp.error = err;
		send_evt.info.imma.info.errRsp.errStrings =
		    immModel_ccbGrabErrStrings(cb, evt->info.objCreate.ccbId);

		if (send_evt.info.imma.info.errRsp.errStrings) {
			send_evt.info.imma.type = IMMA_EVT_ND2A_IMM_ERROR_2;
		} else {
			send_evt.info.imma.type = IMMA_EVT_ND2A_IMM_ERROR;
		}

		if (immnd_mds_send_rsp(cb, &(cl_node->tmpSinfo), &send_evt) !=
		    NCSCC_RC_SUCCESS) {
			LOG_WA("Failed to send result to Agent over MDS");
		}
		immsv_evt_free_attrNames(
		    send_evt.info.imma.info.errRsp.errStrings);
	}
	osaf_extended_name_free(&objName);
	TRACE_LEAVE();
}

/****************************************************************************
 * Name          : immnd_evt_proc_object_modify
 *
 * Description   : Function to process the saImmOmCcbObjectModify/_2
 *                 operation from agent. Arrives over FEVS.
 *
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *                 bool originatedAtThisNode - Did it come from this node?
 *                 SaImmHandleT clnt_hdl - The client handle (only relevant if
 *                                         originatedAtThisNode is true).
 *                 IMM_DEST reply_dest - The dest of the ND to where reply
 *                                         is to be sent (only relevant if
 *                                       originatedAtThisNode is false).
 * Return Values : None
 *
 *****************************************************************************/
static void immnd_evt_proc_object_modify(IMMND_CB *cb, IMMND_EVT *evt,
					 bool originatedAtThisNd,
					 SaImmHandleT clnt_hdl,
					 MDS_DEST reply_dest)
{
	SaAisErrorT err = SA_AIS_OK;
	IMMSV_EVT send_evt;
	IMMND_IMM_CLIENT_NODE *cl_node = NULL;

	IMMND_IMM_CLIENT_NODE *oi_cl_node = NULL;
	SaImmOiHandleT implHandle = 0LL;
	bool delayedReply = false;
	SaUint32T implConn = 0;
	NCS_NODE_ID implNodeId = 0;
	SaUint32T continuationId = 0;
	SaUint32T pbeConn = 0;
	NCS_NODE_ID pbeNodeId = 0;
	NCS_NODE_ID *pbeNodeIdPtr = NULL;
	SaNameT objName;
	osaf_extended_name_clear(&objName);
	bool hasLongDns = false;
	/* These 2 attr-mods lists will only be generated on demand */
	IMMSV_ATTR_MODS_LIST *canonicalizedAttrMod = NULL;
	IMMSV_ATTR_MODS_LIST *allWritableAttr = NULL;
	/* Used when canonicalizing all writable attributes */
	bool writableAttrHasLongDns = false;

	TRACE_ENTER();
#if 0  /*ABT DEBUG PRINTOUTS START */
	TRACE_2("ABT immnd_evt_proc_object_modify object:%s", evt->info.objModify.objectName.buf);
	TRACE_2("ABT immnd_evt_proc_object_modify CCB:%u ADMOWN:%u",
		evt->info.objModify.ccbId, evt->info.objModify.adminOwnerId);

	TRACE_2("ABT attrMods:%p ", evt->info.objModify.attrMods);

	IMMSV_ATTR_MODS_LIST *p = evt->info.objModify.attrMods;

	while (p) {
		TRACE_2("ABT FOUND AN ATTRIBUTE-MOD, MOD_TYPE:%u NAME: %s type:%ld "
			"values:%ld", p->attrModType,
			p->attrValue.attrName.buf, p->attrValue.attrValueType, p->attrValue.attrValuesNumber);
		if (p->attrValue.attrValuesNumber) {
			printImmValue(p->attrValue.attrValueType, &(p->attrValue.attrValue));
		}
		p = p->next;
	}
#endif /*ABT DEBUG PRINTOUTS STOP */

	if (cb->mPbeFile && (cb->mRim == SA_IMM_KEEP_REPOSITORY)) {
		pbeNodeIdPtr = &pbeNodeId;
		/* If pbeNodeIdPtr is NULL then ccbObjectModify skips
		   the lookup of the pbe implementer.
		 */
	}

	err = immModel_ccbObjectModify(cb, &(evt->info.objModify), &implConn,
				       &implNodeId, &continuationId, &pbeConn,
				       pbeNodeIdPtr, &objName, &hasLongDns);

	/* If 'hasLongDns' is true, allWritableAttr will also contains long DN
	 */
	writableAttrHasLongDns = hasLongDns;

	if (pbeNodeIdPtr && pbeConn && err == SA_AIS_OK) {
		/*The persistent back-end is present and executing at THIS node.
		 */
		osafassert(cb->mIsCoord);
		osafassert(pbeNodeId);
		osafassert(pbeNodeId == cb->node_id);
		implHandle = m_IMMSV_PACK_HANDLE(pbeConn, pbeNodeId);

		/*Fetch client node for PBE */
		immnd_client_node_get(cb, implHandle, &oi_cl_node);
		osafassert(oi_cl_node);
		if (oi_cl_node->mIsStale) {
			LOG_WA("PBE is down => ccb %u fails",
			       evt->info.objModify.ccbId);
			/* This is an asymetric failure, dangerous because we
			   are not waiting on reply for the modify UC to PBE.
			   But the op count plus forced wait on PBE-completed,
			   should prevent any apply to succeed.
			*/
			err = SA_AIS_ERR_FAILED_OPERATION;
			immModel_setCcbErrorString(
			    cb, evt->info.objModify.ccbId,
			    IMM_RESOURCE_ABORT "PBE is down");
			immnd_proc_global_abort_ccb(cb,
						    evt->info.objModify.ccbId);
		} else {
			memset(&send_evt, '\0', sizeof(IMMSV_EVT));
			send_evt.type = IMMSV_EVT_TYPE_IMMA;
			/* PBE is internal => can handle long DNs */
			send_evt.info.imma.type =
			    IMMA_EVT_ND2A_OI_OBJ_MODIFY_UC;
			send_evt.info.imma.info.objModify = evt->info.objModify;
			canonicalizedAttrMod =
			    immModel_canonicalizeAttrModification(
				cb, &(evt->info.objModify));
			send_evt.info.imma.info.objModify.attrMods =
			    canonicalizedAttrMod;
			send_evt.info.imma.info.objModify.adminOwnerId = 0;
			/*We re-use the adminOwner member of the ccbModify
			  message to hold the invocation id. In this case, 0 =>
			  no reply is expected. */

			send_evt.info.imma.info.objModify.immHandle =
			    implHandle;

			TRACE_2("MAKING PBE-IMPLEMENTER OBJ MODIFY upcall");
			if (immnd_mds_msg_send(cb, NCSMDS_SVC_ID_IMMA_OI,
					       oi_cl_node->agent_mds_dest,
					       &send_evt) != NCSCC_RC_SUCCESS) {
				LOG_ER("Upcall over MDS for ccbObjectModify "
				       "to PBE failed! - aborting");
				err = SA_AIS_ERR_FAILED_OPERATION;
				immModel_setCcbErrorString(
				    cb, evt->info.objModify.ccbId,
				    IMM_RESOURCE_ABORT
				    "Upcall over MDS to PBE failed");
				immnd_proc_global_abort_ccb(
				    cb, evt->info.objModify.ccbId);
			}
			implHandle = 0LL;
			oi_cl_node = NULL;
		}
	}

	if (err == SA_AIS_OK && implNodeId) {
		/*We have an implementer (somewhere) */
		delayedReply = true;
		if (implConn) {
			/*The implementer is local, make the up-call */
			osafassert(implNodeId == cb->node_id);
			implHandle = m_IMMSV_PACK_HANDLE(implConn, implNodeId);

			/*Fetch client node for OI ! */
			immnd_client_node_get(cb, implHandle, &oi_cl_node);
			osafassert(oi_cl_node != NULL);
			if (oi_cl_node->mIsStale) {
				LOG_WA(
				    "OI Client went down so no modify upcall");
				err = SA_AIS_ERR_FAILED_OPERATION;
				delayedReply = false;
				immModel_setCcbErrorString(
				    cb, evt->info.objModify.ccbId,
				    IMM_RESOURCE_ABORT "OI client went down");
			} else {
				memset(&send_evt, '\0', sizeof(IMMSV_EVT));
				send_evt.type = IMMSV_EVT_TYPE_IMMA;
				send_evt.info.imma.type =
				    hasLongDns
					? IMMA_EVT_ND2A_OI_OBJ_MODIFY_LONG_UC
					: IMMA_EVT_ND2A_OI_OBJ_MODIFY_UC;

				send_evt.info.imma.info.objModify =
				    evt->info.objModify;

				/* For A.2.17 or later */
				if (oi_cl_node->version.minorVersion >= 0x11 &&
				    oi_cl_node->version.majorVersion == 0x2 &&
				    oi_cl_node->version.releaseCode == 'A') {
					if (!canonicalizedAttrMod) { /* Check if
									canonicalizedAttrMod
									is
									already
									built */
						canonicalizedAttrMod =
						    immModel_canonicalizeAttrModification(
							cb,
							&(evt->info.objModify));
					}
					send_evt.info.imma.info.objModify
					    .attrMods = canonicalizedAttrMod;
				}

				/* shallow copy into stack alocated structure.
				 */

				send_evt.info.imma.info.objModify.adminOwnerId =
				    continuationId;
				/*We re-use the adminOwner member of the
				   ccbModify message to hold the continuation
				   id. */

				/*immHandle needed on agent side to find correct
				   node. This to send the message over process
				   internal IPC . */
				send_evt.info.imma.info.objModify.immHandle =
				    implHandle;

				TRACE_2(
				    "MAKING OI-IMPLEMENTER OBJ MODIFY upcall towards agent");
				if (immnd_mds_msg_send(
					cb, NCSMDS_SVC_ID_IMMA_OI,
					oi_cl_node->agent_mds_dest,
					&send_evt) != NCSCC_RC_SUCCESS) {
					LOG_ER(
					    "Agent upcall over MDS for ccbObjectModify failed");
					err = SA_AIS_ERR_FAILED_OPERATION;
					immModel_setCcbErrorString(
					    cb, evt->info.objModify.ccbId,
					    IMM_RESOURCE_ABORT
					    "Agent upcall over MDS failed");
				}
			}
		}
	}

	if (!osaf_is_extended_name_empty(&objName) && (err == SA_AIS_OK)) {
		/* Generate applier upcalls for the object modify */
		SaUint32T *applConnArr = NULL;
		SaUint32T arrSize = immModel_getLocalAppliersForObj(
		    cb, &objName, evt->info.objModify.ccbId, &applConnArr,
		    false);
		SaUint32T pbeApplierConn = immModel_getPbeApplierConn(
		    cb); /* 0 if not local or not exist */

		if (arrSize) {
			memset(&send_evt, '\0', sizeof(IMMSV_EVT));
			send_evt.type = IMMSV_EVT_TYPE_IMMA;
			send_evt.info.imma.info.objModify = evt->info.objModify;
			send_evt.info.imma.info.objModify.adminOwnerId = 0;
			/* Re-use the adminOwner member of the ccbModify message
			  to hold the invocation id. In this case, 0 => no reply
			  is expected. */
			int ix = 0;
			for (; ix < arrSize && err == SA_AIS_OK; ++ix) {
				bool isSpecialApplier = false;
				send_evt.info.imma.info.objModify.attrMods =
				    evt->info.objModify.attrMods;
				send_evt.info.imma.type =
				    hasLongDns
					? IMMA_EVT_ND2A_OI_OBJ_MODIFY_LONG_UC
					: IMMA_EVT_ND2A_OI_OBJ_MODIFY_UC;
				implHandle = m_IMMSV_PACK_HANDLE(
				    applConnArr[ix], cb->node_id);
				send_evt.info.imma.info.objModify.immHandle =
				    implHandle;

				if (ix == (arrSize - 1)) {
					/* Last local applier may be a special
					   applier. If so then attribute-list
					   will be trimmed for special applier.
					   If not special applier, then
					   attribute-list will be untouched
					   since the last applier is then a
					   regular applier.
					 */
					send_evt.info.imma.info.objModify
					    .attrMods =
					    immModel_specialApplierTrimModify(
						cb, applConnArr[ix],
						&(evt->info.objModify));

					/* If attrMods of 'send_evt' is
					 * different from attrMods of 'evt' then
					 * we are sending to the special
					 * applier. */
					if (send_evt.info.imma.info.objModify
						.attrMods !=
					    evt->info.objModify.attrMods) {
						isSpecialApplier = true;
						evt->info.objModify.attrMods =
						    send_evt.info.imma.info
							.objModify.attrMods;
					}
				}

				/*Fetch client node for Applier OI ! */
				immnd_client_node_get(cb, implHandle,
						      &oi_cl_node);
				osafassert(oi_cl_node != NULL);

				/* For A.2.17 or later */
				if (!isSpecialApplier &&
				    oi_cl_node->version.minorVersion >= 0x11 &&
				    oi_cl_node->version.majorVersion == 0x2 &&
				    oi_cl_node->version.releaseCode == 'A') {
					if (applConnArr[ix] ==
					    pbeApplierConn) { /* Slave pbe */
						if (!canonicalizedAttrMod) { /* Must already be built for primary pbe */
							canonicalizedAttrMod =
							    immModel_canonicalizeAttrModification(
								cb,
								&(evt->info
								      .objModify));
						}
						send_evt.info.imma.info
						    .objModify.attrMods =
						    canonicalizedAttrMod;
					} else {
						if (!allWritableAttr) { /* Check
									   if
									   allWritableAttr
									   is
									   already
									   built
									 */
							allWritableAttr =
							    immModel_getAllWritableAttributes(
								cb,
								&(evt->info
								      .objModify),
								&writableAttrHasLongDns);
						}
						send_evt.info.imma.info
						    .objModify.attrMods =
						    allWritableAttr;
						send_evt.info.imma.type =
						    writableAttrHasLongDns
							? IMMA_EVT_ND2A_OI_OBJ_MODIFY_LONG_UC
							: IMMA_EVT_ND2A_OI_OBJ_MODIFY_UC;
					}
				}
				/* Slave pbe is initialized with latest OI api
				 * version, it will also receive canonicalized
				 * attrMod like the primary pbe
				 */

				if (oi_cl_node->mIsStale) {
					LOG_WA(
					    "Applier client went down so modify upcall not sent");
					continue;
				} else if (immnd_mds_msg_send(
					       cb, NCSMDS_SVC_ID_IMMA_OI,
					       oi_cl_node->agent_mds_dest,
					       &send_evt) != NCSCC_RC_SUCCESS) {
					LOG_WA(
					    "Modify upcall for applier failed");
				}
			}

			free(applConnArr);
			applConnArr = NULL;
		}
	}

	if (originatedAtThisNd && !delayedReply) {
		immnd_client_node_get(cb, clnt_hdl, &cl_node);
		if (cl_node == NULL || cl_node->mIsStale) {
			LOG_WA("IMMND - Client went down so no response");
			goto done; /* Dont leak memory on error */
		}

		TRACE_2("send immediate reply to client/agent");
		memset(&send_evt, '\0', sizeof(IMMSV_EVT));
		send_evt.type = IMMSV_EVT_TYPE_IMMA;
		send_evt.info.imma.info.errRsp.error = err;
		send_evt.info.imma.info.errRsp.errStrings =
		    immModel_ccbGrabErrStrings(cb, evt->info.objModify.ccbId);

		if (send_evt.info.imma.info.errRsp.errStrings) {
			send_evt.info.imma.type = IMMA_EVT_ND2A_IMM_ERROR_2;
		} else {
			send_evt.info.imma.type = IMMA_EVT_ND2A_IMM_ERROR;
		}

		if (immnd_mds_send_rsp(cb, &(cl_node->tmpSinfo), &send_evt) !=
		    NCSCC_RC_SUCCESS) {
			LOG_WA("Failed to send result to Agent over MDS");
		}
		immsv_evt_free_attrNames(
		    send_evt.info.imma.info.errRsp.errStrings);
	}

done:
	/* Free the canonicalized attr mods */
	immsv_free_attrmods(canonicalizedAttrMod);
	canonicalizedAttrMod = NULL;
	immsv_free_attrmods(allWritableAttr);
	allWritableAttr = NULL;
	osaf_extended_name_free(&objName);
	TRACE_LEAVE();
}

/****************************************************************************
 * Name          : immnd_evt_proc_rt_object_modify
 *
 * Description   : Function to process the saImmOiRtObjectUpdate/_2
 *                 operation from agent. Arrives over FEVS.
 *
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *                 bool originatedAtThisNode - Did it come from this node?
 *                 SaImmHandleT clnt_hdl - The client handle (only relevant if
 *                                         originatedAtThisNode is true).
 *                 IMM_DEST reply_dest - The dest of the ND to where reply
 *                                         is to be sent (only relevant if
 *                                       originatedAtThisNode is false).
 * Return Values : None
 *
 *****************************************************************************/
static void immnd_evt_proc_rt_object_modify(IMMND_CB *cb, IMMND_EVT *evt,
					    bool originatedAtThisNd,
					    SaImmHandleT clnt_hdl,
					    MDS_DEST reply_dest,
					    SaUint64T msgNo)
{
	SaAisErrorT err = SA_AIS_OK;
	IMMSV_EVT send_evt;
	IMMND_IMM_CLIENT_NODE *cl_node = NULL;
	IMMND_IMM_CLIENT_NODE *pbe_cl_node = NULL;
	SaImmOiHandleT implHandle = 0LL;
	SaUint32T reqConn = m_IMMSV_UNPACK_HANDLE_HIGH(clnt_hdl);
	SaUint32T nodeId = m_IMMSV_UNPACK_HANDLE_LOW(clnt_hdl);
	bool delayedReply = false;
	SaUint32T pbeConn = 0;
	SaUint32T pbe2BConn = 0;
	NCS_NODE_ID pbeNodeId = 0;
	NCS_NODE_ID *pbeNodeIdPtr = NULL;
	SaUint32T continuationId = 0;
	SaUint32T spApplConn = 0; /* Special applier locally connected. */
	TRACE_ENTER();

#if 0  /*DEBUG PRINTOUTS START */
	TRACE_2("ABT immnd_evt_proc_rt_object_modify object:%s", evt->info.objModify.objectName.buf);
	TRACE_2("ABT attrMods:%p ", evt->info.objModify.attrMods);

	IMMSV_ATTR_MODS_LIST *p = evt->info.objModify.attrMods;

	while (p) {
		TRACE_2("ABT FOUND AN ATTRIBUTE-MOD, MOD_TYPE:%u NAME: %s type:%ld "
			"values:%ld", p->attrModType,
			p->attrValue.attrName.buf, p->attrValue.attrValueType, p->attrValue.attrValuesNumber);
		if (p->attrValue.attrValuesNumber) {
			printImmValue(p->attrValue.attrValueType, &(p->attrValue.attrValue));
		}
		p = p->next;
	}
#endif /*DEBUG PRINTOUTS STOP */

	if (cb->mPbeFile && (cb->mRim == SA_IMM_KEEP_REPOSITORY)) {
		pbeNodeIdPtr = &pbeNodeId;
		TRACE("We expect there to be a PBE");
		/* If pbeNodeIdPtr is NULL then rtObjectUpdate skips the lookup
		   of the pbe implementer.
		 */
	}

	unsigned int isLocal = 0;

	if (originatedAtThisNd) {
		err = immModel_rtObjectUpdate(
		    cb, &(evt->info.objModify), reqConn, nodeId, &isLocal,
		    &continuationId, &pbeConn, pbeNodeIdPtr, &spApplConn,
		    &pbe2BConn);
		if (err != SA_AIS_OK) {
			LOG_WA(
			    "Got error on non local rt object update err: %u",
			    err);
			if (cb->mIsCoord && immModel_immNotWritable(cb) &&
			    (cb->mState == IMM_SERVER_SYNC_SERVER)) {
				LOG_WA(
				    "Failed RtObject update has to abort sync");
				/* Clarification: We have to abort the sync here
				   even for soft errors like TRY_AGAIN. This
				   even though all nodes (except the sync
				   clients) are in agreement. The problem is
				   exactly the sync clients. The typical reason
				   for getting TRY_AGAIN on an RtUpdate during
				   sync is that the update is an update of a
				   PERSISTENT RTA and the PBE happens to be
				   currently not available. This has nothing to
				   do with the sync per se, it is simply that
				   when PRTAs are used a lot, then the PBE tends
				   to become unavailable.

				   The sync clients have not synced enough to
				   detect PBE configurability and so can not
				   detect that a PBE should be attached but is
				   currently not, so the clients will accept the
				   RTA update while all other veteran nodes
				   reject it with TRY_AGAIN. Thus the clients
				   have to be forced to restart. Hopefully this
				   will not happen too often as people realize
				   that PERSISTENT RTAs should never be used.
				 */
				immnd_abortSync(cb);
				if (cb->syncPid > 0) {
					LOG_WA("STOPPING sync process pid %u",
					       cb->syncPid);
					kill(
					    cb->syncPid,
					    SIGKILL); /* Immediate stop of the
							 sync process. */
						      /* The server state will be changed to
						       * SERVER_READY in immnd_proc_server */
				} else {
					/* Sync process is not forked yet,
					 * change server state to SERVER_READY
					 */
					cb->mState = IMM_SERVER_READY;
					LOG_NO(
					    "SERVER STATE: IMM_SERVER_SYNC_SERVER --> IMM_SERVER_READY");
				}
			}
		}
	} else {
		err = immModel_rtObjectUpdate(cb, &(evt->info.objModify), 0,
					      nodeId, &isLocal, &continuationId,
					      &pbeConn, pbeNodeIdPtr,
					      &spApplConn, &pbe2BConn);
		if (err == SA_AIS_ERR_REPAIR_PENDING) {
			/* This is the special case of an rtObjectUpdate
			   arriving over fevs at a node that is being synced. If
			   the object has been synced when the update arrives,
			   then the update should be applied normally. But if
			   the object does not exist, then this is interpreted
			   as the object not yet having been synced. In this
			   latter case we store the update message and possibly
			   apply it later when the object has been synced. Only
			   updates that arrive over fevs AFTER the sync message
			   has been generated, but before the sync message
			   arrives, are applied. Message that arrive before the
			   sync message is generated, are reflected in the sync
			   message.
			*/
			immModel_deferRtUpdate(cb, &(evt->info.objModify),
					       msgNo);
		}
	}

	/* Non cached attributes only updated locally, see
	   immnd_ect_proc_rt_update. Cached rt-attributes are updated everywhere
	   and this is handled here. But if there are also one or more
	   PERSISTENT RT attributes included in the set to be updated, then
	   immModel_rtObjectUpdat above only did checks. The actual updates of
	   the cahced and the persistent&cached attributes is postponed until we
	   get a reply from PBE.
	*/

	if (pbeNodeId && err == SA_AIS_OK) {
		/*The persistent back-end is present => wait for reply. */
		delayedReply = true;
		if (pbeConn) {
			/*The persistent back-end is executing at THIS node. */
			osafassert(!pbe2BConn);
			osafassert(cb->mIsCoord);
			osafassert(pbeNodeId);
			osafassert(pbeNodeId == cb->node_id);
			implHandle = m_IMMSV_PACK_HANDLE(pbeConn, pbeNodeId);

			/*Fetch client node for PBE */
			immnd_client_node_get(cb, implHandle, &pbe_cl_node);
			osafassert(pbe_cl_node);
			if (pbe_cl_node->mIsStale) {
				LOG_WA(
				    "PBE is down => persistify of rtAttr is aborted!");
				/* ****
				   TODO: send a fevs faked reply from PBE
				   indicating failure to update. This will
				   rollback the rtObjCreate. Currently it should
				   get cleaned up by cleanTheBasement, but that
				   will typically lead to ERR_TIMEOUT for the OI
				   client.
				*/
			} else {
				memset(&send_evt, '\0', sizeof(IMMSV_EVT));
				send_evt.type = IMMSV_EVT_TYPE_IMMA;
				/* PBE is internal => can handle long DNs */
				send_evt.info.imma.type =
				    IMMA_EVT_ND2A_OI_OBJ_MODIFY_UC;
				send_evt.info.imma.info.objModify =
				    evt->info.objModify;
				send_evt.info.imma.info.objModify.adminOwnerId =
				    continuationId;
				/*We re-use the adminOwner member of the
				  ccbmodify message to hold the continuation id.
				*/
				send_evt.info.imma.info.objModify.immHandle =
				    implHandle;
				osafassert(evt->info.objModify.ccbId == 0);

				TRACE_2(
				    "MAKING PBE-IMPLEMENTER PERSISTENT RT-OBJ MODIFY upcall");
				if (immnd_mds_msg_send(
					cb, NCSMDS_SVC_ID_IMMA_OI,
					pbe_cl_node->agent_mds_dest,
					&send_evt) != NCSCC_RC_SUCCESS) {
					LOG_WA(
					    "Upcall over MDS for persistent rt attr update "
					    "to PBE failed!");
					/* See comment **** above. */
				}
			}
			implHandle = 0LL;
			pbe_cl_node = NULL;
		} else if (pbe2BConn) {
			TRACE("PBE SLAVE is executing at THIS node");
			osafassert(!(cb->mIsCoord));
			osafassert(pbeNodeId != cb->node_id);
			implHandle =
			    m_IMMSV_PACK_HANDLE(pbe2BConn, cb->node_id);

			/*Fetch client node for PBE */
			immnd_client_node_get(cb, implHandle, &pbe_cl_node);
			osafassert(pbe_cl_node);
			if (pbe_cl_node->mIsStale) {
				LOG_WA(
				    "SLAVE PBE is down => persistify of rtAttr is aborted!");
				/* ****
				   TODO: send a fevs faked reply from PBE
				   indicating failure to update. This will
				   rollback the rtObjCreate. Currently it should
				   get cleaned up by cleanTheBasement, but that
				   will typically lead to ERR_TIMEOUT for the OI
				   client.
				*/
			} else {
				memset(&send_evt, '\0', sizeof(IMMSV_EVT));
				send_evt.type = IMMSV_EVT_TYPE_IMMA;
				/* PBE2B is internal => can handle long DNs */
				send_evt.info.imma.type =
				    IMMA_EVT_ND2A_OI_OBJ_MODIFY_UC;
				send_evt.info.imma.info.objModify =
				    evt->info.objModify;
				send_evt.info.imma.info.objModify.adminOwnerId =
				    continuationId;
				/*We re-use the adminOwner member of the
				  ccbmodify message to hold the continuation id.
				*/
				send_evt.info.imma.info.objModify.immHandle =
				    implHandle;
				osafassert(evt->info.objModify.ccbId == 0);

				TRACE_2(
				    "MAKING PBE-IMPLEMENTER PERSISTENT RT-OBJ MODIFY upcall");
				if (immnd_mds_msg_send(
					cb, NCSMDS_SVC_ID_IMMA_OI,
					pbe_cl_node->agent_mds_dest,
					&send_evt) != NCSCC_RC_SUCCESS) {
					LOG_WA(
					    "Upcall over MDS for persistent rt attr update "
					    "to PBE failed!");
					/* See comment **** above. */
				}
			}
			implHandle = 0LL;
			pbe_cl_node = NULL;
		}

		if (spApplConn) {
			/* If we get here then there is a special applier
			   attached locally at this node, but this RTA update
			   was actually a PRTA update, PBE is enabled and
			   running on some node. The PBE has just been notified
			   of this PRTA update. The special applier callback has
			   to wait for the reply on the successfull commit of
			   the PRTA update from PBE. We need to save the pending
			   special applier callback data with the PRTA update
			   continuation. Note also that PRTA update could be
			   directed at either a runtime or config object.
			 */
			evt->info.objModify.attrMods =
			    immModel_specialApplierTrimModify(
				cb, spApplConn, &(evt->info.objModify));

			immModel_specialApplierSaveRtUpdateAttrs(
			    cb, &(evt->info.objModify), continuationId);
		}
	}

	if (spApplConn && (err == SA_AIS_OK) && !delayedReply) {
		/* Indicates object is marked with SA_IMM_ATTR_NOTIFY and
		   special applier is present at this node and we dont need to
		   wait for ack from PBE (non persistent RTO or PBE not
		   enabled).*/

		implHandle = m_IMMSV_PACK_HANDLE(spApplConn, cb->node_id);
		/*Fetch client node for Special applier OI ! */
		cl_node = 0LL;
		immnd_client_node_get(cb, implHandle, &cl_node);
		osafassert(cl_node != NULL);

		TRACE_2("Special applier needs to be notified of RTA update.");
		memset(&send_evt, '\0', sizeof(IMMSV_EVT));
		send_evt.type = IMMSV_EVT_TYPE_IMMA;
		/* Assuming special applier is long DN capable. */
		send_evt.info.imma.type = IMMA_EVT_ND2A_OI_OBJ_MODIFY_UC;
		send_evt.info.imma.info.objModify = evt->info.objModify;
		send_evt.info.imma.info.objModify.adminOwnerId = 0;
		send_evt.info.imma.info.objModify.immHandle = implHandle;
		osafassert(evt->info.objModify.ccbId == 0);
		evt->info.objModify.attrMods =
		    send_evt.info.imma.info.objModify.attrMods =
			immModel_specialApplierTrimModify(
			    cb, spApplConn, &(evt->info.objModify));

		if (cl_node->mIsStale) {
			LOG_WA(
			    "Special applier client went down so create upcall not sent");
		} else if (immnd_mds_msg_send(cb, NCSMDS_SVC_ID_IMMA_OI,
					      cl_node->agent_mds_dest,
					      &send_evt) != NCSCC_RC_SUCCESS) {
			LOG_WA("RTO update upcall for special applier failed");
			/* This is an unhandled message loss case!*/
		}
	}

	if (originatedAtThisNd && !delayedReply) {
		immnd_client_node_get(cb, clnt_hdl, &cl_node);
		if (cl_node == NULL || cl_node->mIsStale) {
			LOG_WA("IMMND - Client went down so no response");
			goto done; /* Dont leak memory on error */
		}

		TRACE_2("send reply to client/agent");
		memset(&send_evt, '\0', sizeof(IMMSV_EVT));
		send_evt.type = IMMSV_EVT_TYPE_IMMA;
		send_evt.info.imma.info.errRsp.error = err;
		send_evt.info.imma.type = IMMA_EVT_ND2A_IMM_ERROR;

		if (immnd_mds_send_rsp(cb, &(cl_node->tmpSinfo), &send_evt) !=
		    NCSCC_RC_SUCCESS) {
			LOG_WA("Failed to send result to OI client over MDS");
		}
	}

done:
	/*Free the incomming events substructure. */
	free(evt->info.objModify.objectName.buf);
	evt->info.objModify.objectName.buf = NULL;
	evt->info.objModify.objectName.size = 0;
	immsv_free_attrmods(evt->info.objModify.attrMods);
	evt->info.objModify.attrMods = NULL;
	TRACE_LEAVE();
}

static void immnd_evt_ccb_abort(IMMND_CB *cb, SaUint32T ccbId,
				SaUint32T **clientArr, SaUint32T *clArrsize,
				SaUint32T *nodeId)
{
	IMMSV_EVT send_evt;
	SaUint32T *implConnArr = NULL;
	SaUint32T arrSize = 0, dummyClsize = 0;
	SaUint32T dummynodeId = 0;
	SaUint32T *dummyClient = NULL;
	SaImmOiHandleT implHandle = 0LL;
	NCS_NODE_ID pbeNodeId = 0;
	NCS_NODE_ID *pbeNodeIdPtr = NULL;
	IMMND_IMM_CLIENT_NODE *oi_cl_node = NULL;

	TRACE_ENTER();

	if (cb->mPbeFile && (cb->mRim == SA_IMM_KEEP_REPOSITORY)) {
		pbeNodeIdPtr = &pbeNodeId;
		TRACE("We expect there to be a PBE");
		/* If pbeNodeIdPtr is NULL then ccbAbort
		   skips the lookup of the pbe implementer.
		 */
	}

	if (!immModel_ccbAbort(cb, ccbId, &arrSize, &implConnArr, &dummyClient,
			       &dummyClsize, &dummynodeId, pbeNodeIdPtr)) {
		goto done;
	}

	if (clientArr) {
		*clientArr = dummyClient;
		*clArrsize = dummyClsize;
	} else {
		free(dummyClient);
		dummyClient = NULL; /* dont reply to client here*/
	}

	if (nodeId && cb->node_id == dummynodeId) {
		*nodeId = dummynodeId;
		TRACE_2("ccb:%u is originated from this node", ccbId);
	}

	if (arrSize) {

		TRACE_2("THERE ARE LOCAL IMPLEMENTERS in ccb:%u", ccbId);

		memset(&send_evt, '\0', sizeof(IMMSV_EVT));
		send_evt.type = IMMSV_EVT_TYPE_IMMA;
		send_evt.info.imma.type = IMMA_EVT_ND2A_OI_CCB_ABORT_UC;
		int ix = 0;
		for (; ix < arrSize; ++ix) {
			/*Look up the client node for the implementer, using
			 * implConn */
			if (implConnArr[ix] == 0) {
				continue;
			}
			implHandle =
			    m_IMMSV_PACK_HANDLE(implConnArr[ix], cb->node_id);

			/*Fetch client node for OI ! */
			immnd_client_node_get(cb, implHandle, &oi_cl_node);
			if (oi_cl_node == NULL || oi_cl_node->mIsStale) {
				LOG_WA(
				    "IMMND - Client <%u, %u> went down so can not send abort UC - ignoring!",
				    implConnArr[ix], cb->node_id);
			} else {
				send_evt.info.imma.info.ccbCompl.ccbId = ccbId;
				send_evt.info.imma.info.ccbCompl.implId =
				    implConnArr[ix];
				send_evt.info.imma.info.ccbCompl.immHandle =
				    implHandle;

				TRACE_2("MAKING IMPLEMENTER CCB ABORT upcall");
				if (immnd_mds_msg_send(
					cb, NCSMDS_SVC_ID_IMMA_OI,
					oi_cl_node->agent_mds_dest,
					&send_evt) != NCSCC_RC_SUCCESS) {
					LOG_ER(
					    "IMMND CCB ABORT UPCALL SEND FAILED FOR impl: %u",
					    implConnArr[ix]);
				}
			}
		} // for
		free(implConnArr);
	} // if(arrSize

	SaUint32T applCtn = 0;
	SaUint32T *applConnArr = NULL;
	SaUint32T applArrSize =
	    immModel_getLocalAppliersForCcb(cb, ccbId, &applConnArr, &applCtn);

	if (applArrSize) {
		memset(&send_evt, '\0', sizeof(IMMSV_EVT));
		send_evt.type = IMMSV_EVT_TYPE_IMMA;
		send_evt.info.imma.type = IMMA_EVT_ND2A_OI_CCB_ABORT_UC;
		send_evt.info.imma.info.ccbCompl.ccbId = ccbId;
		int ix = 0;
		for (; ix < applArrSize; ++ix) {
			SaImmOiHandleT implHandle =
			    m_IMMSV_PACK_HANDLE(applConnArr[ix], cb->node_id);
			send_evt.info.imma.info.ccbCompl.immHandle = implHandle;
			send_evt.info.imma.info.ccbCompl.implId =
			    applConnArr[ix];

			/*Fetch client node for Applier OI ! */
			immnd_client_node_get(cb, implHandle, &oi_cl_node);
			if (oi_cl_node == NULL || oi_cl_node->mIsStale) {
				LOG_WA(
				    "Applier client went down so completed upcall not sent");
				continue;
			} else if (immnd_mds_msg_send(
				       cb, NCSMDS_SVC_ID_IMMA_OI,
				       oi_cl_node->agent_mds_dest,
				       &send_evt) != NCSCC_RC_SUCCESS) {
				LOG_WA("Completed upcall for applier failed");
			}
		}

		free(applConnArr);
		applConnArr = NULL;
	}

#if 0

	if (dummyClient) {	/* Apparently abort during an ongoing client call. */
		IMMND_IMM_CLIENT_NODE *om_cl_node = NULL;
		SaImmHandleT omHandle;
		/*Look up the client node for the implementer, using implConn */
		omHandle = m_IMMSV_PACK_HANDLE(dummyClient, cb->node_id);

		/*Fetch client node for OM ! */
		immnd_client_node_get(cb, omHandle, &om_cl_node);
		if (om_cl_node == NULL || om_cl_node->mIsStale) {
			LOG_WA("IMMND - Client <%u, %u> went down so can not send reply ignoring!",
				dummyClient, cb->node_id);
		} else {
			memset(&send_evt, '\0', sizeof(IMMSV_EVT));
			send_evt.type = IMMSV_EVT_TYPE_IMMA;
			send_evt.info.imma.type = IMMA_EVT_ND2A_IMM_ERROR;
			send_evt.info.imma.info.errRsp.error =
			    timeout ? SA_AIS_ERR_TIMEOUT : SA_AIS_ERR_FAILED_OPERATION;

			if(send_evt.info.imma.info.errRsp.error == SA_AIS_ERR_FAILED_OPERATION) {
				immModel_setCcbErrorString(cb, ccbId, IMM_RESOURCE_ABORT);
			}

			send_evt.info.imma.info.errRsp.errStrings =
				immModel_ccbGrabErrStrings(cb, ccbId);
			if(send_evt.info.imma.info.errRsp.errStrings) {
				send_evt.info.imma.type = IMMA_EVT_ND2A_IMM_ERROR_2;
			}

			if (om_cl_node->tmpSinfo.stype == MDS_SENDTYPE_SNDRSP) {
				TRACE_2("SENDRSP %u", send_evt.info.imma.info.errRsp.error);
				if (immnd_mds_send_rsp(cb, &(om_cl_node->tmpSinfo), &send_evt)
				    != NCSCC_RC_SUCCESS) {
					LOG_WA("Failed to send response to agent/client over MDS");
				}
			}
		}
	}
#endif
done:
	TRACE_LEAVE();
}

/****************************************************************************
 * Name          : immnd_evt_proc_object_delete
 *
 * Description   : Function to process the saImmOmCcbObjectDelete
 *                 operation from agent.
 *
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *                 bool originatedAtThisNode - Did it come from this node?
 *                 SaImmHandleT clnt_hdl - The client handle (only relevant if
 *                                         originatedAtThisNode is true).
 *                 IMM_DEST reply_dest - The dest of the ND to where reply
 *                                       is to be sent (only relevant if
 *                                       originatedAtThisNode is false).
 * Return Values : None
 *
 *****************************************************************************/
static void immnd_evt_proc_object_delete(IMMND_CB *cb, IMMND_EVT *evt,
					 bool originatedAtThisNd,
					 SaImmHandleT clnt_hdl,
					 MDS_DEST reply_dest)
{
	SaAisErrorT err = SA_AIS_OK;
	IMMSV_EVT send_evt;
	IMMND_IMM_CLIENT_NODE *cl_node = NULL;

	IMMND_IMM_CLIENT_NODE *oi_cl_node = NULL;
	SaImmOiHandleT implHandle = 0LL;
	SaUint32T conn = m_IMMSV_UNPACK_HANDLE_HIGH(clnt_hdl);

	SaUint32T *implConnArr = NULL;
	SaUint32T *invocArr = NULL;
	SaStringT *objNameArr = NULL;
	SaUint32T arrSize = 0;
	bool delayedReply = false;

	SaUint32T pbeConn = 0;
	NCS_NODE_ID pbeNodeId = 0;
	NCS_NODE_ID *pbeNodeIdPtr = NULL;
	bool augDelete = false;
	bool hasLongDn = false;
	TRACE_ENTER();

#if 0  /*ABT DEBUG PRINTOUTS START */
	TRACE_2("ABT immnd_evt_proc_object_delete object:%s", evt->info.objDelete.objectName.buf);
	TRACE_2("ABT immnd_evt_proc_object_delete CCB:%u ADMOWN:%u",
		evt->info.objDelete.ccbId, evt->info.objDelete.adminOwnerId);
#endif /*ABT DEBUG PRINTOUTS STOP */

	if (cb->mPbeFile && (cb->mRim == SA_IMM_KEEP_REPOSITORY)) {
		pbeNodeIdPtr = &pbeNodeId;
		/* If pbeNodeIdPtr is NULL then ccbObjectCreate skips the lookup
		   of the pbe implementer. */
	}

	err = immModel_ccbObjectDelete(
	    cb, &(evt->info.objDelete), originatedAtThisNd ? conn : 0, &arrSize,
	    &implConnArr, &invocArr, &objNameArr, &pbeConn, pbeNodeIdPtr,
	    &augDelete, &hasLongDn);

	/* Before generating implementer upcalls for any local implementers,
	   generate PBE upcalls for ALL deleted objects, if the PBE exists and
	   is local. To reduce nrof messages, we do not wait for ack from
	   create/modify/delete upcalls to PBE. Instead we count the number of
	   ops for the ccb in both model and in PBE. The count is verified in
	   the completed upcall to PBE where we *do* wait for ack. In fact the
	   ccb-commit/abort decision is delegated to the PBE when the completed
	   upcall is done.
	 */
	if (pbeNodeIdPtr && pbeConn && err == SA_AIS_OK) {
		TRACE_5("PBE exists and is local to this node arrSize:%u",
			arrSize);
		osafassert(cb->mIsCoord);
		osafassert(pbeNodeId);
		osafassert(pbeNodeId == cb->node_id);
		implHandle = m_IMMSV_PACK_HANDLE(pbeConn, pbeNodeId);
		memset(&send_evt, '\0', sizeof(IMMSV_EVT));
		send_evt.type = IMMSV_EVT_TYPE_IMMA;
		/* PBE is internal => can handle long DNs */
		send_evt.info.imma.type = IMMA_EVT_ND2A_OI_OBJ_DELETE_UC;
		send_evt.info.imma.info.objDelete.ccbId =
		    evt->info.objDelete.ccbId;
		send_evt.info.imma.info.objDelete.immHandle = implHandle;
		send_evt.info.imma.info.objDelete.adminOwnerId =
		    0; /* No reply!*/

		/*Fetch client node for PBE */
		immnd_client_node_get(cb, implHandle, &oi_cl_node);
		osafassert(oi_cl_node);
		if (oi_cl_node->mIsStale) {
			LOG_WA("IMMND - PBE is down => ccb %u fails",
			       evt->info.objDelete.ccbId);
			/* This is an asymetric failure, dangerous because we
			   are not waiting on replies for delete UC to PBE.
			   But the op count plus forced wait on PBE-completed,
			   should prevent any apply to succeed.
			*/
			err = SA_AIS_ERR_FAILED_OPERATION;
			immModel_setCcbErrorString(
			    cb, evt->info.objDelete.ccbId,
			    IMM_RESOURCE_ABORT "PBE is down");
			immnd_proc_global_abort_ccb(cb,
						    evt->info.objDelete.ccbId);
		} else {
			/* We have obtained PBE handle & dest info for PBE.
			   Iterate through objNameArray and send delete upcalls
			   to PBE. PBE delete upcalls are generated for all
			   config objects plus any PERSISTENT runtime objects
			   that are deleted as a side effect. No PBE delete
			   upcalls are generated for cached non-persistent
			   runtime objects that are delete as a side effect.
			 */
			int ix = 0;
			for (; ix < arrSize && err == SA_AIS_OK; ++ix) {
				send_evt.info.imma.info.objDelete.objectName
				    .size =
				    (SaUint32T)strlen(objNameArr[ix]) + 1;
				send_evt.info.imma.info.objDelete.objectName
				    .buf = objNameArr[ix];

				TRACE_2(
				    "MAKING PBE-IMPLEMENTER OBJ DELETE upcall");
				if (immnd_mds_msg_send(
					cb, NCSMDS_SVC_ID_IMMA_OI,
					oi_cl_node->agent_mds_dest,
					&send_evt) != NCSCC_RC_SUCCESS) {
					LOG_ER(
					    "Immnd upcall over MDS for ccbObjectDelete "
					    "to PBE failed! - aborting ccb %u",
					    evt->info.objDelete.ccbId);
					err = SA_AIS_ERR_FAILED_OPERATION;
					immModel_setCcbErrorString(
					    cb, evt->info.objDelete.ccbId,
					    IMM_RESOURCE_ABORT
					    "Upcall over MDS to PBE failed");
					immnd_proc_global_abort_ccb(
					    cb, evt->info.objDelete.ccbId);
				}
			}
		}
	} /* End of PersistentBackEnd handling. */

	if (err == SA_AIS_OK) {
		if (arrSize) {
			/*We have local implementer(s) for deleted object(s) */
			memset(&send_evt, '\0', sizeof(IMMSV_EVT));
			send_evt.type = IMMSV_EVT_TYPE_IMMA;
			send_evt.info.imma.type =
			    hasLongDn ? IMMA_EVT_ND2A_OI_OBJ_DELETE_LONG_UC
				      : IMMA_EVT_ND2A_OI_OBJ_DELETE_UC;
			send_evt.info.imma.info.objDelete.ccbId =
			    evt->info.objDelete.ccbId;
			int ix = 0;
			for (; ix < arrSize && err == SA_AIS_OK; ++ix) {
				if (implConnArr[ix] == 0) {
					/* implConn zero => ony for PBE or
					 * appliers. */
					TRACE(
					    "Skiping over %s (was only for PBE)",
					    objNameArr[ix]);
					continue;
				}

				delayedReply = true;
				/*Look up the client node for the implementer,
				 * using implConn */
				implHandle = m_IMMSV_PACK_HANDLE(
				    implConnArr[ix], cb->node_id);

				/*Fetch client node for OI ! */
				immnd_client_node_get(cb, implHandle,
						      &oi_cl_node);
				osafassert(oi_cl_node != NULL);
				if (oi_cl_node->mIsStale) {
					LOG_WA(
					    "Client went down so no delete upcall to one client");
					/* This should cause the ccb-operation
					 * to timeout on wait for the reply. */
					err = SA_AIS_ERR_FAILED_OPERATION;
					delayedReply = false;
					immModel_setCcbErrorString(
					    cb, evt->info.objDelete.ccbId,
					    IMM_RESOURCE_ABORT
					    "Client went down");
				} else {
					/* Generate an implementer upcall for
					   each deleted config object. No
					   implementer upcalls are generated for
					   any runtime objects (persistent or
					   not) that are deleted as a side
					   effect.
					 */
					send_evt.info.imma.info.objDelete
					    .objectName.size =
					    (SaUint32T)strlen(objNameArr[ix]) +
					    1;
					send_evt.info.imma.info.objDelete
					    .objectName.buf = objNameArr[ix];
					send_evt.info.imma.info.objDelete
					    .adminOwnerId = invocArr[ix];
					send_evt.info.imma.info.objDelete
					    .immHandle = implHandle;
					/*Yes that was a bit ugly. But the
					   upcall message is the same structure
					   as the request message, except the
					   semantics of one integer member
					   differs.
					 */
					TRACE_2(
					    "MAKING OI-IMPLEMENTER OBJ DELETE upcall invoc:%u",
					    invocArr[ix]);
					if (immnd_mds_msg_send(
						cb, NCSMDS_SVC_ID_IMMA_OI,
						oi_cl_node->agent_mds_dest,
						&send_evt) !=
					    NCSCC_RC_SUCCESS) {
						LOG_ER(
						    "Immnd upcall over MDS for ccbObjectDelete failed");
						/* This should cause the
						 * ccb-operation to timeout. */
						err =
						    SA_AIS_ERR_FAILED_OPERATION;
						delayedReply = false;
						immModel_setCcbErrorString(
						    cb,
						    evt->info.objDelete.ccbId,
						    IMM_RESOURCE_ABORT
						    "Upcall over MDS failed");
					}
				}
			} /*for */
		}

		if (!delayedReply && (err == SA_AIS_OK) &&
		    immModel_ccbWaitForDeleteImplAck(cb, evt->info.ccbId, &err,
						     augDelete)) {
			TRACE_2(
			    "No local implementers but wait for remote ones. ccb: %u",
			    evt->info.ccbId);
			delayedReply = (err == SA_AIS_OK);
		}

		if (!delayedReply) {
			TRACE_2(
			    "NO IMPLEMENTERS AT ALL. for ccb:%u err:%u sz:%u",
			    evt->info.ccbId, err, arrSize);
		}
	}

	if ((err == SA_AIS_OK) && arrSize) {
		/* Generate applier delete upcalls. */
		memset(&send_evt, '\0', sizeof(IMMSV_EVT));
		send_evt.type = IMMSV_EVT_TYPE_IMMA;
		send_evt.info.imma.type =
		    hasLongDn ? IMMA_EVT_ND2A_OI_OBJ_DELETE_LONG_UC
			      : IMMA_EVT_ND2A_OI_OBJ_DELETE_UC;
		send_evt.info.imma.info.objDelete.ccbId =
		    evt->info.objDelete.ccbId;
		send_evt.info.imma.info.objDelete.adminOwnerId = 0;
		/* Re-use the adminOwner member of the ccbDelete message to hold
		   the invocation id. In this case, 0 => no reply is expected.
		 */
		int ix = 0;
		for (; ix < arrSize && err == SA_AIS_OK;
		     ++ix) { /* Iterate over deleted objects */
			SaUint32T *applConnArr = NULL;
			SaNameT objName;
			osaf_extended_name_lend(objNameArr[ix], &objName);
			send_evt.info.imma.info.objDelete.objectName.size =
			    strlen(objNameArr[ix]) + 1;
			send_evt.info.imma.info.objDelete.objectName.buf =
			    objNameArr[ix];

			SaUint32T arrSize2 = immModel_getLocalAppliersForObj(
			    cb, &objName, evt->info.objDelete.ccbId,
			    &applConnArr, true);

			int ix2 = 0;
			for (; ix2 < arrSize2 && err == SA_AIS_OK;
			     ++ix2) { /* Iterate over applier connections for
					 object */

				/* Look up the client node for the applier */
				implHandle = m_IMMSV_PACK_HANDLE(
				    applConnArr[ix2], cb->node_id);

				/*Fetch client node for OI ! */
				immnd_client_node_get(cb, implHandle,
						      &oi_cl_node);
				osafassert(oi_cl_node != NULL);
				if (oi_cl_node->mIsStale) {
					LOG_WA(
					    "Applier went down so no delete upcall to one client");
					continue;
				}

				send_evt.info.imma.info.objDelete.immHandle =
				    implHandle;

				if ((ix2 == (arrSize2 - 1)) &&
				    immModel_isSpecialAndAddModify(
					cb, applConnArr[ix2],
					evt->info.objDelete.ccbId)) {
					IMMSV_EVT send_evt2;
					memset(&send_evt2, '\0',
					       sizeof(IMMSV_EVT));
					/* Last local applier may be special
					   applier. If so then we may need to
					   send a fake modify upcall, to inform
					   the special applier of the ccb's
					   admin-owner-name.
					*/
					TRACE(
					    "Special fake object-modify with admin-owner-name appended to ccb:%u",
					    evt->info.objDelete.ccbId);

					send_evt2.type = IMMSV_EVT_TYPE_IMMA;
					/* Assuming special applier is long DN
					 * capable. */
					send_evt2.info.imma.type =
					    IMMA_EVT_ND2A_OI_OBJ_MODIFY_UC;
					send_evt2.info.imma.info.objModify
					    .ccbId = evt->info.objDelete.ccbId;
					send_evt2.info.imma.info.objModify
					    .adminOwnerId =
					    evt->info.objDelete
						.adminOwnerId; /* Temporary for
								  downcall */
					send_evt2.info.imma.info.objModify
					    .objectName =
					    send_evt.info.imma.info.objDelete
						.objectName;
					send_evt2.info.imma.info.objModify
					    .immHandle = implHandle;

					immModel_genSpecialModify(
					    cb, &(send_evt2.info.imma.info
						      .objModify));
					send_evt2.info.imma.info.objModify
					    .adminOwnerId =
					    0; /* Reset to 0 for no reply*/

					if (immnd_mds_msg_send(
						cb, NCSMDS_SVC_ID_IMMA_OI,
						oi_cl_node->agent_mds_dest,
						&send_evt2) !=
					    NCSCC_RC_SUCCESS) {
						LOG_WA(
						    "Special modify upcall for applier failed conn:%u",
						    applConnArr[ix2]);
					}
					/* Free temporary structures allocated
					 * in immModel_genSpecialModify */
					free(send_evt2.info.imma.info.objModify
						 .attrMods->attrValue.attrValue
						 .val.x.buf); /* free3 */
					send_evt2.info.imma.info.objModify
					    .attrMods->attrValue.attrValue.val.x
					    .buf = NULL;
					send_evt2.info.imma.info.objModify
					    .attrMods->attrValue.attrValue.val.x
					    .size = 0;

					free(send_evt2.info.imma.info.objModify
						 .attrMods->attrValue.attrName
						 .buf); /* free2 */
					send_evt2.info.imma.info.objModify
					    .attrMods->attrValue.attrName.buf =
					    NULL;
					send_evt2.info.imma.info.objModify
					    .attrMods->attrValue.attrName.size =
					    0;

					free(send_evt2.info.imma.info.objModify
						 .attrMods); /* free1 */
					send_evt2.info.imma.info.objModify
					    .attrMods = NULL;
				}

				TRACE_2("MAKING OI-APPLIER OBJ DELETE upcall ");
				if (immnd_mds_msg_send(
					cb, NCSMDS_SVC_ID_IMMA_OI,
					oi_cl_node->agent_mds_dest,
					&send_evt) != NCSCC_RC_SUCCESS) {
					LOG_WA(
					    "Delete upcall for applier failed conn:%u",
					    applConnArr[ix2]);
				}
			} /* for(; ix2.. */
			free(applConnArr);
			applConnArr = NULL;
		} /* for(; ix... */
	}

	if (arrSize) {
		int ix;
		free(implConnArr);
		implConnArr = NULL;
		free(invocArr);
		invocArr = NULL;

		for (ix = 0; ix < arrSize; ++ix) {
			free(objNameArr[ix]);
		}
		free(objNameArr);
		objNameArr = NULL;
		arrSize = 0;
	}

	/* err!=SA_AIS_OK or no implementers =>immediate reply */
	if (originatedAtThisNd && !delayedReply) {
		immnd_client_node_get(cb, clnt_hdl, &cl_node);
		if (cl_node == NULL || cl_node->mIsStale) {
			LOG_WA("IMMND - OM Client went down so no response");
			return;
		}

		TRACE_2("Send immediate reply to OM client");
		memset(&send_evt, '\0', sizeof(IMMSV_EVT));
		send_evt.type = IMMSV_EVT_TYPE_IMMA;
		send_evt.info.imma.info.errRsp.error = err;
		send_evt.info.imma.info.errRsp.errStrings =
		    immModel_ccbGrabErrStrings(cb, evt->info.objDelete.ccbId);

		if (send_evt.info.imma.info.errRsp.errStrings) {
			send_evt.info.imma.type = IMMA_EVT_ND2A_IMM_ERROR_2;
		} else {
			send_evt.info.imma.type = IMMA_EVT_ND2A_IMM_ERROR;
		}

		if (immnd_mds_send_rsp(cb, &(cl_node->tmpSinfo), &send_evt) !=
		    NCSCC_RC_SUCCESS) {
			LOG_WA("Failed to send result to Agent over MDS");
		}
		immsv_evt_free_attrNames(
		    send_evt.info.imma.info.errRsp.errStrings);
	}
}

/****************************************************************************
 * Name          : immnd_evt_proc_rt_object_delete
 *
 * Description   : Function to process the saImmOiRtObjectDelete
 *                 operation from agent.
 *
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *                 bool originatedAtThisNode - Did it come from this node?
 *                 SaImmHandleT clnt_hdl - The client handle (only relevant if
 *                                         originatedAtThisNode is true).
 *                 IMM_DEST reply_dest - The dest of the ND to where reply
 *                                         is to be sent (only relevant if
 *                                         originatedAtThisNode is false).
 * Return Values : None
 *
 *****************************************************************************/
static void immnd_evt_proc_rt_object_delete(IMMND_CB *cb, IMMND_EVT *evt,
					    bool originatedAtThisNd,
					    SaImmHandleT clnt_hdl,
					    MDS_DEST reply_dest)
{
	SaAisErrorT err = SA_AIS_OK;
	IMMSV_EVT send_evt;
	IMMND_IMM_CLIENT_NODE *cl_node = NULL;
	IMMND_IMM_CLIENT_NODE *pbe_cl_node = NULL;
	SaImmOiHandleT implHandle = 0LL;
	SaUint32T reqConn = m_IMMSV_UNPACK_HANDLE_HIGH(clnt_hdl);
	SaUint32T nodeId = m_IMMSV_UNPACK_HANDLE_LOW(clnt_hdl);
	bool delayedReply = false;
	SaUint32T pbeConn = 0;
	SaUint32T pbe2BConn = 0;
	NCS_NODE_ID pbeNodeId = 0;
	NCS_NODE_ID *pbeNodeIdPtr = NULL;
	SaUint32T continuationId = 0;
	SaStringT *objNameArr = NULL;
	SaUint32T arrSize = 0;
	SaUint32T spApplConn = 0;
	TRACE_ENTER();

	if (cb->mPbeFile && (cb->mRim == SA_IMM_KEEP_REPOSITORY)) {
		pbeNodeIdPtr = &pbeNodeId;
		TRACE("We expect there to be a PBE");
		/* If pbeNodeIdPtr is NULL then rtObjectDelete skips the lookup
		   of the pbe implementer.
		 */
	}

	if (originatedAtThisNd) {
		err = immModel_rtObjectDelete(
		    cb, &(evt->info.objDelete), reqConn, nodeId,
		    &continuationId, &pbeConn, pbeNodeIdPtr, &objNameArr,
		    &arrSize, &spApplConn, &pbe2BConn);
	} else {
		err = immModel_rtObjectDelete(
		    cb, &(evt->info.objDelete), 0, nodeId, &continuationId,
		    &pbeConn, pbeNodeIdPtr, &objNameArr, &arrSize, &spApplConn,
		    &pbe2BConn);
	}

	if (pbeNodeId && (err == SA_AIS_OK)) {
		/*The persistent back-end is present & PRTOs deleted in subtree
		 * => wait for reply. */
		delayedReply = true;
		if (pbeConn && arrSize) {
			TRACE("PBE at this node arrSize:%u", arrSize);
			/*The persistent back-end is executing at THIS node. */
			osafassert(!pbe2BConn);
			osafassert(cb->mIsCoord);
			osafassert(pbeNodeId == cb->node_id);
			implHandle = m_IMMSV_PACK_HANDLE(pbeConn, pbeNodeId);
			memset(&send_evt, '\0', sizeof(IMMSV_EVT));
			send_evt.type = IMMSV_EVT_TYPE_IMMA;
			/* PBE is internal => can handle long DNs */
			send_evt.info.imma.type =
			    IMMA_EVT_ND2A_OI_OBJ_DELETE_UC;
			send_evt.info.imma.info.objDelete.ccbId = 0;
			send_evt.info.imma.info.objDelete.adminOwnerId =
			    continuationId;
			send_evt.info.imma.info.objDelete.immHandle =
			    implHandle;

			/*Fetch client node for PBE */
			immnd_client_node_get(cb, implHandle, &pbe_cl_node);
			osafassert(pbe_cl_node);
			if (pbe_cl_node->mIsStale) {
				LOG_WA(
				    "PBE is down => persistify of rtObj delete is dropped!");
				/*
				 TODO: send a fevs faked reply from PBE
				 indicating failure to update. This will
				 rollback the rtObjDeletes. Currently it will be
				 cleaned up by cleanTheBasement.
				*/
				goto done;
			} else {
				/* We have obtained PBE handle & dest info for
				   PBE. Iterate through objNameArray and send
				   delete upcalls to PBE.
				*/
				int ix = 0;
				for (; ix < arrSize && err == SA_AIS_OK; ++ix) {
					send_evt.info.imma.info.objDelete
					    .objectName.size =
					    (SaUint32T)strlen(objNameArr[ix]) +
					    1;
					send_evt.info.imma.info.objDelete
					    .objectName.buf = objNameArr[ix];

					TRACE_2(
					    "MAKING PBE-IMPLEMENTER PERSISTENT RT-OBJ DELETE upcalls");
					if (immnd_mds_msg_send(
						cb, NCSMDS_SVC_ID_IMMA_OI,
						pbe_cl_node->agent_mds_dest,
						&send_evt) !=
					    NCSCC_RC_SUCCESS) {
						LOG_WA(
						    "Upcall over MDS for persistent rt obj delete "
						    "to PBE failed!");
						/* TODO: we could possibly
						   revert the delete here an
						   return TRY_AGAIN. We may have
						   succeeded in sending some
						   deletes, but since we did not
						   send the completed, the PRTO
						   deletes will not be commited
						   by the PBE.
						 */
						goto done;
					}
				}

				send_evt.info.imma.type =
				    IMMA_EVT_ND2A_OI_CCB_COMPLETED_UC;
				send_evt.info.imma.info.ccbCompl.ccbId = 0;
				send_evt.info.imma.info.ccbCompl.immHandle =
				    implHandle;
				send_evt.info.imma.info.ccbCompl.implId =
				    arrSize;
				/* ^^Hack: Use implId to store objCount, see
				   #1809.^^ This avoids having to change the
				   protocol.
				 */
				send_evt.info.imma.info.ccbCompl.invocation =
				    continuationId;

				TRACE_2(
				    "MAKING PBE PRTO DELETE COMPLETED upcall");
				if (immnd_mds_msg_send(
					cb, NCSMDS_SVC_ID_IMMA_OI,
					pbe_cl_node->agent_mds_dest,
					&send_evt) != NCSCC_RC_SUCCESS) {
					LOG_WA(
					    "Upcall over MDS for persistent rt obj deletes "
					    "completed, to PBE failed!");
					/* See comment **** above. */
					/* TODO: we could actually revert the
					 * delete here an return TRY_AGAIN*/
					goto done;
				}
			}
			implHandle = 0LL;
			pbe_cl_node = NULL;
		} else if (pbe2BConn && arrSize) {
			TRACE("PBE SLAVE at this node arrSize:%u", arrSize);
			/*The persistent back-end is executing at THIS node. */
			osafassert(!(cb->mIsCoord));
			osafassert(cb->mCanBeCoord);
			osafassert(pbeNodeId != cb->node_id);
			implHandle =
			    m_IMMSV_PACK_HANDLE(pbe2BConn, cb->node_id);

			memset(&send_evt, '\0', sizeof(IMMSV_EVT));
			send_evt.type = IMMSV_EVT_TYPE_IMMA;

			send_evt.info.imma.type =
			    IMMA_EVT_ND2A_OI_OBJ_DELETE_UC;
			send_evt.info.imma.info.objDelete.ccbId = 0;
			send_evt.info.imma.info.objDelete.adminOwnerId =
			    continuationId;
			send_evt.info.imma.info.objDelete.immHandle =
			    implHandle;

			/*Fetch client node for Slave PBE */
			immnd_client_node_get(cb, implHandle, &pbe_cl_node);
			osafassert(pbe_cl_node);
			if (pbe_cl_node->mIsStale) {
				LOG_WA(
				    "SLAVE PBE is down => persistify of rtObj delete is dropped!");
				goto done;
			} else {
				/* We have obtained handle & dest info for Slave
				   PBE. Iterate through objNameArray and send
				   delete upcalls to Slave PBE.
				*/
				int ix = 0;
				for (; ix < arrSize && err == SA_AIS_OK; ++ix) {
					send_evt.info.imma.info.objDelete
					    .objectName.size =
					    (SaUint32T)strlen(objNameArr[ix]) +
					    1;
					send_evt.info.imma.info.objDelete
					    .objectName.buf = objNameArr[ix];

					TRACE_2(
					    "MAKING PBE-SLAVE PERSISTENT RT-OBJ DELETE upcalls");
					if (immnd_mds_msg_send(
						cb, NCSMDS_SVC_ID_IMMA_OI,
						pbe_cl_node->agent_mds_dest,
						&send_evt) !=
					    NCSCC_RC_SUCCESS) {
						LOG_WA(
						    "Upcall over MDS for persistent rt obj delete "
						    "to Slave PBE failed!");
						/* TODO: we could possibly
						   revert the delete here an
						   return TRY_AGAIN. We may have
						   succeeded in sending some
						   deletes, but since we did not
						   send the completed, the PRTO
						   deletes will not be commited
						   by the PBE.
						 */
						goto done;
					}
				}
			}
			implHandle = 0LL;
			pbe_cl_node = NULL;
		}
	}

	if (spApplConn && (err == SA_AIS_OK) && !delayedReply) {
		int ix = 0;
		/* Indicates object is marked with SA_IMM_ATTR_NOTIFY and
		   special applier is present at this node and we dont need to
		   wait for ack from PBE (non persistent RTO or PBE not
		   enabled).*/
		implHandle = m_IMMSV_PACK_HANDLE(spApplConn, cb->node_id);
		/*Fetch client node for Special applier OI */
		cl_node = 0LL;
		immnd_client_node_get(cb, implHandle, &cl_node);
		osafassert(cl_node != NULL);
		if (cl_node->mIsStale) {
			LOG_WA(
			    "Special applier is down => notify of rtObj delete is dropped");
			goto done;
		}
		TRACE_2("Special applier needs to be notified of RTO delete.");
		memset(&send_evt, '\0', sizeof(IMMSV_EVT));
		send_evt.type = IMMSV_EVT_TYPE_IMMA;
		send_evt.info.imma.type = IMMA_EVT_ND2A_OI_OBJ_DELETE_UC;
		send_evt.info.imma.info.objDelete.ccbId = 0;
		send_evt.info.imma.info.objDelete.adminOwnerId = 0;
		send_evt.info.imma.info.objDelete.immHandle = implHandle;

		for (; ix < arrSize && err == SA_AIS_OK; ++ix) {
			send_evt.info.imma.info.objDelete.objectName.size =
			    (SaUint32T)strlen(objNameArr[ix]) + 1;
			send_evt.info.imma.info.objDelete.objectName.buf =
			    objNameArr[ix];

			TRACE_2("MAKING PBE-IMPLEMENTER RT-OBJ DELETE upcalls");
			if (immnd_mds_msg_send(cb, NCSMDS_SVC_ID_IMMA_OI,
					       cl_node->agent_mds_dest,
					       &send_evt) != NCSCC_RC_SUCCESS) {
				LOG_WA(
				    "RTO delete Upcall special applier failed!");
				/* This is an unhandled message loss case!*/
			}
		}
	}

done:
	if (arrSize) {
		int ix;
		for (ix = 0; ix < arrSize; ++ix) {
			free(objNameArr[ix]);
		}
		free(objNameArr);
		objNameArr = NULL;
		arrSize = 0;
	}

	if (originatedAtThisNd && !delayedReply) {
		immnd_client_node_get(cb, clnt_hdl, &cl_node);
		if (cl_node == NULL || cl_node->mIsStale) {
			LOG_WA("IMMND - Client went down so no response");
			return;
		}

		TRACE_2("send immediate reply to client/agent");
		memset(&send_evt, '\0', sizeof(IMMSV_EVT));
		send_evt.type = IMMSV_EVT_TYPE_IMMA;
		send_evt.info.imma.info.errRsp.error = err;
		send_evt.info.imma.type = IMMA_EVT_ND2A_IMM_ERROR;

		if (immnd_mds_send_rsp(cb, &(cl_node->tmpSinfo), &send_evt) !=
		    NCSCC_RC_SUCCESS) {
			LOG_WA("Failed to send result to implementer over MDS");
		}
	}
	TRACE_LEAVE();
}

/****************************************************************************
 * Name          : immnd_evt_proc_ccb_finalize
 *
 * Description   : Function to process the saImmOmCcbFinalize
 *                 operation from agent (and internal ccb_abort calls)
 *
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *                 bool originatedAtThisNode - Did it come from this node?
 *                 SaImmHandleT clnt_hdl - The client handle (only relevant if
 *                                         originatedAtThisNode is true).
 *                 IMM_DEST reply_dest - The dest of the ND to where reply
 *                                         is to be sent (only relevant if
 *                                       originatedAtThisNode is false).
 * Return Values : None
 *****************************************************************************/
static void immnd_evt_proc_ccb_finalize(IMMND_CB *cb, IMMND_EVT *evt,
					bool originatedAtThisNd,
					SaImmHandleT clnt_hdl,
					MDS_DEST reply_dest)
{
	SaAisErrorT err = SA_AIS_OK;
	IMMSV_EVT send_evt;
	IMMND_IMM_CLIENT_NODE *cl_node = NULL;
	SaUint32T clArrSize = 0, nodeId = 0, ix = 0;
	SaUint32T *clientArr = NULL;
	bool internalCcbAbort = false;
	IMMSV_ATTR_NAME_LIST *errStrings = NULL;
	TRACE_ENTER();

	osafassert(evt);
	immnd_evt_ccb_abort(cb, evt->info.ccbId, &clientArr, &clArrSize,
			    &nodeId);

	if (nodeId && !originatedAtThisNd) {
		/* nodeId will be set when CCB is originated from this node.The
		   CCB is aborted, and the error response forwarded to client
		   will be ERR_FAILED_OPERATION.
		*/

		originatedAtThisNd = true;
		internalCcbAbort = true;
		err = SA_AIS_ERR_FAILED_OPERATION;
	} else {
		TRACE_2("ccb aborted and finalized");
	}

	if (originatedAtThisNd) { /*Send reply to client from this ND. */
		do {
			TRACE_2(
			    "ccbFinalize originated at this node => Send reply");
			/* Perhaps the following osafassert is a bit strong. A
			   corrupt client/agent could "accidentally" use the
			   wrong ccbId if its heap was thrashed. This wrong
			   ccbid could accidentally be an existing used ccbId.
			   But in this case it would also have to originate at
			   this node.
			 */
			if (clArrSize) {
				clnt_hdl =
				    m_IMMSV_PACK_HANDLE(clientArr[ix], nodeId);
				TRACE_2(
				    "client == m_IMMSV_UNPACK_HANDLE_HIGH(clnt_hdl))"
				    "??: %u == %u",
				    clientArr[ix],
				    (SaUint32T)m_IMMSV_UNPACK_HANDLE_HIGH(
					clnt_hdl));
				osafassert(
				    !clArrSize ||
				    clientArr[ix] ==
					m_IMMSV_UNPACK_HANDLE_HIGH(clnt_hdl));
			}
			immnd_client_node_get(cb, clnt_hdl, &cl_node);
			if (cl_node == NULL || cl_node->mIsStale) {
				LOG_WA(
				    "IMMND - Client went down so no response");
			} else {

				memset(&send_evt, '\0', sizeof(IMMSV_EVT));

				send_evt.type = IMMSV_EVT_TYPE_IMMA;
				send_evt.info.imma.info.errRsp.error = err;
				if (internalCcbAbort && !errStrings) {
					errStrings = immModel_ccbGrabErrStrings(
					    cb, evt->info.objModify.ccbId);
				}
				if (errStrings) {
					send_evt.info.imma.info.errRsp
					    .errStrings = errStrings;
					send_evt.info.imma.type =
					    IMMA_EVT_ND2A_IMM_ERROR_2;
				} else {
					send_evt.info.imma.type =
					    IMMA_EVT_ND2A_IMM_ERROR;
				}

				TRACE_2("SENDRSP %u", err);

				if (immnd_mds_send_rsp(cb, &(cl_node->tmpSinfo),
						       &send_evt) !=
				    NCSCC_RC_SUCCESS) {
					LOG_WA(
					    "Failed to send response to agent/client over MDS");
				}
			}
			ix++;
		} while (clArrSize && (ix < clArrSize));
	}

	free(clientArr);
	immsv_evt_free_attrNames(errStrings);
	err = immModel_ccbFinalize(cb, evt->info.ccbId);
	TRACE_LEAVE2("err:%d", err);
}

/****************************************************************************
 * Name          : immnd_evt_proc_ccb_apply
 *
 * Description   : Function to process the saImmOmCcbApply
 *                 operation from agent.
 *
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *                 bool originatedAtThisNode - Did it come from this node?
 *                 SaImmHandleT clnt_hdl - The client handle (only relevant if
 *                                         originatedAtThisNode is true).
 *                 IMM_DEST reply_dest - The dest of the ND to where reply
 *                                         is to be sent (only relevant if
 *                                         originatedAtThisNode is false).
 *                 bool validateOnly - true => only do validation, not
 *commit/apply. Return Values : None
 *
 *****************************************************************************/
static void immnd_evt_proc_ccb_apply(IMMND_CB *cb, IMMND_EVT *evt,
				     bool originatedAtThisNd,
				     SaImmHandleT clnt_hdl, MDS_DEST reply_dest,
				     bool validateOnly)
{
	SaAisErrorT err = SA_AIS_OK;
	IMMSV_EVT send_evt;
	IMMND_IMM_CLIENT_NODE *cl_node = NULL;
	IMMND_IMM_CLIENT_NODE *oi_cl_node = NULL;
	SaImmOiHandleT implHandle = 0LL;
	SaUint32T conn = m_IMMSV_UNPACK_HANDLE_HIGH(clnt_hdl);

	SaUint32T *implConnArr = NULL;
	SaUint32T *implIdArr = NULL;
	SaUint32T *ctnArr = NULL;
	SaUint32T arrSize = 0;
	bool delayedReply = false;
	SaUint32T pbeConn = 0;
	NCS_NODE_ID pbeNodeId = 0;
	NCS_NODE_ID *pbeNodeIdPtr = NULL;
	SaUint32T pbeId = 0;
	SaUint32T pbeCtn = 0;
	int bCcbFinalize = 0;
	TRACE_ENTER();
#if 0 /* Ticket #496  testcase immomtest 6 22 */
	if((evt->info.ccbId == 10)&&(originatedAtThisNd)) {
		LOG_ER("FAULT INJECTION CRASH IN APPLY OF CCB 10");
		abort();
	}
#endif
	if (cb->mPbeDisableCritical && !validateOnly) {
		LOG_WA(
		    "Disable of PBE has been initiated, waiting for the reply from the PBE");
		err = SA_AIS_ERR_TRY_AGAIN;
		goto immediate_reply;
	}

	if (cb->mPbeFile && (cb->mRim == SA_IMM_KEEP_REPOSITORY)) {
		if (immModel_pbeNotWritable(cb)) {
			/* NO_RESOURCES is here imm internal proxy for
			   TRY_AGAIN. The library code for saImmOmCcbApply will
			   translate NO_RESOURCES to TRY_AGAIN towards the user.
			   That library code (for ccbApply) treats TRY_AGAIN
			   from IMMND as an indication of handle resurrect. So
			   we need to take this detour to really communicate
			   TRY_AGAIN towards that particular library code.
			 */
			err = SA_AIS_ERR_NO_RESOURCES;
			goto immediate_reply; /* Intentionally jumps *into* code
						 block below */
		}

		pbeNodeIdPtr = &pbeNodeId;
		TRACE("We expect there to be a PBE");
		/* If pbeNodeIdPtr is NULL then ccbWaitForCompletedAck
		   (further down below) skips the lookup of the pbe implementer.
		 */
	}

	err = immModel_ccbApply(
	    cb, evt->info.ccbId, originatedAtThisNd ? conn : 0, &arrSize,
	    &implConnArr, &implIdArr, &ctnArr, validateOnly);

	if (err == SA_AIS_OK) {
		if (arrSize) {
			TRACE_2(
			    "THERE ARE LOCAL IMPLEMENTERS in ccb:%u sending completed upcall",
			    evt->info.ccbId);
			delayedReply = true;
			memset(&send_evt, '\0', sizeof(IMMSV_EVT));
			send_evt.type = IMMSV_EVT_TYPE_IMMA;
			send_evt.info.imma.type =
			    IMMA_EVT_ND2A_OI_CCB_COMPLETED_UC;
			int ix = 0;
			for (; ix < arrSize && err == SA_AIS_OK; ++ix) {

				/*Look up the client node for the implementer,
				 * using implConn */
				implHandle = m_IMMSV_PACK_HANDLE(
				    implConnArr[ix], cb->node_id);

				/*Fetch client node for OI ! */
				immnd_client_node_get(cb, implHandle,
						      &oi_cl_node);
				if (oi_cl_node == NULL ||
				    oi_cl_node->mIsStale) {
					LOG_WA(
					    "IMMND - Client went down so no response");
					err = SA_AIS_ERR_FAILED_OPERATION;
					delayedReply = false;
					immModel_setCcbErrorString(
					    cb, evt->info.ccbId,
					    IMM_RESOURCE_ABORT
					    "Client went down");
				} else {
					send_evt.info.imma.info.ccbCompl.ccbId =
					    evt->info.ccbId;
					send_evt.info.imma.info.ccbCompl
					    .implId = implIdArr[ix];
					send_evt.info.imma.info.ccbCompl
					    .invocation = ctnArr[ix];
					send_evt.info.imma.info.ccbCompl
					    .immHandle = implHandle;

					TRACE_2(
					    "MAKING IMPLEMENTER CCB COMPLETED upcall");
					if (immnd_mds_msg_send(
						cb, NCSMDS_SVC_ID_IMMA_OI,
						oi_cl_node->agent_mds_dest,
						&send_evt) !=
					    NCSCC_RC_SUCCESS) {
						LOG_ER(
						    "IMMND CCB APPLY: CCB COMPLETED UPCALL SEND FAILED");
						err =
						    SA_AIS_ERR_FAILED_OPERATION;
						/* should abort the entire ccb
						 */
						delayedReply = false;
						immModel_setCcbErrorString(
						    cb, evt->info.ccbId,
						    IMM_RESOURCE_ABORT
						    "Upcall over MDS failed");
					} else {
						TRACE_2(
						    "IMMND UPCALL TO OI, SEND SUCCEEDED");
					}
				}
			} /*for */
		} else if (immModel_ccbWaitForCompletedAck(
			       cb, evt->info.ccbId, &err, &pbeConn,
			       pbeNodeIdPtr, &pbeId, &pbeCtn)) {
			TRACE_2(
			    "No local regular implementers but wait for remote and/or PBE ccb: %u",
			    evt->info.ccbId);
			delayedReply = true;
			if (pbeNodeId) {
				/* There is be a PBE. */
				osafassert(err == SA_AIS_OK); /* If not OK then
								 we should not
								 be waiting. */
				TRACE_5(
				    "Wait for PBE commit decision for ccb %u",
				    evt->info.ccbId);
				if (pbeConn) {
					TRACE_5(
					    "PBE is LOCAL - send completed upcall for %u",
					    evt->info.ccbId);
					/* The PBE is connected at THIS node.
					   Make the completed upcall. If the PBE
					   decides to commit it soes so
					   immediately. It does not wait for the
					   apply upcall.
					*/
					SaImmOiHandleT implHandle =
					    m_IMMSV_PACK_HANDLE(pbeConn,
								pbeNodeId);
					/*Fetch client node for OI ! */
					immnd_client_node_get(cb, implHandle,
							      &oi_cl_node);
					osafassert(oi_cl_node);
					if (oi_cl_node->mIsStale) {
						LOG_WA("PBE-OI has detached");
						/* This is a bad case. The
						   immnds have just delegated
						   the decision to commit or
						   abort this ccb to the PBE,
						   yet it has just detached. We
						   know here that we have not
						   sent the completed call to
						   the pbe, so in principle the
						   abort should be simple. But
						   the Ccb has just entered the
						   critical state in ImmModel,
						   so we would need to undo that
						   critical state. Problem is it
						   would have to be undone at
						   all IMMNDs which is
						   complicated. Instead of
						   optimizing this error case
						   with complex logic and
						   messaging, we unify it with
						   the case of having sent
						   completed to the PBE, not
						   getting any response and let
						   ccb-recovery sort it out.
						*/
						goto skip_send;
					}

					memset(&send_evt, '\0',
					       sizeof(IMMSV_EVT));
					send_evt.type = IMMSV_EVT_TYPE_IMMA;
					send_evt.info.imma.type =
					    IMMA_EVT_ND2A_OI_CCB_COMPLETED_UC;
					send_evt.info.imma.info.ccbCompl.ccbId =
					    evt->info.ccbId;
					send_evt.info.imma.info.ccbCompl
					    .immHandle = implHandle;
					send_evt.info.imma.info.ccbCompl
					    .implId = pbeId;
					send_evt.info.imma.info.ccbCompl
					    .invocation = pbeCtn;

					TRACE_2(
					    "MAKING PBE-IMPLEMENTER CCB COMPLETED upcall");
					if (immnd_mds_msg_send(
						cb, NCSMDS_SVC_ID_IMMA_OI,
						oi_cl_node->agent_mds_dest,
						&send_evt) !=
					    NCSCC_RC_SUCCESS) {
						LOG_WA(
						    "CCB COMPLETED UPCALL SEND TO PBE FAILED");
						/* If the send to PBE fails in
						   Mds then we *dont* know for
						   sure that the message did not
						   reach the PBE. Therefore act
						   as if send succceeded and let
						   ccb-recovery sort it out.
						 */
					} else {
						TRACE_5(
						    "IMMND UPCALL TO PBE for ccb %u, SEND SUCCEEDED",
						    evt->info.ccbId);
					}
				}
				if (cb->mPbeDisableCcbId == evt->info.ccbId) {
					TRACE(
					    " Disable of PBE is sent to PBE for ACK, in ccb:%u",
					    evt->info.ccbId);
					cb->mPbeDisableCritical = true;
				}
			}
		} else {
			TRACE_2(
			    "NO IMPLEMENTERS AT ALL AND NO PBE. for ccb:%u err:%u sz:%u",
			    evt->info.ccbId, err, arrSize);
			TRACE("delayedReply:%u", delayedReply);
		}
	}

skip_send:
	/* err != SA_AIS_OK or no implementers => immediate reply. */
	if (arrSize) {
		free(implConnArr);
		implConnArr = NULL;

		free(implIdArr);
		implIdArr = NULL;

		free(ctnArr);
		ctnArr = NULL;

		arrSize = 0;
	}

	/*REPLY can not be sent immediately if there are implementers or PBE.
	   Must wait for prepare votes. */
	if (!delayedReply) {
		if (err == SA_AIS_ERR_INTERRUPT) {
			/* ERR_INTERUPT is an IMMND internal error code
			   indicating that this was etiher a saImmOmCcbValidate
			   that is now complete, i.e. we need to interrupt and
			   return without continuing with the commit/apply part.
			   Or it was an saImmOmCcbApply continuting such
			   interrupted processing, i.e. we must here skip
			   sending completed callbacks (validation) since this
			   is done already for this ccb.
			 */
			err = SA_AIS_OK;
			if (validateOnly) {
				TRACE("Explicit validation finished OK");
				goto immediate_reply;
			} else {
				/* Fake continuation of end of validation.
				   To avoid writing large ammounts of new code
				   almost identical to existing, we fake a reply
				   from a non existent OI on a final completed
				   callback. This is what would trigger the
				   commit/apply of a normal CCB.
				 */
				memset(&send_evt, '\0', sizeof(IMMSV_EVT));
				send_evt.type = IMMSV_EVT_TYPE_IMMND;
				send_evt.info.immnd.type =
				    IMMND_EVT_A2ND_CCB_COMPLETED_RSP;
				send_evt.info.immnd.info.ccbUpcallRsp.ccbId =
				    evt->info.ccbId;
				immnd_evt_proc_ccb_compl_rsp(
				    cb, &(send_evt.info.immnd),
				    originatedAtThisNd, clnt_hdl, reply_dest);
				goto done;
			}
		}

		if (err == SA_AIS_OK) {
			/*No implementers anywhere and all is OK */
			/* Check for appliers. */
			SaUint32T applCtn = 0;
			SaUint32T *applConnArr = NULL;
			SaUint32T applArrSize = immModel_getLocalAppliersForCcb(
			    cb, evt->info.ccbId, &applConnArr, &applCtn);

			if (applArrSize) {
				memset(&send_evt, '\0', sizeof(IMMSV_EVT));
				send_evt.type = IMMSV_EVT_TYPE_IMMA;
				send_evt.info.imma.type =
				    IMMA_EVT_ND2A_OI_CCB_COMPLETED_UC;
				send_evt.info.imma.info.ccbCompl.ccbId =
				    evt->info.ccbId;
				send_evt.info.imma.info.ccbCompl.implId = 0;
				send_evt.info.imma.info.ccbCompl.invocation =
				    applCtn;
				int ix = 0;
				for (; ix < applArrSize && err == SA_AIS_OK;
				     ++ix) {
					implHandle = m_IMMSV_PACK_HANDLE(
					    applConnArr[ix], cb->node_id);
					send_evt.info.imma.info.ccbCompl
					    .immHandle = implHandle;

					/*Fetch client node for Applier OI ! */
					immnd_client_node_get(cb, implHandle,
							      &oi_cl_node);
					osafassert(oi_cl_node != NULL);
					if (oi_cl_node->mIsStale) {
						LOG_WA(
						    "Applier client went down so completed upcall not sent");
						continue;
					} else if (immnd_mds_msg_send(
						       cb,
						       NCSMDS_SVC_ID_IMMA_OI,
						       oi_cl_node
							   ->agent_mds_dest,
						       &send_evt) !=
						   NCSCC_RC_SUCCESS) {
						LOG_WA(
						    "Completed upcall for applier failed");
					}
				}
			}

			TRACE_2("DIRECT COMMIT");
			/* TODO:
			   This is a dangerous variant of commit. If any other
			   ND fails to commit => inconsistency. This could
			   happen if implementer disconect or node departure, is
			   not handled correctly/consistently over FEVS. We
			   should transform this to a more elaborate
			   immnd_evt_ccb_commit call.
			 */
			if (immModel_ccbCommit(cb, evt->info.ccbId, &arrSize,
					       &implConnArr)) {
				SaImmRepositoryInitModeT oldRim = cb->mRim;
				cb->mRim = immModel_getRepositoryInitMode(cb);
				if (oldRim != cb->mRim) {
					/* Reset mPbeVeteran to force
					   re-generation of db-file when mRim is
					   changed again. We can not continue
					   using any existing db-file because we
					   would get a gap in the persistent
					   history.
					 */
					cb->mPbeVeteran = false;
					cb->mPbeVeteranB = false;

					TRACE_2(
					    "Repository init mode changed to: %s",
					    (cb->mRim == SA_IMM_INIT_FROM_FILE)
						? "INIT_FROM_FILE"
						: "KEEP_REPOSITORY");
					if (cb->mIsCoord && cb->mPbeFile &&
					    (cb->mRim ==
					     SA_IMM_INIT_FROM_FILE)) {
						cb->mBlockPbeEnable = 0x1;
						/* Prevent PBE re-enable until
						   current PBE has terminated.
						 */
						immnd_announceDump(cb);
						/* Communicates RIM to IMMD.
						   Needed to decide if reload
						   must cause cluster restart.
						*/
					}
				}
			}
			osafassert(arrSize == 0);
			if (applArrSize) {
				memset(&send_evt, '\0', sizeof(IMMSV_EVT));
				send_evt.type = IMMSV_EVT_TYPE_IMMA;
				send_evt.info.imma.type =
				    IMMA_EVT_ND2A_OI_CCB_APPLY_UC;
				send_evt.info.imma.info.ccbCompl.ccbId =
				    evt->info.ccbId;
				send_evt.info.imma.info.ccbCompl.implId = 0;
				send_evt.info.imma.info.ccbCompl.invocation = 0;
				int ix = 0;
				for (; ix < applArrSize && err == SA_AIS_OK;
				     ++ix) {
					implHandle = m_IMMSV_PACK_HANDLE(
					    applConnArr[ix], cb->node_id);
					send_evt.info.imma.info.ccbCompl
					    .immHandle = implHandle;

					/*Fetch client node for Applier OI ! */
					immnd_client_node_get(cb, implHandle,
							      &oi_cl_node);
					osafassert(oi_cl_node != NULL);
					if (oi_cl_node->mIsStale) {
						LOG_WA(
						    "Applier client went down so completed upcall not sent");
						continue;
					} else if (immnd_mds_msg_send(
						       cb,
						       NCSMDS_SVC_ID_IMMA_OI,
						       oi_cl_node
							   ->agent_mds_dest,
						       &send_evt) !=
						   NCSCC_RC_SUCCESS) {
						LOG_WA(
						    "Completed upcall for applier failed");
					}
				}

				free(applConnArr);
				applConnArr = NULL;
			}
		} else {
			SaUint32T clArrSize = 0, *clArr = NULL;
			if (err == SA_AIS_ERR_ACCESS_DENIED) {
				LOG_WA(
				    "Spurious and redundant ccb-apply request ignored ccbId:%u",
				    evt->info.ccbId);
				/* Dont touch the ccb object and dont even reply
				 * to the client. */
				goto done;
			}
			/*err != SA_AIS_OK => generate SaImmOiCcbAbortCallbackT
			 * upcalls
			 */
			immnd_evt_ccb_abort(cb, evt->info.ccbId, &clArr,
					    &clArrSize, NULL);
			/* when the client is not originated from this ND then
			   the client connection must be zero. We are in apply
			   and there will be only one implementer connection and
			   no augumentaion connections.
			*/
			osafassert(!clArrSize || !clArr[0] ||
				   originatedAtThisNd);
			free(clArr);
		}
		TRACE_2("CCB APPLY TERMINATING CCB: %u", evt->info.ccbId);
		bCcbFinalize = 1;

	immediate_reply:
		if (originatedAtThisNd) {
			immnd_client_node_get(cb, clnt_hdl, &cl_node);
			if (cl_node == NULL || cl_node->mIsStale) {
				LOG_WA(
				    "IMMND - Client went down so no response");
			} else {
				TRACE_2("send immediate reply to client");
				memset(&send_evt, '\0', sizeof(IMMSV_EVT));
				send_evt.type = IMMSV_EVT_TYPE_IMMA;
				send_evt.info.imma.info.errRsp.error = err;
				send_evt.info.imma.info.errRsp.errStrings =
				    immModel_ccbGrabErrStrings(cb,
							       evt->info.ccbId);

				if (send_evt.info.imma.info.errRsp.errStrings) {
					send_evt.info.imma.type =
					    IMMA_EVT_ND2A_IMM_ERROR_2;
				} else {
					send_evt.info.imma.type =
					    IMMA_EVT_ND2A_IMM_ERROR;
				}

				if (immnd_mds_send_rsp(cb, &(cl_node->tmpSinfo),
						       &send_evt) !=
				    NCSCC_RC_SUCCESS) {
					LOG_WA(
					    "Failed to send result to Agent over MDS");
				}

				immsv_evt_free_attrNames(
				    send_evt.info.imma.info.errRsp.errStrings);
			}
		}

		if (bCcbFinalize) {
			immModel_ccbFinalize(cb, evt->info.ccbId);
		}
	}
done:
	TRACE_LEAVE();
}

static uint32_t immnd_restricted_ok(IMMND_CB *cb, uint32_t id)
{
	if (cb->mSync) {
		if (id == IMMND_EVT_A2ND_CLASS_CREATE ||
		    id == IMMND_EVT_A2ND_OBJ_SYNC ||
		    id == IMMND_EVT_A2ND_OBJ_SYNC_2 ||
		    id == IMMND_EVT_ND2ND_SYNC_FINALIZE ||
		    id == IMMND_EVT_ND2ND_SYNC_FINALIZE_2 ||
		    id == IMMND_EVT_D2ND_SYNC_ABORT ||
		    id == IMMND_EVT_D2ND_DISCARD_IMPL ||
		    id == IMMND_EVT_D2ND_DISCARD_NODE ||
		    id == IMMND_EVT_A2ND_OI_IMPL_CLR ||
		    id == IMMND_EVT_D2ND_SYNC_FEVS_BASE ||
		    id == IMMND_EVT_A2ND_OI_OBJ_MODIFY ||
		    id == IMMND_EVT_D2ND_IMPLSET_RSP ||
		    id == IMMND_EVT_D2ND_IMPLSET_RSP_2 ||
		    id == IMMND_EVT_D2ND_ADMO_HARD_FINALIZE) {
			return 1;
		}
	}

	if ((cb->preLoadPid > 0) &&
	    (cb->mState == IMM_SERVER_CLUSTER_WAITING)) {
		TRACE("Restricted OK TRUE in preload");
		return 1;
	}

	return 0;
}

/****************************************************************************
 * Name          : immnd_evt_proc_fevs_dispatch
 *
 * Description   : Function to dispatch fevs
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_OCTET_STRING *msg - Message to unpack and dispatch
 *                 bool originatedAtThisNode - Did it come from this node?
 *                 SaImmHandleT clnt_hdl - The client handle (only relevant if
 *                                         originatedAtThisNode is true).
 *                 MDS_DEST reply_dest - The dest of the ND where reply is to
 *                                       go.
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 * Notes         : None.
 *****************************************************************************/
static SaAisErrorT
immnd_evt_proc_fevs_dispatch(IMMND_CB *cb, IMMSV_OCTET_STRING *msg,
			     bool originatedAtThisNd, SaImmHandleT clnt_hdl,
			     MDS_DEST reply_dest, SaUint64T msgNo)
{
	SaAisErrorT error = SA_AIS_OK;
	IMMSV_EVT frwrd_evt;
	NCS_UBAID uba;
	uba.start = NULL;

	memset(&frwrd_evt, '\0', sizeof(IMMSV_EVT));

	/*Unpack the embedded message */
	if (ncs_enc_init_space_pp(&uba, 0, 0) != NCSCC_RC_SUCCESS) {
		LOG_ER("Failed init ubaid");
		error = SA_AIS_ERR_NO_RESOURCES;
		goto unpack_failure;
	}

	if (ncs_encode_n_octets_in_uba(&uba, (uint8_t *)msg->buf, msg->size) !=
	    NCSCC_RC_SUCCESS) {
		LOG_ER("Failed buffer copy");
		error = SA_AIS_ERR_NO_RESOURCES;
		goto unpack_failure;
	}

	/* for debugging messages
	   int size = evt->info.fevsReq.msg.size;
	   if(size >340) {
	   char* tmpData = calloc(1, size);
	   char * data = m_MMGR_DATA_AT_START(uba.ub,
	   size, tmpData);
	   TRACE_2("ABT immnd_mds_dec:DUMP:%u/%p [", size, data);
	   int ix;
	   for(ix = 0; ix < size; ++ix) {
	   unsigned char c = data[ix];
	   unsigned int i = c;
	   TRACE_2("%u|", i);
	   }
	   TRACE_2("]");
	   }
	 */

	ncs_dec_init_space(&uba, uba.start);
	uba.bufp = NULL;

	/* Decode non flat. */
	if (immsv_evt_dec(&uba, &frwrd_evt) != NCSCC_RC_SUCCESS) {
		LOG_ER("Edu decode Failed");
		error = SA_AIS_ERR_LIBRARY;
		goto unpack_failure;
	}

	if (frwrd_evt.type != IMMSV_EVT_TYPE_IMMND) {
		LOG_ER("IMMND - Unknown Event Type");
		error = SA_AIS_ERR_LIBRARY;
		goto unpack_failure;
	}

	if (!(cb->mAccepted ||
	      immnd_restricted_ok(cb, frwrd_evt.info.immnd.type))) {
		error = SA_AIS_ERR_ACCESS;
		goto discard_message;
	}

	/*Dispatch the unpacked FEVS message */
	immsv_msg_trace_rec(frwrd_evt.sinfo.dest, &frwrd_evt);

	switch (frwrd_evt.info.immnd.type) {
	case IMMND_EVT_A2ND_OBJ_CREATE:
	case IMMND_EVT_A2ND_OBJ_CREATE_2:
		immnd_evt_proc_object_create(cb, &frwrd_evt.info.immnd,
					     originatedAtThisNd, clnt_hdl,
					     reply_dest);
		break;

	case IMMND_EVT_A2ND_OI_OBJ_CREATE:
	case IMMND_EVT_A2ND_OI_OBJ_CREATE_2:
		immnd_evt_proc_rt_object_create(cb, &frwrd_evt.info.immnd,
						originatedAtThisNd, clnt_hdl,
						reply_dest);
		break;

	case IMMND_EVT_A2ND_OBJ_MODIFY:
		immnd_evt_proc_object_modify(cb, &frwrd_evt.info.immnd,
					     originatedAtThisNd, clnt_hdl,
					     reply_dest);
		break;

	case IMMND_EVT_A2ND_OI_OBJ_MODIFY:
		immnd_evt_proc_rt_object_modify(cb, &frwrd_evt.info.immnd,
						originatedAtThisNd, clnt_hdl,
						reply_dest, msgNo);
		break;

	case IMMND_EVT_A2ND_OBJ_DELETE:
		immnd_evt_proc_object_delete(cb, &frwrd_evt.info.immnd,
					     originatedAtThisNd, clnt_hdl,
					     reply_dest);
		break;

	case IMMND_EVT_A2ND_OI_OBJ_DELETE:
		immnd_evt_proc_rt_object_delete(cb, &frwrd_evt.info.immnd,
						originatedAtThisNd, clnt_hdl,
						reply_dest);
		break;

	case IMMND_EVT_A2ND_OBJ_SYNC:
	case IMMND_EVT_A2ND_OBJ_SYNC_2:
		immnd_evt_proc_object_sync(cb, &frwrd_evt.info.immnd,
					   originatedAtThisNd, clnt_hdl,
					   reply_dest, msgNo);
		break;

	case IMMND_EVT_A2ND_IMM_ADMOP:
	case IMMND_EVT_A2ND_IMM_ADMOP_ASYNC:
		immnd_evt_proc_admop(cb, &frwrd_evt.info.immnd,
				     originatedAtThisNd, clnt_hdl, reply_dest);
		break;

	case IMMND_EVT_A2ND_CLASS_CREATE:
		immnd_evt_proc_class_create(cb, &frwrd_evt.info.immnd,
					    originatedAtThisNd, clnt_hdl,
					    reply_dest);
		break;

	case IMMND_EVT_A2ND_CLASS_DELETE:
		immnd_evt_proc_class_delete(cb, &frwrd_evt.info.immnd,
					    originatedAtThisNd, clnt_hdl,
					    reply_dest);
		break;

	case IMMND_EVT_D2ND_DISCARD_IMPL:
		immnd_evt_proc_discard_impl(cb, &frwrd_evt.info.immnd,
					    originatedAtThisNd, clnt_hdl,
					    reply_dest);
		break;

	case IMMND_EVT_D2ND_DISCARD_NODE:
		immnd_evt_proc_discard_node(cb, &frwrd_evt.info.immnd,
					    originatedAtThisNd, clnt_hdl,
					    reply_dest);
		break;

	case IMMND_EVT_D2ND_ADMINIT:
		immnd_evt_proc_adminit_rsp(cb, &frwrd_evt.info.immnd,
					   originatedAtThisNd, clnt_hdl,
					   reply_dest);
		break;

	case IMMND_EVT_D2ND_IMPLSET_RSP:
	case IMMND_EVT_D2ND_IMPLSET_RSP_2:
		immnd_evt_proc_impl_set_rsp(cb, &frwrd_evt.info.immnd,
					    originatedAtThisNd, clnt_hdl,
					    reply_dest);
		break;

	case IMMND_EVT_D2ND_CCBINIT:
		immnd_evt_proc_ccbinit_rsp(cb, &frwrd_evt.info.immnd,
					   originatedAtThisNd, clnt_hdl,
					   reply_dest);
		break;

	case IMMND_EVT_A2ND_CCB_APPLY:
		immnd_evt_proc_ccb_apply(cb, &frwrd_evt.info.immnd,
					 originatedAtThisNd, clnt_hdl,
					 reply_dest, false);
		break;

	case IMMND_EVT_A2ND_CCB_VALIDATE:
		immnd_evt_proc_ccb_apply(cb, &frwrd_evt.info.immnd,
					 originatedAtThisNd, clnt_hdl,
					 reply_dest, true);
		break;

	case IMMND_EVT_A2ND_CCB_FINALIZE:
		immnd_evt_proc_ccb_finalize(cb, &frwrd_evt.info.immnd,
					    originatedAtThisNd, clnt_hdl,
					    reply_dest);
		break;

	case IMMND_EVT_D2ND_ABORT_CCB:
		immnd_evt_proc_ccb_finalize(cb, &frwrd_evt.info.immnd,
					    /*originatedAtThisNd */ false,
					    clnt_hdl, reply_dest);
		break;

	case IMMND_EVT_A2ND_OI_CCB_AUG_INIT:
		immnd_evt_ccb_augment_init(cb, &frwrd_evt.info.immnd,
					   originatedAtThisNd, clnt_hdl,
					   reply_dest);
		break;

	case IMMND_EVT_A2ND_AUG_ADMO:
		immnd_evt_ccb_augment_admo(cb, &frwrd_evt.info.immnd,
					   originatedAtThisNd, clnt_hdl,
					   reply_dest);
		break;

	case IMMND_EVT_A2ND_OI_IMPL_CLR:
		immnd_evt_proc_impl_clr(cb, &frwrd_evt.info.immnd,
					originatedAtThisNd, clnt_hdl,
					reply_dest);
		break;

	case IMMND_EVT_A2ND_OI_CL_IMPL_SET:
		immnd_evt_proc_cl_impl_set(cb, &frwrd_evt.info.immnd,
					   originatedAtThisNd, clnt_hdl,
					   reply_dest);
		break;

	case IMMND_EVT_A2ND_OI_CL_IMPL_REL:
		immnd_evt_proc_cl_impl_rel(cb, &frwrd_evt.info.immnd,
					   originatedAtThisNd, clnt_hdl,
					   reply_dest);
		break;

	case IMMND_EVT_A2ND_OI_OBJ_IMPL_SET:
		immnd_evt_proc_obj_impl_set(cb, &frwrd_evt.info.immnd,
					    originatedAtThisNd, clnt_hdl,
					    reply_dest);
		break;

	case IMMND_EVT_A2ND_OI_OBJ_IMPL_REL:
		immnd_evt_proc_obj_impl_rel(cb, &frwrd_evt.info.immnd,
					    originatedAtThisNd, clnt_hdl,
					    reply_dest);
		break;

	case IMMND_EVT_A2ND_ADMO_FINALIZE:
		immnd_evt_proc_admo_finalize(cb, &frwrd_evt.info.immnd,
					     originatedAtThisNd, clnt_hdl,
					     reply_dest);
		break;

	case IMMND_EVT_D2ND_ADMO_HARD_FINALIZE:
		immnd_evt_proc_admo_hard_finalize(cb, &frwrd_evt.info.immnd,
						  originatedAtThisNd, clnt_hdl,
						  reply_dest);
		break;

	case IMMND_EVT_A2ND_ADMO_SET:
		immnd_evt_proc_admo_set(cb, &frwrd_evt.info.immnd,
					originatedAtThisNd, clnt_hdl,
					reply_dest);
		break;

	case IMMND_EVT_A2ND_ADMO_RELEASE:
		immnd_evt_proc_admo_release(cb, &frwrd_evt.info.immnd,
					    originatedAtThisNd, clnt_hdl,
					    reply_dest);
		break;

	case IMMND_EVT_A2ND_ADMO_CLEAR:
		immnd_evt_proc_admo_clear(cb, &frwrd_evt.info.immnd,
					  originatedAtThisNd, clnt_hdl,
					  reply_dest);
		break;

	case IMMND_EVT_ND2ND_SYNC_FINALIZE:
		immnd_evt_proc_finalize_sync(cb, &frwrd_evt.info.immnd,
					     originatedAtThisNd, clnt_hdl,
					     reply_dest);
		break;

	case IMMND_EVT_ND2ND_SYNC_FINALIZE_2:
		immnd_evt_proc_finalize_sync(cb, &frwrd_evt.info.immnd,
					     originatedAtThisNd, clnt_hdl,
					     reply_dest);
		break;

	case IMMND_EVT_A2ND_CCB_COMPLETED_RSP:
	case IMMND_EVT_A2ND_CCB_COMPLETED_RSP_2:
		immnd_evt_proc_ccb_compl_rsp(cb, &frwrd_evt.info.immnd,
					     originatedAtThisNd, clnt_hdl,
					     reply_dest);
		break;

	case IMMND_EVT_A2ND_CCB_OBJ_CREATE_RSP:
	case IMMND_EVT_A2ND_CCB_OBJ_CREATE_RSP_2:
		immnd_evt_proc_ccb_obj_create_rsp(cb, &frwrd_evt.info.immnd,
						  originatedAtThisNd, clnt_hdl,
						  reply_dest);
		break;

	case IMMND_EVT_A2ND_CCB_OBJ_MODIFY_RSP:
	case IMMND_EVT_A2ND_CCB_OBJ_MODIFY_RSP_2:
		immnd_evt_proc_ccb_obj_modify_rsp(cb, &frwrd_evt.info.immnd,
						  originatedAtThisNd, clnt_hdl,
						  reply_dest);
		break;

	case IMMND_EVT_A2ND_CCB_OBJ_DELETE_RSP:
	case IMMND_EVT_A2ND_CCB_OBJ_DELETE_RSP_2:
		immnd_evt_proc_ccb_obj_delete_rsp(cb, &frwrd_evt.info.immnd,
						  originatedAtThisNd, clnt_hdl,
						  reply_dest);
		break;

	case IMMND_EVT_A2ND_PBE_PRT_OBJ_CREATE_RSP:
		immnd_evt_pbe_rt_obj_create_rsp(cb, &frwrd_evt.info.immnd,
						originatedAtThisNd, clnt_hdl,
						reply_dest);
		break;

	case IMMND_EVT_A2ND_PBE_PRTO_DELETES_COMPLETED_RSP:
		immnd_evt_pbe_rt_obj_deletes_rsp(cb, &frwrd_evt.info.immnd,
						 originatedAtThisNd, clnt_hdl,
						 reply_dest);
		break;

	case IMMND_EVT_A2ND_PBE_PRT_ATTR_UPDATE_RSP:
		immnd_evt_pbe_rt_attr_update_rsp(cb, &frwrd_evt.info.immnd,
						 originatedAtThisNd, clnt_hdl,
						 reply_dest);
		break;

	case IMMND_EVT_A2ND_PBE_ADMOP_RSP:
		immnd_evt_pbe_admop_rsp(cb, &frwrd_evt.info.immnd,
					originatedAtThisNd, clnt_hdl,
					reply_dest);
		break;

	case IMMND_EVT_D2ND_SYNC_FEVS_BASE:
		immnd_evt_sync_fevs_base(cb, &frwrd_evt.info.immnd,
					 originatedAtThisNd, clnt_hdl,
					 reply_dest);
		break;

	case IMMND_EVT_A2ND_OBJ_SAFE_READ:
		TRACE("IMMND_EVT_A2ND_OBJ_SAFE_READ Received over fevs");
		immnd_evt_safe_read_lock(cb, &frwrd_evt.info.immnd,
					 originatedAtThisNd, clnt_hdl,
					 reply_dest);
		break;

	default:
		LOG_ER(
		    "UNPACK FAILURE, unrecognized message type: %u over FEVS",
		    frwrd_evt.info.immnd.type);
		break;
	}

discard_message:
unpack_failure:

	if (error == SA_AIS_ERR_ACCESS) {
		if (frwrd_evt.info.immnd.type ==
		    IMMND_EVT_A2ND_OI_CL_IMPL_SET) {
			LOG_NO(
			    "Sync client discarded classimplementer set. Impl-id:%u Class:%s",
			    frwrd_evt.info.immnd.info.implSet.impl_id,
			    frwrd_evt.info.immnd.info.implSet.impl_name.buf);
		} else if (frwrd_evt.info.immnd.type ==
			   IMMND_EVT_A2ND_OI_CL_IMPL_REL) {
			LOG_NO(
			    "Sync client discarded classimplementer release. Impl-id:%u Class:%s",
			    frwrd_evt.info.immnd.info.implSet.impl_id,
			    frwrd_evt.info.immnd.info.implSet.impl_name.buf);
		}
	}

	if (uba.start) {
		m_MMGR_FREE_BUFR_LIST(uba.start);
	}
	immnd_evt_destroy(&frwrd_evt, false, __LINE__);
	if ((error != SA_AIS_OK) && (error != SA_AIS_ERR_ACCESS)) {
		TRACE_2("Could not process FEVS message, ERROR:%u", error);
	}
	return error;
}

/****************************************************************************
 * Name          : immnd_evt_proc_dump_ok
 *
 * Description   : Function to increment epoch at all nodes, after dump
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *                 IMMSV_SEND_INFO *sinfo - Sender MDS information.
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 *****************************************************************************/
static uint32_t immnd_evt_proc_dump_ok(IMMND_CB *cb, IMMND_EVT *evt,
				       IMMSV_SEND_INFO *sinfo)
{
	TRACE_ENTER();
	if (cb->mAccepted) {
		cb->mRulingEpoch = evt->info.ctrl.rulingEpoch;
		TRACE_2("Ruling Epoch:%u", cb->mRulingEpoch);

		if ((cb->mMyEpoch + 1) == cb->mRulingEpoch) {
			++(cb->mMyEpoch);
			TRACE_2("MyEpoch incremented to %u", cb->mMyEpoch);

			/*Note that the searchOp for the dump should really be
			   done here. Currently it is done before sending the
			   dump request which means that technically the
			   epoch/contents of the dump is not guaranteed to match
			   exactly the final contents of the epoch at the other
			   nodes. On the other hand, this slight difference is
			   currently not persistent. */

			int retryCount = 0;
			for (; !immnd_is_immd_up(cb) && retryCount < 8;
			     ++retryCount) {
				LOG_WA(
				    "Coord blocked in adjust Epoch because IMMD is DOWN %u",
				    retryCount);
				sleep(1);
			}

			if (immnd_is_immd_up(cb)) {
				immnd_adjustEpoch(cb, true);
			} else {
				/* Not critical to increment epoch for dump */
				LOG_ER(
				    "Dump-ok failed to adjust epoch, IMMD DOWN:");
			}
		} else {
			LOG_ER(
			    "Missmatch on epoch mine:%u proposed new epoch:%u",
			    cb->mMyEpoch, cb->mRulingEpoch);
		}
	}
	TRACE_LEAVE();
	return NCSCC_RC_SUCCESS;
}

/****************************************************************************
 * Name          : immnd_evt_proc_abort_sync
 *
 * Description   : Function to process abort sync message
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *                 IMMSV_SEND_INFO *sinfo - Sender MDS information.
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 *****************************************************************************/
uint32_t immnd_evt_proc_abort_sync(IMMND_CB *cb, IMMND_EVT *evt,
				   IMMSV_SEND_INFO *sinfo)
{
	TRACE_ENTER();
	osafassert(cb->mRulingEpoch <= evt->info.ctrl.rulingEpoch);
	cb->mRulingEpoch = evt->info.ctrl.rulingEpoch;

	LOG_WA("Global ABORT SYNC received for epoch %u", cb->mRulingEpoch);

	if (cb->mIsCoord) { /* coord should already be up to date. */
		osafassert(cb->mMyEpoch == cb->mRulingEpoch);
		osafassert(!(cb->mSyncFinalizing));
	} else { /* Noncoord IMMNDs */
		if (cb->mState == IMM_SERVER_SYNC_CLIENT ||
		    cb->mState ==
			IMM_SERVER_SYNC_PENDING) { /* Sync client will have to
						      restart the sync */
			cb->mState = IMM_SERVER_LOADING_PENDING;
			LOG_WA(
			    "SERVER STATE: IMM_SERVER_SYNC_CLIENT --> IMM_SERVER_LOADING_PENDING (sync aborted)");
			cb->mStep = 0;
			osaf_clock_gettime(CLOCK_MONOTONIC, &cb->mJobStart);
			cb->mMyEpoch = 0;
			cb->mSync = false;
			osafassert(!(cb->mAccepted));
		} else if (cb->mState ==
			   IMM_SERVER_READY) { /* old (nonccord) members */
			if (cb->mRulingEpoch == cb->mMyEpoch + 1) {
				cb->mMyEpoch = cb->mRulingEpoch;
			} else if (cb->mRulingEpoch != cb->mMyEpoch) {
				LOG_ER(
				    "immnd_evt_proc_abort_sync not clean on epoch: "
				    "RE:%u ME:%u",
				    cb->mRulingEpoch, cb->mMyEpoch);
			}

			int retryCount = 0;
			for (; !immnd_is_immd_up(cb) && retryCount < 16;
			     ++retryCount) {
				LOG_WA(
				    "IMMND can not adjust epoch because IMMD is DOWN %u",
				    retryCount);
				sleep(1);
			}
			immnd_adjustEpoch(
			    cb, true); /* will osafassert if immd is down. */
		}
		immModel_abortSync(cb);
	}
	TRACE_LEAVE();
	return NCSCC_RC_SUCCESS;
}

/****************************************************************************
 * Name          : immnd_evt_proc_pbe_prto_purge_mutations
 *
 * Description   : Function to remove un-ack'ed mutation records for
 *                 persistent runtime objects (prtos). This is needed if
 *                 the PBE process hangs and needs to be restarted.
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *                 IMMSV_SEND_INFO *sinfo - Sender MDS information.
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 *****************************************************************************/
uint32_t immnd_evt_proc_pbe_prto_purge_mutations(IMMND_CB *cb, IMMND_EVT *evt,
						 IMMSV_SEND_INFO *sinfo)
{
	SaUint32T reqConnArrSize = 0;
	SaUint32T *reqConnArr = NULL;
	TRACE_ENTER();
	osafassert(cb->mRulingEpoch <= evt->info.ctrl.rulingEpoch);
	cb->mRulingEpoch = evt->info.ctrl.rulingEpoch;

	LOG_WA("Global PURGE PERSISTENT RTO mutations received in epoch %u",
	       cb->mRulingEpoch);

	if (cb->mIsCoord) { /* coord should already be up to date. */
		osafassert(cb->mMyEpoch == cb->mRulingEpoch);
	}

	immModel_pbePrtoPurgeMutations(cb, cb->node_id, &reqConnArrSize,
				       &reqConnArr);

	if (cb->mPbeVeteran || cb->mPbeVeteranB) {
		/* This should be redundant at coord. It is mainly for the
		   noncoord immnd, in case it gets elected coord later.
		*/
		cb->mPbeVeteran = false;
		cb->mPbeVeteranB = false;
	}

	if (cb->pbePid2 != 0) {
		LOG_WA(
		    "Forced to kill SLAVE PBE due to purge of PRTO mutations, sending SIGKILL");
		kill(cb->pbePid2, SIGKILL);
	}

	if (reqConnArrSize) {
		uint32_t rc = NCSCC_RC_SUCCESS;
		IMMSV_EVT send_evt;
		IMMND_IMM_CLIENT_NODE *cl_node = NULL;
		int ix = 0;
		osafassert(reqConnArr);
		memset(&send_evt, '\0', sizeof(IMMSV_EVT));
		send_evt.type = IMMSV_EVT_TYPE_IMMA;
		send_evt.info.imma.type = IMMA_EVT_ND2A_IMM_ERROR;
		send_evt.info.imma.info.errRsp.error =
		    SA_AIS_ERR_TRY_AGAIN; /* Fall back assumed. */

		for (; ix < reqConnArrSize; ++ix) {
			SaImmHandleT tmp_hdl =
			    m_IMMSV_PACK_HANDLE(reqConnArr[ix], cb->node_id);
			immnd_client_node_get(cb, tmp_hdl, &cl_node);
			if (cl_node == NULL || cl_node->mIsStale) {
				LOG_WA(
				    "IMMND - Client went down so no response");
				continue;
			}
			TRACE("Sending TRY_AGAIN reply to connection %u",
			      reqConnArr[ix]);
			rc = immnd_mds_send_rsp(cb, &(cl_node->tmpSinfo),
						&send_evt);
			if (rc != NCSCC_RC_SUCCESS) {
				LOG_WA(
				    "Failed to send response to agent/client "
				    "over MDS rc:%u",
				    rc);
			}
		}

		free(reqConnArr);
	}

	TRACE_LEAVE();
	return NCSCC_RC_SUCCESS;
}

/****************************************************************************
 * Name          : immnd_evt_proc_start_sync
 *
 * Description   : Function to process start sync message
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *                 IMMSV_SEND_INFO *sinfo - Sender MDS information.
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 *****************************************************************************/
static uint32_t immnd_evt_proc_start_sync(IMMND_CB *cb, IMMND_EVT *evt,
					  IMMSV_SEND_INFO *sinfo)
{
	if ((cb->mState == IMM_SERVER_LOADING_CLIENT) &&
	    (immModel_getLoader(cb) == 0)) {
		immnd_adjustEpoch(cb, true);
		immnd_ackToNid(NCSCC_RC_SUCCESS);
		cb->mState = IMM_SERVER_READY;
		LOG_NO(
		    "SERVER STATE: IMM_SERVER_LOADING_CLIENT --> IMM_SERVER_READY (materialized by start sync)");
		/* This is the case where loading has completed,
		   yet this node has not grabbed this fact in immnd_proc
		   under case IMM_SERVER_LOADING_CLIENT, and now a start
		   sync arrives. We force the completion of the loading
		   client phase here. This node will now be an "old member"
		   with respect to the just arriving start-sync.
		   Search for "ticket:#598" in immnd_proc.c
		 */
		immModel_setScAbsenceAllowed(cb);
	} else if ((cb->mState == IMM_SERVER_SYNC_CLIENT) &&
		   (immnd_syncComplete(cb, false, cb->mStep))) {
		cb->mStep = 0;
		osaf_clock_gettime(CLOCK_MONOTONIC, &cb->mJobStart);
		cb->mState = IMM_SERVER_READY;
		immnd_ackToNid(NCSCC_RC_SUCCESS);
		LOG_NO(
		    "SERVER STATE: IMM_SERVER_SYNC_CLIENT --> IMM SERVER READY (materialized by start sync)");
		/* This is the case where sync has completed,
		   this node is a sync client,
		   yet this node has not grabbed this fact in immnd_proc
		   under case IMM_SERVER_SYNC_CLIENT, and now a start
		   sync arrives. We force the completion of the prior sync
		   client phase here. This node will now be an "old member"
		   with respect to the just arriving start-sync.
		   Search for "ticket:#599" in immnd_proc.c
		 */
		immModel_setScAbsenceAllowed(cb);
	}

	cb->mRulingEpoch = evt->info.ctrl.rulingEpoch;
	TRACE_2("Ruling Epoch:%u", cb->mRulingEpoch);

	if (cb->mState == IMM_SERVER_SYNC_PENDING) {
		/*This node wants to be synced. */
		osafassert(!cb->mAccepted);
		immModel_recognizedIsolated(
		    cb); /*Reply to req sync not arrived ? */
		cb->mSync = true;
		cb->mMyEpoch = cb->mRulingEpoch - 1;
		TRACE_2("Adjust fevs count:%llu %llu %llu", cb->highestReceived,
			cb->highestProcessed, evt->info.ctrl.fevsMsgStart);

		osafassert(cb->highestProcessed <= evt->info.ctrl.fevsMsgStart);
		osafassert(cb->highestReceived <= evt->info.ctrl.fevsMsgStart);
		cb->highestProcessed = evt->info.ctrl.fevsMsgStart;
		cb->highestReceived = evt->info.ctrl.fevsMsgStart;
	}

	/*Nodes that are not accepted and that have not requested sync do not
	   partake in this sync. They dont try to keep up with info on others
	 */
	if (cb->mMyEpoch + 1 == cb->mRulingEpoch) {
		/*old members and joining sync'ers */
		if (!cb->mSync) { /*If I am an old established member then
				     increase my epoch now! */
			cb->mMyEpoch++;
			if (!(cb->mIsCoord)) {
				/* Sync apparently started by coord => this
				   immnd which is not coord can forget Lost
				   Nodes. This is mostly relevant for "standby"
				   i.e. the non-coord immnd which is on an SC.
				 */
				cb->mLostNodes = 0;
			}
		}
		immModel_prepareForSync(cb, cb->mSync);
		cb->mPendSync = 0; // Sync can now begin.
	} else {
		if (cb->mMyEpoch + 1 < cb->mRulingEpoch) {
			if (cb->mState > IMM_SERVER_LOADING_PENDING) {
				LOG_WA(
				    "Imm at this node has epoch %u, "
				    "appears to be a stragler in wrong state %u",
				    cb->mMyEpoch, cb->mState);
				abort();
			} else {
				TRACE_2(
				    "This nodes apparently missed start of sync");
			}
		} else {
			osafassert(cb->mMyEpoch + 1 > cb->mRulingEpoch);
			LOG_WA(
			    "Imm at this evs node has epoch %u, "
			    "COORDINATOR appears to be a stragler!!, aborting.",
			    cb->mMyEpoch);
			abort();
			/* TODO: 080414 re-inserted the osafassert/abort ...
			   This is an extreemely odd case. Possibly it could
			   occur after a failover ?? */
		}
	}
	return NCSCC_RC_SUCCESS;
}

/****************************************************************************
 * Name          : immnd_evt_proc_reset
 *
 * Description   : Function to process RESET broadcast from IMMD
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *                 IMMSV_SEND_INFO *sinfo - Sender MDS information.
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 *****************************************************************************/
static uint32_t immnd_evt_proc_reset(IMMND_CB *cb, IMMND_EVT *evt,
				     IMMSV_SEND_INFO *sinfo)
{
	TRACE_ENTER();
	if (cb->mIntroduced == 1) {
		LOG_ER("IMMND forced to restart on order from IMMD, exiting");
		if (cb->mState < IMM_SERVER_READY) {
			immnd_ackToNid(NCSCC_RC_FAILURE);
		}
		exit(1);
	} else {
		LOG_NO(
		    "IMMND received reset order from IMMD, but has just restarted - ignoring");
	}
	TRACE_LEAVE();
	return NCSCC_RC_SUCCESS;
}

/****************************************************************************
 * Name          : immnd_evt_proc_loading_ok
 *
 * Description   : Function to process loading_ok broadcast
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *                 IMMSV_SEND_INFO *sinfo - Sender MDS information.
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 *****************************************************************************/
static uint32_t immnd_evt_proc_loading_ok(IMMND_CB *cb, IMMND_EVT *evt,
					  IMMSV_SEND_INFO *sinfo)
{
	TRACE_ENTER();
	cb->mRulingEpoch = evt->info.ctrl.rulingEpoch;
	TRACE_2("Loading can start, ruling epoch:%u", cb->mRulingEpoch);

	if ((cb->mState == IMM_SERVER_LOADING_PENDING) ||
	    (cb->mState == IMM_SERVER_LOADING_CLIENT)) {
		osafassert(((cb->mMyEpoch + 1) == cb->mRulingEpoch));
		++(cb->mMyEpoch);
		immModel_prepareForLoading(cb);
		TRACE_2(
		    "prepareForLoading non-coord variant. fevsMsgCount reset to %llu",
		    cb->highestProcessed);
		cb->highestProcessed = evt->info.ctrl.fevsMsgStart;
		cb->highestReceived = evt->info.ctrl.fevsMsgStart;
		if (!(cb->mAccepted)) {
			osafassert(cb->mState == IMM_SERVER_LOADING_PENDING);
			LOG_NO(
			    "SERVER STATE: IMM_SERVER_LOADING_PENDING --> IMM_SERVER_LOADING_CLIENT (materialized by proc_loading_ok)");
			cb->mState = IMM_SERVER_LOADING_CLIENT;
			cb->mStep = 0;
			osaf_clock_gettime(CLOCK_MONOTONIC, &cb->mJobStart);
			cb->mAccepted = true;
			if (cb->mCanBeCoord) {
				cb->mIsOtherScUp = true;
			}
		}
	} else if (cb->mState == IMM_SERVER_LOADING_SERVER) {
		osafassert(cb->mMyEpoch == cb->mRulingEpoch);
		immModel_prepareForLoading(cb);
		cb->highestProcessed = evt->info.ctrl.fevsMsgStart;
		cb->highestReceived = evt->info.ctrl.fevsMsgStart;
		TRACE_2(
		    "prepareForLoading coord variant. fevsMsgCount reset to %llu",
		    cb->highestProcessed);
		if (cb->m2Pbe == 2) {
			/* 2PBE but apparently did not succeed in obtaining
			   preload parameters. Could for example be initial
			   start.
			 */
			cb->m2Pbe = 1;
		}
	} else {
		/* Note: corrected for case of IMMND at *both* controllers
		   failing. This must force down IMMND on all payloads, since
		   these can not act as IMM coordinator. */
		if (cb->mState > IMM_SERVER_SYNC_PENDING) {
			LOG_ER(
			    "This IMMND can not accept start loading in state %s %u, exiting",
			    (cb->mState == IMM_SERVER_SYNC_CLIENT)
				? "IMM_SERVER_SYNC_CLIENT"
				: (cb->mState == IMM_SERVER_SYNC_SERVER)
				      ? "IMM_SERVER_SYNC_SERVER"
				      : (cb->mState == IMM_SERVER_READY)
					    ? "IMM_SERVER_READY"
					    : "UNKNOWN",
			    cb->mState);
			exit(1);
		}
	}
	TRACE_LEAVE();
	return NCSCC_RC_SUCCESS;
}

/****************************************************************************
 * Name          : immnd_evt_proc_sync_req
 *
 * Description   : Function to process sync request over IMMD.
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *                 IMMSV_SEND_INFO *sinfo - Sender MDS information.
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 *****************************************************************************/
static uint32_t immnd_evt_proc_sync_req(IMMND_CB *cb, IMMND_EVT *evt,
					IMMSV_SEND_INFO *sinfo)
{
	TRACE_2("SYNC REQUEST RECEIVED VIA IMMD");

	if ((evt->info.ctrl.nodeId == cb->node_id) && (cb->mIntroduced) &&
	    !(cb->mSync)) {
		TRACE_2("Sync client");
		immModel_recognizedIsolated(cb);
	} else if (cb->mIsCoord) {
		TRACE_2("Set marker for sync at coordinator");
		cb->mSyncRequested = true;
		if (cb->mLostNodes > 0) {
			cb->mLostNodes--;
		}
		/*osafassert(cb->mRulingEpoch == evt->info.ctrl.rulingEpoch); */
		TRACE_2("At COORD: My Ruling Epoch:%u Cenral Ruling Epoch:%u",
			cb->mRulingEpoch, evt->info.ctrl.rulingEpoch);
	}
	return NCSCC_RC_SUCCESS;
}

/****************************************************************************
 * Name          : immnd_evt_proc_intro_rsp
 *
 * Description   : Function to process intro reply from immd.
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *                 IMMSV_SEND_INFO *sinfo - Sender MDS information.
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 *****************************************************************************/
static uint32_t immnd_evt_proc_intro_rsp(IMMND_CB *cb, IMMND_EVT *evt,
					 IMMSV_SEND_INFO *sinfo)
{
	TRACE_ENTER2("evt->info.ctrl.nodeId(%x) != cb->node_id:(%x) ?%u",
		     evt->info.ctrl.nodeId, cb->node_id,
		     evt->info.ctrl.nodeId != cb->node_id);
	cb->mNumNodes++;
	TRACE("immnd_evt_proc_intro_rsp cb->mNumNodes: %u", cb->mNumNodes);
	LOG_IN("immnd_evt_proc_intro_rsp: epoch:%i rulingEpoch:%u",
	       cb->mMyEpoch, evt->info.ctrl.rulingEpoch);
	if (evt->info.ctrl.rulingEpoch > cb->mRulingEpoch) {
		cb->mRulingEpoch = evt->info.ctrl.rulingEpoch;
	}

	if (evt->info.ctrl.nodeId == cb->node_id) {
		/*This node was introduced to the IMM cluster */
		uint8_t oldCanBeCoord = cb->mCanBeCoord;
		cb->mIntroduced = 1;
		if (evt->info.ctrl.canBeCoord == 3) {
			cb->m2Pbe = 1;
			evt->info.ctrl.canBeCoord = 1;
			LOG_IN("2PBE SYNC CASE CAUGHT oldCanBeCoord:%u",
			       oldCanBeCoord);
		}

		cb->mCanBeCoord = evt->info.ctrl.canBeCoord;
		if ((cb->mCanBeCoord == 2) && (cb->m2Pbe < 2) &&
		    immnd_cb->isNodeTypeController) {
			LOG_NO("2PBE startup arbitration initiated from IMMD");
			cb->m2Pbe = 2;
			/* mCanBeCoord > 0  => This immnd resides on an SC. */
			/* mCanBeCoord == 2  => IMMD has not chosen coord yet.
			 */

			/* m2Pbe > 0  => Redunant PBE configured */
			/* m2Pbe == 2  => IMMND has not yet sent PBE stats to
			 * IMMD */
		} else if ((oldCanBeCoord == 2) && (cb->mCanBeCoord == 1)) {
			LOG_NO("2PBE startup arbitration resolved by IMMD");
			if (evt->info.ctrl.isCoord == 0) {
				LOG_NO("2PBE other IMMND chosen as coord");
			}
		} else if ((cb->mCanBeCoord == 2) &&
			   !immnd_cb->isNodeTypeController) {
			cb->mCanBeCoord = 0;
		}

		if (cb->m2Pbe && cb->mCanBeCoord) {
			const char *pbe_file_suffix =
			    getenv("IMMSV_PBE_FILE_SUFFIX");
			if (!pbe_file_suffix) {
				char pbeFileSuffix[64];
				snprintf(pbeFileSuffix, 64, ".%x", cb->node_id);
				osafassert(setenv("IMMSV_PBE_FILE_SUFFIX",
						  pbeFileSuffix, 0) == 0);
			}
			LOG_IN("cb->m2Pbe: %u  cb->mCanBeCoord:%u", cb->m2Pbe,
			       cb->mCanBeCoord);
			LOG_NO("2PBE configured, IMMSV_PBE_FILE_SUFFIX:%s (%s)",
			       getenv("IMMSV_PBE_FILE_SUFFIX"),
			       (cb->mCanBeCoord == 2)
				   ? "preload"
				   : ((oldCanBeCoord == 2) ? "load" : "sync"));
		}

		if (cb->mCanBeCoord == 4) {
			osafassert(!(cb->m2Pbe));
			cb->mScAbsenceAllowed = evt->info.ctrl.ndExecPid;
			LOG_IN(
			    "cb->mScAbsenceAllowed:%u evt->info.ctrl.ndExecPid:%u",
			    cb->mScAbsenceAllowed, evt->info.ctrl.ndExecPid);
			LOG_IN(
			    "SC_ABSENCE_ALLOWED is configured for %u seconds. CanBeCoord:%u",
			    cb->mScAbsenceAllowed, cb->mCanBeCoord);
		}

		if (evt->info.ctrl.isCoord) {
			if (cb->mIsCoord) {
				LOG_NO(
				    "This IMMND re-elected coord redundantly, failover ?");
			} else {
				LOG_NO("This IMMND is now the NEW Coord");
				if (cb->m2Pbe) {
					cb->mPbeVeteran =
					    cb
						->mPbeVeteranB; /* switch of
								   role. */
					cb->mPbeVeteranB = false;
				} else if (cb->mPbeVeteran &&
					   !immModel_pbeIsInSync(cb, true)) {
					/* Note: This is to avoid a race between
					   the old PBE at other SC terminating
					   and the new PBE at this SC starting.
					   Both accessing the *same* shared
					   imm.db overlapping in time. This can
					   happen since posix advisory locks may
					   not be reliable over NFS. The result
					   can be a corrupted imm.db. See
					   ticket:
					   http://devel.opensaf.org/ticket/2717
					   But: this race is not relevant if we
					   are running 2Pbe.
					*/
					LOG_NO(
					    "PBE writing when new coord elected => "
					    "force PBE to regenerate db file");
					cb->mPbeVeteran = false;
				}
			}
		}
		if (cb->mIsCoord) {
			if (!(evt->info.ctrl.isCoord)) {
				LOG_NO(
				    "Avoided canceling coord - SHOULD NOT GET HERE");
			}
		} else {
			LOG_IN("SETTING COORD TO %u CLOUD PROTO",
			       evt->info.ctrl.isCoord);
			cb->mIsCoord = evt->info.ctrl.isCoord;
		}
		osafassert(!cb->mIsCoord || cb->mCanBeCoord);
		cb->mRulingEpoch = evt->info.ctrl.rulingEpoch;
		if (cb->mRulingEpoch) {
			TRACE_2("Ruling Epoch:%u", cb->mRulingEpoch);
		}
	} else {
		/* Received intro for some other node. We should be an SC and
		   the intro should be for the other SC. That should also mean
		   we are coord. If oneSAfe2PBEAllowed is true, then we now must
		   turn it off. This is done by sending an admin-op to PBE
		   turning off the oneSafe2PBEAllwed flag. This comes back to
		   the cluster as an RTA update, turning off the flag also in
		   imm-ram. The flag must be turned off to allow the rejoining
		   slave PBE to take a fresh dump of from imm-ram to the sqlite
		   file. No Ccbs can be allowed to start untill the slave PBE
		   has rejoined establishing 2-safe 2PBE. See

		*/
		if (cb->mCanBeCoord && evt->info.ctrl.canBeCoord) {
			LOG_IN("Other %s IMMND node (%x) has been introduced",
			       (cb->mScAbsenceAllowed) ? "candidate coord"
						       : "SC",
			       evt->info.ctrl.nodeId);
			cb->mIsOtherScUp =
			    true; /* Prevents oneSafe2PBEAllowed from being
				     turned on */
			cb->other_sc_node_id = evt->info.ctrl.nodeId;

			if (cb->mIsCoord && immModel_oneSafe2PBEAllowed(cb)) {
				/* oneSafe2PBE currently ON => turn it OFF. */
				IMMSV_EVT fake_evt;
				memset(&fake_evt, '\0', sizeof(IMMSV_EVT));
				IMMSV_ADMIN_OPERATION_PARAM param;
				char *opensafImmObj = OPENSAF_IMM_OBJECT_DN;
				char *nostParam = OPENSAF_IMM_ATTR_NOSTD_FLAGS;
				memset(&param, '\0',
				       sizeof(IMMSV_ADMIN_OPERATION_PARAM));
				LOG_NO(
				    "Internally generating 'admin-op' for toggling OFF: "
				    "1-safe2PBE(0x%x) in %s",
				    OPENSAF_IMM_FLAG_2PBE1_ALLOW,
				    OPENSAF_IMM_ATTR_NOSTD_FLAGS);

				memset(&fake_evt, '\0', sizeof(IMMSV_EVT));
				fake_evt.type = IMMSV_EVT_TYPE_IMMND;
				fake_evt.info.immnd.type =
				    IMMND_EVT_A2ND_IMM_ADMOP;

				fake_evt.info.immnd.info.admOpReq.adminOwnerId =
				    immModel_getAdmoIdForObj(cb, opensafImmObj);

				fake_evt.info.immnd.info.admOpReq.operationId =
				    OPENSAF_IMM_NOST_FLAG_OFF;
				fake_evt.info.immnd.info.admOpReq
				    .continuationId = 0;
				fake_evt.info.immnd.info.admOpReq.objectName
				    .buf = opensafImmObj;
				fake_evt.info.immnd.info.admOpReq.objectName
				    .size = strlen(opensafImmObj) + 1;
				fake_evt.info.immnd.info.admOpReq.params =
				    &param;
				fake_evt.info.immnd.info.admOpReq.params
				    ->paramName.buf = nostParam;
				fake_evt.info.immnd.info.admOpReq.params
				    ->paramName.size = strlen(nostParam) + 1;
				fake_evt.info.immnd.info.admOpReq.params
				    ->paramType = SA_IMM_ATTR_SAUINT32T;
				fake_evt.info.immnd.info.admOpReq.params
				    ->paramBuffer.val.sauint32 =
				    OPENSAF_IMM_FLAG_2PBE1_ALLOW;

				/* A *fake* fake-evs. */
				LOG_NO(
				    "****Normalizing from 1-safe 2PBE to 2-safe 2PBE****");
				immnd_evt_proc_admop(cb, &(fake_evt.info.immnd),
						     false, 0LL, 0LL);
			}
		}
	}
	return NCSCC_RC_SUCCESS;
}

void dequeue_outgoing(IMMND_CB *cb)
{
	TRACE_ENTER();
	IMMND_EVT dummy_evt;

	unsigned int space =
	    (cb->fevs_replies_pending < IMMSV_DEFAULT_FEVS_MAX_PENDING)
		? (IMMSV_DEFAULT_FEVS_MAX_PENDING - cb->fevs_replies_pending)
		: 0;

	TRACE("Pending replies:%u space:%u out list?:%p",
	      cb->fevs_replies_pending, space, cb->fevs_out_list);

	while (cb->fevs_out_list && space &&
	       (cb->fevs_replies_pending < IMMSV_DEFAULT_FEVS_MAX_PENDING) &&
	       immnd_is_immd_up(cb)) {
		memset(&dummy_evt, '\0', sizeof(IMMND_EVT));
		unsigned int backlog = immnd_dequeue_outgoing_fevs_msg(
		    cb, &dummy_evt.info.fevsReq.msg,
		    &dummy_evt.info.fevsReq.client_hdl);

		if (immnd_evt_proc_fevs_forward(cb, &dummy_evt, NULL, false,
						false) != NCSCC_RC_SUCCESS) {
			LOG_WA(
			    "Failed to process delayed asyncronous fevs message - discarded");
		}
		--space;

		if (backlog) {
			if (backlog % 50) {
				TRACE_2(
				    "Backlogged outgoing asyncronous fevs messages: %u, "
				    "incoming messages pending: %u",
				    backlog, cb->fevs_replies_pending);
			} else {
				LOG_IN(
				    "Backlogged outgoing asyncronous fevs messages: %u, "
				    "incoming messages pending: %u",
				    backlog, cb->fevs_replies_pending);
			}
		}

		if (dummy_evt.info.fevsReq.msg.buf) {
			free(dummy_evt.info.fevsReq.msg.buf);
			dummy_evt.info.fevsReq.msg.size = 0;
		}
	}
	TRACE_LEAVE();
}

/****************************************************************************
 * Name          : immnd_evt_proc_fevs_rcv
 *
 * Description   : Function to process fevs broadcast received  at immnd
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *                 IMMSV_SEND_INFO *sinfo - Sender MDS information.
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 *****************************************************************************/
static uint32_t immnd_evt_proc_fevs_rcv(IMMND_CB *cb, IMMND_EVT *evt,
					IMMSV_SEND_INFO *sinfo)
{ /*sinfo not used */
	osafassert(evt);
	SaUint64T msgNo = evt->info.fevsReq.sender_count; /*Global MsgNo */
	SaImmHandleT clnt_hdl = evt->info.fevsReq.client_hdl;
	IMMSV_OCTET_STRING *msg = &evt->info.fevsReq.msg;
	MDS_DEST reply_dest = evt->info.fevsReq.reply_dest;
	bool isObjSync = (evt->type == IMMND_EVT_D2ND_GLOB_FEVS_REQ_2)
			     ? evt->info.fevsReq.isObjSync
			     : false;
	TRACE_ENTER();

	if (cb->highestProcessed >= msgNo) {
		/*We have already received this message, discard it. */
		LOG_WA("DISCARD DUPLICATE FEVS message:%llu", msgNo);
		dequeue_outgoing(cb);
		return NCSCC_RC_FAILURE; /*TODO: ensure evt is discarded by
					    invoker */
	}

	bool originatedAtThisNd =
	    (m_IMMSV_UNPACK_HANDLE_LOW(clnt_hdl) == cb->node_id);

	if (originatedAtThisNd) {
		osafassert(!reply_dest || (reply_dest == cb->immnd_mdest_id) ||
			   isObjSync);
		if (cb->fevs_replies_pending) {
			--(cb->fevs_replies_pending); /*flow control towards
							 IMMD */
		}
		TRACE_2("FEVS from myself, still pending:%u",
			cb->fevs_replies_pending);
	} else {
		TRACE_2(
		    "REMOTE FEVS received. Messages from me still pending:%u",
		    cb->fevs_replies_pending);
	}

	if (cb->highestReceived < msgNo) {
		cb->highestReceived = msgNo;
	}

	if ((cb->highestProcessed + 1) < msgNo) {
		/*We received an out-of-order (higher msgNo than expected)
		 * message */
		if (cb->mAccepted) {
			LOG_ER(
			    "MESSAGE:%llu OUT OF ORDER my highest processed:%llu - exiting",
			    msgNo, cb->highestProcessed);
			exit(1);
		} else if (cb
			       ->mSync) { /* If we receive out of sync message
					     at the time of syncing */
			LOG_ER(
			    "Sync MESSAGE:%llu OUT OF ORDER my highest processed:%llu - exiting",
			    msgNo, cb->highestProcessed);
			immnd_ackToNid(NCSCC_RC_FAILURE);
			exit(1);
		} else if (cb->mState < IMM_SERVER_LOADING_PENDING) {
			LOG_NO("Fevs count adjusted to %llu preLoadPid: %u",
			       msgNo - 1, cb->preLoadPid);
			cb->highestProcessed = msgNo - 1;
		}
	}

	if ((evt->type == IMMND_EVT_D2ND_GLOB_FEVS_REQ_2) && (msg->size == 0) &&
	    (msg->buf == NULL)) {
		// This is  sync message Re-broadcasted by IMMD standby because
		// of failover
		TRACE("Re-broadcasted FEVS at the time of sync");
		goto done;
	}

	/*NORMAL CASE: Received the expected in-order message. */

	SaAisErrorT err = SA_AIS_OK;
	if (isObjSync && cb->mIsCoord && (cb->syncPid > 0)) {
		TRACE("Coord discards object sync message");
	} else {
		err = immnd_evt_proc_fevs_dispatch(cb, msg, originatedAtThisNd,
						   clnt_hdl, reply_dest, msgNo);
	}

	if (err != SA_AIS_OK) {
		if (err == SA_AIS_ERR_ACCESS) {
			TRACE_2("DISCARDING msg no:%llu", msgNo);
		} else {
			LOG_ER("PROBLEM %u WITH msg no:%llu", err, msgNo);
		}
	}

done:
	cb->highestProcessed++;
	dequeue_outgoing(cb);
	TRACE_LEAVE();
	return NCSCC_RC_SUCCESS;
}

/****************************************************************************
 * Name          : immnd_evt_proc_discard_impl
 *
 * Description   : Internal call over fevs to cleanup after terminated
 *                 implementer.
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *                 bool originatedAtThisNode - Did it come from this node?
 *                 SaImmHandleT clnt_hdl - The client handle (only relevant if
 *                                         originatedAtThisNode is true).
 *                 IMM_DEST reply_dest - The dest of the ND to where reply
 *                                         is to be sent (only relevant if
 *                                       originatedAtThisNode is false).
 *****************************************************************************/
static void immnd_evt_proc_discard_impl(IMMND_CB *cb, IMMND_EVT *evt,
					bool originatedAtThisNd,
					SaImmHandleT clnt_hdl,
					MDS_DEST reply_dest)
{
	SaUint32T *globIdArr = NULL;
	SaUint32T globArrSize = 0;
	TRACE_ENTER();
	osafassert(evt);
	TRACE_2("Global discard implementer for id:%u",
		evt->info.implSet.impl_id);
	immModel_discardImplementer(cb, evt->info.implSet.impl_id, true,
				    &globArrSize, &globIdArr);
	if (globArrSize) {
		SaUint32T ix;
		for (ix = 0; ix < globArrSize; ++ix) {
			LOG_WA(
			    "Discard implementer for id  %u, abort Non-critical ccbId %u which has "
			    "discarded implementer",
			    evt->info.implSet.impl_id, globIdArr[ix]);
			immnd_proc_global_abort_ccb(cb, globIdArr[ix]);
		}
		free(globIdArr);
		globIdArr = NULL;
		globArrSize = 0;
	}

	TRACE_LEAVE();
}

/****************************************************************************
 * Name          : immnd_evt_proc_discard_node
 *
 * Description   : Internal call over fevs to cleanup after terminated
 *                 IMMND, due to IMMND crash or node crash.
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *                 bool originatedAtThisNode - Did it come from this node?
 *                 SaImmHandleT clnt_hdl - The client handle (only relevant if
 *                                         originatedAtThisNode is true).
 *                 IMM_DEST reply_dest - The dest of the ND to where reply
 *                                         is to be sent (only relevant if
 *                                       originatedAtThisNode is false).
 *****************************************************************************/
static void immnd_evt_proc_discard_node(IMMND_CB *cb, IMMND_EVT *evt,
					bool originatedAtThisNd,
					SaImmHandleT clnt_hdl,
					MDS_DEST reply_dest)
{
	SaUint32T *idArr = NULL;
	SaUint32T arrSize = 0;
	SaUint32T *globIdArr = NULL;
	SaUint32T globArrSize = 0;

	TRACE_ENTER();
	osafassert(evt);
	if (evt->info.ctrl.nodeId == cb->node_id) {
		LOG_ER("immnd_evt_proc_discard_node for *this* node %u => "
		       "Cluster partitioned (\"split brain\") - exiting",
		       cb->node_id);
		exit(1);
	}
	LOG_NO("Global discard node received for nodeId:%x pid:%u",
	       evt->info.ctrl.nodeId, evt->info.ctrl.ndExecPid);
	/* We should remember the nodeId/pid pair to avoid a redundant message
	   causing a newly reattached node being discarded.
	 */
	cb->mLostNodes++;
	immModel_discardNode(cb, evt->info.ctrl.nodeId, &arrSize, &idArr,
			     &globArrSize, &globIdArr);
	if (globArrSize) {
		SaUint32T ix;
		for (ix = 0; ix < globArrSize; ++ix) {
			LOG_WA(
			    "Detected crash at node %x, abort Non-critical ccbId %u which has implementer "
			    "on the crashed node ",
			    evt->info.ctrl.nodeId, globIdArr[ix]);
			immnd_proc_global_abort_ccb(cb, globIdArr[ix]);
		}
		free(globIdArr);
		globIdArr = NULL;
		globArrSize = 0;
	}

	if (arrSize) {
		SaAisErrorT err = SA_AIS_OK;
		SaUint32T ix;
		for (ix = 0; ix < arrSize; ++ix) {
			LOG_WA("Detected crash at node %x, abort ccbId  %u",
			       evt->info.ctrl.nodeId, idArr[ix]);
			immnd_evt_ccb_abort(cb, idArr[ix], NULL, NULL, NULL);
			err = immModel_ccbFinalize(cb, idArr[ix]);
			if (err != SA_AIS_OK) {
				LOG_WA("Failed to remove Ccb %u - ignoring",
				       idArr[ix]);
			}
		}
		free(idArr);
		idArr = NULL;
		arrSize = 0;
	}

	if (cb->other_sc_node_id == evt->info.ctrl.nodeId) {
		/* Open up for *allowing* 1-safe 2pbe */
		cb->mIsOtherScUp = false;
	}

	TRACE_LEAVE();
}

/****************************************************************************
 * Name          : immnd_evt_proc_adminit_rsp
 *
 * Description   : Function to process adminit response from Director
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *                 bool originatedAtThisNode - Did it come from this node?
 *                 SaImmHandleT clnt_hdl - The client handle (only relevant if
 *                                         originatedAtThisNode is true).
 *                 IMM_DEST reply_dest - The dest of the ND to where reply
 *                                         is to be sent (only relevant if
 *                                       originatedAtThisNode is false).
 *****************************************************************************/
static void immnd_evt_proc_adminit_rsp(IMMND_CB *cb, IMMND_EVT *evt,
				       bool originatedAtThisNd,
				       SaImmHandleT clnt_hdl,
				       MDS_DEST reply_dest)
{
	uint32_t rc = NCSCC_RC_SUCCESS;
	IMMSV_EVT send_evt;
	IMMND_IMM_CLIENT_NODE *cl_node = NULL;
	SaAisErrorT err;
	NCS_NODE_ID nodeId;
	SaUint32T conn;
	SaUint32T ownerId = 0;

	/* Remember latest admo_id for IMMD recovery. */
	cb->mLatestAdmoId = evt->info.adminitGlobal.globalOwnerId;

	conn = m_IMMSV_UNPACK_HANDLE_HIGH(clnt_hdl);
	nodeId = m_IMMSV_UNPACK_HANDLE_LOW(clnt_hdl);
	ownerId = evt->info.adminitGlobal.globalOwnerId;
	err =
	    immModel_adminOwnerCreate(cb, &(evt->info.adminitGlobal.i), ownerId,
				      (originatedAtThisNd) ? conn : 0, nodeId);

	if (originatedAtThisNd) { /*Send reply to client from this ND. */
		immnd_client_node_get(cb, clnt_hdl, &cl_node);
		if (cl_node == NULL) {
			/* Client was down */
			TRACE_5(
			    "Failed to get client node, discarding adm-owner id:%u originating at connection: %u",
			    ownerId, conn);
			memset(&send_evt, '\0', sizeof(IMMSV_EVT));
			send_evt.type = IMMSV_EVT_TYPE_IMMD;
			send_evt.info.immd.type =
			    IMMD_EVT_ND2D_ADMO_HARD_FINALIZE;
			send_evt.info.immd.info.admoId = ownerId;
			if (immnd_mds_msg_send(cb, NCSMDS_SVC_ID_IMMD,
					       cb->immd_mdest_id,
					       &send_evt) != NCSCC_RC_SUCCESS) {
				LOG_ER(
				    "Failed to discard adm-owner id:%u, this adm-owner will be orphanded",
				    ownerId);
			}
			return;
		} else if (cl_node->mIsStale) {
			LOG_WA("IMMND - Client went down so no response");
			return;
		}

		memset(&send_evt, '\0', sizeof(IMMSV_EVT));

		if (err != SA_AIS_OK) {
			send_evt.info.imma.info.admInitRsp.error = err;
		} else {
			/*Pick up continuation, if gone (timeout or client
			   termination) => drop. */
			send_evt.info.imma.info.admInitRsp.error = SA_AIS_OK;
			send_evt.info.imma.info.admInitRsp.ownerId =
			    evt->info.adminitGlobal.globalOwnerId;

			TRACE_2("admin owner id:%u",
				evt->info.adminitGlobal.globalOwnerId);
		}

		send_evt.type = IMMSV_EVT_TYPE_IMMA;
		send_evt.info.imma.type = IMMA_EVT_ND2A_IMM_ADMINIT_RSP;

		rc = immnd_mds_send_rsp(cb, &(cl_node->tmpSinfo), &send_evt);
		if (rc != NCSCC_RC_SUCCESS) {
			LOG_WA(
			    "Failed to send response to agent/client over MDS");
		}
	}
}

/****************************************************************************
 * Name          : immnd_evt_proc_finalize_sync
 *
 * Description   : Function to process finalize IMMND sync from IMMND coord.
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *                 bool originatedAtThisNode - Did it come from this node?
 *                 SaImmHandleT clnt_hdl - The client handle (only relevant if
 *                                         originatedAtThisNode is true).
 *                 IMM_DEST reply_dest - The dest of the ND to where reply
					 is to be sent (only relevant if
					 originatedAtThisNode is false).
*****************************************************************************/
static void immnd_evt_proc_finalize_sync(IMMND_CB *cb, IMMND_EVT *evt,
					 bool originatedAtThisNd,
					 SaImmHandleT clnt_hdl,
					 MDS_DEST reply_dest)
{
	TRACE_ENTER();
	TRACE_2("**********immnd_evt_proc_finalize_sync***********");
	if (cb->mSync) {
		IMMND_IMM_CLIENT_NODE *cl_node = NULL;
		SaImmHandleT prev_hdl;
		unsigned int count = 0;
		IMMSV_EVT send_evt;
		TRACE_2("FinalizeSync for sync client");
		SaAisErrorT err = immModel_finalizeSync(
		    cb, &(evt->info.finSync), false, true);
		if (err != SA_AIS_OK) {
			immnd_ackToNid(NCSCC_RC_FAILURE);
			if (err == SA_AIS_ERR_BAD_OPERATION) {
				LOG_NO("exiting");
				exit(1); /* Dont core dump as this was not a
					    local error */
			} else {
				LOG_ER(
				    "Unexpected local error %u in finalizeSync for sync client - aborting",
				    err);
				abort();
			}
		}
		cb->mAccepted =
		    true; /*Accept ALL fevs messages after this one! */
		cb->syncFevsBase = 0LL;
		cb->mMyEpoch++;
		/*This must bring the epoch of the joiner up to the ruling epoch
		 */
		osafassert(cb->mMyEpoch == cb->mRulingEpoch);
		/*This adjust-epoch will persistify the new epoch for
		 * sync-clients. */
		if (cb->mPbeFile) { /* Pbe enabled */
			cb->mRim = immModel_getRepositoryInitMode(cb);

			TRACE("RepositoryInitMode: %s",
			      (cb->mRim == SA_IMM_KEEP_REPOSITORY)
				  ? "SA_IMM_KEEP_REPOSITORY"
				  : "SA_IMM_INIT_FROM_FILE");
		}
		immnd_adjustEpoch(cb, true);

		/* Sync completed for client => trigger active resurrect. */
		memset(&send_evt, '\0', sizeof(IMMSV_EVT));
		send_evt.type = IMMSV_EVT_TYPE_IMMA;
		send_evt.info.imma.type = IMMA_EVT_ND2A_PROC_STALE_CLIENTS;
		immnd_client_node_getnext(cb, 0, &cl_node);
		while (cl_node) {
			prev_hdl = cl_node->imm_app_hdl;
			if (!(cl_node->mIsResurrect)) {
				LOG_WA(
				    "Found active client id: %llx version:%c %u %u, after sync, should not happen",
				    cl_node->imm_app_hdl,
				    cl_node->version.releaseCode,
				    cl_node->version.majorVersion,
				    cl_node->version.minorVersion);
				immnd_client_node_getnext(cb, prev_hdl,
							  &cl_node);
				continue;
			}
			/* Send resurrect message. */
			if (immnd_mds_msg_send(cb, cl_node->sv_id,
					       cl_node->agent_mds_dest,
					       &send_evt) != NCSCC_RC_SUCCESS) {
				LOG_WA(
				    "Failed to send active resurrect message");
			}
			++count;
			/* Remove the temporary client node. */
			immnd_client_node_del(cb, cl_node);
			memset(cl_node, '\0', sizeof(IMMND_IMM_CLIENT_NODE));
			free(cl_node);
			cl_node = NULL;
			immnd_client_node_getnext(cb, 0, &cl_node);
		}
		TRACE_2("Triggered %u active resurrects", count);
	} else {
		if (cb->mIsCoord) {
			if (!(cb->mSyncFinalizing)) {
				LOG_WA(
				    "Received unexpected syncFinalize (sync aborted ?) - ignoring");
				goto done;
			}
			IMMND_IMM_CLIENT_NODE *cl_node = NULL;
			immnd_client_node_get(cb, clnt_hdl, &cl_node);
			if (cl_node == NULL || cl_node->mIsStale) {
				TRACE_2(
				    "IMMND - Sync client already disconnected");
			} else if (cl_node->mSyncBlocked) {
				cl_node->mSyncBlocked = false;
			}
			int retryCount = 0;
			for (; !immnd_is_immd_up(cb) && retryCount < 24;
			     ++retryCount) {
				LOG_WA(
				    "Coord blocked in adjust Epoch because IMMD is DOWN");
				sleep(1);
			}
			/*This adjust-epoch will persistify the new epoch for:
			 * coord. */
			immnd_adjustEpoch(
			    cb, true); /* Will osafassert if immd is down. */
			cb->mSyncFinalizing = 0x0;
		} else {
			TRACE_2(
			    "FinalizeSync for veteran node that is non coord");
			/* In this case we use the sync message to verify the
			   state instad of syncing. */
			osafassert(
			    immModel_finalizeSync(cb, &(evt->info.finSync),
						  false, false) == SA_AIS_OK);
			int retryCount = 0;
			for (; !immnd_is_immd_up(cb) && retryCount < 16;
			     ++retryCount) {
				LOG_WA(
				    "IMMND blocked in adjust Epoch because IMMD is DOWN");
				sleep(1);
			}
			/*This adjust-epoch will persistify the new epoch for:
			 * veterans. */
			immnd_adjustEpoch(
			    cb, true); /* Will osafassert if immd is down. */
		}

		if (cb->mScAbsenceAllowed) { /* Coord and veteran nodes. */
			IMMND_IMM_CLIENT_NODE *cl_node = NULL;
			SaImmHandleT prev_hdl;
			unsigned int count = 0;
			IMMSV_EVT send_evt;
			/* Sync completed for veteran & SC absence allowed =>
			   trigger active resurrect. */
			memset(&send_evt, '\0', sizeof(IMMSV_EVT));
			send_evt.type = IMMSV_EVT_TYPE_IMMA;
			send_evt.info.imma.type =
			    IMMA_EVT_ND2A_PROC_STALE_CLIENTS;
			immnd_client_node_getnext(cb, 0, &cl_node);
			while (cl_node) {
				prev_hdl = cl_node->imm_app_hdl;
				if (!(cl_node->mIsResurrect)) {
					LOG_IN(
					    "Veteran node found active client id: %llx "
					    "version:%c %u %u, after sync.",
					    cl_node->imm_app_hdl,
					    cl_node->version.releaseCode,
					    cl_node->version.majorVersion,
					    cl_node->version.minorVersion);
					immnd_client_node_getnext(cb, prev_hdl,
								  &cl_node);
					continue;
				}
				/* Send resurrect message. */
				if (immnd_mds_msg_send(cb, cl_node->sv_id,
						       cl_node->agent_mds_dest,
						       &send_evt) !=
				    NCSCC_RC_SUCCESS) {
					LOG_WA(
					    "Failed to send active resurrect message");
				}
				/* Remove the temporary client node. */
				immnd_client_node_del(cb, cl_node);
				memset(cl_node, '\0',
				       sizeof(IMMND_IMM_CLIENT_NODE));
				free(cl_node);
				cl_node = NULL;
				++count;
				immnd_client_node_getnext(cb, 0, &cl_node);
			}
			TRACE_2(
			    "Triggered %u active resurrects at veteran node",
			    count);
		}
	}

done:
	TRACE_LEAVE();
}

/****************************************************************************
 * Name          : immnd_evt_proc_admo_set
 *
 * Description   : Function to process admin owner set from agent
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *                 bool originatedAtThisNode - Did it come from this node?
 *                 SaImmHandleT clnt_hdl - The client handle (only relevant if
 *                                         originatedAtThisNode is true).
 *                 IMM_DEST reply_dest - The dest of the ND to where reply
 *                                         is to be sent (only relevant if
 *                                       originatedAtThisNode is false).
 *****************************************************************************/
static void immnd_evt_proc_admo_set(IMMND_CB *cb, IMMND_EVT *evt,
				    bool originatedAtThisNd,
				    SaImmHandleT clnt_hdl, MDS_DEST reply_dest)
{
	uint32_t rc = NCSCC_RC_SUCCESS;
	IMMSV_EVT send_evt;
	IMMND_IMM_CLIENT_NODE *cl_node = NULL;
	SaAisErrorT err;

	osafassert(evt);
	if (evt->info.admReq.adm_owner_id != 0) {
		err = immModel_adminOwnerChange(cb, &(evt->info.admReq), false);
	} else {
		LOG_ER("adminOwnerSet can not have 0 admin owner id");
		err = SA_AIS_ERR_LIBRARY;
	}

	if (originatedAtThisNd) { /*Send reply to client from this ND. */
		immnd_client_node_get(cb, clnt_hdl, &cl_node);
		if (cl_node == NULL || cl_node->mIsStale) {
			LOG_WA("IMMND - Client went down so no response");
			return;
		}

		memset(&send_evt, '\0', sizeof(IMMSV_EVT));

		send_evt.type = IMMSV_EVT_TYPE_IMMA;
		send_evt.info.imma.type = IMMA_EVT_ND2A_IMM_ERROR;
		send_evt.info.imma.info.errRsp.error = err;

		TRACE_2("SENDRSP %u", err);

		rc = immnd_mds_send_rsp(cb, &(cl_node->tmpSinfo), &send_evt);
		if (rc != NCSCC_RC_SUCCESS) {
			LOG_WA(
			    "Failed to send response to agent/client over MDS");
		}
	}
}

/****************************************************************************
 * Name          : immnd_evt_proc_admo_release
 *
 * Description   : Function to process admin owner release from agent
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *                 bool originatedAtThisNode - Did it come from this node?
 *                 SaImmHandleT clnt_hdl - The client handle (only relevant if
 *                                         originatedAtThisNode is true).
 *                 IMM_DEST reply_dest - The dest of the ND to where reply
 *                                         is to be sent (only relevant if
 *                                       originatedAtThisNode is false).
 *****************************************************************************/
static void immnd_evt_proc_admo_release(IMMND_CB *cb, IMMND_EVT *evt,
					bool originatedAtThisNd,
					SaImmHandleT clnt_hdl,
					MDS_DEST reply_dest)
{
	uint32_t rc = NCSCC_RC_SUCCESS;
	IMMSV_EVT send_evt;
	IMMND_IMM_CLIENT_NODE *cl_node = NULL;
	SaAisErrorT err;

	osafassert(evt);
	if (evt->info.admReq.adm_owner_id != 0) {
		err = immModel_adminOwnerChange(cb, &(evt->info.admReq), true);
	} else {
		LOG_ER("adminOwnerRelease can not have 0 admin owner id");
		err = SA_AIS_ERR_LIBRARY;
	}

	if (originatedAtThisNd) { /*Send reply to client from this ND. */
		immnd_client_node_get(cb, clnt_hdl, &cl_node);
		if (cl_node == NULL || cl_node->mIsStale) {
			LOG_WA("IMMND - Client went down so no response");
			return;
		}

		memset(&send_evt, '\0', sizeof(IMMSV_EVT));

		send_evt.type = IMMSV_EVT_TYPE_IMMA;
		send_evt.info.imma.type = IMMA_EVT_ND2A_IMM_ERROR;
		send_evt.info.imma.info.errRsp.error = err;

		TRACE_2("SENDRSP %u", err);

		rc = immnd_mds_send_rsp(cb, &(cl_node->tmpSinfo), &send_evt);
		if (rc != NCSCC_RC_SUCCESS) {
			LOG_WA(
			    "Failed to send response to agent/client over MDS");
		}
	}
}

/****************************************************************************
 * Name          : immnd_evt_proc_admo_clear
 *
 * Description   : Function to process admin owner clear from agent
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *                 bool originatedAtThisNode - Did it come from this node?
 *                 SaImmHandleT clnt_hdl - The client handle (only relevant if
 *                                         originatedAtThisNode is true).
 *                 IMM_DEST reply_dest - The dest of the ND to where reply
 *                                         is to be sent (only relevant if
 *                                       originatedAtThisNode is false).
 *****************************************************************************/
static void immnd_evt_proc_admo_clear(IMMND_CB *cb, IMMND_EVT *evt,
				      bool originatedAtThisNd,
				      SaImmHandleT clnt_hdl,
				      MDS_DEST reply_dest)
{
	uint32_t rc = NCSCC_RC_SUCCESS;
	IMMSV_EVT send_evt;
	IMMND_IMM_CLIENT_NODE *cl_node = NULL;
	SaAisErrorT err;

	osafassert(evt);
	if (evt->info.admReq.adm_owner_id == 0) {
		err = immModel_adminOwnerChange(cb, &(evt->info.admReq), true);
	} else {
		LOG_ER("adminOwnerClear can not have non ZERO admin owner id");
		err = SA_AIS_ERR_LIBRARY;
	}

	if (originatedAtThisNd) { /*Send reply to client from this ND. */
		immnd_client_node_get(cb, clnt_hdl, &cl_node);
		if (cl_node == NULL || cl_node->mIsStale) {
			LOG_WA("IMMND - Client went down so no response");
			return;
		}

		memset(&send_evt, '\0', sizeof(IMMSV_EVT));

		send_evt.type = IMMSV_EVT_TYPE_IMMA;
		send_evt.info.imma.type = IMMA_EVT_ND2A_IMM_ERROR;
		send_evt.info.imma.info.errRsp.error = err;

		TRACE_2("SENDRSP %u", err);

		rc = immnd_mds_send_rsp(cb, &(cl_node->tmpSinfo), &send_evt);
		if (rc != NCSCC_RC_SUCCESS) {
			LOG_WA(
			    "Failed to send response to agent/client over MDS");
		}
	}
}

/****************************************************************************
 * Name          : immnd_evt_proc_admo_finalize
 *
 * Description   : Function to process admin owner finalize from agent
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *                 bool originatedAtThisNode - Did it come from this node?
 *                 SaImmHandleT clnt_hdl - The client handle (only relevant if
 *                                         originatedAtThisNode is true).
 *                 IMM_DEST reply_dest - The dest of the ND to where reply
 *                                         is to be sent (only relevant if
 *                                         originatedAtThisNode is false).
 *****************************************************************************/
static void immnd_evt_proc_admo_finalize(IMMND_CB *cb, IMMND_EVT *evt,
					 bool originatedAtThisNd,
					 SaImmHandleT clnt_hdl,
					 MDS_DEST reply_dest)
{
	uint32_t rc = NCSCC_RC_SUCCESS;
	IMMSV_EVT send_evt;
	IMMND_IMM_CLIENT_NODE *cl_node = NULL;
	SaAisErrorT err;
	bool wasLoading = ((cb->mState == IMM_SERVER_LOADING_CLIENT) ||
			   (cb->mState == IMM_SERVER_LOADING_SERVER)) &&
			  immModel_getLoader(cb);

	/* TODO: ABT should really remove any open ccbs owned by this admowner.
	   Currently this cleanup is left for the closure of the imm-handle or
	   timeout cleanup or process termination to take care of.
	 */

	osafassert(evt);
	err =
	    immModel_adminOwnerDelete(cb, evt->info.admFinReq.adm_owner_id, 0);

	if (wasLoading) {
		if (err == SA_AIS_ERR_NOT_READY) {
			immnd_ackToNid(NCSCC_RC_FAILURE);
			LOG_ER("Unexpected failure in loading - aborting");
			abort();
		}

		if (immModel_getLoader(cb) == 0) {
			if (cb->mPbeFile) { /* Pbe configured */
				cb->mRim = immModel_getRepositoryInitMode(cb);
				TRACE("RepositoryInitMode: %s",
				      (cb->mRim == SA_IMM_KEEP_REPOSITORY)
					  ? "SA_IMM_KEEP_REPOSITORY"
					  : "SA_IMM_INIT_FROM_FILE");
			}

			TRACE(
			    "Adjusting epoch directly after loading has completed");
			immnd_adjustEpoch(
			    cb,
			    true); /* Moved to here from immnd_proc.c #1987 */
		}
	}

	if (originatedAtThisNd) { /*Send reply to client from this ND. */
		immnd_client_node_get(cb, clnt_hdl, &cl_node);
		if (cl_node == NULL || cl_node->mIsStale) {
			LOG_WA("IMMND - Client went down so no response");
			return;
		}

		memset(&send_evt, '\0', sizeof(IMMSV_EVT));

		send_evt.type = IMMSV_EVT_TYPE_IMMA;
		send_evt.info.imma.type = IMMA_EVT_ND2A_IMM_ERROR;
		send_evt.info.imma.info.errRsp.error = err;

		TRACE_2("SENDRSP %u", err);

		rc = immnd_mds_send_rsp(cb, &(cl_node->tmpSinfo), &send_evt);
		if (rc != NCSCC_RC_SUCCESS) {
			LOG_WA(
			    "Failed to send response to agent/client over MDS");
		}
	}
}

/****************************************************************************
 * Name          : immnd_evt_proc_admo_hard_finalize
 *
 * Description   : Function for hard admin owner finalize (lost connection)
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *                 bool originatedAtThisNode - Did it come from this node?
 *                 SaImmHandleT clnt_hdl - The client handle (only relevant if
 *                                         originatedAtThisNode is true).
 *                 IMM_DEST reply_dest - The dest of the ND to where reply
 *                                         is to be sent (only relevant if
 *                                         originatedAtThisNode is false).
 *****************************************************************************/
void immnd_evt_proc_admo_hard_finalize(IMMND_CB *cb, IMMND_EVT *evt,
				       bool originatedAtThisNd,
				       SaImmHandleT clnt_hdl,
				       MDS_DEST reply_dest)
{
	SaAisErrorT err = SA_AIS_OK;
	TRACE_ENTER();

	/* TODO: ABT should really remove any open ccbs owned by this admowner.
	   Currently this cleanup is left for the closure of the imm-handle or
	   timeout cleanup or process termination to take care of.
	 */

	osafassert(evt);
	TRACE("immnd_evt_proc_admo_hard_finalize of adm_owner_id: %u",
	      evt->info.admFinReq.adm_owner_id);
	if (cb->mSync && !cb->mAccepted) { /* Sync client */
		immModel_addDeadAdminOwnerDuringSync(
		    evt->info.admFinReq.adm_owner_id);
	} else {
		err = immModel_adminOwnerDelete(
		    cb, evt->info.admFinReq.adm_owner_id, 1);
	}
	if (err != SA_AIS_OK) {
		if (cb->loaderPid != (-1)) {
			LOG_WA("Failed in hard remove of admin owner %u",
			       evt->info.admFinReq.adm_owner_id);
		} else {
			TRACE(
			    "Failed in hard remove of admin owner %u. Preload?",
			    evt->info.admFinReq.adm_owner_id);
		}
	}
	TRACE_LEAVE();
}

/****************************************************************************
 * Name          : immnd_evt_proc_impl_set_rsp
 *
 * Description   : Function to process implementer set response from Director
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *                 bool originatedAtThisNode - Did it come from this node?
 *                 SaImmHandleT clnt_hdl - The client handle (only relevant if
 *                                         originatedAtThisNode is true).
 *                 IMM_DEST reply_dest - The dest of the ND to where reply
 *                                         is to be sent (only relevant if
 *                                       originatedAtThisNode is false).
 *****************************************************************************/
static void immnd_evt_proc_impl_set_rsp(IMMND_CB *cb, IMMND_EVT *evt,
					bool originatedAtThisNd,
					SaImmHandleT clnt_hdl,
					MDS_DEST reply_dest)
{
	uint32_t rc = NCSCC_RC_SUCCESS;
	IMMSV_EVT send_evt;
	IMMND_IMM_CLIENT_NODE *cl_node = NULL;
	SaAisErrorT err;
	NCS_NODE_ID nodeId;
	SaUint32T conn;
	SaUint32T implId = 0;
	bool discardImplementer = false;

	osafassert(evt);
	osafassert(!originatedAtThisNd || reply_dest == cb->immnd_mdest_id);
	conn = m_IMMSV_UNPACK_HANDLE_HIGH(clnt_hdl);
	nodeId = m_IMMSV_UNPACK_HANDLE_LOW(clnt_hdl);
	implId = evt->info.implSet.impl_id;
	TRACE_2("originated here?:%u nodeId:%x conn: %u", originatedAtThisNd,
		nodeId, conn);

	if (evt->type == IMMND_EVT_D2ND_IMPLSET_RSP) {
		/* In immModel_implementerSet, 0 will be replaced with
		 * DEFAULT_TIMEOUT_SEC as the default value */
		evt->info.implSet.oi_timeout = 0;
	}

	/* Remember latest impl_id for IMMD recovery. */
	cb->mLatestImplId = evt->info.implSet.impl_id;

	err = immModel_implementerSet(
	    cb, &(evt->info.implSet.impl_name), (originatedAtThisNd) ? conn : 0,
	    nodeId, implId, reply_dest, evt->info.implSet.oi_timeout,
	    &discardImplementer);

	if (originatedAtThisNd) { /*Send reply to client from this ND. */
		immnd_client_node_get(cb, clnt_hdl, &cl_node);
		if ((cl_node == NULL) || discardImplementer) {
			/* Client was down */
			TRACE_5(
			    "Discarding implementer id:%u for connection: %u",
			    implId, conn);
			memset(&send_evt, '\0', sizeof(IMMSV_EVT));
			send_evt.type = IMMSV_EVT_TYPE_IMMD;
			send_evt.info.immd.type = IMMD_EVT_ND2D_DISCARD_IMPL;
			send_evt.info.immd.info.impl_set.r.impl_id = implId;
			if (immnd_mds_msg_send(cb, NCSMDS_SVC_ID_IMMD,
					       cb->immd_mdest_id,
					       &send_evt) != NCSCC_RC_SUCCESS) {
				LOG_ER(
				    "Discard implementer failed for implId:%u. Client will be orphanded",
				    implId);
			}
			/* Mark the implementer as dying to make sure no upcall
			   is sent to the client. The implementer will be really
			   discarded when global-discard message comes */
			immModel_discardImplementer(cb, implId, false, NULL,
						    NULL);

			if (cl_node == NULL) {
				return;
			}
		}
		if (cl_node->mIsStale) {
			LOG_WA("IMMND - Client went down so no response");
			return;
		}

		memset(&send_evt, '\0', sizeof(IMMSV_EVT));

		if (err != SA_AIS_OK) {
			send_evt.info.imma.info.implSetRsp.error = err;
		} else {
			/*Pick up continuation, if gone (timeout or client
			   termination) => drop. */
			send_evt.info.imma.info.implSetRsp.error = SA_AIS_OK;
			send_evt.info.imma.info.implSetRsp.implId =
			    evt->info.implSet.impl_id;

			TRACE_2("Implementer global id:%u",
				evt->info.implSet.impl_id);
		}

		send_evt.type = IMMSV_EVT_TYPE_IMMA;
		send_evt.info.imma.type = IMMA_EVT_ND2A_IMPLSET_RSP;

		rc = immnd_mds_send_rsp(cb, &(cl_node->tmpSinfo), &send_evt);
		if (rc != NCSCC_RC_SUCCESS) {
			LOG_WA(
			    "Failed to send response to agent/client over MDS");
		}
	}
}

/****************************************************************************
 * Name          : immnd_evt_proc_impl_clr
 *
 * Description   : Function to process implementer clear over FEVS from Ag.
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *                 bool originatedAtThisNode - Did it come from this node?
 *                 SaImmHandleT clnt_hdl - The client handle (only relevant if
 *                                         originatedAtThisNode is true).
 *                 IMM_DEST reply_dest - The dest of the ND to where reply
 *                                         is to be sent (only relevant if
 *                                       originatedAtThisNode is false).
 ***************************************************************************/
static void immnd_evt_proc_impl_clr(IMMND_CB *cb, IMMND_EVT *evt,
				    bool originatedAtThisNd,
				    SaImmHandleT clnt_hdl, MDS_DEST reply_dest)
{
	uint32_t rc = NCSCC_RC_SUCCESS;
	IMMSV_EVT send_evt;
	IMMND_IMM_CLIENT_NODE *cl_node = NULL;
	SaAisErrorT err;
	NCS_NODE_ID nodeId;
	SaUint32T conn, globArrSize = 0;
	SaUint32T *globIdArr = NULL;

	osafassert(evt);
	conn = m_IMMSV_UNPACK_HANDLE_HIGH(clnt_hdl);
	nodeId = m_IMMSV_UNPACK_HANDLE_LOW(clnt_hdl);

	err = immModel_implementerClear(cb, &(evt->info.implSet),
					(originatedAtThisNd) ? conn : 0, nodeId,
					&globArrSize, &globIdArr);
	if (err == SA_AIS_OK && globArrSize) {
		SaUint32T ix;
		for (ix = 0; ix < globArrSize; ++ix) {
			LOG_WA(
			    "Cleared implementer for id  %u, abort Non-critical ccbId %u which has "
			    "cleared the implementer",
			    evt->info.implSet.impl_id, globIdArr[ix]);
			immnd_proc_global_abort_ccb(cb, globIdArr[ix]);
		}
		free(globIdArr);
		globIdArr = NULL;
		globArrSize = 0;
	}

	if (originatedAtThisNd) { /*Send reply to client from this ND. */
		immnd_client_node_get(cb, clnt_hdl, &cl_node);
		if (cl_node == NULL || cl_node->mIsStale) {
			LOG_WA("IMMND - Client went down so no response");
			return;
		}

		memset(&send_evt, '\0', sizeof(IMMSV_EVT));

		send_evt.info.imma.info.errRsp.error = err;
		send_evt.type = IMMSV_EVT_TYPE_IMMA;
		send_evt.info.imma.type = IMMA_EVT_ND2A_IMM_ERROR;

		rc = immnd_mds_send_rsp(cb, &(cl_node->tmpSinfo), &send_evt);
		if (rc != NCSCC_RC_SUCCESS) {
			LOG_WA(
			    "Failed to send response to agent/client over MDS");
		}
	}
}

/****************************************************************************
 * Name          : immnd_evt_proc_cl_impl_set
 *
 * Description   : Function to process class implementer set over FEVS from Ag.
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *                 bool originatedAtThisNode - Did it come from this node?
 *                 SaImmHandleT clnt_hdl - The client handle (only relevant if
 *                                         originatedAtThisNode is true).
 *                 IMM_DEST reply_dest - The dest of the ND to where reply
 *                                         is to be sent (only relevant if
 *                                       originatedAtThisNode is false).
 *****************************************************************************/
static void immnd_evt_proc_cl_impl_set(IMMND_CB *cb, IMMND_EVT *evt,
				       bool originatedAtThisNd,
				       SaImmHandleT clnt_hdl,
				       MDS_DEST reply_dest)
{
	uint32_t rc = NCSCC_RC_SUCCESS;
	IMMSV_EVT send_evt;
	IMMND_IMM_CLIENT_NODE *cl_node = NULL;
	SaAisErrorT err;
	NCS_NODE_ID nodeId;
	SaUint32T conn;
	SaUint32T ccbId = 0;

	osafassert(evt);
	conn = m_IMMSV_UNPACK_HANDLE_HIGH(clnt_hdl);
	nodeId = m_IMMSV_UNPACK_HANDLE_LOW(clnt_hdl);

	err = immModel_classImplementerSet(cb, &(evt->info.implSet),
					   (originatedAtThisNd) ? conn : 0,
					   nodeId, &ccbId);
	if (err == SA_AIS_ERR_NO_BINDINGS) {
		/* Idempotency case. */
		TRACE(
		    "Idempotent classImplementerSet for impl_id:%u detected in fevs received request.",
		    evt->info.implSet.impl_id);
		osafassert(!ccbId);
		err = SA_AIS_OK;
	}

	if (originatedAtThisNd) { /*Send reply to client from this ND. */
		if (err == SA_AIS_ERR_TRY_AGAIN && ccbId) {
			/* Global abort of ccb only from originating node. */
			immnd_proc_global_abort_ccb(cb, ccbId);
		}

		immnd_client_node_get(cb, clnt_hdl, &cl_node);
		if (cl_node == NULL || cl_node->mIsStale) {
			LOG_WA("IMMND - Client went down so no response");
			return;
		}

		memset(&send_evt, '\0', sizeof(IMMSV_EVT));

		send_evt.info.imma.info.errRsp.error = err;
		send_evt.type = IMMSV_EVT_TYPE_IMMA;
		send_evt.info.imma.type = IMMA_EVT_ND2A_IMM_ERROR;

		rc = immnd_mds_send_rsp(cb, &(cl_node->tmpSinfo), &send_evt);
		if (rc != NCSCC_RC_SUCCESS) {
			LOG_WA(
			    "Failed to send response to agent/client over MDS");
		}
	}

	if ((err == SA_AIS_OK) &&
	    (strcmp(OPENSAF_IMM_CLASS_NAME, evt->info.implSet.impl_name.buf) ==
	     0)) {
		TRACE(
		    "PBE class implementer set, send initial epoch to implementer");
		immnd_adjustEpoch(
		    cb,
		    false); /*No increment, just inform PBE of current epoch. */
	}
}

/****************************************************************************
 * Name          : immnd_evt_proc_cl_impl_rel
 *
 * Description   : Function to process class implementer release over FEVS.
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *                 bool originatedAtThisNode - Did it come from this node?
 *                 SaImmHandleT clnt_hdl - The client handle (only relevant if
 *                                         originatedAtThisNode is true).
 *                 IMM_DEST reply_dest - The dest of the ND to where reply
 *                                         is to be sent (only relevant if
 *                                       originatedAtThisNode is false).
 *****************************************************************************/
static void immnd_evt_proc_cl_impl_rel(IMMND_CB *cb, IMMND_EVT *evt,
				       bool originatedAtThisNd,
				       SaImmHandleT clnt_hdl,
				       MDS_DEST reply_dest)
{
	uint32_t rc = NCSCC_RC_SUCCESS;
	IMMSV_EVT send_evt;
	IMMND_IMM_CLIENT_NODE *cl_node = NULL;
	SaAisErrorT err;
	NCS_NODE_ID nodeId;
	SaUint32T conn;

	osafassert(evt);
	conn = m_IMMSV_UNPACK_HANDLE_HIGH(clnt_hdl);
	nodeId = m_IMMSV_UNPACK_HANDLE_LOW(clnt_hdl);

	err = immModel_classImplementerRelease(
	    cb, &(evt->info.implSet), (originatedAtThisNd) ? conn : 0, nodeId);

	if (originatedAtThisNd) { /*Send reply to client from this ND. */
		immnd_client_node_get(cb, clnt_hdl, &cl_node);
		if (cl_node == NULL || cl_node->mIsStale) {
			LOG_WA("IMMND - Client went down so no response");
			return;
		}

		memset(&send_evt, '\0', sizeof(IMMSV_EVT));

		if (err != SA_AIS_OK) {
			send_evt.info.imma.info.errRsp.error = err;
		} else {
			send_evt.info.imma.info.errRsp.error = SA_AIS_OK;
		}

		send_evt.type = IMMSV_EVT_TYPE_IMMA;
		send_evt.info.imma.type = IMMA_EVT_ND2A_IMM_ERROR;
		TRACE_2("SENDRSP OK");

		rc = immnd_mds_send_rsp(cb, &(cl_node->tmpSinfo), &send_evt);
		if (rc != NCSCC_RC_SUCCESS) {
			LOG_WA(
			    "Failed to send response to agent/client over MDS");
		}
	}
}

/****************************************************************************
 * Name          : immnd_evt_proc_obj_impl_set
 *
 * Description   : Function to process object implementer set over FEVS.
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *                 bool originatedAtThisNode - Did it come from this node?
 *                 SaImmHandleT clnt_hdl - The client handle (only relevant if
 *                                         originatedAtThisNode is true).
 *                 IMM_DEST reply_dest - The dest of the ND to where reply
 *                                         is to be sent (only relevant if
 *                                       originatedAtThisNode is false).
 *****************************************************************************/
static void immnd_evt_proc_obj_impl_set(IMMND_CB *cb, IMMND_EVT *evt,
					bool originatedAtThisNd,
					SaImmHandleT clnt_hdl,
					MDS_DEST reply_dest)
{
	uint32_t rc = NCSCC_RC_SUCCESS;
	IMMSV_EVT send_evt;
	IMMND_IMM_CLIENT_NODE *cl_node = NULL;
	SaAisErrorT err;
	NCS_NODE_ID nodeId;
	SaUint32T conn;
	SaUint32T ccbId = 0;
	TRACE_ENTER();

	osafassert(evt);
	conn = m_IMMSV_UNPACK_HANDLE_HIGH(clnt_hdl);
	nodeId = m_IMMSV_UNPACK_HANDLE_LOW(clnt_hdl);

	err = immModel_objectImplementerSet(cb, &(evt->info.implSet),
					    (originatedAtThisNd) ? conn : 0,
					    nodeId, &ccbId);
	if (err == SA_AIS_ERR_NO_BINDINGS) {
		/* idempotency case. */
		TRACE(
		    "Idempotent objectImplementerSet for impl_id:%u detected in fevs received request.",
		    evt->info.implSet.impl_id);
		err = SA_AIS_OK;
	}

	if (originatedAtThisNd) { /*Send reply to client from this ND. */
		if (err == SA_AIS_ERR_TRY_AGAIN && ccbId) {
			/* Global abort of ccb only from originating node. */
			immnd_proc_global_abort_ccb(cb, ccbId);
		}

		immnd_client_node_get(cb, clnt_hdl, &cl_node);
		if (cl_node == NULL || cl_node->mIsStale) {
			LOG_WA("IMMND - Client went down so no response");
			return;
		}

		memset(&send_evt, '\0', sizeof(IMMSV_EVT));

		send_evt.info.imma.info.errRsp.error = err;

		send_evt.type = IMMSV_EVT_TYPE_IMMA;
		send_evt.info.imma.type = IMMA_EVT_ND2A_IMM_ERROR;

		rc = immnd_mds_send_rsp(cb, &(cl_node->tmpSinfo), &send_evt);
		if (rc != NCSCC_RC_SUCCESS) {
			LOG_WA(
			    "Failed to send response to agent/client over MDS");
		}
	}
	TRACE_LEAVE();
}

/****************************************************************************
 * Name          : immnd_evt_proc_obj_impl_rel
 *
 * Description   : Function to process object implementer release over FEVS.
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *                 bool originatedAtThisNode - Did it come from this node?
 *                 SaImmHandleT clnt_hdl - The client handle (only relevant if
 *                                         originatedAtThisNode is true).
 *                 IMM_DEST reply_dest - The dest of the ND to where reply
 *                                         is to be sent (only relevant if
 *                                       originatedAtThisNode is false).
 *****************************************************************************/
static void immnd_evt_proc_obj_impl_rel(IMMND_CB *cb, IMMND_EVT *evt,
					bool originatedAtThisNd,
					SaImmHandleT clnt_hdl,
					MDS_DEST reply_dest)
{
	uint32_t rc = NCSCC_RC_SUCCESS;
	IMMSV_EVT send_evt;
	IMMND_IMM_CLIENT_NODE *cl_node = NULL;
	SaAisErrorT err;
	NCS_NODE_ID nodeId;
	SaUint32T conn;
	TRACE_ENTER();

	osafassert(evt);
	conn = m_IMMSV_UNPACK_HANDLE_HIGH(clnt_hdl);
	nodeId = m_IMMSV_UNPACK_HANDLE_LOW(clnt_hdl);

	err = immModel_objectImplementerRelease(
	    cb, &(evt->info.implSet), (originatedAtThisNd) ? conn : 0, nodeId);

	if (originatedAtThisNd) { /*Send reply to client from this ND. */
		immnd_client_node_get(cb, clnt_hdl, &cl_node);
		if (cl_node == NULL || cl_node->mIsStale) {
			LOG_WA("IMMND - Client went down so no response");
			return;
		}

		memset(&send_evt, '\0', sizeof(IMMSV_EVT));

		send_evt.info.imma.info.errRsp.error = err;

		send_evt.type = IMMSV_EVT_TYPE_IMMA;
		send_evt.info.imma.type = IMMA_EVT_ND2A_IMM_ERROR;

		rc = immnd_mds_send_rsp(cb, &(cl_node->tmpSinfo), &send_evt);
		if (rc != NCSCC_RC_SUCCESS) {
			LOG_WA(
			    "Failed to send response to agent/client over MDS");
		}
	}
	TRACE_LEAVE();
}

/****************************************************************************
 * Name          : immnd_evt_proc_ccbinit_rsp
 *
 * Description   : Function to process ccbinit response from Director
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *                 bool originatedAtThisNode - Did it come from this node?
 *                 SaImmHandleT clnt_hdl - The client handle (only relevant if
 *                                         originatedAtThisNode is true).
 *                 IMM_DEST reply_dest - The dest of the ND to where reply
 *                                         is to be sent (only relevant if
 *                                       originatedAtThisNode is false).
 * Return Values : None
 *
 *****************************************************************************/
static void immnd_evt_proc_ccbinit_rsp(IMMND_CB *cb, IMMND_EVT *evt,
				       bool originatedAtThisNd,
				       SaImmHandleT clnt_hdl,
				       MDS_DEST reply_dest)
{
	uint32_t rc = NCSCC_RC_SUCCESS;
	IMMSV_EVT send_evt;
	IMMND_IMM_CLIENT_NODE *cl_node = NULL;
	SaAisErrorT err;
	SaUint32T nodeId;
	SaUint32T conn;
	SaUint32T ccbId = 0;

	osafassert(evt);
	conn = m_IMMSV_UNPACK_HANDLE_HIGH(clnt_hdl);
	nodeId = m_IMMSV_UNPACK_HANDLE_LOW(clnt_hdl);
	ccbId = evt->info.ccbinitGlobal.globalCcbId;

	/* Remember latest ccb_id for IMMD recovery. */
	cb->mLatestCcbId = evt->info.ccbinitGlobal.globalCcbId;

	err = immModel_ccbCreate(cb, evt->info.ccbinitGlobal.i.adminOwnerId,
				 evt->info.ccbinitGlobal.i.ccbFlags, ccbId,
				 nodeId, (originatedAtThisNd) ? conn : 0);

	if (originatedAtThisNd) { /*Send reply to client from this ND. */
		immnd_client_node_get(cb, clnt_hdl, &cl_node);
		if (cl_node == NULL) {
			/* Client was down */
			TRACE_5(
			    "Failed to get client node, discarding ccb id:%u originating at connection: %u",
			    ccbId, conn);
			memset(&send_evt, '\0', sizeof(IMMSV_EVT));
			send_evt.type = IMMSV_EVT_TYPE_IMMD;
			send_evt.info.immd.type = IMMD_EVT_ND2D_ABORT_CCB;
			send_evt.info.immd.info.ccbId = ccbId;
			if (immnd_mds_msg_send(cb, NCSMDS_SVC_ID_IMMD,
					       cb->immd_mdest_id,
					       &send_evt) != NCSCC_RC_SUCCESS) {
				LOG_ER(
				    "Failed to discard ccb id:%u, this ccb will be orphanded",
				    ccbId);
			}
			return;
		} else if (cl_node->mIsStale) {
			LOG_WA("IMMND - Client went down so no response");
			return;
		}

		memset(&send_evt, '\0', sizeof(IMMSV_EVT));
		send_evt.info.imma.info.ccbInitRsp.error = err;

		if (err == SA_AIS_OK) {
			/*Pick up continuation, if gone (timeout or client
			   termination) => drop. */
			send_evt.info.imma.info.ccbInitRsp.ccbId =
			    evt->info.ccbinitGlobal.globalCcbId;

			TRACE_2("Ccb global id:%u",
				send_evt.info.imma.info.ccbInitRsp.ccbId);
		}

		send_evt.type = IMMSV_EVT_TYPE_IMMA;
		send_evt.info.imma.type = IMMA_EVT_ND2A_CCBINIT_RSP;

		rc = immnd_mds_send_rsp(cb, &(cl_node->tmpSinfo), &send_evt);
		if (rc != NCSCC_RC_SUCCESS) {
			LOG_WA(
			    "Failed to send response to agent/client over MDS");
		}
	}
}

void immnd_evt_ccb_augment_init(IMMND_CB *cb, IMMND_EVT *evt,
				bool originatedAtThisNd, SaImmHandleT clnt_hdl,
				MDS_DEST reply_dest)
{
	uint32_t rc = NCSCC_RC_SUCCESS;
	IMMSV_EVT send_evt;
	IMMND_IMM_CLIENT_NODE *cl_node = NULL;
	NCS_NODE_ID nodeId = m_IMMSV_UNPACK_HANDLE_LOW(clnt_hdl);
	SaUint32T conn = m_IMMSV_UNPACK_HANDLE_HIGH(clnt_hdl);
	osafassert(evt);
	SaUint32T adminOwnerId = 0;
	TRACE_ENTER();

	SaAisErrorT err = immModel_ccbAugmentInit(
	    cb, &(evt->info.ccbUpcallRsp), nodeId,
	    (originatedAtThisNd) ? conn : 0, &adminOwnerId);

	if (originatedAtThisNd) { /*Send reply to client from this ND. */
		immnd_client_node_get(cb, clnt_hdl, &cl_node);
		if (cl_node == NULL || cl_node->mIsStale) {
			LOG_WA("IMMND - Client went down so no response");
			return;
		}

		memset(&send_evt, '\0', sizeof(send_evt));
		send_evt.type = IMMSV_EVT_TYPE_IMMA;
		send_evt.info.imma.type = IMMA_EVT_ND2A_CCB_AUG_INIT_RSP;
		send_evt.info.imma.info.admInitRsp.error = err;
		if (err == SA_AIS_OK || err == SA_AIS_ERR_NO_SECTIONS) {
			osafassert(adminOwnerId);
			send_evt.info.imma.info.admInitRsp.ownerId =
			    adminOwnerId;
		}

		rc = immnd_mds_send_rsp(cb, &(cl_node->tmpSinfo), &send_evt);
		if (rc != NCSCC_RC_SUCCESS) {
			LOG_WA(
			    "Failed to send response to agent/client over MDS");
		}
	}
	TRACE_LEAVE();
}

void immnd_evt_ccb_augment_admo(IMMND_CB *cb, IMMND_EVT *evt,
				bool originatedAtThisNd, SaImmHandleT clnt_hdl,
				MDS_DEST reply_dest)
{
	osafassert(evt);
	TRACE_ENTER2("AdminOwner %u ccbId:%u", evt->info.objDelete.adminOwnerId,
		     evt->info.objDelete.ccbId);
	immModel_ccbAugmentAdmo(cb, evt->info.objDelete.adminOwnerId,
				evt->info.objDelete.ccbId);

	TRACE_LEAVE();
}

/****************************************************************************
 * Name          : immnd_evt_proc_mds_evt
 *
 * Description   : Function to process the Events received from MDS
 *
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 *                 IMMSV_EVT *evt - Received Event structure
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 * Notes         : None.
 *****************************************************************************/
static uint32_t immnd_evt_proc_mds_evt(IMMND_CB *cb, IMMND_EVT *evt)
{
	/*TRACE_ENTER(); */
	uint32_t rc = NCSCC_RC_SUCCESS;

	if ((evt->info.mds_info.change == NCSMDS_DOWN) &&
	    (evt->info.mds_info.svc_id == NCSMDS_SVC_ID_IMMA_OM ||
	     evt->info.mds_info.svc_id == NCSMDS_SVC_ID_IMMA_OI)) {
		TRACE_2("IMMA DOWN EVENT");
		immnd_proc_imma_down(cb, evt->info.mds_info.dest,
				     evt->info.mds_info.svc_id);
	} else if ((evt->info.mds_info.change == NCSMDS_DOWN) &&
		   evt->info.mds_info.svc_id == NCSMDS_SVC_ID_IMMD) {
		/* Cluster is going down. */
		if (cb->mScAbsenceAllowed == 0) {
			/* Regular (non Hydra) exit on IMMD DOWN. */
			LOG_ER("No IMMD service => cluster restart, exiting");
			if (cb->mState < IMM_SERVER_SYNC_SERVER) {
				immnd_ackToNid(NCSCC_RC_FAILURE);
			}
			exit(1);
		} else { /* SC ABSENCE ALLOWED */
			cb->mIntroduced = 2;
			LOG_WA("SC Absence IS allowed:%u IMMD service is DOWN",
			       cb->mScAbsenceAllowed);
			if (cb->mIsCoord) {
				cb->mIsCoord = false;

				if (cb->mSyncRequested) {
					/* Just got sync requested from IMMD,
					 * nothing happened yet */
					cb->mSyncRequested = false;

				} else if (cb->mState ==
					       IMM_SERVER_SYNC_SERVER &&
					   cb->mPendSync) {
					/* Sent out ND2D_SYNC_START msg but sync
					 * didn't start yet, revert the state to
					 * IMM_SERVER_READY */
					cb->mPendSync = false;
					cb->mState = IMM_SERVER_READY;
					LOG_NO(
					    "SERVER STATE: IMM_SERVER_SYNC_SERVER --> IMM_SERVER_READY");

				} else if (cb->mState ==
					       IMM_SERVER_SYNC_SERVER &&
					   (cb->syncPid <= 0)) {
					/* Received D2ND_SYNC_START msg but sync
					 * process wasn't forked yet, revert the
					 * state to IMM_SERVER_READY */
					cb->mState = IMM_SERVER_READY;
					LOG_NO(
					    "SERVER STATE: IMM_SERVER_SYNC_SERVER --> IMM_SERVER_READY");
					immnd_abortSync(cb);

				} else if (cb->mState ==
					       IMM_SERVER_SYNC_SERVER &&
					   (cb->syncPid > 0)) {
					/* Sync started, force kill sync process
					 */
					osafassert(!cb->mPendSync);

					LOG_NO(
					    "Force kill sync process and abort sync");
					kill(cb->syncPid, SIGKILL);

					cb->mState = IMM_SERVER_READY;
					LOG_NO(
					    "SERVER STATE: IMM_SERVER_SYNC_SERVER --> IMM_SERVER_READY");
					immnd_abortSync(cb);
				}

			} else if (cb->mState <= IMM_SERVER_LOADING_PENDING) {
				/* Reset state in payloads that had not joined.
				 * No need to restart. */
				LOG_IN(
				    "Resetting IMMND state from %u to IMM_SERVER_ANONYMOUS",
				    cb->mState);
				cb->mState = IMM_SERVER_ANONYMOUS;

			} else if (cb->mState == IMM_SERVER_READY &&
				   immModel_immNotWritable(cb)) {
				/* This SC absence allowed case, when IMMD is
				 down and The sync is in progress. Veteran nodes
				 Other than the syncing node, has to change the
				 node state from NODE_R_AVAILABLE to
				 NODE_FULLY_AVAILABLE*/
				immnd_abortSync(cb);

			} else if (cb->mState < IMM_SERVER_READY) {
				LOG_WA(
				    "IMMND was being synced or loaded (%u), has to restart",
				    cb->mState);
				if (cb->mState < IMM_SERVER_SYNC_SERVER) {
					immnd_ackToNid(NCSCC_RC_FAILURE);
				}
				exit(1);
			}
		}
		LOG_NO(
		    "IMMD SERVICE IS DOWN, HYDRA IS CONFIGURED => UNREGISTERING IMMND form MDS");

		immnd_mds_unregister(cb);
		/* Discard local clients ...  */
		immnd_proc_discard_other_nodes(
		    cb); /* Isolate from the rest of cluster */
		LOG_NO("MDS unregisterede. sleeping ...");
		sleep(1);
		LOG_NO("Sleep done registering IMMND with MDS");
		rc = immnd_mds_register(immnd_cb);
		if (rc == NCSCC_RC_SUCCESS) {
			LOG_NO("SUCCESS IN REGISTERING IMMND WITH MDS");
		} else {
			LOG_ER(
			    "FAILURE IN REGISTERING IMMND WITH MDS - exiting");
			exit(1);
		}

		if (cb->pbePid > 0) {
			/* Check if pbe process is terminated.
			 * Will send SIGKILL if it's not terminated. */
			int status = 0;
			if (waitpid(cb->pbePid, &status, WNOHANG) > 0) {
				LOG_NO("PBE has terminated due to SC absence");
				cb->pbePid = 0;
			} else {
				LOG_WA(
				    "SC were absent and PBE appears hung, sending SIGKILL");
				kill(cb->pbePid, SIGKILL);
			}
		}

	} else if ((evt->info.mds_info.change == NCSMDS_UP) &&
		   (evt->info.mds_info.svc_id == NCSMDS_SVC_ID_IMMD)) {
		LOG_NO(
		    "IMMD service is UP ... ScAbsenseAllowed?:%u introduced?:%u",
		    cb->mScAbsenceAllowed, cb->mIntroduced);
		if ((cb->mIntroduced == 2) &&
		    (immnd_introduceMe(cb) != NCSCC_RC_SUCCESS)) {
			LOG_WA(
			    "IMMND re-introduceMe after IMMD restart failed, will retry");
		}
	} else if ((evt->info.mds_info.change == NCSMDS_UP) &&
		   (evt->info.mds_info.svc_id == NCSMDS_SVC_ID_IMMA_OI ||
		    evt->info.mds_info.svc_id == NCSMDS_SVC_ID_IMMA_OM)) {
		TRACE_2("IMMA UP EVENT");
	} else if ((evt->info.mds_info.change == NCSMDS_RED_UP) &&
		   (evt->info.mds_info.role == V_DEST_RL_ACTIVE) &&
		   evt->info.mds_info.svc_id == NCSMDS_SVC_ID_IMMD) {
		TRACE_2("IMMD new activeEVENT");
		/*immnd_evt_immd_new_active(cb); */
	} else if ((evt->info.mds_info.change == NCSMDS_CHG_ROLE) &&
		   (evt->info.mds_info.role == V_DEST_RL_ACTIVE) &&
		   (evt->info.mds_info.svc_id == NCSMDS_SVC_ID_IMMD)) {

		TRACE_2("IMMD FAILOVER");
		/* The IMMD has failed over. */
		immnd_proc_imma_discard_stales(cb);
	} else if (evt->info.mds_info.svc_id == NCSMDS_SVC_ID_IMMND) {
		LOG_NO("MDS SERVICE EVENT OF TYPE IMMND!!");
	}
	/*TRACE_LEAVE(); */
	return rc;
}

/****************************************************************************
 * Name          : immnd_evt_proc_cb_dump
 * Description   : Function to dump the Control Block
 * Arguments     : IMMND_CB *cb - IMMND CB pointer
 * Return Values : NCSCC_RC_SUCCESS/Error.
 * Notes         : None.
 *****************************************************************************/
static uint32_t immnd_evt_proc_cb_dump(IMMND_CB *cb)
{
	immnd_cb_dump();

	return NCSCC_RC_SUCCESS;
}

/*****************************************************************************
 * Name          : immnd_is_immd_up
 *
 * Description   : This routine tests whether IMMD is up or down
 * Arguments     : cb       - IMMND Control Block pointer
 *
 * Return Values : true/false
 *****************************************************************************/
uint32_t immnd_is_immd_up(IMMND_CB *cb)
{
	uint32_t is_immd_up;

	m_NCS_LOCK(&cb->immnd_immd_up_lock, NCS_LOCK_WRITE);

	is_immd_up = cb->is_immd_up;

	m_NCS_UNLOCK(&cb->immnd_immd_up_lock, NCS_LOCK_WRITE);

	return is_immd_up;
}
