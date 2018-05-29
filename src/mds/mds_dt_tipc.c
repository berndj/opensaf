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

..............................................................................

  DESCRIPTION:

******************************************************************************
*/
#include "mds_dt.h"
#include "mds_log.h"
#include "mds_core.h"
#include "base/ncssysf_def.h"
#include "base/ncssysf_tsk.h"
#include "base/ncssysf_mem.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sched.h>
#include <sys/poll.h>
#include <poll.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

#include <signal.h>
#include <sys/timerfd.h>

#include "mds_dt_tipc.h"
#include "mds_dt_tcp_disc.h"
#include "mds_core.h"
#include "base/osaf_utility.h"
#include "base/osaf_poll.h"

#ifndef SOCK_CLOEXEC
enum { SOCK_CLOEXEC = 0x80000 };
#define SOCK_CLOEXEC SOCK_CLOEXEC
#endif

/*
    tipc_id will be <NODE_ID,RANDOM NUMBER>
*/

/* Added for the subscription changes */

/*
 * In the default addressing scheme TIPC addresses will be 1.1.31, 1.1.47.
 * The slot ID is shifted 4 bits up and subslot ID is added in the 4 LSB.
 * When use of subslot ID is disabled (set MDS_USE_SUBSLOT_ID=0 in CFLAGS), the
 * TIPC addresses will be 1.1.1, 1.1.2, etc.
 */

/* Following is defined for use by MDS in TIPC 2.0 as TIPC 2.0 supports only
 * network order */
static bool mds_use_network_order = false;

#define NTOHL(x) (mds_use_network_order ? ntohl(x) : x)
#define HTONL(x) (mds_use_network_order ? htonl(x) : x)

extern bool tipc_mcast_enabled;
static uint32_t mdtm_tipc_check_for_endianness(void);

uint32_t mdtm_tipc_init(NODE_ID node_id, uint32_t *mds_tipc_ref);
uint32_t mdtm_tipc_destroy(void);

static uint32_t mdtm_create_rcv_task(int mdtm_hdle);
static uint32_t mdtm_destroy_rcv_task(void);

static uint32_t mdtm_process_recv_events(void);
static uint32_t mdtm_process_discovery_events(uint32_t flag,
					      struct tipc_event event);

uint32_t mds_mdtm_svc_install_tipc(PW_ENV_ID pwe_id, MDS_SVC_ID svc_id,
				   NCSMDS_SCOPE_TYPE install_scope,
				   V_DEST_RL role, MDS_VDEST_ID vdest_id,
				   NCS_VDEST_TYPE policy,
				   MDS_SVC_PVT_SUB_PART_VER mds_svc_pvt_ver);
uint32_t mds_mdtm_svc_uninstall_tipc(PW_ENV_ID pwe_id, MDS_SVC_ID svc_id,
				     NCSMDS_SCOPE_TYPE install_scope,
				     V_DEST_RL role, MDS_VDEST_ID vdest_id,
				     NCS_VDEST_TYPE policy,
				     MDS_SVC_PVT_SUB_PART_VER mds_svc_pvt_ver);
uint32_t mds_mdtm_svc_subscribe_tipc(PW_ENV_ID pwe_id, MDS_SVC_ID svc_id,
				     NCSMDS_SCOPE_TYPE install_scope,
				     MDS_SVC_HDL svc_hdl,
				     MDS_SUBTN_REF_VAL *subtn_ref_val);
uint32_t mds_mdtm_svc_unsubscribe_tipc(PW_ENV_ID pwe_id, MDS_SVC_ID svc_id,
				       NCSMDS_SCOPE_TYPE install_scope,
				       MDS_SUBTN_REF_VAL subtn_ref_val);
uint32_t mds_mdtm_vdest_install_tipc(MDS_VDEST_ID vdest_id);
uint32_t mds_mdtm_vdest_uninstall_tipc(MDS_VDEST_ID vdest_id);
uint32_t mds_mdtm_vdest_subscribe_tipc(MDS_VDEST_ID vdest_id,
				       MDS_SUBTN_REF_VAL *subtn_ref_val);
uint32_t mds_mdtm_vdest_unsubscribe_tipc(MDS_VDEST_ID vdest_id,
					 MDS_SUBTN_REF_VAL subtn_ref_val);
uint32_t mds_mdtm_tx_hdl_register_tipc(MDS_DEST adest);
uint32_t mds_mdtm_tx_hdl_unregister_tipc(MDS_DEST adest);

uint32_t mds_mdtm_send_tipc(MDTM_SEND_REQ *req);

/* Tipc actual send, can be made as Macro even*/
static uint32_t mdtm_sendto(uint8_t *buffer, uint16_t buff_len,
			    struct tipc_portid tipc_id);
static uint32_t mdtm_mcast_sendto(void *buffer, size_t size,
				  const MDTM_SEND_REQ *req);

uint32_t mdtm_frag_and_send(MDTM_SEND_REQ *req, uint32_t seq_num,
			    struct tipc_portid id, int frag_size);

static uint32_t mdtm_add_mds_hdr(uint8_t *buffer, MDTM_SEND_REQ *req);

uint16_t mds_checksum(uint32_t length, uint8_t buff[]);

uint32_t mds_mdtm_node_subscribe_tipc(MDS_SVC_HDL svc_hdl,
				      MDS_SUBTN_REF_VAL *subtn_ref_val);
uint32_t mds_mdtm_node_unsubscribe_tipc(MDS_SUBTN_REF_VAL subtn_ref_val);

typedef struct mdtm_tipc_cb {
	int Dsock;
	int BSRsock;

	void *mdtm_hdle_task;
	int hdle_mdtm;
	uint64_t adest;
	char adest_details[MDS_MAX_PROCESS_NAME_LEN];

	SYSF_MBX tmr_mbx;
	int tmr_fd;
	uint32_t node_id;
	uint8_t *recvbuf; /* receive buffer for receive thread */
} MDTM_TIPC_CB;

MDTM_TIPC_CB tipc_cb;

static uint32_t mdtm_tipc_own_node(int fd);

struct sockaddr_tipc topsrv;

#define FOREVER ~0

static MDS_SUBTN_REF_VAL handle;
static uint16_t num_subscriptions;

uint32_t mdtm_global_frag_num;

const unsigned int MAX_RECV_THRESHOLD = 30;

static bool get_tipc_port_id(int sock, uint32_t* port_id) {
	struct sockaddr_tipc addr;
	socklen_t sz = sizeof(addr);

	memset(&addr, 0, sizeof(addr));
	*port_id = 0;
	if (0 > getsockname(sock, (struct sockaddr *)&addr, &sz)) {
		syslog(LOG_ERR, "MDTM:TIPC Failed to get socket name, err: %s",
		       strerror(errno));
		return false;
	}

	*port_id = addr.addr.id.ref;
	return true;
}

/*********************************************************

  Function NAME: mdtm_tipc_init

  DESCRIPTION:

  ARGUMENTS:

  RETURNS:  1 - NCSCC_RC_SUCCESS
	    2 - NCSCC_RC_FAILURE

*********************************************************/

