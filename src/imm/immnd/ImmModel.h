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

#ifndef IMM_IMMND_IMMMODEL_H_
#define IMM_IMMND_IMMMODEL_H_ 1

#include <saImmOm.h>
#include <cstdarg>
#include <sys/types.h>
#include <string>
#include <vector>
#include <map>
#include "imm/common/immsv_api.h"

struct ClassInfo;
struct CcbInfo;
struct ObjectInfo;
struct AdminOwnerInfo;
struct ImplementerInfo;

// TODO: Some of the struct forward declarations below are obsolete.

struct ImmsvOmSearchInit;
struct ImmsvOmCcbObjectCreate;
struct ImmsvOmCcbObjectModify;
struct ImmsvOmCcbObjectDelete;
struct ImmsvOmClassDescr;
struct ImmsvOmAdminOperationInvoke;
struct ImmsvOmAdminOwnerInitialize;
struct ImmsvAttrDefinition;
struct immsv_octet_string;
struct immsv_edu_attr_val;
struct ImmsvOiImplSetReq;
struct ImmsvOmObjectSync;
struct immsv_a2nd_admown_set;
struct ImmsvOmSearchOneAttr;
struct ImmsvOmFinalizeSync;

struct ImmOmClassDelete;
struct ImmOmCcbObjectDelete;
struct ImmOmCcbApply;
struct ImmOiObjectImplementerSet;
struct ImmOiRtObjectCreate;

struct immsv_oi_ccb_upcall_rsp;
struct immsv_attr_values_list;
struct immsv_attr_mods_list;

struct ImmsvOmRspSearchNext;

struct ImmsvAttrNameList;

struct AttrInfo;
typedef std::map<std::string, AttrInfo*> AttrMap;

class ImmSearchOp;

typedef std::vector<SaUint32T> ConnVector;
typedef std::vector<unsigned int> NodeIdVector;
typedef std::vector<std::string> ObjectNameVector;
typedef std::vector<std::string> ImplNameVector;
typedef std::vector<SaUint32T> IdVector;
typedef std::map<std::string, ObjectInfo*> ObjectMap;
typedef std::vector<SaInvocationT> InvocVector;

/* Maps an object pointer, to a set of object pointers.*/
typedef std::multimap<ObjectInfo*, ObjectInfo*> ObjectMMap;

/**/
typedef std::map<ObjectInfo*, uint16_t> ObjectShortCountMap;

/* Object mutation */
struct ObjectMutation;
typedef std::map<std::string, ObjectMutation*> ObjectMutationMap;

typedef std::set<std::string> ObjectNameSet;

struct ImmOiImplementerClear;

class ImmModel {
 public:
  ImmModel();
  static ImmModel* instance(void** sInstancep);
  // Classes

  SaAisErrorT classCreate(const ImmsvOmClassDescr* req, SaUint32T reqConn,
                          SaUint32T nodeId, SaUint32T* continuationId,
                          SaUint32T* pbeConn, unsigned int* pbeNodeId);

  bool nocaseCompare(const std::string& s1, const std::string& s2) const;
  const char* authorizedGroup();
  OsafImmAccessControlModeT accessControlMode();
  bool schemaChangeAllowed();
  bool protocol41Allowed();
  bool protocol43Allowed();
  bool protocol45Allowed();
  bool protocol46Allowed();
  bool protocol47Allowed();
  bool protocol50Allowed();
  bool protocol51Allowed();
  bool protocol51710Allowed();
  bool oneSafe2PBEAllowed();
  bool purgeSyncRequest(SaUint32T clientId);
  bool verifySchemaChange(const std::string& className, ClassInfo* oldClass,
                          ClassInfo* newClass, AttrMap& newAttrs,
                          AttrMap& changedAttrs);

  bool notCompatibleAtt(const std::string& className, ClassInfo* newClass,
                        const std::string& attName, const AttrInfo* oldAttr,
                        AttrInfo* newAttr, AttrMap* changedAttrs);

  void migrateObj(ObjectInfo* object, std::string className, AttrMap& newAttrs,
                  AttrMap& changedAttrs);

