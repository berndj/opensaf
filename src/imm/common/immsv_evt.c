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
  DESCRIPTION:

  This file consists of IMMSV routines used for encoding/decoding pointer
  structures in an IMMSV_EVT.
*****************************************************************************/

#include <string.h>
#include <stdlib.h>
#include "imm/common/immsv.h"
#include "imm/common/immsv_api.h"
#include "base/osaf_extended_name.h"
#include <saAis.h>

#define IMMSV_RSRV_SPACE_ASSERT(P, B, S)                                       \
	P = ncs_enc_reserve_space(B, S);                                       \
	osafassert(P)
#define IMMSV_FLTN_SPACE_ASSERT(P, M, B, S)                                    \
	P = ncs_dec_flatten_space(B, M, S);                                    \
	osafassert(P)

/* This array must match IMMD_EVT_TYPE */
static const char *immd_evt_names[] = {"undefined",
				       "IMMD_EVT_MDS_INFO",
				       "IMMD_EVT_ND2D_INTRO",
				       "IMMD_EVT_ND2D_ANNOUNCE_LOADING",
				       "IMMD_EVT_ND2D_REQ_SYNC",
				       "IMMD_EVT_ND2D_ANNOUNCE_DUMP",
				       "IMMD_EVT_ND2D_SYNC_START",
				       "IMMD_EVT_ND2D_SYNC_ABORT",
				       "IMMD_EVT_ND2D_ADMINIT_REQ",
				       "IMMD_EVT_ND2D_ACTIVE_SET",
				       "IMMD_EVT_ND2D_IMM_SYNC_INFO2",
				       "IMMD_EVT_ND2D_FEVS_REQ",
				       "IMMD_EVT_ND2D_CCBINIT_REQ",
				       "IMMD_EVT_ND2D_IMPLSET_REQ",
				       "IMMD_EVT_ND2D_OI_OBJ_MODIFY",
				       "IMMD_EVT_ND2D_DISCARD_IMPL",
				       "IMMD_EVT_ND2D_ABORT_CCB",
				       "IMMD_EVT_ND2D_ADMO_HARD_FINALIZE",
				       "IMMD_EVT_CB_DUMP",
				       "IMMD_EVT_MDS_QUIESCED_ACK_RSP",
				       "IMMD_EVT_RDA_CB",
				       "IMMD_EVT_ND2D_PBE_PRTO_PURGE_MUTATIONS",
				       "IMMD_EVT_ND2D_LOADING_FAILED",
				       "IMMD_EVT_ND2D_SYNC_FEVS_BASE",
				       "IMMD_EVT_ND2D_FEVS_REQ_2",
				       "IMMD_EVT_ND2D_LOADING_COMPLETED",
				       "IMMD_EVT_ND2D_2PBE_PRELOAD",
				       "IMMD_EVT_ND2D_IMPLSET_REQ_2",
				       "IMMD_EVT_ND2D_IMPLDELETE",
				       "undefined (high)"};

static const char *immsv_get_immd_evt_name(unsigned int id)
{
	if (id < IMMD_EVT_MAX)
		return immd_evt_names[id];
	else
		return "unknown";
}

/* This array must match IMMND_EVT_TYPE */
static const char *immnd_evt_names[] = {
    "undefined",
    "IMMND_EVT_MDS_INFO",	     /* IMMA/IMMND/IMMD UP/DOWN Info */
    "IMMND_EVT_TIME_OUT",	     /* Time out event */
    "IMMND_EVT_A2ND_IMM_INIT",	/* ImmOm Initialization */
    "IMMND_EVT_A2ND_IMM_OI_INIT",     /* ImmOi Initialization */
    "IMMND_EVT_A2ND_IMM_FINALIZE",    /* ImmOm finalization */
    "IMMND_EVT_A2ND_IMM_OI_FINALIZE", /* ImmOi finalization */
    "IMMND_EVT_A2ND_IMM_ADMINIT",     /* AdminOwnerInitialize */
    "IMMND_EVT_A2ND_ADMO_FINALIZE",   /* AdminOwnerFinalize */
    "IMMND_EVT_A2ND_ADMO_SET",	/* AdminOwnerSet */
    "IMMND_EVT_A2ND_ADMO_RELEASE",    /* AdminOwnerRelease */
    "IMMND_EVT_A2ND_ADMO_CLEAR",      /* AdminOwnerClear */
    "IMMND_EVT_A2ND_IMM_ADMOP",       /* Syncronous AdminOp */
    "IMMND_EVT_A2ND_IMM_ADMOP_ASYNC", /* Asyncronous AdminOp */
    "IMMND_EVT_A2ND_IMM_FEVS",	/* Fake EVS msg from Agent (forward) */
    "IMMND_EVT_A2ND_CCBINIT",	 /* CcbInitialize */
    "IMMND_EVT_A2ND_SEARCHINIT",      /* SearchInitialize */
    "IMMND_EVT_A2ND_SEARCHNEXT",      /* SearchNext */
    "IMMND_EVT_A2ND_SEARCHFINALIZE",  /* SearchFinalize */
    "IMMND_EVT_A2ND_SEARCH_REMOTE",   /* forward fetch of pure rt attr vals */
    "IMMND_EVT_A2ND_RT_ATT_UPPD_RSP", /* reply for fetch of pure rt attr vals */
    "IMMND_EVT_A2ND_ADMOP_RSP",       /* AdminOperation sync local Reply */
    "IMMND_EVT_A2ND_ASYNC_ADMOP_RSP", /* AdminOperation async local Reply */
    "IMMND_EVT_A2ND_CCB_COMPLETED_RSP",  /* CcbCompleted local Reply */
    "IMMND_EVT_A2ND_CCB_OBJ_CREATE_RSP", /*CcbObjCreate local Reply */
    "IMMND_EVT_A2ND_CCB_OBJ_MODIFY_RSP", /*CcbObjModify local Reply */
    "IMMND_EVT_A2ND_CCB_OBJ_DELETE_RSP", /*CcbObjDelete local Reply */
    "IMMND_EVT_A2ND_CLASS_CREATE",       /* saImmOmClassCreate */
    "IMMND_EVT_A2ND_CLASS_DESCR_GET",    /* saImmOmClassDescriptionGet */
    "IMMND_EVT_A2ND_CLASS_DELETE",       /* saImmOmClassDelete */
    "IMMND_EVT_A2ND_OBJ_CREATE",	 /* saImmOmCcbObjectCreate */
    "IMMND_EVT_A2ND_OBJ_MODIFY",	 /* saImmOmCcbObjectModify */
    "IMMND_EVT_A2ND_OBJ_DELETE",	 /* saImmOmCcbObjectDelete */
    "IMMND_EVT_A2ND_CCB_APPLY",		 /* saImmOmCcbApply */
    "IMMND_EVT_A2ND_CCB_FINALIZE",       /* saImmOmCcbFinalize */
    "IMMND_EVT_A2ND_OBJ_SYNC",		 /* immsv_sync */
    "IMMND_EVT_A2ND_SYNC_FINALIZE",      /* immsv_finalize_sync */
    "IMMND_EVT_A2ND_OI_OBJ_CREATE",      /* saImmOiRtObjectCreate */
    "IMMND_EVT_A2ND_OI_OBJ_MODIFY",      /* saImmOiRtObjectUpdate */
    "IMMND_EVT_A2ND_OI_OBJ_DELETE",      /* saImmOiRtObjectDelete */
    "IMMND_EVT_A2ND_OI_IMPL_SET",	/* saImmOiImplementerSet */
    "IMMND_EVT_A2ND_OI_IMPL_CLR",	/* saImmOiImplementerClear */
    "IMMND_EVT_A2ND_OI_CL_IMPL_SET",     /* saImmOiClassImplementerSet */
    "IMMND_EVT_A2ND_OI_CL_IMPL_REL",     /* saImmOiClassImplementerRelease */
    "IMMND_EVT_A2ND_OI_OBJ_IMPL_SET",    /* saImmOiObjectImplementerSet */
    "IMMND_EVT_A2ND_OI_OBJ_IMPL_REL",    /* saImmOiObjectImplementerRelease */
    "IMMND_EVT_ND2ND_ADMOP_RSP",	 /* AdminOperation sync fevs Reply */
    "IMMND_EVT_ND2ND_ASYNC_ADMOP_RSP",   /* AdminOperation async fevs Reply */
    "IMMND_EVT_ND2ND_SYNC_FINALIZE", /* Sync finalize from coord over fevs */
    "IMMND_EVT_ND2ND_SEARCH_REMOTE", /* Forward of search request ro impl ND */
    "IMMND_EVT_ND2ND_SEARCH_REMOTE_RSP", /* Forward of search request ro impl ND
					  */
    "IMMND_EVT_D2ND_INTRO_RSP",
    "IMMND_EVT_D2ND_SYNC_REQ",
    "IMMND_EVT_D2ND_LOADING_OK",
    "IMMND_EVT_D2ND_SYNC_START",
    "IMMND_EVT_D2ND_SYNC_ABORT",
    "IMMND_EVT_D2ND_DUMP_OK",
    "IMMND_EVT_D2ND_GLOB_FEVS_REQ", /* Fake EVS msg from director (consume) */
    "IMMND_EVT_D2ND_ADMINIT",       /* Admin Owner init reply */
    "IMMND_EVT_D2ND_CCBINIT",       /* Ccb init reply */
    "IMMND_EVT_D2ND_IMPLSET_RSP",   /* Implementer set reply from D with impl id
				     */
    "IMMND_EVT_D2ND_DISCARD_IMPL",  /* Discard implementer broadcast to NDs */
    "IMMND_EVT_D2ND_ABORT_CCB",     /* Abort CCB broadcast to NDs */
    "IMMND_EVT_D2ND_ADMO_HARD_FINALIZE", /* Admo hard finalize broadcast to NDs
					  */
    "IMMND_EVT_D2ND_DISCARD_NODE", /* Crashed IMMND or node, broadcast to NDs */
    "IMMND_EVT_D2ND_RESET",	/* Restart all IMMNDs. */
    "IMMND_EVT_CB_DUMP",
    "IMMND_EVT_A2ND_IMM_OM_RESURRECT",
    "IMMND_EVT_A2ND_IMM_OI_RESURRECT",
    "IMMND_EVT_A2ND_IMM_CLIENTHIGH",
    "IMMND_EVT_ND2ND_SYNC_FINALIZE_2",
    "IMMND_EVT_A2ND_RECOVER_CCB_OUTCOME",
    "IMMND_EVT_A2ND_PBE_PRT_OBJ_CREATE_RSP",
    "IMMND_EVT_D2ND_PBE_PRTO_PURGE_MUTATIONS",
    "IMMND_EVT_A2ND_PBE_PRTO_DELETES_COMPLETED_RSP",
    "IMMND_EVT_A2ND_PBE_PRT_ATTR_UPDATE_RSP",
    "IMMND_EVT_A2ND_IMM_OM_CLIENTHIGH",
    "IMMND_EVT_A2ND_IMM_OI_CLIENTHIGH",
    "IMMND_EVT_A2ND_PBE_ADMOP_RSP",
    "IMMND_EVT_D2ND_SYNC_FEVS_BASE",
    "IMMND_EVT_A2ND_OBJ_SYNC_2",
    "IMMND_EVT_A2ND_IMM_FEVS_2",
    "IMMND_EVT_D2ND_GLOB_FEVS_REQ_2",
    "IMMND_EVT_A2ND_CCB_COMPLETED_RSP_2",  /* CcbCompleted local Reply */
    "IMMND_EVT_A2ND_CCB_OBJ_CREATE_RSP_2", /*CcbObjCreate local Reply */
    "IMMND_EVT_A2ND_CCB_OBJ_MODIFY_RSP_2", /*CcbObjModify local Reply */
    "IMMND_EVT_A2ND_CCB_OBJ_DELETE_RSP_2", /*CcbObjDelete local Reply */
    "IMMND_EVT_A2ND_ADMOP_RSP_2",       /* AdminOp sync fevs Reply - extended */
    "IMMND_EVT_A2ND_ASYNC_ADMOP_RSP_2", /* AdminOp async fevs Reply - extended
					 */
    "IMMND_EVT_ND2ND_ADMOP_RSP_2",      /* AdminOp sync p2p Reply - extended */
    "IMMND_EVT_ND2ND_ASYNC_ADMOP_RSP_2", /* AdminOp async p2p Reply - extended
					  */
    "IMMND_EVT_A2ND_OI_CCB_AUG_INIT",
    "IMMND_EVT_A2ND_AUG_ADMO",
    "IMMND_EVT_A2ND_CL_TIMEOUT",
    "IMMND_EVT_A2ND_ACCESSOR_GET",
    "IMMND_EVT_A2ND_CCB_VALIDATE",    /* saImmOmCcbValidate */
    "IMMND_EVT_A2ND_OI_IMPL_SET_2",   /* saImmOiImplementerSet */
    "IMMND_EVT_D2ND_IMPLSET_RSP_2",   /* Implementer set reply from D with impl id
				       */
    "IMMND_EVT_A2ND_OBJ_CREATE_2",    /* saImmOmCcbObjectCreate_o3 */
    "IMMND_EVT_A2ND_OI_OBJ_CREATE_2", /* saImmOiRtObjectCreate_o3 */
    "IMMND_EVT_A2ND_OBJ_SAFE_READ",   /* saImmOmCcbObjectRead */
    "IMMND_EVT_D2ND_IMPLDELETE",
    "undefined (high)"};

static const char *immsv_get_immnd_evt_name(unsigned int id)
{
	if (id < IMMND_EVT_MAX)
		return immnd_evt_names[id];
	else
		return "unknown";
}

void immsv_evt_enc_inline_string(NCS_UBAID *o_ub, IMMSV_OCTET_STRING *os)
{
	if (os->size) {
		if (ncs_encode_n_octets_in_uba(o_ub, (uint8_t *)os->buf,
					       os->size) != NCSCC_RC_SUCCESS) {
			LOG_ER(
			    "Failure of ncs_encode_n_octets_in_uba in enc_inline_string");
			abort();
		}
	}
}

bool immsv_evt_enc_inline_text(int line, NCS_UBAID *o_ub,
			       IMMSV_OCTET_STRING *os)
{
	if (os->size) {
		if (strnlen((char *)os->buf, os->size) + 1 < os->size) {
			LOG_WA(
			    "immsv_evt_enc_inline_text: Length missmatch from source line:%u (%zu %u '%s')",
			    line, strnlen((char *)os->buf, os->size) + 1,
			    os->size, os->buf);
			return false;
		}

		if (ncs_encode_n_octets_in_uba(o_ub, (uint8_t *)os->buf,
					       os->size) != NCSCC_RC_SUCCESS) {
			LOG_ER(
			    "Failure of ncs_encode_n_octets_in_uba in enc_inline_text");
			return false;
		}
	}
	return true;
}

void immsv_evt_dec_inline_string(NCS_UBAID *i_ub, IMMSV_OCTET_STRING *os)
{
	if (os->size) {
		os->buf = (void *)calloc(1, os->size);

		if (ncs_decode_n_octets_from_uba(i_ub, (uint8_t *)os->buf,
						 os->size) !=
		    NCSCC_RC_SUCCESS) {
			LOG_ER("Failure inside ncs_decode_n_octets_from_uba");
			abort();
		}
	} else {
		os->buf = NULL;
	}
}

static void immsv_evt_enc_att_val(NCS_UBAID *o_ub, IMMSV_EDU_ATTR_VAL *v,
				  SaImmValueTypeT t)
{
	uint8_t *p8;
	uint32_t helper32;
	uint64_t helper64;
	void *anonymous;
	IMMSV_OCTET_STRING *os = NULL;

	switch (t) {
	case SA_IMM_ATTR_SANAMET:
	/* Intentional fall through */
	case SA_IMM_ATTR_SASTRINGT:
		os = &(v->val.x);
		IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
		ncs_encode_32bit(&p8, os->size);
		ncs_enc_claim_space(o_ub, 4);
		osafassert(immsv_evt_enc_inline_text(__LINE__, o_ub, os));
		break;
	case SA_IMM_ATTR_SAANYT:
		os = &(v->val.x);
		IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
		ncs_encode_32bit(&p8, os->size);
		ncs_enc_claim_space(o_ub, 4);
		immsv_evt_enc_inline_string(o_ub, os);
		break;
	case SA_IMM_ATTR_SAINT32T:
		IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
		ncs_encode_32bit(&p8, v->val.saint32);
		ncs_enc_claim_space(o_ub, 4);
		break;
	case SA_IMM_ATTR_SAUINT32T:
		IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
		ncs_encode_32bit(&p8, v->val.sauint32);
		ncs_enc_claim_space(o_ub, 4);
		break;
	case SA_IMM_ATTR_SAINT64T:
		IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
		ncs_encode_64bit(&p8, v->val.saint64);
		ncs_enc_claim_space(o_ub, 8);
		break;
	case SA_IMM_ATTR_SAUINT64T:
		IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
		ncs_encode_64bit(&p8, v->val.sauint64);
		ncs_enc_claim_space(o_ub, 8);
		break;
	case SA_IMM_ATTR_SATIMET:
		IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
		ncs_encode_64bit(&p8, v->val.satime);
		ncs_enc_claim_space(o_ub, 8);
		break;
	case SA_IMM_ATTR_SAFLOATT:
		// helper32 = *((uint32_t *)&(v->val.safloat));
		anonymous = &(v->val.safloat);
		helper32 = *((uint32_t *)anonymous);
		IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
		ncs_encode_32bit(&p8, helper32);
		ncs_enc_claim_space(o_ub, 4);
		break;
	case SA_IMM_ATTR_SADOUBLET:
		// helper64 = *((uint64_t *)&(v->val.sadouble));
		anonymous = &(v->val.sadouble);
		helper64 = *((uint64_t *)anonymous);
		IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
		ncs_encode_64bit(&p8, helper64);
		ncs_enc_claim_space(o_ub, 8);
		break;

	default:
		LOG_ER("Unrecognized IMM value type: %u", t);
		osafassert(0);
	}
}

static void immsv_evt_dec_att_val(NCS_UBAID *i_ub, IMMSV_EDU_ATTR_VAL *v,
				  SaImmValueTypeT t)
{
	uint8_t *p8;
	uint32_t helper32;
	uint64_t helper64;
	uint8_t local_data[8];
	void *anonymous;
	IMMSV_OCTET_STRING *os = NULL;

	switch (t) {
	case SA_IMM_ATTR_SANAMET:
	/*
	   osafassert((*opp)->paramBuffer.val.x.size <= SA_MAX_NAME_LENGTH);
	   Assume correct encoding
	 */
	case SA_IMM_ATTR_SASTRINGT:
	case SA_IMM_ATTR_SAANYT:
		os = &(v->val.x);
		IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
		os->size = ncs_decode_32bit(&p8);
		ncs_dec_skip_space(i_ub, 4);
		immsv_evt_dec_inline_string(i_ub, os);
		break;
	case SA_IMM_ATTR_SAINT32T:
		IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
		v->val.saint32 = ncs_decode_32bit(&p8);
		ncs_dec_skip_space(i_ub, 4);
		break;
	case SA_IMM_ATTR_SAUINT32T:
		IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
		v->val.sauint32 = ncs_decode_32bit(&p8);
		ncs_dec_skip_space(i_ub, 4);
		break;
	case SA_IMM_ATTR_SAINT64T:
		IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
		v->val.saint64 = ncs_decode_64bit(&p8);
		ncs_dec_skip_space(i_ub, 8);
		break;
	case SA_IMM_ATTR_SAUINT64T:
		IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
		v->val.sauint64 = ncs_decode_64bit(&p8);
		ncs_dec_skip_space(i_ub, 8);
		break;
	case SA_IMM_ATTR_SATIMET:
		IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
		v->val.satime = ncs_decode_64bit(&p8);
		ncs_dec_skip_space(i_ub, 8);
		break;
	case SA_IMM_ATTR_SAFLOATT:
		IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
		helper32 = ncs_decode_32bit(&p8);
		ncs_dec_skip_space(i_ub, 4);
		// v->val.safloat = *((float *)&helper32);
		anonymous = &helper32;
		v->val.safloat = *((float *)anonymous);
		break;
	case SA_IMM_ATTR_SADOUBLET:
		IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
		helper64 = ncs_decode_64bit(&p8);
		ncs_dec_skip_space(i_ub, 8);
		// v->val.sadouble = *((double *)&helper64);
		anonymous = &helper64;
		v->val.sadouble = *((double *)anonymous);
		break;

	default:
		LOG_ER("Unrecognized IMM value type: %u", t);
		osafassert(0);
	}
}

void immsv_evt_free_att_val_raw(IMMSV_EDU_ATTR_VAL *p, long valueType)
{
	immsv_evt_free_att_val(p, valueType);
}

void immsv_evt_free_att_val(IMMSV_EDU_ATTR_VAL *p, SaImmValueTypeT t)
{
	switch (t) {
	case SA_IMM_ATTR_SAINT32T:
	case SA_IMM_ATTR_SAUINT32T:
	case SA_IMM_ATTR_SAINT64T:
	case SA_IMM_ATTR_SAUINT64T:
	case SA_IMM_ATTR_SATIMET:
	case SA_IMM_ATTR_SAFLOATT:
	case SA_IMM_ATTR_SADOUBLET:
		return;

	case SA_IMM_ATTR_SANAMET:
	case SA_IMM_ATTR_SASTRINGT:
	case SA_IMM_ATTR_SAANYT:
		if (p->val.x.size) {
			free(p->val.x.buf);
			p->val.x.buf = NULL;
			p->val.x.size = 0;
		}
		return;

	default:
		TRACE_1("Incorrect value for SaImmValueTypeT:%u. "
			"Did you forget to set the attrValueType member in a "
			"SaImmAttrValuesT value ?",
			t);
		osafassert(0);
	}
}

static void immsv_evt_enc_attr_def(NCS_UBAID *o_ub, IMMSV_ATTR_DEF_LIST *ad)
{
	if (!ad)
		return;
	uint8_t *p8;

	IMMSV_OCTET_STRING *os = &(ad->d.attrName);
	IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
	ncs_encode_32bit(&p8, os->size);
	ncs_enc_claim_space(o_ub, 4);
	osafassert(immsv_evt_enc_inline_text(__LINE__, o_ub, os));

	IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
	ncs_encode_32bit(&p8, ad->d.attrValueType);
	ncs_enc_claim_space(o_ub, 4);

	IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
	ncs_encode_64bit(&p8, ad->d.attrFlags);
	ncs_enc_claim_space(o_ub, 8);

	IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
	ncs_encode_32bit(&p8, ad->d.attrNtfId);
	ncs_enc_claim_space(o_ub, 4);

	IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 1);
	ncs_encode_8bit(&p8, (ad->d.attrDefaultValue) ? 1 : 0);
	ncs_enc_claim_space(o_ub, 1);
	if (ad->d.attrDefaultValue) {
		immsv_evt_enc_att_val(o_ub, ad->d.attrDefaultValue,
				      ad->d.attrValueType);
	}
	IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 1);
	ncs_encode_8bit(&p8, (ad->next) ? 1 : 0);
	ncs_enc_claim_space(o_ub, 1);
}

static void immsv_evt_enc_attr_mod(NCS_UBAID *o_ub, IMMSV_ATTR_MODS_LIST *p)
{
	if (!p)
		return;
	uint8_t *p8;
	uint32_t attrValuesNumber = p->attrValue.attrValuesNumber;

	IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
	ncs_encode_32bit(&p8, p->attrModType);
	ncs_enc_claim_space(o_ub, 4);

	IMMSV_OCTET_STRING *os = &(p->attrValue.attrName);
	IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
	ncs_encode_32bit(&p8, os->size);
	ncs_enc_claim_space(o_ub, 4);
	osafassert(immsv_evt_enc_inline_text(__LINE__, o_ub, os));

	IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
	ncs_encode_32bit(&p8, p->attrValue.attrValueType);
	ncs_enc_claim_space(o_ub, 4);

	IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
	ncs_encode_32bit(&p8, attrValuesNumber);
	ncs_enc_claim_space(o_ub, 4);

	if (attrValuesNumber) {
		immsv_evt_enc_att_val(o_ub, &(p->attrValue.attrValue),
				      p->attrValue.attrValueType);
		--attrValuesNumber;

		IMMSV_EDU_ATTR_VAL_LIST *al = p->attrValue.attrMoreValues;
		while (al && attrValuesNumber) {
			immsv_evt_enc_att_val(o_ub, &(al->n),
					      p->attrValue.attrValueType);
			al = al->next;
			--attrValuesNumber;
		}
	}
	osafassert(!attrValuesNumber);

	IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 1);
	ncs_encode_8bit(&p8, (p->next) ? 1 : 0);
	ncs_enc_claim_space(o_ub, 1);
}

static void immsv_evt_enc_attribute(NCS_UBAID *o_ub, IMMSV_ATTR_VALUES_LIST *p)
{
	if (!p)
		return;
	uint8_t *p8;
	uint32_t attrValuesNumber = p->n.attrValuesNumber;

	/*TRACE_2("attribute:%s values:%u", p->n.attrName.buf,
	 * attrValuesNumber); */

	IMMSV_OCTET_STRING *os = &(p->n.attrName);
	IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
	ncs_encode_32bit(&p8, os->size);
	ncs_enc_claim_space(o_ub, 4);
	osafassert(immsv_evt_enc_inline_text(__LINE__, o_ub, os));

	IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
	ncs_encode_32bit(&p8, p->n.attrValueType);
	ncs_enc_claim_space(o_ub, 4);

	IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
	ncs_encode_32bit(&p8, attrValuesNumber);
	ncs_enc_claim_space(o_ub, 4);

	if (attrValuesNumber) {
		immsv_evt_enc_att_val(o_ub, &(p->n.attrValue),
				      p->n.attrValueType);
		--attrValuesNumber;

		IMMSV_EDU_ATTR_VAL_LIST *al = p->n.attrMoreValues;
		while (al && attrValuesNumber) {
			immsv_evt_enc_att_val(o_ub, &(al->n),
					      p->n.attrValueType);
			al = al->next;
			--attrValuesNumber;
		}
	}
	osafassert(!attrValuesNumber);

	IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 1);
	ncs_encode_8bit(&p8, (p->next) ? 1 : 0);
	ncs_enc_claim_space(o_ub, 1);
}

void immsv_free_attr_list(IMMSV_EDU_ATTR_VAL_LIST *al,
			  const SaImmValueTypeT avt)
{
	while (al) {
		IMMSV_EDU_ATTR_VAL_LIST *tmp = al;
		al = al->next;
		tmp->next = NULL;
		immsv_evt_free_att_val(&(tmp->n), avt);
		free(tmp);
	}
}

void immsv_free_attr_list_raw(IMMSV_EDU_ATTR_VAL_LIST *al, const long avt)
{
	immsv_free_attr_list(al, avt);
}

void immsv_free_attrmods(IMMSV_ATTR_MODS_LIST *p)
{
	while (p) {
		IMMSV_ATTR_MODS_LIST *tmp = p;
		p = p->next;
		tmp->next = NULL;
		free(tmp->attrValue.attrName.buf);
		tmp->attrValue.attrName.buf = NULL;
		tmp->attrValue.attrName.size = 0;
		if (tmp->attrValue.attrValuesNumber) {
			immsv_evt_free_att_val(&(tmp->attrValue.attrValue),
					       tmp->attrValue.attrValueType);
			if (tmp->attrValue.attrValuesNumber > 1) {
				immsv_free_attr_list(
				    tmp->attrValue.attrMoreValues,
				    tmp->attrValue.attrValueType);
			}
		}
		free(tmp);
	}
}

void immsv_free_attrvalues_list(IMMSV_ATTR_VALUES_LIST *avl)
{
	/*TRACE_ENTER(); */
	while (avl) {
		IMMSV_ATTR_VALUES_LIST *tmp = avl;
		avl = avl->next;
		tmp->next = NULL;
		free(tmp->n.attrName.buf);
		tmp->n.attrName.buf = NULL;
		tmp->n.attrName.size = 0;
		if (tmp->n.attrValuesNumber) {
			immsv_evt_free_att_val(&(tmp->n.attrValue),
					       tmp->n.attrValueType);
			if (tmp->n.attrValuesNumber > 1) {
				immsv_free_attr_list(tmp->n.attrMoreValues,
						     tmp->n.attrValueType);
			}
		}
		free(tmp);
	}
	/*TRACE_LEAVE(); */
}

