/*      -*- OpenSAF  -*-
 *
 * (C) Copyright 2010 The OpenSAF Foundation
 * Copyright Ericsson AB 2017 - All Rights Reserved.
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

#include "dtm/dtmnd/dtm_intra.h"
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sched.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <cstdlib>
#include <cstring>
#include "base/ncs_main_papi.h"
#include "base/ncssysf_def.h"
#include "base/ncssysf_mem.h"
#include "base/ncssysf_tsk.h"
#include "dtm/dtmnd/dtm.h"
#include "dtm/dtmnd/dtm_cb.h"
#include "dtm/dtmnd/dtm_inter_trans.h"
#include "dtm/dtmnd/dtm_intra_disc.h"
#include "dtm/dtmnd/dtm_intra_trans.h"
#include "osaf/configmake.h"

DTM_INTRANODE_CB *dtm_intranode_cb = nullptr;

#define DTM_INTRANODE_POLL_TIMEOUT 20000
#define DTM_INTRANODE_TASKNAME "DTM_INTRANODE"
#define DTM_INTRANODE_STACKSIZE NCS_STACKSIZE_HUGE

#ifndef MDS_PORT_NUMBER
#define DTM_INTRA_SERVER_PORT                                                  \
	7000 /* Fixed port number for intranode communications */
#else
#define DTM_INTRA_SERVER_PORT MDS_PORT_NUMBER
#endif

uint32_t intranode_max_processes;
static struct pollfd *dtm_intranode_pfd;
static struct pollfd *pfd_list;

static int dtm_intranode_max_fd;

static uint32_t dtm_intra_processing_init(char *node_name, char *node_ip,
					  DTM_IP_ADDR_TYPE i_addr_family,
					  int32_t sndbuf_size,
					  int32_t rcvbuf_size);
static void dtm_intranode_processing(void*);
static uint32_t dtm_intranode_add_poll_fdlist(int fd, uint16_t event);
static uint32_t dtm_intranode_create_rcv_task(int task_hdl);
static uint32_t dtm_intranode_process_incoming_conn();
static uint32_t dtm_intranode_del_poll_fdlist(int fd);

static uint32_t dtm_intranode_fill_fd_set();
uint32_t dtm_socket_domain = AF_UNIX;

/**
 * Function to init the service descovery
 *
 *
 * @return NCSCC_RC_SUCCESS
 * @return NCSCC_RC_FAILURE
 *
 */
uint32_t dtm_service_discovery_init(DTM_INTERNODE_CB *dtms_cb)
{
	return dtm_intra_processing_init(
	    dtms_cb->node_name, dtms_cb->ip_addr, dtms_cb->i_addr_family,
	    dtms_cb->sock_sndbuf_size, dtms_cb->sock_rcvbuf_size);
}

/**
 * Function to init the intranode processing
 *
 *
 * @return NCSCC_RC_SUCCESS
 * @return NCSCC_RC_FAILURE
 *
 */
