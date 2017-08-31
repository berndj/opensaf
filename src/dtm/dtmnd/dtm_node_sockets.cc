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

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include "base/ncsencdec_pub.h"
#include "base/usrbuf.h"
#include "dtm/dtmnd/dtm.h"
#include "dtm/dtmnd/dtm_node.h"
#include "dtm/dtmnd/dtm_socket.h"

#ifndef TCP_USER_TIMEOUT
#define TCP_USER_TIMEOUT 18
#endif

#define MYPORT "6900"
#define MAXBUFLEN 100

struct addrinfo *mcast_sender_addr; /* Holder for mcast_sender_addr address */

struct sockaddr_storage bcast_dest_storage;
struct sockaddr *bcast_dest_address = reinterpret_cast<struct sockaddr *>(&bcast_dest_storage);

size_t bcast_sen_addr_size; /* Holder for bcast_dest_address size ip v4 or v6
			       address */

/**
 * Close the socketr descriptors
 *
 * @param sock_desc
 *
 * @return NCSCC_RC_SUCCESS
 * @return NCSCC_RC_FAILURE
 *
 */
uint32_t dtm_sockdesc_close(int sock_desc)
{
	TRACE_ENTER();

	if (close(sock_desc) != 0) {
		LOG_ER("DTM : dtm_sockdesc_close err :%s", strerror(errno));
		return NCSCC_RC_FAILURE;
	}

	TRACE_LEAVE2("rc :%d", NCSCC_RC_SUCCESS);
	return NCSCC_RC_SUCCESS;
}

/**
 * Set the keep alive
 *
 * @param dtms_cb sock_desc
 *
 * @return NCSCC_RC_SUCCESS
 * @return NCSCC_RC_FAILURE
 *
 */
static uint32_t set_keepalive(DTM_INTERNODE_CB *dtms_cb, int sock_desc)
{

	TRACE_ENTER();
	int comm_keepidle_time, comm_keepalive_intvl, comm_keepalive_probes;
	unsigned int comm_user_timeout;
	int so_keepalive;
	int smode = 1;

	/* Start with setting the tcp keepalive stuff */
	so_keepalive = dtms_cb->so_keepalive;
	comm_keepidle_time = dtms_cb->comm_keepidle_time;
	comm_keepalive_intvl = dtms_cb->comm_keepalive_intvl;
	comm_keepalive_probes = dtms_cb->comm_keepalive_probes;
	comm_user_timeout = dtms_cb->comm_user_timeout;

	if (so_keepalive == 1) {
		socklen_t optlen;

		/* Set SO_KEEPALIVE */
		optlen = sizeof(so_keepalive);
		if (setsockopt(sock_desc, SOL_SOCKET, SO_KEEPALIVE,
			       &so_keepalive, optlen) < 0) {
			LOG_ER("DTM :setsockopt SO_KEEPALIVE failed err :%s",
			       strerror(errno));
			TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
			return NCSCC_RC_FAILURE;
		}

		/* Set TCP_KEEPIDLE */
		optlen = sizeof(comm_keepidle_time);
		if (setsockopt(sock_desc, IPPROTO_TCP, TCP_KEEPIDLE,
			       &comm_keepidle_time, optlen) < 0) {
			LOG_ER("DTM :setsockopt TCP_KEEPIDLE failed err :%s ",
			       strerror(errno));
			TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
			return NCSCC_RC_FAILURE;
		}

		/* Set TCP_KEEPINTVL */
		optlen = sizeof(comm_keepalive_intvl);
		if (setsockopt(sock_desc, IPPROTO_TCP, TCP_KEEPINTVL,
			       &comm_keepalive_intvl, optlen) < 0) {
			LOG_ER("DTM :setsockopt TCP_KEEPINTVL failed err :%s ",
			       strerror(errno));
			TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
			return NCSCC_RC_FAILURE;
		}

		/* Set TCP_KEEPCNT */
		optlen = sizeof(comm_keepalive_probes);
		if (setsockopt(sock_desc, IPPROTO_TCP, TCP_KEEPCNT,
			       &comm_keepalive_probes, optlen) < 0) {
			LOG_ER("DTM :setsockopt TCP_KEEPCNT  failed err :%s",
			       strerror(errno));
			TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
			return NCSCC_RC_FAILURE;
		}

		/* Set TCP_USER_TIMEOUT  timeout in milliseconds [ms]  */
		optlen = sizeof(comm_user_timeout);
		if (setsockopt(sock_desc, IPPROTO_TCP, TCP_USER_TIMEOUT,
			       &comm_user_timeout, optlen) < 0) {
			LOG_ER(
			    "DTM :setsockopt TCP_USER_TIMEOUT failed err :%s ",
			    strerror(errno));
			TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
			return NCSCC_RC_FAILURE;
		}
	}

	if ((setsockopt(sock_desc, SOL_SOCKET, SO_REUSEADDR, &smode,
			sizeof(smode)) == -1)) {
		LOG_ER("DTM : Error setsockpot: err :%s", strerror(errno));
		TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
		return NCSCC_RC_FAILURE;
	}

	TRACE_LEAVE2("rc :%d", NCSCC_RC_SUCCESS);
	return NCSCC_RC_SUCCESS;
}

/**
 * Enable the dgram bcast
 *
 * @param sock_desc
 *
 * @return NCSCC_RC_SUCCESS
 * @return NCSCC_RC_FAILURE
 *
 */
static uint32_t dgram_enable_bcast(int sock_desc)
{

	TRACE_ENTER();
	/* If this fails, we'll hear about it when we try to send.  This will
	 * allow */
	/* system that cannot bcast to continue if they don't plan to bcast */
	int bcast_permission = 1;
	if (setsockopt(sock_desc, SOL_SOCKET, SO_BROADCAST,
		       &bcast_permission,
		       sizeof(bcast_permission)) < 0) {
		LOG_ER("DTM :setsockopt(SO_BROADCAST) failed err :%s ",
		       strerror(errno));
		TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
		return NCSCC_RC_FAILURE;
	}

	TRACE_LEAVE2("rc :%d", NCSCC_RC_SUCCESS);
	return NCSCC_RC_SUCCESS;
}

/**
 * Join the mcast group
 *
 * @param dtms_cb mcast_receiver_addr
 *
 * @return NCSCC_RC_SUCCESS
 * @return NCSCC_RC_FAILURE
 *
 */
static uint32_t dgram_join_mcast_group(DTM_INTERNODE_CB *dtms_cb,
				       struct addrinfo *mcast_receiver_addr)
{

	TRACE_ENTER();

	/*  we need some address-family-specific pieces */
	if (mcast_receiver_addr->ai_family == AF_INET6) {
		/* Now join the mcast "group" (address) */
		struct ipv6_mreq join_request;
		memcpy(&join_request.ipv6mr_multiaddr,
		       &(reinterpret_cast<struct sockaddr_in6 *>(mcast_receiver_addr->ai_addr))
			    ->sin6_addr,
		       sizeof(struct in6_addr));
		join_request.ipv6mr_interface = if_nametoindex(dtms_cb->ifname);
		TRACE("DTM :Joining IPv6 mcast group...");
		if (setsockopt(dtms_cb->dgram_sock_rcvr, IPPROTO_IPV6,
			       IPV6_JOIN_GROUP, &join_request,
			       sizeof(join_request)) < 0) {
			LOG_ER(
			    "DTM :setsockopt(IPV6_JOIN_GROUP) failed err :%s",
			    strerror(errno));
			TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
			return NCSCC_RC_FAILURE;
		}
	} else if (mcast_receiver_addr->ai_family == AF_INET) {

		/* Now join the mcast "group" */
		struct ip_mreq join_request;
		memset(&join_request, 0, sizeof(join_request));
		join_request.imr_multiaddr =
		    reinterpret_cast<struct sockaddr_in *>(mcast_receiver_addr->ai_addr)
			->sin_addr;
		join_request.imr_interface.s_addr = inet_addr(dtms_cb->ip_addr);
		TRACE("DTM :Joining IPv4 mcast group...");
		if (setsockopt(dtms_cb->dgram_sock_rcvr, IPPROTO_IP,
			       IP_ADD_MEMBERSHIP, &join_request,
			       sizeof(join_request)) < 0) {
			LOG_ER(
			    "DTM :setsockopt(IP_ADD_MEMBERSHIP) failed err :%s ",
			    strerror(errno));
			TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
			return NCSCC_RC_FAILURE;
		}

	} else {
		LOG_ER("DTM: AF not supported :%d",
		       mcast_receiver_addr->ai_family);
		TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
		return NCSCC_RC_FAILURE;
	}

