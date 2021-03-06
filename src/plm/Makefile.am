#      -*- OpenSAF  -*-
#
# (C) Copyright 2016 The OpenSAF Foundation
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

EXTRA_DIST += \
	src/plm/README \
	src/plm/README.conf \
	src/plm/config/plmsv_objects.xml.atca \
	src/plm/config/plmsv_objects.xml.hp \
	src/plm/config/plmsv_objects.xml.pc \
	src/plm/plmcd/README \
	src/plm/plmcd/test_lib.c

if ENABLE_AIS_PLM

lib_LTLIBRARIES += lib/libplmc_utils.la

lib_libplmc_utils_la_CFLAGS = $(OSAF_LIB_FLAGS) $(AM_CFLAGS)

lib_libplmc_utils_la_CXXFLAGS = $(OSAF_LIB_FLAGS) $(AM_CXXFLAGS)

lib_libplmc_utils_la_CPPFLAGS = \
	-DNCS_PLMSV_LOG=1 \
	$(AM_CPPFLAGS)

lib_libplmc_utils_la_LDFLAGS = \
	$(AM_LDFLAGS)

lib_libplmc_utils_la_SOURCES = \
	src/plm/plmcd/plmc_cmd_string_to_enum.c \
	src/plm/plmcd/plmc_get_listening_ip_addr.c \
	src/plm/plmcd/plmc_read_config.c

nodist_EXTRA_lib_libplmc_utils_la_SOURCES = dummy.cc

osaf_lib_LTLIBRARIES += lib/libplm_common.la

lib_libplm_common_la_CFLAGS = $(OSAF_LIB_FLAGS) $(AM_CFLAGS) @HPI_CFLAGS@

lib_libplm_common_la_CXXFLAGS = $(OSAF_LIB_FLAGS) $(AM_CXXFLAGS) @HPI_CFLAGS@

lib_libplm_common_la_CPPFLAGS = \
	-DNCS_PLMSV_LOG=1 \
	$(AM_CPPFLAGS)

lib_libplm_common_la_LDFLAGS = \
	$(AM_LDFLAGS)

lib_libplm_common_la_SOURCES = \
	src/plm/common/plms_common_mds.c \
	src/plm/common/plms_common_utils.c \
	src/plm/common/plms_edu.c

nodist_EXTRA_lib_libplm_common_la_SOURCES = dummy.cc

pkglib_LTLIBRARIES += lib/libplmc.la

lib_libplmc_la_CFLAGS = $(OSAF_LIB_FLAGS) $(AM_CFLAGS)

lib_libplmc_la_CXXFLAGS = $(OSAF_LIB_FLAGS) $(AM_CXXFLAGS)

lib_libplmc_la_CPPFLAGS = \
	-DNCS_PLMSV_LOG=1 \
	$(AM_CPPFLAGS)

lib_libplmc_la_LDFLAGS = $(AM_LDFLAGS)

lib_libplmc_la_SOURCES = \
	src/plm/plmcd/plmc_lib.c \
	src/plm/plmcd/plmc_lib_internal.c

nodist_EXTRA_lib_libplmc_la_SOURCES = dummy.cc

lib_libplmc_la_LIBADD = \
	lib/libplmc_utils.la

pkglib_LTLIBRARIES += lib/libplms_hpi.la

lib_libplms_hpi_la_CPPFLAGS = \
	$(AM_CPPFLAGS)

lib_libplms_hpi_la_CFLAGS = $(OSAF_LIB_FLAGS) $(AM_CFLAGS) @HPI_CFLAGS@

lib_libplms_hpi_la_CXXFLAGS = $(OSAF_LIB_FLAGS) $(AM_CXXFLAGS) @HPI_CFLAGS@

lib_libplms_hpi_la_LDFLAGS = $(AM_LDFLAGS)

lib_libplms_hpi_la_SOURCES = \
	src/plm/common/plms_epath_util.c \
	src/plm/common/plms_hrb.c \
	src/plm/common/plms_hrb_mds.c \
	src/plm/common/plms_hsm.c

nodist_EXTRA_lib_libplms_hpi_la_SOURCES = dummy.cc

lib_libplms_hpi_la_LIBADD = \
	lib/libplmc.la \
	lib/libplmc_utils.la \
	@HPI_LIBS@

lib_LTLIBRARIES += lib/libSaPlm.la

lib_libSaPlm_la_SOURCES = \
	src/plm/agent/plma_api.c \
	src/plm/agent/plma_comm.c \
	src/plm/agent/plma_init.c \
	src/plm/agent/plma_mds.c

nodist_EXTRA_lib_libSaPlm_la_SOURCES = dummy.cc

lib_libSaPlm_la_CFLAGS = $(OSAF_LIB_FLAGS) $(AM_CFLAGS) @HPI_CFLAGS@

lib_libSaPlm_la_CXXFLAGS = $(OSAF_LIB_FLAGS) $(AM_CXXFLAGS) @HPI_CFLAGS@

lib_libSaPlm_la_CPPFLAGS = \
	-DNCS_PLMA=1 -DNCS_PLMSV_LOG=1 \
	$(AM_CPPFLAGS)

lib_libSaPlm_la_LDFLAGS = \
	$(AM_LDFLAGS)

if HAVE_LD_VERSION_SCRIPT
lib_libSaPlm_la_LDFLAGS += \
	-Wl,-version-script=$(top_srcdir)/src/ais/lib/libSaPlm.map
endif

lib_libSaPlm_la_LDFLAGS += \
	-version-number 0:0:0

