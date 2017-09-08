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

#ifndef IMM_AGENT_IMMA_CB_H_
#define IMM_AGENT_IMMA_CB_H_

#include <set>

/* Node to store Ccb info for OI client */
struct imma_callback_info;

struct imma_oi_ccb_record {
  struct imma_oi_ccb_record *next;
  SaImmOiCcbIdT ccbId; /* High order 32 bits used for PRTO 'pseudo ccbs'.*/
  SaUint32T opCount;   /* Used to ensure PBE has not missed any unacked ops. */
  SaStringT mCcbErrorString; /* See saImmOiCcbSetErrorString */
  bool isStale;              /* 1 => ccb was terminated by IMMND down. */
  bool isCritical; /* 1 => OI has replied OK on completed callback but not */
                   /*      received abort-callback or apply-callback.      */
  bool
      isCcbErrOk;  /* 1 => Ok to set error string create/delete/modify/completed
                    */
  bool isCcbAugOk; /* 1 => Ok to augment CCB in create/delete/modify */
  bool isAborted;  /* 1 => abort upcall received by mds thread */
  /* Members below only used for OI-Augmented CCB #1963. */
  struct imma_callback_info *ccbCallback;
  SaImmHandleT privateAugOmHandle;
  SaImmAdminOwnerHandleT privateAoHandle;
  SaStringT object;     /* The  Object string from the modify/delete ccb operation.
                       The object is used to obtain     adminOwner when
                       saImmOiAugmentCcbInitialize is called in completed-callback*/
  SaStringT adminOwner; /* adminowner of the ccb id, assigned at
                  ccb-create-callback. The adminOwner used in
                  saImmOiAugmentCcbInitialize is called in completed-callback*/
};

typedef struct imma_client_node {
  NCS_PATRICIA_NODE patnode; /* index for the tree */
  SaImmHandleT handle;
  union {
    SaImmCallbacksT mCallbk;
    SaImmCallbacksT_o2 mCallbkA2b;
    SaImmOiCallbacksT_2 iCallbk;
    SaImmOiCallbacksT_o3 iCallbkA2f;
  } o;
  SaUint32T mImplementerId;                 /*Only used for OI.*/
  SaImmOiImplementerNameT mImplementerName; /* needed for active resurrect*/
  SaTimeT syncr_timeout;                    /* Timeout on syncr downcalls, dflt 10s, or setenv
                                               IMMA_SYNCR_TIMEOUT */
  unsigned char
      replyPending;  /* Syncronous or asyncronous call made towards IMMND */
  bool isOm;         /*If true => then this is an OM client */
  bool stale;        /*Loss of connection with immnd
                               will set this to true for the
                               connection. A resurrect can remove it.*/
  bool exposed;      /* Exposed => stale is irreversible */
  bool clmExposed;   /* True ==> then handle is unavailable, due to clm leaving
                        the cluster*/
  bool selObjUsable; /* Active resurrect possible for this client */
  bool isPbe;        /* True => This is the PBE-OI */
  bool isImmA2b;     /* Version A.02.11 */
  bool isImmA2bCbk;  /* Version A.02.11 callback*/
  bool isImmA2d;     /* Version A.02.13 */
  bool isImmA2e;     /* Version A.02.14 */
  bool isImmA2f;     /* Version A.02.15 */
  bool isImmA2fCbk;  /* Version A.02.15 callback*/
  bool isImmA2x10;   /* Version A.02.16 */
  bool isImmA2x11;   /* Version A.02.17 */
  bool isImmA2x12;   /* Version A.02.18 */
  bool isApplier;    /* True => This is an Applier-OI */
  bool isAug;        /* True => handle internal to OI augmented CCB */
  bool isBusy;       /* True => handle is locked by a thread until a function
                        execution is done */
  struct imma_oi_ccb_record
      *activeOiCcbs;   /* For ccb termination on IMMND down.*/
  SYSF_MBX callbk_mbx; /*Mailbox Queue for clnt messages */

  /* Maximum number of open search handles pre IMM handle, managed by
   * enviroment variable IMMA_MAX_OPEN_SEARCHES_PER_HANDLE */
  uint32_t maxSearchHandles;
  uint32_t searchHandleSize; /* Number of open search handles */
  SaTimeT
      oiTimeout; /* Timeout for OI callback. If the value is 0, the default
                    timeout (6s) will be used */

  /* Current callback invocations */
  std::set<SaInvocationT> callbackInvocationSet;
} IMMA_CLIENT_NODE;

