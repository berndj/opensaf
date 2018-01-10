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

#ifndef CKPT_CKPTD_CPD_CB_H_
#define CKPT_CKPTD_CPD_CB_H_

#include <stdbool.h>
#include <saClm.h>
#include "cpd_tmr.h"

#define CPSV_CPD_MBCSV_VERSION 2

#define m_CPND_IS_ON_SCXB(m, n) ((m == n) ? 1 : 0)

#define m_CPD_IS_LOCAL_NODE(m, n) (m == n) ? 1 : 0
#define m_NCS_MDS_DEST_NODEID_EQUAL(m, n) (m == n) ? 1 : 0
#define m_CPND_NODE_ID_CMP(m, n) (m == n) ? 1 : 0
#define CPSV_WAIT_TIME 1000

/*30B Versioning Changes */
#define CPD_MDS_PVT_SUBPART_VERSION 3
/*CPD -CPND communication */
#define CPD_WRT_CPND_SUBPART_VER_MIN 1
#define CPD_WRT_CPND_SUBPART_VER_MAX 3

#define CPD_WRT_CPND_SUBPART_VER_RANGE \
  (CPD_WRT_CPND_SUBPART_VER_MAX - CPD_WRT_CPND_SUBPART_VER_MIN + 1)

/*CPD - CPA communication */
#define CPD_WRT_CPA_SUBPART_VER_MIN 1
#define CPD_WRT_CPA_SUBPART_VER_MAX 2

#define CPD_WRT_CPA_SUBPART_VER_RANGE \
  (CPD_WRT_CPA_SUBPART_VER_MAX - CPD_WRT_CPA_SUBPART_VER_MIN + 1)

#define CPSV_CPD_MBCSV_VERSION_MIN 1
#define CPSV_CPD_MBCSV_VERSION_USR_INFO 2

#include <saImmOi.h>

typedef enum rep_type {
  REP_ACTIVE = 1,
  REP_NOTACTIVE,
  REP_NONCOLL,
  REP_SYNCUPD
} REP_TYPE;

typedef struct cpsv_node_ref_info {
  MDS_DEST dest;
  bool exp_opened;
  struct cpsv_node_ref_info *prev;
  struct cpsv_node_ref_info *next;
} CPD_NODE_REF_INFO;

typedef struct cpd_ckpt_info_node {
  NCS_PATRICIA_NODE patnode;
  SaCkptCheckpointHandleT ckpt_id;
  SaConstStringT ckpt_name;
  uint32_t dest_cnt;
  CPD_NODE_REF_INFO *node_list;
  bool is_unlink_set;
  SaCkptCheckpointCreationAttributesT attributes;
  SaCkptCheckpointOpenFlagsT ckpt_flags;
  bool is_active_exists;
  uint32_t ckpt_on_scxb1;
  uint32_t ckpt_on_scxb2;
  MDS_DEST active_dest;
  SaTimeT ret_time;
  uint32_t num_users;
  uint32_t num_readers;
  uint32_t num_writers;
  uint32_t node_users_cnt;
  CPD_NODE_USER_INFO *node_users;

  /* for imm */
  SaUint32T ckpt_used_size;
  SaTimeT create_time;
  uint32_t num_sections;
  uint32_t num_corrupt_sections;
  uint8_t *sec_id;
  uint32_t sec_state;
  uint64_t sec_size;
  SaTimeT sec_last_update;
  SaTimeT sec_exp_time;
} CPD_CKPT_INFO_NODE;

#define CPD_CKPT_INFO_NODE_NULL ((CPD_CKPT_INFO_NODE *)0)

typedef struct cpd_ckpt_ref_info {
  CPD_CKPT_INFO_NODE *ckpt_node;
  struct cpd_ckpt_ref_info *prev;
  struct cpd_ckpt_ref_info *next;
} CPD_CKPT_REF_INFO;

typedef struct cpd_cpnd_info_node {
  NCS_PATRICIA_NODE patnode;
  MDS_DEST cpnd_dest;
  uint32_t ckpt_cnt;
  CPD_TMR cpnd_ret_timer;
  CPD_CKPT_REF_INFO *ckpt_ref_list;
  NODE_ID cpnd_key;
  uint32_t timer_state;
  bool ckpt_cpnd_scxb_exist;
  /* for imm */
  SaConstStringT node_name;
  uint32_t rep_type;
} CPD_CPND_INFO_NODE;

#define CPD_CPND_INFO_NODE_NULL ((CPD_CPND_INFO_NODE *)0)

typedef struct cpd_ckpt_map_info {
  NCS_PATRICIA_NODE patnode;
  SaConstStringT ckpt_name;
  SaCkptCheckpointHandleT ckpt_id;
  SaCkptCheckpointCreationAttributesT attributes;
  SaVersionT client_version;
} CPD_CKPT_MAP_INFO;

