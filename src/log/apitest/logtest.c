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

#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>
#include <limits.h>
#include <stdlib.h>
#include "osaf/configmake.h"
#include <ctype.h>
#include <stdarg.h>
#include <saImmOm.h>
#include "osaf/immutil/immutil.h"
#include <saImm.h>
#include "base/saf_error.h"

#include "log/apitest/logtest.h"
#include "base/osaf_extended_name.h"
#include <saAis.h>

#define LLDTEST

SaNameT systemStreamName;
SaNameT alarmStreamName;
SaNameT globalConfig;
SaNameT notificationStreamName;
SaNameT app1StreamName;
SaNameT notifyingObject;
SaNameT notificationObject;
SaNameT configurationObject;

SaConstStringT obj256 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
			"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
			"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
			"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
			"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
			"bbbbb";
SaNameT saNameT_Object_256;

SaConstStringT name256 = "safLgStr=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
			 "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
			 "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
			 "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
			 "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
			 "bbbbb";

SaNameT saNameT_appstream_name_256;

static char buf[2048];

SaLogBufferT alarmStreamBuffer = {
    .logBuf = (SaUint8T *)buf, .logBufSize = 0,
};

SaLogBufferT notificationStreamBuffer = {
    .logBuf = (SaUint8T *)buf, .logBufSize = 0,
};

static SaLogBufferT genLogBuffer = {
    .logBuf = (SaUint8T *)buf, .logBufSize = 0,
};

SaNameT logSvcUsrName;

/*
 * NOTE: Don't move this global pointer genLogrecord.logBuffer->logBuf.
 * Do so could have impact on other test cases using this global data.
 * e.g: if you move logBuffer->logBuf to your local data, writing to
 * this memory in other test case will get "invalid write".
 */
SaLogRecordT genLogRecord = {
    .logTimeStamp = SA_TIME_UNKNOWN,
    .logHdrType = SA_LOG_GENERIC_HEADER,
    .logHeader.genericHdr.notificationClassId = NULL,
    .logHeader.genericHdr.logSeverity = SA_LOG_SEV_INFO,
    .logHeader.genericHdr.logSvcUsrName = &logSvcUsrName,
    .logBuffer = &genLogBuffer};

SaAisErrorT rc;
SaLogHandleT logHandle;
SaLogStreamHandleT logStreamHandle;
SaLogCallbacksT logCallbacks = {NULL, NULL, NULL};
SaInvocationT invocation = 0;
SaSelectionObjectT selectionObject;
char log_root_path[PATH_MAX];

void init_logrootpath(void)
{
	SaImmHandleT omHandle;
	SaConstStringT config = "logConfig=1,safApp=safLogService";
	SaNameT objectName;
	SaImmAccessorHandleT accessorHandle;
	SaImmAttrValuesT_2 *attribute;
	SaImmAttrValuesT_2 **attributes;
	SaAisErrorT ais_rc = SA_AIS_OK;
	const char logRootDirectory_name[] = "logRootDirectory";
	SaImmAttrNameT attributeNames[2] = {(char *)logRootDirectory_name,
					    NULL};
	void *value;

	saAisNameLend(config, &objectName);
	/* NOTE: immutil init osaf_assert if error */
	(void)immutil_saImmOmInitialize(&omHandle, NULL, &kImmVersion);
	(void)immutil_saImmOmAccessorInitialize(omHandle, &accessorHandle);

	/* Get all attributes of the object */
	ais_rc = immutil_saImmOmAccessorGet_2(accessorHandle, &objectName,
					      attributeNames, &attributes);
	if (ais_rc == SA_AIS_OK) {
		attribute = attributes[0];
		value = attribute->attrValues[0];
		strncpy(log_root_path, *((char **)value), PATH_MAX);
	} else {
		/* We didn't get a root path from IMM. Use default */
		strncpy(log_root_path, PKGLOGDIR, PATH_MAX);
	}
	(void)immutil_saImmOmFinalize(omHandle);
}

/**
 * Get attribute value from IMM
 * @param inObjname Distinguished Name
 * @param inAttr Attribute to search for value
 * @param value The holder for the output value
 * @return 0 if successfull, otherwise (-1)
 */
