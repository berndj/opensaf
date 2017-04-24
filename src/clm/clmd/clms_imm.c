/*      -*- OpenSAF  -*-
 *
 * (C) Copyright 2010 The OpenSAF Foundation
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
 * Author(s):  Emerson Network Power
 */

#include "imm/saf/saImmOm.h"
#include "imm/saf/saImmOi.h"

#include "clms.h"
#include "base/osaf_extended_name.h"
#include "base/osaf_utility.h"
#include "base/osaf_time.h"

extern struct ImmutilWrapperProfile immutilWrapperProfile;

void clms_all_node_rattr_update(void);
static SaBoolT clms_is_any_rtu_pending(void);
SaAisErrorT clms_node_ccb_comp_cb(CcbUtilOperationData_t *opdata);
uint32_t clms_imm_node_unlock(CLMS_CLUSTER_NODE *nodeop);
uint32_t clms_imm_node_lock(CLMS_CLUSTER_NODE *nodeop);
uint32_t clms_imm_node_shutdown(CLMS_CLUSTER_NODE *nodeop);
static void clms_lock_send_start_cbk(CLMS_CLUSTER_NODE *nodeop);
static void clms_timer_ipc_send(SaNameT node_name);
static uint32_t clms_lock_send_no_start_cbk(CLMS_CLUSTER_NODE *nodeop);

static SaVersionT immVersion = {'A', 2, 1};

const unsigned int sleep_delay_ms = 500;
const unsigned int max_waiting_time_ms = 60 * 1000; /* 60 seconds */

/**
 * Initialize the track response patricia tree for the node
 * @param[in] node node to initialize trackresponse tree
 */
void clms_trackresp_patricia_init(CLMS_CLUSTER_NODE *node)
{
	NCS_PATRICIA_PARAMS trackresp_param;
	TRACE_ENTER();

	memset(&trackresp_param, 0, sizeof(NCS_PATRICIA_PARAMS));
	trackresp_param.key_size = sizeof(SaInvocationT);

	if (NCSCC_RC_SUCCESS !=
	    ncs_patricia_tree_init(&node->trackresp, &trackresp_param)) {
		LOG_ER("patricia tree init failed for node's trackresp tree");
		osafassert(0);
	}

	TRACE_LEAVE();
}

/**
 * Become object and class implementer.
 * @param[in] _cb
 * @return void*
 */
static void *imm_impl_set_node_down_proc(void *_cb)
{
	SaAisErrorT rc;
	CLMS_CB *cb = (CLMS_CB *)_cb;
	int msecs_waited;

	TRACE_ENTER();

	/* Update IMM */

	msecs_waited = 0;
	rc = saImmOiImplementerSet(cb->immOiHandle, IMPLEMENTER_NAME);
	while (((rc == SA_AIS_ERR_TRY_AGAIN) || (rc == SA_AIS_ERR_EXIST)) &&
	       (msecs_waited < max_waiting_time_ms)) {
		usleep(sleep_delay_ms * 1000);
		msecs_waited += sleep_delay_ms;
		rc = saImmOiImplementerSet(cb->immOiHandle, IMPLEMENTER_NAME);
	}
	if (rc != SA_AIS_OK) {
		/* We have tried enough, now just exit */
		LOG_ER("saImmOiImplementerSet failed, rc = %u", rc);
		exit(EXIT_FAILURE);
	}

	msecs_waited = 0;
	rc = saImmOiClassImplementerSet(cb->immOiHandle, "SaClmNode");
	while (((rc == SA_AIS_ERR_TRY_AGAIN) || (rc == SA_AIS_ERR_EXIST)) &&
	       (msecs_waited < max_waiting_time_ms)) {
		usleep(sleep_delay_ms * 1000);
		msecs_waited += sleep_delay_ms;
		rc = saImmOiClassImplementerSet(cb->immOiHandle, "SaClmNode");
	}
	if (rc != SA_AIS_OK) {
		LOG_ER(
		    "saImmOiClassImplementerSet failed for class SaClmNode, rc = %u",
		    rc);
		exit(EXIT_FAILURE);
	}

	msecs_waited = 0;
	rc = saImmOiClassImplementerSet(cb->immOiHandle, "SaClmCluster");
	while (((rc == SA_AIS_ERR_TRY_AGAIN) || (rc == SA_AIS_ERR_EXIST)) &&
	       (msecs_waited < max_waiting_time_ms)) {
		usleep(sleep_delay_ms * 1000);
		msecs_waited += sleep_delay_ms;
		rc =
		    saImmOiClassImplementerSet(cb->immOiHandle, "SaClmCluster");
	}
	if (rc != SA_AIS_OK) {
		LOG_ER(
		    "saImmOiClassImplementerSet failed for class SaClmCluster, rc = %u,",
		    rc);
		exit(EXIT_FAILURE);
	}

	cb->is_impl_set = true;

	TRACE_LEAVE();
	return NULL;
}

/**
 * Lookup the client db to find if any subscribers exist for the VALIDATE/START
 * step
 * @return NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE
 */
uint32_t clms_chk_sub_exist(SaUint8T track_flag)
{

	CLMS_CLIENT_INFO *rec = NULL;
	uint32_t client_id = 0;

	TRACE_ENTER();

	while (NULL != (rec = clms_client_getnext_by_id(client_id))) {
		client_id = rec->client_id;

		if (rec->track_flags & (track_flag))
			return NCSCC_RC_SUCCESS;
	}

	TRACE_LEAVE();

	return NCSCC_RC_FAILURE;
}

/**
 * Become object and class implementer, non-blocking.
 * @param cb
 */
void clms_imm_impl_set(CLMS_CB *cb)
{
	pthread_t thread;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	TRACE_ENTER();

	if (pthread_create(&thread, &attr, imm_impl_set_node_down_proc, cb) !=
	    0) {
		LOG_ER("pthread_create FAILED: %s, exiting", strerror(errno));
		exit(EXIT_FAILURE);
	}

	TRACE_LEAVE();
}

/**
 * Create the new node
 * @param[in] name  DN name of the node to create
 * @param[in] attrs node's attributes
 * @return Cluster node
 */
CLMS_CLUSTER_NODE *clms_node_new(SaNameT *name,
				 const SaImmAttrValuesT_2 **attrs)
{
	CLMS_CLUSTER_NODE *node = NULL;
	const SaImmAttrValuesT_2 *attr;
	int i = 0;

	TRACE_ENTER();

	node = (CLMS_CLUSTER_NODE *)malloc(sizeof(CLMS_CLUSTER_NODE));
	if (node == NULL) {
		LOG_ER("Calloc failed");
		goto done;
	}

	memset(node, 0, sizeof(CLMS_CLUSTER_NODE));
	/*Initialize some attributes of the node like */
	memcpy(node->node_name.value, name->value, name->length);
	node->node_name.length = name->length;
	node->node_addr.family = 1;
	node->admin_state = SA_CLM_ADMIN_UNLOCKED;
	node->rtu_pending = false;
	node->admin_rtu_pending = false;

	TRACE("RTU pending flag is switched off");
	TRACE("nodename %s", node->node_name.value);

	while ((attr = attrs[i++]) != NULL) {
		void *value;

		if (attr->attrValuesNumber == 0)
			continue;

		value = attr->attrValues[0];
		TRACE("Inside the while loop attrname %s", attr->attrName);

		if (!strcmp(attr->attrName, "saClmNodeAdminState")) {
			node->admin_state = *((SaUint32T *)value);

		} else if (!strcmp(attr->attrName,
				   "saClmNodeLockCallbackTimeout")) {
			node->lck_cbk_timeout = *((SaTimeT *)value);

		} else if (!strcmp(attr->attrName, "saClmNodeDisableReboot")) {
			node->disable_reboot = *((SaUint32T *)value);

		} else if (!strcmp(attr->attrName, "saClmNodeAddressFamily")) {
			node->node_addr.family = *((SaUint32T *)value);

		} else if (!strcmp(attr->attrName, "saClmNodeAddress")) {
			node->node_addr.length =
			    (SaUint16T)strlen(*((char **)value));
			strncpy((char *)node->node_addr.value,
				*((char **)value), node->node_addr.length);

		} else if (!strcmp(attr->attrName, "saClmNodeEE")) {
			SaNameT *name = (SaNameT *)value;
			size_t nameLen = osaf_extended_name_length(name);

			TRACE("saClmNodeEE attribute name's length %zu",
			      nameLen);

			if (nameLen != 0) {
				if (nameLen >= SA_MAX_NAME_LENGTH) {
					LOG_ER(
					    "saClmNodeEE attribute name length is longer than 255");
					free(node);
					node = NULL;
					goto done;
				}
				if (strncmp((const char *)name->value,
					    "safEE=", 6)) {
					LOG_ER(
					    "Please provide the saf compliant ee name");
					free(node);
					node = NULL;
					goto done;
				}

				if (((ncs_patricia_tree_size(
					 &clms_cb->ee_lookup)) >= 1) &&
				    (clms_cb->reg_with_plm == SA_FALSE))
					LOG_ER(
					    "Incomplete Configuration: EE attribute is not specified for some CLM nodes");
				clms_cb->reg_with_plm = SA_TRUE;
				memcpy(node->ee_name.value, name->value,
				       name->length);
				node->ee_name.length = name->length;
			} else {
				if (clms_cb->reg_with_plm == SA_TRUE) {
					LOG_ER(
					    "Incomplete Configuration: EE attribute is empty for CLM node = %s",
					    node->node_name.value);
				}
			}
		}
	}

	/*Init patricia trackresp for each node */
	clms_trackresp_patricia_init(node);

done:
	TRACE_LEAVE();
	return node;
}

/**
 * This constructs the node db by reading the config attributes from immsv
 */
SaAisErrorT clms_node_create_config(void)
{
	SaAisErrorT rc = SA_AIS_ERR_INVALID_PARAM;
	uint32_t rt, num_nodes = 0;
	SaImmHandleT imm_om_hdl;
	SaImmSearchHandleT search_hdl;
	SaImmSearchParametersT_2 searchParam;
	SaNameT dn;
	SaImmAttrValuesT_2 **attributes;
	/*For time being changed to SaClmNode1 but shud be SaClmNode */
	const char *className = "SaClmNode";

	TRACE_ENTER();

	CLMS_CLUSTER_NODE *node = NULL;

	(void)immutil_saImmOmInitialize(&imm_om_hdl, NULL, &immVersion);

	searchParam.searchOneAttr.attrName = "SaImmAttrClassName";
	searchParam.searchOneAttr.attrValueType = SA_IMM_ATTR_SASTRINGT;
	searchParam.searchOneAttr.attrValue = &className;

	(void)immutil_saImmOmSearchInitialize_2(
	    imm_om_hdl, NULL, SA_IMM_SUBTREE,
	    SA_IMM_SEARCH_ONE_ATTR | SA_IMM_SEARCH_GET_ALL_ATTR, &searchParam,
	    NULL, &search_hdl);
	// TODO: Read saClmClusterNumNodes attribute of SaClmCluster and verify
	// here.  Nodes read below >= saClmClusterNumNodes (dynamic addition may
	// add more nodes).
	for (;;) {
		rc = immutil_saImmOmSearchNext_2(search_hdl, &dn, &attributes);
		TRACE("saImmOmSearchNext_2() returns '%s'", saf_error(rc));
		if (rc == SA_AIS_ERR_NOT_EXIST) {
			// No more nodes to read.
			rc = SA_AIS_OK;
			break;
		} else if (rc == SA_AIS_OK) {
			TRACE("dn:'%s'", dn.value);
			if ((rt = clms_node_dn_chk(&dn)) != NCSCC_RC_SUCCESS) {
				TRACE("Node DN name is incorrect");
				rc = SA_AIS_ERR_BAD_OPERATION;
				goto done2;
			}
			if (clms_node_get_by_name(&dn) != NULL) {
				TRACE("'%s' is already present in db.",
				      dn.value);
				continue;
			}
			if ((node = clms_node_new(
				 &dn,
				 (const SaImmAttrValuesT_2 **)attributes)) ==
			    NULL)
				goto done2;
			num_nodes++;
			clms_node_add_to_model(node);
		} else if ((rc == SA_AIS_ERR_BAD_HANDLE) ||
			   (rc == SA_AIS_ERR_TIMEOUT)) {
			(void)immutil_saImmOmSearchFinalize(search_hdl);
			(void)immutil_saImmOmFinalize(imm_om_hdl);

			// Try 10 times in a gap of 100 millisecs.
			uint16_t count = 0;
			rc = immutil_saImmOmInitialize(&imm_om_hdl, NULL,
						       &immVersion);
			while ((rc == SA_AIS_ERR_TIMEOUT) && (count < 10)) {
				(void)immutil_saImmOmFinalize(imm_om_hdl);
				count++;
				osaf_nanosleep(&kHundredMilliseconds);
				rc = immutil_saImmOmInitialize(
				    &imm_om_hdl, NULL, &immVersion);
			}
			if (rc != SA_AIS_OK) {
				LOG_ER("saImmOmInitialize failed with '%u'",
				       rc);
				goto done2;
			}

			count = 0;
			rc = immutil_saImmOmSearchInitialize_2(
			    imm_om_hdl, NULL, SA_IMM_SUBTREE,
			    SA_IMM_SEARCH_ONE_ATTR | SA_IMM_SEARCH_GET_ALL_ATTR,
			    &searchParam, NULL, &search_hdl);
			while (((rc == SA_AIS_ERR_TIMEOUT) ||
				(rc == SA_AIS_ERR_BAD_HANDLE)) &&
			       (count < 10)) {
				(void)immutil_saImmOmSearchFinalize(search_hdl);
				(void)immutil_saImmOmFinalize(imm_om_hdl);

				count++;
				osaf_nanosleep(&kHundredMilliseconds);

				// Last try to both.
				(void)immutil_saImmOmInitialize(
				    &imm_om_hdl, NULL, &immVersion);
				rc = immutil_saImmOmSearchInitialize_2(
				    imm_om_hdl, NULL, SA_IMM_SUBTREE,
				    SA_IMM_SEARCH_ONE_ATTR |
					SA_IMM_SEARCH_GET_ALL_ATTR,
				    &searchParam, NULL, &search_hdl);
			}
			if (rc != SA_AIS_OK) {
				LOG_ER(
				    "saImmOmSearchInitialize_2 failed with '%u'",
				    rc);
				goto done2;
			}
			// Got new handles, continue.
			continue;
		} else
			// Calling function will report nid failire for any
			// other error.
			break;
	}
	TRACE("nodes count: '%u'", num_nodes);
	TRACE("clms_cb->nodes_db size:'%u",
	      ncs_patricia_tree_size(&clms_cb->nodes_db));
	if ((ncs_patricia_tree_size(&clms_cb->nodes_db) !=
	     ncs_patricia_tree_size(&clms_cb->ee_lookup)) &&
	    (clms_cb->reg_with_plm == true))
		LOG_ER(
		    "Incomplete Configuration: EE attribute is not specified for some CLM nodes");
done2:
	(void)immutil_saImmOmSearchFinalize(search_hdl);
	(void)immutil_saImmOmFinalize(imm_om_hdl);
	TRACE_LEAVE();
	return rc;
}

