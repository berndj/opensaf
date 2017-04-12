/*      -*- OpenSAF  -*-
 *
 * (C) Copyright 2008 The OpenSAF Foundation
 * Copyright Ericsson AB 2017 - All Rights Reserved.
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

  This file contains utility routines for managing encode/decode operations
  in AvSv components.
..............................................................................

  FUNCTIONS INCLUDED in this module:


******************************************************************************
*/

#include "amf/common/amf.h"
#include "amf/common/amf_d2nmsg.h"

/* AVD-AVND common EDPs */
#include "amf/common/amf_d2nedu.h"
#include "amf/common/amf_eduutil.h"

/*****************************************************************************

  PROCEDURE NAME:   avsv_edp_dnd_msg

  DESCRIPTION:      EDU program handler for "AVSV_DND_MSG" data. This
		    function is invoked by EDU for performing encode/decode
		    operation on "AVSV_DND_MSG" data.

  RETURNS:          NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

*****************************************************************************/
uint32_t avsv_edp_dnd_msg(EDU_HDL *hdl, EDU_TKN *edu_tkn, NCSCONTEXT ptr,
			  uint32_t *ptr_data_len, EDU_BUF_ENV *buf_env,
			  EDP_OP_TYPE op, EDU_ERR *o_err)
{
	uint32_t rc = NCSCC_RC_SUCCESS;
	AVSV_DND_MSG *struct_ptr = NULL, **d_ptr = NULL;
	uint16_t ver3 = AVSV_AVD_AVND_MSG_FMT_VER_3;
	uint16_t ver5 = AVSV_AVD_AVND_MSG_FMT_VER_5;
	uint16_t ver6 = AVSV_AVD_AVND_MSG_FMT_VER_6;

	EDU_INST_SET avsv_dnd_msg_rules[] = {
	    {EDU_START, avsv_edp_dnd_msg, 0, 0, 0, sizeof(AVSV_DND_MSG), 0,
	     NULL},

	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_type, 0, NULL},
	    {EDU_TEST, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_type, 0,
	     avsv_dnd_msg_test_type_fnc},

	    /* AVSV_N2D_NODE_UP_MSG_INFO */
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.n2d_node_up.msg_id, 0, NULL},
	    {EDU_VER_GE, NULL, 0, 0, 2, 0, 0,
	     (EDU_EXEC_RTINE)((uint16_t *)(&(ver6)))},
	    {EDU_EXEC, ncs_edp_ncs_bool, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.n2d_node_up.leds_set, 0,
	     NULL},
	    {EDU_EXEC, m_NCS_EDP_SACLMNODEIDT, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.n2d_node_up.node_id, 0, NULL},
	    {EDU_VER_GE, NULL, 0, 0, 2, 0, 0,
	     (EDU_EXEC_RTINE)((uint16_t *)(&(ver6)))},
	    {EDU_EXEC, ncs_edp_sanamet, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.n2d_node_up.node_name, 0,
	     NULL},
	    {EDU_EXEC, ncs_edp_mds_dest, 0, 0, EDU_EXIT,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.n2d_node_up.adest_address, 0,
	     NULL},

	    /* AVSV_N2D_REG_SU_MSG_INFO */
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.n2d_reg_su.msg_id, 0, NULL},
	    {EDU_EXEC, m_NCS_EDP_SACLMNODEIDT, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.n2d_reg_su.node_id, 0, NULL},
	    {EDU_EXEC, ncs_edp_sanamet, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.n2d_reg_su.su_name, 0, NULL},
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, EDU_EXIT,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.n2d_reg_su.error, 0, NULL},

	    /* AVSV_N2D_REG_COMP_MSG_INFO */
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.n2d_reg_comp.msg_id, 0, NULL},
	    {EDU_EXEC, m_NCS_EDP_SACLMNODEIDT, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.n2d_reg_comp.node_id, 0,
	     NULL},
	    {EDU_EXEC, ncs_edp_sanamet, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.n2d_reg_comp.comp_name, 0,
	     NULL},
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, EDU_EXIT,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.n2d_reg_comp.error, 0, NULL},

	    /* AVSV_N2D_OPERATION_STATE_MSG_INFO */
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.n2d_opr_state.msg_id, 0,
	     NULL},
	    {EDU_EXEC, m_NCS_EDP_SACLMNODEIDT, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.n2d_opr_state.node_id, 0,
	     NULL},
	    {EDU_EXEC, m_NCS_EDP_SAAMFRECOMMENDEDRECOVERYT, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.n2d_opr_state.rec_rcvr, 0,
	     NULL},
	    {EDU_EXEC, m_NCS_EDP_NCSOPERSTATE, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.n2d_opr_state.node_oper_state,
	     0, NULL},
	    {EDU_EXEC, ncs_edp_sanamet, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.n2d_opr_state.su_name, 0,
	     NULL},
	    {EDU_EXEC, m_NCS_EDP_NCSOPERSTATE, 0, 0, EDU_EXIT,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.n2d_opr_state.su_oper_state,
	     0, NULL},

	    /* AVSV_N2D_INFO_SU_SI_ASSIGN_MSG_INFO */
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.n2d_su_si_assign.msg_id, 0,
	     NULL},
	    {EDU_EXEC, m_NCS_EDP_SACLMNODEIDT, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.n2d_su_si_assign.node_id, 0,
	     NULL},
	    {EDU_EXEC, ncs_edp_int, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.n2d_su_si_assign.msg_act, 0,
	     NULL},
	    {EDU_EXEC, ncs_edp_sanamet, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.n2d_su_si_assign.su_name, 0,
	     NULL},
	    {EDU_EXEC, ncs_edp_sanamet, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.n2d_su_si_assign.si_name, 0,
	     NULL},
	    {EDU_EXEC, m_NCS_EDP_SAAMFHASTATET, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.n2d_su_si_assign.ha_state, 0,
	     NULL},
	    {EDU_VER_GE, NULL, 0, 0, 2, 0, 0,
	     (EDU_EXEC_RTINE)((uint16_t *)(&(ver3)))},
	    {EDU_EXEC, ncs_edp_ncs_bool, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.n2d_su_si_assign.single_csi,
	     0, NULL},
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, EDU_EXIT,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.n2d_su_si_assign.error, 0,
	     NULL},

	    /* AVSV_N2D_PG_TRACK_ACT_MSG_INFO */
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.n2d_pg_trk_act.msg_id, 0,
	     NULL},
	    {EDU_EXEC, m_NCS_EDP_SACLMNODEIDT, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.n2d_pg_trk_act.node_id, 0,
	     NULL},
	    {EDU_EXEC, ncs_edp_ncs_bool, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.n2d_pg_trk_act.msg_on_fover,
	     0, NULL},
	    {EDU_EXEC, ncs_edp_sanamet, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.n2d_pg_trk_act.csi_name, 0,
	     NULL},
	    {EDU_EXEC, ncs_edp_int, 0, 0, EDU_EXIT,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.n2d_pg_trk_act.actn, 0, NULL},

	    /* AVSV_N2D_OPERATION_REQUEST_MSG_INFO */
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.n2d_op_req.msg_id, 0, NULL},
	    {EDU_EXEC, m_NCS_EDP_SACLMNODEIDT, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.n2d_op_req.node_id, 0, NULL},
	    {EDU_EXEC, avsv_edp_param_info, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.n2d_op_req.param_info, 0,
	     NULL},
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, EDU_EXIT,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.n2d_op_req.error, 0, NULL},

	    /* AVSV_N2D_DATA_REQUEST_MSG_INFO */
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.n2d_data_req.msg_id, 0, NULL},
	    {EDU_EXEC, m_NCS_EDP_SACLMNODEIDT, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.n2d_data_req.node_id, 0,
	     NULL},
	    {EDU_EXEC, avsv_edp_param_info, 0, 0, EDU_EXIT,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.n2d_data_req.param_info, 0,
	     NULL},

	    /* AVSV_N2D_SHUTDOWN_APP_SU_MSG_INFO */
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.n2d_node_down_info.msg_id, 0,
	     NULL},
	    {EDU_EXEC, m_NCS_EDP_SACLMNODEIDT, 0, 0, EDU_EXIT,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.n2d_node_down_info.node_id,
	     0, NULL},

	    /* AVSV_N2D_VERIFY_ACK_NACK_MSG_INFO */
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.n2d_ack_nack_info.msg_id, 0,
	     NULL},
	    {EDU_EXEC, m_NCS_EDP_SACLMNODEIDT, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.n2d_ack_nack_info.node_id, 0,
	     NULL},
	    {EDU_EXEC, ncs_edp_ncs_bool, 0, 0, EDU_EXIT,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.n2d_ack_nack_info.ack, 0,
	     NULL},

	    /* AVSV_D2N_CLM_NODE_UP_MSG_INFO */
	    {EDU_EXEC, m_NCS_EDP_SACLMNODEIDT, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_node_up.node_id, 0, NULL},
	    {EDU_EXEC, ncs_edp_int, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_node_up.node_type, 0,
	     NULL},
	    {EDU_EXEC, m_NCS_EDP_SATIMET, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_node_up.su_failover_prob,
	     0, NULL},
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, EDU_EXIT,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_node_up.su_failover_max,
	     0, NULL},

	    /* AVSV_D2N_REG_SU_MSG_INFO */
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_reg_su.msg_id, 0, NULL},
	    {EDU_EXEC, ncs_edp_ncs_bool, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_reg_su.msg_on_fover, 0,
	     NULL},
	    {EDU_EXEC, m_NCS_EDP_SACLMNODEIDT, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_reg_su.nodeid, 0, NULL},
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_reg_su.num_su, 0, NULL},
	    {EDU_EXEC, avsv_edp_su_info_msg, EDQ_POINTER, 0, EDU_EXIT,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_reg_su.su_list, 0, NULL},

	    /* AVSV_D2N_REG_COMP_MSG_INFO */
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_reg_comp.msg_id, 0, NULL},
	    {EDU_EXEC, ncs_edp_ncs_bool, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_reg_comp.msg_on_fover, 0,
	     NULL},
	    {EDU_EXEC, m_NCS_EDP_SACLMNODEIDT, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_reg_comp.node_id, 0,
	     NULL},
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_reg_comp.num_comp, 0,
	     NULL},
	    {EDU_EXEC, avsv_edp_comp_info_msg, EDQ_POINTER, 0, EDU_EXIT,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_reg_comp.list, 0, NULL},

	    /* AVSV_D2N_INFO_SU_SI_ASSIGN_MSG_INFO */
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_su_si_assign.msg_id, 0,
	     NULL},
	    {EDU_EXEC, m_NCS_EDP_SACLMNODEIDT, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_su_si_assign.node_id, 0,
	     NULL},
	    {EDU_EXEC, ncs_edp_int, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_su_si_assign.msg_act, 0,
	     NULL},
	    {EDU_EXEC, ncs_edp_sanamet, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_su_si_assign.su_name, 0,
	     NULL},
	    {EDU_EXEC, ncs_edp_sanamet, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_su_si_assign.si_name, 0,
	     NULL},

	    /* Include SI rank in version 5 and higher */
	    {EDU_VER_GE, NULL, 0, 0, 2, 0, 0,
	     (EDU_EXEC_RTINE)((uint16_t *)(&(ver5)))},
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_su_si_assign.si_rank, 0,
	     NULL},

	    {EDU_EXEC, m_NCS_EDP_SAAMFHASTATET, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_su_si_assign.ha_state, 0,
	     NULL},
	    {EDU_VER_GE, NULL, 0, 0, 2, 0, 0,
	     (EDU_EXEC_RTINE)((uint16_t *)(&(ver3)))},
	    {EDU_EXEC, ncs_edp_ncs_bool, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_su_si_assign.single_csi,
	     0, NULL},
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_su_si_assign.num_assigns,
	     0, NULL},
	    {EDU_EXEC, avsv_edp_susi_asgn, EDQ_POINTER, 0, EDU_EXIT,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_su_si_assign.list, 0,
	     NULL},

	    /* AVSV_D2N_PG_TRACK_ACT_RSP_MSG_INFO */
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)
		 ->msg_info.d2n_pg_track_act_rsp.msg_id_ack,
	     0, NULL},
	    {EDU_EXEC, m_NCS_EDP_SACLMNODEIDT, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_pg_track_act_rsp.node_id,
	     0, NULL},
	    {EDU_EXEC, ncs_edp_ncs_bool, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)
		 ->msg_info.d2n_pg_track_act_rsp.msg_on_fover,
	     0, NULL},
	    {EDU_EXEC, ncs_edp_int, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_pg_track_act_rsp.actn, 0,
	     NULL},
	    {EDU_EXEC, ncs_edp_sanamet, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_pg_track_act_rsp.csi_name,
	     0, NULL},
	    {EDU_EXEC, ncs_edp_ncs_bool, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)
		 ->msg_info.d2n_pg_track_act_rsp.is_csi_exist,
	     0, NULL},
	    {EDU_EXEC, avsv_edp_saamfprotectiongroupnotificationbuffert, 0, 0,
	     EDU_EXIT,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_pg_track_act_rsp.mem_list,
	     0, NULL},

	    /* AVSV_D2N_PG_UPD_MSG_INFO */
	    {EDU_EXEC, m_NCS_EDP_SACLMNODEIDT, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_pg_upd.node_id, 0, NULL},
	    {EDU_EXEC, ncs_edp_sanamet, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_pg_upd.csi_name, 0, NULL},
	    {EDU_EXEC, ncs_edp_int, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_pg_upd.is_csi_del, 0,
	     NULL},
	    {EDU_EXEC, ncs_edp_saamfprotectiongroupnotificationt, 0, 0,
	     EDU_EXIT, (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_pg_upd.mem, 0,
	     NULL},

	    /* AVSV_D2N_OPERATION_REQUEST_MSG_INFO */
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_op_req.msg_id, 0, NULL},
	    {EDU_EXEC, m_NCS_EDP_SACLMNODEIDT, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_op_req.node_id, 0, NULL},
	    {EDU_EXEC, avsv_edp_param_info, 0, 0, EDU_EXIT,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_op_req.param_info, 0,
	     NULL},

	    /* AVSV_D2N_PRESENCE_SU_MSG_INFO */
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_prsc_su.msg_id, 0, NULL},
	    {EDU_EXEC, m_NCS_EDP_SACLMNODEIDT, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_prsc_su.node_id, 0, NULL},
	    {EDU_EXEC, ncs_edp_sanamet, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_prsc_su.su_name, 0, NULL},
	    {EDU_EXEC, ncs_edp_ncs_bool, 0, 0, EDU_EXIT,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_prsc_su.term_state, 0,
	     NULL},

	    /* AVSV_D2N_DATA_VERIFY_MSG */
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_data_verify.snd_id_cnt, 0,
	     NULL},
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_data_verify.rcv_id_cnt, 0,
	     NULL},
	    {EDU_EXEC, m_NCS_EDP_SACLMNODEIDT, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_data_verify.node_id, 0,
	     NULL},
	    {EDU_EXEC, m_NCS_EDP_SATIMET, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)
		 ->msg_info.d2n_data_verify.su_failover_prob,
	     0, NULL},
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, EDU_EXIT,
	     (long)&((AVSV_DND_MSG *)0)
		 ->msg_info.d2n_data_verify.su_failover_max,
	     0, NULL},

	    /* AVSV_D2N_DATA_ACK_MSG */
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_ack_info.msg_id_ack, 0,
	     NULL},
	    {EDU_EXEC, m_NCS_EDP_SACLMNODEIDT, 0, 0, EDU_EXIT,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_ack_info.node_id, 0,
	     NULL},

	    /* AVSV_D2N_SHUTDOWN_APP_SU_MSG_INFO */
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_shutdown_app_su.msg_id, 0,
	     NULL},
	    {EDU_EXEC, m_NCS_EDP_SACLMNODEIDT, 0, 0, EDU_EXIT,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_shutdown_app_su.node_id,
	     0, NULL},

	    /* AVSV_D2N_SET_LEDS_MSG_INFO */
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_set_leds.msg_id, 0, NULL},
	    {EDU_EXEC, m_NCS_EDP_SACLMNODEIDT, 0, 0, EDU_EXIT,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_set_leds.node_id, 0,
	     NULL},

	    /* AVSV_N2D_COMP_VALIDATION_INFO */
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.n2d_comp_valid_info.msg_id, 0,
	     NULL},
	    {EDU_EXEC, m_NCS_EDP_SACLMNODEIDT, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.n2d_comp_valid_info.node_id,
	     0, NULL},
	    {EDU_EXEC, ncs_edp_sanamet, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.n2d_comp_valid_info.comp_name,
	     0, NULL},
	    {EDU_EXEC, m_NCS_EDP_SAAMFHANDLET, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.n2d_comp_valid_info.hdl, 0,
	     NULL},
	    {EDU_EXEC, ncs_edp_sanamet, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)
		 ->msg_info.n2d_comp_valid_info.proxy_comp_name,
	     0, NULL},
	    {EDU_EXEC, ncs_edp_mds_dest, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.n2d_comp_valid_info.mds_dest,
	     0, NULL},
	    {EDU_EXEC, ncs_edp_uns8, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)
		 ->msg_info.n2d_comp_valid_info.mds_ctxt.length,
	     0, NULL},
	    {EDU_EXEC, ncs_edp_uns8, EDQ_ARRAY, 0, EDU_EXIT,
	     (long)&((AVSV_DND_MSG *)0)
		 ->msg_info.n2d_comp_valid_info.mds_ctxt.data,
	     MDS_SYNC_SND_CTXT_LEN_MAX, NULL},

	    /* AVSV_D2N_COMP_VALIDATION_RESP_INFO */
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)
		 ->msg_info.d2n_comp_valid_resp_info.msg_id,
	     0, NULL},
	    {EDU_EXEC, m_NCS_EDP_SACLMNODEIDT, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)
		 ->msg_info.d2n_comp_valid_resp_info.node_id,
	     0, NULL},
	    {EDU_EXEC, ncs_edp_sanamet, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)
		 ->msg_info.d2n_comp_valid_resp_info.comp_name,
	     0, NULL},
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, EDU_EXIT,
	     (long)&((AVSV_DND_MSG *)0)
		 ->msg_info.d2n_comp_valid_resp_info.result,
	     0, NULL},

	    /* AVSV_D2N_ROLE_CHANGE_MSG */
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_role_change_info.msg_id,
	     0, NULL},
	    {EDU_EXEC, m_NCS_EDP_SACLMNODEIDT, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_role_change_info.node_id,
	     0, NULL},
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, EDU_EXIT,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_role_change_info.role, 0,
	     NULL},

	    /* AVSV_D2N_ADMIN_OP_REQ_MSG */
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_admin_op_req_info.msg_id,
	     0, NULL},
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)
		 ->msg_info.d2n_admin_op_req_info.class_id,
	     0, NULL},
	    {EDU_EXEC, ncs_edp_sanamet, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_admin_op_req_info.dn, 0,
	     NULL},
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, EDU_EXIT,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_admin_op_req_info.oper_id,
	     0, NULL},

	    /* AVSV_D2N_HEARTBEAT_MSG */
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, EDU_EXIT,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_hb_info.seq_id, 0, NULL},

	    /* AVSV_D2N_REBOOT_MSG */
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_reboot_info.msg_id, 0,
	     NULL},
	    {EDU_EXEC, m_NCS_EDP_SACLMNODEIDT, 0, 0, EDU_EXIT,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.d2n_reboot_info.node_id, 0,
	     NULL},

	    /* AVSV_N2D_ND_SISU_STATE_INFO_MSG */
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.n2d_nd_sisu_state_info.msg_id,
	     0, NULL},
	    {EDU_EXEC, m_NCS_EDP_SACLMNODEIDT, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)
		 ->msg_info.n2d_nd_sisu_state_info.node_id,
	     0, NULL},
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)
		 ->msg_info.n2d_nd_sisu_state_info.num_sisu,
	     0, NULL},
	    {EDU_EXEC, avsv_edp_sisu_state_info_msg, EDQ_POINTER, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)
		 ->msg_info.n2d_nd_sisu_state_info.sisu_list,
	     0, NULL},
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)->msg_info.n2d_nd_sisu_state_info.num_su,
	     0, NULL},
	    {EDU_EXEC, avsv_edp_su_state_info_msg, EDQ_POINTER, 0, EDU_EXIT,
	     (long)&((AVSV_DND_MSG *)0)
		 ->msg_info.n2d_nd_sisu_state_info.su_list,
	     0, NULL},

	    /* AVSV_N2D_ND_CSICOMP_STATE_INFO_MSG */
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)
		 ->msg_info.n2d_nd_csicomp_state_info.msg_id,
	     0, NULL},
	    {EDU_EXEC, m_NCS_EDP_SACLMNODEIDT, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)
		 ->msg_info.n2d_nd_csicomp_state_info.node_id,
	     0, NULL},
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)
		 ->msg_info.n2d_nd_csicomp_state_info.num_csicomp,
	     0, NULL},
	    {EDU_EXEC, avsv_edp_csicomp_state_info_msg, EDQ_POINTER, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)
		 ->msg_info.n2d_nd_csicomp_state_info.csicomp_list,
	     0, NULL},
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)
		 ->msg_info.n2d_nd_csicomp_state_info.num_comp,
	     0, NULL},
	    {EDU_EXEC, avsv_edp_comp_state_info_msg, EDQ_POINTER, 0, EDU_EXIT,
	     (long)&((AVSV_DND_MSG *)0)
		 ->msg_info.n2d_nd_csicomp_state_info.comp_list,
	     0, NULL},

	    /*AVSV_D2N_COMPCSI_ASSIGN_MSG_INFO*/
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)
		 ->msg_info.d2n_compcsi_assign_msg_info.msg_id,
	     0, NULL},
	    {EDU_EXEC, m_NCS_EDP_SACLMNODEIDT, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)
		 ->msg_info.d2n_compcsi_assign_msg_info.node_id,
	     0, NULL},
	    {EDU_EXEC, ncs_edp_int, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)
		 ->msg_info.d2n_compcsi_assign_msg_info.msg_act,
	     0, NULL},
	    {EDU_EXEC, ncs_edp_sanamet, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)
		 ->msg_info.d2n_compcsi_assign_msg_info.comp_name,
	     0, NULL},
	    {EDU_EXEC, ncs_edp_sanamet, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)
		 ->msg_info.d2n_compcsi_assign_msg_info.csi_name,
	     0, NULL},
	    {EDU_EXEC, avsv_edp_csi_attr_info, 0, 0, 0,
	     (long)&((AVSV_DND_MSG *)0)
		 ->msg_info.d2n_compcsi_assign_msg_info.info.attrs,
	     0, NULL},

	    {EDU_END, 0, 0, 0, 0, 0, 0, NULL},
	};

	if (op == EDP_OP_TYPE_ENC) {
		struct_ptr = (AVSV_DND_MSG *)ptr;
	} else if (op == EDP_OP_TYPE_DEC) {
		d_ptr = (AVSV_DND_MSG **)ptr;
		if (*d_ptr == NULL) {
			*d_ptr = malloc(sizeof(AVSV_DND_MSG));
			if (*d_ptr == NULL) {
				*o_err = EDU_ERR_MEM_FAIL;
				return NCSCC_RC_FAILURE;
			}
		}
		memset(*d_ptr, '\0', sizeof(AVSV_DND_MSG));
		struct_ptr = *d_ptr;
	} else {
		struct_ptr = ptr;
	}
	rc = m_NCS_EDU_RUN_RULES(hdl, edu_tkn, avsv_dnd_msg_rules, struct_ptr,
				 ptr_data_len, buf_env, op, o_err);
	return rc;
}

