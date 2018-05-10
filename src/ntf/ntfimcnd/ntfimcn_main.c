/*      -*- OpenSAF  -*-
 *
 * (C) Copyright 2013 The OpenSAF Foundation
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

#include "ntfimcn_main.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <libgen.h>
#include <limits.h>
#include <time.h>
#include <signal.h>
#include <poll.h>
#include <unistd.h>
#include <errno.h>

#include "base/osaf_gcov.h"
#include "base/os_defs.h"
#include "base/ncs_osprm.h"
#include "base/logtrace.h"
#include "base/saf_error.h"

#include "ntfimcn_notifier.h"
#include "ntfimcn_imm.h"
#include "osaf/configmake.h"

#define NTFIMCN_DEFAULT_LOG "osafntfimcn"

enum { FD_IMM = 0, SIZE_FDS } NTFIMCN_FDS;

/*
 * Global parameters
 */
ntfimcn_cb_t ntfimcn_cb;

static struct pollfd fds[SIZE_FDS];
static nfds_t nfds = SIZE_FDS;
static unsigned int category_mask = 0;

void imcn_exit(int status)
{
	// No need to clear OI Applier name
	// as IMM will do that if the owning
	// process no longer exist.
	_Exit(status);
}

/**
 * USR2 signal handler to enable/disable trace (toggle)
 * @param sig[in]
 */
static void sigusr2_handler(int sig)
{
	if (category_mask == 0) {
		category_mask = CATEGORY_ALL;
	} else {
		category_mask = 0;
	}

	if (trace_category_set(category_mask) == -1)
		LOG_ER("trace_category_set failed");

	if (category_mask != 0) {
		LOG_NO("osafntfimcn trace enabled");
	}
}

/*
 * Exit if anything fails. This will cause ntfs to restart ntfimcn
 */
int main(int argc, char **argv)
{
	const char *logPath;
	const char *trace_label = "osafntfimcnd";
	SaAisErrorT ais_error = SA_AIS_OK;

	/*
	 * Activate Log Trace
	 */
	openlog(basename(argv[0]), LOG_PID, LOG_LOCAL0);
	if ((logPath = getenv("NTFSCN_TRACE_PATHNAME"))) {
		category_mask = 0xffffffff;
	} else {
		logPath =  NTFIMCN_DEFAULT_LOG;
	}

	if (logtrace_init(trace_label, logPath, category_mask) == -1) {
		syslog(LOG_ERR, "osafntfimcnd logtrace_init FAILED");
		/* We allow to execute anyway. */
	}

	/*
	 * Initiate HA state
	 */
	if (strcmp(argv[1], "active") == 0) {
		ntfimcn_cb.haState = SA_AMF_HA_ACTIVE;
	} else {
		ntfimcn_cb.haState = SA_AMF_HA_STANDBY;
	}

	/*
	 * Initiate external functionallity
	 * Exit on fail. This will cause ntfs to restart ntfimcn
	 */
	if (ntfimcn_ntf_init(&ntfimcn_cb) == NTFIMCN_INTERNAL_ERROR) {
		LOG_ER("ntfimcn_ntf_init() Fail");
		_Exit(EXIT_FAILURE);
	}

	if (ntfimcn_imm_init(&ntfimcn_cb) == NTFIMCN_INTERNAL_ERROR) {
		LOG_ER("ntfimcn_imm_init() Fail");
		imcn_exit(EXIT_FAILURE);
	}

	/* Signal for TRACE on/off */
	if (signal(SIGUSR2, sigusr2_handler) == SIG_ERR) {
		LOG_ER("signal USR2 failed: %s", strerror(errno));
		/* We allow to execute anyway. */
	}

	if (ntfimcn_send_lost_cm_notification() == NTFIMCN_INTERNAL_ERROR) {
		LOG_ER("send_lost_cm_notification() Fail");
		imcn_exit(EXIT_FAILURE);
	}

	// Enable code coverage logging. (if --enable-gcov in configure)
	create_gcov_flush_thread();

	/*
	 * Initiate polling
	 */
	fds[FD_IMM].fd = ntfimcn_cb.immSelectionObject;
	fds[FD_IMM].events = POLLIN;

	LOG_NO("Started");

	while (1) {
		if (poll(fds, nfds, -1) == (-1)) {
			if (errno == EINTR) {
				continue;
			}

			LOG_ER("poll Fail - %s", strerror(errno));
			imcn_exit(EXIT_FAILURE);
		}

		if (fds[FD_IMM].revents & POLLIN) {
			ais_error = saImmOiDispatch(ntfimcn_cb.immOiHandle,
						    SA_DISPATCH_ALL);
			if (ais_error != SA_AIS_OK) {
				LOG_NO("saImmOiDispatch() Fail %s",
				       saf_error(ais_error));
				imcn_exit(EXIT_FAILURE);
			}
		}
	}

	return 0; /* Dummy */
}