uint32_t dtm_intra_processing_init(char *node_name, char *node_ip,
				   DTM_IP_ADDR_TYPE i_addr_family,
				   int32_t sndbuf_size, int32_t rcvbuf_size)
{

	struct sockaddr_un serv_addr; /* For Unix Sock address */
	NCS_PATRICIA_PARAMS pat_tree_params;
	struct sockaddr_in serveraddr;
	struct sockaddr_in6 serveraddr6;

	TRACE_ENTER();
	/* UNIX is default transport for intranode */
	dtm_socket_domain = AF_UNIX;

	if (nullptr == (dtm_intranode_cb = static_cast<DTM_INTRANODE_CB *>(calloc(1, sizeof(DTM_INTRANODE_CB))))) {
		LOG_ER("DTM: Memory allocation failed for dtm_intranode_cb");
		return NCSCC_RC_FAILURE;
	}
	dtm_intranode_cb->sock_domain = dtm_socket_domain;
	dtm_intranode_cb->sock_sndbuf_size = sndbuf_size;
	dtm_intranode_cb->sock_rcvbuf_size = rcvbuf_size;
	dtm_intranode_cb->max_processes = intranode_max_processes;

	if (nullptr == (dtm_intranode_pfd = static_cast<struct pollfd *>(calloc(dtm_intranode_cb->max_processes,
                                                                          sizeof(struct pollfd))))) {
		LOG_ER("DTM: Memory allocation failed for dtm_intranode_pfd");
		return NCSCC_RC_FAILURE;
	}
	if (nullptr == (pfd_list = static_cast<struct pollfd *>(calloc(dtm_intranode_cb->max_processes,
                                                                         sizeof(struct pollfd))))) {
		LOG_ER("DTM: Memory allocation failed for pfd_list");
		return NCSCC_RC_FAILURE;
	}

	/* Open a socket, If socket opens to fail return Error */
	dtm_intranode_cb->server_sockfd =
	    socket(dtm_socket_domain, SOCK_STREAM | SOCK_CLOEXEC, 0);

	if (dtm_intranode_cb->server_sockfd < 0) {
		LOG_ER("DTM: Socket creation failed err :%s ", strerror(errno));
		free(dtm_intranode_cb);
		free(dtm_intranode_pfd);
		free(pfd_list);
		return NCSCC_RC_FAILURE;
	}

	/* Increase the socket buffer size */
	if ((rcvbuf_size > 0) &&
	    (setsockopt(dtm_intranode_cb->server_sockfd, SOL_SOCKET, SO_RCVBUF,
			&rcvbuf_size, sizeof(rcvbuf_size)) != 0)) {
		LOG_ER("DTM: Unable to set the SO_RCVBUF err :%s ",
		       strerror(errno));
		close(dtm_intranode_cb->server_sockfd);
		free(dtm_intranode_cb);
		free(dtm_intranode_pfd);
		free(pfd_list);
		return NCSCC_RC_FAILURE;
	}
	if ((sndbuf_size > 0) &&
	    (setsockopt(dtm_intranode_cb->server_sockfd, SOL_SOCKET, SO_SNDBUF,
			&sndbuf_size, sizeof(sndbuf_size)) != 0)) {
		LOG_ER("DTM: Unable to set the SO_SNDBUF err :%s ",
		       strerror(errno));
		close(dtm_intranode_cb->server_sockfd);
		free(dtm_intranode_cb);
		free(dtm_intranode_pfd);
		free(pfd_list);
		return NCSCC_RC_FAILURE;
	}

	dtm_intranode_cb->nodeid = m_NCS_GET_NODE_ID;

	memset(&serv_addr, 0, sizeof(serv_addr));

	if (dtm_socket_domain == AF_UNIX) {
#define UX_SOCK_NAME_PREFIX PKGLOCALSTATEDIR "/osaf_dtm_intra_server"

		char server_ux_name[255];
		sprintf(server_ux_name, "%s", UX_SOCK_NAME_PREFIX);
		serv_addr.sun_family = AF_UNIX;
		strcpy(serv_addr.sun_path, server_ux_name);

		unlink(serv_addr.sun_path);

		int servlen =
		    strlen(serv_addr.sun_path) + sizeof(serv_addr.sun_family);

		/* Bind the created socket here with the address  NODEID,
		 *  if bind fails return error by the closing the
		 * created socket*/

		if (bind(dtm_intranode_cb->server_sockfd,
			 reinterpret_cast<struct sockaddr *>(&serv_addr), servlen) < 0) {
			LOG_ER("DTM: Bind failed err :%s ", strerror(errno));
			close(dtm_intranode_cb->server_sockfd);
			free(dtm_intranode_cb);
			free(dtm_intranode_pfd);
			free(pfd_list);
			return NCSCC_RC_FAILURE;
		}

		if (chmod(UX_SOCK_NAME_PREFIX, (S_IRUSR | S_IWUSR | S_IRGRP |
						S_IWGRP | S_IROTH | S_IWOTH)) <
		    0) {
			LOG_ER("chmod %s failed - %s", UX_SOCK_NAME_PREFIX,
			       strerror(errno));
			close(dtm_intranode_cb->server_sockfd);
			free(dtm_intranode_cb);
			free(dtm_intranode_pfd);
			free(pfd_list);
			return NCSCC_RC_FAILURE;
		}
	} else {
		if (dtm_socket_domain == AF_INET) {
			memset(&serveraddr, 0, sizeof(serveraddr));
			serveraddr.sin_family = AF_INET;
			serveraddr.sin_port = htons(DTM_INTRA_SERVER_PORT);
			serveraddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

			if (bind(dtm_intranode_cb->server_sockfd,
				 reinterpret_cast<struct sockaddr *>(&serveraddr),
				 sizeof(serveraddr)) < 0) {
				LOG_ER("DTM: Bind failed err :%s ",
				       strerror(errno));
				close(dtm_intranode_cb->server_sockfd);
				free(dtm_intranode_cb);
				free(dtm_intranode_pfd);
				free(pfd_list);
				return NCSCC_RC_FAILURE;
			}
		} else {
			memset(&serveraddr6, 0, sizeof(serveraddr6));
			serveraddr6.sin6_family = AF_INET6;
			serveraddr6.sin6_port = htons(DTM_INTRA_SERVER_PORT);
			inet_pton(AF_INET6, "localhost",
				  &serveraddr6.sin6_addr);

			if (bind(dtm_intranode_cb->server_sockfd,
				 reinterpret_cast<struct sockaddr *>(&serveraddr6),
				 sizeof(serveraddr6)) < 0) {
				LOG_ER("DTM_INTRA: Bind failed");
				close(dtm_intranode_cb->server_sockfd);
				free(dtm_intranode_cb);
				free(dtm_intranode_pfd);
				free(pfd_list);
				return NCSCC_RC_FAILURE;
			}
		}
	}

	listen(dtm_intranode_cb->server_sockfd, 20);
	memset(&pat_tree_params, 0, sizeof(pat_tree_params));
	pat_tree_params.key_size = sizeof(uint32_t);
	if (NCSCC_RC_SUCCESS !=
	    ncs_patricia_tree_init(&dtm_intranode_cb->dtm_intranode_pid_list,
				   &pat_tree_params)) {
		LOG_ER(
		    "DTM: ncs_patricia_tree_init failed for dtm_intranode_pid_list");
		close(dtm_intranode_cb->server_sockfd);
		free(dtm_intranode_cb);
		free(dtm_intranode_pfd);
		free(pfd_list);
		return NCSCC_RC_FAILURE;
	}

	pat_tree_params.key_size = sizeof(int);
	if (NCSCC_RC_SUCCESS !=
	    ncs_patricia_tree_init(&dtm_intranode_cb->dtm_intranode_fd_list,
				   &pat_tree_params)) {
		LOG_ER(
		    "DTM: ncs_patricia_tree_init failed for dtm_intranode_pid_list");
		close(dtm_intranode_cb->server_sockfd);
		free(dtm_intranode_cb);
		free(dtm_intranode_pfd);
		free(pfd_list);
		return NCSCC_RC_FAILURE;
	}

	pat_tree_params.key_size = sizeof(uint32_t);
	if (NCSCC_RC_SUCCESS !=
	    ncs_patricia_tree_init(&dtm_intranode_cb->dtm_svc_subscr_list,
				   &pat_tree_params)) {
		LOG_ER(
		    "DTM: ncs_patricia_tree_init failed for dtm_intranode_pid_list");
		close(dtm_intranode_cb->server_sockfd);
		free(dtm_intranode_cb);
		free(dtm_intranode_pfd);
		free(pfd_list);
		return NCSCC_RC_FAILURE;
	}

	pat_tree_params.key_size = sizeof(uint32_t);
	if (NCSCC_RC_SUCCESS !=
	    ncs_patricia_tree_init(&dtm_intranode_cb->dtm_svc_install_list,
				   &pat_tree_params)) {
		LOG_ER(
		    "DTM: ncs_patricia_tree_init failed for dtm_intranode_pid_list");
		close(dtm_intranode_cb->server_sockfd);
		free(dtm_intranode_cb);
		free(dtm_intranode_pfd);
		free(pfd_list);
		return NCSCC_RC_FAILURE;
	}

	dtm_intranode_add_self_node_to_node_db(
	    dtm_intranode_cb->nodeid, node_name, node_ip, i_addr_family);

	if (m_NCS_IPC_CREATE(&dtm_intranode_cb->mbx) != NCSCC_RC_SUCCESS) {
		/* Mail box creation failed */
		LOG_ER("DTM : Intranode Mailbox Creation failed");
		close(dtm_intranode_cb->server_sockfd);
		free(dtm_intranode_cb);
		free(dtm_intranode_pfd);
		free(pfd_list);
		return NCSCC_RC_FAILURE;
	} else {

		NCS_SEL_OBJ obj;

		/* Code added for attaching the mailbox, to eliminate the DBG
		 * Print at sysf_ipc.c: 640 */
		if (NCSCC_RC_SUCCESS !=
		    m_NCS_IPC_ATTACH(&dtm_intranode_cb->mbx)) {
			m_NCS_IPC_RELEASE(&dtm_intranode_cb->mbx, nullptr);
			close(dtm_intranode_cb->server_sockfd);
			free(dtm_intranode_cb);
			free(dtm_intranode_pfd);
			free(pfd_list);
			LOG_ER("DTM: Intranode Mailbox  Attach failed");
			return NCSCC_RC_FAILURE;
		}

		obj = m_NCS_IPC_GET_SEL_OBJ(&dtm_intranode_cb->mbx);

		/* retreive the corresponding fd for mailbox and fill it in cb
		 */
		dtm_intranode_cb->mbx_fd = m_GET_FD_FROM_SEL_OBJ(
		    obj); /* extract and fill value needs to be extracted */
	}

	dtm_intranode_add_poll_fdlist(dtm_intranode_cb->server_sockfd, POLLIN);
	dtm_intranode_add_poll_fdlist(dtm_intranode_cb->mbx_fd, POLLIN);

	if (dtm_intranode_create_rcv_task(dtm_intranode_cb->task_hdl) !=
	    NCSCC_RC_SUCCESS) {
		LOG_ER("MDS:MDTM: Receive Task Creation Failed in MDTM_INIT\n");
		close(dtm_intranode_cb->server_sockfd);
		free(dtm_intranode_cb);
		free(dtm_intranode_pfd);
		free(pfd_list);
		return NCSCC_RC_FAILURE;
	}

	TRACE_LEAVE();
	return NCSCC_RC_SUCCESS;
}

