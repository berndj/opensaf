
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

/*
 * This file contains a command line utility to write a single log record
 * to the SAF LOG.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <fcntl.h>
#include <ctype.h>
#include <time.h>
#include <poll.h>
#include <unistd.h>
#include <limits.h>
#include <stdbool.h>
#include "base/osaf_extended_name.h"

#include <saAis.h>
#include <saLog.h>

#include "base/saf_error.h"

#define DEFAULT_FORMAT_EXPRESSION "@Cr @Ch:@Cn:@Cs @Cm/@Cd/@CY @Sv @Sl \"@Cb\""
#define DEFAULT_APP_LOG_REC_SIZE 150
#define DEFAULT_APP_LOG_FILE_SIZE 1024
#define VENDOR_ID 193
#define DEFAULT_MAX_FILES_ROTATED 4
/* Try for 10 seconds before giving up on an API */
#define TEN_SECONDS 10 * 1000 * 1000
/* Sleep for 100 ms before retrying an API */
#define HUNDRED_MS 100 * 1000

static void logWriteLogCallbackT(SaInvocationT invocation, SaAisErrorT error);

static SaLogCallbacksT logCallbacks = {0, 0, logWriteLogCallbackT};

static char *progname = "saflogger";
static SaInvocationT cb_invocation;
static SaAisErrorT cb_error;

const SaVersionT kLogVersion = {'A', 2, 3};

static SaTimeT get_current_SaTime(void)
{
	struct timeval tv;
	SaTimeT ntfTime;

	gettimeofday(&tv, 0);

	ntfTime = ((unsigned)tv.tv_sec * 1000000000ULL) +
		  ((unsigned)tv.tv_usec * 1000ULL);

	return ntfTime;
}

/**
 *
 */
static void usage(void)
{
	printf("\nNAME\n");
	printf("\t%s - write log record to log stream\n", progname);

	printf("\nSYNOPSIS\n");
	printf("\t%s [options] [-f <FILENAME>] [message ...]\n", progname);

	printf("\nDESCRIPTION\n");
	printf(
	    "\t%s is a SAF LOG client used to write a log record into a specified log stream.\n",
	    progname);

	printf("\nOPTIONS\n");

	printf("\t-h or --help                   this help\n");
	printf("\t-l or --alarm                  write to alarm stream\n");
	printf(
	    "\t-n or --notification           write to notification stream\n");
	printf(
	    "\t-y or --system                 write to system stream (default)\n");
	printf(
	    "\t-a NAME or --application=NAME  write to application stream NAME (create it if not exist)\n");
	printf(
	    "\t-f FILENAME                    write log record to FILENAME\n");
	printf(
	    "\t-s SEV or --severity=SEV       use severity SEV, default INFO\n");
	printf(
	    "\t\tvalid severity names: emerg, alert, crit, error, warn, notice, info\n");
	printf("\nNOTES\n");
	printf("\t1) -f is only applicable for runtime app stream.\n");
	printf(
	    "\t1) <FILENAME> length must not be longer than 255 characters.\n");

	printf("\nEXAMPLES\n");
	printf("\tsaflogger -a safLgStr=Test \"Hello world\"\n");
	printf("\tsaflogger -a safLgStr=Test -f testLogFile \"Hello world\"\n");
	printf("\tsaflogger -s crit \"I am going down\"\n\n");
}

static void logWriteLogCallbackT(SaInvocationT invocation, SaAisErrorT error)
{
	cb_invocation = invocation;
	cb_error = error;
}

/**
 * Write log record to stream, wait for ack
 * @param logRecord
 * @param interval
 * @param write_count
 *
 * @return SaAisErrorT
 */
