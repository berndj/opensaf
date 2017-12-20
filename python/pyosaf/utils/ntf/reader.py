############################################################################
#
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
""" NTF reader utilities """
from collections import Iterator
from pyosaf import saNtf
from pyosaf.saAis import eSaAisErrorT
from pyosaf.utils import log_err, bad_handle_retry
from pyosaf.utils.ntf import agent as ntf
from pyosaf.utils.ntf.subscriber import NtfConsumer


class NtfReader(NtfConsumer, Iterator):
    """ This class provides functions of the NTF Reader interface """

    def __init__(self, version=None):
        """ Constructor for NtfReader class """
        super(NtfReader, self).__init__(version)
        self.read_handle = None
        self.filter_handles = \
            saNtf.SaNtfNotificationTypeFilterHandlesT(0, 0, 0, 0, 0)
        self.search_direction = \
            saNtf.eSaNtfSearchDirectionT.SA_NTF_SEARCH_OLDER
        self.search_criteria = saNtf.SaNtfSearchCriteriaT(
            saNtf.eSaNtfSearchModeT.SA_NTF_SEARCH_ONLY_FILTER, 0, 0)

    def __enter__(self):
        """ Enter method for NtfReader class """
        return self

    def __exit__(self, exception_type, exception_value, traceback):
        """ Exit method for NtfReader class

        Finalize the NTF agent handle and clean up any allocated resources
        """
        if self.read_handle is not None:
            ntf.saNtfNotificationReadFinalize(self.read_handle)
            self.read_handle = None
        if self.handle is not None:
            ntf.saNtfFinalize(self.handle)
            self.handle = None

    def __del__(self):
        """ Destructor for NtfReader class

        Finalize the NTF agent handle and clean up any allocated resources
        """
        if self.read_handle is not None:
            saNtf.saNtfNotificationReadFinalize(self.read_handle)
            self.read_handle = None
        if self.handle is not None:
            saNtf.saNtfFinalize(self.handle)
            self.handle = None

    def __iter__(self):
        """ Iterator for NtfReader class """
        return self

    def next(self):
        """ Override next() method of Iterator class """
        return self.__next__()

    def __next__(self):
        """ Return the next element of the class iterator """
        notification = saNtf.SaNtfNotificationsT()
        rc = ntf.saNtfNotificationReadNext(self.read_handle,
                                           self.search_direction,
                                           notification)
        if rc != eSaAisErrorT.SA_AIS_OK:
            if rc != eSaAisErrorT.SA_AIS_ERR_NOT_EXIST:
                log_err("saNtfNotificationReadNext FAILED - %s" %
                        eSaAisErrorT.whatis(rc))
            raise StopIteration

        return self._parse_notification(notification)

    def init(self):
        """ Initialize the NTF agent

        Returns:
            SaAisErrorT: Return code of the corresponding NTF API call(s)
        """
        self.finalize()
        return self.initialize()

    def _parse_notification(self, notification):
        """ Parse the read-out notification to retrieve its information

        Args:
            notification (SaNtfNotificationsT): Notification to parse

        Returns:
            SaNtfNotificationTypeT: Notification type
            NotificationInfo: A NotificationInfo structure containing
                information of the parsed notification
        """
        _ntf_type = notification.notificationType
        if _ntf_type == saNtf.eSaNtfNotificationTypeT.SA_NTF_TYPE_ALARM:
            notification = notification.notification.alarmNotification
            _ntf_info = self._parse_notification_header(
                notification.notificationHandle,
                notification.notificationHeader)
            self._parse_alarm_ntf(notification, _ntf_info)
        else:
            notification = notification.notification.securityAlarmNotification
            _ntf_info = self._parse_notification_header(
                notification.notificationHandle,
                notification.notificationHeader)
            self._parse_security_alarm_ntf(notification, _ntf_info)

        return _ntf_type, _ntf_info

    def set_search_criteria(self, search_criteria):
        """ Set the notification search criteria

        Args:
            search_criteria (SaNtfSearchCriteriaT): Search criteria
        """
        self.search_criteria = search_criteria

    def set_search_direction(self, search_direction):
        """ Set the notification search direction

        Args:
            search_direction (SaNtfSearchDirectionT): Search direction
        """
        self.search_direction = search_direction

    @bad_handle_retry
    def read(self, notification_types=None):
        """ Start reading NTF notifications with the types specified in the
        notification_types list. If the list is not provided, all types of
        notification are read by default.

        Current NTF implementation of this function only supports read filters
        for alarm and security alarm notification types.

        NOTE: Users have to set the wanted filters criteria via the set of
        set_filter_*() methods before calling this method. Otherwise, the
        filters criteria will be set with default values.

        Args:
            notification_types (list(SaNtfNotificationTypeT)): List of
                notification types

        Returns:
            SaAisErrorT: Return code of the corresponding NTF API call(s)
        """
        alarm_type = saNtf.eSaNtfNotificationTypeT.SA_NTF_TYPE_ALARM
        security_alarm_type = \
            saNtf.eSaNtfNotificationTypeT.SA_NTF_TYPE_SECURITY_ALARM

        # Filters for notification types other than SA_NTF_TYPE_ALARM and
        # SA_NTF_TYPE_SECURITY_ALARM are not supported
        if notification_types is not None:
            if alarm_type not in notification_types \
                    and security_alarm_type not in notification_types:
                return eSaAisErrorT.SA_AIS_ERR_NOT_SUPPORTED

        rc = eSaAisErrorT.SA_AIS_OK
        # Generate the alarm notification filter
        if notification_types is None or alarm_type in notification_types:
            rc = self._generate_alarm_filter()

        # Generate the security alarm notification filter
        if rc == eSaAisErrorT.SA_AIS_OK:
            if notification_types is None \
                    or security_alarm_type in notification_types:
                rc = self._generate_security_alarm_filter()

        if rc == eSaAisErrorT.SA_AIS_OK:
            self.read_handle = saNtf.SaNtfReadHandleT()
            rc = ntf.saNtfNotificationReadInitialize(self.search_criteria,
                                                     self.filter_handles,
                                                     self.read_handle)
        if rc == eSaAisErrorT.SA_AIS_ERR_BAD_HANDLE:
            init_rc = self.init()
            # If the re-initialization of agent handle succeeds, we still need
            # to return BAD_HANDLE to the function decorator, so that it would
            # re-try the failed operation. Otherwise, the true error code is
            # returned to the user to decide further actions.
            if init_rc != eSaAisErrorT.SA_AIS_OK:
                rc = init_rc

        return rc
