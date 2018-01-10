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
  FILE NAME: cpcn_evt.c

  DESCRIPTION: CPND Routines to operate on client_info_db, ckpt_info_db.
	       (Routines to add/delete nodes into database, routines to
		destroy the databases)

******************************************************************************/

#include "ckpt/ckptnd/cpnd.h"

/****************************************************************************
 * Name          : cpnd_ckpt_node_get
 *
 * Description   : Function to get the ckpt node from Tree.
 *
 * Arguments     : CPND_CB *cb, - CPND Control Block
 *
 client* Return Values : CPND_CKPT_NODE** ckpt_node - Ckpt Node
 *
 * Notes         : None.
 *****************************************************************************/
void cpnd_ckpt_node_get(CPND_CB *cb, SaCkptCheckpointHandleT ckpt_hdl,
			CPND_CKPT_NODE **ckpt_node)
{
	*ckpt_node = (CPND_CKPT_NODE *)ncs_patricia_tree_get(
	    &cb->ckpt_info_db, (uint8_t *)&ckpt_hdl);
	return;
}

/****************************************************************************
 * Name          : cpnd_ckpt_node_getnext
 *
 * Description   : Function to get the ckpt node from Tree.
 *
 * Arguments     : CPND_CB *cb, - CPND Control Block
 *                 uint32_t ckpt_hdl - ckpt handle
 *
 * Return Values : CPND_CKPT_NODE** ckpt_node - Ckpt Node at CPND
 *
 * Notes         : None.
 *****************************************************************************/
void cpnd_ckpt_node_getnext(CPND_CB *cb, SaCkptCheckpointHandleT ckpt_hdl,
			    CPND_CKPT_NODE **ckpt_node)
{
	if (ckpt_hdl)
		*ckpt_node = (CPND_CKPT_NODE *)ncs_patricia_tree_getnext(
		    &cb->ckpt_info_db, (uint8_t *)&ckpt_hdl);
	else
		*ckpt_node = (CPND_CKPT_NODE *)ncs_patricia_tree_getnext(
		    &cb->ckpt_info_db, (uint8_t *)NULL);
	return;
}

/****************************************************************************
 * Name          : cpnd_ckpt_node_add
 *
 * Description   : Function to Add the Ckpt node into ckpt db Tree.
 *
 * Arguments     : CPND_CB *cb, - CPND Control Block
 *
 * Return Values :
 *
 * Notes         : None.
 *****************************************************************************/
uint32_t cpnd_ckpt_node_add(CPND_CB *cb, CPND_CKPT_NODE *ckpt_node)
{
	uint32_t rc = NCSCC_RC_FAILURE;

	ckpt_node->patnode.key_info = (uint8_t *)&ckpt_node->ckpt_id;

	rc = ncs_patricia_tree_add(&cb->ckpt_info_db,
				   (NCS_PATRICIA_NODE *)&ckpt_node->patnode);
	return rc;
}

/****************************************************************************
 * Name          : cpnd_ckpt_node_del
 *
 * Description   : Function to Delete the Ckpt Node into Ckpt Db Tree.
 *
 * Arguments     : CPND_CB *cb, - CPND Control Block.
 *               : CPND_CKPT_NODE* ckpt_node - Ckpt Node to del.
 *
 * Return Values :
 *
 * Notes         : None.
 *****************************************************************************/
uint32_t cpnd_ckpt_node_del(CPND_CB *cb, CPND_CKPT_NODE *ckpt_node)
{
	uint32_t rc = NCSCC_RC_FAILURE;

	rc = ncs_patricia_tree_del(&cb->ckpt_info_db,
				   (NCS_PATRICIA_NODE *)&ckpt_node->patnode);
	return rc;
}

/****************************************************************************
 * Name          : cpnd_ckpt_node_destroy
 *
 * Description   : Function to Delete the Ckpt Node, all its internal pointers
 *and its references.
 *
 * Arguments     : CPND_CB *cb, - CPND Control Block.
 *               : CPND_CKPT_NODE* ckpt_node - Ckpt Node to del.
 *
 * Return Values :
 *
 * Notes         : None.
 *****************************************************************************/
