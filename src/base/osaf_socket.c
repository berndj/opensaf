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

#include "base/osaf_socket.h"
#include <errno.h>
#include "base/osaf_time.h"
#include "base/osaf_utility.h"

int osaf_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
  int rc;
  int e;
  for (int tries = 0; tries != 300; ++tries) {
    rc = bind(sockfd, addr, addrlen);
    e = errno;
    if (rc == 0 || addr->sa_family != AF_INET6 || e != EADDRNOTAVAIL) break;
    osaf_nanosleep(&kHundredMilliseconds);
  }
  errno = e;
  if (rc < 0 && (e == EBADF || e == EINVAL || e == ENOTSOCK || e == EFAULT ||
		 e == EDESTADDRREQ || e == EISCONN)) {
	  osaf_abort(sockfd);
  }
  return rc;
}
