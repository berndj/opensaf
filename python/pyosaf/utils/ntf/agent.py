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
# pylint: disable=unused-argument,too-few-public-methods
""" NTF utils common data structures and functions """
from copy import deepcopy

from pyosaf.saAis import saAis, SaVersionT, SaSelectionObjectT, eSaAisErrorT
from pyosaf import saNtf
from pyosaf.utils import decorate, initialize_decorate, log_err

# Decorate pure saNtf* API's with error-handling retry and exception raising
# Library Life Cycle API's
saNtfInitialize = initialize_decorate(saNtf.saNtfInitialize)
saNtfInitialize_2 = initialize_decorate(saNtf.saNtfInitialize_2)
saNtfInitialize_3 = initialize_decorate(saNtf.saNtfInitialize_3)
saNtfSelectionObjectGet = decorate(saNtf.saNtfSelectionObjectGet)
saNtfDispatch = decorate(saNtf.saNtfDispatch)
saNtfFinalize = decorate(saNtf.saNtfFinalize)
# Producer API's
saNtfObjectCreateDeleteNotificationAllocate = \
    decorate(saNtf.saNtfObjectCreateDeleteNotificationAllocate)
saNtfAttributeChangeNotificationAllocate = \
    decorate(saNtf.saNtfAttributeChangeNotificationAllocate)
saNtfStateChangeNotificationAllocate = \
    decorate(saNtf.saNtfStateChangeNotificationAllocate)
saNtfStateChangeNotificationAllocate_3 = \
    decorate(saNtf.saNtfStateChangeNotificationAllocate_3)
saNtfAlarmNotificationAllocate = decorate(saNtf.saNtfAlarmNotificationAllocate)
saNtfSecurityAlarmNotificationAllocate = \
    decorate(saNtf.saNtfSecurityAlarmNotificationAllocate)
saNtfMiscellaneousNotificationAllocate = \
    decorate(saNtf.saNtfMiscellaneousNotificationAllocate)
saNtfPtrValAllocate = decorate(saNtf.saNtfPtrValAllocate)
saNtfArrayValAllocate = decorate(saNtf.saNtfArrayValAllocate)
saNtfIdentifierAllocate = decorate(saNtf.saNtfIdentifierAllocate)
saNtfNotificationSend = decorate(saNtf.saNtfNotificationSend)
saNtfNotificationSendWithId = decorate(saNtf.saNtfNotificationSendWithId)
saNtfNotificationFree = decorate(saNtf.saNtfNotificationFree)
saNtfVariableDataSizeGet = decorate(saNtf.saNtfVariableDataSizeGet)
# Consumer API's
saNtfLocalizedMessageGet = decorate(saNtf.saNtfLocalizedMessageGet)
saNtfLocalizedMessageFree = decorate(saNtf.saNtfLocalizedMessageFree)
saNtfLocalizedMessageFree_2 = decorate(saNtf.saNtfLocalizedMessageFree_2)
saNtfPtrValGet = decorate(saNtf.saNtfPtrValGet)
saNtfArrayValGet = decorate(saNtf.saNtfArrayValGet)
saNtfObjectCreateDeleteNotificationFilterAllocate = \
    decorate(saNtf.saNtfObjectCreateDeleteNotificationFilterAllocate)
saNtfAttributeChangeNotificationFilterAllocate = \
    decorate(saNtf.saNtfAttributeChangeNotificationFilterAllocate)
saNtfStateChangeNotificationFilterAllocate = \
    decorate(saNtf.saNtfStateChangeNotificationFilterAllocate)
saNtfStateChangeNotificationFilterAllocate_2 = \
    decorate(saNtf.saNtfStateChangeNotificationFilterAllocate_2)
saNtfAlarmNotificationFilterAllocate = \
    decorate(saNtf.saNtfAlarmNotificationFilterAllocate)
