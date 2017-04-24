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

#include "log/agent/lga_util.h"
#include <stdlib.h>
#include <syslog.h>
#include "base/ncs_hdl_pub.h"
#include "base/ncs_main_papi.h"
#include "base/osaf_poll.h"
#include "log/agent/lga_state.h"
#include "log/agent/lga.h"
#include "log/agent/lga_mds.h"
#include "base/osaf_extended_name.h"

/* Variables used during startup/shutdown only */
static pthread_mutex_t lga_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned int lga_use_count;

/**
 * @return unsigned int
 */
static unsigned int lga_create(void) {
  unsigned int rc = NCSCC_RC_SUCCESS;

  /* create and init sel obj for mds sync */
  m_NCS_SEL_OBJ_CREATE(&lga_cb.lgs_sync_sel);
  lga_cb.lgs_sync_awaited = 1;

  /* register with MDS */
  if ((NCSCC_RC_SUCCESS != (rc = lga_mds_init(&lga_cb)))) {
    rc = NCSCC_RC_FAILURE;
    goto error;
  }

  /* Block and wait for indication from MDS meaning LGS is up */

  /* #1179 Change timeout from 30 sec (30000) to 10 sec (10000)
   * 30 sec is probably too long for a synchronous API function
   */
  osaf_poll_one_fd(m_GET_FD_FROM_SEL_OBJ(lga_cb.lgs_sync_sel), 10000);

  osaf_mutex_lock_ordie(&lga_cb.cb_lock);
  lga_cb.lgs_sync_awaited = 0;
  lga_cb.clm_node_state = SA_CLM_NODE_JOINED;
  osaf_mutex_unlock_ordie(&lga_cb.cb_lock);

  /* No longer needed */
  m_NCS_SEL_OBJ_DESTROY(&lga_cb.lgs_sync_sel);

  return rc;

error:
  /* delete the lga init instances */
  lga_hdl_list_del(&lga_cb.client_list);

  return rc;
}

/**
 *
 *
 * @return unsigned int
 */
static void lga_destroy(void) {
  TRACE_ENTER();

  /* delete the hdl db */
  lga_hdl_list_del(&lga_cb.client_list);

  /* unregister with MDS */
  lga_mds_finalize(&lga_cb);
}

/****************************************************************************
 * Name          : lga_clear_mbx
 *
 * Description   : This is the function which deletes all the messages from
 *                 the mail box.
 *
 * Arguments     : arg     - argument to be passed.
 *                 msg     - Event start pointer.
 *
 * Return Values : true/false
 *
 * Notes         : None.
 *****************************************************************************/
static bool lga_clear_mbx(NCSCONTEXT arg, NCSCONTEXT msg) {
  lgsv_msg_t *cbk, *pnext;

  pnext = cbk = static_cast<lgsv_msg_t *>(msg);
  while (pnext) {
    pnext = cbk->next;
    lga_msg_destroy(cbk);
    cbk = pnext;
  }
  return true;
}

/****************************************************************************
  Name          : lga_log_stream_hdl_rec_list_del

  Description   : This routine deletes a list of log stream records.

  Arguments     : pointer to the list of log stream records anchor.

  Return Values : None

  Notes         :
******************************************************************************/
static void lga_log_stream_hdl_rec_list_del(
    lga_log_stream_hdl_rec_t **plstr_hdl) {
  lga_log_stream_hdl_rec_t *lstr_hdl;
  TRACE_ENTER();
  while ((lstr_hdl = *plstr_hdl) != nullptr) {
    TRACE("%s stream \"%s\", hdl = %d",
          __FUNCTION__, lstr_hdl->log_stream_name != nullptr ?
          (lstr_hdl->log_stream_name) : ("nullptr"), lstr_hdl->log_stream_hdl);
    *plstr_hdl = lstr_hdl->next;
    ncshm_destroy_hdl(NCS_SERVICE_ID_LGA, lstr_hdl->log_stream_hdl);

    /* Check nullptr to avoid the case: initialize -> finalize */
    if (lstr_hdl->log_stream_name != nullptr) {
      free(lstr_hdl->log_stream_name);
      lstr_hdl->log_stream_name = nullptr;
    }

    free(lstr_hdl);
    lstr_hdl = nullptr;
  }
  TRACE_LEAVE();
}

