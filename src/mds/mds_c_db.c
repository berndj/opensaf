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

  DESCRIPTION:  Database for MCM

******************************************************************************
*/

#include <inttypes.h>
#include "mds_core.h"
#include "mds_log.h"
#include "base/ncs_main_papi.h"
extern bool tipc_mode_enabled;
extern uint32_t mds_mcm_check_intranode(MDS_DEST adest);
/*****************************************************
Function NAME: get_adest_details()
Returns : <node[nodeid]:processname[pid]>
*****************************************************/
void get_adest_details(MDS_DEST adest, char *adest_details)
{
	char *token, *saveptr;
	struct stat s;
	uint32_t process_id = 0;
	char pid_path[1024];
	char *pid_name = NULL;
	char process_name[MDS_MAX_PROCESS_NAME_LEN];
	bool remote = false;

	memset(adest_details, 0, MDS_MAX_PROCESS_NAME_LEN);
	memset(process_name, 0, MDS_MAX_PROCESS_NAME_LEN);
	memset(pid_path, 0, 1024);

	NCS_NODE_ID ncs_node_id = m_NCS_NODE_ID_FROM_MDS_DEST(adest);

	if (!tipc_mode_enabled) {
		process_id = m_MDS_GET_PROCESS_ID_FROM_ADEST(adest);
		if (NCSCC_RC_SUCCESS == mds_mcm_check_intranode(adest)) {
			sprintf(pid_path, "/proc/%d/cmdline", process_id);
			if (stat(pid_path, &s) != 0) {
				snprintf(process_name, MDS_MAX_PROCESS_NAME_LEN,
					 "pid[%d]", process_id);
			} else {

				FILE *f = fopen(pid_path, "r");
				if (f) {
					size_t size;
					size = fread(pid_path, sizeof(char),
						     1024, f);
					if (size > 0) {
						if ('\n' == pid_path[size - 1])
							pid_path[size - 1] =
							    '\0';
					}
					fclose(f);
				}
				token = strtok_r(pid_path, "/", &saveptr);
				while (token != NULL) {
					pid_name = token;
					token = strtok_r(NULL, "/", &saveptr);
				}
				snprintf(process_name, MDS_MAX_PROCESS_NAME_LEN,
					 "%s[%d]", pid_name, process_id);
			}

		} else {
			snprintf(process_name, MDS_MAX_PROCESS_NAME_LEN,
				 "dest_pid[%d]", process_id);
			remote = true;
		}
	} else if (tipc_mode_enabled) {
		process_id = getpid();
		if (NCSCC_RC_SUCCESS == mds_mcm_check_intranode(adest)) {
			sprintf(pid_path, "/proc/%d/cmdline", process_id);
			if (stat(pid_path, &s) != 0) {
				snprintf(process_name, MDS_MAX_PROCESS_NAME_LEN,
					 "tipc_id_ref[%d]", process_id);
			} else {

				FILE *f = fopen(pid_path, "r");
				if (f) {
					size_t size;
					size = fread(pid_path, sizeof(char),
						     1024, f);
					if (size > 0) {
						if ('\n' == pid_path[size - 1])
							pid_path[size - 1] =
							    '\0';
					}
					fclose(f);
				}
				token = strtok_r(pid_path, "/", &saveptr);
				while (token != NULL) {
					pid_name = token;
					token = strtok_r(NULL, "/", &saveptr);
				}
				snprintf(process_name, MDS_MAX_PROCESS_NAME_LEN,
					 "%s[%d]", pid_name, process_id);
			}

		} else {

			process_id = m_MDS_GET_PROCESS_ID_FROM_ADEST(adest);
			snprintf(process_name, MDS_MAX_PROCESS_NAME_LEN,
				 "dest_tipc_id_ref[%d]", process_id);
			remote = true;
		}
	}

	if (remote == true)
		snprintf(adest_details, MDS_MAX_PROCESS_NAME_LEN,
			 "<rem_nodeid[0x%" PRIx32 "]:%s>", ncs_node_id, process_name);
	else
		snprintf(adest_details, MDS_MAX_PROCESS_NAME_LEN,
			 "<nodeid[0x%" PRIx32 "]:%s>", ncs_node_id, process_name);

	m_MDS_LOG_DBG("MDS:DB: adest_details: %s ", adest_details);
	m_MDS_LEAVE();
}

/*****************************************************
  Function NAME: get_subtn_adest_details
  Returns : <node[nodeid]:processname[pid]>
 *****************************************************/
void get_subtn_adest_details(MDS_PWE_HDL pwe_hdl, MDS_SVC_ID svc_id,
			     MDS_DEST adest, char *adest_details)
{
	uint32_t process_id = 0;
	char process_name[MDS_MAX_PROCESS_NAME_LEN];
	bool remote = false;
	MDS_SVC_INFO *svc_info = NULL;
	char pid_path[1024];
	char *token, *saveptr;
	char *pid_name = NULL;
	struct stat s;

	memset(process_name, 0, MDS_MAX_PROCESS_NAME_LEN);
	memset(pid_path, 0, 1024);

	NCS_NODE_ID ncs_node_id = m_NCS_NODE_ID_FROM_MDS_DEST(adest);
	process_id = m_MDS_GET_PROCESS_ID_FROM_ADEST(adest);

	if (NCSCC_RC_SUCCESS == mds_mcm_check_intranode(adest)) {
		if (NCSCC_RC_SUCCESS ==
		    mds_svc_tbl_get(pwe_hdl, svc_id, (NCSCONTEXT)&svc_info)) {
			strcpy(adest_details, svc_info->adest_details);
			goto done;
		} else if (!tipc_mode_enabled) {

			sprintf(pid_path, "/proc/%d/cmdline", process_id);
			if (stat(pid_path, &s) != 0) {
				snprintf(process_name, MDS_MAX_PROCESS_NAME_LEN,
					 "pid[%u]", process_id);
			} else {

				FILE *f = fopen(pid_path, "r");
				if (f) {
					size_t size;
					size = fread(pid_path, sizeof(char),
						     1024, f);
					if (size > 0) {
						if ('\n' == pid_path[size - 1])
							pid_path[size - 1] =
							    '\0';
					}
					fclose(f);
				}
				token = strtok_r(pid_path, "/", &saveptr);
				while (token != NULL) {
					pid_name = token;
					token = strtok_r(NULL, "/", &saveptr);
				}
				snprintf(process_name, MDS_MAX_PROCESS_NAME_LEN,
					 "%s[%u]", pid_name, process_id);
			}
		} else {
			/* Service Doesn't exist */
			snprintf(process_name, MDS_MAX_PROCESS_NAME_LEN,
				 "tipc_id_ref[%u]", process_id);
		}
	} else {
		if (!tipc_mode_enabled) {
			snprintf(process_name, MDS_MAX_PROCESS_NAME_LEN,
				 "dest_pid[%u]", process_id);
			remote = true;
		} else {
			snprintf(process_name, MDS_MAX_PROCESS_NAME_LEN,
				 "dest_tipc_id_ref[%u]", process_id);
			remote = true;
		}
	}

	if (remote == true)
		snprintf(adest_details, MDS_MAX_PROCESS_NAME_LEN,
			 "<rem_node[0x%" PRIx32 "]:%s>", ncs_node_id, process_name);
	else
		snprintf(adest_details, MDS_MAX_PROCESS_NAME_LEN,
			 "<node[0x%" PRIx32 "]:%s>", ncs_node_id, process_name);
done:
	m_MDS_LOG_DBG("MDS:DB: adest_details: %s ", adest_details);
	m_MDS_LEAVE();
}

/* ******************************************** */
/* ******************************************** */
/*            VDEST TABLE Operations            */
/* ******************************************** */
/* ******************************************** */

/*********************************************************
  Function NAME: mds_vdest_tbl_add
*********************************************************/

uint32_t mds_vdest_tbl_add(MDS_VDEST_ID vdest_id, NCS_VDEST_TYPE policy,
			   MDS_VDEST_HDL *vdest_hdl)
{
	MDS_VDEST_INFO *vdest_info;

	m_MDS_ENTER();
	/* Check if vdest is not already created */
	if (ncs_patricia_tree_get(&gl_mds_mcm_cb->vdest_list,
				  (uint8_t *)&vdest_id) != NULL) {
		m_MDS_LEAVE();
		return NCSCC_RC_FAILURE;
	}

	vdest_info = m_MMGR_ALLOC_VDEST_INFO;
	memset(vdest_info, 0, sizeof(MDS_VDEST_INFO));

	vdest_info->vdest_id = vdest_id;
	vdest_info->policy = policy;
	vdest_info->role = V_DEST_RL_STANDBY;

	/* Create Quiesced timer */
	m_NCS_TMR_CREATE(vdest_info->quiesced_cbk_tmr, MDS_QUIESCED_TMR_VAL,
			 mds_tmr_callback, (void *)NULL);

	m_MDS_LOG_DBG("Quiescedcbk tmr=0x%08x",
		      *(unsigned int *)vdest_info->quiesced_cbk_tmr);
	vdest_info->node.key_info = (uint8_t *)&vdest_info->vdest_id;

	ncs_patricia_tree_add(&gl_mds_mcm_cb->vdest_list,
			      (NCS_PATRICIA_NODE *)&vdest_info->node);

	*vdest_hdl = m_MDS_GET_VDEST_HDL_FROM_VDEST_ID(vdest_id);
	m_MDS_LEAVE();
	return NCSCC_RC_SUCCESS;
}

/*********************************************************
  Function NAME: mds_vdest_tbl_del
*********************************************************/
uint32_t mds_vdest_tbl_del(MDS_VDEST_ID vdest_id)
{
	MDS_VDEST_INFO *vdest_info = NULL;

	m_MDS_ENTER();
	/* Check if vdest is already created */
	vdest_info = (MDS_VDEST_INFO *)ncs_patricia_tree_get(
	    &gl_mds_mcm_cb->vdest_list, (uint8_t *)&vdest_id);
	if (vdest_info == NULL) {
		m_MDS_LOG_DBG("MDS:DB:  VDEST not present");
		m_MDS_LEAVE();
		return NCSCC_RC_FAILURE;
	} else {
		/* If timer is running free TMR_REQ_INFO */
		if (vdest_info->tmr_running == true) {
			/* Delete Handle */
			ncshm_destroy_hdl(
			    NCS_SERVICE_ID_COMMON,
			    (uint32_t)vdest_info->tmr_req_info_hdl);
			/* Free tmr_req_info */
			m_MMGR_FREE_TMR_INFO(vdest_info->tmr_req_info);
			vdest_info->tmr_req_info = NULL;
		}

		/* Destroy timer */
		m_NCS_TMR_DESTROY(vdest_info->quiesced_cbk_tmr);

		ncs_patricia_tree_del(&gl_mds_mcm_cb->vdest_list,
				      (NCS_PATRICIA_NODE *)vdest_info);

		/* Free memory of VDEST_INFO */
		m_MMGR_FREE_VDEST_INFO(vdest_info);
	}
	m_MDS_LEAVE();
	return NCSCC_RC_SUCCESS;
}

/*********************************************************
  Function NAME: mds_vdest_tbl_update_role
*********************************************************/
uint32_t mds_vdest_tbl_update_role(MDS_VDEST_ID vdest_id, V_DEST_RL role,
				   bool del_tmr_info)
{
	MDS_VDEST_INFO *vdest_info = NULL;
	MDS_TMR_REQ_INFO *tmr_req_info = NULL;

	m_MDS_ENTER();
	/* Check if vdest is already created */
	vdest_info = (MDS_VDEST_INFO *)ncs_patricia_tree_get(
	    &gl_mds_mcm_cb->vdest_list, (uint8_t *)&vdest_id);
	if (vdest_info == NULL) {
		m_MDS_LOG_DBG("MDS:DB: VDEST not present");
		m_MDS_LEAVE();
		return NCSCC_RC_FAILURE;
	} else {
		if (role == V_DEST_RL_QUIESCED) { /* New role is quiesced */

			/* Start Quiesced timer */
			vdest_info->tmr_running = true;

			tmr_req_info = m_MMGR_ALLOC_TMR_INFO;
			memset(tmr_req_info, 0, sizeof(MDS_TMR_REQ_INFO));
			tmr_req_info->type = MDS_QUIESCED_TMR;
			tmr_req_info->info.quiesced_tmr_info.vdest_id =
			    vdest_id;

			vdest_info->tmr_req_info = tmr_req_info;
			vdest_info->tmr_req_info_hdl = ncshm_create_hdl(
			    NCS_HM_POOL_ID_COMMON, NCS_SERVICE_ID_COMMON,
			    (NCSCONTEXT)tmr_req_info);
			m_NCS_TMR_START(
			    vdest_info->quiesced_cbk_tmr, MDS_QUIESCED_TMR_VAL,
			    (TMR_CALLBACK)mds_tmr_callback,
			    (void *)(long)vdest_info->tmr_req_info_hdl);
			m_MDS_LOG_DBG(
			    "MCM_DB:UpdateRole:TimerStart:Quiesced:Hdl=0x%08x:Vdest=%d\n",
			    vdest_info->tmr_req_info_hdl, vdest_id);

		} else if (vdest_info->role ==
			   V_DEST_RL_QUIESCED) { /* Old role is quiesced */

			if (del_tmr_info == true) {
				/* This is not called by the expiry function */
				/* So free tmr_info and stop timer */
				ncshm_destroy_hdl(
				    NCS_SERVICE_ID_COMMON,
				    (uint32_t)vdest_info->tmr_req_info_hdl);
				m_NCS_TMR_STOP(vdest_info->quiesced_cbk_tmr);
				m_MMGR_FREE_TMR_INFO(vdest_info->tmr_req_info);
			}
			/* Stop timer and free tmr_req_info */
			vdest_info->tmr_running = false;
			vdest_info->tmr_req_info = NULL;
		}
		/* Update new role */
		vdest_info->role = role;
	}
	m_MDS_LEAVE();
	return NCSCC_RC_SUCCESS;
}

/*********************************************************
  Function NAME: mds_vdest_tbl_update_ref_val
*********************************************************/
uint32_t mds_vdest_tbl_update_ref_val(MDS_VDEST_ID vdest_id,
				      MDS_SUBTN_REF_VAL subtn_ref_val)
{
	MDS_VDEST_INFO *vdest_info = NULL;

	m_MDS_ENTER();
	/* Check if vdest is already created */
	vdest_info = (MDS_VDEST_INFO *)ncs_patricia_tree_get(
	    &gl_mds_mcm_cb->vdest_list, (uint8_t *)&vdest_id);
	if (vdest_info == NULL) {
		m_MDS_LOG_DBG("MDS:DB: VDEST not present");
		m_MDS_LEAVE();
		return NCSCC_RC_FAILURE;
	} else {
		vdest_info->subtn_ref_val = subtn_ref_val;
	}
	m_MDS_LEAVE();
	return NCSCC_RC_SUCCESS;
}

/*********************************************************
  Function NAME: mds_vdest_tbl_query
*********************************************************/
uint32_t mds_vdest_tbl_query(MDS_VDEST_ID vdest_id)
{
	MDS_VDEST_INFO *vdest_info = NULL;

	m_MDS_ENTER();
	/* Check if vdest is already created */
	vdest_info = (MDS_VDEST_INFO *)ncs_patricia_tree_get(
	    &gl_mds_mcm_cb->vdest_list, (uint8_t *)&vdest_id);
	if (vdest_info == NULL) {
		m_MDS_LOG_DBG("MDS:DB:  VDEST not present");
		m_MDS_LEAVE();
		return NCSCC_RC_FAILURE;
	} else {
		m_MDS_LEAVE();
		return NCSCC_RC_SUCCESS;
	}
}

/*********************************************************
  Function NAME: mds_vdest_tbl_get_role
*********************************************************/
uint32_t mds_vdest_tbl_get_role(MDS_VDEST_ID vdest_id, V_DEST_RL *role)
{
	MDS_VDEST_INFO *vdest_info = NULL;

	m_MDS_ENTER();
	/* Check if vdest is already created */
	vdest_info = (MDS_VDEST_INFO *)ncs_patricia_tree_get(
	    &gl_mds_mcm_cb->vdest_list, (uint8_t *)&vdest_id);
	if (vdest_info == NULL) {
		m_MDS_LOG_DBG("MDS:DB: VDEST not present");
		m_MDS_LEAVE();
		return NCSCC_RC_FAILURE;
	} else {
		*role = vdest_info->role;
	}
	m_MDS_LEAVE();
	return NCSCC_RC_SUCCESS;
}

