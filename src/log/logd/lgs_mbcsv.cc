/*      -*- OpenSAF  -*-
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

#include "log/logd/lgs.h"
#include "base/ncssysf_def.h"
#include "base/ncssysf_mem.h"
#include "base/osaf_time.h"

#include "osaf/immutil/immutil.h"
#include "log/logd/lgs_dest.h"
#include "log/logd/lgs_mbcsv_v6.h"
#include "log/logd/lgs_mbcsv_v5.h"
#include "log/logd/lgs_mbcsv_v3.h"
#include "log/logd/lgs_mbcsv_v2.h"
#include "log/logd/lgs_mbcsv_v1.h"
#include "log/logd/lgs_recov.h"

/*
  LGS_CKPT_DATA_HEADER
  4                4               4                 2
  -----------------------------------------------------------------
  | ckpt_rec_type | num_ckpt_records | tot_data_len |  checksum   |
  -----------------------------------------------------------------

  LGSV_CKPT_COLD_SYNC_MSG
  -----------------------------------------------------------------------------------------------------------------------
  | LGS_CKPT_DATA_HEADER|LGSV_CKPT_REC 1st| next |LGSV_CKPT_REC 2nd| next
  ..|..|..|..|LGSV_CKPT_REC "num_ckpt_records" th |
  -----------------------------------------------------------------------------------------------------------------------
*/

/**
 * Note on MBCSV checkpoint versions for log service configuration
 * ---------------------------------------------------------------
 * Version 2: Only log root directory and close timestamp are checkpointed.
 *            The data structure is `lgsv_ckpt_msg_v2_t` which is used to
 * replicate to standby node.
 *
 * Version 3: No change in the checkpoint data structure. Means
 * `lgsv_ckpt_msg_v2_t` is re-used. This version was introduced to handle the
 * rule on changing mailbox limits. Therefore, the data processing
 * for checkpoint version 3 is same as version 2's.
 *
 * Version 4: Added group name to data structure. A new data structure was
 * introduced `lgsv_ckpt_msg_v3_t`.
 *
 * Version 5: Was introduced to avoid creating new checkpoint version if any
 * added/changed configuration parameters.
 *
 * Version 6: Added client version to initialized message data structure. A new
 * data structure is lgsv_ckpt_msg_v6_t which is used for checkpoint.
 *
 */

static uint32_t ckpt_proc_initialize_client(lgs_cb_t *cb, void *data);
static uint32_t ckpt_proc_finalize_client(lgs_cb_t *cb, void *data);
static uint32_t ckpt_proc_agent_down(lgs_cb_t *cb, void *data);
static uint32_t ckpt_proc_log_write(lgs_cb_t *cb, void *data);
static uint32_t ckpt_proc_open_stream(lgs_cb_t *cb, void *data);
static uint32_t ckpt_proc_close_stream(lgs_cb_t *cb, void *data);
static uint32_t ckpt_proc_cfg_stream(lgs_cb_t *cb, void *data);

static void enc_ckpt_header(uint8_t *pdata, lgsv_ckpt_header_t header);
static uint32_t dec_ckpt_header(NCS_UBAID *uba, lgsv_ckpt_header_t *header);
static uint32_t ckpt_decode_cbk_handler(NCS_MBCSV_CB_ARG *cbk_arg);
static uint32_t mbcsv_callback(
    NCS_MBCSV_CB_ARG *arg); /* Common Callback interface to mbcsv */
static uint32_t ckpt_decode_async_update(lgs_cb_t *cb,
                                         NCS_MBCSV_CB_ARG *cbk_arg);

static uint32_t ckpt_encode_cbk_handler(NCS_MBCSV_CB_ARG *cbk_arg);
static uint32_t ckpt_enc_cold_sync_data(lgs_cb_t *lgs_cb,
                                        NCS_MBCSV_CB_ARG *cbk_arg,
                                        bool data_req);
static uint32_t ckpt_encode_async_update(lgs_cb_t *lgs_cb, EDU_HDL edu_hdl,
                                         NCS_MBCSV_CB_ARG *cbk_arg);
static uint32_t ckpt_decode_cold_sync(lgs_cb_t *cb, NCS_MBCSV_CB_ARG *cbk_arg);
static uint32_t ckpt_peer_info_cbk_handler(NCS_MBCSV_CB_ARG *arg);
static uint32_t ckpt_notify_cbk_handler(NCS_MBCSV_CB_ARG *arg);
static uint32_t ckpt_err_ind_cbk_handler(NCS_MBCSV_CB_ARG *arg);

static uint32_t edu_enc_reg_list(lgs_cb_t *cb, NCS_UBAID *uba);
static uint32_t edu_enc_streams(lgs_cb_t *cb, NCS_UBAID *uba);
static uint32_t process_ckpt_data(lgs_cb_t *cb, void *data);

typedef uint32_t (*LGS_CKPT_HDLR)(lgs_cb_t *cb, void *data);

static LGS_CKPT_HDLR ckpt_data_handler[] = {
    ckpt_proc_initialize_client, ckpt_proc_finalize_client,
    ckpt_proc_agent_down,        ckpt_proc_log_write,
    ckpt_proc_open_stream,       ckpt_proc_close_stream,
    ckpt_proc_cfg_stream,        ckpt_proc_lgs_cfg_v2,
    ckpt_proc_lgs_cfg_v3,        ckpt_proc_lgs_cfg_v5};

/****************************************************************************
 * Name          : edp_ed_open_stream_rec
 *
 * Description   : This function is an EDU program for encoding/decoding
 *                 lgsv checkpoint open_stream log rec.
 *
 * Arguments     : EDU_HDL - pointer to edu handle,
 *                 EDU_TKN - internal edu token to help encode/decode,
 *                 POINTER to the structure to encode/decode from/to,
 *                 data length specifying number of structures,
 *                 EDU_BUF_ENV - pointer to buffer for encoding/decoding.
 *                 op - operation type being encode/decode.
 *                 EDU_ERR - out param to indicate errors in processing.
 *
 * Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE
 *
 * Notes         : None.
 *****************************************************************************/

uint32_t edp_ed_open_stream_rec(EDU_HDL *edu_hdl, EDU_TKN *edu_tkn,
                                NCSCONTEXT ptr, uint32_t *ptr_data_len,
                                EDU_BUF_ENV *buf_env, EDP_OP_TYPE op,
                                EDU_ERR *o_err) {
  uint32_t rc = NCSCC_RC_SUCCESS;
  lgs_ckpt_stream_open_t *ckpt_open_stream_msg_ptr = NULL,
                         **ckpt_open_stream_msg_dec_ptr;
  if (lgs_is_peer_v6()) {
    EDU_INST_SET ckpt_open_stream_rec_ed_rules[] = {
        {EDU_START, edp_ed_open_stream_rec, 0, 0, 0,
         sizeof(lgs_ckpt_stream_open_t), 0, NULL},
        {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
         (long)&((lgs_ckpt_stream_open_t *)0)->streamId, 0, NULL},
        {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
         (long)&((lgs_ckpt_stream_open_t *)0)->clientId, 0, NULL},
        {EDU_EXEC, ncs_edp_string, 0, 0, 0,
         (long)&((lgs_ckpt_stream_open_t *)0)->logFile, 0, NULL},
        {EDU_EXEC, ncs_edp_string, 0, 0, 0,
         (long)&((lgs_ckpt_stream_open_t *)0)->logPath, 0, NULL},
        {EDU_EXEC, ncs_edp_string, 0, 0, 0,
         (long)&((lgs_ckpt_stream_open_t *)0)->logFileCurrent, 0, NULL},
        {EDU_EXEC, ncs_edp_string, 0, 0, 0,
         (long)&((lgs_ckpt_stream_open_t *)0)->dest_names, 0, NULL},
        {EDU_EXEC, ncs_edp_uns64, 0, 0, 0,
         (long)&((lgs_ckpt_stream_open_t *)0)->maxFileSize, 0, NULL},
        {EDU_EXEC, ncs_edp_int32, 0, 0, 0,
         (long)&((lgs_ckpt_stream_open_t *)0)->maxLogRecordSize, 0, NULL},
        {EDU_EXEC, ncs_edp_int32, 0, 0, 0,
         (long)&((lgs_ckpt_stream_open_t *)0)->logFileFullAction, 0, NULL},
        {EDU_EXEC, ncs_edp_int32, 0, 0, 0,
         (long)&((lgs_ckpt_stream_open_t *)0)->maxFilesRotated, 0, NULL},
        {EDU_EXEC, ncs_edp_string, 0, 0, 0,
         (long)&((lgs_ckpt_stream_open_t *)0)->fileFmt, 0, NULL},
        {EDU_EXEC, ncs_edp_string, 0, 0, 0,
         (long)&((lgs_ckpt_stream_open_t *)0)->logStreamName, 0, NULL},
        {EDU_EXEC, ncs_edp_uns64, 0, 0, 0,
         (long)&((lgs_ckpt_stream_open_t *)0)->creationTimeStamp, 0, NULL},
        {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
         (long)&((lgs_ckpt_stream_open_t *)0)->numOpeners, 0, NULL},
        {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
         (long)&((lgs_ckpt_stream_open_t *)0)->streamType, 0, NULL},
        {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
         (long)&((lgs_ckpt_stream_open_t *)0)->logRecordId, 0, NULL},
        {EDU_END, 0, 0, 0, 0, 0, 0, NULL},
    };

    if (op == EDP_OP_TYPE_ENC) {
      ckpt_open_stream_msg_ptr = static_cast<lgs_ckpt_stream_open_t *>(ptr);
    } else if (op == EDP_OP_TYPE_DEC) {
      ckpt_open_stream_msg_dec_ptr =
          static_cast<lgs_ckpt_stream_open_t **>(ptr);
      if (*ckpt_open_stream_msg_dec_ptr == NULL) {
        *o_err = EDU_ERR_MEM_FAIL;
        return NCSCC_RC_FAILURE;
      }
      memset(*ckpt_open_stream_msg_dec_ptr, '\0',
             sizeof(lgs_ckpt_stream_open_t));
      ckpt_open_stream_msg_ptr = *ckpt_open_stream_msg_dec_ptr;
    } else {
      ckpt_open_stream_msg_ptr = static_cast<lgs_ckpt_stream_open_t *>(ptr);
    }

    rc = m_NCS_EDU_RUN_RULES(edu_hdl, edu_tkn, ckpt_open_stream_rec_ed_rules,
                             ckpt_open_stream_msg_ptr, ptr_data_len, buf_env,
                             op, o_err);
  } else {
    EDU_INST_SET ckpt_open_stream_rec_ed_rules[] = {
        {EDU_START, edp_ed_open_stream_rec, 0, 0, 0,
         sizeof(lgs_ckpt_stream_open_t), 0, NULL},
        {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
         (long)&((lgs_ckpt_stream_open_t *)0)->streamId, 0, NULL},
        {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
         (long)&((lgs_ckpt_stream_open_t *)0)->clientId, 0, NULL},
        {EDU_EXEC, ncs_edp_string, 0, 0, 0,
         (long)&((lgs_ckpt_stream_open_t *)0)->logFile, 0, NULL},
        {EDU_EXEC, ncs_edp_string, 0, 0, 0,
         (long)&((lgs_ckpt_stream_open_t *)0)->logPath, 0, NULL},
        {EDU_EXEC, ncs_edp_string, 0, 0, 0,
         (long)&((lgs_ckpt_stream_open_t *)0)->logFileCurrent, 0, NULL},
        {EDU_EXEC, ncs_edp_uns64, 0, 0, 0,
         (long)&((lgs_ckpt_stream_open_t *)0)->maxFileSize, 0, NULL},
        {EDU_EXEC, ncs_edp_int32, 0, 0, 0,
         (long)&((lgs_ckpt_stream_open_t *)0)->maxLogRecordSize, 0, NULL},
        {EDU_EXEC, ncs_edp_int32, 0, 0, 0,
         (long)&((lgs_ckpt_stream_open_t *)0)->logFileFullAction, 0, NULL},
        {EDU_EXEC, ncs_edp_int32, 0, 0, 0,
         (long)&((lgs_ckpt_stream_open_t *)0)->maxFilesRotated, 0, NULL},
        {EDU_EXEC, ncs_edp_string, 0, 0, 0,
         (long)&((lgs_ckpt_stream_open_t *)0)->fileFmt, 0, NULL},
        {EDU_EXEC, ncs_edp_string, 0, 0, 0,
         (long)&((lgs_ckpt_stream_open_t *)0)->logStreamName, 0, NULL},
        {EDU_EXEC, ncs_edp_uns64, 0, 0, 0,
         (long)&((lgs_ckpt_stream_open_t *)0)->creationTimeStamp, 0, NULL},
        {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
         (long)&((lgs_ckpt_stream_open_t *)0)->numOpeners, 0, NULL},
        {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
         (long)&((lgs_ckpt_stream_open_t *)0)->streamType, 0, NULL},
        {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
         (long)&((lgs_ckpt_stream_open_t *)0)->logRecordId, 0, NULL},
        {EDU_END, 0, 0, 0, 0, 0, 0, NULL},
    };

    if (op == EDP_OP_TYPE_ENC) {
      ckpt_open_stream_msg_ptr = static_cast<lgs_ckpt_stream_open_t *>(ptr);
    } else if (op == EDP_OP_TYPE_DEC) {
      ckpt_open_stream_msg_dec_ptr =
          static_cast<lgs_ckpt_stream_open_t **>(ptr);
      if (*ckpt_open_stream_msg_dec_ptr == NULL) {
        *o_err = EDU_ERR_MEM_FAIL;
        return NCSCC_RC_FAILURE;
      }
      memset(*ckpt_open_stream_msg_dec_ptr, '\0',
             sizeof(lgs_ckpt_stream_open_t));
      ckpt_open_stream_msg_ptr = *ckpt_open_stream_msg_dec_ptr;
    } else {
      ckpt_open_stream_msg_ptr = static_cast<lgs_ckpt_stream_open_t *>(ptr);
    }

    rc = m_NCS_EDU_RUN_RULES(edu_hdl, edu_tkn, ckpt_open_stream_rec_ed_rules,
                             ckpt_open_stream_msg_ptr, ptr_data_len, buf_env,
                             op, o_err);
  }

  return rc;
} /* End edp_ed_open_stream_rec */
/* End of EDU encode/decode functions */

/**
 * @param ptr
 */
void lgs_free_edu_mem(char *ptr) {
  m_NCS_MEM_FREE(ptr, NCS_MEM_REGION_PERSISTENT, NCS_SERVICE_ID_OS_SVCS, 0);
}

/****************************************************************************
 * Name          : lgsv_mbcsv_init
 *
 * Description   : This function initializes the mbcsv interface and
 *                 obtains a selection object from mbcsv.
 *
 * Arguments     : LGS_CB * - A pointer to the lgs control block.
 *
 * Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE
 *
 * Notes         : None.
 *****************************************************************************/
