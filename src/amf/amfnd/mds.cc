/*      -*- OpenSAF  -*-
 *
 * (C) Copyright 2008 The OpenSAF Foundation
 * Copyright (C) 2017, Oracle and/or its affiliates. All rights reserved.
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
 * Author(s): Emerson Network Power
 *
 */

/*****************************************************************************
..............................................................................

..............................................................................

  DESCRIPTION:

  This file contains routines for MDS interaction.
..............................................................................

  FUNCTIONS INCLUDED in this module:


******************************************************************************
*/
#include <cinttypes>

#include "base/logtrace.h"
#include "amf/amfnd/avnd.h"
#include "amf/common/amf_d2nedu.h"
#include "amf/common/amf_n2avaedu.h"
#include "base/ncsencdec_pub.h"

/* messages to director */
const MDS_CLIENT_MSG_FORMAT_VER avnd_avd_msg_fmt_map_table[] = {
    AVSV_AVD_AVND_MSG_FMT_VER_1, AVSV_AVD_AVND_MSG_FMT_VER_2,
    AVSV_AVD_AVND_MSG_FMT_VER_3, AVSV_AVD_AVND_MSG_FMT_VER_4,
    AVSV_AVD_AVND_MSG_FMT_VER_4, AVSV_AVD_AVND_MSG_FMT_VER_6,
    AVSV_AVD_AVND_MSG_FMT_VER_7};

/* messages from director */
const MDS_CLIENT_MSG_FORMAT_VER avd_avnd_msg_fmt_map_table[] = {
    AVSV_AVD_AVND_MSG_FMT_VER_1, AVSV_AVD_AVND_MSG_FMT_VER_2,
    AVSV_AVD_AVND_MSG_FMT_VER_3, AVSV_AVD_AVND_MSG_FMT_VER_4,
    AVSV_AVD_AVND_MSG_FMT_VER_5, AVSV_AVD_AVND_MSG_FMT_VER_6,
    AVSV_AVD_AVND_MSG_FMT_VER_7};

const MDS_CLIENT_MSG_FORMAT_VER avnd_avnd_msg_fmt_map_table[] = {
    AVSV_AVND_AVND_MSG_FMT_VER_1};

const MDS_CLIENT_MSG_FORMAT_VER avnd_ava_msg_fmt_map_table[] = {
    AVSV_AVND_AVA_MSG_FMT_VER_1, AVSV_AVND_AVA_MSG_FMT_VER_2};

/* static function declarations */

static uint32_t avnd_mds_param_get(AVND_CB *);

static uint32_t avnd_mds_rcv(AVND_CB *, MDS_CALLBACK_RECEIVE_INFO *);

static uint32_t avnd_mds_cpy(AVND_CB *, MDS_CALLBACK_COPY_INFO *);

static uint32_t avnd_mds_svc_evt(AVND_CB *, MDS_CALLBACK_SVC_EVENT_INFO *);

static uint32_t avnd_mds_enc(AVND_CB *, MDS_CALLBACK_ENC_INFO *);
static uint32_t avnd_mds_flat_enc(AVND_CB *, MDS_CALLBACK_ENC_INFO *);
static uint32_t avnd_mds_flat_ava_enc(AVND_CB *, MDS_CALLBACK_ENC_INFO *);

static uint32_t avnd_mds_dec(AVND_CB *, MDS_CALLBACK_DEC_INFO *);
static uint32_t avnd_mds_flat_dec(AVND_CB *, MDS_CALLBACK_DEC_INFO *);
static uint32_t avnd_mds_flat_ava_dec(AVND_CB *, MDS_CALLBACK_DEC_INFO *);
static uint32_t avnd_mds_quiesced_process(AVND_CB *cb);

/****************************************************************************
  Name          : avnd_mds_reg

  Description   : This routine registers the AVND Service with MDS. It does
                  the following:
                  a) Gets the MDS handle & AvND MDS address
                  b) installs AvND service with MDS
                  c) Subscribes to MDS events

  Arguments     : cb - ptr to the AVND control block

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avnd_mds_reg(AVND_CB *cb) {
  NCSMDS_INFO mds_info;
  NCSADA_INFO ada_info;
  MDS_SVC_ID svc_ids[2];
  /* get the mds-hdl & avnd mds address */
  uint32_t rc = avnd_mds_param_get(cb);
  if (NCSCC_RC_SUCCESS != rc) {
    LOG_CR("MDS param get failed");
    return NCSCC_RC_FAILURE;
  }
  TRACE("MDS param get success");

  /* fill common fields */
  memset(&mds_info, 0, sizeof(NCSMDS_INFO));
  mds_info.i_mds_hdl = cb->mds_hdl;
  mds_info.i_svc_id = NCSMDS_SVC_ID_AVND;

  /*** install avnd service with mds ***/
  mds_info.i_op = MDS_INSTALL;
  mds_info.info.svc_install.i_mds_q_ownership = false;
  mds_info.info.svc_install.i_svc_cb = avnd_mds_cbk;
  mds_info.info.svc_install.i_install_scope = NCSMDS_SCOPE_NONE;
  mds_info.info.svc_install.i_mds_svc_pvt_ver = AVND_MDS_SUB_PART_VERSION;

  rc = ncsmds_api(&mds_info);
  if (NCSCC_RC_SUCCESS != rc) {
    LOG_CR("MDS install failed");
    goto done;
  }
  TRACE("MDS install success");

  /*** subscribe to mds events ***/
  mds_info.i_op = MDS_SUBSCRIBE;
  mds_info.info.svc_subscribe.i_svc_ids = svc_ids;

  /* subscribe to events from avd */
  mds_info.info.svc_subscribe.i_scope = NCSMDS_SCOPE_NONE;
  mds_info.info.svc_subscribe.i_num_svcs = 1;
  svc_ids[0] = NCSMDS_SVC_ID_AVD;
  rc = ncsmds_api(&mds_info);
  if (NCSCC_RC_SUCCESS != rc) {
    LOG_CR("MDS subscription for AVD events failed");
    goto done;
  }
  TRACE("MDS subscription for AVD events success");

  /* subscribe to events from ava */
  mds_info.info.svc_subscribe.i_scope = NCSMDS_SCOPE_INTRANODE;
  mds_info.info.svc_subscribe.i_num_svcs = 1;
  svc_ids[0] = NCSMDS_SVC_ID_AVA;
  rc = ncsmds_api(&mds_info);
  if (NCSCC_RC_SUCCESS != rc) {
    LOG_CR("MDS subscription for AVA events failed");
    goto done;
  }
  TRACE("MDS subscription for AVA events success");

  /* Subscribe for AvND itself. Will be used for External/Inernode proxy support
   */
  mds_info.i_op = MDS_SUBSCRIBE;
  mds_info.info.svc_subscribe.i_svc_ids = svc_ids;

  /* subscribe to events from avd */
  mds_info.info.svc_subscribe.i_scope = NCSMDS_SCOPE_NONE;
  mds_info.info.svc_subscribe.i_num_svcs = 1;
  svc_ids[0] = NCSMDS_SVC_ID_AVND;
  rc = ncsmds_api(&mds_info);
  if (NCSCC_RC_SUCCESS != rc) {
    LOG_CR("MDS subscription for AVND events failed");
    goto done;
  }
  TRACE("MDS subscription for AVND events success");

  /* Subscribe for Controller AvND Vdest.
     It will be used for External Comp support */
  mds_info.i_op = MDS_SUBSCRIBE;
  mds_info.info.svc_subscribe.i_svc_ids = svc_ids;

  /* subscribe to events from avd */
  mds_info.info.svc_subscribe.i_scope = NCSMDS_SCOPE_NONE;
  mds_info.info.svc_subscribe.i_num_svcs = 1;
  svc_ids[0] = NCSMDS_SVC_ID_AVND_CNTLR;
  rc = ncsmds_api(&mds_info);
  if (NCSCC_RC_SUCCESS != rc) {
    LOG_CR("MDS subscription for Controller AVND vdest failed");
    goto done;
  }
  TRACE("MDS subscription for Controller AVND vdest success");

  /* get the handle from MDS */

  memset(&ada_info, 0, sizeof(ada_info));

  ada_info.req = NCSADA_GET_HDLS;

  rc = ncsada_api(&ada_info);

  if (rc != NCSCC_RC_SUCCESS) {
    goto done;
  }

  /* Temp: For AvD down handling */
  cb->is_avd_down = false;

done:
  return rc;
}

