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
# pylint: disable=too-many-arguments
""" NTF common utilities """
from pyosaf.saAis import saAis, eSaAisErrorT, SaVersionT, eSaDispatchFlagsT
from pyosaf import saNtf
from pyosaf.utils import deprecate, SafException
from pyosaf.utils.ntf import agent as ntf
from pyosaf.utils.ntf.producer import NtfProducer
from pyosaf.utils.ntf.subscriber import NtfSubscriber

_ntf_producer = None
_ntf_subscriber = None


@deprecate
def initialize(notification_callback=None):
    """ Initialize the NTF library

    Args:
        notification_callback (SaNtfNotificationCallbackT): Callback to be
            invoked by NTF server to deliver a notification to the subscriber

    Raises:
        SafException: If any NTF API call did not return SA_AIS_OK
    """
    # Set the NTF API version to initialize
    version = SaVersionT('A', 1, 1)

    global _ntf_producer
    _ntf_producer = NtfProducer(version)

    global _ntf_subscriber
    _ntf_subscriber = NtfSubscriber(version)

    rc = _ntf_producer.init()
    if rc != eSaAisErrorT.SA_AIS_OK:
        raise SafException(rc)

    rc = _ntf_subscriber.init(notification_callback)
    if rc != eSaAisErrorT.SA_AIS_OK:
        raise SafException(rc)


@deprecate
def send_object_create_notification(vendor_id, major_id, minor_id,
                                    additional_text="",
                                    notification_object="",
                                    notifying_object="",
                                    attributes=None,
                                    event_time=saAis.SA_TIME_UNKNOWN,
                                    additional_info=None):
    """ Send notification about an object creation event

    Args:
        vendor_id (SaUint32T): Vendor id
        major_id (SaUint16T): Major id
        minor_id (SaUint16T): Minor id
        additional_text (str): Additional text
        notification_object (str): Notification object's dn
        notifying_object (str): Notifying object's dn
        attributes (list(Attribute)): List of Attribute structures
        event_time (SaTimeT): Event time
        additional_info (list(AdditionalInfo)): List of AdditionalInfo
            structures

    Returns:
        SaAisErrorT: Return code of the corresponding NTF API call(s)

    Raises:
        SafException: If any NTF API call did not return SA_AIS_OK
    """
    ntf_class_id = saNtf.SaNtfClassIdT(vendor_id, major_id, minor_id)
    if _ntf_producer is None:
        # SA_AIS_ERR_INIT is returned if user calls this function without first
        # calling initialize()
        return eSaAisErrorT.SA_AIS_ERR_INIT

    _ntf_producer.set_event_type(saNtf.eSaNtfEventTypeT.SA_NTF_OBJECT_CREATION)
    _ntf_producer.set_class_id(ntf_class_id)
    _ntf_producer.set_additional_text(additional_text)
    _ntf_producer.set_notification_object(notification_object)
    _ntf_producer.set_notifying_object(notifying_object)
    if attributes:
        _ntf_producer.set_object_attributes(attributes)
    _ntf_producer.set_event_time(event_time)
    if additional_info:
        _ntf_producer.set_additional_info(additional_info)

    rc = _ntf_producer.send_object_create_delete_notification()
    if rc != eSaAisErrorT.SA_AIS_OK:
        raise SafException(rc)

    # Clear the internally saved notification information
    _ntf_producer.clear_info()

    return rc