/* Node to store adminOwner info */
typedef struct imma_admin_owner_node {
  NCS_PATRICIA_NODE patnode;              /* index for the tree */
  SaImmAdminOwnerHandleT admin_owner_hdl; /* locally generated handle */
  SaImmHandleT mImmHandle;                /* The immOm handle */
  SaUint32T mAdminOwnerId;
  SaImmAdminOwnerNameT mAdminOwnerName; /* Needed for OM resurrect. */
  bool mReleaseOnFinalize; /* Release on finalize set, stale irreversible*/
  bool mAugCcb;            /* This admo is purely for a ccb augmentation. */
} IMMA_ADMIN_OWNER_NODE;

/* Node to store Ccb info for OM client */
typedef struct imma_ccb_node {
  NCS_PATRICIA_NODE patnode; /* index for the tree */
  SaImmCcbHandleT ccb_hdl;   /* locally generated handle */
  SaImmHandleT mImmHandle;   /* The immOm handle */
  SaImmAdminOwnerHandleT mAdminOwnerHdl;
  SaImmAccessorHandleT mCcbObjectReadAccessorHandle;
  SaImmCcbFlagsT mCcbFlags;
  SaUint32T mCcbId; /* Om client uses 32 bit ccbId. */
  SaStringT *mErrorStrings;
  bool mExclusive; /* 1 => Ccb-id being created, applied or finalized */
  bool
      mApplying;   /* Critical (apply invoked), IMMND contact lost =>
                                          timeout => Ccb-outcome to be recovered.
                    */
  bool mApplied;   /* Current mCcbId applied&terminated */
  bool mAborted;   /* Current mCcbId aborted */
  bool mValidated; /* Current mCcbId validated */
  bool mAugCcb;    /* Current and only mCcbId is an augment. */
  bool
      mAugIsTainted; /* AugCcb has tainted root CCB => apply aug or abort root*/
} IMMA_CCB_NODE;

/* Node to store Search info */
typedef struct imma_search_node {
  NCS_PATRICIA_NODE patnode;     /* index for the tree */
  SaImmSearchHandleT search_hdl; /* locally generated handle */
  SaImmHandleT mImmHandle;       /* The immOm handle */
  SaUint32T mSearchId;
  void *mLastAttributes; /* From previous searchNext */
  void *mLastObjectName; /* From previous searchNext */
  SaUint32T searchIndex;
  IMMSV_OM_RSP_SEARCH_BUNDLE_NEXT *searchBundle;
} IMMA_SEARCH_NODE;

typedef struct imma_continuation_record {
  SaInt32T invocation;
  SaInvocationT userInvoc;
  SaImmHandleT immHandle;
  struct imma_continuation_record *next;
} IMMA_CONTINUATION_RECORD;

/*****************************************************************************
 * Data Structure Used to hold IMMA control block
 *****************************************************************************/
typedef struct imma_cb {
  /* Identification Information about the IMMA */
  uint32_t imma_mds_hdl;
  MDS_DEST imma_mds_adest; /* adest of this MDS core */
  NCSMDS_SVC_ID sv_id;
  NCS_LOCK cb_lock;
  uint32_t pend_dis; /* Number of pending dispaches */
  uint32_t pend_fin; /* Number of pending agent destroy */
  EDU_HDL edu_hdl;   /* edu handle obscurely needed by mds */

  /* Information about IMMND */
  MDS_DEST immnd_mds_dest;
  bool is_immnd_up;
  uint16_t dispatch_clients_to_resurrect; /* Nrof clients pending
                                          active resurrect.  */

  /* IMMA data */                /* Used for both OM and OI */
  NCS_PATRICIA_TREE client_tree; /* IMMA_CLIENT_NODE - node */

  /* These trees could theoretically be moved into the client node
     But the assumption is that the typical process has few connections
     and few subhandles. By "few" we mean < 10.
   */

  NCS_PATRICIA_TREE admin_owner_tree; /* IMMA_ADMIN_OWNER_NODE  - node */

  NCS_PATRICIA_TREE ccb_tree; /* IMMA_CCB_NODE  - node */

  NCS_PATRICIA_TREE search_tree; /* IMMA_SEARCH_NODE  - node */

  /*Used for matching async reply to saImmOmAdminOperationInvokeAsync */
  IMMA_CONTINUATION_RECORD *imma_continuations;

  /* Sync up with IMMND ( MDS ) see imma_sync_with_immnd() in imma_init.c */
  NCS_LOCK immnd_sync_lock;
  bool immnd_sync_awaited;
  NCS_SEL_OBJ immnd_sync_sel;
  bool clmMemberNode; /* True if the node is CLM Member node */
} IMMA_CB;