static SaAisErrorT write_log_record(SaLogHandleT logHandle,
				    SaLogStreamHandleT logStreamHandle,
				    SaSelectionObjectT selectionObject,
				    const SaLogRecordT *logRecord)
{
	SaAisErrorT errorCode;
	SaInvocationT invocation;
	int i = 0;
	struct pollfd fds[1];
	int ret;
	unsigned int wait_time = 0;

	i++;

	invocation = random();

retry:
	errorCode = saLogWriteLogAsync(logStreamHandle, invocation,
				       SA_LOG_RECORD_WRITE_ACK, logRecord);
	if (errorCode == SA_AIS_ERR_TRY_AGAIN && wait_time < TEN_SECONDS) {
		usleep(HUNDRED_MS);
		wait_time += HUNDRED_MS;
		goto retry;
	}

	if (errorCode != SA_AIS_OK) {
		if (wait_time)
			fprintf(stderr, "Waited for %u seconds.\n",
				wait_time / 1000000);
		fprintf(stderr, "saLogWriteLogAsync FAILED: %s\n",
			saf_error(errorCode));
		return errorCode;
	}

	fds[0].fd = (int)selectionObject;
	fds[0].events = POLLIN;

poll_retry:
	ret = poll(fds, 1, 20000);

	if (ret == EINTR)
		goto poll_retry;

	if (ret == -1) {
		fprintf(stderr, "poll FAILED: %u\n", ret);
		return SA_AIS_ERR_BAD_OPERATION;
	}

	if (ret == 0) {
		fprintf(stderr,
			"poll timeout, message %u was most likely lost\n", i);
		return SA_AIS_ERR_BAD_OPERATION;
	}

	errorCode = saLogDispatch(logHandle, SA_DISPATCH_ONE);
	if (errorCode != SA_AIS_OK) {
		fprintf(stderr, "saLogDispatch FAILED: %s\n",
			saf_error(errorCode));
		return errorCode;
	}

	if (cb_invocation != invocation) {
		fprintf(stderr,
			"logWriteLogCallbackT FAILED: wrong invocation\n");
		return SA_AIS_ERR_BAD_OPERATION;
	}

	if (cb_error == SA_AIS_ERR_TRY_AGAIN && wait_time < TEN_SECONDS) {
		usleep(HUNDRED_MS);
		wait_time += HUNDRED_MS;
		goto retry;
	}

	if (cb_error == SA_AIS_ERR_TIMEOUT) {
		usleep(HUNDRED_MS);
		fprintf(stderr, "got SA_AIS_ERR_TIMEOUT, retry\n");
		goto retry;
	}

	if (cb_error != SA_AIS_OK) {
		if (wait_time)
			fprintf(stderr, "Waited for %u seconds.\n",
				wait_time / 1000000);
		fprintf(stderr, "logWriteLogCallbackT FAILED: %s\n",
			saf_error(cb_error));
		return cb_error;
	}

	return errorCode;
}