/**
 * Function to create the rcv task thread
 *
 * @param task_hdl
 *
 * @return NCSCC_RC_SUCCESS
 * @return NCSCC_RC_FAILURE
 *
 */
static uint32_t dtm_intranode_create_rcv_task(int task_hdl)
{
	/*
	   STEP 1: Create a recv task which will accept the connections, recv
	   data from the local nodes */

	TRACE_ENTER();

	int policy = SCHED_RR; /*root defaults */
	int max_prio = sched_get_priority_max(policy);
	int min_prio = sched_get_priority_min(policy);
	int prio_val = ((max_prio - min_prio) * 0.87);

	if (ncs_task_create(dtm_intranode_processing,
                            reinterpret_cast<NCSCONTEXT>(task_hdl),
			      "OSAF_DTM_INTRANODE", prio_val, policy,
			      DTM_INTRANODE_STACKSIZE,
			      &dtm_intranode_cb->dtm_intranode_hdl_task) !=
	    NCSCC_RC_SUCCESS) {
		LOG_ER("Intra NODE Task Creation-failed");
		return NCSCC_RC_FAILURE;
	}

	/* Start the created task,
	 *   if start fails,
	 *        release the task by calling the NCS task release function*/
	if (ncs_task_start(dtm_intranode_cb->dtm_intranode_hdl_task) !=
	    NCSCC_RC_SUCCESS) {
		LOG_ER("DTM:Start of the Created Task-failed");
		m_NCS_TASK_RELEASE(dtm_intranode_cb->dtm_intranode_hdl_task);
		return NCSCC_RC_FAILURE;
	}
	/* return NCS success */
	TRACE_LEAVE();
	return NCSCC_RC_SUCCESS;
}