/*********************************************************
  Function NAME: mds_vdest_tbl_get_policy
*********************************************************/
uint32_t mds_vdest_tbl_get_policy(MDS_VDEST_ID vdest_id, NCS_VDEST_TYPE *policy)
{
	MDS_VDEST_INFO *vdest_info = NULL;

	m_MDS_ENTER();
	/* Check if vdest is already created */
	vdest_info = (MDS_VDEST_INFO *)ncs_patricia_tree_get(
	    &gl_mds_mcm_cb->vdest_list, (uint8_t *)&vdest_id);
	if (vdest_info == NULL) {
		m_MDS_LOG_DBG("MDS:DB: VDEST not present");
		m_MDS_LEAVE();
		return NCSCC_RC_FAILURE;
	} else {
		*policy = vdest_info->policy;
	}
	m_MDS_LEAVE();
	return NCSCC_RC_SUCCESS;
}

/*********************************************************
  Function NAME: mds_vdest_tbl_get_first
*********************************************************/
uint32_t mds_vdest_tbl_get_first(MDS_VDEST_ID vdest_id,
				 MDS_PWE_HDL *first_pwe_hdl)
{
	MDS_VDEST_INFO *vdest_info = NULL;

	m_MDS_ENTER();
	/* Check if vdest is already created */
	vdest_info = (MDS_VDEST_INFO *)ncs_patricia_tree_get(
	    &gl_mds_mcm_cb->vdest_list, (uint8_t *)&vdest_id);
	if (vdest_info == NULL) {
		m_MDS_LOG_DBG("MDS:DB: VDEST not present");
		m_MDS_LEAVE();
		return NCSCC_RC_FAILURE;
	} else {
		if (vdest_info->pwe_list == NULL) {
			*first_pwe_hdl = (MDS_PWE_HDL)(long)NULL;
			m_MDS_LOG_DBG(
			    "MDS:DB: VDEST present but no PWE on this VDEST");
			m_MDS_LEAVE();
			return NCSCC_RC_FAILURE;
		} else {
			*first_pwe_hdl =
			    m_MDS_GET_PWE_HDL_FROM_PWE_ID_AND_VDEST_ID(
				vdest_id, vdest_info->pwe_list->pwe_id);
			m_MDS_LEAVE();
			return NCSCC_RC_SUCCESS;
		}
	}
}

/*********************************************************
  Function NAME: mds_vdest_tbl_get_vdest_hdl
*********************************************************/
uint32_t mds_vdest_tbl_get_vdest_hdl(MDS_VDEST_ID vdest_id,
				     MDS_VDEST_HDL *vdest_hdl)
{
	m_MDS_ENTER();
	*vdest_hdl = m_MDS_GET_VDEST_HDL_FROM_VDEST_ID(vdest_id);
	m_MDS_LEAVE();
	return NCSCC_RC_SUCCESS;
}

/*********************************************************
  Function NAME: mds_vdest_tbl_get_subtn_ref_val
*********************************************************/
uint32_t mds_vdest_tbl_get_subtn_ref_val(MDS_VDEST_ID vdest_id,
					 MDS_SUBTN_REF_VAL *subtn_ref_val)
{
	MDS_VDEST_INFO *vdest_info = NULL;

	m_MDS_ENTER();
	/* Check if vdest is already created */
	vdest_info = (MDS_VDEST_INFO *)ncs_patricia_tree_get(
	    &gl_mds_mcm_cb->vdest_list, (uint8_t *)&vdest_id);
	if (vdest_info == NULL) {
		m_MDS_LOG_DBG("MDS:DB: VDEST not present");
		m_MDS_LEAVE();
		return NCSCC_RC_FAILURE;
	} else {
		*subtn_ref_val = vdest_info->subtn_ref_val;
	}
	m_MDS_LEAVE();
	return NCSCC_RC_SUCCESS;
}

/*********************************************************
  Function NAME: mds_vdest_tbl_get_vdest_info_cb
*********************************************************/
uint32_t mds_vdest_tbl_get_vdest_info_cb(MDS_VDEST_ID vdest_id,
					 MDS_VDEST_INFO **vdest_info)
{
	m_MDS_ENTER();
	*vdest_info = (MDS_VDEST_INFO *)ncs_patricia_tree_get(
	    &gl_mds_mcm_cb->vdest_list, (uint8_t *)&vdest_id);
	m_MDS_LEAVE();
	return NCSCC_RC_SUCCESS;
}

/*********************************************************
  Function NAME: mds_vdest_tbl_cleanup
*********************************************************/
uint32_t mds_vdest_tbl_cleanup(void)
{
	MDS_VDEST_INFO *vdest_info = NULL;
	MDS_VDEST_ID vdest_id = 0;
	MDS_PWE_INFO *temp_pwe_info = NULL;
	m_MDS_ENTER();
	/* Check if vdest is already created */
	vdest_info = (MDS_VDEST_INFO *)ncs_patricia_tree_getnext(
	    &gl_mds_mcm_cb->vdest_list, (uint8_t *)&vdest_id);
	while (vdest_info != NULL) {
		/* If timer is running free TMR_REQ_INFO */
		if (vdest_info->tmr_running == true) {
			/* Delete Handle */
			ncshm_destroy_hdl(
			    NCS_SERVICE_ID_COMMON,
			    (uint32_t)vdest_info->tmr_req_info_hdl);

			/* Stop timer */
			m_NCS_TMR_STOP(vdest_info->quiesced_cbk_tmr);

			/* Free tmr_req_info */
			m_MMGR_FREE_TMR_INFO(vdest_info->tmr_req_info);
			vdest_info->tmr_req_info = NULL;
		}

		/* Destroy timer */
		m_NCS_TMR_DESTROY(vdest_info->quiesced_cbk_tmr);

		/* Free pwe list */
		while (vdest_info->pwe_list != NULL) {
			temp_pwe_info = vdest_info->pwe_list;
			vdest_info->pwe_list = vdest_info->pwe_list->next_pwe;
			m_MMGR_FREE_PWE_INFO(temp_pwe_info);
		}

		/* Store current vdest_id for getting next entry in the patricia
		 * tree */
		vdest_id = vdest_info->vdest_id;

		/* Delete from tree */
		ncs_patricia_tree_del(&gl_mds_mcm_cb->vdest_list,
				      (NCS_PATRICIA_NODE *)vdest_info);

		/* Free memory of VDEST_INFO */
		m_MMGR_FREE_VDEST_INFO(vdest_info);

		vdest_info = (MDS_VDEST_INFO *)ncs_patricia_tree_getnext(
		    &gl_mds_mcm_cb->vdest_list, (uint8_t *)&vdest_id);
	}
	m_MDS_LEAVE();
	return NCSCC_RC_SUCCESS;
}

/* ******************************************** */
/* ******************************************** */
/*            PWE  TABLE Operations             */
/* ******************************************** */
/* ******************************************** */

/*********************************************************
  Function NAME: mds_pwe_tbl_add
*********************************************************/
uint32_t mds_pwe_tbl_add(MDS_VDEST_HDL vdest_hdl, PW_ENV_ID pwe_id,
			 MDS_PWE_HDL *pwe_hdl)
{
	MDS_VDEST_INFO *vdest_info = NULL;
	MDS_VDEST_ID vdest_id;
	MDS_PWE_INFO *new_pwe_info = NULL;

	m_MDS_ENTER();
	vdest_id = m_MDS_GET_VDEST_ID_FROM_VDEST_HDL(vdest_hdl);

	/* Check if vdest is already created */
	vdest_info = (MDS_VDEST_INFO *)ncs_patricia_tree_get(
	    &gl_mds_mcm_cb->vdest_list, (uint8_t *)&vdest_id);
	if (vdest_info == NULL) {
		/* VDEST doesn't exist */
		m_MDS_LOG_DBG("MDS:DB: VDEST not present");
		m_MDS_LEAVE();
		return NCSCC_RC_FAILURE;
	} else {
		if (mds_pwe_tbl_query(vdest_hdl, pwe_id) == NCSCC_RC_SUCCESS) {
			/* PWE entry already exist */
			m_MDS_LOG_DBG(
			    "MDS:DB:  VDEST present, but PWE on this VDEST not present");
			m_MDS_LEAVE();
			return NCSCC_RC_FAILURE;
		} else {
			/* PWE entry doesn't exist, so add */
			new_pwe_info = m_MMGR_ALLOC_PWE_INFO;
			memset(new_pwe_info, 0, sizeof(MDS_PWE_INFO));

			new_pwe_info->pwe_id = pwe_id;
			new_pwe_info->parent_vdest = vdest_info;
			new_pwe_info->next_pwe = vdest_info->pwe_list;
			vdest_info->pwe_list = new_pwe_info;
			*pwe_hdl = m_MDS_GET_PWE_HDL_FROM_PWE_ID_AND_VDEST_ID(
			    pwe_id, vdest_id);
		}
	}
	m_MDS_LEAVE();
	return NCSCC_RC_SUCCESS;
}

/*********************************************************
  Function NAME: mds_pwe_tbl_del
*********************************************************/
uint32_t mds_pwe_tbl_del(MDS_PWE_HDL pwe_hdl)
{ /* mds_hdl is stored as back ptr in PWE_INFO */
	MDS_VDEST_INFO *vdest_info = NULL;
	MDS_VDEST_ID vdest_id;
	MDS_PWE_ID pwe_id;
	MDS_VDEST_HDL vdest_hdl;
	MDS_PWE_INFO *temp_current_pwe_info = NULL;
	MDS_PWE_INFO *temp_previous_pwe_info = NULL;

	m_MDS_ENTER();
	vdest_id = m_MDS_GET_VDEST_ID_FROM_PWE_HDL(pwe_hdl);
	pwe_id = m_MDS_GET_PWE_ID_FROM_PWE_HDL(pwe_hdl);
	vdest_hdl = m_MDS_GET_VDEST_HDL_FROM_VDEST_ID(vdest_id);

	vdest_info = (MDS_VDEST_INFO *)ncs_patricia_tree_get(
	    &gl_mds_mcm_cb->vdest_list, (uint8_t *)&vdest_id);

	/* Check if vdest is already created */

	if (mds_pwe_tbl_query(vdest_hdl, pwe_id) == NCSCC_RC_FAILURE) {
		/* PWE entry doesn't exist */
		m_MDS_LOG_DBG("MDS:DB: PWE not present");
		m_MDS_LEAVE();
		return NCSCC_RC_FAILURE;
	} else {
		/* PWE exists, Go and delete it */
		temp_current_pwe_info = vdest_info->pwe_list;
		temp_previous_pwe_info = vdest_info->pwe_list;

		while (temp_current_pwe_info != NULL) {
			if (temp_current_pwe_info->pwe_id == pwe_id) {
				/* Got it, Delete it */
				if (temp_previous_pwe_info ==
				    temp_current_pwe_info) {
					/* It is the first element */
					vdest_info->pwe_list =
					    temp_current_pwe_info->next_pwe;
				} else {
					temp_previous_pwe_info->next_pwe =
					    temp_current_pwe_info->next_pwe;
				}

				m_MMGR_FREE_PWE_INFO(temp_current_pwe_info);
				m_MDS_LEAVE();
				return NCSCC_RC_SUCCESS;
			}
			temp_previous_pwe_info = temp_current_pwe_info;
			temp_current_pwe_info = temp_current_pwe_info->next_pwe;
		}
		m_MDS_LOG_DBG("MDS:DB: PWE not present");
		m_MDS_LEAVE();
		return NCSCC_RC_FAILURE;
	}
}

/*********************************************************
  Function NAME: mds_pwe_tbl_query
*********************************************************/
uint32_t mds_pwe_tbl_query(MDS_VDEST_HDL vdest_hdl, PW_ENV_ID pwe_id)
{
	MDS_VDEST_INFO *vdest_info = NULL;
	MDS_VDEST_ID vdest_id;
	MDS_PWE_INFO *temp_pwe_info = NULL;

	m_MDS_ENTER();
	vdest_id = m_MDS_GET_VDEST_ID_FROM_VDEST_HDL(vdest_hdl);

	/* Check if vdest is already created */
	vdest_info = (MDS_VDEST_INFO *)ncs_patricia_tree_get(
	    &gl_mds_mcm_cb->vdest_list, (uint8_t *)&vdest_id);

	if (vdest_info == NULL) {
		/* VDEST doesn't exist */
		m_MDS_LOG_DBG("MDS:DB: VDEST not present");
		m_MDS_LEAVE();
		return NCSCC_RC_FAILURE;
	} else {
		/* VDEST exists, so now search for PWE */
		temp_pwe_info = vdest_info->pwe_list;

		while (temp_pwe_info != NULL) {
			if (temp_pwe_info->pwe_id == pwe_id) {
				m_MDS_LEAVE();
				return NCSCC_RC_SUCCESS;
			}
			temp_pwe_info = temp_pwe_info->next_pwe;
		}
		m_MDS_LOG_DBG("MDS:DB: PWE not present");
		m_MDS_LEAVE();
		return NCSCC_RC_FAILURE;
	}
}

/* ******************************************** */
/* ******************************************** */
/*            SVC  TABLE Operations             */
/* ******************************************** */
/* ******************************************** */

/*********************************************************
  Function NAME: mds_svc_tbl_add
*********************************************************/
uint32_t mds_svc_tbl_add(NCSMDS_INFO *info)
{
	MDS_VDEST_HDL vdest_hdl;
	MDS_VDEST_ID vdest_id;
	PW_ENV_ID pwe_id;
	MDS_SVC_INFO *svc_info = NULL;
	MDS_VDEST_INFO *parent_vdest_info;

	m_MDS_ENTER();
	if (mds_svc_tbl_query((MDS_PWE_HDL)info->i_mds_hdl, info->i_svc_id) ==
	    NCSCC_RC_SUCCESS) {
		/* SVC already exist */
		m_MDS_LOG_DBG("MDS:DB: SVC already present");
		m_MDS_LEAVE();
		return NCSCC_RC_FAILURE;
	}

	vdest_hdl = m_MDS_GET_VDEST_HDL_FROM_PWE_HDL(info->i_mds_hdl);
	pwe_id = m_MDS_GET_PWE_ID_FROM_PWE_HDL(info->i_mds_hdl);
	vdest_id = m_MDS_GET_VDEST_ID_FROM_PWE_HDL(info->i_mds_hdl);
	mds_vdest_tbl_get_vdest_info_cb(vdest_id, &parent_vdest_info);

	if (mds_pwe_tbl_query(vdest_hdl, pwe_id) != NCSCC_RC_SUCCESS) {
		/* PWE on which it want to install doesn't exist */
		m_MDS_LOG_DBG("MDS:DB: PWE not present");
		m_MDS_LEAVE();
		return NCSCC_RC_FAILURE;
	}

	svc_info = m_MMGR_ALLOC_SVC_INFO;
	memset(svc_info, 0, sizeof(MDS_SVC_INFO));

	svc_info->svc_hdl = m_MDS_GET_SVC_HDL_FROM_PWE_HDL_AND_SVC_ID(
	    (MDS_PWE_HDL)info->i_mds_hdl, info->i_svc_id);
	svc_info->svc_id = info->i_svc_id;
	svc_info->install_scope = info->info.svc_install.i_install_scope;
	svc_info->cback_ptr = info->info.svc_install.i_svc_cb;
	svc_info->yr_svc_hdl = info->info.svc_install.i_yr_svc_hdl;
	svc_info->q_ownership = info->info.svc_install.i_mds_q_ownership;
	svc_info->parent_vdest_info = parent_vdest_info;
	svc_info->svc_sub_part_ver = info->info.svc_install.i_mds_svc_pvt_ver;
	svc_info->i_fail_no_active_sends =
	    info->info.svc_install.i_fail_no_active_sends;
	svc_info->i_msg_loss_indication =
	    info->info.svc_install.i_msg_loss_indication;

	if (svc_info->q_ownership == 1) {
		if (m_NCS_IPC_CREATE(&svc_info->q_mbx) != NCSCC_RC_SUCCESS) {
			m_MMGR_FREE_SVC_INFO(svc_info);
			m_MDS_LOG_ERR("MDS:DB: Can't Create SVC Mailbox");
			m_MDS_LEAVE();
			return NCSCC_RC_FAILURE;
		}
		m_NCS_IPC_ATTACH(&svc_info->q_mbx);

		/* Output Sel obj */
		info->info.svc_install.o_sel_obj =
		    m_NCS_IPC_GET_SEL_OBJ(&svc_info->q_mbx);
	}

	strcpy(svc_info->adest_details, gl_mds_mcm_cb->adest_details);
	svc_info->svc_list_node.key_info = (uint8_t *)&svc_info->svc_hdl;

	ncs_patricia_tree_add(&gl_mds_mcm_cb->svc_list,
			      (NCS_PATRICIA_NODE *)&svc_info->svc_list_node);

	/* Output parameter */
	if (vdest_id == m_VDEST_ID_FOR_ADEST_ENTRY) {
		info->info.svc_install.o_dest = (gl_mds_mcm_cb->adest);
	} else {
		info->info.svc_install.o_dest = (MDS_DEST)vdest_id;
	}

	info->info.svc_install.o_anc = (V_DEST_QA)(gl_mds_mcm_cb->adest);

	m_MDS_LOG_DBG("MDS:DB: svc_details: %s ", svc_info->adest_details);
	m_MDS_LEAVE();
	return NCSCC_RC_SUCCESS;
}

