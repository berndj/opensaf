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

/*****************************************************************************
 * Important information
 * ---------------------
 * To prevent log service thread from "hanging" if communication with NFS is
 * not working or is very slow all functions using file I/O must run their file
 * handling in a separate thread.
 * For more information on how to do that see the following files:
 * lgs_file.h, lgs_file.c, lgs_filendl.c and lgs_filehdl.h
 * Examples can be found in file lgs_stream.c, e.g. function fileopen(...)
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "imm/saf/saImmOm.h"
#include "imm/saf/saImmOi.h"

#include "osaf/immutil/immutil.h"
#include "base/osaf_time.h"
#include "log/logd/lgs.h"
#include "log/logd/lgs_util.h"
#include "log/logd/lgs_file.h"
#include "log/logd/lgs_recov.h"
#include "log/logd/lgs_config.h"
#include "log/logd/lgs_dest.h"
#include "base/saf_error.h"

#include "lgs_mbcsv_v1.h"
#include "lgs_mbcsv_v2.h"
#include "lgs_mbcsv_v3.h"
#include "lgs_mbcsv_v5.h"
#include "lgs_mbcsv_v6.h"

/* TYPE DEFINITIONS
 * ----------------
 */

/* Used for protecting global imm OI handle and selection object during
 * initialize of OI
 */
pthread_mutex_t lgs_OI_init_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Used for checkpointing time when files are closed */
static time_t chkp_file_close_time = 0;

const unsigned int sleep_delay_ms = 500;
const unsigned int max_waiting_time_60s = 60 * 1000; /* 60 secs */
const unsigned int max_waiting_time_10s = 10 * 1000; /* 10 secs */

/* Must be able to index this array using streamType */
static const char *log_file_format[] = {
    DEFAULT_ALM_NOT_FORMAT_EXP, DEFAULT_ALM_NOT_FORMAT_EXP,
    DEFAULT_APP_SYS_FORMAT_EXP, DEFAULT_APP_SYS_FORMAT_EXP};

static SaVersionT immVersion = {'A', 2, 11};

static const SaImmOiImplementerNameT implementerName =
    const_cast<SaImmOiImplementerNameT>("safLogService");
static const SaImmClassNameT logConfig_str =
    const_cast<SaImmClassNameT>("OpenSafLogConfig");
static const SaImmClassNameT streamConfig_str =
    const_cast<SaImmClassNameT>("SaLogStreamConfig");

/* FUNCTIONS
 * ---------
 */

static void report_oi_error(SaImmOiHandleT immOiHandle, SaImmOiCcbIdT ccbId,
                            const char *format, ...)
    __attribute__((format(printf, 3, 4)));

static void report_om_error(SaImmOiHandleT immOiHandle,
                            SaInvocationT invocation, const char *format, ...)
    __attribute__((format(printf, 3, 4)));

/**
 * To be used in OI callbacks to report errors by setting an error string
 * Also writes the same error string using TRACE
 *
 * @param immOiHandle
 * @param ccbId
 * @param format
 * @param ...
 */
static void report_oi_error(SaImmOiHandleT immOiHandle, SaImmOiCcbIdT ccbId,
                            const char *format, ...) {
  char err_str[256];
  va_list ap;

  va_start(ap, format);
  (void)vsnprintf(err_str, 256, format, ap);
  va_end(ap);

  TRACE("%s", err_str);
  (void)saImmOiCcbSetErrorString(immOiHandle, ccbId, err_str);
}

static void report_om_error(SaImmOiHandleT immOiHandle,
                            SaInvocationT invocation, const char *format, ...) {
  char ao_err_string[256];
  SaStringT p_ao_err_string = ao_err_string;
  char admop_err[256];
  strcpy(admop_err, SA_IMM_PARAM_ADMOP_ERROR);
  SaImmAdminOperationParamsT_2 ao_err_param = {admop_err, SA_IMM_ATTR_SASTRINGT,
                                               &p_ao_err_string};
  const SaImmAdminOperationParamsT_2 *ao_err_params[2] = {&ao_err_param, NULL};

  va_list ap;
  va_start(ap, format);
  (void)vsnprintf(ao_err_string, 256, format, ap);
  va_end(ap);

  TRACE("%s", ao_err_string);
  (void)saImmOiAdminOperationResult_o2(immOiHandle, invocation,
                                       SA_AIS_ERR_INVALID_PARAM, ao_err_params);
}

/**
 * Check to see if given DN is well-known stream or notE
 *
 * @param dn stream DN
 * @ret true if well-known stream, false otherwise
 */
static bool is_well_know_stream(const char *dn) {
  if (strcmp(dn, SA_LOG_STREAM_ALARM) == 0) return true;
  if (strcmp(dn, SA_LOG_STREAM_NOTIFICATION) == 0) return true;
  if (strcmp(dn, SA_LOG_STREAM_SYSTEM) == 0) return true;

  return false;
}

/**
 * Pack and send a log service config checkpoint using mbcsv
 * @param stream
 *
 * @return NCSCC_RC_... error code
 */
static uint32_t ckpt_lgs_cfg(bool is_root_dir_changed,
                             lgs_config_chg_t *v5_ckpt) {
  void *ckpt = NULL;
  lgsv_ckpt_msg_v2_t ckpt_v2;
  lgsv_ckpt_msg_v3_t ckpt_v3;
  lgsv_ckpt_msg_v5_t ckpt_v5;
  uint32_t rc = NCSCC_RC_SUCCESS;

  TRACE_ENTER();

  if (!lgs_is_peer_v2()) {
    /* Can be called only if we are OI. This is not the case if Standby */
    TRACE("%s Called when check-pointing version 1", __FUNCTION__);
    return NCSCC_RC_FAILURE;
  }

  if (lgs_is_peer_v5()) {
    /* Checkpoint version 5 */
    memset(&ckpt_v5, 0, sizeof(ckpt_v5));
    ckpt_v5.header.ckpt_rec_type = LGS_CKPT_LGS_CFG_V5;
    ckpt_v5.header.num_ckpt_records = 1;
    ckpt_v5.header.data_len = 1;
    ckpt_v5.ckpt_rec.lgs_cfg.c_file_close_time_stamp = chkp_file_close_time;
    ckpt_v5.ckpt_rec.lgs_cfg.buffer = v5_ckpt->ckpt_buffer_ptr;
    ckpt_v5.ckpt_rec.lgs_cfg.buffer_size = v5_ckpt->ckpt_buffer_size;
    ckpt = &ckpt_v5;
    TRACE("\tCheck-pointing v5");
  } else if (lgs_is_peer_v4()) {
    /* Checkpoint version 4 */
    memset(&ckpt_v3, 0, sizeof(ckpt_v3));
    ckpt_v3.header.ckpt_rec_type = LGS_CKPT_LGS_CFG_V3;
    ckpt_v3.header.num_ckpt_records = 1;
    ckpt_v3.header.data_len = 1;
    ckpt_v3.ckpt_rec.lgs_cfg.logRootDirectory = const_cast<char *>(
        static_cast<const char *>(lgs_cfg_get(LGS_IMM_LOG_ROOT_DIRECTORY)));
    ckpt_v3.ckpt_rec.lgs_cfg.logDataGroupname = const_cast<char *>(
        static_cast<const char *>(lgs_cfg_get(LGS_IMM_DATA_GROUPNAME)));
    ckpt_v3.ckpt_rec.lgs_cfg.c_file_close_time_stamp = chkp_file_close_time;
    ckpt = &ckpt_v3;
    TRACE("\tCheck-pointing v3 peer is v4");
  } else {
    /* Checkpoint version 3 or 2 */
    if (is_root_dir_changed) {
      memset(&ckpt_v2, 0, sizeof(ckpt_v2));
      ckpt_v2.header.ckpt_rec_type = LGS_CKPT_LGS_CFG;
      ckpt_v2.header.num_ckpt_records = 1;
      ckpt_v2.header.data_len = 1;
      ckpt_v2.ckpt_rec.lgs_cfg.logRootDirectory = const_cast<char *>(
          static_cast<const char *>(lgs_cfg_get(LGS_IMM_LOG_ROOT_DIRECTORY)));
      ckpt_v2.ckpt_rec.lgs_cfg.c_file_close_time_stamp = chkp_file_close_time;
      ckpt = &ckpt_v2;
    }
    TRACE("\tCheck-pointing v2 peer < v4");
  }

  if (ckpt) {
    rc = lgs_ckpt_send_async(lgs_cb, ckpt, NCS_MBCSV_ACT_ADD);
  }

  TRACE_LEAVE();
  return rc;
}

/**
 * Pack and send a stream checkpoint using mbcsv
 * @param stream
 *
 * @return NCSCC_RC_... error code
 */
static uint32_t ckpt_stream_config(log_stream_t *stream) {
  uint32_t rc;
  lgsv_ckpt_msg_v1_t ckpt_v1;
  lgsv_ckpt_msg_v2_t ckpt_v2;
  lgsv_ckpt_msg_v6_t ckpt_v3;

  void *ckpt_ptr;

  TRACE_ENTER();

  if (lgs_is_peer_v6()) {
    memset(&ckpt_v3, 0, sizeof(ckpt_v3));
    ckpt_v3.header.ckpt_rec_type = LGS_CKPT_CFG_STREAM;
    ckpt_v3.header.num_ckpt_records = 1;
    ckpt_v3.header.data_len = 1;

    ckpt_v3.ckpt_rec.stream_cfg.name = const_cast<char *>(stream->name.c_str());
    ckpt_v3.ckpt_rec.stream_cfg.fileName =
        const_cast<char *>(stream->fileName.c_str());
    ckpt_v3.ckpt_rec.stream_cfg.pathName =
        const_cast<char *>(stream->pathName.c_str());
    ckpt_v3.ckpt_rec.stream_cfg.maxLogFileSize = stream->maxLogFileSize;
    ckpt_v3.ckpt_rec.stream_cfg.fixedLogRecordSize = stream->fixedLogRecordSize;
    ckpt_v3.ckpt_rec.stream_cfg.logFullAction = stream->logFullAction;
    ckpt_v3.ckpt_rec.stream_cfg.logFullHaltThreshold =
        stream->logFullHaltThreshold;
    ckpt_v3.ckpt_rec.stream_cfg.maxFilesRotated = stream->maxFilesRotated;
    ckpt_v3.ckpt_rec.stream_cfg.logFileFormat = stream->logFileFormat;
    ckpt_v3.ckpt_rec.stream_cfg.severityFilter = stream->severityFilter;
    ckpt_v3.ckpt_rec.stream_cfg.logFileCurrent =
        const_cast<char *>(stream->logFileCurrent.c_str());
    ckpt_v3.ckpt_rec.stream_cfg.dest_names =
        const_cast<char *>(stream->stb_dest_names.c_str());
    ckpt_v3.ckpt_rec.stream_cfg.c_file_close_time_stamp =
        stream->act_last_close_timestamp;
    ckpt_ptr = &ckpt_v3;
  } else if (lgs_is_peer_v2()) {
    memset(&ckpt_v2, 0, sizeof(ckpt_v2));
    ckpt_v2.header.ckpt_rec_type = LGS_CKPT_CFG_STREAM;
    ckpt_v2.header.num_ckpt_records = 1;
    ckpt_v2.header.data_len = 1;

    ckpt_v2.ckpt_rec.stream_cfg.name = const_cast<char *>(stream->name.c_str());
    ckpt_v2.ckpt_rec.stream_cfg.fileName =
        const_cast<char *>(stream->fileName.c_str());
    ckpt_v2.ckpt_rec.stream_cfg.pathName =
        const_cast<char *>(stream->pathName.c_str());
    ckpt_v2.ckpt_rec.stream_cfg.maxLogFileSize = stream->maxLogFileSize;
    ckpt_v2.ckpt_rec.stream_cfg.fixedLogRecordSize = stream->fixedLogRecordSize;
    ckpt_v2.ckpt_rec.stream_cfg.logFullAction = stream->logFullAction;
    ckpt_v2.ckpt_rec.stream_cfg.logFullHaltThreshold =
        stream->logFullHaltThreshold;
    ckpt_v2.ckpt_rec.stream_cfg.maxFilesRotated = stream->maxFilesRotated;
    ckpt_v2.ckpt_rec.stream_cfg.logFileFormat = stream->logFileFormat;
    ckpt_v2.ckpt_rec.stream_cfg.severityFilter = stream->severityFilter;
    ckpt_v2.ckpt_rec.stream_cfg.logFileCurrent =
        const_cast<char *>(stream->logFileCurrent.c_str());
    ckpt_v2.ckpt_rec.stream_cfg.c_file_close_time_stamp =
        stream->act_last_close_timestamp;
    ckpt_ptr = &ckpt_v2;
  } else {
    memset(&ckpt_v2, 0, sizeof(ckpt_v2));
    ckpt_v1.header.ckpt_rec_type = LGS_CKPT_CFG_STREAM;
    ckpt_v1.header.num_ckpt_records = 1;
    ckpt_v1.header.data_len = 1;

    ckpt_v1.ckpt_rec.stream_cfg.name = const_cast<char *>(stream->name.c_str());
    ckpt_v1.ckpt_rec.stream_cfg.fileName =
        const_cast<char *>(stream->fileName.c_str());
    ckpt_v1.ckpt_rec.stream_cfg.pathName =
        const_cast<char *>(stream->pathName.c_str());
    ckpt_v1.ckpt_rec.stream_cfg.maxLogFileSize = stream->maxLogFileSize;
    ckpt_v1.ckpt_rec.stream_cfg.fixedLogRecordSize = stream->fixedLogRecordSize;
    ckpt_v1.ckpt_rec.stream_cfg.logFullAction = stream->logFullAction;
    ckpt_v1.ckpt_rec.stream_cfg.logFullHaltThreshold =
        stream->logFullHaltThreshold;
    ckpt_v1.ckpt_rec.stream_cfg.maxFilesRotated = stream->maxFilesRotated;
    ckpt_v1.ckpt_rec.stream_cfg.logFileFormat = stream->logFileFormat;
    ckpt_v1.ckpt_rec.stream_cfg.severityFilter = stream->severityFilter;
    ckpt_v1.ckpt_rec.stream_cfg.logFileCurrent =
        const_cast<char *>(stream->logFileCurrent.c_str());
    ckpt_ptr = &ckpt_v1;
  }

  rc = lgs_ckpt_send_async(lgs_cb, ckpt_ptr, NCS_MBCSV_ACT_ADD);

  TRACE_LEAVE();
  return rc;
}

/**
 * Pack and send a close stream checkpoint using mbcsv
 * @param stream
 * @param recType
 *
 * @return uint32
 */
static uint32_t ckpt_stream_close(log_stream_t *stream, time_t closetime) {
  uint32_t rc;
  lgsv_ckpt_msg_v1_t ckpt_v1;
  lgsv_ckpt_msg_v2_t ckpt_v2;
  void *ckpt_ptr;

  TRACE_ENTER();

  if (lgs_is_peer_v2()) {
    memset(&ckpt_v2, 0, sizeof(ckpt_v2));
    ckpt_v2.header.ckpt_rec_type = LGS_CKPT_CLOSE_STREAM;
    ckpt_v2.header.num_ckpt_records = 1;
    ckpt_v2.header.data_len = 1;

    /* No client. Logservice itself has opened stream */
    ckpt_v2.ckpt_rec.stream_close.clientId = -1;
    ckpt_v2.ckpt_rec.stream_close.streamId = stream->streamId;
    ckpt_v2.ckpt_rec.stream_close.c_file_close_time_stamp = closetime;
    ckpt_ptr = &ckpt_v2;
  } else {
    memset(&ckpt_v1, 0, sizeof(ckpt_v1));
    ckpt_v1.header.ckpt_rec_type = LGS_CKPT_CLOSE_STREAM;
    ckpt_v1.header.num_ckpt_records = 1;
    ckpt_v1.header.data_len = 1;

    /* No client. Logservice itself has opened stream */
    ckpt_v1.ckpt_rec.stream_close.clientId = -1;
    ckpt_v1.ckpt_rec.stream_close.streamId = stream->streamId;
    ckpt_ptr = &ckpt_v1;
  }

  rc = lgs_ckpt_send_async(lgs_cb, ckpt_ptr, NCS_MBCSV_ACT_ADD);

  TRACE_LEAVE();
  return rc;
}

/**
 * LOG Admin operation handling. This function is executed as an
 * IMM callback. In LOG A.02.01 only SA_LOG_ADMIN_CHANGE_FILTER
 * is supported.
 *
 * @param immOiHandle
 * @param invocation
 * @param objectName
 * @param opId
 * @param params
 */