@deprecate
def send_object_delete_notification(vendor_id, major_id, minor_id,
                                    additional_text="",
                                    notification_object="",
                                    notifying_object="",
                                    attributes=None,
                                    event_time=saAis.SA_TIME_UNKNOWN,
                                    additional_info=None):
    """ Send notification about an object deletion event

    Args:
        vendor_id (SaUint32T): Vendor id
        major_id (SaUint16T): Major id
        minor_id (SaUint16T): Minor id
        additional_text (str): Additional text
        notification_object (str): Notification object's dn
        notifying_object (str): Notifying object's dn
        attributes (list(Attribute)): List of Attribute structures
        event_time (SaTimeT): Event time
        additional_info (list(AdditionalInfo)): List of AdditionalInfo
            structures

    Returns:
        SaAisErrorT: Return code of the corresponding NTF API call(s)

    Raises:
        SafException: If any NTF API call did not return SA_AIS_OK
    """
    ntf_class_id = saNtf.SaNtfClassIdT(vendor_id, major_id, minor_id)
    if _ntf_producer is None:
        # SA_AIS_ERR_INIT is returned if user calls this function without first
        # calling initialize()
        return eSaAisErrorT.SA_AIS_ERR_INIT

    _ntf_producer.set_event_type(saNtf.eSaNtfEventTypeT.SA_NTF_OBJECT_DELETION)
    _ntf_producer.set_class_id(ntf_class_id)
    _ntf_producer.set_additional_text(additional_text)
    _ntf_producer.set_notification_object(notification_object)
    _ntf_producer.set_notifying_object(notifying_object)
    if attributes:
        _ntf_producer.set_object_attributes(attributes)
    _ntf_producer.set_event_time(event_time)
    if additional_info:
        _ntf_producer.set_additional_info(additional_info)

    rc = _ntf_producer.send_object_create_delete_notification()
    if rc != eSaAisErrorT.SA_AIS_OK:
        raise SafException(rc)

    # Clear the internally saved notification information
    _ntf_producer.clear_info()

    return rc


@deprecate
def send_attribute_change_notification(
        vendor_id, major_id, minor_id, additional_text="",
        notification_object="", notifying_object="",
        event_type=saNtf.eSaNtfEventTypeT.SA_NTF_ATTRIBUTE_ADDED,
        event_time=saAis.SA_TIME_UNKNOWN, additional_info=None,
        changed_attributes=None):
    """ Send notification about an attribute change event

    Args:
        vendor_id (SaUint32T): Vendor id
        major_id (SaUint16T): Major id
        minor_id (SaUint16T): Minor id
        additional_text (str): Additional text
        notification_object (str): Notification object's dn
        notifying_object (str): Notifying object's dn
        event_type (SaNtfEventTypeT): Event type
        event_time (SaTimeT): Event time
        additional_info (list(AdditionalInfo)): List of AdditionalInfo
            structures
        changed_attributes (list(AttributeChange)): List of AttributeChange
            structures

    Returns:
        SaAisErrorT: Return code of the corresponding NTF API call(s)

    Raises:
        SafException: If any NTF API call did not return SA_AIS_OK
    """
    ntf_class_id = saNtf.SaNtfClassIdT(vendor_id, major_id, minor_id)
    if _ntf_producer is None:
        # SA_AIS_ERR_INIT is returned if user calls this function without first
        # calling initialize()
        return eSaAisErrorT.SA_AIS_ERR_INIT

    _ntf_producer.set_event_type(event_type)
    _ntf_producer.set_class_id(ntf_class_id)
    _ntf_producer.set_additional_text(additional_text)
    _ntf_producer.set_notification_object(notification_object)
    _ntf_producer.set_notifying_object(notifying_object)
    _ntf_producer.set_event_time(event_time)
    if additional_info:
        _ntf_producer.set_additional_info(additional_info)
    if changed_attributes:
        _ntf_producer.set_changed_attributes(changed_attributes)

    rc = _ntf_producer.send_attribute_change_notification()
    if rc != eSaAisErrorT.SA_AIS_OK:
        raise SafException(rc)

    # Clear the internally saved notification information
    _ntf_producer.clear_info()

    return rc


