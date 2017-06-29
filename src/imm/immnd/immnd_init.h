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

/*****************************************************************************
..............................................................................

 FILE NAME: immnd_init.h

..............................................................................

  DESCRIPTION:
  This file consists of constants, enums and data structs used by immnd_xxx.c
******************************************************************************/

#ifndef IMM_IMMND_IMMND_INIT_H_
#define IMM_IMMND_IMMND_INIT_H_

#include <saClm.h>
#include "imm/common/immsv_evt_model.h"
#include "imm/common/immsv_api.h"

extern IMMND_CB *immnd_cb;

/* internal helping function declaration */

/* file : -  immnd_proc.c */

void immnd_proc_discard_other_nodes(IMMND_CB *cb);

void immnd_proc_imma_down(IMMND_CB *cb, MDS_DEST dest, NCSMDS_SVC_ID sv_id);
uint32_t immnd_proc_imma_discard_connection(IMMND_CB *cb,
                                            IMMND_IMM_CLIENT_NODE *cl_node,
                                            bool scAbsenceAllowed);
void immnd_proc_imma_discard_stales(IMMND_CB *cb);

void immnd_cb_dump(void);
void immnd_proc_immd_down(IMMND_CB *cb);

void immnd_proc_imma_up(IMMND_CB *cb, MDS_DEST dest);
void immnd_proc_app_status(IMMND_CB *cb);
void immnd_adjustEpoch(IMMND_CB *cb, bool increment);
uint32_t immnd_introduceMe(IMMND_CB *cb);
void immnd_announceDump(IMMND_CB *cb);
uint32_t immnd_is_immd_up(IMMND_CB *cb);
IMMSV_ADMIN_OPERATION_PARAM *immnd_getOsafImmPbeAdmopParam(
    SaImmAdminOperationIdT operationId, void *evt,
    IMMSV_ADMIN_OPERATION_PARAM *param);
void search_req_continue(IMMND_CB *cb, IMMSV_OM_RSP_SEARCH_REMOTE *reply,
                         SaUint32T reqConn);
void immnd_ackToNid(uint32_t rc);
bool immnd_syncComplete(IMMND_CB *cb, bool coordinator, SaUint32T step);

void immnd_proc_global_abort_ccb(IMMND_CB *cb, SaUint32T ccbId);
void immnd_abortSync(IMMND_CB *cb);

/* End immnd_proc.c */

