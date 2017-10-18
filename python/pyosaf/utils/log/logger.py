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
""" SAF Logger utility """
import ctypes

from pyosaf import saLog, saAis
from pyosaf.utils import log


class SafLogger(object):
    """ SafLogger class handling log record write to the system log stream """
    @staticmethod
    def write_log_callback(invocation, error):
        """ This callback is triggered when the operation requested by the
        invocation of saLogWriteLogAsync() completes

        Args:
            invocation (SaInvocationT): Invocation associated with an
                invocation of saLogWriteLogAsync()
            error (SaAisErrorT): Return code of the log write operation
        """
        pass

    def __init__(self, exec_name=""):
        """ Constructor for SafLogger class

        Args:
            exec_name (str): User name
        """
        self.invocation = 0
        self.log_handle = saLog.SaLogHandleT()
        self.sel_obj = saAis.SaSelectionObjectT()
        self.callbacks = saLog.SaLogCallbacksT()
        self.stream_handle = saLog.SaLogStreamHandleT()
        self.record = saLog.SaLogRecordT()

        self.callbacks.saLogWriteLogCallback = \
            saLog.SaLogWriteLogCallbackT(self.write_log_callback)

        log.saLogInitialize(self.log_handle, self.callbacks, log.LOG_VERSION)
        log.saLogSelectionObjectGet(self.log_handle, self.sel_obj)

        stream_name = saAis.SaNameT(saLog.saLog.SA_LOG_STREAM_SYSTEM)
        log.saLogStreamOpen_2(self.log_handle, stream_name, None, 0,
                              saAis.saAis.SA_TIME_ONE_SECOND,
                              self.stream_handle)

        user_name = ctypes.pointer(saAis.SaNameT(exec_name))
        header_type = saLog.eSaLogHeaderTypeT.SA_LOG_GENERIC_HEADER
        self.record.logTimeStamp = saAis.saAis.SA_TIME_UNKNOWN
        self.record.logHdrType = header_type
        self.record.logHeader.genericHdr.logSvcUsrName = user_name

    def finalize(self):
        """ Close a log stream and finalize the log handle corresponding to
        the log stream
        """
        log.saLogStreamClose(self.stream_handle)
        log.saLogFinalize(self.log_handle)

    def get_invocation(self):
        """ Increase the invocation number and return it """
        self.invocation += 1
        return self.invocation

    def __del__(self):
        """ Destructor to automatically finalize log handle """
        self.finalize()

    def log(self, message):
        """ Write a log record to the system log stream

        Args:
            message (str): Message to write to system log stream
        """

        log_buffer = ctypes.pointer(saLog.SaLogBufferT(message))
        notice = saLog.saLog.SA_LOG_SEV_NOTICE
        self.record.logBuffer = log_buffer
        self.record.logHeader.genericHdr.logSeverity = notice
        log.saLogWriteLogAsync(self.stream_handle, self.get_invocation(),
                               saLog.saLog.SA_LOG_RECORD_WRITE_ACK,
                               self.record)

    def dispatch(self, read_fds):
        """ Invoke pending callbacks

        Args:
            read_fds (list): List of file descriptors associated with the
                log handle
        """
        if self.sel_obj.value in read_fds:
            log.saLogDispatch(self.log_handle,
                              saAis.eSaDispatchFlagsT.SA_DISPATCH_ALL)