@deprecate
def send_state_change_notification(vendor_id,
                                   major_id,
                                   minor_id,
                                   additional_text="",
                                   notification_object="",
                                   notifying_object="",
                                   event_time=saAis.SA_TIME_UNKNOWN,
                                   additional_info=None,
                                   state_changes=None):
    """ Send notification about a state change event

    Args:
        vendor_id (SaUint32T): Vendor id
        major_id (SaUint16T): Major id
        minor_id (SaUint16T): Minor id
        additional_text (str): Additional text
        notification_object (str): Notification object's dn
        notifying_object (str): Notifying object's dn
        event_time (SaTimeT): Event time
        additional_info (list(AdditionalInfo)): List of AdditionalInfo
            structures
        state_changes (list(StateChange)): List of StateChange structures

    Returns:
        SaAisErrorT: Return code of the corresponding NTF API call(s)

    Raises:
        SafException: If any NTF API call did not return SA_AIS_OK
    """
    ntf_class_id = saNtf.SaNtfClassIdT(vendor_id, major_id, minor_id)
    if _ntf_producer is None:
        # SA_AIS_ERR_INIT is returned if user calls this function without first
        # calling initialize()
        return eSaAisErrorT.SA_AIS_ERR_INIT

    _ntf_producer.set_event_type(
        saNtf.eSaNtfEventTypeT.SA_NTF_OBJECT_STATE_CHANGE)
    _ntf_producer.set_class_id(ntf_class_id)
    _ntf_producer.set_additional_text(additional_text)
    _ntf_producer.set_notification_object(notification_object)
    _ntf_producer.set_notifying_object(notifying_object)
    _ntf_producer.set_event_time(event_time)
    if additional_info:
        _ntf_producer.set_additional_info(additional_info)
    if state_changes:
        _ntf_producer.set_state_changes(state_changes)

    rc = _ntf_producer.send_state_change_notification()
    if rc != eSaAisErrorT.SA_AIS_OK:
        raise SafException(rc)

    # Clear the internally saved notification information
    _ntf_producer.clear_info()

    return rc


@deprecate
def send_alarm_notification(
        vendor_id, major_id, minor_id, perceived_severity, additional_text="",
        notification_object="", notifying_object="",
        event_type=saNtf.eSaNtfEventTypeT.SA_NTF_ALARM_PROCESSING,
        event_time=saAis.SA_TIME_UNKNOWN, additional_info=None):
    """ Send an alarm notification

    Args:
        vendor_id (SaUint32T): Vendor id
        major_id (SaUint16T): Major id
        minor_id (SaUint16T): Minor id
        perceived_severity (SaNtfSeverityT): Perceived severity
        additional_text (str): Additional text
        notification_object (str): Notification object's dn
        notifying_object (str): Notifying object's dn
        event_type (SaNtfEventTypeT): Event type
        event_time (SaTimeT): Event time
        additional_info (list(AdditionalInfo)): List of AdditionalInfo
            structures

    Returns:
        SaAisErrorT: Return code of the corresponding NTF API call(s)

    Raises:
        SafException: If any NTF API call did not return SA_AIS_OK
    """
    ntf_class_id = saNtf.SaNtfClassIdT(vendor_id, major_id, minor_id)
    if _ntf_producer is None:
        # SA_AIS_ERR_INIT is returned if user calls this function without first
        # calling initialize()
        return eSaAisErrorT.SA_AIS_ERR_INIT

    _ntf_producer.set_event_type(event_type)
    _ntf_producer.set_class_id(ntf_class_id)
    _ntf_producer.set_additional_text(additional_text)
    _ntf_producer.set_notification_object(notification_object)
    _ntf_producer.set_notifying_object(notifying_object)
    _ntf_producer.set_event_time(event_time)
    _ntf_producer.set_perceived_severity(perceived_severity)
    if additional_info:
        _ntf_producer.set_additional_info(additional_info)

    rc = _ntf_producer.send_alarm_notification()
    if rc != eSaAisErrorT.SA_AIS_OK:
        raise SafException(rc)

    # Clear the internally saved notification information
    _ntf_producer.clear_info()

    return rc