uint32_t lgs_mbcsv_init(lgs_cb_t *cb, SaAmfHAStateT ha_state) {
  uint32_t rc;
  NCS_MBCSV_ARG arg;

  TRACE_ENTER();

  /* Initialize with MBCSv library */
  arg.i_op = NCS_MBCSV_OP_INITIALIZE;
  arg.info.initialize.i_mbcsv_cb = mbcsv_callback;
  arg.info.initialize.i_version = LGS_MBCSV_VERSION;
  arg.info.initialize.i_service = NCS_SERVICE_ID_LGS;
  LOG_NO("LGS_MBCSV_VERSION = %d", LGS_MBCSV_VERSION);

  if ((rc = ncs_mbcsv_svc(&arg)) != NCSCC_RC_SUCCESS) {
    LOG_ER("NCS_MBCSV_OP_INITIALIZE FAILED");
    goto done;
  }

  cb->mbcsv_hdl = arg.info.initialize.o_mbcsv_hdl;

  /* Open a checkpoint */
  arg.i_op = NCS_MBCSV_OP_OPEN;
  arg.i_mbcsv_hdl = cb->mbcsv_hdl;
  arg.info.open.i_pwe_hdl = (uint32_t)cb->mds_hdl;
  arg.info.open.i_client_hdl = 0;

  if ((rc = ncs_mbcsv_svc(&arg)) != NCSCC_RC_SUCCESS) {
    LOG_ER("NCS_MBCSV_OP_OPEN FAILED");
    goto done;
  }
  cb->mbcsv_ckpt_hdl = arg.info.open.o_ckpt_hdl;

  /* Get Selection Object */
  arg.i_op = NCS_MBCSV_OP_SEL_OBJ_GET;
  arg.i_mbcsv_hdl = cb->mbcsv_hdl;
  arg.info.sel_obj_get.o_select_obj = 0;
  if (NCSCC_RC_SUCCESS != (rc = ncs_mbcsv_svc(&arg))) {
    LOG_ER("NCS_MBCSV_OP_SEL_OBJ_GET FAILED");
    goto done;
  }

  cb->mbcsv_sel_obj = arg.info.sel_obj_get.o_select_obj;
  cb->ckpt_state = COLD_SYNC_IDLE;

  /* Disable warm sync */
  arg.i_op = NCS_MBCSV_OP_OBJ_SET;
  arg.i_mbcsv_hdl = cb->mbcsv_hdl;
  arg.info.obj_set.i_ckpt_hdl = cb->mbcsv_ckpt_hdl;
  arg.info.obj_set.i_obj = NCS_MBCSV_OBJ_WARM_SYNC_ON_OFF;
  arg.info.obj_set.i_val = false;
  if (ncs_mbcsv_svc(&arg) != NCSCC_RC_SUCCESS) {
    LOG_ER("NCS_MBCSV_OP_OBJ_SET FAILED");
    goto done;
  }

  rc = lgs_mbcsv_change_HA_state(cb, ha_state);

done:
  TRACE_LEAVE();
  return rc;
}

/****************************************************************************
 * Name          : lgs_mbcsv_change_HA_state
 *
 * Description   : This function inform mbcsv of our HA state.
 *                 All checkpointing operations are triggered based on the
 *                 state.
 *
 * Arguments     : LGS_CB * - A pointer to the lgs control block.
 *
 * Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE
 *
 * Notes         : This function should be ideally called only once, i.e.
 *                 during the first CSI assignment from AVSv  .
 *****************************************************************************/

uint32_t lgs_mbcsv_change_HA_state(lgs_cb_t *cb, SaAmfHAStateT ha_state) {
  TRACE_ENTER();
  NCS_MBCSV_ARG mbcsv_arg;

  memset(&mbcsv_arg, '\0', sizeof(NCS_MBCSV_ARG));

  /* Set the mbcsv args */
  mbcsv_arg.i_op = NCS_MBCSV_OP_CHG_ROLE;
  mbcsv_arg.i_mbcsv_hdl = cb->mbcsv_hdl;
  mbcsv_arg.info.chg_role.i_ckpt_hdl = cb->mbcsv_ckpt_hdl;
  mbcsv_arg.info.chg_role.i_ha_state = ha_state;

  if (ncs_mbcsv_svc(&mbcsv_arg) != NCSCC_RC_SUCCESS) {
    LOG_ER("ncs_mbcsv_svc NCS_MBCSV_OP_CHG_ROLE FAILED");
    return NCSCC_RC_FAILURE;
  }

  /* If configured for split file system and becoming active some
   * stb stream parameters has to be copied to corresponding active. The
   * reason is that standby may have for example another current file in a
   * stream than the active and check-pointing is not done from standby to
   * active.
   */
  log_stream_t *stream;
  if (lgs_is_split_file_system()) {
    // Iterate all existing log streams in cluster.
    SaBoolT endloop = SA_FALSE, jstart = SA_TRUE;
    while ((stream = iterate_all_streams(endloop, jstart)) && !endloop) {
      jstart = SA_FALSE;
      if (ha_state == SA_AMF_HA_ACTIVE) {
        stream->logFileCurrent = stream->stb_logFileCurrent;
        stream->curFileSize = stream->stb_curFileSize;
        *stream->p_fd = -1; /* Reopen files */
      } else if (ha_state == SA_AMF_HA_QUIESCED) {
        stream->stb_logFileCurrent = stream->logFileCurrent;
        stream->stb_prev_actlogFileCurrent = stream->stb_logFileCurrent;
        stream->stb_curFileSize = stream->curFileSize;
        *stream->p_fd = -1; /* Reopen files */
      }
    }
  }

  TRACE_LEAVE();
  return NCSCC_RC_SUCCESS;
} /*End lgs_mbcsv_change_HA_state */

/**
 * Mbcsv dispatcher
 * @param mbcsv_hdl
 * @return
 */
uint32_t lgs_mbcsv_dispatch(NCS_MBCSV_HDL mbcsv_hdl) {
  NCS_MBCSV_ARG mbcsv_arg;

  memset(&mbcsv_arg, 0, sizeof(NCS_MBCSV_ARG));
  mbcsv_arg.i_op = NCS_MBCSV_OP_DISPATCH;
  mbcsv_arg.i_mbcsv_hdl = mbcsv_hdl;
  mbcsv_arg.info.dispatch.i_disp_flags = SA_DISPATCH_ALL;

  return ncs_mbcsv_svc(&mbcsv_arg);
}

/**
 * Check if peer is version 2 (or later)
 * @return bool
 */
bool lgs_is_peer_v2() {
  if (lgs_cb->mbcsv_peer_version >= LGS_MBCSV_VERSION_2) {
    return true;
  } else {
    return false;
  }
}

/**
 * Check if peer is version 4 (or later)
 * @return bool
 */
bool lgs_is_peer_v4() {
  if (lgs_cb->mbcsv_peer_version >= LGS_MBCSV_VERSION_4) {
    return true;
  } else {
    return false;
  }
}

/**
 * Check if peer is version 5 (or later)
 * @return bool
 */
bool lgs_is_peer_v5() {
  if (lgs_cb->mbcsv_peer_version >= LGS_MBCSV_VERSION_5) {
    return true;
  } else {
    return false;
  }
}

/**
 * Check if peer is version 6 (or later)
 * @return bool
 */
bool lgs_is_peer_v6() {
  if (lgs_cb->mbcsv_peer_version >= LGS_MBCSV_VERSION_6) {
    return true;
  } else {
    return false;
  }
}

/**
 * Check if configured for split file system.
 * If other node is version 1 split file system mode is not applicable.
 *
 * @return bool
 */
bool lgs_is_split_file_system() {
  SaUint32T lgs_file_config;
  lgs_file_config =
      *static_cast<const SaUint32T *>(lgs_cfg_get(LGS_IMM_LOG_FILE_SYS_CONFIG));

  if ((lgs_file_config == LGS_LOG_SPLIT_FILESYSTEM) && lgs_is_peer_v2()) {
    return true;
  } else {
    return false;
  }
}

/****************************************************************************
 * Name          : mbcsv_callback
 *
 * Description   : This callback is the single entry point for mbcsv to
 *                 notify lgs of all checkpointing operations.
 *
 * Arguments     : NCS_MBCSV_CB_ARG - Callback Info pertaining to the mbcsv
 *                 event from ACTIVE/STANDBY LGS peer.
 *
 * Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE
 *
 * Notes         : Based on the mbcsv message type, the corresponding mbcsv
 *                 message handler shall be invoked.
 *****************************************************************************/
static uint32_t mbcsv_callback(NCS_MBCSV_CB_ARG *arg) {
  uint32_t rc = NCSCC_RC_SUCCESS;

  osafassert(arg != NULL);

  switch (arg->i_op) {
    case NCS_MBCSV_CBOP_ENC:
      /* Encode Request from MBCSv */
      rc = ckpt_encode_cbk_handler(arg);
      break;
    case NCS_MBCSV_CBOP_DEC:
      /* Decode Request from MBCSv */
      rc = ckpt_decode_cbk_handler(arg);
      if (rc != NCSCC_RC_SUCCESS) TRACE("ckpt_decode_cbk_handler FAILED");
      break;
    case NCS_MBCSV_CBOP_PEER:
      /* LGS Peer info from MBCSv */
      rc = ckpt_peer_info_cbk_handler(arg);
      if (rc != NCSCC_RC_SUCCESS) TRACE("ckpt_peer_info_cbk_handler FAILED");
      break;
    case NCS_MBCSV_CBOP_NOTIFY:
      /* NOTIFY info from LGS peer */
      rc = ckpt_notify_cbk_handler(arg);
      if (rc != NCSCC_RC_SUCCESS) TRACE("ckpt_notify_cbk_handler FAILED");
      break;
    case NCS_MBCSV_CBOP_ERR_IND:
      /* Peer error indication info */
      rc = ckpt_err_ind_cbk_handler(arg);
      if (rc != NCSCC_RC_SUCCESS) TRACE("ckpt_err_ind_cbk_handler FAILED");
      break;
    default:
      rc = NCSCC_RC_FAILURE;
      if (rc != NCSCC_RC_SUCCESS) TRACE("default FAILED");
      break;
  }

  return rc;
}

/****************************************************************************
 * Name          : ckpt_encode_cbk_handler
 *
 * Description   : This function invokes the corresponding encode routine
 *                 based on the MBCSv encode request type.
 *
 * Arguments     : arg - Pointer to NCS_MBCSV_CB_ARG with encode info
 *
 * Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE
 *
 * Notes         : None.
 *****************************************************************************/

static uint32_t ckpt_encode_cbk_handler(NCS_MBCSV_CB_ARG *cbk_arg) {
  uint32_t rc = NCSCC_RC_SUCCESS;
  uint16_t mbcsv_version;

  osafassert(cbk_arg != NULL);

  mbcsv_version = m_NCS_MBCSV_FMT_GET(cbk_arg->info.encode.i_peer_version,
                                      LGS_MBCSV_VERSION, LGS_MBCSV_VERSION_MIN);
  if (0 == mbcsv_version) {
    TRACE("Wrong mbcsv_version!!!\n");
    return NCSCC_RC_FAILURE;
  }

  switch (cbk_arg->info.encode.io_msg_type) {
    case NCS_MBCSV_MSG_ASYNC_UPDATE:
      /* Encode async update */
      if ((rc = ckpt_encode_async_update(lgs_cb, lgs_cb->edu_hdl, cbk_arg)) !=
          NCSCC_RC_SUCCESS)
        TRACE("  ckpt_encode_async_update FAILED");
      break;

    case NCS_MBCSV_MSG_COLD_SYNC_REQ:
      TRACE_2("COLD SYNC REQ ENCODE CALLED");
      break;

    case NCS_MBCSV_MSG_COLD_SYNC_RESP:
      /* Encode cold sync response */
      rc = ckpt_enc_cold_sync_data(lgs_cb, cbk_arg, false);
      if (rc != NCSCC_RC_SUCCESS) {
        TRACE(" COLD SYNC ENCODE FAIL....");
      } else {
        lgs_cb->ckpt_state = COLD_SYNC_COMPLETE;
        TRACE_2(" COLD SYNC RESPONSE SEND SUCCESS....");
      }
      break;

    case NCS_MBCSV_MSG_WARM_SYNC_REQ:
    case NCS_MBCSV_MSG_WARM_SYNC_RESP:
      break;

    case NCS_MBCSV_MSG_DATA_REQ:
      break;

    case NCS_MBCSV_MSG_DATA_RESP:
    case NCS_MBCSV_MSG_DATA_RESP_COMPLETE:
      if ((rc = ckpt_enc_cold_sync_data(lgs_cb, cbk_arg, true)) !=
          NCSCC_RC_SUCCESS)
        TRACE("  ckpt_enc_cold_sync_data FAILED");
      break;
    default:
      rc = NCSCC_RC_FAILURE;
      TRACE("  default FAILED");
      break;
  } /*End switch(io_msg_type) */

  return rc;
} /*End ckpt_encode_cbk_handler() */

/****************************************************************************
 *                 Add logserv config (log root path)?
 * Name          : ckpt_enc_cold_sync_data
 *
 * Description   : This function encodes cold sync data., viz
 *                 1.REGLIST
 *                 2.OPEN STREAMS
 *                 3.Async Update Count
 *                 in that order.
 *                 Each records contain a header specifying the record type
 *                 and number of such records.
 *
 * Arguments     : lgs_cb - pointer to the lgs control block.
 *                 cbk_arg - Pointer to NCS_MBCSV_CB_ARG with encode info.
 *                 data_req - Flag to specify if its for cold sync or data
 *                 request for warm sync.
 * Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE
 *
 * Notes         : None.
 *****************************************************************************/

static uint32_t ckpt_enc_cold_sync_data(lgs_cb_t *lgs_cb,
                                        NCS_MBCSV_CB_ARG *cbk_arg,
                                        bool data_req) {
  /* asynsc Update Count */
  uint8_t *async_upd_cnt = NULL;

  /* Currently, we shall send all data in one send.
   * This shall avoid "delta data" problems that are associated during
   * multiple sends
   */
  TRACE_2("COLD SYNC ENCODE START........");
  /* Encode Registration list */
  uint32_t rc = edu_enc_reg_list(lgs_cb, &cbk_arg->info.encode.io_uba);
  if (rc != NCSCC_RC_SUCCESS) {
    TRACE("  edu_enc_reg_list FAILED");
    return NCSCC_RC_FAILURE;
  }

  rc = edu_enc_streams(lgs_cb, &cbk_arg->info.encode.io_uba);
  if (rc != NCSCC_RC_SUCCESS) {
    TRACE("  edu_enc_streams FAILED");
    return NCSCC_RC_FAILURE;
  }
  /* Encode the Async Update Count at standby */

  /* This will have the count of async updates that have been sent,
     this will be 0 initially */
  async_upd_cnt =
      ncs_enc_reserve_space(&cbk_arg->info.encode.io_uba, sizeof(uint32_t));
  if (async_upd_cnt == NULL) {
    /* Log this error */
    TRACE("  ncs_enc_reserve_space FAILED");
    return NCSCC_RC_FAILURE;
  }

  ncs_encode_32bit(&async_upd_cnt, lgs_cb->async_upd_cnt);
  ncs_enc_claim_space(&cbk_arg->info.encode.io_uba, sizeof(uint32_t));

  /* Set response mbcsv msg type to complete */
  if (data_req == true)
    cbk_arg->info.encode.io_msg_type = NCS_MBCSV_MSG_DATA_RESP_COMPLETE;
  else
    cbk_arg->info.encode.io_msg_type = NCS_MBCSV_MSG_COLD_SYNC_RESP_COMPLETE;
  TRACE_2("COLD SYNC ENCODE END........");
  return rc;
} /*End  ckpt_enc_cold_sync_data() */

/**
 * Set parameters for check-pointing the opened stream
 *
 * @param logStream
 * @param stream_open
 * @param client_id
 * @return
 */
