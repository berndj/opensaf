
/*      -*- OpenSAF  -*-
 *
 * (C) Copyright 2010 The OpenSAF Foundation
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
 * Author(s): Ericsson AB
 */

/*****************************************************************************
 *
 * DESCRIPTION:
 *
 * This file contains the AMF watchdog process.
 *
 * If health checks stops being received from AMF or if AMF goes down,
 * the latest received health check will be printed in syslog and the node
 * will be restarted.
 *
 * The timeout period (when AMF is considered being down) is
 * 60 seconds.
 *
 *****************************************************************************/

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <poll.h>
#include <syslog.h>
#include <libgen.h>
#include <time.h>
#include <sched.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <saAmf.h>
#include "base/ncssysf_def.h"
#include "osaf/configmake.h"
#include "base/daemon.h"

extern void ava_install_amf_down_cb(void (*cb)(void));

/* AMF Handle */
static SaAmfHandleT amf_hdl;

/* latest saved healthcheck trace */
static char latest_healthcheck_trace[100];

/* The temporary file created to indicate that a component termination
 * is attempted.
 */
static const char *term_state_file = PKGPIDDIR "/osafamfwd_termstate";

static void amf_csi_set_callback(SaInvocationT inv, const SaNameT *comp_name,
				 SaAmfHAStateT ha_state,
				 SaAmfCSIDescriptorT csi_desc)
{
	SaAisErrorT rc;

	rc = saAmfResponse(amf_hdl, inv, SA_AIS_OK);
	if (SA_AIS_OK != rc) {
		syslog(LOG_ERR, "saAmfResponse FAILED - %u", rc);
		exit(1);
	}
}

static void amf_csi_remove_callback(SaInvocationT inv, const SaNameT *comp_name,
				    const SaNameT *csi_name,
				    SaAmfCSIFlagsT csi_flags)
{
	SaAisErrorT rc;

	rc = saAmfResponse(amf_hdl, inv, SA_AIS_OK);
	if (SA_AIS_OK != rc) {
		syslog(LOG_ERR, "saAmfResponse FAILED - %u", rc);
		exit(1);
	}
}

static void amf_healthcheck_callback(SaInvocationT inv,
				     const SaNameT *comp_name,
				     SaAmfHealthcheckKeyT *health_check_key)
{
	SaAisErrorT rc;
	time_t local_time;
	static int healthcheck_count;
	struct tm *tstamp_data, tm_info;

	healthcheck_count++;

	/* Store latest health check trace */
	local_time = time(NULL);
	tzset();
	tstamp_data = localtime_r(&local_time, &tm_info);
	osafassert(tstamp_data);

	snprintf(latest_healthcheck_trace, sizeof(latest_healthcheck_trace),
		 "Last received healthcheck cnt=%d at %s", healthcheck_count,
		 asctime(tstamp_data));

	rc = saAmfResponse(amf_hdl, inv, SA_AIS_OK);
	if (SA_AIS_OK != rc) {
		syslog(LOG_ERR, "saAmfResponse FAILED - %u", rc);
		exit(1);
	}
}

static void amf_comp_terminate_callback(SaInvocationT inv,
					const SaNameT *comp_name)
{
	SaAisErrorT rc;
	int fd;

	fd = open(term_state_file, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);

	if (fd >= 0)
		(void)close(fd);
	else
		LOG_NO("cannot create termstate file %s: %s", term_state_file,
		       strerror(errno));

	rc = saAmfResponse(amf_hdl, inv, SA_AIS_OK);
	if (SA_AIS_OK != rc) {
		syslog(LOG_ERR, "saAmfResponse FAILED - %u", rc);
		exit(1);
	}

	daemon_exit();
}

static void amf_down_cb(void)
{
	const char *env_value = getenv("AMFWDOG_TIMEOUT_MS");

	if (env_value &&
	    !strtol(env_value, NULL, 0)) /* AMF watchdog disabled */
		syslog(LOG_ERR,
		       "AMF is down. AMF watchdog is disabled, no action");
	else { /* AMF watchdog enabled */
		opensaf_reboot(0, NULL, "AMF unexpectedly crashed");
	}
}