static void immsv_evt_dec_attrmods(NCS_UBAID *i_ub, IMMSV_ATTR_MODS_LIST **p)
{
	int depth = 1;
	uint8_t c8;

	do {
		uint8_t *p8;
		uint8_t local_data[8];

		*p = (IMMSV_ATTR_MODS_LIST *)calloc(
		    1, sizeof(IMMSV_ATTR_MODS_LIST));

		IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
		(*p)->attrModType = ncs_decode_32bit(&p8);
		ncs_dec_skip_space(i_ub, 4);

		IMMSV_OCTET_STRING *os = &((*p)->attrValue.attrName);
		IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
		os->size = ncs_decode_32bit(&p8);
		ncs_dec_skip_space(i_ub, 4);
		immsv_evt_dec_inline_string(i_ub, os);

		IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
		(*p)->attrValue.attrValueType = ncs_decode_32bit(&p8);
		ncs_dec_skip_space(i_ub, 4);

		IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
		(*p)->attrValue.attrValuesNumber = ncs_decode_32bit(&p8);
		ncs_dec_skip_space(i_ub, 4);

		uint32_t attrValuesNumber = (*p)->attrValue.attrValuesNumber;

		if (attrValuesNumber) {
			immsv_evt_dec_att_val(i_ub,
					      &((*p)->attrValue.attrValue),
					      (*p)->attrValue.attrValueType);
			--attrValuesNumber;

			IMMSV_EDU_ATTR_VAL_LIST *al = NULL;

			while (attrValuesNumber) {
				al = (IMMSV_EDU_ATTR_VAL_LIST *)calloc(
				    1, sizeof(IMMSV_EDU_ATTR_VAL_LIST));

				immsv_evt_dec_att_val(
				    i_ub, &(al->n),
				    (*p)->attrValue.attrValueType);

				al->next =
				    (*p)->attrValue
					.attrMoreValues; /*NULL initially */
				(*p)->attrValue.attrMoreValues = al;
				--attrValuesNumber;
			}
		}

		IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 1);
		c8 = ncs_decode_8bit(&p8);
		ncs_dec_skip_space(i_ub, 1);
		p = &((*p)->next);
		++depth;
	} while (c8 && (depth < IMMSV_MAX_ATTRIBUTES));

	if (depth >= IMMSV_MAX_ATTRIBUTES) {
		LOG_ER("TOO MANY attribute modifications line:%u", __LINE__);
		osafassert(depth < IMMSV_MAX_ATTRIBUTES);
	}
}

static void immsv_evt_dec_attributes(NCS_UBAID *i_ub,
				     IMMSV_ATTR_VALUES_LIST **p)
{
	int depth = 1;
	uint8_t c8;

	do {
		uint8_t *p8;
		uint8_t local_data[8];

		*p = (IMMSV_ATTR_VALUES_LIST *)calloc(
		    1, sizeof(IMMSV_ATTR_VALUES_LIST));

		IMMSV_OCTET_STRING *os = &((*p)->n.attrName);
		IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
		os->size = ncs_decode_32bit(&p8);
		ncs_dec_skip_space(i_ub, 4);
		immsv_evt_dec_inline_string(i_ub, os);

		IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
		(*p)->n.attrValueType = ncs_decode_32bit(&p8);
		ncs_dec_skip_space(i_ub, 4);

		IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
		(*p)->n.attrValuesNumber = ncs_decode_32bit(&p8);
		ncs_dec_skip_space(i_ub, 4);

		uint32_t attrValuesNumber = (*p)->n.attrValuesNumber;

		if (attrValuesNumber) {
			immsv_evt_dec_att_val(i_ub, &((*p)->n.attrValue),
					      (*p)->n.attrValueType);
			--attrValuesNumber;

			IMMSV_EDU_ATTR_VAL_LIST *al = NULL;

			while (attrValuesNumber) {
				al = (IMMSV_EDU_ATTR_VAL_LIST *)calloc(
				    1, sizeof(IMMSV_EDU_ATTR_VAL_LIST));

				immsv_evt_dec_att_val(i_ub, &(al->n),
						      (*p)->n.attrValueType);

				al->next =
				    (*p)->n.attrMoreValues; /*NULL initially */
				(*p)->n.attrMoreValues = al;
				--attrValuesNumber;
			}
		}

		IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 1);
		c8 = ncs_decode_8bit(&p8);
		ncs_dec_skip_space(i_ub, 1);
		p = &((*p)->next);
		++depth;
	} while (c8 && (depth < IMMSV_MAX_ATTRIBUTES));

	if (depth >= IMMSV_MAX_ATTRIBUTES) {
		LOG_ER("TOO MANY attributes line:%u", __LINE__);
		osafassert(depth < IMMSV_MAX_ATTRIBUTES);
	}
}

static uint32_t immsv_evt_enc_name_list(NCS_UBAID *o_ub, IMMSV_OBJ_NAME_LIST *p)
{
	uint8_t *p8;
	uint16_t objs = 0;

	IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 1);
	ncs_encode_8bit(&p8, p ? 1 : 0);
	ncs_enc_claim_space(o_ub, 1);

	while (p && (objs < IMMSV_MAX_OBJECTS)) {
		++objs;
		IMMSV_OCTET_STRING *os = &(p->name);
		IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
		ncs_encode_32bit(&p8, os->size);
		ncs_enc_claim_space(o_ub, 4);
		if (!immsv_evt_enc_inline_text(__LINE__, o_ub, os)) {
			return NCSCC_RC_OUT_OF_MEM;
		}

		p = p->next;
		IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 1);
		ncs_encode_8bit(&p8, p ? 1 : 0);
		ncs_enc_claim_space(o_ub, 1);
	}

	if (objs >= IMMSV_MAX_OBJECTS) {
		LOG_ER("TOO MANY Object Names line:%u", __LINE__);
		return NCSCC_RC_NO_OBJECT;
	}
	return NCSCC_RC_SUCCESS;
}

static uint32_t immsv_evt_dec_name_list(NCS_UBAID *i_ub,
					IMMSV_OBJ_NAME_LIST **p)
{
	int depth = 1;
	uint8_t *p8;
	uint8_t local_data[8];
	uint8_t c8;
	/*TRACE_ENTER(); */

	IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 1);
	c8 = ncs_decode_8bit(&p8);
	ncs_dec_skip_space(i_ub, 1);

	while (c8 && (depth < IMMSV_MAX_OBJECTS)) {

		*p = (IMMSV_OBJ_NAME_LIST *)calloc(1,
						   sizeof(IMMSV_OBJ_NAME_LIST));

		IMMSV_OCTET_STRING *os = &((*p)->name);
		IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
		os->size = ncs_decode_32bit(&p8);
		ncs_dec_skip_space(i_ub, 4);
		immsv_evt_dec_inline_string(i_ub, os);

		IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 1);
		c8 = ncs_decode_8bit(&p8);
		ncs_dec_skip_space(i_ub, 1);
		p = &((*p)->next);
		++depth;
	}

	if (depth >= IMMSV_MAX_OBJECTS) {
		LOG_ER("TOO MANY Object Names line:%u", __LINE__);
		return NCSCC_RC_OUT_OF_MEM;
	}
	return NCSCC_RC_SUCCESS;
	/*TRACE_LEAVE(); */
}

void immsv_evt_free_name_list(IMMSV_OBJ_NAME_LIST *p)
{
	while (p) {
		IMMSV_OBJ_NAME_LIST *tmp = p;
		p = p->next;
		tmp->next = NULL;
		free(tmp->name.buf);
		tmp->name.buf = NULL;
		tmp->name.size = 0;
		free(tmp);
	}
}

static void immsv_evt_enc_class(NCS_UBAID *o_ub, IMMSV_CLASS_LIST *r)
{
	if (!r)
		return;
	uint8_t *p8;

	IMMSV_OCTET_STRING *os = &(r->className);
	IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
	ncs_encode_32bit(&p8, os->size);
	ncs_enc_claim_space(o_ub, 4);
	osafassert(immsv_evt_enc_inline_text(__LINE__, o_ub, os));

	os = &(r->classImplName);
	IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
	ncs_encode_32bit(&p8, os->size);
	ncs_enc_claim_space(o_ub, 4);
	osafassert(immsv_evt_enc_inline_text(__LINE__, o_ub, os));

	IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
	ncs_encode_32bit(&p8, r->nrofInstances);
	ncs_enc_claim_space(o_ub, 4);

	IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 1);
	ncs_encode_8bit(&p8, (r->next) ? 1 : 0);
	ncs_enc_claim_space(o_ub, 1);
}

static uint32_t immsv_evt_dec_class(NCS_UBAID *i_ub, IMMSV_CLASS_LIST **r)
{
	uint8_t c8;

	do {
		uint8_t *p8;
		uint8_t local_data[8];

		*r = (IMMSV_CLASS_LIST *)calloc(1, sizeof(IMMSV_CLASS_LIST));

		IMMSV_OCTET_STRING *os = &((*r)->className);
		IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
		os->size = ncs_decode_32bit(&p8);
		ncs_dec_skip_space(i_ub, 4);
		immsv_evt_dec_inline_string(i_ub, os);

		os = &((*r)->classImplName);
		IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
		os->size = ncs_decode_32bit(&p8);
		ncs_dec_skip_space(i_ub, 4);
		immsv_evt_dec_inline_string(i_ub, os);

		IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
		(*r)->nrofInstances = ncs_decode_32bit(&p8);
		ncs_dec_skip_space(i_ub, 4);

		IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 1);
		c8 = ncs_decode_8bit(&p8);
		ncs_dec_skip_space(i_ub, 1);

		r = &((*r)->next);
	} while (c8);

	return NCSCC_RC_SUCCESS;
}

void immsv_evt_free_classList(IMMSV_CLASS_LIST *p)
{
	while (p) {
		IMMSV_CLASS_LIST *tmp = p;
		p = p->next;
		tmp->next = NULL;
		free(tmp->className.buf);
		tmp->className.buf = NULL;
		tmp->className.size = 0;
		free(tmp->classImplName.buf);
		tmp->classImplName.buf = NULL;
		tmp->classImplName.size = 0;
		free(tmp);
	}
}

void immsv_evt_free_ccbOutcomeList(IMMSV_CCB_OUTCOME_LIST *o)
{
	while (o) {
		IMMSV_CCB_OUTCOME_LIST *tmp = o;
		o = o->next;
		tmp->next = NULL;
		free(tmp);
	}
}

static void immsv_evt_enc_impl(NCS_UBAID *o_ub, IMMSV_IMPL_LIST *q)
{
	if (!q)
		return;
	uint8_t *p8;

	IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
	ncs_encode_32bit(&p8, q->id);
	ncs_enc_claim_space(o_ub, 4);

	IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
	ncs_encode_32bit(&p8, q->nodeId);
	ncs_enc_claim_space(o_ub, 4);

	IMMSV_OCTET_STRING *os = &(q->implementerName);
	IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
	ncs_encode_32bit(&p8, os->size);
	ncs_enc_claim_space(o_ub, 4);
	osafassert(immsv_evt_enc_inline_text(__LINE__, o_ub, os));

	IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
	ncs_encode_64bit(&p8, q->mds_dest);
	ncs_enc_claim_space(o_ub, 8);

	IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 1);
	ncs_encode_8bit(&p8, (q->next) ? 1 : 0);
	ncs_enc_claim_space(o_ub, 1);
}

static uint32_t immsv_evt_dec_impl(NCS_UBAID *i_ub, IMMSV_IMPL_LIST **q)
{
	uint8_t c8;

	do {
		uint8_t *p8;
		uint8_t local_data[8];

		*q = (IMMSV_IMPL_LIST *)calloc(1, sizeof(IMMSV_IMPL_LIST));

		IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
		(*q)->id = ncs_decode_32bit(&p8);
		ncs_dec_skip_space(i_ub, 4);

		IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
		(*q)->nodeId = ncs_decode_32bit(&p8);
		ncs_dec_skip_space(i_ub, 4);

		IMMSV_OCTET_STRING *os = &((*q)->implementerName);
		IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
		os->size = ncs_decode_32bit(&p8);
		ncs_dec_skip_space(i_ub, 4);
		immsv_evt_dec_inline_string(i_ub, os);

		IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
		(*q)->mds_dest = ncs_decode_64bit(&p8);
		ncs_dec_skip_space(i_ub, 8);

		IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 1);
		c8 = ncs_decode_8bit(&p8);
		ncs_dec_skip_space(i_ub, 1);

		q = &((*q)->next);
	} while (c8);

	return NCSCC_RC_SUCCESS;
}

void immsv_evt_free_impl(IMMSV_IMPL_LIST *q)
{
	while (q) {
		IMMSV_IMPL_LIST *tmp = q;
		q = q->next;
		tmp->next = NULL;
		free(tmp->implementerName.buf);
		tmp->implementerName.buf = NULL;
		tmp->implementerName.size = 0;
		free(tmp);
	}
}

static uint32_t immsv_evt_enc_admo(NCS_UBAID *o_ub, IMMSV_ADMO_LIST *p)
{
	if (!p)
		return NCSCC_RC_SUCCESS;
	uint8_t *p8;
	uint32_t rc = NCSCC_RC_SUCCESS;

	IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
	ncs_encode_32bit(&p8, p->id);
	ncs_enc_claim_space(o_ub, 4);

	IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
	ncs_encode_32bit(&p8, p->nodeId);
	ncs_enc_claim_space(o_ub, 4);

	IMMSV_OCTET_STRING *os = &(p->adminOwnerName);
	IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
	ncs_encode_32bit(&p8, os->size);
	ncs_enc_claim_space(o_ub, 4);
	if (!immsv_evt_enc_inline_text(__LINE__, o_ub, os)) {
		rc = NCSCC_RC_OUT_OF_MEM;
		goto done;
	}

	IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 1);
	ncs_encode_8bit(&p8, (p->releaseOnFinalize) ? ((p->isDying) ? 0x3 : 0x1)
						    : 0x0);
	ncs_enc_claim_space(o_ub, 1);
	if (p->isDying) {
		TRACE("immsv_evt_enc_admo isDying == TRUE encoded");
	}

	rc = immsv_evt_enc_name_list(o_ub, p->touchedObjects);

	IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 1);
	ncs_encode_8bit(&p8, (p->next) ? 1 : 0);
	ncs_enc_claim_space(o_ub, 1);

done:
	return rc;
}

static uint32_t immsv_evt_dec_admo(NCS_UBAID *i_ub, IMMSV_ADMO_LIST **p)
{
	uint8_t c8;

	do {
		uint8_t *p8;
		uint8_t local_data[8];

		*p = (IMMSV_ADMO_LIST *)calloc(1, sizeof(IMMSV_ADMO_LIST));

		IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
		(*p)->id = ncs_decode_32bit(&p8);
		ncs_dec_skip_space(i_ub, 4);

		IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
		(*p)->nodeId = ncs_decode_32bit(&p8);
		ncs_dec_skip_space(i_ub, 4);

		IMMSV_OCTET_STRING *os = &((*p)->adminOwnerName);
		IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
		os->size = ncs_decode_32bit(&p8);
		ncs_dec_skip_space(i_ub, 4);
		immsv_evt_dec_inline_string(i_ub, os);

		IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 1);
		c8 = ncs_decode_8bit(&p8);
		ncs_dec_skip_space(i_ub, 1);
		(*p)->releaseOnFinalize = (c8 & ((uint8_t)0x1));
		(*p)->isDying = (c8 & ((uint8_t)0x2)) ? 0x1 : 0x0;
		if ((*p)->isDying) {
			TRACE("immsv_evt_dec_admo isDying == TRUE decoded");
		}

		immsv_evt_dec_name_list(i_ub, &((*p)->touchedObjects));

		IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 1);
		c8 = ncs_decode_8bit(&p8);
		ncs_dec_skip_space(i_ub, 1);

		p = &((*p)->next);
	} while (c8);

	return NCSCC_RC_SUCCESS;
}

void immsv_evt_free_admo(IMMSV_ADMO_LIST *p)
{
	while (p) {
		IMMSV_ADMO_LIST *tmp = p;
		p = p->next;
		tmp->next = NULL;

		free(tmp->adminOwnerName.buf);
		tmp->adminOwnerName.buf = NULL;
		tmp->adminOwnerName.size = 0;
		IMMSV_OBJ_NAME_LIST *onl = tmp->touchedObjects;
		while (onl) {
			IMMSV_OBJ_NAME_LIST *onlTmp = onl;
			onl = onl->next;
			onlTmp->next = NULL;
			free(onlTmp->name.buf);
			onlTmp->name.buf = NULL;
			onlTmp->name.size = 0;
			free(onlTmp);
		}
		tmp->touchedObjects = NULL;
		free(tmp);
	}
}

static void immsv_evt_enc_attrName(NCS_UBAID *o_ub, IMMSV_ATTR_NAME_LIST *p)
{
	if (!p)
		return;
	uint8_t *p8;

	IMMSV_OCTET_STRING *os = &(p->name);
	IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
	ncs_encode_32bit(&p8, os->size);
	ncs_enc_claim_space(o_ub, 4);
	osafassert(immsv_evt_enc_inline_text(__LINE__, o_ub, os));

	IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 1);
	ncs_encode_8bit(&p8, (p->next) ? 1 : 0);
	ncs_enc_claim_space(o_ub, 1);
}

static uint32_t immsv_evt_dec_attrNames(NCS_UBAID *i_ub,
					IMMSV_ATTR_NAME_LIST **p)
{
	int depth = 1;
	uint8_t c8;

	do {
		uint8_t *p8;
		uint8_t local_data[8];

		*p = (IMMSV_ATTR_NAME_LIST *)calloc(
		    1, sizeof(IMMSV_ATTR_NAME_LIST));

		IMMSV_OCTET_STRING *os = &((*p)->name);
		IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
		os->size = ncs_decode_32bit(&p8);
		ncs_dec_skip_space(i_ub, 4);
		immsv_evt_dec_inline_string(i_ub, os);

		IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 1);
		c8 = ncs_decode_8bit(&p8);
		ncs_dec_skip_space(i_ub, 1);
		p = &((*p)->next);
		++depth;
	} while (c8 && (depth < IMMSV_MAX_ATTRIBUTES));

	if (depth >= IMMSV_MAX_ATTRIBUTES) {
		LOG_ER("TOO MANY attrNames");
		return NCSCC_RC_OUT_OF_MEM;
	}
	return NCSCC_RC_SUCCESS;
}

void immsv_evt_free_attrNames(IMMSV_ATTR_NAME_LIST *p)
{
	while (p) {
		IMMSV_ATTR_NAME_LIST *tmp = p;
		p = p->next;
		tmp->next = NULL;
		free(tmp->name.buf);
		tmp->name.buf = NULL;
		tmp->name.size = 0;
		free(tmp);
	}
}

static void immsv_evt_enc_admop_param(NCS_UBAID *o_ub,
				      IMMSV_ADMIN_OPERATION_PARAM *op)
{
	if (!op)
		return;
	uint8_t *p8;

	IMMSV_OCTET_STRING *os = &(op->paramName);
	IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
	ncs_encode_32bit(&p8, os->size);
	ncs_enc_claim_space(o_ub, 4);
	osafassert(immsv_evt_enc_inline_text(__LINE__, o_ub, os));

	IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
	ncs_encode_32bit(&p8, op->paramType);
	ncs_enc_claim_space(o_ub, 4);

	immsv_evt_enc_att_val(o_ub, &(op->paramBuffer), op->paramType);

	IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 1);
	ncs_encode_8bit(&p8, (op->next) ? 1 : 0);
	ncs_enc_claim_space(o_ub, 1);
}

void immsv_free_attrdefs_list(IMMSV_ATTR_DEF_LIST *adp)
{
	while (adp) {
		IMMSV_ATTR_DEF_LIST *tmp = adp;
		adp = adp->next;
		tmp->next = NULL;
		free(tmp->d.attrName.buf); /*free-3 */
		tmp->d.attrName.buf = NULL;
		tmp->d.attrName.size = 0;
		if (tmp->d.attrDefaultValue) {
			immsv_evt_free_att_val(
			    tmp->d.attrDefaultValue, /*free-4.2 */
			    tmp->d.attrValueType);
			free(tmp->d.attrDefaultValue); /*free-4.1 */
			tmp->d.attrDefaultValue = NULL;
		}
		free(tmp); /*free-2 */
	}
}

static uint32_t immsv_evt_dec_attr_def(NCS_UBAID *i_ub,
				       IMMSV_ATTR_DEF_LIST **adp)
{
	int depth = 1;
	uint8_t c8;

	do {
		uint8_t *p8;
		uint8_t local_data[8];

		*adp = (IMMSV_ATTR_DEF_LIST *)calloc(
		    1, sizeof(IMMSV_ATTR_DEF_LIST));

		IMMSV_OCTET_STRING *os = &((*adp)->d.attrName);

		IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
		os->size = ncs_decode_32bit(&p8);
		ncs_dec_skip_space(i_ub, 4);
		immsv_evt_dec_inline_string(i_ub, os);

		IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
		(*adp)->d.attrValueType = ncs_decode_32bit(&p8);
		ncs_dec_skip_space(i_ub, 4);

		IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
		(*adp)->d.attrFlags = ncs_decode_64bit(&p8);
		ncs_dec_skip_space(i_ub, 8);

		IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
		(*adp)->d.attrNtfId = ncs_decode_32bit(&p8);
		ncs_dec_skip_space(i_ub, 4);

		IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 1);
		c8 = ncs_decode_8bit(&p8);
		ncs_dec_skip_space(i_ub, 1);

		if (c8) {
			(*adp)->d.attrDefaultValue =
			    (IMMSV_EDU_ATTR_VAL *)calloc(
				1, sizeof(IMMSV_EDU_ATTR_VAL));

			immsv_evt_dec_att_val(i_ub, (*adp)->d.attrDefaultValue,
					      (*adp)->d.attrValueType);
		}

		IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 1);
		c8 = ncs_decode_8bit(&p8);
		ncs_dec_skip_space(i_ub, 1);
		adp = &((*adp)->next);
		++depth;
	} while (c8 && (depth < IMMSV_MAX_ATTRIBUTES));
	if (depth >= IMMSV_MAX_ATTRIBUTES) {
		LOG_ER("TOO MANY ATTR DEFS line:%u", __LINE__);
		return NCSCC_RC_OUT_OF_MEM;
	}
	return NCSCC_RC_SUCCESS;
}

static uint32_t immsv_evt_dec_admop_param(NCS_UBAID *i_ub,
					  IMMSV_ADMIN_OPERATION_PARAM **opp)
{
	int depth = 1;
	uint8_t c8;

	do {
		uint8_t *p8;
		uint8_t local_data[8];

		*opp = (IMMSV_ADMIN_OPERATION_PARAM *)calloc(
		    1, sizeof(IMMSV_ADMIN_OPERATION_PARAM));

		IMMSV_OCTET_STRING *os = &((*opp)->paramName);
		IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
		os->size = ncs_decode_32bit(&p8);
		ncs_dec_skip_space(i_ub, 4);
		immsv_evt_dec_inline_string(i_ub, os);
		IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
		(*opp)->paramType = ncs_decode_32bit(&p8);
		ncs_dec_skip_space(i_ub, 4);

		/*Then decode the potentially variable length part */
		immsv_evt_dec_att_val(i_ub, &((*opp)->paramBuffer),
				      (*opp)->paramType);

		IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 1);
		c8 = ncs_decode_8bit(&p8);
		ncs_dec_skip_space(i_ub, 1);
		opp = &((*opp)->next);
		++depth;
	} while (c8 && (depth < IMMSV_MAX_ATTRIBUTES));
	if (depth >= IMMSV_MAX_ATTRIBUTES) {
		LOG_ER("TOO MANY PARAMS line:%u", __LINE__);
		return NCSCC_RC_OUT_OF_MEM;
	}
	return NCSCC_RC_SUCCESS;
}