void lgs_ckpt_stream_open_set(log_stream_t *logStream,
                              lgs_ckpt_stream_open_t *stream_open,
                              const uint32_t &client_id) {
  memset(stream_open, 0, sizeof(lgs_ckpt_stream_open_t));
  stream_open->clientId = client_id;
  stream_open->streamId = logStream->streamId;
  stream_open->logFile = const_cast<char *>(logStream->fileName.c_str());
  stream_open->logPath = const_cast<char *>(logStream->pathName.c_str());
  stream_open->logFileCurrent =
      const_cast<char *>(logStream->logFileCurrent.c_str());
  stream_open->fileFmt = logStream->logFileFormat;
  stream_open->logStreamName = const_cast<char *>(logStream->name.c_str());
  stream_open->dest_names =
      const_cast<char *>(logStream->stb_dest_names.c_str());
  stream_open->maxFileSize = logStream->maxLogFileSize;
  stream_open->maxLogRecordSize = logStream->fixedLogRecordSize;
  stream_open->logFileFullAction = logStream->logFullAction;
  stream_open->maxFilesRotated = logStream->maxFilesRotated;
  stream_open->creationTimeStamp = logStream->creationTimeStamp;
  stream_open->numOpeners = logStream->numOpeners;
  stream_open->streamType = logStream->streamType;
  stream_open->logRecordId = logStream->logRecordId;
}

/**
 * Encode all existing streams. Used for cold sync
 * @param cb
 * @param uba
 * @return
 */
static uint32_t edu_enc_streams(lgs_cb_t *cb, NCS_UBAID *uba) {
  log_stream_t *log_stream_rec = NULL;
  lgs_ckpt_stream_open_t *ckpt_stream_rec;
  EDU_ERR ederror;
  uint32_t rc = NCSCC_RC_SUCCESS, num_rec = 0;
  uint8_t *pheader = NULL;
  lgsv_ckpt_header_t ckpt_hdr;

  /* Prepare reg. structure to encode */
  ckpt_stream_rec = static_cast<lgs_ckpt_stream_open_t *>(
      malloc(sizeof(lgs_ckpt_stream_open_t)));
  if (ckpt_stream_rec == NULL) {
    LOG_WA("malloc FAILED");
    return (NCSCC_RC_FAILURE);
  }

  /*Reserve space for "Checkpoint Header" */
  pheader = ncs_enc_reserve_space(uba, sizeof(lgsv_ckpt_header_t));
  if (pheader == NULL) {
    TRACE("ncs_enc_reserve_space FAILED");
    free(ckpt_stream_rec);
    return (rc = EDU_ERR_MEM_FAIL);
  }
  ncs_enc_claim_space(uba, sizeof(lgsv_ckpt_header_t));

  /* Walk through the reg list and encode record by record */
  // Iterate all existing log streams in cluster.
  SaBoolT endloop = SA_FALSE, jstart = SA_TRUE;
  while ((log_stream_rec = iterate_all_streams(endloop, jstart)) && !endloop) {
    jstart = SA_FALSE;
    // Encode the stream with invalid clientId for cold sync
    lgs_ckpt_stream_open_set(log_stream_rec, ckpt_stream_rec, -1);
    rc = m_NCS_EDU_EXEC(&cb->edu_hdl, edp_ed_open_stream_rec, uba,
                        EDP_OP_TYPE_ENC, ckpt_stream_rec, &ederror);

    if (rc != NCSCC_RC_SUCCESS) {
      m_NCS_EDU_PRINT_ERROR_STRING(ederror);
      TRACE("m_NCS_EDU_EXEC FAILED");
      free(ckpt_stream_rec);
      return rc;
    }
    ++num_rec;
  } /* End while RegRec */

  /* Encode RegHeader */
  ckpt_hdr.ckpt_rec_type = LGS_CKPT_OPEN_STREAM;
  ckpt_hdr.num_ckpt_records = num_rec;
  ckpt_hdr.data_len = 0; /*Not in Use for Cold Sync */

  enc_ckpt_header(pheader, ckpt_hdr);

  free(ckpt_stream_rec);

  return NCSCC_RC_SUCCESS;
}

/****************************************************************************
 * Name          : edu_enc_reg_list
 *
 * Description   : This function walks through the reglist and encodes all
 *                 records in the reglist, using the edps defined for the
 *                 same.
 *
 * Arguments     : cb - Pointer to LGS control block.
 *                 uba - Pointer to ubaid provided by mbcsv
 *
 * Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE
 *
 * Notes         : None.
 ***************************************************************************/

static uint32_t edu_enc_reg_list(lgs_cb_t *cb, NCS_UBAID *uba) {
  log_client_t *client = NULL;
  lgs_ckpt_initialize_msg_t ckpt_reg_rec;
  lgs_ckpt_initialize_msg_v6_t ckpt_reg_rec_v6;
  void *ckpt_client_reg;
  EDU_ERR ederror;
  uint32_t rc = NCSCC_RC_SUCCESS, num_rec = 0;
  lgsv_ckpt_header_t ckpt_hdr;
  EDU_PROG_HANDLER edp_function_reg = NULL;

  TRACE_ENTER();

  /*Reserve space for "Checkpoint Header" */
  uint8_t *pheader = ncs_enc_reserve_space(uba, sizeof(lgsv_ckpt_header_t));
  if (pheader == NULL) {
    TRACE("  ncs_enc_reserve_space FAILED");
    return (rc = EDU_ERR_MEM_FAIL);
  }
  ncs_enc_claim_space(uba, sizeof(lgsv_ckpt_header_t));
  /* Loop through Client DB */
  ClientMap *clientMap(reinterpret_cast<ClientMap *>(client_db));
  for (const auto &value : *clientMap) {
    client = value.second;

    if (lgs_is_peer_v6()) {
      ckpt_reg_rec_v6.client_id = client->client_id;
      ckpt_reg_rec_v6.mds_dest = client->mds_dest;
      ckpt_reg_rec_v6.stream_list = client->stream_list_root;
      ckpt_reg_rec_v6.client_ver = client->client_ver;
      ckpt_client_reg = &ckpt_reg_rec_v6;
      edp_function_reg = edp_ed_reg_rec_v6;
    } else {
      ckpt_reg_rec.client_id = client->client_id;
      ckpt_reg_rec.mds_dest = client->mds_dest;
      ckpt_reg_rec.stream_list = client->stream_list_root;
      ckpt_client_reg = &ckpt_reg_rec;
      edp_function_reg = edp_ed_reg_rec;
    }

    rc = m_NCS_EDU_EXEC(&cb->edu_hdl, edp_function_reg, uba, EDP_OP_TYPE_ENC,
                        ckpt_client_reg, &ederror);

    if (rc != NCSCC_RC_SUCCESS) {
      m_NCS_EDU_PRINT_ERROR_STRING(ederror);
      TRACE("  m_NCS_EDU_EXEC FAILED");
      return rc;
    }
    ++num_rec;

  } /* End while RegRec */

  /* Encode RegHeader */
  ckpt_hdr.ckpt_rec_type = LGS_CKPT_CLIENT_INITIALIZE;
  ckpt_hdr.num_ckpt_records = num_rec;
  ckpt_hdr.data_len = 0; /*Not in Use for Cold Sync */

  enc_ckpt_header(pheader, ckpt_hdr);

  TRACE_LEAVE();
  return NCSCC_RC_SUCCESS;
} /* End edu_enc_reg_list() */

/****************************************************************************
 * Name          : ckpt_encode_async_update
 *
 * Description   : This function encodes data to be sent as an async update.
 *                 The caller of this function would set the address of the
 *                 record to be encoded in the reo_hdl field(while invoking
 *                 SEND_CKPT option of ncs_mbcsv_svc.
 *
 * Arguments     : cb - pointer to the LGS control block.
 *                 cbk_arg - Pointer to NCS MBCSV callback argument struct.
 *
 * Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE
 *
 * Notes         : None.
 ****************************************************************************/

static uint32_t ckpt_encode_async_update(lgs_cb_t *lgs_cb, EDU_HDL edu_hdl,
                                         NCS_MBCSV_CB_ARG *cbk_arg) {
  lgsv_ckpt_msg_v6_t *data_v6 = NULL;
  lgsv_ckpt_msg_v5_t *data_v5 = NULL;
  lgsv_ckpt_msg_v3_t *data_v3 = NULL;
  lgsv_ckpt_msg_v2_t *data_v2 = NULL;
  lgsv_ckpt_msg_v1_t *data_v1 = NULL;
  void *vdata = NULL;
  EDU_ERR ederror;
  uint32_t rc = NCSCC_RC_SUCCESS;
  EDU_PROG_HANDLER edp_function;

  TRACE_ENTER();
  /* Set reo_hdl from callback arg to ckpt_rec */
  if (lgs_is_peer_v6()) {
    data_v6 = reinterpret_cast<lgsv_ckpt_msg_v6_t *>(
        static_cast<long>(cbk_arg->info.encode.io_reo_hdl));
    vdata = data_v6;
    edp_function = edp_ed_ckpt_msg_v6;
  } else if (lgs_is_peer_v5()) {
    data_v5 = reinterpret_cast<lgsv_ckpt_msg_v5_t *>(
        static_cast<long>(cbk_arg->info.encode.io_reo_hdl));
    vdata = data_v5;
    edp_function = edp_ed_ckpt_msg_v5;
  } else if (lgs_is_peer_v4()) {
    data_v3 = reinterpret_cast<lgsv_ckpt_msg_v3_t *>(
        static_cast<long>(cbk_arg->info.encode.io_reo_hdl));
    vdata = data_v3;
    edp_function = edp_ed_ckpt_msg_v3;
  } else if (lgs_is_peer_v2()) {
    data_v2 = reinterpret_cast<lgsv_ckpt_msg_v2_t *>(
        static_cast<long>(cbk_arg->info.encode.io_reo_hdl));
    vdata = data_v2;
    edp_function = edp_ed_ckpt_msg_v2;
  } else {
    data_v1 = reinterpret_cast<lgsv_ckpt_msg_v1_t *>(
        static_cast<long>(cbk_arg->info.encode.io_reo_hdl));
    vdata = data_v1;
    edp_function = edp_ed_ckpt_msg_v1;
  }

  if (vdata == NULL) {
    TRACE("data == NULL, FAILED");
    TRACE_LEAVE();
    return NCSCC_RC_FAILURE;
  }
  /* Encode async record,except publish & subscribe */
  rc = m_NCS_EDU_EXEC(&edu_hdl, edp_function, &cbk_arg->info.encode.io_uba,
                      EDP_OP_TYPE_ENC, vdata, &ederror);

  if (rc != NCSCC_RC_SUCCESS) {
    m_NCS_EDU_PRINT_ERROR_STRING(ederror);
    /* free(data); FIX ??? */
    TRACE_2("eduerr: %x", ederror);
    TRACE_LEAVE();
    return rc;
  }

  /* Update the Async Update Count at standby */
  lgs_cb->async_upd_cnt++;
  /* free (data); FIX??? */
  TRACE_LEAVE();
  return rc;
}

/****************************************************************************
 * Name          : ckpt_decode_cbk_handler
 *
 * Description   : This function is the single entry point to all decode
 *                 requests from mbcsv.
 *                 Invokes the corresponding decode routine based on the
 *                 MBCSv decode request type.
 *
 * Arguments     : arg - Pointer to NCS_MBCSV_CB_ARG with decode info
 *
 * Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE
 *
 * Notes         : None.
 *****************************************************************************/

static uint32_t ckpt_decode_cbk_handler(NCS_MBCSV_CB_ARG *cbk_arg) {
  uint32_t rc = NCSCC_RC_SUCCESS;
  uint16_t msg_fmt_version;

  osafassert(cbk_arg != NULL);

  msg_fmt_version =
      m_NCS_MBCSV_FMT_GET(cbk_arg->info.decode.i_peer_version,
                          LGS_MBCSV_VERSION, LGS_MBCSV_VERSION_MIN);
  if (0 == msg_fmt_version) {
    TRACE("wrong msg_fmt_version!!!\n");
    return NCSCC_RC_FAILURE;
  }

  switch (cbk_arg->info.decode.i_msg_type) {
    case NCS_MBCSV_MSG_COLD_SYNC_REQ:
      TRACE_2(" COLD SYNC REQ DECODE called");
      break;

    case NCS_MBCSV_MSG_COLD_SYNC_RESP:
    case NCS_MBCSV_MSG_COLD_SYNC_RESP_COMPLETE:
      TRACE_2(" COLD SYNC RESP DECODE called");
      if (lgs_cb->ckpt_state !=
          COLD_SYNC_COMPLETE) { /*this check is needed to handle repeated
                                   requests */
        if ((rc = ckpt_decode_cold_sync(lgs_cb, cbk_arg)) != NCSCC_RC_SUCCESS) {
          TRACE(" COLD SYNC RESPONSE DECODE ....");
        } else {
          TRACE_2(" COLD SYNC RESPONSE DECODE SUCCESS....");
          lgs_cb->ckpt_state = COLD_SYNC_COMPLETE;
        }
      }
      break;

    case NCS_MBCSV_MSG_ASYNC_UPDATE:
      TRACE_2(" ASYNC UPDATE DECODE called");
      if ((rc = ckpt_decode_async_update(lgs_cb, cbk_arg)) != NCSCC_RC_SUCCESS)
        TRACE("ckpt_decode_async_update FAILED %u", rc);
      break;

    case NCS_MBCSV_MSG_WARM_SYNC_REQ:
    case NCS_MBCSV_MSG_WARM_SYNC_RESP:
    case NCS_MBCSV_MSG_WARM_SYNC_RESP_COMPLETE:
    case NCS_MBCSV_MSG_DATA_REQ:
      TRACE_2("WARM SYNC called, not used");
      break;
    case NCS_MBCSV_MSG_DATA_RESP:
    case NCS_MBCSV_MSG_DATA_RESP_COMPLETE:
      TRACE_2("DATA RESP COMPLETE DECODE called");
      if ((rc = ckpt_decode_cold_sync(lgs_cb, cbk_arg)) != NCSCC_RC_SUCCESS)
        TRACE("   FAILED");
      break;

    default:
      TRACE_2(" INCORRECT DECODE called");
      rc = NCSCC_RC_FAILURE;
      TRACE("  INCORRECT DECODE called, FAILED");
      m_LEAP_DBG_SINK_VOID;
      break;
  } /*End switch(io_msg_type) */

  return rc;
} /*End ckpt_decode_cbk_handler() */

/****************************************************************************
 * Name          : ckpt_decode_async_update
 *
 * Description   : This function with helper functions decodes async update
 *                 data, based on the record type contained in the header.
 *
 * Arguments     : arg - Pointer to NCS_MBCSV_CB_ARG with decode info
 *                 cb - pointer to lgs cb.
 *
 * Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE
 *
 * Notes         : None.
 *****************************************************************************/
/* START ckpt_decode_async_update helper functions */
/**
 * Fill in the structure pointed to by "struct_ptr" with decoded data using
 * the "edp_function"
 * @param cb[in]
 * @param cbk_arg[in]
 * @param ckpt_msg[in]
 * @param struct_ptr[out]
 * @param edp_function[in]
 * @return
 */
static uint32_t ckpt_decode_log_struct(
    lgs_cb_t *cb,                  /* lgs cb data */
    NCS_MBCSV_CB_ARG *cbk_arg,     /* Mbcsv callback data */
    void *ckpt_msg,                /* Checkpointed message */
    void *struct_ptr,              /* Checkpointed structure */
    EDU_PROG_HANDLER edp_function) /* EDP function for decoding */
{
  EDU_ERR ederror;

  uint32_t rc =
      m_NCS_EDU_EXEC(&cb->edu_hdl, edp_function, &cbk_arg->info.decode.i_uba,
                     EDP_OP_TYPE_DEC, &struct_ptr, &ederror);

  if (rc != NCSCC_RC_SUCCESS) {
    m_NCS_EDU_PRINT_ERROR_STRING(ederror);
    TRACE("%s - ERROR m_NCS_EDU_EXEC rc = %d", __FUNCTION__, rc);
  }

  rc = process_ckpt_data(cb, ckpt_msg);
  return rc;
}

