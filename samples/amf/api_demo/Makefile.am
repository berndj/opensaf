#      -*- OpenSAF  -*-
#
# Copyright (C) 2017, Oracle and/or its affiliates. All rights reserved.
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
#

include $(top_srcdir)/Makefile.common

MAINTAINERCLEANFILES = Makefile.in

EXTRA_DIST = README

bin_PROGRAMS = amfscstatusdemo amfscstatusdispatchdemo

amfscstatusdemo_CPPFLAGS = \
        -DSA_EXTENDED_NAME_SOURCE \
        $(AM_CPPFLAGS)

amfscstatusdemo_SOURCES = \
        amf_sc_status_demo.c

amfscstatusdemo_LDADD = \
        @SAF_AIS_AMF_LIBS@

amfscstatusdispatchdemo_CPPFLAGS = \
        -DSA_EXTENDED_NAME_SOURCE \
        $(AM_CPPFLAGS)

amfscstatusdispatchdemo_SOURCES = \
        amf_sc_status_dispatch_demo.c

amfscstatusdispatchdemo_LDADD = \
        @SAF_AIS_AMF_LIBS@

install-data-hook:
	$(mkinstalldirs) $(DESTDIR)/opt/amf_api_demo
	cp amfscstatusdemo $(DESTDIR)/opt/amf_api_demo
	cp amfscstatusdispatchdemo $(DESTDIR)/opt/amf_api_demo

uninstall-hook:
	rm -rf $(DESTDIR)/opt/amf_api_demo