/****************************************************************************
  Name          : avnd_mds_cbk

  Description   : This routine is a callback routine that is provided to MDS.
                  MDS calls this routine for encode, decode, copy, receive &
                  service event notification operations.

  Arguments     : info - ptr to the MDS callback info

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avnd_mds_cbk(NCSMDS_CALLBACK_INFO *info) {
  AVND_CB *cb = avnd_cb;
  uint32_t rc = NCSCC_RC_SUCCESS;

  if (!info) goto done;

  switch (info->i_op) {
    case MDS_CALLBACK_RECEIVE: {
      rc = avnd_mds_rcv(cb, &info->info.receive);
      if (NCSCC_RC_SUCCESS != rc) LOG_CR("MDS receive callback failed");
    } break;

    case MDS_CALLBACK_COPY: {
      rc = avnd_mds_cpy(cb, &info->info.cpy);
      if (NCSCC_RC_SUCCESS != rc) LOG_CR("MDS copy callback failed");
    } break;

    case MDS_CALLBACK_SVC_EVENT: {
      rc = avnd_mds_svc_evt(cb, &info->info.svc_evt);
      if (NCSCC_RC_SUCCESS != rc) LOG_CR("MDS service event callback failed");
    } break;

    case MDS_CALLBACK_ENC: {
      rc = avnd_mds_enc(cb, &info->info.enc);
      if (NCSCC_RC_SUCCESS != rc) LOG_CR("MDS encode callback failed");
    } break;

    case MDS_CALLBACK_ENC_FLAT: {
      rc = avnd_mds_flat_enc(cb, &info->info.enc_flat);
      if (NCSCC_RC_SUCCESS != rc) LOG_CR("MDS encode flat callback failed");
    } break;

    case MDS_CALLBACK_DEC: {
      rc = avnd_mds_dec(cb, &info->info.dec);
      if (NCSCC_RC_SUCCESS != rc) LOG_CR("MDS decode callback failed");
    } break;

    case MDS_CALLBACK_DEC_FLAT: {
      rc = avnd_mds_flat_dec(cb, &info->info.dec_flat);
      if (NCSCC_RC_SUCCESS != rc) LOG_CR("MDS decode flat callback failed");
    } break;

    case MDS_CALLBACK_QUIESCED_ACK: {
      rc = avnd_mds_quiesced_process(cb);
      ;
      if (NCSCC_RC_SUCCESS != rc)
        LOG_CR("MDS Quiesced Ack callback failed");
      else
        TRACE("MDS Quiesced Ack callback success");
    } break;

    default:
      osafassert(0);
      break;
  }

done:
  return rc;
}

/****************************************************************************
  Name          : avnd_mds_rcv

  Description   : This routine is invoked when AvND message is received from
                  AvD, AvA. It creates AvND event & enqueues it to the
                  AvND mailbox.

  Arguments     : cb       - ptr to the AvND control block
                  rcv_info - ptr to the MDS receive info

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avnd_mds_rcv(AVND_CB *cb, MDS_CALLBACK_RECEIVE_INFO *rcv_info) {
  AVND_EVT_TYPE type = AVND_EVT_INVALID;
  AVND_EVT *evt = 0;
  AVND_MSG msg;
  uint32_t rc = NCSCC_RC_SUCCESS;

  memset(&msg, 0, sizeof(AVND_MSG));

  if (!rcv_info->i_msg) {
    rc = NCSCC_RC_FAILURE;
    goto done;
  }

  /* populate the msg structure (ptr assignments) */
  switch (rcv_info->i_fr_svc_id) {
    case NCSMDS_SVC_ID_AVD:

      /*
       * Set the Active Anchor value, if the message is Verify message or Node
       * up message, to the Anchor of the received message.
       */
      if ((AVSV_D2N_NODE_UP_MSG ==
           ((AVSV_DND_MSG *)(rcv_info->i_msg))->msg_type) ||
          (AVSV_D2N_DATA_VERIFY_MSG ==
           ((AVSV_DND_MSG *)(rcv_info->i_msg))->msg_type)) {
        cb->active_avd_adest = rcv_info->i_fr_dest;
        TRACE_1("Active AVD Adest = %" PRIu64, cb->active_avd_adest);
      }

      msg.type = AVND_MSG_AVD;
      msg.info.avd = (AVSV_DND_MSG *)rcv_info->i_msg;

      /*
       * Validate the anchor value received with
       * the message with Anchor of Active. Don't accept message
       * from any other anchor than Active (except for HB message).
       */
      if ((rcv_info->i_fr_dest != cb->active_avd_adest) &&
          (msg.info.avd->msg_type != AVSV_D2N_HEARTBEAT_MSG)) {
        LOG_ER("Received dest: %" PRIu64 " and cb active AVD adest:%" PRIu64
               " mismatch, message type = %u",
               rcv_info->i_fr_dest, cb->active_avd_adest,
               ((AVSV_DND_MSG *)(rcv_info->i_msg))->msg_type);
        avsv_dnd_msg_free(((AVSV_DND_MSG *)rcv_info->i_msg));
        rcv_info->i_msg = 0;
        return NCSCC_RC_SUCCESS;
      }
      break;

    case NCSMDS_SVC_ID_AVA:

      msg.type = AVND_MSG_AVA;
      msg.info.ava = (AVSV_NDA_AVA_MSG *)rcv_info->i_msg;

      if (msg.info.ava->type != AVSV_AVA_API_MSG) {
        LOG_NO("%s: unknown message type (%u)", __FUNCTION__,
               msg.info.ava->type);
        rc = NCSCC_RC_FAILURE;
        goto done;
      }

      if ((msg.info.ava->info.api_info.type == 0) ||
          (msg.info.ava->info.api_info.type >= AVSV_AMF_API_MAX)) {
        LOG_NO("%s: unknown API type (%u)", __FUNCTION__,
               msg.info.ava->info.api_info.type);
        rc = NCSCC_RC_FAILURE;
        goto done;
      }
      break;

    case NCSMDS_SVC_ID_AVND:

      msg.type = AVND_MSG_AVND;
      msg.info.avnd = (AVSV_ND2ND_AVND_MSG *)rcv_info->i_msg;
      break;

    default:
      osafassert(0);
      break;
  }

  /* nullify the ptr in rcv-info */
  rcv_info->i_msg = 0;

  /* determine the event type */
  switch (msg.type) {
    case AVND_MSG_AVD:

      if (msg.info.avd->msg_type == AVSV_D2N_COMPCSI_ASSIGN_MSG)
        type = AVND_EVT_AVD_COMPCSI_ASSIGN_MSG;
      else
        type = static_cast<AVND_EVT_TYPE>(
            (msg.info.avd->msg_type - AVSV_D2N_NODE_UP_MSG) +
            AVND_EVT_AVD_NODE_UP_MSG);
      break;

    case AVND_MSG_AVA:
      osafassert(AVSV_AVA_API_MSG == msg.info.ava->type);
      type = static_cast<AVND_EVT_TYPE>(
          (msg.info.ava->info.api_info.type - AVSV_AMF_FINALIZE) +
          AVND_EVT_AVA_FINALIZE);
      break;

    case AVND_MSG_AVND:
      type = AVND_EVT_AVND_AVND_MSG;
      break;

    default:
      osafassert(0);
      break;
  }

  /* create the event */
  evt = avnd_evt_create(cb, type, &rcv_info->i_msg_ctxt, &rcv_info->i_fr_dest,
                        (msg.info.avd) ? (void *)msg.info.avd
                                       : ((msg.info.ava) ? (void *)msg.info.ava
                                                         : (msg.info.avnd)),
                        0, 0);
  if (!evt) {
    rc = NCSCC_RC_FAILURE;
    goto done;
  }

  /* save the msg version for possible later use in event handlers */
  evt->msg_fmt_ver = rcv_info->i_msg_fmt_ver;

  /* nullify the msg as it is used in the event */
  memset(&msg, 0, sizeof(AVND_MSG));

  /* send the event */
  rc = avnd_evt_send(cb, evt);

done:
  /* free the event */
  if (NCSCC_RC_SUCCESS != rc && evt) avnd_evt_destroy(evt);

  /* free the msg content */
  avnd_msg_content_free(cb, &msg);

  return rc;
}

