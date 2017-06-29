/*      -*- OpenSAF  -*-
 *
 * (C) Copyright 2008 The OpenSAF Foundation
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
 * Author(s): Emerson Network Power
 *
 */

/*****************************************************************************
..............................................................................

..............................................................................

  DESCRIPTION:

  List of MQSv-MQD header files, include as per dependencies

******************************************************************************
*/

#ifndef MSG_MSGD_MQD_H_
#define MSG_MSGD_MQD_H_
#include "msg/common/mqsv.h"

#include "mbc/mbcsv_papi.h"
/* MQD includes ... */
#include "mqd_tmr.h"
#include "mqd_db.h"
#include "mqd_red.h"
#include "mqd_mem.h"
#include "mqd_dl_api.h"
#include "mqd_api.h"
#include "mqd_saf.h"
#include "mqd_clm.h"
#include <saClm.h>
#include "base/ncssysf_lck.h"
#include "base/ncs_mda_pvt.h"

#include "base/daemon.h"

#endif  // MSG_MSGD_MQD_H_