int get_attr_value(SaNameT *inObjName, char *inAttr, void *outValue)
{
	SaImmHandleT omHandle;
	SaImmAccessorHandleT accessorHandle;
	SaImmAttrValuesT_2 *attribute = NULL;
	SaImmAttrValuesT_2 **attributes;
	SaAisErrorT ais_rc = SA_AIS_OK;
	SaImmAttrNameT attributeNames[2] = {inAttr, NULL};
	void *value = NULL;
	int rc = 0;

	/* NOTE: immutil init osaf_assert if error */
	(void)immutil_saImmOmInitialize(&omHandle, NULL, &kImmVersion);
	(void)immutil_saImmOmAccessorInitialize(omHandle, &accessorHandle);

	/* Get all attributes of the object */
	ais_rc = immutil_saImmOmAccessorGet_2(accessorHandle, inObjName,
					      attributeNames, &attributes);
	if (ais_rc == SA_AIS_OK) {
		attribute = attributes[0];
		if ((attribute != NULL) && (attribute->attrValuesNumber != 0)) {
			value = attribute->attrValues[0];
			switch (attribute->attrValueType) {
			case SA_IMM_ATTR_SAINT32T:
				*((SaInt32T *)outValue) = *(SaInt32T *)value;
				break;

			case SA_IMM_ATTR_SAUINT32T:
				*((SaUint32T *)outValue) = *(SaUint32T *)value;
				break;

			case SA_IMM_ATTR_SAINT64T:
				*((SaInt64T *)outValue) = *(SaInt64T *)value;
				break;

			case SA_IMM_ATTR_SAUINT64T:
				*((SaUint64T *)outValue) = *(SaUint64T *)value;
				break;

			case SA_IMM_ATTR_SATIMET:
				*((SaTimeT *)outValue) = *(SaTimeT *)value;
				break;

			case SA_IMM_ATTR_SAFLOATT:
				*((SaFloatT *)outValue) = *(SaFloatT *)value;
				break;

			case SA_IMM_ATTR_SADOUBLET:
				*((SaDoubleT *)outValue) = *(SaDoubleT *)value;
				break;

			case SA_IMM_ATTR_SANAMET:
			case SA_IMM_ATTR_SASTRINGT:
				strcpy(outValue, *((char **)value));
				break;

			default:
				fprintf(stderr, "Unsupported data type (%s) \n",
					inAttr);
				rc = -1;
				break;
			}
		} else {
			rc = (-1);
		}
	} else {
		/* We didn't get the attribute value from IMM. Return error (-1)
		 */
		rc = (-1);
	}
	(void)immutil_saImmOmFinalize(omHandle);

	return rc;
}

/**
 * Verbose printing
 * If chosen extra trace printouts will be done during test.
 *
 * In-parameters same as printf()
 * @param format [in]
 * @param ... [in]
 */
void printf_v(const char *format, ...)
{
	va_list argptr;

	if (!verbose_flg)
		return;

	va_start(argptr, format);
	(void)vfprintf(stdout, format, argptr);
	va_end(argptr);
}

/**
 * Silent printing
 * If chosen no information printouts will be done during test.
 * Only affects stdout. Do not affect default printouts for PASS/FAIL info
 *
 * In-parameters same as printf()
 * @param format [in]
 * @param ... [in]
 */
void printf_s(const char *format, ...)
{
	va_list argptr;

	if (silent_flg)
		return;

	va_start(argptr, format);
	(void)vfprintf(stdout, format, argptr);
	va_end(argptr);
}

/**
 * Tag printing
 * If chosen no information printouts will be done during test.
 * Only affects stdout. Do not affect default printouts for PASS/FAIL info
 *
 * In-parameters same as printf()
 * @param format [in]
 * @param ... [in]
 */
void print_t(const char *format, ...)
{
	va_list argptr;

	if (!tag_flg)
		return;

	va_start(argptr, format);
	(void)vfprintf(stdout, format, argptr);
	va_end(argptr);
	(void)fflush(NULL);
}

/**
 * Return info about which SC node that is Active
 *
 * Note: Using immutil with default setting meaning abort if error
 *
 * @return 1 SC-1, 2 SC-2
 */