static void adminOperationCallback(
    SaImmOiHandleT immOiHandle, SaInvocationT invocation,
    const SaNameT *objectName, SaImmAdminOperationIdT opId,
    const SaImmAdminOperationParamsT_2 **params) {
  SaUint32T severityFilter;
  const SaImmAdminOperationParamsT_2 *param = params[0];
  log_stream_t *stream;
  SaAisErrorT ais_rc = SA_AIS_OK;
  SaConstStringT objName = osaf_extended_name_borrow(objectName);

  TRACE_ENTER2("%s", objName);

  if (lgs_cb->ha_state != SA_AMF_HA_ACTIVE) {
    LOG_ER("admin op callback in applier");
    goto done;
  }

  if ((stream = log_stream_get_by_name(objName)) == NULL) {
    report_om_error(immOiHandle, invocation, "Stream %s not found", objName);
    goto done;
  }

  if (opId == SA_LOG_ADMIN_CHANGE_FILTER) {
    /* Only allowed to update runtime objects (application streams) */
    if (stream->streamType != STREAM_TYPE_APPLICATION) {
      report_om_error(immOiHandle, invocation,
                      "Admin op change filter for non app stream");
      goto done;
    }

    /* Not allow perform adm op on configurable app streams */
    if (stream->isRtStream != SA_TRUE) {
      ais_rc = immutil_saImmOiAdminOperationResult(immOiHandle, invocation,
                                                   SA_AIS_ERR_NOT_SUPPORTED);
      if (ais_rc != SA_AIS_OK) {
        LOG_ER("immutil_saImmOiAdminOperationResult failed %s",
               saf_error(ais_rc));
        osaf_abort(0);
      }

      goto done;
    }

    if (param == NULL) {
      /* No parameters given in admin op */
      report_om_error(immOiHandle, invocation,
                      "Admin op change filter: parameters are missing");
      goto done;
    }

    if (strcmp(param->paramName, "saLogStreamSeverityFilter") != 0) {
      report_om_error(immOiHandle, invocation,
                      "Admin op change filter, invalid param name");
      goto done;
    }

    if (param->paramType != SA_IMM_ATTR_SAUINT32T) {
      report_om_error(immOiHandle, invocation,
                      "Admin op change filter: invalid parameter type");
      goto done;
    }

    severityFilter = *((SaUint32T *)param->paramBuffer);

    if (severityFilter > 0x7f) { /* not a level, a bitmap */
      report_om_error(immOiHandle, invocation,
                      "Admin op change filter: invalid severity");
      goto done;
    }

    if (severityFilter == stream->severityFilter) {
      ais_rc = immutil_saImmOiAdminOperationResult(immOiHandle, invocation,
                                                   SA_AIS_ERR_NO_OP);
      if (ais_rc != SA_AIS_OK) {
        LOG_ER("immutil_saImmOiAdminOperationResult failed %s",
               saf_error(ais_rc));
        osaf_abort(0);
      }

      goto done;
    }

    TRACE("Changing severity for stream %s to %u", stream->name.c_str(),
          severityFilter);
    stream->severityFilter = severityFilter;

    ais_rc = immutil_update_one_rattr(
        immOiHandle, objName,
        const_cast<SaImmAttrNameT>("saLogStreamSeverityFilter"),
        SA_IMM_ATTR_SAUINT32T, &stream->severityFilter);
    if (ais_rc != SA_AIS_OK) {
      LOG_ER("immutil_update_one_rattr failed %s", saf_error(ais_rc));
      osaf_abort(0);
    }

    ais_rc =
        immutil_saImmOiAdminOperationResult(immOiHandle, invocation, SA_AIS_OK);
    if (ais_rc != SA_AIS_OK) {
      LOG_ER("immutil_saImmOiAdminOperationResult failed %s",
             saf_error(ais_rc));
      osaf_abort(0);
    }

    /* Send changed severity filter to clients */
    lgs_send_severity_filter_to_clients(stream->streamId, severityFilter);

    /* Checkpoint to standby LOG server */
    ckpt_stream_config(stream);
  } else {
    report_om_error(
        immOiHandle, invocation,
        "Invalid operation ID, should be %d (one) for change filter",
        SA_LOG_ADMIN_CHANGE_FILTER);
  }

done:
  TRACE_LEAVE();
}

static SaAisErrorT ccbObjectDeleteCallback(SaImmOiHandleT immOiHandle,
                                           SaImmOiCcbIdT ccbId,
                                           const SaNameT *objectName) {
  SaAisErrorT rc = SA_AIS_OK;
  struct CcbUtilCcbData *ccbUtilCcbData;
  SaConstStringT objName = osaf_extended_name_borrow(objectName);

  TRACE_ENTER2("CCB ID %llu, '%s'", ccbId, objName);

  if ((ccbUtilCcbData = ccbutil_findCcbData(ccbId)) == NULL) {
    if ((ccbUtilCcbData = ccbutil_getCcbData(ccbId)) == NULL) {
      report_oi_error(immOiHandle, ccbId, "Failed to get CCB object for %llu",
                      ccbId);
      rc = SA_AIS_ERR_NO_MEMORY;
      goto done;
    }
  }

  ccbutil_ccbAddDeleteOperation(ccbUtilCcbData, objectName);

done:
  TRACE_LEAVE();
  return rc;
}

static SaAisErrorT ccbObjectCreateCallback(SaImmOiHandleT immOiHandle,
                                           SaImmOiCcbIdT ccbId,
                                           const SaImmClassNameT className,
                                           const SaNameT *parentName,
                                           const SaImmAttrValuesT_2 **attr) {
  SaAisErrorT rc = SA_AIS_OK;
  struct CcbUtilCcbData *ccbUtilCcbData;
  SaConstStringT parName = osaf_extended_name_borrow(parentName);

  TRACE_ENTER2("CCB ID %llu, class '%s', parent '%s'", ccbId, className,
               parName);

  if ((ccbUtilCcbData = ccbutil_findCcbData(ccbId)) == NULL) {
    if ((ccbUtilCcbData = ccbutil_getCcbData(ccbId)) == NULL) {
      report_oi_error(immOiHandle, ccbId, "Failed to get CCB object for %llu",
                      ccbId);
      rc = SA_AIS_ERR_NO_MEMORY;
      goto done;
    }
  }

  ccbutil_ccbAddCreateOperation(ccbUtilCcbData, className, parentName, attr);

done:
  TRACE_LEAVE();
  return rc;
}

/**
 * Validate and "memorize" a change request
 *
 * @param immOiHandle
 * @param ccbId
 * @param objectName
 * @param attrMods
 * @return
 */
static SaAisErrorT ccbObjectModifyCallback(
    SaImmOiHandleT immOiHandle, SaImmOiCcbIdT ccbId, const SaNameT *objectName,
    const SaImmAttrModificationT_2 **attrMods) {
  SaAisErrorT rc = SA_AIS_OK;
  struct CcbUtilCcbData *ccbUtilCcbData;
  SaConstStringT objName = osaf_extended_name_borrow(objectName);

  TRACE_ENTER2("CCB ID %llu, '%s'", ccbId, objName);

  if ((ccbUtilCcbData = ccbutil_findCcbData(ccbId)) == NULL) {
    if ((ccbUtilCcbData = ccbutil_getCcbData(ccbId)) == NULL) {
      report_oi_error(immOiHandle, ccbId, "Failed to get CCB object for %llu",
                      ccbId);
      rc = SA_AIS_ERR_NO_MEMORY;
      goto done;
    }
  }

  ccbutil_ccbAddModifyOperation(ccbUtilCcbData, objectName, attrMods);

done:
  TRACE_LEAVE();
  return rc;
}

/**
 * Creation of log service configuration object. Not allowed
 *
 * @param immOiHandle
 * @param opdata
 * @return
 */
static SaAisErrorT config_ccb_completed_create(
    SaImmOiHandleT immOiHandle, const CcbUtilOperationData_t *opdata) {
  SaAisErrorT rc = SA_AIS_ERR_BAD_OPERATION;

  TRACE_ENTER2("CCB ID %llu", opdata->ccbId);
  report_oi_error(
      immOiHandle, opdata->ccbId,
      "Creation of OpenSafLogConfig object is not allowed, ccbId = %llu",
      opdata->ccbId);
  TRACE_LEAVE2("%u", rc);
  return rc;
}

/* =================================================
 * config_ccb_completed_modify() with help functions
 * =================================================
 */

struct vattr_t {
  /* true if any of the attributes has been changed */
  bool validate_flag;
  SaUint32T logStreamSystemHighLimit;
  /* true if this attribute has been changed */
  bool logStreamSystemHighLimit_changed;
  SaUint32T logStreamSystemLowLimit;
  bool logStreamSystemLowLimit_changed;
  SaUint32T logStreamAppHighLimit;
  bool logStreamAppHighLimit_changed;
  SaUint32T logStreamAppLowLimit;
  bool logStreamAppLowLimit_changed;
};

/**
 * Validate attributes:
 * Must be done after all attributes has been read in order to check against
 * the correct values.
 * See lgs_config for validation functions
 *
 * @param vattr[in] struct with attributes to validate
 * @param err_str [out] char vector of 256 bytes to return a string.
 * @return error code
 */
static SaAisErrorT validate_mailbox_limits(struct vattr_t vattr,
                                           char *err_str) {
  SaUint32T value32_high = 0;
  SaUint32T value32_low = 0;
  SaAisErrorT ais_rc = SA_AIS_OK;
  int rc = 0;

  TRACE_ENTER();

  /* Validate attributes that can be modified */
  if (vattr.logStreamSystemHighLimit_changed) {
    value32_high = vattr.logStreamSystemHighLimit;
    if (vattr.logStreamSystemLowLimit_changed) {
      value32_low = vattr.logStreamSystemLowLimit;
    } else {
      value32_low = *static_cast<const SaUint32T *>(
          lgs_cfg_get(LGS_IMM_LOG_STREAM_SYSTEM_LOW_LIMIT));
    }

    rc = lgs_cfg_verify_mbox_limit(value32_high, value32_low);
    if (rc == -1) {
      ais_rc = SA_AIS_ERR_BAD_OPERATION;
      snprintf(err_str, 256, "HIGH limit < LOW limit");
      goto done;
    }
  }

  if (vattr.logStreamSystemLowLimit_changed) {
    value32_low = vattr.logStreamSystemLowLimit;
    if (vattr.logStreamSystemHighLimit_changed) {
      value32_high = vattr.logStreamSystemHighLimit;
    } else {
      value32_high = *static_cast<const SaUint32T *>(
          lgs_cfg_get(LGS_IMM_LOG_STREAM_SYSTEM_HIGH_LIMIT));
    }

    rc = lgs_cfg_verify_mbox_limit(value32_high, value32_low);
    if (rc == -1) {
      ais_rc = SA_AIS_ERR_BAD_OPERATION;
      snprintf(err_str, 256, "HIGH limit < LOW limit");
      goto done;
    }
  }

  if (vattr.logStreamAppHighLimit_changed) {
    value32_high = vattr.logStreamAppHighLimit;
    if (vattr.logStreamAppLowLimit_changed) {
      value32_low = vattr.logStreamAppLowLimit;
    } else {
      value32_low = *static_cast<const SaUint32T *>(
          lgs_cfg_get(LGS_IMM_LOG_STREAM_APP_LOW_LIMIT));
    }

    rc = lgs_cfg_verify_mbox_limit(value32_high, value32_low);
    if (rc == -1) {
      ais_rc = SA_AIS_ERR_BAD_OPERATION;
      snprintf(err_str, 256, "HIGH limit < LOW limit");
      goto done;
    }
  }

  if (vattr.logStreamAppLowLimit_changed) {
    value32_low = vattr.logStreamAppLowLimit;
    if (vattr.logStreamAppHighLimit_changed) {
      value32_high = vattr.logStreamAppHighLimit;
    } else {
      value32_high = *static_cast<const SaUint32T *>(
          lgs_cfg_get(LGS_IMM_LOG_STREAM_APP_HIGH_LIMIT));
    }

    rc = lgs_cfg_verify_mbox_limit(value32_high, value32_low);
    if (rc == -1) {
      ais_rc = SA_AIS_ERR_BAD_OPERATION;
      snprintf(err_str, 256, "HIGH limit < LOW limit");
      goto done;
    }
  }

done:
  TRACE_LEAVE2("rc = %d", ais_rc);
  return ais_rc;
}

/**
 * Modification of attributes in log service configuration object.
 * Validate given attribute changes and report result to IMM
 *
 * @param immOiHandle
 * @param opdata
 * @return
 */
static SaAisErrorT config_ccb_completed_modify(
    SaImmOiHandleT immOiHandle, const CcbUtilOperationData_t *opdata) {
  const SaImmAttrModificationT_2 *attrMod;
  SaAisErrorT ais_rc = SA_AIS_OK;
  int rc = 0;
  int i = 0;

  struct vattr_t vattr = {
      .validate_flag = false,
      .logStreamSystemHighLimit = 0,
      .logStreamSystemHighLimit_changed = false,
      .logStreamSystemLowLimit = 0,
      .logStreamSystemLowLimit_changed = false,
      .logStreamAppHighLimit = 0,
      .logStreamAppHighLimit_changed = false,
      .logStreamAppLowLimit = 0,
      .logStreamAppLowLimit_changed = false,
  };

  char oi_err_str[256];
  SaConstStringT objName = osaf_extended_name_borrow(&opdata->objectName);
  TRACE_ENTER2("CCB ID %llu, '%s'", opdata->ccbId, objName);

  attrMod = opdata->param.modify.attrMods[i++];
  while (attrMod != NULL) {
    void *value = NULL;
    const SaImmAttrValuesT_2 *attribute = &attrMod->modAttr;

    TRACE("attribute %s", attribute->attrName);

    /**
     *  Ignore deletion of attributes
     *  except for logDataGroupname,
     *  logStreamFileFormat and
     *  logRecordDestinationConfiguration
     */
    if ((strcmp(attribute->attrName, LOG_DATA_GROUPNAME) != 0) &&
        (strcmp(attribute->attrName, LOG_STREAM_FILE_FORMAT) != 0) &&
        (strcmp(attribute->attrName, LOG_RECORD_DESTINATION_CONFIGURATION) !=
         0) &&
        (attribute->attrValuesNumber == 0)) {
      report_oi_error(
          immOiHandle, opdata->ccbId,
          "deletion of value is not allowed for attribute %s stream %s",
          attribute->attrName, objName);
      ais_rc = SA_AIS_ERR_BAD_OPERATION;
      goto done;
    }

    if (attribute->attrValuesNumber != 0) {
      value = attribute->attrValues[0];
    }

    if (!strcmp(attribute->attrName, LOG_ROOT_DIRECTORY)) {
      if (attribute->attrValuesNumber != 0) {
        std::string pathName = *(static_cast<char **>(value));
        if (lgs_cfg_verify_root_dir(pathName) != 0) {
          report_oi_error(immOiHandle, opdata->ccbId,
                          "pathName: %s is NOT accepted", pathName.c_str());
          ais_rc = SA_AIS_ERR_BAD_OPERATION;
          goto done;
        }
        TRACE("pathName: %s is accepted", pathName.c_str());
      }
    } else if (!strcmp(attribute->attrName, LOG_DATA_GROUPNAME)) {
      if (attribute->attrValuesNumber == 0) {
        TRACE("Deleting log data group");
      } else {
        char *groupname = *((char **)value);
        rc = lgs_cfg_verify_log_data_groupname(groupname);
        if (rc == -1) {
          report_oi_error(immOiHandle, opdata->ccbId,
                          "groupname: %s is NOT accepted", groupname);
          ais_rc = SA_AIS_ERR_INVALID_PARAM;
          goto done;
        }
        TRACE("groupname: %s is accepted", groupname);
      }
    } else if (!strcmp(attribute->attrName, LOG_STREAM_FILE_FORMAT)) {
      if (attribute->attrValuesNumber == 0) {
        TRACE("Delete logStreamFileFormat");
      } else {
        char *logFileFormat = *((char **)value);
        rc = lgs_cfg_verify_log_file_format(logFileFormat);
        if (rc == -1) {
          report_oi_error(immOiHandle, opdata->ccbId,
                          "%s value is NOT accepted", attribute->attrName);
          ais_rc = SA_AIS_ERR_INVALID_PARAM;
          goto done;
        }
        TRACE("logFileFormat: %s value is accepted", logFileFormat);
      }
    } else if (!strcmp(attribute->attrName, LOG_MAX_LOGRECSIZE)) {
      SaUint32T maxLogRecordSize = *((SaUint32T *)value);
      rc = lgs_cfg_verify_max_logrecsize(maxLogRecordSize);
      if (rc == -1) {
        report_oi_error(immOiHandle, opdata->ccbId, "%s value is NOT accepted",
                        attribute->attrName);
        ais_rc = SA_AIS_ERR_INVALID_PARAM;
        goto done;
      }
      TRACE("maxLogRecordSize: %s is accepted", attribute->attrName);
    } else if (!strcmp(attribute->attrName, LOG_STREAM_SYSTEM_HIGH_LIMIT)) {
      vattr.logStreamSystemHighLimit = *((SaUint32T *)value);
      vattr.logStreamSystemHighLimit_changed = true;
      TRACE("%s %s = %d", __FUNCTION__, attribute->attrName,
            vattr.logStreamSystemHighLimit);
    } else if (!strcmp(attribute->attrName, LOG_STREAM_SYSTEM_LOW_LIMIT)) {
      vattr.logStreamSystemLowLimit = *((SaUint32T *)value);
      vattr.logStreamSystemLowLimit_changed = true;
      TRACE("%s %s = %d", __FUNCTION__, attribute->attrName,
            vattr.logStreamSystemHighLimit);
    } else if (!strcmp(attribute->attrName, LOG_STREAM_APP_HIGH_LIMIT)) {
      vattr.logStreamAppHighLimit = *((SaUint32T *)value);
      vattr.logStreamAppHighLimit_changed = true;
      TRACE("%s %s = %d", __FUNCTION__, attribute->attrName,
            vattr.logStreamSystemHighLimit);
    } else if (!strcmp(attribute->attrName, LOG_STREAM_APP_LOW_LIMIT)) {
      vattr.logStreamAppLowLimit = *((SaUint32T *)value);
      vattr.logStreamAppLowLimit_changed = true;
      TRACE("%s %s = %d", __FUNCTION__, attribute->attrName,
            vattr.logStreamSystemHighLimit);
    } else if (!strcmp(attribute->attrName, LOG_MAX_APPLICATION_STREAMS)) {
      report_oi_error(immOiHandle, opdata->ccbId, "%s cannot be changed",
                      attribute->attrName);
      ais_rc = SA_AIS_ERR_FAILED_OPERATION;
      goto done;
    } else if (!strcmp(attribute->attrName, LOG_FILE_IO_TIMEOUT)) {
      SaUint32T logFileIoTimeout = *((SaUint32T *)value);
      rc = lgs_cfg_verify_file_io_timeout(logFileIoTimeout);
      if (rc == -1) {
        report_oi_error(immOiHandle, opdata->ccbId, "%s value is NOT accepted",
                        attribute->attrName);
        ais_rc = SA_AIS_ERR_INVALID_PARAM;
        goto done;
      }
      TRACE("logFileIoTimeout: %d value is accepted", logFileIoTimeout);
    } else if (!strcmp(attribute->attrName, LOG_FILE_SYS_CONFIG)) {
      report_oi_error(immOiHandle, opdata->ccbId, "%s cannot be changed",
                      attribute->attrName);
      ais_rc = SA_AIS_ERR_FAILED_OPERATION;
      goto done;
    } else if (!strcmp(attribute->attrName,
                       LOG_RECORD_DESTINATION_CONFIGURATION)) {
      // Note: Multi value attribute
      TRACE("logRecordDestinationConfiguration. Values number = %d",
            attribute->attrValuesNumber);
      std::vector<std::string> values_vector{};
      for (uint32_t i = 0; i < attribute->attrValuesNumber; i++) {
        value = attribute->attrValues[i];
        char *value_str = *(reinterpret_cast<char **>(value));
        values_vector.push_back(value_str);
      }
      rc = lgs_cfg_verify_log_record_destination_configuration(
          values_vector, attrMod->modType);
      if (rc == -1) {
        report_oi_error(immOiHandle, opdata->ccbId, "%s value is NOT accepted",
                        attribute->attrName);
        ais_rc = SA_AIS_ERR_INVALID_PARAM;
        goto done;
      }
    } else {
      report_oi_error(immOiHandle, opdata->ccbId, "attribute %s not recognized",
                      attribute->attrName);
      ais_rc = SA_AIS_ERR_FAILED_OPERATION;
      goto done;
    }

    attrMod = opdata->param.modify.attrMods[i++];
  }

  ais_rc = validate_mailbox_limits(vattr, oi_err_str);
  if (ais_rc != SA_AIS_OK) {
    TRACE("Reporting oi error \"%s\"", oi_err_str);
    report_oi_error(immOiHandle, opdata->ccbId, "%s", oi_err_str);
  }

done:
  TRACE_LEAVE2("ais_rc='%s'", saf_error(ais_rc));
  return ais_rc;
}

