/*      -*- OpenSAF  -*-
 *
 * (C) Copyright 2009 The OpenSAF Foundation
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
 * Author(s): Hewlett-Packard Development Company, L.P.
 *
 */

#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <syslog.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/eventfd.h>
#include "base/logtrace.h"
#include "plm/plmcd/plmc_lib_internal.h"
#include "plm/plmcd/plmc_cmds.h"

extern int plmc_read_config(char *plmc_config_file, PLMC_config_data *config);
extern FILE *plmc_lib_debug;
extern cb_functions callbacks;

/**********************************************************************
 * This is the function that carries out all the API calls
 *
 * Parameters:
 * ee_id => the ee_id to used to lookup the plmc
 * cb => the callback function that will be used by the plmc_client_mgr
 * cmd => the command to carry out
 *
 *	Returns:
 *	This function returns a 0 if successful and a 1 if not
 ***********************************************************************/
int do_command(char *ee_id, int (*cb)(tcp_msg *), char *cmd,
	       PLMC_cmd_idx cmd_enum)
{
	thread_entry *tentry;
	tentry = find_thread_entry(ee_id);

	if (tentry == NULL) {
		return (PLMC_API_EEID_NOT_FOUND);
	}
	/* Try to lock the entry */
	if (pthread_mutex_lock(&tentry->thread_d.td_lock) != 0) {
		syslog(LOG_ERR, "plmc_lib: encountered an error getting a "
				"lock for a client");
		return (PLMC_API_LOCK_FAILED);
	}

	/* Check if there is valid socket */
	if (tentry->thread_d.socketfd == 0) {
		if (pthread_mutex_unlock(&tentry->thread_d.td_lock) != 0) {
			syslog(LOG_ERR, "plmc_lib: encountered an error "
					"unlocking a mutex for a client");
			return (PLMC_API_UNLOCK_FAILED);
		}
		return (PLMC_API_NOT_CONNECTED);
	}

	strncpy(tentry->thread_d.command, cmd, PLMC_CMD_NAME_MAX_LENGTH);
	tentry->thread_d.command[PLMC_CMD_NAME_MAX_LENGTH - 1] = '\0';
	tentry->thread_d.callback = cb;

	plmc_client_mgr(tentry);

	/* Unlock */
	if (pthread_mutex_unlock(&tentry->thread_d.td_lock) != 0) {
		syslog(LOG_ERR, "plmc_lib: encountered an error unlocking a "
				"mutex for a client");
		return (PLMC_API_UNLOCK_FAILED);
	}
	return (PLMC_API_SUCCESS);
}

/**********************************************************************
 * plmc_lib_initialize - This is the function that needs to be
 * called to start the library. It will basically start up two
 * separate threads, the TCP listener thread (plmc_tcp_listener) and
 * the UDP listener thread (run_udp). One should call
 * plmc_lib_initialize only once during the library life cycle.
 *
 * Parameters:
 * cb1 => the callback function for new TCP connections
 * cb2 => the callback function for new UDP messages
 *
 *	Returns:
 *	This function returns a 0 if successful and a 1 if not
 ***********************************************************************/

int plmc_initialize(int (*connect_cb)(char *, char *), int (*udp_cb)(udp_msg *),
		    int (*err_cb)(plmc_lib_error *))
{
	pthread_attr_t attr;
	int retval;
#ifdef PLMC_LIB_DEBUG
	/* Open up the debug file */
	pid_t mypid;
	char buf[12];
	char debug_file[30];
	strcpy(debug_file, "/tmp/plmc_lib_debug.");
	mypid = getpid();
	sprintf(buf, "%d", mypid);
	strcat(debug_file, buf);
	plmc_lib_debug = fopen(debug_file, "w");
#endif

	plmc_config_file = getenv("PLMC_CONF_PATH");
	if (plmc_config_file == NULL)
		plmc_config_file = PLMC_CONFIG_FILE_LOC;

	retval = plmc_read_config(plmc_config_file, &config);

	/*
	Are without a conf file and need to do something about that?
	*/
	if (retval != 0) {
		syslog(LOG_ERR, "plmc_lib: Can't find a valid conf file %s",
		       plmc_config_file);
		syslog(LOG_ERR, "plmc_lib: errnor is %d", errno);
		return (PLMC_API_NO_CONF);
	}

	/* Set the struct with the callbacks */
	callbacks.connect_cb = connect_cb;
	callbacks.udp_cb = udp_cb;
	callbacks.err_cb = err_cb;

	pthread_attr_init(&attr);

	tcp_listener_stop_fd = eventfd(0, EFD_NONBLOCK);

	if (tcp_listener_stop_fd < 0) {
		LOG_ER("eventfd failed: %i", errno);
		return PLMC_API_FAILURE;
	}

	TRACE("tcp_listener_stop_fd: %i", tcp_listener_stop_fd);

	/* Create the threads */
	if (pthread_create(&(tcp_listener_id), &attr, plmc_tcp_listener,
			   NULL) != 0) {
		syslog(LOG_ERR, "plmc_lib: Could not create a new thread "
				"when initializing");
		pthread_attr_destroy(&attr);
		return (PLMC_API_FAILURE);
	}

	pthread_attr_destroy(&attr);
	return (PLMC_API_SUCCESS);
}
/* CHANGE - move all logic to a single function and call it from
 * the APIs
 */

