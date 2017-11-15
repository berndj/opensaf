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

  DESCRIPTION:

  This module contains the PG processing routines.

..............................................................................

  FUNCTIONS INCLUDED in this module:

  avd_pg_trk_act_func - protection group track start/stop message handler.

  avd_pg_csi_node_add     - Adds pg recs to the csi & node.
  avd_pg_csi_node_del     - Deletes pg recs from the csi & node.
  avd_pg_csi_node_del_all - Deletes all the pg node recs from the csi.
  avd_pg_node_csi_del_all - Deletes all the pg-csi recs from the node

******************************************************************************
*/

#include "amf/amfd/amfd.h"

/*****************************************************************************
 * Function: avd_pg_trk_act_func
 *
 * Purpose: This function is the handler for PG track start & stop event. If
 *          it is a track start request, the track request is appended to the
 *          CSI and the current members of the PG are sent to AvND. If it is
 *          a stop request, the request is deleted from the CSI & and ack is
 *          sent to AvND.
 *
 * Input: cb - the AVD control block
 *        evt - The event information.
 *
 * Returns: None.
 *
 *
 * NOTES: For optimizing the AvND down handling, node structure also
 *        maintains a list of all the CSIs that it tracks. The corresponding
 *        updation is also done.
 **************************************************************************/
void avd_pg_trk_act_evh(AVD_CL_CB *cb, AVD_EVT *evt) {
  AVSV_N2D_PG_TRACK_ACT_MSG_INFO *info =
      &evt->info.avnd_msg->msg_info.n2d_pg_trk_act;
  AVD_DND_MSG *n2d_msg = evt->info.avnd_msg;
  AVD_AVND *node = 0;
  AVD_CSI *csi = 0;

  TRACE_ENTER2("%s", osaf_extended_name_borrow(&info->csi_name));

  /* run sanity check on the msg */
  if ((node = avd_msg_sanity_chk(evt, info->node_id, AVSV_N2D_PG_TRACK_ACT_MSG,
                                 info->msg_id)) == nullptr)
    goto done;

  /* Update the receive id for the node */
  m_AVD_SET_AVND_RCV_ID(cb, node, (n2d_msg->msg_info.n2d_pg_trk_act.msg_id));

  if ((node->node_state == AVD_AVND_STATE_ABSENT) ||
      (node->node_state == AVD_AVND_STATE_GO_DOWN)) {
    LOG_ER("%s: invalid node state %u for node id '%u'", __FUNCTION__,
           node->node_state, info->node_id);
    goto done;
  }

  /* get the node & csi */
  csi = csi_db->find(Amf::to_string(&info->csi_name));

  /* update the pg lists maintained on csi & node */
  if (csi != nullptr) {
    switch (info->actn) {
      case AVSV_PG_TRACK_ACT_START:
        /* add the relvant recs to the lists */
        avd_pg_csi_node_add(cb, csi, node);
        break;

      case AVSV_PG_TRACK_ACT_STOP:
        /* delete the relvant recs from the lists */
        avd_pg_csi_node_del(cb, csi, node);
        break;

      default:
        osafassert(0);
    } /* switch */
  }

  /* send back the response */
  if (NCSCC_RC_SUCCESS != avd_snd_pg_resp_msg(cb, node, csi, info))
    LOG_ER("%s:%u: %u", __FILE__, __LINE__, node->node_info.nodeId);

done:
  avsv_dnd_msg_free(evt->info.avnd_msg);
  evt->info.avnd_msg = nullptr;
  TRACE_LEAVE();
}

/*****************************************************************************
 * Function: avd_pg_susi_chg_prc
 *
 * Purpose: This function does the PG processing for any change in the SU-SI
 *          relationship (new/modify). It scans the comp-csi list &
 *          generates PG updates for each node that started PG tracking on the
 *          corresponding CSI.
 *
 * Input:   cb     - the AVD control block
 *          susi   - ptr to the su-si relationship block
 *
 * Returns: None.
 *
 * NOTES: None
 **************************************************************************/
uint32_t avd_pg_susi_chg_prc(AVD_CL_CB *cb, AVD_SU_SI_REL *susi) {
  AVD_COMP_CSI_REL *comp_csi = 0;
  uint32_t rc = NCSCC_RC_SUCCESS;
  TRACE_ENTER();

  /* scan the comp-csi rel list & generate pg upd for each track req */
  for (comp_csi = susi->list_of_csicomp; comp_csi;
       comp_csi = comp_csi->susi_csicomp_next) {
    rc = avd_pg_compcsi_chg_prc(cb, comp_csi, false);
    if (NCSCC_RC_SUCCESS != rc) break;
  }
  TRACE_LEAVE();
  return rc;
}

/*****************************************************************************
 * Function: avd_pg_compcsi_chg_prc
 *
 * Purpose: This function does the PG processing for any change in the comp
 *          csi relationship (new/modify/delete). It generates PG updates for
 *          each node that started PG tracking on the corresponding CSI.
 *
 * Input:   cb       - the AVD control block
 *          comp_csi - ptr to the comp-csi relationship block
 *          is_rmv   - indicates if the susi relationship is removed
 *
 * Returns: None.
 *
 * NOTES: None
 **************************************************************************/
