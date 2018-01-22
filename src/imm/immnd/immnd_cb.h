/*      -*- OpenSAF  -*-
 *
 * (C) Copyright 2008 The OpenSAF Foundation
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
 *
 */

#ifndef IMM_IMMND_IMMND_CB_H_
#define IMM_IMMND_IMMND_CB_H_

#include <saClm.h>

typedef enum immnd_server_state {
  IMM_SERVER_UNKNOWN = 0,
  IMM_SERVER_ANONYMOUS = 1,
  IMM_SERVER_CLUSTER_WAITING = 2,
  IMM_SERVER_LOADING_PENDING = 3,
  IMM_SERVER_LOADING_SERVER = 4,
  IMM_SERVER_LOADING_CLIENT = 5,
  IMM_SERVER_SYNC_PENDING = 6,
  IMM_SERVER_SYNC_CLIENT = 7,
  IMM_SERVER_SYNC_SERVER = 8,
  IMM_SERVER_DUMP = 9,  // Possibly needed to prevent conflicting ops?
  IMM_SERVER_READY = 10
} IMMND_SERVER_STATE;

/*****************************************************************************
 Client  information
*****************************************************************************/
typedef struct immnd_om_search_node {
  SaUint32T searchId;
  void *searchOp;
  struct immnd_om_search_node *next;
} IMMND_OM_SEARCH_NODE;

typedef struct immnd_clm_node_list {
  NCS_PATRICIA_NODE patnode;
  NCS_NODE_ID node_id;
} IMMND_CLM_NODE_LIST;

typedef struct immnd_immom_client_node {
  NCS_PATRICIA_NODE patnode;
  SaImmHandleT imm_app_hdl; /* index for the client tree */
  MDS_DEST agent_mds_dest;  /* mds dest of the agent */
  SaVersionT version;
  IMMSV_SEND_INFO tmpSinfo; /*needed for replying to
                               syncronousrequests */

  NCSMDS_SVC_ID sv_id; /* OM or OI */
  IMMND_OM_SEARCH_NODE *searchOpList;
  uint8_t mIsSync;             /* Client is special sync client */
  uint8_t mIsPbe;              /* Client is persistent back end */
  uint8_t mSyncBlocked;        /* Sync client expects reply */
  uint8_t mIsStale;            /* Client disconnected when IMMD
                          is unavailable => postpone
                          discardClient. */
  uint8_t mIsResurrect;        /* The client is a temprary place holder
                              for handle resurrect. We use it to send
                              the PROC_STALE_CLIENT internal upcall to
                              the IMMA clients that are dispatching.
                              This is done when immnd sync has completed.
                              The tmp client is then removed, anticipating
                              a resurrect request by the IMMA.
                           */
  struct timespec mLastSearch; /* Time of the latest used search handle
                                                                  It is used to
                                  reduce number of iterations of inactive search
                                  handles.
                                                                */
} IMMND_IMM_CLIENT_NODE;

/******************************************************************************
 Used to maintain the linked list of fevs messages received out of order
*****************************************************************************/

typedef struct immnd_fevs_msg_node {
  SaUint64T msgNo;
  SaImmHandleT clnt_hdl;
  MDS_DEST reply_dest;
  IMMSV_OCTET_STRING msg;
  struct immnd_fevs_msg_node *next;
} IMMND_FEVS_MSG_NODE;

/*****************************************************************************
 * Data Structure used to hold IMMND control block
 *****************************************************************************/