/**
 * Function to process intranode poll rcv message common
 *
 *
 *
 */
uint32_t
dtm_intranode_process_poll_rcv_msg_common(DTM_INTRANODE_PID_INFO *pid_node)
{
	uint32_t identifier = 0;
	uint8_t version = 0, *data = nullptr;

	pid_node->buffer[pid_node->buff_total_len + 2] = '\0';
	data = &pid_node->buffer[2];

	/* Decode the message */
	identifier = ncs_decode_32bit(&data);
	version = ncs_decode_8bit(&data);

	if ((DTM_INTRANODE_RCV_MSG_IDENTIFIER != identifier) ||
	    (DTM_INTRANODE_RCV_MSG_VER != version)) {
		TRACE("DTM_INTRA: Malformed packet recd, Ident = %d, ver = %d",
		      identifier, version);
		free(pid_node->buffer);
		return NCSCC_RC_FAILURE;
	}

	DTM_INTRANODE_RCV_MSG_TYPES msg_type = static_cast<DTM_INTRANODE_RCV_MSG_TYPES>(ncs_decode_8bit(&data));

	if (DTM_INTRANODE_RCV_PID_TYPE == msg_type) {
		dtm_intranode_process_pid_msg(&pid_node->buffer[8],
					      pid_node->accepted_fd);
		free(pid_node->buffer);
	} else if (DTM_INTRANODE_RCV_BIND_TYPE == msg_type) {
		dtm_intranode_process_bind_msg(&pid_node->buffer[8],
					       pid_node->accepted_fd);
		free(pid_node->buffer);
	} else if (DTM_INTRANODE_RCV_UNBIND_TYPE == msg_type) {
		dtm_intranode_process_unbind_msg(&pid_node->buffer[8],
						 pid_node->accepted_fd);
		free(pid_node->buffer);
	} else if (DTM_INTRANODE_RCV_SUBSCRIBE_TYPE == msg_type) {
		dtm_intranode_process_subscribe_msg(&pid_node->buffer[8],
						    pid_node->accepted_fd);
		free(pid_node->buffer);
	} else if (DTM_INTRANODE_RCV_UNSUBSCRIBE_TYPE == msg_type) {
		dtm_intranode_process_unsubscribe_msg(&pid_node->buffer[8],
						      pid_node->accepted_fd);
		free(pid_node->buffer);
	} else if (DTM_INTRANODE_RCV_NODE_SUBSCRIBE_TYPE == msg_type) {
		dtm_intranode_process_node_subscribe_msg(&pid_node->buffer[8],
							 pid_node->accepted_fd);
		free(pid_node->buffer);
	} else if (DTM_INTRANODE_RCV_NODE_UNSUBSCRIBE_TYPE == msg_type) {
		dtm_intranode_process_node_unsubscribe_msg(
		    &pid_node->buffer[8], pid_node->accepted_fd);
		free(pid_node->buffer);
	} else if (DTM_INTRANODE_RCV_MESSAGE_TYPE == msg_type) {
		/* Get the Destination Node ID */
		NODE_ID dst_nodeid = 0;
		uint32_t dst_processid = 0;
		dst_nodeid = ncs_decode_32bit(&data);
		dst_processid = ncs_decode_32bit(&data);
		if (dtm_intranode_cb->nodeid == dst_nodeid) {
			/* local node message */
			dtm_intranode_process_rcv_data_msg(
			    pid_node->buffer, dst_processid,
			    (pid_node->buff_total_len + 2));
		} else {
			/* remote node message */
			dtm_add_to_msg_dist_list(pid_node->buffer,
						 (pid_node->buff_total_len + 2),
						 dst_nodeid);
		}
		pid_node->bytes_tb_read = 0;
		pid_node->buff_total_len = 0;
		pid_node->num_by_read_for_len_buff = 0;
		return NCSCC_RC_SUCCESS;
	} else {
		/* msg_type not supported, log error */
		TRACE("DTM_INTRA: Recd msg_type unknown, dropping the message");
	}

	pid_node->bytes_tb_read = 0;
	pid_node->buff_total_len = 0;
	pid_node->num_by_read_for_len_buff = 0;
	pid_node->buffer = nullptr;
	return NCSCC_RC_SUCCESS;
}

/**
 * Function to process intranode poll and rcv message
 *
 *
 *
 */