/****************************************************************************\
 PROCEDURE NAME : immsv_evt_enc_sublevels.

 DESCRIPTION    : Encodes the sublevels of IMMSV_EVT into userbuf
		  Sublevels are encoded with byter order correction and
		  used by both encode_flat and encode (because we dont
		  really save much space/execution by encoding the bits
		  and pieces of the sub-level as flat bits and pieces).

 ARGUMENTS      : *i_evt - Event Struct.
		  *o_ub  - User Buff.

 RETURNS        : None

 NOTES          :
\*****************************************************************************/
static uint32_t immsv_evt_enc_sublevels(IMMSV_EVT *i_evt, NCS_UBAID *o_ub)
{
	/* Encode the internal Pointers always using byte-order correction. */
	if (i_evt->type == IMMSV_EVT_TYPE_IMMA) {
		if (i_evt->info.imma.type ==
		    IMMA_EVT_ND2A_CLASS_DESCR_GET_RSP) {
			int depth = 0;
			/*Encode attribute definitions. */
			IMMSV_ATTR_DEF_LIST *ad =
			    i_evt->info.imma.info.classDescr.attrDefinitions;
			while (ad && (depth < IMMSV_MAX_ATTRIBUTES)) {
				immsv_evt_enc_attr_def(o_ub, ad);
				ad = ad->next;
				++depth;
			}
			if (depth >= IMMSV_MAX_ATTRIBUTES) {
				LOG_ER("Class description: TOO MANY ATTR DEFS");
				return NCSCC_RC_OUT_OF_MEM;
			}
		} else if (i_evt->info.imma.type ==
			   IMMA_EVT_ND2A_SEARCHBUNDLENEXT_RSP) {
			uint32_t i;
			uint8_t *p8;
			IMMSV_OM_RSP_SEARCH_NEXT *sn;
			IMMSV_OCTET_STRING *os;
			IMMSV_ATTR_VALUES_LIST *al;
			IMMSV_OM_RSP_SEARCH_BUNDLE_NEXT *bsn =
			    i_evt->info.imma.info.searchBundleNextRsp;
			osafassert(bsn);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8, bsn->resultSize);
			ncs_enc_claim_space(o_ub, 4);

			for (i = 0; i < bsn->resultSize; i++) {
				sn = bsn->searchResult[i];
				osafassert(sn);

				os = &(sn->objectName);
				IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
				ncs_encode_32bit(&p8, os->size);
				ncs_enc_claim_space(o_ub, 4);
				if (!immsv_evt_enc_inline_text(__LINE__, o_ub,
							       os)) {
					return NCSCC_RC_OUT_OF_MEM;
				}

				IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 1);
				ncs_encode_8bit(&p8,
						(sn->attrValuesList) ? 1 : 0);
				ncs_enc_claim_space(o_ub, 1);

				if (sn->attrValuesList) {
					int depth = 0;
					al = sn->attrValuesList;
					do {
						immsv_evt_enc_attribute(o_ub,
									al);
						al = al->next;
						++depth;
					} while (al && (depth <
							IMMSV_MAX_ATTRIBUTES));
					if (depth >= IMMSV_MAX_ATTRIBUTES) {
						LOG_ER(
						    "Search next: TOO MANY ATTR DEFS line:%u",
						    __LINE__);
						return NCSCC_RC_OUT_OF_MEM;
					}
				}
			}
		} else if ((i_evt->info.imma.type ==
			    IMMA_EVT_ND2A_SEARCHNEXT_RSP) ||
			   (i_evt->info.imma.type ==
			    IMMA_EVT_ND2A_ACCESSOR_GET_RSP)) {
			uint8_t *p8;
			int depth = 0;
			/*Encode searchNext response */
			IMMSV_OM_RSP_SEARCH_NEXT *sn =
			    i_evt->info.imma.info.searchNextRsp;
			osafassert(sn);

			IMMSV_OCTET_STRING *os = &(sn->objectName);
			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8, os->size);
			ncs_enc_claim_space(o_ub, 4);
			if (!immsv_evt_enc_inline_text(__LINE__, o_ub, os)) {
				return NCSCC_RC_OUT_OF_MEM;
			}

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 1);
			ncs_encode_8bit(&p8, (sn->attrValuesList) ? 1 : 0);
			ncs_enc_claim_space(o_ub, 1);

			if (sn->attrValuesList) {
				IMMSV_ATTR_VALUES_LIST *al = sn->attrValuesList;
				do {
					immsv_evt_enc_attribute(o_ub, al);
					al = al->next;
					++depth;
				} while (al && (depth < IMMSV_MAX_ATTRIBUTES));
				if (depth >= IMMSV_MAX_ATTRIBUTES) {
					LOG_ER(
					    "Search next: TOO MANY ATTR DEFS line:%u",
					    __LINE__);
					return NCSCC_RC_OUT_OF_MEM;
				}
			}
			/*
			   else
			   { No attrValues }
			 */
		} else if ((i_evt->info.imma.type ==
			    IMMA_EVT_ND2A_OI_OBJ_CREATE_UC) ||
			   (i_evt->info.imma.type ==
			    IMMA_EVT_ND2A_OI_OBJ_CREATE_LONG_UC)) {
			int depth = 0;
			/*Encode the className */
			IMMSV_OCTET_STRING *os =
			    &(i_evt->info.imma.info.objCreate.className);
			if (!immsv_evt_enc_inline_text(__LINE__, o_ub, os)) {
				return NCSCC_RC_OUT_OF_MEM;
			}

			/*Encode the parentName */
			os =
			    &(i_evt->info.imma.info.objCreate.parentOrObjectDn);
			if (!immsv_evt_enc_inline_text(__LINE__, o_ub, os)) {
				return NCSCC_RC_OUT_OF_MEM;
			}

			/*Encode the list of attributes */
			IMMSV_ATTR_VALUES_LIST *p =
			    i_evt->info.imma.info.objCreate.attrValues;

			while (p && (depth < IMMSV_MAX_ATTRIBUTES)) {
				immsv_evt_enc_attribute(o_ub, p);
				p = p->next;
				++depth;
			}
			if (depth >= IMMSV_MAX_ATTRIBUTES) {
				LOG_ER("TOO MANY attributes line:%u", __LINE__);
				return NCSCC_RC_OUT_OF_MEM;
			}
		} else if ((i_evt->info.imma.type ==
			    IMMA_EVT_ND2A_OI_OBJ_MODIFY_UC) ||
			   (i_evt->info.imma.type ==
			    IMMA_EVT_ND2A_OI_OBJ_MODIFY_LONG_UC)) {
			int depth = 0;

			/*Encode the objectName */
			IMMSV_OCTET_STRING *os =
			    &(i_evt->info.imma.info.objModify.objectName);
			if (!immsv_evt_enc_inline_text(__LINE__, o_ub, os)) {
				return NCSCC_RC_OUT_OF_MEM;
			}

			/*Encode the list of attribute modifications */
			IMMSV_ATTR_MODS_LIST *p =
			    i_evt->info.imma.info.objModify.attrMods;

			while (p && (depth < IMMSV_MAX_ATTRIBUTES)) {
				immsv_evt_enc_attr_mod(o_ub, p);
				p = p->next;
				++depth;
			}
			if (depth >= IMMSV_MAX_ATTRIBUTES) {
				LOG_ER(
				    "TOO MANY attribute modifications line:%u",
				    __LINE__);
				return NCSCC_RC_OUT_OF_MEM;
			}
		} else if ((i_evt->info.imma.type ==
			    IMMA_EVT_ND2A_OI_OBJ_DELETE_UC) ||
			   (i_evt->info.imma.type ==
			    IMMA_EVT_ND2A_OI_OBJ_DELETE_LONG_UC)) {
			/*Encode the objectName */
			IMMSV_OCTET_STRING *os =
			    &(i_evt->info.imma.info.objDelete.objectName);
			if (!immsv_evt_enc_inline_text(__LINE__, o_ub, os)) {
				return NCSCC_RC_OUT_OF_MEM;
			}
		} else if ((i_evt->info.imma.type == IMMA_EVT_ND2A_IMM_ADMOP) ||
			   (i_evt->info.imma.type ==
			    IMMA_EVT_ND2A_IMM_PBE_ADMOP)) {
			int depth = 0;
			/*Encode the objectName */
			IMMSV_OCTET_STRING *os =
			    &(i_evt->info.imma.info.admOpReq.objectName);
			if (!immsv_evt_enc_inline_text(__LINE__, o_ub, os)) {
				return NCSCC_RC_OUT_OF_MEM;
			}

			/*Encode the param list */
			IMMSV_ADMIN_OPERATION_PARAM *op =
			    i_evt->info.imma.info.admOpReq.params;
			while (op && (depth < IMMSV_MAX_ATTRIBUTES)) {
				immsv_evt_enc_admop_param(o_ub, op);
				op = op->next;
				++depth;
			}
			if (depth >= IMMSV_MAX_ATTRIBUTES) {
				LOG_ER("TOO MANY PARAMS line:%u", __LINE__);
				return NCSCC_RC_OUT_OF_MEM;
			}
		} else if (i_evt->info.imma.type == IMMA_EVT_ND2A_ADMOP_RSP_2) {
			int depth = 0;
			/*Encode the param list */
			IMMSV_ADMIN_OPERATION_PARAM *op =
			    i_evt->info.imma.info.admOpRsp.parms;
			while (op && (depth < IMMSV_MAX_ATTRIBUTES)) {
				immsv_evt_enc_admop_param(o_ub, op);
				op = op->next;
				++depth;
			}
			if (depth >= IMMSV_MAX_ATTRIBUTES) {
				LOG_ER("TOO MANY PARAMS line:%u", __LINE__);
				return NCSCC_RC_OUT_OF_MEM;
			}
		} else if (i_evt->info.imma.type ==
			   IMMA_EVT_ND2A_SEARCH_REMOTE) {
			int depth = 0;
			/*Encode the objectName */
			IMMSV_OCTET_STRING *os =
			    &(i_evt->info.imma.info.searchRemote.objectName);
			if (!immsv_evt_enc_inline_text(__LINE__, o_ub, os)) {
				return NCSCC_RC_OUT_OF_MEM;
			}

			/*Encode the list of attribute names */
			IMMSV_ATTR_NAME_LIST *p =
			    i_evt->info.imma.info.searchRemote.attributeNames;

			while (p && (depth < IMMSV_MAX_ATTRIBUTES)) {
				immsv_evt_enc_attrName(o_ub, p);
				p = p->next;
				++depth;
			}

			if (depth >= IMMSV_MAX_ATTRIBUTES) {
				LOG_ER("TOO MANY attribute names line:%u",
				       __LINE__);
				return NCSCC_RC_OUT_OF_MEM;
			}
		} else if (i_evt->info.imma.type == IMMA_EVT_ND2A_IMM_ERROR_2) {
			int depth = 0;
			IMMSV_ATTR_NAME_LIST *p =
			    i_evt->info.imma.info.errRsp.errStrings;
			while (p && (depth < IMMSV_MAX_ATTRIBUTES)) {
				immsv_evt_enc_attrName(o_ub, p);
				p = p->next;
				++depth;
			}

			if (depth >= IMMSV_MAX_ATTRIBUTES) {
				LOG_ER("TOO MANY Error strings line:%u",
				       __LINE__);
				return NCSCC_RC_OUT_OF_MEM;
			}
		}
	} else if (i_evt->type == IMMSV_EVT_TYPE_IMMD) {
		if ((i_evt->info.immd.type == IMMD_EVT_ND2D_FEVS_REQ) ||
		    (i_evt->info.immd.type == IMMD_EVT_ND2D_FEVS_REQ_2)) {
			IMMSV_OCTET_STRING *os =
			    &(i_evt->info.immd.info.fevsReq.msg);
			immsv_evt_enc_inline_string(o_ub, os);
		} else if ((i_evt->info.immd.type ==
			    IMMD_EVT_ND2D_IMPLSET_REQ) ||
			   (i_evt->info.immd.type ==
			    IMMD_EVT_ND2D_IMPLSET_REQ_2)) {
			IMMSV_OCTET_STRING *os =
			    &(i_evt->info.immd.info.impl_set.r.impl_name);
			if (!immsv_evt_enc_inline_text(__LINE__, o_ub, os)) {
				return NCSCC_RC_OUT_OF_MEM;
			}
		} else if (i_evt->info.immd.type ==
			   IMMD_EVT_ND2D_OI_OBJ_MODIFY) {
			int depth = 0;

			/*Encode the objectName */
			IMMSV_OCTET_STRING *os =
			    &(i_evt->info.immd.info.objModify.objectName);
			if (!immsv_evt_enc_inline_text(__LINE__, o_ub, os)) {
				return NCSCC_RC_OUT_OF_MEM;
			}

			/*Encode the list of attribute modifications */
			IMMSV_ATTR_MODS_LIST *p =
			    i_evt->info.immd.info.objModify.attrMods;

			while (p && (depth < IMMSV_MAX_ATTRIBUTES)) {
				immsv_evt_enc_attr_mod(o_ub, p);
				p = p->next;
				++depth;
			}
			if (depth >= IMMSV_MAX_ATTRIBUTES) {
				LOG_ER(
				    "TOO MANY attribute modifications line:%u",
				    __LINE__);
				return NCSCC_RC_OUT_OF_MEM;
			}
		} else if (i_evt->info.immd.type == IMMD_EVT_ND2D_IMPLDELETE) {
			uint8_t *p8;
			IMMSV_IMPLDELETE *impl_delete = &i_evt->info.immd.info.impl_delete;

			for(SaUint32T i=0; i<impl_delete->size; ++i) {
				uint32_t length = strnlen(impl_delete->implNameList[i].buf,
										  impl_delete->implNameList[i].size);

				IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
				ncs_encode_32bit(&p8, length);
				ncs_enc_claim_space(o_ub, 4);

				if (ncs_encode_n_octets_in_uba(o_ub,
						(uint8_t *)impl_delete->implNameList[i].buf,
						length) != NCSCC_RC_SUCCESS) {
					LOG_WA(
						"Failure inside ncs_encode_n_octets_in_uba");
					return NCSCC_RC_FAILURE;
				}
			}
		} else if ((i_evt->info.immd.info.ctrl_msg.pbeEnabled >= 3) &&
			   (i_evt->info.immd.type ==
			    IMMD_EVT_ND2D_INTRO)) { /* extended intro */

			IMMSV_OCTET_STRING *os =
			    &(i_evt->info.immd.info.ctrl_msg.dir);
			if (!immsv_evt_enc_inline_text(__LINE__, o_ub, os)) {
				return NCSCC_RC_OUT_OF_MEM;
			}

			os = &(i_evt->info.immd.info.ctrl_msg.xmlFile);
			if (!immsv_evt_enc_inline_text(__LINE__, o_ub, os)) {
				return NCSCC_RC_OUT_OF_MEM;
			}

			os = &(i_evt->info.immd.info.ctrl_msg.pbeFile);
			if (!immsv_evt_enc_inline_text(__LINE__, o_ub, os)) {
				return NCSCC_RC_OUT_OF_MEM;
			}
		}
	} else if (i_evt->type == IMMSV_EVT_TYPE_IMMND) {
		if ((i_evt->info.immnd.type == IMMND_EVT_A2ND_IMM_FEVS) ||
		    (i_evt->info.immnd.type == IMMND_EVT_D2ND_GLOB_FEVS_REQ) ||
		    (i_evt->info.immnd.type == IMMND_EVT_A2ND_IMM_FEVS_2) ||
		    (i_evt->info.immnd.type ==
		     IMMND_EVT_D2ND_GLOB_FEVS_REQ_2)) {
			IMMSV_OCTET_STRING *os =
			    &(i_evt->info.immnd.info.fevsReq.msg);
			immsv_evt_enc_inline_string(o_ub, os);
		} else if ((i_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_OI_IMPL_SET) ||
			   (i_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_OI_IMPL_SET_2) ||
			   (i_evt->info.immnd.type ==
			    IMMND_EVT_D2ND_IMPLSET_RSP) ||
			   (i_evt->info.immnd.type ==
			    IMMND_EVT_D2ND_IMPLSET_RSP_2) ||
			   (i_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_OI_CL_IMPL_SET) ||
			   (i_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_OI_CL_IMPL_REL) ||
			   (i_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_OI_OBJ_IMPL_SET) ||
			   (i_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_OI_OBJ_IMPL_REL)) {
			IMMSV_OCTET_STRING *os =
			    &(i_evt->info.immnd.info.implSet.impl_name);
			if (!immsv_evt_enc_inline_text(__LINE__, o_ub, os)) {
				return NCSCC_RC_OUT_OF_MEM;
			}
		} else if ((i_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_IMM_ADMOP) ||
			   (i_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_IMM_ADMOP_ASYNC)) {
			int depth = 0;
			/*Encode the objectName */
			IMMSV_OCTET_STRING *os =
			    &(i_evt->info.immnd.info.admOpReq.objectName);
			if (!immsv_evt_enc_inline_text(__LINE__, o_ub, os)) {
				return NCSCC_RC_OUT_OF_MEM;
			}

			/*Encode the param list */
			IMMSV_ADMIN_OPERATION_PARAM *op =
			    i_evt->info.immnd.info.admOpReq.params;
			while (op && (depth < IMMSV_MAX_ATTRIBUTES)) {
				immsv_evt_enc_admop_param(o_ub, op);
				op = op->next;
				++depth;
			}
			if (depth >= IMMSV_MAX_ATTRIBUTES) {
				LOG_ER("TOO MANY PARAMS line:%u", __LINE__);
				return NCSCC_RC_OUT_OF_MEM;
			}
		} else if ((i_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_ADMOP_RSP_2) ||
			   (i_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_ASYNC_ADMOP_RSP_2) ||
			   (i_evt->info.immnd.type ==
			    IMMND_EVT_ND2ND_ADMOP_RSP_2) ||
			   (i_evt->info.immnd.type ==
			    IMMND_EVT_ND2ND_ASYNC_ADMOP_RSP_2)) {
			/*Encode the param list */
			int depth = 0;
			IMMSV_ADMIN_OPERATION_PARAM *op =
			    i_evt->info.immnd.info.admOpRsp.parms;
			while (op && (depth < IMMSV_MAX_ATTRIBUTES)) {
				immsv_evt_enc_admop_param(o_ub, op);
				op = op->next;
				++depth;
			}
			if (depth >= IMMSV_MAX_ATTRIBUTES) {
				LOG_ER("TOO MANY PARAMS line:%u", __LINE__);
				return NCSCC_RC_OUT_OF_MEM;
			}
		} else if (i_evt->info.immnd.type ==
			   IMMND_EVT_A2ND_CLASS_DELETE) {
			IMMSV_OCTET_STRING *os =
			    &(i_evt->info.immnd.info.classDescr.className);
			if (!immsv_evt_enc_inline_text(__LINE__, o_ub, os)) {
				return NCSCC_RC_OUT_OF_MEM;
			}
		} else if ((i_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_CLASS_CREATE) ||
			   (i_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_CLASS_DESCR_GET)) {
			int depth = 0;
			IMMSV_OCTET_STRING *os =
			    &(i_evt->info.immnd.info.classDescr.className);

			if (!immsv_evt_enc_inline_text(__LINE__, o_ub, os)) {
				return NCSCC_RC_OUT_OF_MEM;
			}

			/*Encode attribute definitions. */
			IMMSV_ATTR_DEF_LIST *ad =
			    i_evt->info.immnd.info.classDescr.attrDefinitions;
			while (ad && (depth < IMMSV_MAX_ATTRIBUTES)) {
				immsv_evt_enc_attr_def(o_ub, ad);
				ad = ad->next;
				++depth;
			}
			if (depth >= IMMSV_MAX_ATTRIBUTES) {
				LOG_ER("TOO MANY ATTR DEFS line:%u", __LINE__);
				return NCSCC_RC_OUT_OF_MEM;
			}
		} else if ((i_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_OBJ_CREATE) ||
			   (i_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_OI_OBJ_CREATE) ||
			   (i_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_OBJ_CREATE_2) ||
			   (i_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_OI_OBJ_CREATE_2)) {
			int depth = 0;
			/*Encode the className */
			IMMSV_OCTET_STRING *os =
			    &(i_evt->info.immnd.info.objCreate.className);

			if (!immsv_evt_enc_inline_text(__LINE__, o_ub, os)) {
				return NCSCC_RC_OUT_OF_MEM;
			}

			/*Encode the parentName */
			os = &(
			    i_evt->info.immnd.info.objCreate.parentOrObjectDn);
			if (!immsv_evt_enc_inline_text(__LINE__, o_ub, os)) {
				return NCSCC_RC_OUT_OF_MEM;
			}

			/*Encode the list of attributes */
			IMMSV_ATTR_VALUES_LIST *p =
			    i_evt->info.immnd.info.objCreate.attrValues;

			while (p && (depth < IMMSV_MAX_ATTRIBUTES)) {
				immsv_evt_enc_attribute(o_ub, p);
				p = p->next;
				++depth;
			}
			if (depth >= IMMSV_MAX_ATTRIBUTES) {
				LOG_ER("TOO MANY attributes line:%u", __LINE__);
				return NCSCC_RC_OUT_OF_MEM;
			}
		} else if ((i_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_OBJ_MODIFY) ||
			   (i_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_OI_OBJ_MODIFY)) {
			int depth = 0;

			/*Encode the objectName */
			IMMSV_OCTET_STRING *os =
			    &(i_evt->info.immnd.info.objModify.objectName);
			if (!immsv_evt_enc_inline_text(__LINE__, o_ub, os)) {
				return NCSCC_RC_OUT_OF_MEM;
			}

			/*Encode the list of attribute modifications */
			IMMSV_ATTR_MODS_LIST *p =
			    i_evt->info.immnd.info.objModify.attrMods;

			while (p && (depth < IMMSV_MAX_ATTRIBUTES)) {
				immsv_evt_enc_attr_mod(o_ub, p);
				p = p->next;
				++depth;
			}
			if (depth >= IMMSV_MAX_ATTRIBUTES) {
				LOG_ER(
				    "TOO MANY attribute modifications line:%u",
				    __LINE__);
				return NCSCC_RC_OUT_OF_MEM;
			}
		} else if ((i_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_OBJ_DELETE) ||
			   (i_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_OI_OBJ_DELETE)) {
			/*Encode the objectName */
			IMMSV_OCTET_STRING *os =
			    &(i_evt->info.immnd.info.objDelete.objectName);
			if (!immsv_evt_enc_inline_text(__LINE__, o_ub, os)) {
				return NCSCC_RC_OUT_OF_MEM;
			}

		} else if ((i_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_OBJ_SYNC) ||
			   (i_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_OBJ_SYNC_2)) {
			int syncDepth = 0;
			IMMSV_OM_OBJECT_SYNC *obj_sync =
			    &(i_evt->info.immnd.info.obj_sync);
			while (obj_sync) {
				uint8_t *p8;
				int attrDepth = 0;

				++syncDepth;
				if (syncDepth >= IMMSV_MAX_OBJS_IN_SYNCBATCH) {
					LOG_ER(
					    "TOO MANY objects %u >= %u in sync line:%u",
					    syncDepth,
					    IMMSV_MAX_OBJS_IN_SYNCBATCH,
					    __LINE__);
					return NCSCC_RC_OUT_OF_MEM;
				}

				/*Encode the className */
				IMMSV_OCTET_STRING *os = &(obj_sync->className);
				if (!immsv_evt_enc_inline_text(__LINE__, o_ub,
							       os)) {
					return NCSCC_RC_OUT_OF_MEM;
				}

				/*Encode the objecttName */
				os = &(obj_sync->objectName);
				if (!immsv_evt_enc_inline_text(__LINE__, o_ub,
							       os)) {
					return NCSCC_RC_OUT_OF_MEM;
				}

				/*Encode the list of attributes */
				IMMSV_ATTR_VALUES_LIST *p =
				    obj_sync->attrValues;

				while (p &&
				       (attrDepth < IMMSV_MAX_ATTRIBUTES)) {
					immsv_evt_enc_attribute(o_ub, p);
					p = p->next;
					++attrDepth;
				}
				if (attrDepth >= IMMSV_MAX_ATTRIBUTES) {
					LOG_ER("TOO MANY attributes line:%u",
					       __LINE__);
					return NCSCC_RC_OUT_OF_MEM;
				}

				if (i_evt->info.immnd.type ==
				    IMMND_EVT_A2ND_OBJ_SYNC) {
					/* Old OBJ_SYNC messages contain only
					   one object and have no next marker.
					 */
					TRACE("Old OBJ_SYNC send case");
					osafassert(obj_sync->next == NULL);
					break;
				}

				obj_sync = obj_sync->next;
				if (obj_sync) { /* Another object in the sync
						   batch. */
					/* next marker == 1 */
					IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 1);
					ncs_encode_8bit(&p8, 1);
					ncs_enc_claim_space(o_ub, 1);

					IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
					ncs_encode_32bit(
					    &p8, obj_sync->className.size);
					ncs_enc_claim_space(o_ub, 4);

					IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
					ncs_encode_32bit(
					    &p8, obj_sync->objectName.size);
					ncs_enc_claim_space(o_ub, 4);

					IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 1);
					ncs_encode_8bit(
					    &p8,
					    (obj_sync->attrValues) ? 0x1 : 0x0);
					ncs_enc_claim_space(o_ub, 1);
				} else { /* End of the sync batch */
					/* next marker == 0 */
					IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 1);
					ncs_encode_8bit(&p8, 0x0);
					ncs_enc_claim_space(o_ub, 1);
				}
			}
			if (syncDepth > 20) {
				TRACE("syncDepth > 20 :%u", syncDepth);
			}
		} else if ((i_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_ADMO_SET) ||
			   (i_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_ADMO_RELEASE) ||
			   (i_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_ADMO_CLEAR)) {
			return immsv_evt_enc_name_list(
			    o_ub, i_evt->info.immnd.info.admReq.objectNames);
		} else if ((i_evt->info.immnd.type ==
			    IMMND_EVT_ND2ND_SYNC_FINALIZE) ||
			   (i_evt->info.immnd.type ==
			    IMMND_EVT_ND2ND_SYNC_FINALIZE_2)) {
			uint8_t *p8;

			IMMSV_ADMO_LIST *p =
			    i_evt->info.immnd.info.finSync.adminOwners;
			while (p) {
				if (immsv_evt_enc_admo(o_ub, p) !=
				    NCSCC_RC_SUCCESS) {
					return NCSCC_RC_OUT_OF_MEM;
				}
				p = p->next;
			}

			IMMSV_IMPL_LIST *q =
			    i_evt->info.immnd.info.finSync.implementers;

			while (q) {
				immsv_evt_enc_impl(o_ub, q);
				q = q->next;
			}

			IMMSV_CLASS_LIST *r =
			    i_evt->info.immnd.info.finSync.classes;
			while (r) {
				immsv_evt_enc_class(o_ub, r);
				r = r->next;
			}

			if (i_evt->info.immnd.type ==
			    IMMND_EVT_ND2ND_SYNC_FINALIZE_2) {
				TRACE(
				    "Encoding SUB level for IMMND_EVT_ND2ND_SYNC_FINALIZE_2");
				IMMSV_CCB_OUTCOME_LIST *ol =
				    i_evt->info.immnd.info.finSync.ccbResults;
				/*Encode start marker*/
				IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 1);
				ncs_encode_8bit(&p8, ol ? 1 : 0);
				ncs_enc_claim_space(o_ub, 1);

				while (ol) {
					IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
					ncs_encode_32bit(&p8, ol->ccbId);
					ncs_enc_claim_space(o_ub, 4);

					IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
					ncs_encode_32bit(&p8, ol->ccbState);
					ncs_enc_claim_space(o_ub, 4);

					ol = ol->next;
					IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 1);
					ncs_encode_8bit(&p8, (ol) ? 1 : 0);
					ncs_enc_claim_space(o_ub, 1);
				}
			}
		} else if ((i_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_SEARCHINIT) ||
			   (i_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_ACCESSOR_GET) ||
			   (i_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_OBJ_SAFE_READ)) {
			int depth = 0;
			/*Encode the rootName */
			IMMSV_OCTET_STRING *os =
			    &(i_evt->info.immnd.info.searchInit.rootName);
			if (!immsv_evt_enc_inline_text(__LINE__, o_ub, os)) {
				return NCSCC_RC_OUT_OF_MEM;
			}

			if (i_evt->info.immnd.info.searchInit.searchParam
				    .present ==
				ImmOmSearchParameter_PR_oneAttrParam ||
			    i_evt->info.immnd.info.searchInit.searchParam
				    .present ==
				ImmOmSearchParameter_PR_nonExtendedName_oneAttrParam) {
				os = &(i_evt->info.immnd.info.searchInit
					   .searchParam.choice.oneAttrParam
					   .attrName);
				if (!immsv_evt_enc_inline_text(__LINE__, o_ub,
							       os)) {
					return NCSCC_RC_OUT_OF_MEM;
				}
				immsv_evt_enc_att_val(
				    o_ub,
				    &(i_evt->info.immnd.info.searchInit
					  .searchParam.choice.oneAttrParam
					  .attrValue),
				    i_evt->info.immnd.info.searchInit
					.searchParam.choice.oneAttrParam
					.attrValueType);
			} else {
				osafassert(
				    i_evt->info.immnd.info.searchInit
					    .searchParam.present ==
					ImmOmSearchParameter_PR_NOTHING ||
				    i_evt->info.immnd.info.searchInit
					    .searchParam.present ==
					ImmOmSearchParameter_PR_nonExtendedName_NOTHING);
			}

			/*Encode the list of attribute names */
			IMMSV_ATTR_NAME_LIST *p =
			    i_evt->info.immnd.info.searchInit.attributeNames;

			while (p && (depth < IMMSV_MAX_ATTRIBUTES)) {
				immsv_evt_enc_attrName(o_ub, p);
				p = p->next;
				++depth;
			}

			if (depth >= IMMSV_MAX_ATTRIBUTES) {
				LOG_ER("TOO MANY attribute names line:%u",
				       __LINE__);
				return NCSCC_RC_OUT_OF_MEM;
			}
		} else if (i_evt->info.immnd.type ==
			   IMMND_EVT_A2ND_RT_ATT_UPPD_RSP) {
			int depth = 0;
			/*Encode the objectName */
			IMMSV_OCTET_STRING *os =
			    &(i_evt->info.immnd.info.rtAttUpdRpl.sr.objectName);
			if (!immsv_evt_enc_inline_text(__LINE__, o_ub, os)) {
				return NCSCC_RC_OUT_OF_MEM;
			}

			/*Encode the list of attribute names */
			IMMSV_ATTR_NAME_LIST *p =
			    i_evt->info.immnd.info.rtAttUpdRpl.sr
				.attributeNames;

			while (p && (depth < IMMSV_MAX_ATTRIBUTES)) {
				immsv_evt_enc_attrName(o_ub, p);
				p = p->next;
				++depth;
			}

			if (depth >= IMMSV_MAX_ATTRIBUTES) {
				LOG_ER("TOO MANY attribute names line:%u",
				       __LINE__);
				return NCSCC_RC_OUT_OF_MEM;
			}
		} else if (i_evt->info.immnd.type ==
			   IMMND_EVT_ND2ND_SEARCH_REMOTE) {
			int depth = 0;
			/*Encode the objectName */
			IMMSV_OCTET_STRING *os =
			    &(i_evt->info.immnd.info.searchRemote.objectName);
			if (!immsv_evt_enc_inline_text(__LINE__, o_ub, os)) {
				return NCSCC_RC_OUT_OF_MEM;
			}

			/*Encode the list of attribute names */
			IMMSV_ATTR_NAME_LIST *p =
			    i_evt->info.immnd.info.searchRemote.attributeNames;

			while (p && (depth < IMMSV_MAX_ATTRIBUTES)) {
				immsv_evt_enc_attrName(o_ub, p);
				p = p->next;
				++depth;
			}

			if (depth >= IMMSV_MAX_ATTRIBUTES) {
				LOG_ER("TOO MANY attribute names line:%u",
				       __LINE__);
				return NCSCC_RC_OUT_OF_MEM;
			}
		} else if (i_evt->info.immnd.type ==
			   IMMND_EVT_ND2ND_SEARCH_REMOTE_RSP) {
			uint8_t *p8;
			int depth = 0;
			/*Encode searchRemote response */
			IMMSV_OM_RSP_SEARCH_REMOTE *sr =
			    &(i_evt->info.immnd.info.rspSrchRmte);
			IMMSV_OM_RSP_SEARCH_NEXT *sn = &(sr->runtimeAttrs);

			IMMSV_OCTET_STRING *os = &(sn->objectName);
			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8, os->size);
			ncs_enc_claim_space(o_ub, 4);
			if (!immsv_evt_enc_inline_text(__LINE__, o_ub, os)) {
				return NCSCC_RC_OUT_OF_MEM;
			}

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 1);
			ncs_encode_8bit(&p8, (sn->attrValuesList) ? 1 : 0);
			ncs_enc_claim_space(o_ub, 1);

			if (sn->attrValuesList) {
				IMMSV_ATTR_VALUES_LIST *al = sn->attrValuesList;
				do {
					immsv_evt_enc_attribute(o_ub, al);
					al = al->next;
					++depth;
				} while (al && (depth < IMMSV_MAX_ATTRIBUTES));
				if (depth >= IMMSV_MAX_ATTRIBUTES) {
					LOG_ER(
					    "Search next: TOO MANY ATTR DEFS line:%u",
					    __LINE__);
					return NCSCC_RC_OUT_OF_MEM;
				}
			}
			/*
			   else
			   { No attrValues }
			 */
		} else if ((i_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_CCB_COMPLETED_RSP_2) ||
			   (i_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_CCB_OBJ_MODIFY_RSP_2) ||
			   (i_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_CCB_OBJ_CREATE_RSP_2) ||
			   (i_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_CCB_OBJ_DELETE_RSP_2)) {
			IMMSV_OCTET_STRING *os =
			    &(i_evt->info.immnd.info.ccbUpcallRsp.errorString);
			if (!immsv_evt_enc_inline_text(__LINE__, o_ub, os)) {
				return NCSCC_RC_OUT_OF_MEM;
			}
		} else if (i_evt->info.immnd.type == IMMND_EVT_D2ND_IMPLDELETE) {
			uint32_t length;
			uint8_t *p8;
			IMMSV_IMPLDELETE *impl_delete =
					&i_evt->info.immnd.info.impl_delete;

			for(SaUint32T i=0; i<impl_delete->size; ++i) {
				length = strnlen(impl_delete->implNameList[i].buf,
								impl_delete->implNameList[i].size);

				IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
				ncs_encode_32bit(&p8, length);
				ncs_enc_claim_space(o_ub, 4);

				if (ncs_encode_n_octets_in_uba(o_ub,
						(uint8_t *)impl_delete->implNameList[i].buf,
						length) != NCSCC_RC_SUCCESS) {
					LOG_WA(
					    "Failure inside ncs_encode_n_octets_in_uba");
					return NCSCC_RC_FAILURE;
				}
			}
		}
	}

	return NCSCC_RC_SUCCESS;
}