uint32_t mdtm_tipc_init(NODE_ID nodeid, uint32_t *mds_tipc_ref)
{
	uint32_t tipc_node_id = 0;

	NCS_PATRICIA_PARAMS pat_tree_params;

	memset(&tipc_cb, 0, sizeof(tipc_cb));

	/* Added to assist the shutdown bug */
	mdtm_ref_hdl_list_hdr = NULL;
	num_subscriptions = 0;
	handle = 0;
	mdtm_global_frag_num = 0;

	/* REASSEMBLY TREE */
	memset(&pat_tree_params, 0, sizeof(pat_tree_params));
	pat_tree_params.key_size = sizeof(MDTM_REASSEMBLY_KEY);
	if (NCSCC_RC_SUCCESS !=
	    ncs_patricia_tree_init(&mdtm_reassembly_list, &pat_tree_params)) {
		syslog(LOG_ERR,
		       "MDTM:TIPC ncs_patricia_tree_init failed MDTM_INIT\n");
		return NCSCC_RC_FAILURE;
	}

	/* Create the sockets required for Binding, Send, receive and Discovery
	 */

	tipc_cb.Dsock = socket(AF_TIPC, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
	if (tipc_cb.Dsock < 0) {
		syslog(
		    LOG_ERR,
		    "MDTM:TIPC Dsock Socket creation failed in MDTM_INIT err :%s",
		    strerror(errno));
		return NCSCC_RC_FAILURE;
	}
	tipc_cb.BSRsock = socket(AF_TIPC, SOCK_RDM | SOCK_CLOEXEC, 0);
	if (tipc_cb.BSRsock < 0) {
		syslog(
		    LOG_ERR,
		    "MDTM:TIPC BSRsock Socket creation failed in MDTM_INIT err :%s",
		    strerror(errno));
		return NCSCC_RC_FAILURE;
	}

	/* Code for getting the self tipc random number */
	if (!get_tipc_port_id(tipc_cb.BSRsock, mds_tipc_ref)) {
		close(tipc_cb.Dsock);
		close(tipc_cb.BSRsock);
		return NCSCC_RC_FAILURE;
	}

	tipc_cb.adest = ((uint64_t)(nodeid)) << 32;
	tipc_cb.adest |= *mds_tipc_ref;
	tipc_cb.node_id = nodeid;
	get_adest_details(tipc_cb.adest, tipc_cb.adest_details);

	tipc_node_id = mdtm_tipc_own_node(
	    tipc_cb.BSRsock); /* This gets the tipc ownaddress */

	if (tipc_node_id == 0) {
		syslog(LOG_ERR, "MDTM:TIPC Zero tipc_node_id ");
		close(tipc_cb.Dsock);
		close(tipc_cb.BSRsock);
		return NCSCC_RC_FAILURE;
	}

	switch (mdtm_tipc_check_for_endianness()) {
	case 0:
		mds_use_network_order = false;
		break;
	case 1:
		mds_use_network_order = true;
		break;
	default:
		close(tipc_cb.Dsock);
		close(tipc_cb.BSRsock);
		return NCSCC_RC_FAILURE;
	}

	/* Connect to the Topology Server */
	memset(&topsrv, 0, sizeof(topsrv));
	topsrv.family = AF_TIPC;
	topsrv.addrtype = TIPC_ADDR_NAME;
	topsrv.addr.name.name.type = TIPC_TOP_SRV;
	topsrv.addr.name.name.instance = TIPC_TOP_SRV;

	if (0 > connect(tipc_cb.Dsock, (struct sockaddr *)&topsrv,
			sizeof(topsrv))) {
		syslog(
		    LOG_ERR,
		    "MDTM:TIPC Failed to connect to topology server  err :%s",
		    strerror(errno));
		close(tipc_cb.Dsock);
		close(tipc_cb.BSRsock);
		return NCSCC_RC_FAILURE;
	}

	/* Code for Tmr Mailbox Creation used for Tmr Msg Retrival */

	if (m_NCS_IPC_CREATE(&tipc_cb.tmr_mbx) != NCSCC_RC_SUCCESS) {
		/* Mail box creation failed */
		syslog(LOG_ERR, "MDTM:TIPC Tmr Mailbox Creation failed:\n");
		close(tipc_cb.Dsock);
		close(tipc_cb.BSRsock);
		return NCSCC_RC_FAILURE;
	} else {

		NCS_SEL_OBJ obj;

		/* Code added for attaching the mailbox, to eliminate the DBG
		 * Print at sysf_ipc.c: 640 */
		if (NCSCC_RC_SUCCESS != m_NCS_IPC_ATTACH(&tipc_cb.tmr_mbx)) {
			m_NCS_IPC_RELEASE(&tipc_cb.tmr_mbx, NULL);
			syslog(LOG_ERR,
			       "MDTM:TIPC Tmr Mailbox  Attach failed:\n");
			close(tipc_cb.Dsock);
			close(tipc_cb.BSRsock);
			return NCSCC_RC_FAILURE;
		}

		obj = m_NCS_IPC_GET_SEL_OBJ(&tipc_cb.tmr_mbx);

		/* retreive the corresponding fd for mailbox and fill it in cb
		 */
		tipc_cb.tmr_fd = m_GET_FD_FROM_SEL_OBJ(
		    obj); /* extract and fill value needs to be extracted */
		mdtm_attach_mbx(tipc_cb.tmr_mbx);
		mdtm_set_transport(MDTM_TX_TYPE_TIPC);
	}

	/* Create a task to receive the events and data */
	if (mdtm_create_rcv_task(tipc_cb.hdle_mdtm) != NCSCC_RC_SUCCESS) {
		syslog(LOG_ERR,
		       "MDTM:TIPC Receive Task Creation Failed in MDTM_INIT\n");
		close(tipc_cb.Dsock);
		close(tipc_cb.BSRsock);
		m_NCS_IPC_RELEASE(&tipc_cb.tmr_mbx, NULL);
		return NCSCC_RC_FAILURE;
	}

	/* Set the default TIPC importance level */
	if (TIPCIMPORTANCE > TIPC_HIGH_IMPORTANCE) {
		m_MDS_LOG_ERR("MDTM: Can't set socket option TIPC_IMP = %d\n",
			      TIPCIMPORTANCE);
		osafassert(0);
	}

	int imp = TIPCIMPORTANCE;
	if (setsockopt(tipc_cb.BSRsock, SOL_TIPC, TIPC_IMPORTANCE, &imp,
		       sizeof(imp)) != 0) {
		m_MDS_LOG_ERR(
		    "MDTM: Can't set default socket option TIPC_IMP err :%s\n",
		    strerror(errno));
		osafassert(0);
	} else {
		m_MDS_LOG_INFO(
		    "MDTM: Successfully set default socket option TIPC_IMP = %d",
		    TIPCIMPORTANCE);
	}

	int droppable = 0;
	if (setsockopt(tipc_cb.BSRsock, SOL_TIPC, TIPC_DEST_DROPPABLE,
		       &droppable, sizeof(droppable)) != 0) {
		LOG_ER("MDTM: Can't set TIPC_DEST_DROPPABLE to zero err :%s\n",
		       strerror(errno));
		m_MDS_LOG_ERR(
		    "MDTM: Can't set TIPC_DEST_DROPPABLE to zero err :%s\n",
		    strerror(errno));
		osafassert(0);
	} else {
		m_MDS_LOG_NOTIFY(
		    "MDTM: Successfully set TIPC_DEST_DROPPABLE to zero");
	}

	return NCSCC_RC_SUCCESS;
}

/*********************************************************

  Function NAME: mdtm_tipc_check_for_endianness

  DESCRIPTION:

  ARGUMENTS:

  RETURNS:  0 - host order
	    1 - network order
	    2 - NCSCC_RC_FAILURE

*********************************************************/
static uint32_t mdtm_tipc_check_for_endianness(void)
{
	struct sockaddr_tipc srv;
	int d_fd;
	struct tipc_event event;

	struct tipc_subscr subs = {
	    {0, 0, ~0}, ~0, TIPC_SUB_PORTS, {2, 2, 2, 2, 2, 2, 2, 2}};

	/* Connect to the Topology Server */
	d_fd = socket(AF_TIPC, SOCK_SEQPACKET, 0);
	if (d_fd < 0) {
		syslog(
		    LOG_ERR,
		    "MDTM:TIPC D Socket creation failed in mdtm_tipc_check_for_endianness err :%s",
		    strerror(errno));
		return NCSCC_RC_FAILURE;
	}

	memset(&srv, 0, sizeof(srv));
	srv.family = AF_TIPC;
	srv.addrtype = TIPC_ADDR_NAME;
	srv.addr.name.name.type = TIPC_TOP_SRV;
	srv.addr.name.name.instance = TIPC_TOP_SRV;

	if (0 > connect(d_fd, (struct sockaddr *)&srv, sizeof(srv))) {
		syslog(
		    LOG_ERR,
		    "MDTM:TIPC Failed to connect to topology server in mdtm_check_for_endianness err :%s",
		    strerror(errno));
		close(d_fd);
		return NCSCC_RC_FAILURE;
	}

	if (send(d_fd, &subs, sizeof(subs), 0) != sizeof(subs)) {
		syslog(LOG_ERR, "MDTM:TIPC Failed to send subscription err :%s",
		       strerror(errno));
		close(d_fd);
		return NCSCC_RC_FAILURE;
	}

	while (recv(d_fd, &event, sizeof(event), 0) == sizeof(event)) {
		close(d_fd);
		return 0;
	}
	close(d_fd);

	return 1;
}

/*********************************************************

  Function NAME: mdtm_tipc_destroy

  DESCRIPTION:

  ARGUMENTS:

  RETURNS:  1 - NCSCC_RC_SUCCESS
	    2 - NCSCC_RC_FAILURE

*********************************************************/
uint32_t mdtm_tipc_destroy(void)
{
	MDTM_REF_HDL_LIST *temp_ref_hdl_list_entry = NULL;
	MDTM_REASSEMBLY_QUEUE *reassem_queue = NULL;
	MDTM_REASSEMBLY_KEY reassembly_key;

	/* close sockets first */
	close(tipc_cb.BSRsock);
	close(tipc_cb.Dsock);

	/* Destroy receiving task */
	if (mdtm_destroy_rcv_task() != NCSCC_RC_SUCCESS) {
		m_MDS_LOG_ERR(
		    "MDTM: Receive Task Destruction Failed in MDTM_INIT\n");
	}
	/* Destroy mailbox */
	m_NCS_IPC_DETACH(&tipc_cb.tmr_mbx, (NCS_IPC_CB)mdtm_mailbox_mbx_cleanup,
			 NULL);
	m_NCS_IPC_RELEASE(&tipc_cb.tmr_mbx,
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
	num_subscriptions = 0;
	handle = 0;
	mdtm_global_frag_num = 0;

	return NCSCC_RC_SUCCESS;
}

/*********************************************************

  Function NAME: mdtm_tipc_own_node

  DESCRIPTION:

  ARGUMENTS:

  RETURNS:  1 - NCSCC_RC_SUCCESS
	    2 - NCSCC_RC_FAILURE

*********************************************************/
static uint32_t mdtm_tipc_own_node(int fd)
{
	struct sockaddr_tipc addr;
	socklen_t sz = sizeof(addr);

	memset(&addr, 0, sizeof(addr));
	if (0 > getsockname(tipc_cb.Dsock, (struct sockaddr *)&addr, &sz)) {
		m_MDS_LOG_ERR("MDTM: Failed to get Own Node Address  err :%s",
			      strerror(errno));
		return 0;
	}
	return addr.addr.id.node;
}

/*********************************************************

  Function NAME: mdtm_create_rcv_task

  DESCRIPTION:

  ARGUMENTS:

  RETURNS:  1 - NCSCC_RC_SUCCESS
	    2 - NCSCC_RC_FAILURE

*********************************************************/

static uint32_t mdtm_create_rcv_task(int mdtm_hdle)
{
	int policy = SCHED_RR; /*root defaults */
	int max_prio = sched_get_priority_max(policy);
	int min_prio = sched_get_priority_min(policy);
	int prio_val = ((max_prio - min_prio) * 0.87);

	tipc_cb.recvbuf = malloc(TIPC_MAX_USER_MSG_SIZE);
	if (tipc_cb.recvbuf == NULL) {
		m_MDS_LOG_ERR("MDS: %s: malloc failed", __FUNCTION__);
		return NCSCC_RC_OUT_OF_MEM;
	}

	if (ncs_task_create((NCS_OS_CB)mdtm_process_recv_events,
			    (NCSCONTEXT)(long)mdtm_hdle, "OSAF_MDS", prio_val,
			    policy, NCS_MDTM_STACKSIZE,
			    &tipc_cb.mdtm_hdle_task) != NCSCC_RC_SUCCESS) {
		m_MDS_LOG_ERR("MDTM: Task Creation-failed:\n");
		free(tipc_cb.recvbuf);
		return NCSCC_RC_FAILURE;
	}

	return NCSCC_RC_SUCCESS;
}

/*********************************************************

  Function NAME: mdtm_destroy_rcv_task

  DESCRIPTION:

  ARGUMENTS:

  RETURNS:  1 - NCSCC_RC_SUCCESS
	    2 - NCSCC_RC_FAILURE

*********************************************************/
static uint32_t mdtm_destroy_rcv_task(void)
{
	if (ncs_task_release(tipc_cb.mdtm_hdle_task) != NCSCC_RC_SUCCESS) {
		m_MDS_LOG_ERR("MDTM: Stop of the Created Task-failed:\n");
	}

	free(tipc_cb.recvbuf);

	return NCSCC_RC_SUCCESS;
}

/*********************************************************
  Function NAME: recvfrom_connectionless
  DESCRIPTION: The following routines have been created to assist to determine
 why an undelivered message has been returned to its sender.

  ARGUMENTS: Similer to recvfrom() of TIPC

  RETURNS: Similer to recvfrom() of TIPC
 *********************************************************/
ssize_t recvfrom_connectionless(int sd, void *buf, size_t nbytes, int flags,
				struct sockaddr *from, socklen_t *addrlen)
{
	struct msghdr msg = {0};
	struct iovec iov = {0};
	char anc_buf[CMSG_SPACE(8) + CMSG_SPACE(1024) + CMSG_SPACE(12)];
	struct cmsghdr *anc;
	int has_addr;
	int anc_data[2];

	has_addr = (from != NULL) && (addrlen != NULL);

	iov.iov_base = buf;
	iov.iov_len = nbytes;

	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_name = from;
	msg.msg_namelen = (has_addr) ? *addrlen : 0;
	msg.msg_control = anc_buf;
	msg.msg_controllen = sizeof(anc_buf);

	while (true) {
		ssize_t sz = recvmsg(sd, &msg, flags);
		if (sz == -1) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				return sz;
			} else if (errno == EINTR) {
				continue;
			} else {
				/* -1 indicates connection termination
				 * connectionless this not possible */
				osaf_abort(sz);
			}
		}
		if (sz >= 0) {
			anc = CMSG_FIRSTHDR(&msg);
			if (anc == NULL) {
				m_MDS_LOG_DBG("MDTM: size: %d  anc is NULL",
					      (int)sz);
			}
			while (anc != NULL) {
				/* Receipt of a normal data message never
				   creates the TIPC_ERRINFO and TIPC_RETDATA
				   objects, and only creates the TIPC_DESTNAME
				   object if the message was sent using a TIPC
				   name or name sequence as the destination
				   rather than a TIPC port ID*/
				if (anc->cmsg_type == TIPC_ERRINFO) {
					anc_data[0] =
					    *((unsigned int *)(CMSG_DATA(anc) +
							       0));
					if (anc_data[0] == TIPC_ERR_OVERLOAD) {
						LOG_ER(
						    "MDTM: From <0x%"PRIx32 ":%"PRIu32 "> undeliverable message condition ancillary data: TIPC_ERR_OVERLOAD",
						    ((struct sockaddr_tipc*) from)->addr.id.node,
						    ((struct sockaddr_tipc*) from)->addr.id.ref);
						m_MDS_LOG_CRITICAL(
						    "MDTM: From <0x%"PRIx32 ":%"PRIu32 "> undeliverable message condition ancillary data: TIPC_ERR_OVERLOAD ",
						    ((struct sockaddr_tipc*) from)->addr.id.node,
						    ((struct sockaddr_tipc*) from)->addr.id.ref);
					} else {
						/* TIPC_ERRINFO - TIPC error
						 * code associated with a
						 * returned data message or a
						 * connection termination
						 * message */
						m_MDS_LOG_DBG(
						    "MDTM: undelivered message condition ancillary data: TIPC_ERRINFO err : %d",
						    anc_data[0]);
					}
				} else if (anc->cmsg_type == TIPC_RETDATA) {
					/* If we set TIPC_DEST_DROPPABLE off
					   message (configure TIPC to return
					   rejected messages to the sender ) we
					   will hit this when we implement MDS
					   retransmit lost messages, can be
					   replaced with flow control logic */
					/* TIPC_RETDATA -The contents of a
					 * returned data message */
					LOG_IN(
					    "MDTM: undelivered message condition ancillary data: TIPC_RETDATA");
					m_MDS_LOG_INFO(
					    "MDTM: undelivered message condition ancillary data: TIPC_RETDATA");
				} else if (anc->cmsg_type == TIPC_DESTNAME) {
					if (sz == 0) {
						m_MDS_LOG_DBG(
						    "MDTM: recd bytes=0 on received on sock, abnormal/unknown  condition. Ignoring");
					}
				} else {
					m_MDS_LOG_INFO(
					    "MDTM: unrecognized ancillary data type %u\n",
					    anc->cmsg_type);
					if (sz == 0) {
						m_MDS_LOG_DBG(
						    "MDTM: recd bytes=0 on received on sock, abnormal/unkown  condition. Ignoring");
					}
				}

				anc = CMSG_NXTHDR(&msg, anc);
			}

			if (has_addr)
				*addrlen = msg.msg_namelen;
		}
		return sz;
	}
}

static void osaf_sigalrm_handler(int signo) {
	raise(SIGABRT);
}

/*********************************************************

  Function NAME: mdtm_process_recv_events

  DESCRIPTION:

  ARGUMENTS:

  RETURNS:  1 - NCSCC_RC_SUCCESS
	    2 - NCSCC_RC_FAILURE

 *********************************************************/
