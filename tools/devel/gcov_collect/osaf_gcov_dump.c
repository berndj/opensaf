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
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// gcc -g -Wall -o osaf_gcov_dump osaf_gcov_dump.c
#define MULTICAST_PORT 4712
#define MULTICAST_GROUP "239.0.0.1"

int main()
{
	struct sockaddr_in addr;
	int fd;
	const char message[] = "Not used";

	if ((fd = socket(AF_INET, SOCK_DGRAM,0)) < 0) {
		perror("socket");
		exit(1);
	}

	memset(&addr,0,sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr(MULTICAST_GROUP);
	addr.sin_port = htons(MULTICAST_PORT);

	if (sendto(fd, message, sizeof(message), 0, (struct sockaddr *) &addr,
		   sizeof(addr)) < 0) {
		perror("sendto");
	} else {
		printf("sendto %s %d ok!\n", MULTICAST_GROUP, MULTICAST_PORT);
	}
	return 0;
}