int get_active_sc(void)
{
	int active_sc = 0;
	SaImmHandleT omHandle;
	SaConstStringT objname = "safSu=SC-1,safSg=2N,safApp=OpenSAF";
	SaNameT objectName1;
	SaImmAccessorHandleT accessorHandle;
	SaImmAttrValuesT_2 **attributes;
	const char saAmfSUNumCurrActiveSIs[] = "saAmfSUNumCurrActiveSIs";
	SaImmAttrNameT attributeNames[2] = {(char *)saAmfSUNumCurrActiveSIs,
					    NULL};
	SaUint32T curr_act_sis = 0;

	saAisNameLend(objname, &objectName1);
	/* NOTE: immutil init osaf_assert if error
	 */
	(void)immutil_saImmOmInitialize(&omHandle, NULL, &kImmVersion);
	(void)immutil_saImmOmAccessorInitialize(omHandle, &accessorHandle);

	/* Get attributes of the object
	 * We may have to wait until a value is available
	 */
	int try_cnt = 0;
	while (1) {
		(void)immutil_saImmOmAccessorGet_2(accessorHandle, &objectName1,
						   attributeNames, &attributes);
		if (attributes[0]->attrValuesNumber != 0)
			break;
		sleep(1);
		if (try_cnt++ >= 10) {
			fprintf(stderr,
				"%s FAILED Attribute value could not be read\n",
				__FUNCTION__);
			abort();
		}
	}

	/* Checking SC-1 */
	curr_act_sis = *((SaUint32T *)attributes[0]->attrValues[0]);

	if (curr_act_sis > 0) {
		active_sc = 1;
	} else {
		active_sc = 2;
	}

	(void)immutil_saImmOmFinalize(omHandle);
	return active_sc;
}

/**
 * Print help text
 */
static void usage(void)
{
	printf("\nNAME\n");
	printf("\tlogtest - Test log service\n");

	printf("\nSYNOPSIS\n");
	printf("\tlogtest [options]\n");

	printf("\nDESCRIPTION\n");
	printf("\tRun test suite or single test case for log service\n"
	       "\tSome tests are not included in the 'general' test suite.\n"
	       "\tOptions are used to 'activate' not included tests.\n"
	       "\tIf logtest is used with no options all 'general' test cases\n"
	       "\tin all suites will be executed.\n"
	       "\tIt is also possible to run one suite or one test case.\n"
	       "\tExamples:\n"
	       "\tlogtest 1\tWill execute all test cases in suite 1\n"
	       "\tlogtest 1 1\tWill execute test case 1 in suite 1\n");

	printf("\nOPTIONS\n");

	printf("  '0'\t\tList all 'general' test cases.\n"
	       "  \t\tNo test case is executed\n");
	printf(
	    "  [s] [t]\tRun test case 't' in 'general' suite 's'\n"
	    "  \t\tIf 's' or 't' (or both) is omittet all 'general' testcases\n"
	    "  \t\tare executed or all test cases in suite 's'\n");
	printf("  -l\t\tList all test cases including test cases not included\n"
	       "  \t\tin the 'general' test\n");
	printf("  -e \"<s> [t]\"\tExecute 'extended' tests not part of\n"
	       "  \t\t\'general' test.\n"
	       "  \t\ts: Test suite number. Must be given\n"
	       "  \t\tt: Test case number. Can be omitted. If omitted all\n"
	       "  \t\t   testcases in the suite is executed\n");
	printf("  -h\t\tThis help\n");

	printf(
	    "\n  The following options can only be used together with -e\n\n");
	printf("  -v\t\tActivate verbose printing. Some test cases can print\n"
	       "  \t\textra information if verbose is activated\n");
	printf(
	    "  -s\t\tActivate silent mode. Suppress printing of information\n"
	    "  \t\tduring test. Only 'standard' PASS/FAIL information will\n"
	    "  \t\tbe printed\n");
	printf(
	    "  -t\t\tActivate tag mode. Suppress printing of information\n"
	    "  \t\tduring test. Only 'standard' PASS/FAIL information and\n"
	    "  \t\ttags will be printed. A tag is a short string that will\n"
	    "  \t\tnever be changed. It can be used to trig external events\n"
	    "  \t\tif a test is executed in an external test framework\n");

	printf("\n");
}

static void err_exit(void)
{
	usage();
	exit(1);
}

/**
 * Get numeric value (positive) from a string
 * Will return the value if the initial part of the string is a numeric value
 * See strtol()
 *
 * @param in_str [in] String to investigate
 * @return value or -2 if not numeric or negative
 */