/****************************************************************************
  Name          : lga_hdl_cbk_rec_prc

  Description   : This routine invokes the registered callback routine & frees
                  the callback record resources.

  Arguments     : cb      - ptr to the LGA control block
                  msg     - ptr to the callback message
                  reg_cbk - ptr to the registered callbacks

  Return Values : None

  Notes         : None
******************************************************************************/
static void lga_hdl_cbk_rec_prc(
    lga_cb_t *cb, lgsv_msg_t *msg, SaLogCallbacksT *reg_cbk) {
  lgsv_cbk_info_t *cbk_info = &msg->info.cbk_info;

  /* invoke the corresponding callback */
  switch (cbk_info->type) {
    case LGSV_WRITE_LOG_CALLBACK_IND:
      {
        if (reg_cbk->saLogWriteLogCallback)
          reg_cbk->saLogWriteLogCallback(
              cbk_info->inv, cbk_info->write_cbk.error);
      }
      break;

    case LGSV_SEVERITY_FILTER_CALLBACK:
      {
        osaf_mutex_lock_ordie(&cb->cb_lock);
        if (reg_cbk->saLogFilterSetCallback) {
          lga_log_stream_hdl_rec_t *lga_str_hdl_rec =
              lga_find_stream_hdl_rec_by_regid(cb, cbk_info->lgs_client_id,
                                               cbk_info->lgs_stream_id);
          if (lga_str_hdl_rec != nullptr)
            reg_cbk->saLogFilterSetCallback(
                lga_str_hdl_rec->log_stream_hdl,
                cbk_info->serverity_filter_cbk.log_severity);
        }
        osaf_mutex_unlock_ordie(&cb->cb_lock);
      }
      break;

    default:
      TRACE("unknown callback type: %d", cbk_info->type);
      break;
  }
}

/****************************************************************************
  Name          : lga_hdl_cbk_dispatch_one

  Description   : This routine dispatches one pending callback.

  Arguments     : cb      - ptr to the LGA control block
                  hdl_rec - ptr to the handle record

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None
******************************************************************************/
static SaAisErrorT lga_hdl_cbk_dispatch_one(
    lga_cb_t *cb, lga_client_hdl_rec_t *hdl_rec) {
  lgsv_msg_t *cbk_msg;
  SaAisErrorT rc = SA_AIS_OK;

  /* Nonblk receive to obtain the message from priority queue */
  while (nullptr != (cbk_msg = reinterpret_cast<lgsv_msg_t *>(
             m_NCS_IPC_NON_BLK_RECEIVE(&hdl_rec->mbx, cbk_msg)))) {
    if (cbk_msg->info.cbk_info.type == LGSV_WRITE_LOG_CALLBACK_IND ||
        cbk_msg->info.cbk_info.type == LGSV_SEVERITY_FILTER_CALLBACK) {
      lga_hdl_cbk_rec_prc(cb, cbk_msg, &hdl_rec->reg_cbk);
      lga_msg_destroy(cbk_msg);
      break;
    } else {
      TRACE("Unsupported callback type = %d", cbk_msg->info.cbk_info.type);
      rc = SA_AIS_ERR_LIBRARY;
      lga_msg_destroy(cbk_msg);
    }
  }

  return rc;
}

/****************************************************************************
  Name          : lga_hdl_cbk_dispatch_all

  Description   : This routine dispatches all the pending callbacks.

  Arguments     : cb      - ptr to the LGA control block
                  hdl_rec - ptr to the handle record

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None
******************************************************************************/
static SaAisErrorT lga_hdl_cbk_dispatch_all(
    lga_cb_t *cb, lga_client_hdl_rec_t *hdl_rec) {
  lgsv_msg_t *cbk_msg;
  SaAisErrorT rc = SA_AIS_OK;

  /* Recv all the cbk notifications from the queue & process them */
  do {
    if (nullptr == (cbk_msg = reinterpret_cast<lgsv_msg_t *>(
            m_NCS_IPC_NON_BLK_RECEIVE(&hdl_rec->mbx, cbk_msg))))
      break;
    if (cbk_msg->info.cbk_info.type == LGSV_WRITE_LOG_CALLBACK_IND ||
        cbk_msg->info.cbk_info.type == LGSV_SEVERITY_FILTER_CALLBACK) {
      TRACE_2("LGSV_LGS_DELIVER_EVENT");
      lga_hdl_cbk_rec_prc(cb, cbk_msg, &hdl_rec->reg_cbk);
    } else {
      TRACE("unsupported callback type %d", cbk_msg->info.cbk_info.type);
    }
    /* now that we are done with this rec, free the resources */
    lga_msg_destroy(cbk_msg);
  } while (1);

  return rc;
}