/**
 * This creates the cluster database by reading the cluster dn from immsv
 */
SaAisErrorT clms_cluster_config_get(void)
{
	SaAisErrorT rc = SA_AIS_ERR_INVALID_PARAM;
	uint32_t rt = NCSCC_RC_SUCCESS;
	SaImmHandleT imm_om_hdl;
	SaImmSearchHandleT search_hdl;
	SaImmSearchParametersT_2 searchParam;
	SaNameT dn;
	SaImmAttrValuesT_2 **attributes;
	const char *className = "SaClmCluster";

	TRACE_ENTER();

	(void)immutil_saImmOmInitialize(&imm_om_hdl, NULL, &immVersion);

	searchParam.searchOneAttr.attrName = "SaImmAttrClassName";
	searchParam.searchOneAttr.attrValueType = SA_IMM_ATTR_SASTRINGT;
	searchParam.searchOneAttr.attrValue = &className;

	rc = immutil_saImmOmSearchInitialize_2(
	    imm_om_hdl, &osaf_cluster->name, SA_IMM_SUBTREE,
	    SA_IMM_SEARCH_ONE_ATTR | SA_IMM_SEARCH_GET_ALL_ATTR, &searchParam,
	    NULL, &search_hdl);

	if (rc != SA_AIS_OK) {
		LOG_ER("No Object of  SaClmCluster Class was found");
		goto done1;
	}
	/*Multiple Cluster is not supported */

	if ((immutil_saImmOmSearchNext_2(search_hdl, &dn, &attributes)) ==
	    SA_AIS_OK) {
		TRACE("clms_cluster_config_get:%s", dn.value);
		if ((rt = clms_cluster_dn_chk(&dn)) != NCSCC_RC_SUCCESS) {
			TRACE("clms_cluster_dn_chk failed");
			goto done2;
		}
		if (osaf_cluster == NULL) {
			osaf_cluster = calloc(1, sizeof(CLMS_CLUSTER_INFO));
			if (osaf_cluster == NULL) {
				LOG_ER("osaf_cluster calloc failed");
				goto done2;
			}
		}
		memcpy(&osaf_cluster->name, &dn, sizeof(SaNameT));

		osaf_cluster->num_nodes = 0; /*Will be updated @runtime */

		if (clms_cb->ha_state == SA_AMF_HA_ACTIVE) {
			osaf_cluster->init_time = clms_get_SaTime();
		}

		osaf_cluster->rtu_pending = false;
		TRACE("RTU pending flag is switched off");
	}
	rc = SA_AIS_OK;
done2:

	(void)immutil_saImmOmSearchFinalize(search_hdl);

done1:
	(void)immutil_saImmOmFinalize(imm_om_hdl);
	TRACE_LEAVE();
	return rc;
}

/**
 * Retrieve the Cluster Node configuration from IMM using the
 * IMM-OM interface and initialize the corresponding information
 * in the CLM control block.
 */
SaAisErrorT clms_imm_activate(CLMS_CB *cb)
{
	SaAisErrorT rc = SA_AIS_ERR_FAILED_OPERATION;
	struct ImmutilWrapperProfile tmp_profile = immutilWrapperProfile;

	TRACE_ENTER();

	immutilWrapperProfile.errorsAreFatal = 0;
	immutilWrapperProfile.retryInterval = 1000;
	immutilWrapperProfile.nTries = 180;

	if ((rc = clms_cluster_config_get()) != SA_AIS_OK) {
		LOG_ER("clms_cluster_config_get failed rc:%u", rc);
		goto done;
	}

	if ((rc = clms_node_create_config()) != SA_AIS_OK) {
		LOG_ER("clms_node_create_config failed rc:%u", rc);
		goto done;
	}

	if (clms_cb->ha_state == SA_AMF_HA_ACTIVE) {
		int msecs_waited = 0;
		for (;;) {
			if (msecs_waited >= max_waiting_time_ms) {
				LOG_ER("Timeout in clms_imm_activate");
				goto done;
			}

			rc = immutil_saImmOiImplementerSet(cb->immOiHandle,
							   IMPLEMENTER_NAME);
			if (rc == SA_AIS_ERR_TIMEOUT ||
			    rc == SA_AIS_ERR_BAD_HANDLE) {
				LOG_WA("saImmOiImplementerSet returned %u",
				       (unsigned)rc);
				usleep(sleep_delay_ms * 1000);
				msecs_waited += sleep_delay_ms;
				saImmOiFinalize(cb->immOiHandle);
				cb->immOiHandle = 0;
				cb->imm_sel_obj = -1;
				cb->is_impl_set = false;
				if ((rc = clms_imm_init(clms_cb)) !=
				    NCSCC_RC_SUCCESS) {
					LOG_ER("clms_imm_init FAILED");
					goto done;
				}
				continue;
			}
			if (rc != SA_AIS_OK) {
				LOG_ER("saImmOiImplementerSet failed rc: %u",
				       (unsigned)rc);
				goto done;
			}

			rc = immutil_saImmOiClassImplementerSet(cb->immOiHandle,
								"SaClmNode");
			if (rc == SA_AIS_ERR_TIMEOUT ||
			    rc == SA_AIS_ERR_BAD_HANDLE) {
				LOG_WA("saImmOiClassImplementerSet returned %u",
				       (unsigned)rc);
				usleep(sleep_delay_ms * 1000);
				msecs_waited += sleep_delay_ms;
				saImmOiFinalize(cb->immOiHandle);
				cb->immOiHandle = 0;
				cb->imm_sel_obj = -1;
				cb->is_impl_set = false;
				if ((rc = clms_imm_init(clms_cb)) !=
				    NCSCC_RC_SUCCESS) {
					LOG_ER("clms_imm_init FAILED");
					goto done;
				}
				continue;
			}
			if (rc != SA_AIS_OK) {
				LOG_ER("saImmOiClassImplementerSet failed for "
				       "class SaClmNode rc: %u",
				       (unsigned)rc);
				goto done;
			}

			rc = immutil_saImmOiClassImplementerSet(cb->immOiHandle,
								"SaClmCluster");
			if (rc == SA_AIS_ERR_TIMEOUT ||
			    rc == SA_AIS_ERR_BAD_HANDLE) {
				LOG_WA("saImmOiClassImplementerSet returned %u",
				       (unsigned)rc);
				usleep(sleep_delay_ms * 1000);
				msecs_waited += sleep_delay_ms;
				saImmOiFinalize(cb->immOiHandle);
				cb->immOiHandle = 0;
				cb->imm_sel_obj = -1;
				cb->is_impl_set = false;
				if ((rc = clms_imm_init(clms_cb)) !=
				    NCSCC_RC_SUCCESS) {
					LOG_ER("clms_imm_init FAILED");
					goto done;
				}
				continue;
			}
			if (rc != SA_AIS_OK) {
				LOG_ER("saImmOiClassImplementerSet failed for "
				       "class SaClmNode rc %u",
				       (unsigned)rc);
				goto done;
			}

			cb->is_impl_set = true;
			clms_all_node_rattr_update();
			break;
		}
	}

	rc = SA_AIS_OK;

done:
	immutilWrapperProfile = tmp_profile;
	TRACE_LEAVE();
	return rc;
}

/**
 * Update IMMSv the admin state(runtime info) of the node
 * @param[in] nd pointer to cluster node
 */
void clms_admin_state_update_rattr(CLMS_CLUSTER_NODE *nd)
{
	SaImmAttrModificationT_2 attr_Mod[1];
	SaAisErrorT rc;

	TRACE_ENTER2("Admin state %d update for node %s", nd->admin_state,
		     nd->node_name.value);

	CLMS_CLUSTER_NODE *node = NULL;
	/* If this update being attempted was for a node down and as a part of
	 * try-again-later, then we need to lookup using name, because the
	 * node_id record would have been deleted as a part of node down
	 * processing
	 */
	osafassert((node = clms_node_get_by_name(&nd->node_name)));

	if (clms_cb->is_impl_set == false) {
		TRACE(
		    "Implementer not yet set: Switching on the tryagain flag");
		node->rtu_pending = true;
		clms_cb->rtu_pending = true;
		TRACE_LEAVE();
		return;
	}

	SaImmAttrValueT attrUpdateValue[] = {&nd->admin_state};
	const SaImmAttrModificationT_2 *attrMods[] = {&attr_Mod[0], NULL};

	attr_Mod[0].modType = SA_IMM_ATTR_VALUES_REPLACE;
	attr_Mod[0].modAttr.attrName = "saClmNodeAdminState";
	attr_Mod[0].modAttr.attrValuesNumber = 1;
	attr_Mod[0].modAttr.attrValueType = SA_IMM_ATTR_SAUINT32T;
	attr_Mod[0].modAttr.attrValues = attrUpdateValue;

	rc = saImmOiRtObjectUpdate_2(clms_cb->immOiHandle, &nd->node_name,
				     attrMods);
	if (rc == SA_AIS_OK) {
		node->admin_rtu_pending = false;
		/* Walk through all nodes to find out if any other rtu is
		 * pending and accordingly turn off the global flag in cb.
		 */
		if (clms_cb->rtu_pending == true) {
			if (clms_is_any_rtu_pending() == false) {
				clms_cb->rtu_pending = false;
				TRACE("RTUpdate success. Turning off flag");
			}
		}
	} else if ((rc == SA_AIS_ERR_TRY_AGAIN) || (rc == SA_AIS_ERR_TIMEOUT)) {
		LOG_IN(
		    "saImmOiRtObjectUpdate for %s failed with rc = %u. Trying again",
		    node->node_name.value, rc);
		node->admin_rtu_pending = true;
		clms_cb->rtu_pending = true;
	} else {
		/* Right now, there is no guarantee on IMM error codes. So
		 * Reinit for everyother error code */
		LOG_IN(
		    "saImmOiRtObjectUpdate for %s failed with rc = %u. Reinit with IMM",
		    node->node_name.value, rc);
		node->admin_rtu_pending = true;
		clms_cb->rtu_pending = true;

		saImmOiFinalize(clms_cb->immOiHandle);
		clms_cb->immOiHandle = 0;
		clms_cb->imm_sel_obj = -1;
		clms_cb->is_impl_set = false;

		/* Initiate IMM reinitializtion in the background */
		clm_imm_reinit_bg(clms_cb);
	}

	TRACE_LEAVE();
}

/**
 * Update IMMSv the runtime info of the node
 * @param[in] nd pointer to cluster node
 */