#define m_IMMSV_SET_SANAMET(name)                      \
  {                                                    \
    if (name->length <= SA_MAX_NAME_LENGTH) {          \
      memset((uint8_t *)&name->value[name->length], 0, \
             (SA_MAX_NAME_LENGTH - name->length));     \
    }                                                  \
  }

/* IMMA Function Declerations */
/* function prototypes for client handling*/

uint32_t imma_db_init(IMMA_CB *cb);
uint32_t imma_db_destroy(IMMA_CB *cb);

/*client tree*/
uint32_t imma_client_tree_init(IMMA_CB *cb);
void imma_client_node_get(NCS_PATRICIA_TREE *client_tree, SaImmHandleT *cl_hdl,
                          IMMA_CLIENT_NODE **cl_node);
uint32_t imma_client_node_add(NCS_PATRICIA_TREE *client_tree,
                              IMMA_CLIENT_NODE *cl_node);
uint32_t imma_client_node_delete(IMMA_CB *cb, IMMA_CLIENT_NODE *cl_node);
void imma_client_tree_destroy(IMMA_CB *cb);
void imma_client_tree_cleanup(IMMA_CB *cb);
void imma_mark_clients_stale(IMMA_CB *cb, bool mark_exposed);
int isExposed(IMMA_CB *cb, IMMA_CLIENT_NODE *clnode);
void imma_oi_ccb_record_add(IMMA_CLIENT_NODE *cl_node, SaImmOiCcbIdT ccbId,
                            SaUint32T inv);
int imma_oi_ccb_record_ok_for_critical(IMMA_CLIENT_NODE *cl_node,
                                       SaImmOiCcbIdT ccbId, SaUint32T inv);
int imma_oi_ccb_record_set_critical(IMMA_CLIENT_NODE *cl_node,
                                    SaImmOiCcbIdT ccbId, SaUint32T inv);
int imma_oi_ccb_record_terminate(IMMA_CLIENT_NODE *cl_node,
                                 SaImmOiCcbIdT ccbId);
int imma_oi_ccb_record_abort(IMMA_CLIENT_NODE *cl_node, SaImmOiCcbIdT ccbId);
int imma_oi_ccb_record_exists(IMMA_CLIENT_NODE *cl_node, SaImmOiCcbIdT ccbId);
struct imma_oi_ccb_record *imma_oi_ccb_record_find(IMMA_CLIENT_NODE *cl_node,
                                                   SaImmOiCcbIdT ccbId);
int imma_oi_ccb_record_set_error(IMMA_CLIENT_NODE *cl_node, SaImmOiCcbIdT ccbId,
                                 const SaStringT errorString);
SaStringT imma_oi_ccb_record_get_error(IMMA_CLIENT_NODE *cl_node,
                                       SaImmOiCcbIdT ccbId);
void imma_oi_ccb_allow_error_string(IMMA_CLIENT_NODE *cl_node,
                                    SaImmOiCcbIdT ccbId);
int imma_oi_ccb_record_note_callback(IMMA_CLIENT_NODE *cl_node,
                                     SaImmOiCcbIdT ccbId,
                                     struct imma_callback_info *callback);
struct imma_callback_info *imma_oi_ccb_record_ok_augment(
    IMMA_CLIENT_NODE *cl_node, SaImmOiCcbIdT ccbId,
    SaImmHandleT *privateOmHandle, SaImmAdminOwnerHandleT *privateAoHandle);
void imma_oi_ccb_record_augment(IMMA_CLIENT_NODE *cl_node, SaImmOiCcbIdT ccbId,
                                SaImmHandleT privateOmHandle,
                                SaImmAdminOwnerHandleT privateAoHandle);