/**********************************************************************
 * This function will return the protocol version that plmc is using
 *
 * Parameters:
 * ee_id => the ee_id to used to lookup the plmc
 * cb => the callback function that will be used by the plmc_client_mgr
 *
 *	Returns:
 *	This function returns a 0 if successful and a 1 if not
 ***********************************************************************/

int plmc_get_protocol_ver(char *ee_id, int (*cb)(tcp_msg *))
{
	return do_command(ee_id, cb, PLMC_cmd_name[PLMC_GET_PROTOCOL_VER_CMD],
			  PLMC_GET_PROTOCOL_VER_CMD);
}

/**********************************************************************
 * This function will return information about the OS that plmc is
 * running on
 *
 * Parameters:
 * ee_id => the ee_id to used to lookup the plmc
 * cb => the callback function that will be used by the plmc_client_mgr
 *
 *	Returns:
 *	This function returns a 0 if successful and a 1 if not
 ***********************************************************************/

int plmc_get_os_info(char *ee_id, int (*cb)(tcp_msg *))
{
	return do_command(ee_id, cb, PLMC_cmd_name[PLMC_GET_OSINFO_CMD],
			  PLMC_GET_OSINFO_CMD);
}

/**********************************************************************
 * This function will carry out an administrative
 * unlock operation on the PLMC
 *
 * Parameters:
 * ee_id => the ee_id to used to lookup the plmc
 * cb => the callback function that will be used by the plmc_client_mgr
 *
 *	Returns:
 *	This function returns a 0 if successful and a 1 if not
 ***********************************************************************/

int plmc_sa_plm_admin_unlock(char *ee_id, int (*cb)(tcp_msg *))
{
	return do_command(ee_id, cb,
			  PLMC_cmd_name[PLMC_SA_PLM_ADMIN_UNLOCK_CMD],
			  PLMC_SA_PLM_ADMIN_UNLOCK_CMD);
}

/**********************************************************************
 * This function will carry out an administrative
 * lock instantiation operation on the PLMC
 *
 * Parameters:
 * ee_id => the ee_id to used to lookup the plmc
 * cb => the callback function that will be used by the plmc_client_mgr
 *
 *	Returns:
 *	This function returns a 0 if successful and a 1 if not
 ***********************************************************************/

int plmc_sa_plm_admin_lock_instantiation(char *ee_id, int (*cb)(tcp_msg *))
{
	return do_command(
	    ee_id, cb, PLMC_cmd_name[PLMC_SA_PLM_ADMIN_LOCK_INSTANTIATION_CMD],
	    PLMC_SA_PLM_ADMIN_LOCK_INSTANTIATION_CMD);
}

/**********************************************************************
 * This function will carry out an administrative
 * lock operation on the PLMC
 *
 * Parameters:
 * ee_id => the ee_id to used to lookup the plmc
 * cb => the callback function that will be used by the plmc_client_mgr
 *
 *	Returns:
 *	This function returns a 0 if successful and a 1 if not
 ***********************************************************************/

int plmc_sa_plm_admin_lock(char *ee_id, int (*cb)(tcp_msg *))
{
	return do_command(ee_id, cb, PLMC_cmd_name[PLMC_SA_PLM_ADMIN_LOCK_CMD],
			  PLMC_SA_PLM_ADMIN_LOCK_CMD);
}

/**********************************************************************
 * This function will carry out an administrative
 * restart operation on the PLMC
 *
 * Parameters:
 * ee_id => the ee_id to used to lookup the plmc
 * cb => the callback function that will be used by the plmc_client_mgr
 *
 *	Returns:
 *	This function returns a 0 if successful and a 1 if not
 ***********************************************************************/

int plmc_sa_plm_admin_restart(char *ee_id, int (*cb)(tcp_msg *))
{
	return do_command(ee_id, cb,
			  PLMC_cmd_name[PLMC_SA_PLM_ADMIN_RESTART_CMD],
			  PLMC_SA_PLM_ADMIN_RESTART_CMD);
}

