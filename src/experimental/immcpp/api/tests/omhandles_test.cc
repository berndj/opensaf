/*      -*- OpenSAF  -*-
 *
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
 * Author(s): Ericsson AB
 *
 */

// To be able to access directly private attributes of class
#define protected public
#define private public

#include <string>
#include <vector>
#include "gtest/gtest.h"
#include "experimental/immcpp/api/include/om_handle.h"
#include "experimental/immcpp/api/include/om_search_handle.h"
#include "experimental/immcpp/api/include/om_accessor_handle.h"
#include "experimental/immcpp/api/include/om_admin_owner_handle.h"
#include "experimental/immcpp/api/include/om_ccb_handle.h"

//>
// To verify if handles (OM handle, OM CCB handles, etc.) constructs
// with default values as expected or not.
//<
static SaVersionT version{'A', 10, 20};
static void adminopcb(SaInvocationT invo, SaAisErrorT opret, SaAisErrorT err) {
};
static SaImmCallbacksT cb{adminopcb};


//
// ImmOmHandle
//
TEST(ImmOmHandle, ImmOmHandle) {
  immom::ImmOmHandle omhandleobj{&cb};

  // Validate if inherited attributes as expected
  EXPECT_EQ(omhandleobj.ais_error_, SA_AIS_OK);
  EXPECT_EQ(omhandleobj.retry_ctrl_.interval, kDefaultIntervalMs);
  EXPECT_EQ(omhandleobj.retry_ctrl_.timeout, kDefaultTimeoutMs);
  EXPECT_EQ(omhandleobj.om_handle_, 0);

  // Validate if having default version as expected
  EXPECT_EQ(omhandleobj.version_.releaseCode, 'A');
  EXPECT_EQ(omhandleobj.version_.majorVersion, 2);
  EXPECT_EQ(omhandleobj.version_.minorVersion, 11);

  EXPECT_EQ(omhandleobj.callbacks_, &cb);
}

TEST(ImmOmHandle, ImmOmHandle_Version) {
  immom::ImmOmHandle omhandleobj{version, nullptr};

  // Validate if inherited attributes as expected
  EXPECT_EQ(omhandleobj.ais_error_, SA_AIS_OK);
  EXPECT_EQ(omhandleobj.retry_ctrl_.interval, kDefaultIntervalMs);
  EXPECT_EQ(omhandleobj.retry_ctrl_.timeout, kDefaultTimeoutMs);
  EXPECT_EQ(omhandleobj.om_handle_, 0);

  // Validate if having version as inputed one
  EXPECT_EQ(omhandleobj.version_.releaseCode, version.releaseCode);
  EXPECT_EQ(omhandleobj.version_.majorVersion, version.majorVersion);
  EXPECT_EQ(omhandleobj.version_.minorVersion, version.minorVersion);

  EXPECT_EQ(omhandleobj.callbacks_, nullptr);
}

//
// ImmOmSearchHandle
//
static immom::ImmOmSearchCriteria setting{};
TEST(ImmOmSearchHandle, ImmOmSearchHandle) {
  immom::ImmOmSearchHandle searchobj{10};

  EXPECT_EQ(searchobj.om_handle_, 10);
  EXPECT_EQ(searchobj.search_handle_, 0);
  EXPECT_EQ(searchobj.setting_, nullptr);
}

TEST(ImmOmSearchHandle, ImmOmSearchHandle_Setting) {
  immom::ImmOmSearchHandle searchobj{10, setting};

  EXPECT_EQ(searchobj.om_handle_, 10);

  EXPECT_EQ(searchobj.search_handle_, 0);

  // Check if having default setting_ as expected
  EXPECT_EQ(searchobj.setting_->attribute_name_.empty(), true);
  EXPECT_EQ(searchobj.setting_->attribute_value_.empty(), true);
  EXPECT_EQ(searchobj.setting_->search_root_.empty(), true);
  EXPECT_EQ(searchobj.setting_->scope_, SA_IMM_SUBTREE);
  EXPECT_EQ(searchobj.setting_->option_, SA_IMM_SEARCH_GET_NO_ATTR);
  EXPECT_EQ(searchobj.setting_->attributes_.empty(), true);

  EXPECT_EQ(searchobj.setting_->search_param_.searchOneAttr.attrName, nullptr);
  EXPECT_EQ(searchobj.setting_->search_param_.searchOneAttr.attrValue, nullptr);
}

//
// ImmOmAccessorHandle
//
TEST(ImmOmAccessorHandle, ImmOmAccessorHandle) {
  immom::ImmOmAccessorHandle accessorobj{10};

  EXPECT_EQ(accessorobj.om_handle_, 10);
  EXPECT_EQ(accessorobj.access_handle_, 0);
}

//
// ImmOmCcbhandle
//
TEST(ImmOmCcbHandle, ImmOmCcbHandle) {
  immom::ImmOmCcbHandle ccbhandleobj{10};

  EXPECT_EQ(ccbhandleobj.admin_owner_handle_, 10);
  EXPECT_EQ(ccbhandleobj.ccb_flags_, SA_IMM_CCB_REGISTERED_OI);
  EXPECT_EQ(ccbhandleobj.ccb_handle_, 0);
}

TEST(ImmOmCcbHandle, ImmOmCcbHandle_Flags) {
  immom::ImmOmCcbHandle ccbhandleobj{10, 0};

  EXPECT_EQ(ccbhandleobj.admin_owner_handle_, 10);
  EXPECT_EQ(ccbhandleobj.ccb_flags_, 0);
  EXPECT_EQ(ccbhandleobj.ccb_handle_, 0);
}

//
// ImmOmAdminOwnerHandle
//
TEST(ImmOmAdminOwnerHandle, ImmOmAdminOwnerHandle) {
  immom::ImmOmAdminOwnerHandle adminownerobj{10, "hello"};

  EXPECT_EQ(adminownerobj.om_handle_, 10);
  EXPECT_EQ(adminownerobj.admin_owner_name_, "hello");
  EXPECT_EQ(adminownerobj.release_ownership_on_finalize_, SA_TRUE);
  EXPECT_EQ(adminownerobj.admin_owner_handle_, 0);
}