static uint32_t mdtm_process_recv_events(void)
{
	enum { FD_DSOCK = 0, FD_BSRSOCK, FD_TMRFD, FD_TMRWD, NUM_FDS };

	/*
	   STEP 1: Poll on the BSRsock and Dsock to get the events
	   if data is received process the received data
	   if discovery events are received , process the discovery events
	 */

	int timerfd = -1;
	if (getenv("OSAF_MDS_WATCHDOG") != NULL) {
		signal(SIGALRM, osaf_sigalrm_handler);
		timerfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
		struct itimerspec spec;
		spec.it_interval.tv_sec = 0;
		spec.it_interval.tv_nsec = 100000000;
		spec.it_value.tv_sec = 0;
		spec.it_value.tv_nsec = 100000000;
		timerfd_settime(timerfd, 0, &spec, NULL);
		alarm(4);
		LOG_NO("Started MDS watchdog");
	}

	while (true) {
		int pollres;

		struct pollfd pfd[NUM_FDS] = {{0}};

		struct tipc_event event;

		pfd[FD_DSOCK].fd = tipc_cb.Dsock;
		pfd[FD_DSOCK].events = POLLIN;
		pfd[FD_BSRSOCK].fd = tipc_cb.BSRsock;
		pfd[FD_BSRSOCK].events = POLLIN;
		pfd[FD_TMRFD].fd = tipc_cb.tmr_fd;
		pfd[FD_TMRFD].events = POLLIN;
		pfd[FD_TMRWD].fd = timerfd;
		pfd[FD_TMRWD].events = POLLIN;

		pfd[FD_DSOCK].revents = pfd[FD_BSRSOCK].revents =
		    pfd[FD_TMRFD].revents = pfd[FD_TMRWD].revents = 0;

		pollres = poll(pfd, NUM_FDS, MDTM_TIPC_POLL_TIMEOUT);

		if (pollres > 0) { /* Check for EINTR and discard */
			memset(&event, 0, sizeof(event));
			osaf_mutex_lock_ordie(&gl_mds_library_mutex);
			if (pfd[FD_DSOCK].revents == POLLIN) {
				while (true) {
					ssize_t rc =
					    recv(tipc_cb.Dsock, &event,
						 sizeof(event), MSG_DONTWAIT);
					if (rc == -1) {
						if (errno == EAGAIN ||
						    errno == EWOULDBLOCK) {
							break;
						} else if (errno == EINTR) {
							continue;
						} else {
							m_MDS_LOG_ERR(
							    "Unable to capture the recd event .. Continuing  err :%s",
							    strerror(errno));
							break;
						}
					} else {
						if (rc != sizeof(event)) {
							m_MDS_LOG_ERR(
							    "Unable to capture the recd event .. Continuing  err :%s",
							    strerror(errno));
							break;
						} else {
							if (NTOHL(
								event.event) ==
							    TIPC_PUBLISHED) {

								m_MDS_LOG_INFO(
								    "MDTM: Published: ");
								m_MDS_LOG_INFO(
								    "MDTM:  <%u,%u,%u> port id <0x%08x:%u>\n",
								    NTOHL(
									event.s
									    .seq
									    .type),
								    NTOHL(
									event
									    .found_lower),
								    NTOHL(
									event
									    .found_upper),
								    NTOHL(
									event
									    .port
									    .node),
								    NTOHL(
									event
									    .port
									    .ref));

								if (NCSCC_RC_SUCCESS !=
								    mdtm_process_discovery_events(
									TIPC_PUBLISHED,
									event)) {
								}
							} else if (
							    NTOHL(
								event.event) ==
							    TIPC_WITHDRAWN) {
								m_MDS_LOG_INFO(
								    "MDTM: Withdrawn: ");
								m_MDS_LOG_INFO(
								    "MDTM:  <%u,%u,%u> port id <0x%08x:%u>\n",
								    NTOHL(
									event.s
									    .seq
									    .type),
								    NTOHL(
									event
									    .found_lower),
								    NTOHL(
									event
									    .found_upper),
								    NTOHL(
									event
									    .port
									    .node),
								    NTOHL(
									event
									    .port
									    .ref));

								if (NCSCC_RC_SUCCESS !=
								    mdtm_process_discovery_events(
									TIPC_WITHDRAWN,
									event)) {
									m_MDS_LOG_INFO(
									    "MDTM: Withdrawn event processing status: F");
								}
							} else if (
							    NTOHL(
								event.event) ==
							    TIPC_SUBSCR_TIMEOUT) {
								/* As the
								 * timeout
								 * passed in
								 * infinite, No
								 * need to check
								 * for the
								 * Timeout */
								m_MDS_LOG_ERR(
								    "MDTM: Timeou Event");
								m_MDS_LOG_INFO(
								    "MDTM: Timeou Event");
								m_MDS_LOG_INFO(
								    "MDTM:  <%u,%u,%u> port id <0x%08x:%u>\n",
								    NTOHL(
									event.s
									    .seq
									    .type),
								    NTOHL(
									event
									    .found_lower),
								    NTOHL(
									event
									    .found_upper),
								    NTOHL(
									event
									    .port
									    .node),
								    NTOHL(
									event
									    .port
									    .ref));
							} else {
								m_MDS_LOG_ERR(
								    "MDTM: Unknown Event");
								/* This should
								 * never come */
								osafassert(0);
							}
						}
					}
					if (pfd[FD_DSOCK].revents & POLLHUP) {
						m_MDS_LOG_INFO(
						    "MDTM: break (pfd[FD_DSOCK].revents == POLLIN) Loop..");
						break;
					}
				}
			}
			if (pfd[FD_DSOCK].revents & POLLHUP) {
				/* This value is returned when the number of
				   subscriptions made cross the tipc max_subscr
				   limit, so no more connection to the tipc
				   topserver is present(viz no more up/down
				   events), so abort and exit the process */
				m_MDS_LOG_CRITICAL(
				    "MDTM: POLLHUP returned on Discovery Socket, No. of subscriptions=%d",
				    num_subscriptions);
				osaf_abort(
				    pfd[FD_DSOCK].revents); /* This means, the
							       process is use
							       less as it has
							       lost the
							       connectivity with
							       the topology
							       server and will
							       not be able to
							       receive any
							       UP/DOWN events */
			}

			if (pfd[FD_BSRSOCK].revents & POLLIN) {

				/* Data Received */

				uint8_t *inbuf = tipc_cb.recvbuf;
				uint8_t *data; /* Used for decoding */
#ifdef MDS_CHECKSUM_ENABLE_FLAG
				uint16_t old_checksum = 0;
				uint16_t new_checksum = 0;
#endif
				struct sockaddr_tipc client_addr;
				socklen_t alen = sizeof(client_addr);

				m_MDS_LOG_INFO(
				    "MDTM: Data received: Processing data ");

				unsigned int recv_ctr = 0;
				while (true) {
					uint16_t recd_buf_len = 0;
					ssize_t recd_bytes =
					    recvfrom_connectionless(
						tipc_cb.BSRsock, inbuf,
						TIPC_MAX_USER_MSG_SIZE,
						MSG_DONTWAIT,
						(struct sockaddr *)&client_addr,
						&alen);
					if (recd_bytes == -1) {
						m_MDS_LOG_DBG(
						    "MDTM: no more data to read");
						break;
					}
					if (recd_bytes == 0) {
						m_MDS_LOG_DBG(
						    "MDTM: recd bytes=0 on received on sock, abnormal/unknown/hack  condition. Ignoring");
						break;
					}
					data = inbuf;

					recd_buf_len = ncs_decode_16bit(&data);

					if (pfd[FD_BSRSOCK].revents & POLLERR) {
						m_MDS_LOG_ERR(
						    "MDTM: Error Recd:tipc_id=<0x%08x:%u>:errno=0x%08x",
						    client_addr.addr.id.node,
						    client_addr.addr.id.ref,
						    errno);
					} else if (recd_buf_len == recd_bytes) {
						uint64_t tipc_id = 0;
						uint32_t buff_dump = 0;
						tipc_id =
						    ((uint64_t)client_addr.addr
							 .id.node)
						    << 32; /* TIPC_ID=<NODE,REF>
							    */
						tipc_id |=
						    client_addr.addr.id.ref;

#ifdef MDS_CHECKSUM_ENABLE_FLAG
						if (inbuf[2] == 1) {
							old_checksum =
							    ((uint16_t)inbuf[3]
								 << 8 |
							     inbuf[4]);
							inbuf[3] = 0;
							inbuf[4] = 0;
							new_checksum =
							    mds_checksum(
								recd_bytes,
								inbuf);

							if (old_checksum !=
							    new_checksum) {
								m_MDS_LOG_ERR(
								    "CHECKSUM-MISMATCH:recvd_on_sock=%zd, Tipc_id=0x%llu, Adest = <%08x,%u>",
								    recd_bytes,
								    tipc_id,
								    m_MDS_GET_NCS_NODE_ID_FROM_TIPC_NODE_ID(
									client_addr
									    .addr
									    .id
									    .node),
								    client_addr
									.addr.id
									.ref);
								mds_buff_dump(
								    inbuf,
								    recd_bytes,
								    100);
								osaf_abort();
								break;
							}
							mdtm_process_recv_data(
							    &inbuf[5],
							    recd_bytes - 5,
							    tipc_id,
							    &buff_dump);
							if (buff_dump) {
								m_MDS_LOG_ERR(
								    "RECV_DATA_PROCESS:recvd_on_sock=%zd, Tipc_id=0x%llu, Adest = <%08x,%u>",
								    recd_bytes,
								    tipc_id,
								    m_MDS_GET_NCS_NODE_ID_FROM_TIPC_NODE_ID(
									client_addr
									    .addr
									    .id
									    .node),
								    client_addr
									.addr.id
									.ref);
								mds_buff_dump(
								    inbuf,
								    recd_bytes,
								    100);
							}
						} else {
							mdtm_process_recv_data(
							    &inbuf[5],
							    recd_bytes - 5,
							    tipc_id,
							    &buff_dump);
						}
#else
						mdtm_process_recv_data(
						    &inbuf[2], recd_bytes - 2,
						    tipc_id, &buff_dump);
#endif
					} else {
						uint64_t tipc_id;
						tipc_id =
						    ((uint64_t)client_addr.addr
							 .id.node)
						    << 32; /* TIPC_ID=<NODE,REF>
							    */
						tipc_id |=
						    client_addr.addr.id.ref;

						/* Log message that we are
						 * dropping the data */
						m_MDS_LOG_ERR(
						    "LEN-MISMATCH:recvd_on_sock=%zd, size_in_mds_hdr=%d,  Tipc_id= %" PRIu64
						    ", Adest = <%08x,%u>",
						    recd_bytes, recd_buf_len,
						    tipc_id,
						    m_MDS_GET_NCS_NODE_ID_FROM_TIPC_NODE_ID(
							client_addr.addr.id
							    .node),
						    client_addr.addr.id.ref);
						mds_buff_dump(inbuf, recd_bytes,
							      100);
					}
					if ((++recv_ctr > MAX_RECV_THRESHOLD) ||
					    (osaf_poll_one_fd(pfd[FD_DSOCK].fd,
							      0) == 1) ||
					    (pfd[FD_TMRFD].revents == POLLIN)) {
						m_MDS_LOG_INFO(
						    "MDTM: break (pfd[FD_BSRSOCK].revents & POLLIN) Loop...");
						break;
					}
				}
			}
			if (pfd[FD_TMRFD].revents == POLLIN) {
				m_MDS_LOG_INFO(
				    "MDTM: Processing Timer mailbox events\n");

				/* Check if destroy-event has been processed */
				if (mds_tmr_mailbox_processing() ==
				    NCSCC_RC_DISABLED) {
					/* Quit ASAP. We have acknowledge that
					   MDS thread can be destroyed. Note
					   that the destroying thread is waiting
					   for the MDS_UNLOCK, before proceeding
					   with pthread-cancel and pthread-join
					 */
					osaf_mutex_unlock_ordie(
					    &gl_mds_library_mutex);

					/* N O T E : No further system calls
					   etc. This is to ensure that the
					   pthread-cancel & pthread-join, do not
					   get blocked. */
					return NCSCC_RC_SUCCESS; /* Thread quit
								  */
				}
			}

			if (pfd[FD_TMRWD].revents == POLLIN) {
				alarm(4);
				uint64_t expirations = 0;
				if (read(timerfd, &expirations, 8) != 8) LOG_ER("MDS error reading timerfd value");
				if (expirations != 1) LOG_NO("MDS timerfd expired %" PRIu64 " times", expirations);
			}
			osaf_mutex_unlock_ordie(&gl_mds_library_mutex);
		} /* if pollres */
	}	 /* while */
	return NCSCC_RC_SUCCESS;
}

/*********************************************************

  Function NAME: mdtm_process_discovery_events

  DESCRIPTION:

  ARGUMENTS:

  RETURNS:  1 - NCSCC_RC_SUCCESS
	    2 - NCSCC_RC_FAILURE

*********************************************************/

static uint32_t mdtm_process_discovery_events(uint32_t discovery_event,
					      struct tipc_event event)
{
	/*
	   STEP 1: Extract the type of  event name such as VDEST, SVC, PCON,
	   NODE or Process. STEP 2: Depending upon the discovery_event call the
	   respective "xx_up" or  "xx_down" functions

	   if(discovery_event==TIPC_PUBLISHED)
	   {
	   if(MDS_TIPC_NAME_TYPE == SVC_INST_TYPE)
	   {
	   STEP 1: If an entry with TX_HDL = TIPC_ID doesnt exist in
	   ADEST_INFO_TABLE then add an entry.

	   STEP 2: Call mds_mcm_svc_up(pwe_id, svc_id, role, scope, vdest,
	   MDS_DEST adest)
	   }
	   else if (MDS_TIPC_NAME_TYPE == PCON_ID_DETAILS )
	   {

	   STEP 1: Retrieve ADEST, PCON_ID and TIPC_ID from Publish event

	   STEP 2: Update ADEST_INFO-TABLE with the parameters
	   }
	   else if (MDS_TIPC_NAME_TYPE = VDEST_INST_TYPE)
	   {
	   STEP 1: If with ID VDEST_ ID is present in LOCAL-VDEST table
	   then call mds_mcm_vdest_up(vdest_id)
	   }
	   * SIM ONLY *
	   else if (MDS_TIPC_NAME_TYPE == NODE_ID_DETAILS)
	   {
	   STEP 1: Retrieve NODE_ID and store in ADEST-INFO Table
	   }
	   }
	   else if (discovery_event==TIPC_WITHDRAWN)
	   {
	   if (MDS_TIPC_NAME_TYPE == SVC_INST_TYPE )
	   {
	   STEP 1: If an entry with TX_HDL = TIPC_ID exists in ADEST_INFO_TABLE.
	   Call mds_mcm_svc_down(pwe_id, svc_id, role, scope, vdest, MDS_DEST
	   adest)
	   }
	   else if (MDS_TIPC_NAME_TYPE == PCON_ID_DETAILS )
	   {
	   Remove Entry from the ADEST-INFO-TABLE matching the ADEST,PCON and
	   TIPC_ID
	   }
	   else if (MDS_TIPC_NAME_TYPE = VDEST_INST_TYPE)
	   {
	   * DISCARD : As remote VDEST need to know only Multiple Active
	   VDEST coming up in system *
	   }
	   * SIM ONLY *
	   else if (MDS_TIPC_NAME_TYPE = NODE_ID_DETAILS)
	   {
	   Remove Entry from the ADEST-INFO-TABLE matching the ADEST, NODE_ID
	   and TIPC_ID
	   }
	   }
	 */
	uint32_t inst_type = 0, type = 0, lower = 0, node = 0, ref = 0;

	MDS_DEST adest = 0;
	MDS_SUBTN_REF_VAL subtn_ref_val;

	type = NTOHL(event.s.seq.type);
	lower = NTOHL(event.found_lower);
	node = NTOHL(event.port.node);
	ref = NTOHL(event.port.ref);
	subtn_ref_val = *((uint64_t *)(event.s.usr_handle));

