/*      -*- OpenSAF  -*-
 *
 * (C) Copyright 2010 The OpenSAF Foundation
 * Copyright Ericsson AB 2017 - All Rights Reserved.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. This file and program are licensed
 * under the GNU Lesser General Public License Version 2.1, February 1999.
 * The complete license can be accessed from the following location:
 * http://opensource.org/licenses/lgpl-license.php
 * See the Copying file included with the OpenSAF distribution for full
 * licensing terms.z
 *
 * Author(s): GoAhead Software
 *
 */

/* ========================================================================
 *   INCLUDE FILES
 * ========================================================================
 */

#include <sched.h>
#include <unistd.h>
#include <cstdlib>
#include "base/daemon.h"
#include "base/ncs_main_papi.h"
#include "base/ncsencdec_pub.h"
#include "base/osaf_poll.h"
#include "base/time.h"
#include "dtm/dtmnd/multicast.h"
#include "dtm/dtmnd/dtm.h"
#include "dtm/dtmnd/dtm_node.h"
#include "nid/agent/nid_api.h"
#include "osaf/configmake.h"

/* ========================================================================
 *   DEFINITIONS
 * ========================================================================
 */

/* ========================================================================
 *   TYPE DEFINITIONS
 * ========================================================================
 */

#define DTM_CONFIG_FILE PKGSYSCONFDIR "/dtmd.conf"

/* ========================================================================
 *   DATA DECLARATIONS
 * ========================================================================
 */

NCSCONTEXT gl_node_dis_task_hdl = nullptr;
NCSCONTEXT gl_serv_dis_task_hdl = nullptr;

DTM_INTERNODE_CB *dtms_gl_cb = nullptr;

bool initial_discovery_phase = true;

/* ========================================================================
 *   FUNCTION PROTOTYPES
 * ========================================================================
 */

DTM_INTERNODE_CB::DTM_INTERNODE_CB()
    : multicast_{},
      cluster_id{},
      node_id{},
      node_name{},
      ip_addr{},
      mcast_addr{},
      bcast_addr{},
      ifname{},
      scope_link{},
      stream_port{},
      dgram_port_sndr{},
      dgram_port_rcvr{},
      stream_sock{},
      i_addr_family{},
      initial_dis_timeout{},
      cont_bcast_int{},
      bcast_msg_freq{},
      nodeid_tree{},
      ip_addr_tree{},
      so_keepalive{},
      cb_lock{},
      comm_keepidle_time{},
      comm_keepalive_intvl{},
      comm_keepalive_probes{},
      comm_user_timeout{},
      sock_sndbuf_size{},
      sock_rcvbuf_size{},
      mbx{},
      mbx_fd{},
      epoll_fd{} {}

DTM_INTERNODE_CB::~DTM_INTERNODE_CB() { delete multicast_; }

/**
 * Function to init the dtm process
 *
 * @param dtms_cb
 *
 * @return NCSCC_RC_SUCCESS
 * @return NCSCC_RC_FAILURE
 *
 */
static uint32_t dtm_init(DTM_INTERNODE_CB *dtms_cb) {
  uint32_t rc = NCSCC_RC_SUCCESS;

  TRACE_ENTER();

  if (ncs_leap_startup() != NCSCC_RC_SUCCESS) {
    LOG_ER("DTM: LEAP svcs startup failed \n");
    rc = NCSCC_RC_FAILURE;
    goto done;
  }

  /* Initialize  control block */
  if ((rc = dtm_cb_init(dtms_cb)) != NCSCC_RC_SUCCESS) {
    rc = NCSCC_RC_FAILURE;
    LOG_ER("DTM: dtm_cb_init FAILED");
    goto done;
  }

done:

  TRACE_LEAVE2("rc : %d", rc);
  return rc;
}

/**
 * Function to destroy node discovery thread
 *
 *
 * @return NCSCC_RC_SUCCESS
 * @return NCSCC_RC_FAILURE
 *
 */