@deprecate
def send_security_alarm_notification(
        vendor_id, major_id, minor_id, severity, alarm_detector, user,
        provider, additional_text="", notification_object="",
        notifying_object="",
        event_type=saNtf.eSaNtfEventTypeT.SA_NTF_INTEGRITY_VIOLATION,
        event_time=saAis.SA_TIME_UNKNOWN, additional_info=None,
        probable_cause=saNtf.eSaNtfProbableCauseT.SA_NTF_SOFTWARE_ERROR):

    """ Send a security alarm notification

    Args:
        vendor_id (SaUint32T): Vendor id
        major_id (SaUint16T): Major id
        minor_id (SaUint16T): Minor id
        severity (SaNtfSeverityT): Severity
        alarm_detector (SecurityAlarmDetector): SecurityAlarmDetector
            information structure
        user (ServiceUser): ServiceUser information structure
        provider (ServiceProvider): ServiceProvider information structure
        additional_text (str): Additional text
        notification_object (str): Notification object's dn
        notifying_object (str): Notifying object's dn
        event_type (SaNtfEventTypeT): Event type
        event_time (SaTimeT): Event time
        additional_info (list(AdditionalInfo)): List of AdditionalInfo
            structures
        probable_cause (SaNtfProbableCauseT): Probable cause

    Returns:
        SaAisErrorT: Return code of the corresponding NTF API call(s)

    Raises:
        SafException: If any NTF API call did not return SA_AIS_OK
    """
    ntf_class_id = saNtf.SaNtfClassIdT(vendor_id, major_id, minor_id)
    if _ntf_producer is None:
        # SA_AIS_ERR_INIT is returned if user calls this function without first
        # calling initialize()
        return eSaAisErrorT.SA_AIS_ERR_INIT

    _ntf_producer.set_event_type(event_type)
    _ntf_producer.set_class_id(ntf_class_id)
    _ntf_producer.set_additional_text(additional_text)
    _ntf_producer.set_notification_object(notification_object)
    _ntf_producer.set_notifying_object(notifying_object)
    _ntf_producer.set_event_time(event_time)
    if additional_info:
        _ntf_producer.set_additional_info(additional_info)
    _ntf_producer.set_severity(severity)
    _ntf_producer.set_probable_cause(probable_cause)
    _ntf_producer.set_security_alarm_detector(alarm_detector)
    _ntf_producer.set_service_user(user)
    _ntf_producer.set_service_provider(provider)

    rc = _ntf_producer.send_security_alarm_notification()
    if rc != eSaAisErrorT.SA_AIS_OK:
        raise SafException(rc)

    # Clear the internally saved notification information
    _ntf_producer.clear_info()

    return rc


@deprecate
def subscribe_for_notifications(notification_types=None):
    """ Subscribe for notifications from NTF with the types specified in the
    notification_types list
    If the list is not provided, all types of notification are subscribed to
    by default.

    Args:
        notification_types (list(SaNtfNotificationTypeT)): List of
            notification types

    Returns:
        SaAisErrorT: Return code of the corresponding NTF API call(s)

    Raises:
        SafException: If any NTF API call did not return SA_AIS_OK
    """
    rc = _ntf_subscriber.subscribe(1, notification_types)
    if rc != eSaAisErrorT.SA_AIS_OK:
        raise SafException(rc)

    return rc


@deprecate
def get_producer_handle():
    """ Get the NTF producer agent handle

    Returns:
        SaNtfHandleT: NTF producer agent handle
    """
    return _ntf_producer.get_handle()


@deprecate
def get_subscriber_handle():
    """ Get the NTF subscriber agent handle

    Returns:
        SaNtfHandleT: NTF subscriber agent handle
    """
    return _ntf_subscriber.get_handle()


@deprecate
def get_subscriber_selection_object():
    """ Get the selection object associated with the subscriber agent handle

    Returns:
        SaSelectionObjectT: The NTF subscriber selection object
    """
    return _ntf_subscriber.get_selection_object()


@deprecate
def dispatch(flags=eSaDispatchFlagsT.SA_DISPATCH_ALL):
    """ Invoke NTF callbacks for queued events. The default is to dispatch all
    available events.

    Args:
        flags (eSaDispatchFlagsT): Flags specifying dispatch mode

    Returns:
        SaAisErrorT: Return code of the corresponding NTF API call(s)

    Raises:
        SafException: If any NTF API call did not return SA_AIS_OK
    """
    rc = _ntf_subscriber.dispatch(flags)
    if rc != eSaAisErrorT.SA_AIS_OK:
        raise SafException(rc)

    return rc
