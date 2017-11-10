/*      -*- OpenSAF  -*-
 *
 * (C) Copyright 2017 The OpenSAF Foundation
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
 * Author(s): Genband
 *
 */

/*****************************************************************************
..............................................................................

  FILE NAME: mqd_ntf.h

..............................................................................

  DESCRIPTION:

  MQD Ntf Structures & Parameters.

******************************************************************************/

#ifndef MSG_MSGD_MQD_NTF_H_
#define MSG_MSGD_MQD_NTF_H_

#include <saNtf.h>

#ifdef __cplusplus
extern "C" {
#endif

void mqdNtfNotificationCallback(SaNtfSubscriptionIdT subscriptionId,
				const SaNtfNotificationsT *notification);

SaAisErrorT mqdInitNtfSubscriptions(SaNtfHandleT);

#ifdef __cplusplus
}
#endif

#endif
