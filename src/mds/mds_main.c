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

#ifdef HAVE_CONFIG_H
#include "osaf/config.h"
#endif

#include "osaf/configmake.h"

/*****************************************************************************
..............................................................................

..............................................................................

  DESCRIPTION:  MDS layer initialization and destruction entry points

******************************************************************************
*/

#include <pthread.h>
#include "base/ncsgl_defs.h"
#include "base/ncssysf_def.h"
#include "base/ncs_lib.h"
#include "base/ncssysf_tmr.h"
#include "mds/mds_dl_api.h"
#include "mds_core.h"
#include "base/ncspatricia.h"
#include "mds_log.h"
#include "base/ncs_main_pub.h"
#include "mds_dt_tcp.h"
#include "mds_dt_tcp_disc.h"
#include "mds_dt_tcp_trans.h"
#include <netdb.h>
#include "base/osaf_utility.h"
#include "base/osaf_poll.h"
#include "base/osaf_secutil.h"
#ifdef ENABLE_TIPC_TRANSPORT
#include "mds_dt_tipc.h"
#define MDS_MDTM_CONNECT_PATH PKGLOCALSTATEDIR "/osaf_dtm_intra_server"
#endif

extern uint32_t mds_socket_domain;
void mds_init_transport(void);
bool tipc_mode_enabled = false;
bool tipc_mcast_enabled = true;

/* MDS Control Block */
MDS_MCM_CB *gl_mds_mcm_cb = NULL;

/* See mds_core.h for description of this global variable. */
pthread_mutex_t gl_mds_library_mutex;

/* pthreads once control used for initialising s_mds_library_mutex */
static pthread_once_t s_mds_mutex_init_once_control = PTHREAD_ONCE_INIT;

/**
 * @brief Helper function to be used only by mds_mutex_init_once()
 *
 * This is a helper function used by the function mds_mutex_init_once(). It
 * should only be used in that function.
 */
static void mds_mutex_init_once_routine(void)
{
	pthread_mutexattr_t attr;
	int result;
	result = pthread_mutexattr_init(&attr);
	if (result != 0)
		osaf_abort(result);
	result = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	if (result != 0)
		osaf_abort(result);
	result = pthread_mutex_init(&gl_mds_library_mutex, &attr);
	if (result != 0)
		osaf_abort(result);
	result = pthread_mutexattr_destroy(&attr);
	if (result != 0)
		osaf_abort(result);
}

/**
 * @brief Initialise the MDS library mutex exactly once in a thread safe way
 *
 * This function initialises the MDS library mutex. It is thread safe and
 * guarantees that the mutex is initialised only once even if this function is
 * called from multiple threads concurrently. It is also safe to call this
 * function again when the MDS library mutex already has been initialised.
 */
static void mds_mutex_init_once(void)
{
	int result = pthread_once(&s_mds_mutex_init_once_control,
				  mds_mutex_init_once_routine);
	if (result != 0)
		osaf_abort(result);
}

/* global Log level variable */
uint32_t gl_mds_checksum = 0;

uint32_t MDS_QUIESCED_TMR_VAL = 80;
uint32_t MDS_AWAIT_ACTIVE_TMR_VAL = 18000;
uint32_t MDS_SUBSCRIPTION_TMR_VAL = 500;
uint32_t MDTM_REASSEMBLE_TMR_VAL = 500;
uint32_t MDTM_CACHED_EVENTS_TMR_VAL = 24000;

enum { MDS_REGISTER_REQ = 77,
       MDS_REGISTER_RESP = 78,
       MDS_UNREGISTER_REQ = 79,
       MDS_UNREGISTER_RESP = 80 };

/**
 * Handler for mds register requests
 * Note: executed by and in context of the auth thread!
 * Communicates with the main thread (where the
 * real work is done) to get outcome of initialization request which is then
 * sent back to the client.
 * @param fd
 * @param creds credentials for client
 */