/****************************************************************************\
 PROCEDURE NAME : immsv_evt_dec_sublevel

 DESCRIPTION    : Decodes the sublevels of IMMSV_EVT from user buf
		  Inverse of immsv_evt_enc_sublevel.

 ARGUMENTS      : *i_evt - Event Struct.
		  *o_ub  - User Buff.

 RETURNS        : None

 NOTES          :
\*****************************************************************************/
static uint32_t immsv_evt_dec_sublevels(NCS_UBAID *i_ub, IMMSV_EVT *o_evt)
{
	/* Decode the internal pointers */
	if (o_evt->type == IMMSV_EVT_TYPE_IMMA) {
		if (o_evt->info.imma.type ==
		    IMMA_EVT_ND2A_CLASS_DESCR_GET_RSP) {
			/*Decode attribute definitons */
			IMMSV_ATTR_DEF_LIST *ad =
			    o_evt->info.imma.info.classDescr.attrDefinitions;
			if (ad) {
				immsv_evt_dec_attr_def(i_ub, &ad);
				o_evt->info.imma.info.classDescr
				    .attrDefinitions = ad;
			}
		} else if (o_evt->info.imma.type ==
			   IMMA_EVT_ND2A_SEARCHBUNDLENEXT_RSP) {
			uint32_t i;
			SaUint32T resultSize;
			uint8_t *p8;
			uint8_t c8;
			uint8_t local_data[8];
			IMMSV_OM_RSP_SEARCH_BUNDLE_NEXT *bsn;
			IMMSV_OCTET_STRING *os;
			IMMSV_ATTR_VALUES_LIST *p;

			/*Decode searchBundleNext Response */
			bsn = o_evt->info.imma.info.searchBundleNextRsp =
			    calloc(1, sizeof(IMMSV_OM_RSP_SEARCH_BUNDLE_NEXT));

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			resultSize = bsn->resultSize = ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			bsn->searchResult = calloc(
			    resultSize, sizeof(IMMSV_OM_RSP_SEARCH_NEXT *));

			for (i = 0; i < bsn->resultSize; i++) {
				bsn->searchResult[i] =
				    calloc(1, sizeof(IMMSV_OM_RSP_SEARCH_NEXT));

				/* Decode object name */
				os = &(bsn->searchResult[i]->objectName);
				IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub,
							4);
				os->size = ncs_decode_32bit(&p8);
				ncs_dec_skip_space(i_ub, 4);
				immsv_evt_dec_inline_string(i_ub, os);

				IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub,
							1);
				c8 = ncs_decode_8bit(&p8);
				ncs_dec_skip_space(i_ub, 1);
				if (c8) {
					/*Decode the list of attributes */
					p = NULL;
					immsv_evt_dec_attributes(i_ub, &p);
					bsn->searchResult[i]->attrValuesList =
					    p;
				} else {
					bsn->searchResult[i]->attrValuesList =
					    NULL;
				}
			}
		} else if ((o_evt->info.imma.type ==
			    IMMA_EVT_ND2A_SEARCHNEXT_RSP) ||
			   (o_evt->info.imma.type ==
			    IMMA_EVT_ND2A_ACCESSOR_GET_RSP)) {
			uint8_t *p8;
			uint8_t c8;
			uint8_t local_data[8];

			/*Decode searchNext Response */
			o_evt->info.imma.info.searchNextRsp =
			    calloc(1, sizeof(IMMSV_OM_RSP_SEARCH_NEXT));

			/* Decode object name */
			IMMSV_OCTET_STRING *os =
			    &(o_evt->info.imma.info.searchNextRsp->objectName);
			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			os->size = ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);
			immsv_evt_dec_inline_string(i_ub, os);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 1);
			c8 = ncs_decode_8bit(&p8);
			ncs_dec_skip_space(i_ub, 1);
			if (c8) {
				/*Decode the list of attributes */
				IMMSV_ATTR_VALUES_LIST *p = NULL;
				immsv_evt_dec_attributes(i_ub, &p);
				o_evt->info.imma.info.searchNextRsp
				    ->attrValuesList = p;
			} else {
				o_evt->info.imma.info.searchNextRsp
				    ->attrValuesList = NULL;
			}
		} else if ((o_evt->info.imma.type ==
			    IMMA_EVT_ND2A_OI_OBJ_CREATE_UC) ||
			   (o_evt->info.imma.type ==
			    IMMA_EVT_ND2A_OI_OBJ_CREATE_LONG_UC)) {
			/*Decode the className */
			IMMSV_OCTET_STRING *os =
			    &(o_evt->info.imma.info.objCreate.className);
			immsv_evt_dec_inline_string(i_ub, os);

			/*Decode the parentName */
			os =
			    &(o_evt->info.imma.info.objCreate.parentOrObjectDn);
			immsv_evt_dec_inline_string(i_ub, os);

			/*Decode the list of attributes */
			IMMSV_ATTR_VALUES_LIST *p =
			    o_evt->info.imma.info.objCreate.attrValues;
			if (p) {
				immsv_evt_dec_attributes(i_ub, &p);
				o_evt->info.imma.info.objCreate.attrValues = p;
			}
		} else if ((o_evt->info.imma.type ==
			    IMMA_EVT_ND2A_OI_OBJ_MODIFY_UC) ||
			   (o_evt->info.imma.type ==
			    IMMA_EVT_ND2A_OI_OBJ_MODIFY_LONG_UC)) {
			/*Decode the objectName */
			IMMSV_OCTET_STRING *os =
			    &(o_evt->info.imma.info.objModify.objectName);
			immsv_evt_dec_inline_string(i_ub, os);

			/*Decode the list of attribute modifications */
			IMMSV_ATTR_MODS_LIST *p =
			    o_evt->info.imma.info.objModify.attrMods;
			if (p) {
				immsv_evt_dec_attrmods(i_ub, &p);
				o_evt->info.imma.info.objModify.attrMods = p;
			}
		} else if ((o_evt->info.imma.type ==
			    IMMA_EVT_ND2A_OI_OBJ_DELETE_UC) ||
			   (o_evt->info.imma.type ==
			    IMMA_EVT_ND2A_OI_OBJ_DELETE_LONG_UC)) {
			/*Decode the objectName */
			IMMSV_OCTET_STRING *os =
			    &(o_evt->info.imma.info.objDelete.objectName);
			immsv_evt_dec_inline_string(i_ub, os);

		} else if ((o_evt->info.imma.type == IMMA_EVT_ND2A_IMM_ADMOP) ||
			   (o_evt->info.imma.type ==
			    IMMA_EVT_ND2A_IMM_PBE_ADMOP)) {
			/*Decode the objectName */
			IMMSV_OCTET_STRING *os =
			    &(o_evt->info.imma.info.admOpReq.objectName);
			immsv_evt_dec_inline_string(i_ub, os);

			/*Decode the param list */
			IMMSV_ADMIN_OPERATION_PARAM *op =
			    o_evt->info.imma.info.admOpReq.params;
			if (op) {
				immsv_evt_dec_admop_param(i_ub, &op);
				o_evt->info.imma.info.admOpReq.params = op;
			}
		} else if (o_evt->info.imma.type == IMMA_EVT_ND2A_ADMOP_RSP_2) {
			/*Decode the param list */
			IMMSV_ADMIN_OPERATION_PARAM *op =
			    o_evt->info.imma.info.admOpRsp.parms;
			if (op) {
				immsv_evt_dec_admop_param(i_ub, &op);
				o_evt->info.imma.info.admOpRsp.parms = op;
			}
		} else if (o_evt->info.imma.type ==
			   IMMA_EVT_ND2A_SEARCH_REMOTE) {
			/*Decode the objectName */
			IMMSV_OCTET_STRING *os =
			    &(o_evt->info.imma.info.searchRemote.objectName);
			immsv_evt_dec_inline_string(i_ub, os);

			/*Decode the list of attribute names */
			IMMSV_ATTR_NAME_LIST *p =
			    o_evt->info.imma.info.searchRemote.attributeNames;
			if (p) {
				immsv_evt_dec_attrNames(i_ub, &p);
				o_evt->info.imma.info.searchRemote
				    .attributeNames = p;
			}
		} else if (o_evt->info.imma.type == IMMA_EVT_ND2A_IMM_ERROR_2) {
			IMMSV_ATTR_NAME_LIST *p =
			    o_evt->info.imma.info.errRsp.errStrings;
			if (p) {
				immsv_evt_dec_attrNames(i_ub, &p);
				o_evt->info.imma.info.errRsp.errStrings = p;
			}
		}
	} else if (o_evt->type == IMMSV_EVT_TYPE_IMMD) {
		if ((o_evt->info.immd.type == IMMD_EVT_ND2D_FEVS_REQ) ||
		    (o_evt->info.immd.type == IMMD_EVT_ND2D_FEVS_REQ_2)) {
			IMMSV_OCTET_STRING *os =
			    &(o_evt->info.immd.info.fevsReq.msg);
			immsv_evt_dec_inline_string(i_ub, os);
		} else if ((o_evt->info.immd.type ==
			    IMMD_EVT_ND2D_IMPLSET_REQ) ||
			   (o_evt->info.immd.type ==
			    IMMD_EVT_ND2D_IMPLSET_REQ_2)) {
			IMMSV_OCTET_STRING *os =
			    &(o_evt->info.immd.info.impl_set.r.impl_name);
			immsv_evt_dec_inline_string(i_ub, os);
		} else if (o_evt->info.immd.type ==
			   IMMD_EVT_ND2D_OI_OBJ_MODIFY) {

			/*Decode the objectName */
			IMMSV_OCTET_STRING *os =
			    &(o_evt->info.immd.info.objModify.objectName);
			immsv_evt_dec_inline_string(i_ub, os);

			/*Decode the list of attribute modifications */
			IMMSV_ATTR_MODS_LIST *p =
			    o_evt->info.immd.info.objModify.attrMods;
			if (p) {
				immsv_evt_dec_attrmods(i_ub, &p);
				o_evt->info.immd.info.objModify.attrMods = p;
			}
		} else if (o_evt->info.immd.type == IMMD_EVT_ND2D_IMPLDELETE) {
			uint8_t *p8;
			uint8_t local_data[8];
			IMMSV_OCTET_STRING *implNameList;
			IMMSV_IMPLDELETE *impl_delete = &o_evt->info.immd.info.impl_delete;

			impl_delete->implNameList = (IMMSV_OCTET_STRING *)calloc(1,
					impl_delete->size * sizeof(IMMSV_OCTET_STRING));
			implNameList = impl_delete->implNameList;
			for(SaUint32T i=0; i<impl_delete->size; ++i) {
				IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
				implNameList[i].size = ncs_decode_32bit(&p8);
				ncs_dec_skip_space(i_ub, 4);

				implNameList[i].buf = (char *)malloc(
						implNameList[i].size + 1);
				if (implNameList[i].buf == NULL ||
						ncs_decode_n_octets_from_uba(i_ub,
								(uint8_t *)implNameList[i].buf,
								implNameList[i].size) != NCSCC_RC_SUCCESS) {
					LOG_WA("Failure inside ncs_decode_n_octets_from_uba");
					return NCSCC_RC_FAILURE;
				}
				implNameList[i].buf[implNameList[i].size] = 0;
			}
		} else if ((o_evt->info.immd.info.ctrl_msg.pbeEnabled >= 3) &&
			   (o_evt->info.immd.type ==
			    IMMD_EVT_ND2D_INTRO)) { /* extended intro */

			IMMSV_OCTET_STRING *os =
			    &(o_evt->info.immd.info.ctrl_msg.dir);
			immsv_evt_dec_inline_string(i_ub, os);

			os = &(o_evt->info.immd.info.ctrl_msg.xmlFile);
			immsv_evt_dec_inline_string(i_ub, os);

			os = &(o_evt->info.immd.info.ctrl_msg.pbeFile);
			immsv_evt_dec_inline_string(i_ub, os);
		}
	} else if (o_evt->type == IMMSV_EVT_TYPE_IMMND) {
		if ((o_evt->info.immnd.type == IMMND_EVT_A2ND_IMM_FEVS) ||
		    (o_evt->info.immnd.type == IMMND_EVT_D2ND_GLOB_FEVS_REQ) ||
		    (o_evt->info.immnd.type == IMMND_EVT_A2ND_IMM_FEVS_2) ||
		    (o_evt->info.immnd.type ==
		     IMMND_EVT_D2ND_GLOB_FEVS_REQ_2)) {
			IMMSV_OCTET_STRING *os =
			    &(o_evt->info.immnd.info.fevsReq.msg);
			immsv_evt_dec_inline_string(i_ub, os);
		} else if ((o_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_OI_IMPL_SET) ||
			   (o_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_OI_IMPL_SET_2) ||
			   (o_evt->info.immnd.type ==
			    IMMND_EVT_D2ND_IMPLSET_RSP) ||
			   (o_evt->info.immnd.type ==
			    IMMND_EVT_D2ND_IMPLSET_RSP_2) ||
			   (o_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_OI_CL_IMPL_SET) ||
			   (o_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_OI_CL_IMPL_REL) ||
			   (o_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_OI_OBJ_IMPL_SET) ||
			   (o_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_OI_OBJ_IMPL_REL)) {
			IMMSV_OCTET_STRING *os =
			    &(o_evt->info.immnd.info.implSet.impl_name);
			immsv_evt_dec_inline_string(i_ub, os);
		} else if ((o_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_IMM_ADMOP) ||
			   (o_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_IMM_ADMOP_ASYNC)) {
			/*Decode the objectName */
			IMMSV_OCTET_STRING *os =
			    &(o_evt->info.immnd.info.admOpReq.objectName);
			immsv_evt_dec_inline_string(i_ub, os);

			/*Decode the param list */
			IMMSV_ADMIN_OPERATION_PARAM *op =
			    o_evt->info.immnd.info.admOpReq.params;
			if (op) {
				immsv_evt_dec_admop_param(i_ub, &op);
				o_evt->info.immnd.info.admOpReq.params = op;
			}
		} else if ((o_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_ADMOP_RSP_2) ||
			   (o_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_ASYNC_ADMOP_RSP_2) ||
			   (o_evt->info.immnd.type ==
			    IMMND_EVT_ND2ND_ADMOP_RSP_2) ||
			   (o_evt->info.immnd.type ==
			    IMMND_EVT_ND2ND_ASYNC_ADMOP_RSP_2)) {
			/*Decode the param list */
			IMMSV_ADMIN_OPERATION_PARAM *op =
			    o_evt->info.immnd.info.admOpRsp.parms;
			if (op) {
				immsv_evt_dec_admop_param(i_ub, &op);
				o_evt->info.immnd.info.admOpRsp.parms = op;
			}
		} else if (o_evt->info.immnd.type ==
			   IMMND_EVT_A2ND_CLASS_DELETE) {
			/*Decode the className */
			IMMSV_OCTET_STRING *os =
			    &(o_evt->info.immnd.info.classDescr.className);
			immsv_evt_dec_inline_string(i_ub, os);
		} else if ((o_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_CLASS_CREATE) ||
			   (o_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_CLASS_DESCR_GET)) {
			/*Decode the className */
			IMMSV_OCTET_STRING *os =
			    &(o_evt->info.immnd.info.classDescr.className);
			immsv_evt_dec_inline_string(i_ub, os);

			/*Decode attribute definitons */
			IMMSV_ATTR_DEF_LIST *ad =
			    o_evt->info.immnd.info.classDescr.attrDefinitions;
			if (ad) {
				immsv_evt_dec_attr_def(i_ub, &ad);
				o_evt->info.immnd.info.classDescr
				    .attrDefinitions = ad;
			}
		} else if ((o_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_OBJ_CREATE) ||
			   (o_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_OI_OBJ_CREATE) ||
			   (o_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_OBJ_CREATE_2) ||
			   (o_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_OI_OBJ_CREATE_2)) {
			/*Decode the className */
			IMMSV_OCTET_STRING *os =
			    &(o_evt->info.immnd.info.objCreate.className);
			immsv_evt_dec_inline_string(i_ub, os);

			os = &(
			    o_evt->info.immnd.info.objCreate.parentOrObjectDn);
			immsv_evt_dec_inline_string(i_ub, os);

			/*Decode the list of attributes */
			IMMSV_ATTR_VALUES_LIST *p =
			    o_evt->info.immnd.info.objCreate.attrValues;
			if (p) {
				immsv_evt_dec_attributes(i_ub, &p);
				o_evt->info.immnd.info.objCreate.attrValues = p;
			}
		} else if ((o_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_OBJ_MODIFY) ||
			   (o_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_OI_OBJ_MODIFY)) {
			/*Decode the objectName */
			IMMSV_OCTET_STRING *os =
			    &(o_evt->info.immnd.info.objModify.objectName);
			immsv_evt_dec_inline_string(i_ub, os);

			/*Decode the list of attribute modifications */
			IMMSV_ATTR_MODS_LIST *p =
			    o_evt->info.immnd.info.objModify.attrMods;
			if (p) {
				immsv_evt_dec_attrmods(i_ub, &p);
				o_evt->info.immnd.info.objModify.attrMods = p;
			}
		} else if ((o_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_OBJ_DELETE) ||
			   (o_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_OI_OBJ_DELETE)) {
			/*Decode the objectName */
			IMMSV_OCTET_STRING *os =
			    &(o_evt->info.immnd.info.objDelete.objectName);
			immsv_evt_dec_inline_string(i_ub, os);
		} else if ((o_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_OBJ_SYNC) ||
			   (o_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_OBJ_SYNC_2)) {
			uint8_t *p8;
			uint8_t local_data[8];
			int syncDepth = 0;
			IMMSV_OM_OBJECT_SYNC *obj_sync =
			    &(o_evt->info.immnd.info.obj_sync);
			while (obj_sync) {
				++syncDepth;
				if (syncDepth >= IMMSV_MAX_OBJS_IN_SYNCBATCH) {
					LOG_ER(
					    "TOO MANY objects %u >= %u in sync line:%u",
					    syncDepth,
					    IMMSV_MAX_OBJS_IN_SYNCBATCH,
					    __LINE__);
					return NCSCC_RC_OUT_OF_MEM;
				}

				/*Decode the className */
				IMMSV_OCTET_STRING *os = &(obj_sync->className);
				immsv_evt_dec_inline_string(i_ub, os);

				/*Decode the objectName */
				os = &(obj_sync->objectName);
				immsv_evt_dec_inline_string(i_ub, os);

				/*Decode the list of attributes */
				IMMSV_ATTR_VALUES_LIST *p =
				    obj_sync->attrValues;
				if (p) {
					immsv_evt_dec_attributes(i_ub, &p);
					obj_sync->attrValues = p;
				}

				if (o_evt->info.immnd.type ==
				    IMMND_EVT_A2ND_OBJ_SYNC) {
					/* Old OBJ_SYNC messages contain only
					   one object and have no next marker.
					 */
					TRACE("Old OBJ_SYNC receive case");
					obj_sync->next = NULL;
					break;
				}

				/* IMMND_EVT_A2ND_OBJ_SYNC_2 => possible batch.
				 */

				IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub,
							1);
				if (ncs_decode_8bit(&p8)) {
					obj_sync->next = calloc(
					    1, sizeof(IMMSV_OM_OBJECT_SYNC));
				}
				ncs_dec_skip_space(i_ub, 1);

				if (obj_sync->next) {
					IMMSV_FLTN_SPACE_ASSERT(p8, local_data,
								i_ub, 4);
					obj_sync->next->className.size =
					    ncs_decode_32bit(&p8);
					ncs_dec_skip_space(i_ub, 4);

					IMMSV_FLTN_SPACE_ASSERT(p8, local_data,
								i_ub, 4);
					obj_sync->next->objectName.size =
					    ncs_decode_32bit(&p8);
					ncs_dec_skip_space(i_ub, 4);

					IMMSV_FLTN_SPACE_ASSERT(p8, local_data,
								i_ub, 1);
					if (ncs_decode_8bit(&p8)) {
						obj_sync->next->attrValues =
						    (void *)0x1;
					}
					ncs_dec_skip_space(i_ub, 1);
				}
				obj_sync = obj_sync->next;
			}
		} else if ((o_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_ADMO_SET) ||
			   (o_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_ADMO_RELEASE) ||
			   (o_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_ADMO_CLEAR)) {
			immsv_evt_dec_name_list(
			    i_ub, &(o_evt->info.immnd.info.admReq.objectNames));
		} else if ((o_evt->info.immnd.type ==
			    IMMND_EVT_ND2ND_SYNC_FINALIZE) ||
			   (o_evt->info.immnd.type ==
			    IMMND_EVT_ND2ND_SYNC_FINALIZE_2)) {
			IMMSV_ADMO_LIST *p =
			    o_evt->info.immnd.info.finSync.adminOwners;
			if (p) {
				immsv_evt_dec_admo(i_ub, &p);
				o_evt->info.immnd.info.finSync.adminOwners = p;
			}

			IMMSV_IMPL_LIST *q =
			    o_evt->info.immnd.info.finSync.implementers;
			if (q) {
				immsv_evt_dec_impl(i_ub, &q);
				o_evt->info.immnd.info.finSync.implementers = q;
			}

			IMMSV_CLASS_LIST *r =
			    o_evt->info.immnd.info.finSync.classes;
			if (r) {
				immsv_evt_dec_class(i_ub, &r);
				o_evt->info.immnd.info.finSync.classes = r;
			}

			if (o_evt->info.immnd.type ==
			    IMMND_EVT_ND2ND_SYNC_FINALIZE_2) {
				uint8_t *p8;
				uint8_t local_data[8];
				uint8_t c8;
				TRACE(
				    "Decoding SUB level for IMMND_EVT_ND2ND_SYNC_FINALIZE_2");
				o_evt->info.immnd.info.finSync.ccbResults =
				    NULL;

				IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub,
							1);
				c8 = ncs_decode_8bit(&p8);
				ncs_dec_skip_space(i_ub, 1);

				while (c8) {
					IMMSV_CCB_OUTCOME_LIST *ol =
					    (IMMSV_CCB_OUTCOME_LIST *)calloc(
						1,
						sizeof(IMMSV_CCB_OUTCOME_LIST));

					IMMSV_FLTN_SPACE_ASSERT(p8, local_data,
								i_ub, 4);
					ol->ccbId = ncs_decode_32bit(&p8);
					ncs_dec_skip_space(i_ub, 4);

					IMMSV_FLTN_SPACE_ASSERT(p8, local_data,
								i_ub, 4);
					ol->ccbState = ncs_decode_32bit(&p8);
					ncs_dec_skip_space(i_ub, 4);

					ol->next = o_evt->info.immnd.info
						       .finSync.ccbResults;
					o_evt->info.immnd.info.finSync
					    .ccbResults = ol;

					IMMSV_FLTN_SPACE_ASSERT(p8, local_data,
								i_ub, 1);
					c8 = ncs_decode_8bit(&p8);
					ncs_dec_skip_space(i_ub, 1);
				}
			}
		} else if ((o_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_SEARCHINIT) ||
			   (o_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_ACCESSOR_GET) ||
			   (o_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_OBJ_SAFE_READ)) {
			/*Decode the rootName */
			IMMSV_OCTET_STRING *os =
			    &(o_evt->info.immnd.info.searchInit.rootName);
			if (os->size) {
				immsv_evt_dec_inline_string(i_ub, os);
			}

			if (o_evt->info.immnd.info.searchInit.searchParam
				    .present ==
				ImmOmSearchParameter_PR_oneAttrParam ||
			    o_evt->info.immnd.info.searchInit.searchParam
				    .present ==
				ImmOmSearchParameter_PR_nonExtendedName_oneAttrParam) {
				os = &(o_evt->info.immnd.info.searchInit
					   .searchParam.choice.oneAttrParam
					   .attrName);
				if (os->size) {
					immsv_evt_dec_inline_string(i_ub, os);
				}
				immsv_evt_dec_att_val(
				    i_ub,
				    &(o_evt->info.immnd.info.searchInit
					  .searchParam.choice.oneAttrParam
					  .attrValue),
				    o_evt->info.immnd.info.searchInit
					.searchParam.choice.oneAttrParam
					.attrValueType);
			} else {
				if (o_evt->info.immnd.info.searchInit
					    .searchParam.present !=
					ImmOmSearchParameter_PR_NOTHING &&
				    o_evt->info.immnd.info.searchInit
					    .searchParam.present !=
					ImmOmSearchParameter_PR_nonExtendedName_NOTHING) {
					LOG_ER(
					    "DECODE IMMND_EVT_A2ND_SEARCHINIT WRONG ENUM VALUE");
				}
			}

			/*Decode the list of attribute names */
			IMMSV_ATTR_NAME_LIST *p =
			    o_evt->info.immnd.info.searchInit.attributeNames;
			if (p) {
				immsv_evt_dec_attrNames(i_ub, &p);
				o_evt->info.immnd.info.searchInit
				    .attributeNames = p;
			}
		} else if (o_evt->info.immnd.type ==
			   IMMND_EVT_A2ND_RT_ATT_UPPD_RSP) {
			/*Decode the objectName */
			IMMSV_OCTET_STRING *os =
			    &(o_evt->info.immnd.info.rtAttUpdRpl.sr.objectName);
			immsv_evt_dec_inline_string(i_ub, os);

			/*Decode the list of attribute names */
			IMMSV_ATTR_NAME_LIST *p =
			    o_evt->info.immnd.info.rtAttUpdRpl.sr
				.attributeNames;
			if (p) {
				immsv_evt_dec_attrNames(i_ub, &p);
				o_evt->info.immnd.info.rtAttUpdRpl.sr
				    .attributeNames = p;
			}
		} else if (o_evt->info.immnd.type ==
			   IMMND_EVT_ND2ND_SEARCH_REMOTE) {
			/*Decode the objectName */
			IMMSV_OCTET_STRING *os =
			    &(o_evt->info.immnd.info.searchRemote.objectName);
			immsv_evt_dec_inline_string(i_ub, os);

			/*Decode the list of attribute names */
			IMMSV_ATTR_NAME_LIST *p =
			    o_evt->info.immnd.info.searchRemote.attributeNames;
			if (p) {
				immsv_evt_dec_attrNames(i_ub, &p);
				o_evt->info.immnd.info.searchRemote
				    .attributeNames = p;
			}
		} else if (o_evt->info.immnd.type ==
			   IMMND_EVT_ND2ND_SEARCH_REMOTE_RSP) {
			uint8_t *p8;
			uint8_t c8;
			uint8_t local_data[8];

			/*Decode searchRemote Response */

			/* Decode object name */
			IMMSV_OCTET_STRING *os =
			    &(o_evt->info.immnd.info.rspSrchRmte.runtimeAttrs
				  .objectName);
			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			os->size = ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);
			immsv_evt_dec_inline_string(i_ub, os);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 1);
			c8 = ncs_decode_8bit(&p8);
			ncs_dec_skip_space(i_ub, 1);
			if (c8) {
				/*Decode the list of attributes */
				IMMSV_ATTR_VALUES_LIST *p = NULL;
				immsv_evt_dec_attributes(i_ub, &p);
				o_evt->info.immnd.info.rspSrchRmte.runtimeAttrs
				    .attrValuesList = p;
			} else {
				o_evt->info.immnd.info.rspSrchRmte.runtimeAttrs
				    .attrValuesList = NULL;
			}
		} else if ((o_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_CCB_COMPLETED_RSP_2) ||
			   (o_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_CCB_OBJ_MODIFY_RSP_2) ||
			   (o_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_CCB_OBJ_CREATE_RSP_2) ||
			   (o_evt->info.immnd.type ==
			    IMMND_EVT_A2ND_CCB_OBJ_DELETE_RSP_2)) {
			IMMSV_OCTET_STRING *os =
			    &(o_evt->info.immnd.info.ccbUpcallRsp.errorString);
			immsv_evt_dec_inline_string(i_ub, os);
		} else if (o_evt->info.immnd.type == IMMND_EVT_D2ND_IMPLDELETE) {
			uint8_t *p8;
			uint8_t local_data[8];
			IMMSV_OCTET_STRING *implNameList;
			IMMSV_IMPLDELETE *impl_delete;

			impl_delete = &o_evt->info.immnd.info.impl_delete;
			impl_delete->implNameList = (IMMSV_OCTET_STRING *)calloc(1,
							impl_delete->size * sizeof(IMMSV_OCTET_STRING));
			implNameList = impl_delete->implNameList;
			for (SaUint32T i=0; i<impl_delete->size; ++i) {
				IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
				implNameList[i].size = ncs_decode_32bit(&p8);
				ncs_dec_skip_space(i_ub, 4);

				implNameList[i].buf = (char *)malloc(implNameList[i].size);
				if (implNameList[i].buf == NULL ||
						ncs_decode_n_octets_from_uba(i_ub,
								(uint8_t *)implNameList[i].buf,
								implNameList[i].size) != NCSCC_RC_SUCCESS) {
					LOG_WA("Failure inside ncs_decode_n_octets_from_uba");
					return NCSCC_RC_FAILURE;
				}
				implNameList[i].buf[implNameList[i].size] = 0;
			}
		}
	}
	return NCSCC_RC_SUCCESS;
}