	TRACE_LEAVE2("rc :%d", NCSCC_RC_SUCCESS);
	return NCSCC_RC_SUCCESS;
}

/**
 * Send the dgram mcast message
 *
 * @param dtms_cb buffer buffer_len
 *
 * @return NCSCC_RC_SUCCESS
 * @return NCSCC_RC_FAILURE
 *
 */
uint32_t dtm_dgram_sendto_mcast(DTM_INTERNODE_CB *dtms_cb, const void *buffer,
				int buffer_len)
{
	TRACE_ENTER();
	/* Multicast the string to all who have joined the group */
	ssize_t num_bytes =
	    sendto(dtms_cb->dgram_sock_sndr, buffer, buffer_len, 0,
		   mcast_sender_addr->ai_addr, mcast_sender_addr->ai_addrlen);
	if (num_bytes < 0) {
		LOG_ER("DTM : sendto() failed err :%s ", strerror(errno));
		TRACE_LEAVE2("rc::%d", NCSCC_RC_FAILURE);
		return NCSCC_RC_FAILURE;
	} else if (num_bytes != buffer_len) {
		LOG_ER("DTM :sendto() sent unexpected number of bytes err :%s ",
		       strerror(errno));
		TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
		return NCSCC_RC_FAILURE;
	}

	TRACE_LEAVE2("rc :%d", NCSCC_RC_SUCCESS);
	return NCSCC_RC_SUCCESS;
}

/**
 * Bcast send function
 *
 * @param dtms_cb buffer buffer_len
 *
 * @return NCSCC_RC_SUCCESS
 * @return NCSCC_RC_FAILURE
 *
 */
uint32_t dtm_dgram_sendto_bcast(DTM_INTERNODE_CB *dtms_cb, const void *buffer,
				int buffer_len)
{
	TRACE_ENTER();

	/* Multicast the string to all who have joined the group */
	ssize_t num_bytes =
	    sendto(dtms_cb->dgram_sock_sndr, buffer, buffer_len, 0,
		   reinterpret_cast<struct sockaddr *>(bcast_dest_address), bcast_sen_addr_size);
	if (num_bytes < 0) {
		LOG_ER("DTM :sendto() failed err :%s", strerror(errno));
		TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
		return NCSCC_RC_FAILURE;
	} else if (num_bytes != buffer_len) {
		LOG_ER("DTM :sendto() sent unexpected number of bytes err :%s ",
		       strerror(errno));
		TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
		return NCSCC_RC_FAILURE;
	}

	TRACE_LEAVE2("rc :%d", NCSCC_RC_SUCCESS);
	return NCSCC_RC_SUCCESS;
}

/**
 * Set the mcast ttl
 *
 * @param dtms_cb mcast_ttl
 *
 * @return NCSCC_RC_SUCCESS
 * @return NCSCC_RC_FAILURE
 *
 */
static uint32_t dgram_set_mcast_ttl(DTM_INTERNODE_CB *dtms_cb, int mcast_ttl)
{

	TRACE_ENTER();
	/* Set TTL of mcast packet. Unfortunately this requires */
	/* address-family-specific code */
	if (mcast_sender_addr->ai_family == AF_INET6) { // v6-specific
		/* The v6 mcast TTL socket option requires that the value be */
		/* passed in as an integer */
		if (setsockopt(dtms_cb->dgram_sock_sndr, IPPROTO_IPV6,
			       IPV6_MULTICAST_HOPS, &mcast_ttl,
			       sizeof(mcast_ttl)) < 0) {
			LOG_ER(
			    "DTM : setsockopt(IPV6_MULTICAST_HOPS) failed err :%s",
			    strerror(errno));
			TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
			return NCSCC_RC_FAILURE;
		}
		unsigned int ifindex;
		ifindex = if_nametoindex(dtms_cb->ifname);
		if (setsockopt(dtms_cb->dgram_sock_sndr, IPPROTO_IPV6,
			       IPV6_MULTICAST_IF, &ifindex,
			       sizeof(ifindex)) < 0) {
			LOG_ER(
			    "DTM :setsockopt(IPV6_MULTICAST_IF) failed err :%s ifname :%d",
			    strerror(errno), if_nametoindex(dtms_cb->ifname));
			TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
			return NCSCC_RC_FAILURE;
		}
	} else if (mcast_sender_addr->ai_family == AF_INET) { /* v4 specific */
		/* The v4 mcast TTL socket option requires that the value be */
		/* passed in an unsigned char */
		int mcTTL = mcast_ttl;
		if (setsockopt(dtms_cb->dgram_sock_sndr, IPPROTO_IP,
			       IP_MULTICAST_TTL, &mcTTL, sizeof(mcTTL)) < 0) {
			LOG_ER(
			    "DTM :setsockopt(IP_MULTICAST_TTL) failed err :%s",
			    strerror(errno));
			TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
			return NCSCC_RC_FAILURE;
		}
	} else {

		LOG_ER("DTM: AF not supported :%d",
		       mcast_sender_addr->ai_family);
		TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
		return NCSCC_RC_FAILURE;
	}

	TRACE_LEAVE2("rc :%d", NCSCC_RC_SUCCESS);
	return NCSCC_RC_SUCCESS;
}

/**
 * Close the socket
 *
 * @param comm_socket
 *
 * @return NCSCC_RC_SUCCESS
 * @return NCSCC_RC_FAILURE
 *
 */
void dtm_comm_socket_close(DTM_NODE_DB *node)
{
	TRACE_ENTER();
	int err = 0;

	if (node != nullptr) {
		TRACE("DTM: node deleting  enty ");
		if (node->comm_status == true) {
			TRACE(
			    "DTM: dtm_comm_socket_close node_ip:%s, node_id:%u i_addr_family:%d ",
			    node->node_ip, node->node_id, node->i_addr_family);
			if (dtm_process_node_up_down(
				node->node_id, node->node_name, node->node_ip,
				node->i_addr_family,
				false) != NCSCC_RC_SUCCESS) {
				LOG_ER("dtm_process_node_up_down() failed");
				goto done;
			}
		}

		if (dtm_node_delete(node, 0) != NCSCC_RC_SUCCESS) {
			LOG_ER("DTM :dtm_node_delete failed ");
		}

		if (dtm_node_delete(node, 2) != NCSCC_RC_SUCCESS) {
			LOG_ER("DTM :dtm_node_delete failed ");
		}

		if (close(node->comm_socket) != 0) {
			err = errno;
			LOG_ER("DTM : dtm_sockdesc_close err :%s ",
			       strerror(err));
			goto done;
		}

		free(node);

	} else
		TRACE("DTM :comm_socket_not exist ");

done:
	TRACE_LEAVE();
}

/**
 * Send the message
 *
 * @param sock_desc buffer buffer_len
 *
 * @return NCSCC_RC_SUCCESS
 * @return NCSCC_RC_FAILURE
 *
 */
