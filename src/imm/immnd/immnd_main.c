/*      -*- OpenSAF  -*-
 *
 * (C) Copyright 2008-2010 The OpenSAF Foundation
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
 *            Wind River Systems
 *
 */

/*****************************************************************************
  FILE NAME: immnd_main.c

  DESCRIPTION: APIs used to initialize & Destroy the IMMND Service Part.

******************************************************************************/

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>
#include <poll.h>

#include "osaf/configmake.h"
#include "rde/agent/rda_papi.h"
#include "base/daemon.h"
#include "nid/agent/nid_api.h"
#include "mds/mds_dl_api.h"

#include "immnd.h"

#define FD_TERM 0
#define FD_AMF 1
#define FD_MBX 2
#define FD_CLM_INIT 3
#define FD_CLM 4

static IMMND_CB _immnd_cb;
IMMND_CB *immnd_cb = &_immnd_cb;

/* Static Function Declerations */

/**
 * USR1 signal is used when AMF wants instantiate us as a
 * component. Wake up the main thread so it can register with
 * AMF.
 *
 * @param i_sig_num
 */
static void sigusr1_handler(int sig)
{
	(void)sig;
	signal(SIGUSR1, SIG_IGN);
	ncs_sel_obj_ind(&immnd_cb->usr1_sel_obj);
	/* Do not use trace in the signal handler
	   It can apparently cause the main thread
	   to spin or deadlock. See ticket #1173.
	 */
}

/****************************************************************************
 * Name          : immnd_cb_db_init
 *
 * Description   : This is the function which initializes all the data
 *                 structures and locks used belongs to IMMND.
 *
 * Arguments     : cb  - IMMND control block pointer.
 *
 * Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE.
 *
 * Notes         : None.
 *****************************************************************************/
static uint32_t immnd_cb_db_init(IMMND_CB *cb)
{
	uint32_t rc = immnd_client_node_tree_init(cb);

	if (rc == NCSCC_RC_FAILURE)
		LOG_ER("client node tree init failed");

	rc = immnd_clm_node_list_init(cb);
	return (rc);
}

/****************************************************************************
 * Name          : initialize_immnd
 *
 * Description   : This is the function which initalize the IMMND libarary.
 *                 This function creates an IPC mail Box and spawns IMMND
 *                 thread.
 *                 This function initializes the CB, MDS, IMMD connection
 *                 and Registers with AMF with respect to the component Type
 *                 (IMMND).
 *
 *
 * Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE.
 *
 * Notes         : None.
 *****************************************************************************/