/**
 * Delete log service configuration object. Not allowed
 *
 * @param immOiHandle
 * @param opdata
 * @return
 */
static SaAisErrorT config_ccb_completed_delete(
    SaImmOiHandleT immOiHandle, const CcbUtilOperationData_t *opdata) {
  SaAisErrorT rc = SA_AIS_ERR_BAD_OPERATION;

  TRACE_ENTER2("CCB ID %llu", opdata->ccbId);
  report_oi_error(immOiHandle, opdata->ccbId,
                  "Deletion of OpenSafLogConfig object is not allowed");
  TRACE_LEAVE2("%u", rc);
  return rc;
}

static SaAisErrorT config_ccb_completed(SaImmOiHandleT immOiHandle,
                                        const CcbUtilOperationData_t *opdata) {
  SaAisErrorT rc = SA_AIS_ERR_BAD_OPERATION;

  TRACE_ENTER2("CCB ID %llu", opdata->ccbId);

  switch (opdata->operationType) {
    case CCBUTIL_CREATE:
      rc = config_ccb_completed_create(immOiHandle, opdata);
      break;
    case CCBUTIL_MODIFY:
      rc = config_ccb_completed_modify(immOiHandle, opdata);
      break;
    case CCBUTIL_DELETE:
      rc = config_ccb_completed_delete(immOiHandle, opdata);
      break;
    default:
      assert(0);
      break;
  }

  TRACE_LEAVE2("%u", rc);
  return rc;
}

/**********************************************
 * Help functions for check_attr_validity(...)
 **********************************************/

typedef struct {
  /* Store default values for the stream config class. The values are fetched
   * using saImmOmClassDescriptionGet(...)
   * Note: Only values relevant for validity checks are stored.
   */
  SaUint64T saLogStreamMaxLogFileSize;
  SaUint32T saLogStreamFixedLogRecordSize;
} lgs_stream_defval_t;

static bool lgs_stream_defval_updated_flag = false;
static lgs_stream_defval_t lgs_stream_defval;

/**
 * Return a stream config class default values. Fetched using imm om interface
 *
 * @return  struct of type lgs_stream_defval_t
 */
static lgs_stream_defval_t *get_SaLogStreamConfig_default() {
  SaImmHandleT om_handle = 0;
  SaAisErrorT rc = SA_AIS_OK;
  SaImmClassCategoryT cc;
  SaImmAttrDefinitionT_2 **attributes = NULL;
  SaImmAttrDefinitionT_2 *attribute = NULL;

  TRACE_ENTER();
  if (lgs_stream_defval_updated_flag == false) {
    /* Get class defaults for SaLogStreamConfig class from IMM
     * We are only interested in saLogStreamMaxLogFileSize and
     * saLogStreamFixedLogRecordSize
     */
    rc = immutil_saImmOmInitialize(&om_handle, NULL, &immVersion);
    if (rc != SA_AIS_OK) {
      TRACE("immutil_saImmOmInitialize fail rc=%d", rc);
    }
    if (rc == SA_AIS_OK) {
      rc = immutil_saImmOmClassDescriptionGet_2(
          om_handle, const_cast<SaImmClassNameT>("SaLogStreamConfig"), &cc,
          &attributes);
    }

    if (rc == SA_AIS_OK) {
      int i = 0;
      while ((attribute = attributes[i++]) != NULL) {
        if (!strcmp(attribute->attrName, "saLogStreamMaxLogFileSize")) {
          TRACE("Got saLogStreamMaxLogFileSize");
          lgs_stream_defval.saLogStreamMaxLogFileSize =
              *((SaUint64T *)attribute->attrDefaultValue);
          TRACE("value = %lld", lgs_stream_defval.saLogStreamMaxLogFileSize);
        } else if (!strcmp(attribute->attrName,
                           "saLogStreamFixedLogRecordSize")) {
          TRACE("Got saLogStreamFixedLogRecordSize");
          lgs_stream_defval.saLogStreamFixedLogRecordSize =
              *((SaUint32T *)attribute->attrDefaultValue);
          TRACE("value = %d", lgs_stream_defval.saLogStreamFixedLogRecordSize);
        }
      }

      rc = immutil_saImmOmClassDescriptionMemoryFree_2(om_handle, attributes);
      if (rc != SA_AIS_OK) {
        LOG_ER("%s: Failed to free class description memory rc=%d",
               __FUNCTION__, rc);
        osafassert(0);
      }
      lgs_stream_defval_updated_flag = true;
    } else {
      /* Default values are not fetched. Temporary use hard coded values.
       */
      TRACE("saImmOmClassDescriptionGet_2 failed rc=%d", rc);
      lgs_stream_defval.saLogStreamMaxLogFileSize = 5000000;
      lgs_stream_defval.saLogStreamFixedLogRecordSize = 150;
    }

    rc = immutil_saImmOmFinalize(om_handle);
    if (rc != SA_AIS_OK) {
      TRACE("immutil_saImmOmFinalize fail rc=%d", rc);
    }
  } else {
    TRACE("Defaults are already fetched");
    TRACE("saLogStreamMaxLogFileSize=%lld",
          lgs_stream_defval.saLogStreamMaxLogFileSize);
    TRACE("saLogStreamFixedLogRecordSize=%d",
          lgs_stream_defval.saLogStreamFixedLogRecordSize);
  }

  TRACE_LEAVE();
  return &lgs_stream_defval;
}

/**
 * Check if a stream with the same file name and relative path already
 * exist
 *
 * @param immOiHandle
 * @param fileName
 * @param pathName
 * @param stream
 * @param operationType
 * @return true if exists
 */
bool chk_filepath_stream_exist(std::string &fileName, std::string &pathName,
                               log_stream_t *stream,
                               enum CcbUtilOperationType operationType) {
  log_stream_t *i_stream = NULL;
  std::string i_fileName;
  std::string i_pathName;
  bool rc = false;

  TRACE_ENTER();
  TRACE("fileName \"%s\", pathName \"%s\"", fileName.c_str(), pathName.c_str());

  /* If a stream is modified only the name may be modified. The path name
   * must be fetched from the stream.
   */
  if (operationType == CCBUTIL_MODIFY) {
    TRACE("MODIFY");
    if (stream == NULL) {
      /* No stream to modify. Should never happen */
      osafassert(0);
    }
    if ((fileName.empty() == true) && (pathName.empty() == true)) {
      /* Nothing has changed */
      TRACE("Nothing has changed");
      return false;
    }
    if (fileName.empty() == true) {
      i_fileName = stream->fileName;
      TRACE("From stream: fileName \"%s\"", i_fileName.c_str());
    } else {
      i_fileName = fileName;
    }
    if (pathName.empty() == true) {
      i_pathName = stream->pathName;
      TRACE("From stream: pathName \"%s\"", i_pathName.c_str());
    } else {
      i_pathName = pathName;
    }
  } else if (operationType == CCBUTIL_CREATE) {
    TRACE("CREATE");
    if ((fileName.empty() == true) || (pathName.empty() == true)) {
      /* Should never happen
       * A valid fileName and pathName is always given at create */
      LOG_ER("fileName or pathName is not a string");
      osafassert(0);
    }

    i_fileName = fileName;
    i_pathName = pathName;
  } else {
    /* Unknown operationType. Should never happen */
    osafassert(0);
  }

  /* Check if any stream has given filename and path */
  TRACE("Check if any stream has given filename and path");
  // Iterate all existing log streams in cluster.
  SaBoolT endloop = SA_FALSE, jstart = SA_TRUE;
  while ((i_stream = iterate_all_streams(endloop, jstart)) && !endloop) {
    jstart = SA_FALSE;
    TRACE("Check stream \"%s\"", i_stream->name.c_str());
    if ((i_stream->fileName == i_fileName) &&
        (i_stream->pathName == i_pathName)) {
      rc = true;
      break;
    }
  }

  TRACE_LEAVE2("rc = %d", rc);
  return rc;
}

/**
 * Verify fixedLogRecordSize and maxLogFileSize.
 * Rules:
 * 1 - saLogStreamFixedLogRecordSize must be less than saLogStreamMaxLogFileSize
 * 2 - saLogStreamFixedLogRecordSize must be less than or equal to
 * logMaxLogrecsize 3 - saLogStreamFixedLogRecordSize can be 0. Means variable
 * record size 4 - saLogStreamMaxLogFileSize must be bigger than 0. No limit is
 * not supported 5 - saLogStreamMaxLogFileSize must be bigger than
 * logMaxLogrecsize 6 - saLogStreamMaxLogFileSize must be bigger than
 * saLogStreamFixedLogRecordSize
 *
 * The ..._mod variable == true means that the corresponding attribute is
 * modified.
 *
 * @param immOiHandle[in]
 * @param streamMaxLogFileSize[in]
 * @param streamMaxLogFileSize_mod[in]
 * @param streamFixedLogRecordSize[in]
 * @param streamFixedLogRecordSize_mod[in]
 * @param stream[in]
 * @param operationType[in]
 * @return false if error
 */
static bool chk_max_filesize_recordsize_compatible(
    SaImmOiHandleT immOiHandle, SaUint64T streamMaxLogFileSize,
    bool streamMaxLogFileSize_mod, SaUint32T streamFixedLogRecordSize,
    bool streamFixedLogRecordSize_mod, log_stream_t *stream,
    enum CcbUtilOperationType operationType, SaImmOiCcbIdT ccbId) {
  SaUint64T i_streamMaxLogFileSize = 0;
  SaUint32T i_streamFixedLogRecordSize = 0;
  SaUint32T i_logMaxLogrecsize = 0;
  lgs_stream_defval_t *stream_default;

  bool rc = true;

  TRACE_ENTER();

  /** Get all parameters **/

  /* Get logMaxLogrecsize from configuration parameters */
  i_logMaxLogrecsize =
      *static_cast<const SaUint32T *>(lgs_cfg_get(LGS_IMM_LOG_MAX_LOGRECSIZE));
  TRACE("i_logMaxLogrecsize = %d", i_logMaxLogrecsize);
  /* Get stream default settings */
  stream_default = get_SaLogStreamConfig_default();

  if (operationType == CCBUTIL_MODIFY) {
    TRACE("operationType == CCBUTIL_MODIFY");
    /* The stream exists.
     */
    if (stream == NULL) {
      /* Should never happen */
      LOG_ER("%s stream == NULL", __FUNCTION__);
      osafassert(0);
    }
    if (streamFixedLogRecordSize_mod == false) {
      /* streamFixedLogRecordSize is not given. Get from stream */
      i_streamFixedLogRecordSize = stream->fixedLogRecordSize;
      TRACE("Get from stream, streamFixedLogRecordSize = %d",
            i_streamFixedLogRecordSize);
    } else {
      i_streamFixedLogRecordSize = streamFixedLogRecordSize;
    }

    if (streamMaxLogFileSize_mod == false) {
      /* streamMaxLogFileSize is not given. Get from stream */
      i_streamMaxLogFileSize = stream->maxLogFileSize;
      TRACE("Get from stream, streamMaxLogFileSize = %lld",
            i_streamMaxLogFileSize);
    } else {
      i_streamMaxLogFileSize = streamMaxLogFileSize;
      TRACE("Modified streamMaxLogFileSize = %lld", i_streamMaxLogFileSize);
    }
  } else if (operationType == CCBUTIL_CREATE) {
    TRACE("operationType == CCBUTIL_CREATE");
    /* The stream does not yet exist
     */
    if (streamFixedLogRecordSize_mod == false) {
      /* streamFixedLogRecordSize is not given. Use default */
      i_streamFixedLogRecordSize =
          stream_default->saLogStreamFixedLogRecordSize;
      TRACE("Get default, streamFixedLogRecordSize = %d",
            i_streamFixedLogRecordSize);
    } else {
      i_streamFixedLogRecordSize = streamFixedLogRecordSize;
    }

    if (streamMaxLogFileSize_mod == false) {
      /* streamMaxLogFileSize is not given. Use default */
      i_streamMaxLogFileSize = stream_default->saLogStreamMaxLogFileSize;
      TRACE("Get default, streamMaxLogFileSize = %lld", i_streamMaxLogFileSize);
    } else {
      i_streamMaxLogFileSize = streamMaxLogFileSize;
    }
  } else {
    /* Unknown operationType */
    LOG_ER("%s Unknown operationType", __FUNCTION__);
    osafassert(0);
  }

  /*
   * Do verification
   */
  if (streamMaxLogFileSize_mod == true) {
    if (i_streamMaxLogFileSize <= i_logMaxLogrecsize) {
      /* streamMaxLogFileSize must be bigger than logMaxLogrecsize */
      report_oi_error(immOiHandle, ccbId, "streamMaxLogFileSize out of range");
      TRACE("i_streamMaxLogFileSize (%lld) <= i_logMaxLogrecsize (%d)",
            i_streamMaxLogFileSize, i_logMaxLogrecsize);
      rc = false;
      goto done;
    } else if (i_streamMaxLogFileSize <= i_streamFixedLogRecordSize) {
      /* streamMaxLogFileSize must be bigger than streamFixedLogRecordSize */
      report_oi_error(immOiHandle, ccbId, "streamMaxLogFileSize out of range");
      TRACE("i_streamMaxLogFileSize (%lld) <= i_streamFixedLogRecordSize (%d)",
            i_streamMaxLogFileSize, i_streamFixedLogRecordSize);
      rc = false;
      goto done;
    }
  }

  if (streamFixedLogRecordSize_mod == true) {
    if (i_streamFixedLogRecordSize == 0) {
      /* streamFixedLogRecordSize can be 0. Means variable record size */
      TRACE("streamFixedLogRecordSize = 0");
      rc = true;
    } else if (i_streamFixedLogRecordSize < SA_LOG_MIN_RECORD_SIZE) {
      /* streamFixedLogRecordSize must be larger than 150 */
      report_oi_error(immOiHandle, ccbId,
                      "streamFixedLogRecordSize out of range");
      TRACE("i_streamFixedLogRecordSize (%d) < %d", i_streamFixedLogRecordSize,
            SA_LOG_MIN_RECORD_SIZE);
      rc = false;
    } else if (i_streamFixedLogRecordSize >= i_streamMaxLogFileSize) {
      /* streamFixedLogRecordSize must be less than streamMaxLogFileSize */
      report_oi_error(immOiHandle, ccbId,
                      "streamFixedLogRecordSize out of range");
      TRACE("i_streamFixedLogRecordSize (%d) >= i_streamMaxLogFileSize (%lld)",
            i_streamFixedLogRecordSize, i_streamMaxLogFileSize);
      rc = false;
    } else if (i_streamFixedLogRecordSize > i_logMaxLogrecsize) {
      /* streamFixedLogRecordSize must be less than streamMaxLogFileSize */
      report_oi_error(immOiHandle, ccbId,
                      "streamFixedLogRecordSize out of range");
      TRACE("i_streamFixedLogRecordSize (%d) > i_logMaxLogrecsize (%d)",
            i_streamFixedLogRecordSize, i_logMaxLogrecsize);
      rc = false;
    }
  }

done:
  TRACE_LEAVE2("rc = %d", rc);
  return rc;
}

static bool is_valid_dest_names(const std::vector<std::string> &names) {
  // Stream has no destination name
  if (names.size() == 0) return true;
  for (const auto &name : names) {
    // Empty destination name is invalid
    if (name.empty() == true || name.size() == 0) {
      TRACE("%s name is empty", __func__);
      return false;
    }

    // Contain special characters is not allowed
    if (logutil::isValidName(name) == false) {
      TRACE("%s name has special chars in", __func__);
      return false;
    }

    // Name is too long
    if (name.length() > kNameMaxLength) {
      TRACE("%s too long name", __func__);
      return false;
    }
  }
  return true;
}

/**
 * Validate input parameters creation and modify of a persistent stream
 * i.e. a stream that has a configuration object.
 *
 * @param ccbUtilOperationData
 *
 * @return SaAisErrorT
 */