void cpnd_ckpt_node_destroy(CPND_CB *cb, CPND_CKPT_NODE *cp_node)
{

	/* Remove the client reference from ckpt node cp_node */

	CPND_CKPT_CLLIST_NODE *cllist_node = cp_node->clist;
	CPND_CKPT_CLLIST_NODE *free_cllist_node = NULL;
	CPSV_CPND_DEST_INFO *tmp = NULL, *free_tmp = NULL;
	CPND_CKPT_CLIENT_NODE *cl_node = NULL;

	TRACE_ENTER();
	while (cllist_node) {
		cl_node = cllist_node->cnode;
		cpnd_client_ckpt_info_delete(cl_node, cp_node);

		/* cllist_node is no longer required, free it and adjust the
		 * list */
		free_cllist_node = cllist_node;

		cllist_node = cllist_node->next;

		m_MMGR_FREE_CPND_CKPT_CLIST_NODE(free_cllist_node);
	}

	/* Remove the cp_node from patricia */
	cpnd_ckpt_node_del(cb, cp_node);

	tmp = cp_node->cpnd_dest_list;
	while (tmp != NULL) {
		free_tmp = tmp;
		tmp = tmp->next;
		m_MMGR_FREE_CPND_DEST_INFO(free_tmp);
	}
	cp_node->cpnd_dest_list = tmp;

	if (cp_node->open_active_sync_tmr.is_active)
		cpnd_tmr_stop(&cp_node->open_active_sync_tmr);
	if (cp_node->ret_tmr.is_active)
		cpnd_tmr_stop(&cp_node->ret_tmr);

	cpnd_ckpt_sec_map_destroy(&cp_node->replica_info);

	free((void *)cp_node->ckpt_name);

	m_MMGR_FREE_CPND_CKPT_NODE(cp_node);
	TRACE_LEAVE();
}

/* CLIENT DB OPER */
/****************************************************************************
 * Name          : cpnd_client_node_get
 *
 * Description   : Function to get the client node from client db Tree.
 *
 * Arguments     : CPND_CB *cb, - CPND Control Block
 *               : uint32_t ckpt_client_hdl - Client Handle.
 *
 client* Return Values : CPND_CKPT_CLIENT_NODE** ckpt_client_node
 *
 * Notes         : None.
 *****************************************************************************/
void cpnd_client_node_get(CPND_CB *cb, SaCkptHandleT ckpt_client_hdl,
			  CPND_CKPT_CLIENT_NODE **ckpt_client_node)
{
	*ckpt_client_node = (CPND_CKPT_CLIENT_NODE *)ncs_patricia_tree_get(
	    &cb->client_info_db, (uint8_t *)&ckpt_client_hdl);
	return;
}

/****************************************************************************
 * Name          : cpnd_number_of_clients_get
 *
 * Description   : Function to get the number of client nodes from client db
 *Tree.
 *
 * Arguments     : CPND_CB *cb, - CPND Control Block
 *
 * Return Values : uint32_t, the number of clients in db Tree.
 *
 * Notes         : None.
 *****************************************************************************/
uint32_t cpnd_number_of_clients_get(CPND_CB *cb)
{
	return (uint32_t)ncs_patricia_tree_size(&cb->client_info_db);
}

/****************************************************************************
 * Name          : cpnd_client_node_getnext
 *
 * Description   : Function to get next ckpt node of a given ckpt from ckpt
 *Tree.
 *
 * Arguments     : CPND_CB *cb, - CPND Control Block
 *                 uint32_t ckpt_hdl - ckpt handle
 *
 * Return Values : CPND_CKPT_CLIENT_NODE** ckpt_node - Ckpt Node at CPND
 *
 * Notes         : None.
 *****************************************************************************/
void cpnd_client_node_getnext(CPND_CB *cb, SaCkptHandleT ckpt_client_hdl,
			      CPND_CKPT_CLIENT_NODE **ckpt_client_node)
{
	if (ckpt_client_hdl)
		*ckpt_client_node =
		    (CPND_CKPT_CLIENT_NODE *)ncs_patricia_tree_getnext(
			&cb->client_info_db, (uint8_t *)&ckpt_client_hdl);
	else
		*ckpt_client_node =
		    (CPND_CKPT_CLIENT_NODE *)ncs_patricia_tree_getnext(
			&cb->client_info_db, (uint8_t *)NULL);
	return;
}

/****************************************************************************
 * Name          : cpnd_client_node_add
 *
 * Description   : Function to Add the Ckpt node into ckpt db Tree.
 *
 * Arguments     : CPND_CB *cb, - CPND Control Block
 *
 * Return Values :
 *
 * Notes         : None.
 *****************************************************************************/
uint32_t cpnd_client_node_add(CPND_CB *cb, CPND_CKPT_CLIENT_NODE *ckpt_node)
{
	uint32_t rc = NCSCC_RC_FAILURE;

	ckpt_node->patnode.key_info = (uint8_t *)&ckpt_node->ckpt_app_hdl;

	rc = ncs_patricia_tree_add(&cb->client_info_db,
				   (NCS_PATRICIA_NODE *)&ckpt_node->patnode);
	return rc;
}

