/*      -*- OpenSAF  -*-
 *
 * (C) Copyright 2008 The OpenSAF Foundation
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. This file and program are licensed
 * under the GNU Lesser General Public License Version 2.1, February 1999.
 * The complete license can be accessed from the following location:
 * http://opensource.org/licenses/lgpl-license.php
 * See the Copying file included with the OpenSAF distribution for full
 * licensing terms.
 *
 * Author(s): Ericsson AB
 *
 */

#include <string.h>
#include "ntfa.h"
#include "ntf/common/ntfsv_mem.h"
#include "base/osaf_extended_name.h"
#include <saAis.h>
#define NCS_SAF_MIN_ACCEPT_TIME 10
#define NTFS_WAIT_TIME 1000

/* Macro to validate the dispatch flags */
#define m_DISPATCH_FLAG_IS_VALID(flag)                                         \
	((SA_DISPATCH_ONE == flag) || (SA_DISPATCH_ALL == flag) ||             \
	 (SA_DISPATCH_BLOCKING == flag))

#define NTFSV_NANOSEC_TO_LEAPTM 10000000

/* The main controle block */
ntfa_cb_t ntfa_cb = {
    .cb_lock = PTHREAD_MUTEX_INITIALIZER, .ntfa_ntfsv_state = NTFA_NTFSV_NONE,
};

/* list of subscriptions for this process */
ntfa_subscriber_list_t *subscriberNoList = NULL;

/*
 * @Brief: Determine the common "availability" of API, which depends on the
 *         NTF Server state
 * @Param: None
 * @Return: OK - If Server state is UP
 *          TRY_AGAIN - If Server state is Down and No Active (temporary no
 * active)
 */
static SaAisErrorT checkNtfServerState()
{
	SaAisErrorT rc = SA_AIS_ERR_NOT_SUPPORTED;

	osafassert(pthread_mutex_lock(&ntfa_cb.cb_lock) == 0);

	/* Check NTF server availability */
	if (ntfa_cb.ntfa_ntfsv_state == NTFA_NTFSV_UP) {
		rc = SA_AIS_OK;
	} else if (ntfa_cb.ntfa_ntfsv_state == NTFA_NTFSV_DOWN) {
		TRACE("NTFS server is down");
		rc = SA_AIS_ERR_TRY_AGAIN;
	} else if (ntfa_cb.ntfa_ntfsv_state == NTFA_NTFSV_NO_ACTIVE) {
		TRACE("NTFS server is unavailable");
		rc = SA_AIS_ERR_TRY_AGAIN;
	} else if (ntfa_cb.ntfa_ntfsv_state == NTFA_NTFSV_NONE) {
		TRACE(
		    "No NTF server is detected, or API is called during headless");
		rc = SA_AIS_ERR_TRY_AGAIN;
	} else
		TRACE("Not supported API call under NTF Server state (%u)",
		      ntfa_cb.ntfa_ntfsv_state);

	osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);

	return rc;
}

/*
 * @Brief: Return ntf server state in thread safe manner
 * @Param: None
 * @Return: Server state
 */
static ntfa_ntfsv_state_t getServerState()
{
	osafassert(pthread_mutex_lock(&ntfa_cb.cb_lock) == 0);

	ntfa_ntfsv_state_t state = ntfa_cb.ntfa_ntfsv_state;

	osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
	return state;
}

static SaAisErrorT checkNtfValueTypeRange(SaNtfValueTypeT type)
{
	return (type < SA_NTF_VALUE_UINT8 || type > SA_NTF_VALUE_ARRAY)
		   ? SA_AIS_ERR_INVALID_PARAM
		   : SA_AIS_OK;
}

static SaAisErrorT checkNtfValue(v_data *pvdata, SaNtfValueTypeT type,
				 SaNtfValueT *value)
{
	SaAisErrorT rc = SA_AIS_OK;
	SaNameT *ptr;
	if ((rc = checkNtfValueTypeRange(type)) != SA_AIS_OK) {
		TRACE_1("Invalid value type(%d)", type);
		return rc;
	}

	if (type == SA_NTF_VALUE_LDAP_NAME) {
		/* SaNameT must have length > 2 */
		if (value->ptrVal.dataSize <= 2)
			return SA_AIS_ERR_INVALID_PARAM;

		/* Check SaNameT */
		ptr = pvdata->p_base + value->ptrVal.dataOffset;
		if (!ntfsv_sanamet_is_valid(ptr))
			return SA_AIS_ERR_INVALID_PARAM;

		/* Refill the extended SaNameT into variable data */
		if (osaf_is_an_extended_name(ptr)) {
			size_t length_to_copy;
			void *p;
			if (value->ptrVal.dataSize <
			    (osaf_extended_name_length(ptr) + 2)) {
				LOG_NO(
				    "The allocated memory is not large enough,"
				    " the object will be truncated (%s)",
				    osaf_extended_name_borrow(ptr));
				length_to_copy = value->ptrVal.dataSize - 2;
			} else
				length_to_copy = osaf_extended_name_length(ptr);

			p = pvdata->p_base + value->ptrVal.dataOffset + 2;
			memcpy(p, osaf_extended_name_borrow(ptr),
			       length_to_copy);
		} else {
			if (value->ptrVal.dataSize >
			    SA_MAX_UNEXTENDED_NAME_LENGTH + 2)
				value->ptrVal.dataSize =
				    SA_MAX_UNEXTENDED_NAME_LENGTH + 2;
		}
	}

	return rc;
}

static SaAisErrorT getTypeFromValue(ntfa_notification_hdl_rec_t *not_hdl,
				    const SaNtfValueT *value,
				    SaNtfValueTypeT *type)
{
	SaAisErrorT rc = SA_AIS_ERR_NOT_EXIST;
	SaNtfNotificationHeaderT *not_header = NULL;
	SaNtfAlarmNotificationT *not_alarm;
	SaNtfSecurityAlarmNotificationT *not_sec_alarm;
	SaNtfAttributeChangeNotificationT *not_att_change;
	SaNtfObjectCreateDeleteNotificationT *not_obj;
	int i;

	if (not_hdl == NULL || value == NULL || type == NULL)
		return SA_AIS_ERR_INVALID_PARAM;

	switch (not_hdl->ntfNotificationType) {
	case SA_NTF_TYPE_ALARM:
		not_alarm = &not_hdl->ntfNotification.ntfAlarmNotification;
		not_header = &not_alarm->notificationHeader;
		for (i = 0; i < not_alarm->numSpecificProblems; i++)
			if (value ==
			    &not_alarm->specificProblems[i].problemValue) {
				*type =
				    not_alarm->specificProblems[i].problemType;
				rc = SA_AIS_OK;
				goto done;
			}

		for (i = 0; i < not_alarm->numMonitoredAttributes; i++)
			if (value ==
			    &not_alarm->monitoredAttributes[i].attributeValue) {
				*type = not_alarm->monitoredAttributes[i]
					    .attributeType;
				rc = SA_AIS_OK;
				goto done;
			}

		for (i = 0; i < not_alarm->numProposedRepairActions; i++)
			if (value ==
			    &not_alarm->proposedRepairActions[i].actionValue) {
				*type = not_alarm->proposedRepairActions[i]
					    .actionValueType;
				rc = SA_AIS_OK;
				goto done;
			}

		if (not_alarm->thresholdInformation != NULL &&
		    (value ==
			 &not_alarm->thresholdInformation->thresholdValue ||
		     value == &not_alarm->thresholdInformation
				   ->thresholdHysteresis ||
		     value ==
			 &not_alarm->thresholdInformation->observedValue)) {
			*type =
			    not_alarm->thresholdInformation->thresholdValueType;
			rc = SA_AIS_OK;
			goto done;
		}
		break;
	case SA_NTF_TYPE_SECURITY_ALARM:
		not_sec_alarm =
		    &not_hdl->ntfNotification.ntfSecurityAlarmNotification;
		not_header = &not_sec_alarm->notificationHeader;
		if (not_sec_alarm->securityAlarmDetector != NULL &&
		    value == &not_sec_alarm->securityAlarmDetector->value) {
			*type = not_sec_alarm->securityAlarmDetector->valueType;
			rc = SA_AIS_OK;
			goto done;
		}

		if (not_sec_alarm->serviceUser != NULL &&
		    value == &not_sec_alarm->serviceUser->value) {
			*type = not_sec_alarm->serviceUser->valueType;
			rc = SA_AIS_OK;
			goto done;
		}

		if (not_sec_alarm->serviceProvider != NULL &&
		    value == &not_sec_alarm->serviceProvider->value) {
			*type = not_sec_alarm->serviceProvider->valueType;
			rc = SA_AIS_OK;
			goto done;
		}
		break;
	case SA_NTF_TYPE_STATE_CHANGE:
		not_header =
		    &not_hdl->ntfNotification.ntfStateChangeNotification
			 .notificationHeader;
		break;

	case SA_NTF_TYPE_ATTRIBUTE_CHANGE:
		not_att_change =
		    &not_hdl->ntfNotification.ntfAttributeChangeNotification;
		not_header = &not_att_change->notificationHeader;
		for (i = 0; i < not_att_change->numAttributes; i++) {
			if (value == &not_att_change->changedAttributes[i]
					  .oldAttributeValue ||
			    value == &not_att_change->changedAttributes[i]
					  .newAttributeValue) {
				*type = not_att_change->changedAttributes[i]
					    .attributeType;
				/* NTFIMCN has being used SA_NTF_VALUE_LDAP_NAME
				 * as SA_NTF_VALUE_STRING for changeAttributes,
				 * so it must return SA_NTF_VALUE_STRING due to
				 * backward compatibility
				 */
				if (*type == SA_NTF_VALUE_LDAP_NAME)
					*type = SA_NTF_VALUE_STRING;
				rc = SA_AIS_OK;
				goto done;
			}
		}
		break;

	case SA_NTF_TYPE_OBJECT_CREATE_DELETE:
		not_obj =
		    &not_hdl->ntfNotification.ntfObjectCreateDeleteNotification;
		not_header = &not_obj->notificationHeader;
		for (i = 0; i < not_obj->numAttributes; i++) {
			if (value ==
			    &not_obj->objectAttributes[i].attributeValue) {
				*type =
				    not_obj->objectAttributes[i].attributeType;
				/* NTFIMCN has being used SA_NTF_VALUE_LDAP_NAME
				 * as SA_NTF_VALUE_STRING for objectAttributes,
				 * so it must return SA_NTF_VALUE_STRING due to
				 * backward compatibility
				 */
				if (*type == SA_NTF_VALUE_LDAP_NAME)
					*type = SA_NTF_VALUE_STRING;
				rc = SA_AIS_OK;
				goto done;
			}
		}
		break;
	default:
		TRACE_1("Unknown notification type");
		return SA_AIS_ERR_INVALID_PARAM;
	}

	for (i = 0; i < not_header->numAdditionalInfo; i++)
		if (value == &not_header->additionalInfo[i].infoValue) {
			*type = not_header->additionalInfo[i].infoType;
			rc = SA_AIS_OK;
			goto done;
		}
done:
	return rc;
}

static SaAisErrorT checkFilterHeader(SaNtfNotificationFilterHeaderT *nfh)
{

	SaUint16T i;
	for (i = 0; i != nfh->numNotificationObjects; ++i)
		if (!ntfsv_sanamet_is_valid(&nfh->notificationObjects[i]))
			return SA_AIS_ERR_INVALID_PARAM;
	for (i = 0; i != nfh->numNotifyingObjects; ++i)
		if (!ntfsv_sanamet_is_valid(&nfh->notifyingObjects[i]))
			return SA_AIS_ERR_INVALID_PARAM;

	return SA_AIS_OK;
}

static SaAisErrorT checkAttributeChangeFilterParameters(
    ntfa_filter_hdl_rec_t *attributeChangeFilterData)
{
	SaUint16T i;

	for (i = 0; i < attributeChangeFilterData->notificationFilter
			    .attributeChangeNotificationfilter
			    .notificationFilterHeader.numEventTypes;
	     i++) {
		if (attributeChangeFilterData->notificationFilter
			    .attributeChangeNotificationfilter
			    .notificationFilterHeader.eventTypes[i] <
			SA_NTF_ATTRIBUTE_NOTIFICATIONS_START ||
		    attributeChangeFilterData->notificationFilter
			    .attributeChangeNotificationfilter
			    .notificationFilterHeader.eventTypes[i] >
			SA_NTF_ATTRIBUTE_RESET) {
			TRACE_1(
			    "Invalid eventType value = %d",
			    (int)attributeChangeFilterData->notificationFilter
				.attributeChangeNotificationfilter
				.notificationFilterHeader.eventTypes[i]);
			return SA_AIS_ERR_INVALID_PARAM;
		}
	}

	for (i = 0;
	     i < attributeChangeFilterData->notificationFilter
		     .attributeChangeNotificationfilter.numSourceIndicators;
	     i++) {
		if (attributeChangeFilterData->notificationFilter
			    .attributeChangeNotificationfilter
			    .sourceIndicators[i] < SA_NTF_OBJECT_OPERATION ||
		    attributeChangeFilterData->notificationFilter
			    .attributeChangeNotificationfilter
			    .sourceIndicators[i] > SA_NTF_UNKNOWN_OPERATION) {
			TRACE_1(
			    "Invalid eventType value = %d",
			    (int)attributeChangeFilterData->notificationFilter
				.attributeChangeNotificationfilter
				.sourceIndicators[i]);
			return SA_AIS_ERR_INVALID_PARAM;
		}
	}

	return checkFilterHeader(
	    &attributeChangeFilterData->notificationFilter
		 .attributeChangeNotificationfilter.notificationFilterHeader);
}

static SaAisErrorT checkObjectCreateDeleteFilterParameters(
    ntfa_filter_hdl_rec_t *objectCreateDeleteFilterData)
{
	SaUint16T i;

	for (i = 0; i < objectCreateDeleteFilterData->notificationFilter
			    .objectCreateDeleteNotificationfilter
			    .notificationFilterHeader.numEventTypes;
	     i++) {
		if (objectCreateDeleteFilterData->notificationFilter
			    .objectCreateDeleteNotificationfilter
			    .notificationFilterHeader.eventTypes[i] <
			SA_NTF_OBJECT_NOTIFICATIONS_START ||
		    objectCreateDeleteFilterData->notificationFilter
			    .objectCreateDeleteNotificationfilter
			    .notificationFilterHeader.eventTypes[i] >
			SA_NTF_OBJECT_DELETION) {
			TRACE_1("Invalid eventType value = %d",
				(int)objectCreateDeleteFilterData
				    ->notificationFilter
				    .objectCreateDeleteNotificationfilter
				    .notificationFilterHeader.eventTypes[i]);
			return SA_AIS_ERR_INVALID_PARAM;
		}
	}

	for (i = 0;
	     i < objectCreateDeleteFilterData->notificationFilter
		     .objectCreateDeleteNotificationfilter.numSourceIndicators;
	     i++) {
		if (objectCreateDeleteFilterData->notificationFilter
			    .objectCreateDeleteNotificationfilter
			    .sourceIndicators[i] < SA_NTF_OBJECT_OPERATION ||
		    objectCreateDeleteFilterData->notificationFilter
			    .objectCreateDeleteNotificationfilter
			    .sourceIndicators[i] > SA_NTF_UNKNOWN_OPERATION) {
			TRACE_1("Invalid eventType value = %d",
				(int)objectCreateDeleteFilterData
				    ->notificationFilter
				    .objectCreateDeleteNotificationfilter
				    .sourceIndicators[i]);
			return SA_AIS_ERR_INVALID_PARAM;
		}
	}

	return checkFilterHeader(
	    &objectCreateDeleteFilterData->notificationFilter
		 .objectCreateDeleteNotificationfilter
		 .notificationFilterHeader);
}

static SaAisErrorT
checkStateChangeFilterParameters(ntfa_filter_hdl_rec_t *stateChangeFilterData)
{
	SaUint16T i;

	for (i = 0; i < stateChangeFilterData->notificationFilter
			    .stateChangeNotificationfilter
			    .notificationFilterHeader.numEventTypes;
	     i++) {
		if (stateChangeFilterData->notificationFilter
			    .stateChangeNotificationfilter
			    .notificationFilterHeader.eventTypes[i] <
			SA_NTF_STATE_CHANGE_NOTIFICATIONS_START ||
		    stateChangeFilterData->notificationFilter
			    .stateChangeNotificationfilter
			    .notificationFilterHeader.eventTypes[i] >
			SA_NTF_OBJECT_STATE_CHANGE) {
			TRACE_1("Invalid eventType value = %d",
				(int)stateChangeFilterData->notificationFilter
				    .stateChangeNotificationfilter
				    .notificationFilterHeader.eventTypes[i]);
			return SA_AIS_ERR_INVALID_PARAM;
		}
	}

	for (i = 0; i < stateChangeFilterData->notificationFilter
			    .stateChangeNotificationfilter.numSourceIndicators;
	     i++) {
		if (stateChangeFilterData->notificationFilter
			    .stateChangeNotificationfilter.sourceIndicators[i] <
			SA_NTF_OBJECT_OPERATION ||
		    stateChangeFilterData->notificationFilter
			    .stateChangeNotificationfilter.sourceIndicators[i] >
			SA_NTF_UNKNOWN_OPERATION) {
			TRACE_1("Invalid eventType value = %d",
				(int)stateChangeFilterData->notificationFilter
				    .stateChangeNotificationfilter
				    .sourceIndicators[i]);
			return SA_AIS_ERR_INVALID_PARAM;
		}
	}

	for (i = 0; i < stateChangeFilterData->notificationFilter
			    .stateChangeNotificationfilter.numStateChanges;
	     i++) {
		SaBoolT oldstate =
		    stateChangeFilterData->notificationFilter
			.stateChangeNotificationfilter.changedStates[i]
			.oldStatePresent;
		if (oldstate != SA_FALSE && oldstate != SA_TRUE) {
			TRACE_1("Invalid changedStates value = %d", oldstate);
			return SA_AIS_ERR_INVALID_PARAM;
		}
	}
	return checkFilterHeader(
	    &stateChangeFilterData->notificationFilter
		 .stateChangeNotificationfilter.notificationFilterHeader);
}

static SaAisErrorT
checkAlarmFilterParameters(ntfa_filter_hdl_rec_t *alarmFilterData)
{
	SaUint16T i;

	for (i = 0;
	     i < alarmFilterData->notificationFilter.alarmNotificationfilter
		     .notificationFilterHeader.numEventTypes;
	     i++) {
		if (alarmFilterData->notificationFilter.alarmNotificationfilter
			    .notificationFilterHeader.eventTypes[i] <
			SA_NTF_ALARM_NOTIFICATIONS_START ||
		    alarmFilterData->notificationFilter.alarmNotificationfilter
			    .notificationFilterHeader.eventTypes[i] >
			SA_NTF_ALARM_ENVIRONMENT) {
			TRACE_1("Invalid eventType value = %d",
				(int)alarmFilterData->notificationFilter
				    .alarmNotificationfilter
				    .notificationFilterHeader.eventTypes[i]);
			return SA_AIS_ERR_INVALID_PARAM;
		}
	}

	for (i = 0; i < alarmFilterData->notificationFilter
			    .alarmNotificationfilter.numProbableCauses;
	     i++) {
		if (alarmFilterData->notificationFilter.alarmNotificationfilter
			    .probableCauses[i] < SA_NTF_ADAPTER_ERROR ||
		    alarmFilterData->notificationFilter.alarmNotificationfilter
			    .probableCauses[i] > SA_NTF_UNSPECIFIED_REASON) {
			TRACE_1("Invalid ProbableCause value = %d",
				(int)alarmFilterData->notificationFilter
				    .alarmNotificationfilter.probableCauses[i]);
			return SA_AIS_ERR_INVALID_PARAM;
		}
	}

	for (i = 0; i < alarmFilterData->notificationFilter
			    .alarmNotificationfilter.numPerceivedSeverities;
	     i++) {
		if (alarmFilterData->notificationFilter.alarmNotificationfilter
			    .perceivedSeverities[i] < SA_NTF_SEVERITY_CLEARED ||
		    alarmFilterData->notificationFilter.alarmNotificationfilter
			    .perceivedSeverities[i] >
			SA_NTF_SEVERITY_CRITICAL) {
			TRACE_1("Invalid PercievedSeverity value = %d",
				(int)alarmFilterData->notificationFilter
				    .alarmNotificationfilter
				    .perceivedSeverities[i]);
			return SA_AIS_ERR_INVALID_PARAM;
		}
	}

	for (i = 0; i < alarmFilterData->notificationFilter
			    .alarmNotificationfilter.numTrends;
	     i++) {
		if (alarmFilterData->notificationFilter.alarmNotificationfilter
			    .trends[i] < SA_NTF_TREND_MORE_SEVERE ||
		    alarmFilterData->notificationFilter.alarmNotificationfilter
			    .trends[i] > SA_NTF_TREND_LESS_SEVERE) {
			TRACE_1("Invalid PercievedSeverity value = %d",
				(int)alarmFilterData->notificationFilter
				    .alarmNotificationfilter.trends[i]);
			return SA_AIS_ERR_INVALID_PARAM;
		}
	}

	return checkFilterHeader(
	    &alarmFilterData->notificationFilter.alarmNotificationfilter
		 .notificationFilterHeader);
}