static SaAisErrorT check_attr_validity(
    SaImmOiHandleT immOiHandle, const struct CcbUtilOperationData *opdata) {
  SaAisErrorT rc = SA_AIS_OK;
  void *value = NULL;
  int aindex = 0;
  const SaImmAttrValuesT_2 *attribute = NULL;
  log_stream_t *stream = NULL;

  /* Attribute values to be checked
   */
  /* Mandatory if create. Can be modified */
  std::string i_fileName;
  bool i_fileName_mod = false;
  /* Mandatory if create. Cannot be changed (handled in class definition) */
  std::string i_pathName;
  bool i_pathName_mod = false;
  /* Modification flag -> true if modified */
  SaUint32T i_streamFixedLogRecordSize = 0;
  bool i_streamFixedLogRecordSize_mod = false;
  SaUint64T i_streamMaxLogFileSize = 0;
  bool i_streamMaxLogFileSize_mod = false;
  SaUint32T i_logFullAction = 0;
  bool i_logFullAction_mod = false;
  char *i_logFileFormat = NULL;
  bool i_logFileFormat_mod = false;
  SaUint32T i_logFullHaltThreshold = 0;
  bool i_logFullHaltThreshold_mod = false;
  SaUint32T i_maxFilesRotated = 0;
  bool i_maxFilesRotated_mod = false;
  SaUint32T i_severityFilter = 0;
  bool i_severityFilter_mod = false;

  TRACE_ENTER();

  /* Get first attribute if any and fill in name and path if the stream
   * exist (modify)
   */
  if (opdata->operationType == CCBUTIL_MODIFY) {
    TRACE("Validate for MODIFY");
    SaConstStringT objName =
        osaf_extended_name_borrow(opdata->param.modify.objectName);
    stream = log_stream_get_by_name(objName);
    if (stream == NULL) {
      /* No stream to modify */
      report_oi_error(immOiHandle, opdata->ccbId, "Stream does not exist");
      TRACE("Stream does not exist");
      rc = SA_AIS_ERR_BAD_OPERATION;
      goto done;
    } else {
      /* Get first attribute */
      if (opdata->param.modify.attrMods[aindex] != NULL) {
        attribute = &opdata->param.modify.attrMods[aindex]->modAttr;
        aindex++;
        TRACE("First attribute \"%s\" fetched", attribute->attrName);
      } else {
        TRACE("No attributes found");
      }
      /* Get relative path and file name from stream.
       * Name may be modified
       */
      i_pathName = stream->pathName;
      i_fileName = stream->fileName;
      TRACE("From stream: pathName \"%s\", fileName \"%s\"", i_pathName.c_str(),
            i_fileName.c_str());
    }
  } else if (opdata->operationType == CCBUTIL_CREATE) {
    TRACE("Validate for CREATE");
    /* Check if stream already exist after parameters are saved.
     * Parameter value for fileName is needed.
     */
    /* Get first attribute */
    attribute = opdata->param.create.attrValues[aindex];
    aindex++;
    TRACE("First attribute \"%s\" fetched", attribute->attrName);
  } else {
    /* Invalid operation type */
    LOG_ER("%s Invalid operation type", __FUNCTION__);
    osafassert(0);
  }

  while ((attribute != NULL) && (rc == SA_AIS_OK)) {
    /* Save all changed/given attribute values
     */

    /* Get the value */
    if (attribute->attrValuesNumber > 0) {
      value = attribute->attrValues[0];
    } else if (opdata->operationType == CCBUTIL_MODIFY) {
      if (!strcmp(attribute->attrName, "saLogRecordDestination")) {
        // do nothing
      } else {
        /* An attribute without a value is never valid if modify */
        report_oi_error(immOiHandle, opdata->ccbId, "Attribute %s has no value",
                        attribute->attrName);
        TRACE("Modify: Attribute %s has no value", attribute->attrName);
        rc = SA_AIS_ERR_BAD_OPERATION;
        goto done;
      }
    } else {
      /* If create all attributes will be present also the ones without
       * any value.
       */
      TRACE("Create: Attribute %s has no value", attribute->attrName);
      goto next;
    }

    /* Save attributes with a value */
    if (!strcmp(attribute->attrName, "saLogStreamFileName")) {
      /* Save filename. Check later together with path name */
      i_fileName = *((char **)value);
      i_fileName_mod = true;
      TRACE("Saved attribute \"%s\"", attribute->attrName);
    } else if (!strcmp(attribute->attrName, "saLogStreamPathName")) {
      /* Save path name. Check later together with filename */
      i_pathName = *((char **)value);
      i_pathName_mod = true;
      TRACE("Saved attribute \"%s\"", attribute->attrName);
    } else if (!strcmp(attribute->attrName, "saLogStreamMaxLogFileSize")) {
      /* Save and compare with FixedLogRecordSize after all attributes
       * are read. Must be bigger than FixedLogRecordSize
       */
      i_streamMaxLogFileSize = *((SaUint64T *)value);
      i_streamMaxLogFileSize_mod = true;
      TRACE("Saved attribute \"%s\"", attribute->attrName);
    } else if (!strcmp(attribute->attrName, "saLogStreamFixedLogRecordSize")) {
      /* Save and compare with MaxLogFileSize after all attributes
       * are read. Must be smaller than MaxLogFileSize
       */
      i_streamFixedLogRecordSize = *((SaUint64T *)value);
      i_streamFixedLogRecordSize_mod = true;
      TRACE("Saved attribute \"%s\"", attribute->attrName);
    } else if (!strcmp(attribute->attrName, "saLogStreamLogFullAction")) {
      i_logFullAction = *((SaUint32T *)value);
      i_logFullAction_mod = true;
      TRACE("Saved attribute \"%s\"", attribute->attrName);
    } else if (!strcmp(attribute->attrName, "saLogStreamLogFileFormat")) {
      i_logFileFormat = *((char **)value);
      i_logFileFormat_mod = true;
      TRACE("Saved attribute \"%s\"", attribute->attrName);
    } else if (!strcmp(attribute->attrName,
                       "saLogStreamLogFullHaltThreshold")) {
      i_logFullHaltThreshold = *((SaUint32T *)value);
      i_logFullHaltThreshold_mod = true;
      TRACE("Saved attribute \"%s\"", attribute->attrName);
    } else if (!strcmp(attribute->attrName, "saLogStreamMaxFilesRotated")) {
      i_maxFilesRotated = *((SaUint32T *)value);
      i_maxFilesRotated_mod = true;
      TRACE("Saved attribute \"%s\"", attribute->attrName);
    } else if (!strcmp(attribute->attrName, "saLogStreamSeverityFilter")) {
      i_severityFilter = *((SaUint32T *)value);
      i_severityFilter_mod = true;
      TRACE("Saved attribute \"%s\" = %d", attribute->attrName,
            i_severityFilter);
    } else if (!strcmp(attribute->attrName, "saLogRecordDestination")) {
      std::vector<std::string> vstring{};
      for (unsigned i = 0; i < attribute->attrValuesNumber; i++) {
        value = attribute->attrValues[i];
        char *value_str = *(reinterpret_cast<char **>(value));
        vstring.push_back(value_str);
      }
      if (is_valid_dest_names(vstring) == false) {
        /* Report failed if has special character in file name */
        rc = SA_AIS_ERR_BAD_OPERATION;
        report_oi_error(immOiHandle, opdata->ccbId,
                        "Invalid saLogRecordDestination value");
        TRACE("Invalid saLogRecordDestination value");
        goto done;
      }
    }

  /* Get next attribute or detect no more attributes */
  next:
    if (opdata->operationType == CCBUTIL_CREATE) {
      attribute = opdata->param.create.attrValues[aindex];
    } else {
      /* CCBUTIL_MODIFY */
      if (opdata->param.modify.attrMods[aindex] != NULL) {
        attribute = &opdata->param.modify.attrMods[aindex]->modAttr;
      } else {
        attribute = NULL;
      }
    }
    aindex++;
  }

  /* Check all attributes:
   * Attributes must be within limits
   * Note: Mandatory attributes are flagged SA_INITIALIZED meaning that
   * IMM will reject create if these attributes are not defined. Therefore
   * this is not checked here.
   */
  if (rc == SA_AIS_OK) {
    /* saLogStreamPathName
     * Must be valid and not outside of root path
     */
    if (i_pathName_mod) {
      TRACE("Checking saLogStreamPathName");
      if (i_pathName.empty() == true) {
        /* Must point to a string */
        rc = SA_AIS_ERR_BAD_OPERATION;
        report_oi_error(immOiHandle, opdata->ccbId,
                        "NULL pointer to saLogStreamPathName");
        TRACE("NULL pointer to saLogStreamPathName");
        goto done;
      }

      if (lgs_is_valid_pathlength(i_pathName, i_fileName) == false) {
        /* Path name is too long */
        rc = SA_AIS_ERR_BAD_OPERATION;
        report_oi_error(immOiHandle, opdata->ccbId,
                        "saLogStreamPathName is so long");
        TRACE("saLogStreamPathName is so long (max: %d)", PATH_MAX);
        goto done;
      }

      if (lgs_relative_path_check_ts(i_pathName) == true) {
        report_oi_error(immOiHandle, opdata->ccbId, "Path %s not valid",
                        i_pathName.c_str());
        rc = SA_AIS_ERR_BAD_OPERATION;
        TRACE("Path %s not valid", i_pathName.c_str());
        goto done;
      }
    }

    /* saLogStreamFileName
     * Must be a name. A stream with this name and relative path must not
     * already exist.
     */
    if (i_fileName_mod) {
      TRACE("Checking saLogStreamFileName");
      if (i_fileName.empty() == true) {
        /* Must point to a string */
        rc = SA_AIS_ERR_BAD_OPERATION;
        report_oi_error(immOiHandle, opdata->ccbId,
                        "NULL pointer to saLogStreamFileName");
        TRACE("NULL pointer to saLogStreamFileName");
        goto done;
      }

      if ((lgs_is_valid_pathlength(i_pathName, i_fileName) == false) ||
          (lgs_is_valid_filelength(i_fileName) == false)) {
        /* File name is too long */
        rc = SA_AIS_ERR_BAD_OPERATION;
        report_oi_error(immOiHandle, opdata->ccbId,
                        "saLogStreamFileName is so long");
        TRACE("saLogStreamFileName is so long");
        goto done;
      }

      /* Not allow special characters exising in file name */
      if (lgs_has_special_char(i_fileName) == true) {
        /* Report failed if has special character in file name */
        rc = SA_AIS_ERR_BAD_OPERATION;
        report_oi_error(immOiHandle, opdata->ccbId,
                        "Invalid saLogStreamFileName value");
        TRACE("\t448 Invalid saLogStreamFileName value");
        goto done;
      }

      if (chk_filepath_stream_exist(i_fileName, i_pathName, stream,
                                    opdata->operationType)) {
        report_oi_error(immOiHandle, opdata->ccbId,
                        "Path/file %s/%s already exist", i_pathName.c_str(),
                        i_fileName.c_str());
        rc = SA_AIS_ERR_BAD_OPERATION;
        TRACE("Path/file %s/%s already exist", i_pathName.c_str(),
              i_fileName.c_str());
        goto done;
      }
    }

    /* saLogStreamMaxLogFileSize or saLogStreamFixedLogRecordSize
     * See chk_max_filesize_recordsize_compatible() for rules
     */
    if (i_streamMaxLogFileSize_mod || i_streamFixedLogRecordSize_mod) {
      TRACE(
          "Check saLogStreamMaxLogFileSize,"
          " saLogStreamFixedLogRecordSize");
      if (chk_max_filesize_recordsize_compatible(
              immOiHandle, i_streamMaxLogFileSize, i_streamMaxLogFileSize_mod,
              i_streamFixedLogRecordSize, i_streamFixedLogRecordSize_mod,
              stream, opdata->operationType, opdata->ccbId) == false) {
        /* report_oi_error is done within check function */
        rc = SA_AIS_ERR_BAD_OPERATION;
        TRACE("chk_max_filesize_recordsize_compatible Fail");
        goto done;
      }
    }

    /* saLogStreamLogFullAction
     * 1, 2 and 3 are valid according to AIS but only action rotate (3)
     * is supported.
     */
    if (i_logFullAction_mod) {
      TRACE("Check saLogStreamLogFullAction");
      /* If this attribute is changed an oi error report is written
       * and the value is changed (backwards compatible) but this will
       * not affect change action
       */
      if ((i_logFullAction < SA_LOG_FILE_FULL_ACTION_WRAP) ||
          (i_logFullAction > SA_LOG_FILE_FULL_ACTION_ROTATE)) {
        report_oi_error(immOiHandle, opdata->ccbId,
                        "logFullAction out of range");
        rc = SA_AIS_ERR_BAD_OPERATION;
        TRACE("logFullAction out of range");
        goto done;
      }
      if ((i_logFullAction == SA_LOG_FILE_FULL_ACTION_WRAP) ||
          (i_logFullAction == SA_LOG_FILE_FULL_ACTION_HALT)) {
        report_oi_error(
            immOiHandle, opdata->ccbId,
            "logFullAction:Current Implementation doesn't support Wrap and halt");
        rc = SA_AIS_ERR_BAD_OPERATION;
        TRACE("logFullAction: Not supported");
        goto done;
      }
    }

    /* saLogStreamLogFileFormat
     * If create the only possible stream type is application and no stream
     * is yet created. All streams can be modified.
     */
    if (i_logFileFormat_mod) {
      SaBoolT dummy;
      if (opdata->operationType == CCBUTIL_CREATE) {
        if (!lgs_is_valid_format_expression(i_logFileFormat,
                                            STREAM_TYPE_APPLICATION, &dummy)) {
          report_oi_error(immOiHandle, opdata->ccbId,
                          "Invalid logFileFormat: %s", i_logFileFormat);
          rc = SA_AIS_ERR_BAD_OPERATION;
          TRACE("Create: Invalid logFileFormat: %s", i_logFileFormat);
          goto done;
        }
      } else {
        if (!lgs_is_valid_format_expression(i_logFileFormat, stream->streamType,
                                            &dummy)) {
          report_oi_error(immOiHandle, opdata->ccbId,
                          "Invalid logFileFormat: %s", i_logFileFormat);
          rc = SA_AIS_ERR_BAD_OPERATION;
          TRACE("Modify: Invalid logFileFormat: %s", i_logFileFormat);
          goto done;
        }
      }
    }

    /* saLogStreamLogFullHaltThreshold
     * Not supported and not used. For backwards compatibility the
     * value can still be set/changed between 0 and 99 (percent)
     */
    if (i_logFullHaltThreshold_mod) {
      TRACE("Checking saLogStreamLogFullHaltThreshold");
      if (i_logFullHaltThreshold >= 100) {
        report_oi_error(immOiHandle, opdata->ccbId,
                        "logFullHaltThreshold out of range");
        rc = SA_AIS_ERR_BAD_OPERATION;
        goto done;
      }
    }

    /* saLogStreamMaxFilesRotated
     * < 127
     */
    if (i_maxFilesRotated_mod) {
      TRACE("Checking saLogStreamMaxFilesRotated");
      if ((i_maxFilesRotated < 1) || (i_maxFilesRotated > 127)) {
        report_oi_error(immOiHandle, opdata->ccbId,
                        "maxFilesRotated out of range (min 1, max 127): %u",
                        i_maxFilesRotated);
        rc = SA_AIS_ERR_BAD_OPERATION;
        TRACE(
            "maxFilesRotated out of range "
            "(min 1, max 127): %u",
            i_maxFilesRotated);
        goto done;
      }
    }

    /* saLogStreamSeverityFilter
     *     <= 0x7f
     */
    if (i_severityFilter_mod) {
      TRACE("Checking saLogStreamSeverityFilter");
      if (i_severityFilter > 0x7f) {
        report_oi_error(immOiHandle, opdata->ccbId, "Invalid severity: %x",
                        i_severityFilter);
        rc = SA_AIS_ERR_BAD_OPERATION;
        TRACE("Invalid severity: %x", i_severityFilter);
        goto done;
      }
    }
  }

done:
  TRACE_LEAVE2("rc = %d", rc);
  return rc;
}

/**
 * Create a log stream configuration object
 * @param immOiHandle
 * @param opdata
 * @return
 */
static SaAisErrorT stream_ccb_completed_create(
    SaImmOiHandleT immOiHandle, const CcbUtilOperationData_t *opdata) {
  SaAisErrorT rc = SA_AIS_ERR_BAD_OPERATION;

  TRACE_ENTER2("CCB ID %llu", opdata->ccbId);
  if (!check_max_stream()) {
    rc = check_attr_validity(immOiHandle, opdata);
  } else {
    report_oi_error(immOiHandle, opdata->ccbId,
                    "Number of stream out of limitation");
  }
  TRACE_LEAVE2("%u", rc);
  return rc;
}

/**
 * Modify attributes in log stream configuration object
 * @param opdata
 * @return
 */
static SaAisErrorT stream_ccb_completed_modify(
    SaImmOiHandleT immOiHandle, const CcbUtilOperationData_t *opdata) {
  SaAisErrorT rc;

  TRACE_ENTER2("CCB ID %llu, '%s'", opdata->ccbId,
               osaf_extended_name_borrow(&opdata->objectName));
  rc = check_attr_validity(immOiHandle, opdata);
  TRACE_LEAVE2("rc = %u", rc);
  return rc;
}

/**
 * Delete log stream configuration object
 * @param immOiHandle
 * @param opdata
 * @return
 */
static SaAisErrorT stream_ccb_completed_delete(
    SaImmOiHandleT immOiHandle, const CcbUtilOperationData_t *opdata) {
  SaAisErrorT rc = SA_AIS_ERR_BAD_OPERATION;

  TRACE_ENTER2("CCB ID %llu, '%s'", opdata->ccbId,
               osaf_extended_name_borrow(&opdata->objectName));

  SaConstStringT name =
      osaf_extended_name_borrow(opdata->param.delete_.objectName);
  log_stream_t *stream = log_stream_get_by_name(name);

  if (stream != NULL) {
    /**
     * Use stream name (DN) to regcogize if the stream is well-known or not
     * instead of using streamID. This is to avoid any mishandling on streamId.
     */
    if (is_well_know_stream(name)) {
      report_oi_error(immOiHandle, opdata->ccbId,
                      "Stream delete: well known stream '%s' cannot be deleted",
                      name);
      goto done;
    }

    if (stream->numOpeners > 1) {
      report_oi_error(immOiHandle, opdata->ccbId,
                      "Stream '%s' cannot be deleted: opened by %u clients",
                      name, stream->numOpeners);
      goto done;
    }

    rc = SA_AIS_OK;
  } else {
    report_oi_error(immOiHandle, opdata->ccbId, "stream %s not found", name);
  }

done:
  TRACE_LEAVE2("%u", rc);
  return rc;
}