  SaAisErrorT classDelete(const ImmsvOmClassDescr* req, SaUint32T reqConn,
                          SaUint32T nodeId, SaUint32T* continuationId,
                          SaUint32T* pbeConn, unsigned int* pbeNodeId);

  SaAisErrorT classDescriptionGet(const immsv_octet_string* clName,
                                  ImmsvOmClassDescr* res);

  SaAisErrorT attrCreate(ClassInfo* classInfo, const ImmsvAttrDefinition* attr,
                         const std::string& attrName);

  // Admin ownership

  SaAisErrorT adminOwnerCreate(const ImmsvOmAdminOwnerInitialize* req,
                               SaUint32T ownerId, SaUint32T conn,
                               unsigned int nodeId);

  SaAisErrorT adminOwnerDelete(SaUint32T ownerId, bool hard, bool pbe2 = false);

  SaAisErrorT adminOwnerChange(const immsv_a2nd_admown_set* req,
                               bool release = false);

  // CCB
  SaAisErrorT ccbCreate(SaUint32T adminOwnerId, SaUint32T ccbFlags,
                        SaUint32T ccbId, unsigned int originatingNode,
                        SaUint32T originatingConn);

  SaAisErrorT ccbApply(SaUint32T ccbId, SaUint32T reqConn,
                       ConnVector& connVector, IdVector& implIds,
                       IdVector& continuations, bool validateOnly);

  SaAisErrorT ccbTerminate(SaUint32T ccbId);

  SaAisErrorT ccbAugmentInit(immsv_oi_ccb_upcall_rsp* rsp,
                             SaUint32T originatingNode,
                             SaUint32T originatingConn,
                             SaUint32T* adminOwnerId);

  void ccbAugmentAdmo(SaUint32T adminOwnerid, SaUint32T ccbId);

  SaAisErrorT ccbObjectCreate(ImmsvOmCcbObjectCreate* req, SaUint32T* implConn,
                              unsigned int* implNodeId,
                              SaUint32T* continuationId, SaUint32T* pbeConn,
                              unsigned int* pbeNodeId, std::string& objectName,
                              bool* dnOrRdnIsLong, bool isObjectDnUsed);

  immsv_attr_values_list* specialApplierTrimCreate(SaUint32T clientId,
                                                   ImmsvOmCcbObjectCreate* req);

  void specialApplierSavePrtoCreateAttrs(ImmsvOmCcbObjectCreate* req,
                                         SaUint32T continuationId);

  void specialApplierSaveRtUpdateAttrs(ImmsvOmCcbObjectModify* req,
                                       SaUint32T continuationId);

  immsv_attr_mods_list* specialApplierTrimModify(SaUint32T clientId,
                                                 ImmsvOmCcbObjectModify* req);

  bool isSpecialAndAddModify(SaUint32T clientId, SaUint32T ccbId);
  void genSpecialModify(ImmsvOmCcbObjectModify* req);

  void getLocalAppliersForObj(const SaNameT* objName, SaUint32T ccbId,
                              ConnVector& connVector, bool externalRep);

  void getLocalAppliersForCcb(SaUint32T ccbId, ConnVector& connVector,
                              SaUint32T* appCtnPtr);

  SaUint32T getPbeApplierConn();

  immsv_attr_mods_list* canonicalizeAttrModification(
      const struct ImmsvOmCcbObjectModify* req);

  immsv_attr_mods_list* getAllWritableAttributes(
      const ImmsvOmCcbObjectModify* req, bool* hasLongDn);

  SaAisErrorT ccbObjectModify(const ImmsvOmCcbObjectModify* req,
                              SaUint32T* implConn, unsigned int* implNodeId,
                              SaUint32T* continuationId, SaUint32T* pbeConn,
                              unsigned int* pbeNodeId, std::string& objectName,
                              bool* hasLongDns, bool pbeFile, bool* changeRim);

  SaAisErrorT ccbObjectDelete(const ImmsvOmCcbObjectDelete* req,
                              SaUint32T reqConn,
                              ObjectNameVector& objNameVector,
                              ConnVector& connVector, IdVector& continuations,
                              SaUint32T* pbeConn, unsigned int* pbeNodeId,
                              bool* augDelete, bool* hasLongDn);