/****************************************************************************
  Name          : lga_hdl_cbk_dispatch_block

  Description   : This routine blocks forever for receiving indications from
                  LGS. The routine returns when saEvtFinalize is executed on
                  the handle.

  Arguments     : cb      - ptr to the LGA control block
                  hdl_rec - ptr to the handle record

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None
******************************************************************************/
static SaAisErrorT lga_hdl_cbk_dispatch_block(
    lga_cb_t *cb, lga_client_hdl_rec_t *hdl_rec) {
  lgsv_msg_t *cbk_msg;
  SaAisErrorT rc = SA_AIS_OK;

  for (;;) {
    if (nullptr != (cbk_msg = reinterpret_cast<lgsv_msg_t *>(
            m_NCS_IPC_RECEIVE(&hdl_rec->mbx, cbk_msg)))) {
      if (cbk_msg->info.cbk_info.type == LGSV_WRITE_LOG_CALLBACK_IND ||
          cbk_msg->info.cbk_info.type == LGSV_SEVERITY_FILTER_CALLBACK) {
        TRACE_2("LGSV_LGS_DELIVER_EVENT");
        lga_hdl_cbk_rec_prc(cb, cbk_msg, &hdl_rec->reg_cbk);
      } else {
        TRACE("unsupported callback type %d", cbk_msg->info.cbk_info.type);
      }
      lga_msg_destroy(cbk_msg);
    } else {
      /* FIX to handle finalize clean up of mbx */
      return rc;
    }
  }
  return rc;
}

/**
 * Free the memory allocated for one client handle with mutex protection.
 *
 * @param p_client_hdl
 * @return void
 */
static void lga_free_client_hdl(lga_client_hdl_rec_t **p_client_hdl) {
  /* Synchronize b/w client & mds thread */
  osaf_mutex_lock_ordie(&lga_cb.cb_lock);

  lga_client_hdl_rec_t *client_hdl = *p_client_hdl;
  if (client_hdl == nullptr) goto done;

  free(client_hdl);
  client_hdl = nullptr;

done:
  osaf_mutex_unlock_ordie(&lga_cb.cb_lock);
}

/**
 * Initiate the agent when first used.
 * Start NCS service
 * Register with MDS
 *
 * @return unsigned int
 */
unsigned int lga_startup(lga_cb_t *cb) {
  unsigned int rc = NCSCC_RC_SUCCESS;
  osaf_mutex_lock_ordie(&lga_lock);

  TRACE_ENTER2("lga_use_count: %u", lga_use_count);

  if (cb->mds_hdl == 0) {
    if ((rc = ncs_agents_startup()) != NCSCC_RC_SUCCESS) {
      TRACE("ncs_agents_startup FAILED");
      goto done;
    }

    if ((rc = lga_create()) != NCSCC_RC_SUCCESS) {
      cb->mds_hdl = 0;
      ncs_agents_shutdown();
      goto done;
    }

    /* Agent has successfully been started including communication
     * with server
     */
    set_lga_state(LGA_NORMAL);
  }

  /* Increase the use_count */
  lga_use_count++;

done:
  osaf_mutex_unlock_ordie(&lga_lock);

  TRACE_LEAVE2("rc: %u, lga_use_count: %u", rc, lga_use_count);
  return rc;
}

/**
 * If called when only one (the last) client for this agent the client list a
 * complete 'shut down' of the agent is done.
 *  - Erase the clients list. Frees all memory including list of open
 *    streams.
 *  - Unregister with MDS
 *  - Shut down ncs agents
 *
 * Global lga_use_count Conatins number of registered clients
 *
 * @return unsigned int (always NCSCC_RC_SUCCESS)
 */