static uint32_t ckpt_decode_log_write(lgs_cb_t *cb, void *ckpt_msg,
                                      NCS_MBCSV_CB_ARG *cbk_arg) {
  uint32_t rc = NCSCC_RC_SUCCESS;
  void *write_log;
  EDU_PROG_HANDLER edp_function;
  const int sleep_delay_ms = 10;
  const int max_waiting_time_ms = 100;

  if (lgs_is_peer_v2()) {
    lgsv_ckpt_msg_v2_t *ckpt_msg_v2 =
        static_cast<lgsv_ckpt_msg_v2_t *>(ckpt_msg);
    write_log = &ckpt_msg_v2->ckpt_rec.write_log;
    edp_function = edp_ed_write_rec_v2;
  } else {
    lgsv_ckpt_msg_v1_t *ckpt_msg_v1 =
        static_cast<lgsv_ckpt_msg_v1_t *>(ckpt_msg);
    write_log = &ckpt_msg_v1->ckpt_rec.write_log;
    edp_function = edp_ed_write_rec_v1;
  }

  rc = ckpt_decode_log_struct(cb, cbk_arg, ckpt_msg, write_log, edp_function);

  /*
    When getting error return code (NCSCC_RC_REQ_TIMEOUT), means there are
    something happens in log handler thread, then we have to re-try it.

    One note is that, in time-out case,  allocated log record is freed
    by the log handler thread. Therefore, it is needed to re-allocate
    the log record again.
  */
  int msecs_waited = 0;
  while (lgs_is_split_file_system() && (rc == NCSCC_RC_REQ_TIMOUT) &&
         (msecs_waited < max_waiting_time_ms)) {
    usleep(sleep_delay_ms * 1000);
    msecs_waited += sleep_delay_ms;
    rc = ckpt_decode_log_struct(cb, cbk_arg, ckpt_msg, write_log, edp_function);
  }

  if (rc != NCSCC_RC_SUCCESS) {
    TRACE("%s - ckpt_decode_log_struct Fail", __FUNCTION__);
  }
  return rc;
}

static uint32_t ckpt_decode_log_close(lgs_cb_t *cb, void *ckpt_msg,
                                      NCS_MBCSV_CB_ARG *cbk_arg) {
  uint32_t rc = NCSCC_RC_SUCCESS;
  void *stream_close;
  EDU_PROG_HANDLER edp_function;

  if (lgs_is_peer_v2()) {
    lgsv_ckpt_msg_v2_t *ckpt_msg_v2 =
        static_cast<lgsv_ckpt_msg_v2_t *>(ckpt_msg);
    stream_close = &ckpt_msg_v2->ckpt_rec.stream_close;
    edp_function = edp_ed_close_stream_rec_v2;
  } else {
    lgsv_ckpt_msg_v1_t *ckpt_msg_v1 =
        static_cast<lgsv_ckpt_msg_v1_t *>(ckpt_msg);
    stream_close = &ckpt_msg_v1->ckpt_rec.stream_close;
    edp_function = edp_ed_close_stream_rec_v1;
  }

  rc =
      ckpt_decode_log_struct(cb, cbk_arg, ckpt_msg, stream_close, edp_function);
  if (rc != NCSCC_RC_SUCCESS) {
    TRACE("%s - ckpt_decode_log_struct Fail", __FUNCTION__);
  }
  return rc;
}

static uint32_t ckpt_decode_log_client_finalize(lgs_cb_t *cb, void *ckpt_msg,
                                                NCS_MBCSV_CB_ARG *cbk_arg) {
  uint32_t rc = NCSCC_RC_SUCCESS;
  void *finalize_client;
  EDU_PROG_HANDLER edp_function;

  if (lgs_is_peer_v2()) {
    lgsv_ckpt_msg_v2_t *ckpt_msg_v2 =
        static_cast<lgsv_ckpt_msg_v2_t *>(ckpt_msg);
    finalize_client = &ckpt_msg_v2->ckpt_rec.finalize_client;
    edp_function = edp_ed_finalize_rec_v2;
  } else {
    lgsv_ckpt_msg_v1_t *ckpt_msg_v1 =
        static_cast<lgsv_ckpt_msg_v1_t *>(ckpt_msg);
    finalize_client = &ckpt_msg_v1->ckpt_rec.finalize_client;
    edp_function = edp_ed_finalize_rec_v1;
  }

  rc = ckpt_decode_log_struct(cb, cbk_arg, ckpt_msg, finalize_client,
                              edp_function);
  if (rc != NCSCC_RC_SUCCESS) {
    TRACE("%s - ckpt_decode_log_struct Fail", __FUNCTION__);
  }
  return rc;
}

static uint32_t ckpt_decode_log_client_down(lgs_cb_t *cb, void *ckpt_msg,
                                            NCS_MBCSV_CB_ARG *cbk_arg) {
  uint32_t rc = NCSCC_RC_SUCCESS;
  void *client_down;
  EDU_PROG_HANDLER edp_function;

  if (lgs_is_peer_v2()) {
    lgsv_ckpt_msg_v2_t *ckpt_msg_v2 =
        static_cast<lgsv_ckpt_msg_v2_t *>(ckpt_msg);
    client_down = &ckpt_msg_v2->ckpt_rec.agent_down;
    edp_function = edp_ed_agent_down_rec_v2;
  } else {
    lgsv_ckpt_msg_v1_t *ckpt_msg_v1 =
        static_cast<lgsv_ckpt_msg_v1_t *>(ckpt_msg);
    client_down = &ckpt_msg_v1->ckpt_rec.agent_dest;
    edp_function = ncs_edp_mds_dest;
  }

  rc = ckpt_decode_log_struct(cb, cbk_arg, ckpt_msg, client_down, edp_function);
  if (rc != NCSCC_RC_SUCCESS) {
    TRACE("%s - ckpt_decode_log_struct Fail", __FUNCTION__);
  }
  return rc;
}

static uint32_t ckpt_decode_log_cfg_stream(lgs_cb_t *cb, void *ckpt_msg,
                                           NCS_MBCSV_CB_ARG *cbk_arg) {
  uint32_t rc = NCSCC_RC_SUCCESS;
  void *stream_cfg;
  EDU_PROG_HANDLER edp_function;

  if (lgs_is_peer_v6()) {
    lgsv_ckpt_msg_v6_t *ckpt_msg_v6 =
        static_cast<lgsv_ckpt_msg_v6_t *>(ckpt_msg);
    stream_cfg = &ckpt_msg_v6->ckpt_rec.stream_cfg;
    edp_function = edp_ed_cfg_stream_rec_v6;
  } else if (lgs_is_peer_v2()) {
    lgsv_ckpt_msg_v2_t *ckpt_msg_v2 =
        static_cast<lgsv_ckpt_msg_v2_t *>(ckpt_msg);
    stream_cfg = &ckpt_msg_v2->ckpt_rec.stream_cfg;
    edp_function = edp_ed_cfg_stream_rec_v2;
  } else {
    lgsv_ckpt_msg_v1_t *ckpt_msg_v1 =
        static_cast<lgsv_ckpt_msg_v1_t *>(ckpt_msg);
    stream_cfg = &ckpt_msg_v1->ckpt_rec.stream_cfg;
    edp_function = edp_ed_cfg_stream_rec_v1;
  }

  rc = ckpt_decode_log_struct(cb, cbk_arg, ckpt_msg, stream_cfg, edp_function);
  if (rc != NCSCC_RC_SUCCESS) {
    TRACE("%s - ckpt_decode_log_struct Fail", __FUNCTION__);
  }
  return rc;
}

static uint32_t ckpt_decode_log_cfg(lgs_cb_t *cb, void *ckpt_msg,
                                    NCS_MBCSV_CB_ARG *cbk_arg) {
  uint32_t rc = NCSCC_RC_SUCCESS;
  void *lgs_cfg = NULL;
  EDU_PROG_HANDLER edp_function = NULL;
  lgsv_ckpt_msg_v6_t *ckpt_msg_v6;
  lgsv_ckpt_msg_v5_t *ckpt_msg_v5;
  lgsv_ckpt_msg_v3_t *ckpt_msg_v3;
  lgsv_ckpt_msg_v2_t *ckpt_msg_v2;

  if (lgs_is_peer_v6()) {
    ckpt_msg_v6 = static_cast<lgsv_ckpt_msg_v6_t *>(ckpt_msg);
    lgs_cfg = &ckpt_msg_v6->ckpt_rec.lgs_cfg;
    edp_function = edp_ed_lgs_cfg_rec_v5;
  } else if (lgs_is_peer_v5()) {
    ckpt_msg_v5 = static_cast<lgsv_ckpt_msg_v5_t *>(ckpt_msg);
    lgs_cfg = &ckpt_msg_v5->ckpt_rec.lgs_cfg;
    edp_function = edp_ed_lgs_cfg_rec_v5;
  } else if (lgs_is_peer_v4()) {
    ckpt_msg_v3 = static_cast<lgsv_ckpt_msg_v3_t *>(ckpt_msg);
    lgs_cfg = &ckpt_msg_v3->ckpt_rec.lgs_cfg;
    edp_function = edp_ed_lgs_cfg_rec_v3;
  } else if (lgs_is_peer_v2()) {
    ckpt_msg_v2 = static_cast<lgsv_ckpt_msg_v2_t *>(ckpt_msg);
    lgs_cfg = &ckpt_msg_v2->ckpt_rec.lgs_cfg;
    edp_function = edp_ed_lgs_cfg_rec_v2;
  } else {
    LOG_ER("%s - log config shall not be checkpointed in checkpoint ver 1!",
           __FUNCTION__);
    osafassert(0);
  }

  rc = ckpt_decode_log_struct(cb, cbk_arg, ckpt_msg, lgs_cfg, edp_function);
  if (rc != NCSCC_RC_SUCCESS) {
    TRACE("%s - ckpt_decode_log_struct Fail", __FUNCTION__);
  }

  return rc;
}
/* END ckpt_decode_async_update helper functions */

static uint32_t ckpt_decode_async_update(lgs_cb_t *cb,
                                         NCS_MBCSV_CB_ARG *cbk_arg) {
  EDU_ERR ederror;
  lgsv_ckpt_msg_v1_t msg_v1;
  lgsv_ckpt_msg_v1_t *ckpt_msg_v1 = &msg_v1;
  lgsv_ckpt_msg_v2_t msg_v2;
  lgsv_ckpt_msg_v2_t *ckpt_msg_v2 = &msg_v2;
  lgsv_ckpt_msg_v3_t msg_v3;
  lgsv_ckpt_msg_v3_t *ckpt_msg_v3 = &msg_v3;
  lgsv_ckpt_msg_v5_t msg_v5;
  lgsv_ckpt_msg_v5_t *ckpt_msg_v5 = &msg_v5;
  lgsv_ckpt_msg_v6_t msg_v6;
  lgsv_ckpt_msg_v6_t *ckpt_msg_v6 = &msg_v6;
  void *ckpt_msg;
  lgsv_ckpt_header_t hdr, *hdr_ptr = &hdr;

  void *reg_rec;
  EDU_PROG_HANDLER edp_function_reg = NULL;
  /* Same in all versions */
  lgs_ckpt_stream_open_t *stream_open;

  TRACE_ENTER();

  /* Decode the message header */
  uint32_t rc = m_NCS_EDU_EXEC(&cb->edu_hdl, edp_ed_header_rec,
                               &cbk_arg->info.decode.i_uba, EDP_OP_TYPE_DEC,
                               &hdr_ptr, &ederror);
  if (rc != NCSCC_RC_SUCCESS) {
    m_NCS_EDU_PRINT_ERROR_STRING(ederror);
    TRACE("\tFail decode header");
  }

  TRACE_2("\tckpt_rec_type: %d ", (int)hdr_ptr->ckpt_rec_type);

  if (lgs_is_peer_v6()) {
    ckpt_msg_v6->header = hdr;
    ckpt_msg = ckpt_msg_v6;
  } else if (lgs_is_peer_v5()) {
    ckpt_msg_v5->header = hdr;
    ckpt_msg = ckpt_msg_v5;
  } else if (lgs_is_peer_v4()) {
    ckpt_msg_v3->header = hdr;
    ckpt_msg = ckpt_msg_v3;
  } else if (lgs_is_peer_v2()) {
    ckpt_msg_v2->header = hdr;
    ckpt_msg = ckpt_msg_v2;
  } else {
    ckpt_msg_v1->header = hdr;
    ckpt_msg = ckpt_msg_v1;
  }

  /* Call decode routines appropriately */
  switch (hdr_ptr->ckpt_rec_type) {
    case LGS_CKPT_CLIENT_INITIALIZE:
      TRACE_2("\tINITIALIZE REC: UPDATE");
      if (lgs_is_peer_v6()) {
        reg_rec = &ckpt_msg_v6->ckpt_rec.initialize_client;
        edp_function_reg = edp_ed_reg_rec_v6;
      } else if (lgs_is_peer_v5()) {
        reg_rec = &ckpt_msg_v5->ckpt_rec.initialize_client;
        edp_function_reg = edp_ed_reg_rec;
      } else if (lgs_is_peer_v4()) {
        reg_rec = &ckpt_msg_v3->ckpt_rec.initialize_client;
        edp_function_reg = edp_ed_reg_rec;
      } else if (lgs_is_peer_v2()) {
        reg_rec = &ckpt_msg_v2->ckpt_rec.initialize_client;
        edp_function_reg = edp_ed_reg_rec;
      } else {
        reg_rec = &ckpt_msg_v1->ckpt_rec.initialize_client;
        edp_function_reg = edp_ed_reg_rec;
      }

      rc = ckpt_decode_log_struct(cb, cbk_arg, ckpt_msg, reg_rec,
                                  edp_function_reg);
      if (rc != NCSCC_RC_SUCCESS) {
        goto done;
      }
      break;
    case LGS_CKPT_LOG_WRITE:
      TRACE_2("\tWRITE LOG: UPDATE");
      rc = ckpt_decode_log_write(cb, ckpt_msg, cbk_arg);
      if (rc != NCSCC_RC_SUCCESS) {
        goto done;
      }
      break;

    case LGS_CKPT_OPEN_STREAM: /* 4 */
      TRACE_2("\tSTREAM OPEN: UPDATE");
      if (lgs_is_peer_v6()) {
        stream_open = &ckpt_msg_v6->ckpt_rec.stream_open;
      } else if (lgs_is_peer_v5()) {
        stream_open = &ckpt_msg_v5->ckpt_rec.stream_open;
      } else if (lgs_is_peer_v4()) {
        stream_open = &ckpt_msg_v3->ckpt_rec.stream_open;
      } else if (lgs_is_peer_v2()) {
        stream_open = &ckpt_msg_v2->ckpt_rec.stream_open;
      } else {
        stream_open = &ckpt_msg_v1->ckpt_rec.stream_open;
      }

      rc = ckpt_decode_log_struct(cb, cbk_arg, ckpt_msg, stream_open,
                                  edp_ed_open_stream_rec);
      if (rc != NCSCC_RC_SUCCESS) {
        goto done;
      }
      break;

    case LGS_CKPT_CLOSE_STREAM:
      TRACE_2("\tSTREAM CLOSE: UPDATE");
      rc = ckpt_decode_log_close(cb, ckpt_msg, cbk_arg);
      if (rc != NCSCC_RC_SUCCESS) {
        goto done;
      }
      break;

    case LGS_CKPT_CLIENT_FINALIZE:
      TRACE_2("\tFINALIZE REC: UPDATE");
      rc = ckpt_decode_log_client_finalize(cb, ckpt_msg, cbk_arg);
      if (rc != NCSCC_RC_SUCCESS) {
        goto done;
      }
      break;
    case LGS_CKPT_CLIENT_DOWN:
      TRACE_2("\tAGENT DOWN REC: UPDATE");
      rc = ckpt_decode_log_client_down(cb, ckpt_msg, cbk_arg);
      if (rc != NCSCC_RC_SUCCESS) {
        goto done;
      }
      break;
    case LGS_CKPT_CFG_STREAM:
      TRACE_2("\tCFG STREAM REC: UPDATE");
      rc = ckpt_decode_log_cfg_stream(cb, ckpt_msg, cbk_arg);
      if (rc != NCSCC_RC_SUCCESS) {
        goto done;
      }
      break;
    case LGS_CKPT_LGS_CFG:
    case LGS_CKPT_LGS_CFG_V3:
    case LGS_CKPT_LGS_CFG_V5:
      TRACE_2("\tLGS CFG REC: UPDATE");
      rc = ckpt_decode_log_cfg(cb, ckpt_msg, cbk_arg);
      if (rc != NCSCC_RC_SUCCESS) {
        goto done;
      }
      break;
    default:
      rc = NCSCC_RC_FAILURE;
      TRACE("\tFAILED Unknown ckpt record type");
      goto done;
      break;
  } /*end switch */

  /* Update the Async Update Count at standby */
  cb->async_upd_cnt++;
done:
  TRACE_LEAVE2("rc = %d (1 = SUCCESS)", rc);
  return rc;
  /* if failure, should an indication be sent to active ? */
}