void dtm_intranode_process_poll_rcv_msg(int fd)
{
	DTM_INTRANODE_PID_INFO *node = dtm_intranode_get_pid_info_using_fd(fd);

	if (nullptr == node) {
		LOG_ER(
		    "DTM INTRA : PID info coressponding to fd doesnt exist, database mismatch. fd :%d",
		    fd);
		osafassert(0);
	}
	if (0 == node->bytes_tb_read) {
		if (0 == node->num_by_read_for_len_buff) {
			uint8_t *data;
			int recd_bytes = 0;

			/*******************************************************/
			/* Receive all incoming data on this socket */
			/*******************************************************/

			recd_bytes = recv(fd, node->len_buff, 2, 0);
			if (0 == recd_bytes) {
				TRACE("DTM_INTRA: Socket close: %d  err :%s",
				      fd, strerror(errno));
				dtm_intranode_process_pid_down(fd);
				dtm_intranode_del_poll_fdlist(fd);
				return;
			} else if (2 == recd_bytes) {
				uint16_t local_len_buf = 0;

				data = node->len_buff;
				local_len_buf = ncs_decode_16bit(&data);
				node->buff_total_len = local_len_buf;
				node->num_by_read_for_len_buff = 2;

				if (nullptr == (node->buffer = static_cast<uint8_t *>(calloc(
                                        1, (local_len_buf + 3))))) {
					/* Length + 2 is done to reuse the same
					   buffer while sending to other nodes
					 */
					LOG_ER(
					    "Memory allocation failed in dtm_intranode_processing");
					return;
				}
				recd_bytes = recv(fd, &node->buffer[2],
						  local_len_buf, 0);

				if (recd_bytes < 0) {
					return;
				} else if (0 == recd_bytes) {
					TRACE(
					    "DTM_INTRA: Socket close: %d  err :%s",
					    fd, strerror(errno));
					dtm_intranode_process_pid_down(fd);
					dtm_intranode_del_poll_fdlist(fd);
					return;
				} else if (local_len_buf > recd_bytes) {
					/* can happen only in two cases, system
					 * call interrupt or half data, */
					TRACE(
					    "less data recd, recd bytes = %d, actual len = %d",
					    recd_bytes, local_len_buf);
					node->bytes_tb_read =
					    node->buff_total_len - recd_bytes;
					return;
				} else if (local_len_buf == recd_bytes) {
					/* Call the common rcv function */
					dtm_intranode_process_poll_rcv_msg_common(
					    node);
				} else {
					LOG_ER(
					    "DTM :unknown corrupted data received on this file descriptor \n");
					osafassert(0);
				}
			} else {
				/* we had recd some bytes */
				if (recd_bytes < 0) {
					/* This can happen due to system call
					 * interrupt */
					return;
				} else if (1 == recd_bytes) {
					/* We recd one byte of the length part
					 */
					node->num_by_read_for_len_buff =
					    recd_bytes;
				} else {
					LOG_ER(
					    "DTM :unknown corrupted data received on this file descriptor \n");
					osafassert(0);
				}
			}
		} else if (1 == node->num_by_read_for_len_buff) {
			int recd_bytes = 0;

			recd_bytes = recv(fd, &node->len_buff[1], 1, 0);
			if (recd_bytes < 0) {
				/* This can happen due to system call interrupt
				 */
				return;
			} else if (1 == recd_bytes) {
				/* We recd one byte(remaining) of the length
				 * part */
				uint8_t *data = node->len_buff;
				node->num_by_read_for_len_buff = 2;
				node->buff_total_len = ncs_decode_16bit(&data);
				return;
			} else if (0 == recd_bytes) {
				TRACE("DTM_INTRA: Socket close: %d  err :%s",
				      fd, strerror(errno));
				dtm_intranode_process_pid_down(fd);
				dtm_intranode_del_poll_fdlist(fd);
				return;
			} else {
				LOG_ER(
				    "DTM :unknown corrupted data received on this file descriptor \n");
				osafassert(0); /* This should never occur */
			}
		} else if (2 == node->num_by_read_for_len_buff) {
			int recd_bytes = 0;

			if (nullptr == (node->buffer = static_cast<uint8_t *>(calloc(
                                1, (node->buff_total_len + 3))))) {
				/* Length + 2 is done to reuse the same buffer
				   while sending to other nodes */
				LOG_ER(
				    "\nMemory allocation failed in dtm_internode_processing");
				return;
			}
			recd_bytes =
			    recv(fd, &node->buffer[2], node->buff_total_len, 0);

			if (recd_bytes < 0) {
				return;
			} else if (0 == recd_bytes) {
				TRACE("DTM_INTRA: Socket close: %d  err :%s",
				      fd, strerror(errno));
				dtm_intranode_process_pid_down(fd);
				dtm_intranode_del_poll_fdlist(fd);
				return;
			} else if (node->buff_total_len > recd_bytes) {
				/* can happen only in two cases, system call
				 * interrupt or half data, */
				TRACE(
				    "less data recd, recd bytes = %d, actual len = %d",
				    recd_bytes, node->buff_total_len);
				node->bytes_tb_read =
				    node->buff_total_len - recd_bytes;
				return;
			} else if (node->buff_total_len == recd_bytes) {
				/* Call the common rcv function */
				dtm_intranode_process_poll_rcv_msg_common(node);
			} else {
				LOG_ER(
				    "DTM :unknown corrupted data received on this file descriptor \n");
				osafassert(0);
			}
		} else {
			LOG_ER(
			    "DTM :unknown corrupted data received on this file descriptor \n");
			osafassert(0);
		}

	} else {
		/* Partial data already read */
		int recd_bytes = 0;

		recd_bytes = recv(fd,
				  &node->buffer[2 + (node->buff_total_len -
						     node->bytes_tb_read)],
				  node->bytes_tb_read, 0);

		if (recd_bytes < 0) {
			return;
		} else if (0 == recd_bytes) {
			TRACE("DTM_INTRA: Socket close: %d  err :%s", fd,
			      strerror(errno));
			/* Close the connection */
			dtm_intranode_process_pid_down(fd);
			dtm_intranode_del_poll_fdlist(fd);
			return;
		} else if (node->bytes_tb_read > recd_bytes) {
			/* can happen only in two cases, system call interrupt
			 * or half data, */
			TRACE(
			    "less data recd, recd bytes = %d, actual len = %d",
			    recd_bytes, node->bytes_tb_read);
			node->bytes_tb_read = node->bytes_tb_read - recd_bytes;
			return;
		} else if (node->bytes_tb_read == recd_bytes) {
			/* Call the common rcv function */
			dtm_intranode_process_poll_rcv_msg_common(node);
		} else {
			LOG_ER(
			    "DTM :unknown corrupted data received on this file descriptor \n");
			osafassert(0);
		}
	}
	return;
}
/**
 * Function to handle the intranode processing
 *
 *
 * @return NCSCC_RC_SUCCESS
 * @return NCSCC_RC_FAILURE
 *
 */