static SaAisErrorT stream_ccb_completed(SaImmOiHandleT immOiHandle,
                                        const CcbUtilOperationData_t *opdata) {
  SaAisErrorT rc = SA_AIS_ERR_BAD_OPERATION;

  TRACE_ENTER2("CCB ID %llu", opdata->ccbId);

  switch (opdata->operationType) {
    case CCBUTIL_CREATE:
      rc = stream_ccb_completed_create(immOiHandle, opdata);
      break;
    case CCBUTIL_MODIFY:
      rc = stream_ccb_completed_modify(immOiHandle, opdata);
      break;
    case CCBUTIL_DELETE:
      rc = stream_ccb_completed_delete(immOiHandle, opdata);
      break;
    default:
      assert(0);
      break;
  }

  TRACE_LEAVE2("rc = %u", rc);
  return rc;
}

/**
 * The CCB is now complete. Verify that the changes can be applied
 *
 * @param immOiHandle
 * @param ccbId
 * @return
 */
static SaAisErrorT ccbCompletedCallback(SaImmOiHandleT immOiHandle,
                                        SaImmOiCcbIdT ccbId) {
  SaAisErrorT rc = SA_AIS_OK;
  SaAisErrorT cb_rc = SA_AIS_OK;
  struct CcbUtilCcbData *ccbUtilCcbData;
  struct CcbUtilOperationData *opdata;

  TRACE_ENTER2("CCB ID %llu", ccbId);

  if (lgs_cb->ha_state != SA_AMF_HA_ACTIVE) {
    TRACE("State Not Active. Nothing to do, we are an applier");
    goto done;
  }

  if ((ccbUtilCcbData = ccbutil_findCcbData(ccbId)) == NULL) {
    LOG_ER("%s: Failed to find CCB object for %llu", __FUNCTION__, ccbId);
    cb_rc = SA_AIS_ERR_BAD_OPERATION;
    goto done;  // or exit?
  }

  /*
  ** "check that the sequence of change requests contained in the CCB is
  ** valid and that no errors will be generated when these changes
  ** are applied."
  */
  for (opdata = ccbUtilCcbData->operationListHead; opdata;
       opdata = opdata->next) {
    switch (opdata->operationType) {
      case CCBUTIL_CREATE:
        if (!strcmp(opdata->param.create.className, "OpenSafLogConfig"))
          rc = config_ccb_completed(immOiHandle, opdata);
        else if (!strcmp(opdata->param.create.className, "SaLogStreamConfig"))
          rc = stream_ccb_completed(immOiHandle, opdata);
        else
          osafassert(0);
        break;
      case CCBUTIL_DELETE:
      case CCBUTIL_MODIFY:
        if (!strncmp(osaf_extended_name_borrow(&opdata->objectName),
                     "safLgStrCfg", 11)) {
          rc = stream_ccb_completed(immOiHandle, opdata);
        } else {
          rc = config_ccb_completed(immOiHandle, opdata);
        }
        break;
      default:
        assert(0);
        break;
    }
  }

done:
  /*
   * For now always return rc (original code)
   * TBD: Only use relevant return codes for Completed Callback
   */
  cb_rc = rc;
  TRACE_LEAVE2("rc = %u", cb_rc);
  return cb_rc;
}
/**
 * Set logRootDirectory to new value
 *   - Close all open logfiles
 *   - Rename all log files and .cfg files.
 *   - Open all logfiles and .cfg files in new directory.
 *
 * @param new_logRootDirectory[in]
 *             String containg path for new root directory
 * @param old_logRootDirectory[in]
 *            String containg path for old root directory
 * @param close_time[in]
 *            Time for file close time stamp
 */
/**
 *
 * @param new_logRootDirectory
 * @param old_logRootDirectory
 * @param cur_time_in
 */
void logRootDirectory_filemove(const std::string &new_logRootDirectory,
                               const std::string &old_logRootDirectory,
                               time_t *cur_time_in) {
  TRACE_ENTER();
  log_stream_t *stream;
  std::string current_logfile;
  SaBoolT endloop = SA_FALSE, jstart = SA_TRUE;
  /* Close and rename files at current path
   */
  // Iterate all existing log streams in cluster.
  while ((stream = iterate_all_streams(endloop, jstart)) && !endloop) {
    jstart = SA_FALSE;

    TRACE("Handling file %s", stream->logFileCurrent.c_str());
    if (lgs_cb->ha_state == SA_AMF_HA_ACTIVE) {
      current_logfile = stream->logFileCurrent;
    } else {
      current_logfile = stream->stb_logFileCurrent;
    }

    TRACE("current_logfile \"%s\"", current_logfile.c_str());

    if (log_stream_config_change(!LGS_STREAM_CREATE_FILES, old_logRootDirectory,
                                 stream, current_logfile, cur_time_in) != 0) {
      LOG_WA("Old log files could not be renamed and closed for stream: %s",
             stream->name.c_str());
    }
  }

  // Change logrootDirectory to new_logRootDirectory
  lgs_rootpathconf_set(new_logRootDirectory);

  /* Create new files at new path
   */
  endloop = SA_FALSE;
  jstart = SA_TRUE;
  while ((stream = iterate_all_streams(endloop, jstart)) && !endloop) {
    jstart = SA_FALSE;
    if (lgs_create_config_file_h(new_logRootDirectory, stream) != 0) {
      LOG_WA("New config file could not be created for stream: %s",
             stream->name.c_str());
    }

    /* Create the new log file based on updated configuration */
    char *current_time = lgs_get_time(cur_time_in);
    stream->logFileCurrent = stream->fileName + "_" + current_time;

    if ((*stream->p_fd = log_file_open(new_logRootDirectory, stream,
                                       stream->logFileCurrent, NULL)) == -1) {
      LOG_WA("New log file could not be created for stream: %s",
             stream->name.c_str());
    }

    /* Also update standby current file name
     * Used if standby and configured for split file system
     */
    stream->stb_logFileCurrent = stream->logFileCurrent;
  }

  TRACE_LEAVE();
}

/**
 * Apply new group name
 *   - Update lgs_conf with new group (logDataGroupname).
 *   - Reown all log files by this new group.
 *
 * @param new_logDataGroupname[in]
 *            String contains new group.
 */
void logDataGroupname_fileown(const char *new_logDataGroupname) {
  TRACE_ENTER();

  if (new_logDataGroupname == NULL) {
    LOG_ER("Data group is NULL");
    return;
  }

  /* For each log stream, reown all log files */
  if (strcmp(new_logDataGroupname, "")) {
    /* Not attribute values deletion
     * Change ownership of log files to this new group
     */
    // Iterate all existing log streams in cluster.
    SaBoolT endloop = SA_FALSE, jstart = SA_TRUE;
    log_stream_t *stream;
    while ((stream = iterate_all_streams(endloop, jstart)) && !endloop) {
      jstart = SA_FALSE;
      lgs_own_log_files_h(stream, new_logDataGroupname);
    }
  }

  TRACE_LEAVE();
}

/**
 * Change log root directory
 *
 * @param old_logRootDirectory[in]
 * @param new_logRootDirectory[in]
 */
static void apply_conf_logRootDirectory(const char *old_logRootDirectory,
                                        const char *new_logRootDirectory) {
  struct timespec curtime_tspec;
  osaf_clock_gettime(CLOCK_REALTIME, &curtime_tspec);
  time_t cur_time = curtime_tspec.tv_sec;

  logRootDirectory_filemove(new_logRootDirectory, old_logRootDirectory,
                            &cur_time);
}

static void apply_conf_logDataGroupname(const char *logDataGroupname) {
  const char *value_ptr = NULL;
  const char noGroupname[] = "";

  if (logDataGroupname == NULL)
    value_ptr = noGroupname;
  else
    value_ptr = logDataGroupname;

  logDataGroupname_fileown(value_ptr);
}

static void apply_config_destinations_change(
    const std::vector<std::string> &vdestcfg, SaImmAttrModificationTypeT type,
    lgs_config_chg_t *config_data) {
  switch (type) {
    case SA_IMM_ATTR_VALUES_ADD:
      // Configure destinations
      CfgDestination(vdestcfg, ModifyType::kAdd);
      lgs_cfgupd_multival_add(LOG_RECORD_DESTINATION_CONFIGURATION, vdestcfg,
                              config_data);
      break;

    case SA_IMM_ATTR_VALUES_DELETE:
      // Configure destinations
      CfgDestination(vdestcfg, ModifyType::kDelete);
      lgs_cfgupd_multival_delete(LOG_RECORD_DESTINATION_CONFIGURATION, vdestcfg,
                                 config_data);
      break;

    case SA_IMM_ATTR_VALUES_REPLACE:
      CfgDestination(vdestcfg, ModifyType::kReplace);
      lgs_cfgupd_mutival_replace(LOG_RECORD_DESTINATION_CONFIGURATION, vdestcfg,
                                 config_data);
      break;

    default:
      // Shall never happen
      LOG_ER("%s: Unknown modType %d", __FUNCTION__, type);
      osafassert(0);
  };
}

/**
 * Apply changes. Validation is not needed here since all validation is done in
 * the complete callback
 *
 * @param opdata[in] From IMM
 */
static void config_ccb_apply_modify(const CcbUtilOperationData_t *opdata) {
  const SaImmAttrModificationT_2 *attrMod;
  int i = 0;
  char *value_str = NULL;
  uint32_t uint32_val = 0;
  char uint32_str[20] = {0};
  lgs_config_chg_t config_data = {NULL, 0};
  int rc = 0;
  bool root_dir_chg_flag = false; /* Needed for v3 ckpt protocol */

  /* Flag set if any of the mailbox limit values have changed */
  bool mailbox_lim_upd = false;

  TRACE_ENTER();

  attrMod = opdata->param.modify.attrMods[i++];
  while (attrMod != NULL) {
    /* Get all changed config data and save in cfg buffer */
    const SaImmAttrValuesT_2 *attribute = &attrMod->modAttr;
    void *value = NULL;
    if (attribute->attrValuesNumber != 0)
      value = attribute->attrValues[0];
    else
      value = NULL;

    TRACE("attribute %s", attribute->attrName);

    /* For each modified attribute:
     * - Get the new value
     * - Apply the change
     * - Add to update buffer. Used for updating configuration data
     *   and check-pointing
     */
    if (!strcmp(attribute->attrName, LOG_ROOT_DIRECTORY)) {
      value_str = *((char **)value); /* New directory */
      const char *old_dir =
          static_cast<const char *>(lgs_cfg_get(LGS_IMM_LOG_ROOT_DIRECTORY));
      apply_conf_logRootDirectory(old_dir, value_str);
      lgs_cfgupd_list_create(LOG_ROOT_DIRECTORY, value_str, &config_data);
      root_dir_chg_flag = true;
    } else if (!strcmp(attribute->attrName, LOG_DATA_GROUPNAME)) {
      if (value == NULL) {
        value_str = const_cast<char *>("");
      } else {
        value_str = *((char **)value);
      }
      apply_conf_logDataGroupname(value_str);
      lgs_cfgupd_list_create(LOG_DATA_GROUPNAME, value_str, &config_data);
    } else if (!strcmp(attribute->attrName, LOG_STREAM_FILE_FORMAT)) {
      if (value == NULL) {
        /* Use built-in file format as default */
        value_str = const_cast<char *>(DEFAULT_APP_SYS_FORMAT_EXP);
      } else {
        value_str = *((char **)value);
      }
      /**
       * This attribute value is used for default log file format of
       * app stream. Changing this value don't result in cfg/log file creation.
       */
      lgs_cfgupd_list_create(LOG_STREAM_FILE_FORMAT, value_str, &config_data);
    } else if (!strcmp(attribute->attrName, LOG_STREAM_SYSTEM_HIGH_LIMIT)) {
      uint32_val = *(uint32_t *)value;
      snprintf(uint32_str, 20, "%u", uint32_val);
      mailbox_lim_upd = true;
      lgs_cfgupd_list_create(LOG_STREAM_SYSTEM_HIGH_LIMIT, uint32_str,
                             &config_data);
    } else if (!strcmp(attribute->attrName, LOG_STREAM_SYSTEM_LOW_LIMIT)) {
      uint32_val = *(uint32_t *)value;
      snprintf(uint32_str, 20, "%u", uint32_val);
      mailbox_lim_upd = true;
      lgs_cfgupd_list_create(LOG_STREAM_SYSTEM_LOW_LIMIT, uint32_str,
                             &config_data);
    } else if (!strcmp(attribute->attrName, LOG_STREAM_APP_HIGH_LIMIT)) {
      uint32_val = *(uint32_t *)value;
      snprintf(uint32_str, 20, "%u", uint32_val);
      mailbox_lim_upd = true;
      lgs_cfgupd_list_create(LOG_STREAM_APP_HIGH_LIMIT, uint32_str,
                             &config_data);
    } else if (!strcmp(attribute->attrName, LOG_STREAM_APP_LOW_LIMIT)) {
      uint32_val = *(uint32_t *)value;
      snprintf(uint32_str, 20, "%u", uint32_val);
      mailbox_lim_upd = true;
      lgs_cfgupd_list_create(LOG_STREAM_APP_LOW_LIMIT, uint32_str,
                             &config_data);
    } else if (!strcmp(attribute->attrName, LOG_MAX_LOGRECSIZE)) {
      uint32_val = *(SaUint32T *)value;
      snprintf(uint32_str, 20, "%u", uint32_val);
      lgs_cfgupd_list_create(LOG_MAX_LOGRECSIZE, uint32_str, &config_data);
    } else if (!strcmp(attribute->attrName, LOG_FILE_IO_TIMEOUT)) {
      uint32_val = *(SaUint32T *)value;
      snprintf(uint32_str, 20, "%u", uint32_val);
      lgs_cfgupd_list_create(LOG_FILE_IO_TIMEOUT, uint32_str, &config_data);
    } else if (!strcmp(attribute->attrName,
                       LOG_RECORD_DESTINATION_CONFIGURATION)) {
      // Note: Multi value attribute
      TRACE("logRecordDestinationConfiguration");
      std::vector<std::string> values_vector;
      for (uint32_t i = 0; i < attribute->attrValuesNumber; i++) {
        value = attribute->attrValues[i];
        char *value_str = *(reinterpret_cast<char **>(value));
        values_vector.push_back(value_str);
      }
      apply_config_destinations_change(values_vector, attrMod->modType,
                                       &config_data);
    }

    attrMod = opdata->param.modify.attrMods[i++];
  }

  /* Update configuration data store */
  rc = lgs_cfg_update(&config_data);
  if (rc == -1) {
    LOG_ER("%s lgs_cfg_update Fail", __FUNCTION__);
    osafassert(0);
  }

  /* Update mailbox limits if any of them has changed
   * Note: Must be done after configuration data store is updated
   */
  if (mailbox_lim_upd == true) {
    TRACE("\tUpdating mailbox limits");
    (void)lgs_configure_mailbox();
  }

  /* Check-point changes */
  (void)ckpt_lgs_cfg(root_dir_chg_flag, &config_data);

  lgs_trace_config();

  /* Cleanup and free cfg buffer */
  if (config_data.ckpt_buffer_ptr != NULL) free(config_data.ckpt_buffer_ptr);

  TRACE_LEAVE();
}

static void config_ccb_apply(const CcbUtilOperationData_t *opdata) {
  TRACE_ENTER();

  switch (opdata->operationType) {
    case CCBUTIL_CREATE:
      osafassert(0);
      break;
    case CCBUTIL_MODIFY:
      config_ccb_apply_modify(opdata);
      break;
    case CCBUTIL_DELETE:
      osafassert(0);
      break;
    default:
      osafassert(0);
      break;
  }

  TRACE_LEAVE();
}

/**
 * Allocate and initialize new application configuration stream object.
 * @param ccb
 *
 * @return SaAisErrorT
 */
