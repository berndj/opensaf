#      -*- OpenSAF  -*-
#
# (C) Copyright 2010 The OpenSAF Foundation
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
# Author(s): Wind River Systems 
#

include $(top_srcdir)/Makefile.common

MAINTAINERCLEANFILES = Makefile.in

EXTRA_DIST = \
	amf_demo_script \
	AppConfig-2N.xml \
	AppConfig-nwayactive.xml \
	README

bin_PROGRAMS = amf_demo

amf_demo_CPPFLAGS = \
	-DSA_EXTENDED_NAME_SOURCE \
	$(AM_CPPFLAGS)

amf_demo_SOURCES = \
	amf_demo.c

amf_demo_LDADD = \
	@SAF_AIS_AMF_LIBS@

install-data-hook:
	$(mkinstalldirs) $(DESTDIR)/opt/amf_demo
	cp amf_demo $(DESTDIR)/opt/amf_demo
	cp amf_demo_script $(DESTDIR)/opt/amf_demo

uninstall-hook:
	rm -rf $(DESTDIR)/opt/amf_demo