static void dtm_intranode_processing(void*)
{
	TRACE_ENTER();
	while (1) {
		int poll_ret_val = 0, j = 0;
		for (j = 0; j < dtm_intranode_max_fd; j++)
			pfd_list[j] = dtm_intranode_pfd[j];

		poll_ret_val = poll(pfd_list, dtm_intranode_max_fd,
				    DTM_INTRANODE_POLL_TIMEOUT);

		if (poll_ret_val > 0) {
			int num_fd_checked = 0, i = 0;
			for (i = 0; i < dtm_intranode_max_fd; i++) {
				if (pfd_list[i].revents & POLLIN) {
					num_fd_checked++;
					if (pfd_list[i].fd ==
					    dtm_intranode_cb->server_sockfd) {
						/* Read indication on server
						 * listening socket */
						/* Accept the incoming
						 * connection */
						dtm_intranode_process_incoming_conn();
					} else if (pfd_list[i].fd ==
						   dtm_intranode_cb->mbx_fd) {
						/* Message process from
						 * internode */
						DTM_RCV_MSG_ELEM *msg_elem =
						    reinterpret_cast<DTM_RCV_MSG_ELEM*>
							 (ncs_ipc_non_blk_recv(
                                                             &dtm_intranode_cb->mbx));

						if (nullptr == msg_elem) {
							LOG_ER(
							    "DTM : Intra Node Mailbox IPC_NON_BLK_RECEIVE Failed");
							continue;
						} else if (DTM_MBX_UP_TYPE ==
							   msg_elem->type) {
							dtm_process_internode_service_up_msg(
							    msg_elem->info
								.svc_event
								.buffer,
							    msg_elem->info
								.svc_event.len,
							    msg_elem->info
								.svc_event
								.node_id);
							free(msg_elem->info
								 .svc_event
								 .buffer);
						} else if (DTM_MBX_DOWN_TYPE ==
							   msg_elem->type) {
							dtm_process_internode_service_down_msg(
							    msg_elem->info
								.svc_event
								.buffer,
							    msg_elem->info
								.svc_event.len,
							    msg_elem->info
								.svc_event
								.node_id);
							free(msg_elem->info
								 .svc_event
								 .buffer);
						} else if (
						    DTM_MBX_NODE_UP_TYPE ==
						    msg_elem->type) {
							TRACE(
							    "DTM: node_ip:%s, node_id:%u i_addr_family:%d ",
							    msg_elem->info.node
								.node_ip,
							    msg_elem->info.node
								.node_id,
							    msg_elem->info.node
								.i_addr_family);
							dtm_intranode_process_node_up(
							    msg_elem->info.node
								.node_id,
							    msg_elem->info.node
								.node_name,
							    msg_elem->info.node
								.node_ip,
							    msg_elem->info.node
								.i_addr_family,
							    msg_elem->info.node
								.mbx);
						} else if (
						    DTM_MBX_NODE_DOWN_TYPE ==
						    msg_elem->type) {
							TRACE(
							    "DTM: node_ip:%s, node_id:%u i_addr_family:%d ",
							    msg_elem->info.node
								.node_ip,
							    msg_elem->info.node
								.node_id,
							    msg_elem->info.node
								.i_addr_family);
							dtm_intranode_process_node_down(
							    msg_elem->info.node
								.node_id);
						} else if (DTM_MBX_MSG_TYPE ==
							   msg_elem->type) {
							dtm_process_rcv_internode_data_msg(
							    msg_elem->info.data
								.buffer,
							    msg_elem->info.data
								.dst_pid,
							    msg_elem->info.data
								.len);
						} else {
							LOG_ER(
							    "DTM: Intranode :Invalid evt type from mbx");
						}
						free(msg_elem);

					} else {
						/* Data to be received on
						 * accepted connections */
						dtm_intranode_process_poll_rcv_msg(
						    pfd_list[i].fd);
					}
				} else if (pfd_list[i].revents & POLLOUT) {
					num_fd_checked++;
					dtm_intranode_process_pollout(
					    pfd_list[i].fd);
				} else if (pfd_list[i].revents & POLLHUP) {
					num_fd_checked++;
					TRACE(
					    "DTM_INTRA: Socket close: %d  err :%s",
					    pfd_list[i].fd, strerror(errno));
					dtm_intranode_process_pid_down(
					    pfd_list[i].fd);
					dtm_intranode_del_poll_fdlist(
					    pfd_list[i].fd);
				}
				if (num_fd_checked == poll_ret_val) {
					/* No more FD's to be checked */
					break;
				}
			}
		}
	} /* While loop */
	free(dtm_intranode_pfd);
	free(pfd_list);
	TRACE_LEAVE();
}

