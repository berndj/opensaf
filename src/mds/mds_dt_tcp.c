/*      -*- OpenSAF  -*-
 *
 * (C) Copyright 2010 The OpenSAF Foundation
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. This file and program are licensed
 * under the GNU Lesser General Public License Version 2.1, February 1999.
 * The complete license can be accessed from the following location:
 * http://opensource.org/licenses/lgpl-license.php
 * See the Copying file included with the OpenSAF distribution for full
 * licensing terms.z
 *
 * Author(s): GoAhead Software
 *
 */

#include "mds_dt.h"
#include "mds_log.h"
#include "mds_core.h"
#include "base/ncssysf_def.h"
#include "base/ncssysf_tsk.h"
#include "base/ncssysf_mem.h"
#include "mds_dt_tcp.h"
#include "mds_dt_tcp_disc.h"
#include "mds_dt_tcp_trans.h"

#include <stdlib.h>
#include <sched.h>
#include <sys/poll.h>
#include <poll.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <linux/un.h>
#include <netdb.h>
#include <arpa/inet.h>
#include "osaf/configmake.h"

#ifndef SOCK_CLOEXEC
enum { SOCK_CLOEXEC = 0x80000 };
#define SOCK_CLOEXEC SOCK_CLOEXEC
#endif

#define MDS_MDTM_SUN_PATH 255
#define MDS_MDTM_CONNECT_PATH PKGLOCALSTATEDIR "/osaf_dtm_intra_server"

#ifndef MDS_PORT_NUMBER
#define MDTM_INTRA_SERVER_PORT                                                 \
	7000 /* Fixed port number for intranode communications */
#else
#define MDTM_INTRA_SERVER_PORT MDS_PORT_NUMBER
#endif

/*  mds_indentifire + mds_version +   msg_type + node_id + process_id   */
#define MDS_MDTM_DTM_PID_SIZE 14 /* 4 + 1 + 1 + 4 + 4 */

/* Send_buffer_size + MDS_MDTM_DTM_PID_BUFFER_SIZE   */
#define MDS_MDTM_DTM_PID_BUFFER_SIZE (2 + MDS_MDTM_DTM_PID_SIZE)

/* The default value is set by the rmem_default/wmem_default  */
#define MDS_SOCK_SND_RCV_BUF_SIZE 65536

extern MDS_SUBTN_REF_VAL mdtm_handle;
extern uint32_t mdtm_global_frag_num_tcp;

uint32_t mds_socket_domain = AF_UNIX;

/* Get the pid of the process */
pid_t mdtm_pid;

static void mds_mdtm_enc_init(MDS_MDTM_DTM_MSG *init, uint8_t *buff);
static uint32_t mdtm_create_rcv_task(void);
static uint32_t mdtm_destroy_rcv_task_tcp(void);
uint32_t mdtm_process_recv_events_tcp(void);

/**
 * Initialize the tcp interface creation of sockets
 *
 * @param nodeid, mds_tcp_ref
 *
 * @return NCSCC_RC_SUCCESS
 * @return NCSCC_RC_FAILURE
 *
 */