static SaAisErrorT checkSecurityAlarmFilterParameters(
    ntfa_filter_hdl_rec_t *securityAlarmFilterData)
{
	SaUint16T i;

	for (i = 0; i < securityAlarmFilterData->notificationFilter
			    .securityAlarmNotificationfilter
			    .notificationFilterHeader.numEventTypes;
	     i++) {
		if (securityAlarmFilterData->notificationFilter
			    .securityAlarmNotificationfilter
			    .notificationFilterHeader.eventTypes[i] <
			SA_NTF_SECURITY_ALARM_NOTIFICATIONS_START ||
		    securityAlarmFilterData->notificationFilter
			    .securityAlarmNotificationfilter
			    .notificationFilterHeader.eventTypes[i] >
			SA_NTF_TIME_VIOLATION) {
			TRACE_1("Invalid eventType value = %d",
				(int)securityAlarmFilterData->notificationFilter
				    .securityAlarmNotificationfilter
				    .notificationFilterHeader.eventTypes[i]);
			return SA_AIS_ERR_INVALID_PARAM;
		}
	}

	for (i = 0; i < securityAlarmFilterData->notificationFilter
			    .securityAlarmNotificationfilter.numProbableCauses;
	     i++) {
		if (securityAlarmFilterData->notificationFilter
			    .securityAlarmNotificationfilter.probableCauses[i] <
			SA_NTF_ADAPTER_ERROR ||
		    securityAlarmFilterData->notificationFilter
			    .securityAlarmNotificationfilter.probableCauses[i] >
			SA_NTF_UNSPECIFIED_REASON) {
			TRACE_1("Invalid ProbableCause value = %d",
				(int)securityAlarmFilterData->notificationFilter
				    .securityAlarmNotificationfilter
				    .probableCauses[i]);
			return SA_AIS_ERR_INVALID_PARAM;
		}
	}

	for (i = 0; i < securityAlarmFilterData->notificationFilter
			    .securityAlarmNotificationfilter.numSeverities;
	     i++) {
		if (securityAlarmFilterData->notificationFilter
			    .securityAlarmNotificationfilter.severities[i] <
			SA_NTF_SEVERITY_CLEARED ||
		    securityAlarmFilterData->notificationFilter
			    .securityAlarmNotificationfilter.severities[i] >
			SA_NTF_SEVERITY_CRITICAL) {
			TRACE_1(
			    "Invalid PercievedSeverity value = %d",
			    (int)securityAlarmFilterData->notificationFilter
				.securityAlarmNotificationfilter.severities[i]);
			return SA_AIS_ERR_INVALID_PARAM;
		}
	}

	for (i = 0;
	     i < securityAlarmFilterData->notificationFilter
		     .securityAlarmNotificationfilter.numSecurityAlarmDetectors;
	     i++) {
		if (checkNtfValueTypeRange(
			securityAlarmFilterData->notificationFilter
			    .securityAlarmNotificationfilter
			    .securityAlarmDetectors[i]
			    .valueType) != SA_AIS_OK) {
			TRACE_1(
			    "Invalid securityAlarmDetectors value type = %d",
			    (int)securityAlarmFilterData->notificationFilter
				.securityAlarmNotificationfilter
				.securityAlarmDetectors[i]
				.valueType);
			return SA_AIS_ERR_INVALID_PARAM;
		}
	}

	for (i = 0; i < securityAlarmFilterData->notificationFilter
			    .securityAlarmNotificationfilter.numServiceUsers;
	     i++) {
		if (checkNtfValueTypeRange(
			securityAlarmFilterData->notificationFilter
			    .securityAlarmNotificationfilter.serviceUsers[i]
			    .valueType) != SA_AIS_OK) {
			TRACE_1(
			    "Invalid serviceUsers value type = %d",
			    (int)securityAlarmFilterData->notificationFilter
				.securityAlarmNotificationfilter.serviceUsers[i]
				.valueType);
			return SA_AIS_ERR_INVALID_PARAM;
		}
	}

	for (i = 0;
	     i < securityAlarmFilterData->notificationFilter
		     .securityAlarmNotificationfilter.numServiceProviders;
	     i++) {
		if (checkNtfValueTypeRange(
			securityAlarmFilterData->notificationFilter
			    .securityAlarmNotificationfilter.serviceProviders[i]
			    .valueType) != SA_AIS_OK) {
			TRACE_1("Invalid serviceProviders value type = %d",
				(int)securityAlarmFilterData->notificationFilter
				    .securityAlarmNotificationfilter
				    .serviceProviders[i]
				    .valueType);
			return SA_AIS_ERR_INVALID_PARAM;
		}
	}

	return checkFilterHeader(
	    &securityAlarmFilterData->notificationFilter
		 .securityAlarmNotificationfilter.notificationFilterHeader);
}

/* help functions */
static SaAisErrorT checkHeader(v_data *pvdata, SaNtfNotificationHeaderT *nh)
{
	int i = 0;
	SaAisErrorT rc;
	if (!ntfsv_sanamet_is_valid(nh->notificationObject) ||
	    !ntfsv_sanamet_is_valid(nh->notifyingObject)) {
		TRACE_1("SaNameT is invaild");
		return SA_AIS_ERR_INVALID_PARAM;
	}

	for (i = 0; i < nh->numAdditionalInfo; i++) {
		if ((rc = checkNtfValue(pvdata, nh->additionalInfo[i].infoType,
					&nh->additionalInfo[i].infoValue)) !=
		    SA_AIS_OK)
			return rc;
	}

	// AIS: additionalText must be consistent with lengthAdditionalText
	if (nh->additionalText == NULL && nh->lengthAdditionalText != 0) {
		LOG_ER("additionalText is NULL and lengthAdditionalText:%u",
		       nh->lengthAdditionalText);
		return SA_AIS_ERR_INVALID_PARAM;
	}

	if (nh->lengthAdditionalText > MAX_ADDITIONAL_TEXT_LENGTH) {
		LOG_ER("lengthAdditionalText is too long");
		return SA_AIS_ERR_INVALID_PARAM;
	}

	SaUint16T len = nh->lengthAdditionalText;
	SaStringT addT = nh->additionalText;
	if ((addT != NULL) && (len != strlen(addT) + 1)) {
		LOG_ER(
		    "Mismatch size b/w additionalText:%u and lengthAdditionalText:%u",
		    (SaUint16T)(strlen(addT) + 1), len);
		return SA_AIS_ERR_INVALID_PARAM;
	}

	return SA_AIS_OK;
}

static SaAisErrorT
checkAlarmParameters(ntfa_notification_hdl_rec_t *not_hdl_rec, v_data *pvdata)
{
	int i = 0;
	SaNtfAlarmNotificationT *notification =
	    &not_hdl_rec->ntfNotification.ntfAlarmNotification;
	if (*notification->probableCause < SA_NTF_ADAPTER_ERROR ||
	    *notification->probableCause > SA_NTF_UNSPECIFIED_REASON) {
		TRACE_1("Invalid probableCause value");
		return SA_AIS_ERR_INVALID_PARAM;
	}

	if (*notification->perceivedSeverity < SA_NTF_SEVERITY_CLEARED ||
	    *notification->perceivedSeverity > SA_NTF_SEVERITY_CRITICAL) {
		TRACE_1("Invalid perceivedSeverity value");
		return SA_AIS_ERR_INVALID_PARAM;
	}

	if (*notification->trend < SA_NTF_TREND_MORE_SEVERE ||
	    *notification->trend > SA_NTF_TREND_LESS_SEVERE) {
		TRACE_1("Invalid trend value");
		return SA_AIS_ERR_INVALID_PARAM;
	}

	for (i = 0; i < notification->numSpecificProblems; i++) {
		if (checkNtfValue(
			pvdata, notification->specificProblems[i].problemType,
			&notification->specificProblems[i].problemValue) !=
		    SA_AIS_OK) {
			TRACE_1("Invalid specificProblems");
			return SA_AIS_ERR_INVALID_PARAM;
		}
	}

	for (i = 0; i < notification->numMonitoredAttributes; i++) {
		if (checkNtfValue(
			pvdata,
			notification->monitoredAttributes[i].attributeType,
			&notification->monitoredAttributes[i].attributeValue) !=
		    SA_AIS_OK) {
			TRACE_1("Invalid monitoredAttributes");
			return SA_AIS_ERR_INVALID_PARAM;
		}
	}

	for (i = 0; i < notification->numProposedRepairActions; i++) {
		if (checkNtfValue(
			pvdata,
			notification->proposedRepairActions[i].actionValueType,
			&notification->proposedRepairActions[i].actionValue) !=
		    SA_AIS_OK) {
			TRACE_1("Invalid proposedRepairActions");
			return SA_AIS_ERR_INVALID_PARAM;
		}
	}

	if (checkNtfValueTypeRange(
		notification->thresholdInformation->thresholdValueType) !=
	    SA_AIS_OK) {
		TRACE_1("Invalid thresholdInformation type value");
		return SA_AIS_ERR_INVALID_PARAM;
	}

	if (*notification->notificationHeader.eventType <
		SA_NTF_ALARM_NOTIFICATIONS_START ||
	    *notification->notificationHeader.eventType >
		SA_NTF_ALARM_ENVIRONMENT) {
		TRACE_1("Invalid eventType value = %d",
			(int)*notification->notificationHeader.eventType);
		return SA_AIS_ERR_INVALID_PARAM;
	}

	return checkHeader(pvdata, &notification->notificationHeader);
}

static SaAisErrorT
checkSecurityAlarmParameters(ntfa_notification_hdl_rec_t *not_hdl_rec,
			     v_data *pvdata)
{
	SaNtfSecurityAlarmNotificationT *notification =
	    &not_hdl_rec->ntfNotification.ntfSecurityAlarmNotification;
	if (*notification->notificationHeader.eventType <
		SA_NTF_SECURITY_ALARM_NOTIFICATIONS_START ||
	    *notification->notificationHeader.eventType >
		SA_NTF_TIME_VIOLATION) {
		TRACE_1("Invalid eventType value");
		return SA_AIS_ERR_INVALID_PARAM;
	}

	if (*notification->probableCause < SA_NTF_ADAPTER_ERROR ||
	    *notification->probableCause > SA_NTF_UNSPECIFIED_REASON) {
		TRACE_1("Invalid probableCause value");
		return SA_AIS_ERR_INVALID_PARAM;
	}

	if (checkNtfValue(
		pvdata, notification->securityAlarmDetector->valueType,
		&notification->securityAlarmDetector->value) != SA_AIS_OK) {
		TRACE_1("Invalid securityAlarmDetector");
		return SA_AIS_ERR_INVALID_PARAM;
	}

	if (checkNtfValue(pvdata, notification->serviceUser->valueType,
			  &notification->serviceUser->value) != SA_AIS_OK) {
		TRACE_1("Invalid serviceUser");
		return SA_AIS_ERR_INVALID_PARAM;
	}

	if (checkNtfValue(pvdata, notification->serviceProvider->valueType,
			  &notification->serviceProvider->value) != SA_AIS_OK) {
		TRACE_1("Invalid serviceProvider");
		return SA_AIS_ERR_INVALID_PARAM;
	}

	if (*notification->severity < SA_NTF_SEVERITY_CLEARED ||
	    *notification->severity > SA_NTF_SEVERITY_CRITICAL) {
		TRACE_1("Invalid Severity value");
		return SA_AIS_ERR_INVALID_PARAM;
	}

	return checkHeader(pvdata, &notification->notificationHeader);
}

static SaAisErrorT
checkStateChangeParameters(ntfa_notification_hdl_rec_t *not_hdl_rec,
			   v_data *pvdata)
{
	int i;
	SaNtfStateChangeNotificationT *notification =
	    &not_hdl_rec->ntfNotification.ntfStateChangeNotification;
	if (*notification->notificationHeader.eventType <
		SA_NTF_STATE_CHANGE_NOTIFICATIONS_START ||
	    (*notification->notificationHeader.eventType >
		 SA_NTF_OBJECT_STATE_CHANGE &&
	     *notification->notificationHeader.eventType <
		 SA_NTF_MISCELLANEOUS_NOTIFICATIONS_START) ||
	    *notification->notificationHeader.eventType >
		SA_NTF_HPI_EVENT_OTHER) {
		TRACE_1("Invalid eventType value");
		return SA_AIS_ERR_INVALID_PARAM;
	}

	if (*(notification->sourceIndicator) < SA_NTF_OBJECT_OPERATION ||
	    *(notification->sourceIndicator) > SA_NTF_UNKNOWN_OPERATION) {
		TRACE_1("Invalid sourceIndicator value");
		return SA_AIS_ERR_INVALID_PARAM;
	}

	for (i = 0; i < notification->numStateChanges; i++) {
		SaBoolT sp = notification->changedStates[i].oldStatePresent;
		if (sp != SA_FALSE && sp != SA_TRUE)
			return SA_AIS_ERR_INVALID_PARAM;
	}
	return checkHeader(pvdata, &notification->notificationHeader);
}

static SaAisErrorT
checkAttributeChangeParameters(ntfa_notification_hdl_rec_t *not_hdl_rec,
			       v_data *pvdata)
{
	int i;
	SaNtfAttributeChangeNotificationT *notification =
	    &not_hdl_rec->ntfNotification.ntfAttributeChangeNotification;
	if (*notification->notificationHeader.eventType <
		SA_NTF_ATTRIBUTE_NOTIFICATIONS_START ||
	    *notification->notificationHeader.eventType >
		SA_NTF_ATTRIBUTE_RESET) {
		TRACE_1("Invalid eventType value");
		return SA_AIS_ERR_INVALID_PARAM;
	}

	if (*(notification->sourceIndicator) < SA_NTF_OBJECT_OPERATION ||
	    *(notification->sourceIndicator) > SA_NTF_UNKNOWN_OPERATION) {
		TRACE_1("Invalid sourceIndicator value");
		return SA_AIS_ERR_INVALID_PARAM;
	}

	for (i = 0; i < notification->numAttributes; i++) {
		SaBoolT sp =
		    notification->changedAttributes[i].oldAttributePresent;
		if (sp != SA_FALSE && sp != SA_TRUE)
			return SA_AIS_ERR_INVALID_PARAM;
		if (checkNtfValueTypeRange(
			notification->changedAttributes[i].attributeType) !=
		    SA_AIS_OK)
			return SA_AIS_ERR_INVALID_PARAM;
	}
	return checkHeader(pvdata, &notification->notificationHeader);
}

static SaAisErrorT
checkObjectCreateDeleteParameters(ntfa_notification_hdl_rec_t *not_hdl_rec,
				  v_data *pvdata)
{

	int i = 0;
	SaNtfObjectCreateDeleteNotificationT *notification =
	    &not_hdl_rec->ntfNotification.ntfObjectCreateDeleteNotification;
	if (*notification->notificationHeader.eventType <
		SA_NTF_OBJECT_NOTIFICATIONS_START ||
	    *notification->notificationHeader.eventType >
		SA_NTF_OBJECT_DELETION) {
		TRACE_1("Invalid eventType value");
		return SA_AIS_ERR_INVALID_PARAM;
	}

	if (*(notification->sourceIndicator) < SA_NTF_OBJECT_OPERATION ||
	    *(notification->sourceIndicator) > SA_NTF_UNKNOWN_OPERATION) {
		TRACE_1("Invalid sourceIndicator value");
		return SA_AIS_ERR_INVALID_PARAM;
	}

	for (i = 0; i < notification->numAttributes; i++) {
		if (checkNtfValueTypeRange(
			notification->objectAttributes[i].attributeType) !=
		    SA_AIS_OK) {
			TRACE_1("Invalid attributeType value");
			return SA_AIS_ERR_INVALID_PARAM;
		}
	}

	return checkHeader(pvdata, &notification->notificationHeader);
}

/**
 *
 * @param ntfNotificationInstance
 * @param reqNtfSendnot
 */
static SaAisErrorT fillSendStruct(SaNtfNotificationHeaderT *notificationHeader,
				  ntfsv_send_not_req_t *reqNtfSendnot)
{
	SaAisErrorT rc = SA_AIS_OK;
	SaTimeT ntfTime;
	struct timeval currentTime;

	/* Fetch current system time for time stamp value */
	(void)gettimeofday(&currentTime, 0);

	ntfTime = ((unsigned)currentTime.tv_sec * 1000000000ULL) +
		  ((unsigned)currentTime.tv_usec * 1000ULL);

	/* only used in send back, server-->lib */
	reqNtfSendnot->subscriptionId = 0;

	/* Set the current system time stamp if SA_TIME_UNKNOWN */
	if (*notificationHeader->eventTime == (SaTimeT)SA_TIME_UNKNOWN) {
		*notificationHeader->eventTime = (SaTimeT)ntfTime;
	}

	/* nodificationId set to zero means that this is a new notification */
	/* and not an sync message send from the server. */
	*(notificationHeader->notificationId) = 0;

	/* If notifyingObject is empty, its default value is notificationObject
	 */
	if (osaf_is_extended_name_empty(notificationHeader->notifyingObject)) {
		osaf_extended_name_lend(
		    osaf_extended_name_borrow(
			notificationHeader->notificationObject),
		    notificationHeader->notifyingObject);
	}

	return rc;
}

SaAisErrorT
getFilters(const SaNtfNotificationTypeFilterHandlesT *notificationFilterHandles,
	   ntfsv_filter_ptrs_t *filters, SaNtfHandleT *firstHandle,
	   ntfa_client_hdl_rec_t **client_hdl_rec)
{
	int i;
	SaAisErrorT rc = SA_AIS_ERR_NOT_SUPPORTED;
	ntfa_filter_hdl_rec_t *filter_hdl_rec = NULL;

	SaNtfNotificationFilterHandleT filterHndl[5];

	if (notificationFilterHandles == NULL) {
		TRACE_1("NULL Pointer exception of notificationFilterHandles!");
		TRACE_LEAVE();
		return SA_AIS_ERR_INVALID_PARAM;
	} else {
		if (!notificationFilterHandles->alarmFilterHandle &&
		    !notificationFilterHandles->attributeChangeFilterHandle &&
		    !notificationFilterHandles
			 ->objectCreateDeleteFilterHandle &&
		    !notificationFilterHandles->securityAlarmFilterHandle &&
		    !notificationFilterHandles->stateChangeFilterHandle) {
			TRACE_1(
			    "All handles in notificationFilterHandles set to NULL!");
			TRACE_LEAVE();
			return SA_AIS_ERR_INVALID_PARAM;
		}
	}

	filterHndl[0] = notificationFilterHandles->attributeChangeFilterHandle;
	filterHndl[1] =
	    notificationFilterHandles->objectCreateDeleteFilterHandle;
	filterHndl[2] = notificationFilterHandles->securityAlarmFilterHandle;
	filterHndl[3] = notificationFilterHandles->stateChangeFilterHandle;
	filterHndl[4] = notificationFilterHandles->alarmFilterHandle;

	for (i = 0; i < 5; i++) {
		TRACE_1("filter_hdl[%d] = %llu", i, filterHndl[i]);
		if (filterHndl[i]) {
			TRACE_1("Get FilterHandle");

			/* retrieve notification filter hdl rec */
			filter_hdl_rec =
			    ncshm_take_hdl(NCS_SERVICE_ID_NTFA, filterHndl[i]);
			if (filter_hdl_rec == NULL) {
				TRACE_1("ncshm_take_hdl failed");
				TRACE_LEAVE();
				return SA_AIS_ERR_BAD_HANDLE;
			}
			TRACE_1("filter_hdl[%d] = %llu", i,
				filter_hdl_rec->filter_hdl);
			switch (i) {
			case 0:
				rc = checkAttributeChangeFilterParameters(
				    filter_hdl_rec);
				if (rc != SA_AIS_OK) {
					for (i = 0; i <= 0; i++) {
						ncshm_give_hdl(filterHndl[i]);
					}
					TRACE_LEAVE();
					return rc;
				}
				break;
			case 1:
				rc = checkObjectCreateDeleteFilterParameters(
				    filter_hdl_rec);
				if (rc != SA_AIS_OK) {
					for (i = 0; i <= 1; i++) {
						ncshm_give_hdl(filterHndl[i]);
					}
					TRACE_LEAVE();
					return rc;
				}
				break;
			case 2:
				rc = checkSecurityAlarmFilterParameters(
				    filter_hdl_rec);
				if (rc != SA_AIS_OK) {
					for (i = 0; i <= 2; i++) {
						ncshm_give_hdl(filterHndl[i]);
					}
					TRACE_LEAVE();
					return rc;
				}
				break;
			case 3:
				rc = checkStateChangeFilterParameters(
				    filter_hdl_rec);
				if (rc != SA_AIS_OK) {
					for (i = 0; i <= 3; i++) {
						ncshm_give_hdl(filterHndl[i]);
					}
					TRACE_LEAVE();
					return rc;
				}
				break;
			case 4:
				rc = checkAlarmFilterParameters(filter_hdl_rec);
				if (rc != SA_AIS_OK) {
					for (i = 0; i <= 4; i++) {
						ncshm_give_hdl(filterHndl[i]);
					}
					TRACE_LEAVE();
					return rc;
				}
				break;
			}

			if (*client_hdl_rec == NULL) {
				/* retrieve client hdl rec */
				*client_hdl_rec =
				    ncshm_take_hdl(NCS_SERVICE_ID_NTFA,
						   filter_hdl_rec->ntfHandle);
				if (*client_hdl_rec == NULL) {
					int max_i = i;
					TRACE_1("ncshm_take_hdl failed");
					for (i = 0; i < max_i; i++) {
						ncshm_give_hdl(filterHndl[i]);
					}
					TRACE_LEAVE();
					return SA_AIS_ERR_BAD_HANDLE;
				}
				*firstHandle = filter_hdl_rec->ntfHandle;
			}

			if (*firstHandle != filter_hdl_rec->ntfHandle) {
				TRACE_1(
				    "filter handles refers to different clients");
				return SA_AIS_ERR_BAD_HANDLE;
			}
			switch (i) {
			case 0:
				filters->att_ch_filter =
				    &filter_hdl_rec->notificationFilter
					 .attributeChangeNotificationfilter;
				break;
			case 1:
				filters->obj_cr_del_filter =
				    &filter_hdl_rec->notificationFilter
					 .objectCreateDeleteNotificationfilter;
				break;
			case 2:
				filters->sec_al_filter =
				    &filter_hdl_rec->notificationFilter
					 .securityAlarmNotificationfilter;
				break;
			case 3:
				filters->sta_ch_filter =
				    &filter_hdl_rec->notificationFilter
					 .stateChangeNotificationfilter;
				break;
			case 4:
				filters->alarm_filter =
				    &filter_hdl_rec->notificationFilter
					 .alarmNotificationfilter;
				break;
			}
		}
	}
	return rc;
}

