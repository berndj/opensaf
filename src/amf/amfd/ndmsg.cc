/*      -*- OpenSAF  -*-
 *
 * (C) Copyright 2008 The OpenSAF Foundation
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

  DESCRIPTION:This module does the encode/decode,copy,free,transmission
  and reception  of messages between the Availability Director and
  Availability Node Director.

..............................................................................

  FUNCTIONS INCLUDED in this module:

  avd_mds_enc - encodes AVD to AvND messages.
  avd_mds_enc_flat  - encodes flat AVD to AvND messages. Dummy not required.
  avd_mds_cpy - copies AVD to AvND messages.
  avd_mds_dec - decodes AvND to AVD messages.
  avd_mds_dec_flat - decodes flat AvND to AVD messages. Dummy not required.
  avd_d2n_msg_snd - transmits message to node director.
  avd_d2n_msg_bcast - broadcasts message to all node director.
  avd_n2d_msg_rcv - Procresses messages from AvND.

******************************************************************************
*/

/*
 * Module Inclusion Control...
 */

#include "amf/amfd/amfd.h"

/****************************************************************************
  Name          : avd_mds_enc

  Description   : This is a callback routine that is invoked to encode
                  AvND-AvD messages.

  Arguments     : enc_info  - ptr to the MDS encode info.

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         :
******************************************************************************/
uint32_t avd_mds_enc(MDS_CALLBACK_ENC_INFO *enc_info) {
  AVD_CL_CB *cb = avd_cb;
  EDU_ERR ederror = static_cast<EDU_ERR>(0);
  uint32_t rc;

  rc = m_NCS_EDU_VER_EXEC(&cb->mds_edu_hdl, avsv_edp_dnd_msg, enc_info->io_uba,
                          EDP_OP_TYPE_ENC, enc_info->i_msg, &ederror,
                          enc_info->o_msg_fmt_ver);

  if (rc != NCSCC_RC_SUCCESS) {
    LOG_ER("%s: encode failed %u", __FUNCTION__, rc);
    return rc;
  }

  return rc;
}

/****************************************************************************
  Name          : avd_mds_enc_flat

  Description   : This is a callback routine that is invoked to encode
                  AvND-AvD messages in a flat buffer.

  Arguments     : enc_info  - ptr to the MDS encode info.

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         :
******************************************************************************/
uint32_t avd_mds_enc_flat(MDS_CALLBACK_ENC_FLAT_INFO *enc_info) {
  return avd_mds_enc((MDS_CALLBACK_ENC_INFO *)enc_info);
}

/****************************************************************************
  Name          : avd_mds_cpy

  Description   : This is a callback routine that is invoked to copy
                  AvND-AvD messages.

  Arguments     : cpy_info - ptr to the MDS copy info

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avd_mds_cpy(MDS_CALLBACK_COPY_INFO *cpy_info) {
  AVD_DND_MSG *dst_msg;

  TRACE_ENTER();

  if (cpy_info->i_msg == nullptr) {
    LOG_ER("%s: no msg", __FUNCTION__);
    return NCSCC_RC_FAILURE;
  }

  dst_msg = new AVD_DND_MSG;

  if (NCSCC_RC_SUCCESS !=
      avsv_dnd_msg_copy(dst_msg, (AVSV_DND_MSG *)cpy_info->i_msg)) {
    LOG_ER("%s: copy failed", __FUNCTION__);
    delete dst_msg;
    return NCSCC_RC_FAILURE;
  }

  cpy_info->o_cpy = (NCSCONTEXT)dst_msg;
  return NCSCC_RC_SUCCESS;
}

/****************************************************************************
  Name          : avd_mds_dec

  Description   : This is a callback routine that is invoked to decode
                  AvD-AvND messages.

  Arguments     : dec_info  - ptr to the MDS decode info.

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avd_mds_dec(MDS_CALLBACK_DEC_INFO *dec_info) {
  AVD_CL_CB *cb = avd_cb;
  EDU_ERR ederror = static_cast<EDU_ERR>(0);
  uint32_t rc;

  rc = m_NCS_EDU_VER_EXEC(&cb->mds_edu_hdl, avsv_edp_dnd_msg, dec_info->io_uba,
                          EDP_OP_TYPE_DEC, &dec_info->o_msg, &ederror,
                          dec_info->i_msg_fmt_ver);

  if (rc != NCSCC_RC_SUCCESS) {
    LOG_ER("%s: decode failed %u %u", __FUNCTION__, rc, ederror);

    if (dec_info->o_msg != nullptr) {
      avsv_dnd_msg_free(static_cast<AVSV_DND_MSG *>(dec_info->o_msg));
      dec_info->o_msg = nullptr;
    }

    return rc;
  }

  return rc;
}

/****************************************************************************
  Name          : avd_mds_dec_flat

  Description   : This is a callback routine that is invoked to decode
                  AvD-AvND messages from flat buffers.

  Arguments     : dec_info  - ptr to the MDS decode info.

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avd_mds_dec_flat(MDS_CALLBACK_DEC_FLAT_INFO *dec_info) {
  return avd_mds_dec((MDS_CALLBACK_DEC_INFO *)dec_info);
}

/****************************************************************************
  Name          : avd_d2n_msg_enqueue

  Description   : This is a routine used for enqueuing messages which are being
                  sent to the node director.

  Arguments     : cb     :  The control block of AvD
                  snd_msg: the send message that needs to be sent.

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
static void avd_d2n_msg_enqueue(AVD_CL_CB *cb, NCSMDS_INFO *snd_mds) {
  AVSV_ND_MSG_QUEUE *nd_msg;
  /*
   * Allocate the message and put it in the queue.
   * We will have to send it at later point of time.
   */
  nd_msg = new AVSV_ND_MSG_QUEUE();

  memcpy(&nd_msg->snd_msg, snd_mds, sizeof(NCSMDS_INFO));

  cb->nd_msg_queue_list.push(nd_msg);
}