/*********************************************************
  Function NAME: mds_svc_tbl_del
*********************************************************/
uint32_t mds_svc_tbl_del(MDS_PWE_HDL pwe_hdl, MDS_SVC_ID svc_id,
			 MDS_Q_MSG_FREE_CB msg_free_cb)
{
	/* destroy mbx if q_own is true */
	MDS_SVC_HDL svc_hdl;
	MDS_SVC_INFO *svc_info = NULL;

	m_MDS_ENTER();
	svc_hdl = m_MDS_GET_SVC_HDL_FROM_PWE_HDL_AND_SVC_ID(pwe_hdl, svc_id);

	svc_info = (MDS_SVC_INFO *)ncs_patricia_tree_get(
	    &gl_mds_mcm_cb->svc_list, (uint8_t *)&svc_hdl);
	if (svc_info == NULL) {
		/* Service doesn't exist */
		m_MDS_LOG_DBG("MDS:DB: SVC not present");
		m_MDS_LEAVE();
		return NCSCC_RC_FAILURE;
	} else {
		if (svc_info->q_ownership == 1) {
			m_NCS_IPC_DETACH(&svc_info->q_mbx,
					 (NCS_IPC_CB)msg_free_cb, NULL);
			m_NCS_IPC_RELEASE(&svc_info->q_mbx, NULL);
		}
		ncs_patricia_tree_del(&gl_mds_mcm_cb->svc_list,
				      (NCS_PATRICIA_NODE *)svc_info);
		m_MMGR_FREE_SVC_INFO(svc_info);
	}
	m_MDS_LEAVE();
	return NCSCC_RC_SUCCESS;
}

/*********************************************************
  Function NAME: mds_svc_tbl_query
*********************************************************/
uint32_t mds_svc_tbl_query(MDS_PWE_HDL pwe_hdl, MDS_SVC_ID svc_id)
{
	MDS_SVC_INFO *svc_info;
	MDS_SVC_HDL svc_hdl;

	m_MDS_ENTER();
	svc_hdl = m_MDS_GET_SVC_HDL_FROM_PWE_HDL_AND_SVC_ID(pwe_hdl, svc_id);

	/* Check if svc already exist */
	svc_info = (MDS_SVC_INFO *)ncs_patricia_tree_get(
	    &gl_mds_mcm_cb->svc_list, (uint8_t *)&svc_hdl);
	if (svc_info == NULL) {
		/* service doesn't exist */
		m_MDS_LOG_DBG("MDS:DB: SVC not present");
		m_MDS_LEAVE();
		return NCSCC_RC_FAILURE;
	} else {
		/* service exists */
		m_MDS_LEAVE();
		return NCSCC_RC_SUCCESS;
	}
}

/*********************************************************
  Function NAME: mds_svc_tbl_get
*********************************************************/
uint32_t mds_svc_tbl_get(MDS_PWE_HDL pwe_hdl, MDS_SVC_ID svc_id,
			 NCSCONTEXT *svc_cb)
{
	MDS_SVC_INFO *svc_info;
	MDS_SVC_HDL svc_hdl;

	m_MDS_ENTER();
	svc_hdl = m_MDS_GET_SVC_HDL_FROM_PWE_HDL_AND_SVC_ID(pwe_hdl, svc_id);

	/* Check if svc already exist */
	svc_info = (MDS_SVC_INFO *)ncs_patricia_tree_get(
	    &gl_mds_mcm_cb->svc_list, (uint8_t *)&svc_hdl);
	if (svc_info == NULL) {
		/* service doesn't exist */
		m_MDS_LOG_DBG("MDS:DB: SVC not present");
		m_MDS_LEAVE();
		return NCSCC_RC_FAILURE;
	} else {
		*svc_cb = (NCSCONTEXT)svc_info;
		m_MDS_LEAVE();
		return NCSCC_RC_SUCCESS;
	}
}

/*********************************************************
  Function NAME: mds_svc_tbl_get_install_scope
*********************************************************/
uint32_t mds_svc_tbl_get_install_scope(MDS_PWE_HDL pwe_hdl, MDS_SVC_ID svc_id,
				       NCSMDS_SCOPE_TYPE *install_scope)
{
	MDS_SVC_INFO *svc_info;
	MDS_SVC_HDL svc_hdl;

	m_MDS_ENTER();
	svc_hdl = m_MDS_GET_SVC_HDL_FROM_PWE_HDL_AND_SVC_ID(pwe_hdl, svc_id);

	/* Check if svc already exist */
	svc_info = (MDS_SVC_INFO *)ncs_patricia_tree_get(
	    &gl_mds_mcm_cb->svc_list, (uint8_t *)&svc_hdl);
	if (svc_info == NULL) {
		/* service doesn't exist */
		m_MDS_LOG_DBG("MDS:DB: SVC not present");
		m_MDS_LEAVE();
		return NCSCC_RC_FAILURE;
	} else {
		*install_scope = svc_info->install_scope;
		m_MDS_LEAVE();
		return NCSCC_RC_SUCCESS;
	}
}

/*********************************************************
  Function NAME: mds_svc_tbl_get_svc_hdl
*********************************************************/
uint32_t mds_svc_tbl_get_svc_hdl(MDS_PWE_HDL pwe_hdl, MDS_SVC_ID svc_id,
				 MDS_SVC_HDL *svc_hdl)
{
	uint32_t status = NCSCC_RC_SUCCESS;
	m_MDS_ENTER();
	status = mds_svc_tbl_query(pwe_hdl, svc_id);
	if (status == NCSCC_RC_FAILURE) {
		m_MDS_LOG_DBG("MDS:DB: SVC not present");
		m_MDS_LEAVE();
		return NCSCC_RC_FAILURE;
	} else {
		*svc_hdl =
		    m_MDS_GET_SVC_HDL_FROM_PWE_HDL_AND_SVC_ID(pwe_hdl, svc_id);
		m_MDS_LEAVE();
		return NCSCC_RC_SUCCESS;
	}
}

/*********************************************************
  Function NAME: mds_svc_tbl_get_first_subscription
*********************************************************/
uint32_t
mds_svc_tbl_get_first_subscription(MDS_SVC_HDL svc_hdl,
				   MDS_SUBSCRIPTION_INFO **first_subscription)
{
	MDS_SVC_INFO *svc_info;

	m_MDS_ENTER();
	/* Check if svc already exist */
	svc_info = (MDS_SVC_INFO *)ncs_patricia_tree_get(
	    &gl_mds_mcm_cb->svc_list, (uint8_t *)&svc_hdl);
	if (svc_info == NULL) {
		/* service doesn't exist */
		m_MDS_LOG_DBG("MDS:DB: SVC not present");
		m_MDS_LEAVE();
		return NCSCC_RC_FAILURE;
	} else {
		if (svc_info->subtn_info != NULL) {
			*first_subscription = svc_info->subtn_info;
			m_MDS_LEAVE();
			return NCSCC_RC_SUCCESS;
		} else {
			*first_subscription = NULL;
			m_MDS_LOG_DBG("MDS:DB: No Subscription present");
			m_MDS_LEAVE();
			return NCSCC_RC_FAILURE;
		}
	}
}

/*********************************************************
  Function NAME: mds_svc_tbl_get_role
*********************************************************/
uint32_t mds_svc_tbl_get_role(MDS_SVC_HDL svc_hdl)
{
	uint32_t status = NCSCC_RC_SUCCESS;
	MDS_PWE_HDL pwe_hdl;
	MDS_VDEST_ID vdest_id;
	MDS_SVC_ID svc_id;
	V_DEST_RL vdest_role;

	m_MDS_ENTER();
	pwe_hdl = m_MDS_GET_PWE_HDL_FROM_SVC_HDL(svc_hdl);
	vdest_id = m_MDS_GET_VDEST_ID_FROM_SVC_HDL(svc_hdl);
	svc_id = m_MDS_GET_SVC_ID_FROM_SVC_HDL(svc_hdl);

	status = mds_svc_tbl_query(pwe_hdl, svc_id);
	if (status == NCSCC_RC_FAILURE) {
		m_MDS_LOG_DBG("MDS:DB: SVC not present");
		m_MDS_LEAVE();
		return NCSCC_RC_FAILURE;
	}

	mds_vdest_tbl_get_role(vdest_id, &vdest_role);

	if (vdest_role == V_DEST_RL_ACTIVE ||
	    vdest_role == V_DEST_RL_QUIESCED) {
		m_MDS_LEAVE();
		return NCSCC_RC_SUCCESS;
	} else {
		m_MDS_LOG_DBG("MDS:DB: SVC is in STANDBY");
		m_MDS_LEAVE();
		return NCSCC_RC_FAILURE;
	}
}

/*********************************************************
  Function NAME: mds_svc_tbl_getnext_on_vdest
*********************************************************/
uint32_t mds_svc_tbl_getnext_on_vdest(MDS_VDEST_ID vdest_id,
				      MDS_SVC_HDL current_svc_hdl,
				      MDS_SVC_INFO **svc_info)
{
	MDS_SVC_INFO *temp_svc_info = NULL;

	m_MDS_ENTER();
	temp_svc_info = (MDS_SVC_INFO *)ncs_patricia_tree_getnext(
	    &gl_mds_mcm_cb->svc_list, (uint8_t *)&current_svc_hdl);
	while (temp_svc_info != NULL) {
		if (m_MDS_GET_VDEST_ID_FROM_SVC_HDL(temp_svc_info->svc_hdl) ==
		    vdest_id) {
			*svc_info = temp_svc_info;
			m_MDS_LEAVE();
			return NCSCC_RC_SUCCESS;
		}
		temp_svc_info = (MDS_SVC_INFO *)ncs_patricia_tree_getnext(
		    &gl_mds_mcm_cb->svc_list,
		    (uint8_t *)&temp_svc_info->svc_hdl);
	}
	m_MDS_LOG_DBG("MDS:DB:  Next SVC not present");
	m_MDS_LEAVE();
	return NCSCC_RC_FAILURE;
}

/*********************************************************
  Function NAME: mds_svc_tbl_cleanup
*********************************************************/
uint32_t mds_svc_tbl_cleanup(void)
{
	MDS_SVC_INFO *svc_info;
	MDS_SVC_HDL svc_hdl = 0;
	MDS_SUBSCRIPTION_INFO *temp_current_subtn_info = NULL;
	MDS_AWAIT_DISC_QUEUE *temp_disc_queue = NULL;

	MDS_MCM_SYNC_SEND_QUEUE *q_hdr = NULL, *prev_mem = NULL;

	m_MDS_ENTER();
	/* Check if svc already exist */
	svc_info = (MDS_SVC_INFO *)ncs_patricia_tree_getnext(
	    &gl_mds_mcm_cb->svc_list, (uint8_t *)&svc_hdl);
	while (svc_info != NULL) {

		while (svc_info->subtn_info != NULL) {
			/* Temporary store current subtn info */
			temp_current_subtn_info = svc_info->subtn_info;

			/* Point to next subtn info */
			svc_info->subtn_info = svc_info->subtn_info->next;

			/* Stop Timers and delete pending Messages, if any. */
			if (temp_current_subtn_info->tmr_flag == true) {
				/* Destroy Handle */
				ncshm_destroy_hdl(
				    NCS_SERVICE_ID_COMMON,
				    (uint32_t)temp_current_subtn_info
					->tmr_req_info_hdl);

				/* Stop Timer */
				m_NCS_TMR_STOP(
				    temp_current_subtn_info->discovery_tmr);

				/* Free tmr_req_info */
				m_MMGR_FREE_TMR_INFO(
				    temp_current_subtn_info->tmr_req_info);
				temp_current_subtn_info->tmr_req_info = NULL;

				/* Delete entries awaiting on this subscription
				 * timer */
				while (
				    temp_current_subtn_info->await_disc_queue !=
				    NULL) {
					/* Store current request pointer */
					temp_disc_queue =
					    temp_current_subtn_info
						->await_disc_queue;

					/* Move pointer to next disc_queue */
					temp_current_subtn_info
					    ->await_disc_queue =
					    temp_current_subtn_info
						->await_disc_queue->next_msg;

					/* Destroy selection object */
					/* m_NCS_SEL_OBJ_DESTROY(temp_disc_queue->sel_obj);
					 */
					/* We raise the selection object,
					 * instead of the destroy */
					m_NCS_SEL_OBJ_IND(
					    &temp_disc_queue->sel_obj);

					/* Free queued request */
					m_MMGR_FREE_DISC_QUEUE(temp_disc_queue);
				}
			}
			/* Destory Timer */
			m_NCS_TMR_DESTROY(
			    temp_current_subtn_info->discovery_tmr);

			/* Free Subscription info */
			m_MMGR_FREE_SUBTN_INFO(temp_current_subtn_info);
		}
		/* Store current svc_hdl for getting next service in tree */
		svc_hdl = svc_info->svc_hdl;

		/* Delete service table entry */
		if (svc_info->q_ownership == 1) {
			/*  todo put cleanup function instead of NULL in
			 * (NCS_IPC_CB) */
			m_NCS_IPC_DETACH(&svc_info->q_mbx, (NCS_IPC_CB)NULL,
					 NULL);
			/*  todo to provide cleanup callback */
			m_NCS_IPC_RELEASE(&svc_info->q_mbx, NULL);
		}

		/* Raise the selection object for the sync send Q and free the
		 * memory */
		q_hdr = svc_info->sync_send_queue;
		while (q_hdr != NULL) {
			prev_mem = q_hdr;
			q_hdr = q_hdr->next_send;
			m_NCS_SEL_OBJ_IND(&prev_mem->sel_obj);
			m_MMGR_FREE_SYNC_SEND_QUEUE(prev_mem);
		}
		svc_info->sync_send_queue = NULL;

		/* Delete from tree */
		ncs_patricia_tree_del(&gl_mds_mcm_cb->svc_list,
				      (NCS_PATRICIA_NODE *)svc_info);
		/* Free svc_info */
		m_MMGR_FREE_SVC_INFO(svc_info);

		/* Get next service */
		svc_info = (MDS_SVC_INFO *)ncs_patricia_tree_getnext(
		    &gl_mds_mcm_cb->svc_list, (uint8_t *)&svc_hdl);
	}
	return NCSCC_RC_SUCCESS;
}

/* ******************************************** */
/* ******************************************** */
/*           SUBTN TABLE Operations             */
/* ******************************************** */
/* ******************************************** */

