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

#include "logtest.h"

extern struct LogProfile logProfile;

//>
// This test suite - multiple threads is to test if there is coredump
// as there is race condition on log stream or log client.
// Also to test if there is any possibility of deadlock or not.
// The tests ignore the return value of APIs.
//<

// If you care of seeing the return value of APIs, set the environment
// variable @LOGTEST_DEBUG
static int g_debug = 0;
#define OUTPUT(str, rc)                                                        \
	do {                                                                   \
		if (g_debug)                                                   \
			printf("%s(%s) rc: %d\n", __func__, str, rc);          \
	} while (0);

//
// Multiple threads on same log handle
//
void *create_and_close_log_stream(void *thread_num)
{
	char filename[100] = {0};
	char data[100] = {0};

	memset(genLogRecord.logBuffer->logBuf, 'X', 1000);
	genLogRecord.logBuffer->logBufSize = 1000;

	SaLogFileCreateAttributesT_2 appStreamLogFileCreateAttributes_me = {
	    .logFilePathName = DEFAULT_APP_FILE_PATH_NAME,
	    .maxLogFileSize = 4096,
	    .maxLogRecordSize = 1024,
	    .haProperty = SA_TRUE,
	    .logFileFullAction = SA_LOG_FILE_FULL_ACTION_ROTATE,
	    .maxFilesRotated = DEFAULT_MAX_FILE_ROTATED,
	    .logFileFmt = DEFAULT_FORMAT_EXPRESSION};

	sprintf(filename, "thread_num_%d", *(int *)thread_num);
	appStreamLogFileCreateAttributes_me.logFileName = filename;
	SaNameT appStreamName_me;
	SaAisErrorT rc_me = SA_AIS_OK;
	sprintf(data, "safLgStr=data_thread_num_%d", *(int *)thread_num);
	saAisNameLend(data, &appStreamName_me);

	rc_me = logAppStreamOpen(&appStreamName_me,
				 &appStreamLogFileCreateAttributes_me);
	if (rc_me != SA_AIS_OK) {
		OUTPUT("logAppStreamOpen", rc_me);
		goto done;
	}

	usleep(1000);

	rc_me = logStreamClose();
	if (rc_me != SA_AIS_OK) {
		OUTPUT("logStreamClose", rc_me);
		goto done;
	}

done:
	pthread_exit(NULL);
	return NULL;
}

void *read_log_stream(void *thread_num)
{
	SaAisErrorT rc_me = SA_AIS_OK;
	SaSelectionObjectT obj;
	rc_me = saLogSelectionObjectGet(logHandle, &obj);
	if (rc_me != SA_AIS_OK) {
		OUTPUT("saLogSelectionObjectGet", rc_me);
		goto done;
	}

	rc_me = logStreamOpen(&systemStreamName);
	if (rc_me != SA_AIS_OK) {
		OUTPUT("logStreamOpen", rc_me);
		goto done;
	}

	rc_me = logWriteAsync(&genLogRecord);
	if (rc_me != SA_AIS_OK) {
		OUTPUT("logWriteAsync", rc_me);
		goto done;
	}

done:
	pthread_exit(NULL);
	return NULL;
}

void delete_and_access_log_stream_on_multiple_threads(void)
{
	int i, nThreads = 5;
	SaAisErrorT rc_me = SA_AIS_OK;
	pthread_t threads[nThreads];
	pthread_t threads2[nThreads];

	rc_me = logInitialize();
	if (rc_me != SA_AIS_OK) {
		fprintf(stderr, "LogInit \n");
		test_validate(rc_me, SA_AIS_OK);
		return;
	}

	logProfile.nTries = 50;
	for (i = 0; i < nThreads; i++) {
		pthread_create(&threads[i], NULL, create_and_close_log_stream,
			       &i);
		pthread_create(&threads2[i], NULL, read_log_stream, &i);
	}

	// Wait for all threads terminated
	for (i = 0; i < nThreads; i++) {
		pthread_join(threads[i], NULL);
		pthread_join(threads2[i], NULL);
	}

	logProfile.nTries = 100;
	rc_me = logFinalize();
	if (rc_me != SA_AIS_OK) {
		fprintf(stderr, "LogFinalize %d\n", rc_me);
		test_validate(rc_me, SA_AIS_OK);
		return;
	}

	logProfile.nTries = 25;
	test_validate(rc_me, SA_AIS_OK);
}

//
// Test race condition on log client database
//
static SaLogHandleT logHandle_me = 0;
void *create_and_close_log_handle(void *thread_num)
{
	SaAisErrorT rc_me = SA_AIS_OK;
	unsigned int nTries = 1;
	SaVersionT log_version = kLogVersion;

	rc_me = saLogInitialize(&logHandle_me, &logCallbacks, &log_version);
	while (rc_me == SA_AIS_ERR_TRY_AGAIN && nTries < 50) {
		usleep(100 * 1000);
		log_version = kLogVersion;
		rc_me =
		    saLogInitialize(&logHandle_me, &logCallbacks, &log_version);
		nTries++;
	}
	if (rc_me != SA_AIS_OK) {
		OUTPUT("saLogInitialize", rc_me);
		goto done;
	}

	usleep(10000);

	rc_me = saLogFinalize(logHandle_me);
	nTries = 1;
	while (rc_me == SA_AIS_ERR_TRY_AGAIN && nTries < 50) {
		usleep(100 * 1000);
		rc_me = saLogFinalize(logHandle_me);
		nTries++;
	}
	if (rc_me != SA_AIS_OK) {
		OUTPUT("saLogFinalize", rc_me);
	}

done:
	pthread_exit(NULL);
	return NULL;
}