/*****************************************************************************

  PROCEDURE NAME:   avsv_dnd_msg_test_type_fnc

  DESCRIPTION:      Test function, that gets invoked from the EDP
		    function "avsv_edp_dnd_msg".

  RETURNS:          uns32, denoting the offset of the next-instruction
		    to be executed(relative to the EDU_TEST instruction
		    defined in the EDP "avsv_edp_dnd_msg").

*****************************************************************************/
int avsv_dnd_msg_test_type_fnc(NCSCONTEXT arg)
{
	enum { LCL_JMP_OFFSET_AVSV_N2D_NODE_UP_MSG = 1,
	       LCL_JMP_OFFSET_AVSV_N2D_REG_SU_MSG = 8,
	       LCL_JMP_OFFSET_AVSV_N2D_REG_COMP_MSG = 12,
	       LCL_JMP_OFFSET_AVSV_N2D_OPERATION_STATE_MSG = 16,
	       LCL_JMP_OFFSET_AVSV_N2D_INFO_SU_SI_ASSIGN_MSG = 22,
	       LCL_JMP_OFFSET_AVSV_N2D_PG_TRACK_ACT_MSG = 31,
	       LCL_JMP_OFFSET_AVSV_N2D_OPERATION_REQUEST_MSG = 36,
	       LCL_JMP_OFFSET_AVSV_N2D_DATA_REQUEST_MSG = 40,
	       LCL_JMP_OFFSET_AVSV_N2D_NODE_DOWN_MSG = 43,
	       LCL_JMP_OFFSET_AVSV_N2D_VERIFY_ACK_NACK_MSG = 45,
	       LCL_JMP_OFFSET_AVSV_D2N_CLM_NODE_UP_MSG = 48,
	       LCL_JMP_OFFSET_AVSV_D2N_REG_SU_MSG = 52,
	       LCL_JMP_OFFSET_AVSV_D2N_REG_COMP_MSG = 57,
	       LCL_JMP_OFFSET_AVSV_D2N_INFO_SU_SI_ASSIGN_MSG = 62,
	       LCL_JMP_OFFSET_AVSV_D2N_PG_TRACK_ACT_RSP_MSG = 74,
	       LCL_JMP_OFFSET_AVSV_D2N_PG_UPD_MSG = 81,
	       LCL_JMP_OFFSET_AVSV_D2N_OPERATION_REQUEST_MSG = 85,
	       LCL_JMP_OFFSET_AVSV_D2N_PRESENCE_SU_MSG = 88,
	       LCL_JMP_OFFSET_AVSV_D2N_DATA_VERIFY_MSG = 92,
	       LCL_JMP_OFFSET_AVSV_D2N_DATA_ACK_MSG = 97,
	       LCL_JMP_OFFSET_AVSV_D2N_SHUTDOWN_APP_SU_MSG = 99,
	       LCL_JMP_OFFSET_AVSV_D2N_SET_LEDS_MSG = 101,
	       LCL_JMP_OFFSET_AVSV_N2D_COMP_VALID_MSG = 103,
	       LCL_JMP_OFFSET_AVSV_D2N_COMP_VALID_RESP_MSG = 111,
	       LCL_JMP_OFFSET_AVSV_D2N_ROLE_CHANGE_MSG = 115,
	       LCL_JMP_OFFSET_AVSV_D2N_ADMIN_OP_REQ_MSG = 118,
	       LCL_JMP_OFFSET_AVSV_D2N_HEARTBEAT_MSG = 122,
	       LCL_JMP_OFFSET_AVSV_D2N_REBOOT_MSG = 123,
	       LCL_JMP_OFFSET_AVSV_N2D_ND_SISU_STATE_INFO_MSG = 125,
	       LCL_JMP_OFFSET_AVSV_N2D_ND_CSICOMP_STATE_INFO_MSG = 131,
	       LCL_JMP_OFFSET_AVSV_D2N_COMPCSI_ASSIGN_MSG = 137 };
	AVSV_DND_MSG_TYPE type;

	if (arg == NULL)
		return EDU_FAIL;

	type = *(AVSV_DND_MSG_TYPE *)arg;

	switch (type) {
	case AVSV_N2D_NODE_UP_MSG:
		return LCL_JMP_OFFSET_AVSV_N2D_NODE_UP_MSG;
	case AVSV_N2D_REG_SU_MSG:
		return LCL_JMP_OFFSET_AVSV_N2D_REG_SU_MSG;
	case AVSV_N2D_REG_COMP_MSG:
		return LCL_JMP_OFFSET_AVSV_N2D_REG_COMP_MSG;
	case AVSV_N2D_OPERATION_STATE_MSG:
		return LCL_JMP_OFFSET_AVSV_N2D_OPERATION_STATE_MSG;
	case AVSV_N2D_INFO_SU_SI_ASSIGN_MSG:
		return LCL_JMP_OFFSET_AVSV_N2D_INFO_SU_SI_ASSIGN_MSG;
	case AVSV_N2D_PG_TRACK_ACT_MSG:
		return LCL_JMP_OFFSET_AVSV_N2D_PG_TRACK_ACT_MSG;
	case AVSV_N2D_OPERATION_REQUEST_MSG:
		return LCL_JMP_OFFSET_AVSV_N2D_OPERATION_REQUEST_MSG;
	case AVSV_N2D_DATA_REQUEST_MSG:
		return LCL_JMP_OFFSET_AVSV_N2D_DATA_REQUEST_MSG;
	case AVSV_N2D_NODE_DOWN_MSG:
		return LCL_JMP_OFFSET_AVSV_N2D_NODE_DOWN_MSG;
	case AVSV_N2D_VERIFY_ACK_NACK_MSG:
		return LCL_JMP_OFFSET_AVSV_N2D_VERIFY_ACK_NACK_MSG;
	case AVSV_D2N_NODE_UP_MSG:
		return LCL_JMP_OFFSET_AVSV_D2N_CLM_NODE_UP_MSG;
	case AVSV_D2N_REG_SU_MSG:
		return LCL_JMP_OFFSET_AVSV_D2N_REG_SU_MSG;
	case AVSV_D2N_REG_COMP_MSG:
		return LCL_JMP_OFFSET_AVSV_D2N_REG_COMP_MSG;
	case AVSV_D2N_INFO_SU_SI_ASSIGN_MSG:
		return LCL_JMP_OFFSET_AVSV_D2N_INFO_SU_SI_ASSIGN_MSG;
	case AVSV_D2N_PG_TRACK_ACT_RSP_MSG:
		return LCL_JMP_OFFSET_AVSV_D2N_PG_TRACK_ACT_RSP_MSG;
	case AVSV_D2N_PG_UPD_MSG:
		return LCL_JMP_OFFSET_AVSV_D2N_PG_UPD_MSG;
	case AVSV_D2N_OPERATION_REQUEST_MSG:
		return LCL_JMP_OFFSET_AVSV_D2N_OPERATION_REQUEST_MSG;
	case AVSV_D2N_PRESENCE_SU_MSG:
		return LCL_JMP_OFFSET_AVSV_D2N_PRESENCE_SU_MSG;
	case AVSV_D2N_DATA_VERIFY_MSG:
		return LCL_JMP_OFFSET_AVSV_D2N_DATA_VERIFY_MSG;
	case AVSV_D2N_DATA_ACK_MSG:
		return LCL_JMP_OFFSET_AVSV_D2N_DATA_ACK_MSG;
	case AVSV_D2N_SHUTDOWN_APP_SU_MSG:
		return LCL_JMP_OFFSET_AVSV_D2N_SHUTDOWN_APP_SU_MSG;
	case AVSV_D2N_SET_LEDS_MSG:
		return LCL_JMP_OFFSET_AVSV_D2N_SET_LEDS_MSG;
	case AVSV_N2D_COMP_VALIDATION_MSG:
		return LCL_JMP_OFFSET_AVSV_N2D_COMP_VALID_MSG;
	case AVSV_D2N_COMP_VALIDATION_RESP_MSG:
		return LCL_JMP_OFFSET_AVSV_D2N_COMP_VALID_RESP_MSG;
	case AVSV_D2N_ROLE_CHANGE_MSG:
		return LCL_JMP_OFFSET_AVSV_D2N_ROLE_CHANGE_MSG;
	case AVSV_D2N_ADMIN_OP_REQ_MSG:
		return LCL_JMP_OFFSET_AVSV_D2N_ADMIN_OP_REQ_MSG;
	case AVSV_D2N_HEARTBEAT_MSG:
		return LCL_JMP_OFFSET_AVSV_D2N_HEARTBEAT_MSG;
	case AVSV_D2N_REBOOT_MSG:
		return LCL_JMP_OFFSET_AVSV_D2N_REBOOT_MSG;
	case AVSV_N2D_ND_SISU_STATE_INFO_MSG:
		return LCL_JMP_OFFSET_AVSV_N2D_ND_SISU_STATE_INFO_MSG;
	case AVSV_N2D_ND_CSICOMP_STATE_INFO_MSG:
		return LCL_JMP_OFFSET_AVSV_N2D_ND_CSICOMP_STATE_INFO_MSG;
	case AVSV_D2N_COMPCSI_ASSIGN_MSG:
		return LCL_JMP_OFFSET_AVSV_D2N_COMPCSI_ASSIGN_MSG;

	default:
		break;
	}

	return EDU_FAIL;
}