/****************************************************************************
 * Name          : ckpt_decode_cold_sync
 *
 * Description   : This function decodes cold-sync data.
 *
 * Arguments     : arg - Pointer to NCS_MBCSV_CB_ARG with decode info
 *                 cb - pointer to lgs cb.
 *
 * Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE
 *
 * Notes         : COLD SYNC RECORDS are expected in an order
 *                 1. REG RECORDS
 *                 2. OPEN STREAMS RECORDS
 *
 *                 For each record type,
 *                     a) decode header.
 *                     b) decode individual records for
 *                        header->num_records times,
 *****************************************************************************/
static uint32_t ckpt_decode_cold_sync(lgs_cb_t *cb, NCS_MBCSV_CB_ARG *cbk_arg) {
  uint32_t rc = NCSCC_RC_SUCCESS;
  EDU_ERR ederror;
  lgsv_ckpt_msg_v1_t msg_v1;
  lgsv_ckpt_msg_v2_t msg_v2;
  lgsv_ckpt_msg_v6_t msg_v6;
  uint32_t num_rec = 0;
  void *reg_rec = NULL;
  lgs_ckpt_stream_open_t *stream_rec = NULL;
  uint32_t num_of_async_upd;
  uint8_t *ptr;
  uint8_t data_cnt[16];

  lgsv_ckpt_header_t *header;
  void *initialize_client_rec_ptr;
  lgs_ckpt_stream_open_t *stream_open_rec_ptr;
  void *vckpt_rec;
  size_t ckpt_rec_size;
  void *vdata;
  EDU_PROG_HANDLER edp_function_reg = edp_ed_reg_rec;

  TRACE_ENTER();
  /*
     -------------------------------------------------
     | Header|RegRecords1..n|Header|streamRecords1..n|
     -------------------------------------------------
  */

  if (lgs_is_peer_v6()) {
    lgsv_ckpt_msg_v6_t *data_v6 = &msg_v6;
    header = &data_v6->header;
    initialize_client_rec_ptr = &data_v6->ckpt_rec.initialize_client;
    stream_open_rec_ptr = &data_v6->ckpt_rec.stream_open;
    vdata = data_v6;
    vckpt_rec = &data_v6->ckpt_rec;
    ckpt_rec_size = sizeof(data_v6->ckpt_rec);
    edp_function_reg = edp_ed_reg_rec_v6;
  } else if (lgs_is_peer_v2()) {
    lgsv_ckpt_msg_v2_t *data_v2 = &msg_v2;
    header = &data_v2->header;
    initialize_client_rec_ptr = &data_v2->ckpt_rec.initialize_client;
    stream_open_rec_ptr = &data_v2->ckpt_rec.stream_open;
    vdata = data_v2;
    vckpt_rec = &data_v2->ckpt_rec;
    ckpt_rec_size = sizeof(data_v2->ckpt_rec);
  } else {
    lgsv_ckpt_msg_v1_t *data_v1 = &msg_v1;
    header = &data_v1->header;
    initialize_client_rec_ptr = &data_v1->ckpt_rec.initialize_client;
    stream_open_rec_ptr = &data_v1->ckpt_rec.stream_open;
    vdata = data_v1;
    vckpt_rec = &data_v1->ckpt_rec;
    ckpt_rec_size = sizeof(data_v1->ckpt_rec);
  }

  TRACE_2("COLD SYNC DECODE START........");

  /* Decode the current message header */
  if ((rc = dec_ckpt_header(&cbk_arg->info.decode.i_uba, header)) !=
      NCSCC_RC_SUCCESS) {
    goto done;
  }
  /* Check if the first in the order of records is reg record */
  if (header->ckpt_rec_type != LGS_CKPT_CLIENT_INITIALIZE) {
    TRACE("FAILED data->header.ckpt_rec_type != LGS_CKPT_INITIALIZE_REC");
    rc = NCSCC_RC_FAILURE;
    goto done;
  }

  /* Process the reg_records */
  num_rec = header->num_ckpt_records;
  TRACE("regid: num_rec = %u", num_rec);
  while (num_rec) {
    reg_rec = initialize_client_rec_ptr;
    rc = m_NCS_EDU_EXEC(&cb->edu_hdl, edp_function_reg,
                        &cbk_arg->info.decode.i_uba, EDP_OP_TYPE_DEC, &reg_rec,
                        &ederror);

    if (rc != NCSCC_RC_SUCCESS) {
      TRACE("FAILED: COLD SYNC DECODE REG REC");
      m_NCS_EDU_PRINT_ERROR_STRING(ederror);
      goto done;
    }
    /* Update our database */
    rc = process_ckpt_data(cb, vdata);
    if (rc != NCSCC_RC_SUCCESS) {
      goto done;
    }
    memset(vckpt_rec, 0, ckpt_rec_size);
    --num_rec;
  } /*End while, reg records */

  if ((rc = dec_ckpt_header(&cbk_arg->info.decode.i_uba, header)) !=
      NCSCC_RC_SUCCESS) {
    rc = NCSCC_RC_FAILURE;
    TRACE("lgs_dec_ckpt_header FAILED");
    goto done;
  }

  /* Check if record type is open_stream */
  if (header->ckpt_rec_type != LGS_CKPT_OPEN_STREAM) {
    rc = NCSCC_RC_FAILURE;
    TRACE("FAILED: LGS_CKPT_OPEN_STREAM type is expected, got %u",
          header->ckpt_rec_type);
    goto done;
  }

  /* Process the stream records */
  num_rec = header->num_ckpt_records;
  TRACE("opens_streams: num_rec = %u", num_rec);
  while (num_rec) {
    stream_rec = stream_open_rec_ptr;
    rc = m_NCS_EDU_EXEC(&cb->edu_hdl, edp_ed_open_stream_rec,
                        &cbk_arg->info.decode.i_uba, EDP_OP_TYPE_DEC,
                        &stream_rec, &ederror);

    if (rc != NCSCC_RC_SUCCESS) {
      TRACE("FAILED: COLD SYNC DECODE STREAM REC");
      m_NCS_EDU_PRINT_ERROR_STRING(ederror);
      goto done;
    }
    /* Update our database */
    rc = process_ckpt_data(cb, vdata);

    if (rc != NCSCC_RC_SUCCESS) {
      goto done;
    }
    memset(vckpt_rec, 0, ckpt_rec_size);
    --num_rec;
  } /*End while, stream records */

  /* Get the async update count */
  ptr = ncs_dec_flatten_space(&cbk_arg->info.decode.i_uba, data_cnt,
                              sizeof(uint32_t));
  num_of_async_upd = ncs_decode_32bit(&ptr);
  cb->async_upd_cnt = num_of_async_upd;
  ncs_dec_skip_space(&cbk_arg->info.decode.i_uba, 4);

  /* If we reached here, we are through. Good enough for coldsync with ACTIVE */
  TRACE_2("COLD SYNC DECODE END........");

done:
  if (rc != NCSCC_RC_SUCCESS) {
    /* Do not allow standby to get out of sync */
    lgs_exit("Cold sync failed", SA_AMF_COMPONENT_RESTART);
  }
  TRACE_LEAVE();
  return rc;
}

/****************************************************************************
 * Name          : process_ckpt_data
 *
 * Description   : Call the a data handling process that corresponds to the
 *                 check-point record type
 *
 * Arguments     : cb - pointer to LGS ControlBlock.
 *                 data - pointer to  LGS_CHECKPOINT_DATA.
 *
 *
 * Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE
 *
 * Notes         : None.
 *****************************************************************************/

static uint32_t process_ckpt_data(lgs_cb_t *cb, void *data) {
  uint32_t rc = NCSCC_RC_SUCCESS;
  lgsv_ckpt_msg_type_t lgsv_ckpt_msg_type;
  lgsv_ckpt_msg_v1_t *data_v1;
  lgsv_ckpt_msg_v2_t *data_v2;
  lgsv_ckpt_msg_v3_t *data_v3;
  lgsv_ckpt_msg_v5_t *data_v5;
  lgsv_ckpt_msg_v6_t *data_v6;

  if ((!cb) || (data == NULL)) {
    TRACE("%s - FAILED: (!cb) || (data == NULL)", __FUNCTION__);
    return (rc = NCSCC_RC_FAILURE);
  }

  if (lgs_is_peer_v6()) {
    data_v6 = static_cast<lgsv_ckpt_msg_v6_t *>(data);
    lgsv_ckpt_msg_type = data_v6->header.ckpt_rec_type;
  } else if (lgs_is_peer_v5()) {
    data_v5 = static_cast<lgsv_ckpt_msg_v5_t *>(data);
    lgsv_ckpt_msg_type = data_v5->header.ckpt_rec_type;
  } else if (lgs_is_peer_v4()) {
    data_v3 = static_cast<lgsv_ckpt_msg_v3_t *>(data);
    lgsv_ckpt_msg_type = data_v3->header.ckpt_rec_type;
  } else if (lgs_is_peer_v2()) {
    data_v2 = static_cast<lgsv_ckpt_msg_v2_t *>(data);
    lgsv_ckpt_msg_type = data_v2->header.ckpt_rec_type;
  } else {
    data_v1 = static_cast<lgsv_ckpt_msg_v1_t *>(data);
    lgsv_ckpt_msg_type = data_v1->header.ckpt_rec_type;
  }

  if ((cb->ha_state == SA_AMF_HA_STANDBY) ||
      (cb->ha_state == SA_AMF_HA_QUIESCED)) {
    if (lgsv_ckpt_msg_type >= LGS_CKPT_MSG_MAX) {
      TRACE("%s - FAILED: data->header.ckpt_rec_type >= LGS_CKPT_MSG_MAX",
            __FUNCTION__);
      return NCSCC_RC_FAILURE;
    }
    /* Use function corresponding to check-pointed data type to
     * process received check-point data */
    rc = ckpt_data_handler[lgsv_ckpt_msg_type](cb, data);
  } else {
    TRACE("%s - ERROR Called in ACTIVE state", __FUNCTION__);
    rc = NCSCC_RC_FAILURE;
  }

  return rc;
}

/****************************************************************************
 * Name          : ckpt_proc_initialize_client
 *
 * Description   : This function updates the lgs reglist based on the
 *                 info received from the ACTIVE lgs peer.
 *
 * Arguments     : cb - pointer to LGS  ControlBlock.
 *                 data - pointer to  LGS_CHECKPOINT_DATA.
 *
 *
 * Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE
 *
 * Notes         : None.
 ****************************************************************************/

static uint32_t ckpt_proc_initialize_client(lgs_cb_t *cb, void *data) {
  log_client_t *client;

  TRACE_ENTER();
  if (lgs_is_peer_v6()) {
    lgs_ckpt_initialize_msg_v6_t *param;
    lgsv_ckpt_msg_v6_t *data_v6 = static_cast<lgsv_ckpt_msg_v6_t *>(data);
    param = &data_v6->ckpt_rec.initialize_client;

    TRACE("client ID: %d", param->client_id);
    client = lgs_client_get_by_id(param->client_id);
    if (client == NULL) {
      /* Client does not exist, create new one */
      if ((client = lgs_client_new(param->mds_dest, param->client_id,
                                   param->stream_list)) == NULL) {
        /* Do not allow standby to get out of sync */
        lgs_exit("Could not create new client", SA_AMF_COMPONENT_RESTART);
      } else {
        client->client_ver = param->client_ver;
      }
    } else {
      /* Client with ID already exist, check other attributes */
      if (client->mds_dest != param->mds_dest) {
        /* Do not allow standby to get out of sync */
        lgs_exit("Client attributes differ", SA_AMF_COMPONENT_RESTART);
      }
    }
  } else {
    lgs_ckpt_initialize_msg_t *param;
    lgsv_ckpt_msg_v1_t *data_v1 = static_cast<lgsv_ckpt_msg_v1_t *>(data);
    param = &data_v1->ckpt_rec.initialize_client;

    TRACE("client ID: %d", param->client_id);
    client = lgs_client_get_by_id(param->client_id);
    if (client == NULL) {
      /* Client does not exist, create new one */
      if ((client = lgs_client_new(param->mds_dest, param->client_id,
                                   param->stream_list)) == NULL) {
        /* Do not allow standby to get out of sync */
        lgs_exit("Could not create new client", SA_AMF_COMPONENT_RESTART);
      }
    } else {
      /* Client with ID already exist, check other attributes */
      if (client->mds_dest != param->mds_dest) {
        /* Do not allow standby to get out of sync */
        lgs_exit("Client attributes differ", SA_AMF_COMPONENT_RESTART);
      }
    }
  }

  TRACE_LEAVE();
  return NCSCC_RC_SUCCESS;
}

/**
 * Helper functions for ckpt_proc_log_write()
 *
 */
static SaTimeT setLogTime() {
  struct timeval currentTime;
  SaTimeT logTime;

  /* Fetch current system time for time stamp value */
  (void)gettimeofday(&currentTime, 0);

  logTime = ((unsigned)currentTime.tv_sec * 1000000000ULL) +
            ((unsigned)currentTime.tv_usec * 1000ULL);

  return logTime;
}

/**
 * Helper function for creating local info message in log stream
 * @param stream
 * @param message
 */