typedef struct cpd_rep_key_info {
  SaConstStringT ckpt_name;
  SaConstStringT node_name;
} CPD_REP_KEY_INFO;

typedef struct cpd_ckpt_reploc_info {
  NCS_PATRICIA_NODE patnode;
  CPD_REP_KEY_INFO rep_key;
  uint32_t rep_type;
} CPD_CKPT_REPLOC_INFO;

#define CPD_CKPT_REPLOC_INFO_NULL ((CPD_CKPT_REPLOC_INFO *)0)

typedef struct cpd_cb_tag {
  SYSF_MBX cpd_mbx;
  SaNameT comp_name;
  uint32_t mds_handle;
  uint8_t hm_poolid;
  NCSCONTEXT task_hdl;
  uint32_t cpd_hdl;
  V_DEST_QA cpd_anc;
  MDS_DEST cpd_dest_id;
  NCS_MBCSV_HDL mbcsv_handle;
  SaSelectionObjectT mbcsv_sel_obj;
  NCS_MBCSV_CKPT_HDL o_ckpt_hdl;
  SaCkptCheckpointHandleT prev_ckpt_id;
  uint32_t cpd_sync_cnt;
  uint32_t sync_upd_cnt;

  NCS_NODE_ID cpd_self_id;
  NCS_NODE_ID cpd_remote_id;

  bool is_db_upd;

  NCS_NODE_ID node_id;

  bool is_loc_cpnd_up;
  bool is_rem_cpnd_up;
  bool is_quiesced_set;
  MDS_DEST loc_cpnd_dest;
  MDS_DEST rem_cpnd_dest;

  NCS_PATRICIA_TREE cpnd_tree;

  bool is_cpnd_tree_up;            /* if true cpnd_tree is UP */
  NCS_PATRICIA_TREE ckpt_tree;     /* Checkpoint info indexed by Ckpt Handle */
  bool is_ckpt_tree_up;            /* if true cpnd_tree is UP */
  NCS_PATRICIA_TREE ckpt_map_tree; /* Maps from Ckpt name --> Ckpt ID      */
  bool is_ckpt_map_up;             /* if true CKPT MAP tree is UP */

  NCS_PATRICIA_TREE ckpt_reploc_tree;
  bool is_ckpt_reploc_up;

  SaAmfHandleT amf_hdl; /* AMF handle, obtained thru AMF init   */
  SaClmHandleT clm_hdl;

  SaAmfHAStateT ha_state; /* present AMF HA state of the component */
  EDU_HDL edu_hdl;        /* EDU Handle for encode decodes        */
  uint64_t nxt_ckpt_id;   /* Next ckpt id to be generated         */

  SaInvocationT amf_invocation;

  bool cold_or_warm_sync_on;

  SaImmOiHandleT immOiHandle;     /* IMM OI Handle */
  SaSelectionObjectT imm_sel_obj; /*Selection object to wait for IMM events */
  SaSelectionObjectT clm_sel_obj;

  CPD_TMR ckpt_update_timer;
  bool is_ckpt_updating;
  bool scAbsenceAllowed;
  bool fully_initialized;

} CPD_CB;

#define CPD_CB_NULL ((CPD_CB *)0)

/* Function Declarations */
CPD_CKPT_INFO_NODE *cpd_find_add_ckpt_name(CPD_CB *cpd_cb,
                                           SaConstStringT ckpt_name);

void cpd_free_ckpt_node(CPD_CB *gld_cb, CPD_CKPT_INFO_NODE *ckpt_info);

/* CPD Function declerations */
uint32_t cpd_ckpt_tree_init(CPD_CB *cb);
uint32_t cpd_ckpt_node_get(NCS_PATRICIA_TREE *ckpt_tree,
                           SaCkptCheckpointHandleT *ckpt_hdl,
                           CPD_CKPT_INFO_NODE **ckpt_node);
void cpd_ckpt_node_getnext(NCS_PATRICIA_TREE *ckpt_tree,
                           SaCkptCheckpointHandleT *ckpt_hdl,
                           CPD_CKPT_INFO_NODE **ckpt_node);

uint32_t cpd_ckpt_node_add(NCS_PATRICIA_TREE *ckpt_tree,
                           CPD_CKPT_INFO_NODE *ckpt_node,
                           SaAmfHAStateT ha_state, SaImmOiHandleT immOiHandle);
uint32_t cpd_ckpt_node_delete(CPD_CB *cb, CPD_CKPT_INFO_NODE *ckpt_node);
void cpd_ckpt_tree_cleanup(CPD_CB *cb);
void cpd_ckpt_tree_destroy(CPD_CB *cb);
void cpd_ckpt_tree_node_destroy(CPD_CB *cb);

