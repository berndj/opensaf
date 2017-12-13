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

#ifndef PLM_PLMCD_PLMC_LIB_INTERNAL_H_
#define PLM_PLMCD_PLMC_LIB_INTERNAL_H_

#include <pthread.h>
#include "plm/plmcd/plmc_lib.h"
#include "plm/plmcd/plmc.h"

#define PLMC_CONN_CB_MSG "CONNECTED"
#define PLMC_DISCONN_CB_MSG "DISCONNECTED"

typedef struct cb_functions_struct {
  int (*connect_cb)(char *, char *);
  int (*udp_cb)(udp_msg *);
  int (*err_cb)(plmc_lib_error *);
} cb_functions;

extern char *plmc_config_file;
extern PLMC_config_data config;
extern pthread_t tcp_listener_id, udp_listener_id;
extern int tcp_listener_stop_fd, udp_listener_stop_fd;

/********************************************************************
 * This struct is used for the data entry that a client_mgr thread
 * is using to carry out a command
 *
 ********************************************************************/
typedef struct thread_data_struct {
  pthread_mutex_t td_lock; /* single thread lock */
  char ee_id[PLMC_EE_ID_MAX_LENGTH];
  char command[PLMC_CMD_NAME_MAX_LENGTH];
  int (*callback)(tcp_msg *);
  int socketfd;
} thread_data;

typedef struct thread_entry_struct thread_entry;

/********************************************************************
 * This struct is used for a list of thread_datas
 *
 ********************************************************************/
struct thread_entry_struct {
  thread_data thread_d;
  thread_entry *next;
  thread_entry *previous;
};

thread_entry *find_thread_entry(const char *ee_id);
void delete_all_thread_entries();
void *plmc_tcp_listener(void *arguments);
void plmc_client_mgr(thread_entry *);
int send_error(int error, int action, char *ee_id, PLMC_cmd_idx cmd_enum);
#endif  // PLM_PLMCD_PLMC_LIB_INTERNAL_H_