/****************************************************************************
  Name          : avnd_mds_cpy

  Description   : This routine is invoked when AvND message is to be copied.
                  It transfers the memory ownership.

  Arguments     : cb       - ptr to the AvND control block
                  cpy_info - ptr to the MDS copy info

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avnd_mds_cpy(AVND_CB *cb, MDS_CALLBACK_COPY_INFO *cpy_info) {
  TRACE_ENTER();
  AVND_MSG *msg;
  uint32_t rc = NCSCC_RC_SUCCESS;

  if (cpy_info->i_msg == nullptr) {
    LOG_ER("%s: no msg", __FUNCTION__);
    return NCSCC_RC_FAILURE;
  }

  msg = new AVND_MSG();

  /* the message may be destined to avd, ava */
  switch (cpy_info->i_to_svc_id) {
    case NCSMDS_SVC_ID_AVD:
      cpy_info->o_msg_fmt_ver =
          avnd_avd_msg_fmt_map_table[cpy_info->i_rem_svc_pvt_ver - 1];
      avnd_msg_copy(cb, msg, (AVND_MSG *)cpy_info->i_msg);
      cpy_info->o_cpy = (NCSCONTEXT)msg->info.avd;
      msg->info.avd = 0;
      break;

    case NCSMDS_SVC_ID_AVND:
    case NCSMDS_SVC_ID_AVND_CNTLR:
      cpy_info->o_msg_fmt_ver =
          avnd_avnd_msg_fmt_map_table[cpy_info->i_rem_svc_pvt_ver - 1];
      avnd_msg_copy(cb, msg, (AVND_MSG *)cpy_info->i_msg);
      cpy_info->o_cpy = (NCSCONTEXT)msg->info.avnd;
      msg->info.avnd = 0;
      break;

    case NCSMDS_SVC_ID_AVA:
      avnd_msg_copy(cb, msg, (AVND_MSG *)cpy_info->i_msg);
      cpy_info->o_cpy = (NCSCONTEXT)msg->info.ava;
      msg->info.ava = 0;
      break;

    default:
      osafassert(0);
      break;
  }

  if (rc != NCSCC_RC_SUCCESS) {
    LOG_ER("%s: copy failed", __FUNCTION__);
    delete msg;
  }

  TRACE_LEAVE();
  return rc;
}

/****************************************************************************
  Name          : avnd_mds_svc_evt

  Description   : This routine is invoked to inform AvND of MDS events. AvND
                  had subscribed to these events during MDS registration.

  Arguments     : cb       - ptr to the AvND control block
                  evt_info - ptr to the MDS event info

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avnd_mds_svc_evt(AVND_CB *cb, MDS_CALLBACK_SVC_EVENT_INFO *evt_info) {
  AVND_EVT *evt = 0;
  uint32_t rc = NCSCC_RC_SUCCESS;

  /* assign mds-dest for AVD, AVND & AVA as per the MDS event */
  switch (evt_info->i_change) {
    case NCSMDS_NEW_ACTIVE:
      if (evt_info->i_svc_id == NCSMDS_SVC_ID_AVD) {
        LOG_NO("AVD NEW_ACTIVE, adest:%" PRIu64, evt_info->i_dest);

        // sometimes NEW_ACTIVE director is received before
        // DOWN is received for the old director ..
        if (m_AVND_CB_IS_AVD_UP(cb)) {
          m_AVND_CB_AVD_UP_RESET(cb);
        }

        evt = avnd_evt_create(cb, AVND_EVT_MDS_AVD_UP, 0, &evt_info->i_dest, 0,
                              0, 0);
        evt->info.mds.i_change = evt_info->i_change;
      }
      break;
    case NCSMDS_UP:
      switch (evt_info->i_svc_id) {
        case NCSMDS_SVC_ID_AVD:
          /* create the mds event */
          evt = avnd_evt_create(cb, AVND_EVT_MDS_AVD_UP, 0, &evt_info->i_dest,
                                0, 0, 0);
          evt->info.mds.i_change = evt_info->i_change;
          break;

        case NCSMDS_SVC_ID_AVA:
          // Post MDS info of the agent.
          evt = avnd_evt_create(cb, AVND_EVT_AMFA_MDS_VER_INFO, 0,
                                &evt_info->i_dest, 0, 0, 0);
          evt->info.amfa_mds_info.mds_dest = evt_info->i_dest;
          evt->info.amfa_mds_info.mds_version = evt_info->i_rem_svc_pvt_ver;

          break;

        case NCSMDS_SVC_ID_AVND:
          /*  New AVND has come up. Update NODE_ID to MDS_DEST */
          /* Validate whether this is a ADEST or VDEST */
          if (!m_MDS_DEST_IS_AN_ADEST(evt_info->i_dest)) {
            return rc;
          }

          /* Create the mds event.
             if(evt_info->i_dest != cb->avnd_dest)
             Store its own dest id also. This is useful in Proxy at Ctrl
             registering external component.
           */
          evt = avnd_evt_create(cb, AVND_EVT_MDS_AVND_UP, 0, &evt_info->i_dest,
                                0, 0, 0);
          break;

        case NCSMDS_SVC_ID_AVND_CNTLR:
          /* This is a VDEST. Store it for use in sending ext comp req
             to this Vdest. This Vdest is of Contr AvND hosting ext comp. */
          /* Validate whether this is a ADEST or VDEST */
          if (m_MDS_DEST_IS_AN_ADEST(evt_info->i_dest)) {
            return rc;
          }
          cb->cntlr_avnd_vdest = evt_info->i_dest;
          return rc;
          break;

        default:
          osafassert(0);
      }
      break;

    case NCSMDS_DOWN:
      switch (evt_info->i_svc_id) {
        case NCSMDS_SVC_ID_AVD:
          if (m_MDS_DEST_IS_AN_ADEST(evt_info->i_dest) &&
              (evt_info->i_node_id != ncs_get_node_id())) {
            /* No action is required, Proceed ahead. */
          } else /* Reset the vdest as this node is going to get rebooted. */
            memset(&cb->avd_dest, 0, sizeof(MDS_DEST));

          evt = avnd_evt_create(cb, AVND_EVT_MDS_AVD_DN, 0, &evt_info->i_dest,
                                &evt_info->i_node_id, 0, 0);
          break;

        case NCSMDS_SVC_ID_AVA:
          evt = avnd_evt_create(cb, AVND_EVT_MDS_AVA_DN, 0, &evt_info->i_dest,
                                0, 0, 0);
          break;

        case NCSMDS_SVC_ID_AVND:
          /*  New AVND has come up. Update NODE_ID to MDS_DEST */
          /* Validate whether this is a ADEST or VDEST */
          if (!m_MDS_DEST_IS_AN_ADEST(evt_info->i_dest)) return rc;

          /* Create the mds event.
             if(evt_info->i_dest != cb->avnd_dest)
             Store its own dest id also. This is useful in Proxy at Ctrl
             registering external component.
           */
          evt = avnd_evt_create(cb, AVND_EVT_MDS_AVND_DN, 0, &evt_info->i_dest,
                                0, 0, 0);
          break;

        case NCSMDS_SVC_ID_AVND_CNTLR:
          break;

        default:
          osafassert(0);
      }
      break;

    default:
      break;
  }

  /* send the event */
  if (evt) rc = avnd_evt_send(cb, evt);

  /* if failure, free the event */
  if (NCSCC_RC_SUCCESS != rc && evt) avnd_evt_destroy(evt);

  return rc;
}

