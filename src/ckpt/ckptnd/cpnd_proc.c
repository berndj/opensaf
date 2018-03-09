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
  FILE NAME: cpcn_evt.c

  DESCRIPTION: CPND Event handling routines

  FUNCTIONS INCLUDED in this module:
  cpnd_ckpt_client_add............Add the new client info to ckpt info.
  cpnd_proc_colloc_ckpt_open .....Processing for collocated ckpt open.
  cpnd_proc_noncolloc_ckpt_open...Processing for non-collocated ckpt open.

******************************************************************************/

#include "ckpt/ckptnd/cpnd.h"
#include <saImm.h>
#include <saImmOi.h>
#include <saImmOm.h>
#include "osaf/immutil/immutil.h"
#include "base/saf_error.h"

extern struct ImmutilWrapperProfile immutilWrapperProfile;
/* IMMSv Defs */
#define CPSV_IMM_RELEASE_CODE 'A'
#define CPSV_IMM_MAJOR_VERSION 0x02
#define CPSV_IMM_MINOR_VERSION 0x01

static SaVersionT imm_version = {CPSV_IMM_RELEASE_CODE, CPSV_IMM_MAJOR_VERSION,
				 CPSV_IMM_MINOR_VERSION};

extern uint32_t gl_read_lck;
static void cpnd_dump_ckpt_info(CPND_CKPT_NODE *ckpt_node);
static void cpnd_dump_client_info(CPND_CKPT_CLIENT_NODE *cl_node);
static void cpnd_dump_replica_info(CPND_CKPT_REPLICA_INFO *ckpt_replica_node);
static void cpnd_dump_section_info(CPND_CKPT_SECTION_INFO *sec_info);
static void cpnd_dump_shm_info(NCS_OS_POSIX_SHM_REQ_INFO *open);
static void cpnd_dump_ckpt_attri(CPND_CKPT_NODE *cp_node);
static void cpnd_ckpt_sc_cpnd_mdest_del(CPND_CB *cb);
static void cpnd_headless_ckpt_node_del(CPND_CB *cb);
static SaUint32T cpnd_get_imm_attr(char **attribute_names);

/****************************************************************************
 * Name          : cpnd_ckpt_client_add
 *
 * Description   : This routine will add the client details to the existing
 *                 ckpt info
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 * Notes         : None.
 *****************************************************************************/
uint32_t cpnd_ckpt_client_add(CPND_CKPT_NODE *cp_node,
			      CPND_CKPT_CLIENT_NODE *cl_node)
{
	uint32_t rc = NCSCC_RC_SUCCESS;
	CPND_CKPT_CLLIST_NODE *clist_node;
	CPND_CKPT_CLLIST_NODE *ptr_cl_node = cp_node->clist;

	while (ptr_cl_node) {
		if (ptr_cl_node->cnode->ckpt_app_hdl == cl_node->ckpt_app_hdl) {
			ptr_cl_node->cl_ref_cnt++;
			cl_node->upd_shm = false;
			return rc;
		} else {
			if (ptr_cl_node->next == NULL)
				break;
			else
				ptr_cl_node = ptr_cl_node->next;
		}
	}

	clist_node = m_MMGR_ALLOC_CPND_CKPT_CLIST_NODE;
	memset(clist_node, '\0', sizeof(CPND_CKPT_CLLIST_NODE));

	clist_node->next = ptr_cl_node;
	clist_node->cnode = cl_node;
	clist_node->cl_ref_cnt = 1;
	cp_node->clist = clist_node;
	cl_node->upd_shm = true;

	return rc;
}

/****************************************************************************
 * Name          : cpnd_ckpt_client_find
 *
 * Description   : This routine will find the client details to the existing
 *                 ckpt info
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 * Notes         : None.
 *****************************************************************************/
uint32_t cpnd_ckpt_client_find(CPND_CKPT_NODE *cp_node,
			       CPND_CKPT_CLIENT_NODE *cl_node)
{
	CPND_CKPT_CLLIST_NODE *ptr_cl_node = cp_node->clist;
	uint32_t rc = NCSCC_RC_SUCCESS;

	while (ptr_cl_node != NULL) {
		if (ptr_cl_node->cnode == cl_node)
			return rc;
	}

	return NCSCC_RC_FAILURE;
}

/****************************************************************************
 * Name          : cpnd_ckpt_client_del
 *
 * Description   : This routine will del the client details to the existing
 *                 ckpt info
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 * Notes         : None.
 *****************************************************************************/
uint32_t cpnd_ckpt_client_del(CPND_CKPT_NODE *cp_node,
			      CPND_CKPT_CLIENT_NODE *cl_node)
{

	CPND_CKPT_CLLIST_NODE *ptr_cl_node = cp_node->clist;
	CPND_CKPT_CLLIST_NODE *prev_ptr_cl_node = NULL;

	while (ptr_cl_node != NULL) {
		if (ptr_cl_node->cnode == cl_node)
			break;
		prev_ptr_cl_node = ptr_cl_node;
		ptr_cl_node = ptr_cl_node->next;
	}
	if (ptr_cl_node == NULL) {
		return NCSCC_RC_FAILURE;
	} else {
		ptr_cl_node->cl_ref_cnt--;

		if (ptr_cl_node->cl_ref_cnt) {
			return NCSCC_RC_SUCCESS;
		}

		if (prev_ptr_cl_node == NULL) {
			cp_node->clist = ptr_cl_node->next;
		} else {
			prev_ptr_cl_node->next = ptr_cl_node->next;
		}
	}

	m_MMGR_FREE_CPND_CKPT_CLIST_NODE(ptr_cl_node);
	return NCSCC_RC_SUCCESS;
}

/****************************************************************************
 * Name          : cpnd_client_ckpt_info_add;
 *
 * Description   : This routine will add the ckpt referenced by this
 *                 client.
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 * Notes         : None.
 *****************************************************************************/
uint32_t cpnd_client_ckpt_info_add(CPND_CKPT_CLIENT_NODE *cl_node,
				   CPND_CKPT_NODE *cp_node)
{
	uint32_t rc = NCSCC_RC_SUCCESS;

	CPND_CKPT_CKPT_LIST_NODE *cplist = NULL;
	CPND_CKPT_CKPT_LIST_NODE *ptr_cp_node = cl_node->ckpt_list;

	cplist = m_MMGR_ALLOC_CPND_CKPT_LIST_NODE;

	/* need to check */
	memset(cplist, '\0', sizeof(CPND_CKPT_CKPT_LIST_NODE));

	cplist->next = ptr_cp_node;
	cplist->cnode = cp_node;

	cl_node->ckpt_list = cplist;

	return rc;
}

/****************************************************************************
 * Name          : cpnd_client_ckpt_info_del.
 *
 * Description   : This routine will del the ckpt refrence info
 *                 from this client.
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 * Notes         : None.
 *****************************************************************************/
uint32_t cpnd_client_ckpt_info_del(CPND_CKPT_CLIENT_NODE *cl_node,
				   CPND_CKPT_NODE *cp_node)
{

	CPND_CKPT_CKPT_LIST_NODE *ptr_cp_node = cl_node->ckpt_list;
	CPND_CKPT_CKPT_LIST_NODE *prev_ptr_cp_node = NULL;

	while (ptr_cp_node != NULL) {
		if (ptr_cp_node->cnode == cp_node)
			break;
		prev_ptr_cp_node = ptr_cp_node;
		ptr_cp_node = ptr_cp_node->next;
	}
	if (ptr_cp_node == NULL) {
		return NCSCC_RC_FAILURE;
	} else {
		if (prev_ptr_cp_node == NULL)
			cl_node->ckpt_list = ptr_cp_node->next;
		else
			prev_ptr_cp_node->next = ptr_cp_node->next;
	}
	m_MMGR_FREE_CPND_CKPT_LIST_NODE(ptr_cp_node);
	return NCSCC_RC_SUCCESS;
}

/****************************************************************************
 * Name          : cpnd_client_ckpt_info_delete.
 *
 * Description   : This routine will delete all the ckpt refrence info
 *                 from this client.
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 * Notes         : None.
 *****************************************************************************/
uint32_t cpnd_client_ckpt_info_delete(CPND_CKPT_CLIENT_NODE *cl_node,
				      CPND_CKPT_NODE *cp_node)
{

	CPND_CKPT_CKPT_LIST_NODE *ptr_cp_node = cl_node->ckpt_list;
	CPND_CKPT_CKPT_LIST_NODE *prev_ptr_cp_node = NULL, *free_cp_node = NULL;
	;

	while (ptr_cp_node != NULL) {

		if (ptr_cp_node->cnode == cp_node) {
			free_cp_node = ptr_cp_node;
			ptr_cp_node = ptr_cp_node->next;

			if (prev_ptr_cp_node == NULL)
				cl_node->ckpt_list = free_cp_node->next;
			else
				prev_ptr_cp_node->next = free_cp_node->next;

			m_MMGR_FREE_CPND_CKPT_LIST_NODE(free_cp_node);
		} else {
			prev_ptr_cp_node = ptr_cp_node;
			ptr_cp_node = ptr_cp_node->next;
		}
	}
	return NCSCC_RC_SUCCESS;
}

/**************************************************************************
 * Name         : cpnd_proc_ckpt_arrival_info_ntfy
 *
 * Description  :
 *
 *************************************************************************/
uint32_t cpnd_proc_ckpt_arrival_info_ntfy(CPND_CB *cb, CPND_CKPT_NODE *cp_node,
					  CPSV_CKPT_ACCESS *in_evt,
					  CPSV_SEND_INFO *sinfo)
{
	CPND_CKPT_CLLIST_NODE *ptr_cl_node = cp_node->clist;
	CPSV_EVT send_evt;
	memset(&send_evt, '\0', sizeof(CPSV_EVT));
	uint32_t rc = NCSCC_RC_SUCCESS;

	while (ptr_cl_node != NULL) {
		if (ptr_cl_node->cnode->arrival_cb_flag == true) {
			send_evt.type = CPSV_EVT_TYPE_CPA;
			send_evt.info.cpa.type = CPA_EVT_ND2A_CKPT_ARRIVAL_NTFY;
			send_evt.info.cpa.info.arr_msg.client_hdl =
			    ptr_cl_node->cnode->ckpt_app_hdl;
			send_evt.info.cpa.info.arr_msg.ckpt_hdl =
			    cp_node->ckpt_id;
			send_evt.info.cpa.info.arr_msg.lcl_ckpt_hdl =
			    in_evt->lcl_ckpt_id;
			send_evt.info.cpa.info.arr_msg.mdest =
			    in_evt->agent_mdest;
			send_evt.info.cpa.info.arr_msg.num_of_elmts =
			    in_evt->num_of_elmts;
			send_evt.info.cpa.info.arr_msg.ckpt_data = in_evt->data;
			send_evt.info.cpa.info.arr_msg.ckpt_data->readSize = 0;

			rc = cpnd_mds_msg_send(
			    cb, NCSMDS_SVC_ID_CPA,
			    ptr_cl_node->cnode->agent_mds_dest, &send_evt);
		}
		ptr_cl_node = ptr_cl_node->next;
	}
	return rc;
}

uint32_t cpnd_proc_ckpt_clm_node_left(CPND_CB *cb)
{
	CPSV_EVT send_evt;
	memset(&send_evt, '\0', sizeof(CPSV_EVT));
	uint32_t rc = NCSCC_RC_SUCCESS;
	cb->is_joined_cl = false;
	send_evt.type = CPSV_EVT_TYPE_CPA;
	send_evt.info.cpa.type = CPA_EVT_ND2A_CKPT_CLM_NODE_LEFT;
	rc = cpnd_mds_bcast_send(cb, &send_evt, NCSMDS_SVC_ID_CPA);
	return rc;
}

uint32_t cpnd_proc_ckpt_clm_node_joined(CPND_CB *cb)
{
	CPSV_EVT send_evt;
	memset(&send_evt, '\0', sizeof(CPSV_EVT));
	uint32_t rc = NCSCC_RC_SUCCESS;
	cb->is_joined_cl = true;
	send_evt.type = CPSV_EVT_TYPE_CPA;
	send_evt.info.cpa.type = CPA_EVT_ND2A_CKPT_CLM_NODE_JOINED;
	rc = cpnd_mds_bcast_send(cb, &send_evt, NCSMDS_SVC_ID_CPA);
	return rc;
}

/****************************************************************************
 * Name          : cpnd_ckpt_replica_destroy
 *
 * Description   : This routine will create the replica for the non collocated
 *                 ckpt, and send the details to CPD. Then sync the local
 *                 replica with the active replica.
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 * Notes         : None.
 *****************************************************************************/
uint32_t cpnd_ckpt_replica_destroy(CPND_CB *cb, CPND_CKPT_NODE *cp_node,
				   SaAisErrorT *error)
{
	NCS_OS_POSIX_SHM_REQ_INFO shm_info;
	CPSV_EVT evt, *out_evt = NULL;
	uint32_t rc = NCSCC_RC_SUCCESS;
	CPND_CPD_DEFERRED_REQ_NODE *node = NULL;

	TRACE_ENTER();
	if (cp_node->cpnd_rep_create) {

		/* First delete all sections in the heckpoint about to be
		 * deleted */
		cpnd_ckpt_delete_all_sect(cp_node);

		memset(&shm_info, '\0', sizeof(shm_info));

		shm_info.type = NCS_OS_POSIX_SHM_REQ_CLOSE;
		shm_info.info.close.i_addr =
		    cp_node->replica_info.open.info.open.o_addr;
		shm_info.info.close.i_fd =
		    cp_node->replica_info.open.info.open.o_fd;
		shm_info.info.close.i_hdl =
		    cp_node->replica_info.open.info.open.o_hdl;
		shm_info.info.close.i_size =
		    cp_node->replica_info.open.info.open.i_size;

		rc = ncs_os_posix_shm(&shm_info);

		if (rc == NCSCC_RC_FAILURE) {
			TRACE_4("cpnd ckpt close failed for ckpt_id:%llx",
				cp_node->ckpt_id);
			*error = SA_AIS_ERR_LIBRARY;
			TRACE_LEAVE();
			return rc;
		}

		/* unlink the name */
		shm_info.type = NCS_OS_POSIX_SHM_REQ_UNLINK;
		shm_info.info.unlink.i_name =
		    cp_node->replica_info.open.info.open.i_name;

		rc = ncs_os_posix_shm(&shm_info);

		if (rc == NCSCC_RC_FAILURE) {
			TRACE_4("cpnd ckpt unlink failed ckpt_id:%llx",
				cp_node->ckpt_id);
			*error = SA_AIS_ERR_LIBRARY;
			TRACE_LEAVE();
			return rc;
		}

		if (cb->num_rep)
			cb->num_rep--;

		m_MMGR_FREE_CPND_DEFAULT(
		    cp_node->replica_info.open.info.open.i_name);

		/* freeing the sec_mapping memory */
		if (cp_node->replica_info.shm_sec_mapping)
			m_MMGR_FREE_CPND_DEFAULT(
			    cp_node->replica_info.shm_sec_mapping);
	}

	if (!m_CPND_IS_COLLOCATED_ATTR_SET(
		cp_node->create_attrib.creationFlags)) {
		if (cp_node->is_active_exist &&
		    !m_NCS_MDS_DEST_EQUAL(&cp_node->active_mds_dest,
					  &cb->cpnd_mdest_id)) {
			/*  Active replica present on other CPND ,it will send
			 * destroy evt to CPD */
			goto done;
		}
	}

	/* send destroy evt to CPD */
	memset(&evt, '\0', sizeof(CPSV_EVT));

	evt.type = CPSV_EVT_TYPE_CPD;
	evt.info.cpd.type = CPD_EVT_ND2D_CKPT_DESTROY;
	evt.info.cpd.info.ckpt_destroy.ckpt_id = cp_node->ckpt_id;

	rc = cpnd_mds_msg_sync_send(cb, NCSMDS_SVC_ID_CPD, cb->cpd_mdest_id,
				    &evt, &out_evt, CPSV_WAIT_TIME);

	if (rc != NCSCC_RC_SUCCESS) {
		TRACE_4("cpnd mds msg sync send failed  %d", rc);
		m_NCS_LOCK(&cb->cpnd_cpd_up_lock, NCS_LOCK_WRITE);

		if ((rc == NCSCC_RC_REQ_TIMOUT) ||
		    ((rc == NCSCC_RC_FAILURE) && (!cb->is_cpd_up))) {

			node = (CPND_CPD_DEFERRED_REQ_NODE *)
			    m_MMGR_ALLOC_CPND_CPD_DEFERRED_REQ_NODE;
			if (!node) {
				TRACE_4(
				    "cpnd cpd deferred req node memory allocation failed");
				m_NCS_UNLOCK(&cb->cpnd_cpd_up_lock,
					     NCS_LOCK_WRITE);
				*error = SA_AIS_ERR_NO_MEMORY;
				TRACE_LEAVE();
				return NCSCC_RC_FAILURE;
			}

			memset(node, '\0', sizeof(CPND_CPD_DEFERRED_REQ_NODE));
			node->evt = evt;

			ncs_enqueue(&cb->cpnd_cpd_deferred_reqs_list,
				    (void *)node);
		} else {
			m_NCS_UNLOCK(&cb->cpnd_cpd_up_lock, NCS_LOCK_WRITE);
			*error = SA_AIS_ERR_TRY_AGAIN;
			TRACE_LEAVE();
			return NCSCC_RC_FAILURE;
		}
		m_NCS_UNLOCK(&cb->cpnd_cpd_up_lock, NCS_LOCK_WRITE);
	}

	cpnd_evt_destroy(out_evt);
done:
	*error = SA_AIS_OK;
	TRACE_LEAVE();
	return NCSCC_RC_SUCCESS;
}