/****************************************************************************
 * Name          : cpnd_client_node_del
 *
 * Description   : Function to Delete the Client Node into Client Db Tree.
 *
 * Arguments     : CPND_CB *cb, - CPND Control Block.
 *               : CPND_CKPT_CLIENT_NODE* ckpt_client_node - CheckPoint Client
 *Node to del.
 *
 * Return Values : NCSCC_RC_FAILURE/NCSCC_RC_SUCCESS
 *
 * Notes         : None.
 *****************************************************************************/
uint32_t cpnd_client_node_del(CPND_CB *cb,
			      CPND_CKPT_CLIENT_NODE *ckpt_client_node)
{
	uint32_t rc = NCSCC_RC_FAILURE;

	rc = ncs_patricia_tree_del(
	    &cb->client_info_db,
	    (NCS_PATRICIA_NODE *)&ckpt_client_node->patnode);
	return rc;
}

/****************************************************************************
 * Name          : cpnd_ckpt_node_find_by_name
 *
 * Description   : Function to Find the checkpoint Node using name.
 *
 * Arguments     : CPND_CB *cb, - CPND Control Block.
 *               : SaNameT ckpt_name - Check point Name.
 *
 * Return Values :  NULL / CPND_CKPT_NODE
 *
 * Notes         : None.
 *****************************************************************************/
CPND_CKPT_NODE *cpnd_ckpt_node_find_by_name(CPND_CB *cpnd_cb,
					    SaConstStringT ckpt_name)
{
	CPND_CKPT_NODE *ckpt_node = NULL;
	SaCkptCheckpointHandleT prev_ckpt_id;

	cpnd_ckpt_node_getnext(cpnd_cb, 0, &ckpt_node);

	while (ckpt_node) {
		prev_ckpt_id = ckpt_node->ckpt_id;
		if (strcmp(ckpt_name, ckpt_node->ckpt_name) == 0) {
			return ckpt_node;
		}
		cpnd_ckpt_node_getnext(cpnd_cb, prev_ckpt_id, &ckpt_node);
	}
	return NULL;
}

/****************************************************************************
 * Name          : cpnd_evt_node_get
 *
 * Description   : Function to get the all repl evt node from Tree.
 *
 * Arguments     : CPND_CB *cb, - CPND Control Block
 *
 * Return Values : CPSV_CPND_ALL_REPL_EVT_NODE ** evt_node - evt Node
 *
 * Notes         : None.
 *****************************************************************************/
void cpnd_evt_node_get(CPND_CB *cb, SaCkptCheckpointHandleT lcl_ckpt_id,
		       CPSV_CPND_ALL_REPL_EVT_NODE **evt_node)
{
	*evt_node = (CPSV_CPND_ALL_REPL_EVT_NODE *)ncs_patricia_tree_get(
	    &cb->writeevt_db, (uint8_t *)&lcl_ckpt_id);
	return;
}

/****************************************************************************
 * Name          : cpnd_evt_node_getnext
 *
 * Description   : Function to get the all repl evt node from Tree.
 *
 * Arguments     : CPND_CB *cb, - CPND Control Block
 *
 * Return Values : CPSV_CPND_ALL_REPL_EVT_NODE** - evt Node at CPND
 *
 * Notes         : None.
 *****************************************************************************/
void cpnd_evt_node_getnext(CPND_CB *cb, SaCkptCheckpointHandleT lcl_ckpt_id,
			   CPSV_CPND_ALL_REPL_EVT_NODE **evt_node)
{
	if (lcl_ckpt_id)
		*evt_node =
		    (CPSV_CPND_ALL_REPL_EVT_NODE *)ncs_patricia_tree_getnext(
			&cb->writeevt_db, (uint8_t *)&lcl_ckpt_id);
	else
		*evt_node =
		    (CPSV_CPND_ALL_REPL_EVT_NODE *)ncs_patricia_tree_getnext(
			&cb->writeevt_db, (uint8_t *)NULL);
	return;
}

/****************************************************************************
 * Name          : cpnd_evt_node_add
 *
 * Description   : Function to Add the all repl write evt node into ckpt db
 *Tree.
 *
 * Arguments     : CPND_CB *cb, - CPND Control Block
 *
 * Return Values :
 *
 * Notes         : None.
 *****************************************************************************/
uint32_t cpnd_evt_node_add(CPND_CB *cb, CPSV_CPND_ALL_REPL_EVT_NODE *evt_node)
{
	uint32_t rc = NCSCC_RC_FAILURE;

	evt_node->patnode.key_info = (uint8_t *)&evt_node->lcl_ckpt_id;

	rc = ncs_patricia_tree_add(&cb->writeevt_db,
				   (NCS_PATRICIA_NODE *)&evt_node->patnode);
	return rc;
}

