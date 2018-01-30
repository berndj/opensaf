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

#ifndef FM_FMD_FM_H_
#define FM_FMD_FM_H_

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <saAmf.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <termios.h>
#include <unistd.h>

#include "base/ncs_main_papi.h"
#include "base/ncs_osprm.h"
#include "base/ncsgl_defs.h"

#include "base/ncs_hdl_pub.h"
#include "base/ncs_svd.h"
#include "base/ncspatricia.h"
#include "base/ncssysf_def.h"
#include "base/ncssysf_ipc.h"
#include "base/ncssysf_lck.h"
#include "base/ncssysf_mem.h"
#include "base/ncssysf_tmr.h"
#include "base/ncssysf_tsk.h"
#include "base/ncssysfpool.h"
#include "base/ncsusrbuf.h"

#include "base/ncs_queue.h"

#include "base/logtrace.h"
#include "base/ncs_edu_pub.h"
#include "base/ncs_lib.h"
#include "mds/mds_papi.h"
#include "nid/agent/nid_api.h"
#include "nid/agent/nid_start_util.h"
/* SAF Include file. */
#include <saAis.h>
#include <saAmf.h>

#include "rde/agent/rda_papi.h"

/* The below files are very specific to fm. */
#include "fm_amf.h"
#include "fm_cb.h"
#include "fm_evt.h"
#include "fm_mds.h"
#include "fm_mem.h"

extern void amfnd_down_callback(void);
extern "C" void ava_install_amf_down_cb(void (*cb)(void));
extern uint32_t initialize_for_assignment(FM_CB *cb, SaAmfHAStateT ha_state);
extern void fm_tmr_stop(FM_TMR *tmr);
#endif  // FM_FMD_FM_H_