static uint32_t dtm_destroy_node_discovery_task() {
  TRACE_ENTER();

  if (m_NCS_TASK_STOP(gl_node_dis_task_hdl) != NCSCC_RC_SUCCESS) {
    LOG_ER("DTM: Stop of the Created Task-failed:gl_node_dis_task_hdl  \n");
  }

  if (m_NCS_TASK_RELEASE(gl_node_dis_task_hdl) != NCSCC_RC_SUCCESS) {
    LOG_ER("DTM: Stop of the Created Task-failed:gl_node_dis_task_hdl \n");
  }

  TRACE_LEAVE();
  return NCSCC_RC_SUCCESS;
}

/**
 * Function to destroy service discovery thread
 *
 *
 * @return NCSCC_RC_SUCCESS
 * @return NCSCC_RC_FAILURE
 *
 */
static uint32_t dtm_destroy_service_discovery_task() {
  TRACE_ENTER();

  if (m_NCS_TASK_STOP(gl_serv_dis_task_hdl) != NCSCC_RC_SUCCESS) {
    LOG_ER("DTM: Stop of the Created Task-failed:gl_serv_dis_task_hdl  \n");
  }

  if (m_NCS_TASK_RELEASE(gl_serv_dis_task_hdl) != NCSCC_RC_SUCCESS) {
    LOG_ER("DTM: Stop of the Created Task-failed:gl_serv_dis_task_hdl \n");
  }

  TRACE_LEAVE();
  return NCSCC_RC_SUCCESS;
}

/**
 * Function to create & start node_discovery task
 *
 *
 * @return NCSCC_RC_SUCCESS
 * @return NCSCC_RC_FAILURE
 *
 */
uint32_t dtm_node_discovery_task_create() {
  TRACE_ENTER();

  int policy = SCHED_RR; /*root defaults */
  int max_prio = sched_get_priority_max(policy);
  int min_prio = sched_get_priority_min(policy);
  int prio_val = ((max_prio - min_prio) * 0.87);

  uint32_t rc = ncs_task_create(
      node_discovery_process, nullptr, "OSAF_NODE_DISCOVERY", prio_val, policy,
      m_NODE_DISCOVERY_STACKSIZE, &gl_node_dis_task_hdl);
  if (NCSCC_RC_SUCCESS != rc) {
    LOG_ER("DTM: node_discovery thread CREATE failed");
    goto err;
  }

  TRACE("DTM: Created node_discovery thread ");

  /* now start the task */
  rc = m_NCS_TASK_START(gl_node_dis_task_hdl);
  if (NCSCC_RC_SUCCESS != rc) {
    LOG_ER("DTM: node_discovery thread START failed : gl_node_dis_task_hdl ");
    goto err;
  }

  TRACE("DTM: Started node_discovery thread");
  TRACE_LEAVE2("rc : %d", rc);
  return rc;

err:
  /* destroy the task */
  if (gl_node_dis_task_hdl) {
    /* release the task */
    m_NCS_TASK_RELEASE(gl_node_dis_task_hdl);

    gl_node_dis_task_hdl = nullptr;
  } else {
    /* Detach this thread and allow it to have its own life. This
       thread is going to exit on its own This macro is going to
       release the refecences to this thread in the LEAP */
    m_NCS_TASK_DETACH(gl_node_dis_task_hdl);
  }

  TRACE_LEAVE2("rc : %d", rc);
  return rc;
}

/**
 *  DTM process main function
 *
 *
 * @return NCSCC_RC_SUCCESS
 * @return NCSCC_RC_FAILURE
 *
 */