static void insert_localmsg_in_stream(log_stream_t *stream, char *message) {
  int n = 0;
  int rc = 0;
  SaLogRecordT log_record;
  SaLogBufferT logBuffer;
  SaStringT logOutputString = NULL;
  SaUint32T buf_size;
  const int LOG_REC_ID = 0;
  const int sleep_delay_ms = 10;
  const int max_waiting_time_ms = 100;
  int msecs_waited = 0;

  /* Log record common */
  // log_record.logTimeStamp = setLogTime();
  log_record.logTimeStamp = setLogTime();
  logBuffer.logBuf = (SaUint8T *)message;
  logBuffer.logBufSize = strlen(message) + 1;
  log_record.logBuffer = &logBuffer;

  SaNtfClassIdT SaNtfClassId;
  SaNtfClassId.majorId = SA_SCV_LOG;
  SaNtfClassId.minorId = 0;
  SaNtfClassId.vendorId = SA_NTF_VENDOR_ID_SAF;

  /* Construct logSvcUsrName for log service */
  SaNameT logSvcUsrName;
  SaConstStringT tmpSvcName = "safApp=safLogService";
  osaf_extended_name_lend(tmpSvcName, &logSvcUsrName);

  /* Create a log header corresponding to type of stream */
  if ((stream->streamType == STREAM_TYPE_ALARM) ||
      (stream->streamType == STREAM_TYPE_NOTIFICATION)) {
    log_record.logHdrType = SA_LOG_NTF_HEADER;
    log_record.logHeader.ntfHdr.notificationClassId = &SaNtfClassId;
    log_record.logHeader.ntfHdr.eventTime = setLogTime();
    log_record.logHeader.ntfHdr.eventType = SA_NTF_APPLICATION_EVENT;
    log_record.logHeader.ntfHdr.notificationId = SA_NTF_IDENTIFIER_UNUSED;
    log_record.logHeader.ntfHdr.notificationObject = &logSvcUsrName;
    log_record.logHeader.ntfHdr.notifyingObject = &logSvcUsrName;
  } else {
    log_record.logHdrType = SA_LOG_GENERIC_HEADER;
    log_record.logHeader.genericHdr.notificationClassId = &SaNtfClassId;
    log_record.logHeader.genericHdr.logSeverity = SA_LOG_SEV_NOTICE;
    log_record.logHeader.genericHdr.logSvcUsrName = &logSvcUsrName;
  }

  /* Allocate memory for the resulting log record */
  uint32_t max_logrecsize =
      *static_cast<const uint32_t *>(lgs_cfg_get(LGS_IMM_LOG_MAX_LOGRECSIZE));
  buf_size = stream->fixedLogRecordSize == 0 ? max_logrecsize
                                             : stream->fixedLogRecordSize;
  logOutputString = static_cast<char *>(
      calloc(1, buf_size + 1)); /* Make room for a '\0' termination */
  if (logOutputString == NULL) {
    LOG_ER("%s - Could not allocate %d bytes", __FUNCTION__,
           stream->fixedLogRecordSize + 1);
    goto done;
  }

  /* Use local host name as node name for missing record */
  char host_name[_POSIX_HOST_NAME_MAX];
  memset(host_name, 0, _POSIX_HOST_NAME_MAX);
  if (gethostname(host_name, _POSIX_HOST_NAME_MAX) == -1) {
    LOG_WA("gethostname failed (%s). Use default SC-2", strerror(errno));
    strcpy(host_name, "SC-2");
  }

  /* Format the log record */
  if ((n = lgs_format_log_record(
           &log_record, stream->logFileFormat, stream->maxLogFileSize,
           stream->fixedLogRecordSize, buf_size, logOutputString, LOG_REC_ID,
           host_name)) == 0) {
    LOG_ER("%s - Could not format internal log record", __FUNCTION__);
    goto done;
  }

  /* Write the log record to file */
  rc = log_stream_write_h(stream, logOutputString, n);
  while ((rc == -2) && (msecs_waited < max_waiting_time_ms)) {
    usleep(sleep_delay_ms * 1000);
    msecs_waited += sleep_delay_ms;

    /*
      In time-out case, file handler thread frees the log record memory.
      Therefore, need to re-allocate memory & retry writing log.
    */
    if (logOutputString == NULL) {
      logOutputString = static_cast<char *>(
          calloc(1, buf_size + 1)); /* Make room for a '\0' termination */
      if (logOutputString == NULL) {
        LOG_ER("%s - Could not allocate %d bytes", __FUNCTION__,
               stream->fixedLogRecordSize + 1);
      }

      /* Format the log record */
      if ((n = lgs_format_log_record(
               &log_record, stream->logFileFormat, stream->maxLogFileSize,
               stream->fixedLogRecordSize, buf_size, logOutputString,
               LOG_REC_ID, host_name)) == 0) {
        LOG_ER("%s - Could not format internal log record", __FUNCTION__);
      }
    }

    rc = log_stream_write_h(stream, logOutputString, n);
  }
  if (rc != 0) {
    TRACE("Error %d when writing log record", rc);
  }
/*
  Since the logOutputString is referred by the log handler thread, in timeout
  case, the log API thread might be still using the log record memory.

  To make sure there is no corruption of memory usage in case of time-out (rc =
  -2), We leave the log record memory freed to the log handler thread..

  It is never a good idea to allocate and free memory in different places.
  But consider it as a trade-off to have a better performance of LOGsv
  as time-out occurs very rarely.

  Other cases, the allocator frees it.
*/
done:
  if ((rc != -2) && (logOutputString != NULL)) {
    free(logOutputString);
    logOutputString = NULL;
  }
}

/****************************************************************************
 * Name          : ckpt_proc_log_write
 *
 * Description   : This function updates the lgs alarm logRecordId
 *                 received from the ACTIVE lgs peer.
 *
 *
 * Arguments     : cb - pointer to LGS  ControlBlock.
 *                 data - pointer to  LGS_CHECKPOINT_DATA.
 *
 *
 * Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE
 *
 * Notes         : None.
 ****************************************************************************/

static uint32_t ckpt_proc_log_write(lgs_cb_t *cb, void *data) {
  log_stream_t *stream;
  uint32_t streamId;
  uint32_t recordId;
  uint32_t curFileSize;
  char *logFileCurrent;
  char *logRecord = NULL;
  uint64_t c_file_close_time_stamp = 0;
  int rc = 0;

  TRACE_ENTER();

  if (lgs_is_peer_v2()) {
    lgsv_ckpt_msg_v2_t *data_v2 = static_cast<lgsv_ckpt_msg_v2_t *>(data);
    streamId = data_v2->ckpt_rec.write_log.streamId;
    recordId = data_v2->ckpt_rec.write_log.recordId;
    curFileSize = data_v2->ckpt_rec.write_log.curFileSize;
    logFileCurrent = data_v2->ckpt_rec.write_log.logFileCurrent;
    logRecord = data_v2->ckpt_rec.write_log.logRecord;
    c_file_close_time_stamp =
        data_v2->ckpt_rec.write_log.c_file_close_time_stamp;
  } else {
    lgsv_ckpt_msg_v1_t *data_v1 = static_cast<lgsv_ckpt_msg_v1_t *>(data);
    streamId = data_v1->ckpt_rec.write_log.streamId;
    recordId = data_v1->ckpt_rec.write_log.recordId;
    curFileSize = data_v1->ckpt_rec.write_log.curFileSize;
    logFileCurrent = data_v1->ckpt_rec.write_log.logFileCurrent;
  }

  stream = log_stream_get_by_id(streamId);
  if (stream == NULL) {
    TRACE("Could not lookup stream: %u", streamId);
    goto done;
  }

  stream->logRecordId = recordId;
  stream->curFileSize = curFileSize;
  stream->logFileCurrent = logFileCurrent;

  /* If configured for split file system log records shall be written also if
   * we are standby.
   */
  if (lgs_is_split_file_system()) {
    size_t rec_len = strlen(logRecord);
    stream->act_last_close_timestamp = c_file_close_time_stamp;

    /* Check if record id numbering is inconsistent. If so there are
     * possible missed log records and a notification shall be inserted
     * in log file.
     */
    if ((stream->stb_logRecordId + 1) != recordId) {
      insert_localmsg_in_stream(
          stream, const_cast<char *>("Possible loss of log record"));
    }

    /* Make a limited number of attempts to write if file IO timed out when
     * trying to write the log record.
     */
    rc = log_stream_write_h(stream, logRecord, rec_len);
    if (rc != 0) {
      TRACE("\tError %d when writing log record", rc);
    }

    stream->stb_logRecordId = recordId;
  } /* END lgs_is_split_file_system */

done:
  /*
    Since the logRecord is referred by the log handler thread, in time-out case,
    the log API thread might be still using the log record memory.

    To make sure there is no corruption of memory usage in case of time-out (rc
    = -2), We leave the log record memory freed to the log handler thread..

    It is never a good idea to allocate and free memory in different places.
    But consider it as a trade-off to have a better performance of LOGsv
    as time-out occurs very rarely.

    Other cases, the allocator frees it.
  */
  if ((rc != -2) && (logRecord != NULL)) {
    lgs_free_edu_mem(logRecord);
    logRecord = NULL;
  }

  lgs_free_edu_mem(logFileCurrent);

  TRACE_LEAVE();
  /*
    If rc == -2, means something happens in log handler thread
    return TIMEOUT error, so that the caller will try again.
  */
  return (rc == -2 ? NCSCC_RC_REQ_TIMOUT : NCSCC_RC_SUCCESS);
}

/****************************************************************************
 * Name          : ckpt_proc_close_stream
 *
 * Description   : This function close a stream.
 *
 * Arguments     : cb - pointer to LGS  ControlBlock.
 *                 data - pointer to  LGS_CHECKPOINT_DATA.
 *
 *
 * Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE
 *
 * Notes         : None.
 ****************************************************************************/

static uint32_t ckpt_proc_close_stream(lgs_cb_t *cb, void *data) {
  log_stream_t *stream;
  time_t *closetime_ptr;

  uint32_t streamId;
  uint32_t clientId;

  TRACE_ENTER();

  if (lgs_is_peer_v2()) {
    lgsv_ckpt_msg_v2_t *data_v2 = static_cast<lgsv_ckpt_msg_v2_t *>(data);
    streamId = data_v2->ckpt_rec.stream_close.streamId;
    clientId = data_v2->ckpt_rec.stream_close.clientId;
    /* Set time for closing. Used for renaming */
    closetime_ptr = reinterpret_cast<time_t *>(
        &data_v2->ckpt_rec.stream_close.c_file_close_time_stamp);
  } else {
    lgsv_ckpt_msg_v1_t *data_v1 = static_cast<lgsv_ckpt_msg_v1_t *>(data);
    streamId = data_v1->ckpt_rec.stream_close.streamId;
    clientId = data_v1->ckpt_rec.stream_close.clientId;
    closetime_ptr = NULL;
  }

  if ((stream = log_stream_get_by_id(streamId)) == NULL) {
    TRACE("Could not lookup stream");
    goto done;
  }

  TRACE("close stream %s, id: %u", stream->name.c_str(), stream->streamId);

  if ((stream->numOpeners > 0) || (clientId == 0)) {
    /* No clients to remove if no openers or if closing a stream opened
     * by log service itself
     */
    (void)lgs_client_stream_rmv(clientId, streamId);
  }

  log_stream_close(&stream, closetime_ptr);

done:
  TRACE_LEAVE();
  return NCSCC_RC_SUCCESS;
}

/****************************************************************************
 * Name          : ckpt_proc_open_stream
 *
 * Description   : This function updates an existing stream or creates a new.
 *
 * Arguments     : cb - pointer to LGS  ControlBlock.
 *                data - pointer to  LGS_CHECKPOINT_DATA.
 *
 *
 * Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE
 *
 * Notes         : None.
 ****************************************************************************/

uint32_t ckpt_proc_open_stream(lgs_cb_t *cb, void *data) {
  lgs_ckpt_stream_open_t *param;
  log_stream_t *stream;
  int pos = 0, err = 0;
  SaNameT objectName;

  TRACE_ENTER();

  if (lgs_is_peer_v6()) {
    lgsv_ckpt_msg_v6_t *data_v6 = static_cast<lgsv_ckpt_msg_v6_t *>(data);
    param = &data_v6->ckpt_rec.stream_open;
  } else if (lgs_is_peer_v2()) {
    lgsv_ckpt_msg_v2_t *data_v2 = static_cast<lgsv_ckpt_msg_v2_t *>(data);
    param = &data_v2->ckpt_rec.stream_open;
  } else {
    lgsv_ckpt_msg_v1_t *data_v1 = static_cast<lgsv_ckpt_msg_v1_t *>(data);
    param = &data_v1->ckpt_rec.stream_open;
  }

  uint32_t invalidClient = static_cast<uint32_t>(-1);
  /* Check that client still exist */
  if ((param->clientId != invalidClient) &&
      (lgs_client_get_by_id(param->clientId) == NULL)) {
    LOG_WA("\tClient %u does not exist, failed to create stream '%s'",
           param->clientId, param->logStreamName);
    goto done;
  }

  stream = log_stream_get_by_name(param->logStreamName);
  if (stream != NULL) {
    TRACE("\tExisting stream - id %u", stream->streamId);
    /*
    ** Update stream attributes that might change when a stream is
    ** opened a second time.
    */
    stream->numOpeners = param->numOpeners;
  } else {
    TRACE("\tNew stream %s, id %u", param->logStreamName, param->streamId);

    SaAisErrorT rc = SA_AIS_OK;
    stream = log_stream_new(param->logStreamName, param->streamId);
    if (stream == NULL) {
      LOG_ER("Failed to create log stream %s", param->logStreamName);
      goto done;
    }
    err = lgs_populate_log_stream(
        param->logFile, param->logPath, param->maxFileSize,
        param->maxLogRecordSize, param->logFileFullAction,
        param->maxFilesRotated, param->fileFmt, param->streamType,
        SA_FALSE,  // FIX sync or calculate?
        param->logRecordId,
        stream  // output
        );

    if (err == -1) {
      log_stream_delete(&stream);
      goto done;
    }

    rc = lgs_create_rt_appstream(stream);
    if (rc != SA_AIS_OK) {
      log_stream_delete(&stream);
      goto done;
    }

    stream->numOpeners = param->numOpeners;
    stream->creationTimeStamp = param->creationTimeStamp;
    stream->stb_curFileSize = 0;
    stream->logFileCurrent = param->logFileCurrent;
    stream->stb_prev_actlogFileCurrent = param->logFileCurrent;
    stream->stb_logFileCurrent = param->logFileCurrent;

    osaf_extended_name_lend(param->logStreamName, &objectName);
    SaImmClassNameT className = immutil_get_className(&objectName);
    if (className != nullptr && strcmp(className, "SaLogStreamConfig") == 0) {
      stream->isRtStream = SA_FALSE;
    } else {
      stream->isRtStream = SA_TRUE;
    }
    if (className != nullptr) free(className);

    // Only update destination names if peer is v6 or upper.
    if (lgs_is_peer_v6()) {
      if (param->dest_names != nullptr && strlen(param->dest_names) != 0) {
        TRACE("Dest_name %s", param->dest_names);
        stream->stb_dest_names = std::string{param->dest_names};
        stream->dest_names = logutil::Parser(stream->stb_dest_names, ";");
      } else {
        stream->stb_dest_names = "";
        stream->dest_names.clear();
      }

      // Generate & cache `MSGID` to `rfc5424MsgId` which later
      // used in RFC5424 protocol
      stream->rfc5424MsgId = DestinationHandler::Instance().GenerateMsgId(
          stream->name, stream->isRtStream);
    }
  }

  /* If configured for split file system files shall be opened on stand by
   * also.
   */
  if (lgs_is_split_file_system()) {
    if (stream->numOpeners <= 1) {
      TRACE("%s: log_initiate_stream_files(%s)", __FUNCTION__,
            stream->fileName.c_str());

      log_initiate_stream_files(stream);
    }
  }
  log_stream_print(stream);

  /*
  ** Create an association between this client_id and the stream
  ** A client ID of -1 indicates that no client exist, skip this step.
  */
  if ((param->clientId != invalidClient) &&
      lgs_client_stream_add(param->clientId, stream->streamId) != 0) {
    /* Do not allow standby to get out of sync */
    LOG_ER("%s - Failed to add stream '%s' to client %u", __FUNCTION__,
           param->logStreamName, param->clientId);
    lgs_exit("Could not add stream to client", SA_AMF_COMPONENT_RESTART);
  }

  /* Stream is opened  on standby. Remove from rtobj list if exist */
  pos = log_rtobj_list_find(param->logStreamName);
  if (pos != -1) log_rtobj_list_erase_one_pos(pos);

done:
  /* Free strings allocated by the EDU encoder */
  lgs_free_edu_mem(param->logFile);
  lgs_free_edu_mem(param->logPath);
  lgs_free_edu_mem(param->fileFmt);
  lgs_free_edu_mem(param->logFileCurrent);
  lgs_free_edu_mem(param->logStreamName);

  TRACE_LEAVE();
  return NCSCC_RC_SUCCESS;
}