uint32_t dtm_comm_socket_send(int sock_desc, const void *buffer, int buffer_len)
{
	TRACE_ENTER();
	int rtn = 0;
	int err = 0;
	int rc = NCSCC_RC_SUCCESS;
	rtn = send(sock_desc, buffer, buffer_len, MSG_NOSIGNAL);
	err = errno;
	if (rtn < 0) {
		LOG_ER("DTM :dtm_comm_socket_send failed  err :%s",
		       strerror(err));
		rc = NCSCC_RC_FAILURE;
	}
	TRACE_LEAVE2("rc :%d", rc);
	return rc;
}

/**
 * Setup the new communication socket
 *
 * @param dtms_cb foreign_address foreign_port ip_addr_type
 *
 * @return NCSCC_RC_SUCCESS
 * @return NCSCC_RC_FAILURE
 *
 */
int comm_socket_setup_new(DTM_INTERNODE_CB *dtms_cb,
			  const char *foreign_address, in_port_t foreign_port,
			  DTM_IP_ADDR_TYPE ip_addr_type)
{
	int sock_desc = -1, sndbuf_size = dtms_cb->sock_sndbuf_size,
	    rcvbuf_size = dtms_cb->sock_rcvbuf_size;
	int err = 0, rv;
	char local_port_str[INET6_ADDRSTRLEN];
	struct addrinfo *addr_list;
	struct addrinfo addr_criteria, *p; /* Criteria for address match */
	char foreign_address_eth[INET6_ADDRSTRLEN + IFNAMSIZ];
        int flag;
	TRACE_ENTER();

	/* Construct the serv address structure */
	TRACE("DTM:dgram_port_rcvr :%d", dtms_cb->dgram_port_rcvr);
	snprintf(local_port_str, sizeof(local_port_str), "%d", foreign_port);

	/* Construct the serv address structure */
	memset(&addr_criteria, 0,
	       sizeof(addr_criteria));		 /* Zero out structure */
	addr_criteria.ai_family = AF_UNSPEC;     /* v4 or v6 is OK */
	addr_criteria.ai_socktype = SOCK_STREAM; /* Only streaming sockets */
	addr_criteria.ai_protocol = IPPROTO_TCP; /* Only TCP protocol */
	addr_criteria.ai_flags |= AI_NUMERICHOST;

	/* For link-local address, need to set sin6_scope_id to match the
	   device index of the network device on it has to connecct */
	if (dtms_cb->scope_link == true) {
		memset(foreign_address_eth, 0, (INET6_ADDRSTRLEN + IFNAMSIZ));
		sprintf(foreign_address_eth, "%s%s%s", foreign_address, "%",
			dtms_cb->ifname);
		rv = getaddrinfo(foreign_address_eth, local_port_str,
				 &addr_criteria, &addr_list);
		TRACE("DTM:foreign_address_eth : %s local_port_str :%s",
		      foreign_address_eth, local_port_str);
	} else {
		rv = getaddrinfo(foreign_address, local_port_str,
				 &addr_criteria, &addr_list);
		TRACE("DTM:foreign_address : %s local_port_str :%s",
		      foreign_address, local_port_str);
	}
	if (rv != 0) {
		LOG_ER("DTM:Unable to getaddrinfo() rtn_val :%d err :%s", rv,
		       strerror(errno));
		TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
		return NCSCC_RC_FAILURE;
	}

	if (addr_list == nullptr) {
		LOG_ER("DTM:Unable to getaddrinfo() rtn_val :%d err :%s", rv,
		       strerror(errno));
		TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
		return NCSCC_RC_FAILURE;
	}

	/* results and bind to the first we can */
	p = addr_list;

	TRACE("DTM : family : %d, socktype : %d, protocol :%d", p->ai_family,
	      p->ai_socktype, p->ai_protocol);
	/* Create socket for sending multicast datagrams */
	if ((sock_desc = socket(p->ai_family, p->ai_socktype | SOCK_CLOEXEC,
				p->ai_protocol)) == SOCKET_ERROR()) {
		LOG_ER("DTM:Socket creation failed (socket()) err :%s",
		       strerror(errno));
		goto done;
	}

	if ((rcvbuf_size > 0) &&
	    (setsockopt(sock_desc, SOL_SOCKET, SO_RCVBUF, &rcvbuf_size,
			sizeof(rcvbuf_size)) != 0)) {
		LOG_ER("DTM:Socket rcv buf size set failed err :%s",
		       strerror(errno));
		close(sock_desc);
		sock_desc = -1;
		goto done;
	}

	if ((sndbuf_size > 0) &&
	    (setsockopt(sock_desc, SOL_SOCKET, SO_SNDBUF, &sndbuf_size,
			sizeof(sndbuf_size)) != 0)) {
		LOG_ER("DTM:Socket snd buf size set failed err :%s",
		       strerror(errno));
		close(sock_desc);
		sock_desc = -1;
		goto done;
	}

	flag = 1;
	if (setsockopt(sock_desc, IPPROTO_TCP, TCP_NODELAY, &flag,
		       sizeof(flag)) != 0) {
		LOG_ER("DTM:Socket TCP_NODELAY set failed err :%s",
		       strerror(errno));
		close(sock_desc);
		sock_desc = -1;
		goto done;
	}

	if (NCSCC_RC_SUCCESS != set_keepalive(dtms_cb, sock_desc)) {
		LOG_ER("DTM :set_keepalive failed ");
		close(sock_desc);
		sock_desc = -1;
		goto done;
	}

	/* Try to connect to the given port */
	if (connect(sock_desc, addr_list->ai_addr, addr_list->ai_addrlen) < 0) {
		err = errno;
		LOG_ER("DTM :Connect failed (connect()) err :%s",
		       strerror(err));
		close(sock_desc);
		sock_desc = -1;
		goto done;
	}

done:
	/* Free address structure(s) allocated by getaddrinfo() */
	freeaddrinfo(addr_list);

	TRACE_LEAVE2("sock_desc : %d", sock_desc);
	return sock_desc;
}

/**
 * Bind the socket
 *
 * @param dtms_cb stream_addr
 *
 * @return NCSCC_RC_SUCCESS
 * @return NCSCC_RC_FAILURE
 *
 */
#define MAXPENDING 20

static uint32_t stream_sock_bind(DTM_INTERNODE_CB *dtms_cb,
				 struct addrinfo *stream_addr)
{

	/* Bind to the local address and set socket to list */
	TRACE_ENTER();
	if ((bind(dtms_cb->stream_sock, stream_addr->ai_addr,
		  stream_addr->ai_addrlen) == 0) &&
	    (listen(dtms_cb->stream_sock, MAXPENDING) == 0)) {
		/* get local address of socket */
		struct sockaddr_storage local_addr;
		socklen_t addr_size = sizeof(local_addr);
		if (getsockname(dtms_cb->stream_sock,
				reinterpret_cast<struct sockaddr *>(&local_addr),
				&addr_size) < 0) {
			LOG_ER("DTM : getsockname() failed err :%s",
			       strerror(errno));
			TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
			return NCSCC_RC_FAILURE;
		}

		TRACE("DTM : Binding done for : %d", dtms_cb->stream_sock);
	}

	TRACE_LEAVE2("rc :%d", NCSCC_RC_SUCCESS);
	return NCSCC_RC_SUCCESS;
}

/**
 * Stream the non blocking listner
 *
 * @param dtms_cb
 *
 * @return NCSCC_RC_SUCCESS
 * @return NCSCC_RC_FAILURE
 *
 */