	inst_type =
	    type & 0x00ff0000; /* To get which type event
				  either SVC,VDEST, PCON, NODE OR process */
	switch (inst_type) {
	case MDS_SVC_INST_TYPE: {
		PW_ENV_ID pwe_id;
		MDS_SVC_ID svc_id;
		V_DEST_RL role;
		NCSMDS_SCOPE_TYPE scope;
		MDS_VDEST_ID vdest;
		NCS_VDEST_TYPE policy = 0;
		MDS_SVC_HDL svc_hdl;
		char adest_details[MDS_MAX_PROCESS_NAME_LEN];
		memset(adest_details, 0, MDS_MAX_PROCESS_NAME_LEN);

		MDS_SVC_PVT_SUB_PART_VER svc_sub_part_ver;
		MDS_SVC_ARCHWORD_TYPE archword_type;

		uint32_t node_status = 0;

		svc_id = (uint16_t)(type & MDS_EVENT_MASK_FOR_SVCID);
		vdest = (MDS_VDEST_ID)(lower);
		archword_type = (MDS_SVC_ARCHWORD_TYPE)(
		    (lower & MDS_ARCHWORD_MASK) >>
		    (LEN_4_BYTES - MDS_ARCHWORD_BITS_LEN));
		svc_sub_part_ver = (MDS_SVC_PVT_SUB_PART_VER)(
		    (lower & MDS_VER_MASK) >>
		    (LEN_4_BYTES - MDS_VER_BITS_LEN - MDS_ARCHWORD_BITS_LEN));
		pwe_id =
		    (type >> MDS_EVENT_SHIFT_FOR_PWE) & MDS_EVENT_MASK_FOR_PWE;
		policy = (lower & MDS_POLICY_MASK) >>
			 (LEN_4_BYTES - MDS_ARCHWORD_BITS_LEN -
			  MDS_VER_BITS_LEN - VDEST_POLICY_LEN);
		role = (lower & MDS_ROLE_MASK) >>
		       (LEN_4_BYTES - MDS_ARCHWORD_BITS_LEN - MDS_VER_BITS_LEN -
			VDEST_POLICY_LEN - ACT_STBY_LEN);
		scope =
		    (lower & MDS_SCOPE_MASK) >>
		    (LEN_4_BYTES - MDS_ARCHWORD_BITS_LEN - MDS_VER_BITS_LEN -
		     VDEST_POLICY_LEN - ACT_STBY_LEN - MDS_SCOPE_LEN);

		m_MDS_LOG_INFO("MDTM: Received SVC event");

		if (NCSCC_RC_SUCCESS !=
		    mdtm_get_from_ref_tbl(subtn_ref_val, &svc_hdl)) {
			m_MDS_LOG_INFO(
			    "MDTM: No entry in ref tbl so dropping the recd event");
			return NCSCC_RC_FAILURE;
		}

		if (role == 0)
			role = V_DEST_RL_ACTIVE;
		else
			role = V_DEST_RL_STANDBY;

		if (policy == 0)
			policy = NCS_VDEST_TYPE_MxN;
		else
			policy = NCS_VDEST_TYPE_N_WAY_ROUND_ROBIN;

		if (scope == 0)
			scope = NCSMDS_SCOPE_NONE;
		else if (scope == 1)
			scope = NCSMDS_SCOPE_INTRANODE;
		else if (scope == 3)
			scope = NCSMDS_SCOPE_INTRACHASSIS;
		else {
			m_MDS_LOG_ERR("MDTM: SVC Scope Not supported");
			return NCSCC_RC_FAILURE;
		}

		node_status = m_MDS_CHECK_TIPC_NODE_ID_RANGE(node);

		if (NCSCC_RC_SUCCESS == node_status) {
			adest = ((((uint64_t)(
				      m_MDS_GET_NCS_NODE_ID_FROM_TIPC_NODE_ID(
					  node)))
				  << 32) |
				 ref);
		} else {
			m_MDS_LOG_ERR(
			    "MDTM: Dropping  the svc event for svc_id = %s(%d), subscribed by "
			    "svc_id = %s(%d) as the TIPC NODEid is not in the prescribed range=0x%08x, SVC Event type=%d",
			    get_svc_names(svc_id), svc_id,
			    get_svc_names(
				m_MDS_GET_SVC_ID_FROM_SVC_HDL(svc_hdl)),
			    m_MDS_GET_SVC_ID_FROM_SVC_HDL(svc_hdl), node,
			    discovery_event);
			return NCSCC_RC_FAILURE;
		}
		get_subtn_adest_details(m_MDS_GET_PWE_HDL_FROM_SVC_HDL(svc_hdl),
					m_MDS_GET_SVC_ID_FROM_SVC_HDL(svc_hdl),
					adest, adest_details);

		if (TIPC_PUBLISHED == discovery_event) {
			m_MDS_LOG_NOTIFY(
			    "MDTM: svc up event for svc_id = %s(%d), subscri. by svc_id = %s(%d) pwe_id=%d Adest = %s",
			    get_svc_names(svc_id), svc_id,
			    get_svc_names(
				m_MDS_GET_SVC_ID_FROM_SVC_HDL(svc_hdl)),
			    m_MDS_GET_SVC_ID_FROM_SVC_HDL(svc_hdl), pwe_id,
			    adest_details);

			if (NCSCC_RC_SUCCESS !=
			    mds_mcm_svc_up(pwe_id, svc_id, role, scope, vdest,
					   policy, adest, 0, svc_hdl,
					   subtn_ref_val, svc_sub_part_ver,
					   archword_type)) {
				m_MDS_LOG_ERR(
				    "SVC-UP Event processsing failed for svc_id = %s(%d), subscribed by svc_id = %s(%d)",
				    get_svc_names(svc_id), svc_id,
				    get_svc_names(
					m_MDS_GET_SVC_ID_FROM_SVC_HDL(svc_hdl)),
				    m_MDS_GET_SVC_ID_FROM_SVC_HDL(svc_hdl));
				return NCSCC_RC_FAILURE;
			}
			return NCSCC_RC_SUCCESS;
		} else if (TIPC_WITHDRAWN == discovery_event) {
			m_MDS_LOG_NOTIFY(
			    "MDTM: svc down event for svc_id = %s(%d), subscri. by svc_id = %s(%d) pwe_id=%d Adest = %s",
			    get_svc_names(svc_id), svc_id,
			    get_svc_names(
				m_MDS_GET_SVC_ID_FROM_SVC_HDL(svc_hdl)),
			    m_MDS_GET_SVC_ID_FROM_SVC_HDL(svc_hdl), pwe_id,
			    adest_details);

			if (NCSCC_RC_SUCCESS !=
			    mds_mcm_svc_down(pwe_id, svc_id, role, scope, vdest,
					     policy, adest, 0, svc_hdl,
					     subtn_ref_val, svc_sub_part_ver,
					     archword_type)) {
				m_MDS_LOG_ERR(
				    "MDTM: SVC-DOWN Event processsing failed for svc_id = %s(%d), subscribed by svc_id = %s(%d)\n",
				    get_svc_names(svc_id), svc_id,
				    get_svc_names(
					m_MDS_GET_SVC_ID_FROM_SVC_HDL(svc_hdl)),
				    m_MDS_GET_SVC_ID_FROM_SVC_HDL(svc_hdl));
				return NCSCC_RC_FAILURE;
			}
			return NCSCC_RC_SUCCESS;
		} else {
			m_MDS_LOG_INFO(
			    "MDTM: TIPC EVENT UNSUPPORTED for SVC (other than Publish and Withdraw)\n");
			return NCSCC_RC_FAILURE;
		}
	} break;

	case MDS_VDEST_INST_TYPE: {
		MDS_VDEST_ID vdest_id;
		uint32_t node_status = 0;

		vdest_id = (uint16_t)lower;

		node_status = m_MDS_CHECK_TIPC_NODE_ID_RANGE(node);

		if (NCSCC_RC_SUCCESS == node_status) {
			adest = ((((uint64_t)(
				      m_MDS_GET_NCS_NODE_ID_FROM_TIPC_NODE_ID(
					  node)))
				  << 32) |
				 ref);
		} else {
			m_MDS_LOG_ERR(
			    "MDTM: Dropping  the vdest event, vdest id = %d, as the TIPC NODEid is not \
						in the prescribed range=0x%08x,  Event type=%d",
			    vdest_id, node, discovery_event);
			return NCSCC_RC_FAILURE;
		}
		m_MDS_LOG_INFO("MDTM: Received VDEST event");

		if (TIPC_PUBLISHED == discovery_event) {
			m_MDS_LOG_INFO(
			    "MDTM: Raising the VDEST UP event for VDEST id = %d",
			    vdest_id);
			if (NCSCC_RC_SUCCESS !=
			    mds_mcm_vdest_up(vdest_id, adest)) {
				m_MDS_LOG_ERR(
				    "MDTM: VDEST UP event processing failed for VDEST id = %d",
				    vdest_id);
				return NCSCC_RC_FAILURE;
			}
			/* Return from here */
			return NCSCC_RC_SUCCESS;
		} else if (TIPC_WITHDRAWN == discovery_event) {
			m_MDS_LOG_INFO(
			    "MDTM: Raising the VDEST DOWN event for VDEST id = %d",
			    vdest_id);
			if (NCSCC_RC_SUCCESS !=
			    mds_mcm_vdest_down(vdest_id, adest)) {
				m_MDS_LOG_ERR(
				    "MDTM: VDEST DOWN event processing failed");
				return NCSCC_RC_FAILURE;
			}
			return NCSCC_RC_SUCCESS;
		} else {
			m_MDS_LOG_INFO(
			    "MDTM: TIPC EVENT UNSUPPORTED for VDEST(other than Publish and Withdraw)\n");
			return NCSCC_RC_FAILURE;
		}
	} break;

	case MDS_NODE_INST_TYPE: {
		MDS_SVC_HDL svc_hdl;
		uint32_t node_status = 0;
		NODE_ID node_id = 0;

		m_MDS_LOG_INFO("MDTM: Received NODE event");

		node_status = m_MDS_CHECK_TIPC_NODE_ID_RANGE(node);

		if (NCSCC_RC_SUCCESS == node_status) {
			node_id = ((NODE_ID)(
			    m_MDS_GET_NCS_NODE_ID_FROM_TIPC_NODE_ID(node)));

		} else {
			m_MDS_LOG_ERR(
			    "MDTM: Dropping  the node event,as the TIPC NODEid is not in the \
				     prescribed range=0x%08x,  Event type=%d",
			    node, discovery_event);
			return NCSCC_RC_FAILURE;
		}

		if (NCSCC_RC_SUCCESS !=
		    mdtm_get_from_ref_tbl(subtn_ref_val, &svc_hdl)) {
			m_MDS_LOG_INFO(
			    "MDTM: No entry in ref tbl so dropping the recd event");
			return NCSCC_RC_FAILURE;
		}

		if (TIPC_PUBLISHED == discovery_event) {
			m_MDS_LOG_INFO(
			    "MDTM: Raising the NODE UP event for NODE id = %d",
			    node_id);
			uint32_t up_node_id;
			if ((up_node_id = m_MDS_GET_NODE_ID_FROM_ADEST(
				 m_MDS_GET_ADEST)) == node_id) {
				m_MDS_LOG_INFO(
				    "MDTM:NODE_UP for subtn_ref_val:%" PRIu64
				    " node_name:%s, node_id:%u addr_family:%d ",
				    (uint64_t)subtn_ref_val,
				    gl_mds_mcm_cb->node_name, node_id, AF_TIPC);
				return mds_mcm_node_up(
				    svc_hdl, node_id, NULL, AF_TIPC,
				    gl_mds_mcm_cb->node_name);
			} else {

				m_MDS_LOG_INFO(
				    "MDTM:NODE_UP for subtn_ref_val:%" PRIu64
				    " node_name:%s, node_id:%u addr_family:%d ",
				    (uint64_t)subtn_ref_val, "REMOTE_NODE",
				    node_id, AF_TIPC);
				return mds_mcm_node_up(svc_hdl, node_id, NULL,
						       AF_TIPC, "REMOTE_NODE");
			}

		} else if (TIPC_WITHDRAWN == discovery_event) {
			m_MDS_LOG_INFO(
			    "MDTM: Raising the NODE DOWN event for NODE id = %d",
			    node_id);
			return mds_mcm_node_down(svc_hdl, node_id, AF_TIPC);
		} else {
			m_MDS_LOG_INFO(
			    "MDTM: TIPC EVENT UNSUPPORTED for Node (other than Publish and Withdraw)\n");
			return NCSCC_RC_FAILURE;
		}

	} break;

	default: {
		m_MDS_LOG_ERR(
		    "MDTM: TIPC EVENT UNSUPPORTED (default). If this case comes this should assert as there no other events being processed");
		return NCSCC_RC_FAILURE;
	} break;
	}
	return NCSCC_RC_SUCCESS;
}

/*********************************************************

  Function NAME: mdtm_free_reassem_msg_mem

  DESCRIPTION:

  ARGUMENTS:

  RETURNS:  1 - NCSCC_RC_SUCCESS
	    2 - NCSCC_RC_FAILURE

*********************************************************/
/* Presently doing nothing */

/*********************************************************

  Function NAME: mdtm_process_recv_data

  DESCRIPTION:

  ARGUMENTS:

  RETURNS:  1 - NCSCC_RC_SUCCESS
	    2 - NCSCC_RC_FAILURE

*********************************************************/
/*
   Get the MDS Header from the data received
   if Destination SVC exists
   if the message is a fragmented one
   reassemble  the recd data(is done based on the received data <Sequence
   number, TIPC_ID>) An entry for the matching TIPC_ID is serached in the
   MDS_ADEST_LIST

   if found
   reasesemble is done on the MDS_ADEST_LIST
   if not found
   an entry is created and reasesemble is done on the MDS_ADEST_LIST
   A timer is also started on the reassembly queue (to prevent large timegaps
   between the fragemented packets) Send the data to the upper (Check the role
   of Destination Service for some send types before giving the data to upper
   layer)
 */

/* If the data is recd over here, it means its a fragmented or non-fragmented
 * pkt) */

/* Added for seq number check */

/* We got a fragmented pkt, reassemble */
/* Check in reasssebly queue whether any pkts are present */

/* Checking in reassembly queue */

/* Check whether we recd the last pkt */
/* Last frag in the message recd */

/* Free memory Allocated to this msg and MDTM_REASSEMBLY_QUEUE */

/* Destroy Handle */

/* stop timer and free memory */

/* Delete entry from MDTM_REASSEMBLY_QUEUE */

/* Check SVC_hdl role here */
/* fUNTION WILL RETURN success when svc is in active or quiesced */

/* Free the queued data depending on the data type */

/* Destroy Handle */
/* stop timer and free memory */

/* Delete this entry from Global reassembly queue */

/* Enqueue the data at the End depending on the data type */

/* Reassemble and send to upper layer if last pkt */
/* Last frag in the message recd */
/* Total Data reassembled */

/* Destroy Handle */

/* stop timer and free memory */

/* Depending on the msg type if flat or full enc apply dec space, for setting
 * the uba to decode by user */
/* for direct buff and cpy encoding modes we do nothig */

/* Now delete this entry from Global reassembly queue */

/* Incrementing for next frag */
/* fragment recd is not next fragment */

/* Last frag in the message recd */

/* Free memory Allocated to UB and MDTM_REASSEMBLY_QUEUE */

/* Destroy Handle */

/* stop timer and free memory */

/* Delete entry from MDTM_REASSEMBLY_QUEUE */

/* Some stale message, Log and Drop */

/*********************************************************

  Function NAME: mdtm_process_recv_message_common

  DESCRIPTION:

  ARGUMENTS:

  RETURNS:  1 - NCSCC_RC_SUCCESS
	    2 - NCSCC_RC_FAILURE

*********************************************************/

/* We receive buffer pointer starting from MDS HDR only */

/* Added for seqnum check */

/* Length Checks */

/* Check whether total lenght of message is not less than MDS header length */
/* Check whether mds header length received is not less than mds header length
 * of this instance */

/* Search whether the Destination exists or not , SVC,PWE,VDEST */

/* Allocate the memory for reassem_queue */

/* do nothing */

/* NOTE: Version in ACK message is ignored */
/* Size of Payload data in ACK message should be zero, If not its an error */
/* only for non ack cases */

/* Drop message if version is not 1 and return */
/*if(reassem_queue->recv.msg_fmt_ver!=1)
   {
   m_MDS_LOG_ERR("MDTM: Unable to process the recd message due to wrong
   version=%d",reassem_queue->recv.msg_fmt_ver);
   m_MMGR_FREE_REASSEM_QUEUE(reassem_queue);
   return NCSCC_RC_FAILURE;
   } */

/* Depending on msg type if flat or full enc apply dec space, for setting the
 * uba to decode by user */
/* for direct buff and cpy encoding modes we do nothig */

/* Call upper layer */

/* Free Memory allocated to this structure */

/*
   Message seq no. (32-bit) debug assist) | MoreFrags(1-bit)|
   Fragment Number(15-bit) | Fragment Size(16-bit)
 */

/* Added for seqnum check */

/* Length Checks */

/* Check whether total lenght of message is not less than or equal to MDS header
 * length and MDTM frag header length */
/* Check whether mds header length received is not less than mds header length
 * of this instance */

/* Search whether the Destination exists or not , SVC,PWE,VDEST */

/* do nothing */

/*start the timer */

/*********************************************************

  Function NAME: mdtm_fill_data

  DESCRIPTION:

  ARGUMENTS:

  RETURNS:  1 - NCSCC_RC_SUCCESS
	    2 - NCSCC_RC_FAILURE

*********************************************************/
/* We will never reach here */
/* Nothing done here */