/*********************************************************
  Function NAME: mds_subtn_tbl_add
*********************************************************/
uint32_t mds_subtn_tbl_add(MDS_SVC_HDL svc_hdl, MDS_SVC_ID subscr_svc_id,
			   NCSMDS_SCOPE_TYPE scope, MDS_VIEW view,
			   MDS_SUBTN_TYPE subtn_type)
{
	uint32_t status = NCSCC_RC_SUCCESS;
	MDS_SVC_INFO *svc_info;
	MDS_SUBSCRIPTION_INFO *subtn_info;
	MDS_TMR_REQ_INFO *tmr_req_info = NULL;

	m_MDS_ENTER();
	status = mds_subtn_tbl_query(svc_hdl, subscr_svc_id);
	if (status == NCSCC_RC_SUCCESS || status == NCSCC_RC_NO_CREATION) {
		/* Service already subscribed IMPLICITLY */
		m_MDS_LOG_INFO(
		    "MDS:DB: subtn_tbl_add : IMPLICIT SUBSCRIPTION of svc_id = %s(%d)  ALREADY EXIST",
		    get_svc_names(subscr_svc_id), subscr_svc_id);
		m_MDS_LEAVE();
		return NCSCC_RC_FAILURE;
	}

	svc_info = (MDS_SVC_INFO *)ncs_patricia_tree_get(
	    &gl_mds_mcm_cb->svc_list, (uint8_t *)&svc_hdl);
	if (svc_info == NULL) {
		/* Local service doesn't exist */
		m_MDS_LOG_DBG("MDS:DB: SVC not present");
		m_MDS_LEAVE();
		return NCSCC_RC_FAILURE;
	}

	subtn_info = m_MMGR_ALLOC_SUBTN_INFO;
	memset(subtn_info, 0, sizeof(MDS_SUBSCRIPTION_INFO));

	subtn_info->sub_svc_id = subscr_svc_id;
	subtn_info->scope = scope;
	subtn_info->view = view;
	subtn_info->subtn_type = subtn_type;

	/* Add subscription to linklist */
	subtn_info->next = svc_info->subtn_info;
	svc_info->subtn_info = subtn_info;

	strcpy(subtn_info->sub_adest_details, svc_info->adest_details);
	/*  STEP 2.b: Start Subscription Timer */

	subtn_info->tmr_flag = true;

	tmr_req_info = m_MMGR_ALLOC_TMR_INFO;
	memset(tmr_req_info, 0, sizeof(MDS_TMR_REQ_INFO));
	tmr_req_info->type = MDS_SUBTN_TMR;
	tmr_req_info->info.subtn_tmr_info.svc_hdl = svc_hdl;
	tmr_req_info->info.subtn_tmr_info.sub_svc_id = subscr_svc_id;

	/* Store pointer of allocated tmr_req_info */
	subtn_info->tmr_req_info = tmr_req_info;

	/* Create Handle */
	subtn_info->tmr_req_info_hdl =
	    ncshm_create_hdl(NCS_HM_POOL_ID_COMMON, NCS_SERVICE_ID_COMMON,
			     (NCSCONTEXT)tmr_req_info);

	m_NCS_TMR_CREATE(subtn_info->discovery_tmr, MDS_SUBSCRIPTION_TMR_VAL,
			 mds_tmr_callback, (void *)tmr_req_info);
	m_MDS_LOG_DBG("discovery_tmr=0x%08x",
		      *(unsigned int *)subtn_info->discovery_tmr);
	m_NCS_TMR_START(subtn_info->discovery_tmr, MDS_SUBSCRIPTION_TMR_VAL,
			(TMR_CALLBACK)mds_tmr_callback,
			(void *)(long)subtn_info->tmr_req_info_hdl);
	m_MDS_LOG_DBG(
	    "MCM_DB:mds_subtn_tbl_add:TimerStart:SubTmr:Hdl=0x%08x:SvcHdl=%" PRIu64
	    ":sbscr-svcid=%s(%d)\n",
	    subtn_info->tmr_req_info_hdl, svc_hdl, get_svc_names(subscr_svc_id),
	    subscr_svc_id);

	m_MDS_LOG_DBG("MDS:DB: sub_adest_details : %s",
		      subtn_info->sub_adest_details);
	m_MDS_LEAVE();
	return NCSCC_RC_SUCCESS;
}

/*********************************************************
  Function NAME: mds_subtn_tbl_del
*********************************************************/
uint32_t mds_subtn_tbl_del(MDS_SVC_HDL svc_hdl, uint32_t subscr_svc_id)
{
	uint32_t status = NCSCC_RC_SUCCESS;
	MDS_SVC_INFO *svc_info;
	MDS_SUBSCRIPTION_INFO *temp_current_subtn_info;
	MDS_SUBSCRIPTION_INFO *temp_previous_subtn_info;
	MDS_AWAIT_DISC_QUEUE *temp_disc_queue = NULL;

	m_MDS_ENTER();
	status = mds_subtn_tbl_query(svc_hdl, subscr_svc_id);
	if (status == NCSCC_RC_FAILURE) {
		/* Subscription doesn't exist */
		m_MDS_LOG_DBG("MDS:DB: Subscription not present");
		m_MDS_LEAVE();
		return NCSCC_RC_FAILURE;
	}

	svc_info = (MDS_SVC_INFO *)ncs_patricia_tree_get(
	    &gl_mds_mcm_cb->svc_list, (uint8_t *)&svc_hdl);
	if (svc_info == NULL) {
		/* Local service doesn't exist */
		m_MDS_LOG_DBG("MDS:DB: SVC not present");
		m_MDS_LEAVE();
		return NCSCC_RC_FAILURE;
	}

	temp_current_subtn_info = svc_info->subtn_info;
	temp_previous_subtn_info = svc_info->subtn_info;

	while (temp_current_subtn_info != NULL) {
		if (temp_current_subtn_info->sub_svc_id == subscr_svc_id) {
			/* Got it, Delete it */

			if (temp_previous_subtn_info ==
			    temp_current_subtn_info) {
				/* It is the first element */
				svc_info->subtn_info =
				    temp_current_subtn_info->next;
			} else {
				temp_previous_subtn_info->next =
				    temp_current_subtn_info->next;
			}

			/* Stop Timers and delete pending Messages, if any. */
			if (temp_current_subtn_info->tmr_flag == true) {
				/* Destroy Handle */
				ncshm_destroy_hdl(
				    NCS_SERVICE_ID_COMMON,
				    (uint32_t)temp_current_subtn_info
					->tmr_req_info_hdl);

				/* Destory Timer */
				m_NCS_TMR_DESTROY(
				    temp_current_subtn_info->discovery_tmr);

				/* Free tmr_req_info */
				m_MMGR_FREE_TMR_INFO(
				    temp_current_subtn_info->tmr_req_info);
				temp_current_subtn_info->tmr_req_info = NULL;

				/* Delete entries awaiting on this subscription
				 * timer */
				while (
				    temp_current_subtn_info->await_disc_queue !=
				    NULL) {
					/* Store current request pointer */
					temp_disc_queue =
					    temp_current_subtn_info
						->await_disc_queue;

					/* Move pointer to next disc_queue */
					temp_current_subtn_info
					    ->await_disc_queue =
					    temp_current_subtn_info
						->await_disc_queue->next_msg;

					/* Destroy selection object */
					/* m_NCS_SEL_OBJ_DESTROY(temp_disc_queue->sel_obj);
					 */
					/* We raise the selection object,
					 * instead of the destroy */
					m_NCS_SEL_OBJ_IND(
					    &temp_disc_queue->sel_obj);

					/* Free queued request */
					m_MMGR_FREE_DISC_QUEUE(temp_disc_queue);
				}
			}

			/* Free Subscription info */
			m_MMGR_FREE_SUBTN_INFO(temp_current_subtn_info);
			m_MDS_LEAVE();
			return NCSCC_RC_SUCCESS;
		}
		temp_previous_subtn_info = temp_current_subtn_info;
		temp_current_subtn_info = temp_current_subtn_info->next;
	}
	m_MDS_LOG_DBG(
	    "MDS:DB: Subscription not present, Reached till end of function");
	m_MDS_LEAVE();
	return NCSCC_RC_FAILURE;
}

/*********************************************************
  Function NAME: mds_subtn_tbl_change_explicit
*********************************************************/
uint32_t mds_subtn_tbl_change_explicit(MDS_SVC_HDL svc_hdl,
				       MDS_SVC_ID subscr_svc_id,
				       MDS_VIEW subtn_view_type)
{
	MDS_SVC_INFO *svc_info;
	MDS_SUBSCRIPTION_INFO *temp_subtn_info;
	MDS_SUBSCRIPTION_RESULTS_INFO *temp_subtn_result_info = NULL;
	MDS_SUBSCRIPTION_RESULTS_KEY subtn_res_key;

	m_MDS_ENTER();
	memset(&subtn_res_key, 0, sizeof(MDS_SUBSCRIPTION_RESULTS_KEY));

	svc_info = (MDS_SVC_INFO *)ncs_patricia_tree_get(
	    &gl_mds_mcm_cb->svc_list, (uint8_t *)&svc_hdl);
	if (svc_info == NULL) {
		/* Local service doesn't exist */
		m_MDS_LOG_DBG("MDS:DB: SVC not present");
		m_MDS_LEAVE();
		return NCSCC_RC_FAILURE;
	}

	temp_subtn_info = svc_info->subtn_info;

	while (temp_subtn_info != NULL) {
		if (temp_subtn_info->sub_svc_id == subscr_svc_id) {
			temp_subtn_info->subtn_type = MDS_SUBTN_EXPLICIT;
			temp_subtn_info->view = subtn_view_type;

			/* Search for current UP services in subtn_res_tbl and
			 * give to user */

			/* Search for Active entry */
			subtn_res_key.svc_hdl = svc_hdl;
			subtn_res_key.sub_svc_id = subscr_svc_id;
			subtn_res_key.vdest_id = 0;
			subtn_res_key.adest = 0;

			temp_subtn_result_info = NULL;
			temp_subtn_result_info =
			    (MDS_SUBSCRIPTION_RESULTS_INFO *)
				ncs_patricia_tree_getnext(
				    &gl_mds_mcm_cb->subtn_results,
				    (uint8_t *)&subtn_res_key);
			while (temp_subtn_result_info != NULL &&
			       temp_subtn_result_info->key.sub_svc_id ==
				   subscr_svc_id) {
				if (temp_subtn_result_info->key.adest ==
				    0) { /* This is active entry for a VDEST */

					if (temp_subtn_result_info->info
						.active_vdest.active_route_info
						->tmr_running ==
					    true) { /* Await Active Timer is
						       running */

						/* so just give Await Active to
						 * user no matter vdest policy
						 */
						if (NCSCC_RC_SUCCESS !=
						    mds_mcm_user_event_callback(
							svc_hdl,
							m_MDS_GET_PWE_ID_FROM_SVC_HDL(
							    svc_hdl),
							subscr_svc_id,
							V_DEST_RL_ACTIVE,
							temp_subtn_result_info
							    ->key.vdest_id,
							0, NCSMDS_NO_ACTIVE,
							temp_subtn_result_info
							    ->rem_svc_sub_part_ver,
							MDS_SVC_ARCHWORD_TYPE_UNSPECIFIED)) {
							m_MDS_LOG_ERR(
							    "MDS:DB:mds_mcm_user_event_callback: Await Active Entry: F, svc_id=%d, subscribed_svc=%d",
							    m_MDS_GET_SVC_ID_FROM_SVC_HDL(
								svc_hdl),
							    subscr_svc_id);
						}
					} else { /* Active entry exist */

						/* Call user callback UP for
						 * first active */
						if (NCSCC_RC_SUCCESS !=
						    mds_mcm_user_event_callback(
							svc_hdl,
							m_MDS_GET_PWE_ID_FROM_SVC_HDL(
							    svc_hdl),
							subscr_svc_id,
							V_DEST_RL_ACTIVE,
							temp_subtn_result_info
							    ->key.vdest_id,
							temp_subtn_result_info
							    ->info.active_vdest
							    .active_route_info
							    ->next_active_in_turn
							    ->key.adest,
							NCSMDS_UP,
							temp_subtn_result_info
							    ->rem_svc_sub_part_ver,
							temp_subtn_result_info
							    ->rem_svc_arch_word)) {
							m_MDS_LOG_ERR(
							    "MDS:DB:mds_mcm_user_event_callback: Active Entry: F, svc_id=%d, subscribed_svc=%d",
							    m_MDS_GET_SVC_ID_FROM_SVC_HDL(
								svc_hdl),
							    subscr_svc_id);
						}
					}
				} else if (
				    temp_subtn_result_info->key.vdest_id ==
				    m_VDEST_ID_FOR_ADEST_ENTRY) { /* This is
								     ADEST entry
								   */

					/* so just give UP to user */
					if (NCSCC_RC_SUCCESS !=
					    mds_mcm_user_event_callback(
						svc_hdl,
						m_MDS_GET_PWE_ID_FROM_SVC_HDL(
						    svc_hdl),
						subscr_svc_id, V_DEST_RL_ACTIVE,
						m_VDEST_ID_FOR_ADEST_ENTRY,
						temp_subtn_result_info->key
						    .adest,
						NCSMDS_UP,
						temp_subtn_result_info
						    ->rem_svc_sub_part_ver,
						temp_subtn_result_info
						    ->rem_svc_arch_word)) {
						m_MDS_LOG_ERR(
						    "MDS:DB:mds_mcm_user_event_callback: ADEST Entry: F, svc_id=%d, subscribed_svc=%d",
						    m_MDS_GET_SVC_ID_FROM_SVC_HDL(
							svc_hdl),
						    subscr_svc_id);
					}
				} else { /* This is VDEST entry, it can be
					    active or standby */
					if (temp_subtn_info->view ==
					    MDS_VIEW_RED) {
						/* Call user callback RED_UP for
						 * all instances */
						if (NCSCC_RC_SUCCESS !=
						    mds_mcm_user_event_callback(
							svc_hdl,
							m_MDS_GET_PWE_ID_FROM_SVC_HDL(
							    svc_hdl),
							subscr_svc_id,
							V_DEST_RL_ACTIVE,
							temp_subtn_result_info
							    ->key.vdest_id,
							temp_subtn_result_info
							    ->key.adest,
							NCSMDS_RED_UP,
							temp_subtn_result_info
							    ->rem_svc_sub_part_ver,
							temp_subtn_result_info
							    ->rem_svc_arch_word)) {
							m_MDS_LOG_ERR(
							    "MDS:DB:mds_mcm_user_event_callback: RED_VIEW: F, svc_id=%d, subscribed_svc=%d",
							    m_MDS_GET_SVC_ID_FROM_SVC_HDL(
								svc_hdl),
							    subscr_svc_id);
						}
					}
				}

				/* Get next subtn result info */
				temp_subtn_result_info =
				    (MDS_SUBSCRIPTION_RESULTS_INFO *)
					ncs_patricia_tree_getnext(
					    &gl_mds_mcm_cb->subtn_results,
					    (uint8_t *)&temp_subtn_result_info
						->key);
			}
		}
		/* Get next subtn info */
		temp_subtn_info = temp_subtn_info->next;
	}
	m_MDS_LEAVE();
	return NCSCC_RC_SUCCESS;
}

/*********************************************************
  Function NAME: mds_subtntbl_query
*********************************************************/
uint32_t mds_subtn_tbl_query(MDS_SVC_HDL svc_hdl, MDS_SVC_ID subscr_svc_id)
{

	MDS_SVC_INFO *svc_info;
	MDS_SUBSCRIPTION_INFO *temp_subtn_info;

	m_MDS_ENTER();
	svc_info = (MDS_SVC_INFO *)ncs_patricia_tree_get(
	    &gl_mds_mcm_cb->svc_list, (uint8_t *)&svc_hdl);
	if (svc_info == NULL) {
		/* Local service doesn't exist */
		m_MDS_LOG_DBG("MDS:DB: SVC not present");
		m_MDS_LEAVE();
		return NCSCC_RC_FAILURE;
	}

	temp_subtn_info = svc_info->subtn_info;

	while (temp_subtn_info != NULL) {
		if (temp_subtn_info->sub_svc_id == subscr_svc_id) {
			if (temp_subtn_info->subtn_type == MDS_SUBTN_IMPLICIT) {
				/* It is IMPLICIT subscription */
				m_MDS_LEAVE();
				return NCSCC_RC_NO_CREATION;
			} else {
				/* It is EXPLICIT subscription */
				m_MDS_LEAVE();
				return NCSCC_RC_SUCCESS;
			}
		}
		temp_subtn_info = temp_subtn_info->next;
	}
	m_MDS_LEAVE();
	return NCSCC_RC_FAILURE;
}