/****************************************************************************
  Name          : avnd_mds_enc

  Description   : This routine is invoked to encode AvD messages.

  Arguments     : cb       - ptr to the AvND control block
                  enc_info - ptr to the MDS encode info

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : This routine also frees the message after encoding it in
                  the userbuf.
******************************************************************************/
uint32_t avnd_mds_enc(AVND_CB *cb, MDS_CALLBACK_ENC_INFO *enc_info) {
  AVND_MSG *msg;
  EDU_ERR ederror = static_cast<EDU_ERR>(0);
  uint32_t rc = NCSCC_RC_SUCCESS;

  msg = (AVND_MSG *)enc_info->i_msg;

  switch (msg->type) {
    case AVND_MSG_AVD:
      enc_info->o_msg_fmt_ver = m_NCS_ENC_MSG_FMT_GET(
          enc_info->i_rem_svc_pvt_ver, AVND_AVD_SUBPART_VER_MIN,
          AVND_AVD_SUBPART_VER_MAX, avnd_avd_msg_fmt_map_table);

      if (enc_info->o_msg_fmt_ver < AVSV_AVD_AVND_MSG_FMT_VER_1) {
        LOG_ER("%s,%u: wrong msg fmt not valid %u, res'%u'", __FUNCTION__,
               __LINE__, enc_info->i_rem_svc_pvt_ver, enc_info->o_msg_fmt_ver);
        return NCSCC_RC_FAILURE;
      }

      rc = m_NCS_EDU_VER_EXEC(&cb->edu_hdl, avsv_edp_dnd_msg, enc_info->io_uba,
                              EDP_OP_TYPE_ENC, msg->info.avd, &ederror,
                              enc_info->o_msg_fmt_ver);
      break;

    case AVND_MSG_AVND:
      enc_info->o_msg_fmt_ver = m_NCS_ENC_MSG_FMT_GET(
          enc_info->i_rem_svc_pvt_ver, AVND_AVND_SUBPART_VER_MIN,
          AVND_AVND_SUBPART_VER_MAX, avnd_avnd_msg_fmt_map_table);

      if (enc_info->o_msg_fmt_ver < AVSV_AVD_AVND_MSG_FMT_VER_1) {
        return NCSCC_RC_FAILURE;
      }

      rc = m_NCS_EDU_VER_EXEC(&cb->edu_hdl, avsv_edp_ndnd_msg, enc_info->io_uba,
                              EDP_OP_TYPE_ENC, msg->info.avnd, &ederror,
                              enc_info->o_msg_fmt_ver);
      break;

    case AVND_MSG_AVA:
      enc_info->o_msg_fmt_ver = m_NCS_ENC_MSG_FMT_GET(
          enc_info->i_rem_svc_pvt_ver, AVND_AVA_SUBPART_VER_MIN,
          AVND_AVA_SUBPART_VER_MAX, avnd_ava_msg_fmt_map_table);

      if (enc_info->o_msg_fmt_ver < AVSV_AVND_AVA_MSG_FMT_VER_1) {
        return NCSCC_RC_FAILURE;
      }

      rc = m_NCS_EDU_VER_EXEC(&cb->edu_hdl, avsv_edp_nda_msg, enc_info->io_uba,
                              EDP_OP_TYPE_ENC, msg->info.ava, &ederror,
                              enc_info->o_msg_fmt_ver);
      break;

    default:
      osafassert(0);
      break;
  }

  if (rc != NCSCC_RC_SUCCESS) {
    /* Encode failed!!! */
    return rc;
  }

  /* free the message */
  avnd_msg_content_free(cb, msg);

  return rc;
}

/****************************************************************************
  Name          : avnd_mds_flat_enc

  Description   : This routine is invoked to (flat) encode AvA messages.

  Arguments     : cb       - ptr to the AvND control block
                  enc_info - ptr to the MDS encode info

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : This routine also frees the message after encoding it in
                  the userbuf.
******************************************************************************/
uint32_t avnd_mds_flat_enc(AVND_CB *cb, MDS_CALLBACK_ENC_INFO *enc_info) {
  AVND_MSG *msg;
  uint32_t rc = NCSCC_RC_SUCCESS;
  EDU_ERR ederror = static_cast<EDU_ERR>(0);

  msg = (AVND_MSG *)enc_info->i_msg;

  switch (msg->type) {
    case AVND_MSG_AVA:
      enc_info->o_msg_fmt_ver = m_NCS_ENC_MSG_FMT_GET(
          enc_info->i_rem_svc_pvt_ver, AVND_AVA_SUBPART_VER_MIN,
          AVND_AVA_SUBPART_VER_MAX, avnd_ava_msg_fmt_map_table);

      if (enc_info->o_msg_fmt_ver < AVSV_AVND_AVA_MSG_FMT_VER_1) {
        return NCSCC_RC_FAILURE;
      }

      rc = avnd_mds_flat_ava_enc(cb, enc_info);
      break;

    case AVND_MSG_AVD:
      enc_info->o_msg_fmt_ver = m_NCS_ENC_MSG_FMT_GET(
          enc_info->i_rem_svc_pvt_ver, AVND_AVD_SUBPART_VER_MIN,
          AVND_AVD_SUBPART_VER_MAX, avnd_avd_msg_fmt_map_table);

      if (enc_info->o_msg_fmt_ver < AVSV_AVD_AVND_MSG_FMT_VER_1) {
        LOG_ER("%s,%u: wrong msg fmt not valid %u, res'%u'", __FUNCTION__,
               __LINE__, enc_info->i_rem_svc_pvt_ver, enc_info->o_msg_fmt_ver);
        return NCSCC_RC_FAILURE;
      }

      rc = m_NCS_EDU_VER_EXEC(&cb->edu_hdl, avsv_edp_dnd_msg, enc_info->io_uba,
                              EDP_OP_TYPE_ENC, msg->info.avd, &ederror,
                              enc_info->o_msg_fmt_ver);
      break;

    case AVND_MSG_AVND:
      enc_info->o_msg_fmt_ver = m_NCS_ENC_MSG_FMT_GET(
          enc_info->i_rem_svc_pvt_ver, AVND_AVND_SUBPART_VER_MIN,
          AVND_AVND_SUBPART_VER_MAX, avnd_avnd_msg_fmt_map_table);

      if (enc_info->o_msg_fmt_ver < AVSV_AVND_AVND_MSG_FMT_VER_1) {
        return NCSCC_RC_FAILURE;
      }

      rc = m_NCS_EDU_VER_EXEC(&cb->edu_hdl, avsv_edp_ndnd_msg, enc_info->io_uba,
                              EDP_OP_TYPE_ENC, msg->info.avnd, &ederror,
                              enc_info->o_msg_fmt_ver);
      break;

    default:
      osafassert(0);
      break;
  } /* switch */

  if (rc != NCSCC_RC_SUCCESS) {
    /* Encode failed!!! */
    return rc;
  }

  /* free the message */
  avnd_msg_content_free(cb, msg);

  return rc;
}
/**
 * @brief  Encodes CSI attribute change callback msg.
 *
 * @param  uba (ptr to NCS_UBAID).
 * @param  msg (ptr to AVSV_NDA_AVA_MSG).
 *
 * @return NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE.
 */