static SaAisErrorT stream_create_and_configure1(
    const struct CcbUtilOperationData *ccb, log_stream_t **stream) {
  SaAisErrorT rc = SA_AIS_OK;
  *stream = NULL;
  int i = 0;
  std::string objectName;
  std::string fileName;
  std::string pathName;

  TRACE_ENTER();

  while (ccb->param.create.attrValues[i] != NULL) {
    if (!strncmp(ccb->param.create.attrValues[i]->attrName, "safLgStrCfg",
                 sizeof("safLgStrCfg"))) {
      SaConstStringT parentName =
          osaf_extended_name_borrow(ccb->param.create.parentName);
      if (strlen(parentName) > 0) {
        objectName =
            std::string(*(const SaStringT *)ccb->param.create.attrValues[i]
                             ->attrValues[0]) +
            "," + parentName;
      } else {
        objectName = std::string(
            *(const SaStringT *)ccb->param.create.attrValues[i]->attrValues[0]);
      }

      if ((*stream = log_stream_new(objectName, STREAM_NEW)) == NULL) {
        rc = SA_AIS_ERR_NO_MEMORY;
        goto done;
      }
    }
    i++;
  }

  if (*stream == NULL) goto done;

  i = 0;

  // a configurable application stream.
  (*stream)->streamType = STREAM_TYPE_APPLICATION;

  while (ccb->param.create.attrValues[i] != NULL) {
    if (ccb->param.create.attrValues[i]->attrValuesNumber > 0) {
      SaImmAttrValueT value = ccb->param.create.attrValues[i]->attrValues[0];

      if (!strcmp(ccb->param.create.attrValues[i]->attrName,
                  "saLogStreamFileName")) {
        fileName = *(static_cast<char **>(value));
      } else if (!strcmp(ccb->param.create.attrValues[i]->attrName,
                         "saLogStreamPathName")) {
        pathName = *(static_cast<char **>(value));
      } else if (!strcmp(ccb->param.create.attrValues[i]->attrName,
                         "saLogStreamMaxLogFileSize")) {
        (*stream)->maxLogFileSize = *((SaUint64T *)value);
        TRACE("maxLogFileSize: %llu", (*stream)->maxLogFileSize);
      } else if (!strcmp(ccb->param.create.attrValues[i]->attrName,
                         "saLogStreamFixedLogRecordSize")) {
        (*stream)->fixedLogRecordSize = *((SaUint32T *)value);
        TRACE("fixedLogRecordSize: %u", (*stream)->fixedLogRecordSize);
      } else if (!strcmp(ccb->param.create.attrValues[i]->attrName,
                         "saLogStreamLogFullAction")) {
        (*stream)->logFullAction =
            static_cast<SaLogFileFullActionT>(*static_cast<SaUint32T *>(value));
        TRACE("logFullAction: %u", (*stream)->logFullAction);
      } else if (!strcmp(ccb->param.create.attrValues[i]->attrName,
                         "saLogStreamLogFullHaltThreshold")) {
        (*stream)->logFullHaltThreshold = *((SaUint32T *)value);
        TRACE("logFullHaltThreshold: %u", (*stream)->logFullHaltThreshold);
      } else if (!strcmp(ccb->param.create.attrValues[i]->attrName,
                         "saLogStreamMaxFilesRotated")) {
        (*stream)->maxFilesRotated = *((SaUint32T *)value);
        TRACE("maxFilesRotated: %u", (*stream)->maxFilesRotated);
      } else if (!strcmp(ccb->param.create.attrValues[i]->attrName,
                         "saLogStreamLogFileFormat")) {
        SaBoolT dummy;
        char *logFileFormat = *((char **)value);
        if (!lgs_is_valid_format_expression(logFileFormat,
                                            (*stream)->streamType, &dummy)) {
          LOG_WA("Invalid logFileFormat for stream %s, using default",
                 (*stream)->name.c_str());
          logFileFormat = const_cast<char *>(static_cast<const char *>(
              lgs_cfg_get(LGS_IMM_LOG_STREAM_FILE_FORMAT)));
        }
        (*stream)->logFileFormat = strdup(logFileFormat);
        TRACE("logFileFormat: %s", (*stream)->logFileFormat);
      } else if (!strcmp(ccb->param.create.attrValues[i]->attrName,
                         "saLogStreamSeverityFilter")) {
        (*stream)->severityFilter = *((SaUint32T *)value);
        TRACE("severityFilter: %u", (*stream)->severityFilter);
      } else if (!strcmp(ccb->param.create.attrValues[i]->attrName,
                         "saLogRecordDestination")) {
        std::vector<std::string> vstring{};
        for (unsigned ii = 0;
             ii < ccb->param.create.attrValues[i]->attrValuesNumber; ii++) {
          value = ccb->param.create.attrValues[i]->attrValues[ii];
          char *value_str = *(reinterpret_cast<char **>(value));
          vstring.push_back(value_str);
        }

        log_stream_add_dest_name(*stream, vstring);
        if (vstring.empty() == false) {
          // Generate & cache `MSGID` to `rfc5424MsgId` which later
          // used in RFC5424 protocol
          (*stream)->rfc5424MsgId =
              DestinationHandler::Instance().GenerateMsgId(
              (*stream)->name, (*stream)->isRtStream);
          TRACE("%s: stream %s - msgid = %s", __func__, (*stream)->name.c_str(),
                (*stream)->rfc5424MsgId.c_str());
        }
      }
    }
    i++;
  }  // while

  /* Check if filename & pathName is valid */
  if (lgs_is_valid_filelength(fileName) &&
      lgs_is_valid_pathlength(pathName, fileName)) {
    (*stream)->fileName = fileName;
    (*stream)->pathName = pathName;
    TRACE("fileName: %s", fileName.c_str());
    TRACE("pathName: %s", pathName.c_str());
  } else {
    TRACE("Problem with fileName (%s) or pathName (%s)", fileName.c_str(),
          pathName.c_str());
    rc = SA_AIS_ERR_NO_MEMORY;
    goto done;
  }

  if ((*stream)->logFileFormat == NULL) {
    /* If passing NULL to log file format, use default value */
    const char *logFileFormat =
        static_cast<const char *>(lgs_cfg_get(LGS_IMM_LOG_STREAM_FILE_FORMAT));
    (*stream)->logFileFormat = strdup(logFileFormat);
  }

  /* Update creation timestamp */
  rc = immutil_update_one_rattr(
      lgs_cb->immOiHandle, objectName.c_str(),
      const_cast<SaImmAttrNameT>("saLogStreamCreationTimestamp"),
      SA_IMM_ATTR_SATIMET, &(*stream)->creationTimeStamp);
  if (rc != SA_AIS_OK) {
    LOG_ER("immutil_update_one_rattr failed %s", saf_error(rc));
    osaf_abort(0);
  }

done:
  TRACE_LEAVE2("rc: %s", saf_error(rc));
  return rc;
}

static void stream_ccb_apply_create(const CcbUtilOperationData_t *opdata) {
  SaAisErrorT rc;
  log_stream_t *stream;

  TRACE_ENTER2("CCB ID %llu, '%s'", opdata->ccbId,
               osaf_extended_name_borrow(&opdata->objectName));

  if ((rc = stream_create_and_configure1(opdata, &stream)) == SA_AIS_OK) {
    log_stream_open_fileinit(stream);
    // Checkpoint the opened stream with invalid clientId (-1)
    lgs_ckpt_stream_open(stream, -1);
  } else {
    LOG_IN("Stream create and configure failed %d", rc);
  }

  TRACE_LEAVE();
}

static void apply_destination_names_change(
    log_stream_t *stream, const std::vector<std::string> &destname,
    SaImmAttrModificationTypeT type) {
  switch (type) {
    case SA_IMM_ATTR_VALUES_ADD:
      log_stream_add_dest_name(stream, destname);
      break;

    case SA_IMM_ATTR_VALUES_DELETE:
      log_stream_delete_dest_name(stream, destname);
      break;

    case SA_IMM_ATTR_VALUES_REPLACE:
      log_stream_replace_dest_name(stream, destname);
      break;

    default:
      // Shall never happen
      LOG_ER("%s: Unknown modType %d", __FUNCTION__, type);
      osafassert(0);
  }
}

static void stream_ccb_apply_modify(const CcbUtilOperationData_t *opdata) {
  const SaImmAttrModificationT_2 *attrMod;
  int i = 0;
  log_stream_t *stream;
  std::string current_logfile_name;
  bool new_cfg_file_needed = false;
  struct timespec curtime_tspec;
  std::string fileName;
  bool stream_filename_modified = false;
  SaConstStringT objName = osaf_extended_name_borrow(&opdata->objectName);
  TRACE_ENTER2("CCB ID %llu, '%s'", opdata->ccbId, objName);

  stream = log_stream_get_by_name(objName);
  osafassert(stream);

  current_logfile_name = stream->logFileCurrent;

  attrMod = opdata->param.modify.attrMods[i++];
  while (attrMod != NULL) {
    void *value = nullptr;
    const SaImmAttrValuesT_2 *attribute = &attrMod->modAttr;

    TRACE("attribute %s", attribute->attrName);
    if (attribute->attrValuesNumber != 0) {
      value = attribute->attrValues[0];
    } else {
      if (!strcmp(attribute->attrName, "saLogRecordDestination")) {
        const std::vector<std::string> dname{};
        LOG_NO("%s deleted", __func__);
        log_stream_delete_dest_name(stream, dname);
        attrMod = opdata->param.modify.attrMods[i++];
        continue;
      }
    }

    if (!strcmp(attribute->attrName, "saLogStreamFileName")) {
      fileName = *((char **)value);
      stream_filename_modified = true;
    } else if (!strcmp(attribute->attrName, "saLogStreamMaxLogFileSize")) {
      SaUint64T maxLogFileSize = *((SaUint64T *)value);
      stream->maxLogFileSize = maxLogFileSize;
      new_cfg_file_needed = true;
    } else if (!strcmp(attribute->attrName, "saLogStreamFixedLogRecordSize")) {
      SaUint32T fixedLogRecordSize = *((SaUint32T *)value);
      stream->fixedLogRecordSize = fixedLogRecordSize;
      new_cfg_file_needed = true;
    } else if (!strcmp(attribute->attrName, "saLogStreamLogFullAction")) {
      SaLogFileFullActionT logFullAction =
          static_cast<SaLogFileFullActionT>(*static_cast<SaUint32T *>(value));
      stream->logFullAction = logFullAction;
      new_cfg_file_needed = true;
    } else if (!strcmp(attribute->attrName,
                       "saLogStreamLogFullHaltThreshold")) {
      SaUint32T logFullHaltThreshold = *((SaUint32T *)value);
      stream->logFullHaltThreshold = logFullHaltThreshold;
    } else if (!strcmp(attribute->attrName, "saLogStreamMaxFilesRotated")) {
      SaUint32T maxFilesRotated = *((SaUint32T *)value);
      stream->maxFilesRotated = maxFilesRotated;
      new_cfg_file_needed = true;
    } else if (!strcmp(attribute->attrName, "saLogStreamLogFileFormat")) {
      char *logFileFormat = *((char **)value);
      if (stream->logFileFormat != NULL) free(stream->logFileFormat);
      stream->logFileFormat = strdup(logFileFormat);
      new_cfg_file_needed = true;
    } else if (!strcmp(attribute->attrName, "saLogStreamSeverityFilter")) {
      SaUint32T severityFilter = *((SaUint32T *)value);
      stream->severityFilter = severityFilter;

      /* Send changed severity filter to clients */
      if (stream->streamType != STREAM_TYPE_ALARM &&
          stream->streamType != STREAM_TYPE_NOTIFICATION)
        lgs_send_severity_filter_to_clients(stream->streamId, severityFilter);
    } else if (!strcmp(attribute->attrName, "saLogRecordDestination")) {
      std::vector<std::string> vstring{};
      for (unsigned i = 0; i < attribute->attrValuesNumber; i++) {
        value = attribute->attrValues[i];
        char *value_str = *(reinterpret_cast<char **>(value));
        vstring.push_back(value_str);
      }

      apply_destination_names_change(stream, vstring, attrMod->modType);
      // Make sure generated msg is only called when
      // 1) Have destination set
      // 2) Not yet generated
      if (vstring.empty() == false && stream->rfc5424MsgId.empty() == true) {
        // Generate & cache `MSGID` to `rfc5424MsgId` which later
        // used in RFC5424 protocol
        stream->rfc5424MsgId =
            DestinationHandler::Instance().GenerateMsgId(
                stream->name, stream->isRtStream);
        TRACE("%s: stream %s - msgid = %s", __func__, stream->name.c_str(),
              stream->rfc5424MsgId.c_str());
      }
    } else {
      LOG_ER("Error: Unknown attribute name");
      osafassert(0);
    }

    attrMod = opdata->param.modify.attrMods[i++];
  }

  osaf_clock_gettime(CLOCK_REALTIME, &curtime_tspec);
  time_t cur_time = curtime_tspec.tv_sec;
  std::string root_path =
      static_cast<const char *>(lgs_cfg_get(LGS_IMM_LOG_ROOT_DIRECTORY));

  /* Fix for ticket #1346 */
  if (stream_filename_modified) {
    int rc;
    if ((rc = log_stream_config_change(!LGS_STREAM_CREATE_FILES, root_path,
                                       stream, current_logfile_name,
                                       &cur_time)) != 0) {
      LOG_WA("log_stream_config_change failed: %d", rc);
    }

    stream->fileName = fileName;

    if ((rc = lgs_create_config_file_h(root_path, stream)) != 0) {
      LOG_WA("lgs_create_config_file_h failed: %d", rc);
    }

    char *current_time = lgs_get_time(&cur_time);
    stream->logFileCurrent = stream->fileName + "_" + current_time;

    // Create the new log file based on updated configuration
    if ((*stream->p_fd = log_file_open(root_path, stream,
                                       stream->logFileCurrent, NULL)) == -1)
      LOG_WA("New log file could not be created for stream: %s",
             stream->name.c_str());
  } else if (new_cfg_file_needed) {
    int rc;
    if ((rc = log_stream_config_change(LGS_STREAM_CREATE_FILES, root_path,
                                       stream, current_logfile_name,
                                       &cur_time)) != 0) {
      LOG_WA("log_stream_config_change failed: %d", rc);
    }
  }

  /* Checkpoint to standby LOG server */
  /* Save time change was done for standby */
  stream->act_last_close_timestamp = cur_time;
  ckpt_stream_config(stream);

  TRACE_LEAVE();
}

static void stream_ccb_apply_delete(const CcbUtilOperationData_t *opdata) {
  log_stream_t *stream;
  struct timespec closetime_tspec;
  osaf_clock_gettime(CLOCK_REALTIME, &closetime_tspec);
  time_t file_closetime = closetime_tspec.tv_sec;
  SaConstStringT objName = osaf_extended_name_borrow(&opdata->objectName);

  TRACE_ENTER2("CCB ID %llu, '%s'", opdata->ccbId, objName);

  stream = log_stream_get_by_name(objName);

  /* Checkpoint to standby LOG server */
  ckpt_stream_close(stream, file_closetime);
  log_stream_close(&stream, &file_closetime);

  TRACE_LEAVE();
}

static void stream_ccb_apply(const CcbUtilOperationData_t *opdata) {
  TRACE_ENTER2("CCB ID %llu", opdata->ccbId);

  switch (opdata->operationType) {
    case CCBUTIL_CREATE:
      stream_ccb_apply_create(opdata);
      break;
    case CCBUTIL_MODIFY:
      stream_ccb_apply_modify(opdata);
      break;
    case CCBUTIL_DELETE:
      stream_ccb_apply_delete(opdata);
      break;
    default:
      assert(0);
      break;
  }

  TRACE_LEAVE();
}

/**
 * Configuration changes are done and now it's time to act on the changes
 *
 * @param immOiHandle
 * @param ccbId
 */
static void ccbApplyCallback(SaImmOiHandleT immOiHandle, SaImmOiCcbIdT ccbId) {
  struct CcbUtilCcbData *ccbUtilCcbData;
  struct CcbUtilOperationData *opdata;

  TRACE_ENTER2("CCB ID %llu", ccbId);

  if ((ccbUtilCcbData = ccbutil_findCcbData(ccbId)) == NULL) {
    LOG_ER("%s: Failed to find CCB object for %llu", __FUNCTION__, ccbId);
    goto done;  // or exit?
  }

  for (opdata = ccbUtilCcbData->operationListHead; opdata;
       opdata = opdata->next) {
    switch (opdata->operationType) {
      case CCBUTIL_CREATE:
        if (!strcmp(opdata->param.create.className, "OpenSafLogConfig"))
          config_ccb_apply(opdata);
        else if (!strcmp(opdata->param.create.className, "SaLogStreamConfig"))
          stream_ccb_apply(opdata);
        else
          osafassert(0);
        break;
      case CCBUTIL_DELETE:
      case CCBUTIL_MODIFY:
        if (!strncmp(osaf_extended_name_borrow(&opdata->objectName),
                     "safLgStrCfg", 11))
          stream_ccb_apply(opdata);
        else
          config_ccb_apply(opdata);
        break;
      default:
        assert(0);
        break;
    }
  }

done:
  ccbutil_deleteCcbData(ccbUtilCcbData);
  TRACE_LEAVE();
}

static void ccbAbortCallback(SaImmOiHandleT immOiHandle, SaImmOiCcbIdT ccbId) {
  struct CcbUtilCcbData *ccbUtilCcbData;

  TRACE_ENTER2("CCB ID %llu", ccbId);

  if ((ccbUtilCcbData = ccbutil_findCcbData(ccbId)) != NULL)
    ccbutil_deleteCcbData(ccbUtilCcbData);
  else
    LOG_ER("%s: Failed to find CCB object for %llu", __FUNCTION__, ccbId);

  TRACE_LEAVE();
}

/**
 * IMM requests us to update a non cached runtime attribute.
 * Can be non cached attribute in stream runtime object or
 * runtime configuration object
 *
 * @param immOiHandle[in]
 * @param objectName[in]
 * @param attributeNames[in]
 *
 * @return SaAisErrorT
 */
static SaAisErrorT rtAttrUpdateCallback(SaImmOiHandleT immOiHandle,
                                        const SaNameT *objectName,
                                        const SaImmAttrNameT *attributeNames) {
  SaAisErrorT rc = SA_AIS_ERR_FAILED_OPERATION;
  SaImmAttrNameT attributeName;
  int i = 0;
  SaConstStringT objName = osaf_extended_name_borrow(objectName);
  TRACE_ENTER2("%s", objName);

  if (lgs_cb->ha_state != SA_AMF_HA_ACTIVE) {
    LOG_ER("admin op callback in applier");
    goto done;
  }

  /* Handle configuration runtime object */
  if (strncmp(objName, LGS_CFG_RUNTIME_OBJECT, strlen(objName)) == 0) {
    /* Handle Runtome configuration object */
    conf_runtime_obj_hdl(immOiHandle, attributeNames);
  } else {
    /* Handle stream object if valid
     */
    log_stream_t *stream = log_stream_get_by_name(objName);
    if (stream == NULL) {
      LOG_ER("%s: stream %s not found", __FUNCTION__, objName);
      goto done;
    }

    while ((attributeName = attributeNames[i++]) != NULL) {
      TRACE("Attribute %s", attributeName);
      rc = SA_AIS_OK;
      if (!strcmp(attributeName, "saLogStreamNumOpeners")) {
        rc = immutil_update_one_rattr(immOiHandle, objName, attributeName,
                                      SA_IMM_ATTR_SAUINT32T,
                                      &stream->numOpeners);
      } else if (!strcmp(attributeName, "logStreamDiscardedCounter")) {
        rc = immutil_update_one_rattr(immOiHandle, objName, attributeName,
                                      SA_IMM_ATTR_SAUINT64T, &stream->filtered);
      } else {
        LOG_ER("%s: unknown attribute %s", __FUNCTION__, attributeName);
        goto done;
      }

      if (rc != SA_AIS_OK) {
        LOG_ER("immutil_update_one_rattr (%s) failed %s", attributeName,
               saf_error(rc));
        osaf_abort(0);
      }
    }
  }

  rc = SA_AIS_OK;

done:
  TRACE_LEAVE();
  return rc;
}

/**
 * Allocate new stream object. Get configuration from IMM and
 * initialize the stream object.
 *
 * @param dn
 * @param in_stream
 * @param stream_id
 *
 * @return SaAisErrorT
 */