/****************************************************************************
 * Name          : cpnd_evt_node_del
 *
 * Description   : Function to Delete the evt Node info in write event Db Tree.
 *
 * Arguments     : CPND_CB *cb, - CPND Control Block.
 *               : CPSV_CPND_ALL_REPL_EVT_NODE* evt_node - Evt Node to del.
 *
 * Return Values : NCSCC_RC_FAILURE/NCSCC_RC_SUCCESS
 *
 * Notes         : None.
 *****************************************************************************/
uint32_t cpnd_evt_node_del(CPND_CB *cb, CPSV_CPND_ALL_REPL_EVT_NODE *evt_node)
{
	uint32_t rc = NCSCC_RC_FAILURE;

	rc = ncs_patricia_tree_del(&cb->writeevt_db,
				   (NCS_PATRICIA_NODE *)&evt_node->patnode);
	return rc;
}

/****************************************************************************
 * Name          : cpnd_ckpt_sec_add
 *
 * Description   : Function to add the section to a checkpoint.
 *
 * Arguments     : CPND_CKPT_NODE *cp_node - Check point node.
 *               : SaCkptSectionIdT id - Section Identifier
 *
 * Return Values :  NULL/CPND_CKPT_SECTION_INFO
 *
 * Notes         : None.
 *****************************************************************************/
CPND_CKPT_SECTION_INFO *cpnd_ckpt_sec_add(CPND_CB *cb, CPND_CKPT_NODE *cp_node,
					  SaCkptSectionIdT *id,
					  SaTimeT exp_time, uint32_t gen_flag)
{
	CPND_CKPT_SECTION_INFO *pSecPtr = NULL;
	int32_t lcl_sec_id = 0;
	uint32_t rc = NCSCC_RC_SUCCESS;
	uint32_t value = 0, i = 0, j = 0;
	bool hdr_update = true;

	TRACE_ENTER();
	/* get the lcl_sec_id */

	lcl_sec_id = cpnd_ckpt_get_lck_sec_id(cp_node);
	if (lcl_sec_id ==
	    -1) { /* -1 is invalid section Id need to define it in *.h file */
		TRACE_4("cpnd creating more than max sections");
		return NULL;
	}

	/* creat the cpnd_ckpt_section_info structure,memset */
	pSecPtr = m_MMGR_ALLOC_CPND_CKPT_SECTION_INFO;
	if (pSecPtr == NULL) {
		LOG_ER("cpnd ckpt section info memory allocation failed");
		return NULL;
	}

	memset(pSecPtr, '\0', sizeof(CPND_CKPT_SECTION_INFO));

	if (gen_flag) {

		if (cp_node->create_attrib.maxSectionIdSize >
		    CPSV_GEN_SECTION_ID_SIZE)
			pSecPtr->sec_id.idLen = CPSV_GEN_SECTION_ID_SIZE;
		else
			pSecPtr->sec_id.idLen =
			    cp_node->create_attrib.maxSectionIdSize;

		/* convert the lcl_sec_id to Network order */
		value = lcl_sec_id;
		pSecPtr->sec_id.id =
		    m_MMGR_ALLOC_CPND_DEFAULT(pSecPtr->sec_id.idLen);
		if (pSecPtr->sec_id.id == NULL) {
			LOG_ER("cpnd sect memory allocation failed");
			goto sec_id_allocate_fails;
		}
		memset(pSecPtr->sec_id.id, '\0', pSecPtr->sec_id.idLen);

		for (i = (CPSV_GEN_SECTION_ID_SIZE - pSecPtr->sec_id.idLen);
		     i < CPSV_GEN_SECTION_ID_SIZE; i++) {
			pSecPtr->sec_id.id[j] =
			    (value >> (24 - (8 * i))) & 0xff;
			j++;
		}
	} else {
		pSecPtr->sec_id.idLen = id->idLen;
		pSecPtr->sec_id.id = NULL;
		if (id->idLen > 0) {
			pSecPtr->sec_id.id =
			    m_MMGR_ALLOC_CPND_DEFAULT(id->idLen);
			memcpy(pSecPtr->sec_id.id, id->id,
			       pSecPtr->sec_id.idLen);
		}
	}

	pSecPtr->exp_tmr = exp_time;
	pSecPtr->lcl_sec_id = lcl_sec_id;
	pSecPtr->sec_state = SA_CKPT_SECTION_VALID;

	/* add the structure */
	rc = cpnd_ckpt_sec_add_db(&cp_node->replica_info, pSecPtr);
	if (rc == NCSCC_RC_FAILURE) {
		LOG_ER("unable to add section to database");
		goto section_add_fails;
	}

	cp_node->replica_info.n_secs++;

	/* UPDATE THE SECTION HEADER */
	rc = cpnd_sec_hdr_update(cb, pSecPtr, cp_node);
	if (rc == NCSCC_RC_FAILURE) {
		LOG_ER("cpnd sect hdr update failed");
		goto section_hdr_update_fails;
	}
	/* UPDATE THE CHECKPOINT HEADER */
	rc = cpnd_ckpt_hdr_update(cb, cp_node);
	if (rc == NCSCC_RC_FAILURE) {
		LOG_ER("cpnd ckpt hdr update failed");
		goto ckpt_hdr_update_fails;
	}

	TRACE_LEAVE();
	return pSecPtr;

section_hdr_update_fails:
	hdr_update = false;
ckpt_hdr_update_fails:
	cpnd_ckpt_sec_del(cb, cp_node, id, hdr_update);

section_add_fails:
	if (pSecPtr->sec_id.id != NULL)
		m_MMGR_FREE_CPND_DEFAULT(pSecPtr->sec_id.id);

sec_id_allocate_fails:
	m_MMGR_FREE_CPND_CPND_CKPT_SECTION_INFO(pSecPtr);
	TRACE_LEAVE();
	return NULL;
}