/****************************************************************************\
 PROCEDURE NAME : immsv_evt_enc_flat

 DESCRIPTION    : Encodes the contents of IMMSV_EVT into userbuf
		  The top level is encode flat (without byte-order correction).
		  Levels below the top are encoded with byter order correction
		  (because we dont really save much space/execution by
		   encoding the bits and pieces of the sub-level as flat).

 ARGUMENTS      : *i_evt - Event Struct.
		  *o_ub  - User Buff.

 RETURNS        : None

 NOTES          :
\*****************************************************************************/
uint32_t immsv_evt_enc_flat(IMMSV_EVT *i_evt, NCS_UBAID *o_ub)
{
	uint32_t size;

	size = sizeof(IMMSV_EVT);

	/* Encode the Top level evt without byte-order correction. */
	if (ncs_encode_n_octets_in_uba(o_ub, (uint8_t *)i_evt, size) !=
	    NCSCC_RC_SUCCESS) {
		LOG_ER(
		    "Failed to flat encode IMMSV_EVT in ncs_encode_n_octets_in_uba");
		abort();
	}
	return immsv_evt_enc_sublevels(i_evt, o_ub);
}

/****************************************************************************\
 PROCEDURE NAME : immsv_evt_dec_flat

 DESCRIPTION    : Decodes the contents of IMMSV_EVT from user buf
		  Inverse of immsv_evt_enc_flat.

 ARGUMENTS      : *i_evt - Event Struct.
		  *o_ub  - User Buff.

 RETURNS        : None

 NOTES          :
\*****************************************************************************/
uint32_t immsv_evt_dec_flat(NCS_UBAID *i_ub, IMMSV_EVT *o_evt)
{
	uint32_t size;

	size = sizeof(IMMSV_EVT);

	/* Decode the Top level evt without byte order correction */
	if (ncs_decode_n_octets_from_uba(i_ub, (uint8_t *)o_evt, size) !=
	    NCSCC_RC_SUCCESS) {
		LOG_ER(
		    "Failed to flat decode IMMSV_EVT in ncs_decode_n_octets_from_uba");
		abort();
	}

	return immsv_evt_dec_sublevels(i_ub, o_evt);
}