/*********************************************************

  Function NAME: mdtm_check_reassem_queue

  DESCRIPTION:

  ARGUMENTS:

  RETURNS:  1 - NCSCC_RC_SUCCESS
	    2 - NCSCC_RC_FAILURE

*********************************************************/
/*
   STEP 1: Check whether an entry is present with the seq_num and id,
   If present
   return the Pointer
   else
   return Null
 */

/*********************************************************

  Function NAME: mdtm_add_reassemble_queue

  DESCRIPTION:

  ARGUMENTS:

  RETURNS:  1 - NCSCC_RC_SUCCESS
	    2 - NCSCC_RC_FAILURE

*********************************************************/
/*
   STEP 1: create an entry in the reassemble queue with parameters as seq_num
   and id, return the Pointer to the reassembly queue
 */

/* Allocate Memory for reassem_queue */

/*********************************************************

  Function NAME: mdtm_del_reassemble_queue

  DESCRIPTION:

  ARGUMENTS:

  RETURNS:  1 - NCSCC_RC_SUCCESS
	    2 - NCSCC_RC_FAILURE

*********************************************************/
/*
   STEP 1: Check whether an entry is present with the seq_num and id,
   If present
   delete the node
   return success
   else
   return failure
 */

/*********************************************************

  Function NAME: mds_mdtm_svc_install_tipc

  DESCRIPTION:

  ARGUMENTS:

  RETURNS:  1 - NCSCC_RC_SUCCESS
	    2 - NCSCC_RC_FAILURE

*********************************************************/
uint32_t mds_mdtm_svc_install_tipc(PW_ENV_ID pwe_id, MDS_SVC_ID svc_id,
				   NCSMDS_SCOPE_TYPE install_scope,
				   V_DEST_RL role, MDS_VDEST_ID vdest_id,
				   NCS_VDEST_TYPE policy,
				   MDS_SVC_PVT_SUB_PART_VER mds_svc_pvt_ver)
{
	/*
	   STEP 1: Bind to socket BSRSock with Tipc name sequence
	   TIPC Name:
	   <MDS-prefix, SVC_INST_TYPE, svcid>
	   TIPC Range:
	   <pwe,scope,ROLE=role ,vdest> to
	   <pwe,scope,ROLE=role ,vdest>
	 */
	struct sockaddr_tipc server_addr;
	uint32_t server_type = 0, server_inst = 0;
	MDS_SVC_ARCHWORD_TYPE archword = MDS_SELF_ARCHWORD;
	pwe_id = pwe_id << MDS_EVENT_SHIFT_FOR_PWE;
	svc_id = svc_id & MDS_EVENT_MASK_FOR_SVCID;

	if (svc_id == NCSMDS_SVC_ID_AVND) {
		int imp = 0;
		imp = TIPC_HIGH_IMPORTANCE;

		if (setsockopt(tipc_cb.BSRsock, SOL_TIPC, TIPC_IMPORTANCE, &imp,
			       sizeof(imp)) != 0) {
			m_MDS_LOG_ERR(
			    "MDTM: Can't set socket option TIPC_IMP err :%s\n",
			    strerror(errno));
			osafassert(0);
		} else {
			m_MDS_LOG_INFO(
			    "MDTM: Successfully set socket option TIPC_IMP, svc_id = %s(%d)",
			    get_svc_names(svc_id), svc_id);
		}
	}

	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.family = AF_TIPC;
	server_addr.addrtype = TIPC_ADDR_NAMESEQ;

	server_type =
	    server_type | MDS_TIPC_PREFIX | MDS_SVC_INST_TYPE | pwe_id | svc_id;
	server_inst |= (uint32_t)(
	    (archword) << (LEN_4_BYTES -
			   MDS_ARCHWORD_BITS_LEN)); /* Upper  4  bits */
	server_inst |= (uint32_t)((mds_svc_pvt_ver)
				  << (LEN_4_BYTES - MDS_ARCHWORD_BITS_LEN -
				      MDS_VER_BITS_LEN)); /* next 8  Bits */

	if (policy == NCS_VDEST_TYPE_MxN) {
		policy = 0;
		policy = 0 & 0x1;
	} else {
		policy = 0;
		policy = 1 & 0x1;
	}

	server_inst |=
	    (uint32_t)((policy & 0x1) << (LEN_4_BYTES - MDS_ARCHWORD_BITS_LEN -
					  MDS_VER_BITS_LEN -
					  VDEST_POLICY_LEN)); /* Next 1 bit */

	if (role == V_DEST_RL_ACTIVE) {
		role = 0;
	} else
		role = 1;

	server_inst |=
	    (uint32_t)((role & 0x1) << (LEN_4_BYTES - MDS_ARCHWORD_BITS_LEN -
					MDS_VER_BITS_LEN - VDEST_POLICY_LEN -
					ACT_STBY_LEN)); /* Next 1 bit */

	if (install_scope == NCSMDS_SCOPE_NONE) {
		server_addr.scope = TIPC_CLUSTER_SCOPE;
		install_scope = 0;
	} else if (install_scope == NCSMDS_SCOPE_INTRANODE) {
		server_addr.scope = TIPC_NODE_SCOPE;
		install_scope = 1;
	} else if (install_scope == NCSMDS_SCOPE_INTRACHASSIS) {
		server_addr.scope = TIPC_CLUSTER_SCOPE;
		install_scope = 3;
	}

	server_inst |=
	    (uint32_t)((install_scope & 0x3)
		       << (LEN_4_BYTES - MDS_ARCHWORD_BITS_LEN -
			   MDS_VER_BITS_LEN - VDEST_POLICY_LEN - ACT_STBY_LEN -
			   MDS_SCOPE_LEN)); /* Next 2  bit */
	server_inst |= vdest_id;

	server_addr.addr.nameseq.type = server_type;
	server_addr.addr.nameseq.lower = server_inst;
	server_addr.addr.nameseq.upper = server_inst;

	/* The self tipc random port number */
	uint32_t port_id = 0;
	get_tipc_port_id(tipc_cb.BSRsock, &port_id);
	m_MDS_LOG_NOTIFY("MDTM: install_tipc : <p:%u,s:%u,i:%u>", port_id,
			 server_type, server_inst);

	if (0 != bind(tipc_cb.BSRsock, (struct sockaddr *)&server_addr,
		      sizeof(server_addr))) {
		m_MDS_LOG_ERR("MDTM: SVC-INSTALL Failure err :%s\n",
			      strerror(errno));
		return NCSCC_RC_FAILURE;
	}
	m_MDS_LOG_NOTIFY("MDTM: install_tipc : svc_id = %s(%d), vdest=%d",
			 get_svc_names(svc_id), svc_id, vdest_id);
	m_MDS_LOG_INFO("MDTM: SVC-INSTALL Success\n");
	return NCSCC_RC_SUCCESS;
}

/*********************************************************

  Function NAME: mds_mdtm_svc_uninstall_tipc

  DESCRIPTION:

  ARGUMENTS:

  RETURNS:  1 - NCSCC_RC_SUCCESS
	    2 - NCSCC_RC_FAILURE

*********************************************************/

uint32_t mds_mdtm_svc_uninstall_tipc(PW_ENV_ID pwe_id, MDS_SVC_ID svc_id,
				     NCSMDS_SCOPE_TYPE install_scope,
				     V_DEST_RL role, MDS_VDEST_ID vdest_id,
				     NCS_VDEST_TYPE policy,
				     MDS_SVC_PVT_SUB_PART_VER mds_svc_pvt_ver)
{
	/*
	   STEP 1: If MDEST=VDEST
	   NEWROLE(of VDEST)=Backup/Quiesced
	   Else
	   NEWROLE =Active

	   STEP 2: Unbind to socket BSRSock with Tipc name sequence
	   TIPC Name:
	   <MDS-prefix, SVC_INST_TYPE, svcid>
	   TIPC Range:
	   <pwe,scope,NEWROLE,vdest>
	   to
	   <pwe,scope,NEWROLE,vdest>

	 */
	uint32_t server_inst = 0, server_type = 0;
	MDS_SVC_ARCHWORD_TYPE archword = MDS_SELF_ARCHWORD;
	pwe_id = pwe_id << MDS_EVENT_SHIFT_FOR_PWE;
	svc_id = svc_id & MDS_EVENT_MASK_FOR_SVCID;

	struct sockaddr_tipc server_addr;
	memset(&server_addr, 0, sizeof(server_addr));

	server_type =
	    server_type | MDS_TIPC_PREFIX | MDS_SVC_INST_TYPE | pwe_id | svc_id;
	server_inst |= (uint32_t)(
	    (archword) << (LEN_4_BYTES -
			   MDS_ARCHWORD_BITS_LEN)); /* Upper 4 Bits */
	server_inst |= (uint32_t)((mds_svc_pvt_ver)
				  << (LEN_4_BYTES - MDS_ARCHWORD_BITS_LEN -
				      MDS_VER_BITS_LEN)); /* next 8 Bits */

	if (policy == NCS_VDEST_TYPE_MxN) {
		policy = 0;
	} else {
		policy = 1;
	}

	server_inst |=
	    (uint32_t)((policy & 0x1) << (LEN_4_BYTES - MDS_ARCHWORD_BITS_LEN -
					  MDS_VER_BITS_LEN -
					  VDEST_POLICY_LEN)); /* Next 1 bit */

	if (role == V_DEST_RL_ACTIVE) {
		role = 0;
	} else
		role = 1;

	server_inst |=
	    (uint32_t)((role & 0x1) << (LEN_4_BYTES - MDS_ARCHWORD_BITS_LEN -
					MDS_VER_BITS_LEN - VDEST_POLICY_LEN -
					ACT_STBY_LEN)); /* Next 1 bit */

	if (install_scope == NCSMDS_SCOPE_NONE) {
		install_scope = 0;
		server_addr.scope = -TIPC_CLUSTER_SCOPE;
	} else if (install_scope == NCSMDS_SCOPE_INTRANODE) {
		install_scope = 1;
		server_addr.scope = -TIPC_NODE_SCOPE;
	} else if (install_scope == NCSMDS_SCOPE_INTRACHASSIS) {
		install_scope = 3;
		server_addr.scope = -TIPC_CLUSTER_SCOPE;
	}

	server_inst |=
	    (uint32_t)((install_scope & 0x3)
		       << (LEN_4_BYTES - MDS_ARCHWORD_BITS_LEN -
			   MDS_VER_BITS_LEN - VDEST_POLICY_LEN - ACT_STBY_LEN -
			   MDS_SCOPE_LEN)); /* Next 2  bit */

	server_inst |= vdest_id;

	m_MDS_LOG_INFO("MDTM: uninstall_tipc : <%u,%u,%u>", server_type,
		       server_inst, server_inst);

	server_addr.family = AF_TIPC;
	server_addr.addrtype = TIPC_ADDR_NAMESEQ;
	server_addr.addr.nameseq.type = server_type;
	server_addr.addr.nameseq.lower = server_inst;
	server_addr.addr.nameseq.upper = server_inst;

	if (0 != bind(tipc_cb.BSRsock, (struct sockaddr *)&server_addr,
		      sizeof(server_addr))) {
		m_MDS_LOG_ERR("MDTM: SVC-UNINSTALL Failure err :%s \n",
			      strerror(errno));
		return NCSCC_RC_FAILURE;
	}
	m_MDS_LOG_NOTIFY("MDTM: uninstall_tipc : svc_id = %s(%d),vdest_id = %d",
			 get_svc_names(svc_id), svc_id, vdest_id);
	m_MDS_LOG_INFO("MDTM: SVC-UNINSTALL Success\n");
	return NCSCC_RC_SUCCESS;
}

/*********************************************************

  Function NAME: mds_mdtm_svc_subscribe_tipc

  DESCRIPTION:

  ARGUMENTS:

  RETURNS:  1 - NCSCC_RC_SUCCESS
	    2 - NCSCC_RC_FAILURE

*********************************************************/

uint32_t mds_mdtm_svc_subscribe_tipc(PW_ENV_ID pwe_id, MDS_SVC_ID svc_id,
				     NCSMDS_SCOPE_TYPE install_scope,
				     MDS_SVC_HDL svc_hdl,
				     MDS_SUBTN_REF_VAL *subtn_ref_val)
{
	/*
	   STEP 1: Subscribe to socket DSock with TIPC name sequence
	   TIPC Name:
	   <MDS-prefix, SVC_INST, svcid>
	   TIPC Range:
	   <pwe, scope,ROLE=0, 0x0000>
	   to
	   < pwe, scope, ROLE=1, 0xffff >
	   STEP 2: Fill the ref val

	 */
	uint32_t server_type = 0, status = 0;
	struct tipc_subscr subscr;
	pwe_id = pwe_id << MDS_EVENT_SHIFT_FOR_PWE;
	svc_id = svc_id & MDS_EVENT_MASK_FOR_SVCID;

	if (num_subscriptions > MAX_SUBSCRIPTIONS) {
		m_MDS_LOG_ERR(
		    "MDTM: SYSTEM CRITICAL Crossing =%d subscriptions\n",
		    num_subscriptions);
		if (num_subscriptions > MAX_SUBSCRIPTIONS_RETURN_ERROR) {
			m_MDS_LOG_ERR(
			    "MDTM: SYSTEM has crossed the max =%d subscriptions , Returning failure to the user",
			    MAX_SUBSCRIPTIONS_RETURN_ERROR);
			return NCSCC_RC_FAILURE;
		}
	}

	server_type =
	    server_type | MDS_TIPC_PREFIX | MDS_SVC_INST_TYPE | pwe_id | svc_id;

	memset(&subscr, 0, sizeof(subscr));
	subscr.seq.type = HTONL(server_type);
	subscr.seq.lower = HTONL(0x00000000);
	subscr.seq.upper = HTONL(0xffffffff);
	subscr.timeout = HTONL(FOREVER);
	subscr.filter = HTONL(TIPC_SUB_PORTS);
	*subtn_ref_val = 0;
	*subtn_ref_val = ++handle;
	*((uint64_t *)subscr.usr_handle) = *subtn_ref_val;

	if (send(tipc_cb.Dsock, &subscr, sizeof(subscr), 0) != sizeof(subscr)) {
		m_MDS_LOG_ERR("MDTM: SVC-SUBSCRIBE Failure err :%s",
			      strerror(errno));
		return NCSCC_RC_FAILURE;
	}

	status = mdtm_add_to_ref_tbl(svc_hdl, *subtn_ref_val);

	++num_subscriptions;
	m_MDS_LOG_INFO("MDTM: SVC-SUBSCRIBE Success\n");

	return status;
}

/*********************************************************

  Function NAME: mds_mdtm_node_subscribe_tipc

  DESCRIPTION: Subscription to Node UP and DOWN events

  ARGUMENTS:

  RETURNS:  1 - NCSCC_RC_SUCCESS
	    2 - NCSCC_RC_FAILURE

*********************************************************/