static SaAisErrorT stream_create_and_configure(
    const std::string &dn, log_stream_t **in_stream, int stream_id,
    SaImmAccessorHandleT accessorHandle) {
  SaAisErrorT rc = SA_AIS_OK;
  SaNameT objectName;
  SaImmAttrValuesT_2 *attribute;
  SaImmAttrValuesT_2 **attributes;
  int i = 0;
  log_stream_t *stream;
  char *attribute_names[] = {
      const_cast<char *>("saLogStreamFileName"),
      const_cast<char *>("saLogStreamPathName"),
      const_cast<char *>("saLogStreamMaxLogFileSize"),
      const_cast<char *>("saLogStreamFixedLogRecordSize"),
      const_cast<char *>("saLogStreamLogFullAction"),
      const_cast<char *>("saLogStreamLogFullHaltThreshold"),
      const_cast<char *>("saLogStreamMaxFilesRotated"),
      const_cast<char *>("saLogStreamLogFileFormat"),
      const_cast<char *>("saLogRecordDestination"),
      const_cast<char *>("saLogStreamSeverityFilter"),
      const_cast<char *>("saLogStreamCreationTimestamp"),
      NULL};

  TRACE_ENTER2("(%s)", dn.c_str());

  osaf_extended_name_lend(dn.c_str(), &objectName);

  *in_stream = stream = log_stream_new(dn, stream_id);

  if (stream == NULL) {
    rc = SA_AIS_ERR_NO_MEMORY;
    goto done;
  }

  if (dn == SA_LOG_STREAM_ALARM)
    stream->streamType = STREAM_TYPE_ALARM;
  else if (dn == SA_LOG_STREAM_NOTIFICATION)
    stream->streamType = STREAM_TYPE_NOTIFICATION;
  else if (dn == SA_LOG_STREAM_SYSTEM)
    stream->streamType = STREAM_TYPE_SYSTEM;
  else
    stream->streamType = STREAM_TYPE_APPLICATION;

  /* Get all attributes of the object */
  if ((rc = immutil_saImmOmAccessorGet_2(accessorHandle, &objectName,
                                         attribute_names, &attributes)) !=
      SA_AIS_OK) {
    LOG_ER("Configuration for %s not found: %s", dn.c_str(), saf_error(rc));
    rc = SA_AIS_ERR_NOT_EXIST;
    goto done;
  }

  while ((attribute = attributes[i++]) != NULL) {
    void *value;

    if (attribute->attrValuesNumber == 0) continue;

    value = attribute->attrValues[0];

    if (!strcmp(attribute->attrName, "saLogStreamFileName")) {
      stream->fileName = *(static_cast<char **>(value));
      TRACE("fileName: %s", stream->fileName.c_str());
    } else if (!strcmp(attribute->attrName, "saLogStreamPathName")) {
      stream->pathName = *(static_cast<char **>(value));
      TRACE("pathName: %s", stream->pathName.c_str());
    } else if (!strcmp(attribute->attrName, "saLogStreamMaxLogFileSize")) {
      stream->maxLogFileSize = *((SaUint64T *)value);
      TRACE("maxLogFileSize: %llu", stream->maxLogFileSize);
    } else if (!strcmp(attribute->attrName, "saLogStreamFixedLogRecordSize")) {
      stream->fixedLogRecordSize = *((SaUint32T *)value);
      TRACE("fixedLogRecordSize: %u", stream->fixedLogRecordSize);
    } else if (!strcmp(attribute->attrName, "saLogStreamLogFullAction")) {
      stream->logFullAction =
          static_cast<SaLogFileFullActionT>(*static_cast<SaUint32T *>(value));
      TRACE("logFullAction: %u", stream->logFullAction);
    } else if (!strcmp(attribute->attrName,
                       "saLogStreamLogFullHaltThreshold")) {
      stream->logFullHaltThreshold = *((SaUint32T *)value);
      TRACE("logFullHaltThreshold: %u", stream->logFullHaltThreshold);
    } else if (!strcmp(attribute->attrName, "saLogStreamMaxFilesRotated")) {
      stream->maxFilesRotated = *((SaUint32T *)value);
      TRACE("maxFilesRotated: %u", stream->maxFilesRotated);
    } else if (!strcmp(attribute->attrName, "saLogStreamLogFileFormat")) {
      SaBoolT dummy;
      char *logFileFormat = *((char **)value);
      if (!lgs_is_valid_format_expression(logFileFormat, stream->streamType,
                                          &dummy)) {
        LOG_WA("Invalid logFileFormat for stream %s, using default",
               stream->name.c_str());

        if (stream->streamType == STREAM_TYPE_APPLICATION) {
          logFileFormat = const_cast<char *>(static_cast<const char *>(
              lgs_cfg_get(LGS_IMM_LOG_STREAM_FILE_FORMAT)));
        } else {
          strcpy(logFileFormat, log_file_format[stream->streamType]);
        }
      }
      stream->logFileFormat = strdup(logFileFormat);
      TRACE("logFileFormat: %s", stream->logFileFormat);
    } else if (!strcmp(attribute->attrName, "saLogStreamSeverityFilter")) {
      stream->severityFilter = *((SaUint32T *)value);
      TRACE("severityFilter: %u", stream->severityFilter);
    } else if (!strcmp(attribute->attrName, "saLogRecordDestination")) {
      std::vector<std::string> vstring{};
      for (unsigned i = 0; i < attribute->attrValuesNumber; i++) {
        value = attribute->attrValues[i];
        char *value_str = *(reinterpret_cast<char **>(value));
        vstring.push_back(value_str);
      }
      log_stream_add_dest_name(stream, vstring);
      if (vstring.empty() == false) {
        // Generate & cache `MSGID` to `rfc5424MsgId` which later
        // used in RFC5424 protocol
        stream->rfc5424MsgId = DestinationHandler::Instance().GenerateMsgId(
            dn, stream->isRtStream);
        TRACE("%s: stream %s - msgid = %s", __func__, stream->name.c_str(),
              stream->rfc5424MsgId.c_str());
      }
    } else if (!strcmp(attribute->attrName, "saLogStreamCreationTimestamp")) {
      if (attribute->attrValuesNumber != 0) {
        /* Restore creation timestamp if exist
         * Note: Creation timestamp in stream is set to
         * current time which will be the value if no
         * value found in object
         */
        stream->creationTimeStamp = *(static_cast<SaTimeT *>(value));
      }
    }
  }

  /* Check if the fileName and pathName are valid */
  if ((lgs_is_valid_filelength(stream->fileName) == false) ||
      (lgs_is_valid_pathlength(stream->pathName, stream->fileName) == false)) {
    LOG_ER("Problem with fileName (%s)/pathName (%s)", stream->fileName.c_str(),
           stream->pathName.c_str());
    osafassert(0);
  }

  if (stream->logFileFormat == NULL) {
    if (stream->streamType == STREAM_TYPE_APPLICATION) {
      const char *logFileFormat = static_cast<const char *>(
          lgs_cfg_get(LGS_IMM_LOG_STREAM_FILE_FORMAT));
      stream->logFileFormat = strdup(logFileFormat);
    } else {
      stream->logFileFormat = strdup(log_file_format[stream->streamType]);
    }
  }

done:
  TRACE_LEAVE2("rc: %s", saf_error(rc));
  return rc;
}

static const SaImmOiCallbacksT_2 callbacks = {
    .saImmOiAdminOperationCallback = adminOperationCallback,
    .saImmOiCcbAbortCallback = ccbAbortCallback,
    .saImmOiCcbApplyCallback = ccbApplyCallback,
    .saImmOiCcbCompletedCallback = ccbCompletedCallback,
    .saImmOiCcbObjectCreateCallback = ccbObjectCreateCallback,
    .saImmOiCcbObjectDeleteCallback = ccbObjectDeleteCallback,
    .saImmOiCcbObjectModifyCallback = ccbObjectModifyCallback,
    .saImmOiRtAttrUpdateCallback = rtAttrUpdateCallback};

/**
 * Retrieve the LOG stream configuration from IMM using the
 * IMM-OM interface and initialize the corresponding information
 * in the LOG control block. Initialize the LOG IMM-OI
 * interface. Become class implementer.
 *
 * @param cb[in] control block
 * @return
 */
SaAisErrorT lgs_imm_init_configStreams(lgs_cb_t *cb) {
  SaAisErrorT ais_rc = SA_AIS_OK;
  SaAisErrorT om_rc;
  int int_rc = 0;
  log_stream_t *stream;
  SaImmHandleT omHandle;
  SaImmAccessorHandleT accessorHandle;
  SaVersionT immVersion = {'A', 2, 1};
  SaImmSearchHandleT immSearchHandle;
  SaImmSearchParametersT_2 objectSearch;
  SaImmAttrValuesT_2 **attributes;
  int wellknownStreamId = 0;
  int appStreamId = 3;
  uint32_t streamId = 0;
  SaNameT objectName;
  const char *className = "SaLogStreamConfig";
  SaBoolT endloop = SA_FALSE, jstart = SA_TRUE;
  TRACE_ENTER();

  om_rc = immutil_saImmOmInitialize(&omHandle, NULL, &immVersion);
  if (om_rc != SA_AIS_OK) {
    LOG_ER("immutil_saImmOmInitialize failed %s", saf_error(om_rc));
    osaf_abort(0);
  }

  om_rc = immutil_saImmOmAccessorInitialize(omHandle, &accessorHandle);
  if (om_rc != SA_AIS_OK) {
    LOG_ER("immutil_saImmOmAccessorInitialize failed %s", saf_error(om_rc));
    osaf_abort(0);
  }

  /* Search for all objects of class "SaLogStreamConfig". */
  /**
   * Should not base on the attribute name `safLgStrCfg`
   * as the user can create any class having that name
   */
  objectSearch.searchOneAttr.attrName =
      const_cast<SaImmAttrNameT>("SaImmAttrClassName");
  objectSearch.searchOneAttr.attrValueType = SA_IMM_ATTR_SASTRINGT;
  objectSearch.searchOneAttr.attrValue = &className;

  /**
   * Search from root as app stream DN name can be any - maybe not under the RDN
   * `safApp=safLogService` Therefore, searching all class under
   * `safApp=safLogService` might miss configurable app stream eg: app stream
   * with DN `saLgStrCfg=test`
   */
  if ((om_rc = immutil_saImmOmSearchInitialize_2(
           omHandle, NULL, SA_IMM_SUBTREE,
           SA_IMM_SEARCH_ONE_ATTR | SA_IMM_SEARCH_GET_NO_ATTR, &objectSearch,
           NULL, /* Get no attributes */
           &immSearchHandle)) == SA_AIS_OK) {
    while (immutil_saImmOmSearchNext_2(immSearchHandle, &objectName,
                                       &attributes) == SA_AIS_OK) {
      /**
       * With headless mode enabled, when lgsv restarts, there could be other
       * configurable app streams. It differs from legacy mode in which no app
       * stream could be exist at startup.
       *
       * With well-known streams, stream ID is in reserved numbers [0-2].
       */
      SaConstStringT name = osaf_extended_name_borrow(&objectName);
      streamId =
          is_well_know_stream(name) ? wellknownStreamId++ : appStreamId++;
      ais_rc =
          stream_create_and_configure(name, &stream, streamId, accessorHandle);
      if (ais_rc != SA_AIS_OK) {
        LOG_WA("stream_create_and_configure failed %d", ais_rc);
        goto done;
      }
    }
  }

  /* 1.Become implementer
   * 2.Update creation timestamp for all configure object, must be object
   * implementer first 3.Open all streams Config file and log file will be
   * created. If this fails we give up without returning any error. A new
   * attempt to create the files will be done when trying to write a log
   * record to the stream.
   */
  ais_rc =
      immutil_saImmOiClassImplementerSet(cb->immOiHandle, "SaLogStreamConfig");
  if (ais_rc != SA_AIS_OK) {
    LOG_ER("immutil_saImmOiClassImplementerSet failed %s", saf_error(ais_rc));
    osaf_abort(0);
  }

  // Iterate all existing log streams in cluster.
  while ((stream = iterate_all_streams(endloop, jstart)) && !endloop) {
    jstart = SA_FALSE;
    if (cb->scAbsenceAllowed != 0) {
      int_rc = log_stream_open_file_restore(stream);
      if (int_rc == -1) {
        /* Restore failed. Initialize instead */
        LOG_NO("%s: log_stream_open_file_restore Fail", __FUNCTION__);
        log_stream_open_fileinit(stream);
        stream->creationTimeStamp = lgs_get_SaTime();
      }
    } else {
      /* Always create new files and set current timestamp to
       * current time
       */
      log_stream_open_fileinit(stream);
      stream->creationTimeStamp = lgs_get_SaTime();
    }

    ais_rc = immutil_update_one_rattr(
        cb->immOiHandle, stream->name.c_str(),
        const_cast<SaImmAttrNameT>("saLogStreamCreationTimestamp"),
        SA_IMM_ATTR_SATIMET, &stream->creationTimeStamp);
    if (ais_rc != SA_AIS_OK) {
      LOG_ER("immutil_update_one_rattr failed %s", saf_error(ais_rc));
      osaf_abort(0);
    }
  }

done:
  /* Do not abort if error when finalizing */
  om_rc = immutil_saImmOmAccessorFinalize(accessorHandle);
  if (om_rc != SA_AIS_OK) {
    LOG_NO("%s immutil_saImmOmAccessorFinalize() Fail %d", __FUNCTION__, om_rc);
  }

  om_rc = immutil_saImmOmSearchFinalize(immSearchHandle);
  if (om_rc != SA_AIS_OK) {
    LOG_NO("%s immutil_saImmOmSearchFinalize() Fail %d", __FUNCTION__, om_rc);
  }

  om_rc = immutil_saImmOmFinalize(omHandle);
  if (om_rc != SA_AIS_OK) {
    LOG_NO("%s immutil_saImmOmFinalize() Fail %d", __FUNCTION__, om_rc);
  }

  TRACE_LEAVE2("rc: %s", saf_error(ais_rc));
  return ais_rc;
}

/**
 * Initialize the OI interface and get a selection object.
 * Become OI for safLogService if Active
 *
 * @param immOiHandle[out]
 * @param immSelectionObject[out]
 */
void lgs_imm_init_OI_handle(SaImmOiHandleT *immOiHandle,
                            SaSelectionObjectT *immSelectionObject) {
  SaAisErrorT rc;
  uint32_t msecs_waited = 0;

  TRACE_ENTER();

  /* Initialize IMM OI service */
  rc = saImmOiInitialize_2(immOiHandle, &callbacks, &immVersion);
  while ((rc == SA_AIS_ERR_TRY_AGAIN) &&
         (msecs_waited < max_waiting_time_60s)) {
    usleep(sleep_delay_ms * 1000);
    msecs_waited += sleep_delay_ms;
    rc = saImmOiInitialize_2(immOiHandle, &callbacks, &immVersion);
  }
  if (rc != SA_AIS_OK) {
    lgs_exit("saImmOiInitialize_2 failed", SA_AMF_COMPONENT_RESTART);
  }

  /* Get selection object for event handling */
  msecs_waited = 0;
  rc = saImmOiSelectionObjectGet(*immOiHandle, immSelectionObject);
  while ((rc == SA_AIS_ERR_TRY_AGAIN) &&
         (msecs_waited < max_waiting_time_10s)) {
    usleep(sleep_delay_ms * 1000);
    msecs_waited += sleep_delay_ms;
    rc = saImmOiSelectionObjectGet(*immOiHandle, immSelectionObject);
  }
  if (rc != SA_AIS_OK) {
    lgs_exit("saImmOiSelectionObjectGet failed", SA_AMF_COMPONENT_RESTART);
  }

  TRACE_LEAVE2("rc: %s", saf_error(rc));
}

/**
 * Does the sequence of setting an implementer name and class implementer
 *
 * @param immOiHandle[in]
 * @return SaAisErrorT
 */
static SaAisErrorT imm_impl_set_sequence(
    SaImmOiHandleT *immOiHandle, SaSelectionObjectT *immSelectionObject) {
  SaAisErrorT rc = SA_AIS_OK;
  uint32_t msecs_waited = 0;

  TRACE_ENTER();

  for (;;) {
    if (msecs_waited >= max_waiting_time_60s) {
      TRACE("Timeout in imm_impl_set_sequence");
      goto done;
    }

    /* Become object implementer
     */
    rc = saImmOiImplementerSet(*immOiHandle, implementerName);
    while (((rc == SA_AIS_ERR_TRY_AGAIN) || (rc == SA_AIS_ERR_EXIST)) &&
           (msecs_waited < max_waiting_time_60s)) {
      usleep(sleep_delay_ms * 1000);
      msecs_waited += sleep_delay_ms;
      rc = saImmOiImplementerSet(*immOiHandle, implementerName);
    }
    if (rc == SA_AIS_ERR_BAD_HANDLE || rc == SA_AIS_ERR_TIMEOUT) {
      LOG_WA("saImmOiImplementerSet returned %s", saf_error(rc));
      usleep(sleep_delay_ms * 1000);
      msecs_waited += sleep_delay_ms;
      saImmOiFinalize(*immOiHandle);
      *immOiHandle = 0;
      *immSelectionObject = -1;
      lgs_imm_init_OI_handle(immOiHandle, immSelectionObject);
      continue;
    }
    if (rc != SA_AIS_OK) {
      TRACE("saImmOiImplementerSet failed %s", saf_error(rc));
      goto done;
    }

    /*
     * Become class implementer for the OpenSafLogConfig class if it exists
     * Become class implementer for the SaLogStreamConfig class
     */
    if (true == *static_cast<const bool *>(
                    lgs_cfg_get(LGS_IMM_LOG_OPENSAFLOGCONFIG_CLASS_EXIST))) {
      rc = saImmOiClassImplementerSet(*immOiHandle, logConfig_str);
      while (((rc == SA_AIS_ERR_TRY_AGAIN) || (rc == SA_AIS_ERR_EXIST)) &&
             (msecs_waited < max_waiting_time_60s)) {
        usleep(sleep_delay_ms * 1000);
        msecs_waited += sleep_delay_ms;
        rc = saImmOiClassImplementerSet(*immOiHandle, logConfig_str);
      }
      if (rc == SA_AIS_ERR_BAD_HANDLE || rc == SA_AIS_ERR_TIMEOUT) {
        LOG_WA("saImmOiClassImplementerSet returned %s", saf_error(rc));
        usleep(sleep_delay_ms * 1000);
        msecs_waited += sleep_delay_ms;
        saImmOiFinalize(*immOiHandle);
        *immOiHandle = 0;
        *immSelectionObject = -1;
        lgs_imm_init_OI_handle(immOiHandle, immSelectionObject);
        continue;
      }
      if (rc != SA_AIS_OK) {
        TRACE("saImmOiClassImplementerSet OpenSafLogConfig failed %s",
              saf_error(rc));
        goto done;
      }
    }

    rc = saImmOiClassImplementerSet(*immOiHandle, streamConfig_str);
    while (((rc == SA_AIS_ERR_TRY_AGAIN) || (rc == SA_AIS_ERR_EXIST)) &&
           (msecs_waited < max_waiting_time_60s)) {
      usleep(sleep_delay_ms * 1000);
      msecs_waited += sleep_delay_ms;
      rc = saImmOiClassImplementerSet(*immOiHandle, streamConfig_str);
    }
    if (rc == SA_AIS_ERR_BAD_HANDLE || rc == SA_AIS_ERR_TIMEOUT) {
      LOG_WA("saImmOiClassImplementerSet returned %s", saf_error(rc));
      usleep(sleep_delay_ms * 1000);
      msecs_waited += sleep_delay_ms;
      saImmOiFinalize(*immOiHandle);
      *immOiHandle = 0;
      *immSelectionObject = -1;
      lgs_imm_init_OI_handle(immOiHandle, immSelectionObject);
      continue;
    }
    if (rc != SA_AIS_OK) {
      TRACE("saImmOiClassImplementerSet SaLogStreamConfig failed %s",
            saf_error(rc));
      goto done;
    }
    break;
  }

done:
  TRACE_LEAVE2("rc: %s", saf_error(rc));
  return rc;
}