static void mds_register_callback(int fd, const struct ucred *creds)
{
	uint8_t buf[32];
	uint8_t *p = buf;

	TRACE_ENTER2("fd:%d, pid:%u", fd, creds->pid);

	int n = recv(fd, buf, sizeof(buf), 0);
	if (n == -1) {
		syslog(LOG_ERR, "MDS: %s: recv failed - %s", __FUNCTION__,
		       strerror(errno));
		goto done;
	}

	if (n != 16) {
		syslog(LOG_ERR, "MDS: %s: recv failed - %d bytes", __FUNCTION__,
		       n);
		goto done;
	}

	int type = ncs_decode_32bit(&p);

	NCSMDS_SVC_ID svc_id = ncs_decode_32bit(&p);
	MDS_DEST mds_dest = ncs_decode_64bit(&p);

	TRACE("MDS: received %d from %" PRIx64 ", pid %d", type, mds_dest,
	      creds->pid);

	if (type == MDS_REGISTER_REQ) {
		osaf_mutex_lock_ordie(&gl_mds_library_mutex);

		MDS_PROCESS_INFO *info = mds_process_info_get(mds_dest, svc_id);
		if (info == NULL) {
			MDS_PROCESS_INFO *info =
			    calloc(1, sizeof(MDS_PROCESS_INFO));
			osafassert(info);
			info->mds_dest = mds_dest;
			info->svc_id = svc_id;
			info->uid = creds->uid;
			info->pid = creds->pid;
			info->gid = creds->gid;
			int rc = mds_process_info_add(info);
			osafassert(rc == NCSCC_RC_SUCCESS);
		} else {
			/* when can this happen? */
			LOG_NO("MDS: %s: dest %" PRIx64 " already exist",
			       __FUNCTION__, mds_dest);

			// just update credentials
			info->uid = creds->uid;
			info->pid = creds->pid;
			info->gid = creds->gid;
		}

		osaf_mutex_unlock_ordie(&gl_mds_library_mutex);

		p = buf;
		uint32_t sz = ncs_encode_32bit(&p, MDS_REGISTER_RESP);
		sz += ncs_encode_32bit(&p, 0); // result OK

		if ((n = send(fd, buf, sz, MSG_NOSIGNAL)) == -1)
			syslog(LOG_ERR, "MDS: %s: send to pid %d failed - %s",
			       __FUNCTION__, creds->pid, strerror(errno));
	} else if (type == MDS_UNREGISTER_REQ) {
		osaf_mutex_lock_ordie(&gl_mds_library_mutex);

		MDS_PROCESS_INFO *info = mds_process_info_get(mds_dest, svc_id);
		if (info != NULL) {
			(void)mds_process_info_del(info);
			free(info);
		}
		osaf_mutex_unlock_ordie(&gl_mds_library_mutex);

		p = buf;
		uint32_t sz = ncs_encode_32bit(&p, MDS_UNREGISTER_RESP);
		sz += ncs_encode_32bit(&p, 0); // result OK

		if ((n = send(fd, buf, sz, MSG_NOSIGNAL)) == -1)
			syslog(LOG_ERR, "MDS: %s: send to pid %d failed - %s",
			       __FUNCTION__, creds->pid, strerror(errno));
	} else {
		syslog(LOG_ERR, "MDS: %s: recv failed - wrong type %d",
		       __FUNCTION__, type);
		goto done;
	}

done:
	TRACE_LEAVE();
}

/**
 * Creates an auth server socket for this process
 * @param name file system name of socket
 * @return NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE
 */
int mds_auth_server_create(const char *name)
{
	if (mds_process_info_db_init() != NCSCC_RC_SUCCESS) {
		syslog(LOG_ERR, "MDS: %s: mds_process_info_db_init failed",
		       __FUNCTION__);
		return NCSCC_RC_FAILURE;
	}

	if (osaf_auth_server_create(name, mds_register_callback) != 0) {
		syslog(LOG_ERR, "MDS: %s: osaf_auth_server_create failed",
		       __FUNCTION__);
		return NCSCC_RC_FAILURE;
	}

	return NCSCC_RC_SUCCESS;
}

