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

#include "dtm.h"
#include <stdbool.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include "dtm_socket.h"
#include "dtm_node.h"
#include "dtm_inter.h"
#include "dtm_inter_disc.h"
#include "dtm_inter_trans.h"

#define DTM_INTERNODE_RECV_BUFFER_SIZE 1024

/* packet_size +  mds_indentifier + mds_version +  msg_type  +node_id +
 * node_name_len + node_name */
#define NODE_INFO_HDR_SIZE 16

#define NODE_INFO_PKT_SIZE (NODE_INFO_HDR_SIZE + _POSIX_HOST_NAME_MAX)

static void ReceiveBcastOrMcast(void);
static void AcceptTcpConnections(uint8_t *node_info_hrd,
				 int node_info_buffer_len);
static void ReceiveFromMailbox(void);
static void AddNodeToEpoll(DTM_INTERNODE_CB *dtms_cb, DTM_NODE_DB *node);
static void RemoveNodeFromEpoll(DTM_INTERNODE_CB *dtms_cb, DTM_NODE_DB *node);

static DTM_NODE_DB dgram_sock_rcvr;
static DTM_NODE_DB stream_sock;
static DTM_NODE_DB mbx_fd;

/**
 * Function to construct the node info hdr
 *
 * @param dtms_cb buf_ptr pack_size
 *
 * @return NCSCC_RC_SUCCESS
 * @return NCSCC_RC_FAILURE
 *
 */
static uint32_t dtm_construct_node_info_hdr(DTM_INTERNODE_CB *dtms_cb,
					    uint8_t *buf_ptr, int *pack_size)
{
	/* Add the FRAG HDR to the Buffer */
	uint8_t *data;
	data = buf_ptr;

	*pack_size = NODE_INFO_HDR_SIZE + strlen(dtms_cb->node_name);

	ncs_encode_16bit(&data, (uint16_t)(*pack_size - 2)); /*pkt_type  */
	ncs_encode_32bit(&data, (uint32_t)(DTM_INTERNODE_SND_MSG_IDENTIFIER));
	ncs_encode_8bit(&data, (uint8_t)DTM_INTERNODE_SND_MSG_VER);
	ncs_encode_8bit(&data, (uint8_t)DTM_CONN_DETAILS_MSG_TYPE);
	ncs_encode_32bit(&data, dtms_cb->node_id);
	ncs_encode_32bit(&data, strlen(dtms_cb->node_name));
	ncs_encode_octets(&data, (uint8_t *)dtms_cb->node_name,
			  (uint8_t)strlen(dtms_cb->node_name));

	return NCSCC_RC_SUCCESS;
}

/**
 * Function to process the node info hdr
 *
 * @param dtms_cb stream_sock buffer node_info_hrd buffer_len
 *
 * @return NCSCC_RC_SUCCESS
 * @return NCSCC_RC_FAILURE
 *
 */