void clms_node_update_rattr(CLMS_CLUSTER_NODE *nd)
{
	SaImmAttrModificationT_2 attr_Mod[4];
	SaAisErrorT rc;
	SaImmAttrValueT attrUpdateValue[] = {&nd->member};
	SaImmAttrValueT attrUpdateValue1[] = {&nd->node_id};
	SaImmAttrValueT attrUpdateValue2[] = {&nd->boot_time};
	SaImmAttrValueT attrUpdateValue3[] = {&nd->init_view};

	const SaImmAttrModificationT_2 *attrMods[] = {
	    &attr_Mod[0], &attr_Mod[1], &attr_Mod[2], &attr_Mod[3], NULL};

	CLMS_CLUSTER_NODE *node = NULL;

	TRACE_ENTER();
	osafassert((node = clms_node_get_by_name(&nd->node_name)));

	if (clms_cb->is_impl_set == false) {
		TRACE(
		    "Implementer not yet set: Switching on the tryagain flag");
		node->rtu_pending = true;
		clms_cb->rtu_pending = true;
		TRACE_LEAVE();
		return;
	}

	attr_Mod[0].modType = SA_IMM_ATTR_VALUES_REPLACE;
	attr_Mod[0].modAttr.attrName = "saClmNodeIsMember";
	attr_Mod[0].modAttr.attrValuesNumber = 1;
	attr_Mod[0].modAttr.attrValueType = SA_IMM_ATTR_SAUINT32T;
	attr_Mod[0].modAttr.attrValues = attrUpdateValue;

	attr_Mod[1].modType = SA_IMM_ATTR_VALUES_REPLACE;
	attr_Mod[1].modAttr.attrName = "saClmNodeID";
	attr_Mod[1].modAttr.attrValuesNumber = 1;
	attr_Mod[1].modAttr.attrValueType = SA_IMM_ATTR_SAUINT32T;
	attr_Mod[1].modAttr.attrValues = attrUpdateValue1;

	attr_Mod[2].modType = SA_IMM_ATTR_VALUES_REPLACE;
	attr_Mod[2].modAttr.attrName = "saClmNodeBootTimeStamp";
	attr_Mod[2].modAttr.attrValuesNumber = 1;
	attr_Mod[2].modAttr.attrValueType = SA_IMM_ATTR_SATIMET;
	attr_Mod[2].modAttr.attrValues = attrUpdateValue2;

	attr_Mod[3].modType = SA_IMM_ATTR_VALUES_REPLACE;
	attr_Mod[3].modAttr.attrName = "saClmNodeInitialViewNumber";
	attr_Mod[3].modAttr.attrValuesNumber = 1;
	attr_Mod[3].modAttr.attrValueType = SA_IMM_ATTR_SAUINT64T;
	attr_Mod[3].modAttr.attrValues = attrUpdateValue3;

	rc = saImmOiRtObjectUpdate_2(clms_cb->immOiHandle, &nd->node_name,
				     attrMods);

	if (rc == SA_AIS_OK) {
		node->rtu_pending = false;
		/* Walk through all nodes to find out if any other rtu is
		 * pending and accordingly turn off the global flag in cb.
		 */
		if (clms_cb->rtu_pending == true) {
			if (clms_is_any_rtu_pending() == false) {
				clms_cb->rtu_pending = false;
				TRACE("RTUpdate success. Turning off flag");
			}
		}
	} else if ((rc == SA_AIS_ERR_TRY_AGAIN) || (rc == SA_AIS_ERR_TIMEOUT)) {
		LOG_IN(
		    "saImmOiRtObjectUpdate for %s failed with rc = %u. Trying again",
		    node->node_name.value, rc);
		node->rtu_pending = true;
		clms_cb->rtu_pending = true;
	} else {
		LOG_IN(
		    "saImmOiRtObjectUpdate for %s failed with rc = %u. Reinit with IMM",
		    node->node_name.value, rc);
		node->rtu_pending = true;
		clms_cb->rtu_pending = true;

		saImmOiFinalize(clms_cb->immOiHandle);
		clms_cb->immOiHandle = 0;
		clms_cb->imm_sel_obj = -1;
		clms_cb->is_impl_set = false;

		/* Initiate IMM reinitializtion in the background */
		clm_imm_reinit_bg(clms_cb);
	}

	TRACE_LEAVE();
}

/**
 * Update IMMSv the runtime info of all nodes
 */
void clms_all_node_rattr_update(void)
{
	CLMS_CLUSTER_NODE *node = NULL;
	SaUint32T nodeid = 0;

	node = clms_node_getnext_by_id(nodeid);
	while (node != NULL) {
		clms_node_update_rattr(node);
		node = clms_node_getnext_by_id(node->node_id);
	}
}

/**
 *  Process all pending runtime attribute updates toward IMM
 */
void clms_retry_pending_rtupdates(void)
{
	CLMS_CLUSTER_NODE *node = NULL;
	SaNameT nodename = {0};
	TRACE_ENTER();

	if (clms_cb->is_impl_set == false) {
		TRACE_LEAVE2("Implementerset yet to happen, try later");
		return;
	}
	for (node = clms_node_getnext_by_name(&nodename); node != NULL;
	     node = clms_node_getnext_by_name(&nodename)) {
		if (node->rtu_pending == true)
			clms_node_update_rattr(node);
		if (node->admin_rtu_pending == true)
			clms_admin_state_update_rattr(node);
		memcpy(&nodename, &node->node_name, sizeof(SaNameT));
	}

	if (osaf_cluster->rtu_pending == true)
		clms_cluster_update_rattr(osaf_cluster);
	TRACE_LEAVE();
}

/**
 *  As a standby, clear all pending runtime attribute updates toward IMM
 *  The new active will take care of it.
 */
void clms_switchoff_all_pending_rtupdates(void)
{
	CLMS_CLUSTER_NODE *node = NULL;
	SaNameT nodename = {0};
	TRACE_ENTER();

	for (node = clms_node_getnext_by_name(&nodename); node != NULL;
	     node = clms_node_getnext_by_name(&nodename)) {
		TRACE("Switching on the tryagain flag");
		node->rtu_pending = false;
		node->admin_rtu_pending = false;
		memcpy(&nodename, &node->node_name, sizeof(SaNameT));
	}
	osaf_cluster->rtu_pending = false;
	clms_cb->rtu_pending = false;
	TRACE_LEAVE();
}

/**
 *  As a active, mark runtime attribute updates pending for all nodes.
 */
void clms_switchon_all_pending_rtupdates(void)
{
	CLMS_CLUSTER_NODE *node = NULL;
	SaNameT nodename = {0};
	TRACE_ENTER();

	for (node = clms_node_getnext_by_name(&nodename); node != NULL;
	     node = clms_node_getnext_by_name(&nodename)) {
		TRACE("Switching on the pending RTUs");
		node->rtu_pending = true;
		node->admin_rtu_pending = true;
		memcpy(&nodename, &node->node_name, sizeof(SaNameT));
	}
	osaf_cluster->rtu_pending = true;
	clms_cb->rtu_pending = true;
	TRACE_LEAVE();
}

/**
 *  As a standby, clear all pending runtime attribute updates toward IMM
 *  The new active will take care of it.
 */
static SaBoolT clms_is_any_rtu_pending(void)
{
	CLMS_CLUSTER_NODE *node = NULL;
	SaNameT nodename = {0};
	TRACE_ENTER();

	if (osaf_cluster->rtu_pending == true)
		return true;

	for (node = clms_node_getnext_by_name(&nodename); node != NULL;
	     node = clms_node_getnext_by_name(&nodename)) {
		if ((node->rtu_pending == true) ||
		    (node->admin_rtu_pending == true)) {
			TRACE_LEAVE2("There is a pending RTU");
			return true;
		}
		memcpy(&nodename, &node->node_name, sizeof(SaNameT));
	}
	TRACE_LEAVE2("There are no pending RTUs");
	return false;
}

/**
 * Update IMMSv with the runtime info of the osaf cluster
 * @param[in] osaf_cluster pointer to CLM Cluster
 */
void clms_cluster_update_rattr(CLMS_CLUSTER_INFO *osaf_cluster)
{
	SaImmAttrModificationT_2 attr_Mod[2];
	SaAisErrorT rc;
	SaImmAttrValueT attrUpdateValue[] = {&osaf_cluster->num_nodes};
	SaImmAttrValueT attrUpdateValue1[] = {&osaf_cluster->init_time};

	const SaImmAttrModificationT_2 *attrMods[] = {&attr_Mod[0],
						      &attr_Mod[1], NULL};

	TRACE_ENTER();

	if (clms_cb->is_impl_set == false) {
		TRACE("Implementer is not set. Switching on flag in %s",
		      __FUNCTION__);
		osaf_cluster->rtu_pending = true;
		clms_cb->rtu_pending = true;
		TRACE_LEAVE();
		return;
	}

	attr_Mod[0].modType = SA_IMM_ATTR_VALUES_REPLACE;
	attr_Mod[0].modAttr.attrName = "saClmClusterNumNodes";
	attr_Mod[0].modAttr.attrValuesNumber = 1;
	attr_Mod[0].modAttr.attrValueType = SA_IMM_ATTR_SAUINT32T;
	attr_Mod[0].modAttr.attrValues = attrUpdateValue;

	attr_Mod[1].modType = SA_IMM_ATTR_VALUES_REPLACE;
	attr_Mod[1].modAttr.attrName = "saClmClusterInitTimestamp";
	attr_Mod[1].modAttr.attrValuesNumber = 1;
	attr_Mod[1].modAttr.attrValueType = SA_IMM_ATTR_SATIMET;
	attr_Mod[1].modAttr.attrValues = attrUpdateValue1;

	rc = saImmOiRtObjectUpdate_2(clms_cb->immOiHandle, &osaf_cluster->name,
				     attrMods);

	if (rc == SA_AIS_OK) {
		osaf_cluster->rtu_pending = false;
		if (clms_cb->rtu_pending == true) {
			if (clms_is_any_rtu_pending() == false) {
				clms_cb->rtu_pending = false;
				TRACE("RTU success, Switching off");
			}
		}
	} else if ((rc == SA_AIS_ERR_TRY_AGAIN) || (rc == SA_AIS_ERR_TIMEOUT)) {
		LOG_IN(
		    "saImmOiRtObjectUpdate failed for cluster object with rc = %u. Trying again",
		    rc);
		osaf_cluster->rtu_pending = true;
		clms_cb->rtu_pending = true;
	} else {
		LOG_IN(
		    "saImmOiRtObjectUpdate failed for cluster object with rc = %u. Reinit with IMM",
		    rc);
		osaf_cluster->rtu_pending = true;
		clms_cb->rtu_pending = true;

		saImmOiFinalize(clms_cb->immOiHandle);
		clms_cb->immOiHandle = 0;
		clms_cb->imm_sel_obj = -1;
		clms_cb->is_impl_set = false;

		/* Initiate IMM reinitializtion in the background */
		clm_imm_reinit_bg(clms_cb);
	}

	/* TBD: We need to handle a case where there's only one node,
	   this node is not a member yet, because after a cluster reset,
	   when we came up, we could be in a locked state, which means,
	   we are not yet a member, which would also mean as per the definition
	   of this particular attribute, it is the timestamp ofthe first
	   "member" that joined the cluster. Hence, - The above attribute update
	   should happen under a condition that if our adminstate is not
	   unlocked and we are member. - this timestamp or attribute updation
	   should also happen when an IMM unlock for the self-node happens and
	   we become member. */

	TRACE_LEAVE();
}

/**
 * Handles CLM Admin Operation.This is executed as an IMM callback.
 * @param immOiHandle
 * @param invocation
 * @param objectName
 * @param opId
 * @param params
 */
static void
clms_imm_admin_op_callback(SaImmOiHandleT immOiHandle, SaInvocationT invocation,
			   const SaNameT *objectName,
			   SaImmAdminOperationIdT opId,
			   const SaImmAdminOperationParamsT_2 **params)
{
	CLMS_CLUSTER_NODE *nodeop;
	uint32_t rc = NCSCC_RC_SUCCESS;

	TRACE_ENTER2("Admin callback for nodename:%s, opId:%llu",
		     objectName->value, opId);

	// E.g. immadm -o 4 safCluster=myClmCluster
	if (strncmp(osaf_extended_name_borrow(objectName),
		    osaf_extended_name_borrow(&osaf_cluster->name),
		    osaf_extended_name_length(objectName)) == 0) {
		if (opId == SA_CLM_ADMIN_CLUSTER_RESET) {
			LOG_WA(
			    "Cluster reboot requested. Ordering cluster reboot");
			// MDS broadcast/multi cast call is synchronous
			clms_cluster_reboot();
			osaf_nanosleep(&kOneSecond);
			osaf_safe_reboot();
		} else {
			LOG_ER("Admin Operation not supported for %s",
			       osaf_extended_name_borrow(objectName));
			immutil_saImmOiAdminOperationResult(
			    immOiHandle, invocation, SA_AIS_ERR_INVALID_PARAM);
		}
		goto done;
	}

	/*Lookup by the node_name and get the cluster node for CLM Admin oper */
	nodeop = clms_node_get_by_name(objectName);
	if (nodeop == NULL) {
		LOG_ER("Admin Operation on invalid node_name");
		immutil_saImmOiAdminOperationResult(immOiHandle, invocation,
						    SA_AIS_ERR_INVALID_PARAM);
		goto done;
	}

	if (nodeop->admin_state == SA_CLM_ADMIN_SHUTTING_DOWN) {
		switch (opId) {

		case SA_CLM_ADMIN_LOCK:
		case SA_CLM_ADMIN_UNLOCK:
			/* Empty the previous track response list if not null */
			if (ncs_patricia_tree_size(&nodeop->trackresp) != 0)
				clms_node_trackresplist_empty(nodeop);
			immutil_saImmOiAdminOperationResult(
			    immOiHandle, invocation, SA_AIS_ERR_INTERRUPT);
			nodeop->admin_op = 0; /* suspending previous shutdown
						 admin openration */
			break;
		}
	}

	/* Don't proceed: already admin op is in progress on this node */
	if (nodeop->admin_op != 0) {
		TRACE("Another Admin operation already in progress: %d",
		      nodeop->admin_op);
		(void)immutil_saImmOiAdminOperationResult(
		    immOiHandle, invocation, SA_AIS_ERR_BAD_OPERATION);
		goto done;
	}

	nodeop->curr_admin_inv = invocation;

	/*Check for admin oper for opId */
	switch (opId) {

	case SA_CLM_ADMIN_LOCK:
		nodeop->admin_op = IMM_LOCK;
		rc = clms_imm_node_lock(nodeop);
		if (rc != NCSCC_RC_SUCCESS) {
			LOG_NO("clms_imm_node_lock failed");
			goto done;
		}

		break;
	case SA_CLM_ADMIN_UNLOCK:

		nodeop->admin_op = IMM_UNLOCK;
		rc = clms_imm_node_unlock(nodeop);
		if (rc != NCSCC_RC_SUCCESS) {
			LOG_ER("clms_imm_node_unlock failed");
			goto done;
		}
		break;
	case SA_CLM_ADMIN_SHUTDOWN:
		nodeop->admin_op = IMM_SHUTDOWN;
		rc = clms_imm_node_shutdown(nodeop);
		if (rc != NCSCC_RC_SUCCESS) {
			LOG_ER("clms_imm_node_shutdown failed");
			goto done;
		}
		break;
	default:
		TRACE("Admin operation not supported");
		(void)immutil_saImmOiAdminOperationResult(
		    immOiHandle, invocation, SA_AIS_ERR_NOT_SUPPORTED);
		goto done;
	}

	/*CheckPoint the node data */
	ckpt_node_rec(nodeop);

	/*Checkpoint Cluster data */
	ckpt_cluster_rec();

done:
	TRACE_LEAVE();
}