/****************************************************************************
 * Name          : cpnd_ckpt_replica_create
 *
 * Description   : Function to process the
 *                 from Applications.
 *
 * Arguments     : CPND_CB *cb - CPND CB pointer
 *                 CPND_CKPT_NODE - ckpt node pointer
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 * Notes         : None.
 *****************************************************************************/
uint32_t cpnd_ckpt_replica_create(CPND_CB *cb, CPND_CKPT_NODE *cp_node)
{

	uint32_t rc = NCSCC_RC_SUCCESS;
	char *buf;
	int32_t sec_cnt = 0;

	TRACE_ENTER();
	/* Check  maximum number of allowed replicas ,if exceeded Return Error
	 * no resource */
	if (!(cb->num_rep < CPND_MAX_REPLICAS)) {
		LOG_ER(
		    "cpnd has exceeded the maximum number of allowed replicas (CPND_MAX_REPLICAS)");
		TRACE_LEAVE();
		return NCSCC_RC_FAILURE;
	}

	buf = m_MMGR_ALLOC_CPND_DEFAULT(CPND_MAX_REPLICA_NAME_LENGTH);
	memset(buf, '\0', CPND_MAX_REPLICA_NAME_LENGTH);
	strncpy(buf, cp_node->ckpt_name, CPND_REP_NAME_MAX_CKPT_NAME_LENGTH);

	sprintf(buf + strlen(buf) - 1, "_%u_%llu",
		(uint32_t)m_NCS_NODE_ID_FROM_MDS_DEST(cb->cpnd_mdest_id),
		cp_node->ckpt_id);
	/* size of chkpt */
	memset(&cp_node->replica_info.open, '\0',
	       sizeof(cp_node->replica_info.open));

	cp_node->replica_info.open.type = NCS_OS_POSIX_SHM_REQ_OPEN;
	cp_node->replica_info.open.info.open.i_size =
	    sizeof(CPSV_CKPT_HDR) +
	    cp_node->create_attrib.maxSections *
		(sizeof(CPSV_SECT_HDR) + cp_node->create_attrib.maxSectionSize);
	cp_node->replica_info.open.ensures_space =
	    cb->shm_alloc_guaranteed == 1;

	cp_node->replica_info.open.info.open.i_offset = 0;
	cp_node->replica_info.open.info.open.i_name = buf;
	cp_node->replica_info.open.info.open.i_map_flags = MAP_SHARED;
	cp_node->replica_info.open.info.open.o_addr = NULL;
	cp_node->replica_info.open.info.open.i_flags = O_RDWR;

again:
	rc = ncs_os_posix_shm(&cp_node->replica_info.open);

	if (rc == NCSCC_RC_FAILURE) {
		if ((cp_node->replica_info.open.info.open.i_flags & O_RDWR) &&
		    (cp_node->replica_info.open.info.open.i_flags & O_CREAT)) {
			TRACE_4(
			    "cpnd ckpt rep create failed with return value %d",
			    rc);
			TRACE_LEAVE();
			return NCSCC_RC_FAILURE;
		} else {
			/* the replica does not exist, we need to create it */
			cp_node->replica_info.open.info.open.i_flags =
			    O_RDWR | O_CREAT;
			goto again;
		}
	}

	if (cp_node->replica_info.open.info.open.i_flags & O_CREAT)
		cb->num_rep++;

	cp_node->replica_info.shm_sec_mapping =
	    (uint32_t *)m_MMGR_ALLOC_CPND_DEFAULT(
		sizeof(uint32_t) * cp_node->create_attrib.maxSections);

	for (; sec_cnt < cp_node->create_attrib.maxSections; sec_cnt++)
		cp_node->replica_info.shm_sec_mapping[sec_cnt] = 1;

	TRACE_LEAVE();
	return rc;
}

/****************************************************************************
 * Name          : cpnd_ckpt_remote_cpnd_add
 *
 * Description   : This routine will add the remote cpnd mdest to the existing
 *                 ckpt info
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 * Notes         : None.
 *****************************************************************************/
uint32_t cpnd_ckpt_remote_cpnd_add(CPND_CKPT_NODE *cp_node, MDS_DEST mds_info)
{

	uint32_t rc = NCSCC_RC_SUCCESS;

	TRACE_ENTER();
	CPSV_CPND_DEST_INFO *cpnd_mdest;
	CPSV_CPND_DEST_INFO *ptr_cpnd_mdest = cp_node->cpnd_dest_list;
	CPSV_CPND_DEST_INFO *cpnd_mdest_trav = cp_node->cpnd_dest_list;

	while (cpnd_mdest_trav) {
		if (m_CPND_IS_LOCAL_NODE(&cpnd_mdest_trav->dest, &mds_info) ==
		    0) {
			TRACE_LEAVE();
			return rc;
		} else
			cpnd_mdest_trav = cpnd_mdest_trav->next;
	}

	cpnd_mdest = m_MMGR_ALLOC_CPND_DEST_INFO;
	memset(cpnd_mdest, '\0', sizeof(CPSV_CPND_DEST_INFO));

	cpnd_mdest->next = ptr_cpnd_mdest;
	cpnd_mdest->dest = mds_info;

	cp_node->cpnd_dest_list = cpnd_mdest;
	TRACE_4("cpnd replica add success ckpt_id:%llx mds_info:%" PRIu64,
		cp_node->ckpt_id, mds_info);
	TRACE_LEAVE();
	return rc;
}

/****************************************************************************
 * Name          : cpnd_ckpt_remote_cpnd_del
 *
 * Description   : This routine will delete the remote cpnd details to the
 *existing ckpt info
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 * Notes         : None.
 *****************************************************************************/
uint32_t cpnd_ckpt_remote_cpnd_del(CPND_CKPT_NODE *cp_node, MDS_DEST mds_info)
{

	CPSV_CPND_DEST_INFO *ptr_cpnd_mdest = cp_node->cpnd_dest_list;
	CPSV_CPND_DEST_INFO *prev_ptr_cpnd_mdest = NULL;

	TRACE_ENTER();
	while (ptr_cpnd_mdest != NULL) {

		if (m_CPND_IS_LOCAL_NODE(&ptr_cpnd_mdest->dest, &mds_info) == 0)
			break;

		prev_ptr_cpnd_mdest = ptr_cpnd_mdest;
		ptr_cpnd_mdest = ptr_cpnd_mdest->next;
	}
	if (ptr_cpnd_mdest == NULL) {
		TRACE_4(
		    "Remote Cpnd entry does not exist in dest list mds_dest:%" PRIu64,
		    mds_info);
		return NCSCC_RC_FAILURE;
	} else {
		if (prev_ptr_cpnd_mdest == NULL)
			cp_node->cpnd_dest_list = ptr_cpnd_mdest->next;
		else
			prev_ptr_cpnd_mdest->next = ptr_cpnd_mdest->next;
	}

	m_MMGR_FREE_CPND_DEST_INFO(ptr_cpnd_mdest);
	TRACE_LEAVE();
	return NCSCC_RC_SUCCESS;
}

/****************************************************************************
 * Name          :  cpnd_ckpt_get_lck_sec_id
 *
 * Description   : Function to process the
 *                 from Applications.
 *
 * Arguments     : CPND_CKPT_NODE *cp_node  - CPND CKPT pointer
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 * Notes         : None.
 *****************************************************************************/
int32_t cpnd_ckpt_get_lck_sec_id(CPND_CKPT_NODE *cp_node)
{
	uint32_t i = 0;

	for (; i < cp_node->create_attrib.maxSections; i++)
		if (cp_node->replica_info.shm_sec_mapping[i] == 1) {
			cp_node->replica_info.shm_sec_mapping[i] = 0;
			break;
		}
	if (i == cp_node->create_attrib.maxSections) {
		return -1;
	}

	return i;
}

/****************************************************************************
 * Name          : cpnd_ckpt_sec_write
 *
 * Description   : Function to process the
 *                 from Applications.
 *
 * Arguments     : CPND_CB *cb - CPND CB pointer
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 * Notes         : None.
 *****************************************************************************/
uint32_t cpnd_ckpt_sec_write(CPND_CB *cb, CPND_CKPT_NODE *cp_node,
			     CPND_CKPT_SECTION_INFO *sec_info, const void *data,
			     uint64_t size, uint64_t offset, uint32_t type)
{ /* for sync type=2 */
	uint32_t rc = NCSCC_RC_SUCCESS;
	NCS_OS_POSIX_SHM_REQ_INFO write_req;

	TRACE_ENTER();
	/* checking to write,whether it is possible to write or not */

	if (type == 0) {
		if (offset + size > cp_node->create_attrib.maxSectionSize) {
			return NCSCC_RC_FAILURE;
		}
	} else if (type == 1) { /* Over write Case */
		if (size > cp_node->create_attrib.maxSectionSize) {
			return NCSCC_RC_FAILURE;
		}
	}

	write_req.type = NCS_OS_POSIX_SHM_REQ_WRITE;
	write_req.info.write.i_addr =
	    (void *)((char *)cp_node->replica_info.open.info.open.o_addr +
		     sizeof(CPSV_CKPT_HDR) +
		     ((sec_info->lcl_sec_id + 1) * sizeof(CPSV_SECT_HDR)) +
		     (sec_info->lcl_sec_id *
		      cp_node->create_attrib.maxSectionSize));
	write_req.info.write.i_from_buff = (uint8_t *)data;

	/* if ( type == 0) Needs to be cleaned up later TBD
	   write_req.info.write.i_offset=sec_info->size + offset;
	   else */
	write_req.info.write.i_offset = offset;

	write_req.info.write.i_write_size = size;
	write_req.ensures_space = cb->shm_alloc_guaranteed != 0;
	if (ncs_os_posix_shm(&write_req) == NCSCC_RC_FAILURE) {
		LOG_ER("shm write failed for cpnd_ckpt_sec_write");
		return NCSCC_RC_FAILURE;
	}

	m_GET_TIME_STAMP(sec_info->lastUpdate);

	if (type == 0) {
		if (sec_info->sec_size < offset + size) {
			cp_node->replica_info.mem_used =
			    cp_node->replica_info.mem_used +
			    ((offset + size) - sec_info->sec_size);
			sec_info->sec_size = offset + size;
		}

		/* SECTION HEADER UPDATE */
		if (cpnd_sec_hdr_update(cb, sec_info, cp_node) ==
		    NCSCC_RC_FAILURE) {
			LOG_ER("cpnd sect hdr update failed");
			rc = NCSCC_RC_FAILURE;
		}

	} else if ((type == 1) || (type == 3)) {
		cp_node->replica_info.mem_used -= sec_info->sec_size;
		sec_info->sec_size = size;
		cp_node->replica_info.mem_used += size;
		if ((cpnd_sec_hdr_update(cb, sec_info, cp_node)) ==
		    NCSCC_RC_FAILURE) {
			LOG_ER("cpnd sect hdr update failed");
			rc = NCSCC_RC_FAILURE;
		}
	}
	TRACE_LEAVE();
	return rc;
}

/****************************************************************************
 * Name          : cpnd_ckpt_sec_read
 *
 * Description   : Function to process the
 *                 from Applications.
 *
 * Arguments     : CPND_CB *cb - CPND CB pointer
 *                 CPSV_EVT *evt - Received Event structure
 *                 CPSV_SEND_INFO *sinfo - Sender MDS information.
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 * Notes         : None.
 *****************************************************************************/
uint32_t cpnd_ckpt_sec_read(CPND_CKPT_NODE *cp_node,
			    CPND_CKPT_SECTION_INFO *sec_info, void *data,
			    uint64_t size, uint64_t offset)
{

	uint32_t rc = NCSCC_RC_SUCCESS;
	NCS_OS_POSIX_SHM_REQ_INFO read_req;
	read_req.type = NCS_OS_POSIX_SHM_REQ_READ;
	read_req.info.read.i_addr =
	    (void *)((char *)cp_node->replica_info.open.info.open.o_addr +
		     sizeof(CPSV_CKPT_HDR) +
		     ((sec_info->lcl_sec_id + 1) * sizeof(CPSV_SECT_HDR)) +
		     (sec_info->lcl_sec_id *
		      cp_node->create_attrib.maxSectionSize));
	read_req.info.read.i_to_buff = data;
	read_req.info.read.i_read_size = size;
	read_req.info.read.i_offset = offset;

	rc = ncs_os_posix_shm(&read_req);

	return rc;
}

/****************************************************************************
 * Name          : cpnd_proc_cpd_down
 *
 * Description   : Function to handle Director going down
 *
 * Arguments     : CPND_CB *cb - CPND CB pointer
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 * Notes         : Policy used for handling cpd down is to blindly cleanup
 *cpnd_cb
 *****************************************************************************/
void cpnd_proc_cpd_down(CPND_CB *cb)
{
	if (cb->scAbsenceAllowed) {
		/* cleanup all SC cpnd mdests in the ckpt node dest list */
		cpnd_ckpt_sc_cpnd_mdest_del(cb);

		/* cleanup ckpt_node and replica */
		cpnd_headless_ckpt_node_del(cb);
	} else {
		/* cleanup ckpt node tree */
		cpnd_ckpt_node_tree_cleanup(cb);

		/* cleanup client node tree */
		cpnd_client_node_tree_cleanup(cb);
	}
}