saNtfSecurityAlarmNotificationFilterAllocate = \
    decorate(saNtf.saNtfSecurityAlarmNotificationFilterAllocate)
saNtfNotificationFilterFree = decorate(saNtf.saNtfNotificationFilterFree)
# Subscriber API's
saNtfNotificationSubscribe = decorate(saNtf.saNtfNotificationSubscribe)
saNtfNotificationSubscribe_3 = decorate(saNtf.saNtfNotificationSubscribe_3)
saNtfNotificationUnsubscribe = decorate(saNtf.saNtfNotificationUnsubscribe)
saNtfNotificationUnsubscribe_2 = decorate(saNtf.saNtfNotificationUnsubscribe_2)
# Reader API's
saNtfNotificationReadInitialize = \
    decorate(saNtf.saNtfNotificationReadInitialize)
saNtfNotificationReadInitialize_2 = \
    decorate(saNtf.saNtfNotificationReadInitialize_2)
saNtfNotificationReadInitialize_3 = \
    decorate(saNtf.saNtfNotificationReadInitialize_3)
saNtfNotificationReadNext = decorate(saNtf.saNtfNotificationReadNext)
saNtfNotificationReadNext_3 = decorate(saNtf.saNtfNotificationReadNext_3)
saNtfNotificationReadFinalize = decorate(saNtf.saNtfNotificationReadFinalize)


class NotificationInfo(object):
    """ This class encapsulates data structures for use by each specific
    notification type """
    def __init__(self):
        # Header info
        self.event_type = 0
        self.notification_object = ""
        self.notifying_object = ""
        self.ntf_class_id = saNtf.SaNtfClassIdT(0, 0, 0)
        self.event_time = saAis.SA_TIME_UNKNOWN
        self.notification_id = None
        self.additional_text = ""
        self.additional_info = []
        # Object create/delete notification info
        self.object_attributes = []
        self.source_indicator = \
            saNtf.eSaNtfSourceIndicatorT.SA_NTF_UNKNOWN_OPERATION
        # Attribute change notification info
        self.changed_attributes = []
        # State change notification info
        self.state_changes = []
        # Alarm info
        self.probable_cause = \
            saNtf.eSaNtfProbableCauseT.SA_NTF_UNSPECIFIED_REASON
        self.specific_problems = []
        self.perceived_severity = \
            saNtf.eSaNtfSeverityT.SA_NTF_SEVERITY_INDETERMINATE
        self.trend = saNtf.eSaNtfSeverityTrendT.SA_NTF_TREND_NO_CHANGE
        self.threshold_information = None
        self.monitored_attrs = []
        self.proposed_repair_actions = []
        # Security alarm info
        self.severity = saNtf.eSaNtfSeverityT.SA_NTF_SEVERITY_INDETERMINATE
        self.security_alarm_detector = None
        self.service_user = None
        self.service_provider = None


class NotificationFilterInfo(object):
    """ This class encapsulates the notification filter data structure for use
    by each specific notification type """
    def __init__(self):
        # Header info
        self.obj_create_del_evt_list = []
        self.attr_change_evt_list = []
        self.state_change_evt_list = []
        self.alarm_evt_list = []
        self.sec_alarm_evt_list = []
        self.notification_object_list = []
        self.notifying_objects_list = []
        self.ntf_class_id_list = []
        # Filter info
        self.source_indicator_list = []
        self.changed_state_list = []
        self.probable_cause_list = []
        self.perceived_severity_list = []
        self.trend_list = []
        self.severity_list = []


class AdditionalInfo(object):
    """ This class contains a piece of additional information to be included
    in a notification """
    def __init__(self, info_id=None, info_type=None, info_value=None):
        """ Constructor for AdditionalInfo class

        Args:
            info_id (SaNtfElementIdT): infoId field of SaNtfAdditionalInfoT
            info_type (SaNtfValueTypeT): infoType field of SaNtfAdditionalInfoT
            info_value (Any type of eSaNtfValueTypeT): infoValue field of
                SaNtfAdditionalInfoT
        """
        self.info_id = info_id
        self.info_type = info_type
        self.info_value = info_value