static uint32_t enc_csi_attr_change_cbk(NCS_UBAID *uba, AVSV_NDA_AVA_MSG *msg) {
  AVSV_AMF_CSI_ATTR_CHANGE_PARAM *csi_attr_change =
      &msg->info.cbk_info->param.csi_attr_change;
  uint8_t *p8;
  uint16_t len;
  uint32_t rc = NCSCC_RC_SUCCESS, i;
  SaStringT value = nullptr;
  if (csi_attr_change->attrs.number == 0) goto done;
  if (osaf_is_an_extended_name(&csi_attr_change->csi_name)) {
    osaf_encode_sanamet(uba, &csi_attr_change->csi_name);
  }

  for (i = 0; i < csi_attr_change->attrs.number; i++) {
    rc = ncs_encode_n_octets_in_uba(
        uba, (uint8_t *)&csi_attr_change->attrs.list[i].name, sizeof(SaNameT));
    if (NCSCC_RC_SUCCESS != rc) goto done;
    if (osaf_is_an_extended_name(&csi_attr_change->attrs.list[i].name)) {
      osaf_encode_sanamet(uba, &csi_attr_change->attrs.list[i].name);
    }

    if (!csi_attr_change->attrs.list[i].string_ptr) {
      value = new char[osaf_extended_name_length(
                           &csi_attr_change->attrs.list[i].value) +
                       1]();
      strcpy(value,
             osaf_extended_name_borrow(&csi_attr_change->attrs.list[i].value));
    } else {
      value = csi_attr_change->attrs.list[i].string_ptr;
    }
    len = strlen(value);
    p8 = ncs_enc_reserve_space(uba, 2);
    ncs_encode_16bit(&p8, len);
    ncs_enc_claim_space(uba, 2);
    rc = ncs_encode_n_octets_in_uba(uba, (uint8_t *)value, (len + 1));
    // free value
    if (value != nullptr &&
        value != csi_attr_change->attrs.list[i].string_ptr) {
      delete[] value;
      value = nullptr;
    }

    if (NCSCC_RC_SUCCESS != rc) goto done;
  }
done:
  return rc;
}
/****************************************************************************
  Name          : avnd_mds_flat_ava_enc

  Description   : This routine is invoked to (flat) encode AvA message.

  Arguments     : cb       - ptr to the AvND control block
                  enc_info - ptr to the MDS encode info

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avnd_mds_flat_ava_enc(AVND_CB *cb, MDS_CALLBACK_ENC_INFO *enc_info) {
  AVSV_NDA_AVA_MSG *ava;
  SaStringT value = nullptr;

  ava = ((AVND_MSG *)enc_info->i_msg)->info.ava;
  osafassert(ava);

  /* encode top-level ava message structure into userbuf */
  uint32_t rc = ncs_encode_n_octets_in_uba(enc_info->io_uba, (uint8_t *)ava,
                                           sizeof(AVSV_NDA_AVA_MSG));
  if (NCSCC_RC_SUCCESS != rc) goto done;

  /* encode the individual ava messages */
  switch (ava->type) {
    case AVSV_AVND_AMF_CBK_MSG: {
      AVSV_AMF_CBK_INFO *cbk_info = ava->info.cbk_info;
      uint8_t *p8;
      uint16_t len;

      /* encode cbk-info */
      rc = ncs_encode_n_octets_in_uba(enc_info->io_uba, (uint8_t *)cbk_info,
                                      sizeof(AVSV_AMF_CBK_INFO));
      if (NCSCC_RC_SUCCESS != rc) goto done;

      switch (cbk_info->type) {
        case AVSV_AMF_CSI_ATTR_CHANGE:
          rc = enc_csi_attr_change_cbk(enc_info->io_uba, ava);
          break;
        case AVSV_AMF_CSI_SET:
          if (osaf_is_an_extended_name(&cbk_info->param.csi_set.comp_name)) {
            osaf_encode_sanamet(enc_info->io_uba,
                                &cbk_info->param.csi_set.comp_name);
          }

          if (osaf_is_an_extended_name(
                  &cbk_info->param.csi_set.csi_desc.csiName)) {
            osaf_encode_sanamet(enc_info->io_uba,
                                &cbk_info->param.csi_set.csi_desc.csiName);
          }

          if (cbk_info->param.csi_set.attrs.number) {
            for (uint32_t i = 0; i < cbk_info->param.csi_set.attrs.number;
                 i++) {
              rc = ncs_encode_n_octets_in_uba(
                  enc_info->io_uba,
                  (uint8_t *)&cbk_info->param.csi_set.attrs.list[i].name,
                  sizeof(SaNameT));
              if (NCSCC_RC_SUCCESS != rc) goto done;

              // This is to make sure the backward compatibility
              if (osaf_is_an_extended_name(
                      &cbk_info->param.csi_set.attrs.list[i].name)) {
                osaf_encode_sanamet(
                    enc_info->io_uba,
                    &cbk_info->param.csi_set.attrs.list[i].name);
              }
              //
              if (!cbk_info->param.csi_set.attrs.list[i].string_ptr) {
                value =
                    new char[osaf_extended_name_length(
                                 &cbk_info->param.csi_set.attrs.list[i].value) +
                             1]();
                strcpy(value,
                       osaf_extended_name_borrow(
                           &cbk_info->param.csi_set.attrs.list[i].value));
              } else {
                value = cbk_info->param.csi_set.attrs.list[i].string_ptr;
              }
              len = strlen(value);
              p8 = ncs_enc_reserve_space(enc_info->io_uba, 2);
              ncs_encode_16bit(&p8, len);
              ncs_enc_claim_space(enc_info->io_uba, 2);
              rc = ncs_encode_n_octets_in_uba(enc_info->io_uba,
                                              (uint8_t *)value, (len + 1));
              // free value
              if (value != nullptr &&
                  value != cbk_info->param.csi_set.attrs.list[i].string_ptr) {
                delete[] value;
                value = nullptr;
              }

              if (NCSCC_RC_SUCCESS != rc) goto done;
            }
          }

          if (cbk_info->param.csi_set.ha == SA_AMF_HA_ACTIVE) {
            if (osaf_is_an_extended_name(
                    &cbk_info->param.csi_set.csi_desc.csiStateDescriptor
                         .activeDescriptor.activeCompName)) {
              osaf_encode_sanamet(
                  enc_info->io_uba,
                  &cbk_info->param.csi_set.csi_desc.csiStateDescriptor
                       .activeDescriptor.activeCompName);
            }
          }

          if (cbk_info->param.csi_set.ha == SA_AMF_HA_STANDBY) {
            if (osaf_is_an_extended_name(
                    &cbk_info->param.csi_set.csi_desc.csiStateDescriptor
                         .standbyDescriptor.activeCompName)) {
              osaf_encode_sanamet(
                  enc_info->io_uba,
                  &cbk_info->param.csi_set.csi_desc.csiStateDescriptor
                       .standbyDescriptor.activeCompName);
            }
          }

          break;

        case AVSV_AMF_PG_TRACK:
          if (osaf_is_an_extended_name(&cbk_info->param.pg_track.csi_name)) {
            osaf_encode_sanamet(enc_info->io_uba,
                                &cbk_info->param.pg_track.csi_name);
          }

          if (cbk_info->param.pg_track.buf.numberOfItems) {
            rc = ncs_encode_n_octets_in_uba(
                enc_info->io_uba,
                (uint8_t *)cbk_info->param.pg_track.buf.notification,
                sizeof(SaAmfProtectionGroupNotificationT) *
                    cbk_info->param.pg_track.buf.numberOfItems);
            if (NCSCC_RC_SUCCESS != rc) goto done;
            for (uint32_t i = 0; i < cbk_info->param.pg_track.buf.numberOfItems;
                 i++) {
              if (osaf_is_an_extended_name(
                      &cbk_info->param.pg_track.buf.notification[i]
                           .member.compName)) {
                osaf_encode_sanamet(
                    enc_info->io_uba,
                    &cbk_info->param.pg_track.buf.notification[i]
                         .member.compName);
              }
            }
          }
          break;

        case AVSV_AMF_HC:
          if (osaf_is_an_extended_name(&cbk_info->param.hc.comp_name)) {
            osaf_encode_sanamet(enc_info->io_uba,
                                &cbk_info->param.hc.comp_name);
          }
          break;

        case AVSV_AMF_COMP_TERM:
          if (osaf_is_an_extended_name(&cbk_info->param.comp_term.comp_name)) {
            osaf_encode_sanamet(enc_info->io_uba,
                                &cbk_info->param.comp_term.comp_name);
          }
          break;

        case AVSV_AMF_CSI_REM:
          if (osaf_is_an_extended_name(&cbk_info->param.csi_rem.comp_name)) {
            osaf_encode_sanamet(enc_info->io_uba,
                                &cbk_info->param.csi_rem.comp_name);
          }

          if (osaf_is_an_extended_name(&cbk_info->param.csi_rem.csi_name)) {
            osaf_encode_sanamet(enc_info->io_uba,
                                &cbk_info->param.csi_rem.csi_name);
          }
          break;

        case AVSV_AMF_PXIED_COMP_INST:
          if (osaf_is_an_extended_name(
                  &cbk_info->param.pxied_comp_inst.comp_name)) {
            osaf_encode_sanamet(enc_info->io_uba,
                                &cbk_info->param.pxied_comp_inst.comp_name);
          }
          break;

        case AVSV_AMF_PXIED_COMP_CLEAN:
          if (osaf_is_an_extended_name(
                  &cbk_info->param.pxied_comp_clean.comp_name)) {
            osaf_encode_sanamet(enc_info->io_uba,
                                &cbk_info->param.pxied_comp_clean.comp_name);
          }
          break;

        default:
          osafassert(0);
      } /* switch */
    } break;

    case AVSV_AVND_AMF_API_RESP_MSG: {
      AVSV_AMF_API_RESP_INFO *api_resp_info = &ava->info.api_resp_info;

      if (api_resp_info->type == AVSV_AMF_HA_STATE_GET) {
        if (osaf_is_an_extended_name(&api_resp_info->param.ha_get.comp_name)) {
          osaf_encode_sanamet(enc_info->io_uba,
                              &api_resp_info->param.ha_get.comp_name);
        }

        if (osaf_is_an_extended_name(&api_resp_info->param.ha_get.csi_name)) {
          osaf_encode_sanamet(enc_info->io_uba,
                              &api_resp_info->param.ha_get.csi_name);
        }
      }
    } break;

    case AVSV_AVA_API_MSG:
    default:
      osafassert(0);
  } /* switch */

done:
  return rc;
}