/****************************************************************************
 * Name          : cpnd_proc_cpa_down
 *
 * Description   : Function to process agent going down
 *                 from Applications.
 *
 * Arguments     : CPND_CB *cb - CPND CB pointer
 *                 MDS_DEST dest - Agent MDS_DEST
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 * Notes         : None.
 *****************************************************************************/
void cpnd_proc_cpa_down(CPND_CB *cb, MDS_DEST dest)
{
	CPND_CKPT_CLIENT_NODE *cl_node = NULL;
	CPND_CKPT_NODE *cp_node = NULL;
	SaCkptHandleT prev_ckpt_hdl;
	SaAisErrorT error;
	uint32_t rc = NCSCC_RC_SUCCESS;
	SaCkptCheckpointOpenFlagsT tmp_open_flags;

	TRACE_ENTER();

	/* go through the client tree ,need to check tmr list ,for close stuff
	 */
	cpnd_client_node_getnext(cb, 0, &cl_node);

	while (cl_node) {
		prev_ckpt_hdl = cl_node->ckpt_app_hdl;

		if (memcmp(&dest, &cl_node->agent_mds_dest, sizeof(MDS_DEST)) ==
		    0) {
			while (cl_node->ckpt_list != NULL) {
				cp_node = cl_node->ckpt_list->cnode;

				/* Process the pending write events for this
				 * checkpoint if any */
				cpnd_proc_pending_writes(cb, cp_node, dest);

				cpnd_ckpt_client_del(cp_node, cl_node);
				cpnd_client_ckpt_info_del(cl_node, cp_node);

				if (cp_node->ckpt_lcl_ref_cnt)
					cp_node->ckpt_lcl_ref_cnt--;

				cpnd_restart_client_reset(cb, cp_node, cl_node);
				tmp_open_flags = 0;

				if (cl_node->ckpt_open_ref_cnt) {

					if (cp_node->open_flags &
					    SA_CKPT_CHECKPOINT_CREATE) {

						tmp_open_flags =
						    tmp_open_flags |
						    SA_CKPT_CHECKPOINT_CREATE;
						cp_node->open_flags = 0;
					}

					if (cl_node->open_reader_flags_cnt) {
						tmp_open_flags =
						    tmp_open_flags |
						    SA_CKPT_CHECKPOINT_READ;
						cl_node
						    ->open_reader_flags_cnt--;
					}

					if (cl_node->open_writer_flags_cnt) {
						tmp_open_flags =
						    tmp_open_flags |
						    SA_CKPT_CHECKPOINT_WRITE;
						cl_node
						    ->open_writer_flags_cnt--;
					}

					rc = cpnd_send_ckpt_usr_info_to_cpd(
					    cb, cp_node, tmp_open_flags,
					    CPSV_USR_INFO_CKPT_CLOSE);
					if (rc != NCSCC_RC_SUCCESS) {
						TRACE_4("cpnd mds send failed");
					}
					cl_node->ckpt_open_ref_cnt--;
				}
				TRACE_1(
				    "CPND - Checkpoint Close Success , cli_hdl/ckpt_id/lcl_ref_cnt %llx,%llx,%d",
				    cl_node->ckpt_app_hdl, cp_node->ckpt_id,
				    cp_node->ckpt_lcl_ref_cnt);

				if (m_CPND_IS_COLLOCATED_ATTR_SET(
					cp_node->create_attrib.creationFlags)) {
					rc = cpnd_ckpt_replica_close(
					    cb, cp_node, &error);
					if (rc != NCSCC_RC_SUCCESS) {
						TRACE_4(
						    "cpnd ckpt replica close failed ckpt_id:%llx",
						    cp_node->ckpt_id);
					}
				}
				tmp_open_flags = 0;
			}

			/* CPND RESTART - FREE THE GLOBAL SHARED MEMORY */
			TRACE_4(
			    "cpnd ckpt client del success ckpt_app_hdl:%llx",
			    cl_node->ckpt_app_hdl);
			cpnd_restart_client_node_del(cb, cl_node);
			cpnd_client_node_del(cb, cl_node);
			m_MMGR_FREE_CPND_CKPT_CLIENT_NODE(cl_node);
		}

		cpnd_client_node_getnext(cb, prev_ckpt_hdl, &cl_node);
	}
	TRACE_LEAVE();
}

/**************************************************************************
 * Name     :  cpnd_proc_cpa_up
 *
 * Description   : Function to process agent going down
 *                 from Applications.
 *
 * Arguments     : CPND_CB *cb - CPND CB pointer
 *                 MDS_DEST dest - Agent MDS_DEST
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 * Notes         : None.
 *****************************************************************************/
void cpnd_proc_cpa_up(CPND_CB *cb, MDS_DEST dest)
{
	CPND_CKPT_CLIENT_NODE *cl_node = NULL;
	SaCkptHandleT prev_ckpt_hdl;

	cpnd_client_node_getnext(cb, 0, &cl_node);

	while (cl_node) {
		prev_ckpt_hdl = cl_node->ckpt_app_hdl;
		if (memcmp(&dest, &cl_node->agent_mds_dest, sizeof(MDS_DEST)) ==
		    0) {
			cl_node->app_status = true;
		}
		cpnd_client_node_getnext(cb, prev_ckpt_hdl, &cl_node);
	}
}

/**************************************************************************
 * Name     :  cpnd_proc_app_status
 *
 * Description   : Function to process agent going down
 *                 from Applications.
 *
 * Arguments     : CPND_CB *cb - CPND CB pointer
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 * Notes         : None.
 *****************************************************************************/
void cpnd_proc_app_status(CPND_CB *cb)
{
	CPND_CKPT_CLIENT_NODE *cl_node = NULL;
	SaCkptHandleT prev_ckpt_hdl;
	CPSV_EVT send_evt, *evt = NULL;
	uint32_t rc = NCSCC_RC_SUCCESS;

	cpnd_client_node_getnext(cb, 0, &cl_node);

	while (cl_node) {
		prev_ckpt_hdl = cl_node->ckpt_app_hdl;
		memset(&send_evt, 0, sizeof(CPSV_EVT));
		send_evt.type = CPSV_EVT_TYPE_CPA;
		send_evt.info.cpa.type = CPA_EVT_ND2A_CKPT_BCAST_SEND;
		rc = cpnd_mds_msg_send(cb, NCSMDS_SVC_ID_CPA,
				       cl_node->agent_mds_dest, &send_evt);

		if (rc == NCSCC_RC_FAILURE) {
			/* Post the CPA_DOWN event to CPND mailbox, this is in
			 * Main thread */
			evt = m_MMGR_ALLOC_CPSV_EVT(NCS_SERVICE_ID_CPND);
			memset(evt, 0, sizeof(CPSV_EVT));
			evt->type = CPSV_EVT_TYPE_CPND;
			evt->info.cpnd.type = CPND_EVT_MDS_INFO;
			evt->info.cpnd.info.mds_info.change = NCSMDS_DOWN;
			evt->info.cpnd.info.mds_info.svc_id = NCSMDS_SVC_ID_CPA;
			evt->info.cpnd.info.mds_info.dest =
			    cl_node->agent_mds_dest;

			rc = m_NCS_IPC_SEND(&cb->cpnd_mbx, (NCSCONTEXT)evt,
					    NCS_IPC_PRIORITY_VERY_HIGH);
		}

		cpnd_client_node_getnext(cb, prev_ckpt_hdl, &cl_node);
	}
}

/****************************************************************************
 * Name          : cpnd_ckpt_update_replica
 *
 * Description   : Function to process
 *                 from Applications.
 *
 * Arguments     : CPND_CB *cb - CPND CB pointer
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 * Notes         : None.
 *****************************************************************************/
uint32_t cpnd_ckpt_update_replica(CPND_CB *cb, CPND_CKPT_NODE *cp_node,
				  CPSV_CKPT_ACCESS *write_data, uint32_t type,
				  uint32_t *err_type, uint32_t *errflag)
{
	uint32_t rc = NCSCC_RC_SUCCESS;
	uint32_t i = 0;
	CPSV_CKPT_DATA *data;
	CPND_CKPT_SECTION_INFO *sec_info = NULL;

	TRACE_ENTER();
	data = write_data->data;
	for (; i < write_data->num_of_elmts; i++) {
		sec_info = cpnd_ckpt_sec_get_create(cp_node, &data->sec_id);
		if (sec_info == NULL) {
			if (type == CPSV_CKPT_ACCESS_SYNC) {
				sec_info = cpnd_ckpt_sec_add(
				    cb, cp_node, &data->sec_id,
				    data->expirationTime, 0);

				if (sec_info == NULL) {
					TRACE_4(
					    "cpnd - ckpt sect add failed , sec_id:%s ckpt_id:%llx",
					    data->sec_id.id, cp_node->ckpt_id);
					TRACE_LEAVE();
					return NCSCC_RC_FAILURE;
				}
			} else {
				TRACE_4("cpnd replica has no sections");
				*errflag = i;
				*err_type = CKPT_UPDATE_REPLICA_NO_SECTION;
				TRACE_LEAVE();
				return NCSCC_RC_FAILURE;
			}
		}

		rc = cpnd_ckpt_sec_write(cb, cp_node, sec_info, data->data,
					 data->dataSize, data->dataOffset,
					 write_data->type);
		if (rc == NCSCC_RC_FAILURE) {
			TRACE("cpnd ckpt sect write failed for sec_id:%s",
			      data->sec_id.id);
			*errflag = i;
			*err_type = CKPT_UPDATE_REPLICA_RES_ERR;
			TRACE_LEAVE();
			return rc;
		}

		data = data->next;
	}
	return rc;
}

/****************************************************************************
 * Name          : cpnd_ckpt_read_replica
 *
 * Description   : Function to process
 *                 from Applications.
 *
 * Arguments     : CPND_CB *cb - CPND CB pointer
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 * Notes         : None.
 *****************************************************************************/
uint32_t cpnd_ckpt_read_replica(CPND_CB *cb, CPND_CKPT_NODE *cp_node,
				CPSV_CKPT_ACCESS *read_data, CPSV_EVT *evt)
{

	uint32_t rc = NCSCC_RC_SUCCESS;
	uint32_t i = 0, read_size = 0, j = 0;
	CPSV_CKPT_DATA *data;
	CPND_CKPT_SECTION_INFO *sec_info = NULL;
	CPSV_ND2A_READ_DATA *ptr_read_data;

	TRACE_ENTER();
	evt->info.cpa.info.sec_data_rsp.num_of_elmts = read_data->num_of_elmts;
	evt->info.cpa.info.sec_data_rsp.size = read_data->num_of_elmts;

	evt->info.cpa.info.sec_data_rsp.info.read_data =
	    (CPSV_ND2A_READ_DATA *)m_MMGR_ALLOC_CPND_DEFAULT(
		sizeof(CPSV_ND2A_READ_DATA) * read_data->num_of_elmts);
	if (evt->info.cpa.info.sec_data_rsp.info.read_data == NULL) {
		TRACE_4("cpnd cpsv nd2a read data memory allocation failed");
		evt->info.cpa.info.sec_data_rsp.num_of_elmts = -1;
		evt->info.cpa.info.sec_data_rsp.error = SA_AIS_ERR_NO_MEMORY;
		evt->info.cpa.info.sec_data_rsp.size = 0;
		TRACE_LEAVE();
		return NCSCC_RC_FAILURE;
	}

	memset(evt->info.cpa.info.sec_data_rsp.info.read_data, '\0',
	       sizeof(CPSV_ND2A_READ_DATA) * read_data->num_of_elmts);

	ptr_read_data = evt->info.cpa.info.sec_data_rsp.info.read_data;
	data = read_data->data;

	for (; i < read_data->num_of_elmts; i++, data = data->next) {
		sec_info = cpnd_ckpt_sec_get(cp_node, &data->sec_id);
		if (sec_info == NULL) {
			TRACE_4(
			    "cpnd ckpt sect get failed for section id:%s,ckpt_id:%llx",
			    data->sec_id.id, cp_node->ckpt_id);
			ptr_read_data[i].data = NULL;
			ptr_read_data[i].err = 1;
			continue;
		}

		if (data->dataOffset > sec_info->sec_size) {
			TRACE_4("CPND - section boundary violated");
			evt->info.cpa.info.sec_data_rsp.num_of_elmts = -1;
			evt->info.cpa.info.sec_data_rsp.error_index =
			    i; /** Error index updated **/
			evt->info.cpa.info.sec_data_rsp.error =
			    SA_AIS_ERR_INVALID_PARAM;
			for (j = 0; j < i; j++) {
				if (ptr_read_data[j].data != NULL) {
					m_MMGR_FREE_CPND_DEFAULT(
					    ptr_read_data[j].data);
					ptr_read_data[j].data = NULL;
					ptr_read_data[j].read_size = 0;
				}
			}
			rc = NCSCC_RC_FAILURE;
			break;
		}

		/* If dataSize == 0 then it means that dataBuffer is NULL */
		if (data->dataSize == 0) {
			read_size = sec_info->sec_size - data->dataOffset;
		} else {
			if ((data->dataOffset + data->dataSize) >=
			    sec_info->sec_size)
				read_size =
				    sec_info->sec_size - data->dataOffset;
			else
				read_size = data->dataSize;
		}

		if (!read_size) {
			if (ptr_read_data[i].data != NULL)
				m_MMGR_FREE_CPND_DEFAULT(ptr_read_data[i].data);
			ptr_read_data[i].data = NULL;
			ptr_read_data[i].read_size = 0;
			continue;
		}

		ptr_read_data[i].data = m_MMGR_ALLOC_CPND_DEFAULT(read_size);
		if (ptr_read_data[i].data == NULL) {
			ptr_read_data[i].err = 1;
			continue;
		}
		rc =
		    cpnd_ckpt_sec_read(cp_node, sec_info, ptr_read_data[i].data,
				       read_size, data->dataOffset);
		if (rc == NCSCC_RC_FAILURE) {
			ptr_read_data[i].err = 1;
			m_MMGR_FREE_CPND_DEFAULT(ptr_read_data[i].data);
			ptr_read_data[i].data = NULL;
			continue;
		}
		ptr_read_data[i].read_size = read_size;
	}
	TRACE_LEAVE();
	return rc;
}

/****************************************************************************
 * Name          : cpnd_ckpt_generate_cpsv_ckpt_access_evt
 *
 * Description   : Function to process the
 *                 from Applications.
 *
 * Arguments     : CPND_CB *cb - CPND CB pointer
 *                 CPSV_EVT *evt - Received Event structure
 *                 CPSV_SEND_INFO *sinfo - Sender MDS information.
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 * Notes         : None.
 *****************************************************************************/