unsigned int lga_shutdown_after_last_client(void) {
  unsigned int rc = NCSCC_RC_SUCCESS;

  TRACE_ENTER2("lga_use_count: %u", lga_use_count);
  osaf_mutex_lock_ordie(&lga_lock);

  if (lga_use_count > 1) {
    /* Users still exist, just decrement the use count */
    lga_use_count--;
  } else if (lga_use_count == 1) {
    lga_use_count = 0;
  }

  osaf_mutex_unlock_ordie(&lga_lock);

  TRACE_LEAVE2("rc: %u, lga_use_count: %u", rc, lga_use_count);
  return rc;
}

/**
 * Makes a forced shut down of the agent if there are registered clients
 * Makes all handles invalid and frees all resources (memory, mds)
 *
 * clients and the log server is down and no other recovery is possible.
 *  - Erase the clients list. Frees all memory including list of open
 *    streams.
 *  - Unregister with MDS
 *  - Shut down ncs agents
 *  - Set
 *
 * Global lga_use_count [in/out] = 0
 *
 * @return always NCSCC_RC_SUCCESS
 */
unsigned int lga_force_shutdown(void) {
  unsigned int rc = NCSCC_RC_SUCCESS;
  TRACE_ENTER();
  osaf_mutex_lock_ordie(&lga_lock);
  if (lga_use_count > 0) {
    lga_destroy();
    rc = ncs_agents_shutdown(); /* Always returns NCSCC_RC_SUCCESS */
    lga_use_count = 0;
    TRACE("%s: Forced shutdown. Handles invalidated\n", __FUNCTION__);
  }
  osaf_mutex_unlock_ordie(&lga_lock);
  TRACE_LEAVE();
  return rc;
}


/****************************************************************************
  Name          : lga_find_hdl_rec_by_regid

  Description   : This routine looks up a lga_client_hdl_rec by client_id

  Arguments     : cb      - ptr to the LGA control block
                  client_id  - cluster wide unique allocated by LGS

  Return Values : LGA_CLIENT_HDL_REC * or nullptr

  Notes         : The lga_cb in-parameter is most likely pointing to the global
  *                lga_cb structure and that is not thread safe. If that is the
  *                case the lga_cb data must be protected by a mutex before
  *                calling this function.
  *
  ******************************************************************************/
lga_client_hdl_rec_t *lga_find_hdl_rec_by_regid(
    lga_cb_t *lga_cb, uint32_t client_id) {
  lga_client_hdl_rec_t *lga_hdl_rec;

  for (lga_hdl_rec = lga_cb->client_list; lga_hdl_rec != nullptr;
       lga_hdl_rec = lga_hdl_rec->next) {
    if (lga_hdl_rec->lgs_client_id == client_id)
      return lga_hdl_rec;
  }

  return nullptr;
}

/****************************************************************************
  Name          : lga_find_stream_hdl_rec_by_regid

  Description   : This routine looks up a lga_log_stream_hdl_rec by client_id
                  and stream_id

  Arguments     : cb
                  client_id
                  stream_id

  Return Values : LGA_LOG_STREAM_HDL_REC * or nullptr

  Notes         : The lga_cb in-parameter is most likely pointing to the global
                  lga_cb structure and that is not thread safe. If that is the
                  case the lga_cb data must be protected by a mutex before
                  calling this function.

******************************************************************************/
lga_log_stream_hdl_rec_t *lga_find_stream_hdl_rec_by_regid(
    lga_cb_t *lga_cb,
    uint32_t client_id, uint32_t stream_id) {
  TRACE_ENTER();
  lga_client_hdl_rec_t *lga_hdl_rec =
      lga_find_hdl_rec_by_regid(lga_cb, client_id);

  if (lga_hdl_rec != nullptr) {
    lga_log_stream_hdl_rec_t *lga_str_hdl_rec = lga_hdl_rec->stream_list;

    while (lga_str_hdl_rec != nullptr) {
      if (lga_str_hdl_rec->lgs_log_stream_id == stream_id)
        return lga_str_hdl_rec;
      lga_str_hdl_rec = lga_str_hdl_rec->next;
    }
  }

  TRACE_LEAVE();
  return nullptr;
}