typedef struct immnd_cb_tag {
  SYSF_MBX immnd_mbx; /* mailbox */
  SaNameT comp_name;
  uint32_t immnd_mds_hdl;
  MDS_DEST immnd_mdest_id;
  NCS_NODE_ID node_id;
  NCS_NODE_ID other_sc_node_id;  // Not reliably set, see mIsOtherScUp (2pbe).

  /*Nr of FEVS messages sent, but not received back at origin.*/
  uint8_t fevs_replies_pending;

  SaUint32T cli_id_gen; /* for generating client_id */

  SaUint64T highestProcessed;          // highest fevs msg processed so far.
  SaUint64T highestReceived;           // highest fevs msg received so far
  SaUint64T syncFevsBase;              // Last fevsMessage before sync iterator.
  IMMND_FEVS_MSG_NODE *fevs_in_list;   // incomming queue
  IMMND_FEVS_MSG_NODE *fevs_out_list;  // outgoing queue
  IMMND_FEVS_MSG_NODE *fevs_out_list_end;  // end outgoing queue
  SaUint32T fevs_out_count;                // Nrof elements in outgoing queue

  void *immModel;
  SaUint32T mMyEpoch;  // Epoch counter, used in synch of immnds
  SaUint32T mMyPid;    // Is this needed ??
  SaUint32T mRulingEpoch;
  SaUint32T mLatestAdmoId;
  SaUint32T mLatestImplId;
  SaUint32T mLatestCcbId;

  uint8_t
      mAccepted;  // If=!0 Fevs messages can be processed. 2=>IMMD re-introduce.
  uint8_t mIntroduced;      // Ack received on introduce message
  uint8_t mSyncRequested;   // true=> I am coord, other req sync
  uint8_t mPendSync;        // 1=>sync announced but not received.
  uint8_t mSyncFinalizing;  // 1=>finalizeSync sent but not received.
  uint8_t mSync;            // true => this node is being synced (client).
  uint8_t mCanBeCoord;      // If!=0 then SC, 2 => 2pbe arbitration, 4 =>
                        // absentScAllowed.
  uint8_t mIsCoord;
  uint8_t mLostNodes;          // Detached & not syncreq => delay sync start
  uint8_t mBlockPbeEnable;     // Current PBE has not completed shutdown yet.
  uint8_t mPbeKills;           // If != 0 then immnd has tried to kill Pbe.
  uint8_t m2Pbe;               // If!=0 => 2PBE, 2 => fetch PBE file info.
  SaUint32T mPbeDisableCcbId;  // CcbId, operation of the Disable PBE.
  bool mPbeDisableCritical;  // If true then PBE disable is sent to PBE for ACK.

  bool mIsOtherScUp;  // If set & this is an SC then other SC is up(2pbe).
                      // False=> *allow* 1safe 2pbe. May err conservatively
                      // (true)
  bool mForceClean;            // true => Force cleanTheHouse to run once *now*.
  SaUint32T mScAbsenceAllowed; /* Non zero if SC absence is allowed (loss of
                                 both IMMDs/SCs). Value is number of seconds of
                                 SC absence tolerated. */

  /* Information about the IMMD */
  MDS_DEST immd_mdest_id;
  bool is_immd_up;

  /* IMMND data */
  NCS_PATRICIA_TREE client_info_db; /* IMMND_IMM_CLIENT_NODE - node */

  SaClmHandleT clm_hdl;
  SaClmNodeIdT clm_node_id;
  SaAmfHandleT amf_hdl;  // AMF handle, obtained thru AMF init

  int32_t preLoadPid;  // Forked preloader probes PBE file, for 2Pbe.
  int32_t loaderPid;
  int32_t syncPid;
  int32_t pbePid;   // Persistent back end (PBE) is running if pbePid > 0
  int32_t pbePid2;  // Redundant PBE (2PBE) is running if pbePid2 > 0
  IMMND_SERVER_STATE mState;
  uint32_t mStep;  // Measures progress in immnd_proc_server
  struct timespec
      mJobStart;    // Start time for major server tasks like start, load, sync.
  char *mProgName;  // The full path name of the immnd executable.
  const char *mDir;      // The directory where imm.xml & pbe files reside
  const char *mFile;     // The imm.xml file to start from
  const char *mPbeFile;  // Pbe feature is configured (IMMSV_PBE_FILE).
  SaImmRepositoryInitModeT mRim;
  struct timespec
      mCleanedHouseAt;  // When cleanTheHouse was executed (immnd_proc.c)

  uint8_t mExpectedNodes;
  uint8_t mWaitSecs;
  uint8_t mNumNodes;
  uint8_t mPbeVeteran;      // false => regenerate. true => re-attach db-file
  uint8_t mPbeVeteranB;     // false => regenerate. true => re-attach db-file
  uint8_t mPbeOldVeteranB;  // false => restarted,  true => stable. (only to
                            // reduce logging).

  SaAmfHAStateT ha_state;  // present AMF HA state of the component
  EDU_HDL immnd_edu_hdl;   // edu handle, obscurely needed by mds.
  NCSCONTEXT task_hdl;

  NCS_LOCK immnd_immd_up_lock;

  NCS_SEL_OBJ usr1_sel_obj;       /* Selection object for USR1 signal events */
  SaSelectionObjectT amf_sel_obj; /* Selection Object for AMF events */
  int nid_started;                /* true if started by NID */
  bool isNodeTypeController;      // true node type is controller
  SaSelectionObjectT
      clmSelectionObject; /* Selection object to wait for clms events*/
  NCS_SEL_OBJ
      clm_init_sel_obj; /* Selection object wait for  clms intialization*/
  bool isClmNodeJoined; /* True => If clm joined the cluster*/
  NCS_PATRICIA_TREE immnd_clm_list; /* IMMND_IMM_CLIENT_NODE - node */
} IMMND_CB;