/**
 * This forms the track response list for the start/validate step during admin
 * operation
 * @param[in] node          pointer to cluster node on which this list is to be
 * created
 * @param[client] client    client info to be added to list
 */
static void clms_create_track_resp_list(CLMS_CLUSTER_NODE *node,
					CLMS_CLIENT_INFO *client)
{
	CLMS_TRACK_INFO *trk = NULL;
	TRACE_ENTER();

	trk = (CLMS_TRACK_INFO *)calloc(1, sizeof(CLMS_TRACK_INFO));
	if (trk == NULL) {
		LOG_ER("trk calloc failed");
		osafassert(0);
	}
	/*add to the tracklist of clmresp tracking */
	trk->client_id = client->client_id;
	trk->client_dest = client->mds_dest;
	trk->client_id_net = client->client_id_net;
	trk->inv_id = client->inv_id;
	trk->pat_node.key_info =
	    (uint8_t *)&trk->inv_id; /*Key Info as inv id */

	if (ncs_patricia_tree_add(&node->trackresp, &trk->pat_node) !=
	    NCSCC_RC_SUCCESS) {
		LOG_ER(
		    "patricia tree add failed for CLMS_TRACK_INFO inv_id %llu",
		    client->inv_id);
		osafassert(0);
	}

	TRACE_LEAVE();
}

/**
 * This sends the track callback to its clients on the basis of the subscribed
 * trackflags
 * @param[in] cb      CLM CB
 * @param[in] node    pointer to cluster node
 * @param[in] step    CLM step for which to send the callback
 */
void clms_send_track(CLMS_CB *cb, CLMS_CLUSTER_NODE *node,
		     SaClmChangeStepT step, bool node_reboot)
{
	CLMS_CLIENT_INFO *rec;
	uint32_t client_id = 0;
	SaClmClusterNotificationT_4 *notify_changes = NULL;
	SaClmClusterNotificationT_4 *notify_changes_only = NULL;
	uint32_t rc = NCSCC_RC_SUCCESS;
	SaUint32T node_id;

	TRACE_ENTER2("step: %d", step);

	/* Check for previous stale tracklist on this node and delete it.
	   PLM doesnt allows multiple admin operations, hence this means
	   PLM is enforcing its clients to forcibly-move to the next step
	   If it was validate, then we get abort. If we were in start,
	   we get completed.
	   for the same admin operation during which a fail/switchover
	   happened
	 */
	if (ncs_patricia_tree_size(&node->trackresp) != 0)
		clms_node_trackresplist_empty(node);

	notify_changes_only = clms_notbuffer_changes_only(step);
	notify_changes = clms_notbuffer_changes(step);

	while ((rec = clms_client_getnext_by_id(client_id)) != NULL) {
		client_id = rec->client_id;
		node_id = m_NCS_NODE_ID_FROM_MDS_DEST(rec->mds_dest);
		if (step == SA_CLM_CHANGE_START) {

			if (rec->track_flags & (SA_TRACK_START_STEP)) {
				/*Create the inv-id */
				rec->inv_id = m_CLMSV_PACK_INV(
				    (cb->curr_invid)++, node->node_id);
				TRACE("rec->inv_id %llu", rec->inv_id);

				if (rec->track_flags & SA_TRACK_CHANGES_ONLY) {
					if (rec->track_flags & SA_TRACK_LOCAL) {
						if (node_id == node->node_id) {
							/*Implies the change is
							 * on this local node */
							rc = clms_send_track_local(
							    node, rec,
							    SA_CLM_CHANGE_START);
						}

					} else
						rc = clms_prep_and_send_track(
						    cb, node, rec,
						    SA_CLM_CHANGE_START,
						    notify_changes_only);

				} else if (rec->track_flags &
					   SA_TRACK_CHANGES) {
					if (rec->track_flags & SA_TRACK_LOCAL) {
						if (node_id == node->node_id) {
							/*Implies the change is
							 * on this local node */
							rc = clms_send_track_local(
							    node, rec,
							    SA_CLM_CHANGE_START);
						}

					} else
						rc = clms_prep_and_send_track(
						    cb, node, rec,
						    SA_CLM_CHANGE_START,
						    notify_changes);
				}

				if (rc != NCSCC_RC_SUCCESS) {
					TRACE(
					    "Sending track callback failed for SA_CLM_CHANGE_START");
				}

				/* rc == NCSCC_RC_SUCCESS is true then add rec
				 * into track response list*/
				if (rc == NCSCC_RC_SUCCESS) {
					clms_create_track_resp_list(node, rec);
					TRACE(
					    "patricia tree add succeeded for start step");
				}
			}
		} else if (step == SA_CLM_CHANGE_VALIDATE) {
			if (rec->track_flags & (SA_TRACK_VALIDATE_STEP)) {
				/*Create the inv-id */
				rec->inv_id = m_CLMSV_PACK_INV(
				    (cb->curr_invid)++, node->node_id);

				if (rec->track_flags & SA_TRACK_CHANGES_ONLY) {
					if (rec->track_flags & SA_TRACK_LOCAL) {
						if (node_id == node->node_id) {
							/*Implies the change is
							 * on this local node */
							rc = clms_send_track_local(
							    node, rec,
							    SA_CLM_CHANGE_VALIDATE);
						}

					} else
						rc = clms_prep_and_send_track(
						    cb, node, rec,
						    SA_CLM_CHANGE_VALIDATE,
						    notify_changes_only);

				} else if (rec->track_flags &
					   SA_TRACK_CHANGES) {
					if (rec->track_flags & SA_TRACK_LOCAL) {
						if (node_id == node->node_id) {
							/*Implies the change is
							 * on this local node */
							rc = clms_send_track_local(
							    node, rec,
							    SA_CLM_CHANGE_VALIDATE);
						}

					} else
						rc = clms_prep_and_send_track(
						    cb, node, rec,
						    SA_CLM_CHANGE_VALIDATE,
						    notify_changes);
				}

				if (rc != NCSCC_RC_SUCCESS) {
					TRACE(
					    "Sending track callback failed for SA_CLM_CHANGE_VALIDATE");
				}

				/*rc == NCSCC_RC_SUCCESS is true then add rec
				 * into track response list*/
				if (rc == NCSCC_RC_SUCCESS) {
					clms_create_track_resp_list(node, rec);
					TRACE(
					    "patricia tree add succeeded for validate step");
				}
			}
		} else if (step == SA_CLM_CHANGE_COMPLETED) {

			rec->inv_id = 0; /*No resp for Completed Step */

			if (rec->track_flags & SA_TRACK_CHANGES_ONLY) {
				if (rec->track_flags & SA_TRACK_LOCAL) {
					if (node_id == node->node_id) {
						/*Implies the change is on this
						 * local node */
						rc = clms_send_track_local(
						    node, rec,
						    SA_CLM_CHANGE_COMPLETED);
					}
				} else {
					/* In the COMPLETED step and when the
					 * trigger is node reboot, we shouldn't
					 * send track to agents on the same node
					 * that left the cluster.
					 */
					if ((node_id == node->node_id) &&
					    (node_reboot)) {
						LOG_NO(
						    "Node %u went down. Not sending track callback for agents on that node",
						    node_id);
					} else {
						rc = clms_prep_and_send_track(
						    cb, node, rec,
						    SA_CLM_CHANGE_COMPLETED,
						    notify_changes_only);
					}
				}
			} else if (rec->track_flags & SA_TRACK_CHANGES) {
				if (rec->track_flags & SA_TRACK_LOCAL) {
					if (node_id == node->node_id) {
						/*Implies the change is on this
						 * local node */
						rc = clms_send_track_local(
						    node, rec,
						    SA_CLM_CHANGE_COMPLETED);
					}
				} else {
					/* In the COMPLETED step and when there
					 * is no admin op in progress, we
					 * shouldn't send track to agents on the
					 * same node that left the cluster.
					 */
					if ((node_id == node->node_id) &&
					    (node_reboot)) {
						LOG_NO(
						    "Node %u went down. Not sending track callback for agents on that node",
						    node_id);
					} else {
						rc = clms_prep_and_send_track(
						    cb, node, rec,
						    SA_CLM_CHANGE_COMPLETED,
						    notify_changes);
					}
				}
			}

			if (rc != NCSCC_RC_SUCCESS) {
				TRACE(
				    "Sending track callback failed for SA_CLM_CHANGE_COMPLETED");
			}

		} else if (
		    step ==
		    SA_CLM_CHANGE_ABORTED) { /*no track response list is
						maintained */

			if ((rec->track_flags & SA_TRACK_VALIDATE_STEP) &&
			    (node->admin_op == PLM)) {
				rec->inv_id = 0; /*No resp for Aborted Step */

				if (rec->track_flags & SA_TRACK_CHANGES_ONLY)
					rc = clms_prep_and_send_track(
					    cb, node, rec,
					    SA_CLM_CHANGE_ABORTED,
					    notify_changes_only);
				else if (rec->track_flags & SA_TRACK_CHANGES)
					rc = clms_prep_and_send_track(
					    cb, node, rec,
					    SA_CLM_CHANGE_ABORTED,
					    notify_changes);

				if (rc != NCSCC_RC_SUCCESS) {
					TRACE(
					    "Sending track callback failed for SA_CLM_CHANGE_ABORTED");
				}

			} else if ((rec->track_flags & SA_TRACK_START_STEP) &&
				   ((node->admin_op != PLM) &&
				    (node->admin_op != 0))) {
				rec->inv_id = 0; /*No resp for Aborted Step */

				if (rec->track_flags & SA_TRACK_CHANGES_ONLY)
					rc = clms_prep_and_send_track(
					    cb, node, rec,
					    SA_CLM_CHANGE_ABORTED,
					    notify_changes_only);
				else if (rec->track_flags & SA_TRACK_CHANGES)
					rc = clms_prep_and_send_track(
					    cb, node, rec,
					    SA_CLM_CHANGE_ABORTED,
					    notify_changes);

				if (rc != NCSCC_RC_SUCCESS) {
					TRACE(
					    "Sending track callback failed for SA_CLM_CHANGE_ABORTED");
				}
			}
		}
	}
	free(notify_changes_only);
	free(notify_changes);
	TRACE_LEAVE();
}

/**
 * Prepares the CLMSV msg to send to clma
 * @param[in] node
 * @param[in] client
 * @param[in] step
 */