/****************************************************************************
 * Name          : cpnd_evt_backup_queue_add
 *
 * Description   : Function to add write event into back up queue
 *
 * Arguments     : CPND_CB *cb - CPND CB pointer
 *                 CPND_EVT *evt - CPND Event structure
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 * Notes         : None.
 *****************************************************************************/
void cpnd_evt_backup_queue_add(CPND_CKPT_NODE *cp_node, CPND_EVT *evt)
{
#define offset_of(TYPE, MEMBER) ((long)&((TYPE *)0)->MEMBER)

#define container_of(ptr, type, member)                                        \
	({                                                                     \
		const typeof(((type *)0)->member) *__mptr = (ptr);             \
		(type *)((char *)__mptr - offset_of(type, member));            \
	})

	CPSV_EVT *ptr = container_of(evt, CPSV_EVT, info.cpnd);
	CPSV_EVT *tmp_evt = NULL;

	evt->dont_free_me = true;

	ptr->next = NULL;
	/* add it to the queue */

	if (cp_node->evt_bckup_q == NULL) {
		cp_node->evt_bckup_q = ptr;
	} else {
		tmp_evt = cp_node->evt_bckup_q; /* can change the below logic
						   with ptr to last */
		while (tmp_evt->next != NULL)
			tmp_evt = tmp_evt->next;
		tmp_evt->next = ptr;
	}
	return;
}

/****************************************************************************
 * Name          : cpnd_ckpt_node_tree_init
 *
 * Description   : Function to Initialise ckpt tree
 *
 * Arguments     : CPND_CB *cb - CPND CB pointer
 *
 * Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE.
 *
 * Notes         : None.
 *****************************************************************************/
uint32_t cpnd_ckpt_node_tree_init(CPND_CB *cb)
{
	NCS_PATRICIA_PARAMS param;
	memset(&param, 0, sizeof(NCS_PATRICIA_PARAMS));
	param.key_size = sizeof(SaCkptCheckpointHandleT);
	if (ncs_patricia_tree_init(&cb->ckpt_info_db, &param) !=
	    NCSCC_RC_SUCCESS) {
		LOG_ER("ckpt node patricia tree init failed");
		return NCSCC_RC_FAILURE;
	}
	return NCSCC_RC_SUCCESS;
}

/****************************************************************************
 * Name          :  cpnd_client_node_tree_init
 *
 * Description   :  Function to Initialise client tree
 *
 * Arguments     : CPND_CB *cb - CPND CB pointer
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 * Notes         : None.
 *****************************************************************************/
uint32_t cpnd_client_node_tree_init(CPND_CB *cb)
{
	NCS_PATRICIA_PARAMS param;
	memset(&param, 0, sizeof(NCS_PATRICIA_PARAMS));
	param.key_size = sizeof(SaCkptHandleT);
	if (ncs_patricia_tree_init(&cb->client_info_db, &param) !=
	    NCSCC_RC_SUCCESS) {
		LOG_ER("client node patricia tree init failed");
		return NCSCC_RC_FAILURE;
	}

	return NCSCC_RC_SUCCESS;
}