/* CB prototypes */
IMMND_CB *immnd_cb_create(uint32_t pool_id);
bool immnd_cleanup_mbx(NCSCONTEXT arg, NCSCONTEXT msg);
uint32_t immnd_cb_destroy(IMMND_CB *immnd_cb);
void immnd_dump_cb(IMMND_CB *immnd_cb);

uint32_t immnd_client_extract_bits(uint32_t bitmap_value,
                                   uint32_t *bit_position);

void immnd_client_node_get(IMMND_CB *cb, SaImmHandleT imm_client_hdl,
                           IMMND_IMM_CLIENT_NODE **imm_client_node);
void immnd_client_node_getnext(IMMND_CB *cb, SaImmHandleT imm_client_hdl,
                               IMMND_IMM_CLIENT_NODE **imm_client_node);
uint32_t immnd_client_node_add(IMMND_CB *cb, IMMND_IMM_CLIENT_NODE *cl_node);
uint32_t immnd_client_node_del(IMMND_CB *cb, IMMND_IMM_CLIENT_NODE *cl_node);
uint32_t immnd_client_node_tree_init(IMMND_CB *cb);
void immnd_client_node_tree_cleanup(IMMND_CB *cb);
void immnd_client_node_tree_destroy(IMMND_CB *cb);

uint32_t immnd_clm_node_list_init(IMMND_CB *cb);
void immnd_clm_node_get(IMMND_CB *cb, NODE_ID node,
                        IMMND_CLM_NODE_LIST **imm_clm_node);
uint32_t immnd_clm_node_add(IMMND_CB *cb, NODE_ID key);
uint32_t immnd_clm_node_delete(IMMND_CB *cb,
                               IMMND_CLM_NODE_LIST *immnd_clm_node);
void immnd_clm_node_cleanup(IMMND_CB *cb);
void immnd_clm_node_destroy(IMMND_CB *cb);

/*
  #define m_IMMSV_CONVERT_EXPTIME_TEN_MILLI_SEC(t) \
  SaTimeT now; \
  t = (( (t) - (m_GET_TIME_STAMP(now)*(1000000000)))/(10000000));
*/

/*30B Versioning Changes */
#define IMMND_MDS_PVT_SUBPART_VERSION 1

/*IMMND - IMMA communication */
#define IMMND_WRT_IMMA_SUBPART_VER_MIN 1
#define IMMND_WRT_IMMA_SUBPART_VER_MAX 1

#define IMMND_WRT_IMMA_SUBPART_VER_RANGE \
  (IMMND_WRT_IMMA_SUBPART_VER_MAX - IMMND_WRT_IMMA_SUBPART_VER_MIN + 1)

/*IMMND - IMMND communication */
#define IMMND_WRT_IMMND_SUBPART_VER_MIN 1
#define IMMND_WRT_IMMND_SUBPART_VER_MAX 1

#define IMMND_WRT_IMMND_SUBPART_VER_RANGE \
  (IMMND_WRT_IMMND_SUBPART_VER_MAX - IMMND_WRT_IMMND_SUBPART_VER_MIN + 1)

/*IMMND - IMMD communication */
#define IMMND_WRT_IMMD_SUBPART_VER_MIN 1
#define IMMND_WRT_IMMD_SUBPART_VER_MAX 1

#define IMMND_WRT_IMMD_SUBPART_VER_RANGE \
  (IMMND_WRT_IMMD_SUBPART_VER_MAX - IMMND_WRT_IMMD_SUBPART_VER_MIN + 1)

/* IMMND - Prefix in IMM error strings for SA_AIS_ERR_NO_RESOURCE */
#define IMM_VALIDATION_ABORT "IMM: Validation abort: "
#define IMM_RESOURCE_ABORT "IMM: Resource abort: "

#endif  // IMM_IMMND_IMMND_CB_H_
