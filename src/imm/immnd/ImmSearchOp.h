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
 * Author(s): Ericsson AB
 *
 */

#ifndef IMM_IMMND_IMMSEARCHOP_H_
#define IMM_IMMND_IMMSEARCHOP_H_ 1

#include <saImmOm.h>
#include "imm/immnd/ImmAttrValue.h"
#include <string>
#include <list>
#include <time.h>
#include "immnd.h"

struct SearchAttribute {
  SearchAttribute(const std::string& attributeName)
      : name(attributeName), valuep(NULL), flags(0) {
    valueType = (SaImmValueTypeT)0;
  }
  std::string name;
  ImmAttrValue* valuep;
  SaImmValueTypeT valueType;
  SaImmAttrFlagsT flags;

  ~SearchAttribute();
};
typedef std::list<SearchAttribute> AttributeList;

struct SearchObject {
  SearchObject(const std::string& objectName)
      : name(objectName), implInfo(NULL) {}
  std::string name;
  AttributeList attributeList;
  void* implInfo; /* ImplementerInfo * */

  ~SearchObject();
};
typedef std::list<SearchObject> ResultList;

class ImmSearchOp {
 public:
  ImmSearchOp();
  ~ImmSearchOp();

  void addObject(const std::string& objectName);
  void addAttribute(const std::string& attributeName, SaUint32T valueType,
                    SaImmAttrFlagsT flags);
  void addAttrValue(const ImmAttrValue& value);
  void setImplementer(void* implInfo);

  SaAisErrorT testTopResult(void** implInfo, bool* bRtsToFetch);

  SaAisErrorT nextResult(IMMSV_OM_RSP_SEARCH_NEXT** rsp, void** implInfo,
                         AttributeList** rtsToFetch);

  IMMSV_OM_RSP_SEARCH_NEXT* fetchLastResult() { return mLastResult; }
  void clearLastResult() { mLastResult = NULL; }
  void setIsSync() { mIsSync = true; }
  void setIsAccessor() { mIsAccessor = true; }
  void setNonExtendedName() { mNonExtendedName = true; }
  bool isSync() { return mIsSync; }
  bool isAccessor() { return mIsAccessor; }
  bool isNonExtendedNameSet() { return mNonExtendedName; }
  timespec getLastSearchTime() { return mLastSearch; }
  void updateSearchTime() { osaf_clock_gettime(CLOCK_MONOTONIC, &mLastSearch); }
  void popLastResult() { mResultList.pop_front(); }
  void* syncOsi;
  void* attrNameList;
  void* classInfo;

 private:
  ResultList mResultList;
  IMMSV_OM_RSP_SEARCH_NEXT* mLastResult;  // only used to save result during
  // fetching of runtime attribute values.
  AttributeList mRtsToFetch;
  bool mIsSync;
  bool mIsAccessor;
  timespec mLastSearch;
  bool mNonExtendedName;
};

#endif  // IMM_IMMND_IMMSEARCHOP_H_