/*********************************************************
  Function NAME: mds_subtn_tbl_update_ref_hdl
*********************************************************/
uint32_t mds_subtn_tbl_update_ref_hdl(MDS_SVC_HDL svc_hdl,
				      MDS_SVC_ID subscr_svc_id,
				      MDS_SUBTN_REF_VAL subscr_ref_hdl)
{

	MDS_SVC_INFO *svc_info;
	MDS_SUBSCRIPTION_INFO *temp_subtn_info;

	m_MDS_ENTER();
	svc_info = (MDS_SVC_INFO *)ncs_patricia_tree_get(
	    &gl_mds_mcm_cb->svc_list, (uint8_t *)&svc_hdl);
	if (svc_info == NULL) {
		/* Local service doesn't exist */
		m_MDS_LOG_DBG("MDS:DB:  SVC not present");
		m_MDS_LEAVE();
		return NCSCC_RC_FAILURE;
	}

	temp_subtn_info = svc_info->subtn_info;

	while (temp_subtn_info != NULL) {
		if (temp_subtn_info->sub_svc_id == subscr_svc_id) {
			temp_subtn_info->subscr_req_hdl = subscr_ref_hdl;
			m_MDS_LEAVE();
			return NCSCC_RC_SUCCESS;
		}
		temp_subtn_info = temp_subtn_info->next;
	}
	m_MDS_LEAVE();
	return NCSCC_RC_FAILURE;
}

/*********************************************************
  Function NAME: mds_subtn_tbl_get
*********************************************************/
uint32_t mds_subtn_tbl_get(MDS_SVC_HDL svc_hdl, MDS_SVC_ID subscr_svc_id,
			   MDS_SUBSCRIPTION_INFO **result)
{

	MDS_SVC_INFO *svc_info;
	MDS_SUBSCRIPTION_INFO *temp_subtn_info;

	m_MDS_ENTER();
	svc_info = (MDS_SVC_INFO *)ncs_patricia_tree_get(
	    &gl_mds_mcm_cb->svc_list, (uint8_t *)&svc_hdl);
	if (svc_info == NULL) {
		/* Local service doesn't exist */
		m_MDS_LOG_DBG("MDS:DB: SVC not present");
		m_MDS_LEAVE();
		return NCSCC_RC_FAILURE;
	}

	temp_subtn_info = svc_info->subtn_info;

	while (temp_subtn_info != NULL) {
		if (temp_subtn_info->sub_svc_id == subscr_svc_id) {
			*result = temp_subtn_info;
			m_MDS_LEAVE();
			return NCSCC_RC_SUCCESS;
		}
		temp_subtn_info = temp_subtn_info->next;
	}
	m_MDS_LEAVE();
	return NCSCC_RC_FAILURE;
}

/*********************************************************
  Function NAME: mds_subtn_tbl_get_details
*********************************************************/
uint32_t mds_subtn_tbl_get_details(MDS_SVC_HDL svc_hdl,
				   MDS_SVC_ID subscr_svc_id,
				   NCSMDS_SCOPE_TYPE *scope, MDS_VIEW *view)
{

	MDS_SVC_INFO *svc_info;
	MDS_SUBSCRIPTION_INFO *temp_subtn_info;

	m_MDS_ENTER();
	svc_info = (MDS_SVC_INFO *)ncs_patricia_tree_get(
	    &gl_mds_mcm_cb->svc_list, (uint8_t *)&svc_hdl);
	if (svc_info == NULL) {
		/* Local service doesn't exist */
		m_MDS_LOG_DBG("MDS:DB: SVC not present");
		m_MDS_LEAVE();
		return NCSCC_RC_FAILURE;
	}

	temp_subtn_info = svc_info->subtn_info;

	while (temp_subtn_info != NULL) {
		if (temp_subtn_info->sub_svc_id == subscr_svc_id) {
			*scope = temp_subtn_info->scope;
			*view = temp_subtn_info->view;
			m_MDS_LEAVE();
			return NCSCC_RC_SUCCESS;
		}
		temp_subtn_info = temp_subtn_info->next;
	}
	m_MDS_LEAVE();
	return NCSCC_RC_FAILURE;
}

/*********************************************************
  Function NAME: mds_subtn_tbl_get_ref_hdl
*********************************************************/
uint32_t mds_subtn_tbl_get_ref_hdl(MDS_SVC_HDL svc_hdl,
				   MDS_SVC_ID subscr_svc_id,
				   MDS_SUBTN_REF_VAL *subscr_ref_hdl,
				   NCSMDS_SCOPE_TYPE *scope)
{

	MDS_SVC_INFO *svc_info;
	MDS_SUBSCRIPTION_INFO *temp_subtn_info;

	m_MDS_ENTER();
	svc_info = (MDS_SVC_INFO *)ncs_patricia_tree_get(
	    &gl_mds_mcm_cb->svc_list, (uint8_t *)&svc_hdl);
	if (svc_info == NULL) {
		/* Local service doesn't exist */
		m_MDS_LOG_DBG("MDS:DB: SVC not present");
		m_MDS_LEAVE();
		return NCSCC_RC_FAILURE;
	}

	temp_subtn_info = svc_info->subtn_info;

	while (temp_subtn_info != NULL) {
		if (temp_subtn_info->sub_svc_id == subscr_svc_id) {
			*subscr_ref_hdl = temp_subtn_info->subscr_req_hdl;
			*scope = temp_subtn_info->scope;
			m_MDS_LEAVE();
			return NCSCC_RC_SUCCESS;
		}
		temp_subtn_info = temp_subtn_info->next;
	}
	m_MDS_LOG_DBG("MDS:DB: Subscription not present");
	m_MDS_LEAVE();
	return NCSCC_RC_FAILURE;
}

/* ******************************************** */
/* ******************************************** */
/*        BLOCK SEND REQ TABLE Operations       */
/* ******************************************** */
/* ******************************************** */

/*********************************************************
  Function NAME: mds_block_snd_req_tbl_add
*********************************************************/
uint32_t mds_block_snd_req_tbl_add(MDS_SVC_HDL svc_hdl, MDS_SYNC_TXN_ID txn_id,
				   MDS_MCM_SYNC_SEND_QUEUE *result)
{
	return NCSCC_RC_SUCCESS;
}

/*********************************************************
  Function NAME: mds_block_snd_req_tbl_del
*********************************************************/
uint32_t mds_block_snd_req_tbl_del(MDS_SVC_HDL svc_hdl, MDS_SYNC_TXN_ID txn_id)
{
	return NCSCC_RC_SUCCESS;
}

/*********************************************************
  Function NAME: mds_block_snd_req_tbl_query
*********************************************************/
uint32_t mds_block_snd_req_tbl_query(MDS_SVC_HDL svc_hdl,
				     MDS_SYNC_TXN_ID txn_id)
{
	return NCSCC_RC_SUCCESS;
}

/*********************************************************
  Function NAME: mds_block_snd_req_tbl_get
*********************************************************/
uint32_t mds_block_snd_req_tbl_get(MDS_SVC_HDL svc_hdl, MDS_SYNC_TXN_ID txn_id,
				   MDS_MCM_SYNC_SEND_QUEUE *result)
{
	return NCSCC_RC_SUCCESS;
}

/* ******************************************** */
/* ******************************************** */
/*       SUBTN RESULT TABLE Operations          */
/* ******************************************** */
/* ******************************************** */

/*********************************************************
  Function NAME: mds_subtn_res_tbl_add
*********************************************************/
uint32_t mds_subtn_res_tbl_add(MDS_SVC_HDL svc_hdl, MDS_SVC_ID subscr_svc_id,
			       MDS_VDEST_ID vdest_id, MDS_DEST adest,
			       V_DEST_RL role, NCSMDS_SCOPE_TYPE scope,
			       NCS_VDEST_TYPE local_vdest_policy,
			       MDS_SVC_PVT_SUB_PART_VER svc_sub_part_ver,
			       MDS_SVC_ARCHWORD_TYPE archword_type)
{
	MDS_SUBSCRIPTION_RESULTS_INFO *subtn_res_info = NULL;
	MDS_SUBSCRIPTION_RESULTS_INFO *active_subtn_res_info = NULL;
	MDS_SUBSCRIPTION_RESULTS_KEY subtn_res_key;
	MDS_ACTIVE_RESULT_INFO *active_info = NULL;

	m_MDS_ENTER();
	memset(&subtn_res_key, 0, sizeof(MDS_SUBSCRIPTION_RESULTS_KEY));

	subtn_res_key.svc_hdl = svc_hdl;
	subtn_res_key.sub_svc_id = subscr_svc_id;
	subtn_res_key.vdest_id = vdest_id;
	subtn_res_key.adest = adest;

	subtn_res_info = (MDS_SUBSCRIPTION_RESULTS_INFO *)ncs_patricia_tree_get(
	    &gl_mds_mcm_cb->subtn_results, (uint8_t *)&subtn_res_key);

	if (subtn_res_info != NULL) {
		/* Subscription entry already exist */
		m_MDS_LOG_DBG("MDS:DB: Subscription Result already present");
		m_MDS_LEAVE();
		return NCSCC_RC_FAILURE;
	}

	subtn_res_info = m_MMGR_ALLOC_SUBTN_RESULT_INFO;
	memset(subtn_res_info, 0, sizeof(MDS_SUBSCRIPTION_RESULTS_INFO));

	memcpy(&subtn_res_info->key, &subtn_res_key,
	       sizeof(MDS_SUBSCRIPTION_RESULTS_KEY));

	subtn_res_info->node.key_info = (uint8_t *)&subtn_res_info->key;

	subtn_res_info->info.vdest_inst.role = role;
	subtn_res_info->install_scope = scope;
	subtn_res_info->rem_svc_sub_part_ver = svc_sub_part_ver;
	subtn_res_info->rem_svc_arch_word = archword_type;

	get_subtn_adest_details(m_MDS_GET_PWE_HDL_FROM_SVC_HDL(svc_hdl),
				subscr_svc_id, adest,
				subtn_res_info->sub_adest_details);

	ncs_patricia_tree_add(&gl_mds_mcm_cb->subtn_results,
			      (NCS_PATRICIA_NODE *)&subtn_res_info->node);

	if (vdest_id !=
	    m_VDEST_ID_FOR_ADEST_ENTRY) { /* Entry to add is VDEST entry */

		subtn_res_info->info.vdest_inst.policy = local_vdest_policy;

		if (role == V_DEST_RL_ACTIVE) {

			subtn_res_key.adest = 0;
			active_subtn_res_info =
			    (MDS_SUBSCRIPTION_RESULTS_INFO *)
				ncs_patricia_tree_get(
				    &gl_mds_mcm_cb->subtn_results,
				    (uint8_t *)&subtn_res_key);

			if (active_subtn_res_info ==
			    NULL) { /* Active vdest entry doesn't exist */

				active_subtn_res_info =
				    m_MMGR_ALLOC_SUBTN_RESULT_INFO;
				memset(active_subtn_res_info, 0,
				       sizeof(MDS_SUBSCRIPTION_RESULTS_INFO));
				memcpy(&active_subtn_res_info->key,
				       &subtn_res_key,
				       sizeof(MDS_SUBSCRIPTION_RESULTS_KEY));

				/* Allocate active result info */
				active_info =
				    m_MMGR_ALLOC_SUBTN_ACTIVE_RESULT_INFO;
				memset(active_info, 0,
				       sizeof(MDS_ACTIVE_RESULT_INFO));

				/* point to currently added active guy as active
				 */
				/* works for both n-way as well as mxn vdests */
				active_subtn_res_info->info.active_vdest
				    .active_route_info = active_info;
				active_info->next_active_in_turn =
				    subtn_res_info;

				if (local_vdest_policy == NCS_VDEST_TYPE_MxN) {
					active_info->dest_is_n_way = false;
				} else {
					active_info->dest_is_n_way = true;
				}
				active_info->last_active_svc_sub_part_ver =
				    svc_sub_part_ver;

				/* Create Await active timer (dont start, just
				 * create) */
				m_NCS_TMR_CREATE(active_info->await_active_tmr,
						 MDS_AWAIT_ACTIVE_TMR_VAL,
						 mds_tmr_callback, NULL);

				m_MDS_LOG_DBG("Await active tmr=0x%08x",
					      *(unsigned int *)active_info
						   ->await_active_tmr);
				/* add entry in tree */
				active_subtn_res_info->node.key_info =
				    (uint8_t *)&active_subtn_res_info->key;
				ncs_patricia_tree_add(
				    &gl_mds_mcm_cb->subtn_results,
				    (NCS_PATRICIA_NODE *)&active_subtn_res_info
					->node);
			} else { /* Active entry Or Await active entry exist */

				if (active_subtn_res_info->info.active_vdest
					.active_route_info->tmr_running ==
				    true) { /* Present entry is Await Active
					       Entry */

					/* Stop await active timer */

					/* Trun tmr_running flag off */
					active_subtn_res_info->info.active_vdest
					    .active_route_info->tmr_running =
					    false;
					/* Destroy Handle */
					ncshm_destroy_hdl(
					    NCS_SERVICE_ID_COMMON,
					    (uint32_t)active_subtn_res_info
						->info.active_vdest
						.active_route_info
						->tmr_req_info_hdl);

					/* Stop Timer */
					m_NCS_TMR_STOP(
					    active_subtn_res_info->info
						.active_vdest.active_route_info
						->await_active_tmr);
					/* Free tmr_req_info */
					m_MMGR_FREE_TMR_INFO(
					    active_subtn_res_info->info
						.active_vdest.active_route_info
						->tmr_req_info);

					/* Send Pending messages in Await Active
					 * queue */
					mds_await_active_tbl_send(
					    active_subtn_res_info->info
						.active_vdest.active_route_info
						->await_active_queue,
					    adest, svc_hdl);

					/* Make await active header ptr null */
					active_subtn_res_info->info.active_vdest
					    .active_route_info
					    ->await_active_queue = NULL;
					active_subtn_res_info->info.active_vdest
					    .active_route_info
					    ->last_active_svc_sub_part_ver =
					    svc_sub_part_ver;

					/* Point to this active as new active */
					active_subtn_res_info->info.active_vdest
					    .active_route_info
					    ->next_active_in_turn =
					    subtn_res_info;
				} else { /* Present entry is Active Entry */

					if (local_vdest_policy ==
					    NCS_VDEST_TYPE_MxN) {
						/* Change active to point to
						 * this active */
						active_subtn_res_info->info
						    .active_vdest
						    .active_route_info
						    ->next_active_in_turn =
						    subtn_res_info;
					} else {
						/* Do nothing just add entry */
					}
				}
			}
		} else { /* role == V_DEST_RL_STANDBY */

			/* do nothing already added in start of this function */
		}
	} else { /* This is adest entry */

		/* do nothing already added entry in tree */
	}
	m_MDS_LOG_DBG("MDS:DB: sub_adest_details: %s",
		      subtn_res_info->sub_adest_details);
	m_MDS_LEAVE();
	return NCSCC_RC_SUCCESS;
}

/*********************************************************
  Function NAME: mds_subtn_res_tbl_del
*********************************************************/
uint32_t mds_subtn_res_tbl_del(MDS_SVC_HDL svc_hdl, MDS_SVC_ID sub_svc_id,
			       MDS_VDEST_ID vdest_id, MDS_DEST adest,
			       NCS_VDEST_TYPE local_vdest_policy,
			       MDS_SVC_PVT_SUB_PART_VER svc_sub_part_ver,
			       MDS_SVC_ARCHWORD_TYPE archword_type)
{
	MDS_SUBSCRIPTION_RESULTS_INFO *subtn_res_info;
	MDS_SUBSCRIPTION_RESULTS_KEY subtn_res_key;

	m_MDS_ENTER();
	memset(&subtn_res_key, 0, sizeof(MDS_SUBSCRIPTION_RESULTS_KEY));

	subtn_res_key.svc_hdl = svc_hdl;
	subtn_res_key.sub_svc_id = sub_svc_id;
	subtn_res_key.vdest_id = vdest_id;
	subtn_res_key.adest = adest;

	subtn_res_info = (MDS_SUBSCRIPTION_RESULTS_INFO *)ncs_patricia_tree_get(
	    &gl_mds_mcm_cb->subtn_results, (uint8_t *)&subtn_res_key);
	if (subtn_res_info == NULL) {
		/* Subscription result entry doesn't exist */
		m_MDS_LOG_DBG("MDS:DB: Subscription Result not present");
		m_MDS_LEAVE();
		return NCSCC_RC_FAILURE;
	}

	/* Delete entry from subtn result tree */
	ncs_patricia_tree_del(&gl_mds_mcm_cb->subtn_results,
			      (NCS_PATRICIA_NODE *)subtn_res_info);
	/* Free subscription result info */
	m_MMGR_FREE_SUBTN_RESULT_INFO(subtn_res_info);
	m_MDS_LEAVE();
	return NCSCC_RC_SUCCESS;
}