uint32_t clms_send_track_local(CLMS_CLUSTER_NODE *node,
			       CLMS_CLIENT_INFO *client, SaClmChangeStepT step)
{
	CLMSV_MSG msg;
	uint32_t rc = NCSCC_RC_SUCCESS;

	TRACE_ENTER();

	/* stick the notification buffer into the message */
	memset(&msg, 0, sizeof(CLMSV_MSG));

	/* Fill the msg */
	msg.evt_type = CLMSV_CLMS_TO_CLMA_CBK_MSG;
	msg.info.cbk_info.client_id = client->client_id;
	msg.info.cbk_info.type = CLMSV_TRACK_CBK;
	msg.info.cbk_info.param.track.mem_num = osaf_cluster->num_nodes;
	msg.info.cbk_info.param.track.err = SA_AIS_OK;
	msg.info.cbk_info.param.track.inv = client->inv_id;

	msg.info.cbk_info.param.track.root_cause_ent =
	    (SaNameT *)malloc(sizeof(SaNameT));

	if (node->admin_op != PLM) {
		msg.info.cbk_info.param.track.root_cause_ent->length =
		    node->node_name.length;
		memcpy(msg.info.cbk_info.param.track.root_cause_ent->value,
		       node->node_name.value, node->node_name.length);
	} else {
		msg.info.cbk_info.param.track.root_cause_ent->length =
		    node->ee_name.length;
		memcpy(msg.info.cbk_info.param.track.root_cause_ent->value,
		       node->ee_name.value, node->ee_name.length);
	}

	msg.info.cbk_info.param.track.cor_ids = (SaNtfCorrelationIdsT *)malloc(
	    sizeof(SaNtfCorrelationIdsT)); /*Not Supported as of now */
	msg.info.cbk_info.param.track.step = step;

	if (step == SA_CLM_CHANGE_START)
		msg.info.cbk_info.param.track.time_super =
		    node->lck_cbk_timeout;
	else
		msg.info.cbk_info.param.track.time_super =
		    (SaTimeT)SA_TIME_UNKNOWN;

	msg.info.cbk_info.param.track.buf_info.viewNumber =
	    clms_cb->cluster_view_num;

	if (client->track_flags & SA_TRACK_CHANGES_ONLY) {
		msg.info.cbk_info.param.track.buf_info.numberOfItems = 1;
		msg.info.cbk_info.param.track.buf_info.notification =
		    (SaClmClusterNotificationT_4 *)malloc(
			sizeof(SaClmClusterNotificationT_4));

		if (!msg.info.cbk_info.param.track.buf_info.notification) {
			LOG_ER("Malloc failed for notification");
			osafassert(0);
		}

		memset(msg.info.cbk_info.param.track.buf_info.notification, 0,
		       sizeof(SaClmClusterNotificationT_4));

		msg.info.cbk_info.param.track.buf_info.notification->clusterNode
		    .nodeId = node->node_id;

		msg.info.cbk_info.param.track.buf_info.notification->clusterNode
		    .nodeAddress.family = node->node_addr.family;
		msg.info.cbk_info.param.track.buf_info.notification->clusterNode
		    .nodeAddress.length = node->node_addr.length;
		memcpy(msg.info.cbk_info.param.track.buf_info.notification
			   ->clusterNode.nodeAddress.value,
		       node->node_addr.value,
		       msg.info.cbk_info.param.track.buf_info.notification
			   ->clusterNode.nodeAddress.length);

		msg.info.cbk_info.param.track.buf_info.notification->clusterNode
		    .nodeName.length = node->node_name.length;
		memcpy(msg.info.cbk_info.param.track.buf_info.notification
			   ->clusterNode.nodeName.value,
		       node->node_name.value,
		       msg.info.cbk_info.param.track.buf_info.notification
			   ->clusterNode.nodeName.length);

		msg.info.cbk_info.param.track.buf_info.notification->clusterNode
		    .executionEnvironment.length = node->ee_name.length;
		memcpy(msg.info.cbk_info.param.track.buf_info.notification
			   ->clusterNode.executionEnvironment.value,
		       node->ee_name.value,
		       msg.info.cbk_info.param.track.buf_info.notification
			   ->clusterNode.executionEnvironment.length);

		msg.info.cbk_info.param.track.buf_info.notification->clusterNode
		    .member = node->member;
		msg.info.cbk_info.param.track.buf_info.notification->clusterNode
		    .bootTimestamp = node->boot_time;
		msg.info.cbk_info.param.track.buf_info.notification->clusterNode
		    .initialViewNumber = node->init_view;

		if (node->member == SA_FALSE)
			msg.info.cbk_info.param.track.buf_info.notification
			    ->clusterChange = SA_CLM_NODE_LEFT;
		else if (step == SA_CLM_CHANGE_COMPLETED)
			msg.info.cbk_info.param.track.buf_info.notification
			    ->clusterChange = node->change;
		else
			msg.info.cbk_info.param.track.buf_info.notification
			    ->clusterChange = SA_CLM_NODE_NO_CHANGE;

	} else if (client->track_flags & SA_TRACK_CHANGES) {
		msg.info.cbk_info.param.track.buf_info.numberOfItems =
		    clms_nodedb_lookup(1);
		msg.info.cbk_info.param.track.buf_info.notification =
		    clms_notbuffer_changes(step);
	}

	rc = clms_mds_msg_send(clms_cb, &msg, &client->mds_dest, NULL,
			       MDS_SEND_PRIORITY_MEDIUM, NCSMDS_SVC_ID_CLMA);

	if (rc != NCSCC_RC_SUCCESS) {
		TRACE("callback msg send to clma  failed");
	}

	if (msg.info.cbk_info.param.track.buf_info.notification)
		free(msg.info.cbk_info.param.track.buf_info.notification);
	free(msg.info.cbk_info.param.track.root_cause_ent);
	free(msg.info.cbk_info.param.track.cor_ids);

	TRACE_LEAVE();
	return rc;
}

/**
 * Prepares the CLMSV msg to send to clma
 * @param[in] node
 * @param[in] client
 * @param[in] step
 * @param[in] notification buffer
 */
uint32_t clms_prep_and_send_track(CLMS_CB *cb, CLMS_CLUSTER_NODE *node,
				  CLMS_CLIENT_INFO *client,
				  SaClmChangeStepT step,
				  SaClmClusterNotificationT_4 *notify)
{
	CLMSV_MSG msg;
	uint32_t rc = NCSCC_RC_SUCCESS;

	TRACE_ENTER();

	/* stick the notification buffer into the message */
	memset(&msg, 0, sizeof(CLMSV_MSG));

	/* Fill the msg */
	msg.evt_type = CLMSV_CLMS_TO_CLMA_CBK_MSG;
	msg.info.cbk_info.client_id = client->client_id;
	msg.info.cbk_info.type = CLMSV_TRACK_CBK;
	msg.info.cbk_info.param.track.mem_num = osaf_cluster->num_nodes;
	msg.info.cbk_info.param.track.err = SA_AIS_OK;
	msg.info.cbk_info.param.track.inv = client->inv_id;

	msg.info.cbk_info.param.track.root_cause_ent =
	    (SaNameT *)malloc(sizeof(SaNameT));

	if (node->admin_op != PLM) {
		msg.info.cbk_info.param.track.root_cause_ent->length =
		    node->node_name.length;
		memcpy(msg.info.cbk_info.param.track.root_cause_ent->value,
		       node->node_name.value, node->node_name.length);
	} else {
		msg.info.cbk_info.param.track.root_cause_ent->length =
		    node->ee_name.length;
		memcpy(msg.info.cbk_info.param.track.root_cause_ent->value,
		       node->ee_name.value, node->ee_name.length);
	}

	msg.info.cbk_info.param.track.cor_ids = (SaNtfCorrelationIdsT *)malloc(
	    sizeof(SaNtfCorrelationIdsT)); /*Not Supported as of now */
	msg.info.cbk_info.param.track.step = step;

	if (step == SA_CLM_CHANGE_START)
		msg.info.cbk_info.param.track.time_super =
		    node->lck_cbk_timeout;
	else
		msg.info.cbk_info.param.track.time_super =
		    (SaTimeT)SA_TIME_UNKNOWN;

	msg.info.cbk_info.param.track.buf_info.viewNumber =
	    clms_cb->cluster_view_num;

	if (client->track_flags & SA_TRACK_CHANGES_ONLY)
		msg.info.cbk_info.param.track.buf_info.numberOfItems =
		    clms_nodedb_lookup(0);
	else if (client->track_flags & SA_TRACK_CHANGES)
		msg.info.cbk_info.param.track.buf_info.numberOfItems =
		    clms_nodedb_lookup(1);

	msg.info.cbk_info.param.track.buf_info.notification =
	    (SaClmClusterNotificationT_4 *)malloc(
		msg.info.cbk_info.param.track.buf_info.numberOfItems *
		sizeof(SaClmClusterNotificationT_4));

	if (!msg.info.cbk_info.param.track.buf_info.notification) {
		LOG_ER("Malloc failed for notification");
		osafassert(0);
	}

	memset(msg.info.cbk_info.param.track.buf_info.notification, 0,
	       (msg.info.cbk_info.param.track.buf_info.numberOfItems *
		sizeof(SaClmClusterNotificationT_4)));

	if (notify != NULL) {
		memcpy(msg.info.cbk_info.param.track.buf_info.notification,
		       notify,
		       (msg.info.cbk_info.param.track.buf_info.numberOfItems *
			sizeof(SaClmClusterNotificationT_4)));
	}

	rc = clms_mds_msg_send(cb, &msg, &client->mds_dest, NULL,
			       MDS_SEND_PRIORITY_MEDIUM, NCSMDS_SVC_ID_CLMA);

	if (rc != NCSCC_RC_SUCCESS) {
		TRACE("callback msg send to clma  failed");
	}

	if (msg.info.cbk_info.param.track.buf_info.notification)
		free(msg.info.cbk_info.param.track.buf_info.notification);
	free(msg.info.cbk_info.param.track.root_cause_ent);
	free(msg.info.cbk_info.param.track.cor_ids);

	TRACE_LEAVE();
	return rc;
}

/**
 * Handles the creation of the new Clm node.This is executed as an IMM callback
 */
static SaAisErrorT clms_imm_ccb_obj_create_callback(
    SaImmOiHandleT immOiHandle, SaImmOiCcbIdT ccbId,
    const SaImmClassNameT className, const SaNameT *parentName,
    const SaImmAttrValuesT_2 **attr)
{

	SaAisErrorT rc = SA_AIS_OK;
	uint32_t rt;
	CcbUtilCcbData_t *ccb_util_ccb_data;
	CcbUtilOperationData_t *operation = NULL;
	size_t parentNameLen = 0;

	TRACE_ENTER2("CCB ID %llu, class %s, parent %s", ccbId, className,
		     parentName ? osaf_extended_name_borrow(parentName)
				: "(null)");

	if (parentName) {
		parentNameLen = osaf_extended_name_length(parentName);
		if (parentNameLen >= SA_MAX_NAME_LENGTH) {
			LOG_ER("Parent name is longer than 255");
			rc = SA_AIS_ERR_BAD_OPERATION;
			goto done;
		}
	}

	if ((ccb_util_ccb_data = ccbutil_getCcbData(ccbId)) != NULL) {
		int i = 0;
		const SaImmAttrValuesT_2 *attrValue;

		operation = ccbutil_ccbAddCreateOperation(
		    ccb_util_ccb_data, className, parentName, attr);

		if (operation == NULL) {
			LOG_ER("Failed to get CCB operation object for %llu",
			       ccbId);
			rc = SA_AIS_ERR_NO_MEMORY;
			goto done;
		}

		/* Find the RDN attribute and store the object DN */
		while ((attrValue = attr[i++]) != NULL) {
			if (!strncmp(attrValue->attrName, "safCluster", 10)) {
				rc = SA_AIS_ERR_BAD_OPERATION;
				goto done;
			} else if (!strncmp(attrValue->attrName, "safNode",
					    7)) {
				if (attrValue->attrValueType ==
				    SA_IMM_ATTR_SASTRINGT) {
					SaStringT rdnVal =
					    *((SaStringT *)
						  attrValue->attrValues[0]);
					if ((parentName != NULL) &&
					    (parentNameLen > 0) &&
					    (strlen(rdnVal) + parentNameLen +
					     1) < SA_MAX_NAME_LENGTH) {
						operation->objectName.length =
						    (SaUint16T)sprintf(
							(char *)operation
							    ->objectName.value,
							"%s,%s", rdnVal,
							parentName->value);
					} else {
						if (!parentName ||
						    parentNameLen == 0) {
							LOG_ER(
							    "Node DN name is incorrect. Parent is NULL or empty");
						} else {
							LOG_ER(
							    "Node DN name is incorrect. DN is longer than 255");
						}
						rc = SA_AIS_ERR_BAD_OPERATION;
						goto done;
					}
				}

				TRACE("Operation's object Name %s",
				      operation->objectName.value);
			} else if (!strncmp(attrValue->attrName, "saClmNodeEE",
					    11) &&
				   attrValue->attrValuesNumber == 1) {
				SaNameT *name =
				    (SaNameT *)attrValue->attrValues[0];
				if (osaf_extended_name_length(name) >=
				    SA_MAX_NAME_LENGTH) {
					LOG_ER(
					    "saClmNodeEE is longer than 255");
					rc = SA_AIS_ERR_BAD_OPERATION;
					goto done;
				}
			}
		}
	}

	if ((rt = clms_node_dn_chk(&operation->objectName)) !=
	    NCSCC_RC_SUCCESS) {
		TRACE("Node DN name is incorrect");
		rc = SA_AIS_ERR_BAD_OPERATION;
	}

done:
	TRACE_LEAVE();
	return rc;
}

/**
 * Handles the deletion of the Clm node.This is executed as an IMM callback
 */
static SaAisErrorT clms_imm_ccb_obj_delete_callback(SaImmOiHandleT immOiHandle,
						    SaImmOiCcbIdT ccbId,
						    const SaNameT *objectName)
{
	SaAisErrorT rc = SA_AIS_OK;
	struct CcbUtilCcbData *ccb_util_ccb_data;

	TRACE_ENTER2("CCB ID %llu, %s", ccbId, objectName->value);

	if ((ccb_util_ccb_data = ccbutil_getCcbData(ccbId)) != NULL) {
		/* "memorize the request" */
		ccbutil_ccbAddDeleteOperation(ccb_util_ccb_data, objectName);
	} else {
		LOG_ER("Failed to get CCB object for %llu", ccbId);
		rc = SA_AIS_ERR_NO_MEMORY;
	}

	if (!strncmp((const char *)objectName->value, "safCluster=", 11)) {
		LOG_ER(
		    "Deletion of the present single cluster Object not supported");
		rc = SA_AIS_ERR_BAD_OPERATION;
	}

	TRACE_LEAVE();

	return rc;
}

/**
 * Handles the node's attribute value modification.This is executed as an IMM
 * callback
 */