static int str_to_digit(const char *in_str)
{
	long int value = -1;
	char *endptr;

	value = strtol(in_str, &endptr, 10);
	if ((endptr == in_str) || (value < 0)) {
		/* No numeric value or negative */
		value = -2;
	}

	return (int)value;
}

int main(int argc, char **argv)
{
	int suite = ALL_SUITES, tcase = ALL_TESTS;
	int opt_val = 0;
	bool etst_flg = false;

	if (setenv("SA_ENABLE_EXTENDED_NAMES", "1", 1) != 0) {
		fprintf(stderr, "Failed to set SA_ENABLE_EXTENDED_NAMES (%s)",
			strerror(errno));
		exit(0);
	}

	saAisNameLend(SA_LOG_STREAM_SYSTEM, &systemStreamName);
	saAisNameLend(SA_LOG_STREAM_ALARM, &alarmStreamName);
	saAisNameLend(LOGTST_IMM_LOG_GCFG, &globalConfig);
	saAisNameLend(SA_LOG_STREAM_NOTIFICATION, &notificationStreamName);
	saAisNameLend(SA_LOG_STREAM_APPLICATION1, &app1StreamName);
	saAisNameLend(DEFAULT_NOTIFYING_OBJECT, &notifyingObject);
	saAisNameLend(DEFAULT_NOTIFICATION_OBJECT, &notificationObject);
	saAisNameLend(SA_LOG_CONFIGURATION_OBJECT, &configurationObject);
	saAisNameLend(obj256, &saNameT_Object_256);
	saAisNameLend(name256, &saNameT_appstream_name_256);
	saAisNameLend(SA_LOG_STREAM_APPLICATION1, &logSvcUsrName);

	init_logrootpath();
	srandom(getpid());
	silent_flg = false;

	/* Handle options */
	char optstr[] = "hle:vst";
	opt_val = getopt(argc, argv, optstr);
	do {
		if (opt_val == -1) /* No option */
			break;

		switch (opt_val) {
		case 'h':
			usage();
			exit(0);
		case 'l':
			add_suite_9();
			add_suite_10();
			add_suite_11();
			add_suite_12();
			add_suite_14();
			add_suite_15();
			add_suite_16();
			test_list();
			exit(0);
		case 'e':
			if (argv[optind - 1] != NULL) {
				suite = atoi(argv[optind - 1]);
				if (suite == 0) {
					/* Argument is not numeric or 0
					 */
					err_exit();
				}
			} else {
					err_exit();
			}
			if (optind < argc && argv[optind] != NULL)
				tcase = atoi(argv[optind]);
			if (tcase == 0) {
				/* Argument is not numeric or 0
				 */
				err_exit();
			}
			etst_flg = true;
			add_suite_9();
			add_suite_10();
			add_suite_11();
			add_suite_12();
			add_suite_14();
			add_suite_15();
			add_suite_16();
			break;
		case 'v':
			if (silent_flg == true) {
				/* Cannot have silent and
				 * verbose at the same time */
				err_exit();
			}
			verbose_flg = true;
			break;
		case 's':
			if (verbose_flg == true) {
				/* Cannot have silent and
				 * verbose at the same time */
				err_exit();
			}
			silent_flg = true;
			break;
		case 't':
			if (verbose_flg == true) {
				/* Cannot have tag/silent and
				 * verbose at the same time */
				err_exit();
			}
			silent_flg = true;
			tag_flg = true;
			break;
		default:
			/* Unknown option */
			err_exit();
		}

		// if (break_flg) break;

		opt_val = getopt(argc, argv, optstr);
	} while (opt_val != -1);

	if (etst_flg == false) {
		/* Get input values if no options */
		if (argc > 1) {
			suite = str_to_digit(argv[1]);
			if (suite < ALL_SUITES) {
				err_exit();
			}
		}

		if (argc > 2) {
			tcase = str_to_digit(argv[2]);
			if (tcase < ALL_TESTS) {
				err_exit();
			}
		}

		if (suite == 0) {
			test_list();
			return 0;
		}

		if (suite == 999) {
			return 0;
		}
	}

	/* Run test and Measure time to run test.
	 * Result will be printed if -v option
	 */
	time_meas_t tm_tr;
	time_meas_start(&tm_tr);
	int rc_tr = test_run(suite, tcase);
	time_meas_print_v(&tm_tr, "Complete test");
	return rc_tr;
}