CPSV_CKPT_DATA *cpnd_ckpt_generate_cpsv_ckpt_access_evt(CPND_CKPT_NODE *cp_node)
{

	CPSV_CKPT_DATA *tmp_sec_data = NULL, *sec_data = NULL;
	CPND_CKPT_SECTION_INFO *tmp_sec_info = NULL;
	uint32_t i;

	tmp_sec_info = cpnd_ckpt_sec_get_first(&cp_node->replica_info);

	for (i = 0; i < cp_node->replica_info.n_secs; i++) {
		tmp_sec_data = m_MMGR_ALLOC_CPSV_CKPT_DATA;
		memset(tmp_sec_data, '\0', sizeof(CPSV_CKPT_DATA));

		tmp_sec_data->sec_id = tmp_sec_info->sec_id;
		tmp_sec_data->dataSize = tmp_sec_info->sec_size;

		tmp_sec_data->data =
		    m_MMGR_ALLOC_CPND_DEFAULT(tmp_sec_data->dataSize);

		cpnd_ckpt_sec_read(cp_node, tmp_sec_info, tmp_sec_data->data,
				   tmp_sec_data->dataSize, 0);

		tmp_sec_data->next = sec_data;
		sec_data = tmp_sec_data;

		tmp_sec_info = cpnd_ckpt_sec_get_next(&cp_node->replica_info,
						      tmp_sec_info);
	}

	return sec_data;
}

/****************************************************************************
 * Name          : cpnd_proc_free_cpsv_ckpt_data
 *
 * Description   : Function to free temporary ckpt data
 *
 * Arguments     : CPSV_CKPT_DATA *data - Pointer to temporary Ckpt Data.
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 * Notes         : None.
 *****************************************************************************/
void cpnd_proc_free_cpsv_ckpt_data(CPSV_CKPT_DATA *data)
{
	CPSV_CKPT_DATA *tmp_data = NULL;

	if (data == NULL) {
		return;
	}

	while (data != NULL) {

		tmp_data = data;
		data = data->next;
		if (tmp_data->data)
			m_MMGR_FREE_CPND_DEFAULT(tmp_data->data);
		m_MMGR_FREE_CPSV_CKPT_DATA(tmp_data);
	}
}

/****************************************************************************
 * Name          : cpnd_allrepl_write_evt_node_free
 *
 * Description   : Function to free ALLREPL write evt memory
 *
 * Arguments     : Pointer to ALLREPL event node.
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 * Notes         : None.
 *****************************************************************************/
uint32_t cpnd_allrepl_write_evt_node_free(CPSV_CPND_ALL_REPL_EVT_NODE *evt_node)
{
	CPSV_CPND_UPDATE_DEST *free_tmp = evt_node->cpnd_update_dest_list,
			      *tmp = NULL;

	while (free_tmp) {
		tmp = free_tmp->next;
		m_MMGR_FREE_CPND_UPDATE_DEST_INFO(free_tmp);
		free_tmp = tmp;
	}

	m_MMGR_FREE_CPND_ALL_REPL_EVT_NODE(evt_node);

	return NCSCC_RC_SUCCESS;
}

/****************************************************************************
 * Name          :
 *
 * Description   : Function to process the
 *                 from Applications.
 *
 * Arguments     : CPND_CB *cb - CPND CB pointer
 *                 CPSV_EVT *evt - Received Event structure
 *                 CPSV_SEND_INFO *sinfo - Sender MDS information.
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 * Notes         : None.
 *****************************************************************************/
void cpnd_proc_gen_mapping(CPND_CKPT_NODE *cp_node, CPSV_CKPT_ACCESS *ckpt_read,
			   CPSV_EVT *evt)
{

	CPND_CKPT_SECTION_INFO *sec_info = NULL;
	CPSV_CKPT_DATA *sec_data = NULL;
	int iter;
	uint32_t read_size = 0;

	TRACE_ENTER();
	evt->type = CPSV_EVT_TYPE_CPA;
	evt->info.cpa.type = CPA_EVT_ND2A_CKPT_DATA_RSP;
	evt->info.cpa.info.sec_data_rsp.type = CPSV_DATA_ACCESS_LCL_READ_RSP;
	evt->info.cpa.info.sec_data_rsp.num_of_elmts = ckpt_read->num_of_elmts;

	evt->info.cpa.info.sec_data_rsp.info.read_mapping =
	    (CPSV_ND2A_READ_MAP *)m_MMGR_ALLOC_CPND_DEFAULT(
		sizeof(CPSV_ND2A_READ_MAP) * ckpt_read->num_of_elmts);
	if (evt->info.cpa.info.sec_data_rsp.info.read_mapping == NULL) {
		TRACE_4("cpnd - cpsv nd2a read map alloc failed");
		evt->info.cpa.info.sec_data_rsp.num_of_elmts = -1;
		evt->info.cpa.info.sec_data_rsp.error = SA_AIS_ERR_NO_MEMORY;
		TRACE_LEAVE();
		return;
	}

	memset(evt->info.cpa.info.sec_data_rsp.info.read_mapping, '\0',
	       sizeof(CPSV_ND2A_READ_MAP) * ckpt_read->num_of_elmts);

	sec_data = ckpt_read->data;
	for (iter = 0; iter < ckpt_read->num_of_elmts;
	     iter++, sec_data = sec_data->next) {
		sec_info = cpnd_ckpt_sec_get(cp_node, &sec_data->sec_id);
		if (sec_info == NULL) {
			TRACE_4("cpnd ckpt sect get failed for ckpt_id:%llx",
				cp_node->ckpt_id);
			evt->info.cpa.info.sec_data_rsp.info.read_mapping[iter]
			    .offset_index = -1;
			continue;
		}

		if (((sec_data->dataOffset + sec_data->dataSize) >
		     (cp_node->create_attrib.maxSectionSize)) ||
		    (sec_data->dataOffset > sec_info->sec_size)) {
			TRACE_4("cpnd - section boundary violated");
			evt->info.cpa.info.sec_data_rsp.num_of_elmts = -1;
			evt->info.cpa.info.sec_data_rsp.error =
			    SA_AIS_ERR_INVALID_PARAM;
			TRACE_LEAVE();
			return;
		}

		/* If dataSize == 0 then that means dataBuffer is NULL */
		if (sec_data->dataSize == 0) {
			read_size = sec_info->sec_size - sec_data->dataOffset;
		} else {
			if ((sec_data->dataOffset + sec_data->dataSize) >=
			    sec_info->sec_size)
				read_size =
				    sec_info->sec_size - sec_data->dataOffset;
			else
				read_size = sec_data->dataSize;
		}

		evt->info.cpa.info.sec_data_rsp.info.read_mapping[iter]
		    .offset_index = sec_info->lcl_sec_id;
		evt->info.cpa.info.sec_data_rsp.info.read_mapping[iter]
		    .read_size = read_size;
	}
	TRACE_LEAVE();
}

/****************************************************************************
 * Name          :
 *
 * Description   : Function to process the
 *                 from Applications.
 *
 * Arguments     : CPND_CB *cb - CPND CB pointer
 *                 CPSV_EVT *evt - Received Event structure
 *                 CPSV_SEND_INFO *sinfo - Sender MDS information.
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 * Notes         : None.
 *****************************************************************************/
uint32_t cpnd_proc_update_remote(CPND_CB *cb, CPND_CKPT_NODE *cp_node,
				 CPND_EVT *in_evt, CPSV_EVT *out_evt,
				 CPSV_SEND_INFO *sinfo)
{
	CPSV_EVT send_evt;
	CPSV_CPND_ALL_REPL_EVT_NODE *all_repl_evt = NULL;
	CPSV_CPND_UPDATE_DEST *new = NULL, *head = NULL;
	uint32_t rc = NCSCC_RC_SUCCESS;
	SaTimeT timeout = 0;
	SaSizeT datasize = 0;

	TRACE_ENTER();
	memset(&send_evt, '\0', sizeof(CPSV_EVT));
	if (m_CPND_IS_ALL_REPLICA_ATTR_SET(
		cp_node->create_attrib.creationFlags) == true) {
		if (cp_node->cpnd_dest_list != NULL) {
			CPSV_CPND_DEST_INFO *tmp = NULL;

			tmp = cp_node->cpnd_dest_list;
			send_evt.type = CPSV_EVT_TYPE_CPND;
			send_evt.info.cpnd.type =
			    CPSV_EVT_ND2ND_CKPT_SECT_ACTIVE_DATA_ACCESS_REQ;

			{
				send_evt.info.cpnd.info.ckpt_nd2nd_data =
				    in_evt->info.ckpt_write;
				datasize =
				    in_evt->info.ckpt_write.num_of_elmts *
				    cp_node->create_attrib.maxSectionSize;
				timeout = CPND_WAIT_TIME(datasize);
			}
			/*Flag set to distinguish ALL_REPL case on response side
			 */
			send_evt.info.cpnd.info.ckpt_nd2nd_data
			    .all_repl_evt_flag = true;
			send_evt.info.cpnd.info.ckpt_nd2nd_data.agent_mdest =
			    sinfo->dest;
			/*Allocate memory to store the ALL REPL event node */
			all_repl_evt = m_MMGR_ALLOC_CPND_ALL_REPL_EVT_NODE;
			if (all_repl_evt) {

				memset(all_repl_evt, '\0',
				       sizeof(CPSV_CPND_ALL_REPL_EVT_NODE));

				/*Populate the Event node */
				all_repl_evt->ckpt_id = cp_node->ckpt_id;
				all_repl_evt->lcl_ckpt_id =
				    in_evt->info.ckpt_write.lcl_ckpt_id;
				all_repl_evt->sinfo = *sinfo;

				/*Copy the entire dest_list info of ckpt node to
				 * all_repl_evt dest_list */
				while (tmp != NULL) {
					new =
					    m_MMGR_ALLOC_CPND_UPDATE_DEST_INFO;
					if (!new) {
						rc = NCSCC_RC_FAILURE;
						goto mem_fail;
					}
					memset(new, 0,
					       sizeof(CPSV_CPND_UPDATE_DEST));
					new->dest = tmp->dest;
					if (head == NULL) {
						head = new;
					} else {
						new->next = head;
						head = new;
					}
					tmp = tmp->next;
				}
				all_repl_evt->cpnd_update_dest_list = head;

				if (cpnd_evt_node_add(cb, all_repl_evt) ==
				    NCSCC_RC_FAILURE) {
					/*log this error */
					LOG_ER(
					    "cpnd a multi event_add request for lcl_ckpt_id: %llx, evt is in progress for ckpt_id : %llx",
					    in_evt->info.ckpt_write.lcl_ckpt_id,
					    cp_node->ckpt_id);
				}

				/*Start the timer before send the async req to
				 * remote node */
				all_repl_evt->write_rsp_tmr.type =
				    CPND_ALL_REPL_RSP_EXPI;
				all_repl_evt->write_rsp_tmr.uarg =
				    cb->cpnd_cb_hdl_id;
				all_repl_evt->write_rsp_tmr.ckpt_id =
				    cp_node->ckpt_id;
				all_repl_evt->write_rsp_tmr.lcl_ckpt_hdl =
				    in_evt->info.ckpt_write.lcl_ckpt_id;
				all_repl_evt->write_rsp_tmr.agent_dest =
				    sinfo->dest;
				all_repl_evt->write_rsp_tmr.write_type =
				    in_evt->info.ckpt_write.type;
				rc = cpnd_tmr_start(
				    &all_repl_evt->write_rsp_tmr, timeout);

				while (head != NULL) {
					rc = cpnd_mds_msg_send(
					    cb, NCSMDS_SVC_ID_CPND, head->dest,
					    &send_evt);
					if (rc != NCSCC_RC_SUCCESS) {
						TRACE_4(
						    "CPND - MDS send failed from Active Dest to Remote Dest cpnd_mdest_id:%" PRIu64
						    ",\
						dest:%" PRIu64
						    ",ckpt_id:%llx:rc:%d",
						    cb->cpnd_mdest_id,
						    head->dest,
						    cp_node->ckpt_id, rc);
					} else {
						all_repl_evt->write_rsp_cnt++;
					}

					head = head->next;
				}
			} else {
				/*Log memory allocation failure */
			}
		}
	}

	else if ((m_CPND_IS_ACTIVE_REPLICA_ATTR_SET(
		      cp_node->create_attrib.creationFlags) == true) ||
		 (m_CPND_IS_ACTIVE_REPLICA_WEAK_ATTR_SET(
		      cp_node->create_attrib.creationFlags) == true)) {
		/* send rsp to agent */
		/* send to all other cpnd's using mds send */
		if (cp_node->cpnd_dest_list != NULL) {
			CPSV_CPND_DEST_INFO *tmp = NULL;
			tmp = cp_node->cpnd_dest_list;
			send_evt.type = CPSV_EVT_TYPE_CPND;
			send_evt.info.cpnd.type =
			    CPSV_EVT_ND2ND_CKPT_SECT_ACTIVE_DATA_ACCESS_REQ;
			send_evt.info.cpnd.info.ckpt_nd2nd_data =
			    in_evt->info.ckpt_write;
			while (tmp != NULL) {
				if (sinfo && m_CPND_IS_LOCAL_NODE(
						 tmp, &sinfo->dest) == 0) {
					tmp = tmp->next;
					continue;
				}
				rc = cpnd_mds_msg_send(cb, NCSMDS_SVC_ID_CPND,
						       tmp->dest, &send_evt);
				if (rc == NCSCC_RC_FAILURE) {
					if (rc == NCSCC_RC_REQ_TIMOUT) {
						TRACE_4(
						    "CPND - MDS send failed from Active Dest to Remote Dest cpnd_mdest_id:%" PRIu64
						    ",\
                                                dest:%" PRIu64
						    ",ckpt_id:%llx:rc:%d for replica attr set",
						    cb->cpnd_mdest_id,
						    tmp->dest, cp_node->ckpt_id,
						    rc);
					}

					/*  mds_failure= true;
					   goto mds_failure; */
				}
				tmp = tmp->next;
			}
		}
	}

	/* TBD IF received from Agent, fill the response to Agent */

	/* if ( (tmp_out_evt == NULL) || tmp_out_evt->type ==
	 * CPSV_EVT_ND2ND_CKPT_SECT_DATA_ACCESS_REQ) */
	{
		out_evt->type = CPSV_EVT_TYPE_CPA;
		out_evt->info.cpa.type = CPA_EVT_ND2A_CKPT_DATA_RSP;

		switch (in_evt->info.ckpt_write.type) {
		case CPSV_CKPT_ACCESS_WRITE:
			out_evt->info.cpa.info.sec_data_rsp.type =
			    CPSV_DATA_ACCESS_WRITE_RSP;
			out_evt->info.cpa.info.sec_data_rsp.error = SA_AIS_OK;
			break;

		case CPSV_CKPT_ACCESS_OVWRITE:
			out_evt->info.cpa.info.sec_data_rsp.type =
			    CPSV_DATA_ACCESS_OVWRITE_RSP;
			out_evt->info.cpa.info.sec_data_rsp.info.ovwrite_error
			    .error = SA_AIS_OK;
			break;
		}
	}

mem_fail:
	TRACE_LEAVE();
	return rc;
}

/****************************************************************************
 * Name          : cpnd_proc_rt_expiry
 * Description   : Function to process ckpt retentionDuration timer
 * Arguments     : CPND_CB *cb - CPND CB pointer
 *                 SaCkptCheckpointHandleT -- Ckpt handle
 * Return Values : NCSCC_RC_SUCCESS/Error.
 * Notes         : None.
 *****************************************************************************/