/*********************************************************
  Function NAME: mds_subtn_res_tbl_query
*********************************************************/
uint32_t mds_subtn_res_tbl_query(MDS_SVC_HDL svc_hdl, MDS_SVC_ID subscr_svc_id,
				 MDS_VDEST_ID vdest_id)
{
	uint32_t status = NCSCC_RC_SUCCESS;

	m_MDS_ENTER();
	status = mds_subtn_res_tbl_query_by_adest(svc_hdl, subscr_svc_id,
						  vdest_id, 0);
	m_MDS_LEAVE();
	return status;
}

/*********************************************************
  Function NAME: mds_subtn_res_tbl_query_by_adest
*********************************************************/
uint32_t mds_subtn_res_tbl_query_by_adest(MDS_SVC_HDL svc_hdl,
					  MDS_SVC_ID subscr_svc_id,
					  MDS_VDEST_ID vdest_id, MDS_DEST adest)
{

	MDS_SUBSCRIPTION_RESULTS_INFO *subtn_res_info;
	MDS_SUBSCRIPTION_RESULTS_KEY subtn_res_key;

	memset(&subtn_res_key, 0, sizeof(MDS_SUBSCRIPTION_RESULTS_KEY));

	subtn_res_key.svc_hdl = svc_hdl;
	subtn_res_key.sub_svc_id = subscr_svc_id;
	subtn_res_key.vdest_id = vdest_id;
	subtn_res_key.adest = adest;

	m_MDS_ENTER();
	subtn_res_info = (MDS_SUBSCRIPTION_RESULTS_INFO *)ncs_patricia_tree_get(
	    &gl_mds_mcm_cb->subtn_results, (uint8_t *)&subtn_res_key);
	if (subtn_res_info == NULL) {
		/* Subscription result entry doesn't exist */
		m_MDS_LOG_DBG("MDS:DB: Subscription Result not present");
		m_MDS_LEAVE();
		return NCSCC_RC_FAILURE;
	} else {
		m_MDS_LEAVE();
		return NCSCC_RC_SUCCESS;
	}
}

/*********************************************************
  Function NAME: mds_get_subtn_res_tbl_by_adest
*********************************************************/
uint32_t mds_get_subtn_res_tbl_by_adest(MDS_SVC_HDL svc_hdl,
					MDS_SVC_ID subscr_svc_id,
					MDS_VDEST_ID vdest_id, MDS_DEST adest,
					MDS_SUBSCRIPTION_RESULTS_INFO **result)
{
	MDS_SUBSCRIPTION_RESULTS_INFO *subtn_res_info;
	MDS_SUBSCRIPTION_RESULTS_KEY subtn_res_key;

	memset(&subtn_res_key, 0, sizeof(MDS_SUBSCRIPTION_RESULTS_KEY));

	subtn_res_key.svc_hdl = svc_hdl;
	subtn_res_key.sub_svc_id = subscr_svc_id;
	subtn_res_key.vdest_id = vdest_id;
	subtn_res_key.adest = adest;

	m_MDS_ENTER();
	subtn_res_info = (MDS_SUBSCRIPTION_RESULTS_INFO *)ncs_patricia_tree_get(
	    &gl_mds_mcm_cb->subtn_results, (uint8_t *)&subtn_res_key);
	if (subtn_res_info == NULL) {
		/* Subscription result entry doesn't exist */
		*result = NULL;
		m_MDS_LOG_DBG("MDS:DB: Subscription Result not present");
		m_MDS_LEAVE();
		return NCSCC_RC_FAILURE;
	} else {
		*result = subtn_res_info;
		m_MDS_LEAVE();
		return NCSCC_RC_SUCCESS;
	}
}

/*********************************************************
  Function NAME: mds_incr_subs_res_recvd_msg_cnt
*********************************************************/
void mds_incr_subs_res_recvd_msg_cnt(MDS_SVC_HDL svc_hdl,
				     MDS_SVC_ID subscr_svc_id,
				     MDS_VDEST_ID vdest_id, MDS_DEST adest,
				     uint32_t src_seq_num)
{
	MDS_SUBSCRIPTION_RESULTS_INFO *lcl_subtn_res = NULL;
	if (NCSCC_RC_SUCCESS ==
	    mds_get_subtn_res_tbl_by_adest(svc_hdl, subscr_svc_id, vdest_id,
					   adest, &lcl_subtn_res)) {
		lcl_subtn_res->msg_rcv_cnt = src_seq_num;
		lcl_subtn_res->msg_rcv_cnt++;
	}
}

/*********************************************************
  Function NAME: mds_subtn_res_tbl_change_active
*********************************************************/
uint32_t
mds_subtn_res_tbl_change_active(MDS_SVC_HDL svc_hdl, MDS_SVC_ID subscr_svc_id,
				MDS_VDEST_ID vdest_id,
				MDS_SUBSCRIPTION_RESULTS_INFO *active_result,
				MDS_SVC_PVT_SUB_PART_VER svc_sub_part_ver,
				MDS_SVC_ARCHWORD_TYPE archword_type)
{

	MDS_SUBSCRIPTION_RESULTS_INFO *subtn_res_info;
	MDS_SUBSCRIPTION_RESULTS_KEY subtn_res_key;

	m_MDS_ENTER();
	memset(&subtn_res_key, 0, sizeof(MDS_SUBSCRIPTION_RESULTS_KEY));

	subtn_res_key.svc_hdl = svc_hdl;
	subtn_res_key.sub_svc_id = subscr_svc_id;
	subtn_res_key.vdest_id = vdest_id;
	subtn_res_key.adest = 0;

	subtn_res_info = (MDS_SUBSCRIPTION_RESULTS_INFO *)ncs_patricia_tree_get(
	    &gl_mds_mcm_cb->subtn_results, (uint8_t *)&subtn_res_key);
	if (subtn_res_info == NULL) {

		/* Subscription result entry doesn't exist for active result */
		m_MDS_LOG_DBG("MDS:DB: Subscription Result not present");
		m_MDS_LEAVE();
		return NCSCC_RC_FAILURE;
	} else { /* Active or Await active entry exists */

		/* We will just make this one as active, and wont make current
		   active as standby or forced standby, as it will get
		   respective events seperately */

		/* Stop timer if running */
		if (subtn_res_info->info.active_vdest.active_route_info
			->tmr_running ==
		    true) { /* Present entry is Await Active Entry */

			/* Stop await active timer */

			/* Trun tmr_running flag off */
			subtn_res_info->info.active_vdest.active_route_info
			    ->tmr_running = false;
			/* Destroy Handle */
			ncshm_destroy_hdl(
			    NCS_SERVICE_ID_COMMON,
			    (uint32_t)subtn_res_info->info.active_vdest
				.active_route_info->tmr_req_info_hdl);
			/* Stop Timer */
			m_NCS_TMR_STOP(
			    subtn_res_info->info.active_vdest.active_route_info
				->await_active_tmr);
			/* Free tmr_req_info */
			m_MMGR_FREE_TMR_INFO(
			    subtn_res_info->info.active_vdest.active_route_info
				->tmr_req_info);
			subtn_res_info->info.active_vdest.active_route_info
			    ->last_active_svc_sub_part_ver = svc_sub_part_ver;
			/* Send Awaiting messages */
			mds_await_active_tbl_send(
			    subtn_res_info->info.active_vdest.active_route_info
				->await_active_queue,
			    active_result->key.adest, svc_hdl);

			/* Make await active header ptr null */
			subtn_res_info->info.active_vdest.active_route_info
			    ->await_active_queue = NULL;
		}

		/* Start pointing to New active provided */
		subtn_res_info->info.active_vdest.active_route_info
		    ->next_active_in_turn = active_result;
	}
	m_MDS_LEAVE();
	return NCSCC_RC_SUCCESS;
}

/*********************************************************
  Function NAME: mds_subtn_res_tbl_remove_active
*********************************************************/
uint32_t mds_subtn_res_tbl_remove_active(MDS_SVC_HDL svc_hdl,
					 MDS_SVC_ID subscr_svc_id,
					 MDS_VDEST_ID vdest_id)
{
	/* This will be called only when vdest is N-Way */

	MDS_SUBSCRIPTION_RESULTS_INFO *subtn_res_info;
	MDS_SUBSCRIPTION_RESULTS_KEY subtn_res_key;
	MDS_TMR_REQ_INFO *tmr_req_info = NULL;

	m_MDS_ENTER();
	memset(&subtn_res_key, 0, sizeof(MDS_SUBSCRIPTION_RESULTS_KEY));

	subtn_res_key.svc_hdl = svc_hdl;
	subtn_res_key.sub_svc_id = subscr_svc_id;
	subtn_res_key.vdest_id = vdest_id;
	subtn_res_key.adest = 0;

	subtn_res_info = (MDS_SUBSCRIPTION_RESULTS_INFO *)ncs_patricia_tree_get(
	    &gl_mds_mcm_cb->subtn_results, (uint8_t *)&subtn_res_key);
	if (subtn_res_info == NULL) {
		/* Subscription result entry doesn't exist for active result */
		m_MDS_LOG_DBG("MDS:DB: Subscription Result not present");
		m_MDS_LEAVE();
		return NCSCC_RC_FAILURE;
	} else {
		subtn_res_info->info.active_vdest.active_route_info
		    ->next_active_in_turn = NULL;
		/* Start Await Active timer */
		subtn_res_info->info.active_vdest.active_route_info
		    ->tmr_running = true;

		/* Start no active timer */
		tmr_req_info = m_MMGR_ALLOC_TMR_INFO;
		memset(tmr_req_info, 0, sizeof(MDS_TMR_REQ_INFO));
		tmr_req_info->type = MDS_AWAIT_ACTIVE_TMR;
		tmr_req_info->info.await_active_tmr_info.svc_hdl = svc_hdl;
		tmr_req_info->info.await_active_tmr_info.sub_svc_id =
		    subscr_svc_id;
		tmr_req_info->info.await_active_tmr_info.vdest_id = vdest_id;

		/* Store pointer of allocated tmr_req_info */
		subtn_res_info->info.active_vdest.active_route_info
		    ->tmr_req_info = tmr_req_info;

		/* Create handle */
		subtn_res_info->info.active_vdest.active_route_info
		    ->tmr_req_info_hdl = ncshm_create_hdl(
		    NCS_HM_POOL_ID_COMMON, NCS_SERVICE_ID_COMMON,
		    (NCSCONTEXT)tmr_req_info);

		m_NCS_TMR_START(
		    subtn_res_info->info.active_vdest.active_route_info
			->await_active_tmr,
		    MDS_AWAIT_ACTIVE_TMR_VAL, (TMR_CALLBACK)mds_tmr_callback,
		    (void *)(long)(subtn_res_info->info.active_vdest
				       .active_route_info->tmr_req_info_hdl));
		m_MDS_LOG_DBG(
		    "MCM_DB:RemoveActive:TimerStart:AwaitActiveTmri:Hdl=0x%08x:SvcHdl=%" PRIu64
		    ":sbscr-svcid=%s(%d),vdest=%d\n",
		    subtn_res_info->info.active_vdest.active_route_info
			->tmr_req_info_hdl,
		    svc_hdl, get_svc_names(subscr_svc_id), subscr_svc_id,
		    vdest_id);
	}
	m_MDS_LEAVE();
	return NCSCC_RC_SUCCESS;
}

/*********************************************************
  Function NAME: mds_subtn_res_tbl_add_active
*********************************************************/
uint32_t
mds_subtn_res_tbl_add_active(MDS_SVC_HDL svc_hdl, MDS_SVC_ID subscr_svc_id,
			     MDS_VDEST_ID vdest_id, NCS_VDEST_TYPE vdest_policy,
			     MDS_SUBSCRIPTION_RESULTS_INFO *active_result,
			     MDS_SVC_PVT_SUB_PART_VER svc_sub_part_ver,
			     MDS_SVC_ARCHWORD_TYPE archword_type)
{

	MDS_SUBSCRIPTION_RESULTS_INFO *subtn_res_info;
	MDS_SUBSCRIPTION_RESULTS_KEY subtn_res_key;
	MDS_ACTIVE_RESULT_INFO *active_info = NULL;

	m_MDS_ENTER();
	memset(&subtn_res_key, 0, sizeof(MDS_SUBSCRIPTION_RESULTS_KEY));

	subtn_res_key.svc_hdl = svc_hdl;
	subtn_res_key.sub_svc_id = subscr_svc_id;
	subtn_res_key.vdest_id = vdest_id;
	subtn_res_key.adest = 0;

	subtn_res_info = (MDS_SUBSCRIPTION_RESULTS_INFO *)ncs_patricia_tree_get(
	    &gl_mds_mcm_cb->subtn_results, (uint8_t *)&subtn_res_key);
	if (subtn_res_info != NULL) {
		/* Active Subscription result entry Already exist */
		m_MDS_LOG_INFO("MDS:DB: Active entry already present");
		m_MDS_LEAVE();
		return NCSCC_RC_FAILURE;
	} else {
		subtn_res_info = m_MMGR_ALLOC_SUBTN_RESULT_INFO;
		memset(subtn_res_info, 0,
		       sizeof(MDS_SUBSCRIPTION_RESULTS_INFO));

		memcpy(&subtn_res_info->key, &subtn_res_key,
		       sizeof(MDS_SUBSCRIPTION_RESULTS_KEY));
		subtn_res_info->node.key_info = (uint8_t *)&subtn_res_info->key;

		ncs_patricia_tree_add(
		    &gl_mds_mcm_cb->subtn_results,
		    (NCS_PATRICIA_NODE *)&subtn_res_info->node);

		/* Allocate active result info */
		active_info = m_MMGR_ALLOC_SUBTN_ACTIVE_RESULT_INFO;
		memset(active_info, 0, sizeof(MDS_ACTIVE_RESULT_INFO));

		/* point to currently added active guy as active */
		/* works for both n-way as well as mxn vdests */
		subtn_res_info->info.active_vdest.active_route_info =
		    active_info;
		active_info->next_active_in_turn = active_result;
		active_info->last_active_svc_sub_part_ver = svc_sub_part_ver;

		/* Create Await active timer (dont start, just create) */
		m_NCS_TMR_CREATE(active_info->await_active_tmr,
				 MDS_AWAIT_ACTIVE_TMR_VAL, mds_tmr_callback,
				 NULL);

		m_MDS_LOG_DBG("Await active tmr=0x%08x",
			      *(unsigned int *)active_info->await_active_tmr);

		if (vdest_policy == NCS_VDEST_TYPE_MxN) {
			active_info->dest_is_n_way = false;
		} else {
			active_info->dest_is_n_way = true;
		}
	}
	m_MDS_LEAVE();
	return NCSCC_RC_SUCCESS;
}