  SaAisErrorT deleteObject(ObjectMap::iterator& oi, SaUint32T reqConn,
                           AdminOwnerInfo* adminOwner, CcbInfo* ccb, bool doIt,
                           ObjectNameVector& objNameVector,
                           ConnVector& connVector, IdVector& continuations,
                           unsigned int pbeIsLocal,
                           ObjectInfo** readLockedObject,
                           bool sendToPbe);

  void setCcbErrorString(CcbInfo* ccb, const char* errorString, va_list vl);

  void setCcbErrorString(CcbInfo* ccb, const char* errorString, ...);

  bool hasLocalClassAppliers(ClassInfo* classInfo);
  bool hasLocalObjAppliers(const std::string& objName);

  bool ccbWaitForDeleteImplAck(SaUint32T ccbId, SaAisErrorT* err,
                               bool augDelete);

  bool ccbWaitForCompletedAck(SaUint32T ccbId, SaAisErrorT* err,
                              SaUint32T* pbeConn, unsigned int* pbeNodeId,
                              SaUint32T* pbeId, SaUint32T* pbeCtn,
                              bool mPbeDisableCritical);

  void ccbObjDelContinuation(immsv_oi_ccb_upcall_rsp* rsp, SaUint32T* reqConn,
                             bool* augDelete);

  void ccbCompletedContinuation(immsv_oi_ccb_upcall_rsp* rsp,
                                SaUint32T* reqConn);

  void ccbObjCreateContinuation(SaUint32T ccbId, SaUint32T invocation,
                                SaAisErrorT err, SaUint32T* reqConn);

  void ccbObjModifyContinuation(SaUint32T ccbId, SaUint32T invocation,
                                SaAisErrorT err, SaUint32T* reqConn);

  void pbePrtObjCreateContinuation(SaUint32T invocation, SaAisErrorT error,
                                   unsigned int nodeId, SaUint32T* reqConn,
                                   SaUint32T* spApplConn,
                                   struct ImmsvOmCcbObjectCreate* req);

  int pbePrtObjDeletesContinuation(SaUint32T invocation, SaAisErrorT error,
                                   unsigned int nodeId, SaUint32T* reqConn,
                                   ObjectNameVector& objNameVector,
                                   SaUint32T* spApplConnPtr,
                                   SaUint32T* pbe2BConn);

  void pbePrtAttrUpdateContinuation(SaUint32T invocation, SaAisErrorT error,
                                    unsigned int nodeId, SaUint32T* reqConn,
                                    SaUint32T* spApplConnPtr,
                                    struct ImmsvOmCcbObjectModify* req);

  void pbeClassCreateContinuation(SaUint32T invocation, unsigned int nodeId,
                                  SaUint32T* reqConn);

  void pbeClassDeleteContinuation(SaUint32T invocation, unsigned int nodeId,
                                  SaUint32T* reqConn);

  void pbeUpdateEpochContinuation(SaUint32T invocation, unsigned int nodeId);

  // Administrative operations
  SaAisErrorT adminOperationInvoke(const ImmsvOmAdminOperationInvoke* req,
                                   SaUint32T reqConn, SaUint64T reply_dest,
                                   SaInvocationT& inv, SaUint32T* implConn,
                                   unsigned int* implNodeId, bool pbeExpected,
                                   bool* displayRes, bool isAtCoord);

  // Objects

  SaAisErrorT searchInitialize(ImmsvOmSearchInit* req, ImmSearchOp& op);

  SaAisErrorT nextSyncResult(ImmsvOmRspSearchNext** rsp, ImmSearchOp& op);

  SaAisErrorT objectIsLockedByCcb(const ImmsvOmSearchInit* req);

  SaAisErrorT ccbReadLockObject(const ImmsvOmSearchInit* req);

  SaAisErrorT accessorGet(const ImmsvOmSearchInit* req, ImmSearchOp& op);

  bool filterMatch(ObjectInfo* obj, ImmsvOmSearchOneAttr* filter,
                   SaAisErrorT& err, const char* objName);