/****************************************************************************
  Name          : lga_hdl_list_del

  Description   : This routine deletes all handles for this library.

  Arguments     : cb  - ptr to the LGA control block

  Return Values : None

  Notes         : None
******************************************************************************/
void lga_hdl_list_del(lga_client_hdl_rec_t **p_client_hdl) {
  lga_client_hdl_rec_t *client_hdl;

  TRACE_ENTER();

  while ((client_hdl = *p_client_hdl) != nullptr) {
    *p_client_hdl = client_hdl->next;
    ncshm_destroy_hdl(NCS_SERVICE_ID_LGA, client_hdl->local_hdl);
    /** clean up the channel records for this lga-client
     **/
    lga_log_stream_hdl_rec_list_del(&client_hdl->stream_list);
    lga_free_client_hdl(&client_hdl);
  }
  TRACE_LEAVE();
}

/****************************************************************************
  Name          : lga_log_stream_hdl_rec_del

  Description   : This routine deletes the a log stream handle record from
                  a list of log stream hdl records.

  Arguments     : LGA_LOG_STREAM_HDL_REC **list_head
                  LGA_LOG_STREAM_HDL_REC *rm_node


  Return Values : None

  Notes         :
******************************************************************************/
uint32_t lga_log_stream_hdl_rec_del(
    lga_log_stream_hdl_rec_t **list_head, lga_log_stream_hdl_rec_t *rm_node) {
  TRACE_ENTER();
  /* Find the channel hdl record in the list of records */
  lga_log_stream_hdl_rec_t *list_iter = *list_head;

  /* If the to be removed record is the first record */
  if (rm_node != nullptr && list_iter == rm_node) {
    *list_head = rm_node->next;
    /** remove the association with hdl-mngr
     **/
    if (rm_node->log_stream_name != nullptr) {
      free(rm_node->log_stream_name);
      rm_node->log_stream_name = nullptr;
    }

    ncshm_give_hdl(rm_node->log_stream_hdl);
    ncshm_destroy_hdl(NCS_SERVICE_ID_LGA, rm_node->log_stream_hdl);
    free(rm_node);
    TRACE_LEAVE();
    return NCSCC_RC_SUCCESS;
  } else {
    /* find the rec */
    while (nullptr != list_iter) {
      if (list_iter->next == rm_node) {
        list_iter->next = rm_node->next;
        /** remove the association with hdl-mngr
         **/
        if (rm_node->log_stream_name != nullptr) {
          free(rm_node->log_stream_name);
          rm_node->log_stream_name = nullptr;
        }

        ncshm_give_hdl(rm_node->log_stream_hdl);
        ncshm_destroy_hdl(NCS_SERVICE_ID_LGA, rm_node->log_stream_hdl);
        free(rm_node);
        TRACE_LEAVE();
        return NCSCC_RC_SUCCESS;
      }
      /* move onto the next one */
      list_iter = list_iter->next;
    }
  }
  /** The node couldn't be deleted **/
  TRACE("The node couldn't be deleted");
  return NCSCC_RC_FAILURE;
}

/****************************************************************************
  Name          : lga_hdl_rec_del

  Description   : This routine deletes the a client handle record from
                  a list of client hdl records.

  Arguments     : LGA_CLIENT_HDL_REC **list_head
                  LGA_CLIENT_HDL_REC *rm_node

  Return Values : None

  Notes         : The selection object is destroyed after all the means to
                  access the handle record (ie. hdl db tree or hdl mngr) is
                  removed. This is to disallow the waiting thread to access
                  the hdl rec while other thread executes saAmfFinalize on it.
******************************************************************************/
uint32_t lga_hdl_rec_del(
    lga_client_hdl_rec_t **list_head, lga_client_hdl_rec_t *rm_node) {
  uint32_t rc = NCSCC_RC_FAILURE;
  lga_client_hdl_rec_t *list_iter = *list_head;

  TRACE_ENTER();

  /* If the to be removed record is the first record */
  if (list_iter == rm_node) {
    *list_head = rm_node->next;

    /** detach & release the IPC
     **/
    m_NCS_IPC_DETACH(&rm_node->mbx, lga_clear_mbx, nullptr);
    m_NCS_IPC_RELEASE(&rm_node->mbx, nullptr);

    ncshm_give_hdl(rm_node->local_hdl);
    ncshm_destroy_hdl(NCS_SERVICE_ID_LGA, rm_node->local_hdl);
    /** Free the channel records off this hdl
     **/
    lga_log_stream_hdl_rec_list_del(&rm_node->stream_list);
    lga_free_client_hdl(&rm_node);
    rc = NCSCC_RC_SUCCESS;
    goto out;
  } else {
    /* find the rec */
    while (nullptr != list_iter) {
      if (list_iter->next == rm_node) {
        list_iter->next = rm_node->next;

        /** detach & release the IPC */
        m_NCS_IPC_DETACH(&rm_node->mbx, lga_clear_mbx, nullptr);
        m_NCS_IPC_RELEASE(&rm_node->mbx, nullptr);

        ncshm_give_hdl(rm_node->local_hdl);
        ncshm_destroy_hdl(NCS_SERVICE_ID_LGA, rm_node->local_hdl);
        /** Free the channel records off this lga_hdl  */
        lga_log_stream_hdl_rec_list_del(&rm_node->stream_list);
        lga_free_client_hdl(&rm_node);
        rc = NCSCC_RC_SUCCESS;
        goto out;
      }
      /* move onto the next one */
      list_iter = list_iter->next;
    }
  }

out:
  TRACE_LEAVE2("rc = %d (2 <=> fail)", rc);
  return rc;
}