/* file : - ImmModel.cc */
#ifdef __cplusplus
extern "C" {
#endif

/* These are functions that bridge from the C world of the OpenSaf framework
   to the C++ world of the IMM server model. */

void immModel_abortSync(IMMND_CB *cb);

void immModel_isolateThisNode(IMMND_CB *cb);

void immModel_abortNonCriticalCcbs(IMMND_CB *cb);

void immModel_pbePrtoPurgeMutations(IMMND_CB *cb, unsigned int nodeId,
                                    SaUint32T *reqArrSize,
                                    SaUint32T **reqConArr);

SaAisErrorT immModel_adminOwnerCreate(
    IMMND_CB *cb, const struct ImmsvOmAdminOwnerInitialize *req,
    SaUint32T ownerId, SaUint32T conn, unsigned int nodeId);
SaAisErrorT immModel_adminOwnerDelete(IMMND_CB *cb, SaUint32T ownerId,
                                      SaUint32T hard);
void immModel_addDeadAdminOwnerDuringSync(SaUint32T ownerId);

SaAisErrorT immModel_ccbCreate(IMMND_CB *cb, SaUint32T adminOwnerId,
                               SaUint32T ccbFlags, SaUint32T ccbId,
                               unsigned int originatingNode,
                               SaUint32T originatingConn);

SaUint32T immModel_getAdmoIdForObj(IMMND_CB *cb, const char *objNameBuf);

SaAisErrorT immModel_adminOperationInvoke(
    IMMND_CB *cb, const struct ImmsvOmAdminOperationInvoke *req,
    SaUint32T reqConn, SaUint64T reply_dest, SaInvocationT inv,
    SaUint32T *implConn, SaClmNodeIdT *implNodeId, bool pbeExpected,
    bool *displayRes);

SaAisErrorT immModel_classCreate(IMMND_CB *cb,
                                 const struct ImmsvOmClassDescr *req,
                                 SaUint32T reqConn, SaUint32T nodeId,
                                 SaUint32T *continuationId, SaUint32T *pbeConn,
                                 unsigned int *pbeNodeId);

SaAisErrorT immModel_classDelete(IMMND_CB *cb,
                                 const struct ImmsvOmClassDescr *req,
                                 SaUint32T reqConn, SaUint32T nodeId,
                                 SaUint32T *continuationId, SaUint32T *pbeConn,
                                 unsigned int *pbeNodeId);

SaAisErrorT immModel_classDescriptionGet(IMMND_CB *cb,
                                         const IMMSV_OCTET_STRING *clName,
                                         struct ImmsvOmClassDescr *res);

SaUint32T immModel_cleanTheBasement(
    IMMND_CB *cb, SaInvocationT **admReqArr, SaUint32T *admReqArrSize,
    SaInvocationT **searchReqArr, SaUint32T *searchReqArrSize,
    SaUint32T **ccbIdArr, SaUint32T *ccbIdArrSize, SaUint32T **pbePrtoReqIdArr,
    SaUint32T *pbePrtoReqArrSize, bool iAmCoordNow);

void immModel_getNonCriticalCcbs(IMMND_CB *cb, SaUint32T **ccbIdArr,
                                 SaUint32T *ccbIdArrSize);

void immModel_getOldCriticalCcbs(IMMND_CB *cb, SaUint32T **ccbIdArr,
                                 SaUint32T *ccbIdArrSize, SaUint32T *pbeConn,
                                 SaClmNodeIdT *pbeNodeId, SaUint32T *pbeId);

SaUint32T immModel_getIdForLargeAdmo(IMMND_CB *cb);

SaAisErrorT immModel_ccbApply(IMMND_CB *cb, SaUint32T ccbId,
                              SaUint32T replyConn, SaUint32T *arrSize,
                              SaUint32T **implConnArr, SaUint32T **implIdArr,
                              SaUint32T **ctnArr, bool validateOnly);

bool immModel_ccbAbort(IMMND_CB *cb, SaUint32T ccbId, SaUint32T *arrSize,
                       SaUint32T **implConnArr, SaUint32T **clientArr,
                       SaUint32T *clArrSize, SaUint32T *nodeId,
                       SaClmNodeIdT *pbeNodeId);

void immModel_getCcbIdsForOrigCon(IMMND_CB *cb, SaUint32T origConn,
                                  SaUint32T *arrSize, SaUint32T **ccbIdArr);

void immModel_discardNode(IMMND_CB *cb, SaUint32T nodeId, SaUint32T *arrSize,
                          SaUint32T **ccbIdArr, SaUint32T *globArrSize,
                          SaUint32T **globccbIdArr);

SaAisErrorT immModel_ccbObjectDelete(
    IMMND_CB *cb, const struct ImmsvOmCcbObjectDelete *req, SaUint32T reqConn,
    SaUint32T *arrSize, SaUint32T **implConnArr, SaUint32T **implIdArr,
    SaStringT **objNameArr, SaUint32T *pbeConn, SaClmNodeIdT *pbeNodeId,
    bool *augDelete, bool *hasLongDn);

SaAisErrorT immModel_ccbObjectCreate(
    IMMND_CB *cb, struct ImmsvOmCcbObjectCreate *req, SaUint32T *implConn,
    SaClmNodeIdT *implNodeId, SaUint32T *continuationId, SaUint32T *pbeConn,
    SaClmNodeIdT *pbeNodeId, SaNameT *objName, bool *dnOrRdnIsLong,
    bool isObjectDnUsed);

SaUint32T immModel_getLocalAppliersForObj(IMMND_CB *cb, const SaNameT *objName,
                                          SaUint32T ccbId,
                                          SaUint32T **aplConnArr,
                                          bool externalRep);
SaUint32T immModel_getLocalAppliersForCcb(IMMND_CB *cb, SaUint32T ccbId,
                                          SaUint32T **aplConnArr,
                                          SaUint32T *applCtnPtr);
SaUint32T immModel_getPbeApplierConn(IMMND_CB *cb);

SaAisErrorT immModel_ccbObjectModify(
    IMMND_CB *cb, const struct ImmsvOmCcbObjectModify *req, SaUint32T *implConn,
    SaClmNodeIdT *implNodeId, SaUint32T *continuationId, SaUint32T *pbeConn,
    SaClmNodeIdT *pbeNodeId, SaNameT *objName, bool *hasLongDns);

void immModel_ccbCompletedContinuation(IMMND_CB *cb,
                                       struct immsv_oi_ccb_upcall_rsp *rsp,
                                       SaUint32T *reqConn);

void immModel_ccbObjDelContinuation(IMMND_CB *cb,
                                    struct immsv_oi_ccb_upcall_rsp *rsp,
                                    SaUint32T *reqConn, bool *augDelete);

bool immModel_ccbWaitForCompletedAck(IMMND_CB *cb, SaUint32T ccbId,
                                     SaAisErrorT *err, SaUint32T *pbeConn,
                                     SaClmNodeIdT *pbeNodeId, SaUint32T *pbeId,
                                     SaUint32T *pbeCtn);

bool immModel_ccbWaitForDeleteImplAck(IMMND_CB *cb, SaUint32T ccbId,
                                      SaAisErrorT *err, bool augDelete);

bool immModel_ccbCommit(IMMND_CB *cb, SaUint32T ccbId, SaUint32T *arrSize,
                        SaUint32T **implConnArr);
SaAisErrorT immModel_ccbFinalize(IMMND_CB *cb, SaUint32T ccbId);

SaAisErrorT immModel_searchInitialize(IMMND_CB *cb,
                                      struct ImmsvOmSearchInit *req,
                                      void **searchOp, bool isSync,
                                      bool isAccessor);

SaAisErrorT immModel_objectIsLockedByCcb(IMMND_CB *cb,
                                         struct ImmsvOmSearchInit *req);
SaAisErrorT immModel_ccbReadLockObject(IMMND_CB *cb,
                                       struct ImmsvOmSearchInit *req);

SaAisErrorT immModel_testTopResult(void *searchOp, SaUint32T *implNodeId,
                                   bool *bRtAttrsToFetch);

SaAisErrorT immModel_nextResult(IMMND_CB *cb, void *searchOp,
                                IMMSV_OM_RSP_SEARCH_NEXT **rsp,
                                SaUint32T *implConn, SaUint32T *implNodeId,
                                struct ImmsvAttrNameList **rtAttrsToFetch,
                                MDS_DEST *implDest, bool retardSync,
                                SaUint32T *implTimeout);

void immModel_deleteSearchOp(void *searchOp);

void immModel_clearLastResult(void *searchOp);

void immModel_fetchLastResult(void *searchOp, IMMSV_OM_RSP_SEARCH_NEXT **rsp);

void immModel_popLastResult(void *searchOp);

bool immModel_isSearchOpAccessor(void *searchOp);

bool immModel_isSearchOpNonExtendedNameSet(void *searchOp);

void immModel_setAdmReqContinuation(IMMND_CB *cb, SaInvocationT invoc,
                                    SaUint32T reqCon);

void immModel_setSearchReqContinuation(IMMND_CB *cb, SaInvocationT invoc,
                                       SaUint32T reqCon, SaUint32T implTimeout);

void immModel_setSearchImplContinuation(IMMND_CB *cb, SaUint32T searchId,
                                        SaUint32T requestnodeId,
                                        MDS_DEST reply_dest);

SaAisErrorT immModel_implementerSet(IMMND_CB *cb,
                                    const IMMSV_OCTET_STRING *implName,
                                    SaUint32T implConn, SaUint32T implNodeId,
                                    SaUint32T implId, MDS_DEST mds_dest,
                                    SaUint32T implTimeout,
                                    bool *discardImplementer);

SaAisErrorT immModel_implementerClear(IMMND_CB *cb,
                                      const struct ImmsvOiImplSetReq *req,
                                      SaUint32T implConn, SaUint32T implNodeId,
                                      SaUint32T *globArrSize,
                                      SaUint32T **globccbIdArr);
SaAisErrorT immModel_classImplementerSet(IMMND_CB *cb,
                                         const struct ImmsvOiImplSetReq *req,
                                         SaUint32T implConn,
                                         SaUint32T implNodeId,
                                         SaUint32T *ccbId);

SaAisErrorT immModel_classImplementerClear(IMMND_CB *cb,
                                           const struct ImmsvOiImplSetReq *req,
                                           SaUint32T implConn,
                                           SaUint32T implNodeId);

SaAisErrorT immModel_classImplementerRelease(
    IMMND_CB *cb, const struct ImmsvOiImplSetReq *req, SaUint32T implConn,
    SaUint32T implNodeId);

SaAisErrorT immModel_objectImplementerSet(IMMND_CB *cb,
                                          const struct ImmsvOiImplSetReq *req,
                                          SaUint32T implConn,
                                          SaUint32T implNodeId,
                                          SaUint32T *ccbId);

SaAisErrorT immModel_objectImplementerRelease(
    IMMND_CB *cb, const struct ImmsvOiImplSetReq *req, SaUint32T implConn,
    SaUint32T implNodeId);
SaUint32T immModel_getImplementerId(IMMND_CB *cb, SaUint32T implConn);

void immModel_discardImplementer(IMMND_CB *cb, SaUint32T implId,
                                 bool reallyDiscard, SaUint32T *globArrSize,
                                 SaUint32T **globccbIdArr);

void immModel_fetchAdmOpContinuations(IMMND_CB *cb, SaInvocationT inv,
                                      bool local, SaUint32T *implConn,
                                      SaUint32T *reqConn,
                                      SaUint64T *reply_dest);

void immModel_fetchSearchReqContinuation(IMMND_CB *cb, SaInvocationT inv,
                                         SaUint32T *reqConn);
void immModel_fetchSearchImplContinuation(IMMND_CB *cb, SaUint32T searchId,
                                          SaUint32T reqNodeId,
                                          MDS_DEST *reply_dest);

SaUint32T immModel_findConnForImplementerOfObject(
    IMMND_CB *cb, IMMSV_OCTET_STRING *objectName);

void immModel_prepareForLoading(IMMND_CB *cb);

bool immModel_readyForLoading(IMMND_CB *cb);

SaInt32T immModel_getLoader(IMMND_CB *cb);

void immModel_setLoader(IMMND_CB *cb, SaInt32T loaderPid);

unsigned int immModel_pbeOiExists(IMMND_CB *cb);
unsigned int immModel_pbeBSlaveExists(IMMND_CB *cb);

struct immsv_attr_values_list *immModel_specialApplierTrimCreate(
    IMMND_CB *cb, SaUint32T clientId, struct ImmsvOmCcbObjectCreate *req);

void immModel_specialApplierSavePrtoCreateAttrs(
    IMMND_CB *cb, struct ImmsvOmCcbObjectCreate *req, SaUint32T continuationId);

void immModel_specialApplierSaveRtUpdateAttrs(
    IMMND_CB *cb, struct ImmsvOmCcbObjectModify *req, SaUint32T continuationId);

struct immsv_attr_mods_list *immModel_specialApplierTrimModify(
    IMMND_CB *cb, SaUint32T clientId, struct ImmsvOmCcbObjectModify *req);

bool immModel_isSpecialAndAddModify(IMMND_CB *cb, SaUint32T clientId,
                                    SaUint32T ccbId);

void immModel_genSpecialModify(IMMND_CB *cb,
                               struct ImmsvOmCcbObjectModify *req);

struct immsv_attr_mods_list *immModel_canonicalizeAttrModification(
    IMMND_CB *cb, const struct ImmsvOmCcbObjectModify *req);

struct immsv_attr_mods_list *immModel_getAllWritableAttributes(
    IMMND_CB *cb, const struct ImmsvOmCcbObjectModify *req, bool *hasLongDn);

bool immModel_protocol41Allowed(IMMND_CB *cb);
bool immModel_protocol43Allowed(IMMND_CB *cb);
bool immModel_protocol45Allowed(IMMND_CB *cb);
bool immModel_protocol46Allowed(IMMND_CB *cb);
bool immModel_protocol47Allowed(IMMND_CB *cb);
bool immModel_protocol50Allowed(IMMND_CB *cb);
bool immModel_oneSafe2PBEAllowed(IMMND_CB *cb);
OsafImmAccessControlModeT immModel_accessControlMode(IMMND_CB *cb);
const char *immModel_authorizedGroup(IMMND_CB *cb);

bool immModel_purgeSyncRequest(IMMND_CB *cb, SaUint32T clientId);

void immModel_recognizedIsolated(IMMND_CB *cb);

bool immModel_syncComplete(IMMND_CB *cb);

bool immModel_ccbsTerminated(IMMND_CB *cb, bool allowEmpty);

void immModel_prepareForSync(IMMND_CB *cb, bool isJoining);

SaAisErrorT immModel_objectSync(IMMND_CB *cb,
                                const struct ImmsvOmObjectSync *req);

SaAisErrorT immModel_finalizeSync(IMMND_CB *cb, struct ImmsvOmFinalizeSync *req,
                                  bool isCoord, bool isSyncClient);

SaUint32T immModel_adjustEpoch(IMMND_CB *cb, SaUint32T suggestedEpoch,
                               SaUint32T *continuationId, SaUint32T *pbeConn,
                               SaClmNodeIdT *pbeNodeId, bool increment);

SaUint32T immModel_adminOwnerChange(IMMND_CB *cb,
                                    const struct immsv_a2nd_admown_set *req,
                                    bool isRelease);
void immModel_getAdminOwnerIdsForCon(IMMND_CB *cb, SaUint32T conn,
                                     SaUint32T *arrSize, SaUint32T **ccbIdArr);

void immModel_ccbObjCreateContinuation(IMMND_CB *cb, SaUint32T ccbId,
                                       SaUint32T invocation, SaAisErrorT error,
                                       SaUint32T *reqConn);

void immModel_pbePrtObjCreateContinuation(IMMND_CB *cb, SaUint32T invocation,
                                          SaAisErrorT err, SaClmNodeIdT nodeId,
                                          SaUint32T *reqConn,
                                          SaUint32T *spApplConn,
                                          struct ImmsvOmCcbObjectCreate *req);

int immModel_pbePrtObjDeletesContinuation(
    IMMND_CB *cb, SaUint32T invocation, SaAisErrorT err, SaClmNodeIdT nodeId,
    SaUint32T *reqConn, SaStringT **objNameArr, SaUint32T *arrSize,
    SaUint32T *spApplConn, SaUint32T *pbe2BConn);

void immModel_pbePrtAttrUpdateContinuation(IMMND_CB *cb, SaUint32T invocation,
                                           SaAisErrorT err, SaClmNodeIdT nodeId,
                                           SaUint32T *reqConn,
                                           SaUint32T *spApplConn,
                                           struct ImmsvOmCcbObjectModify *req);

void immModel_pbeClassCreateContinuation(IMMND_CB *cb, SaUint32T invocation,
                                         SaClmNodeIdT nodeId,
                                         SaUint32T *reqConn);

void immModel_pbeClassDeleteContinuation(IMMND_CB *cb, SaUint32T invocation,
                                         SaClmNodeIdT nodeId,
                                         SaUint32T *reqConn);

void immModel_pbeUpdateEpochContinuation(IMMND_CB *cb, SaUint32T invocation,
                                         SaClmNodeIdT nodeId);

void immModel_ccbObjModifyContinuation(IMMND_CB *cb, SaUint32T ccbId,
                                       SaUint32T invocation, SaAisErrorT error,
                                       SaUint32T *reqConn);

void immModel_discardContinuations(IMMND_CB *cb, SaUint32T deadConn);

bool immModel_immNotWritable(IMMND_CB *cb);

bool immModel_pbeIsInSync(IMMND_CB *cb, bool checkCriticalCcbs);

SaImmRepositoryInitModeT immModel_getRepositoryInitMode(IMMND_CB *cb);

unsigned int immModel_getMaxSyncBatchSize(IMMND_CB *cb);

SaAisErrorT immModel_rtObjectCreate(IMMND_CB *cb,
                                    struct ImmsvOmCcbObjectCreate *req,
                                    SaUint32T implConn, SaClmNodeIdT implNodeId,
                                    SaUint32T *continuationId,
                                    SaUint32T *pbeConn, SaClmNodeIdT *pbeNodeId,
                                    SaUint32T *spAplConn, SaUint32T *pbe2BConn,
                                    bool isObjectDnUsed);

SaAisErrorT immModel_rtObjectDelete(
    IMMND_CB *cb, const struct ImmsvOmCcbObjectDelete *req, SaUint32T implConn,
    SaClmNodeIdT implNodeId, SaUint32T *continuationId, SaUint32T *pbeConn,
    SaClmNodeIdT *pbeNodeId, SaStringT **objNameArr, SaUint32T *arrSize,
    SaUint32T *spApplConn, SaUint32T *pbe2BConn);

SaAisErrorT immModel_rtObjectUpdate(IMMND_CB *cb,
                                    const struct ImmsvOmCcbObjectModify *req,
                                    SaUint32T implConn, SaClmNodeIdT implNodeId,
                                    unsigned int *isPureLocal,
                                    SaUint32T *continuationId,
                                    SaUint32T *pbeConn, SaClmNodeIdT *pbeNodeId,
                                    SaUint32T *spAplConn, SaUint32T *pbe2BConn);

SaAisErrorT immModel_ccbResult(IMMND_CB *cb, SaUint32T ccbId);

IMMSV_ATTR_NAME_LIST *immModel_ccbGrabErrStrings(IMMND_CB *cb, SaUint32T ccbId);

void immModel_deferRtUpdate(IMMND_CB *cb, struct ImmsvOmCcbObjectModify *req,
                            SaUint64T msgNo);

bool immModel_fetchRtUpdate(IMMND_CB *cb, struct ImmsvOmObjectSync *syncReq,
                            struct ImmsvOmCcbObjectModify *rtModReq,
                            SaUint64T syncFevsBase);

SaAisErrorT immModel_ccbAugmentInit(IMMND_CB *cb,
                                    IMMSV_OI_CCB_UPCALL_RSP *ccbUpcallRsp,
                                    SaUint32T originatingNode,
                                    SaUint32T originatingConn,
                                    SaUint32T *adminOwnerId);

void immModel_ccbAugmentAdmo(IMMND_CB *cb, SaUint32T adminOwnerId,
                             SaUint32T ccbId);

bool immModel_pbeNotWritable(IMMND_CB *cb);

SaAisErrorT immModel_implIsFree(IMMND_CB *cb, const IMMSV_OI_IMPLSET_REQ *req,
                                SaUint32T *impl_id);

SaAisErrorT immModel_resourceDisplay(
    IMMND_CB *cb, const struct ImmsvAdminOperationParam *reqparams,
    struct ImmsvAdminOperationParam **rparams);

void immModel_setCcbErrorString(IMMND_CB *cb, SaUint32T ccbId,
                                const char *errorString, ...);

void immModel_setScAbsenceAllowed(IMMND_CB *cb);

void immmModel_getLocalImplementers(IMMND_CB *cb, SaUint32T *arrSize,
                                    SaUint32T **implIdArr,
                                    SaUint32T **implConnArr);

#ifdef __cplusplus
}
#endif
/* End ImmModel.cc */ /* File : ---  immnd_amf.c */ uint32_t immnd_amf_init(
    IMMND_CB *immnd_cb);