  // Implementer API
  SaAisErrorT implementerSet(const immsv_octet_string* implName, SaUint32T con,
                             SaUint32T nodeId, SaUint32T ownerId,
                             SaUint64T mds_dest, SaUint32T implTimeout,
                             bool* discardImplementer);

  SaAisErrorT classImplementerSet(const struct ImmsvOiImplSetReq* req,
                                  SaUint32T con, unsigned int nodeId,
                                  SaUint32T* ccbId);

  SaAisErrorT classImplementerRelease(const struct ImmsvOiImplSetReq* req,
                                      SaUint32T con, unsigned int nodeId);

  SaAisErrorT objectImplementerSet(const struct ImmsvOiImplSetReq* req,
                                   SaUint32T con, unsigned int nodeId,
                                   SaUint32T* ccbId);

  SaAisErrorT objectImplementerRelease(const struct ImmsvOiImplSetReq* req,
                                       SaUint32T con, unsigned int nodeId);

  void clearImplName(ObjectInfo* obj);

  void implementerDelete(const char *implementerName);

  SaAisErrorT implementerClear(const struct ImmsvOiImplSetReq* req,
                               SaUint32T con, unsigned int nodeId, IdVector& gv,
                               bool isAtCoord);

  SaAisErrorT rtObjectCreate(struct ImmsvOmCcbObjectCreate* req, SaUint32T conn,
                             unsigned int nodeId, SaUint32T* continuationId,
                             SaUint32T* pbeConn, unsigned int* pbeNodeId,
                             SaUint32T* spApplConn, SaUint32T* pbe2BConn,
                             bool isObjectDnUsed);

  SaAisErrorT rtObjectUpdate(
      const ImmsvOmCcbObjectModify* req,  // re-used struct
      SaUint32T conn, unsigned int nodeId, bool* isPureLocal,
      SaUint32T* continuationId, SaUint32T* pbeConn, unsigned int* pbeNodeId,
      SaUint32T* specialApplCon, SaUint32T* pbe2BConn);

  void deferRtUpdate(ImmsvOmCcbObjectModify* req, SaUint64T msgNo);

  SaAisErrorT rtObjectDelete(
      const ImmsvOmCcbObjectDelete* req,  // re-used struct
      SaUint32T conn, unsigned int nodeId, SaUint32T* continuationId,
      SaUint32T* pbeConnPtr, unsigned int* pbeNodeIdPtr,
      ObjectNameVector& objNameVector, SaUint32T* specialApplCon,
      SaUint32T* pbe2BConn);

  SaAisErrorT deleteRtObject(ObjectMap::iterator& oi, bool doIt,
                             ImplementerInfo* info, bool& subTreeHasPersistent,
                             bool& subTreeHasSpecialAppl);

  SaAisErrorT resourceDisplay(const struct ImmsvAdminOperationParam* reqparams,
                              struct ImmsvAdminOperationParam** rparams,
                              SaUint64T searchcount);

  void setScAbsenceAllowed(SaUint32T scAbsenceAllowed);

  SaAisErrorT objectSync(const ImmsvOmObjectSync* req);
  bool fetchRtUpdate(ImmsvOmObjectSync* syncReq, ImmsvOmCcbObjectModify* modReq,
                     SaUint64T syncFevsBase);

  SaAisErrorT finalizeSync(ImmsvOmFinalizeSync* req, bool isCoord,
                           bool isSyncClient, SaUint32T* latestCcbId);

  void eduAtValToOs(immsv_octet_string* tmpos, immsv_edu_attr_val* v,
                    SaImmValueTypeT t);

  void getObjectName(ObjectInfo* obj, std::string& name);

  void getParentDn(std::string& parentName, const std::string& objectName);
  void setLoader(int pid);
  int getLoader();
  int adjustEpoch(int suggestedEpoch, SaUint32T* continuationId,
                  SaUint32T* pbeConnPtr, unsigned int* pbeNodeIdPtr,
                  bool increment);