uint32_t dtm_stream_nonblocking_listener(DTM_INTERNODE_CB *dtms_cb)
{
	struct addrinfo addr_criteria; /* Criteria for address match */
	char local_port_str[6];
	struct addrinfo *addr_list = nullptr, *p;
	; /* List of serv addresses */
	int sndbuf_size = dtms_cb->sock_sndbuf_size,
	    rcvbuf_size = dtms_cb->sock_rcvbuf_size;
	int rv;
	dtms_cb->stream_sock = -1;
	TRACE_ENTER();
	/* Construct the serv address structure */

	memset(&addr_criteria, 0,
	       sizeof(addr_criteria));		 /* Zero out structure */
	addr_criteria.ai_family = AF_UNSPEC;     /* Any address family */
	addr_criteria.ai_socktype = SOCK_STREAM; /* Only stream sockets */
	addr_criteria.ai_protocol = IPPROTO_TCP; /* Only TCP protocol */
	addr_criteria.ai_flags |= AI_NUMERICHOST;

	TRACE("DTM :stream_port :%d", dtms_cb->stream_port);
	/* snprintf(local_port_str, sizeof(local_port_str), "%d",
	 * htons(dtms_cb->stream_port)); */
	snprintf(local_port_str, sizeof(local_port_str), "%d",
		 dtms_cb->stream_port);

	/* For link-local address, need to set sin6_scope_id to match the
	   device index of the network device on it has to connecct */
	if (dtms_cb->scope_link == true) {
		char ip_addr_eth[INET6_ADDRSTRLEN + IFNAMSIZ];
		memset(ip_addr_eth, 0, (INET6_ADDRSTRLEN + IFNAMSIZ));
		sprintf(ip_addr_eth, "%s%s%s", dtms_cb->ip_addr, "%",
			dtms_cb->ifname);
		rv = getaddrinfo(ip_addr_eth, local_port_str, &addr_criteria,
				 &addr_list);
		TRACE("DTM:foreign_address_eth : %s local_port_str :%s",
		      ip_addr_eth, local_port_str);
	} else {
		rv = getaddrinfo(dtms_cb->ip_addr, local_port_str,
				 &addr_criteria, &addr_list);
		TRACE("DTM :ip_addr : %s local_port_str -%s", dtms_cb->ip_addr,
		      local_port_str);
	}
	if (rv != 0) {
		LOG_ER("DTM:Unable to getaddrinfo() rtn_val :%d err :%s", rv,
		       strerror(errno));
		TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
		return NCSCC_RC_FAILURE;
	}

	if (addr_list == nullptr) {
		TRACE("DTM:Unable to getaddrinfo() rtn_val :%d err :%s", rv,
		      strerror(errno));
		TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
		return NCSCC_RC_FAILURE;
	}

	/* results and bind to the first we can */
	p = addr_list;

	TRACE("DTM :family : %d, socktype : %d, protocol :%d", p->ai_family,
	      p->ai_socktype, p->ai_protocol);
	/* Create socket for sending multicast datagrams */
	if ((dtms_cb->stream_sock = socket(
		 p->ai_family, p->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC,
		 p->ai_protocol)) == SOCKET_ERROR()) {
		LOG_ER("DTM:Socket creation failed (socket()) err :%s",
		       strerror(errno));
		TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
		return NCSCC_RC_FAILURE;
	}

	if ((rcvbuf_size > 0) &&
	    (setsockopt(dtms_cb->stream_sock, SOL_SOCKET, SO_RCVBUF,
			&rcvbuf_size, sizeof(rcvbuf_size)) != 0)) {
		LOG_ER("DTM:Socket rcv buf size set failed err :%s",
		       strerror(errno));
	}

	if ((sndbuf_size > 0) &&
	    (setsockopt(dtms_cb->stream_sock, SOL_SOCKET, SO_SNDBUF,
			&sndbuf_size, sizeof(sndbuf_size)) != 0)) {
		LOG_ER("DTM:Socket snd buf size set failed err :%s",
		       strerror(errno));
	}

	int flag = 1;
	if (setsockopt(dtms_cb->stream_sock, IPPROTO_TCP, TCP_NODELAY,
		       &flag, sizeof(flag)) != 0) {
		LOG_ER("DTM:Socket TCP_NODELAY set failed err :%s",
		       strerror(errno));
		dtm_sockdesc_close(dtms_cb->stream_sock);
		return NCSCC_RC_FAILURE;
	}

	if (set_keepalive(dtms_cb, dtms_cb->stream_sock) != NCSCC_RC_SUCCESS) {
		LOG_ER("DTM : set_keepalive() failed");
		dtm_sockdesc_close(dtms_cb->stream_sock);
		TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
		return NCSCC_RC_FAILURE;
	}

	if (stream_sock_bind(dtms_cb, p) != NCSCC_RC_SUCCESS) {
		LOG_ER("DTM : stream_sock_bind() failed");
		dtm_sockdesc_close(dtms_cb->stream_sock);
		TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
		return NCSCC_RC_FAILURE;
	}

	/* Free address structure(s) allocated by getaddrinfo() */
	freeaddrinfo(addr_list);

	TRACE_LEAVE2("rc :%d", NCSCC_RC_SUCCESS);
	return NCSCC_RC_SUCCESS;
}

/**
 * Function to listen to mcast message
 *
 * @param dtms_cb
 *
 * @return NCSCC_RC_SUCCESS
 * @return NCSCC_RC_FAILURE
 *
 */
uint32_t dtm_dgram_mcast_listener(DTM_INTERNODE_CB *dtms_cb)
{

	/* Construct the serv address structure */
	struct addrinfo addr_criteria, *p; /* Criteria for address match */
	char local_port_str[INET6_ADDRSTRLEN];
	int rv;
	struct addrinfo *addr_list; /* Holder serv address */
	TRACE_ENTER();

	TRACE("DTM :dgram_port_rcvr :%d", dtms_cb->dgram_port_rcvr);
	snprintf(local_port_str, sizeof(local_port_str), "%d",
		 (dtms_cb->dgram_port_rcvr));

	dtms_cb->dgram_sock_rcvr = -1;

	memset(&addr_criteria, 0,
	       sizeof(addr_criteria));		 /* Zero out structure */
	addr_criteria.ai_family = AF_UNSPEC;     /* v4 or v6 is OK */
	addr_criteria.ai_socktype = SOCK_DGRAM;  /* Only datagram sockets */
	addr_criteria.ai_protocol = IPPROTO_UDP; /* Only UDP protocol */
	addr_criteria.ai_flags |=
	    AI_NUMERICHOST; /* Don't try to resolve address */

	/* For link-local address, need to set sin6_scope_id to match the
	   device index of the network device on it has to connecct */
	if (dtms_cb->scope_link == true) {
		char mcast_addr_eth[INET6_ADDRSTRLEN + IFNAMSIZ];
		memset(mcast_addr_eth, 0, (INET6_ADDRSTRLEN + IFNAMSIZ));
		sprintf(mcast_addr_eth, "%s%s%s", dtms_cb->mcast_addr, "%",
			dtms_cb->ifname);
		rv = getaddrinfo(mcast_addr_eth, local_port_str, &addr_criteria,
				 &addr_list);
		TRACE("DTM:mcast_addr_eth : %s local_port_str :%s",
		      mcast_addr_eth, local_port_str);
	} else {
		rv = getaddrinfo(dtms_cb->mcast_addr, local_port_str,
				 &addr_criteria, &addr_list);
		TRACE("DTM :mcast_addr : %s local_port_str :%s",
		      dtms_cb->mcast_addr, local_port_str);
	}
	if (rv != 0) {
		LOG_ER("DTM:Unable to getaddrinfo() rtn_val :%d err :%s", rv,
		       strerror(errno));
		TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
		return NCSCC_RC_FAILURE;
	}

	if (addr_list == nullptr) {
		LOG_ER("DTM:Unable to getaddrinfo() rtn_val :%d err :%s", rv,
		       strerror(errno));
		TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
		return NCSCC_RC_FAILURE;
	}

	/* results and bind to the first we can */
	p = addr_list;

	TRACE("DTM :family : %d, socktype : %d, protocol :%d", p->ai_family,
	      p->ai_socktype, p->ai_protocol);
	/* Create socket for sending multicast datagrams */
	if ((dtms_cb->dgram_sock_rcvr = socket(
		 p->ai_family, p->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC,
		 p->ai_protocol)) == SOCKET_ERROR()) {
		LOG_ER("DTM:Socket creation failed (socket()) err :%s",
		       strerror(errno));
		TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
		return NCSCC_RC_FAILURE;
	}

	if (dtms_cb->dgram_sock_rcvr == -1) {
		LOG_ER("DTM:Socket creation failed (socket())");
		TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
		return NCSCC_RC_FAILURE;
	}

	if (bind(dtms_cb->dgram_sock_rcvr, p->ai_addr, p->ai_addrlen) < 0) {
		LOG_ER("DTM : bind() failed err :%s ", strerror(errno));
		dtm_sockdesc_close(dtms_cb->dgram_sock_rcvr);
		TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
		return NCSCC_RC_FAILURE;
	}

	if (dgram_join_mcast_group(dtms_cb, p) != NCSCC_RC_SUCCESS) {
		LOG_ER("DTM : dgram_join_mcast_group() failed");
		dtm_sockdesc_close(dtms_cb->dgram_sock_rcvr);
		TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
		return NCSCC_RC_FAILURE;
	}

	/* Free address structure(s) allocated by getaddrinfo() */
	freeaddrinfo(addr_list);
	TRACE_LEAVE2("rc :%d", NCSCC_RC_SUCCESS);
	return NCSCC_RC_SUCCESS;
}