lib_libSaPlm_la_LIBADD = \
	lib/libplm_common.la \
	lib/libais.la \
	lib/libopensaf_core.la

noinst_HEADERS += \
	src/plm/agent/plma.h \
	src/plm/common/plms.h \
	src/plm/common/plms_evt.h \
	src/plm/common/plms_hpi.h \
	src/plm/common/plms_hrb.h \
	src/plm/common/plms_hsm.h \
	src/plm/common/plms_mbcsv.h \
	src/plm/common/plms_notifications.h \
	src/plm/common/plms_scale.h \
	src/plm/common/plms_utils.h \
	src/plm/common/plms_virt.h \
	src/plm/plmcd/plmc.h \
	src/plm/plmcd/plmc_cmds.h \
	src/plm/plmcd/plmc_lib.h \
	src/plm/plmcd/plmc_lib_internal.h

osaf_execbin_PROGRAMS += bin/osafplmd

nodist_pkgclccli_SCRIPTS += \
	src/plm/plmd/osaf-plmd

if ENABLE_IMMXML

dist_pkgimmxml_svc_DATA += \
	src/plm/config/plmsv_classes.xml \
	src/plm/config/plmsv_objects.xml \
	src/plm/config/plmsv_pl_template.xml \
	src/plm/config/plmsv_sc_template.xml

endif

dist_pkgsysconf_DATA += \
	src/plm/plmd/plmd.conf

bin_osafplmd_CPPFLAGS = \
	$(AM_CPPFLAGS)

bin_osafplmd_CFLAGS = $(AM_CFLAGS) @HPI_CFLAGS@

bin_osafplmd_CXXFLAGS = $(AM_CXXFLAGS) @HPI_CFLAGS@

bin_osafplmd_SOURCES = \
	src/plm/plmd/plms_adm_fsm.c \
	src/plm/plmd/plms_amf.c \
	src/plm/plmd/plms_dbg_utils.c \
	src/plm/plmd/plms_he_pres_fsm.c \
	src/plm/plmd/plms_imm.c \
	src/plm/plmd/plms_main.c \
	src/plm/plmd/plms_mbcsv.c \
	src/plm/plmd/plms_mds.c \
	src/plm/plmd/plms_notifications.c \
	src/plm/plmd/plms_plmc.c \
	src/plm/plmd/plms_proc.c \
	src/plm/plmd/plms_scale.cc \
	src/plm/plmd/plms_stdby.c \
	src/plm/plmd/plms_utils.c \
	src/plm/plmd/plms_virt.cc

bin_osafplmd_LDADD = \
	@LIBVIRT_LIBS@ \
	lib/libosaf_common.la \
	lib/libopensaf_core.la \
	lib/libplmc.la \
	lib/libplm_common.la \
	lib/libSaAmf.la \
	lib/libSaImmOi.la \
	lib/libSaImmOm.la \
	lib/libSaNtf.la

if ENABLE_TESTS

noinst_HEADERS += \
	src/plm/apitest/plmtest.h

pkglib_LTLIBRARIES += lib/libplmtest.la

lib_libplmtest_la_CFLAGS = $(OSAF_LIB_FLAGS) $(AM_CFLAGS)

lib_libplmtest_la_CXXFLAGS = $(OSAF_LIB_FLAGS) $(AM_CXXFLAGS)

lib_libplmtest_la_CPPFLAGS = \
	$(AM_CPPFLAGS)

lib_libplmtest_la_SOURCES = \
	src/plm/apitest/plmtest.c

nodist_EXTRA_lib_libplmtest_la_SOURCES = dummy.cc

bin_PROGRAMS += bin/plmtest

bin_plmtest_CPPFLAGS = \
	$(AM_CPPFLAGS)

bin_plmtest_SOURCES = \
	src/plm/apitest/test_saPlmInitialize.c	\
	src/plm/apitest/test_saPlmSelectionObjectGet.c \
	src/plm/apitest/test_saPlmDispatch.c  \
	src/plm/apitest/test_saPlmFinalize.c \
	src/plm/apitest/test_saPlmEntityGroupCreate.c \
	src/plm/apitest/test_saPlmEntityGroupAdd.c \
	src/plm/apitest/test_saPlmEntityGroupDelete.c \
	src/plm/apitest/test_saPlmEntityGroupRemove.c \
	src/plm/apitest/test_saPlmReadinessTrack.c

bin_plmtest_LDADD = \
	lib/libSaPlm.la \
	lib/libopensaf_core.la \
	lib/libapitest.la \
	lib/libplmtest.la

endif

dist_sysconf_DATA += \
	src/plm/plmcd/plmcd.conf

sbin_PROGRAMS += bin/plmcd

bin_plmcd_CPPFLAGS = \
	$(AM_CPPFLAGS)

bin_plmcd_CFLAGS = $(AM_CFLAGS)

bin_plmcd_SOURCES = \
	src/plm/plmcd/plmcd.c

bin_plmcd_LDFLAGS = $(AM_LDFLAGS)

bin_plmcd_LDADD = \
	lib/libplmc_utils.la

if ENABLE_SYSTEMD

dist_systemd_DATA += \
	src/plm/plmcd/plmcboot.service \
	src/plm/plmcd/plmcd.service

bin_plmcd_LDFLAGS += -lsystemd-daemon

else

nodist_lsbinit_SCRIPTS += \
	src/plm/plmcd/plmcboot \
	src/plm/plmcd/plmcd

endif

endif