/****************************************************************************
  Name          : lga_log_stream_hdl_rec_add

  Description   : This routine adds the logstream handle record to the list
                  of handles in the client hdl record.

  Arguments     : LGA_CLIENT_HDL_REC *hdl_rec
                  uint32_t               lstr_id
                  uint32_t               lstr_open_id
                  uint32_t               log_stream_open_flags
                  SaNameT             *logStreamName

  Return Values : ptr to the channel handle record

  Notes         : None
******************************************************************************/
lga_log_stream_hdl_rec_t *lga_log_stream_hdl_rec_add(
    lga_client_hdl_rec_t **hdl_rec,
    uint32_t lstr_id,
    uint32_t log_stream_open_flags,
    const char *logStreamName, uint32_t log_header_type) {
  lga_log_stream_hdl_rec_t *rec = static_cast<lga_log_stream_hdl_rec_t*>(
      calloc(1, sizeof(lga_log_stream_hdl_rec_t)));

  if (rec == nullptr) {
    TRACE("calloc failed");
    return nullptr;
  }

  /* create the association with hdl-mngr */
  if (0 == (rec->log_stream_hdl = ncshm_create_hdl(
          NCS_HM_POOL_ID_COMMON, NCS_SERVICE_ID_LGA, (NCSCONTEXT)rec))) {
    TRACE("ncshm_create_hdl failed");
    free(rec);
    return nullptr;
  }

  /** Initialize the known channel attributes at this point
   **/
  rec->lgs_log_stream_id = lstr_id;
  rec->open_flags = log_stream_open_flags;
  rec->log_header_type = log_header_type;

  /* This allocated memory will be freed
     when log stream handle is closed/finalized */
  rec->log_stream_name = static_cast<char*>(
      calloc(1, strlen(logStreamName) + 1));
  memcpy(rec->log_stream_name, logStreamName, strlen(logStreamName));

  /***
   * Initiate the recovery flag
   * The setting means that the stream is initialized and that there is
   * no reason to recover. This setting will change if server down is
   * detected
   */
  rec->recovered_flag = true;

  /** Initialize the parent handle **/
  rec->parent_hdl = *hdl_rec;

  /** Insert this record into the list of channel hdl records
   **/
  rec->next = (*hdl_rec)->stream_list;
  (*hdl_rec)->stream_list = rec;

  /** Everything appears fine, so return the
   ** steam hdl.
   **/
  return rec;
}

