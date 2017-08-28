/*      -*- OpenSAF  -*-
 *
 * Copyright (C) 2017, Oracle and/or its affiliates. All rights reserved.
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
 */

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <poll.h>
#include <libgen.h>
#include <signal.h>
#include <string.h>
#include <sys/inotify.h>

#include <saAmf.h>
#include <saAis.h>

char path[256];
FILE *fp;

/*
 * @brief   AMF will invoke this callback whenever SCs goes down and
 *          joins back.
 */
static void my_sc_status_cbk(OsafAmfSCStatusT state)
{
	fp = fopen(path, "w");
	if (fp == NULL)
		exit(EXIT_FAILURE);
	if (state == OSAF_AMF_SC_PRESENT) {
		fprintf(stdout,
		"\nReceived SC Status Change Callback: SC status Present\n\n");
		fprintf(fp,
		"\nReceived SC Status Change Callback: SC status Present\n\n");
	} else if (state == OSAF_AMF_SC_ABSENT) {
		fprintf(stdout,
		"\nReceived SC Status Change Callback: SC status Absent\n\n");
		fprintf(fp,
		"\nReceived SC Status Change Callback: SC status Absent\n\n");
	}
	fclose(fp);
}

int main(int argc, char **argv)
{
	SaAisErrorT rc;
	struct pollfd fds[1];
	int inotify_fd, inotify_fd2;
	SaAmfCallbacksT_4 amf_cbk = {0};
	SaVersionT ver = {'B', 0x04, 0x01};
	SaAmfHandleT amf_hdl;

	/*Open log file. Log pid, a message and close it.*/
	snprintf(path, sizeof(path), "%s/amfscstatusdemo.log", "/tmp");
	fp = fopen(path, "w");
	if (fp == NULL) {
		fprintf(stderr, " log file open FAILED:%u", rc);
		goto done;
	}
	fprintf(fp, "%d\n", getpid());
	fclose(fp);

	rc = saAmfInitialize_4(&amf_hdl, &amf_cbk, &ver);
	if (rc != SA_AIS_OK) {
		fprintf(stderr, " saAmfInitialize FAILED %u", rc);
		goto done;
	}

	/*Register SC Status Change Callback with AMF.*/
	rc = osafAmfInstallSCStatusChangeCallback(0, my_sc_status_cbk);
	if (rc != SA_AIS_OK) {
		fprintf(stderr,
			"osafAmfInstallSCStatusChangeCallback FAILED %u", rc);
		goto done;
	}
	inotify_fd = inotify_init();
	if (inotify_fd < 0) {
		fprintf(stderr,
			"inotify_init() failed with: '%s'\n", strerror(errno));
		goto done;
	}
	inotify_fd2 = inotify_add_watch(inotify_fd, path, IN_MODIFY);
	if (inotify_fd2 < 0) {
		fprintf(stderr,
			"inotify_add_watch failed with: '%s'\n",
			strerror(errno));
		goto done;
	}

	fds[0].fd = inotify_fd;
	fds[0].events = POLLIN;

	while (1) {
		int res = poll(fds, 1, -1);

		if (res == -1) {
			if (errno == EINTR) {
				continue;
			} else {
				fprintf(stderr,
					"poll FAILED - %s", strerror(errno));
				goto done;
			}
		}
		if (fds[0].revents & POLLIN) {
			rc = saAmfFinalize(amf_hdl);
			if (rc != SA_AIS_OK) {
				fprintf(stderr, " saAmfFinalize FAILED %u", rc);
				exit(EXIT_FAILURE);
			}
			inotify_rm_watch(inotify_fd, inotify_fd2);
			exit(EXIT_SUCCESS);
		}
	}
done:
	return EXIT_FAILURE;
}