static SaAisErrorT
clms_imm_ccb_obj_modify_callback(SaImmOiHandleT immOiHandle,
				 SaImmOiCcbIdT ccbId, const SaNameT *objectName,
				 const SaImmAttrModificationT_2 **attrMods)
{
	SaAisErrorT rc = SA_AIS_OK;
	struct CcbUtilCcbData *ccbUtilCcbData;
	int i = 0;
	SaImmAttrModificationT_2 *attrMod;
	SaNameT *name;

	TRACE_ENTER2("CCB ID %llu for object-name:%s ", ccbId,
		     objectName->value);

	if ((ccbUtilCcbData = ccbutil_getCcbData(ccbId)) != NULL) {
		/* saClmNodeEE cannot be longer than 255 */
		while ((attrMod = (SaImmAttrModificationT_2 *)attrMods[i++]) !=
		       NULL) {
			if (strncmp(attrMod->modAttr.attrName, "saClmNodeEE",
				    11) != 0)
				continue;

			if (attrMod->modAttr.attrValuesNumber == 0) {
				break;
			}

			name = (SaNameT *)attrMod->modAttr.attrValues[0];
			if (osaf_extended_name_length(name) >=
			    SA_MAX_NAME_LENGTH) {
				LOG_ER("saClmNodeEE is longer than 255");
				rc = SA_AIS_ERR_BAD_OPERATION;
				break;
			}
		}

		/*memorize the modification request */
		if (ccbutil_ccbAddModifyOperation(ccbUtilCcbData, objectName,
						  attrMods) != 0) {
			LOG_ER("Failed ccb object modify %s",
			       objectName->value);
			rc = SA_AIS_ERR_BAD_OPERATION;
		}
	} else {
		LOG_ER("Failed to get CCB object for %llu", ccbId);
		rc = SA_AIS_ERR_NO_MEMORY;
	}

	TRACE_LEAVE();
	return rc;
}

static SaAisErrorT clms_imm_ccb_completed_callback(SaImmOiHandleT immOiHandle,
						   SaImmOiCcbIdT ccbId)
{
	SaAisErrorT rc = SA_AIS_OK;
	CcbUtilOperationData_t *OperData = NULL;

	TRACE_ENTER2("CCB ID %llu", ccbId);

	/* "check that the sequence of change requests contained in the CCB is
	   valid and that no errors will be generated when these changes
	   are applied." */

	while ((OperData = ccbutil_getNextCcbOp(ccbId, OperData)) != NULL) {
		rc = clms_node_ccb_comp_cb(OperData);

		/* Get out at first error */
		if (rc != SA_AIS_OK)
			break;
	}
	TRACE_LEAVE();

	return rc;
}

static void clms_imm_ccb_abort_callback(SaImmOiHandleT immOiHandle,
					SaImmOiCcbIdT ccbId)
{
	struct CcbUtilCcbData *ccbUtilCcbData;

	TRACE_ENTER2("CCB Id %llu", ccbId);

	if ((ccbUtilCcbData = ccbutil_findCcbData(ccbId)) != NULL)
		ccbutil_deleteCcbData(ccbUtilCcbData);
	else
		LOG_ER("Failed to find CCB object for %llu", ccbId);

	TRACE_LEAVE();
}

SaAisErrorT clms_node_ccb_comp_modify(CcbUtilOperationData_t *opdata)
{
	SaAisErrorT rc = SA_AIS_OK;
	const SaImmAttrModificationT_2 *attr_mod;
	int i = 0;

	TRACE_ENTER2("%s", opdata->objectName.value);

	while ((attr_mod = opdata->param.modify.attrMods[i++]) != NULL) {
		const SaImmAttrValuesT_2 *attribute = &attr_mod->modAttr;
		void *value = attribute->attrValues[0];

		if (!strcmp(attribute->attrName, "saClmNodeDisableReboot")) {
			SaBoolT val = *((SaBoolT *)value);
			if (val > SA_TRUE) {
				rc = SA_AIS_ERR_BAD_OPERATION;
				goto done;
			}
		} else if (!strcmp(attribute->attrName,
				   "saClmNodeAddressFamily")) {
			LOG_NO(
			    "Modification of saClmNodeAddressFamily of object %s not allowed",
			    opdata->objectName.value);
			rc = SA_AIS_ERR_BAD_OPERATION;
			goto done;
		} else if (!strcmp(attribute->attrName, "saClmNodeAddress")) {
			LOG_NO(
			    "Modification of saClmNodeAddress of object %s not allowed",
			    opdata->objectName.value);
			rc = SA_AIS_ERR_BAD_OPERATION;
			goto done;
		} else if (!strcmp(attribute->attrName,
				   "saClmNodeLockCallbackTimeout")) {
			SaTimeT Timeout = *((SaTimeT *)value);
			if (Timeout == 0) {
				rc = SA_AIS_ERR_BAD_OPERATION;
				goto done;
			}
		} else if (!strcmp(attribute->attrName, "saClmNodeEE")) {

			if (clms_cb->reg_with_plm == SA_FALSE) {
				LOG_NO(
				    "saClmNodeEE attribute change doesn't apply when plm in not in model");
				rc = SA_AIS_ERR_BAD_OPERATION;
				goto done;
			} else {
				LOG_NO(
				    "saClmNodeEE attribute change of object %s is not supported",
				    opdata->objectName.value);
				rc = SA_AIS_ERR_BAD_OPERATION;
				goto done;
			}
		} else {
			TRACE("Unknown attribute %s", attribute->attrName);
			rc = SA_AIS_ERR_BAD_OPERATION;
			goto done;
		}
	}

done:

	TRACE_LEAVE();
	return rc;
}

/**
 * This routine handles all CCB operations on SaClmNode objects.
 * @param[in] opdata  : Ccb Util Oper Data
 * @return SA_AIS_OK/SA_AIS_ERR_BAD_OPERATION
 */
SaAisErrorT clms_node_ccb_comp_cb(CcbUtilOperationData_t *opdata)
{
	SaAisErrorT rc = SA_AIS_ERR_BAD_OPERATION;
	uint32_t rt = NCSCC_RC_SUCCESS;
	CLMS_CLUSTER_NODE *node;

	TRACE_ENTER2("'%s', %u", opdata->objectName.value,
		     (unsigned int)opdata->ccbId);

	switch (opdata->operationType) {
	case CCBUTIL_CREATE:
		if ((rt = clms_node_dn_chk(&opdata->objectName)) !=
		    NCSCC_RC_SUCCESS) {
			TRACE("Node DN name is incorrect");
			goto done;
		}

		if ((node = clms_node_new(&opdata->objectName,
					  opdata->param.create.attrValues)) ==
		    NULL) {
			goto done;
		}
		opdata->userData = node; /* Save for later use in apply */

		break;
	case CCBUTIL_MODIFY:
		if ((rc = clms_node_ccb_comp_modify(opdata)) != SA_AIS_OK)
			goto done;
		break;
	case CCBUTIL_DELETE:
		/*lookup by objectName in the database */
		if ((node = clms_node_get_by_name(&opdata->objectName)) == NULL)
			goto done;
		if (node->member == SA_TRUE) {
			LOG_ER("Node %s is still a cluster member",
			       opdata->objectName.value);
			goto done;
		}

		if (node->admin_state != SA_CLM_ADMIN_LOCKED) {
			LOG_ER("Node '%s' not locked",
			       opdata->objectName.value);
			goto done;
		}
		break;
	default:
		osafassert(0);
		break;
	}

	TRACE_LEAVE();
	return SA_AIS_OK;
done:

	TRACE_LEAVE();
	return rc;
}

SaAisErrorT clms_node_ccb_apply_modify(CcbUtilOperationData_t *opdata)
{
	SaAisErrorT rc = SA_AIS_OK;
	const SaImmAttrModificationT_2 *attr_mod;
	CLMS_CLUSTER_NODE *node = NULL;
	int i = 0;

	TRACE_ENTER2("%s", opdata->objectName.value);

	node = clms_node_get_by_name(&opdata->objectName);
	osafassert(node != NULL);

	while ((attr_mod = opdata->param.modify.attrMods[i++]) != NULL) {
		const SaImmAttrValuesT_2 *attribute = &attr_mod->modAttr;
		void *value = attribute->attrValues[0];

		if (!strcmp(attribute->attrName, "saClmNodeDisableReboot")) {
			node->disable_reboot = *((SaBoolT *)value);

		} else if (!strcmp(attribute->attrName,
				   "saClmNodeLockCallbackTimeout")) {
			node->lck_cbk_timeout = *((SaTimeT *)value);
		}
	}
	node->stat_change = SA_TRUE;
	node->change = SA_CLM_NODE_RECONFIGURED;
	node->admin_op = IMM_RECONFIGURED;
	++(clms_cb->cluster_view_num);
	/*Send track callback with saClmClusterChangesT=
	 * SA_CLM_NODE_RECONFIGURED */
	clms_send_track(clms_cb, node, SA_CLM_CHANGE_COMPLETED, false);
	/*Clear admin_op and stat_change */
	node->admin_op = 0;
	node->stat_change = SA_FALSE;

	/*Send Notification with ntfid as SA_CLM_NTFID_NODE_RECONFIG */
	clms_node_reconfigured_ntf(clms_cb, node);
	node->change = SA_CLM_NODE_NO_CHANGE;

	/*
	   If node is also created through CCB when standby
	   is coming up then standby may not read this node from IMM. Also
	   Standby will not get node information from COLD sync also (active
	   shares only node_id based db during cold sync). So send full node
	   record.
	 */
	send_async_update_for_node_rec(node);
	ckpt_cluster_rec();

	TRACE_LEAVE();
	return rc;
}

SaAisErrorT clms_node_ccb_apply_cb(CcbUtilOperationData_t *opdata)
{
	SaAisErrorT rc = SA_AIS_ERR_BAD_OPERATION;
	CLMS_CLUSTER_NODE *node = NULL;
	CLMS_CKPT_REC ckpt;
#ifdef ENABLE_AIS_PLM
	SaNameT *entityNames = NULL;
#endif

	TRACE_ENTER2("%s, %llu", opdata->objectName.value, opdata->ccbId);

	switch (opdata->operationType) {
	case CCBUTIL_CREATE:
		clms_node_add_to_model(opdata->userData);
		saflog(LOG_NOTICE, clmSvcUsrName, "%s ADDED",
		       opdata->objectName.value);
/*clms_cluster_update_rattr(osaf_cluster); */
/*Checkpointing also need to be done */
/* Add to the plm entity group */
#ifdef ENABLE_AIS_PLM

		node = (CLMS_CLUSTER_NODE *)opdata->userData;

		if (clms_cb->reg_with_plm == SA_TRUE) {

			if (node->ee_name.length != 0) {
				entityNames =
				    (SaNameT *)malloc(sizeof(SaNameT));
				memset(entityNames, 0, sizeof(SaNameT));
				entityNames->length = node->ee_name.length;
				(void)memcpy(entityNames->value,
					     node->ee_name.value,
					     entityNames->length);

				rc = saPlmEntityGroupAdd(
				    clms_cb->ent_group_hdl, entityNames, 1,
				    SA_PLM_GROUP_SINGLE_ENTITY);
				if (rc != SA_AIS_OK) {
					LOG_ER(
					    "saPlmEntityGroupAdd FAILED rc = %d",
					    rc);
					return rc;
				}
				free(entityNames);
			}
		}
#endif

		/* Need to discuss - if need to send Clmtrack callback with node
		   join from here as node create might not imply node member */
		if (clms_cb->ha_state == SA_AMF_HA_ACTIVE)
			send_async_update_for_node_rec(opdata->userData);
		break;
	case CCBUTIL_MODIFY:
		saflog(LOG_NOTICE, clmSvcUsrName, "%s MODIFIED",
		       opdata->objectName.value);
		rc = clms_node_ccb_apply_modify(opdata);
		if (rc != SA_AIS_OK)
			goto done;

		if (clms_cb->ha_state == SA_AMF_HA_ACTIVE) {

			memset(&ckpt, 0, sizeof(CLMS_CKPT_REC));
			ckpt.header.type = CLMS_CKPT_NODE_CONFIG_REC;
			ckpt.header.num_ckpt_records = 1;
			ckpt.header.data_len = 1;

			node = clms_node_get_by_name(&opdata->objectName);
			prepare_ckpt_config_node(&ckpt.param.node_config_rec,
						 node);

			rc = clms_send_async_update(clms_cb, &ckpt,
						    NCS_MBCSV_ACT_ADD);
			if (rc != NCSCC_RC_SUCCESS)
				TRACE("send_async_update FAILED rc = %u",
				      (unsigned int)rc);
		}

		break;
	case CCBUTIL_DELETE:
		saflog(LOG_NOTICE, clmSvcUsrName, "%s DELETED",
		       opdata->objectName.value);
		if ((node = clms_node_get_by_name(&opdata->objectName)) == NULL)
			goto done;

		rc = clms_send_is_member_info(clms_cb, node->node_id, false,
					      false);
		if (rc != NCSCC_RC_SUCCESS) {
			TRACE("clms_send_is_member_info FAILED rc = %u",
			      (unsigned int)rc);
			goto done;
		}

		if (ncs_patricia_tree_get(&clms_cb->id_lookup,
					  (uint8_t *)&node->node_id)) {
			clms_node_delete(node, 0);
		}
		clms_node_delete(node, 1);
		clms_node_delete(node, 2);
		clms_cluster_update_rattr(osaf_cluster);

#ifdef ENABLE_AIS_PLM
		/*Delete it from the plm entity group */
		entityNames = &node->ee_name;
		if (clms_cb->reg_with_plm == SA_TRUE) {
			rc = saPlmEntityGroupRemove(clms_cb->ent_group_hdl,
						    entityNames, 1);
			if (rc != SA_AIS_OK) {
				LOG_ER("saPlmEntityGroupAdd FAILED rc = %d",
				       rc);
				return rc;
			}
		}
#endif
		free(node);

		/*Checkpointing also need to be done */
		if (clms_cb->ha_state == SA_AMF_HA_ACTIVE) {

			memset(&ckpt, 0, sizeof(CLMS_CKPT_REC));
			ckpt.header.type = CLMS_CKPT_NODE_DEL_REC;
			ckpt.header.num_ckpt_records = 1;
			ckpt.header.data_len = 1;

			ckpt.param.node_del_rec.node_name.length =
			    opdata->objectName.length;
			(void)memcpy(ckpt.param.node_del_rec.node_name.value,
				     opdata->objectName.value,
				     opdata->objectName.length);

			rc = clms_send_async_update(clms_cb, &ckpt,
						    NCS_MBCSV_ACT_ADD);
			if (rc != NCSCC_RC_SUCCESS)
				TRACE("send_async_update FAILED rc = %u",
				      (unsigned int)rc);
		}

		break;
	default:
		osafassert(0);
		break;
	}

	TRACE_LEAVE();
	return SA_AIS_OK;
done:
	TRACE_LEAVE();
	return rc;
}