uint32_t cpnd_proc_rt_expiry(CPND_CB *cb, SaCkptCheckpointHandleT ckpt_id)
{
	uint32_t rc = NCSCC_RC_SUCCESS;
	CPND_CKPT_NODE *cp_node = NULL;
	SaAisErrorT error;

	cpnd_ckpt_node_get(cb, ckpt_id, &cp_node);

	if (cp_node == NULL) {
		TRACE_4("cpnd ckpt node get failed for ckpt_id:%llx", ckpt_id);
		return NCSCC_RC_FAILURE;
	}

	if (!m_CPND_IS_COLLOCATED_ATTR_SET(
		cp_node->create_attrib.creationFlags)) {

		if (cpnd_is_noncollocated_replica_present_on_payload(cb,
								     cp_node))
			return NCSCC_RC_SUCCESS;
	}
	rc = cpnd_ckpt_replica_destroy(cb, cp_node, &error);
	if (rc == NCSCC_RC_FAILURE) {
		TRACE_4("cpnd ckpt replica destry failed");
		return NCSCC_RC_FAILURE;
	}

	cpnd_restart_shm_ckpt_free(cb, cp_node);
	cpnd_ckpt_node_destroy(cb, cp_node);

	return NCSCC_RC_SUCCESS;
}

/****************************************************************************
 * Name          : cpnd_proc_non_colloc_rt_expiry
 * Description   : Function to process ckpt retentionDuration timer for
 *non-colloc Arguments     : CPND_CB *cb - CPND CB pointer
 *                 SaCkptCheckpointHandleT -- Ckpt handle
 * Return Values : NCSCC_RC_SUCCESS/Error.
 * Notes         : None.
 *****************************************************************************/
uint32_t cpnd_proc_non_colloc_rt_expiry(CPND_CB *cb,
					SaCkptCheckpointHandleT ckpt_id)
{
	uint32_t rc = NCSCC_RC_SUCCESS;
	CPND_CKPT_NODE *cp_node = NULL;
	SaAisErrorT error;

	cpnd_ckpt_node_get(cb, ckpt_id, &cp_node);

	if (cp_node == NULL) {
		TRACE_4("cpnd ckpt node get failed for ckpt_id:%llx", ckpt_id);
		return NCSCC_RC_FAILURE;
	}

	rc = cpnd_ckpt_replica_destroy(cb, cp_node, &error);
	if (rc == NCSCC_RC_FAILURE) {
		TRACE_4(
		    "cpnd ckpt replica destroy failed in non colloc for ckpt_id:%llx",
		    cp_node->ckpt_id);
		return NCSCC_RC_FAILURE;
	}

	cpnd_restart_shm_ckpt_free(cb, cp_node);
	cpnd_ckpt_node_destroy(cb, cp_node);

	return NCSCC_RC_SUCCESS;
}

/****************************************************************************
 * Name          : cpnd_proc_sec_expiry
 *
 * Description   : Function to process section expriry timer
 *
 * Arguments     : CPND_CB *cb - CPND CB pointer
 *                 CPND_TMR_INFO - Sec Tmr block
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 * Notes         : None.
 *****************************************************************************/
uint32_t cpnd_proc_sec_expiry(CPND_CB *cb, CPND_TMR_INFO *tmr_info)
{

	CPND_CKPT_NODE *cp_node = NULL;
	CPND_CKPT_SECTION_INFO *pSec_info = NULL;
	CPSV_EVT send_evt, *out_evt = NULL;
	uint32_t rc = NCSCC_RC_SUCCESS;

	memset(&send_evt, '\0', sizeof(CPSV_EVT));

	cpnd_ckpt_node_get(cb, tmr_info->ckpt_id, &cp_node);
	if (cp_node == NULL) {
		TRACE_4(
		    "cpnd ckpt node get failed in proc_sec_expiry ckpt_id:%llx",
		    tmr_info->ckpt_id);
		return NCSCC_RC_FAILURE;
	}

	pSec_info = cpnd_get_sect_with_id(cp_node, tmr_info->lcl_sec_id);
	if (pSec_info == NULL) {
		TRACE_4("cpnd - ckpt sect find failed , ckpt_id:%d",
			tmr_info->lcl_sec_id);
		return NCSCC_RC_FAILURE;
	}

	cpnd_ckpt_sec_del(cb, cp_node, &pSec_info->sec_id, true);
	cp_node->replica_info.shm_sec_mapping[pSec_info->lcl_sec_id] = 1;

	/* send out destory to all cpnd's maintaining this ckpt */
	if (cp_node->cpnd_dest_list != NULL) {

		CPSV_CPND_DEST_INFO *tmp = NULL;
		tmp = cp_node->cpnd_dest_list;
		while (tmp != NULL) {
			send_evt.type = CPSV_EVT_TYPE_CPND;
			send_evt.info.cpnd.type =
			    CPSV_EVT_ND2ND_CKPT_SECT_DELETE_REQ;
			send_evt.info.cpnd.info.sec_delete_req.ckpt_id =
			    tmr_info->ckpt_id;
			send_evt.info.cpnd.info.sec_delete_req.sec_id =
			    pSec_info->sec_id;

			rc = cpnd_mds_msg_sync_send(cb, NCSMDS_SVC_ID_CPND,
						    tmp->dest, &send_evt,
						    &out_evt, CPSV_WAIT_TIME);
			if (rc != NCSCC_RC_SUCCESS) {
				if (rc == NCSCC_RC_REQ_TIMOUT) {
					TRACE_4(
					    "cpnd - mds send failed from active dest to remote dest cpnd_mdest_id:%" PRIu64
					    ",dest:%" PRIu64 ",ckpt_id:%llx",
					    cb->cpnd_mdest_id, tmp->dest,
					    cp_node->ckpt_id);
				}
			}

			if (out_evt != NULL)
				cpnd_evt_destroy(out_evt);

			tmp = tmp->next;
			out_evt = NULL;
		}
	}
	m_CPND_FREE_CKPT_SECTION(pSec_info);
	return NCSCC_RC_SUCCESS;
}

/****************************************************************************
 * Name          : cpnd_all_repl_rsp_expiry
 *
 * Description   : Function to process ALLREPL write event expiry
 *
 * Arguments     : CPND_CB *cb - CPND CB pointer
 *                 CPND_TMR_INFO - Sec Tmr block
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 * Notes         : None.
 *****************************************************************************/
uint32_t cpnd_open_active_sync_expiry(CPND_CB *cb, CPND_TMR_INFO *tmr_info)
{
	CPSV_EVT des_evt, *out_evt = NULL;
	CPSV_EVT send_evt;
	memset(&des_evt, '\0', sizeof(CPSV_EVT));
	memset(&send_evt, '\0', sizeof(CPSV_EVT));
	des_evt.type = CPSV_EVT_TYPE_CPD;
	des_evt.info.cpd.type = CPD_EVT_ND2D_CKPT_DESTROY;
	des_evt.info.cpd.info.ckpt_destroy.ckpt_id = tmr_info->ckpt_id;
	(void)cpnd_mds_msg_sync_send(cb, NCSMDS_SVC_ID_CPD, cb->cpd_mdest_id,
				     &des_evt, &out_evt, CPSV_WAIT_TIME);
	if (out_evt && out_evt->info.cpnd.info.destroy_ack.error != SA_AIS_OK) {
		TRACE_4("cpnd cpd new active destroy failed with error:%d",
			out_evt->info.cpnd.info.destroy_ack.error);
	}
	if (out_evt)
		cpnd_evt_destroy(out_evt);
	send_evt.info.cpa.info.openRsp.error = SA_AIS_ERR_TIMEOUT;
	send_evt.type = CPSV_EVT_TYPE_CPA;
	send_evt.info.cpa.type = CPA_EVT_ND2A_CKPT_OPEN_RSP;
	send_evt.info.cpa.info.openRsp.lcl_ckpt_hdl = tmr_info->lcl_ckpt_hdl;
	if (tmr_info->sinfo.stype == MDS_SENDTYPE_SNDRSP) {
		(void)cpnd_mds_send_rsp(cb, &tmr_info->sinfo, &send_evt);
	} else {
		send_evt.info.cpa.info.openRsp.invocation =
		    tmr_info->invocation;
		(void)cpnd_mds_msg_send(cb, tmr_info->sinfo.to_svc,
					tmr_info->sinfo.dest, &send_evt);
	}
	return NCSCC_RC_SUCCESS;
}

uint32_t cpnd_all_repl_rsp_expiry(CPND_CB *cb, CPND_TMR_INFO *tmr_info)
{
	uint32_t rc = NCSCC_RC_SUCCESS;
	CPND_CKPT_NODE *cp_node = NULL;
	CPSV_CPND_ALL_REPL_EVT_NODE *evt_node = NULL;
	CPSV_EVT rsp_evt;

	TRACE_ENTER();
	cpnd_ckpt_node_get(cb, tmr_info->ckpt_id, &cp_node);
	cpnd_evt_node_get(cb, tmr_info->lcl_ckpt_hdl, &evt_node);

	memset(&rsp_evt, 0, sizeof(CPSV_EVT));

	rsp_evt.type = CPSV_EVT_TYPE_CPA;
	rsp_evt.info.cpa.type = CPA_EVT_ND2A_CKPT_DATA_RSP;

	if (cp_node && evt_node) {
		if (cp_node->ckpt_id == evt_node->ckpt_id) {
			switch (tmr_info->write_type) {
			case CPSV_CKPT_ACCESS_WRITE:
				rsp_evt.info.cpa.info.sec_data_rsp.type =
				    CPSV_DATA_ACCESS_WRITE_RSP;
				rsp_evt.info.cpa.info.sec_data_rsp.error =
				    SA_AIS_ERR_TIMEOUT;
				break;

			case CPSV_CKPT_ACCESS_OVWRITE:
				rsp_evt.info.cpa.info.sec_data_rsp.type =
				    CPSV_DATA_ACCESS_OVWRITE_RSP;
				rsp_evt.info.cpa.info.sec_data_rsp.info
				    .ovwrite_error.error = SA_AIS_ERR_TIMEOUT;
				break;
			}

			rc = cpnd_mds_send_rsp(cb, &evt_node->sinfo, &rsp_evt);
		}
	}

	if (evt_node) {
		/*Remove the all repl event node */
		cpnd_evt_node_del(cb, evt_node);

		/*Free the memory */
		cpnd_allrepl_write_evt_node_free(evt_node);
	}

	TRACE_LEAVE();
	return rc;
}

uint32_t cpnd_proc_fill_sec_desc(CPND_CKPT_SECTION_INFO *pTmpSecPtr,
				 SaCkptSectionDescriptorT *sec_des)
{

	sec_des->sectionId = pTmpSecPtr->sec_id;
	sec_des->expirationTime = pTmpSecPtr->exp_tmr;
	sec_des->sectionSize = pTmpSecPtr->sec_size;
	sec_des->sectionState = pTmpSecPtr->sec_state;
	sec_des->lastUpdate = pTmpSecPtr->lastUpdate; /* need to
							 update the section */
	return NCSCC_RC_SUCCESS;
}

uint32_t cpnd_proc_getnext_section(CPND_CKPT_NODE *cp_node,
				   CPSV_A2ND_SECT_ITER_GETNEXT *get_next,
				   SaCkptSectionDescriptorT *sec_des,
				   uint32_t *n_secs_trav)
{

	CPND_CKPT_SECTION_INFO *pSecPtr = NULL, *pTmpSecPtr = NULL;
	pSecPtr = cpnd_ckpt_sec_get_first(&cp_node->replica_info);

	TRACE_ENTER();
	if (cp_node->replica_info.n_secs == 0 ||
	    (cp_node->replica_info.n_secs <= get_next->n_secs_trav)) {
		TRACE_4("cpnd replica has no sections replica_info of nsecs %d",
			cp_node->replica_info.n_secs);
		TRACE_LEAVE();
		return NCSCC_RC_FAILURE;
	}

	if (get_next->n_secs_trav < cp_node->replica_info.n_secs) {
		if (get_next->section_id.idLen == 0 &&
		    cp_node->create_attrib.maxSections == 1) {
			if (get_next->filter == SA_CKPT_SECTIONS_ANY ||
			    get_next->filter == SA_CKPT_SECTIONS_FOREVER ||
			    get_next->filter ==
				SA_CKPT_SECTIONS_GEQ_EXPIRATION_TIME) {
				cpnd_proc_fill_sec_desc(pSecPtr, sec_des);
				*n_secs_trav = get_next->n_secs_trav + 1;
				TRACE_LEAVE();
				return NCSCC_RC_SUCCESS;
			} else /* for default section rem options results in no
				* section * need to think about corruption */
				TRACE_LEAVE();
			return NCSCC_RC_FAILURE;
		}

		/* search the existing section id */
		*n_secs_trav = get_next->n_secs_trav;
		if (pSecPtr != NULL && *n_secs_trav != 0) {
			pSecPtr =
			    cpnd_ckpt_sec_get(cp_node, &get_next->section_id);
		}
		/* if next is NULL then return no more sections */
		if (pSecPtr == NULL) {
			TRACE_4("cpnd replica has no sections");
			return NCSCC_RC_FAILURE;
		}

		/* get section descriptor with given filter */
		if (*n_secs_trav == 0)
			pTmpSecPtr = pSecPtr;
		else
			pTmpSecPtr = cpnd_ckpt_sec_get_next(
			    &cp_node->replica_info, pSecPtr);

		switch (get_next->filter) {
		case SA_CKPT_SECTIONS_ANY:
			if (pTmpSecPtr == NULL) {
				TRACE_4("cpnd replica has no sections ");
				TRACE_LEAVE();
				return NCSCC_RC_FAILURE;
			}
			(*n_secs_trav)++;
			break;

		case SA_CKPT_SECTIONS_CORRUPTED:
			TRACE_LEAVE();
			return NCSCC_RC_FAILURE;
			break;

		case SA_CKPT_SECTIONS_FOREVER:
			while (pTmpSecPtr != NULL) {
				if (pTmpSecPtr->exp_tmr == SA_TIME_END) {
					(*n_secs_trav)++;
					break;
				} else
					pTmpSecPtr = cpnd_ckpt_sec_get_next(
					    &cp_node->replica_info, pTmpSecPtr);
			}
			if (pTmpSecPtr == NULL) {
				TRACE_4("cpnd replica has no sections ");
				TRACE_LEAVE();
				return NCSCC_RC_FAILURE;
			}
			break;

		case SA_CKPT_SECTIONS_LEQ_EXPIRATION_TIME:
			while (pTmpSecPtr != NULL) {
				if (pTmpSecPtr->exp_tmr <= get_next->exp_tmr) {
					(*n_secs_trav)++;
					break;
				} else
					pTmpSecPtr = cpnd_ckpt_sec_get_next(
					    &cp_node->replica_info, pTmpSecPtr);
			}
			if (pTmpSecPtr == NULL) {
				TRACE_4(
				    "cpnd replica has no sections in lEQ expiration time");
				TRACE_LEAVE();
				return NCSCC_RC_FAILURE;
			}
			break;

		case SA_CKPT_SECTIONS_GEQ_EXPIRATION_TIME:
			while (pTmpSecPtr != NULL) {
				if (pTmpSecPtr->exp_tmr >= get_next->exp_tmr) {
					(*n_secs_trav)++;
					break;
				} else
					pTmpSecPtr = cpnd_ckpt_sec_get_next(
					    &cp_node->replica_info, pTmpSecPtr);
			}
			if (pTmpSecPtr == NULL) {
				TRACE_4(
				    "cpnd replica has no sections in GEQ expiration time");
				return NCSCC_RC_FAILURE;
			}
			break;

		default:
			return NCSCC_RC_FAILURE;
		}

		cpnd_proc_fill_sec_desc(pTmpSecPtr, sec_des);
		return NCSCC_RC_SUCCESS;
	}
	return NCSCC_RC_FAILURE;
}