/****************************************************************************
  Name          : lga_hdl_rec_add

  Description   : This routine adds the handle record to the lga cb.

  Arguments     : cb       - ptr tot he LGA control block
                  reg_cbks - ptr to the set of registered callbacks
                  client_id   - obtained from LGS.

  Return Values : ptr to the lga handle record

  Notes         : None
******************************************************************************/
lga_client_hdl_rec_t *lga_hdl_rec_add(
    lga_cb_t *cb, const SaLogCallbacksT *reg_cbks, uint32_t client_id) {
  lga_client_hdl_rec_t *rec = static_cast<lga_client_hdl_rec_t*>(
      calloc(1, sizeof(lga_client_hdl_rec_t)));

  if (rec == nullptr) {
    TRACE("calloc failed");
    goto out;
  }

  /* create the association with hdl-mngr */
  if (0 == (rec->local_hdl = ncshm_create_hdl(
          NCS_HM_POOL_ID_COMMON, NCS_SERVICE_ID_LGA, (NCSCONTEXT)rec))) {
    TRACE("ncshm_create_hdl failed");
    goto err_free;
  }

  /* store the registered callbacks */
  if (reg_cbks != nullptr)
    memcpy(&rec->reg_cbk, reg_cbks, sizeof(SaLogCallbacksT));

  /** Associate with the client_id obtained from LGS
   **/
  rec->lgs_client_id = client_id;

  rec->is_stale_client = false;
  /***
   * Initiate the recovery flags
   * The setting means that the client is initialized and that there is
   * no reason to recover. This setting will change if server down is
   * detected
   */
  rec->initialized_flag = true;
  rec->recovered_flag = true;

  /** Initialize and attach the IPC/Priority queue
   **/
  if (m_NCS_IPC_CREATE(&rec->mbx) != NCSCC_RC_SUCCESS) {
    TRACE("m_NCS_IPC_CREATE failed");
    goto err_destroy_hdl;
  }

  if (m_NCS_IPC_ATTACH(&rec->mbx) != NCSCC_RC_SUCCESS) {
    TRACE("m_NCS_IPC_ATTACH failed");
    goto err_ipc_release;
  }

  /** Add this to the Link List of
   ** CLIENT_HDL_RECORDS for this LGA_CB
   **/

  osaf_mutex_lock_ordie(&cb->cb_lock);
  /* add this to the start of the list */
  rec->next = cb->client_list;
  cb->client_list = rec;
  osaf_mutex_unlock_ordie(&cb->cb_lock);

  goto out;

err_ipc_release:
  (void)m_NCS_IPC_RELEASE(&rec->mbx, nullptr);

err_destroy_hdl:
  (void)ncshm_destroy_hdl(NCS_SERVICE_ID_LGA, rec->local_hdl);

err_free:
  free(rec);
  rec = nullptr;

out:
  return rec;
}

/****************************************************************************
  Name          : lga_hdl_cbk_dispatch

  Description   : This routine dispatches the pending callbacks as per the
                  dispatch flags.

  Arguments     : cb      - ptr to the LGA control block
                  hdl_rec - ptr to the handle record
                  flags   - dispatch flags

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None
******************************************************************************/
SaAisErrorT lga_hdl_cbk_dispatch(
    lga_cb_t *cb, lga_client_hdl_rec_t *hdl_rec, SaDispatchFlagsT flags) {
  SaAisErrorT rc;

  switch (flags) {
    case SA_DISPATCH_ONE:
      rc = lga_hdl_cbk_dispatch_one(cb, hdl_rec);
      break;

    case SA_DISPATCH_ALL:
      rc = lga_hdl_cbk_dispatch_all(cb, hdl_rec);
      break;

    case SA_DISPATCH_BLOCKING:
      rc = lga_hdl_cbk_dispatch_block(cb, hdl_rec);
      break;

    default:
      TRACE("dispatch flag not valid");
      rc = SA_AIS_ERR_INVALID_PARAM;
      break;
  }

  return rc;
}

/**
 * Check if the name is valid or not.
 */
bool lga_is_extended_name_valid(const SaNameT* name) {
  if (name == nullptr) return false;
  if (osaf_is_extended_name_valid(name) == false) return false;

  SaConstStringT str = osaf_extended_name_borrow(name);
  if (strlen(str) >= kOsafMaxDnLength) return false;

  return true;
}

/*
 * To enable tracing early in saLogInitialize, use a GCC constructor
 */
__attribute__((constructor))
static void logtrace_init_constructor(void) {
  char *value;

  /* Initialize trace system first of all so we can see what is going. */
  if ((value = getenv("LOGSV_TRACE_PATHNAME")) != nullptr) {
    if (logtrace_init("lga", value, CATEGORY_ALL) != 0) {
      /* error, we cannot do anything */
      return;
    }
  }
}
