/*      -*- OpenSAF  -*-
 *
 * (C) Copyright 2017 The OpenSAF Foundation
 * Copyright Ericsson AB 2017 - All Rights Reserved.
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

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

// gcc -g -Wall -o osaf_gcov_dump osaf_gcov_dump.c

#define DFLT_MULTICAST_PORT "4712"
#define DFLT_MULTICAST_GROUP "239.0.0.1"

// ./osaf_gcov_dump
// ./osaf_gcov_dump 224.0.0.1 4712
// ./osaf_gcov_dump ff02::1 4712

static int udp_socket(const char *host, const char *serv,
		  struct sockaddr **sa_ptr, socklen_t *salen_ptr) {
	int sock_fd;
	int rc;
	struct addrinfo hints;
	struct addrinfo *res;
	struct addrinfo *rp;

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC; 		// Allow IPv4 or IPv6
	hints.ai_socktype = SOCK_DGRAM;

	rc = getaddrinfo(host, serv, &hints, &res);
	if (rc != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc));
		exit(EXIT_FAILURE);
	}

	for (rp = res; rp != NULL; rp = rp->ai_next) {
		sock_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (sock_fd >= 0) {
			break;
		}
	}

	if (rp == NULL) {
		fprintf(stderr, "Could not create socket for %s port %s: %s\n",
			host, serv, strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (rp->ai_family == PF_INET) {
		in_addr_t iface = INADDR_ANY;
		if (setsockopt(sock_fd, IPPROTO_IP, IP_MULTICAST_IF,
				&iface, sizeof(iface)) != 0) {
			fprintf(stderr, "setsockopt failed: %s\n", strerror(errno));
			exit(EXIT_FAILURE);
		}

	} else if (rp->ai_family == PF_INET6) {
		unsigned int ifindex = 0;
		if (setsockopt(sock_fd, IPPROTO_IPV6, IPV6_MULTICAST_IF,
				&ifindex, sizeof(ifindex)) != 0) {
			fprintf(stderr, "setsockopt failed: %s\n", strerror(errno));
			exit(EXIT_FAILURE);
		}

	} else {
		fprintf(stderr, "Unknown AI_FAMILY: %d\n", rp->ai_family);
		exit(EXIT_FAILURE);
	}

	*sa_ptr = malloc(rp->ai_addrlen);
	memcpy(*sa_ptr, rp->ai_addr, rp->ai_addrlen);
	*salen_ptr = rp->ai_addrlen;

	freeaddrinfo(res);

	return sock_fd;
}

int main(int argc, char *argv[])
{
	char host[128];
	char port[10];

	struct sockaddr *sa;
	socklen_t sa_len;
	int sock_fd;

	if (argc == 1) {
		strncpy(host, DFLT_MULTICAST_GROUP, sizeof(host));
		strncpy(port, DFLT_MULTICAST_PORT, sizeof(port));
	} else if (argc == 3) {
		strncpy(host, argv[1], sizeof(host));
		strncpy(port, argv[2], sizeof(port));
	} else {
		fprintf(stderr, "Usage: %s [host port]\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	sock_fd = udp_socket(host, port, &sa, &sa_len);

	if ((sendto(sock_fd, "", 1, 0, sa, sa_len)) < 0) {
		fprintf(stderr, "sendto: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	} else {
		fprintf(stdout, "sendto %s port %s ok!\n", host, port);
	}

	free(sa);

	return 0;
}