uint32_t mds_mdtm_node_subscribe_tipc(MDS_SVC_HDL svc_hdl,
				      MDS_SUBTN_REF_VAL *subtn_ref_val)
{
	struct tipc_subscr net_subscr;
	uint32_t status = NCSCC_RC_SUCCESS;

	m_MDS_LOG_INFO("MDTM: In mds_mdtm_node_subscribe_tipc\n");
	memset(&net_subscr, 0, sizeof(net_subscr));
	net_subscr.seq.type = HTONL(0);
	net_subscr.seq.lower = HTONL(0);
	net_subscr.seq.upper = HTONL(~0);
	net_subscr.timeout = HTONL(FOREVER);
	net_subscr.filter = HTONL(TIPC_SUB_PORTS);
	*subtn_ref_val = 0;
	*subtn_ref_val = ++handle;
	*((uint64_t *)net_subscr.usr_handle) = *subtn_ref_val;

	if (send(tipc_cb.Dsock, &net_subscr, sizeof(net_subscr), 0) !=
	    sizeof(net_subscr)) {
		syslog(LOG_ERR,
		       "MDS:MDTM: failed to send network subscription err :%s",
		       strerror(errno));
		m_MDS_LOG_ERR("MDTM: NODE-SUBSCRIBE Failure\n");
		return NCSCC_RC_FAILURE;
	}

	status = mdtm_add_to_ref_tbl(svc_hdl, *subtn_ref_val);
	++num_subscriptions;
	m_MDS_LOG_INFO("MDTM: NODE-SUBSCRIBE Success\n");

	return status;
}

/*********************************************************

  Function NAME: mds_mdtm_node_unsubscribe_tipc

  DESCRIPTION:

  ARGUMENTS:

  RETURNS:  1 - NCSCC_RC_SUCCESS
	    2 - NCSCC_RC_FAILURE

*********************************************************/
uint32_t mds_mdtm_node_unsubscribe_tipc(MDS_SUBTN_REF_VAL subtn_ref_val)
{
#ifdef TIPC_SUB_CANCEL
	struct tipc_subscr net_subscr;

	memset(&net_subscr, 0, sizeof(net_subscr));
	net_subscr.seq.type = HTONL(0);
	net_subscr.seq.lower = HTONL(0);
	net_subscr.seq.upper = HTONL(~0);
	net_subscr.timeout = HTONL(FOREVER);
	net_subscr.filter = HTONL(TIPC_SUB_CANCEL | TIPC_SUB_PORTS);
	*((uint64_t *)net_subscr.usr_handle) = subtn_ref_val;

	if (send(tipc_cb.Dsock, &net_subscr, sizeof(net_subscr), 0) !=
	    sizeof(net_subscr)) {
		perror("MDS:MDTM: failed to send network subscription");
		m_MDS_LOG_ERR("MDTM: NODE-UNSUBSCRIBE Failure\n");
		return NCSCC_RC_FAILURE;
	}
	--num_subscriptions;
#endif
	m_MDS_LOG_INFO("MDTM: In mds_mdtm_node_unsubscribe_tipc\n");
	/* Presently TIPC doesnt supports the unsubscribe */
	mdtm_del_from_ref_tbl(subtn_ref_val);
	m_MDS_LOG_INFO("MDTM: NODE-UNSUBSCRIBE Success\n");
	return NCSCC_RC_SUCCESS;
}

/*********************************************************

  Function NAME: mds_mdtm_svc_unsubscribe_tipc

  DESCRIPTION:

  ARGUMENTS:

  RETURNS:  1 - NCSCC_RC_SUCCESS
	    2 - NCSCC_RC_FAILURE

*********************************************************/
uint32_t mds_mdtm_svc_unsubscribe_tipc(PW_ENV_ID pwe_id, MDS_SVC_ID svc_id,
				       NCSMDS_SCOPE_TYPE install_scope,
				       MDS_SUBTN_REF_VAL subtn_ref_val)
{
/*
   STEP 1: Get ref_val and call the TIPC unsubscribe with the ref_val
 */
#ifdef TIPC_SUB_CANCEL
	uint32_t server_type = 0;
	struct tipc_subscr subscr;
	pwe_id = pwe_id << MDS_EVENT_SHIFT_FOR_PWE;
	svc_id = svc_id & MDS_EVENT_MASK_FOR_SVCID;

	server_type =
	    server_type | MDS_TIPC_PREFIX | MDS_SVC_INST_TYPE | pwe_id | svc_id;

	memset(&subscr, 0, sizeof(subscr));
	subscr.seq.type = HTONL(server_type);
	subscr.seq.lower = HTONL(0x00000000);
	subscr.seq.upper = HTONL(0xffffffff);
	subscr.timeout = HTONL(FOREVER);
	subscr.filter = HTONL(TIPC_SUB_CANCEL | TIPC_SUB_PORTS);
	*((uint64_t *)subscr.usr_handle) = subtn_ref_val;

	if (send(tipc_cb.Dsock, &subscr, sizeof(subscr), 0) != sizeof(subscr)) {
		m_MDS_LOG_ERR("MDTM: SVC-UNSUBSCRIBE Failure\n");
		return NCSCC_RC_FAILURE;
	}
	--num_subscriptions;
#endif
	mdtm_del_from_ref_tbl(subtn_ref_val);

	/* Presently 1.5 doesnt supports the unsubscribe */
	m_MDS_LOG_INFO("MDTM: SVC-UNSUBSCRIBE Success\n");

	return NCSCC_RC_SUCCESS;
}

/*********************************************************
  Function NAME: mds_mdtm_vdest_install_tipc

  DESCRIPTION:

  ARGUMENTS:

  RETURNS:  1 - NCSCC_RC_SUCCESS
	    2 - NCSCC_RC_FAILURE

*********************************************************/

uint32_t mds_mdtm_vdest_install_tipc(MDS_VDEST_ID vdest_id)
{
	/*
	   STEP 1: Bind to socket BSRSock with Tipc name sequence
	   TIPC Name:
	   <MDS-prefix, VDEST_INST_TYPE, 0>
	   TIPC Range:
	   <0,ROLE=0,POLICY=0,VDEST_ID > to
	   <0,ROLE=0,POLICY=0,VDEST_ID >
	 */
	struct sockaddr_tipc server_addr;
	uint32_t server_type = 0, server_inst = 0;

	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.family = AF_TIPC;

	server_addr.addrtype = TIPC_ADDR_NAMESEQ;

	server_type = server_type | MDS_TIPC_PREFIX | MDS_VDEST_INST_TYPE;
	server_inst |= vdest_id;

	server_addr.addr.nameseq.type = server_type;
	server_addr.addr.nameseq.lower = server_inst;
	server_addr.addr.nameseq.upper = server_inst;
	server_addr.scope = TIPC_CLUSTER_SCOPE;

	if (0 != bind(tipc_cb.BSRsock, (struct sockaddr *)&server_addr,
		      sizeof(server_addr))) {
		m_MDS_LOG_ERR("MDTM: VDEST-INSTALL Failure err :%s\n",
			      strerror(errno));
		return NCSCC_RC_FAILURE;
	}
	m_MDS_LOG_INFO("MDTM: VDEST-INSTALL Success\n");
	return NCSCC_RC_SUCCESS;
}

/*********************************************************

  Function NAME: mds_mdtm_vdest_uninstall_tipc

  DESCRIPTION:

  ARGUMENTS:

  RETURNS:  1 - NCSCC_RC_SUCCESS
	    2 - NCSCC_RC_FAILURE

*********************************************************/

uint32_t mds_mdtm_vdest_uninstall_tipc(MDS_VDEST_ID vdest_id)
{
	/*
	   STEP 1: Unbind to socket BSRSock with Tipc name sequence
	   TIPC Name:
	   <MDS-prefix, VDEST_INST_TYPE, 0>
	   TIPC Range:
	   <0,ROLE=0,POLICY=0,VDEST_ID > to
	   <0,ROLE=0,POLICY=0,VDEST_ID >
	 */

	uint32_t server_inst = 0, server_type = 0;
	struct sockaddr_tipc server_addr;
	memset(&server_addr, 0, sizeof(server_addr));

	server_type = server_type | MDS_TIPC_PREFIX | MDS_VDEST_INST_TYPE;
	server_inst |= vdest_id;

	server_addr.family = AF_TIPC;
	server_addr.addrtype = TIPC_ADDR_NAMESEQ;
	server_addr.addr.nameseq.type = server_type;
	server_addr.addr.nameseq.lower = server_inst;
	server_addr.addr.nameseq.upper = server_inst;
	server_addr.scope = -TIPC_CLUSTER_SCOPE;
	if (0 != bind(tipc_cb.BSRsock, (struct sockaddr *)&server_addr,
		      sizeof(server_addr))) {
		m_MDS_LOG_ERR("MDTM: VDEST-UNINSTALL Failure err :%s\n",
			      strerror(errno));
		return NCSCC_RC_FAILURE;
	}

	m_MDS_LOG_INFO("MDTM: VDEST-UNINSTALL Success\n");
	return NCSCC_RC_SUCCESS;
}

/*********************************************************

  Function NAME: mds_mdtm_vdest_subscribe_tipc

  DESCRIPTION:

  ARGUMENTS:

  RETURNS:  1 - NCSCC_RC_SUCCESS
	    2 - NCSCC_RC_FAILURE

*********************************************************/

uint32_t mds_mdtm_vdest_subscribe_tipc(MDS_VDEST_ID vdest_id,
				       MDS_SUBTN_REF_VAL *subtn_ref_val)
{
	/*
	   STEP 1: Subscribe to socket DSock with Tipc name sequence
	   TIPC Name:
	   <MDS-prefix, VDEST_INST_TYPE, 0>
	   TIPC Range:
	   <0,ROLE=0,POLICY=0,VDEST_ID > to
	   <0,ROLE=0,POLICY=0,VDEST_ID >
	 */

	uint32_t inst = 0, server_type = 0;
	struct tipc_subscr subscr;

	if (num_subscriptions > MAX_SUBSCRIPTIONS) {
		m_MDS_LOG_ERR(
		    "MDTM: SYSTEM CRITICAL Crossing =%d subscriptions\n",
		    num_subscriptions);
		if (num_subscriptions > MAX_SUBSCRIPTIONS_RETURN_ERROR) {
			m_MDS_LOG_ERR(
			    "MDTM: SYSTEM has crossed the max =%d subscriptions , Returning failure to the user",
			    MAX_SUBSCRIPTIONS_RETURN_ERROR);
			return NCSCC_RC_FAILURE;
		}
	}

	server_type = server_type | MDS_TIPC_PREFIX | MDS_VDEST_INST_TYPE;
	inst |= vdest_id;
	memset(&subscr, 0, sizeof(subscr));
	subscr.seq.type = HTONL(server_type);
	subscr.seq.lower = HTONL(inst);
	subscr.seq.upper = HTONL(inst);
	subscr.timeout = HTONL(FOREVER);
	subscr.filter = HTONL(TIPC_SUB_PORTS);
	*subtn_ref_val = 0;
	*subtn_ref_val = ++handle;
	*((uint64_t *)subscr.usr_handle) = *subtn_ref_val;

	if (send(tipc_cb.Dsock, &subscr, sizeof(subscr), 0) != sizeof(subscr)) {
		m_MDS_LOG_ERR("MDTM: VDEST-SUBSCRIBE Failure\n");
		return NCSCC_RC_FAILURE;
	}
	++num_subscriptions;

	m_MDS_LOG_INFO("MDTM: VDEST-SUBSCRIBE Success\n");

	return NCSCC_RC_SUCCESS;
}

/*********************************************************

  Function NAME: mds_mdtm_vdest_unsubscribe_tipc

  DESCRIPTION:

  ARGUMENTS:

  RETURNS:  1 - NCSCC_RC_SUCCESS
	    2 - NCSCC_RC_FAILURE

*********************************************************/

uint32_t mds_mdtm_vdest_unsubscribe_tipc(MDS_VDEST_ID vdest_id,
					 MDS_SUBTN_REF_VAL subtn_ref_val)
{
/*
   STEP 1: Get ref_val and call the TIPC unsubscribe with the ref_val
 */
/* Fix me, Presently unsupported */
/* Presently 1.5 doesnt supports the unsubscribe */
#ifdef TIPC_SUB_CANCEL
	uint32_t inst = 0, server_type = 0;
	struct tipc_subscr subscr;

	server_type = server_type | MDS_TIPC_PREFIX | MDS_VDEST_INST_TYPE;
	inst |= vdest_id;
	memset(&subscr, 0, sizeof(subscr));
	subscr.seq.type = HTONL(server_type);
	subscr.seq.lower = HTONL(inst);
	subscr.seq.upper = HTONL(inst);
	subscr.timeout = HTONL(FOREVER);
	subscr.filter = HTONL(TIPC_SUB_CANCEL | TIPC_SUB_PORTS);
	*((uint64_t *)subscr.usr_handle) = subtn_ref_val;

	if (send(tipc_cb.Dsock, &subscr, sizeof(subscr), 0) != sizeof(subscr)) {
		m_MDS_LOG_ERR("MDTM: VDEST-UNSUBSCRIBE Failure\n");
		return NCSCC_RC_FAILURE;
	}
	--num_subscriptions;
#endif
	m_MDS_LOG_INFO("MDTM: VDEST-UNSUBSCRIBE Success\n");

	return NCSCC_RC_SUCCESS;
}

/*********************************************************

  Function NAME: mds_mdtm_tx_hdl_register_tipc

  DESCRIPTION:

  ARGUMENTS:

  RETURNS:  1 - NCSCC_RC_SUCCESS
	    2 - NCSCC_RC_FAILURE

*********************************************************/
/* Tx Register (For incrementing the use count) */
uint32_t mds_mdtm_tx_hdl_register_tipc(MDS_DEST adest)
{
	/*
	   STEP 1: Check whether there is any TIPC_ID corresponding
	   to the passed ADEST in the ADEST table
	   if present
	   increment the use count in the ADEST table
	   else
	   return failure
	 */
	return NCSCC_RC_SUCCESS;
}

/*********************************************************

  Function NAME: mds_mdtm_tx_hdl_unregister_tipc

  DESCRIPTION:

  ARGUMENTS:

  RETURNS:  1 - NCSCC_RC_SUCCESS
	    2 - NCSCC_RC_FAILURE

*********************************************************/
/* Tx Unregister (For decrementing the use count) */
uint32_t mds_mdtm_tx_hdl_unregister_tipc(MDS_DEST adest)
{
	/*
	   STEP 1: Check whether there is any TIPC_ID corresponding to the
	   passed ADEST in the ADEST table if present decrement the use count in
	   the ADEST table if the use count is zero delete the Entry in the
	   ADEST table else return failure
	 */

	return NCSCC_RC_SUCCESS;
}