/**
 * Function to send the mcast message
 *
 * @param dtms_cb mcast_ttl
 *
 * @return NCSCC_RC_SUCCESS
 * @return NCSCC_RC_FAILURE
 *
 */
uint32_t dtm_dgram_mcast_sender(DTM_INTERNODE_CB *dtms_cb, int mcast_ttl)
{

	/* Construct the serv address structure */
	struct addrinfo addr_criteria, *p; // Criteria for address match
	char local_port_str[INET6_ADDRSTRLEN];
	int rv;
	TRACE_ENTER();

	dtms_cb->dgram_sock_sndr = -1;

	TRACE("DTM :dgram_port_rcvr :%d", dtms_cb->dgram_port_rcvr);
	snprintf(local_port_str, sizeof(local_port_str), "%d",
		 (dtms_cb->dgram_port_rcvr));

	memset(&addr_criteria, 0,
	       sizeof(addr_criteria));		 /* Zero out structure */
	addr_criteria.ai_family = AF_UNSPEC;     /* v4 or v6 is OK */
	addr_criteria.ai_socktype = SOCK_DGRAM;  /* Only datagram sockets */
	addr_criteria.ai_protocol = IPPROTO_UDP; /* Only UDP please */
	addr_criteria.ai_flags |=
	    AI_NUMERICHOST; /* Don't try to resolve address */

	/* For link-local address, need to set sin6_scope_id to match the
	   device index of the network device on it has to connecct */
	if (dtms_cb->scope_link == true) {
		char mcast_addr_eth[INET6_ADDRSTRLEN + IFNAMSIZ];
		memset(mcast_addr_eth, 0, (INET6_ADDRSTRLEN + IFNAMSIZ));
		sprintf(mcast_addr_eth, "%s%s%s", dtms_cb->mcast_addr, "%",
			dtms_cb->ifname);
		rv = getaddrinfo(mcast_addr_eth, local_port_str, &addr_criteria,
				 &mcast_sender_addr);
		TRACE("DTM :mcast_addr : %s local_port_str :%s", mcast_addr_eth,
		      local_port_str);
	} else {
		rv = getaddrinfo(dtms_cb->mcast_addr, local_port_str,
				 &addr_criteria, &mcast_sender_addr);
		TRACE("DTM :mcast_addr : %s local_port_str :%s",
		      dtms_cb->mcast_addr, local_port_str);
	}
	if (rv != 0) {
		LOG_ER("DTM:Unable to getaddrinfo() rtn_val :%d err :%s", rv,
		       strerror(errno));
		TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
		return NCSCC_RC_FAILURE;
	}

	if (mcast_sender_addr == nullptr) {
		LOG_ER("DTM:Unable to getaddrinfo() rtn_val :%d err :%s", rv,
		       strerror(errno));
		TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
		return NCSCC_RC_FAILURE;
	}

	/* results and bind to the first we can */
	p = mcast_sender_addr;

	TRACE("DTM :family : %d, socktype : %d, protocol :%d", p->ai_family,
	      p->ai_socktype, p->ai_protocol);
	/* Create socket for sending multicast datagrams */
	if ((dtms_cb->dgram_sock_sndr = socket(
		 p->ai_family, p->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC,
		 p->ai_protocol)) == SOCKET_ERROR()) {
		LOG_ER("DTM:Socket creation failed (socket()) err :%s",
		       strerror(errno));
		TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
		return NCSCC_RC_FAILURE;
	}

	if (dgram_set_mcast_ttl(dtms_cb, mcast_ttl) != NCSCC_RC_SUCCESS) {
		LOG_ER("DTM : dgram_set_mcast_ttl() failed");
		dtm_sockdesc_close(dtms_cb->dgram_sock_rcvr);
		TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
		return NCSCC_RC_FAILURE;
	}

	/* Free address structure(s) allocated by getaddrinfo() */
	/*freeaddrinfo(mcast_sender_addr); */

	TRACE_LEAVE2("rc :%d", NCSCC_RC_SUCCESS);
	return NCSCC_RC_SUCCESS;
}

/**
 * Function to listen to the bcast message
 *
 * @param dtms_cb
 *
 * @return NCSCC_RC_SUCCESS
 * @return NCSCC_RC_FAILURE
 *
 */