/****************************************************************************
  Name          : avd_d2n_msg_dequeue

  Description   : This is a routine used for dequeuing messages which are to be
                  sent to the node director.

  Arguments     : cb     :  The control block of AvD

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avd_d2n_msg_dequeue(AVD_CL_CB *cb) {
  AVSV_ND_MSG_QUEUE *queue_elem;
  uint32_t rc = NCSCC_RC_SUCCESS;
  /*
   * De-queue messages from the Queue and then do the MDS send.
   */

  while (!cb->nd_msg_queue_list.empty()) {
    queue_elem = cb->nd_msg_queue_list.front();
    cb->nd_msg_queue_list.pop();
    /*
     * Now do MDS send.
     */
    if ((rc = ncsmds_api(&queue_elem->snd_msg)) != NCSCC_RC_SUCCESS) {
      LOG_ER("%s: ncsmds_api failed %u", __FUNCTION__, rc);
    }

    d2n_msg_free((AVD_DND_MSG *)queue_elem->snd_msg.info.svc_send.i_msg);

    delete queue_elem;
  }

  return rc;
}

/****************************************************************************
  Name          : avd_d2n_msg_snd

  Description   : This is a routine invoked by AVD for transmitting
  messages to node director.

  Arguments     : cb     :  The control block of AvD
                  nd_node:  Node director node to which this message
                            will be sent.

                  snd_msg: the send message that needs to be sent.

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/

uint32_t avd_d2n_msg_snd(AVD_CL_CB *cb, AVD_AVND *nd_node,
                         AVD_DND_MSG *snd_msg) {
  NCSMDS_INFO snd_mds = {0};

  if (nd_node->adest == 0) {
    LOG_WA("Invalid adest for %x, msg type %u", nd_node->node_info.nodeId,
           snd_msg->msg_type);
    return NCSCC_RC_FAILURE;
  }

  snd_mds.i_mds_hdl = cb->adest_hdl;
  snd_mds.i_svc_id = NCSMDS_SVC_ID_AVD;
  snd_mds.i_op = MDS_SEND;
  snd_mds.info.svc_send.i_msg = (NCSCONTEXT)snd_msg;
  snd_mds.info.svc_send.i_to_svc = NCSMDS_SVC_ID_AVND;
  snd_mds.info.svc_send.i_priority = MDS_SEND_PRIORITY_HIGH;
  snd_mds.info.svc_send.i_sendtype = MDS_SENDTYPE_SND;
  snd_mds.info.svc_send.info.snd.i_to_dest = nd_node->adest;

  avd_d2n_msg_enqueue(cb, &snd_mds);

  return NCSCC_RC_SUCCESS;
}

/****************************************************************************
  Name          : avd_d2n_msg_bcast

  Description   : This is a routine invoked by AVD for broadcasting
  messages to node directors.

  Arguments     : cb     :  The control block of AvD
                  bcast_msg: the broadcast message that needs to be sent.

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/

uint32_t avd_d2n_msg_bcast(AVD_CL_CB *cb, AVD_DND_MSG *bcast_msg) {
  NCSMDS_INFO snd_mds = {0};
  uint32_t rc;

  snd_mds.i_mds_hdl = cb->adest_hdl;
  snd_mds.i_svc_id = NCSMDS_SVC_ID_AVD;
  snd_mds.i_op = MDS_SEND;
  snd_mds.info.svc_send.i_msg = (NCSCONTEXT)bcast_msg;
  snd_mds.info.svc_send.i_to_svc = NCSMDS_SVC_ID_AVND;
  snd_mds.info.svc_send.i_priority = MDS_SEND_PRIORITY_HIGH;
  snd_mds.info.svc_send.i_sendtype = MDS_SENDTYPE_BCAST;
  snd_mds.info.svc_send.info.bcast.i_bcast_scope = NCSMDS_SCOPE_NONE;

  if ((rc = ncsmds_api(&snd_mds)) != NCSCC_RC_SUCCESS) {
    LOG_ER("%s: ncsmds_api MDS_SEND failed %u", __FUNCTION__, rc);
    return rc;
  }

  return NCSCC_RC_SUCCESS;
}

/****************************************************************************
  Name          : avd_n2d_msg_rcv

  Description   : This routine is invoked by AvD when a message arrives
  from AvND. It converts the message to the corresponding event and posts
  the message to the mailbox for processing by the main loop.

  Arguments     : rcv_msg    -  ptr to the received message

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/

uint32_t avd_n2d_msg_rcv(AVD_DND_MSG *rcv_msg, NODE_ID node_id,
                         uint16_t msg_fmt_ver) {
  AVD_EVT *evt = AVD_EVT_NULL;
  AVD_CL_CB *cb = avd_cb;

  TRACE_ENTER();

  if (rcv_msg == nullptr) {
    LOG_ER("%s: no msg", __FUNCTION__);
    TRACE_LEAVE();
    return NCSCC_RC_FAILURE;
  }

  /* create the message event */
  evt = new AVD_EVT();

  if (node_id == cb->node_id_avd_other) {
    /* We need to maintain version information of peer AvND. We shouldn't
       send role change message to older version Controller AvND. */
    cb->peer_msg_fmt_ver = msg_fmt_ver;
  }

  switch (rcv_msg->msg_type) {
    case AVSV_N2D_ND_SISU_STATE_INFO_MSG:
      // 'offset lookup' can't be used for this
      evt->rcv_evt = AVD_EVT_ND_SISU_STATE_INFO_MSG;
      break;
    case AVSV_N2D_ND_CSICOMP_STATE_INFO_MSG:
      // 'offset lookup' can't be used for this
      evt->rcv_evt = AVD_EVT_ND_CSICOMP_STATE_INFO_MSG;
      break;
    default:
      evt->rcv_evt = static_cast<AVD_EVT_TYPE>(
          (rcv_msg->msg_type - AVSV_N2D_NODE_UP_MSG) + AVD_EVT_NODE_UP_MSG);
      break;
  }

  osafassert((AVD_EVT_INVALID < evt->rcv_evt) && (evt->rcv_evt < AVD_EVT_MAX));

  evt->info.avnd_msg = rcv_msg;

  NCS_IPC_PRIORITY priority = NCS_IPC_PRIORITY_HIGH;
  if (evt->rcv_evt == AVD_EVT_NODE_UP_MSG) {
    priority = NCS_IPC_PRIORITY_VERY_HIGH;
  }

  if (m_NCS_IPC_SEND(&cb->avd_mbx, evt, priority) !=
      NCSCC_RC_SUCCESS) {
    LOG_ER("%s: ncs_ipc_send failed", __FUNCTION__);
    avsv_dnd_msg_free(rcv_msg);
    evt->info.avnd_msg = nullptr;
    delete evt;
    TRACE_LEAVE();
    return NCSCC_RC_FAILURE;
  }

  TRACE_LEAVE();
  return NCSCC_RC_SUCCESS;
}