uint32_t dtm_process_node_info(DTM_INTERNODE_CB *dtms_cb, DTM_NODE_DB *node,
			       uint8_t *buffer, uint8_t *node_info_hrd,
			       int buffer_len)
{
	uint32_t node_id;
	uint32_t nodename_len;
	char nodename[_POSIX_HOST_NAME_MAX];
	int rc = 0;
	uint8_t *data = buffer;
	TRACE_ENTER();

	if (node == NULL) {
		rc = NCSCC_RC_FAILURE;
		goto done;
	}

	int fd = node->comm_socket;

	node_id = ncs_decode_32bit(&data);
	nodename_len = ncs_decode_32bit(&data);
	strncpy((char *)nodename, (char *)data, nodename_len);

	if (!node->comm_status) {

		/*****************************************************/
		/* nodeinfo data back to the client  NODE is still */
		/*****************************************************/

		if (node->node_id == 0) {
			node->node_id = node_id;
			strncpy((char *)&node->node_name, nodename,
				nodename_len);
			node->comm_status = true;
			if (dtm_node_add(node, 0) != NCSCC_RC_SUCCESS) {
				LOG_ER(
				    "DTM:  A node already exists in the cluster with similar "
					"configuration (possible duplicate IP address and/or node id), please "
					"correct the other joining Node configuration");
				rc = NCSCC_RC_FAILURE;
				goto done;
			}

		} else if (node->node_id == node_id) {
			strncpy((char *)&node->node_name, nodename,
				nodename_len);
			rc =
			    dtm_comm_socket_send(fd, node_info_hrd, buffer_len);
			if (rc != NCSCC_RC_SUCCESS) {

				LOG_ER(
				    "DTM: dtm_comm_socket_send() failed rc : %d",
				    rc);
				rc = NCSCC_RC_FAILURE;
				goto done;
			}
			node->comm_status = true;

		} else {
			LOG_ER(
				    "DTM:  A node already exists in the cluster with similar "
				"configuration (possible duplicate IP address and/or node id), please "
				"correct the other joining Node configuration");
			rc = NCSCC_RC_FAILURE;
			goto done;
		}

		TRACE(
		    "DTM: dtm_process_node_info node_ip:%s, node_id:%u i_addr_family:%d ",
		    node->node_ip, node->node_id, node->i_addr_family);
		rc = dtm_process_node_up_down(
		    node->node_id, node->node_name, node->node_ip,
		    node->i_addr_family, node->comm_status);

		if (rc != NCSCC_RC_SUCCESS) {
			LOG_ER(
			    "DTM: dtm_process_node_up_down() failed rc : %d ",
			    rc);
			rc = NCSCC_RC_FAILURE;
		}
	} else {
		LOG_ER("DTM: Node down already  received  for this node ");
		osafassert(0);
	}

done:
	TRACE_LEAVE();
	return rc;
}

/**
 * Function to process node up and down
 *
 * @param node_id node_name comm_status
 *
 * @return NCSCC_RC_SUCCESS
 * @return NCSCC_RC_FAILURE
 *
 */
uint32_t dtm_process_node_up_down(NODE_ID node_id, char *node_name,
				  char *node_ip, DTM_IP_ADDR_TYPE i_addr_family,
				  bool comm_status)
{
	if (comm_status == true) {
		TRACE(
		    "DTM: dtm_process_node_up_down node_ip:%s, node_id:%u i_addr_family:%d ",
		    node_ip, node_id, i_addr_family);
		dtm_node_up(node_id, node_name, node_ip, i_addr_family, 0);
	} else {
		dtm_node_down(node_id, node_name, 0);
	}
	return NCSCC_RC_SUCCESS;
}

/**
 * Function to process internode poll and rcv message
 *
 * @param node local_len_buf node_info_hrd node_info_buffer_len close_conn fd
 *
 * @return NCSCC_RC_SUCCESS
 * @return NCSCC_RC_FAILURE
 *
 */