uint32_t dtm_dgram_bcast_listener(DTM_INTERNODE_CB *dtms_cb)
{

	struct addrinfo addr_criteria, *addr_list = nullptr,
				       *p; // Criteria for address
	char local_port_str[INET6_ADDRSTRLEN];
	int rv;
	char bcast_addr_eth[INET6_ADDRSTRLEN + IFNAMSIZ];
	TRACE_ENTER();

	TRACE("DTM :dgram_port_rcvr :%d", dtms_cb->dgram_port_rcvr);
	snprintf(local_port_str, sizeof(local_port_str), "%d",
		 (dtms_cb->dgram_port_rcvr));

	dtms_cb->dgram_sock_rcvr = -1;
	/*  Construct the server address structure */
	memset(&addr_criteria, 0, sizeof(addr_criteria)); // Zero out structure
	addr_criteria.ai_family = AF_UNSPEC;		  // Any address family
	addr_criteria.ai_flags = AI_PASSIVE;     // Accept on any address/port
	addr_criteria.ai_socktype = SOCK_DGRAM;  // Only datagram socket
	addr_criteria.ai_protocol = IPPROTO_UDP; // Only UDP socket
	addr_criteria.ai_flags |= AI_NUMERICHOST;

	TRACE("DTM :ip_addr : %s local_port_str :%s", dtms_cb->ip_addr,
	      local_port_str);
	if (dtms_cb->i_addr_family == DTM_IP_ADDR_TYPE_IPV4) {
		if ((rv = getaddrinfo(dtms_cb->bcast_addr, local_port_str,
				      &addr_criteria, &addr_list)) != 0) {
			LOG_ER(
			    "DTM:Unable to getaddrinfo() rtn_val :%d err :%s",
			    rv, strerror(errno));
			TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
			return NCSCC_RC_FAILURE;
		}
	} else if (dtms_cb->i_addr_family == DTM_IP_ADDR_TYPE_IPV6) {
		if (dtms_cb->scope_link == true) {
			memset(bcast_addr_eth, 0,
			       (INET6_ADDRSTRLEN + IFNAMSIZ));
			sprintf(bcast_addr_eth, "%s%s%s", dtms_cb->bcast_addr,
				"%", dtms_cb->ifname);
			if ((rv = getaddrinfo(bcast_addr_eth, local_port_str,
					      &addr_criteria, &addr_list)) !=
			    0) {
				LOG_ER(
				    "DTM:Unable to getaddrinfo() rtn_val :%d err :%s",
				    rv, strerror(errno));
				TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
				return NCSCC_RC_FAILURE;
			}
		} else {
			if ((rv = getaddrinfo(dtms_cb->bcast_addr,
					      local_port_str, &addr_criteria,
					      &addr_list)) != 0) {
				LOG_ER(
				    "DTM:Unable to getaddrinfo() rtn_val :%d err :%s",
				    rv, strerror(errno));
				TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
				return NCSCC_RC_FAILURE;
			}
		}
	}

	if (addr_list == nullptr) {
		LOG_ER("DTM:Unable to get addr_list ");
		TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
		return NCSCC_RC_FAILURE;
	}

	/* results and bind to the first we can */
	p = addr_list;
	bool binded = false;
	unsigned int ifindex;

	for (; p; p = p->ai_next) {

		TRACE("DTM :family : %d, socktype : %d, protocol :%d",
		      p->ai_family, p->ai_socktype, p->ai_protocol);
		if (dtms_cb->i_addr_family != p->ai_family) {
			continue;
		}

		if (p->ai_family == AF_INET) {
			void *addr;
			char ipstr[INET6_ADDRSTRLEN];
			struct sockaddr_in *ipv4 =
			    reinterpret_cast<struct sockaddr_in *>(p->ai_addr);
			addr = &(ipv4->sin_addr);
			inet_ntop(p->ai_family, addr, ipstr, sizeof ipstr);
			if (strcasecmp(ipstr, dtms_cb->bcast_addr) != 0) {
				continue;
			} else
				TRACE("DTM: DGRAM Socket binded to  = %s\n",
				      ipstr);
		} else if (p->ai_family == AF_INET6) {
			void *addr;
			char ipstr[INET6_ADDRSTRLEN];
			struct sockaddr_in6 *ipv6 =
			    reinterpret_cast<struct sockaddr_in6 *>(p->ai_addr);
			addr = &(ipv6->sin6_addr);
			inet_ntop(p->ai_family, addr, ipstr, sizeof ipstr);
			if (strcasecmp(ipstr, dtms_cb->bcast_addr) != 0) {
				continue;
			} else
				TRACE("DTM: DGRAM Socket binded to  = %s\n",
				      ipstr);
		}
		if ((dtms_cb->dgram_sock_rcvr =
			 socket(p->ai_family,
				p->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC,
				p->ai_protocol)) == SOCKET_ERROR()) {
			LOG_ER("DTM:Socket creation failed (socket()) err :%s",
			       strerror(errno));
			TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
			continue;
		}

		int smode = 1;
		if ((setsockopt(dtms_cb->dgram_sock_rcvr, SOL_SOCKET,
				SO_REUSEADDR, &smode,
				sizeof(smode)) == -1)) {
			LOG_ER("DTM : Error setsockpot: err :%s",
			       strerror(errno));
			TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
			return NCSCC_RC_FAILURE;
		}
		if (p->ai_family == AF_INET6) {
			struct sockaddr_in6 *ipv6 =
			    reinterpret_cast<struct sockaddr_in6 *>(p->ai_addr);
			ifindex = if_nametoindex(dtms_cb->ifname);
			ipv6->sin6_scope_id = ifindex;
			if (setsockopt(dtms_cb->dgram_sock_rcvr, IPPROTO_IPV6,
				       IPV6_MULTICAST_IF, &ifindex,
				       sizeof(ifindex)) < 0) {
				LOG_ER(
				    "DTM :setsockopt(IPV6_MULTICAST_IF) failed err :%s",
				    strerror(errno));
				TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
				return NCSCC_RC_FAILURE;
			}

			struct ipv6_mreq maddr;
			struct sockaddr_in6 *ipv6_mr =
			    reinterpret_cast<struct sockaddr_in6 *>(p->ai_addr);
			memset(&maddr, 0, sizeof(maddr));
			maddr.ipv6mr_multiaddr = ipv6_mr->sin6_addr;
			maddr.ipv6mr_interface =
			    if_nametoindex(dtms_cb->ifname);
			if (setsockopt(dtms_cb->dgram_sock_rcvr, IPPROTO_IPV6,
				       IPV6_ADD_MEMBERSHIP, &maddr,
				       sizeof(maddr)) < 0) {
				LOG_ER(
				    "DTM :setsockopt(IPV6_ADD_MEMBERSHIP) failed err :%s",
				    strerror(errno));
				TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
				return NCSCC_RC_FAILURE;
			}
		}
		if (bind(dtms_cb->dgram_sock_rcvr, p->ai_addr, p->ai_addrlen) ==
		    -1) {
			LOG_ER("DTM:Socket bind failed  err :%s",
			       strerror(errno));
			close(dtms_cb->dgram_sock_rcvr);
			TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
			perror("listener: bind");
			freeaddrinfo(addr_list);
			return NCSCC_RC_FAILURE;
		} else {
			binded = true;
			break;
		}
	}

	/* Free address structure(s) allocated by getaddrinfo() */
	freeaddrinfo(addr_list);
	if (binded != true) {
		TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
		return NCSCC_RC_FAILURE;
	} else {
		TRACE_LEAVE2("rc :%d", NCSCC_RC_SUCCESS);
		return NCSCC_RC_SUCCESS;
	}
}

/**
 * Function for sending the bcast
 *
 * @param dtms_cb
 *
 * @return NCSCC_RC_SUCCESS
 * @return NCSCC_RC_FAILURE
 *
 */
