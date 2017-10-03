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

/* ========================================================================
 *   INCLUDE FILES
 * ========================================================================
 */
#include <sys/poll.h>

#include "base/osaf_utility.h"
#include <saAis.h>
#include <saLog.h>
#include "ntf/ntfd/NtfAdmin.h"
#include "ntf/ntfd/NtfLogger.h"
#include "ntfs_com.h"
#include "base/logtrace.h"
#include "ntf/common/ntfsv_mem.h"
#include "base/osaf_extended_name.h"

/* ========================================================================
 *   DEFINITIONS
 * ========================================================================
 */
#define LOG_OPEN_TIMEOUT SA_TIME_ONE_SECOND
#define LOG_NOTIFICATION_TIMEOUT SA_TIME_ONE_SECOND
#define AIS_TIMEOUT 500000

/* ========================================================================
 *   TYPE DEFINITIONS
 * ========================================================================
 */

/* ========================================================================
 *   FUNCTION PROTOTYPES
 * ========================================================================
 */
/* Callbacks */
void saLogFilterSetCallback(SaLogStreamHandleT logStreamHandle,
                            SaLogSeverityFlagsT logSeverity);

void saLogStreamOpenCallback(SaInvocationT invocation,
                             SaLogStreamHandleT logStreamHandle,
                             SaAisErrorT error);

void saLogWriteLogCallback(SaInvocationT invocation, SaAisErrorT error);

/* ========================================================================
 *   DATA DECLARATIONS
 * ========================================================================
 */

static SaLogCallbacksT logCallbacks = {NULL, NULL, saLogWriteLogCallback};

static SaLogHandleT logHandle;
static SaLogStreamHandleT alarmStreamHandle;
const SaVersionT kLogVersion = {'A', 2, 3};

NtfLogger::NtfLogger() : readCounter(0) {
  if (SA_AIS_OK != initLog()) {
    LOG_ER("initialize saflog failed exiting...");
    exit(EXIT_FAILURE);
  }
}

/* Callbacks */
void saLogFilterSetCallback(SaLogStreamHandleT logStreamHandle,
                            SaLogSeverityFlagsT logSeverity) {
  LOG_IN("Ignoring log filter set callback");
}

void saLogStreamOpenCallback(SaInvocationT invocation,
                             SaLogStreamHandleT logStreamHandle,
                             SaAisErrorT error) {
  LOG_IN("Ignoring log stream open callback");
}

void saLogWriteLogCallback(SaInvocationT invocation, SaAisErrorT error) {
  TRACE_ENTER2("Callback for notificationId %llu", invocation);

  if (SA_AIS_OK != error) {
    NtfSmartPtr notification;

    TRACE_1("Error when logging (%d), queue for relogging", error);

    notification = NtfAdmin::theNtfAdmin->getNotificationById(
        (SaNtfIdentifierT)invocation);

    osafassert(notification != NULL);

    if (!notification->loggedOk()) {
      NtfAdmin::theNtfAdmin->logger.queueNotifcation(notification);
      TRACE_LEAVE();
      return;
    } else {
      LOG_ER("Already marked as logged notificationId: %d", (int)invocation);
      /* this should not happen */
      osafassert(0);
    }
  }
  sendLoggedConfirm((SaNtfIdentifierT)invocation);
  TRACE_LEAVE();
}

void NtfLogger::log(NtfSmartPtr& notif, bool isLocal) {
  unsigned int collSize = (unsigned int)coll_.size();
  TRACE_ENTER();
  TRACE_2("notification Id=%llu received in logger with size %d",
          notif->getNotificationId(), collSize);

  if (isLocal) {
    TRACE_2("IS LOCAL, logging");
    /* Currently only alarm notifations are logged */
    this->checkQueueAndLog(notif);
  }

  if ((notif->sendNotInfo_->notificationType == SA_NTF_TYPE_ALARM) ||
      (notif->sendNotInfo_->notificationType == SA_NTF_TYPE_SECURITY_ALARM)) {
    TRACE_2("template queue handling...");
    if (coll_.size() < ntfs_cb->cache_size) {
      TRACE_2("push_back");
      coll_.push_back(notif);
    } else {
      TRACE_2("pop_front");
      coll_.pop_front();
      TRACE_2("push_back");
      coll_.push_back(notif);
    }
  }
  TRACE_LEAVE();
}