  SaImmRepositoryInitModeT getRepositoryInitMode();
  unsigned int getMaxSyncBatchSize();
  bool getLongDnsAllowed(ObjectInfo* immObject = NULL);
  void prepareForLoading();
  bool readyForLoading();
  void prepareForSync(bool isJoining);
  void recognizedIsolated();
  bool syncComplete(bool isJoining);
  void abortSync();
  void isolateThisNode(unsigned int thisNode, bool isAtCoord);
  void pbePrtoPurgeMutations(unsigned int nodeId, ConnVector& connVector);
  SaAisErrorT ccbResult(SaUint32T ccbId);
  ImmsvAttrNameList* ccbGrabErrStrings(SaUint32T ccbId);
  bool ccbsTerminated(bool allowEmpty);
  bool pbeIsInSync(bool checkCriticalCcbs);
  SaUint32T getIdForLargeAdmo();
  void getNonCriticalCcbs(IdVector& cv);
  void getOldCriticalCcbs(IdVector& cv, SaUint32T* pbeConn,
                          unsigned int* pbeNodeId, SaUint32T* pbeId);
  void sendSyncAbortAt(timespec& time);
  void getSyncAbortRsp();
  bool immNotWritable();
  bool immNotPbeWritable(bool isPrtoClient = true);
  void* getPbeOi(SaUint32T* pbeConn, unsigned int* pbeNode,
                 bool fevsSafe = true);
  void* getPbeBSlave(SaUint32T* pbeConn, unsigned int* pbeNode,
                     bool fevsSafe = true);
  bool pbeBSlaveHasExisted();
  ImplementerInfo* getSpecialApplier();
  bool specialApplyForClass(ClassInfo* classInfo);
  SaUint32T findConnForImplementerOfObject(std::string objectDn);
  ImplementerInfo* findImplementer(SaUint32T);
  ImplementerInfo* findImplementer(const std::string&);
  SaUint32T getImplementerId(SaUint32T localConn);
  void discardImplementer(unsigned int implHandle, bool reallyDiscard,
                          IdVector& gv, bool isAtCoord);
  void discardContinuations(SaUint32T dead);
  void discardNode(unsigned int nodeId, IdVector& cv, IdVector& gv,
                   bool isAtCoord, bool scAbsence);
  void getCcbIdsForOrigCon(SaUint32T dead, IdVector& cv);
  void getAdminOwnerIdsForCon(SaUint32T dead, IdVector& cv);
  bool ccbCommit(SaUint32T ccbId, ConnVector& connVector);
  bool ccbAbort(SaUint32T ccbId, ConnVector& connVector,
                ConnVector& clientVector, SaUint32T* nodeId,
                unsigned int* pbeNodeIdPtr);
  SaUint32T cleanTheBasement(InvocVector& admReqs, InvocVector& searchReqs,
                             IdVector& ccbs, IdVector& pbePrtoReqs,
                             ImplNameVector& appliers, bool iAmCoord);

  void fetchAdmImplContinuation(SaInvocationT& inv,
                                SaUint32T* implConn,     // in-out!
                                SaUint64T* reply_dest);  // out
  void fetchAdmReqContinuation(SaInvocationT& inv,
                               SaUint32T* reqConn);  // in-out?
  void fetchSearchReqContinuation(SaInvocationT& inv,
                                  SaUint32T* reqConn);  // in-out?
  void setSearchReqContinuation(SaInvocationT& inv, SaUint32T conn,
                                SaUint32T implTimeout);

  void setAdmReqContinuation(SaInvocationT& inv, SaUint32T conn);

  void fetchSearchImplContinuation(SaInvocationT& inv,
                                   SaUint64T* reply_dest);  // out
  void setSearchImplContinuation(SaInvocationT& inv, SaUint64T reply_dest);

  void fetchCcbObjDelImplContinuation(SaInvocationT& inv, SaUint32T* implConn,
                                      SaUint64T* reply_dest);
  /*
    void              setCcbCompletedImplContinuation(
                                                      SaInvocationT& inv,
                                                      SaUint32T conn,
                                                      SaUint64T reply_dest);
    void              fetchCcbCompletedContinuation(
                                                    SaInvocationT& inv,
                                                    SaUint32T* implConn,
                                                    SaUint64T* reply_dest);
  */