void *read_log_handle(void *thread_num)
{
	char filename[100] = {0};
	char data[100] = {0};
	SaAisErrorT rc_me = SA_AIS_OK;
	SaSelectionObjectT obj;
	unsigned int nTries = 1;
	SaLogStreamHandleT logStreamHandle_me;

	rc_me = saLogSelectionObjectGet(logHandle, &obj);
	if (rc_me != SA_AIS_OK) {
		OUTPUT("saLogSelectionObjectGet", rc_me);
		goto done;
	}

	SaLogFileCreateAttributesT_2 appStreamLogFileCreateAttributes_me = {
	    .logFilePathName = DEFAULT_APP_FILE_PATH_NAME,
	    .maxLogFileSize = 4096,
	    .maxLogRecordSize = 1024,
	    .haProperty = SA_TRUE,
	    .logFileFullAction = SA_LOG_FILE_FULL_ACTION_ROTATE,
	    .maxFilesRotated = DEFAULT_MAX_FILE_ROTATED,
	    .logFileFmt = DEFAULT_FORMAT_EXPRESSION};
	sprintf(filename, "diff_handle_thread_num_%d", *(int *)thread_num);
	appStreamLogFileCreateAttributes_me.logFileName = filename;
	SaNameT appStreamName_me;
	sprintf(data, "safLgStr=diff_handle_data_thread_num_%d",
		*(int *)thread_num);
	saAisNameLend(data, &appStreamName_me);

	rc_me = saLogStreamOpen_2(logHandle_me, &appStreamName_me,
				  &appStreamLogFileCreateAttributes_me,
				  SA_LOG_STREAM_CREATE, SA_TIME_ONE_SECOND,
				  &logStreamHandle_me);
	nTries = 1;
	while (rc_me == SA_AIS_ERR_TRY_AGAIN && nTries < 50) {
		usleep(100 * 1000);
		rc_me = saLogStreamOpen_2(
		    logHandle_me, &appStreamName_me,
		    &appStreamLogFileCreateAttributes_me, SA_LOG_STREAM_CREATE,
		    SA_TIME_ONE_SECOND, &logStreamHandle_me);
		nTries++;
	}
	if (rc_me != SA_AIS_OK) {
		OUTPUT("saLogStreamOpen_2", rc_me);
		goto done;
	}

	strcpy((char *)genLogRecord.logBuffer->logBuf, __FUNCTION__);
	genLogRecord.logBuffer->logBufSize = strlen(__FUNCTION__);
	rc_me = saLogWriteLogAsync(logStreamHandle_me, invocation, 0,
				   &genLogRecord);
	if (rc_me != SA_AIS_OK) {
		OUTPUT("saLogWriteLogAsync", rc_me);
		goto done;
	}

	rc_me = saLogStreamClose(logStreamHandle_me);
	nTries = 1;
	while (rc_me == SA_AIS_ERR_TRY_AGAIN && nTries < 50) {
		usleep(100 * 1000);
		rc_me = saLogStreamClose(logStreamHandle_me);
		nTries++;
	}
	if (rc_me != SA_AIS_OK) {
		OUTPUT("saLogStreamClose", rc_me);
		goto done;
	}

	rc_me = logWriteAsync(&genLogRecord);
	if (rc_me != SA_AIS_OK) {
		OUTPUT("logWriteAsync", rc_me);
		goto done;
	}

done:
	pthread_exit(NULL);
	return NULL;
}

void delete_and_access_log_handle_on_multiple_threads(void)
{
	int i, nThreads = 5;
	SaAisErrorT rc_me = SA_AIS_OK;
	pthread_t threads[nThreads];
	pthread_t threads2[nThreads];

	logProfile.nTries = 50;
	for (i = 0; i < nThreads; i++) {
		pthread_create(&threads[i], NULL, create_and_close_log_handle,
			       &i);
		pthread_create(&threads2[i], NULL, read_log_handle, &i);
	}

	// Wait for all threads terminated
	for (i = 0; i < nThreads; i++) {
		pthread_join(threads[i], NULL);
		pthread_join(threads2[i], NULL);
	}

	test_validate(rc_me, SA_AIS_OK);
}

__attribute__((constructor)) static void multiple_thread_testsuite(void)
{
	/* Tests for create */
	if (getenv("LOGTEST_DEBUG") != NULL) {
		g_debug = 1;
	}
	test_suite_add(19, "Multiple thread");
	test_case_add(19, delete_and_access_log_stream_on_multiple_threads,
		      "Delete and access log stream on multiple theads");
	test_case_add(19, delete_and_access_log_handle_on_multiple_threads,
		      "Delete and access log handle on multiple threads");
}