void dtm_internode_process_poll_rcv_msg_common(DTM_NODE_DB *node,
					       uint16_t local_len_buf,
					       uint8_t *node_info_hrd,
					       uint16_t node_info_buffer_len,
					       bool *close_conn)
{
	DTM_MSG_TYPES pkt_type = 0;
	uint32_t identifier = 0;
	uint8_t version = 0;
	uint8_t *data = NULL;
	DTM_INTERNODE_CB *dtms_cb = dtms_gl_cb;

	node->buffer[local_len_buf + 2] = '\0';
	data = &node->buffer[2];
	/* Decode the message */
	identifier = ncs_decode_32bit(&data);
	version = ncs_decode_8bit(&data);

	if ((DTM_INTERNODE_RCV_MSG_IDENTIFIER != identifier) ||
	    (DTM_INTERNODE_RCV_MSG_VER != version)) {
		LOG_ER("DTM: Malformed packet recd, Ident : %d, ver : %d",
		       identifier, version);
		goto done;
	}

	pkt_type = ncs_decode_8bit(&data);

	if (pkt_type == DTM_UP_MSG_TYPE) {
		uint8_t *alloc_buffer = NULL;

		if (NULL == (alloc_buffer = calloc(1, (local_len_buf - 6)))) {
			LOG_ER(
			    "\nMemory allocation failed in dtm_internode_processing");
			goto done;
		}
		memcpy(alloc_buffer, &node->buffer[8], (local_len_buf - 6));

		dtm_internode_process_rcv_up_msg(
		    alloc_buffer, (local_len_buf - 6), node->node_id);
	} else if (pkt_type == DTM_CONN_DETAILS_MSG_TYPE) {
		if (dtm_process_node_info(
			dtms_cb, node, &node->buffer[8], node_info_hrd,
			node_info_buffer_len) != NCSCC_RC_SUCCESS) {
			LOG_ER(
			    " DTM : communication socket Connection closed\n");
			*close_conn = true;
		}
	} else if (pkt_type == DTM_DOWN_MSG_TYPE) {
		uint8_t *alloc_buffer = NULL;

		if (NULL == (alloc_buffer = calloc(1, (local_len_buf - 6)))) {
			LOG_ER(
			    "\nMemory allocation failed in dtm_internode_processing");
			goto done;
		}
		memcpy(alloc_buffer, &node->buffer[8], (local_len_buf - 6));
		dtm_internode_process_rcv_down_msg(
		    alloc_buffer, (local_len_buf - 6), node->node_id);
	} else if (pkt_type == DTM_MESSAGE_MSG_TYPE) {
		NODE_ID dst_nodeid = 0;
		uint32_t dst_processid = 0;
		dst_nodeid = ncs_decode_32bit(&data);
		if (dtms_cb->node_id != dst_nodeid)
			LOG_ER(
			    "Invalid dest_nodeid: %u received in dtm_internode_processing",
			    dst_nodeid);
		dst_processid = ncs_decode_32bit(&data);
		dtm_internode_process_rcv_data_msg(node->buffer, dst_processid,
						   (local_len_buf + 2));
		node->bytes_tb_read = 0;
		node->buff_total_len = 0;
		node->num_by_read_for_len_buff = 0;
		return;
	}
done:
	node->bytes_tb_read = 0;
	node->buff_total_len = 0;
	node->num_by_read_for_len_buff = 0;
	free(node->buffer);
	node->buffer = NULL;
	return;
}

/**
 * Function to process internode poll and rcv message
 *
 * @param fd close_conn node_info_hrd node_info_buffer_len
 *
 * @return NCSCC_RC_SUCCESS
 * @return NCSCC_RC_FAILURE
 *
 */