/****************************************************************************
 * Name          : ckpt_proc_finalize_client
 *
 * Description   : This function clears the lgs reglist and assosicated DB
 *                 based on the info received from the ACTIVE lgs peer.
 *
 * Arguments     : cb - pointer to LGS ControlBlock.
 *                 data - pointer to  LGS_CHECKPOINT_DATA.
 *
 *
 * Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE
 *
 * Notes         : None.
 ****************************************************************************/

static uint32_t ckpt_proc_finalize_client(lgs_cb_t *cb, void *data) {
  uint32_t client_id;
  time_t *closetime_ptr;

  if (lgs_is_peer_v2()) {
    lgsv_ckpt_msg_v2_t *data_v2 = static_cast<lgsv_ckpt_msg_v2_t *>(data);
    lgs_ckpt_finalize_msg_v2_t *param = &data_v2->ckpt_rec.finalize_client;
    closetime_ptr = reinterpret_cast<time_t *>(&param->c_file_close_time_stamp);
    client_id = param->client_id;
  } else {
    lgsv_ckpt_msg_v1_t *data_v1 = static_cast<lgsv_ckpt_msg_v1_t *>(data);
    lgs_ckpt_finalize_msg_v1_t *param = &data_v1->ckpt_rec.finalize_client;
    closetime_ptr = NULL;
    client_id = param->client_id;
  }

  TRACE_ENTER();

  /* Ensure all resources allocated by this registration are freed. */
  (void)lgs_client_delete(client_id, closetime_ptr);

  TRACE_LEAVE();
  return NCSCC_RC_SUCCESS;
}

/****************************************************************************
 * Name          : ckpt_proc_agent_down
 *
 * Description   : This function processes a agent down message
 *                 received from the ACTIVE LGS peer.
 *
 * Arguments     : cb - pointer to LGS ControlBlock.
 *                 data - pointer to  LGS_CHECKPOINT_DATA.
 *
 *
 * Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE
 *
 * Notes         : None
 ****************************************************************************/

uint32_t ckpt_proc_agent_down(lgs_cb_t *cb, void *data) {
  MDS_DEST agent_dest;
  time_t *closetime_ptr;

  TRACE_ENTER();

  if (lgs_is_peer_v2()) {
    lgsv_ckpt_msg_v2_t *data_v2 = static_cast<lgsv_ckpt_msg_v2_t *>(data);
    closetime_ptr = reinterpret_cast<time_t *>(
        &data_v2->ckpt_rec.agent_down.c_file_close_time_stamp);
    agent_dest = data_v2->ckpt_rec.agent_down.agent_dest;
  } else {
    lgsv_ckpt_msg_v1_t *data_v1 = static_cast<lgsv_ckpt_msg_v1_t *>(data);
    closetime_ptr = NULL;
    agent_dest = data_v1->ckpt_rec.agent_dest;
  }

  /*Remove the  LGA DOWN REC from the LGA_DOWN_LIST */
  /* Search for matching LGA in the LGA_DOWN_LIST  */
  lgs_remove_lga_down_rec(cb, agent_dest);

  /* Ensure all resources allocated by this registration are freed. */
  /* Files are closed if there is a fd in stream that's not -1
   * but time must be checkpointed and checkpointed time used here
   */
  (void)lgs_client_delete_by_mds_dest(agent_dest, closetime_ptr);

  TRACE_LEAVE();
  return NCSCC_RC_SUCCESS;
}

/****************************************************************************
 * Name          : ckpt_proc_cfg_streamc
 *
 * Description   : This function configures a stream.
 *
 * Arguments     : cb - pointer to LGS  ControlBlock.
 *                 data - pointer to  LGS_CHECKPOINT_DATA.
 *
 *
 * Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE
 *
 * Notes         : None.
 ****************************************************************************/

static uint32_t ckpt_proc_cfg_stream(lgs_cb_t *cb, void *data) {
  log_stream_t *stream;
  time_t closetime;
  struct timespec closetime_tspec;

  char *name;
  char *fileName;
  char *pathName;
  SaUint64T maxLogFileSize;
  SaUint32T fixedLogRecordSize;
  SaLogFileFullActionT logFullAction;
  SaUint32T logFullHaltThreshold; /* !app log stream */
  SaUint32T maxFilesRotated;
  char *logFileFormat;
  char *dest_names = nullptr;
  SaUint32T severityFilter;
  char *logFileCurrent;

  TRACE_ENTER();

  if (lgs_is_peer_v6()) {
    lgsv_ckpt_msg_v6_t *data_v6 = static_cast<lgsv_ckpt_msg_v6_t *>(data);
    name = data_v6->ckpt_rec.stream_cfg.name;
    fileName = data_v6->ckpt_rec.stream_cfg.fileName;
    pathName = data_v6->ckpt_rec.stream_cfg.pathName;
    maxLogFileSize = data_v6->ckpt_rec.stream_cfg.maxLogFileSize;
    fixedLogRecordSize = data_v6->ckpt_rec.stream_cfg.fixedLogRecordSize;
    logFullAction = data_v6->ckpt_rec.stream_cfg.logFullAction;
    logFullHaltThreshold = data_v6->ckpt_rec.stream_cfg.logFullHaltThreshold;
    maxFilesRotated = data_v6->ckpt_rec.stream_cfg.maxFilesRotated;
    logFileFormat = data_v6->ckpt_rec.stream_cfg.logFileFormat;
    severityFilter = data_v6->ckpt_rec.stream_cfg.severityFilter;
    logFileCurrent = data_v6->ckpt_rec.stream_cfg.logFileCurrent;
    dest_names = data_v6->ckpt_rec.stream_cfg.dest_names;
    closetime = data_v6->ckpt_rec.stream_cfg.c_file_close_time_stamp;
  } else if (lgs_is_peer_v2()) {
    lgsv_ckpt_msg_v2_t *data_v2 = static_cast<lgsv_ckpt_msg_v2_t *>(data);
    name = data_v2->ckpt_rec.stream_cfg.name;
    fileName = data_v2->ckpt_rec.stream_cfg.fileName;
    pathName = data_v2->ckpt_rec.stream_cfg.pathName;
    maxLogFileSize = data_v2->ckpt_rec.stream_cfg.maxLogFileSize;
    fixedLogRecordSize = data_v2->ckpt_rec.stream_cfg.fixedLogRecordSize;
    logFullAction = data_v2->ckpt_rec.stream_cfg.logFullAction;
    logFullHaltThreshold = data_v2->ckpt_rec.stream_cfg.logFullHaltThreshold;
    maxFilesRotated = data_v2->ckpt_rec.stream_cfg.maxFilesRotated;
    logFileFormat = data_v2->ckpt_rec.stream_cfg.logFileFormat;
    severityFilter = data_v2->ckpt_rec.stream_cfg.severityFilter;
    logFileCurrent = data_v2->ckpt_rec.stream_cfg.logFileCurrent;
    closetime = data_v2->ckpt_rec.stream_cfg.c_file_close_time_stamp;
  } else {
    lgsv_ckpt_msg_v1_t *data_v1 = static_cast<lgsv_ckpt_msg_v1_t *>(data);
    name = data_v1->ckpt_rec.stream_cfg.name;
    fileName = data_v1->ckpt_rec.stream_cfg.fileName;
    pathName = data_v1->ckpt_rec.stream_cfg.pathName;
    maxLogFileSize = data_v1->ckpt_rec.stream_cfg.maxLogFileSize;
    fixedLogRecordSize = data_v1->ckpt_rec.stream_cfg.fixedLogRecordSize;
    logFullAction = data_v1->ckpt_rec.stream_cfg.logFullAction;
    logFullHaltThreshold = data_v1->ckpt_rec.stream_cfg.logFullHaltThreshold;
    maxFilesRotated = data_v1->ckpt_rec.stream_cfg.maxFilesRotated;
    logFileFormat = data_v1->ckpt_rec.stream_cfg.logFileFormat;
    severityFilter = data_v1->ckpt_rec.stream_cfg.severityFilter;
    logFileCurrent = data_v1->ckpt_rec.stream_cfg.logFileCurrent;
    osaf_clock_gettime(CLOCK_REALTIME, &closetime_tspec);
    closetime = closetime_tspec.tv_sec;
  }

  if ((stream = log_stream_get_by_name(name)) == NULL) {
    LOG_ER("Could not lookup stream");
    goto done;
  }

  TRACE("config stream %s, id: %u", stream->name.c_str(), stream->streamId);
  stream->act_last_close_timestamp = closetime; /* Not used if ver 1 */
  stream->fileName = fileName;
  stream->maxLogFileSize = maxLogFileSize;
  stream->fixedLogRecordSize = fixedLogRecordSize;
  stream->logFullAction = logFullAction;
  stream->logFullHaltThreshold = logFullHaltThreshold;
  stream->maxFilesRotated = maxFilesRotated;

  if (stream->logFileFormat != NULL) {
    free(stream->logFileFormat);
    stream->logFileFormat = strdup(logFileFormat);
  }

  strcpy(stream->logFileFormat, logFileFormat);
  stream->severityFilter = severityFilter;
  stream->logFileCurrent = logFileCurrent;
  // Only update destination name if peer is v6 or upper.
  if (lgs_is_peer_v6()) {
    if (dest_names != nullptr && strlen(dest_names) != 0) {
      TRACE("dest_names: %s", dest_names);
      stream->stb_dest_names = std::string{dest_names};
      stream->dest_names = logutil::Parser(stream->stb_dest_names, ";");
    } else {
      stream->stb_dest_names = "";
      stream->dest_names.clear();
    }
  }

  /* If split file mode, update standby files */
  if (lgs_is_split_file_system()) {
    int rc = 0;
    std::string root_path =
        static_cast<const char *>(lgs_cfg_get(LGS_IMM_LOG_ROOT_DIRECTORY));
    if ((rc = log_stream_config_change(LGS_STREAM_CREATE_FILES, root_path,
                                       stream, stream->stb_logFileCurrent,
                                       &closetime)) != 0) {
      LOG_WA("log_stream_config_change failed: %d", rc);
    }

    /* When modifying old files are closed and new are opened meaning that
     * we have a new  standby current log-file
     */
    stream->stb_prev_actlogFileCurrent = stream->logFileCurrent;
    stream->stb_logFileCurrent = stream->logFileCurrent;
  }

done:
  /* Free strings allocated by the EDU encoder */
  lgs_free_edu_mem(fileName);
  lgs_free_edu_mem(pathName);
  lgs_free_edu_mem(logFileFormat);
  lgs_free_edu_mem(logFileCurrent);
  lgs_free_edu_mem(name);

  TRACE_LEAVE();
  return NCSCC_RC_SUCCESS;
}

/****************************************************************************
 * Name          : lgs_ckpt_send_async
 *
 * Description   : This function makes a request to MBCSV to send an async
 *                 update to the STANDBY LGS for the record held at
 *                 the address i_reo_hdl.
 *
 * Arguments     : cb - A pointer to the lgs control block.
 *                 ckpt_rec - pointer to the checkpoint record to be
 *                 sent as an async update.
 *                 action - type of async update to indiciate whether
 *                 this update is for addition, deletion or modification of
 *                 the record being sent.
 *
 * Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE
 *
 * Notes         : MBCSV, inturn calls our encode callback for this async
 *                 update. We use the reo_hdl in the encode callback to
 *                 retrieve the record for encoding the same.
 *****************************************************************************/

uint32_t lgs_ckpt_send_async(lgs_cb_t *cb, void *ckpt_rec, uint32_t action) {
  uint32_t rc = NCSCC_RC_SUCCESS;
  NCS_MBCSV_ARG mbcsv_arg;
  lgsv_ckpt_msg_type_t ckpt_rec_type;

  TRACE_ENTER();

  if (lgs_is_peer_v6()) {
    lgsv_ckpt_msg_v6_t *ckpt_rec_v6 =
        static_cast<lgsv_ckpt_msg_v6_t *>(ckpt_rec);
    ckpt_rec_type = ckpt_rec_v6->header.ckpt_rec_type;
  } else if (lgs_is_peer_v5()) {
    lgsv_ckpt_msg_v5_t *ckpt_rec_v5 =
        static_cast<lgsv_ckpt_msg_v5_t *>(ckpt_rec);
    ckpt_rec_type = ckpt_rec_v5->header.ckpt_rec_type;
  } else if (lgs_is_peer_v4()) {
    lgsv_ckpt_msg_v3_t *ckpt_rec_v3 =
        static_cast<lgsv_ckpt_msg_v3_t *>(ckpt_rec);
    ckpt_rec_type = ckpt_rec_v3->header.ckpt_rec_type;
  } else if (lgs_is_peer_v2()) {
    lgsv_ckpt_msg_v2_t *ckpt_rec_v2 =
        static_cast<lgsv_ckpt_msg_v2_t *>(ckpt_rec);
    ckpt_rec_type = ckpt_rec_v2->header.ckpt_rec_type;
  } else {
    lgsv_ckpt_msg_v1_t *ckpt_rec_v1 =
        static_cast<lgsv_ckpt_msg_v1_t *>(ckpt_rec);
    ckpt_rec_type = ckpt_rec_v1->header.ckpt_rec_type;
  }

  /* Fill mbcsv specific data */
  memset(&mbcsv_arg, '\0', sizeof(NCS_MBCSV_ARG));
  mbcsv_arg.i_op = NCS_MBCSV_OP_SEND_CKPT;
  mbcsv_arg.i_mbcsv_hdl = cb->mbcsv_hdl;
  mbcsv_arg.info.send_ckpt.i_action = static_cast<NCS_MBCSV_ACT_TYPE>(action);
  mbcsv_arg.info.send_ckpt.i_ckpt_hdl =
      static_cast<NCS_MBCSV_CKPT_HDL>(cb->mbcsv_ckpt_hdl);
  mbcsv_arg.info.send_ckpt.i_reo_hdl =
      NCS_PTR_TO_UNS64_CAST(ckpt_rec); /*Will be used in encode callback */

  /* Just store the address of the data to be send as an
   * async update record in reo_hdl. The same shall then be
   *dereferenced during encode callback
   */
  mbcsv_arg.info.send_ckpt.i_reo_type = ckpt_rec_type;
  mbcsv_arg.info.send_ckpt.i_send_type = NCS_MBCSV_SND_USR_ASYNC;

  /* Send async update */
  if (NCSCC_RC_SUCCESS != (rc = ncs_mbcsv_svc(&mbcsv_arg))) {
    LOG_ER("%s: MBCSV send FAILED rc=%u.", __FUNCTION__, rc);
    TRACE_LEAVE();
    return NCSCC_RC_FAILURE;
  }

  return rc;
} /*End send_async_update() */

/****************************************************************************
 * Name          : ckpt_peer_info_cbk_handler
 *
 * Description   : This callback is invoked by mbcsv when a peer info message
 *                 is received from LGS STANDBY.
 *
 * Arguments     : NCS_MBCSV_ARG containing info pertaining to the STANDBY.
 *
 * Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE
 *
 * Notes         : None.
 ***************************************************************************/
static uint32_t ckpt_peer_info_cbk_handler(NCS_MBCSV_CB_ARG *arg) {
  osafassert(arg != NULL);

  lgs_cb->mbcsv_peer_version = arg->info.peer.i_peer_version;
  if (lgs_cb->mbcsv_peer_version < LGS_MBCSV_VERSION_MIN) {
    TRACE("peer_version (%d) not correct!!\n", lgs_cb->mbcsv_peer_version);
    return NCSCC_RC_FAILURE;
  }
  TRACE("%s - peer_version = %d", __FUNCTION__, lgs_cb->mbcsv_peer_version);
  return NCSCC_RC_SUCCESS;
}