/* end help functions */
/****************************************************************************
  Name          : reinitializeClient

  Description   : This routine sends initialize messages to NTF Server in
		  order to obtain new client id

  Arguments     : client_hdl* [IN]: client handle

  Return Values : SA_AIS_OK if success, others are failure

  Notes         : None
******************************************************************************/
SaAisErrorT reinitializeClient(ntfa_client_hdl_rec_t *client_hdl)
{
	uint32_t mds_rc;
	SaAisErrorT rc = SA_AIS_OK;
	ntfsv_msg_t i_msg, *o_msg = NULL;

	TRACE_ENTER();

	memset(&i_msg, 0, sizeof(ntfsv_msg_t));
	i_msg.type = NTFSV_NTFA_API_MSG;
	i_msg.info.api_info.type = NTFSV_INITIALIZE_REQ;
	i_msg.info.api_info.param.init.version = client_hdl->version;

	mds_rc =
	    ntfa_mds_msg_sync_send(&ntfa_cb, &i_msg, &o_msg, NTFS_WAIT_TIME);
	switch (mds_rc) {
	case NCSCC_RC_SUCCESS:
		break;
	case NCSCC_RC_REQ_TIMOUT:
		rc = SA_AIS_ERR_TRY_AGAIN;
		goto done;
	default:
		rc = SA_AIS_ERR_BAD_HANDLE;
		goto done;
	}

	osafassert(o_msg != NULL);
	/* Check response msg: type, rc */
	if (o_msg->info.api_resp_info.type != NTFSV_INITIALIZE_RSP) {
		TRACE("info.api_resp_info.type:%u",
		      o_msg->info.api_resp_info.type);
		rc = SA_AIS_ERR_LIBRARY;
		goto done;
	}
	if ((rc = o_msg->info.api_resp_info.rc) != SA_AIS_OK) {
		TRACE("info.api_resp_info.rc:%u", o_msg->info.api_resp_info.rc);
		if (rc != SA_AIS_ERR_UNAVAILABLE)
			rc = SA_AIS_ERR_BAD_HANDLE;
		goto done;
	}

	client_hdl->ntfs_client_id =
	    o_msg->info.api_resp_info.param.init_rsp.client_id;
	TRACE("Successfully recover client_id, new client_id:%u",
	      client_hdl->ntfs_client_id);

done:
	if (o_msg)
		ntfa_msg_destroy(o_msg);
	TRACE_LEAVE();
	return rc;
}
/****************************************************************************
  Name          : recoverReader

  Description   : This routine sends read_initialize messages to NTF Server in
		  order to reintroduce this reader to NTF Server

  Arguments     : client_hdl* [IN]: client handle
		  reader_hdl* [IN]: reader handle

  Return Values : SA_AIS_OK if success, others are failure

  Notes         : None
******************************************************************************/
SaAisErrorT recoverReader(ntfa_client_hdl_rec_t *client_hdl,
			  ntfa_reader_hdl_rec_t *reader_hdl)
{
	uint32_t mds_rc;
	SaAisErrorT rc = SA_AIS_OK;
	ntfsv_msg_t i_msg, *o_msg = NULL;
	ntfsv_reader_init_req_2_t *send_param;

	TRACE_ENTER();

	/* Make sure the filters have not been deleted */
	if (reader_hdl->filters.alarm_filter == NULL &&
	    reader_hdl->filters.att_ch_filter == NULL &&
	    reader_hdl->filters.obj_cr_del_filter == NULL &&
	    reader_hdl->filters.sec_al_filter == NULL &&
	    reader_hdl->filters.sta_ch_filter == NULL)
		return SA_AIS_ERR_BAD_HANDLE;

	memset(&i_msg, 0, sizeof(ntfsv_msg_t));
	i_msg.type = NTFSV_NTFA_API_MSG;
	i_msg.info.api_info.type = NTFSV_READER_INITIALIZE_REQ_2;
	send_param = &i_msg.info.api_info.param.reader_init_2;
	send_param->head.client_id = client_hdl->ntfs_client_id;
	send_param->head.searchCriteria = reader_hdl->searchCriteria;
	send_param->f_rec = reader_hdl->filters;

	mds_rc =
	    ntfa_mds_msg_sync_send(&ntfa_cb, &i_msg, &o_msg, NTFS_WAIT_TIME);

	switch (mds_rc) {
	case NCSCC_RC_SUCCESS:
		break;
	case NCSCC_RC_REQ_TIMOUT:
		rc = SA_AIS_ERR_TRY_AGAIN;
		goto done;
	default:
		rc = SA_AIS_ERR_BAD_HANDLE;
		goto done;
	}

	osafassert(o_msg != NULL);
	if ((rc = o_msg->info.api_resp_info.rc) != SA_AIS_OK) {
		TRACE("o_msg->info.api_resp_info.rc:%u",
		      o_msg->info.api_resp_info.rc);
		if (rc != SA_AIS_ERR_UNAVAILABLE)
			rc = SA_AIS_ERR_BAD_HANDLE;
		goto done;
	}

	if (o_msg->info.api_resp_info.type != NTFSV_READER_INITIALIZE_RSP) {
		TRACE("msg type (%d) failed",
		      (int)o_msg->info.api_resp_info.type);
		rc = SA_AIS_ERR_LIBRARY;
		goto done;
	}
	/* Update reader_id since it may be changed */
	reader_hdl->reader_id =
	    o_msg->info.api_resp_info.param.reader_init_rsp.readerId;

	TRACE("Recover reader successfully");
done:
	if (o_msg)
		ntfa_msg_destroy(o_msg);
	TRACE_LEAVE();
	return rc;
}
/****************************************************************************
  Name          : recoverSubscriber

  Description   : This routine sends subscribe messages to NTF Server in
		  order to reintroduce this subscriber to NTF Server

  Arguments     : client_hdl* [IN]: client handle
		  subscriber_hdl* [IN]: subscriber handle

  Return Values : SA_AIS_OK if success, others are failure

  Notes         : None
******************************************************************************/
SaAisErrorT recoverSubscriber(ntfa_client_hdl_rec_t *client_hdl,
			      ntfa_subscriber_list_t *subscriber_hdl)
{
	uint32_t mds_rc;
	SaAisErrorT rc = SA_AIS_OK;
	ntfsv_msg_t i_msg, *o_msg = NULL;
	ntfsv_subscribe_req_t *send_param;

	TRACE_ENTER();

	/* Make sure the filters have not been deleted */
	if (subscriber_hdl->filters.alarm_filter == NULL &&
	    subscriber_hdl->filters.att_ch_filter == NULL &&
	    subscriber_hdl->filters.obj_cr_del_filter == NULL &&
	    subscriber_hdl->filters.sec_al_filter == NULL &&
	    subscriber_hdl->filters.sta_ch_filter == NULL)
		return SA_AIS_ERR_BAD_HANDLE;

	memset(&i_msg, 0, sizeof(ntfsv_msg_t));
	i_msg.type = NTFSV_NTFA_API_MSG;
	i_msg.info.api_info.type = NTFSV_SUBSCRIBE_REQ;
	send_param = &i_msg.info.api_info.param.subscribe;

	send_param->client_id = client_hdl->ntfs_client_id;
	send_param->subscriptionId =
	    subscriber_hdl->subscriberListSubscriptionId;
	send_param->f_rec = subscriber_hdl->filters;

	mds_rc =
	    ntfa_mds_msg_sync_send(&ntfa_cb, &i_msg, &o_msg, NTFS_WAIT_TIME);

	switch (mds_rc) {
	case NCSCC_RC_SUCCESS:
		break;
	case NCSCC_RC_REQ_TIMOUT:
		rc = SA_AIS_ERR_TRY_AGAIN;
		goto done;
	default:
		rc = SA_AIS_ERR_BAD_HANDLE;
		goto done;
	}

	osafassert(o_msg != NULL);

	if ((rc = o_msg->info.api_resp_info.rc) != SA_AIS_OK) {
		TRACE("o_msg->info.api_resp_info.rc:%u",
		      o_msg->info.api_resp_info.rc);
		if (rc != SA_AIS_ERR_UNAVAILABLE)
			rc = SA_AIS_ERR_BAD_HANDLE;
		goto done;
	}

	if (o_msg->info.api_resp_info.type != NTFSV_SUBSCRIBE_RSP) {
		TRACE("msg type (%d) failed",
		      (int)o_msg->info.api_resp_info.type);
		rc = SA_AIS_ERR_LIBRARY;
		goto done;
	}
	TRACE("Recover subscriber successfully");
done:
	if (o_msg)
		ntfa_msg_destroy(o_msg);

	TRACE_LEAVE();
	return rc;
}
/****************************************************************************
  Name          : recoverClient

  Description   : In NTF-A.03.01, section 2.1.2. A client can be producer,
		  subscriber, and reader at the same time.
		  This routine will recovery:
		  (1) client_id
		  (2) Then it will continue the recovery if this client also
		  has instance of subscriber or reader.
  Arguments     : client_hdl* [IN]: client handle

  Return Values : SA_AIS_OK if success, others are failure

  Notes         : None
******************************************************************************/
SaAisErrorT recoverClient(ntfa_client_hdl_rec_t *client_hdl)
{

	TRACE_ENTER();
	SaAisErrorT rc = SA_AIS_OK;

	osafassert(client_hdl);
	osafassert(pthread_mutex_lock(&ntfa_cb.cb_lock) == 0);

	if (client_hdl->valid == true)
		goto done;

	if ((rc = reinitializeClient(client_hdl)) == SA_AIS_OK) {
		/* Restore reader */
		ntfa_reader_hdl_rec_t *reader_hdl = client_hdl->reader_list;
		while (reader_hdl != NULL && rc == SA_AIS_OK) {
			rc = recoverReader(client_hdl, reader_hdl);
			reader_hdl = reader_hdl->next;
		}
		if (rc != SA_AIS_OK) {
			TRACE("Failed to restore reader (readerId:%d)",
			      reader_hdl->reader_id);
			goto done;
		}
		/* Restore subscriber */
		ntfa_subscriber_list_t *subscriber_hdl = subscriberNoList;
		while (subscriber_hdl != NULL && rc == SA_AIS_OK) {
			if (client_hdl->local_hdl ==
			    subscriber_hdl->subscriberListNtfHandle)
				rc = recoverSubscriber(client_hdl,
						       subscriber_hdl);
			subscriber_hdl = subscriber_hdl->next;
		}
		if (rc != SA_AIS_OK) {
			TRACE(
			    "Failed to restore subscriber (subscriptionId:%d)",
			    subscriber_hdl->subscriberListSubscriptionId);
			goto done;
		}
		client_hdl->valid = true;
	} else {
		TRACE("Failed to restore client (id:%d)",
		      client_hdl->ntfs_client_id);
		goto done;
	}

done:
	osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
	TRACE_LEAVE();
	return rc;
}
/***************************************************************************
 * 8.4.1
 *
 * saNtfInitialize()
 *
 * This function initializes the Ntf Service.
 *
 * Parameters
 *
 * ntfHandle - [out] A pointer to the handle for this initialization of the
 *                   Ntf Service.
 * callbacks - [in] A pointer to the callbacks structure that contains the
 *                  callback functions of the invoking process that the
 *                  Ntf Service may invoke.
 * version - [in] A pointer to the version of the Ntf Service that the
 *                invoking process is using.
 *
 ***************************************************************************/
SaAisErrorT saNtfInitialize(SaNtfHandleT *ntfHandle,
			    const SaNtfCallbacksT *ntfCallbacks,
			    SaVersionT *version)
{
	ntfa_client_hdl_rec_t *ntfa_hdl_rec;
	ntfsv_msg_t i_msg, *o_msg = NULL;
	SaAisErrorT rc;
	uint32_t client_id, mds_rc;

	TRACE_ENTER();

	if ((ntfHandle == NULL) || (version == NULL)) {
		TRACE("version or handle FAILED");
		rc = SA_AIS_ERR_INVALID_PARAM;
		goto done;
	}

	if ((rc = ntfa_startup()) != NCSCC_RC_SUCCESS) {
		TRACE("ntfa_startup FAILED");
		rc = SA_AIS_ERR_LIBRARY;
		goto done;
	}

	/* validate the version */
	if ((version->releaseCode == NTF_RELEASE_CODE) &&
	    (version->majorVersion <= NTF_MAJOR_VERSION) &&
	    (0 < version->majorVersion)) {
		version->majorVersion = NTF_MAJOR_VERSION;
		if (version->minorVersion != NTF_MINOR_VERSION_0)
			version->minorVersion = NTF_MINOR_VERSION;
	} else {
		TRACE(
		    "version FAILED, required: %c.%u.%u, supported: %c.%u.%u\n",
		    version->releaseCode, version->majorVersion,
		    version->minorVersion, NTF_RELEASE_CODE, NTF_MAJOR_VERSION,
		    NTF_MINOR_VERSION);
		version->releaseCode = NTF_RELEASE_CODE;
		version->majorVersion = NTF_MAJOR_VERSION;
		version->minorVersion = NTF_MINOR_VERSION;
		ntfa_shutdown(false);
		rc = SA_AIS_ERR_VERSION;
		goto done;
	}

	/* Check NTF server availability */
	if ((rc = checkNtfServerState()) != SA_AIS_OK) {
		ntfa_shutdown(false);
		goto done;
	}

	/* Populate the message to be sent to the NTFS */
	memset(&i_msg, 0, sizeof(ntfsv_msg_t));
	i_msg.type = NTFSV_NTFA_API_MSG;
	i_msg.info.api_info.type = NTFSV_INITIALIZE_REQ;
	i_msg.info.api_info.param.init.version = *version;

	/* Send a message to NTFS to obtain a client_id/server ref id which is
	 * cluster wide unique.
	 */
	mds_rc =
	    ntfa_mds_msg_sync_send(&ntfa_cb, &i_msg, &o_msg, NTFS_WAIT_TIME);
	switch (mds_rc) {
	case NCSCC_RC_SUCCESS:
		break;
	case NCSCC_RC_REQ_TIMOUT:
		rc = SA_AIS_ERR_TIMEOUT;
		TRACE("ntfa_mds_msg_sync_send FAILED: %u", rc);
		goto err;
	default:
		TRACE("mtfa_mds_msg_sync_send FAILED: %u", rc);
		rc = SA_AIS_ERR_TRY_AGAIN;
		goto err;
	}
	osafassert(o_msg != NULL);
	/** Make sure the NTFS return status was SA_AIS_OK
	 **/
	if (SA_AIS_OK != o_msg->info.api_resp_info.rc) {
		rc = o_msg->info.api_resp_info.rc;
		TRACE("NTFS return FAILED");
		/*Check CLM membership of node.*/
		if (rc == SA_AIS_ERR_UNAVAILABLE) {
			TRACE("Node not CLM member or stale client");
		}
		goto err;
	}

	/** Store the regId returned by the NTFS
	 ** to pass into the next routine
	 **/
	client_id = o_msg->info.api_resp_info.param.init_rsp.client_id;

	/* create the hdl record & store the callbacks */
	ntfa_hdl_rec = ntfa_hdl_rec_add(&ntfa_cb, ntfCallbacks, client_id);
	if (ntfa_hdl_rec == NULL) {
		rc = SA_AIS_ERR_NO_MEMORY;
		goto err;
	}
	ntfa_hdl_rec->version = *version;
	/* pass the handle value to the appl */
	if (SA_AIS_OK == rc)
		*ntfHandle = ntfa_hdl_rec->local_hdl;
	TRACE_1("OK intitialize with client_id: %u", client_id);

err:
	/* free up the response message */
	if (o_msg)
		ntfa_msg_destroy(o_msg);

	if (rc != SA_AIS_OK) {
		TRACE_2("NTFA INIT FAILED\n");
		ntfa_shutdown(false);
	}

done:
	TRACE_LEAVE();
	return rc;
}

/***************************************************************************
 * 8.4.2
 *
 * saNtfSelectionObjectGet()
 *
 * The saNtfSelectionObjectGet() function returns the operating system handle
 * selectionObject, associated with the handle ntfHandle, allowing the
 * invoking process to ascertain when callbacks are pending. This function
 * allows a process to avoid repeated invoking saNtfDispatch() to see if
 * there is a new event, thus, needlessly consuming CPU time. In a POSIX
 * environment the system handle could be a file descriptor that is used with
 * the poll() system call to detect incoming callbacks.
 *
 *
 * Parameters
 *
 * ntfHandle - [in]
 *   A pointer to the handle, obtained through the saNtfInitialize() function,
 *   designating this particular initialization of the Ntf Service.
 *
 * selectionObject - [out]
 *   A pointer to the operating system handle that the process may use to
 *   detect pending callbacks.
 *
 ***************************************************************************/
SaAisErrorT saNtfSelectionObjectGet(SaNtfHandleT ntfHandle,
				    SaSelectionObjectT *selectionObject)
{
	SaAisErrorT rc = SA_AIS_OK;
	ntfa_client_hdl_rec_t *hdl_rec;
	NCS_SEL_OBJ sel_obj;

	TRACE_ENTER();

	if (selectionObject == NULL) {
		TRACE("selectionObject = NULL");
		rc = SA_AIS_ERR_INVALID_PARAM;
		goto done;
	}

	/* retrieve hdl rec */
	hdl_rec = ncshm_take_hdl(NCS_SERVICE_ID_NTFA, ntfHandle);
	if (hdl_rec == NULL) {
		TRACE("ncshm_take_hdl failed");
		rc = SA_AIS_ERR_BAD_HANDLE;
		goto done;
	}
	/*Check CLM membership of node.*/
	if (hdl_rec->is_stale_client == true) {
		TRACE("Node not CLM member or stale client");
		ncshm_give_hdl(ntfHandle);
		rc = SA_AIS_ERR_UNAVAILABLE;
		goto done;
	}
	/* Obtain the selection object from the IPC queue */
	sel_obj = m_NCS_IPC_GET_SEL_OBJ(&hdl_rec->mbx);

	/* everything's fine.. pass the sel fd to the appl */
	*selectionObject = (SaSelectionObjectT)m_GET_FD_FROM_SEL_OBJ(sel_obj);

	/* return hdl rec */
	ncshm_give_hdl(ntfHandle);

done:
	TRACE_LEAVE();
	return rc;
}

/***************************************************************************
 * 8.4.3
 *
 * saNtfDispatch()
 *
 * The saNtfDispatch() function invokes, in the context of the calling thread,
 * one or all of the pending callbacks for the handle ntfHandle.
 *
 *
 * Parameters
 *
 * ntfHandle - [in]
 *   A pointer to the handle, obtained through the saNtfInitialize() function,
 *   designating this particular initialization of the Ntf Service.
 *
 * dispatchFlags - [in]
 *   Flags that specify the callback execution behavior of the the
 *   saNtfDispatch() function, which have the values SA_DISPATCH_ONE,
 *   SA_DISPATCH_ALL or SA_DISPATCH_BLOCKING, as defined in Section 3.3.8.
 *
 ***************************************************************************/