/*********************************************************

  Function NAME: mds_mdtm_send_tipc

  DESCRIPTION:

  ARGUMENTS:

  RETURNS:  1 - NCSCC_RC_SUCCESS
	    2 - NCSCC_RC_FAILURE

*********************************************************/

/* Send messages to the destination */

uint32_t mds_mdtm_send_tipc(MDTM_SEND_REQ *req)
{
	/*
	   STEP 1: Get the TIPC_ID from the ADEST present in the recd structure
	   STEP 2: Get the total length of the message to be sent,
	   if len is greater than a single frag size,
	   fragment the message
	   Add fragment hdr
	   convert the msg into flat buffer
	   send message
	 */
	uint32_t status = 0;
	uint32_t sum_mds_hdr_plus_mdtm_hdr_plus_len;
	int version = req->msg_arch_word & 0x7;
	if (version > 1) {
		sum_mds_hdr_plus_mdtm_hdr_plus_len =
		    (SUM_MDS_HDR_PLUS_MDTM_HDR_PLUS_LEN +
		     gl_mds_mcm_cb->node_name_len);
	} else {
		/* sending message to Old version Node	*/
		sum_mds_hdr_plus_mdtm_hdr_plus_len =
		    (SUM_MDS_HDR_PLUS_MDTM_HDR_PLUS_LEN - 1);
	}

	if (req->to == DESTINATION_SAME_PROCESS) {
		MDS_DATA_RECV recv;
		memset(&recv, 0, sizeof(recv));

		recv.dest_svc_hdl = (MDS_SVC_HDL)
		    m_MDS_GET_SVC_HDL_FROM_PWE_ID_VDEST_ID_AND_SVC_ID(
			req->dest_pwe_id, req->dest_vdest_id, req->dest_svc_id);
		recv.src_svc_id = req->src_svc_id;
		recv.src_pwe_id = req->src_pwe_id;
		recv.src_vdest = req->src_vdest_id;
		recv.exchange_id = req->xch_id;
		recv.src_adest = tipc_cb.adest;
		recv.snd_type = req->snd_type;
		recv.msg = req->msg;
		recv.pri = req->pri;
		recv.msg_fmt_ver = req->msg_fmt_ver;
		recv.src_svc_sub_part_ver = req->src_svc_sub_part_ver;

		strncpy((char *)recv.src_node_name,
			(char *)gl_mds_mcm_cb->node_name,
			gl_mds_mcm_cb->node_name_len);
		/* This is exclusively for the Bcast ENC and ENC_FLAT case */
		if (recv.msg.encoding == MDS_ENC_TYPE_FULL) {
			ncs_dec_init_space(&recv.msg.data.fullenc_uba,
					   recv.msg.data.fullenc_uba.start);
			recv.msg_arch_word = req->msg_arch_word;
		} else if (recv.msg.encoding == MDS_ENC_TYPE_FLAT) {
			/* This case will not arise, but just to be on safe side
			 */
			ncs_dec_init_space(&recv.msg.data.flat_uba,
					   recv.msg.data.flat_uba.start);
		} else {
			/* Do nothing for the DIrect buff and Copy case */
		}

		status = mds_mcm_ll_data_rcv(&recv);

		return status;

	} else {
		struct tipc_portid tipc_id;
		USRBUF *usrbuf;

		uint32_t frag_seq_num = 0, node_status = 0;

		node_status = m_MDS_CHECK_NCS_NODE_ID_RANGE(
		    m_MDS_GET_NODE_ID_FROM_ADEST(req->adest));

		if (NCSCC_RC_SUCCESS == node_status) {
			tipc_id.node = m_MDS_GET_TIPC_NODE_ID_FROM_NCS_NODE_ID(
			    m_MDS_GET_NODE_ID_FROM_ADEST(req->adest));
			tipc_id.ref = (uint32_t)(req->adest);
		} else {
			if (req->snd_type !=
			    MDS_SENDTYPE_ACK) { /* This check is becoz in ack
						   cases we are only sending the
						   hdr and no data part is being
						   send, so no message free ,
						   fix me */
				mdtm_free_reassem_msg_mem(&req->msg);
			}
			return NCSCC_RC_FAILURE;
		}

		frag_seq_num = ++mdtm_global_frag_num;

		/* Only for the ack and not for any other message */
		if (req->snd_type == MDS_SENDTYPE_ACK ||
		    req->snd_type == MDS_SENDTYPE_RACK) {
			uint8_t len = sum_mds_hdr_plus_mdtm_hdr_plus_len;
			uint8_t buffer_ack[len];

			/* Add mds_hdr */
			if (NCSCC_RC_SUCCESS !=
			    mdtm_add_mds_hdr(buffer_ack, req)) {
				return NCSCC_RC_FAILURE;
			}

			/* Add frag_hdr */
			if (NCSCC_RC_SUCCESS !=
			    mdtm_add_frag_hdr(buffer_ack, len, frag_seq_num,
					      0)) {
				return NCSCC_RC_FAILURE;
			}

			m_MDS_LOG_DBG(
			    "MDTM:Sending message with Service Seqno=%d, TO Dest_Tipc_id=<0x%08x:%u> ",
			    req->svc_seq_num, tipc_id.node, tipc_id.ref);
			return mdtm_sendto(buffer_ack, len, tipc_id);
		}

		if (MDS_ENC_TYPE_FLAT == req->msg.encoding) {
			usrbuf = req->msg.data.flat_uba.start;
		} else if (MDS_ENC_TYPE_FULL == req->msg.encoding) {
			usrbuf = req->msg.data.fullenc_uba.start;
		} else {
			usrbuf = NULL; /* This is because, usrbuf is used only
					  in the above two cases and if it has
					  come here, it means, it is a direct
					  send. Direct send will not use the
					  USRBUF */
		}

		switch (req->msg.encoding) {
		case MDS_ENC_TYPE_CPY:
			/* will not present here */
			break;

		case MDS_ENC_TYPE_FLAT:
		case MDS_ENC_TYPE_FULL: {
			uint32_t len = 0;
			len = m_MMGR_LINK_DATA_LEN(
			    usrbuf); /* Getting total len */

			m_MDS_LOG_INFO(
			    "MDTM: User Sending Data lenght=%d From svc_id = %s(%d) to svc_id = %s(%d)\n",
			    len, get_svc_names(req->src_svc_id),
			    req->src_svc_id, get_svc_names(req->dest_svc_id),
			    req->dest_svc_id);

			// determine fragment limit using a bit in destination
			// archword
			int frag_size;
			if (version > 0) {
				// normal mode, use TIPC fragmentation
				frag_size = MDS_DIRECT_BUF_MAXSIZE;
			} else {
				// old mode, use some TIPC fragmentation but not
				// full capabilities
				frag_size = MDTM_NORMAL_MSG_FRAG_SIZE;
			}

			if (len > frag_size) {
				/* Packet needs to be fragmented and send */
				m_MDS_LOG_DBG(
				    "MDTM: User fragment and Sending Data lenght=%d From svc_id = %s(%d) to svc_id = %s(%d)\n",
				    len, get_svc_names(req->src_svc_id),
				    req->src_svc_id,
				    get_svc_names(req->dest_svc_id),
				    req->dest_svc_id);
				return mdtm_frag_and_send(req, frag_seq_num,
							  tipc_id, frag_size);
			} else {
				uint8_t *p8;
				uint8_t *body = NULL;
				body = calloc(
				    1,
				    len + sum_mds_hdr_plus_mdtm_hdr_plus_len);

				p8 = (uint8_t *)m_MMGR_DATA_AT_START(
				    usrbuf, len,
				    (char
					 *)(body +
					    sum_mds_hdr_plus_mdtm_hdr_plus_len));

				if (p8 !=
				    (body + sum_mds_hdr_plus_mdtm_hdr_plus_len))
					memcpy(
					    (body +
					     sum_mds_hdr_plus_mdtm_hdr_plus_len),
					    p8, len);

				if (NCSCC_RC_SUCCESS !=
				    mdtm_add_mds_hdr(body, req)) {
					m_MDS_LOG_ERR(
					    "MDTM: Unable to add the mds Hdr to the send msg\n");
					m_MMGR_FREE_BUFR_LIST(usrbuf);
					free(body);
					return NCSCC_RC_FAILURE;
				}

				if (NCSCC_RC_SUCCESS !=
				    mdtm_add_frag_hdr(
					body,
					(len +
					 sum_mds_hdr_plus_mdtm_hdr_plus_len),
					frag_seq_num, 0)) {
					m_MDS_LOG_ERR(
					    "MDTM: Unable to add the frag Hdr to the send msg\n");
					m_MMGR_FREE_BUFR_LIST(usrbuf);
					free(body);
					return NCSCC_RC_FAILURE;
				}

				m_MDS_LOG_DBG(
				    "MDTM:Sending message with Service Seqno=%d, TO Dest_Tipc_id=<0x%08x:%u> ",
				    req->svc_seq_num, tipc_id.node,
				    tipc_id.ref);

				len += sum_mds_hdr_plus_mdtm_hdr_plus_len;
				if (((req->snd_type == MDS_SENDTYPE_RBCAST) ||
				     (req->snd_type == MDS_SENDTYPE_BCAST)) &&
				    (version > 0) && (tipc_mcast_enabled)) {
					m_MDS_LOG_DBG(
					    "MDTM: User Sending Multicast Data lenght=%d From svc_id = %s(%d) to svc_id = %s(%d)\n",
					    len, get_svc_names(req->src_svc_id),
					    req->src_svc_id,
					    get_svc_names(req->dest_svc_id),
					    req->dest_svc_id);
					if (len - sum_mds_hdr_plus_mdtm_hdr_plus_len > MDS_DIRECT_BUF_MAXSIZE) {
						m_MMGR_FREE_BUFR_LIST(usrbuf);
						free(body);
						LOG_NO(
						    "MDTM: Not possible to send size:%d TIPC multicast to svc_id = %s(%d)",
						    len,
						    get_svc_names(
							req->dest_svc_id),
						    req->dest_svc_id);
						return NCSCC_RC_FAILURE;
					}
					if (NCSCC_RC_SUCCESS !=
					    mdtm_mcast_sendto(body, len, req)) {
						m_MDS_LOG_ERR(
						    "MDTM: Failed to send Multicast message Data lenght=%d "
						    "From svc_id = %s(%d) to svc_id = %s(%d) err :%s",
						    len,
						    get_svc_names(
							req->src_svc_id),
						    req->src_svc_id,
						    get_svc_names(
							req->dest_svc_id),
						    req->dest_svc_id,
						    strerror(errno));
						m_MMGR_FREE_BUFR_LIST(usrbuf);
						free(body);
						return NCSCC_RC_FAILURE;
					}
				} else {
					if (NCSCC_RC_SUCCESS !=
					    mdtm_sendto(body, len, tipc_id)) {
						m_MDS_LOG_ERR(
						    "MDTM: Unable to send the msg thru TIPC\n");
						m_MMGR_FREE_BUFR_LIST(usrbuf);
						free(body);
						return NCSCC_RC_FAILURE;
					}
				}
				m_MMGR_FREE_BUFR_LIST(usrbuf);
				free(body);
				return NCSCC_RC_SUCCESS;
			}
		} break;

		case MDS_ENC_TYPE_DIRECT_BUFF: {
			if (req->msg.data.buff_info.len >
			    (MDTM_MAX_DIRECT_BUFF_SIZE -
			     sum_mds_hdr_plus_mdtm_hdr_plus_len)) {
				m_MDS_LOG_CRITICAL(
				    "MDTM: Passed pkt len is more than the single send direct buff\n");
				mds_free_direct_buff(
				    req->msg.data.buff_info.buff);
				return NCSCC_RC_FAILURE;
			}

			m_MDS_LOG_INFO(
			    "MDTM: User Sending Data len=%d From svc_id = %s(%d) to svc_id = %s(%d)\n",
			    req->msg.data.buff_info.len,
			    get_svc_names(req->src_svc_id), req->src_svc_id,
			    get_svc_names(req->dest_svc_id), req->dest_svc_id);

			uint8_t *body = NULL;
			body = calloc(1, (req->msg.data.buff_info.len +
					  sum_mds_hdr_plus_mdtm_hdr_plus_len));

			if (NCSCC_RC_SUCCESS != mdtm_add_mds_hdr(body, req)) {
				m_MDS_LOG_ERR(
				    "MDTM: Unable to add the mds Hdr to the send msg\n");
				free(body);
				mds_free_direct_buff(
				    req->msg.data.buff_info.buff);
				return NCSCC_RC_FAILURE;
			}
			if (NCSCC_RC_SUCCESS !=
			    mdtm_add_frag_hdr(
				body,
				req->msg.data.buff_info.len +
				    sum_mds_hdr_plus_mdtm_hdr_plus_len,
				frag_seq_num, 0)) {
				m_MDS_LOG_ERR(
				    "MDTM: Unable to add the frag Hdr to the send msg\n");
				free(body);
				mds_free_direct_buff(
				    req->msg.data.buff_info.buff);
				return NCSCC_RC_FAILURE;
			}
			memcpy(&body[sum_mds_hdr_plus_mdtm_hdr_plus_len],
			       req->msg.data.buff_info.buff,
			       req->msg.data.buff_info.len);

			if (NCSCC_RC_SUCCESS !=
			    mdtm_sendto(body,
					(req->msg.data.buff_info.len +
					 sum_mds_hdr_plus_mdtm_hdr_plus_len),
					tipc_id)) {
				m_MDS_LOG_ERR(
				    "MDTM: Unable to send the msg thru TIPC\n");
				free(body);
				mds_free_direct_buff(
				    req->msg.data.buff_info.buff);
				return NCSCC_RC_FAILURE;
			}

			/* If Direct Send is bcast it will be done at bcast
			 * function */
			if (req->snd_type == MDS_SENDTYPE_BCAST ||
			    req->snd_type == MDS_SENDTYPE_RBCAST) {
				/* Dont free Here */
			} else {
				mds_free_direct_buff(
				    req->msg.data.buff_info.buff);
			}
			free(body);
			return NCSCC_RC_SUCCESS;
		} break;

		default:
			m_MDS_LOG_ERR("MDTM: Encoding type not supported\n");
			return NCSCC_RC_SUCCESS;
			break;
		}
		return NCSCC_RC_SUCCESS;
	}
	return NCSCC_RC_FAILURE;
}

/*********************************************************

  Function NAME: mdtm_frag_and_send

  DESCRIPTION:

  ARGUMENTS:

  RETURNS:  1 - NCSCC_RC_SUCCESS
	    2 - NCSCC_RC_FAILURE

*********************************************************/

#ifdef MDS_CHECKSUM_ENABLE_FLAG
#define MDTM_FRAG_HDR_PLUS_LEN_2 13
#else
#define MDTM_FRAG_HDR_PLUS_LEN_2 10
#endif