/***************************************************************************
 * Name          : cpnd_ckpt_hdr_update
 *
 * Description   : To update the checkpoint header information
 *
 * Arguments     : CPND_CKPT_NODE - ckpt node
 *
 * Return Values : Success / Error
 ****************************************************************************/

uint32_t cpnd_ckpt_hdr_update(CPND_CB *cb, CPND_CKPT_NODE *cp_node)
{
	CPSV_CKPT_HDR ckpt_hdr;
	uint32_t rc = NCSCC_RC_SUCCESS;
	NCS_OS_POSIX_SHM_REQ_INFO write_req;

	memset(&write_req, '\0', sizeof(write_req));
	memset(&ckpt_hdr, '\0', sizeof(CPSV_CKPT_HDR));
	ckpt_hdr.ckpt_id = cp_node->ckpt_id;
	ckpt_hdr.create_attrib = cp_node->create_attrib;
	ckpt_hdr.open_flags = cp_node->open_flags;
	ckpt_hdr.is_unlink = cp_node->is_unlink;
	ckpt_hdr.is_close = cp_node->is_close;
	ckpt_hdr.n_secs = cp_node->replica_info.n_secs;
	ckpt_hdr.mem_used = cp_node->replica_info.mem_used;
	ckpt_hdr.cpnd_rep_create = cp_node->cpnd_rep_create;
	ckpt_hdr.is_active_exist = (bool)cp_node->is_active_exist;
	ckpt_hdr.active_mds_dest = cp_node->active_mds_dest;
	ckpt_hdr.cpnd_lcl_wr = cp_node->cur_state;
	ckpt_hdr.cpnd_oth_state = cp_node->oth_state;

	write_req.type = NCS_OS_POSIX_SHM_REQ_WRITE;
	write_req.info.write.i_addr =
	    cp_node->replica_info.open.info.open.o_addr;
	write_req.info.write.i_from_buff = (CPSV_CKPT_HDR *)&ckpt_hdr;
	write_req.info.write.i_offset = 0;
	write_req.info.write.i_write_size = sizeof(CPSV_CKPT_HDR);
	write_req.ensures_space = cb->shm_alloc_guaranteed != 0;
	rc = ncs_os_posix_shm(&write_req);

	return rc;
}

/**********************************************************************************
 * Name          :  cpnd_sec_hdr_update

 * Description   : To update the section header information

 * Arguments    : CPND_CKPT_SECTION_INFO - section info , CPND_CKPT_NODE - ckpt
node

 * Return Values : Success / Error
***********************************************************************************/

uint32_t cpnd_sec_hdr_update(CPND_CB *cb, CPND_CKPT_SECTION_INFO *sec_info,
			     CPND_CKPT_NODE *cp_node)
{
	CPSV_SECT_HDR sec_hdr;
	uint32_t rc = NCSCC_RC_SUCCESS;
	NCS_OS_POSIX_SHM_REQ_INFO write_req;
	memset(&write_req, '\0', sizeof(write_req));
	memset(&sec_hdr, '\0', sizeof(CPSV_SECT_HDR));
	sec_hdr.lcl_sec_id = sec_info->lcl_sec_id;
	sec_hdr.idLen = sec_info->sec_id.idLen;
	memcpy(sec_hdr.id, sec_info->sec_id.id, sec_info->sec_id.idLen);
	sec_hdr.sec_state = sec_info->sec_state;
	sec_hdr.sec_size = sec_info->sec_size;
	sec_hdr.exp_tmr = sec_info->exp_tmr;
	sec_hdr.lastUpdate = sec_info->lastUpdate;

	if ((sec_info->lcl_sec_id *
	     (sizeof(CPSV_SECT_HDR) + cp_node->create_attrib.maxSectionSize)) >
	    UINTMAX_MAX) {
		LOG_ER(
		    "cpnd Section hdr update failed exceeded the update limits(UINT64_MAX)");
		return NCSCC_RC_FAILURE;
	}
	write_req.type = NCS_OS_POSIX_SHM_REQ_WRITE;
	write_req.info.write.i_addr =
	    (void *)((char *)cp_node->replica_info.open.info.open.o_addr +
		     sizeof(CPSV_CKPT_HDR));
	write_req.info.write.i_from_buff = (CPSV_SECT_HDR *)&sec_hdr;
	write_req.info.write.i_offset =
	    sec_info->lcl_sec_id *
	    (sizeof(CPSV_SECT_HDR) + cp_node->create_attrib.maxSectionSize);
	write_req.info.write.i_write_size = sizeof(CPSV_SECT_HDR);
	write_req.ensures_space = cb->shm_alloc_guaranteed != 0;
	rc = ncs_os_posix_shm(&write_req);

	return rc;
}

/****************************************************************************
 * Name          : cpnd_cb_dump
 *
 * Description   : This is the function dumps the contents of the control block.
 ** Arguments     : cpnd_cb     -  Pointer to the control block
 *
 * Return Values : true/false
 *
 * Notes         : None.
 *****************************************************************************/
void cpnd_cb_dump(void)
{
	CPND_CB *cb = NULL;

	uint32_t cb_hdl = m_CPND_GET_CB_HDL;

	/* Get the CB from the handle */
	cb = ncshm_take_hdl(NCS_SERVICE_ID_CPND, cb_hdl);

	if (!cb) {
		TRACE_4("cpnd cb take handle failed");
		return;
	}

	CPND_CKPT_NODE *ckpt_node = NULL;
	CPND_CKPT_CLIENT_NODE *cl_node = NULL;

	TRACE("************ CPND CB Details *************** ");

	TRACE("**** Global Cb Details ***************");
	TRACE("CPND MDS dest - %d",
	      m_NCS_NODE_ID_FROM_MDS_DEST(cb->cpnd_mdest_id));
	if (cb->is_cpd_up) {
		TRACE("CPD is UP  ");
		TRACE("CPD MDS dest - %d",
		      m_NCS_NODE_ID_FROM_MDS_DEST(cb->cpd_mdest_id));
	} else
		TRACE("CPD is DOWN ");

	TRACE("*************************************************");
	TRACE("Number of Shared memory segments %d\n", cb->num_rep);

	TRACE("***** Start of Ckpt Details ***************");
	cpnd_ckpt_node_getnext(cb, 0, &ckpt_node);
	while (ckpt_node != NULL) {
		SaCkptCheckpointHandleT prev_ckpt_id;
		CPND_CKPT_CLLIST_NODE *ckpt_client_list = NULL;

		cpnd_dump_ckpt_info(ckpt_node);
		cpnd_dump_ckpt_attri(ckpt_node);
		cpnd_dump_replica_info(&ckpt_node->replica_info);

		ckpt_client_list = ckpt_node->clist;
		while (ckpt_client_list != NULL) {
			cpnd_dump_client_info(ckpt_client_list->cnode);
			ckpt_client_list = ckpt_client_list->next;
		}

		prev_ckpt_id = ckpt_node->ckpt_id;
		cpnd_ckpt_node_getnext(cb, prev_ckpt_id, &ckpt_node);
	}
	TRACE("***** End of Ckpt Details ***************");

	TRACE("***** Start of Client Details ***************");
	cpnd_client_node_getnext(cb, 0, &cl_node);
	while (cl_node != NULL) {
		SaCkptHandleT prev_cl_hdl;
		CPND_CKPT_CKPT_LIST_NODE *ckpt_list = NULL;

		cpnd_dump_client_info(cl_node);
		ckpt_list = cl_node->ckpt_list;
		while (ckpt_list != NULL) {
			cpnd_dump_ckpt_info(ckpt_list->cnode);
			ckpt_list = ckpt_list->next;
		}
		prev_cl_hdl = cl_node->ckpt_app_hdl;
		cpnd_client_node_getnext(cb, prev_cl_hdl, &cl_node);
	}
	TRACE("***** End of Client Details ***************");
}

/****************************************************************************
 * Name          : cpnd_dump_ckpt_attri
 *
 * Description   : This is the function dumps the ckpt attri
 * Arguments     : cp_node    -  Pointer to the Ckpt Node
 *
 * Return Values : true/false
 *
 * Notes         : None.
 *****************************************************************************/
static void cpnd_dump_ckpt_attri(CPND_CKPT_NODE *cp_node)
{

	TRACE("++++++++++++++++++++ATTRI+++++++++++++++++++++++++++");

	TRACE(" Creation Flags : ");
	switch (cp_node->create_attrib.creationFlags) {
	case SA_CKPT_WR_ALL_REPLICAS:
		TRACE("SA_CKPT_WR_ALL_REPLICAS");
		break;

	case SA_CKPT_WR_ACTIVE_REPLICA:
		TRACE("SA_CKPT_WR_ACTIVE_REPLICA");
		break;

	case SA_CKPT_WR_ACTIVE_REPLICA_WEAK:
		TRACE("SA_CKPT_WR_ACTIVE_REPLICA_WEAK");
		break;

	case SA_CKPT_CHECKPOINT_COLLOCATED:
		TRACE("SA_CKPT_CHECKPOINT_COLLOCATED");
		break;

	default:
		TRACE("\n Unkown creationFlags ..");
		break;
	}

	TRACE("Ckpt Size %d  retnetion time %d(scale of 100ms) ",
	      (uint32_t)(cp_node->create_attrib.checkpointSize),
	      (uint32_t)(cp_node->create_attrib.retentionDuration));
	TRACE("Ckpt Max Sections %d Ckpt Max Section Size %d",
	      (uint32_t)(cp_node->create_attrib.maxSections),
	      (uint32_t)(cp_node->create_attrib.maxSectionSize));
	TRACE("Ckpt Section Max Size %d ",
	      (uint32_t)(cp_node->create_attrib.maxSectionIdSize));
	TRACE("++++++++++++++++++++++++++++++++++++++++++++++++++");
}

/****************************************************************************
 * Name          : cpnd_dump_shm_info
 *
 * Description   : This is the function dumps the shared Memory Info
 * Arguments     : open    -  Pointer to the NCS_OS_POSIX_SHM_REQ_INFO
 *
 * Return Values : true/false
 *
 * Notes         : None.
 *****************************************************************************/
static void cpnd_dump_shm_info(NCS_OS_POSIX_SHM_REQ_INFO *open)
{

	TRACE("+++++++++++++++++SHM++++++++++++++++++++++++++++++");

	TRACE("Shm File Name %s ", open->info.open.i_name);
	TRACE("Shm Starting Address %p", open->info.open.o_addr);
	TRACE("Shm File Descriptor %d ", (uint32_t)open->info.open.o_fd);

	TRACE("++++++++++++++++++++++++++++++++++++++++++++++++++");
}

/****************************************************************************
 * Name          : cpnd_dump_section_info
 *
 * Description   : This is the function dumps the Ckpt Section info
 * Arguments     : sec_info    -  Pointer to the CPND_CKPT_SECTION_INFO
 *
 * Return Values : true/false
 *
 * Notes         : None.
 *****************************************************************************/
static void cpnd_dump_section_info(CPND_CKPT_SECTION_INFO *sec_info)
{
	int i;
	SaUint8T *buf;

	TRACE("++++++++++++++++++++SEC+++++++++++++++++++++++++++");
	if (sec_info != NULL) {
		TRACE("Shm Offset %d ", sec_info->lcl_sec_id);
		TRACE("Section Id ");
		i = sec_info->sec_id.idLen;
		buf = sec_info->sec_id.id;

		if (buf != NULL) {
			while (i >= 0) {
				TRACE("%c", buf[sec_info->sec_id.idLen - i]);
				i--;
			}
		}
		TRACE("Section Size %d  - Section state %d ",
		      (uint32_t)sec_info->sec_size,
		      (uint32_t)sec_info->sec_state);
		TRACE("Expiration Time %d ", (uint32_t)sec_info->exp_tmr);
		TRACE("++++++++++++++++++++++++++++++++++++++++++++++++++");
	}
}

/****************************************************************************
 * Name          : cpnd_dump_replica_info
 *
 * Description   : This is the function dumps the Ckpt Replica Info
 * Arguments     : ckp_replica_node   -  Pointer to the CPND_CKPT_REPLICA_INFO
 *
 * Return Values : true/false
 *
 * Notes         : None.
 *****************************************************************************/
static void cpnd_dump_replica_info(CPND_CKPT_REPLICA_INFO *ckpt_replica_node)
{
	cpnd_dump_shm_info(&ckpt_replica_node->open);

	if (ckpt_replica_node->n_secs) {
		CPND_CKPT_SECTION_INFO *sec_info = NULL;
		sec_info = cpnd_ckpt_sec_get_first(ckpt_replica_node);
		while (sec_info != NULL) {
			cpnd_dump_section_info(sec_info);
			sec_info =
			    cpnd_ckpt_sec_get_next(ckpt_replica_node, sec_info);
		}
	}
}

/****************************************************************************
 * Name          : cpnd_dump_client_info
 *
 * Description   : This is the function dumps the Ckpt Client info
 * Arguments     : cl_node  -  Pointer to the CPND_CKPT_CLIENT_NODE
 *
 * Return Values : true/false
 *
 * Notes         : None.
 *****************************************************************************/
static void cpnd_dump_client_info(CPND_CKPT_CLIENT_NODE *cl_node)
{
	TRACE("++++++++++++++++++++++++++++++++++++++++++++++++++\n");

	TRACE("Client_hdl  %d MDS DEST %d ", (uint32_t)(cl_node->ckpt_app_hdl),
	      (uint32_t)m_NCS_NODE_ID_FROM_MDS_DEST(cl_node->agent_mds_dest));
	TRACE("++++++++++++++++++++++++++++++++++++++++++++++++++");
}

static void cpnd_dump_ckpt_info(CPND_CKPT_NODE *ckpt_node)
{

	CPSV_CPND_DEST_INFO *cpnd_dest_list = NULL;

	TRACE("++++++++++++++++++++++++++++++++++++++++++++++++++");
	TRACE("Ckpt_id - %d  Ckpt Name - %.10s ", (uint32_t)ckpt_node->ckpt_id,
	      ckpt_node->ckpt_name);
	if (ckpt_node->is_unlink)
		TRACE("Ckpt Unlinked - ");
	if (ckpt_node->is_close)
		TRACE("Ckpt Closed ");
	TRACE("ref count - %d", ckpt_node->ckpt_lcl_ref_cnt);
	TRACE("ACTIVE CKPT NODE MDS dest - %d",
	      m_NCS_NODE_ID_FROM_MDS_DEST(ckpt_node->active_mds_dest));

	TRACE("CPND List ......");

	cpnd_dest_list = ckpt_node->cpnd_dest_list;
	while (cpnd_dest_list != NULL) {
		TRACE("--%d-> ",
		      m_NCS_NODE_ID_FROM_MDS_DEST(cpnd_dest_list->dest));
		cpnd_dest_list = cpnd_dest_list->next;
	}
	TRACE("++++++++++++++++++++++++++++++++++++++++++++++++++");
}

/****************************************************************************
 * Name          : cpnd_match_dest
 *
 * Description   : This is the function dumps the Ckpt Replica Info
 * Arguments     : key    - mds_dest of cpnd which has done a sync send to this
 *cpnd qelem  - item in the cb->cpnd_sync_send_list Return Values : true/false
 *
 * Notes         : None.
 *****************************************************************************/
