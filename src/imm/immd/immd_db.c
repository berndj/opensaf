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
 * Author(s): Ericsson AB
 *
 */

/*****************************************************************************

  DESCRIPTION:

  This file contains the IMMD Database access Routines
*****************************************************************************/

#include "immd.h"

/****************************************************************************
  Name          : immd_immnd_info_tree_init
  Description   : This routine is used to initialize the IMMND info Tree
  Arguments     : cb - pointer to the IMMD Control Block
  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE
  Notes         : None
*****************************************************************************/
uint32_t immd_immnd_info_tree_init(IMMD_CB *cb)
{
	NCS_PATRICIA_PARAMS param;
	memset(&param, 0, sizeof(NCS_PATRICIA_PARAMS));

	param.key_size = sizeof(NODE_ID);
	if (ncs_patricia_tree_init(&cb->immnd_tree, &param) !=
	    NCSCC_RC_SUCCESS) {
		return NCSCC_RC_FAILURE;
	}
	cb->is_immnd_tree_up = true;
	return NCSCC_RC_SUCCESS;
}

/****************************************************************************
  Name          : immd_immnd_info_node_get
  Description   : This routine finds the IMMND Info node.
  Arguments     : immnd_tree - IMMND Tree.
		  dest - MDS_DEST
  Return Values : immnd_info_node - IMMND Node
		  NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE
*****************************************************************************/
uint32_t immd_immnd_info_node_get(NCS_PATRICIA_TREE *immnd_tree, MDS_DEST *dest,
				  IMMD_IMMND_INFO_NODE **immnd_info_node)
{
	NODE_ID key;

	memset(&key, 0, sizeof(NODE_ID));
	/* Fill the Key */
	key = m_NCS_NODE_ID_FROM_MDS_DEST((*dest));

	*immnd_info_node = (IMMD_IMMND_INFO_NODE *)ncs_patricia_tree_get(
	    immnd_tree, (uint8_t *)&key);

	if (*immnd_info_node && !(*immnd_info_node)->isUp) {
		*immnd_info_node = NULL;
	}

	return NCSCC_RC_SUCCESS;
}

/****************************************************************************
  Name          : immd_immnd_info_node_getnext
  Description   : This routine finds the IMMND Info node.
  Arguments     : immnd_tree - IMMND Tree.
		  dest - MDS_DEST
  Return Values : immnd_info_node - IMMND Node
		  NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE
  Notes         : The caller takes the cb lock before calling this function
*****************************************************************************/
void immd_immnd_info_node_getnext(NCS_PATRICIA_TREE *immnd_tree, MDS_DEST *dest,
				  IMMD_IMMND_INFO_NODE **immnd_info_node)
{
	IMMD_IMMND_INFO_NODE* next_node = NULL;
	MDS_DEST* current_dest = dest;
	do {
		if (current_dest) {
			NODE_ID key;
			memset(&key, 0, sizeof(NODE_ID));
			key = m_NCS_NODE_ID_FROM_MDS_DEST((*current_dest));

			next_node =
			    (IMMD_IMMND_INFO_NODE *)ncs_patricia_tree_getnext(
				immnd_tree, (uint8_t *)&key);
		} else {
			next_node =
			    (IMMD_IMMND_INFO_NODE *)ncs_patricia_tree_getnext(
				immnd_tree, (uint8_t *)NULL);
		}
		if (next_node) {
			current_dest = &next_node->immnd_dest;
		}
	} while (next_node && !next_node->isUp);

	*immnd_info_node = next_node;

	return;
}

