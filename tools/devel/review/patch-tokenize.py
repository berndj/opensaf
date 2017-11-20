#!/usr/bin/python
#      -*- OpenSAF  -*-
#
# (C) Copyright 2010 The OpenSAF Foundation
# (C) Copyright Ericsson AB 2015, 2016, 2017. All rights reserved.
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
#            Ericsson AB
#
# pylint: disable=invalid-name
""" Tokenize alphanumeric words in patch file """
from __future__ import print_function
import re
import sys

# Validate only on lines that are patch additions (e.g. + foo)
pattern = re.compile(r"\+\s.*")

m = pattern.match(sys.argv[1])
if m:
    # Tokenize all alphanumeric words for banned word lookup
    for word in re.findall(r"\s*(\w+).?\(", m.group(0)):
        print (word)