uint32_t cpd_ckpt_reploc_tree_init(CPD_CB *cb);
uint32_t cpd_ckpt_reploc_get(NCS_PATRICIA_TREE *ckpt_reploc_tree,
                             CPD_REP_KEY_INFO *key_info,
                             CPD_CKPT_REPLOC_INFO **ckpt_reploc_node);
void cpd_ckpt_reploc_getnext(NCS_PATRICIA_TREE *ckpt_reploc_tree,
                             CPD_REP_KEY_INFO *key_info,
                             CPD_CKPT_REPLOC_INFO **ckpt_reploc_node);
uint32_t cpd_ckpt_reploc_node_add(NCS_PATRICIA_TREE *ckpt_reploc_tree,
                                  CPD_CKPT_REPLOC_INFO *ckpt_reploc_node,
                                  SaAmfHAStateT ha_state,
                                  SaImmOiHandleT immOiHandle);
uint32_t cpd_ckpt_reploc_node_delete(CPD_CB *cb,
                                     CPD_CKPT_REPLOC_INFO *ckpt_reploc_node,
                                     bool is_unlink_set);
void cpd_ckpt_reploc_cleanup(CPD_CB *cb);
void cpd_ckpt_reploc_tree_destroy(CPD_CB *cb);

uint32_t cpd_ckpt_map_tree_init(CPD_CB *cb);
uint32_t cpd_ckpt_map_node_get(NCS_PATRICIA_TREE *ckpt_map_tree,
                               SaConstStringT ckpt_name,
                               CPD_CKPT_MAP_INFO **ckpt_map_node);
void cpd_ckpt_map_node_getnext(NCS_PATRICIA_TREE *ckpt_map_tree,
                               SaConstStringT *ckpt_name,
                               CPD_CKPT_MAP_INFO **ckpt_map_node);

uint32_t cpd_ckpt_map_node_add(NCS_PATRICIA_TREE *ckpt_map_tree,
                               CPD_CKPT_MAP_INFO *ckpt_map_node);
uint32_t cpd_ckpt_map_node_delete(CPD_CB *cb, CPD_CKPT_MAP_INFO *ckpt_map_node);
void cpd_ckpt_map_tree_cleanup(CPD_CB *cb);
void cpd_ckpt_map_tree_destroy(CPD_CB *cb);
uint32_t cpd_cpnd_info_tree_init(CPD_CB *cb);
uint32_t cpd_cpnd_info_node_get(NCS_PATRICIA_TREE *cpnd_tree, MDS_DEST *dest,
                                CPD_CPND_INFO_NODE **cpnd_info_node);
void cpd_cpnd_info_node_getnext(NCS_PATRICIA_TREE *cpnd_tree, MDS_DEST *dest,
                                CPD_CPND_INFO_NODE **cpnd_info_node);
void cpd_ckpt_node_and_ref_delete(CPD_CB *cb, CPD_CKPT_INFO_NODE *ckpt_node);

uint32_t cpd_cpnd_info_node_add(NCS_PATRICIA_TREE *cpnd_tree,
                                CPD_CPND_INFO_NODE *cpnd_info_node);
uint32_t cpd_cpnd_info_node_delete(CPD_CB *cb,
                                   CPD_CPND_INFO_NODE *cpnd_info_node);
void cpd_cpnd_info_tree_cleanup(CPD_CB *cb);
void cpd_cpnd_info_tree_destroy(CPD_CB *cb);
uint32_t cpd_cpnd_info_node_find_add(NCS_PATRICIA_TREE *cpnd_tree,
                                     MDS_DEST *dest,
                                     CPD_CPND_INFO_NODE **cpnd_info_node,
                                     bool *add_flag);

uint32_t cpd_cb_db_init(CPD_CB *cb);

uint32_t cpd_cb_db_destroy(CPD_CB *cb);

void cpd_ckpt_ref_info_add(CPD_CPND_INFO_NODE *node_info,
                           CPD_CKPT_INFO_NODE *ckpt_node);
void cpd_ckpt_ref_info_del(CPD_CPND_INFO_NODE *node_info,
                           CPD_CKPT_REF_INFO *cref_info);
void cpd_node_ref_info_add(CPD_CKPT_INFO_NODE *ckpt_node, MDS_DEST *mds_dest);
void cpd_node_ref_info_del(CPD_CKPT_INFO_NODE *ckpt_node,
                           CPD_NODE_REF_INFO *nref_info);

void cpd_clm_cluster_track_cb(
    const SaClmClusterNotificationBufferT *notificationBuffer,
    SaUint32T numberOfMembers, SaAisErrorT error);

void cpd_ckpt_tree_node_destroy(CPD_CB *cb);

#endif  // CKPT_CKPTD_CPD_CB_H_