class StateChange(object):
    """ This class contains information about a state change event """
    def __init__(self, state_id=None, new_state=None, old_state_present=False,
                 old_state=None):
        """ Constructor for StateChange class

        Args:
            state_id (SaNtfElementIdT): stateId field of SaNtfStateChangeT
            new_state (int): newState field of SaNtfStateChangeT
            old_state_present (bool): oldStatePresent field of
                SaNtfStateChangeT
            old_state (int): oldState field of SaNtfStateChangeT
        """
        self.state_id = state_id
        self.new_state = new_state
        self.old_state_present = old_state_present
        self.old_state = old_state


class AttributeChange(object):
    """ This class contains information about an object's attribute change """
    def __init__(self, attribute_id=None, attribute_type=None,
                 new_attribute_value=None, old_attribute_present=False,
                 old_attribute_value=None):
        """ Constructor for AttributeChange class

        Args:
            attribute_id (SaNtfElementIdT): attributeId field of
                SaNtfAttributeChangeT
            attribute_type (SaNtfValueTypeT): attributeType field of
                SaNtfAttributeChangeT
            new_attribute_value (Any type of eSaNtfValueTypeT):
                newAttributeValue field of SaNtfAttributeChangeT
            old_attribute_present (bool): oldAttributePresent field of
                SaNtfAttributeChangeT
            old_attribute_value (Any type of eSaNtfValueTypeT):
                oldAttributeValue field of SaNtfAttributeChangeT
        """
        self.attribute_id = attribute_id
        self.attribute_type = attribute_type
        self.new_attribute_value = new_attribute_value
        self.old_attribute_present = old_attribute_present
        self.old_attribute_value = old_attribute_value


class Attribute(object):
    """ This class contains information about an object's attribute """
    def __init__(self, attribute_id=None, attribute_type=None,
                 attribute_value=None):
        """ Constructor for Attribute class

        Args:
            attribute_id (SaNtfElementIdT): attributeId field of
                SaNtfAttributeT
            attribute_type (SaNtfValueTypeT): attributeType field of
                SaNtfAttributeT
            attribute_value (Any type of eSaNtfValueTypeT): attributeValue
                field of SaNtfAttributeT
        """
        self.attribute_id = attribute_id
        self.attribute_type = attribute_type
        self.attribute_value = attribute_value


class SecurityAlarmDetector(object):
    """ This class contains information about a security alarm detector """
    def __init__(self, value=None, value_type=None):
        """ Constructor for SecurityAlarmDetector class

        Args:
            value (Any type of eSaNtfValueTypeT): valueType field of
                SaNtfSecurityAlarmDetectorT
            value_type (SaNtfValueTypeT): value field of
                SaNtfSecurityAlarmDetectorT
        """
        self.value = value
        self.value_type = value_type


class ServiceUser(object):
    """ This class contains information about a service user """
    def __init__(self, value=None, value_type=None):
        """ Constructor for ServiceUser class

        Args:
            value (Any type of eSaNtfValueTypeT): valueType field of
                SaNtfServiceUserT
            value_type (SaNtfValueTypeT): value field of SaNtfServiceUserT
        """
        self.value = value
        self.value_type = value_type


class ServiceProvider(ServiceUser):
    """ This class contains information about a service provider """
    pass