static void clms_imm_ccb_apply_callback(SaImmOiHandleT immOiHandle,
					SaImmOiCcbIdT ccbId)
{
	CcbUtilOperationData_t *opdata = NULL;
	SaAisErrorT rc;

	TRACE_ENTER2("CCB ID %llu", ccbId);

	while ((opdata = ccbutil_getNextCcbOp(ccbId, opdata)) != NULL) {
		rc = clms_node_ccb_apply_cb(opdata);
		if (rc != SA_AIS_OK) {
			LOG_EM("clms_node_ccb_apply_cb failed");
		}
	}

	TRACE_LEAVE();
}

/**
 * Add to the patricia tree
 * @param[in] node pointer to cluster node
 */
void clms_node_add_to_model(CLMS_CLUSTER_NODE *node)
{

	TRACE_ENTER();

	osafassert(node != NULL);
	if (node->ee_name.length != 0) {
		/*If the Node is already added to patricia tree;then ignore it
		 */
		if (clms_node_get_by_eename(&node->ee_name) == NULL) {
			if (clms_cb->reg_with_plm == SA_TRUE) {
				if (clms_node_add(node, 1) !=
				    NCSCC_RC_SUCCESS) {
					osafassert(0);
				}
			}
		}
	}

	/*If Cluster Node already is already added to patricia tree;then ignore
	 * it  */
	if (clms_node_get_by_name(&node->node_name) == NULL) {
		TRACE("Not in patricia tree");

		if (clms_node_add(node, 2) != NCSCC_RC_SUCCESS) {
			if (clms_node_delete(node, 1) != NCSCC_RC_SUCCESS)
				TRACE(
				    "patricia tree deleted failed for eename");

			osafassert(0);
		}
	}

	TRACE_LEAVE();
}

static const SaImmOiCallbacksT_2 callbacks = {
    .saImmOiAdminOperationCallback = clms_imm_admin_op_callback,
    .saImmOiCcbAbortCallback = clms_imm_ccb_abort_callback,
    .saImmOiCcbApplyCallback = clms_imm_ccb_apply_callback,
    .saImmOiCcbCompletedCallback = clms_imm_ccb_completed_callback,
    .saImmOiCcbObjectCreateCallback = clms_imm_ccb_obj_create_callback,
    .saImmOiCcbObjectDeleteCallback = clms_imm_ccb_obj_delete_callback,
    .saImmOiCcbObjectModifyCallback = clms_imm_ccb_obj_modify_callback,
    .saImmOiRtAttrUpdateCallback = NULL};

/**
 * Initialize the OI interface and get a selection object.
 * @param cb
 *
 * @return SaAisErrorT
 */
uint32_t clms_imm_init(CLMS_CB *cb)
{
	uint32_t rc = NCSCC_RC_SUCCESS;
	SaAisErrorT ais_rc = SA_AIS_OK;
	immutilWrapperProfile.errorsAreFatal = 0;
	TRACE_ENTER();
	if ((ais_rc = immutil_saImmOiInitialize_2(&cb->immOiHandle, &callbacks,
						  &immVersion)) != SA_AIS_OK) {
		LOG_ER("saImmOiInitialize_2 failed %u", rc);
		rc = NCSCC_RC_FAILURE;
		goto done;
	}

	if ((ais_rc = immutil_saImmOiSelectionObjectGet(
		 cb->immOiHandle, &cb->imm_sel_obj)) != SA_AIS_OK) {
		LOG_ER("saImmOiSelectionObjectGet failed %u", rc);
		rc = NCSCC_RC_FAILURE;
		goto done;
	}

	TRACE_LEAVE();

done:
	return rc;
}

/**
 *  Signal handler on lock timer expiry.
 */
void clms_lock_timer_exp(int signo, siginfo_t *info, void *context)
{
	CLMS_CLUSTER_NODE *node = NULL;
	TRACE_ENTER();

	/* acquire cb write lock */
	/*m_NCS_LOCK(&clms_cb->lock, NCS_LOCK_WRITE); */

	node = (CLMS_CLUSTER_NODE *)info->si_value.sival_ptr;

	TRACE("node on which lock was performed %s", node->node_name.value);

	clms_timer_ipc_send(node->node_name);

	/*m_NCS_UNLOCK(&clms_cb->lock, NCS_LOCK_WRITE); */

	TRACE_LEAVE();
}

/**
 * Send the timer expiry event to the mailbox
 * @param[in] node_id  timer expired for admin operation on  node_id
 */
static void clms_timer_ipc_send(SaNameT node_name)
{
	CLMSV_CLMS_EVT *clmsv_evt;
	uint32_t rc = NCSCC_RC_SUCCESS;
	TRACE_ENTER();

	/** allocate an CLMSV_CLMS_EVENT now **/
	if (NULL == (clmsv_evt = calloc(1, sizeof(CLMSV_CLMS_EVT)))) {
		LOG_ER("memory alloc FAILED");
		osafassert(0);
	}

	memset(clmsv_evt, 0, sizeof(CLMSV_CLMS_EVT));
	clmsv_evt->type = CLMSV_CLMS_NODE_LOCK_TMR_EXP;
	clmsv_evt->info.tmr_info.node_name = node_name;

	TRACE("clmsv_evt.type %d,node_name %s", clmsv_evt->type,
	      clmsv_evt->info.tmr_info.node_name.value);

	rc = m_NCS_IPC_SEND(&clms_cb->mbx, clmsv_evt,
			    MDS_SEND_PRIORITY_VERY_HIGH);
	if (rc != NCSCC_RC_SUCCESS) {
		LOG_ER("IPC send failed %d", rc);
		free(clmsv_evt);
	}

	TRACE_LEAVE();
}

/**
 * This sends the track callback when there are no start step subscribers
 */
static uint32_t clms_lock_send_no_start_cbk(CLMS_CLUSTER_NODE *nodeop)
{
	uint32_t rc = NCSCC_RC_SUCCESS;

	TRACE_ENTER();
	nodeop->change = SA_CLM_NODE_LEFT;
	if (nodeop->member == SA_TRUE) {
		--(osaf_cluster->num_nodes);
	}

	nodeop->member = SA_FALSE;
	nodeop->stat_change = SA_TRUE;
	nodeop->admin_state = SA_CLM_ADMIN_LOCKED;
	++(clms_cb->cluster_view_num);

	/* Update immsv before sending completed callback to clm agents*/
	clms_admin_state_update_rattr(nodeop);
	clms_node_update_rattr(nodeop);
	clms_cluster_update_rattr(osaf_cluster);

	clms_send_track(clms_cb, nodeop, SA_CLM_CHANGE_COMPLETED, false);

	/*Clear admin_op and stat_change */
	nodeop->admin_op = 0;
	nodeop->stat_change = SA_FALSE;

	(void)immutil_saImmOiAdminOperationResult(
	    clms_cb->immOiHandle, nodeop->curr_admin_inv, SA_AIS_OK);

	nodeop->change = SA_CLM_NODE_NO_CHANGE;

	/* Send node leave notification */
	clms_node_admin_state_change_ntf(clms_cb, nodeop, SA_CLM_ADMIN_LOCKED);

	clms_node_exit_ntf(clms_cb, nodeop);

	rc = clms_send_is_member_info(clms_cb, nodeop->node_id, nodeop->member,
				      true);
	if (rc != NCSCC_RC_SUCCESS) {
		TRACE("clms_send_is_member_info %u", rc);
	}

	if (nodeop->disable_reboot == SA_FALSE) {
		if (clms_cb->reg_with_plm == SA_TRUE) {
			clms_reboot_remote_node(
			    nodeop,
			    "Clm lock:no start subscriber and  disable reboot set to false");
		} /* Without PLM in system,till now there is no mechanism to
		     reboot remote node*/
	}
	TRACE_LEAVE();
	return rc;
}

/**
 * Performs the admin lock operation on the node
 */
uint32_t clms_imm_node_lock(CLMS_CLUSTER_NODE *nodeop)
{

	uint32_t rc = NCSCC_RC_SUCCESS;
	TRACE_ENTER2("Node name: %s to lock", nodeop->node_name.value);

	if (nodeop->admin_state == SA_CLM_ADMIN_LOCKED) {
		LOG_ER("Node is already locked");
		nodeop->admin_op = 0;
		rc = NCSCC_RC_FAILURE;
		(void)immutil_saImmOiAdminOperationResult(
		    clms_cb->immOiHandle, nodeop->curr_admin_inv,
		    SA_AIS_ERR_NO_OP);
		goto done;
	}

	if (nodeop->node_id == clms_cb->node_id) {
		LOG_NO("Lock on active node not allowed");
		nodeop->admin_op = 0;
		rc = NCSCC_RC_FAILURE;
		(void)immutil_saImmOiAdminOperationResult(
		    clms_cb->immOiHandle, nodeop->curr_admin_inv,
		    SA_AIS_ERR_NOT_SUPPORTED);
		goto done;
	}

	if (nodeop->member == true) {

		/*No Subscribers for start step */
		nodeop->stat_change = SA_TRUE;
		rc = clms_chk_sub_exist(SA_TRACK_START_STEP);

		if (rc == NCSCC_RC_SUCCESS) {

			TRACE("Clients exist for the start step");
			clms_lock_send_start_cbk(nodeop);

		} else {
			TRACE("No Clients for start step");
			rc = clms_lock_send_no_start_cbk(nodeop);
			if (rc != NCSCC_RC_SUCCESS) {
				TRACE("clms_lock_send_no_start_cbk failed");
				goto done;
			}
		}
	} else {
		TRACE("Node is not a member node");
		nodeop->admin_state = SA_CLM_ADMIN_LOCKED;
		nodeop->admin_op = 0;
		nodeop->stat_change = SA_FALSE;

		/*except admin state other update are not */
		/*required */
		clms_admin_state_update_rattr(nodeop);
		clms_node_update_rattr(nodeop);
		clms_cluster_update_rattr(osaf_cluster);

		(void)immutil_saImmOiAdminOperationResult(
		    clms_cb->immOiHandle, nodeop->curr_admin_inv, SA_AIS_OK);

		/*Send Notification */
		clms_node_admin_state_change_ntf(clms_cb, nodeop,
						 SA_CLM_ADMIN_LOCKED);
	}
done:
	TRACE_LEAVE();
	return rc;
}

/**
 * Perform the admin unlock operation on the node
 */