/* End immnd_amf.c */

/* File immnd_db.c */

void immnd_client_node_get(IMMND_CB *cb, SaImmHandleT imm_client_hdl,
                           IMMND_IMM_CLIENT_NODE **imm_client_node);
void immnd_client_node_getnext(IMMND_CB *cb, SaImmHandleT imm_client_hdl,
                               IMMND_IMM_CLIENT_NODE **imm_client_node);
uint32_t immnd_client_node_add(IMMND_CB *cb, IMMND_IMM_CLIENT_NODE *imm_node);
uint32_t immnd_client_node_del(IMMND_CB *cb,
                               IMMND_IMM_CLIENT_NODE *imm_client_node);

unsigned int immnd_dequeue_outgoing_fevs_msg(IMMND_CB *cb,
                                             IMMSV_OCTET_STRING *msg,
                                             SaImmHandleT *clnt_hdl);

unsigned int immnd_enqueue_outgoing_fevs_msg(IMMND_CB *cb,
                                             SaImmHandleT clnt_hdl,
                                             IMMSV_OCTET_STRING *msg);

void dequeue_outgoing(IMMND_CB *cb);

/* End  File : immnd_db.c */

/* File : --- immnd_mds.c */
SaAisErrorT immnd_mds_client_not_busy(IMMSV_SEND_INFO *s_info);
uint32_t immnd_mds_send_rsp(IMMND_CB *cb, IMMSV_SEND_INFO *s_info,
                            IMMSV_EVT *evt);