class ThresholdInformation(object):
    """ This class contains information about a threshold """
    def __init__(self, threshold_id=None, threshold_value_type=None,
                 threshold_value=None, threshold_hysteresis=None,
                 observed_value=None, arm_time=None):
        """ Constructor for ThresholdInformation class

        Args:
            threshold_id (SaNtfElementIdT): thresholdId field of
                SaNtfThresholdInformationT
            threshold_value_type (SaNtfValueTypeT): thresholdValueType field of
                SaNtfThresholdInformationT
            threshold_value (Any type of eSaNtfValueTypeT): thresholdValue
                 field of SaNtfThresholdInformationT
            threshold_hysteresis (SaNtfValueTypeT): thresholdHysteresis
                field of SaNtfThresholdInformationT
            observed_value (Any type of eSaNtfValueTypeT): observedValue field
                of SaNtfThresholdInformationT
            arm_time (SaTimeT): armTime field of SaNtfThresholdInformationT
        """
        self.threshold_id = threshold_id
        self.threshold_value_type = threshold_value_type
        self.threshold_value = threshold_value
        self.threshold_hysteresis = threshold_hysteresis
        self.observed_value = observed_value
        self.arm_time = arm_time


class SpecificProblem(object):
    """ This class contains information about a specific problem """
    def __init__(self, problem_id=None, problem_class_id=None,
                 problem_type=None, problem_value=None):
        """ Constructor for SpecificProblem class

        Args:
            problem_id (SaNtfElementIdT): problemId field of
                SaNtfSpecificProblemT
            problem_class_id (SaNtfClassIdT): problemClassId field of
                SaNtfSpecificProblemT
            problem_type (SaNtfValueTypeT): problemType field of
                SaNtfSpecificProblemT
            problem_value (Any type of eSaNtfValueTypeT): problemValue field of
                SaNtfSpecificProblemT
        """
        self.problem_id = problem_id
        self.problem_class_id = problem_class_id
        self.problem_type = problem_type
        self.problem_value = problem_value


class ProposedRepairAction(object):
    """ This class contains information about a proposed repair action """
    def __init__(self, action_id=None, action_value_type=None,
                 action_value=None):
        """ Constructor for ProposedRepairAction class

        Args:
            action_id (SaNtfElementIdT): actionId field of
                SaNtfProposedRepairActionT
            action_value_type (SaNtfValueTypeT): actionValueType field of
                SaNtfProposedRepairActionT
            action_value (Any type of eSaNtfValueTypeT): actionValue field of
                SaNtfProposedRepairActionT
        """
        self.action_id = action_id
        self.action_value_type = action_value_type
        self.action_value = action_value