/****************************************************************************
  Name          : cpnd_ckpt_tree_cleanup
  Description   : This routine Removes & Frees all the internal nodes.
  Arguments     : CPND_CB *cb - CPD Control Block.
  Return Values : None
  Notes         : None
******************************************************************************/
void cpnd_ckpt_node_tree_cleanup(CPND_CB *cb)
{

	CPND_CKPT_NODE *cp_node = NULL;
	CPND_CKPT_CLLIST_NODE *cp_cl_ref = NULL, *tmp_ref = NULL;
	CPSV_CPND_DEST_INFO *cpnd_dest_list = NULL, *tmp_dest = NULL;
	SaAisErrorT error;

	TRACE_ENTER();
	while ((cp_node = (CPND_CKPT_NODE *)ncs_patricia_tree_getnext(
		    &cb->ckpt_info_db, (uint8_t *)0))) {

		cp_cl_ref = cp_node->clist;
		while (cp_cl_ref != NULL) {
			tmp_ref = cp_cl_ref;
			cp_cl_ref = cp_cl_ref->next;
			m_MMGR_FREE_CPND_CKPT_CLIST_NODE(tmp_ref);
		}

		cpnd_dest_list = cp_node->cpnd_dest_list;
		while (cpnd_dest_list != NULL) {
			tmp_dest = cpnd_dest_list;
			cpnd_dest_list = cpnd_dest_list->next;
			m_MMGR_FREE_CPND_DEST_INFO(tmp_dest);
		}

		cpnd_ckpt_replica_destroy(cb, cp_node, &error);

		ncs_patricia_tree_del(&cb->ckpt_info_db,
				      (NCS_PATRICIA_NODE *)&cp_node->patnode);
		if (cp_node->ret_tmr.is_active)
			cpnd_tmr_stop(&cp_node->ret_tmr);

		cpnd_ckpt_sec_map_destroy(&cp_node->replica_info);

		m_MMGR_FREE_CPND_CKPT_NODE(cp_node);
	}
	TRACE_LEAVE();
	return;
}

/****************************************************************************
 * Name          : cpnd_ckpt_node_tree_destroy
 *
 * Description   : Function to destroy ckpt db tree
 *
 * Arguments     : CPND_CB *cb - CPND CB pointer
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 * Notes         : None.
 *****************************************************************************/
void cpnd_ckpt_node_tree_destroy(CPND_CB *cb)
{
	/* cleanup the client tree */
	cpnd_ckpt_node_tree_cleanup(cb);

	/* destroy the tree */
	ncs_patricia_tree_destroy(&cb->ckpt_info_db);

	return;
}

/****************************************************************************
 * Name          : cpnd_client_node_tree_cleanup
 * Description   : Function to cleanup client db info
 * Arguments     : CPND_CB *cb - CPND CB pointer
 * Return Values : NCSCC_RC_SUCCESS/Error.
 * Notes         : None.
 *****************************************************************************/
void cpnd_client_node_tree_cleanup(CPND_CB *cb)
{

	CPND_CKPT_CLIENT_NODE *cl_node = NULL;
	CPND_CKPT_CKPT_LIST_NODE *cl_ckpt_ref = NULL, *tmp_ref = NULL;

	while ((cl_node = (CPND_CKPT_CLIENT_NODE *)ncs_patricia_tree_getnext(
		    &cb->client_info_db, (uint8_t *)0))) {

		cl_ckpt_ref = cl_node->ckpt_list;
		while (cl_ckpt_ref != NULL) {
			tmp_ref = cl_ckpt_ref;
			cl_ckpt_ref = cl_ckpt_ref->next;
			m_MMGR_FREE_CPND_CKPT_LIST_NODE(tmp_ref);
		}
		ncs_patricia_tree_del(&cb->client_info_db,
				      (NCS_PATRICIA_NODE *)&cl_node->patnode);
		m_MMGR_FREE_CPND_CKPT_CLIENT_NODE(cl_node);
	}

	return;
}

/****************************************************************************
 * Name          : cpnd_client_node_tree_destroy
 * Description   : Function to destroy client db tree
 * Arguments     : CPND_CB *cb - CPND CB pointer
 * Return Values : NCSCC_RC_SUCCESS/Error.
 * Notes         : None.
 *****************************************************************************/
void cpnd_client_node_tree_destroy(CPND_CB *cb)
{
	/* cleanup the client tree */
	cpnd_client_node_tree_cleanup(cb);

	/* destroy the tree */
	ncs_patricia_tree_destroy(&cb->client_info_db);

	return;
}

/****************************************************************************
 * Name          :  cpnd_allrepl_write_evt_node_tree_init
 *
 * Description   :  Function to Initialise all replica write evt tree
 *
 * Arguments     : CPND_CB *cb - CPND CB pointer
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 * Notes         : None.
 *****************************************************************************/