/****************************************************************************
  Name          : avnd_mds_dec

  Description   : This routine is invoked to decode AvD message.

  Arguments     : cb       - ptr to the AvND control block
                  dec_info - ptr to the MDS decode info

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avnd_mds_dec(AVND_CB *cb, MDS_CALLBACK_DEC_INFO *dec_info) {
  EDU_ERR ederror = static_cast<EDU_ERR>(0);
  uint32_t rc = NCSCC_RC_SUCCESS;

  switch (dec_info->i_fr_svc_id) {
    case NCSMDS_SVC_ID_AVD:
      if (!m_NCS_MSG_FORMAT_IS_VALID(
              dec_info->i_msg_fmt_ver, AVND_AVD_SUBPART_VER_MIN,
              AVND_AVD_SUBPART_VER_MAX, avd_avnd_msg_fmt_map_table)) {
        LOG_ER("%s,%u: wrong msg fmt not valid %u", __FUNCTION__, __LINE__,
               dec_info->i_msg_fmt_ver);
        return NCSCC_RC_FAILURE;
      }

      rc = m_NCS_EDU_VER_EXEC(
          &cb->edu_hdl, avsv_edp_dnd_msg, dec_info->io_uba, EDP_OP_TYPE_DEC,
          (AVSV_DND_MSG **)&dec_info->o_msg, &ederror, dec_info->i_msg_fmt_ver);
      if (rc != NCSCC_RC_SUCCESS) {
        if (dec_info->o_msg != nullptr) {
          avsv_dnd_msg_free(static_cast<AVSV_DND_MSG *>(dec_info->o_msg));
          dec_info->o_msg = nullptr;
        }
        return rc;
      }
      break;

    case NCSMDS_SVC_ID_AVND:
      if (!m_NCS_MSG_FORMAT_IS_VALID(
              dec_info->i_msg_fmt_ver, AVND_AVND_SUBPART_VER_MIN,
              AVND_AVND_SUBPART_VER_MAX, avnd_avnd_msg_fmt_map_table)) {
        return NCSCC_RC_FAILURE;
      }

      rc = m_NCS_EDU_VER_EXEC(&cb->edu_hdl, avsv_edp_ndnd_msg, dec_info->io_uba,
                              EDP_OP_TYPE_DEC,
                              (AVSV_ND2ND_AVND_MSG **)&dec_info->o_msg,
                              &ederror, dec_info->i_msg_fmt_ver);
      if (rc != NCSCC_RC_SUCCESS) {
        if (dec_info->o_msg != nullptr) {
          avsv_nd2nd_avnd_msg_free(
              static_cast<AVSV_ND2ND_AVND_MSG *>(dec_info->o_msg));
          dec_info->o_msg = nullptr;
        }
        return rc;
      }
      break;

    case NCSMDS_SVC_ID_AVA:
      if (!m_NCS_MSG_FORMAT_IS_VALID(
              dec_info->i_msg_fmt_ver, AVND_AVA_SUBPART_VER_MIN,
              AVND_AVA_SUBPART_VER_MAX, avnd_ava_msg_fmt_map_table)) {
        return NCSCC_RC_FAILURE;
      }

      rc = m_NCS_EDU_VER_EXEC(&cb->edu_hdl, avsv_edp_nda_msg, dec_info->io_uba,
                              EDP_OP_TYPE_DEC,
                              (AVSV_NDA_AVA_MSG **)&dec_info->o_msg, &ederror,
                              dec_info->i_msg_fmt_ver);
      if (rc != NCSCC_RC_SUCCESS) {
        if (dec_info->o_msg != nullptr) {
          avsv_nda_ava_msg_free(
              static_cast<AVSV_NDA_AVA_MSG *>(dec_info->o_msg));
          dec_info->o_msg = nullptr;
        }
        return rc;
      }
      break;

    default:
      LOG_NO("%s: unknown 'from' SVC ID %u", __FUNCTION__,
             dec_info->i_fr_svc_id);
      rc = NCSCC_RC_FAILURE;
      break;
  }

  return rc;
}

/****************************************************************************
  Name          : avnd_mds_flat_dec

  Description   : This routine is invoked to decode AvA messages.

  Arguments     : cb       - ptr to the AvND control block
                  dec_info - ptr to the MDS decode info

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avnd_mds_flat_dec(AVND_CB *cb, MDS_CALLBACK_DEC_INFO *dec_info) {
  uint32_t rc = NCSCC_RC_SUCCESS;
  EDU_ERR ederror = static_cast<EDU_ERR>(0);

  switch (dec_info->i_fr_svc_id) {
    case NCSMDS_SVC_ID_AVA:
      if (!m_NCS_MSG_FORMAT_IS_VALID(
              dec_info->i_msg_fmt_ver, AVND_AVA_SUBPART_VER_MIN,
              AVND_AVA_SUBPART_VER_MAX, avnd_ava_msg_fmt_map_table)) {
        return NCSCC_RC_FAILURE;
      }

      rc = avnd_mds_flat_ava_dec(cb, dec_info);
      break;

    case NCSMDS_SVC_ID_AVD:
      if (!m_NCS_MSG_FORMAT_IS_VALID(
              dec_info->i_msg_fmt_ver, AVND_AVD_SUBPART_VER_MIN,
              AVND_AVD_SUBPART_VER_MAX, avd_avnd_msg_fmt_map_table)) {
        LOG_ER("%s,%u: wrong msg fmt not valid %u", __FUNCTION__, __LINE__,
               dec_info->i_msg_fmt_ver);
        return NCSCC_RC_FAILURE;
      }

      rc = m_NCS_EDU_VER_EXEC(
          &cb->edu_hdl, avsv_edp_dnd_msg, dec_info->io_uba, EDP_OP_TYPE_DEC,
          (AVSV_DND_MSG **)&dec_info->o_msg, &ederror, dec_info->i_msg_fmt_ver);
      if (rc != NCSCC_RC_SUCCESS) {
        if (dec_info->o_msg != nullptr) {
          avsv_dnd_msg_free(static_cast<AVSV_DND_MSG *>(dec_info->o_msg));
          dec_info->o_msg = nullptr;
        }
        return rc;
      }
      break;

    case NCSMDS_SVC_ID_AVND:
      if (!m_NCS_MSG_FORMAT_IS_VALID(
              dec_info->i_msg_fmt_ver, AVND_AVND_SUBPART_VER_MIN,
              AVND_AVND_SUBPART_VER_MAX, avnd_avnd_msg_fmt_map_table)) {
        return NCSCC_RC_FAILURE;
      }

      rc = m_NCS_EDU_VER_EXEC(&cb->edu_hdl, avsv_edp_ndnd_msg, dec_info->io_uba,
                              EDP_OP_TYPE_DEC,
                              (AVSV_ND2ND_AVND_MSG **)&dec_info->o_msg,
                              &ederror, dec_info->i_msg_fmt_ver);
      if (rc != NCSCC_RC_SUCCESS) {
        if (dec_info->o_msg != nullptr) {
          avsv_nd2nd_avnd_msg_free(
              static_cast<AVSV_ND2ND_AVND_MSG *>(dec_info->o_msg));
          dec_info->o_msg = nullptr;
        }
        return rc;
      }
      break;

    default:
      LOG_NO("%s: unknown 'from' SVC ID %u", __FUNCTION__,
             dec_info->i_fr_svc_id);
      rc = NCSCC_RC_FAILURE;
      break;
  }

  return rc;
}

/****************************************************************************
  Name          : avnd_mds_flat_ava_dec

  Description   : This routine is invoked to (flat) decode AvA message.

  Arguments     : cb       - ptr to the AvND control block
                  dec_info - ptr to the MDS decode info

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avnd_mds_flat_ava_dec(AVND_CB *cb, MDS_CALLBACK_DEC_INFO *dec_info) {
  AVSV_NDA_AVA_MSG *ava_msg = 0;
  uint32_t rc = NCSCC_RC_FAILURE;

  ava_msg =
      static_cast<AVSV_NDA_AVA_MSG *>(calloc(1, sizeof(AVSV_NDA_AVA_MSG)));
  if (!ava_msg) {
    rc = NCSCC_RC_FAILURE;
    goto err;
  }

  /* decode the msg */
  rc = ncs_decode_n_octets_from_uba(dec_info->io_uba, (uint8_t *)ava_msg,
                                    sizeof(AVSV_NDA_AVA_MSG));
  if (NCSCC_RC_SUCCESS != rc) goto err;

  /* decode individual ava msg (if not decoded above) */
  switch (ava_msg->type) {
    case AVSV_AVA_API_MSG:
      switch (ava_msg->info.api_info.type) {
        case AVSV_AMF_FINALIZE:
          if (osaf_is_an_extended_name(
                  &ava_msg->info.api_info.param.finalize.comp_name)) {
            osaf_decode_sanamet(
                dec_info->io_uba,
                &ava_msg->info.api_info.param.finalize.comp_name);
          }
          break;

        case AVSV_AMF_COMP_REG:
          if (osaf_is_an_extended_name(
                  &ava_msg->info.api_info.param.reg.comp_name)) {
            osaf_decode_sanamet(dec_info->io_uba,
                                &ava_msg->info.api_info.param.reg.comp_name);
          }

          if (osaf_is_an_extended_name(
                  &ava_msg->info.api_info.param.reg.proxy_comp_name)) {
            osaf_decode_sanamet(
                dec_info->io_uba,
                &ava_msg->info.api_info.param.reg.proxy_comp_name);
          }
          break;

        case AVSV_AMF_COMP_UNREG:
          if (osaf_is_an_extended_name(
                  &ava_msg->info.api_info.param.unreg.comp_name)) {
            osaf_decode_sanamet(dec_info->io_uba,
                                &ava_msg->info.api_info.param.unreg.comp_name);
          }

          if (osaf_is_an_extended_name(
                  &ava_msg->info.api_info.param.unreg.proxy_comp_name)) {
            osaf_decode_sanamet(
                dec_info->io_uba,
                &ava_msg->info.api_info.param.unreg.proxy_comp_name);
          }
          break;

        case AVSV_AMF_PM_START:
          if (osaf_is_an_extended_name(
                  &ava_msg->info.api_info.param.pm_start.comp_name)) {
            osaf_decode_sanamet(
                dec_info->io_uba,
                &ava_msg->info.api_info.param.pm_start.comp_name);
          }
          break;

        case AVSV_AMF_PM_STOP:
          if (osaf_is_an_extended_name(
                  &ava_msg->info.api_info.param.pm_stop.comp_name)) {
            osaf_decode_sanamet(
                dec_info->io_uba,
                &ava_msg->info.api_info.param.pm_stop.comp_name);
          }
          break;

        case AVSV_AMF_HC_START:
          if (osaf_is_an_extended_name(
                  &ava_msg->info.api_info.param.hc_start.comp_name)) {
            osaf_decode_sanamet(
                dec_info->io_uba,
                &ava_msg->info.api_info.param.hc_start.comp_name);
          }

          if (osaf_is_an_extended_name(
                  &ava_msg->info.api_info.param.hc_start.proxy_comp_name)) {
            osaf_decode_sanamet(
                dec_info->io_uba,
                &ava_msg->info.api_info.param.hc_start.proxy_comp_name);
          }

          break;

        case AVSV_AMF_HC_STOP:
          if (osaf_is_an_extended_name(
                  &ava_msg->info.api_info.param.hc_stop.comp_name)) {
            osaf_decode_sanamet(
                dec_info->io_uba,
                &ava_msg->info.api_info.param.hc_stop.comp_name);
          }

          if (osaf_is_an_extended_name(
                  &ava_msg->info.api_info.param.hc_stop.proxy_comp_name)) {
            osaf_decode_sanamet(
                dec_info->io_uba,
                &ava_msg->info.api_info.param.hc_stop.proxy_comp_name);
          }
          break;

        case AVSV_AMF_HC_CONFIRM:
          if (osaf_is_an_extended_name(
                  &ava_msg->info.api_info.param.hc_confirm.comp_name)) {
            osaf_decode_sanamet(
                dec_info->io_uba,
                &ava_msg->info.api_info.param.hc_confirm.comp_name);
          }

          if (osaf_is_an_extended_name(
                  &ava_msg->info.api_info.param.hc_confirm.proxy_comp_name)) {
            osaf_decode_sanamet(
                dec_info->io_uba,
                &ava_msg->info.api_info.param.hc_confirm.proxy_comp_name);
          }
          break;

        case AVSV_AMF_CSI_QUIESCING_COMPLETE:
          if (osaf_is_an_extended_name(
                  &ava_msg->info.api_info.param.csiq_compl.comp_name)) {
            osaf_decode_sanamet(
                dec_info->io_uba,
                &ava_msg->info.api_info.param.csiq_compl.comp_name);
          }
          break;

        case AVSV_AMF_HA_STATE_GET:
          if (osaf_is_an_extended_name(
                  &ava_msg->info.api_info.param.ha_get.comp_name)) {
            osaf_decode_sanamet(dec_info->io_uba,
                                &ava_msg->info.api_info.param.ha_get.comp_name);
          }

          if (osaf_is_an_extended_name(
                  &ava_msg->info.api_info.param.ha_get.csi_name)) {
            osaf_decode_sanamet(dec_info->io_uba,
                                &ava_msg->info.api_info.param.ha_get.csi_name);
          }
          break;

        case AVSV_AMF_PG_START:
          if (osaf_is_an_extended_name(
                  &ava_msg->info.api_info.param.pg_start.csi_name)) {
            osaf_decode_sanamet(
                dec_info->io_uba,
                &ava_msg->info.api_info.param.pg_start.csi_name);
          }
          break;

        case AVSV_AMF_PG_STOP:
          if (osaf_is_an_extended_name(
                  &ava_msg->info.api_info.param.pg_stop.csi_name)) {
            osaf_decode_sanamet(dec_info->io_uba,
                                &ava_msg->info.api_info.param.pg_stop.csi_name);
          }
          break;

        case AVSV_AMF_ERR_REP:
          if (osaf_is_an_extended_name(
                  &ava_msg->info.api_info.param.err_rep.err_comp)) {
            osaf_decode_sanamet(dec_info->io_uba,
                                &ava_msg->info.api_info.param.err_rep.err_comp);
          }
          break;

        case AVSV_AMF_ERR_CLEAR:
          if (osaf_is_an_extended_name(
                  &ava_msg->info.api_info.param.err_clear.comp_name)) {
            osaf_decode_sanamet(
                dec_info->io_uba,
                &ava_msg->info.api_info.param.err_clear.comp_name);
          }
          break;

        case AVSV_AMF_RESP:
          if (osaf_is_an_extended_name(
                  &ava_msg->info.api_info.param.resp.comp_name)) {
            osaf_decode_sanamet(dec_info->io_uba,
                                &ava_msg->info.api_info.param.resp.comp_name);
          }
          break;

        default:
          break;
      } /* switch */
      break;
    case AVSV_AVND_AMF_CBK_MSG:
    case AVSV_AVND_AMF_API_RESP_MSG:
    default:
      osafassert(0);
  } /* switch */

  /* decode over successfully */
  dec_info->o_msg = (NCSCONTEXT)ava_msg;

  return rc;

