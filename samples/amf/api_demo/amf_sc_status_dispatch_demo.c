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

#include <saAmf.h>
#include <saAis.h>

/*
 * @brief   AMF will invoke this callback whenever SCs goes down and
 *          joins back.
 */
static void my_sc_status_cbk(OsafAmfSCStatusT state)
{
	if (state == OSAF_AMF_SC_PRESENT) {
		fprintf(stdout,
		"\nReceived SC Status Change Callback: SC status Present\n\n");
	} else if (state == OSAF_AMF_SC_ABSENT) {
		fprintf(stdout,
		"\nReceived SC Status Change Callback: SC status Absent\n\n");
	}
}

int main(int argc, char **argv)
{
	SaAisErrorT rc;
	struct pollfd fds[1];
	SaAmfCallbacksT_4 amf_cbk = {0};
	SaAmfHandleT amf_hdl;
	SaSelectionObjectT amf_sel_obj;
	SaVersionT ver = {'B', 0x04, 0x01};

	rc = saAmfInitialize_4(&amf_hdl, &amf_cbk, &ver);
	if (rc != SA_AIS_OK) {
		fprintf(stderr, "saAmfInitialize FAILED %u", rc);
		goto done;
	}

	rc = saAmfSelectionObjectGet(amf_hdl, &amf_sel_obj);
	if (rc != SA_AIS_OK) {
		fprintf(stderr, "saAmfSelectionObjectGet FAILED %u", rc);
		goto done;
	}

	/*Register SC Status Change Callback with AMF.*/
	rc = osafAmfInstallSCStatusChangeCallback(amf_hdl, my_sc_status_cbk);
	if (rc != SA_AIS_OK) {
		fprintf(stderr,
			"osafAmfInstallSCStatusChangeCallback FAILED %u", rc);
		goto done;
	}

	fds[0].fd = amf_sel_obj;
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
			fprintf(stdout, "\nReceived some AMF event.");
			rc = saAmfDispatch(amf_hdl, SA_DISPATCH_ONE);
			if (rc != SA_AIS_OK) {
				fprintf(stderr, "saAmfDispatch FAILED %u", rc);
				goto done;
			}
		}
	}
done:
	return EXIT_FAILURE;
}