/*****************************************************************************

  PROCEDURE NAME:   avsv_edp_param_info

  DESCRIPTION:      EDU program handler for "AVSV_PARAM_INFO" data. This
function is invoked by EDU for performing encode/decode operation on
"AVSV_PARAM_INFO" data.

  RETURNS:          NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

*****************************************************************************/
uint32_t avsv_edp_param_info(EDU_HDL *hdl, EDU_TKN *edu_tkn, NCSCONTEXT ptr,
			     uint32_t *ptr_data_len, EDU_BUF_ENV *buf_env,
			     EDP_OP_TYPE op, EDU_ERR *o_err)
{
	uint32_t rc = NCSCC_RC_SUCCESS;
	AVSV_PARAM_INFO *struct_ptr = NULL, **d_ptr = NULL;

	EDU_INST_SET avsv_param_info_rules[] = {
	    {EDU_START, avsv_edp_param_info, 0, 0, 0, sizeof(AVSV_PARAM_INFO),
	     0, NULL},
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_PARAM_INFO *)0)->class_id, 0, NULL},
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_PARAM_INFO *)0)->attr_id, 0, NULL},
	    {EDU_EXEC, ncs_edp_sanamet, 0, 0, 0,
	     (long)&((AVSV_PARAM_INFO *)0)->name, 0, NULL},
	    {EDU_EXEC, ncs_edp_sanamet, 0, 0, 0,
	     (long)&((AVSV_PARAM_INFO *)0)->name_sec, 0, NULL},
	    {EDU_EXEC, ncs_edp_int, 0, 0, 0, (long)&((AVSV_PARAM_INFO *)0)->act,
	     0, NULL},
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_PARAM_INFO *)0)->value_len, 0, NULL},
	    {EDU_EXEC, ncs_edp_uns8, EDQ_ARRAY, 0, 0,
	     (long)&((AVSV_PARAM_INFO *)0)->value, AVSV_MISC_STR_MAX_SIZE,
	     NULL},
	    {EDU_END, 0, 0, 0, 0, 0, 0, NULL},
	};

	if (op == EDP_OP_TYPE_ENC) {
		struct_ptr = (AVSV_PARAM_INFO *)ptr;
	} else if (op == EDP_OP_TYPE_DEC) {
		d_ptr = (AVSV_PARAM_INFO **)ptr;
		if (*d_ptr == NULL) {
			/* Malloc failed!! */
			*o_err = EDU_ERR_MEM_FAIL;
			return NCSCC_RC_FAILURE;
		}
		memset(*d_ptr, '\0', sizeof(AVSV_PARAM_INFO));
		struct_ptr = *d_ptr;
	} else {
		struct_ptr = ptr;
	}
	rc = m_NCS_EDU_RUN_RULES(hdl, edu_tkn, avsv_param_info_rules,
				 struct_ptr, ptr_data_len, buf_env, op, o_err);
	return rc;
}

