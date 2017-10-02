/*      -*- OpenSAF  -*-
 *
 * Copyright Ericsson AB 2017 - All Rights Reserved.
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
#include "log/agent/lga_stream.h"
#include <assert.h>
#include <string.h>
#include "base/ncs_hdl_pub.h"
#include "log/common/lgsv_msg.h"
#include "log/common/lgsv_defs.h"
#include "base/logtrace.h"
#include "base/time.h"
#include "base/osaf_extended_name.h"
#include "log/agent/lga_mds.h"
#include "base/saf_error.h"
#include "mds/mds_papi.h"

LogStreamInfo::LogStreamInfo(const std::string& name, uint32_t id)
    : stream_name_{name} {
  TRACE_ENTER();
  stream_id_ = id;
  recovered_flag_ = true;

  // Initialize @handle_ for this log stream
  if ((handle_ = ncshm_create_hdl(NCS_HM_POOL_ID_COMMON, NCS_SERVICE_ID_LGA,
                                  this)) == 0) {
    assert(handle_ != 0 && "Creating handle failed");
  }
}

LogStreamInfo::~LogStreamInfo() {
  TRACE_ENTER();
  if (handle_ != 0) {
    ncshm_give_hdl(handle_);
    ncshm_destroy_hdl(NCS_SERVICE_ID_LGA, handle_);
    handle_ = 0;
  }
}

bool LogStreamInfo::SendOpenStreamMsg(uint32_t client_id) {
  TRACE_ENTER();
  SaAisErrorT ais_rc = SA_AIS_OK;
  uint32_t ncs_rc = NCSCC_RC_SUCCESS;
  lgsv_msg_t i_msg, *o_msg;
  lgsv_stream_open_req_t* open_param;

  //>
  // Populate a stream open message to the Log server
  //<
  memset(&i_msg, 0, sizeof(lgsv_msg_t));
  open_param = &i_msg.info.api_info.param.lstr_open_sync;

  // Set the open parameters to open a stream for recovery
  osaf_extended_name_lend(stream_name_.c_str(), &open_param->lstr_name);
  open_param->client_id = client_id;
  open_param->logFileFmt = nullptr;
  open_param->logFileFmtLength = 0;
  open_param->maxLogFileSize = 0;
  open_param->maxLogRecordSize = 0;
  open_param->haProperty = SA_FALSE;
  open_param->logFileFullAction = SA_LOG_FULL_ACTION_BEGIN;
  open_param->maxFilesRotated = 0;
  open_param->lstr_open_flags = 0;

  i_msg.type = LGSV_LGA_API_MSG;
  i_msg.info.api_info.type = LGSV_STREAM_OPEN_REQ;

  //>
  // Send a message to LGS to obtain a stream_id
  //<
  // Default wait time for retries (30s)
  const uint64_t kWaitTime = 30 * 1000;
  base::Timer wtime(kWaitTime);
  while (wtime.is_timeout() == false) {
    ncs_rc = lga_mds_msg_sync_send(&i_msg, &o_msg, LGS_WAIT_TIME,
                                   MDS_SEND_PRIORITY_HIGH);
    if (ncs_rc != NCSCC_RC_SUCCESS) {
      LOG_NO("%s lga_mds_msg_sync_send() fail %d", __func__, ncs_rc);
      return false;
    }

    ais_rc = o_msg->info.api_resp_info.rc;
    if (ais_rc == SA_AIS_ERR_TRY_AGAIN) {
      base::Sleep(base::kOneHundredMilliseconds);
      continue;
    }
    break;
  }
  if (SA_AIS_OK != ais_rc) {
    TRACE("%s LGS error response %s", __func__, saf_error(ais_rc));
    lga_msg_destroy(o_msg);
    return false;
  }

  // Open succeeded
  stream_id_ = o_msg->info.api_resp_info.param.lstr_open_rsp.lstr_id;
  lga_msg_destroy(o_msg);

  return true;
}

// Recover log stream for client @client_id
bool LogStreamInfo::RecoverMeForClientId(uint32_t client_id) {
  TRACE_ENTER();
  if (recovered_flag_ == true) return true;
  if (SendOpenStreamMsg(client_id) == false) return false;
  recovered_flag_ = true;
  return true;
}
