/*      -*- OpenSAF  -*-
 *
 * (C) Copyright 2008 The OpenSAF Foundation
 * Copyright Ericsson AB 2017 - All Rights Reserved.
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

#include <stdio.h>
#include "imm/apitest/immtest.h"
#include <unistd.h>

static const SaNameT rdnObj1 = {sizeof("Obj1"), "Obj1"};
static const SaNameT rdnObj2 = {sizeof("Obj2"), "Obj2"};
static SaNameT dnObj1;
static SaNameT dnObj2;
static const SaNameT *dnObjs[] = {&dnObj1, NULL};
static SaImmAttrValuesT_2 **attributes;

static SaAisErrorT config_object_create(SaImmHandleT immHandle,
					SaImmAdminOwnerHandleT ownerHandle,
					SaImmCcbHandleT ccbHandle,
					const SaNameT *parentName)
{
	SaAisErrorT rc = SA_AIS_OK;
	const SaNameT *nameValues[] = {&rdnObj1, NULL};
	SaImmAttrValuesT_2 v2 = {"rdn", SA_IMM_ATTR_SANAMET, 1,
				 (void **)nameValues};
	SaUint32T int1Value1 = __LINE__;
	SaUint32T *int1Values[] = {&int1Value1};
	SaStringT strValue1 = "String1-duplicate";
	SaStringT strValue2 = "String2";
	SaStringT *strValues[] = {&strValue2};
	SaStringT *str2Values[] = {&strValue1, &strValue2, &strValue1};
	SaImmAttrValuesT_2 v1 = {"attr1", SA_IMM_ATTR_SAUINT32T, 1,
				 (void **)int1Values};
	SaImmAttrValuesT_2 v3 = {"attr3", SA_IMM_ATTR_SASTRINGT, 3,
				 (void **)str2Values};
	SaImmAttrValuesT_2 v4 = {"attr4", SA_IMM_ATTR_SASTRINGT, 1,
				 (void **)strValues};
	const SaImmAttrValuesT_2 *attrValues[] = {&v1, &v2, &v3, &v4, NULL};
	bool localCcb = (ccbHandle == 0LL);

	if (localCcb) {
		safassert(immutil_saImmOmCcbInitialize(ownerHandle, 0, &ccbHandle),
			  SA_AIS_OK);
	}
	safassert(immutil_saImmOmCcbObjectCreate_2(ccbHandle, configClassName,
					   parentName, attrValues),
		  SA_AIS_OK);
	if (localCcb) {
		safassert(immutil_saImmOmCcbApply(ccbHandle), SA_AIS_OK);
		rc = immutil_saImmOmCcbFinalize(ccbHandle);
	}
	return rc;
}

static SaAisErrorT config_object_delete(SaImmHandleT immHandle,
					SaImmAdminOwnerHandleT ownerHandle)
{
	SaImmCcbHandleT ccbHandle;

	safassert(immutil_saImmOmCcbInitialize(ownerHandle, 0, &ccbHandle), SA_AIS_OK);
	immutil_saImmOmCcbObjectDelete(ccbHandle, &dnObj1);
	safassert(immutil_saImmOmCcbApply(ccbHandle), SA_AIS_OK);
	return immutil_saImmOmCcbFinalize(ccbHandle);
}

void saImmOmCcbObjectModify_2_01(void)
{
	const SaImmAdminOwnerNameT adminOwnerName =
	    (SaImmAdminOwnerNameT) __FUNCTION__;
	SaImmAdminOwnerHandleT ownerHandle;
	SaImmCcbHandleT ccbHandle;
	const SaNameT *objectNames[] = {&rootObj, NULL};
	SaUint32T int1Value1 = __LINE__;
	SaUint32T *int1Values[] = {&int1Value1};
	SaImmAttrValuesT_2 v1 = {"attr1", SA_IMM_ATTR_SAUINT32T, 1,
				 (void **)int1Values};
	SaImmAttrModificationT_2 attrMod = {SA_IMM_ATTR_VALUES_REPLACE, v1};
	const SaImmAttrModificationT_2 *attrMods[] = {&attrMod, NULL};

	safassert(immutil_saImmOmInitialize(&immOmHandle, NULL, &immVersion),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerInitialize(immOmHandle, adminOwnerName,
					      SA_TRUE, &ownerHandle),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerSet(ownerHandle, objectNames, SA_IMM_ONE),
		  SA_AIS_OK);
	safassert(config_object_create(immOmHandle, ownerHandle, 0LL, &rootObj),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerSet(ownerHandle, dnObjs, SA_IMM_ONE),
		  SA_AIS_OK);
	safassert(immutil_saImmOmCcbInitialize(ownerHandle, 0, &ccbHandle), SA_AIS_OK);

	safassert(immutil_saImmOmCcbObjectModify_2(ccbHandle, &dnObj1, NULL),
		  SA_AIS_ERR_INVALID_PARAM);
	test_validate(immutil_saImmOmCcbObjectModify_2(ccbHandle, &dnObj1, attrMods),
		      SA_AIS_OK);

	safassert(immutil_saImmOmCcbFinalize(ccbHandle), SA_AIS_OK);
	safassert(config_object_delete(immOmHandle, ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerFinalize(ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);
}

void saImmOmCcbObjectModify_2_02(void)
{
	const SaImmAdminOwnerNameT adminOwnerName =
	    (SaImmAdminOwnerNameT) __FUNCTION__;
	SaImmAdminOwnerHandleT ownerHandle;
	SaImmCcbHandleT ccbHandle;
	const SaNameT *objectNames[] = {&rootObj, NULL};
	SaUint32T int1Value1 = __LINE__;
	SaUint32T *int1Values[] = {&int1Value1};
	SaImmAttrValuesT_2 v1 = {"attr1", SA_IMM_ATTR_SAUINT32T, 1,
				 (void **)int1Values};
	SaImmAttrModificationT_2 attrMod = {SA_IMM_ATTR_VALUES_REPLACE, v1};
	const SaImmAttrModificationT_2 *attrMods[] = {&attrMod, NULL};

	safassert(immutil_saImmOmInitialize(&immOmHandle, NULL, &immVersion),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerInitialize(immOmHandle, adminOwnerName,
					      SA_TRUE, &ownerHandle),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerSet(ownerHandle, objectNames, SA_IMM_ONE),
		  SA_AIS_OK);
	safassert(config_object_create(immOmHandle, ownerHandle, 0LL, &rootObj),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerSet(ownerHandle, dnObjs, SA_IMM_ONE),
		  SA_AIS_OK);

	/* invalid handle */
	if ((rc = immutil_saImmOmCcbObjectModify_2(-1, &dnObj1, attrMods)) !=
	    SA_AIS_ERR_BAD_HANDLE)
		goto done;

	/* already finalized handle */
	safassert(immutil_saImmOmCcbInitialize(ownerHandle, 0, &ccbHandle), SA_AIS_OK);
	safassert(immutil_saImmOmCcbFinalize(ccbHandle), SA_AIS_OK);
	rc = immutil_saImmOmCcbObjectModify_2(ccbHandle, &dnObj1, attrMods);

