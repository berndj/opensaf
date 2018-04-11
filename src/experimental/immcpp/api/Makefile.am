#      -*- OpenSAF  -*-
#
# Copyright Ericsson AB 2017 - All Rights Reserved.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
# or FITNESS FOR A PARTICULAR PURPOSE. This file and program are licensed
# under the GNU Lesser General Public License Version 2.1, February 1999.
# The complete license can be accessed from the following location:
# http://opensource.org/licenses/lgpl-license.php
# See the Copying file included with the OpenSAF distribution for full
# licensing terms.
#
# Author(s): Ericsson AB
#

pkglib_LTLIBRARIES += lib/libopensaf_immoi.la lib/libopensaf_immom.la

lib_libopensaf_immoi_la_CFLAGS = $(OSAF_LIB_FLAGS) $(AM_CFLAGS)
lib_libopensaf_immom_la_CFLAGS = $(OSAF_LIB_FLAGS) $(AM_CFLAGS)

lib_libopensaf_immoi_la_CXXFLAGS = $(OSAF_LIB_FLAGS) $(AM_CXXFLAGS)
lib_libopensaf_immom_la_CXXFLAGS = $(OSAF_LIB_FLAGS) $(AM_CXXFLAGS)

lib_libopensaf_immoi_la_LDFLAGS = \
	$(AM_LDFLAGS) \
	-version-number 0:2:0

lib_libopensaf_immom_la_LDFLAGS = \
	$(AM_LDFLAGS) \
	-version-number 0:2:0

lib_libopensaf_immoi_la_SOURCES = \
	src/experimental/immcpp/api/common/common.cc \
	src/experimental/immcpp/api/common/imm_attribute.cc \
	src/experimental/immcpp/api/oi/oi_handle.cc \
	src/experimental/immcpp/api/oi/oi_implementer.cc \
	src/experimental/immcpp/api/oi/oi_runtime_object_create.cc \
	src/experimental/immcpp/api/oi/oi_runtime_object_update.cc \
	src/experimental/immcpp/api/oi/oi_runtime_object_delete.cc \
	src/experimental/immcpp/api/oi/oi_admin_operation_result.cc

lib_libopensaf_immom_la_SOURCES = \
	src/experimental/immcpp/api/common/common.cc \
	src/experimental/immcpp/api/common/imm_attribute.cc \
	src/experimental/immcpp/api/om/om_handle.cc \
	src/experimental/immcpp/api/om/om_search_handle.cc \
	src/experimental/immcpp/api/om/om_search_next.cc \
	src/experimental/immcpp/api/om/om_class_description_get.cc \
	src/experimental/immcpp/api/om/om_accessor_handle.cc \
	src/experimental/immcpp/api/om/om_accessor_get.cc \
	src/experimental/immcpp/api/om/om_class_create.cc \
	src/experimental/immcpp/api/om/om_class_delete.cc \
	src/experimental/immcpp/api/om/om_admin_owner_handle.cc \
	src/experimental/immcpp/api/om/om_admin_owner_set.cc \
	src/experimental/immcpp/api/om/om_admin_owner_release.cc \
	src/experimental/immcpp/api/om/om_admin_owner_clear.cc \
	src/experimental/immcpp/api/om/om_admin_operation.cc \
	src/experimental/immcpp/api/om/om_ccb_handle.cc \
	src/experimental/immcpp/api/om/om_ccb_object_create.cc \
	src/experimental/immcpp/api/om/om_ccb_object_delete.cc \
	src/experimental/immcpp/api/om/om_ccb_object_modify.cc

nodist_EXTRA_lib_libopensaf_immoi_la_SOURCES = dummy.cc
nodist_EXTRA_lib_libopensaf_immom_la_SOURCES = dummy.cc

LOCAL_IMM_OI_LIBADD = \
	lib/libosaf_common.la \
	lib/libSaImmOi.la \
	lib/libopensaf_core.la

LOCAL_IMM_OM_LIBADD = \
	lib/libosaf_common.la \
	lib/libSaImmOm.la \
	lib/libopensaf_core.la

lib_libopensaf_immoi_la_LIBADD = $(LOCAL_IMM_OI_LIBADD)
lib_libopensaf_immom_la_LIBADD = $(LOCAL_IMM_OM_LIBADD)

