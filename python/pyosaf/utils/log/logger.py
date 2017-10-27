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
# pylint: disable=unused-argument
""" SAF Logger utility """
import ctypes
from copy import deepcopy
from select import select

from pyosaf.saAis import eSaAisErrorT, SaVersionT, SaSelectionObjectT, \
    eSaDispatchFlagsT, SaNameT, saAis, SaStringT
from pyosaf.saNtf import saNtf, eSaNtfEventTypeT, SaNtfClassIdT
from pyosaf.saLog import saLog, eSaLogFileFullActionT, \
    SaLogFileCreateAttributesT_2, eSaLogHeaderTypeT, SaLogNtfLogHeaderT, \
    SaLogGenericLogHeaderT, SaLogRecordT, SaLogCallbacksT, \
    SaLogWriteLogCallbackT, SaLogHandleT, SaLogStreamHandleT, SaLogHeaderT, \
    SaLogBufferT
from pyosaf.utils import log, bad_handle_retry, log_err

SYSTEM_STREAM = saLog.SA_LOG_STREAM_SYSTEM
NOTIF_STREAM = saLog.SA_LOG_STREAM_NOTIFICATION
ALARM_STREAM = saLog.SA_LOG_STREAM_ALARM


class LoggerInfo(object):
    """ This class encapsulates data structures used when opening a specific
    log stream, or writing a log record to a log stream """
    def __init__(self, service_user_name=""):
        self.stream_name = SYSTEM_STREAM
        # log file create attributes
        self.log_file_name = "saLogApplication"
        self.log_file_path_name = "."
        self.max_log_file_size = 5000000
        self.max_log_record_size = 150
        self.ha_property = True
        self.log_file_full_action = \
            eSaLogFileFullActionT.SA_LOG_FILE_FULL_ACTION_ROTATE
        self.max_files_rotated = 4
        self.log_file_format = "@Cr @Ch:@Cn:@Cs @Sv @Sl \"@Cb\""
        # notification header info
        self.event_type = eSaNtfEventTypeT.SA_NTF_ALARM_QOS
        self.notification_object = ""
        self.notifying_object = ""
        self.ntf_class_id = SaNtfClassIdT(0, 0, 0)
        self.event_time = saAis.SA_TIME_UNKNOWN
        # generic info
        self.log_severity = saLog.SA_LOG_SEV_INFO
        self.log_service_user_name = service_user_name