SaAisErrorT saNtfDispatch(SaNtfHandleT ntfHandle,
			  SaDispatchFlagsT dispatchFlags)
{
	ntfa_client_hdl_rec_t *hdl_rec;
	SaAisErrorT rc;

	TRACE_ENTER();

	if (!m_DISPATCH_FLAG_IS_VALID(dispatchFlags)) {
		TRACE("Invalid dispatchFlags");
		rc = SA_AIS_ERR_INVALID_PARAM;
		goto done;
	}

	/* retrieve hdl rec */
	hdl_rec = ncshm_take_hdl(NCS_SERVICE_ID_NTFA, ntfHandle);
	if (hdl_rec == NULL) {
		TRACE("ncshm_take_hdl ntfHandle ");
		rc = SA_AIS_ERR_BAD_HANDLE;
		goto done;
	}

	/* Need to check NTF server availability here
	 * Because if the notificationCallback just comes after MDS_DOWN
	 * it will go to recovery then result in failure due to no director
	 */
	if ((rc = checkNtfServerState()) != SA_AIS_OK) {
		ncshm_give_hdl(ntfHandle);
		goto done;
	}

	if (!hdl_rec->valid) {
		/* recovery */
		if ((rc = recoverClient(hdl_rec)) != SA_AIS_OK) {
			if (rc == SA_AIS_ERR_BAD_HANDLE) {
				ncshm_give_hdl(ntfHandle);
				osafassert(
				    pthread_mutex_lock(&ntfa_cb.cb_lock) == 0);
				ntfa_hdl_rec_force_del(&ntfa_cb.client_list,
						       hdl_rec);
				osafassert(pthread_mutex_unlock(
					       &ntfa_cb.cb_lock) == 0);
				ntfa_shutdown(false);
				goto done;
			}
			if (rc == SA_AIS_ERR_UNAVAILABLE) {
				TRACE("Node not CLM member or stale client");
				ncshm_give_hdl(ntfHandle);
				goto done;
			}
		}
	}

	/*Check CLM membership of node.*/
	if (hdl_rec->is_stale_client == true) {
		TRACE("Node not CLM member or stale client");
		ncshm_give_hdl(ntfHandle);
		rc = SA_AIS_ERR_UNAVAILABLE;
		goto done;
	}

	if ((rc = ntfa_hdl_cbk_dispatch(&ntfa_cb, hdl_rec, dispatchFlags)) !=
	    SA_AIS_OK)
		TRACE("NTFA_DISPATCH_FAILURE");

	ncshm_give_hdl(ntfHandle);

done:
	TRACE_LEAVE();
	return rc;
}

/***************************************************************************
 * 8.4.4
 *
 * saNtfFinalize()
 *
 * The saNtfFinalize() function closes the association, represented by the
 * ntfHandle parameter, between the process and the Ntf Service.
 * It may free up resources.
 *
 * This function cannot be invoked before the process has invoked the
 * corresponding saNtfInitialize() function for the Ntf Service.
 * After this function is invoked, the selection object is no longer valid.
 * Moreover, the Ntf Service is unavailable for further use unless it is
 * reinitialized using the saNtfInitialize() function.
 *
 * Parameters
 *
 * ntfHandle - [in]
 *   A pointer to the handle, obtained through the saNtfInitialize() function,
 *   designating this particular initialization of the Ntf Service.
 *
 * Note: Avoid using lock on ntfa_cb.cb_lock before giving ntfHandle in this API
 *       as it may lead to deadlock situation in an application in which other
 *       APIs are called along with this in parallel.
 ***************************************************************************/
SaAisErrorT saNtfFinalize(SaNtfHandleT ntfHandle)
{
	ntfa_client_hdl_rec_t *hdl_rec;
	ntfsv_msg_t msg, *o_msg = NULL;
	SaAisErrorT rc = SA_AIS_OK;
	uint32_t mds_rc;

	TRACE_ENTER();

	/* Check NTF server availability */
	if (getServerState() == NTFA_NTFSV_NO_ACTIVE) {
		TRACE("NTFS server is temporarily unavailable");
		rc = SA_AIS_ERR_TRY_AGAIN;
		goto done;
	}

	/* retrieve hdl rec */
	hdl_rec = ncshm_take_hdl(NCS_SERVICE_ID_NTFA, ntfHandle);
	if (hdl_rec == NULL) {
		TRACE("ncshm_take_hdl failed");
		rc = SA_AIS_ERR_BAD_HANDLE;
		goto done;
	}
	if (hdl_rec->valid) {
		/** populate & send the finalize message
		 ** and make sure the finalize from the server
		 ** end returned before deleting the local records.
		 **/
		memset(&msg, 0, sizeof(ntfsv_msg_t));
		msg.type = NTFSV_NTFA_API_MSG;
		msg.info.api_info.type = NTFSV_FINALIZE_REQ;
		msg.info.api_info.param.finalize.client_id =
		    hdl_rec->ntfs_client_id;

		mds_rc = ntfa_mds_msg_sync_send(&ntfa_cb, &msg, &o_msg,
						NTFS_WAIT_TIME);
		switch (mds_rc) {
		case NCSCC_RC_SUCCESS:
			break;
		case NCSCC_RC_REQ_TIMOUT:
			rc = SA_AIS_ERR_TIMEOUT;
			TRACE("ntfa_mds_msg_sync_send FAILED: %u", rc);
			goto done_give_hdl;
		default:
			TRACE("ntfa_mds_msg_sync_send FAILED: %u", rc);
			rc = SA_AIS_ERR_NO_RESOURCES;
			goto done_give_hdl;
		}

		if (o_msg != NULL) {
			rc = o_msg->info.api_resp_info.rc;
			ntfa_msg_destroy(o_msg);
		} else
			rc = SA_AIS_ERR_NO_RESOURCES;
	}

	if (rc == SA_AIS_OK) {
		/** delete the hdl rec
		 ** including all resources allocated by client if MDS send is
		 ** succesful.
		 **/
		rc = ntfa_hdl_rec_del(&ntfa_cb.client_list, hdl_rec);
		if (rc != NCSCC_RC_SUCCESS) {
			TRACE_1("ntfa_hdl_rec_del failed");
			rc = SA_AIS_ERR_BAD_HANDLE;
		}
	}

done_give_hdl:
	ncshm_give_hdl(ntfHandle);

	if (rc == SA_AIS_OK) {
		rc = ntfa_shutdown(false);
		if (rc != NCSCC_RC_SUCCESS)
			TRACE_1("ntfa_shutdown failed");
	}

done:
	TRACE_LEAVE();
	return rc;
}

/**
 * This API internally allocates memory for an alarm notification and
 * initializes the notification structure.
 *
 * @param ntfHandle
 * @param notification
 * @param numCorrelatedNotifications
 * @param lengthAdditionalText
 * @param numAdditionalInfo
 * @param numSpecificProblems
 * @param numMonitoredAttributes
 * @param numProposedRepairActions
 * @param variableDataSize
 *
 * @return SaAisErrorT
 */
SaAisErrorT saNtfAlarmNotificationAllocate(
    SaNtfHandleT ntfHandle, SaNtfAlarmNotificationT *notification,
    SaUint16T numCorrelatedNotifications, SaUint16T lengthAdditionalText,
    SaUint16T numAdditionalInfo, SaUint16T numSpecificProblems,
    SaUint16T numMonitoredAttributes, SaUint16T numProposedRepairActions,
    SaInt16T variableDataSize)
{
	SaAisErrorT rc;
	ntfa_notification_hdl_rec_t *notification_hdl_rec;

	ntfa_client_hdl_rec_t *hdl_rec;
	TRACE_ENTER();

	/* Input pointer check */
	if (notification == NULL) {
		TRACE("NULL pointer in notification struct inparameter to \
	       saNtfAlarmNotificationAllocate()");
		rc = SA_AIS_ERR_INVALID_PARAM;
		goto done;
	}

	/* retrieve hdl rec */
	hdl_rec = ncshm_take_hdl(NCS_SERVICE_ID_NTFA, ntfHandle);
	if (hdl_rec == NULL) {
		TRACE("ncshm_take_hdl failed");
		rc = SA_AIS_ERR_BAD_HANDLE;
		goto done;
	}

	/**                 Lock ntfa_CB                 **/
	osafassert(pthread_mutex_lock(&ntfa_cb.cb_lock) == 0);

	/*Check CLM membership of node.*/
	if (hdl_rec->is_stale_client == true) {
		TRACE("Node not CLM member or stale client");
		osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
		rc = SA_AIS_ERR_UNAVAILABLE;
		goto done_give_hdl;
	}

	/** Allocate an ntfa_LOG_STREAM_HDL_REC structure and insert this
	 *  into the list of channel hdl record.
	 **/
	notification_hdl_rec =
	    ntfa_notification_hdl_rec_add(&hdl_rec, variableDataSize, &rc);
	if (notification_hdl_rec == NULL) {
		osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
		goto done_give_hdl;
	}
	TRACE_1("notification_hdl = %u",
		notification_hdl_rec->notification_hdl);

	notification_hdl_rec->ntfNotificationType = SA_NTF_TYPE_ALARM;
	notification_hdl_rec->ntfNotification.ntfAlarmNotification
	    .notificationHandle =
	    (SaUint64T)notification_hdl_rec->notification_hdl;
	notification_hdl_rec->parent_hdl = hdl_rec;

	/* Allocate all data fields in the header */
	rc = ntfsv_alloc_ntf_header(
	    &notification_hdl_rec->ntfNotification.ntfAlarmNotification
		 .notificationHeader,
	    numCorrelatedNotifications, lengthAdditionalText,
	    numAdditionalInfo);
	if (rc != SA_AIS_OK) {
		goto error_put;
	}

	/* Allocate all alarm specific data fields */
	rc = ntfsv_alloc_ntf_alarm(
	    &notification_hdl_rec->ntfNotification.ntfAlarmNotification,
	    numSpecificProblems, numMonitoredAttributes,
	    numProposedRepairActions);
	if (rc != SA_AIS_OK) {
		goto error_put;
	}

	/* Assign alarm specific data pointers for the client */
	*notification =
	    notification_hdl_rec->ntfNotification.ntfAlarmNotification;
	osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
	goto done_give_hdl;

error_put:
	if (NCSCC_RC_SUCCESS !=
	    ntfa_notification_hdl_rec_del(&hdl_rec->notification_list,
					  notification_hdl_rec)) {
		TRACE("Unable to delete notifiction record");
	}
	osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
done_give_hdl:
	ncshm_give_hdl(ntfHandle);
done:
	TRACE_LEAVE();
	return rc;
}

/**
 * Frees the memory previously allocated for a notification.
 *
 * @param notificationHandle
 *
 * @return SaAisErrorT
 */
SaAisErrorT saNtfNotificationFree(SaNtfNotificationHandleT notificationHandle)
{
	SaAisErrorT rc = SA_AIS_OK;
	unsigned int client_handle;
	ntfa_notification_hdl_rec_t *notification_hdl_rec;

	ntfa_client_hdl_rec_t *client_rec;
	TRACE_ENTER();

	/**                 Lock ntfa_CB                 **/
	osafassert(pthread_mutex_lock(&ntfa_cb.cb_lock) == 0);
	TRACE_1("notificationHandle = %llu", notificationHandle);
	/* retrieve notification hdl rec */
	notification_hdl_rec =
	    ncshm_take_hdl(NCS_SERVICE_ID_NTFA, notificationHandle);
	if (notification_hdl_rec == NULL) {
		osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
		TRACE("ncshm_take_hdl notificationHandle failed");
		rc = SA_AIS_ERR_BAD_HANDLE;
		goto done;
	}
	client_handle = notification_hdl_rec->parent_hdl->local_hdl;
	/* retrieve client hdl rec */
	client_rec = ncshm_take_hdl(NCS_SERVICE_ID_NTFA, client_handle);
	if (client_rec == NULL) {
		/**                  UnLockntfa_CB            **/
		TRACE("ncshm_take_hdl client_handle failed");
		rc = SA_AIS_ERR_BAD_HANDLE;
		goto done_give_hdl;
	}
	/*Check CLM membership of node.*/
	if (client_rec->is_stale_client == true) {
		TRACE("Node not CLM member or stale client");
		ncshm_give_hdl(client_handle);
		rc = SA_AIS_ERR_UNAVAILABLE;
		goto done_give_hdl;
	}

	/* free the resources allocated by saNtf<ntfType>NotificationAllocate */
	ntfa_notification_destructor(notification_hdl_rec);

	/** Delete the resources related to the notificationHandle &
	 *  remove reference in the client.
	 **/
	if (NCSCC_RC_SUCCESS !=
	    ntfa_notification_hdl_rec_del(&client_rec->notification_list,
					  notification_hdl_rec)) {
		TRACE("Unable to delete notifiction record");
		rc = SA_AIS_ERR_LIBRARY;
	}

	ncshm_give_hdl(client_handle);
done_give_hdl:
	/**                  UnLockntfa_CB            **/
	osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
	ncshm_give_hdl(notificationHandle);
done:
	TRACE_LEAVE();
	return rc;
}

/**
 * This method is used to send a notification. The notification is identified
 * by the notification handle that is returned in the notification structure
 * created with a preceding call to one of the
 * saNtf<notification type>NotificationAllocate() functions.
 *
 * @param notificationHandle
 *
 * @return SaAisErrorT
 */
SaAisErrorT saNtfNotificationSend(SaNtfNotificationHandleT notificationHandle)
{
	TRACE_ENTER();
	SaAisErrorT rc = SA_AIS_OK;

	unsigned int client_handle;
	ntfa_client_hdl_rec_t *client_rec;
	ntfa_notification_hdl_rec_t *notification_hdl_rec;
	ntfsv_msg_t msg, *o_msg = NULL;
	ntfsv_send_not_req_t *send_param;
	SaTimeT timeout = NTFS_WAIT_TIME;
	uint32_t mds_rc;
	SaNtfNotificationHeaderT *ntfHeader;

	send_param = calloc(1, sizeof(ntfsv_send_not_req_t));
	if (send_param == NULL) {
		rc = SA_AIS_ERR_NO_MEMORY;
		goto done;
	}

	osafassert(pthread_mutex_lock(&ntfa_cb.cb_lock) == 0);
	notification_hdl_rec =
	    ncshm_take_hdl(NCS_SERVICE_ID_NTFA, notificationHandle);
	if (notification_hdl_rec == NULL) {
		osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
		TRACE("ncshm_take_hdl notificationHandle failed");
		rc = SA_AIS_ERR_BAD_HANDLE;
		goto err_free;
	}
	notification_hdl_rec->is_longdn_agent_owner = false;
	client_handle = notification_hdl_rec->parent_hdl->local_hdl;
	/* retrieve client hdl rec */
	client_rec = ncshm_take_hdl(NCS_SERVICE_ID_NTFA, client_handle);
	if (client_rec == NULL) {
		osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
		TRACE("ncshm_take_hdl client_handle failed");
		rc = SA_AIS_ERR_BAD_HANDLE;
		goto done_give_hdl;
	}
	/*Check CLM membership of node.*/
	if (client_rec->is_stale_client == true) {
		TRACE("Node not CLM member or stale client");
		rc = SA_AIS_ERR_UNAVAILABLE;
		osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
		ncshm_give_hdl(client_handle);
		goto done_give_hdl;
	}
	osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);

	/* Check NTF server availability */
	if ((rc = checkNtfServerState()) != SA_AIS_OK)
		goto done_give_hdls;

	/* Recover if this is invalid handle */
	if (!client_rec->valid) {
		if ((rc = recoverClient(client_rec)) != SA_AIS_OK) {
			ncshm_give_hdl(client_handle);
			ncshm_give_hdl(notificationHandle);
			if (rc == SA_AIS_ERR_BAD_HANDLE) {
				osafassert(
				    pthread_mutex_lock(&ntfa_cb.cb_lock) == 0);
				ntfa_hdl_rec_force_del(&ntfa_cb.client_list,
						       client_rec);
				osafassert(pthread_mutex_unlock(
					       &ntfa_cb.cb_lock) == 0);
				ntfa_shutdown(false);
			}
			goto err_free;
		}
	}
	/**
	 ** Populate a sync MDS message
	 **/
	memset(&msg, 0, sizeof(ntfsv_msg_t));
	msg.type = NTFSV_NTFA_API_MSG;
	msg.info.api_info.type = NTFSV_SEND_NOT_REQ;
	msg.info.api_info.param.send_notification = send_param;
	send_param->client_id = client_rec->ntfs_client_id;
	send_param->notificationType =
	    notification_hdl_rec->ntfNotificationType;
	ntfsv_v_data_cp(&send_param->variable_data,
			&notification_hdl_rec->variable_data);

	/* Check parameters, depending on type */
	switch (notification_hdl_rec->ntfNotificationType) {
	case SA_NTF_TYPE_ALARM:
		TRACE_1("Checking Alarm Notification Parameters");
		/* TODO: assign send_param for all */
		send_param->notification.alarm =
		    notification_hdl_rec->ntfNotification.ntfAlarmNotification;
		ntfHeader = &notification_hdl_rec->ntfNotification
				 .ntfAlarmNotification.notificationHeader;
		rc = checkAlarmParameters(notification_hdl_rec,
					  &send_param->variable_data);
		break;
	case SA_NTF_TYPE_SECURITY_ALARM:
		TRACE_1("Checking Security Alarm Notification Parameters");
		send_param->notification.securityAlarm =
		    notification_hdl_rec->ntfNotification
			.ntfSecurityAlarmNotification;
		ntfHeader =
		    &notification_hdl_rec->ntfNotification
			 .ntfSecurityAlarmNotification.notificationHeader;
		rc = checkSecurityAlarmParameters(notification_hdl_rec,
						  &send_param->variable_data);
		break;
	case SA_NTF_TYPE_STATE_CHANGE:
		TRACE_1("Checking State Change Notification Parameters");
		send_param->notification.stateChange =
		    notification_hdl_rec->ntfNotification
			.ntfStateChangeNotification;
		ntfHeader = &notification_hdl_rec->ntfNotification
				 .ntfStateChangeNotification.notificationHeader;
		rc = checkStateChangeParameters(notification_hdl_rec,
						&send_param->variable_data);
		break;
	case SA_NTF_TYPE_ATTRIBUTE_CHANGE:
		TRACE_1("Checking Attribute Change Notification Parameters");
		send_param->notification.attributeChange =
		    notification_hdl_rec->ntfNotification
			.ntfAttributeChangeNotification;
		ntfHeader =
		    &notification_hdl_rec->ntfNotification
			 .ntfAttributeChangeNotification.notificationHeader;
		rc = checkAttributeChangeParameters(notification_hdl_rec,
						    &send_param->variable_data);
		break;
	case SA_NTF_TYPE_OBJECT_CREATE_DELETE:
		TRACE_1(
		    "Checking Object Create/Delete Notification Parameters");
		send_param->notification.objectCreateDelete =
		    notification_hdl_rec->ntfNotification
			.ntfObjectCreateDeleteNotification;
		ntfHeader =
		    &notification_hdl_rec->ntfNotification
			 .ntfObjectCreateDeleteNotification.notificationHeader;
		rc = checkObjectCreateDeleteParameters(
		    notification_hdl_rec, &send_param->variable_data);
		break;
	default:
		TRACE_1("Unkown notification type");
		rc = SA_AIS_ERR_INVALID_PARAM;
		goto done_give_hdls;
	}

	if (rc != SA_AIS_OK) {
		TRACE_1("Invalid parameter");
		goto done_give_hdls;
	}
	rc = fillSendStruct(ntfHeader, send_param);
	if (rc != SA_AIS_OK) {
		goto done_give_hdls;
	}

	/* Send a sync MDS message to obtain a notification id */
	mds_rc = ntfa_mds_msg_sync_send(&ntfa_cb, &msg, &o_msg, timeout);
	switch (mds_rc) {
	case NCSCC_RC_SUCCESS:
		break;
	case NCSCC_RC_REQ_TIMOUT:
		rc = SA_AIS_ERR_TIMEOUT;
		TRACE("ntfa_mds_msg_sync_send FAILED: %u", rc);
		break;
	case NCSCC_RC_INVALID_INPUT:
		/* This don't work since MDS does not forward error code from
		   encode/decode */
		rc = SA_AIS_ERR_INVALID_PARAM;
		TRACE("ntfa_mds_msg_sync_send FAILED: %u", mds_rc);
		break;
	default:
		TRACE("ntfa_mds_msg_sync_send FAILED: %u", rc);
		rc = SA_AIS_ERR_TRY_AGAIN;
	}
	if (mds_rc != NCSCC_RC_SUCCESS) {
		goto done_give_hdls;
	}

	osafassert(o_msg != NULL);
	if (SA_AIS_OK != o_msg->info.api_resp_info.rc) {
		rc = o_msg->info.api_resp_info.rc;
		TRACE("Bad return status!!! rc = %d", rc);
		/*Check CLM membership of node.*/
		if (rc == SA_AIS_ERR_UNAVAILABLE) {
			TRACE("Node not CLM member or stale client");
		}
		goto done_give_hdls;
	}
	/* Return the notificationId to client from right notification */
	switch (notification_hdl_rec->ntfNotificationType) {
	case SA_NTF_TYPE_ALARM:
		TRACE_1("Not id back: %llu", o_msg->info.api_resp_info.param
						 .send_not_rsp.notificationId);
		TRACE_1(
		    "Not ptr %p:",
		    notification_hdl_rec->ntfNotification.ntfAlarmNotification
			.notificationHeader.notificationId);
		*(notification_hdl_rec->ntfNotification.ntfAlarmNotification
		      .notificationHeader.notificationId) =
		    o_msg->info.api_resp_info.param.send_not_rsp.notificationId;
		break;

	case SA_NTF_TYPE_STATE_CHANGE:
		*notification_hdl_rec->ntfNotification
		     .ntfStateChangeNotification.notificationHeader
		     .notificationId =
		    o_msg->info.api_resp_info.param.send_not_rsp.notificationId;
		break;

	case SA_NTF_TYPE_OBJECT_CREATE_DELETE:
		*notification_hdl_rec->ntfNotification
		     .ntfObjectCreateDeleteNotification.notificationHeader
		     .notificationId =
		    o_msg->info.api_resp_info.param.send_not_rsp.notificationId;
		break;

	case SA_NTF_TYPE_ATTRIBUTE_CHANGE:
		*notification_hdl_rec->ntfNotification
		     .ntfAttributeChangeNotification.notificationHeader
		     .notificationId =
		    o_msg->info.api_resp_info.param.send_not_rsp.notificationId;
		break;

	case SA_NTF_TYPE_SECURITY_ALARM:
		*notification_hdl_rec->ntfNotification
		     .ntfSecurityAlarmNotification.notificationHeader
		     .notificationId =
		    o_msg->info.api_resp_info.param.send_not_rsp.notificationId;
		break;

	default:
		TRACE_1("Unkown notification type");
		rc = SA_AIS_ERR_INVALID_PARAM;
		break;
	}

