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
#include "experimental/immcpp/api/include/oi_implementer.h"

//>
// To verify interfaces of ImmOiObjectImplementer class.
// Steps:
// 1) Create ImmOiObjectImplementer object
// 2) Invoke one of its interfaces
// 3) Check if the private data is formed as expected or not
//<

TEST(ImmOiObjectImplementer, ImmOiObjectImplementer_NoClassName) {
  const std::string implname{"helloworld"};
  immoi::ImmOiObjectImplementer implobj{10, implname};
  ASSERT_EQ(implobj.oi_handle_, 10);
  ASSERT_EQ(implobj.implementer_name_, implname);
  ASSERT_EQ(implobj.list_of_class_names_.empty(), true);
}

TEST(ImmOiObjectImplementer, ImmOiObjectImplementer) {
  const std::string implname{"helloworld"};
  const std::vector<std::string> classnames{"one", "two"};
  immoi::ImmOiObjectImplementer implobj{10, implname, classnames};
  ASSERT_EQ(implobj.oi_handle_, 10);
  ASSERT_EQ(implobj.implementer_name_, implname);
  ASSERT_EQ(implobj.list_of_class_names_, classnames);
}