class SafLogger(object):
    """ This class provides logging function of the LOG service """
    def __init__(self, service_user_name="", version=None):
        """ Constructor for SafLogger class

        Args:
            service_user_name (str): Logger's service user name
        """
        self.init_version = version if version else SaVersionT('A', 2, 3)
        self.version = None
        self.invocation = 0
        self.log_handle = None
        self.stream_handle = None
        self.sel_obj = SaSelectionObjectT()
        self.record = SaLogRecordT()
        self.log_write_error = eSaAisErrorT.SA_AIS_OK
        self.callbacks = SaLogCallbacksT()
        self.callbacks.saLogWriteLogCallback = \
            SaLogWriteLogCallbackT(self._log_write_callback)
        self.logger_info = LoggerInfo(service_user_name)

    def __enter__(self):
        """ Enter method for SafLogger class """
        return self

    def __exit__(self, exception_type, exception_value, traceback):
        """ Exit method for SafLogger class """
        self._finalize()

    def __del__(self):
        """ Destructor for SafLogger class """
        self._finalize()

    def _log_write_callback(self, invocation, error):
        """ This callback is invoked by the LOG service to acknowledge a
        previous invocation of the saLogWriteLogAsync() function with the
        SA_LOG_RECORD_WRITE_ACK flag, and also provides the result of the
        respective log write operation.

        Args:
            invocation (SaInvocationT): Invocation associated with an
                invocation of saLogWriteLogAsync()
            error (SaAisErrorT): Result of the log write operation
        """
        self.log_write_error = error

    def _finalize(self):
        """ Finalize the log handle
        The log stream associated with the handle will be closed as a result.
        """
        if self.log_handle is not None:
            rc = log.saLogFinalize(self.log_handle)
            if rc != eSaAisErrorT.SA_AIS_OK:
                log_err("saLogFinalize FAILED - %s" %
                        eSaAisErrorT.whatis(rc))
            if rc == eSaAisErrorT.SA_AIS_OK or \
                    rc == eSaAisErrorT.SA_AIS_ERR_BAD_HANDLE:
                self.log_handle = None
                self.stream_handle = None

    @bad_handle_retry
    def init(self):
        """ Initialize the LOG agent library

        Returns:
            SaAisErrorT: Return code of the corresponding LOG API call(s)
        """
        # Clean up resources left from previous initialization (if any)
        self._finalize()

        self.log_handle = SaLogHandleT()
        self.version = deepcopy(self.init_version)

        rc = log.saLogInitialize(self.log_handle, self.callbacks, self.version)
        if rc != eSaAisErrorT.SA_AIS_OK:
            log_err("saLogInitialize FAILED - %s" %
                    eSaAisErrorT.whatis(rc))
        else:
            rc = log.saLogSelectionObjectGet(self.log_handle, self.sel_obj)
            if rc != eSaAisErrorT.SA_AIS_OK:
                log_err("saLogSelectionObjectGet FAILED - %s" %
                        eSaAisErrorT.whatis(rc))

        return rc

    @bad_handle_retry
    def open_stream(self):
        """ Open the log stream specified by the user

        Returns:
            SaAisErrorT: Return code of the corresponding LOG API call(s)
        """
        rc = eSaAisErrorT.SA_AIS_OK
        if self.stream_handle is not None:
            self.close_stream()
        if self.log_handle is None:
            rc = self.init()
        self.stream_handle = SaLogStreamHandleT()
        if rc == eSaAisErrorT.SA_AIS_OK:
            stream_name = SaNameT(self.logger_info.stream_name)
            create_attrs, open_flag = self._generate_log_stream_open_config()
            rc = log.saLogStreamOpen_2(self.log_handle, stream_name,
                                       create_attrs, open_flag,
                                       saAis.SA_TIME_ONE_SECOND,
                                       self.stream_handle)
            if rc != eSaAisErrorT.SA_AIS_OK:
                log_err("saLogStreamOpen_2 FAILED - %s" %
                        eSaAisErrorT.whatis(rc))
        if rc == eSaAisErrorT.SA_AIS_ERR_BAD_HANDLE:
            self.log_handle = None

        return rc

    def close_stream(self):
        """ Close the log stream associated with the handle

        Returns:
            SaAisErrorT: Return code of the corresponding LOG API call(s)
        """
        rc = log.saLogStreamClose(self.stream_handle)
        if rc != eSaAisErrorT.SA_AIS_OK:
            log_err("saLogStreamClose FAILED - %s" %
                    eSaAisErrorT.whatis(rc))
        if rc == eSaAisErrorT.SA_AIS_OK or \
                rc == eSaAisErrorT.SA_AIS_ERR_BAD_HANDLE:
            self.stream_handle = None

        return rc

    def _get_invocation(self):
        """ Generate and return a new unique invocation number for use

        Returns:
            int: The new invocation number
        """
        self.invocation += 1
        return self.invocation

    def _generate_log_stream_open_config(self):
        """ Configure the attributes and flag used to open (or create, if not
        yet existed) an application log stream

        If the stream to open is a config log stream, setting of attributes and
        flag is not allowed, and default values are enforced.

        Returns:
            SaLogFileCreateAttributesT_2: Log file create attributes
            SaLogStreamOpenFlagsT: Log stream open flag
        """
        # Return default values of 'None' for log file create attributes and
        # '0' for log stream open flag if the stream to open is a config stream
        if "safLgStrCfg" in self.logger_info.stream_name:
            return None, 0

        create_attrs = SaLogFileCreateAttributesT_2()
        create_attrs.logFileName = SaStringT(self.logger_info.log_file_name)
        create_attrs.logFilePathName = SaStringT(
            self.logger_info.log_file_path_name)
        create_attrs.maxLogFileSize = self.logger_info.max_log_file_size
        create_attrs.maxLogRecordSize = self.logger_info.max_log_record_size
        create_attrs.haProperty = self.logger_info.ha_property
        create_attrs.logFileFullAction = \
            self.logger_info.log_file_full_action
        create_attrs.maxFilesRotated = self.logger_info.max_files_rotated
        create_attrs.logFileFmt = SaStringT(self.logger_info.log_file_format)

        return create_attrs, saLog.SA_LOG_STREAM_CREATE

    def _generate_log_header(self):
        """ Fill in the header of the log record to be written with user-
        provided information

        Returns:
            SaLogHeaderTypeT: Log header type
            SaLogHeaderT: Log header structure filled with information
        """
        if self.logger_info.stream_name == ALARM_STREAM or \
                self.logger_info.stream_name == NOTIF_STREAM:
            header_type = eSaLogHeaderTypeT.SA_LOG_NTF_HEADER
            ntf_header = SaLogNtfLogHeaderT()
            ntf_header.notificationId = saNtf.SA_NTF_IDENTIFIER_UNUSED
            ntf_header.eventType = self.logger_info.event_type
            ntf_header.notificationObject.contents = SaNameT(
                self.logger_info.notification_object)
            ntf_header.notifyingObject.contents = SaNameT(
                self.logger_info.notifying_object)
            if self.logger_info.ntf_class_id:
                ntf_header.notificationClassId.contents = \
                    self.logger_info.ntf_class_id
            ntf_header.eventTime = self.logger_info.event_time
            log_header = SaLogHeaderT()
            log_header.ntfHdr = ntf_header
        else:
            header_type = eSaLogHeaderTypeT.SA_LOG_GENERIC_HEADER
            generic_header = SaLogGenericLogHeaderT()
            if self.logger_info.ntf_class_id:
                generic_header.notificationClassId.contents = \
                    self.logger_info.ntf_class_id
            generic_header.logSvcUsrName.contents = \
                SaNameT(self.logger_info.log_service_user_name)
            generic_header.logSeverity = self.logger_info.log_severity
            log_header = SaLogHeaderT()
            log_header.genericHdr = generic_header
        return header_type, log_header

    def set_log_stream_name(self, stream_name):
        """ Set the name of the log stream to open/create

        Args:
            stream_name (str): DN of the log stream; must be one of the below:
                1) saLog.SA_LOG_STREAM_SYSTEM
                2) saLog.SA_LOG_STREAM_NOTIFICATION
                3) saLog.SA_LOG_STREAM_ALARM
                4) User-defined application stream name with DN format:
                   safLgStr=<stream_name>,safApp=safLogService
        """
        self.logger_info.stream_name = stream_name

    def set_log_file_name(self, file_name):
        """ Set the logFileName field of SaLogFileCreateAttributesT_2

        Args:
            file_name (str): Log file name
        """
        self.logger_info.log_file_name = file_name

    def set_log_file_path_name(self, path_name):
        """ Set the logFilePathName field of SaLogFileCreateAttributesT_2

        Args:
            path_name (str): Log file path name
        """
        self.logger_info.log_file_path_name = path_name

    def set_max_log_file_size(self, max_file_size):
        """ Set the maxLogFileSize field of SaLogFileCreateAttributesT_2

        Args:
            max_file_size (int): Max log file size in bytes
        """
        self.logger_info.max_log_file_size = max_file_size

    def set_max_log_record_size(self, max_record_size):
        """ Set the maxLogRecordSize field of SaLogFileCreateAttributesT_2

        Args:
            max_record_size (int): Max log record size in bytes
        """
        self.logger_info.max_log_file_size = max_record_size

    def set_ha_property(self, ha_property):
        """ Set the haProperty field of SaLogFileCreateAttributesT_2

        Args:
            ha_property (bool): HA property of the log file
        """
        self.logger_info.ha_property = ha_property

    def set_log_file_full_action(self, file_full_action):
        """ Set the logFileFullAction field of SaLogFileCreateAttributesT_2

        Args:
            file_full_action (SaLogFileFullActionT): Log file-full action
        """
        self.logger_info.log_file_full_action = file_full_action

    def set_max_files_rotated(self, max_files_rotated):
        """ Set the maxFilesRotated field of SaLogFileCreateAttributesT_2

        Args:
            max_files_rotated (int): Max number of rotated log files
        """
        self.logger_info.max_files_rotated = max_files_rotated

    def set_log_file_fmt(self, log_file_format):
        """ Set the logFileFmt field of SaLogFileCreateAttributesT_2

        Args:
            log_file_format (str): Log record format expression
        """
        self.logger_info.log_file_format = log_file_format

    def set_event_type(self, event_type):
        """ Set the eventType field of SaLogNtfLogHeaderT

        Args:
            event_type (SaNtfEventTypeT): Alarm/Notification log event type
        """
        self.logger_info.event_type = event_type

    def set_notification_object(self, notification_object):
        """ Set the notificationObject field of SaLogNtfLogHeaderT

        Args:
            notification_object (str): Notification object's dn
        """
        self.logger_info.notification_object = notification_object

    def set_notifying_object(self, notifying_object):
        """ Set the notifyingObject field of SaLogNtfLogHeaderT

        Args:
            notifying_object (str): Notifying object's dn
        """
        self.logger_info.notifying_object = notifying_object

    def set_ntf_class_id(self, ntf_class_id):
        """ Set the notificationClassId field of SaLogNtfLogHeaderT and
        SaLogGenericLogHeaderT

        Args:
            ntf_class_id (SaNtfClassIdT): Notification class id
        """
        self.logger_info.ntf_class_id = ntf_class_id

    def set_ntf_event_time(self, event_time):
        """ Set the eventTime field of SaLogNtfLogHeaderT

        Args:
            event_time (SaTimeT): Alarm/Notification log event time
        """
        self.logger_info.event_time = event_time

    def set_log_severity(self, log_severity):
        """ Set the logSeverity field of SaLogGenericLogHeaderT

        Args:
            log_severity (SaLogSeverityT): Log severity
        """
        self.logger_info.log_severity = log_severity

    def set_service_user_name(self, service_user_name):
        """ Set the logSvcUsrName field of SaLogGenericLogHeaderT

        Args:
            service_user_name (str): Logger's service user name
        """
        self.logger_info.log_service_user_name = service_user_name

    @bad_handle_retry
    def log(self, message):
        """ Write a message (log record) to a log stream

        Args:
            message (str): Message to write to the log stream

        Returns:
            SaAisErrorT: Return code of the corresponding LOG API call(s)
        """
        header_type, log_header = self._generate_log_header()
        self.record.logTimeStamp = saAis.SA_TIME_UNKNOWN
        self.record.logHdrType = header_type
        self.record.logHeader = log_header

        log_buffer = ctypes.pointer(SaLogBufferT(message))
        self.record.logBuffer = log_buffer
        rc = log.saLogWriteLogAsync(self.stream_handle,
                                    self._get_invocation(),
                                    saLog.SA_LOG_RECORD_WRITE_ACK,
                                    self.record)
        if rc != eSaAisErrorT.SA_AIS_OK:
            log_err("saLogWriteLogAsync FAILED - %s" %
                    eSaAisErrorT.whatis(rc))
        else:
            fds = [self.sel_obj.value]
            io_evt = select(fds, [], [], 10)
            if self.sel_obj.value in io_evt[0]:
                rc = log.saLogDispatch(self.log_handle,
                                       eSaDispatchFlagsT.SA_DISPATCH_ALL)
                if rc != eSaAisErrorT.SA_AIS_OK:
                    log_err("saLogDispatch FAILED - %s" %
                            eSaAisErrorT.whatis(rc))
            else:
                self.log_write_error = eSaAisErrorT.SA_AIS_ERR_TIMEOUT

        if rc == eSaAisErrorT.SA_AIS_OK and self.log_write_error == \
                eSaAisErrorT.SA_AIS_ERR_BAD_HANDLE:
            rc = eSaAisErrorT.SA_AIS_ERR_BAD_HANDLE

        if rc == eSaAisErrorT.SA_AIS_ERR_BAD_HANDLE:
            init_rc = self.init()
            # If the re-initialization of agent handle succeeds, we still need
            # to return BAD_HANDLE to the function decorator, so that it would
            # re-try the failed operation. Otherwise, the true error code is
            # returned to the user to decide further actions.
            if init_rc != eSaAisErrorT.SA_AIS_OK:
                rc = init_rc

        return rc

    def get_log_write_result(self):
        """ Return the result of the log write operation to the user

        Returns:
            SaAisErrorT: Result of the log write operation
        """
        return self.log_write_error