  void setCcbObjCreateImplContinuation(SaInvocationT& inv, SaUint32T conn,
                                       SaUint64T reply_dest);
  void setCcbObjDelImplContinuation(SaInvocationT& inv, SaUint32T conn,
                                    SaUint64T reply_dest);
  void fetchCcbObjCreateImplContinuation(SaInvocationT& inv,
                                         SaUint32T* implConn,     // in-out!
                                         SaUint64T* reply_dest);  // out
  void setCcbObjModifyImplContinuation(SaInvocationT& inv, SaUint32T conn);
  void fetchCcbObjModifyImplContinuation(SaInvocationT& inv,
                                         SaUint32T* implConn);  // in-out!

  SaUint32T fetchSearchReqContinuation(SaInvocationT& inv);

  SaAisErrorT setOneObjectImplementer(std::string objectName, ObjectInfo* obj,
                                      ImplementerInfo* info, bool doIt,
                                      SaUint32T* ccbId);

  SaAisErrorT releaseImplementer(std::string objectName, ObjectInfo* obj,
                                 ImplementerInfo* info, bool doIt);

  SaAisErrorT adminOwnerSet(std::string objectName, ObjectInfo* obj,
                            AdminOwnerInfo* adm, bool doIt);

  SaAisErrorT adminOwnerRelease(std::string objectName, ObjectInfo* obj,
                                AdminOwnerInfo* adm, bool doIt);
  SaAisErrorT verifyImmLimits(ObjectInfo* object, std::string objectName);

  void getLocalImplementers(ConnVector& cv, IdVector& idv);

 private:
  bool checkSubLevel(const std::string& objectName, size_t rootStart);

  bool nameCheck(const std::string& name, bool strict = true) const;
  bool schemaNameCheck(const std::string& name) const;
  bool nameToInternal(std::string& name);
  void nameToExternal(std::string& name);

  void updateImmObject(std::string newClassName, bool remove = false);
  SaAisErrorT updateImmObject2(const ImmsvOmAdminOperationInvoke* req);
  SaAisErrorT admoImmMngtObject(const ImmsvOmAdminOperationInvoke* req,
                                bool isAtCoord);

  void addNoDanglingRefs(ObjectInfo* obj);
  void removeNoDanglingRefs(ObjectInfo* object, ObjectInfo* afim,
                            bool removeRefsToObject = false);
  void addNewNoDanglingRefs(ObjectInfo* obj, ObjectNameSet& dnSet);
  void removeNoDanglingRefSet(ObjectInfo* obj, ObjectNameSet& dnSet);

  void commitCreate(ObjectInfo* afim);
  bool commitModify(const std::string& dn, ObjectInfo* afim);
  void commitDelete(const std::string& dn);

  int loaderPid;  //(-1) => loading not started or loading partiticpant.
                  // >0  => loading in progress here at coordinator.
                  //  0  => loading completed at coordinator and participants.

  bool validateNoDanglingRefsModify(CcbInfo* ccb,
                                    ObjectMutationMap::iterator& omit);
  bool validateNoDanglingRefsDelete(CcbInfo* ccb,
                                    ObjectMutationMap::iterator& omit);
  bool validateNoDanglingRefs(CcbInfo* ccb);

  void collectNoDanglingRefs(ObjectInfo* object, ObjectNameSet& objSet);

  SaAisErrorT objectModifyNDTargetOk(const char* srcObjectName,
                                     const char* attrName,
                                     const char* targetObjectName,
                                     SaUint32T ccbId);

  bool noDanglingRefExist(ObjectInfo* obj, const char* noDanglingRef);

  ObjectInfo* getObjectAfterImageInCcb(const std::string& objName,
                                       SaUint32T ccbId);

  immsv_attr_mods_list* attrValueToAttrMod(const ObjectInfo* obj,
                                           const std::string& attrName,
                                           SaUint32T attrType,
                                           SaImmAttrFlagsT attrFlags = 0,
                                           bool* hasLongDn = NULL);
};

#endif  // IMM_IMMND_IMMMODEL_H_