int main(int argc, char *argv[]) {
  int rc = -1;
  long int dis_time_out_usec = 0;
  int64_t dis_elapsed_time_usec = 0;

  TRACE_ENTER();

  daemonize(argc, argv);

  /*************************************************************/
  /* Set up CB stuff  */
  /*************************************************************/

  dtms_gl_cb = new DTM_INTERNODE_CB;
  DTM_INTERNODE_CB *dtms_cb = dtms_gl_cb;

  if (dtms_cb == nullptr || dtm_init(dtms_cb) != NCSCC_RC_SUCCESS) {
    LOG_ER("DTM: dtm_init failed");
    goto done3;
  }

  rc = dtm_read_config(dtms_cb, DTM_CONFIG_FILE);

  if (rc != 0) {
    LOG_ER("DTM:Error reading %s.  errno : %d", DTM_CONFIG_FILE, rc);
    goto done3;
  }

  /*************************************************************/
  /* Set up the initial bcast or mcast sender socket */
  /*************************************************************/
  dtms_cb->multicast_ = new Multicast{
      dtms_cb->cluster_id,      dtms_cb->node_id,       dtms_cb->stream_port,
      dtms_cb->dgram_port_rcvr, dtms_cb->i_addr_family, dtms_cb->ip_addr,
      dtms_cb->bcast_addr,      dtms_cb->mcast_addr,    dtms_cb->ifname,
      dtms_cb->scope_link};
  if (dtms_cb->multicast_ == nullptr || dtms_cb->multicast_->fd() < 0) {
    LOG_ER("Failed to initialize Multicast instance");
    goto done3;
  }

  /*************************************************************/
  /* Set up the initial node_discovery_task  */
  /*************************************************************/
  rc = dtm_node_discovery_task_create();
  if (NCSCC_RC_SUCCESS != rc) {
    LOG_ER("DTM: node_discovery thread CREATE failed rc : %d ", rc);
    goto done2;
  }

  /*************************************************************/
  /* Set up the initialservice_discovery_task */
  /*************************************************************/
  rc = dtm_service_discovery_init(dtms_cb);
  if (NCSCC_RC_SUCCESS != rc) {
    LOG_ER("DTM:service_discovery thread CREATE failed rc : %d ", rc);
    goto done1;
  }

  rc = nid_notify("TRANSPORT", NCSCC_RC_SUCCESS, nullptr);
  if (NCSCC_RC_SUCCESS != rc) {
    LOG_ER("TRANSPORT: nid_notify failed rc : %d ", rc);
    goto done1;
  }
  dis_time_out_usec = (dtms_cb->initial_dis_timeout * 1000000);

  do {
    /* Wait up to bcast_msg_freq seconds. */
    /* Check if stdin has input. */
    base::Sleep(base::MillisToTimespec(dtms_cb->bcast_msg_freq));

    /* Broadcast msg string in datagram to clients every 250  m
     * seconds */
    dtms_cb->multicast_->Send();

    dis_elapsed_time_usec =
        dis_elapsed_time_usec + (dtms_cb->bcast_msg_freq * 1000);

  } while (dis_elapsed_time_usec <= dis_time_out_usec);
  /*************************************************************/
  /* Keep waiting forever */
  /*************************************************************/
  initial_discovery_phase = false;
  while (1) {
    if (dtms_cb->cont_bcast_int) {
      m_NCS_TASK_SLEEP(dtms_cb->cont_bcast_int);
      /* periodically send a broadcast */
      dtms_cb->multicast_->Send();
    } else {
      for (;;) pause();
    }
  }
done1:
  LOG_ER("DTM : dtm_destroy_service_discovery_task exiting...");
  /* Destroy node_discovery_task  task */
  rc = dtm_destroy_service_discovery_task();
  if (NCSCC_RC_SUCCESS != rc) {
    LOG_ER("DTM: service_discover Task Destruction Failed rc : %d \n", rc);
  }
done2:
  /* Destroy receiving task */
  rc = dtm_destroy_node_discovery_task();
  if (NCSCC_RC_SUCCESS != rc) {
    LOG_ER("DTM: node_discovery Task Destruction Failed rc : %d \n", rc);
  }

done3:
  delete dtms_cb;
  TRACE_LEAVE();
  (void)nid_notify("TRANSPORT", NCSCC_RC_FAILURE, nullptr);
  exit(1);
}