uint32_t mdtm_frag_and_send(MDTM_SEND_REQ *req, uint32_t seq_num,
			    struct tipc_portid id, int frag_size)
{
	USRBUF *usrbuf;
	uint32_t len = 0;
	uint16_t len_buf = 0;
	uint8_t *p8;
	uint16_t i = 1;
	uint16_t frag_val = 0;
	uint32_t sum_mds_hdr_plus_mdtm_hdr_plus_len;
	int version = req->msg_arch_word & 0x7;

	if (version > 1) {
		sum_mds_hdr_plus_mdtm_hdr_plus_len =
		    (SUM_MDS_HDR_PLUS_MDTM_HDR_PLUS_LEN +
		     gl_mds_mcm_cb->node_name_len);
	} else {
		sum_mds_hdr_plus_mdtm_hdr_plus_len =
		    (SUM_MDS_HDR_PLUS_MDTM_HDR_PLUS_LEN - 1);
	}

	int max_send_pkt_size = frag_size + sum_mds_hdr_plus_mdtm_hdr_plus_len;

	switch (req->msg.encoding) {
	case MDS_ENC_TYPE_FULL:
		usrbuf = req->msg.data.fullenc_uba.start;
		break;

	case MDS_ENC_TYPE_FLAT:
		usrbuf = req->msg.data.flat_uba.start;
		break;

	default:
		return NCSCC_RC_SUCCESS;
	}

	len = m_MMGR_LINK_DATA_LEN(usrbuf); /* Getting total len */

	/* We have 15 bits for frag number so 2( pow 15) -1=32767 */
	if (len > (32767 * frag_size)) {
		m_MDS_LOG_CRITICAL(
		    "MDTM: App. is trying to send data more than MDTM Can fragment "
		    "and send, Max size is =%d\n",
		    32767 * frag_size);
		m_MMGR_FREE_BUFR_LIST(usrbuf);
		return NCSCC_RC_FAILURE;
	}

	while (len != 0) {
		if (len > frag_size) {
			if (i == 1) {
				len_buf = max_send_pkt_size;
				frag_val = MORE_FRAG_BIT | i;
			} else {
				if ((len + MDTM_FRAG_HDR_PLUS_LEN_2) >
				    max_send_pkt_size) {
					len_buf = max_send_pkt_size;
					frag_val = MORE_FRAG_BIT | i;
				} else {
					len_buf =
					    len + MDTM_FRAG_HDR_PLUS_LEN_2;
					frag_val = NO_FRAG_BIT | i;
				}
			}
		} else {
			len_buf = len + MDTM_FRAG_HDR_PLUS_LEN_2;
			frag_val = NO_FRAG_BIT | i;
		}
		{
			uint8_t *body = NULL;
			body = calloc(1, len_buf);
			if (i == 1) {
				p8 = (uint8_t *)m_MMGR_DATA_AT_START(
				    usrbuf,
				    (len_buf -
				     sum_mds_hdr_plus_mdtm_hdr_plus_len),
				    (char
					 *)(body +
					    sum_mds_hdr_plus_mdtm_hdr_plus_len));

				if (p8 !=
				    (body + sum_mds_hdr_plus_mdtm_hdr_plus_len))
					memcpy(
					    (body +
					     sum_mds_hdr_plus_mdtm_hdr_plus_len),
					    p8,
					    (len_buf -
					     sum_mds_hdr_plus_mdtm_hdr_plus_len));

				if (NCSCC_RC_SUCCESS !=
				    mdtm_add_mds_hdr(body, req)) {
					m_MDS_LOG_ERR(
					    "MDTM: frg MDS hdr addition failed\n");
					free(body);
					m_MMGR_FREE_BUFR_LIST(usrbuf);
					return NCSCC_RC_FAILURE;
				}

				if (NCSCC_RC_SUCCESS !=
				    mdtm_add_frag_hdr(body, len_buf, seq_num,
						      frag_val)) {
					m_MDS_LOG_ERR(
					    "MDTM: Frag hdr addition failed\n");
					m_MMGR_FREE_BUFR_LIST(usrbuf);
					free(body);
					return NCSCC_RC_FAILURE;
				}
				m_MDS_LOG_DBG(
				    "MDTM:Sending message with Service Seqno=%d, Fragment Seqnum=%d, frag_num=%d, TO Dest_Tipc_id=<0x%08x:%u>",
				    req->svc_seq_num, seq_num, frag_val,
				    id.node, id.ref);
				mdtm_sendto(body, len_buf, id);
				m_MMGR_REMOVE_FROM_START(
				    &usrbuf,
				    len_buf -
					sum_mds_hdr_plus_mdtm_hdr_plus_len);
				free(body);
				len =
				    len - (len_buf -
					   sum_mds_hdr_plus_mdtm_hdr_plus_len);
			} else {
				p8 = (uint8_t *)m_MMGR_DATA_AT_START(
				    usrbuf, len_buf - MDTM_FRAG_HDR_PLUS_LEN_2,
				    (char *)(body + MDTM_FRAG_HDR_PLUS_LEN_2));
				if (p8 != (body + MDTM_FRAG_HDR_PLUS_LEN_2))
					memcpy(
					    (body + MDTM_FRAG_HDR_PLUS_LEN_2),
					    p8,
					    len_buf - MDTM_FRAG_HDR_PLUS_LEN_2);

				if (NCSCC_RC_SUCCESS !=
				    mdtm_add_frag_hdr(body, len_buf, seq_num,
						      frag_val)) {
					m_MDS_LOG_ERR(
					    "MDTM: Frag hde addition failed\n");
					m_MMGR_FREE_BUFR_LIST(usrbuf);
					free(body);
					return NCSCC_RC_FAILURE;
				}
				m_MDS_LOG_DBG(
				    "MDTM:Sending message with Service Seqno=%d, Fragment Seqnum=%d, frag_num=%d, TO Dest_Tipc_id=<0x%08x:%u>",
				    req->svc_seq_num, seq_num, frag_val,
				    id.node, id.ref);
				mdtm_sendto(body, len_buf, id);
				m_MMGR_REMOVE_FROM_START(
				    &usrbuf,
				    (len_buf - MDTM_FRAG_HDR_PLUS_LEN_2));
				free(body);
				len =
				    len - (len_buf - MDTM_FRAG_HDR_PLUS_LEN_2);
				if (len == 0)
					break;
			}
		}
		i++;
		frag_val = 0;
	}
	return NCSCC_RC_SUCCESS;
}

/*********************************************************

  Function NAME: mdtm_add_frag_hdr

  DESCRIPTION:

  ARGUMENTS:

  RETURNS:  1 - NCSCC_RC_SUCCESS
	    2 - NCSCC_RC_FAILURE

*********************************************************/

uint32_t mdtm_add_frag_hdr(uint8_t *buf_ptr, uint16_t len, uint32_t seq_num,
			   uint16_t frag_byte)
{
	/* Add the FRAG HDR to the Buffer */
	uint8_t *data;
	data = buf_ptr;
	ncs_encode_16bit(&data, len);
#ifdef MDS_CHECKSUM_ENABLE_FLAG
	ncs_encode_8bit(&data, 0);
	ncs_encode_16bit(&data, 0);
#endif
	ncs_encode_32bit(&data, seq_num);
	ncs_encode_16bit(&data, frag_byte);
#ifdef MDS_CHECKSUM_ENABLE_FLAG
	ncs_encode_16bit(&data, len - MDTM_FRAG_HDR_LEN -
				    5); /* 2 bytes for keeping len, to cross
					   check at the receiver end */
#else
	ncs_encode_16bit(&data, len - MDTM_FRAG_HDR_LEN -
				    2); /* 2 bytes for keeping len, to cross
					   check at the receiver end */
#endif
	return NCSCC_RC_SUCCESS;
}

/*********************************************************

  Function NAME: mdtm_sendto

  DESCRIPTION:

  ARGUMENTS:

  RETURNS:  1 - NCSCC_RC_SUCCESS
	    2 - NCSCC_RC_FAILURE

*********************************************************/

static uint32_t mdtm_sendto(uint8_t *buffer, uint16_t buff_len,
			    struct tipc_portid id)
{
	/* Can be made as macro even */
	struct sockaddr_tipc server_addr;
	ssize_t send_len = 0;
#ifdef MDS_CHECKSUM_ENABLE_FLAG
	uint16_t checksum = 0;
#endif

	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.family = AF_TIPC;
	server_addr.addrtype = TIPC_ADDR_ID;
	server_addr.addr.id = id;

	m_MDS_LOG_INFO("MDTM: TIPC Sending Len=%d\n", buff_len);

#ifdef MDS_CHECKSUM_ENABLE_FLAG
	if (gl_mds_checksum == 1) {
		buffer[2] = 1;
		buffer[3] = 0;
		buffer[4] = 0;
		checksum = mds_checksum(buff_len, buffer);
		buffer[3] = checksum >> 8;
		buffer[4] = checksum;
	}
#endif

	send_len = sendto(tipc_cb.BSRsock, buffer, buff_len, 0,
			  (struct sockaddr *)&server_addr, sizeof(server_addr));
	if (send_len == buff_len) {
		m_MDS_LOG_INFO("MDTM: Successfully sent message");
		return NCSCC_RC_SUCCESS;
	} else if (send_len == -1) {
		m_MDS_LOG_ERR("MDTM: Failed to send message err :%s",
			      strerror(errno));
		return NCSCC_RC_FAILURE;
	} else {
		m_MDS_LOG_ERR("MDTM: Failed to send message send_len :%zd",
			      send_len);
		return NCSCC_RC_FAILURE;
	}
}

/*********************************************************

  Function NAME: mdtm_mcast_sendto

  DESCRIPTION:

  ARGUMENTS:

  RETURNS:  1 - NCSCC_RC_SUCCESS
	    2 - NCSCC_RC_FAILURE

*********************************************************/
static uint32_t mdtm_mcast_sendto(void *buffer, size_t size,
				  const MDTM_SEND_REQ *req)
{
	struct sockaddr_tipc server_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.family = AF_TIPC;
	server_addr.addrtype = TIPC_ADDR_MCAST;
	server_addr.addr.nameseq.type =
	    MDS_TIPC_PREFIX | MDS_SVC_INST_TYPE |
	    (req->dest_pwe_id << MDS_EVENT_SHIFT_FOR_PWE) | req->dest_svc_id;
	/*This can be scope-down to dest_svc_id  server_inst TBD*/
	server_addr.addr.nameseq.lower = HTONL(MDS_MDTM_LOWER_INSTANCE);
	/*This can be scope-down to dest_svc_id  server_inst TBD*/
	server_addr.addr.nameseq.upper = HTONL(MDS_MDTM_UPPER_INSTANCE);

	int send_len =
	    sendto(tipc_cb.BSRsock, buffer, size, 0,
		   (struct sockaddr *)&server_addr, sizeof(server_addr));
	if (send_len == size) {
		m_MDS_LOG_INFO("MDTM: Successfully sent message");
		return NCSCC_RC_SUCCESS;
	} else {
		return NCSCC_RC_FAILURE;
	}
}
/****************************************************************************
 *
 * Function Name: mdtm_add_mds_hdr
 *
 * Purpose:
 *
 * Return Value:  NCSCC_RC_SUCCESS
 *                NCSCC_RC_FAILURE
 *
 ****************************************************************************/

static uint32_t mdtm_add_mds_hdr(uint8_t *buffer, MDTM_SEND_REQ *req)
{
	uint8_t prot_ver = 0, enc_snd_type = 0;
	uint16_t mds_hdr_len;
	int version = req->msg_arch_word & 0x7;
	if (version > 1) {
		mds_hdr_len = (MDS_HDR_LEN + gl_mds_mcm_cb->node_name_len);
	} else {
		/* Old MDS_HDR_LEN = 24 */
		mds_hdr_len = (MDS_HDR_LEN - 1);
	}

#ifdef MDS_CHECKSUM_ENABLE_FLAG
	uint8_t zero_8 = 0;
#endif

	uint16_t zero_16 = 0;
	uint32_t zero_32 = 0;

	uint32_t xch_id = 0;

	uint8_t *ptr;
	ptr = buffer;

	prot_ver |= MDS_PROT | MDS_VERSION | ((uint8_t)(req->pri - 1));
	enc_snd_type = (req->msg.encoding << 6);
	enc_snd_type |= (uint8_t)req->snd_type;

	/* Message length */
	ncs_encode_16bit(&ptr, zero_16);

#ifdef MDS_CHECKSUM_ENABLE_FLAG
	/* FRAG HDR */
	ncs_encode_8bit(&ptr, zero_8);   /* for checksum flag */
	ncs_encode_16bit(&ptr, zero_16); /* for checksum */
#endif

	ncs_encode_32bit(&ptr, zero_32);
	ncs_encode_16bit(&ptr, zero_16);
	ncs_encode_16bit(&ptr, zero_16);

	/* MDS HDR */
	ncs_encode_8bit(&ptr, prot_ver);
	ncs_encode_16bit(&ptr,
			 (
			     uint16_t)mds_hdr_len); /* Will be updated if any
						       additional options are
						       being added at the end */
	ncs_encode_32bit(&ptr, req->svc_seq_num);
	ncs_encode_8bit(&ptr, enc_snd_type);
	ncs_encode_16bit(&ptr, req->src_pwe_id);
	ncs_encode_16bit(&ptr, req->src_vdest_id);
	ncs_encode_16bit(&ptr, req->src_svc_id);
	ncs_encode_16bit(&ptr, req->dest_vdest_id);
	ncs_encode_16bit(&ptr, req->dest_svc_id);

	switch (req->snd_type) {
	case MDS_SENDTYPE_SNDRSP:
	case MDS_SENDTYPE_SNDRACK:
	case MDS_SENDTYPE_SNDACK:
	case MDS_SENDTYPE_RSP:
	case MDS_SENDTYPE_REDRSP:
	case MDS_SENDTYPE_REDRACK:
	case MDS_SENDTYPE_REDACK:
	case MDS_SENDTYPE_RRSP:
	case MDS_SENDTYPE_ACK:
	case MDS_SENDTYPE_RACK:
		xch_id = req->xch_id;
		break;

	default:
		xch_id = 0;
		break;
	}

	ncs_encode_32bit(&ptr, xch_id);
	ncs_encode_16bit(&ptr, req->msg_fmt_ver); /* New field */
	if (version > 1) {
		ncs_encode_8bit(&ptr,
				gl_mds_mcm_cb->node_name_len); /* New field 1 */
		ncs_encode_octets(
		    &ptr, (uint8_t *)gl_mds_mcm_cb->node_name,
		    gl_mds_mcm_cb->node_name_len); /* New field 2 */
	}
	return NCSCC_RC_SUCCESS;
}