err:
  /* free ava-msg */
  if (ava_msg) avsv_nda_ava_msg_free(ava_msg);
  dec_info->o_msg = 0;
  return rc;
}

/****************************************************************************
  Name          : avnd_mds_send

  Description   : This routine sends the mds message to AvA or AvD or AvND.

  Arguments     : cb       - ptr to the AvND control block
                  msg      - ptr to the message
                  dest     - ptr to the MDS destination
                  mds_ctxt - ptr to the MDS message context

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avnd_mds_send(AVND_CB *cb, AVND_MSG *msg, MDS_DEST *dest,
                       MDS_SYNC_SND_CTXT *mds_ctxt) {
  NCSMDS_INFO mds_info;
  MDS_SEND_INFO *send_info = &mds_info.info.svc_send;
  uint32_t rc = NCSCC_RC_FAILURE;

  TRACE_ENTER2("Msg type '%u'", msg->type);

  /* populate the mds params */
  memset(&mds_info, 0, sizeof(NCSMDS_INFO));

  mds_info.i_mds_hdl = cb->mds_hdl;
  mds_info.i_svc_id = NCSMDS_SVC_ID_AVND;
  mds_info.i_op = MDS_SEND;

  send_info->i_msg = (NCSCONTEXT)msg;
  send_info->i_priority = MDS_SEND_PRIORITY_MEDIUM;

  switch (msg->type) {
    case AVND_MSG_AVD:
      send_info->i_to_svc = NCSMDS_SVC_ID_AVD;
      /* Don't send any messages if we are shutting down */
      if (m_AVND_IS_SHUTTING_DOWN(cb) && msg->info.avd->msg_type != AVSV_N2D_NODE_DOWN_MSG) {
        TRACE_1("Shutting down, not sending msg to AMFD.");
        goto done;
      }
      break;

    case AVND_MSG_AVA:
      send_info->i_to_svc = NCSMDS_SVC_ID_AVA;
      break;

    default:
      osafassert(0);
      break;
  }

  if (!mds_ctxt) {
    /* regular send */
    MDS_SENDTYPE_SND_INFO *send = &send_info->info.snd;

    send_info->i_sendtype = MDS_SENDTYPE_SND;
    send->i_to_dest = *dest;
  } else {
    /* response message (somebody is waiting for it) */
    MDS_SENDTYPE_RSP_INFO *resp = &send_info->info.rsp;

    send_info->i_sendtype = MDS_SENDTYPE_RSP;
    resp->i_sender_dest = *dest;
    resp->i_msg_ctxt = *mds_ctxt;
  }

  /* send the message */
  rc = ncsmds_api(&mds_info);
  if (NCSCC_RC_SUCCESS != rc) {
    if (m_AVND_IS_SHUTTING_DOWN(cb)) {
      /* Don't log anything if we are shutting down */
      TRACE("ncsmds_api for %u FAILED, dest=%" PRIx64, send_info->i_sendtype,
            *dest);
    } else
      LOG_ER("ncsmds_api for %u FAILED, dest=%" PRIx64, send_info->i_sendtype,
             *dest);
  }