/*********************************************************
  Function NAME: mds_subtn_res_tbl_change_role
*********************************************************/
uint32_t mds_subtn_res_tbl_change_role(MDS_SVC_HDL svc_hdl,
				       MDS_SVC_ID subscr_svc_id,
				       MDS_VDEST_ID vdest_id, MDS_DEST adest,
				       V_DEST_RL role)
{

	MDS_SUBSCRIPTION_RESULTS_INFO *subtn_res_info;

	MDS_SUBSCRIPTION_RESULTS_KEY subtn_res_key;
	NCS_VDEST_TYPE local_vdest_policy;

	m_MDS_ENTER();
	memset(&subtn_res_key, 0, sizeof(MDS_SUBSCRIPTION_RESULTS_KEY));

	subtn_res_key.svc_hdl = svc_hdl;
	subtn_res_key.sub_svc_id = subscr_svc_id;
	subtn_res_key.vdest_id = vdest_id;
	subtn_res_key.adest = adest;

	/* Get policy of local vdest */
	mds_vdest_tbl_get_policy(vdest_id, &local_vdest_policy);

	subtn_res_info = (MDS_SUBSCRIPTION_RESULTS_INFO *)ncs_patricia_tree_get(
	    &gl_mds_mcm_cb->subtn_results, (uint8_t *)&subtn_res_key);
	if (subtn_res_info == NULL) {
		/* Subscription result entry doesn't exist for active result */
		m_MDS_LOG_DBG("MDS:DB: Subscription Result not present");
		m_MDS_LEAVE();
		return NCSCC_RC_FAILURE;
	} else {
		/* change role */
		subtn_res_info->info.vdest_inst.role = role;
	}
	m_MDS_LEAVE();
	return NCSCC_RC_SUCCESS;
}

/*********************************************************
  Function NAME: mds_subtn_res_tbl_get
*********************************************************/
uint32_t mds_subtn_res_tbl_get(MDS_SVC_HDL svc_hdl, MDS_SVC_ID subscr_svc_id,
			       MDS_VDEST_ID vdest_id, MDS_DEST *adest,
			       bool *tmr_running,
			       MDS_SUBSCRIPTION_RESULTS_INFO **result,
			       bool call_ref_val)
{
	uint32_t status = NCSCC_RC_SUCCESS;
	MDS_SUBSCRIPTION_RESULTS_INFO *subtn_res_info;
	MDS_SUBSCRIPTION_RESULTS_INFO *active_subtn_res_info;
	MDS_SUBSCRIPTION_RESULTS_INFO *next_active_result_info;
	MDS_SUBSCRIPTION_RESULTS_KEY subtn_res_key;

	m_MDS_ENTER();
	memset(&subtn_res_key, 0, sizeof(MDS_SUBSCRIPTION_RESULTS_KEY));

	subtn_res_key.svc_hdl = svc_hdl;
	subtn_res_key.sub_svc_id = subscr_svc_id;
	subtn_res_key.vdest_id = vdest_id;
	subtn_res_key.adest = 0;

	subtn_res_info = (MDS_SUBSCRIPTION_RESULTS_INFO *)ncs_patricia_tree_get(
	    &gl_mds_mcm_cb->subtn_results, (uint8_t *)&subtn_res_key);
	if (subtn_res_info == NULL) {
		/* Subscription result entry doesn't exist for active result */
		m_MDS_LOG_DBG("MDS:DB: Subscription Result not present");
		*adest = 0;
		m_MDS_LEAVE();
		return NCSCC_RC_FAILURE;
	} else {
		active_subtn_res_info =
		    subtn_res_info->info.active_vdest.active_route_info
			->next_active_in_turn;
		*tmr_running = subtn_res_info->info.active_vdest
				   .active_route_info->tmr_running;

		if (subtn_res_info->info.active_vdest.active_route_info
			->tmr_running == true) {
			*adest = 0;
			*result = subtn_res_info;
		} else {
			*adest = active_subtn_res_info->key.adest;
			*result = active_subtn_res_info;
		}

		if (call_ref_val == false &&
		    subtn_res_info->info.active_vdest.active_route_info
			    ->dest_is_n_way == true &&
		    subtn_res_info->info.active_vdest.active_route_info
			    ->tmr_running == false) {
			status = NCSCC_RC_SUCCESS;
			status = mds_subtn_res_tbl_query_next_active(
			    svc_hdl, subscr_svc_id, vdest_id,
			    active_subtn_res_info, &next_active_result_info);

			if (status == NCSCC_RC_FAILURE) {
				/* No other active present */
				/* Let it point to same old Active */
			} else {
				/* Some other active is present */
				/* Change to Active to point to next active */
				mds_subtn_res_tbl_change_active(
				    svc_hdl, subscr_svc_id, vdest_id,
				    next_active_result_info,
				    subtn_res_info->rem_svc_sub_part_ver,
				    subtn_res_info->rem_svc_arch_word);
			}
		}
	}
	m_MDS_LEAVE();
	return NCSCC_RC_SUCCESS;
}

/*********************************************************
  Function NAME: mds_subtn_res_tbl_get_by_adest
*********************************************************/
uint32_t mds_subtn_res_tbl_get_by_adest(MDS_SVC_HDL svc_hdl,
					MDS_SVC_ID subscr_svc_id,
					MDS_VDEST_ID vdest_id, MDS_DEST adest,
					V_DEST_RL *o_role,
					MDS_SUBSCRIPTION_RESULTS_INFO **result)
{

	MDS_SUBSCRIPTION_RESULTS_INFO *subtn_res_info = NULL;
	MDS_SUBSCRIPTION_RESULTS_KEY subtn_res_key;

	m_MDS_ENTER();
	memset(&subtn_res_key, 0, sizeof(MDS_SUBSCRIPTION_RESULTS_KEY));

	subtn_res_key.svc_hdl = svc_hdl;
	subtn_res_key.sub_svc_id = subscr_svc_id;
	subtn_res_key.vdest_id = vdest_id;
	subtn_res_key.adest = adest;

	subtn_res_info = (MDS_SUBSCRIPTION_RESULTS_INFO *)ncs_patricia_tree_get(
	    &gl_mds_mcm_cb->subtn_results, (uint8_t *)&subtn_res_key);
	if (subtn_res_info == NULL) {
		/* Subscription result entry doesn't exist for active result */
		m_MDS_LOG_DBG("MDS:DB: Subscription Result not present");
		m_MDS_LEAVE();
		return NCSCC_RC_FAILURE;
	} else {
		*o_role = subtn_res_info->info.vdest_inst.role;
		*result = subtn_res_info;
	}
	m_MDS_LEAVE();
	return NCSCC_RC_SUCCESS;
}

/*********************************************************
  Function NAME: mds_subtn_res_tbl_getnext_active
*********************************************************/
uint32_t
mds_subtn_res_tbl_getnext_active(MDS_SVC_HDL svc_hdl, MDS_SVC_ID subscr_svc_id,
				 MDS_SUBSCRIPTION_RESULTS_INFO **result)
{ /* use for bcast */

	MDS_SUBSCRIPTION_RESULTS_INFO *subtn_res_info;

	m_MDS_ENTER();
	if (*result == NULL) {
		subtn_res_info =
		    (MDS_SUBSCRIPTION_RESULTS_INFO *)ncs_patricia_tree_getnext(
			&gl_mds_mcm_cb->subtn_results, (uint8_t *)NULL);
	} else {
		subtn_res_info =
		    (MDS_SUBSCRIPTION_RESULTS_INFO *)ncs_patricia_tree_getnext(
			&gl_mds_mcm_cb->subtn_results,
			(uint8_t *)&((*result)->key));
	}

	while (subtn_res_info != NULL) {
		if (svc_hdl == subtn_res_info->key.svc_hdl &&
		    subscr_svc_id == subtn_res_info->key.sub_svc_id &&
		    V_DEST_RL_ACTIVE == subtn_res_info->info.vdest_inst.role) {
			if (subtn_res_info->key.adest ==
			    0) { /* Entry is Active/Await Active result entry */
				if (subtn_res_info->info.active_vdest
					.active_route_info->tmr_running ==
				    true) { /* It is Await Active entry */
					*result = subtn_res_info;
					m_MDS_LEAVE();
					return NCSCC_RC_SUCCESS;
				}
			} else {
				*result = subtn_res_info;
				m_MDS_LEAVE();
				return NCSCC_RC_SUCCESS;
			}
		}
		subtn_res_info =
		    (MDS_SUBSCRIPTION_RESULTS_INFO *)ncs_patricia_tree_getnext(
			&gl_mds_mcm_cb->subtn_results,
			(uint8_t *)&(subtn_res_info->key));
	}
	m_MDS_LOG_DBG("MDS:DB: Active Result not present");
	m_MDS_LEAVE();
	return NCSCC_RC_FAILURE;
}

/*********************************************************
  Function NAME: mds_subtn_res_tbl_getnext_any
*********************************************************/
uint32_t mds_subtn_res_tbl_getnext_any(MDS_SVC_HDL svc_hdl,
				       MDS_SVC_ID subscr_svc_id,
				       MDS_SUBSCRIPTION_RESULTS_INFO **result)
{ /* use for bcast */

	MDS_SUBSCRIPTION_RESULTS_INFO *subtn_res_info;

	m_MDS_ENTER();
	if (*result == NULL) {
		subtn_res_info =
		    (MDS_SUBSCRIPTION_RESULTS_INFO *)ncs_patricia_tree_getnext(
			&gl_mds_mcm_cb->subtn_results, (uint8_t *)NULL);
	} else {
		subtn_res_info =
		    (MDS_SUBSCRIPTION_RESULTS_INFO *)ncs_patricia_tree_getnext(
			&gl_mds_mcm_cb->subtn_results,
			(uint8_t *)&((*result)->key));
	}

	while (subtn_res_info != NULL) {
		if (svc_hdl == subtn_res_info->key.svc_hdl &&
		    subscr_svc_id == subtn_res_info->key.sub_svc_id &&
		    subtn_res_info->key.adest != 0) {
			*result = subtn_res_info;
			m_MDS_LEAVE();
			return NCSCC_RC_SUCCESS;
		}
		subtn_res_info =
		    (MDS_SUBSCRIPTION_RESULTS_INFO *)ncs_patricia_tree_getnext(
			&gl_mds_mcm_cb->subtn_results,
			(uint8_t *)&(subtn_res_info->key));
	}
	m_MDS_LOG_DBG("MDS:DB: Subscription Result not present");
	m_MDS_LEAVE();
	return NCSCC_RC_FAILURE;
}

/*********************************************************
  Function NAME: mds_subtn_res_tbl_getnext_by_adest
*********************************************************/

/*used for giving down to all svc on adest going down*/

uint32_t
mds_subtn_res_tbl_getnext_by_adest(MDS_DEST adest,
				   MDS_SUBSCRIPTION_RESULTS_KEY *key,
				   MDS_SUBSCRIPTION_RESULTS_INFO **ret_result)
{

	MDS_SUBSCRIPTION_RESULTS_INFO *subtn_res_info;

	m_MDS_ENTER();
	subtn_res_info =
	    (MDS_SUBSCRIPTION_RESULTS_INFO *)ncs_patricia_tree_getnext(
		&gl_mds_mcm_cb->subtn_results, (uint8_t *)key);

	while (subtn_res_info != NULL) {

		if (subtn_res_info->key.adest == adest) {
			*ret_result = subtn_res_info; /* return subtn result */
			m_MDS_LEAVE();
			return NCSCC_RC_SUCCESS;
		}

		key = &subtn_res_info->key;

		subtn_res_info =
		    (MDS_SUBSCRIPTION_RESULTS_INFO *)ncs_patricia_tree_getnext(
			&gl_mds_mcm_cb->subtn_results, (uint8_t *)key);
	}

	*ret_result = NULL;
	m_MDS_LOG_DBG("MDS:DB: End of Subscription result table");
	m_MDS_LEAVE();

	return NCSCC_RC_FAILURE;
}

/*********************************************************
  Function NAME: mds_subtn_res_tbl_query_next_active
*********************************************************/
uint32_t mds_subtn_res_tbl_query_next_active(
    MDS_SVC_HDL svc_hdl, MDS_SVC_ID sub_svc_id, MDS_VDEST_ID vdest_id,
    MDS_SUBSCRIPTION_RESULTS_INFO *current_active_result,
    MDS_SUBSCRIPTION_RESULTS_INFO **next_active_result)
{
	/* Called only when vdest in N-Way */

	MDS_SUBSCRIPTION_RESULTS_INFO *subtn_res_info;

	m_MDS_ENTER();
	/* Starting from current active till end of tree */
	if (current_active_result == NULL) {
		subtn_res_info =
		    (MDS_SUBSCRIPTION_RESULTS_INFO *)ncs_patricia_tree_getnext(
			&gl_mds_mcm_cb->subtn_results, (uint8_t *)NULL);
	} else {
		subtn_res_info =
		    (MDS_SUBSCRIPTION_RESULTS_INFO *)ncs_patricia_tree_getnext(
			&gl_mds_mcm_cb->subtn_results,
			(uint8_t *)&current_active_result->key);
	}

	while (subtn_res_info != NULL) {
		if (svc_hdl == subtn_res_info->key.svc_hdl &&
		    sub_svc_id == subtn_res_info->key.sub_svc_id &&
		    vdest_id == subtn_res_info->key.vdest_id &&
		    V_DEST_RL_ACTIVE == subtn_res_info->info.vdest_inst.role) {
			*next_active_result = subtn_res_info;
			m_MDS_LEAVE();
			return NCSCC_RC_SUCCESS;
		}
		subtn_res_info =
		    (MDS_SUBSCRIPTION_RESULTS_INFO *)ncs_patricia_tree_getnext(
			&gl_mds_mcm_cb->subtn_results,
			(uint8_t *)&subtn_res_info->key);
	}

	/* Starting from start of tree till Current active */

	if (current_active_result ==
	    NULL) { /* Searching started from first node of tree itself */
		m_MDS_LOG_DBG("MDS:DB: Subscription Result not present");
		m_MDS_LEAVE();
		return NCSCC_RC_FAILURE;
	}

	subtn_res_info =
	    (MDS_SUBSCRIPTION_RESULTS_INFO *)ncs_patricia_tree_getnext(
		&gl_mds_mcm_cb->subtn_results, (uint8_t *)NULL);

	while (subtn_res_info != NULL ||
	       subtn_res_info == current_active_result) {
		if (svc_hdl == subtn_res_info->key.svc_hdl &&
		    sub_svc_id == subtn_res_info->key.sub_svc_id &&
		    vdest_id == subtn_res_info->key.vdest_id &&
		    V_DEST_RL_ACTIVE == subtn_res_info->info.vdest_inst.role) {
			*next_active_result = subtn_res_info;
			m_MDS_LEAVE();
			return NCSCC_RC_SUCCESS;
		}
		subtn_res_info =
		    (MDS_SUBSCRIPTION_RESULTS_INFO *)ncs_patricia_tree_getnext(
			&gl_mds_mcm_cb->subtn_results,
			(uint8_t *)&subtn_res_info->key);
	}
	m_MDS_LOG_DBG("MDS:DB: Subscription Result not present");
	m_MDS_LEAVE();
	return NCSCC_RC_FAILURE;
}

/*********************************************************
  Function NAME: mds_subtn_res_tbl_del_all
*********************************************************/
uint32_t mds_subtn_res_tbl_del_all(MDS_SVC_HDL svc_hdl, MDS_SVC_ID sub_svc_id)
{
	MDS_SUBSCRIPTION_RESULTS_INFO *subtn_res_info;
	MDS_SUBSCRIPTION_RESULTS_KEY subtn_res_key;

	m_MDS_ENTER();
	memset(&subtn_res_key, 0, sizeof(MDS_SUBSCRIPTION_RESULTS_KEY));

	subtn_res_info =
	    (MDS_SUBSCRIPTION_RESULTS_INFO *)ncs_patricia_tree_getnext(
		&gl_mds_mcm_cb->subtn_results, (uint8_t *)NULL);

	while (subtn_res_info != NULL) {
		if (svc_hdl == subtn_res_info->key.svc_hdl &&
		    sub_svc_id == subtn_res_info->key.sub_svc_id) {

			/* Delete this entry */
			if (subtn_res_info->key.adest == 0) {
				/* This is active result entry */
				if (subtn_res_info->info.active_vdest
					.active_route_info->tmr_running ==
				    true) {
					/* Destroy Handle */
					ncshm_destroy_hdl(
					    NCS_SERVICE_ID_COMMON,
					    (uint32_t)subtn_res_info->info
						.active_vdest.active_route_info
						->tmr_req_info_hdl);
					/* Stop Timer */
					m_NCS_TMR_STOP(
					    subtn_res_info->info.active_vdest
						.active_route_info
						->await_active_tmr);
					/* Free tmr_req_info */
					m_MMGR_FREE_TMR_INFO(
					    subtn_res_info->info.active_vdest
						.active_route_info
						->tmr_req_info);

					/* Fixed, Free the memory of
					 * await_active_queue : modified */
					mds_await_active_tbl_del(
					    subtn_res_info->info.active_vdest
						.active_route_info
						->await_active_queue);
				}

				m_NCS_TMR_DESTROY(
				    subtn_res_info->info.active_vdest
					.active_route_info->await_active_tmr);

				/* Free active result info */
				m_MMGR_FREE_SUBTN_ACTIVE_RESULT_INFO(
				    subtn_res_info->info.active_vdest
					.active_route_info);
			}

			/* Delete entry from tree */
			ncs_patricia_tree_del(
			    &gl_mds_mcm_cb->subtn_results,
			    (NCS_PATRICIA_NODE *)subtn_res_info);

			/* Store the key for getting next node in tree */
			memcpy(&subtn_res_key, &subtn_res_info->key,
			       sizeof(MDS_SUBSCRIPTION_RESULTS_KEY));

			/* Free subscription entry */
			m_MMGR_FREE_SUBTN_RESULT_INFO(subtn_res_info);

			subtn_res_info = (MDS_SUBSCRIPTION_RESULTS_INFO *)
			    ncs_patricia_tree_getnext(
				&gl_mds_mcm_cb->subtn_results,
				(uint8_t *)&subtn_res_key);
			continue;
		}
		/* Get next entry */
		subtn_res_info =
		    (MDS_SUBSCRIPTION_RESULTS_INFO *)ncs_patricia_tree_getnext(
			&gl_mds_mcm_cb->subtn_results,
			(uint8_t *)&subtn_res_info->key);
	}
	m_MDS_LEAVE();
	return NCSCC_RC_SUCCESS;
}

