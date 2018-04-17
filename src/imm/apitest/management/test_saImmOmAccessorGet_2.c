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

#include "imm/apitest/immtest.h"

static SaImmAccessorHandleT accessorHandle;
static SaImmAttrValuesT_2 **attributes;
static SaNameT nonExistingObjectName = {
    .value = "nonExistingObjectName", .length = sizeof("nonExistingObjectName"),
};
static const SaNameT objectName = {
    .value = "opensafImm=opensafImm,safApp=safImmService",
    .length = sizeof("opensafImm=opensafImm,safApp=safImmService"),
};
static const SaNameT badObjectName = {
    .value = "opensafImm=badOpensafImm,safApp=safImmService",
    .length = sizeof("opensafImm=badOpensafImm,safApp=safImmService"),
};

static unsigned int print_SaImmAttrValuesT_2(SaImmAttrValuesT_2 **attributes)
{
	int i = 0, j;

	TRACE("\n");

	while (attributes[i]) {
		TRACE("  %s\n", attributes[i]->attrName);
		for (j = 0; j < attributes[i]->attrValuesNumber; j++) {
			switch (attributes[i]->attrValueType) {
			case SA_IMM_ATTR_SAINT32T:
				TRACE("    %d\n", *((SaInt32T *)attributes[i]
							->attrValues[j]));
				break;
			case SA_IMM_ATTR_SAUINT32T:
				TRACE("    %u\n", *((SaInt32T *)attributes[i]
							->attrValues[j]));
				break;
			case SA_IMM_ATTR_SAINT64T:
				TRACE("    %lld\n", *((SaInt64T *)attributes[i]
							  ->attrValues[j]));
				break;
			case SA_IMM_ATTR_SAUINT64T:
				TRACE("    %llu\n", *((SaUint64T *)attributes[i]
							  ->attrValues[j]));
				break;
			case SA_IMM_ATTR_SATIMET:
				TRACE(
				    "    %lld\n",
				    *((SaTimeT *)attributes[i]->attrValues[j]));
				break;
			case SA_IMM_ATTR_SANAMET:
				TRACE("    %s (%u)\n",
				      ((SaNameT *)attributes[i]->attrValues[j])
					  ->value,
				      ((SaNameT *)attributes[i]->attrValues[j])
					  ->length);
				break;
			case SA_IMM_ATTR_SAFLOATT:
				TRACE("    %f\n", *((SaFloatT *)attributes[i]
							->attrValues[j]));
				break;
			case SA_IMM_ATTR_SADOUBLET:
				TRACE("    %ff\n", *((SaDoubleT *)attributes[i]
							 ->attrValues[j]));
				break;
			case SA_IMM_ATTR_SASTRINGT:
				TRACE("    %s\n", *((SaStringT *)attributes[i]
							->attrValues[j]));
				break;
			case SA_IMM_ATTR_SAANYT:
				TRACE("    %llu, %p\n",
				      ((SaAnyT *)attributes[i]->attrValues[j])
					  ->bufferSize,
				      ((SaAnyT *)attributes[i]->attrValues[j])
					  ->bufferAddr);
				break;
			default:
				break;
			}
		}
		i++;
	}

	TRACE("\n");

	return i;
}

void saImmOmAccessorGet_2_01(void)
{
	int cnt;

	safassert(immutil_saImmOmInitialize(&immOmHandle, &immOmCallbacks, &immVersion),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAccessorInitialize(immOmHandle, &accessorHandle),
		  SA_AIS_OK);
	rc = immutil_saImmOmAccessorGet_2(accessorHandle, &objectName, NULL,
				  &attributes);
	safassert(rc, SA_AIS_OK);
	cnt = print_SaImmAttrValuesT_2(attributes);
	test_validate(rc, SA_AIS_OK);
	assert(cnt > 7);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);
}

void saImmOmAccessorGet_2_02(void)
{
	safassert(immutil_saImmOmInitialize(&immOmHandle, &immOmCallbacks, &immVersion),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAccessorInitialize(immOmHandle, &accessorHandle),
		  SA_AIS_OK);
	rc = immutil_saImmOmAccessorGet_2(-1, &objectName, NULL, &attributes);
	test_validate(rc, SA_AIS_ERR_BAD_HANDLE);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);
}

void saImmOmAccessorGet_2_03(void)
{
	safassert(immutil_saImmOmInitialize(&immOmHandle, &immOmCallbacks, &immVersion),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAccessorInitialize(immOmHandle, &accessorHandle),
		  SA_AIS_OK);
	rc = immutil_saImmOmAccessorGet_2(accessorHandle, &nonExistingObjectName, NULL,
				  &attributes);
	test_validate(rc, SA_AIS_ERR_NOT_EXIST);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);
}