/****************************************************************************
 * Name          : ckpt_notify_cbk_handler
 *
 * Description   : This callback is invoked by mbcsv when a notify message
 *                 is received from LGS STANDBY.
 *
 * Arguments     : NCS_MBCSV_ARG - contains notification info from STANDBY.
 *
 * Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE
 *
 * Notes         : None.
 ***************************************************************************/
static uint32_t ckpt_notify_cbk_handler(NCS_MBCSV_CB_ARG *arg) {
  /* Currently nothing to be done */
  return NCSCC_RC_SUCCESS;
}

/****************************************************************************
 * Name          : ckpt_err_ind_cbk_handler
 *
 * Description   : This callback is invoked by mbcsv when a notify message
 *                 is received from LGS STANDBY.
 *
 *
 * Arguments     : NCS_MBCSV_ARG - contains notification info from STANDBY.
 *
 * Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE
 *
 * Notes         : None.
 ***************************************************************************/
static uint32_t ckpt_err_ind_cbk_handler(NCS_MBCSV_CB_ARG *arg) {
  /* Currently nothing to be done. */
  return NCSCC_RC_SUCCESS;
}

/****************************************************************************
 * Name          : edp_ed_reg_rec
 *
 * Description   : This function is an EDU program for encoding/decoding
 *                 lgsv checkpoint registration rec.
 *
 * Arguments     : EDU_HDL - pointer to edu handle,
 *                 EDU_TKN - internal edu token to help encode/decode,
 *                 POINTER to the structure to encode/decode from/to,
 *                 data length specifying number of structures,
 *                 EDU_BUF_ENV - pointer to buffer for encoding/decoding.
 *                 op - operation type being encode/decode.
 *                 EDU_ERR - out param to indicate errors in processing.
 *
 * Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE
 *
 * Notes         : None.
 *****************************************************************************/
/**
 * Helper function for encoding/decoding a stream list used by edp_ed_reg_rec()
 *
 * @param edu_hdl
 * @param edu_tkn
 * @param ptr
 * @param ptr_data_len
 * @param buf_env
 * @param op
 * @param o_err
 * @return
 */
uint32_t edp_ed_stream_list(EDU_HDL *edu_hdl, EDU_TKN *edu_tkn, NCSCONTEXT ptr,
                            uint32_t *ptr_data_len, EDU_BUF_ENV *buf_env,
                            EDP_OP_TYPE op, EDU_ERR *o_err) {
  uint32_t rc = NCSCC_RC_SUCCESS;
  lgs_stream_list_t *ckpt_stream_list_msg_ptr = NULL,
                    **ckpt_stream_list_msg_dec_ptr;

  EDU_INST_SET ckpt_stream_list_ed_rules[] = {
      {EDU_START, edp_ed_stream_list, EDQ_LNKLIST, 0, 0,
       sizeof(lgs_stream_list_t), 0, NULL},
      {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
       (long)&((lgs_stream_list_t *)0)->stream_id, 0, NULL},
      {EDU_TEST_LL_PTR, edp_ed_stream_list, 0, 0, 0,
       (long)&((lgs_stream_list_t *)0)->next, 0, NULL},
      {EDU_END, 0, 0, 0, 0, 0, 0, NULL},
  };

  if (op == EDP_OP_TYPE_ENC) {
    ckpt_stream_list_msg_ptr = static_cast<lgs_stream_list_t *>(ptr);
  } else if (op == EDP_OP_TYPE_DEC) {
    ckpt_stream_list_msg_dec_ptr = static_cast<lgs_stream_list_t **>(ptr);
    if (*ckpt_stream_list_msg_dec_ptr == NULL) {
      *ckpt_stream_list_msg_dec_ptr =
          static_cast<lgs_stream_list *>(calloc(1, sizeof(lgs_stream_list_t)));
      if (*ckpt_stream_list_msg_dec_ptr == NULL) {
        LOG_WA("calloc FAILED");
        *o_err = EDU_ERR_MEM_FAIL;
        return NCSCC_RC_FAILURE;
      }
    }
    memset(*ckpt_stream_list_msg_dec_ptr, '\0', sizeof(lgs_stream_list_t));
    ckpt_stream_list_msg_ptr = *ckpt_stream_list_msg_dec_ptr;
  } else {
    ckpt_stream_list_msg_ptr = static_cast<lgs_stream_list *>(ptr);
  }

  rc = m_NCS_EDU_RUN_RULES(edu_hdl, edu_tkn, ckpt_stream_list_ed_rules,
                           ckpt_stream_list_msg_ptr, ptr_data_len, buf_env, op,
                           o_err);
  return rc;
}

uint32_t edp_ed_reg_rec(EDU_HDL *edu_hdl, EDU_TKN *edu_tkn, NCSCONTEXT ptr,
                        uint32_t *ptr_data_len, EDU_BUF_ENV *buf_env,
                        EDP_OP_TYPE op, EDU_ERR *o_err) {
  uint32_t rc = NCSCC_RC_SUCCESS;
  lgs_ckpt_initialize_msg_t *ckpt_reg_msg_ptr = NULL, **ckpt_reg_msg_dec_ptr;

  EDU_INST_SET ckpt_reg_rec_ed_rules[] = {
      {EDU_START, edp_ed_reg_rec, 0, 0, 0, sizeof(lgs_ckpt_initialize_msg_t), 0,
       NULL},
      {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
       (long)&((lgs_ckpt_initialize_msg_t *)0)->client_id, 0, NULL},
      {EDU_EXEC, ncs_edp_mds_dest, 0, 0, 0,
       (long)&((lgs_ckpt_initialize_msg_t *)0)->mds_dest, 0, NULL},
      {EDU_EXEC, edp_ed_stream_list, EDQ_POINTER, 0, 0,
       (long)&((lgs_ckpt_initialize_msg_t *)0)->stream_list, 0, NULL},
      {EDU_END, 0, 0, 0, 0, 0, 0, NULL},
  };

  if (op == EDP_OP_TYPE_ENC) {
    ckpt_reg_msg_ptr = static_cast<lgs_ckpt_initialize_msg_t *>(ptr);
  } else if (op == EDP_OP_TYPE_DEC) {
    ckpt_reg_msg_dec_ptr = static_cast<lgs_ckpt_initialize_msg_t **>(ptr);

    if (*ckpt_reg_msg_dec_ptr == NULL) {
      *o_err = EDU_ERR_MEM_FAIL;
      return NCSCC_RC_FAILURE;
    }
    memset(*ckpt_reg_msg_dec_ptr, '\0', sizeof(lgs_ckpt_initialize_msg_t));
    ckpt_reg_msg_ptr = *ckpt_reg_msg_dec_ptr;
  } else {
    ckpt_reg_msg_ptr = static_cast<lgs_ckpt_initialize_msg_t *>(ptr);
  }

  rc = m_NCS_EDU_RUN_RULES(edu_hdl, edu_tkn, ckpt_reg_rec_ed_rules,
                           ckpt_reg_msg_ptr, ptr_data_len, buf_env, op, o_err);
  return rc;
} /* End edp_ed_reg_rec */

/****************************************************************************
 * Name          : edp_ed_header_rec
 *
 * Description   : This function is an EDU program for encoding/decoding
 *                 lgsv checkpoint message header record.
 *
 * Arguments     : EDU_HDL - pointer to edu handle,
 *                 EDU_TKN - internal edu token to help encode/decode,
 *                 POINTER to the structure to encode/decode from/to,
 *                 data length specifying number of structures,
 *                 EDU_BUF_ENV - pointer to buffer for encoding/decoding.
 *                 op - operation type being encode/decode.
 *                 EDU_ERR - out param to indicate errors in processing.
 *
 * Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE
 *
 * Notes         : None.
 *****************************************************************************/

uint32_t edp_ed_header_rec(EDU_HDL *edu_hdl, EDU_TKN *edu_tkn, NCSCONTEXT ptr,
                           uint32_t *ptr_data_len, EDU_BUF_ENV *buf_env,
                           EDP_OP_TYPE op, EDU_ERR *o_err) {
  uint32_t rc = NCSCC_RC_SUCCESS;
  lgsv_ckpt_header_t *ckpt_header_ptr = NULL, **ckpt_header_dec_ptr;

  EDU_INST_SET ckpt_header_rec_ed_rules[] = {
      {EDU_START, edp_ed_header_rec, 0, 0, 0, sizeof(lgsv_ckpt_header_t), 0,
       NULL},
      {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
       (long)&((lgsv_ckpt_header_t *)0)->ckpt_rec_type, 0, NULL},
      {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
       (long)&((lgsv_ckpt_header_t *)0)->num_ckpt_records, 0, NULL},
      {EDU_EXEC, ncs_edp_uns32, 0, 0, 0,
       (long)&((lgsv_ckpt_header_t *)0)->data_len, 0, NULL},
      {EDU_END, 0, 0, 0, 0, 0, 0, NULL},
  };

  if (op == EDP_OP_TYPE_ENC) {
    ckpt_header_ptr = static_cast<lgsv_ckpt_header_t *>(ptr);
  } else if (op == EDP_OP_TYPE_DEC) {
    ckpt_header_dec_ptr = static_cast<lgsv_ckpt_header_t **>(ptr);
    if (*ckpt_header_dec_ptr == NULL) {
      *o_err = EDU_ERR_MEM_FAIL;
      return NCSCC_RC_FAILURE;
    }
    memset(*ckpt_header_dec_ptr, '\0', sizeof(lgsv_ckpt_header_t));
    ckpt_header_ptr = *ckpt_header_dec_ptr;
  } else {
    ckpt_header_ptr = static_cast<lgsv_ckpt_header_t *>(ptr);
  }
  rc = m_NCS_EDU_RUN_RULES(edu_hdl, edu_tkn, ckpt_header_rec_ed_rules,
                           ckpt_header_ptr, ptr_data_len, buf_env, op, o_err);

  return rc;
} /* End edp_ed_header_rec() */

/****************************************************************************
 * Name          : ckpt_msg_test_type
 *
 * Description   : This function is an EDU_TEST program which returns the
 *                 offset to call the appropriate EDU programe based on
 *                 based on the checkpoint message type, for use by
 *                 the EDU program edu_encode_ckpt_msg.
 *
 * Arguments     : EDU_HDL - pointer to edu handle,
 *                 EDU_TKN - internal edu token to help encode/decode,
 *                 POINTER to the structure to encode/decode from/to,
 *                 data length specifying number of structures,
 *                 EDU_BUF_ENV - pointer to buffer for encoding/decoding.
 *                 op - operation type being encode/decode.
 *                 EDU_ERR - out param to indicate errors in processing.
 *
 * Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE
 *
 * Notes         : None.
 *****************************************************************************/

int32_t ckpt_msg_test_type(NCSCONTEXT arg) {
  enum {
    LCL_TEST_JUMP_OFFSET_LGS_CKPT_REG = 1,
    LCL_TEST_JUMP_OFFSET_LGS_CKPT_FINAL,
    LCL_TEST_JUMP_OFFSET_LGS_CKPT_WRITE_LOG,
    LCL_TEST_JUMP_OFFSET_LGS_CKPT_OPEN_STREAM,
    LCL_TEST_JUMP_OFFSET_LGS_CKPT_CLOSE_STREAM,
    LCL_TEST_JUMP_OFFSET_LGS_CKPT_AGENT_DOWN,
    LCL_TEST_JUMP_OFFSET_LGS_CKPT_CFG_STREAM,
    LCL_TEST_JUMP_OFFSET_LGS_CKPT_LGS_CFG
  };
  lgsv_ckpt_msg_type_t ckpt_rec_type;

  if (arg == NULL) return EDU_FAIL;
  ckpt_rec_type = *static_cast<lgsv_ckpt_msg_type_t *>(arg);

  switch (ckpt_rec_type) {
    case LGS_CKPT_CLIENT_INITIALIZE:
      return LCL_TEST_JUMP_OFFSET_LGS_CKPT_REG;
    case LGS_CKPT_CLIENT_FINALIZE:
      return LCL_TEST_JUMP_OFFSET_LGS_CKPT_FINAL;
    case LGS_CKPT_LOG_WRITE:
      return LCL_TEST_JUMP_OFFSET_LGS_CKPT_WRITE_LOG;
    case LGS_CKPT_OPEN_STREAM:
      return LCL_TEST_JUMP_OFFSET_LGS_CKPT_OPEN_STREAM;
    case LGS_CKPT_CLOSE_STREAM:
      return LCL_TEST_JUMP_OFFSET_LGS_CKPT_CLOSE_STREAM;
    case LGS_CKPT_CLIENT_DOWN:
      return LCL_TEST_JUMP_OFFSET_LGS_CKPT_AGENT_DOWN;
    case LGS_CKPT_CFG_STREAM:
      return LCL_TEST_JUMP_OFFSET_LGS_CKPT_CFG_STREAM;
    case LGS_CKPT_LGS_CFG:
    case LGS_CKPT_LGS_CFG_V3:
    case LGS_CKPT_LGS_CFG_V5:
      return LCL_TEST_JUMP_OFFSET_LGS_CKPT_LGS_CFG;
    default:
      return EDU_EXIT;
      break;
  }
  return EDU_FAIL;
}

/* Non EDU routines */
/****************************************************************************
 * Name          : lgs_enc_ckpt_header
 *
 * Description   : This function encodes the checkpoint message header
 *                 using leap provided apis.
 *
 * Arguments     : pdata - pointer to the buffer to encode this struct in.
 *                 LGS_CKPT_HEADER - lgsv checkpoint message header.
 *
 * Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE
 *
 * Notes         : None.
 *****************************************************************************/

static void enc_ckpt_header(uint8_t *pdata, lgsv_ckpt_header_t header) {
  ncs_encode_32bit(&pdata, header.ckpt_rec_type);
  ncs_encode_32bit(&pdata, header.num_ckpt_records);
  ncs_encode_32bit(&pdata, header.data_len);
}

/****************************************************************************
 * Name          : lgs_dec_ckpt_header
 *
 * Description   : This function decodes the checkpoint message header
 *                 using leap provided apis.
 *
 * Arguments     : NCS_UBAID - pointer to the NCS_UBAID containing data.
 *                 LGS_CKPT_HEADER - lgsv checkpoint message header.
 *
 * Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE
 *
 * Notes         : None.
 *****************************************************************************/

static uint32_t dec_ckpt_header(NCS_UBAID *uba, lgsv_ckpt_header_t *header) {
  uint8_t *p8;
  uint8_t local_data[256];

  TRACE_ENTER();

  if ((uba == NULL) || (header == NULL)) {
    TRACE("NULL pointer, FAILED");
    return NCSCC_RC_FAILURE;
  }

  p8 = ncs_dec_flatten_space(uba, local_data, 4);
  header->ckpt_rec_type =
      static_cast<lgsv_ckpt_msg_type_t>(ncs_decode_32bit(&p8));
  ncs_dec_skip_space(uba, 4);

  p8 = ncs_dec_flatten_space(uba, local_data, 4);
  header->num_ckpt_records = ncs_decode_32bit(&p8);
  ncs_dec_skip_space(uba, 4);

  p8 = ncs_dec_flatten_space(uba, local_data, 4);
  header->data_len = ncs_decode_32bit(&p8);
  ncs_dec_skip_space(uba, 4);

  TRACE_LEAVE();

  return NCSCC_RC_SUCCESS;
} /*End lgs_dec_ckpt_header */