/*********************************************************
  Function NAME: mds_subtn_res_tbl_cleanup
*********************************************************/
uint32_t mds_subtn_res_tbl_cleanup(void)
{

	MDS_SUBSCRIPTION_RESULTS_INFO *subtn_res_info;
	MDS_SUBSCRIPTION_RESULTS_KEY subtn_res_key;

	m_MDS_ENTER();
	memset(&subtn_res_key, 0, sizeof(MDS_SUBSCRIPTION_RESULTS_KEY));

	subtn_res_info =
	    (MDS_SUBSCRIPTION_RESULTS_INFO *)ncs_patricia_tree_getnext(
		&gl_mds_mcm_cb->subtn_results, (uint8_t *)NULL);

	while (subtn_res_info != NULL) {
		/* Delete this entry */
		if (subtn_res_info->key.adest == 0) {
			/* This is active result entry */
			if (subtn_res_info->info.active_vdest.active_route_info
				->tmr_running == true) {
				/* Stop Timer */
				m_NCS_TMR_STOP(
				    subtn_res_info->info.active_vdest
					.active_route_info->await_active_tmr);
				/* Destroy Handle */
				ncshm_destroy_hdl(
				    NCS_SERVICE_ID_COMMON,
				    (uint32_t)subtn_res_info->info.active_vdest
					.active_route_info->tmr_req_info_hdl);
				/* Free tmr_req_info */
				m_MMGR_FREE_TMR_INFO(
				    subtn_res_info->info.active_vdest
					.active_route_info->tmr_req_info);

				/*  Fixed, Free the memory of await_active_queue
				 * : modified */
				mds_await_active_tbl_del(
				    subtn_res_info->info.active_vdest
					.active_route_info->await_active_queue);
			}
			/* Destroy Timer */
			m_NCS_TMR_DESTROY(
			    subtn_res_info->info.active_vdest.active_route_info
				->await_active_tmr);

			/* Free active result info */
			m_MMGR_FREE_SUBTN_ACTIVE_RESULT_INFO(
			    subtn_res_info->info.active_vdest
				.active_route_info);
		}

		/* Store the key for getting next node in tree */
		memcpy(&subtn_res_key, &subtn_res_info->key,
		       sizeof(MDS_SUBSCRIPTION_RESULTS_KEY));

		/* Delete entry from tree */
		ncs_patricia_tree_del(&gl_mds_mcm_cb->subtn_results,
				      (NCS_PATRICIA_NODE *)subtn_res_info);

		/* Free subscription entry */
		m_MMGR_FREE_SUBTN_RESULT_INFO(subtn_res_info);

		subtn_res_info =
		    (MDS_SUBSCRIPTION_RESULTS_INFO *)ncs_patricia_tree_getnext(
			&gl_mds_mcm_cb->subtn_results,
			(uint8_t *)&subtn_res_key);
	}
	m_MDS_LEAVE();
	return NCSCC_RC_SUCCESS;
}

/****************************************************************************/
/* Process info database, stores mapping between mdsdest and pid
 * Only used by some services. Tree itself cannot be located in control block
 * which is dynamically allocated.
 */

static NCS_PATRICIA_TREE process_info_db; /* all known local MDS dests */

MDS_PROCESS_INFO *mds_process_info_get(MDS_DEST mds_dest, NCSMDS_SVC_ID svc_id)
{
	if (process_info_db.n_nodes > 0) {
		uint64_t key = ((mds_dest & 0xffffffff) << 32) | svc_id;
		return (MDS_PROCESS_INFO *)ncs_patricia_tree_get(
		    &process_info_db, (uint8_t *)&key);
	}
	return NULL;
}

int mds_process_info_add(MDS_PROCESS_INFO *info)
{
	TRACE_ENTER2("dest:%" PRIx64 ", pid:%d, svc:%d", info->mds_dest,
		     info->pid, info->svc_id);
	info->key = ((info->mds_dest & 0xffffffff) << 32) | info->svc_id;
	info->patnode.key_info = (uint8_t *)&info->key;
	int rc = ncs_patricia_tree_add(&process_info_db,
				       (NCS_PATRICIA_NODE *)&info->patnode);
	return rc;
}

int mds_process_info_del(MDS_PROCESS_INFO *info)
{
	TRACE_ENTER2("dest:%" PRIx64 ", pid:%d, svc:%d", info->mds_dest,
		     info->pid, info->svc_id);
	int rc = ncs_patricia_tree_del(&process_info_db,
				       (NCS_PATRICIA_NODE *)&info->patnode);
	return rc;
}

int mds_process_info_db_init(void)
{
	NCS_PATRICIA_PARAMS pat_tree_params = {0};

	/* locking not needed */
	pat_tree_params.key_size = sizeof(uint64_t);
	if (NCSCC_RC_SUCCESS !=
	    ncs_patricia_tree_init(&process_info_db, &pat_tree_params)) {
		syslog(LOG_ERR, "MDS:DB %s: patricia_tree_init failed",
		       __FUNCTION__);
		return NCSCC_RC_FAILURE;
	}

	return NCSCC_RC_SUCCESS;
}

/*********************************************************
  Function NAME: mds_mcm_cleanup
*********************************************************/
uint32_t mds_mcm_cleanup(void)
{
	/* Call Patricia tree specific cleanup function */
	mds_subtn_res_tbl_cleanup();
	mds_svc_tbl_cleanup();
	mds_vdest_tbl_cleanup();

	return NCSCC_RC_SUCCESS;
}

/*********************************************************
  Function NAME: ncsmds_pp
*********************************************************/
#ifdef NCS_DEBUG

void ncsmds_pp()
{

	MDS_VDEST_INFO *vdest_info = NULL;
	MDS_PWE_INFO *pwe_info = NULL;
	MDS_SVC_INFO *svc_info = NULL;
	MDS_SUBSCRIPTION_INFO *subtn_info = NULL;
	MDS_SUBSCRIPTION_RESULTS_INFO *subtn_res_info = NULL;
	MDS_SUBSCRIPTION_RESULTS_KEY subtn_res_key;
	char policy, role, install_scope, subtn_scope, subtn_view, subtn_type,
	    subtn_res_role;

	m_MDS_ENTER();
	memset(&subtn_res_key, 0, sizeof(MDS_SUBSCRIPTION_RESULTS_KEY));

	printf("\n\n  ==> M D S  P r e t t y  P r i n t <==\n\n");

	vdest_info = (MDS_VDEST_INFO *)ncs_patricia_tree_getnext(
	    &gl_mds_mcm_cb->vdest_list, (uint8_t *)NULL);
	while (vdest_info != NULL) {
		printf("\n\n\n\n\n ::::::: VDEST INFO ::::::: \n\n");
		printf(
		    "\n| VDEST ID | SUBTN REF VAL | POLICY | ROLE | TMR_RUNNING |\n");

		if (vdest_info->policy == NCS_VDEST_TYPE_MxN)
			policy = 'M';
		else
			policy = 'N';

		if (vdest_info->role == V_DEST_RL_ACTIVE)
			role = 'A';
		else if (vdest_info->role == V_DEST_RL_STANDBY)
			role = 'S';
		else
			role = 'Q';

		printf("|  %5d   |  %10lld  |    %c   |   %c  |    %5d    |\n",
		       vdest_info->vdest_id,
		       (long long)(vdest_info->subtn_ref_val), policy, role,
		       vdest_info->tmr_running);
		printf(
		    "\n|-----------------------------------------------------------------\n");

		pwe_info = vdest_info->pwe_list;
		while (pwe_info != NULL) {
			printf("\n ::::::: PWE INFO ::::::: \n\n");
			printf("\n| PWE-ID |\n");
			printf("|  %5d |\n", pwe_info->pwe_id);

			svc_info = (MDS_SVC_INFO *)ncs_patricia_tree_getnext(
			    &gl_mds_mcm_cb->svc_list, (uint8_t *)NULL);
			while (svc_info != NULL) {
				if (m_MDS_GET_PWE_ID_FROM_SVC_HDL(
					svc_info->svc_hdl) ==
					pwe_info->pwe_id &&
				    m_MDS_GET_VDEST_ID_FROM_SVC_HDL(
					svc_info->svc_hdl) ==
					vdest_info->vdest_id) {

					if (svc_info->install_scope ==
					    NCSMDS_SCOPE_NONE)
						install_scope = 'O';
					else if (svc_info->install_scope ==
						 NCSMDS_SCOPE_INTRANODE)
						install_scope = 'N';
					else if (svc_info->install_scope ==
						 NCSMDS_SCOPE_INTRACHASSIS)
						install_scope = 'C';
					else {
						/* Added as part of warning fix
						 */
						install_scope = '?';
					}

					printf(
					    "\n ::::::: SVC INFO ::::::: \n\n");
					printf(
					    "\n| SVC-ID |       SVC_HDL        | INSTALL-SCOPE | YR-SVC_HDL | Q_OWN | SEQ_NO |\n");
					printf(
					    "|  %5d | %20lld |        %c      |  %8x  | %5d |  %5d |\n",
					    svc_info->svc_id, svc_info->svc_hdl,
					    install_scope,
					    (int)svc_info->yr_svc_hdl,
					    svc_info->q_ownership,
					    svc_info->seq_no);

					/* Subscriptions */

					subtn_info = svc_info->subtn_info;
					while (subtn_info != NULL) {
						if (subtn_info->scope ==
						    NCSMDS_SCOPE_NONE)
							subtn_scope = 'O';
						else if (subtn_info->scope ==
							 NCSMDS_SCOPE_INTRANODE)
							subtn_scope = 'N';
						else if (
						    subtn_info->scope ==
						    NCSMDS_SCOPE_INTRACHASSIS)
							subtn_scope = 'C';
						else {
							/* Added as part of
							 * warning fix */
							subtn_scope = '?';
						}

						if (subtn_info->view ==
						    MDS_VIEW_NORMAL)
							subtn_view = 'N';
						else
							subtn_view = 'R';

						if (subtn_info->subtn_type ==
						    MDS_SUBTN_IMPLICIT)
							subtn_type = 'I';
						else
							subtn_type = 'E';

						printf(
						    "\n\n ::::::: SUBSCRIPTION INFO ::::::: \n");
						printf(
						    "\n| SUB_SVC_ID | SCOPE | VIEW | SUBTN_TYPE | SUBTN_HDL | TMR |\n");
						printf(
						    "|    %5d   |   %c   |   %c  |     %c      |  %8lld | %3d |\n",
						    subtn_info->sub_svc_id,
						    subtn_scope, subtn_view,
						    subtn_type,
						    (long long)subtn_info
							->subscr_req_hdl,
						    subtn_info->tmr_flag);

						printf(
						    "\n ::::::: SUBSCRIPTION RESULT INFO ::::::: \n");
						printf(
						    "\n| VDEST_ID |   NODE_ID  | PROCESS_ID | ROLE |\n");

						subtn_res_key.svc_hdl =
						    svc_info->svc_hdl;
						subtn_res_key.sub_svc_id =
						    subtn_info->sub_svc_id;
						subtn_res_key.vdest_id = 0;
						subtn_res_key.adest = 0;

						subtn_res_info =
						    (MDS_SUBSCRIPTION_RESULTS_INFO
							 *)
							ncs_patricia_tree_getnext(
							    &gl_mds_mcm_cb
								 ->subtn_results,
							    (uint8_t
								 *)&subtn_res_key);
						while (subtn_res_info != NULL) {
							if (subtn_res_info->key
								    .svc_hdl !=
								svc_info
								    ->svc_hdl ||
							    subtn_res_info->key
								    .sub_svc_id !=
								subtn_info
								    ->sub_svc_id) {
								break;
							}

							if (subtn_res_info->key
								.adest ==
							    0) { /* This is
								    active
								    instance
								    entry */
								if (subtn_res_info
									->info
									.active_vdest
									.active_route_info
									->tmr_running ==
								    false) {
									printf(
									    "|CUR_ACTIVE| 0x%08x | %10d |######|\n",
									    m_MDS_GET_NODE_ID_FROM_ADEST(
										subtn_res_info
										    ->info
										    .active_vdest
										    .active_route_info
										    ->next_active_in_turn
										    ->key
										    .adest),
									    m_MDS_GET_PROCESS_ID_FROM_ADEST(
										subtn_res_info
										    ->info
										    .active_vdest
										    .active_route_info
										    ->next_active_in_turn
										    ->key
										    .adest));
								}
							} else if (
							    subtn_res_info->key
								.vdest_id ==
							    m_VDEST_ID_FOR_ADEST_ENTRY) { /* This is ADEST entry */
								printf(
								    "|  ADEST   | 0x%08x | %10d |   A  |\n",
								    m_MDS_GET_NODE_ID_FROM_ADEST(
									subtn_res_info
									    ->key
									    .adest),
								    m_MDS_GET_PROCESS_ID_FROM_ADEST(
									subtn_res_info
									    ->key
									    .adest));
							} else { /* This is
								    VDEST entry
								  */

								if (subtn_res_info
									->info
									.vdest_inst
									.role ==
								    V_DEST_RL_ACTIVE)
									subtn_res_role =
									    'A';
								else
									subtn_res_role =
									    'S';

								printf(
								    "|  %5d   | 0x%08x | %10d |   %c  |\n",
								    subtn_res_info
									->key
									.vdest_id,
								    m_MDS_GET_NODE_ID_FROM_ADEST(
									subtn_res_info
									    ->key
									    .adest),
								    m_MDS_GET_PROCESS_ID_FROM_ADEST(
									subtn_res_info
									    ->key
									    .adest),
								    subtn_res_role);
							}

							subtn_res_key.vdest_id =
							    subtn_res_info->key
								.vdest_id;
							subtn_res_key.adest =
							    subtn_res_info->key
								.adest;

							subtn_res_info = (MDS_SUBSCRIPTION_RESULTS_INFO
									      *)
							    ncs_patricia_tree_getnext(
								&gl_mds_mcm_cb
								     ->subtn_results,
								(uint8_t
								     *)&subtn_res_key);
						}

						subtn_info = subtn_info->next;
					}
				}
				svc_info =
				    (MDS_SVC_INFO *)ncs_patricia_tree_getnext(
					&gl_mds_mcm_cb->svc_list,
					(uint8_t *)&svc_info->svc_hdl);
			}

			pwe_info = pwe_info->next_pwe;
		}

		vdest_info = (MDS_VDEST_INFO *)ncs_patricia_tree_getnext(
		    &gl_mds_mcm_cb->vdest_list,
		    (uint8_t *)&vdest_info->vdest_id);
		printf(
		    "\n|-----------------------------------------------------------------\n");
	}
	m_MDS_LEAVE();
	return;
}

#endif