done_give_hdls:
	if (o_msg)
		ntfa_msg_destroy(o_msg);
	ncshm_give_hdl(client_handle);
done_give_hdl:
	ncshm_give_hdl(notificationHandle);
err_free:
	if (send_param->variable_data.p_base != NULL)
		free(send_param->variable_data.p_base);
	free(send_param);
done:
	TRACE_LEAVE();
	return rc;
}

static ntfa_subscriber_list_t *listItemGet(SaNtfSubscriptionIdT subscriptionId)
{
	ntfa_subscriber_list_t *listPtr = NULL;
	/* Get list item or return NULL */
	for (listPtr = subscriberNoList; listPtr != NULL;
	     listPtr = listPtr->next) {
		TRACE_1("listPtr->SubscriptionId %d",
			listPtr->subscriberListSubscriptionId);
		if (listPtr->subscriberListSubscriptionId == subscriptionId) {
			break;
		}
	}
	return listPtr;
}

static SaNtfHandleT ntfHandleGet(SaNtfSubscriptionIdT subscriptionId)
{
	SaNtfHandleT ntfHandle = 0;
	ntfa_subscriber_list_t *listPtr = NULL;

	osafassert(pthread_mutex_lock(&ntfa_cb.cb_lock) == 0);
	listPtr = listItemGet(subscriptionId);
	if (listPtr) {
		ntfHandle = listPtr->subscriberListNtfHandle;
		TRACE_1("ListItem: sId: %d, ntfhandle: %llu",
			listPtr->subscriberListSubscriptionId, ntfHandle);
	}
	osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
	return ntfHandle;
}

static SaAisErrorT subscriptionListAdd(SaNtfHandleT ntfHandle,
				       SaNtfSubscriptionIdT subscriptionId,
				       ntfsv_filter_ptrs_t filters)
{
	SaAisErrorT rc = SA_AIS_OK;
	ntfa_subscriber_list_t *ntfSubscriberList;
	/* An unique subscriptionId was passed */
	ntfSubscriberList = malloc(sizeof(ntfa_subscriber_list_t));
	if (!ntfSubscriberList) {
		LOG_ER("Out of memory in ntfSubscriberList");
		rc = SA_AIS_ERR_NO_MEMORY;
		goto done;
	}
	/* Add ntfHandle and subscriptionId into list */
	ntfSubscriberList->subscriberListNtfHandle = ntfHandle;
	ntfSubscriberList->subscriberListSubscriptionId = subscriptionId;
	memset(&ntfSubscriberList->filters, 0, sizeof(ntfsv_filter_ptrs_t));
	ntfa_copy_ntf_filter_ptrs(&ntfSubscriberList->filters, &filters);

	osafassert(pthread_mutex_lock(&ntfa_cb.cb_lock) == 0);
	if (NULL == subscriberNoList) {
		subscriberNoList = ntfSubscriberList;
		subscriberNoList->prev = NULL;
		subscriberNoList->next = NULL;
	} else {
		ntfSubscriberList->prev = NULL;
		ntfSubscriberList->next = subscriberNoList;
		subscriberNoList->prev = ntfSubscriberList;
		subscriberNoList = ntfSubscriberList;
	}
	TRACE_1("ADD subscriberNoList: subId %d ntfhandle %llu",
		subscriberNoList->subscriberListSubscriptionId,
		subscriberNoList->subscriberListNtfHandle);
	osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
done:
	return rc;
}

static void subscriberListItemRemove(SaNtfSubscriptionIdT subscriptionId)
{
	ntfa_subscriber_list_t *listPtr = NULL;
	osafassert(pthread_mutex_lock(&ntfa_cb.cb_lock) == 0);
	listPtr = listItemGet(subscriptionId);
	if (listPtr == NULL)
		goto done;
	if (listPtr->next != NULL) {
		listPtr->next->prev = listPtr->prev;
	}

	if (listPtr->prev != NULL) {
		listPtr->prev->next = listPtr->next;
	} else {
		if (listPtr->next != NULL)
			subscriberNoList = listPtr->next;
		else
			subscriberNoList = NULL;
	}
	TRACE_1("REMOVE: listPtr->SubscriptionId %d",
		listPtr->subscriberListSubscriptionId);
	ntfa_del_ntf_filter_ptrs(&listPtr->filters);
	free(listPtr);
done:
	osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
}

/*  3.15.3	Subscriber Operations   */
/*  3.15.3.1	saNtfNotificationSubscribe()  */
SaAisErrorT saNtfNotificationSubscribe(
    const SaNtfNotificationTypeFilterHandlesT *notificationFilterHandles,
    SaNtfSubscriptionIdT subscriptionId)
{
	SaAisErrorT rc;

	ntfa_client_hdl_rec_t *client_hdl_rec = NULL;
	SaNtfHandleT ntfHandle, tmpHandle = 0;
	ntfsv_filter_ptrs_t filters = {NULL, NULL, NULL, NULL, NULL};

	ntfsv_msg_t msg, *o_msg = NULL;
	ntfsv_subscribe_req_t *send_param;
	SaTimeT timeout = NTFS_WAIT_TIME;
	bool recovery_failed = false;
	TRACE_ENTER();

	rc = getFilters(notificationFilterHandles, &filters, &ntfHandle,
			&client_hdl_rec);
	if (rc != SA_AIS_OK) {
		TRACE("getFilters failed");
		goto done;
	}

	/* Check NTF server availability */
	if ((rc = checkNtfServerState()) != SA_AIS_OK)
		goto done;

	/* recovery */
	if (!client_hdl_rec->valid) {
		if ((rc = recoverClient(client_hdl_rec)) != SA_AIS_OK) {
			recovery_failed = true;
			goto done;
		}
	}
	/*Check CLM membership of node.*/
	if (client_hdl_rec->is_stale_client == true) {
		TRACE("Node not CLM member or stale client");
		rc = SA_AIS_ERR_UNAVAILABLE;
		goto done;
	}
	tmpHandle = ntfHandleGet(subscriptionId);
	if (tmpHandle != 0) {
		rc = SA_AIS_ERR_EXIST;
		goto done;
	}
	rc = subscriptionListAdd(ntfHandle, subscriptionId, filters);
	if (rc != SA_AIS_OK) {
		goto done;
	}

	/*      Populate a sync MDS message  */
	memset(&msg, 0, sizeof(ntfsv_msg_t));
	msg.type = NTFSV_NTFA_API_MSG;
	msg.info.api_info.type = NTFSV_SUBSCRIBE_REQ;
	send_param = &msg.info.api_info.param.subscribe;

	send_param->client_id = client_hdl_rec->ntfs_client_id;
	send_param->subscriptionId = subscriptionId;
	send_param->f_rec = filters;

	uint32_t rv;
	/* Send a sync MDS message to obtain a log stream id */
	rv = ntfa_mds_msg_sync_send(&ntfa_cb, &msg, &o_msg, timeout);
	if (rv == NCSCC_RC_SUCCESS) {
		osafassert(o_msg != NULL);
		if (SA_AIS_OK == o_msg->info.api_resp_info.rc) {
			TRACE_1("subscriptionId from server %u",
				o_msg->info.api_resp_info.param.subscribe_rsp
				    .subscriptionId);
		} else {
			rc = o_msg->info.api_resp_info.rc;
			TRACE("Bad return status!!! rc = %d", rc);
		}
	} else {
		if (rv == NCSCC_RC_INVALID_INPUT)
			rc = SA_AIS_ERR_INVALID_PARAM;
		else
			rc = SA_AIS_ERR_TRY_AGAIN;
	}

	if (rc != SA_AIS_OK) {
		subscriberListItemRemove(subscriptionId);
		/*Check CLM membership of node.*/
		if (rc == SA_AIS_ERR_UNAVAILABLE) {
			TRACE("Node not CLM member or stale client");
		}
	}
	if (o_msg)
		ntfa_msg_destroy(o_msg);
done:

	ncshm_give_hdl(ntfHandle);
	if (notificationFilterHandles) {
		if (notificationFilterHandles->attributeChangeFilterHandle)
			ncshm_give_hdl(notificationFilterHandles
					   ->attributeChangeFilterHandle);
		if (notificationFilterHandles->objectCreateDeleteFilterHandle)
			ncshm_give_hdl(notificationFilterHandles
					   ->objectCreateDeleteFilterHandle);
		if (notificationFilterHandles->securityAlarmFilterHandle)
			ncshm_give_hdl(notificationFilterHandles
					   ->securityAlarmFilterHandle);
		if (notificationFilterHandles->stateChangeFilterHandle)
			ncshm_give_hdl(
			    notificationFilterHandles->stateChangeFilterHandle);
		if (notificationFilterHandles->alarmFilterHandle)
			ncshm_give_hdl(
			    notificationFilterHandles->alarmFilterHandle);
	}
	if (recovery_failed && (rc == SA_AIS_ERR_BAD_HANDLE)) {
		osafassert(pthread_mutex_lock(&ntfa_cb.cb_lock) == 0);
		ntfa_hdl_rec_force_del(&ntfa_cb.client_list, client_hdl_rec);
		osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
		ntfa_shutdown(false);
	}
	TRACE_LEAVE();
	return rc;
}

/*  Producer Operations  */
/*  ===================  */
/**
 * This API internally allocates memory for an object create delete notification
 * and initializes the notification structure.
 *
 * @param ntfHandle
 * @param notification
 * @param numCorrelatedNotifications
 * @param lengthAdditionalText
 * @param numAdditionalInfo
 * @param numAttributes
 * @param variableDataSize
 *
 * @return SaAisErrorT
 */
SaAisErrorT saNtfObjectCreateDeleteNotificationAllocate(
    SaNtfHandleT ntfHandle, SaNtfObjectCreateDeleteNotificationT *notification,
    SaUint16T numCorrelatedNotifications, SaUint16T lengthAdditionalText,
    SaUint16T numAdditionalInfo, SaUint16T numAttributes,
    SaInt16T variableDataSize)
{
	SaAisErrorT rc;
	ntfa_notification_hdl_rec_t *notification_hdl_rec;
	ntfa_client_hdl_rec_t *hdl_rec;

	TRACE_ENTER();

	/* Input pointer check */
	if (notification == NULL) {
		TRACE("NULL pointer in notification struct inparameter");
		rc = SA_AIS_ERR_INVALID_PARAM;
		goto done;
	}

	/* retrieve hdl rec */
	hdl_rec = ncshm_take_hdl(NCS_SERVICE_ID_NTFA, ntfHandle);
	if (hdl_rec == NULL) {
		TRACE("ncshm_take_hdl failed");
		rc = SA_AIS_ERR_BAD_HANDLE;
		goto done;
	}

	/**                 Lock ntfa_CB                 **/
	osafassert(pthread_mutex_lock(&ntfa_cb.cb_lock) == 0);
	/*Check CLM membership of node.*/
	if (hdl_rec->is_stale_client == true) {
		TRACE("Node not CLM member or stale client");
		osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
		rc = SA_AIS_ERR_UNAVAILABLE;
		goto done_give_hdl;
	}
	notification_hdl_rec =
	    ntfa_notification_hdl_rec_add(&hdl_rec, variableDataSize, &rc);
	if (notification_hdl_rec == NULL) {
		osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
		goto done_give_hdl;
	}
	TRACE_1("notification_hdl_rec = %u",
		notification_hdl_rec->notification_hdl);
	notification_hdl_rec->ntfNotification.ntfObjectCreateDeleteNotification
	    .notificationHandle =
	    (SaUint64T)notification_hdl_rec->notification_hdl;
	notification_hdl_rec->parent_hdl = hdl_rec;
	notification_hdl_rec->ntfNotificationType =
	    SA_NTF_TYPE_OBJECT_CREATE_DELETE;

	/* Allocate all data fields in the header */
	rc = ntfsv_alloc_ntf_header(
	    &notification_hdl_rec->ntfNotification
		 .ntfObjectCreateDeleteNotification.notificationHeader,
	    numCorrelatedNotifications, lengthAdditionalText,
	    numAdditionalInfo);
	if (rc != SA_AIS_OK) {
		goto error_put;
	}

	/* Allocate all object create/delete specific data fields */
	rc = ntfsv_alloc_ntf_obj_create_del(
	    &notification_hdl_rec->ntfNotification
		 .ntfObjectCreateDeleteNotification,
	    numAttributes);
	if (rc != SA_AIS_OK) {
		goto error_put;
	}

	/* A copy to the client */
	*notification = notification_hdl_rec->ntfNotification
			    .ntfObjectCreateDeleteNotification;
	/**                  UnLock ntfa_CB            **/
	osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
	goto done_give_hdl;

error_put:
	if (NCSCC_RC_SUCCESS !=
	    ntfa_notification_hdl_rec_del(&hdl_rec->notification_list,
					  notification_hdl_rec)) {
		TRACE("Unable to delete notifiction record");
	}
	osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
done_give_hdl:
	ncshm_give_hdl(ntfHandle);
done:
	TRACE_LEAVE();
	return rc;
}

/*  3.14.2      saNtfAttributeChangeNotificationAllocate()  */
SaAisErrorT saNtfAttributeChangeNotificationAllocate(
    SaNtfHandleT ntfHandle, SaNtfAttributeChangeNotificationT *notification,
    SaUint16T numCorrelatedNotifications, SaUint16T lengthAdditionalText,
    SaUint16T numAdditionalInfo, SaUint16T numAttributes,
    SaInt16T variableDataSize)
{
	SaAisErrorT rc;
	ntfa_notification_hdl_rec_t *notification_hdl_rec;
	ntfa_client_hdl_rec_t *hdl_rec;
	TRACE_ENTER();
	/* Input pointer check */
	if (notification == NULL) {
		TRACE("NULL pointer in notification struct inparameter");
		rc = SA_AIS_ERR_INVALID_PARAM;
		goto done;
	}

	/* retrieve hdl rec */
	hdl_rec = ncshm_take_hdl(NCS_SERVICE_ID_NTFA, ntfHandle);
	if (hdl_rec == NULL) {
		TRACE("ncshm_take_hdl failed");
		rc = SA_AIS_ERR_BAD_HANDLE;
		goto done;
	}
	/**                 Lock ntfa_CB                 **/
	osafassert(pthread_mutex_lock(&ntfa_cb.cb_lock) == 0);
	/*Check CLM membership of node.*/
	if (hdl_rec->is_stale_client == true) {
		TRACE("Node not CLM member or stale client");
		osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
		rc = SA_AIS_ERR_UNAVAILABLE;
		goto done_give_hdl;
	}
	notification_hdl_rec =
	    ntfa_notification_hdl_rec_add(&hdl_rec, variableDataSize, &rc);
	if (notification_hdl_rec == NULL) {
		osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
		goto done_give_hdl;
	}
	TRACE_1("notification_hdl_rec = %u",
		notification_hdl_rec->notification_hdl);

	notification_hdl_rec->ntfNotification.ntfAttributeChangeNotification
	    .notificationHandle =
	    (SaUint64T)notification_hdl_rec->notification_hdl;
	notification_hdl_rec->parent_hdl = hdl_rec;
	notification_hdl_rec->ntfNotificationType =
	    SA_NTF_TYPE_ATTRIBUTE_CHANGE;

	/* Allocate all data fields in the header */
	rc = ntfsv_alloc_ntf_header(
	    &notification_hdl_rec->ntfNotification
		 .ntfAttributeChangeNotification.notificationHeader,
	    numCorrelatedNotifications, lengthAdditionalText,
	    numAdditionalInfo);
	if (rc != SA_AIS_OK) {
		goto error_put;
	}

	/* Allocate all object create/delete specific data fields */
	rc = ntfsv_alloc_ntf_attr_change(&notification_hdl_rec->ntfNotification
					      .ntfAttributeChangeNotification,
					 numAttributes);
	if (rc != SA_AIS_OK) {
		goto error_put;
	}
	*notification = notification_hdl_rec->ntfNotification
			    .ntfAttributeChangeNotification;
	osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
	goto done_give_hdl;

error_put:
	if (NCSCC_RC_SUCCESS !=
	    ntfa_notification_hdl_rec_del(&hdl_rec->notification_list,
					  notification_hdl_rec)) {
		TRACE("Unable to delete notifiction record");
	}
	osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
done_give_hdl:
	ncshm_give_hdl(ntfHandle);
done:
	TRACE_LEAVE();
	return rc;
}

/*  3.14.3      saNtfStateChangeNotificationAllocate()  */
SaAisErrorT saNtfStateChangeNotificationAllocate(
    SaNtfHandleT ntfHandle, SaNtfStateChangeNotificationT *notification,
    SaUint16T numCorrelatedNotifications, SaUint16T lengthAdditionalText,
    SaUint16T numAdditionalInfo, SaUint16T numStateChanges,
    SaInt16T variableDataSize)
{
	SaAisErrorT rc;
	ntfa_notification_hdl_rec_t *notification_hdl_rec;
	ntfa_client_hdl_rec_t *hdl_rec;
	TRACE_ENTER();

	/* Input pointer check */
	if (notification == NULL) {
		TRACE("NULL pointer in notification struct inparameter");
		rc = SA_AIS_ERR_INVALID_PARAM;
		goto done;
	}

	/* retrieve hdl rec */
	hdl_rec = ncshm_take_hdl(NCS_SERVICE_ID_NTFA, ntfHandle);
	if (hdl_rec == NULL) {
		TRACE("ncshm_take_hdl failed");
		rc = SA_AIS_ERR_BAD_HANDLE;
		goto done;
	}

	/**                 Lock ntfa_CB                 **/
	osafassert(pthread_mutex_lock(&ntfa_cb.cb_lock) == 0);
	/*Check CLM membership of node.*/
	if (hdl_rec->is_stale_client == true) {
		TRACE("Node not CLM member or stale client");
		osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
		rc = SA_AIS_ERR_UNAVAILABLE;
		goto done_give_hdl;
	}
	notification_hdl_rec =
	    ntfa_notification_hdl_rec_add(&hdl_rec, variableDataSize, &rc);
	if (notification_hdl_rec == NULL) {
		osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
		goto done_give_hdl;
	}
	TRACE_1("notification_hdl_rec = %u",
		notification_hdl_rec->notification_hdl);

	notification_hdl_rec->ntfNotification.ntfStateChangeNotification
	    .notificationHandle =
	    (SaUint64T)notification_hdl_rec->notification_hdl;
	notification_hdl_rec->parent_hdl = hdl_rec;
	notification_hdl_rec->ntfNotificationType = SA_NTF_TYPE_STATE_CHANGE;

	/* Allocate all data fields in the header */
	rc = ntfsv_alloc_ntf_header(
	    &notification_hdl_rec->ntfNotification.ntfStateChangeNotification
		 .notificationHeader,
	    numCorrelatedNotifications, lengthAdditionalText,
	    numAdditionalInfo);
	if (rc != SA_AIS_OK) {
		goto error_put;
	}

	/* Allocate all object create/delete specific data fields */
	rc = ntfsv_alloc_ntf_state_change(
	    &notification_hdl_rec->ntfNotification.ntfStateChangeNotification,
	    numStateChanges);
	if (rc != SA_AIS_OK) {
		goto error_put;
	}
	*notification =
	    notification_hdl_rec->ntfNotification.ntfStateChangeNotification;
	osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
	goto done_give_hdl;

error_put:
	if (NCSCC_RC_SUCCESS !=
	    ntfa_notification_hdl_rec_del(&hdl_rec->notification_list,
					  notification_hdl_rec)) {
		TRACE("Unable to delete notifiction record");
	}
	osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);