/**
 * Sends and receives an initialize message using osaf_secutil
 * @param evt_type
 * @param mds_dest
 * @param version
 * @param out_evt
 * @param timeout max time to wait for a response in ms unit
 * @return 0 - OK, negated errno otherwise
 */
int mds_auth_server_connect(const char *name, MDS_DEST mds_dest, int svc_id,
			    int64_t timeout)
{
	uint32_t rc;
	uint8_t msg[32];
	uint8_t *p = msg;
	uint32_t sz;
	int n;

	sz = ncs_encode_32bit(&p, MDS_REGISTER_REQ);
	sz += ncs_encode_32bit(&p, svc_id);
	sz += ncs_encode_64bit(&p, mds_dest);

	n = osaf_auth_server_connect(name, msg, sz, msg, sizeof(msg), timeout);

	if (n < 0) {
		TRACE_3("err n:%d", n);
		rc = NCSCC_RC_FAILURE;
		goto fail;
	} else if (n == 0) {
		TRACE_3("tmo");
		rc = NCSCC_RC_REQ_TIMOUT;
		goto fail;
	} else if (n == 8) {
		p = msg;
		int type = ncs_decode_32bit(&p);
		if (type != MDS_REGISTER_RESP) {
			TRACE_3("wrong type %d", type);
			rc = NCSCC_RC_FAILURE;
			goto fail;
		}
		int status = ncs_decode_32bit(&p);
		TRACE("received type:%d, status:%d", type, status);
		status == 0 ? (rc = NCSCC_RC_SUCCESS) : (rc = NCSCC_RC_FAILURE);
	} else {
		TRACE_3("err n:%d", n);
		rc = NCSCC_RC_FAILURE;
		goto fail;
	}

fail:
	return rc;
}

/**
 * Sends and receives an unregister message using osaf_secutil
 * @param evt_type
 * @param mds_dest
 * @param version
 * @param out_evt
 * @param timeout max time to wait for a response in ms unit
 * @return 0 - OK, negated errno otherwise
 */
int mds_auth_server_disconnect(const char *name, MDS_DEST mds_dest, int svc_id,
			       int64_t timeout)
{
	uint32_t rc;
	uint8_t msg[32];
	uint8_t *p = msg;
	uint32_t sz;
	int n;

	sz = ncs_encode_32bit(&p, MDS_UNREGISTER_REQ);
	sz += ncs_encode_32bit(&p, svc_id);
	sz += ncs_encode_64bit(&p, mds_dest);

	n = osaf_auth_server_connect(name, msg, sz, msg, sizeof(msg), timeout);

	if (n < 0) {
		TRACE_3("err n:%d", n);
		rc = NCSCC_RC_FAILURE;
		goto fail;
	} else if (n == 0) {
		TRACE_3("tmo");
		rc = NCSCC_RC_REQ_TIMOUT;
		goto fail;
	} else if (n == 8) {
		p = msg;
		int type = ncs_decode_32bit(&p);
		if (type != MDS_UNREGISTER_RESP) {
			TRACE_3("wrong type %d", type);
			rc = NCSCC_RC_FAILURE;
			goto fail;
		}
		int status = ncs_decode_32bit(&p);
		TRACE("received type:%d, status:%d", type, status);
		status == 0 ? (rc = NCSCC_RC_SUCCESS) : (rc = NCSCC_RC_FAILURE);
	} else {
		TRACE_3("err n:%d", n);
		rc = NCSCC_RC_FAILURE;
		goto fail;
	}

fail:
	return rc;
}

/* ******************************************** */
/* ******************************************** */
/* ******************************************** */

/*          mds_lib_req                         */

/* ******************************************** */
/* ******************************************** */
/* ******************************************** */