void saImmOmAccessorGet_2_04(void)
{
	int cnt = 0;
	SaImmAttrNameT accessorGetConfigAttrsToken[2] = {
	    "SA_IMM_SEARCH_GET_CONFIG_ATTR", NULL};
	safassert(immutil_saImmOmInitialize(&immOmHandle, &immOmCallbacks, &immVersion),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAccessorInitialize(immOmHandle, &accessorHandle),
		  SA_AIS_OK);
	rc = immutil_saImmOmAccessorGet_2(accessorHandle, &objectName,
				  accessorGetConfigAttrsToken, &attributes);
	/* Count the number of config attributes of the class */
	SaImmClassCategoryT category;
	SaImmAttrDefinitionT_2 **classAttributes;
	safassert(immutil_saImmOmClassDescriptionGet_2(immOmHandle, "OpensafImm",
					       &category, &classAttributes),
		  SA_AIS_OK);
	int configAttrCount = 0;
	SaImmAttrDefinitionT_2 **currentAttr = classAttributes;
	while (*currentAttr) {
		if ((*currentAttr)->attrFlags & SA_IMM_ATTR_CONFIG) {
			++configAttrCount;
		}
		++currentAttr;
	}
	safassert(
	    immutil_saImmOmClassDescriptionMemoryFree_2(immOmHandle, classAttributes),
	    SA_AIS_OK);
	/* Verify the number of config attributes */
	cnt = print_SaImmAttrValuesT_2(attributes);
	assert(cnt == configAttrCount);
	test_validate(rc, SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);
}

void saImmOmAccessorGet_2_05(void)
{
	SaImmAttrNameT attributeNames[] = {NULL};

	safassert(immutil_saImmOmInitialize(&immOmHandle, &immOmCallbacks, &immVersion),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAccessorInitialize(immOmHandle, &accessorHandle),
		  SA_AIS_OK);
	rc = immutil_saImmOmAccessorGet_2(accessorHandle, &objectName, attributeNames,
				  NULL);
	test_validate(rc, SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);
}

void saImmOmAccessorGet_2_06(void)
{
	safassert(immutil_saImmOmInitialize(&immOmHandle, &immOmCallbacks, &immVersion),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAccessorInitialize(immOmHandle, &accessorHandle),
		  SA_AIS_OK);
	rc = immutil_saImmOmAccessorGet_2(accessorHandle, &objectName, NULL, NULL);
	test_validate(rc, SA_AIS_ERR_INVALID_PARAM);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);
}

void saImmOmAccessorGet_2_07(void)
{
	SaImmAttrNameT attributeNames[] = {"SaImmAttrClassName", NULL};

	safassert(immutil_saImmOmInitialize(&immOmHandle, &immOmCallbacks, &immVersion),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAccessorInitialize(immOmHandle, &accessorHandle),
		  SA_AIS_OK);
	rc = immutil_saImmOmAccessorGet_2(accessorHandle, &objectName, attributeNames,
				  NULL);
	test_validate(rc, SA_AIS_ERR_INVALID_PARAM);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);
}

void saImmOmAccessorGet_2_08(void)
{
	SaImmAttrNameT attributeNames[] = {NULL};

	safassert(immutil_saImmOmInitialize(&immOmHandle, &immOmCallbacks, &immVersion),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAccessorInitialize(immOmHandle, &accessorHandle),
		  SA_AIS_OK);
	rc = immutil_saImmOmAccessorGet_2(accessorHandle, &badObjectName,
				  attributeNames, NULL);
	test_validate(rc, SA_AIS_ERR_NOT_EXIST);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);
}

void saImmOmAccessorGet_2_09(void)
{
	SaNameT objectName1 = {
	    .value = "", .length = 1,
	};

	safassert(immutil_saImmOmInitialize(&immOmHandle, &immOmCallbacks, &immVersion),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAccessorInitialize(immOmHandle, &accessorHandle),
		  SA_AIS_OK);
	rc = immutil_saImmOmAccessorGet_2(accessorHandle, &objectName1, NULL,
				  &attributes);
	test_validate(rc, SA_AIS_ERR_INVALID_PARAM);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);
}

void saImmOmAccessorGet_2_10(void)
{
	int count;
	rc = SA_AIS_OK;

	safassert(immutil_saImmOmInitialize(&immOmHandle, &immOmCallbacks, &immVersion),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAccessorInitialize(immOmHandle, &accessorHandle),
		  SA_AIS_OK);
	for (count = 0; count < 100 && rc == SA_AIS_OK; ++count) {
		rc = immutil_saImmOmAccessorGet_2(accessorHandle, &objectName, NULL,
					  &attributes);
	}
	test_validate(rc, SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);
}

void saImmOmAccessorGet_2_11(void)
{
	int maxSearchHandles = 100; /* By default it is 100 */
	char *value;
	int i;

	if ((value = getenv("IMMA_MAX_OPEN_SEARCHES_PER_HANDLE"))) {
		char *endptr;
		int n = (int)strtol(value, &endptr, 10);
		if (*value && !*endptr)
			maxSearchHandles = n;
	}

	safassert(immutil_saImmOmInitialize(&immOmHandle, &immOmCallbacks, &immVersion),
		  SA_AIS_OK);
	for (i = 0; i < maxSearchHandles; i++)
		safassert(
		    immutil_saImmOmAccessorInitialize(immOmHandle, &accessorHandle),
		    SA_AIS_OK);

	rc = immutil_saImmOmAccessorInitialize(immOmHandle, &accessorHandle);
	test_validate(rc, SA_AIS_ERR_NO_RESOURCES);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);
}