void dtm_internode_process_poll_rcv_msg(DTM_NODE_DB *node, bool *close_conn,
					uint8_t *node_info_hrd,
					uint16_t node_info_buffer_len)
{
	TRACE_ENTER();

	if (NULL == node) {
		LOG_ER("DTM: database mismatch");
		osafassert(0);
	}

	int fd = node->comm_socket;

	if (0 == node->bytes_tb_read) {
		if (0 == node->num_by_read_for_len_buff) {
			uint8_t *data;
			int recd_bytes = 0;

			/*******************************************************/
			/* Receive all incoming data on this socket */
			/*******************************************************/

			recd_bytes = recv(fd, node->len_buff, 2, MSG_DONTWAIT);
			if (0 == recd_bytes) {
				*close_conn = true;
				return;
			} else if (2 == recd_bytes) {
				uint16_t local_len_buf = 0;

				data = node->len_buff;
				local_len_buf = ncs_decode_16bit(&data);
				node->buff_total_len = local_len_buf;
				node->num_by_read_for_len_buff = 2;

				if (NULL == (node->buffer = calloc(
						 1, (local_len_buf + 3)))) {
					/* Length + 2 is done to reuse the same
					   buffer while sending to other nodes
					 */
					LOG_ER(
					    "\nMemory allocation failed in dtm_internode_processing");
					return;
				}
				recd_bytes = recv(fd, &node->buffer[2],
						  local_len_buf, MSG_DONTWAIT);

				if (recd_bytes < 0) {
					return;
				} else if (0 == recd_bytes) {
					*close_conn = true;
					return;
				} else if (local_len_buf > recd_bytes) {
					/* can happen only in two cases, system
					 * call interrupt or half data, */
					TRACE(
					    "DTM: less data recd, recd bytes : %d, actual len : %d",
					    recd_bytes, local_len_buf);
					node->bytes_tb_read =
					    node->buff_total_len - recd_bytes;
					return;
				} else if (local_len_buf == recd_bytes) {
					/* Call the common rcv function */
					dtm_internode_process_poll_rcv_msg_common(
					    node, local_len_buf, node_info_hrd,
					    node_info_buffer_len, close_conn);
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

			recd_bytes =
			    recv(fd, &node->len_buff[1], 1, MSG_DONTWAIT);
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
				*close_conn = true;
				return;
			} else {
				LOG_ER(
				    "DTM :unknown corrupted data received on this file descriptor \n");
				osafassert(0); /* This should never occur */
			}
		} else if (2 == node->num_by_read_for_len_buff) {
			int recd_bytes = 0;

			if (NULL == (node->buffer = calloc(
					 1, (node->buff_total_len + 3)))) {
				/* Length + 2 is done to reuse the same buffer
				   while sending to other nodes */
				LOG_ER(
				    "DTM :Memory allocation failed in dtm_internode_processing \n");
				return;
			}
			recd_bytes = recv(fd, &node->buffer[2],
					  node->buff_total_len, MSG_DONTWAIT);

			if (recd_bytes < 0) {
				return;
			} else if (0 == recd_bytes) {
				*close_conn = true;
				return;
			} else if (node->buff_total_len > recd_bytes) {
				/* can happen only in two cases, system call
				 * interrupt or half data, */
				TRACE(
				    "DTM: less data recd, recd bytes : %d, actual len : %d",
				    recd_bytes, node->buff_total_len);
				node->bytes_tb_read =
				    node->buff_total_len - recd_bytes;
				return;
			} else if (node->buff_total_len == recd_bytes) {
				/* Call the common rcv function */
				dtm_internode_process_poll_rcv_msg_common(
				    node, node->buff_total_len, node_info_hrd,
				    node_info_buffer_len, close_conn);
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
				  node->bytes_tb_read, MSG_DONTWAIT);

		if (recd_bytes < 0) {
			return;
		} else if (0 == recd_bytes) {
			*close_conn = true;
			return;
		} else if (node->bytes_tb_read > recd_bytes) {
			/* can happen only in two cases, system call interrupt
			 * or half data, */
			TRACE(
			    "DTM: less data recd, recd bytes : %d, actual len : %d",
			    recd_bytes, node->bytes_tb_read);
			node->bytes_tb_read = node->bytes_tb_read - recd_bytes;
			return;
		} else if (node->bytes_tb_read == recd_bytes) {
			/* Call the common rcv function */
			dtm_internode_process_poll_rcv_msg_common(
			    node, node->buff_total_len, node_info_hrd,
			    node_info_buffer_len, close_conn);
		} else {
			LOG_ER(
			    "DTM :unknown corrupted data received on this file descriptor \n");
			osafassert(0);
		}
	}
	TRACE_LEAVE();
	return;
}

/**
 * Function to handle node discovery process
 *
 * @param args
 *
 * @return NCSCC_RC_SUCCESS
 * @return NCSCC_RC_FAILURE
 *
 */
void node_discovery_process(void *arg)
{
	TRACE_ENTER();

	bool close_conn = false;
	DTM_INTERNODE_CB *dtms_cb = dtms_gl_cb;

	/* Data Received */
	int node_info_buffer_len = 0;
	uint8_t node_info_hrd[NODE_INFO_PKT_SIZE];

	/*************************************************************/
	/* Set up the initial bcast or mcast receiver socket */
	/*************************************************************/

	if (dtms_cb->mcast_flag != true) {

		if (NCSCC_RC_SUCCESS != dtm_dgram_bcast_listener(dtms_cb)) {
			LOG_ER(
			    "DTM:Set up the initial bcast  receiver socket   failed");
			exit(EXIT_FAILURE);
		}

	} else {

		if (NCSCC_RC_SUCCESS != dtm_dgram_mcast_listener(dtms_cb)) {
			LOG_ER(
			    "DTM:Set up the initial mcast  receiver socket   failed");
			exit(EXIT_FAILURE);
		}
	}

	/*************************************************************/
	/* Set up the initial listening socket */
	/*************************************************************/
	if (NCSCC_RC_SUCCESS != dtm_stream_nonblocking_listener(dtms_cb)) {
		LOG_ER(
		    "DTM: Set up the initial stream nonblocking serv  failed");
		exit(EXIT_FAILURE);
	}

	dgram_sock_rcvr.comm_socket = dtms_cb->dgram_sock_rcvr;
	stream_sock.comm_socket = dtms_cb->stream_sock;
	mbx_fd.comm_socket = dtms_cb->mbx_fd;
	AddNodeToEpoll(dtms_cb, &dgram_sock_rcvr);
	AddNodeToEpoll(dtms_cb, &stream_sock);
	AddNodeToEpoll(dtms_cb, &mbx_fd);

	/*************************************************************/
	/* Set up the initial listening socket */
	/*************************************************************/

	if (dtm_construct_node_info_hdr(dtms_cb, node_info_hrd,
					&node_info_buffer_len) !=
	    NCSCC_RC_SUCCESS) {

		LOG_ER("DTM: dtm_construct_node_info_hdr failed");
		goto done;
	}

	/*************************************************************/
	/* Loop waiting for incoming connects or for incoming data */
	/* on any of the connected sockets. */
	/*************************************************************/

	for (;;) {
		/***********************************************************/
		/* Call poll() and wait . */
		/***********************************************************/
		struct epoll_event events[128];
		int poll_ret;
		do {
			poll_ret =
			    epoll_wait(dtms_cb->epoll_fd, &events[0],
				       sizeof(events) / sizeof(events[0]), -1);
		} while (poll_ret < 0 && errno == EINTR);
		/***********************************************************/

		/* Check to see if the poll call failed. */
		/***********************************************************/
		if (poll_ret < 0) {
			LOG_ER("epoll_wait() failed: %d", errno);
			break;
		}

		/***********************************************************/
		/* One or more descriptors are readable. Need to */
		/* determine which ones they are. */
		/***********************************************************/
		for (int i = 0; i < poll_ret; ++i) {
			DTM_NODE_DB *node = (DTM_NODE_DB *)events[i].data.ptr;

			/*********************************************************/
			/* Loop through to find the descriptors that returned */
			/* EPOLLIN and determine whether it's the listening */
			/* or the active connection. */
			/*********************************************************/
			if ((events[i].events & EPOLLIN) != 0) {
				if (node == &dgram_sock_rcvr) {
					/* Data Received */
					ReceiveBcastOrMcast();
				} else if (node == &stream_sock) {
					/*******************************************************/
					/* Listening descriptor is readable. */
					/*******************************************************/
					TRACE(
					    " DTM :Listening socket is readable");
					AcceptTcpConnections(
					    node_info_hrd,
					    node_info_buffer_len);
				} else if (node == &mbx_fd) {
					/* MBX fd messages that need to be sent
					 * out from this node */
					/* Process the mailbox events */
					ReceiveFromMailbox();
				} else {

					/*********************************************************/
					/* This is not the listening socket,
					 * therefore an */
					/* existing connection must be readable
					 */
					/*********************************************************/
					dtm_internode_process_poll_rcv_msg(
					    node, &close_conn, node_info_hrd,
					    node_info_buffer_len);
				}
			} else if ((events[i].events & EPOLLOUT) != 0) {
				dtm_internode_process_pollout(node);
			} else if ((events[i].events & EPOLLHUP) != 0) {
				close_conn = true;
			}

			/*******************************************************/
			/* If the close_conn flag was turned on, we need */
			/* to clean up this active connection. This */
			/* clean up process includes removing the */
			/* descriptor. */
			/*******************************************************/
			if (close_conn) {
				close_conn = false;
				RemoveNodeFromEpoll(dtms_cb, node);
				dtm_comm_socket_close(node);
			}
		}
	}

/* End of serving running. */
/*************************************************************/
/* Clean up all of the sockets that are open */
/*************************************************************/
done:
	TRACE_LEAVE();
	return;
}

static void ReceiveBcastOrMcast(void)
{
	DTM_INTERNODE_CB *dtms_cb = dtms_gl_cb;
	uint8_t inbuf[DTM_INTERNODE_RECV_BUFFER_SIZE];
	ssize_t recd_bytes;
	do {
		recd_bytes =
		    dtm_dgram_recv_bmcast(dtms_cb, inbuf, sizeof(inbuf));
		if (recd_bytes >= 2) {
			uint8_t *data1 = inbuf;
			uint16_t recd_buf_len = ncs_decode_16bit(&data1);
			if (recd_buf_len == (size_t)recd_bytes) {
				DTM_NODE_DB *new_node = dtm_process_connect(
				    dtms_cb, data1,
				    (recd_bytes - sizeof(uint16_t)));
				if (new_node != NULL) {
					// Add the new incoming connection to
					// the pollfd structure
					LOG_IN(
					    "DTM: add New incoming connection to fd : %d\n",
					    new_node->comm_socket);
					AddNodeToEpoll(dtms_cb, new_node);
				}
			} else {
				// Log message that we are dropping the data
				LOG_ER("DTM: BRoadcastLEN-MISMATCH %" PRIu16
				       "/%zd: dropping the data",
				       recd_buf_len, recd_bytes);
			}
		} else {
			if (recd_bytes >= 0)
				LOG_ER("DTM: recd bytes=%zd on DGRAM sock",
				       recd_bytes);
		}
	} while (recd_bytes >= 0);
}

static void AcceptTcpConnections(uint8_t *node_info_hrd,
				 int node_info_buffer_len)
{
	DTM_INTERNODE_CB *dtms_cb = dtms_gl_cb;
	DTM_NODE_DB *new_node;
	while ((new_node = dtm_process_accept(dtms_cb, dtms_cb->stream_sock)) !=
	       NULL) {
		if (dtm_comm_socket_send(new_node->comm_socket, node_info_hrd,
					 node_info_buffer_len) ==
		    NCSCC_RC_SUCCESS) {
			TRACE("DTM: add New incoming connection to fd: %d",
			      new_node->comm_socket);
			AddNodeToEpoll(dtms_cb, new_node);
		} else {
			dtm_comm_socket_close(new_node);
			LOG_ER("DTM: send() failed");
		}
	}
}

static void ReceiveFromMailbox(void)
{
	DTM_INTERNODE_CB *dtms_cb = dtms_gl_cb;
	DTM_SND_MSG_ELEM *msg_elem;
	while ((msg_elem = (DTM_SND_MSG_ELEM *)(m_NCS_IPC_NON_BLK_RECEIVE(
		    &dtms_cb->mbx, NULL))) != NULL) {
		if (msg_elem->type == DTM_MBX_ADD_DISTR_TYPE) {
			dtm_internode_add_to_svc_dist_list(
			    msg_elem->info.svc_event.server_type,
			    msg_elem->info.svc_event.server_inst,
			    msg_elem->info.svc_event.pid);
		} else if (msg_elem->type == DTM_MBX_DEL_DISTR_TYPE) {
			dtm_internode_del_from_svc_dist_list(
			    msg_elem->info.svc_event.server_type,
			    msg_elem->info.svc_event.server_inst,
			    msg_elem->info.svc_event.pid);
		} else if (msg_elem->type == DTM_MBX_DATA_MSG_TYPE) {
			dtm_prepare_data_msg(msg_elem->info.data.buffer,
					     msg_elem->info.data.buff_len);
			dtm_internode_snd_msg_to_node(
			    msg_elem->info.data.buffer,
			    msg_elem->info.data.buff_len,
			    msg_elem->info.data.dst_nodeid);
		} else {
			LOG_ER("DTM Intranode :Invalid evt type from mbx");
		}
		free(msg_elem);
	}
}

static void AddNodeToEpoll(DTM_INTERNODE_CB *dtms_cb, DTM_NODE_DB *node)
{
	struct epoll_event event = {EPOLLIN, {.ptr = node}};
	if (epoll_ctl(dtms_cb->epoll_fd, EPOLL_CTL_ADD, node->comm_socket,
		      &event) != 0) {
		LOG_ER("DTM: epoll_ctl(%d, EPOLL_CTL_ADD, %d) failed: %d",
		       dtms_cb->epoll_fd, node->comm_socket, errno);
		exit(EXIT_FAILURE);
	}
}

static void RemoveNodeFromEpoll(DTM_INTERNODE_CB *dtms_cb, DTM_NODE_DB *node)
{
	if (epoll_ctl(dtms_cb->epoll_fd, EPOLL_CTL_DEL, node->comm_socket,
		      NULL) != 0) {
		LOG_ER("DTM: epoll_ctl(%d, EPOLL_CTL_DEL, %d) failed: %d",
		       dtms_cb->epoll_fd, node->comm_socket, errno);
		exit(EXIT_FAILURE);
	}
}

/**
 * Function to set poll fdlist
 *
 * @param fd events
 *
 * @return NCSCC_RC_SUCCESS
 * @return NCSCC_RC_FAILURE
 *
 */
void dtm_internode_set_pollout(DTM_NODE_DB *node)
{
	DTM_INTERNODE_CB *dtms_cb = dtms_gl_cb;
	struct epoll_event event = {EPOLLIN | EPOLLOUT, {.ptr = node}};
	if (epoll_ctl(dtms_cb->epoll_fd, EPOLL_CTL_MOD, node->comm_socket,
		      &event) == 0) {
		TRACE("event set success, in the poll fd list");
	} else {
		LOG_ER("DTM: epoll_ctl(%d, EPOLL_CTL_MOD, %d) failed: %d",
		       dtms_cb->epoll_fd, node->comm_socket, errno);
	}
}

/**
 * Function to reset poll fdlist
 *
 * @param fd
 *
 * @return NCSCC_RC_SUCCESS
 * @return NCSCC_RC_FAILURE
 *
 */
void dtm_internode_clear_pollout(DTM_NODE_DB *node)
{
	DTM_INTERNODE_CB *dtms_cb = dtms_gl_cb;
	struct epoll_event event = {EPOLLIN, {.ptr = node}};
	if (epoll_ctl(dtms_cb->epoll_fd, EPOLL_CTL_MOD, node->comm_socket,
		      &event) == 0) {
		TRACE("event set success, in the poll fd list");
	} else {
		LOG_ER("DTM: epoll_ctl(%d, EPOLL_CTL_MOD, %d) failed: %d",
		       dtms_cb->epoll_fd, node->comm_socket, errno);
	}
}