void NtfLogger::queueNotifcation(NtfSmartPtr& notif) {
  TRACE_2("Queue notification: %llu", notif->getNotificationId());
  queuedNotificationList.push_back(notif);
}

void NtfLogger::checkQueueAndLog(NtfSmartPtr& newNotif) {
  TRACE_ENTER();
  /* Check if there are not logged notifications in queue */
  while (!queuedNotificationList.empty()) {
    NtfSmartPtr notification = queuedNotificationList.front();
    queuedNotificationList.pop_front();
    TRACE_2("Log queued notification: %llu", notification->getNotificationId());
    if (SA_AIS_OK != this->logNotification(notification)) {
      TRACE_2("Push back queued notification: %llu",
              notification->getNotificationId());
      queuedNotificationList.push_front(notification); /* keep order */
      queueNotifcation(newNotif);
      TRACE_LEAVE();
      return;
    }
  }

  if (SA_AIS_OK != this->logNotification(newNotif)) {
    queueNotifcation(newNotif);
  }
  TRACE_LEAVE();
}

SaAisErrorT NtfLogger::logNotification(NtfSmartPtr& notif) {
  /* Write to the log if we're the local node */
  SaAisErrorT errorCode = SA_AIS_OK;
  SaLogHeaderT logHeader;
  char addTextBuf[MAX_ADDITIONAL_TEXT_LENGTH] = {0};
  SaLogBufferT logBuffer;
  ntfsv_send_not_req_t* sendNotInfo;
  SaNtfNotificationHeaderT* ntfHeader;
  TRACE_ENTER();

  sendNotInfo = notif->getNotInfo();
  ntfsv_get_ntf_header(sendNotInfo, &ntfHeader);

  // Workaround to fix with failed upgrade
  // Auto correction if there is inconsistent b/w lengthAdditionalText and
  // additionalText
  // TODO: This workaround should be removed when Opensaf 5.0 is no longer
  // supported
  if (ntfHeader->additionalText != NULL &&
      ntfHeader->lengthAdditionalText > strlen(ntfHeader->additionalText) + 1) {
    LOG_WA("Mismatch b/w lengthAdditionalText and additionalText. Re-Adjust!");
    ntfHeader->lengthAdditionalText = strlen(ntfHeader->additionalText) + 1;
  }

  logBuffer.logBufSize = ntfHeader->lengthAdditionalText;
  logBuffer.logBuf = (SaUint8T*)&addTextBuf[0];

  if (MAX_ADDITIONAL_TEXT_LENGTH < ntfHeader->lengthAdditionalText) {
    logBuffer.logBufSize = MAX_ADDITIONAL_TEXT_LENGTH;
  }
  (void)strncpy(addTextBuf, (SaStringT)ntfHeader->additionalText,
                logBuffer.logBufSize);

  SaLogNtfLogHeaderT ntfLogHeader = {
      *ntfHeader->notificationId,     *ntfHeader->eventType,
      ntfHeader->notificationObject,  ntfHeader->notifyingObject,
      ntfHeader->notificationClassId, *ntfHeader->eventTime};

  logHeader.ntfHdr = ntfLogHeader;

  SaLogRecordT logRecord = {*ntfHeader->eventTime, SA_LOG_NTF_HEADER, logHeader,
                            &logBuffer};

  /* Also write alarms and security alarms to the alarm log */
  if ((notif->sendNotInfo_->notificationType == SA_NTF_TYPE_ALARM) ||
      (notif->sendNotInfo_->notificationType == SA_NTF_TYPE_SECURITY_ALARM)) {
    TRACE_2("Logging notification to alarm stream");
    errorCode =
        saLogWriteLogAsync(alarmStreamHandle, notif->getNotificationId(),
                           SA_LOG_RECORD_WRITE_ACK, &logRecord);
    switch (errorCode) {
      case SA_AIS_OK:
        break;

      /* LOGsv is busy. Put the notification to queue and re-send next time */
      case SA_AIS_ERR_TRY_AGAIN:
      case SA_AIS_ERR_TIMEOUT:
        TRACE("Failed to log notification (ret: %d). Try next time.",
              errorCode);
        break;

      default:
        osaf_abort(errorCode);
    }
  }

  TRACE_LEAVE();
  return errorCode;
}