uint32_t clms_imm_node_unlock(CLMS_CLUSTER_NODE *nodeop)
{
	uint32_t rc = NCSCC_RC_SUCCESS;
	TRACE_ENTER2("Node name %s to unlock", nodeop->node_name.value);
	SaAisErrorT ais_rc = SA_AIS_OK;
	if (nodeop->admin_state == SA_CLM_ADMIN_UNLOCKED) {
		LOG_NO("Node is already in an unlocked state");
		nodeop->admin_op = 0;
		(void)immutil_saImmOiAdminOperationResult(
		    clms_cb->immOiHandle, nodeop->curr_admin_inv,
		    SA_AIS_ERR_NO_OP);
		rc = NCSCC_RC_FAILURE;
		goto done;
	}
	if (((nodeop->admin_state == SA_CLM_ADMIN_LOCKED) ||
	     (nodeop->admin_state == SA_CLM_ADMIN_SHUTTING_DOWN))) {

		if (clms_cb->reg_with_plm == SA_FALSE) {
			if (nodeop->nodeup == SA_TRUE) {

				if (nodeop->member == SA_FALSE) {
					++(osaf_cluster->num_nodes);
				}

				/* nodeup true  ==> cluster member */
				nodeop->member = SA_TRUE;
				nodeop->admin_state = SA_CLM_ADMIN_UNLOCKED;
				nodeop->init_view =
				    ++(clms_cb->cluster_view_num);
				nodeop->stat_change = SA_TRUE;
				nodeop->change = SA_CLM_NODE_JOINED;

				/*update immsv*/
				clms_admin_state_update_rattr(nodeop);
				clms_node_update_rattr(nodeop);
				clms_cluster_update_rattr(osaf_cluster);

				/*Send Callback to its clienst */
				clms_send_track(clms_cb, nodeop,
						SA_CLM_CHANGE_COMPLETED, false);

				/*Send node join notification */
				clms_node_join_ntf(clms_cb, nodeop);

				rc = clms_send_is_member_info(
				    clms_cb, nodeop->node_id, nodeop->member,
				    true);
				if (rc != NCSCC_RC_SUCCESS) {
					TRACE(
					    "clms_send_is_member_info failed %u",
					    rc);
					ais_rc = SA_AIS_ERR_TIMEOUT;
				}
				nodeop->change = SA_CLM_NODE_NO_CHANGE;
			}
		} else {
#ifdef ENABLE_AIS_PLM
			if ((nodeop->ee_red_state ==
			     SA_PLM_READINESS_IN_SERVICE) &&
			    (nodeop->nodeup == SA_TRUE)) {
				if (nodeop->member == SA_FALSE) {
					++(osaf_cluster->num_nodes);
				}
				/* SA_PLM_READINESS_IN_SERVICE ==> cluster
				 * member */
				nodeop->member = SA_TRUE;
				nodeop->admin_state = SA_CLM_ADMIN_UNLOCKED;
				nodeop->init_view =
				    ++(clms_cb->cluster_view_num);
				nodeop->stat_change = SA_TRUE;
				nodeop->change = SA_CLM_NODE_JOINED;

				/*update immsv*/
				clms_admin_state_update_rattr(nodeop);
				clms_node_update_rattr(nodeop);
				clms_cluster_update_rattr(osaf_cluster);

				/*Send Callback to its clients */
				clms_send_track(clms_cb, nodeop,
						SA_CLM_CHANGE_COMPLETED, false);

				/*Send node join notification */
				clms_node_join_ntf(clms_cb, nodeop);

				rc = clms_send_is_member_info(
				    clms_cb, nodeop->node_id, nodeop->member,
				    true);
				if (rc != NCSCC_RC_SUCCESS) {
					TRACE(
					    "clms_send_is_member_info failed %u",
					    rc);
					goto done;
				}
				nodeop->change = SA_CLM_NODE_NO_CHANGE;

			} else if (nodeop->ee_red_state !=
				   SA_PLM_READINESS_IN_SERVICE) {

				nodeop->member = SA_FALSE;
				nodeop->admin_state = SA_CLM_ADMIN_UNLOCKED;
				/*clms_send_is_member_info should not be called,
				 * node is down*/
				/*update immsv*/
				clms_admin_state_update_rattr(nodeop);
				clms_node_update_rattr(nodeop);
				clms_cluster_update_rattr(osaf_cluster);
			}
#endif
		}
	}

	/* Update IMM in the case when node is not up */
	if (nodeop->nodeup != SA_TRUE) {
		nodeop->admin_state = SA_CLM_ADMIN_UNLOCKED;
		clms_admin_state_update_rattr(nodeop);
	}

	/*Clear stat_change and admin_op after sending the callback */
	nodeop->stat_change = SA_FALSE;
	nodeop->admin_op = 0;

	/* Send node join notification */
	(void)immutil_saImmOiAdminOperationResult(
	    clms_cb->immOiHandle, nodeop->curr_admin_inv, ais_rc);
	clms_node_admin_state_change_ntf(clms_cb, nodeop,
					 SA_CLM_ADMIN_UNLOCKED);
done:
	TRACE_LEAVE();
	return rc;
}

/**
 * Perform the admin shutdown operation on the node
 */
uint32_t clms_imm_node_shutdown(CLMS_CLUSTER_NODE *nodeop)
{
	uint32_t rc = NCSCC_RC_SUCCESS;
	TRACE_ENTER();

	if ((nodeop->admin_state == SA_CLM_ADMIN_LOCKED) ||
	    (nodeop->admin_state == SA_CLM_ADMIN_SHUTTING_DOWN)) {
		LOG_ER("Node is already in locked or shutting down state");
		nodeop->admin_op = 0;
		rc = NCSCC_RC_FAILURE;
		(void)immutil_saImmOiAdminOperationResult(
		    clms_cb->immOiHandle, nodeop->curr_admin_inv,
		    SA_AIS_ERR_NO_OP);
		goto done;
	}

	if (nodeop->node_id == clms_cb->node_id) {
		LOG_ER("Shutdown of an active node not allowed");
		nodeop->admin_op = 0;
		rc = NCSCC_RC_FAILURE;
		(void)immutil_saImmOiAdminOperationResult(
		    clms_cb->immOiHandle, nodeop->curr_admin_inv,
		    SA_AIS_ERR_NOT_SUPPORTED);
		goto done;
	}

	if ((nodeop->member == SA_FALSE) &&
	    (nodeop->admin_state == SA_CLM_ADMIN_UNLOCKED)) {

		nodeop->admin_state = SA_CLM_ADMIN_LOCKED;
		nodeop->admin_op = 0;
		(void)immutil_saImmOiAdminOperationResult(
		    clms_cb->immOiHandle, nodeop->curr_admin_inv, SA_AIS_OK);
		clms_admin_state_update_rattr(nodeop);

	} else {

		rc = clms_chk_sub_exist(SA_TRACK_START_STEP);
		if (rc == NCSCC_RC_SUCCESS) {
			nodeop->admin_state = SA_CLM_ADMIN_SHUTTING_DOWN;
			nodeop->stat_change = SA_TRUE;
			nodeop->change = SA_CLM_NODE_SHUTDOWN;
			clms_send_track(clms_cb, nodeop, SA_CLM_CHANGE_START,
					false);

			clms_node_admin_state_change_ntf(
			    clms_cb, nodeop, SA_CLM_ADMIN_SHUTTING_DOWN);

			/* update imm about state change */
			clms_admin_state_update_rattr(nodeop);
			clms_node_update_rattr(nodeop);
			clms_cluster_update_rattr(osaf_cluster);

		} else {
			nodeop->admin_state = SA_CLM_ADMIN_LOCKED;
			nodeop->stat_change = SA_TRUE;
			if (nodeop->member == SA_TRUE) {
				--(osaf_cluster->num_nodes);
			}
			nodeop->member = SA_FALSE;
			nodeop->change = SA_CLM_NODE_SHUTDOWN;
			++(clms_cb->cluster_view_num);

			/*update immsv*/
			clms_admin_state_update_rattr(nodeop);
			clms_node_update_rattr(nodeop);
			clms_cluster_update_rattr(osaf_cluster);

			clms_send_track(clms_cb, nodeop,
					SA_CLM_CHANGE_COMPLETED, false);

			/*Clear Admin_op and stat_change */
			nodeop->admin_op = 0;
			nodeop->stat_change = SA_FALSE;

			(void)immutil_saImmOiAdminOperationResult(
			    clms_cb->immOiHandle, nodeop->curr_admin_inv,
			    SA_AIS_OK);

			nodeop->change = SA_CLM_NODE_NO_CHANGE;

			clms_node_exit_ntf(clms_cb, nodeop);

			clms_node_admin_state_change_ntf(clms_cb, nodeop,
							 SA_CLM_ADMIN_LOCKED);

			rc = clms_send_is_member_info(clms_cb, nodeop->node_id,
						      nodeop->member, true);
			if (rc != NCSCC_RC_SUCCESS) {
				TRACE("clms_send_is_member_info failed %u", rc);
				goto done;
			}

			if (nodeop->disable_reboot == SA_FALSE) {
				if (clms_cb->reg_with_plm == SA_TRUE) {
					clms_reboot_remote_node(
					    nodeop,
					    "Clm Shutdown:no start subscriber and  disable reboot set to false");
				} /* Without PLM in system,till now there is no
				     mechanism to reboot remote node*/
			}
		}
	}
done:
	TRACE_LEAVE();
	return rc;
}

/**
 * Start the timer for admin lock operation
 */
static void clms_lock_send_start_cbk(CLMS_CLUSTER_NODE *nodeop)
{
	struct itimerspec timer;
	struct sigevent signal_spec;
	struct sigaction act;

	TRACE_ENTER2("Node name: %s to lock", nodeop->node_name.value);

	signal_spec.sigev_notify = SIGEV_SIGNAL;
	signal_spec.sigev_signo = SIGALRM;
	signal_spec.sigev_value.sival_ptr = nodeop;

	act.sa_sigaction = clms_lock_timer_exp;
	act.sa_flags = SA_SIGINFO;

	nodeop->change = SA_CLM_NODE_LEFT;
	nodeop->stat_change = SA_TRUE;

	clms_send_track(clms_cb, nodeop, SA_CLM_CHANGE_START, false);
	if (sigaction(SIGALRM, &act, NULL) != 0) {
		TRACE("Sigaction failed");
		osafassert(0);
	}
	/*Create the lock Callbck timer */
	if ((timer_create(CLOCK_REALTIME, &signal_spec,
			  &nodeop->lock_timerid)) != 0) {
		TRACE("Creating Lock Timer failed");
		osafassert(0);
	}
	TRACE("Timer creation successful");

	/* Assumed that the timer expiration value be specified in nanoseconds
	 */
	timer.it_value.tv_sec =
	    (time_t)(nodeop->lck_cbk_timeout / sec_to_nanosec);
	timer.it_value.tv_nsec =
	    (long int)(nodeop->lck_cbk_timeout % sec_to_nanosec);
	timer.it_interval.tv_sec = 0;
	timer.it_interval.tv_nsec = 0;

	if ((timer_settime(nodeop->lock_timerid, 0, &timer, NULL)) != 0) {
		LOG_ER("Setting lock timer value failed");
		osafassert(0);
	}
	TRACE("Timer started");
	TRACE_LEAVE();
}

/**
 * Initialize the OI interface and get a selection object.
 * @param cb
 *
 * @return SaAisErrorT
 */
static void *clm_imm_reinit_thread(void *_cb)
{
	SaAisErrorT ais_rc = SA_AIS_OK;
	CLMS_CB *cb = (CLMS_CB *)_cb;

	TRACE_ENTER();
	int msecs_waited = 0;
	for (;;) {
		if (msecs_waited >= max_waiting_time_ms) {
			LOG_ER("Timeout in clm_imm_reinit_thread, exiting");
			exit(EXIT_FAILURE);
		}

		if ((ais_rc = immutil_saImmOiInitialize_2(
			 &cb->immOiHandle, &callbacks, &immVersion)) !=
		    SA_AIS_OK) {
			LOG_ER("saImmOiInitialize_2 failed %u, exiting",
			       ais_rc);
			exit(EXIT_FAILURE);
		}

		if ((ais_rc = immutil_saImmOiSelectionObjectGet(
			 cb->immOiHandle, &cb->imm_sel_obj)) != SA_AIS_OK) {
			LOG_ER("saImmOiSelectionObjectGet failed %u, exiting",
			       ais_rc);
			exit(EXIT_FAILURE);
		}

		if (cb->ha_state == SA_AMF_HA_ACTIVE) {
			/* Update IMM */
			if ((ais_rc = immutil_saImmOiImplementerSet(
				 cb->immOiHandle, IMPLEMENTER_NAME)) !=
			    SA_AIS_OK) {
				if (ais_rc == SA_AIS_ERR_TIMEOUT) {
					LOG_WA("saImmOiImplementerSet returned "
					       "%u",
					       (unsigned)ais_rc);
					usleep(sleep_delay_ms * 1000);
					msecs_waited += sleep_delay_ms;
					saImmOiFinalize(cb->immOiHandle);
					cb->immOiHandle = 0;
					cb->imm_sel_obj = -1;
					cb->is_impl_set = false;
					continue;
				}
				LOG_ER("saImmOiImplementerSet failed rc: %u, "
				       "exiting",
				       (unsigned)ais_rc);
				exit(EXIT_FAILURE);
			}

			if ((ais_rc = immutil_saImmOiClassImplementerSet(
				 cb->immOiHandle, "SaClmNode")) != SA_AIS_OK) {
				if (ais_rc == SA_AIS_ERR_TIMEOUT) {
					LOG_WA("saImmOiClassImplementerSet "
					       "returned %u",
					       (unsigned)ais_rc);
					usleep(sleep_delay_ms * 1000);
					msecs_waited += sleep_delay_ms;
					saImmOiFinalize(cb->immOiHandle);
					cb->immOiHandle = 0;
					cb->imm_sel_obj = -1;
					cb->is_impl_set = false;
					continue;
				}
				LOG_ER("saImmOiClassImplementerSet failed for "
				       "class SaClmNode rc: %u, exiting",
				       (unsigned)ais_rc);
				exit(EXIT_FAILURE);
			}

			if ((ais_rc = immutil_saImmOiClassImplementerSet(
				 cb->immOiHandle, "SaClmCluster")) !=
			    SA_AIS_OK) {
				if (ais_rc == SA_AIS_ERR_TIMEOUT) {
					LOG_WA("saImmOiClassImplementerSet "
					       "returned %u",
					       (unsigned)ais_rc);
					usleep(sleep_delay_ms * 1000);
					msecs_waited += sleep_delay_ms;
					saImmOiFinalize(cb->immOiHandle);
					cb->immOiHandle = 0;
					cb->imm_sel_obj = -1;
					cb->is_impl_set = false;
					continue;
				}
				LOG_ER("saImmOiClassImplementerSet failed for "
				       "class SaClmCluster rc: %u, exiting",
				       (unsigned)ais_rc);
				exit(EXIT_FAILURE);
			}
			cb->is_impl_set = true;
		}
		break;
	}

	TRACE_LEAVE();
	return NULL;
}

/**
 * Become object and class implementer, non-blocking.
 * @param cb
 */
void clm_imm_reinit_bg(CLMS_CB *cb)
{
	pthread_t thread;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	TRACE_ENTER();

	if (pthread_create(&thread, &attr, clm_imm_reinit_thread, cb) != 0) {
		LOG_ER("pthread_create FAILED: %s, exiting", strerror(errno));
		exit(EXIT_FAILURE);
	}
	pthread_attr_destroy(&attr);
	TRACE_LEAVE();
}
