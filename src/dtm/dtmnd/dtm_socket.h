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

#ifndef DTM_DTMND_DTM_SOCKET_H_
#define DTM_DTMND_DTM_SOCKET_H_

#include <cerrno>

#define SOCKET_ERROR() -1
#define SDDR_IN_USE (errno == EADDRINUSE)
#define IS_CONNECTED(a) 0
#define SOCKET_RESET(a) ((a == EPIPE) || (a == ECONNRESET))

#endif  // DTM_DTMND_DTM_SOCKET_H_