bool cpnd_match_dest(void *key, void *qelem)
{
	CPND_SYNC_SEND_NODE *node = (CPND_SYNC_SEND_NODE *)qelem;
	MDS_DEST *dest = (MDS_DEST *)key;

	if (!qelem)
		return false;

	if (*dest == node->dest)
		return true;

	return false;
}

/****************************************************************************
 * Name          : cpnd_match_evt
 *
 * Description   : This is the function dumps the Ckpt Replica Info
 * Arguments     : key    - CPSV_EVT* sync send request to this cpnd
 *                 qelem  - item in the cb->cpnd_sync_send_list
 * Return Values : true/false
 *
 * Notes         : None.
 *****************************************************************************/
bool cpnd_match_evt(void *key, void *qelem)
{
	CPND_SYNC_SEND_NODE *node = (CPND_SYNC_SEND_NODE *)qelem;
	CPSV_EVT *evt = (CPSV_EVT *)key;

	if (!qelem)
		return false;

	if (evt == node->evt)
		return true;

	return false;
}

/****************************************************************************
 * Name          : cpnd_is_noncollocated_replica_present_on_payload
 *
 * Description   : This is the function dumps the Ckpt Replica Info
 * Arguments     : cb       - CPND Control Block pointer
 *                 cp_node  - pointer to checkpoint node
 *
 * Return Values : true/false
 *****************************************************************************/
bool cpnd_is_noncollocated_replica_present_on_payload(CPND_CB *cb,
						      CPND_CKPT_NODE *cp_node)
{
	CPSV_CPND_DEST_INFO *dest_list = NULL;

	TRACE_ENTER();
	/* Check if the CPND is on Active or Standby SCXB */
	if (m_CPND_IS_ON_SCXB(
		cb->cpnd_active_id,
		cpnd_get_node_id_from_mds_dest(cb->cpnd_mdest_id)) ||
	    m_CPND_IS_ON_SCXB(
		cb->cpnd_standby_id,
		cpnd_get_node_id_from_mds_dest(cb->cpnd_mdest_id))) {

		dest_list = cp_node->cpnd_dest_list;

		while (dest_list) {
			/* Check if a replica is present on one of the payload
			 * blades */
			if ((!m_CPND_IS_ON_SCXB(
				cb->cpnd_active_id,
				cpnd_get_node_id_from_mds_dest(
				    dest_list->dest))) &&
			    (!m_CPND_IS_ON_SCXB(
				cb->cpnd_standby_id,
				cpnd_get_node_id_from_mds_dest(
				    dest_list->dest)))) {
				TRACE_LEAVE();
				return true;
			}

			dest_list = dest_list->next;
		}
	}

	TRACE_LEAVE();
	return false;
}

/****************************************************************************************
 * Name          : cpnd_send_ckpt_usr_info_to_cpd
 *
 * Description   : This is the function dumps the Ckpt Replica Info
 * Arguments     : cb       - CPND Control Block pointer
 *                 cp_node  - pointer to checkpoint node
 *                 ckpt_open_flags - checkpoint open flags
 *                 usrinfoType - CPSV_USR_INFO_CKPT_OPEN /
 *CPSV_USR_INFO_CKPT_CLOSE
 *
 * Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE
 *****************************************************************************************/
uint32_t
cpnd_send_ckpt_usr_info_to_cpd(CPND_CB *cb, CPND_CKPT_NODE *cp_node,
			       SaCkptCheckpointOpenFlagsT ckpt_open_flags,
			       CPSV_USR_INFO_CKPT_TYPE usrinfoType)
{
	CPSV_EVT send_evt;
	uint32_t rc;

	TRACE_ENTER();
	memset(&send_evt, '\0', sizeof(CPSV_EVT));

	send_evt.type = CPSV_EVT_TYPE_CPD;
	send_evt.info.cpd.type = CPD_EVT_ND2D_CKPT_USR_INFO;
	send_evt.info.cpd.info.ckpt_usr_info.ckpt_id = cp_node->ckpt_id;
	if (usrinfoType == CPSV_USR_INFO_CKPT_OPEN) {
		if (cp_node->ckpt_lcl_ref_cnt == 1) {
			usrinfoType = CPSV_USR_INFO_CKPT_OPEN_FIRST;
		}
	}
	if (usrinfoType == CPSV_USR_INFO_CKPT_CLOSE) {
		if (cp_node->ckpt_lcl_ref_cnt == 0) {
			usrinfoType = CPSV_USR_INFO_CKPT_CLOSE_LAST;
		}
	}
	send_evt.info.cpd.info.ckpt_usr_info.info_type = usrinfoType;
	send_evt.info.cpd.info.ckpt_usr_info.ckpt_flags = ckpt_open_flags;
	rc = cpnd_mds_msg_send(cb, NCSMDS_SVC_ID_CPD, cb->cpd_mdest_id,
			       &send_evt);

	if (rc != NCSCC_RC_SUCCESS) {
		TRACE_LEAVE();
		return NCSCC_RC_FAILURE;
	}
	TRACE_LEAVE();
	return NCSCC_RC_SUCCESS;
}

/****************************************************************************************
 * Name          : cpnd_ckpt_replica_close
 *
 * Description   : This is the function dumps the Ckpt Replica Info
 * Arguments     : cb       - CPND Control Block pointer
 *                 cp_node  - pointer to checkpoint node
 *
 * Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE
 *****************************************************************************************/

uint32_t cpnd_ckpt_replica_close(CPND_CB *cb, CPND_CKPT_NODE *cp_node,
				 SaAisErrorT *error)
{
	SaTimeT presentTime;
	uint32_t rc = NCSCC_RC_SUCCESS;

	TRACE_ENTER();
	if (cp_node->ckpt_lcl_ref_cnt == 0) {

		cp_node->is_close = true;
		cpnd_restart_set_close_flag(cb, cp_node);

		if (cp_node->is_unlink != true &&
		    (m_CPSV_CONVERT_SATIME_TEN_MILLI_SEC(
			 cp_node->create_attrib.retentionDuration) != 0)) {
			m_GET_TIME_STAMP(presentTime);
			cpnd_restart_update_timer(cb, cp_node, presentTime);

			cp_node->ret_tmr.type = CPND_TMR_TYPE_RETENTION;
			cp_node->ret_tmr.uarg = cb->cpnd_cb_hdl_id;
			cp_node->ret_tmr.ckpt_id = cp_node->ckpt_id;
			cpnd_tmr_start(
			    &cp_node->ret_tmr,
			    m_CPSV_CONVERT_SATIME_TEN_MILLI_SEC(
				cp_node->create_attrib.retentionDuration));

			TRACE_1(
			    "CPND - Checkpoint retention tmr started , ckpt_id:%llx",
			    cp_node->ckpt_id);
		} else {
			/* Check for Non-Collocated Replica */
			if (!m_CPND_IS_COLLOCATED_ATTR_SET(
				cp_node->create_attrib.creationFlags)) {
				if (cpnd_is_noncollocated_replica_present_on_payload(
					cb, cp_node)) {
					TRACE_LEAVE();
					return NCSCC_RC_SUCCESS;
				}
			}
			rc = cpnd_ckpt_replica_destroy(cb, cp_node, error);
			if (rc == NCSCC_RC_FAILURE) {
				TRACE_4(
				    "cpnd ckpt replica destroy failed ckpt_id:%llx",
				    cp_node->ckpt_id);
				TRACE_LEAVE();
				return NCSCC_RC_FAILURE;
			}
			TRACE_1(
			    "cpnd ckpt replica destroy success ckpt_id:%llx",
			    cp_node->ckpt_id);

			cpnd_restart_shm_ckpt_free(cb, cp_node);
			cpnd_ckpt_node_destroy(cb, cp_node);
		}
	}
	TRACE_LEAVE();
	return NCSCC_RC_SUCCESS;
}

/****************************************************************************************
 * Name          : cpnd_proc_rdset_start
 *
 * Description   : This is the function process the event CPSV_CKPT_RDSET_START
 *                 This event is only applicable for non-collocated checkpoint
 * Arguments     : cb       - CPND Control Block pointer
 *                 cp_node  - pointer to checkpoint node
 *
 * Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE
 *****************************************************************************************/

uint32_t cpnd_proc_rdset_start(CPND_CB *cb, CPND_CKPT_NODE *cp_node)
{
	SaTimeT presentTime;
	SaAisErrorT error = SA_AIS_OK;
	uint32_t rc = NCSCC_RC_SUCCESS;

	TRACE_ENTER();

	if (m_CPND_IS_COLLOCATED_ATTR_SET(
		cp_node->create_attrib.creationFlags)) {
		TRACE_LEAVE();
		return NCSCC_RC_SUCCESS;
	}

	if (cp_node->ckpt_lcl_ref_cnt != 0) {
		/*  Ticket #1786
		 *  The log message also happens in case interval between
		 * previous saCkptCheckpointClose() and saCkptCheckpointOpen()
		 * is too short. In this case, the CPD processes the
		 * CPND_EVT_A2ND_CKPT_OPEN (for saCkptCheckpointOpen()) before
		 * the CPND_EVT_D2ND_RDSET_INFO. Then when CPND processes the
		 * CPND_EVT_D2ND_RDSET_INFO, it logs this message although it
		 * is not an error in this case.*/
		LOG_NO(
		    "cpnd receives CPND_EVT_D2ND_RDSET_INFO with START while ckpt_lcl_ref_cnt = %d",
		    cp_node->ckpt_lcl_ref_cnt);
		TRACE_LEAVE();
		return NCSCC_RC_FAILURE;
	}

	cp_node->is_close = true;
	cpnd_restart_set_close_flag(cb, cp_node);

	if (cp_node->is_unlink != true &&
	    (m_CPSV_CONVERT_SATIME_TEN_MILLI_SEC(
		 cp_node->create_attrib.retentionDuration) != 0)) {
		m_GET_TIME_STAMP(presentTime);
		cpnd_restart_update_timer(cb, cp_node, presentTime);

		cp_node->ret_tmr.type = CPND_TMR_TYPE_NON_COLLOC_RETENTION;
		cp_node->ret_tmr.uarg = cb->cpnd_cb_hdl_id;
		cp_node->ret_tmr.ckpt_id = cp_node->ckpt_id;
		cpnd_tmr_start(&cp_node->ret_tmr,
			       m_CPSV_CONVERT_SATIME_TEN_MILLI_SEC(
				   cp_node->create_attrib.retentionDuration));
		TRACE_1("cpnd ckpt ret tmr success ckpt_id:%llx",
			cp_node->ckpt_id);
	} else {
		rc = cpnd_ckpt_replica_destroy(cb, cp_node, &error);
		if (rc == NCSCC_RC_FAILURE) {
			LOG_ER(
			    "cpnd ckpt replica destroy failed ckpt_id:%llx, error:%d",
			    cp_node->ckpt_id, error);
			return NCSCC_RC_FAILURE;
		}
		TRACE_1("cpnd ckpt replica destroy success ckpt_id:%llx",
			cp_node->ckpt_id);

		cpnd_restart_shm_ckpt_free(cb, cp_node);
		cpnd_ckpt_node_destroy(cb, cp_node);
	}

	TRACE_LEAVE();
	return NCSCC_RC_SUCCESS;
}

void cpnd_proc_free_read_data(CPSV_EVT *evt)
{
	uint32_t iter = 0;
	CPSV_ND2A_READ_DATA *ptr_read_data;
	ptr_read_data = evt->info.cpa.info.sec_data_rsp.info.read_data;

	if (evt->info.cpa.info.sec_data_rsp.info.read_data) {
		for (; iter < evt->info.cpa.info.sec_data_rsp.size; iter++) {
			if (ptr_read_data[iter].data != NULL) {
				m_MMGR_FREE_CPND_DEFAULT(
				    ptr_read_data[iter].data);
			}
		}
		m_MMGR_FREE_CPND_DEFAULT(
		    evt->info.cpa.info.sec_data_rsp.info.read_data);
	}
}

/****************************************************************************************
 * Name          : cpnd_ckpt_sc_cpnd_mdest_del
 *
 * Description   : This is the function delete sc cpnd mds dests on all ckpt
 *node Arguments     : cb       - CPND Control Block pointer
 *
 * Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE
 *****************************************************************************************/
void cpnd_ckpt_sc_cpnd_mdest_del(CPND_CB *cb)
{
	CPND_CKPT_NODE *ckpt_node = NULL;

	TRACE_ENTER();
	cpnd_ckpt_node_getnext(cb, 0, &ckpt_node);
	while (ckpt_node != NULL) {
		CPSV_CPND_DEST_INFO *dest_list = NULL;

		dest_list = ckpt_node->cpnd_dest_list;

		/* Delete SC cpnd mdests in the cpnd_dest_list */
		while (dest_list) {
			if ((m_CPND_IS_ON_SCXB(
				cb->cpnd_active_id,
				cpnd_get_node_id_from_mds_dest(
				    dest_list->dest))) ||
			    (m_CPND_IS_ON_SCXB(
				cb->cpnd_standby_id,
				cpnd_get_node_id_from_mds_dest(
				    dest_list->dest))))
				cpnd_ckpt_remote_cpnd_del(ckpt_node,
							  dest_list->dest);

			dest_list = dest_list->next;
		}

		SaCkptCheckpointHandleT prev_ckpt_id;
		prev_ckpt_id = ckpt_node->ckpt_id;
		cpnd_ckpt_node_getnext(cb, prev_ckpt_id, &ckpt_node);
	}
	TRACE_LEAVE();
}

/****************************************************************************************
 * Name          : cpnd_headless_ckpt_node_del
 *
 * Description   : This is the function delete following type of ckpt node:
 *                 - ckpt node was unlinked
 *                 - non-collocated ckpt node which has active replica located
 *on SC
 *
 * Arguments     : cb       - CPND Control Block pointer
 *
 * Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE
 *****************************************************************************************/
void cpnd_headless_ckpt_node_del(CPND_CB *cb)
{
	CPND_CKPT_NODE *ckpt_node = NULL;
	CPSV_EVT send_evt;

	TRACE_ENTER();
	cpnd_ckpt_node_getnext(cb, 0, &ckpt_node);
	while (ckpt_node != NULL) {
		SaCkptCheckpointHandleT prev_ckpt_id = ckpt_node->ckpt_id;
		MDS_DEST active_mds_dest = ckpt_node->active_mds_dest;
		bool destroy = false;

		/* Unlink checkpoint */
		if (ckpt_node->is_unlink == true)
			destroy = true;

		/* Non collocated checkpoint with active replica located on SC
		 */
		if (!m_CPND_IS_COLLOCATED_ATTR_SET(
			ckpt_node->create_attrib.creationFlags)) {
			if ((m_CPND_IS_ON_SCXB(
				cb->cpnd_active_id,
				cpnd_get_node_id_from_mds_dest(
				    active_mds_dest))) ||
			    (m_CPND_IS_ON_SCXB(
				cb->cpnd_standby_id,
				cpnd_get_node_id_from_mds_dest(
				    active_mds_dest))))
				destroy = true;
		}

		if (destroy == true) {
			/* Delete ckpt_node and replica */
			cpnd_ckpt_replica_delete(cb, ckpt_node);
			cpnd_restart_shm_ckpt_free(cb, ckpt_node);
			cpnd_ckpt_node_destroy(cb, ckpt_node);

			/* Broadcase CPA_EVT_ND2A_CKPT_DESTROY to CPA */
			memset(&send_evt, 0, sizeof(CPSV_EVT));
			send_evt.type = CPSV_EVT_TYPE_CPA;
			send_evt.info.cpa.type = CPA_EVT_ND2A_CKPT_DESTROY;
			send_evt.info.cpa.info.ckpt_destroy.ckpt_id =
			    prev_ckpt_id;
			cpnd_mds_bcast_send(cb, &send_evt, NCSMDS_SVC_ID_CPA);

			LOG_IN(
			    "cpnd ckpt_node and replica destroy successful - cpkt_id - %llu",
			    prev_ckpt_id);
		}

		cpnd_ckpt_node_getnext(cb, prev_ckpt_id, &ckpt_node);
	}
	TRACE_LEAVE();
}