done:
	safassert(config_object_delete(immOmHandle, ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerFinalize(ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);
	test_validate(rc, SA_AIS_ERR_BAD_HANDLE);
}

/* SA_AIS_ERR_INVALID_PARAM */
void saImmOmCcbObjectModify_2_03(void)
{
	const SaImmAdminOwnerNameT adminOwnerName =
	    (SaImmAdminOwnerNameT) __FUNCTION__;
	SaImmAdminOwnerHandleT ownerHandle;
	SaImmCcbHandleT ccbHandle;
	const SaNameT *objectNames[] = {&rootObj, NULL};
	SaUint32T int1Value1 = __LINE__;
	SaUint32T *int1Values[] = {&int1Value1};
	const SaNameT rdn = {sizeof("Obj2"), "Obj2"};
	const SaNameT *nameValues[] = {&rdn, NULL};
	SaImmAttrValuesT_2 v1 = {"attr2", SA_IMM_ATTR_SAUINT32T, 1,
				 (void **)int1Values};
	SaImmAttrValuesT_2 v3 = {"attr1", SA_IMM_ATTR_SAINT32T, 1,
				 (void **)int1Values};
	SaImmAttrValuesT_2 v4 = {"rdn", SA_IMM_ATTR_SANAMET, 1,
				 (void **)nameValues};
	SaImmAttrModificationT_2 attrMod1 = {SA_IMM_ATTR_VALUES_REPLACE, v1};
	const SaImmAttrModificationT_2 *attrMods1[] = {&attrMod1, NULL};
	SaImmAttrModificationT_2 attrMod3 = {SA_IMM_ATTR_VALUES_REPLACE, v3};
	const SaImmAttrModificationT_2 *attrMods3[] = {&attrMod3, NULL};
	SaImmAttrModificationT_2 attrMod4 = {SA_IMM_ATTR_VALUES_REPLACE, v4};
	const SaImmAttrModificationT_2 *attrMods4[] = {&attrMod4, NULL};

	safassert(immutil_saImmOmInitialize(&immOmHandle, NULL, &immVersion),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerInitialize(immOmHandle, adminOwnerName,
					      SA_TRUE, &ownerHandle),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerSet(ownerHandle, objectNames, SA_IMM_ONE),
		  SA_AIS_OK);
	safassert(config_object_create(immOmHandle, ownerHandle, 0LL, &rootObj),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerSet(ownerHandle, dnObjs, SA_IMM_ONE),
		  SA_AIS_OK);
	safassert(immutil_saImmOmCcbInitialize(ownerHandle, 0, &ccbHandle), SA_AIS_OK);

	/* runtime attributes */
	if ((rc = immutil_saImmOmCcbObjectModify_2(ccbHandle, &dnObj1, attrMods1)) !=
	    SA_AIS_ERR_INVALID_PARAM)
		goto done;

#if 0

    A.02.01 spec bug. Fixed in A.03.01

    /* attributes that are not defined for the specified class */
    if ((rc = immutil_saImmOmCcbObjectModify_2(ccbHandle, &dnObj1, attrMods2)) != SA_AIS_ERR_INVALID_PARAM)
        goto done;
#endif

	/* attributes with values that do not match the defined value type for
	 * the attribute */
	if ((rc = immutil_saImmOmCcbObjectModify_2(ccbHandle, &dnObj1, attrMods3)) !=
	    SA_AIS_ERR_INVALID_PARAM)
		goto done;

	/* a new value for the RDN attribute */
	if ((rc = immutil_saImmOmCcbObjectModify_2(ccbHandle, &dnObj1, attrMods4)) !=
	    SA_AIS_ERR_INVALID_PARAM)
		goto done;

/* attributes that cannot be modified */

/* multiple values or additional values for a single-valued attribute */

done:
	safassert(config_object_delete(immOmHandle, ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerFinalize(ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);
	test_validate(rc, SA_AIS_ERR_INVALID_PARAM);
}

/* SA_AIS_ERR_BAD_OPERATION */
void saImmOmCcbObjectModify_2_04(void)
{
	const SaImmAdminOwnerNameT adminOwnerName =
	    (SaImmAdminOwnerNameT) __FUNCTION__;
	SaImmAdminOwnerHandleT ownerHandle;
	SaImmCcbHandleT ccbHandle;
	const SaNameT *objectNames[] = {&rootObj, NULL};
	SaUint32T int1Value1 = __LINE__;
	SaUint32T *int1Values[] = {&int1Value1};
	SaImmAttrValuesT_2 v1 = {"attr1", SA_IMM_ATTR_SAUINT32T, 1,
				 (void **)int1Values};
	SaImmAttrModificationT_2 attrMod = {SA_IMM_ATTR_VALUES_REPLACE, v1};
	const SaImmAttrModificationT_2 *attrMods[] = {&attrMod, NULL};

	safassert(immutil_saImmOmInitialize(&immOmHandle, NULL, &immVersion),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerInitialize(immOmHandle, adminOwnerName,
					      SA_TRUE, &ownerHandle),
		  SA_AIS_OK);
	safassert(
	    immutil_saImmOmAdminOwnerSet(ownerHandle, objectNames, SA_IMM_SUBTREE),
	    SA_AIS_OK);
	safassert(config_object_create(immOmHandle, ownerHandle, 0LL, &rootObj),
		  SA_AIS_OK);
	safassert(
	    immutil_saImmOmAdminOwnerRelease(ownerHandle, objectNames, SA_IMM_SUBTREE),
	    SA_AIS_OK);
	safassert(immutil_saImmOmCcbInitialize(ownerHandle, 0, &ccbHandle), SA_AIS_OK);
	rc = immutil_saImmOmCcbObjectModify_2(ccbHandle, &dnObj1, attrMods);

	safassert(immutil_saImmOmCcbFinalize(ccbHandle), SA_AIS_OK);
	safassert(
	    immutil_saImmOmAdminOwnerSet(ownerHandle, objectNames, SA_IMM_SUBTREE),
	    SA_AIS_OK);
	safassert(config_object_delete(immOmHandle, ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerFinalize(ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);
	test_validate(rc, SA_AIS_ERR_BAD_OPERATION);
}

/* SA_AIS_ERR_NOT_EXIST */
void saImmOmCcbObjectModify_2_05(void)
{
	const SaImmAdminOwnerNameT adminOwnerName =
	    (SaImmAdminOwnerNameT) __FUNCTION__;
	SaImmAdminOwnerHandleT ownerHandle;
	SaImmCcbHandleT ccbHandle;
	const SaNameT *objectNames[] = {&rootObj, NULL};
	SaUint32T int1Value1 = __LINE__;
	SaUint32T *int1Values[] = {&int1Value1};
	SaImmAttrValuesT_2 v1 = {"attr1", SA_IMM_ATTR_SAUINT32T, 1,
				 (void **)int1Values};
	SaImmAttrValuesT_2 v2 = {"attr-not-exists", SA_IMM_ATTR_SAUINT32T, 1,
				 (void **)int1Values};
	SaImmAttrModificationT_2 attrMod1 = {SA_IMM_ATTR_VALUES_REPLACE, v1};
	const SaImmAttrModificationT_2 *attrMods1[] = {&attrMod1, NULL};
	SaImmAttrModificationT_2 attrMod2 = {SA_IMM_ATTR_VALUES_REPLACE, v2};
	const SaImmAttrModificationT_2 *attrMods2[] = {&attrMod2, NULL};

	safassert(immutil_saImmOmInitialize(&immOmHandle, NULL, &immVersion),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerInitialize(immOmHandle, adminOwnerName,
					      SA_TRUE, &ownerHandle),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerSet(ownerHandle, objectNames, SA_IMM_ONE),
		  SA_AIS_OK);
	safassert(config_object_create(immOmHandle, ownerHandle, 0LL, &rootObj),
		  SA_AIS_OK);
	safassert(immutil_saImmOmCcbInitialize(ownerHandle, SA_IMM_CCB_REGISTERED_OI,
				       &ccbHandle),
		  SA_AIS_OK);

	/*
	** The name to which the objectName parameter points is not the name of
	*an * existing object.
	*/
	if ((rc = immutil_saImmOmCcbObjectModify_2(ccbHandle, &dnObj2, attrMods1)) !=
	    SA_AIS_ERR_NOT_EXIST)
		goto done;

	/*
	** One or more attribute names specified by the attrMods parameter are
	*not valid * for the object class.
	*/
	if ((rc = immutil_saImmOmCcbObjectModify_2(ccbHandle, &dnObj1, attrMods2)) !=
	    SA_AIS_ERR_NOT_EXIST)
		goto done;

	/*
	** There is no registered Object Implementer for the object designated
	*by the name * to which the objectName parameter points, and the CCB has
	*been initialized * with the SA_IMM_CCB_REGISTERED_OI flag set.
	*/
	if ((rc = immutil_saImmOmCcbObjectModify_2(ccbHandle, &dnObj1, attrMods1)) !=
	    SA_AIS_ERR_NOT_EXIST)
		goto done;

done:
	safassert(immutil_saImmOmCcbFinalize(ccbHandle), SA_AIS_OK);
	safassert(config_object_delete(immOmHandle, ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerFinalize(ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);
	test_validate(rc, SA_AIS_ERR_NOT_EXIST);
}

/* SA_AIS_ERR_BUSY */
void saImmOmCcbObjectModify_2_06(void)
{
	const SaImmAdminOwnerNameT adminOwnerName =
	    (SaImmAdminOwnerNameT) __FUNCTION__;
	SaImmAdminOwnerHandleT ownerHandle;
	SaImmCcbHandleT ccbHandle1;
	SaImmCcbHandleT ccbHandle2;
	const SaNameT *objectNames[] = {&rootObj, NULL};
	SaUint32T int1Value1 = __LINE__;
	SaUint32T *int1Values[] = {&int1Value1};
	SaImmAttrValuesT_2 v1 = {"attr1", SA_IMM_ATTR_SAUINT32T, 1,
				 (void **)int1Values};
	SaImmAttrModificationT_2 attrMod = {SA_IMM_ATTR_VALUES_REPLACE, v1};
	const SaImmAttrModificationT_2 *attrMods[] = {&attrMod, NULL};

	safassert(immutil_saImmOmInitialize(&immOmHandle, NULL, &immVersion),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerInitialize(immOmHandle, adminOwnerName,
					      SA_TRUE, &ownerHandle),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerSet(ownerHandle, objectNames, SA_IMM_ONE),
		  SA_AIS_OK);
	safassert(config_object_create(immOmHandle, ownerHandle, 0LL, &rootObj),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerSet(ownerHandle, dnObjs, SA_IMM_ONE),
		  SA_AIS_OK);
	safassert(immutil_saImmOmCcbInitialize(ownerHandle, 0, &ccbHandle1), SA_AIS_OK);
	safassert(immutil_saImmOmCcbObjectModify_2(ccbHandle1, &dnObj1, attrMods),
		  SA_AIS_OK);

	safassert(immutil_saImmOmCcbInitialize(ownerHandle, 0, &ccbHandle2), SA_AIS_OK);
	test_validate(immutil_saImmOmCcbObjectModify_2(ccbHandle2, &dnObj1, attrMods),
		      SA_AIS_ERR_BUSY);

	safassert(immutil_saImmOmCcbFinalize(ccbHandle1), SA_AIS_OK);
	safassert(immutil_saImmOmCcbFinalize(ccbHandle2), SA_AIS_OK);
	safassert(config_object_delete(immOmHandle, ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerFinalize(ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);
}

void saImmOmCcbObjectModify_2_07(void)
{
	const SaImmAdminOwnerNameT adminOwnerName =
	    (SaImmAdminOwnerNameT) __FUNCTION__;
	SaImmAdminOwnerHandleT ownerHandle;
	SaImmCcbHandleT ccbHandle;
	const SaNameT *objectNames[] = {&rootObj, NULL};
	SaStringT strValue1 = "String1-duplicate";
	SaStringT strValue2 = "String2";
	SaStringT *strValues[] = {&strValue1, &strValue2};
	SaImmAttrValuesT_2 v1 = {"attr3", SA_IMM_ATTR_SASTRINGT, 2,
				 (void **)strValues};
	SaImmAttrModificationT_2 attrMod = {SA_IMM_ATTR_VALUES_DELETE, v1};
	const SaImmAttrModificationT_2 *attrMods[] = {&attrMod, NULL};

	safassert(immutil_saImmOmInitialize(&immOmHandle, NULL, &immVersion),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerInitialize(immOmHandle, adminOwnerName,
					      SA_TRUE, &ownerHandle),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerSet(ownerHandle, objectNames, SA_IMM_ONE),
		  SA_AIS_OK);
	safassert(config_object_create(immOmHandle, ownerHandle, 0LL, &rootObj),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerSet(ownerHandle, dnObjs, SA_IMM_ONE),
		  SA_AIS_OK);
	safassert(immutil_saImmOmCcbInitialize(ownerHandle, 0, &ccbHandle), SA_AIS_OK);

	safassert(immutil_saImmOmCcbObjectModify_2(ccbHandle, &dnObj1, attrMods),
		  SA_AIS_OK);
	test_validate(immutil_saImmOmCcbApply(ccbHandle), SA_AIS_OK);
	safassert(immutil_saImmOmCcbFinalize(ccbHandle), SA_AIS_OK);
	safassert(config_object_delete(immOmHandle, ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerFinalize(ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);
}

void saImmOmCcbObjectModify_2_08(void)
{
	const SaImmAdminOwnerNameT adminOwnerName =
	    (SaImmAdminOwnerNameT) __FUNCTION__;
	SaImmAdminOwnerHandleT ownerHandle;
	SaImmCcbHandleT ccbHandle;
	const SaNameT *objectNames[] = {&rootObj, NULL};
	SaStringT strValue1 = "String1-duplicate";
	SaStringT strValue2 = "String2";
	SaStringT *strValues[] = {&strValue1, &strValue2};
	SaImmAttrValuesT_2 v1 = {"attr4", SA_IMM_ATTR_SASTRINGT, 2,
				 (void **)strValues};
	SaImmAttrModificationT_2 attrMod = {SA_IMM_ATTR_VALUES_DELETE, v1};
	const SaImmAttrModificationT_2 *attrMods[] = {&attrMod, NULL};

	safassert(immutil_saImmOmInitialize(&immOmHandle, NULL, &immVersion),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerInitialize(immOmHandle, adminOwnerName,
					      SA_TRUE, &ownerHandle),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerSet(ownerHandle, objectNames, SA_IMM_ONE),
		  SA_AIS_OK);
	safassert(config_object_create(immOmHandle, ownerHandle, 0LL, &rootObj),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerSet(ownerHandle, dnObjs, SA_IMM_ONE),
		  SA_AIS_OK);
	safassert(immutil_saImmOmCcbInitialize(ownerHandle, 0, &ccbHandle), SA_AIS_OK);

	safassert(immutil_saImmOmCcbObjectModify_2(ccbHandle, &dnObj1, attrMods),
		  SA_AIS_OK);
	test_validate(immutil_saImmOmCcbApply(ccbHandle), SA_AIS_OK);
	safassert(immutil_saImmOmCcbFinalize(ccbHandle), SA_AIS_OK);
	safassert(config_object_delete(immOmHandle, ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerFinalize(ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);
}

void saImmOmCcbObjectModify_2_09(void)
{
	const SaImmAdminOwnerNameT adminOwnerName =
	    (SaImmAdminOwnerNameT) __FUNCTION__;
	SaImmAdminOwnerHandleT ownerHandle;
	SaImmCcbHandleT ccbHandle;
	const SaNameT *objectNames[] = {&rootObj, NULL};
	SaStringT strValue = "Sch\366ne";
	SaStringT *strValues[] = {&strValue};
	SaImmAttrValuesT_2 v1 = {"attr4", SA_IMM_ATTR_SASTRINGT, 1,
				 (void **)strValues};
	SaImmAttrModificationT_2 attrMod = {SA_IMM_ATTR_VALUES_REPLACE, v1};
	const SaImmAttrModificationT_2 *attrMods[] = {&attrMod, NULL};

	safassert(immutil_saImmOmInitialize(&immOmHandle, NULL, &immVersion),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerInitialize(immOmHandle, adminOwnerName,
					      SA_TRUE, &ownerHandle),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerSet(ownerHandle, objectNames, SA_IMM_ONE),
		  SA_AIS_OK);
	safassert(config_object_create(immOmHandle, ownerHandle, 0LL, &rootObj),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerSet(ownerHandle, dnObjs, SA_IMM_ONE),
		  SA_AIS_OK);
	safassert(immutil_saImmOmCcbInitialize(ownerHandle, 0, &ccbHandle), SA_AIS_OK);

	test_validate(immutil_saImmOmCcbObjectModify_2(ccbHandle, &dnObj1, attrMods),
		      SA_AIS_ERR_INVALID_PARAM);

	safassert(immutil_saImmOmCcbFinalize(ccbHandle), SA_AIS_OK);
	safassert(config_object_delete(immOmHandle, ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerFinalize(ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);
}

void saImmOmCcbObjectModify_2_10(void)
{
	const SaImmAdminOwnerNameT adminOwnerName =
	    (SaImmAdminOwnerNameT) __FUNCTION__;
	SaImmAdminOwnerHandleT ownerHandle;
	SaImmCcbHandleT ccbHandle;
	const SaNameT *objectNames[] = {&rootObj, NULL};
	SaStringT strValue = "Sch\303\266ne";
	SaStringT *strValues[] = {&strValue};
	SaImmAttrValuesT_2 v1 = {"attr4", SA_IMM_ATTR_SASTRINGT, 1,
				 (void **)strValues};
	SaImmAttrModificationT_2 attrMod = {SA_IMM_ATTR_VALUES_REPLACE, v1};
	const SaImmAttrModificationT_2 *attrMods[] = {&attrMod, NULL};

	safassert(immutil_saImmOmInitialize(&immOmHandle, NULL, &immVersion),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerInitialize(immOmHandle, adminOwnerName,
					      SA_TRUE, &ownerHandle),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerSet(ownerHandle, objectNames, SA_IMM_ONE),
		  SA_AIS_OK);
	safassert(config_object_create(immOmHandle, ownerHandle, 0LL, &rootObj),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerSet(ownerHandle, dnObjs, SA_IMM_ONE),
		  SA_AIS_OK);
	safassert(immutil_saImmOmCcbInitialize(ownerHandle, 0, &ccbHandle), SA_AIS_OK);

	safassert(immutil_saImmOmCcbObjectModify_2(ccbHandle, &dnObj1, attrMods),
		  SA_AIS_OK);
	test_validate(immutil_saImmOmCcbApply(ccbHandle), SA_AIS_OK);

	safassert(immutil_saImmOmCcbFinalize(ccbHandle), SA_AIS_OK);
	safassert(config_object_delete(immOmHandle, ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerFinalize(ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);
}

void saImmOmCcbObjectModify_2_11(void)
{
	const SaImmAdminOwnerNameT adminOwnerName =
	    (SaImmAdminOwnerNameT) __FUNCTION__;
	SaImmAdminOwnerHandleT ownerHandle;
	SaImmCcbHandleT ccbHandle;
	const SaNameT *objectNames[] = {&rootObj, NULL};
	SaStringT strValue = "Sch\001\002ne";
	SaStringT *strValues[] = {&strValue};
	SaImmAttrValuesT_2 v1 = {"attr4", SA_IMM_ATTR_SASTRINGT, 1,
				 (void **)strValues};
	SaImmAttrModificationT_2 attrMod = {SA_IMM_ATTR_VALUES_REPLACE, v1};
	const SaImmAttrModificationT_2 *attrMods[] = {&attrMod, NULL};

	safassert(immutil_saImmOmInitialize(&immOmHandle, NULL, &immVersion),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerInitialize(immOmHandle, adminOwnerName,
					      SA_TRUE, &ownerHandle),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerSet(ownerHandle, objectNames, SA_IMM_ONE),
		  SA_AIS_OK);
	safassert(config_object_create(immOmHandle, ownerHandle, 0LL, &rootObj),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerSet(ownerHandle, dnObjs, SA_IMM_ONE),
		  SA_AIS_OK);
	safassert(immutil_saImmOmCcbInitialize(ownerHandle, 0, &ccbHandle), SA_AIS_OK);

	safassert(immutil_saImmOmCcbObjectModify_2(ccbHandle, &dnObj1, attrMods),
		  SA_AIS_OK);
	test_validate(immutil_saImmOmCcbApply(ccbHandle), SA_AIS_OK);

	safassert(immutil_saImmOmCcbFinalize(ccbHandle), SA_AIS_OK);
	safassert(config_object_delete(immOmHandle, ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerFinalize(ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);
}

void saImmOmCcbObjectModify_2_12(void)
{
	const SaImmAdminOwnerNameT adminOwnerName =
	    (SaImmAdminOwnerNameT) __FUNCTION__;
	SaImmAdminOwnerHandleT ownerHandle;
	SaImmCcbHandleT ccbHandle;
	SaImmAccessorHandleT accessorHandle;
	const SaNameT *objectNames[] = {&rootObj, NULL};
	SaStringT strValue = "";
	SaStringT *strValues[] = {&strValue};
	SaImmAttrValuesT_2 v1 = {"attr4", SA_IMM_ATTR_SASTRINGT, 1,
				 (void **)strValues};
	SaImmAttrModificationT_2 attrMod = {SA_IMM_ATTR_VALUES_REPLACE, v1};
	const SaImmAttrModificationT_2 *attrMods[] = {&attrMod, NULL};
	const SaImmAttrNameT attributeNames[2] = {"attr4", NULL};
	SaImmAttrValuesT_2 **attributes;
	SaStringT str;

	safassert(immutil_saImmOmInitialize(&immOmHandle, NULL, &immVersion),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerInitialize(immOmHandle, adminOwnerName,
					      SA_TRUE, &ownerHandle),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerSet(ownerHandle, objectNames, SA_IMM_ONE),
		  SA_AIS_OK);
	safassert(config_object_create(immOmHandle, ownerHandle, 0LL, &rootObj),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerSet(ownerHandle, dnObjs, SA_IMM_ONE),
		  SA_AIS_OK);
	safassert(immutil_saImmOmCcbInitialize(ownerHandle, 0, &ccbHandle), SA_AIS_OK);

	safassert(immutil_saImmOmCcbObjectModify_2(ccbHandle, &dnObj1, attrMods),
		  SA_AIS_OK);
	safassert(immutil_saImmOmCcbApply(ccbHandle), SA_AIS_OK);

	safassert(immutil_saImmOmAccessorInitialize(immOmHandle, &accessorHandle),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAccessorGet_2(accessorHandle, &dnObj1, attributeNames,
				       &attributes),
		  SA_AIS_OK);

	assert(attributes);
	assert(attributes[0]);

	assert(attributes[0]->attrValuesNumber == 1);
	str = *(SaStringT *)attributes[0]->attrValues[0];

	assert(str);
	assert(strlen(str) == 0);

	safassert(immutil_saImmOmAccessorFinalize(accessorHandle), SA_AIS_OK);

	test_validate(SA_AIS_OK, SA_AIS_OK);

	safassert(immutil_saImmOmCcbFinalize(ccbHandle), SA_AIS_OK);
	safassert(config_object_delete(immOmHandle, ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerFinalize(ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);
}

void saImmOmCcbObjectModify_2_13(void)
{
	const SaImmAdminOwnerNameT adminOwnerName =
	    (SaImmAdminOwnerNameT) __FUNCTION__;
	SaImmAdminOwnerHandleT ownerHandle;
	SaImmCcbHandleT ccbHandle;
	SaImmAccessorHandleT accessorHandle;
	const SaNameT *objectNames[] = {&rootObj, NULL};
	SaAnyT anyValue = {0, (SaUint8T *)""};
	SaAnyT *anyValues[] = {&anyValue, &anyValue, &anyValue};
	SaImmAttrValuesT_2 any5 = {"attr5", SA_IMM_ATTR_SAANYT, 1,
				   (void **)anyValues};
	SaImmAttrValuesT_2 any6 = {"attr6", SA_IMM_ATTR_SAANYT, 3,
				   (void **)anyValues};
	SaImmAttrModificationT_2 attrMod5 = {SA_IMM_ATTR_VALUES_REPLACE, any5};
	SaImmAttrModificationT_2 attrMod6 = {SA_IMM_ATTR_VALUES_REPLACE, any6};
	const SaImmAttrModificationT_2 *attrMods[] = {&attrMod5, &attrMod6,
						      NULL};
	SaImmAttrValuesT_2 **attributes;
	int i, k, counter;

	safassert(immutil_saImmOmInitialize(&immOmHandle, NULL, &immVersion),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerInitialize(immOmHandle, adminOwnerName,
					      SA_TRUE, &ownerHandle),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerSet(ownerHandle, objectNames, SA_IMM_ONE),
		  SA_AIS_OK);
	safassert(config_object_create(immOmHandle, ownerHandle, 0LL, &rootObj),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerSet(ownerHandle, dnObjs, SA_IMM_ONE),
		  SA_AIS_OK);
	safassert(immutil_saImmOmCcbInitialize(ownerHandle, 0, &ccbHandle), SA_AIS_OK);

	safassert(immutil_saImmOmCcbObjectModify_2(ccbHandle, &dnObj1, attrMods),
		  SA_AIS_OK);
	safassert(immutil_saImmOmCcbApply(ccbHandle), SA_AIS_OK);

	safassert(immutil_saImmOmAccessorInitialize(immOmHandle, &accessorHandle),
		  SA_AIS_OK);
	safassert(
	    immutil_saImmOmAccessorGet_2(accessorHandle, &dnObj1, NULL, &attributes),
	    SA_AIS_OK);

	counter = 0;
	for (i = 0; attributes[i]; i++) {
		SaAnyT *any;
		if (!strcmp(attributes[i]->attrName, "attr5")) {
			counter++;
			/* Test that there is one SaAnyT value */
			assert(attributes[i]->attrValuesNumber == 1);
			/* ... and it's not NULL */
			assert(attributes[i]->attrValues);
			any = (SaAnyT *)(attributes[i]->attrValues[0]);
			/* ... and return value is empty string */
			assert(any);
			assert(any->bufferSize == 0);
		} else if (!strcmp(attributes[i]->attrName, "attr6")) {
			counter++;
			/* Test that there are three SaAnyT values */
			assert(attributes[i]->attrValuesNumber == 3);
			assert(attributes[i]->attrValues);
			/* All three values are empty strings */
			for (k = 0; k < 3; k++) {
				any = (SaAnyT *)(attributes[i]->attrValues[k]);
				assert(any);
				assert(any->bufferSize == 0);
			}
		}
	}

	/* We have tested both parameters */
	assert(counter == 2);

	/* If we come here, then the test is successful */
	test_validate(SA_AIS_OK, SA_AIS_OK);

	safassert(immutil_saImmOmAccessorFinalize(accessorHandle), SA_AIS_OK);
	safassert(immutil_saImmOmCcbFinalize(ccbHandle), SA_AIS_OK);
	safassert(config_object_delete(immOmHandle, ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerFinalize(ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);
}

void saImmOmCcbObjectModify_2_14(void)
{
	const SaImmAdminOwnerNameT adminOwnerName =
	    (SaImmAdminOwnerNameT) __FUNCTION__;
	SaImmAdminOwnerHandleT ownerHandle;
	SaImmCcbHandleT ccbHandle;
	const SaNameT *objectNames[] = {&rootObj, NULL};
	SaAnyT anyValue = {0, (SaUint8T *)""};
	SaAnyT *anyValues[] = {&anyValue, &anyValue, &anyValue};
	SaImmAttrValuesT_2 any5 = {"attr5", SA_IMM_ATTR_SAANYT, 1,
				   (void **)anyValues};
	SaImmAttrValuesT_2 any6 = {"attr6", SA_IMM_ATTR_SAANYT, 3,
				   (void **)anyValues};
	SaImmAttrValuesT_2 any7 = {"attr7", SA_IMM_ATTR_SAANYT, 3,
				   (void **)anyValues};
	SaImmAttrModificationT_2 attrMod5 = {SA_IMM_ATTR_VALUES_REPLACE, any5};
	SaImmAttrModificationT_2 attrMod6 = {SA_IMM_ATTR_VALUES_REPLACE, any6};
	SaImmAttrModificationT_2 attrMod7 = {SA_IMM_ATTR_VALUES_REPLACE, any7};
	const SaImmAttrModificationT_2 *attrMods[] = {&attrMod5, &attrMod6,
						      &attrMod7, NULL};

	safassert(immutil_saImmOmInitialize(&immOmHandle, NULL, &immVersion),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerInitialize(immOmHandle, adminOwnerName,
					      SA_TRUE, &ownerHandle),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerSet(ownerHandle, objectNames, SA_IMM_ONE),
		  SA_AIS_OK);
	safassert(config_object_create(immOmHandle, ownerHandle, 0LL, &rootObj),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerSet(ownerHandle, dnObjs, SA_IMM_ONE),
		  SA_AIS_OK);
	safassert(immutil_saImmOmCcbInitialize(ownerHandle, 0, &ccbHandle), SA_AIS_OK);

	safassert(immutil_saImmOmCcbObjectModify_2(ccbHandle, &dnObj1, attrMods),
		  SA_AIS_ERR_INVALID_PARAM);

	/* If we come here, then the test is successful */
	test_validate(SA_AIS_OK, SA_AIS_OK);

	safassert(immutil_saImmOmCcbFinalize(ccbHandle), SA_AIS_OK);
	safassert(config_object_delete(immOmHandle, ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerFinalize(ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);
}

void saImmOmCcbObjectModify_2_15(void)
{
	const SaImmAdminOwnerNameT adminOwnerName =
	    (SaImmAdminOwnerNameT) __FUNCTION__;
	SaImmAdminOwnerHandleT ownerHandle;
	SaImmCcbHandleT ccbHandle;
	const SaNameT obj1 = {strlen("id=1"), "id=1"};
	const SaNameT obj2 = {strlen("id=2"), "id=2"};
	const SaNameT *attrValues[] = {&obj1};
	SaImmAttrValuesT_2 v1 = {"attr1", SA_IMM_ATTR_SANAMET, 1,
				 (void **)attrValues};
	SaImmAttrModificationT_2 attrMod = {SA_IMM_ATTR_VALUES_REPLACE, v1};
	const SaImmAttrModificationT_2 *attrMods[] = {&attrMod, NULL};

	safassert(immutil_saImmOmInitialize(&immOmHandle, NULL, &immVersion),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerInitialize(immOmHandle, adminOwnerName,
					      SA_TRUE, &ownerHandle),
		  SA_AIS_OK);
	safassert(nodangling_class_create(immOmHandle), SA_AIS_OK);
	safassert(object_create(immOmHandle, ownerHandle, nodanglingClassName,
				&obj1, NULL, NULL),
		  SA_AIS_OK);
	safassert(object_create(immOmHandle, ownerHandle, nodanglingClassName,
				&obj2, NULL, NULL),
		  SA_AIS_OK);
	safassert(immutil_saImmOmCcbInitialize(ownerHandle, 0, &ccbHandle), SA_AIS_OK);

	rc = immutil_saImmOmCcbObjectModify_2(ccbHandle, &obj2, attrMods);
	if (rc == SA_AIS_OK) {
		rc = immutil_saImmOmCcbApply(ccbHandle);
	}

	safassert(immutil_saImmOmCcbFinalize(ccbHandle), SA_AIS_OK);
	safassert(object_delete(ownerHandle, &obj2, 1), SA_AIS_OK);
	safassert(object_delete(ownerHandle, &obj1, 1), SA_AIS_OK);
	safassert(nodangling_class_delete(immOmHandle), SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerFinalize(ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);

	test_validate(rc, SA_AIS_OK);
}

void saImmOmCcbObjectModify_2_16(void)
{
	const SaImmAdminOwnerNameT adminOwnerName =
	    (SaImmAdminOwnerNameT) __FUNCTION__;
	SaImmAdminOwnerHandleT ownerHandle;
	SaImmCcbHandleT ccbHandle;
	const SaNameT obj1 = {strlen("id=1"), "id=1"};
	const SaNameT obj2 = {strlen("id=2"), "id=2"};
	const SaNameT obj3 = {strlen("id=3"), "id=3"};
	const SaNameT *attrValues1[] = {&obj1};
	SaImmAttrValuesT_2 v1 = {"attr1", SA_IMM_ATTR_SANAMET, 1,
				 (void **)attrValues1};
	SaImmAttrModificationT_2 attrMod = {SA_IMM_ATTR_VALUES_REPLACE, v1};
	const SaImmAttrModificationT_2 *attrMods[] = {&attrMod, NULL};

	const SaNameT *attrValues2[] = {&obj2};
	SaImmAttrValuesT_2 v2 = {"attr1", SA_IMM_ATTR_SANAMET, 1,
				 (void **)attrValues2};

	safassert(immutil_saImmOmInitialize(&immOmHandle, NULL, &immVersion),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerInitialize(immOmHandle, adminOwnerName,
					      SA_TRUE, &ownerHandle),
		  SA_AIS_OK);
	safassert(nodangling_class_create(immOmHandle), SA_AIS_OK);
	safassert(object_create(immOmHandle, ownerHandle, nodanglingClassName,
				&obj1, NULL, NULL),
		  SA_AIS_OK);
	safassert(object_create(immOmHandle, ownerHandle, nodanglingClassName,
				&obj2, NULL, NULL),
		  SA_AIS_OK);
	safassert(object_create(immOmHandle, ownerHandle, nodanglingClassName,
				&obj3, NULL, &v2),
		  SA_AIS_OK);
	safassert(immutil_saImmOmCcbInitialize(ownerHandle, 0, &ccbHandle), SA_AIS_OK);

	rc = immutil_saImmOmCcbObjectModify_2(ccbHandle, &obj3, attrMods);
	if (rc == SA_AIS_OK) {
		rc = immutil_saImmOmCcbApply(ccbHandle);
	}

	safassert(immutil_saImmOmCcbFinalize(ccbHandle), SA_AIS_OK);
	safassert(object_delete(ownerHandle, &obj3, 1), SA_AIS_OK);
	safassert(object_delete(ownerHandle, &obj2, 1), SA_AIS_OK);
	safassert(object_delete(ownerHandle, &obj1, 1), SA_AIS_OK);
	safassert(nodangling_class_delete(immOmHandle), SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerFinalize(ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);

	test_validate(rc, SA_AIS_OK);
}

void saImmOmCcbObjectModify_2_17(void)
{
	const SaImmAdminOwnerNameT adminOwnerName =
	    (SaImmAdminOwnerNameT) __FUNCTION__;
	const SaImmOiImplementerNameT implementerName =
	    (SaImmOiImplementerNameT) __FUNCTION__;
	SaImmAdminOwnerHandleT ownerHandle;
	SaImmCcbHandleT ccbHandle;
	const SaNameT obj1 = {strlen("id=1"), "id=1"};
	const SaNameT obj2 = {strlen("id=2"), "id=2"};
	const SaNameT *attrValues[] = {&obj1};
	SaImmAttrValuesT_2 v1 = {"attr1", SA_IMM_ATTR_SANAMET, 1,
				 (void **)attrValues};
	SaImmAttrModificationT_2 attrMod = {SA_IMM_ATTR_VALUES_REPLACE, v1};
	const SaImmAttrModificationT_2 *attrMods[] = {&attrMod, NULL};

	const SaNameT *rdnValues1[] = {&obj1};
	SaImmAttrValuesT_2 rdnAttr1 = {"rdn", SA_IMM_ATTR_SANAMET, 1,
				       (void **)rdnValues1};
	const SaImmAttrValuesT_2 *attrValues1[] = {&rdnAttr1, NULL};

	safassert(immutil_saImmOmInitialize(&immOmHandle, NULL, &immVersion),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerInitialize(immOmHandle, adminOwnerName,
					      SA_TRUE, &ownerHandle),
		  SA_AIS_OK);
	safassert(nodangling_class_create(immOmHandle), SA_AIS_OK);
	safassert(object_create(immOmHandle, ownerHandle, nodanglingClassName,
				&obj2, NULL, NULL),
		  SA_AIS_OK);

	safassert(
	    immutil_saImmOiInitialize_2(&immOiHandle, &immOiCallbacks, &immVersion),
	    SA_AIS_OK);
	safassert(immutil_saImmOiImplementerSet(immOiHandle, implementerName),
		  SA_AIS_OK);
	safassert(immutil_saImmOiRtObjectCreate_2(immOiHandle, runtimeClassName, NULL,
					  attrValues1),
		  SA_AIS_OK);

	safassert(immutil_saImmOmCcbInitialize(ownerHandle, 0, &ccbHandle), SA_AIS_OK);
	rc = immutil_saImmOmCcbObjectModify_2(ccbHandle, &obj2, attrMods);
	safassert(immutil_saImmOmCcbFinalize(ccbHandle), SA_AIS_OK);

	safassert(immutil_saImmOiRtObjectDelete(immOiHandle, &obj1), SA_AIS_OK);
	safassert(immutil_saImmOiFinalize(immOiHandle), SA_AIS_OK);

	safassert(object_delete(ownerHandle, &obj2, 1), SA_AIS_OK);
	safassert(nodangling_class_delete(immOmHandle), SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerFinalize(ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);

	test_validate(rc, SA_AIS_ERR_INVALID_PARAM);
}

void saImmOmCcbObjectModify_2_18(void)
{
	const SaImmAdminOwnerNameT adminOwnerName =
	    (SaImmAdminOwnerNameT) __FUNCTION__;
	SaImmAdminOwnerHandleT ownerHandle;
	SaImmCcbHandleT ccbHandle;
	const SaNameT obj1 = {strlen("id=1"), "id=1"};
	const SaNameT obj2 = {strlen("id=2"), "id=2"};
	const SaNameT *attrValues[] = {&obj1};
	SaImmAttrValuesT_2 v1 = {"attr1", SA_IMM_ATTR_SANAMET, 1,
				 (void **)attrValues};
	SaImmAttrModificationT_2 attrMod = {SA_IMM_ATTR_VALUES_REPLACE, v1};
	const SaImmAttrModificationT_2 *attrMods[] = {&attrMod, NULL};

	safassert(immutil_saImmOmInitialize(&immOmHandle, NULL, &immVersion),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerInitialize(immOmHandle, adminOwnerName,
					      SA_TRUE, &ownerHandle),
		  SA_AIS_OK);
	safassert(nodangling_class_create(immOmHandle), SA_AIS_OK);
	safassert(object_create(immOmHandle, ownerHandle, nodanglingClassName,
				&obj1, NULL, NULL),
		  SA_AIS_OK);
	safassert(object_create(immOmHandle, ownerHandle, nodanglingClassName,
				&obj2, NULL, NULL),
		  SA_AIS_OK);
	safassert(immutil_saImmOmCcbInitialize(ownerHandle, 0, &ccbHandle), SA_AIS_OK);

	safassert(object_delete_2(ccbHandle, &obj1, 1), SA_AIS_OK);
	rc = immutil_saImmOmCcbObjectModify_2(ccbHandle, &obj2, attrMods);

	safassert(immutil_saImmOmCcbFinalize(ccbHandle), SA_AIS_OK);
	safassert(object_delete(ownerHandle, &obj2, 1), SA_AIS_OK);
	safassert(object_delete(ownerHandle, &obj1, 1), SA_AIS_OK);
	safassert(nodangling_class_delete(immOmHandle), SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerFinalize(ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);

	test_validate(rc, SA_AIS_ERR_BAD_OPERATION);
}

void saImmOmCcbObjectModify_2_19(void)
{
	SaImmHandleT immOmHandle1, immOmHandle2;
	const SaImmAdminOwnerNameT adminOwnerName1 =
	    (SaImmAdminOwnerNameT) __FUNCTION__;
	const SaImmAdminOwnerNameT adminOwnerName2 =
	    (SaImmAdminOwnerNameT)__FILE__;
	SaImmAdminOwnerHandleT ownerHandle1;
	SaImmAdminOwnerHandleT ownerHandle2;
	SaImmCcbHandleT ccbHandle1;
	SaImmCcbHandleT ccbHandle2;
	const SaNameT obj1 = {strlen("id=1"), "id=1"};
	const SaNameT obj2 = {strlen("id=2"), "id=2"};
	const SaNameT *attrValues[] = {&obj1};
	SaImmAttrValuesT_2 v1 = {"attr1", SA_IMM_ATTR_SANAMET, 1,
				 (void **)attrValues};
	SaImmAttrModificationT_2 attrMod = {SA_IMM_ATTR_VALUES_REPLACE, v1};
	const SaImmAttrModificationT_2 *attrMods[] = {&attrMod, NULL};

	safassert(immutil_saImmOmInitialize(&immOmHandle1, NULL, &immVersion),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerInitialize(immOmHandle1, adminOwnerName1,
					      SA_TRUE, &ownerHandle1),
		  SA_AIS_OK);
	safassert(immutil_saImmOmInitialize(&immOmHandle2, NULL, &immVersion),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerInitialize(immOmHandle2, adminOwnerName2,
					      SA_TRUE, &ownerHandle2),
		  SA_AIS_OK);
	safassert(nodangling_class_create(immOmHandle1), SA_AIS_OK);
	safassert(object_create(immOmHandle1, ownerHandle1, nodanglingClassName,
				&obj1, NULL, NULL),
		  SA_AIS_OK);
	safassert(object_create(immOmHandle2, ownerHandle2, nodanglingClassName,
				&obj2, NULL, NULL),
		  SA_AIS_OK);
	safassert(immutil_saImmOmCcbInitialize(ownerHandle1, 0, &ccbHandle1),
		  SA_AIS_OK);
	safassert(immutil_saImmOmCcbInitialize(ownerHandle2, 0, &ccbHandle2),
		  SA_AIS_OK);

	safassert(object_delete_2(ccbHandle1, &obj1, 1), SA_AIS_OK);
	rc = immutil_saImmOmCcbObjectModify_2(ccbHandle2, &obj2, attrMods);

	safassert(immutil_saImmOmCcbFinalize(ccbHandle2), SA_AIS_OK);
	safassert(immutil_saImmOmCcbFinalize(ccbHandle1), SA_AIS_OK);
	safassert(object_delete(ownerHandle2, &obj2, 1), SA_AIS_OK);
	safassert(object_delete(ownerHandle1, &obj1, 1), SA_AIS_OK);
	safassert(nodangling_class_delete(immOmHandle1), SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerFinalize(ownerHandle2), SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerFinalize(ownerHandle1), SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle2), SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle1), SA_AIS_OK);

	test_validate(rc, SA_AIS_ERR_BUSY);
}

void saImmOmCcbObjectModify_2_20(void)
{
	SaImmHandleT immOmHandle1, immOmHandle2;
	const SaImmAdminOwnerNameT adminOwnerName1 =
	    (SaImmAdminOwnerNameT) __FUNCTION__;
	const SaImmAdminOwnerNameT adminOwnerName2 =
	    (SaImmAdminOwnerNameT)__FILE__;
	SaImmAdminOwnerHandleT ownerHandle1;
	SaImmAdminOwnerHandleT ownerHandle2;
	SaImmCcbHandleT ccbHandle1;
	SaImmCcbHandleT ccbHandle2;
	const SaNameT obj1 = {strlen("id=1"), "id=1"};
	const SaNameT obj2 = {strlen("id=2"), "id=2"};
	const SaNameT *attrValues[] = {&obj1};
	SaImmAttrValuesT_2 v1 = {"attr1", SA_IMM_ATTR_SANAMET, 1,
				 (void **)attrValues};
	SaImmAttrModificationT_2 attrMod = {SA_IMM_ATTR_VALUES_REPLACE, v1};
	const SaImmAttrModificationT_2 *attrMods[] = {&attrMod, NULL};

	safassert(immutil_saImmOmInitialize(&immOmHandle1, NULL, &immVersion),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerInitialize(immOmHandle1, adminOwnerName1,
					      SA_TRUE, &ownerHandle1),
		  SA_AIS_OK);
	safassert(immutil_saImmOmInitialize(&immOmHandle2, NULL, &immVersion),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerInitialize(immOmHandle2, adminOwnerName2,
					      SA_TRUE, &ownerHandle2),
		  SA_AIS_OK);
	safassert(nodangling_class_create(immOmHandle1), SA_AIS_OK);
	safassert(object_create(immOmHandle2, ownerHandle2, nodanglingClassName,
				&obj2, NULL, NULL),
		  SA_AIS_OK);
	safassert(immutil_saImmOmCcbInitialize(ownerHandle1, 0, &ccbHandle1),
		  SA_AIS_OK);
	safassert(immutil_saImmOmCcbInitialize(ownerHandle2, 0, &ccbHandle2),
		  SA_AIS_OK);

	safassert(object_create_2(immOmHandle1, ccbHandle1, nodanglingClassName,
				  &obj1, NULL, NULL),
		  SA_AIS_OK);
	rc = immutil_saImmOmCcbObjectModify_2(ccbHandle2, &obj2, attrMods);

	safassert(immutil_saImmOmCcbFinalize(ccbHandle2), SA_AIS_OK);
	safassert(immutil_saImmOmCcbFinalize(ccbHandle1), SA_AIS_OK);
	safassert(object_delete(ownerHandle2, &obj2, 1), SA_AIS_OK);
	safassert(nodangling_class_delete(immOmHandle1), SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerFinalize(ownerHandle2), SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerFinalize(ownerHandle1), SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle2), SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle1), SA_AIS_OK);

	test_validate(rc, SA_AIS_ERR_BUSY);
}

void saImmOmCcbObjectModify_2_21(void)
{
	/*
	 * Set NO_DANGLING reference to an object created in the same CCB
	 */
	const SaImmAdminOwnerNameT adminOwnerName =
	    (SaImmAdminOwnerNameT) __FUNCTION__;
	SaImmAdminOwnerHandleT ownerHandle;
	SaImmCcbHandleT ccbHandle;
	const SaNameT obj1 = {strlen("id=1"), "id=1"};
	const SaNameT obj2 = {strlen("id=2"), "id=2"};
	const SaNameT *attrValues[] = {&obj1};
	SaImmAttrValuesT_2 v1 = {"attr1", SA_IMM_ATTR_SANAMET, 1,
				 (void **)attrValues};
	SaImmAttrModificationT_2 attrMod = {SA_IMM_ATTR_VALUES_REPLACE, v1};
	const SaImmAttrModificationT_2 *attrMods[] = {&attrMod, NULL};

	safassert(immutil_saImmOmInitialize(&immOmHandle, NULL, &immVersion),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerInitialize(immOmHandle, adminOwnerName,
					      SA_TRUE, &ownerHandle),
		  SA_AIS_OK);
	safassert(nodangling_class_create(immOmHandle), SA_AIS_OK);

	safassert(object_create(immOmHandle, ownerHandle, nodanglingClassName,
				&obj2, NULL, NULL),
		  SA_AIS_OK);

	safassert(immutil_saImmOmCcbInitialize(ownerHandle, 0, &ccbHandle), SA_AIS_OK);
	safassert(object_create_2(immOmHandle, ccbHandle, nodanglingClassName,
				  &obj1, NULL, NULL),
		  SA_AIS_OK);
	safassert(immutil_saImmOmCcbObjectModify_2(ccbHandle, &obj2, attrMods),
		  SA_AIS_OK);
	rc = immutil_saImmOmCcbApply(ccbHandle);

	safassert(immutil_saImmOmCcbFinalize(ccbHandle), SA_AIS_OK);
	safassert(object_delete(ownerHandle, &obj2, 1), SA_AIS_OK);
	safassert(object_delete(ownerHandle, &obj1, 0), SA_AIS_OK);
	safassert(nodangling_class_delete(immOmHandle), SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerFinalize(ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);

	test_validate(rc, SA_AIS_OK);
}

void saImmOmCcbObjectModify_2_22(void)
{
	/*
	 * STRONG_DEFAULT, Set value of attribute to NULL
	 */

	/* Create class */
	safassert(immutil_saImmOmInitialize(&immOmHandle, &immOmCallbacks, &immVersion),
		  SA_AIS_OK);
	const SaImmClassNameT className = (SaImmClassNameT) __FUNCTION__;
	SaImmAttrDefinitionT_2 rdn = {"rdn", SA_IMM_ATTR_SANAMET,
				      SA_IMM_ATTR_CONFIG | SA_IMM_ATTR_RDN,
				      NULL};
	SaUint32T defaultVal = 100;
	SaImmAttrDefinitionT_2 attr = {"attr", SA_IMM_ATTR_SAUINT32T,
				       SA_IMM_ATTR_CONFIG |
					   SA_IMM_ATTR_WRITABLE |
					   SA_IMM_ATTR_STRONG_DEFAULT,
				       &defaultVal};
	const SaImmAttrDefinitionT_2 *attrDefinitions[] = {&rdn, &attr, NULL};
	safassert(immutil_saImmOmClassCreate_2(immOmHandle, className,
				       SA_IMM_CLASS_CONFIG, attrDefinitions),
		  SA_AIS_OK);

	/* Create object */
	const SaImmAdminOwnerNameT adminOwnerName =
	    (SaImmAdminOwnerNameT) __FUNCTION__;
	SaImmAdminOwnerHandleT ownerHandle;
	const SaNameT obj = {strlen("id=1"), "id=1"};
	SaUint32T val = 200;
	SaImmAttrValueT valArray = &val;
	SaImmAttrValuesT_2 createValue = {"attr", SA_IMM_ATTR_SAUINT32T, 1,
					  &valArray};
	safassert(immutil_saImmOmAdminOwnerInitialize(immOmHandle, adminOwnerName,
					      SA_TRUE, &ownerHandle),
		  SA_AIS_OK);
	safassert(object_create(immOmHandle, ownerHandle, className, &obj, NULL,
				&createValue),
		  SA_AIS_OK);

	/* Set the strong default attribute to NULL */
	SaImmCcbHandleT ccbHandle;
	SaImmAttrValuesT_2 value = {"attr", SA_IMM_ATTR_SAUINT32T, 0, NULL};
	SaImmAttrModificationT_2 attrMod = {SA_IMM_ATTR_VALUES_REPLACE, value};
	const SaImmAttrModificationT_2 *attrMods[] = {&attrMod, NULL};
	safassert(immutil_saImmOmCcbInitialize(ownerHandle, 0, &ccbHandle), SA_AIS_OK);
	safassert(immutil_saImmOmCcbObjectModify_2(ccbHandle, &obj, attrMods),
		  SA_AIS_OK);
	safassert(immutil_saImmOmCcbApply(ccbHandle), SA_AIS_OK);
	safassert(immutil_saImmOmCcbFinalize(ccbHandle), SA_AIS_OK);

	/* Check value of the attribute */
	SaImmAccessorHandleT accessorHandle;
	const SaImmAttrNameT attName = "attr";
	SaImmAttrNameT attNames[] = {attName, NULL};
	SaImmAttrValuesT_2 **resultAttrs;
	safassert(immutil_saImmOmAccessorInitialize(immOmHandle, &accessorHandle),
		  SA_AIS_OK);
	safassert(
	    immutil_saImmOmAccessorGet_2(accessorHandle, &obj, attNames, &resultAttrs),
	    SA_AIS_OK);
	assert(resultAttrs[0] &&
	       (resultAttrs[0]->attrValueType == SA_IMM_ATTR_SAUINT32T));
	assert(resultAttrs[0]->attrValuesNumber == 1);
	test_validate(*((SaUint32T *)resultAttrs[0]->attrValues[0]),
		      defaultVal);

	/* Delete object */
	safassert(object_delete(ownerHandle, &obj, 1), SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerFinalize(ownerHandle), SA_AIS_OK);

	/* Delete class */
	safassert(immutil_saImmOmClassDelete(immOmHandle, className), SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);
}

void saImmOmCcbObjectModify_2_23(void)
{
	/*
	 * STRONG_DEFAULT, Set value of multi-valued attribute to NULL
	 */

	/* Create class */
	safassert(immutil_saImmOmInitialize(&immOmHandle, &immOmCallbacks, &immVersion),
		  SA_AIS_OK);
	const SaImmClassNameT className = (SaImmClassNameT) __FUNCTION__;
	SaImmAttrDefinitionT_2 rdn = {"rdn", SA_IMM_ATTR_SANAMET,
				      SA_IMM_ATTR_CONFIG | SA_IMM_ATTR_RDN,
				      NULL};
	SaUint32T defaultVal = 100;
	SaImmAttrDefinitionT_2 attr = {
	    "attr", SA_IMM_ATTR_SAUINT32T,
	    SA_IMM_ATTR_CONFIG | SA_IMM_ATTR_WRITABLE |
		SA_IMM_ATTR_STRONG_DEFAULT | SA_IMM_ATTR_MULTI_VALUE,
	    &defaultVal};
	const SaImmAttrDefinitionT_2 *attrDefinitions[] = {&rdn, &attr, NULL};
	safassert(immutil_saImmOmClassCreate_2(immOmHandle, className,
				       SA_IMM_CLASS_CONFIG, attrDefinitions),
		  SA_AIS_OK);

	/* Create object */
	const SaImmAdminOwnerNameT adminOwnerName =
	    (SaImmAdminOwnerNameT) __FUNCTION__;
	SaImmAdminOwnerHandleT ownerHandle;
	const SaNameT obj = {strlen("id=1"), "id=1"};
	SaUint32T val1 = 200;
	SaUint32T val2 = 300;
	SaImmAttrValueT valArray[] = {&val1, &val2};
	SaImmAttrValuesT_2 createValue = {"attr", SA_IMM_ATTR_SAUINT32T, 2,
					  valArray};
	safassert(immutil_saImmOmAdminOwnerInitialize(immOmHandle, adminOwnerName,
					      SA_TRUE, &ownerHandle),
		  SA_AIS_OK);
	safassert(object_create(immOmHandle, ownerHandle, className, &obj, NULL,
				&createValue),
		  SA_AIS_OK);

	/* Set the strong default attribute to NULL */
	SaImmCcbHandleT ccbHandle;
	SaImmAttrValuesT_2 modValue = {"attr", SA_IMM_ATTR_SAUINT32T, 0, NULL};
	SaImmAttrModificationT_2 attrMod = {SA_IMM_ATTR_VALUES_REPLACE,
					    modValue};
	const SaImmAttrModificationT_2 *attrMods[] = {&attrMod, NULL};
	safassert(immutil_saImmOmCcbInitialize(ownerHandle, 0, &ccbHandle), SA_AIS_OK);
	safassert(immutil_saImmOmCcbObjectModify_2(ccbHandle, &obj, attrMods),
		  SA_AIS_OK);
	safassert(immutil_saImmOmCcbApply(ccbHandle), SA_AIS_OK);
	safassert(immutil_saImmOmCcbFinalize(ccbHandle), SA_AIS_OK);

	/* Check value of the attribute */
	SaImmAccessorHandleT accessorHandle;
	const SaImmAttrNameT attName = "attr";
	SaImmAttrNameT attNames[] = {attName, NULL};
	SaImmAttrValuesT_2 **resultAttrs;
	safassert(immutil_saImmOmAccessorInitialize(immOmHandle, &accessorHandle),
		  SA_AIS_OK);
	safassert(
	    immutil_saImmOmAccessorGet_2(accessorHandle, &obj, attNames, &resultAttrs),
	    SA_AIS_OK);
	assert(resultAttrs[0] &&
	       (resultAttrs[0]->attrValueType == SA_IMM_ATTR_SAUINT32T));
	assert(resultAttrs[0]->attrValuesNumber == 1);
	test_validate(*((SaUint32T *)resultAttrs[0]->attrValues[0]),
		      defaultVal);

	/* Delete object */
	safassert(object_delete(ownerHandle, &obj, 1), SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerFinalize(ownerHandle), SA_AIS_OK);

	/* Delete class */
	safassert(immutil_saImmOmClassDelete(immOmHandle, className), SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);
}

void saImmOmCcbObjectModify_2_24(void)
{
	/*
	 * STRONG_DEFAULT, Delete all values of multi-valued attribute
	 */

	/* Create class */
	safassert(immutil_saImmOmInitialize(&immOmHandle, &immOmCallbacks, &immVersion),
		  SA_AIS_OK);
	const SaImmClassNameT className = (SaImmClassNameT) __FUNCTION__;
	SaImmAttrDefinitionT_2 rdn = {"rdn", SA_IMM_ATTR_SANAMET,
				      SA_IMM_ATTR_CONFIG | SA_IMM_ATTR_RDN,
				      NULL};
	SaUint32T defaultVal = 100;
	SaImmAttrDefinitionT_2 attr = {
	    "attr", SA_IMM_ATTR_SAUINT32T,
	    SA_IMM_ATTR_CONFIG | SA_IMM_ATTR_WRITABLE |
		SA_IMM_ATTR_STRONG_DEFAULT | SA_IMM_ATTR_MULTI_VALUE,
	    &defaultVal};
	const SaImmAttrDefinitionT_2 *attrDefinitions[] = {&rdn, &attr, NULL};
	safassert(immutil_saImmOmClassCreate_2(immOmHandle, className,
				       SA_IMM_CLASS_CONFIG, attrDefinitions),
		  SA_AIS_OK);

	/* Create object */
	const SaImmAdminOwnerNameT adminOwnerName =
	    (SaImmAdminOwnerNameT) __FUNCTION__;
	SaImmAdminOwnerHandleT ownerHandle;
	const SaNameT obj = {strlen("id=1"), "id=1"};
	SaUint32T val1 = 200;
	SaUint32T val2 = 300;
	SaImmAttrValueT valArray[] = {&val1, &val2};
	SaImmAttrValuesT_2 createValue = {"attr", SA_IMM_ATTR_SAUINT32T, 2,
					  valArray};
	safassert(immutil_saImmOmAdminOwnerInitialize(immOmHandle, adminOwnerName,
					      SA_TRUE, &ownerHandle),
		  SA_AIS_OK);
	safassert(object_create(immOmHandle, ownerHandle, className, &obj, NULL,
				&createValue),
		  SA_AIS_OK);

	/* Delete all value of attribute */
	SaImmCcbHandleT ccbHandle;
	SaImmAttrValuesT_2 modValue = {"attr", SA_IMM_ATTR_SAUINT32T, 2,
				       valArray};
	SaImmAttrModificationT_2 attrMod = {SA_IMM_ATTR_VALUES_DELETE,
					    modValue};
	const SaImmAttrModificationT_2 *attrMods[] = {&attrMod, NULL};
	safassert(immutil_saImmOmCcbInitialize(ownerHandle, 0, &ccbHandle), SA_AIS_OK);
	safassert(immutil_saImmOmCcbObjectModify_2(ccbHandle, &obj, attrMods),
		  SA_AIS_OK);
	safassert(immutil_saImmOmCcbApply(ccbHandle), SA_AIS_OK);
	safassert(immutil_saImmOmCcbFinalize(ccbHandle), SA_AIS_OK);

	/* Check value of the attribute */
	SaImmAccessorHandleT accessorHandle;
	const SaImmAttrNameT attName = "attr";
	SaImmAttrNameT attNames[] = {attName, NULL};
	SaImmAttrValuesT_2 **resultAttrs;
	safassert(immutil_saImmOmAccessorInitialize(immOmHandle, &accessorHandle),
		  SA_AIS_OK);
	safassert(
	    immutil_saImmOmAccessorGet_2(accessorHandle, &obj, attNames, &resultAttrs),
	    SA_AIS_OK);
	assert(resultAttrs[0] &&
	       (resultAttrs[0]->attrValueType == SA_IMM_ATTR_SAUINT32T));
	assert(resultAttrs[0]->attrValuesNumber == 1);
	test_validate(*((SaUint32T *)resultAttrs[0]->attrValues[0]),
		      defaultVal);

	/* Delete object */
	safassert(object_delete(ownerHandle, &obj, 1), SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerFinalize(ownerHandle), SA_AIS_OK);

	/* Delete class */
	safassert(immutil_saImmOmClassDelete(immOmHandle, className), SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);
}

void saImmOmCcbObjectRead_01(void)
{
	// SaVersionT immVersion = {'A', 0x2, 0x11};
	const SaImmAdminOwnerNameT adminOwnerName =
	    (SaImmAdminOwnerNameT) __FUNCTION__;
	SaImmAdminOwnerHandleT ownerHandle;
	SaImmCcbHandleT ccbHandle;
	const SaNameT *objectNames[] = {&rootObj, NULL};
	SaAisErrorT err = SA_AIS_OK;
	safassert(immutil_saImmOmInitialize(&immOmHandle, NULL, &immVersion),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerInitialize(immOmHandle, adminOwnerName,
					      SA_TRUE, &ownerHandle),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerSet(ownerHandle, objectNames, SA_IMM_ONE),
		  SA_AIS_OK);
	safassert(immutil_saImmOmCcbInitialize(ownerHandle, 0, &ccbHandle), SA_AIS_OK);
	safassert(
	    config_object_create(immOmHandle, ownerHandle, ccbHandle, &rootObj),
	    SA_AIS_OK);

	err = immutil_saImmOmCcbObjectRead(ccbHandle, rootObjS, NULL, &attributes);
	err = immutil_saImmOmCcbObjectRead(ccbHandle, "rdn=root", NULL, &attributes);
	test_validate(err, SA_AIS_OK);

	safassert(immutil_saImmOmCcbFinalize(ccbHandle), SA_AIS_OK);
	safassert(config_object_delete(immOmHandle, ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerFinalize(ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);
}

void saImmOmCcbObjectRead_02(void)
{
	SaAisErrorT err = SA_AIS_OK;
	// SaVersionT immVersion = {'A', 0x2, 0x11};
	const SaImmAdminOwnerNameT adminOwnerName =
	    (SaImmAdminOwnerNameT) __FUNCTION__;
	SaImmAdminOwnerHandleT ownerHandle;
	SaImmCcbHandleT ccbHandle;
	const SaNameT *objectNames[] = {&rootObj, NULL};
	SaUint32T int1Value1 = __LINE__;
	SaUint32T *int1Values[] = {&int1Value1};
	SaImmAttrValuesT_2 v1 = {"attr1", SA_IMM_ATTR_SAUINT32T, 1,
				 (void **)int1Values};
	SaImmAttrModificationT_2 attrMod = {SA_IMM_ATTR_VALUES_REPLACE, v1};
	const SaImmAttrModificationT_2 *attrMods[] = {&attrMod, NULL};

	safassert(immutil_saImmOmInitialize(&immOmHandle, NULL, &immVersion),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerInitialize(immOmHandle, adminOwnerName,
					      SA_TRUE, &ownerHandle),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerSet(ownerHandle, objectNames, SA_IMM_ONE),
		  SA_AIS_OK);
	safassert(config_object_create(immOmHandle, ownerHandle, 0LL, &rootObj),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerSet(ownerHandle, dnObjs, SA_IMM_ONE),
		  SA_AIS_OK);
	safassert(immutil_saImmOmCcbInitialize(ownerHandle, 0, &ccbHandle), SA_AIS_OK);

	err = immutil_saImmOmCcbObjectRead(ccbHandle, rootObjS, NULL, &attributes);
	err = immutil_saImmOmCcbObjectModify_2(ccbHandle, &dnObj1, attrMods);
	safassert(immutil_saImmOmCcbFinalize(ccbHandle), SA_AIS_OK);
	safassert(config_object_delete(immOmHandle, ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerFinalize(ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);

	test_validate(err, SA_AIS_OK);
}

void saImmOmCcbObjectRead_03(void)
{
	SaAisErrorT err = SA_AIS_OK;
	// SaVersionT immVersion = {'A', 0x2, 0x11};
	const SaImmAdminOwnerNameT adminOwnerName =
	    (SaImmAdminOwnerNameT) __FUNCTION__;
	SaImmAdminOwnerHandleT ownerHandle;
	SaImmCcbHandleT ccbHandle;
	const SaNameT *objectNames[] = {&rootObj, NULL};

	safassert(immutil_saImmOmInitialize(&immOmHandle, NULL, &immVersion),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerInitialize(immOmHandle, adminOwnerName,
					      SA_TRUE, &ownerHandle),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerSet(ownerHandle, objectNames, SA_IMM_ONE),
		  SA_AIS_OK);
	safassert(config_object_create(immOmHandle, ownerHandle, 0LL, &rootObj),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerSet(ownerHandle, dnObjs, SA_IMM_ONE),
		  SA_AIS_OK);
	safassert(immutil_saImmOmCcbInitialize(ownerHandle, 0, &ccbHandle), SA_AIS_OK);

	err = immutil_saImmOmCcbObjectRead(ccbHandle, rootObjS, NULL, &attributes);
	err = immutil_saImmOmCcbObjectDelete(ccbHandle, &dnObj1);
	safassert(immutil_saImmOmCcbFinalize(ccbHandle), SA_AIS_OK);
	safassert(config_object_delete(immOmHandle, ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerFinalize(ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);

	test_validate(err, SA_AIS_OK);
}

__attribute__((constructor)) static void
saImmOmCcbObjectModify_2_constructor(void)
{
	dnObj1.length = (SaUint16T)snprintf((char *)dnObj1.value,
					    sizeof(dnObj1.value), "%s,%s",
					    rdnObj1.value, rootObj.value);
	dnObj2.length = (SaUint16T)snprintf((char *)dnObj2.value,
					    sizeof(dnObj2.value), "%s,%s",
					    rdnObj2.value, rootObj.value);
}