done_give_hdl:
	ncshm_give_hdl(ntfHandle);
done:
	TRACE_LEAVE();
	return rc;
}

/*  3.14.5	saNtfSecurityAlarmNotificationAllocate()  */
SaAisErrorT saNtfSecurityAlarmNotificationAllocate(
    SaNtfHandleT ntfHandle, SaNtfSecurityAlarmNotificationT *notification,
    SaUint16T numCorrelatedNotifications, SaUint16T lengthAdditionalText,
    SaUint16T numAdditionalInfo, SaInt16T variableDataSize)
{
	SaAisErrorT rc;
	ntfa_notification_hdl_rec_t *notification_hdl_rec;
	ntfa_client_hdl_rec_t *hdl_rec;
	TRACE_ENTER();
	/* Input pointer check */
	if (notification == NULL) {
		TRACE("NULL pointer in notification struct inparameter");
		rc = SA_AIS_ERR_INVALID_PARAM;
		goto done;
	}

	/* retrieve hdl rec */
	hdl_rec = ncshm_take_hdl(NCS_SERVICE_ID_NTFA, ntfHandle);
	if (hdl_rec == NULL) {
		TRACE("ncshm_take_hdl failed");
		rc = SA_AIS_ERR_BAD_HANDLE;
		goto done;
	}
	/**                 Lock ntfa_CB                 **/
	osafassert(pthread_mutex_lock(&ntfa_cb.cb_lock) == 0);
	/*Check CLM membership of node.*/
	if (hdl_rec->is_stale_client == true) {
		TRACE("Node not CLM member or stale client");
		osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
		rc = SA_AIS_ERR_UNAVAILABLE;
		goto done_give_hdl;
	}
	notification_hdl_rec =
	    ntfa_notification_hdl_rec_add(&hdl_rec, variableDataSize, &rc);
	if (notification_hdl_rec == NULL) {
		osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
		goto done_give_hdl;
	}
	TRACE_1("notification_hdl_rec = %u",
		notification_hdl_rec->notification_hdl);

	notification_hdl_rec->ntfNotification.ntfSecurityAlarmNotification
	    .notificationHandle =
	    (SaUint64T)notification_hdl_rec->notification_hdl;
	notification_hdl_rec->parent_hdl = hdl_rec;
	notification_hdl_rec->ntfNotificationType = SA_NTF_TYPE_SECURITY_ALARM;

	/* Allocate all data fields in the header */
	rc = ntfsv_alloc_ntf_header(
	    &notification_hdl_rec->ntfNotification.ntfSecurityAlarmNotification
		 .notificationHeader,
	    numCorrelatedNotifications, lengthAdditionalText,
	    numAdditionalInfo);
	if (rc != SA_AIS_OK) {
		goto error_put;
	}
	/* Allocate all object create/delete specific data fields */
	rc = ntfsv_alloc_ntf_security_alarm(
	    &notification_hdl_rec->ntfNotification
		 .ntfSecurityAlarmNotification);
	if (rc != SA_AIS_OK) {
		goto error_put;
	}
	*notification =
	    notification_hdl_rec->ntfNotification.ntfSecurityAlarmNotification;
	osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
	goto done_give_hdl;

error_put:
	if (NCSCC_RC_SUCCESS !=
	    ntfa_notification_hdl_rec_del(&hdl_rec->notification_list,
					  notification_hdl_rec)) {
		TRACE("Unable to delete notification record");
	}
	osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);

done_give_hdl:
	ncshm_give_hdl(ntfHandle);
done:
	TRACE_LEAVE();
	return rc;
}

/*  3.14.6	saNtfPtrValAllocate()  */
SaAisErrorT saNtfPtrValAllocate(SaNtfNotificationHandleT notificationHandle,
				SaUint16T dataSize, void **dataPtr,
				SaNtfValueT *value)
{
	SaAisErrorT rc = SA_AIS_OK;
	unsigned int client_handle;
	ntfa_client_hdl_rec_t *client_rec;
	ntfa_notification_hdl_rec_t *notification_hdl_rec;
	TRACE_ENTER();
	if (dataPtr == NULL || value == NULL) {
		rc = SA_AIS_ERR_INVALID_PARAM;
		goto done;
	}

	notification_hdl_rec =
	    ncshm_take_hdl(NCS_SERVICE_ID_NTFA, notificationHandle);
	if (notification_hdl_rec == NULL) {
		TRACE("ncshm_take_hdl notificationHandle failed");
		rc = SA_AIS_ERR_BAD_HANDLE;
		goto done;
	}

	client_handle = notification_hdl_rec->parent_hdl->local_hdl;
	/* retrieve client hdl rec */
	client_rec = ncshm_take_hdl(NCS_SERVICE_ID_NTFA, client_handle);
	if (client_rec == NULL) {
		TRACE("ncshm_take_hdl client_handle failed");
		rc = SA_AIS_ERR_BAD_HANDLE;
		goto done_give_hdl;
	}

	osafassert(pthread_mutex_lock(&ntfa_cb.cb_lock) == 0);
	/*Check CLM membership of node.*/
	if (client_rec->is_stale_client == true) {
		TRACE("Node not CLM member or stale client");
		osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
		ncshm_give_hdl(client_handle);
		rc = SA_AIS_ERR_UNAVAILABLE;
		goto done_give_hdl;
	}
	rc = ntfsv_ptr_val_alloc(&notification_hdl_rec->variable_data, value,
				 dataSize, dataPtr);
	osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
	ncshm_give_hdl(client_handle);
done_give_hdl:
	ncshm_give_hdl(notificationHandle);
done:
	TRACE_LEAVE();
	return rc;
}

/*  3.14.7	saNtfArrayValAllocate()  */
SaAisErrorT saNtfArrayValAllocate(SaNtfNotificationHandleT notificationHandle,
				  SaUint16T numElements, SaUint16T elementSize,
				  void **arrayPtr, SaNtfValueT *value)
{
	SaAisErrorT rc = SA_AIS_OK;
	unsigned int client_handle;
	ntfa_client_hdl_rec_t *client_rec;
	ntfa_notification_hdl_rec_t *notification_hdl_rec;
	TRACE_ENTER();
	if (arrayPtr == NULL || value == NULL) {
		rc = SA_AIS_ERR_INVALID_PARAM;
		goto done;
	}

	notification_hdl_rec =
	    ncshm_take_hdl(NCS_SERVICE_ID_NTFA, notificationHandle);
	if (notification_hdl_rec == NULL) {
		TRACE("ncshm_take_hdl notificationHandle failed");
		rc = SA_AIS_ERR_BAD_HANDLE;
		goto done;
	}

	client_handle = notification_hdl_rec->parent_hdl->local_hdl;
	/* retrieve client hdl rec */
	client_rec = ncshm_take_hdl(NCS_SERVICE_ID_NTFA, client_handle);
	if (client_rec == NULL) {
		TRACE("ncshm_take_hdl client_handle failed");
		rc = SA_AIS_ERR_BAD_HANDLE;
		goto done_give_hdl;
	}
	osafassert(pthread_mutex_lock(&ntfa_cb.cb_lock) == 0);
	/*Check CLM membership of node.*/
	if (client_rec->is_stale_client == true) {
		TRACE("Node not CLM member or stale client");
		osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
		ncshm_give_hdl(client_handle);
		rc = SA_AIS_ERR_UNAVAILABLE;
		goto done_give_hdl;
	}
	rc = ntfsv_array_val_alloc(&notification_hdl_rec->variable_data, value,
				   numElements, elementSize, arrayPtr);
	osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
	ncshm_give_hdl(client_handle);
done_give_hdl:
	ncshm_give_hdl(notificationHandle);
done:
	TRACE_LEAVE();
	return rc;
}

/*  3.15	Consumer Operations  */
/*  3.15.2	Common Operations  */
/*  3.15.2.1	saNtfLocalizedMessageGet()  */
SaAisErrorT
saNtfLocalizedMessageGet(SaNtfNotificationHandleT notificationHandle,
			 SaStringT *message)
{
	return SA_AIS_ERR_NOT_SUPPORTED;
}

/*  3.15.2.2 saNtfLocalizedMessageFree() */
SaAisErrorT saNtfLocalizedMessageFree(SaStringT message)
{
	return SA_AIS_ERR_NOT_SUPPORTED;
}

/*  3.15.2.3	saNtfPtrValGet()  */
SaAisErrorT saNtfPtrValGet(SaNtfNotificationHandleT notificationHandle,
			   const SaNtfValueT *value, void **dataPtr,
			   SaUint16T *dataSize)
{
	SaAisErrorT rc = SA_AIS_OK;
	unsigned int client_handle;
	ntfa_client_hdl_rec_t *client_rec;
	ntfa_notification_hdl_rec_t *notification_hdl_rec;
	SaNtfValueTypeT type;
	TRACE_ENTER();
	if (notificationHandle == 0) {
		rc = SA_AIS_ERR_BAD_HANDLE;
		goto done;
	}

	if (dataPtr == NULL || value == NULL || dataSize == 0 ||
	    value->ptrVal.dataSize == 0) {
		rc = SA_AIS_ERR_INVALID_PARAM;
		goto done;
	}

	notification_hdl_rec =
	    ncshm_take_hdl(NCS_SERVICE_ID_NTFA, notificationHandle);
	if (notification_hdl_rec == NULL) {
		TRACE("ncshm_take_hdl notificationHandle failed");
		rc = SA_AIS_ERR_BAD_HANDLE;
		goto done;
	}

	if (getTypeFromValue(notification_hdl_rec, value, &type) != SA_AIS_OK) {
		TRACE("SaNtfValueT@value is not found in notification");
		rc = SA_AIS_ERR_INVALID_PARAM;
		goto done_give_hdl;
	}

	if (type != SA_NTF_VALUE_LDAP_NAME && type != SA_NTF_VALUE_STRING &&
	    type != SA_NTF_VALUE_IPADDRESS && type != SA_NTF_VALUE_BINARY) {
		TRACE(
		    "type(%d) of SaNtfValueT@value is not valid for saNtfPtrValGet()",
		    type);
		rc = SA_AIS_ERR_INVALID_PARAM;
		goto done_give_hdl;
	}

	if (type == SA_NTF_VALUE_LDAP_NAME) {
		SaNameT name;
		osaf_extended_name_lend(
		    (SaConstStringT)notification_hdl_rec->variable_data.p_base +
			value->ptrVal.dataOffset + 2,
		    &name);
		if (!ntfsv_sanamet_is_valid(&name)) {
			TRACE("Invalid value of type(%d)", type);
			rc = SA_AIS_ERR_NAME_TOO_LONG;
			goto done_give_hdl;
		}
	}

	client_handle = notification_hdl_rec->parent_hdl->local_hdl;
	/* retrieve client hdl rec */
	client_rec = ncshm_take_hdl(NCS_SERVICE_ID_NTFA, client_handle);
	if (client_rec == NULL) {
		TRACE("ncshm_take_hdl client_handle failed");
		rc = SA_AIS_ERR_BAD_HANDLE;
		goto done_give_hdl;
	}

	osafassert(pthread_mutex_lock(&ntfa_cb.cb_lock) == 0);
	/*Check CLM membership of node.*/
	if (client_rec->is_stale_client == true) {
		TRACE("Node not CLM member or stale client");
		osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
		ncshm_give_hdl(client_handle);
		rc = SA_AIS_ERR_UNAVAILABLE;
		goto done_give_hdl;
	}
	rc = ntfsv_ptr_val_get(&notification_hdl_rec->variable_data, value,
			       dataPtr, dataSize);
	osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
	ncshm_give_hdl(client_handle);
done_give_hdl:
	ncshm_give_hdl(notificationHandle);
done:
	TRACE_LEAVE();
	return rc;
}

/*  3.15.2.4	saNtfArrayValGet()  */
SaAisErrorT saNtfArrayValGet(SaNtfNotificationHandleT notificationHandle,
			     const SaNtfValueT *value, void **arrayPtr,
			     SaUint16T *numElements, SaUint16T *elementSize)
{
	SaAisErrorT rc = SA_AIS_OK;
	unsigned int client_handle;
	ntfa_client_hdl_rec_t *client_rec;
	ntfa_notification_hdl_rec_t *notification_hdl_rec;
	TRACE_ENTER();
	if (notificationHandle == 0) {
		rc = SA_AIS_ERR_BAD_HANDLE;
		goto done;
	}
	if (arrayPtr == NULL || value == NULL || numElements == NULL ||
	    elementSize == NULL || value->arrayVal.elementSize == 0 ||
	    value->arrayVal.numElements == 0) {
		rc = SA_AIS_ERR_INVALID_PARAM;
		goto done;
	}

	notification_hdl_rec =
	    ncshm_take_hdl(NCS_SERVICE_ID_NTFA, notificationHandle);
	if (notification_hdl_rec == NULL) {
		TRACE("ncshm_take_hdl notificationHandle failed");
		rc = SA_AIS_ERR_BAD_HANDLE;
		goto done;
	}

	client_handle = notification_hdl_rec->parent_hdl->local_hdl;
	/* retrieve client hdl rec */
	client_rec = ncshm_take_hdl(NCS_SERVICE_ID_NTFA, client_handle);
	if (client_rec == NULL) {
		TRACE("ncshm_take_hdl client_handle failed");
		rc = SA_AIS_ERR_BAD_HANDLE;
		goto done_give_hdl;
	}

	osafassert(pthread_mutex_lock(&ntfa_cb.cb_lock) == 0);
	/*Check CLM membership of node.*/
	if (client_rec->is_stale_client == true) {
		TRACE("Node not CLM member or stale client");
		osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
		ncshm_give_hdl(client_handle);
		rc = SA_AIS_ERR_UNAVAILABLE;
		goto done_give_hdl;
	}
	rc = ntfsv_array_val_get(&notification_hdl_rec->variable_data, value,
				 arrayPtr, numElements, elementSize);
	ncshm_give_hdl(client_handle);
	osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
done_give_hdl:
	ncshm_give_hdl(notificationHandle);
done:
	TRACE_LEAVE();
	return rc;
}

/* 3.15.2.5  saNtfObjectCreateDeleteNotificationFilterAllocate() */
SaAisErrorT saNtfObjectCreateDeleteNotificationFilterAllocate(
    SaNtfHandleT ntfHandle,
    SaNtfObjectCreateDeleteNotificationFilterT *notificationFilter,
    SaUint16T numEventTypes, SaUint16T numNotificationObjects,
    SaUint16T numNotifyingObjects, SaUint16T numNotificationClassIds,
    SaUint16T numSourceIndicators)
{
	SaAisErrorT rc = SA_AIS_OK;
	ntfa_client_hdl_rec_t *client_rec;
	ntfa_filter_hdl_rec_t *filter_hdl_rec;
	SaNtfObjectCreateDeleteNotificationFilterT *new_filter;
	SaNtfNotificationFilterHeaderT *new_header;
	TRACE_ENTER();
	if (notificationFilter == NULL) {
		TRACE_1("notificationFilter == NULL");
		rc = SA_AIS_ERR_INVALID_PARAM;
		goto done;
	}

	/* retrieve hdl rec */
	client_rec = ncshm_take_hdl(NCS_SERVICE_ID_NTFA, ntfHandle);
	if (client_rec == NULL) {
		TRACE("ncshm_take_hdl failed");
		rc = SA_AIS_ERR_BAD_HANDLE;
		goto done;
	}

	/**                 Lock ntfa_CB                 **/
	osafassert(pthread_mutex_lock(&ntfa_cb.cb_lock) == 0);
	/*Check CLM membership of node.*/
	if (client_rec->is_stale_client == true) {
		TRACE("Node not CLM member or stale client");
		osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
		rc = SA_AIS_ERR_UNAVAILABLE;
		goto done_give_hdl;
	}

	filter_hdl_rec = ntfa_filter_hdl_rec_add(&client_rec);
	if (filter_hdl_rec == NULL) {
		osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
		TRACE_1("ntfa_filter_hdl_rec_add failed");
		rc = SA_AIS_ERR_NO_MEMORY;
		goto done_give_hdl;
	}
	TRACE_1("filter_hdl = %llu", filter_hdl_rec->filter_hdl);

	/* Set the instance handle */
	filter_hdl_rec->ntfHandle = ntfHandle;
	filter_hdl_rec->ntfType = SA_NTF_TYPE_OBJECT_CREATE_DELETE;

	new_filter = &(filter_hdl_rec->notificationFilter
			   .objectCreateDeleteNotificationfilter);
	new_filter->notificationFilterHandle = filter_hdl_rec->filter_hdl;
	new_header = &(
	    filter_hdl_rec->notificationFilter
		.objectCreateDeleteNotificationfilter.notificationFilterHeader);

	/* Allocate memory */
	rc = ntfsv_filter_header_alloc(
	    new_header, numEventTypes, numNotificationObjects,
	    numNotifyingObjects, numNotificationClassIds);
	if (rc != SA_AIS_OK) {
		goto done_rec_del;
	}
	rc = ntfsv_filter_obj_cr_del_alloc(new_filter, numSourceIndicators);
	if (rc != SA_AIS_OK) {
		goto done_rec_del;
	}
	/* initialize the Client struct data */
	*notificationFilter = *new_filter;
	osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);

done_give_hdl:
	ncshm_give_hdl(ntfHandle);
done:
	TRACE_LEAVE();
	return rc;

done_rec_del:
	if (NCSCC_RC_SUCCESS !=
	    ntfa_filter_hdl_rec_del(&client_rec->filter_list, filter_hdl_rec)) {
		TRACE("Unable to delete notifiction record");
		rc = SA_AIS_ERR_LIBRARY;
	}
	osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
	TRACE("ERROR, rc = %d!!!", rc);
	goto done_give_hdl;
}

/*  3.15.2.6	saNtfAttributeChangeNotificationFilterAllocate()  */
SaAisErrorT saNtfAttributeChangeNotificationFilterAllocate(
    SaNtfHandleT ntfHandle,
    SaNtfAttributeChangeNotificationFilterT *notificationFilter,
    SaUint16T numEventTypes, SaUint16T numNotificationObjects,
    SaUint16T numNotifyingObjects, SaUint16T numNotificationClassIds,
    SaUint16T numSourceIndicators)
{
	SaAisErrorT rc = SA_AIS_OK;
	ntfa_client_hdl_rec_t *client_rec;
	ntfa_filter_hdl_rec_t *filter_hdl_rec;
	SaNtfAttributeChangeNotificationFilterT *new_filter;
	SaNtfNotificationFilterHeaderT *new_header;
	TRACE_ENTER();
	if (notificationFilter == NULL) {
		TRACE_1("notificationFilter == NULL");
		rc = SA_AIS_ERR_INVALID_PARAM;
		goto done;
	}

	/* retrieve hdl rec */
	client_rec = ncshm_take_hdl(NCS_SERVICE_ID_NTFA, ntfHandle);
	if (client_rec == NULL) {
		TRACE("ncshm_take_hdl failed");
		rc = SA_AIS_ERR_BAD_HANDLE;
		goto done;
	}

	/**                 Lock ntfa_CB                 **/
	osafassert(pthread_mutex_lock(&ntfa_cb.cb_lock) == 0);
	/*Check CLM membership of node.*/
	if (client_rec->is_stale_client == true) {
		TRACE("Node not CLM member or stale client");
		osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
		rc = SA_AIS_ERR_UNAVAILABLE;
		goto done_give_hdl;
	}

	filter_hdl_rec = ntfa_filter_hdl_rec_add(&client_rec);
	if (filter_hdl_rec == NULL) {
		osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
		TRACE_1("ntfa_filter_hdl_rec_add failed");
		rc = SA_AIS_ERR_NO_MEMORY;
		goto done_give_hdl;
	}
	TRACE_1("filter_hdl = %llu", filter_hdl_rec->filter_hdl);

	/* Set the instance handle */
	filter_hdl_rec->ntfHandle = ntfHandle;
	filter_hdl_rec->ntfType = SA_NTF_TYPE_ATTRIBUTE_CHANGE;

	new_filter = &(filter_hdl_rec->notificationFilter
			   .attributeChangeNotificationfilter);
	new_filter->notificationFilterHandle = filter_hdl_rec->filter_hdl;
	new_header =
	    &(filter_hdl_rec->notificationFilter
		  .attributeChangeNotificationfilter.notificationFilterHeader);

	/* Allocate memory */
	rc = ntfsv_filter_header_alloc(
	    new_header, numEventTypes, numNotificationObjects,
	    numNotifyingObjects, numNotificationClassIds);
	if (rc != SA_AIS_OK) {
		goto done_rec_del;
	}
	rc = ntfsv_filter_attr_change_alloc(new_filter, numSourceIndicators);
	if (rc != SA_AIS_OK) {
		goto done_rec_del;
	}
	/* initialize the Client struct data */
	*notificationFilter = *new_filter;
	/**                  UnLock ntfa_CB            **/
	osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);

done_give_hdl:
	ncshm_give_hdl(ntfHandle);
done:
	TRACE_LEAVE();
	return rc;