/*****************************************************************************

  PROCEDURE NAME:   avsv_edp_su_info_msg

  DESCRIPTION:      EDU program handler for "AVSV_SU_INFO_MSG" data. This
function is invoked by EDU for performing encode/decode operation on
"AVSV_SU_INFO_MSG" data.

  RETURNS:          NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

*****************************************************************************/
uint32_t avsv_edp_su_info_msg(EDU_HDL *hdl, EDU_TKN *edu_tkn, NCSCONTEXT ptr,
			      uint32_t *ptr_data_len, EDU_BUF_ENV *buf_env,
			      EDP_OP_TYPE op, EDU_ERR *o_err)
{
	uint32_t rc = NCSCC_RC_SUCCESS;
	AVSV_SU_INFO_MSG *struct_ptr = NULL, **d_ptr = NULL;
	uint16_t ver2 = AVSV_AVD_AVND_MSG_FMT_VER_2;
	uint16_t ver5 = AVSV_AVD_AVND_MSG_FMT_VER_5;

	EDU_INST_SET avsv_su_info_msg_rules[] = {
	    {EDU_START, avsv_edp_su_info_msg, EDQ_LNKLIST, 0, 0,
	     sizeof(AVSV_SU_INFO_MSG), 0, NULL},

	    {EDU_EXEC, ncs_edp_sanamet, 0, 0, 0,
	     (long)&((AVSV_SU_INFO_MSG *)0)->name, 0, NULL},
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_SU_INFO_MSG *)0)->dummy, 0, NULL},
	    {EDU_EXEC, m_NCS_EDP_SATIMET, 0, 0, 0,
	     (long)&((AVSV_SU_INFO_MSG *)0)->comp_restart_prob, 0, NULL},
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_SU_INFO_MSG *)0)->comp_restart_max, 0, NULL},
	    {EDU_EXEC, m_NCS_EDP_SATIMET, 0, 0, 0,
	     (long)&((AVSV_SU_INFO_MSG *)0)->su_restart_prob, 0, NULL},
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_SU_INFO_MSG *)0)->su_restart_max, 0, NULL},
	    {EDU_EXEC, ncs_edp_ncs_bool, 0, 0, 0,
	     (long)&((AVSV_SU_INFO_MSG *)0)->is_ncs, 0, NULL},

	    /* Include su_is_external in version 2 and higher */
	    {EDU_VER_GE, NULL, 0, 0, 2, 0, 0,
	     (EDU_EXEC_RTINE)((uint16_t *)(&(ver2)))},
	    {EDU_EXEC, ncs_edp_ncs_bool, 0, 0, 0,
	     (long)&((AVSV_SU_INFO_MSG *)0)->su_is_external, 0, NULL},

	    /* Include su_failover in version 5 and higher */
	    {EDU_VER_GE, NULL, 0, 0, 2, 0, 0,
	     (EDU_EXEC_RTINE)((uint16_t *)(&(ver5)))},
	    {EDU_EXEC, ncs_edp_ncs_bool, 0, 0, 0,
	     (long)&((AVSV_SU_INFO_MSG *)0)->su_failover, 0, NULL},

	    {EDU_TEST_LL_PTR, avsv_edp_su_info_msg, 0, 0, 0,
	     (long)&((AVSV_SU_INFO_MSG *)0)->next, 0, NULL},
	    {EDU_END, 0, 0, 0, 0, 0, 0, NULL},
	};

	if (op == EDP_OP_TYPE_ENC) {
		struct_ptr = (AVSV_SU_INFO_MSG *)ptr;
	} else if (op == EDP_OP_TYPE_DEC) {
		d_ptr = (AVSV_SU_INFO_MSG **)ptr;
		if (*d_ptr == NULL) {
			*d_ptr = malloc(sizeof(AVSV_SU_INFO_MSG));
			if (*d_ptr == NULL) {
				*o_err = EDU_ERR_MEM_FAIL;
				return NCSCC_RC_FAILURE;
			}
		}
		memset(*d_ptr, '\0', sizeof(AVSV_SU_INFO_MSG));
		struct_ptr = *d_ptr;
	} else {
		struct_ptr = ptr;
	}
	rc = m_NCS_EDU_RUN_RULES(hdl, edu_tkn, avsv_su_info_msg_rules,
				 struct_ptr, ptr_data_len, buf_env, op, o_err);
	return rc;
}