uint32_t avd_pg_compcsi_chg_prc(AVD_CL_CB *cb, AVD_COMP_CSI_REL *comp_csi,
                                bool is_rmv) {
  AVD_PG_CSI_NODE *csi_node = 0;
  uint32_t rc = NCSCC_RC_SUCCESS;
  TRACE_ENTER();

  /* generate pg upd for each track req */
  for (csi_node = (AVD_PG_CSI_NODE *)m_NCS_DBLIST_FIND_FIRST(
           &comp_csi->csi->pg_node_list);
       csi_node; csi_node = (AVD_PG_CSI_NODE *)m_NCS_DBLIST_FIND_NEXT(
                     &csi_node->csi_dll_node)) {
    rc = avd_snd_pg_upd_msg(cb, csi_node->node, comp_csi,
                            (true == is_rmv) ? SA_AMF_PROTECTION_GROUP_REMOVED
                                             : SA_AMF_PROTECTION_GROUP_ADDED,
                            std::string(""));
  } /* for */
  TRACE_LEAVE2("%u", rc);
  return rc;
}

/*****************************************************************************
 * Function: avd_pg_csi_node_add
 *
 * Purpose:  This function links the node & CSI ptrs to the AvD PG records
 *           maintained by CSI & node.
 *
 * Input: cb      - the AVD control block
 *        csi     - ptr to the csi structure.
 *        node    - ptr to the node structure.
 *
 * Returns: NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE
 *
 * NOTES: None.
 **************************************************************************/
uint32_t avd_pg_csi_node_add(AVD_CL_CB *cb, AVD_CSI *csi, AVD_AVND *node) {
  AVD_PG_CSI_NODE *pg_csi_node = 0;
  AVD_PG_NODE_CSI *pg_node_csi = 0;

  TRACE_ENTER();

  /* alloc the pg-csi-node & pg-node-csi recs */
  pg_csi_node = new AVD_PG_CSI_NODE();
  pg_node_csi = new AVD_PG_NODE_CSI();

  /* add the node to the pg list maintained by csi */
  pg_csi_node->node = node;
  pg_csi_node->csi_dll_node.key = (uint8_t *)&pg_csi_node->node;
  ncs_db_link_list_add(&csi->pg_node_list, &pg_csi_node->csi_dll_node);

  /* add the csi to the pg list maintained by node */
  pg_node_csi->csi = csi;
  pg_node_csi->node_dll_node.key = (uint8_t *)&pg_node_csi->csi;
  ncs_db_link_list_add(&node->pg_csi_list, &pg_node_csi->node_dll_node);

  return NCSCC_RC_SUCCESS;
}

/*****************************************************************************
 * Function: avd_pg_csi_node_del
 *
 * Purpose:  This function unlinks the node & CSI ptrs from the AvD PG records
 *           maintained by CSI & node.
 *
 * Input: cb      - the AVD control block
 *        csi     - ptr to the csi structure.
 *        node    - ptr to the node structure.
 *
 * Returns: None.
 *
 * NOTES: None.
 **************************************************************************/
void avd_pg_csi_node_del(AVD_CL_CB *cb, AVD_CSI *csi, AVD_AVND *node) {
  AVD_PG_CSI_NODE *pg_csi_node = 0;
  AVD_PG_NODE_CSI *pg_node_csi = 0;

  TRACE_ENTER();

  /* free from pg list maintained on csi */
  pg_csi_node = (AVD_PG_CSI_NODE *)ncs_db_link_list_remove(&csi->pg_node_list,
                                                           (uint8_t *)&node);
  if (pg_csi_node) delete pg_csi_node;

  /* free from pg list maintained on node */
  pg_node_csi = (AVD_PG_NODE_CSI *)ncs_db_link_list_remove(&node->pg_csi_list,
                                                           (uint8_t *)&csi);
  if (pg_node_csi) delete pg_node_csi;

  TRACE_LEAVE();
  return;
}

/*****************************************************************************
 * Function: avd_pg_csi_node_del_all
 *
 * Purpose:  This function deletes all the records in the PG node list
 *           maintained by the CSI. It also deletes the corresponding pg
 *           association records on the corresponding nodes.
 *
 * Input: cb  - the AVD control block
 *        csi - ptr to the csi structure.
 *
 * Returns: None.
 *
 * NOTES: None.
 **************************************************************************/
void avd_pg_csi_node_del_all(AVD_CL_CB *cb, AVD_CSI *csi) {
  AVD_PG_CSI_NODE *curr = 0;

  TRACE_ENTER();

  while (0 != (curr = (AVD_PG_CSI_NODE *)m_NCS_DBLIST_FIND_FIRST(
                   &csi->pg_node_list)))
    avd_pg_csi_node_del(cb, csi, curr->node);

  TRACE_LEAVE();
  return;
}

/*****************************************************************************
 * Function: avd_pg_node_csi_del_all
 *
 * Purpose:  This function deletes all the records in the PG CSI list
 *           maintained by the node. It also deletes all the corresponding
 *           association records on CSIs.
 *
 * Input: cb   - the AVD control block
 *        node - ptr to the avnd structure.
 *
 * Returns: None.
 *
 * NOTES: None.
 **************************************************************************/
void avd_pg_node_csi_del_all(AVD_CL_CB *cb, AVD_AVND *node) {
  AVD_PG_NODE_CSI *curr = 0;

  TRACE_ENTER();

  while (0 != (curr = (AVD_PG_NODE_CSI *)m_NCS_DBLIST_FIND_FIRST(
                   &node->pg_csi_list)))
    avd_pg_csi_node_del(cb, curr->csi, node);

  return;
}