static uint32_t immnd_initialize(char *progname)
{
	uint32_t rc = NCSCC_RC_FAILURE;
	char *envVar = NULL;

	TRACE_ENTER();

	/* Determine how this process was started, by NID or AMF */
	if (getenv("SA_AMF_COMPONENT_NAME") == NULL)
		immnd_cb->nid_started = 1;

	/* After restart it is important to do this before MDS gets initialized
	 * to avoid a timing problem in imma. It waits for MDS IMMND UP and then
	 * registers/connects with the server. The server should do the reverse.
	 */
	const char *name = PKGLOCALSTATEDIR "/immnd.sock";
	if (mds_auth_server_create(name) != NCSCC_RC_SUCCESS) {
		LOG_ER("mds_auth_server_create FAILED");
		goto done;
	}

	if (ncs_agents_startup() != NCSCC_RC_SUCCESS) {
		LOG_ER("ncs_agents_startup FAILED");
		goto done;
	}

	/* unset so that forked processes (e.g. loader) does not create MDS
	 * server */
	unsetenv("MDS_SOCK_SERVER_CREATE");

	/* Initialize immnd control block */
	immnd_cb->ha_state = SA_AMF_HA_ACTIVE;
	immnd_cb->cli_id_gen = 1;
	m_NCS_EDU_HDL_INIT(&immnd_cb->immnd_edu_hdl);
	immnd_cb->mState = IMM_SERVER_ANONYMOUS;
	immnd_cb->loaderPid = (-1);
	immnd_cb->syncPid = (-1);
	immnd_cb->mMyPid = getpid();
	immnd_cb->mProgName = progname;
	immnd_cb->mDir = getenv("IMMSV_ROOT_DIRECTORY");
	immnd_cb->mFile = getenv("IMMSV_LOAD_FILE");
	immnd_cb->clm_hdl = 0;
	immnd_cb->clmSelectionObject = -1;
	/* isClmNodeJoined will be intially set to true, untill CLMS service is
	   up. from there isClmNodeJoined will be controlled by CLM membership
	   join/left.
	*/
	immnd_cb->isClmNodeJoined = true;
	if ((envVar = getenv("IMMSV_NUM_NODES"))) {
		int numNodes = atoi(envVar);
		if (numNodes > 255) {
			LOG_WA("IMMSV_NUM_NODES set to %u, must be "
			       "less than 256. Setting to 255",
			       numNodes);
			numNodes = 255;
		}
		immnd_cb->mExpectedNodes = (uint8_t)numNodes;
	}
	if ((envVar = getenv("IMMSV_MAX_WAIT"))) {
		int waitSecs = atoi(envVar);
		if (waitSecs > 255) {
			LOG_WA("IMMSV_MAX_WAIT set to %u, must be "
			       "less than 256. Setting to 255",
			       waitSecs);
			waitSecs = 255;
		}
		immnd_cb->mWaitSecs = waitSecs;
	}

	if ((immnd_cb->mPbeFile = getenv("IMMSV_PBE_FILE")) != NULL) {
		LOG_NO(
		    "Persistent Back-End capability configured, Pbe file:%s (suffix may get added)",
		    immnd_cb->mPbeFile);
	}

	FILE *fp;
	char node_type[20];
	fp = fopen(PKGSYSCONFDIR "/node_type", "r");
	if (fp == NULL) {
		LOG_ER("Could not open file %s - %s",
		       PKGSYSCONFDIR "/node_type", strerror(errno));
		goto done;
	}
	if (EOF == fscanf(fp, "%15s", node_type)) {
		LOG_ER("Could not read node type - %s", strerror(errno));
		fclose(fp);
		goto done;
	}
	if (strcmp(node_type, "controller") == 0) {
		immnd_cb->isNodeTypeController = true;
	} else if (strcmp(node_type, "payload") == 0) {
		immnd_cb->isNodeTypeController = false;
	} else {
		LOG_ER("Wrong node type is specified for the node");
		fclose(fp);
		goto done;
	}
	fclose(fp);

	immnd_cb->mRim = SA_IMM_INIT_FROM_FILE;
	immnd_cb->mPbeVeteran = false;
	immnd_cb->mPbeVeteranB = false;

	if (immnd_cb->mDir == NULL || (strlen(immnd_cb->mDir) == 0)) {
		LOG_ER("Env var IMMSV_ROOT_DIRECTORY missing");
		goto done;
	}

	if (immnd_cb->mFile == NULL || (strlen(immnd_cb->mFile) == 0)) {
		LOG_ER("Env var IMMSV_LOAD_FILE missing");
		goto done;
	}

	TRACE_2("Dir:%s File:%s ExpectedNodes:%u WaitSecs:%u", immnd_cb->mDir,
		immnd_cb->mFile, immnd_cb->mExpectedNodes, immnd_cb->mWaitSecs);

	if ((rc = immnd_cb_db_init(immnd_cb)) == NCSCC_RC_FAILURE) {
		TRACE("immnd_cb_db_init Failed");
		goto done;
	}

	/* Create a mail box */
	if ((rc = m_NCS_IPC_CREATE(&immnd_cb->immnd_mbx)) != NCSCC_RC_SUCCESS) {
		LOG_ER("m_NCS_IPC_CREATE FAILED");
		goto done;
	}

	/* Attach the IPC to mail box */
	if ((rc = m_NCS_IPC_ATTACH(&immnd_cb->immnd_mbx)) != NCSCC_RC_SUCCESS) {
		LOG_ER("m_NCS_IPC_ATTACH FAILED");
		goto done;
	}

	/* Create a selection object for clm intialization*/
	if ((rc = ncs_sel_obj_create(&immnd_cb->clm_init_sel_obj)) !=
	    NCSCC_RC_SUCCESS) {
		LOG_ER("ncs_sel_obj_create failed for clm intialization");
		goto done;
	}

	if ((rc = immnd_mds_register(immnd_cb)) != NCSCC_RC_SUCCESS) {
		TRACE("immnd_mds_register FAILED %u", rc);
		goto done;
	}

	/* Create a selection object */
	if ((rc = ncs_sel_obj_create(&immnd_cb->usr1_sel_obj)) !=
	    NCSCC_RC_SUCCESS) {
		LOG_ER("ncs_sel_obj_create failed");
		goto done;
	}

	if (immnd_cb->nid_started &&
	    signal(SIGUSR1, sigusr1_handler) == SIG_ERR) {
		LOG_ER("signal USR1 failed: %s", strerror(errno));
		rc = NCSCC_RC_FAILURE;
		goto done;
	}

	/* If AMF started register immidiately */
	if (!immnd_cb->nid_started) {
		if ((rc = immnd_amf_init(immnd_cb)) != NCSCC_RC_SUCCESS)
			goto done;
	}

	rc = NCSCC_RC_SUCCESS;
	syslog(LOG_INFO, "Initialization Success");

done:
	if (immnd_cb->nid_started && (rc == NCSCC_RC_FAILURE)) {
		if (nid_notify("IMMND", NCSCC_RC_FAILURE, NULL) !=
		    NCSCC_RC_SUCCESS)
			LOG_ER("nid_notify failed");
	}

	TRACE_LEAVE();
	return (rc);
}

