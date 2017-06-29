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
 * Author(s): Ericsson AB
 *
 */

#ifndef NTF_NTFD_NTFS_H_
#define NTF_NTFD_NTFS_H_

/* ========================================================================
 *   INCLUDE FILES
 * ========================================================================
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <saAmf.h>

#include "base/ncsgl_defs.h"
#include "base/ncs_lib.h"
#include "mds/mds_papi.h"
#include "base/ncs_mda_pvt.h"
#include "mbc/mbcsv_papi.h"
#include "base/ncs_edu_pub.h"
#include "base/ncs_util.h"
#include "base/logtrace.h"

/* NTFS files */
#include "ntf/common/ntfsv_defs.h"
#include "ntfs_cb.h"
#include "ntf/common/ntfsv_msg.h"
#include "ntfs_evt.h"
#include "ntfs_mbcsv.h"

/* ========================================================================
 *   DEFINITIONS
 * ========================================================================
 */
#define NTFSV_READER_CACHE_DEFAULT 10000

/* ========================================================================
 *   TYPE DEFINITIONS
 * ========================================================================
 */

/* ========================================================================
 *   DATA DECLARATIONS
 * ========================================================================
 */
extern ntfs_cb_t *ntfs_cb;
extern SaAisErrorT ntfs_amf_init();
extern uint32_t ntfs_mds_init(ntfs_cb_t *cb, SaAmfHAStateT ha_state);
extern void init_with_clm(void);
extern uint32_t ntfs_mds_finalize(ntfs_cb_t *cb);
extern uint32_t ntfs_mds_change_role();
extern uint32_t ntfs_mds_msg_send(ntfs_cb_t *cb, ntfsv_msg_t *msg,
                                  MDS_DEST *dest, MDS_SYNC_SND_CTXT *mds_ctxt,
                                  MDS_SEND_PRIORITY_TYPE prio);
extern void ntfs_evt_destroy(ntfsv_ntfs_evt_t *evt);

const char *ha_state_str(SaAmfHAStateT state);
extern uint32_t initialize_for_assignment(ntfs_cb_t *cb,
                                          SaAmfHAStateT ha_state);
#endif  // NTF_NTFD_NTFS_H_