static uint32_t immsv_evt_enc_toplevel(IMMSV_EVT *i_evt, NCS_UBAID *o_ub)
{
	uint32_t rc = NCSCC_RC_SUCCESS;
	uint8_t *p8;
	/*TRACE_ENTER(); */

	/*i_evt->type */
	IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
	ncs_encode_32bit(&p8, i_evt->type);
	ncs_enc_claim_space(o_ub, 4);

	if (i_evt->type == IMMSV_EVT_TYPE_IMMA) {
		IMMA_EVT *immaevt = &i_evt->info.imma;

		/*Nonflat encode for IMMA needed when agent and IMMND use
		   different compilations despite being colocated. Example:
		   Agent is compiled 32bit, IMMND compiled for 64 bit. */

		IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
		ncs_encode_32bit(&p8, immaevt->type);
		ncs_enc_claim_space(o_ub, 4);

		switch (immaevt->type) {
		/*Local events, not sent over MDS */
		case IMMA_EVT_MDS_INFO: /* IMMND UP/DOWN Info */
		case IMMA_EVT_TIME_OUT: /* Time out events at IMMA */
			LOG_ER("Unexpected event over MDS->IMMA:%u",
			       immaevt->type);
			break;

		/* Events from IMMND */
		case IMMA_EVT_ND2A_IMM_INIT_RSP:
			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
			ncs_encode_64bit(&p8, immaevt->info.initRsp.immHandle);
			ncs_enc_claim_space(o_ub, 8);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8, immaevt->info.initRsp.error);
			ncs_enc_claim_space(o_ub, 4);
			break;

		case IMMA_EVT_ND2A_IMM_FINALIZE_RSP:
		case IMMA_EVT_ND2A_IMM_ERROR:
		case IMMA_EVT_ND2A_IMM_RESURRECT_RSP:
		case IMMA_EVT_ND2A_PROC_STALE_CLIENTS: /* Really nothing to
							  encode for this one */
		case IMMA_EVT_ND2A_IMM_ERROR_2:
			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8, immaevt->info.errRsp.error);
			ncs_enc_claim_space(o_ub, 4);

			if (immaevt->type == IMMA_EVT_ND2A_IMM_ERROR_2) {
				IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 1);
				ncs_encode_8bit(
				    &p8,
				    (immaevt->info.errRsp.errStrings) ? 1 : 0);
				ncs_enc_claim_space(o_ub, 1);
			}

			break;

		case IMMA_EVT_ND2A_IMM_ADMINIT_RSP:
		case IMMA_EVT_ND2A_CCB_AUG_INIT_RSP:
			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8, immaevt->info.admInitRsp.ownerId);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8, immaevt->info.admInitRsp.error);
			ncs_enc_claim_space(o_ub, 4);
			break;

		case IMMA_EVT_ND2A_IMM_ADMOP:     // Admin-op OI callback
		case IMMA_EVT_ND2A_IMM_PBE_ADMOP: // Admin-op OI callback
			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8,
					 immaevt->info.admOpReq.adminOwnerId);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8,
					 immaevt->info.admOpReq.invocation);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
			ncs_encode_64bit(&p8,
					 immaevt->info.admOpReq.operationId);
			ncs_enc_claim_space(o_ub, 8);

			/*skip admOpReq.timeout */

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
			ncs_encode_64bit(&p8,
					 immaevt->info.admOpReq.continuationId);
			ncs_enc_claim_space(o_ub, 8);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(
			    &p8, immaevt->info.admOpReq.objectName.size);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 1);
			ncs_encode_8bit(
			    &p8, (immaevt->info.admOpReq.params) ? 1 : 0);
			ncs_enc_claim_space(o_ub, 1);
			break;

		case IMMA_EVT_ND2A_ADMOP_RSP: // Response from AdminOp to OM
					      // client
		case IMMA_EVT_ND2A_ADMOP_RSP_2: // Response from AdminOp to OM
						// client
			/*skip oi_client_hdl */

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
			ncs_encode_64bit(&p8,
					 immaevt->info.admOpRsp.invocation);
			ncs_enc_claim_space(o_ub, 8);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8, immaevt->info.admOpRsp.result);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8, immaevt->info.admOpRsp.error);
			ncs_enc_claim_space(o_ub, 4);
			if (immaevt->type == IMMA_EVT_ND2A_ADMOP_RSP_2) {
				IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 1);
				ncs_encode_8bit(
				    &p8,
				    (immaevt->info.admOpRsp.parms) ? 1 : 0);
				ncs_enc_claim_space(o_ub, 1);
			}
			break;

		case IMMA_EVT_ND2A_CCBINIT_RSP: // Response from for CcbInit
			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8, immaevt->info.ccbInitRsp.error);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8, immaevt->info.ccbInitRsp.ccbId);
			ncs_enc_claim_space(o_ub, 4);
			break;

		case IMMA_EVT_ND2A_SEARCHINIT_RSP: // Response from for
						   // SearchInit
			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8,
					 immaevt->info.searchInitRsp.error);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8,
					 immaevt->info.searchInitRsp.searchId);
			ncs_enc_claim_space(o_ub, 4);
			break;

		case IMMA_EVT_ND2A_SEARCHBUNDLENEXT_RSP: // Response from for
							 // SearchNext
			/*Totaly encoded in sublevel. */
			break;

		case IMMA_EVT_ND2A_SEARCHNEXT_RSP: // Response from for
						   // SearchNext
		case IMMA_EVT_ND2A_ACCESSOR_GET_RSP: // Response from for
						     // AccessorGet
			/*Totaly encoded in sublevel. */
			break;

		case IMMA_EVT_ND2A_SEARCH_REMOTE: // Fetch pure runtime
						  // attributes.
			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
			ncs_encode_64bit(&p8,
					 immaevt->info.searchRemote.client_hdl);
			ncs_enc_claim_space(o_ub, 8);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(
			    &p8, immaevt->info.searchRemote.requestNodeId);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(
			    &p8, immaevt->info.searchRemote.remoteNodeId);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8,
					 immaevt->info.searchRemote.searchId);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(
			    &p8, immaevt->info.searchRemote.objectName.size);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 1);
			ncs_encode_8bit(
			    &p8, (immaevt->info.searchRemote.attributeNames)
				     ? 1
				     : 0);
			ncs_enc_claim_space(o_ub, 1);
			break;

		case IMMA_EVT_ND2A_CLASS_DESCR_GET_RSP: // Response for
							// ClassDescriptionGet
			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(
			    &p8, immaevt->info.classDescr.classCategory);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 1);
			ncs_encode_8bit(
			    &p8,
			    (immaevt->info.classDescr.attrDefinitions) ? 1 : 0);
			ncs_enc_claim_space(o_ub, 1);
			break;

		case IMMA_EVT_ND2A_IMPLSET_RSP: // Response for
						// saImmOiImplementerSet
			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8, immaevt->info.implSetRsp.error);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8, immaevt->info.implSetRsp.implId);
			ncs_enc_claim_space(o_ub, 4);
			break;

		case IMMA_EVT_ND2A_OI_OBJ_CREATE_UC:      // OBJ CREATE UP-CALL
		case IMMA_EVT_ND2A_OI_OBJ_CREATE_LONG_UC: // OBJ CREATE UP-CALL
							  // for long DN/RDN
			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8, immaevt->info.objCreate.ccbId);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8,
					 immaevt->info.objCreate.adminOwnerId);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(
			    &p8, immaevt->info.objCreate.className.size);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(
			    &p8, immaevt->info.objCreate.parentOrObjectDn.size);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
			ncs_encode_64bit(&p8,
					 immaevt->info.objCreate.immHandle);
			ncs_enc_claim_space(o_ub, 8);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 1);
			ncs_encode_8bit(
			    &p8, (immaevt->info.objCreate.attrValues) ? 1 : 0);
			ncs_enc_claim_space(o_ub, 1);
			break;

		case IMMA_EVT_ND2A_OI_OBJ_DELETE_UC: // OBJ DELETE UP-CALL.
		case IMMA_EVT_ND2A_OI_OBJ_DELETE_LONG_UC:
			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8, immaevt->info.objDelete.ccbId);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8,
					 immaevt->info.objDelete.adminOwnerId);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(
			    &p8, immaevt->info.objDelete.objectName.size);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
			ncs_encode_64bit(&p8,
					 immaevt->info.objDelete.immHandle);
			ncs_enc_claim_space(o_ub, 8);
			break;

		case IMMA_EVT_ND2A_OI_OBJ_MODIFY_UC:      // OBJ MODIFY UP-CALL.
		case IMMA_EVT_ND2A_OI_OBJ_MODIFY_LONG_UC: // OBJ MODIFY UP-CALL
							  // for long DN.
			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8, immaevt->info.objModify.ccbId);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8,
					 immaevt->info.objModify.adminOwnerId);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(
			    &p8, immaevt->info.objModify.objectName.size);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 1);
			ncs_encode_8bit(
			    &p8, (immaevt->info.objModify.attrMods) ? 1 : 0);
			ncs_enc_claim_space(o_ub, 1);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
			ncs_encode_64bit(&p8,
					 immaevt->info.objModify.immHandle);
			ncs_enc_claim_space(o_ub, 8);
			break;

		case IMMA_EVT_ND2A_OI_CCB_COMPLETED_UC: // CCB COMPLETED
							// UP-CALL.
		case IMMA_EVT_ND2A_OI_CCB_APPLY_UC: // CCB APPLY UP-CALL.
		case IMMA_EVT_ND2A_OI_CCB_ABORT_UC: // CCB ABORT UP-CALL.
			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8, immaevt->info.ccbCompl.ccbId);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8, immaevt->info.ccbCompl.implId);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8,
					 immaevt->info.ccbCompl.invocation);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
			ncs_encode_64bit(&p8, immaevt->info.ccbCompl.immHandle);
			ncs_enc_claim_space(o_ub, 8);
			break;

		/*
		   case IMMA_EVT_ND2A_IMM_SYNC_RSP:
		   break;
		 */

		case IMMA_EVT_ND2A_IMM_CLM_NODE_LEFT:
		case IMMA_EVT_ND2A_IMM_CLM_NODE_JOINED:
			/* Noinformation is shared because of these evevts
			   encoding/decoding is not required */
			break;

		default:
			LOG_ER("Illegal IMMA message type:%u", immaevt->type);
			rc = NCSCC_RC_OUT_OF_MEM;
		}
	} else if (i_evt->type == IMMSV_EVT_TYPE_IMMD) {
		IMMD_EVT *immdevt = &i_evt->info.immd;

		/*immdevt->type */
		IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
		ncs_encode_32bit(&p8, immdevt->type);
		ncs_enc_claim_space(o_ub, 4);

		switch (immdevt->type) {
		/* ALL IMMD MDS messages come from IMMND */

		/* Messages using immdevt->info.ctrl_msg */
		case IMMD_EVT_ND2D_INTRO:	    /*Identification message from IMMND to
							IMMD. */
		case IMMD_EVT_ND2D_ANNOUNCE_LOADING: /*Coordinator attempts
							start of load. */
		case IMMD_EVT_ND2D_REQ_SYNC:      /*Straggler requests sync.  */
		case IMMD_EVT_ND2D_ANNOUNCE_DUMP: /*Dump/backup invoked */
		case IMMD_EVT_ND2D_SYNC_START:    /*Coordinator wants to start
						     sync. */
		case IMMD_EVT_ND2D_SYNC_ABORT:    /*Coordinator wants to abort
						     sync. */
		case IMMD_EVT_ND2D_PBE_PRTO_PURGE_MUTATIONS: /*Coord wants to
								purge rt obj
								mutations*/
		case IMMD_EVT_ND2D_LOADING_FAILED:	   /*Coord informs that loading
								failed.*/
		case IMMD_EVT_ND2D_LOADING_COMPLETED:	/*Coord informs that
								loading completed*/

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8, immdevt->info.ctrl_msg.ndExecPid);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8, immdevt->info.ctrl_msg.epoch);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 1);
			ncs_encode_8bit(&p8, immdevt->info.ctrl_msg.refresh);
			ncs_enc_claim_space(o_ub, 1);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 1);
			ncs_encode_8bit(&p8, immdevt->info.ctrl_msg.pbeEnabled);
			ncs_enc_claim_space(o_ub, 1);

			if ((immdevt->info.ctrl_msg.pbeEnabled >= 3) &&
			    (immdevt->type ==
			     IMMD_EVT_ND2D_INTRO)) { /* extended intro */
				IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
				ncs_encode_32bit(
				    &p8, immdevt->info.ctrl_msg.dir.size);
				ncs_enc_claim_space(o_ub, 4);
				/* immdevt->info.ctrl_msg.dir.buf encoded by
				 * sublevel */

				IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
				ncs_encode_32bit(
				    &p8, immdevt->info.ctrl_msg.xmlFile.size);
				ncs_enc_claim_space(o_ub, 4);
				/* immdevt->info.ctrl_msg.xmlFile.buf encoded by
				 * sublevel */

				IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
				ncs_encode_32bit(
				    &p8, immdevt->info.ctrl_msg.pbeFile.size);
				ncs_enc_claim_space(o_ub, 4);
				/* immdevt->info.ctrl_msg.pbeFile.buf encoded by
				 * sublevel */
			}

			if ((immdevt->info.ctrl_msg.refresh == 2) &&
			    (immdevt->type ==
			     IMMD_EVT_ND2D_INTRO)) { /* Intro after IMMD
							restart. */

				IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
				ncs_encode_64bit(
				    &p8, immdevt->info.ctrl_msg.fevs_count);
				ncs_enc_claim_space(o_ub, 8);

				IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
				ncs_encode_32bit(
				    &p8, immdevt->info.ctrl_msg.admo_id_count);
				ncs_enc_claim_space(o_ub, 4);

				IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
				ncs_encode_32bit(
				    &p8, immdevt->info.ctrl_msg.ccb_id_count);
				ncs_enc_claim_space(o_ub, 4);

				IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
				ncs_encode_32bit(
				    &p8, immdevt->info.ctrl_msg.impl_count);
				ncs_enc_claim_space(o_ub, 4);
			}

			break;

		case IMMD_EVT_ND2D_SYNC_FEVS_BASE:
			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
			ncs_encode_64bit(&p8,
					 immdevt->info.syncFevsBase.client_hdl);
			ncs_enc_claim_space(o_ub, 8);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
			ncs_encode_64bit(&p8,
					 immdevt->info.syncFevsBase.fevsBase);
			ncs_enc_claim_space(o_ub, 8);
			break;

		case IMMD_EVT_ND2D_ADMINIT_REQ: /* AdminOwnerInitialize */
		{
			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
			ncs_encode_64bit(&p8,
					 immdevt->info.admown_init.client_hdl);
			ncs_enc_claim_space(o_ub, 8);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 2);
			const char *value = osaf_extended_name_borrow(
			    &immdevt->info.admown_init.i.adminOwnerName);
			size_t length = strlen(value);
			if (length >= IMMSV_MAX_ADMO_NAME_LENGTH) {
				// With this check we don't need to check for
				// extended names
				LOG_WA(
				    "Admin owner name is too long (max. 255 characters)");
				rc = NCSCC_RC_FAILURE;
				break;
			}
			ncs_encode_16bit(&p8, length);
			ncs_enc_claim_space(o_ub, 2);

			/* adminOwnerName.value is top level because type is
			 * SaNameT */
			if (ncs_encode_n_octets_in_uba(o_ub, (uint8_t *)value,
						       length) !=
			    NCSCC_RC_SUCCESS) {
				LOG_WA(
				    "Failure inside ncs_encode_n_octets_in_uba");
				rc = NCSCC_RC_FAILURE;
				break;
			}

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 1);
			ncs_encode_8bit(&p8, immdevt->info.admown_init.i
						 .releaseOwnershipOnFinalize);
			ncs_enc_claim_space(o_ub, 1);
			break;
		}

		case IMMD_EVT_ND2D_FEVS_REQ: /*Fake EVS over Director. */
		case IMMD_EVT_ND2D_FEVS_REQ_2:
			osafassert(sizeof(MDS_DEST) == 8);
			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
			ncs_encode_64bit(&p8, immdevt->info.fevsReq.reply_dest);
			ncs_enc_claim_space(o_ub, 8);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
			ncs_encode_64bit(&p8, immdevt->info.fevsReq.client_hdl);
			ncs_enc_claim_space(o_ub, 8);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8, immdevt->info.fevsReq.msg.size);
			ncs_enc_claim_space(o_ub, 4);
			/* immdevt->info.fevsRe.msg.buf encoded by sublevel */

			if (immdevt->type == IMMD_EVT_ND2D_FEVS_REQ_2) {
				IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 1);
				ncs_encode_8bit(
				    &p8, immdevt->info.fevsReq.isObjSync);
				ncs_enc_claim_space(o_ub, 1);
			}
			break;

		case IMMD_EVT_ND2D_CCBINIT_REQ: /* CcbInitialize */
			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8,
					 immdevt->info.ccb_init.adminOwnerId);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
			ncs_encode_64bit(&p8, immdevt->info.ccb_init.ccbFlags);
			ncs_enc_claim_space(o_ub, 8);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
			ncs_encode_64bit(&p8,
					 immdevt->info.ccb_init.client_hdl);
			ncs_enc_claim_space(o_ub, 8);
			break;

		case IMMD_EVT_ND2D_IMPLSET_REQ:   /*OiImplementerSet */
		case IMMD_EVT_ND2D_IMPLSET_REQ_2: /*OiImplementerSet */
			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
			ncs_encode_64bit(&p8,
					 immdevt->info.impl_set.reply_dest);
			ncs_enc_claim_space(o_ub, 8);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
			ncs_encode_64bit(&p8,
					 immdevt->info.impl_set.r.client_hdl);
			ncs_enc_claim_space(o_ub, 8);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(
			    &p8, immdevt->info.impl_set.r.impl_name.size);
			ncs_enc_claim_space(o_ub, 4);
			/* immdevt->info.impl_set.r.impl_name.buf encoded by
			 * sublevel */

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8, immdevt->info.impl_set.r.scope);
			ncs_enc_claim_space(o_ub, 4);

			if (immdevt->type == IMMD_EVT_ND2D_IMPLSET_REQ_2) {
				IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
				ncs_encode_32bit(
				    &p8, immdevt->info.impl_set.r.oi_timeout);
				ncs_enc_claim_space(o_ub, 4);
			}

		/*intentional fall through - encode impl_id */
		case IMMD_EVT_ND2D_DISCARD_IMPL: /*Internal discard implementer
						    message */

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8, immdevt->info.impl_set.r.impl_id);
			ncs_enc_claim_space(o_ub, 4);
			break;

		case IMMD_EVT_ND2D_OI_OBJ_MODIFY: /*saImmOiRtObjectUpdate */

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
			ncs_encode_64bit(&p8,
					 immdevt->info.objModify.immHandle);
			ncs_enc_claim_space(o_ub, 8);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8,
					 immdevt->info.objModify.adminOwnerId);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(
			    &p8, immdevt->info.objModify.objectName.size);
			ncs_enc_claim_space(o_ub, 4);
			/* immdevt->info.objModify.objectName.buf encoded by
			 * sublevel */

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 1);
			ncs_encode_8bit(
			    &p8, (immdevt->info.objModify.attrMods) ? 1 : 0);
			ncs_enc_claim_space(o_ub, 1);
			break;

		case IMMD_EVT_ND2D_ABORT_CCB: /*Broadcast attempt to abort ccb.
					       */
			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8, immdevt->info.ccbId);
			ncs_enc_claim_space(o_ub, 4);
			break;

		case IMMD_EVT_ND2D_ADMO_HARD_FINALIZE: /* Broadcast hard admo
							  finalize */
			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8, immdevt->info.admoId);
			ncs_enc_claim_space(o_ub, 4);
			break;

		case IMMD_EVT_ND2D_2PBE_PRELOAD: /* 2PBE loading intialization
						  */
			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8, immdevt->info.pbe2.epoch);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8, immdevt->info.pbe2.maxCommitTime);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8, immdevt->info.pbe2.maxCcbId);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8,
					 immdevt->info.pbe2.maxWeakCommitTime);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
			ncs_encode_64bit(&p8, immdevt->info.pbe2.maxWeakCcbId);
			ncs_enc_claim_space(o_ub, 8);
			break;

		case IMMD_EVT_ND2D_IMPLDELETE:
			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8, immdevt->info.impl_delete.size);
			ncs_enc_claim_space(o_ub, 4);
			break;

		case IMMD_EVT_MDS_INFO:
			LOG_WA("Unexpected event over MDS->IMMD:%u",
			       immdevt->type);
		case IMMD_EVT_ND2D_IMM_SYNC_INFO: /*May need for
						     Director<->standby sync */
		case IMMD_EVT_ND2D_ACTIVE_SET:    /*may need this or similar */
		default:
			LOG_ER("Illegal IMMD message type:%u", immdevt->type);
			rc = NCSCC_RC_FAILURE;
		}
	} else if (i_evt->type == IMMSV_EVT_TYPE_IMMND) {
		IMMND_EVT *immndevt = &i_evt->info.immnd;

		/*immndevt->type */
		IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
		ncs_encode_32bit(&p8, immndevt->type);
		ncs_enc_claim_space(o_ub, 4);
		/*TRACE_1("Encoding IMMND event:%u %s",immndevt->type,
		   immsv_get_immnd_evt_name(immndevt->type)); */
		switch (immndevt->type) {

		/*Nonflat encode for IMMA needed when agent and IMMND use
		   different compilations despite being colocated. Example:
		   Agent is compiled 32bit, IMMND compiled for 64 bit. */

		case IMMND_EVT_A2ND_IMM_INIT:       /* ImmOm Initialization */
		case IMMND_EVT_A2ND_IMM_OI_INIT:    /* ImmOi Initialization */
		case IMMND_EVT_A2ND_IMM_CLIENTHIGH: /* For client resurrections
						     */
		case IMMND_EVT_A2ND_IMM_OM_CLIENTHIGH: /* For client
							  resurrections */
		case IMMND_EVT_A2ND_IMM_OI_CLIENTHIGH: /* For client
							  resurrections */
			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(
			    &p8, immndevt->info.initReq.version.releaseCode);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(
			    &p8, immndevt->info.initReq.version.majorVersion);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(
			    &p8, immndevt->info.initReq.version.minorVersion);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8,
					 immndevt->info.initReq.client_pid);
			ncs_enc_claim_space(o_ub, 4);
			break;

		case IMMND_EVT_A2ND_IMM_FINALIZE:     /* ImmOm finalization */
		case IMMND_EVT_A2ND_IMM_OI_FINALIZE:  /* ImmOi finalization */
		case IMMND_EVT_A2ND_IMM_OM_RESURRECT: /* ImmOm resurrect hdl */
		case IMMND_EVT_A2ND_IMM_OI_RESURRECT: /* ImmOi resurrect hdl */
		case IMMND_EVT_A2ND_SYNC_FINALIZE:    /* immsv_finalize_sync */
		case IMMND_EVT_A2ND_CL_TIMEOUT: /* lib timeout on sync call */
			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
			ncs_encode_64bit(&p8, immndevt->info.finReq.client_hdl);
			ncs_enc_claim_space(o_ub, 8);
			break;

		case IMMND_EVT_A2ND_IMM_ADMINIT: /* AdminOwnerInitialize */
		{
			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
			ncs_encode_64bit(&p8,
					 immndevt->info.adminitReq.client_hdl);
			ncs_enc_claim_space(o_ub, 8);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 2);
			const char *value = osaf_extended_name_borrow(
			    &immndevt->info.adminitReq.i.adminOwnerName);
			size_t length = strlen(value);
			if (length >= IMMSV_MAX_ADMO_NAME_LENGTH) {
				// With this check we don't need to check for
				// extended names
				LOG_WA(
				    "Admin owner name is too long (max. 255 characters)");
				rc = NCSCC_RC_FAILURE;
				break;
			}
			ncs_encode_16bit(&p8, length);
			ncs_enc_claim_space(o_ub, 2);

			/* adminOwnerName.value is top level because type is
			 * SaNameT */
			if (ncs_encode_n_octets_in_uba(o_ub, (uint8_t *)value,
						       length) !=
			    NCSCC_RC_SUCCESS) {
				LOG_WA(
				    "Failure inside ncs_encode_n_octets_in_uba");
				rc = NCSCC_RC_FAILURE;
				break;
			}

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 1);
			ncs_encode_8bit(&p8, immndevt->info.adminitReq.i
						 .releaseOwnershipOnFinalize);
			ncs_enc_claim_space(o_ub, 1);
			break;
		}

		case IMMND_EVT_A2ND_IMM_FEVS: /*Fake EVS msg from Agent
						 (forward) */
		case IMMND_EVT_A2ND_IMM_FEVS_2:
		case IMMND_EVT_D2ND_GLOB_FEVS_REQ: /* Fake EVS msg from director
						      (consume) */
		case IMMND_EVT_D2ND_GLOB_FEVS_REQ_2:
			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
			ncs_encode_64bit(&p8,
					 immndevt->info.fevsReq.sender_count);
			ncs_enc_claim_space(o_ub, 8);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
			ncs_encode_64bit(&p8,
					 immndevt->info.fevsReq.reply_dest);
			ncs_enc_claim_space(o_ub, 8);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
			ncs_encode_64bit(&p8,
					 immndevt->info.fevsReq.client_hdl);
			ncs_enc_claim_space(o_ub, 8);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8, immndevt->info.fevsReq.msg.size);
			ncs_enc_claim_space(o_ub, 4);
			/* immndevt->info.fevsReq.msg.buf encoded by encode
			 * sublevel */

			if ((immndevt->type == IMMND_EVT_A2ND_IMM_FEVS_2) ||
			    (immndevt->type ==
			     IMMND_EVT_D2ND_GLOB_FEVS_REQ_2)) {
				IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 1);
				ncs_encode_8bit(
				    &p8, immndevt->info.fevsReq.isObjSync);
				ncs_enc_claim_space(o_ub, 1);
			}
			break;

		case IMMND_EVT_A2ND_CCBINIT: /* CcbInitialize */
			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(
			    &p8, immndevt->info.ccbinitReq.adminOwnerId);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
			ncs_encode_64bit(&p8,
					 immndevt->info.ccbinitReq.ccbFlags);
			ncs_enc_claim_space(o_ub, 8);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
			ncs_encode_64bit(&p8,
					 immndevt->info.ccbinitReq.client_hdl);
			ncs_enc_claim_space(o_ub, 8);
			break;

		case IMMND_EVT_A2ND_SEARCHINIT:    /* SearchInitialize */
		case IMMND_EVT_A2ND_ACCESSOR_GET:  /* AccessorGet */
		case IMMND_EVT_A2ND_OBJ_SAFE_READ: /* saImmOmCcbObjectRead */

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
			ncs_encode_64bit(&p8,
					 immndevt->info.searchInit.client_hdl);
			ncs_enc_claim_space(o_ub, 8);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(
			    &p8, immndevt->info.searchInit.rootName.size);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8, immndevt->info.searchInit.scope);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
			ncs_encode_64bit(
			    &p8, immndevt->info.searchInit.searchOptions);
			ncs_enc_claim_space(o_ub, 8);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(
			    &p8, immndevt->info.searchInit.searchParam.present);
			ncs_enc_claim_space(o_ub, 4);

			if (immndevt->info.searchInit.searchParam.present ==
				ImmOmSearchParameter_PR_oneAttrParam ||
			    immndevt->info.searchInit.searchParam.present ==
				ImmOmSearchParameter_PR_nonExtendedName_oneAttrParam) {
				IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
				ncs_encode_32bit(
				    &p8,
				    immndevt->info.searchInit.searchParam.choice
					.oneAttrParam.attrName.size);
				ncs_enc_claim_space(o_ub, 4);

				IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
				ncs_encode_32bit(
				    &p8,
				    immndevt->info.searchInit.searchParam.choice
					.oneAttrParam.attrValueType);
				ncs_enc_claim_space(o_ub, 4);
			}

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 1);
			ncs_encode_8bit(
			    &p8,
			    (immndevt->info.searchInit.attributeNames) ? 1 : 0);
			ncs_enc_claim_space(o_ub, 1);

			if (immndevt->type == IMMND_EVT_A2ND_OBJ_SAFE_READ) {
				IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
				ncs_encode_32bit(
				    &p8, immndevt->info.searchInit.ccbId);
				ncs_enc_claim_space(o_ub, 4);
			}
			break;

		case IMMND_EVT_A2ND_SEARCHNEXT:     /* SearchNext */
		case IMMND_EVT_A2ND_SEARCHFINALIZE: /* SearchFinalize */
			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
			ncs_encode_64bit(&p8,
					 immndevt->info.searchOp.client_hdl);
			ncs_enc_claim_space(o_ub, 8);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8, immndevt->info.searchOp.searchId);
			ncs_enc_claim_space(o_ub, 4);
			break;

		/*
		   case IMMND_EVT_A2ND_SEARCH_REMOTE:
		 */

		case IMMND_EVT_A2ND_RT_ATT_UPPD_RSP: /* reply for fetch of rt
							attr vals */
			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
			ncs_encode_64bit(
			    &p8, immndevt->info.rtAttUpdRpl.sr.client_hdl);
			ncs_enc_claim_space(o_ub, 8);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(
			    &p8, immndevt->info.rtAttUpdRpl.sr.requestNodeId);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(
			    &p8, immndevt->info.rtAttUpdRpl.sr.remoteNodeId);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(
			    &p8, immndevt->info.rtAttUpdRpl.sr.searchId);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(
			    &p8, immndevt->info.rtAttUpdRpl.sr.objectName.size);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 1);
			ncs_encode_8bit(
			    &p8, (immndevt->info.rtAttUpdRpl.sr.attributeNames)
				     ? 1
				     : 0);
			ncs_enc_claim_space(o_ub, 1);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8,
					 immndevt->info.rtAttUpdRpl.result);
			ncs_enc_claim_space(o_ub, 4);
			break;

		case IMMND_EVT_A2ND_PBE_ADMOP_RSP:     /* PBE AdminOperation fevs
							  Reply */
		case IMMND_EVT_A2ND_ADMOP_RSP:	 /* AdminOperation sync local
							  Reply */
		case IMMND_EVT_A2ND_ASYNC_ADMOP_RSP:   /* AdminOperation async
							  local Reply */
		case IMMND_EVT_A2ND_ADMOP_RSP_2:       /* AdminOperation sync local
							  Reply - extended */
		case IMMND_EVT_A2ND_ASYNC_ADMOP_RSP_2: /* AdminOperation async
							  local Reply - extended
							*/
			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
			ncs_encode_64bit(&p8,
					 immndevt->info.admOpRsp.oi_client_hdl);
			ncs_enc_claim_space(o_ub, 8);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
			ncs_encode_64bit(&p8,
					 immndevt->info.admOpRsp.invocation);
			ncs_enc_claim_space(o_ub, 8);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8, immndevt->info.admOpRsp.result);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8, immndevt->info.admOpRsp.error);
			ncs_enc_claim_space(o_ub, 4);

			if ((immndevt->type == IMMND_EVT_A2ND_ADMOP_RSP_2) ||
			    (immndevt->type ==
			     IMMND_EVT_A2ND_ASYNC_ADMOP_RSP_2)) {
				IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 1);
				ncs_encode_8bit(
				    &p8,
				    (immndevt->info.admOpRsp.parms) ? 1 : 0);
				ncs_enc_claim_space(o_ub, 1);
			}
			break;

		case IMMND_EVT_A2ND_CLASS_DESCR_GET: /* saImmOmClassDescriptionGet
						      */
			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(
			    &p8, immndevt->info.classDescr.className.size);
			ncs_enc_claim_space(o_ub, 4);
			break;

		case IMMND_EVT_A2ND_OI_IMPL_SET:   /* saImmOiImplementerSet */
		case IMMND_EVT_A2ND_OI_IMPL_SET_2: /* saImmOiImplementerSet */
			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
			ncs_encode_64bit(&p8,
					 immndevt->info.implSet.client_hdl);
			ncs_enc_claim_space(o_ub, 8);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8,
					 immndevt->info.implSet.impl_name.size);
			ncs_enc_claim_space(o_ub, 4);
			/* immndevt->info.implSet.impl_name.buf encoded by
			 * sublevel */

			/*skip scope & impl_id */

			if (immndevt->type == IMMND_EVT_A2ND_OI_IMPL_SET_2) {
				IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
				ncs_encode_32bit(
				    &p8, immndevt->info.implSet.oi_timeout);
				ncs_enc_claim_space(o_ub, 4);
			}
			break;

		/*Fevs call IMMA->IMMD->IMMNDs have to suport non-flat encoding
		 */
		case IMMND_EVT_A2ND_OBJ_CREATE:    /* saImmOmCcbObjectCreate */
		case IMMND_EVT_A2ND_OI_OBJ_CREATE: /* saImmOiRtObjectCreate */
		case IMMND_EVT_A2ND_OBJ_CREATE_2:  /* saImmOmCcbObjectCreate_o3
						    */
		case IMMND_EVT_A2ND_OI_OBJ_CREATE_2: /* saImmOiRtObjectCreate_o3
						      */

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8, immndevt->info.objCreate.ccbId);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8,
					 immndevt->info.objCreate.adminOwnerId);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(
			    &p8, immndevt->info.objCreate.className.size);
			ncs_enc_claim_space(o_ub, 4);
			/* immndevt->info.objCreate.className.buf encoded by
			 * sublevel */

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(
			    &p8,
			    immndevt->info.objCreate.parentOrObjectDn.size);
			ncs_enc_claim_space(o_ub, 4);
			/* immndevt->info.objCreate.parentName.buf encoded by
			 * sublevel */

			/* immhandle field not used for these immnd messages
			   IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
			   ncs_encode_64bit(&p8,
			   immndevt->info.objCreate.immHandle);
			   ncs_enc_claim_space(o_ub, 8);
			 */
			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 1);
			ncs_encode_8bit(
			    &p8, (immndevt->info.objCreate.attrValues) ? 1 : 0);
			ncs_enc_claim_space(o_ub, 1);
			break;

		case IMMND_EVT_A2ND_OBJ_MODIFY:    /* saImmOmCcbObjectModify */
						   /* IMMND_EVT_A2ND_OI_OBJ_MODIFY is "converted" to fevs by
						    * immd_evt.c */
		case IMMND_EVT_A2ND_OI_OBJ_MODIFY: /* saImmOiRtObjectUpdate */

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
			ncs_encode_64bit(&p8,
					 immndevt->info.objModify.immHandle);
			ncs_enc_claim_space(o_ub, 8);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8, immndevt->info.objModify.ccbId);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8,
					 immndevt->info.objModify.adminOwnerId);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(
			    &p8, immndevt->info.objModify.objectName.size);
			ncs_enc_claim_space(o_ub, 4);
			/* immndevt->info.objModify.objectName.buf encoded by
			 * sublevel */

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 1);
			ncs_encode_8bit(
			    &p8, (immndevt->info.objModify.attrMods) ? 1 : 0);
			ncs_enc_claim_space(o_ub, 1);
			break;

		case IMMND_EVT_A2ND_OBJ_DELETE:    /* saImmOmCcbObjectDelete */
		case IMMND_EVT_A2ND_OI_OBJ_DELETE: /* saImmOiRtObjectDelete */
		case IMMND_EVT_A2ND_AUG_ADMO:      /* Inform IMMNDs of admo for
						      augment ccb*/
			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8, immndevt->info.objDelete.ccbId);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8,
					 immndevt->info.objDelete.adminOwnerId);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(
			    &p8, immndevt->info.objDelete.objectName.size);
			ncs_enc_claim_space(o_ub, 4);
			/* immndevt->info.objModify.objectName.buf encoded by
			 * sublevel */

			/*immndevt->info.objDelete.immHandle not used in these
			 * calls */
			break;

		case IMMND_EVT_A2ND_CCB_APPLY:    /* saImmOmCcbApply */
		case IMMND_EVT_A2ND_CCB_FINALIZE: /* saImmOmCcbFinalize */
		case IMMND_EVT_A2ND_RECOVER_CCB_OUTCOME:
		case IMMND_EVT_A2ND_CCB_VALIDATE: /* saImmOmCcbValidate */

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8, immndevt->info.ccbId);
			ncs_enc_claim_space(o_ub, 4);

			break;

		case IMMND_EVT_A2ND_IMM_ADMOP:       /* Syncronous AdminOp */
		case IMMND_EVT_A2ND_IMM_ADMOP_ASYNC: /* Asyncronous AdminOp */
			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8,
					 immndevt->info.admOpReq.adminOwnerId);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
			ncs_encode_64bit(&p8,
					 immndevt->info.admOpReq.operationId);
			ncs_enc_claim_space(o_ub, 8);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
			ncs_encode_64bit(
			    &p8, immndevt->info.admOpReq.continuationId);
			ncs_enc_claim_space(o_ub, 8);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
			ncs_encode_64bit(&p8, immndevt->info.admOpReq.timeout);
			ncs_enc_claim_space(o_ub, 8);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8,
					 immndevt->info.admOpReq.invocation);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(
			    &p8, immndevt->info.admOpReq.objectName.size);
			ncs_enc_claim_space(o_ub, 4);
			/* immndevt->info.admOpReq.objectName.buf encoded by
			 * sublevel */

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 1);
			ncs_encode_8bit(
			    &p8, (immndevt->info.admOpReq.params) ? 1 : 0);
			ncs_enc_claim_space(o_ub, 1);
			break;

		case IMMND_EVT_A2ND_CLASS_CREATE: /* saImmOmClassCreate */
		case IMMND_EVT_A2ND_CLASS_DELETE: /* saImmOmClassDelete */
			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(
			    &p8, immndevt->info.classDescr.className.size);
			ncs_enc_claim_space(o_ub, 4);
			/* immndevt->info.classDescr.className.buf encoded by
			 * sublevel */

			if (immndevt->type == IMMND_EVT_A2ND_CLASS_CREATE) {
				IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
				ncs_encode_32bit(
				    &p8,
				    immndevt->info.classDescr.classCategory);
				ncs_enc_claim_space(o_ub, 4);

				IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 1);
				ncs_encode_8bit(
				    &p8,
				    (immndevt->info.classDescr.attrDefinitions)
					? 1
					: 0);
				ncs_enc_claim_space(o_ub, 1);
			}
			break;

		case IMMND_EVT_A2ND_OBJ_SYNC: /* immsv_sync */
		case IMMND_EVT_A2ND_OBJ_SYNC_2:
			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(
			    &p8, immndevt->info.obj_sync.className.size);
			ncs_enc_claim_space(o_ub, 4);
			/* immndevt->info.obj_sync.className.buf encoded by
			 * sublevel */

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(
			    &p8, immndevt->info.obj_sync.objectName.size);
			ncs_enc_claim_space(o_ub, 4);
			/* immndevt->info.obj_sync.objectName.buf encoded by
			 * sublevel */

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 1);
			ncs_encode_8bit(
			    &p8, (immndevt->info.obj_sync.attrValues) ? 1 : 0);
			ncs_enc_claim_space(o_ub, 1);
			break;

		case IMMND_EVT_A2ND_ADMO_SET:     /* AdminOwnerSet */
		case IMMND_EVT_A2ND_ADMO_RELEASE: /* AdminOwnerRelease */
		case IMMND_EVT_A2ND_ADMO_CLEAR:   /* AdminOwnerClear */
			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8,
					 immndevt->info.admReq.adm_owner_id);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8, immndevt->info.admReq.scope);
			ncs_enc_claim_space(o_ub, 4);

			/* objectNames encoded by sublevel. */
			break;

		case IMMND_EVT_A2ND_ADMO_FINALIZE: /* AdminOwnerFinalize */
			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8,
					 immndevt->info.admFinReq.adm_owner_id);
			ncs_enc_claim_space(o_ub, 4);
			break;

		case IMMND_EVT_A2ND_OI_IMPL_CLR: /* saImmOiImplementerClear */
		case IMMND_EVT_A2ND_OI_CL_IMPL_SET:  /* saImmOiClassImplementerSet
						      */
		case IMMND_EVT_A2ND_OI_CL_IMPL_REL:  /* saImmOiClassImplementerRelease
						      */
		case IMMND_EVT_A2ND_OI_OBJ_IMPL_SET: /* saImmOiObjectImplementerSet
						      */
		case IMMND_EVT_A2ND_OI_OBJ_IMPL_REL: /* saImmOiObjectImplementerRelease
						      */
			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
			ncs_encode_64bit(&p8,
					 immndevt->info.implSet.client_hdl);
			ncs_enc_claim_space(o_ub, 8);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8, immndevt->info.implSet.impl_id);
			ncs_enc_claim_space(o_ub, 4);

			if (immndevt->type != IMMND_EVT_A2ND_OI_IMPL_CLR) {
				IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
				ncs_encode_32bit(
				    &p8, immndevt->info.implSet.impl_name.size);
				ncs_enc_claim_space(o_ub, 4);
				/* immndevt->info.implSet.impl_name.buf encoded
				 * by sublevel */
				if ((immndevt->type ==
				     IMMND_EVT_A2ND_OI_OBJ_IMPL_SET) ||
				    (immndevt->type ==
				     IMMND_EVT_A2ND_OI_OBJ_IMPL_REL)) {
					IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
					ncs_encode_32bit(
					    &p8, immndevt->info.implSet.scope);
					ncs_enc_claim_space(o_ub, 4);
				}
			}
			break;

		case IMMND_EVT_A2ND_CCB_COMPLETED_RSP:		    /* CcbCompleted local
								       Reply */
		case IMMND_EVT_A2ND_CCB_COMPLETED_RSP_2:	    /* CcbCompleted local
								       Reply */
		case IMMND_EVT_A2ND_CCB_OBJ_CREATE_RSP:		    /*CcbObjCreate local
								       Reply */
		case IMMND_EVT_A2ND_CCB_OBJ_CREATE_RSP_2:	   /*CcbObjCreate local
								       Reply */
		case IMMND_EVT_A2ND_CCB_OBJ_MODIFY_RSP:		    /*CcbObjModify local
								       Reply */
		case IMMND_EVT_A2ND_CCB_OBJ_MODIFY_RSP_2:	   /*CcbObjModify local
								       Reply */
		case IMMND_EVT_A2ND_CCB_OBJ_DELETE_RSP:		    /*CcbObjDelete local
								       Reply */
		case IMMND_EVT_A2ND_CCB_OBJ_DELETE_RSP_2:	   /*CcbObjDelete local
								       Reply */
		case IMMND_EVT_A2ND_PBE_PRT_OBJ_CREATE_RSP:	 /* Pbe OI rt obj
								       create response
								     */
		case IMMND_EVT_A2ND_PBE_PRTO_DELETES_COMPLETED_RSP: /*Pbe PRTO
								       deletes
								       done */
		case IMMND_EVT_A2ND_PBE_PRT_ATTR_UPDATE_RSP:	/* Pbe OI rt attr
								       update
								       response*/
		case IMMND_EVT_A2ND_OI_CCB_AUG_INIT:		    /*OI augments CCB inside
								       ccb upcall. #1963 */
		{
			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
			ncs_encode_64bit(
			    &p8, immndevt->info.ccbUpcallRsp.oi_client_hdl);
			ncs_enc_claim_space(o_ub, 8);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8,
					 immndevt->info.ccbUpcallRsp.ccbId);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8,
					 immndevt->info.ccbUpcallRsp.implId);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8, immndevt->info.ccbUpcallRsp.inv);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8,
					 immndevt->info.ccbUpcallRsp.result);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 2);
			const char *value = osaf_extended_name_borrow(
			    &immndevt->info.ccbUpcallRsp.name);
			size_t length = strlen(value);
			osafassert(length <= 0xffff);
			ncs_encode_16bit(&p8, length);
			ncs_enc_claim_space(o_ub, 2);

			/* name.value is top level because type is SaNameT */
			if ((immndevt->type ==
			     IMMND_EVT_A2ND_CCB_OBJ_DELETE_RSP) ||
			    (immndevt->type ==
			     IMMND_EVT_A2ND_CCB_OBJ_DELETE_RSP_2) ||
			    (immndevt->type ==
			     IMMND_EVT_A2ND_OI_CCB_AUG_INIT)) {
				if (length) {
					if (ncs_encode_n_octets_in_uba(
						o_ub, (uint8_t *)value,
						length) != NCSCC_RC_SUCCESS) {
						LOG_WA(
						    "Failure inside ncs_encode_n_octets_in_uba");
						rc = NCSCC_RC_FAILURE;
						break;
					}
				}
			}

			if ((immndevt->type ==
			     IMMND_EVT_A2ND_CCB_COMPLETED_RSP_2) ||
			    (immndevt->type ==
			     IMMND_EVT_A2ND_CCB_OBJ_CREATE_RSP_2) ||
			    (immndevt->type ==
			     IMMND_EVT_A2ND_CCB_OBJ_DELETE_RSP_2) ||
			    (immndevt->type ==
			     IMMND_EVT_A2ND_CCB_OBJ_MODIFY_RSP_2)) {
				IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
				ncs_encode_32bit(&p8,
						 immndevt->info.ccbUpcallRsp
						     .errorString.size);
				ncs_enc_claim_space(o_ub, 4);
				/* immndevt->info.ccbUpcallRsp.errorString.buf
				 * encoded by sublevel */
			}

			break;
		}

		/* Events IMMND->IMMND (asyncronous) type); */
		case IMMND_EVT_ND2ND_ADMOP_RSP:		/* AdminOperation sync fevs
							   Reply */
		case IMMND_EVT_ND2ND_ADMOP_RSP_2:       /* AdminOperation sync fevs
							   Reply */
		case IMMND_EVT_ND2ND_ASYNC_ADMOP_RSP:   /* AdminOperation async
							   fevs Reply */
		case IMMND_EVT_ND2ND_ASYNC_ADMOP_RSP_2: /* AdminOperation async
							   fevs Reply */

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
			ncs_encode_64bit(&p8,
					 immndevt->info.admOpRsp.oi_client_hdl);
			ncs_enc_claim_space(o_ub, 8);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
			ncs_encode_64bit(&p8,
					 immndevt->info.admOpRsp.invocation);
			ncs_enc_claim_space(o_ub, 8);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8, immndevt->info.admOpRsp.result);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8, immndevt->info.admOpRsp.error);
			ncs_enc_claim_space(o_ub, 4);

			if ((immndevt->type == IMMND_EVT_ND2ND_ADMOP_RSP_2) ||
			    (immndevt->type ==
			     IMMND_EVT_ND2ND_ASYNC_ADMOP_RSP_2)) {
				IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 1);
				ncs_encode_8bit(
				    &p8,
				    (immndevt->info.admOpRsp.parms) ? 1 : 0);
				ncs_enc_claim_space(o_ub, 1);
			}
			break;

		case IMMND_EVT_ND2ND_SYNC_FINALIZE: /* Sync finalize from coord
						       over fevs */
		case IMMND_EVT_ND2ND_SYNC_FINALIZE_2:

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(
			    &p8, immndevt->info.finSync.lastContinuationId);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 1);
			ncs_encode_8bit(
			    &p8, (immndevt->info.finSync.adminOwners) ? 1 : 0);
			ncs_enc_claim_space(o_ub, 1);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 1);
			ncs_encode_8bit(
			    &p8, (immndevt->info.finSync.implementers) ? 1 : 0);
			ncs_enc_claim_space(o_ub, 1);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 1);
			ncs_encode_8bit(
			    &p8, (immndevt->info.finSync.classes) ? 1 : 0);
			ncs_enc_claim_space(o_ub, 1);

			if (immndevt->type == IMMND_EVT_ND2ND_SYNC_FINALIZE_2) {
				TRACE(
				    "Encoding top level for IMMND_EVT_ND2ND_SYNC_FINALIZE_2");
				/* Start marker always explicitly encoded*/
			}

			break;

		case IMMND_EVT_ND2ND_SEARCH_REMOTE: /* Forward search request
						       from impl ND */

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
			ncs_encode_64bit(
			    &p8, immndevt->info.searchRemote.client_hdl);
			ncs_enc_claim_space(o_ub, 8);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(
			    &p8, immndevt->info.searchRemote.requestNodeId);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(
			    &p8, immndevt->info.searchRemote.remoteNodeId);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8,
					 immndevt->info.searchRemote.searchId);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(
			    &p8, immndevt->info.searchRemote.objectName.size);
			ncs_enc_claim_space(o_ub, 4);
			/* immndevt->info.searchRemote.objectName.buf encoded by
			 * sublevel */

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 1);
			ncs_encode_8bit(
			    &p8, (immndevt->info.searchRemote.attributeNames)
				     ? 1
				     : 0);
			ncs_enc_claim_space(o_ub, 1);
			break;

		case IMMND_EVT_ND2ND_SEARCH_REMOTE_RSP: /*Forward search req
							   from impl ND */

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8,
					 immndevt->info.rspSrchRmte.result);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(
			    &p8, immndevt->info.rspSrchRmte.requestNodeId);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(
			    &p8, immndevt->info.rspSrchRmte.remoteNodeId);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8,
					 immndevt->info.rspSrchRmte.searchId);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8,
					 immndevt->info.rspSrchRmte.runtimeAttrs
					     .objectName.size);
			ncs_enc_claim_space(o_ub, 4);
			/* immndevt->info.rspSrchRmte.runtimeAttrs.objectName.buf
			 * encoded by sublevel */

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 1);
			ncs_encode_8bit(&p8, (immndevt->info.rspSrchRmte
						  .runtimeAttrs.attrValuesList)
						 ? 1
						 : 0);
			ncs_enc_claim_space(o_ub, 1);
			break;

		/* Events from IMMD to IMMND */
		case IMMND_EVT_D2ND_INTRO_RSP:
		case IMMND_EVT_D2ND_SYNC_REQ:
		case IMMND_EVT_D2ND_LOADING_OK: /*rulingEpoch & fevsMsgStart */
		case IMMND_EVT_D2ND_SYNC_START:
		case IMMND_EVT_D2ND_SYNC_ABORT:
		case IMMND_EVT_D2ND_DUMP_OK:
		case IMMND_EVT_D2ND_DISCARD_NODE: /* Crashed IMMND or node,
						     bcast to NDs */
		case IMMND_EVT_D2ND_PBE_PRTO_PURGE_MUTATIONS:

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8, immndevt->info.ctrl.nodeId);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8, immndevt->info.ctrl.rulingEpoch);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
			ncs_encode_64bit(&p8, immndevt->info.ctrl.fevsMsgStart);
			ncs_enc_claim_space(o_ub, 8);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8, immndevt->info.ctrl.ndExecPid);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 1);
			ncs_encode_8bit(&p8, immndevt->info.ctrl.canBeCoord);
			ncs_enc_claim_space(o_ub, 1);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 1);
			ncs_encode_8bit(&p8,
					(immndevt->info.ctrl.isCoord) ? 1 : 0);
			ncs_enc_claim_space(o_ub, 1);

			/*syncStarted & nodeEpoch not really used D->ND,
			   only D->D mbcp
			 */
			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 1);
			ncs_encode_8bit(
			    &p8, (immndevt->info.ctrl.syncStarted) ? 1 : 0);
			ncs_enc_claim_space(o_ub, 1);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8, immndevt->info.ctrl.nodeEpoch);
			ncs_enc_claim_space(o_ub, 4);
			break;

		case IMMND_EVT_D2ND_SYNC_FEVS_BASE:
			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
			ncs_encode_64bit(&p8, immndevt->info.syncFevsBase);
			ncs_enc_claim_space(o_ub, 8);
			break;

		case IMMND_EVT_D2ND_ADMINIT: /* Admin Owner init reply */
			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(
			    &p8, immndevt->info.adminitGlobal.globalOwnerId);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 2);
			const char *value = osaf_extended_name_borrow(
			    &immndevt->info.adminitGlobal.i.adminOwnerName);
			size_t length = strlen(value);
			if (length >= IMMSV_MAX_ADMO_NAME_LENGTH) {
				// With this check we don't need to check for
				// extended names
				LOG_WA(
				    "Admin owner name is too long (max. 255 characters)");
				rc = NCSCC_RC_FAILURE;
				break;
			}
			ncs_encode_16bit(&p8, length);
			ncs_enc_claim_space(o_ub, 2);

			/* adminOwnerName.value is top level because type is
			 * SaNameT */
			if (ncs_encode_n_octets_in_uba(o_ub, (uint8_t *)value,
						       length) !=
			    NCSCC_RC_SUCCESS) {
				LOG_WA(
				    "Failure inside ncs_encode_n_octets_in_uba");
				rc = NCSCC_RC_FAILURE;
				break;
			}

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 1);
			ncs_encode_8bit(&p8, immndevt->info.adminitGlobal.i
						 .releaseOwnershipOnFinalize);
			ncs_enc_claim_space(o_ub, 1);
			break;

		case IMMND_EVT_D2ND_CCBINIT: /* Ccb init reply */
			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(
			    &p8, immndevt->info.ccbinitGlobal.globalCcbId);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(
			    &p8, immndevt->info.ccbinitGlobal.i.adminOwnerId);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
			ncs_encode_64bit(
			    &p8, immndevt->info.ccbinitGlobal.i.ccbFlags);
			ncs_enc_claim_space(o_ub, 8);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
			ncs_encode_64bit(
			    &p8, immndevt->info.ccbinitGlobal.i.client_hdl);
			ncs_enc_claim_space(o_ub, 8);
			break;

		case IMMND_EVT_D2ND_IMPLSET_RSP: /* Impl set reply from D with
						    impl id */
		case IMMND_EVT_D2ND_IMPLSET_RSP_2:
			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 8);
			ncs_encode_64bit(&p8,
					 immndevt->info.implSet.client_hdl);
			ncs_enc_claim_space(o_ub, 8);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8, immndevt->info.implSet.impl_id);
			ncs_enc_claim_space(o_ub, 4);

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8,
					 immndevt->info.implSet.impl_name.size);
			ncs_enc_claim_space(o_ub, 4);
			/* immndevt->info.implSet.impl_name.buf encoded by
			 * sublevel */

			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8, immndevt->info.implSet.scope);
			ncs_enc_claim_space(o_ub, 4);

			if (immndevt->type == IMMND_EVT_D2ND_IMPLSET_RSP_2) {
				IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
				ncs_encode_32bit(
				    &p8, immndevt->info.implSet.oi_timeout);
				ncs_enc_claim_space(o_ub, 4);
			}
			break;

		case IMMND_EVT_D2ND_DISCARD_IMPL: /* Discard implementer
						     broadcast to NDs */
			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8, immndevt->info.implSet.impl_id);
			ncs_enc_claim_space(o_ub, 4);
			break;

		case IMMND_EVT_D2ND_ABORT_CCB: /* Abort CCB broadcast to NDs */
			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8, immndevt->info.ccbId);
			ncs_enc_claim_space(o_ub, 4);
			break;

		case IMMND_EVT_D2ND_ADMO_HARD_FINALIZE: /* Admo hard fnlz bcast
							   to NDs */
			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8,
					 immndevt->info.admFinReq.adm_owner_id);
			ncs_enc_claim_space(o_ub, 4);
			break;

		case IMMND_EVT_D2ND_RESET:
			/* message has no contents */
			break;

		case IMMND_EVT_D2ND_IMPLDELETE:
			IMMSV_RSRV_SPACE_ASSERT(p8, o_ub, 4);
			ncs_encode_32bit(&p8, immndevt->info.impl_delete.size);
			ncs_enc_claim_space(o_ub, 4);
			break;

		case IMMND_EVT_MDS_INFO: /* IMMA/IMMND/IMMD UP/DOWN Info */
		case IMMND_EVT_TIME_OUT: /* Time out event */
		case IMMND_EVT_CB_DUMP:
			LOG_ER("Unexpected event over MDS->IMMND:%u",
			       immndevt->type);
		/*Fall through */
		default:
			LOG_ER("Illegal IMMND message type:%u", immndevt->type);

			rc = NCSCC_RC_FAILURE;
		}
	} else {
		LOG_ER("Illegal event message type or format");
		rc = NCSCC_RC_FAILURE;
	}
	/*TRACE_LEAVE(); */
	return rc;
}

