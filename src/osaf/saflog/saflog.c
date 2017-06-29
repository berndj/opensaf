/*      -*- OpenSAF  -*-
 *
 * (C) Copyright 2010 The OpenSAF Foundation
 * (C) Copyright Ericsson AB 2010, 2017 - All Rights Reserved.
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
 * Author(s): Ericsson
 *
 */

#include <stdio.h>
#include <syslog.h>
#include <stdarg.h>
#include <unistd.h>
#include "osaf/saflog/saflog.h"
#include <saAis.h>

static int initialized;
static SaLogStreamHandleT logStreamHandle;
static SaLogHandleT logHandle;

void saflog_init(void)
{
	SaAisErrorT error;

	if (!initialized) {
		SaVersionT logVersion = {'A', 2, 3};
		SaNameT stream_name;
		saAisNameLend(SA_LOG_STREAM_SYSTEM, &stream_name);

		error = saLogInitialize(&logHandle, NULL, &logVersion);
		if (error != SA_AIS_OK) {
			syslog(LOG_INFO,
			       "saflogInit: saLogInitialize FAILED: %u", error);
			return;
		}

		error = saLogStreamOpen_2(logHandle, &stream_name, NULL, 0,
					  SA_TIME_ONE_SECOND, &logStreamHandle);
		if (error != SA_AIS_OK) {
			syslog(LOG_INFO,
			       "saflogInit: saLogStreamOpen_2 FAILED: %u",
			       error);
			if (saLogFinalize(logHandle) != SA_AIS_OK)
				syslog(LOG_INFO,
				       "saflogInit: saLogFinalize FAILED: %u",
				       error);
			return;
		}
		initialized = 1;
	}
}

void saflog(int priority, const SaNameT *logSvcUsrName, const char *format, ...)
{
	SaAisErrorT error;
	SaLogRecordT logRecord;
	SaLogBufferT logBuffer;
	va_list ap;
	char str[SA_LOG_MAX_RECORD_SIZE + 1];
	/* SA_LOG_MAX_RECORD_SIZE = 65535
	 * This will allow logging of record containing long dns */

	va_start(ap, format);
	logBuffer.logBufSize = vsnprintf(str, sizeof(str), format, ap);
	va_end(ap);

	if (logBuffer.logBufSize > SA_LOG_MAX_RECORD_SIZE) {
		syslog(LOG_INFO,
		       "saflog write FAILED: log record size > %u max limit",
		       SA_LOG_MAX_RECORD_SIZE);
		return;
	}

	if (!initialized) {
		SaVersionT logVersion = {'A', 2, 3};
		SaNameT stream_name;
		saAisNameLend(SA_LOG_STREAM_SYSTEM, &stream_name);

		error = saLogInitialize(&logHandle, NULL, &logVersion);
		if (error != SA_AIS_OK) {
			syslog(LOG_INFO, "saLogInitialize FAILED: %u", error);
			goto done;
		}

		error = saLogStreamOpen_2(logHandle, &stream_name, NULL, 0,
					  SA_TIME_ONE_SECOND, &logStreamHandle);
		if (error != SA_AIS_OK) {
			syslog(LOG_INFO, "saLogStreamOpen_2 FAILED: %u", error);
			if (saLogFinalize(logHandle) != SA_AIS_OK)
				syslog(LOG_INFO, "saLogFinalize FAILED: %u",
				       error);
			goto done;
		}
		initialized = 1;
	}

	logRecord.logTimeStamp = SA_TIME_UNKNOWN;
	logRecord.logHdrType = SA_LOG_GENERIC_HEADER;
	logRecord.logHeader.genericHdr.notificationClassId = NULL;
	logRecord.logHeader.genericHdr.logSeverity = priority;
	logRecord.logHeader.genericHdr.logSvcUsrName = logSvcUsrName;
	logRecord.logBuffer = &logBuffer;
	logBuffer.logBuf = (SaUint8T *)str;

	error = saLogWriteLogAsync(logStreamHandle, 0, 0, &logRecord);

done:
	/* fallback to syslog at ANY error, syslog prio same as saflog severity
	 */
	if (error != SA_AIS_OK)
		syslog(priority, "%s", str);
}