/**
 * Function to add the fd to fdlist for intranode
 *
 * @param fd event
 *
 * @return NCSCC_RC_SUCCESS
 * @return NCSCC_RC_FAILURE
 *
 */
static uint32_t dtm_intranode_add_poll_fdlist(int fd, uint16_t event)
{
	DTM_INTRANODE_POLLFD_LIST *alloc_ptr = nullptr, *head_ptr = nullptr,
				  *tail_ptr = nullptr;

	TRACE_ENTER();
	if (nullptr ==
	    (alloc_ptr = static_cast<DTM_INTRANODE_POLLFD_LIST *>(calloc(1, sizeof(DTM_INTRANODE_POLLFD_LIST))))) {
		return NCSCC_RC_FAILURE;
	}
	alloc_ptr->dtm_intranode_fd.fd = fd;
	alloc_ptr->dtm_intranode_fd.events = event;

	head_ptr = dtm_intranode_cb->fd_list_ptr_head;
	tail_ptr = dtm_intranode_cb->fd_list_ptr_tail;

	alloc_ptr->next = nullptr;

	if (head_ptr == nullptr) {
		dtm_intranode_cb->fd_list_ptr_head = alloc_ptr;
		dtm_intranode_cb->fd_list_ptr_tail = alloc_ptr;
	} else {
		tail_ptr->next = alloc_ptr;
		dtm_intranode_cb->fd_list_ptr_tail = alloc_ptr;
	}
	dtm_intranode_max_fd++;
	dtm_intranode_fill_fd_set();
	TRACE_LEAVE();
	return NCSCC_RC_SUCCESS;
}

/**
 * Function to delete the fd from fdlist
 *
 * @param fd
 *
 * @return NCSCC_RC_SUCCESS
 * @return NCSCC_RC_FAILURE
 *
 */
static uint32_t dtm_intranode_del_poll_fdlist(int fd)
{
	DTM_INTRANODE_POLLFD_LIST *back = nullptr, *mov_ptr = nullptr;

	TRACE_ENTER();
	for (back = nullptr, mov_ptr = dtm_intranode_cb->fd_list_ptr_head;
	     mov_ptr != nullptr; back = mov_ptr, mov_ptr = mov_ptr->next) {
		if (fd == mov_ptr->dtm_intranode_fd.fd) {
			/* STEP: Detach "mov_ptr" from linked-list */
			if (back == nullptr) {
				/* The head node is being deleted */
				dtm_intranode_cb->fd_list_ptr_head =
				    mov_ptr->next;
				dtm_intranode_cb->fd_list_ptr_tail =
				    mov_ptr->next;

			} else {
				back->next = mov_ptr->next;
				if (nullptr == mov_ptr->next) {
					dtm_intranode_cb->fd_list_ptr_tail =
					    back;
				}
			}

			/* STEP: Detach "mov_ptr" from linked-list */
			free(mov_ptr);
			mov_ptr = nullptr;
			dtm_intranode_max_fd--;
			dtm_intranode_fill_fd_set();
			TRACE("DTM :Successfully deleted fd list");
			return NCSCC_RC_SUCCESS;
		}
	}
	LOG_ER("DTM:No matching entry found in fd list");
	TRACE_LEAVE();
	return NCSCC_RC_FAILURE;
}

/**
 * Function to set the fdlist
 *
 * @param fd events
 *
 * @return NCSCC_RC_SUCCESS
 * @return NCSCC_RC_FAILURE
 *
 */
uint32_t dtm_intranode_set_poll_fdlist(int fd, uint16_t events)
{
	DTM_INTRANODE_POLLFD_LIST *mov_ptr = dtm_intranode_cb->fd_list_ptr_head;

	TRACE_ENTER();
	if (mov_ptr == nullptr) {
		LOG_ER("DTM:Unable to set the event in the poll list");
		return NCSCC_RC_FAILURE;
	}
	while (mov_ptr != nullptr) {
		if (fd == mov_ptr->dtm_intranode_fd.fd) {
			mov_ptr->dtm_intranode_fd.events =
			    mov_ptr->dtm_intranode_fd.events | events;
			dtm_intranode_fill_fd_set();
			return NCSCC_RC_SUCCESS;
		}
		mov_ptr = mov_ptr->next;
	}
	LOG_ER("DTM:Unable to set the event in the poll list");
	TRACE_LEAVE();
	return NCSCC_RC_FAILURE;
}

/**
 * Function to reset the poll fdlist
 *
 * @param fd
 *
 * @return NCSCC_RC_SUCCESS
 * @return NCSCC_RC_FAILURE
 *
 */
uint32_t dtm_intranode_reset_poll_fdlist(int fd)
{
	DTM_INTRANODE_POLLFD_LIST *mov_ptr = dtm_intranode_cb->fd_list_ptr_head;

	TRACE_ENTER();
	if (mov_ptr == nullptr) {
		LOG_ER("DTM:Unable to set the event in the poll list");
		return NCSCC_RC_FAILURE;
	}
	while (mov_ptr != nullptr) {
		if (fd == mov_ptr->dtm_intranode_fd.fd) {
			mov_ptr->dtm_intranode_fd.events = POLLIN;
			dtm_intranode_fill_fd_set();
			return NCSCC_RC_SUCCESS;
		}
		mov_ptr = mov_ptr->next;
	}
	LOG_ER("DTM:Unable to set the event in the poll list");
	TRACE_LEAVE();
	return NCSCC_RC_FAILURE;
}