/****************************************************************************
  Name          : immd_immnd_info_node_add
  Description   : This routine adds the new node to immnd_tree.
  Arguments     : immnd_tree - IMMND Tree.
		  immnd_node -  IMMND Node.
  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE
  Notes         : The caller takes the cb lock before calling this function
*****************************************************************************/
#if 0 /*CURRENTLY NOT CALLED */
uint32_t immd_immnd_info_node_add(NCS_PATRICIA_TREE *immnd_tree, IMMD_IMMND_INFO_NODE *immnd_info_node)
{
	/* Store the client_info pointer as msghandle. */
	NODE_ID key;

	key = m_NCS_NODE_ID_FROM_MDS_DEST(immnd_info_node->immnd_dest);

	immnd_info_node->patnode.key_info = (uint8_t *)&key;

	if (ncs_patricia_tree_add(immnd_tree, &immnd_info_node->patnode) != NCSCC_RC_SUCCESS) {
		LOG_ER("IMMD - ncs_patricia_tree_add failed");
		return NCSCC_RC_FAILURE;
	}

	return NCSCC_RC_SUCCESS;
}
#endif

/****************************************************************************
  Name          : immd_immnd_info_node_find_add
  Description   : This routine adds the new node to immnd_tree.
  Arguments     : immnd_tree - IMMND Tree.
		  immnd_info_node -  IMMND Node.
  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE
*****************************************************************************/
uint32_t immd_immnd_info_node_find_add(NCS_PATRICIA_TREE *immnd_tree,
				       MDS_DEST *dest,
				       IMMD_IMMND_INFO_NODE **immnd_info_node,
				       bool *add_flag)
{
	NODE_ID key;

	memset(&key, 0, sizeof(NODE_ID));
	/* Fill the Key */
	key = m_NCS_NODE_ID_FROM_MDS_DEST((*dest));

	*immnd_info_node = (IMMD_IMMND_INFO_NODE *)ncs_patricia_tree_get(
	    immnd_tree, (uint8_t *)&key);
	if ((*immnd_info_node == NULL) && (*add_flag == true)) {
		*immnd_info_node = calloc(1, sizeof(IMMD_IMMND_INFO_NODE));
		if (*immnd_info_node == NULL) {
			LOG_ER("IMMD - calloc returned NULL");
			return NCSCC_RC_FAILURE;
		}

		(*immnd_info_node)->immnd_dest = *dest;
		(*immnd_info_node)->immnd_key =
		    m_NCS_NODE_ID_FROM_MDS_DEST((*dest));
		(*immnd_info_node)->patnode.key_info =
		    (uint8_t *)&((*immnd_info_node)->immnd_key);

		if (ncs_patricia_tree_add(immnd_tree,
					  &(*immnd_info_node)->patnode) !=
		    NCSCC_RC_SUCCESS) {
			LOG_ER("ncs_patricia_tree_add FAILED");
			free(*immnd_info_node);
			*immnd_info_node = NULL;
			return NCSCC_RC_FAILURE;
		}
		*add_flag = false;
	}

	return NCSCC_RC_SUCCESS;
}

/****************************************************************************
  Name          : immd_immnd_info_node_delete
  Description   : This routine deletes the immnd_info node from tree
  Arguments     : IMMD_CB *cb - IMMD Control Block.
		: IMMD_IMMND_INFO_NODE *immnd_info - IMMND Info Node.
  Return Values : None
*****************************************************************************/
uint32_t immd_immnd_info_node_delete(IMMD_CB *cb,
				     IMMD_IMMND_INFO_NODE *immnd_info_node)
{
	uint32_t rc = NCSCC_RC_SUCCESS;

	/* Remove the Node from the client tree */
	if (ncs_patricia_tree_del(&cb->immnd_tree, &immnd_info_node->patnode) !=
	    NCSCC_RC_SUCCESS) {
		LOG_WA("IMMD - IMMND INFO NODE DELETE FROM PAT TREE FAILED");
		rc = NCSCC_RC_FAILURE;
	}

	/* Free the Client Node */
	if (immnd_info_node) {
		free(immnd_info_node);
	}

	return rc;
}