/**
 * Set implementer name and become class implementer.
 * This function will block until done.
 *
 * @param cb
 */
void lgs_imm_impl_set(SaImmOiHandleT *immOiHandle,
                      SaSelectionObjectT *immSelectionObject) {
  SaAisErrorT rc = SA_AIS_OK;

  TRACE_ENTER();

  rc = imm_impl_set_sequence(immOiHandle, immSelectionObject);
  if (rc != SA_AIS_OK) {
    lgs_exit("Becoming OI implementer failed", SA_AMF_COMPONENT_RESTART);
  }

  TRACE_LEAVE();
}

/**
 * Thread
 * Restore object and class implementer/applier.
 *
 * @param _cb[in]
 */
static void *imm_impl_init_thread(void *_cb) {
  lgs_cb_t *cb = static_cast<lgs_cb_t *>(_cb);
  SaSelectionObjectT immSelectionObject = 0;
  SaImmOiHandleT immOiHandle = 0;
  SaAisErrorT rc = SA_AIS_OK;

  TRACE_ENTER();

  /* Initialize handles and become implementer */
  lgs_imm_init_OI_handle(&immOiHandle, &immSelectionObject);
  rc = imm_impl_set_sequence(&immOiHandle, &immSelectionObject);
  if (rc != SA_AIS_OK) {
    lgs_exit("Becoming OI implementer failed", SA_AMF_COMPONENT_RESTART);
  }

  /* Store handle and selection object.
   * Protect if the poll loop in main is released during the storage
   * sequence.
   */
  osaf_mutex_lock_ordie(&lgs_OI_init_mutex);
  cb->immSelectionObject = immSelectionObject;
  cb->immOiHandle = immOiHandle;
  osaf_mutex_unlock_ordie(&lgs_OI_init_mutex);

  /* Activate the poll loop in main()
   * This will reinstall IMM poll event handling
   */
  lgsv_lgs_evt_t *lgsv_evt;
  lgsv_evt = static_cast<lgsv_lgs_evt_t *>(calloc(1, sizeof(lgsv_lgs_evt_t)));
  osafassert(lgsv_evt);
  lgsv_evt->evt_type = LGSV_EVT_NO_OP;
  if (m_NCS_IPC_SEND(&lgs_mbx, lgsv_evt, LGS_IPC_PRIO_CTRL_MSGS) !=
      NCSCC_RC_SUCCESS) {
    LOG_WA("imm_reinit_thread failed to send IPC message to main thread");
    /*
     * Se no reason why this would happen. But if it does at least there
     * is something in the syslog. The main thread should still pick up
     * the new imm FD when there is a healthcheck, but it could take
     *minutes.
     */
    free(lgsv_evt);
  }

  TRACE_LEAVE();
  return NULL;
}

/**
 * In a separate thread:
 * Initiate IMM OI handle and selection object
 * Create imm implementer for log IMM objects
 * When complete:
 * Store the new handle and selection object in the cb store.
 * Activate the poll loop in main() by sending an empty mailbox message
 *
 * @param cb[out]
 */
void lgs_imm_impl_reinit_nonblocking(lgs_cb_t *cb) {
  pthread_t thread;
  pthread_attr_t attr;

  TRACE_ENTER();

  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

  if (pthread_create(&thread, &attr, imm_impl_init_thread, cb) != 0) {
    LOG_ER("pthread_create FAILED: %s", strerror(errno));
    exit(EXIT_FAILURE);
  }

  pthread_attr_destroy(&attr);

  TRACE_LEAVE();
}

/******************************************************************************
 * Functions used for recovery handling
 ******************************************************************************/
/**
 * Search for old runtime stream objects and put their names in a list.
 * Become OI for the runtime stream class if any objects found
 *
 * @param object_list
 */
void lgs_search_stream_objects() {
  int rc = 0;
  SaAisErrorT ais_rc = SA_AIS_OK;
  SaImmHandleT immOmHandle;
  SaImmSearchHandleT immSearchHandle;
  const char *class_name = "SaLogStream";

  TRACE_ENTER();

  /* Intialize Om API
   */
  ais_rc = immutil_saImmOmInitialize(&immOmHandle, NULL, &immVersion);
  if (ais_rc != SA_AIS_OK) {
    LOG_WA("%s saImmOmInitialize FAIL %d", __FUNCTION__, ais_rc);
    goto done;
  }

  /* Initialize search for application stream runtime objects
   * Search for all objects of class 'SaLogStream'
   * Search criteria:
   * Attribute SaImmAttrClassName == SaLogStream
   */
  SaImmSearchParametersT_2 searchParam;
  searchParam.searchOneAttr.attrName = const_cast<char *>("SaImmAttrClassName");
  searchParam.searchOneAttr.attrValueType = SA_IMM_ATTR_SASTRINGT;
  searchParam.searchOneAttr.attrValue = &class_name;

  ais_rc = immutil_saImmOmSearchInitialize_2(
      immOmHandle, NULL, SA_IMM_SUBTREE,
      SA_IMM_SEARCH_ONE_ATTR | SA_IMM_SEARCH_GET_NO_ATTR, &searchParam, NULL,
      &immSearchHandle);
  if (ais_rc != SA_AIS_OK) {
    LOG_WA("%s saImmOmSearchInitialize FAIL %d", __FUNCTION__, ais_rc);
    goto done_fin_Om;
  }

  /* Iterate the search and save objects in list until all objects found
   */
  SaNameT object_name;
  SaImmAttrValuesT_2 **attributes;

  ais_rc =
      immutil_saImmOmSearchNext_2(immSearchHandle, &object_name, &attributes);
  if (ais_rc == SA_AIS_ERR_NOT_EXIST) {
    TRACE("\tNo objects found %s", saf_error(ais_rc));
  }

  while (ais_rc == SA_AIS_OK) {
    SaConstStringT objName = osaf_extended_name_borrow(&object_name);
    TRACE("\tFound object \"%s\"", objName);
    /* Add the string to the list
     */
    rc = log_rtobj_list_add(objName);
    if (rc == -1) {
      TRACE("%s Could not add %s to list Fail", __FUNCTION__, objName);
    }

    /* Get next object */
    ais_rc =
        immutil_saImmOmSearchNext_2(immSearchHandle, &object_name, &attributes);
  }
  if (ais_rc == SA_AIS_ERR_NOT_EXIST) {
    TRACE("\tAll objects found OK");
  } else {
    LOG_WA("\t%s saImmOmSearchNext FAILED %d", __FUNCTION__, ais_rc);
  }

  /* Finalize the serach API
   */
  ais_rc = immutil_saImmOmSearchFinalize(immSearchHandle);
  if (ais_rc != SA_AIS_OK) {
    TRACE("\tsaImmOmSearchFinalize FAIL %d", ais_rc);
  }

done_fin_Om:
  /* Finalize the Om API
   */
  ais_rc = immutil_saImmOmFinalize(immOmHandle);
  if (ais_rc != SA_AIS_OK) {
    TRACE("\tsaImmOmFinalize FAIL %d", ais_rc);
  }

done:
  TRACE_LEAVE2("rc: %s", saf_error(ais_rc));
}

void lgs_delete_one_stream_object(const std::string &name_str) {
  SaAisErrorT ais_rc = SA_AIS_OK;
  SaNameT object_name;

  if (name_str.empty() == true) {
    TRACE("%s No object name given", __FUNCTION__);
    return;
  }

  /* Copy name to a SaNameT */
  osaf_extended_name_lend(name_str.c_str(), &object_name);

  /* and delete the object */
  ais_rc = immutil_saImmOiRtObjectDelete(lgs_cb->immOiHandle, &object_name);
  if (ais_rc == SA_AIS_OK) {
    TRACE("%s Object \"%s\" deleted", __FUNCTION__, name_str.c_str());
  } else {
    LOG_WA("%s saImmOiRtObjectDelete for \"%s\" FAILED %d", __FUNCTION__,
           name_str.c_str(), ais_rc);
  }
}

/**
 * Delete all stream objects in the stream object list.
 * And do appending close time to cfg/log files of the streams.
 * See also: lgs_search_stream_objects()
 */
void lgs_cleanup_abandoned_streams() {
  SaAisErrorT ais_rc = SA_AIS_OK;
  int pos = 0;
  SaNameT object_name;

  TRACE_ENTER();
  /* Check if there are any objects to delete. If not do nothing  */
  if (log_rtobj_list_no() <= 0) {
    TRACE("%s\t No objects to delete is found", __FUNCTION__);
    return;
  }

  pos = log_rtobj_list_getnamepos();
  while (pos != -1) {
    /* Get found name */
    char *name_str = log_rtobj_list_getname(pos);

    /* Append the close time to log file and configuration file */
    (void)log_close_rtstream_files(name_str);

    /* Delete the object if in object list. Note this is an Oi operation
     * Remove name from list
     */
    if (name_str != NULL) {
      /* Copy name to a SaNameT */
      osaf_extended_name_lend(name_str, &object_name);
      /* and delete the object */
      ais_rc = immutil_saImmOiRtObjectDelete(lgs_cb->immOiHandle, &object_name);
      if (ais_rc == SA_AIS_OK) {
        TRACE("\tObject \"%s\" deleted", name_str);
      } else {
        LOG_WA("%s saImmOiRtObjectDelete for \"%s\" FAILED %d", __FUNCTION__,
               name_str, ais_rc);
      }
    } else {
      /* Should never happen! */
      TRACE("%s\tFound name has NULL pointer!", __FUNCTION__);
    }
    /* Remove deleted object name from list */
    log_rtobj_list_erase_one_pos(pos);

    /* Get next object */
    pos = log_rtobj_list_getnamepos();
  }

  TRACE_LEAVE();
}

/**
 * Get cached stream attributes for given object name
 *
 * @param attrib_out[out] Pointer to a NULL terminated SaImmAttrValuesT_2 vector
 * @param object_name[in]  String containing the object name
 * @param immOmHandle[out]
 * @return -1 on error
 */
int lgs_get_streamobj_attr(SaImmAttrValuesT_2 ***attrib_out,
                           const std::string &object_name_in,
                           SaImmHandleT *immOmHandle) {
  int rc = 0;
  SaAisErrorT ais_rc = SA_AIS_OK;
  SaImmAccessorHandleT accessorHandle;
  char *attribute_names[] = {
      const_cast<char *>("saLogStreamFileName"),
      const_cast<char *>("saLogStreamPathName"),
      const_cast<char *>("saLogStreamMaxLogFileSize"),
      const_cast<char *>("saLogStreamFixedLogRecordSize"),
      const_cast<char *>("saLogStreamHaProperty"),
      const_cast<char *>("saLogStreamLogFullAction"),
      const_cast<char *>("saLogStreamMaxFilesRotated"),
      const_cast<char *>("saLogStreamLogFileFormat"),
      const_cast<char *>("saLogStreamSeverityFilter"),
      const_cast<char *>("saLogStreamCreationTimestamp"),
      NULL};

  TRACE_ENTER2("object_name_in \"%s\"", object_name_in.c_str());

  SaNameT object_name;
  if (object_name_in.empty() == true) {
    TRACE("%s No object name given (NULL)", __FUNCTION__);
    rc = -1;
    goto done;
  }

  /* Initialize Om API
   */
  ais_rc = immutil_saImmOmInitialize(immOmHandle, NULL, &immVersion);
  if (ais_rc != SA_AIS_OK) {
    LOG_WA("\t%s saImmOmInitialize FAIL %d", __FUNCTION__, ais_rc);
    rc = -1;
    goto done;
  }

  /* Initialize accessor for reading attributes
   */
  ais_rc = immutil_saImmOmAccessorInitialize(*immOmHandle, &accessorHandle);
  if (ais_rc != SA_AIS_OK) {
    TRACE("%s\t saImmOmAccessorInitialize Fail '%s'", __FUNCTION__,
          saf_error(ais_rc));
    rc = -1;
    goto done;
  }

  osaf_extended_name_lend(object_name_in.c_str(), &object_name);

  ais_rc = immutil_saImmOmAccessorGet_2(accessorHandle, &object_name,
                                        attribute_names, attrib_out);
  if (ais_rc != SA_AIS_OK) {
    TRACE("%s\t saImmOmAccessorGet_2 Fail '%s'", __FUNCTION__,
          saf_error(ais_rc));
    rc = -1;
    goto done_fin_Om;
  } else {
    goto done;
  }

done_fin_Om:
  /* Free resources if fail */
  ais_rc = immutil_saImmOmFinalize(*immOmHandle);
  if (ais_rc != SA_AIS_OK) {
    TRACE("%s\t saImmOmFinalize Fail '%s'", __FUNCTION__, saf_error(ais_rc));
  }

done:
  TRACE_LEAVE();
  return rc;
}

/**
 * Free resources claimed when calling lgs_get_streamobj_attr()
 *
 * @param immHandle[in]
 * @return -1 on error
 */
int lgs_free_streamobj_attr(SaImmHandleT immOmHandle) {
  int rc = 0;
  SaAisErrorT ais_rc = SA_AIS_OK;

  TRACE_ENTER();

  ais_rc = immutil_saImmOmFinalize(immOmHandle);
  if (ais_rc != SA_AIS_OK) {
    TRACE("%s\t saImmOmFinalize Fail '%s'", __FUNCTION__, saf_error(ais_rc));
    rc = -1;
  }

  TRACE_LEAVE();
  return rc;
}

/**
 * Read the scAbsenceAllowed attribute in
 * opensafImm=opensafImm,safApp=safImmService object.
 * No value means that "absence handling" is not allowed. For Log this means
 * that no recovery shall be done.
 *
 * @param attr_val[out] Value read from scAbsenceAllowed attribute. 0 = No value
 * @return Pointer to attr_val if value is read. NULL if no value. If NULL the
 *         value in attr_val is not valid
 */
SaUint32T *lgs_get_scAbsenceAllowed_attr(SaUint32T *attr_val) {
  SaUint32T *rc_attr_val = NULL;
  SaAisErrorT ais_rc = SA_AIS_OK;
  SaImmAccessorHandleT accessorHandle;
  SaImmHandleT immOmHandle;
  SaImmAttrValuesT_2 *attribute;
  SaImmAttrValuesT_2 **attributes;

  TRACE_ENTER();
  char *attribute_names[] = {const_cast<char *>("scAbsenceAllowed"), NULL};
  std::string object_name_str = "opensafImm=opensafImm,safApp=safImmService";

  SaNameT object_name;
  osaf_extended_name_lend(object_name_str.c_str(), &object_name);

  /* Default restore handling shall be disabled. Is enabled if the
   * scAbsenceAllowed attribute is not empty
   */
  *attr_val = 0;

  /* Initialize Om API
   */
  ais_rc = immutil_saImmOmInitialize(&immOmHandle, NULL, &immVersion);
  if (ais_rc != SA_AIS_OK) {
    LOG_WA("\t%s saImmOmInitialize FAIL %d", __FUNCTION__, ais_rc);
    goto done;
  }

  /* Initialize accessor for reading attributes
   */
  ais_rc = immutil_saImmOmAccessorInitialize(immOmHandle, &accessorHandle);
  if (ais_rc != SA_AIS_OK) {
    TRACE("%s\t saImmOmAccessorInitialize Fail '%s'", __FUNCTION__,
          saf_error(ais_rc));
    goto done;
  }

  ais_rc = immutil_saImmOmAccessorGet_2(accessorHandle, &object_name,
                                        attribute_names, &attributes);
  if (ais_rc != SA_AIS_OK) {
    TRACE("%s\t saImmOmAccessorGet_2 Fail '%s'", __FUNCTION__,
          saf_error(ais_rc));
    goto done_fin_Om;
  }

  /* Handle the global scAbsenceAllowed_flag */
  attribute = attributes[0];
  if (attribute != NULL) {
    TRACE("%s\t attrName \"%s\"", __FUNCTION__, attribute->attrName);
    if (attribute->attrValuesNumber != 0) {
      /* scAbsenceAllowed has value. Get the value */
      void *value = attribute->attrValues[0];
      *attr_val = *(static_cast<SaUint32T *>(value));
      rc_attr_val = attr_val;
    }
  }

done_fin_Om:
  /* Free Om resources */
  ais_rc = immutil_saImmOmFinalize(immOmHandle);
  if (ais_rc != SA_AIS_OK) {
    TRACE("%s\t saImmOmFinalize Fail '%s'", __FUNCTION__, saf_error(ais_rc));
  }

done:
  TRACE_LEAVE();
  return rc_attr_val;
}