done_rec_del:
	if (NCSCC_RC_SUCCESS !=
	    ntfa_filter_hdl_rec_del(&client_rec->filter_list, filter_hdl_rec)) {
		TRACE("Unable to delete notifiction record");
		rc = SA_AIS_ERR_LIBRARY;
	}
	osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);

	free(new_filter->sourceIndicators);
	TRACE("ERROR, rc = %d!!!", rc);
	goto done_give_hdl;
}

/*  3.15.2.7	saNtfStateChangeNotificationFilterAllocate()  */
SaAisErrorT saNtfStateChangeNotificationFilterAllocate(
    SaNtfHandleT ntfHandle,
    SaNtfStateChangeNotificationFilterT *notificationFilter,
    SaUint16T numEventTypes, SaUint16T numNotificationObjects,
    SaUint16T numNotifyingObjects, SaUint16T numNotificationClassIds,
    SaUint16T numSourceIndicators, SaUint16T numChangedStates)
{
	SaAisErrorT rc = SA_AIS_OK;
	ntfa_client_hdl_rec_t *client_rec;
	ntfa_filter_hdl_rec_t *filter_hdl_rec;
	SaNtfStateChangeNotificationFilterT *new_filter;
	SaNtfNotificationFilterHeaderT *new_header;
	TRACE_ENTER();
	if (notificationFilter == NULL) {
		TRACE_1("notificationFilter == NULL");
		rc = SA_AIS_ERR_INVALID_PARAM;
		goto done;
	}

	/* retrieve hdl rec */
	client_rec = ncshm_take_hdl(NCS_SERVICE_ID_NTFA, ntfHandle);
	if (client_rec == NULL) {
		TRACE("ncshm_take_hdl failed");
		rc = SA_AIS_ERR_BAD_HANDLE;
		goto done;
	}

	/**                 Lock ntfa_CB                 **/
	osafassert(pthread_mutex_lock(&ntfa_cb.cb_lock) == 0);
	/*Check CLM membership of node.*/
	if (client_rec->is_stale_client == true) {
		TRACE("Node not CLM member or stale client");
		osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
		rc = SA_AIS_ERR_UNAVAILABLE;
		goto done_give_hdl;
	}

	filter_hdl_rec = ntfa_filter_hdl_rec_add(&client_rec);
	if (filter_hdl_rec == NULL) {
		osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
		TRACE_1("ntfa_filter_hdl_rec_add failed");
		rc = SA_AIS_ERR_NO_MEMORY;
		goto done_give_hdl;
	}
	TRACE_1("filter_hdl = %llu", filter_hdl_rec->filter_hdl);

	/* Set the instance handle */
	filter_hdl_rec->ntfHandle = ntfHandle;
	filter_hdl_rec->ntfType = SA_NTF_TYPE_STATE_CHANGE;

	new_filter =
	    &(filter_hdl_rec->notificationFilter.stateChangeNotificationfilter);
	new_filter->notificationFilterHandle = filter_hdl_rec->filter_hdl;
	new_header =
	    &(filter_hdl_rec->notificationFilter.stateChangeNotificationfilter
		  .notificationFilterHeader);

	/* Allocate memory */
	rc = ntfsv_filter_header_alloc(
	    new_header, numEventTypes, numNotificationObjects,
	    numNotifyingObjects, numNotificationClassIds);
	if (rc != SA_AIS_OK) {
		goto done_rec_del;
	}
	rc = ntfsv_filter_state_ch_alloc(new_filter, numSourceIndicators,
					 numChangedStates);
	if (rc != SA_AIS_OK) {
		goto done_rec_del;
	}
	/* initialize the Client struct data */
	*notificationFilter = *new_filter;
	osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);

done_give_hdl:
	ncshm_give_hdl(ntfHandle);
done:
	TRACE_LEAVE();
	return rc;

done_rec_del:
	if (NCSCC_RC_SUCCESS !=
	    ntfa_filter_hdl_rec_del(&client_rec->filter_list, filter_hdl_rec)) {
		TRACE("Unable to delete notifiction record");
		rc = SA_AIS_ERR_LIBRARY;
	}
	osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
	ntfsv_filter_state_ch_free(new_filter, false);
	TRACE("ERROR, rc = %d!!!", rc);
	goto done_give_hdl;
}

/*  3.15.2.8	saNtfAlarmNotificationFilterAllocate()  */
SaAisErrorT saNtfAlarmNotificationFilterAllocate(
    SaNtfHandleT ntfHandle, SaNtfAlarmNotificationFilterT *notificationFilter,
    SaUint16T numEventTypes, SaUint16T numNotificationObjects,
    SaUint16T numNotifyingObjects, SaUint16T numNotificationClassIds,
    SaUint16T numProbableCauses, SaUint16T numPerceivedSeverities,
    SaUint16T numTrends)
{
	SaAisErrorT rc = SA_AIS_OK;
	ntfa_client_hdl_rec_t *client_rec;
	ntfa_filter_hdl_rec_t *filter_hdl_rec;
	SaNtfAlarmNotificationFilterT *new_filter;
	SaNtfNotificationFilterHeaderT *new_header;
	TRACE_ENTER();
	if (notificationFilter == NULL) {
		TRACE_1("notificationFilter == NULL");
		rc = SA_AIS_ERR_INVALID_PARAM;
		goto done;
	}

	/* retrieve hdl rec */
	client_rec = ncshm_take_hdl(NCS_SERVICE_ID_NTFA, ntfHandle);
	if (client_rec == NULL) {
		TRACE("ncshm_take_hdl failed");
		rc = SA_AIS_ERR_BAD_HANDLE;
		goto done;
	}

	/**                 Lock ntfa_CB                 **/
	osafassert(pthread_mutex_lock(&ntfa_cb.cb_lock) == 0);
	/*Check CLM membership of node.*/
	if (client_rec->is_stale_client == true) {
		TRACE("Node not CLM member or stale client");
		osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
		rc = SA_AIS_ERR_UNAVAILABLE;
		goto done_give_hdl;
	}

	filter_hdl_rec = ntfa_filter_hdl_rec_add(&client_rec);
	if (filter_hdl_rec == NULL) {
		osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
		TRACE_1("ntfa_filter_hdl_rec_add failed");
		rc = SA_AIS_ERR_NO_MEMORY;
		goto done_give_hdl;
	}
	TRACE_1("filter_hdl = %llu", filter_hdl_rec->filter_hdl);

	/* Set the instance handle */
	filter_hdl_rec->ntfHandle = ntfHandle;
	filter_hdl_rec->ntfType = SA_NTF_TYPE_ALARM;

	new_filter =
	    &(filter_hdl_rec->notificationFilter.alarmNotificationfilter);
	new_filter->notificationFilterHandle = filter_hdl_rec->filter_hdl;
	new_header = &(filter_hdl_rec->notificationFilter
			   .alarmNotificationfilter.notificationFilterHeader);

	/* Allocate memory */
	rc = ntfsv_filter_header_alloc(
	    new_header, numEventTypes, numNotificationObjects,
	    numNotifyingObjects, numNotificationClassIds);
	if (rc != SA_AIS_OK) {
		goto done_rec_del;
	}

	rc = ntfsv_filter_alarm_alloc(new_filter, numProbableCauses,
				      numPerceivedSeverities, numTrends);
	if (rc != SA_AIS_OK) {
		goto done_rec_del;
	}
	/* initialize the Client struct data */
	*notificationFilter = *new_filter;
	osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);

done_give_hdl:
	ncshm_give_hdl(ntfHandle);
done:
	TRACE_LEAVE();
	return rc;

done_rec_del:
	if (NCSCC_RC_SUCCESS !=
	    ntfa_filter_hdl_rec_del(&client_rec->filter_list, filter_hdl_rec)) {
		TRACE("Unable to delete notifiction record");
		rc = SA_AIS_ERR_LIBRARY;
	}
	osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
	ntfsv_filter_alarm_free(new_filter, false);
	TRACE("ERROR, rc = %d!!!", rc);
	goto done_give_hdl;
}

/*  3.15.2.9	saNtfSecurityAlarmNotificationFilterAllocate()  */
SaAisErrorT saNtfSecurityAlarmNotificationFilterAllocate(
    SaNtfHandleT ntfHandle,
    SaNtfSecurityAlarmNotificationFilterT *notificationFilter,
    SaUint16T numEventTypes, SaUint16T numNotificationObjects,
    SaUint16T numNotifyingObjects, SaUint16T numNotificationClassIds,
    SaUint16T numProbableCauses, SaUint16T numSeverities,
    SaUint16T numSecurityAlarmDetectors, SaUint16T numServiceUsers,
    SaUint16T numServiceProviders)
{
	SaAisErrorT rc = SA_AIS_OK;
	ntfa_client_hdl_rec_t *client_rec;
	ntfa_filter_hdl_rec_t *filter_hdl_rec;
	SaNtfSecurityAlarmNotificationFilterT *new_filter;
	SaNtfNotificationFilterHeaderT *new_header;
	TRACE_ENTER();
	if (notificationFilter == NULL) {
		TRACE_1("notificationFilter == NULL");
		rc = SA_AIS_ERR_INVALID_PARAM;
		goto done;
	}

	/* retrieve hdl rec */
	client_rec = ncshm_take_hdl(NCS_SERVICE_ID_NTFA, ntfHandle);
	if (client_rec == NULL) {
		TRACE("ncshm_take_hdl failed");
		rc = SA_AIS_ERR_BAD_HANDLE;
		goto done;
	}

	/**                 Lock ntfa_CB                 **/
	osafassert(pthread_mutex_lock(&ntfa_cb.cb_lock) == 0);
	/*Check CLM membership of node.*/
	if (client_rec->is_stale_client == true) {
		TRACE("Node not CLM member or stale client");
		osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
		rc = SA_AIS_ERR_UNAVAILABLE;
		goto done_give_hdl;
	}

	filter_hdl_rec = ntfa_filter_hdl_rec_add(&client_rec);
	if (filter_hdl_rec == NULL) {
		osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
		TRACE_1("ntfa_filter_hdl_rec_add failed");
		rc = SA_AIS_ERR_NO_MEMORY;
		goto done_give_hdl;
	}
	TRACE_1("filter_hdl = %llu", filter_hdl_rec->filter_hdl);

	/* Set the instance handle */
	filter_hdl_rec->ntfHandle = ntfHandle;
	filter_hdl_rec->ntfType = SA_NTF_TYPE_SECURITY_ALARM;

	new_filter = &(
	    filter_hdl_rec->notificationFilter.securityAlarmNotificationfilter);
	new_filter->notificationFilterHandle = filter_hdl_rec->filter_hdl;
	new_header =
	    &(filter_hdl_rec->notificationFilter.securityAlarmNotificationfilter
		  .notificationFilterHeader);

	/* Allocate memory */
	rc = ntfsv_filter_header_alloc(
	    new_header, numEventTypes, numNotificationObjects,
	    numNotifyingObjects, numNotificationClassIds);
	if (rc != SA_AIS_OK) {
		goto done_rec_del;
	}
	rc = ntfsv_filter_sec_alarm_alloc(
	    new_filter, numProbableCauses, numSeverities,
	    numSecurityAlarmDetectors, numServiceUsers, numServiceProviders);
	if (rc != SA_AIS_OK) {
		goto done_rec_del;
	}

	/* initialize the Client struct data */
	*notificationFilter = *new_filter;
	osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);

done_give_hdl:
	ncshm_give_hdl(ntfHandle);
done:
	TRACE_LEAVE();
	return rc;

done_rec_del:
	if (NCSCC_RC_SUCCESS !=
	    ntfa_filter_hdl_rec_del(&client_rec->filter_list, filter_hdl_rec)) {
		TRACE("Unable to delete notifiction record");
		rc = SA_AIS_ERR_LIBRARY;
	}
	osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
	TRACE("ERROR, rc = %d!!!", rc);
	goto done_give_hdl;
}

/*  3.15.2.10	saNtfNotificationFilterFree()  */
SaAisErrorT saNtfNotificationFilterFree(
    SaNtfNotificationFilterHandleT notificationFilterHandle)
{
	SaAisErrorT rc = SA_AIS_OK;
	unsigned int client_handle;
	ntfa_filter_hdl_rec_t *filter_hdl_rec;

	ntfa_client_hdl_rec_t *client_rec;
	TRACE_ENTER();

	/**                 Lock ntfa_CB                 **/
	osafassert(pthread_mutex_lock(&ntfa_cb.cb_lock) == 0);
	TRACE_1("filterHandle = %llu", notificationFilterHandle);
	/* retrieve filter hdl rec */
	filter_hdl_rec =
	    ncshm_take_hdl(NCS_SERVICE_ID_NTFA, notificationFilterHandle);
	if (filter_hdl_rec == NULL) {
		osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
		TRACE("ncshm_take_hdl notificationFilterHandle failed");
		rc = SA_AIS_ERR_BAD_HANDLE;
		goto done;
	}
	client_handle = filter_hdl_rec->parent_hdl->local_hdl;
	/* retrieve client hdl rec */
	client_rec = ncshm_take_hdl(NCS_SERVICE_ID_NTFA, client_handle);
	if (client_rec == NULL) {
		/**                  UnLock ntfa_CB            *   */
		osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
		TRACE("ncshm_take_hdl client_handle failed");
		rc = SA_AIS_ERR_BAD_HANDLE;
		goto done_give_hdl;
	}

	/*Check CLM membership of node.*/
	if (client_rec->is_stale_client == true) {
		TRACE("Node not CLM member or stale client");
		osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
		rc = SA_AIS_ERR_UNAVAILABLE;
		ncshm_give_hdl(client_handle);
		goto done_give_hdl;
	}
	/* free the resources allocated by saNtf<ntfType>FilterAllocate */
	ntfa_filter_destructor(filter_hdl_rec);

	/** Delete the resources related to the notificationFilterHandle &
	 *  remove reference in the client.
	 **/
	if (NCSCC_RC_SUCCESS !=
	    ntfa_filter_hdl_rec_del(&client_rec->filter_list, filter_hdl_rec)) {
		TRACE("Unable to delete notifiction record");
		rc = SA_AIS_ERR_LIBRARY;
	}
	/**                  UnLockntfa_CB            **/
	osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);

	ncshm_give_hdl(client_handle);
done_give_hdl:
	ncshm_give_hdl(notificationFilterHandle);
done:
	TRACE_LEAVE();
	return rc;
}

/*  3.15.3.2	saNtfNotificationUnsubscribe()  */
/**
 * The saNtfNotificationUnsubscribe() function deletes the subscription
 * previously made with a call to saNtfNotificationSubscribe().
 *
 * @param subscriptionId
 *
 * @return SaAisErrorT
 */
SaAisErrorT saNtfNotificationUnsubscribe(SaNtfSubscriptionIdT subscriptionId)
{
	TRACE_ENTER();
	SaAisErrorT rc = SA_AIS_OK;
	SaNtfHandleT ntfHandle;
	ntfa_client_hdl_rec_t *client_hdl_rec;

	ntfsv_msg_t msg, *o_msg = NULL;
	ntfsv_msg_t *async_cbk_msg = NULL, *process = NULL, *cbk_msg = NULL;
	ntfsv_unsubscribe_req_t *send_param;
	SaTimeT timeout = NTFS_WAIT_TIME;

	/* Check NTF server availability */
	if (getServerState() == NTFA_NTFSV_NO_ACTIVE) {
		TRACE("NTFS server is temporarily unavailable");
		rc = SA_AIS_ERR_TRY_AGAIN;
		goto done;
	}

	ntfHandle = ntfHandleGet(subscriptionId);
	if (ntfHandle == 0) {
		TRACE_1("ntfHandleGet failed, subscription not exist");
		rc = SA_AIS_ERR_NOT_EXIST;
		goto done;
	}

	/* retrieve client hdl rec */
	client_hdl_rec = ncshm_take_hdl(NCS_SERVICE_ID_NTFA, ntfHandle);
	if (client_hdl_rec == NULL) {
		TRACE_1("ncshm_take_hdl failed");
		rc = SA_AIS_ERR_BAD_HANDLE;
		goto done;
	}
	/*Check CLM membership of node.*/
	if (client_hdl_rec->is_stale_client == true) {
		TRACE("Node not CLM member or stale client");
		rc = SA_AIS_ERR_UNAVAILABLE;
		goto done_give_hdl;
	}

	if (client_hdl_rec->valid) {
		/**
		 ** Populate a sync MDS message
		 **/
		memset(&msg, 0, sizeof(ntfsv_msg_t));
		msg.type = NTFSV_NTFA_API_MSG;
		msg.info.api_info.type = NTFSV_UNSUBSCRIBE_REQ;
		send_param = &msg.info.api_info.param.unsubscribe;

		send_param->client_id = client_hdl_rec->ntfs_client_id;
		send_param->subscriptionId = subscriptionId;

		/* Send a sync MDS message to obtain a log stream id */
		rc = ntfa_mds_msg_sync_send(&ntfa_cb, &msg, &o_msg, timeout);
		if (rc != NCSCC_RC_SUCCESS) {
			TRACE_1("ntfa_mds_msg_sync_send failed with: %u", rc);
			rc = SA_AIS_ERR_TRY_AGAIN;
			goto done_give_hdl;
		}

		osafassert(o_msg != NULL);
		if (SA_AIS_OK != o_msg->info.api_resp_info.rc) {
			rc = o_msg->info.api_resp_info.rc;
			TRACE_1("Bad return status! rc = %d", rc);
			/*Check CLM membership of node.*/
			if (rc == SA_AIS_ERR_UNAVAILABLE) {
				TRACE("Node not CLM member or stale client");
			}
			goto done_give_hdl;
		}
	}
	subscriberListItemRemove(subscriptionId);

	/*Remove msg for subscriptionId from mailbox*/
	do {
		if (NULL == (cbk_msg = (ntfsv_msg_t *)m_NCS_IPC_NON_BLK_RECEIVE(
				 &client_hdl_rec->mbx, cbk_msg)))
			break;

		if (cbk_msg->info.cbk_info.subscriptionId == subscriptionId)
			ntfa_msg_destroy(cbk_msg);
		else
			ntfa_add_to_async_cbk_msg_list(&async_cbk_msg, cbk_msg);
	} while (1);

	/*post msg to mailbox*/
	process = async_cbk_msg;
	while (process) {
		/* IPC send is making next as NULL in process pointer */
		/* process the message */
		async_cbk_msg = async_cbk_msg->next;
		rc = ntfa_ntfs_msg_proc(
		    &ntfa_cb, process,
		    process->info.cbk_info.mds_send_priority);
		if (rc != NCSCC_RC_SUCCESS) {
			TRACE_2(
			    "From saNtfNotificationUnsubscribe ntfa_ntfs_msg_proc returned: %d",
			    rc);
		}
		process = async_cbk_msg;
	}

done_give_hdl:
	if (o_msg)
		ntfa_msg_destroy(o_msg);

	if (!client_hdl_rec->valid && getServerState() == NTFA_NTFSV_UP) {
		if ((rc = recoverClient(client_hdl_rec)) != SA_AIS_OK) {
			if (rc == SA_AIS_ERR_BAD_HANDLE) {
				ncshm_give_hdl(ntfHandle);
				osafassert(
				    pthread_mutex_lock(&ntfa_cb.cb_lock) == 0);
				ntfa_hdl_rec_force_del(&ntfa_cb.client_list,
						       client_hdl_rec);
				osafassert(pthread_mutex_unlock(
					       &ntfa_cb.cb_lock) == 0);
				ntfa_shutdown(false);
				goto done;
			}
		}
	}
	ncshm_give_hdl(ntfHandle);
done:
	TRACE_LEAVE();
	return rc;
}