/**
 * Function to fill the fd set for intranode
 *
 *
 * @return NCSCC_RC_SUCCESS
 * @return NCSCC_RC_FAILURE
 *
 */
static uint32_t dtm_intranode_fill_fd_set()
{
	int i = 0;
	DTM_INTRANODE_POLLFD_LIST *mov_ptr = dtm_intranode_cb->fd_list_ptr_head;

	TRACE_ENTER();
	for (i = 0; i < dtm_intranode_max_fd; i++) {
		if (nullptr != mov_ptr) {
			dtm_intranode_pfd[i].fd = mov_ptr->dtm_intranode_fd.fd;
			dtm_intranode_pfd[i].events =
			    mov_ptr->dtm_intranode_fd.events;
			mov_ptr = mov_ptr->next;
		} else {
			LOG_ER(
			    "DTM: Intranode dtm_intranode_max_fd and fd_list differ");
			return NCSCC_RC_FAILURE;
		}
	}
	TRACE_LEAVE();
	return NCSCC_RC_SUCCESS;
}
static uint32_t dtm_intranode_create_pid_info(int fd)
{
	DTM_INTRANODE_PID_INFO *pid_node = nullptr;
	TRACE_ENTER();
	if (nullptr == (pid_node = static_cast<DTM_INTRANODE_PID_INFO *>(calloc(1, sizeof(DTM_INTRANODE_PID_INFO))))) {
		TRACE("\nMemory allocation failed for DTM_INTRANODE_PID_INFO");
		return NCSCC_RC_FAILURE;
	}

	pid_node->accepted_fd = fd;
	pid_node->pid =
	    0; /* Yet to be filled from the PID Message which is yet to come */
	pid_node->node_id = m_NCS_GET_NODE_ID;
	pid_node->fd_node.key_info = reinterpret_cast<uint8_t *>(&pid_node->accepted_fd);

	if (m_NCS_IPC_CREATE(&pid_node->mbx) != NCSCC_RC_SUCCESS) {
		/* Mail box creation failed */
		TRACE("Mailbox creation failed,dtm_accept msg");
		free(pid_node);
		return NCSCC_RC_FAILURE;
	} else {

		NCS_SEL_OBJ obj;
		/* Code added for attaching the mailbox */
		if (NCSCC_RC_SUCCESS != m_NCS_IPC_ATTACH(&pid_node->mbx)) {
			TRACE(
			    "\nMailbox attach failed,dtm_intranode_process_pid_msg");
			m_NCS_IPC_RELEASE(&pid_node->mbx, nullptr);
			free(pid_node);
			return NCSCC_RC_FAILURE;
		}

		obj = m_NCS_IPC_GET_SEL_OBJ(&pid_node->mbx);

		/* retreive the corresponding fd for mailbox */
		pid_node->mbx_fd = m_GET_FD_FROM_SEL_OBJ(
		    obj); /* extract and fill value needs to be extracted */
	}
	ncs_patricia_tree_add(&dtm_intranode_cb->dtm_intranode_fd_list,
			      &pid_node->fd_node);
	TRACE_LEAVE();
	return NCSCC_RC_SUCCESS;
}

/**
 * Function to process the incoming connection request
 *
 *
 * @return NCSCC_RC_SUCCESS
 * @return NCSCC_RC_FAILURE
 *
 */
static uint32_t dtm_intranode_process_incoming_conn()
{
	TRACE_ENTER();
	/* Accept processing */
	int sndbuf_size = dtm_intranode_cb->sock_sndbuf_size;
	int rcvbuf_size = dtm_intranode_cb->sock_rcvbuf_size;
	socklen_t len = sizeof(struct sockaddr_un);
	struct sockaddr_un cli_addr;
	/* Accept should be non_block */
	int accept_fd = accept(dtm_intranode_cb->server_sockfd,
			   reinterpret_cast<struct sockaddr *>(&cli_addr), &len);

	/* Error Checking */
	if (accept_fd < 0) {
		LOG_ER("DTM: Connection accept fail");
		return NCSCC_RC_FAILURE;
	}

	if ((rcvbuf_size > 0) &&
	    (setsockopt(accept_fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf_size,
			sizeof(rcvbuf_size)) != 0)) {
		LOG_ER("DTM: Unable to set the SO_RCVBUF ");
		close(accept_fd);
		return NCSCC_RC_FAILURE;
	}
	if ((sndbuf_size > 0) &&
	    (setsockopt(accept_fd, SOL_SOCKET, SO_SNDBUF, &sndbuf_size,
			sizeof(sndbuf_size)) != 0)) {
		LOG_ER("DTM: Unable to set the SO_SNDBUF ");
		close(accept_fd);
		return NCSCC_RC_FAILURE;
	}
	dtm_intranode_add_poll_fdlist(accept_fd, POLLIN);
	dtm_intranode_create_pid_info(accept_fd);
	TRACE_LEAVE();
	return NCSCC_RC_SUCCESS;
}