noinst_HEADERS += \
	src/experimental/immcpp/api/common/common.h \
	src/experimental/immcpp/api/common/imm_cpp_type.h \
	src/experimental/immcpp/api/common/imm_attribute.h \
	src/experimental/immcpp/api/include/om_handle.h \
	src/experimental/immcpp/api/include/om_search_handle.h \
	src/experimental/immcpp/api/include/om_search_next.h \
	src/experimental/immcpp/api/include/om_class_description_get.h \
	src/experimental/immcpp/api/include/om_accessor_handle.h \
	src/experimental/immcpp/api/include/om_accessor_get.h \
	src/experimental/immcpp/api/include/om_admin_owner_handle.h \
	src/experimental/immcpp/api/include/om_admin_owner_set.h \
	src/experimental/immcpp/api/include/om_admin_owner_release.h \
	src/experimental/immcpp/api/include/om_admin_owner_clear.h \
	src/experimental/immcpp/api/include/om_admin_operation.h \
	src/experimental/immcpp/api/include/om_ccb_handle.h \
	src/experimental/immcpp/api/include/om_ccb_object_create.h \
	src/experimental/immcpp/api/include/om_ccb_object_delete.h \
	src/experimental/immcpp/api/include/om_ccb_object_modify.h \
	src/experimental/immcpp/api/include/om_class_create.h \
	src/experimental/immcpp/api/include/oi_implementer.h \
	src/experimental/immcpp/api/include/oi_runtime_object_create.h \
	src/experimental/immcpp/api/include/oi_runtime_object_update.h \
	src/experimental/immcpp/api/include/oi_runtime_object_delete.h \
	src/experimental/immcpp/api/include/oi_admin_operation_result.h

bin_PROGRAMS += bin/immcppcfg

CORE_INCLUDES += -I$(top_srcdir)/src/osaf/saf \
		-I$(top_srcdir)


TESTS += bin/testimmcpp

bin_testimmcpp_CXXFLAGS =$(AM_CXXFLAGS)

bin_testimmcpp_CPPFLAGS = \
	-DSA_EXTENDED_NAME_SOURCE \
	$(AM_CPPFLAGS) \
	-I$(GTEST_DIR)/include \
	-I$(GMOCK_DIR)/include

bin_testimmcpp_LDFLAGS = \
	$(AM_LDFLAGS)

noinst_HEADERS += \
	src/experimental/immcpp/api/tests/common.h

bin_testimmcpp_SOURCES = \
	src/experimental/immcpp/api/tests/omclassmanagement_test.cc \
	src/experimental/immcpp/api/tests/omsearchnext_test.cc \
	src/experimental/immcpp/api/tests/omaccessorget_test.cc \
	src/experimental/immcpp/api/tests/omsearchcriteria_test.cc \
	src/experimental/immcpp/api/tests/omclasscreate_test.cc \
	src/experimental/immcpp/api/tests/omhandles_test.cc \
	src/experimental/immcpp/api/tests/oiruntimeobjectcreate_test.cc \
	src/experimental/immcpp/api/tests/oiruntimeobjectupdate_test.cc \
	src/experimental/immcpp/api/tests/oiimplementer_test.cc \
	src/experimental/immcpp/api/tests/immattribute_test.cc

bin_testimmcpp_LDADD = \
	$(GTEST_DIR)/lib/libgtest.la \
	$(GTEST_DIR)/lib/libgtest_main.la \
	$(GMOCK_DIR)/lib/libgmock.la \
	$(GMOCK_DIR)/lib/libgmock_main.la \
	lib/libopensaf_core.la \
	lib/libopensaf_immoi.la \
	lib/libopensaf_immom.la

bin_immcppcfg_CXXFLAGS = \
	$(AM_CXXFLAGS)

bin_immcppcfg_CPPFLAGS = \
	-DSA_EXTENDED_NAME_SOURCE \
	$(AM_CPPFLAGS)

bin_immcppcfg_SOURCES = \
	src/experimental/immcpp/api/demo/immcppcfg.cc

bin_immcppcfg_LDADD = \
	lib/libosaf_common.la \
	lib/libopensaf_immom.la \
	lib/libopensaf_core.la \
	lib/libais.la

if ENABLE_TESTS

bin_PROGRAMS += bin/immcpptest

noinst_HEADERS += \
	src/experimental/immcpp/api/demo/common.h \
	src/experimental/immcpp/api/demo/omclassmanagement.h \
	src/experimental/immcpp/api/demo/omsearchnext.h \
	src/experimental/immcpp/api/demo/omaccessorget.h \
	src/experimental/immcpp/api/demo/omclasscreate.h \
	src/experimental/immcpp/api/demo/omconfigurationchanges.h \
	src/experimental/immcpp/api/demo/oiruntimeobject.h

bin_immcpptest_CFLAGS = $(AM_CFLAGS) -Wformat=1

bin_immcpptest_CPPFLAGS = \
	-DSA_EXTENDED_NAME_SOURCE \
	$(AM_CPPFLAGS)

bin_immcpptest_SOURCES = \
	src/experimental/immcpp/api/demo/omsearchnext.cc \
	src/experimental/immcpp/api/demo/omclassmanagement.cc \
	src/experimental/immcpp/api/demo/omaccessorget.cc \
	src/experimental/immcpp/api/demo/omclasscreate.cc \
	src/experimental/immcpp/api/demo/omconfigurationchanges.cc \
	src/experimental/immcpp/api/demo/oiruntimeobject.cc \
	src/experimental/immcpp/api/demo/hold_changes_info.cc \
	src/experimental/immcpp/api/demo/immcpptest.cc

bin_immcpptest_LDADD = \
	lib/libosaf_common.la \
	lib/libSaImmOi.la \
	lib/libSaImmOm.la \
	lib/libopensaf_core.la \
	lib/libopensaf_immom.la \
	lib/libopensaf_immoi.la


endif