uint32_t dtm_dgram_bcast_sender(DTM_INTERNODE_CB *dtms_cb)
{

	TRACE_ENTER();

	dtms_cb->dgram_sock_sndr = -1;
	bcast_sen_addr_size = 0;
	memset(&bcast_dest_storage, 0, sizeof(bcast_dest_storage));

	if (dtms_cb->i_addr_family == DTM_IP_ADDR_TYPE_IPV4) {
		/* Holder for bcast_dest_address address */
		uint32_t rc = 0;
		struct sockaddr_in *bcast_sender_addr_in =
		    reinterpret_cast<struct sockaddr_in *>(&bcast_dest_storage);
		bcast_sender_addr_in->sin_family = AF_INET;
		bcast_sender_addr_in->sin_port =
		    htons((dtms_cb->dgram_port_rcvr));
		TRACE(
		    "DTM:  IP address : %s  Bcast address : %s sa_family : %d ",
		    dtms_cb->ip_addr, dtms_cb->bcast_addr,
		    dtms_cb->i_addr_family);
		rc = inet_pton(AF_INET, dtms_cb->bcast_addr,
			       &bcast_sender_addr_in->sin_addr);
		if (1 != rc) {
			LOG_ER("DTM : inet_pton failed");
			TRACE_LEAVE2("rc :%d", rc);
			return NCSCC_RC_FAILURE;
		}

		memset(bcast_sender_addr_in->sin_zero, '\0',
		       sizeof bcast_sender_addr_in->sin_zero);
		bcast_sen_addr_size = sizeof(struct sockaddr_in);
	} else if (dtms_cb->i_addr_family == DTM_IP_ADDR_TYPE_IPV6) {
		/* Holder for bcast_dest_address address */
		struct sockaddr_in6 *bcast_sender_addr_in6 =
		    reinterpret_cast<struct sockaddr_in6 *>(&bcast_dest_storage);
		bcast_sender_addr_in6->sin6_family = AF_INET6;
		bcast_sender_addr_in6->sin6_port =
		    htons((dtms_cb->dgram_port_rcvr));
		bcast_sender_addr_in6->sin6_flowinfo = 0;
		bcast_sender_addr_in6->sin6_scope_id =
		    if_nametoindex(dtms_cb->ifname);
		TRACE(
		    "DTM:  IP address : %s  Bcast address : %s sa_family : %d ",
		    dtms_cb->ip_addr, dtms_cb->bcast_addr,
		    dtms_cb->i_addr_family);
		inet_pton(AF_INET6, dtms_cb->bcast_addr,
			  &bcast_sender_addr_in6->sin6_addr);
		bcast_sen_addr_size = sizeof(struct sockaddr_in6);
	} else {
		LOG_ER("DTM : dgram_enable_bcast failed");
		TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
		return NCSCC_RC_FAILURE;
	}

	/* Create socket for sending/receiving datagrams */
	dtms_cb->dgram_sock_sndr =
	    socket(bcast_dest_address->sa_family,
		   SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_UDP);
	if (dtms_cb->dgram_sock_sndr == SOCKET_ERROR()) {
		LOG_ER("DTM :socket create  failederr :%s", strerror(errno));
		TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
		return NCSCC_RC_FAILURE;
	}

	if (dgram_enable_bcast(dtms_cb->dgram_sock_sndr) != NCSCC_RC_SUCCESS) {
		LOG_ER("DTM : dgram_enable_bcast failed");
		dtm_sockdesc_close(dtms_cb->dgram_sock_sndr);
		TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
		return NCSCC_RC_FAILURE;
	}

	if (dtms_cb->i_addr_family == DTM_IP_ADDR_TYPE_IPV6) {
		struct sockaddr_in6 *bcast_sender_addr_in6 =
		    reinterpret_cast<struct sockaddr_in6 *>(&bcast_dest_storage);
		int yes = 1;
		if (setsockopt(dtms_cb->dgram_sock_sndr, IPPROTO_IPV6,
			       IPV6_MULTICAST_HOPS, &yes, sizeof yes) < 0) {
			LOG_ER(
			    "DTM :setsockopt(IPV6_MULTICAST_HOPS) failed err :%s",
			    strerror(errno));
			TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
			return NCSCC_RC_FAILURE;
		}

		if (setsockopt(dtms_cb->dgram_sock_sndr, SOL_SOCKET,
			       SO_REUSEADDR, &yes, sizeof yes) < 0) {
			LOG_ER("DTM :setsockopt(SO_REUSEADDR) failed err :%s",
			       strerror(errno));
			TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
			return NCSCC_RC_FAILURE;
		}

		unsigned int ifindex;
		ifindex = if_nametoindex(dtms_cb->ifname);
		if (setsockopt(dtms_cb->dgram_sock_sndr, IPPROTO_IPV6,
			       IPV6_MULTICAST_IF, &ifindex,
			       sizeof(ifindex)) < 0) {
			LOG_ER(
			    "DTM :setsockopt(IPV6_MULTICAST_IF) failed err :%s ifname :%d",
			    strerror(errno), if_nametoindex(dtms_cb->ifname));
			TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
			return NCSCC_RC_FAILURE;
		}

		struct ipv6_mreq maddr;
		memset(&maddr, 0, sizeof(maddr));
		maddr.ipv6mr_multiaddr = bcast_sender_addr_in6->sin6_addr;
		maddr.ipv6mr_interface = if_nametoindex(dtms_cb->ifname);
		if (setsockopt(dtms_cb->dgram_sock_sndr, IPPROTO_IPV6,
			       IPV6_ADD_MEMBERSHIP, &maddr, sizeof maddr) < 0) {
			LOG_ER(
			    "DTM :setsockopt(IPV6_ADD_MEMBERSHIP) failed err :%s",
			    strerror(errno));
			TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
			return NCSCC_RC_FAILURE;
		}
	}
	TRACE_LEAVE2("rc :%d", NCSCC_RC_SUCCESS);
	return NCSCC_RC_SUCCESS;
}

/**
 * Function for dtm connect process
 *
 * @param dtms_cb node_ip data len
 *
 * @return NCSCC_RC_SUCCESS
 * @return NCSCC_RC_FAILURE
 *
 */
DTM_NODE_DB *dtm_process_connect(DTM_INTERNODE_CB *dtms_cb, uint8_t *data,
				 uint16_t len)
{

	in_port_t foreign_port;
	DTM_NODE_DB node = {0};
	DTM_NODE_DB *new_node = nullptr;
	uint8_t *buffer = data;
	bool mcast_flag;
	TRACE_ENTER();

	memset(&node, 0, sizeof(DTM_NODE_DB));

	/* Decode start */
	node.cluster_id = ncs_decode_16bit(&buffer);
	node.node_id = ncs_decode_32bit(&buffer);
	if (dtms_cb->node_id == node.node_id) {
		if (dtms_cb->mcast_flag != true) {
			TRACE(
			    "DTM: received the self node_id bcast message, dropping message cluster_id: %d node_id: %u",
			    node.cluster_id, node.node_id);
		} else {
			TRACE(
			    "DTM: received the self node_id mcast message, dropping message cluster_id: %d node_id: %u",
			    node.cluster_id, node.node_id);
		}
		TRACE_LEAVE();
		return nullptr;
	}

	/* Decode end */
	if (node.cluster_id != dtms_cb->cluster_id) {
		LOG_WA(
		    "DTM:cluster_id  mis match  dropping message cluster_id: %d, node_id: %u",
		    node.cluster_id, node.node_id);
		TRACE_LEAVE();
		return nullptr;
	}

	mcast_flag = ncs_decode_8bit(&buffer) != 0;
	TRACE("mcast flag: %d", mcast_flag ? 1 : 0);

	/* foreign_port = htons((in_port_t)(ncs_decode_16bit(&buffer))); */
	foreign_port = ncs_decode_16bit(&buffer);
	node.i_addr_family = static_cast<DTM_IP_ADDR_TYPE>(ncs_decode_8bit(&buffer));
	memcpy(node.node_ip, buffer, INET6_ADDRSTRLEN);

	if (initial_discovery_phase == true) {
		if (node.node_id < dtms_cb->node_id) {
			TRACE(
			    "DTM: received node_id is less than local node_id dropping message cluster_id: %d node_id: %u",
			    node.cluster_id, node.node_id);
			return nullptr;
		}
	}

	new_node = dtm_node_get_by_id(node.node_id);
	if (new_node != nullptr) {
		if ((new_node->node_id == 0) ||
		    (new_node->node_id == node.node_id) ||
		    (strncmp(node.node_ip, new_node->node_ip,
			     INET6_ADDRSTRLEN) == 0)) {
			if (new_node->comm_status == true) {
				if ((new_node->node_id == node.node_id) &&
				    (strncmp(node.node_ip, new_node->node_ip,
					     INET6_ADDRSTRLEN) == 0))
					TRACE(
					    "DTM:node already discovered dropping message cluster_id: %d,node_id :%u, node_ip: %s",
					    node.cluster_id, node.node_id,
					    node.node_ip);
				else
					LOG_WA(
					    "DTM:node duplicate discovered dropping message  cluster_id: %d, node_id :%u, node_ip:%s",
					    node.cluster_id, node.node_id,
					    node.node_ip);
			} else {

				TRACE(
				    "DTM: discovery in progress dropping message cluster_id: %d, node_id :%u, node_ip:%s",
				    node.cluster_id, node.node_id,
				    node.node_ip);
			}
			TRACE_LEAVE();
			return nullptr;
		} else if ((new_node->comm_status == false) &&
			   ((new_node->node_id != node.node_id) ||
			    (strncmp(node.node_ip, new_node->node_ip,
				     INET6_ADDRSTRLEN) != 0))) {
			TRACE(
			    "DTM: deleting stale enty cluster_id: %d, node_id :%u, node_ip:%s",
			    node.cluster_id, node.node_id, node.node_ip);
			if (dtm_node_delete(new_node, 0) != NCSCC_RC_SUCCESS) {
				LOG_ER("DTM :dtm_node_delete failed (recv())");
			}
			if (dtm_node_delete(new_node, 2) != NCSCC_RC_SUCCESS) {
				LOG_ER("DTM :dtm_node_delete failed (recv())");
			}
			free(new_node);
		}
	}

	new_node = dtm_node_new(&node);

	if (new_node == nullptr) {
		LOG_ER(" dtm_node_new failed .node_ip : %s ", node.node_ip);
		return nullptr;
	}

	int sock_desc = comm_socket_setup_new(dtms_cb, node.node_ip,
					      foreign_port, node.i_addr_family);

	new_node->comm_socket = sock_desc;
	new_node->node_id = node.node_id;
	memcpy(new_node->node_ip, node.node_ip, INET6_ADDRSTRLEN);
	new_node->i_addr_family = node.i_addr_family;

	if (sock_desc != -1) {

		TRACE(
		    "DTM: dtm_node_add .node_ip: %s node_id: %u, comm_socket %d",
		    new_node->node_ip, new_node->node_id,
		    new_node->comm_socket);
		if (dtm_node_add(new_node, 0) != NCSCC_RC_SUCCESS) {
			LOG_ER(
			    "DTM: dtm_node_add failed .node_ip: %s, node_id: %u",
			    new_node->node_ip, new_node->node_id);
			dtm_comm_socket_close(new_node);
			sock_desc = -1;
			goto node_fail;
		}

		if (dtm_node_add(new_node, 2) != NCSCC_RC_SUCCESS) {
			LOG_ER(
			    "DTM: dtm_node_add failed .node_ip: %s, node_id: %u",
			    new_node->node_ip, new_node->node_id);
			dtm_comm_socket_close(new_node);
			sock_desc = -1;
			goto node_fail;
		} else
			TRACE("DTM: dtm_node_add add .node_ip: %s, node_id: %u",
			      new_node->node_ip, new_node->node_id);
	}

node_fail:
	TRACE_LEAVE2("sock_desc :%d", sock_desc);
	return new_node;
}