/*****************************************************************************

  PROCEDURE NAME:   avsv_edp_comp_info_msg

  DESCRIPTION:      EDU program handler for "AVSV_COMP_INFO_MSG" data. This
function is invoked by EDU for performing encode/decode operation on
"AVSV_COMP_INFO_MSG" data.

  RETURNS:          NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

*****************************************************************************/
uint32_t avsv_edp_comp_info_msg(EDU_HDL *hdl, EDU_TKN *edu_tkn, NCSCONTEXT ptr,
				uint32_t *ptr_data_len, EDU_BUF_ENV *buf_env,
				EDP_OP_TYPE op, EDU_ERR *o_err)
{
	uint32_t rc = NCSCC_RC_SUCCESS;
	AVSV_COMP_INFO_MSG *struct_ptr = NULL, **d_ptr = NULL;

	EDU_INST_SET avsv_comp_info_msg_rules[] = {
	    {EDU_START, avsv_edp_comp_info_msg, EDQ_LNKLIST, 0, 0,
	     sizeof(AVSV_COMP_INFO_MSG), 0, NULL},

	    {EDU_EXEC, ncs_edp_sanamet, 0, 0, 0,
	     (long)&((AVSV_COMP_INFO_MSG *)0)->comp_info.name, 0, NULL},
	    {EDU_EXEC, m_NCS_EDP_SAAMFCOMPONENTCAPABILITYMODELT, 0, 0, 0,
	     (long)&((AVSV_COMP_INFO_MSG *)0)->comp_info.cap, 0, NULL},
	    {EDU_EXEC, ncs_edp_int, 0, 0, 0,
	     (long)&((AVSV_COMP_INFO_MSG *)0)->comp_info.category, 0, NULL},
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_COMP_INFO_MSG *)0)->comp_info.init_len, 0, NULL},
	    {EDU_EXEC, ncs_edp_uns8, EDQ_ARRAY, 0, 0,
	     (long)&((AVSV_COMP_INFO_MSG *)0)->comp_info.init_info,
	     AVSV_MISC_STR_MAX_SIZE, NULL},
	    {EDU_EXEC, m_NCS_EDP_SATIMET, 0, 0, 0,
	     (long)&((AVSV_COMP_INFO_MSG *)0)->comp_info.init_time, 0, NULL},
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_COMP_INFO_MSG *)0)->comp_info.term_len, 0, NULL},
	    {EDU_EXEC, ncs_edp_uns8, EDQ_ARRAY, 0, 0,
	     (long)&((AVSV_COMP_INFO_MSG *)0)->comp_info.term_info,
	     AVSV_MISC_STR_MAX_SIZE, NULL},
	    {EDU_EXEC, m_NCS_EDP_SATIMET, 0, 0, 0,
	     (long)&((AVSV_COMP_INFO_MSG *)0)->comp_info.term_time, 0, NULL},
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_COMP_INFO_MSG *)0)->comp_info.clean_len, 0, NULL},
	    {EDU_EXEC, ncs_edp_uns8, EDQ_ARRAY, 0, 0,
	     (long)&((AVSV_COMP_INFO_MSG *)0)->comp_info.clean_info,
	     AVSV_MISC_STR_MAX_SIZE, NULL},
	    {EDU_EXEC, m_NCS_EDP_SATIMET, 0, 0, 0,
	     (long)&((AVSV_COMP_INFO_MSG *)0)->comp_info.clean_time, 0, NULL},
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_COMP_INFO_MSG *)0)->comp_info.amstart_len, 0, NULL},
	    {EDU_EXEC, ncs_edp_uns8, EDQ_ARRAY, 0, 0,
	     (long)&((AVSV_COMP_INFO_MSG *)0)->comp_info.amstart_info,
	     AVSV_MISC_STR_MAX_SIZE, NULL},
	    {EDU_EXEC, m_NCS_EDP_SATIMET, 0, 0, 0,
	     (long)&((AVSV_COMP_INFO_MSG *)0)->comp_info.amstart_time, 0, NULL},
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_COMP_INFO_MSG *)0)->comp_info.amstop_len, 0, NULL},
	    {EDU_EXEC, ncs_edp_uns8, EDQ_ARRAY, 0, 0,
	     (long)&((AVSV_COMP_INFO_MSG *)0)->comp_info.amstop_info,
	     AVSV_MISC_STR_MAX_SIZE, NULL},
	    {EDU_EXEC, m_NCS_EDP_SATIMET, 0, 0, 0,
	     (long)&((AVSV_COMP_INFO_MSG *)0)->comp_info.amstop_time, 0, NULL},
	    {EDU_EXEC, m_NCS_EDP_SATIMET, 0, 0, 0,
	     (long)&((AVSV_COMP_INFO_MSG *)0)
		 ->comp_info.terminate_callback_timeout,
	     0, NULL},
	    {EDU_EXEC, m_NCS_EDP_SATIMET, 0, 0, 0,
	     (long)&((AVSV_COMP_INFO_MSG *)0)
		 ->comp_info.csi_set_callback_timeout,
	     0, NULL},
	    {EDU_EXEC, m_NCS_EDP_SATIMET, 0, 0, 0,
	     (long)&((AVSV_COMP_INFO_MSG *)0)
		 ->comp_info.quiescing_complete_timeout,
	     0, NULL},
	    {EDU_EXEC, m_NCS_EDP_SATIMET, 0, 0, 0,
	     (long)&((AVSV_COMP_INFO_MSG *)0)
		 ->comp_info.csi_rmv_callback_timeout,
	     0, NULL},
	    {EDU_EXEC, m_NCS_EDP_SATIMET, 0, 0, 0,
	     (long)&((AVSV_COMP_INFO_MSG *)0)
		 ->comp_info.proxied_inst_callback_timeout,
	     0, NULL},
	    {EDU_EXEC, m_NCS_EDP_SATIMET, 0, 0, 0,
	     (long)&((AVSV_COMP_INFO_MSG *)0)
		 ->comp_info.proxied_clean_callback_timeout,
	     0, NULL},
	    {EDU_EXEC, ncs_edp_ncs_bool, 0, 0, 0,
	     (long)&((AVSV_COMP_INFO_MSG *)0)->comp_info.am_enable, 0, NULL},
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_COMP_INFO_MSG *)0)->comp_info.max_num_amstart, 0,
	     NULL},
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_COMP_INFO_MSG *)0)->comp_info.max_num_amstop, 0,
	     NULL},
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_COMP_INFO_MSG *)0)->comp_info.inst_level, 0, NULL},
	    {EDU_EXEC, m_NCS_EDP_SAAMFRECOMMENDEDRECOVERYT, 0, 0, 0,
	     (long)&((AVSV_COMP_INFO_MSG *)0)->comp_info.def_recvr, 0, NULL},
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_COMP_INFO_MSG *)0)->comp_info.max_num_inst, 0, NULL},
	    {EDU_EXEC, ncs_edp_ncs_bool, 0, 0, 0,
	     (long)&((AVSV_COMP_INFO_MSG *)0)->comp_info.comp_restart, 0, NULL},

	    {EDU_TEST_LL_PTR, avsv_edp_comp_info_msg, 0, 0, 0,
	     (long)&((AVSV_COMP_INFO_MSG *)0)->next, 0, NULL},
	    {EDU_END, 0, 0, 0, 0, 0, 0, NULL},
	};

	if (op == EDP_OP_TYPE_ENC) {
		struct_ptr = (AVSV_COMP_INFO_MSG *)ptr;
	} else if (op == EDP_OP_TYPE_DEC) {
		d_ptr = (AVSV_COMP_INFO_MSG **)ptr;
		if (*d_ptr == NULL) {
			*d_ptr = malloc(sizeof(AVSV_COMP_INFO_MSG));
			if (*d_ptr == NULL) {
				*o_err = EDU_ERR_MEM_FAIL;
				return NCSCC_RC_FAILURE;
			}
		}
		memset(*d_ptr, '\0', sizeof(AVSV_COMP_INFO_MSG));
		struct_ptr = *d_ptr;
	} else {
		struct_ptr = ptr;
	}
	rc = m_NCS_EDU_RUN_RULES(hdl, edu_tkn, avsv_comp_info_msg_rules,
				 struct_ptr, ptr_data_len, buf_env, op, o_err);
	return rc;
}

