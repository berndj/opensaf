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

#ifndef LOG_LOGD_LGS_H_
#define LOG_LOGD_LGS_H_

/* ========================================================================
 *   INCLUDE FILES
 * ========================================================================
 */

#include <cstdlib>
#include <stdint.h>

#include "base/ncs_mda_pvt.h"
#include "mbc/mbcsv_papi.h"
#include "base/ncs_edu_pub.h"
#include "base/ncs_util.h"
#include <saAis.h>
#include "base/saf_error.h"
#include <saImmOm.h>
#include <saAmf.h>

/* LGS files */
#include "log/common/lgsv_defs.h"
#include "lgs_cb.h"
#include "log/common/lgsv_msg.h"
#include "lgs_evt.h"
#include "lgs_mbcsv.h"
#include "log/logd/lgs_util.h"
#include "lgs_stream.h"
#include "lgs_config.h"
#include "lgs_imm_gcfg.h"

/* ========================================================================
 *   DEFINITIONS
 * ========================================================================
 */

/* Hard Coded in test!!! FIX */
#define ALARM_LOG_STREAM_ID 0
#define NOTIFICATION_LOG_STREAM_ID 1
#define SYSTEM_LOG_STREAM_ID 2
#define APPLICATION_LOG_STREAM_ID 3
#define LGS_FORMAT_EXP_MAX 256
#define DEFAULT_FIXED_LOG_RECORD_SIZE 150
#define DEFAULT_ALM_ACTION "ROTATE"
#define DEFAULT_ALM_ACTION_ROTATE 3
#define DEFAULT_MAX_LOG_FILE_SIZE 500000
#define DEFAULT_MAX_FILES_ROTATED 4
#define DEFAULT_APP_MAX_LOG_FILE_SIZE 800000
#define DEFAULT_APP_NUM_ROTATIONS 4

/* IPC priorities used when adding messages to the LOG mailbox */
#define LGS_IPC_PRIO_CTRL_MSGS NCS_IPC_PRIORITY_VERY_HIGH
#define LGS_IPC_PRIO_ALARM_STREAM NCS_IPC_PRIORITY_HIGH
#define LGS_IPC_PRIO_SYS_STREAM NCS_IPC_PRIORITY_NORMAL
#define LGS_IPC_PRIO_APP_STREAM NCS_IPC_PRIORITY_LOW

/* The name of log service config object */
#define LGS_IMM_LOG_CONFIGURATION "logConfig=1,safApp=safLogService"

/* The possible configurations for LGS_IMM_LOG_FILE_SYS_CONFIG */
#define LGS_LOG_SHARED_FILESYSTEM 1 /* Use shared filesystem. Default */
#define LGS_LOG_SPLIT_FILESYSTEM          \
  2 /* Store logs on local file system on \
       each node */

/* ========================================================================
 *   DATA DECLARATIONS
 * ========================================================================
 */
extern lgs_cb_t *lgs_cb;
extern SYSF_MBX lgs_mbx;
extern uint32_t mbox_high[NCS_IPC_PRIORITY_MAX];
extern uint32_t mbox_msgs[NCS_IPC_PRIORITY_MAX];
extern bool mbox_full[NCS_IPC_PRIORITY_MAX];
extern uint32_t mbox_low[NCS_IPC_PRIORITY_MAX];
extern pthread_mutex_t lgs_mbox_init_mutex;
extern pthread_mutex_t lgs_OI_init_mutex;

extern uint32_t initialize_for_assignment(lgs_cb_t *cb, SaAmfHAStateT ha_state);

extern uint32_t lgs_configure_mailbox();

extern uint32_t lgs_mds_init(lgs_cb_t *cb, SaAmfHAStateT ha_state);
extern uint32_t lgs_mds_finalize(lgs_cb_t *cb);
extern uint32_t lgs_mds_change_role(lgs_cb_t *cb);
extern uint32_t lgs_mds_msg_send(lgs_cb_t *cb, lgsv_msg_t *msg, MDS_DEST *dest,
                                 MDS_SYNC_SND_CTXT *mds_ctxt,
                                 MDS_SEND_PRIORITY_TYPE prio);

extern SaAisErrorT lgs_imm_create_configStream(lgs_cb_t *cb);
extern void logRootDirectory_filemove(const std::string &new_logRootDirectory,
                                      const std::string &old_logRootDirectory,
                                      time_t *cur_time_in);
extern void logDataGroupname_fileown(const char *new_logDataGroupname);

extern void lgs_imm_impl_reinit_nonblocking(lgs_cb_t *cb);
extern void lgs_imm_init_OI_handle(SaImmOiHandleT *immOiHandle,
                                   SaSelectionObjectT *immSelectionObject);
extern void lgs_imm_impl_set(SaImmOiHandleT *immOiHandle,
                             SaSelectionObjectT *immSelectionObject);
extern SaAisErrorT lgs_imm_init_configStreams(lgs_cb_t *cb);

// Functions for recovery handling
void lgs_cleanup_abandoned_streams();
void lgs_delete_one_stream_object(const std::string &name_str);
void lgs_search_stream_objects();
SaUint32T *lgs_get_scAbsenceAllowed_attr(SaUint32T *attr_val);
int lgs_get_streamobj_attr(SaImmAttrValuesT_2 ***attrib_out,
                           const std::string &object_name,
                           SaImmHandleT *immOmHandle);
int lgs_free_streamobj_attr(SaImmHandleT immHandle);

#endif  // LOG_LOGD_LGS_H_