uint32_t mds_mdtm_init_tcp(NODE_ID nodeid, uint32_t *mds_tcp_ref)
{
	uint32_t sndbuf_size = 0; /* Send buffer size */
	uint32_t rcvbuf_size = 0; /* Receive buffer size */
	socklen_t optlen;	 /* Option length */
	struct sockaddr_un server_addr_un, dhserver_addr_un;
	struct sockaddr_in server_addr_in;
	struct sockaddr_in6 server_addr_in6;
	uint8_t buffer[MDS_MDTM_DTM_PID_BUFFER_SIZE];
	char *ptr;

	mdtm_pid = getpid();

	MDS_MDTM_DTM_MSG send_evt;

	NCS_PATRICIA_PARAMS pat_tree_params;
	mdtm_ref_hdl_list_hdr = NULL;
	mdtm_num_subscriptions = 0;
	mdtm_handle = 0;
	mdtm_global_frag_num_tcp = 0;
	*mds_tcp_ref = 0;

	memset(&server_addr_un, 0, sizeof(struct sockaddr_un));
	memset(&dhserver_addr_un, 0, sizeof(struct sockaddr_un));
	memset(&server_addr_in, 0, sizeof(struct sockaddr_in));
	memset(&server_addr_in6, 0, sizeof(struct sockaddr_in6));
	memset(&send_evt, 0, sizeof(MDS_MDTM_DTM_MSG));
	memset(&buffer, 0, MDS_MDTM_DTM_PID_BUFFER_SIZE);

	tcp_cb = (MDTM_TCP_CB *)malloc(sizeof(MDTM_TCP_CB));
	if (tcp_cb == NULL) {
		syslog(LOG_ERR, "MDTM:TCP InSufficient Memory !!\n");
		return NCSCC_RC_FAILURE;
	}

	memset(tcp_cb, 0, sizeof(MDTM_TCP_CB));

	memset(&pat_tree_params, 0, sizeof(pat_tree_params));
	pat_tree_params.key_size = sizeof(MDTM_REASSEMBLY_KEY);
	if (NCSCC_RC_SUCCESS !=
	    ncs_patricia_tree_init(&mdtm_reassembly_list, &pat_tree_params)) {
		syslog(LOG_ERR,
		       "MDTM:TCP ncs_patricia_tree_init failed MDTM_INIT\n");
		return NCSCC_RC_FAILURE;
	}

	/* Create the sockets required for Binding, Send, receive and Discovery
	 */

	tcp_cb->DBSRsock =
	    socket(mds_socket_domain, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (tcp_cb->DBSRsock < 0) {
		syslog(
		    LOG_ERR,
		    "MDTM:TCP DBSRsock Socket creation failed in MDTM_INIT err :%s",
		    strerror(errno));
		return NCSCC_RC_FAILURE;
	}

	/*  setting MDS_SOCK_SND_RCV_BUF_SIZE  from environment variable if
	   given. The default value is set to MDS_SOCK_SND_RCV_BUF_SIZE
	   (126976). based on application requirements user need to  export
	   MDS_SOCK_SND_RCV_BUF_SIZE varible.

	    If MDS_SOCK_SND_RCV_BUF_SIZE exported to new value
	    it is also mandatory to  change `DTM_SOCK_SND_RCV_BUF_SIZE=` with
	   the same value of for example if we export
	   MDS_SOCK_SND_RCV_BUF_SIZE=126976 DTM_SOCK_SND_RCV_BUF_SIZE=126976
	   also need to be changed in /etc/opensaf/dtm.conf */
	if ((ptr = getenv("MDS_SOCK_SND_RCV_BUF_SIZE")) != NULL) {
		sndbuf_size = rcvbuf_size = atoi(ptr);
		if (sndbuf_size < MDS_SOCK_SND_RCV_BUF_SIZE) {
			sndbuf_size = rcvbuf_size = MDS_SOCK_SND_RCV_BUF_SIZE;
		} else {
			rcvbuf_size = sndbuf_size;
		}
	} else {
		optlen = sizeof(rcvbuf_size);
		getsockopt(tcp_cb->DBSRsock, SOL_SOCKET, SO_RCVBUF,
			   &rcvbuf_size, &optlen);
		if (rcvbuf_size < MDS_SOCK_SND_RCV_BUF_SIZE) {
			rcvbuf_size = MDS_SOCK_SND_RCV_BUF_SIZE;
		} else {
			rcvbuf_size = 0;
		}
		optlen = sizeof(sndbuf_size);
		getsockopt(tcp_cb->DBSRsock, SOL_SOCKET, SO_SNDBUF,
			   &sndbuf_size, &optlen);
		if (sndbuf_size < MDS_SOCK_SND_RCV_BUF_SIZE) {
			sndbuf_size = MDS_SOCK_SND_RCV_BUF_SIZE;
		} else {
			sndbuf_size = 0;
		}
	}

	/* Increase the socket buffer size */
	if ((rcvbuf_size > 0) &&
	    (setsockopt(tcp_cb->DBSRsock, SOL_SOCKET, SO_RCVBUF, &rcvbuf_size,
			sizeof(rcvbuf_size)) != 0)) {
		syslog(
		    LOG_ERR,
		    "MDTM:TCP Unable to set the SO_RCVBUF for DBSRsock  err :%s",
		    strerror(errno));
		close(tcp_cb->DBSRsock);
		return NCSCC_RC_FAILURE;
	}

	if ((sndbuf_size > 0) &&
	    (setsockopt(tcp_cb->DBSRsock, SOL_SOCKET, SO_SNDBUF, &sndbuf_size,
			sizeof(sndbuf_size)) != 0)) {
		syslog(
		    LOG_ERR,
		    "MDTM:TCP Unable to set the SO_SNDBUF for DBSRsock  err :%s",
		    strerror(errno));
		close(tcp_cb->DBSRsock);
		return NCSCC_RC_FAILURE;
	}

	tcp_cb->adest = ((uint64_t)(nodeid)) << 32;
	tcp_cb->adest |= mdtm_pid;
	tcp_cb->node_id = nodeid;

	*mds_tcp_ref = mdtm_pid;

	get_adest_details(tcp_cb->adest, tcp_cb->adest_details);

	if (mds_socket_domain == AF_UNIX) {
		int servlen = 0;
		server_addr_un.sun_family = AF_UNIX;
		strcpy(server_addr_un.sun_path, MDS_MDTM_CONNECT_PATH);

		servlen = strlen(server_addr_un.sun_path) +
			  sizeof(server_addr_un.sun_family);
		/* Blocking Connect */
		if (connect(tcp_cb->DBSRsock,
			    (struct sockaddr *)&server_addr_un,
			    servlen) == -1) {
			syslog(LOG_ERR,
			       "MDTM:TCP DBSRsock unable to connect err :%s",
			       strerror(errno));
			close(tcp_cb->DBSRsock);
			return NCSCC_RC_FAILURE;
		}
	} else {
		/* Case for AF_INET */
		if (AF_INET == mds_socket_domain) {
			server_addr_in.sin_family = AF_INET;
			server_addr_in.sin_port = htons(MDTM_INTRA_SERVER_PORT);
			server_addr_in.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			bzero(&(server_addr_in.sin_zero), 8);

			/* Blocking Connect */
			if (connect(tcp_cb->DBSRsock,
				    (struct sockaddr *)&server_addr_in,
				    sizeof(struct sockaddr)) == -1) {
				syslog(
				    LOG_ERR,
				    "MDTM:TCP  DBSRsock unable to connect err :%s",
				    strerror(errno));
				close(tcp_cb->DBSRsock);
				return NCSCC_RC_FAILURE;
			}
		} else {
			server_addr_in6.sin6_family = AF_INET6;
			server_addr_in6.sin6_port =
			    htons(MDTM_INTRA_SERVER_PORT);
			inet_pton(AF_INET6, "localhost",
				  &server_addr_in6.sin6_addr);

			/* Blocking Connect */
			if (connect(tcp_cb->DBSRsock,
				    (struct sockaddr *)&server_addr_in,
				    sizeof(struct sockaddr)) == -1) {
				syslog(
				    LOG_ERR,
				    "MDTM:TCP  DBSRsock unable to connect err :%s",
				    strerror(errno));
				close(tcp_cb->DBSRsock);
				return NCSCC_RC_FAILURE;
			}
		}
	}

	/* Send the Message to DH Server "Packet-type Node-id Pid" total of 9
	 * bytes */
	send_evt.size = MDS_MDTM_DTM_PID_SIZE;
	send_evt.mds_version = MDS_SND_VERSION;
	send_evt.mds_indentifire = MDS_IDENTIFIRE;
	send_evt.type = MDS_MDTM_DTM_PID_TYPE;
	send_evt.info.pid.node_id = nodeid;
	send_evt.info.pid.process_id = mdtm_pid;

	/* Convert into the encoded buffer before send */
	mds_mdtm_enc_init(&send_evt, buffer);

	/* This is still a blocking send */
	if (send(tcp_cb->DBSRsock, buffer, MDS_MDTM_DTM_PID_BUFFER_SIZE, 0) !=
	    MDS_MDTM_DTM_PID_BUFFER_SIZE) {
		syslog(LOG_ERR, "MDTM:TCP Send on DBSRsock failed");
		close(tcp_cb->DBSRsock);
		return NCSCC_RC_FAILURE;
	}

	/* Code for Tmr Mailbox Creation used for Tmr Msg Retrival */

	if (m_NCS_IPC_CREATE(&tcp_cb->tmr_mbx) != NCSCC_RC_SUCCESS) {
		/* Mail box creation failed */
		syslog(LOG_ERR, "MDTM:TCP Tmr Mailbox Creation failed:\n");
		close(tcp_cb->DBSRsock);
		return NCSCC_RC_FAILURE;
	} else {

		NCS_SEL_OBJ obj;

		if (NCSCC_RC_SUCCESS != m_NCS_IPC_ATTACH(&tcp_cb->tmr_mbx)) {
			m_NCS_IPC_RELEASE(&tcp_cb->tmr_mbx, NULL);
			syslog(LOG_ERR,
			       "MDTM:TCP Tmr Mailbox  Attach failed:\n");
			close(tcp_cb->DBSRsock);
			return NCSCC_RC_FAILURE;
		}

		obj = m_NCS_IPC_GET_SEL_OBJ(&tcp_cb->tmr_mbx);

		/* retreive the corresponding fd for mailbox and fill it in cb
		 */
		tcp_cb->tmr_fd = m_GET_FD_FROM_SEL_OBJ(
		    obj); /* extract and fill value needs to be extracted */
		mdtm_attach_mbx(tcp_cb->tmr_mbx);
		mdtm_set_transport(MDTM_TX_TYPE_TCP);
	}

	/* Create a task to receive the events and data */
	if (mdtm_create_rcv_task() != NCSCC_RC_SUCCESS) {
		syslog(LOG_ERR,
		       "MDTM:TCP Receive Task Creation Failed in MDTM_INIT\n");
		close(tcp_cb->DBSRsock);
		m_NCS_IPC_RELEASE(&tcp_cb->tmr_mbx, NULL);
		return NCSCC_RC_FAILURE;
	}

	return NCSCC_RC_SUCCESS;
}

/**
 * Start the rcv thread
 *
 * @return NCSCC_RC_SUCCESS
 * @return NCSCC_RC_FAILURE
 *
 */
static uint32_t mdtm_create_rcv_task(void)
{
	/*
	   STEP 1: Create a recv task which will recv data and
	   captures the discovery events as well */

	int policy = SCHED_RR; /*root defaults */
	int max_prio = sched_get_priority_max(policy);
	int min_prio = sched_get_priority_min(policy);
	int prio_val = ((max_prio - min_prio) * 0.87);

	if (m_NCS_TASK_CREATE((NCS_OS_CB)mdtm_process_recv_events_tcp,
			      (NCSCONTEXT)NULL, (char *)"OSAF_MDS", prio_val,
			      policy, NCS_MDTM_STACKSIZE,
			      &tcp_cb->mdtm_hdle_task) != NCSCC_RC_SUCCESS) {
		m_MDS_LOG_ERR("MDTM: Task Creation-failed:\n");
		return NCSCC_RC_FAILURE;
	}

	/* Start the created task,
	 *   if start fails,
	 *        release the task by calling the NCS task release function*/
	if (m_NCS_TASK_START(tcp_cb->mdtm_hdle_task) != NCSCC_RC_SUCCESS) {
		m_MDS_LOG_ERR("MDTM: Start of the Created Task-failed:\n");
		m_NCS_TASK_RELEASE(tcp_cb->mdtm_hdle_task);
		m_MDS_LOG_ERR("MDTM: START of created task failed");
		return NCSCC_RC_FAILURE;
	}

	/* return NCS success */
	return NCSCC_RC_SUCCESS;
}

/**
 * Destroy the tcp interface close the sockets
 *
 *
 * @return NCSCC_RC_SUCCESS
 * @return NCSCC_RC_FAILURE
 *
 */
uint32_t mds_mdtm_destroy_tcp(void)
{
	MDTM_REF_HDL_LIST *temp_ref_hdl_list_entry = NULL;
	MDTM_REASSEMBLY_QUEUE *reassem_queue = NULL;
	MDTM_REASSEMBLY_KEY reassembly_key;

	/* close sockets first */
	close(tcp_cb->DBSRsock);

	/* Destroy receiving task */
	if (mdtm_destroy_rcv_task_tcp() != NCSCC_RC_SUCCESS) {
		m_MDS_LOG_ERR(
		    "MDTM: Receive Task Destruction Failed in MDTM_INIT\n");
	}
	/* Destroy mailbox */
	m_NCS_IPC_DETACH(&tcp_cb->tmr_mbx, (NCS_IPC_CB)mdtm_mailbox_mbx_cleanup,
			 NULL);
	m_NCS_IPC_RELEASE(&tcp_cb->tmr_mbx,
			  (NCS_IPC_CB)mdtm_mailbox_mbx_cleanup);

	/* Clear reference hdl list */
	while (mdtm_ref_hdl_list_hdr != NULL) {
		/* Store temporary the pointer of entry to be deleted */
		temp_ref_hdl_list_entry = mdtm_ref_hdl_list_hdr;
		/* point to next entry */
		mdtm_ref_hdl_list_hdr = mdtm_ref_hdl_list_hdr->next;
		/* Free the entry */
		m_MMGR_FREE_HDL_LIST(temp_ref_hdl_list_entry);
	}

	/* Delete the pat tree for the reassembly */
	reassem_queue = (MDTM_REASSEMBLY_QUEUE *)ncs_patricia_tree_getnext(
	    &mdtm_reassembly_list, (uint8_t *)NULL);

	while (NULL != reassem_queue) {
		/* stop timer and free memory */
		m_NCS_TMR_STOP(reassem_queue->tmr);
		m_NCS_TMR_DESTROY(reassem_queue->tmr);

		/* Free tmr_info */
		m_MMGR_FREE_TMR_INFO(reassem_queue->tmr_info);

		/* Destroy Handle */
		ncshm_destroy_hdl(NCS_SERVICE_ID_COMMON,
				  reassem_queue->tmr_hdl);

		reassem_queue->tmr_info = NULL;

		/* Free memory Allocated to this msg and MDTM_REASSEMBLY_QUEUE
		 */
		mdtm_free_reassem_msg_mem(&reassem_queue->recv.msg);

		reassembly_key = reassem_queue->key;

		ncs_patricia_tree_del(&mdtm_reassembly_list,
				      (NCS_PATRICIA_NODE *)reassem_queue);
		m_MMGR_FREE_REASSEM_QUEUE(reassem_queue);

		reassem_queue =
		    (MDTM_REASSEMBLY_QUEUE *)ncs_patricia_tree_getnext(
			&mdtm_reassembly_list, (uint8_t *)&reassembly_key);
	}

	ncs_patricia_tree_destroy(&mdtm_reassembly_list);
	mdtm_ref_hdl_list_hdr = NULL;
	mdtm_num_subscriptions = 0;
	mdtm_handle = 0;
	mdtm_global_frag_num_tcp = 0;
	free(tcp_cb);

	return NCSCC_RC_SUCCESS;
}

uint32_t mdtm_destroy_rcv_task_tcp(void)
{
	if (m_NCS_TASK_STOP(tcp_cb->mdtm_hdle_task) != NCSCC_RC_SUCCESS) {
		m_MDS_LOG_ERR("MDTM: Stop of the Created Task-failed:\n");
	}

	if (m_NCS_TASK_RELEASE(tcp_cb->mdtm_hdle_task) != NCSCC_RC_SUCCESS) {
		m_MDS_LOG_ERR("MDTM: Stop of the Created Task-failed:\n");
	}

	return NCSCC_RC_SUCCESS;
}

/**
 * Encode the send message
 *
 * @param Send buffer, Data packet
 *
 */
static void mds_mdtm_enc_init(MDS_MDTM_DTM_MSG *init, uint8_t *data)
{
	uint8_t *buff = data;
	ncs_encode_16bit(&buff, init->size);
	ncs_encode_32bit(&buff, init->mds_indentifire);
	ncs_encode_8bit(&buff, init->mds_version);
	ncs_encode_8bit(&buff, init->type);
	ncs_encode_32bit(&buff, init->info.pid.node_id);
	ncs_encode_32bit(&buff, init->info.pid.process_id);
}