uint32_t mds_lib_req(NCS_LIB_REQ_INFO *req)
{
	char *p_field = NULL;
	uint32_t
	    node_id = 0,
	    mds_tipc_ref =
		0; /* this mds tipc ref is random num part of the TIPC id */
	uint32_t status = NCSCC_RC_SUCCESS;
	NCS_SEL_OBJ destroy_ack_obj;
	char *ptr;

	switch (req->i_op) {
	case NCS_LIB_REQ_CREATE:

		mds_mutex_init_once();
		osaf_mutex_lock_ordie(&gl_mds_library_mutex);

		if (gl_mds_mcm_cb != NULL) {
			syslog(LOG_ERR,
			       "MDS:LIB_CREATE: MDS is already initialized");
			osaf_mutex_unlock_ordie(&gl_mds_library_mutex);
			return NCSCC_RC_FAILURE;
		}

		/* Initialize mcm database */
		mds_mcm_init();

		/* Extract parameters from req and fill adest and pcon_id */

		/* Get Node_id */
		node_id = ncs_get_node_id();

		FILE *fp;
#ifdef ENABLE_TIPC_TRANSPORT
		int rc;
		struct stat sockStat;

		rc = stat(MDS_MDTM_CONNECT_PATH, &sockStat);
		if (rc != 0) {
			/* dtm intra server not exists */
			tipc_mode_enabled = true;
		}

		if (tipc_mode_enabled) {
			/* Get tipc_mcast_enabled */
			if ((ptr = getenv("MDS_TIPC_MCAST_ENABLED")) != NULL) {
				tipc_mcast_enabled = atoi(ptr);
				if (tipc_mcast_enabled != false)
					tipc_mcast_enabled = true;

				m_MDS_LOG_DBG(
				    "MDS: TIPC_MCAST_ENABLED: %d  Set argument \n",
				    tipc_mcast_enabled);
			}
		}
#endif
		fp = fopen(PKGSYSCONFDIR "/node_name", "r");
		if (fp == NULL) {
			LOG_ER("MDS:TIPC Could not open file  node_name ");
			mds_mcm_destroy();
			osaf_mutex_unlock_ordie(&gl_mds_library_mutex);
			return m_LEAP_DBG_SINK(NCSCC_RC_FAILURE);
		}
		if (EOF == fscanf(fp, "%s", gl_mds_mcm_cb->node_name)) {
			fclose(fp);
			LOG_ER("MDS:TIPC Could not get node name ");
			mds_mcm_destroy();
			osaf_mutex_unlock_ordie(&gl_mds_library_mutex);
			return m_LEAP_DBG_SINK(NCSCC_RC_FAILURE);
		}
		fclose(fp);
		gl_mds_mcm_cb->node_name_len =
		    (uint8_t)strlen(gl_mds_mcm_cb->node_name);
		m_MDS_LOG_DBG("MDS:TIPC config->node_name : %s",
			      gl_mds_mcm_cb->node_name);

		/*  to use cluster id in mds prefix? */

		/* Get gl_mds_log_level */
		/*  setting MDS_LOG_LEVEL from environment variable if given */
		if ((ptr = getenv("MDS_LOG_LEVEL")) != NULL) {
			gl_mds_log_level = atoi(ptr);
		} else {

			p_field = NULL;
			p_field = (char *)ncs_util_search_argv_list(
			    req->info.create.argc, req->info.create.argv,
			    "MDS_LOG_LEVEL=");
			if (p_field != NULL) {
				if (sscanf(p_field + strlen("MDS_LOG_LEVEL="),
					   "%d", &gl_mds_log_level) != 1) {
					syslog(
					    LOG_ERR,
					    "MDS:LIB_CREATE: Problem in MDS_LOG_LEVEL argument\n");
					mds_mcm_destroy();
					osaf_mutex_unlock_ordie(
					    &gl_mds_library_mutex);
					return m_LEAP_DBG_SINK(
					    NCSCC_RC_FAILURE);
				}
			}
		}
		/* gl_mds_log_level consistency check */
		if (gl_mds_log_level > 5 || gl_mds_log_level < 1) {
			/* gl_mds_log_level specified is outside range so reset
			 * to Default = 3 */
			gl_mds_log_level = 3;
		}

		/* Get gl_mds_checksum */

		/*  setting MDS_CHECKSUM from environment variable if given */
		if ((ptr = getenv("MDS_CHECKSUM")) != NULL) {
			gl_mds_checksum = atoi(ptr);
		} else {

			p_field = NULL;
			p_field = (char *)ncs_util_search_argv_list(
			    req->info.create.argc, req->info.create.argv,
			    "MDS_CHECKSUM=");
			if (p_field != NULL) {
				if (sscanf(p_field + strlen("MDS_CHECKSUM="),
					   "%d", &gl_mds_checksum) != 1) {
					syslog(
					    LOG_ERR,
					    "MDS:LIB_CREATE: Problem in MDS_CHECKSUM argument\n");
					mds_mcm_destroy();
					osaf_mutex_unlock_ordie(
					    &gl_mds_library_mutex);
					return m_LEAP_DBG_SINK(
					    NCSCC_RC_FAILURE);
				}
			}
		}
		/* gl_mds_checksum consistency check */
		if (gl_mds_checksum != 1) {
			/* gl_mds_checksum specified is not 1 so reset to 0 */
			gl_mds_checksum = 0;
		}

		/*****************************/
		/* Timer value Configuration */
		/*****************************/

		/* Get Subscription timer value */
		p_field = NULL;
		p_field = (char *)ncs_util_search_argv_list(
		    req->info.create.argc, req->info.create.argv,
		    "SUBSCRIPTION_TMR_VAL=");
		if (p_field != NULL) {
			if (sscanf(p_field + strlen("SUBSCRIPTION_TMR_VAL="),
				   "%d", &MDS_SUBSCRIPTION_TMR_VAL) != 1) {
				syslog(
				    LOG_ERR,
				    "MDS:LIB_CREATE: Problem in SUBSCRIPTION_TMR_VAL argument\n");
				mds_mcm_destroy();
				osaf_mutex_unlock_ordie(&gl_mds_library_mutex);
				return m_LEAP_DBG_SINK(NCSCC_RC_FAILURE);
			}
		}

		/* Get Await Active timer value */
		p_field = NULL;
		p_field = (char *)ncs_util_search_argv_list(
		    req->info.create.argc, req->info.create.argv,
		    "AWAIT_ACTIVE_TMR_VAL=");
		if (p_field != NULL) {
			if (sscanf(p_field + strlen("AWAIT_ACTIVE_TMR_VAL="),
				   "%d", &MDS_AWAIT_ACTIVE_TMR_VAL) != 1) {
				syslog(
				    LOG_ERR,
				    "MDS:LIB_CREATE: Problem in AWAIT_ACTIVE_TMR_VAL argument\n");
				mds_mcm_destroy();
				osaf_mutex_unlock_ordie(&gl_mds_library_mutex);
				return m_LEAP_DBG_SINK(NCSCC_RC_FAILURE);
			}
		}

		/* Get Quiesced timer value */
		p_field = NULL;
		p_field = (char *)ncs_util_search_argv_list(
		    req->info.create.argc, req->info.create.argv,
		    "QUIESCED_TMR_VAL=");
		if (p_field != NULL) {
			if (sscanf(p_field + strlen("QUIESCED_TMR_VAL="), "%d",
				   &MDS_QUIESCED_TMR_VAL) != 1) {
				syslog(
				    LOG_ERR,
				    "MDS:LIB_CREATE: Problem in QUIESCED_TMR_VAL argument\n");
				mds_mcm_destroy();
				osaf_mutex_unlock_ordie(&gl_mds_library_mutex);
				return m_LEAP_DBG_SINK(NCSCC_RC_FAILURE);
			}
		}

		/* Get Reassembly timer value */
		p_field = NULL;
		p_field = (char *)ncs_util_search_argv_list(
		    req->info.create.argc, req->info.create.argv,
		    "REASSEMBLE_TMR_VAL=");
		if (p_field != NULL) {
			if (sscanf(p_field + strlen("REASSEMBLE_TMR_VAL="),
				   "%d", &MDTM_REASSEMBLE_TMR_VAL) != 1) {
				syslog(
				    LOG_ERR,
				    "MDS:LIB_CREATE: Problem in REASSEMBLE_TMR_VAL argument\n");
				mds_mcm_destroy();
				osaf_mutex_unlock_ordie(&gl_mds_library_mutex);
				return m_LEAP_DBG_SINK(NCSCC_RC_FAILURE);
			}
		}

		mds_init_transport();
		/* Invoke MDTM-INIT.  */
		status = mds_mdtm_init(node_id, &mds_tipc_ref);
		if (status != NCSCC_RC_SUCCESS) {
			/*  todo cleanup */
			syslog(
			    LOG_ERR,
			    "MDS:LIB_CREATE: mds_mdtm_init failed\n");
			mds_mcm_destroy();
			osaf_mutex_unlock_ordie(&gl_mds_library_mutex);
			return NCSCC_RC_FAILURE;
		}
		gl_mds_mcm_cb->adest =
		    m_MDS_GET_ADEST_FROM_NODE_ID_AND_PROCESS_ID(node_id,
								mds_tipc_ref);
		get_adest_details(gl_mds_mcm_cb->adest,
				  gl_mds_mcm_cb->adest_details);

		/* Initialize logging */
		{
			char buff[50];
			snprintf(buff, sizeof(buff), PKGLOGDIR "/mds.log");
			mds_log_init(buff);
		}

		osaf_mutex_unlock_ordie(&gl_mds_library_mutex);

		break;

	case NCS_LIB_REQ_DESTROY:
		/* STEP 1: Invoke MDTM-Destroy. */
		/* mds_mdtm_destroy (); */
		/* STEP 2: Destroy MCM-CB;      */
		/* ncs_patricia_tree_destroy(&gl_mds_mcm_cb->vdest_list); */
		/* m_MMGR_FREE_MDS_CB(gl_mds_mcm_cb); */

		m_NCS_SEL_OBJ_CREATE(&destroy_ack_obj);

		/* Post a dummy message to MDS thread to guarantee that it
		   wakes up (and thereby sees the destroy_ind) */
		if (mds_destroy_event(destroy_ack_obj) == NCSCC_RC_FAILURE) {
			m_NCS_SEL_OBJ_DESTROY(&destroy_ack_obj);
			return NCSCC_RC_FAILURE;
		}
		/* Wait for indication from MDS thread that it is ok to kill it
		 */
		osaf_poll_one_fd(m_GET_FD_FROM_SEL_OBJ(destroy_ack_obj),
				 70000); /* 70 seconds */
		m_MDS_LOG_DBG(
		    "MDS:LIB_DESTROY: Destroy ack from MDS thread in 70 s");

		/* Take the lock before killing the thread */
		osaf_mutex_lock_ordie(&gl_mds_library_mutex);

		/* Now two things have happened
		   (1) MDS thread has acked the destroy-event. So it will do no
		   further things beyound MDS unlock (2) We have obtained
		   MDS-Lock. So, even the un-lock by MDS thead is completed Now
		   we can proceed with the systematic destruction of MDS
		   internal Data */

		/* Free the objects related to destroy-indication. The destroy
		   mailbox event will be automatically freed by MDS processing
		   or during MDS mailbox destruction. Since we will be
		   destroying the MDS-thread, the following selection-object can
		   no longer be accessed. Hence, it is safe and correct to
		   destroy it now */
		m_NCS_SEL_OBJ_DESTROY(&destroy_ack_obj);
		memset(&destroy_ack_obj, 0,
		       sizeof(destroy_ack_obj)); /* Destroy info */

		/* Sanity check */
		if (gl_mds_mcm_cb == NULL) {
			syslog(LOG_ERR,
			       "MDS:LIB_DESTROY: MDS LIB is already Destroyed");
			osaf_mutex_unlock_ordie(&gl_mds_library_mutex);
			return NCSCC_RC_FAILURE;
		}

		status = mds_mdtm_destroy();
		if (status != NCSCC_RC_SUCCESS) {
			/*  todo anything? */
		}
		status = mds_mcm_destroy();
		if (status != NCSCC_RC_SUCCESS) {
			/*  todo anything? */
		}

		/* Just Unlock the lock, Lock is never destroyed */
		osaf_mutex_unlock_ordie(&gl_mds_library_mutex);
		break;

	default:
		break;
	}

	return NCSCC_RC_SUCCESS;
}