done:

  TRACE_LEAVE2("%u", rc);
  return rc;
}

/****************************************************************************
  Name          : avnd_mds_red_send

  Description   : This routine sends the mds message to specified AvD.

  Arguments     : cb       - ptr to the AvND control block
                  msg      - ptr to the message
                  dest     - ptr to the MDS destination
                  adest    - ptr to the MDS adest(anchor)

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : This funtion as of now is only used to send the ACK-NACK msg
                  to AvD. This funtion is introduced to overcome the problem
                  of MDS dropping a msg when the role has changed but MDS in
                  AvND has not updated its tables about the role change.
                  Due to this problem MDS will try to send the msg to old
                  active which may not be there in the system and hence the
                  msg will be dropped.
                  With this funtion we are sending msg to the new active AvD
                  directly, without looking for its MDS role as seen by AvND.
******************************************************************************/
uint32_t avnd_mds_red_send(AVND_CB *cb, AVND_MSG *msg, MDS_DEST *dest,
                           MDS_DEST *adest) {
  NCSMDS_INFO mds_info;
  MDS_SEND_INFO *send_info = &mds_info.info.svc_send;
  MDS_SENDTYPE_RED_INFO *send = &send_info->info.red;
  uint32_t rc = NCSCC_RC_SUCCESS;
  TRACE_ENTER2("Msg type '%u'", msg->type);

  /* Don't send any messages if we are shutting down */
  if (m_AVND_IS_SHUTTING_DOWN(cb)) goto done;

  /* populate the mds params */
  memset(&mds_info, 0, sizeof(NCSMDS_INFO));

  mds_info.i_mds_hdl = cb->mds_hdl;
  mds_info.i_svc_id = NCSMDS_SVC_ID_AVND;
  mds_info.i_op = MDS_SEND;

  send_info->i_msg = (NCSCONTEXT)msg;
  send_info->i_priority = MDS_SEND_PRIORITY_MEDIUM;

  /* this is be used only for ACK NACK for AVD */
  if (msg->type != AVND_MSG_AVD) osafassert(0);

  send_info->i_to_svc = NCSMDS_SVC_ID_AVD;
  send_info->i_sendtype = MDS_SENDTYPE_RED;
  send->i_to_vdest = *dest;
  send->i_to_anc = *adest; /* assumption-:ADEST is same as anchor */

  /* send the message */
  rc = ncsmds_api(&mds_info);
  if (NCSCC_RC_SUCCESS != rc) {
    if (m_AVND_IS_SHUTTING_DOWN(cb)) {
      /* Don't log anything if we are shutting down */
      TRACE("AVND MDS send failed: Msg type = %u, vdest = %" PRIu64
            ", anchor = %" PRIu64,
            msg->type, send->i_to_vdest, send->i_to_anc);
    } else
      LOG_CR("AVND MDS send failed: Msg type = %u, vdest = %" PRIu64
             ", anchor = %" PRIu64,
             msg->type, send->i_to_vdest, send->i_to_anc);
  }
done:

  TRACE_LEAVE2("rc '%u'", rc);
  return rc;
}

/****************************************************************************
 * Name          : avnd_mds_param_get
 *
 * Description   : This routine gets the mds handle & AvND MDS address.
 *
 * Arguments     : cb - ptr to the AvND control block
 *
 * Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE
 *
 * Notes         : None.
 *****************************************************************************/
uint32_t avnd_mds_param_get(AVND_CB *cb) {
  NCSADA_INFO ada_info;

  memset(&ada_info, 0, sizeof(ada_info));

  ada_info.req = NCSADA_GET_HDLS;

  /* invoke ada request */
  uint32_t rc = ncsada_api(&ada_info);
  if (NCSCC_RC_SUCCESS != rc) goto done;

  /* store values returned by ada */
  cb->mds_hdl = ada_info.info.adest_get_hdls.o_mds_pwe1_hdl;
  cb->avnd_dest = ada_info.info.adest_get_hdls.o_adest;

  /* get the node-id from mds */
  cb->node_info.nodeId = m_NCS_NODE_ID_FROM_MDS_DEST(cb->avnd_dest);

done:
  return rc;
}

/****************************************************************************
 * Name          : avnd_avnd_mds_send
 *
 * Description   : This routine send messages to AvND in ASYNC.
 *
 * Arguments     : cb - ptr to the AvND control block
 *                       i_msg -ptr to the AvA message
 *
 * Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE
 *
 * Notes         : None.
 *****************************************************************************/
uint32_t avnd_avnd_mds_send(AVND_CB *cb, MDS_DEST mds_dest, AVND_MSG *i_msg) {
  NCSMDS_INFO mds_info;
  uint32_t rc = NCSCC_RC_SUCCESS;

  TRACE_ENTER();
  memset(&mds_info, 0, sizeof(NCSMDS_INFO));

  mds_info.i_mds_hdl = cb->mds_hdl;

  mds_info.i_svc_id = NCSMDS_SVC_ID_AVND;

  mds_info.i_op = MDS_SEND;

  MDS_SEND_INFO *send_info = &mds_info.info.svc_send;
  MDS_SENDTYPE_SNDRSP_INFO *send = &send_info->info.sndrsp;

  /* populate the send info */
  send_info->i_msg = (NCSCONTEXT)i_msg;

  if (m_MDS_DEST_IS_AN_ADEST(mds_dest))
    send_info->i_to_svc = NCSMDS_SVC_ID_AVND;
  else
    send_info->i_to_svc = NCSMDS_SVC_ID_AVND_CNTLR;

  send_info->i_priority =
      MDS_SEND_PRIORITY_MEDIUM; /* Discuss the priority assignments TBD */
  send_info->i_sendtype = MDS_SENDTYPE_SND;
  send->i_to_dest = mds_dest;

  /* send the message & block until AvND responds or operation times out */
  rc = ncsmds_api(&mds_info);

  TRACE_LEAVE();
  return rc;
}

/****************************************************************************
  Name          : avnd_mds_set_vdest_role

  Description   : This routine is used for setting the VDEST role.

  Arguments     : cb - ptr to the AVND control block
                  role - Set the role.

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avnd_mds_set_vdest_role(AVND_CB *cb, SaAmfHAStateT role) {
  NCSVDA_INFO vda_info;

  if (SA_AMF_HA_QUIESCED == role) {
    cb->is_quisced_set = true;
  }
  memset(&vda_info, '\0', sizeof(NCSVDA_INFO));

  /* set the role of the vdest */
  vda_info.req = NCSVDA_VDEST_CHG_ROLE;
  vda_info.info.vdest_chg_role.i_new_role = static_cast<V_DEST_RL>(role);
  vda_info.info.vdest_chg_role.i_vdest = cb->avnd_mbcsv_vaddr;

  if (ncsvda_api(&vda_info) != NCSCC_RC_SUCCESS) {
    return NCSCC_RC_FAILURE;
  }

  return NCSCC_RC_SUCCESS;
}

/****************************************************************************\
 PROCEDURE NAME : avnd_mds_quiesced_process

 DESCRIPTION    : Sending the event to it's mail box

 ARGUMENTS      : cb : IFD control Block

 RETURNS        : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE
\*****************************************************************************/
uint32_t avnd_mds_quiesced_process(AVND_CB *cb) {
  AVND_EVT *evt = 0;
  uint32_t rc = NCSCC_RC_FAILURE;

  evt = avnd_evt_create(cb, AVND_EVT_HA_STATE_CHANGE, 0, nullptr, 0, 0, 0);
  if (nullptr == evt) {
    LOG_ER("%s, Event is NULL", __FUNCTION__);
  } else {
    /* Don't use avail_state_avnd as this is not yet updated. It will be
       updated during processing of this event. */
    evt->info.ha_state_change.ha_state = SA_AMF_HA_QUIESCED;
  }

  /* send the event */
  if (evt) rc = avnd_evt_send(cb, evt);

  /* if failure, free the event */
  if (NCSCC_RC_SUCCESS != rc && evt) avnd_evt_destroy(evt);

  return rc;
}