int imma_oi_ccb_record_close_augment(IMMA_CLIENT_NODE *cl_node,
                                     SaImmOiCcbIdT ccbId,
                                     SaImmHandleT *privateOmHandle,
                                     bool ccbDone);

/*admin_owner tree*/
uint32_t imma_admin_owner_tree_init(IMMA_CB *cb);
void imma_admin_owner_node_get(NCS_PATRICIA_TREE *admin_owner_tree,
                               SaImmAdminOwnerHandleT *adm_hdl,
                               IMMA_ADMIN_OWNER_NODE **adm_node);
void imma_admin_owner_node_getnext(IMMA_CB *cb, SaImmAdminOwnerHandleT *adm_hdl,
                                   IMMA_ADMIN_OWNER_NODE **adm_node);
uint32_t imma_admin_owner_node_add(NCS_PATRICIA_TREE *admin_owner_tree,
                                   IMMA_ADMIN_OWNER_NODE *adm_node);
void imma_admin_owner_node_delete(IMMA_CB *cb, IMMA_ADMIN_OWNER_NODE *adm_node);
void imma_admin_owner_tree_destroy(IMMA_CB *cb);
void imma_admin_owner_tree_cleanup(IMMA_CB *cb);

/*ccb tree */
uint32_t imma_ccb_tree_init(IMMA_CB *cb);
void imma_ccb_node_get(NCS_PATRICIA_TREE *ccb_tree, SaImmCcbHandleT *ccb_hdl,
                       IMMA_CCB_NODE **ccb_node);
void imma_ccb_node_getnext(IMMA_CB *cb, SaImmCcbHandleT *ccb_hdl,
                           IMMA_CCB_NODE **ccb_node);
uint32_t imma_ccb_node_add(NCS_PATRICIA_TREE *ccb_tree,
                           IMMA_CCB_NODE *ccb_node);
uint32_t imma_ccb_node_delete(IMMA_CB *cb, IMMA_CCB_NODE *ccb_node);
void imma_ccb_tree_destroy(IMMA_CB *cb);
void imma_ccb_tree_cleanup(IMMA_CB *cb);

/*search tree */
uint32_t imma_search_tree_init(IMMA_CB *cb);
void imma_search_node_get(NCS_PATRICIA_TREE *search_tree,
                          SaImmSearchHandleT *search_hdl,
                          IMMA_SEARCH_NODE **search_node);
void imma_search_node_getnext(IMMA_CB *cb, SaImmSearchHandleT *search_hdl,
                              IMMA_SEARCH_NODE **search_node);
uint32_t imma_search_node_add(NCS_PATRICIA_TREE *search_tree,
                              IMMA_SEARCH_NODE *search_node);
uint32_t imma_search_node_delete(IMMA_CB *cb, IMMA_SEARCH_NODE *search_node);
void imma_search_tree_destroy(IMMA_CB *cb);
void imma_search_tree_cleanup(IMMA_CB *cb);

void imma_process_stale_clients(IMMA_CB *cb);

void imma_free_errorStrings(SaStringT *errorStrings);
SaStringT *imma_getErrorStrings(IMMSV_SAERR_INFO *errRsp);

void imma_client_tree_mark_clmexposed(IMMA_CB *cb);

/*30B Versioning Changes */
#define IMMA_MDS_PVT_SUBPART_VERSION 1
/*IMMA - IMMND communication */
#define IMMA_WRT_IMMND_SUBPART_VER_MIN 1
#define IMMA_WRT_IMMND_SUBPART_VER_MAX 1

#define IMMA_WRT_IMMND_SUBPART_VER_RANGE \
  (IMMA_WRT_IMMND_SUBPART_VER_MAX - IMMA_WRT_IMMND_SUBPART_VER_MIN + 1)

/*IMMND - IMMD communication */
#define IMMA_WRT_IMMD_SUBPART_VER_MIN 1
#define IMMA_WRT_IMMD_SUBPART_VER_MAX 1

#define IMMA_WRT_IMMD_SUBPART_VER_RANGE \
  (IMMA_WRT_IMMD_SUBPART_VER_MAX - IMMA_WRT_IMMD_SUBPART_VER_MIN + 1)

#endif  // IMM_AGENT_IMMA_CB_H_
