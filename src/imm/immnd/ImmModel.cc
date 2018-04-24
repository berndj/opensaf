/*      -*- OpenSAF  -*-
 *
 * (C) Copyright 2008 The OpenSAF Foundation
 * Copyright Ericsson AB 2009, 2017 - All Rights Reserved.
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

#include <set>
#include <algorithm>
#include <time.h>

#include "imm/immnd/ImmModel.h"
#include "imm/immnd/ImmAttrValue.h"
#include "imm/immnd/ImmSearchOp.h"

#include "immnd.h"
#include "base/osaf_unicode.h"
#include "base/osaf_extended_name.h"

// Local types
#define DEFAULT_TIMEOUT_SEC 6  /* Should be saImmOiTimeout in SaImmMngt */
#define PRT_LOW_THRESHOLD 1    /* See ImmModel::immNotPbeWritable */
#define PRT_HIGH_THRESHOLD 4   /* See ImmModel::immNotPbeWritable */
#define CCB_CRIT_THRESHOLD 8   /* See ImmModel::immNotPbeWritable */
#define SEARCH_TIMEOUT_SEC 600 /* Search timeout */

struct ContinuationInfo2 {
  ContinuationInfo2()
      : mCreateTime(kZeroSeconds), mConn(0), mTimeout(0), mImplId(0) {}
  ContinuationInfo2(SaUint32T conn, SaUint32T timeout)
      : mConn(conn), mTimeout(timeout), mImplId(0) {
    osaf_clock_gettime(CLOCK_MONOTONIC, &mCreateTime);
  }
  timespec mCreateTime;
  SaUint32T mConn;
  SaUint32T mTimeout;  // 0=> no timeout. Otherwise timeout in SECONDS.
  // Default copy constructor and assignement operator used in ContinuationMap2
  SaUint32T mImplId;
};
typedef std::map<SaInvocationT, ContinuationInfo2> ContinuationMap2;

struct ContinuationInfo3 {
  ContinuationInfo3() : mConn(0), mReply_dest(0LL), mImplementer(NULL) {}
  ContinuationInfo3(SaUint32T conn, SaUint64T reply_dest, ImplementerInfo* i)
      : mConn(conn), mReply_dest(reply_dest), mImplementer(i) {}

  // time_t  mCreateTime;
  SaUint32T mConn;
  SaUint64T mReply_dest;
  ImplementerInfo* mImplementer;
  // Default copy constructor and assignement operator used in ContinuationMap3
};

typedef std::map<SaInvocationT, ContinuationInfo3> ContinuationMap3;

struct AttrInfo {
  AttrInfo() : mValueType(0), mFlags(0), mNtfId(0) {}
  SaUint32T mValueType;
  SaImmAttrFlagsT mFlags;
  SaUint32T mNtfId;
  ImmAttrValue mDefaultValue;
};

struct ImplementerInfo {
  ImplementerInfo()
      : mId(0),
        mConn(0),
        mNodeId(0),
        mMds_dest(0LL),
        mAdminOpBusy(0),
        mDying(false),
        mApplier(false),
        mTimeout(DEFAULT_TIMEOUT_SEC),
        mApplierDisconnected(0) {}
  SaUint32T mId;
  SaUint32T mConn;  // Current implementer, only valid on one node.
  // NULL otherwise.
  unsigned int mNodeId;
  std::string mImplementerName;
  SaUint64T mMds_dest;
  unsigned int mAdminOpBusy;
  bool mDying;
  bool mApplier;       // This is an applier OI
  SaUint32T mTimeout;  // OI callback timeout
  SaUint64T mApplierDisconnected;  // Time when applier is disconnected
};

typedef std::vector<ImplementerInfo*> ImplementerVector;
typedef std::set<ImplementerInfo*> ImplementerSet;
typedef std::map<std::string, ImplementerSet*> ImplementerSetMap;

typedef std::map<ImplementerInfo*, ContinuationInfo2> ImplementerEvtMap;

struct ImplementerCcbAssociation {
  explicit ImplementerCcbAssociation(ImplementerInfo* impl)
      : mImplementer(impl), mContinuationId(0), mWaitForImplAck(false) {}
  ImplementerInfo* mImplementer;
  SaUint32T mContinuationId;  // used to identify related pending msg replies.
  bool mWaitForImplAck;
};

typedef std::map<SaUint32T, ImplementerCcbAssociation*> CcbImplementerMap;

typedef std::set<ObjectInfo*> ObjectSet;

struct ClassInfo {
  explicit ClassInfo(SaUint32T category)
      : mCategory(category), mImplementer(NULL) {}
  ~ClassInfo() {
    mCategory = 0;
    mImplementer = NULL;
  }

  SaUint32T mCategory;
  AttrMap mAttrMap;               //<-Each AttrInfo requires explicit delete
  ImplementerInfo* mImplementer;  //<- Main OI points INTO sImplementerVector
  ObjectSet mExtent;
  ImplementerSet mAppliers;  // OIs did classImplementerSet on this class
};
typedef std::map<std::string, ClassInfo*> ClassMap;

typedef std::map<std::string, ImmAttrValue*> ImmAttrValueMap;

typedef SaUint32T ImmObjectFlags;
#define IMM_CREATE_LOCK 0x00000001
// If create lock is on, it signifies that a ccb has reserved space in
// the name-tree for the object, pending the ccb-apply. The object must
// be invisible in contents (invisinble to search and get), visible in name
// to creates from any ccb, and finally, invisible as 'parent' to creates
// from other Ccb's. Creates of subobjects in the same ccb must be allowed.
// Deletes from other ccbs need not test on ths lock because they will conflict
// on the ccbId of the object anyway.
// The lock is also used during creates of persistent runtime objects (PRTOs).
// If the Persistent Back End (PBE) is enabled, then the create of the PRTO
// has to wait on ack from the PBE before being released to the client.
// During the wait, the create lock is set on the object.

#define IMM_DELETE_LOCK 0x00000002
// If the delete-lock is on, it signifies that a ccb has registered a delete
// of this object, pending the apply of the ccb. The only signifficance of this
// is to prevent the creates of subobjects to this object in any ccb.
// Even in the SAME ccb it is not allowed to create subobject to a registered
// delete.
// The lock is also used during deletes of persistent runtime objects (PRTOs).
// If the Persistent Back End (PBE) is enabled, then the deletes of a subtree,
// which may include both PRTOs and RTOs, has to wait on ack from the PBE,
// before being released to the client.
// During the wait, the delete lock is set on the object.

#define IMM_DN_INTERNAL_REP 0x00000004
// If this flag is set it indicates that the DN has been changed in rep
// from external rep to an internal rep, where any escaped commas "\,"
// in external rep have been replaced by an escape hash "\#" in internal rep;
// Escape hash in external rep are not allowed, will be rejected in create.

#define IMM_PRTO_FLAG 0x00000008
// Flag indicates that the object is a persistent runtime object (PRTO).
// This flag is not reliably set in general. I.e. it can not (yet) be
// used in general to test if the object is a PRTO.
// If the flag is set then the object must be a PRTO.
// If the flag is not set, the object could still be a PRTO.
// It is currently only used in rtObjectDelete() and deleteRtObject()

#define IMM_RT_UPDATE_LOCK 0x00000010
// This is only set by rtObjectUpdate() when the update includes persistent
// runtime attributes PRTAs. The PRTAs may belong to either a runtime object
// or a configureation object. If the PBE is enabled, then the updates of
// cached RTAs (peristent or just cached) included in the call, has to wait
// for ack from the PBE on the perisistification of the PRTAs.
// The lock must be inspected/read by both ccb calls and RT calls.

#define IMM_DELETE_ROOT 0x00000020
// This flag is set in conjunction with a delete-operation only on the root
// object for the possibly cascading delete. This is used to identify the root
// delete object during the commit of the ccb/prto-delete, so that we can
// decrement  the child-counter in any parent(s) of the deleted subtree.

#define IMM_RTNFY_FLAG 0x00000040
// Flag indicates that the object is a runtime object that has some
// attributes flagged for special notification. See: SA_IMM_ATTR_NOTIFY.
// This flag is not reliably set in general. I.e. it can not be
// used in general to test if the object has a notifiable attribute.
// If the flag is set then the object must have such an attribute.
// But if the flag is not set, the object may still have such an attr.
// It is currently only used in rtObjectDelete() and deleteRtObject()

#define IMM_NO_DANGLING_FLAG 0x00000080
// The flag indicates that a CCB operation is done on an object with
// NO_DANGLING attributes.
// The flag is set to the object in next cases:
// a) creates an object with no dangling attribute(s) (that is/are not empty).
// b) deletes an object that has no dangling attribute(s) (that are not empty).
// c) modifies a NO_DANGLING attribute,
// d) deletes an object that is referred to by some NO_DANGLING attribute.

#define IMM_SHARED_READ_LOCK 0x00000100
// If shared read lock is on, it signifies that one or more ccbs have locked the
// object for safe-read. This prevents the object from being modified or deleted
// by any other ccb until safe-readers have released the read lock.
// If only one single ccb has a read lock on an object, i.e. the lock is
// currently  not actually shared, then that ccb may upgrade the read-lock to an
// exclusive lock  for delete or modify.  See also the data structure
// sObjectReadLockCount.

struct ObjectInfo {
  ObjectInfo()
      : mAdminOwnerAttrVal(NULL),
        mCcbId(0),
        mClassInfo(NULL),
        mImplementer(NULL),
        mObjFlags(0),
        mParent(NULL),
        mChildCount(0) {}

  ~ObjectInfo() {
    mAdminOwnerAttrVal = NULL;
    mCcbId = 0;
    mClassInfo = NULL;
    mImplementer = NULL;
    mObjFlags = 0;
    mParent = NULL;
    mChildCount = 0;
  }

  void getAdminOwnerName(std::string* str) const;

  ImmAttrValue* mAdminOwnerAttrVal;  // Pointer INTO mAttrValueMap
  SaUint32T mCcbId;  // Zero => may be read-locked see:IMM_SHARED_READ_LOCK
                     // Nonzero => may be exclusive lock if id is active ccb
  ImmAttrValueMap mAttrValueMap;  //<-Each ImmAttrValue needs explicit delete
  ClassInfo* mClassInfo;          //<-Points INTO ClassMap. Not own copy!
  ImplementerInfo* mImplementer;  //<-Points INTO ImplementerVector
  ImmObjectFlags mObjFlags;
  ObjectInfo* mParent;    //<-Points to parent object
  SaUint32T mChildCount;  //<-Nrof children, transitive
};

struct DeferredRtAUpdate {
  SaUint64T fevsMsgNo;
  immsv_attr_mods_list* attrModsList;
};

typedef std::list<DeferredRtAUpdate> DeferredRtAUpdateList;

typedef std::map<std::string, DeferredRtAUpdateList*> DeferredObjUpdatesMap;

void ObjectInfo::getAdminOwnerName(std::string* str) const {
  osafassert(this->mAdminOwnerAttrVal);
  str->clear();
  if (!(this->mAdminOwnerAttrVal->empty())) {
    str->assign(this->mAdminOwnerAttrVal->getValueC_str());
  }
}

typedef enum {
  IMM_CREATE = 1,
  IMM_MODIFY = 2,
  IMM_DELETE = 3,
  IMM_CREATE_CLASS = 4,
  IMM_DELETE_CLASS = 5,
  IMM_UPDATE_EPOCH = 6
} ImmMutationType;

struct ObjectMutation {
  explicit ObjectMutation(ImmMutationType opType)
      : mOpType(opType),
        mAfterImage(NULL),
        mContinuationId(0),
        mAugmentAdmo(0),
        mSavedData(NULL),
        mWaitForImplAck(false),
        mIsAugDelete(false),
        m2PbeCount(0) {}
  ~ObjectMutation() {
    osafassert(mAfterImage == NULL);
    mContinuationId = 0;
    mAugmentAdmo = 0;
    mWaitForImplAck = false;
    mIsAugDelete = false;
    if (mSavedData) {
      switch (mOpType) {
        case IMM_CREATE:
          immsv_free_attrvalues_list((IMMSV_ATTR_VALUES_LIST*)mSavedData);
          break;

        case IMM_MODIFY:
          immsv_free_attrmods((IMMSV_ATTR_MODS_LIST*)mSavedData);
          break;

        case IMM_DELETE:
          TRACE("Nothing to do");
          break;

        default:
          osafassert(false);
      }
      mSavedData = NULL;
    }
  }

  ImmMutationType mOpType;
  ObjectInfo* mAfterImage;
  SaUint32T mContinuationId;  // used to identify related pending msg replies.
  SaUint32T mAugmentAdmo; /* Aug admo with ROF==true for aug ccbCreates #2428 */
  void* mSavedData;       // For special applier PRTO. Type of data depends on
                     // mOpType
  bool mWaitForImplAck;  // ack required from implementer for THIS object
  bool mIsAugDelete;     // The mutation is an augmented delete.
  SaUint8T m2PbeCount;   // How many PBE replies to wait for (2PBE only).
};

typedef enum {
  IMM_CCB_EMPTY = 1,      // State after creation
  IMM_CCB_READY = 2,      // Ready for new ops, or commit or abort.
  IMM_CCB_CREATE_OP = 3,  // Ongoing create (pending implementer calls/replies)
  IMM_CCB_MODIFY_OP = 4,  // Ongoing modify (pending implementer calls/replies)
  IMM_CCB_DELETE_OP = 5,  // Ongoing delete (pending implementer calls/replies)
  IMM_CCB_VALIDATING =
      6,  // Explicit validate started (saImmOmCcbValidate only)
  IMM_CCB_VALIDATED =
      7,  // Explicit validate has completed (saImmOmCcbValidate only)
  IMM_CCB_PREPARE = 8,   // Waiting for nodes prepare & completed calls/replies
  IMM_CCB_CRITICAL = 9,  // Unilateral abort no longer allowed (except by PBE).
  IMM_CCB_PBE_ABORT = 10,  // The Persistent back end replied with abort
  IMM_CCB_COMMITTED = 11,  // Committed at nodes pending implementer apply calls
  IMM_CCB_ABORTED = 12,    // READY->ABORTED PREPARE->ABORTED
  IMM_CCB_ILLEGAL = 13     // CCB has been removed.
} ImmCcbState;

struct AugCcbParent {
  AugCcbParent()
      : mOriginatingConn(0),
        mOriginatingNode(0),
        mState(IMM_CCB_ILLEGAL),
        mErrorStrings(NULL),
        mContinuationId(0),
        mImplId(0),
        mAugmentAdmo(0) {}
  SaUint32T mOriginatingConn;        // Deferred Originating conn
  unsigned int mOriginatingNode;     // Deferred originating node
  ImmCcbState mState;                // Deferred state
  ImmsvAttrNameList* mErrorStrings;  // Deferred errorStrings
  SaUint32T mContinuationId;         // Deferred continuationId
  SaUint32T mImplId;                 /* ImplId for augmenting implementer*/
  SaUint32T mAugmentAdmo; /* Aug admo with ROF==true for aug ccbCreates #2428 */
};

static ObjectShortCountMap
    sObjectReadLockCount;  // Needs to be declared above CcbInfo

struct CcbInfo {
  CcbInfo()
      : mId(0),
        mAdminOwnerId(0),
        mCcbFlags(0),
        mOriginatingConn(0),
        mOriginatingNode(0),
        mState(IMM_CCB_ILLEGAL),
        mVeto(SA_AIS_OK),
        mWaitStartTime(kZeroSeconds),
        mOpCount(0),
        mPbeRestartId(0),
        mErrorStrings(NULL),
        mAugCcbParent(NULL),
        mPurged(false) {}
  bool isOk() { return mVeto == SA_AIS_OK; }
  bool isActive() { return (mState < IMM_CCB_COMMITTED); }
  void addObjReadLock(ObjectInfo* obj, std::string& objName);
  void removeObjReadLock(ObjectInfo* obj, bool removeObject = true);
  void removeAllObjReadLocks();  // Removes all locks held by *this* ccb.
  bool isReadLockedByThisCcb(ObjectInfo* obj);

  SaUint32T mId;
  SaUint32T mAdminOwnerId;
  SaUint32T mCcbFlags;

  SaUint32T mOriginatingConn;  // If !NULL then originating at this node
  // with this conn.
  unsigned int mOriginatingNode;  // Needed for node crash Ccb GC
  ImmCcbState mState;
  CcbImplementerMap mImplementers;
  ObjectMutationMap mMutations;
  SaAisErrorT mVeto;  // SA_AIS_OK as long as no "participan" voted error.
  timespec mWaitStartTime;
  SaUint32T mOpCount;
  SaUint32T mPbeRestartId; /* ImplId for new PBE to resolve CCBs in critical */
  ImplementerSet mLocalAppliers;
  ImmsvAttrNameList* mErrorStrings; /*Error strings generated by current op */
  AugCcbParent* mAugCcbParent;
  ObjectSet mSafeReadSet;
  bool mPurged; /* True if the ccb continuation is purged.
                   Note that on remote nodes, mPurged is always 'false' */
};
typedef std::vector<CcbInfo*> CcbVector;

void CcbInfo::addObjReadLock(ObjectInfo* obj, std::string& objName) {
  ObjectShortCountMap::iterator oscm;
  if (obj->mObjFlags & IMM_SHARED_READ_LOCK) {
    TRACE_7("Object '%s' already locked for safe-read by some ccb(s)",
            objName.c_str());
    // Read-lock is a shared lock, join the group of lockers.
  } else {
    osafassert(!(obj->mObjFlags & IMM_CREATE_LOCK));
    osafassert(!(obj->mObjFlags & IMM_DELETE_LOCK));
    /* IMM_RT_UPDATE_LOCK does not conflict with safe read because it deals with
       PRTO/PRTA updates, i.e. runtime data and not config data.
    */

    osafassert(obj->mCcbId == 0);
    /* A non-zero mCcbId for an object indicates exclusive lock
       (create/delete/modify) if and only if the identified ccb is still
       *active*. A zero mCcbId for an object *guarantees* that it is not
       exclusive locked. This function *requires* that the invoker has done the
       pre-check of no interference of a proposed shared reader with any
       pre-existing exclusive op and zeroed the ccbId if and only if an existing
       ccbId was no longer identifying an active ccb.

       There is no explicit ccb-modify lock since it can be inferred by (a) the
       ccb-id of an object identifying an active CCB and (b) neither the create
       or modify flag being set on the object.
    */

    obj->mObjFlags |= IMM_SHARED_READ_LOCK;
  }

  this->mSafeReadSet.insert(obj); /* Ccb keeps track of its safe-readers. */

  oscm = sObjectReadLockCount.find(obj);
  if (oscm == sObjectReadLockCount.end()) {
    sObjectReadLockCount[obj] = 1;
  } else {
    // TODO: Should check that we dont increment beyond maxshort.
    (oscm->second)++;
    LOG_IN("Incremented read-lock count for %s to %u", objName.c_str(),
           oscm->second);
  }
}

void CcbInfo::removeAllObjReadLocks() {
  /* Removes all the read-locks set by this ccb.
     Must be executed with ccb-commit or ccb-abort.
  */
  ObjectSet::iterator osi;

  for (osi = this->mSafeReadSet.begin(); osi != this->mSafeReadSet.end();
       ++osi) {
    removeObjReadLock(*osi, false);
  }
  this->mSafeReadSet.clear();
}

void CcbInfo::removeObjReadLock(ObjectInfo* obj,
                                bool removeObject /* = true*/) {
  ObjectShortCountMap::iterator oscm = sObjectReadLockCount.find(obj);
  osafassert(oscm != sObjectReadLockCount.end());
  osafassert(oscm->second > 0);
  (oscm->second)--;
  TRACE("CcbInfo::removeObjReadLock decremented safe read count for %p to %u",
        obj, oscm->second);
  if (oscm->second == 0) {
    sObjectReadLockCount.erase(obj);
    TRACE("Object %p no longer has any read locks", obj);
    obj->mObjFlags &= ~IMM_SHARED_READ_LOCK;
  } else {
    TRACE("Object %p still has read lock count of %u", obj, oscm->second);
  }

  if (removeObject) {
    this->mSafeReadSet.erase(obj);
  }
}

bool CcbInfo::isReadLockedByThisCcb(ObjectInfo* obj) {
  TRACE_ENTER();
  ObjectSet::iterator osi = this->mSafeReadSet.find(obj);
  bool result = (osi != this->mSafeReadSet.end());
  TRACE_LEAVE();
  return result;
}

struct AdminOwnerInfo {
  AdminOwnerInfo()
      : mId(0),
        mConn(0),
        mNodeId(0),
        mReleaseOnFinalize(false),
        mDying(false) {}
  SaUint32T mId;
  std::string mAdminOwnerName;
  SaUint32T mConn;
  unsigned int mNodeId;
  ObjectSet mTouchedObjects;  // No good, sync needs the object names.
  bool mReleaseOnFinalize;
  bool mDying;
};
typedef std::vector<AdminOwnerInfo*> AdminOwnerVector;

typedef enum {
  IMM_NODE_UNKNOWN = 0,          // Initial state
  IMM_NODE_LOADING = 1,          // We are participating in a cluster restart.
  IMM_NODE_FULLY_AVAILABLE = 2,  // Normal fully available state.
  IMM_NODE_ISOLATED = 3,     // We are trying to join an established cluster.
  IMM_NODE_W_AVAILABLE = 4,  // We are being synced, no reads allowed here
  IMM_NODE_R_AVAILABLE = 5   // We are write locked while other nodes are
                            // being synced.
} ImmNodeState;

// Missing parents map used in loading and sync when objects not always
// inserted in tree order. The Object set contains pointers to known children
// of the missing parent.
typedef std::map<std::string, ObjectSet> MissingParentsMap;

// Local variables

static ClassMap sClassMap;
static AdminOwnerVector sOwnerVector;
static CcbVector sCcbVector;
static ObjectMap sObjectMap;
static ObjectMMap sReverseRefsNoDanglingMMap;
/* Maps an object pointer, to the set of pointers to *other* objects
   (not including self and not including children AS CHILDREN)
   where all those other objects contain at least one NO_DANGLING
   flagged reference (SaNameT) containing the DN of this object.
   Child objects could appear in the set of referencing objects if
   the child *explicitly* points to the parent in such a NO_DANGLING
   flagged attribute. That could be optimized out (?) but the main issue
   is if that is more convenient for the rest of the code related to this.
*/
static ImplementerVector sImplementerVector;
static MissingParentsMap sMissingParents;
static ContinuationMap2 sAdmReqContinuationMap;
static ContinuationMap3 sAdmImplContinuationMap;
static ContinuationMap2 sSearchReqContinuationMap;
static ContinuationMap3 sSearchImplContinuationMap;
static ContinuationMap2 sPbeRtReqContinuationMap;
static ObjectMutationMap sPbeRtMutations; /* Persistent Runtime Mutations
                                              At most one mutating op per Prto
                                              is allowed. Entry removed on on
                                              ack from PBE. */
static ImplementerEvtMap
    sImplDetachTime; /* Give admop TRY_AGAIN when impl death is recent */
static SaUint32T sPbeRtMinContId = 0; /* Monitors that no PbePrto gets stuck. */
static SaUint32T sPbeRtBacklog = 0;   /* Monitors PbePrto capacity problems. */
static SaUint32T sPbeRegressPeriods = 0;

static IdVector sNodesDeadDuringSync;  // Keep track of implementers/nodes that
static IdVector
    sImplsDeadDuringSync;  // die after finalizeSync is sent by coord,
                           // but before it arrives over fevs.
                           // This to avoid apparent implementor
                           // re-create by finalizeSync (at non coord).
static IdVector
    sAdmosDeadDuringSync; /* Keep track of admo that die during sync */

static DeferredObjUpdatesMap sDeferredObjUpdatesMap;

static ImplementerSetMap sObjAppliersMap;

static SaUint32T sLastContinuationId = 0;

static ImmNodeState sImmNodeState = IMM_NODE_UNKNOWN;

static const std::string immObjectDn(OPENSAF_IMM_OBJECT_DN);
static const std::string immAttrClasses(OPENSAF_IMM_ATTR_CLASSES);
static const std::string immAttrEpoch(OPENSAF_IMM_ATTR_EPOCH);
static const std::string immClassName(OPENSAF_IMM_CLASS_NAME);
static const std::string immAttrNostFlags(OPENSAF_IMM_ATTR_NOSTD_FLAGS);
static const std::string immSyncBatchSize(OPENSAF_IMM_SYNC_BATCH_SIZE);
static const std::string immPbeBSlaveName(OPENSAF_IMM_2PBE_APPL_NAME);
static const std::string immLongDnsAllowed(OPENSAF_IMM_LONG_DNS_ALLOWED);
static const std::string immAccessControlMode(OPENSAF_IMM_ACCESS_CONTROL_MODE);
static const std::string immAuthorizedGroup(OPENSAF_IMM_AUTHORIZED_GROUP);
static const std::string immScAbsenceAllowed(OPENSAF_IMM_SC_ABSENCE_ALLOWED);
static const std::string immMaxClasses(OPENSAF_IMM_MAX_CLASSES);
static const std::string immMaxAdmOwn(OPENSAF_IMM_MAX_ADMINOWNERS);
static const std::string immMaxCcbs(OPENSAF_IMM_MAX_CCBS);
static const std::string immMaxImp(OPENSAF_IMM_MAX_IMPLEMENTERS);
static const std::string immMinApplierTimeout(OPENSAF_IMM_MIN_APPLIER_TIMEOUT);

static const std::string immMngtClass("SaImmMngt");
static const std::string immManagementDn(
    "safRdn=immManagement,safApp=safImmService");
static const std::string saImmRepositoryInit("saImmRepositoryInit");
static const std::string saImmOiTimeout("saImmOiTimeout");

static SaImmRepositoryInitModeT immInitMode = SA_IMM_INIT_FROM_FILE;

static SaUint32T sCcbIdLongDnGuard =
    0; /* Disallow long DN additions if longDnsAllowed is being changed in ccb*/
static bool sIsLongDnLoaded =
    false; /* track long DNs before opensafImm=opensafImm,safApp=safImmService
              is created */
static bool sAbortNonCriticalCcbs =
    false;                                /* Set to true at coord by the special imm admin-op to abort ccbs
                                             #1107 */
static SaUint32T sTerminatedCcbcount = 0; /* Terminated ccbs count. calculated
                                             at cleanTheBasement  for every
                                             second*/

struct AttrFlagIncludes {
  explicit AttrFlagIncludes(SaImmAttrFlagsT attrFlag) : mFlag(attrFlag) {}

  bool operator()(AttrMap::value_type& item) const {
    return (item.second->mFlags & mFlag) != 0;
  }

  SaImmAttrFlagsT mFlag;
};

struct IdIs {
  explicit IdIs(SaUint32T id) : mId(id) {}

  bool operator()(AdminOwnerInfo*& item) const { return item->mId == mId; }

  SaUint32T mId;
};

struct CcbIdIs {
  explicit CcbIdIs(SaUint32T id) : mId(id) {}

  bool operator()(CcbInfo*& item) const { return item->mId == mId; }

  SaUint32T mId;
};

void immModel_setScAbsenceAllowed(IMMND_CB* cb) {
  if (cb->mCanBeCoord == 4) {
    osafassert(cb->mScAbsenceAllowed > 0);
  } else {
    osafassert(cb->mScAbsenceAllowed == 0);
  }
  ImmModel::instance(&cb->immModel)->setScAbsenceAllowed(cb->mScAbsenceAllowed);
}

SaAisErrorT immModel_ccbResult(IMMND_CB* cb, SaUint32T ccbId) {
  return ImmModel::instance(&cb->immModel)->ccbResult(ccbId);
}

IMMSV_ATTR_NAME_LIST* immModel_ccbGrabErrStrings(IMMND_CB* cb,
                                                 SaUint32T ccbId) {
  return ImmModel::instance(&cb->immModel)->ccbGrabErrStrings(ccbId);
}

void immModel_abortSync(IMMND_CB* cb) {
  ImmModel::instance(&cb->immModel)->abortSync();
}

void immModel_isolateThisNode(IMMND_CB* cb) {
  ImmModel::instance(&cb->immModel)->isolateThisNode(cb->node_id, cb->mIsCoord);
}

void immModel_abortNonCriticalCcbs(IMMND_CB* cb) {
  SaUint32T arrSize;
  SaUint32T* implConnArr = NULL;
  SaUint32T* clientArr = NULL;
  SaUint32T clientArrSize = 0;
  SaClmNodeIdT pbeNodeId;
  SaUint32T nodeId;
  CcbVector::iterator i3 = sCcbVector.begin();
  for (; i3 != sCcbVector.end(); ++i3) {
    if ((*i3)->mState < IMM_CCB_CRITICAL) {
      osafassert(immModel_ccbAbort(cb, (*i3)->mId, &arrSize, &implConnArr,
                                   &clientArr, &clientArrSize, &nodeId,
                                   &pbeNodeId));
      osafassert(immModel_ccbFinalize(cb, (*i3)->mId) == SA_AIS_OK);
      if (arrSize) {
        free(implConnArr);
      }
      if (clientArrSize) {
        free(clientArr);
      }
    }
  }
}

void immModel_pbePrtoPurgeMutations(IMMND_CB* cb, SaUint32T nodeId,
                                    SaUint32T* reqArrSize,
                                    SaUint32T** reqConnArr) {
  ConnVector cv;
  ConnVector::iterator cvi;
  ImmModel::instance(&cb->immModel)->pbePrtoPurgeMutations(nodeId, cv);
  *reqArrSize = (SaUint32T)cv.size();
  if (*reqArrSize) {
    unsigned int ix = 0;
    *reqConnArr = (SaUint32T*)malloc((*reqArrSize) * sizeof(SaUint32T));
    for (cvi = cv.begin(); cvi != cv.end(); ++cvi, ++ix) {
      (*reqConnArr)[ix] = (*cvi);
    }
  }
}

SaAisErrorT immModel_adminOwnerCreate(IMMND_CB* cb,
                                      const ImmsvOmAdminOwnerInitialize* req,
                                      SaUint32T ownerId, SaUint32T conn,
                                      SaClmNodeIdT nodeId) {
  bool preLoad = (cb->preLoadPid > 0);
  ImmNodeState prevState = IMM_NODE_UNKNOWN;
  SaAisErrorT err = SA_AIS_OK;
  if (preLoad) {
    prevState = sImmNodeState;
    sImmNodeState = IMM_NODE_LOADING;
    TRACE("Preload admo create");
  }

  err = ImmModel::instance(&cb->immModel)
            ->adminOwnerCreate(req, ownerId, conn, nodeId);

  if (preLoad) {
    sImmNodeState = prevState;
    TRACE("Preload, returning model state to %u", prevState);
  }

  return err;
}

SaAisErrorT immModel_adminOwnerDelete(IMMND_CB* cb, SaUint32T ownerId,
                                      SaUint32T hard) {
  return ImmModel::instance(&cb->immModel)
      ->adminOwnerDelete(ownerId, hard, cb->m2Pbe);
}

void immModel_addDeadAdminOwnerDuringSync(SaUint32T ownerId) {
  osafassert(sImmNodeState == IMM_NODE_W_AVAILABLE); /* Sync client */
  osafassert(sOwnerVector.empty());
  /* Remember the dead admo during sync.
   * They will be cleaned up when finalizing sync. */
  TRACE("Adding admo id=%u to sAdmosDeadDuringSync", ownerId);
  sAdmosDeadDuringSync.push_back(ownerId);
}

SaAisErrorT immModel_ccbCreate(IMMND_CB* cb, SaUint32T adminOwnerwnerId,
                               SaUint32T ccbFlags, SaUint32T ccbId,
                               SaClmNodeIdT nodeId, SaUint32T conn) {
  return ImmModel::instance(&cb->immModel)
      ->ccbCreate(adminOwnerwnerId, ccbFlags, ccbId, nodeId, conn);
}

SaAisErrorT immModel_classCreate(IMMND_CB* cb,
                                 const struct ImmsvOmClassDescr* req,
                                 SaUint32T reqConn, unsigned int nodeId,
                                 SaUint32T* continuationId, SaUint32T* pbeConn,
                                 unsigned int* pbeNodeId) {
  return ImmModel::instance(&cb->immModel)
      ->classCreate(req, reqConn, nodeId, continuationId, pbeConn, pbeNodeId);
}

SaAisErrorT immModel_classDelete(IMMND_CB* cb,
                                 const struct ImmsvOmClassDescr* req,
                                 SaUint32T reqConn, unsigned int nodeId,
                                 SaUint32T* continuationId, SaUint32T* pbeConn,
                                 unsigned int* pbeNodeId) {
  return ImmModel::instance(&cb->immModel)
      ->classDelete(req, reqConn, nodeId, continuationId, pbeConn, pbeNodeId);
}

SaAisErrorT immModel_ccbObjectCreate(
    IMMND_CB* cb, struct ImmsvOmCcbObjectCreate* req, SaUint32T* implConn,
    SaClmNodeIdT* implNodeId, SaUint32T* continuationId, SaUint32T* pbeConn,
    SaClmNodeIdT* pbeNodeId, SaNameT* objName, bool* dnOrRdnIsLong,
    bool isObjectDnUsed) {
  std::string objectName;

  if (req->attrValues && (req->attrValues->n.attrName.size == 0 ||
                          req->attrValues->n.attrValueType == 0)) {
    LOG_NO(
        "ERR_INVALID_PARAM: Attribute name or attribute type is not properly set");
    return SA_AIS_ERR_INVALID_PARAM;
  }

  /* Find RDN and parent DN */
  if (isObjectDnUsed) {
    std::string parentName;

    if (req->parentOrObjectDn.size == 0) {
      LOG_NO("ERR_INVALID_PARAM: Object DN cannot be empty string");
      return SA_AIS_ERR_INVALID_PARAM;
    }

    uint32_t n;
    /* start from second character. The first character cannot be comma */
    for (n = 1; n < req->parentOrObjectDn.size; n++) {
      if (req->parentOrObjectDn.buf[n] == ',' &&
          req->parentOrObjectDn.buf[n - 1] != '\\') {
        break;
      }
    }

    if (n + 1 < req->parentOrObjectDn.size) {
      parentName.append(req->parentOrObjectDn.buf + n + 1,
                        strnlen(req->parentOrObjectDn.buf + n + 1,
                                req->parentOrObjectDn.size - (n + 1)));
    }

    // Add RDN attribute to req->attrValues
    IMMSV_ATTR_VALUES_LIST* attr =
        (IMMSV_ATTR_VALUES_LIST*)calloc(1, sizeof(IMMSV_ATTR_VALUES_LIST));
    osafassert(attr);
    attr->n.attrName.buf = NULL;
    attr->n.attrName.size = 0;
    /* Temporary setting type to SA_IMM_ATTR_SASTRINGT, which will be later set
     * to the right type */
    attr->n.attrValueType = SA_IMM_ATTR_SASTRINGT;
    attr->n.attrValuesNumber = 1;
    attr->n.attrValue.val.x.buf = strndup(req->parentOrObjectDn.buf, n);
    attr->n.attrValue.val.x.size = strlen(attr->n.attrValue.val.x.buf) + 1;
    attr->next = req->attrValues;
    req->attrValues = attr;

    // Add parent to req
    req->parentOrObjectDn.size = parentName.size() + 1;
    memcpy(req->parentOrObjectDn.buf, parentName.c_str(),
           req->parentOrObjectDn.size);
  }

  SaAisErrorT err =
      ImmModel::instance(&cb->immModel)
          ->ccbObjectCreate(req, implConn, implNodeId, continuationId, pbeConn,
                            pbeNodeId, objectName, dnOrRdnIsLong,
                            isObjectDnUsed);

  if (err == SA_AIS_OK) {
    osaf_extended_name_alloc(objectName.c_str(), objName);
  }

  return err;
}

struct immsv_attr_values_list* immModel_specialApplierTrimCreate(
    IMMND_CB* cb, SaUint32T clientId, struct ImmsvOmCcbObjectCreate* req) {
  return ImmModel::instance(&cb->immModel)
      ->specialApplierTrimCreate(clientId, req);
}

void immModel_specialApplierSavePrtoCreateAttrs(
    IMMND_CB* cb, struct ImmsvOmCcbObjectCreate* req,
    SaUint32T continuationId) {
  return ImmModel::instance(&cb->immModel)
      ->specialApplierSavePrtoCreateAttrs(req, continuationId);
}

void immModel_specialApplierSaveRtUpdateAttrs(
    IMMND_CB* cb, struct ImmsvOmCcbObjectModify* req,
    SaUint32T continuationId) {
  return ImmModel::instance(&cb->immModel)
      ->specialApplierSaveRtUpdateAttrs(req, continuationId);
}

struct immsv_attr_mods_list* immModel_specialApplierTrimModify(
    IMMND_CB* cb, SaUint32T clientId, struct ImmsvOmCcbObjectModify* req) {
  return ImmModel::instance(&cb->immModel)
      ->specialApplierTrimModify(clientId, req);
}

bool immModel_isSpecialAndAddModify(IMMND_CB* cb, SaUint32T clientId,
                                    SaUint32T ccbId) {
  return ImmModel::instance(&cb->immModel)
      ->isSpecialAndAddModify(clientId, ccbId);
}

void immModel_genSpecialModify(IMMND_CB* cb,
                               struct ImmsvOmCcbObjectModify* req) {
  ImmModel::instance(&cb->immModel)->genSpecialModify(req);
}

struct immsv_attr_mods_list* immModel_canonicalizeAttrModification(
    IMMND_CB* cb, const struct ImmsvOmCcbObjectModify* req) {
  return ImmModel::instance(&cb->immModel)->canonicalizeAttrModification(req);
}

struct immsv_attr_mods_list* immModel_getAllWritableAttributes(
    IMMND_CB* cb, const ImmsvOmCcbObjectModify* req, bool* hasLongDn) {
  return ImmModel::instance(&cb->immModel)
      ->getAllWritableAttributes(req, hasLongDn);
}

SaUint32T immModel_getLocalAppliersForObj(IMMND_CB* cb, const SaNameT* objName,
                                          SaUint32T ccbId,
                                          SaUint32T** aplConnArr,
                                          bool externalRep) {
  ConnVector cv;
  ConnVector::iterator cvi;

  ImmModel::instance(&cb->immModel)
      ->getLocalAppliersForObj(objName, ccbId, cv, externalRep);

  SaUint32T arrSize = (SaUint32T)cv.size();

  if (arrSize) {
    unsigned int ix = 0;
    *aplConnArr = (SaUint32T*)malloc((arrSize) * sizeof(SaUint32T));

    for (cvi = cv.begin(); cvi != cv.end(); ++cvi, ++ix) {
      (*aplConnArr)[ix] = (*cvi);
    }
  }

  return arrSize;
}

SaUint32T immModel_getLocalAppliersForCcb(IMMND_CB* cb, SaUint32T ccbId,
                                          SaUint32T** aplConnArr,
                                          SaUint32T* applCtnPtr) {
  ConnVector cv;
  ConnVector::iterator cvi;

  ImmModel::instance(&cb->immModel)
      ->getLocalAppliersForCcb(ccbId, cv, applCtnPtr);

  SaUint32T arrSize = (SaUint32T)cv.size();

  if (arrSize) {
    unsigned int ix = 0;
    *aplConnArr = (SaUint32T*)malloc((arrSize) * sizeof(SaUint32T));

    for (cvi = cv.begin(); cvi != cv.end(); ++cvi, ++ix) {
      (*aplConnArr)[ix] = (*cvi);
    }
  }

  return arrSize;
}

SaUint32T immModel_getPbeApplierConn(IMMND_CB* cb) {
  return ImmModel::instance(&cb->immModel)->getPbeApplierConn();
}

SaAisErrorT immModel_ccbObjectModify(
    IMMND_CB* cb, const struct ImmsvOmCcbObjectModify* req, SaUint32T* implConn,
    SaClmNodeIdT* implNodeId, SaUint32T* continuationId, SaUint32T* pbeConn,
    SaClmNodeIdT* pbeNodeId, SaNameT* objName, bool* hasLongDns) {
  std::string objectName;
  bool pbeFile = (cb->mPbeFile != NULL);
  bool changeRim = false;
  SaAisErrorT err =
      ImmModel::instance(&cb->immModel)
          ->ccbObjectModify(req, implConn, implNodeId, continuationId, pbeConn,
                            pbeNodeId, objectName, hasLongDns, pbeFile,
                            &changeRim);

  if (err == SA_AIS_OK) {
    osaf_extended_name_alloc(objectName.c_str(), objName);
  }

  if (err == SA_AIS_OK && changeRim) {
    cb->mPbeDisableCcbId = req->ccbId;
    TRACE("The mPbeDisableCcbId is set to ccbid:%u", cb->mPbeDisableCcbId);
  }

  return err;
}

SaAisErrorT immModel_ccbObjectDelete(
    IMMND_CB* cb, const struct ImmsvOmCcbObjectDelete* req, SaUint32T reqConn,
    SaUint32T* arrSize, SaUint32T** implConnArr, SaUint32T** implIdArr,
    SaStringT** objNameArr, SaUint32T* pbeConn, SaClmNodeIdT* pbeNodeId,
    bool* augDelete, bool* hasLongDn)

{
  ConnVector cv;
  IdVector iv;
  ObjectNameVector ov;
  ConnVector::iterator cvi;
  IdVector::iterator ivi;
  ObjectNameVector::iterator oni;
  unsigned int ix = 0;

  SaAisErrorT err = ImmModel::instance(&cb->immModel)
                        ->ccbObjectDelete(req, reqConn, ov, cv, iv, pbeConn,
                                          pbeNodeId, augDelete, hasLongDn);
  *arrSize = (SaUint32T)cv.size();
  osafassert(*arrSize == iv.size());
  osafassert(*arrSize == ov.size());
  if ((err == SA_AIS_OK) && (*arrSize)) {
    *implConnArr = (SaUint32T*)malloc((*arrSize) * sizeof(SaUint32T));

    *implIdArr = (SaUint32T*)malloc((*arrSize) * sizeof(SaUint32T));

    *objNameArr = (SaStringT*)malloc((*arrSize) * sizeof(SaStringT));

    for (cvi = cv.begin(), ivi = iv.begin(), oni = ov.begin(); cvi != cv.end();
         ++cvi, ++ivi, ++oni, ++ix) {
      std::string delObjName = (*oni);
      (*implConnArr)[ix] = (*cvi);
      (*implIdArr)[ix] = (*ivi);
      (*objNameArr)[ix] = (SaStringT)malloc(delObjName.size() + 1);
      strncpy((*objNameArr)[ix], delObjName.c_str(), delObjName.size() + 1);
    }
  }
  osafassert(ix == (*arrSize));
  return err;
}

void immModel_getNonCriticalCcbs(IMMND_CB* cb, SaUint32T** ccbIdArr,
                                 SaUint32T* ccbIdArrSize) {
  IdVector ccbs;
  IdVector::iterator ix2;

  ImmModel::instance(&cb->immModel)->getNonCriticalCcbs(ccbs);
  *ccbIdArrSize = (SaUint32T)ccbs.size();
  if (*ccbIdArrSize) {
    unsigned int ix;
    *ccbIdArr = (SaUint32T*)malloc((*ccbIdArrSize) * sizeof(SaUint32T));

    for (ix2 = ccbs.begin(), ix = 0; ix2 != ccbs.end(); ++ix2, ++ix) {
      (*ccbIdArr)[ix] = (*ix2);
    }
    osafassert(ix == (*ccbIdArrSize));
  }
}

SaUint32T immModel_getIdForLargeAdmo(IMMND_CB* cb) {
  return ImmModel::instance(&cb->immModel)->getIdForLargeAdmo();
}

void immModel_getOldCriticalCcbs(IMMND_CB* cb, SaUint32T** ccbIdArr,
                                 SaUint32T* ccbIdArrSize, SaUint32T* pbeConn,
                                 unsigned int* pbeNodeId, SaUint32T* pbeId) {
  IdVector ccbs;
  IdVector::iterator ix2;

  if (ImmModel::instance(&cb->immModel)->getPbeOi(pbeConn, pbeNodeId, false)) {
    ImmModel::instance(&cb->immModel)
        ->getOldCriticalCcbs(ccbs, pbeConn, pbeNodeId, pbeId);
    *ccbIdArrSize = (SaUint32T)ccbs.size();
    if (*ccbIdArrSize) {
      unsigned int ix;
      *ccbIdArr = (SaUint32T*)malloc((*ccbIdArrSize) * sizeof(SaUint32T));

      for (ix2 = ccbs.begin(), ix = 0; ix2 != ccbs.end(); ++ix2, ++ix) {
        (*ccbIdArr)[ix] = (*ix2);
      }
      osafassert(ix == (*ccbIdArrSize));
    }
  }
}

void immmModel_getLocalImplementers(IMMND_CB* cb, SaUint32T* arrSize,
                                    SaUint32T** implIdArr,
                                    SaUint32T** implConnArr) {
  ConnVector cv;
  ConnVector::iterator cvi;
  IdVector idv;
  IdVector::iterator idv_it;
  unsigned int ix = 0;

  ImmModel::instance(&cb->immModel)->getLocalImplementers(cv, idv);
  if (cv.size()) {
    *arrSize = cv.size();
    *implIdArr = (SaUint32T*)malloc((*arrSize) * sizeof(SaUint32T));
    *implConnArr = (SaUint32T*)malloc((*arrSize) * sizeof(SaUint32T));
    for (cvi = cv.begin(), idv_it = idv.begin(); cvi != cv.end();
         ix++, cvi++, idv_it++) {
      (*implIdArr)[ix] = (*idv_it);
      (*implConnArr)[ix] = (*cvi);
    }
  }
}
unsigned int immModel_pbeOiExists(IMMND_CB* cb) {
  SaUint32T pbeConn = 0;
  unsigned int pbeNode = 0;
  if (ImmModel::instance(&cb->immModel)->getPbeOi(&pbeConn, &pbeNode, false)) {
    return pbeNode;
  }

  return 0;
}

unsigned int immModel_pbeBSlaveExists(IMMND_CB* cb) {
  SaUint32T pbeBConn = 0;
  unsigned int pbeBNode = 0;
  if (ImmModel::instance(&cb->immModel)
          ->getPbeBSlave(&pbeBConn, &pbeBNode, false)) {
    return pbeBNode;
  }

  return 0;
}

bool immModel_protocol41Allowed(IMMND_CB* cb) {
  return ImmModel::instance(&cb->immModel)->protocol41Allowed();
}

bool immModel_protocol43Allowed(IMMND_CB* cb) {
  return ImmModel::instance(&cb->immModel)->protocol43Allowed();
}

bool immModel_oneSafe2PBEAllowed(IMMND_CB* cb) {
  return ImmModel::instance(&cb->immModel)->oneSafe2PBEAllowed();
}

bool immModel_protocol45Allowed(IMMND_CB* cb) {
  return ImmModel::instance(&cb->immModel)->protocol45Allowed();
}

bool immModel_protocol46Allowed(IMMND_CB* cb) {
  return ImmModel::instance(&cb->immModel)->protocol46Allowed();
}

bool immModel_protocol47Allowed(IMMND_CB* cb) {
  return ImmModel::instance(&cb->immModel)->protocol47Allowed();
}

bool immModel_protocol50Allowed(IMMND_CB* cb) {
  return ImmModel::instance(&cb->immModel)->protocol50Allowed();
}

bool immModel_protocol51Allowed(IMMND_CB* cb) {
  return ImmModel::instance(&cb->immModel)->protocol51Allowed();
}

bool immModel_protocol51710Allowed(IMMND_CB* cb) {
  return ImmModel::instance(&cb->immModel)->protocol51710Allowed();
}

OsafImmAccessControlModeT immModel_accessControlMode(IMMND_CB* cb) {
  return ImmModel::instance(&cb->immModel)->accessControlMode();
}

const char* immModel_authorizedGroup(IMMND_CB* cb) {
  return ImmModel::instance(&cb->immModel)->authorizedGroup();
}

bool immModel_purgeSyncRequest(IMMND_CB* cb, SaUint32T clientId) {
  return ImmModel::instance(&cb->immModel)->purgeSyncRequest(clientId);
}

SaUint32T immModel_cleanTheBasement(
    IMMND_CB* cb, SaInvocationT** admReqArr, SaUint32T* admReqArrSize,
    SaInvocationT** searchReqArr, SaUint32T* searchReqArrSize,
    SaUint32T** ccbIdArr, SaUint32T* ccbIdArrSize, SaUint32T** pbePrtoReqArr,
    SaUint32T* pbePrtoReqArrSize, IMMSV_OCTET_STRING **applierArr,
    SaUint32T *applierArrSize, bool iAmCoordNow) {
  InvocVector admReqs;
  InvocVector searchReqs;
  InvocVector::iterator ix1;
  IdVector ccbs;
  IdVector pbePrtoReqs;
  ImplNameVector appliers;
  IdVector::iterator ix2;
  unsigned int ix;

  SaUint32T stuck = ImmModel::instance(&cb->immModel)
                        ->cleanTheBasement(admReqs, searchReqs, ccbs,
                                           pbePrtoReqs, appliers,
                                           iAmCoordNow);

  *admReqArrSize = (SaUint32T)admReqs.size();
  if (*admReqArrSize) {
    *admReqArr =
        (SaInvocationT*)malloc((*admReqArrSize) * sizeof(SaInvocationT));
    for (ix1 = admReqs.begin(), ix = 0; ix1 != admReqs.end(); ++ix1, ++ix) {
      (*admReqArr)[ix] = (*ix1);
    }
    osafassert(ix == (*admReqArrSize));
  }

  *searchReqArrSize = (SaUint32T)searchReqs.size();
  if (*searchReqArrSize) {
    *searchReqArr =
        (SaInvocationT*)malloc((*searchReqArrSize) * sizeof(SaInvocationT));
    for (ix1 = searchReqs.begin(), ix = 0; ix1 != searchReqs.end();
         ++ix1, ++ix) {
      (*searchReqArr)[ix] = (*ix1);
    }
    osafassert(ix == (*searchReqArrSize));
  }

  *ccbIdArrSize = (SaUint32T)ccbs.size();
  if (*ccbIdArrSize) {
    *ccbIdArr = (SaUint32T*)malloc((*ccbIdArrSize) * sizeof(SaUint32T));

    for (ix2 = ccbs.begin(), ix = 0; ix2 != ccbs.end(); ++ix2, ++ix) {
      (*ccbIdArr)[ix] = (*ix2);
    }
    osafassert(ix == (*ccbIdArrSize));
  }

  *pbePrtoReqArrSize = (SaUint32T)pbePrtoReqs.size();
  if (*pbePrtoReqArrSize) {
    *pbePrtoReqArr =
        (SaUint32T*)malloc((*pbePrtoReqArrSize) * sizeof(SaUint32T));

    for (ix2 = pbePrtoReqs.begin(), ix = 0; ix2 != pbePrtoReqs.end();
         ++ix2, ++ix) {
      (*pbePrtoReqArr)[ix] = (*ix2);
    }
    osafassert(ix == (*pbePrtoReqArrSize));
  }

  timespec now;
  osaf_clock_gettime(CLOCK_MONOTONIC, &now);
  timespec nextSearch;
  timespec opSearchTime;
  ImmSearchOp* op;
  IMMND_OM_SEARCH_NODE* searchOp;
  IMMND_OM_SEARCH_NODE** prevSearchOp;
  IMMND_IMM_CLIENT_NODE* cl_node =
      (IMMND_IMM_CLIENT_NODE*)ncs_patricia_tree_getnext(&cb->client_info_db,
                                                        NULL);
  int clearCounter = 0;
  while (cl_node) {
    if (!cl_node->mIsSync && cl_node->searchOpList &&
        osaf_timer_is_expired_sec(&now, &cl_node->mLastSearch,
                                  SEARCH_TIMEOUT_SEC)) {
      nextSearch = now;
      clearCounter = 0;
      searchOp = cl_node->searchOpList;
      prevSearchOp = &cl_node->searchOpList;
      while (searchOp) {
        osafassert(searchOp->searchOp);
        op = (ImmSearchOp*)searchOp->searchOp;
        opSearchTime = op->getLastSearchTime();
        if (!op->isSync() && osaf_timer_is_expired_sec(&now, &opSearchTime,
                                                       SEARCH_TIMEOUT_SEC)) {
          TRACE_2(
              "Clear search result. Timeout %dsec. Search id: %d, OM handle: %llx",
              SEARCH_TIMEOUT_SEC, searchOp->searchId, cl_node->imm_app_hdl);
          *prevSearchOp = searchOp->next;
          immModel_deleteSearchOp(op);
          free(searchOp);
          searchOp = *prevSearchOp;
          clearCounter++;
        } else {
          if (osaf_timespec_compare(&opSearchTime, &nextSearch) == -1) {
            nextSearch = opSearchTime;
          }
          prevSearchOp = &searchOp->next;
          searchOp = searchOp->next;
        }
      }

      cl_node->mLastSearch = nextSearch;

      if (clearCounter) {
        LOG_NO(
            "Clear %d search result(s) for OM handle %llx. Search timeout %dsec",
            clearCounter, cl_node->imm_app_hdl, SEARCH_TIMEOUT_SEC);
      }
    }

    cl_node = (IMMND_IMM_CLIENT_NODE*)ncs_patricia_tree_getnext(
        &cb->client_info_db, cl_node->patnode.key_info);
  }

  if(appliers.size() > 0) {
    *applierArrSize = appliers.size();
    *applierArr = (IMMSV_OCTET_STRING *)malloc(
            appliers.size() * sizeof(IMMSV_OCTET_STRING));
    for(size_t i=0; i<appliers.size(); ++i) {
      (*applierArr)[i].buf = strdup(appliers[i].c_str());
      (*applierArr)[i].size = strlen((*applierArr)[i].buf);
    }
  }

  return stuck;
}

SaAisErrorT immModel_ccbApply(IMMND_CB* cb, SaUint32T ccbId, SaUint32T reqConn,
                              SaUint32T* arrSize, SaUint32T** implConnArr,
                              SaUint32T** implIdArr, SaUint32T** ctnArr,
                              bool validateOnly) {
  ConnVector cv;
  IdVector implsv;
  IdVector ctnv;
  ConnVector::iterator cvi;
  IdVector::iterator ivi1;
  IdVector::iterator ivi2;
  unsigned int ix = 0;

  SaAisErrorT err =
      ImmModel::instance(&cb->immModel)
          ->ccbApply(ccbId, reqConn, cv, implsv, ctnv, validateOnly);

  *arrSize = (SaUint32T)cv.size();
  if (*arrSize) {
    *implConnArr = (SaUint32T*)malloc((*arrSize) * sizeof(SaUint32T));

    *implIdArr = (SaUint32T*)malloc((*arrSize) * sizeof(SaUint32T));

    *ctnArr = (SaUint32T*)malloc((*arrSize) * sizeof(SaUint32T));

    for (cvi = cv.begin(), ivi1 = implsv.begin(), ivi2 = ctnv.begin();
         cvi != cv.end(); ++cvi, ++ivi1, ++ivi2, ++ix) {
      (*implConnArr)[ix] = (*cvi);
      (*implIdArr)[ix] = (*ivi1);
      (*ctnArr)[ix] = (*ivi2);
    }
  }
  osafassert(ix == (*arrSize));
  return err;
}

bool immModel_ccbAbort(IMMND_CB* cb, SaUint32T ccbId, SaUint32T* arrSize,
                       SaUint32T** implConnArr, SaUint32T** clientArr,
                       SaUint32T* clArrSize, SaUint32T* nodeId,
                       SaClmNodeIdT* pbeNodeId) {
  ConnVector cv, cl;
  ConnVector::iterator cvi, cli;
  unsigned int ix = 0;

  bool aborted = ImmModel::instance(&cb->immModel)
                     ->ccbAbort(ccbId, cv, cl, nodeId, pbeNodeId);

  if (aborted && (cb->mPbeDisableCcbId == ccbId)) {
    TRACE("PbeDisableCcbId %u has been aborted", ccbId);
    cb->mPbeDisableCcbId = 0;
    cb->mPbeDisableCritical = false;
  }

  *arrSize = (SaUint32T)cv.size();
  if (*arrSize) {
    *implConnArr = (SaUint32T*)malloc((*arrSize) * sizeof(SaUint32T));

    for (cvi = cv.begin(); cvi != cv.end(); ++cvi, ++ix) {
      (*implConnArr)[ix] = (*cvi);
    }
  }
  osafassert(ix == (*arrSize));

  ix = 0;
  *clArrSize = (SaUint32T)cl.size();
  if (*clArrSize) {
    *clientArr = (SaUint32T*)malloc((*clArrSize) * sizeof(SaUint32T));
    for (cli = cl.begin(); cli != cl.end(); ++cli, ++ix) {
      (*clientArr)[ix] = (*cli);
    }
  }
  osafassert(ix == (*clArrSize));
  return aborted;
}

void immModel_getCcbIdsForOrigCon(IMMND_CB* cb, SaUint32T deadCon,
                                  SaUint32T* arrSize, SaUint32T** ccbIdArr) {
  ConnVector cv;
  ConnVector::iterator cvi;
  unsigned int ix = 0;
  ImmModel::instance(&cb->immModel)->getCcbIdsForOrigCon(deadCon, cv);
  *arrSize = (SaUint32T)cv.size();
  if (*arrSize) {
    *ccbIdArr = (SaUint32T*)malloc((*arrSize) * sizeof(SaUint32T));
    for (cvi = cv.begin(); cvi != cv.end(); ++cvi, ++ix) {
      (*ccbIdArr)[ix] = (*cvi);
    }
  }
  osafassert(ix == (*arrSize));
}

void immModel_discardNode(IMMND_CB* cb, SaUint32T nodeId, SaUint32T* arrSize,
                          SaUint32T** ccbIdArr, SaUint32T* globArrSize,
                          SaUint32T** globccbIdArr) {
  ConnVector cv, gv;
  ConnVector::iterator cvi, gvi;
  unsigned int ix = 0;
  ImmModel::instance(&cb->immModel)
      ->discardNode(nodeId, cv, gv, cb->mIsCoord, false);
  *arrSize = (SaUint32T)cv.size();
  if (*arrSize) {
    *ccbIdArr = (SaUint32T*)malloc((*arrSize) * sizeof(SaUint32T));
    for (cvi = cv.begin(); cvi != cv.end(); ++cvi, ++ix) {
      (*ccbIdArr)[ix] = (*cvi);
    }
  }
  osafassert(ix == (*arrSize));

  *globArrSize = (SaUint32T)gv.size();
  ix = 0;
  if (*globArrSize) {
    *globccbIdArr = (SaUint32T*)malloc((*globArrSize) * sizeof(SaUint32T));
    for (gvi = gv.begin(); gvi != gv.end(); ++gvi, ++ix) {
      (*globccbIdArr)[ix] = (*gvi);
    }
  }
  osafassert(ix == (*globArrSize));
}

void immModel_getAdminOwnerIdsForCon(IMMND_CB* cb, SaUint32T deadCon,
                                     SaUint32T* arrSize,
                                     SaUint32T** admoIdArr) {
  ConnVector cv;
  ConnVector::iterator cvi;
  unsigned int ix = 0;
  ImmModel::instance(&cb->immModel)->getAdminOwnerIdsForCon(deadCon, cv);
  *arrSize = (SaUint32T)cv.size();
  if (*arrSize) {
    *admoIdArr = (SaUint32T*)malloc((*arrSize) * sizeof(SaUint32T));
    for (cvi = cv.begin(); cvi != cv.end(); ++cvi, ++ix) {
      (*admoIdArr)[ix] = (*cvi);
    }
  }
  osafassert(ix == (*arrSize));
}

SaAisErrorT immModel_adminOperationInvoke(
    IMMND_CB* cb, const struct ImmsvOmAdminOperationInvoke* req,
    SaUint32T reqConn, SaUint64T reply_dest, SaInvocationT inv,
    SaUint32T* implConn, SaClmNodeIdT* implNodeId, bool pbeExpected,
    bool* displayRes) {
  bool wasAbortNonCritical = sAbortNonCriticalCcbs;
  SaAisErrorT err = ImmModel::instance(&cb->immModel)
                        ->adminOperationInvoke(
                            req, reqConn, reply_dest, inv, implConn, implNodeId,
                            pbeExpected, displayRes, cb->mIsCoord);

  if (sAbortNonCriticalCcbs && !wasAbortNonCritical) {
    TRACE("cb->mForceClean set to true");
    cb->mForceClean = true;
  }
  return err;
}

SaUint32T /* Returns admo-id for object if object exists and active admo exists,
             otherwise zero. */
    immModel_getAdmoIdForObj(IMMND_CB* cb, const char* opensafImmObj) {
  TRACE_ENTER();
  SaUint32T admoId = 0;
  std::string objectName(opensafImmObj);
  std::string admOwner;

  AdminOwnerVector::iterator i = sOwnerVector.begin();
  ObjectMap::iterator oi = sObjectMap.find(objectName);
  if (oi == sObjectMap.end()) {
    TRACE("immModel_getAdmoIdForObj: Could not find %s", opensafImmObj);
    goto done;
  }

  oi->second->getAdminOwnerName(&admOwner);
  for (; i != sOwnerVector.end(); ++i) {
    if (!(*i)->mDying && (*i)->mAdminOwnerName == admOwner) {
      admoId = (*i)->mId;
      break;
    }
  }

done:
  TRACE_LEAVE();
  return admoId;
}

SaUint32T immModel_findConnForImplementerOfObject(
    IMMND_CB* cb, IMMSV_OCTET_STRING* objectName) {
  size_t sz = strnlen((char*)objectName->buf, (size_t)objectName->size);

  std::string objectDn((const char*)objectName->buf, sz);

  return ImmModel::instance(&cb->immModel)
      ->findConnForImplementerOfObject(objectDn);
}

bool immModel_ccbWaitForCompletedAck(IMMND_CB* cb, SaUint32T ccbId,
                                     SaAisErrorT* err, SaUint32T* pbeConn,
                                     SaClmNodeIdT* pbeNodeId, SaUint32T* pbeId,
                                     SaUint32T* pbeCtn) {
  return ImmModel::instance(&cb->immModel)
      ->ccbWaitForCompletedAck(ccbId, err, pbeConn, pbeNodeId, pbeId, pbeCtn,
                               cb->mPbeDisableCritical);
}

bool immModel_ccbWaitForDeleteImplAck(IMMND_CB* cb, SaUint32T ccbId,
                                      SaAisErrorT* err, bool augDelete) {
  return ImmModel::instance(&cb->immModel)
      ->ccbWaitForDeleteImplAck(ccbId, err, augDelete);
}

bool immModel_ccbCommit(IMMND_CB* cb, SaUint32T ccbId, SaUint32T* arrSize,
                        SaUint32T** implConnArr) {
  ConnVector cv;
  ConnVector::iterator cvi;
  unsigned int ix = 0;

  bool pbeModeChange = ImmModel::instance(&cb->immModel)->ccbCommit(ccbId, cv);

  *arrSize = (SaUint32T)cv.size();
  if (*arrSize) {
    *implConnArr = (SaUint32T*)malloc((*arrSize) * sizeof(SaUint32T));

    for (cvi = cv.begin(); cvi != cv.end(); ++cvi, ++ix) {
      (*implConnArr)[ix] = (*cvi);
    }
  }

  osafassert(ix == (*arrSize));
  return pbeModeChange;
}

SaAisErrorT immModel_ccbFinalize(IMMND_CB* cb, SaUint32T ccbId) {
  return ImmModel::instance(&cb->immModel)->ccbTerminate(ccbId);
}

SaAisErrorT immModel_ccbAugmentInit(IMMND_CB* cb,
                                    IMMSV_OI_CCB_UPCALL_RSP* ccbUpcallRsp,
                                    SaUint32T originatingNode,
                                    SaUint32T originatingConn,
                                    SaUint32T* adminOwnerId) {
  return ImmModel::instance(&cb->immModel)
      ->ccbAugmentInit(ccbUpcallRsp, originatingNode, originatingConn,
                       adminOwnerId);
}

void immModel_ccbAugmentAdmo(IMMND_CB* cb, SaUint32T adminOwnerId,
                             SaUint32T ccbId) {
  ImmModel::instance(&cb->immModel)->ccbAugmentAdmo(adminOwnerId, ccbId);
}

SaAisErrorT immModel_objectIsLockedByCcb(IMMND_CB* cb,
                                         struct ImmsvOmSearchInit* req) {
  return ImmModel::instance(&cb->immModel)->objectIsLockedByCcb(req);
}

SaAisErrorT immModel_ccbReadLockObject(IMMND_CB* cb,
                                       struct ImmsvOmSearchInit* req) {
  return ImmModel::instance(&cb->immModel)->ccbReadLockObject(req);
}

SaAisErrorT immModel_searchInitialize(IMMND_CB* cb,
                                      struct ImmsvOmSearchInit* req,
                                      void** searchOp, bool isSync,
                                      bool isAccessor) {
  SaAisErrorT err = SA_AIS_OK;
  ImmSearchOp* op = new ImmSearchOp();
  *searchOp = op;

  if (isSync) {
    op->setIsSync();
  }
  if (isAccessor) {
    op->setIsAccessor();
  }

  if (isAccessor) {
    TRACE("Allocating accessor searchOp:%p", op);
  } else {
    TRACE("Allocating iterator searchOp:%p", op);
  }

  /* Reset search time */
  op->updateSearchTime();

  if (isAccessor) {
    err = ImmModel::instance(&cb->immModel)->accessorGet(req, *op);
  } else {
    err = ImmModel::instance(&cb->immModel)->searchInitialize(req, *op);
  }

  return err;
}

SaAisErrorT immModel_testTopResult(void* searchOp, SaUint32T* implNodeId,
                                   bool* bRtAttrsToFetch) {
  SaAisErrorT err;
  ImplementerInfo* implInfo = NULL;
  ImmSearchOp* op = (ImmSearchOp*)searchOp;

  err = op->testTopResult((void**)&implInfo, bRtAttrsToFetch);
  *implNodeId = (implInfo) ? implInfo->mNodeId : 0;

  return err;
}

SaAisErrorT immModel_nextResult(IMMND_CB* cb, void* searchOp,
                                IMMSV_OM_RSP_SEARCH_NEXT** rsp,
                                SaUint32T* implConn, SaUint32T* implNodeId,
                                struct ImmsvAttrNameList** rtAttrsToFetch,
                                MDS_DEST* implDest, bool retardSync,
                                SaUint32T* implTimeout) {
  AttributeList* rtAttrs = NULL;
  SaAisErrorT err = SA_AIS_OK;
  osafassert(searchOp && rsp);
  if (rtAttrsToFetch) {
    osafassert(implConn && implNodeId && implDest);
  }
  ImmSearchOp* op = (ImmSearchOp*)searchOp;

  if (op->isSync()) {
    /* Special handling for sync. Because immsv_sync (sync object send)
       is asyncronous, it can not be throttled directly. Insead we throttle the
       sync object send by retarding the iterator used in syncing.
       In addition, the nextResult operation is handled differently for sync.
    */
    if (retardSync) {
      TRACE_2(
          "ERR_TRY_AGAIN: Too many pending incoming fevs "
          "messages (> %u) rejecting sync iteration next request",
          IMMSV_DEFAULT_FEVS_MAX_PENDING);
      return SA_AIS_ERR_TRY_AGAIN;
    }
    err = ImmModel::instance(&cb->immModel)->nextSyncResult(rsp, *op);
  } else {
    ImplementerInfo* implInfo = NULL;
    /* Reset search time */
    op->updateSearchTime();

    err = op->nextResult(rsp, (void**)&implInfo,
                         (rtAttrsToFetch) ? (&rtAttrs) : NULL);

    if (err == SA_AIS_OK) {
      if (implInfo) {
        if (implConn) {
          *implConn = implInfo->mConn;
        }
        if (implNodeId) {
          *implNodeId = implInfo->mNodeId;
        }
        if (implDest) {
          *implDest = implInfo->mMds_dest;
        }
        if (implTimeout) {
          *implTimeout = implInfo->mTimeout;
        }
      } else {
        if (implConn) {
          *implConn = 0;
        }
        if (implNodeId) {
          *implNodeId = 0;
        }
        if (implDest) {
          *implDest = 0;
        }
        if (implTimeout) {
          *implTimeout = 0;
        }
      }
      /* If it doesn't involve any OI, we can pop out the object.
         There are 3 cases:
         1. There are no pure rt attributes
         2. There are pure rt attributes to fetch but OI detached
         (*implNodeId==0) 3. nextResult() is called by
         immnd_evt_proc_oi_att_pull_rpl() to get the updated values for pure rt
         attributes. In this case, rtAttrsToFetch==NULL
       */
      if (!rtAttrsToFetch || /* Case 3 */
          !(*implNodeId)) {  /* Case 1 & 2 */
        op->popLastResult();
      }
    }
  }

  if (err != SA_AIS_OK) {
    return err;
  }

  if (rtAttrs && (*implNodeId)) {
    AttributeList::iterator ai;
    for (ai = rtAttrs->begin(); ai != rtAttrs->end(); ++ai) {
      ImmsvAttrNameList* p =
          (ImmsvAttrNameList*)malloc(sizeof(ImmsvAttrNameList)); /*alloc-1*/
      p->name.size = (SaUint32T)(*ai).name.length() + 1;
      p->name.buf = (char*)malloc(p->name.size); /*alloc-2*/
      strncpy(p->name.buf, (*ai).name.c_str(), p->name.size);

      p->next = *rtAttrsToFetch;
      *rtAttrsToFetch = p;
    }
  }
  return err;
}

void immModel_deleteSearchOp(void* searchOp) {
  ImmSearchOp* op = (ImmSearchOp*)searchOp;
  if (op) {
    if (op->isSync()) {
      ObjectSet::iterator* osip = (ObjectSet::iterator*)op->syncOsi;
      ImmsvAttrNameList* theAttList = (ImmsvAttrNameList*)op->attrNameList;
      if (osip) {
        delete osip;
        op->syncOsi = NULL;
        immsv_evt_free_attrNames(theAttList);
        op->attrNameList = NULL;
        op->classInfo = NULL;
      }
    }
    if (op->isAccessor()) {
      TRACE("Deleting accessor searchOp %p", op);
    } else {
      TRACE("Deleting iterator searchOp %p", op);
    }
    delete op;
  }
}

void immModel_fetchLastResult(void* searchOp, IMMSV_OM_RSP_SEARCH_NEXT** rsp) {
  ImmSearchOp* op = (ImmSearchOp*)searchOp;
  *rsp = op->fetchLastResult();
}

void immModel_popLastResult(void* searchOp) {
  ImmSearchOp* op = (ImmSearchOp*)searchOp;
  op->popLastResult();
}

void immModel_clearLastResult(void* searchOp) {
  ImmSearchOp* op = (ImmSearchOp*)searchOp;
  op->clearLastResult();
}

bool immModel_isSearchOpAccessor(void* searchOp) {
  ImmSearchOp* op = (ImmSearchOp*)searchOp;
  return op->isAccessor();
}

bool immModel_isSearchOpNonExtendedNameSet(void* searchOp) {
  ImmSearchOp* op = (ImmSearchOp*)searchOp;
  return op->isNonExtendedNameSet();
}

void immModel_setAdmReqContinuation(IMMND_CB* cb, SaInvocationT invoc,
                                    SaUint32T reqConn) {
  ImmModel::instance(&cb->immModel)->setAdmReqContinuation(invoc, reqConn);
}

void immModel_setSearchReqContinuation(IMMND_CB* cb, SaInvocationT invoc,
                                       SaUint32T reqConn,
                                       SaUint32T implTimeout) {
  ImmModel::instance(&cb->immModel)
      ->setSearchReqContinuation(invoc, reqConn, implTimeout);
}

void immModel_setSearchImplContinuation(IMMND_CB* cb, SaUint32T searchId,
                                        SaUint32T reqNodeId,
                                        MDS_DEST reply_dest) {
  SaInvocationT tmp_hdl = m_IMMSV_PACK_HANDLE(searchId, reqNodeId);
  ImmModel::instance(&cb->immModel)
      ->setSearchImplContinuation(tmp_hdl, (SaUint64T)reply_dest);
}

SaAisErrorT immModel_implementerSet(IMMND_CB* cb,
                                    const IMMSV_OCTET_STRING* implName,
                                    SaUint32T implConn, SaUint32T implNodeId,
                                    SaUint32T implId, MDS_DEST mds_dest,
                                    SaUint32T implTimeout,
                                    bool* discardImplementer) {
  return ImmModel::instance(&cb->immModel)
      ->implementerSet(implName, implConn, implNodeId, implId,
                       (SaUint64T)mds_dest, implTimeout, discardImplementer);
}

SaAisErrorT immModel_implIsFree(IMMND_CB* cb, const IMMSV_OI_IMPLSET_REQ* req,
                                SaUint32T* impl_id) {
  *impl_id = 0;
  SaImmOiImplementerNameT impName = req->impl_name.buf;
  std::string implName(impName);
  if (implName.empty()) {
    LOG_NO("ERR_INVALID_PARAM: Empty implementer name");
    return SA_AIS_ERR_INVALID_PARAM;
  }

  ImplementerInfo* impl =
      ImmModel::instance(&cb->immModel)->findImplementer(implName);

  if (!impl) {
    return SA_AIS_OK;
  }

  if (impl->mId == 0) {
    return SA_AIS_OK;
  }
  if (impl->mDying) {
    return SA_AIS_ERR_TRY_AGAIN;
  }

  /* Check for redundant request that comes from previously timed out client */
  if (impl->mConn == m_IMMSV_UNPACK_HANDLE_HIGH(req->client_hdl) &&
      impl->mNodeId == m_IMMSV_UNPACK_HANDLE_LOW(req->client_hdl)) {
    *impl_id = impl->mId;
  }

  return SA_AIS_ERR_EXIST;
}

SaUint32T immModel_getImplementerId(IMMND_CB* cb, SaUint32T deadConn) {
  return ImmModel::instance(&cb->immModel)->getImplementerId(deadConn);
}

void immModel_discardImplementer(IMMND_CB* cb, SaUint32T implId,
                                 bool reallyDiscard, SaUint32T* globArrSize,
                                 SaUint32T** globccbIdArr) {
  ConnVector gv;
  ConnVector::iterator gvi;
  ImmModel::instance(&cb->immModel)
      ->discardImplementer(implId, reallyDiscard, gv, cb->mIsCoord);

  if (globArrSize && globccbIdArr) {
    *globArrSize = (SaUint32T)gv.size();
    unsigned int ix = 0;
    if (*globArrSize) {
      *globccbIdArr = (SaUint32T*)malloc((*globArrSize) * sizeof(SaUint32T));
      for (gvi = gv.begin(); gvi != gv.end(); ++gvi, ++ix) {
        (*globccbIdArr)[ix] = (*gvi);
      }
    }
    osafassert(ix == (*globArrSize));
  }
}

SaAisErrorT immModel_implementerClear(IMMND_CB* cb,
                                      const struct ImmsvOiImplSetReq* req,
                                      SaUint32T implConn, SaUint32T implNodeId,
                                      SaUint32T* globArrSize,
                                      SaUint32T** globccbIdArr) {
  SaAisErrorT err;
  ConnVector gv;
  ConnVector::iterator gvi;
  err = ImmModel::instance(&cb->immModel)
            ->implementerClear(req, implConn, implNodeId, gv, cb->mIsCoord);

  if (globArrSize && globccbIdArr) {
    *globArrSize = (SaUint32T)gv.size();
    SaUint32T ix = 0;
    if (*globArrSize) {
      *globccbIdArr = (SaUint32T*)malloc((*globArrSize) * sizeof(SaUint32T));
      for (gvi = gv.begin(); gvi != gv.end(); ++gvi, ++ix) {
        (*globccbIdArr)[ix] = (*gvi);
      }
    }
    osafassert(ix == (*globArrSize));
  }

  return err;
}

SaAisErrorT immModel_classImplementerSet(IMMND_CB* cb,
                                         const struct ImmsvOiImplSetReq* req,
                                         SaUint32T implConn,
                                         SaUint32T implNodeId,
                                         SaUint32T* ccbId) {
  return ImmModel::instance(&cb->immModel)
      ->classImplementerSet(req, implConn, implNodeId, ccbId);
}

SaAisErrorT immModel_classImplementerRelease(
    IMMND_CB* cb, const struct ImmsvOiImplSetReq* req, SaUint32T implConn,
    SaUint32T implNodeId) {
  return ImmModel::instance(&cb->immModel)
      ->classImplementerRelease(req, implConn, implNodeId);
}

SaAisErrorT immModel_objectImplementerSet(IMMND_CB* cb,
                                          const struct ImmsvOiImplSetReq* req,
                                          SaUint32T implConn,
                                          SaUint32T implNodeId,
                                          SaUint32T* ccbId) {
  return ImmModel::instance(&cb->immModel)
      ->objectImplementerSet(req, implConn, implNodeId, ccbId);
}

SaAisErrorT immModel_objectImplementerRelease(
    IMMND_CB* cb, const struct ImmsvOiImplSetReq* req, SaUint32T implConn,
    SaUint32T implNodeId) {
  return ImmModel::instance(&cb->immModel)
      ->objectImplementerRelease(req, implConn, implNodeId);
}

void immModel_ccbCompletedContinuation(IMMND_CB* cb,
                                       struct immsv_oi_ccb_upcall_rsp* rsp,
                                       SaUint32T* reqConn) {
  ImmModel::instance(&cb->immModel)->ccbCompletedContinuation(rsp, reqConn);
}

void immModel_ccbObjDelContinuation(IMMND_CB* cb,
                                    struct immsv_oi_ccb_upcall_rsp* rsp,
                                    SaUint32T* reqConn, bool* augDelete) {
  ImmModel::instance(&cb->immModel)
      ->ccbObjDelContinuation(rsp, reqConn, augDelete);
}

void immModel_ccbObjCreateContinuation(IMMND_CB* cb, SaUint32T ccbId,
                                       SaUint32T invocation, SaAisErrorT error,
                                       SaUint32T* reqConn) {
  ImmModel::instance(&cb->immModel)
      ->ccbObjCreateContinuation(ccbId, invocation, error, reqConn);
}

void immModel_pbePrtObjCreateContinuation(IMMND_CB* cb, SaUint32T invocation,
                                          SaAisErrorT err, SaClmNodeIdT nodeId,
                                          SaUint32T* reqConn,
                                          SaUint32T* spApplConn,
                                          struct ImmsvOmCcbObjectCreate* req) {
  ImmModel::instance(&cb->immModel)
      ->pbePrtObjCreateContinuation(invocation, err, nodeId, reqConn,
                                    spApplConn, req);
}

void immModel_pbeClassCreateContinuation(IMMND_CB* cb, SaUint32T invocation,
                                         SaClmNodeIdT nodeId,
                                         SaUint32T* reqConn) {
  ImmModel::instance(&cb->immModel)
      ->pbeClassCreateContinuation(invocation, nodeId, reqConn);
}

void immModel_pbeClassDeleteContinuation(IMMND_CB* cb, SaUint32T invocation,
                                         SaClmNodeIdT nodeId,
                                         SaUint32T* reqConn) {
  ImmModel::instance(&cb->immModel)
      ->pbeClassDeleteContinuation(invocation, nodeId, reqConn);
}

void immModel_pbeUpdateEpochContinuation(IMMND_CB* cb, SaUint32T invocation,
                                         SaClmNodeIdT nodeId) {
  ImmModel::instance(&cb->immModel)
      ->pbeUpdateEpochContinuation(invocation, nodeId);
}

int immModel_pbePrtObjDeletesContinuation(
    IMMND_CB* cb, SaUint32T invocation, SaAisErrorT err, SaClmNodeIdT nodeId,
    SaUint32T* reqConn, SaStringT** objNameArr, SaUint32T* arrSizePtr,
    SaUint32T* spApplConn, SaUint32T* pbe2BConn) {
  ObjectNameVector ov;
  ObjectNameVector::iterator oni;
  int numOps = 0;
  osafassert(arrSizePtr);

  numOps = ImmModel::instance(&cb->immModel)
               ->pbePrtObjDeletesContinuation(invocation, err, nodeId, reqConn,
                                              ov, spApplConn, pbe2BConn);

  (*arrSizePtr) = (SaUint32T)ov.size();
  if (*arrSizePtr) {
    unsigned int ix = 0;
    *objNameArr = (SaStringT*)malloc((*arrSizePtr) * sizeof(SaStringT));

    for (oni = ov.begin(); oni != ov.end(); ++oni, ++ix) {
      std::string delObjName = (*oni);
      (*objNameArr)[ix] = (SaStringT)strdup(delObjName.c_str());
    }
    osafassert(ix == (*arrSizePtr));
  }
  return numOps;
}

void immModel_pbePrtAttrUpdateContinuation(IMMND_CB* cb, SaUint32T invocation,
                                           SaAisErrorT err, SaClmNodeIdT nodeId,
                                           SaUint32T* reqConn,
                                           SaUint32T* spApplConn,
                                           struct ImmsvOmCcbObjectModify* req) {
  ImmModel::instance(&cb->immModel)
      ->pbePrtAttrUpdateContinuation(invocation, err, nodeId, reqConn,
                                     spApplConn, req);
}

void immModel_ccbObjModifyContinuation(IMMND_CB* cb, SaUint32T ccbId,
                                       SaUint32T invocation, SaAisErrorT error,
                                       SaUint32T* reqConn) {
  ImmModel::instance(&cb->immModel)
      ->ccbObjModifyContinuation(ccbId, invocation, error, reqConn);
}

SaAisErrorT immModel_classDescriptionGet(IMMND_CB* cb,
                                         const IMMSV_OCTET_STRING* clName,
                                         struct ImmsvOmClassDescr* res) {
  return ImmModel::instance(&cb->immModel)->classDescriptionGet(clName, res);
}

void immModel_fetchAdmOpContinuations(IMMND_CB* cb, SaInvocationT inv,
                                      bool local, SaUint32T* implConn,
                                      SaUint32T* reqConn,
                                      SaUint64T* reply_dest) {
  if (local) {
    ImmModel::instance(&cb->immModel)
        ->fetchAdmImplContinuation(inv, implConn, reply_dest);
  }

  ImmModel::instance(&cb->immModel)->fetchAdmReqContinuation(inv, reqConn);
}

void immModel_fetchSearchReqContinuation(IMMND_CB* cb, SaInvocationT inv,
                                         SaUint32T* reqConn) {
  ImmModel::instance(&cb->immModel)->fetchSearchReqContinuation(inv, reqConn);
}

void immModel_fetchSearchImplContinuation(IMMND_CB* cb, SaUint32T searchId,
                                          SaUint32T reqNodeId,
                                          MDS_DEST* reply_dest) {
  SaInvocationT tmp_hdl = m_IMMSV_PACK_HANDLE(searchId, reqNodeId);
  ImmModel::instance(&cb->immModel)
      ->fetchSearchImplContinuation(tmp_hdl, (SaUint64T*)reply_dest);
}

void immModel_prepareForLoading(IMMND_CB* cb) {
  ImmModel::instance(&cb->immModel)->prepareForLoading();
}

bool immModel_readyForLoading(IMMND_CB* cb) {
  return ImmModel::instance(&cb->immModel)->readyForLoading();
}

SaInt32T immModel_getLoader(IMMND_CB* cb) {
  return ImmModel::instance(&cb->immModel)->getLoader();
}

void immModel_setLoader(IMMND_CB* cb, SaInt32T loaderPid) {
  ImmModel::instance(&cb->immModel)->setLoader(loaderPid);
}

void immModel_recognizedIsolated(IMMND_CB* cb) {
  ImmModel::instance(&cb->immModel)->recognizedIsolated();
}

bool immModel_syncComplete(IMMND_CB* cb) {
  return ImmModel::instance(&cb->immModel)->syncComplete(false);
}

bool immModel_ccbsTerminated(IMMND_CB* cb, bool allowEmpty) {
  return ImmModel::instance(&cb->immModel)->ccbsTerminated(allowEmpty);
}

bool immModel_immNotWritable(IMMND_CB* cb) {
  if (cb->preLoadPid > 0) {
    TRACE("immModel_immNotWritable: WRITABLE in preload");
    return false;
  }
  return ImmModel::instance(&cb->immModel)->immNotWritable();
}

bool immModel_pbeNotWritable(IMMND_CB* cb) {
  // Assumption: this call arrives only from ccb related code in immnd_evt.c
  // Not from PRTO related code.
  // Hence the argument 'isPrtoClient' is set to false here---vvv
  return ImmModel::instance(&cb->immModel)->immNotPbeWritable(false);
}

bool immModel_pbeIsInSync(IMMND_CB* cb, bool checkCriticalCcbs) {
  return ImmModel::instance(&cb->immModel)->pbeIsInSync(checkCriticalCcbs);
}

SaImmRepositoryInitModeT immModel_getRepositoryInitMode(IMMND_CB* cb) {
  return (SaImmRepositoryInitModeT)ImmModel::instance(&cb->immModel)
      ->getRepositoryInitMode();
}

unsigned int immModel_getMaxSyncBatchSize(IMMND_CB* cb) {
  return ImmModel::instance(&cb->immModel)->getMaxSyncBatchSize();
}

void immModel_prepareForSync(IMMND_CB* cb, bool isJoining) {
  ImmModel::instance(&cb->immModel)->prepareForSync(isJoining);
}

void immModel_discardContinuations(IMMND_CB* cb, SaUint32T deadConn) {
  ImmModel::instance(&cb->immModel)->discardContinuations(deadConn);
}

SaAisErrorT immModel_objectSync(IMMND_CB* cb,
                                const struct ImmsvOmObjectSync* req) {
  return ImmModel::instance(&cb->immModel)->objectSync(req);
}

SaAisErrorT immModel_finalizeSync(IMMND_CB* cb, struct ImmsvOmFinalizeSync* req,
                                  bool isCoord, bool isSyncClient) {
  return ImmModel::instance(&cb->immModel)
      ->finalizeSync(req, isCoord, isSyncClient, &cb->mLatestCcbId);
}

SaUint32T immModel_adjustEpoch(IMMND_CB* cb, SaUint32T suggestedEpoch,
                               SaUint32T* continuationId, SaUint32T* pbeConn,
                               SaClmNodeIdT* pbeNodeId, bool increment) {
  return ImmModel::instance(&cb->immModel)
      ->adjustEpoch(suggestedEpoch, continuationId, pbeConn, pbeNodeId,
                    increment);
}

SaUint32T immModel_adminOwnerChange(IMMND_CB* cb,
                                    const struct immsv_a2nd_admown_set* req,
                                    bool isRelease) {
  return ImmModel::instance(&cb->immModel)->adminOwnerChange(req, isRelease);
}

SaAisErrorT immModel_rtObjectCreate(IMMND_CB* cb,
                                    struct ImmsvOmCcbObjectCreate* req,
                                    SaUint32T implConn, SaClmNodeIdT implNodeId,
                                    SaUint32T* continuationId,
                                    SaUint32T* pbeConn, SaClmNodeIdT* pbeNodeId,
                                    SaUint32T* spApplConn, SaUint32T* pbe2BConn,
                                    bool isObjectDnUsed)

{
  if (req->attrValues && (req->attrValues->n.attrName.size == 0 ||
                          req->attrValues->n.attrValueType == 0)) {
    LOG_NO(
        "ERR_INVALID_PARAM: Attribute name or attribute type is not properly set");
    return SA_AIS_ERR_INVALID_PARAM;
  }

  /* Find RDN and parent DN */
  if (isObjectDnUsed) {
    std::string parentName;

    if (req->parentOrObjectDn.size == 0) {
      LOG_NO("ERR_INVALID_PARAM: Object DN cannot be empty string");
      return SA_AIS_ERR_INVALID_PARAM;
    }

    uint32_t n;
    /* start from second character. The first character cannot be comma */
    for (n = 1; n < req->parentOrObjectDn.size; n++) {
      if (req->parentOrObjectDn.buf[n] == ',' &&
          req->parentOrObjectDn.buf[n - 1] != '\\') {
        break;
      }
    }

    if (n + 1 < req->parentOrObjectDn.size) {
      parentName.append(req->parentOrObjectDn.buf + n + 1,
                        strnlen(req->parentOrObjectDn.buf + n + 1,
                                req->parentOrObjectDn.size - (n + 1)));
    }

    // Add RDN attribute to req->attrValues
    IMMSV_ATTR_VALUES_LIST* attr =
        (IMMSV_ATTR_VALUES_LIST*)calloc(1, sizeof(IMMSV_ATTR_VALUES_LIST));
    osafassert(attr);
    attr->n.attrName.buf = NULL;
    attr->n.attrName.size = 0;
    /* Temporary setting type to SA_IMM_ATTR_SASTRINGT, which will be later set
     * to the right type */
    attr->n.attrValueType = SA_IMM_ATTR_SASTRINGT;
    attr->n.attrValuesNumber = 1;
    attr->n.attrValue.val.x.buf = strndup(req->parentOrObjectDn.buf, n);
    attr->n.attrValue.val.x.size = strlen(attr->n.attrValue.val.x.buf) + 1;
    attr->next = req->attrValues;
    req->attrValues = attr;

    // Add parent to req
    req->parentOrObjectDn.size = parentName.size() + 1;
    memcpy(req->parentOrObjectDn.buf, parentName.c_str(),
           req->parentOrObjectDn.size);
  }

  return ImmModel::instance(&cb->immModel)
      ->rtObjectCreate(req, implConn, (unsigned int)implNodeId, continuationId,
                       pbeConn, pbeNodeId, spApplConn, pbe2BConn,
                       isObjectDnUsed);
}

SaAisErrorT immModel_rtObjectDelete(
    IMMND_CB* cb, const struct ImmsvOmCcbObjectDelete* req, SaUint32T implConn,
    SaClmNodeIdT implNodeId, SaUint32T* continuationId, SaUint32T* pbeConn,
    SaClmNodeIdT* pbeNodeId, SaStringT** objNameArr, SaUint32T* arrSizePtr,
    SaUint32T* spApplConn, SaUint32T* pbe2BConn) {
  ObjectNameVector ov;
  ObjectNameVector::iterator oni;
  unsigned int ix = 0;
  osafassert(arrSizePtr);

  SaAisErrorT err =
      ImmModel::instance(&cb->immModel)
          ->rtObjectDelete(req, implConn, (unsigned int)implNodeId,
                           continuationId, pbeConn, pbeNodeId, ov, spApplConn,
                           pbe2BConn);

  (*arrSizePtr) = (SaUint32T)ov.size();
  if ((err == SA_AIS_OK) && (*arrSizePtr)) {
    *objNameArr = (SaStringT*)malloc((*arrSizePtr) * sizeof(SaStringT));

    for (oni = ov.begin(); oni != ov.end(); ++oni, ++ix) {
      std::string delObjName = (*oni);
      (*objNameArr)[ix] = (SaStringT)malloc(delObjName.size() + 1);
      strncpy((*objNameArr)[ix], delObjName.c_str(), delObjName.size() + 1);
    }
  }
  osafassert(ix == (*arrSizePtr));
  return err;
}

SaAisErrorT immModel_rtObjectUpdate(
    IMMND_CB* cb, const struct ImmsvOmCcbObjectModify* req, SaUint32T implConn,
    SaClmNodeIdT implNodeId, unsigned int* isPureLocal,
    SaUint32T* continuationId, SaUint32T* pbeConn, SaClmNodeIdT* pbeNodeId,
    SaUint32T* spApplConn, SaUint32T* pbe2BConn) {
  SaAisErrorT err = SA_AIS_OK;
  TRACE_ENTER();
  bool isPl = *isPureLocal;
  TRACE_5("on enter isPl:%u", isPl);
  err = ImmModel::instance(&cb->immModel)
            ->rtObjectUpdate(req, implConn, (unsigned int)implNodeId, &isPl,
                             continuationId, pbeConn, pbeNodeId, spApplConn,
                             pbe2BConn);
  TRACE_5("on leave isPl:%u", isPl);
  *isPureLocal = isPl;
  TRACE_LEAVE();
  return err;
}

void immModel_deferRtUpdate(IMMND_CB* cb, struct ImmsvOmCcbObjectModify* req,
                            SaUint64T msgNo) {
  ImmModel::instance(&cb->immModel)->deferRtUpdate(req, msgNo);
}

bool immModel_fetchRtUpdate(IMMND_CB* cb, struct ImmsvOmObjectSync* syncReq,
                            struct ImmsvOmCcbObjectModify* rtModReq,
                            SaUint64T syncFevsBase) {
  return ImmModel::instance(&cb->immModel)
      ->fetchRtUpdate(syncReq, rtModReq, syncFevsBase);
}

SaAisErrorT immModel_resourceDisplay(
    IMMND_CB* cb, const struct ImmsvAdminOperationParam* reqparams,
    struct ImmsvAdminOperationParam** rparams) {
  const struct ImmsvAdminOperationParam* params = reqparams;
  SaStringT opName = NULL;
  SaUint64T searchcount = 0;

  while (params) {
    if ((strcmp(params->paramName.buf, SA_IMM_PARAM_ADMOP_NAME)) == 0) {
      opName = params->paramBuffer.val.x.buf;
      break;
    }
    params = params->next;
  }

  if ((strcmp(opName, "display") == 0)) {
    IMMND_OM_SEARCH_NODE* searchOp;
    IMMND_IMM_CLIENT_NODE* cl_node =
        (IMMND_IMM_CLIENT_NODE*)ncs_patricia_tree_getnext(&cb->client_info_db,
                                                          NULL);
    while (cl_node) {
      searchOp = cl_node->searchOpList;
      while (searchOp) {
        searchcount++;
        searchOp = searchOp->next;
      }
      cl_node = (IMMND_IMM_CLIENT_NODE*)ncs_patricia_tree_getnext(
          &cb->client_info_db, cl_node->patnode.key_info);
    }
  }

  return ImmModel::instance(&cb->immModel)
      ->resourceDisplay(reqparams, rparams, searchcount);
}

void immModel_setCcbErrorString(IMMND_CB* cb, SaUint32T ccbId,
                                const char* errorString, ...) {
  CcbVector::iterator cvi;
  va_list vl;

  cvi = std::find_if(sCcbVector.begin(), sCcbVector.end(), CcbIdIs(ccbId));
  if (cvi == sCcbVector.end()) {
    /* Cannot find CCB. Error string will be ignored.
     * This case should never happen. */
    return;
  }

  va_start(vl, errorString);
  ImmModel::instance(&cb->immModel)->setCcbErrorString(*cvi, errorString, vl);
  va_end(vl);
}

void immModel_implementerDelete(IMMND_CB *cb, const char *implementerName) {
  ImmModel::instance(&cb->immModel)->implementerDelete(implementerName);
}

void immModel_sendSyncAbortAt(IMMND_CB *cb, struct timespec time) {
  ImmModel::instance(&cb->immModel)->sendSyncAbortAt(time);
}

void immModel_getSyncAbortRsp(IMMND_CB *cb) {
  ImmModel::instance(&cb->immModel)->getSyncAbortRsp();
}

/*====================================================================*/

ImmModel::ImmModel() : loaderPid(-1) {}

//>
// When sSyncAbortingAt != {0,0}, it means the SYNC has been aborted
// by coord and that abort message not yet broadcasted to IMMNDs.
// Therefore, NODE STATE at IMMND coord (IMM_NODE_FULLY_AVAILABLE) is
// different with veteran node(s) (IMM_NODE_R_AVAILABLE) at the moment.
// So, any change to IMM data model at the coord will result data mismatchs
// comparing with veterans, and lead to veterans restarted at sync finalize.
//
// In addition, we check the time here to avoid the worst cases
// such as the abort message arrived at active IMMD, but later on
// it fails to send the response (e.g: hang IMMD, or reboot IMMD, etc.)
// For that reason, if we don't receive the response within 6 seconds,
// consider sending abort failed.
//
// And we just do the check for rsp messages come from active IMMD.
//<

// Store the start time of sync abort sent
static struct timespec sSyncAbortSentAt;

void ImmModel::sendSyncAbortAt(timespec& time) {
  sSyncAbortSentAt = time;
}

void ImmModel::getSyncAbortRsp() {
  sSyncAbortSentAt.tv_sec  = 0;
  sSyncAbortSentAt.tv_nsec = 0;
}

static bool is_sync_aborting() {
  bool unset = (sSyncAbortSentAt.tv_nsec == 0 &&
                sSyncAbortSentAt.tv_sec == 0);

  if (unset) return false;

  time_t duration_in_second = 0xffffffff;
  struct timespec now, duration;
  osaf_clock_gettime(CLOCK_MONOTONIC, &now);
  osaf_timespec_subtract(&now, &sSyncAbortSentAt, &duration);
  duration_in_second = duration.tv_sec;
  if (duration_in_second > DEFAULT_TIMEOUT_SEC) {
    sSyncAbortSentAt.tv_sec  = 0;
    sSyncAbortSentAt.tv_nsec = 0;
  }

  return (duration_in_second <= DEFAULT_TIMEOUT_SEC);
}

bool ImmModel::immNotWritable() {
  switch (sImmNodeState) {
    case IMM_NODE_R_AVAILABLE:
    case IMM_NODE_UNKNOWN:
    case IMM_NODE_ISOLATED:
      return true;

    case IMM_NODE_W_AVAILABLE:
    case IMM_NODE_FULLY_AVAILABLE:
    case IMM_NODE_LOADING:
      return false;

    default:
      LOG_ER("Impossible node state, will terminate");
  }
  abort();
}

/* immNotPbeWritable returning true means:
   (1) immNotWriteable is true OR...
   (2) immNotWritable is false (imm service is writable), but according to
   configuration there should be a persistent back-end (Pbe) and the Pbe is
   currently not operational. OR..
   (3) PBE is operational, but backlog on PRTOs or Ccbs is large enough to
   warant back-presure (TRY_AGAIN) towards the application.

   Pbe is dynamically disabled by setting immInitMode to SA_IMM_INIT_FROM_FILE.
*/
bool ImmModel::immNotPbeWritable(bool isPrtoClient) {
  SaUint32T dummyCon;
  unsigned int dummyNode;
  /* Not writable => Not persitent writable. */
  if (immNotWritable() || is_sync_aborting()) {
    return true;
  }

  /* INIT_FROM_FILE => no PBE => Writable => PersistentWritable */
  if (immInitMode == SA_IMM_INIT_FROM_FILE) {
    return false;
  }

  if (immInitMode != SA_IMM_KEEP_REPOSITORY) {
    LOG_ER("Illegal value on RepositoryInitMode:%u", immInitMode);
    immInitMode = SA_IMM_INIT_FROM_FILE;
    return false;
  }

  /* immInitMode == SA_IMM_KEEP_REPOSITORY */
  /* Check if PBE OI is available and making progress. */

  if (!getPbeOi(&dummyCon, &dummyNode)) {
    /* Pbe SHOULD be present but is NOT */
    return true;
  }

  if (pbeBSlaveHasExisted() && !getPbeBSlave(&dummyCon, &dummyNode)) {
    /* Pbe slave SHOULD be present but is NOT. Normally this
       means immNotPbeWritable() returns true. But if oneSafe2PBEAllowed()
       is true, (indicating SC repair or similar) then the unavailability
       of the slave is accepted, otherwise not. By default
       oneSafePBEAllowed() is false. So normally we will exit with
       reject (true) here.
    */
    if (!oneSafe2PBEAllowed()) {
      return true;
    }
  }

  /* Pbe is present but Check also for backlog. */

  unsigned int ccbsInCritical = 0;
  CcbVector::iterator i3 = sCcbVector.begin();
  for (; i3 != sCcbVector.end(); ++i3) {
    if ((*i3)->mState == IMM_CCB_CRITICAL) {
      ccbsInCritical++;

      if ((*i3)->mPbeRestartId) {
        /* PBE was restarted with a ccb in critical. */
        return true;
      }
    }
  }

  /* If too many ccbs are already critical then delay new ccbs. */
  if (ccbsInCritical > CCB_CRIT_THRESHOLD) {
    return true;
  }

  /* Finally, be extra stringent PRTO/PRTA changes:
     PrtCreate, PrtUpdate, PrtDelete
     (will also cover classCreate and classDelete).
  */
  if (isPrtoClient) {
    SaUint32T prtoBacklog = (SaUint32T)sPbeRtMutations.size();

    static bool prtoHighReached = false; /* Note *static*, for hysteresis */

    if (prtoHighReached && (prtoBacklog > PRT_LOW_THRESHOLD)) {
      return true;
    }

    prtoHighReached = false;

    if (prtoBacklog > PRT_HIGH_THRESHOLD) {
      prtoHighReached = true;
      return true;
    }
  }

  return false; /* Pbe IS present & backlog is contained */
}

void ImmModel::prepareForLoading() {
  switch (sImmNodeState) {
    case IMM_NODE_UNKNOWN:
      sImmNodeState = IMM_NODE_LOADING;
      LOG_NO("NODE STATE-> IMM_NODE_LOADING");
      break;

    case IMM_NODE_ISOLATED:
      LOG_IN(
          "Node is not ready to participate in loading, "
          "will wait and be synced instead.");
      break;
    case IMM_NODE_LOADING:
    case IMM_NODE_FULLY_AVAILABLE:
    case IMM_NODE_W_AVAILABLE:
    case IMM_NODE_R_AVAILABLE:
      LOG_ER(
          "Node is in a state that cannot accept start "
          "of loading, will terminate");
      abort();
    default:
      LOG_ER("Impossible node state, will terminate");
      abort();
  }
}

bool ImmModel::readyForLoading() { return sImmNodeState == IMM_NODE_LOADING; }

void ImmModel::recognizedIsolated() {
  switch (sImmNodeState) {
    case IMM_NODE_UNKNOWN:
      sImmNodeState = IMM_NODE_ISOLATED;
      LOG_NO("NODE STATE-> IMM_NODE_ISOLATED");
      break;

    case IMM_NODE_ISOLATED:
      LOG_IN("Redundant sync request, when IMM_NODE_ISOLATED");
      break;
    case IMM_NODE_W_AVAILABLE:
      LOG_WA("Redundant sync request, when IMM_NODE_W_AVAILABLE");
      break;

    case IMM_NODE_FULLY_AVAILABLE:
    case IMM_NODE_LOADING:
    case IMM_NODE_R_AVAILABLE:
      LOG_ER("Node is in a state that cannot request sync, will terminate");
      abort();
    default:
      LOG_ER("Impossible node state, will terminate");
      abort();
  }
}

void ImmModel::prepareForSync(bool isJoining) {
  switch (sImmNodeState) {
    case IMM_NODE_ISOLATED:
      osafassert(isJoining);
      sImmNodeState = IMM_NODE_W_AVAILABLE;  // accept the sync messages
      LOG_NO("NODE STATE-> IMM_NODE_W_AVAILABLE");
      break;

    case IMM_NODE_FULLY_AVAILABLE:
      osafassert(!isJoining);
      sImmNodeState = IMM_NODE_R_AVAILABLE;  // Stop mutations.
      LOG_NO("NODE STATE-> IMM_NODE_R_AVAILABLE");
      break;

    case IMM_NODE_UNKNOWN:
      LOG_IN(
          "Node is not ready to participate in sync, "
          "will wait and be synced later.");
      break;

    case IMM_NODE_LOADING:
    case IMM_NODE_W_AVAILABLE:
    case IMM_NODE_R_AVAILABLE:
      LOG_ER(
          "Node is in a state that cannot accept start of sync, "
          "will terminate");
      abort();
    default:
      LOG_ER("Impossible node state, will terminate");
      abort();
  }
}

bool ImmModel::syncComplete(bool isJoining) {
  // Dont need the isJoining argument for now.

  return sImmNodeState == IMM_NODE_FULLY_AVAILABLE;
}

void ImmModel::pbePrtoPurgeMutations(unsigned int nodeId,
                                     ConnVector& connVector) {
  ObjectMutationMap::iterator omuti;
  ObjectMap::iterator oi;
  SaInvocationT inv;
  ContinuationMap2::iterator ci;
  ImmAttrValueMap::iterator oavi;
  ObjectInfo* afim = NULL;
  std::set<SaUint32T> connSet;
  TRACE_ENTER();
  bool dummy = false;
  bool dummy2 = false;

  for (omuti = sPbeRtMutations.begin(); omuti != sPbeRtMutations.end();
       ++omuti) {
    ObjectInfo* grandParent = NULL;
    ObjectMutation* oMut = omuti->second;
    oi = sObjectMap.find(omuti->first);
    osafassert(oi != sObjectMap.end() || (oMut->mOpType == IMM_CREATE_CLASS) ||
               (oMut->mOpType == IMM_DELETE_CLASS) ||
               (oMut->mOpType == IMM_UPDATE_EPOCH));

    inv = m_IMMSV_PACK_HANDLE(oMut->mContinuationId, nodeId);
    ci = sPbeRtReqContinuationMap.find(inv);

    if (ci != sPbeRtReqContinuationMap.end()) {
      if (oi != sObjectMap.end()) {
        /* Only reply the default reply of TRY_AGAIN on failed
           RTO ops, not failed class ops. The RTO ops are
           reverted on fauilure here, which is consistent with
           a reply of TRY_AGAIN. The class ops are NOT reverted,
           so a reply of TRY_AGAIN would be incorrect and a
           reply of OK would be premature. For class ops
           that have trouble with the PBE, ERR_TIMEOUT would
           be the only proper reply, so we let the client
           timeout by not replying.
        */
        if(connSet.find(ci->second.mConn) == connSet.end()) {
          /* Don't add a connection more that once */
          connSet.insert(ci->second.mConn);
          connVector.push_back(ci->second.mConn);
        }
      }
      sPbeRtReqContinuationMap.erase(ci);
    }

    /* Fall back. */
    switch (oMut->mOpType) {
      case IMM_CREATE:
        osafassert(oi->second == oMut->mAfterImage);

        oMut->mAfterImage->mObjFlags &= ~IMM_CREATE_LOCK;
        /* Decrement mChildCount for all parents of the reverted
           PRTO create. Note: There could be several PRTO creates to
           process in this purge, but none can be the parent of another
           in this purge. This because a PRTO create of a subobject
           to a parent object is not allowed if the create of the parent
           has not yet been ack'ed by the PBE. So one can never have
           both parent and child being an un-acked PRTO create.
        */
        grandParent = oMut->mAfterImage->mParent;
        while (grandParent) {
          std::string gpDn;
          getObjectName(grandParent, gpDn);
          grandParent->mChildCount--;
          LOG_IN(
              "Childcount for (grand)parent %s of purged create prto %s adjusted to %u",
              gpDn.c_str(), omuti->first.c_str(), grandParent->mChildCount);
          grandParent = grandParent->mParent;
        }
        oMut->mAfterImage = NULL;
        LOG_WA("Create of PERSISTENT runtime object '%s' REVERTED ",
               omuti->first.c_str());
        osafassert(deleteRtObject(oi, true, NULL, dummy, dummy2) == SA_AIS_OK);
        break;

      case IMM_DELETE:
        osafassert(oi->second == oMut->mAfterImage);

        oMut->mAfterImage->mObjFlags &= ~IMM_DELETE_LOCK;
        oMut->mAfterImage = NULL;
        /* A delete may delete a tree including both persistent
           and non persistent RTOs. The roll back here has to
           revert all the deletes, not just the persistent RTOs.
        */
        LOG_WA("Delete of runtime object '%s' REVERTED ", omuti->first.c_str());
        break;

      case IMM_MODIFY:
        osafassert(oi->second != oMut->mAfterImage); /* NOT equal */
        oi->second->mObjFlags &= ~IMM_RT_UPDATE_LOCK;
        afim = oMut->mAfterImage;
        for (oavi = afim->mAttrValueMap.begin();
             oavi != afim->mAttrValueMap.end(); ++oavi) {
          delete oavi->second;
        }
        afim->mAttrValueMap.clear();
        delete afim;
        oMut->mAfterImage = NULL;
        LOG_WA(
            "update of PERSISTENT runtime attributes in object '%s' REVERTED.",
            omuti->first.c_str());
        break;

      case IMM_CREATE_CLASS:
        LOG_WA("PBE failed in persistification of class create %s",
               omuti->first.c_str());
        /* The PBE should have aborted when this happened.
           The existence of this mutation would prevent a restart
           with --recover. Instead a restart will regenerate the
           PBE file, forcing it in line with the imms runtime
           state.
        */
        break;

      case IMM_DELETE_CLASS:
        LOG_WA("PBE failed in persistification of class delete %s",
               omuti->first.c_str());
        /* The PBE should have aborted when this happened.
           The existence of this mutation would prevent a restart
           with --recover. Instead a restart will regenerate the
           PBE file, forcing it in line with the imms runtime
           state.
        */

        break;

      case IMM_UPDATE_EPOCH:
        LOG_WA("PBE failed in persistification of update epoch");
        /* The PBE should have aborted when this happened.
           The existence of this mutation would prevent a restart
           with --recover. Instead a restart will regenerate the
           PBE file, forcing it in line with the imms runtime
           state.
        */

        break;

      default:
        abort();
    }
    delete oMut;
  }

  sPbeRtMutations.clear();
  sPbeRtReqContinuationMap.clear();
  TRACE_LEAVE();
}

void ImmModel::abortSync() {
  ClassMap::iterator ci;
  switch (sImmNodeState) {
    case IMM_NODE_R_AVAILABLE:
      sImmNodeState = IMM_NODE_FULLY_AVAILABLE;
      sNodesDeadDuringSync.clear();
      sImplsDeadDuringSync.clear();
      osafassert(sAdmosDeadDuringSync.empty());

      LOG_NO("NODE STATE-> IMM_NODE_FULLY_AVAILABLE (%u)", __LINE__);
      break;

    case IMM_NODE_UNKNOWN:
    case IMM_NODE_ISOLATED:
      break;

    case IMM_NODE_FULLY_AVAILABLE:
      LOG_WA(
          "Abort sync received while being fully available, "
          "should not happen.");
      break;

    case IMM_NODE_W_AVAILABLE:
      sImmNodeState = IMM_NODE_UNKNOWN;
      LOG_NO("NODE STATE-> IMM_NODE_UNKNOW %u", __LINE__);
      /* Aborting a started but not completed sync. */
      LOG_NO("Abort sync: Discarding synced objects");
      // Remove all NO_DANGLING references
      sReverseRefsNoDanglingMMap.clear();
      while (!sObjectMap.empty()) {
        ObjectMap::iterator oi = sObjectMap.begin();
        TRACE("sObjectmap.size:%u delete: %s", (unsigned int)sObjectMap.size(),
              oi->first.c_str());
        commitDelete(oi->first);
      }

      LOG_NO("Abort sync: Discarding synced classes");
      ci = sClassMap.begin();
      while (ci != sClassMap.end()) {
        TRACE("Removing Class:%s", ci->first.c_str());
        osafassert(ci->second->mExtent.empty());

        AttrMap::iterator ai = ci->second->mAttrMap.begin();
        while (ai != ci->second->mAttrMap.end()) {
          TRACE("Remove Attr:%s", ai->first.c_str());
          AttrInfo* ainfo = ai->second;
          osafassert(ainfo);
          delete (ainfo);
          ++ai;
        }
        ci->second->mAttrMap.clear();

        delete ci->second;
        updateImmObject(ci->first, true);
        ++ci;
      }
      sClassMap.clear();

      if (!sDeferredObjUpdatesMap.empty()) {
        LOG_NO("Abort sync: discarding deferred RTA updates");

        DeferredObjUpdatesMap::iterator doumIter =
            sDeferredObjUpdatesMap.begin();
        while (doumIter != sDeferredObjUpdatesMap.end()) {
          DeferredRtAUpdateList* attrUpdList = doumIter->second;

          DeferredRtAUpdateList::iterator drtauIter = attrUpdList->begin();
          while (drtauIter != attrUpdList->end()) {
            immsv_free_attrmods(drtauIter->attrModsList);
            drtauIter->attrModsList = NULL;
            ++drtauIter;
          }
          attrUpdList->clear();

          delete attrUpdList;
          ++doumIter;
        }
        sDeferredObjUpdatesMap.clear();
      }

      sNodesDeadDuringSync.clear();
      sImplsDeadDuringSync.clear();
      sAdmosDeadDuringSync.clear();
      sImplDetachTime.clear();

      if (!sImplementerVector.empty()) {
        ImplementerVector::iterator i;
        for (i = sImplementerVector.begin(); i != sImplementerVector.end();
             ++i) {
          ImplementerInfo* info = (*i);
          delete info;
        }
        sImplementerVector.clear();
      }

      osafassert(sOwnerVector.empty());
      osafassert(sCcbVector.empty());

      sMissingParents.clear();
      break;

    case IMM_NODE_LOADING:

      LOG_ER(
          "Node is in a state %u that cannot accept abort "
          " of sync, will terminate",
          sImmNodeState);
      abort();
    default:
      LOG_ER("Impossible node state, will terminate");
      abort();
  }
}

/**
 * Sets the pid for the loader process. Bars other processes
 * from accessing the IMM model during loading.
 *
 */
void ImmModel::setLoader(int pid) {
  loaderPid = pid;
  if (pid == 0) {
    sImmNodeState = IMM_NODE_FULLY_AVAILABLE;
    LOG_NO("NODE STATE-> IMM_NODE_FULLY_AVAILABLE %u", __LINE__);

    immInitMode = getRepositoryInitMode();
    if (immInitMode == SA_IMM_KEEP_REPOSITORY) {
      LOG_NO("RepositoryInitModeT is SA_IMM_KEEP_REPOSITORY");
    } else {
      LOG_NO("RepositoryInitModeT is SA_IMM_INIT_FROM_FILE");
      immInitMode = SA_IMM_INIT_FROM_FILE; /* Ensure valid value*/
    }

    if (accessControlMode() == ACCESS_CONTROL_DISABLED) {
      LOG_WA("IMM Access Control mode is DISABLED!");
    } else if (accessControlMode() == ACCESS_CONTROL_PERMISSIVE) {
      LOG_WA("IMM Access Control mode is PERMISSIVE");
    } else {
      LOG_NO("IMM Access Control mode is ENFORCING");
    }

  } else {
    TRACE_5("Loading starts, pid:%u", pid);
  }
}

/**
 * Returns the pid for the loader process. Used to check if loading is
 * currently in progress.
 *
 */
int ImmModel::getLoader() { return loaderPid; }

/**
 * Changes the current epoch to either suggestedEpoch or to
 * the epoch fetched from the imm-object.
 * The former case is expected to be after a sync. The latter
 * case is expected after a reload.
 * Normally the epoch is incremented when suggestedEpoch is <=
 * than current epoch. But if the 'inrement' arg is false then
 * no incrementing is done here. This is used when only the side
 * effects of adjustEpoch are desired (pushing epoch to IMMD and PBE).
 */
int ImmModel::adjustEpoch(int suggestedEpoch, SaUint32T* continuationIdPtr,
                          SaUint32T* pbeConnPtr, unsigned int* pbeNodeIdPtr,
                          bool increment) {
  int restoredEpoch = 0;
  ImmAttrValueMap::iterator avi;
  ObjectMap::iterator oi = sObjectMap.find(immObjectDn);
  osafassert(oi != sObjectMap.end() && oi->second);

  ObjectInfo* immObject = oi->second;
  osafassert(immObject != nullptr);
  avi = immObject->mAttrValueMap.find(immAttrEpoch);
  osafassert(avi != immObject->mAttrValueMap.end());
  osafassert(!avi->second->isMultiValued());

  restoredEpoch = avi->second->getValue_int();

  if (restoredEpoch < 1) {
    restoredEpoch = 1;
  }

  if (suggestedEpoch <= restoredEpoch) {
    suggestedEpoch = restoredEpoch;
    if (increment) {
      ++suggestedEpoch;
    }
  }

  if ((increment) && (avi != immObject->mAttrValueMap.end())) {
    avi->second->setValue_int(suggestedEpoch);
    LOG_NO("Epoch set to %u in ImmModel", suggestedEpoch);
  }

  if (pbeNodeIdPtr) {
    /* Generate global continuation if PBE is enabled, regardless if
       PBE is currently present or not. This is necessary since
       ImmModel::adjustEpoch is not always invoked in the context of
       a fevs message. Thus even though getPbeOi below is fevs safe
       in that it is based on current fevs context, that fevs context
       may be a different fevs context on other nodes by the time they
       get the message that triggers adjustEpoch, for example in
       immnd_abortSync().
    */

    if (++sLastContinuationId >= 0xfffffffe) {
      sLastContinuationId = 1;
    }
    *continuationIdPtr = sLastContinuationId;
    TRACE("continuation generated: %u", *continuationIdPtr);

    if (getPbeOi(pbeConnPtr, pbeNodeIdPtr)) {
      if (sPbeRtMutations.find(immObjectDn) == sPbeRtMutations.end()) {
        ObjectMutation* oMut = new ObjectMutation(IMM_UPDATE_EPOCH);
        oMut->mContinuationId = (*continuationIdPtr);
        oMut->mAfterImage = NULL;
        sPbeRtMutations[immObjectDn] = oMut;
      } else {
        LOG_WA("Continuation for Pbe mutation on %s already exists",
               immObjectDn.c_str());
      }
    }
  }

  return suggestedEpoch;
}

/**
 * Fetches the SaImmRepositoryInitT value of the attribute
 * 'saImmRepositoryInit' in the object immManagementDn.
 * If not found then return SA_IMM_INIT_FROM_FILE as default.
 */
SaImmRepositoryInitModeT ImmModel::getRepositoryInitMode() {
  ImmAttrValueMap::iterator avi;
  ObjectInfo* immMgObject = NULL;
  ObjectMap::iterator oi = sObjectMap.find(immManagementDn);
  if (oi != sObjectMap.end()) {
    immMgObject = oi->second;
    avi = immMgObject->mAttrValueMap.find(saImmRepositoryInit);

    if (avi != immMgObject->mAttrValueMap.end()) {
      osafassert(!avi->second->isMultiValued());
      return (SaImmRepositoryInitModeT)avi->second->getValue_int();
    }
  }

  TRACE_2("%s not found or %s not found, returning INIT_FROM_FILE",
          immManagementDn.c_str(), saImmRepositoryInit.c_str());
  return SA_IMM_INIT_FROM_FILE;
}

unsigned int ImmModel::getMaxSyncBatchSize() {
  TRACE_ENTER();
  unsigned int mbSize = 0;
  ObjectMap::iterator oi = sObjectMap.find(immObjectDn);
  if (oi == sObjectMap.end()) {
    TRACE_LEAVE();
    return 0;
  }

  ObjectInfo* immObject = oi->second;
  ImmAttrValueMap::iterator avi =
      immObject->mAttrValueMap.find(immSyncBatchSize);
  if (avi != immObject->mAttrValueMap.end()) {
    osafassert(!(avi->second->isMultiValued()));
    ImmAttrValue* valuep = avi->second;
    mbSize = valuep->getValue_int();
  }
  TRACE_LEAVE();
  return mbSize;
}

void ImmModel::getLocalImplementers(ConnVector& cv, IdVector& idv) {
  TRACE_ENTER();
  ImplementerVector::iterator i;
  for (i = sImplementerVector.begin(); i != sImplementerVector.end(); ++i) {
    ImplementerInfo* info = (*i);
    if (info->mConn) {
      cv.push_back(info->mConn);
      idv.push_back(info->mId);
    }
  }
  TRACE_LEAVE();
}

bool ImmModel::getLongDnsAllowed(ObjectInfo* immObject) {
  TRACE_ENTER();
  bool longDnsAllowed = false;
  if (sCcbIdLongDnGuard) {
    /* A ccb is currently mutating longDnsAllowed */
    return false;
  }

  if ((sImmNodeState > IMM_NODE_LOADING) && !protocol45Allowed()) {
    LOG_IN("Long DNs not allowed before upgrade to OpenSAF 4.5 is complete");
    return false;
  }

  ObjectMap::iterator oi;
  if (immObject == NULL) {
    oi = sObjectMap.find(immObjectDn);
    if (oi == sObjectMap.end()) {
      TRACE_LEAVE();
      return sImmNodeState == IMM_NODE_LOADING;
    }
    immObject = oi->second;
  }

  ImmAttrValueMap::iterator avi =
      immObject->mAttrValueMap.find(immLongDnsAllowed);
  if (avi != immObject->mAttrValueMap.end()) {
    osafassert(!(avi->second->isMultiValued()));
    ImmAttrValue* valuep = avi->second;
    longDnsAllowed = (valuep->getValue_int() != 0);
  }
  TRACE_LEAVE();
  return longDnsAllowed;
}

/**
 * Fetches the nodeId and possibly connection id for the
 * implementer connected to the class OPENSAF_IMM_CLASS_NAME.
 * This is used both when testing for the presence of the PBE
 * and for locating the PBE when a message/upcall is to be sent
 * to it.
 *
 * If fevsSafe is true, then a PBE-OI detected as dying at this node
 * is still treated as if it exists. This to get uniformity of state
 * change across all nodes. The connection is still returned as NULL
 * if it is dead, which means ALL nodes will believe the PBE is at
 * some another node, whereas it is really not attached anywhere.
 * This typically means that all nodes will wait for the PBE to
 * re-attach.
 */
void* /*default true*/
    ImmModel::getPbeOi(SaUint32T* pbeConn, unsigned int* pbeNode,
                       bool fevsSafe) {
  *pbeConn = 0;
  *pbeNode = 0;
  ClassMap::iterator ci = sClassMap.find(immClassName);
  osafassert(ci != sClassMap.end());
  ClassInfo* classInfo = ci->second;
  if (classInfo->mImplementer == NULL || classInfo->mImplementer->mId == 0) {
    return NULL;
  }

  /* PBE-OI exists but could be dying. */
  if (classInfo->mImplementer->mDying) {
    if (!fevsSafe) {
      /* Not fevsSafe => immediately inform about dying PBE.*/
      LOG_NO("ImmModel::getPbeOi reports missing PbeOi locally => unsafe");
      return NULL;
    }
  } else {
    /* Only assign conn when PBE is not dying. */
    *pbeConn = classInfo->mImplementer->mConn;
  }

  *pbeNode = classInfo->mImplementer->mNodeId;

  return classInfo->mImplementer;
}

/**
 * Fetches a special Applier, if one is attached at the local node.
 * See README and ticket #2873.
 */
ImplementerInfo* ImmModel::getSpecialApplier() {
  TRACE_ENTER();
  const std::string spApplierA(OPENSAF_IMM_REPL_A_NAME);
  const std::string spApplierB(OPENSAF_IMM_REPL_B_NAME);

  ImplementerInfo* specialApplier = findImplementer(spApplierA);

  if (!specialApplier || specialApplier->mDying || !(specialApplier->mConn)) {
    specialApplier = findImplementer(spApplierB);
    if (specialApplier &&
        (specialApplier->mDying || !(specialApplier->mConn))) {
      specialApplier = NULL;
    }
  }

  TRACE_LEAVE();
  return specialApplier;
}

bool ImmModel::specialApplyForClass(ClassInfo* classInfo) {
  TRACE_ENTER();
  osafassert(classInfo);
  AttrMap::iterator atti;

  if (getSpecialApplier()) {
    for (atti = classInfo->mAttrMap.begin(); atti != classInfo->mAttrMap.end();
         ++atti) {
      if (atti->second->mFlags & SA_IMM_ATTR_NOTIFY) {
        return true;
      }
    }
  }

  TRACE_LEAVE();
  return false;
}

bool ImmModel::pbeBSlaveHasExisted() {
  /* An IMMND never explicitly discard implementer names or applier names.
     At IMMND process start, implementer names are synced *including* applier
     names. But second level map for appliers is not synced (applier=>class
     etc). The existence of the slave PBE applier-name indicates that this IMMND
     has participated in a cluster where the slave PBE has been present.
     If slave PBE is or is not present currently is answered by getPbeBSlave().
   */
  return findImplementer(immPbeBSlaveName) != NULL;
}

void* ImmModel::getPbeBSlave(SaUint32T* pbeConn, unsigned int* pbeNode,
                             bool fevsSafe) {
  *pbeConn = 0;
  *pbeNode = 0;
  ImplementerInfo* pbeBSlave = findImplementer(immPbeBSlaveName);
  if (pbeBSlave == NULL || pbeBSlave->mId == 0) {
    return NULL;
  }

  if (pbeBSlave->mDying) {
    if (!fevsSafe) {
      LOG_NO(
          "ImmModel::getPbeBSlave reports missing PbeBSlave locally => unsafe");
      return NULL;
    }
  } else {
    *pbeConn = pbeBSlave->mConn;
  }

  *pbeNode = pbeBSlave->mNodeId;

  return pbeBSlave;
}

/**
 * Returns the only instance of ImmModel.
 */
ImmModel* ImmModel::instance(void** sInstancep) {
  // static ImmModel* sInstance = 0;

  if (*sInstancep == NULL) {
    *sInstancep = new ImmModel();
  }

  return (ImmModel*)*sInstancep;
}

bool ImmModel::nocaseCompare(const std::string& str1,
                             const std::string& str2) const {
  size_t pos;
  size_t len = str1.length();

  if (len != str2.length()) return false;

  for (pos = 0; pos < len; ++pos) {
    if (toupper(str1.at(pos)) != toupper(str2.at(pos))) return false;
  }

  return true;
}

/**
 * Creates a class.
 */
SaAisErrorT ImmModel::classCreate(const ImmsvOmClassDescr* req,
                                  SaUint32T reqConn, unsigned int nodeId,
                                  SaUint32T* continuationIdPtr,
                                  SaUint32T* pbeConnPtr,
                                  unsigned int* pbeNodeIdPtr) {
  int rdns = 0;
  int illegal = 0;
  bool persistentRt = false;
  bool persistentRdn = false;
  bool useUnknownFlag = false;
  ClassInfo* classInfo = NULL;
  ClassInfo* prevClassInfo = NULL;
  ClassInfo dummyClass(req->classCategory);
  bool schemaChange = false;
  AttrMap newAttrs;
  AttrMap changedAttrs;
  ImmsvAttrDefList* list = req->attrDefinitions;
  bool isLoading =
      (sImmNodeState == IMM_NODE_LOADING || /* LOADING-SEVER, LOADING-CLIENT */
       sImmNodeState == IMM_NODE_W_AVAILABLE); /* SYNC-CLIENT */

  TRACE_ENTER2("cont:%p connp:%p nodep:%p", continuationIdPtr, pbeConnPtr,
               pbeNodeIdPtr);
  size_t sz = strnlen((char*)req->className.buf, (size_t)req->className.size);

  std::string className((const char*)req->className.buf, sz);
  SaAisErrorT err = SA_AIS_OK;

  if (immNotPbeWritable()) {
    return SA_AIS_ERR_TRY_AGAIN;
  }

  if (!schemaNameCheck(className)) {
    LOG_NO("ERR_INVALID_PARAM: Not a proper class name %s", className.c_str());
    return SA_AIS_ERR_INVALID_PARAM;
  }

  ObjectMap::iterator oit = sObjectMap.find(immObjectDn);
  if (protocol51Allowed() && oit != sObjectMap.end() && !isLoading) {
    ObjectInfo* immObject = oit->second;
    ImmAttrValueMap::iterator avi =
        immObject->mAttrValueMap.find(immMaxClasses);
    osafassert(avi != immObject->mAttrValueMap.end());
    osafassert(!(avi->second->isMultiValued()));
    ImmAttrValue* valuep = avi->second;
    unsigned int maxClasses = valuep->getValue_int();
    if (sClassMap.size() >= maxClasses) {
      LOG_NO(
          "ERR_NO_RESOURCES: maximum class limit %d has been reched for the cluster",
          maxClasses);
      return SA_AIS_ERR_NO_RESOURCES;
    }
  }
  ClassMap::iterator i = sClassMap.find(className);
  if (i == sClassMap.end()) {
    /* Class-name is unique case-sensitive.
       Verify class-name unique case-insensitive. See #1846. */
    for (i = sClassMap.begin(); i != sClassMap.end(); ++i) {
      if (nocaseCompare(className, i->first)) {
        LOG_NO(
            "ERR_EXIST: New class-name:%s identical to existing class-name %s "
            "(case insensitive)",
            className.c_str(), i->first.c_str());
        return SA_AIS_ERR_EXIST;
      }
    }
    /*Class-name is unique case-insensitive.*/

    TRACE_5("CREATE CLASS '%s' category:%u", className.c_str(),
            req->classCategory);
    classInfo = new ClassInfo(req->classCategory);
  } else {
    /* Class name exists */

    /* First check for prior create of same class in progress. */
    if (sPbeRtMutations.find(className) != sPbeRtMutations.end()) {
      LOG_NO(
          "ERR_BUSY: Create of class %s received while class "
          "with same name is already being mutated",
          className.c_str());
      err = SA_AIS_ERR_BUSY;
      goto done;
    }

    /* Second, check for schema upgrade.*/
    if (schemaChangeAllowed()) {
      /* New non-standard upgrade behavior. */
      LOG_NO("Class '%s' exist - check implied schema upgrade",
             className.c_str());
      schemaChange = true;
      classInfo =
          &dummyClass; /* New class created as dummy, do all normal checks */
      prevClassInfo = i->second;
    } else {
      /* Standard behavior, reject. */
      TRACE_7("ERR_EXIST: class '%s' exist", className.c_str());
      err = SA_AIS_ERR_EXIST;
      goto done;
    }
  }

  /* The entire while loop below is independent of whether this is an upgrade
     or not. Each attrdef is checked for fundamental correctness.
     A proposed upgrade of a class def has to pass all the checks it would
     face, had it been the first version of the class def.
     Additional checks, specific to upgrade are done, later (after this loop).
  */
  while (list) {
    ImmsvAttrDefinition* attr = &list->d;
    sz = strnlen((char*)attr->attrName.buf, (size_t)attr->attrName.size);
    std::string attrName((const char*)attr->attrName.buf, sz);
    const char* attNm = attrName.c_str();

    if ((attr->attrFlags & SA_IMM_ATTR_CONFIG) &&
        (attr->attrFlags & SA_IMM_ATTR_RUNTIME)) {
      LOG_NO(
          "ERR_INVALID_PARAM: Attribute '%s' can not be both SA_IMM_ATTR_CONFIG "
          "and SA_IMM_ATTR_RUNTIME",
          attNm);
      illegal = 1;
    }

    if ((attr->attrFlags & SA_IMM_ATTR_NO_DUPLICATES) &&
        !(attr->attrFlags & SA_IMM_ATTR_MULTI_VALUE)) {
      LOG_NO(
          "ERR_INVALID_PARAM: Attribute '%s' can not have SA_IMM_ATTR_NO_DUPLICATES "
          "when not SA_IMM_ATTR_MULTI_VALUE",
          attNm);
      illegal = 1;
    }

    if (attr->attrFlags & SA_IMM_ATTR_CONFIG) {
      if (req->classCategory == SA_IMM_CLASS_RUNTIME) {
        LOG_NO(
            "ERR_INVALID_PARAM: Runtime objects can not have config "
            "attributes '%s'",
            attNm);
        illegal = 1;
      }

      if (attr->attrFlags & SA_IMM_ATTR_PERSISTENT) {
        LOG_NO(
            "ERR_INVALID_PARAM: Config attribute '%s' can not be declared "
            "SA_IMM_ATTR_PERSISTENT",
            attNm);
        illegal = 1;
      }

      if (attr->attrFlags & SA_IMM_ATTR_CACHED) {
        LOG_NO(
            "ERR_INVALID_PARAM: Config attribute '%s' can not be declared "
            "SA_IMM_ATTR_CACHED",
            attNm);
        illegal = 1;
      }

    } else if (attr->attrFlags & SA_IMM_ATTR_RUNTIME) {
      if ((attr->attrFlags & SA_IMM_ATTR_PERSISTENT) &&
          !(attr->attrFlags & SA_IMM_ATTR_CACHED)) {
        attr->attrFlags |= SA_IMM_ATTR_CACHED;
        LOG_NO(
            "persistent implies cached as of SAI-AIS-IMM-A.02.01 "
            "class:%s atrName:%s - corrected",
            className.c_str(), attNm);
      }

      if (attr->attrFlags & SA_IMM_ATTR_WRITABLE) {
        LOG_NO(
            "ERR_INVALID_PARAM: Runtime attribute '%s' can not be declared "
            "SA_IMM_ATTR_WRITABLE",
            attNm);
        illegal = 1;
      }

      if (attr->attrFlags & SA_IMM_ATTR_INITIALIZED) {
        LOG_NO(
            "ERR_INVALID_PARAM: Runtime attribute '%s' can not be declared "
            "SA_IMM_ATTR_INITIALIZED",
            attNm);
        illegal = 1;
      }

      if ((req->classCategory == SA_IMM_CLASS_RUNTIME) &&
          (attr->attrFlags & SA_IMM_ATTR_PERSISTENT)) {
        TRACE_5("PERSISTENT RT %s:%s", className.c_str(), attNm);
        persistentRt = true;
        if (attr->attrFlags & SA_IMM_ATTR_RDN) {
          TRACE_5("PERSISTENT RT && PERSISTENT RDN %s:%s", className.c_str(),
                  attNm);
          persistentRdn = true;
        }
      }

      if ((attr->attrFlags & SA_IMM_ATTR_NOTIFY) &&
          !(attr->attrFlags & SA_IMM_ATTR_CACHED)) {
        LOG_NO(
            "ERR_INVALID_PARAM: Runtime attribute:%s is not cached =>"
            "can not be delcared SA_IMM_ATTR_NOTIFY",
            attNm);
        illegal = 1;
      }
    } else {
      LOG_NO(
          "ERR_INVALID_PARAM: Attribute '%s' is neither SA_IMM_ATTR_CONFIG nor "
          "SA_IMM_ATTR_RUNTIME",
          attNm);
      illegal = 1;
    }

    if (attr->attrFlags & SA_IMM_ATTR_RDN) {
      ++rdns;

      if ((req->classCategory == SA_IMM_CLASS_CONFIG) &&
          (attr->attrFlags & SA_IMM_ATTR_RUNTIME)) {
        LOG_NO(
            "ERR_INVALID_PARAM: RDN '%s' of a configuration object "
            "can not be a runtime attribute",
            attNm);
        illegal = 1;
      }

      if (attr->attrFlags & SA_IMM_ATTR_WRITABLE) {
        LOG_NO(
            "ERR_INVALID_PARAM: RDN attribute '%s' can not be SA_IMM_ATTR_WRITABLE",
            attNm);
        illegal = 1;
      }

      if ((req->classCategory == SA_IMM_CLASS_RUNTIME) &&
          !(attr->attrFlags & SA_IMM_ATTR_CACHED)) {
        LOG_NO(
            "ERR_INVALID_PARAM: RDN '%s' of a runtime object must be declared "
            "cached",
            attNm);
        illegal = 1;
      }

      if (attr->attrFlags & SA_IMM_ATTR_NO_DANGLING) {
        LOG_NO("ERR_INVALID_PARAM: RDN '%s' cannot have NO_DANGLING flag",
               attNm);
        illegal = 1;
      }

      if (attr->attrFlags & SA_IMM_ATTR_DEFAULT_REMOVED) {
        LOG_NO(
            "ERR_INVALID_PARAM: RDN '%s' cannot have SA_IMM_ATTR_DEFAULT_REMOVED flag",
            attNm);
        illegal = 1;
      }

      if (attr->attrFlags & SA_IMM_ATTR_STRONG_DEFAULT) {
        LOG_NO(
            "ERR_INVALID_PARAM: RDN '%s' cannot have SA_IMM_ATTR_STRONG_DEFAULT flag",
            attNm);
        illegal = 1;
      }
    }

    if (attr->attrFlags & SA_IMM_ATTR_NO_DANGLING) {
      if (req->classCategory == SA_IMM_CLASS_RUNTIME) {
        LOG_NO(
            "ERR_INVALID_PARAM: Runtime object '%s' cannot have NO_DANGLING "
            "attribute",
            attNm);
        illegal = 1;
      }

      if (attr->attrValueType != SA_IMM_ATTR_SANAMET &&
          !((attr->attrFlags & SA_IMM_ATTR_DN) &&
            (attr->attrValueType == SA_IMM_ATTR_SASTRINGT))) {
        LOG_NO(
            "ERR_INVALID_PARAM: Attribute '%s' must be of type SaNameT, "
            "or of type SaStringT with DN flag",
            attNm);
        illegal = 1;
      }

      if (attr->attrFlags & SA_IMM_ATTR_RUNTIME) {
        LOG_NO(
            "ERR_INVALID_PARAM: Runtime attribute '%s' cannot have "
            "NO_DANGLING flag",
            attNm);
        illegal = 1;
      }
    }

    if ((attr->attrFlags & SA_IMM_ATTR_DN) &&
        (attr->attrValueType != SA_IMM_ATTR_SASTRINGT)) {
      LOG_NO(
          "ERR_INVALID_PARAM: Attribute '%s' has SA_IMM_ATTR_DN flag, "
          "but the attribute is not of type SaStringT",
          attNm);
      illegal = 1;
    }

    if ((attr->attrFlags & SA_IMM_ATTR_DEFAULT_REMOVED) && !isLoading &&
        !schemaChange) {
      LOG_NO(
          "ERR_INVALID_PARAM: Attribute '%s' has SA_IMM_ATTR_DEFAULT_REMOVED flag, "
          "but this flag is only available for schema changes",
          attNm);
      illegal = 1;
    }

    if (attr->attrFlags & SA_IMM_ATTR_STRONG_DEFAULT) {
      if (!attr->attrDefaultValue) {
        LOG_NO(
            "ERR_INVALID_PARAM: Attribute '%s' can not have SA_IMM_ATTR_STRONG_DEFAULT flag "
            "without having a default value",
            attNm);
        illegal = 1;
      }

      if (attr->attrFlags & SA_IMM_ATTR_DEFAULT_REMOVED) {
        LOG_NO(
            "ERR_INVALID_PARAM: Attribute '%s' can not have both SA_IMM_ATTR_STRONG_DEFAULT flag "
            "and SA_IMM_ATTR_DEFAULT_REMOVED flag",
            attNm);
        illegal = 1;
      }
    }

    if (attr->attrDefaultValue) {
      if (attr->attrFlags & SA_IMM_ATTR_RDN) {
        LOG_NO("ERR_INVALID_PARAM: RDN '%s' can not have a default", attNm);
        illegal = 1;
      }

      if ((attr->attrFlags & SA_IMM_ATTR_RUNTIME) &&
          !(attr->attrFlags & SA_IMM_ATTR_CACHED) &&
          !(attr->attrFlags & SA_IMM_ATTR_PERSISTENT)) {
        LOG_NO(
            "ERR_INVALID_PARAM: Runtime attribute '%s' is neither cached nor "
            "persistent => can not have a default",
            attNm);
        illegal = 1;
      }

      if (attr->attrFlags & SA_IMM_ATTR_NO_DANGLING) {
        if (isLoading) {
          LOG_WA(
              "Attribute '%s' of class '%s' has a default no-dangling reference, "
              "the class definition should be corrected",
              attNm, className.c_str());
        } else {
          LOG_NO(
              "ERR_INVALID_PARAM: No dangling attribute '%s' can not have a default",
              attNm);
          illegal = 1;
        }
      }

      if (attr->attrFlags & SA_IMM_ATTR_INITIALIZED) {
        LOG_NO(
            "ERR_INVALID_PARAM: Attribute %s declared as SA_IMM_ATTR_INITIALIZED "
            "inconsistent with having default",
            attNm);
        illegal = 1;
      }

      if (attr->attrFlags & SA_IMM_ATTR_DEFAULT_REMOVED) {
        LOG_NO(
            "ERR_INVALID_PARAM: Attribute '%s' can not have a default with SA_IMM_ATTR_DEFAULT_REMOVED",
            attNm);
        illegal = 1;
      }

      if (attr->attrValueType == SA_IMM_ATTR_SANAMET) {
        immsv_edu_attr_val* v = attr->attrDefaultValue;
        if (!getLongDnsAllowed() &&
            v->val.x.size >= SA_MAX_UNEXTENDED_NAME_LENGTH) {
          LOG_NO(
              "ERR_LIBRARY: attr '%s' of type SaNameT is too long:%u. "
              "Extended names is not enabled",
              attNm, v->val.x.size - 1);
          err = SA_AIS_ERR_LIBRARY;
          illegal = 1;
        } else if (v->val.x.size > kOsafMaxDnLength) {
          LOG_NO("ERR_LIBRARY: attr '%s' of type SaNameT is too long:%u", attNm,
                 v->val.x.size - 1);
          err = SA_AIS_ERR_LIBRARY;
          illegal = 1;
        } else {
          std::string tmpName(v->val.x.buf,
                              v->val.x.size ? v->val.x.size - 1 : 0);
          if (!(nameCheck(tmpName) || nameToInternal(tmpName))) {
            LOG_NO(
                "ERR_INVALID_PARAM: attr '%s' of type SaNameT contains non "
                "printable characters",
                attNm);
            err = SA_AIS_ERR_INVALID_PARAM;
            illegal = 1;
          }
        }
      } else if (attr->attrValueType == SA_IMM_ATTR_SASTRINGT) {
        immsv_edu_attr_val* v = attr->attrDefaultValue;
        if (v->val.x.size && !(osaf_is_valid_utf8(v->val.x.buf))) {
          LOG_NO(
              "ERR_INVALID_PARAM: Attribute '%s' defined on type SaStringT "
              "has a default value that is not valid UTF-8",
              attrName.c_str());
          illegal = 1;
        }
      }
    }
    err = attrCreate(classInfo, attr, attrName);
    if (err == SA_AIS_ERR_NOT_SUPPORTED) {
      useUnknownFlag = true;
      err = SA_AIS_OK;
    } else if (err != SA_AIS_OK) {
      illegal = 1;
    }
    list = list->next;
  }  // while(list)

  if (persistentRt && !persistentRdn) {
    LOG_NO(
        "ERR_INVALID_PARAM: Class for persistent runtime object requires "
        "persistent RDN");
    illegal = 1;
  }

  if (rdns != 1) {
    LOG_NO(
        "ERR_INVALID_PARAM: ONE and only ONE RDN attribute must be defined!");
    illegal = 1;
  }

  if (schemaChange && !illegal) {
    /*
     If all basic checks passed and this is an upgrade, do upgrade specific
     checks.
    */
    if (verifySchemaChange(className, prevClassInfo, classInfo, newAttrs,
                           changedAttrs)) {
      LOG_NO(
          "Schema change for class %s ACCEPTED. Adding %u and changing %u attribute defs",
          className.c_str(), (unsigned int)newAttrs.size(),
          (unsigned int)changedAttrs.size());
    } else {
      LOG_NO("ERR_EXIST: Class '%s' exist - possible failed schema upgrade",
             className.c_str());
      err = SA_AIS_ERR_EXIST;
      illegal = 1;
    }
  }

  if (illegal) {
    if (err == SA_AIS_OK) {
      LOG_NO("ERR_INVALID_PARAM: Problem with new class '%s'",
             className.c_str());
      err = SA_AIS_ERR_INVALID_PARAM;
    }

    AttrMap::iterator ai = classInfo->mAttrMap.begin();
    while (ai != classInfo->mAttrMap.end()) {
      AttrInfo* ainfo = ai->second;
      osafassert(ainfo);
      delete (ainfo);
      ++ai;
    }
    classInfo->mAttrMap.clear();

    if (!schemaChange) {
      delete classInfo;
    }
    classInfo = NULL;
    goto done;
  }

  if (useUnknownFlag) {
    LOG_NO("At least one attribute in class %s has unsupported attribute flag",
           className.c_str());
  }

  /* All checks passed, now install the class def. */

  if (!schemaChange) {
    /* Normal case, install the brand new class. */
    sClassMap[className] = classInfo;
    updateImmObject(className);
  } else {
    /* Schema upgrade case, Change the attr defs. */
    AttrMap::iterator ai;
    AttrMap::iterator pcai;  // for prevClassInfo->mAttrMap
    AttrInfo* ainfo = NULL;
    ObjectSet::iterator oi;
    bool hasNoDanglingRefChanges = false;
    osafassert(prevClassInfo);

    /* Add NO_DANGLING flag changes */
    for (ai = newAttrs.begin(); ai != newAttrs.end(); ++ai) {
      if (ai->second->mFlags & SA_IMM_ATTR_NO_DANGLING) {
        hasNoDanglingRefChanges = true;
        break;
      }
    }
    if (!hasNoDanglingRefChanges) {
      for (ai = changedAttrs.begin(); ai != changedAttrs.end(); ++ai) {
        if (ai->second->mFlags & SA_IMM_ATTR_NO_DANGLING) {
          hasNoDanglingRefChanges = true;
          break;
        }
        // Check NO_DANGLING flag of the attribute in the previous class
        if ((pcai = prevClassInfo->mAttrMap.find(ai->first)) !=
                prevClassInfo->mAttrMap.end() &&
            (pcai->second->mFlags & SA_IMM_ATTR_NO_DANGLING)) {
          hasNoDanglingRefChanges = true;
          break;
        }
      }
    }

    if (hasNoDanglingRefChanges) {
      // Remove all no dangling references from all objects of the class
      for (oi = prevClassInfo->mExtent.begin();
           oi != prevClassInfo->mExtent.end(); ++oi) {
        removeNoDanglingRefs(*oi, *oi);
      }
    }

    /* Remove old attr defs. */
    ai = prevClassInfo->mAttrMap.begin();
    while (ai != prevClassInfo->mAttrMap.end()) {
      TRACE_5("Removing old attribute %s:%s", className.c_str(),
              ai->first.c_str());
      ainfo = ai->second;
      osafassert(ainfo);
      delete (ainfo);
      ++ai;
    }
    prevClassInfo->mAttrMap.clear();

    /* Move new attr defs from dummyClass to existing ClassInfo object.
       This leaves references from existing instances to the ClassInfo intact.
    */
    for (ai = dummyClass.mAttrMap.begin(); ai != dummyClass.mAttrMap.end();
         ++ai) {
      TRACE_5("Inserting attribute %s:%s", className.c_str(),
              ai->first.c_str());
      prevClassInfo->mAttrMap[ai->first] = ai->second;
    }
    dummyClass.mAttrMap.clear();

    /* Migrate instances. */
    if ((prevClassInfo->mExtent.empty())) {
      LOG_NO("No instances to migrate - schema change could have been avoided");
    } else { /* There are instances. */
      CcbVector::iterator ci;
      ObjectMutationMap::iterator omit;
      ObjectMutation* omut = NULL;

      /* Migrate current version of instances. */
      for (oi = prevClassInfo->mExtent.begin();
           oi != prevClassInfo->mExtent.end(); ++oi) {
        osafassert((*oi)->mClassInfo == prevClassInfo);
        migrateObj(*oi, className, newAttrs, changedAttrs);
      }

      /* Migrate new versions of modify of instances, in active CCBs */
      for (ci = sCcbVector.begin(); ci != sCcbVector.end(); ++ci) {
        CcbInfo* ccb = (*ci);
        if (ccb->isActive()) {
          for (omit = ccb->mMutations.begin(); omit != ccb->mMutations.end();
               ++omit) {
            omut = omit->second;
            if (omut->mOpType == IMM_MODIFY && omut->mAfterImage &&
                omut->mAfterImage->mClassInfo == prevClassInfo) {
              migrateObj(omut->mAfterImage, className, newAttrs, changedAttrs);
            }
          }
        }
      }

      /* Migrate new versions of modify of instances for ongoing PBE PRTA data
       * updates */
      for (omit = sPbeRtMutations.begin(); omit != sPbeRtMutations.end();
           ++omit) {
        omut = omit->second;
        if (omut->mOpType == IMM_MODIFY && omut->mAfterImage &&
            omut->mAfterImage->mClassInfo == prevClassInfo) {
          migrateObj(omut->mAfterImage, className, newAttrs, changedAttrs);
        }
      }

      if (hasNoDanglingRefChanges) {
        // Set back no dangling references for all objects
        for (oi = prevClassInfo->mExtent.begin();
             oi != prevClassInfo->mExtent.end(); ++oi) {
          addNoDanglingRefs(*oi);
        }
      }
    }

    LOG_NO("Schema change completed for class %s %s", className.c_str(),
           pbeNodeIdPtr ? "(PBE changes still pending)." : "");
  } /* end of schema upgrade case. */

  if (pbeNodeIdPtr) {
    if (!getPbeOi(pbeConnPtr, pbeNodeIdPtr)) {
      LOG_ER("Pbe is not available, can not happen here");
      abort();
    }
    if (++sLastContinuationId >= 0xfffffffe) {
      sLastContinuationId = 1;
    }
    (*continuationIdPtr) = sLastContinuationId;

    osafassert(sPbeRtMutations.find(className) == sPbeRtMutations.end());
    /* Create a mutation record to bar pbe restart --recover  */

    ObjectMutation* oMut = new ObjectMutation(IMM_CREATE_CLASS);
    oMut->mContinuationId = (*continuationIdPtr);
    oMut->mAfterImage = NULL;
    sPbeRtMutations[className] = oMut;

    if (reqConn) {
      SaInvocationT tmp_hdl = m_IMMSV_PACK_HANDLE((*continuationIdPtr), nodeId);
      sPbeRtReqContinuationMap[tmp_hdl] =
          ContinuationInfo2(reqConn, DEFAULT_TIMEOUT_SEC);
    }
  }

done:
  TRACE_LEAVE();
  return err;
}

void ImmModel::migrateObj(ObjectInfo* object, std::string className,
                          AttrMap& newAttrs, AttrMap& changedAttrs) {
  AttrMap::iterator ai;
  ImmAttrValue* attrValue = NULL;
  std::string
      objectDn; /* objectDn only used for trace/log => external rep ok. */
  getObjectName(object, objectDn);

  TRACE_5("Migrating %s object %s", className.c_str(), objectDn.c_str());

  /* Add new attributes to instances. */
  for (ai = newAttrs.begin(); ai != newAttrs.end(); ++ai) {
    AttrInfo* attr = ai->second;

    if (attr->mFlags & SA_IMM_ATTR_MULTI_VALUE) {
      if (attr->mDefaultValue.empty()) {
        attrValue = new ImmAttrMultiValue();
      } else {
        attrValue = new ImmAttrMultiValue(attr->mDefaultValue);
      }
    } else {
      if (attr->mDefaultValue.empty()) {
        attrValue = new ImmAttrValue();
      } else {
        attrValue = new ImmAttrValue(attr->mDefaultValue);
      }
    }
    object->mAttrValueMap[ai->first] = attrValue;
  }

  /* Adjust existing attributes.*/
  ImmAttrValueMap::iterator oavi;
  for (oavi = object->mAttrValueMap.begin();
       oavi != object->mAttrValueMap.end(); ++oavi) {
    /*
       TRACE_5("CHECKING existing attribute %s:%s for object %s",
       className.c_str(), oavi->first.c_str(), objectDn.c_str());
    */

    /* Change from single to multi-value requires change of value rep.*/
    ai = changedAttrs.find(oavi->first);
    if (ai != changedAttrs.end()) {
      if ((ai->second->mFlags & SA_IMM_ATTR_MULTI_VALUE) &&
          (!oavi->second->isMultiValued())) {
        attrValue = new ImmAttrMultiValue(*(oavi->second));
        TRACE_5(
            "Schema change adjusted attribute %s in object:%s "
            "to be multivalued",
            oavi->first.c_str(), objectDn.c_str());
        delete oavi->second;
        object->mAttrValueMap[ai->first] = attrValue;
      }
    }

    /* Correct object->mAdminOwnerAttrVal. */
    if (oavi->first == std::string(SA_IMM_ATTR_ADMIN_OWNER_NAME)) {
      object->mAdminOwnerAttrVal = oavi->second;
      TRACE_5("Schema change corrected attr %s in object:%s",
              oavi->first.c_str(), objectDn.c_str());
    }
  }
}

bool ImmModel::oneSafe2PBEAllowed() {
  // TRACE_ENTER();
  ObjectMap::iterator oi = sObjectMap.find(immObjectDn);
  if (oi == sObjectMap.end()) {
    // TRACE_LEAVE();
    return false;
  }

  ObjectInfo* immObject = oi->second;
  ImmAttrValueMap::iterator avi =
      immObject->mAttrValueMap.find(immAttrNostFlags);
  osafassert(avi != immObject->mAttrValueMap.end());
  osafassert(!(avi->second->isMultiValued()));
  ImmAttrValue* valuep = avi->second;
  unsigned int noStdFlags = valuep->getValue_int();

  // TRACE_LEAVE();
  return noStdFlags & OPENSAF_IMM_FLAG_2PBE1_ALLOW;
}

bool ImmModel::protocol43Allowed() {
  // TRACE_ENTER();
  ObjectMap::iterator oi = sObjectMap.find(immObjectDn);
  if (oi == sObjectMap.end()) {
    // TRACE_LEAVE();
    return false;
  }

  ObjectInfo* immObject = oi->second;
  ImmAttrValueMap::iterator avi =
      immObject->mAttrValueMap.find(immAttrNostFlags);
  osafassert(avi != immObject->mAttrValueMap.end());
  osafassert(!(avi->second->isMultiValued()));
  ImmAttrValue* valuep = avi->second;
  unsigned int noStdFlags = valuep->getValue_int();

  // TRACE_LEAVE();
  return noStdFlags & OPENSAF_IMM_FLAG_PRT43_ALLOW;
}

bool ImmModel::protocol45Allowed() {
  // TRACE_ENTER();
  ObjectMap::iterator oi = sObjectMap.find(immObjectDn);
  if (oi == sObjectMap.end()) {
    // TRACE_LEAVE();
    return false;
  }

  ObjectInfo* immObject = oi->second;
  ImmAttrValueMap::iterator avi =
      immObject->mAttrValueMap.find(immAttrNostFlags);
  osafassert(avi != immObject->mAttrValueMap.end());
  osafassert(!(avi->second->isMultiValued()));
  ImmAttrValue* valuep = avi->second;
  unsigned int noStdFlags = valuep->getValue_int();

  // TRACE_LEAVE();
  return noStdFlags & OPENSAF_IMM_FLAG_PRT45_ALLOW;
}

bool ImmModel::protocol46Allowed() {
  // TRACE_ENTER();
  ObjectMap::iterator oi = sObjectMap.find(immObjectDn);
  if (oi == sObjectMap.end()) {
    // TRACE_LEAVE();
    return false;
  }

  ObjectInfo* immObject = oi->second;
  ImmAttrValueMap::iterator avi =
      immObject->mAttrValueMap.find(immAttrNostFlags);
  osafassert(avi != immObject->mAttrValueMap.end());
  osafassert(!(avi->second->isMultiValued()));
  ImmAttrValue* valuep = avi->second;
  unsigned int noStdFlags = valuep->getValue_int();

  // TRACE_LEAVE();
  return noStdFlags & OPENSAF_IMM_FLAG_PRT46_ALLOW;
}

bool ImmModel::protocol47Allowed() {
  // TRACE_ENTER();
  ObjectMap::iterator oi = sObjectMap.find(immObjectDn);
  if (oi == sObjectMap.end()) {
    // TRACE_LEAVE();
    return false;
  }

  ObjectInfo* immObject = oi->second;
  ImmAttrValueMap::iterator avi =
      immObject->mAttrValueMap.find(immAttrNostFlags);
  osafassert(avi != immObject->mAttrValueMap.end());
  osafassert(!(avi->second->isMultiValued()));
  ImmAttrValue* valuep = avi->second;
  unsigned int noStdFlags = valuep->getValue_int();

  // TRACE_LEAVE();
  return noStdFlags & OPENSAF_IMM_FLAG_PRT47_ALLOW;
}

bool ImmModel::protocol50Allowed() {
  // TRACE_ENTER();
  /* Assume that all nodes are running the same version when loading */
  if (sImmNodeState == IMM_NODE_LOADING) {
    return true;
  }
  ObjectMap::iterator oi = sObjectMap.find(immObjectDn);
  if (oi == sObjectMap.end()) {
    // TRACE_LEAVE();
    return false;
  }

  ObjectInfo* immObject = oi->second;
  ImmAttrValueMap::iterator avi =
      immObject->mAttrValueMap.find(immAttrNostFlags);
  osafassert(avi != immObject->mAttrValueMap.end());
  osafassert(!(avi->second->isMultiValued()));
  ImmAttrValue* valuep = avi->second;
  unsigned int noStdFlags = valuep->getValue_int();

  // TRACE_LEAVE();
  return noStdFlags & OPENSAF_IMM_FLAG_PRT50_ALLOW;
}

bool ImmModel::protocol51Allowed() {
  // TRACE_ENTER();
  /* Assume that all nodes are running the same version when loading */
  if (sImmNodeState == IMM_NODE_LOADING) {
    return true;
  }
  ObjectMap::iterator oi = sObjectMap.find(immObjectDn);
  if (oi == sObjectMap.end()) {
    // TRACE_LEAVE();
    return false;
  }

  ObjectInfo* immObject = oi->second;
  ImmAttrValueMap::iterator avi =
      immObject->mAttrValueMap.find(immAttrNostFlags);
  osafassert(avi != immObject->mAttrValueMap.end());
  osafassert(!(avi->second->isMultiValued()));
  ImmAttrValue* valuep = avi->second;
  unsigned int noStdFlags = valuep->getValue_int();

  // TRACE_LEAVE();
  return noStdFlags & OPENSAF_IMM_FLAG_PRT51_ALLOW;
}

bool ImmModel::protocol51710Allowed() {
  // TRACE_ENTER();
  /* Assume that all nodes are running the same version when loading */
  if (sImmNodeState == IMM_NODE_LOADING) {
    return true;
  }
  ObjectMap::iterator oi = sObjectMap.find(immObjectDn);
  if (oi == sObjectMap.end()) {
    // TRACE_LEAVE();
    return false;
  }

  ObjectInfo* immObject = oi->second;
  ImmAttrValueMap::iterator avi =
      immObject->mAttrValueMap.find(immAttrNostFlags);
  osafassert(avi != immObject->mAttrValueMap.end());
  osafassert(!(avi->second->isMultiValued()));
  ImmAttrValue* valuep = avi->second;
  unsigned int noStdFlags = valuep->getValue_int();

  // TRACE_LEAVE();
  return noStdFlags & OPENSAF_IMM_FLAG_PRT51710_ALLOW;
}

bool ImmModel::protocol41Allowed() {
  // TRACE_ENTER();
  ObjectMap::iterator oi = sObjectMap.find(immObjectDn);
  if (oi == sObjectMap.end()) {
    // TRACE_LEAVE();
    return false;
  }

  ObjectInfo* immObject = oi->second;
  ImmAttrValueMap::iterator avi =
      immObject->mAttrValueMap.find(immAttrNostFlags);
  osafassert(avi != immObject->mAttrValueMap.end());
  osafassert(!(avi->second->isMultiValued()));
  ImmAttrValue* valuep = avi->second;
  unsigned int noStdFlags = valuep->getValue_int();

  // TRACE_LEAVE();
  return noStdFlags & OPENSAF_IMM_FLAG_PRT41_ALLOW;
}

bool ImmModel::schemaChangeAllowed() {
  TRACE_ENTER();
  ObjectMap::iterator oi = sObjectMap.find(immObjectDn);
  if (oi == sObjectMap.end()) {
    TRACE_LEAVE();
    return false;
  }

  ObjectInfo* immObject = oi->second;
  ImmAttrValueMap::iterator avi =
      immObject->mAttrValueMap.find(immAttrNostFlags);
  osafassert(avi != immObject->mAttrValueMap.end());
  osafassert(!(avi->second->isMultiValued()));
  ImmAttrValue* valuep = avi->second;
  unsigned int noStdFlags = valuep->getValue_int();

  TRACE_LEAVE();
  return noStdFlags & OPENSAF_IMM_FLAG_SCHCH_ALLOW;
}

OsafImmAccessControlModeT ImmModel::accessControlMode() {
  TRACE_ENTER();
  ObjectMap::iterator oi = sObjectMap.find(immObjectDn);
  if (oi == sObjectMap.end()) {
    TRACE_LEAVE();
    return ACCESS_CONTROL_DISABLED;
  }

  ObjectInfo* immObject = oi->second;
  ImmAttrValueMap::iterator avi =
      immObject->mAttrValueMap.find(immAccessControlMode);
  if (avi == immObject->mAttrValueMap.end()) {
    return ACCESS_CONTROL_DISABLED;
  }
  osafassert(!(avi->second->isMultiValued()));
  ImmAttrValue* valuep = avi->second;
  OsafImmAccessControlModeT accessControlMode =
      static_cast<OsafImmAccessControlModeT>(valuep->getValue_int());

  TRACE_LEAVE2("%u", accessControlMode);
  return accessControlMode;
}

const char* ImmModel::authorizedGroup() {
  TRACE_ENTER();
  ObjectMap::iterator oi = sObjectMap.find(immObjectDn);
  if (oi == sObjectMap.end()) {
    TRACE_LEAVE();
    return NULL;
  }

  ObjectInfo* immObject = oi->second;
  ImmAttrValueMap::iterator avi =
      immObject->mAttrValueMap.find(immAuthorizedGroup);
  if (avi == immObject->mAttrValueMap.end()) {
    return NULL;
  }
  osafassert(!(avi->second->isMultiValued()));
  ImmAttrValue* valuep = avi->second;
  const char* adminGroupName = valuep->getValueC_str();

  TRACE_LEAVE();
  return adminGroupName;
}

/**
 * Verify that a class create with same name as an existing class is a legal
 * schema upgrade.
 */
bool ImmModel::verifySchemaChange(const std::string& className,
                                  ClassInfo* oldClassInfo,
                                  ClassInfo* newClassInfo, AttrMap& newAttrs,
                                  AttrMap& changedAttrs) {
  AttrMap::iterator iold;
  AttrMap::iterator inew;
  bool verifyFailed = false;
  TRACE_ENTER2("ClassName:%s", className.c_str());
  osafassert(oldClassInfo && newClassInfo);
  unsigned int oldCount = (unsigned int)oldClassInfo->mAttrMap.size();
  unsigned int newCount = (unsigned int)newClassInfo->mAttrMap.size();
  if (oldCount > newCount) {
    LOG_NO(
        "Impossible upgrade, new class descr for '%s' has fewer "
        "attributes %u than existing %u.",
        className.c_str(), newCount, oldCount);
    return false;
  }

  if (oldClassInfo->mCategory != newClassInfo->mCategory) {
    LOG_NO(
        "Impossible upgrade, new class descr for '%s' not of "
        "same category as existing.",
        className.c_str());
    return false;
  }

  /* Verify that all old attrs exist in new. */

  for (iold = oldClassInfo->mAttrMap.begin();
       iold != oldClassInfo->mAttrMap.end(); ++iold) {
    std::string attName = iold->first;
    inew = newClassInfo->mAttrMap.find(attName);
    if (inew == newClassInfo->mAttrMap.end()) {
      LOG_NO("Attribute %s missing in new class def.", attName.c_str());
      verifyFailed = true;
    } else {
      --oldCount;
    }
  }

  if (verifyFailed) {
    return false;
  }

  osafassert(oldCount == 0); /* Yes this check is redundant. */

  /* Check compatibility for all attrdefs in new class def. */
  for (inew = newClassInfo->mAttrMap.begin();
       inew != newClassInfo->mAttrMap.end(); ++inew) {
    std::string attName = inew->first;
    AttrInfo* newAttr = inew->second;
    iold = oldClassInfo->mAttrMap.find(attName);
    if (iold == oldClassInfo->mAttrMap.end()) {
      LOG_NO("New attribute %s added by new class def", attName.c_str());
      verifyFailed = notCompatibleAtt(className, newClassInfo, attName, NULL,
                                      newAttr, NULL) ||
                     verifyFailed;
      newAttrs[inew->first] = newAttr;
      if (!verifyFailed && (className == immClassName)) {
        unsigned int val;
        if (attName == immMaxClasses) {
          val = newAttr->mDefaultValue.getValue_int();
          if (sClassMap.size() > val) {
            LOG_NO(
                "The Number of classes in the cluster %zu greater than the schema change"
                "value %d",
                sClassMap.size(), val);
            verifyFailed = true;
          }
        }

        if (!verifyFailed && attName == immMaxImp) {
          val = newAttr->mDefaultValue.getValue_int();
          if (sImplementerVector.size() > val) {
            LOG_NO(
                "The Number of Implementers in the cluster %zu greater than the schema change"
                "value %d",
                sImplementerVector.size(), val);
            verifyFailed = true;
          }
        }

        if (!verifyFailed && attName == immMaxAdmOwn) {
          val = newAttr->mDefaultValue.getValue_int();
          if (sOwnerVector.size() > val) {
            LOG_NO(
                "The Number of AdminOwners in the cluster %zu greater than the schema change"
                "value %d",
                sOwnerVector.size(), val);
            verifyFailed = true;
          }
        }

        if (!verifyFailed && attName == immMaxCcbs) {
          val = newAttr->mDefaultValue.getValue_int();
          if ((sCcbVector.size() - sTerminatedCcbcount) > val) {
            LOG_NO(
                "The Number of Ccbs in the cluster %zu greater than the schema change"
                "value %d",
                sCcbVector.size(), val);
            verifyFailed = true;
          }
        }
      }
    } else {
      TRACE_5("Existing attribute %s", attName.c_str());
      verifyFailed = notCompatibleAtt(className, newClassInfo, attName,
                                      iold->second, newAttr, &changedAttrs) ||
                     verifyFailed;
    }
  }

  if (!verifyFailed && newAttrs.empty() && changedAttrs.empty()) {
    LOG_WA("New class def is same as old, not a real schema upgrade");
    verifyFailed = true;
  }

  TRACE_LEAVE();
  return !verifyFailed;
}

bool ImmModel::notCompatibleAtt(const std::string& className,
                                ClassInfo* newClassInfo,
                                const std::string& attName,
                                const AttrInfo* oldAttr, AttrInfo* newAttr,
                                AttrMap* changedAttrs) {
  if (oldAttr) {
    /* Existing attribute, possibly changed. */
    bool change = false;
    bool checkCcb = false;
    bool checkNoDup = false;
    bool checkNoDanglingRefs = false;
    bool checkStrongDefault = false;
    osafassert(changedAttrs);
    if (oldAttr->mValueType != newAttr->mValueType) {
      LOG_NO("Impossible upgrade, attribute %s:%s changes value type",
             className.c_str(), attName.c_str());
      return true;
    }

    if (oldAttr->mFlags != newAttr->mFlags) {
      if ((oldAttr->mFlags & SA_IMM_ATTR_RUNTIME) !=
          (newAttr->mFlags & SA_IMM_ATTR_RUNTIME)) {
        LOG_NO(
            "Impossible upgrade, attribute %s:%s changes flag "
            "SA_IMM_ATTR_RUNTIME",
            className.c_str(), attName.c_str());
        return true;
      }

      if ((oldAttr->mFlags & SA_IMM_ATTR_CONFIG) !=
          (newAttr->mFlags & SA_IMM_ATTR_CONFIG)) {
        LOG_NO(
            "Impossible upgrade, attribute %s:%s changes flag "
            "SA_IMM_ATTR_CONFIG",
            className.c_str(), attName.c_str());
        return true;
      }

      if ((oldAttr->mFlags & SA_IMM_ATTR_CACHED) !=
          (newAttr->mFlags & SA_IMM_ATTR_CACHED)) {
        LOG_NO(
            "Impossible upgrade, attribute %s:%s changes flag "
            "SA_IMM_ATTR_CACHED",
            className.c_str(), attName.c_str());
        return true;
      }

      if ((oldAttr->mFlags & SA_IMM_ATTR_RDN) !=
          (newAttr->mFlags & SA_IMM_ATTR_RDN)) {
        LOG_NO(
            "Impossible upgrade, attribute %s:%s changes flag "
            "SA_IMM_ATTR_RDN",
            className.c_str(), attName.c_str());
        return true;
      }

      if ((oldAttr->mFlags & SA_IMM_ATTR_MULTI_VALUE) &&
          !(newAttr->mFlags & SA_IMM_ATTR_MULTI_VALUE)) {
        LOG_NO(
            "Impossible upgrade, attribute %s:%s removes flag "
            "SA_IMM_ATTR_MULTI_VALUE",
            className.c_str(), attName.c_str());
        return true;
      }

      if (!(oldAttr->mFlags & SA_IMM_ATTR_MULTI_VALUE) &&
          (newAttr->mFlags & SA_IMM_ATTR_MULTI_VALUE)) {
        LOG_NO(
            "Allowed upgrade, attribute %s:%s adds flag "
            "SA_IMM_ATTR_MULTI_VALUE",
            className.c_str(), attName.c_str());

        change = true; /* Instances NEED migration. */
      }

      if ((oldAttr->mFlags & SA_IMM_ATTR_NO_DUPLICATES) &&
          !(newAttr->mFlags & SA_IMM_ATTR_NO_DUPLICATES)) {
        LOG_NO(
            "Allowed upgrade, attribute %s:%s removes flag "
            "SA_IMM_ATTR_NO_DUPLICATES",
            className.c_str(), attName.c_str());
        change = true; /* Instances dont need migration. */
      }

      if (!(oldAttr->mFlags & SA_IMM_ATTR_NO_DUPLICATES) &&
          (newAttr->mFlags & SA_IMM_ATTR_NO_DUPLICATES)) {
        LOG_NO(
            "Allowed upgrade, attribute %s:%s adds flag "
            "SA_IMM_ATTR_NO_DUPLICATES",
            className.c_str(), attName.c_str());

        change = true;     /* Instances dont need migration. */
        checkCcb = true;   /* Check for ccb interference. */
        checkNoDup = true; /* Instances need validation of no dup */
      }

      if ((oldAttr->mFlags & SA_IMM_ATTR_WRITABLE) &&
          !(newAttr->mFlags & SA_IMM_ATTR_WRITABLE)) {
        LOG_NO(
            "Impossible upgrade, attribute %s:%s removes flag "
            "SA_IMM_ATTR_WRITABLE",
            className.c_str(), attName.c_str());
        return true;
      }

      if (!(oldAttr->mFlags & SA_IMM_ATTR_WRITABLE) &&
          (newAttr->mFlags & SA_IMM_ATTR_WRITABLE)) {
        LOG_NO(
            "Allowed upgrade, attribute %s:%s adds flag "
            "SA_IMM_ATTR_WRITABLE",
            className.c_str(), attName.c_str());

        change = true; /* Instances dont need migration. */
      }

      if (!(oldAttr->mFlags & SA_IMM_ATTR_INITIALIZED) &&
          (newAttr->mFlags & SA_IMM_ATTR_INITIALIZED)) {
        LOG_NO(
            "Impossible upgrade, attribute %s:%s adds flag "
            "SA_IMM_ATTR_INITIALIZED",
            className.c_str(), attName.c_str());
        return true;
      }

      if ((oldAttr->mFlags & SA_IMM_ATTR_INITIALIZED) &&
          !(newAttr->mFlags & SA_IMM_ATTR_INITIALIZED)) {
        LOG_NO(
            "Allowed upgrade, attribute %s:%s removes flag "
            "SA_IMM_ATTR_INITIALIZED",
            className.c_str(), attName.c_str());

        change = true; /* Instances dont need migration. */
      }

      if (!(oldAttr->mFlags & SA_IMM_ATTR_PERSISTENT) &&
          (newAttr->mFlags & SA_IMM_ATTR_PERSISTENT)) {
        LOG_NO(
            "Impossible upgrade, attribute %s:%s adds flag "
            "SA_IMM_ATTR_PERSISTENT",
            className.c_str(), attName.c_str());
        return true;
      }

      if ((oldAttr->mFlags & SA_IMM_ATTR_PERSISTENT) &&
          !(newAttr->mFlags & SA_IMM_ATTR_PERSISTENT)) {
        LOG_NO(
            "Allowed upgrade, attribute %s:%s removes flag "
            "SA_IMM_ATTR_PERSISTENT",
            className.c_str(), attName.c_str());

        change = true;   /* Instances dont need migration. */
        checkCcb = true; /* Check for ccb interference. */
      }

      if (!(oldAttr->mFlags & SA_IMM_ATTR_NOTIFY) &&
          (newAttr->mFlags & SA_IMM_ATTR_NOTIFY)) {
        /* Check for CACHED on RTAs done in basic check. */
        LOG_NO(
            "Allowed upgrade, attribute %s:%s adds flag "
            "SA_IMM_ATTR_NOTIFY",
            className.c_str(), attName.c_str());
        change = true;   /* Instances dont need migration. */
        checkCcb = true; /* Check for ccb interference. */
      }

      if ((oldAttr->mFlags & SA_IMM_ATTR_NOTIFY) &&
          !(newAttr->mFlags & SA_IMM_ATTR_NOTIFY)) {
        LOG_NO(
            "Allowed upgrade, attribute %s:%s removes flag "
            "SA_IMM_ATTR_NOTIFY",
            className.c_str(), attName.c_str());
        change = true;   /* Instances dont need migration. */
        checkCcb = true; /* Check for ccb interference. */
      }

      if (!(oldAttr->mFlags & SA_IMM_ATTR_NO_DANGLING) &&
          (newAttr->mFlags & SA_IMM_ATTR_NO_DANGLING)) {
        LOG_NO(
            "Allowed upgrade, attribute %s:%s adds flag "
            "SA_IMM_ATTR_NO_DANGLING",
            className.c_str(), attName.c_str());
        change = true;
        checkCcb = true;
        checkNoDanglingRefs = true;
      }

      if ((oldAttr->mFlags & SA_IMM_ATTR_NO_DANGLING) &&
          !(newAttr->mFlags & SA_IMM_ATTR_NO_DANGLING)) {
        LOG_NO(
            "Allowed upgrade, attribute %s:%s removes flag "
            "SA_IMM_ATTR_NO_DANGLING",
            className.c_str(), attName.c_str());
        change = true;
        checkCcb = true;
        checkNoDanglingRefs = true;
      }

      if ((oldAttr->mFlags & SA_IMM_ATTR_DN) !=
          (newAttr->mFlags & SA_IMM_ATTR_DN)) {
        LOG_NO(
            "Impossible upgrade, attribute %s:%s changes flag "
            "SA_IMM_ATTR_DN",
            className.c_str(), attName.c_str());
        return true;
      }

      if (!(oldAttr->mFlags & SA_IMM_ATTR_DEFAULT_REMOVED) &&
          (newAttr->mFlags & SA_IMM_ATTR_DEFAULT_REMOVED)) {
        if (oldAttr->mDefaultValue.empty()) {
          LOG_NO(
              "Impossible upgrade, attribute %s:%s doesn't have default, "
              "can not add SA_IMM_ATTR_DEFAULT_REMOVED",
              className.c_str(), attName.c_str());
          return true;
        } else {
          LOG_NO(
              "Allowed upgrade, attribute %s:%s adds flag "
              "SA_IMM_ATTR_DEFAULT_REMOVED",
              className.c_str(), attName.c_str());
          change = true;
        }
      }

      if ((oldAttr->mFlags & SA_IMM_ATTR_DEFAULT_REMOVED) &&
          !(newAttr->mFlags & SA_IMM_ATTR_DEFAULT_REMOVED)) {
        if (newAttr->mDefaultValue.empty()) {
          LOG_NO(
              "Impossible upgrade, can not remove SA_IMM_ATTR_DEFAULT_REMOVED "
              "without adding new default for attribute %s:%s",
              className.c_str(), attName.c_str());
          return true;
        } else {
          LOG_NO(
              "Allowed upgrade, attribute %s:%s removes flag "
              "SA_IMM_ATTR_DEFAULT_REMOVED",
              className.c_str(), attName.c_str());
          change = true;
        }
      }

      if (!(oldAttr->mFlags & SA_IMM_ATTR_STRONG_DEFAULT) &&
          (newAttr->mFlags & SA_IMM_ATTR_STRONG_DEFAULT) &&
          protocol50Allowed()) {
        LOG_NO(
            "Allowed upgrade, attribute %s:%s adds flag "
            "SA_IMM_ATTR_STRONG_DEFAULT",
            className.c_str(), attName.c_str());
        checkCcb = true;
        checkStrongDefault = true;
        change = true;
      }

      if ((oldAttr->mFlags & SA_IMM_ATTR_STRONG_DEFAULT) &&
          !(newAttr->mFlags & SA_IMM_ATTR_STRONG_DEFAULT)) {
        LOG_NO(
            "Allowed upgrade, attribute %s:%s removes flag "
            "SA_IMM_ATTR_STRONG_DEFAULT",
            className.c_str(), attName.c_str());
        change = true;
      }
    }

    osafassert(!checkNoDup || checkCcb);  // Duplicate-check implies ccb-check

    if (checkCcb) {
      /* Check for ccb-interference. Changing ATTR_NOTIFY or ATTR_PERSISTENT
         or adding NO_DUPLICATES when there is an on-going ccb that is still
         open (not critical) and is operating on instances of the class could
         cause partial and incomplete notification/persistification/unique-check
         relative to the CCB. Abort the ccb in that case. NO_DUPLICATES also
         need to check for interference in ccbs that are in critical (waiting on
         PBE) because such a CCB can not be aborted and could be adding
         duplicates just as NO_DUPLICATES is added to an attribute by this
         schema change.
      */
      CcbVector::iterator i;
      ClassMap::iterator i3 = sClassMap.find(className);
      osafassert(i3 != sClassMap.end());
      ClassInfo* oldClassInfo = i3->second;
      ObjectInfo* obj = NULL;

      for (i = sCcbVector.begin(); i != sCcbVector.end(); ++i) {
        CcbInfo* ccb = (*i);
        if (ccb->mState <= IMM_CCB_CRITICAL && ccb->mVeto == SA_AIS_OK) {
          ObjectMutationMap::iterator omit;
          for (omit = ccb->mMutations.begin(); omit != ccb->mMutations.end();
               ++omit) {
            ObjectMap::iterator oi = sObjectMap.find(omit->first);
            osafassert(oi != sObjectMap.end());
            obj = oi->second;
            if (obj->mClassInfo == oldClassInfo) {
              if (ccb->mState < IMM_CCB_CRITICAL) {
                LOG_NO(
                    "Ccb %u is active on object '%s', interferes with "
                    "class change for class: %s attr: %s. Aborting Ccb.",
                    ccb->mId, omit->first.c_str(), className.c_str(),
                    attName.c_str());
                setCcbErrorString(ccb,
                                  IMM_RESOURCE_ABORT
                                  "Class change for class: %s, attr: %s",
                                  className.c_str(), attName.c_str());
                ccb->mVeto = SA_AIS_ERR_FAILED_OPERATION;
              } else {
                osafassert(ccb->mState == IMM_CCB_CRITICAL);
                LOG_NO(
                    "Ccb %u in critical state (can not abort) is active "
                    "on object '%s', interferes with class change for "
                    "class: %s attr: %s. Aborting class change",
                    ccb->mId, omit->first.c_str(), className.c_str(),
                    attName.c_str());
                return true;
              }
              break; /* Out of inner loop. */
            }        // if
          }          // for
        }            // if
      }              // for

      if (checkNoDup) {
        /* Screen all instances of class for no duplicates on attr. */
        ObjectSet::iterator osi = oldClassInfo->mExtent.begin();
        for (; osi != oldClassInfo->mExtent.end(); ++osi) {
          obj = *osi;
          ImmAttrValueMap::iterator oavi = obj->mAttrValueMap.find(attName);
          osafassert(oavi != obj->mAttrValueMap.end());
          osafassert(oavi->second->isMultiValued());
          if (oavi->second->hasDuplicates()) {
            std::string objName;
            getObjectName(obj, objName);
            LOG_NO(
                "Impossible upgrade, attribute %s:%s adds flag "
                "SA_IMM_ATTR_NO_DUPLICATE, but object '%s' has "
                "duplicate values in that attribute",
                className.c_str(), attName.c_str(), objName.c_str());
            return true;
          }
        }
      }

      if (checkStrongDefault) {
        /* Screen all instances of the class.
         * If there's an instance with the attribute being NULL, abort the
         * schema change. */
        ObjectSet::iterator osi = oldClassInfo->mExtent.begin();
        for (; osi != oldClassInfo->mExtent.end(); ++osi) {
          obj = *osi;
          ImmAttrValueMap::iterator oavi = obj->mAttrValueMap.find(attName);
          osafassert(oavi != obj->mAttrValueMap.end());
          if (oavi->second->empty()) {
            std::string objName;
            getObjectName(obj, objName);
            LOG_NO(
                "Impossible upgrade, attribute %s:%s adds SA_IMM_ATTR_STRONG_DEFAULT flag, "
                "but that attribute of object '%s' has NULL value",
                className.c_str(), attName.c_str(), objName.c_str());
            return true;
          }
        }
      }
    }

    /* "changedAttrs != NULL" ensures that this check is only for the schema
     * update */
    if (checkNoDanglingRefs && changedAttrs) {
      if (newAttr->mValueType != SA_IMM_ATTR_SANAMET &&
          !(newAttr->mValueType == SA_IMM_ATTR_SASTRINGT &&
            (newAttr->mFlags & SA_IMM_ATTR_DN))) {
        LOG_NO(
            "Impossible upgrade, attribute %s:%s adds/removes SA_IMM_ATTR_NO_DANGLING flag, ",
            className.c_str(), attName.c_str());
        return true;
      }

      ClassMap::iterator cmi = sClassMap.find(className);
      osafassert(cmi != sClassMap.end());

      ClassInfo* oldClassInfo = cmi->second;

      // Collect attributes with added NO_DANGLING flags
      for (ObjectSet::iterator osi = oldClassInfo->mExtent.begin();
           osi != oldClassInfo->mExtent.end(); ++osi) {
        // Check if there is any create or delete CCB operation on objects in
        // the attribute with NO_DANGLING flag
        ImmAttrValueMap::iterator avmi = (*osi)->mAttrValueMap.find(attName);
        osafassert(avmi != (*osi)->mAttrValueMap.end());
        ObjectMap::iterator omi;
        ImmAttrValue* av = avmi->second;
        while (av) {
          if (av->getValueC_str()) {
            omi = sObjectMap.find(av->getValueC_str());
            if (omi == sObjectMap.end()) {
              std::string objName;
              getObjectName(*osi, objName);
              LOG_WA(
                  "Object %s attribute %s has a reference to a non-existing object %s. "
                  "Schema change to add NO_DANGLING is not possible",
                  objName.c_str(), attName.c_str(), av->getValueC_str());
              return true;
            }
            if (omi->second->mClassInfo->mCategory == SA_IMM_CLASS_RUNTIME &&
                std::find_if(omi->second->mClassInfo->mAttrMap.begin(),
                             omi->second->mClassInfo->mAttrMap.end(),
                             AttrFlagIncludes(SA_IMM_ATTR_PERSISTENT)) ==
                    omi->second->mClassInfo->mAttrMap.end()) {
              std::string objName;
              getObjectName(*osi, objName);
              LOG_WA(
                  "Object %s attribute %s has a reference to a non-persistent runtime object %s. "
                  "Schema change to add NO_DANGLING is not possible",
                  objName.c_str(), attName.c_str(), av->getValueC_str());
              return true;
            }
            if (omi->second->mObjFlags & (IMM_CREATE_LOCK | IMM_DELETE_LOCK)) {
              std::string objName;
              getObjectName(*osi, objName);
              LOG_WA(
                  "CCB in progress operating on object %s attribute %s referenced by object %s is being upgraded. "
                  "Schema change to add NO_DANGLING is not possible",
                  objName.c_str(), attName.c_str(), av->getValueC_str());
              return true;
            }
          }

          if (av->isMultiValued()) {
            av = ((ImmAttrMultiValue*)av)->getNextAttrValue();
          } else {
            break;
          }
        }
      }
    }

    if (oldAttr->mDefaultValue.empty() && !newAttr->mDefaultValue.empty()) {
      if (protocol45Allowed()) {
        LOG_NO("Allowed upgrade, attribute %s:%s adds default value",
               className.c_str(), attName.c_str());
        change = true;
      } else {
        LOG_NO("Impossible upgrade, attribute %s:%s adds default value",
               className.c_str(), attName.c_str());
        return true;
      }
    }

    if (!oldAttr->mDefaultValue.empty() && newAttr->mDefaultValue.empty()) {
      if (newAttr->mFlags & SA_IMM_ATTR_DEFAULT_REMOVED) {
        LOG_NO("Allowed upgrade, attribute %s:%s removes default value",
               className.c_str(), attName.c_str());
        change = true;
      } else {
        LOG_NO("Impossible upgrade, attribute %s:%s removes default value",
               className.c_str(), attName.c_str());
        return true;
      }
    }

    /* Default value may change, this will only affect new instances. */
    if (!oldAttr->mDefaultValue.empty() && !newAttr->mDefaultValue.empty()) {
      IMMSV_EDU_ATTR_VAL oldval;
      IMMSV_OCTET_STRING tmpos;
      oldAttr->mDefaultValue.copyValueToEdu(
          &oldval, (SaImmValueTypeT)oldAttr->mValueType);
      eduAtValToOs(&tmpos, &oldval, (SaImmValueTypeT)oldAttr->mValueType);
      if (newAttr->mDefaultValue.hasMatchingValue(tmpos)) {
        TRACE_5("Unchanged default value for %s:%s", className.c_str(),
                attName.c_str());
      } else {
        LOG_NO("Allowed upgrade, attribute %s:%s changes default value",
               className.c_str(), attName.c_str());
        change = true;
      }
      immsv_evt_free_att_val(&oldval, (SaImmValueTypeT)oldAttr->mValueType);
    }

    if (change) {
      (*changedAttrs)[attName] = newAttr;
    }
  } else {
    /* This a new attribute apended to the class. */
    if (newAttr->mFlags & SA_IMM_ATTR_INITIALIZED) {
      LOG_NO(
          "Impossible upgrade, new attribute %s:%s has SA_IMM_ATTR_INITIALIZED "
          "flag set",
          className.c_str(), attName.c_str());
      return true;
    }

    if ((newAttr->mFlags & SA_IMM_ATTR_CACHED) &&
        (newClassInfo->mCategory == SA_IMM_CLASS_RUNTIME) &&
        newAttr->mDefaultValue.empty()) {
      LOG_NO(
          "Impossible upgrade, runtime class has new attribute %s:%s with"
          " SA_IMM_ATTR_CACHED flag set, but no default value",
          className.c_str(), attName.c_str());
      return true;
    }

    if (newAttr->mFlags & SA_IMM_ATTR_DEFAULT_REMOVED) {
      LOG_NO(
          "Impossible upgrade, new attribute %s:%s has SA_IMM_ATTR_DEFAULT_REMOVED "
          "flag set",
          className.c_str(), attName.c_str());
      return true;
    }
  }

  return false;
}

/**
 * Deletes a class.
 */
SaAisErrorT ImmModel::classDelete(const ImmsvOmClassDescr* req,
                                  SaUint32T reqConn, unsigned int nodeId,
                                  SaUint32T* continuationIdPtr,
                                  SaUint32T* pbeConnPtr,
                                  unsigned int* pbeNodeIdPtr) {
  TRACE_ENTER();

  size_t sz = strnlen((char*)req->className.buf, (size_t)req->className.size);
  std::string className((const char*)req->className.buf, sz);

  SaAisErrorT err = SA_AIS_OK;

  if (immNotPbeWritable()) {
    err = SA_AIS_ERR_TRY_AGAIN;
  } else if (!schemaNameCheck(className)) {
    LOG_NO("ERR_INVALID_PARAM: Not a proper class name: %s", className.c_str());
    err = SA_AIS_ERR_INVALID_PARAM;
  } else {
    ClassMap::iterator i = sClassMap.find(className);
    if (i == sClassMap.end()) {
      TRACE_7("ERR_NOT_EXIST: class '%s' does not exist", className.c_str());
      err = SA_AIS_ERR_NOT_EXIST;
    } else if (!(i->second->mExtent.empty())) {
      LOG_WA("ERR_BUSY: class '%s' busy, refCount:%u", className.c_str(),
             (unsigned int)i->second->mExtent.size());
      err = SA_AIS_ERR_BUSY;
    } else if (sPbeRtMutations.find(className) != sPbeRtMutations.end()) {
      LOG_NO(
          "ERR_BUSY: Delete of class %s received while class "
          "with same name is already being mutated",
          className.c_str());
      err = SA_AIS_ERR_BUSY;
    } else {
      AttrMap::iterator ai = i->second->mAttrMap.begin();
      while (ai != i->second->mAttrMap.end()) {
        AttrInfo* ainfo = ai->second;
        osafassert(ainfo);
        delete (ainfo);
        ++ai;
      }
      i->second->mAttrMap.clear();
      delete i->second;
      sClassMap.erase(i);
      updateImmObject(className, true);
      TRACE_5("class %s deleted", className.c_str());

      if (pbeNodeIdPtr) {
        if (!getPbeOi(pbeConnPtr, pbeNodeIdPtr)) {
          LOG_ER("Pbe is not available, can not happen here");
          abort();
        }
        if (++sLastContinuationId >= 0xfffffffe) {
          sLastContinuationId = 1;
        }
        (*continuationIdPtr) = sLastContinuationId;

        osafassert(sPbeRtMutations.find(className) == sPbeRtMutations.end());
        /* Create a mutation record to bar pbe restart --recover  */
        ObjectMutation* oMut = new ObjectMutation(IMM_DELETE_CLASS);
        oMut->mContinuationId = (*continuationIdPtr);
        oMut->mAfterImage = NULL;
        sPbeRtMutations[className] = oMut;

        if (reqConn) {
          SaInvocationT tmp_hdl =
              m_IMMSV_PACK_HANDLE((*continuationIdPtr), nodeId);
          sPbeRtReqContinuationMap[tmp_hdl] =
              ContinuationInfo2(reqConn, DEFAULT_TIMEOUT_SEC);
        }
      }
    }
  }
  TRACE_LEAVE();
  return err;
}

/**
 * Creates an attribute.
 */
SaAisErrorT ImmModel::attrCreate(ClassInfo* classInfo,
                                 const ImmsvAttrDefinition* attr,
                                 const std::string& attrName) {
  SaAisErrorT err = SA_AIS_OK;
  // TRACE_ENTER();

  if (!schemaNameCheck(attrName)) {
    LOG_NO("ERR_INVALID_PARAM: Not a proper attribute name: %s",
           attrName.c_str());
    err = SA_AIS_ERR_INVALID_PARAM;
  } else {
    AttrMap::iterator i = classInfo->mAttrMap.find(attrName);
    if (i != classInfo->mAttrMap.end()) {
      LOG_NO("ERR_INVALID_PARAM: attr def for '%s' is duplicated",
             attrName.c_str());
      err = SA_AIS_ERR_INVALID_PARAM;
      goto done;
    }

    /* Verify attribute name is unique within class case-insensitive. */
    for (i = classInfo->mAttrMap.begin(); i != classInfo->mAttrMap.end(); ++i) {
      if (nocaseCompare(attrName, i->first)) {
        LOG_NO(
            "ERR_INVALID_PARAM: attr name '%s'/'%s' is duplicated "
            "(case insensitive)",
            attrName.c_str(), i->first.c_str());
        err = SA_AIS_ERR_INVALID_PARAM;
        goto done;
      }
    }

    SaImmAttrFlagsT unknownFlags =
        attr->attrFlags &
        ~(SA_IMM_ATTR_MULTI_VALUE | SA_IMM_ATTR_RDN | SA_IMM_ATTR_CONFIG |
          SA_IMM_ATTR_WRITABLE | SA_IMM_ATTR_INITIALIZED | SA_IMM_ATTR_RUNTIME |
          SA_IMM_ATTR_PERSISTENT | SA_IMM_ATTR_CACHED |
          SA_IMM_ATTR_NO_DUPLICATES | SA_IMM_ATTR_NOTIFY |
          SA_IMM_ATTR_NO_DANGLING | SA_IMM_ATTR_DN |
          SA_IMM_ATTR_DEFAULT_REMOVED | SA_IMM_ATTR_STRONG_DEFAULT);

    if (unknownFlags) {
      /* This error means that at least one attribute flag is not supported by
         this OpenSAF release. This release will still try to cope with such
         flags by ignoring them (e.g. not failing to load the rest of the data).
         This can only be done if either the XSD pointed at by the file can be
         loaded and the unsupported flag is confirmed as a valid flag name in
         that XSD; or if the recognition of the unsupported flag has been
         hardcoded into this older release. Hardcoding is only done for
         non-integrity related flags such as SA_IMM_ATTR_NOTIFY and allows the
         flag to pass through this system (e.g. be included in a dump).
      */
      err = SA_AIS_ERR_NOT_SUPPORTED;
      TRACE_5(
          "create attribute '%s' with unknown flag(s), attribute flag: %llu",
          attrName.c_str(), attr->attrFlags);
    } else {
      TRACE_5("create attribute '%s'", attrName.c_str());
    }

    AttrInfo* attrInfo = new AttrInfo;
    attrInfo->mValueType = attr->attrValueType;
    attrInfo->mFlags = attr->attrFlags;
    attrInfo->mNtfId = attr->attrNtfId;
    if (attr->attrDefaultValue) {
      IMMSV_OCTET_STRING tmpos;  // temporary octet string
      eduAtValToOs(&tmpos, attr->attrDefaultValue,
                   (SaImmValueTypeT)attr->attrValueType);
      attrInfo->mDefaultValue.setValue(tmpos);
    }
    classInfo->mAttrMap[attrName] = attrInfo;
  }
done:
  // TRACE_LEAVE();
  return err;
}

struct AttrDescriptionGet {
  explicit AttrDescriptionGet(ImmsvOmClassDescr*& s) : classDescription(s) {}

  AttrMap::value_type operator()(const AttrMap::value_type& item) {
    ImmsvAttrDefList* p =
        (ImmsvAttrDefList*)calloc(1, sizeof(ImmsvAttrDefList)); /*Alloc-X*/
    const std::string& attrName = item.first;
    p->d.attrName.size = (int)attrName.length() + 1; /*Alloc-Y*/
    p->d.attrName.buf = (char*)malloc(p->d.attrName.size);
    strncpy(p->d.attrName.buf, attrName.c_str(), p->d.attrName.size);
    p->d.attrValueType = item.second->mValueType;
    p->d.attrFlags = item.second->mFlags;
    p->d.attrNtfId = item.second->mNtfId;
    if (item.second->mDefaultValue.empty()) {
      p->d.attrDefaultValue = NULL;
    } else {
      p->d.attrDefaultValue = (IMMSV_EDU_ATTR_VAL*)/*alloc-Z*/
          calloc(1, sizeof(IMMSV_EDU_ATTR_VAL));

      item.second->mDefaultValue.copyValueToEdu(p->d.attrDefaultValue,
                                                (SaImmValueTypeT) /*alloc-ZZ*/
                                                p->d.attrValueType);
    }
    p->next = classDescription->attrDefinitions;
    classDescription->attrDefinitions = p;
    return item;
  }

  ImmsvOmClassDescr*& classDescription;  // Data member
};

/**
 * Returns a class description.
 */
SaAisErrorT ImmModel::classDescriptionGet(const IMMSV_OCTET_STRING* clName,
                                          ImmsvOmClassDescr* res) {
  TRACE_ENTER();
  size_t sz = strnlen((char*)clName->buf, (size_t)clName->size);
  std::string className((const char*)clName->buf, sz);

  SaAisErrorT err = SA_AIS_OK;

  if (!schemaNameCheck(className)) {
    LOG_NO("ERR_INVALID_PARAM: Not a proper class name");
    err = SA_AIS_ERR_INVALID_PARAM;
  } else {
    ClassMap::iterator i = sClassMap.find(className);
    if (i == sClassMap.end()) {
      TRACE_7("ERR_NOT_EXIST: class '%s' does not exist", className.c_str());
      err = SA_AIS_ERR_NOT_EXIST;
    } else {
      ClassInfo* classInfo = i->second;
      res->classCategory = classInfo->mCategory;

      std::for_each(classInfo->mAttrMap.begin(), classInfo->mAttrMap.end(),
                    AttrDescriptionGet(res));
    }
  }
  TRACE_LEAVE();
  return err;
}

/**
 * Creates an admin owner.
 */
SaAisErrorT ImmModel::adminOwnerCreate(const ImmsvOmAdminOwnerInitialize* req,
                                       SaUint32T ownerId, SaUint32T conn,
                                       unsigned int nodeId) {
  SaAisErrorT err = SA_AIS_OK;
  bool isLoading = (sImmNodeState == IMM_NODE_LOADING);

  TRACE_ENTER();
  if (immNotWritable() || is_sync_aborting()) {
    TRACE_LEAVE();
    return SA_AIS_ERR_TRY_AGAIN;
  }

  if (strcmp("IMMLOADER", osaf_extended_name_borrow(&req->adminOwnerName)) ==
      0) {
    if (sImmNodeState != IMM_NODE_LOADING) {
      LOG_NO(
          "ERR_INVALID_PARAM: Admin Owner 'IMMLOADER' only allowed for loading");
      TRACE_LEAVE();
      return SA_AIS_ERR_INVALID_PARAM;
    }
  }

  ObjectMap::iterator oi = sObjectMap.find(immObjectDn);
  if (protocol51Allowed() && oi != sObjectMap.end() && !isLoading) {
    ObjectInfo* immObject = oi->second;
    ImmAttrValueMap::iterator avi = immObject->mAttrValueMap.find(immMaxAdmOwn);
    osafassert(avi != immObject->mAttrValueMap.end());
    osafassert(!(avi->second->isMultiValued()));
    ImmAttrValue* valuep = avi->second;
    unsigned int maxAdmOwn = valuep->getValue_int();
    if (sOwnerVector.size() >= maxAdmOwn) {
      LOG_NO(
          "ERR_NO_RESOURCES: maximum AdminOwners limit %d has been reached for the cluster",
          maxAdmOwn);
      TRACE_LEAVE();
      return SA_AIS_ERR_NO_RESOURCES;
    }
  }

  AdminOwnerInfo* info = new AdminOwnerInfo;

  info->mId = ownerId;

  info->mAdminOwnerName.append(osaf_extended_name_borrow(&req->adminOwnerName));
  if (info->mAdminOwnerName.empty() || !nameCheck(info->mAdminOwnerName)) {
    LOG_NO("ERR_INVALID_PARAM: Not a valid Admin Owner Name");
    delete info;
    info = NULL;
    TRACE_LEAVE();
    return SA_AIS_ERR_INVALID_PARAM;
  }
  info->mReleaseOnFinalize = req->releaseOwnershipOnFinalize;
  info->mConn = conn;
  info->mNodeId = nodeId;
  info->mDying = false;
  sOwnerVector.push_back(info);
  TRACE_5("admin owner '%s' %u created", info->mAdminOwnerName.c_str(),
          info->mId);

  TRACE_LEAVE();
  return err;
}

/**
 * Deletes and admin owner.
 */
SaAisErrorT ImmModel::adminOwnerDelete(SaUint32T ownerId, bool hard,
                                       bool pbe2) {
  SaAisErrorT err = SA_AIS_OK;

  // Even if the finalize is hard (client crash/detach), we assume
  // the order for hard finalize arrives over fevs, i.e. in the same
  // order position relative other fevs messages, for all IMMNDs.
  // This means that a hard finalize of admin owner can not cause
  // differences in behavior at diferent IMMNDs.

  TRACE_ENTER();

  AdminOwnerVector::iterator i;

  i = std::find_if(sOwnerVector.begin(), sOwnerVector.end(), IdIs(ownerId));
  if (i == sOwnerVector.end()) {
    LOG_NO("ERR_BAD_HANDLE: Admin owner %u does not exist", ownerId);
    err = SA_AIS_ERR_BAD_HANDLE;
  } else {
    unsigned int loopCount = 0;

    if ((*i)->mAdminOwnerName == std::string("IMMLOADER")) {
      // Loader-pid set to zero both in immnd-coord and in other immnds.
      // In coord the loader-pid was positive.
      // In non-coords the loader-pid was negative.
      if (hard) {
        if ((this->loaderPid > 0) && (!pbe2 || (ownerId > 2))) {
          LOG_WA(
              "Hard close of admin owner IMMLOADER id(%u)"
              "=> Loading must have failed",
              ownerId);
        } else if (this->loaderPid == (-1)) {
          LOG_IN(
              "Hard close of admin owner IMMLOADER id(%u) "
              "=> Must be from pre-loader ? %u",
              ownerId, pbe2);
        } else if (loaderPid == 0) {
          LOG_IN(
              "Hard close of admin owner IMMLOADER id(%u) "
              "but loading is done",
              ownerId);
        }
      } else {
        if (this->getLoader() && (sImmNodeState == IMM_NODE_LOADING)) {
          LOG_NO(
              "Closing admin owner IMMLOADER id(%u), "
              "loading of IMM done",
              ownerId);
          this->setLoader(0);
          /* BEGIN Temporary code for enabling all protocol upgrade
             flags when cluster is started/loaded or restarted/reloaded.
          */
          ObjectMap::iterator oi = sObjectMap.find(immObjectDn);
          if (oi == sObjectMap.end()) {
            LOG_ER("Failed to find object %s - loading failed",
                   immObjectDn.c_str());
            /* Return special error up to immnd_evt so that NackToNid can be
               sent before aborting. */
            err = SA_AIS_ERR_NOT_READY;
            goto done;
          }
          ObjectInfo* immObject = oi->second;
          ImmAttrValueMap::iterator avi =
              immObject->mAttrValueMap.find(immAttrNostFlags);
          osafassert(avi != immObject->mAttrValueMap.end());
          osafassert(!(avi->second->isMultiValued()));
          ImmAttrValueMap::iterator avi1 =
              immObject->mAttrValueMap.find(immMaxClasses);
          ImmAttrValueMap::iterator avi2 =
              immObject->mAttrValueMap.find(immMaxImp);
          ImmAttrValueMap::iterator avi3 =
              immObject->mAttrValueMap.find(immMaxAdmOwn);
          ImmAttrValueMap::iterator avi4 =
              immObject->mAttrValueMap.find(immMaxCcbs);
          ImmAttrValueMap::iterator avi5 =
              immObject->mAttrValueMap.find(immMinApplierTimeout);

          ImmAttrValue* valuep = (ImmAttrValue*)avi->second;
          unsigned int noStdFlags = valuep->getValue_int();
          noStdFlags |= OPENSAF_IMM_FLAG_PRT41_ALLOW;
          noStdFlags |= OPENSAF_IMM_FLAG_PRT43_ALLOW;
          noStdFlags |= OPENSAF_IMM_FLAG_PRT45_ALLOW;
          noStdFlags |= OPENSAF_IMM_FLAG_PRT46_ALLOW;
          noStdFlags |= OPENSAF_IMM_FLAG_PRT47_ALLOW;
          noStdFlags |= OPENSAF_IMM_FLAG_PRT50_ALLOW;
          if ((avi1 == immObject->mAttrValueMap.end()) ||
              (avi2 == immObject->mAttrValueMap.end()) ||
              (avi3 == immObject->mAttrValueMap.end()) ||
              (avi4 == immObject->mAttrValueMap.end())) {
            LOG_NO(
                "protcol51 is not set for opensafImmNostdFlags because"
                "The new OpenSAF 5.1 attributes are not added to OpensafImm class");
          } else {
            noStdFlags |= OPENSAF_IMM_FLAG_PRT51_ALLOW;
          }
          if(avi5 == immObject->mAttrValueMap.end()) {
            LOG_NO("protcol51710 is not set for opensafImmNostdFlags because"
                "The new OpenSAF 5.17.10 attributes are not added "
                "to OpensafImm class");
          } else {
            noStdFlags |= OPENSAF_IMM_FLAG_PRT51710_ALLOW;
          }
          valuep->setValue_int(noStdFlags);
          LOG_NO("%s changed to: 0x%x", immAttrNostFlags.c_str(), noStdFlags);
          /* END Temporary code. */
        } else {
          LOG_IN("Forcing close of preloader admo if:%u", ownerId);
          goto forced;
        }
      }
    }

    if (immNotWritable() || is_sync_aborting()) {
      if (hard) {
        unsigned int siz = (unsigned int)(*i)->mTouchedObjects.size();
        if (siz >= IMMSV_MAX_OBJECTS) {
          LOG_WA(
              "Forcing immediate hard delete of large (%u) admin owner with id:%u "
              "to clear way for sync",
              siz, ownerId);
          goto forced;
        }
        if (sImmNodeState > IMM_NODE_UNKNOWN) {
          LOG_WA(
              "Postponing hard delete of admin owner with id:%u "
              "when imm is not writable state",
              ownerId);
        }
        (*i)->mDying = true;
        err = SA_AIS_ERR_BUSY;
      } else {
        err = SA_AIS_ERR_TRY_AGAIN;
      }
      goto done;
    }

  forced:
    if ((*i)->mDying) {
      LOG_NO("Removing zombie Admin Owner %u %s", ownerId,
             (*i)->mAdminOwnerName.c_str());
    } else {
      TRACE_5("Delete admin owner %u '%s'", ownerId,
              (*i)->mAdminOwnerName.c_str());
    }

    if ((*i)->mReleaseOnFinalize) {
      ObjectSet::iterator oi = (*i)->mTouchedObjects.begin();
      while (oi != (*i)->mTouchedObjects.end()) {
        std::string dummyName;
        err = adminOwnerRelease(dummyName, *oi, *i, true);
        if (err != SA_AIS_OK) {
          // This should never happen.
          TRACE_5(
              "Internal error when trying to release owner on "
              "finalize. Application may need to do an "
              "AdminOwnerClear");
          // Ignore the error, assume brutal adminOwnerClear was done
          err = SA_AIS_OK;
        }
        if (++loopCount > 500) {
          if (loopCount % 1000 == 0) {
            TRACE_5("Busy in adminOwnerDelete? %u", loopCount);
          }
          if (loopCount % 50000 == 0) {
            LOG_WA("Busy in HUGE admin owner release %u", loopCount);
          }
          if (loopCount % 2000000 == 0) {
            LOG_ER("Assuming stuck in adminOwnerDelete %u", loopCount);
            err = SA_AIS_ERR_LIBRARY;
            goto done;
          }
        }
        // adminOwnerRelease deletes elements from the collection we
        // are iterating over here, which is why this is not a for-loop.
        // Instead, it is a while-loop, where we always fetch the first
        // element, until the collection is empty.
        // We are assuming here that adminOwnerRelease always removes
        // each element from mTouchedObjects. If it does not,
        // we would get an endless loop here!
        // The above check is an extra precaution to obtain a clear
        // error indication should this happen.
        oi = (*i)->mTouchedObjects.begin();
      }
    }
    (*i)->mId = 0;
    (*i)->mAdminOwnerName.clear();
    (*i)->mConn = 0;
    (*i)->mNodeId = 0;
    (*i)->mTouchedObjects.clear();
    (*i)->mReleaseOnFinalize = false;
    (*i)->mDying = false;
    delete *i;
    sOwnerVector.erase(i);
  }
done:
  TRACE_LEAVE();
  return err;
}

/**
 * Admin owner set and release
 */
SaAisErrorT ImmModel::adminOwnerChange(const struct immsv_a2nd_admown_set* req,
                                       bool release) {
  // This method handles three om-api-calls:
  //(a) AdminOwnerSet (release is false and req->adminOwnerId is non-zero;
  //(b) AdminOwnerRelease (release is true and req->adminOwnerId is non-zero;
  //(c) AdminOwnerClear (release is true and req->adminOwnerId is zero.
  SaAisErrorT err = SA_AIS_OK;
  TRACE_ENTER();

  if (immNotWritable() || is_sync_aborting()) {
    TRACE_LEAVE();
    return SA_AIS_ERR_TRY_AGAIN;
  }

  SaUint32T ownerId = req->adm_owner_id;

  osafassert(release || ownerId);
  SaImmScopeT scope = (SaImmScopeT)req->scope;
  AdminOwnerVector::iterator i = sOwnerVector.end();
  if (ownerId) {
    i = std::find_if(sOwnerVector.begin(), sOwnerVector.end(), IdIs(ownerId));
    if ((i != sOwnerVector.end()) && (*i)->mDying) {
      i = sOwnerVector.end();  // Pretend it does not exist.
      TRACE_5("Attempt to use dead admin owner in adminOwnerChange");
    }
  }

  if (ownerId && (i == sOwnerVector.end())) {
    LOG_NO("ERR_BAD_HANDLE: admin owner %u does not exist", ownerId);
    err = SA_AIS_ERR_BAD_HANDLE;
  } else {
    TRACE_5("%s admin owner '%s'",
            release ? (ownerId ? "Release" : "Clear") : "Set",
            (ownerId) ? (*i)->mAdminOwnerName.c_str() : "ALL");

    AdminOwnerInfo* adm = ownerId ? (*i) : NULL;

    for (int doIt = 0; (doIt < 2) && (err == SA_AIS_OK); ++doIt) {
      IMMSV_OBJ_NAME_LIST* ol = req->objectNames;
      while (ol && err == SA_AIS_OK) {
        std::string objectName(
            (const char*)ol->name.buf,
            (size_t)strnlen((const char*)ol->name.buf, (size_t)ol->name.size));
        if (doIt) {
          TRACE_5("%s Admin Owner for object %s\n",
                  release ? (ownerId ? "Release" : "Clear") : "Set",
                  objectName.c_str());
        }

        if (!(nameCheck(objectName) || nameToInternal(objectName))) {
          LOG_NO("ERR_INVALID_PARAM: Not a proper object name");
          osafassert(!doIt);
          return SA_AIS_ERR_INVALID_PARAM;
        }

        ObjectMap::iterator i1 = sObjectMap.find(objectName);
        if (i1 == sObjectMap.end()) {
          TRACE_7("ERR_NOT_EXIST: object '%s' does not exist",
                  objectName.c_str());
          return SA_AIS_ERR_NOT_EXIST;
        } else {
          SaUint32T ccbIdOfObj;
          CcbVector::iterator i2;
          ObjectInfo* objectInfo = i1->second;
          ccbIdOfObj = objectInfo->mCcbId;
          if (!doIt && ccbIdOfObj) {
            // check for ccb interference
            // For set the adminowner is either same == noop;
            // or different => will be caught in adminOwnerSet()
            i2 = std::find_if(sCcbVector.begin(), sCcbVector.end(),
                              CcbIdIs(ccbIdOfObj));
            if (i2 != sCcbVector.end() && (*i2)->isActive()) {
              std::string oldOwner;
              objectInfo->getAdminOwnerName(&oldOwner);
              if (!release && (adm->mAdminOwnerName == oldOwner)) {
                TRACE("Idempotent adminOwner set for %s on %s",
                      oldOwner.c_str(), objectName.c_str());
              } else if (!release && oldOwner.empty()
                  && (*i2)->mState == IMM_CCB_CRITICAL) {
                TRACE_7("ERR_TRY_AGAIN: Object '%s' is in a critical CCB (%d)",
                        objectName.c_str(), (*i2)->mId);
                TRACE_LEAVE();
                return SA_AIS_ERR_TRY_AGAIN;
              } else if (release) {
                LOG_IN("ERR_BUSY: ccb id %u active on object %s", ccbIdOfObj,
                       objectName.c_str());
                TRACE_LEAVE();
                return SA_AIS_ERR_BUSY;
              }
            }
          }
          if (release) {
            err = adminOwnerRelease(objectName, objectInfo, adm, doIt);
          } else {
            err = adminOwnerSet(objectName, objectInfo, adm, doIt);
          }

          if (err == SA_AIS_OK && scope != SA_IMM_ONE) {
            SaUint32T childCount = objectInfo->mChildCount;
            // Find all sub objects to the root object
            for (i1 = sObjectMap.begin();
                 i1 != sObjectMap.end() && err == SA_AIS_OK && childCount;
                 ++i1) {
              std::string subObjName = i1->first;
              if (subObjName.length() > objectName.length()) {
                size_t pos = subObjName.length() - objectName.length();
                if ((subObjName.rfind(objectName, pos) == pos) &&
                    (subObjName[pos - 1] == ',')) {
                  if (scope == SA_IMM_SUBTREE ||
                      checkSubLevel(subObjName, pos)) {
                    ObjectInfo* subObj = i1->second;
                    ccbIdOfObj = subObj->mCcbId;
                    if (!doIt && ccbIdOfObj) {
                      // check for ccb interference
                      i2 = std::find_if(sCcbVector.begin(), sCcbVector.end(),
                                        CcbIdIs(ccbIdOfObj));
                      if (i2 != sCcbVector.end() && (*i2)->isActive()) {
                        std::string oldOwner;
                        subObj->getAdminOwnerName(&oldOwner);
                        if (!release && adm->mAdminOwnerName == oldOwner) {
                          TRACE("Idempotent adminOwner set for %s on %s",
                                oldOwner.c_str(), subObjName.c_str());
                        } else {
                          LOG_IN(
                              "ERR_BUSY: ccb id %u active on"
                              "object %s",
                              ccbIdOfObj, subObjName.c_str());
                          TRACE_LEAVE();
                          return SA_AIS_ERR_BUSY;
                        }
                      }
                    }
                    --childCount;
                    if (release) {
                      err = adminOwnerRelease(subObjName, subObj, adm, doIt);
                    } else {
                      err = adminOwnerSet(subObjName, subObj, adm, doIt);
                    }
                    if (!childCount) {
                      TRACE("Cutoff in admo-change-loop by childCount");
                    }
                  }
                }
              }
            }
          }
        }
        ol = ol->next;
      }
    }
  }

  TRACE_LEAVE();
  return err;
}

/**
 * Creates a CCB.
 */
SaAisErrorT ImmModel::ccbCreate(SaUint32T adminOwnerId, SaUint32T ccbFlags,
                                SaUint32T ccbId, unsigned int originatingNodeId,
                                SaUint32T originatingConn) {
  SaAisErrorT err = SA_AIS_OK;
  bool isLoading = (sImmNodeState == IMM_NODE_LOADING);

  TRACE_ENTER();

  if (immNotWritable() || is_sync_aborting()) {
    TRACE_LEAVE();
    return SA_AIS_ERR_TRY_AGAIN;
  }

  ObjectMap::iterator oi = sObjectMap.find(immObjectDn);
  if (protocol51Allowed() && oi != sObjectMap.end() && !isLoading) {
    ObjectInfo* immObject = oi->second;
    ImmAttrValueMap::iterator avi = immObject->mAttrValueMap.find(immMaxCcbs);
    osafassert(avi != immObject->mAttrValueMap.end());
    osafassert(!(avi->second->isMultiValued()));
    ImmAttrValue* valuep = avi->second;
    unsigned int maxCcbs = valuep->getValue_int();
    if ((sCcbVector.size() - sTerminatedCcbcount) >= maxCcbs) {
      LOG_NO(
          "ERR_NO_RESOURCES: maximum Ccbs limit %d has been reached for the cluster",
          maxCcbs);
      TRACE_LEAVE();
      return SA_AIS_ERR_NO_RESOURCES;
    }
  }

  CcbInfo* info = new CcbInfo;
  info->mId = ccbId;
  info->mAdminOwnerId = adminOwnerId;
  info->mCcbFlags = ccbFlags;
  info->mOriginatingNode = originatingNodeId;
  info->mOriginatingConn = originatingConn;
  info->mVeto = SA_AIS_OK;
  info->mState = IMM_CCB_EMPTY;
  info->mWaitStartTime = kZeroSeconds;
  info->mOpCount = 0;
  info->mPbeRestartId = 0;
  info->mErrorStrings = NULL;
  info->mAugCcbParent = NULL;
  sCcbVector.push_back(info);

  TRACE_5("CCB %u created with admo %u", info->mId, adminOwnerId);

  TRACE_LEAVE();
  return err;
}

SaAisErrorT ImmModel::ccbResult(SaUint32T ccbId) {
  SaAisErrorT err = SA_AIS_OK;
  CcbVector::iterator i;
  i = std::find_if(sCcbVector.begin(), sCcbVector.end(), CcbIdIs(ccbId));
  if (i == sCcbVector.end()) {
    LOG_WA("CCB %u does not exist- probably removed already", ccbId);
    err = SA_AIS_ERR_NO_RESOURCES;
  } else {
    switch ((*i)->mState) {
      case IMM_CCB_ABORTED:
      case IMM_CCB_PBE_ABORT:
        TRACE_5("Fetch ccb result: CCB %u was aborted", ccbId);
        err = SA_AIS_ERR_FAILED_OPERATION;
        break;  // Normal

      case IMM_CCB_COMMITTED:
        TRACE_5("Fetch ccb result: CCB %u was committed", ccbId);
        err = SA_AIS_OK;
        break;  // Normal

      case IMM_CCB_EMPTY:
      case IMM_CCB_READY:
      case IMM_CCB_CREATE_OP:
      case IMM_CCB_MODIFY_OP:
      case IMM_CCB_DELETE_OP:
        LOG_WA("ccbResult: CCB %u is active, state:%u.", ccbId, (*i)->mState);
        err = SA_AIS_ERR_TRY_AGAIN;
        break;

      case IMM_CCB_PREPARE:
        LOG_WA("ccbResult: CCB %u in state 'prepare' validation in progress",
               ccbId);
        err = SA_AIS_ERR_TRY_AGAIN;
        break;

      case IMM_CCB_VALIDATING:
        LOG_WA(
            "ccbResult: CCB %u in state 'validating' explicit validation in progress",
            ccbId);
        err = SA_AIS_ERR_TRY_AGAIN;
        break;

      case IMM_CCB_VALIDATED:
        LOG_WA(
            "ccbResult: CCB %u in state 'validated'. Waiting for apply or abort/finalize",
            ccbId);
        err = SA_AIS_ERR_TRY_AGAIN;
        break;

      case IMM_CCB_CRITICAL:
        LOG_NO("ccbResult: CCB %u in critical state! Commit/apply in progress",
               ccbId);
        err = SA_AIS_ERR_TRY_AGAIN;
        break;  // Can be here if PBE is crashed, hung on fs, or backloged.

      default:
        LOG_ER("ccbResult: Illegal state %u in ccb %u", (*i)->mState, ccbId);
        abort();
    }
  }
  return err;
}

/*
 * Validate NO_DANGLING references for an object in object create and object
 * modify operations
 */
bool ImmModel::validateNoDanglingRefsModify(CcbInfo* ccb,
                                            ObjectMutationMap::iterator& omit) {
  ClassInfo* classInfo = omit->second->mAfterImage->mClassInfo;
  AttrMap::iterator amit;
  ImmAttrValueMap::iterator avit;
  ObjectMap::iterator omi;

  TRACE_ENTER();

  if (classInfo->mCategory == SA_IMM_CLASS_RUNTIME) {
    TRACE_LEAVE();
    return true;
  }

  for (amit = classInfo->mAttrMap.begin(); amit != classInfo->mAttrMap.end();
       ++amit) {
    AttrInfo* ai = amit->second;
    if (ai->mFlags & SA_IMM_ATTR_NO_DANGLING) {
      if ((avit = omit->second->mAfterImage->mAttrValueMap.find(amit->first)) !=
          omit->second->mAfterImage->mAttrValueMap.end()) {
        ImmAttrValue* av = avit->second;

        while (av) {
          /* Empty attribute */
          if (av->getValueC_str()) {
            omi = sObjectMap.find(av->getValueC_str());
            if (omi == sObjectMap.end()) {
              LOG_NO(
                  "ERR_FAILED_OPERATION: NO_DANGLING reference (%s) is dangling (Ccb %u)",
                  av->getValueC_str(), ccb->mId);
              setCcbErrorString(ccb, "IMM: ERR_FAILED_OPERATION: "
                                "Object %s has dangling reference (%s)",
                                omit->first.c_str(), av->getValueC_str());
              TRACE_LEAVE();
              return false;
            }

            osafassert(omi->second != NULL);

            if (omi->second->mClassInfo->mCategory == SA_IMM_CLASS_RUNTIME) {
              if (std::find_if(omi->second->mClassInfo->mAttrMap.begin(),
                               omi->second->mClassInfo->mAttrMap.end(),
                               AttrFlagIncludes(SA_IMM_ATTR_PERSISTENT)) ==
                  omi->second->mClassInfo->mAttrMap.end()) {
                LOG_NO(
                    "ERR_FAILED_OPERATION: NO_DANGLING Reference (%s) "
                    "refers to a non-persistent RTO (Ccb %u)",
                    av->getValueC_str(), ccb->mId);
                setCcbErrorString(
                    ccb, "IMM: ERR_FAILED_OPERATION: "
                    "Object %s refers to a non-persistent RTO (%s)",
                    omit->first.c_str(), av->getValueC_str());
                TRACE_LEAVE();
                return false;
              }
            }

            if (omi->second->mObjFlags & IMM_DELETE_LOCK) {
              if (omi->second->mCcbId !=
                  ccb->mId) {  // Will be deleted by another CCB
                LOG_WA(
                    "ERR_FAILED_OPERATION: NO_DANGLING reference (%s) "
                    "refers to object flagged for delete by another Ccb: %u, (this Ccb %u)",
                    av->getValueC_str(), omi->second->mCcbId, ccb->mId);
              }
              setCcbErrorString(
                  ccb, "IMM: ERR_FAILED_OPERATION: "
                  "Object %s refers to object flagged for deletion (%s)",
                  omit->first.c_str(), av->getValueC_str());
              TRACE_LEAVE();
              return false;
            }
            if ((omi->second->mObjFlags & IMM_CREATE_LOCK) &&
                (omi->second->mCcbId != ccb->mId)) {
              LOG_WA(
                  "ERR_FAILED_OPERATION: NO_DANGLING reference (%s) "
                  "refers to object flagged for create by another CCB: %u, (this Ccb %u)",
                  av->getValueC_str(), omi->second->mCcbId, ccb->mId);
              setCcbErrorString(
                  ccb, "IMM: ERR_FAILED_OPERATION: "
                  "Object %s refers to object flagged for creation in another CCB (%s)",
                  omit->first.c_str(), av->getValueC_str());
              TRACE_LEAVE();
              return false;
            }
          }

          if (av->isMultiValued())
            av = ((ImmAttrMultiValue*)av)->getNextAttrValue();
          else
            break;
        }
      }
    }
  }

  TRACE_LEAVE();

  return true;
}

/*
 * Validate NO_DANGLING references for an object in object delete operations
 */
bool ImmModel::validateNoDanglingRefsDelete(CcbInfo* ccb,
                                            ObjectMutationMap::iterator& omit) {
  TRACE_ENTER();

  ObjectMap::iterator omi = sObjectMap.find(omit->first.c_str());
  osafassert(omi != sObjectMap.end());

  bool rc = true;
  ObjectMMap::iterator ommi = sReverseRefsNoDanglingMMap.find(omi->second);
  while (ommi != sReverseRefsNoDanglingMMap.end() &&
         ommi->first == omi->second) {
    if (!(ommi->second->mObjFlags & IMM_DELETE_LOCK)) {
      std::string objName;
      getObjectName(ommi->second, objName);
      if ((ommi->second->mObjFlags & IMM_NO_DANGLING_FLAG) &&
          (ommi->second->mCcbId ==
           ccb->mId)) {  // IMM_NO_DANGLING_FLAG will be set in CcbModify
        /* If the NO_DANGLING reference is modified(removed) in the same CCB,
           then deletion of the NO_DANGLING object can be allowed because, we
           are in apply and the CCBcommit will modify sReverseRefsNoDanglingMMap
         */
        ObjectMutationMap::iterator omit = ccb->mMutations.find(objName);
        osafassert(omit != ccb->mMutations.end());
        ObjectInfo* afim = omit->second->mAfterImage;
        ObjectNameSet afimRefs;
        ObjectNameSet::iterator it;
        collectNoDanglingRefs(afim, afimRefs);
        if (afimRefs.find(omit->first.c_str()) == afimRefs.end()) {
          TRACE(
              "Delete of object %s is removed from NO_DANGLING reference in the same Ccb%u "
              "from object %s",
              omit->first.c_str(), ccb->mId, objName.c_str());
          ++ommi;
          continue;
        }
      }
      LOG_WA(
          "ERR_FAILED_OPERATION: Delete of object %s would violate NO_DANGLING reference "
          "from object %s, not scheduled for delete by this Ccb:%u",
          omit->first.c_str(), objName.c_str(), ccb->mId);
      setCcbErrorString(ccb, "IMM: ERR_FAILED_OPERATION: "
                        "Object %s is currently reffered by object %s, "
                        "not scheduled for deletion by this CCB",
                        omit->first.c_str(), objName.c_str());
      rc = false;
      break;
    }
    if (ommi->second->mCcbId != ccb->mId) {
      std::string objName;
      getObjectName(ommi->second, objName);
      LOG_WA(
          "ERR_FAILED_OPERATION: Delete of object %s would violate NO_DANGLING reference "
          "from object %s, scheduled for delete by another Ccb:%u (this Ccb:%u)",
          omit->first.c_str(), objName.c_str(), ommi->second->mCcbId, ccb->mId);
      setCcbErrorString(ccb, "IMM: ERR_FAILED_OPERATION: "
                        "Object %s is currently reffered by object %s, "
                        "scheduled for deletion by another CCB",
                        omit->first.c_str(), objName.c_str());
      rc = false;
      break;
    }

    ++ommi;
  }

  TRACE_LEAVE();

  return rc;
}

/*
 * The function validates all objects that are affected by NO_DANGLING flag in
 * CCB operations
 */
bool ImmModel::validateNoDanglingRefs(CcbInfo* ccb) {
  if (!(ccb->mCcbFlags & OPENSAF_IMM_CCB_NO_DANGLING_MUTATE))
    return true;  // No changes on NO_DANGLING attributes

  TRACE_ENTER();

  bool rc = true;
  ObjectMutationMap::iterator omit;
  for (omit = ccb->mMutations.begin(); rc && omit != ccb->mMutations.end();
       ++omit) {
    switch (omit->second->mOpType) {
      case IMM_CREATE:
      case IMM_MODIFY:
        rc = validateNoDanglingRefsModify(ccb, omit);
        break;
      case IMM_DELETE:
        rc = validateNoDanglingRefsDelete(ccb, omit);
        break;
      default:
        osafassert(omit->second->mOpType <= IMM_DELETE);
        break;
    }
  }

  TRACE_LEAVE();

  return rc;
}

/**
 * Commits a CCB
 */
SaAisErrorT ImmModel::ccbApply(SaUint32T ccbId, SaUint32T reqConn,
                               ConnVector& connVector, IdVector& implIds,
                               IdVector& continuations, bool validateOnly) {
  SaAisErrorT err = SA_AIS_OK;
  TRACE_ENTER();
  if (validateOnly) {
    TRACE_5("Explicit VALIDATE started for CCB ID:%u", ccbId);
  } else {
    TRACE_5("APPLYING CCB ID:%u", ccbId);
  }

  CcbVector::iterator i;
  AdminOwnerVector::iterator i2;
  i = std::find_if(sCcbVector.begin(), sCcbVector.end(), CcbIdIs(ccbId));
  if (i == sCcbVector.end()) {
    LOG_NO("ERR_BAD_HANDLE: CCB %u does not exist", ccbId);
    err = SA_AIS_ERR_BAD_HANDLE;
  } else {
    CcbInfo* ccb = (*i);
    i2 = std::find_if(sOwnerVector.begin(), sOwnerVector.end(),
                      IdIs(ccb->mAdminOwnerId));
    if ((i2 != sOwnerVector.end()) && (*i2)->mDying) {
      i2 = sOwnerVector.end();  // Pretend it does not exist.
      TRACE_5("Attempt to use dead admin owner in ccbApply");
    }
    if (i2 == sOwnerVector.end()) {
      LOG_NO("ERR_BAD_HANDLE: Admin owner id %u does not exist",
             ccb->mAdminOwnerId);
      ccb->mVeto = SA_AIS_ERR_BAD_HANDLE;
      /* Later in the code, error code will be set to
       * SA_AIS_ERR_FAILED_OPERATION */
      setCcbErrorString(ccb, IMM_RESOURCE_ABORT "Admin owner does not exist");
    } else if (ccb->mState > IMM_CCB_READY) {
      if (ccb->mState == IMM_CCB_VALIDATING) {
        LOG_IN(
            "Ccb <%u> in incorrect state 'CCB_VALIDATING for "
            "saImmOmCcbValidate() ignoring request",
            ccb->mId);
        err = SA_AIS_ERR_ACCESS_DENIED;
        goto done;
      } else if (ccb->mState == IMM_CCB_VALIDATED) {
        if (validateOnly) {
          LOG_IN(
              "Ccb <%u> in incorrect state 'CCB_VALIDATED for "
              "saImmOmCcbValidate() ignoring request",
              ccb->mId);
          err = SA_AIS_ERR_ACCESS_DENIED;
          goto done;
        } else {
          LOG_IN("Ccb <%u> in state (IMM_CCB_VALIDATED) received apply/commit",
                 ccb->mId);
          err = ccb->mVeto;
          if (err == SA_AIS_OK) {
            if (!ccb->mImplementers.empty()) {
              /* apply callback needs to be sent to implementers. */
              err = SA_AIS_ERR_INTERRUPT;
            }
          } else if (err == SA_AIS_ERR_FAILED_OPERATION) {
            setCcbErrorString(
                ccb, IMM_VALIDATION_ABORT "Completed validation failed");
          }
          goto done;
        }
      } else if (ccb->mState != IMM_CCB_ABORTED) {
        LOG_NO("Ccb <%u> not in correct state (%u) for Apply ignoring request",
               ccb->mId, ccb->mState);
        /* This includes state IMM_CCB_VALIDATING which means explicit
           validation is in progress. Neihter a redundant saImmOmValidate(), nor
           a premature saImmOmCcbApply() can be accepted in this state. But a
           saImmOmCcbAbort() or a saImmOmCcbFinalize would be accepted.
        */
        err = SA_AIS_ERR_ACCESS_DENIED;
        goto done;
      }
    }

    osafassert(reqConn == 0 || (ccb->mOriginatingConn == reqConn));

    if (!ccb->isOk()) {
      setCcbErrorString(ccb, IMM_RESOURCE_ABORT "CCB is in an error state");
      err = SA_AIS_ERR_FAILED_OPERATION;
    } else if ((sImmNodeState == IMM_NODE_LOADING) &&
               !sMissingParents.empty()) {
      MissingParentsMap::iterator mpm;
      LOG_ER("Can not apply because there are %u missing parents",
             (unsigned int)sMissingParents.size());
      for (mpm = sMissingParents.begin(); mpm != sMissingParents.end(); ++mpm) {
        LOG_ER("Missing Parent DN: %s", mpm->first.c_str());
      }
      err = SA_AIS_ERR_FAILED_OPERATION;
      ccb->mVeto = SA_AIS_ERR_FAILED_OPERATION;
      setCcbErrorString(ccb, IMM_VALIDATION_ABORT "Parent is missing");
    } else if (!validateNoDanglingRefs(ccb)) {
      err = SA_AIS_ERR_FAILED_OPERATION;
      ccb->mVeto = SA_AIS_ERR_FAILED_OPERATION;
      setCcbErrorString(ccb,
                        IMM_VALIDATION_ABORT "No dangling validation failed");
    } else {
      /* sMissingParents must be empty if err is SA_AIS_OK */
      osafassert(sMissingParents.empty());

      if (validateOnly) {
        TRACE_5("Validate CCB %u", ccb->mId);
        ccb->mState = IMM_CCB_VALIDATING;
      } else {
        TRACE_5("Apply CCB %u", ccb->mId);
        ccb->mState = IMM_CCB_PREPARE;
      }

      CcbImplementerMap::iterator isi;
      for (isi = ccb->mImplementers.begin(); isi != ccb->mImplementers.end();
           ++isi) {
        ImplementerCcbAssociation* implAssoc = isi->second;
        ImplementerInfo* impInfo = implAssoc->mImplementer;
        osafassert(impInfo);
        if (++sLastContinuationId >= 0xfffffffe) {
          sLastContinuationId = 1;
        }
        /* incremented sLastContinuationId unconditionally before
           check on impInfo->mDying because mDying is not purely
           fevs regulated => we could get discrepancy on sLastContinuationId.
           The locally lost implementer connection below may seem dangerous
           because we do an early abort of the apply attempt only on this node,
           but not globally. But the other nodes will sitll wait for a reply
           from the dead implementer, which they will never get, i.e. they
           will abort also.
           Important though that the sLastContinuationId is in step.
         */
        if (impInfo->mDying) {
          LOG_WA(
              "Lost connection with implementer %s, "
              "refusing apply",
              impInfo->mImplementerName.c_str());
          err = SA_AIS_ERR_FAILED_OPERATION;
          ccb->mVeto = SA_AIS_ERR_FAILED_OPERATION;
          setCcbErrorString(
              ccb, IMM_RESOURCE_ABORT "Lost connection with implementer");
          break;
        }
        // Wait for ack, possibly remote
        implAssoc->mWaitForImplAck = true;
        implAssoc->mContinuationId =
            sLastContinuationId; /* incremented above */
        if (osaf_timespec_compare(&ccb->mWaitStartTime, &kZeroSeconds) == 0) {
          osaf_clock_gettime(CLOCK_MONOTONIC, &ccb->mWaitStartTime);
          TRACE("Wait timer for completed started for ccb:%u", ccb->mId);
        }
        SaUint32T implConn = impInfo->mConn;
        if (implConn) {
          connVector.push_back(implConn);
          implIds.push_back(impInfo->mId);
          continuations.push_back(sLastContinuationId);
        }
      }
    }
  }
done:

  TRACE_LEAVE();
  return err;
}

/**
 * This function extracts all no dangling references that exist in "object"
 * and insert them into sReverseRefsNoDanglingMMap, with "object" as a source
 * and the object matching the DN in the no dangling attribute as a target.
 */
void ImmModel::addNoDanglingRefs(ObjectInfo* object) {
  ClassInfo* ci = object->mClassInfo;
  AttrMap::iterator ami;
  ImmAttrValueMap::iterator avmi;
  ImmAttrValue* av;
  ObjectMap::iterator omi;
  ObjectSet objSet;
  ObjectSet::iterator osi;

  TRACE_ENTER();

  for (ami = ci->mAttrMap.begin(); ami != ci->mAttrMap.end(); ++ami) {
    if (ami->second->mFlags & SA_IMM_ATTR_NO_DANGLING) {
      avmi = object->mAttrValueMap.find(ami->first);
      osafassert(avmi != object->mAttrValueMap.end());
      av = avmi->second;
      while (av) {
        if (av->getValueC_str()) {
          omi = sObjectMap.find(av->getValueC_str());
          osafassert(omi != sObjectMap.end());

          //  avoid duplicates
          objSet.insert(omi->second);
        }

        if (av->isMultiValued())
          av = ((ImmAttrMultiValue*)av)->getNextAttrValue();
        else
          break;
      }
    }
  }

  for (osi = objSet.begin(); osi != objSet.end(); ++osi) {
    sReverseRefsNoDanglingMMap.insert(
        std::pair<ObjectInfo*, ObjectInfo*>(*osi, object));
  }

  TRACE_LEAVE();
}

/**
 * Delete from sReverseRefsNoDanglingMMap all NO_DANGLING reference values
 * existing *in* 'object'. If "removeRefsToObject" parameter is set to true,
 * then *also* delete from sReverseRefsNoDanglingMMap all NO_DANGLING references
 * pointing *to* 'object.'. "removeRefsToObject" parameter is set to "false" by
 * default, and is an optional parameter.
 */
void ImmModel::removeNoDanglingRefs(ObjectInfo* object, ObjectInfo* afim,
                                    bool removeRefsToObject) {
  ClassInfo* ci = object->mClassInfo;
  AttrMap::iterator ami;
  ImmAttrValueMap::iterator avmi;
  ImmAttrValue* av;
  ObjectMap::iterator omi;
  ObjectMMap::iterator ommi;

  TRACE_ENTER();

  for (ami = ci->mAttrMap.begin(); ami != ci->mAttrMap.end(); ++ami) {
    if (ami->second->mFlags & SA_IMM_ATTR_NO_DANGLING) {
      avmi = afim->mAttrValueMap.find(ami->first);
      osafassert(avmi != afim->mAttrValueMap.end());
      av = avmi->second;
      while (av) {
        if (av->getValueC_str()) {
          omi = sObjectMap.find(av->getValueC_str());
          if (omi != sObjectMap.end()) {
            ommi = sReverseRefsNoDanglingMMap.find(omi->second);
            while (ommi != sReverseRefsNoDanglingMMap.end() &&
                   ommi->first == omi->second) {
              if (ommi->second == object) {
                sReverseRefsNoDanglingMMap.erase(ommi);
                break;
              }
              ++ommi;
            }
          }
        }

        if (ami->second->mFlags & SA_IMM_ATTR_MULTI_VALUE)
          av = ((ImmAttrMultiValue*)av)->getNextAttrValue();
        else
          break;
      }
    }
  }

  if (removeRefsToObject) {
    sReverseRefsNoDanglingMMap.erase(object);
  }

  TRACE_LEAVE();
}

/*
 * Select created objects which DNs are stored in "dnSet" and insert them to
 * sReverseRefsNoDanglingMMap as target objects.
 * "obj" will be inserted as a source object.
 */
void ImmModel::addNewNoDanglingRefs(ObjectInfo* obj, ObjectNameSet& dnSet) {
  ObjectNameSet::iterator si;
  ObjectMMap::iterator ommi;
  ObjectMap::iterator omi;

  TRACE_ENTER();

  for (si = dnSet.begin(); si != dnSet.end(); ++si) {
    omi = sObjectMap.find(*si);
    // After all validation, object must exist
    osafassert(omi != sObjectMap.end());

    // Searching for NO_DANGLING reference
    ommi = sReverseRefsNoDanglingMMap.find(omi->second);
    for (;
         ommi != sReverseRefsNoDanglingMMap.end() && ommi->first == omi->second;
         ++ommi) {
      if (ommi->second == obj) {
        break;
      }
    }

    // The reference does not exist. It will be added to
    // sReverseRefsNoDanglingMMap
    if (ommi == sReverseRefsNoDanglingMMap.end() ||
        ommi->first != omi->second) {
      sReverseRefsNoDanglingMMap.insert(
          std::pair<ObjectInfo*, ObjectInfo*>(omi->second, obj));
    }
  }

  TRACE_LEAVE();
}

/*
 * Remove NO_DANGLING references from sReverseRefsNoDanglingMMap where
 * a target object DN is stored in "dnSet", and "obj" is used as a source object
 */
void ImmModel::removeNoDanglingRefSet(ObjectInfo* obj, ObjectNameSet& dnSet) {
  ObjectNameSet::iterator si;
  ObjectMMap::iterator ommi;
  ObjectMap::iterator omi;

  TRACE_ENTER();

  for (si = dnSet.begin(); si != dnSet.end(); ++si) {
    omi = sObjectMap.find(*si);
    if (omi != sObjectMap.end()) {
      for (ommi = sReverseRefsNoDanglingMMap.find(omi->second);
           ommi != sReverseRefsNoDanglingMMap.end() &&
           ommi->first == omi->second;
           ++ommi) {
        if (ommi->second == obj) {
          sReverseRefsNoDanglingMMap.erase(ommi);
          break;
        }
      }
    }
  }

  TRACE_LEAVE();
}

void ImmModel::commitCreate(ObjectInfo* obj) {
  if (obj->mObjFlags & IMM_NO_DANGLING_FLAG) {
    /* It's more efficient to first remove all NO_DANGLING references, and
     * then add them all again.
     * Otherwise, we need to have a lookup for each NO_DANGLING reference, and
     * check if NO_DANGLING references exist.
     * There could be a case when a target object is created in the same CCB,
     * then NO_DANGLING references, that refer to the target object,
     * don't exist yet.
     */
    removeNoDanglingRefs(obj, obj);
    addNoDanglingRefs(obj);
  }

  // obj->mCreateLock = false;
  obj->mObjFlags &= ~(IMM_CREATE_LOCK | IMM_NO_DANGLING_FLAG);
  /*TRACE_5("Flags after remove create lock:%u", obj->mObjFlags);*/
}

bool ImmModel::commitModify(const std::string& dn, ObjectInfo* afterImage) {
  TRACE_ENTER();
  TRACE_5("COMMITING MODIFY of %s", dn.c_str());
  ObjectMap::iterator oi = sObjectMap.find(dn);
  osafassert(oi != sObjectMap.end());
  ObjectInfo* beforeImage = oi->second;
  ClassInfo* classInfo = beforeImage->mClassInfo;
  osafassert(classInfo);
  if (beforeImage->mAdminOwnerAttrVal->empty()) {
    /* Empty Admin Owner can imply (hard) release during apply/commit.
       This can happen if client invokes apply and then disconnects
       without waiting for reply. Typically because of timeout on
       the syncronous ccbApply. This can happen for large CCBs
       and/or with a sluggish PBE. The releaseOn finalize will
       have auto-released the adminOwner on the before-image but
       not on the after image of modify. Corrected here.
     */
    afterImage->mAdminOwnerAttrVal->setValueC_str(NULL);
  }

  /* The set of references in the before-image minus the set of references
     in the after-image constitutes the set of references *removed* by
     the (possibly chained) modify operation.
     The set of references in the after-image minus the set of references
     in the before-image constitutes the set of  references *added* by
     the (possibly chained) modify operation.
  */
  if (beforeImage->mObjFlags & IMM_NO_DANGLING_FLAG) {
    ObjectNameSet origNDRefs, afimNDRefs, deletedNDRefs, addedNDRefs;

    beforeImage->mObjFlags ^= IMM_NO_DANGLING_FLAG;

    collectNoDanglingRefs(beforeImage, origNDRefs);
    collectNoDanglingRefs(afterImage, afimNDRefs);

    std::set_difference(origNDRefs.begin(), origNDRefs.end(),
                        afimNDRefs.begin(), afimNDRefs.end(),
                        std::inserter(deletedNDRefs, deletedNDRefs.end()));

    removeNoDanglingRefSet(beforeImage, deletedNDRefs);

    std::set_difference(afimNDRefs.begin(), afimNDRefs.end(),
                        origNDRefs.begin(), origNDRefs.end(),
                        std::inserter(addedNDRefs, addedNDRefs.end()));

    addNewNoDanglingRefs(beforeImage, addedNDRefs);
  }

  //  sObjectMap.erase(oi);
  // sObjectMap[dn] = afterImage;
  // instead of switching ObjectInfo record, I move the attribute values
  // from the after-image tothe before-image. This to avoid having to
  // update stuff such as AdminOwnerInfo->mTouchedObjects

  ImmAttrValueMap::iterator oavi;
  for (oavi = beforeImage->mAttrValueMap.begin();
       oavi != beforeImage->mAttrValueMap.end(); ++oavi) {
    AttrMap::iterator i4 = classInfo->mAttrMap.find(oavi->first);
    osafassert(i4 != classInfo->mAttrMap.end());
    if (i4->second->mFlags & SA_IMM_ATTR_CONFIG) {
      delete oavi->second;
    }
  }

  for (oavi = afterImage->mAttrValueMap.begin();
       oavi != afterImage->mAttrValueMap.end(); ++oavi) {
    AttrMap::iterator i4 = classInfo->mAttrMap.find(oavi->first);
    osafassert(i4 != classInfo->mAttrMap.end());
    osafassert(i4->second->mFlags & SA_IMM_ATTR_CONFIG);
    beforeImage->mAttrValueMap[oavi->first] = oavi->second;
    if (oavi->first == std::string(SA_IMM_ATTR_ADMIN_OWNER_NAME)) {
      beforeImage->mAdminOwnerAttrVal = oavi->second;
    }
  }
  afterImage->mAttrValueMap.clear();
  delete afterImage;
  if (dn == immManagementDn) {
    /* clumsy solution to check every modify for this.
       TODO: catch this in the modify op OI handler to
       prevent datatype error. ? Problem is that seems not
       tight enough.
    */
    SaImmRepositoryInitModeT oldMode = immInitMode;
    immInitMode = getRepositoryInitMode();
    if (oldMode != immInitMode) {
      if (immInitMode == SA_IMM_KEEP_REPOSITORY) {
        LOG_NO("SaImmRepositoryInitModeT changed to: SA_IMM_KEEP_REPOSITORY");
      } else {
        LOG_NO("SaImmRepositoryInitModeT changed to: SA_IMM_INIT_FROM_FILE");
      }
      TRACE_LEAVE();
      return true;
    }
  }
  TRACE_LEAVE();
  return false;
}

void ImmModel::commitDelete(const std::string& dn) {
  TRACE_ENTER();
  TRACE_5("COMMITING DELETE of %s", dn.c_str());
  ObjectMap::iterator oi = sObjectMap.find(dn);
  osafassert(oi != sObjectMap.end());

  if (oi->second->mObjFlags & IMM_NO_DANGLING_FLAG) {
    removeNoDanglingRefs(oi->second, oi->second, true);
  }

  if (oi->second->mObjFlags & IMM_DELETE_ROOT) {
    oi->second->mObjFlags &= ~IMM_DELETE_ROOT;

    ObjectInfo* grandParent = oi->second->mParent;
    while (grandParent) {
      std::string gpDn;
      getObjectName(grandParent, gpDn);
      osafassert(grandParent->mChildCount >= (oi->second->mChildCount + 1));
      grandParent->mChildCount -= (oi->second->mChildCount + 1);
      TRACE_5(
          "Childcount for (grand)parent %s of deleted root %s adjusted to %u",
          gpDn.c_str(), dn.c_str(), grandParent->mChildCount);
      grandParent = grandParent->mParent;
    }
  }

  ImmAttrValueMap::iterator oavi;
  for (oavi = oi->second->mAttrValueMap.begin();
       oavi != oi->second->mAttrValueMap.end(); ++oavi) {
    delete oavi->second;
  }

  // Empty the collection, probably not necessary (as the ObjectInfo
  // record is deleted below), but does not hurt.
  oi->second->mAttrValueMap.clear();

  osafassert(!oi->second->mClassInfo->mExtent.empty());
  osafassert(oi->second->mClassInfo->mExtent.erase(oi->second) == 1);

  TRACE_5("delete object '%s'", oi->first.c_str());
  AdminOwnerVector::iterator i2;

  // adminOwner->mTouchedObjects.erase(obj);
  // TODO This is not so efficient. Correct with Ccbs.
  for (i2 = sOwnerVector.begin(); i2 != sOwnerVector.end(); ++i2) {
    (*i2)->mTouchedObjects.erase(oi->second);
    // Note that on STL sets, the erase operation is polymorphic.
    // Here we are erasing based on value, not iterator position.
  }

  delete oi->second;
  sObjectMap.erase(oi);

  /* Remove any object instance appliers */
  ImplementerSetMap::iterator ismIter = sObjAppliersMap.find(dn);
  if (ismIter != sObjAppliersMap.end()) {
    ImplementerSet* implSet = ismIter->second;
    implSet->clear();
    delete implSet;
    sObjAppliersMap.erase(ismIter);
  }
  TRACE_LEAVE();
}

bool ImmModel::ccbCommit(SaUint32T ccbId, ConnVector& connVector) {
  TRACE_ENTER();
  CcbVector::iterator i;
  bool pbeModeChange = false;
  bool ccbNotEmpty = false;

  i = std::find_if(sCcbVector.begin(), sCcbVector.end(), CcbIdIs(ccbId));
  osafassert(i != sCcbVector.end());
  TRACE_5("Commit CCB %u", (*i)->mId);
  CcbInfo* ccb = (*i);
  osafassert(ccb->isOk());
  if (ccb->mState == IMM_CCB_PREPARE) {
    ccb->mState = IMM_CCB_CRITICAL;
  } else {
    TRACE_5("Ccb %u comitted by PBE now in state:%u", ccbId, ccb->mState);
    osafassert(ccb->mState == IMM_CCB_CRITICAL);
    TRACE_5("Comitting Ccb %u in IMMND", ccbId);
  }
  ccb->mWaitStartTime = kZeroSeconds;

  // Do the actual commit!
  ObjectMutationMap::iterator omit;
  for (omit = ccb->mMutations.begin(); omit != ccb->mMutations.end(); ++omit) {
    ccbNotEmpty = true;
    ObjectMutation* omut = omit->second;
    osafassert(!omut->mWaitForImplAck);
    switch (omut->mOpType) {
      case IMM_CREATE:
        if (ccbId != 1) {
          TRACE_5("COMMITING CREATE of %s", omit->first.c_str());
        }
        osafassert(omut->mAfterImage);
        commitCreate(omut->mAfterImage);
        omut->mAfterImage = NULL;
        break;
      case IMM_MODIFY:
        osafassert(omut->mAfterImage);
        pbeModeChange =
            commitModify(omit->first, omut->mAfterImage) || pbeModeChange;
        omut->mAfterImage = NULL;
        break;
      case IMM_DELETE:
        osafassert(omut->mAfterImage == NULL);
        commitDelete(omit->first);
        break;
      default:
        abort();
    }  // switch
    delete omut;
  }  // for

  ccb->mMutations.clear();
  ccb->removeAllObjReadLocks();

  if (sCcbIdLongDnGuard == ccbId) {
    sCcbIdLongDnGuard = 0;
  }

  // If there are implementers involved then send the final apply callback
  // to them and remove the implementers from the Ccb.

  CcbImplementerMap::iterator isi;
  for (isi = ccb->mImplementers.begin(); isi != ccb->mImplementers.end();
       ++isi) {
    ccbNotEmpty = true;
    ImplementerCcbAssociation* implAssoc = isi->second;
    osafassert(!(implAssoc->mWaitForImplAck));
    ImplementerInfo* impInfo = implAssoc->mImplementer;
    osafassert(impInfo);
    SaUint32T implConn = impInfo->mConn;
    if (implConn) {
      if (impInfo->mDying) {
        LOG_WA(
            "LOST CONNECTION WITH IMPLEMENTER %s AFTER COMPLETED OK BUT "
            "BEFORE APPLY UC. CCB %u will apply ayway. "
            "Discrepancy between Immsv and this implementer may be "
            "the result",
            impInfo->mImplementerName.c_str(), ccbId);

      } else {
        connVector.push_back(implConn);
      }
    }
    delete isi->second;  // Should not affect the iteration
  }

  ccb->mImplementers.clear();

  // With FEVS and no PBE the critical phase is trivially completed.
  // We do not wait for replies from peer imm-server-nodes or from
  // implementers on the apply callback (no return code).
  ccb->mState = IMM_CCB_COMMITTED;
  if (ccbNotEmpty) {
    AdminOwnerVector::iterator i;
    i = std::find_if(sOwnerVector.begin(), sOwnerVector.end(),
                     IdIs(ccb->mAdminOwnerId));
    if (i != sOwnerVector.end()) {
      LOG_NO("Ccb %u COMMITTED (%s)", ccb->mId, (*i)->mAdminOwnerName.c_str());
    } else {
      LOG_NO("Ccb %u COMMITTED (%s)", ccb->mId, "<released>");
    }
  }
  return pbeModeChange;
}

bool ImmModel::ccbAbort(SaUint32T ccbId, ConnVector& connVector,
                        ConnVector& clVector, SaUint32T* nodeId,
                        unsigned int* pbeNodeIdPtr) {
  SaUint32T pbeConn = 0;
  TRACE_ENTER();
  CcbVector::iterator i;

  i = std::find_if(sCcbVector.begin(), sCcbVector.end(), CcbIdIs(ccbId));
  if (i == sCcbVector.end()) {
    LOG_WA("Could not find ccb %u ignoring abort", ccbId);
    TRACE_LEAVE();
    return false;
  }
  TRACE_5("ABORT CCB %u", ccbId);
  CcbInfo* ccb = (*i);

  switch (ccb->mState) {
    case IMM_CCB_EMPTY:
    case IMM_CCB_READY:
      break;  // OK
    case IMM_CCB_CREATE_OP:
      LOG_NO(
          "Aborting ccb %u while waiting for "
          "reply from implementer on CREATE-OP",
          ccbId);
      break;

    case IMM_CCB_MODIFY_OP:
      LOG_NO(
          "Aborting ccb %u while waiting for "
          "reply from implementers on MODIFY-OP",
          ccbId);
      break;

    case IMM_CCB_DELETE_OP:
      LOG_NO(
          "Aborting ccb %u while waiting for "
          "replies from implementers on DELETE-OP",
          ccbId);
      break;

    case IMM_CCB_PREPARE:
    case IMM_CCB_VALIDATING:
      LOG_NO("Ccb %u aborted in COMPLETED processing (validation)", ccbId);
      break;

    case IMM_CCB_VALIDATED:
      LOG_NO("Ccb %u aborted after explicit validation", ccbId);
      break;

    case IMM_CCB_CRITICAL:
      LOG_WA("CCB %u is in critical state, can not abort", ccbId);
      TRACE_LEAVE();
      return false;

    case IMM_CCB_PBE_ABORT:
      TRACE_5("Aborting ccb %u because PBE decided ABORT", ccbId);
      break;

    case IMM_CCB_COMMITTED:
      TRACE_5("CCB %u was already committed", ccbId);
      TRACE_LEAVE();
      return false;

    case IMM_CCB_ABORTED:
      TRACE_5("CCB %u was already aborted", ccbId);
      TRACE_LEAVE();
      return false;

    default:
      LOG_ER("Illegal state %u in ccb %u", ccb->mState, ccbId);
      abort();
  }

  if (ccb->mMutations.empty()) {
    TRACE_5("Ccb %u ABORTED", ccb->mId);
  } else {
    AdminOwnerVector::iterator i;
    i = std::find_if(sOwnerVector.begin(), sOwnerVector.end(),
                     IdIs(ccb->mAdminOwnerId));
    if (i != sOwnerVector.end()) {
      LOG_NO("Ccb %u ABORTED (%s)", ccb->mId, (*i)->mAdminOwnerName.c_str());
    } else {
      LOG_NO("Ccb %u ABORTED (%s)", ccb->mId, "<released>");
    }
  }

  /* Only send response when ccb continuation is not purged */
  if (!ccb->mPurged && ccb->mState != IMM_CCB_EMPTY &&
      ccb->mState != IMM_CCB_READY && ccb->mState != IMM_CCB_VALIDATED) {
    clVector.push_back(ccb->mOriginatingConn);
    *nodeId = ccb->mOriginatingNode;
  }

  if (ccb->mAugCcbParent && ccb->mAugCcbParent->mOriginatingConn) {
    if (ccb->mOriginatingConn && !ccb->mPurged &&
        ccb->mState != IMM_CCB_EMPTY && ccb->mState != IMM_CCB_READY &&
        ccb->mState != IMM_CCB_VALIDATED) {
      /* Case where augumented client and Augumented parent
         are originated from ths node. Send the client response
         to both clients. */
      osafassert(*nodeId == ccb->mAugCcbParent->mOriginatingNode);
    } else {
      /* Case where augumented client is not orignated from this node
         and Augumented parent is originated from this node.
         Send the response to the Augumented parent. */
      *nodeId = ccb->mAugCcbParent->mOriginatingNode;
      if (!clVector.empty()) {
        clVector.pop_back();
      }
    }
    clVector.push_back(ccb->mAugCcbParent->mOriginatingConn);
  }

  ccb->mState = IMM_CCB_ABORTED;
  if (ccb->mVeto == SA_AIS_OK) {
    /* An abort of a CCB currently not vetoed is most likely due to
       either OI-timeout, or explicit abort by the om-client, or
       abort by special admin-operation. All such cases must be
       regarded as a resource error rather than a validation error.
       Only an explicit reject of a ccb from an OI can be defined as
       a validation error. Explicit abort by om-client or admin-op is
       not really any error at all, but mVeto should not be SA_AIS_OK.
    */
    ccb->mVeto = SA_AIS_ERR_NO_RESOURCES;
    setCcbErrorString(ccb, IMM_RESOURCE_ABORT
                      "CCB abort due to either "
                      "OI timeout or explicit abort request.");
  }

  ccb->mWaitStartTime = kZeroSeconds;

  CcbImplementerMap::iterator isi;
  for (isi = ccb->mImplementers.begin(); isi != ccb->mImplementers.end();
       ++isi) {
    ImplementerCcbAssociation* implAssoc = isi->second;
    ImplementerInfo* impInfo = implAssoc->mImplementer;
    osafassert(impInfo);
    SaUint32T implConn = impInfo->mConn;
    if (implConn) {
      if (impInfo->mDying) {
        LOG_WA(
            "Lost connection with implementer %s, cannot send "
            "abort upcall. Ccb %u is aborted anyway.",
            impInfo->mImplementerName.c_str(), ccbId);
      } else {
        connVector.push_back(implConn);
        TRACE_5("Abort upcall for implementer %u/%s", impInfo->mId,
                impInfo->mImplementerName.c_str());
      }
    }
    delete isi->second;  // Should not affect the iteration
  }

  ccb->mImplementers.clear();

  if (ccb->mMutations.empty() && pbeNodeIdPtr) {
    TRACE_5("Ccb %u being aborted is empty, avoid involving PBE", ccbId);
    pbeNodeIdPtr = NULL;
  }

  if (pbeNodeIdPtr && getPbeOi(&pbeConn, pbeNodeIdPtr, false)) {
    /* Unsafe getPbeOi used here which is ok since the only
       potential effect is to NOT send an abort upcall to PBE
       when PBE is down.*/
    /* There is a PBE and it is registered at this node.
       Send abort also to pbe.*/

    if (pbeConn) {
      connVector.push_back(pbeConn);
      TRACE_5("Ccb abort upcall for ccb %u for local PBE ", ccbId);
    }
  }

  if (sCcbIdLongDnGuard == ccbId) {
    sCcbIdLongDnGuard = 0;
  }

  return true;
}

/**
 * Deletes a CCB
 */
SaAisErrorT ImmModel::ccbTerminate(SaUint32T ccbId) {
  SaAisErrorT err = SA_AIS_OK;
  TRACE_ENTER();

  CcbVector::iterator i;
  ObjectMap::iterator oi;
  i = std::find_if(sCcbVector.begin(), sCcbVector.end(), CcbIdIs(ccbId));
  if (i == sCcbVector.end()) {
    LOG_NO("ERR_BAD_HANDLE: CCB %u does not exist", ccbId);
    err = SA_AIS_ERR_BAD_HANDLE;
  } else {
    TRACE_5("terminate the CCB %u", (*i)->mId);

    // Delete ObjectMutations and afims.
    CcbInfo* ccb = *i;
    switch (ccb->mState) {
      case IMM_CCB_EMPTY:
      case IMM_CCB_READY:
      case IMM_CCB_COMMITTED:
      case IMM_CCB_ABORTED:
        break;  // OK
      case IMM_CCB_CREATE_OP:
        LOG_WA(
            "Will not terminate ccb %u while waiting for "
            "reply from implementer on create-op",
            ccbId);
        TRACE_LEAVE();
        return SA_AIS_ERR_TRY_AGAIN;

      case IMM_CCB_MODIFY_OP:
        LOG_WA(
            "Will not terminate ccb %u while waiting for "
            "reply from implementers on modify-op",
            ccbId);
        TRACE_LEAVE();
        return SA_AIS_ERR_TRY_AGAIN;

      case IMM_CCB_DELETE_OP:
        LOG_WA(
            "Will not terminate ccb %u while waiting for "
            "replies from implementers on delete-op",
            ccbId);
        TRACE_LEAVE();
        return SA_AIS_ERR_TRY_AGAIN;

      case IMM_CCB_PREPARE:
      case IMM_CCB_VALIDATING:
      case IMM_CCB_PBE_ABORT:
        LOG_WA(
            "Will not terminate ccb %u while waiting for "
            "replies from implementers on completed ack",
            ccbId);
        TRACE_LEAVE();
        return SA_AIS_ERR_TRY_AGAIN;

      case IMM_CCB_VALIDATED: /* Could logically be allowed, but prefer propper
                                 cleanup */
        LOG_NO(
            "Will not terminate ccb %u in validated state, should be aborted first",
            ccbId);
        TRACE_LEAVE();
        return SA_AIS_ERR_TRY_AGAIN;

      case IMM_CCB_CRITICAL:
        LOG_WA("Will not terminate ccb %u in critical state ", ccbId);
        TRACE_LEAVE();
        return SA_AIS_ERR_TRY_AGAIN;
      default:
        LOG_ER("Illegal state %u in ccb %u", ccb->mState, ccbId);
        abort();
    }
    SaUint32T trailingAdmoIdAbortCreate = 0; /* only for abort create */
    AdminOwnerVector::iterator admoIterAbortCreate =
        sOwnerVector.end(); /* only for abort create */
    ObjectMutationMap::iterator omit;
    ObjectSet createsAbortedInCcb;
    for (omit = ccb->mMutations.begin(); omit != ccb->mMutations.end();
         ++omit) {
      ObjectMutation* omut = omit->second;
      ObjectInfo* afim = omut->mAfterImage;
      omut->mAfterImage = NULL;
      switch (omut->mOpType) {
        case IMM_DELETE:
          TRACE_2("Aborting Delete of %s", omit->first.c_str());
          oi = sObjectMap.find(omit->first);
          osafassert(oi != sObjectMap.end());
          oi->second->mObjFlags &= ~IMM_DELETE_LOCK;  // Remove delete lock
          oi->second->mObjFlags &=
              ~IMM_DELETE_ROOT;  // Remove any delete-root flag
          TRACE_5("Flags after remove delete lock:%u", oi->second->mObjFlags);
          break;

        case IMM_CREATE: {
          const std::string& dn = omit->first;
          oi = sObjectMap.find(dn);
          osafassert(oi != sObjectMap.end());
          if (oi->second->mObjFlags & IMM_NO_DANGLING_FLAG) {
            removeNoDanglingRefs(oi->second, oi->second, true);
          }
          sObjectMap.erase(oi);
          osafassert(afim);
          SaUint32T adminOwnerId =
              (omut->mAugmentAdmo) ? omut->mAugmentAdmo : ccb->mAdminOwnerId;
          TRACE_2("Aborting Create of %s admo:%u", omit->first.c_str(),
                  adminOwnerId);
          osafassert(!afim->mClassInfo->mExtent.empty());
          osafassert(afim->mClassInfo->mExtent.erase(afim) == 1);
          // Aborting create => ensure no references to the object from the
          // admin owner for the object. Typically the admo is the same for
          // all objects created in one ccb. The exception would be creates
          // that are augmented to the ccb by an OI.
          if (adminOwnerId != trailingAdmoIdAbortCreate) {
            trailingAdmoIdAbortCreate = adminOwnerId;
            admoIterAbortCreate = std::find_if(
                sOwnerVector.begin(), sOwnerVector.end(), IdIs(adminOwnerId));
            if (admoIterAbortCreate == sOwnerVector.end()) {
              LOG_WA("Admin owner id %u does not exist", adminOwnerId);
            }
          }

          if (admoIterAbortCreate != sOwnerVector.end()) {
            (*admoIterAbortCreate)->mTouchedObjects.erase(afim);
            // Note that on STL sets, the erase operation is
            // polymorphic. Here we are erasing based on value, not
            // iterator position. See commitDelete, which is similar
            // in nature to abortCreate. One difference is that
            // abortCreate can only have references in from the
            // adminOwner of the ccb that creaed it. No other
            // ccb/admin-owner can have seen the object.
          }

          /*Aborting a ccb-create also means we need to decrement mChildcount
            for all parents of the object being reverted. */
          ObjectInfo* grandParent = afim->mParent;
          while (grandParent) {
            std::string gpDn;
            getObjectName(grandParent, gpDn);
            osafassert(grandParent->mChildCount >= (afim->mChildCount + 1));
            --grandParent->mChildCount;
            TRACE_2(
                "Childcount for (grand)parent %s of aborted create %s "
                "decremented to %u",
                gpDn.c_str(), dn.c_str(), grandParent->mChildCount);
            grandParent = grandParent->mParent;
          }

          /* Dont delete afim yet. Needed if aborting create of a subTREE!
             That is, this afim may have children (creates) that are yet
             to be aborted. Afim needed as parent link in decrementing
             childCount. Attributes of the afim also accessed in log info print
             of childcount. Deleted instead at #### below.
          */
          createsAbortedInCcb.insert(afim);
        } break;

        case IMM_MODIFY: {
          oi = sObjectMap.find(omit->first);
          osafassert(oi != sObjectMap.end());
          osafassert(afim);
          // Remove all NO_DANGLING references from the original object and
          // after image object
          removeNoDanglingRefs(oi->second, oi->second);
          removeNoDanglingRefs(oi->second, afim);
          // Add NO_DANGLING references from the original object
          addNoDanglingRefs(oi->second);
          ImmAttrValueMap::iterator oavi;
          for (oavi = afim->mAttrValueMap.begin();
               oavi != afim->mAttrValueMap.end(); ++oavi) {
            delete oavi->second;
          }
          // Empty the collection, probably not necessary (as the
          // ObjectInfo record is deleted below), but does not hurt.
          afim->mAttrValueMap.clear();
          delete afim;
        } break;
        default:
          abort();
      }  // switch
      delete omut;
    }  // for each mutation

    /* #### Now delete afims from aborted creates. */
    ObjectSet::iterator osi = createsAbortedInCcb.begin();
    while (osi != createsAbortedInCcb.end()) {
      ImmAttrValueMap::iterator oavi;
      for (oavi = (*osi)->mAttrValueMap.begin();
           oavi != (*osi)->mAttrValueMap.end(); ++oavi) {
        delete oavi->second;
      }
      (*osi)->mAttrValueMap.clear();
      delete (*osi);
      ++osi;
    }
    createsAbortedInCcb.clear();

    ccb->mMutations.clear();
    ccb->removeAllObjReadLocks();
    if (!ccb->mImplementers.empty()) {
      LOG_WA("Ccb destroyed without notifying some implementers from IMMND.");
      CcbImplementerMap::iterator ix;
      for (ix = ccb->mImplementers.begin(); ix != ccb->mImplementers.end();
           ++ix) {
        struct ImplementerCcbAssociation* impla = ix->second;
        delete impla;
      }
      ccb->mImplementers.clear();
    }

    ccb->mLocalAppliers.clear();
    /*  Retain the ccb info to allow ccb result recovery. */

    if (osaf_timespec_compare(&ccb->mWaitStartTime, &kZeroSeconds) == 0) {
      osaf_clock_gettime(CLOCK_MONOTONIC, &ccb->mWaitStartTime);
      TRACE_5("Ccb Wait-time for GC set. State: %u/%s", ccb->mState,
              (ccb->mState == IMM_CCB_COMMITTED)
                  ? "COMMITTED"
                  : ((ccb->mState == IMM_CCB_ABORTED) ? "ABORTED" : "OTHER"));
    }

    while (ccb->mErrorStrings) {
      ImmsvAttrNameList* errStr = ccb->mErrorStrings;
      free(errStr->name.buf);
      errStr->name.buf = NULL;
      errStr->name.size = 0;
      ccb->mErrorStrings = errStr->next;
      errStr->next = NULL;
      free(errStr);
    }

    if (ccb->mAugCcbParent) {
      while (ccb->mAugCcbParent->mErrorStrings) {
        /* Free error strings */
        ImmsvAttrNameList* errStr = ccb->mAugCcbParent->mErrorStrings;
        free(errStr->name.buf);
        errStr->name.buf = NULL;
        errStr->name.size = 0;
        ccb->mAugCcbParent->mErrorStrings = errStr->next;
        errStr->next = NULL;
        free(errStr);
      }
      free(ccb->mAugCcbParent);
      ccb->mAugCcbParent = NULL;
    }
    // TODO(?) Would be neat to store ccb outcomes in the OpenSafImm object.
  }

  TRACE_LEAVE();
  return err;
}

/**
 * OI wants to augment a CCB
 */
SaAisErrorT ImmModel::ccbAugmentInit(immsv_oi_ccb_upcall_rsp* rsp,
                                     SaUint32T originatingNode,
                                     SaUint32T originatingConn,
                                     SaUint32T* adminOwnerId) {
  SaAisErrorT err = SA_AIS_OK;
  SaUint32T ccbId = rsp->ccbId;
  CcbVector::iterator i;
  CcbInfo* ccb = NULL;
  AdminOwnerVector::iterator i2;
  AdminOwnerInfo* adminOwner = NULL;
  ObjectMutationMap::iterator omuti;
  ObjectInfo* obj = NULL;
  ObjectMap::iterator oi;

  TRACE_ENTER();
  /*Note: objectName is parent-name for the create case! */
  std::string objectName(osaf_extended_name_borrow(&rsp->name));
  osafassert(nameCheck(objectName) || nameToInternal(objectName));

  i = std::find_if(sCcbVector.begin(), sCcbVector.end(), CcbIdIs(ccbId));
  if (i == sCcbVector.end()) {
    LOG_NO("ERR_BAD_HANDLE: CCB %u does not exist", ccbId);
    err = SA_AIS_ERR_BAD_HANDLE;
    goto done;
  }

  ccb = *i;

  if (ccb->mAugCcbParent) {
    TRACE(
        "ERR_TRY_AGAIN: A ccb augmentation already exists on %u => "
        "can not accept another augmentation until curent is completed",
        ccbId);
    err = SA_AIS_ERR_TRY_AGAIN;
    goto done;
  }

  switch (ccb->mState) {
    case IMM_CCB_EMPTY:
    case IMM_CCB_READY:
    case IMM_CCB_COMMITTED:
    case IMM_CCB_ABORTED:
    case IMM_CCB_VALIDATED:
    case IMM_CCB_PBE_ABORT:
    case IMM_CCB_CRITICAL:
      LOG_WA("Ccb Augment attempted in wrong CCB state %u", ccb->mState);
      err = SA_AIS_ERR_BAD_OPERATION;
      goto done;

    case IMM_CCB_VALIDATING:
      LOG_IN("Augment CCB in state VALIDATING");
      goto augment_for_safe_read;

    case IMM_CCB_PREPARE:
      LOG_IN("Augment CCB in state PREPARE %u", IMM_CCB_PREPARE);
      goto augment_for_safe_read;

    case IMM_CCB_CREATE_OP:
      TRACE("Augment CCB in state CREATE_OP");
      for (omuti = ccb->mMutations.begin(); omuti != ccb->mMutations.end();
           ++omuti) {
        if (omuti->second->mContinuationId == rsp->inv) {
          break;
        }
      }
      if (omuti != ccb->mMutations.end()) {
        obj = omuti->second->mAfterImage;
      }
      break;

    case IMM_CCB_MODIFY_OP:
      TRACE("Augment CCB in state MODIFY_OP");
      omuti = ccb->mMutations.find(objectName);
      if (omuti != ccb->mMutations.end()) {
        obj = omuti->second->mAfterImage;
      }
      break;

    case IMM_CCB_DELETE_OP:
      TRACE("Augment CCB in state DELETE_OP");
      omuti = ccb->mMutations.find(objectName);
      if (omuti == ccb->mMutations.end()) {
        break;
      }

      /* Delete mutation has no after image, fetch object from main map */
      oi = sObjectMap.find(objectName);
      osafassert(oi != sObjectMap.end());
      obj = oi->second;
      break;

    default:
      LOG_ER("Illegal state %u in ccb %u", ccb->mState, ccbId);
      abort();
  }

  if (omuti == ccb->mMutations.end()) {
    LOG_WA("invocation '%u' Not found in ccb - aborting ccb %u", rsp->inv,
           ccbId);
    if (ccb->mVeto == SA_AIS_OK) {
      ccb->mVeto = SA_AIS_ERR_FAILED_OPERATION;
      /* Note: This error code is returned to OI. Parent CCB is aborted */
      err = SA_AIS_ERR_BAD_OPERATION;
    }
    goto done;
  }
  /* Check on wait time for ccb, dont want to exceed client wait. */

  TRACE("omuti->second:%p", omuti->second);
  TRACE("omuti->second->mContinuationId:%u == rsp->inv:%u",
        omuti->second->mContinuationId, rsp->inv);

  osafassert(omuti->second->mContinuationId == rsp->inv);
  osafassert(omuti->second->mWaitForImplAck);

augment_for_safe_read:

  TRACE("obj:%p", obj);
  // TRACE("obj->mImplementer:%p", obj->mImplementer);
  // osafassert(obj && obj->mImplementer);

  if (ccb->mVeto != SA_AIS_OK) {
    TRACE("Ccb %u is already in an error state %u, can not accept augmentation",
          ccbId, ccb->mVeto);
    setCcbErrorString(ccb, IMM_RESOURCE_ABORT "CCB is in an error state");
    err = SA_AIS_ERR_FAILED_OPERATION; /*ccb->mVeto;*/
    goto done;
  }

  /* Check on adminOwner release on finalize or not */

  i2 = std::find_if(sOwnerVector.begin(), sOwnerVector.end(),
                    IdIs(ccb->mAdminOwnerId));
  if (i2 == sOwnerVector.end()) {
    LOG_WA("ERR_BAD_HANDLE: admin owner id %u of ccb %u does not exist",
           ccb->mAdminOwnerId, ccbId);
    err = SA_AIS_ERR_BAD_HANDLE;
    goto done;
  }

  adminOwner = *i2;
  if (adminOwner->mDying) {
    LOG_NO("ERR_BAD_HANDLE: admin owner id %u of ccb %u is dying",
           ccb->mAdminOwnerId, ccbId);
    err = SA_AIS_ERR_BAD_HANDLE;
    goto done;
  }

  if (!(adminOwner->mReleaseOnFinalize)) {
    /* OI client intends to use admin-owner handle,
       but release-on-finalize is not set. */
    err = SA_AIS_ERR_NO_SECTIONS;
    /* Not a real error */
  }

  *adminOwnerId = ccb->mAdminOwnerId;

  ccb->mAugCcbParent = new AugCcbParent;
  ccb->mAugCcbParent->mOriginatingConn = ccb->mOriginatingConn;
  ccb->mAugCcbParent->mOriginatingNode = ccb->mOriginatingNode;
  ccb->mAugCcbParent->mState = ccb->mState;
  ccb->mAugCcbParent->mErrorStrings = ccb->mErrorStrings;
  ccb->mAugCcbParent->mContinuationId = rsp->inv;
  ccb->mAugCcbParent->mImplId = (obj) ? obj->mImplementer->mId : 0;
  ccb->mAugCcbParent->mAugmentAdmo = 0;

  ccb->mOriginatingConn = originatingConn;
  ccb->mOriginatingNode = originatingNode;
  if (ccb->mState < IMM_CCB_VALIDATING) {
    ccb->mState = IMM_CCB_READY;
  }

  ccb->mWaitStartTime = kZeroSeconds;
  ccb->mErrorStrings = NULL;

done:
  TRACE_LEAVE();
  return err;
}

void ImmModel::ccbAugmentAdmo(SaUint32T adminOwnerId, SaUint32T ccbId) {
  CcbVector::iterator i1;
  AdminOwnerVector::iterator i2;
  CcbInfo* ccb = NULL;
  TRACE_ENTER();
  i1 = std::find_if(sCcbVector.begin(), sCcbVector.end(), CcbIdIs(ccbId));
  if (i1 == sCcbVector.end()) {
    LOG_WA("ccbAugmentAdmo: ccb id %u does not exist", ccbId);
    goto done;
  }
  ccb = *i1;

  if (ccb->mAugCcbParent == NULL) {
    LOG_WA(
        "ccbAugmentAdmo: could not set augment-admo because ccb %u is not in augment",
        ccbId);
    goto done;
  }

  ccb->mAugCcbParent->mAugmentAdmo = adminOwnerId;
  LOG_NO("Added augment admo-id:%u to ccb:%u, original admo-id:%u",
         adminOwnerId, ccbId, ccb->mAdminOwnerId);

done:
  TRACE_LEAVE();
}

void ImmModel::eduAtValToOs(immsv_octet_string* tmpos, immsv_edu_attr_val* v,
                            SaImmValueTypeT t) {
  // This function simply transforms an IMM value type to a temporary
  // octet string, simplifying the storage model of the internal IMM repository.

  switch (t) {
    case SA_IMM_ATTR_SANAMET:
    case SA_IMM_ATTR_SASTRINGT:
    case SA_IMM_ATTR_SAANYT:
      tmpos->size = v->val.x.size;
      tmpos->buf = v->val.x.buf;  // Warning, only borowing the pointer.
      return;

    case SA_IMM_ATTR_SAINT32T:
      tmpos->size = 4;
      tmpos->buf = (char*)&(v->val.saint32);
      return;

    case SA_IMM_ATTR_SAUINT32T:
      tmpos->size = 4;
      tmpos->buf = (char*)&(v->val.sauint32);
      return;

    case SA_IMM_ATTR_SAINT64T:
      tmpos->size = 8;
      tmpos->buf = (char*)&(v->val.saint64);
      return;

    case SA_IMM_ATTR_SAUINT64T:
      tmpos->size = 8;
      tmpos->buf = (char*)&(v->val.sauint64);
      return;

    case SA_IMM_ATTR_SAFLOATT:
      tmpos->size = 4;
      tmpos->buf = (char*)&(v->val.safloat);
      return;

    case SA_IMM_ATTR_SATIMET:
      tmpos->size = 8;
      tmpos->buf = (char*)&(v->val.satime);
      return;

    case SA_IMM_ATTR_SADOUBLET:
      tmpos->size = 8;
      tmpos->buf = (char*)&(v->val.sadouble);
      return;
  }
}

void ImmModel::getLocalAppliersForCcb(SaUint32T ccbId, ConnVector& cv,
                                      SaUint32T* applCtnPtr) {
  CcbInfo* ccb = 0;
  CcbVector::iterator i1;
  cv.clear();
  *applCtnPtr = 0;

  i1 = std::find_if(sCcbVector.begin(), sCcbVector.end(), CcbIdIs(ccbId));
  if (i1 == sCcbVector.end()) {
    LOG_NO("Ccb id %u does not exist", ccbId);
    abort();
  }

  ccb = *i1;

  if (!ccb->mLocalAppliers.empty()) {
    ImplementerSet::iterator ii;
    for (ii = ccb->mLocalAppliers.begin(); ii != ccb->mLocalAppliers.end();
         ++ii) {
      ImplementerInfo* impl = *ii;
      if (impl->mConn && !impl->mDying && impl->mId) {
        cv.push_back(impl->mConn);
      }
    }
  }

  *applCtnPtr = ccb->mOpCount;
}

void ImmModel::getLocalAppliersForObj(const SaNameT* objName, SaUint32T ccbId,
                                      ConnVector& cv, bool externalRep) {
  CcbInfo* ccb = 0;
  CcbVector::iterator i1;
  cv.clear();

  std::string objectName(osaf_extended_name_borrow(objName));
  if (externalRep && !(nameCheck(objectName) || nameToInternal(objectName))) {
    LOG_ER("Not a proper object name");
    abort();
  }

  ObjectMap::iterator i5 = sObjectMap.find(objectName);
  if (i5 == sObjectMap.end()) {
    LOG_ER("Could not find expected object:%s", objectName.c_str());
    abort();
  }

  ObjectInfo* object = i5->second;
  ClassInfo* classInfo = object->mClassInfo;

  if (classInfo->mCategory == SA_IMM_CLASS_RUNTIME) {
    TRACE("Ignoring runtime object %s", objectName.c_str());
    return;
  }

  i1 = std::find_if(sCcbVector.begin(), sCcbVector.end(), CcbIdIs(ccbId));
  if (i1 == sCcbVector.end()) {
    LOG_NO("Ccb id %u does not exist", ccbId);
    abort();
  }

  ccb = *i1;

  if (!classInfo->mAppliers.empty()) {
    ImplementerSet::iterator ii = classInfo->mAppliers.begin();
    for (; ii != classInfo->mAppliers.end(); ++ii) {
      ImplementerInfo* impl = *ii;
      if (impl->mConn && !impl->mDying && impl->mId) {
        cv.push_back(impl->mConn);
        ccb->mLocalAppliers.insert(impl);
      }
    }
  }

  TRACE("Checking for local instance level appliers for %s",
        objectName.c_str());
  /* Check for instance level object appliers */
  ImplementerSetMap::iterator ismIter = sObjAppliersMap.find(objectName);

  if (ismIter != sObjAppliersMap.end()) {
    ImplementerSet* implSet = ismIter->second;
    ImplementerSet::iterator ii = implSet->begin();
    for (; ii != implSet->end(); ++ii) {
      ImplementerInfo* impl = *ii;
      if (impl->mConn && !impl->mDying && impl->mId) {
        cv.push_back(impl->mConn);
        ccb->mLocalAppliers.insert(impl);
        TRACE("LOCAL instance appliers found for %s", objectName.c_str());
      }
    }
  }

  ImplementerInfo* impl = getSpecialApplier();
  if (impl && specialApplyForClass(classInfo)) {
    cv.push_back(impl->mConn);
    ccb->mLocalAppliers.insert(impl);
  }
}

SaUint32T ImmModel::getPbeApplierConn() {
  SaUint32T conn = 0;
  unsigned int nodeId = 0;
  getPbeBSlave(&conn, &nodeId);

  return conn;
}

/*
  Checks if the there is any local special applier AND if the CCB op callbacks
  have not yet included information about admin-owner-name.
  Admin-owner name is always included in create and modify callbacks directed at
  special appliers. Delete callbacks do not contain any attribute info and so
  can not inform the special applier about the admin-owner-name for the CCB.
  If this function returns true then it signals the need for generating a fake
  modify-callback only containing the system attribute SaImmAttrAdminOwnerName.
 */
bool ImmModel::isSpecialAndAddModify(SaUint32T clientId, SaUint32T ccbId) {
  ImplementerInfo* specialApplier = getSpecialApplier();
  if (specialApplier && !(specialApplier->mDying) &&
      specialApplier->mConn == clientId) {
    CcbVector::iterator i;
    i = std::find_if(sCcbVector.begin(), sCcbVector.end(), CcbIdIs(ccbId));
    osafassert(i != sCcbVector.end());
    CcbInfo* ccb = (*i);
    osafassert(ccb->isActive());
    if (!(ccb->mCcbFlags & OPENSAF_IMM_CCB_ADMO_PROVIDED)) {
      TRACE("isSpecialAndAddModify returns TRUE");
      return true;
    }
  }

  /* Admo already provided, no need for fake modify. */
  TRACE("isSpecialAndAddModify returns FALSE");
  return false;
}

/*
  This function populates the fake modify callback to be directed at special
  appliers, informing them of admin-owner-name related to a ccb.
*/
void ImmModel::genSpecialModify(ImmsvOmCcbObjectModify* req) {
  TRACE_ENTER();
  std::string objAdminOwnerName;
  std::string objectName((const char*)req->objectName.buf);
  osafassert(nameCheck(objectName) || nameToInternal(objectName));
  ObjectMap::iterator oi = sObjectMap.find(objectName);
  osafassert(oi != sObjectMap.end());
  ObjectInfo* object = oi->second;

  CcbVector::iterator i1 =
      std::find_if(sCcbVector.begin(), sCcbVector.end(), CcbIdIs(req->ccbId));
  osafassert(i1 != sCcbVector.end());
  CcbInfo* ccb = *i1;
  osafassert(ccb->isOk() && ccb->isActive());

  AdminOwnerVector::iterator i2 = std::find_if(
      sOwnerVector.begin(), sOwnerVector.end(), IdIs(req->adminOwnerId));
  osafassert(i2 != sOwnerVector.end());
  AdminOwnerInfo* adminOwner = *i2;

  object->getAdminOwnerName(&objAdminOwnerName);

  osafassert(objAdminOwnerName == adminOwner->mAdminOwnerName);
  osafassert(req->attrMods == NULL);

  req->attrMods =
      (IMMSV_ATTR_MODS_LIST*)malloc(sizeof(IMMSV_ATTR_MODS_LIST)); /* alloc1 */
  req->attrMods->attrModType = SA_IMM_ATTR_VALUES_REPLACE;
  req->attrMods->next = NULL;

  req->attrMods->attrValue.attrMoreValues = NULL;
  req->attrMods->attrValue.attrValuesNumber = 1;
  req->attrMods->attrValue.attrValueType = SA_IMM_ATTR_SASTRINGT;
  req->attrMods->attrValue.attrName.size =
      strlen(SA_IMM_ATTR_ADMIN_OWNER_NAME) + 1;
  req->attrMods->attrValue.attrName.buf =
      strdup(SA_IMM_ATTR_ADMIN_OWNER_NAME); /* alloc2 */
  req->attrMods->attrValue.attrValue.val.x.size = objAdminOwnerName.size() + 1;
  req->attrMods->attrValue.attrValue.val.x.buf =
      strdup(objAdminOwnerName.c_str()); /* alloc3 */

  ccb->mCcbFlags |= OPENSAF_IMM_CCB_ADMO_PROVIDED;

  TRACE_LEAVE();
}

immsv_attr_mods_list* ImmModel::specialApplierTrimModify(
    SaUint32T clientId, ImmsvOmCcbObjectModify* req) {
  TRACE_ENTER();
  /*
     Check for special applier, if not present then return modlist unmolested.

     Go through attrmods.
     Filter out any mods on attributes that are not flagged for NOTIFY.

     Canonicalize the remaining application level modifications to be
     of type REPLACE and containing just the after-value.

     Add fake attribute modifications for class-name and for admin-owner.
     Fake admin-owner modification only added once per ccb.

   */

  immsv_attr_mods_list* attrMods = req->attrMods;
  ImplementerInfo* specialApplier = getSpecialApplier();
  if (specialApplier && specialApplier->mConn == clientId) {
    std::string objectName((const char*)req->objectName.buf);
    osafassert(nameCheck(objectName) || nameToInternal(objectName));
    ObjectMap::iterator oi = sObjectMap.find(objectName);
    osafassert(oi != sObjectMap.end());
    ObjectInfo* obj = oi->second; /* Points to before image. */
    ClassInfo* classInfo = obj->mClassInfo;
    CcbInfo* ccb = NULL;

    if (!specialApplyForClass(classInfo)) {
      goto done;
    }

    if ((classInfo->mCategory == SA_IMM_CLASS_CONFIG) && req->ccbId) {
      CcbVector::iterator i1 = std::find_if(
          sCcbVector.begin(), sCcbVector.end(), CcbIdIs(req->ccbId));
      osafassert(i1 != sCcbVector.end());
      ccb = *i1;
    }
    int x = 0;
    immsv_attr_mods_list** head = &attrMods;
    bool processAdmoAttr =
        false; /* AdminOwner attribute added for regular ccbs */
    bool processImplAttr =
        false; /* Implementer attribute added for RTA updates (ccbId ==0) */
    bool processClassAttr = true; /* Allways add class name attribute */
    immsv_attr_mods_list* tmp =
        (immsv_attr_mods_list*)calloc(1, sizeof(immsv_attr_mods_list));

    /* Always prepend fake class-name-attr modification */
    tmp->attrModType =
        SA_IMM_ATTR_VALUES_ADD; /* to trigger replacement below */
    tmp->attrValue.attrName.buf = strdup(SA_IMM_ATTR_CLASS_NAME);
    tmp->attrValue.attrName.size = strlen(tmp->attrValue.attrName.buf) + 1;
    tmp->attrValue.attrValueType = SA_IMM_ATTR_SASTRINGT;
    /* All other members zeroed in calloc above. */
    tmp->next = attrMods;
    immsv_attr_mods_list* current = attrMods = tmp;
    tmp = NULL;
    /* Prepend fake class-name attribute modification done */

    if (ccb) {
      if (ccb->mCcbFlags & OPENSAF_IMM_CCB_ADMO_PROVIDED) {
        TRACE("Special applier already notified of admin-owner for ccb %u",
              req->ccbId);
      } else {
        /* Mark the ccb for admin-owner-name having been sent. */
        ccb->mCcbFlags |= OPENSAF_IMM_CCB_ADMO_PROVIDED;

        /* Add admin-owner as fake modification. */
        processAdmoAttr = true;
        immsv_attr_mods_list* tmp =
            (immsv_attr_mods_list*)calloc(1, sizeof(immsv_attr_mods_list));
        tmp->attrModType =
            SA_IMM_ATTR_VALUES_ADD; /* to trigger replacement below */
        tmp->attrValue.attrName.buf = strdup(SA_IMM_ATTR_ADMIN_OWNER_NAME);
        tmp->attrValue.attrName.size = strlen(tmp->attrValue.attrName.buf) + 1;
        tmp->attrValue.attrValueType = SA_IMM_ATTR_SASTRINGT;
        /* All other members zeroed in calloc above. */
        tmp->next = attrMods;
        current = attrMods = tmp;
        tmp = NULL;
      }
    } else { /* RTA update */
      /* Add implementer-name as fake modification. */
      processImplAttr = true;
      immsv_attr_mods_list* tmp =
          (immsv_attr_mods_list*)calloc(1, sizeof(immsv_attr_mods_list));
      tmp->attrModType =
          SA_IMM_ATTR_VALUES_ADD; /* to trigger replacement below */
      tmp->attrValue.attrName.buf = strdup(SA_IMM_ATTR_IMPLEMENTER_NAME);
      tmp->attrValue.attrName.size = strlen(tmp->attrValue.attrName.buf) + 1;
      tmp->attrValue.attrValueType = SA_IMM_ATTR_SASTRINGT;
      /* All other members zeroed in calloc above. */
      tmp->next = attrMods;
      current = attrMods = tmp;
      tmp = NULL;
    }

    do {
      std::string attName((const char*)current->attrValue.attrName.buf);
      TRACE("Loop count:%u head:%p (*head):%p current:%p next:%p attr:%s", x++,
            head, *head, current, current->next, attName.c_str());

      AttrMap::iterator iatt = classInfo->mAttrMap.find(attName);
      osafassert(iatt != classInfo->mAttrMap.end());

      bool attrIsRuntime = (iatt->second->mFlags & SA_IMM_ATTR_RUNTIME);

      if (iatt->second->mFlags & SA_IMM_ATTR_NOTIFY) {
        TRACE(
            "Attribute %s marked ATTR_NOTIFY, kept for special "
            "applier callback for ccb %u",
            current->attrValue.attrName.buf, req->ccbId);
        goto keep_op;
      } else if (processAdmoAttr) {
        /* Admin-owner attribute was fake added as top list element. */
        osafassert(!processImplAttr);
        processAdmoAttr = false;
        goto keep_op;
      } else if (processImplAttr) {
        /* Implementer attribute was fake added as top list element. */
        osafassert(!processAdmoAttr);
        processImplAttr = false;
        goto keep_op;
      } else if (processClassAttr) {
        /* Class attribute always added as top (or second) list element. */
        processClassAttr = false; /* ABT corrected. */
        goto keep_op;
      }

      /* Remove the attribute modification */
      *head = current->next;
      current->next = NULL;
      immsv_free_attrmods(current);
      current = *head;
      TRACE("Discarded attribute %s", attName.c_str());
      continue;

    keep_op:
      TRACE("Kept attribute %s", attName.c_str());
      /* Canonicalize kept attribute to VALUES_REPLACE */
      if (current->attrModType != SA_IMM_ATTR_VALUES_REPLACE) {
        osafassert(current->attrModType == SA_IMM_ATTR_VALUES_ADD ||
                   current->attrModType == SA_IMM_ATTR_VALUES_DELETE);

        if (current->attrValue.attrValuesNumber) {
          /* Discard the attr-mod add/remove value, if any. */
          immsv_evt_free_att_val_raw(&(current->attrValue.attrValue),
                                     current->attrValue.attrValueType);
          if (current->attrValue.attrValuesNumber > 1) {
            immsv_free_attr_list_raw(current->attrValue.attrMoreValues,
                                     current->attrValue.attrValueType);
            current->attrValue.attrMoreValues = NULL;
          }
          current->attrValue.attrValuesNumber = 0;
        }

        current->attrModType = SA_IMM_ATTR_VALUES_REPLACE;
        TRACE("Replacing attr %s", attName.c_str());
        /* Find the after-value in the AFIM for the object. */
        ObjectMutationMap::iterator omuti;
        if (ccb) {
          /* For config data use after-image. */
          omuti = ccb->mMutations.find(objectName);
          osafassert(omuti != ccb->mMutations.end());
          obj = omuti->second->mAfterImage;
        } else {
          omuti = sPbeRtMutations.find(objectName);
          if (omuti != sPbeRtMutations.end()) {
            /* For persistent RT data use after-image. */
            if (attrIsRuntime) {
              obj = omuti->second->mAfterImage;
            } else {
              /* Config obj and config attr, use before image.
                 Typically the implementer-name attr in a config object.
                 To be appended to a PRTA update on the config object.
               */
              TRACE("Config attribute to append: %s", attName.c_str());
            }
          } /*else normal cached, use the before image*/
        }

        osafassert(obj);

        ImmAttrValueMap::iterator j = obj->mAttrValueMap.find(attName);
        osafassert(j != obj->mAttrValueMap.end());
        ImmAttrValue* attVal = j->second;

        if (attVal->empty() &&
            (j->first == std::string(SA_IMM_ATTR_IMPLEMENTER_NAME)) &&
            obj->mImplementer) {
          j->second->setValueC_str(obj->mImplementer->mImplementerName.c_str());
        }

        if (!attVal->empty()) {
          /* Extract the head value "inline" */
          attVal->copyValueToEdu(
              &(current->attrValue.attrValue),
              (SaImmValueTypeT)current->attrValue.attrValueType);
          if (attVal->extraValues()) {
            /*  Extract extra values */
            osafassert(attVal->isMultiValued());
            ImmAttrMultiValue* multiVal = (ImmAttrMultiValue*)attVal;
            multiVal->copyExtraValuesToEdu(
                &(current->attrValue.attrMoreValues),
                (SaImmValueTypeT)current->attrValue.attrValueType);
          }
          current->attrValue.attrValuesNumber = 1 + attVal->extraValues();
        } else {
          TRACE("attribute %s was EMPTY", attName.c_str());
        }
      }

      head = &((*head)->next);
      current = *head;

    } while (current);
  }

done:
  TRACE_LEAVE();
  return attrMods;
}

void ImmModel::specialApplierSavePrtoCreateAttrs(ImmsvOmCcbObjectCreate* req,
                                                 SaUint32T invocation) {
  TRACE_ENTER();
  ObjectMutationMap::iterator i2;
  for (i2 = sPbeRtMutations.begin(); i2 != sPbeRtMutations.end(); ++i2) {
    if (i2->second->mContinuationId == invocation) {
      break;
    }
  }

  osafassert(i2 != sPbeRtMutations.end());

  ObjectMutation* oMut = i2->second;
  oMut->mSavedData = req->attrValues;
  req->attrValues = NULL;
  TRACE_LEAVE();
}
/* ABT: Unify these two, call unified form immModel_... */
void ImmModel::specialApplierSaveRtUpdateAttrs(ImmsvOmCcbObjectModify* req,
                                               SaUint32T invocation) {
  TRACE_ENTER();
  ObjectMutationMap::iterator i2;
  for (i2 = sPbeRtMutations.begin(); i2 != sPbeRtMutations.end(); ++i2) {
    if (i2->second->mContinuationId == invocation) {
      break;
    }
  }

  osafassert(i2 != sPbeRtMutations.end());

  ObjectMutation* oMut = i2->second;
  oMut->mSavedData = req->attrMods;
  req->attrMods = NULL;
  TRACE_LEAVE();
}

immsv_attr_values_list* ImmModel::specialApplierTrimCreate(
    SaUint32T clientId, ImmsvOmCcbObjectCreate* req) {
  immsv_attr_values_list* attrValues = req->attrValues;
  ImplementerInfo* specialApplier = getSpecialApplier();
  if (specialApplier && specialApplier->mConn == clientId) {
    /* Go through the attrValues and remove all except where the attrdef
       in the classdef has the NOTIFY flag set. */
    size_t sz = strnlen((char*)req->className.buf, (size_t)req->className.size);
    std::string className((const char*)req->className.buf, sz);
    ClassMap::iterator i3 = sClassMap.find(className);
    osafassert(i3 != sClassMap.end());
    ClassInfo* classInfo = i3->second;
    CcbInfo* ccb = NULL;

    if (!specialApplyForClass(classInfo)) {
      goto done;
    }

    if (classInfo->mCategory == SA_IMM_CLASS_CONFIG) {
      CcbVector::iterator i1 = std::find_if(
          sCcbVector.begin(), sCcbVector.end(), CcbIdIs(req->ccbId));
      osafassert(i1 != sCcbVector.end());
      ccb = *i1;
      ccb->mCcbFlags |= OPENSAF_IMM_CCB_ADMO_PROVIDED;
      /* Marks the ccb for admin-owner-name having been sent. */
    } else {
      osafassert(req->ccbId == 0 &&
                 classInfo->mCategory == SA_IMM_CLASS_RUNTIME);
    }

    immsv_attr_values_list** head = &attrValues;
    immsv_attr_values_list* current = attrValues;
    int x = 0;
    do {
      sz = strnlen((char*)current->n.attrName.buf,
                   (size_t)current->n.attrName.size);
      std::string attName((const char*)current->n.attrName.buf, sz);
      TRACE("Loop count:%u head:%p (*head):%p current:%p next:%p attr:%s", x++,
            head, *head, current, current->next, attName.c_str());

      AttrMap::iterator iatt = classInfo->mAttrMap.find(attName);
      osafassert(iatt != classInfo->mAttrMap.end());

      if (iatt->second->mFlags & SA_IMM_ATTR_NOTIFY) {
        TRACE(
            "Attribute %s marked ATTR_NOTIFY, kept for special "
            "applier callback for ccb %u",
            current->n.attrName.buf, req->ccbId);
        goto keep_attr;
      }

      if (iatt->second->mFlags & SA_IMM_ATTR_RDN) {
        TRACE(
            "Attribute %s is the RDN attribute, kept for special "
            "applier callback for ccb %u",
            current->n.attrName.buf, req->ccbId);
        goto keep_attr;
      }

      if (strncmp((char*)current->n.attrName.buf, SA_IMM_ATTR_CLASS_NAME,
                  current->n.attrName.size) == 0) {
        TRACE("Kept class name attribute");
        goto keep_attr;
      }

      if (req->ccbId &&
          strncmp((char*)current->n.attrName.buf, SA_IMM_ATTR_ADMIN_OWNER_NAME,
                  current->n.attrName.size) == 0) {
        goto keep_attr;
      }

      if (!(req->ccbId) &&
          strncmp((char*)current->n.attrName.buf, SA_IMM_ATTR_IMPLEMENTER_NAME,
                  current->n.attrName.size) == 0) {
        goto keep_attr;
      }

      /* Remove the attribute */
      *head = current->next;
      current->next = NULL;
      immsv_free_attrvalues_list(current);
      current = *head;
      TRACE("Discarded attribute %s", attName.c_str());
      continue;

    keep_attr:
      TRACE("Kept attribute %s", attName.c_str());
      head = &((*head)->next);
      current = *head;
    } while (current);
  }

done:
  return attrValues;
}

/* Get after image of an object in a specific CCB */
ObjectInfo* ImmModel::getObjectAfterImageInCcb(const std::string& objName,
                                               SaUint32T ccbId) {
  CcbVector::iterator ci;
  ObjectInfo* afim = NULL;
  ObjectMutationMap::iterator omuti;

  osafassert(!objName.empty());

  /* Get ccb info */
  ci = std::find_if(sCcbVector.begin(), sCcbVector.end(), CcbIdIs(ccbId));
  osafassert(ci != sCcbVector.end());
  CcbInfo* ccb = *ci;

  /* Get object mutation */
  omuti = ccb->mMutations.find(objName);
  osafassert(omuti != ccb->mMutations.end());
  ObjectMutation* oMut = omuti->second;

  /* Get after image */
  if (oMut->mOpType == IMM_CREATE || oMut->mOpType == IMM_MODIFY) {
    afim = oMut->mAfterImage;
  } /* return NULL for IMM_DELETE */

  return afim;
}

/* This function converts value(s) of an attribute into a single attr-mod.
 * attrModType is always SA_IMM_ATTR_VALUES_REPLACE.
 *
 * 'hasLongDns' is set to true when there's at least one value is long dn.
 * If it's NULL or 'true', no checking for long DNs will be done.
 */
immsv_attr_mods_list* ImmModel::attrValueToAttrMod(const ObjectInfo* obj,
                                                   const std::string& attrName,
                                                   SaUint32T attrType,
                                                   SaImmAttrFlagsT attrFlags,
                                                   bool* hasLongDn) {
  ImmAttrValueMap::const_iterator avi = obj->mAttrValueMap.find(attrName);
  osafassert(avi != obj->mAttrValueMap.end());
  ImmAttrValue* attrValue = avi->second;

  immsv_attr_mods_list* attrMod =
      (immsv_attr_mods_list*)calloc(1, sizeof(immsv_attr_mods_list));
  osafassert(attrMod);
  attrMod->attrModType = SA_IMM_ATTR_VALUES_REPLACE;

  /* attrValue.attrValueType */
  attrMod->attrValue.attrValueType = attrType;

  /* attrValue.attrName */
  attrMod->attrValue.attrName.size = strlen(attrName.c_str()) + 1;
  attrMod->attrValue.attrName.buf =
      (char*)malloc(attrMod->attrValue.attrName.size);
  osafassert(attrMod->attrValue.attrName.buf);
  strncpy(attrMod->attrValue.attrName.buf, attrName.c_str(),
          attrMod->attrValue.attrName.size);

  /* attrValue.attrValuesNumber, attrValue.attrValue and
   * attrValue.attrMoreValues */
  attrMod->attrValue.attrValuesNumber = 0;
  if (!attrValue->empty()) {
    attrValue->copyValueToEdu(&(attrMod->attrValue.attrValue),
                              (SaImmValueTypeT)attrType);
    if (attrValue->extraValues()) {
      ImmAttrMultiValue* multiVal = (ImmAttrMultiValue*)attrValue;
      multiVal->copyExtraValuesToEdu(&(attrMod->attrValue.attrMoreValues),
                                     (SaImmValueTypeT)attrType);
    }
    attrMod->attrValue.attrValuesNumber = 1 + attrValue->extraValues();
  } /* else, attrValuesNumber is already set to 0 */

  /* Check for long DN when attribute type is
   * 'SA_IMM_ATTR_SANAMET' or 'SA_IMM_ATTR_SASTRINGT with SA_IMM_ATTR_DN'.
   * Will be skipped if hasLongDn is NULL or already 'true'. */
  if (hasLongDn && !(*hasLongDn) && !attrValue->empty() &&
      (attrType == SA_IMM_ATTR_SANAMET ||
       (attrType == SA_IMM_ATTR_SASTRINGT && (attrFlags & SA_IMM_ATTR_DN)))) {
    if (strlen(attrValue->getValueC_str()) >=
        SA_MAX_UNEXTENDED_NAME_LENGTH) { /* Check head value */
      *hasLongDn = true;                 /* Found! Skip checking more values */
    } else if (attrValue->extraValues()) { /* Check more values */
      ImmAttrMultiValue* multiVal =
          ((ImmAttrMultiValue*)attrValue)->getNextAttrValue();
      while (multiVal) {
        if (!multiVal->empty() && strlen(multiVal->getValueC_str()) >=
                                      SA_MAX_UNEXTENDED_NAME_LENGTH) {
          *hasLongDn = true; /* Found! Stop checking */
          break;             /* while */
        }
        multiVal = multiVal->getNextAttrValue();
      }
    }
  }

  return attrMod;
}

/* This function allocates new memory for canonicalized attribute-modifications.
 * The 'attrMods' of input ImmsvOmCcbObjectModify remains untouched.
 * Remember to free the memory with immsv_free_attrmods().
 */
immsv_attr_mods_list* ImmModel::canonicalizeAttrModification(
    const ImmsvOmCcbObjectModify* req) {
  TRACE_ENTER();
  immsv_attr_mods_list* result = NULL;
  std::string objectName;
  ObjectInfo* afim = NULL;

  /* Get object Name */
  size_t sz = strnlen(req->objectName.buf, (size_t)req->objectName.size);
  objectName.append((const char*)req->objectName.buf, sz);
  osafassert(nameToInternal(objectName));
  osafassert(!objectName.empty());

  /* Get after image */
  osafassert(afim = getObjectAfterImageInCcb(objectName, req->ccbId));

  /* Build canonicalized attr-mod list */
  immsv_attr_mods_list* reqAttrMods = req->attrMods;
  for (; reqAttrMods; reqAttrMods = reqAttrMods->next) {
    size_t sz = strnlen(reqAttrMods->attrValue.attrName.buf,
                        (size_t)reqAttrMods->attrValue.attrName.size);
    std::string attrName((const char*)reqAttrMods->attrValue.attrName.buf, sz);
    immsv_attr_mods_list* attrMod = attrValueToAttrMod(
        afim, attrName, reqAttrMods->attrValue.attrValueType);
    attrMod->next = result;
    result = attrMod;
  }

  TRACE_LEAVE();
  return result;
}

/* This function allocates new memory for attribute-modifications
 * for all writable attributes of object.
 * Remember to free the memory with immsv_free_attrmods().
 */
immsv_attr_mods_list* ImmModel::getAllWritableAttributes(
    const ImmsvOmCcbObjectModify* req, bool* hasLongDn) {
  TRACE_ENTER();
  immsv_attr_mods_list* result = NULL;
  std::string objectName;
  ObjectInfo* afim = NULL;
  AttrMap::iterator ai;

  /* Get object Name */
  size_t sz = strnlen(req->objectName.buf, (size_t)req->objectName.size);
  objectName.append((const char*)req->objectName.buf, sz);
  osafassert(nameToInternal(objectName));
  osafassert(!objectName.empty());

  /* Get after image */
  osafassert(afim = getObjectAfterImageInCcb(objectName, req->ccbId));

  /* Build attr-mod list for all writable attributes */
  osafassert(hasLongDn);
  ClassInfo* classInfo = afim->mClassInfo;
  ai = classInfo->mAttrMap.begin();
  for (; ai != classInfo->mAttrMap.end(); ++ai) {
    if (ai->second->mFlags & SA_IMM_ATTR_WRITABLE) {
      immsv_attr_mods_list* attrMod = attrValueToAttrMod(
          afim, (std::string&)ai->first, ai->second->mValueType,
          ai->second->mFlags, hasLongDn);
      attrMod->next = result;
      result = attrMod;
    }
  }

  TRACE_LEAVE();
  return result;
}

SaAisErrorT ImmModel::verifyImmLimits(ObjectInfo* object,
                                      std::string objectName) {
  SaAisErrorT err = SA_AIS_OK;
  SaUint32T val;

  TRACE_ENTER();
  osafassert(objectName == immObjectDn);
  ImmAttrValueMap::iterator class_avmi =
      object->mAttrValueMap.find(immMaxClasses);
  if (class_avmi != object->mAttrValueMap.end()) {
    osafassert(class_avmi->second);
    val = (SaUint32T)class_avmi->second->getValue_int();
  } else {
    val = IMMSV_MAX_CLASSES;
  }
  if (sClassMap.size() > val) {
    LOG_NO(
        "ERR_NO_RESOURCES: maximum class limit %d has been reched for the cluster",
        val);
    err = SA_AIS_ERR_NO_RESOURCES;
    TRACE_LEAVE();
    return err;
  }

  ImmAttrValueMap::iterator impl_avmi = object->mAttrValueMap.find(immMaxImp);
  if (impl_avmi != object->mAttrValueMap.end()) {
    osafassert(impl_avmi->second);
    val = (SaUint32T)impl_avmi->second->getValue_int();
  } else {
    val = IMMSV_MAX_IMPLEMENTERS;
  }
  if (sImplementerVector.size() > val) {
    LOG_NO(
        "ERR_NO_RESOURCES: maximum implementers limit %d has been reched for the cluster",
        val);
    err = SA_AIS_ERR_NO_RESOURCES;
    TRACE_LEAVE();
    return err;
  }

  ImmAttrValueMap::iterator admi_avmi =
      object->mAttrValueMap.find(immMaxAdmOwn);
  if (admi_avmi != object->mAttrValueMap.end()) {
    osafassert(admi_avmi->second);
    val = (SaUint32T)admi_avmi->second->getValue_int();
  } else {
    val = IMMSV_MAX_ADMINOWNERS;
  }
  if (sOwnerVector.size() > val) {
    LOG_NO(
        "ERR_NO_RESOURCES: maximum adminownerslimit %d has been reched for the cluster",
        val);
    err = SA_AIS_ERR_NO_RESOURCES;
    TRACE_LEAVE();
    return err;
  }

  ImmAttrValueMap::iterator ccb_avmi = object->mAttrValueMap.find(immMaxCcbs);
  if (ccb_avmi != object->mAttrValueMap.end()) {
    osafassert(ccb_avmi->second);
    val = (SaUint32T)ccb_avmi->second->getValue_int();
  } else {
    val = IMMSV_MAX_CCBS;
  }
  if ((sCcbVector.size() - sTerminatedCcbcount) > val) {
    LOG_NO(
        "ERR_NO_RESOURCES: maximum ccbs limit %d has been reched for the cluster",
        val);
    err = SA_AIS_ERR_NO_RESOURCES;
    TRACE_LEAVE();
    return err;
  }

  TRACE_LEAVE();
  return err;
}

/**
 * Creates an object
 */
SaAisErrorT ImmModel::ccbObjectCreate(
    ImmsvOmCcbObjectCreate* req, SaUint32T* implConn, unsigned int* implNodeId,
    SaUint32T* continuationId, SaUint32T* pbeConnPtr,
    unsigned int* pbeNodeIdPtr, std::string& objectName, bool* dnOrRdnIsLong,
    bool isObjectDnUsed) {
  TRACE_ENTER();
  osafassert(dnOrRdnIsLong);
  *dnOrRdnIsLong = false;
  SaAisErrorT err = SA_AIS_OK;
  // osafassert(!immNotWritable());
  // It should be safe to allow old ccbs to continue to mutate the IMM.
  // The sync does not realy start until all ccb's are completed.

  size_t sz = strnlen((char*)req->className.buf, (size_t)req->className.size);
  std::string className((const char*)req->className.buf, sz);

  sz = strnlen((char*)req->parentOrObjectDn.buf,
               (size_t)req->parentOrObjectDn.size);
  std::string parentName((const char*)req->parentOrObjectDn.buf, sz);

  TRACE_2("parentName:%s\n", parentName.c_str());
  objectName.clear();
  SaUint32T ccbId = req->ccbId;  // Warning: SaImmOiCcbIdT is 64-bits
  SaUint32T adminOwnerId = req->adminOwnerId;

  CcbInfo* ccb = NULL;
  AdminOwnerInfo* adminOwner = NULL;
  ClassInfo* classInfo = NULL;
  ObjectInfo* parent = NULL;

  CcbVector::iterator i1;
  AdminOwnerVector::iterator i2;
  ClassMap::iterator i3 = sClassMap.find(className);
  AttrMap::iterator i4;
  ObjectMutation* oMut = 0;
  ObjectMap::iterator i5;
  ImmAttrValueMap::iterator i6;
  MissingParentsMap::iterator mpm = sMissingParents.end();

  immsv_attr_values_list* attrValues = NULL;

  bool nameCorrected = false;
  bool rdnAttFound = false;
  bool isAugAdmo = false;
  bool isSpecialApplForClass = false;
  bool longDnsPermitted = getLongDnsAllowed();

  ObjectSet refObjectSet;

  // int isLoading = this->getLoader() > 0;
  int isLoading = (sImmNodeState == IMM_NODE_LOADING);

  if (sz >= SA_MAX_UNEXTENDED_NAME_LENGTH) {
    if (longDnsPermitted) {
      /* The catch here of this long parent DN case is actually redundant.
         This since a long parent + rdn will be even longer than the long
         parent name and that case is also caught below.
      */
      (*dnOrRdnIsLong) = true;
    } else {
      /* The case of this parent actually existing should not be impossible. */
      LOG_NO(
          "ERR_NAME_TOO_LONG: Parent name '%s' has a long DN. "
          "Not allowed by IMM service or extended names are disabled",
          parentName.c_str());
      err = SA_AIS_ERR_NAME_TOO_LONG;
      goto ccbObjectCreateExit;
    }
  }

  if (!nameCheck(parentName)) {
    if (nameToInternal(parentName)) {
      nameCorrected = true;
    } else {
      LOG_NO("ERR_INVALID_PARAM: Not a proper parent name:%s size:%u",
             parentName.c_str(), (unsigned int)parentName.size());
      err = SA_AIS_ERR_INVALID_PARAM;
      goto ccbObjectCreateExit;
    }
  }

  if (!schemaNameCheck(className)) {
    LOG_NO("ERR_INVALID_PARAM: Not a proper class name:%s", className.c_str());
    err = SA_AIS_ERR_INVALID_PARAM;
    goto ccbObjectCreateExit;
  }

  i1 = std::find_if(sCcbVector.begin(), sCcbVector.end(), CcbIdIs(ccbId));
  if (i1 == sCcbVector.end()) {
    LOG_NO("ERR_BAD_HANDLE: ccb id %u does not exist", ccbId);
    err = SA_AIS_ERR_BAD_HANDLE;
    goto ccbObjectCreateExit;
  }
  ccb = *i1;

  if (!ccb->isOk()) {
    LOG_NO(
        "ERR_FAILED_OPERATION: ccb %u is in an error state "
        "rejecting ccbObjectCreate operation ",
        ccbId);
    err = SA_AIS_ERR_FAILED_OPERATION;
    setCcbErrorString(ccb, IMM_RESOURCE_ABORT "CCB is in an error state");
    goto ccbObjectCreateExit;
  }

  if (ccb->mState > IMM_CCB_READY) {
    LOG_NO(
        "ERR_FAILED_OPERATION: ccb %u is not in an expected state:%u "
        "rejecting ccbObjectCreate operation ",
        ccbId, ccb->mState);
    err = SA_AIS_ERR_FAILED_OPERATION;
    setCcbErrorString(ccb,
                      IMM_RESOURCE_ABORT "CCB is not in an expected state");
    goto ccbObjectCreateExit;
  }

  if (ccb->mAugCcbParent && ccb->mAugCcbParent->mAugmentAdmo) {
    adminOwnerId = ccb->mAugCcbParent->mAugmentAdmo;
    isAugAdmo = true;
    LOG_NO(
        "Augmented ccbCreate uses augmentation AdminOwner %u to get ROF == true",
        adminOwnerId);
  }

  i2 = std::find_if(sOwnerVector.begin(), sOwnerVector.end(),
                    IdIs(adminOwnerId));
  if (i2 == sOwnerVector.end()) {
    LOG_NO("ERR_BAD_HANDLE: admin owner id %u does not exist", adminOwnerId);
    err = SA_AIS_ERR_BAD_HANDLE;
    goto ccbObjectCreateExit;
  }
  adminOwner = *i2;

  osafassert(!adminOwner->mDying);

  if (adminOwner->mId != ccb->mAdminOwnerId) {
    if (isAugAdmo) {
      osafassert(adminOwner->mReleaseOnFinalize);
    } else {
      LOG_WA(
          "ERR_FAILED_OPERATION: Inconsistency between Ccb admoId:%u and AdminOwner-id:%u",
          adminOwner->mId, ccb->mAdminOwnerId);
      err = SA_AIS_ERR_FAILED_OPERATION;
      setCcbErrorString(
          ccb, IMM_RESOURCE_ABORT "Inconsistency between CCB and AdminOwner");
      goto ccbObjectCreateExit;
    }
  }

  if (i3 == sClassMap.end()) {
    TRACE_7("ERR_NOT_EXIST: class '%s' does not exist", className.c_str());
    setCcbErrorString(ccb, "IMM: ERR_NOT_EXIST: class '%s' does not exist",
                      className.c_str());
    err = SA_AIS_ERR_NOT_EXIST;
    goto ccbObjectCreateExit;
  } else if (sPbeRtMutations.find(className) != sPbeRtMutations.end()) {
    TRACE_7(
        "ERR_TRY_AGAIN: class '%s' is currently being created (pending PBE reply)",
        className.c_str());
    err = SA_AIS_ERR_TRY_AGAIN;
    goto ccbObjectCreateExit;
  }

  classInfo = i3->second;

  if ((classInfo->mCategory != SA_IMM_CLASS_CONFIG) && !isLoading) {
    // If class is not config and isloading==true, then a check that only
    // accepts persistent RT attributes is enforced below. Se ***.
    LOG_NO("ERR_INVALID_PARAM: class '%s' is not a configuration class",
           className.c_str());
    err = SA_AIS_ERR_INVALID_PARAM;
    goto ccbObjectCreateExit;
  }

  if (parentName.length() > 0) {
    i5 = sObjectMap.find(parentName);
    if (i5 == sObjectMap.end()) {
      if (isLoading) {
        TRACE_7("Parent object '%s' does not exist..yet", parentName.c_str());
        // Save parentName in sMissingParents. Remove when loaded.
        // Check at end of loading that sMissingParents is empty.
        mpm = sMissingParents.find(parentName);
        if (mpm == sMissingParents.end()) {
          sMissingParents[parentName] = ObjectSet();
          mpm = sMissingParents.find(parentName);
        }
        osafassert(mpm != sMissingParents.end());
      } else {
        TRACE_7("ERR_NOT_EXIST: parent object '%s' does not exist",
                parentName.c_str());
        err = SA_AIS_ERR_NOT_EXIST;
        goto ccbObjectCreateExit;
      }
    } else {
      parent = i5->second;
      if ((parent->mObjFlags & IMM_CREATE_LOCK) && (parent->mCcbId != ccbId)) {
        TRACE_7(
            "ERR_NOT_EXIST: Parent object '%s' is being created in a different "
            "ccb %u from the ccb for this create %u",
            parentName.c_str(), parent->mCcbId, ccbId);
        err = SA_AIS_ERR_NOT_EXIST;
        goto ccbObjectCreateExit;
      }
      if (parent->mObjFlags & IMM_DELETE_LOCK) {
        TRACE_7(
            "ERR_NOT_EXIST: Parent object '%s' is locked for delete in ccb: %u. "
            "Will not allow create of subobject.",
            parentName.c_str(), parent->mCcbId);
        err = SA_AIS_ERR_NOT_EXIST;
        goto ccbObjectCreateExit;
      }
      if (parent->mClassInfo->mCategory == SA_IMM_CLASS_RUNTIME) {
        if (classInfo->mCategory == SA_IMM_CLASS_CONFIG) {
          LOG_NO(
              "ERR_NOT_EXIST: Parent object '%s' is a runtime object."
              "Will not allow create of config sub-object.",
              parentName.c_str());
          err = SA_AIS_ERR_NOT_EXIST;
          goto ccbObjectCreateExit;
        } else {
          osafassert(isLoading);
          /* See check above, only loader allowed to create
             (persistent) rtos this way. Should really check
             that parent and child are persistent. Since
             we know we are loading, we assume we are
             loading a dump that only contains persistent
             runtime objects.
          */
        }
      }
    }
  }

  if (parent) {
    std::string parentAdminOwnerName;
    parent->getAdminOwnerName(&parentAdminOwnerName);
    if ((parentAdminOwnerName != adminOwner->mAdminOwnerName) && !isLoading) {
      LOG_NO("ERR_BAD_OPERATION: parent object not owned by '%s'",
             adminOwner->mAdminOwnerName.c_str());
      err = SA_AIS_ERR_BAD_OPERATION;
      goto ccbObjectCreateExit;
    }
  }

  i4 = std::find_if(classInfo->mAttrMap.begin(), classInfo->mAttrMap.end(),
                    AttrFlagIncludes(SA_IMM_ATTR_RDN));
  if (i4 == classInfo->mAttrMap.end()) {
    LOG_WA("ERR_FAILED_OPERATION: No RDN attribute found in class!");
    err = SA_AIS_ERR_FAILED_OPERATION;  // Should never happen!
    // Here IMMND should assert ??? It cannot happen that class does not have
    // RDN. This could happen due to memory corruption or heap overflow
    setCcbErrorString(ccb, IMM_RESOURCE_ABORT "No RDN found in a class");
    goto ccbObjectCreateExit;
  }

  attrValues = req->attrValues;

  /* Set attribute name and attribute type for RDN (first element in the list)
   */
  if (isObjectDnUsed) {
    attrValues->n.attrName.buf = strdup(i4->first.c_str());
    attrValues->n.attrName.size = i4->first.size();
    attrValues->n.attrValueType = i4->second->mValueType;
  }

  while (attrValues) {
    sz = strnlen((char*)attrValues->n.attrName.buf,
                 (size_t)attrValues->n.attrName.size);
    std::string attrName((const char*)attrValues->n.attrName.buf, sz);

    if (attrName == i4->first) {  // Match on name for RDN attribute
      if (rdnAttFound) {
        LOG_NO(
            "ERR_INVALID_PARAM: Rdn attribute occurs more than once "
            "in attribute list");
        err = SA_AIS_ERR_INVALID_PARAM;
        goto ccbObjectCreateExit;
      }

      rdnAttFound = true;

      if ((attrValues->n.attrValueType != SA_IMM_ATTR_SANAMET) &&
          (attrValues->n.attrValueType != SA_IMM_ATTR_SASTRINGT)) {
        LOG_NO(
            "ERR_INVALID_PARAM: Value type for RDN attribute is "
            "neither SaNameT nor SaStringT");
        err = SA_AIS_ERR_INVALID_PARAM;
        goto ccbObjectCreateExit;
      }

      /* size includes null termination byte. */
      if (((size_t)attrValues->n.attrValue.val.x.size > 65) &&
          (i4->second->mValueType == SA_IMM_ATTR_SASTRINGT)) {
        if (longDnsPermitted) {
          (*dnOrRdnIsLong) = true;
          /* Triggers new message type for OI-create callback to enable
             imma-oi-lib to protect legacy OI that does not support long DNs. */
        } else {
          LOG_NO(
              "ERR_INVALID_PARAM: RDN attribute value %s is too large: %u. Max length is 64 "
              "for SaStringT",
              attrValues->n.attrValue.val.x.buf,
              (attrValues->n.attrValue.val.x.size - 1));
          err = SA_AIS_ERR_INVALID_PARAM;
          goto ccbObjectCreateExit;
        }
      }

      if (attrValues->n.attrValueType != (int)i4->second->mValueType) {
        if (isLoading) {
          // Be lenient on the loader. It assumes RDN is always SaNameT.
          TRACE_7(
              "COMPONENT TEST*** IF I SEE THIS THEN DONT REMOVE THE CODE BRANCH");
          // I dont think we ever get this case.
          osafassert(i4->second->mValueType == SA_IMM_ATTR_SANAMET ||
                     i4->second->mValueType == SA_IMM_ATTR_SASTRINGT);
          attrValues->n.attrValueType = i4->second->mValueType;
        }
      }

      objectName.append((const char*)attrValues->n.attrValue.val.x.buf,
                        strnlen((const char*)attrValues->n.attrValue.val.x.buf,
                                (size_t)attrValues->n.attrValue.val.x.size));
    } else if ((attrValues->n.attrValueType == SA_IMM_ATTR_SANAMET) ||
               (attrValues->n.attrValueType == SA_IMM_ATTR_SASTRINGT)) {
      AttrMap::iterator it = classInfo->mAttrMap.find(attrName);
      if (it == classInfo->mAttrMap.end()) {
        LOG_WA("ERR_INVALID_PARAM: Cannot find attribute '%s'",
               attrName.c_str());
        err = SA_AIS_ERR_INVALID_PARAM;  // Should never happen!
        goto ccbObjectCreateExit;
      }

      if ((attrValues->n.attrValueType == SA_IMM_ATTR_SANAMET) ||
          (it->second->mFlags & SA_IMM_ATTR_DN)) {
        if (attrValues->n.attrValue.val.x.size >=
            SA_MAX_UNEXTENDED_NAME_LENGTH) {
          if (longDnsPermitted) {
            (*dnOrRdnIsLong) = true;
            if (isLoading) {
              sIsLongDnLoaded = true;
            }
          } else {
            LOG_NO(
                "ERR_NAME_TOO_LONG: Attribute '%s' has long name. "
                "Not allowed by IMM service or extended names are disabled",
                attrName.c_str());
            err = SA_AIS_ERR_NAME_TOO_LONG;
            goto ccbObjectCreateExit;
          }
        }

        IMMSV_EDU_ATTR_VAL_LIST* value = attrValues->n.attrMoreValues;
        while (value) {
          if (value->n.val.x.size >= SA_MAX_UNEXTENDED_NAME_LENGTH) {
            if (longDnsPermitted) {
              (*dnOrRdnIsLong) = true;
              if (isLoading) {
                sIsLongDnLoaded = true;
              }
            } else {
              LOG_NO(
                  "ERR_NAME_TOO_LONG: Attribute '%s' has long DN. "
                  "Not allowed by IMM service or extended names are disabled",
                  attrName.c_str());
              err = SA_AIS_ERR_NAME_TOO_LONG;
              goto ccbObjectCreateExit;
            }
          }
          value = value->next;
        }
      }
    }

    attrValues = attrValues->next;
  }

  if (objectName.empty()) {
    LOG_NO("ERR_INVALID_PARAM: no RDN attribute found or value is empty");
    err = SA_AIS_ERR_INVALID_PARAM;
    goto ccbObjectCreateExit;
  }

  if (!nameCheck(objectName)) {
    if (nameToInternal(objectName)) {
      nameCorrected = true;
    } else {
      LOG_NO("ERR_INVALID_PARAM: Not a proper RDN (%s, %u)", objectName.c_str(),
             (unsigned int)objectName.length());
      err = SA_AIS_ERR_INVALID_PARAM;
      goto ccbObjectCreateExit;
    }
  }

  if (objectName.find(',') != std::string::npos) {
    LOG_NO("ERR_INVALID_PARAM: Can not tolerate ',' in RDN");
    err = SA_AIS_ERR_INVALID_PARAM;
    goto ccbObjectCreateExit;
  }

  if (!parentName.empty()) {
    objectName.append(",");
    objectName.append(parentName);
  }

  if (objectName.size() > ((longDnsPermitted)
                               ? kOsafMaxDnLength
                               : (SA_MAX_UNEXTENDED_NAME_LENGTH - 1))) {
    TRACE_7("ERR_NAME_TOO_LONG: DN is too long, size:%u, max size is:%u",
            (unsigned int)objectName.size(), kOsafMaxDnLength);
    err = SA_AIS_ERR_NAME_TOO_LONG;
    goto ccbObjectCreateExit;
  }

  if (objectName.size() >= SA_MAX_UNEXTENDED_NAME_LENGTH) {
    TRACE_7("ccbObjectCreate DN is long, size:%u",
            (unsigned int)objectName.size());
    if (isLoading) {
      sIsLongDnLoaded = true;
    }
    (*dnOrRdnIsLong) = true;
  }

  if ((i5 = sObjectMap.find(objectName)) != sObjectMap.end()) {
    if (i5->second->mObjFlags & IMM_CREATE_LOCK) {
      if (isLoading) {
        LOG_ER(
            "ERR_EXIST: object '%s' is already registered "
            "for creation in a ccb, but not applied yet",
            objectName.c_str());
      } else {
        TRACE_7(
            "ERR_EXIST: object '%s' is already registered "
            "for creation in a ccb, but not applied yet",
            objectName.c_str());
        setCcbErrorString(
            ccb,
            "IMM: ERR_EXIST: object is already registered for creation in a ccb");
      }
    } else {
      TRACE_7("ERR_EXIST: object '%s' exists", objectName.c_str());
    }
    err = SA_AIS_ERR_EXIST;
    goto ccbObjectCreateExit;
  }

  if (err == SA_AIS_OK) {
    TRACE_5("create object '%s'", objectName.c_str());
    ObjectInfo* object = new ObjectInfo();
    object->mCcbId = req->ccbId;
    object->mClassInfo = classInfo;
    object->mImplementer = classInfo->mImplementer;
    // Note: mObjFlags is both initialized and assigned below
    if (nameCorrected) {
      object->mObjFlags = IMM_CREATE_LOCK | IMM_DN_INTERNAL_REP;
    } else {
      object->mObjFlags = IMM_CREATE_LOCK;
    }
    // TRACE_5("Flags after insert create lock:%u", object->mObjFlags);

    // Add attributes to object
    for (i4 = classInfo->mAttrMap.begin(); i4 != classInfo->mAttrMap.end();
         ++i4) {
      AttrInfo* attr = i4->second;

      ImmAttrValue* attrValue = NULL;
      if (attr->mFlags & SA_IMM_ATTR_MULTI_VALUE) {
        if (attr->mDefaultValue.empty() ||
            (isLoading && (attr->mFlags & SA_IMM_ATTR_NO_DANGLING))) {
          /* Don't assign default value to attributes that
           * have default no-dangling reference when loading */
          attrValue = new ImmAttrMultiValue();
        } else {
          attrValue = new ImmAttrMultiValue(attr->mDefaultValue);
        }
      } else {
        if (attr->mDefaultValue.empty() ||
            (isLoading && (attr->mFlags & SA_IMM_ATTR_NO_DANGLING))) {
          /* Don't assign default value to attributes that
           * have default no-dangling reference when loading */
          attrValue = new ImmAttrValue();
        } else {
          attrValue = new ImmAttrValue(attr->mDefaultValue);
        }
      }

      // Set admin owner as a regular attribute and then
      // also a pointer to the attrValue for efficient access.
      if (i4->first == std::string(SA_IMM_ATTR_ADMIN_OWNER_NAME)) {
        attrValue->setValueC_str(adminOwner->mAdminOwnerName.c_str());
        object->mAdminOwnerAttrVal = attrValue;
      }

      object->mAttrValueMap[i4->first] = attrValue;
    }

    // Set attribute values
    immsv_attr_values_list* p = req->attrValues;
    while (p && (err == SA_AIS_OK)) {
      sz = strnlen((char*)p->n.attrName.buf, (size_t)p->n.attrName.size);
      std::string attrName((const char*)p->n.attrName.buf, sz);

      i6 = object->mAttrValueMap.find(attrName);

      if (i6 == object->mAttrValueMap.end()) {
        TRACE_7("ERR_NOT_EXIST: attr '%s' not defined", attrName.c_str());
        setCcbErrorString(ccb, "IMM: ERR_NOT_EXIST: attr '%s' not defined",
                          attrName.c_str());
        err = SA_AIS_ERR_NOT_EXIST;
        break;  // out of for-loop
      }

      i4 = classInfo->mAttrMap.find(attrName);
      osafassert(i4 != classInfo->mAttrMap.end());
      AttrInfo* attr = i4->second;

      if (attr->mValueType != (unsigned int)p->n.attrValueType) {
        LOG_NO("ERR_INVALID_PARAM: attr '%s' type missmatch", attrName.c_str());
        err = SA_AIS_ERR_INVALID_PARAM;
        break;  // out of for-loop
      }

      if ((attr->mFlags & SA_IMM_ATTR_RUNTIME) &&
          !(attr->mFlags & SA_IMM_ATTR_PERSISTENT)) {
        // *** Reject non-persistent rt attributes from created over OM API.
        LOG_NO(
            "ERR_INVALID_PARAM: attr '%s' is a runtime attribute => "
            "can not be assigned over OM-API.",
            attrName.c_str());
        err = SA_AIS_ERR_INVALID_PARAM;
        break;  // out of for-loop
      }

      if (attr->mValueType == SA_IMM_ATTR_SANAMET) {
        if (p->n.attrValue.val.x.size > kOsafMaxDnLength) {
          LOG_NO("ERR_LIBRARY: attr '%s' of type SaNameT is too long:%u",
                 attrName.c_str(), p->n.attrValue.val.x.size - 1);
          err = SA_AIS_ERR_LIBRARY;
          break;  // out of for-loop
        }

        std::string tmpName(
            p->n.attrValue.val.x.buf,
            p->n.attrValue.val.x.size ? p->n.attrValue.val.x.size - 1 : 0);
        if (!(nameCheck(tmpName) || nameToInternal(tmpName))) {
          LOG_NO(
              "ERR_INVALID_PARAM: attr '%s' of type SaNameT contains non "
              "printable characters",
              attrName.c_str());
          err = SA_AIS_ERR_INVALID_PARAM;
          break;  // out of for-loop
        }
      } else if (attr->mValueType == SA_IMM_ATTR_SASTRINGT) {
        /* Check that the string at least conforms to UTF-8 */
        if (p->n.attrValue.val.x.size &&
            !(osaf_is_valid_utf8(p->n.attrValue.val.x.buf))) {
          LOG_NO(
              "ERR_INVALID_PARAM: attr '%s' of type SaStringT has a value "
              "that is not valid UTF-8",
              attrName.c_str());
          err = SA_AIS_ERR_INVALID_PARAM;
          break;  // out of for-loop
        }
      }

      ImmAttrValue* attrValue = i6->second;
      IMMSV_OCTET_STRING tmpos;  // temporary octet string
      eduAtValToOs(&tmpos, &(p->n.attrValue),
                   (SaImmValueTypeT)p->n.attrValueType);
      attrValue->setValue(tmpos);
      if (p->n.attrValuesNumber > 1) {
        /*
          int extraValues = p->n.attrValuesNumber - 1;
        */
        if (!(attr->mFlags & SA_IMM_ATTR_MULTI_VALUE)) {
          LOG_NO(
              "ERR_INVALID_PARAM: attr '%s' is not multivalued yet "
              "multiple values provided in create call",
              attrName.c_str());
          err = SA_AIS_ERR_INVALID_PARAM;
          break;  // out of for loop
        }
        /*
          else if(extraValues !=
          p->attrMoreValues.list.count) {
          LOG_ER("Multivalued attr '%s' should have "
          "%u extra values but %u extra values tranmitted",
          attrName.c_str(), extraValues,
          p->attrMoreValues.list.count);
          //Should not happen, error in packing.
          err = SA_AIS_ERR_LIBRARY;
          break; //out of for-loop
          } */
        else {
          osafassert(attrValue->isMultiValued());
          if (className != immClassName) {
            // Avoid restoring imm object
            ImmAttrMultiValue* multiattr = (ImmAttrMultiValue*)attrValue;

            IMMSV_EDU_ATTR_VAL_LIST* al = p->n.attrMoreValues;
            while (al) {
              eduAtValToOs(&tmpos, &(al->n),
                           (SaImmValueTypeT)p->n.attrValueType);
              if ((attr->mFlags & SA_IMM_ATTR_NO_DUPLICATES) &&
                  (multiattr->hasMatchingValue(tmpos))) {
                LOG_NO(
                    "ERR_INVALID_PARAM: multivalued attr '%s' with NO_DUPLICATES "
                    "yet duplicate values provided in ccb obj-create call. Class:'%s' obj:'%s'.",
                    attrName.c_str(), className.c_str(), objectName.c_str());
                err = SA_AIS_ERR_INVALID_PARAM;
                break;  // out of loop
              }
              multiattr->setExtraValue(tmpos);
              al = al->next;
            }
            // TODO: Verify that attrValuesNumber was correct.
          }
        }
      }  // if(p->n.attrValuesNumber > 1)
      // If reloading (cluster restart), revive implementers as needed.
      if (isLoading && i4->first == std::string(SA_IMM_ATTR_IMPLEMENTER_NAME)) {
        if (!attrValue->empty()) {
          const std::string implName(attrValue->getValueC_str());
          ImplementerInfo* impl = findImplementer(implName);
          if (!impl) {
            TRACE_5("Reviving implementor %s", implName.c_str());
            // Create implementer
            impl = new ImplementerInfo;
            impl->mImplementerName = implName;
            sImplementerVector.push_back(impl);
            impl->mId = 0;
            impl->mConn = 0;
            impl->mNodeId = 0;
            impl->mMds_dest = 0LL;
            impl->mAdminOpBusy = 0;
            impl->mDying = false;
            impl->mApplier = false;
          }
          // Reconnect implementer.
          object->mImplementer = impl;
        }
      }

      p = p->next;
    }  // while(p...

    // Check that all attributes with INITIALIZED flag have been set.
    //
    // Also: append missing attributes and default values to
    // immsv_attr_values_list  so the generated SaImmOiCcbObjectCreateCallbackT_2
    // will be complete. See #847.  But dont append non persistent runtime
    // attributes.
    //
    // Also: write notice message for attributes that have default no-dangling
    // reference
    //
    // Also: Write notice message for default removed attributes which are empty
    isSpecialApplForClass = specialApplyForClass(classInfo);

    for (i6 = object->mAttrValueMap.begin();
         i6 != object->mAttrValueMap.end() && err == SA_AIS_OK; ++i6) {
      ImmAttrValue* attrValue = i6->second;
      std::string attrName(i6->first);

      i4 = classInfo->mAttrMap.find(attrName);
      osafassert(i4 != classInfo->mAttrMap.end());
      AttrInfo* attr = i4->second;

      if ((attr->mFlags & SA_IMM_ATTR_NO_DANGLING) &&
          !attr->mDefaultValue.empty() && attrValue->empty()) {
        osafassert(isLoading);
        LOG_NO(
            "Attribute '%s' of object '%s' is NULL and has a default no-dangling reference, "
            "default is not assigned when loading",
            attrName.c_str(), objectName.c_str());
      }

      if ((attr->mFlags & SA_IMM_ATTR_DEFAULT_REMOVED) && attrValue->empty()) {
        TRACE("Attribute %s has a removed default, the value will be empty",
              attrName.c_str());
      }

      if ((attr->mFlags & SA_IMM_ATTR_INITIALIZED) && attrValue->empty()) {
        LOG_NO(
            "ERR_INVALID_PARAM: attr '%s' must be initialized "
            "yet no value provided in the object create call",
            attrName.c_str());
        err = SA_AIS_ERR_INVALID_PARAM;
        continue;
      }

      if (pbeNodeIdPtr || /* PBE exists */
          (object->mImplementer &&
           object->mImplementer->mNodeId) || /* Implemener exists */
          (!classInfo->mAppliers.empty()) || /* Appliers exists */
          isSpecialApplForClass) {           /* Notifier exists */
        /* PBE or impl or applier or notifier exists => ensure create up-call is
           complete by appending missing attributes.
        */
        bool found = false;
        immsv_attr_values_list** trailing_p = &p;
        for (p = req->attrValues; p != NULL; p = p->next, trailing_p = &p) {
          if (strncmp(attrName.c_str(), p->n.attrName.buf,
                      p->n.attrName.size) == 0) {
            found = true;
          }
        }

        if (!found) {
          TRACE("Attribute %s was missing from create input list",
                attrName.c_str());

          if ((attr->mFlags & SA_IMM_ATTR_RUNTIME) &&
              !(attr->mFlags & SA_IMM_ATTR_PERSISTENT) &&
              !(attr->mFlags & SA_IMM_ATTR_NOTIFY)) {
            /* Persistent runtime attributes appended for PBE will be
               trimmed away if not needed by special applier.
               Notify marked attributes (must be cached) are appeded for
               special applier, will be ignored by PBE if not persistent.
             */
            continue;
          }

          p = new immsv_attr_values_list;
          (*trailing_p) = p;
          p->n.attrName.size = (SaUint32T)attrName.size() + 1;
          p->n.attrName.buf = strdup(attrName.c_str());
          p->n.attrValueType = attr->mValueType;
          p->n.attrMoreValues = NULL;
          if (attrValue->empty()) {
            TRACE("Empty attribute value appended");
            p->n.attrValuesNumber = 0;
            p->n.attrValue.val.sauint64 = 0LL;
          } else {
            TRACE("Attribute with default value appended");
            p->n.attrValuesNumber = 1;
            attrValue->copyValueToEdu(&(p->n.attrValue),
                                      (SaImmValueTypeT)attr->mValueType);
          }
          p->next = req->attrValues;
          req->attrValues = p;
        }
      }
    }  // for(i6=object->...

    /* Check NO_DANGLING references */
    if (err == SA_AIS_OK) {
      ObjectMap::iterator omi;
      ImmAttrValue* attrVal;
      ImmAttrValueMap::iterator avmi;

      for (i4 = classInfo->mAttrMap.begin();
           err == SA_AIS_OK && i4 != classInfo->mAttrMap.end(); ++i4) {
        if (i4->second->mFlags & SA_IMM_ATTR_NO_DANGLING) {
          i6 = object->mAttrValueMap.find(i4->first);
          osafassert(i6 != object->mAttrValueMap.end());

          attrVal = i6->second;
          while (attrVal) {
            if (attrVal->getValueC_str()) {
              omi = sObjectMap.find(attrVal->getValueC_str());
              if (omi != sObjectMap.end()) {
                /* Check if the object is PRTO */
                if (omi->second->mClassInfo->mCategory ==
                    SA_IMM_CLASS_RUNTIME) {
                  if (std::find_if(omi->second->mClassInfo->mAttrMap.begin(),
                                   omi->second->mClassInfo->mAttrMap.end(),
                                   AttrFlagIncludes(SA_IMM_ATTR_PERSISTENT)) ==
                      omi->second->mClassInfo->mAttrMap.end()) {
                    LOG_NO(
                        "ERR_INVALID_PARAM: NO_DANGLING Creation of object %s attribute (%s) "
                        "refers to a non persistent RTO %s (Ccb %u)",
                        objectName.c_str(), i4->first.c_str(),
                        attrVal->getValueC_str(), ccbId);
                    setCcbErrorString(
                        ccb, "IMM: ERR_INVALID_PARAM: "
                        "Object %s refers to a non-persistent RTO (%s)",
                        objectName.c_str(), attrVal->getValueC_str());
                    err = SA_AIS_ERR_INVALID_PARAM;
                    break;
                  }
                }

                if (omi->second->mObjFlags & IMM_DELETE_LOCK) {
                  if (omi->second->mCcbId == ccbId) {
                    LOG_NO(
                        "ERR_BAD_OPERATION: NO_DANGLING Creation of object %s attribute %s "
                        "refers to object %s that will be deleted in same Ccb (Ccb %u)",
                        objectName.c_str(), i4->first.c_str(),
                        attrVal->getValueC_str(), ccbId);
                    setCcbErrorString(
                        ccb, "IMM: ERR_BAD_OPERATION: "
                        "Object %s refers to object flagged for deletion in the same CCB (%s)",
                        objectName.c_str(), attrVal->getValueC_str());
                    err = SA_AIS_ERR_BAD_OPERATION;
                    break;
                  } else {
                    LOG_IN(
                        "ERR_BUSY: NO_DANGLING Creation of object %s attribute %s refers to "
                        "object %s flagged for deletion in other Ccb (%u), this (Ccb %u)",
                        objectName.c_str(), i4->first.c_str(),
                        attrVal->getValueC_str(), omi->second->mCcbId, ccbId);
                    setCcbErrorString(
                        ccb, "IMM: ERR_BUSY: "
                        "Object %s refers to object flagged for deletion in another CCB (%s)",
                        objectName.c_str(), attrVal->getValueC_str());
                    err = SA_AIS_ERR_BUSY;
                    break;
                  }
                }

                if (omi->second->mObjFlags & IMM_CREATE_LOCK) {
                  if (omi->second->mCcbId != ccbId) {
                    LOG_IN(
                        "ERR_BUSY: NO_DANGLING Creation of object %s attribute %s refers to "
                        "object %s flagged for creation in other ccb (%u), this (Ccb %u)",
                        objectName.c_str(), i4->first.c_str(),
                        attrVal->getValueC_str(), omi->second->mCcbId, ccbId);
                    setCcbErrorString(
                        ccb, "IMM: ERR_BUSY: "
                        "Object %s refers to object flagged for creation in another CCB (%s)",
                        objectName.c_str(), attrVal->getValueC_str());
                    err = SA_AIS_ERR_BUSY;
                    break;
                  }
                } else {
                  // Add reference to the existing object
                  refObjectSet.insert(omi->second);
                }
              }

              object->mObjFlags |= IMM_NO_DANGLING_FLAG;
              ccb->mCcbFlags |= OPENSAF_IMM_CCB_NO_DANGLING_MUTATE;
            }

            if (attrVal->isMultiValued()) {
              attrVal = ((ImmAttrMultiValue*)attrVal)->getNextAttrValue();
            } else {
              break;
            }
          }
        }
      }
    }

    // Prepare for call on PersistentBackEnd
    if ((err == SA_AIS_OK) && pbeNodeIdPtr) {
      void* pbe = getPbeOi(pbeConnPtr, pbeNodeIdPtr);
      if (!pbe) {
        if (!ccb->mMutations.empty()) {
          /* ongoing ccb interrupted by PBE down */
          err = SA_AIS_ERR_FAILED_OPERATION;
          ccb->mVeto = err;
          LOG_WA(
              "ERR_FAILED_OPERATION: Persistent back end is down "
              "ccb %u is aborted",
              ccbId);
          setCcbErrorString(ccb, IMM_RESOURCE_ABORT "PBE is down");
        } else {
          /* Pristine ccb can not start because PBE down */
          TRACE_5("ERR_TRY_AGAIN: Persistent back end is down");
          err = SA_AIS_ERR_TRY_AGAIN;
        }
      }
    }

    if (err == SA_AIS_OK) {
      oMut = new ObjectMutation(IMM_CREATE);
      oMut->mAfterImage = object;
      if (isAugAdmo) {
        oMut->mAugmentAdmo = ccb->mAugCcbParent->mAugmentAdmo;
      }

      // Prepare for call on object implementor
      // and add implementer to ccb.

      bool hasImpl = object->mImplementer && object->mImplementer->mNodeId;
      bool ignoreImpl =
          (hasImpl && ccb->mAugCcbParent &&
           (ccb->mOriginatingNode == object->mImplementer->mNodeId) &&
           (ccb->mAugCcbParent->mImplId == object->mImplementer->mId));

      if (ignoreImpl) {
        LOG_IN(
            "Skipping OI upcall for create of %s since OI is same "
            "as the originator of the modify (OI augmented ccb)",
            objectName.c_str());
        goto bypass_impl;
      }

      if (hasImpl) {
        ccb->mState = IMM_CCB_CREATE_OP;
        *implConn = object->mImplementer->mConn;
        *implNodeId = object->mImplementer->mNodeId;

        CcbImplementerMap::iterator ccbi =
            ccb->mImplementers.find(object->mImplementer->mId);
        if (ccbi == ccb->mImplementers.end()) {
          ImplementerCcbAssociation* impla =
              new ImplementerCcbAssociation(object->mImplementer);
          ccb->mImplementers[object->mImplementer->mId] = impla;
        }
        // Create an implementer continuation to ensure wait
        // can be aborted.
        oMut->mWaitForImplAck = true;

        // Increment even if we dont invoke locally
        if (++sLastContinuationId >= 0xfffffffe) {
          sLastContinuationId = 1;
        }
        oMut->mContinuationId = sLastContinuationId;

        if (*implConn) {
          if (object->mImplementer->mDying) {
            LOG_WA(
                "Lost connection with implementer %s in "
                "CcbObjectCreate.",
                object->mImplementer->mImplementerName.c_str());
            *continuationId = 0;
            *implConn = 0;
            // err = SA_AIS_ERR_FAILED_OPERATION;
            // Let the timeout handling take care of it.
            // This really needs to be tested! But how ?

          } else {
            *continuationId = sLastContinuationId;
          }
        }

        TRACE_5("THERE IS AN IMPLEMENTER %u conn:%u node:%x name:%s\n",
                object->mImplementer->mId, *implConn, *implNodeId,
                object->mImplementer->mImplementerName.c_str());
        osaf_clock_gettime(CLOCK_MONOTONIC, &ccb->mWaitStartTime);
      } else if (className == immMngtClass) {
        if (sImmNodeState == IMM_NODE_LOADING) {
          if (objectName != immManagementDn) {
            /* Backwards compatibility for loading. */
            LOG_WA("Imm loading encountered bogus object '%s' of class '%s'",
                   objectName.c_str(), immMngtClass.c_str());
          }
        } else {
          setCcbErrorString(
              ccb,
              "IMM: ERR_BAD_OPERATION: Imm not allowing creates of instances of class '%s'",
              immMngtClass.c_str());
          err = SA_AIS_ERR_BAD_OPERATION;
        }
      } else if (className == immClassName) {
        if (sImmNodeState == IMM_NODE_LOADING) {
          if (objectName != immObjectDn) {
            /* Backwards compatibility for loading. */
            LOG_WA("Imm loading encountered bogus object '%s' of class '%s'",
                   objectName.c_str(), immClassName.c_str());
          }
          if (sIsLongDnLoaded) {
            i6 = object->mAttrValueMap.find(immLongDnsAllowed);
            if (i6 == object->mAttrValueMap.end() || !i6->second) {
              LOG_WA(
                  "ERR_FAILED_OPERATION: Long DN is used during the loading initial data, "
                  "but attribute 'longDnsAllowed' is not defined in class %s",
                  immClassName.c_str());
              err = SA_AIS_ERR_FAILED_OPERATION;
              setCcbErrorString(
                  ccb, IMM_RESOURCE_ABORT
                  "Long DN is used, but attribute longDnsAllowed in not defined");
            } else if (!i6->second->getValue_int()) {
              LOG_WA(
                  "ERR_FAILED_OPERATION: Long DN is used during the loading initial data, "
                  "but longDnsAllowed is set to 0");
              err = SA_AIS_ERR_FAILED_OPERATION;
              setCcbErrorString(
                  ccb, IMM_RESOURCE_ABORT
                  "Long DN is used when longDnsAllowed is set to 0");
            }
          } else {
            /* 'else' branch is not needed. Only for small performance issue.
             * sIsLongDnLoaded is set to true only to avoid extra checks.
             * opensafImm=opensafImm,safApp=safImmService is created only once,
             * and this code will not be executed again.
             */
            sIsLongDnLoaded = true;
          }
          err = verifyImmLimits(object, immObjectDn);
          if (err != SA_AIS_OK) {
            setCcbErrorString(
                ccb, IMM_RESOURCE_ABORT "Verifying of immLimits failed");
          }
        } else {
          setCcbErrorString(
              ccb,
              "IMM: ERR_BAD_OPERATION: Imm not allowing creates of instances of class '%s'",
              immClassName.c_str());
          err = SA_AIS_ERR_BAD_OPERATION;
        }
      } else if (ccb->mCcbFlags & SA_IMM_CCB_REGISTERED_OI) {
        if ((object->mImplementer == NULL) &&
            (ccb->mCcbFlags & SA_IMM_CCB_ALLOW_NULL_OI)) {
          TRACE_7(
              "Null implementer, SA_IMM_CCB_REGISTERED_OI set, "
              "but SA_IMM_CCB_ALLOW_NULL_OI set => safe relaxation");
        } else {
          TRACE_7(
              "ERR_NOT_EXIST: class '%s' does not have an "
              "implementer and flag SA_IMM_CCB_REGISTERED_OI is set",
              className.c_str());
          setCcbErrorString(
              ccb,
              "IMM: ERR_NOT_EXIST: class '%s' does not have an "
              "implementer and flag SA_IMM_CCB_REGISTERED_OI is set",
              className.c_str());
          err = SA_AIS_ERR_NOT_EXIST;
        }
      } else { /* SA_IMM_CCB_REGISTERED_OI NOT set */
        if (object->mImplementer) {
          if (sImmNodeState != IMM_NODE_LOADING) {
            LOG_WA(
                "Object '%s' DOES have an implementer %s, currently "
                "detached, but flag SA_IMM_CCB_REGISTERED_OI is NOT set - UNSAFE!",
                objectName.c_str(),
                object->mImplementer->mImplementerName.c_str());
          }
        } else {
          TRACE_7(
              "Object '%s' has NULL implementer, flag SA_IMM_CCB_REGISTERED_OI "
              "is NOT set - moderately safe.",
              objectName.c_str());
        }
      }
    }

    /* Implementer or applier exist, so we need to reorder attributes, and set
     * RDN as the first attribute */
    if (err == SA_AIS_OK && (pbeNodeIdPtr || *implNodeId)) {
      IMMSV_ATTR_VALUES_LIST* prev = req->attrValues;
      IMMSV_ATTR_VALUES_LIST* t = req->attrValues;
      size_t rdnAttrLen;

      i4 = std::find_if(classInfo->mAttrMap.begin(), classInfo->mAttrMap.end(),
                        AttrFlagIncludes(SA_IMM_ATTR_RDN));
      osafassert(
          i4 !=
          classInfo->mAttrMap
              .end()); /* This should never happen. It's already checked */
      rdnAttrLen = strnlen(i4->first.c_str(), i4->first.size());

      if (!(strnlen(t->n.attrName.buf, t->n.attrName.size) == rdnAttrLen) ||
          strncmp(t->n.attrName.buf, i4->first.c_str(), rdnAttrLen)) {
        // RDN is not the first element in the list
        t = t->next;
        while (t) {
          if ((strnlen(t->n.attrName.buf, t->n.attrName.size) == rdnAttrLen) &&
              !strncmp(t->n.attrName.buf, i4->first.c_str(), rdnAttrLen)) {
            prev->next = t->next;
            t->next = req->attrValues;
            req->attrValues = t;
            break;
          }
          prev = t;
          t = t->next;
        }
      }
      osafassert(
          t);  // This should never happen. RDN must exist in attribute list
    }

  bypass_impl:

    if (err == SA_AIS_OK) {
      osafassert(oMut);
      // This is a create => No need to check if there already is a
      // mutation on this object.
      ccb->mMutations[objectName] = oMut;
      ccb->mOpCount++;

      // Object placed in map before apply/commit as a place-holder.
      // The mCreateLock on the object should prevent premature
      // read access.
      sObjectMap[objectName] = object;
      classInfo->mExtent.insert(object);
      if (parent) {
        osafassert(mpm == sMissingParents.end());
        object->mParent = parent;

        ObjectInfo* grandParent = parent;
        do {
          grandParent->mChildCount++;
          grandParent = grandParent->mParent;
        } while (grandParent);
      } else if (mpm != sMissingParents.end()) {
        mpm->second.insert(object);
        // TRACE("Missing parent %s has child %p", parentName.c_str(), object);
      }

      // Add NO_DANGLING references collected from NO_DANGLING attributes
      ObjectSet::iterator rosi;  // refObjectSet iterator
      for (rosi = refObjectSet.begin(); rosi != refObjectSet.end(); ++rosi) {
        sReverseRefsNoDanglingMMap.insert(
            std::pair<ObjectInfo*, ObjectInfo*>(*rosi, object));
      }

      unsigned int sze = (unsigned int)sObjectMap.size();
      if (sze >= 5000) {
        bool warn = sze > 350000;

        if (sze % 100000 == 0) {
          if (warn) {
            LOG_WA("Number of objects in IMM is:%u", sze);
          } else {
            LOG_NO("Number of objects in IMM is:%u", sze);
          }
        } else if (sze % 10000 == 0) {
          if (warn) {
            LOG_WA("Number of objects in IMM is:%u", sze);
          } else {
            LOG_IN("Number of objects in IMM is:%u", sze);
          }
        }
        if (sze % 1000 == 0) {
          TRACE("Number of objects in IMM is:%u", sze);
        }
      }

      if (adminOwner->mReleaseOnFinalize) {
        adminOwner->mTouchedObjects.insert(object);
      }

      // Move this to commitCreate ?
      if (className == immClassName) {
        if (objectName == immObjectDn) {
          updateImmObject(immClassName);
        } else {
          LOG_WA("IMM OBJECT DN:%s should be:%s", objectName.c_str(),
                 immObjectDn.c_str());
        }
      }

      if (isLoading && !sMissingParents.empty()) {
        mpm = sMissingParents.find(objectName);
        if (mpm != sMissingParents.end()) {
          TRACE_5("Missing parent %s found during loading", objectName.c_str());
          ObjectSet::iterator oi = mpm->second.begin();
          while (oi != mpm->second.end()) {
            /* Correct the pointer from child to parent */
            (*oi)->mParent = object;
            ObjectInfo* grandParent = object;
            do {
              grandParent->mChildCount += ((*oi)->mChildCount + 1);
              grandParent = grandParent->mParent;
            } while (grandParent);
            // TRACE("Parent %s corrected for child %p", objectName.c_str(),
            // *oi);
            ++oi;
          }
          mpm->second.clear();
          sMissingParents.erase(objectName);
        }
      }
    } else {
      // err != SA_AIS_OK
      // Delete object and its attributes
      if (ccb->mState == IMM_CCB_CREATE_OP) {
        ccb->mState = IMM_CCB_READY;
      }
      ImmAttrValueMap::iterator oavi;
      for (oavi = object->mAttrValueMap.begin();
           oavi != object->mAttrValueMap.end(); ++oavi) {
        delete oavi->second;
      }

      // Empty the collection, probably not necessary
      //(as the ObjectInfo record is deleted below),
      // but does not hurt.
      object->mAttrValueMap.clear();
      delete object;

      if (oMut) {
        oMut->mAfterImage = NULL;
        delete oMut;
        oMut = NULL;
      }
    }
  }
ccbObjectCreateExit:

  if ((err != SA_AIS_OK) || !classInfo ||
      (classInfo->mAppliers.empty() && !isSpecialApplForClass)) {
    /* Only class implementors can exist for object create. */
    objectName.clear();
  }
  TRACE_LEAVE();
  return err;
}

/**
 * Collect all NO_DANGLING references from an object
 */
void ImmModel::collectNoDanglingRefs(ObjectInfo* object,
                                     ObjectNameSet& objSet) {
  AttrMap::iterator ami;
  ImmAttrValueMap::iterator avmi;
  ImmAttrMultiValue* multiattr;
  for (ami = object->mClassInfo->mAttrMap.begin();
       ami != object->mClassInfo->mAttrMap.end(); ++ami) {
    if (ami->second->mFlags & SA_IMM_ATTR_NO_DANGLING) {
      avmi = object->mAttrValueMap.find(ami->first);
      osafassert(avmi != object->mAttrValueMap.end());
      if (avmi->second->getValueC_str())
        objSet.insert(avmi->second->getValueC_str());

      if (ami->second->mFlags & SA_IMM_ATTR_MULTI_VALUE) {
        multiattr = ((ImmAttrMultiValue*)avmi->second)->getNextAttrValue();
        while (multiattr) {
          if (multiattr->getValueC_str())
            objSet.insert(multiattr->getValueC_str());

          multiattr = multiattr->getNextAttrValue();
        }
      }
    }
  }
}

/*
 * Check new added NO_DANGLING reference from ObjectModify operation
 */
SaAisErrorT ImmModel::objectModifyNDTargetOk(const char* srcObjectName,
                                             const char* attrName,
                                             const char* targetObjectName,
                                             SaUint32T ccbId) {
  ObjectMap::iterator omi = sObjectMap.find(targetObjectName);
  if (omi != sObjectMap.end()) {
    if (omi->second->mClassInfo->mCategory == SA_IMM_CLASS_RUNTIME) {
      if (std::find_if(omi->second->mClassInfo->mAttrMap.begin(),
                       omi->second->mClassInfo->mAttrMap.end(),
                       AttrFlagIncludes(SA_IMM_ATTR_PERSISTENT)) ==
          omi->second->mClassInfo->mAttrMap.end()) {
        LOG_NO(
            "ERR_INVALID_PARAM: NO_DANGLING Object %s attribute %s refers to "
            "non persistent RTO %s (Ccb:%u)",
            srcObjectName, attrName, targetObjectName, ccbId);
        return SA_AIS_ERR_INVALID_PARAM;
      }
    }
    if (omi->second->mObjFlags & IMM_DELETE_LOCK) {
      if (omi->second->mCcbId == ccbId) {
        LOG_NO(
            "ERR_BAD_OPERATION: NO_DANGLING Object %s attribute %s refers to "
            "object %s flagged for deletion (Ccb:%u)",
            srcObjectName, attrName, targetObjectName, ccbId);
        return SA_AIS_ERR_BAD_OPERATION;
      } else {
        LOG_IN(
            "ERR_BUSY: NO_DANGLING Object %s attribute %s refers to "
            "object %s flagged for deletion in other Ccb(%u), this Ccb(%u)",
            srcObjectName, attrName, targetObjectName, omi->second->mCcbId,
            ccbId);
        return SA_AIS_ERR_BUSY;
      }
    }
    if (omi->second->mObjFlags & IMM_CREATE_LOCK) {
      if (omi->second->mCcbId != ccbId) {
        LOG_IN(
            "ERR_BUSY: NO_DANGLING Object %s attribute %s refers to "
            "object %s flagged for creation in other Ccb(%u), this Ccb(%u)",
            srcObjectName, attrName, targetObjectName, omi->second->mCcbId,
            ccbId);
        return SA_AIS_ERR_BUSY;
      }
    }
  }

  return SA_AIS_OK;
}

/**
 * Modifies an object
 */
SaAisErrorT ImmModel::ccbObjectModify(
    const ImmsvOmCcbObjectModify* req, SaUint32T* implConn,
    unsigned int* implNodeId, SaUint32T* continuationId, SaUint32T* pbeConnPtr,
    unsigned int* pbeNodeIdPtr, std::string& objectName, bool* hasLongDns,
    bool pbeFile, bool* changeRim) {
  TRACE_ENTER();
  osafassert(hasLongDns);
  *hasLongDns = false;
  SaAisErrorT err = SA_AIS_OK;

  // osafassert(!immNotWritable());
  // It should be safe to allow old ccbs to continue to mutate the IMM.
  // The sync does not realy start until all ccb's are completed.
  size_t sz = strnlen(req->objectName.buf, (size_t)req->objectName.size);
  objectName.append((const char*)req->objectName.buf, sz);

  SaUint32T ccbId = req->ccbId;
  SaUint32T ccbIdOfObj = 0;
  SaUint32T adminOwnerId = req->adminOwnerId;

  TRACE_2("Modify objectName:%s ccbId:%u admoId:%u\n", objectName.c_str(),
          ccbId, adminOwnerId);

  CcbInfo* ccb = 0;
  AdminOwnerInfo* adminOwner = 0;
  ClassInfo* classInfo = 0;
  ObjectInfo* object = 0;
  ObjectInfo* afim = 0;

  CcbVector::iterator i1;
  AdminOwnerVector::iterator i2;
  AttrMap::iterator i4;
  ObjectMap::iterator oi;
  ImmAttrValueMap::iterator oavi;
  std::string objAdminOwnerName;
  ObjectMutationMap::iterator omuti;
  ObjectMutation* oMut = 0;
  bool chainedOp = false;
  immsv_attr_mods_list* p = req->attrMods;
  bool modifiedNotifyAttr = false;
  bool longDnsPermitted = getLongDnsAllowed();

  ObjectNameSet afimPreOpNDRefs;  // Set of NO_DANGLING references from after
                                  // image before CCB operation
  bool hasNoDanglingRefs = false;
  bool modifiedImmMngt =
      false; /* true => modification of the SAF immManagement object. */
  bool upgradeReadLock = false;

  if (sz >= SA_MAX_UNEXTENDED_NAME_LENGTH) {
    if (longDnsPermitted) {
      (*hasLongDns) = true;
    } else {
      LOG_NO(
          "ERR_NAME_TOO_LONG: Object name has a long DN. "
          "Not allowed by IMM service or extended names are disabled");
      err = SA_AIS_ERR_NAME_TOO_LONG;
      goto ccbObjectModifyExit;
    }
  }

  if (!(nameCheck(objectName) || nameToInternal(objectName))) {
    LOG_NO("ERR_INVALID_PARAM: Not a proper object name");
    err = SA_AIS_ERR_INVALID_PARAM;
    goto ccbObjectModifyExit;
  }

  i1 = std::find_if(sCcbVector.begin(), sCcbVector.end(), CcbIdIs(ccbId));
  if (i1 == sCcbVector.end()) {
    LOG_NO("ERR_BAD_HANDLE: ccb id %u does not exist", ccbId);
    err = SA_AIS_ERR_BAD_HANDLE;
    goto ccbObjectModifyExit;
  }
  ccb = *i1;

  if (!ccb->isOk()) {
    LOG_NO(
        "ERR_FAILED_OPERATION: ccb %u is in an error state "
        "rejecting ccbObjectModify operation ",
        ccbId);
    err = SA_AIS_ERR_FAILED_OPERATION;
    setCcbErrorString(ccb, IMM_RESOURCE_ABORT "CCB is in an error state");
    goto ccbObjectModifyExit;
  }

  if (ccb->mState > IMM_CCB_READY) {
    LOG_WA(
        "ERR_FAILED_OPERATION: ccb %u is not in an expected state: %u "
        "rejecting ccbObjectModify operation ",
        ccbId, ccb->mState);
    err = SA_AIS_ERR_FAILED_OPERATION;
    setCcbErrorString(ccb,
                      IMM_RESOURCE_ABORT "CCB is not in an expected state");
    goto ccbObjectModifyExit;
  }

  i2 = std::find_if(sOwnerVector.begin(), sOwnerVector.end(),
                    IdIs(adminOwnerId));
  if (i2 == sOwnerVector.end()) {
    LOG_NO("ERR_BAD_HANDLE: admin owner id %u does not exist", adminOwnerId);
    err = SA_AIS_ERR_BAD_HANDLE;
    goto ccbObjectModifyExit;
  }
  adminOwner = *i2;

  osafassert(!adminOwner->mDying);

  if (adminOwner->mId != ccb->mAdminOwnerId) {
    LOG_WA(
        "ERR_FAILED_OPERATION: Inconsistency between Ccb-admoId:%u and "
        "AdminOwnerId:%u",
        adminOwner->mId, ccb->mAdminOwnerId);
    err = SA_AIS_ERR_FAILED_OPERATION;
    setCcbErrorString(
        ccb, IMM_RESOURCE_ABORT "Inconsistency between CCB and AdminOwner");
    goto ccbObjectModifyExit;
  }

  if (objectName.empty()) {
    LOG_NO("ERR_INVALID_PARAM: Empty DN attribute value");
    err = SA_AIS_ERR_INVALID_PARAM;
    goto ccbObjectModifyExit;
  }

  oi = sObjectMap.find(objectName);
  if (oi == sObjectMap.end()) {
    TRACE_7("ERR_NOT_EXIST: object '%s' does not exist", objectName.c_str());
    err = SA_AIS_ERR_NOT_EXIST;
    goto ccbObjectModifyExit;
  }

  object = oi->second;

  modifiedImmMngt = (objectName == immManagementDn);

  object->getAdminOwnerName(&objAdminOwnerName);
  if (objAdminOwnerName != adminOwner->mAdminOwnerName) {
    LOG_NO("ERR_BAD_OPERATION: Mismatch on administrative owner %s != %s",
           objAdminOwnerName.c_str(), adminOwner->mAdminOwnerName.c_str());
    err = SA_AIS_ERR_BAD_OPERATION;
    goto ccbObjectModifyExit;
  }

  ccbIdOfObj = object->mCcbId;
  if (ccbIdOfObj != ccbId) {
    i1 =
        std::find_if(sCcbVector.begin(), sCcbVector.end(), CcbIdIs(ccbIdOfObj));
    if ((i1 != sCcbVector.end()) && ((*i1)->isActive())) {
      TRACE_7("ERR_BUSY: ccb id %u differs from active ccb id on object %u",
              ccbId, ccbIdOfObj);
      err = SA_AIS_ERR_BUSY;
      goto ccbObjectModifyExit;
    }

    if (ccbIdOfObj == 0) {
      ObjectShortCountMap::iterator oscm = sObjectReadLockCount.find(object);
      if (oscm != sObjectReadLockCount.end()) {
        if ((oscm->second > 1) || !(ccb->isReadLockedByThisCcb(object))) {
          TRACE("ERR_BUSY: object '%s' is safe-read-locked by %u ccb(s)",
                objectName.c_str(), oscm->second);
          err = SA_AIS_ERR_BUSY;
          goto ccbObjectModifyExit;
        } else {
          osafassert((oscm->second == 1) &&
                     (ccb->isReadLockedByThisCcb(object)));
          TRACE(
              "CcbModify: Object '%s' safe-read-locked only by this ccb (%u) => "
              "upgrade to exclusive lock is possible",
              objectName.c_str(), ccbId);
          upgradeReadLock = true;
        }
      }
    }
  }

  if (object->mObjFlags & IMM_RT_UPDATE_LOCK) {
    /* Sorry but any ongoing PRTO update will interfere with additional
       ccb ops on the object because the PRTO update has to be reversible.
    */
    LOG_IN(
        "ERR_TRY_AGAIN: Object '%s' already subject of a persistent runtime "
        "attribute update",
        objectName.c_str());
    err = SA_AIS_ERR_TRY_AGAIN;
    goto ccbObjectModifyExit;
  }

  classInfo = object->mClassInfo;
  osafassert(classInfo);

  if (classInfo->mCategory != SA_IMM_CLASS_CONFIG) {
    LOG_NO("ERR_INVALID_PARAM: object '%s' is not a configuration object",
           objectName.c_str());
    err = SA_AIS_ERR_INVALID_PARAM;
    goto ccbObjectModifyExit;
  }

  omuti = ccb->mMutations.find(objectName);
  if (omuti != ccb->mMutations.end()) {
    oMut = omuti->second;
    if (oMut->mOpType == IMM_DELETE) {
      TRACE_7("ERR_NOT_EXIST: object '%s' is scheduled for delete",
              objectName.c_str());
      err = SA_AIS_ERR_NOT_EXIST;
      goto ccbObjectModifyExit;
    } else if (oMut->mOpType == IMM_CREATE) {
      chainedOp = true;
      afim = object;
    } else {
      osafassert(oMut->mOpType == IMM_MODIFY);
      chainedOp = true;
      afim = oMut->mAfterImage;
    }
  }

  // Find each attribute to be modified.
  // Check that attribute is a config attribute.
  // Check that attribute is writable.
  // If multivalue implied check that it is multivalued.
  // Assign the attribute as intended.

  TRACE_5("modify object '%s'", objectName.c_str());

  if (!chainedOp) {
    afim = new ObjectInfo();
    afim->mCcbId = ccbId;
    afim->mClassInfo = classInfo;
    afim->mImplementer = object->mImplementer;
    afim->mObjFlags = object->mObjFlags;
    afim->mParent = object->mParent;
    afim->mChildCount = object->mChildCount; /* Not used, but be consistent. */

    // Copy attribute values from existing object version to afim
    bool hasNoDanglingAttr = false;
    for (oavi = object->mAttrValueMap.begin();
         oavi != object->mAttrValueMap.end(); ++oavi) {
      ImmAttrValue* oldValue = oavi->second;
      ImmAttrValue* newValue = NULL;

      i4 = classInfo->mAttrMap.find(oavi->first);
      osafassert(i4 != classInfo->mAttrMap.end());
      /* Only copy config attributes to afim. */
      if (i4->second->mFlags & SA_IMM_ATTR_CONFIG) {
        if (oldValue->isMultiValued()) {
          newValue = new ImmAttrMultiValue(*((ImmAttrMultiValue*)oldValue));
        } else {
          newValue = new ImmAttrValue(*oldValue);
        }

        // Set admin owner as a regular attribute and then also a pointer
        // to the attrValue for efficient access.
        if (oavi->first == std::string(SA_IMM_ATTR_ADMIN_OWNER_NAME)) {
          afim->mAdminOwnerAttrVal = newValue;
        }
        afim->mAttrValueMap[oavi->first] = newValue;

        if (i4->second->mFlags & SA_IMM_ATTR_NO_DANGLING)
          hasNoDanglingAttr = true;
      }
    }

    // Skip collecting no dangling references if there is no any attribute with
    // NO_DANGLING flag
    if (hasNoDanglingAttr) {
      collectNoDanglingRefs(afim, afimPreOpNDRefs);
    }
  } else {
    collectNoDanglingRefs(afim, afimPreOpNDRefs);
  }

  for (p = req->attrMods; p; p = p->next) {
    sz = strnlen((char*)p->attrValue.attrName.buf,
                 (size_t)p->attrValue.attrName.size);
    std::string attrName((const char*)p->attrValue.attrName.buf, sz);
    bool modifiedRim = modifiedImmMngt && (attrName == saImmRepositoryInit);

    if (modifiedImmMngt && (attrName == saImmOiTimeout)) {
      /* Currently the IMM does not support this attribute. */
      TRACE_7("ERR_BAD_OPERATION: attr '%s' in IMM object %s is not supported",
              attrName.c_str(), objectName.c_str());
      setCcbErrorString(
          ccb,
          "IMM: ERR_BAD_OPERATION: attr '%s' in IMM object %s is not supported",
          attrName.c_str(), objectName.c_str());
      err = SA_AIS_ERR_BAD_OPERATION;
      break;  // out of for-loop
    }

    if (modifiedRim && !ENABLE_PBE) {
      /* ENABLE_PBE defined in immnd.h */
      LOG_NO(
          "ERR_BAD_OPERATION:  imm has not been built with --enable-imm-pbe");
      setCcbErrorString(
          ccb,
          "IMM: ERR_BAD_OPERATION:  imm has not been built with --enable-imm-pbe");
      err = SA_AIS_ERR_BAD_OPERATION;
      break;
    }

    if (modifiedRim && !pbeFile) {
      LOG_NO("ERR_BAD_OPERATION: PBE file is not configured");
      setCcbErrorString(ccb,
                        "IMM: ERR_BAD_OPERATION: PBE file is not configured");
      err = SA_AIS_ERR_BAD_OPERATION;
      break;
    }

    i4 = classInfo->mAttrMap.find(attrName);
    if (i4 == classInfo->mAttrMap.end()) {
      TRACE_7("ERR_NOT_EXIST: attr '%s' does not exist in object %s",
              attrName.c_str(), objectName.c_str());
      setCcbErrorString(
          ccb, "IMM: ERR_NOT_EXIST: attr '%s' does not exist in object %s",
          attrName.c_str(), objectName.c_str());
      err = SA_AIS_ERR_NOT_EXIST;
      break;  // out of for-loop
    }
    AttrInfo* attr = i4->second;

    if (attr->mValueType != (unsigned int)p->attrValue.attrValueType) {
      LOG_NO("ERR_INVALID_PARAM: attr '%s' type missmatch", attrName.c_str());
      err = SA_AIS_ERR_INVALID_PARAM;
      break;  // out of for-loop
    }

    if (attr->mFlags & SA_IMM_ATTR_RUNTIME) {
      LOG_NO(
          "ERR_INVALID_PARAM: attr '%s' is a runtime attribute => "
          "can not be modified over OM-API.",
          attrName.c_str());
      err = SA_AIS_ERR_INVALID_PARAM;
      break;  // out of for-loop
    }

    if (!(attr->mFlags & SA_IMM_ATTR_WRITABLE)) {
      LOG_NO("ERR_INVALID_PARAM: attr '%s' is not a writable attribute",
             attrName.c_str());
      err = SA_AIS_ERR_INVALID_PARAM;
      break;  // out of for-loop
    }

    if (attr->mFlags & SA_IMM_ATTR_NOTIFY) {
      modifiedNotifyAttr = true;
    }

    if ((attr->mValueType == SA_IMM_ATTR_SANAMET) ||
        (attr->mValueType == SA_IMM_ATTR_SASTRINGT &&
         (attr->mFlags & SA_IMM_ATTR_DN))) {
      if (p->attrValue.attrValue.val.x.size >= SA_MAX_UNEXTENDED_NAME_LENGTH) {
        if (longDnsPermitted) {
          (*hasLongDns) = true;
        } else {
          LOG_NO(
              "ERR_BAD_OPERATION: Attribute '%s' has long DN. "
              "Not allowed by IMM service or extended names are disabled",
              attrName.c_str());
          err = SA_AIS_ERR_BAD_OPERATION;
          break;
        }
        if ((attr->mFlags & SA_IMM_ATTR_MULTI_VALUE) &&
            p->attrValue.attrValuesNumber > 1) {
          IMMSV_EDU_ATTR_VAL_LIST* values = p->attrValue.attrMoreValues;
          while (values) {
            if (values->n.val.x.size >= SA_MAX_UNEXTENDED_NAME_LENGTH) {
              if (longDnsPermitted) {
                (*hasLongDns) = true;
              } else {
                LOG_NO(
                    "ERR_BAD_OPERATION: Attribute '%s' has long DN. "
                    "Not allowed by IMM service or extended names are disabled",
                    attrName.c_str());
                err = SA_AIS_ERR_BAD_OPERATION;
                break;
              }
            }
            values = values->next;
          }
          if (err != SA_AIS_OK) {
            break;
          }
        }
      }

      if (p->attrValue.attrValue.val.x.size > kOsafMaxDnLength) {
        LOG_NO("ERR_LIBRARY: attr '%s' of type SaNameT is too long:%u",
               attrName.c_str(), p->attrValue.attrValue.val.x.size - 1);
        err = SA_AIS_ERR_LIBRARY;
        break;  // out of for-loop
      }

      std::string tmpName(p->attrValue.attrValue.val.x.buf,
                          p->attrValue.attrValue.val.x.size
                              ? p->attrValue.attrValue.val.x.size - 1
                              : 0);
      if (!(nameCheck(tmpName) || nameToInternal(tmpName))) {
        LOG_NO(
            "ERR_INVALID_PARAM: attr '%s' of type SaNameT contains non "
            "printable characters",
            attrName.c_str());
        err = SA_AIS_ERR_INVALID_PARAM;
        break;  // out of for-loop
      }
    }

    if (attr->mValueType == SA_IMM_ATTR_SASTRINGT) {
      /* Check that the string at least conforms to UTF-8 */
      if (p->attrValue.attrValue.val.x.size &&
          !(osaf_is_valid_utf8(p->attrValue.attrValue.val.x.buf))) {
        LOG_NO(
            "ERR_INVALID_PARAM: attr '%s' of type SaStringT has a value "
            "that is not valid UTF-8",
            attrName.c_str());
        err = SA_AIS_ERR_INVALID_PARAM;
        break;  // out of for-loop
      }
    }

    ImmAttrValueMap::iterator i5 = afim->mAttrValueMap.find(attrName);

    if (i5 == afim->mAttrValueMap.end()) {
      LOG_ER("Attr '%s' defined in class yet missing from existing object %s",
             attrName.c_str(), objectName.c_str());
      abort();
    }

    ImmAttrValue* attrValue = i5->second;
    IMMSV_OCTET_STRING tmpos;  // temporary octet string
    ImmAttrMultiValue* multiattr;

    switch (p->attrModType) {
      case SA_IMM_ATTR_VALUES_REPLACE:
        // Save all DNs that will be replaces
        if (attr->mFlags & SA_IMM_ATTR_NO_DANGLING) hasNoDanglingRefs = true;

        // Remove existing values and then fall through to the add case.
        attrValue->discardValues();
        if (p->attrValue.attrValuesNumber == 0) {
          if (attr->mFlags & SA_IMM_ATTR_INITIALIZED) {
            LOG_NO(
                "ERR_INVALID_PARAM: attr '%s' has flag SA_IMM_ATTR_INITIALIZED "
                " cannot modify to zero values",
                attrName.c_str());
            err = SA_AIS_ERR_INVALID_PARAM;
            break;  // out of switch
          }

          if (attr->mFlags & SA_IMM_ATTR_STRONG_DEFAULT) {
            LOG_WA(
                "There's an attempt to set attr '%s' to NULL, "
                "default value will be set",
                attrName.c_str());
            osafassert(!attr->mDefaultValue.empty());
            (*attrValue) = attr->mDefaultValue;

            TRACE("Canonicalizing attr-mod for attribute '%s'",
                  attrName.c_str());
            p->attrValue.attrValuesNumber = 1;
            attrValue->copyValueToEdu(
                &(p->attrValue.attrValue),
                (SaImmValueTypeT)p->attrValue.attrValueType);
          }
          continue;  // Ok to replace with nothing.
        }
      // else intentional fall-through

      case SA_IMM_ATTR_VALUES_ADD:

        if (p->attrValue.attrValuesNumber == 0) {
          LOG_NO(
              "ERR_INVALID_PARAM: Empty value used for adding to attribute %s",
              attrName.c_str());
          err = SA_AIS_ERR_INVALID_PARAM;
          break;
        }

        eduAtValToOs(&tmpos, &(p->attrValue.attrValue),
                     (SaImmValueTypeT)p->attrValue.attrValueType);

        if (attrValue->empty()) {
          attrValue->setValue(tmpos);
          if ((attr->mFlags & SA_IMM_ATTR_NO_DANGLING) &&
              attrValue->getValueC_str()) {
            std::string ndRef = attrValue->getValueC_str();
            err = objectModifyNDTargetOk(objectName.c_str(), attrName.c_str(),
                                         ndRef.c_str(), ccbId);
            if (err != SA_AIS_OK) break;

            hasNoDanglingRefs = true;
          }
        } else {
          if (!(attr->mFlags & SA_IMM_ATTR_MULTI_VALUE)) {
            LOG_NO(
                "ERR_INVALID_PARAM: attr '%s' is not multivalued, yet "
                "multiple values attempted",
                attrName.c_str());
            err = SA_AIS_ERR_INVALID_PARAM;
            break;  // out of switch
          }
          osafassert(attrValue->isMultiValued());
          multiattr = (ImmAttrMultiValue*)attrValue;
          if ((attr->mFlags & SA_IMM_ATTR_NO_DUPLICATES) &&
              (multiattr->hasMatchingValue(tmpos))) {
            LOG_NO(
                "ERR_INVALID_PARAM: multivalued attr '%s' with NO_DUPLICATES "
                "yet duplicate values provided in modify call. Object:'%s'",
                attrName.c_str(), objectName.c_str());
            err = SA_AIS_ERR_INVALID_PARAM;
            break;  // out of for switch
          }

          if ((attr->mFlags & SA_IMM_ATTR_NO_DANGLING) && (tmpos.size > 0)) {
            std::string ndRef(tmpos.buf, strnlen(tmpos.buf, tmpos.size));
            err = objectModifyNDTargetOk(objectName.c_str(), attrName.c_str(),
                                         ndRef.c_str(), ccbId);
            if (err != SA_AIS_OK) break;

            hasNoDanglingRefs = true;
          }

          multiattr->setExtraValue(tmpos);
        }

        if (modifiedRim) {
          SaImmRepositoryInitModeT oldRim = getRepositoryInitMode();
          SaImmRepositoryInitModeT newRim =
              (SaImmRepositoryInitModeT)attrValue->getValue_int();
          if ((newRim != SA_IMM_INIT_FROM_FILE) &&
              (newRim != SA_IMM_KEEP_REPOSITORY)) {
            TRACE_7(
                "ERR_BAD_OPERATION: attr '%s' in IMM object %s can not have value %u",
                attrName.c_str(), objectName.c_str(), newRim);
            setCcbErrorString(
                ccb,
                "IMM: ERR_BAD_OPERATION: attr '%s' in IMM object %s can not have value %u",
                attrName.c_str(), objectName.c_str(), newRim);
            err = SA_AIS_ERR_BAD_OPERATION;
            break;
          }

          if ((oldRim == SA_IMM_KEEP_REPOSITORY) &&
              (newRim == SA_IMM_INIT_FROM_FILE)) {
            LOG_NO("Request for rim change is arrived in ccb%u", ccbId);
            *changeRim = true;
          }
        }

        if (p->attrValue.attrValuesNumber > 1) {
          if (!(attr->mFlags & SA_IMM_ATTR_MULTI_VALUE)) {
            LOG_NO(
                "ERR_INVALID_PARAM: attr '%s' is not multivalued, yet "
                "multiple values provided in modify call",
                attrName.c_str());
            err = SA_AIS_ERR_INVALID_PARAM;
            break;  // out of switch
          } else {
            osafassert(attrValue->isMultiValued());
            multiattr = (ImmAttrMultiValue*)attrValue;

            IMMSV_EDU_ATTR_VAL_LIST* al = p->attrValue.attrMoreValues;
            while (al) {
              eduAtValToOs(&tmpos, &(al->n),
                           (SaImmValueTypeT)p->attrValue.attrValueType);
              if ((attr->mFlags & SA_IMM_ATTR_NO_DUPLICATES) &&
                  (multiattr->hasMatchingValue(tmpos))) {
                LOG_NO(
                    "ERR_INVALID_PARAM: multivalued attr '%s' with "
                    "NO_DUPLICATES yet duplicate values provided in modify call "
                    "Object:'%s'",
                    attrName.c_str(), objectName.c_str());
                err = SA_AIS_ERR_INVALID_PARAM;
                break;  // out of loop
              }

              if ((attr->mFlags & SA_IMM_ATTR_NO_DANGLING) &&
                  (tmpos.size > 0)) {
                std::string ndRef(tmpos.buf, strnlen(tmpos.buf, tmpos.size));
                err = objectModifyNDTargetOk(
                    objectName.c_str(), attrName.c_str(), ndRef.c_str(), ccbId);
                if (err != SA_AIS_OK) break;

                hasNoDanglingRefs = true;
              }

              multiattr->setExtraValue(tmpos);
              al = al->next;
            }
          }
        }
        break;

      case SA_IMM_ATTR_VALUES_DELETE:
        // Delete all values that match that provided by invoker.
        if (p->attrValue.attrValuesNumber == 0) {
          LOG_NO(
              "ERR_INVALID_PARAM: Empty value used for deleting from attribute %s",
              attrName.c_str());
          err = SA_AIS_ERR_INVALID_PARAM;
          break;
        }

        if (modifiedRim) {
          TRACE_7(
              "ERR_BAD_OPERATION: attr '%s' in IMM object %s must not be empty",
              attrName.c_str(), objectName.c_str());
          setCcbErrorString(
              ccb,
              "IMM: ERR_BAD_OPERATION: attr '%s' in IMM object %s must not be empty",
              attrName.c_str(), objectName.c_str());
          err = SA_AIS_ERR_BAD_OPERATION;
          break;
        }

        if (!attrValue->empty()) {
          eduAtValToOs(&tmpos, &(p->attrValue.attrValue),
                       (SaImmValueTypeT)p->attrValue.attrValueType);

          if ((attr->mFlags & SA_IMM_ATTR_NO_DANGLING) && (tmpos.size > 0) &&
              attrValue->hasMatchingValue(tmpos)) {
            hasNoDanglingRefs = true;
          }

          attrValue->removeValue(tmpos);
          // Note: We allow the delete value to be multivalued even
          // if the attribute is single valued. The semantics is that
          // we delete ALL matchings of ANY delete value.
          if (p->attrValue.attrValuesNumber > 1) {
            IMMSV_EDU_ATTR_VAL_LIST* al = p->attrValue.attrMoreValues;
            while (al) {
              eduAtValToOs(&tmpos, &(al->n),
                           (SaImmValueTypeT)p->attrValue.attrValueType);

              if ((attr->mFlags & SA_IMM_ATTR_NO_DANGLING) &&
                  (tmpos.size > 0) && attrValue->hasMatchingValue(tmpos)) {
                hasNoDanglingRefs = true;
              }

              attrValue->removeValue(tmpos);
              al = al->next;
            }
          }

          if (attrValue->empty() && (attr->mFlags & SA_IMM_ATTR_INITIALIZED)) {
            LOG_NO(
                "ERR_INVALID_PARAM: attr '%s' has flag SA_IMM_ATTR_INITIALIZED "
                " cannot modify to zero values",
                attrName.c_str());
            err = SA_AIS_ERR_INVALID_PARAM;
            break;  // out of switch
          }

          if (attrValue->empty() &&
              (attr->mFlags & SA_IMM_ATTR_STRONG_DEFAULT)) {
            LOG_WA(
                "There's an attempt to set attr '%s' to NULL, "
                "default value will be set",
                attrName.c_str());
            osafassert(!attr->mDefaultValue.empty());
            (*attrValue) = attr->mDefaultValue;
          }
        }

        break;  // out of switch

      default:
        err = SA_AIS_ERR_INVALID_PARAM;
        LOG_NO(
            "ERR_INVALID_PARAM: Illegal value for SaImmAttrModificationTypeT: %u",
            p->attrModType);
        break;
    }

    if (err != SA_AIS_OK) {
      break;  // out of for-loop
    }
  }  // for (p = ....)

  // Prepare for call on PersistentBackEnd
  if ((err == SA_AIS_OK) && pbeNodeIdPtr) {
    void* pbe = getPbeOi(pbeConnPtr, pbeNodeIdPtr);
    if (!pbe) {
      if (!ccb->mMutations.empty()) {
        /* ongoing ccb interrupted by PBE down */
        err = SA_AIS_ERR_FAILED_OPERATION;
        ccb->mVeto = err;
        LOG_WA(
            "ERR_FAILED_OPERATION: Persistent back end is down "
            "ccb %u is aborted",
            ccbId);
        setCcbErrorString(ccb, IMM_RESOURCE_ABORT "PBE is down");
      } else {
        /* Pristine ccb can not start because PBE down */
        TRACE_5("ERR_TRY_AGAIN: Persistent back end is down");
        err = SA_AIS_ERR_TRY_AGAIN;
      }
    }
  }

  // Handle NO_DANGLING attributes
  if ((err == SA_AIS_OK) && hasNoDanglingRefs) {
    ObjectNameSet
        origNDRefs;  // Set of NO_DANGLING references from original object
    ObjectNameSet afimPostOpNDRefs;  // Set of NO_DANGLING references from after
                                     // image after CCB operation
    ObjectNameSet s1, s2;
    ObjectNameSet::iterator it;
    ObjectMMap::iterator ommi;
    ObjectMap::iterator omi;

    // Collect all NO_DANGLING references from the original object
    collectNoDanglingRefs(object, origNDRefs);
    // Collect all NO_DANGLING references from afim after applying operations
    collectNoDanglingRefs(afim, afimPostOpNDRefs);

    /* Calculate which NO_DANGLING references can be and will be deleted from
     * sReverseRefsNoDanglingMMap. Only NO_DANGLING references that are created
     * in the CCB are allowed to be deleted from sReverseRefsNoDanglingMMap.
     * First, we need to find all NO_DANGLING references that are created in CCB
     * before modify operation (s1 = afimPreOpNDRefs - origNDRefs). Then, from
     * the set of created NO_DANGLING references, before modify operation, we
     * shall exclude a result of modify operation, and we shall get which
     * NO_DANGLING references need to be removed from sReverseRefsNoDanglingMMap
     * (s2 = s1 - afimPostOpNDRefs).
     */
    std::set_difference(afimPreOpNDRefs.begin(), afimPreOpNDRefs.end(),
                        origNDRefs.begin(), origNDRefs.end(),
                        std::inserter(s1, s1.end()));
    std::set_difference(s1.begin(), s1.end(), afimPostOpNDRefs.begin(),
                        afimPostOpNDRefs.end(), std::inserter(s2, s2.end()));

    // Adjust sReverseRefsNoDanglingMMap
    for (it = s2.begin(); it != s2.end(); ++it) {
      omi = sObjectMap.find(*it);
      if (omi != sObjectMap.end()) {
        ommi = sReverseRefsNoDanglingMMap.find(omi->second);
        while ((ommi != sReverseRefsNoDanglingMMap.end()) &&
               (ommi->first == omi->second)) {
          if (ommi->second == object) {
            sReverseRefsNoDanglingMMap.erase(ommi);
            break;
          }
        }
      }
    }

    s1.clear();
    s2.clear();

    /* Calculate which NO_DANGLING references will be added to
     * sReverseRefsNoDanglingMMap. It's allowed to add a NO_DANGLING reference
     * only if the target object is already created object. First, we need to
     * find all new NO_DANGLING references after modify operation which don't
     * exist in the original object. (s1 = afimPostOpNDRefs - origNDRefs). Then,
     * we shall exclude all already created NO_DANGLING references in CCB before
     * modify operation from s1, and we shall get all new created NO_DANGLING
     * references in modify operation that need to be added to
     * sReverseRefsNoDanglingMMap. (s2 = s1 - afimPreOpNDRefs)
     */
    std::set_difference(afimPostOpNDRefs.begin(), afimPostOpNDRefs.end(),
                        origNDRefs.begin(), origNDRefs.end(),
                        std::inserter(s1, s1.end()));
    std::set_difference(s1.begin(), s1.end(), afimPreOpNDRefs.begin(),
                        afimPreOpNDRefs.end(), std::inserter(s2, s2.end()));

    // Adjust sReverseRefsNoDanglingMMap
    for (it = s2.begin(); it != s2.end(); ++it) {
      omi = sObjectMap.find(*it);
      if (omi != sObjectMap.end()) {
        if (!(omi->second->mObjFlags &
              IMM_CREATE_LOCK)) {  // Add only existing target objects
          sReverseRefsNoDanglingMMap.insert(
              std::pair<ObjectInfo*, ObjectInfo*>(omi->second, object));
        }
      }
    }

    // Set NO_DANGLING flags to the object and the CCB
    object->mObjFlags |= IMM_NO_DANGLING_FLAG;
    ccb->mCcbFlags |= OPENSAF_IMM_CCB_NO_DANGLING_MUTATE;
  }

  // Prepare for call on object implementer
  // and add implementer to ccb.

  if (err == SA_AIS_OK) {
    if (chainedOp) {
      osafassert(oMut);
    } else {
      oMut = new ObjectMutation(IMM_MODIFY);
      oMut->mAfterImage = afim;
    }

    // Prepare for call on object implementer
    // and add implementer to ccb.

    bool hasImpl = object->mImplementer && object->mImplementer->mNodeId;
    bool ignoreImpl =
        (hasImpl && ccb->mAugCcbParent &&
         (ccb->mOriginatingNode == object->mImplementer->mNodeId) &&
         (ccb->mAugCcbParent->mImplId == object->mImplementer->mId));

    if (ignoreImpl) {
      LOG_IN(
          "Skipping OI upcall for modify on %s since OI is same "
          "as the originator of the modify (OI augmented ccb)",
          objectName.c_str());
    } else if (objectName == immObjectDn) {
      ignoreImpl = true;
      /* Ignore implemepter also for the implementer of the OpenSAF IMM service
         object, which is actually the PBE. The PBE is the OI for the OpenSAF
         IMM service object mainly to have the PBE receive and handle
         admin-operations directed at the that object.  But the OpenSAF IMM
         service object also has some config attributes and we then dont want
         the PBE to receive redundant ccb-related callbacks. The PBE gets such
         ccb related callbacks for *all* persistent objects anyway. So we
         discard the "normal" OI callback in this case since the PBE will get
         the callback as PBE.
      */
      LOG_IN("Skipping OI callback for modify on %s since OI is same as PBE",
             objectName.c_str());

      /* Check that the value of accessControlMode is in a range */
      ImmAttrValueMap::iterator avmi =
          afim->mAttrValueMap.find(OPENSAF_IMM_ACCESS_CONTROL_MODE);
      if (avmi != afim->mAttrValueMap.end()) {
        osafassert(avmi->second);
        if (avmi->second->empty()) {
          LOG_WA(
              "ERR_BAD_OPERATION: Value of '%s' attribute in object '%s' cannot be empty",
              OPENSAF_IMM_ACCESS_CONTROL_MODE, objectName.c_str());
          err = SA_AIS_ERR_BAD_OPERATION;
          goto bypass_impl;
        } else {
          SaUint32T mode = (SaUint32T)avmi->second->getValue_int();
          if (mode > 2) { /* ACCESS_CONTROL_ENFORCING == 2 */
            LOG_WA(
                "ERR_BAD_OPERATION: Value of '%s' attribute in object '%s' is out of range",
                OPENSAF_IMM_ACCESS_CONTROL_MODE, objectName.c_str());
            err = SA_AIS_ERR_BAD_OPERATION;
            goto bypass_impl;
          }
        }
      }

      /* From OpenSAF 5.1 immObjectDn has added with attributes maxClasses,
         maxImplementers, maxAdminowners and maxCcbs */

      ImmAttrValueMap::iterator class_avmi =
          afim->mAttrValueMap.find(immMaxClasses);
      ImmAttrValueMap::iterator impl_avmi = afim->mAttrValueMap.find(immMaxImp);
      ImmAttrValueMap::iterator admi_avmi =
          afim->mAttrValueMap.find(immMaxAdmOwn);
      ImmAttrValueMap::iterator ccb_avmi = afim->mAttrValueMap.find(immMaxCcbs);

      if (!protocol51Allowed() && class_avmi != afim->mAttrValueMap.end()) {
        LOG_WA(
            "ERR_NOT_EXIST: The attribute %s can not be modified. The cluster is not yet upgraded"
            "to OpenSAF 5.1",
            immMaxClasses.c_str());
        err = SA_AIS_ERR_NOT_EXIST;
        goto bypass_impl;
      } else if (class_avmi != afim->mAttrValueMap.end()) {
        osafassert(class_avmi->second);
        SaUint32T val = (SaUint32T)class_avmi->second->getValue_int();
        if (val < IMMSV_MAX_CLASSES) {
          LOG_WA(
              "ERR_BAD_OPERATION: The value %d is less than the minimum supported value %d for"
              " the attribute %s",
              val, IMMSV_MAX_CLASSES, immMaxClasses.c_str());
          err = SA_AIS_ERR_BAD_OPERATION;
          goto bypass_impl;
        }
      }

      if (!protocol51Allowed() && impl_avmi != afim->mAttrValueMap.end()) {
        LOG_WA(
            "ERR_NOT_EXIST: The attribute %s can not be modified. The cluster is not yet upgraded"
            " to OpenSAF 5.1",
            immMaxImp.c_str());
        err = SA_AIS_ERR_NOT_EXIST;
        goto bypass_impl;

      } else if (class_avmi != afim->mAttrValueMap.end()) {
        osafassert(impl_avmi->second);
        SaUint32T val = (SaUint32T)impl_avmi->second->getValue_int();
        if (val < IMMSV_MAX_IMPLEMENTERS) {
          LOG_WA(
              "ERR_BAD_OPERATION: The value %d is less than the minimum supported value %d for"
              " the attribute %s",
              val, IMMSV_MAX_IMPLEMENTERS, immMaxImp.c_str());
          err = SA_AIS_ERR_BAD_OPERATION;
          goto bypass_impl;
        }
      }

      if (!protocol51Allowed() && admi_avmi != afim->mAttrValueMap.end()) {
        LOG_WA(
            "ERR_NOT_EXIST: The attribute %s can not be modified. The cluster is not yet upgraded"
            " to OpenSAF 5.1",
            immMaxAdmOwn.c_str());
        err = SA_AIS_ERR_NOT_EXIST;
        goto bypass_impl;

      } else if (admi_avmi != afim->mAttrValueMap.end()) {
        osafassert(admi_avmi->second);
        SaUint32T val = (SaUint32T)admi_avmi->second->getValue_int();
        if (val < IMMSV_MAX_ADMINOWNERS) {
          LOG_WA(
              "ERR_BAD_OPERATION: The value %d is less than the minimum supported value %d for"
              " the attribute %s",
              val, IMMSV_MAX_ADMINOWNERS, immMaxAdmOwn.c_str());
          err = SA_AIS_ERR_BAD_OPERATION;
          goto bypass_impl;
        }
      }

      if (!protocol51Allowed() && ccb_avmi != afim->mAttrValueMap.end()) {
        LOG_WA(
            "ERR_NOT_EXIST: The attribute %s can not be modified. The cluster is not yet upgraded"
            " to OpenSAF 5.1",
            immMaxCcbs.c_str());
        err = SA_AIS_ERR_NOT_EXIST;
        goto bypass_impl;

      } else if (ccb_avmi != afim->mAttrValueMap.end()) {
        osafassert(ccb_avmi->second);
        SaUint32T val = (SaUint32T)ccb_avmi->second->getValue_int();
        if (val < IMMSV_MAX_CCBS) {
          LOG_WA(
              "ERR_BAD_OPERATION: The value %d is less than the minimum supported value %d for"
              " the attribute %s",
              val, IMMSV_MAX_CCBS, immMaxCcbs.c_str());
          err = SA_AIS_ERR_BAD_OPERATION;
          goto bypass_impl;
        }
      }

      /* Pre validate any changes. More efficent here than in apply/completed
         and we need to guard against race on long DN creation allowed. Such
         long DNs are themselves created in CCBs or RTO creates. For
         longDnsAllowed we must check for interference and if setting to 0, i.e.
         not allowed, then verify (a) that no long DNs exist; and (b) that no
         regular RDN attribute value is longer than 64 bytes.

         For opensafImmSyncBatchSize we accept anything.
      */

      bool longDnsAllowedAfter = getLongDnsAllowed(afim);

      /* Check if *this* ccb is attempting to alter longDnsAllowed.*/
      if (longDnsPermitted != longDnsAllowedAfter) {
        if (sCcbIdLongDnGuard) {
          /* This case should never happen since it is guarded by regular ccb
           * handling. */
          setCcbErrorString(ccb,
                            "IMM: ERR_BUSY: Other Ccb (%u) already using %s",
                            sCcbIdLongDnGuard, immLongDnsAllowed.c_str());
          LOG_IN("IMM: ERR_BUSY: Other Ccb (%u) already using %s",
                 sCcbIdLongDnGuard, immLongDnsAllowed.c_str());
          err = SA_AIS_ERR_BUSY;
        } else {
          if (!longDnsAllowedAfter) {
            TRACE(
                "longDnsAllowed assigned 0 => Check that no long DNs exist in IMM-db");
            ObjectMap::iterator omi = sObjectMap.begin();
            while (omi != sObjectMap.end()) {
              if (omi->first.length() >= SA_MAX_UNEXTENDED_NAME_LENGTH) {
                LOG_WA(
                    "Setting attr %s to 0 in %s not allowed when long DN exists: '%s'",
                    immLongDnsAllowed.c_str(), immObjectDn.c_str(),
                    omi->first.c_str());
                err = SA_AIS_ERR_BAD_OPERATION;
                goto bypass_impl;
              }
              /* Check any attributes of type SaNameT that could be dangling,
                 i.e. does NOT have the SA_IMM_ATTR_NO_DANGLING flag set. RDN
                 atribute does not need check for long DN because it is covered
                 by the DN check above, but RDN attribute does need check on RDN
                 lenght less than 65. Implementation below is not the most
                 optimal as it iterates over the attribute definitions of the
                 class for each object. But THIS CASE, of turning OFF
                 longDnsAllowed, must be extreemely rare. The implication is
                 that the turning ON of longDnsAllowed was a mistake.
              */
              for (i4 = omi->second->mClassInfo->mAttrMap.begin();
                   i4 != omi->second->mClassInfo->mAttrMap.end(); ++i4) {
                if (i4->second->mFlags & SA_IMM_ATTR_RDN) {
                  /* 64 byte limit check for RDN attribute ....*/
                  if (i4->second->mValueType != SA_IMM_ATTR_SANAMET) {
                    /* ...that is not an association object.*/
                    oavi = omi->second->mAttrValueMap.find(i4->first);
                    osafassert(oavi != omi->second->mAttrValueMap.end());
                    ImmAttrValue* av = oavi->second;
                    const char* rdn = av->getValueC_str();
                    if (rdn && strlen(rdn) > 64) {
                      LOG_WA(
                          "Setting attr %s to 0 in %s not allowed when long RDN exists "
                          "inside object: %s",
                          immLongDnsAllowed.c_str(), immObjectDn.c_str(),
                          omi->first.c_str());
                      err = SA_AIS_ERR_BAD_OPERATION;
                      goto bypass_impl;
                    }
                  }
                } else if ((i4->second->mValueType == SA_IMM_ATTR_SANAMET ||
                            (i4->second->mValueType == SA_IMM_ATTR_SASTRINGT &&
                             (i4->second->mFlags & SA_IMM_ATTR_DN))) &&
                           !(i4->second->mFlags & SA_IMM_ATTR_NO_DANGLING)) {
                  /* DN limit check for attribute that is:
                     not the RDN attribute (else branch here)
                     not of type SaNameT
                     not flagged as NO_DANGLING (i.e. could be dangling).
                  */
                  oavi = omi->second->mAttrValueMap.find(i4->first);
                  osafassert(oavi != omi->second->mAttrValueMap.end());
                  ImmAttrValue* av = oavi->second;
                  do {
                    const char* dn = av->getValueC_str();
                    if (dn && (strlen(dn) >= SA_MAX_UNEXTENDED_NAME_LENGTH)) {
                      LOG_WA(
                          "Setting attr %s to 0 in %s not allowed when long DN exists "
                          "inside object: %s",
                          immLongDnsAllowed.c_str(), immObjectDn.c_str(),
                          omi->first.c_str());
                      err = SA_AIS_ERR_BAD_OPERATION;
                      goto bypass_impl;
                    }
                    av = (av->isMultiValued())
                             ? ((ImmAttrMultiValue*)av)->getNextAttrValue()
                             : NULL;
                  } while (av);
                }
              }
              ++omi;
            }
          } /* End of LONG DN check */

          sCcbIdLongDnGuard = ccbId;
        }
      }
    }

    if (ignoreImpl) {
      goto bypass_impl;
    } else if (hasImpl && ccb->mAugCcbParent) {
      TRACE("impl found & not ignored Node %u %u ImpllId: %u %u",
            ccb->mOriginatingNode, object->mImplementer->mNodeId,
            ccb->mAugCcbParent->mImplId, object->mImplementer->mId);
    }

    if (hasImpl) {
      ccb->mState = IMM_CCB_MODIFY_OP;
      *implConn = object->mImplementer->mConn;
      *implNodeId = object->mImplementer->mNodeId;

      CcbImplementerMap::iterator ccbi =
          ccb->mImplementers.find(object->mImplementer->mId);
      if (ccbi == ccb->mImplementers.end()) {
        ImplementerCcbAssociation* impla =
            new ImplementerCcbAssociation(object->mImplementer);
        ccb->mImplementers[object->mImplementer->mId] = impla;
      }
      // Create an implementer continuation to ensure wait
      // can be aborted.
      oMut->mWaitForImplAck = true;

      // Increment even if we dont invoke locally
      if (++sLastContinuationId >= 0xfffffffe) {
        sLastContinuationId = 1;
      }
      oMut->mContinuationId = sLastContinuationId;

      if (*implConn) {
        if (object->mImplementer->mDying) {
          LOG_WA(
              "Lost connection with implementer %s in "
              "CcbObjectModify.",
              object->mImplementer->mImplementerName.c_str());
          *continuationId = 0;
          *implConn = 0;
          err = SA_AIS_ERR_FAILED_OPERATION;
          // Let the timeout handling take care of it on other nodes.
          // This really needs to be tested! But how ?
          setCcbErrorString(
              ccb, IMM_RESOURCE_ABORT "Lost connection with implementer");
        } else {
          *continuationId = sLastContinuationId;
        }
      }

      TRACE_5("THERE IS AN IMPLEMENTER %u conn:%u node:%x name:%s\n",
              object->mImplementer->mId, *implConn, *implNodeId,
              object->mImplementer->mImplementerName.c_str());

      osaf_clock_gettime(CLOCK_MONOTONIC, &ccb->mWaitStartTime);
    } else if (ccb->mCcbFlags & SA_IMM_CCB_REGISTERED_OI) {
      if ((object->mImplementer == NULL) &&
          (ccb->mCcbFlags & SA_IMM_CCB_ALLOW_NULL_OI)) {
        TRACE_7(
            "Null implementer, SA_IMM_CCB_REGISTERED_OI set, "
            "but SA_IMM_CCB_ALLOW_NULL_OI set => safe relaxation");
      } else {
        TRACE_7(
            "ERR_NOT_EXIST: object '%s' does not have an "
            "implementer and flag SA_IMM_CCB_REGISTERED_OI is set",
            objectName.c_str());
        setCcbErrorString(ccb,
                          "IMM: ERR_NOT_EXIST: object '%s' exist but "
                          "no implementer (which is required)",
                          objectName.c_str());
        err = SA_AIS_ERR_NOT_EXIST;
      }
    } else { /* SA_IMM_CCB_REGISTERED_OI NOT set */
      if (object->mImplementer) {
        LOG_WA(
            "Object '%s' DOES have an implementer %s, currently "
            "detached, but flag SA_IMM_CCB_REGISTERED_OI is NOT set - UNSAFE!",
            objectName.c_str(), object->mImplementer->mImplementerName.c_str());
      } else {
        TRACE_7(
            "Object '%s' has NULL implementer, flag SA_IMM_CCB_REGISTERED_OI "
            "is NOT set - moderately safe.",
            objectName.c_str());
      }
    }
  }

bypass_impl:

  if (err == SA_AIS_OK) {
    object->mCcbId = ccbId;  // Overwrite any old obsolete ccb id.
    osafassert(oMut);
    if (!chainedOp) {
      ccb->mMutations[objectName] = oMut;
      if (adminOwner->mReleaseOnFinalize) {
        // NOTE: this may not be good enough. For a modify the
        // object should already be "owned" by the admin.
        adminOwner->mTouchedObjects.insert(object);
      }
    }
    ccb->mOpCount++;
    if (upgradeReadLock) {
      ccb->removeObjReadLock(object);
    }
  } else {
    // err != SA_AIS_OK
    if (ccb->mState == IMM_CCB_MODIFY_OP) {
      ccb->mState = IMM_CCB_READY;
    }
    if (chainedOp) {
      err = SA_AIS_ERR_FAILED_OPERATION;
      ccb->mVeto = err;  // Corrupted chain => corrupted ccb
      setCcbErrorString(ccb, IMM_RESOURCE_ABORT "Corrupted CCB");
    } else {  // First op on this object => ccb can survive
      if (afim) {
        // Delete the cloned afim.
        for (oavi = afim->mAttrValueMap.begin();
             oavi != afim->mAttrValueMap.end(); ++oavi) {
          delete oavi->second;
        }
        afim->mAttrValueMap.clear();
        delete afim;
      }
      if (oMut) {
        oMut->mAfterImage = NULL;
        delete oMut;
      }
    }
  }

ccbObjectModifyExit:

  modifiedNotifyAttr = modifiedNotifyAttr && getSpecialApplier();

  if ((err != SA_AIS_OK) || !classInfo ||
      (classInfo->mAppliers.empty() && !modifiedNotifyAttr &&
       sObjAppliersMap.find(objectName) == sObjAppliersMap.end())) {
    objectName.clear();
  }
  TRACE_LEAVE();
  return err;
}

/**
 * Deletes an object
 */
SaAisErrorT ImmModel::ccbObjectDelete(
    const ImmsvOmCcbObjectDelete* req, SaUint32T reqConn,
    ObjectNameVector& objNameVector, ConnVector& connVector,
    IdVector& continuations, SaUint32T* pbeConnPtr, unsigned int* pbeNodeIdPtr,
    bool* augDelete, bool* hasLongDn)

{
  TRACE_ENTER();
  osafassert(augDelete);
  *augDelete = false;
  osafassert(hasLongDn);
  *hasLongDn = false;
  SaAisErrorT err = SA_AIS_OK;

  // osafassert(!immNotWritable());
  // It should be safe to allow old ccbs to continue to mutate the IMM.
  // The sync does not realy start until all ccb's are completed.

  size_t sz = strnlen((char*)req->objectName.buf, (size_t)req->objectName.size);
  std::string objectName((const char*)req->objectName.buf, sz);

  SaUint32T ccbId = req->ccbId;
  SaUint32T adminOwnerId = req->adminOwnerId;

  CcbInfo* ccb = 0;
  AdminOwnerInfo* adminOwner = 0;
  CcbVector::iterator i1;
  AdminOwnerVector::iterator i2;
  ObjectMap::iterator oi, oi2;
  ObjectInfo* deleteRoot = NULL;

  ObjectSet safeReadObjSet;
  ObjectInfo* readLockedObject;

  if (sz >= SA_MAX_UNEXTENDED_NAME_LENGTH) {
    if (getLongDnsAllowed()) {
      *hasLongDn = true;
    } else {
      LOG_NO(
          "ERR_NAME_TOO_LONG: Object name is too long. "
          "Not allowed by IMM service or extended names are disabled");
      err = SA_AIS_ERR_NAME_TOO_LONG;
      goto ccbObjectDeleteExit;
    }
  }

  if (!(nameCheck(objectName) || nameToInternal(objectName))) {
    LOG_NO("ERR_INVALID_PARAM: Not a proper object name");
    err = SA_AIS_ERR_INVALID_PARAM;
    goto ccbObjectDeleteExit;
  }

  i1 = std::find_if(sCcbVector.begin(), sCcbVector.end(), CcbIdIs(ccbId));
  if (i1 == sCcbVector.end()) {
    LOG_NO("ERR_BAD_HANDLE: ccb id %u does not exist", ccbId);
    err = SA_AIS_ERR_BAD_HANDLE;
    goto ccbObjectDeleteExit;
  }
  ccb = *i1;

  /* If this is a delete inside an augmentation, ensure we wait only
     for the delete continuations belonging to this augmentation and
     not on delete continuations belonging to the original CCB. */
  *augDelete = ccb->mAugCcbParent != NULL;

  if (!ccb->isOk()) {
    LOG_NO(
        "ERR_FAILED_OPERATION: ccb %u is in an error state "
        "rejecting ccbObjectDelete operation ",
        ccbId);
    err = SA_AIS_ERR_FAILED_OPERATION;
    setCcbErrorString(ccb, IMM_RESOURCE_ABORT "CCB is in an error state");
    goto ccbObjectDeleteExit;
  }

  if (ccb->mState > IMM_CCB_READY) {
    LOG_WA(
        "ERR_FAILED_OPERATION: ccb %u is not in an expected state:%u "
        "rejecting ccbObjectDelete operation ",
        ccbId, ccb->mState);
    err = SA_AIS_ERR_FAILED_OPERATION;
    setCcbErrorString(ccb,
                      IMM_RESOURCE_ABORT "CCB is not in an expected state");
    goto ccbObjectDeleteExit;
  }

  if (reqConn && (ccb->mOriginatingConn != reqConn)) {
    LOG_NO("ERR_BAD_HANDLE: Missmatch on connection for ccb id %u", ccbId);
    err = SA_AIS_ERR_BAD_HANDLE;
    // Abort the CCB ??
    goto ccbObjectDeleteExit;
  }

  i2 = std::find_if(sOwnerVector.begin(), sOwnerVector.end(),
                    IdIs(adminOwnerId));
  if (i2 == sOwnerVector.end()) {
    LOG_NO("ERR_BAD_HANDLE: admin owner id %u does not exist", adminOwnerId);
    err = SA_AIS_ERR_BAD_HANDLE;
    goto ccbObjectDeleteExit;
  }
  adminOwner = *i2;
  osafassert(!adminOwner->mDying);

  if (adminOwner->mId != ccb->mAdminOwnerId) {
    LOG_WA("ERR_FAILED_OPERATION: Inconsistency between Ccb and AdminOwner");
    err = SA_AIS_ERR_FAILED_OPERATION;
    setCcbErrorString(
        ccb, IMM_RESOURCE_ABORT "Inconsistency between CCB and AdminOwner");
    goto ccbObjectDeleteExit;
  }

  if (objectName.empty()) {
    LOG_NO("ERR_INVALID_PARAM: Empty DN attribute value");
    err = SA_AIS_ERR_INVALID_PARAM;
    goto ccbObjectDeleteExit;
  }

  oi = sObjectMap.find(objectName);
  if (oi == sObjectMap.end()) {
    TRACE_7("ERR_NOT_EXIST: object '%s' does not exist", objectName.c_str());
    err = SA_AIS_ERR_NOT_EXIST;
    goto ccbObjectDeleteExit;
  }

  if (oi->second->mClassInfo->mCategory != SA_IMM_CLASS_CONFIG) {
    LOG_NO("ERR_BAD_OPERATION: object '%s' is not a configuration object",
           objectName.c_str());
    return SA_AIS_ERR_BAD_OPERATION;
  }

  // Prepare for call on PersistentBackEnd

  if ((err == SA_AIS_OK) && pbeNodeIdPtr) {
    void* pbe = getPbeOi(pbeConnPtr, pbeNodeIdPtr);
    if (!pbe) {
      if (!ccb->mMutations.empty()) {
        /* ongoing ccb interrupted by PBE down */
        err = SA_AIS_ERR_FAILED_OPERATION;
        ccb->mVeto = err;
        LOG_WA(
            "ERR_FAILED_OPERATION: Persistent back end is down "
            "ccb %u is aborted",
            ccbId);
        setCcbErrorString(ccb, IMM_RESOURCE_ABORT "PBE is down");
      } else {
        /* Pristine ccb can not start because PBE down */
        TRACE_5("ERR_TRY_AGAIN: Persistent back end is down");
        err = SA_AIS_ERR_TRY_AGAIN;
      }
    } else {
      TRACE_7("PbeConn: %u PbeNode:%u assigned", *pbeConnPtr, *pbeNodeIdPtr);
    }
  }

  if ((oi->second->mObjFlags & IMM_DELETE_LOCK) &&
      !(oi->second->mObjFlags & IMM_DELETE_ROOT)) {
    TRACE_7(
        "Object '%s' already scheduled for delete, not setting DELETE_ROOT flag",
        objectName.c_str());
    /* deleteRoot will be NULL if the object already scheduled for delete and it
       is not already the delete root. If it is already a delete-root, then this
       is a redundant delete of the same subtree. The delete-root will then be
       turned OFF inside deleteObject, only to be turned ON again below.
    */

  } else {
    deleteRoot = oi->second;
  }

  for (int doIt = 0; (doIt < 2) && (err == SA_AIS_OK); ++doIt) {
    SaUint32T childCount = oi->second->mChildCount;

    err = deleteObject(oi, reqConn, adminOwner, ccb, doIt, objNameVector,
                       connVector, continuations,
                       pbeConnPtr ? (*pbeConnPtr) : 0, &readLockedObject,
                       true);

    if (err == SA_AIS_OK && readLockedObject != NULL) {
      safeReadObjSet.insert(readLockedObject);
    }

    if (doIt && deleteRoot) {
      osafassert(err == SA_AIS_OK);
      deleteRoot->mObjFlags |= IMM_DELETE_ROOT;
    }

    // Find all sub objects to the deleted object and delete them
    for (oi2 = sObjectMap.begin();
         oi2 != sObjectMap.end() && err == SA_AIS_OK && childCount; ++oi2) {
      std::string subObjName = oi2->first;
      if (subObjName.length() > objectName.length()) {
        size_t pos = subObjName.length() - objectName.length();
        if ((subObjName.rfind(objectName, pos) == pos) &&
            (subObjName[pos - 1] == ',')) {
          --childCount;
          err = deleteObject(oi2, reqConn, adminOwner, ccb, doIt, objNameVector,
                             connVector, continuations,
                             pbeConnPtr ? (*pbeConnPtr) : 0, &readLockedObject,
                             false);
          if (err == SA_AIS_OK && readLockedObject != NULL) {
            safeReadObjSet.insert(readLockedObject);
          }
          if (!childCount) {
            TRACE("Cutoff in ccb-obj-delete-loop by childCount");
          }
        }
      }
    }
  }
ccbObjectDeleteExit:

  if (err == SA_AIS_OK) {
    // Successfully deleted all objects. Remove read lock from affected objects
    ObjectSet::iterator osi;
    for (osi = safeReadObjSet.begin(); osi != safeReadObjSet.end(); ++osi) {
      ccb->removeObjReadLock(*osi);
    }
  }

  TRACE_LEAVE();
  return err;
}

SaAisErrorT ImmModel::deleteObject(ObjectMap::iterator& oi, SaUint32T reqConn,
                                   AdminOwnerInfo* adminOwner, CcbInfo* ccb,
                                   bool doIt, ObjectNameVector& objNameVector,
                                   ConnVector& connVector,
                                   IdVector& continuations,
                                   unsigned int pbeIsLocal,
                                   ObjectInfo** readLockedObject,
                                   bool sendToPbe) {
  /*TRACE_ENTER();*/
  bool configObj = true;
  std::string objAdminOwnerName;
  SaUint32T ccbIdOfObj = 0;
  CcbVector::iterator i1;
  bool nonPersistentRto =
      false;                /* Dont increment opcount for nonPersistent RTOs */
  *readLockedObject = NULL; /* readLockedObject cannot be NULL */

  oi->second->getAdminOwnerName(&objAdminOwnerName);
  if (objAdminOwnerName != adminOwner->mAdminOwnerName) {
    LOG_NO("ERR_BAD_OPERATION: Mismatch on administrative owner %s != %s",
           objAdminOwnerName.c_str(), adminOwner->mAdminOwnerName.c_str());
    return SA_AIS_ERR_BAD_OPERATION;
  }

  ccbIdOfObj = oi->second->mCcbId;
  if (ccbIdOfObj != ccb->mId) {
    i1 =
        std::find_if(sCcbVector.begin(), sCcbVector.end(), CcbIdIs(ccbIdOfObj));
    if ((i1 != sCcbVector.end()) && ((*i1)->isActive())) {
      LOG_NO("ERR_BUSY: ccb id %u diff from active ccb id on object %u",
             ccb->mId, ccbIdOfObj);
      return SA_AIS_ERR_BUSY;
    }

    if (ccbIdOfObj == 0) {
      ObjectShortCountMap::iterator oscm =
          sObjectReadLockCount.find(oi->second);
      if (oscm != sObjectReadLockCount.end()) {
        if ((oscm->second > 1) || !(ccb->isReadLockedByThisCcb(oi->second))) {
          TRACE("ERR_BUSY: object '%s' is read-locked by %u other ccb(s)",
                oi->first.c_str(), oscm->second);
          osafassert(!doIt);
          return SA_AIS_ERR_BUSY;
        } else if (doIt) {
          osafassert((oscm->second == 1) &&
                     (ccb->isReadLockedByThisCcb(oi->second)));
          TRACE(
              "CcbDelete: Object '%s' safe-read-locked only by this ccb (%u) => "
              "upgrade to exclusive lock is possible",
              oi->first.c_str(), ccb->mId);
          *readLockedObject = oi->second;
        }
      }
    }
  }

  if (oi->second->mClassInfo->mCategory != SA_IMM_CLASS_CONFIG) {
    if (doIt) {
      LOG_IN(
          "Runtime object '%s' will be deleted as a side effect, "
          "if ccb %u is applied",
          oi->first.c_str(), ccb->mId);

      if (!(oi->second->mObjFlags & IMM_PRTO_FLAG)) {
        AttrMap::iterator i4 =
            std::find_if(oi->second->mClassInfo->mAttrMap.begin(),
                         oi->second->mClassInfo->mAttrMap.end(),
                         AttrFlagIncludes(SA_IMM_ATTR_PERSISTENT));
        if (i4 != oi->second->mClassInfo->mAttrMap.end()) {
          TRACE_7("Setting PRTO flag for %s", oi->first.c_str());
          oi->second->mObjFlags |= IMM_PRTO_FLAG;
        } else {
          /* Dont increment opcount for nonPersistent RTOs
             The opcount is only used by the PBE to verify
             that it has not missed receiving any operation.
             Non-persistent RTO deletes dont go to the PBE
             so the opcount should not be incremented.
           */
          nonPersistentRto = true;
        }
      }
    }
    configObj = false;

    /* Check for possible interference with RTO create or delete */
    if (!doIt && !ccbIdOfObj) {
      if (oi->second->mObjFlags & IMM_CREATE_LOCK) {
        TRACE_7(
            "ERR_TRY_AGAIN: sub-object '%s' registered for creation "
            "but not yet applied by PRTO PBE ?",
            oi->first.c_str());
        return SA_AIS_ERR_TRY_AGAIN;
      } else if (oi->second->mObjFlags & IMM_DELETE_LOCK) {
        TRACE_7(
            "ERR_TRY_AGAIN: sub-object '%s' already registered for delete "
            "but not yet applied by PRTO PBE ?",
            oi->first.c_str());
        return SA_AIS_ERR_TRY_AGAIN;
      }

      // Check NO_DANGLING references for PRTO
      if (std::find_if(oi->second->mClassInfo->mAttrMap.begin(),
                       oi->second->mClassInfo->mAttrMap.end(),
                       AttrFlagIncludes(SA_IMM_ATTR_PERSISTENT)) !=
          oi->second->mClassInfo->mAttrMap.end()) {
        ObjectMMap::iterator ommi = sReverseRefsNoDanglingMMap.find(oi->second);
        if (ommi != sReverseRefsNoDanglingMMap.end()) {
          while (ommi != sReverseRefsNoDanglingMMap.end() &&
                 ommi->first == oi->second) {
            if ((ommi->second->mObjFlags & IMM_DELETE_LOCK) &&
                (ommi->second->mCcbId == ccb->mId)) {
              ++ommi;
              continue;
            } else {
              std::string objName;
              getObjectName(ommi->first, objName);
              LOG_NO(
                  "ERR_BAD_OPERATION: PRTO %s can't be deleted by Ccb(%u) "
                  "because another object %s has a NO_DANGLING reference to it",
                  oi->first.c_str(), ccb->mId, objName.c_str());
              setCcbErrorString(ccb, "IMM: ERR_BAD_OPERATION: "
                                "PRTO %s is referred by object %s "
                                "not scheduled for deletion by this CCB",
                                oi->first.c_str(), objName.c_str());
              return SA_AIS_ERR_BAD_OPERATION;
            }
          }

          oi->second->mObjFlags |= IMM_NO_DANGLING_FLAG;
          ccb->mCcbFlags |= OPENSAF_IMM_CCB_NO_DANGLING_MUTATE;
        }
      }
    }
  } else {  // Config object
    if (!doIt) {
      /* Check interference with NO_DANGLING references to the deleting object
       */
      ObjectMMap::iterator ommi = sReverseRefsNoDanglingMMap.find(oi->second);
      while (ommi != sReverseRefsNoDanglingMMap.end() &&
             ommi->first == oi->second) {
        if (ommi->second->mObjFlags & IMM_DELETE_LOCK) {
          if (ommi->second->mCcbId != ccb->mId) {
            std::string objName;
            getObjectName(ommi->first, objName);
            LOG_IN(
                "ERR_BUSY: Object %s with NO_DANGLING reference to object %s "
                "is scheduled to be deleted in another Ccb(%u), this Ccb(%u)",
                objName.c_str(), oi->first.c_str(), ommi->second->mCcbId,
                ccb->mId);
            setCcbErrorString(ccb, "IMM: ERR_BUSY: "
                              "Object %s refers to object %s "
                              "and is flagged for deletion in another CCB",
                              objName.c_str(), oi->first.c_str());
            return SA_AIS_ERR_BUSY;
          }
        } else if (ommi->second->mObjFlags & IMM_CREATE_LOCK) {
          std::string objName;
          getObjectName(ommi->first, objName);
          if (ommi->second->mCcbId == ccb->mId) {
            LOG_NO(
                "ERR_BAD_OPERATION: Created object %s with NO_DANGLING reference to object %s "
                "flagged for deletion in the same Ccb(%u) is not allowed",
                objName.c_str(), oi->first.c_str(), ccb->mId);
            setCcbErrorString(ccb, "IMM: ERR_BAD_OPERATION: "
                              "Object %s refers to object %s "
                              "and is flagged for creation in this CCB",
                              objName.c_str(), oi->first.c_str());
            return SA_AIS_ERR_BAD_OPERATION;
          } else {
            LOG_IN(
                "ERR_BUSY: Created object %s with NO_DANGLING reference to object %s "
                "flagged for deletion is not created in the same Ccb(%u), this Ccb(%u)",
                objName.c_str(), oi->first.c_str(), ommi->second->mCcbId,
                ccb->mId);
            setCcbErrorString(ccb, "IMM: ERR_BUSY: "
                              "Object %s refers to object %s "
                              "and is flagged for creation in another CCB",
                              objName.c_str(), oi->first.c_str());
            return SA_AIS_ERR_BUSY;
          }
        }

        oi->second->mObjFlags |= IMM_NO_DANGLING_FLAG;
        ccb->mCcbFlags |= OPENSAF_IMM_CCB_NO_DANGLING_MUTATE;

        ++ommi;
      }

      if (!(oi->second->mObjFlags & IMM_NO_DANGLING_FLAG)) {
        AttrMap::iterator ami = oi->second->mClassInfo->mAttrMap.begin();
        ImmAttrValueMap::iterator iavmi;
        for (; ami != oi->second->mClassInfo->mAttrMap.end(); ++ami) {
          if (ami->second->mFlags & SA_IMM_ATTR_NO_DANGLING) {
            iavmi = oi->second->mAttrValueMap.find(ami->first);
            osafassert(iavmi != oi->second->mAttrValueMap.end());
            if (iavmi->second != NULL) {
              oi->second->mObjFlags |= IMM_NO_DANGLING_FLAG;
              ccb->mCcbFlags |= OPENSAF_IMM_CCB_NO_DANGLING_MUTATE;
              break;
            }
          }
        }
      }
    }
  }

  if (!doIt &&
      !(oi->second->mImplementer && oi->second->mImplementer->mNodeId) &&
      configObj) {
    /* Implementer is not present. */
    /* Prevent delete of imm service objects, even when there is no OI for them
     */
    if (oi->first == immManagementDn || oi->first == immObjectDn) {
      setCcbErrorString(
          ccb, "IMM: ERR_BAD_OPERATION: Imm not allowing delete of object '%s'",
          oi->first.c_str());
      return SA_AIS_ERR_BAD_OPERATION;
    }

    if (ccb->mCcbFlags & SA_IMM_CCB_REGISTERED_OI) {
      if ((oi->second->mImplementer == NULL) &&
          (ccb->mCcbFlags & SA_IMM_CCB_ALLOW_NULL_OI)) {
        TRACE_7(
            "Null implementer, SA_IMM_CCB_REGISTERED_OI set, "
            "but SA_IMM_CCB_ALLOW_NULL_OI set => safe relaxation");
      } else {
        TRACE_7(
            "ERR_NOT_EXIST: object '%s' does not have an implementer "
            "and flag SA_IMM_CCB_REGISTERED_OI is set",
            oi->first.c_str());
        setCcbErrorString(ccb,
                          "IMM: ERR_NOT_EXIST: object '%s' exist but "
                          "no implementer (which is required)",
                          oi->first.c_str());
        return SA_AIS_ERR_NOT_EXIST;
      }
    } else { /* SA_IMM_CCB_REGISTERED_OI NOT set */
      if (oi->second->mImplementer) {
        LOG_WA(
            "Object '%s' DOES have an implementer %s, currently detached, "
            "but flag SA_IMM_CCB_REGISTERED_OI is NOT set - UNSAFE!",
            oi->first.c_str(),
            oi->second->mImplementer->mImplementerName.c_str());
      } else {
        TRACE_7(
            "Object '%s' has NULL implementer, flag SA_IMM_CCB_REGISTERED_OI "
            "is NOT set - moderately safe.",
            oi->first.c_str());
      }
    }
  }

  ObjectMutationMap::iterator omuti = ccb->mMutations.find(oi->first);
  if (omuti != ccb->mMutations.end()) {
    /*
      TODO: fix to allow delete on top of modify. Possibly also delete on
      create. Delete on create is dangerous, must check if there are
      children creates. But allow delete on top of delete.
    */
    if (omuti->second->mOpType == IMM_DELETE) {
      /* Delete already registered on object. */
      if (doIt) {
        if (oi->second->mObjFlags & IMM_DELETE_ROOT) {
          TRACE_5(
              "DELETE_ROOT flag found ON for object '%s', "
              "clearing it due to new delete-root",
              oi->first.c_str());
          /* This is the case when this subtree has already been deleted
             in the same CCB by a previous delete-op. Either the same
             subtree or a larger tree including this subtree. In either
             case we turn off the DELETE_ROOT flag. The flag will be turned
             on for the correct new root of the delete.
          */
          oi->second->mObjFlags &= ~IMM_DELETE_ROOT;  // Remove delete-root flag
        }
        return SA_AIS_OK;
      }
    } else {
      LOG_NO(
          "ERR_BAD_OPERATION: Object '%s' already subject of another "
          "operation in same ccb. Currently can not handle delete chained "
          "on top of create or update in same ccb.",
          oi->first.c_str());
      return SA_AIS_ERR_BAD_OPERATION;
    }
  }

  if (oi->second->mObjFlags & IMM_RT_UPDATE_LOCK) {
    LOG_IN(
        "ERR_TRY_AGAIN: Object '%s' already subject of a persistent runtime "
        "attribute update",
        oi->first.c_str());
    return SA_AIS_ERR_TRY_AGAIN;
  }

  if (doIt) {
    bool localImpl = false;
    oi->second->mCcbId = ccb->mId;  // Overwrite any old obsolete ccb id.
    oi->second->mObjFlags |= IMM_DELETE_LOCK;  // Prevents creates of
                                               // subobjects.
    /*TRACE_5("Flags after insert delete lock:%u", oi->second->mObjFlags);*/

    ObjectMutation* oMut = new ObjectMutation(IMM_DELETE);
    ccb->mMutations[oi->first] = oMut;
    // oMut->mBeforeImage = oi->second;

    if (nonPersistentRto) {
      TRACE_7("Not incrementing op-count for ccb delete of non-persistent RTO");
    } else if (sendToPbe) {
      ccb->mOpCount++;
    }

    bool hasImpl = oi->second->mImplementer &&
                   oi->second->mImplementer->mNodeId && configObj;
    /* Not sending implementer ccb-delete upcalls for RTOs persistent or not. */

    bool ignoreImpl =
        (hasImpl && ccb->mAugCcbParent &&
         (ccb->mOriginatingNode == oi->second->mImplementer->mNodeId) &&
         (ccb->mAugCcbParent->mImplId == oi->second->mImplementer->mId));

    if (ignoreImpl) {
      LOG_IN(
          "Skipping OI upcall for delete of %s since OI is same "
          "as the originator of the modify (OI augmented ccb)",
          oi->first.c_str());
      goto bypass_impl;
    }

    if (hasImpl) {
      ccb->mState = IMM_CCB_DELETE_OP;

      CcbImplementerMap::iterator ccbi =
          ccb->mImplementers.find(oi->second->mImplementer->mId);
      if (ccbi == ccb->mImplementers.end()) {
        ImplementerCcbAssociation* impla =
            new ImplementerCcbAssociation(oi->second->mImplementer);
        ccb->mImplementers[oi->second->mImplementer->mId] = impla;
      }

      oMut->mWaitForImplAck = true;  // Wait for an ack from implementer
      // Increment even if we dont invoke locally
      if (++sLastContinuationId >= 0xfffffffe) {
        sLastContinuationId = 1;
      }
      oMut->mContinuationId = sLastContinuationId;

      if (ccb->mAugCcbParent) {
        oMut->mIsAugDelete = true;
        TRACE_5("This delete is a part of a ccb augmentation");
      }

      SaUint32T implConn = oi->second->mImplementer->mConn;

      osaf_clock_gettime(CLOCK_MONOTONIC, &ccb->mWaitStartTime);
      /* TODO: Resetting the ccb timer for each deleted object here.
         Not so efficient. Should set it only when all objects
         are processed.
      */

      if (implConn) {  // implementer is on THIS node.
        if (oi->second->mImplementer->mDying) {
          LOG_WA(
              "Lost connection with implementer %s in "
              "CcbObjectDelete.",
              oi->second->mImplementer->mImplementerName.c_str());
          // Let the timeout take care of it.
        } else {
          if (oi->second->mObjFlags & IMM_DN_INTERNAL_REP) {
            std::string objectName(oi->first);
            nameToExternal(objectName);
            objNameVector.push_back(objectName);
          } else {
            objNameVector.push_back(oi->first);
          }
          connVector.push_back(implConn);
          continuations.push_back(sLastContinuationId);
          localImpl = true;
        }
      }
    }

  bypass_impl:

    if (!localImpl && (configObj || (oi->second->mObjFlags & IMM_PRTO_FLAG))) {
      /* Object not yet included in upcall list since there was no regular
         local implementer. But object is persistent.
      */
      if (pbeIsLocal ||
          (!(oi->second->mClassInfo->mAppliers.empty()) &&
           hasLocalClassAppliers(oi->second->mClassInfo)) ||
          hasLocalObjAppliers(oi->first) ||
          specialApplyForClass(oi->second->mClassInfo)) {
        /* PBE is local and/or possibly local appliers.
           Add object to upcall list with zeroed connection and
           zeroed continuation (no reply wanted).
        */
        if (oi->second->mObjFlags & IMM_DN_INTERNAL_REP) {
          std::string objectName(oi->first);
          nameToExternal(objectName);
          objNameVector.push_back(objectName);
        } else {
          objNameVector.push_back(oi->first);
        }
        connVector.push_back(0);
        continuations.push_back(0);
      }
    }
  }
  /*TRACE_LEAVE();*/
  return SA_AIS_OK;
}

void ImmModel::setCcbErrorString(CcbInfo* ccb, const char* errorString,
                                 va_list vl) {
  int errLen = strlen(errorString) + 1;
  int len;
  va_list args;
  int isValidationErrString = 0;

  // Only store error strings on originating node where OM client resides.
  if ((ccb->mAugCcbParent && !ccb->mAugCcbParent->mOriginatingConn) ||
      (!ccb->mOriginatingConn)) {
    return;
  }

  char* fmtError = (char*)malloc(errLen);
  osafassert(fmtError);

  va_copy(args, vl);
  len = vsnprintf(fmtError, errLen, errorString, args);
  va_end(args);

  osafassert(len >= 0);
  len++; /* Reserve one byte for null-terminated sign '\0' */
  if (len > errLen) {
    char* newFmtError = (char*)realloc(fmtError, len);
    if (newFmtError == nullptr) {
      TRACE_5("realloc error, no memory");
      free(fmtError);
      return;
    } else {
      fmtError = newFmtError;
    }
    osafassert(vsnprintf(fmtError, len, errorString, vl) >= 0);
  }

  if (strstr(errorString, IMM_VALIDATION_ABORT) == errorString) {
    isValidationErrString = 1;
  } else if (strstr(errorString, IMM_RESOURCE_ABORT) == errorString) {
    isValidationErrString = 2;
  }

  unsigned int ix = 0;
  ImmsvAttrNameList* errStr = ccb->mErrorStrings;
  ImmsvAttrNameList** errStrTail = &(ccb->mErrorStrings);
  while (errStr) {
    if (!strncmp(fmtError, errStr->name.buf, len)) {
      TRACE_5("Discarding duplicate error string '%s' for ccb id %u", fmtError,
              ccb->mId);
      free(fmtError);
      return;
    }
    if (isValidationErrString) {
      // Precendence of validation abort over resource abort error string
      if (isValidationErrString == 1) {
        // Validation abort error string will replace resource abort error
        // string
        if (strstr(errStr->name.buf, IMM_RESOURCE_ABORT) == errStr->name.buf) {
          free(errStr->name.buf);
          errStr->name.buf = fmtError;
          errStr->name.size = len;
          return;
        }
      } else if (strstr(errStr->name.buf, IMM_RESOURCE_ABORT) ==
                     errStr->name.buf ||
                 strstr(errStr->name.buf, IMM_VALIDATION_ABORT) ==
                     errStr->name.buf) {
        // If validation abort or resource abort error string exist,
        // new resource abort string will not be added.
        // It can exists more validation abort error strings
        // or only the first resource abort error string
        free(fmtError);
        return;
      }
    }
    ++ix;
    errStrTail = &(errStr->next);
    errStr = errStr->next;
  }

  if (ix >= IMMSV_MAX_ATTRIBUTES) {
    TRACE_5("Discarding error string '%s' for ccb id %u (too many)", fmtError,
            ccb->mId);
    free(fmtError);
  } else {
    (*errStrTail) = (ImmsvAttrNameList*)malloc(sizeof(ImmsvAttrNameList));
    (*errStrTail)->next = NULL;
    (*errStrTail)->name.size = len;
    (*errStrTail)->name.buf = fmtError;
  }
}

void ImmModel::setCcbErrorString(CcbInfo* ccb, const char* errorString, ...) {
  va_list vl;

  va_start(vl, errorString);
  setCcbErrorString(ccb, errorString, vl);
  va_end(vl);
}

bool ImmModel::hasLocalClassAppliers(ClassInfo* classInfo) {
  ImplementerSet::iterator ii;
  for (ii = classInfo->mAppliers.begin(); ii != classInfo->mAppliers.end();
       ++ii) {
    ImplementerInfo* impl = *ii;
    if (impl->mConn && !impl->mDying && impl->mId) {
      return true;
    }
  }
  return false;
}

bool ImmModel::hasLocalObjAppliers(const std::string& objectName) {
  ImplementerSetMap::iterator ismIter = sObjAppliersMap.find(objectName);
  if (ismIter != sObjAppliersMap.end()) {
    ImplementerSet* implSet = ismIter->second;
    ImplementerSet::iterator ii = implSet->begin();
    for (; ii != implSet->end(); ++ii) {
      ImplementerInfo* impl = *ii;
      if (impl->mConn && !impl->mDying && impl->mId) {
        return true;
      }
    }
  }

  return false;
}

bool ImmModel::ccbWaitForDeleteImplAck(SaUint32T ccbId, SaAisErrorT* err,
                                       bool augDelete) {
  TRACE_ENTER();
  CcbVector::iterator i1;
  i1 = std::find_if(sCcbVector.begin(), sCcbVector.end(), CcbIdIs(ccbId));
  if (i1 == sCcbVector.end() || (!(*i1)->isActive())) {
    TRACE_5(
        "CCb %u terminated during ccbObjectDelete processing, "
        "ccb must be aborted",
        ccbId);
    *err = SA_AIS_ERR_FAILED_OPERATION;
    if (i1 != sCcbVector.end()) {
      setCcbErrorString(*i1, IMM_RESOURCE_ABORT
                        "CCB is terminated during ccbObjectDelete processing");
    }
    return false;
  }
  CcbInfo* ccb = *i1;

  *err = ccb->mVeto;

  ObjectMutationMap::iterator i2;
  for (i2 = ccb->mMutations.begin(); i2 != ccb->mMutations.end(); ++i2) {
    if (i2->second
            ->mWaitForImplAck) { /* Either aug or main ccb needs to wait */
      TRACE("The overall ccb still has un-ack'ed mutations to wait on");
      if (augDelete) {
        /* The query is in the context of an augmented delete op */
        if (i2->second->mIsAugDelete) {
          /* Found unresolved mutation for delete augmentation
             => the inside aug delete still has to wait */
          TRACE("Augmented delete still needs to wait");
          TRACE_LEAVE();
          return true;
        } else {
          /* Delete augmentation must not wait on delete mutations
             from main ccb, ignore this mutation, check the rest. */
          continue;
        }
      } else {
        /*The query is in the context of an outside delete, part of
          the original om-ccb. Must wait on all delete mutations.
        */
        osafassert(ccb->mState == IMM_CCB_DELETE_OP ||
                   (ccb->mAugCcbParent &&
                    ccb->mAugCcbParent->mState == IMM_CCB_DELETE_OP));
        TRACE_LEAVE();
        return true;
      }
    }
  }

  ccb->mWaitStartTime = kZeroSeconds;
  ccb->mState = IMM_CCB_READY;
  TRACE_LEAVE();
  return false;
}

bool ImmModel::ccbWaitForCompletedAck(SaUint32T ccbId, SaAisErrorT* err,
                                      SaUint32T* pbeConnPtr,
                                      unsigned int* pbeNodeIdPtr,
                                      SaUint32T* pbeIdPtr, SaUint32T* pbeCtnPtr,
                                      bool mPbeDisableCritical) {
  TRACE_ENTER();
  if (pbeNodeIdPtr) {
    TRACE("We expect there to be a Pbe");
    osafassert(pbeConnPtr);
    osafassert(pbeIdPtr);
    osafassert(pbeCtnPtr);
    *pbeNodeIdPtr = 0;
    *pbeConnPtr = 0;
    *pbeIdPtr = 0;
    *pbeCtnPtr = 0;
  }

  CcbVector::iterator i1;
  i1 = std::find_if(sCcbVector.begin(), sCcbVector.end(), CcbIdIs(ccbId));
  if (i1 == sCcbVector.end() || (!(*i1)->isActive())) {
    TRACE_5(
        "Ccb %u terminated during ccbCompleted processing, "
        "ccb must be aborted",
        ccbId);
    *err = SA_AIS_ERR_FAILED_OPERATION;
    if (i1 != sCcbVector.end()) {
      setCcbErrorString(*i1, IMM_RESOURCE_ABORT "CCB is terminated");
    }
    return false;
  }
  CcbInfo* ccb = *i1;

  *err = ccb->mVeto;

  CcbImplementerMap::iterator i2;
  for (i2 = ccb->mImplementers.begin(); i2 != ccb->mImplementers.end(); ++i2) {
    if (i2->second->mWaitForImplAck) {
      TRACE_LEAVE();
      return true; /* Continue waiting for some regular implementers */
    }
  }

  /* No more implementers to wait for.
     If PBE exists then we need to wait for pbe commit decission,
     but there can be no ccb timeout on that wait because we must
     obtain the decision from the PBE.
     If the PBE crashes we could possibly force it to presumed abort.
     But that requires the current repository on disk to be scrapped,
     followed by a fresh PBE dump.
     Instead we reset the timer so that a timeout on a ccb in critical
     will force attempts to recover ccb-outcome from the current PBE.
  */

  if (ccb->mState == IMM_CCB_VALIDATING) {
    /* Explicit validation (saImmOmCcbValidate) stop waiting. */
    ccb->mState = IMM_CCB_VALIDATED;
    TRACE_5("Explicit validation completed err state:%u for ccb:%u", *err,
            ccbId);
    if ((*err) == SA_AIS_OK) {
      /* We interrupt the validate/apply after validation is completed and
         before the commit phase of hte apply.
      */
      *err = SA_AIS_ERR_INTERRUPT;
    }

    ccb->mWaitStartTime = kZeroSeconds;
    return false;
  }

  if ((ccb->mState == IMM_CCB_VALIDATED) && ((*err) == SA_AIS_OK)) {
    TRACE(
        "Continuing apply/commit for explicitly validated ccb:%u err state:%u",
        ccbId, *err);
    TRACE("Dont think we ever get here.");
    LOG_IN("GOING FROM IMM_CCB_VALIDATED to IMM_CCB_PREPARE Ccb:%u", ccbId);
    ccb->mState = IMM_CCB_PREPARE;
  }

  if (ccb->mState == IMM_CCB_CRITICAL) {
    /* This must be the PBE reply. Stop waiting. */
    TRACE_5("PBE replied with rc:%u for ccb:%u", *err, ccbId);
    if ((*err) != SA_AIS_OK) {
      ccb->mState = IMM_CCB_PBE_ABORT;
    }
    ccb->mWaitStartTime = kZeroSeconds;
    return false;
  }

  if (ccb->mMutations.empty() && pbeNodeIdPtr) {
    TRACE_5("Ccb %u being applied is empty, avoid involving PBE", ccbId);
    pbeNodeIdPtr = NULL;
  }

  if (((*err) == SA_AIS_OK) && pbeNodeIdPtr) {
    /* There should be a PBE */
    ImplementerInfo* pbeImpl =
        reinterpret_cast<ImplementerInfo*>(getPbeOi(pbeConnPtr, pbeNodeIdPtr));
    if (pbeImpl && !mPbeDisableCritical) {
      /* There is in fact a PBE (up) */
      osafassert(ccb->mState == IMM_CCB_PREPARE);
      LOG_IN("GOING FROM IMM_CCB_PREPARE to IMM_CCB_CRITICAL Ccb:%u", ccbId);
      ccb->mState = IMM_CCB_CRITICAL;
      *pbeIdPtr = pbeImpl->mId;
      /* Add pbe implementer to the ccb implementer collection.
         Note that this is done here AFTER all normal implementers
         have replied on the ccb-completed upcall.
       */
      ImplementerCcbAssociation* impla = new ImplementerCcbAssociation(pbeImpl);
      ccb->mImplementers[pbeImpl->mId] = impla;
      impla->mWaitForImplAck = true;
      impla->mContinuationId = ccb->mOpCount;

      *pbeCtnPtr = impla->mContinuationId;

      /* With PBE enabled, the critical phase is non-trivial.
         The commit/abort decision is now delegated to the PBE.
         The CCB is in critical phase until we know the outcome.
         This means we can not terminate the ccb (release the
         objects write locked by the ccb) until we know the outcome.
         Restart the timer to catch ccbs hung waiting on PBE.
      */
      osaf_clock_gettime(CLOCK_MONOTONIC, &ccb->mWaitStartTime);
      return true; /* Wait for PBE commit*/
    } else {
      /* But there is not any PBE up currently => abort.  */
      ccb->mVeto = SA_AIS_ERR_FAILED_OPERATION;
      *err = ccb->mVeto;
      LOG_WA(
          "ERR_FAILED_OPERATION: Persistent back end is down "
          "ccb %u is aborted",
          ccbId);
      setCcbErrorString(ccb, IMM_RESOURCE_ABORT "PBE is down");
    }
  }

  TRACE_LEAVE();
  return false; /* nobody to wait for */
}

void ImmModel::ccbObjDelContinuation(immsv_oi_ccb_upcall_rsp* rsp,
                                     SaUint32T* reqConn, bool* augDelete) {
  TRACE_ENTER();
  std::string objectName(osaf_extended_name_borrow(&rsp->name));

  SaUint32T ccbId = rsp->ccbId;
  CcbInfo* ccb = 0;
  CcbVector::iterator i1;

  i1 = std::find_if(sCcbVector.begin(), sCcbVector.end(), CcbIdIs(ccbId));
  if (i1 == sCcbVector.end() || (!(*i1)->isActive())) {
    TRACE_5("ccb id %u missing or terminated in delete processing", ccbId);
    TRACE_LEAVE();
    return;
  }
  ccb = *i1;

  if (!(nameCheck(objectName) || nameToInternal(objectName))) {
    LOG_WA("Not a proper object name: %s", objectName.c_str());
    if (ccb->mVeto == SA_AIS_OK) {
      ccb->mVeto = SA_AIS_ERR_FAILED_OPERATION;
      setCcbErrorString(ccb, IMM_VALIDATION_ABORT "Not a proper object name");
    }
    return;
  }

  if (ccb->mAugCcbParent && (ccb->mAugCcbParent->mContinuationId == rsp->inv)) {
    TRACE("Closing CCB augmentation for inv %u on delete", rsp->inv);
    if (ccb->mState != IMM_CCB_READY) {
      LOG_WA("CCB %u was interrupted in augmentation, state:%u", ccbId,
             ccb->mState);
      if ((rsp->result == SA_AIS_OK) && ccb->isActive()) {
        LOG_NO("Vetoing ccb %u with ERR_FAILED_OPERATION", ccbId);
        ccb->mVeto = SA_AIS_ERR_FAILED_OPERATION;
        setCcbErrorString(
            ccb, IMM_RESOURCE_ABORT "CCB is interrupted in augmentation");
      }
    }
    ccb->mOriginatingConn = ccb->mAugCcbParent->mOriginatingConn;
    ccb->mOriginatingNode = ccb->mAugCcbParent->mOriginatingNode;
    ccb->mState = ccb->mAugCcbParent->mState;
    ccb->mErrorStrings = ccb->mAugCcbParent->mErrorStrings;

    ccb->mAugCcbParent->mErrorStrings = NULL;
    ccb->mAugCcbParent->mContinuationId = 0;
    free(ccb->mAugCcbParent);
    ccb->mAugCcbParent = NULL;
  }

  ObjectMutationMap::iterator omuti = ccb->mMutations.find(objectName);
  if (omuti == ccb->mMutations.end()) {
    LOG_WA("object '%s' Not found in ccb - aborting ccb", objectName.c_str());
    if (ccb->mVeto == SA_AIS_OK) {
      ccb->mVeto = SA_AIS_ERR_FAILED_OPERATION;
      setCcbErrorString(ccb, IMM_VALIDATION_ABORT "Object is not found in CCB");
    }
  } else {
    osafassert(omuti->second->mWaitForImplAck);
    omuti->second->mWaitForImplAck = false;
    osafassert(/*(omuti->second->mContinuationId == 0) ||*/
               (omuti->second->mContinuationId == (SaUint32T)rsp->inv));

    /* Only send response when ccb continuation is not purged */
    if (!ccb->mPurged) {
      *reqConn = ccb->mOriginatingConn;
    }

    *augDelete = omuti->second->mIsAugDelete;

    if (rsp->result != SA_AIS_OK) {
      if (ccb->mVeto == SA_AIS_OK) {
        LOG_IN(
            "ImmModel::ccbObjDelContinuation: "
            "implementer returned error, Ccb aborted with error: %u",
            rsp->result);
        ccb->mVeto = SA_AIS_ERR_FAILED_OPERATION;
        if (rsp->result == SA_AIS_ERR_BAD_OPERATION ||
            rsp->result == SA_AIS_ERR_FAILED_OPERATION) {
          setCcbErrorString(
              ccb, IMM_VALIDATION_ABORT "Implementer returned error: %u",
              rsp->result);
        } else {
          setCcbErrorString(ccb,
                            IMM_RESOURCE_ABORT "Implementer returned error: %u",
                            rsp->result);
        }
        // TODO: This is perhaps more drastic than the specification
        // demands. We are here aborting the entire Ccb, whereas the spec
        // seems to allow for a non-ok returnvalue from implementer
        //(in this callback) to simply reject the delete request,
        // leaving the ccb otherwise intact.
        // On the other hand it is not *against* the specification as the
        // imm implementation has the right to veto any ccb at any time.
      }

      if (rsp->errorString.size) {
        /*Collect err strings, see: http://devel.opensaf.org/ticket/1904
          Only done at originating node since that is where the OM
          client resides. */
        setCcbErrorString(ccb, rsp->errorString.buf);
      }
    }
  }
  TRACE_LEAVE();
}

void ImmModel::ccbCompletedContinuation(immsv_oi_ccb_upcall_rsp* rsp,
                                        SaUint32T* reqConn) {
  TRACE_ENTER();
  SaUint32T ccbId = rsp->ccbId;
  CcbInfo* ccb = 0;
  CcbVector::iterator i1;
  CcbImplementerMap::iterator ix;

  i1 = std::find_if(sCcbVector.begin(), sCcbVector.end(), CcbIdIs(ccbId));
  if (i1 == sCcbVector.end() || (!(*i1)->isActive())) {
    LOG_WA("ccb id %u missing or terminated in completed processing", ccbId);
    TRACE_LEAVE();
    return;
  }
  ccb = *i1;

  if (ccb->mAugCcbParent && (ccb->mAugCcbParent->mContinuationId == rsp->inv)) {
    ccb->mOriginatingConn = ccb->mAugCcbParent->mOriginatingConn;
    ccb->mOriginatingNode = ccb->mAugCcbParent->mOriginatingNode;
    ccb->mState = ccb->mAugCcbParent->mState;
    ccb->mErrorStrings = ccb->mAugCcbParent->mErrorStrings;

    ccb->mAugCcbParent->mErrorStrings = NULL;
    ccb->mAugCcbParent->mContinuationId = 0;
    free(ccb->mAugCcbParent);
    ccb->mAugCcbParent = NULL;
  }

  if (ccb->mState == IMM_CCB_VALIDATED) {
    LOG_IN("GOING FROM IMM_CCB_VALIDATED to IMM_CCB_PREPARE Ccb:%u", ccbId);
    ccb->mState = IMM_CCB_PREPARE;
    /* Only send response when ccb continuation is not purged */
    if (!ccb->mPurged) {
      *reqConn = ccb->mOriginatingConn;
    }
    goto done;
  }

  ix = ccb->mImplementers.find(rsp->implId);
  if (ix == ccb->mImplementers.end()) {
    if ((ccb->mVeto == SA_AIS_OK) && (ccb->mState < IMM_CCB_CRITICAL)) {
      LOG_WA(
          "Completed continuation: implementer '%u' Not found "
          "in ccb %u aborting ccb",
          rsp->implId, ccbId);
      ccb->mVeto = SA_AIS_ERR_FAILED_OPERATION;
      setCcbErrorString(ccb, IMM_RESOURCE_ABORT "Implementer not found");
    } else {
      LOG_WA(
          "Completed continuation: implementer '%u' Not found "
          "in ccb %u in state(%u), can not abort",
          rsp->implId, ccbId, ccb->mState);
    }
  } else {
    if (ccb->mState == IMM_CCB_CRITICAL) {
      /* This is the commit/abort decision from the PBE.*/
      /* Verify that it is the PBE that is replying. */
      SaUint32T dummyConn;
      unsigned int dummyNodeId;
      ImplementerInfo* pbeImpl = reinterpret_cast<ImplementerInfo*>(
          getPbeOi(&dummyConn, &dummyNodeId));
      if (!pbeImpl || (pbeImpl->mId != rsp->implId)) {
        LOG_WA("Received commit/abort decision on ccb %u from terminated PBE",
               ccbId);
        TRACE_LEAVE();
        return; /* Intentionally drop the missmatched response.*/
                /* If the PBE responds correctly, but crashes and gets deregistered
                   as implementer before this reply reaches us, the reply could be
                   seen as valid, but gets discarded here.
                */
      }
      if (rsp->result == SA_AIS_OK) {
        TRACE("COMMIT decision on ccb %u received from PBE", ccbId);
      } else {
        TRACE("ABORT decision on ccb %u received from PBE", ccbId);
      }
    }

    ix->second->mWaitForImplAck = false;
    if (/*(ix->second->mContinuationId == 0) ||*/
        (ix->second->mContinuationId != rsp->inv)) {
      TRACE("ix->second->mContinuationId(%u) != rsp->inv(%u)",
            ix->second->mContinuationId, rsp->inv);
    }

    /* Only send response when ccb continuation is not purged */
    if (!ccb->mPurged) {
      *reqConn = ccb->mOriginatingConn;
    }

    if (rsp->result != SA_AIS_OK) {
      if (ccb->mVeto == SA_AIS_OK) {
        switch (rsp->result) {
          case SA_AIS_ERR_BAD_OPERATION:
            LOG_NO(
                "Validation error (BAD_OPERATION) reported by implementer '%s', "
                "Ccb %u will be aborted",
                ix->second->mImplementer->mImplementerName.c_str(), ccbId);
            setCcbErrorString(ccb, IMM_VALIDATION_ABORT
                              "Completed validation fails (ERR_BAD_OPERATION)");
            break;

          case SA_AIS_ERR_NO_MEMORY:
          case SA_AIS_ERR_NO_RESOURCES:
            LOG_NO(
                "Resource error %u reported by implementer '%s', Ccb %u will be aborted",
                rsp->result, ix->second->mImplementer->mImplementerName.c_str(),
                ccbId);
            setCcbErrorString(ccb,
                              IMM_RESOURCE_ABORT
                              "Completed validation fails (Error code: %u)",
                              rsp->result);
            break;

          default:
            LOG_NO(
                "Invalid error reported implementer '%s', Ccb %u will be aborted",
                ix->second->mImplementer->mImplementerName.c_str(), ccbId);
            setCcbErrorString(ccb,
                              IMM_VALIDATION_ABORT
                              "Completed validation fails (Error code: %u)",
                              rsp->result);
        }
        ccb->mVeto = SA_AIS_ERR_FAILED_OPERATION;
      }

      if (rsp->errorString.size) {
        TRACE("Error string received.");
        /*Collect err strings, see: http://devel.opensaf.org/ticket/1904 */
        setCcbErrorString(ccb, rsp->errorString.buf);
      }
    }
  }
done:
  TRACE_LEAVE();
}

ImmsvAttrNameList* ImmModel::ccbGrabErrStrings(SaUint32T ccbId) {
  CcbInfo* ccb = 0;
  CcbVector::iterator i1;

  i1 = std::find_if(sCcbVector.begin(), sCcbVector.end(), CcbIdIs(ccbId));
  if (i1 == sCcbVector.end()) {
    LOG_WA("ccb id %u missing or terminated", ccbId);
    return NULL;
  }
  ccb = *i1;

  ImmsvAttrNameList* errStr = ccb->mErrorStrings;
  ccb->mErrorStrings = NULL;
  return errStr;
}

void ImmModel::ccbObjCreateContinuation(SaUint32T ccbId, SaUint32T invocation,
                                        SaAisErrorT error, SaUint32T* reqConn) {
  TRACE_ENTER();
  CcbInfo* ccb = 0;
  CcbVector::iterator i1;

  i1 = std::find_if(sCcbVector.begin(), sCcbVector.end(), CcbIdIs(ccbId));
  if (i1 == sCcbVector.end() || (!(*i1)->isActive())) {
    LOG_WA("ccb id %u missing or terminated", ccbId);
    TRACE_LEAVE();
    return;
  }
  ccb = *i1;

  ObjectMutationMap::iterator omuti;
  for (omuti = ccb->mMutations.begin(); omuti != ccb->mMutations.end();
       ++omuti) {
    if (omuti->second->mContinuationId == invocation) {
      break;
    }
  }

  if (omuti == ccb->mMutations.end()) {
    LOG_WA("create invocation '%u' Not found in ccb - aborting ccb %u",
           invocation, ccbId);
    if (ccb->mVeto == SA_AIS_OK) {
      ccb->mVeto = SA_AIS_ERR_FAILED_OPERATION;
      setCcbErrorString(
          ccb, IMM_RESOURCE_ABORT "Create invocation is not found in CCB");
    }
  } else {
    osafassert(omuti->second->mWaitForImplAck);
    omuti->second->mWaitForImplAck = false;
    ccb->mWaitStartTime = kZeroSeconds;
  }

  if (ccb->mAugCcbParent &&
      (ccb->mAugCcbParent->mContinuationId == invocation)) {
    TRACE("Closing CCB augmentation for inv %u on create", invocation);
    if (ccb->mState != IMM_CCB_READY) {
      LOG_WA("CCB %u was interrupted in augmentation, state:%u", ccbId,
             ccb->mState);
      if ((error == SA_AIS_OK) && ccb->isActive()) {
        LOG_IN("Vetoing ccb %u with ERR_FAILED_OPERATION", ccbId);
        error = SA_AIS_ERR_FAILED_OPERATION;
        setCcbErrorString(
            ccb, IMM_RESOURCE_ABORT "CCB was interrupted in augmentation");
      }
    }
    ccb->mOriginatingConn = ccb->mAugCcbParent->mOriginatingConn;
    ccb->mOriginatingNode = ccb->mAugCcbParent->mOriginatingNode;
    ccb->mState = ccb->mAugCcbParent->mState;
    ccb->mErrorStrings = ccb->mAugCcbParent->mErrorStrings;

    ccb->mAugCcbParent->mErrorStrings = NULL;
    ccb->mAugCcbParent->mContinuationId = 0;
    free(ccb->mAugCcbParent);
    ccb->mAugCcbParent = NULL;
  }

  /* Only send response when ccb continuation is not purged */
  if (!ccb->mPurged) {
    *reqConn = ccb->mOriginatingConn;
  }

  if ((ccb->mVeto == SA_AIS_OK) && (error != SA_AIS_OK)) {
    LOG_NO(
        "ImmModel::ccbObjCreateContinuation: "
        "implementer returned error, Ccb aborted with error: %u",
        error);
    ccb->mVeto = SA_AIS_ERR_FAILED_OPERATION;
    if (error == SA_AIS_ERR_BAD_OPERATION ||
        error == SA_AIS_ERR_FAILED_OPERATION) {
      setCcbErrorString(
          ccb, IMM_VALIDATION_ABORT "Implementer returned error: %u", error);
    } else {
      setCcbErrorString(
          ccb, IMM_RESOURCE_ABORT "Implementer returned error: %u", error);
    }
    // TODO: This is perhaps more drastic than the specification demands.
    // We are here aborting the entire Ccb, whereas the spec seems to allow
    // for a non-ok returnvalue from implementer (in this callback) to
    // simply reject the create request, leaving the ccb otherwise intact.
    // On the other hand it is not *against* the specification as the
    // imm implementation has the right to veto any ccb at any time.
  }
  if (ccb->mState != IMM_CCB_CREATE_OP) {
    if (ccb->mVeto == SA_AIS_OK) {
      LOG_ER(
          "Unexpected state(%u) for ccb(%u) found in "
          "ccbObjectCreateContinuation ",
          ccb->mState, ccbId);
      osafassert(ccb->mState == IMM_CCB_CREATE_OP);
    } else {
      LOG_NO(
          "Ignoring unexpected state(%u) for ccb(%u) found in "
          "ccbObjectCreateContinuation ",
          ccb->mState, ccbId);
    }
  }

  ccb->mState = IMM_CCB_READY;
  TRACE_LEAVE();
}

void ImmModel::ccbObjModifyContinuation(SaUint32T ccbId, SaUint32T invocation,
                                        SaAisErrorT error, SaUint32T* reqConn) {
  TRACE_ENTER();
  CcbInfo* ccb = 0;
  CcbVector::iterator i1;

  i1 = std::find_if(sCcbVector.begin(), sCcbVector.end(), CcbIdIs(ccbId));
  if (i1 == sCcbVector.end() || (!(*i1)->isActive())) {
    LOG_WA("ccb id %u missing or terminated", ccbId);
    TRACE_LEAVE();
    return;
  }
  ccb = *i1;

  ObjectMutationMap::iterator omuti;
  for (omuti = ccb->mMutations.begin(); omuti != ccb->mMutations.end();
       ++omuti) {
    if (omuti->second->mContinuationId == invocation) {
      break;
    }
  }

  if (omuti == ccb->mMutations.end()) {
    LOG_WA("modify invocation '%u' Not found in ccb - aborting ccb %u",
           invocation, ccbId);
    if (ccb->mVeto == SA_AIS_OK) {
      ccb->mVeto = SA_AIS_ERR_FAILED_OPERATION;
      setCcbErrorString(
          ccb, IMM_RESOURCE_ABORT "Modify invocation is not found in CCB");
    }
  } else {
    osafassert(omuti->second->mWaitForImplAck);
    omuti->second->mWaitForImplAck = false;
    ccb->mWaitStartTime = kZeroSeconds;
  }

  if (ccb->mAugCcbParent &&
      (ccb->mAugCcbParent->mContinuationId == invocation)) {
    TRACE("Closing CCB augmentation for inv %u on modify", invocation);
    if (ccb->mState != IMM_CCB_READY) {
      LOG_WA("CCB %u was interrupted in augmentation, state:%u", ccbId,
             ccb->mState);
      if ((error == SA_AIS_OK) && ccb->isActive()) {
        LOG_IN("Vetoing ccb %u with ERR_FAILED_OPERATION", ccbId);
        error = SA_AIS_ERR_FAILED_OPERATION;
        setCcbErrorString(
            ccb, IMM_RESOURCE_ABORT "CCB was interrupted in augmentation");
      }
    }
    ccb->mOriginatingConn = ccb->mAugCcbParent->mOriginatingConn;
    ccb->mOriginatingNode = ccb->mAugCcbParent->mOriginatingNode;
    ccb->mState = ccb->mAugCcbParent->mState;
    ccb->mErrorStrings = ccb->mAugCcbParent->mErrorStrings;

    ccb->mAugCcbParent->mErrorStrings = NULL;
    ccb->mAugCcbParent->mContinuationId = 0;
    free(ccb->mAugCcbParent);
    ccb->mAugCcbParent = NULL;
  }

  /* Only send response when ccb continuation is not purged */
  if (!ccb->mPurged) {
    *reqConn = ccb->mOriginatingConn;
  }

  if ((ccb->mVeto == SA_AIS_OK) && (error != SA_AIS_OK)) {
    LOG_IN(
        "ImmModel::ccbObjModifyContinuation: "
        "implementer returned error, Ccb aborted with error: %u",
        error);
    ccb->mVeto = SA_AIS_ERR_FAILED_OPERATION;
    if (error == SA_AIS_ERR_BAD_OPERATION ||
        error == SA_AIS_ERR_FAILED_OPERATION) {
      setCcbErrorString(
          ccb, IMM_VALIDATION_ABORT "Implementer returned error: %u", error);
    } else {
      setCcbErrorString(
          ccb, IMM_RESOURCE_ABORT "Implementer returned error: %u", error);
    }
    // TODO: This is perhaps more drastic than the specification demands.
    // We are here aborting the entire Ccb, whereas the spec seems to allow
    // for a non-ok returnvalue from implementer (in this callback) to
    // simply reject the delete request, leaving the ccb otherwise intact.
    // On the other hand it is not *against* the specification as the
    // imm implementation has the right to veto any ccb at any time.
  }
  if (ccb->mState != IMM_CCB_MODIFY_OP) {
    if (ccb->mVeto == SA_AIS_OK) {
      LOG_ER(
          "Unexpected state(%u) for ccb(%u) found in "
          "ccbObjectModifyContinuation ",
          ccb->mState, ccbId);
      osafassert(ccb->mState == IMM_CCB_MODIFY_OP);
    } else {
      LOG_NO(
          "Ignoring unexpected state(%u) for ccb(%u) found in "
          "ccbObjectModifyContinuation ",
          ccb->mState, ccbId);
    }
  }
  ccb->mState = IMM_CCB_READY;
  TRACE_LEAVE();
}

SaAisErrorT ImmModel::ccbReadLockObject(const ImmsvOmSearchInit* req) {
  TRACE_ENTER();
  SaAisErrorT err = SA_AIS_OK;
  SaUint32T ccbId = req->ccbId;
  size_t sz = strnlen((char*)req->rootName.buf, (size_t)req->rootName.size);
  std::string objectName((const char*)req->rootName.buf, sz);
  ObjectMap::iterator i;
  ObjectInfo* obj = NULL;
  SaUint32T ccbIdOfObj = 0;
  CcbVector::iterator i1;
  CcbInfo* ccb = 0;

  if (objectName.empty()) {
    LOG_NO("ERR_INVALID_PARAM: Empty DN is not allowed");
    err = SA_AIS_ERR_INVALID_PARAM;
    goto done;
  }

  // Validate object name
  if (!(nameCheck(objectName) || nameToInternal(objectName))) {
    LOG_NO("ERR_INVALID_PARAM: Not a proper object name");
    err = SA_AIS_ERR_INVALID_PARAM;
    goto done;
  }

  i = sObjectMap.find(objectName);
  if (i == sObjectMap.end()) {
    // TRACE_7("ERR_NOT_EXIST: Object '%s' does not exist", objectName.c_str());
    err = SA_AIS_ERR_NOT_EXIST;
    goto done;
  }

  obj = i->second;

  osafassert(obj->mClassInfo);

  if (obj->mClassInfo->mCategory != SA_IMM_CLASS_CONFIG) {
    LOG_NO("ERR_INVALID_PARAM: object '%s' is not a configuration object",
           objectName.c_str());
    err = SA_AIS_ERR_INVALID_PARAM;
    goto done;
  }

  i1 = std::find_if(sCcbVector.begin(), sCcbVector.end(), CcbIdIs(ccbId));
  if (i1 == sCcbVector.end()) {
    LOG_NO("ERR_BAD_HANDLE: ccb id %u does not exist", ccbId);
    err = SA_AIS_ERR_BAD_HANDLE;
    goto done;
  }
  ccb = *i1;

  if (!ccb->isOk()) {
    LOG_IN(
        "ERR_FAILED_OPERATION: ccb %u is in an error state "
        "rejecting ccbObjectRead operation ",
        ccbId);
    /* !ccb->isOk(), error string with abort reason must already be set */
    err = SA_AIS_ERR_FAILED_OPERATION;
    goto done;
  }

  if (ccb->mState > IMM_CCB_READY) {
    if (ccb->mAugCcbParent == NULL) {
      LOG_WA(
          "ERR_BAD_OPERATION: ccb %u is not in an expected state: %u "
          "rejecting ccbObjectRead operation ",
          ccbId, ccb->mState);
      err = SA_AIS_ERR_BAD_OPERATION;
      goto done;
    } else {
      if (ccb->mState < IMM_CCB_CRITICAL) {
        LOG_IN("SafeRead inside augmentation allowed");
      } else {
        LOG_WA(
            "ERR_BAD_OPERATION: ccb %u is not in an expected state: %u "
            "rejecting ccbObjectRead operation ",
            ccbId, ccb->mState);
        err = SA_AIS_ERR_BAD_OPERATION;
        goto done;
      }
    }
  }

  ccbIdOfObj = obj->mCcbId;
  if (ccbIdOfObj != ccbId) {
    i1 =
        std::find_if(sCcbVector.begin(), sCcbVector.end(), CcbIdIs(ccbIdOfObj));
    if ((i1 != sCcbVector.end()) && ((*i1)->isActive())) {
      TRACE_7("ERR_BUSY: ccb id %u differs from active ccb id on object %u",
              ccbId, ccbIdOfObj);
      err = SA_AIS_ERR_BUSY;
      goto done;
    }
    /* Set ccb-id to zero to signify no exclusive lock set.
       CcbId of 0 does not mean that a shared read lock must be set for the
       object. It only means that zero or more shared read locks *could* be set
       for this object.
     */
    obj->mCcbId = 0;
  } else {
    TRACE_7(
        "Safe-read: Object '%s' is currently *exclusive* locked by *same* ccb %u",
        objectName.c_str(), ccbId);
    /* Read-lock succeeds as a no-op.
       Actual access will fail if it is delete locked in same ccb.
       We could fail here for the delete locked case, but it is actually not the
       requested locking for the ccb that fails, it is the safe-read-access done
       later. The safe read of the object will fail with ERR_NOT_EXIST because
       the object is scheduled for delete in the *same* ccb as the reader. If
       the object was locked for delete in a different ccb then the safe-reader
       in this ccb would get ERR_BUSY.
    */
    osafassert(err == SA_AIS_OK);
    goto done;
  }

  osafassert(!ccb->isReadLockedByThisCcb(obj));
  ccb->addObjReadLock(obj, objectName);

done:
  TRACE_LEAVE();
  return err;
}

SaAisErrorT ImmModel::objectIsLockedByCcb(const ImmsvOmSearchInit* req) {
  // ##Redo this whole method to simply check ccb and ccb mutation.?
  TRACE_ENTER();
  SaAisErrorT err = SA_AIS_OK;
  SaUint32T ccbId = req->ccbId;
  size_t sz = strnlen((char*)req->rootName.buf, (size_t)req->rootName.size);
  std::string objectName((const char*)req->rootName.buf, sz);
  ObjectMap::iterator i;
  ObjectInfo* obj = NULL;
  SaUint32T ccbIdOfObj = 0;
  CcbVector::iterator i1;
  CcbInfo* ccb = 0;

  if (objectName.empty()) {
    LOG_NO("ERR_INVALID_PARAM: Empty DN is not allowed");
    err = SA_AIS_ERR_INVALID_PARAM;
    goto done;
  }

  // Validate object name
  if (!(nameCheck(objectName) || nameToInternal(objectName))) {
    LOG_NO("ERR_INVALID_PARAM: Not a proper object name");
    err = SA_AIS_ERR_INVALID_PARAM;
    goto done;
  }

  i = sObjectMap.find(objectName);
  if (i == sObjectMap.end()) {
    // TRACE_7("ERR_NOT_EXIST: Object '%s' does not exist", objectName.c_str());
    err = SA_AIS_ERR_NOT_EXIST;
    goto done;
  }

  obj = i->second;
  osafassert(obj->mClassInfo);

  if (obj->mClassInfo->mCategory != SA_IMM_CLASS_CONFIG) {
    LOG_NO("ERR_INVALID_PARAM: object '%s' is not a configuration object",
           objectName.c_str());
    err = SA_AIS_ERR_INVALID_PARAM;
    goto done;
  }

  i1 = std::find_if(sCcbVector.begin(), sCcbVector.end(), CcbIdIs(ccbId));
  if (i1 == sCcbVector.end()) {
    LOG_NO("ERR_BAD_HANDLE: ccb id %u does not exist", ccbId);
    err = SA_AIS_ERR_BAD_HANDLE;
    goto done;
  }
  ccb = *i1;

  if (!ccb->isOk()) {
    LOG_IN(
        "ERR_FAILED_OPERATION: ccb %u is in an error state "
        "rejecting ccbObjectRead operation ",
        ccbId);
    /* !ccb->isOk(), error string with abort reason must already be set */
    err = SA_AIS_ERR_FAILED_OPERATION;
    goto done;
  }

  if (ccb->mState > IMM_CCB_READY) {
    if (ccb->mAugCcbParent == NULL) {
      LOG_WA(
          "ERR_BAD_OPERATION: ccb %u is not in an expected state: %u "
          "rejecting ccbObjectRead operation ",
          ccbId, ccb->mState);
      err = SA_AIS_ERR_BAD_OPERATION;
      goto done;
    } else {
      if (ccb->mState < IMM_CCB_CRITICAL) {
        LOG_IN("SafeRead inside augmentation allowed");
      } else {
        LOG_WA(
            "ERR_BAD_OPERATION: ccb %u is not in an expected state: %u "
            "rejecting ccbObjectRead operation ",
            ccbId, ccb->mState);
        err = SA_AIS_ERR_BAD_OPERATION;
        goto done;
      }
    }
  }

  ccbIdOfObj = obj->mCcbId;
  if (ccbIdOfObj == ccbId) {
    TRACE_7("Object '%s' is currently *exclusive* locked by same ccb %u",
            objectName.c_str(), ccbId);
    osafassert(err == SA_AIS_OK);
    goto done;
  } else if (ccb->isReadLockedByThisCcb(obj)) {
    TRACE_7("Object '%s' is currently *shared* read locked by same ccb %u",
            objectName.c_str(), ccbId);
    osafassert(err == SA_AIS_OK);
    goto done;
  } else if (ccbIdOfObj) {
    // CcbId is not zero.
    TRACE_7("Object '%s' is currently NOT locked by same ccb %u",
            objectName.c_str(), ccbId);
    i1 =
        std::find_if(sCcbVector.begin(), sCcbVector.end(), CcbIdIs(ccbIdOfObj));
    if ((i1 != sCcbVector.end()) && ((*i1)->isActive())) {
      TRACE_7(
          "ERR_BUSY: Safe-read: ccb id %u differs from active ccb id on object %u",
          ccbId, ccbIdOfObj);
      err = SA_AIS_ERR_BUSY;
      goto done;
    }
    /* clear the obsolete ccb-id */
    ccbIdOfObj = 0;
  }

  if (ccb->mAugCcbParent) {
    LOG_IN("This is a lock check in an augmented CCB");
  }

  /* Intentionally NO check for admin-owner. SafeRead allows read sharing with
     other CCBs, => no admin-owner exclusivity.
  */

  if (ccbIdOfObj == 0) {
    TRACE_7("Object '%s' is currently not locked by this ccb %u",
            objectName.c_str(), ccbId);
    err = SA_AIS_ERR_INTERRUPT;
    goto done;
  }

done:
  TRACE_LEAVE();
  return err;
}

SaAisErrorT ImmModel::accessorGet(const ImmsvOmSearchInit* req,
                                  ImmSearchOp& op) {
  TRACE_ENTER();
  SaAisErrorT err = SA_AIS_OK;

  size_t sz = strnlen((char*)req->rootName.buf, (size_t)req->rootName.size);
  std::string objectName((const char*)req->rootName.buf, sz);

  SaImmScopeT scope = (SaImmScopeT)req->scope;
  SaImmSearchOptionsT searchOptions = (SaImmSearchOptionsT)req->searchOptions;
  ObjectMap::iterator i;
  ObjectInfo* obj = NULL;
  bool implNotSet = true;
  ImmAttrValueMap::iterator j;
  ImplementerEvtMap::iterator iem;
  int matchedAttributes = 0;
  int soughtAttributes = 0;
  SaImmSearchOptionsT notAllowedOptions = 0LL;
  bool nonExtendedNameCheck =
      req->searchParam.present > ImmOmSearchParameter_PR_oneAttrParam;
  bool checkAttribute = false;
  bool isSafeRead = (req->ccbId != 0);
  CcbInfo* ccb = NULL;
  CcbVector::iterator i1;

  if (isSafeRead) {
    TRACE_7("SafeRead (accessorGet) on Object: '%s'", objectName.c_str());
  }

  if (nonExtendedNameCheck) {
    op.setNonExtendedName();
  }

  if (objectName.empty()) {
    LOG_NO("ERR_INVALID_PARAM: Empty DN is not allowed");
    err = SA_AIS_ERR_INVALID_PARAM;
    goto accessorExit;
  }

  if (nonExtendedNameCheck &&
      objectName.size() >= SA_MAX_UNEXTENDED_NAME_LENGTH) {
    LOG_NO("ERR_NAME_TOO_LONG: Object name is too long");
    err = SA_AIS_ERR_NAME_TOO_LONG;
    goto accessorExit;
  }

  // Validate object name
  if (!(nameCheck(objectName) || nameToInternal(objectName))) {
    LOG_NO("ERR_INVALID_PARAM: Not a proper object name");
    err = SA_AIS_ERR_INVALID_PARAM;
    goto accessorExit;
  }

  i = sObjectMap.find(objectName);
  if (i == sObjectMap.end()) {
    TRACE_7("ERR_NOT_EXIST: Object '%s' does not exist", objectName.c_str());
    err = SA_AIS_ERR_NOT_EXIST;
    goto accessorExit;
  }
  if (i->second->mObjFlags & IMM_CREATE_LOCK) {
    /* Both regular accessor-get and safe-read affected by create lock.*/
    if (isSafeRead) {
      if (req->ccbId != i->second->mCcbId) {
        TRACE_7(
            "ERR_NOT_EXIST: Object '%s' is being created but not in this ccb.",
            objectName.c_str());
        err = SA_AIS_ERR_NOT_EXIST;
        goto accessorExit;
      }
      TRACE("req->ccbId == i->second->mCcbId  i.e. same ccb ok for safe-read");
      obj = i->second;
    } else { /* Regular accessor-get */
      TRACE_7(
          "ERR_NOT_EXIST: Object '%s' is being created, but ccb "
          "or PRTO PBE, not yet applied",
          objectName.c_str());
      err = SA_AIS_ERR_NOT_EXIST;
      goto accessorExit;
    }
  } else if (isSafeRead) {
    /* Interference with unapplied create taken care of above in if-branch for
       both safeRead and regular accessorGet.

       Regular accessor-get is not affected by open delete, modify, or
       safe-reads. It simply accesses the current committed version.

       Safe-read *is* affected by unapplied modify or delete, hence this else
       branch.

       Mdify in same ccb => safe-read must read the after-image-version in the
       same ccb. Delete in same ccb => the object becomes inaccesible
       (ERR_NOT_EXIST) for safe-read. Mofify or delete by *other* ccb =>
       safe-read should have been rejected (ERR_BUSY) in objectIsLockedByCcb().

       Safe-read is of course not affected by other safe-read operations on this
       object in same or other ccbs. Latest commited version is read.
    */

    i1 =
        std::find_if(sCcbVector.begin(), sCcbVector.end(), CcbIdIs(req->ccbId));
    osafassert(i1 !=
               sCcbVector.end()); /*Already checked in objectIsLockedByCcb. */
    ccb = *i1;
    if ((i->second->mCcbId == 0) && !(ccb->isReadLockedByThisCcb(i->second))) {
      LOG_WA(
          "Safe read on object %s NOT locked by ccb (%u). Should not be caught here",
          objectName.c_str(), req->ccbId);
      /* If we get here this may be seen as a bug to be be corrected. Should
         perhaps assert here, but be lenient for now and just report "INTERRUPT"
         which should result in an new attempt to lock the object for safe-read.
         The safe-read lock should have been obtained before reacing
         accessor-get.
       */
      err = SA_AIS_ERR_INTERRUPT; /* Try to lock the object for safe read.*/
      goto accessorExit;
    }

    if (i->second->mCcbId != req->ccbId) {
      if (i->second->mCcbId == 0) {
        TRACE_7(
            "Object %s locked for shared read in ccb (%u) and possibly other ccbs",
            objectName.c_str(), req->ccbId);
        osafassert(i->second->mObjFlags & IMM_SHARED_READ_LOCK);
        obj = i->second; /* Use latest committed version. */
      } else {
        LOG_WA(
            "Safe read on object %s exclusive locked by other ccb (%u). "
            "Should not be caught here",
            objectName.c_str(), i->second->mCcbId);
        /* If we get here this may be seen as a bug to be be corrected. Should
           perhaps assert here, but be lenient for now and just report "BUSY".
        */
        err = SA_AIS_ERR_BUSY;
        goto accessorExit;
      }
    } else { /* i->second->mCcbId == req->ccbId */
      osafassert(i->second->mCcbId != 0);
      TRACE_7("Safe read on object *exclusive* locked by *this* ccb %u",
              req->ccbId);

      /* This can not be a safe-read. */
      osafassert(!(i->second->mObjFlags & IMM_SHARED_READ_LOCK));

      /* Prior op can not be a create. Determine if it is delete or modify */

      if (i->second->mObjFlags & IMM_DELETE_LOCK) {
        LOG_IN("ERR_NOT_EXIST: Object '%s' is being deleted in same ccb %u",
               objectName.c_str(), req->ccbId);
        err = SA_AIS_ERR_NOT_EXIST;
        goto accessorExit;
      }

      /* Prior op must be a modify.in same ccb. Use latest afim for safe-read.
       */
      TRACE_7("Safe read of Object '%s' that was modified in same ccb %u",
              objectName.c_str(), req->ccbId);
      /* Find mutation */
      ObjectMutationMap::iterator omuti = ccb->mMutations.find(objectName);
      osafassert(omuti != ccb->mMutations.end());
      ObjectMutation* oMut = omuti->second;
      osafassert(oMut->mOpType == IMM_MODIFY);
      obj = oMut->mAfterImage;
    }
  }  // if(safeRead)

  // Validate scope
  if (scope != SA_IMM_ONE) {
    LOG_NO("ERR_INVALID_PARAM: invalid search scope");
    err = SA_AIS_ERR_INVALID_PARAM;
    goto accessorExit;
  }

  // Validate searchOptions

  notAllowedOptions =
      searchOptions & ~(SA_IMM_SEARCH_ONE_ATTR | SA_IMM_SEARCH_GET_ALL_ATTR |
                        SA_IMM_SEARCH_GET_SOME_ATTR |
                        SA_IMM_SEARCH_GET_CONFIG_ATTR | SA_IMM_SEARCH_NO_RDN);

  if (notAllowedOptions) {
    LOG_WA("ERR_LIBRARY: Invalid search criteria - library problem ?");
    err = SA_AIS_ERR_LIBRARY;
    goto accessorExit;
  }

  // TODO: Reverse the order of matching attribute names.
  // The class should be searched first since we must return error
  // if the attribute is not defined in the class of the object

  // SafeRead works exactly like accessor get with respect to runtime
  // attributes.

  if (!obj) {
    osafassert(!isSafeRead);  // SafeRead checks bypassed somehow above.
    obj = i->second;
  }

  if (obj->mObjFlags & IMM_DN_INTERNAL_REP) {
    nameToExternal(objectName);
  }
  op.addObject(objectName);
  for (j = obj->mAttrValueMap.begin(); j != obj->mAttrValueMap.end(); ++j) {
    if (searchOptions & SA_IMM_SEARCH_GET_SOME_ATTR) {
      bool notFound = true;
      ImmsvAttrNameList* list = req->attributeNames;

      if (!soughtAttributes) {
        while (list) {
          ++soughtAttributes;
          list = list->next;
        }
      }

      list = req->attributeNames;
      while (list) {
        size_t sz = strnlen((char*)list->name.buf, (size_t)list->name.size);
        std::string attName((const char*)list->name.buf, sz);
        if (j->first == attName) {
          notFound = false;
          ++matchedAttributes;
          break;
        }
        list = list->next;
      }  // while(list)
      if (notFound) {
        continue;
      }
    }
    AttrMap::iterator k = obj->mClassInfo->mAttrMap.find(j->first);
    osafassert(k != obj->mClassInfo->mAttrMap.end());

    if ((searchOptions & SA_IMM_SEARCH_NO_RDN) &&
        (k->second->mFlags & SA_IMM_ATTR_RDN)) {
      continue;
    }

    if ((searchOptions & SA_IMM_SEARCH_GET_CONFIG_ATTR) &&
        (k->second->mFlags & SA_IMM_ATTR_RUNTIME)) {
      continue;
    }

    op.addAttribute(j->first, k->second->mValueType, k->second->mFlags);
    // Check if attribute is the implementername attribute.
    // If so add the value artificially
    if (j->first == std::string(SA_IMM_ATTR_IMPLEMENTER_NAME)) {
      if (obj->mImplementer) {
        j->second->setValueC_str(obj->mImplementer->mImplementerName.c_str());
      }  // else if(!j->second->empty()){
      // Discard obsolete implementer name
      // j->second->setValueC_str(NULL);
      // BUGFIX 081112
      //}
    }
    if (k->second->mFlags & SA_IMM_ATTR_CONFIG) {
      if (!j->second->empty()) {
        // Config attributes always accessible
        op.addAttrValue(*j->second);
        checkAttribute = true;
      } else {
        checkAttribute = false;
      }
    } else {  // Runtime attributes
      if (obj->mImplementer && obj->mImplementer->mNodeId) {
        // There is an implementer
        if (!(k->second->mFlags & SA_IMM_ATTR_CACHED && j->second->empty())) {
          op.addAttrValue(*j->second);
          // If the attribute is cached and empty then there
          // is truly no value and we are not supposed to fetch
          // any. Thus we get here only if we are not cached
          // or if we are not empty.
          // This means we can be empty and non cached.
          // Should we really add a value then ? Yes because
          // we are to fetch one from the implementer.
        }

        if (implNotSet && !(k->second->mFlags & SA_IMM_ATTR_CACHED)) {
          // Non-cached rtattr and implementer exists => fetch it
          op.setImplementer(obj->mImplementer);
          implNotSet = false;
        }

        checkAttribute = true;
      } else {
        // There is no implementer
        iem = sImplDetachTime.find(obj->mImplementer);
        if ((k->second->mFlags & SA_IMM_ATTR_PERSISTENT) &&
            !(j->second->empty())) {
          op.addAttrValue(*j->second);
          checkAttribute = true;
          // Persistent rt attributes still accessible
          // If they have been given any value
        } else if ((k->second->mFlags & SA_IMM_ATTR_CACHED) &&
                   (iem != sImplDetachTime.end()) && !(j->second->empty())) {
          // The attribute is cached and the OI is transiently detached
          op.addAttrValue(*j->second);
          checkAttribute = true;
        } else {
          checkAttribute = false;
        }
        // No implementer and the rt attribute is not persistent
        // then attribute name, but no value is to be returned.
        // Se spec p 42.
      }  // No-impl
    }    // Rt-attr

    if (nonExtendedNameCheck && checkAttribute &&
        k->second->mValueType == SA_IMM_ATTR_SANAMET) {
      if (!j->second->empty() &&
          strlen(j->second->getValueC_str()) >= SA_MAX_UNEXTENDED_NAME_LENGTH) {
        TRACE("SEARCH_NON_EXTENDED_NAMES filter: %s has long DN value: %s",
              j->first.c_str(), j->second->getValueC_str());
        err = SA_AIS_ERR_NAME_TOO_LONG;
        goto accessorExit;
      }

      if (j->second->isMultiValued()) {
        ImmAttrMultiValue* multi =
            ((ImmAttrMultiValue*)j->second)->getNextAttrValue();
        while (multi) {
          if (!multi->empty() &&
              strlen(multi->getValueC_str()) >= SA_MAX_UNEXTENDED_NAME_LENGTH) {
            TRACE("SEARCH_NON_EXTENDED_NAMES filter: %s has long DN value",
                  j->first.c_str());
            err = SA_AIS_ERR_NAME_TOO_LONG;
            goto accessorExit;
          }
          multi = multi->getNextAttrValue();
        }
      }
    }
  }  // for

  if ((searchOptions & SA_IMM_SEARCH_GET_SOME_ATTR) &&
      (matchedAttributes != soughtAttributes)) {
    LOG_NO(
        "ERR_NOT_EXIST: Some attributeNames did not exist in Object '%s' "
        "(nrof names:%u matched:%u)",
        objectName.c_str(), soughtAttributes, matchedAttributes);
    err = SA_AIS_ERR_NOT_EXIST;
  }

accessorExit:
  TRACE_LEAVE();
  return err;
}

bool ImmModel::filterMatch(ObjectInfo* obj, ImmsvOmSearchOneAttr* filter,
                           SaAisErrorT& err, const char* objName) {
  // TRACE_ENTER();
  std::string attrName((const char*)filter->attrName.buf);
  //(size_t) filter->attrName.size);

  // First check in the class of the object if the attribute should exist.
  AttrMap::iterator k = obj->mClassInfo->mAttrMap.find(attrName);
  if (k == obj->mClassInfo->mAttrMap.end()) {
    // TRACE_LEAVE();
    return false;
  }

  // Attribute exists in object.
  // If there is no value to match, then the filter is only on attribute name
  // Note that "no value" is encoded as zero sized string for all types

  // TODO: This branch really needs component testing!!!
  // All cases of: No value, string value, other type of value
  // Problem: This representation of null value eliminates possibility to
  // match on the zero length string!! Conclusion: Need different rep for
  // filter. OR: Is it even posible to assign a zero sized string value ?
  // The shortest possible string should have length 1.
  if (filter->attrValueType == SA_IMM_ATTR_SASTRINGT &&
      !filter->attrValue.val.x.size) {
    // TRACE_LEAVE();
    return true;
  }

  // There is a value to match => Check that the types match.
  if (k->second->mValueType != (unsigned int)filter->attrValueType) {
    // TRACE_LEAVE();
    return false;
  }

  if ((k->second->mFlags & SA_IMM_ATTR_RUNTIME) &&
      !(k->second->mFlags & SA_IMM_ATTR_CACHED)) {
    // TODO ABT We dont yet handle the case of attribute matching on
    // non-cached runtime attributes. => Warn for this case.
    // To implement this, we should return true here, then fetch the
    // attribute as part of getNext, then check if the fetched attribute
    // does match.
    // If it does not match then simply discard the search object.
    LOG_WA(
        "ERR_NO_RESOURCES: Attribute %s is a non-cached runtime "
        "attribute in object %s => can not handle search with match "
        "on such attributes currently.",
        attrName.c_str(), objName);
    err = SA_AIS_ERR_NO_RESOURCES;
    // TRACE_LEAVE();
    return false;
  }

  // Finally, check that the values match.
  ImmAttrValueMap::iterator j = obj->mAttrValueMap.find(attrName);
  osafassert(j != obj->mAttrValueMap.end());

  // If the attribute is the implementer name attribute, then assign it now.
  if ((j->first == std::string(SA_IMM_ATTR_IMPLEMENTER_NAME)) &&
      obj->mImplementer) {
    j->second->setValueC_str(obj->mImplementer->mImplementerName.c_str());
  }

  if (j->second->empty()) {
    // TRACE_LEAVE();
    return false;
  }

  // Convert match-value to octet string
  IMMSV_OCTET_STRING tmpos;
  eduAtValToOs(&tmpos, &(filter->attrValue),
               (SaImmValueTypeT)filter->attrValueType);

  // TRACE_LEAVE();
  return j->second->hasMatchingValue(tmpos);
}

bool ImmModel::noDanglingRefExist(ObjectInfo* obj, const char* noDanglingRef) {
  ImmAttrValueMap::iterator avmi;
  ImmAttrValue* av;
  AttrMap::iterator ami = obj->mClassInfo->mAttrMap.begin();
  for (; ami != obj->mClassInfo->mAttrMap.end(); ++ami) {
    if (ami->second->mFlags & SA_IMM_ATTR_NO_DANGLING) {
      avmi = obj->mAttrValueMap.find(ami->first);
      osafassert(avmi != obj->mAttrValueMap.end());
      av = avmi->second;
      while (av) {
        if (av->getValueC_str() &&
            !strcmp(av->getValueC_str(), noDanglingRef)) {
          return true;
        }

        if (av->isMultiValued()) {
          av = ((ImmAttrMultiValue*)av)->getNextAttrValue();
        } else {
          break;
        }
      }
    }
  }

  return false;
}

SaAisErrorT ImmModel::searchInitialize(ImmsvOmSearchInit* req,
                                       ImmSearchOp& op) {
  TRACE_ENTER();

  SaAisErrorT err = SA_AIS_OK;
  bool filter = false;
  bool isDumper = false;
  bool isSyncer = false;
  bool nonExtendedNameCheck =
      req->searchParam.present > ImmOmSearchParameter_PR_oneAttrParam;
  ClassInfo* classInfo = NULL;
  SaImmScopeT scope = (SaImmScopeT)req->scope;
  std::string objectName;
  ObjectInfo* obj = NULL;
  ObjectMap::iterator omi;
  ObjectSet::iterator osi;
  ImplementerEvtMap::iterator iem;
  bool noDanglingSearch =
      req->searchOptions & SA_IMM_SEARCH_NO_DANGLING_DEPENDENTS;
  ObjectInfo* refObj = NULL;
  ObjectMMap::iterator ommi = sReverseRefsNoDanglingMMap.end();
  std::string refObjectName;
  SaUint32T childCount = 0;

  if (nonExtendedNameCheck) {
    op.setNonExtendedName();
  }

  size_t sz = strnlen((char*)req->rootName.buf, (size_t)req->rootName.size);
  std::string rootName((const char*)req->rootName.buf, sz);
  const size_t rootlen = rootName.length();

  SaImmSearchOptionsT searchOptions = (SaImmSearchOptionsT)req->searchOptions;

  SaImmSearchOptionsT unknownOptions =
      searchOptions &
      ~(SA_IMM_SEARCH_ONE_ATTR | SA_IMM_SEARCH_GET_ALL_ATTR |
        SA_IMM_SEARCH_GET_NO_ATTR | SA_IMM_SEARCH_GET_SOME_ATTR |
        SA_IMM_SEARCH_GET_CONFIG_ATTR | SA_IMM_SEARCH_PERSISTENT_ATTRS |
        SA_IMM_SEARCH_SYNC_CACHED_ATTRS | SA_IMM_SEARCH_NO_DANGLING_DEPENDENTS |
        SA_IMM_SEARCH_NO_RDN);

  if (unknownOptions) {
    LOG_NO("ERR_INVALID_PARAM: invalid search option 0x%llx", unknownOptions);
    err = SA_AIS_ERR_INVALID_PARAM;
    goto searchInitializeExit;
  }

  if (req->ccbId) {
    LOG_WA("SafeRead ended up in ImmModel::searchInitialize ccb:%u",
           req->ccbId);
    err = SA_AIS_ERR_LIBRARY;
    goto searchInitializeExit;
  }

  // Validate root name
  if (!(nameCheck(rootName) || nameToInternal(rootName))) {
    LOG_NO("ERR_INVALID_PARAM: Not a proper root name");
    err = SA_AIS_ERR_INVALID_PARAM;
    goto searchInitializeExit;
  }

  if (rootlen > 0) {
    omi = sObjectMap.find(rootName);
    if (omi == sObjectMap.end()) {
      TRACE_7("ERR_NOT_EXIST: root object '%s' does not exist",
              rootName.c_str());
      err = SA_AIS_ERR_NOT_EXIST;
      goto searchInitializeExit;
    } else if (omi->second->mObjFlags & IMM_CREATE_LOCK) {
      TRACE_7(
          "ERR_NOT_EXIST: Root object '%s' is being created, "
          "but ccb or PRTO PBE not yet applied",
          rootName.c_str());
      err = SA_AIS_ERR_NOT_EXIST;
      goto searchInitializeExit;
    }
    childCount = omi->second->mChildCount + 1; /* Add one for root itself */
  } else {
    childCount = 0xffffffff;
  }

  // Validate scope
  if ((scope != SA_IMM_SUBLEVEL) && (scope != SA_IMM_SUBTREE)) {
    LOG_NO("ERR_INVALID_PARAM: invalid search scope");
    err = SA_AIS_ERR_INVALID_PARAM;
    goto searchInitializeExit;
  }

  filter = (req->searchParam.choice.oneAttrParam.attrName.size != 0);

  // Validate searchOptions
  if (filter && ((searchOptions & SA_IMM_SEARCH_ONE_ATTR) == 0)) {
    LOG_NO(
        "ERR_INVALID_PARAM: The SA_IMM_SEARCH_ONE_ATTR flag "
        "must be set in the searchOptions parameter");
    err = SA_AIS_ERR_INVALID_PARAM;
    goto searchInitializeExit;
  }

  isDumper = (searchOptions & SA_IMM_SEARCH_PERSISTENT_ATTRS);
  isSyncer = (searchOptions & SA_IMM_SEARCH_SYNC_CACHED_ATTRS);

  if (filter &&
      (strcmp(req->searchParam.choice.oneAttrParam.attrName.buf,
              SA_IMM_ATTR_CLASS_NAME) == 0) &&
      req->searchParam.choice.oneAttrParam.attrValue.val.x.size) {
    /* Check if class exists and that all requested attrs exist in class */
    SaImmClassNameT className =
        (SaImmClassNameT)
            req->searchParam.choice.oneAttrParam.attrValue.val.x.buf;
    TRACE("Class extent search for class:%s", className);
    ClassMap::iterator ci = sClassMap.find(className);
    if (ci == sClassMap.end() || ci->second->mExtent.empty()) {
      /* One could think we should return ERR_NOT_EXIST here, but no,
         that is not acording to the standard. Searching for instances
         of a class when that class is not currently installed, or is
         installed but empty, is still a valid search, only that it
         returns the empty set. The NOT_EXIST will be returned by the
         first saImmOmSearchNext_2().
      */
      TRACE("Extent for class:%s was empty, or class does not exist",
            className);
      osafassert(err == SA_AIS_OK);
      goto searchInitializeExit;
    }

    classInfo = ci->second;

    TRACE("class:%s extent has size %u", className,
          (unsigned int)classInfo->mExtent.size());

    if (searchOptions & SA_IMM_SEARCH_GET_SOME_ATTR) {
      /* Explicit attributes requested, check that they exist in class. */
      bool someAttrsNotFound = false;
      ImmsvAttrNameList* list = req->attributeNames;
      while (list) {
        size_t sz = strnlen((char*)list->name.buf, (size_t)list->name.size);
        std::string attName((const char*)list->name.buf, sz);
        AttrMap::iterator ai = classInfo->mAttrMap.find(attName);
        if (ai == classInfo->mAttrMap.end()) {
          LOG_NO(
              "SearchInit ERR_INVALID_PARAM: attribute %s does not exist "
              "in class %s",
              attName.c_str(), className);
          someAttrsNotFound = true;
        }
        list = list->next;
      }
      if (someAttrsNotFound) {
        err = SA_AIS_ERR_INVALID_PARAM;
        goto searchInitializeExit;
      }
    }

    /* Class extent filter is handled by changing iteration source to class
     * extent */
    filter = false;
    osi = classInfo->mExtent.begin();
    osafassert(osi !=
               classInfo->mExtent.end()); /* Already checked for empty above */
    if (isSyncer) {
      /* Avoid copy iterator for sync. Use a raw STL iterator over extent. */
      op.syncOsi = new ObjectSet::iterator(osi);
      op.attrNameList = req->attributeNames;
      req->attributeNames = NULL;
      op.classInfo = classInfo;
      osafassert(scope == SA_IMM_SUBTREE);
      osafassert(rootlen == 0);
      osafassert(searchOptions & SA_IMM_SEARCH_GET_SOME_ATTR);
      goto searchInitializeExit;
    }
    obj = *(osi);
    omi = sObjectMap.end(); /* Possibly did point to parent*/
    getObjectName(obj, objectName);
    if (obj->mObjFlags & IMM_DN_INTERNAL_REP) {
      /* DN is internal rep => regenerated name needs to be the same,
         since it is used for matching with internal DNs.
      */
      osafassert(nameToInternal(objectName));
    }
  } else if (noDanglingSearch) {
    // Validate parameters for searching no dangling references
    if (req->searchParam.choice.oneAttrParam.attrName.size != 0) {
      LOG_NO(
          "ERR_INVALID_PARAM: attrName must be NULL "
          "when SA_IMM_SEARCH_NO_DANGLING_DEPENDENTS flag is set");
      err = SA_AIS_ERR_INVALID_PARAM;
      goto searchInitializeExit;
    }
    if (req->searchParam.choice.oneAttrParam.attrValueType !=
            SA_IMM_ATTR_SANAMET &&
        req->searchParam.choice.oneAttrParam.attrValueType !=
            SA_IMM_ATTR_SASTRINGT) {
      LOG_NO(
          "ERR_INVALID_PARAM: attrValueType must be type of SaNameT or SaStringT "
          "when SA_IMM_SEARCH_NO_DANGLING_DEPENDENTS flag is set");
      err = SA_AIS_ERR_INVALID_PARAM;
      goto searchInitializeExit;
    }

    refObjectName = std::string(
        req->searchParam.choice.oneAttrParam.attrValue.val.x.buf,
        strnlen(req->searchParam.choice.oneAttrParam.attrValue.val.x.buf,
                req->searchParam.choice.oneAttrParam.attrValue.val.x.size));

    omi = sObjectMap.find(refObjectName);
    if (omi == sObjectMap.end() || (omi->second->mObjFlags & IMM_CREATE_LOCK)) {
      LOG_NO(
          "ERR_INVALID_PARAM: attrValue contains a DN of non-existing object %s",
          refObjectName.c_str());
      err = SA_AIS_ERR_INVALID_PARAM;
      goto searchInitializeExit;
    }

    // Set initialize variables
    refObj = omi->second;
    omi = sObjectMap.end();

    ommi = sReverseRefsNoDanglingMMap.find(refObj);
    while (ommi != sReverseRefsNoDanglingMMap.end() && ommi->first == refObj &&
           !noDanglingRefExist(ommi->second, refObjectName.c_str())) {
      ++ommi;
    }

    if (ommi != sReverseRefsNoDanglingMMap.end() && ommi->first == refObj) {
      obj = ommi->second;
      getObjectName(obj, objectName);
    } else {
      // There is no any match
      goto searchInitializeExit;
    }
  } else {
    if (childCount > 1) {
      /* A root was provided and it has children => Initialize */
      /* iteration for regular object-map as source */
      omi = sObjectMap.begin();
      osafassert(omi != sObjectMap.end()); /* sObjectMap can never be empty! */
    } else {
      TRACE("Singleton match");
      assert(childCount == 1);
      /* A root was provided but it has NO children => Keep cursor
         pointing to the root. Childcount == 1 ensures iteration
         will terminate as soon as root has been processed.
       */
    }
    obj = omi->second;
    objectName = omi->first;
  }

  // Find root object and all sub objects to the root object.
  // Source set is either (a) entire object-map or (b) class extent set
  // or (c) set of no-dangling dependents on refObj
  while (err == SA_AIS_OK && (omi != sObjectMap.end() ||
                              (classInfo && osi != classInfo->mExtent.end()) ||
                              (ommi != sReverseRefsNoDanglingMMap.end() &&
                               ommi->first == refObj))) {
    /*Skip pending creates.*/
    if (obj->mObjFlags & IMM_CREATE_LOCK) {
      goto continue_while_loop;
    }

    if (objectName.length() >= rootlen) {
      size_t pos = objectName.length() - rootlen;
      if ((objectName.rfind(rootName, pos) == pos) &&
          (!pos         // Object IS the current root
           || !rootlen  // Empty root => all objects are sub-root.
           || (objectName[pos - 1] == ',')  // Root with subobject
           )) {
        if (nonExtendedNameCheck &&
            objectName.length() >= SA_MAX_UNEXTENDED_NAME_LENGTH) {
          TRACE("SEARCH_NON_EXTENDED_NAMES filter: Object name is too long: %s",
                objectName.c_str());
          err = SA_AIS_ERR_NAME_TOO_LONG;
          goto searchInitializeExit;
        }

        if (scope == SA_IMM_SUBTREE || checkSubLevel(objectName, pos)) {
          // Before adding the object to the result, check if there
          // is any attribute match filter. If so check if there is a
          // match.
          osafassert(childCount);
          --childCount;

          if (filter && !filterMatch(obj,
                                     (ImmsvOmSearchOneAttr*)&(
                                         req->searchParam.choice.oneAttrParam),
                                     err, objectName.c_str())) {
            goto continue_while_loop;
          }
          if (obj->mObjFlags & IMM_DN_INTERNAL_REP) {
            nameToExternal(objectName);
          }
          /*TRACE_7("Add object:%s flags:%u", objectName.c_str(),
           * obj->mObjFlags);*/
          op.addObject(objectName);

          if (searchOptions &
              (SA_IMM_SEARCH_GET_ALL_ATTR | SA_IMM_SEARCH_GET_SOME_ATTR |
               SA_IMM_SEARCH_GET_CONFIG_ATTR)) {
            ImmAttrValueMap::iterator j;
            bool implNotSet = true;
            bool checkAttribute = false;
            for (j = obj->mAttrValueMap.begin(); j != obj->mAttrValueMap.end();
                 ++j) {
              if (searchOptions & SA_IMM_SEARCH_GET_SOME_ATTR) {
                bool notFound = true;
                ImmsvAttrNameList* list = req->attributeNames;
                while (list) {
                  size_t sz =
                      strnlen((char*)list->name.buf, (size_t)list->name.size);
                  std::string attName((const char*)list->name.buf, sz);
                  if (j->first == attName) {
                    notFound = false;
                    break;
                  }
                  list = list->next;
                }  // while(list)
                if (notFound) {
                  continue;
                } /* for loop */
              }
              AttrMap::iterator k = obj->mClassInfo->mAttrMap.find(j->first);
              osafassert(k != obj->mClassInfo->mAttrMap.end());

              if ((searchOptions & SA_IMM_SEARCH_NO_RDN) &&
                  (k->second->mFlags & SA_IMM_ATTR_RDN)) {
                /* NO_RDN flag is set. RDN will be excluded from the result */
                continue;
              }

              if ((searchOptions & SA_IMM_SEARCH_GET_CONFIG_ATTR) &&
                  (k->second->mFlags & SA_IMM_ATTR_RUNTIME)) {
                TRACE("SEARCH_GET_CONFIG_ATTR filtered RTA:%s from obj:%s",
                      j->first.c_str(), objectName.c_str());
                continue;
              }

              if (isDumper && !(k->second->mFlags & SA_IMM_ATTR_CONFIG) &&
                  !(k->second->mFlags & SA_IMM_ATTR_PERSISTENT)) {
                continue; /* for loop */
                          // If we are the dumper, then exclude
                // non-persistent rt attributes, even when there
                // is an implementer. For normal applications,
                // the above if evaluates to false, because
                // isDumper is false.
              }

              op.addAttribute(j->first, k->second->mValueType,
                              k->second->mFlags);
              // Check if attribute is the implementername
              // attribute. if so add the value artificially
              if (j->first == std::string(SA_IMM_ATTR_IMPLEMENTER_NAME)) {
                if (obj->mImplementer) {
                  j->second->setValueC_str(
                      obj->mImplementer->mImplementerName.c_str());
                }  // else if(!j->second->empty()) {
                // Discard obsolete implementer name
                // j->second->setValueC_str(NULL);
                // BUGFIX 081112
                //}
              }

              if (k->second->mFlags & SA_IMM_ATTR_CONFIG) {
                if (!j->second->empty()) {
                  // Config attributes always accessible
                  op.addAttrValue(*j->second);
                  checkAttribute = true;
                } else {
                  checkAttribute = false;
                }
              } else {  // Runtime attributes
                if (obj->mImplementer && obj->mImplementer->mNodeId) {
                  // There is an implementer
                  if (!(k->second->mFlags & SA_IMM_ATTR_CACHED &&
                        j->second->empty())) {
                    op.addAttrValue(*j->second);
                    // If the attribute is cached and empty
                    // then there is truly no value and we
                    // are not supposed to fetch any. Thus
                    // we get here only if we are not cached
                    // or if we are not empty.
                    // This means we can be empty and non
                    // cached. Should we really add a value
                    // then ? Yes because we are to fetch
                    // one from the implementer.
                  }

                  if (implNotSet && !(k->second->mFlags & SA_IMM_ATTR_CACHED)) {
                    // Non cached rtattr and implementer
                    // exists => fetch it
                    op.setImplementer(obj->mImplementer);
                    implNotSet = false;
                  }

                  checkAttribute = true;
                } else {
                  // There is no implementer
                  iem = sImplDetachTime.find(obj->mImplementer);
                  if (!(j->second->empty()) &&
                      (k->second->mFlags & SA_IMM_ATTR_PERSISTENT ||
                       (isSyncer && k->second->mFlags & SA_IMM_ATTR_CACHED))) {
                    // There is a value AND (
                    // the attribute is persistent OR
                    // this is a syncer and the attribute is
                    // cached. The special case for the
                    // sync'er is to ensure that we still
                    // sync cached values even when there
                    // is no implementer currently.
                    op.addAttrValue(*j->second);
                    checkAttribute = true;
                  } else if ((k->second->mFlags & SA_IMM_ATTR_CACHED) &&
                             (iem != sImplDetachTime.end()) &&
                             !(j->second->empty())) {
                    // The attribute is cached and the OI is transiently
                    // detached
                    op.addAttrValue(*j->second);
                    checkAttribute = true;
                  } else {
                    checkAttribute = false;
                  }
                  // No implementer and the rt attribute is
                  // not persistent, then attribute name, but
                  // no value is to be returned.
                  // Se spec p 42.
                }  // No-imp
              }    // Runtime

              if (nonExtendedNameCheck && checkAttribute &&
                  k->second->mValueType == SA_IMM_ATTR_SANAMET) {
                if (!j->second->empty() && strlen(j->second->getValueC_str()) >=
                                               SA_MAX_UNEXTENDED_NAME_LENGTH) {
                  TRACE(
                      "SEARCH_NON_EXTENDED_NAMES filter: %s has long DN value",
                      j->first.c_str());
                  err = SA_AIS_ERR_NAME_TOO_LONG;
                  goto searchInitializeExit;
                }

                if (j->second->isMultiValued()) {
                  ImmAttrMultiValue* multi =
                      ((ImmAttrMultiValue*)j->second)->getNextAttrValue();
                  while (multi) {
                    if (!multi->empty() && strlen(multi->getValueC_str()) >=
                                               SA_MAX_UNEXTENDED_NAME_LENGTH) {
                      TRACE(
                          "SEARCH_NON_EXTENDED_NAMES filter: %s has long DN value",
                          j->first.c_str());
                      err = SA_AIS_ERR_NAME_TOO_LONG;
                      goto searchInitializeExit;
                    }
                    multi = multi->getNextAttrValue();
                  }
                }
              }
            }  // for(..
          }
        }
      }
    }

  continue_while_loop:
    obj = NULL;
    if (!childCount) { /* We have found all the children of the root. */
      TRACE("SearchInit has found all the children of the root");
      if ((++omi) != sObjectMap.end()) {
        TRACE("Cutoff in search loop by childCount");
      }
      break;
    }
    if (classInfo) {
      ++osi;
      if (osi != classInfo->mExtent.end()) {
        obj = (*osi);
        objectName.clear();
        getObjectName(obj, objectName);
        if (obj->mObjFlags & IMM_DN_INTERNAL_REP) {
          /* DN is internal rep => regenerated name needs to be the same.
             since it is used for matching with internal DNs.
          */
          osafassert(nameToInternal(objectName));
        }
      }
    } else if (noDanglingSearch) {
      ++ommi;
      while (ommi != sReverseRefsNoDanglingMMap.end() &&
             ommi->first == refObj) {
        if (noDanglingRefExist(ommi->second, refObjectName.c_str())) {
          obj = ommi->second;
          objectName.clear();
          getObjectName(obj, objectName);
          break;
        }
        ++ommi;
      }
    } else {
      ++omi;
      if (omi != sObjectMap.end()) {
        obj = omi->second;
        objectName = omi->first;
      }
    }
  }  // while

searchInitializeExit:
  TRACE_LEAVE();
  return err;
}

SaAisErrorT ImmModel::nextSyncResult(ImmsvOmRspSearchNext** rsp,
                                     ImmSearchOp& op) {
  TRACE_ENTER();
  SaAisErrorT err = SA_AIS_OK;
  ObjectSet::iterator* osip = (ObjectSet::iterator*)op.syncOsi;
  if (!osip) {
    return SA_AIS_ERR_NOT_EXIST;
  }
  ImmsvAttrNameList* theAttList = (ImmsvAttrNameList*)op.attrNameList;
  ClassInfo* classInfo = reinterpret_cast<ClassInfo*>(op.classInfo);
  osafassert(classInfo != NULL);
  ObjectInfo* obj = NULL;
  std::string objectName;
  ImmAttrValueMap::iterator j;
  IMMSV_OM_RSP_SEARCH_NEXT* p = NULL;

  if ((*osip) == classInfo->mExtent.end()) {
    delete osip;
    op.syncOsi = NULL;
    immsv_evt_free_attrNames(theAttList);
    op.attrNameList = NULL;
    op.classInfo = NULL;
    err = SA_AIS_ERR_NOT_EXIST;
    goto done;
  }

  obj = *(*osip);
  osafassert(obj);
  osafassert(obj->mClassInfo == classInfo);
  getObjectName(obj, objectName); /* In external form. */

  osafassert(!(obj->mObjFlags & IMM_CREATE_LOCK));
  osafassert(!(obj->mObjFlags & IMM_DELETE_LOCK));
  if (obj->mObjFlags & IMM_RT_UPDATE_LOCK) {
    LOG_NO(
        "ERR_NO_RESOURCES: Sync was aborted due to interference with blocked PRTA update");
    err = SA_AIS_ERR_NO_RESOURCES;
    goto done;
  }

  /* (1) op.addObject(objectName);*/

  p = (IMMSV_OM_RSP_SEARCH_NEXT*)calloc(1, sizeof(IMMSV_OM_RSP_SEARCH_NEXT));
  osafassert(p);
  *rsp = p;

  // Get object name
  p->objectName.size = (int)objectName.length() + 1;
  p->objectName.buf = strdup(objectName.c_str());
  p->attrValuesList = NULL;

  for (j = obj->mAttrValueMap.begin(); j != obj->mAttrValueMap.end(); ++j) {
    /*SA_IMM_SEARCH_GET_SOME_ATTR must have been set since this is sync. */
    IMMSV_ATTR_VALUES_LIST* attrl = NULL;
    IMMSV_ATTR_VALUES* attr = NULL;
    ImmsvAttrNameList* list = theAttList;

    while (list) {
      size_t sz = strnlen((char*)list->name.buf, (size_t)list->name.size);
      std::string attName((const char*)list->name.buf, sz);
      if (j->first == attName) {
        break;
      }
      list = list->next;
    }  // while(list)
    if (!list) {
      continue;
    }  // for loop */

    attrl = (IMMSV_ATTR_VALUES_LIST*)calloc(1, sizeof(IMMSV_ATTR_VALUES_LIST));
    attr = &(attrl->n);

    AttrMap::iterator k = classInfo->mAttrMap.find(j->first);
    osafassert(k != classInfo->mAttrMap.end());
    // op.addAttribute(j->first, k->second->mValueType, k->second->mFlags);
    attr->attrName.size = (SaUint32T)j->first.length() + 1;
    attr->attrName.buf = strdup(j->first.c_str());
    attr->attrValueType = k->second->mValueType;

    // Check if attribute is the implementername
    // attribute. if so add the value artificially
    if ((j->first == std::string(SA_IMM_ATTR_IMPLEMENTER_NAME)) &&
        (obj->mImplementer)) {
      j->second->setValueC_str(obj->mImplementer->mImplementerName.c_str());
    }

    if ((!j->second->empty()) && ((k->second->mFlags & SA_IMM_ATTR_CONFIG) ||
                                  (k->second->mFlags & SA_IMM_ATTR_CACHED))) {
      // Config attributes always synced
      // Runtime attributes synced if they are cached
      // op.addAttrValue(*j->second);
      attr->attrValuesNumber = j->second->extraValues() + 1;
      j->second->copyValueToEdu(&(attr->attrValue),
                                (SaImmValueTypeT)attr->attrValueType);

      if (attr->attrValuesNumber > 1) {
        osafassert(j->second->isMultiValued());
        ((ImmAttrMultiValue*)j->second)
            ->copyExtraValuesToEdu(&(attr->attrMoreValues),
                                   (SaImmValueTypeT)attr->attrValueType);
      }
    }
    attrl->next = p->attrValuesList;
    p->attrValuesList = attrl;
  }

  (*osip)++;

done:
  TRACE_LEAVE();
  return err;
}

/**
 * Administrative oepration invoke.
 */
SaAisErrorT ImmModel::adminOperationInvoke(
    const ImmsvOmAdminOperationInvoke* req, SaUint32T reqConn,
    SaUint64T reply_dest, SaInvocationT& saInv, SaUint32T* implConn,
    unsigned int* implNodeId, bool pbeExpected, bool* displayRes,
    bool isAtCoord) {
  TRACE_ENTER();
  SaAisErrorT err = SA_AIS_OK;

  size_t sz = strnlen((char*)req->objectName.buf, (size_t)req->objectName.size);
  std::string objectName((const char*)req->objectName.buf, sz);

  TRACE_5("Admin op on objectName:%s\n", objectName.c_str());

  SaUint32T adminOwnerId = req->adminOwnerId;

  AdminOwnerInfo* adminOwner = 0;
  ObjectInfo* object = 0;
  struct ObjectInfo
      dummyObject; /* Constructor zeroes members of stack allocated object */

  ImplementerEvtMap::iterator iem;
  AdminOwnerVector::iterator i2;
  ObjectMap::iterator oi;
  std::string objAdminOwnerName;
  CcbVector::iterator i3;
  SaUint32T ccbIdOfObj = 0;
  bool admoMissmatch = false;
  bool ccbBusy = false;

  if (!(nameCheck(objectName) || nameToInternal(objectName))) {
    LOG_NO("ERR_INVALID_PARAM: Not a proper object name");
    err = SA_AIS_ERR_INVALID_PARAM;
    goto done;
  }

  i2 = std::find_if(sOwnerVector.begin(), sOwnerVector.end(),
                    IdIs(adminOwnerId));
  if ((i2 != sOwnerVector.end()) && (*i2)->mDying) {
    i2 = sOwnerVector.end();  // Pretend that it does not exist.
    TRACE_5("Attempt to use dead admin owner in adminOperationInvoke");
  }
  if (i2 == sOwnerVector.end()) {
    if (sImmNodeState > IMM_NODE_UNKNOWN) {
      LOG_NO("ERR_BAD_HANDLE: admin owner id %u does not exist", adminOwnerId);
    }
    err = SA_AIS_ERR_BAD_HANDLE;
    goto done;
  }
  adminOwner = *i2;

  if (objectName.empty()) {
    LOG_NO("ERR_INVALID_PARAM: Empty DN attribute value");
    err = SA_AIS_ERR_INVALID_PARAM;
    goto done;
  }

  oi = sObjectMap.find(objectName);
  if (oi == sObjectMap.end()) {
    /*ESCAPE SPECIAL CASE FOR HANDLING PRELOAD. */
    if ((sImmNodeState == IMM_NODE_UNKNOWN) && (objectName == immObjectDn) &&
        (req->operationId == OPENSAF_IMM_2PBE_PRELOAD_STAT)) {
      TRACE_7("Bouncing special preload admin-op");
      err = SA_AIS_ERR_REPAIR_PENDING;
    } else {
      dummyObject.mImplementer = findImplementer(objectName);
      if (dummyObject.mImplementer) {
        /* Appears to be an admin-op directed at OI.
           Verify that admo-name matches impl-name. */
        if (objectName == adminOwner->mAdminOwnerName) {
          if (this->protocol45Allowed()) {
            object = &dummyObject;
            goto fake_obj;
          } else {
            LOG_NO(
                "ERR_NOT_EXIST: Admin-op on OI rejected. Protocol4.5 not allowed");
          }
        } else {
          LOG_NO(
              "ERR_NOT_EXIST: Admin-op on OI rejected. Implementer '%s' != adminowner '%s'",
              objectName.c_str(), adminOwner->mAdminOwnerName.c_str());
        }
      }

      TRACE_7("ERR_NOT_EXIST: object '%s' does not exist", objectName.c_str());
      err = SA_AIS_ERR_NOT_EXIST;
    }
    goto done;
  }

  object = oi->second;
  object->getAdminOwnerName(&objAdminOwnerName);

  if (objAdminOwnerName != adminOwner->mAdminOwnerName) {
    /* Let implementer errors overide this error. */
    admoMissmatch = true;
  }

  ccbIdOfObj = object->mCcbId;
  if (ccbIdOfObj) {  // check for ccb interference
    i3 =
        std::find_if(sCcbVector.begin(), sCcbVector.end(), CcbIdIs(ccbIdOfObj));
    if (i3 != sCcbVector.end() && (*i3)->isActive()) {
      /* Let implementer errors overide this error. */
      ccbBusy = true;
    }
  }

fake_obj:
  // Check for call on object implementer
  if (object->mImplementer && object->mImplementer->mNodeId) {
    if (req->operationId == SA_IMM_PARAM_ADMOP_ID_ESC &&
        objectName == immObjectDn && displayRes && !protocol45Allowed()) {
      err = SA_AIS_ERR_INVALID_PARAM;
      LOG_NO(
          "ERR_INVALID_PARAM: Display Admin-op rejected. Protocol4.5 not allowed");
      goto done;
    }

    *implConn = object->mImplementer->mConn;
    *implNodeId = object->mImplementer->mNodeId;

    TRACE_5(
        "IMPLEMENTER FOR ADMIN OPERATION INVOKE %u "
        "conn:%u node:%x name:%s",
        object->mImplementer->mId, *implConn, *implNodeId,
        object->mImplementer->mImplementerName.c_str());

    // If request originated on this node => verify request continuation.
    // It should already have been created before the fevs was forwarded.
    if (reqConn) {
      SaUint32T timeout =
          (req->timeout) ? ((SaUint32T)(req->timeout) / 100 + 1) : 0;
      TRACE_5("Updating req invocation inv:%llu conn:%u timeout:%u", saInv,
              reqConn, timeout);

      ContinuationMap2::iterator ci = sAdmReqContinuationMap.find(saInv);
      if (ci == sAdmReqContinuationMap.end()) {
        LOG_IN("Assuming reply for adminOp %llu arrived before request.",
               saInv);
      } else {
        TRACE(
            "Located pre request continuation %llu adjusting timeout"
            " to %u",
            saInv, timeout);
        ci->second.mTimeout = timeout;
        ci->second.mImplId = object->mImplementer->mId;
      }
    }

    if (*implConn) {
      if (object->mImplementer->mDying) {
        LOG_WA("Lost connection with implementer %s %u in admin op",
               object->mImplementer->mImplementerName.c_str(),
               object->mImplementer->mId);

        iem = sImplDetachTime.find(object->mImplementer);
        err = (iem != sImplDetachTime.end()) ? SA_AIS_ERR_TRY_AGAIN
                                             : SA_AIS_ERR_NOT_EXIST;

        if (reqConn) {
          fetchAdmReqContinuation(saInv,
                                  &reqConn); /* Remove any request cont. */
        }
        *implConn = 0;
      } else {
        if (object->mImplementer->mAdminOpBusy) {
          TRACE_5(
              "Implementer '%s' for object: '%s' is busy with other admop, "
              "request is queued",
              object->mImplementer->mImplementerName.c_str(),
              objectName.c_str());
        }

        TRACE_5("Storing impl invocation %u for inv: %llu", *implConn, saInv);

        ++(object->mImplementer->mAdminOpBusy);
        sAdmImplContinuationMap[saInv] =
            ContinuationInfo3(*implConn, reply_dest, object->mImplementer);
      }
    }
  } else {
    /* Check for special imm OI support */
    if (!pbeExpected && objectName == immObjectDn) {
      err = updateImmObject2(req);
      if (req->operationId == SA_IMM_PARAM_ADMOP_ID_ESC && displayRes &&
          req->params && protocol45Allowed()) {
        IMMSV_ADMIN_OPERATION_PARAM* params = req->params;
        err = SA_AIS_ERR_REPAIR_PENDING;

        while (params) {
          if ((strcmp(params->paramName.buf, SA_IMM_PARAM_ADMOP_NAME) == 0) &&
              (params->paramType == SA_IMM_ATTR_SASTRINGT)) {
            if (strncmp(params->paramBuffer.val.x.buf, "display", 7) == 0) {
              *displayRes = true;
            } else {
              LOG_WA(
                  "Invalid OperationName %s for dumping IMM resource"
                  "is not set properly",
                  params->paramBuffer.val.x.buf);
              err = SA_AIS_ERR_INVALID_PARAM;
            }
            break;
          }
          params = params->next;
        }
      }

      TRACE_7("Admin op on special object %s whith no implementer ret:%u",
              objectName.c_str(), err);
    } else if (objectName == immManagementDn) {
      err = admoImmMngtObject(req, isAtCoord);
      TRACE_7("Admin op on special object %s whith no implementer ret:%u",
              objectName.c_str(), err);
    } else {
      if (object->mImplementer &&
          (iem = sImplDetachTime.find(object->mImplementer)) !=
              sImplDetachTime.end()) {
        err = SA_AIS_ERR_TRY_AGAIN;
      } else {
        TRACE_7("ERR_NOT_EXIST: object '%s' does not have an implementer",
                objectName.c_str());
        err = SA_AIS_ERR_NOT_EXIST;
      }
    }
  }

  if ((err == SA_AIS_OK) && ccbBusy) {
    LOG_NO("ERR_BUSY: ccb id %u is active on object %s", ccbIdOfObj,
           objectName.c_str());
    err = SA_AIS_ERR_BUSY;
  }

  if ((err == SA_AIS_OK) && admoMissmatch) {
    LOG_NO("ERR_BAD_OPERATION: Mismatch on administrative owner '%s' != '%s'",
           objAdminOwnerName.c_str(), adminOwner->mAdminOwnerName.c_str());
    err = SA_AIS_ERR_BAD_OPERATION;
  }

done:

  if (err != SA_AIS_OK) {
    fetchAdmReqContinuation(saInv, &reqConn); /* Remove any request cont. */
  }

  TRACE_LEAVE();
  return err;
}

void ImmModel::setAdmReqContinuation(SaInvocationT& saInv, SaUint32T reqConn) {
  TRACE_ENTER();
  TRACE_5("setAdmReqContinuation <%llu, %u>", saInv, reqConn);
  sAdmReqContinuationMap[saInv] =
      ContinuationInfo2(reqConn, 2 * DEFAULT_TIMEOUT_SEC);
  TRACE_LEAVE();
}

void ImmModel::setSearchReqContinuation(SaInvocationT& saInv, SaUint32T reqConn,
                                        SaUint32T implTimeout) {
  TRACE_ENTER();
  TRACE_5("setSearchReqContinuation <%llu, %u, %usec>", saInv, reqConn,
          implTimeout);
  sSearchReqContinuationMap[saInv] = ContinuationInfo2(reqConn, implTimeout);
  TRACE_LEAVE();
}

void ImmModel::setSearchImplContinuation(SaInvocationT& saInv,
                                         SaUint64T reply_dest) {
  sSearchImplContinuationMap[saInv] = ContinuationInfo3(0, reply_dest, NULL);
}

void ImmModel::fetchSearchReqContinuation(SaInvocationT& inv,
                                          SaUint32T* reqConn) {
  TRACE_ENTER();
  ContinuationMap2::iterator ci = sSearchReqContinuationMap.find(inv);
  if (ci == sSearchReqContinuationMap.end()) {
    *reqConn = 0;
    TRACE_5("Failed to find continuation for inv:%llu", inv);
    return;
  }
  TRACE_5("REQ SEARCH CONTINUATION %u FOUND FOR %llu", ci->second.mConn, inv);
  *reqConn = ci->second.mConn;
  sSearchReqContinuationMap.erase(ci);
  TRACE_LEAVE();
}

void ImmModel::fetchSearchImplContinuation(SaInvocationT& inv,
                                           SaUint64T* reply_dest) {
  ContinuationMap3::iterator ci = sSearchImplContinuationMap.find(inv);
  if (ci == sSearchImplContinuationMap.end()) {
    *reply_dest = 0LL;
    return;
  }
  TRACE_5("IMPL SEARCH CONTINUATION FOUND FOR %llu", inv);
  *reply_dest = ci->second.mReply_dest;
  sSearchImplContinuationMap.erase(ci);
}

void ImmModel::fetchAdmImplContinuation(SaInvocationT& inv, SaUint32T* implConn,
                                        SaUint64T* reply_dest) {
  TRACE_5("Fetch implCon for invocation:%llu", inv);
  ContinuationMap3::iterator ci3 = sAdmImplContinuationMap.find(inv);
  if (ci3 == sAdmImplContinuationMap.end()) {
    *implConn = 0;
    *reply_dest = 0LL;
    return;
  }
  if ((*implConn) && (*implConn != ci3->second.mConn)) {
    LOG_WA("Collision on invocation identity %llu", inv);
  }
  TRACE_5("IMPL ADM CONTINUATION %u FOUND FOR %llu", ci3->second.mConn, inv);
  *implConn = ci3->second.mConn;
  *reply_dest = ci3->second.mReply_dest;
  (ci3->second.mImplementer->mAdminOpBusy)--;
  if (ci3->second.mImplementer->mAdminOpBusy) {
    TRACE_5("Waiting for %u additional admop replies after admop return",
            ci3->second.mImplementer->mAdminOpBusy);
  }
  sAdmImplContinuationMap.erase(ci3);
}

void ImmModel::fetchAdmReqContinuation(SaInvocationT& inv, SaUint32T* reqConn) {
  ContinuationMap2::iterator ci = sAdmReqContinuationMap.find(inv);
  if (ci == sAdmReqContinuationMap.end()) {
    *reqConn = 0;
    return;
  }

  TRACE_5("REQ ADM CONTINUATION %u FOUND FOR %llu", ci->second.mConn, inv);
  *reqConn = ci->second.mConn;
  sAdmReqContinuationMap.erase(ci);
}

bool ImmModel::checkSubLevel(const std::string& objectName, size_t rootStart) {
  std::string sublevel = objectName.substr(0, rootStart ? (rootStart - 1) : 0);
  return sublevel.find(',') == std::string::npos;
}

bool ImmModel::schemaNameCheck(const std::string& name) const {
  return is_valid_schema_name(name.c_str());
}

bool ImmModel::nameCheck(const std::string& name, bool strict) const {
  return is_regular_name(name.c_str(), strict);
}

bool ImmModel::nameToInternal(std::string& name) {
  size_t pos;
  size_t len = name.length();
  unsigned char prev_chr = '\0';

  for (pos = 0; pos < len; ++pos) {
    unsigned char chr = name.at(pos);
    if (prev_chr == '\\') {
      if (chr == ',') {
        name.replace(pos, 1, 1, '#');
      } else if (chr == '#') {
        LOG_WA(
            "Can not accept external DN with escaped hash '\\#' "
            "pos:%zu in %s",
            pos, name.c_str());
        return false;
      }
    }
    prev_chr = chr;
  }

  return nameCheck(name, false);
}

void ImmModel::nameToExternal(std::string& name) {
  size_t pos;
  size_t len = name.length();
  unsigned char prev_chr = '\0';

  for (pos = 0; pos < len; ++pos) {
    unsigned char chr = name.at(pos);
    if ((chr == '#') && (prev_chr == '\\')) {
      name.replace(pos, 1, 1, ',');
    }
    prev_chr = chr;
  }
}

void ImmModel::updateImmObject(std::string newClassName,
                               bool remove)  // default is remove == false
{
  TRACE_ENTER();
  ObjectMap::iterator oi = sObjectMap.find(immObjectDn);

  if (oi == sObjectMap.end()) {
    TRACE_LEAVE();
    return;  // ImmObject not created yet.
  }

  ObjectInfo* immObject = oi->second;

  ImmAttrValueMap::iterator avi = immObject->mAttrValueMap.find(immAttrClasses);

  osafassert(avi != immObject->mAttrValueMap.end());

  osafassert(avi->second->isMultiValued());
  ImmAttrMultiValue* valuep = (ImmAttrMultiValue*)avi->second;

  if (remove) {
    osafassert(valuep->removeExtraValueC_str(newClassName.c_str()));
  } else if (newClassName == immClassName) {
    TRACE_5("Imm meta object class %s added", immClassName.c_str());
    // First time, include all current classes
    // Set first value to the class name of the Imm class itself
    valuep->setValueC_str(immClassName.c_str());

    ClassMap::iterator ci;
    for (ci = sClassMap.begin(); ci != sClassMap.end(); ++ci) {
      // Check that it does not already exist.
      // This should be done in a better way.
      // An attribute in the imm object that
      // allows us to simply test if this is
      // intial start or a reload.
      if ((ci->first != immClassName) &&
          (!valuep->hasExtraValueC_str(ci->first.c_str()))) {
        TRACE_5("Adding existing class %s", ci->first.c_str());
        valuep->setExtraValueC_str(ci->first.c_str());
      }
    }
  } else {
    // Just add the newly added class.
    // Check that it does not already exist.
    // This should be done in a better way.
    // An attribute in the imm object that
    // allows us to simply test if this is intial start or a reload.
    if (!valuep->hasExtraValueC_str(newClassName.c_str())) {
      TRACE_5("Adding new class %s", newClassName.c_str());
      valuep->setExtraValueC_str(newClassName.c_str());
    } else {
      TRACE_5("Class %s already existed", newClassName.c_str());
    }
  }
  TRACE_LEAVE();
}

SaAisErrorT ImmModel::updateImmObject2(const ImmsvOmAdminOperationInvoke* req) {
  SaAisErrorT err = SA_AIS_ERR_REPAIR_PENDING;
  /* Function for handling admin-ops directed at the immsv itself.
     In this case the non-standard opensaf object
     opensafImm=opensafImm,safApp=safImmService.

     If PBE is enabled, these admin-ops are handled by the PBE-OI.
     But when there is no PBE, then we handle the adminOp inside the immnds
     and reply directly. An error reply from such an internally handled adminOp
     will be handled identically to an error reply for a normal adminOp where
     the error is caught early.

     But an OK reply for an internally handled adminOp is encoded as
     SA_AIS_ERR_REPAIR_PENDING, to avoid confusion with the normal OK reponse in
     the early processing. That error code is not used anywhere in the imm spec,
     so there is no risk of confusing it with a normal error response from early
     processing either. The ok/SA_AIS_ERR_REPAIR_PENDING is converted back to
     SA_AIS_OK before replying to user.
  */

  ImmAttrValue* valuep = NULL;
  ImmAttrValueMap::iterator avi;
  ObjectInfo* immObject = NULL;
  unsigned int noStdFlags = 0x00000000;

  TRACE_ENTER();
  ObjectMap::iterator oi = sObjectMap.find(immObjectDn);

  if (oi == sObjectMap.end()) {
    err = SA_AIS_ERR_NOT_EXIST;
    goto done;
  }

  immObject = oi->second;
  avi = immObject->mAttrValueMap.find(immAttrNostFlags);
  osafassert(avi != immObject->mAttrValueMap.end());
  osafassert(!(avi->second->isMultiValued()));
  valuep = (ImmAttrValue*)avi->second;
  noStdFlags = valuep->getValue_int();

  if ((req->params == NULL) ||
      (req->params->paramType != SA_IMM_ATTR_SAUINT32T)) {
    err = SA_AIS_ERR_INVALID_PARAM;
    goto done;
  }

  if (req->operationId == OPENSAF_IMM_NOST_FLAG_ON) {
    SaUint32T flagsToSet = req->params->paramBuffer.val.sauint32;
    /* Reject 1safe2pbe, only allowed to set when primary PBE is present. */
    if (flagsToSet == OPENSAF_IMM_FLAG_2PBE1_ALLOW) {
      LOG_NO(
          "Imm %s: Rejecting toggling ON flag 0x%x when primary PBE is not attached",
          immAttrNostFlags.c_str(), OPENSAF_IMM_FLAG_2PBE1_ALLOW);
      err = SA_AIS_ERR_BAD_OPERATION;
      goto done;
    }
    TRACE_5("Admin op NOST_FLAG_ON, current flags %x on flags %x", noStdFlags,
            flagsToSet);
    noStdFlags |= flagsToSet;
    valuep->setValue_int(noStdFlags);
    LOG_NO("%s changed to: 0x%x", immAttrNostFlags.c_str(), noStdFlags);
  } else if (req->operationId == OPENSAF_IMM_NOST_FLAG_OFF) {
    SaUint32T flagsToUnSet = req->params->paramBuffer.val.sauint32;
    TRACE_5("Admin op NOST_FLAG_OFF, current flags %x off flags %x", noStdFlags,
            flagsToUnSet);
    noStdFlags &= ~flagsToUnSet;
    valuep->setValue_int(noStdFlags);
    LOG_NO("%s changed to: 0x%x", immAttrNostFlags.c_str(), noStdFlags);
  } else {
    LOG_NO("Invalid operation ID %llu, for operation on %s",
           (SaUint64T)req->operationId, immObjectDn.c_str());
    err = SA_AIS_ERR_INVALID_PARAM;
  }

done:
  TRACE_LEAVE();
  return err;
}

SaAisErrorT ImmModel::resourceDisplay(
    const struct ImmsvAdminOperationParam* reqparams,
    struct ImmsvAdminOperationParam** rparams, SaUint64T searchcount) {
  SaAisErrorT err = SA_AIS_OK;
  const struct ImmsvAdminOperationParam* params = reqparams;
  SaStringT opName = NULL, resourceName = NULL, errStr = NULL;
  struct ImmsvAdminOperationParam* resparams = NULL;

  TRACE_ENTER();

  while (params) {
    if ((strcmp(params->paramName.buf, SA_IMM_PARAM_ADMOP_NAME)) == 0) {
      opName = params->paramBuffer.val.x.buf;
      break;
    }
    params = params->next;
  }

  if (opName) {
    params = reqparams;
    while (params) {
      if ((strcmp(params->paramName.buf, "resource")) == 0) {
        resourceName = params->paramBuffer.val.x.buf;
        TRACE_5("The resource  is %s", resourceName);
        break;
      }
      params = params->next;
    }
    if (!resourceName) {
      if (strcmp(opName, "display-help") == 0) {
        TRACE_5(
            "supported IMM resources to be displyed by using admin operation");
      } else {
        LOG_WA(
            "The resource type is not present in the requested parameters for displaying IMM resources");
        err = SA_AIS_ERR_INVALID_PARAM;
        errStr =
            strdup("resource type is not present in the requested parameters");
        goto done;
      }
    }
  } else {
    LOG_WA(
        "The Operation name is not present in the requested parameters for displaying IMM resources");
    err = SA_AIS_ERR_INVALID_PARAM;
    errStr =
        strdup("Operation name is not present in the requested parameters");
    goto done;
  }

  if ((strcmp(opName, "display") == 0)) {
    resparams = (struct ImmsvAdminOperationParam*)calloc(
        1, sizeof(struct ImmsvAdminOperationParam));
    resparams->paramType = SA_IMM_ATTR_SAINT64T;
    resparams->next = NULL;
    resparams->paramName.size = strlen("count") + 1;
    resparams->paramName.buf = (char*)malloc(resparams->paramName.size);
    strcpy(resparams->paramName.buf, "count");
    if ((strcmp(resourceName, "implementers") == 0)) {
      resparams->paramBuffer.val.saint64 = sImplementerVector.size();
    } else if ((strcmp(resourceName, "adminowners") == 0)) {
      resparams->paramBuffer.val.saint64 = sOwnerVector.size();
    } else if ((strcmp(resourceName, "ccbs") == 0)) {
      resparams->paramBuffer.val.saint64 = sCcbVector.size();
    } else if ((strcmp(resourceName, "searches") == 0)) {
      resparams->paramBuffer.val.saint64 = searchcount;
    } else {
      LOG_WA("Display of IMM reources for resourceName %s is unsupported",
             resourceName);
      err = SA_AIS_ERR_INVALID_PARAM;
      errStr =
          strdup("Display of IMM reources for resourceName is unsupported") + 1;
      free(resparams);
      resparams = NULL;
      goto done;
    }

  } else if (strcmp(opName, "displayverbose") == 0) {
    struct ImmsvAdminOperationParam* result = NULL;
    if ((strcmp(resourceName, "implementers") == 0)) {
      if (!sImplementerVector.empty()) {
        if (sImplementerVector.size() < 128) {
          ImplementerVector::iterator i;
          for (i = sImplementerVector.begin(); i != sImplementerVector.end();
               ++i) {
            ImplementerInfo* info = (*i);
            struct ImmsvAdminOperationParam* res =
                (struct ImmsvAdminOperationParam*)calloc(
                    1, sizeof(struct ImmsvAdminOperationParam));
            res->paramType = SA_IMM_ATTR_SAINT64T;
            res->next = NULL;
            res->paramName.size = info->mImplementerName.length() + 1;
            res->paramName.buf = (char*)malloc(res->paramName.size);
            strcpy(res->paramName.buf, info->mImplementerName.c_str());
            res->paramBuffer.val.saint64 = info->mNodeId;
            if (result) {
              result->next = res;
              result = res;
            } else {
              result = res;
              resparams = res;
            }
          }

        } else {
          LOG_NO(
              "The Number of implementers are greater than 128, displaying the implementers"
              "information to syslog");
          ImplementerVector::iterator i;
          for (i = sImplementerVector.begin(); i != sImplementerVector.end();
               ++i) {
            ImplementerInfo* info = (*i);
            LOG_IN("Implementer name %s and location of the implementer is %u",
                   info->mImplementerName.c_str(), info->mNodeId);
          }
        }
      }
    } else if ((strcmp(resourceName, "adminowners") == 0)) {
      if (!sOwnerVector.empty()) {
        if (sOwnerVector.size() < 128) {
          AdminOwnerVector::iterator i;
          for (i = sOwnerVector.begin(); i != sOwnerVector.end(); ++i) {
            AdminOwnerInfo* adminOwner = (*i);
            struct ImmsvAdminOperationParam* res =
                (struct ImmsvAdminOperationParam*)calloc(
                    1, sizeof(struct ImmsvAdminOperationParam));
            res->paramType = SA_IMM_ATTR_SAINT64T;
            res->next = NULL;
            res->paramName.size = adminOwner->mAdminOwnerName.length() + 1;
            res->paramName.buf = (char*)malloc(res->paramName.size);
            strcpy(res->paramName.buf, adminOwner->mAdminOwnerName.c_str());
            res->paramBuffer.val.saint64 = adminOwner->mNodeId;
            if (result) {
              result->next = res;
              result = res;
            } else {
              result = res;
              resparams = res;
            }
          }
        } else {
          LOG_NO(
              "The Number of AdminOwners are greater than 128, displaying the adminowner"
              "information to syslog");
          AdminOwnerVector::iterator i;
          for (i = sOwnerVector.begin(); i != sOwnerVector.end(); ++i) {
            AdminOwnerInfo* adminOwner = (*i);
            LOG_IN("Implementer name %s and location of the implementer is %u",
                   adminOwner->mAdminOwnerName.c_str(), adminOwner->mNodeId);
          }
        }
      }
    } else {
      LOG_WA("Verbose display of reourcename %s is unsupported", resourceName);
      err = SA_AIS_ERR_INVALID_PARAM;
      errStr =
          strdup("verbose display of requested resourceName is unsupported");
      goto done;
    }
  } else if ((strcmp(opName, "display-help") == 0)) {
    const char* resources[] = {"implementers", "adminowners", "ccbs",
                               "searches", NULL};
    int i = 0;

    struct ImmsvAdminOperationParam* result = NULL;
    while (resources[i]) {
      struct ImmsvAdminOperationParam* res =
          (struct ImmsvAdminOperationParam*)calloc(
              1, sizeof(struct ImmsvAdminOperationParam));
      res->paramType = SA_IMM_ATTR_SASTRINGT;
      res->next = NULL;
      res->paramName.size = strlen("supportedResources") + 1;
      res->paramName.buf = (char*)malloc(res->paramName.size);
      strcpy(res->paramName.buf, "supportedResources");
      res->paramBuffer.val.x.size = strlen(resources[i]) + 1;
      res->paramBuffer.val.x.buf = (char*)malloc(res->paramBuffer.val.x.size);
      strcpy(res->paramBuffer.val.x.buf, resources[i]);

      if (result) {
        result->next = res;
        result = res;
      } else {
        result = res;
        resparams = res;
      }
      i++;
    }
  } else {
    LOG_WA("OperationName %s is not supported", opName);
    err = SA_AIS_ERR_INVALID_PARAM;
    errStr = strdup("OperationName is not supported");
  }

done:
  if (!resparams && errStr) {
    struct ImmsvAdminOperationParam* errparam =
        (struct ImmsvAdminOperationParam*)calloc(
            1, sizeof(struct ImmsvAdminOperationParam));
    errparam->paramType = SA_IMM_ATTR_SASTRINGT;
    errparam->next = NULL;
    errparam->paramName.size = strlen(SA_IMM_PARAM_ADMOP_ERROR) + 1;
    errparam->paramName.buf = (char*)malloc(errparam->paramName.size);
    strcpy(errparam->paramName.buf, SA_IMM_PARAM_ADMOP_ERROR);
    errparam->paramBuffer.val.x.size = strlen(errStr) + 1;
    errparam->paramBuffer.val.x.buf = errStr;
    resparams = errparam;
  }

  *rparams = resparams;

  TRACE_LEAVE();
  return err;
}

SaAisErrorT ImmModel::admoImmMngtObject(const ImmsvOmAdminOperationInvoke* req,
                                        bool isAtCoord) {
  SaAisErrorT err = SA_AIS_ERR_INTERRUPT;
  /* Function for handling admin-ops directed at the immsv itself.
     In this case the standard object safRdn=immManagement,safApp=safImmService.
  */

  ImmAttrValue* valuep = NULL;
  ImmAttrValueMap::iterator avi;
  ObjectInfo* immObject = NULL;

  TRACE_ENTER();
  ObjectMap::iterator oi = sObjectMap.find(immManagementDn);
  if (oi == sObjectMap.end()) {
    err = SA_AIS_ERR_NOT_EXIST;
    goto done;
  }

  immObject = oi->second;
  avi = immObject->mAttrValueMap.find(saImmRepositoryInit);
  osafassert(avi != immObject->mAttrValueMap.end());
  osafassert(!(avi->second->isMultiValued()));
  valuep = (ImmAttrValue*)avi->second;

  if (req->params != NULL) {
    err = SA_AIS_ERR_INVALID_PARAM;
    goto done;
  }

  if (req->operationId == SA_IMM_ADMIN_EXPORT) { /* Standard */
    err = SA_AIS_ERR_NOT_SUPPORTED;
  } else if (req->operationId ==
             SA_IMM_ADMIN_INIT_FROM_FILE) { /* Non standard. */

    valuep->setValue_int(SA_IMM_INIT_FROM_FILE);
    if (immInitMode != SA_IMM_INIT_FROM_FILE) {
      immInitMode = SA_IMM_INIT_FROM_FILE;
      LOG_NO("SaImmRepositoryInitModeT FORCED to: SA_IMM_INIT_FROM_FILE");
    }
  } else if (req->operationId == SA_IMM_ADMIN_ABORT_CCBS) { /* Non standard. */
    LOG_NO("Received: immadm -o %u safRdn=immManagement,safApp=safImmService",
           SA_IMM_ADMIN_ABORT_CCBS);
    if (isAtCoord) {
      LOG_IN("sAbortNonCriticalCcbs = true;");
      sAbortNonCriticalCcbs = true;
    }
  } else {
    LOG_NO("Invalid operation ID %llu, for operation on %s",
           (SaUint64T)req->operationId, immManagementDn.c_str());
    err = SA_AIS_ERR_INVALID_PARAM;
  }

done:
  TRACE_LEAVE();
  return err;
}

SaUint32T ImmModel::findConnForImplementerOfObject(std::string objectDn) {
  ObjectInfo* object;
  ObjectMap::iterator oi;

  if (!(nameCheck(objectDn) || nameToInternal(objectDn))) {
    LOG_ER("Invalid object name %s sent internally", objectDn.c_str());
    abort();
  }

  oi = sObjectMap.find(objectDn);
  if (oi == sObjectMap.end()) {
    TRACE_7("Object '%s' %u does not exist", objectDn.c_str(),
            (unsigned int)objectDn.size());
    return 0;
  }

  object = oi->second;
  if (!object->mImplementer) {
    return 0;
  }

  return object->mImplementer->mConn;
}

ImplementerInfo* ImmModel::findImplementer(SaUint32T id) {
  // TODO change rep to handle a large number of implementers ?
  // Current is not efficient with vector/linear
  ImplementerVector::iterator i;
  for (i = sImplementerVector.begin(); i != sImplementerVector.end(); ++i) {
    ImplementerInfo* info = (*i);
    if (info->mId == id) return info;
  }

  return NULL;
}

ImplementerInfo* ImmModel::findImplementer(const std::string& iname) {
  // TODO: use a map <name>->impl instead.
  ImplementerVector::iterator i;
  for (i = sImplementerVector.begin(); i != sImplementerVector.end(); ++i) {
    ImplementerInfo* info = (*i);
    if (info->mImplementerName == iname) return info;
  }

  return NULL;
}

SaUint32T ImmModel::getImplementerId(SaUint32T localConn) {
  osafassert(localConn);
  ImplementerVector::iterator i;
  for (i = sImplementerVector.begin(); i != sImplementerVector.end(); ++i) {
    ImplementerInfo* info = (*i);
    if (info->mConn == localConn) return info->mId;
  }

  return 0;
}

void ImmModel::discardNode(unsigned int deadNode, IdVector& cv, IdVector& gv,
                           bool isAtCoord, bool scAbsence) {
  ImplementerVector::iterator i;
  AdminOwnerVector::iterator i2;
  CcbVector::iterator i3;
  ConnVector implv;
  ConnVector::iterator i4;
  ContinuationMap3::iterator ci3;
  TRACE_ENTER();

  if (sImmNodeState == IMM_NODE_W_AVAILABLE) {
    /* Sync is ongoing and we are a sync client.
       Remember the death of the node.
    */
    TRACE_7("Sync client notes death of node %x", deadNode);
    sNodesDeadDuringSync.push_back(deadNode);
    /* discardNode used to be postponed at sync clients. No longer
       after #1871 Opensaf4.3. Instead perform it now and possibly
       again after finalizeSync. We want to discard implementers
       resident at the dead node also at sync clients, since after
       #1871 they could have been created at sync clients during sync.
       sNodesDeadDuringSync is cleared after each sync message.
       At finalizeSync it will only contain any nodes that died
       in the window after the last sync message arrived but before
       the finalizeSync message arrives.
    */
  }

  if (sImmNodeState == IMM_NODE_R_AVAILABLE) {
    /* Sync is ongoing but we are not a sync client.
       Remember the death of the node for verify in finalizeSync
    */
    TRACE_7("Death of node %x noted during on-going sync", deadNode);
    sNodesDeadDuringSync.push_back(deadNode);
  }

  // Discard implementers from that node.
  for (i = sImplementerVector.begin(); i != sImplementerVector.end(); ++i) {
    ImplementerInfo* info = (*i);
    if (info->mNodeId == deadNode) {
      ContinuationMap2::iterator ci2;
      for (ci2 = sAdmReqContinuationMap.begin();
           ci2 != sAdmReqContinuationMap.end(); ++ci2) {
        if ((ci2->second.mTimeout) && (ci2->second.mImplId == info->mId)) {
          TRACE_5("Forcing Adm Req continuation to expire %llu", ci2->first);
          ci2->second.mTimeout = 1; /* one second is minimum timeout. */
        }
      }

      if (isAtCoord) {
        // pushing to implv to find the ccbIds with implemeter id which are
        // going to be disconnected. Later abort the CcbId. This is done only at
        // co-ordinator.
        implv.push_back(info->mId);
      }
      // discardImplementer(info->mId);
      // Doing it directly here for efficiency.
      // But watch out for changes in discardImplementer
      LOG_NO("Implementer disconnected %u <%u, %x(down)> (%s)", info->mId,
             info->mConn, info->mNodeId, info->mImplementerName.c_str());

      // Note the time of death and id of the demised implementer.
      sImplDetachTime[info] = ContinuationInfo2(info->mId, DEFAULT_TIMEOUT_SEC);

      info->mId = 0;
      info->mConn = 0;
      info->mNodeId = 0;
      info->mMds_dest = 0LL;
      info->mAdminOpBusy = 0;
      info->mApplier = false;
      info->mDying = false;  // We are so dead now we dont need to remember
      // We dont need the dying phase for implementer when an entire node
      // is gone. Thew dying phase is only needed to handle trailing evs
      // messages that depend on the implementer. We DO NOT want different
      // behavior at different nodes, only because an implementor is gone.

      // Save the time when applier is disconnected
      if(info->mImplementerName.at(0) == '@') {
        struct timespec ts;
        osaf_clock_gettime(CLOCK_MONOTONIC, &ts);
        info->mApplierDisconnected = osaf_timespec_to_millis(&ts);
      }

      // We keep the ImplementerInfo entry with the implementer name,
      // for now.
      // The ImplementerInfo is pointed to by ClassInfo or ObjectInfo.
      // The point is to allow quick re-attachmenet by a process to the
      // implementer.
    }
  }
  /*
      Fethes CcbIds which are non-critcal and Ccbs has implementer which are
      going to be disconnected, because of node down. This is done only at
     co-ordinator.

  */
  if (isAtCoord && (!implv.empty())) {
    CcbImplementerMap::iterator isi;
    for (i4 = implv.begin(); i4 != implv.end(); ++i4) {
      for (i3 = sCcbVector.begin(); i3 != sCcbVector.end(); ++i3) {
        isi = ((*i3)->mImplementers.find(*i4));
        if (isi != ((*i3)->mImplementers.end()) &&
            ((*i3)->mState < IMM_CCB_CRITICAL)) {
          gv.push_back((*i3)->mId);
        }
      }
    }
  }

  // Discard AdminOwners
  i2 = sOwnerVector.begin();
  while (i2 != sOwnerVector.end()) {
    AdminOwnerInfo* ainfo = (*i2);
    if (ainfo->mNodeId == deadNode && (!ainfo->mDying || scAbsence)) {
      // Inefficient: lookup of admin owner again.
      if (adminOwnerDelete(ainfo->mId, true) == SA_AIS_OK) {
        i2 = sOwnerVector.begin();  // restart of iteration again.
      } else {
        LOG_WA("Could not delete admin owner %u %s", ainfo->mId,
               ainfo->mAdminOwnerName.c_str());
        ++i2;
      }
    } else {
      ++i2;
    }
  }

  // Fetch CcbIds for Ccbs originating at the departed node.
  for (i3 = sCcbVector.begin(); i3 != sCcbVector.end(); ++i3) {
    if (((*i3)->mOriginatingNode == deadNode) && ((*i3)->isActive())) {
      cv.push_back((*i3)->mId);
      osafassert((*i3)->mOriginatingConn == 0);  // Dead node can not be us!!
    }
  }

  /* Discard Adm Impl continuation */
  for (ci3 = sAdmImplContinuationMap.begin();
       ci3 != sAdmImplContinuationMap.end();) {
    if (m_NCS_NODE_ID_FROM_MDS_DEST(ci3->second.mReply_dest) == deadNode) {
      TRACE_5("Discarding Adm Impl continuation %llu", ci3->first);
      ci3 = sAdmImplContinuationMap.erase(ci3);
    } else {
      ++ci3;
    }
  }

  /* Discard Search Impl continuation */
  for (ci3 = sSearchImplContinuationMap.begin();
       ci3 != sSearchImplContinuationMap.end();) {
    if (m_NCS_NODE_ID_FROM_MDS_DEST(ci3->second.mReply_dest) == deadNode) {
      TRACE_5("Discarding Search Impl continuation %llu", ci3->first);
      ci3 = sSearchImplContinuationMap.erase(ci3);
    } else {
      ++ci3;
    }
  }

  TRACE_LEAVE();
}

void ImmModel::discardImplementer(unsigned int implHandle, bool reallyDiscard,
                                  IdVector& gv, bool isAtCoord) {
  // Note: If this function is altered, then you may need to make
  // changes in ImmModel::discardNode, since that function also deletes
  // implementers.
  CcbVector::iterator i1;
  SaUint32T mid;

  TRACE_ENTER();
  ImplementerInfo* info = findImplementer(implHandle);
  if (info) {
    ContinuationMap2::iterator ci2;
    if (reallyDiscard) {
      LOG_NO("Implementer disconnected %u <%u, %x> (%s)", info->mId,
             info->mConn, info->mNodeId, info->mImplementerName.c_str());

      // Note the time of death and id of the demised implementer.
      sImplDetachTime[info] = ContinuationInfo2(info->mId, DEFAULT_TIMEOUT_SEC);
      if (isAtCoord) {
        // Find the Non-critical ccbs with implemeter id which are going to be
        // Disconnected. Later abort the CCBId. This is done only at
        // co-ordinator.
        mid = info->mId;
        CcbImplementerMap::iterator isi;
        for (i1 = sCcbVector.begin(); i1 != sCcbVector.end(); ++i1) {
          isi = (*i1)->mImplementers.find(mid);
          if (isi != ((*i1)->mImplementers.end()) &&
              ((*i1)->mState < IMM_CCB_CRITICAL)) {
            gv.push_back((*i1)->mId);
          }
        }
      }

      info->mId = 0;
      info->mConn = 0;
      info->mNodeId = 0;
      info->mMds_dest = 0LL;
      info->mAdminOpBusy = 0;
      info->mApplier = false;
      info->mDying = false;  // We are so dead now, we dont need to remember
      // We keep the ImplementerInfo entry with the implementer name,
      // for now. The ImplementerInfo is pointed to by ClassInfo or
      // ObjectInfo. The point is to allow quick re-attachmenet by a
      // process to the implementer.
      // But we have a problem of ever growing number of distinct
      // implementers. Possible solution is to here iterate through all
      // classes and objects. If none point to this object then we can
      // actually remove it. Removal must be via fevs though!

      // Save the time when applier is disconnected
      if(info->mImplementerName.at(0) == '@') {
        struct timespec ts;
        osaf_clock_gettime(CLOCK_MONOTONIC, &ts);
        info->mApplierDisconnected = osaf_timespec_to_millis(&ts);
      }

      if ((sImmNodeState == IMM_NODE_R_AVAILABLE) ||
          (sImmNodeState == IMM_NODE_W_AVAILABLE)) {
        /* Sync is ongoing. Remember the death of the implementer. */
        sImplsDeadDuringSync.push_back(implHandle);
      }
    } else {
      if (!info->mDying) {
        LOG_NO(
            "Implementer locally disconnected. Marking it as doomed "
            "%u <%u, %x> (%s)",
            info->mId, info->mConn, info->mNodeId,
            info->mImplementerName.c_str());
        info->mDying = true;
      }
    }

    for (ci2 = sAdmReqContinuationMap.begin();
         ci2 != sAdmReqContinuationMap.end(); ++ci2) {
      if ((ci2->second.mTimeout) && (ci2->second.mImplId == implHandle)) {
        TRACE_5("Forcing Adm Req continuation to expire %llu", ci2->first);
        ci2->second.mTimeout = 1; /* one second is minimum timeout. */
      }
    }
  } else { /* implementer not found */
    if (sImmNodeState == IMM_NODE_W_AVAILABLE) {
      /* Sync is ongoing and we are a sync client.
         Remember the death of the implementer.
      */
      TRACE_7("Sync client notes death of implementer %u", implHandle);
      sImplsDeadDuringSync.push_back(implHandle);
    } else {
      LOG_WA("discardImplementer: Implementer %u is missing - ignoring",
             implHandle);
      /* Should not happen */
    }
  }

  TRACE_LEAVE();
}

void ImmModel::discardContinuations(SaUint32T dead) {
  TRACE_ENTER();
  // ContinuationMap::iterator ci;
  ContinuationMap2::iterator ci2;
  ContinuationMap3::iterator ci3;

  for (ci2 = sAdmReqContinuationMap.begin();
       ci2 != sAdmReqContinuationMap.end();) {
    if (ci2->second.mConn == dead) {
      TRACE_5("Discarding Adm Req continuation %llu", ci2->first);
      ci2 = sAdmReqContinuationMap.erase(ci2);
    } else {
      ++ci2;
    }
  }

  for (ci3 = sAdmImplContinuationMap.begin();
       ci3 != sAdmImplContinuationMap.end();) {
    if (ci3->second.mConn == dead) {
      TRACE_5("Discarding Adm Impl continuation %llu", ci3->first);
      // TODO Should send fevs message to terminate call that caused
      // the implementer continuation to be created.
      // I can only do this if I check that IMMD is up first.
      ci3 = sAdmImplContinuationMap.erase(ci3);
    } else {
      ++ci3;
    }
  }

  for (ci3 = sSearchImplContinuationMap.begin();
       ci3 != sSearchImplContinuationMap.end();) {
    if (ci3->second.mConn == dead) {
      TRACE_5("Discarding Adm Impl continuation %llu", ci3->first);
      // TODO: Should send fevs message to terminate call that caused
      // the implementer continuation to be created.
      // I can only do this if I check that IMMD is up first.
      ci3 = sSearchImplContinuationMap.erase(ci3);
    } else {
      ++ci3;
    }
  }

  for (ci2 = sSearchReqContinuationMap.begin();
       ci2 != sSearchReqContinuationMap.end();) {
    if (ci2->second.mConn == dead) {
      TRACE_5("Discarding Search Req continuation %llu", ci2->first);
      ci2 = sSearchReqContinuationMap.erase(ci2);
    } else {
      ++ci2;
    }
  }

  // TODO: implementer still potentially refered to from Ccbs (mImplementers)

  TRACE_LEAVE();
  return;
}

void ImmModel::getCcbIdsForOrigCon(SaUint32T dead, IdVector& cv) {
  CcbVector::iterator i;
  for (i = sCcbVector.begin(); i != sCcbVector.end(); ++i) {
    if (((*i)->mOriginatingConn == dead) && ((*i)->isActive())) {
      cv.push_back((*i)->mId);
      /* Do not set: (*i)->mOriginatingConn = 0;
         because stale client handling requires repeated invocation
         of this function. */
    }
  }
}

/* Are all ccbs terminated ? */
bool ImmModel::ccbsTerminated(bool allowEmpty) {
  CcbVector::iterator i;

  if (sCcbVector.empty()) {
    return true;
  }

  for (i = sCcbVector.begin(); i != sCcbVector.end(); ++i) {
    if ((*i)->mState < IMM_CCB_COMMITTED) {
      if (allowEmpty && (*i)->mMutations.empty()) {
        osafassert((*i)->mImplementers.empty());
        continue;
      }
      TRACE("Waiting for CCB:%u in state %u", (*i)->mId, (*i)->mState);
      return false;
    }
  }

  return true;
}

void ImmModel::getNonCriticalCcbs(IdVector& cv) {
  CcbVector::iterator i;
  for (i = sCcbVector.begin(); i != sCcbVector.end(); ++i) {
    if ((*i)->mState < IMM_CCB_CRITICAL) {
      cv.push_back((*i)->mId);
    }
  }
}

SaUint32T ImmModel::getIdForLargeAdmo() {
  AdminOwnerVector::iterator i2;
  for (i2 = sOwnerVector.begin(); i2 != sOwnerVector.end(); ++i2) {
    if ((*i2)->mTouchedObjects.size() >= IMMSV_MAX_OBJECTS) {
      /* If the Admo is alive and large then it is a problem for
         finalizeSync.
         This method returns the id of the first large and non-dying
         admo it finds. The id is sent over fevs as a hard finalize
         message to all IMMNDs, which will either discard the admo
         or mark it as dying if immnds are in read-only mode.
         If it is dying and large then finalizeSync will
         discard it before generating the finalizeSync-message.
         If it is small, then it will be part of the message.
      */
      TRACE("Found large Admo %u %u", (*i2)->mId,
            (unsigned int)(*i2)->mTouchedObjects.size());
      return (*i2)->mId;
    }
  }
  return 0;
}

void ImmModel::getOldCriticalCcbs(IdVector& cv, SaUint32T* pbeConnPtr,
                                  unsigned int* pbeNodeIdPtr,
                                  SaUint32T* pbeIdPtr) {
  osafassert(pbeConnPtr);
  osafassert(pbeNodeIdPtr);
  osafassert(pbeIdPtr);
  *pbeNodeIdPtr = 0;
  *pbeConnPtr = 0;
  *pbeIdPtr = 0;
  CcbVector::iterator i;
  timespec now;
  osaf_clock_gettime(CLOCK_MONOTONIC, &now);
  for (i = sCcbVector.begin(); i != sCcbVector.end(); ++i) {
    if ((*i)->mState == IMM_CCB_CRITICAL &&
        ((osaf_timespec_compare(&(*i)->mWaitStartTime, &kZeroSeconds) &&
          osaf_timer_is_expired_sec(
              &now, &(*i)->mWaitStartTime,
              DEFAULT_TIMEOUT_SEC)) || /* Should be saImmOiTimeout*/
         (*i)->mPbeRestartId)) {
      /* We have a critical ccb that has: timed out OR lived through a PBE
         restart. This means the PBE is slow in processing (hung?) OR the PBE
         has crashed. Find the ccb-implementer association and verify which case
         it is.
       */
      CcbInfo* ccb = (*i);
      CcbImplementerMap::iterator isi;
      ImplementerCcbAssociation* implAssoc = NULL;
      unsigned int mutations = (unsigned int)(*i)->mMutations.size();
      int addSecs = mutations / 800;

      for (isi = ccb->mImplementers.begin(); isi != ccb->mImplementers.end();
           ++isi) {
        implAssoc = isi->second;
        if (implAssoc->mWaitForImplAck) {
          break;
        }
        implAssoc = NULL;
      }

      if (!implAssoc) {
        LOG_WA(
            "CCB %u is blocked in CRITICAL, yet NOT waiting on implementer!!",
            ccb->mId);
        continue;
      }

      osafassert(implAssoc->mImplementer);

      bool isPbe = (implAssoc->mImplementer->mImplementerName ==
                    std::string(OPENSAF_IMM_PBE_IMPL_NAME));

      if (!isPbe) {
        LOG_WA("CCB %u is blocked in CRITICAL, waiting on implementer %u/%s.",
               ccb->mId, implAssoc->mImplementer->mId,
               implAssoc->mImplementer->mImplementerName.c_str());
        continue;
      }

      TRACE("CCB %u is waiting on PBE commit", ccb->mId);
      ImplementerInfo* impInfo = reinterpret_cast<ImplementerInfo*>(
          getPbeOi(pbeConnPtr, pbeNodeIdPtr, false));
      /* Unsafe getPbeOI OK here because getOldCriticalCcbs is only a cleanup
         function. It is also only executed at coord and PBE can only be
         colocated with coord.
      */

      if (!impInfo) {
        LOG_WA(
            "No PBE implementer registered at this time, but CCB %u "
            "is waiting on PBE commit",
            ccb->mId);
        /* TODO: Should check rim if it was switched to  FROM_FILE
           FROM_FILE indicates the pathological case of PBE switched off
           accidentally leaving a ccb in critical state.
        */
        continue;
      }

      if ((ccb->mPbeRestartId == 0) &&
          !osaf_timer_is_expired_sec(&now, &ccb->mWaitStartTime,
                                    (DEFAULT_TIMEOUT_SEC + addSecs))) {
        timespec elapsed;
        osaf_timespec_subtract(&now, &ccb->mWaitStartTime, &elapsed);
        LOG_NO("Ccb %u is old, but also large (%u) will wait secs:%f", ccb->mId,
               mutations,
               (double)(DEFAULT_TIMEOUT_SEC + addSecs) -
                   osaf_timespec_to_double(&elapsed));
        continue;
      }

      if (impInfo->mId != ccb->mPbeRestartId) {
        if (ccb->mPbeRestartId == 0 &&
            (implAssoc->mImplementer->mId == impInfo->mId)) {
          LOG_WA("PBE implementer %u seems hung!", impInfo->mId);
          /*TODO return true => signalling need to kill pbe*/
          continue;
        }
        LOG_WA("Missmatch pbe-Id %u != pbe-restart-id %u", impInfo->mId,
               ccb->mPbeRestartId);
      }

      /*This is the perfect match recovery case. **/
      LOG_NO("PBE implementer %s restarted, check ccb %u",
             impInfo->mImplementerName.c_str(), ccb->mId);
      (*pbeIdPtr) = impInfo->mId;
      cv.push_back((*i)->mId);
    }
  }
}

void ImmModel::getAdminOwnerIdsForCon(SaUint32T dead, IdVector& cv) {
  AdminOwnerVector::iterator i;
  for (i = sOwnerVector.begin(); i != sOwnerVector.end(); ++i) {
    if ((*i)->mConn == dead) {
      cv.push_back((*i)->mId);
      /* Do not set: (*i)->mConn = 0;
         because stale client handling requires repeated invocation
         of this function. */
    }
  }
}

bool ImmModel::purgeSyncRequest(SaUint32T clientId) {
  TRACE_ENTER();
  bool purged = false;
  ContinuationMap2::iterator ci2;
  ContinuationMap2::iterator ciFound = sAdmReqContinuationMap.end();
  CcbVector::iterator i3;
  CcbVector::iterator ccbFound = sCcbVector.end();

  for (ci2 = sAdmReqContinuationMap.begin();
       ci2 != sAdmReqContinuationMap.end(); ++ci2) {
    if (ci2->second.mConn == clientId) {
      SaInvocationT inv = ci2->first;
      SaInt32T subinv = m_IMMSV_UNPACK_HANDLE_LOW(inv);
      if (subinv < 0) {
        LOG_IN(
            "Attempt to purge syncronous request for client connection,"
            "and found an asyncronous admin op request %d for that connection,"
            "ignoring the asyncronous continuation",
            subinv);
        continue;
      }

      if (ciFound != sAdmReqContinuationMap.end()) {
        LOG_WA(
            "Attempt to purge synchronous request %d for client connection,"
            "but already purged other synchronous admin op requests for that "
            "connection, incorrect use of imm handle",
            subinv);
        return false;
      }

      ciFound = ci2;
    }
  }

  if (ciFound != sAdmReqContinuationMap.end()) {
    sAdmReqContinuationMap.erase(ciFound);
    purged = true;
    TRACE_5("Purged syncronous Admin-op continuation");
  }

  ciFound = sSearchReqContinuationMap.end();
  for (ci2 = sSearchReqContinuationMap.begin();
       ci2 != sSearchReqContinuationMap.end(); ++ci2) {
    if (ci2->second.mConn == clientId) {
      if (purged || (ciFound != sSearchReqContinuationMap.end())) {
        LOG_WA(
            "Attempt to purge syncronous search request for client connection,"
            "but found multiple requests for that connection, "
            "incorrect use of imm handle");
        return false;
      }
      ciFound = ci2;
    }
  }

  if (ciFound != sSearchReqContinuationMap.end()) {
    sSearchReqContinuationMap.erase(ciFound);
    purged = true;
    TRACE_5("Purged search request continuation");
  }

  ciFound = sPbeRtReqContinuationMap.end();
  for (ci2 = sPbeRtReqContinuationMap.begin();
       ci2 != sPbeRtReqContinuationMap.end(); ++ci2) {
    if (ci2->second.mConn == clientId) {
      if (purged || (ciFound != sPbeRtReqContinuationMap.end())) {
        LOG_WA(
            "Attempt to purge syncronous PRTA request for client connection,"
            "but found multiple requests for that connection, "
            "incorrect use of imm handle");
        return false;
      }
      ciFound = ci2;
    }
  }

  if (ciFound != sPbeRtReqContinuationMap.end()) {
    sPbeRtReqContinuationMap.erase(ciFound);
    purged = true;
    TRACE_5("Purged PRTA update continuation");
  }

  for (i3 = sCcbVector.begin(); i3 != sCcbVector.end(); ++i3) {
    if (((*i3)->mOriginatingConn == clientId) &&
        osaf_timespec_compare(&(*i3)->mWaitStartTime, &kZeroSeconds) == 1) {
      SaUint32T ccbId = (*i3)->mId;

      if ((*i3)->mState > IMM_CCB_CRITICAL) {
        LOG_IN(
            "Attempt to purge syncronous request for client connection,"
            "ignoring CCB %u with state > IMM_CCB_CRITICAL",
            ccbId);
        continue;
      }

      if (purged || (ccbFound != sCcbVector.end())) {
        LOG_WA(
            "Attempt to purge syncronous CCB request for client connection,"
            "but found multiple requests for that connection, "
            "incorrect use of imm handle");
        return false;
      }
      ccbFound = i3;
    }
  }

  if (ccbFound != sCcbVector.end()) {
    /* To drop the ccb continuation, we set mPurged to true. By that,
       we ensure that no attempt can be made to reply to that connection
       for any continuation on this ccb (e.g. a late arriving OI reply).
     */
    (*ccbFound)->mPurged = true;
    purged = true;
    TRACE_5("Purged Ccb continuation for ccb:%u in state %u", (*ccbFound)->mId,
            (*ccbFound)->mState);
  }

  TRACE_LEAVE();
  return purged;
}

SaUint32T ImmModel::cleanTheBasement(InvocVector& admReqs,
                                     InvocVector& searchReqs, IdVector& ccbs,
                                     IdVector& pbePrtoReqs,
                                     ImplNameVector& appliers,
                                     bool iAmCoord) {
  timespec now;
  osaf_clock_gettime(CLOCK_MONOTONIC, &now);
  ContinuationMap2::iterator ci2;
  ImplementerEvtMap::iterator iem;
  CcbVector::iterator i3;
  SaUint32T ccbsStuck = 0;    /* 0 or 1 */
  SaUint32T pbeRtRegress = 0; /* 0 or 2 */

  for (ci2 = sAdmReqContinuationMap.begin();
       ci2 != sAdmReqContinuationMap.end(); ++ci2) {
    if (ci2->second.mTimeout &&
        osaf_timer_is_expired_sec(&now, &ci2->second.mCreateTime,
                                  ci2->second.mTimeout)) {
      TRACE_5("Timeout on AdministrativeOp continuation %llu  tmout:%u",
              ci2->first, ci2->second.mTimeout);
      admReqs.push_back(ci2->first);
    } else {
      timespec elapsed;
      osaf_timespec_subtract(&now, &ci2->second.mCreateTime, &elapsed);
      TRACE_5("Did not timeout now - start < %u(%f)", ci2->second.mTimeout,
              osaf_timespec_to_double(&elapsed));
    }
  }

  // sAdmImplContinuationMap is cleaned up when implementer detaches.
  // TODO: problem is implementer may stay connected for weeks and
  // there is no guarantee of explicit reply from implementer.
  // Possibly use the implicit reply of the return to cleanup contiuation.
  // Conclusion: I should add cleanup logic here anyway, since it is easy to
  // do and solves the problem.

  for (ci2 = sSearchReqContinuationMap.begin();
       ci2 != sSearchReqContinuationMap.end(); ++ci2) {
    if (osaf_timer_is_expired_sec(&now, &ci2->second.mCreateTime,
                                  ci2->second.mTimeout)) {
      TRACE_5("Timeout on Search continuation %llu", ci2->first);
      searchReqs.push_back(ci2->first);
    }
  }

  // sSearchImplContinationMap is cleaned up when implementer detaches.
  // TODO See above problem description for sAdmImplContinuationMap.
  // Conclusion: I should add cleanup logic here anyway, since it is easy to
  // do and solves the problem.

  i3 = sCcbVector.begin();
  int terminatedCcbTime = 300, val;
  ImmAttrValueMap::iterator avi;
  ObjectMap::iterator oi = sObjectMap.find(immObjectDn);
  osafassert(oi != sObjectMap.end() && oi->second);
  ObjectInfo* immObject = oi->second;

  avi = immObject->mAttrValueMap.find(immMaxCcbs);

  if (avi != immObject->mAttrValueMap.end()) {
    osafassert(avi->second);
    val = (SaUint32T)avi->second->getValue_int();
  } else {
    val = IMMSV_MAX_CCBS;
  }

  if (sTerminatedCcbcount > (SaUint32T)2 * val) {
    terminatedCcbTime = 120;
  }
  sTerminatedCcbcount = 0;
  while (i3 != sCcbVector.end()) {
    if ((*i3)->mState > IMM_CCB_CRITICAL) {
      /* Garbage Collect ccbInfo more than five minutes old */
      if (osaf_timespec_compare(&(*i3)->mWaitStartTime, &kZeroSeconds) &&
          osaf_timer_is_expired_sec(&now, &(*i3)->mWaitStartTime,
                                    terminatedCcbTime)) {
        TRACE_5("Removing CCB %u terminated more than %d minutes ago",
                (*i3)->mId, terminatedCcbTime / 60);
        (*i3)->mState = IMM_CCB_ILLEGAL;
        delete (*i3);
        i3 = sCcbVector.erase(i3);
        continue;
      }
      sTerminatedCcbcount++;
    } else if (iAmCoord) {
      // Fetch CcbIds for Ccbs that have waited too long on an implementer
      // AND ccbIds for ccbs in critical and marked with PbeRestartedId.
      // Restarted PBE => try to recover outcome BEFORE timeout, making
      // recovery transparent to user!
      // Also handle the case of admin-op requesting abort of all non-critical
      // ccbs.
      TRACE("Checking active ccb %u for deadlock or blocked implementer",
            (*i3)->mId);
      TRACE("state:%u waitsart:%f PberestartId:%u", (*i3)->mState,
            osaf_timespec_to_double(&(*i3)->mWaitStartTime),
            (*i3)->mPbeRestartId);

      CcbImplementerMap::iterator cim;
      uint32_t max_oi_timeout = DEFAULT_TIMEOUT_SEC;
      bool abortCcb = sAbortNonCriticalCcbs && !((*i3)->mMutations.empty());
      if (abortCcb) {
        LOG_IN("abortCcb is true => set max_oi_timeout to 0");
        max_oi_timeout = 0;
      } else {
        for (cim = (*i3)->mImplementers.begin();
             cim != (*i3)->mImplementers.end(); ++cim) {
          if (cim->second->mImplementer->mTimeout > max_oi_timeout) {
            max_oi_timeout = cim->second->mImplementer->mTimeout;
          }
        }
      }

      uint32_t oi_timeout = ((*i3)->mState == IMM_CCB_CRITICAL)
                                ? DEFAULT_TIMEOUT_SEC
                                : max_oi_timeout;
      if ((osaf_timespec_compare(&(*i3)->mWaitStartTime, &kZeroSeconds) &&
           osaf_timer_is_expired_sec(&now, &(*i3)->mWaitStartTime,
                                     oi_timeout)) || /* normal timeout */
          ((*i3)->mPbeRestartId) ||                  /* CCB was critical when PBE restarted =>
                                                        Must ask new PBE for outcome */
          abortCcb) /* Request to abort ALL non critical CCBs */
      {
        if ((*i3)->mPbeRestartId) {
          oi_timeout = 0;
          TRACE_5("PBE restarted id:%u with ccb:%u in critical",
                  (*i3)->mPbeRestartId, (*i3)->mId);
        } else if (osaf_timespec_compare(&(*i3)->mWaitStartTime,
                                         &kZeroSeconds) &&
                   osaf_timer_is_expired_sec(&now, &(*i3)->mWaitStartTime,
                                             oi_timeout)) {
          oi_timeout = 0;
          TRACE_5("CCB %u timeout while waiting on implementer reply",
                  (*i3)->mId);
        }

        if (abortCcb) {
          LOG_NO(
              "CCB %u aborted by: immadm -o %u safRdn=immManagement,safApp=safImmService",
              (*i3)->mId, SA_IMM_ADMIN_ABORT_CCBS);
        }

        if ((*i3)->mState == IMM_CCB_CRITICAL) {
          /* Add extra wait time for large PBE transactions. */
          unsigned int mutations = (unsigned int)(*i3)->mMutations.size();
          int addSecs = mutations / 800;
          if (addSecs) {
            TRACE("Adding %u secconds to timeout on large ccb (%u) ccbId:%u",
                  addSecs, mutations, (*i3)->mId);
          }
          if ((*i3)->mPbeRestartId) {
            LOG_WA("Ccb: %u in critical state when PBE restarted", (*i3)->mId);
            ccbsStuck = 1;
          } else if (osaf_timer_is_expired_sec(
                         &now, &(*i3)->mWaitStartTime,
                         (DEFAULT_TIMEOUT_SEC + addSecs))) {
            LOG_WA("Timeout (%d) on transaction in critical state! ccb:%u",
                   (DEFAULT_TIMEOUT_SEC + addSecs), (*i3)->mId);
            ccbsStuck = 1;
          }
        } else if (!oi_timeout) {
          /* oi_timeout set to 0 means that the ccb should be added to the
             vector. CCB will be aborted only after the global abort generated
             here by the coord arrives back over fevs.*/
          ccbs.push_back((*i3)->mId); /*Non critical ccb to abort.*/
        }
      }
    }
    ++i3;
  }
  osafassert(sTerminatedCcbcount <= sCcbVector.size());

  if (sAbortNonCriticalCcbs) {
    LOG_IN("sAbortNonCriticalCcbs reset to false");
    sAbortNonCriticalCcbs = false; /* Reset. */
  }

  ci2 = sPbeRtReqContinuationMap.begin();
  while (ci2 != sPbeRtReqContinuationMap.end()) {
    // Timeout on PRT request continuation is hardwired but long.
    // It needs to be long to allow reply on larger batch jobs such as a
    // schema/class change with instance migration and slow file system.
    // It can not be infinite as that could cause a memory leak.
    if (osaf_timer_is_expired_sec(&now, &ci2->second.mCreateTime,
                                  (DEFAULT_TIMEOUT_SEC * 20))) {
      TRACE_5("Timeout on PbeRtReqContinuation %llu", ci2->first);
      pbePrtoReqs.push_back(ci2->second.mConn);
      ci2 = sPbeRtReqContinuationMap.erase(ci2);
    } else {
      ++ci2;
    }
  }

  /* Remove old implementer detach times. */
  iem = sImplDetachTime.begin();
  while (iem != sImplDetachTime.end()) {
    if (osaf_timer_is_expired_sec(&now, &iem->second.mCreateTime,
                                  DEFAULT_TIMEOUT_SEC)) {
      TRACE_5("Timeout on sImplDetachTime implid:%u", iem->first->mId);
      iem = sImplDetachTime.erase(iem);
    } else {
      ++iem;
    }
  }

  /* Check for progress on PbeRtMutations. */
  if (sPbeRtMutations.empty()) {
    if (sPbeRtMinContId) {
      TRACE_5("Progress: sPbeMinRtContId reset from %u to zero",
              sPbeRtMinContId);
      sPbeRtMinContId = 0;
    }

    if (sPbeRtBacklog) {
      TRACE_5("Progress: sPbeBacklog reset from %u to zero", sPbeRtBacklog);
      sPbeRtBacklog = 0;
    }
  } else {
    /* Check for blockage. */
    SaUint32T pbeMinContinuationId = 0xffffffff;
    for (ObjectMutationMap::iterator omuti = sPbeRtMutations.begin();
         omuti != sPbeRtMutations.end(); ++omuti) {
      if (omuti->second->mContinuationId < pbeMinContinuationId) {
        pbeMinContinuationId = omuti->second->mContinuationId;
      }
    }

    osafassert(pbeMinContinuationId);
    osafassert(pbeMinContinuationId < 0xffffffff);
    if (sPbeRtMinContId == pbeMinContinuationId) {
      pbeRtRegress = 2; /* No progress on oldest continuation. */
    } else {
      TRACE_5("Progress: sPbeMinRtContId raised from %u to %u", sPbeRtMinContId,
              pbeMinContinuationId);
      sPbeRtMinContId = pbeMinContinuationId;

      /* We are making progress but is backlog increasing? */
      SaUint32T newBacklog = (SaUint32T)sPbeRtMutations.size();
      if (newBacklog > sPbeRtBacklog) {
        /* Backlog increased. */
        TRACE_5("Backlog increased from %u to %u", sPbeRtBacklog, newBacklog);
        sPbeRtBacklog = newBacklog;
        pbeRtRegress = 2;
      }
    }
  }

  if (pbeRtRegress) {
    if (++sPbeRegressPeriods < 150) {
      /* Have some patience. */
      pbeRtRegress = 0;
      TRACE_5("sPbeRegressPeriods:%u", sPbeRegressPeriods);
    }
  } else {
    if (sPbeRegressPeriods) {
      TRACE_5("Progress: sPbeRegressPeriods reset from %u to zero",
              sPbeRegressPeriods);
      sPbeRegressPeriods = 0;
    }
  }

  if(protocol51710Allowed()) {
    unsigned int applierTimeout;

    ObjectMap::iterator oi = sObjectMap.find(immObjectDn);
    // This has already been checked in protocol51710Allowed()
    osafassert(oi != sObjectMap.end());

    ObjectInfo* immObject = oi->second;
    ImmAttrValueMap::iterator avi =
        immObject->mAttrValueMap.find(immMinApplierTimeout);
    osafassert(avi != immObject->mAttrValueMap.end());
    osafassert(!(avi->second->isMultiValued()));
    ImmAttrValue* valuep = avi->second;
    applierTimeout = valuep->getValue_int();

    if(applierTimeout > 0) {
      SaUint64T now;
      struct timespec ts;
      ImplementerVector::iterator ivi;

      // Convert applierTimeout to miliseconds
      applierTimeout *= 1000;

      osaf_clock_gettime(CLOCK_MONOTONIC, &ts);
      now = osaf_timespec_to_millis(&ts);

      for(ivi=sImplementerVector.begin();
              ivi != sImplementerVector.end();
              ++ivi) {
        if((*ivi)->mImplementerName.at(0) == '@'
                && (*ivi)->mApplierDisconnected > 0
                && ((*ivi)->mApplierDisconnected + applierTimeout) <= now) {
          appliers.push_back((*ivi)->mImplementerName);
        }
      }
    }
  }

  return ccbsStuck + pbeRtRegress;
}

/**
 * Creates an implementer name and registers an implementer.
 */
SaAisErrorT ImmModel::implementerSet(const IMMSV_OCTET_STRING* implementerName,
                                     SaUint32T conn, SaUint32T nodeId,
                                     SaUint32T implementerId,
                                     SaUint64T mds_dest, SaUint32T implTimeout,
                                     bool* discardImplementer) {
  SaAisErrorT err = SA_AIS_OK;
  CcbVector::iterator i;
  bool isLoading = (sImmNodeState == IMM_NODE_LOADING);

  TRACE_ENTER();

  *discardImplementer = false;

  if (immNotWritable() && !protocol43Allowed()) {
    TRACE_LEAVE();
    return SA_AIS_ERR_TRY_AGAIN;
  }

  if (!implTimeout) {
    implTimeout = DEFAULT_TIMEOUT_SEC;
  }

  // Check first if implementer name already exists.
  // If so check if occupied, if not re-use.
  size_t sz =
      strnlen((char*)implementerName->buf, (size_t)implementerName->size);
  const std::string implName((const char*)implementerName->buf, sz);

  if (implName.empty() || !nameCheck(implName)) {
    LOG_NO("ERR_INVALID_PARAM: Not a proper implementer name");
    err = SA_AIS_ERR_INVALID_PARAM;
    return err;
    // This return code not formally allowed here according to IMM standard.
  }

  ObjectMap::iterator oi = sObjectMap.find(immObjectDn);
  if (protocol51Allowed() && oi != sObjectMap.end() && !isLoading) {
    ObjectInfo* immObject = oi->second;
    ImmAttrValueMap::iterator avi = immObject->mAttrValueMap.find(immMaxImp);
    osafassert(avi != immObject->mAttrValueMap.end());
    osafassert(!(avi->second->isMultiValued()));
    ImmAttrValue* valuep = avi->second;
    unsigned int maxImp = valuep->getValue_int();
    if (sImplementerVector.size() >= maxImp) {
      LOG_NO(
          "ERR_NO_RESOURCES: maximum Implementers limit %d has been reached for the cluster",
          maxImp);
      TRACE_LEAVE();
      return SA_AIS_ERR_NO_RESOURCES;
    }
  }

  bool isApplier = (implName.at(0) == '@');

  ImplementerInfo* info = findImplementer(implName);
  if (info) {
    CcbInfo* ccb = NULL;
    ObjectMutationMap::iterator omit;
    ObjectInfo* obj = NULL;
    ObjectMap::iterator oi;
    ImplementerSetMap::iterator ismIter;

    if (info->mId) {
      TRACE_7("ERR_EXIST: Registered implementer already exists: %s",
              implName.c_str());
      err = SA_AIS_ERR_EXIST;
      TRACE_LEAVE();
      return err;
    }
    TRACE_7("Re-using implementer for %s", implName.c_str());
    // Implies we re-use ImplementerInfo. The previous
    // implementer-connection must have been cleared or crashed.
    osafassert(!info->mConn);
    osafassert(!info->mNodeId);

    // Check objects for attachment to this implementer and
    // verify that no ccb is active on them. Iterate over CCB vector!
    for (i = sCcbVector.begin(); i != sCcbVector.end(); ++i) {
      ccb = (*i);
      if (ccb->isActive()) {
        for (omit = ccb->mMutations.begin(); omit != ccb->mMutations.end();
             ++omit) {
          oi = sObjectMap.find(omit->first);
          osafassert(oi != sObjectMap.end());
          obj = oi->second;

          if (obj->mImplementer == info) {
            osafassert(!isApplier);
            TRACE(
                "TRY_AGAIN: ccb %u is active on object '%s' bound to OI name "
                "'%s'. Can not set re-attach implementer",
                ccb->mId, omit->first.c_str(), implName.c_str());
            err = SA_AIS_ERR_TRY_AGAIN;
            goto done;
          }

          if (isApplier && conn) {
            if (!obj->mClassInfo->mAppliers.empty()) {
              ImplementerSet::iterator ii = obj->mClassInfo->mAppliers.begin();
              for (; ii != obj->mClassInfo->mAppliers.end(); ++ii) {
                if ((*ii) == info) {
                  TRACE(
                      "TRY_AGAIN: ccb %u is active on object '%s' "
                      "bound to class applier '%s'. Can not re-attach applier",
                      ccb->mId, omit->first.c_str(), implName.c_str());
                  *discardImplementer = true;
                  err = SA_AIS_ERR_TRY_AGAIN;
                  goto done;
                }
              }
            }

            ismIter = sObjAppliersMap.find(omit->first);
            if (ismIter != sObjAppliersMap.end()) {
              ImplementerSet* implSet = ismIter->second;
              if (implSet->find(info) != implSet->end()) {
                TRACE(
                    "TRY_AGAIN: ccb %u is active on object '%s' "
                    "bound to object applier '%s'. Can not re-attach applier",
                    ccb->mId, omit->first.c_str(), implName.c_str());
                *discardImplementer = true;
                err = SA_AIS_ERR_TRY_AGAIN;
                goto done;
              }
            }
          }
        }
      }
    }
  } else {
    TRACE_7("Creating FRESH NEW implementer for %s", implName.c_str());
    info = new ImplementerInfo;
    info->mImplementerName = implName;
    sImplementerVector.push_back(info);
  }

  if ((sImplementerVector.size() > 300) &&
      (sImplementerVector.size() % 30 == 0)) {
    LOG_WA("More than 300 distinct implementer names used:%u",
           (unsigned int)sImplementerVector.size());
  }

  info->mId = implementerId;
  info->mConn = conn;  // conn!=NULL only on primary.
  info->mNodeId = nodeId;
  info->mAdminOpBusy = 0;
  info->mMds_dest = mds_dest;
  info->mDying = false;
  info->mTimeout = implTimeout;

  if (isApplier) {
    info->mApplier = true;
    info->mApplierDisconnected = 0;
    LOG_NO("Implementer (applier) connected: %u (%s) <%u, %x>", info->mId,
           info->mImplementerName.c_str(), info->mConn, info->mNodeId);
  } else {
    info->mApplier = false;
    LOG_NO("Implementer connected: %u (%s) <%u, %x>", info->mId,
           info->mImplementerName.c_str(), info->mConn, info->mNodeId);
    ImplementerEvtMap::iterator iem = sImplDetachTime.find(info);
    if (iem != sImplDetachTime.end()) {
      sImplDetachTime.erase(iem);
    }
  }

  if (implName == std::string(OPENSAF_IMM_PBE_IMPL_NAME)) {
    TRACE_7(
        "Implementer %s for PBE created - "
        "check for ccbs stuck in critical",
        implName.c_str());
    /* If we find any, then surgically replace the implId of the implAssoc with
        the new implId of the newly reincarnated PBE implementer.*/

    for (i = sCcbVector.begin(); i != sCcbVector.end(); ++i) {
      CcbInfo* ccb = (*i);
      if (ccb->mState == IMM_CCB_CRITICAL) {
        if ((sImmNodeState != IMM_NODE_FULLY_AVAILABLE) &&
            (sImmNodeState != IMM_NODE_R_AVAILABLE)) {
          LOG_ER(
              "ImmModel::implementerSet found Ccbs in critical state yet "
              "immnd is not in a proper state: %u",
              sImmNodeState);
          abort();
        }

        CcbImplementerMap::iterator isi;
        for (isi = ccb->mImplementers.begin(); isi != ccb->mImplementers.end();
             ++isi) {
          ImplementerCcbAssociation* oldImplAssoc = isi->second;
          if (oldImplAssoc->mWaitForImplAck && oldImplAssoc->mImplementer) {
            ImplementerInfo* impInfo = oldImplAssoc->mImplementer;
            if (impInfo->mImplementerName == implName) {
              SaUint32T oldImplId = isi->first;
              if (oldImplId == info->mId) {
                continue;
              } /*already replaced*/
              ccb->mImplementers.erase(isi);
              ccb->mImplementers[info->mId] = oldImplAssoc;
              TRACE_7("Replaced implid %u with %u", oldImplId, info->mId);
              ccb->mPbeRestartId = info->mId;
              osaf_clock_gettime(
                  CLOCK_MONOTONIC,
                  &ccb->mWaitStartTime); /*Reset timer on new impl*/
              /* Can only be one PBE impl asoc*/
              break; /* out of for(isi = ccb->mImplementers....*/
            }
          }
        }
      }
    }
  }
/*
  ImplementerVector::iterator i;
  for(i = sImplementerVector.begin(); i != sImplementerVector.end(); ++i) {
  info = (*i);
  TRACE_7("Implementer id:%u name:%s(%zu) node:%x conn:%u mDying:%s "
  "mMds_dest:%llu AdminOpBusy:%u",
  info->mId,
  info->mImplementerName.c_str(),
  info->mImplementerName.size(),
  info->mNodeId,
  info->mConn,
  (info->mDying)?"TRUE":"FALSE",
  info->mMds_dest,
  info->mAdminOpBusy);
  }
 */
done:
  TRACE_LEAVE();
  return err;
}

/**
 * Registers a specific implementer for a class and all its instances
 */
SaAisErrorT ImmModel::classImplementerSet(const struct ImmsvOiImplSetReq* req,
                                          SaUint32T conn, unsigned int nodeId,
                                          SaUint32T* ccbId) {
  SaAisErrorT err = SA_AIS_OK;
  ObjectSet::iterator os;
  TRACE_ENTER();
  osafassert(ccbId);
  *ccbId = 0;

  ClassInfo* classInfo = NULL;
  ClassMap::iterator i1;
  ImplementerInfo* info = NULL;

  if (immNotWritable() && !protocol43Allowed()) {
    TRACE_LEAVE();
    return SA_AIS_ERR_TRY_AGAIN;
  }

  size_t sz = strnlen((const char*)req->impl_name.buf, req->impl_name.size);
  std::string className((const char*)req->impl_name.buf, sz);

  if (!schemaNameCheck(className)) {
    LOG_NO("ERR_INVALID_PARAM: Not a proper class name");
    err = SA_AIS_ERR_INVALID_PARAM;
    goto done;
  }

  info = findImplementer(req->impl_id);
  if (!info) {
    LOG_IN("ERR_BAD_HANDLE: Not a correct implementer handle %u", req->impl_id);
    err = SA_AIS_ERR_BAD_HANDLE;
    goto done;
  }

  if ((info->mConn != conn) || (info->mNodeId != nodeId)) {
    LOG_IN(
        "ERR_BAD_HANDLE: Not a correct implementer handle "
        "conn:%u nodeId:%x",
        conn, nodeId);
    err = SA_AIS_ERR_BAD_HANDLE;
    goto done;
  }

  if (info->mApplier && !conn) {
    // Applier is not on this node. No need to proceed further
    err = SA_AIS_OK;
    goto done;
  }

  // conn is NULL on all nodes except primary.
  // At these other nodes the only info on implementer existence
  // is that the nodeId is non-zero. The nodeId is the nodeId of
  // the node where the implementer resides.

  i1 = sClassMap.find(className);
  if (i1 == sClassMap.end()) {
    TRACE_7("ERR_NOT_EXIST: class '%s' does not exist", className.c_str());
    err = SA_AIS_ERR_NOT_EXIST;
    goto done;
  }

  classInfo = i1->second;
  if (classInfo->mCategory == SA_IMM_CLASS_RUNTIME) {
    LOG_NO(
        "ERR_BAD_OPERATION: Class '%s' is a runtime "
        "class, not allowed to set class implementer.",
        className.c_str());
    err = SA_AIS_ERR_BAD_OPERATION;
    goto done;
  }

  if (info->mApplier) {
    /* Prevent adding class applier to CCBs in progress.
       Iterate over ccb-vector instead of class extent.
       The ccb interference check needs to be done before
       the idempotency check, because we dont want an applier
       seeing part of a ccb, even if class-impl is idempotent.
    */
    CcbVector::iterator i1;
    for (i1 = sCcbVector.begin(); i1 != sCcbVector.end(); ++i1) {
      if ((*i1)->isActive()) {
        CcbInfo* ccb = (*i1);
        ObjectMutationMap::iterator omit;
        for (omit = ccb->mMutations.begin(); omit != ccb->mMutations.end();
             ++omit) {
          ObjectMap::iterator oi = sObjectMap.find(omit->first);
          osafassert(oi != sObjectMap.end());
          ObjectInfo* object = oi->second;
          if (object->mClassInfo == classInfo) {
            LOG_NO(
                "ERR_TRY_AGAIN: ccb %u is active on object %s "
                "of class %s. Can not add class applier",
                ccb->mId, omit->first.c_str(), className.c_str());
            /*err = SA_AIS_ERR_BUSY;  Not allowed according to spec.
              Neither is SA_AIS_ERR_NO_RESOURCES!!
              Only alternative is TRY_AGAIN which is dangerous here
              because the wait may be unbounded. For appliers we
              dont want to abort (non critical) ccbs because appliers
              are listeners, not participants with veto rights on the ccb.
            */
            err = SA_AIS_ERR_TRY_AGAIN;
            goto done;
          }
        }
      }
    }

    if (classInfo->mAppliers.find(info) != classInfo->mAppliers.end()) {
      /* Idempotency */
      TRACE_7("Applier %s ALREADY set for class %s",
              info->mImplementerName.c_str(), className.c_str());
      err = SA_AIS_ERR_NO_BINDINGS;
      goto done;
    }

    /* Prevent mixing of ClassImplementerSet & ObjectImplementerSet for
       same applier over class and instances of class. */
    ImplementerSetMap::iterator ismIter;
    for (ismIter = sObjAppliersMap.begin(); ismIter != sObjAppliersMap.end();
         ++ismIter) {
      ObjectMap::iterator oi = sObjectMap.find(ismIter->first);
      osafassert(oi != sObjectMap.end());
      ObjectInfo* object = oi->second;
      if (object->mClassInfo == classInfo) {
        ImplementerSet* implSet = ismIter->second;
        if (implSet->find(info) != implSet->end()) {
          LOG_IN(
              "ERR_EXIST: Applier %s is already object applier "
              "for object %s => can not also be applier for class",
              info->mImplementerName.c_str(), ismIter->first.c_str());
          err = SA_AIS_ERR_EXIST;
          goto done;
        }
      }
    }

    if (immNotWritable() || is_sync_aborting()) {
      /* This was not the idempotency case for class-applier set
         and sync is on-going => reject this mutation for now.  */
      err = SA_AIS_ERR_TRY_AGAIN;
      goto done;
    }

    /* Normal class applier set. */
    LOG_IN("Applier %s set for class %s", info->mImplementerName.c_str(),
           className.c_str());
    classInfo->mAppliers.insert(info);
    goto done;
  }

  /* Regular OI */

  /* Prevent mixing of Class/Object ImplementerSet. This has to
     override any previous ObjectImplementerSet for same main-implementer
     over the class and instances of class. But ClassImplementerSet
     can not override previous ObjectImplementerSet by *other* OI.
     At the same time check for CCB interference. We do class extent
     iteration here since there is nothing like sObjAppliersMap for
     main implementers. The main implementer mapping is expected
     to be more long term stable than the applier maping.
  */
  for (os = classInfo->mExtent.begin(); os != classInfo->mExtent.end(); ++os) {
    ObjectInfo* obj = *os;
    osafassert(obj->mClassInfo == classInfo);
    if (obj->mImplementer) {
      std::string objDn;
      getObjectName(obj, objDn); /* External name form */
      if (obj->mImplementer != info) {
        LOG_NO(
            "ERR_EXIST: Object '%s' already has implementer "
            "%s != %s",
            objDn.c_str(), obj->mImplementer->mImplementerName.c_str(),
            info->mImplementerName.c_str());
        err = SA_AIS_ERR_EXIST;
        goto done;
      }
    }

    if (obj->mCcbId) {
      CcbVector::iterator i1 = std::find_if(
          sCcbVector.begin(), sCcbVector.end(), CcbIdIs(obj->mCcbId));
      if (i1 != sCcbVector.end() && (*i1)->isActive()) {
        std::string objDn;
        getObjectName(obj, objDn); /* External name form */
        LOG_NO(
            "ERR_TRY_AGAIN: ccb %u is active on object %s "
            "of class %s. Can not add class implementer",
            obj->mCcbId, objDn.c_str(), className.c_str());
        err = SA_AIS_ERR_TRY_AGAIN;
        /*err = SA_AIS_ERR_BUSY; Not allowed according top spec.
           But ERR_BUSY *is* allowed in the corresponding situation for
           saImmOiClassImplementerRelease. Possibly this is because
           ClassImplementerSet will add validation/protection and so
           should override the progress of CCBs that have bypassed
           validation.
           We can only attempt to abort the ccb and only non critical
           ccbs can be aborted. But critical ccbs are already comitting
           and should hopefully complete the commit soon.
           We only abort one non critical ccb per try of this function.
        */
        if (ccbId && ((*i1)->mState < IMM_CCB_CRITICAL)) {
          *ccbId = (*i1)->mId;
          LOG_NO(
              "Trying to abort ccb %u to allow implementer %s to protect class %s",
              *ccbId, info->mImplementerName.c_str(), className.c_str());
          /* We have located a non critical ccb that blocks this operation.
             Skip checking the remaining ccbs for now. Return and abort this
             one. */
          goto done;
        }
      }
    }
  }  // for

  if (err != SA_AIS_OK) {
    LOG_NO(
        "Implementer %s can not protect class %s due to active ccb in critical",
        info->mImplementerName.c_str(), className.c_str());
    goto done;
  }

  if (classInfo->mImplementer) { /* Class implementer is already set. */
    if (classInfo->mImplementer == info) {
      /* Idempotency */
      TRACE_7("Class '%s' already has %s as class implementer ",
              className.c_str(), info->mImplementerName.c_str());
      err = SA_AIS_ERR_NO_BINDINGS;
    } else {
      LOG_NO(
          "ERR_EXIST: Class '%s' already has OTHER class implementer "
          "%s != %s",
          className.c_str(), classInfo->mImplementer->mImplementerName.c_str(),
          info->mImplementerName.c_str());
      err = SA_AIS_ERR_EXIST;
    }

    goto done;
  }

  osafassert(err == SA_AIS_OK);

  if (immNotWritable() || is_sync_aborting()) {
    /* This was not the idempotency case for class-implementer set and sync
       is on-going => reject this mutation for now. */
    err = SA_AIS_ERR_TRY_AGAIN;
    goto done;
  }

  classInfo->mImplementer = info;
  LOG_NO("implementer for class '%s' is %s => class extent is safe.",
         className.c_str(), info->mImplementerName.c_str());
  /*ABT090225
    classImplementerSet and objectImplementerSet
    are mutually exclusive. This has not been
    explicitly clear in the IMM standard until
    release A.03.01.
  */

  /* Set the main implementer pointer in each object of the class extent. */
  for (os = classInfo->mExtent.begin(); os != classInfo->mExtent.end(); ++os) {
    (*os)->mImplementer = classInfo->mImplementer;
  }

done:
  TRACE_LEAVE();

  return err;
}

/**
 * Releases a specific implementer for a class and all its instances
 */
SaAisErrorT ImmModel::classImplementerRelease(
    const struct ImmsvOiImplSetReq* req, SaUint32T conn, unsigned int nodeId) {
  SaAisErrorT err = SA_AIS_OK;
  ClassInfo* classInfo = NULL;
  ClassMap::iterator i1;
  ImplementerInfo* info = NULL;
  ObjectSet::iterator os;
  CcbVector::iterator ccbi;
  TRACE_ENTER();

  if (immNotWritable() || is_sync_aborting()) {
    TRACE_LEAVE();
    return SA_AIS_ERR_TRY_AGAIN;
  }

  size_t sz = strnlen((const char*)req->impl_name.buf, req->impl_name.size);
  std::string className((const char*)req->impl_name.buf, sz);

  if (!schemaNameCheck(className)) {
    LOG_NO("ERR_INVALID_PARAM: Not a proper class name");
    err = SA_AIS_ERR_INVALID_PARAM;
    goto done;
  }

  info = findImplementer(req->impl_id);
  if (!info) {
    LOG_IN("ERR_BAD_HANDLE: Not a correct implementer handle %u", req->impl_id);
    err = SA_AIS_ERR_BAD_HANDLE;
    goto done;
  }

  if ((info->mConn != conn) || (info->mNodeId != nodeId)) {
    LOG_IN("ERR_BAD_HANDLE: Not a correct implementer handle conn:%u nodeId:%x",
           conn, nodeId);
    err = SA_AIS_ERR_BAD_HANDLE;
    goto done;
  }

  if (info->mApplier && !conn) {
    // Applier is not on this node. No need to proceed further
    err = SA_AIS_OK;
    goto done;
  }

  // conn is NULL on all nodes except primary.
  // At these other nodes the only info on implementer existence
  // is that the nodeId is non-zero. The nodeId is the nodeId of
  // the node where the implementer resides.

  i1 = sClassMap.find(className);
  if (i1 == sClassMap.end()) {
    TRACE_7("ERR_NOT_EXIST: class '%s' does not exist", className.c_str());
    err = SA_AIS_ERR_NOT_EXIST;
    goto done;
  }

  classInfo = i1->second;
  if (classInfo->mCategory == SA_IMM_CLASS_RUNTIME) {
    LOG_NO("ERR_BAD_OPERATION: Class '%s' is a runtime class.",
           className.c_str());
    err = SA_AIS_ERR_BAD_OPERATION;
    goto done;
  }

  if (info->mApplier) {
    if (classInfo->mAppliers.find(info) == classInfo->mAppliers.end()) {
      /* No class applier set.
         Prevent class applier release when set as individual object applier. */

      ImplementerSetMap::iterator ismIter;
      for (ismIter = sObjAppliersMap.begin(); ismIter != sObjAppliersMap.end();
           ++ismIter) {
        ObjectMap::iterator oi = sObjectMap.find(ismIter->first);
        osafassert(oi != sObjectMap.end());
        ObjectInfo* object = oi->second;
        if (object->mClassInfo == classInfo) {
          ImplementerSet* implSet = ismIter->second;
          if (implSet->find(info) != implSet->end()) {
            /* No mixing of ClassImplementerRelease & ObjectImplementerRelease
             */
            LOG_IN(
                "ERR_NOT_EXIST: Applier %s is object applier "
                "for object %s => can not release as applier for class",
                info->mImplementerName.c_str(), ismIter->first.c_str());
            err = SA_AIS_ERR_NOT_EXIST;
            goto done;
          }
        }
      }

      /* Idempotency. */
      TRACE_7("Applier %s was not set for class %s",
              info->mImplementerName.c_str(), className.c_str());
      goto done;
    }

    /* Prevent release of applier when CCBs are in progress.
       Iterate over ccb-vector instead of class extent. */
    for (ccbi = sCcbVector.begin(); ccbi != sCcbVector.end(); ++ccbi) {
      if ((*ccbi)->isActive()) {
        CcbInfo* ccb = (*ccbi);
        ObjectMutationMap::iterator omit;
        for (omit = ccb->mMutations.begin(); omit != ccb->mMutations.end();
             ++omit) {
          ObjectMap::iterator oi = sObjectMap.find(omit->first);
          osafassert(oi != sObjectMap.end());
          ObjectInfo* object = oi->second;
          if (object->mClassInfo == classInfo) {
            LOG_NO(
                "ERR_BUSY: ccb %u is active on object %s "
                "of class %s. Can not release class applier",
                ccb->mId, omit->first.c_str(), className.c_str());
            err = SA_AIS_ERR_BUSY;
            goto done;
          }
        }
      }
    }

    /* Normal release of class-applier. */
    TRACE_7("Applier %s released from class %s", info->mImplementerName.c_str(),
            className.c_str());
    classInfo->mAppliers.erase(info);
    goto done;
  }

  /* Regular OI */

  if (!classInfo->mImplementer) { /* NO Class implementer is set. */
    /* Prevent class implementer release when other individual object
     * implementer set */
    for (os = classInfo->mExtent.begin(); os != classInfo->mExtent.end();
         ++os) {
      ObjectInfo* obj = *os;
      osafassert(obj->mClassInfo == classInfo);
      if (obj->mImplementer && obj->mImplementer != info) {
        std::string objDn;
        getObjectName(obj, objDn); /* External name form */
        LOG_NO(
            "ERR_NOT_EXIST: Release of class implementer for "
            "class %s conflicts with current object implementorship "
            "%s for object %s",
            className.c_str(), obj->mImplementer->mImplementerName.c_str(),
            objDn.c_str());
        err = SA_AIS_ERR_NOT_EXIST;
        goto done;
      }
    }
    /* Idempotency */
    TRACE_7("Class '%s' has no implementer", className.c_str());
    goto done;
  }

  if (classInfo->mImplementer !=
      info) { /* DIFFERENT Class implementer is set. */
    TRACE_7("ERR_NOT_EXIST: Class '%s' has implementer %s != %s",
            className.c_str(),
            classInfo->mImplementer->mImplementerName.c_str(),
            info->mImplementerName.c_str());
    err = SA_AIS_ERR_NOT_EXIST;
    goto done;
  }

  /* Prevent release of class implementer when CCBs are in progress on
     instances. Iterate over ccb-vector instead of class extent. */
  for (ccbi = sCcbVector.begin(); ccbi != sCcbVector.end(); ++ccbi) {
    if ((*ccbi)->isActive()) {
      CcbInfo* ccb = (*ccbi);
      ObjectMutationMap::iterator omit;
      for (omit = ccb->mMutations.begin(); omit != ccb->mMutations.end();
           ++omit) {
        ObjectMap::iterator oi = sObjectMap.find(omit->first);
        osafassert(oi != sObjectMap.end());
        ObjectInfo* object = oi->second;
        if (object->mClassInfo == classInfo) {
          LOG_NO(
              "ERR_BUSY: ccb %u is active on object %s "
              "of class %s. Can not release class implementer",
              ccb->mId, omit->first.c_str(), className.c_str());
          err = SA_AIS_ERR_BUSY;
          goto done;
        }
      }
    }
  }

  LOG_NO("implementer for class '%s' is released => class extent is UNSAFE",
         className.c_str());
  for (os = classInfo->mExtent.begin(); os != classInfo->mExtent.end(); ++os) {
    (*os)->mImplementer = 0;
    this->clearImplName(*os);
  }
  classInfo->mImplementer = 0;

done:
  TRACE_LEAVE();
  return err;
}

/**
 * Registers a specific implementer for an object and possibly sub-objects
 */
SaAisErrorT ImmModel::objectImplementerSet(const struct ImmsvOiImplSetReq* req,
                                           SaUint32T conn, unsigned int nodeId,
                                           SaUint32T* ccbId) {
  SaAisErrorT err = SA_AIS_OK;
  bool idempotencyOk = true; /* Hypothesis to be falsified. */
  ImplementerInfo* info = NULL;
  ObjectMap::iterator i1;
  ObjectInfo* rootObj = NULL;
  osafassert(ccbId);
  *ccbId = 0;
  TRACE_ENTER();

  if (immNotWritable() && !protocol43Allowed()) {
    TRACE_LEAVE();
    return SA_AIS_ERR_TRY_AGAIN;
  }

  size_t sz = strnlen((const char*)req->impl_name.buf, req->impl_name.size);
  std::string objectName((const char*)req->impl_name.buf, sz);

  if (objectName.empty() ||
      !(nameCheck(objectName) || nameToInternal(objectName))) {
    LOG_NO("ERR_INVALID_PARAM: Not a proper object DN");
    err = SA_AIS_ERR_INVALID_PARAM;
    goto done;
  }

  info = findImplementer(req->impl_id);
  if (!info) {
    LOG_IN("ERR_BAD_HANDLE: Not a correct implementer handle %u", req->impl_id);
    err = SA_AIS_ERR_BAD_HANDLE;
    goto done;
  }

  if ((info->mConn != conn) || (info->mNodeId != nodeId)) {
    LOG_IN("ERR_BAD_HANDLE: Not a correct implementer handle conn:%u nodeId:%x",
           conn, nodeId);
    err = SA_AIS_ERR_BAD_HANDLE;
    goto done;
  }

  if (info->mApplier && !conn) {
    // Applier is not on this node. No need to proceed further
    err = SA_AIS_OK;
    goto done;
  }

  // conn is NULL on all nodes except primary.
  // At these other nodes the only info on implementer existence
  // is that the nodeId is non-zero. The nodeId is the nodeId of
  // the node where the implementer resides.

  // I need to run two passes if scope != ONE
  // This because if I fail on ONE object then I have to revert
  // the implementer on all previously set in this op.

  i1 = sObjectMap.find(objectName);
  if (i1 == sObjectMap.end()) {
    TRACE_7("ERR_NOT_EXIST: object '%s' does not exist", objectName.c_str());
    err = SA_AIS_ERR_NOT_EXIST;
    goto done;
  }

  rootObj = i1->second;
  for (int doIt = 0; doIt < 2 && err == SA_AIS_OK; ++doIt) {
    // ObjectInfo* objectInfo = i1->second;
    err = setOneObjectImplementer(objectName, rootObj, info, doIt, ccbId);
    if (err == SA_AIS_ERR_NO_BINDINGS) {
      err = SA_AIS_OK;
    } else {
      idempotencyOk = false;
    }
    SaImmScopeT scope = (SaImmScopeT)req->scope;
    if (err == SA_AIS_OK && scope != SA_IMM_ONE) {
      // Find all sub objects to the root object
      // Warning re-using iterator i1 inside this loop!!!
      SaUint32T childCount = rootObj->mChildCount;
      for (i1 = sObjectMap.begin();
           i1 != sObjectMap.end() && err == SA_AIS_OK && childCount; ++i1) {
        std::string subObjName = i1->first;
        if (subObjName.length() > objectName.length()) {
          size_t pos = subObjName.length() - objectName.length();
          if ((subObjName.rfind(objectName, pos) == pos) &&
              (subObjName[pos - 1] == ',')) {
            if (scope == SA_IMM_SUBTREE || checkSubLevel(subObjName, pos)) {
              --childCount;
              ObjectInfo* subObj = i1->second;
              err = setOneObjectImplementer(subObjName, subObj, info, doIt,
                                            ccbId);
              if (err == SA_AIS_ERR_NO_BINDINGS) {
                err = SA_AIS_OK;
              } else {
                idempotencyOk = false;
              }
              if (!childCount) {
                TRACE("Cutoff in impl-set-loop by childCount");
              }
            }  // if
          }    // if
        }      // if
      }        // for
    }          // if
    if ((err == SA_AIS_OK) && idempotencyOk) {
      /* Idempotency. Bogus error code returned to allow local
         detection & avoiding fevs broadcast. */
      err = SA_AIS_ERR_NO_BINDINGS;
    }
  }  // for

done:
  TRACE_LEAVE();
  return err;
}

/**
 * Releases a specific implementer for an object and possibly sub-objects
 */
SaAisErrorT ImmModel::objectImplementerRelease(
    const struct ImmsvOiImplSetReq* req, SaUint32T conn, unsigned int nodeId) {
  SaAisErrorT err = SA_AIS_OK;
  ObjectInfo* rootObj = NULL;
  ImplementerInfo* info = NULL;
  ObjectMap::iterator i1;
  TRACE_ENTER();

  if (immNotWritable() || is_sync_aborting()) {
    TRACE_LEAVE();
    return SA_AIS_ERR_TRY_AGAIN;
  }

  size_t sz = strnlen((const char*)req->impl_name.buf, req->impl_name.size);
  std::string objectName((const char*)req->impl_name.buf, sz);

  if (objectName.empty() ||
      !(nameCheck(objectName) || nameToInternal(objectName))) {
    LOG_NO("ERR_INVALID_PARAM: Not a proper object DN");
    err = SA_AIS_ERR_INVALID_PARAM;
    goto done;
  }

  info = findImplementer(req->impl_id);
  if (!info) {
    LOG_IN("ERR_BAD_HANDLE: Not a correct implementer handle %u", req->impl_id);
    err = SA_AIS_ERR_BAD_HANDLE;
    goto done;
  }

  if ((info->mConn != conn) || (info->mNodeId != nodeId)) {
    LOG_IN("ERR_BAD_HANDLE: Not a correct implementer handle conn:%u nodeId:%x",
           conn, nodeId);
    err = SA_AIS_ERR_BAD_HANDLE;
    goto done;
  }

  if (info->mApplier && !conn) {
    // Applier is not on this node. No need to proceed further
    err = SA_AIS_OK;
    goto done;
  }

  // conn is NULL on all nodes except primary.
  // At these other nodes the only info on implementer existence
  // is that the nodeId is non-zero. The nodeId is the nodeId of
  // the node where the implementer resides.

  // I need to run two passes if scope != ONE
  // This because if I fail on ONE object then I have to revert
  // the implementer on all previously set in this op.

  i1 = sObjectMap.find(objectName);
  if (i1 == sObjectMap.end()) {
    TRACE_7("ERR_NOT_EXIST: object '%s' does not exist", objectName.c_str());
    err = SA_AIS_ERR_NOT_EXIST;
    goto done;
  }

  rootObj = i1->second;
  for (int doIt = 0; doIt < 2 && err == SA_AIS_OK; ++doIt) {
    // ObjectInfo* objectInfo = i1->second;
    err = releaseImplementer(objectName, rootObj, info, doIt);
    SaImmScopeT scope = (SaImmScopeT)req->scope;
    if (err == SA_AIS_OK && scope != SA_IMM_ONE) {
      // Find all sub objects to the root object
      // Warning re-using iterator i1 inside this loop
      SaUint32T childCount = rootObj->mChildCount;
      for (i1 = sObjectMap.begin();
           i1 != sObjectMap.end() && err == SA_AIS_OK && childCount; ++i1) {
        std::string subObjName = i1->first;
        if (subObjName.length() > objectName.length()) {
          size_t pos = subObjName.length() - objectName.length();
          if ((subObjName.rfind(objectName, pos) == pos) &&
              (subObjName[pos - 1] == ',')) {
            if (scope == SA_IMM_SUBTREE || checkSubLevel(subObjName, pos)) {
              --childCount;
              ObjectInfo* subObj = i1->second;
              err = releaseImplementer(subObjName, subObj, info, doIt);
              if (!childCount) {
                TRACE("Cutoff in impl-release-loop by childCount");
              }
            }  // if
          }    // if
        }      // if
      }        // for
    }          // if
  }            // for

done:
  TRACE_LEAVE();
  return err;
}

/**
 * Helper function to set object implementer.
 */
SaAisErrorT ImmModel::setOneObjectImplementer(std::string objectName,
                                              ObjectInfo* obj,
                                              ImplementerInfo* info, bool doIt,
                                              SaUint32T* ccbId) {
  SaAisErrorT err = SA_AIS_OK;
  /*TRACE_ENTER();*/
  ClassInfo* classInfo = obj->mClassInfo;
  if (classInfo->mCategory == SA_IMM_CLASS_RUNTIME) {
    osafassert(!doIt);
    LOG_NO(
        "ERR_BAD_OPERATION: Object '%s' is a runtime object, "
        "not allowed to set object implementer.",
        objectName.c_str());
    err = SA_AIS_ERR_BAD_OPERATION;
    goto done;
  }

  if (obj->mCcbId && !doIt) {
    /* Dont allow implementer or applier to attatch to objects with
       an active CCB. The result could be that they receive upcalls
       for only part of the modifications that they should have seen.
    */
    CcbVector::iterator i1 = std::find_if(sCcbVector.begin(), sCcbVector.end(),
                                          CcbIdIs(obj->mCcbId));
    if (i1 != sCcbVector.end() && (*i1)->isActive()) {
      LOG_NO(
          "ERR_TRY_AGAIN: ccb %u is active on object %s can not "
          "add object %s",
          obj->mCcbId, objectName.c_str(),
          (info->mApplier) ? "applier" : "implementer");
      /*err = SA_AIS_ERR_BUSY; Not allowed according to spec.
         But ERR_BUSY *is* allowed in the corresponding situation for
         saImmOiObjectImplementerRelease !
         We could use ERR_NO_RESOURCES becuase it *is* alowed
         for saImmOiObjectImplementerSet. But NO_RESOURCES is
         *not* allowed for saImmOiClassImplementerSet! To keep
         things simple for the user, we will return TRY_AGAIN.
         For appliers we dont want to abort (non critical) ccbs
         because appliers are listeneers, not participants with veto
         rights on the ccb.
       */
      err = SA_AIS_ERR_TRY_AGAIN;
      if (!(info->mApplier) && ccbId && ((*i1)->mState < IMM_CCB_CRITICAL)) {
        *ccbId = (*i1)->mId;
        LOG_NO(
            "Trying to abort ccb %u to allow implementer %s to protect object %s",
            *ccbId, info->mImplementerName.c_str(), objectName.c_str());
        /* We have located a non critical ccb that blocks this operation.
           Skip checking the remaining ccbs for now. Return and abort this one.
         */
        goto done;
      }
    }
  }

  if (err != SA_AIS_OK) {
    LOG_NO(
        "Implementer %s can not protect object %s due to active ccb in critical",
        info->mImplementerName.c_str(), objectName.c_str());
    goto done;
  }

  if (info->mApplier) {
    ImplementerSetMap::iterator ismIter = sObjAppliersMap.find(objectName);
    ImplementerSet* implSet =
        (ismIter == sObjAppliersMap.end()) ? NULL : (ismIter->second);

    if (doIt) {
      if (implSet == NULL) {
        implSet = new ImplementerSet;
        sObjAppliersMap[objectName] = implSet;
      }

      if ((implSet->find(info) == implSet->end())) {
        implSet->insert(info);
        TRACE_5("applier %s set for object '%s'",
                info->mImplementerName.c_str(), objectName.c_str());
      } else {
        /* Idempotency */
        TRACE_5("applier %s ALREADY set for object '%s'",
                info->mImplementerName.c_str(), objectName.c_str());
        err = SA_AIS_ERR_NO_BINDINGS;
      }
    } else {  // not doIt  => do checks
      if (classInfo->mAppliers.find(info) != classInfo->mAppliers.end()) {
        /* No mixing of ClassImplementerSet & ObjectImplementerSet */
        LOG_IN(
            "ERR_EXIST: Applier %s is already class applier "
            "for the class of object %s",
            info->mImplementerName.c_str(), objectName.c_str());
        err = SA_AIS_ERR_EXIST;
        goto done;
      } else if (immNotWritable() || is_sync_aborting()) {
        /* Sync ongoing => Only idempotent object-applier set allowed */
        if (implSet != NULL && implSet->find(info) != implSet->end()) {
          TRACE_5("Idempotent object-implset for applier '%s' during sync",
                  info->mImplementerName.c_str());
          err = SA_AIS_ERR_NO_BINDINGS;
        } else {
          err = SA_AIS_ERR_TRY_AGAIN;
          ;
        }
        goto done;
      }
    }
  } else { /* regular OI */
    if (obj->mImplementer && (obj->mImplementer != info)) {
      osafassert(!doIt);
      TRACE_7("ERR_EXIST: Object '%s' already has implementer %s != %s",
              objectName.c_str(), obj->mImplementer->mImplementerName.c_str(),
              info->mImplementerName.c_str());
      err = SA_AIS_ERR_EXIST;
      goto done;
    } else { /* obj->mImplementer == info. But check that it is not class-impl
              */
      if (doIt) {
        /* Covers the idempotency case. */
        obj->mImplementer = info;
        TRACE_5("Implementer for object '%s' is %s", objectName.c_str(),
                info->mImplementerName.c_str());
      } else {  // not doIt => check not a class implementer & immwritable or
                // idempotent
        if (classInfo->mImplementer) {
          /* User has set class-impl. Now tries to set object impl
             This is not the idempotency case. It is a confused user case.
          */
          err = SA_AIS_ERR_EXIST;
          TRACE_7(
              "ERR_EXIST: Object '%s' already has class implementer %s "
              "conflicts with setting object implementer",
              objectName.c_str(),
              classInfo->mImplementer->mImplementerName.c_str());
        } else if (immNotWritable() || is_sync_aborting()) {
          if (obj->mImplementer != info) {
            /* This was not the idempotency case for object-implementer set
               and sync is on-going => reject this mutation for now.  */
            err = SA_AIS_ERR_TRY_AGAIN;
          } else {
            /* Idempotency. */
            err = SA_AIS_ERR_NO_BINDINGS;
          }
        }
      }  // end of not doit
    }
  }

done:
  /*TRACE_LEAVE();*/
  return err;
}

/**
 * Helper function to release object implementer.
 */
SaAisErrorT ImmModel::releaseImplementer(std::string objectName,
                                         ObjectInfo* obj, ImplementerInfo* info,
                                         bool doIt) {
  SaAisErrorT err = SA_AIS_OK;
  /*TRACE_ENTER();*/
  ClassInfo* classInfo = obj->mClassInfo;
  if (classInfo->mCategory == SA_IMM_CLASS_RUNTIME) {
    osafassert(!doIt);
    LOG_NO(
        "ERR_BAD_OPERATION: Object '%s' is a runtime object, "
        "not allowed to release object implementer.",
        objectName.c_str());
    err = SA_AIS_ERR_BAD_OPERATION;
    goto done;
  }

  if (obj->mCcbId && !doIt) {
    /* Dont allow implementer or applier to detatch to objects with
       an active CCB. The result could be that the OI is left "hanging"
       with a non terminated CCB.
    */
    CcbVector::iterator i1 = std::find_if(sCcbVector.begin(), sCcbVector.end(),
                                          CcbIdIs(obj->mCcbId));

    if (i1 != sCcbVector.end() && (*i1)->isActive()) {
      osafassert(!doIt);
      LOG_NO("ERR_BUSY: ccb %u is active on object %s", obj->mCcbId,
             objectName.c_str());
      err = SA_AIS_ERR_BUSY;
      goto done;
    }
  }

  if (info->mApplier) {
    ImplementerSetMap::iterator ismIter = sObjAppliersMap.find(objectName);
    if (doIt) {
      if (ismIter == sObjAppliersMap.end()) {
        /* Idempotency. */
        TRACE_7("Applier %s was not set for object %s",
                info->mImplementerName.c_str(), objectName.c_str());
      } else {
        ImplementerSet* implSet = ismIter->second;
        implSet->erase(info);
        if (implSet->empty()) {
          delete implSet;
          sObjAppliersMap.erase(ismIter);
        }
      }
    } else {  //! doIt => do checks
      if (classInfo->mAppliers.find(info) != classInfo->mAppliers.end()) {
        /* No mixing of ClassImplementerRelease & ObjectImplementerRelease */
        LOG_IN(
            "ERR_NOT_EXIST: Applier %s is >>class<< applier for the class of "
            "object %s. Will not allow ObjectImplementerRelease",
            info->mImplementerName.c_str(), objectName.c_str());
        err = SA_AIS_ERR_NOT_EXIST;
        goto done;
      }
    }
  } else { /* regular OI */
    if (obj->mImplementer == NULL) {
      /* Idempotency */
      TRACE_5("object '%s' has no implementer", objectName.c_str());
      goto done;
    }

    if (obj->mImplementer != info) {
      osafassert(!doIt);
      TRACE_7("ERR_NOT_EXIST: Object '%s' has different implementer %s != %s",
              objectName.c_str(), obj->mImplementer->mImplementerName.c_str(),
              info->mImplementerName.c_str());
      /* This covers both different class implementer and different object
         implementer.*/
      err = SA_AIS_ERR_NOT_EXIST;
      goto done;

    } else { /* obj->mImplementer == info. But check that it is not class-impl
              */
      if (classInfo->mImplementer) {
        /* User has set class impl but tries to release object impl */
        osafassert(!doIt);

        /* Get class name. */
        ClassMap::iterator ci;
        for (ci = sClassMap.begin(); ci != sClassMap.end(); ++ci) {
          if (ci->second == classInfo) break;
        }
        osafassert(ci != sClassMap.end());

        LOG_NO(
            "ERR_NOT_EXIST: Release of object implementer for "
            "object %s conflicts with current class implementorship "
            "for class %s",
            objectName.c_str(), ci->first.c_str());
        err = SA_AIS_ERR_NOT_EXIST;
        /* I would have preferred ERR_BAD_OPERATION for this case,
           but the spec is so explicit on what that error code means for
           ObjectImplementerRelease, see first check in this function.
        */
        goto done;
      }

      if (doIt) {
        obj->mImplementer = 0;
        TRACE_5("implementer for object '%s' is released", objectName.c_str());
        this->clearImplName(obj);
      }
    }
  }
/*TRACE_LEAVE();*/
done:
  return err;
}

void ImmModel::clearImplName(ObjectInfo* obj) {
  std::string implAttr(SA_IMM_ATTR_IMPLEMENTER_NAME);
  ImmAttrValue* att = obj->mAttrValueMap[implAttr];
  osafassert(att);
  if (!(att->empty())) {
    att->setValueC_str(NULL);
  }
}

void ImmModel::implementerDelete(const char *implementerName) {
  ImplementerInfo *applier = NULL;

  TRACE_ENTER();

  auto ivi = sImplementerVector.begin();
  for(; ivi != sImplementerVector.end(); ++ivi) {
    if((*ivi)->mImplementerName == implementerName) {
      applier = *ivi;
      break;
    }
  }

  if(!applier) {
    TRACE("Cannot find implementer with id: %s", implementerName);
    goto failed;
  }

  if(applier->mImplementerName.at(0) != '@') {
    TRACE("Implementer with id %s is not applier", implementerName);
    goto failed;
  }

  if(!applier->mApplierDisconnected) {
    TRACE("Applier with id %s is used again", implementerName);
    goto failed;
  }

  // Delete implementer from sImplementerVector
  sImplementerVector.erase(ivi);

  // Delete implementer from sClassMap
  for(auto cmi = sClassMap.begin(); cmi != sClassMap.end(); ++cmi) {
    // TODO: It might be more efficient
    // if we call only cmi->second->mAppliers.erase(applier);
    auto it = cmi->second->mAppliers.find(applier);
    if(it != cmi->second->mAppliers.end()) {
      cmi->second->mAppliers.erase(it);
    }
  }

  // Delete implementer from sObjAppliersMap
  for(auto oami = sObjAppliersMap.begin();
      oami != sObjAppliersMap.end(); ++oami) {
    // TODO: Same as for sClassMap
    auto it = oami->second->find(applier);
    if(it != oami->second->end()) {
      oami->second->erase(it);
    }
  }

  // Delete implementer from sImplDetachTime if it exists
  sImplDetachTime.erase(applier);

  delete applier;

failed:
  TRACE_LEAVE();
}

/**
 * Helper function to set admin owner.
 */
SaAisErrorT ImmModel::adminOwnerSet(std::string objectName, ObjectInfo* obj,
                                    AdminOwnerInfo* adm, bool doIt) {
  /*TRACE_ENTER();*/
  SaAisErrorT err = SA_AIS_OK;
  std::string oldOwner;
  osafassert(adm);
  osafassert(obj);
  obj->getAdminOwnerName(&oldOwner);

  // TODO: "IMMLOADER" should be environment variable.
  std::string loader("IMMLOADER");

  if (obj->mObjFlags & IMM_RT_UPDATE_LOCK) {
    LOG_IN(
        "ERR_TRY_AGAIN: Object '%s' already subject of a persistent runtime "
        "attribute update",
        objectName.c_str());
    err = SA_AIS_ERR_TRY_AGAIN;
    goto done;
  }

  // No need to check for interference with Ccb
  // If the object points to a CCb and the state of that ccb is
  // active, then adminowner must be set already for that object.

  if (oldOwner.empty() || oldOwner == loader) {
    if (doIt) {
      obj->mAdminOwnerAttrVal->setValueC_str(adm->mAdminOwnerName.c_str());
      if (adm->mReleaseOnFinalize) {
        adm->mTouchedObjects.insert(obj);
      }
    }
  } else if (oldOwner != adm->mAdminOwnerName) {
    TRACE_7("ERR_EXIST: Object '%s' has different administative owner %s != %s",
            objectName.c_str(), oldOwner.c_str(), adm->mAdminOwnerName.c_str());
    err = SA_AIS_ERR_EXIST;
  }

done:
  /*TRACE_LEAVE();*/
  return err;
}

/**
 * Helper function to release admin owner.
 */
SaAisErrorT ImmModel::adminOwnerRelease(std::string objectName, ObjectInfo* obj,
                                        AdminOwnerInfo* adm, bool doIt) {
  // If adm is NULL then this is assumed to be a clear and not just release.
  /*TRACE_ENTER();*/
  SaAisErrorT err = SA_AIS_OK;
  std::string oldOwner;
  osafassert(obj);
  obj->getAdminOwnerName(&oldOwner);

  std::string loader("IMMLOADER");

  if (!doIt && obj->mObjFlags & IMM_RT_UPDATE_LOCK) {
    LOG_IN(
        "ERR_TRY_AGAIN: Object '%s' already subject of a persistent runtime "
        "attribute update",
        objectName.c_str());
    err = SA_AIS_ERR_TRY_AGAIN;
    goto done;
  }

  // TODO: Need to check for interference with Ccb!!
  // If the object points to a CCb and the state of that ccb is
  // active, then ERROR.

  if (oldOwner.empty()) {
    // Actually empty => ERROR acording to spec
    // But we assume that clear ignores this and that
    // release on finalize ignores this
    //(problem assumed to be caused by a previous clear).
    if (adm) {
      // Not a clear=> error
      if (adm->mReleaseOnFinalize) {
        adm->mTouchedObjects.erase(obj);
      }
      err = SA_AIS_ERR_NOT_EXIST;
    }
  } else {
    if (!adm || oldOwner == adm->mAdminOwnerName || oldOwner == loader) {
      // This is the normal explicit IMM API use case (clear or release)
      // where there is an old-owner that is actually to be removed.
      if (doIt) {
        // We may be pulling the rug out from under another admin owner
        obj->mAdminOwnerAttrVal->setValueC_str(NULL);
        if (adm && adm->mReleaseOnFinalize) {
          adm->mTouchedObjects.erase(obj);
        }
      }
    } else {
      // This is the internal use case (release on finalize)
      // Or the error case when a release conflicts with a
      // different admin owner (probably after clear + set)
      // Empty name => internal-use, ignore
      if (objectName.empty()) {
        // internal use case
        TRACE_7(
            "Release on finalize, an object has changed "
            "administative owner %s != %s",
            oldOwner.c_str(), adm->mAdminOwnerName.c_str());
      } else {
        // Error case
        TRACE_7(
            "ERR_NOT_EXIST: Object '%s' has different administative "
            "owner %s != %s",
            objectName.c_str(), oldOwner.c_str(), adm->mAdminOwnerName.c_str());
        err = SA_AIS_ERR_NOT_EXIST;
      }

      // For both error case and internal use, remove from touchedObjects
      if (adm && adm->mReleaseOnFinalize) {
        adm->mTouchedObjects.erase(obj);
      }
    }
  }
done:
  /*TRACE_LEAVE();*/
  return err;
}

/**
 * Clears the implementer name associated with the handle
 */
SaAisErrorT ImmModel::implementerClear(const struct ImmsvOiImplSetReq* req,
                                       SaUint32T conn, unsigned int nodeId,
                                       IdVector& gv, bool isAtCoord) {
  SaAisErrorT err = SA_AIS_OK;
  TRACE_ENTER();

  ImplementerInfo* info = findImplementer(req->impl_id);
  if (!info) {
    if (sImmNodeState == IMM_NODE_W_AVAILABLE) {
      /* Sync is ongoing and we are a sync client.
         Remember the death of the implementer.
      */
      discardImplementer(req->impl_id, true, gv, isAtCoord);
      goto done;
    }
    LOG_NO("ERR_BAD_HANDLE: Not a correct implementer handle? %llu id:%u",
           req->client_hdl, req->impl_id);
    err = SA_AIS_ERR_BAD_HANDLE;
  } else {
    if ((info->mConn != conn) || (info->mNodeId != nodeId)) {
      LOG_NO(
          "ERR_BAD_HANDLE: Not a correct implementer handle conn:%u nodeId:%x",
          conn, nodeId);
      err = SA_AIS_ERR_BAD_HANDLE;
    } else {
      discardImplementer(req->impl_id, true, gv, isAtCoord);
    }
  }

done:
  TRACE_LEAVE();
  return err;
}

/**
 * Creates a runtime object
 */
SaAisErrorT ImmModel::rtObjectCreate(
    struct ImmsvOmCcbObjectCreate* req, SaUint32T reqConn, unsigned int nodeId,
    SaUint32T* continuationId, SaUint32T* pbeConnPtr,
    unsigned int* pbeNodeIdPtr, SaUint32T* spApplConnPtr,
    SaUint32T* pbe2BConnPtr, bool isObjectDnUsed) {
  TRACE_ENTER2("cont:%p connp:%p nodep:%p", continuationId, pbeConnPtr,
               pbeNodeIdPtr);
  SaAisErrorT err = SA_AIS_OK;
  if (spApplConnPtr) {
    (*spApplConnPtr) = 0;
  }

  if (immNotWritable() || is_sync_aborting()) { /*Check for persistent RTO further down. */
    TRACE_LEAVE();
    return SA_AIS_ERR_TRY_AGAIN;
  }

  size_t sz = strnlen((char*)req->className.buf, (size_t)req->className.size);
  std::string className((const char*)req->className.buf, sz);

  sz = strnlen((char*)req->parentOrObjectDn.buf,
               (size_t)req->parentOrObjectDn.size);
  std::string parentName((const char*)req->parentOrObjectDn.buf, sz);

  TRACE_2("parentName:%s\n", parentName.c_str());
  std::string objectName;

  ClassInfo* classInfo = NULL;
  ObjectInfo* parent = NULL;
  ObjectInfo* object = NULL;

  ClassMap::iterator i3;
  AttrMap::iterator i4;
  ObjectMap::iterator i5;
  ImmAttrValueMap::iterator i6;

  immsv_attr_values_list* attrValues = NULL;
  bool isPersistent = false;
  bool nameCorrected = false;
  bool rdnAttFound = false;
  bool isSpecialApplForClass = false;
  bool longDnsPermitted = getLongDnsAllowed();

  /*Should rename member adminOwnerId. Used to store implid here.*/
  ImplementerInfo* info = findImplementer(req->adminOwnerId);
  if (!info) {
    LOG_IN("ERR_BAD_HANDLE: Not a correct implementer handle %u",
           req->adminOwnerId);
    err = SA_AIS_ERR_BAD_HANDLE;
    goto rtObjectCreateExit;
  }

  if ((info->mConn != reqConn) || (info->mNodeId != nodeId)) {
    LOG_IN("ERR_BAD_HANDLE: Not a correct implementer handle conn:%u nodeId:%x",
           reqConn, nodeId);
    err = SA_AIS_ERR_BAD_HANDLE;
    goto rtObjectCreateExit;
  }
  // conn is NULL on all nodes except primary.
  // At these other nodes the only info on implementer existence is that
  // the nodeId is non-zero. The nodeId is the nodeId of the node where
  // the implementer resides.

  // We rely on FEVS to guarantee that the same handleId is produced at
  // all nodes for the same implementer.

  if (!longDnsPermitted && sz >= SA_MAX_UNEXTENDED_NAME_LENGTH) {
    LOG_NO(
        "ERR_NOT_EXIST: Parent name '%s' has a long DN. "
        "Not allowed by IMM service or extended names are disabled",
        parentName.c_str());
    err = SA_AIS_ERR_NOT_EXIST;
    goto rtObjectCreateExit;
  }

  if (!nameCheck(parentName)) {
    if (nameToInternal(parentName)) {
      nameCorrected = true;
    } else {
      LOG_NO("ERR_INVALID_PARAM: Not a proper parent name:%s size:%u",
             parentName.c_str(), (unsigned int)parentName.size());
      err = SA_AIS_ERR_INVALID_PARAM;
      goto rtObjectCreateExit;
    }
  }

  if (!schemaNameCheck(className)) {
    LOG_NO("ERR_INVALID_PARAM: Not a proper class name:%s", className.c_str());
    err = SA_AIS_ERR_INVALID_PARAM;
    goto rtObjectCreateExit;
  }

  i3 = sClassMap.find(className);
  if (i3 == sClassMap.end()) {
    TRACE_7("ERR_NOT_EXIST: class '%s' does not exist", className.c_str());
    err = SA_AIS_ERR_NOT_EXIST;
    goto rtObjectCreateExit;
  }
  classInfo = i3->second;

  if (classInfo->mCategory != SA_IMM_CLASS_RUNTIME) {
    LOG_NO("ERR_INVALID_PARAM: class '%s' is not a runtime class",
           className.c_str());
    err = SA_AIS_ERR_INVALID_PARAM;
    goto rtObjectCreateExit;
  }

  if (parentName.length() > 0) {
    ObjectMap::iterator i = sObjectMap.find(parentName);
    if (i == sObjectMap.end()) {
      TRACE_7("ERR_NOT_EXIST: parent object '%s' does not exist",
              parentName.c_str());
      err = SA_AIS_ERR_NOT_EXIST;
      goto rtObjectCreateExit;
    } else {
      parent = i->second;
      if (parent->mObjFlags & IMM_CREATE_LOCK) {
        TRACE_7(
            "ERR_NOT_EXIST: parent object '%s' is being created "
            "in a ccb %u or PRTO PBE that has not yet been applied.",
            parentName.c_str(), parent->mCcbId);
        err = SA_AIS_ERR_NOT_EXIST;
        goto rtObjectCreateExit;
      }

      if (parent->mObjFlags & IMM_DELETE_LOCK) {
        TRACE_7(
            "ERR_NOT_EXIST: parent object '%s' is registered for delete in ccb: "
            "%u. Will not allow create of subobject.",
            parentName.c_str(), parent->mCcbId);
        err = SA_AIS_ERR_NOT_EXIST; /* object is not deleted yet */
        goto rtObjectCreateExit;
      }
    }
  }

  i4 = std::find_if(classInfo->mAttrMap.begin(), classInfo->mAttrMap.end(),
                    AttrFlagIncludes(SA_IMM_ATTR_RDN));
  if (i4 == classInfo->mAttrMap.end()) {
    LOG_ER("No RDN attribute found in class!");
    abort();
  }

  attrValues = req->attrValues;

  /* Set attribute name and attribute type for RDN (first element in the list)
   */
  if (isObjectDnUsed) {
    attrValues->n.attrName.buf = strdup(i4->first.c_str());
    attrValues->n.attrName.size = i4->first.size();
    attrValues->n.attrValueType = i4->second->mValueType;
  }

  while (attrValues) {
    sz = strnlen((char*)attrValues->n.attrName.buf,
                 (size_t)attrValues->n.attrName.size);
    std::string attrName((const char*)attrValues->n.attrName.buf, sz);

    if (attrName == i4->first) {  // Match on name for RDN attribute
      if (rdnAttFound) {
        LOG_NO(
            "ERR_INVALID_PARAM: Rdn attribute occurs more than once "
            "in attribute list");
        err = SA_AIS_ERR_INVALID_PARAM;
        goto rtObjectCreateExit;
      }

      rdnAttFound = true;

      /* Sloppy type check, supplied value should EQUAL type in class */
      if ((attrValues->n.attrValueType != SA_IMM_ATTR_SANAMET) &&
          (attrValues->n.attrValueType != SA_IMM_ATTR_SASTRINGT)) {
        LOG_NO(
            "ERR_INVALID_PARAM:  value type for RDN attribute is "
            "neither SaNameT nor SaStringT");
        err = SA_AIS_ERR_INVALID_PARAM;
        goto rtObjectCreateExit;
      }

      /* size includes null termination byte. */
      if (((size_t)attrValues->n.attrValue.val.x.size > 65) &&
          (attrValues->n.attrValueType == SA_IMM_ATTR_SASTRINGT) &&
          !longDnsPermitted) {
        LOG_NO(
            "ERR_INVALID_PARAM: RDN attribute value %s is too large: %u. Max length is 64 "
            "for SaStringT",
            attrValues->n.attrValue.val.x.buf,
            (attrValues->n.attrValue.val.x.size - 1));
        err = SA_AIS_ERR_INVALID_PARAM;
        goto rtObjectCreateExit;
      }

      if (i4->second->mFlags & SA_IMM_ATTR_PERSISTENT) {
        isPersistent = true;

        if (immNotPbeWritable()) {
          err = SA_AIS_ERR_TRY_AGAIN;
          TRACE_7(
              "TRY_AGAIN: Can not create persistent RTO when PBE is unavailable");
          goto rtObjectCreateExit;
        }
      }

      if (parent && (parent->mClassInfo->mCategory == SA_IMM_CLASS_RUNTIME) &&
          (i4->second->mFlags & SA_IMM_ATTR_PERSISTENT)) {
        /*Parent is a runtime obj & new obj is persistent => verify that
          parent is persistent. */
        AttrMap::iterator i44 =
            std::find_if(parent->mClassInfo->mAttrMap.begin(),
                         parent->mClassInfo->mAttrMap.end(),
                         AttrFlagIncludes(SA_IMM_ATTR_RDN));

        osafassert(i44 != parent->mClassInfo->mAttrMap.end());

        if (!(i44->second->mFlags & SA_IMM_ATTR_PERSISTENT)) {
          LOG_NO(
              "ERR_INVALID_PARAM: Parent object '%s' is a "
              "non-persistent runtime object. Will not allow "
              "create of persistent sub-object.",
              parentName.c_str());
          err = SA_AIS_ERR_INVALID_PARAM;
          goto rtObjectCreateExit;
        }
      }

      objectName.append((const char*)attrValues->n.attrValue.val.x.buf,
                        strnlen((const char*)attrValues->n.attrValue.val.x.buf,
                                (size_t)attrValues->n.attrValue.val.x.size));
    } else if ((attrValues->n.attrValueType == SA_IMM_ATTR_SANAMET ||
                attrValues->n.attrValueType == SA_IMM_ATTR_SASTRINGT) &&
               !longDnsPermitted) {
      AttrMap::iterator it = classInfo->mAttrMap.find(attrName);
      if (it == classInfo->mAttrMap.end()) {
        LOG_WA("ERR_INVALID_PARAM: Cannot find attribute '%s'",
               attrName.c_str());
        err = SA_AIS_ERR_INVALID_PARAM;
        goto rtObjectCreateExit;
      }

      if (attrValues->n.attrValueType == SA_IMM_ATTR_SANAMET ||
          (it->second->mFlags & SA_IMM_ATTR_DN)) {
        if (attrValues->n.attrValue.val.x.size >=
            SA_MAX_UNEXTENDED_NAME_LENGTH) {
          LOG_NO(
              "ERR_NAME_TOO_LONG: Attribute '%s' has long DN. "
              "Not allowed by IMM service or extended names are disabled",
              attrName.c_str());
          err = SA_AIS_ERR_NAME_TOO_LONG;
          goto rtObjectCreateExit;
        }

        IMMSV_EDU_ATTR_VAL_LIST* value = attrValues->n.attrMoreValues;
        while (value) {
          if (value->n.val.x.size >= SA_MAX_UNEXTENDED_NAME_LENGTH) {
            LOG_NO(
                "ERR_NAME_TOO_LONG: Attribute '%s' has long DN. "
                "Not allowed by IMM service or extended names are disabled",
                attrName.c_str());
            err = SA_AIS_ERR_NAME_TOO_LONG;
            goto rtObjectCreateExit;
          }
          value = value->next;
        }
      }
    }
    attrValues = attrValues->next;
  }

  if (objectName.empty()) {
    LOG_NO("ERR_INVALID_PARAM: No RDN attribute found or value is empty");
    err = SA_AIS_ERR_INVALID_PARAM;
    goto rtObjectCreateExit;
  }

  if (!nameCheck(objectName)) {
    if (nameToInternal(objectName)) {
      nameCorrected = true;
    } else {
      LOG_NO("ERR_INVALID_PARAM: Not a proper RDN (%s, %u)", objectName.c_str(),
             (unsigned int)objectName.length());
      err = SA_AIS_ERR_INVALID_PARAM;
      goto rtObjectCreateExit;
    }
  }

  if (objectName.find(',') != std::string::npos) {
    LOG_NO("ERR_INVALID_PARAM: Can not tolerate ',' in RDN: '%s'",
           objectName.c_str());
    err = SA_AIS_ERR_INVALID_PARAM;
    goto rtObjectCreateExit;
  }

  if (parent) {
    objectName.append(",");
    objectName.append(parentName);
  }

  if (objectName.size() > ((longDnsPermitted)
                               ? kOsafMaxDnLength
                               : (SA_MAX_UNEXTENDED_NAME_LENGTH - 1))) {
    TRACE_7("ERR_NAME_TOO_LONG: DN is too long, size:%u, max size is:%u",
            (unsigned int)objectName.size(), kOsafMaxDnLength);
    err = SA_AIS_ERR_NAME_TOO_LONG;
    goto rtObjectCreateExit;
  }

  if ((i5 = sObjectMap.find(objectName)) != sObjectMap.end()) {
    if (i5->second->mObjFlags & IMM_CREATE_LOCK) {
      TRACE_7(
          "ERR_EXIST: object '%s' is already registered "
          "for creation in a ccb or PRTO PBE, but not applied yet",
          objectName.c_str());
    } else {
      TRACE_7("ERR_EXIST: Object '%s' already exists", objectName.c_str());
    }
    err = SA_AIS_ERR_EXIST;
    goto rtObjectCreateExit;
  }

  if (err == SA_AIS_OK) {
    void* pbe = NULL;
    void* pbe2B = NULL;
    object = new ObjectInfo();
    object->mClassInfo = classInfo;
    object->mImplementer = info;
    if (parent) {
      object->mParent = parent;

      ObjectInfo* grandParent = parent;
      do {
        grandParent->mChildCount++;
        grandParent = grandParent->mParent;
      } while (grandParent);
    }
    if (nameCorrected) {
      object->mObjFlags = IMM_DN_INTERNAL_REP;
    }

    // Add attributes to object
    for (i4 = classInfo->mAttrMap.begin(); i4 != classInfo->mAttrMap.end();
         ++i4) {
      AttrInfo* attr = i4->second;
      ImmAttrValue* attrValue = NULL;

      if (attr->mFlags & SA_IMM_ATTR_MULTI_VALUE) {
        if (attr->mDefaultValue.empty()) {
          attrValue = new ImmAttrMultiValue();
        } else {
          attrValue = new ImmAttrMultiValue(attr->mDefaultValue);
        }
      } else {
        if (attr->mDefaultValue.empty()) {
          attrValue = new ImmAttrValue();
        } else {
          attrValue = new ImmAttrValue(attr->mDefaultValue);
        }
      }

      // Set admin owner as a regular attribute and then also a pointer
      // to the attrValue for efficient access.
      if (i4->first == std::string(SA_IMM_ATTR_ADMIN_OWNER_NAME)) {
        // attrValue->setValueC_str(adminOwner->mAdminOwnerName.c_str());
        object->mAdminOwnerAttrVal = attrValue;
      }

      object->mAttrValueMap[i4->first] = attrValue;
    }

    // Set attribute values
    immsv_attr_values_list* p = req->attrValues;
    while (p && (err == SA_AIS_OK)) {
      sz = strnlen((char*)p->n.attrName.buf, (size_t)p->n.attrName.size);
      std::string attrName((const char*)p->n.attrName.buf, sz);

      i6 = object->mAttrValueMap.find(attrName);

      if (i6 == object->mAttrValueMap.end()) {
        TRACE_7("ERR_NOT_EXIST: attr '%s' not defined", attrName.c_str());
        err = SA_AIS_ERR_NOT_EXIST;
        break;  // out of for-loop
      }

      i4 = classInfo->mAttrMap.find(attrName);
      osafassert(i4 != classInfo->mAttrMap.end());
      AttrInfo* attr = i4->second;

      if (attr->mValueType != (unsigned int)p->n.attrValueType) {
        LOG_NO("ERR_INVALID_PARAM: attr '%s' type missmatch", attrName.c_str());
        err = SA_AIS_ERR_INVALID_PARAM;
        break;  // out of for-loop
      }

      if (attr->mFlags & SA_IMM_ATTR_CONFIG) {
        LOG_NO(
            "ERR_INVALID_PARAM: attr '%s' is a config attribute => "
            "can not be assigned over OI-API.",
            attrName.c_str());
        err = SA_AIS_ERR_INVALID_PARAM;
        break;  // out of for-loop
      } else if (!(attr->mFlags & SA_IMM_ATTR_CACHED)) {
        LOG_NO(
            "ERR_INVALID_PARAM: attr '%s' is a non-cached runtime attribute => "
            "can not be assigned in rtObjectCreate (see spec).",
            attrName.c_str());
        err = SA_AIS_ERR_INVALID_PARAM;
        break;  // out of for-loop
      }
      if (attr->mValueType == SA_IMM_ATTR_SANAMET) {
        if (p->n.attrValue.val.x.size > kOsafMaxDnLength) {
          LOG_NO("ERR_LIBRARY: attr '%s' of type SaNameT is too long:%u",
                 attrName.c_str(), p->n.attrValue.val.x.size - 1);
          err = SA_AIS_ERR_LIBRARY;
          break;  // out of for-loop
        }

        std::string tmpName(
            p->n.attrValue.val.x.buf,
            p->n.attrValue.val.x.size ? p->n.attrValue.val.x.size - 1 : 0);
        if (!(nameCheck(tmpName) || nameToInternal(tmpName))) {
          LOG_NO(
              "ERR_INVALID_PARAM: attr '%s' of type SaNameT contains non "
              "printable characters",
              attrName.c_str());
          err = SA_AIS_ERR_INVALID_PARAM;
          break;  // out of for-loop
        }
      } else if (attr->mValueType == SA_IMM_ATTR_SASTRINGT) {
        /* Check that the string at least conforms to UTF-8 */
        if (p->n.attrValue.val.x.size &&
            !(osaf_is_valid_utf8(p->n.attrValue.val.x.buf))) {
          LOG_NO(
              "ERR_INVALID_PARAM: attr '%s' of type SaStringT has a value "
              "that is not valid UTF-8",
              attrName.c_str());
          err = SA_AIS_ERR_INVALID_PARAM;
          break;  // out of for-loop
        }
      }

      ImmAttrValue* attrValue = i6->second;
      IMMSV_OCTET_STRING tmpos;  // temporary octet string
      eduAtValToOs(&tmpos, &(p->n.attrValue),
                   (SaImmValueTypeT)p->n.attrValueType);
      attrValue->setValue(tmpos);
      if (p->n.attrValuesNumber > 1) {
        /*
          int extraValues = p->attrValuesNumber - 1;
        */
        if (!(attr->mFlags & SA_IMM_ATTR_MULTI_VALUE)) {
          LOG_NO(
              "ERR_INVALID_PARAM: attr '%s' is not multivalued yet "
              "multiple values provided in create call",
              attrName.c_str());
          err = SA_AIS_ERR_INVALID_PARAM;
          break;  // out of for loop
        }
        /*
          else if(extraValues != p->attrMoreValues.list.count) {
          LOG_ER("Multivalued attr '%s' should have "
          "%u extra values but %u extra values tranmitted",
          attrName.c_str(), extraValues,
          p->attrMoreValues.list.count);
          //Should not happen, error in packing.
          err = SA_AIS_ERR_LIBRARY;
          break; //out of for-loop
          } */
        else {
          osafassert(attrValue->isMultiValued());
          if (className != immClassName) {  // Avoid restoring imm object
            ImmAttrMultiValue* multiattr = (ImmAttrMultiValue*)attrValue;

            IMMSV_EDU_ATTR_VAL_LIST* al = p->n.attrMoreValues;
            while (al) {
              eduAtValToOs(&tmpos, &(al->n),
                           (SaImmValueTypeT)p->n.attrValueType);
              if ((attr->mFlags & SA_IMM_ATTR_NO_DUPLICATES) &&
                  (multiattr->hasMatchingValue(tmpos))) {
                LOG_NO(
                    "ERR_INVALID_PARAM: multivalued attr '%s' with NO_DUPLICATES "
                    "yet duplicate values provided in rto-create call. Class: "
                    "'%s' obj:'%s'.",
                    attrName.c_str(), className.c_str(), objectName.c_str());
                err = SA_AIS_ERR_INVALID_PARAM;
                break;  // out of loop
              }
              multiattr->setExtraValue(tmpos);
              al = al->next;
            }
            // TODO: Verify that attrValuesNumber was correct.
          }
        }
      }  // if(p->n.attrValuesNumber > 1)
      p = p->next;
    }  // while(p...

    // Check that all attributes with CACHED flag have been set.
    //
    // Also: append missing cached RTAs with default values to
    // immsv_attr_values_list  if there is a special-applier locally attached.
    // This so the special applier will  get informed of the initial value of all
    // cached RTAs and system attributes. See #2873.
    //
    // Also: Write notice message for default removed attributes which are empty

    isSpecialApplForClass = specialApplyForClass(classInfo);

    for (i6 = object->mAttrValueMap.begin();
         i6 != object->mAttrValueMap.end() && err == SA_AIS_OK; ++i6) {
      ImmAttrValue* attrValue = i6->second;
      std::string attrName(i6->first);

      if (attrName == std::string(SA_IMM_ATTR_IMPLEMENTER_NAME)) {
        if ((pbeNodeIdPtr && isPersistent) || isSpecialApplForClass) {
          /* #1872 Implementer-name attribute must be propagated to PBE
             in create of persistent RTO.
             First set the implementer-name attribute value in the imm object.
             Then add the value to the attribute list for the upcall to PBE.
             Also needed for special applier.
          */
          attrValue->setValueC_str(
              object->mImplementer->mImplementerName.c_str());
          p = new immsv_attr_values_list;
          p->n.attrName.size = (SaUint32T)attrName.size() + 1;
          p->n.attrName.buf = strdup(attrName.c_str());
          p->n.attrValueType = SA_IMM_ATTR_SASTRINGT;
          p->n.attrMoreValues = NULL;
          p->n.attrValuesNumber = 1;
          attrValue->copyValueToEdu(&(p->n.attrValue), SA_IMM_ATTR_SASTRINGT);
          p->next = req->attrValues;
          req->attrValues = p;
        }

        continue;
      }

      if (isSpecialApplForClass &&
          attrName == std::string(SA_IMM_ATTR_CLASS_NAME)) {
        /*
          Class-name is needed by special aplier, will be ignored by
          PBE if not persistent.
        */
        p = new immsv_attr_values_list;
        p->n.attrName.size = (SaUint32T)attrName.size() + 1;
        p->n.attrName.buf = strdup(attrName.c_str());
        p->n.attrValueType = SA_IMM_ATTR_SASTRINGT;
        p->n.attrMoreValues = NULL;
        p->n.attrValuesNumber = 1;
        attrValue->copyValueToEdu(&(p->n.attrValue), SA_IMM_ATTR_SASTRINGT);
        p->next = req->attrValues;
        req->attrValues = p;

        continue;
      }

      if (attrName == std::string(SA_IMM_ATTR_ADMIN_OWNER_NAME)) {
        continue;
      }

      i4 = classInfo->mAttrMap.find(attrName);
      osafassert(i4 != classInfo->mAttrMap.end());
      AttrInfo* attr = i4->second;

      if ((attr->mFlags & SA_IMM_ATTR_DEFAULT_REMOVED) && attrValue->empty()) {
        LOG_NO("Attribute %s has a removed default, the value will be empty",
               attrName.c_str());
      }

      if ((attr->mFlags & SA_IMM_ATTR_CACHED) && attrValue->empty()) {
        /* #1531 Check that the attribute was at least in the input list.
           This is a questionable rule from the standard (still in A.03.01),
           which says that INVALID_PARAM should be returned for this case.
           But this restriction does not apply to RTAs defined in config
           classes!
        */
        attrValues = req->attrValues;
        while (attrValues) {
          sz = strnlen((char*)attrValues->n.attrName.buf,
                       (size_t)attrValues->n.attrName.size);
          std::string inpAttrName((const char*)attrValues->n.attrName.buf, sz);
          if (inpAttrName == attrName) {
            break;
          }
          attrValues = attrValues->next;
        }
        if (!attrValues) {
          LOG_NO(
              "ERR_INVALID_PARAM: attr '%s' is cached "
              "but not included in the rt object create call",
              attrName.c_str());
          err = SA_AIS_ERR_INVALID_PARAM;
        }
      }

      if ((attr->mFlags & SA_IMM_ATTR_NOTIFY) && isSpecialApplForClass &&
          !(attrValue->empty())) {
        osafassert(attr->mFlags & SA_IMM_ATTR_CACHED);
        /* Notify marked attribute has value & special applier present.
           Check that the attribute is in the input/callback list.
           Value could have been set by default value => add it to callback
           list.
        */
        attrValues = req->attrValues;
        while (attrValues) {
          sz = strnlen((char*)attrValues->n.attrName.buf,
                       (size_t)attrValues->n.attrName.size);
          std::string inpAttrName((const char*)attrValues->n.attrName.buf, sz);
          if (inpAttrName == attrName) {
            break;
          }
          attrValues = attrValues->next;
        }
        if (!attrValues) {
          p = new immsv_attr_values_list;
          p->n.attrName.size = (SaUint32T)attrName.size() + 1;
          p->n.attrName.buf = strdup(attrName.c_str());
          p->n.attrValueType = (SaImmValueTypeT)attr->mValueType;
          p->n.attrMoreValues = NULL;
          p->n.attrValuesNumber = 1; /* Defaults can not be multivalued. */
          attrValue->copyValueToEdu(&(p->n.attrValue),
                                    (SaImmValueTypeT)attr->mValueType);
          p->next = req->attrValues;
          req->attrValues = p;
        }
      }
    }

    if (isPersistent && (err == SA_AIS_OK) && pbeNodeIdPtr) {
      unsigned int slaveNodeId = 0;
      /* PBE expected. */
      pbe = getPbeOi(pbeConnPtr, pbeNodeIdPtr);
      if (!pbe) {
        LOG_ER("Pbe is not available, can not happen here");
        abort();
      }

      pbe2B = getPbeBSlave(pbe2BConnPtr, &slaveNodeId);

      /* If 2PBE then not both PBEs on same processor. */
      osafassert(!((*pbeConnPtr) && (*pbe2BConnPtr)));
    }

    if (err != SA_AIS_OK) {
      // Delete object and its attributes
      ImmAttrValueMap::iterator oavi;
      for (oavi = object->mAttrValueMap.begin();
           oavi != object->mAttrValueMap.end(); ++oavi) {
        delete oavi->second;
      }

      // Empty the collection, probably not necessary (as the ObjectInfo
      // record is deleted below), but does not hurt.
      object->mAttrValueMap.clear();
      delete object;
      goto rtObjectCreateExit;
    }

    if (isPersistent) {
      if (pbe) { /* Persistent back end is up (somewhere) */

        object->mObjFlags |= IMM_CREATE_LOCK;
        /* Dont overwrite IMM_DN_INTERNAL_REP*/

        if (++sLastContinuationId >= 0xfffffffe) {
          sLastContinuationId = 1;
        }
        *continuationId = sLastContinuationId;

        ObjectMutation* oMut = new ObjectMutation(IMM_CREATE);
        oMut->mContinuationId = (*continuationId);
        oMut->mAfterImage = object;
        sPbeRtMutations[objectName] = oMut;
        if (reqConn) {
          SaInvocationT tmp_hdl =
              m_IMMSV_PACK_HANDLE((*continuationId), nodeId);
          sPbeRtReqContinuationMap[tmp_hdl] =
              ContinuationInfo2(reqConn, DEFAULT_TIMEOUT_SEC);
        }
        TRACE_5(
            "Tentative create of PERSISTENT runtime object '%s' "
            "by Impl %s pending PBE ack",
            objectName.c_str(), info->mImplementerName.c_str());

        oMut->m2PbeCount = pbe2B ? 2 : 1;

      } else { /* No pbe and no pbe expected. */
        TRACE("Create of PERSISTENT runtime object '%s' by Impl %s",
              objectName.c_str(), info->mImplementerName.c_str());
      }
    } else { /* !isPersistent i.e. normal RTO */
      LOG_IN("Create runtime object '%s' by Impl id: %u", objectName.c_str(),
             info->mId);
    }

    if (isSpecialApplForClass) {
      ImplementerInfo* spAppl = getSpecialApplier();
      if (spAppl && spApplConnPtr) {
        /* Special applier exists locally attached */
        (*spApplConnPtr) = spAppl->mConn;
      }
    }

    /* Reorder attributes for upcall(s), and set RDN as the first attribute */
    if ((isPersistent && pbe) || (*spApplConnPtr)) {
      IMMSV_ATTR_VALUES_LIST* prev = req->attrValues;
      IMMSV_ATTR_VALUES_LIST* t = req->attrValues;
      size_t rdnAttrLen;

      i4 = std::find_if(classInfo->mAttrMap.begin(), classInfo->mAttrMap.end(),
                        AttrFlagIncludes(SA_IMM_ATTR_RDN));
      osafassert(
          i4 !=
          classInfo->mAttrMap
              .end()); /* This should never happen. It's already checked */
      rdnAttrLen = strnlen(i4->first.c_str(), i4->first.size());

      if (!(strnlen(t->n.attrName.buf, t->n.attrName.size) == rdnAttrLen) ||
          strncmp(t->n.attrName.buf, i4->first.c_str(), rdnAttrLen)) {
        // RDN is not the first element in the list
        t = t->next;
        while (t) {
          if ((strnlen(t->n.attrName.buf, t->n.attrName.size) == rdnAttrLen) &&
              !strncmp(t->n.attrName.buf, i4->first.c_str(), rdnAttrLen)) {
            prev->next = t->next;
            t->next = req->attrValues;
            req->attrValues = t;
            break;
          }
          prev = t;
          t = t->next;
        }
      }
      osafassert(
          t);  // This should never happen. RDN must exist in attribute list
    }

    sObjectMap[objectName] = object;
    classInfo->mExtent.insert(object);

    if (className == immClassName) {
      updateImmObject(immClassName);
    }
  }
rtObjectCreateExit:
  TRACE_LEAVE();
  return err;
}

void ImmModel::pbePrtObjCreateContinuation(SaUint32T invocation,
                                           SaAisErrorT error,
                                           unsigned int nodeId,
                                           SaUint32T* reqConn,
                                           SaUint32T* spApplConnPtr,
                                           struct ImmsvOmCcbObjectCreate* req) {
  TRACE_ENTER();
  if (spApplConnPtr) {
    (*spApplConnPtr) = 0;
  }

  SaInvocationT inv = m_IMMSV_PACK_HANDLE(invocation, nodeId);
  ObjectMutationMap::iterator i2;
  for (i2 = sPbeRtMutations.begin(); i2 != sPbeRtMutations.end(); ++i2) {
    if (i2->second->mContinuationId == invocation) {
      break;
    }
  }

  if (i2 == sPbeRtMutations.end()) {
    if (error == SA_AIS_OK) {
      LOG_WA("PBE PRTO Create continuation missing! invoc:%u", invocation);
    }
    return;
  }

  ObjectMutation* oMut = i2->second;

  osafassert(oMut->mOpType == IMM_CREATE);

  if (oMut->m2PbeCount) {
    oMut->m2PbeCount--;
    if (oMut->m2PbeCount && error == SA_AIS_OK) {
      TRACE("pbePrtObjCreateContinuation Wait for reply from other PBE");
      return;
    }
    if (error == SA_AIS_OK) {
      TRACE("pbePrtObjCreateContinuation: All PBEs have responded with OK");
    }
    /* If any of the PBEs reply with error, then the PRTO create is aborted.
       and this contiuation removed.
    */
  }

  ContinuationMap2::iterator ci = sPbeRtReqContinuationMap.find(inv);
  if (ci != sPbeRtReqContinuationMap.end()) {
    /* The client was local. */
    TRACE("CLIENT WAS LOCAL continuation found");
    *reqConn = ci->second.mConn;
    sPbeRtReqContinuationMap.erase(ci);
  }

  oMut->mAfterImage->mObjFlags &= ~IMM_CREATE_LOCK;

  if (error == SA_AIS_OK) {
    if (oMut->mAfterImage->mImplementer) {
      LOG_NO("Create of PERSISTENT runtime object '%s' (%s).",
             i2->first.c_str(),
             oMut->mAfterImage->mImplementer->mImplementerName.c_str());
    } else {
      LOG_NO("Create of PERSISTENT runtime object '%s' (%s).",
             i2->first.c_str(), "<detached>");
    }
    /* Check if there was/is a special applier locally attached.
       Note that we dont care if the special applier found here actually
       attached *after* the PRTO operation was sent to the PBE. We know
       there *was* a special applier attached if oMut->mSavedData is set.
       Even if this is a new special applier, each RTO create is a
       transaction unto itself. So there is no risk that a very recently
       attached special applier gets an 'incomplete' picture when receiving
       the RTO create callback.
    */
    ImplementerInfo* spAppl = getSpecialApplier();
    if (spAppl && spApplConnPtr && oMut->mSavedData) {
      osafassert(req);
      (*spApplConnPtr) = spAppl->mConn;
      if (oMut->mAfterImage->mParent) {
        std::string parentName;
        getParentDn(/*out*/ parentName, /* in */ i2->first.c_str());
        req->parentOrObjectDn.size = parentName.size() + 1;
        req->parentOrObjectDn.buf = strdup(parentName.c_str());
      } else {
        req->parentOrObjectDn.size = 0;
        req->parentOrObjectDn.buf = NULL;
      }

      req->attrValues = (immsv_attr_values_list*)oMut->mSavedData;
      oMut->mSavedData = NULL;
      immsv_attr_values_list* current = req->attrValues;
      do {
        if (strncmp((char*)current->n.attrName.buf, SA_IMM_ATTR_CLASS_NAME,
                    current->n.attrName.size) == 0) {
          osafassert(current->n.attrValueType == SA_IMM_ATTR_SASTRINGT);
          req->className.size = current->n.attrValue.val.x.size;
          req->className.buf = strdup(current->n.attrValue.val.x.buf);
          current = NULL;
        } else {
          current = current->next;
        }
      } while (current);
    }
  } else {
    bool dummy = false;
    bool dummy2 = false;
    ObjectMap::iterator oi = sObjectMap.find(i2->first);
    osafassert(oi != sObjectMap.end());
    /* Need to decrement mChildCount in parents. */
    ObjectInfo* grandParent = oi->second->mParent;
    while (grandParent) {
      std::string gpDn;
      getObjectName(grandParent, gpDn);
      grandParent->mChildCount--;
      LOG_IN(
          "Childcount for (grand)parent %s of reverted create prto %s adjusted to %u",
          gpDn.c_str(), i2->first.c_str(), grandParent->mChildCount);
      grandParent = grandParent->mParent;
    }

    osafassert(deleteRtObject(oi, true, NULL, dummy, dummy2) == SA_AIS_OK);
    LOG_WA("Create of PERSISTENT runtime object '%s' REVERTED. PBE rc:%u",
           i2->first.c_str(), error);
  }

  oMut->mAfterImage = NULL;
  sPbeRtMutations.erase(i2);
  delete oMut;
  TRACE_LEAVE();
}

SaInt32T ImmModel::pbePrtObjDeletesContinuation(
    SaUint32T invocation, SaAisErrorT error, unsigned int nodeId,
    SaUint32T* reqConn, ObjectNameVector& objNameVector,
    SaUint32T* spApplConnPtr, SaUint32T* pbe2BConnPtr) {
  TRACE_ENTER();
  /* 2PBE => two invocations return bool arg to discriminate.
     First invoc is the regular OK/NOK on PRTO delete.
     Second invocation is just the closure of the slave PBE continuation.
     We need to keep track of that *only* for pbe-stuck monitoring.
   */
  osafassert(spApplConnPtr);
  (*spApplConnPtr) = 0;
  osafassert(pbe2BConnPtr);
  (*pbe2BConnPtr) = 0;
  osafassert(reqConn);
  (*reqConn) = 0;
  unsigned int slaveNodeId = 0;
  unsigned int nrofDeletes = 0;
  bool deleteRootFound = false;
  SaInvocationT inv = m_IMMSV_PACK_HANDLE(invocation, nodeId);

  ObjectMutationMap::iterator i2 = sPbeRtMutations.begin();
  if (getPbeBSlave(pbe2BConnPtr, &slaveNodeId)) {
    bool sendCompletedToSlave = false;
    /* slave exists check if reply was from primary or slave.*/
    for (; i2 != sPbeRtMutations.end(); ++i2) {
      if (i2->second->mContinuationId != invocation) {
        continue;
      }

      ObjectMutation* oMut = i2->second;
      osafassert(oMut->mOpType == IMM_DELETE);
      if (oMut->m2PbeCount) {
        osafassert(oMut->mAfterImage->mObjFlags & IMM_DELETE_ROOT);
        oMut->m2PbeCount--;
        if (oMut->m2PbeCount) {
          osafassert(!sendCompletedToSlave); /* Can only have one deleteroot!
                                                and only 2 PBEs */
          if (error == SA_AIS_OK) {
            TRACE(
                "pbePrtObjDeletesContinuation OK from primary PBE Wait for reply from slave PBE");
            sendCompletedToSlave = true;
          } else {
            LOG_WA("Error %u returned from primary PBE on PrtoDelete", error);
            break; /* out of for() */
          }
        } else if (error != SA_AIS_OK) {
          LOG_WA(
              "Error %u returned from slave PBE on PrtoDelete! should never happen",
              error);
          /* Possibly return -1 indicating slave PBE has to be shot down.
             Or make this case *impossible* in the slave logic, i.e. catch
             this case in the slave.!! Possibly assert the immnd colocated with
             the slave (no).
          */
        } else {
          TRACE(
              "pbePrtObjDeletesContinuation: All PBEs have responded with OK");
          (*pbe2BConnPtr) = 0; /* Dont send duplicate completed to slave. */
        }
      }
      ++nrofDeletes;
      /* If primary PBE replies with erro then the PRTO delete is aborted
         and this contiuation removed. */
    }
    if (sendCompletedToSlave && error == SA_AIS_OK) {
      TRACE("Send completed to slave for %u PRTO deletes", nrofDeletes);
      return nrofDeletes;
    }
  }

  /* Regular processing merges with reply from slave processing. */

  TRACE("1PBE or 2PBE with error or 2PBE where both PBEs have replied.");
  nrofDeletes = 0;

  /* Lookup special applier */
  ImplementerInfo* spImpl = getSpecialApplier();
  if (spImpl && spImpl->mConn) {
    (*spApplConnPtr) = spImpl->mConn;
  }

  for (i2 = sPbeRtMutations.begin(); i2 != sPbeRtMutations.end();) {
    if (i2->second->mContinuationId != invocation) {
      ++i2;
      continue;
    }

    ++nrofDeletes;
    ObjectMutation* oMut = i2->second;
    osafassert(oMut->mOpType == IMM_DELETE);

    oMut->mAfterImage->mObjFlags &= ~IMM_DELETE_LOCK;

    if (error == SA_AIS_OK) {
      if (oMut->mAfterImage->mObjFlags & IMM_PRTO_FLAG) {
        if (oMut->mAfterImage->mImplementer) {
          TRACE("Delete of PERSISTENT runtime object '%s' (%s).",
                i2->first.c_str(),
                oMut->mAfterImage->mImplementer->mImplementerName.c_str());
        } else {
          TRACE("Delete of PERSISTENT runtime object '%s' (%s).",
                i2->first.c_str(), "<detached>");
        }
      } else {
        TRACE("Delete of runtime object '%s'.", i2->first.c_str());
      }
      bool dummy = false;
      bool dummy2 = false;
      ObjectMap::iterator oi = sObjectMap.find(i2->first);
      osafassert(oi != sObjectMap.end());

      if (oMut->mAfterImage->mObjFlags & IMM_RTNFY_FLAG) {
        if (*spApplConnPtr) {
          if (oMut->mAfterImage->mObjFlags & IMM_DN_INTERNAL_REP) {
            std::string tmpName(i2->first);
            nameToExternal(tmpName);
            objNameVector.push_back(tmpName);
          } else {
            objNameVector.push_back(i2->first);
          }
        }
        oMut->mAfterImage->mObjFlags &=
            ~IMM_RTNFY_FLAG; /* Reset the notify flag. */
      }

      if (oMut->mAfterImage->mObjFlags & IMM_DELETE_ROOT) {
        /* Adjust ChildCount for parent(s) of the root for the delete-op */
        osafassert(
            !deleteRootFound); /* At most one delete-root per delete-op */
        deleteRootFound = true;
        TRACE("Delete root found '%s'.", i2->first.c_str());
        /* Remove any DELETE_ROOT flag. */
        oMut->mAfterImage->mObjFlags &= ~IMM_DELETE_ROOT;

        ObjectInfo* grandParent = oi->second->mParent;
        while (grandParent) {
          osafassert(grandParent->mChildCount >= (oi->second->mChildCount + 1));
          grandParent->mChildCount -= (oi->second->mChildCount + 1);
          TRACE_5("Childcount for (grand)parent of deleted %s adjusted to %u",
                  i2->first.c_str(), grandParent->mChildCount);
          grandParent = grandParent->mParent;
        }
      }

      osafassert(deleteRtObject(oi, true, NULL, dummy, dummy2) == SA_AIS_OK);
    } else {
      if (oMut->mAfterImage->mObjFlags & IMM_PRTO_FLAG) {
        LOG_WA("Delete of PERSISTENT runtime object '%s' REVERTED. PBE rc:%u",
               i2->first.c_str(), error);
      } else {
        LOG_WA("Delete of cached runtime object '%s' REVERTED. PBE rc:%u",
               i2->first.c_str(), error);
      }
      /* Remove any DELETE_ROOT flag. */
      oMut->mAfterImage->mObjFlags &= ~IMM_DELETE_ROOT;
    }

    oMut->mAfterImage = NULL;
    delete oMut;
    i2 = sPbeRtMutations.erase(i2);
  }  // for

  if (nrofDeletes == 0) {
    LOG_WA("PBE PRTO Deletes continuation missing! invoc:%u", invocation);
  } else if (error == SA_AIS_OK) {
    TRACE("PBE PRTO Deleted %u RT Objects", nrofDeletes);
  }

  ContinuationMap2::iterator ci = sPbeRtReqContinuationMap.find(inv);
  if (ci != sPbeRtReqContinuationMap.end()) {
    /* The client was local. */
    TRACE("CLIENT WAS LOCAL continuation found");
    *reqConn = ci->second.mConn;
    sPbeRtReqContinuationMap.erase(ci);
  }

  /* If no notifications then reset spApplConnPtr to zero */
  if ((*spApplConnPtr) && objNameVector.empty()) {
    (*spApplConnPtr) = 0;
  }

  TRACE_LEAVE();
  return 0;
}

void ImmModel::pbePrtAttrUpdateContinuation(
    SaUint32T invocation, SaAisErrorT error, unsigned int nodeId,
    SaUint32T* reqConn, SaUint32T* spApplConnPtr,
    struct ImmsvOmCcbObjectModify* req) {
  TRACE_ENTER();
  if (spApplConnPtr) {
    (*spApplConnPtr) = 0;
  }

  SaInvocationT inv = m_IMMSV_PACK_HANDLE(invocation, nodeId);
  ObjectMutationMap::iterator i2;
  for (i2 = sPbeRtMutations.begin(); i2 != sPbeRtMutations.end(); ++i2) {
    if (i2->second->mContinuationId == invocation) {
      break;
    }
  }

  if (i2 == sPbeRtMutations.end()) {
    if (error == SA_AIS_OK) {
      LOG_WA("PBE PRTAttrs Update continuation missing! invoc:%u", invocation);
    }
    return;
  }

  ObjectMutation* oMut = i2->second;

  std::string objName(i2->first);
  osafassert(oMut->mOpType == IMM_MODIFY);

  if (oMut->m2PbeCount) {
    oMut->m2PbeCount--;
    if (oMut->m2PbeCount && error == SA_AIS_OK) {
      TRACE("pbePrtObjUpdateContinuation Wait for reply from other PBE");
      return;
    }
    if (error == SA_AIS_OK) {
      TRACE("pbePrtObjUpdateContinuation: All PBEs have responded with OK");
    }
    /* If any of the PBEs reply with error, then the PRTO create is aborted.
       and this contiuation removed.
    */
  }

  ContinuationMap2::iterator ci = sPbeRtReqContinuationMap.find(inv);
  if (ci != sPbeRtReqContinuationMap.end()) {
    /* The client was local. */
    TRACE("CLIENT WAS LOCAL continuation found");
    *reqConn = ci->second.mConn;
    sPbeRtReqContinuationMap.erase(ci);
  }

  ObjectInfo* afim = oMut->mAfterImage;
  osafassert(afim);

  ObjectMap::iterator oi = sObjectMap.find(objName);
  osafassert(oi != sObjectMap.end());
  ObjectInfo* beforeImage = oi->second;
  beforeImage->mObjFlags &= ~IMM_RT_UPDATE_LOCK;

  ClassInfo* classInfo = beforeImage->mClassInfo;

  ImmAttrValueMap::iterator oavi;

  if (error == SA_AIS_OK) {
    if (beforeImage->mImplementer) {
      LOG_IN("Update of PERSISTENT runtime attributes in object '%s' (%s).",
             objName.c_str(),
             beforeImage->mImplementer->mImplementerName.c_str());
    } else {
      LOG_IN("Update of PERSISTENT runtime attributes in object '%s' (%s).",
             objName.c_str(), "<detached>");
    }

    /* Check if there was/is a special applier locally attached.
       Note that we dont care if the special applier found here actually
       attached *after* the PRT operation was sent to the PBE. We know
       there *was* a special applier attached if oMut->mSavedData is set.
       Even if this is a new special applier, each RTA update is a
       transaction unto itself. So there is no risk that a very recently
       attached special applier gets an 'incomplete' picture when receiving
       the RTA update callback.
    */

    if (oMut->mSavedData && spApplConnPtr) {
      ImplementerInfo* spAppl = getSpecialApplier();
      if (spAppl) {
        osafassert(req);
        (*spApplConnPtr) = spAppl->mConn;
        req->objectName.size = objName.size() + 1;
        req->objectName.buf = strdup(objName.c_str());
        req->attrMods = (immsv_attr_mods_list*)oMut->mSavedData;
        oMut->mSavedData = NULL;
      }
    }

    if (afim->mAdminOwnerAttrVal && beforeImage->mAdminOwnerAttrVal->empty()) {
      /*Empty admin Owner can imply (hard) release during PRTA update.
        The releaseOn finalize will have auto-released the adminOwner
        on the before-image but not on the after image of modify.
        Corrected here.
      */
      afim->mAdminOwnerAttrVal->setValueC_str(NULL);
    }

    /* Discard beforeimage RTA values. */
    for (oavi = beforeImage->mAttrValueMap.begin();
         oavi != beforeImage->mAttrValueMap.end(); ++oavi) {
      AttrMap::iterator i4 = classInfo->mAttrMap.find(oavi->first);
      osafassert(i4 != classInfo->mAttrMap.end());
      if (i4->second->mFlags & SA_IMM_ATTR_RUNTIME) {
        delete oavi->second;
      }
    }

    for (oavi = afim->mAttrValueMap.begin(); oavi != afim->mAttrValueMap.end();
         ++oavi) {
      AttrMap::iterator i4 = classInfo->mAttrMap.find(oavi->first);
      osafassert(i4 != classInfo->mAttrMap.end());
      osafassert(i4->second->mFlags & SA_IMM_ATTR_RUNTIME);
      beforeImage->mAttrValueMap[oavi->first] = oavi->second;
      if (oavi->first == std::string(SA_IMM_ATTR_ADMIN_OWNER_NAME)) {
        beforeImage->mAdminOwnerAttrVal = oavi->second;
      }
    }
    afim->mAttrValueMap.clear();
    delete afim;
  } else {
    LOG_WA(
        "update of PERSISTENT runtime attributes in object '%s' REVERTED. "
        "PBE rc:%u",
        objName.c_str(), error);

    /*Discard afterimage */
    for (oavi = afim->mAttrValueMap.begin(); oavi != afim->mAttrValueMap.end();
         ++oavi) {
      delete oavi->second;
    }
    // Empty the collection, probably not necessary (as the
    // ObjectInfo record is deleted below), but does not hurt.
    afim->mAttrValueMap.clear();
    delete afim;
  }

  oMut->mAfterImage = NULL;
  sPbeRtMutations.erase(i2);
  delete oMut;
  oMut = NULL;

  TRACE_LEAVE();
}

void ImmModel::pbeClassCreateContinuation(SaUint32T invocation,
                                          unsigned int nodeId,
                                          SaUint32T* reqConn) {
  TRACE_ENTER();

  SaInvocationT inv = m_IMMSV_PACK_HANDLE(invocation, nodeId);
  ContinuationMap2::iterator ci = sPbeRtReqContinuationMap.find(inv);
  if (ci != sPbeRtReqContinuationMap.end()) {
    /* The client was local. */
    TRACE("CLIENT WAS LOCAL continuation found");
    *reqConn = ci->second.mConn;
    sPbeRtReqContinuationMap.erase(ci);
  }

  ObjectMutationMap::iterator i2;
  for (i2 = sPbeRtMutations.begin(); i2 != sPbeRtMutations.end(); ++i2) {
    if (i2->second->mContinuationId == invocation) {
      break;
    }
  }

  if (i2 == sPbeRtMutations.end()) {
    LOG_WA("PBE Class Create continuation missing! invoc:%u", invocation);
    return;
  }

  ObjectMutation* oMut = i2->second;

  osafassert(oMut->mOpType == IMM_CREATE_CLASS);

  LOG_NO("Create of class %s is PERSISTENT.", i2->first.c_str());

  sPbeRtMutations.erase(i2);
  delete oMut;
  TRACE_LEAVE();
}

void ImmModel::pbeClassDeleteContinuation(SaUint32T invocation,
                                          unsigned int nodeId,
                                          SaUint32T* reqConn) {
  TRACE_ENTER();

  SaInvocationT inv = m_IMMSV_PACK_HANDLE(invocation, nodeId);
  ContinuationMap2::iterator ci = sPbeRtReqContinuationMap.find(inv);
  if (ci != sPbeRtReqContinuationMap.end()) {
    /* The client was local. */
    TRACE("CLIENT WAS LOCAL continuation found");
    *reqConn = ci->second.mConn;
    sPbeRtReqContinuationMap.erase(ci);
  }

  ObjectMutationMap::iterator i2;
  for (i2 = sPbeRtMutations.begin(); i2 != sPbeRtMutations.end(); ++i2) {
    if (i2->second->mContinuationId == invocation) {
      break;
    }
  }

  if (i2 == sPbeRtMutations.end()) {
    LOG_WA("PBE Class Delete continuation missing! invoc:%u", invocation);
    return;
  }

  ObjectMutation* oMut = i2->second;

  osafassert(oMut->mOpType == IMM_DELETE_CLASS);

  LOG_NO("Delete of class %s is PERSISTENT.", i2->first.c_str());

  sPbeRtMutations.erase(i2);
  delete oMut;
  TRACE_LEAVE();
}

void ImmModel::pbeUpdateEpochContinuation(SaUint32T invocation,
                                          unsigned int nodeId) {
  TRACE_ENTER();

  ObjectMutationMap::iterator i2;
  for (i2 = sPbeRtMutations.begin(); i2 != sPbeRtMutations.end(); ++i2) {
    if (i2->second->mContinuationId == invocation) {
      break;
    }
  }

  if (i2 == sPbeRtMutations.end()) {
    LOG_WA("PBE update epoch continuation missing! invoc:%u", invocation);
    return;
  }

  ObjectMutation* oMut = i2->second;

  osafassert(oMut->mOpType == IMM_UPDATE_EPOCH);

  LOG_IN("Update of epoch is PERSISTENT.");

  sPbeRtMutations.erase(i2);
  delete oMut;
  TRACE_LEAVE();
}

bool ImmModel::pbeIsInSync(bool checkCriticalCcbs) {
  if (checkCriticalCcbs) {
    /* Implies that coord has just been elected, that is switched from one SC to
       the other, unless this was a cluster start. Check for ccbs in critical.
       Can not rely on sqlite recovery to resolve it when coord moves to new SC.

       This is because the old PBE on other SC may still be busy with commit of
       such a ccb, at the same time as the new PBE is started on this SC.

       If the new PBE on this side opens the db file, it could trigger a
       "recovery" towards that file, concurrently with the transaction still
       being processed by the old lingering PBE, resulting in file corruption.
       Sqlite uses posix file locking to guard against this, but such file
       locking can not be trusted when used by multiple clients over NFS.

       The db file must then be regenerated by the new PBE as new file (new
       inode).
    */
    CcbVector::iterator i;
    for (i = sCcbVector.begin(); i != sCcbVector.end(); ++i) {
      if ((*i)->mState == IMM_CCB_CRITICAL) {
        LOG_IN("Ccb %u is in critical state", (*i)->mId);
        return false;
      }
    }
  }
  return sPbeRtMutations.empty();
}

void ImmModel::deferRtUpdate(ImmsvOmCcbObjectModify* req, SaUint64T msgNo) {
  DeferredRtAUpdateList* attrUpdList = NULL;
  DeferredRtAUpdate dRtAU;
  TRACE_ENTER2("Defer RtUpdate for object:%s msgNo:%llu", req->objectName.buf,
               msgNo);
  size_t sz = strnlen((char*)req->objectName.buf, (size_t)req->objectName.size);
  std::string objectName((const char*)req->objectName.buf, sz);

  dRtAU.fevsMsgNo = msgNo;
  dRtAU.attrModsList = req->attrMods;
  req->attrMods = NULL; /* Steal the attrModsList */

  DeferredObjUpdatesMap::iterator doumIter =
      sDeferredObjUpdatesMap.find(objectName);
  if (doumIter != sDeferredObjUpdatesMap.end()) {
    attrUpdList = doumIter->second;
  }
  if (!attrUpdList) {
    attrUpdList = new DeferredRtAUpdateList;
    sDeferredObjUpdatesMap[objectName] = attrUpdList;
  }
  attrUpdList->push_back(dRtAU);
}

/**
 * Update a runtime attributes in an object (runtime or config)
 */
SaAisErrorT ImmModel::rtObjectUpdate(
    const ImmsvOmCcbObjectModify* req, SaUint32T conn, unsigned int nodeId,
    bool* isPureLocal, SaUint32T* continuationIdPtr, SaUint32T* pbeConnPtr,
    unsigned int* pbeNodeIdPtr, SaUint32T* spApplConnPtr,
    SaUint32T* pbe2BConnPtr)

{
  TRACE_ENTER2("cont:%p connp:%p nodep:%p", continuationIdPtr, pbeConnPtr,
               pbeNodeIdPtr);
  SaAisErrorT err = SA_AIS_OK;
  bool modifiedNotifyAttr = false;
  if (spApplConnPtr) {
    (*spApplConnPtr) = 0;
  }

  // Even if Imm is not writable (sync is on-going) we must allow
  // updates of non-cached and non-persistent attributes.

  size_t sz = strnlen((char*)req->objectName.buf, (size_t)req->objectName.size);
  std::string objectName((const char*)req->objectName.buf, sz);

  TRACE_5("Update runtime attributes in object '%s'", objectName.c_str());

  ClassInfo* classInfo = 0;
  ObjectInfo* object = 0;
  bool isPersistent = false;
  AttrMap::iterator i4;
  ObjectMap::iterator oi;
  ImmAttrValueMap::iterator oavi;
  ImplementerInfo* info = NULL;
  bool wasLocal = *isPureLocal;
  bool isSyncClient = (sImmNodeState == IMM_NODE_W_AVAILABLE);
  bool longDnsPermitted = getLongDnsAllowed();
  if (wasLocal) {
    osafassert(conn);
  }

  if (objectName.empty()) {
    LOG_NO("ERR_INVALID_PARAM: Empty DN value");
    err = SA_AIS_ERR_INVALID_PARAM;
    goto rtObjectUpdateExit;
  }

  if (!(nameCheck(objectName) || nameToInternal(objectName))) {
    LOG_NO("ERR_INVALID_PARAM: Not a proper object name");
    err = SA_AIS_ERR_INVALID_PARAM;
    goto rtObjectUpdateExit;
  }

  oi = sObjectMap.find(objectName);
  if (oi == sObjectMap.end()) {
    if (isSyncClient) {
      TRACE_7(
          "rtObjectUpdate: Object '%s' is missing, "
          "but this node is being synced",
          objectName.c_str());
      /* Bogus error message, encodes this case. */
      err = SA_AIS_ERR_REPAIR_PENDING;
    } else {
      TRACE_7("ERR_NOT_EXIST: object '%s' does not exist", objectName.c_str());
      err = SA_AIS_ERR_NOT_EXIST;
    }

    goto rtObjectUpdateExit;

  } else if (oi->second->mObjFlags & IMM_CREATE_LOCK) {
    TRACE_7(
        "ERR_NOT_EXIST: object '%s' registered for creation "
        "but not yet applied (ccb or PRTO PBE)",
        objectName.c_str());
    err = SA_AIS_ERR_NOT_EXIST;
    goto rtObjectUpdateExit;
  } else if (oi->second->mObjFlags & IMM_DELETE_LOCK) {
    TRACE_7(
        "ERR_TRY_AGAIN: object '%s' registered for delete "
        "but not yet applied (PRTO PBE)",
        objectName.c_str());
    err = SA_AIS_ERR_TRY_AGAIN;
    goto rtObjectUpdateExit;
  }

  object = oi->second;

  if (!isSyncClient) {
    /*Prevent abuse from wrong implementer.*/
    /*Should rename member adminOwnerId. Used to store implid here.*/
    info = findImplementer(req->adminOwnerId);
    if (!info) {
      LOG_NO("ERR_BAD_HANDLE: Not a correct implementer handle %u",
             req->adminOwnerId);
      err = SA_AIS_ERR_BAD_HANDLE;
      goto rtObjectUpdateExit;
    }

    if ((info->mConn != conn) || (info->mNodeId != nodeId)) {
      LOG_NO(
          "ERR_BAD_OPERATION: The provided implementer handle %u does "
          "not correspond to the actual connection <%u, %x> != <%u, %x>",
          req->adminOwnerId, info->mConn, info->mNodeId, conn, nodeId);
      err = SA_AIS_ERR_BAD_OPERATION;
      goto rtObjectUpdateExit;
    }
    // conn is NULL on all nodes except hosting node
    // At these other nodes the only info on implementer existence is that
    // the nodeId is non-zero. The nodeId is the nodeId of the node where
    // the implementer resides.

    if (!object->mImplementer || (object->mImplementer->mConn != conn) ||
        (object->mImplementer->mNodeId != nodeId)) {
      LOG_NO(
          "ERR_BAD_OPERATION: Not a correct implementer handle or object "
          "not handled by the implementer conn:%u nodeId:%x "
          "object->mImplementer:%p",
          conn, nodeId, object->mImplementer);
      err = SA_AIS_ERR_BAD_OPERATION;
      goto rtObjectUpdateExit;
    }
  }

  classInfo = object->mClassInfo;
  osafassert(classInfo);

  // if(classInfo->mCategory != SA_IMM_CLASS_RUNTIME) {
  // LOG_WA("object '%s' is not a runtime object", objectName.c_str());
  // return SA_AIS_ERR_BAD_OPERATION;
  //}
  // Note: above check was incorrect because this call SHOULD be
  // allowed to update config objects, but only runtime attributes..

  // Find each attribute to be modified.
  // Check that attribute is a runtime attribute.
  // If multivalue implied check that it is multivalued.
  // Assign the attribute as intended.

  for (int doIt = 0; (doIt < 2) && (err == SA_AIS_OK); ++doIt) {
    TRACE_5("update rt attributes doit: %u", doIt);
    void* pbe = NULL;
    void* pbe2B = NULL;
    ImmAttrValueMap::iterator oavi;
    if (doIt && pbeNodeIdPtr && isPersistent && !wasLocal) {
      ObjectInfo* afim = 0;
      ImmAttrValueMap::iterator oavi;
      ObjectMutation* oMut = 0;
      unsigned int slaveNodeId = 0;

      TRACE(
          "PRT ATTRs UPDATE case, defer updates of cached attrs, until ACK from PBE");
      /*
         We expect a PBE and the list of cached attributes to update includes
         some PERSISTENT ATTRs, then dont update any cached RTattrs now,
         instead flag the object (can be a config object) and create a
         an afterimage mutation to hold the updates. Postpone the updates
         until we get an ack from PBE. Note that even non persistent but
         cached rtattr updates included are not updated untile after the ack,
         because we may need to revert the entire operation.
      */
      pbe = getPbeOi(pbeConnPtr, pbeNodeIdPtr);
      if (!pbe) {
        LOG_WA("ERR_TRY_AGAIN: Persistent back end is down - unexpected here");
        err = SA_AIS_ERR_TRY_AGAIN;
        goto rtObjectUpdateExit;
        /* We have already checked for PbeWritable with success inside
           deleteRtObject. Why do we fail in obtaining the PBE data now??
           We could have an osafassert here, but since we have not done any
           mutations yet (doit just turned true), we can get away with a
           TRY_AGAIN.
        */
      }

      pbe2B = getPbeBSlave(pbe2BConnPtr, &slaveNodeId);

      /* If 2PBE then not both PBEs on same processor. */
      osafassert(!((*pbeConnPtr) && (*pbe2BConnPtr)));

      if (++sLastContinuationId >= 0xfffffffe) {
        sLastContinuationId = 1;
      }
      *continuationIdPtr = sLastContinuationId;
      TRACE("continuation generated: %u", *continuationIdPtr);

      if (conn) {
        SaInvocationT tmp_hdl =
            m_IMMSV_PACK_HANDLE((*continuationIdPtr), nodeId);
        sPbeRtReqContinuationMap[tmp_hdl] =
            ContinuationInfo2(conn, DEFAULT_TIMEOUT_SEC);
      }

      TRACE_5(
          "Tentative update of cached runtime attribute(s) for "
          "object '%s' by Impl %s pending PBE ack",
          oi->first.c_str(), info->mImplementerName.c_str());

      object->mObjFlags |= IMM_RT_UPDATE_LOCK;

      /* Note: There is no error case where we leak afim, because doIt==true.
         That is all checks have been passed once already.
      */
      afim = new ObjectInfo();
      afim->mClassInfo = object->mClassInfo;
      afim->mImplementer = object->mImplementer;
      afim->mObjFlags = object->mObjFlags;
      afim->mParent = object->mParent;
      afim->mChildCount =
          object->mChildCount; /* Not used, but be consistent. */
      // Copy attribute values from existing object version to afim
      for (oavi = object->mAttrValueMap.begin();
           oavi != object->mAttrValueMap.end(); ++oavi) {
        ImmAttrValue* oldValue = oavi->second;
        ImmAttrValue* newValue = NULL;

        i4 = classInfo->mAttrMap.find(oavi->first);
        osafassert(i4 != classInfo->mAttrMap.end());
        /* Only copy runtime attributes to afim. */
        if (i4->second->mFlags & SA_IMM_ATTR_RUNTIME) {
          if (oldValue->isMultiValued()) {
            newValue = new ImmAttrMultiValue(*((ImmAttrMultiValue*)oldValue));
          } else {
            newValue = new ImmAttrValue(*oldValue);
          }

          // Set admin owner as a regular attribute and then also a pointer
          // to the attrValue for efficient access.
          if (oavi->first == std::string(SA_IMM_ATTR_ADMIN_OWNER_NAME)) {
            afim->mAdminOwnerAttrVal = newValue;
          }
          afim->mAttrValueMap[oavi->first] = newValue;
        }
      }

      oMut = new ObjectMutation(IMM_MODIFY);
      oMut->mAfterImage = afim;
      oMut->mContinuationId = (*continuationIdPtr);
      oMut->m2PbeCount = pbe2B ? 2 : 1;
      sPbeRtMutations[objectName] = oMut;

      object = afim; /* Rest of the code below updates the afim and not
                        the master object.*/
    }

    immsv_attr_mods_list* p = req->attrMods;
    while (p && (err == SA_AIS_OK)) {
      sz = strnlen((char*)p->attrValue.attrName.buf,
                   (size_t)p->attrValue.attrName.size);
      std::string attrName((const char*)p->attrValue.attrName.buf, sz);

      if (doIt) {
        TRACE_5("update rt attribute '%s'", attrName.c_str());
      }

      ImmAttrValueMap::iterator i5 = object->mAttrValueMap.find(attrName);

      if (i5 == object->mAttrValueMap.end()) {
        TRACE_7("ERR_NOT_EXIST: attr '%s' not defined", attrName.c_str());
        err = SA_AIS_ERR_NOT_EXIST;
        osafassert(!doIt);
        break;  // out of while-loop
      }

      ImmAttrValue* attrValue = i5->second;

      i4 = classInfo->mAttrMap.find(attrName);
      if (i4 == classInfo->mAttrMap.end()) {
        LOG_ER("i4==classInfo->mAttrMap.end())");
        osafassert(i4 != classInfo->mAttrMap.end());
      }

      AttrInfo* attr = i4->second;

      if (attr->mValueType != (unsigned int)p->attrValue.attrValueType) {
        LOG_NO("ERR_INVALID_PARAM: attr '%s' type missmatch", attrName.c_str());
        err = SA_AIS_ERR_INVALID_PARAM;
        osafassert(!doIt);
        break;  // out of while-loop
      }

      if (attr->mFlags & SA_IMM_ATTR_CONFIG) {
        osafassert(!doIt);
        LOG_NO(
            "ERR_INVALID_PARAM: attr '%s' is a config attribute => "
            "can not be modified over OI-API.",
            attrName.c_str());
        err = SA_AIS_ERR_INVALID_PARAM;
        break;  // out of while-loop
      }

      if (attr->mFlags & SA_IMM_ATTR_RDN) {
        LOG_NO(
            "ERR_INVALID_PARAM: attr '%s' is the RDN attribute => "
            "can not be modified over OI-API.",
            attrName.c_str());
        err = SA_AIS_ERR_INVALID_PARAM;
        osafassert(!doIt);
        break;  // out of while-loop
      }

      if ((attr->mValueType == SA_IMM_ATTR_SANAMET ||
           (attr->mValueType == SA_IMM_ATTR_SASTRINGT &&
            (attr->mFlags & SA_IMM_ATTR_DN))) &&
          !longDnsPermitted) {
        if (p->attrValue.attrValue.val.x.size >=
            SA_MAX_UNEXTENDED_NAME_LENGTH) {
          LOG_NO(
              "ERR_BAD_OPERATION: Attribute '%s' has long DN. "
              "Not allowed by IMM service or extended names are disabled",
              attrName.c_str());
          err = SA_AIS_ERR_BAD_OPERATION;
          break;  // out of while-loop
        }
        if ((attr->mFlags & SA_IMM_ATTR_MULTI_VALUE) &&
            p->attrValue.attrValuesNumber > 1) {
          IMMSV_EDU_ATTR_VAL_LIST* values = p->attrValue.attrMoreValues;
          while (values) {
            if (values->n.val.x.size >= SA_MAX_UNEXTENDED_NAME_LENGTH) {
              LOG_NO(
                  "ERR_BAD_OPERATION: Attribute '%s' has long DN. "
                  "Not allowed by IMM service or extended names are disabled",
                  attrName.c_str());
              err = SA_AIS_ERR_BAD_OPERATION;
              break;  // out of while-loop
            }
            values = values->next;
          }
          if (err != SA_AIS_OK) {
            break;  // out of while-loop
          }
        }
      }

      if (attr->mValueType == SA_IMM_ATTR_SANAMET) {
        if (p->attrValue.attrValue.val.x.size > kOsafMaxDnLength) {
          LOG_NO("ERR_LIBRARY: attr '%s' of type SaNameT is too long:%u",
                 attrName.c_str(), p->attrValue.attrValue.val.x.size - 1);
          err = SA_AIS_ERR_LIBRARY;
          break;  // out of for-loop
        }

        std::string tmpName(p->attrValue.attrValue.val.x.buf,
                            p->attrValue.attrValue.val.x.size
                                ? p->attrValue.attrValue.val.x.size - 1
                                : 0);
        if (!(nameCheck(tmpName) || nameToInternal(tmpName))) {
          LOG_NO(
              "ERR_INVALID_PARAM: attr '%s' of type SaNameT contains non "
              "printable characters",
              attrName.c_str());
          err = SA_AIS_ERR_INVALID_PARAM;
          break;  // out of for-loop
        }
      } else if (attr->mValueType == SA_IMM_ATTR_SASTRINGT) {
        /* Check that the string at least conforms to UTF-8 */
        if (p->attrValue.attrValue.val.x.size &&
            !(osaf_is_valid_utf8(p->attrValue.attrValue.val.x.buf))) {
          LOG_NO(
              "ERR_INVALID_PARAM: attr '%s' of type SaStringT has a value "
              "that is not valid UTF-8",
              attrName.c_str());
          err = SA_AIS_ERR_INVALID_PARAM;
          break;  // out of for-loop
        }
      }

      if (attr->mFlags & SA_IMM_ATTR_CACHED ||
          attr->mFlags & SA_IMM_ATTR_PERSISTENT) {
        // A non-local runtime attribute
        if (!doIt) {
          TRACE_5("A non local runtime attribute '%s'", attrName.c_str());
        }

        /* Enhancement ticket #1338 => Allow updates to cached
          (non-persistent) RT attrs concurrently with sync.
          The persistent case is caught below.
         if(immNotWritable()) {
             //Dont allow writes to cached or persistent rt-attrs
             //during sync
             osafassert(!doIt);
             err = SA_AIS_ERR_TRY_AGAIN;
             TRACE_5("IMM not writable => Cant update cahced rtattrs");
             break;//out of while-loop
         }
        */

        if (!doIt && (object->mObjFlags & IMM_RT_UPDATE_LOCK)) {
          err = SA_AIS_ERR_TRY_AGAIN;
          /* Sorry but any ongoing PRTO update will interfere with additional
              updates of cached RTOs because the PRTO update has to be
             reversible.
           */
          LOG_IN(
              "ERR_TRY_AGAIN: Object '%s' already subject of a persistent runtime "
              "attribute update",
              objectName.c_str());
          break;  // out of while-loop
        }

        if (attr->mFlags & SA_IMM_ATTR_PERSISTENT) {
          isPersistent = true;
          if (immNotPbeWritable()) {
            err = SA_AIS_ERR_TRY_AGAIN;
            TRACE_5(
                "ERR_TRY_AGAIN: IMM not persistent writable => Cant update persistent RTO");
            break;  // out of while-loop
          }

          /* Check for ccb interference if it is a config object */
          if (!doIt && classInfo->mCategory == SA_IMM_CLASS_CONFIG &&
              object->mCcbId) {
            CcbVector::iterator ci = std::find_if(
                sCcbVector.begin(), sCcbVector.end(), CcbIdIs(object->mCcbId));
            if ((ci != sCcbVector.end()) && ((*ci)->isActive())) {
              LOG_NO(
                  "ERR_TRY_AGAIN: Object %s is part of active ccb %u, can not "
                  "allow PRT attr updates",
                  objectName.c_str(), object->mCcbId);
              err = SA_AIS_ERR_TRY_AGAIN;
              break;  // out of while-loop
              /* ERR_BAD_OPERATION would be more appropriate, but standard does
                 not allow it.
              */
            }
          }

          if (doIt && !wasLocal) {
            TRACE_5("Update of PERSISTENT runtime attr %s in object %s",
                    attrName.c_str(), objectName.c_str());
          }
        }

        *isPureLocal = false;
        if (wasLocal) {
          p = p->next;
          continue;
        }  // skip non-locals (cached), on first and local invocation.
        // will be set over fevs.
      } else {
        // A local runtime attribute.
        if (!wasLocal) {
          p = p->next;
          continue;
        }  // skip pure locals when invocation was not local.
      }

      if (attr->mFlags & SA_IMM_ATTR_NOTIFY) {
        osafassert(!wasLocal);
        modifiedNotifyAttr = true;
      }

      IMMSV_OCTET_STRING tmpos;  // temporary octet string

      switch (p->attrModType) {
        case SA_IMM_ATTR_VALUES_REPLACE:

          TRACE_3("SA_IMM_ATTR_VALUES_REPLACE");
          // Remove existing values, then fall through to the add case
          if (doIt) {
            attrValue->discardValues();
          }
          if (p->attrValue.attrValuesNumber == 0) {
            if (attr->mFlags & SA_IMM_ATTR_STRONG_DEFAULT) {
              LOG_WA(
                  "There's an attempt to set attr '%s' to NULL, "
                  "default value will be set",
                  attrName.c_str());
              osafassert(!attr->mDefaultValue.empty());
              (*attrValue) = attr->mDefaultValue;
            }

            p = p->next;
            continue;  // Ok to replace with nothing.
          }
        // else intentional fall-through

        case SA_IMM_ATTR_VALUES_ADD:
          TRACE_3("SA_IMM_ATTR_VALUES_ADD");
          if (p->attrValue.attrValuesNumber == 0) {
            osafassert(!doIt);
            LOG_NO(
                "ERR_INVALID_PARAM: Empty value used for adding to attribute %s",
                attrName.c_str());
            err = SA_AIS_ERR_INVALID_PARAM;
            break;  // out of switch
          }

          if (doIt) {
            eduAtValToOs(&tmpos, &(p->attrValue.attrValue),
                         (SaImmValueTypeT)p->attrValue.attrValueType);

            if (p->attrValue.attrValueType == SA_IMM_ATTR_SASTRINGT) {
              TRACE("Update attribute of type string:%s  %s", tmpos.buf,
                    p->attrValue.attrValue.val.x.buf);
            }

            if (p->attrValue.attrValueType == SA_IMM_ATTR_SAUINT32T) {
              TRACE("Update attribute of type uint:%u",
                    p->attrValue.attrValue.val.sauint32);
            }
          }

          if (p->attrModType == SA_IMM_ATTR_VALUES_REPLACE ||
              attrValue->empty()) {
            if (doIt) {
              osafassert(attrValue->empty());
              attrValue->setValue(tmpos);
            }
          } else {
            if (!(attr->mFlags & SA_IMM_ATTR_MULTI_VALUE)) {
              osafassert(!doIt);
              LOG_NO(
                  "ERR_INVALID_PARAM: attr '%s' is not multivalued, yet "
                  "multiple values attempted",
                  attrName.c_str());
              err = SA_AIS_ERR_INVALID_PARAM;
              break;  // out of switch
            }

            osafassert(attrValue->isMultiValued());
            ImmAttrMultiValue* multiattr = (ImmAttrMultiValue*)attrValue;
            eduAtValToOs(&tmpos, &(p->attrValue.attrValue),
                (SaImmValueTypeT)p->attrValue.attrValueType);

            if ((attr->mFlags & SA_IMM_ATTR_NO_DUPLICATES) &&
                (multiattr->hasMatchingValue(tmpos))) {
              LOG_NO(
                  "ERR_INVALID_PARAM: multivalued attr '%s' with "
                  "NO_DUPLICATES yet duplicate values provided in rta-update "
                  "call. Object:'%s'.", attrName.c_str(), objectName.c_str());
              err = SA_AIS_ERR_INVALID_PARAM;
              break;  // out of for switch
            }

            if (doIt) {
              multiattr->setExtraValue(tmpos);
            }
          }

          if (p->attrValue.attrValuesNumber > 1) {
            if (!(attr->mFlags & SA_IMM_ATTR_MULTI_VALUE)) {
              osafassert(!doIt);
              LOG_NO(
                  "ERR_INVALID_PARAM: attr '%s' is not multivalued, yet "
                  "multiple values provided in modify call",
                  attrName.c_str());
              err = SA_AIS_ERR_INVALID_PARAM;
              break;  // out of switch
            }

            osafassert(attrValue->isMultiValued());
            ImmAttrMultiValue* multiattr = (ImmAttrMultiValue*)attrValue;
            IMMSV_EDU_ATTR_VAL_LIST* al = p->attrValue.attrMoreValues;

            while (al) {
              eduAtValToOs(&tmpos, &(al->n),
                  (SaImmValueTypeT)p->attrValue.attrValueType);
              if ((attr->mFlags & SA_IMM_ATTR_NO_DUPLICATES) &&
                  (multiattr->hasMatchingValue(tmpos))) {
                LOG_NO(
                    "ERR_INVALID_PARAM: multivalued attr '%s' with "
                    "NO_DUPLICATES yet duplicate values provided in rta-update "
                    "call. Object:'%s'.", attrName.c_str(), objectName.c_str());
                err = SA_AIS_ERR_INVALID_PARAM;
                break;  // out of loop
              }

              if (doIt) {
                multiattr->setExtraValue(tmpos);
              }

              al = al->next;
            }
          }
          break;

        case SA_IMM_ATTR_VALUES_DELETE:
          TRACE_3("SA_IMM_ATTR_VALUES_DELETE");
          // Delete all values that match that provided by invoker.
          if (p->attrValue.attrValuesNumber == 0) {
            osafassert(!doIt);
            LOG_NO(
                "ERR_INVALID_PARAM: Empty value used for deleting from "
                "attribute %s",
                attrName.c_str());
            err = SA_AIS_ERR_INVALID_PARAM;
            break;  // out of switch
          }

          if (!attrValue->empty() && doIt) {
            eduAtValToOs(&tmpos, &(p->attrValue.attrValue),
                         (SaImmValueTypeT)p->attrValue.attrValueType);
            attrValue->removeValue(tmpos);

            // Note: We allow the delete value to be multivalued
            // even if the attribute is single valued.
            // The semantics is that we delete ALL matchings of ANY
            // delete value.
            if (p->attrValue.attrValuesNumber > 1) {
              IMMSV_EDU_ATTR_VAL_LIST* al = p->attrValue.attrMoreValues;
              while (al) {
                eduAtValToOs(&tmpos, &(al->n),
                             (SaImmValueTypeT)p->attrValue.attrValueType);
                attrValue->removeValue(tmpos);
                al = al->next;
              }
            }

            if (attrValue->empty() &&
                (attr->mFlags & SA_IMM_ATTR_STRONG_DEFAULT)) {
              LOG_WA(
                  "There's an attempt to set attr '%s' to NULL, "
                  "default value will be set",
                  attrName.c_str());
              osafassert(!attr->mDefaultValue.empty());
              (*attrValue) = attr->mDefaultValue;
            }
          }
          break;  // out of switch

        default:
          err = SA_AIS_ERR_INVALID_PARAM;
          LOG_NO(
              "ERR_INVALID_PARAM: Illegal value for SaImmAttrModificationTypeT: %u",
              p->attrModType);
          break;
      }

      if (err != SA_AIS_OK) {
        break;  // out of while-loop
      }
      p = p->next;
    }  // while(p)
    // err!=OK => breaks out of for loop
  }  // for(int doIt...

  if (modifiedNotifyAttr && (err == SA_AIS_OK)) {
    ImplementerInfo* spAppl = getSpecialApplier();
    if (spAppl && spApplConnPtr) {
      (*spApplConnPtr) = spAppl->mConn;
    }
  }
rtObjectUpdateExit:
  TRACE_5("isPureLocal: %u when leaving", *isPureLocal);
  TRACE_LEAVE();
  return err;
}

/**
 * Delete a runtime object
 */
SaAisErrorT ImmModel::rtObjectDelete(
    const ImmsvOmCcbObjectDelete* req, SaUint32T reqConn, unsigned int nodeId,
    SaUint32T* continuationIdPtr, SaUint32T* pbeConnPtr,
    unsigned int* pbeNodeIdPtr, ObjectNameVector& objNameVector,
    SaUint32T* spApplConnPtr, SaUint32T* pbe2BConnPtr) {
  osafassert(spApplConnPtr);
  (*spApplConnPtr) = 0;

  bool subTreeHasPersistent = false;
  bool subTreeHasSpecialAppl = false;

  TRACE_ENTER2("cont:%p connp:%p nodep:%p", continuationIdPtr, pbeConnPtr,
               pbeNodeIdPtr);
  SaAisErrorT err = SA_AIS_OK;
  ImplementerInfo* info = NULL;
  if (pbeNodeIdPtr) {
    (*pbeNodeIdPtr) = 0;
    osafassert(continuationIdPtr);
    (*continuationIdPtr) = 0;
    osafassert(pbeConnPtr);
    (*pbeConnPtr) = 0;
    osafassert(pbe2BConnPtr);
    (*pbe2BConnPtr) = 0;
  }

  if (immNotWritable() || is_sync_aborting()) { /*Check for persistent RTOs further down. */
    TRACE_LEAVE();
    return SA_AIS_ERR_TRY_AGAIN;
  }

  size_t sz = strnlen((char*)req->objectName.buf, (size_t)req->objectName.size);
  std::string objectName((const char*)req->objectName.buf, sz);

  TRACE_2("Delete runtime object '%s' and all subobjects\n",
          objectName.c_str());

  ObjectMap::iterator oi, oi2;

  if (!(nameCheck(objectName) || nameToInternal(objectName))) {
    LOG_NO("ERR_INVALID_PARAM: Not a proper object name");
    err = SA_AIS_ERR_INVALID_PARAM;
    goto rtObjectDeleteExit;
  }

  if (objectName.empty()) {
    LOG_NO("ERR_INVALID_PARAM: Empty DN attribute value");
    err = SA_AIS_ERR_INVALID_PARAM;
    goto rtObjectDeleteExit;
  }

  oi = sObjectMap.find(objectName);
  if (oi == sObjectMap.end()) {
    TRACE_7("ERR_NOT_EXIST: object '%s' does not exist", objectName.c_str());
    err = SA_AIS_ERR_NOT_EXIST;
    goto rtObjectDeleteExit;
  } else if (oi->second->mObjFlags & IMM_CREATE_LOCK) {
    TRACE_7(
        "ERR_NOT_EXIST: object '%s' registered for creation "
        "but not yet applied PRTO PBE ?",
        objectName.c_str());
    err = SA_AIS_ERR_NOT_EXIST;
    goto rtObjectDeleteExit;
  } else if (oi->second->mObjFlags & IMM_DELETE_LOCK) {
    TRACE_7(
        "ERR_TRY_AGAIN: object '%s' registered for deletion "
        "but not yet applied PRTO PBE ?",
        objectName.c_str());
    err = SA_AIS_ERR_TRY_AGAIN;
    goto rtObjectDeleteExit;
  } else if (oi->second->mObjFlags & IMM_RT_UPDATE_LOCK) {
    TRACE_7(
        "ERR_TRY_AGAIN: Object '%s' already subject of a persistent runtime "
        "attribute update",
        objectName.c_str());
    err = SA_AIS_ERR_TRY_AGAIN;
    goto rtObjectDeleteExit;
  }

  /*Prevent abuse from wrong implementer.*/
  /*Should rename member adminOwnerId. Used to store implid here.*/
  info = findImplementer(req->adminOwnerId);
  if (!info) {
    LOG_NO("ERR_BAD_HANDLE: Not a correct implementer handle %u",
           req->adminOwnerId);
    err = SA_AIS_ERR_BAD_HANDLE;
    goto rtObjectDeleteExit;
  }

  if ((info->mConn != reqConn) || (info->mNodeId != nodeId)) {
    LOG_NO(
        "ERR_BAD_OPERATION: The provided implementer handle %u does "
        "not correspond to the actual connection <%u, %x> != <%u, %x>",
        req->adminOwnerId, info->mConn, info->mNodeId, reqConn, nodeId);
    err = SA_AIS_ERR_BAD_OPERATION;
    goto rtObjectDeleteExit;
  }
  // reqConn is NULL on all nodes except primary.
  // At these other nodes the only info on implementer existence is that
  // the nodeId is non-zero. The nodeId is the nodeId of the node where
  // the implementer resides.

  for (int doIt = 0; (doIt < 2) && (err == SA_AIS_OK); ++doIt) {
    void* pbe = NULL;
    void* pbe2B = NULL;
    SaUint32T childCount = oi->second->mChildCount;

    if (doIt && pbeNodeIdPtr && subTreeHasPersistent) {
      unsigned int slaveNodeId = 0;
      TRACE("PRTO DELETE case, deferred deletes until ACK from PBE");
      /* We expect a PBE and the recursive RTO delete includes
         some PERSISETENT RTOs, then dont delete the RTOs now,
         instead flag each object with a DELETE_LOCK and postpone
         the actual deletes pending an ack from PBE. Note that
         even non persistent RTOs that happen to be included
         in the subtree to be deleted, will be flag'ed with
         DELETE_LOCK and wait for the PBE ack. The reason is that
         the *entire* subtree delete must be atomic. If the PBE
         has problems, the recursive delete may get reverted. This
         reversion has to include non persistent RTOs.
      */
      pbe = getPbeOi(pbeConnPtr, pbeNodeIdPtr);
      if (!pbe) {
        LOG_NO("ERR_TRY_AGAIN: Persistent back end is down - unexpected here");
        err = SA_AIS_ERR_TRY_AGAIN;
        goto rtObjectDeleteExit;
        /* We have already checked for PbeWritable with success inside
           deleteRtObject. Why do we fail in obtaining the PBE data now??
           We could have an osafassert here, but since we have not done any
           mutations yet (doit just turned true), we can get away with a
           TRY_AGAIN.
        */
      }

      pbe2B = getPbeBSlave(pbe2BConnPtr, &slaveNodeId);
      TRACE("getPbeBSlave returned: %p", pbe2B);
      /* If 2PBE then not both PBEs on same processor. */
      osafassert(!((*pbeConnPtr) && (*pbe2BConnPtr)));

      /* If the subtree to delete includes PRTOs then we use a continuationId
         as a common pseudo ccbId for all the RTOs in the subtree.
      */
      if (++sLastContinuationId >= 0xfffffffe) {
        sLastContinuationId = 1;
      }
      *continuationIdPtr = sLastContinuationId;
      TRACE("continuation generated: %u", *continuationIdPtr);

      if (reqConn) {
        SaInvocationT tmp_hdl =
            m_IMMSV_PACK_HANDLE((*continuationIdPtr), nodeId);
        sPbeRtReqContinuationMap[tmp_hdl] =
            ContinuationInfo2(reqConn, DEFAULT_TIMEOUT_SEC);
      }

      TRACE_5(
          "Tentative delete of runtime object '%s' "
          "by Impl %s pending PBE ack",
          oi->first.c_str(), info->mImplementerName.c_str());

      oi->second->mObjFlags |= IMM_DELETE_LOCK;
      oi->second->mObjFlags |= IMM_DELETE_ROOT;

      /* Dont overwrite IMM_DN_INTERNAL_REP */
      ObjectMutation* oMut = new ObjectMutation(IMM_DELETE);
      oMut->mContinuationId = (*continuationIdPtr);
      oMut->mAfterImage = oi->second;
      /* We count replies from both PBE's, despite that slave is
         called after primary for PRTO deletes. This unifies the
         error/recovery processing for all PRT ops towards PBE.
      */
      oMut->m2PbeCount = pbe2B ? 2 : 1;
      sPbeRtMutations[oi->first] = oMut;
      if ((oi->second->mObjFlags & IMM_PRTO_FLAG) &&
          ((*pbeConnPtr) || (*pbe2BConnPtr))) {
        TRACE("PRTO flag was set for root of subtree to delete");
        if (oi->second->mObjFlags & IMM_DN_INTERNAL_REP) {
          std::string tmpName(oi->first);
          nameToExternal(tmpName);
          objNameVector.push_back(tmpName);
        } else {
          objNameVector.push_back(oi->first);
        }
      }
    } else { /* No PBE or no PRTOs in subtree => immediate delete */
      if (doIt) {
        /*  No need to set DELETE_ROOT flag since we are deleting here&now.
            Adjust ChildCount for parent(s) of the root for the delete-op
         */
        ObjectInfo* grandParent = oi->second->mParent;
        while (grandParent) {
          osafassert(grandParent->mChildCount >= (oi->second->mChildCount + 1));
          grandParent->mChildCount -= (oi->second->mChildCount + 1);
          TRACE_5(
              "Childcount for (grand)parent of deleted RTO %s adjusted to %u",
              objectName.c_str(), grandParent->mChildCount);
          grandParent = grandParent->mParent;
        }

        if (subTreeHasSpecialAppl) {
          ImplementerInfo* spImpl = getSpecialApplier();
          if (spImpl && spImpl->mConn) {
            (*spApplConnPtr) = spImpl->mConn;

            if (oi->second->mObjFlags & IMM_RTNFY_FLAG) {
              TRACE("NOTIFY flag was set for root of subtree to delete");
              if (oi->second->mObjFlags & IMM_DN_INTERNAL_REP) {
                std::string tmpName(oi->first);
                nameToExternal(tmpName);
                objNameVector.push_back(tmpName);
              } else {
                objNameVector.push_back(oi->first);
              }
              oi->second->mObjFlags &= ~IMM_RTNFY_FLAG;
            }
          } else {
            /* No special applier connected at this node. */
            subTreeHasSpecialAppl = false;
            osafassert(!(*spApplConnPtr));
          }
        }
      }
      err = deleteRtObject(oi, doIt, info, subTreeHasPersistent,
                           subTreeHasSpecialAppl);
    }

    // Find all sub objects to the deleted object and delete them
    for (oi2 = sObjectMap.begin();
         oi2 != sObjectMap.end() && err == SA_AIS_OK && childCount;) {
      bool deleted = false;
      std::string subObjName = oi2->first;
      if (subObjName.length() > objectName.length()) {
        size_t pos = subObjName.length() - objectName.length();
        if ((subObjName.rfind(objectName, pos) == pos) &&
            (subObjName[pos - 1] == ',')) {
          --childCount;
          if (doIt && pbeNodeIdPtr && subTreeHasPersistent) {
            TRACE_5(
                "Tentative delete of runtime object '%s' "
                "by Impl %s pending PBE ack",
                subObjName.c_str(), info->mImplementerName.c_str());

            oi2->second->mObjFlags |= IMM_DELETE_LOCK;
            /* Dont overwrite IMM_DN_INTERNAL_REP */
            ObjectMutation* oMut = new ObjectMutation(IMM_DELETE);
            oMut->mContinuationId = (*continuationIdPtr);
            oMut->mAfterImage = oi2->second;
            sPbeRtMutations[subObjName] = oMut;
            if ((oi2->second->mObjFlags & IMM_PRTO_FLAG) &&
                ((*pbeConnPtr) || (*pbe2BConnPtr))) {
              TRACE("PRTO flag was set for subobj: %s", subObjName.c_str());
              if (oi2->second->mObjFlags & IMM_DN_INTERNAL_REP) {
                std::string tmpName(subObjName);
                nameToExternal(tmpName);
                objNameVector.push_back(tmpName);
              } else {
                objNameVector.push_back(subObjName);
              }
            }
          } else {
            /* No PBE or no PRTOs in subtree => immediate delete */
            if (doIt && (*spApplConnPtr) &&
                (oi2->second->mObjFlags & IMM_RTNFY_FLAG)) {
              TRACE("NOTIFY flag was set for subobj: %s", subObjName.c_str());
              if (oi2->second->mObjFlags & IMM_DN_INTERNAL_REP) {
                std::string tmpName(subObjName);
                nameToExternal(tmpName);
                objNameVector.push_back(tmpName);
              } else {
                objNameVector.push_back(subObjName);
              }
              oi2->second->mObjFlags &= ~IMM_RTNFY_FLAG;
            }
            ObjectMap::iterator oi3 = (doIt) ? (oi2++) : oi2;
            err = deleteRtObject(oi3, doIt, info, subTreeHasPersistent,
                                 subTreeHasSpecialAppl);
            deleted = doIt;
          }  // else
          if (!childCount) {
            TRACE("Cutoff in rtObj delete loop by childCount");
          }
        }  // if
      }    // if
      if (!deleted) {
        ++oi2;
      }
    }  // for
  }

rtObjectDeleteExit:
  TRACE_LEAVE();
  return err;
}

SaAisErrorT ImmModel::deleteRtObject(ObjectMap::iterator& oi, bool doIt,
                                     ImplementerInfo* info,
                                     bool& subTreeHasPersistent,
                                     bool& subTreeHasSpecialAppl) {
  TRACE_ENTER();
  /* If param info!=NULL then this is a normal RTO delete.
     If param info==NULL then this is an immsv internal delete.
     Either a revert of PRTO PBE create due to NACK from PBE.
     OR materialization of a delete duw to ACK from PBE.
     See: ImmModel::pbePrtObjCreateContinuation and
     ImmModl:pbePrtoObjDeletesContinuation.
   */
  ObjectInfo* object = oi->second;
  osafassert(object);
  ClassInfo* classInfo = object->mClassInfo;
  osafassert(classInfo);

  AttrMap::iterator i4;

  if (info && (!object->mImplementer || object->mImplementer != info)) {
    LOG_NO(
        "ERR_BAD_OPERATION: Not a correct implementer handle "
        "or object %s is not handled by the provided implementer "
        "named %s (!=%p <name:'%s' conn:%u, node:%x>)",
        oi->first.c_str(), info->mImplementerName.c_str(), object->mImplementer,
        (object->mImplementer)
            ? (object->mImplementer->mImplementerName.c_str())
            : "NIL",
        (object->mImplementer) ? (object->mImplementer->mConn) : 0,
        (object->mImplementer) ? (object->mImplementer->mNodeId) : 0);
    return SA_AIS_ERR_BAD_OPERATION;
  }

  if (classInfo->mCategory != SA_IMM_CLASS_RUNTIME) {
    LOG_NO("ERR_BAD_OPERATION: object '%s' is not a runtime object",
           oi->first.c_str());
    return SA_AIS_ERR_BAD_OPERATION;
  }

  i4 = std::find_if(classInfo->mAttrMap.begin(), classInfo->mAttrMap.end(),
                    AttrFlagIncludes(SA_IMM_ATTR_PERSISTENT));

  bool isPersistent = i4 != classInfo->mAttrMap.end();

  if (isPersistent) {
    ObjectMMap::iterator ommi = sReverseRefsNoDanglingMMap.find(object);
    if (ommi != sReverseRefsNoDanglingMMap.end()) {
      std::string srcObject;
      getObjectName(ommi->second, srcObject);
      TRACE(
          "ERR_BAD_OPERATION: Object %s is the target "
          "of a NO_DANGLING reference in config object %s",
          oi->first.c_str(), srcObject.c_str());
      return SA_AIS_ERR_BAD_OPERATION;
    }

    object->mObjFlags |= IMM_PRTO_FLAG;
  } else {
    /* Will correct for schema change that has removed persistence */
    object->mObjFlags &= ~IMM_PRTO_FLAG;
  }

  i4 = std::find_if(classInfo->mAttrMap.begin(), classInfo->mAttrMap.end(),
                    AttrFlagIncludes(SA_IMM_ATTR_NOTIFY));

  bool isSpecialAppl = i4 != classInfo->mAttrMap.end();

  if (isSpecialAppl) { /* check for spApplCon ?? If not present, why set the
                          flag ? */
    object->mObjFlags |= IMM_RTNFY_FLAG;
    subTreeHasSpecialAppl = true;
  } else {
    /* Will correct for schema change that has removed notify */
    object->mObjFlags &= ~IMM_RTNFY_FLAG;
  }

  if (!info) {
    if (isPersistent) {
      TRACE_7(
          "Exotic deleteRtObj for PERSISTENT rto, "
          "revert on create or ack on delete from PBE");
    } else {
      TRACE_7(
          "Exotic deleteRtObj for regular rto, "
          "revert on create or ack on delete from PBE");
    }
  } else if (isPersistent && !doIt) {
    if (!subTreeHasPersistent && immNotPbeWritable()) {
      /* Clarification of if statement: We are about to set
         subTreeHasPersistent to true.
         Why the condition "!subTreeHasPersistent" ?
         It is just to avoid repeated execution of immNotPbeWritable
         for every object in the subtree. We only need to check
         immNotPbeWritable once in this job, so we do it the first time
         when we are about to set subTreeHasPersistent,
      */
      TRACE_7(
          "TRY_AGAIN: Can not delete persistent RTO when PBE is unavailable");
      return SA_AIS_ERR_TRY_AGAIN;
    }

    subTreeHasPersistent = true;
    TRACE("Detected persistent object %s in subtree to be deleted",
          oi->first.c_str());
  }

  if (object->mObjFlags & IMM_CREATE_LOCK) {
    TRACE_7(
        "ERR_TRY_AGAIN: sub-object '%s' registered for creation "
        "but not yet applied by PRTO PBE ?",
        oi->first.c_str());
    /* Possibly we should return NOT_EXIST here, but the delete may
       be recursive over a tree where only some subobject is being
       created. Returning NOT_EXIST could be very confusing here.
     */
    TRACE("ERR_TRY_AGAIN CREATE LOCK WAS SET");
    return SA_AIS_ERR_TRY_AGAIN;
  }

  if (object->mObjFlags & IMM_DELETE_LOCK) {
    TRACE_7(
        "ERR_TRY_AGAIN: sub-object '%s' already registered for delete "
        "but not yet applied by PRTO PBE ?",
        oi->first.c_str());
    /* Possibly we should return NOT_EXIST here, but the delete may
       be recursive over a tree where only some subobject is being
       deleted. Returning NOT_EXIST could be very confusing here.
     */
    TRACE("ERR_TRY_AGAIN DELETE LOCK WAS SET");
    return SA_AIS_ERR_TRY_AGAIN;
  }

  if (object->mObjFlags & IMM_RT_UPDATE_LOCK) {
    TRACE_7(
        "ERR_TRY_AGAIN: Object '%s' already subject of a persistent runtime "
        "attribute update",
        oi->first.c_str());
    return SA_AIS_ERR_TRY_AGAIN;
  }

  if (doIt) {
    ImmAttrValueMap::iterator oavi;
    for (oavi = object->mAttrValueMap.begin();
         oavi != object->mAttrValueMap.end(); ++oavi) {
      delete oavi->second;
    }
    object->mAttrValueMap.clear();

    osafassert(!classInfo->mExtent.empty());
    classInfo->mExtent.erase(object);

    if (isPersistent) {
      if (info) {
        TRACE("Delete of PERSISTENT runtime object '%s' by Impl: %s",
              oi->first.c_str(), info->mImplementerName.c_str());
      } else {
        TRACE_7(
            "REVERT of create OR ack from PBE on delete of PERSISTENT "
            "runtime object '%s'",
            oi->first.c_str());
      }
    } else {
      LOG_IN("Delete runtime object '%s' by Impl-id: %u", oi->first.c_str(),
             info ? (info->mId) : 0);
    }

    AdminOwnerVector::iterator i2;

    // Delete rt-object from touched objects in admin-owners
    // TODO: This is not so efficient.
    for (i2 = sOwnerVector.begin(); i2 != sOwnerVector.end(); ++i2) {
      (*i2)->mTouchedObjects.erase(oi->second);
      // Note that on STL sets, the erase operation is polymorphic.
      // Here we are erasing based on value, not iterator position.
    }

    delete object;
    sObjectMap.erase(oi);
  }

  return SA_AIS_OK;
}

bool ImmModel::fetchRtUpdate(ImmsvOmObjectSync* syncReq,
                             ImmsvOmCcbObjectModify* modReq,
                             SaUint64T syncFevsBase) {
  bool retVal = false;
  DeferredRtAUpdateList* attrUpdList = NULL;
  TRACE_ENTER2("fetch updates for object%s later than msgNo:%llu",
               syncReq->objectName.buf, syncFevsBase);

  size_t sz =
      strnlen((char*)syncReq->objectName.buf, (size_t)syncReq->objectName.size);
  std::string objectName((const char*)syncReq->objectName.buf, sz);

  DeferredObjUpdatesMap::iterator doumIter =
      sDeferredObjUpdatesMap.find(objectName);
  if (doumIter == sDeferredObjUpdatesMap.end()) {
    return false;
  }

  attrUpdList = doumIter->second;
  while (!attrUpdList->empty()) {
    DeferredRtAUpdate& dRtAU = attrUpdList->front();

    if (dRtAU.fevsMsgNo <= syncFevsBase) {
      TRACE_7("Discarding RtUpdate message fevs %llu (<= %llu)",
              dRtAU.fevsMsgNo, syncFevsBase);
      immsv_free_attrmods(dRtAU.attrModsList);
      dRtAU.attrModsList = NULL;
      attrUpdList->pop_front();
      continue;
    } else {
      LOG_IN("APPLYING deferred RtUpdate message %llu (> %llu)",
             dRtAU.fevsMsgNo, syncFevsBase);
      modReq->attrMods = dRtAU.attrModsList;
      dRtAU.attrModsList = NULL;
      modReq->objectName.size = (SaUint32T)sz;
      modReq->objectName.buf =
          syncReq->objectName.buf; /* Warning borrowed string. */
      retVal = true;
      attrUpdList->pop_front();
      break;
    }
  }

  if (attrUpdList->empty()) {
    delete attrUpdList;
    sDeferredObjUpdatesMap.erase(doumIter);
  }

  return retVal;
}

SaAisErrorT ImmModel::objectSync(const ImmsvOmObjectSync* req) {
  TRACE_ENTER();
  SaAisErrorT err = SA_AIS_OK;
  immsv_attr_values_list* p = NULL;
  bool nameCorrected = false;

  switch (sImmNodeState) {
    case IMM_NODE_W_AVAILABLE:
      break;
    case IMM_NODE_UNKNOWN:
    case IMM_NODE_ISOLATED:
    case IMM_NODE_LOADING:
    case IMM_NODE_FULLY_AVAILABLE:
    case IMM_NODE_R_AVAILABLE:
      LOG_ER(
          "Node is in a state %u that cannot accept "
          "sync message, will terminate",
          sImmNodeState);
      /* cant goto objectSyncExit from here */
      return SA_AIS_ERR_FAILED_OPERATION;
    /* ImmModel::objectSync() is only invoked at sync clients currently.
       See immnd_evt_proc_object_sync in immnd_evt.c.
       ImmModel::objectSync could in the future be used by veteran
       nodes to verify state of config data. If/when that is done then
       the logic here needs to deal with such messages straggling in
       after an aborted sync.
    */
    default:
      LOG_ER("Impossible node state, will terminate");
      /* cant goto objectSyncExit from here */
      return SA_AIS_ERR_FAILED_OPERATION;
  }

  size_t sz = strnlen((char*)req->className.buf, (size_t)req->className.size);
  std::string className((const char*)req->className.buf, sz);

  sz = strnlen((char*)req->objectName.buf, (size_t)req->objectName.size);
  std::string objectName((const char*)req->objectName.buf, sz);

  TRACE_5("Syncing objectName:%s", objectName.c_str());

  ClassInfo* classInfo = 0;

  ClassMap::iterator i3 = sClassMap.find(className);
  AttrMap::iterator i4;
  ObjectMap::iterator i5;

  if (!nameCheck(objectName)) {
    if (nameToInternal(objectName)) {
      nameCorrected = true;
    } else {
      LOG_NO("ERR_INVALID_PARAM: Not a proper object name:%s size:%u",
             objectName.c_str(), (unsigned int)objectName.size());
      err = SA_AIS_ERR_INVALID_PARAM;
      goto objectSyncExit;
    }
  }

  if (i3 == sClassMap.end()) {
    TRACE_7("ERR_NOT_EXIST: class '%s' does not exist", className.c_str());
    err = SA_AIS_ERR_NOT_EXIST;
    goto objectSyncExit;
  }

  classInfo = i3->second;

  if (objectName.empty()) {
    LOG_NO("ERR_INVALID_PARAM: Empty DN is not allowed");
    err = SA_AIS_ERR_INVALID_PARAM;
    goto objectSyncExit;
  }

  if ((i5 = sObjectMap.find(objectName)) != sObjectMap.end()) {
    if (i5->second->mObjFlags & IMM_CREATE_LOCK) {
      LOG_ER(
          "ERR_EXIST: synced object '%s' is already registered for "
          "creation in a ccb, but not applied yet",
          objectName.c_str());
    } else {
      LOG_ER("ERR_EXIST: synced object '%s' exists", objectName.c_str());
    }
    err = SA_AIS_ERR_EXIST;
    goto objectSyncExit;
  }

  if (err == SA_AIS_OK) {
    TRACE_5("sync inserting object '%s'", objectName.c_str());
    MissingParentsMap::iterator mpm;
    ObjectInfo* object = new ObjectInfo();
    object->mClassInfo = classInfo;
    if (nameCorrected) {
      object->mObjFlags = IMM_DN_INTERNAL_REP;
    }

    std::string parentName;
    getParentDn(/*out*/ parentName, /* in */ objectName);
    if (!parentName.empty()) { /* There should exist a parent. */
      i5 = sObjectMap.find(parentName);
      if (i5 == sObjectMap.end()) { /* Parent apparently not synced yet. */
        mpm = sMissingParents.find(parentName);
        if (mpm == sMissingParents.end()) {
          sMissingParents[parentName] = ObjectSet();
          mpm = sMissingParents.find(parentName);
        }
        osafassert(mpm != sMissingParents.end());
        mpm->second.insert(object);

      } else {
        object->mParent = i5->second;
        ObjectInfo* grandParent = i5->second;
        do {
          grandParent->mChildCount++;
          grandParent = grandParent->mParent;
        } while (grandParent);
      }
    }

    // Add attributes to object
    for (i4 = classInfo->mAttrMap.begin(); i4 != classInfo->mAttrMap.end();
         ++i4) {
      AttrInfo* attr = i4->second;

      ImmAttrValue* attrValue = NULL;
      if (attr->mFlags & SA_IMM_ATTR_MULTI_VALUE) {
        attrValue = new ImmAttrMultiValue();
      } else {
        attrValue = new ImmAttrValue();
      }

      // Set admin owner as a regular attribute and then also a pointer
      // to the attrValue for efficient access.
      if (i4->first == std::string(SA_IMM_ATTR_ADMIN_OWNER_NAME)) {
        // attrValue->setValueC_str(adminOwner->mAdminOwnerName.c_str());
        object->mAdminOwnerAttrVal = attrValue;
      }

      object->mAttrValueMap[i4->first] = attrValue;
    }

    // Set attribute values
    p = req->attrValues;
    while (p) {
      // for (int j = 0; j < req->attrValues.list.count; j++) {
      // ImmAttrValues* p = req->attrValues.list.array[j];

      sz = strnlen((char*)p->n.attrName.buf, (size_t)p->n.attrName.size);
      std::string attrName((const char*)p->n.attrName.buf, sz);

      ImmAttrValueMap::iterator i5 = object->mAttrValueMap.find(attrName);

      if (i5 == object->mAttrValueMap.end()) {
        TRACE_7("ERR_NOT_EXIST: attr '%s' not defined", attrName.c_str());
        err = SA_AIS_ERR_NOT_EXIST;
        break;  // out of for-loop
      }

      i4 = classInfo->mAttrMap.find(attrName);
      osafassert(i4 != classInfo->mAttrMap.end());
      AttrInfo* attr = i4->second;

      if (attr->mValueType != (unsigned int)p->n.attrValueType) {
        LOG_NO("ERR_INVALID_PARAM: attr '%s' type missmatch", attrName.c_str());
        err = SA_AIS_ERR_INVALID_PARAM;
        break;  // out of for-loop
      }

      osafassert(p->n.attrValuesNumber > 0);
      ImmAttrValue* attrValue = i5->second;
      IMMSV_OCTET_STRING tmpos;  // temporary octet string
      eduAtValToOs(&tmpos, &(p->n.attrValue),
                   (SaImmValueTypeT)p->n.attrValueType);
      attrValue->setValue(tmpos);

      if (p->n.attrValuesNumber > 1) {
        /*
          int extraValues = p->n.attrValuesNumber - 1;
        */
        if (!(attr->mFlags & SA_IMM_ATTR_MULTI_VALUE)) {
          LOG_NO(
              "ERR_INVALID_PARAM: attr '%s' is not multivalued yet "
              "multiple values provided in create call",
              attrName.c_str());
          err = SA_AIS_ERR_INVALID_PARAM;
          break;  // out of for loop
        }
        /*
          else if(extraValues != p->attrMoreValues.list.count) {
          LOG_ER( "Multivalued attr '%s' should have "
          "%u extra values but %u extra values tranmitted",
          attrName.c_str(), extraValues,
          p->attrMoreValues.list.count);
          //Should not happen, error in packing.
          err = SA_AIS_ERR_LIBRARY;
          break; //out of for-loop
          } */
        else {
          osafassert(attrValue->isMultiValued());
          ImmAttrMultiValue* multiattr = (ImmAttrMultiValue*)attrValue;

          IMMSV_EDU_ATTR_VAL_LIST* al = p->n.attrMoreValues;
          while (al) {
            eduAtValToOs(&tmpos, &(al->n), (SaImmValueTypeT)p->n.attrValueType);
            multiattr->setExtraValue(tmpos);
            al = al->next;
          }
          // TODO: Verify that attrValuesNumber was correct.
        }
      }  // if(p->attrValuesNumber > 1)
      p = p->next;
    }  // while(p)

    // Check that all attributes with INITIALIZED flag have been set.
    // Check that all attributes with STRONG_DEFAULT flag have been set.
    ImmAttrValueMap::iterator i6;
    for (i6 = object->mAttrValueMap.begin();
         i6 != object->mAttrValueMap.end() && err == SA_AIS_OK; ++i6) {
      ImmAttrValue* attrValue = i6->second;
      std::string attrName(i6->first);

      i4 = classInfo->mAttrMap.find(attrName);
      osafassert(i4 != classInfo->mAttrMap.end());
      AttrInfo* attr = i4->second;

      if ((attr->mFlags & SA_IMM_ATTR_INITIALIZED) && attrValue->empty()) {
        LOG_NO(
            "ERR_INVALID_PARAM: attr '%s' must be initialized "
            "yet no value provided in the object create call",
            attrName.c_str());
        err = SA_AIS_ERR_INVALID_PARAM;
      }

      if ((attr->mFlags & SA_IMM_ATTR_STRONG_DEFAULT) && attrValue->empty()) {
        LOG_WA(
            "ERR_INVALID_PARAM: attr '%s' has STRONG_DEFAULT flag "
            "but has no value",
            attrName.c_str());
        err = SA_AIS_ERR_INVALID_PARAM;
      }
    }

    if (err == SA_AIS_OK) {
      sObjectMap[objectName] = object;
      classInfo->mExtent.insert(object);
      mpm = sMissingParents.find(objectName);

      TRACE_7("Object '%s' was synced ", objectName.c_str());

      if (mpm != sMissingParents.end()) {
        TRACE_5("Missing parent %s found during sync", objectName.c_str());
        ObjectSet::iterator oi = mpm->second.begin();
        while (oi != mpm->second.end()) {
          /* Correct the pointer from child to parent */
          (*oi)->mParent = object;
          ObjectInfo* grandParent = object;
          do {
            grandParent->mChildCount += ((*oi)->mChildCount + 1);
            grandParent = grandParent->mParent;
          } while (grandParent);
          // TRACE("Parent %s corrected for child %p", objectName.c_str(), *oi);
          ++oi;
        }
        mpm->second.clear();
        sMissingParents.erase(objectName);
      }
    } else {
      // err != SA_AIS_OK
      // Delete object and its attributes

      ImmAttrValueMap::iterator oavi;
      for (oavi = object->mAttrValueMap.begin();
           oavi != object->mAttrValueMap.end(); ++oavi) {
        delete oavi->second;
      }

      // Empty the collection, probably not necessary (as the ObjectInfo
      // record is deleted below), but does not hurt.
      object->mAttrValueMap.clear();
      delete object;
    }
  }

objectSyncExit:
  TRACE_LEAVE();
  return err;
}

/*
   NOTE: getObjectName returns the DN in EXTERNAL form.
   If the objectName is to be used for internal lookup, then
   the it must be converted to internal DN form (nameToInternal).
*/
void ImmModel::getObjectName(ObjectInfo* info, std::string& objName) {
  AttrMap::iterator i4 = std::find_if(info->mClassInfo->mAttrMap.begin(),
                                      info->mClassInfo->mAttrMap.end(),
                                      AttrFlagIncludes(SA_IMM_ATTR_RDN));
  osafassert(i4 != info->mClassInfo->mAttrMap.end());

  std::string attrName = i4->first;

  ImmAttrValueMap::iterator oavi = info->mAttrValueMap.find(attrName);
  osafassert(oavi != info->mAttrValueMap.end());

  ImmAttrValue* attr = oavi->second;

  objName.append(attr->getValueC_str());

  if (info->mParent) {
    objName.append(",");
    getObjectName(info->mParent, objName);
  }
}

void ImmModel::getParentDn(std::string& parentName,
                           const std::string& objectName) {
  TRACE_ENTER();
  size_t pos = objectName.find(',');
  if (pos == std::string::npos) {
    if (!parentName.empty()) {
      parentName = std::string();
    }
  } else {
    parentName = std::string(objectName, pos + 1);
  }
  TRACE_LEAVE();
}

void ImmModel::setScAbsenceAllowed(SaUint32T scAbsenceAllowed) {
  ObjectMap::iterator oi = sObjectMap.find(immObjectDn);
  osafassert(oi != sObjectMap.end());
  ObjectInfo* immObject = oi->second;
  ImmAttrValueMap::iterator avi =
      immObject->mAttrValueMap.find(immScAbsenceAllowed);
  if (avi == immObject->mAttrValueMap.end()) {
    LOG_WA("Attribue '%s' does not exist in object '%s'",
           immScAbsenceAllowed.c_str(), immObjectDn.c_str());
    return;
  }

  osafassert(!(avi->second->isMultiValued()));
  ImmAttrValue* valuep = (ImmAttrValue*)avi->second;
  valuep->setValue_int(scAbsenceAllowed);

  LOG_NO("ImmModel received scAbsenceAllowed %u", scAbsenceAllowed);
}

SaAisErrorT ImmModel::finalizeSync(ImmsvOmFinalizeSync* req, bool isCoord,
                                   bool isSyncClient, SaUint32T* latestCcbId) {
  TRACE_ENTER();
  SaAisErrorT err = SA_AIS_OK;
  osafassert(!(isCoord && isSyncClient));
  bool prt45allowed = this->protocol45Allowed();

  switch (sImmNodeState) {
    case IMM_NODE_W_AVAILABLE:
      osafassert(isSyncClient);
      break;
    case IMM_NODE_R_AVAILABLE:
      osafassert(!isSyncClient);
      break;
    case IMM_NODE_UNKNOWN:
    case IMM_NODE_ISOLATED:
    case IMM_NODE_LOADING:
    case IMM_NODE_FULLY_AVAILABLE:
      LOG_ER(
          "Node is in a state %u that cannot accept "
          "finalize of sync, will terminate %u %u",
          sImmNodeState, isCoord, isSyncClient);
      if (isCoord) {
        return SA_AIS_ERR_BAD_OPERATION;
      }
      abort();
    default:
      LOG_ER("Impossible node state, will terminate");
      return SA_AIS_ERR_FAILED_OPERATION;
  }

  if (isCoord) {  // Produce the checkpoint
    CcbVector::iterator ccbItr;

    sImmNodeState = IMM_NODE_FULLY_AVAILABLE;
    LOG_NO("NODE STATE-> IMM_NODE_FULLY_AVAILABLE %u", __LINE__);
    /*WARNING the controller node here goes to writable state
      based on a NON FEVS message, directly from the sync-process.
      Other nodes go to writable based on the reception of the
      finalizeSync message over FEVS. This means that the controller
      actually opens up for writes before having received the
      finalize sync message.

      This should still be safe based on the following reasoning:
      Any writable operation now allowed at this node will be
      sent over fevs AFTER the finalizeSync message that this node
      just sent. This at least is the case for messages arriving at
      this node. So we know that ALL nodes, including this coord node,
      will receive any mutating messages AFTER ALL nodes, including this
      coord node, have received the finalizeSync message. This at least
      for messages arriving from clients at this node.

      At the other nodes. Any mutating message does a local check on
      writability. If currently not writable, the request is rejected locally.
      Shift to writability is only by finalize sync or abortSync.
    */

    req->lastContinuationId = sLastContinuationId;
    req->adminOwners = NULL;
    AdminOwnerVector::iterator i;

    for (i = sOwnerVector.begin(); i != sOwnerVector.end();) {
      if ((*i)->mDying && !((*i)->mReleaseOnFinalize)) {
        LOG_WA(
            "Removing admin owner %u %s (ROF==FALSE) which is in demise, "
            "BEFORE generating finalize sync message",
            (*i)->mId, (*i)->mAdminOwnerName.c_str());
        osafassert(adminOwnerDelete((*i)->mId, true) == SA_AIS_OK);
        // Above does a lookup of admin owner again.
        i = sOwnerVector.begin();  // Restart of iteration.
      } else {
        ++i;
      }
    }

    for (i = sOwnerVector.begin(); i != sOwnerVector.end(); ++i) {
      ImmsvAdmoList* ai = (ImmsvAdmoList*)calloc(1, sizeof(ImmsvAdmoList));
      ai->id = (*i)->mId;
      ai->releaseOnFinalize = (*i)->mReleaseOnFinalize;

      if (ai->releaseOnFinalize) {
        ai->isDying = (*i)->mDying;
      } else {
        osafassert(!(*i)->mDying);
      }

      ai->nodeId = (*i)->mNodeId;
      ai->adminOwnerName.size = (SaUint32T)(*i)->mAdminOwnerName.size();
      ai->adminOwnerName.buf = strndup((char*)(*i)->mAdminOwnerName.c_str(),
                                       ai->adminOwnerName.size);

      ai->touchedObjects = NULL;

      ObjectSet::iterator oi;
      for (oi = (*i)->mTouchedObjects.begin();
           oi != (*i)->mTouchedObjects.end(); ++oi) {
        ImmsvObjNameList* nl =
            (ImmsvObjNameList*)calloc(1, sizeof(ImmsvObjNameList));
        std::string objName;
        getObjectName(*oi, objName); /* External name form */
        if ((*oi)->mObjFlags & IMM_DN_INTERNAL_REP) {
          /* DN is internal rep => regenerated name should be the same.
             Converting to internal rep here instead of at sync-client,
             saves execution cost.
          */
          osafassert(nameToInternal(objName));
        }

        nl->name.size = (SaUint32T)objName.size();
        nl->name.buf = strndup((char*)objName.c_str(), nl->name.size);
        nl->next = ai->touchedObjects;
        ai->touchedObjects = nl;
      }
      ai->next = req->adminOwners;
      req->adminOwners = ai;
    }

    LOG_IN("finalizeSync message contains %u admin-owners",
           (unsigned int)sOwnerVector.size());

    for (i = sOwnerVector.begin(); i != sOwnerVector.end();) {
      if ((*i)->mDying) {
        osafassert((*i)->mReleaseOnFinalize);
        LOG_WA(
            "Removing admin owner %u %s (ROF==TRUE) which is in demise, "
            "AFTER generating finalize sync message",
            (*i)->mId, (*i)->mAdminOwnerName.c_str());
        osafassert(adminOwnerDelete((*i)->mId, true) == SA_AIS_OK);
        // Above does a lookup of admin owner again.
        i = sOwnerVector.begin();  // Restart of iteration.
      } else {
        ++i;
      }
    }

    /* Done with generate Admo */

    req->implementers = NULL;
    ImplementerVector::iterator i2;
    for (i2 = sImplementerVector.begin(); i2 != sImplementerVector.end();
         ++i2) {
      ImmsvImplList* ii = (ImmsvImplList*)calloc(1, sizeof(ImmsvImplList));
      if ((*i2)->mDying) {
        ii->id = 0;
        ii->nodeId = 0;
        ii->mds_dest = 0LL;

        /* Note that here the finalize sync message is bypassing
           any discardImplementer over fevs. Old clients receiving
           this message will need to discard implementer on the
           basis of the incoming 'id' being zero.
         */
      } else {
        ii->id = (*i2)->mId; /* id is zero if dead. */
        ii->nodeId = (*i2)->mNodeId;
        ii->mds_dest = (*i2)->mMds_dest;
      }
      ii->implementerName.size = (SaUint32T)(*i2)->mImplementerName.size();
      ii->implementerName.buf = strndup((char*)(*i2)->mImplementerName.c_str(),
                                        ii->implementerName.size);

      ii->next = req->implementers;
      req->implementers = ii;
    }

    LOG_IN("finalizeSync message contains %u implementers",
           (unsigned int)sImplementerVector.size());

    req->classes = NULL;
    ClassMap::iterator i3;
    for (i3 = sClassMap.begin(); i3 != sClassMap.end(); ++i3) {
      ClassInfo* ci = i3->second;
      ImmsvClassList* ioci = (ImmsvClassList*)calloc(1, sizeof(ImmsvClassList));
      ioci->className.size = (SaUint32T)i3->first.size();
      ioci->className.buf =
          strndup((char*)i3->first.c_str(), ioci->className.size);
      if (ci->mImplementer) {
        ioci->classImplName.size =
            (SaUint32T)ci->mImplementer->mImplementerName.size();
        ioci->classImplName.buf =
            strndup((char*)ci->mImplementer->mImplementerName.c_str(),
                    ioci->classImplName.size);
      }
      ioci->nrofInstances = (SaUint32T)ci->mExtent.size();
      ioci->next = req->classes;
      req->classes = ioci;
    }

    LOG_IN("finalizeSync message contains %u class info records",
           (unsigned int)sClassMap.size());

    for (ccbItr = sCcbVector.begin(); ccbItr != sCcbVector.end(); ++ccbItr) {
      if ((*ccbItr)->isActive()) {
        LOG_ER(
            "FinalizeSync: Found active transaction %u, "
            "sync should not have started!",
            (*ccbItr)->mId);
        err = SA_AIS_ERR_BAD_OPERATION;
        goto done;
      }

      /*
        TRACE("Encode CCB-ID:%u outcome:(%u)%s", (*ccbItr)->mId,
         (*ccbItr)->mState,
         ((*ccbItr)->mState == IMM_CCB_COMMITTED)?"COMMITTED":
         (((*ccbItr)->mState == IMM_CCB_ABORTED)?"ABORTED":"OTHER"));
      */

      ImmsvCcbOutcomeList* ol =
          (ImmsvCcbOutcomeList*)calloc(1, sizeof(ImmsvCcbOutcomeList));
      ol->ccbId = (*ccbItr)->mId;
      /* OpenSAF 4.5 adds two new ccb states before IMM_CCB_PREPARE. Only
         terminated Ccbs can currently be synced (COMMITTED or ABORTED). These
         two states are shifted up by 2 in OpenSAF 4.5
      */
      ol->ccbState = prt45allowed ? (*ccbItr)->mState : ((*ccbItr)->mState - 2);
      ol->next = req->ccbResults;
      req->ccbResults = ol;
    }

    /* Latest ccb-id is sent to sync-client as a ccb with ccb-id being zero.
     * The veteran will simply ignore this ccb because ccb-id never be zero. */
    ImmsvCcbOutcomeList* ol =
        (ImmsvCcbOutcomeList*)calloc(1, sizeof(ImmsvCcbOutcomeList));
    ol->ccbId = 0;
    ol->ccbState = *latestCcbId; /* This is a hack */
    ol->next = req->ccbResults;
    req->ccbResults = ol;

  } else {
    // SyncFinalize received by all cluster members, old and new-joining.
    // Joiners will copy the finalize state.
    // old members (except coordinator) will verify equivalent state.
    if (isSyncClient) {
      AdminOwnerVector::iterator i;

      if (!sDeferredObjUpdatesMap.empty()) {
        LOG_ER("syncFinalize client found deferred RTA updates - aborting");
        err = SA_AIS_ERR_FAILED_OPERATION;
        goto done;
      }

      if (!sMissingParents.empty()) {
        MissingParentsMap::iterator mpm;
        LOG_ER("Can not finalize sync because there are %u missing parents",
               (unsigned int)sMissingParents.size());
        for (mpm = sMissingParents.begin(); mpm != sMissingParents.end();
             ++mpm) {
          LOG_ER("Missing Parent DN: %s", mpm->first.c_str());
        }
        err = SA_AIS_ERR_FAILED_OPERATION;
        goto done;
      }

      // syncronize with the checkpoint
      sLastContinuationId = req->lastContinuationId;

      // Sync currently existing AdminOwners.
      ImmsvAdmoList* ai = req->adminOwners;
      while (ai) {
        AdminOwnerInfo* info = new AdminOwnerInfo;
        info->mId = ai->id;
        info->mNodeId = ai->nodeId;
        info->mConn = 0;
        info->mAdminOwnerName.append((const char*)ai->adminOwnerName.buf,
                                     (size_t)ai->adminOwnerName.size);
        info->mReleaseOnFinalize = ai->releaseOnFinalize;
        info->mDying = ai->isDying;
        if ((info->mDying) && (!(info->mReleaseOnFinalize))) {
          LOG_ER(
              "finalizeSync client: Admo is dying yet releaseOnFinalize is false");
          err = SA_AIS_ERR_FAILED_OPERATION;
          delete info;
          goto done;
        }

        ImmsvObjNameList* nl = ai->touchedObjects;
        TRACE_5("Synced admin owner %s", info->mAdminOwnerName.c_str());
        while (nl) {
          std::string objectName((const char*)nl->name.buf,
                                 (size_t)strnlen((const char*)nl->name.buf,
                                                 (size_t)nl->name.size));

          ObjectMap::iterator oi = sObjectMap.find(objectName);
          if (oi == sObjectMap.end()) {
            LOG_ER(
                "Sync client failed to locate object: "
                "%s, will restart.",
                objectName.c_str());
            err = SA_AIS_ERR_FAILED_OPERATION;
            delete info;
            goto done;
          }
          info->mTouchedObjects.insert(oi->second);
          nl = nl->next;
        }
        sOwnerVector.push_back(info);
        ai = ai->next;
      }

      LOG_IN("finalizeSync message contains %u admin-owners",
             (unsigned int)sOwnerVector.size());

      for (i = sOwnerVector.begin(); i != sOwnerVector.end();) {
        IdVector::iterator ivi;
        ivi = find(sAdmosDeadDuringSync.begin(), sAdmosDeadDuringSync.end(),
                   (*i)->mId);
        if ((*i)->mDying || ivi != sAdmosDeadDuringSync.end()) {
          if ((*i)->mDying && !((*i)->mReleaseOnFinalize)) {
            LOG_ER(
                "finalizeSync client: Admo %u is dying yet releaseOnFinalize is false",
                (*i)->mId);
            err = SA_AIS_ERR_FAILED_OPERATION;
            goto done;
          }

          LOG_WA(
              "Removing admin owner %u %s which is in demise, "
              "AFTER receiving finalize sync message",
              (*i)->mId, (*i)->mAdminOwnerName.c_str());
          // This does a lookup of admin owner again.
          if (adminOwnerDelete((*i)->mId, true) != SA_AIS_OK) {
            LOG_ER("finalizeSync client: Failed to remobe admin-owner %u",
                   (*i)->mId);
            err = SA_AIS_ERR_FAILED_OPERATION;
            goto done;
          }
          i = sOwnerVector.begin();  // Restart of iteration.
        } else {
          ++i;
        }
      }
      /* Done with syncing Admo */

      // Sync currently existing implementers
      ImmsvImplList* ii = req->implementers;
      // Calculate applier timeout before the loop
      struct timespec ts;
      SaUint64T applierDisconnected;
      osaf_clock_gettime(CLOCK_MONOTONIC, &ts);
      applierDisconnected = osaf_timespec_to_millis(&ts);
      for (; ii; ii = ii->next) {
        std::string implName;
        size_t sz = strnlen((const char*)ii->implementerName.buf,
                            (size_t)ii->implementerName.size);
        implName.append((const char*)ii->implementerName.buf, sz);
        ImplementerInfo* info = findImplementer(implName);
        if (info) {
          /* Named implemener already exists. This can happen since we
             now (after #1871) allow saImmOiImplementeSet during sync.
             Discard the synced implementer if the two are identical.

             If not identical, then we must abandon this sync because we
             can not distinguish between the following two cases:

             Case1: A correct and successfull implementerSet arrived at the
             sync-client after finalizeSync was sent from coord but before
             finalizeSync arrived at sync-client (over fevs). The impl-set
             will then be a correct implementer set also here at the
             sync-client, *differing* from the implementer record in
             finalizeSync. In this case the finalizeSync record is obsolete.

             Case2: An implementerSet that passes the existence check
             (see immModel_implFree) locally at a veteran node, but fails
             on ERR_EXIST when arriving over fevs at all veteran nodes, because
             by then, some other user had already allocated the implementer over
             fevs. In this case the implementer-set will be rejected at veteran
             nodes, but possibly accepted (erroneously) at the sync-client, if
             the implementer-name did not yet exist.
             In this case the finalizeSync record is correct and the sync-client
             implementer should be discarded.

             Because we can not distinguish between these cases, we exit the
             sync-client immnd, forcing it to redo the sync.
          */
          if ((info->mId == ii->id) && (info->mNodeId == ii->nodeId) &&
              (info->mMds_dest == ii->mds_dest)) {
            LOG_NO(
                "finalizeSync implementerSet '%s' id: %u has bypassed "
                "finalizeSync, ignoring",
                info->mImplementerName.c_str(), info->mId);
            continue;
          } else if (ii->id == 0 && ii->nodeId == 0 && ii->mds_dest == 0) {
            /* Implementer set is requested just before finalizeSync, and it
             * arrives on nodes after finalizeSync sends implementer info with
             * all 0. So, ignore the implementer record in finalizeSync as it
             * is obsolete.
             */
            LOG_WA("implementerSet '%s' id: %u has arrived "
                   "after fynalizeSync broadcast and "
                   "before finalizeSync arrived to the sync-client, ignoring",
                   info->mImplementerName.c_str(), info->mId);
            continue;
          } else if (info->mId == 0 && info->mNodeId == 0 &&
                     info->mMds_dest == 0) {
            /* Implementer clear is requested just before finalizeSync, and it
             * arrives on nodes after finalizeSync sends implementer info with
             * old set. So, ignore the implementer record in finalizeSync as it
             * is obsolete.
             */
            LOG_WA("implementerClear '%s' id: %u has arrived "
                   "after fynalizeSync broadcast and "
                   "before finalizeSync arrived to the sync-client, ignoring",
                   info->mImplementerName.c_str(), ii->id);
            continue;
          } else {
            LOG_WA(
                "Mismatched implementer '%s' detected at sync client by"
                "finalizeSync, info: id(%u, %u), nodeid(0x%x, 0x%x),"
                "mdsdest(%" PRIx64", %" PRIx64 ") - exiting",
                info->mImplementerName.c_str(),
                info->mId, ii->id,
                info->mNodeId, ii->nodeId,
                (uint64_t)info->mMds_dest, (uint64_t)ii->mds_dest);
            err = SA_AIS_ERR_BAD_OPERATION;
            goto done;
          }
        }
        info = new ImplementerInfo;

        IdVector::iterator ivi = sImplsDeadDuringSync.begin();
        for (; ivi != sImplsDeadDuringSync.end(); ++ivi) {
          if ((*ivi) == ii->id) {
            LOG_NO("Avoiding re-create of implementer %u in finalizeSync",
                   ii->id);
            /* The coord generated finalizeSync just before a discardImplementer
               arrived. Zero parts of incoming data, making it appear as if
               discardImplementer arrived before generation of finalizeSync.
            */
            ii->id = 0;
            ii->nodeId = 0;
            ii->mds_dest = 0LL;
            break; /* out of inner for-loop */
          }
        }

        info->mId = ii->id;
        info->mNodeId = ii->nodeId;
        info->mConn = 0;
        info->mMds_dest = ii->mds_dest;
        info->mDying = false;  // only relevant on the implementers node.
        info->mImplementerName = implName;
        info->mAdminOpBusy = 0;
        info->mApplier = (info->mImplementerName.at(0) == '@');
        if(!info->mNodeId && info->mApplier) {
          /* The info when an applier is disconnected is not synced.
           * It will be calculated from the time it's synced */
          info->mApplierDisconnected = applierDisconnected;
        }
        sImplementerVector.push_back(info);
        TRACE_5(
            "Immnd sync client synced implementer id:%u name:>>%s<< "
            "size:%zu",
            info->mId, implName.c_str(), implName.size());
      }

      LOG_IN("finalizeSync message contains %u implementers",
             (unsigned int)sImplementerVector.size());

      // Attach object implementers using the implementer-name attribute.
      ObjectMap::iterator oi;
      std::string implAttr(SA_IMM_ATTR_IMPLEMENTER_NAME);
      for (oi = sObjectMap.begin(); oi != sObjectMap.end(); ++oi) {
        ObjectInfo* obj = oi->second;
        // ImmAttrValueMap::iterator j;
        ImmAttrValue* att = obj->mAttrValueMap[implAttr];
        if (!att) {
          LOG_ER("Attribute %s is MISSING from obj:%s", implAttr.c_str(),
                 oi->first.c_str());
          err = SA_AIS_ERR_FAILED_OPERATION;
          goto done;
        }
        if (!(att->empty())) {
          const std::string implName(att->getValueC_str());
          /*
             TRACE_5("Attaching implementer %s to object %s",
             implName.c_str(), oi->first.c_str());
           */
          ImplementerInfo* impl = findImplementer(implName);
          if (impl) {
            // Implementer name may be assigned but none attached
            obj->mImplementer = impl;
          } else {
            LOG_ER("Implementer %s for object %s is MISSING", implName.c_str(),
                   oi->first.c_str());
            err = SA_AIS_ERR_FAILED_OPERATION;
            goto done;
          }
        }
      }

      unsigned int classCount = 0;
      ImmsvClassList* ioci = req->classes;
      while (ioci) {
        size_t sz =
            strnlen((char*)ioci->className.buf, (size_t)ioci->className.size);
        std::string className((char*)ioci->className.buf, sz);

        ClassMap::iterator i3 = sClassMap.find(className);
        if (i3 == sClassMap.end()) {
          LOG_ER("Class %s is MISSING", className.c_str());
          err = SA_AIS_ERR_FAILED_OPERATION;
          goto done;
        }
        ClassInfo* ci = i3->second;
        // Is the osafassert below really true if the class is a runtime
        // class ?
        // Yes, because the existence of a rt object is noted at
        // all nodes.
        if ((int)ci->mExtent.size() != (int)ioci->nrofInstances) {
          LOG_ER("Synced class %s has %u instances should have %u",
                 className.c_str(), (unsigned int)ci->mExtent.size(),
                 ioci->nrofInstances);
          err = SA_AIS_ERR_FAILED_OPERATION;
          goto done;
        }
        ++classCount;
        if (ioci->classImplName.size) {
          sz = strnlen((char*)ioci->classImplName.buf,
                       (size_t)ioci->classImplName.size);
          const std::string clImplName((char*)ioci->classImplName.buf, sz);

          ImplementerInfo* impl = findImplementer(clImplName);
          if (!impl) {
            LOG_ER("Class-implementer %s is MISSING", clImplName.c_str());
            err = SA_AIS_ERR_FAILED_OPERATION;
            goto done;
          }
          ci->mImplementer = impl;
        }
        ioci = ioci->next;
      }
      LOG_IN("Synced %u classes", classCount);
      if (sClassMap.size() != classCount) {
        LOG_ER("sClassMap.size %zu != classCount %u", sClassMap.size(),
               classCount);
        err = SA_AIS_ERR_FAILED_OPERATION;
        goto done;
      }

      ImmsvCcbOutcomeList* ol = req->ccbResults;
      while (ol) {
        if (!ol->ccbId) {
          *latestCcbId = ol->ccbState; /* This is a hack */
          ol = ol->next;
          continue;
        }

        CcbInfo* newCcb = new CcbInfo;
        newCcb->mId = ol->ccbId;
        newCcb->mAdminOwnerId = 0;
        newCcb->mCcbFlags = 0;
        newCcb->mOriginatingNode = 0;
        newCcb->mOriginatingConn = 0;
        newCcb->mVeto = SA_AIS_OK;
        newCcb->mState =
            (ImmCcbState)(prt45allowed ? ol->ccbState : (ol->ccbState + 2));
        osaf_clock_gettime(CLOCK_MONOTONIC, &newCcb->mWaitStartTime);
        newCcb->mOpCount = 0;
        newCcb->mPbeRestartId = 0;
        newCcb->mErrorStrings = NULL;
        newCcb->mAugCcbParent = NULL;

        TRACE_5(
            "CCB %u state %s", newCcb->mId,
            (newCcb->mState == IMM_CCB_COMMITTED)
                ? "COMMITTED"
                : ((newCcb->mState == IMM_CCB_ABORTED) ? "ABORTED" : "OTHER"));
        if ((newCcb->isActive())) {
          LOG_ER("Can not sync Ccb that is active");
          err = SA_AIS_ERR_FAILED_OPERATION;
          delete newCcb;
          goto done;
        }
        sCcbVector.push_back(newCcb);
        ol = ol->next;
      }

      // Populate the sReverseRefsNoDanglingMMap
      ClassInfo* ci;
      ObjectSet os;
      ImmAttrValue* av;
      ObjectSet::iterator osi;
      ObjectSet::iterator aosi;
      ImmAttrValueMap::iterator avmi;
      AttrMap::iterator ami;
      ClassMap::iterator cmi;
      ObjectMap::iterator omi;
      for (cmi = sClassMap.begin(); cmi != sClassMap.end(); ++cmi) {
        ci = cmi->second;
        // Check if the class has at least one attribute with NO_DANGLING flag;
        for (ami = ci->mAttrMap.begin();
             ami != ci->mAttrMap.end() &&
             !(ami->second->mFlags & SA_IMM_ATTR_NO_DANGLING);
             ++ami)
          ;
        if (ami == ci->mAttrMap.end()) {
          continue;
        }

        // Iterate through all objects of the class
        for (osi = ci->mExtent.begin(); osi != ci->mExtent.end(); ++osi) {
          os.clear();
          for (ami = ci->mAttrMap.begin(); ami != ci->mAttrMap.end(); ++ami) {
            // Collect NO_DANGLING references
            if (ami->second->mFlags & SA_IMM_ATTR_NO_DANGLING) {
              avmi = (*osi)->mAttrValueMap.find(ami->first.c_str());
              // Attribute must exist
              osafassert(avmi != (*osi)->mAttrValueMap.end());
              av = avmi->second;
              if (!av->empty()) {
                omi = sObjectMap.find(av->getValueC_str());
                if (omi == sObjectMap.end()) {
                  std::string objName;
                  getObjectName(*osi, objName);
                  LOG_ER("Object '%s' is missing. Reference from '%s'",
                         av->getValueC_str(), objName.c_str());
                  abort();
                }
                os.insert(omi->second);
              }
              if (av->isMultiValued()) {
                while ((av = ((ImmAttrMultiValue*)av)->getNextAttrValue())) {
                  if (!av->empty()) {
                    omi = sObjectMap.find(av->getValueC_str());
                    if (omi == sObjectMap.end()) {
                      std::string objName;
                      getObjectName(*osi, objName);
                      LOG_ER("Object '%s' is missing. Reference from '%s'",
                             av->getValueC_str(), objName.c_str());
                      abort();
                    }
                    os.insert(omi->second);
                  }
                }
              }
            }
          }

          // Add NO_DANGLING references to sReverseRefsNoDanglingMMap
          for (aosi = os.begin(); aosi != os.end(); ++aosi) {
            sReverseRefsNoDanglingMMap.insert(
                std::pair<ObjectInfo*, ObjectInfo*>(*aosi, *osi));
          }
        }
      }

      TRACE_5("Synced %u CCB-outcomes", (unsigned int)sCcbVector.size());

      this->setLoader(0);
      // Opens for OM and OI connections to this node.
      // immnd must also set cb->mAccepted=true

      if (!sNodesDeadDuringSync.empty()) {
        IdVector::iterator ivi = sNodesDeadDuringSync.begin();
        for (; ivi != sNodesDeadDuringSync.end(); ++ivi) {
          ConnVector cv, gv;
          LOG_NO("Sync client re-executing discardNode for node %x", (*ivi));
          this->discardNode((*ivi), cv, gv, false, false);
          if (!(cv.empty())) {
            LOG_ER("Sync can not discard node with active ccbs");
            err = SA_AIS_ERR_FAILED_OPERATION;
            goto done;
          }
        }
      }
    } else {
      CcbVector::iterator ccbItr;
      // verify the checkpoint at veteran nodes (not coord & not sync-client)

      sImmNodeState = IMM_NODE_FULLY_AVAILABLE;
      LOG_NO("NODE STATE-> IMM_NODE_FULLY_AVAILABLE %u", __LINE__);

      if (sLastContinuationId != (unsigned int)req->lastContinuationId) {
        LOG_ER(
            "Sync-verify: Established node has "
            "different Continuation count (%u) than expected (%u) - aborting",
            sLastContinuationId, req->lastContinuationId);
        /* Be strict. */
        abort();
      }

      // verify currently existing AdminOwners.

      /* We can't remove the dying ROF==FALSE admo before verifying.
       * There may be ROF=FALSE admo marked as dying when coord is
       * generating the sync-finalize message. */
      AdminOwnerVector::iterator i2;
      ImmsvAdmoList* ai = req->adminOwners;
      for (; ai != NULL; ai = ai->next) {
        int nrofTouchedObjs = 0;
        i2 = std::find_if(sOwnerVector.begin(), sOwnerVector.end(),
                          IdIs(ai->id));

        if (i2 == sOwnerVector.end()) {
          LOG_WA(
              "Missing Admin Owner with id:%u in sync-verify "
              "can happen after pre-loading (2PBE)",
              ai->id);
          osafassert(ai->id < 10); /* Initial admoId used in 2PBE preload. */
          continue;
        }

        AdminOwnerInfo* info = *i2;
        std::string ownerName(
            (const char*)ai->adminOwnerName.buf,
            (size_t)strnlen((const char*)ai->adminOwnerName.buf,
                            (size_t)ai->adminOwnerName.size));
        if (info->mAdminOwnerName != ownerName) {
          LOG_ER(
              "Sync-verify: Established node has different "
              "AdminOwner-name %s for id %u, should be %s.",
              info->mAdminOwnerName.c_str(), ai->id, ownerName.c_str());
          abort();
        }

        if (info->mDying != ai->isDying) {
          LOG_WA(
              "Sync-verify: Established node has "
              "different isDying flag (%u) for AdminOwner "
              "%s, should be %u.",
              info->mDying, ownerName.c_str(), ai->isDying);
          /* We don't abort here because mDying can be set on veterans
           * when coord is generating sync-finalize message. */
        }
        if (info->mReleaseOnFinalize != ai->releaseOnFinalize) {
          LOG_ER(
              "Sync-verify: Established node has "
              "different release-on-verify flag (%u) for AdminOwner "
              "%s, should be %u.",
              info->mReleaseOnFinalize, ownerName.c_str(),
              ai->releaseOnFinalize);
          abort();
        }
        if (info->mNodeId != (unsigned int)ai->nodeId) {
          LOG_ER(
              "Sync-verify: Established node has "
              "different nodeId (%x) for AdminOwner "
              "%s, should be %x.",
              info->mNodeId, ownerName.c_str(), ai->nodeId);
          abort();
        }

        ImmsvObjNameList* nl = ai->touchedObjects;
        while (nl) {
          ++nrofTouchedObjs;
          nl = nl->next;
        }

        if (nrofTouchedObjs != (int)info->mTouchedObjects.size()) {
          LOG_ER(
              "Sync-verify: Missmatch on number of "
              "touched objects %u for AdminOwner %s, should be %u.",
              (unsigned int)info->mTouchedObjects.size(), ownerName.c_str(),
              nrofTouchedObjs);
          abort();
        }
      }

      /* Removing all dying admo, both ROF=TRUE and ROF=FALSE */
      for (i2 = sOwnerVector.begin(); i2 != sOwnerVector.end();) {
        if ((*i2)->mDying) {
          LOG_WA(
              "Removing admin owner %u %s which is in demise, "
              "AFTER receiving sync/verify message",
              (*i2)->mId, (*i2)->mAdminOwnerName.c_str());
          osafassert(adminOwnerDelete((*i2)->mId, true) == SA_AIS_OK);
          // lookup of admin owner again.

          // restart of iteration again.
          i2 = sOwnerVector.begin();
        } else {
          ++i2;
        }
      }

      /* Done with verify admo */

      // Verify currently existing implementers
      ImmsvImplList* ii = req->implementers;
      while (ii) {
        const std::string implName(
            (const char*)ii->implementerName.buf,
            (size_t)strnlen((const char*)ii->implementerName.buf,
                            (size_t)ii->implementerName.size));

        ImplementerInfo* info = findImplementer(implName);
        if (info == NULL) {
          ImplementerVector::iterator i;
          LOG_ER(
              "Missing implementer size %u/%zu name:>>%s<< at veteran "
              "node in finalize sync - aborting",
              ii->implementerName.size, strlen(ii->implementerName.buf),
              ii->implementerName.buf);

          for (i = sImplementerVector.begin(); i != sImplementerVector.end();
               ++i) {
            ImplementerInfo* info = (*i);
            LOG_NO("Implname:>>%s<< size:%zu", info->mImplementerName.c_str(),
                   info->mImplementerName.size());
          }
          abort();
        }

        if (info->mId != (unsigned int)ii->id) {
          bool explained = false;
          if (ii->id == 0) {
            LOG_NO(
                "Sync-verify: Veteran node has different "
                "Implementer-id %u for implementer: %s, should be 0 "
                "according to finalizeSync. Assunimg implSet bypased finSync",
                info->mId, implName.c_str());
            explained = true;
          } else if (info->mId == 0) {
            LOG_NO(
                "Sync-verify: Veteran node has different "
                "Implementer-id %u for implementer: %s, it is 0 "
                "according to finalizeSync. Assuming implClear bypased finSync",
                ii->id, implName.c_str());
            explained = true;
          } else {
            /* Here veteran claims either dead implementer, i.e. (info->mId ==
               0), or different implementer, i.e. new and different info->mId,
               but coord claims non-dead implementer. This can happen when
               implementer disconnects just after coord sends finalizeSync,
               or both disconnects and reconnects just after coord sends
               finalizeSync. Checking for this.
            */
            IdVector::iterator ivi = sImplsDeadDuringSync.begin();
            for (; ivi != sImplsDeadDuringSync.end(); ++ivi) {
              if ((*ivi) == ii->id) {
                LOG_NO(
                    "Detected disconnected implementer %u in "
                    "finalizeSync message - ignoring.",
                    ii->id);
                explained = true;
                break;
              }
            }

            if (ivi == sImplsDeadDuringSync.end() &&
                !(sNodesDeadDuringSync.empty()) && ii->nodeId) {
              /* Could be dead node instead. */
              ivi = sNodesDeadDuringSync.begin();
              for (; ivi != sNodesDeadDuringSync.end(); ++ivi) {
                if ((*ivi) == ii->nodeId) {
                  LOG_NO(
                      "Detected implementer from dead node %u in "
                      "finalizeSync message - ignoring",
                      ii->nodeId);
                  explained = true;
                  break;
                }
              }
            }
          }

          if (!explained) {
            LOG_ER(
                "Sync-verify: Established node has different "
                "Implementer-id: %u for name: %s, sync says %u. ",
                info->mId, implName.c_str(), ii->id);
            abort();
          }

        } else if (info->mNodeId != ii->nodeId) {
          LOG_ER(
              "Sync-verify: Missmatch on node-id "
              "%x for implementer %s, sync says %x",
              info->mNodeId, implName.c_str(), ii->nodeId);
          abort();
        }
        ii = ii->next;
      }

      // Verify classes
      int classCount = 0;
      ImmsvClassList* ioci = req->classes;
      while (ioci) {
        size_t sz =
            strnlen((char*)ioci->className.buf, (size_t)ioci->className.size);
        std::string className((char*)ioci->className.buf, sz);
        ClassMap::iterator i3 = sClassMap.find(className);
        osafassert(i3 != sClassMap.end());
        ClassInfo* ci = i3->second;

        if ((int)ci->mExtent.size() != (int)ioci->nrofInstances) {
          LOG_ER(
              " ci->mExtent.size <%d> != ioci->nrofInstances <%d> for class:%s",
              (int)ci->mExtent.size(), (int)ioci->nrofInstances,
              className.c_str());
          abort();
        }

        if (ioci->classImplName.size) {
          sz = strnlen((char*)ioci->classImplName.buf,
                       (size_t)ioci->classImplName.size);
          const std::string clImplName((char*)ioci->classImplName.buf, sz);
          ImplementerInfo* impl = findImplementer(clImplName);
          if (!impl) {
            LOG_ER("Implemener missing for impl-name:%s", clImplName.c_str());
            osafassert(impl);
          }
          if (!(ci->mImplementer)) {
            LOG_ER("Class %s has no current implementer set",
                   className.c_str());
            osafassert(ci->mImplementer);
          }
          if (!(ci->mImplementer == impl)) {
            LOG_ER("class %s has implementer %s set should have been %s",
                   className.c_str(),
                   ci->mImplementer->mImplementerName.c_str(),
                   impl->mImplementerName.c_str());
            osafassert(ci->mImplementer == impl);
          }
        }
        ++classCount;
        ioci = ioci->next;
      }

      // Verify CCB outcomes.
      ImmsvCcbOutcomeList* ol = req->ccbResults;
      unsigned int verified = 0;
      unsigned int gone = 0;
      while (ol) {
        CcbVector::iterator i1 = std::find_if(
            sCcbVector.begin(), sCcbVector.end(), CcbIdIs(ol->ccbId));
        if (i1 == sCcbVector.end()) {
          if (ol->ccbId) ++gone;
        } else {
          CcbInfo* ccb = *i1;
          if (ccb->mState !=
              (ImmCcbState)(prt45allowed ? ol->ccbState : (ol->ccbState + 2))) {
            LOG_ER("ccb->mState:%u  !=  ol->ccbState:%u for CCB:%u",
                   ccb->mState, ol->ccbState, ccb->mId);
            osafassert(ccb->mState == (ImmCcbState)ol->ccbState);
          }
          ++verified;
          /*
              TRACE_5("CCB %u verified with state %s", ccb->mId,
               (ccb->mState == IMM_CCB_COMMITTED)?"COMMITTED":
               ((ccb->mState == IMM_CCB_ABORTED)?"ABORTED":"OTHER"));
          */
        }
        ol = ol->next;
      }
      TRACE_5("Verified %u CCBs, %u gone", verified, gone);
      if (sCcbVector.size() != verified + gone) {
        LOG_NO("sCcbVector.size()/%u != verified/%u + gone/%u",
               (unsigned int)sCcbVector.size(), verified, gone);
      }
      // Old member passed verification.
    }
  }

  sImplsDeadDuringSync.clear(); /* for coord, sync-client & veterans. */
  sNodesDeadDuringSync.clear(); /* should only be relevant for sync-client. */
  sAdmosDeadDuringSync.clear(); /* should only be relevant for sync-client. */

/* De-comment to get a dump of childcounts after each sync
if(true) {
    ObjectMap::iterator omi = sObjectMap.begin();
    while(omi != sObjectMap.end()) {
        TRACE("Object:%s has children:%u", omi->first.c_str(),
omi->second->mChildCount);
        ++omi;
    }
}*/

done:
  TRACE_LEAVE();
  return err;
}

void ImmModel::isolateThisNode(unsigned int thisNode, bool isAtCoord) {
  /* Move this logic up to immModel_isolate... No need for this extra level.
     But need to abort and terminate ccbs.
   */
  ImplementerVector::iterator i;
  AdminOwnerVector::iterator i2;
  unsigned int otherNode;

  if ((sImmNodeState != IMM_NODE_FULLY_AVAILABLE) &&
      (sImmNodeState != IMM_NODE_R_AVAILABLE)) {
    LOG_NO("SC abscence interrupted sync of this IMMND - exiting");
    exit(0);
  }

  i = sImplementerVector.begin();
  while (i != sImplementerVector.end()) {
    IdVector cv, gv;
    ImplementerInfo* info = (*i);
    otherNode = info->mNodeId;
    if (otherNode == 0) {
      /* The implementer was already discarded */
    } else if (otherNode == thisNode) {
      LOG_WA(
          "Local implementer %u <%u, %x> (%s) was not globally discarded, discarding it...",
          info->mId, info->mConn, info->mNodeId,
          info->mImplementerName.c_str());
      ConnVector cv;
      this->discardImplementer(info->mId, true, cv, isAtCoord);
    } else {
      info = NULL;
      this->discardNode(otherNode, cv, gv, isAtCoord, true);
      LOG_NO("Impl Discarded node %x", otherNode);
    }
    /* Discarding implementer doesn't remove the implementer from
     * sImplementerVector, so we don't have to restart the iteration */
    ++i;
  }

  i2 = sOwnerVector.begin();
  while (i2 != sOwnerVector.end()) {
    IdVector cv, gv;
    AdminOwnerInfo* ainfo = (*i2);
    otherNode = ainfo->mNodeId;
    osafassert(otherNode);
    if (otherNode == thisNode) {
      LOG_WA(
          "Local admin owner %u (%s) was not hard finalized, finalizing it...",
          ainfo->mId, ainfo->mAdminOwnerName.c_str());
      this->adminOwnerDelete(ainfo->mId, true, false);
    } else {
      ainfo = NULL;
      this->discardNode(otherNode, cv, gv, isAtCoord, true);
      LOG_NO("Admo Discarded node %x", otherNode);
    }
    i2 = sOwnerVector.begin(); /* restart iteration. */
  }

  /* Verify that all noncritical CCBs are aborted.
     Ccbs where client resided at this node chould already have been handled in
     immnd_proc_discard_other_nodes() that calls
     immnd_proc_imma_discard_connection()
   */
}