static SaLogSeverityT get_severity(char *severity)
{
	if (strcmp(severity, "emerg") == 0)
		return SA_LOG_SEV_EMERGENCY;

	if (strcmp(severity, "alert") == 0)
		return SA_LOG_SEV_ALERT;

	if (strcmp(severity, "crit") == 0)
		return SA_LOG_SEV_CRITICAL;

	if (strcmp(severity, "error") == 0)
		return SA_LOG_SEV_ERROR;

	if (strcmp(severity, "warn") == 0)
		return SA_LOG_SEV_WARNING;

	if (strcmp(severity, "notice") == 0)
		return SA_LOG_SEV_NOTICE;

	if (strcmp(severity, "info") == 0)
		return SA_LOG_SEV_INFO;

	usage();
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	int c;
	SaNameT logStreamName;
	SaLogFileCreateAttributesT_2 *logFileCreateAttributes = NULL;
	SaLogFileCreateAttributesT_2 appLogFileCreateAttributes;
	SaLogStreamOpenFlagsT logStreamOpenFlags = 0;
	SaLogRecordT logRecord;
	SaNameT logSvcUsrName;
	SaLogBufferT logBuffer;
	SaNtfClassIdT notificationClassId = {1, 2, 3};
	struct option long_options[] = {
	    {"application", required_argument, 0, 'a'},
	    {"alarm", no_argument, 0, 'l'},
	    {"notification", no_argument, 0, 'n'},
	    {"system", no_argument, 0, 'y'},
	    {"severity", required_argument, 0, 's'},
	    {"help", no_argument, 0, 'h'},
	    {0, 0, 0, 0}};
	char hostname[_POSIX_HOST_NAME_MAX];
	SaAisErrorT error;
	SaLogHandleT logHandle;
	SaLogStreamHandleT logStreamHandle;
	SaSelectionObjectT selectionObject;
	unsigned int wait_time;
	bool is_appstream = false, f_opt = false;

	srandom(getpid());

	if (gethostname(hostname, _POSIX_HOST_NAME_MAX) == -1) {
		fprintf(stderr, "gethostname failed: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (setenv("SA_ENABLE_EXTENDED_NAMES", "1", 1) != 0) {
		fprintf(stderr, "Failed to enable Extended SaNameT");
		exit(EXIT_FAILURE);
	}

	/**
	 * osaf_extended_name_init() is added in case osaf_extended_* APIs
	 * are used before saLogInitialize().
	 */
	osaf_extended_name_init();

	char svcUserName[kOsafMaxDnLength];
	snprintf(svcUserName, sizeof(svcUserName), "%s.%u@%s", "saflogger",
		 getpid(), hostname);
	saAisNameLend(svcUserName, &logSvcUsrName);

	/* Setup default values */
	saAisNameLend(SA_LOG_STREAM_SYSTEM, &logStreamName);
	logRecord.logTimeStamp =
	    SA_TIME_UNKNOWN; /* LOG service should supply timestamp */
	logRecord.logHdrType = SA_LOG_GENERIC_HEADER;
	logRecord.logHeader.genericHdr.notificationClassId = NULL;
	logRecord.logHeader.genericHdr.logSeverity = SA_LOG_SEV_INFO;
	logRecord.logHeader.genericHdr.logSvcUsrName = &logSvcUsrName;
	logRecord.logBuffer = NULL;
	appLogFileCreateAttributes.logFilePathName = "saflogger";
	appLogFileCreateAttributes.maxLogFileSize = DEFAULT_APP_LOG_FILE_SIZE;
	appLogFileCreateAttributes.maxLogRecordSize = DEFAULT_APP_LOG_REC_SIZE;
	appLogFileCreateAttributes.haProperty = SA_TRUE;
	appLogFileCreateAttributes.logFileFullAction =
	    SA_LOG_FILE_FULL_ACTION_ROTATE;
	appLogFileCreateAttributes.maxFilesRotated = DEFAULT_MAX_FILES_ROTATED;
	/* Use built-in log file format in log server for app stream */
	appLogFileCreateAttributes.logFileFmt = NULL;
	appLogFileCreateAttributes.logFileName = NULL;

	while (1) {
		c = getopt_long(argc, argv, "?hlnya:s:f:", long_options, NULL);
		if (c == -1) {
			break;
		}
		switch (c) {
		case 'l':
			saAisNameLend(SA_LOG_STREAM_ALARM, &logStreamName);
			logRecord.logHdrType = SA_LOG_NTF_HEADER;
			break;
		case 'n':
			saAisNameLend(SA_LOG_STREAM_NOTIFICATION,
				      &logStreamName);
			logRecord.logHdrType = SA_LOG_NTF_HEADER;
			break;
		case 'y':
			saAisNameLend(SA_LOG_STREAM_SYSTEM, &logStreamName);
			break;
		case 'a':
			logFileCreateAttributes = &appLogFileCreateAttributes;
			logStreamOpenFlags = SA_LOG_STREAM_CREATE;

			char tmpDn[kOsafMaxDnLength + 8 + 1] = {0};
			if (strstr(optarg, "safLgStr"))
				strncpy(tmpDn, optarg, sizeof(tmpDn) - 1);
			else
				snprintf(tmpDn, sizeof(tmpDn), "safLgStr=%s",
					 optarg);

			if (strlen(tmpDn) > kOsafMaxDnLength) {
				fprintf(
				    stderr,
				    "Application stream DN is so long (%zu). Max: %d \n",
				    strlen(optarg), kOsafMaxDnLength);
				fprintf(stderr, "Shut down app. \n");
				exit(EXIT_FAILURE);
			}
			saAisNameLend(tmpDn, &logStreamName);
			is_appstream = true;
			if (f_opt == false)
				appLogFileCreateAttributes.logFileName =
				    strdup(optarg);
			break;
		case 'f':
			if (f_opt == true) {
				fprintf(stderr,
					"More than one option -f are given.\n");
				fprintf(
				    stderr,
				    "Try saflogger -h for more information.\n");
				exit(EXIT_FAILURE);
			}
			if (appLogFileCreateAttributes.logFileName != NULL)
				free(appLogFileCreateAttributes.logFileName);

			appLogFileCreateAttributes.logFileName = strdup(optarg);
			f_opt = true;
			break;
		case 's':
			logRecord.logHeader.genericHdr.logSeverity =
			    get_severity(optarg);
			break;
		case 'h':
			usage();
			exit(EXIT_SUCCESS);
			break;
		case '?':
		default:
			fprintf(stderr,
				"Try saflogger -h for more information.\n");
			exit(EXIT_FAILURE);
			break;
		}
	}

	if (f_opt == true && is_appstream == false) {
		fprintf(stderr,
			"Note: -f is only applicaple for app stream.\n");
		fprintf(stderr, "Try saflogger -h for more information.\n");
		exit(EXIT_FAILURE);
	}

	if (appLogFileCreateAttributes.logFileName != NULL &&
	    is_appstream == true) {
		size_t len = 0;
		if ((len = strlen(appLogFileCreateAttributes.logFileName)) >
		    255) {
			fprintf(
			    stderr,
			    "FILENAME is too long (%zu) (max: 255 characters).\n",
			    len);
			fprintf(stderr,
				"Try saflogger -h for more information.\n");
			exit(EXIT_FAILURE);
		}
	}

	if (optind >= argc) {
		/* No body of log record */
	} else if (optind == argc - 1) {
		/* Create body of log record */
		int sz;
		char *logBuf = NULL;
		sz = strlen(argv[optind]);
		logBuf = malloc(
		    sz + 64); /* add space for index/id in periodic writes */
		strcpy(logBuf, argv[optind]);
		logBuffer.logBufSize = sz;
		logBuffer.logBuf = (SaUint8T *)logBuf;
		logRecord.logBuffer = &logBuffer;
	} else {
		fprintf(stderr, "Invalid argument.\n");
		fprintf(stderr,
			"Enclose message in quotation marks \"\" e.g. \"");
		while (optind < argc) {
			fprintf(stderr, "%s", argv[optind++]);
			if (optind < argc)
				fprintf(stderr, " ");
			else
				fprintf(stderr, "\"\n");
		}
		fprintf(stderr, "Try saflogger -h for more information.\n");
		exit(EXIT_FAILURE);
	}

	if (logRecord.logHdrType == SA_LOG_NTF_HEADER) {
		/* Setup some valid values */
		logRecord.logHeader.ntfHdr.notificationId =
		    SA_NTF_IDENTIFIER_UNUSED;
		logRecord.logHeader.ntfHdr.eventType = SA_NTF_ALARM_PROCESSING;
		logRecord.logHeader.ntfHdr.notificationObject = &logSvcUsrName;
		logRecord.logHeader.ntfHdr.notifyingObject = &logSvcUsrName;
		logRecord.logHeader.ntfHdr.notificationClassId =
		    &notificationClassId;
		logRecord.logHeader.ntfHdr.eventTime = get_current_SaTime();
	}

	wait_time = 0;
	SaVersionT log_version = kLogVersion;
	error = saLogInitialize(&logHandle, &logCallbacks, &log_version);
	while (error == SA_AIS_ERR_TRY_AGAIN && wait_time < TEN_SECONDS) {
		usleep(HUNDRED_MS);
		wait_time += HUNDRED_MS;
		log_version = kLogVersion;
		error =
		      saLogInitialize(&logHandle, &logCallbacks, &log_version);
	}

	if (error != SA_AIS_OK) {
		if (wait_time)
			fprintf(stderr, "Waited for %u seconds.\n",
				wait_time / 1000000);
		fprintf(stderr, "saLogInitialize FAILED: %s\n",
			saf_error(error));
		exit(EXIT_FAILURE);
	}

	error = saLogSelectionObjectGet(logHandle, &selectionObject);
	if (error != SA_AIS_OK) {
		printf("saLogSelectionObjectGet FAILED: %s\n",
		       saf_error(error));
		exit(EXIT_FAILURE);
	}

	/* Try open the stream before creating it. It might be a configured app
	 * stream with other attributes than we have causing open with default
	 * attributes to fail */
	wait_time = 0;
	error = saLogStreamOpen_2(logHandle, &logStreamName, NULL, 0,
				  SA_TIME_ONE_SECOND, &logStreamHandle);
	while (error == SA_AIS_ERR_TRY_AGAIN && wait_time < TEN_SECONDS) {
		usleep(HUNDRED_MS);
		wait_time += HUNDRED_MS;
		error = saLogStreamOpen_2(logHandle, &logStreamName, NULL, 0,
					  SA_TIME_ONE_SECOND, &logStreamHandle);
	}

	if (error == SA_AIS_ERR_NOT_EXIST) {
		wait_time = 0;
		error = saLogStreamOpen_2(
		    logHandle, &logStreamName, logFileCreateAttributes,
		    logStreamOpenFlags, SA_TIME_ONE_SECOND, &logStreamHandle);
		while (error == SA_AIS_ERR_TRY_AGAIN &&
		       wait_time < TEN_SECONDS) {
			usleep(HUNDRED_MS);
			wait_time += HUNDRED_MS;
			error = saLogStreamOpen_2(
			    logHandle, &logStreamName, logFileCreateAttributes,
			    logStreamOpenFlags, SA_TIME_ONE_SECOND,
			    &logStreamHandle);
		}
	}

	if (error != SA_AIS_OK) {
		if (wait_time)
			fprintf(stderr, "Waited for %u seconds.\n",
				wait_time / 1000000);
		fprintf(stderr, "saLogStreamOpen_2 FAILED: %s\n",
			saf_error(error));
		exit(EXIT_FAILURE);
	}

	if (write_log_record(logHandle, logStreamHandle, selectionObject,
			     &logRecord) != SA_AIS_OK) {
		exit(EXIT_FAILURE);
	}

	wait_time = 0;
	error = saLogStreamClose(logStreamHandle);
	while (error == SA_AIS_ERR_TRY_AGAIN && wait_time < TEN_SECONDS) {
		usleep(HUNDRED_MS);
		wait_time += HUNDRED_MS;
		error = saLogStreamClose(logStreamHandle);
	}

	if (SA_AIS_OK != error) {
		if (wait_time)
			fprintf(stderr, "Waited for %u seconds.\n",
				wait_time / 1000000);
		fprintf(stderr, "saLogStreamClose FAILED: %s\n",
			saf_error(error));
		exit(EXIT_FAILURE);
	}

	wait_time = 0;
	error = saLogFinalize(logHandle);
	while (error == SA_AIS_ERR_TRY_AGAIN && wait_time < TEN_SECONDS) {
		usleep(HUNDRED_MS);
		wait_time += HUNDRED_MS;
		error = saLogFinalize(logHandle);
	}

	if (appLogFileCreateAttributes.logFileName != NULL)
		free(appLogFileCreateAttributes.logFileName);

	if (SA_AIS_OK != error) {
		if (wait_time)
			fprintf(stderr, "Waited for %u seconds.\n",
				wait_time / 1000000);
		fprintf(stderr, "saLogFinalize FAILED: %s\n", saf_error(error));
		exit(EXIT_FAILURE);
	}

	exit(EXIT_SUCCESS);
}
