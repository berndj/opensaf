/*      -*- OpenSAF  -*-
 * File:   lgs_mbcsv.h
 *
 * (C) Copyright 2008 The OpenSAF Foundation
 * Copyright Ericsson AB 2008, 2017 - All Rights Reserved.
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

#ifndef LOG_LOGD_LGS_MBCSV_H_
#define LOG_LOGD_LGS_MBCSV_H_

#include <stdint.h>
#include "amf/saf/saAmf.h"

/* Version 1: Logservice check-point version older versions than OpenSAF 4.4.
 *            Cannot be configured for split filesystem.
 * Version 2: Check-pointing version used in OpenSAF version 4.4 after
 *            enhancement for handling split file system was added. See #112 and
 *            #688. Can handle version 1 on other node.
 * Version 3: Runtime configuration of mailbox limits in the log service
 *            configuration object are allowed but shall only be allowed if
 *            supported on both nodes. For the moment check-pointing is not
 *            changed from version 2. Instead the configuration object is always
 *            read when changing from standby to active.
 */
#define LGS_MBCSV_VERSION_1 1
#define LGS_MBCSV_VERSION_2 2
#define LGS_MBCSV_VERSION_4 4
#define LGS_MBCSV_VERSION_5 5
#define LGS_MBCSV_VERSION_6 6

/* Current version */
#define LGS_MBCSV_VERSION 6
#define LGS_MBCSV_VERSION_MIN 1

/* Checkpoint message types(Used as 'reotype' w.r.t mbcsv)  */

/* Checkpoint update messages are processed similar to lgsv internal
 * messages handling, so the types are defined such that the values
 * can be mapped straight with lgsv message types
 */

typedef enum {
  LGS_CKPT_CLIENT_INITIALIZE = 0,
  LGS_CKPT_CLIENT_FINALIZE = 1,
  LGS_CKPT_CLIENT_DOWN = 2,
  LGS_CKPT_LOG_WRITE = 3,
  LGS_CKPT_OPEN_STREAM = 4,
  LGS_CKPT_CLOSE_STREAM = 5,
  LGS_CKPT_CFG_STREAM = 6,
  LGS_CKPT_LGS_CFG = 7,
  LGS_CKPT_LGS_CFG_V3 = 8,
  LGS_CKPT_LGS_CFG_V5 = 9,
  LGS_CKPT_MSG_MAX
} lgsv_ckpt_msg_type_t;

/* Checkpoint message containing lgs data of a particular type.
 * Used during cold and async updates.
 */
typedef struct {
  lgsv_ckpt_msg_type_t
      ckpt_rec_type;         /* Type of lgs data carried in this checkpoint */
  uint32_t num_ckpt_records; /* =1 for async updates,>=1 for cold sync */
  uint32_t data_len; /* Total length of encoded checkpoint data of this type */
} lgsv_ckpt_header_t;

/* Initialize checkpoint record, used in cold/async checkpoint updates */
typedef struct {
  uint32_t client_id; /* Client Id at Active */
  MDS_DEST mds_dest;  /* Handy when an LGA instance goes away */
  lgs_stream_list_t *stream_list;
} lgs_ckpt_initialize_msg_t;

typedef struct {
  uint32_t streamId;
  uint32_t clientId;
  /* correspond to SaLogFileCreateAttributes */
  char *logFile;            /* log file name */
  char *logPath;            /* log file path */
  uint64_t maxFileSize;     /* max file size configurable */
  int32_t maxLogRecordSize; /* constant size of the records */
  SaLogFileFullActionT logFileFullAction;
  int32_t maxFilesRotated; /* max number of rotation files configurable */
  char *fileFmt;
  /* end correspond to SaLogFileCreateAttributes */
  char *logStreamName;
  uint64_t creationTimeStamp;
  uint32_t numOpeners;
  char *logFileCurrent;
  char *dest_names;
  logStreamTypeT streamType;
  uint32_t logRecordId; /* log record identifier increased for each record */
} lgs_ckpt_stream_open_t;

uint32_t lgs_mbcsv_init(lgs_cb_t *lgs_cb, SaAmfHAStateT ha_state);
uint32_t lgs_mbcsv_change_HA_state(lgs_cb_t *cb, SaAmfHAStateT ha_state);
bool lgs_is_peer_v2();
bool lgs_is_peer_v3();
bool lgs_is_peer_v4();
bool lgs_is_peer_v5();
bool lgs_is_peer_v6();
bool lgs_is_split_file_system();
uint32_t lgs_mbcsv_dispatch(NCS_MBCSV_HDL mbcsv_hdl);
void lgs_free_edu_mem(char *ptr);
void lgs_ckpt_stream_open_set(log_stream_t *logStream,
                              lgs_ckpt_stream_open_t *stream_open,
                              const uint32_t &client_id);
uint32_t edp_ed_header_rec(EDU_HDL *edu_hdl, EDU_TKN *edu_tkn, NCSCONTEXT ptr,
                           uint32_t *ptr_data_len, EDU_BUF_ENV *buf_env,
                           EDP_OP_TYPE op, EDU_ERR *o_err);
int32_t ckpt_msg_test_type(NCSCONTEXT arg);

uint32_t edp_ed_stream_list(EDU_HDL *edu_hdl, EDU_TKN *edu_tkn, NCSCONTEXT ptr,
                            uint32_t *ptr_data_len, EDU_BUF_ENV *buf_env,
                            EDP_OP_TYPE op, EDU_ERR *o_err);
uint32_t edp_ed_reg_rec(EDU_HDL *edu_hdl, EDU_TKN *edu_tkn, NCSCONTEXT ptr,
                        uint32_t *ptr_data_len, EDU_BUF_ENV *buf_env,
                        EDP_OP_TYPE op, EDU_ERR *o_err);
uint32_t lgs_ckpt_send_async(lgs_cb_t *cb, void *ckpt_rec, uint32_t action);
uint32_t edp_ed_open_stream_rec(EDU_HDL *edu_hdl, EDU_TKN *edu_tkn,
                                NCSCONTEXT ptr, uint32_t *ptr_data_len,
                                EDU_BUF_ENV *buf_env, EDP_OP_TYPE op,
                                EDU_ERR *o_err);

#endif  // LOG_LOGD_LGS_MBCSV_H_