int main(int argc, char *argv[])
{
	SaAmfCallbacksT amf_callbacks = {0};
	SaVersionT ver = {
	    .releaseCode = 'B', .majorVersion = 0x01, .minorVersion = 0x01};
	SaAisErrorT rc;
	SaSelectionObjectT amf_sel_obj;
	struct pollfd fds[2];
	int poll_timeout = 60000; /* Default timeout is 60s */
	const char *env_value = getenv("AMFWDOG_TIMEOUT_MS");
	SaNameT comp_name;
	SaAmfHealthcheckKeyT hc_key;
	char *hc_key_env;
	int term_fd;

	opensaf_reboot_prepare();
	daemonize(argc, argv);

	ava_install_amf_down_cb(amf_down_cb);

	amf_callbacks.saAmfCSISetCallback = amf_csi_set_callback;
	amf_callbacks.saAmfCSIRemoveCallback = amf_csi_remove_callback;
	amf_callbacks.saAmfHealthcheckCallback = amf_healthcheck_callback;
	amf_callbacks.saAmfComponentTerminateCallback =
	    amf_comp_terminate_callback;

	rc = saAmfInitialize(&amf_hdl, &amf_callbacks, &ver);
	if (SA_AIS_OK != rc) {
		syslog(LOG_ERR, "saAmfInitialize FAILED %u", rc);
		goto done;
	}

	rc = saAmfSelectionObjectGet(amf_hdl, &amf_sel_obj);
	if (SA_AIS_OK != rc) {
		syslog(LOG_ERR, "saAmfSelectionObjectGet FAILED %u", rc);
		goto done;
	}
	daemon_sigterm_install(&term_fd);
	fds[0].fd = amf_sel_obj;
	fds[0].events = POLLIN;
	fds[1].fd = term_fd;
	fds[1].events = POLLIN;

	rc = saAmfComponentNameGet(amf_hdl, &comp_name);
	if (SA_AIS_OK != rc) {
		syslog(LOG_ERR, "saAmfComponentNameGet FAILED %u", rc);
		goto done;
	}

	syslog(LOG_INFO, "'%s' started", saAisNameBorrow(&comp_name));

	rc = saAmfComponentRegister(amf_hdl, &comp_name, 0);
	if (SA_AIS_OK != rc) {
		syslog(LOG_ERR, "saAmfComponentRegister FAILED %u", rc);
		goto done;
	}

	/* start the AMF health check */
	memset(&hc_key, 0, sizeof(hc_key));
	if ((hc_key_env = getenv("AMFWDOG_ENV_HEALTHCHECK_KEY")) == NULL)
		strcpy((char *)hc_key.key, "Default");
	else
		strcpy((char *)hc_key.key, hc_key_env);

	hc_key.keyLen = strlen((char *)hc_key.key);

	rc = saAmfHealthcheckStart(amf_hdl, &comp_name, &hc_key,
				   SA_AMF_HEALTHCHECK_AMF_INVOKED,
				   SA_AMF_COMPONENT_RESTART);
	if (SA_AIS_OK != rc) {
		syslog(LOG_ERR, "saAmfHealthcheckStart FAILED - %u", rc);
		goto done;
	}

	if (env_value) {
		poll_timeout = strtol(env_value, NULL,
				      0); /* Timeout value from env variable */
		if (poll_timeout <= 0)
			poll_timeout = -1; /* turn off timeout in poll */
	}

	while (1) {
		int res = poll(fds, 2, poll_timeout);

		if (res == -1) {
			if (errno == EINTR)
				continue;
			else {
				syslog(LOG_ERR, "poll FAILED - %s",
				       strerror(errno));
				goto done;
			}
		} else if (res == 0) { /* Timeout */
			int status;

			/*
			** Timeout receiving AMF healthcheck requests.
			** Generate core dump for AMF and reboot the node. We
			*generate * a core dump since we should never get here.
			*AMF cannot just * stop sending health check request
			*without it beeing an internal * error. We want to catch
			*that asap and fix it.
			*/
			syslog(
			    LOG_ERR,
			    "TIMEOUT receiving AMF health check request, generating core for amfnd");

			if (getuid() == 0 ||
			    geteuid() == 0) { /* running as a root user */
				if ((status = system(
					 "killall -ABRT osafamfnd")) == -1)
					syslog(
					    LOG_ERR,
					    "system(killall -ABRT osafamfnd) FAILED %x",
					    status);
			} else { /* running as the non-root user, default as the
				    'opensaf' user */
				if ((status = system(
					 "sudo killall -ABRT osafamfnd")) == -1)
					syslog(
					    LOG_ERR,
					    "system(sudo killall -ABRT osafamfnd) FAILED %x",
					    status);
			}

			syslog(LOG_ERR, "%s", latest_healthcheck_trace);
			opensaf_reboot(
			    0, NULL,
			    "AMFND unresponsive, AMFWDOG initiated system reboot");
		}

		if (fds[0].revents & POLLIN) {
			rc = saAmfDispatch(amf_hdl, SA_DISPATCH_ONE);
			if (SA_AIS_OK != rc) {
				syslog(LOG_ERR, "saAmfDispatch FAILED %u", rc);
				goto done;
			}
		}

		if (fds[1].revents & POLLIN) {
			daemon_exit();
		}
	}

done:
	return -1;
}
