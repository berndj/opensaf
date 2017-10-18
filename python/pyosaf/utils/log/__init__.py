############################################################################
#
# (C) Copyright 2015 The OpenSAF Foundation
# (C) Copyright 2017 Ericsson AB. All rights reserved.
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
# Author(s): Ericsson
#
############################################################################
""" LOG common utilities """
from pyosaf.saAis import SaVersionT
from pyosaf import saLog
from pyosaf.utils import decorate, initialize_decorate


LOG_VERSION = SaVersionT('A', 2, 1)

# Decorate pure saLog* API's with error-handling retry and exception raising
saLogInitialize = initialize_decorate(saLog.saLogInitialize)
saLogSelectionObjectGet = decorate(saLog.saLogSelectionObjectGet)
saLogDispatch = decorate(saLog.saLogDispatch)
saLogFinalize = decorate(saLog.saLogFinalize)
saLogStreamOpen_2 = decorate(saLog.saLogStreamOpen_2)
saLogStreamOpenAsync_2 = decorate(saLog.saLogStreamOpenAsync_2)
saLogWriteLog = decorate(saLog.saLogWriteLog)
saLogWriteLogAsync = decorate(saLog.saLogWriteLogAsync)
saLogStreamClose = decorate(saLog.saLogStreamClose)
saLogLimitGet = decorate(saLog.saLogLimitGet)