/*  3.15.4	 Reader Operations  */
/*  3.15.4.1	saNtfNotificationReadInitialize()  */
SaAisErrorT saNtfNotificationReadInitialize(
    SaNtfSearchCriteriaT searchCriteria,
    const SaNtfNotificationTypeFilterHandlesT *notificationFilterHandles,
    SaNtfReadHandleT *readHandle)
{
	SaAisErrorT rc = SA_AIS_ERR_NOT_SUPPORTED;

	ntfa_client_hdl_rec_t *client_hdl_rec = NULL;
	ntfa_reader_hdl_rec_t *reader_hdl_rec = NULL;
	ntfsv_filter_ptrs_t filters = {NULL, NULL, NULL, NULL, NULL};
	SaNtfHandleT ntfHandle = 0;

	ntfsv_msg_t msg, *o_msg = NULL;
	ntfsv_reader_init_req_2_t *send_param;
	SaTimeT timeout = NTFS_WAIT_TIME;

	bool recovery_failed = false;
	TRACE_ENTER();
	if (notificationFilterHandles == NULL || readHandle == NULL) {
		rc = SA_AIS_ERR_INVALID_PARAM;
		TRACE_1(
		    "notificationFilterHandles == NULL or readHandle == NULL");
		goto done;
	}

	if (notificationFilterHandles->attributeChangeFilterHandle != 0 ||
	    notificationFilterHandles->stateChangeFilterHandle != 0 ||
	    notificationFilterHandles->objectCreateDeleteFilterHandle != 0) {
		TRACE_1(
		    "Only alarm and security alarm is supported; all other handles must be set to SA_NTF_FILTER_HANDLE_NULL.");
		rc = SA_AIS_ERR_NOT_SUPPORTED;
		goto done;
	}

	if (searchCriteria.searchMode < SA_NTF_SEARCH_BEFORE_OR_AT_TIME ||
	    searchCriteria.searchMode > SA_NTF_SEARCH_ONLY_FILTER) {
		TRACE_1("searchCriteria.searchMode invalid value: %d",
			(int)searchCriteria.searchMode);
		rc = SA_AIS_ERR_INVALID_PARAM;
		goto done;
	}

	if (!(notificationFilterHandles->alarmFilterHandle ||
	      notificationFilterHandles->securityAlarmFilterHandle)) {
		rc = SA_AIS_ERR_NOT_SUPPORTED;
		TRACE_1("No filterhandle is set");
		goto done;
	}

	rc = getFilters(notificationFilterHandles, &filters, &ntfHandle,
			&client_hdl_rec);
	if (rc != SA_AIS_OK) {
		TRACE("getFilters failed");
		goto done_give_client_hdl;
	}
	/*Check CLM membership of node.*/
	if (client_hdl_rec->is_stale_client == true) {
		TRACE("Node not CLM member or stale client");
		rc = SA_AIS_ERR_UNAVAILABLE;
		goto done_give_client_hdl;
	}

	/* Check NTF server availability */
	if ((rc = checkNtfServerState()) != SA_AIS_OK)
		goto done_give_client_hdl;

	/* recovery */
	if (!client_hdl_rec->valid) {
		if ((rc = recoverClient(client_hdl_rec)) != SA_AIS_OK) {
			recovery_failed = true;
			goto done_give_client_hdl;
		}
	}

	/**
	 ** Populate a sync MDS message
	 **/
	memset(&msg, 0, sizeof(ntfsv_msg_t));
	msg.type = NTFSV_NTFA_API_MSG;
	msg.info.api_info.type = NTFSV_READER_INITIALIZE_REQ_2;
	send_param = &msg.info.api_info.param.reader_init_2;
	send_param->head.client_id = client_hdl_rec->ntfs_client_id;
	/* Fill in ipc send struct */
	send_param->head.searchCriteria = searchCriteria;
	send_param->f_rec = filters;

	/* Send a sync MDS message to obtain a log stream id */
	rc = ntfa_mds_msg_sync_send(&ntfa_cb, &msg, &o_msg, timeout);
	if (rc != NCSCC_RC_SUCCESS) {
		rc = SA_AIS_ERR_TRY_AGAIN;
		goto done_give_client_hdl;
	}

	osafassert(o_msg != NULL);
	if (SA_AIS_OK != o_msg->info.api_resp_info.rc) {
		rc = o_msg->info.api_resp_info.rc;
		TRACE("Bad return status!!! rc = %d", rc);
		/*Check CLM membership of node.*/
		if (rc == SA_AIS_ERR_UNAVAILABLE) {
			TRACE("Node not CLM member or stale client");
		}
		goto done_give_client_hdl;
	}

	if (o_msg->info.api_resp_info.type != NTFSV_READER_INITIALIZE_RSP) {
		TRACE("msg type (%d) failed",
		      (int)o_msg->info.api_resp_info.type);
		rc = SA_AIS_ERR_LIBRARY;
		goto done_give_client_hdl;
	}

	/**                 Lock ntfa_CB                 **/
	osafassert(pthread_mutex_lock(&ntfa_cb.cb_lock) == 0);
	reader_hdl_rec = ntfa_reader_hdl_rec_add(&client_hdl_rec);
	if (reader_hdl_rec == NULL) {
		osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
		rc = SA_AIS_ERR_NO_MEMORY;
		goto done_give_client_hdl;
	}

	TRACE_1("reader_hdl_rec = %u", reader_hdl_rec->reader_hdl);
	*readHandle = (SaNtfReadHandleT)reader_hdl_rec->reader_hdl;
	reader_hdl_rec->ntfHandle = ntfHandle;
	/* Store the readerId returned from server */
	reader_hdl_rec->reader_id =
	    o_msg->info.api_resp_info.param.reader_init_rsp.readerId;
	memset(&reader_hdl_rec->filters, 0, sizeof(ntfsv_filter_ptrs_t));
	ntfa_copy_ntf_filter_ptrs(&reader_hdl_rec->filters, &filters);
	reader_hdl_rec->searchCriteria = searchCriteria;
	/**                  UnLock ntfa_CB            **/
	osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);

done_give_client_hdl:
	if (o_msg)
		ntfa_msg_destroy(o_msg);

	if (client_hdl_rec)
		ncshm_give_hdl(client_hdl_rec->local_hdl);
	if (notificationFilterHandles->attributeChangeFilterHandle)
		ncshm_give_hdl(
		    notificationFilterHandles->attributeChangeFilterHandle);
	if (notificationFilterHandles->objectCreateDeleteFilterHandle)
		ncshm_give_hdl(
		    notificationFilterHandles->objectCreateDeleteFilterHandle);
	if (notificationFilterHandles->securityAlarmFilterHandle)
		ncshm_give_hdl(
		    notificationFilterHandles->securityAlarmFilterHandle);
	if (notificationFilterHandles->stateChangeFilterHandle)
		ncshm_give_hdl(
		    notificationFilterHandles->stateChangeFilterHandle);
	if (notificationFilterHandles->alarmFilterHandle)
		ncshm_give_hdl(notificationFilterHandles->alarmFilterHandle);

	ncshm_give_hdl(notificationFilterHandles->alarmFilterHandle);
	if (recovery_failed && (rc == SA_AIS_ERR_BAD_HANDLE)) {
		osafassert(pthread_mutex_lock(&ntfa_cb.cb_lock) == 0);
		ntfa_hdl_rec_force_del(&ntfa_cb.client_list, client_hdl_rec);
		osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
		ntfa_shutdown(false);
	}
done:
	TRACE_LEAVE();
	return rc;
}

/*  3.15.4.2	saNtfNotificationReadFinalize()  */
SaAisErrorT saNtfNotificationReadFinalize(SaNtfReadHandleT readhandle)
{
	SaAisErrorT rc = SA_AIS_OK;
	uint32_t oas_rc = NCSCC_RC_FAILURE;

	ntfa_client_hdl_rec_t *client_hdl_rec;
	ntfa_reader_hdl_rec_t *reader_hdl_rec;

	ntfsv_msg_t msg, *o_msg = NULL;
	ntfsv_reader_finalize_req_t *send_param;
	SaTimeT timeout = NTFS_WAIT_TIME;

	TRACE_ENTER();
	/* Check NTF server availability */
	if (getServerState() == NTFA_NTFSV_NO_ACTIVE) {
		TRACE("NTFS server is temporarily unavailable");
		rc = SA_AIS_ERR_TRY_AGAIN;
		goto done;
	}

	/* retrieve notification filter hdl rec */
	reader_hdl_rec = ncshm_take_hdl(NCS_SERVICE_ID_NTFA, readhandle);
	if (reader_hdl_rec == NULL) {
		TRACE_1("ncshm_take_hdl failed");
		rc = SA_AIS_ERR_BAD_HANDLE;
		goto done;
	}
	/* retrieve client hdl rec */
	client_hdl_rec =
	    ncshm_take_hdl(NCS_SERVICE_ID_NTFA, reader_hdl_rec->ntfHandle);
	if (client_hdl_rec == NULL) {
		TRACE_1("ncshm_take_hdl failed");
		rc = SA_AIS_ERR_BAD_HANDLE;
		goto done_give_read_hdl;
	}
	/*Check CLM membership of node.*/
	if (client_hdl_rec->is_stale_client == true) {
		TRACE("Node not CLM member or stale client");
		rc = SA_AIS_ERR_UNAVAILABLE;
		goto done_give_hdls;
	}
	TRACE_1("reader_hdl_rec = %u", reader_hdl_rec->reader_hdl);

	if (client_hdl_rec->valid) {
		/**
		 ** Populate a sync MDS message
		 **/
		memset(&msg, 0, sizeof(ntfsv_msg_t));
		msg.type = NTFSV_NTFA_API_MSG;
		msg.info.api_info.type = NTFSV_READER_FINALIZE_REQ;
		send_param = &msg.info.api_info.param.reader_finalize;
		send_param->client_id = client_hdl_rec->ntfs_client_id;
		send_param->readerId = reader_hdl_rec->reader_id;

		/* Send a sync MDS message */
		rc = ntfa_mds_msg_sync_send(&ntfa_cb, &msg, &o_msg, timeout);
		if (rc != NCSCC_RC_SUCCESS) {
			rc = SA_AIS_ERR_TRY_AGAIN;
			goto done_give_hdls;
		}

		osafassert(o_msg != NULL);
		if (SA_AIS_OK != o_msg->info.api_resp_info.rc) {
			rc = o_msg->info.api_resp_info.rc;
			TRACE("Bad return status!!! rc = %d", rc);
			/*Check CLM membership of node.*/
			if (rc == SA_AIS_ERR_UNAVAILABLE) {
				TRACE("Node not CLM member or stale client");
			}
			goto done_give_hdls;
		}

		if (o_msg->info.api_resp_info.type !=
		    NTFSV_READER_FINALIZE_RSP) {
			TRACE("msg type (%d) failed",
			      (int)o_msg->info.api_resp_info.type);
			rc = SA_AIS_ERR_LIBRARY;
			goto done_give_hdls;
		}
	}

	osafassert(pthread_mutex_lock(&ntfa_cb.cb_lock) == 0);
	oas_rc = ntfa_reader_hdl_rec_del(&client_hdl_rec->reader_list,
					 reader_hdl_rec);
	if (oas_rc != NCSCC_RC_SUCCESS) {
		rc = SA_AIS_ERR_LIBRARY;
	}
	osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);

done_give_hdls:
	if (o_msg)
		ntfa_msg_destroy(o_msg);

	if (!client_hdl_rec->valid && getServerState() == NTFA_NTFSV_UP) {
		if ((rc = recoverClient(client_hdl_rec)) != SA_AIS_OK) {
			if (rc == SA_AIS_ERR_BAD_HANDLE) {
				ncshm_give_hdl(client_hdl_rec->local_hdl);
				ncshm_give_hdl(readhandle);
				osafassert(
				    pthread_mutex_lock(&ntfa_cb.cb_lock) == 0);
				ntfa_hdl_rec_force_del(&ntfa_cb.client_list,
						       client_hdl_rec);
				osafassert(pthread_mutex_unlock(
					       &ntfa_cb.cb_lock) == 0);
				ntfa_shutdown(false);
				goto done;
			}
		}
	}
	ncshm_give_hdl(client_hdl_rec->local_hdl);
done_give_read_hdl:
	ncshm_give_hdl(readhandle);
done:
	TRACE_LEAVE();
	return rc;
}

/*  3.15.4.2	saNtfNotificationReadNext()  */
SaAisErrorT saNtfNotificationReadNext(SaNtfReadHandleT readHandle,
				      SaNtfSearchDirectionT searchDirection,
				      SaNtfNotificationsT *notification)
{
	SaAisErrorT rc = SA_AIS_ERR_NOT_SUPPORTED;

	ntfa_client_hdl_rec_t *client_hdl_rec;
	ntfa_reader_hdl_rec_t *reader_hdl_rec;

	ntfsv_msg_t msg, *o_msg = NULL;
	ntfsv_read_next_req_t *send_param;
	SaTimeT timeout = NTFS_WAIT_TIME;
	ntfsv_send_not_req_t *read_not = NULL;
	ntfa_notification_hdl_rec_t *notification_hdl_rec = NULL;
	SaNtfNotificationHeaderT *read_not_header = NULL;
	TRACE_ENTER();

	if (searchDirection < SA_NTF_SEARCH_OLDER ||
	    searchDirection > SA_NTF_SEARCH_YOUNGER) {
		TRACE_1("search direction is not in a valid range");
		rc = SA_AIS_ERR_INVALID_PARAM;
		goto done;
	}

	if (notification == NULL) {
		TRACE_1("notification is NULL");
		rc = SA_AIS_ERR_INVALID_PARAM;
		goto done;
	}

	/* retrieve notification filter hdl rec */
	reader_hdl_rec = ncshm_take_hdl(NCS_SERVICE_ID_NTFA, readHandle);
	if (reader_hdl_rec == NULL) {
		TRACE_1("ncshm_take_hdl failed");
		rc = SA_AIS_ERR_BAD_HANDLE;
		goto done;
	}
	/* retrieve client hdl rec */
	client_hdl_rec =
	    ncshm_take_hdl(NCS_SERVICE_ID_NTFA, reader_hdl_rec->ntfHandle);
	if (client_hdl_rec == NULL) {
		TRACE_1("ncshm_take_hdl failed");
		rc = SA_AIS_ERR_BAD_HANDLE;
		goto done_give_read_hdl;
	}
	/*Check CLM membership of node.*/
	if (client_hdl_rec->is_stale_client == true) {
		TRACE("Node not CLM member or stale client");
		rc = SA_AIS_ERR_UNAVAILABLE;
		goto done_give_hdls;
	}
	TRACE_1("reader_hdl_rec = %u", reader_hdl_rec->reader_hdl);

	/* Check NTF server availability */
	if ((rc = checkNtfServerState()) != SA_AIS_OK)
		goto done_give_hdls;

	if (!client_hdl_rec->valid) {
		if ((rc = recoverClient(client_hdl_rec)) != SA_AIS_OK) {
			ncshm_give_hdl(client_hdl_rec->local_hdl);
			ncshm_give_hdl(readHandle);
			if (rc == SA_AIS_ERR_BAD_HANDLE) {
				osafassert(
				    pthread_mutex_lock(&ntfa_cb.cb_lock) == 0);
				ntfa_hdl_rec_force_del(&ntfa_cb.client_list,
						       client_hdl_rec);
				osafassert(pthread_mutex_unlock(
					       &ntfa_cb.cb_lock) == 0);
				ntfa_shutdown(false);
			}
			goto done;
		}
	}
	/**
	 ** Populate a sync MDS message
	 **/
	memset(&msg, 0, sizeof(ntfsv_msg_t));
	msg.type = NTFSV_NTFA_API_MSG;
	msg.info.api_info.type = NTFSV_READ_NEXT_REQ;
	send_param = &msg.info.api_info.param.read_next;
	send_param->client_id = client_hdl_rec->ntfs_client_id;
	send_param->readerId = reader_hdl_rec->reader_id;
	send_param->searchDirection = searchDirection;

	do {
		/* Send a sync MDS message */
		rc = ntfa_mds_msg_sync_send(&ntfa_cb, &msg, &o_msg, timeout);
		if (rc != NCSCC_RC_SUCCESS) {
			rc = SA_AIS_ERR_TRY_AGAIN;
			goto done_give_hdls;
		}

		osafassert(o_msg != NULL);
		if (SA_AIS_OK != o_msg->info.api_resp_info.rc) {
			rc = o_msg->info.api_resp_info.rc;
			TRACE("error: response msg rc = %d", rc);
			/*Check CLM membership of node.*/
			if (rc == SA_AIS_ERR_UNAVAILABLE) {
				TRACE("Node not CLM member or stale client");
			}
			goto done_give_hdls;
		}
		if (o_msg->info.api_resp_info.type != NTFSV_READ_NEXT_RSP) {
			TRACE("msg type (%d) failed",
			      (int)o_msg->info.api_resp_info.type);
			rc = SA_AIS_ERR_LIBRARY;
			goto done_give_hdls;
		}

		read_not = o_msg->info.api_resp_info.param.read_next_rsp
			       .readNotification;
		if (read_not->notificationType == SA_NTF_TYPE_ALARM)
			read_not_header =
			    &read_not->notification.alarm.notificationHeader;
		else if (read_not->notificationType ==
			 SA_NTF_TYPE_SECURITY_ALARM)
			read_not_header = &read_not->notification.securityAlarm
					       .notificationHeader;
		else {
			TRACE_1("Notification type (%d) is not alarm!",
				(int)read_not->notificationType);
			rc = SA_AIS_ERR_NOT_SUPPORTED;
			goto done_give_hdls;
		}

		/* Skip notification having longDns if this is unadapted longDns
		 * consumer, continue reading next. Otherwise, any return code
		 * differs SA_AIS_OK may stop the consumer reading then miss out
		 * the remaining notifications (shortDns)
		 */
		if (!ntfsv_sanamet_is_valid(
			read_not_header->notificationObject) ||
		    !ntfsv_sanamet_is_valid(read_not_header->notifyingObject)) {
			ntfa_msg_destroy(o_msg);
		} else
			break;
	} while (1);

	/* Only alarm supported */
	if (read_not->notificationType == SA_NTF_TYPE_ALARM) {

		notification->notificationType = SA_NTF_TYPE_ALARM;
		rc = saNtfAlarmNotificationAllocate(
		    reader_hdl_rec->ntfHandle,
		    &notification->notification.alarmNotification,
		    read_not->notification.alarm.notificationHeader
			.numCorrelatedNotifications,
		    read_not->notification.alarm.notificationHeader
			.lengthAdditionalText,
		    read_not->notification.alarm.notificationHeader
			.numAdditionalInfo,
		    read_not->notification.alarm.numSpecificProblems,
		    read_not->notification.alarm.numMonitoredAttributes,
		    read_not->notification.alarm.numProposedRepairActions,
		    read_not->variable_data.size);

		if (rc != SA_AIS_OK) {
			TRACE("saNtfAlarmNotificationAllocate (%d) failed", rc);
			goto done_give_hdls;
		}

		osafassert(pthread_mutex_lock(&ntfa_cb.cb_lock) == 0);
		notification_hdl_rec =
		    ncshm_take_hdl(NCS_SERVICE_ID_NTFA,
				   notification->notification.alarmNotification
				       .notificationHandle);
		if (notification_hdl_rec == NULL) {
			osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
			TRACE("ncshm_take_hdl notificationHandle failed");
			rc = saNtfNotificationFree(
			    notification->notification.alarmNotification
				.notificationHandle);
			if (rc != SA_AIS_OK)
				TRACE("saNtfNotificationFree (%d) failed", rc);

			rc = SA_AIS_ERR_BAD_HANDLE;
			goto done_give_hdls;
		}
		notification_hdl_rec->is_longdn_agent_owner = true;
		rc = ntfsv_v_data_cp(&notification_hdl_rec->variable_data,
				     &read_not->variable_data);
		ncshm_give_hdl(notification->notification.alarmNotification
				   .notificationHandle);
		ntfsv_copy_ntf_alarm(
		    &notification->notification.alarmNotification,
		    &read_not->notification.alarm);
		osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);

	} else if (read_not->notificationType == SA_NTF_TYPE_SECURITY_ALARM) {

		notification->notificationType = SA_NTF_TYPE_SECURITY_ALARM;
		rc = saNtfSecurityAlarmNotificationAllocate(
		    reader_hdl_rec->ntfHandle,
		    &notification->notification.securityAlarmNotification,
		    read_not->notification.securityAlarm.notificationHeader
			.numCorrelatedNotifications,
		    read_not->notification.securityAlarm.notificationHeader
			.lengthAdditionalText,
		    read_not->notification.securityAlarm.notificationHeader
			.numAdditionalInfo,
		    read_not->variable_data.size);

		if (rc != SA_AIS_OK) {
			TRACE(
			    "saNtfSecurityAlarmNotificationAllocate (%d) failed",
			    rc);
			goto done_give_hdls;
		}

		osafassert(pthread_mutex_lock(&ntfa_cb.cb_lock) == 0);
		notification_hdl_rec = ncshm_take_hdl(
		    NCS_SERVICE_ID_NTFA,
		    notification->notification.securityAlarmNotification
			.notificationHandle);
		if (notification_hdl_rec == NULL) {
			osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
			TRACE("ncshm_take_hdl notificationHandle failed");
			rc = saNtfNotificationFree(
			    notification->notification.securityAlarmNotification
				.notificationHandle);
			if (rc != SA_AIS_OK)
				TRACE("saNtfNotificationFree (%d) failed", rc);
			rc = SA_AIS_ERR_BAD_HANDLE;
			goto done_give_hdls;
		}
		notification_hdl_rec->is_longdn_agent_owner = true;
		rc = ntfsv_v_data_cp(&notification_hdl_rec->variable_data,
				     &read_not->variable_data);
		ncshm_give_hdl(
		    notification->notification.securityAlarmNotification
			.notificationHandle);
		ntfsv_copy_ntf_security_alarm(
		    &notification->notification.securityAlarmNotification,
		    &read_not->notification.securityAlarm);
		osafassert(pthread_mutex_unlock(&ntfa_cb.cb_lock) == 0);
	}

done_give_hdls:
	if (o_msg)
		ntfa_msg_destroy(o_msg);
	ncshm_give_hdl(client_hdl_rec->local_hdl);
done_give_read_hdl:
	ncshm_give_hdl(readHandle);
done:
	TRACE_LEAVE();
	return rc;
}
