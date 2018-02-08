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
	src/clm/README

osaf_lib_LTLIBRARIES += lib/libclm_common.la

lib_libclm_common_la_CFLAGS = $(OSAF_LIB_FLAGS) $(AM_CFLAGS)

lib_libclm_common_la_CXXFLAGS = $(OSAF_LIB_FLAGS) $(AM_CXXFLAGS)

lib_libclm_common_la_CPPFLAGS = \
	$(AM_CPPFLAGS)

lib_libclm_common_la_LDFLAGS = \
	$(AM_LDFLAGS)

lib_libclm_common_la_SOURCES = \
	src/clm/common/clmsv_enc_dec.cc

nodist_EXTRA_lib_libclm_common_la_SOURCES = dummy.cc

lib_LTLIBRARIES += lib/libSaClm.la

lib_libSaClm_la_SOURCES = \
	src/clm/agent/clma_api.cc \
	src/clm/agent/clma_mds.cc \
	src/clm/agent/clma_util.cc

nodist_EXTRA_lib_libSaClm_la_SOURCES = dummy.cc

lib_libSaClm_la_CFLAGS = $(OSAF_LIB_FLAGS) $(AM_CFLAGS)

lib_libSaClm_la_CXXFLAGS = $(OSAF_LIB_FLAGS) $(AM_CXXFLAGS)

lib_libSaClm_la_CPPFLAGS = \
	-DSA_CLM_B01=1 \
	$(AM_CPPFLAGS)

lib_libSaClm_la_LDFLAGS = \
	$(AM_LDFLAGS)

if HAVE_LD_VERSION_SCRIPT
lib_libSaClm_la_LDFLAGS += \
	-Wl,-version-script=$(top_srcdir)/src/ais/lib/libSaClm.map
endif

lib_libSaClm_la_LDFLAGS += \
	-version-number 0:0:0

lib_libSaClm_la_LIBADD = \
	lib/libclm_common.la \
	lib/libais.la \
	lib/libopensaf_core.la

noinst_HEADERS += \
	src/clm/agent/clma.h \
	src/clm/clmd/clms.h \
	src/clm/clmd/clms_cb.h \
	src/clm/clmd/clms_evt.h \
	src/clm/clmd/clms_imm.h \
	src/clm/clmd/clms_mbcsv.h \
	src/clm/clmd/clms_ntf.h \
	src/clm/clmnd/cb.h \
	src/clm/clmnd/clmna.h \
	src/clm/clmnd/election_starter.h \
	src/clm/clmnd/election_starter_wrapper.h \
	src/clm/clmnd/evt.h \
	src/clm/common/clmsv_defs.h \
	src/clm/common/clmsv_enc_dec.h \
	src/clm/common/clmsv_msg.h

bin_PROGRAMS += bin/clmprint
osaf_execbin_PROGRAMS += bin/osafclmd bin/osafclmna

nodist_pkgclccli_SCRIPTS += \
	src/clm/clmd/osaf-clmd \
	src/clm/clmnd/osaf-clmna

dist_pkgclmscripts_SCRIPTS += \
	scripts/clm-scripts/osafclm_test

dist_pkgsysconf_DATA += \
	src/clm/clmd/clmd.conf \
	src/clm/clmnd/clmna.conf

if ENABLE_IMMXML

dist_pkgimmxml_svc_DATA += \
	src/clm/config/clmsv_classes.xml \
	src/clm/config/clmsv_objects.xml

if ENABLE_AIS_PLM
dist_pkgimmxml_svc_DATA += \
	src/clm/config/clmsv_plm_pl_template.xml \
	src/clm/config/clmsv_plm_sc_template.xml
else
dist_pkgimmxml_svc_DATA += \
	src/clm/config/clmsv_pl_template.xml \
	src/clm/config/clmsv_sc_template.xml
endif

endif

bin_osafclmd_CXXFLAGS = $(AM_CXXFLAGS)

bin_osafclmd_CPPFLAGS = \
	$(AM_CPPFLAGS)

bin_osafclmd_SOURCES = \
	src/clm/clmd/clms_amf.cc \
	src/clm/clmd/clms_evt.cc \
	src/clm/clmd/clms_imm.cc \
	src/clm/clmd/clms_main.cc \
	src/clm/clmd/clms_mbcsv.cc \
	src/clm/clmd/clms_mds.cc \
	src/clm/clmd/clms_ntf.cc \
	src/clm/clmd/clms_util.cc

bin_osafclmd_LDADD = \
	lib/libclm_common.la \
	lib/libosaf_common.la \
	lib/libosaf_common.la \
	lib/libSaAmf.la \
	lib/libSaImmOi.la \
	lib/libSaImmOm.la \
	lib/libSaLog.la \
	lib/libSaNtf.la \
	lib/libopensaf_core.la

if ENABLE_AIS_PLM

bin_osafclmd_SOURCES += src/clm/clmd/clms_plm.cc

bin_osafclmd_LDADD += \
	lib/libSaPlm.la

endif

bin_osafclmna_CXXFLAGS = $(AM_CXXFLAGS)

bin_osafclmna_CPPFLAGS = \
	$(AM_CPPFLAGS)

bin_osafclmna_SOURCES = \
	src/clm/clmnd/amf.cc \
	src/clm/clmnd/election_starter.cc \
	src/clm/clmnd/election_starter_wrapper.cc \
	src/clm/clmnd/main.cc

bin_osafclmna_LDADD = \
	lib/libclm_common.la \
	lib/libSaAmf.la \
	lib/libopensaf_core.la

dist_bin_SCRIPTS += \
       src/clm/tools/clm-adm \
       src/clm/tools/clm-find \
       src/clm/tools/clm-state

bin_clmprint_CXXFLAGS = $(AM_CXXFLAGS)

bin_clmprint_CPPFLAGS = \
        -DSA_EXTENDED_NAME_SOURCE \
        $(AM_CPPFLAGS)

bin_clmprint_SOURCES = \
        src/clm/tools/clm_print.cc

bin_clmprint_LDADD = \
        lib/libSaClm.la \
	lib/libopensaf_core.la


if ENABLE_TESTS

bin_PROGRAMS += bin/clmtest

noinst_HEADERS += \
	src/clm/apitest/clmtest.h \
	src/clm/apitest/clm_api_with_try_again.h

bin_clmtest_CXXFLAGS = $(AM_CXXFLAGS)

bin_clmtest_CPPFLAGS = \
	-DSA_CLM_B01=1 \
	$(AM_CPPFLAGS)

bin_clmtest_SOURCES = \
	src/clm/apitest/clmtest.cc \
	src/clm/apitest/tet_saClmInitialize.cc \
	src/clm/apitest/tet_saClmClusterNodeGetAsync.cc \
	src/clm/apitest/tet_saClmClusterTrack.cc \
	src/clm/apitest/tet_saClmClusterTrackStop.cc \
	src/clm/apitest/tet_saClmClusterNotificationFree.cc \
	src/clm/apitest/tet_saClmClusterNodeGet.cc \
	src/clm/apitest/tet_saClmResponse.cc \
	src/clm/apitest/tet_ClmOiOps.cc \
	src/clm/apitest/tet_saClmDispatch.cc \
	src/clm/apitest/tet_saClmSelectionObjectGet.cc \
	src/clm/apitest/tet_saClmFinalize.cc \
	src/clm/apitest/tet_ClmLongRdn.cc

bin_clmtest_LDADD = \
	lib/libSaClm.la \
	lib/libSaImmOm.la \
	lib/libopensaf_core.la \
	lib/libapitest.la

endif