/****************************************************************************
  Name          : immd_immnd_info_tree_cleanup
  Description   : This routine Free all the nodes in immnd_tree.
  Arguments     : IMMD_CB *cb - IMMD Control Block.
  Return Values : None
****************************************************************************/
void immd_immnd_info_tree_cleanup(IMMD_CB *cb)
{
	IMMD_IMMND_INFO_NODE *immnd_info_node;
	NODE_ID key;

	memset(&key, 0, sizeof(NODE_ID));

	/* Get the First Node */
	immnd_info_node = (IMMD_IMMND_INFO_NODE *)ncs_patricia_tree_getnext(
	    &cb->immnd_tree, (uint8_t *)&key);
	while (immnd_info_node) {
		key = m_NCS_NODE_ID_FROM_MDS_DEST(immnd_info_node->immnd_dest);

		immd_immnd_info_node_delete(cb, immnd_info_node);

		immnd_info_node =
		    (IMMD_IMMND_INFO_NODE *)ncs_patricia_tree_getnext(
			&cb->immnd_tree, (uint8_t *)&key);
	}

	return;
}

/****************************************************************************
  Name          : immd_immnd_info_tree_destroy
  Description   : This routine destroys the IMMD lcl ckpt tree.
  Arguments     : IMMD_CB *cb - IMMD Control Block.
  Return Values : None
*****************************************************************************/
void immd_immnd_info_tree_destroy(IMMD_CB *cb)
{
	if (!cb->is_immnd_tree_up)
		return;

	/* cleanup the client tree */
	immd_immnd_info_tree_cleanup(cb);

	/* destroy the tree */
	ncs_patricia_tree_destroy(&cb->immnd_tree);

	return;
}

/****************************************************************************
 * Name          : immd_cb_db_init
 *
 * Description   : This is the function which initializes all the data
 *                 structures and locks used belongs to IMMD.
 *
 * Arguments     : cb  - IMMD control block pointer.
 *
 * Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE.
 *****************************************************************************/
uint32_t immd_cb_db_init(IMMD_CB *cb)
{
	uint32_t rc = immd_immnd_info_tree_init(cb);
	if (rc != NCSCC_RC_SUCCESS) {
		LOG_ER("IMMD - IMMND INFO TREE INIT FAILED");
		return rc;
	}

	cb->mRim = SA_IMM_INIT_FROM_FILE;
	cb->m2PbeCanLoad = !cb->mIs2Pbe;
	if ((cb->ha_state == SA_AMF_HA_ACTIVE) && !cb->mScAbsenceAllowed) {
		cb->is_loading = true;
	}

	cb->fully_initialized = false;
	cb->mbcsv_sel_obj = -1;
	cb->usr1_sel_obj.raise_obj = -1;
	cb->usr1_sel_obj.rmv_obj = -1;
	cb->amf_sel_obj = -1;

	return (NCSCC_RC_SUCCESS);
}

/****************************************************************************
 * Name          : immd_cb_db_destroy
 *
 * Description   : Destoroy the databases in CB
 *
 * Arguments     : cb  - Ifsv control block pointer.
 *
 * Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE.
 *
 * Notes         : None.
 ****************************************************************************/
uint32_t immd_cb_db_destroy(IMMD_CB *cb)
{
	immd_immnd_info_tree_destroy(cb);
	return NCSCC_RC_SUCCESS;
}

/*****************************************************************************
 Name    :  immd_get_slot_and_subslot_id_from_mds_dest

 Description :  To get the physical slot & subslot unique  id from the mds_dest

 Arguments   :
*****************************************************************************/
uint32_t immd_get_slot_and_subslot_id_from_mds_dest(MDS_DEST dest)
{
	return GetSlotSubslotIdFromNodeId(m_NCS_NODE_ID_FROM_MDS_DEST(dest));
}

/*****************************************************************************
 Name    :  immd_get_slot_and_subslot_id_from_node_id

 Description :  To get the physical slot & sbuslot unique  id from the node id

 Arguments   :
*****************************************************************************/
uint32_t immd_get_slot_and_subslot_id_from_node_id(NCS_NODE_ID node_id)
{

	return GetSlotSubslotIdFromNodeId(node_id);
}

