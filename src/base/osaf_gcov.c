/*      -*- OpenSAF  -*-
 *
 * (C) Copyright 2017 The OpenSAF Foundation
 * (C) Copyright 2017 Ericsson AB - All Rights Reserved.
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

#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <string.h>
#include <syslog.h>
#include <stdlib.h>

// default multicast group for gcov collection
#define DFLT_MULTICAST_GROUP "239.0.0.1"
#define DFLT_MULTICAST_PORT "4712"

extern void __gcov_dump();
extern void __gcov_reset();

static int mcast_join_group(int socket_fd, const struct addrinfo* multicast_addr) {
	if (multicast_addr->ai_family == PF_INET &&
	     multicast_addr->ai_addrlen == sizeof(struct sockaddr_in)){
		struct ip_mreq multicast_request;

		memcpy(&multicast_request.imr_multiaddr,
		       &((struct sockaddr_in*)(multicast_addr->ai_addr))->sin_addr,
		       sizeof(multicast_request.imr_multiaddr));

		multicast_request.imr_interface.s_addr = htonl(INADDR_ANY);

		if (setsockopt(socket_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
			&multicast_request, sizeof(multicast_request)) != 0 ) {
			syslog(LOG_NOTICE, "%s: setsockopt: %s", __FUNCTION__, strerror(errno));
			return -1;
		}
	} else if (multicast_addr->ai_family == PF_INET6 &&
		multicast_addr->ai_addrlen == sizeof(struct sockaddr_in6)) {
		struct ipv6_mreq multicast_request;

		memcpy(&multicast_request.ipv6mr_multiaddr,
		       &((struct sockaddr_in6*)(multicast_addr->ai_addr))->sin6_addr,
		       sizeof(multicast_request.ipv6mr_multiaddr));

		// Accept on any interface
		multicast_request.ipv6mr_interface = 0;

		if (setsockopt(socket_fd, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP,
			&multicast_request, sizeof(multicast_request)) != 0 ) {
			syslog(LOG_NOTICE, "%s: setsockopt: %s", __FUNCTION__, strerror(errno));
			return -1;
		}
	} else {
		syslog(LOG_NOTICE, "%s: Unknown AI_FAMILY: %d", __FUNCTION__, multicast_addr->ai_family);
		return -1;
	}
	return 0;
}

static int udp_socket(const char *host, const char *serv,
	struct sockaddr **local_addr_ptr, socklen_t *local_addr_len_ptr,
	struct addrinfo **mcast_addr_ptr) {
	int sock_fd;
	int rc;
	struct addrinfo hints;
	struct addrinfo *multicast_addr;
	struct addrinfo *local_addr;

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC; 		// Allow IPv4 or IPv6
	hints.ai_flags  = AI_NUMERICHOST;

	rc = getaddrinfo(host, serv, &hints, &multicast_addr);
	if (rc != 0) {
		syslog(LOG_NOTICE, "%s: getaddrinfo: %s", __FUNCTION__, gai_strerror(rc));
		return -1;
	}

	hints.ai_family   = multicast_addr->ai_family;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags    = AI_PASSIVE;

	rc = getaddrinfo(NULL, serv, &hints, &local_addr);
	if (rc != 0) {
		syslog(LOG_NOTICE, "%s: getaddrinfo: %s", __FUNCTION__, gai_strerror(rc));
		return -1;
	}

	if ((sock_fd = socket(local_addr->ai_family, local_addr->ai_socktype, 0)) < 0 ) {
		syslog(LOG_NOTICE, "%s: socket: %s", __FUNCTION__, strerror(rc));
		return -1;
	}

	*local_addr_ptr = malloc(local_addr->ai_addrlen);
	memcpy(*local_addr_ptr, local_addr->ai_addr, local_addr->ai_addrlen);
	*local_addr_len_ptr = local_addr->ai_addrlen;

	*mcast_addr_ptr = malloc(sizeof(*multicast_addr));
	memcpy(*mcast_addr_ptr, multicast_addr, sizeof(*multicast_addr));

	freeaddrinfo(multicast_addr);
	freeaddrinfo(local_addr);

	return sock_fd;
}

static void* gcov_flush_thread(void* arg) {
	int socket_fd;
	const int on = 1;
	struct sockaddr *local_addr = 0;
	struct addrinfo *multicast_addr = 0;

	socklen_t local_addr_len = 0;

	char buf[40];
	struct sockaddr *addr;
	socklen_t addr_len;

	const char *multicast_port;
	const char *multicast_group;

	if ((multicast_group = getenv("OPENSAF_GCOV_MULTICAST_GROUP")) == NULL) {
		multicast_group = DFLT_MULTICAST_GROUP;
	}

	if ((multicast_port = getenv("OPENSAF_GCOV_MULTICAST_PORT")) == NULL) {
		multicast_port = DFLT_MULTICAST_PORT;
	}

	if ((socket_fd = udp_socket(multicast_group, multicast_port,
		&local_addr, &local_addr_len, &multicast_addr)) > 0) {
		if ((setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) == -1)) {
			syslog(LOG_ERR, "%s: setsockopt failed: %s", __FUNCTION__, strerror(errno));
			return 0;
		}

		if ((bind(socket_fd, local_addr, local_addr_len)) < 0) {
			syslog(LOG_ERR, "%s: bind failed: %s", __FUNCTION__, strerror(errno));
			return 0;
		}

		if (mcast_join_group(socket_fd, multicast_addr) == 0) {
			syslog(LOG_NOTICE, "%s: joined multicast group %s port %s", __FUNCTION__,
				multicast_group, multicast_port);
			addr = malloc(local_addr_len);
			for(;;) {
				addr_len = local_addr_len;
				recvfrom(socket_fd, &buf, sizeof(buf), 0, addr, &addr_len);
				__gcov_dump();
				__gcov_reset();
				syslog(LOG_NOTICE, "%s: __gov_dump() and __gcov_reset() called", __FUNCTION__);
			}

			free(local_addr);
			free(multicast_addr);
			free(addr);

			return 0;
		}
	}
	syslog(LOG_ERR, "%s: failed to setup multicast group %s %s, reason: %s",
		__FUNCTION__, multicast_group, multicast_port, strerror(errno));
	return 0;
}

void create_gcov_flush_thread(void) {
	pthread_t thread;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	if (pthread_create(&thread, &attr, gcov_flush_thread, 0) != 0) {
		syslog(LOG_ERR, "%s: pthread_create FAILED: %s", __FUNCTION__, strerror(errno));
	}

	pthread_attr_destroy(&attr);
}