/*****************************************************************************

  PROCEDURE NAME:   avsv_edp_susi_asgn

  DESCRIPTION:      EDU program handler for "AVSV_SUSI_ASGN" data. This function
		    is invoked by EDU for performing encode/decode operation
		    on "AVSV_SUSI_ASGN" data.

  RETURNS:          NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

*****************************************************************************/
uint32_t avsv_edp_susi_asgn(EDU_HDL *hdl, EDU_TKN *edu_tkn, NCSCONTEXT ptr,
			    uint32_t *ptr_data_len, EDU_BUF_ENV *buf_env,
			    EDP_OP_TYPE op, EDU_ERR *o_err)
{
	uint32_t rc = NCSCC_RC_SUCCESS;
	AVSV_SUSI_ASGN *struct_ptr = NULL, **d_ptr = NULL;
	uint16_t ver5 = AVSV_AVD_AVND_MSG_FMT_VER_5;

	EDU_INST_SET avsv_susi_asgn_rules[] = {
	    {EDU_START, avsv_edp_susi_asgn, EDQ_LNKLIST, 0, 0,
	     sizeof(AVSV_SUSI_ASGN), 0, NULL},

	    {EDU_EXEC, ncs_edp_sanamet, 0, 0, 0,
	     (long)&((AVSV_SUSI_ASGN *)0)->comp_name, 0, NULL},
	    {EDU_EXEC, ncs_edp_sanamet, 0, 0, 0,
	     (long)&((AVSV_SUSI_ASGN *)0)->csi_name, 0, NULL},
	    {EDU_EXEC, ncs_edp_sanamet, 0, 0, 0,
	     (long)&((AVSV_SUSI_ASGN *)0)->active_comp_name, 0, NULL},
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_SUSI_ASGN *)0)->csi_rank, 0, NULL},
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_SUSI_ASGN *)0)->stdby_rank, 0, NULL},
	    {EDU_EXEC, m_NCS_EDP_SAAMFCSITRANSITIONDESCRIPTORT, 0, 0, 0,
	     (long)&((AVSV_SUSI_ASGN *)0)->active_comp_dsc, 0, NULL},

	    /* Include comp capability in version 5 and higher */
	    {EDU_VER_GE, NULL, 0, 0, 2, 0, 0,
	     (EDU_EXEC_RTINE)((uint16_t *)(&(ver5)))},
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_SUSI_ASGN *)0)->capability, 0, NULL},

	    {EDU_EXEC, avsv_edp_csi_attr_info, 0, 0, 0,
	     (long)&((AVSV_SUSI_ASGN *)0)->attrs, 0, NULL},

	    {EDU_TEST_LL_PTR, avsv_edp_susi_asgn, 0, 0, 0,
	     (long)&((AVSV_SUSI_ASGN *)0)->next, 0, NULL},
	    {EDU_END, 0, 0, 0, 0, 0, 0, NULL},
	};

	if (op == EDP_OP_TYPE_ENC) {
		struct_ptr = (AVSV_SUSI_ASGN *)ptr;
	} else if (op == EDP_OP_TYPE_DEC) {
		d_ptr = (AVSV_SUSI_ASGN **)ptr;
		if (*d_ptr == NULL) {
			*d_ptr = malloc(sizeof(AVSV_SUSI_ASGN));
			if (*d_ptr == NULL) {
				*o_err = EDU_ERR_MEM_FAIL;
				return NCSCC_RC_FAILURE;
			}
		}
		memset(*d_ptr, '\0', sizeof(AVSV_SUSI_ASGN));
		struct_ptr = *d_ptr;
	} else {
		struct_ptr = ptr;
	}
	rc = m_NCS_EDU_RUN_RULES(hdl, edu_tkn, avsv_susi_asgn_rules, struct_ptr,
				 ptr_data_len, buf_env, op, o_err);
	return rc;
}
/*****************************************************************************

  PROCEDURE NAME:   avsv_edp_sisu_state_info_msg

  DESCRIPTION:      EDU program handler for "AVSV_SISU_STATE_MSG" data. This
function is invoked by EDU for performing encode/decode operation on
"AVSV_SISU_STATE_MSG" data.

  RETURNS:          NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

*****************************************************************************/
uint32_t avsv_edp_sisu_state_info_msg(EDU_HDL *hdl, EDU_TKN *edu_tkn,
				      NCSCONTEXT ptr, uint32_t *ptr_data_len,
				      EDU_BUF_ENV *buf_env, EDP_OP_TYPE op,
				      EDU_ERR *o_err)
{
	uint32_t rc = NCSCC_RC_SUCCESS;
	AVSV_SISU_STATE_MSG *struct_ptr = NULL, **d_ptr = NULL;
	uint16_t ver7 = AVSV_AVD_AVND_MSG_FMT_VER_7;

	EDU_INST_SET avsv_sisu_state_msg_rules[] = {
	    {EDU_START, avsv_edp_sisu_state_info_msg, EDQ_LNKLIST, 0, 0,
	     sizeof(AVSV_SISU_STATE_MSG), 0, NULL},

	    {EDU_EXEC, ncs_edp_sanamet, 0, 0, 0,
	     (long)&((AVSV_SISU_STATE_MSG *)0)->safSU, 0, NULL},
	    {EDU_EXEC, ncs_edp_sanamet, 0, 0, 0,
	     (long)&((AVSV_SISU_STATE_MSG *)0)->safSI, 0, NULL},
	    {EDU_EXEC, m_NCS_EDP_SAAMFHASTATET, 0, 0, 0,
	     (long)&((AVSV_SISU_STATE_MSG *)0)->saAmfSISUHAState, 0, NULL},
	    {EDU_VER_GE, NULL, 0, 0, 2, 0, 0,
	     (EDU_EXEC_RTINE)((uint16_t *)(&(ver7)))},
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_SISU_STATE_MSG *)0)->assignmentAct, 0, NULL},
	    {EDU_TEST_LL_PTR, avsv_edp_sisu_state_info_msg, 0, 0, 0,
	     (long)&((AVSV_SISU_STATE_MSG *)0)->next, 0, NULL},
	    {EDU_END, 0, 0, 0, 0, 0, 0, NULL},
	};

	if (op == EDP_OP_TYPE_ENC) {
		struct_ptr = (AVSV_SISU_STATE_MSG *)ptr;
	} else if (op == EDP_OP_TYPE_DEC) {
		d_ptr = (AVSV_SISU_STATE_MSG **)ptr;
		if (*d_ptr == NULL) {
			*d_ptr = malloc(sizeof(AVSV_SISU_STATE_MSG));
			if (*d_ptr == NULL) {
				*o_err = EDU_ERR_MEM_FAIL;
				return NCSCC_RC_FAILURE;
			}
		}
		memset(*d_ptr, '\0', sizeof(AVSV_SISU_STATE_MSG));
		struct_ptr = *d_ptr;
	} else {
		struct_ptr = ptr;
	}
	rc = m_NCS_EDU_RUN_RULES(hdl, edu_tkn, avsv_sisu_state_msg_rules,
				 struct_ptr, ptr_data_len, buf_env, op, o_err);
	return rc;
}
/*****************************************************************************

  PROCEDURE NAME:   avsv_edp_su_state_info_msg

  DESCRIPTION:      EDU program handler for "AVSV_SU_STATE_MSG" data. This
function is invoked by EDU for performing encode/decode operation on
"AVSV_SU_STATE_MSG" data.

  RETURNS:          NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

*****************************************************************************/
uint32_t avsv_edp_su_state_info_msg(EDU_HDL *hdl, EDU_TKN *edu_tkn,
				    NCSCONTEXT ptr, uint32_t *ptr_data_len,
				    EDU_BUF_ENV *buf_env, EDP_OP_TYPE op,
				    EDU_ERR *o_err)
{
	uint32_t rc = NCSCC_RC_SUCCESS;
	AVSV_SU_STATE_MSG *struct_ptr = NULL, **d_ptr = NULL;

	EDU_INST_SET avsv_su_state_msg_rules[] = {
	    {EDU_START, avsv_edp_su_state_info_msg, EDQ_LNKLIST, 0, 0,
	     sizeof(AVSV_SU_STATE_MSG), 0, NULL},

	    {EDU_EXEC, ncs_edp_sanamet, 0, 0, 0,
	     (long)&((AVSV_SU_STATE_MSG *)0)->safSU, 0, NULL},
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_SU_STATE_MSG *)0)->su_restart_cnt, 0, NULL},
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_SU_STATE_MSG *)0)->su_pres_state, 0, NULL},
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_SU_STATE_MSG *)0)->su_oper_state, 0, NULL},

	    {EDU_TEST_LL_PTR, avsv_edp_su_state_info_msg, 0, 0, 0,
	     (long)&((AVSV_SU_STATE_MSG *)0)->next, 0, NULL},
	    {EDU_END, 0, 0, 0, 0, 0, 0, NULL},
	};

	if (op == EDP_OP_TYPE_ENC) {
		struct_ptr = (AVSV_SU_STATE_MSG *)ptr;
	} else if (op == EDP_OP_TYPE_DEC) {
		d_ptr = (AVSV_SU_STATE_MSG **)ptr;
		if (*d_ptr == NULL) {
			*d_ptr = malloc(sizeof(AVSV_SU_STATE_MSG));
			if (*d_ptr == NULL) {
				*o_err = EDU_ERR_MEM_FAIL;
				return NCSCC_RC_FAILURE;
			}
		}
		memset(*d_ptr, '\0', sizeof(AVSV_SU_STATE_MSG));
		struct_ptr = *d_ptr;
	} else {
		struct_ptr = ptr;
	}
	rc = m_NCS_EDU_RUN_RULES(hdl, edu_tkn, avsv_su_state_msg_rules,
				 struct_ptr, ptr_data_len, buf_env, op, o_err);
	return rc;
}
/*****************************************************************************

  PROCEDURE NAME:   avsv_edp_csicomp_state_info_msg

  DESCRIPTION:      EDU program handler for "AVSV_CSICOMP_STATE_MSG" data. This
function is invoked by EDU for performing encode/decode operation on
"AVSV_CSICOMP_STATE_MSG" data.

  RETURNS:          NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

*****************************************************************************/
uint32_t avsv_edp_csicomp_state_info_msg(EDU_HDL *hdl, EDU_TKN *edu_tkn,
					 NCSCONTEXT ptr, uint32_t *ptr_data_len,
					 EDU_BUF_ENV *buf_env, EDP_OP_TYPE op,
					 EDU_ERR *o_err)
{
	uint32_t rc = NCSCC_RC_SUCCESS;
	AVSV_CSICOMP_STATE_MSG *struct_ptr = NULL, **d_ptr = NULL;

	EDU_INST_SET avsv_csicomp_state_msg_rules[] = {
	    {EDU_START, avsv_edp_csicomp_state_info_msg, EDQ_LNKLIST, 0, 0,
	     sizeof(AVSV_CSICOMP_STATE_MSG), 0, NULL},

	    {EDU_EXEC, ncs_edp_sanamet, 0, 0, 0,
	     (long)&((AVSV_CSICOMP_STATE_MSG *)0)->safComp, 0, NULL},
	    {EDU_EXEC, ncs_edp_sanamet, 0, 0, 0,
	     (long)&((AVSV_CSICOMP_STATE_MSG *)0)->safCSI, 0, NULL},
	    {EDU_EXEC, m_NCS_EDP_SAAMFHASTATET, 0, 0, 0,
	     (long)&((AVSV_CSICOMP_STATE_MSG *)0)->saAmfCSICompHAState, 0,
	     NULL},

	    {EDU_TEST_LL_PTR, avsv_edp_csicomp_state_info_msg, 0, 0, 0,
	     (long)&((AVSV_CSICOMP_STATE_MSG *)0)->next, 0, NULL},
	    {EDU_END, 0, 0, 0, 0, 0, 0, NULL},
	};

	if (op == EDP_OP_TYPE_ENC) {
		struct_ptr = (AVSV_CSICOMP_STATE_MSG *)ptr;
	} else if (op == EDP_OP_TYPE_DEC) {
		d_ptr = (AVSV_CSICOMP_STATE_MSG **)ptr;
		if (*d_ptr == NULL) {
			*d_ptr = malloc(sizeof(AVSV_CSICOMP_STATE_MSG));
			if (*d_ptr == NULL) {
				*o_err = EDU_ERR_MEM_FAIL;
				return NCSCC_RC_FAILURE;
			}
		}
		memset(*d_ptr, '\0', sizeof(AVSV_CSICOMP_STATE_MSG));
		struct_ptr = *d_ptr;
	} else {
		struct_ptr = ptr;
	}
	rc = m_NCS_EDU_RUN_RULES(hdl, edu_tkn, avsv_csicomp_state_msg_rules,
				 struct_ptr, ptr_data_len, buf_env, op, o_err);
	return rc;
}
/*****************************************************************************

  PROCEDURE NAME:   avsv_edp_comp_state_info_msg

  DESCRIPTION:      EDU program handler for "AVSV_COMP_STATE_MSG" data. This
function is invoked by EDU for performing encode/decode operation on
"AVSV_COMP_STATE_MSG" data.

  RETURNS:          NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

*****************************************************************************/
uint32_t avsv_edp_comp_state_info_msg(EDU_HDL *hdl, EDU_TKN *edu_tkn,
				      NCSCONTEXT ptr, uint32_t *ptr_data_len,
				      EDU_BUF_ENV *buf_env, EDP_OP_TYPE op,
				      EDU_ERR *o_err)
{
	uint32_t rc = NCSCC_RC_SUCCESS;
	AVSV_COMP_STATE_MSG *struct_ptr = NULL, **d_ptr = NULL;

	EDU_INST_SET avsv_comp_state_msg_rules[] = {
	    {EDU_START, avsv_edp_comp_state_info_msg, EDQ_LNKLIST, 0, 0,
	     sizeof(AVSV_COMP_STATE_MSG), 0, NULL},

	    {EDU_EXEC, ncs_edp_sanamet, 0, 0, 0,
	     (long)&((AVSV_COMP_STATE_MSG *)0)->safComp, 0, NULL},
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_COMP_STATE_MSG *)0)->comp_restart_cnt, 0, NULL},
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_COMP_STATE_MSG *)0)->comp_pres_state, 0, NULL},
	    {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
	     (long)&((AVSV_COMP_STATE_MSG *)0)->comp_oper_state, 0, NULL},

	    {EDU_TEST_LL_PTR, avsv_edp_comp_state_info_msg, 0, 0, 0,
	     (long)&((AVSV_COMP_STATE_MSG *)0)->next, 0, NULL},
	    {EDU_END, 0, 0, 0, 0, 0, 0, NULL},
	};

	if (op == EDP_OP_TYPE_ENC) {
		struct_ptr = (AVSV_COMP_STATE_MSG *)ptr;
	} else if (op == EDP_OP_TYPE_DEC) {
		d_ptr = (AVSV_COMP_STATE_MSG **)ptr;
		if (*d_ptr == NULL) {
			*d_ptr = malloc(sizeof(AVSV_COMP_STATE_MSG));
			if (*d_ptr == NULL) {
				*o_err = EDU_ERR_MEM_FAIL;
				return NCSCC_RC_FAILURE;
			}
		}
		memset(*d_ptr, '\0', sizeof(AVSV_COMP_STATE_MSG));
		struct_ptr = *d_ptr;
	} else {
		struct_ptr = ptr;
	}
	rc = m_NCS_EDU_RUN_RULES(hdl, edu_tkn, avsv_comp_state_msg_rules,
				 struct_ptr, ptr_data_len, buf_env, op, o_err);
	return rc;
}