/****************************************************************************************
 * Name          : cpnd_proc_ckpt_info_update
 *
 * Description   : This is the function update cpd about ckpt info after
 *headless
 *
 * Arguments     : cb       - CPND Control Block pointer
 *
 * Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE
 *****************************************************************************************/
void cpnd_proc_ckpt_info_update(CPND_CB *cb)
{
	uint32_t rc = NCSCC_RC_SUCCESS;
	CPND_CKPT_NODE *ckpt_node = NULL;
	CPSV_EVT send_evt;
	SaVersionT client_version = {'B', 0x02, 0x02};
	unsigned int remaining_node = cb->ckpt_info_db.n_nodes;

	TRACE_ENTER();
	memset(&send_evt, '\0', sizeof(CPSV_EVT));

	cpnd_ckpt_node_getnext(cb, 0, &ckpt_node);
	while (ckpt_node != NULL) {
		SaCkptCheckpointHandleT prev_ckpt_id = ckpt_node->ckpt_id;
		CPSV_EVT *out_evt = NULL;
		bool update_success = false;
		remaining_node--;

		/* send info to cpd */
		LOG_NO("cpnd_proc_update_cpd_data::ckpt_name = %s[%llu]",
		       (char *)ckpt_node->ckpt_name, ckpt_node->ckpt_id);
		send_evt.type = CPSV_EVT_TYPE_CPD;
		send_evt.info.cpd.info.ckpt_info.ckpt_id = ckpt_node->ckpt_id;
		osaf_extended_name_lend(
		    ckpt_node->ckpt_name,
		    &send_evt.info.cpd.info.ckpt_info.ckpt_name);
		send_evt.info.cpd.type = CPD_EVT_ND2D_CKPT_INFO_UPDATE;
		send_evt.info.cpd.info.ckpt_info.attributes =
		    ckpt_node->create_attrib;
		send_evt.info.cpd.info.ckpt_info.ckpt_flags =
		    ckpt_node->open_flags;
		send_evt.info.cpd.info.ckpt_info.num_users =
		    ckpt_node->ckpt_lcl_ref_cnt;
		send_evt.info.cpd.info.ckpt_info.num_readers = 0;
		send_evt.info.cpd.info.ckpt_info.num_writers = 0;
		send_evt.info.cpd.info.ckpt_info.client_version =
		    client_version;
		send_evt.info.cpd.info.ckpt_info.is_unlink =
		    ckpt_node->is_unlink;

		if (ckpt_node->is_active_exist &&
		    m_NCS_MDS_DEST_EQUAL(&ckpt_node->active_mds_dest,
					 &cb->cpnd_mdest_id)) {
			send_evt.info.cpd.info.ckpt_info.is_active = true;
		} else
			send_evt.info.cpd.info.ckpt_info.is_active = false;

		if (remaining_node == 0)
			send_evt.info.cpd.info.ckpt_info.is_last = true;
		else
			send_evt.info.cpd.info.ckpt_info.is_last = false;

		LOG_NO(
		    "cpnd_proc_update_cpd_data::send CPD_EVT_ND2D_CKPT_INFO_UPDATE");
		rc = cpnd_mds_msg_sync_send(cb, NCSMDS_SVC_ID_CPD,
					    cb->cpd_mdest_id, &send_evt,
					    &out_evt, CPSV_WAIT_TIME);

		if (rc != NCSCC_RC_SUCCESS) {
			LOG_ER(
			    "cpnd_proc_update_cpd_data::fail to send CPD_EVT_ND2D_CKPT_INFO_UPDATE");
		}

		if (out_evt == NULL) {
			LOG_ER(
			    "cpnd_proc_update_cpd_data:: cpnd ckpt memory alloc failed");
		} else {
			switch (out_evt->info.cpnd.type) {

			case CPND_EVT_D2ND_CKPT_INFO_UPDATE_ACK:
				if (out_evt->info.cpnd.info.ckpt_info_update_ack
					.error == SA_AIS_OK) {
					LOG_NO(
					    "cpnd_proc_update_cpd_data::CPND_EVT_D2ND_CKPT_INFO_UPDATE_ACK received");
					update_success = true;
				} else {
					LOG_ER(
					    "cpnd_proc_update_cpd_data::Fail to update CPD after headless rc=[%d]",
					    out_evt->info.cpnd.info
						.ckpt_info_update_ack.error);
				}
				break;
			default:
				LOG_ER(
				    "cpnd_proc_update_cpd_data:: cpnd evt unknown type :%d",
				    out_evt->info.cpnd.type);
				break;
			}

			cpnd_evt_destroy(out_evt);
		}

		/* Update ckpt fail destroy the ckpt_node and replica */
		if (update_success == false) {
			/* Delete ckpt_node and replica */
			cpnd_ckpt_replica_delete(cb, ckpt_node);
			cpnd_restart_shm_ckpt_free(cb, ckpt_node);
			cpnd_ckpt_node_destroy(cb, ckpt_node);

			/* Broadcase CPA_EVT_ND2A_CKPT_DESTROY to CPA */
			memset(&send_evt, 0, sizeof(CPSV_EVT));
			send_evt.type = CPSV_EVT_TYPE_CPA;
			send_evt.info.cpa.type = CPA_EVT_ND2A_CKPT_DESTROY;
			send_evt.info.cpa.info.ckpt_destroy.ckpt_id =
			    ckpt_node->ckpt_id;
			cpnd_mds_bcast_send(cb, &send_evt, NCSMDS_SVC_ID_CPA);
		}

		/* The SC is UP again and the checkpoint was recovered
		 * successfull. Re-initialize ckpt_node flags */
		ckpt_node->is_restart = false;

		/* end of loop */
		cpnd_ckpt_node_getnext(cb, prev_ckpt_id, &ckpt_node);
	}
}

/****************************************************************************************
 * Name          : cpnd_ckpt_replica_delete
 *
 * Description   : This is the function delete replica without sending destroy
 *signal to CPD
 *
 * Arguments     : cb       - CPND Control Block pointer
 *
 * Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE
 *****************************************************************************************/
void cpnd_ckpt_replica_delete(CPND_CB *cb, CPND_CKPT_NODE *ckpt_node)
{
	NCS_OS_POSIX_SHM_REQ_INFO shm_info;
	uint32_t rc = NCSCC_RC_SUCCESS;

	TRACE_ENTER();
	if (ckpt_node->cpnd_rep_create) {

		/* First delete all sections in the heckpoint about to be
		 * deleted */
		cpnd_ckpt_delete_all_sect(ckpt_node);

		memset(&shm_info, '\0', sizeof(shm_info));

		shm_info.type = NCS_OS_POSIX_SHM_REQ_CLOSE;
		shm_info.info.close.i_addr =
		    ckpt_node->replica_info.open.info.open.o_addr;
		shm_info.info.close.i_fd =
		    ckpt_node->replica_info.open.info.open.o_fd;
		shm_info.info.close.i_hdl =
		    ckpt_node->replica_info.open.info.open.o_hdl;
		shm_info.info.close.i_size =
		    ckpt_node->replica_info.open.info.open.i_size;

		rc = ncs_os_posix_shm(&shm_info);

		if (rc == NCSCC_RC_FAILURE) {
			LOG_ER("cpnd ckpt close failed for ckpt_id:%llx",
			       ckpt_node->ckpt_id);
			TRACE_LEAVE();
			return;
		}

		/* unlink the name */
		shm_info.type = NCS_OS_POSIX_SHM_REQ_UNLINK;
		shm_info.info.unlink.i_name =
		    ckpt_node->replica_info.open.info.open.i_name;

		rc = ncs_os_posix_shm(&shm_info);

		if (rc == NCSCC_RC_FAILURE) {
			LOG_ER("cpnd ckpt unlink failed ckpt_id:%llx",
			       ckpt_node->ckpt_id);
			TRACE_LEAVE();
			return;
		}

		if (cb->num_rep)
			cb->num_rep--;

		m_MMGR_FREE_CPND_DEFAULT(
		    ckpt_node->replica_info.open.info.open.i_name);

		/* freeing the sec_mapping memory */
		if (ckpt_node->replica_info.shm_sec_mapping)
			m_MMGR_FREE_CPND_DEFAULT(
			    ckpt_node->replica_info.shm_sec_mapping);
	}
	TRACE_LEAVE();
}

/****************************************************************************************
 * Name          : cpnd_proc_active_down_ckpt_node_del
 *
 * Description   : This is the function delete non-collocated ckpt node which
 *has active replica node is down
 *
 * Arguments     : cb       - CPND Control Block pointer
 *                 mds_dest - Active replica cpnd mds_dest
 *
 * Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE
 *****************************************************************************************/
void cpnd_proc_active_down_ckpt_node_del(CPND_CB *cb, MDS_DEST mds_dest)
{
	CPND_CKPT_NODE *ckpt_node = NULL;
	CPSV_EVT send_evt;

	TRACE_ENTER();
	cpnd_ckpt_node_getnext(cb, 0, &ckpt_node);
	while (ckpt_node != NULL) {
		SaCkptCheckpointHandleT prev_ckpt_id = ckpt_node->ckpt_id;
		MDS_DEST active_mds_dest = ckpt_node->active_mds_dest;

		if (!m_CPND_IS_COLLOCATED_ATTR_SET(
			ckpt_node->create_attrib.creationFlags) &&
		    (active_mds_dest == mds_dest)) {

			/* Delete ckpt_node and replica */
			cpnd_ckpt_replica_delete(cb, ckpt_node);
			cpnd_restart_shm_ckpt_free(cb, ckpt_node);
			cpnd_ckpt_node_destroy(cb, ckpt_node);

			/* Broadcase CPA_EVT_ND2A_CKPT_DESTROY to CPA */
			memset(&send_evt, 0, sizeof(CPSV_EVT));
			send_evt.type = CPSV_EVT_TYPE_CPA;
			send_evt.info.cpa.type = CPA_EVT_ND2A_CKPT_DESTROY;
			send_evt.info.cpa.info.ckpt_destroy.ckpt_id =
			    prev_ckpt_id;
			cpnd_mds_bcast_send(cb, &send_evt, NCSMDS_SVC_ID_CPA);

			LOG_NO(
			    "cpnd node=0x%X DOWN - ckpt_node and replica destroy successful - cpkt_id - %llu",
			    m_NCS_NODE_ID_FROM_MDS_DEST(mds_dest),
			    prev_ckpt_id);
		}

		cpnd_ckpt_node_getnext(cb, prev_ckpt_id, &ckpt_node);
	}
	TRACE_LEAVE();
}

/****************************************************************************************
 * Name          : cpnd_get_scAbsenceAllowed_attr
 *
 * Description   : This function gets scAbsenceAllowed attribute
 *
 * Arguments     : -
 *
 * Return Values : scAbsenceAllowed attribute (0 = not allowed)
 *****************************************************************************************/
SaUint32T cpnd_get_scAbsenceAllowed_attr()
{
	SaUint32T rc_attr_val = 0;
	char *attribute_names[] = {"scAbsenceAllowed", NULL};

	TRACE_ENTER();

	rc_attr_val = cpnd_get_imm_attr(attribute_names);

	TRACE_LEAVE();
	return rc_attr_val;
}

/****************************************************************************************
 * Name          : cpnd_get_imm_attr
 *
 * Description   : This function gets IMM attribute
 *
 * Arguments     : -
 *
 * Return Values : scAbsenceAllowed attribute (0 = not allowed)
 *****************************************************************************************/
static SaUint32T cpnd_get_imm_attr(char **attribute_names)
{
	SaUint32T rc_attr_val = 0;
	SaAisErrorT rc = SA_AIS_OK;
	SaImmAccessorHandleT accessorHandle;
	SaImmHandleT immOmHandle;
	SaImmAttrValuesT_2 *attribute;
	SaImmAttrValuesT_2 **attributes;

	TRACE_ENTER();

	char object_name_str[] = "opensafImm=opensafImm,safApp=safImmService";

	SaNameT object_name;
	osaf_extended_name_lend(object_name_str, &object_name);

	/* Save immutil settings and reconfigure */
	struct ImmutilWrapperProfile tmp_immutilWrapperProfile;
	tmp_immutilWrapperProfile.errorsAreFatal =
	    immutilWrapperProfile.errorsAreFatal;
	tmp_immutilWrapperProfile.nTries = immutilWrapperProfile.nTries;
	tmp_immutilWrapperProfile.retryInterval =
	    immutilWrapperProfile.retryInterval;

	immutilWrapperProfile.errorsAreFatal = 0;
	immutilWrapperProfile.nTries = 500;
	immutilWrapperProfile.retryInterval = 1000;

	/* Initialize Om API */
	rc = immutil_saImmOmInitialize(&immOmHandle, NULL, &imm_version);
	if (rc != SA_AIS_OK) {
		LOG_ER("%s saImmOmInitialize FAIL %d", __FUNCTION__, rc);
		goto done;
	}

	/* Initialize accessor for reading attributes */
	rc = immutil_saImmOmAccessorInitialize(immOmHandle, &accessorHandle);
	if (rc != SA_AIS_OK) {
		LOG_ER("%s saImmOmAccessorInitialize Fail %s", __FUNCTION__,
		       saf_error(rc));
		goto done;
	}

	rc = immutil_saImmOmAccessorGet_2(accessorHandle, &object_name,
					  attribute_names, &attributes);
	if (rc != SA_AIS_OK) {
		TRACE("%s saImmOmAccessorGet_2 Fail '%s'", __FUNCTION__,
		      saf_error(rc));
		goto done_fin_Om;
	}

	void *value;

	attribute = attributes[0];
	TRACE("%s attrName \"%s\"", __FUNCTION__,
	      attribute ? attribute->attrName : "");
	if ((attribute != NULL) && (attribute->attrValuesNumber != 0)) {
		value = attribute->attrValues[0];
		rc_attr_val = *((SaUint32T *)value);
	}

done_fin_Om:
	/* Free Om resources */
	rc = immutil_saImmOmFinalize(immOmHandle);
	if (rc != SA_AIS_OK) {
		TRACE("%s saImmOmFinalize Fail '%s'", __FUNCTION__,
		      saf_error(rc));
	}

done:
	/* Restore immutil settings */
	immutilWrapperProfile.errorsAreFatal =
	    tmp_immutilWrapperProfile.errorsAreFatal;
	immutilWrapperProfile.nTries = tmp_immutilWrapperProfile.nTries;
	immutilWrapperProfile.retryInterval =
	    tmp_immutilWrapperProfile.retryInterval;

	TRACE_LEAVE();
	return rc_attr_val;
}