/**
 * The main routine for the immnd daemon.
 * @param argc
 * @param argv
 *
 * @return int
 */
int main(int argc, char *argv[])
{
	NCS_SEL_OBJ mbx_fd;
	SaAisErrorT error;
	uint32_t timeout = 100; /* ABT For server maintenance.
				   Using a period of 0.1s during startup. */
	int eventCount = 0;     /* Used to regulate progress of background
				   server task when we are very bussy. */
	int maxEvt = 100;
	struct timespec start_time;
	struct pollfd fds[5];
	int term_fd, nfds = 5;

	daemonize(argc, argv);

	if (setenv("SA_ENABLE_EXTENDED_NAMES", "1", 1)) {
		LOG_ER("failed to set SA_ENABLE_EXTENDED_NAMES");
		goto done;
	}

	if (immnd_initialize(argv[0]) != NCSCC_RC_SUCCESS) {
		LOG_ER("initialize_immd failed");
		goto done;
	}

	daemon_sigterm_install(&term_fd);

	/* Get file descriptor for mailbox */
	mbx_fd = m_NCS_IPC_GET_SEL_OBJ(&immnd_cb->immnd_mbx);

	/* Set up all file descriptors to listen to */
	fds[FD_TERM].fd = term_fd;
	fds[FD_TERM].events = POLLIN;

	if (immnd_cb->nid_started)
		fds[FD_AMF].fd = immnd_cb->usr1_sel_obj.rmv_obj;
	else
		fds[FD_AMF].fd = (int)immnd_cb->amf_sel_obj;

	fds[FD_AMF].events = POLLIN;
	fds[FD_MBX].fd = mbx_fd.rmv_obj;
	fds[FD_MBX].events = POLLIN;
	fds[FD_CLM_INIT].fd = immnd_cb->clm_init_sel_obj.rmv_obj;
	fds[FD_CLM_INIT].events = POLLIN;

	while (1) {
		/* Watch out for performance bug. Possibly change from
		   event-count to recalculated timer. */
		/* ABT 13/07 2009 actually using both event-count and
		 * recalculated timer now. */

		/* calculate new elapsed time */
		struct timespec now;
		osaf_clock_gettime(CLOCK_MONOTONIC, &now);
		struct timespec passed_time;
		osaf_timespec_subtract(&now, &start_time, &passed_time);
		uint64_t passed_time_ms = osaf_timespec_to_millis(&passed_time);
		fds[FD_CLM].fd = immnd_cb->clmSelectionObject;
		fds[FD_CLM].events = POLLIN;

		maxEvt = (timeout == 100) ? 50 : 100;

		/* Wait for events */
		int ret =
		    poll(fds, nfds,
			 (passed_time_ms < timeout) ? (timeout - passed_time_ms)
						    : 0);

		if (ret == -1) {
			if (errno == EINTR)
				continue;

			LOG_ER("poll failed - %s", strerror(errno));
			break;
		}

		if (ret > 0) {
			++eventCount;

			if (fds[FD_TERM].revents & POLLIN) {
				daemon_exit();
			}

			if (fds[FD_AMF].revents & POLLIN) {
				if (immnd_cb->amf_hdl != 0) {
					error = saAmfDispatch(immnd_cb->amf_hdl,
							      SA_DISPATCH_ALL);
					if (error != SA_AIS_OK) {
						LOG_ER(
						    "saAmfDispatch failed: %u",
						    error);
						break;
					}
				} else {
					TRACE("SIGUSR1 event rec");
					ncs_sel_obj_rmv_ind(
					    &immnd_cb->usr1_sel_obj, true,
					    true);
					ncs_sel_obj_destroy(
					    &immnd_cb->usr1_sel_obj);

					if (immnd_amf_init(immnd_cb) !=
					    NCSCC_RC_SUCCESS)
						break;

					TRACE(
					    "AMF Initialization SUCCESS......");
					fds[FD_AMF].fd =
					    (int)immnd_cb->amf_sel_obj;
				}
			}

			if (fds[FD_MBX].revents & POLLIN) {
				uint8_t wasCoord = immnd_cb->mIsCoord;
				immnd_process_evt();
				if ((!wasCoord && immnd_cb->mIsCoord) ||
				    immnd_cb->mForceClean) {
					TRACE(
					    "Just became Coord or special imm admop => Force a server job!");
					/* This is particularly urgent in a
					 * failover situation. */
					eventCount = maxEvt;
					if (immnd_cb->mForceClean) {
						LOG_IN(
						    "ABT immnd-main caught mForceClean");
					}
				}
			}

			if (fds[FD_CLM_INIT].revents & POLLIN) {
				ncs_sel_obj_rmv_ind(&immnd_cb->clm_init_sel_obj,
						    true, true);
				if(!immnd_cb->clm_hdl) {
					TRACE("Initalize CLM ");
					immnd_init_with_clm();
				}
			}

			if (fds[FD_CLM].revents & POLLIN) {
				if ((error = saClmDispatch(immnd_cb->clm_hdl,
							   SA_DISPATCH_ALL)) !=
				    SA_AIS_OK) {
					if (error == SA_AIS_ERR_BAD_HANDLE) {
						LOG_WA(
						    "saClmDispatch failed: %u",
						    error);
						LOG_NO(
						    "Re-initializing with CLMS");
						saClmFinalize(
						    immnd_cb->clm_hdl);
						immnd_clm_node_cleanup(
						    immnd_cb);
						immnd_cb->clm_hdl = 0;
						immnd_cb->clmSelectionObject =
						    -1;
						immnd_init_with_clm();
					} else {
						LOG_ER(
						    "saClmDispatch failed: %u",
						    error);
						break;
					}
				}
			}

			if (eventCount >= maxEvt) {
				/* Make some progress on background task,
				   even when we are very busy. */
				TRACE_2(
				    "FORCE a proc_server job after %u events",
				    eventCount);
				uint32_t rc = immnd_proc_server(&timeout);
				if (rc != NCSCC_RC_SUCCESS) {
					LOG_ER(
					    "IMMND - Periodic server job failed");
					break;
				}
				eventCount = 0;
				osaf_clock_gettime(CLOCK_MONOTONIC,
						   &start_time);
			}
		} else {
			/* Timeout */
			/*TRACE_2("PERIODIC proc_server eventcout: %u",
			 * eventCount); */
			uint32_t rc = immnd_proc_server(&timeout);
			if (rc != NCSCC_RC_SUCCESS) {
				LOG_ER("IMMND - Periodic server job failed");
				break;
			}
			eventCount = 0;
			osaf_clock_gettime(CLOCK_MONOTONIC, &start_time);
		}
	}

done:
	LOG_ER("Failed, exiting...");
	TRACE_LEAVE();
	return -1;
}