SaAisErrorT NtfLogger::initLog() {
  SaAisErrorT result;
  SaNameT alarmStreamName;
  osaf_extended_name_lend(SA_LOG_STREAM_ALARM, &alarmStreamName);
  int first_try = 1;
  SaVersionT log_version = kLogVersion;

  TRACE_ENTER();

  /* Initialize the Log service */
  do {
    result = saLogInitialize(&logHandle, &logCallbacks, &log_version);
    if (SA_AIS_ERR_TRY_AGAIN == result) {
      if (first_try) {
        LOG_WA("saLogInitialize returns try again, retries...");
        first_try = 0;
      }
      usleep(AIS_TIMEOUT);
      log_version = kLogVersion;
    }
  } while (SA_AIS_ERR_TRY_AGAIN == result);

  if (SA_AIS_OK != result) {
    LOG_ER("Log initialize result is %d", result);
    goto exit_point;
  }
  if (!first_try) {
    LOG_IN("saLogInitialize ok");
    first_try = 1;
  }

  /* Get file descriptor to use in select */
  do {
    result = saLogSelectionObjectGet(logHandle, &ntfs_cb->logSelectionObject);
    if (SA_AIS_ERR_TRY_AGAIN == result) {
      if (first_try) {
        LOG_WA("saLogSelectionObjectGet returns try again, retries...");
        first_try = 0;
      }
      usleep(AIS_TIMEOUT);
    }
  } while (SA_AIS_ERR_TRY_AGAIN == result);

  if (SA_AIS_OK != result) {
    LOG_ER("Log SelectionObjectGet result is %d", result);
    goto exit_point;
  }

  if (SA_AIS_OK != result) {
    LOG_ER("Failed to open the notification log stream (%d)", result);
    goto exit_point;
  }
  if (!first_try) {
    LOG_IN("saLogSelectionObjectGet ok");
    first_try = 1;
  }

  /* Open the alarm stream */
  do {
    result = saLogStreamOpen_2(logHandle, &alarmStreamName, NULL, 0,
                               LOG_OPEN_TIMEOUT, &alarmStreamHandle);
    if (SA_AIS_ERR_TRY_AGAIN == result) {
      if (first_try) {
        LOG_WA("saLogStreamOpen_2 returns try again, retries...");
        first_try = 0;
      }
      usleep(AIS_TIMEOUT);
    }
  } while (SA_AIS_ERR_TRY_AGAIN == result);

  if (SA_AIS_OK != result) {
    LOG_ER("Failed to open the alarm log stream (%d)", result);
    goto exit_point;
  }
  if (!first_try) {
    LOG_IN("saLogStreamOpen_2 ok");
    first_try = 1;
  }

exit_point:
  TRACE_LEAVE();
  return (result);
}

void logEvent() {
  SaAisErrorT errorCode;
  errorCode = saLogDispatch(logHandle, SA_DISPATCH_ALL);
  if (SA_AIS_OK != errorCode) {
    TRACE_1("Failed to dispatch log events (%d)", errorCode);
  }
  return;
}

void NtfLogger::printInfo() {
  TRACE("Logger Information:");
  TRACE(" logQueueList size:  %u", (unsigned int)queuedNotificationList.size());
  TRACE(" reader cache size:  %u", (unsigned int)coll_.size());
}
