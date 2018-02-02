/*      -*- OpenSAF  -*-
 *
 * Copyright Ericsson AB 2018 - All Rights Reserved.
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
 */

/** @file
 *
 * This file contains OpenSAF replacement of some POSIX sockets API
 * functions. The definitions in this file are for internal use within OpenSAF
 * only.
 */

#ifndef BASE_OSAF_SOCKET_H_
#define BASE_OSAF_SOCKET_H_

#include <sys/socket.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Bind a name to a socket.
 *
 * This is a convenience function that behaves exactly like the POSIX function
 * bind(3P), except that if bind() fails with errno EADDRNOTAVAIL and the
 * address family is IPv6, it will re-try binding for up to 30 seconds. The
 * reason why this is useful is that binding an IPv6 address may temporary fail
 * for a while when IPv6 Duplicate Address Detection (DAD) is in progress and
 * the address is still considered to be tentative. The failure will be reported
 * back to the caller only after bind() has failed with EADDRNOTAVAIL for 30
 * seconds. No re-try will be attempted for IPv6 addresses.
 *
 * In addition, this function will abort the process for the following errno
 * values that can only happen due to software bugs: EBADF, EINVAL, ENOTSOCK,
 * EFAULT, EDESTADDRREQ, EISCONN. This check is performed regardless of
 * address family.
 */
int osaf_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);

#ifdef __cplusplus
}
#endif

#endif  // BASE_OSAF_SOCKET_H_