void mds_init_transport(void)
{
#ifdef ENABLE_TIPC_TRANSPORT
	if (tipc_mode_enabled) {
		mds_mdtm_init = mdtm_tipc_init;
		mds_mdtm_destroy = mdtm_tipc_destroy;
		mds_mdtm_svc_subscribe = mds_mdtm_svc_subscribe_tipc;
		mds_mdtm_svc_unsubscribe = mds_mdtm_svc_unsubscribe_tipc;
		mds_mdtm_svc_install = mds_mdtm_svc_install_tipc;
		mds_mdtm_svc_uninstall = mds_mdtm_svc_uninstall_tipc;
		mds_mdtm_vdest_install = mds_mdtm_vdest_install_tipc;
		mds_mdtm_vdest_uninstall = mds_mdtm_vdest_uninstall_tipc;
		mds_mdtm_vdest_subscribe = mds_mdtm_vdest_subscribe_tipc;
		mds_mdtm_vdest_unsubscribe = mds_mdtm_vdest_unsubscribe_tipc;
		mds_mdtm_tx_hdl_register = mds_mdtm_tx_hdl_register_tipc;
		mds_mdtm_tx_hdl_unregister = mds_mdtm_tx_hdl_unregister_tipc;
		mds_mdtm_send = mds_mdtm_send_tipc;
		mds_mdtm_node_subscribe = mds_mdtm_node_subscribe_tipc;
		mds_mdtm_node_unsubscribe = mds_mdtm_node_unsubscribe_tipc;
		return;

	} else {
#endif
		mds_mdtm_init = mds_mdtm_init_tcp;
		mds_mdtm_destroy = mds_mdtm_destroy_tcp;
		mds_mdtm_svc_subscribe = mds_mdtm_svc_subscribe_tcp;
		mds_mdtm_svc_unsubscribe = mds_mdtm_svc_unsubscribe_tcp;
		mds_mdtm_svc_install = mds_mdtm_svc_install_tcp;
		mds_mdtm_svc_uninstall = mds_mdtm_svc_uninstall_tcp;
		mds_mdtm_vdest_install = mds_mdtm_vdest_install_tcp;
		mds_mdtm_vdest_uninstall = mds_mdtm_vdest_uninstall_tcp;
		mds_mdtm_vdest_subscribe = mds_mdtm_vdest_subscribe_tcp;
		mds_mdtm_vdest_unsubscribe = mds_mdtm_vdest_unsubscribe_tcp;
		mds_mdtm_tx_hdl_register = mds_mdtm_tx_hdl_register_tcp;
		mds_mdtm_tx_hdl_unregister = mds_mdtm_tx_hdl_unregister_tcp;
		mds_mdtm_send = mds_mdtm_send_tcp;
		mds_mdtm_node_subscribe = mds_mdtm_node_subscribe_tcp;
		mds_mdtm_node_unsubscribe = mds_mdtm_node_unsubscribe_tcp;

		/* UNIX is default transport for intranode */
		mds_socket_domain = AF_UNIX;

		return;

#ifdef ENABLE_TIPC_TRANSPORT
	}
#endif
	/* Should never come here */
	abort();
}