uint32_t immnd_mds_msg_sync_send(IMMND_CB *cb, uint32_t to_svc,
                                 MDS_DEST to_dest, IMMSV_EVT *i_evt,
                                 IMMSV_EVT **o_evt, uint32_t timeout);
uint32_t immnd_mds_msg_send(IMMND_CB *cb, uint32_t to_svc, MDS_DEST to_dest,
                            IMMSV_EVT *evt);
uint32_t immnd_mds_register(IMMND_CB *cb);
void immnd_mds_unregister(IMMND_CB *cb);
uint32_t immnd_mds_get_handle(IMMND_CB *cb);
/* End : --- immnd_mds.c */

/* File : ----  immnd_evt.c */
void immnd_process_evt(void);
uint32_t immnd_evt_destroy(IMMSV_EVT *evt, bool onheap, uint32_t line);
void immnd_evt_proc_admo_hard_finalize(IMMND_CB *cb, IMMND_EVT *evt,
                                       bool originatedAtThisNd,
                                       SaImmHandleT clnt_hdl,
                                       MDS_DEST reply_dest);
void freeSearchNext(IMMSV_OM_RSP_SEARCH_NEXT *rsp, bool freeTop);

/* End : ----  immnd_evt.c  */

/* File : ----  immnd_proc.c */
uint32_t immnd_proc_server(uint32_t *timeout);
void immnd_proc_unregister_local_implemeters(IMMND_CB *cb);
/* End : ----  immnd_proc.c  */

/* File : ----  immnd_clm.c */
uint32_t immnd_clm_node_change(bool left);
void immnd_init_with_clm();
/* Ebd : ----  immnd_clm.c */

#endif  // IMM_IMMND_IMMND_INIT_H_