/**********************************************************************
 * This function will start OpenSAF on the system
 * that PLMC is running on
 *
 * Parameters:
 * ee_id => the ee_id to used to lookup the plmc
 * cb => the callback function that will be used by the plmc_client_mgr
 *
 *	Returns:
 *	This function returns a 0 if successful and a 1 if not
 ***********************************************************************/

int plmc_osaf_start(char *ee_id, int (*cb)(tcp_msg *))
{
	return do_command(ee_id, cb, PLMC_cmd_name[PLMC_OSAF_START_CMD],
			  PLMC_OSAF_START_CMD);
}

/**********************************************************************
 * This function will stop OpenSAF on the system  that PLMC is running
 * on
 *
 * Parameters:
 * ee_id => the ee_id to used to lookup the plmc
 * cb => the callback function that will be used by the plmc_client_mgr
 *
 *	Returns:
 *	This function returns a 0 if successful and a 1 if not
 ***********************************************************************/

int plmc_osaf_stop(char *ee_id, int (*cb)(tcp_msg *))
{
	return do_command(ee_id, cb, PLMC_cmd_name[PLMC_OSAF_STOP_CMD],
			  PLMC_OSAF_STOP_CMD);
}

/**********************************************************************
 * This function will start the service (exluding OpenSAF) on the system
 * that PLMC is running on
 *
 * Parameters:
 * ee_id => the ee_id to used to lookup the plmc
 * cb => the callback function that will be used by the plmc_client_mgr
 *
 *	Returns:
 *	This function returns a 0 if successful and a 1 if not
 ***********************************************************************/

int plmc_osaf_services_start(char *ee_id, int (*cb)(tcp_msg *))
{
	return do_command(ee_id, cb,
			  PLMC_cmd_name[PLMC_OSAF_SERVICES_START_CMD],
			  PLMC_OSAF_SERVICES_START_CMD);
}

/**********************************************************************
 * This function will stop the service (exluding OpenSAF) on the system
 * that PLMC is running on
 *
 * Parameters:
 * ee_id => the ee_id to used to lookup the plmc
 * cb => the callback function that will be used by the plmc_client_mgr
 *
 *	Returns:
 *	This function returns a 0 if successful and a 1 if not
 ***********************************************************************/

int plmc_osaf_services_stop(char *ee_id, int (*cb)(tcp_msg *))
{
	return do_command(ee_id, cb, PLMC_cmd_name[PLMC_OSAF_SERVICES_STOP_CMD],
			  PLMC_OSAF_SERVICES_STOP_CMD);
}

/**********************************************************************
 * This function will restart PLMC
 *
 * Parameters:
 * ee_id => the ee_id to used to lookup the plmc
 * cb => the callback function that will be used by the plmc_client_mgr
 *
 *	Returns:
 *	This function returns a 0 if successful and a 1 if not
 ***********************************************************************/

int plmc_plmcd_restart(char *ee_id, int (*cb)(tcp_msg *))
{
	return do_command(ee_id, cb, PLMC_cmd_name[PLMC_PLMCD_RESTART_CMD],
			  PLMC_PLMCD_RESTART_CMD);
}

/**********************************************************************
 * This function will destroy the library
 *
 * Parameters:
 * No parameters
 *
 *	Returns:
 *	This function returns a 0 if successful and a 1 if not
 ***********************************************************************/

int plmc_destroy()
{
	int rc = 0, s = 0;
	uint64_t stop = 1;
	TRACE_ENTER();

	s = write(udp_listener_stop_fd, &stop, sizeof(stop));

	if (s < 0) {
		LOG_ER("write failed: %i: stopping udp_listener thread", errno);
		rc = 1;
	} else {
		TRACE("waiting for udp thread to stop");
		rc = pthread_join(udp_listener_id, 0);
		if (rc != 0)
			LOG_ER("joining udp thread failed: %i", rc);
		else
			TRACE("udp thread stopped");
	}

	s = write(tcp_listener_stop_fd, &stop, sizeof(stop));

	if (s < 0) {
		LOG_ER("write failed: %i: stopping tcp_listener thread", errno);
		rc = 1;
	} else {
		TRACE("waiting for tcp thread to stop");
		rc = pthread_join(tcp_listener_id, 0);
		if (rc != 0)
			LOG_ER("joining tcp thread failed: %i", rc);
		else
			TRACE("tcp thread stopped");
	}

#ifdef PLMC_LIB_DEBUG
	fprintf(plmc_lib_debug, "Closing the debug file\n");
	/* Close the debug file */
	sleep(2);
	fclose(plmc_lib_debug);
#endif
	TRACE_LEAVE2("rc: %i", rc);
	return rc;
}