uint32_t cpnd_allrepl_write_evt_node_tree_init(CPND_CB *cb)
{
	NCS_PATRICIA_PARAMS param;
	memset(&param, 0, sizeof(NCS_PATRICIA_PARAMS));
	param.key_size = sizeof(SaCkptCheckpointHandleT);
	if (ncs_patricia_tree_init(&cb->writeevt_db, &param) !=
	    NCSCC_RC_SUCCESS)
		return NCSCC_RC_FAILURE;

	return NCSCC_RC_SUCCESS;
}

/****************************************************************************
 * Name          : cpnd_allrepl_write_evt_node_tree_cleanup
 * Description   : Function to cleanup writeevt_db info
 * Arguments     : CPND_CB *cb - CPND CB pointer
 * Return Values : NCSCC_RC_SUCCESS/Error.
 * Notes         : None.
 *****************************************************************************/
void cpnd_allrepl_write_evt_node_tree_cleanup(CPND_CB *cb)
{

	CPSV_CPND_ALL_REPL_EVT_NODE *evt_node = NULL;
	CPSV_CPND_UPDATE_DEST *evt_ckpt_ref = NULL, *tmp_ref = NULL;

	while ((evt_node =
		    (CPSV_CPND_ALL_REPL_EVT_NODE *)ncs_patricia_tree_getnext(
			&cb->writeevt_db, (uint8_t *)0))) {

		evt_ckpt_ref = evt_node->cpnd_update_dest_list;
		while (evt_ckpt_ref != NULL) {
			tmp_ref = evt_ckpt_ref;
			evt_ckpt_ref = evt_ckpt_ref->next;
			m_MMGR_FREE_CPND_UPDATE_DEST_INFO(tmp_ref);
		}
		ncs_patricia_tree_del(&cb->writeevt_db,
				      (NCS_PATRICIA_NODE *)&evt_node->patnode);
		m_MMGR_FREE_CPND_ALL_REPL_EVT_NODE(evt_node);
	}

	return;
}

/****************************************************************************
 * Name          : cpnd_allrepl_write_evt_node_tree_destroy
 * Description   : Function to destroy writeevt_db tree
 * Arguments     : CPND_CB *cb - CPND CB pointer
 * Return Values : NCSCC_RC_SUCCESS/Error.
 * Notes         : None.
 *****************************************************************************/
void cpnd_allrepl_write_evt_node_tree_destroy(CPND_CB *cb)
{
	/* cleanup the ALL REPLICA event tree */
	cpnd_allrepl_write_evt_node_tree_cleanup(cb);

	/* destroy the tree */
	ncs_patricia_tree_destroy(&cb->writeevt_db);

	return;
}

/***********************************************************************************
 * Name            : cpnd_get_node_id_from_mds_dest
 *
 * Description     : To get the Node ID from MDS_DEST
 *
 *********************************************************************************/
NCS_NODE_ID cpnd_get_node_id_from_mds_dest(MDS_DEST dest)
{
	return m_NCS_NODE_ID_FROM_MDS_DEST(dest);
}

/******************************************************************************************
 * Name            : cpnd_agent_dest_add
 *
 * Description     : To add the agent list to checkpoint node
 *
 *****************************************************************************************/
void cpnd_agent_dest_add(CPND_CKPT_NODE *cp_node, MDS_DEST adest)
{
	CPSV_CPND_DEST_INFO *cpnd_dest = NULL;
	cpnd_dest = m_MMGR_ALLOC_CPND_DEST_INFO;
	if (cpnd_dest) {
		memset(cpnd_dest, 0, sizeof(CPSV_CPND_DEST_INFO));

		cpnd_dest->next = cp_node->agent_dest_list;
		cpnd_dest->dest = adest;
		cp_node->agent_dest_list = cpnd_dest;
	}
	/* GBLCK shashi put dlsv log */
}

/******************************************************************************************
 * Name            : cpnd_agent_dest_del
 *
 * Description     : To delete the agent dest from the linked list
 *  Scenario is 1st-CPA & 2nd-CPA with 2 ckpt_open
 *******************************************************************************************/