static uint32_t immsv_evt_dec_toplevel(NCS_UBAID *i_ub, IMMSV_EVT *o_evt)
{
	uint32_t rc = NCSCC_RC_SUCCESS;
	uint8_t *p8;
	uint8_t local_data[8];
	/*TRACE_ENTER(); */

	/*o_evt->type */
	IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
	o_evt->type = ncs_decode_32bit(&p8);
	ncs_dec_skip_space(i_ub, 4);

	if (o_evt->type == IMMSV_EVT_TYPE_IMMA) {
		IMMA_EVT *immaevt = &o_evt->info.imma;

		/*Nonflat decode for IMMA needed when agent and IMMND use
		   different compilations despite being colocated. Example:
		   Agent is compiled 32bit, IMMND compiled for 64 bit. */

		IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
		immaevt->type = ncs_decode_32bit(&p8);
		ncs_dec_skip_space(i_ub, 4);

		switch (immaevt->type) {
		/*Local events, not sent over MDS */
		case IMMA_EVT_MDS_INFO: /* IMMND UP/DOWN Info */
		case IMMA_EVT_TIME_OUT: /* Time out events at IMMA */
			LOG_ER("Unexpected event over MDS->IMMA:%u",
			       immaevt->type);
			break;

		/* Events from IMMND */
		case IMMA_EVT_ND2A_IMM_INIT_RSP:
			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
			immaevt->info.initRsp.immHandle = ncs_decode_64bit(&p8);
			ncs_dec_skip_space(i_ub, 8);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immaevt->info.initRsp.error = ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);
			break;

		case IMMA_EVT_ND2A_IMM_FINALIZE_RSP:
		case IMMA_EVT_ND2A_IMM_ERROR: // Generic error reply
		case IMMA_EVT_ND2A_IMM_RESURRECT_RSP:
		case IMMA_EVT_ND2A_PROC_STALE_CLIENTS: /* Really nothing to
							  encode for this one */
		case IMMA_EVT_ND2A_IMM_ERROR_2:
			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immaevt->info.errRsp.error = ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			if (immaevt->type == IMMA_EVT_ND2A_IMM_ERROR_2) {
				IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub,
							1);
				if (ncs_decode_8bit(&p8)) {
					/*Bogus pointer-val forces
					  decode_sublevel to decode
					  errorStrings. */
					immaevt->info.errRsp.errStrings =
					    (void *)0x1;
				}
				ncs_dec_skip_space(i_ub, 1);
			}

			break;

		case IMMA_EVT_ND2A_IMM_ADMINIT_RSP:
		case IMMA_EVT_ND2A_CCB_AUG_INIT_RSP:
			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immaevt->info.admInitRsp.ownerId =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immaevt->info.admInitRsp.error = ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);
			break;

		case IMMA_EVT_ND2A_IMM_ADMOP:     /*Admin-op OI callback */
		case IMMA_EVT_ND2A_IMM_PBE_ADMOP: /*PBE Admin-op callback */
			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immaevt->info.admOpReq.adminOwnerId =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immaevt->info.admOpReq.invocation =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
			immaevt->info.admOpReq.operationId =
			    ncs_decode_64bit(&p8);
			ncs_dec_skip_space(i_ub, 8);

			/*skip admOpReq.timeout */

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
			immaevt->info.admOpReq.continuationId =
			    ncs_decode_64bit(&p8);
			ncs_dec_skip_space(i_ub, 8);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immaevt->info.admOpReq.objectName.size =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 1);
			if (ncs_decode_8bit(&p8)) {
				/*Bogus pointer-val forces decode_sublevel to
				 * decode params. */
				immaevt->info.admOpReq.params = (void *)0x1;
			}
			ncs_dec_skip_space(i_ub, 1);

			break;

		case IMMA_EVT_ND2A_ADMOP_RSP: // Response from AdminOp to OM
					      // client, normal call
		case IMMA_EVT_ND2A_ADMOP_RSP_2:

			/*skip oi_client_hdl */

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
			immaevt->info.admOpRsp.invocation =
			    ncs_decode_64bit(&p8);
			ncs_dec_skip_space(i_ub, 8);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immaevt->info.admOpRsp.result = ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immaevt->info.admOpRsp.error = ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			immaevt->info.admOpRsp.parms = NULL;

			if (immaevt->type == IMMA_EVT_ND2A_ADMOP_RSP_2) {
				IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub,
							1);
				if (ncs_decode_8bit(&p8)) {
					/*Bogus pointer-val forces
					 * decode_sublevel to decode params. */
					immaevt->info.admOpRsp.parms =
					    (void *)0x1;
				}
				ncs_dec_skip_space(i_ub, 1);
			}
			break;

		case IMMA_EVT_ND2A_CCBINIT_RSP: // Response from for CcbInit
			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immaevt->info.ccbInitRsp.error = ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immaevt->info.ccbInitRsp.ccbId = ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);
			break;

		case IMMA_EVT_ND2A_SEARCHINIT_RSP: // Response from for
						   // SearchInit
			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immaevt->info.searchInitRsp.error =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immaevt->info.searchInitRsp.searchId =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);
			break;

		case IMMA_EVT_ND2A_SEARCHBUNDLENEXT_RSP: // Response from for
							 // SearchNext
			/*Totaly decoded in sublevel. */
			break;

		case IMMA_EVT_ND2A_SEARCHNEXT_RSP: // Response from for
						   // SearchNext
		case IMMA_EVT_ND2A_ACCESSOR_GET_RSP: // Response from for
						     // AccessorGet
			/*Totaly decoded in sublevel. */
			break;

		case IMMA_EVT_ND2A_SEARCH_REMOTE: // Fetch pure runtime
						  // attributes.
			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
			immaevt->info.searchRemote.client_hdl =
			    ncs_decode_64bit(&p8);
			ncs_dec_skip_space(i_ub, 8);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immaevt->info.searchRemote.requestNodeId =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immaevt->info.searchRemote.remoteNodeId =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immaevt->info.searchRemote.searchId =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immaevt->info.searchRemote.objectName.size =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 1);
			if (ncs_decode_8bit(&p8)) {
				/*Bogus pointer-val forces decode_sublevel to
				   decode attributeNames. */
				immaevt->info.searchRemote.attributeNames =
				    (void *)0x1;
			}
			ncs_dec_skip_space(i_ub, 1);
			break;

		case IMMA_EVT_ND2A_CLASS_DESCR_GET_RSP: // Response for
							// SaImmOmClassDescriptionGet
			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immaevt->info.classDescr.classCategory =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 1);
			if (ncs_decode_8bit(&p8)) {
				/*Bogus pointer-val forces decode_sublevel to
				   decode attributeNames. */
				immaevt->info.classDescr.attrDefinitions =
				    (void *)0x1;
			}
			ncs_dec_skip_space(i_ub, 1);
			break;

		case IMMA_EVT_ND2A_IMPLSET_RSP: // Response for
						// saImmOiImplementerSet
			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immaevt->info.implSetRsp.error = ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immaevt->info.implSetRsp.implId = ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);
			break;

		case IMMA_EVT_ND2A_OI_OBJ_CREATE_UC:      // OBJ CREATE UP-CALL
		case IMMA_EVT_ND2A_OI_OBJ_CREATE_LONG_UC: // OBJ CREATE UP-CALL
							  // for long DN/RDN
			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immaevt->info.objCreate.ccbId = ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immaevt->info.objCreate.adminOwnerId =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immaevt->info.objCreate.className.size =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immaevt->info.objCreate.parentOrObjectDn.size =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
			immaevt->info.objCreate.immHandle =
			    ncs_decode_64bit(&p8);
			ncs_dec_skip_space(i_ub, 8);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 1);
			if (ncs_decode_8bit(&p8)) {
				/*Bogus pointer-val forces decode_sublevel to
				 * decode attrValues. */
				immaevt->info.objCreate.attrValues =
				    (void *)0x1;
			}
			ncs_dec_skip_space(i_ub, 1);
			break;

		case IMMA_EVT_ND2A_OI_OBJ_DELETE_UC: // OBJ DELETE UP-CALL.
		case IMMA_EVT_ND2A_OI_OBJ_DELETE_LONG_UC:
			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immaevt->info.objDelete.ccbId = ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immaevt->info.objDelete.adminOwnerId =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immaevt->info.objDelete.objectName.size =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
			immaevt->info.objDelete.immHandle =
			    ncs_decode_64bit(&p8);
			ncs_dec_skip_space(i_ub, 8);
			break;

		case IMMA_EVT_ND2A_OI_OBJ_MODIFY_UC:      // OBJ MODIFY UP-CALL.
		case IMMA_EVT_ND2A_OI_OBJ_MODIFY_LONG_UC: // OBJ MODIFY UP-CALL
							  // for long DN.
			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immaevt->info.objModify.ccbId = ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immaevt->info.objModify.adminOwnerId =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immaevt->info.objModify.objectName.size =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 1);
			if (ncs_decode_8bit(&p8)) {
				/*Bogus pointer-val forces decode_sublevel to
				 * decode attrMods. */
				immaevt->info.objModify.attrMods = (void *)0x1;
			}
			ncs_dec_skip_space(i_ub, 1);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
			immaevt->info.objModify.immHandle =
			    ncs_decode_64bit(&p8);
			ncs_dec_skip_space(i_ub, 8);
			break;

		case IMMA_EVT_ND2A_OI_CCB_COMPLETED_UC: // CCB COMPLETED
							// UP-CALL.
		case IMMA_EVT_ND2A_OI_CCB_APPLY_UC: // CCB APPLY UP-CALL.
		case IMMA_EVT_ND2A_OI_CCB_ABORT_UC: // CCB ABORT UP-CALL.
			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immaevt->info.ccbCompl.ccbId = ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immaevt->info.ccbCompl.implId = ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immaevt->info.ccbCompl.invocation =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
			immaevt->info.ccbCompl.immHandle =
			    ncs_decode_64bit(&p8);
			ncs_dec_skip_space(i_ub, 8);
			break;

		/*
		   case IMMA_EVT_ND2A_IMM_SYNC_RSP:
		   break;
		 */

		case IMMA_EVT_ND2A_IMM_CLM_NODE_LEFT:
		case IMMA_EVT_ND2A_IMM_CLM_NODE_JOINED:
			/* Noinformation is shared because of these evevts
			   encoding/decoding is not required */
			break;

		default:
			LOG_ER("Illegal IMMA message type:%u", immaevt->type);
			rc = NCSCC_RC_FAILURE;
		}
	} else if (o_evt->type == IMMSV_EVT_TYPE_IMMD) {
		IMMD_EVT *immdevt = &o_evt->info.immd;

		/*immdevt->type */
		IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
		immdevt->type = ncs_decode_32bit(&p8);
		ncs_dec_skip_space(i_ub, 4);

		/*TRACE_5("Decoding IMMD EVENT %u", immdevt->type); */

		switch (immdevt->type) {
		/* Events from IMMND */
		case IMMD_EVT_ND2D_INTRO:	    /*Identification message from IMMND to
							IMMD. */
		case IMMD_EVT_ND2D_ANNOUNCE_LOADING: /*Coordinator attempts
							start of load. */
		case IMMD_EVT_ND2D_REQ_SYNC:      /*Straggler requests sync.  */
		case IMMD_EVT_ND2D_ANNOUNCE_DUMP: /*Dump/backup invoked */
		case IMMD_EVT_ND2D_SYNC_START:    /*Coordinator wants to start
						     sync. */
		case IMMD_EVT_ND2D_SYNC_ABORT:    /*Coordinator wants to abort
						     sync. */
		case IMMD_EVT_ND2D_PBE_PRTO_PURGE_MUTATIONS: /*Coord wants to
								purge rt obj
								mutations */
		case IMMD_EVT_ND2D_LOADING_FAILED:	   /*Coord informs that loading
								failed.*/
		case IMMD_EVT_ND2D_LOADING_COMPLETED:	/* Coord informs that
								loading completed*/
			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immdevt->info.ctrl_msg.ndExecPid =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immdevt->info.ctrl_msg.epoch = ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 1);
			immdevt->info.ctrl_msg.refresh = ncs_decode_8bit(&p8);
			ncs_dec_skip_space(i_ub, 1);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 1);
			immdevt->info.ctrl_msg.pbeEnabled =
			    ncs_decode_8bit(&p8);
			ncs_dec_skip_space(i_ub, 1);

			if ((immdevt->info.ctrl_msg.pbeEnabled >= 3) &&
			    (immdevt->type ==
			     IMMD_EVT_ND2D_INTRO)) { /* extended intro */
				IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub,
							4);
				immdevt->info.ctrl_msg.dir.size =
				    ncs_decode_32bit(&p8);
				ncs_dec_skip_space(i_ub, 4);
				/* immdevt->info.ctrl_msg.dir.buf decoded by
				 * sublevel */

				IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub,
							4);
				immdevt->info.ctrl_msg.xmlFile.size =
				    ncs_decode_32bit(&p8);
				ncs_dec_skip_space(i_ub, 4);
				/* immdevt->info.ctrl_msg.xmlFile.buf decoded by
				 * sublevel */

				IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub,
							4);
				immdevt->info.ctrl_msg.pbeFile.size =
				    ncs_decode_32bit(&p8);
				ncs_dec_skip_space(i_ub, 4);
				/* immdevt->info.ctrl_msg.pbeFile.buf decoded by
				 * sublevel */
			}

			if ((immdevt->info.ctrl_msg.refresh == 2) &&
			    (immdevt->type ==
			     IMMD_EVT_ND2D_INTRO)) { /* Intro after IMMD
							restart. */

				IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub,
							8);
				immdevt->info.ctrl_msg.fevs_count =
				    ncs_decode_64bit(&p8);
				ncs_dec_skip_space(i_ub, 8);

				IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub,
							4);
				immdevt->info.ctrl_msg.admo_id_count =
				    ncs_decode_32bit(&p8);
				ncs_dec_skip_space(i_ub, 4);

				IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub,
							4);
				immdevt->info.ctrl_msg.ccb_id_count =
				    ncs_decode_32bit(&p8);
				ncs_dec_skip_space(i_ub, 4);

				IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub,
							4);
				immdevt->info.ctrl_msg.impl_count =
				    ncs_decode_32bit(&p8);
				ncs_dec_skip_space(i_ub, 4);
			}

			break;

		case IMMD_EVT_ND2D_SYNC_FEVS_BASE:
			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
			immdevt->info.syncFevsBase.client_hdl =
			    ncs_decode_64bit(&p8);
			ncs_dec_skip_space(i_ub, 8);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
			immdevt->info.syncFevsBase.fevsBase =
			    ncs_decode_64bit(&p8);
			ncs_dec_skip_space(i_ub, 8);
			break;

		case IMMD_EVT_ND2D_ADMINIT_REQ: /* AdminOwnerInitialize */
		{
			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
			immdevt->info.admown_init.client_hdl =
			    ncs_decode_64bit(&p8);
			ncs_dec_skip_space(i_ub, 8);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 2);
			size_t length = ncs_decode_16bit(&p8);
			ncs_dec_skip_space(i_ub, 2);
			if (length >= IMMSV_MAX_ADMO_NAME_LENGTH) {
				LOG_WA(
				    "Admin owner name is too long (max. 255 characters)");
				rc = NCSCC_RC_FAILURE;
				break;
			}

			/* adminOwnerName.value is top level because type is
			 * SaNameT */
			char *value = (char *)malloc(length + 1);
			if (value == NULL || ncs_decode_n_octets_from_uba(
						 i_ub, (uint8_t *)value,
						 length) != NCSCC_RC_SUCCESS) {
				free(value);
				LOG_WA(
				    "Failure inside ncs_decode_n_octets_from_uba");
				rc = NCSCC_RC_FAILURE;
				break;
			}
			value[length] = '\0';
			osaf_extended_name_steal(
			    value, &immdevt->info.admown_init.i.adminOwnerName);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 1);
			immdevt->info.admown_init.i.releaseOwnershipOnFinalize =
			    ncs_decode_8bit(&p8);
			ncs_dec_skip_space(i_ub, 1);
			break;
		}

		case IMMD_EVT_ND2D_FEVS_REQ: /*Fake EVS over Director. */
		case IMMD_EVT_ND2D_FEVS_REQ_2:
			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
			immdevt->info.fevsReq.reply_dest =
			    ncs_decode_64bit(&p8);
			ncs_dec_skip_space(i_ub, 8);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
			immdevt->info.fevsReq.client_hdl =
			    ncs_decode_64bit(&p8);
			ncs_dec_skip_space(i_ub, 8);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immdevt->info.fevsReq.msg.size = ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);
			/* immdevt->info.fevsReq.msg.buf decoded by sublevel */

			if (immdevt->type == IMMD_EVT_ND2D_FEVS_REQ_2) {
				IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub,
							1);
				immdevt->info.fevsReq.isObjSync =
				    ncs_decode_8bit(&p8);
				ncs_dec_skip_space(i_ub, 1);
			}
			break;

		case IMMD_EVT_ND2D_CCBINIT_REQ: /* CcbInitialize */
			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immdevt->info.ccb_init.adminOwnerId =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
			immdevt->info.ccb_init.ccbFlags = ncs_decode_64bit(&p8);
			ncs_dec_skip_space(i_ub, 8);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
			immdevt->info.ccb_init.client_hdl =
			    ncs_decode_64bit(&p8);
			ncs_dec_skip_space(i_ub, 8);
			break;

		case IMMD_EVT_ND2D_IMPLSET_REQ:   /*OiImplementerSet */
		case IMMD_EVT_ND2D_IMPLSET_REQ_2: /*OiImplementerSet */
			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
			immdevt->info.impl_set.reply_dest =
			    ncs_decode_64bit(&p8);
			ncs_dec_skip_space(i_ub, 8);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
			immdevt->info.impl_set.r.client_hdl =
			    ncs_decode_64bit(&p8);
			ncs_dec_skip_space(i_ub, 8);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immdevt->info.impl_set.r.impl_name.size =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);
			/* immdevt->info.impl_set.r.impl_name.buf decoded by
			 * sublevel */

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immdevt->info.impl_set.r.scope = ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			if (immdevt->type == IMMD_EVT_ND2D_IMPLSET_REQ_2) {
				IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub,
							4);
				immdevt->info.impl_set.r.oi_timeout =
				    ncs_decode_32bit(&p8);
				ncs_dec_skip_space(i_ub, 4);
			}

		/*intentional fall through - decode impl_id */
		case IMMD_EVT_ND2D_DISCARD_IMPL: /*Internal discard implementer
						    message */

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immdevt->info.impl_set.r.impl_id =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);
			break;

		case IMMD_EVT_ND2D_OI_OBJ_MODIFY: /*saImmOiRtObjectUpdate */

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
			immdevt->info.objModify.immHandle =
			    ncs_decode_64bit(&p8);
			ncs_dec_skip_space(i_ub, 8);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immdevt->info.objModify.adminOwnerId =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immdevt->info.objModify.objectName.size =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);
			/* immdevt->info.objModify.objectName.buf decoded by
			 * sublevel */

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 1);
			if (ncs_decode_8bit(&p8)) {
				/*Bogus pointer-val forces decode_sublevel to
				 * decode attrMods. */
				immdevt->info.objModify.attrMods = (void *)0x1;
			}
			ncs_dec_skip_space(i_ub, 1);
			break;

		case IMMD_EVT_ND2D_ABORT_CCB: /*Broadcast attempt to abort ccb.
					       */
			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immdevt->info.ccbId = ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);
			break;

		case IMMD_EVT_ND2D_ADMO_HARD_FINALIZE: /* Broadcast hard admo
							  finalize */
			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immdevt->info.admoId = ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);
			break;

		case IMMD_EVT_ND2D_2PBE_PRELOAD: /* 2PBE loading intialization
						  */
			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immdevt->info.pbe2.epoch = ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immdevt->info.pbe2.maxCommitTime =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immdevt->info.pbe2.maxCcbId = ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immdevt->info.pbe2.maxWeakCommitTime =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
			immdevt->info.pbe2.maxWeakCcbId = ncs_decode_64bit(&p8);
			ncs_dec_skip_space(i_ub, 8);

			break;

		case IMMD_EVT_ND2D_IMPLDELETE:
			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immdevt->info.impl_delete.size = ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);
			break;

		case IMMD_EVT_MDS_INFO:
			LOG_ER("Unexpected event over MDS->IMMD:%u",
			       immdevt->type);
		case IMMD_EVT_ND2D_ACTIVE_SET:    /*ABT may need this or something
						     similar */
		case IMMD_EVT_ND2D_IMM_SYNC_INFO: /*May need this for
						     Director<->standby sync */
		default:
			LOG_ER("Illegal IMMD message type:%u", immdevt->type);
			rc = NCSCC_RC_FAILURE;
		}
	} else if (o_evt->type == IMMSV_EVT_TYPE_IMMND) {
		IMMND_EVT *immndevt = &o_evt->info.immnd;

		/*immndevt->type */
		IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
		immndevt->type = ncs_decode_32bit(&p8);
		ncs_dec_skip_space(i_ub, 4);

		/*TRACE_5("Decoding IMMND event:%u %s",immndevt->type,
		   immsv_get_immnd_evt_name(immndevt->type)); */

		switch (immndevt->type) {

		/* Events from IMMA */

		/* IMMA commincation to the local IMMND => should have been flat
		 * encode. */
		case IMMND_EVT_A2ND_IMM_INIT:       /* ImmOm Initialization */
		case IMMND_EVT_A2ND_IMM_OI_INIT:    /* ImmOi Initialization */
		case IMMND_EVT_A2ND_IMM_CLIENTHIGH: /* For client resurrections
						     */
		case IMMND_EVT_A2ND_IMM_OM_CLIENTHIGH: /* For client
							  resurrections */
		case IMMND_EVT_A2ND_IMM_OI_CLIENTHIGH: /* For client
							  resurrections */
			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.initReq.version.releaseCode =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.initReq.version.majorVersion =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.initReq.version.minorVersion =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.initReq.client_pid =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);
			break;

		case IMMND_EVT_A2ND_IMM_FINALIZE:     /* ImmOm finalization */
		case IMMND_EVT_A2ND_IMM_OI_FINALIZE:  /* ImmOi finalization */
		case IMMND_EVT_A2ND_IMM_OM_RESURRECT: /* ImmOm resurrect hdl*/
		case IMMND_EVT_A2ND_IMM_OI_RESURRECT: /* ImmOi resurrect hdl*/
		case IMMND_EVT_A2ND_SYNC_FINALIZE:    /* immsv_finalize_sync */
		case IMMND_EVT_A2ND_CL_TIMEOUT: /* lib timeout on sync call */
			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
			immndevt->info.finReq.client_hdl =
			    ncs_decode_64bit(&p8);
			ncs_dec_skip_space(i_ub, 8);
			break;

		case IMMND_EVT_A2ND_IMM_ADMINIT: /* AdminOwnerInitialize */
		{
			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
			immndevt->info.adminitReq.client_hdl =
			    ncs_decode_64bit(&p8);
			ncs_dec_skip_space(i_ub, 8);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 2);
			size_t length = ncs_decode_16bit(&p8);
			ncs_dec_skip_space(i_ub, 2);
			if (length >= IMMSV_MAX_ADMO_NAME_LENGTH) {
				LOG_WA(
				    "Admin owner name is too long (max. 255 characters)");
				rc = NCSCC_RC_FAILURE;
				break;
			}

			/* adminOwnerName.value is top level because type is
			 * SaNameT */
			char *value = (char *)malloc(length + 1);
			if (value == NULL || ncs_decode_n_octets_from_uba(
						 i_ub, (uint8_t *)value,
						 length) != NCSCC_RC_SUCCESS) {
				free(value);
				LOG_WA(
				    "Failure inside ncs_decode_n_octets_from_uba");
				rc = NCSCC_RC_FAILURE;
				break;
			}
			value[length] = '\0';
			osaf_extended_name_steal(
			    value, &immndevt->info.adminitReq.i.adminOwnerName);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 1);
			immndevt->info.adminitReq.i.releaseOwnershipOnFinalize =
			    ncs_decode_8bit(&p8);
			ncs_dec_skip_space(i_ub, 1);
			break;
		}

		case IMMND_EVT_A2ND_IMM_FEVS: /* Fake EVS msg from Agent
						 (forward) */
		case IMMND_EVT_A2ND_IMM_FEVS_2:
		case IMMND_EVT_D2ND_GLOB_FEVS_REQ: /* Fake EVS msg from director
						      (consume) */
		case IMMND_EVT_D2ND_GLOB_FEVS_REQ_2:

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
			immndevt->info.fevsReq.sender_count =
			    ncs_decode_64bit(&p8);
			ncs_dec_skip_space(i_ub, 8);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
			immndevt->info.fevsReq.reply_dest =
			    ncs_decode_64bit(&p8);
			ncs_dec_skip_space(i_ub, 8);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
			immndevt->info.fevsReq.client_hdl =
			    ncs_decode_64bit(&p8);
			ncs_dec_skip_space(i_ub, 8);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.fevsReq.msg.size = ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);
			/* immndevt->info.fevsReq.msg.buf decoded by decode
			 * sublevel */

			if ((immndevt->type == IMMND_EVT_A2ND_IMM_FEVS_2) ||
			    (immndevt->type ==
			     IMMND_EVT_D2ND_GLOB_FEVS_REQ_2)) {
				IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub,
							1);
				immndevt->info.fevsReq.isObjSync =
				    ncs_decode_8bit(&p8);
				ncs_dec_skip_space(i_ub, 1);
			}
			break;

		case IMMND_EVT_A2ND_CCBINIT: /* CcbInitialize */
			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.ccbinitReq.adminOwnerId =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
			immndevt->info.ccbinitReq.ccbFlags =
			    ncs_decode_64bit(&p8);
			ncs_dec_skip_space(i_ub, 8);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
			immndevt->info.ccbinitReq.client_hdl =
			    ncs_decode_64bit(&p8);
			ncs_dec_skip_space(i_ub, 8);
			break;

		case IMMND_EVT_A2ND_ACCESSOR_GET:  /* AccessorGet */
		case IMMND_EVT_A2ND_SEARCHINIT:    /* SearchInitialize */
		case IMMND_EVT_A2ND_OBJ_SAFE_READ: /* saImmOmCcbObjectRead */
			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
			immndevt->info.searchInit.client_hdl =
			    ncs_decode_64bit(&p8);
			ncs_dec_skip_space(i_ub, 8);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.searchInit.rootName.size =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.searchInit.scope = ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
			immndevt->info.searchInit.searchOptions =
			    ncs_decode_64bit(&p8);
			ncs_dec_skip_space(i_ub, 8);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.searchInit.searchParam.present =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			if (immndevt->info.searchInit.searchParam.present ==
				ImmOmSearchParameter_PR_oneAttrParam ||
			    immndevt->info.searchInit.searchParam.present ==
				ImmOmSearchParameter_PR_nonExtendedName_oneAttrParam) {
				IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub,
							4);
				immndevt->info.searchInit.searchParam.choice
				    .oneAttrParam.attrName.size =
				    ncs_decode_32bit(&p8);
				ncs_dec_skip_space(i_ub, 4);

				IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub,
							4);
				immndevt->info.searchInit.searchParam.choice
				    .oneAttrParam.attrValueType =
				    ncs_decode_32bit(&p8);
				ncs_dec_skip_space(i_ub, 4);
			}

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 1);
			if (ncs_decode_8bit(&p8)) {
				/*Bogus pointer-val forces decode_sublevel to
				 * decode attributeNames. */
				immndevt->info.searchInit.attributeNames =
				    (void *)0x1;
			}
			ncs_dec_skip_space(i_ub, 1);

			if (immndevt->type == IMMND_EVT_A2ND_OBJ_SAFE_READ) {
				IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub,
							4);
				immndevt->info.searchInit.ccbId =
				    ncs_decode_32bit(&p8);
				ncs_dec_skip_space(i_ub, 4);
			}
			break;

		case IMMND_EVT_A2ND_SEARCHNEXT:     /* SearchNext */
		case IMMND_EVT_A2ND_SEARCHFINALIZE: /* SearchFinalize */
			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
			immndevt->info.searchOp.client_hdl =
			    ncs_decode_64bit(&p8);
			ncs_dec_skip_space(i_ub, 8);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.searchOp.searchId =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);
			break;

		/*
		   case IMMND_EVT_A2ND_SEARCH_REMOTE:
		 */

		case IMMND_EVT_A2ND_RT_ATT_UPPD_RSP: /* reply for fetch of rt
							attr vals */
			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
			immndevt->info.rtAttUpdRpl.sr.client_hdl =
			    ncs_decode_64bit(&p8);
			ncs_dec_skip_space(i_ub, 8);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.rtAttUpdRpl.sr.requestNodeId =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.rtAttUpdRpl.sr.remoteNodeId =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.rtAttUpdRpl.sr.searchId =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.rtAttUpdRpl.sr.objectName.size =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 1);
			if (ncs_decode_8bit(&p8)) {
				/*Bogus pointer-val forces decode_sublevel to
				   decode attributeNames. */
				immndevt->info.rtAttUpdRpl.sr.attributeNames =
				    (void *)0x1;
			}
			ncs_dec_skip_space(i_ub, 1);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.rtAttUpdRpl.result =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);
			break;

		case IMMND_EVT_A2ND_PBE_ADMOP_RSP:     /* AdminOperation sync local
							  Reply */
		case IMMND_EVT_A2ND_ADMOP_RSP:	 /* AdminOperation sync local
							  Reply */
		case IMMND_EVT_A2ND_ASYNC_ADMOP_RSP:   /* AdminOperation async
							  local Reply */
		case IMMND_EVT_A2ND_ADMOP_RSP_2:       /* AdminOperation sync local
							  Reply - extended */
		case IMMND_EVT_A2ND_ASYNC_ADMOP_RSP_2: /* AdminOperation async
							  local Reply - extended
							*/
			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
			immndevt->info.admOpRsp.oi_client_hdl =
			    ncs_decode_64bit(&p8);
			ncs_dec_skip_space(i_ub, 8);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
			immndevt->info.admOpRsp.invocation =
			    ncs_decode_64bit(&p8);
			ncs_dec_skip_space(i_ub, 8);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.admOpRsp.result = ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.admOpRsp.error = ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			immndevt->info.admOpRsp.parms = NULL;

			if ((immndevt->type == IMMND_EVT_A2ND_ADMOP_RSP_2) ||
			    (immndevt->type ==
			     IMMND_EVT_A2ND_ASYNC_ADMOP_RSP_2)) {
				IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub,
							1);
				if (ncs_decode_8bit(&p8)) {
					/*Bogus pointer-val forces
					 * decode_sublevel to decode params. */
					immndevt->info.admOpRsp.parms =
					    (void *)0x1;
				}
				ncs_dec_skip_space(i_ub, 1);
			}
			break;

		case IMMND_EVT_A2ND_CLASS_DESCR_GET: /* saImmOmClassDescriptionGet
						      */
			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.classDescr.className.size =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);
			break;

		case IMMND_EVT_A2ND_OI_IMPL_SET:   /* saImmOiImplementerSet */
		case IMMND_EVT_A2ND_OI_IMPL_SET_2: /* saImmOiImplementerSet */
			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
			immndevt->info.implSet.client_hdl =
			    ncs_decode_64bit(&p8);
			ncs_dec_skip_space(i_ub, 8);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.implSet.impl_name.size =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);
			/* immndevt->info.implSet.impl_name.buf decoded by
			 * sublevel */

			/*skip scope & impl_id */
			if (immndevt->type == IMMND_EVT_A2ND_OI_IMPL_SET_2) {
				IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub,
							4);
				immndevt->info.implSet.oi_timeout =
				    ncs_decode_32bit(&p8);
				ncs_dec_skip_space(i_ub, 4);
			}
			break;

		/*Fevs call IMMA->IMMD->IMMNDs have to suport non-flat encoding
		 */
		case IMMND_EVT_A2ND_OBJ_CREATE:    /* saImmOmCcbObjectCreate */
		case IMMND_EVT_A2ND_OI_OBJ_CREATE: /* saImmOiRtObjectCreate */
		case IMMND_EVT_A2ND_OBJ_CREATE_2:  /* saImmOmCcbObjectCreate_o3
						    */
		case IMMND_EVT_A2ND_OI_OBJ_CREATE_2: /* saImmOiRtObjectCreate_o3
						      */

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.objCreate.ccbId = ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.objCreate.adminOwnerId =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.objCreate.className.size =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);
			/* immndevt->info.objCreate.className.buf decoded by
			 * sublevel */

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.objCreate.parentOrObjectDn.size =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);
			/* immndevt->info.objCreate.parentName.buf decoded by
			 * sublevel */

			/* immhandle field not used for these immnd messages
			   IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
			   immndevt->info.objCreate.immHandle =
			   ncs_decode_64bit(&p8); ncs_dec_skip_space(i_ub, 8);
			 */

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 1);
			if (ncs_decode_8bit(&p8)) {
				/*Bogus pointer-val forces decode_sublevel to
				 * decode attrValues. */
				immndevt->info.objCreate.attrValues =
				    (void *)0x1;
			}
			ncs_dec_skip_space(i_ub, 1);
			break;

		case IMMND_EVT_A2ND_OBJ_MODIFY:    /* saImmOmCcbObjectModify */
						   /* IMMND_EVT_A2ND_OI_OBJ_MODIFY is "converted" to fevs by
						    * immd_evt.c */
		case IMMND_EVT_A2ND_OI_OBJ_MODIFY: /* saImmOiRtObjectUpdate */
			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
			immndevt->info.objModify.immHandle =
			    ncs_decode_64bit(&p8);
			ncs_dec_skip_space(i_ub, 8);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.objModify.ccbId = ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.objModify.adminOwnerId =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.objModify.objectName.size =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);
			/* immndevt->info.objModify.objectName.buf decoded by
			 * sublevel */

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 1);
			if (ncs_decode_8bit(&p8)) {
				/*Bogus pointer-val forces decode_sublevel to
				 * decode attrMods. */
				immndevt->info.objModify.attrMods = (void *)0x1;
			}
			ncs_dec_skip_space(i_ub, 1);
			break;

		case IMMND_EVT_A2ND_OBJ_DELETE:    /* saImmOmCcbObjectDelete */
		case IMMND_EVT_A2ND_OI_OBJ_DELETE: /* saImmOiRtObjectDelete */
		case IMMND_EVT_A2ND_AUG_ADMO:      /* Inform IMMNDs of admo for
						      augment ccb*/
			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.objDelete.ccbId = ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.objDelete.adminOwnerId =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.objDelete.objectName.size =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);
			/* immndevt->info.objDelete.objectName.buf decoded by
			 * sublevel */

			/*immndevt->info.objDelete.immHandle not used in these
			 * calls */
			break;

		case IMMND_EVT_A2ND_CCB_APPLY:    /* saImmOmCcbApply */
		case IMMND_EVT_A2ND_CCB_FINALIZE: /* saImmOmCcbFinalize */
		case IMMND_EVT_A2ND_RECOVER_CCB_OUTCOME:
		case IMMND_EVT_A2ND_CCB_VALIDATE: /* saImmOmCcbValidate */
			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.ccbId = ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);
			break;

		case IMMND_EVT_A2ND_IMM_ADMOP:       /* Syncronous AdminOp */
		case IMMND_EVT_A2ND_IMM_ADMOP_ASYNC: /* Asyncronous AdminOp */
			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.admOpReq.adminOwnerId =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
			immndevt->info.admOpReq.operationId =
			    ncs_decode_64bit(&p8);
			ncs_dec_skip_space(i_ub, 8);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
			immndevt->info.admOpReq.continuationId =
			    ncs_decode_64bit(&p8);
			ncs_dec_skip_space(i_ub, 8);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
			immndevt->info.admOpReq.timeout = ncs_decode_64bit(&p8);
			ncs_dec_skip_space(i_ub, 8);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.admOpReq.invocation =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.admOpReq.objectName.size =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);
			/* immndevt->info.admOpReq.objectName.buf decoded by
			 * sublevel */

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 1);
			if (ncs_decode_8bit(&p8)) {
				/*Bogus pointer-val forces decode_sublevel to
				 * decode params. */
				immndevt->info.admOpReq.params = (void *)0x1;
			}
			ncs_dec_skip_space(i_ub, 1);
			break;

		case IMMND_EVT_A2ND_CLASS_CREATE: /* saImmOmClassCreate */
		case IMMND_EVT_A2ND_CLASS_DELETE: /* saImmOmClassDelete */
			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.classDescr.className.size =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);
			/* immndevt->info.classDescr.className.buf decoded by
			 * sublevel */

			if (immndevt->type == IMMND_EVT_A2ND_CLASS_CREATE) {
				IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub,
							4);
				immndevt->info.classDescr.classCategory =
				    ncs_decode_32bit(&p8);
				ncs_dec_skip_space(i_ub, 4);

				IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub,
							1);
				if (ncs_decode_8bit(&p8)) {
					/*Bogus pointer-val forces
					   decode_sublevel to decode
					   attrDefinitions. */
					immndevt->info.classDescr
					    .attrDefinitions = (void *)0x1;
				}
				ncs_dec_skip_space(i_ub, 1);
			}
			break;

		case IMMND_EVT_A2ND_OBJ_SYNC: /* immsv_sync */
		case IMMND_EVT_A2ND_OBJ_SYNC_2:
			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.obj_sync.className.size =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);
			/* immndevt->info.obj_sync.className.buf decoded by
			 * sublevel */

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.obj_sync.objectName.size =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);
			/* immndevt->info.obj_sync.objectName.buf decoded by
			 * sublevel */

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 1);
			if (ncs_decode_8bit(&p8)) {
				/*Bogus pointer-val forces decode_sublevel to
				   decode attrValues. */
				immndevt->info.obj_sync.attrValues =
				    (void *)0x1;
			}
			ncs_dec_skip_space(i_ub, 1);
			break;

		case IMMND_EVT_A2ND_ADMO_SET:     /* AdminOwnerSet */
		case IMMND_EVT_A2ND_ADMO_RELEASE: /* AdminOwnerRelease */
		case IMMND_EVT_A2ND_ADMO_CLEAR:   /* AdminOwnerClear */
			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.admReq.adm_owner_id =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.admReq.scope = ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			/* objectNames decoded by sublevel. */
			break;

		case IMMND_EVT_A2ND_ADMO_FINALIZE: /* AdminOwnerFinalize */
			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.admFinReq.adm_owner_id =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);
			break;

		case IMMND_EVT_A2ND_OI_IMPL_CLR: /* saImmOiImplementerClear */
		case IMMND_EVT_A2ND_OI_CL_IMPL_SET:  /* saImmOiClassImplementerSet
						      */
		case IMMND_EVT_A2ND_OI_CL_IMPL_REL:  /* saImmOiClassImplementerRelease
						      */
		case IMMND_EVT_A2ND_OI_OBJ_IMPL_SET: /* saImmOiObjectImplementerSet
						      */
		case IMMND_EVT_A2ND_OI_OBJ_IMPL_REL: /* saImmOiObjectImplementerRelease
						      */
			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
			immndevt->info.implSet.client_hdl =
			    ncs_decode_64bit(&p8);
			ncs_dec_skip_space(i_ub, 8);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.implSet.impl_id = ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			if (immndevt->type != IMMND_EVT_A2ND_OI_IMPL_CLR) {
				IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub,
							4);
				immndevt->info.implSet.impl_name.size =
				    ncs_decode_32bit(&p8);
				ncs_dec_skip_space(i_ub, 4);
				/* immndevt->info.implSet.impl_name.buf decoded
				 * by sublevel */
				if ((immndevt->type ==
				     IMMND_EVT_A2ND_OI_OBJ_IMPL_SET) ||
				    (immndevt->type ==
				     IMMND_EVT_A2ND_OI_OBJ_IMPL_REL)) {
					IMMSV_FLTN_SPACE_ASSERT(p8, local_data,
								i_ub, 4);
					immndevt->info.implSet.scope =
					    ncs_decode_32bit(&p8);
					ncs_dec_skip_space(i_ub, 4);
				}
			}
			break;

		case IMMND_EVT_A2ND_CCB_COMPLETED_RSP:		    /* CcbCompleted local
								       Reply */
		case IMMND_EVT_A2ND_CCB_COMPLETED_RSP_2:	    /* CcbCompleted local
								       Reply */
		case IMMND_EVT_A2ND_CCB_OBJ_CREATE_RSP:		    /*CcbObjCreate local
								       Reply */
		case IMMND_EVT_A2ND_CCB_OBJ_CREATE_RSP_2:	   /*CcbObjCreate local
								       Reply */
		case IMMND_EVT_A2ND_CCB_OBJ_MODIFY_RSP:		    /*CcbObjModify local
								       Reply */
		case IMMND_EVT_A2ND_CCB_OBJ_MODIFY_RSP_2:	   /*CcbObjModify local
								       Reply */
		case IMMND_EVT_A2ND_CCB_OBJ_DELETE_RSP:		    /*CcbObjDelete local
								       Reply */
		case IMMND_EVT_A2ND_CCB_OBJ_DELETE_RSP_2:	   /*CcbObjDelete local
								       Reply */
		case IMMND_EVT_A2ND_PBE_PRT_OBJ_CREATE_RSP:	 /* Pbe OI rt obj
								       create response
								     */
		case IMMND_EVT_A2ND_PBE_PRTO_DELETES_COMPLETED_RSP: /*Pbe PRTO
								       deletes
								       done */
		case IMMND_EVT_A2ND_PBE_PRT_ATTR_UPDATE_RSP:	/* Pbe OI rt attr
								       update
								       response*/
		case IMMND_EVT_A2ND_OI_CCB_AUG_INIT:		    /*OI augments CCB inside
								       ccb upcall. #1963 */
		{
			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
			immndevt->info.ccbUpcallRsp.oi_client_hdl =
			    ncs_decode_64bit(&p8);
			ncs_dec_skip_space(i_ub, 8);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.ccbUpcallRsp.ccbId =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.ccbUpcallRsp.implId =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.ccbUpcallRsp.inv = ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.ccbUpcallRsp.result =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 2);
			size_t length = ncs_decode_16bit(&p8);
			ncs_dec_skip_space(i_ub, 2);

			osaf_extended_name_clear(
			    &immndevt->info.ccbUpcallRsp.name);
			if ((immndevt->type ==
			     IMMND_EVT_A2ND_CCB_OBJ_DELETE_RSP) ||
			    (immndevt->type ==
			     IMMND_EVT_A2ND_CCB_OBJ_DELETE_RSP_2) ||
			    (immndevt->type ==
			     IMMND_EVT_A2ND_OI_CCB_AUG_INIT)) {
				/* name.value is top level because type is
				 * SaNameT */
				if (length) {
					char *value =
					    (char *)malloc(length + 1);
					if (value == NULL ||
					    ncs_decode_n_octets_from_uba(
						i_ub, (uint8_t *)value,
						length) != NCSCC_RC_SUCCESS) {
						free(value);
						LOG_WA(
						    "Failure inside ncs_decode_n_octets_from_uba");
						rc = NCSCC_RC_FAILURE;
						break;
					}
					value[length] = '\0';
					osaf_extended_name_steal(
					    value,
					    &immndevt->info.ccbUpcallRsp.name);
				}
			}

			if ((immndevt->type ==
			     IMMND_EVT_A2ND_CCB_COMPLETED_RSP_2) ||
			    (immndevt->type ==
			     IMMND_EVT_A2ND_CCB_OBJ_CREATE_RSP_2) ||
			    (immndevt->type ==
			     IMMND_EVT_A2ND_CCB_OBJ_DELETE_RSP_2) ||
			    (immndevt->type ==
			     IMMND_EVT_A2ND_CCB_OBJ_MODIFY_RSP_2)) {
				IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub,
							4);
				immndevt->info.ccbUpcallRsp.errorString.size =
				    ncs_decode_32bit(&p8);
				ncs_dec_skip_space(i_ub, 4);
				/* immndevt->info.ccbUpcallRsp.errorString.buf
				 * decoded by sublevel */
			}

			break;
		}

		/* Events IMMND->IMMND (asyncronous) */
		case IMMND_EVT_ND2ND_ADMOP_RSP:		/* AdminOperation sync fevs
							   Reply */
		case IMMND_EVT_ND2ND_ADMOP_RSP_2:       /* AdminOperation sync fevs
							   Reply */
		case IMMND_EVT_ND2ND_ASYNC_ADMOP_RSP:   /* AdminOperation async
							   fevs Reply */
		case IMMND_EVT_ND2ND_ASYNC_ADMOP_RSP_2: /* AdminOperation async
							   fevs Reply */

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
			immndevt->info.admOpRsp.oi_client_hdl =
			    ncs_decode_64bit(&p8);
			ncs_dec_skip_space(i_ub, 8);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
			immndevt->info.admOpRsp.invocation =
			    ncs_decode_64bit(&p8);
			ncs_dec_skip_space(i_ub, 8);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.admOpRsp.result = ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.admOpRsp.error = ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			immndevt->info.admOpRsp.parms = NULL;

			if ((immndevt->type == IMMND_EVT_ND2ND_ADMOP_RSP_2) ||
			    (immndevt->type ==
			     IMMND_EVT_ND2ND_ASYNC_ADMOP_RSP_2)) {
				IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub,
							1);
				if (ncs_decode_8bit(&p8)) {
					/*Bogus pointer-val forces
					 * decode_sublevel to decode params. */
					immndevt->info.admOpRsp.parms =
					    (void *)0x1;
				}
				ncs_dec_skip_space(i_ub, 1);
			}
			break;

		case IMMND_EVT_ND2ND_SYNC_FINALIZE_2:
		case IMMND_EVT_ND2ND_SYNC_FINALIZE: /* Sync finalize from coord
						       over fevs */

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.finSync.lastContinuationId =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 1);
			if (ncs_decode_8bit(&p8)) {
				/*Bogus pointer-val forces decode_sublevel to
				   decode adminOwners. */
				immndevt->info.finSync.adminOwners =
				    (void *)0x1;
			}
			ncs_dec_skip_space(i_ub, 1);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 1);
			if (ncs_decode_8bit(&p8)) {
				/*Bogus pointer-val forces decode_sublevel to
				   decode implementers. */
				immndevt->info.finSync.implementers =
				    (void *)0x1;
			}
			ncs_dec_skip_space(i_ub, 1);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 1);
			if (ncs_decode_8bit(&p8)) {
				/*Bogus pointer-val forces decode_sublevel to
				   decode classes. */
				immndevt->info.finSync.classes = (void *)0x1;
			}
			ncs_dec_skip_space(i_ub, 1);

			if (immndevt->type == IMMND_EVT_ND2ND_SYNC_FINALIZE_2) {
				TRACE(
				    "Decoding top level for IMMND_EVT_ND2ND_SYNC_FINALIZE_2");
				/*Start marker always explicitly decoded. */
			}
			break;

		case IMMND_EVT_ND2ND_SEARCH_REMOTE: /* Forward search request
						       from impl ND */

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
			immndevt->info.searchRemote.client_hdl =
			    ncs_decode_64bit(&p8);
			ncs_dec_skip_space(i_ub, 8);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.searchRemote.requestNodeId =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.searchRemote.remoteNodeId =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.searchRemote.searchId =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.searchRemote.objectName.size =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);
			/* immndevt->info.searchRemote.objectName.buf decoded by
			 * sublevel */

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 1);
			if (ncs_decode_8bit(&p8)) {
				/*Bogus pointer-val forces decode_sublevel to
				   decode attributeNames. */
				immndevt->info.searchRemote.attributeNames =
				    (void *)0x1;
			}
			ncs_dec_skip_space(i_ub, 1);
			break;

		case IMMND_EVT_ND2ND_SEARCH_REMOTE_RSP: /*Forward search req
							   from impl ND */

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.rspSrchRmte.result =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.rspSrchRmte.requestNodeId =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.rspSrchRmte.remoteNodeId =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.rspSrchRmte.searchId =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.rspSrchRmte.runtimeAttrs.objectName
			    .size = ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);
			/* immndevt->info.rspSrchRmte.objectName.buf decoded by
			 * sublevel */

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 1);
			if (ncs_decode_8bit(&p8)) {
				/*Bogus pointer-val forces decode_sublevel to
				   decode attrValuesList. */
				immndevt->info.rspSrchRmte.runtimeAttrs
				    .attrValuesList = (void *)0x1;
			}
			ncs_dec_skip_space(i_ub, 1);
			break;

		/* Events from IMMD to IMMND */
		case IMMND_EVT_D2ND_INTRO_RSP:
		case IMMND_EVT_D2ND_SYNC_REQ:
		case IMMND_EVT_D2ND_LOADING_OK:
		case IMMND_EVT_D2ND_SYNC_START:
		case IMMND_EVT_D2ND_SYNC_ABORT:
		case IMMND_EVT_D2ND_DUMP_OK:
		case IMMND_EVT_D2ND_DISCARD_NODE: /* Crashed IMMND or node,
						     bcast to NDs */
		case IMMND_EVT_D2ND_PBE_PRTO_PURGE_MUTATIONS:

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.ctrl.nodeId = ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.ctrl.rulingEpoch = ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
			immndevt->info.ctrl.fevsMsgStart =
			    ncs_decode_64bit(&p8);
			ncs_dec_skip_space(i_ub, 8);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.ctrl.ndExecPid = ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 1);
			immndevt->info.ctrl.canBeCoord = ncs_decode_8bit(&p8);
			ncs_dec_skip_space(i_ub, 1);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 1);
			if (ncs_decode_8bit(&p8)) {
				immndevt->info.ctrl.isCoord = true;
			}
			ncs_dec_skip_space(i_ub, 1);

			/*syncStarted & nodeEpoch not really used D->ND,
			   only D->D mbcp
			 */
			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 1);
			if (ncs_decode_8bit(&p8)) {
				immndevt->info.ctrl.syncStarted = true;
			}
			ncs_dec_skip_space(i_ub, 1);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.ctrl.nodeEpoch = ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);
			break;

		case IMMND_EVT_D2ND_SYNC_FEVS_BASE:
			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
			immndevt->info.syncFevsBase = ncs_decode_64bit(&p8);
			ncs_dec_skip_space(i_ub, 8);
			break;

		case IMMND_EVT_D2ND_ADMINIT: /* Admin Owner init reply */
		{
			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.adminitGlobal.globalOwnerId =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 2);
			size_t length = ncs_decode_16bit(&p8);
			ncs_dec_skip_space(i_ub, 2);

			/* adminOwnerName.value is top level because type is
			 * SaNameT */
			char *value = (char *)malloc(length + 1);
			if (value == NULL || ncs_decode_n_octets_from_uba(
						 i_ub, (uint8_t *)value,
						 length) != NCSCC_RC_SUCCESS) {
				free(value);
				LOG_ER(
				    "Failure inside ncs_decode_n_octets_from_uba");
				rc = NCSCC_RC_FAILURE;
				break;
			}
			value[length] = '\0';
			osaf_extended_name_steal(
			    value,
			    &immndevt->info.adminitGlobal.i.adminOwnerName);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 1);
			immndevt->info.adminitGlobal.i
			    .releaseOwnershipOnFinalize = ncs_decode_8bit(&p8);
			ncs_dec_skip_space(i_ub, 1);
			break;
		}

		case IMMND_EVT_D2ND_CCBINIT: /* Ccb init reply */
			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.ccbinitGlobal.globalCcbId =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.ccbinitGlobal.i.adminOwnerId =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
			immndevt->info.ccbinitGlobal.i.ccbFlags =
			    ncs_decode_64bit(&p8);
			ncs_dec_skip_space(i_ub, 8);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
			immndevt->info.ccbinitGlobal.i.client_hdl =
			    ncs_decode_64bit(&p8);
			ncs_dec_skip_space(i_ub, 8);
			break;

		case IMMND_EVT_D2ND_IMPLSET_RSP: /* Implter set reply from D
						    with impl id */
		case IMMND_EVT_D2ND_IMPLSET_RSP_2:
			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 8);
			immndevt->info.implSet.client_hdl =
			    ncs_decode_64bit(&p8);
			ncs_dec_skip_space(i_ub, 8);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.implSet.impl_id = ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.implSet.impl_name.size =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);
			/* immndevt->info.implSet.impl_name.buf decoded by
			 * sublevel */

			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.implSet.scope = ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);

			if (immndevt->type == IMMND_EVT_D2ND_IMPLSET_RSP_2) {
				IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub,
							4);
				immndevt->info.implSet.oi_timeout =
				    ncs_decode_32bit(&p8);
				ncs_dec_skip_space(i_ub, 4);
			}
			break;

		case IMMND_EVT_D2ND_DISCARD_IMPL: /* Discard implementer
						     broadcast to NDs */
			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.implSet.impl_id = ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);
			break;

		case IMMND_EVT_D2ND_ABORT_CCB: /* Abort CCB broadcast to NDs */
			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.ccbId = ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);
			break;

		case IMMND_EVT_D2ND_ADMO_HARD_FINALIZE: /* Admo hard fin bcast
							   to NDs */
			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.admFinReq.adm_owner_id =
			    ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);
			break;

		case IMMND_EVT_D2ND_IMPLDELETE:
			IMMSV_FLTN_SPACE_ASSERT(p8, local_data, i_ub, 4);
			immndevt->info.impl_delete.size = ncs_decode_32bit(&p8);
			ncs_dec_skip_space(i_ub, 4);
			break;

		case IMMND_EVT_D2ND_RESET:
			/* message has no contents */
			break;

		case IMMND_EVT_MDS_INFO: /* IMMA/IMMND/IMMD UP/DOWN Info */
		case IMMND_EVT_TIME_OUT: /* Time out event */
		case IMMND_EVT_CB_DUMP:
			LOG_ER("Unexpected event over MDS->IMMND:%u",
			       immndevt->type);
			break;
		default:
			LOG_ER("Illegal IMMND message type:%u", immndevt->type);
			rc = NCSCC_RC_FAILURE;
		}
	} else {
		LOG_WA("Illegal event message type or format");
		rc = NCSCC_RC_FAILURE;
	}
	/*TRACE_LEAVE(); */
	return rc;
}