void immd_db_save_fevs(IMMD_CB *cb, IMMSV_FEVS *fevs_msg)
{
	IMMD_SAVED_FEVS_MSG *new_msg = calloc(1, sizeof(IMMD_SAVED_FEVS_MSG));
	TRACE_ENTER();
	osafassert(new_msg);
	/*new_msg->fevsMsg = *fevs_msg; */
	new_msg->fevsMsg.sender_count = fevs_msg->sender_count;
	new_msg->fevsMsg.reply_dest = fevs_msg->reply_dest;
	new_msg->fevsMsg.client_hdl = fevs_msg->client_hdl;
	new_msg->fevsMsg.msg.size = fevs_msg->msg.size;
	new_msg->fevsMsg.msg.buf = fevs_msg->msg.buf;
	fevs_msg->msg.buf = NULL; /* steal the message */
	fevs_msg->msg.size = 0;

	IMMD_SAVED_FEVS_MSG *prior = cb->saved_msgs;
	if (prior) {
		uint16_t nrof_msgs = 1;
		while (prior->next) {
			++nrof_msgs;
			prior = prior->next;
		}
		++nrof_msgs;
		TRACE_5("%u'th message added. Message no %llu", nrof_msgs,
			new_msg->fevsMsg.sender_count);
		prior->next = new_msg;
		if (nrof_msgs > IMMD_MBCSV_MAX_MSG_CNT) {
			/* Discard oldest message */
			prior = cb->saved_msgs;
			cb->saved_msgs = cb->saved_msgs->next;
			TRACE_5("Message no %llu discarded",
				prior->fevsMsg.sender_count);
			free(prior->fevsMsg.msg.buf);
			prior->fevsMsg.msg.buf = NULL;
			prior->fevsMsg.msg.size = 0;
			prior->next = NULL;
			prior->re_sent = false;
			free(prior);
		}
	} else {
		/* first time */
		TRACE_5("First message no %llu", new_msg->fevsMsg.sender_count);
		cb->saved_msgs = new_msg;
	}
	TRACE_LEAVE();
}

IMMSV_FEVS *immd_db_get_fevs(IMMD_CB *cb, const uint16_t back_count)
{
	uint16_t ix = 0;
	IMMD_SAVED_FEVS_MSG *old_msgs[back_count];
	for (; ix < back_count; ++ix) {
		old_msgs[ix] = NULL;
	}

	IMMD_SAVED_FEVS_MSG *old_msg = cb->saved_msgs;

	while (old_msg) {
		for (ix = 0; ix < (back_count - 1); ++ix) {
			old_msgs[ix] = old_msgs[ix + 1];
		}
		old_msgs[back_count - 1] = old_msg;
		old_msg = old_msg->next;
	}

	if (old_msgs[0]) {
		if (old_msgs[0]->re_sent) {
			LOG_NO(
			    "Skipping re-send of fevs message %llu since it has "
			    "recently been resent.",
			    old_msgs[0]->fevsMsg.sender_count);
			return NULL;
		}
		old_msgs[0]->re_sent = true;
		return &(old_msgs[0]->fevsMsg);
	} else {
		return NULL;
	}
}

void immd_db_purge_fevs(IMMD_CB *cb)
{
	TRACE_ENTER();
	while (cb->saved_msgs) {
		IMMD_SAVED_FEVS_MSG *old = cb->saved_msgs;
		cb->saved_msgs = cb->saved_msgs->next;
		TRACE_5("Message no %llu discarded", old->fevsMsg.sender_count);
		free(old->fevsMsg.msg.buf);
		old->fevsMsg.msg.buf = NULL;
		old->fevsMsg.msg.size = 0;
		old->next = NULL;
		old->re_sent = false;
		free(old);
	}
	TRACE_LEAVE();
}

uint32_t immd_immnd_info_node_cardinality(NCS_PATRICIA_TREE *immnd_tree)
{
	return ncs_patricia_tree_size(immnd_tree);
}