class NtfAgent(object):
    """ This class manages the life cycle of an NTF agent """

    def __init__(self, version=None):
        """ Constructor for NtfAgent class

        Args:
            version (SaVersionT): NTF API version
        """
        self.init_version = version if version is not None \
            else SaVersionT('A', 1, 1)
        self.version = None
        self.handle = None
        self.callbacks = None
        self.sel_obj = SaSelectionObjectT()
        self.ntf_notif_function = None
        self.ntf_notif_discarded_function = None

    def __enter__(self):
        """ Enter method for NtfAgent class """
        return self

    def __exit__(self, exception_type, exception_value, traceback):
        """ Exit method for NtfAgent class

        Finalize the NTF agent handle
        """
        if self.handle is not None:
            saNtfFinalize(self.handle)
            self.handle = None

    def __del__(self):
        """ Destructor for NtfAgent class

        Finalize the NTF agent handle
        """
        if self.handle is not None:
            saNtf.saNtfFinalize(self.handle)
            self.handle = None

    def _ntf_notif_callback(self, c_subscription_id, c_notif):
        """ This callback is invoked by NTF to deliver a notification to a
        subscriber of that notification type.

        Args:
            c_subscription_id (SaNtfSubscriptionIdT): The subscription id
                previously provided by the subscriber when subscribing for this
                type of notification
            c_notif (SaNtfNotificationsT): The notification delivered by this
                callback
        """
        pass

    def _ntf_notif_discarded_callback(self, c_subscription_id,
                                      c_notification_type, c_number_discarded,
                                      c_discarded_notification_identifiers):
        """ This callback is invoked by NTF to notify a subscriber of a
        particular notification type that one or more notifications of that
        type have been discarded.

        Args:
            c_subscription_id (SaNtfSubscriptionIdT): The subscription id
                previously provided by the subscriber when subscribing for
                discarded notifications
            c_notification_type (SaNtfNotificationTypeT): The notification type
                of the discarded notifications
            c_number_discarded (SaUint32T): The number of discarded
                notifications
            c_discarded_notification_identifiers (SaNtfIdentifierT): The list
                of notification identifiers of the discarded notifications
        """
        pass

    def initialize(self, ntf_notif_func=None, ntf_notif_discarded_func=None):
        """ Initialize the NTF agent library

        Args:
            ntf_notif_func (callback): Callback function for subscribed
                notifications
            ntf_notif_discarded_func (callback): Callback function for
                discarded notifications

        Returns:
            SaAisErrorT: Return code of the saNtfInitialize() API call
        """
        self.callbacks = None
        # Set up callbacks if any
        if ntf_notif_func is not None or ntf_notif_discarded_func is not None:
            self.ntf_notif_function = ntf_notif_func
            self.ntf_notif_discarded_function = ntf_notif_discarded_func

            self.callbacks = saNtf.SaNtfCallbacksT()
            self.callbacks.saNtfNotificationCallback = \
                saNtf.SaNtfNotificationCallbackT(self._ntf_notif_callback)
            self.callbacks.saNtfNotificationDiscardedCallback = \
                saNtf.SaNtfNotificationDiscardedCallbackT(
                    self._ntf_notif_discarded_callback)

        self.handle = saNtf.SaNtfHandleT()
        self.version = deepcopy(self.init_version)
        rc = saNtfInitialize(self.handle, self.callbacks, self.version)
        if rc != eSaAisErrorT.SA_AIS_OK:
            log_err("saNtfInitialize FAILED - %s" % eSaAisErrorT.whatis(rc))

        return rc

    def get_handle(self):
        """ Return the NTF agent handle successfully initialized

        Returns:
            SaNtfHandleT: NTF agent handle
        """
        return self.handle

    def _fetch_sel_obj(self):
        """ Obtain a selection object (OS file descriptor)

        Returns:
            SaAisErrorT: Return code of the saNtfSelectionObjectGet() API call
        """
        rc = saNtfSelectionObjectGet(self.handle, self.sel_obj)
        if rc != eSaAisErrorT.SA_AIS_OK:
            log_err("saNtfSelectionObjectGet FAILED - %s" %
                    eSaAisErrorT.whatis(rc))

        return rc

    def get_selection_object(self):
        """ Return the selection object associated with the NTF handle

        Returns:
            SaSelectionObjectT: Selection object associated with the NTF handle
        """
        return self.sel_obj

    def dispatch(self, flags):
        """ Invoke NTF callbacks for queued events

        Args:
            flags (eSaDispatchFlagsT): Flags specifying dispatch mode

        Returns:
            SaAisErrorT: Return code of the saNtfDispatch() API call
        """
        rc = saNtfDispatch(self.handle, flags)
        if rc != eSaAisErrorT.SA_AIS_OK:
            log_err("saNtfDispatch FAILED - %s" % eSaAisErrorT.whatis(rc))

        return rc

    def finalize(self):
        """ Finalize the NTF agent handle

        Returns:
            SaAisErrorT: Return code of the saNtfFinalize() API call
        """
        rc = eSaAisErrorT.SA_AIS_OK
        if self.handle:
            rc = saNtfFinalize(self.handle)
            if rc != eSaAisErrorT.SA_AIS_OK:
                log_err("saNtfFinalize FAILED - %s" % eSaAisErrorT.whatis(rc))
            if rc == eSaAisErrorT.SA_AIS_OK \
                    or rc == eSaAisErrorT.SA_AIS_ERR_BAD_HANDLE:
                # If the Finalize() call returned BAD_HANDLE, the handle should
                # already become stale and invalid, so we reset it anyway
                self.handle = None

        return rc