void cpnd_agent_dest_del(CPND_CKPT_NODE *cp_node, MDS_DEST adest)
{
	CPSV_CPND_DEST_INFO *cpnd_dest = NULL, *cpnd_tmp_dest = NULL;

	TRACE_ENTER();
	/* Logic is to delete the duplicates in the linked list */
	cpnd_dest = cp_node->agent_dest_list;

	while (cpnd_dest) {
		if (cpnd_dest->dest == adest) {
			if (cpnd_dest == cp_node->agent_dest_list) {
				cp_node->agent_dest_list =
				    cp_node->agent_dest_list->next;
				m_MMGR_FREE_CPND_DEST_INFO(cpnd_dest);
				cpnd_dest = cp_node->agent_dest_list;
			} else {
				cpnd_tmp_dest->next = cpnd_dest->next;
				m_MMGR_FREE_CPND_DEST_INFO(cpnd_dest);
				cpnd_dest = cpnd_tmp_dest->next;
			}
		} else {
			/* cpnd_tmp_dest holds the prev occurance of cpa dest
			 * block,which need to free */
			cpnd_tmp_dest = cpnd_dest;
			cpnd_dest = cpnd_dest->next;
		}
	}

	TRACE_LEAVE();
}

/********************************************************************************************
 *
 * Name     :  cpnd_agent_read_del
 *
 * Description : This function processes pending writes. In the obsence of CPA
 *down, the same events gets processes in read_ack events
 *
 *******************************************************************************************/

void cpnd_proc_pending_writes(CPND_CB *cb, CPND_CKPT_NODE *cp_node,
			      MDS_DEST adest)
{
	CPSV_EVT *bck_evt = NULL;
	uint32_t err_flag = 0;
	CPSV_SEND_INFO *sinfo = NULL;
	uint32_t errflag = 0;
	/* This check is for one write and 1 local read and then kill the reader
	 */
	TRACE_ENTER();
	cpnd_agent_dest_del(cp_node, adest);

	while (cp_node->evt_bckup_q != NULL) {
		bck_evt = cp_node->evt_bckup_q;

		cpnd_ckpt_update_replica(
		    cb, cp_node, &bck_evt->info.cpnd.info.ckpt_nd2nd_data,
		    bck_evt->info.cpnd.info.ckpt_nd2nd_data.type, &err_flag,
		    &errflag);

		cpnd_proc_ckpt_arrival_info_ntfy(
		    cb, cp_node, &bck_evt->info.cpnd.info.ckpt_nd2nd_data,
		    sinfo);

		cp_node->evt_bckup_q = cp_node->evt_bckup_q->next;

		bck_evt->info.cpnd.dont_free_me = false;

		cpnd_evt_destroy(bck_evt);
	}
	TRACE_LEAVE();
}

void cpnd_clm_cluster_track_cb(
    const SaClmClusterNotificationBufferT *notificationBuffer,
    SaUint32T numberOfMembers, SaAisErrorT error)
{
	CPND_CB *cb = NULL;
	SaClmNodeIdT node_id;
	uint32_t counter = 0;

	TRACE_ENTER2("error argument value %d", error);

	if (error != SA_AIS_OK)
		return;
	m_CPND_RETRIEVE_CB(cb);
	if (cb == NULL) {
		LOG_ER("cpnd cb retrieval failed");
		return;
	}
	if (notificationBuffer != NULL)
		for (counter = 0; counter < notificationBuffer->numberOfItems;
		     counter++) {
			if (notificationBuffer->notification[counter]
				.clusterChange == SA_CLM_NODE_LEFT) {
				node_id =
				    notificationBuffer->notification[counter]
					.clusterNode.nodeId;
				if (node_id == cb->nodeid) {
					if (cpnd_proc_ckpt_clm_node_left(cb) !=
					    NCSCC_RC_SUCCESS) {
						TRACE_4(
						    "cpnd clm node get failed");
					}
					cpnd_proc_cpd_down(cb);
				}
				TRACE(
				    "node_left -%d -%s line-%d clusterChange-%d",
				    node_id, __FILE__, __LINE__,
				    notificationBuffer->notification[counter]
					.clusterChange);
			} else if ((notificationBuffer->notification[counter]
					.clusterChange ==
				    SA_CLM_NODE_NO_CHANGE) ||
				   (notificationBuffer->notification[counter]
					.clusterChange == SA_CLM_NODE_JOINED) ||
				   (notificationBuffer->notification[counter]
					.clusterChange ==
				    SA_CLM_NODE_RECONFIGURED)) {
				node_id =
				    notificationBuffer->notification[counter]
					.clusterNode.nodeId;
				if (node_id == cb->nodeid) {
					if (cpnd_proc_ckpt_clm_node_joined(
						cb) != NCSCC_RC_SUCCESS) {
						TRACE_4(
						    "cpnd clm node get failed");
					}
				}
				TRACE(
				    "node_joined -%d -%s line-%d clusterChange-%d ",
				    node_id, __FILE__, __LINE__,
				    notificationBuffer->notification[counter]
					.clusterChange);
			}
		}
	m_CPND_GIVEUP_CB;
	TRACE_LEAVE();
	return;
}