/**
 * Function for dtm accept the connection
 *
 * @param dtms_cb stream_sock
 *
 * @return new node, or nullptr if no more connections to available to be accepted
 *
 */
DTM_NODE_DB *dtm_process_accept(DTM_INTERNODE_CB *dtms_cb, int stream_sock)
{
	struct sockaddr_storage clnt_addr; /* Client address */
	/* Set length of client address structure (in-out parameter) */
	socklen_t clnt_addrLen = sizeof(clnt_addr);
	const void *numericAddress = nullptr; /* Pointer to binary address */
	char addrBuffer[INET6_ADDRSTRLEN] = {0};
	DTM_NODE_DB node;
	DTM_NODE_DB *new_node = nullptr;
	int err;
	int new_conn_sd, sndbuf_size = dtms_cb->sock_sndbuf_size,
			 rcvbuf_size = dtms_cb->sock_rcvbuf_size;
	const struct sockaddr *clnt_addr1 = reinterpret_cast<struct sockaddr *>(&clnt_addr);
	TRACE_ENTER();

	memset(&node, 0, sizeof(DTM_NODE_DB));

	for (;;) {
		do {
			new_conn_sd =
			    accept(stream_sock, reinterpret_cast<struct sockaddr *>(&clnt_addr),
				   &clnt_addrLen);
			err = errno;
		} while (new_conn_sd < 0 &&
			 (err == EINTR || err == ECONNABORTED));
		if (new_conn_sd < 0) {
			if (err == EAGAIN || err == EWOULDBLOCK)
				return nullptr;
			LOG_ER("DTM:Accept failed (accept()) err :%s",
			       strerror(err));
			exit(EXIT_FAILURE);
		}

		if ((rcvbuf_size > 0) &&
		    (setsockopt(new_conn_sd, SOL_SOCKET, SO_RCVBUF,
				&rcvbuf_size, sizeof(rcvbuf_size)) != 0)) {
			LOG_ER("DTM: Unable to set the SO_RCVBUF ");
			close(new_conn_sd);
			continue;
		}
		if ((sndbuf_size > 0) &&
		    (setsockopt(new_conn_sd, SOL_SOCKET, SO_SNDBUF,
				&sndbuf_size, sizeof(sndbuf_size)) != 0)) {
			LOG_ER("DTM: Unable to set the SO_SNDBUF ");
			close(new_conn_sd);
			continue;
		}

		int flag = 1;
		if (setsockopt(new_conn_sd, IPPROTO_TCP, TCP_NODELAY,
			       &flag, sizeof(flag)) != 0) {
			LOG_ER("DTM:Socket TCP_NODELAY set failed err :%s",
			       strerror(errno));
			close(new_conn_sd);
			continue;
		}

		if (NCSCC_RC_SUCCESS != set_keepalive(dtms_cb, new_conn_sd)) {
			LOG_ER("DTM:set_keepalive failed ");
			close(new_conn_sd);
			continue;
		}

		if (clnt_addr1->sa_family == AF_INET) {
			numericAddress =
			    &reinterpret_cast<const struct sockaddr_in *>(clnt_addr1)->sin_addr;
		} else if (clnt_addr1->sa_family == AF_INET6) {
			numericAddress =
			    &reinterpret_cast<const struct sockaddr_in6 *>(clnt_addr1)->sin6_addr;
		} else {
			LOG_ER("DTM: AF not supported=%d",
			       clnt_addr1->sa_family);
			close(new_conn_sd);
			continue;
		}

		/* Convert binary to printable address */
		if (inet_ntop(clnt_addr1->sa_family, numericAddress, addrBuffer,
			      sizeof(addrBuffer)) == nullptr) {
			LOG_ER("DTM: [invalid address]");
			close(new_conn_sd);
			continue;
		} else {
			memcpy(node.node_ip, addrBuffer,
			       INET6_ADDRSTRLEN);
			node.i_addr_family = static_cast<DTM_IP_ADDR_TYPE>(clnt_addr1->sa_family);
		}

		node.cluster_id = dtms_cb->cluster_id;
		node.comm_socket = new_conn_sd;

		new_node = dtm_node_new(&node);

		if (new_node == nullptr) {
			LOG_ER(
			    "DTM: dtm_node_new failed. node_ip: %s, node_id: %u",
			    node.node_ip, node.node_id);
			close(new_conn_sd);
			continue;
		}

		if (dtm_node_add(new_node, 2) != NCSCC_RC_SUCCESS) {
			LOG_ER(
			    "DTM: dtm_node_add failed .node_ip: %s, node_id: %u",
			    new_node->node_ip, new_node->node_id);
			dtm_comm_socket_close(new_node);
			continue;
		}
		break;
	}

	TRACE_LEAVE2("DTM: new_conn_sd :%d", new_conn_sd);
	return new_node;
}

/**
 * Function to rcv the bcast message
 *
 * @param dtms_cb node_ip buffer buffer_len
 *
 * @return NCSCC_RC_SUCCESS
 * @return NCSCC_RC_FAILURE
 *
 */
ssize_t dtm_dgram_recv_bmcast(DTM_INTERNODE_CB *dtms_cb, void *buffer,
			      int buffer_len)
{
	TRACE_ENTER();

	ssize_t rtn;
	do {
		rtn = recv(dtms_cb->dgram_sock_rcvr, buffer, buffer_len,
			   MSG_DONTWAIT);
	} while (rtn < 0 && errno == EINTR);
	if (rtn < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
		LOG_ER("DTM:Receive failed (recv()) err :%s", strerror(errno));
	}

	TRACE_LEAVE2("rc :%zd", rtn);
	return rtn;
}