/****************************************************************************\
 PROCEDURE NAME : immsv_evt_enc

 DESCRIPTION    : Encodes the contents of IMMSV_EVT into userbuf
		  Both top level and sub-levels are encoded with
		  byte-order correction.

 ARGUMENTS      : *i_evt - Event Struct.
		   *o_ub  - User Buff.

 RETURNS        : None
\*****************************************************************************/
uint32_t immsv_evt_enc(IMMSV_EVT *i_evt, NCS_UBAID *o_ub)
{
	uint32_t rc;

	rc = immsv_evt_enc_toplevel(i_evt, o_ub);
	if (rc == NCSCC_RC_SUCCESS) {
		rc = immsv_evt_enc_sublevels(i_evt, o_ub);
	}

	return rc;
}

/****************************************************************************\
 PROCEDURE NAME : immsv_evt_dec

 DESCRIPTION    : Decodes the contents of IMMSV_EVT from user buf
		  Inverse of immsv_evt_enc.

 ARGUMENTS      : *i_evt - Event Struct.
		  *o_ub  - User Buff.

 RETURNS        : None

 NOTES          :
\*****************************************************************************/
uint32_t immsv_evt_dec(NCS_UBAID *i_ub, IMMSV_EVT *o_evt)
{
	uint32_t rc;

	rc = immsv_evt_dec_toplevel(i_ub, o_evt);
	if (rc == NCSCC_RC_SUCCESS) {
		rc = immsv_evt_dec_sublevels(i_ub, o_evt);
	}

	return rc;
}

void immsv_msg_trace_send(MDS_DEST to, IMMSV_EVT *evt)
{
	if (evt->type == IMMSV_EVT_TYPE_IMMD) {
		TRACE_8("Sending:  %s to %x",
			immsv_get_immd_evt_name(evt->info.immd.type),
			m_NCS_NODE_ID_FROM_MDS_DEST(to));
	}

	if (evt->type == IMMSV_EVT_TYPE_IMMND) {
		TRACE_8("Sending:  %s to %x",
			immsv_get_immnd_evt_name(evt->info.immnd.type),
			m_NCS_NODE_ID_FROM_MDS_DEST(to));
	}
}

void immsv_msg_trace_rec(MDS_DEST from, IMMSV_EVT *evt)
{
	if (evt->type == IMMSV_EVT_TYPE_IMMD) {
		TRACE_8("Received: %s from %x",
			immsv_get_immd_evt_name(evt->info.immd.type),
			m_NCS_NODE_ID_FROM_MDS_DEST(from));
	}

	if (evt->type == IMMSV_EVT_TYPE_IMMND) {
		TRACE_8("Received: %s (%u) from %x",
			immsv_get_immnd_evt_name(evt->info.immnd.type),
			evt->info.immnd.type,
			m_NCS_NODE_ID_FROM_MDS_DEST(from));
	}
}
