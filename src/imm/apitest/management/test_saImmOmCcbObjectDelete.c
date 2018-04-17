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

static const SaNameT rdnObj1 = {sizeof("Obj1"), "Obj1"};
static const SaNameT rdnObj2 = {sizeof("Obj2"), "Obj2"};

static SaNameT dnObj1;
static SaNameT dnObj2;
static const SaNameT *dnObjs[] = {&dnObj1, NULL};

static SaAisErrorT config_object_create(SaImmHandleT immHandle,
					SaImmAdminOwnerHandleT ownerHandle,
					const SaNameT *parentName)
{
	SaImmCcbHandleT ccbHandle;
	const SaNameT *nameValues[] = {&rdnObj1, NULL};
	SaImmAttrValuesT_2 v2 = {"rdn", SA_IMM_ATTR_SANAMET, 1,
				 (void **)nameValues};
	SaUint32T int1Value1 = 7;
	SaUint32T *int1Values[] = {&int1Value1};
	SaImmAttrValuesT_2 v1 = {"attr1", SA_IMM_ATTR_SAUINT32T, 1,
				 (void **)int1Values};
	const SaImmAttrValuesT_2 *attrValues[] = {&v1, &v2, NULL};

	safassert(immutil_saImmOmCcbInitialize(ownerHandle, 0, &ccbHandle), SA_AIS_OK);
	safassert(immutil_saImmOmCcbObjectCreate_2(ccbHandle, configClassName,
					   parentName, attrValues),
		  SA_AIS_OK);
	safassert(immutil_saImmOmCcbApply(ccbHandle), SA_AIS_OK);
	return immutil_saImmOmCcbFinalize(ccbHandle);
}

static SaAisErrorT config_object_delete(SaImmHandleT immHandle,
					SaImmAdminOwnerHandleT ownerHandle)
{
	SaImmCcbHandleT ccbHandle;

	safassert(immutil_saImmOmCcbInitialize(ownerHandle, 0, &ccbHandle), SA_AIS_OK);
	safassert(immutil_saImmOmCcbObjectDelete(ccbHandle, &dnObj1), SA_AIS_OK);
	safassert(immutil_saImmOmCcbApply(ccbHandle), SA_AIS_OK);
	return immutil_saImmOmCcbFinalize(ccbHandle);
}

/*
static SaAisErrorT config_object_create_2(SaImmHandleT immHandle,
    SaImmAdminOwnerHandleT ownerHandle,
    const SaImmClassNameT className,
    const SaNameT *rdnObj,
    const SaNameT *parentName,
    const SaImmAttrValuesT_2 *value)
{
    SaImmCcbHandleT ccbHandle;
    const SaNameT* nameValues[] = {rdnObj, NULL};
    SaImmAttrValuesT_2 v = {NULL,  SA_IMM_ATTR_SANAMET, 1, (void**)nameValues};
    const SaImmAttrValuesT_2 * attrValues[] = {&v, value, NULL};
    SaImmClassCategoryT category;
    SaImmAttrDefinitionT_2 **attrDefinition;

    safassert(immutil_saImmOmClassDescriptionGet_2(immHandle, className, &category,
&attrDefinition), SA_AIS_OK); int i = 0; while(attrDefinition[i]) {
	if(attrDefinition[i]->attrFlags & SA_IMM_ATTR_RDN) {
		v.attrName = attrDefinition[i]->attrName;
		break;
	}
	i++;
    }

    assert(attrDefinition[i]);

    safassert(immutil_saImmOmCcbInitialize(ownerHandle, 0, &ccbHandle), SA_AIS_OK);
    safassert(immutil_saImmOmCcbObjectCreate_2(ccbHandle, className, parentName,
attrValues), SA_AIS_OK); safassert(immutil_saImmOmCcbApply(ccbHandle), SA_AIS_OK);
    safassert(immutil_saImmOmClassDescriptionMemoryFree_2(immHandle, attrDefinition),
SA_AIS_OK); return immutil_saImmOmCcbFinalize(ccbHandle);
}

static SaAisErrorT config_object_delete_2(SaImmHandleT immHandle,
    SaImmAdminOwnerHandleT ownerHandle,
    const SaNameT *dnObj,
    int strict)
{
	SaAisErrorT rc;
    SaImmCcbHandleT ccbHandle;

    safassert(immutil_saImmOmCcbInitialize(ownerHandle, 0, &ccbHandle), SA_AIS_OK);
    rc = immutil_saImmOmCcbObjectDelete(ccbHandle, dnObj);
    if(rc == SA_AIS_OK)
	rc = immutil_saImmOmCcbApply(ccbHandle);
    else if(!strict && rc == SA_AIS_ERR_NOT_EXIST)
	rc = SA_AIS_OK;
    safassert(immutil_saImmOmCcbFinalize(ccbHandle), SA_AIS_OK);

    return rc;
}
*/

void saImmOmCcbObjectDelete_01(void)
{
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
	safassert(config_object_create(immOmHandle, ownerHandle, &rootObj),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerSet(ownerHandle, dnObjs, SA_IMM_ONE),
		  SA_AIS_OK);
	safassert(immutil_saImmOmCcbInitialize(ownerHandle, 0, &ccbHandle), SA_AIS_OK);

	test_validate(immutil_saImmOmCcbObjectDelete(ccbHandle, &dnObj1), SA_AIS_OK);

	safassert(immutil_saImmOmCcbApply(ccbHandle), SA_AIS_OK);
	safassert(immutil_saImmOmCcbFinalize(ccbHandle), SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerFinalize(ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);
}

void saImmOmCcbObjectDelete_02(void)
{
	const SaImmAdminOwnerNameT adminOwnerName =
	    (SaImmAdminOwnerNameT) __FUNCTION__;
	SaImmAdminOwnerHandleT ownerHandle;
	SaImmCcbHandleT ccbHandle;
	const SaNameT *objectNames[] = {&rootObj, NULL};

	safassert(immutil_saImmOmInitialize(&immOmHandle, &immOmCallbacks, &immVersion),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerInitialize(immOmHandle, adminOwnerName,
					      SA_TRUE, &ownerHandle),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerSet(ownerHandle, objectNames, SA_IMM_ONE),
		  SA_AIS_OK);
	safassert(config_object_create(immOmHandle, ownerHandle, &rootObj),
		  SA_AIS_OK);
	safassert(immutil_saImmOmCcbInitialize(ownerHandle, 0, &ccbHandle), SA_AIS_OK);

	/* invalid ccbHandle */
	if ((rc = immutil_saImmOmCcbObjectDelete(-1, &dnObj1)) != SA_AIS_ERR_BAD_HANDLE)
		goto done;

	/* already finalized ccbHandle */
	safassert(immutil_saImmOmCcbInitialize(ownerHandle, 0, &ccbHandle), SA_AIS_OK);
	safassert(immutil_saImmOmCcbFinalize(ccbHandle), SA_AIS_OK);
	rc = immutil_saImmOmCcbObjectDelete(ccbHandle, &dnObj1);

done:
	test_validate(rc, SA_AIS_ERR_BAD_HANDLE);
	safassert(config_object_delete(immOmHandle, ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerFinalize(ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);
}

void saImmOmCcbObjectDelete_03(void)
{
	const SaImmAdminOwnerNameT adminOwnerName =
	    (SaImmAdminOwnerNameT) __FUNCTION__;
	SaImmAdminOwnerHandleT ownerHandle;
	SaImmCcbHandleT ccbHandle;
	const SaNameT *objectNames[] = {&rootObj, NULL};

	safassert(immutil_saImmOmInitialize(&immOmHandle, &immOmCallbacks, &immVersion),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerInitialize(immOmHandle, adminOwnerName,
					      SA_TRUE, &ownerHandle),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerSet(ownerHandle, objectNames, SA_IMM_ONE),
		  SA_AIS_OK);
	safassert(config_object_create(immOmHandle, ownerHandle, &rootObj),
		  SA_AIS_OK);
	safassert(
	    immutil_saImmOmAdminOwnerRelease(ownerHandle, objectNames, SA_IMM_SUBTREE),
	    SA_AIS_OK);
	safassert(immutil_saImmOmCcbInitialize(ownerHandle, 0, &ccbHandle), SA_AIS_OK);

	/*
	** at least one of the targeted objects is not a configuration object
	*that is owned by * the administrative owner of the CCB
	*/
	if ((rc = immutil_saImmOmCcbObjectDelete(ccbHandle, &dnObj1)) !=
	    SA_AIS_ERR_BAD_OPERATION) {
		TRACE_ENTER();
		goto done;
	}

/*
** at least one of the targeted objects has some registered continuation
*identifiers
*/

/*
** the Object Implementer has rejected the deletion of at least one of the
*targeted * objects.
*/

done:
	safassert(
	    immutil_saImmOmAdminOwnerSet(ownerHandle, objectNames, SA_IMM_SUBTREE),
	    SA_AIS_OK);
	safassert(config_object_delete(immOmHandle, ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerFinalize(ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);
	test_validate(rc, SA_AIS_ERR_BAD_OPERATION);
}

void saImmOmCcbObjectDelete_04(void)
{
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
	safassert(config_object_create(immOmHandle, ownerHandle, &rootObj),
		  SA_AIS_OK);
	safassert(immutil_saImmOmCcbInitialize(ownerHandle, SA_IMM_CCB_REGISTERED_OI,
				       &ccbHandle),
		  SA_AIS_OK);

	/*
	** The name to which the objectName parameter points is not the name of
	*an * existing object.
	*/
	if ((rc = immutil_saImmOmCcbObjectDelete(ccbHandle, &dnObj2)) !=
	    SA_AIS_ERR_NOT_EXIST) {
		TRACE_ENTER();
		goto done;
	}

	/*
	** There is no registered Object Implementer for at least one of the
	*objects * targeted by this operation, and the CCB has been initialized
	*with the * SA_IMM_CCB_REGISTERED_OI flag set.
	*/
	if ((rc = immutil_saImmOmCcbObjectDelete(ccbHandle, &dnObj1)) !=
	    SA_AIS_ERR_NOT_EXIST) {
		TRACE_ENTER();
		goto done;
	}

done:
	safassert(config_object_delete(immOmHandle, ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerFinalize(ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);

	test_validate(rc, SA_AIS_ERR_NOT_EXIST);
}

void saImmOmCcbObjectDelete_05(void)
{
	const SaImmAdminOwnerNameT adminOwnerName =
	    (SaImmAdminOwnerNameT) __FUNCTION__;
	SaImmAdminOwnerHandleT ownerHandle;
	SaImmCcbHandleT ccbHandle1;
	SaImmCcbHandleT ccbHandle2;
	const SaNameT *objectNames[] = {&rootObj, NULL};

	safassert(immutil_saImmOmInitialize(&immOmHandle, NULL, &immVersion),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerInitialize(immOmHandle, adminOwnerName,
					      SA_TRUE, &ownerHandle),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerSet(ownerHandle, objectNames, SA_IMM_ONE),
		  SA_AIS_OK);
	safassert(config_object_create(immOmHandle, ownerHandle, &rootObj),
		  SA_AIS_OK);
	safassert(immutil_saImmOmCcbInitialize(ownerHandle, 0, &ccbHandle1), SA_AIS_OK);
	safassert(immutil_saImmOmCcbObjectDelete(ccbHandle1, &dnObj1), SA_AIS_OK);

	/*
	** At least one of the targeted objects is already the target of an
	** administrative operation or of a change request in another CCB.
	*/
	safassert(immutil_saImmOmCcbInitialize(ownerHandle, 0, &ccbHandle2), SA_AIS_OK);
	rc = immutil_saImmOmCcbObjectDelete(ccbHandle2, &dnObj1);

	safassert(immutil_saImmOmCcbFinalize(ccbHandle1), SA_AIS_OK);
	safassert(config_object_delete(immOmHandle, ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerFinalize(ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);

	test_validate(rc, SA_AIS_ERR_BUSY);
}

void saImmOmCcbObjectDelete_06(void)
{
	/*
	 * Create then delete an object with NO_DANGLING attributes
	 */
	const SaImmAdminOwnerNameT adminOwnerName =
	    (SaImmAdminOwnerNameT) __FUNCTION__;
	SaImmAdminOwnerHandleT ownerHandle;
	SaImmCcbHandleT ccbHandle;
	const SaNameT obj = {strlen("id=1"), "id=1"};

	safassert(immutil_saImmOmInitialize(&immOmHandle, NULL, &immVersion),
		  SA_AIS_OK);
	safassert(nodangling_class_create(immOmHandle), SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerInitialize(immOmHandle, adminOwnerName,
					      SA_TRUE, &ownerHandle),
		  SA_AIS_OK);
	safassert(object_create(immOmHandle, ownerHandle, nodanglingClassName,
				&obj, NULL, NULL),
		  SA_AIS_OK);

	safassert(immutil_saImmOmCcbInitialize(ownerHandle, 0, &ccbHandle), SA_AIS_OK);
	rc = immutil_saImmOmCcbObjectDelete(ccbHandle, &obj);
	if (rc == SA_AIS_OK) {
		rc = immutil_saImmOmCcbApply(ccbHandle);
	}

	safassert(immutil_saImmOmCcbFinalize(ccbHandle), SA_AIS_OK);
	object_delete(ownerHandle, &obj, 0);
	safassert(nodangling_class_delete(immOmHandle), SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerFinalize(ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);

	test_validate(rc, SA_AIS_OK);
}

void saImmOmCcbObjectDelete_07(void)
{
	/*
	 * Cascade deleting where a child has a NO_DANGLING reference to the
	 * parent object
	 */
	const SaImmAdminOwnerNameT adminOwnerName =
	    (SaImmAdminOwnerNameT) __FUNCTION__;
	SaImmAdminOwnerHandleT ownerHandle;
	SaImmCcbHandleT ccbHandle;
	const SaNameT obj = {strlen("id=1"), "id=1"};
	const SaNameT parent = {strlen("id=2"), "id=2"};
	const SaNameT objDn = {strlen("id=1,id=2"), "id=1,id=2"};

	const SaNameT *refValues[] = {&parent};
	SaImmAttrValuesT_2 refAttr = {"attr1", SA_IMM_ATTR_SANAMET, 1,
				      (void **)refValues};

	safassert(immutil_saImmOmInitialize(&immOmHandle, NULL, &immVersion),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerInitialize(immOmHandle, adminOwnerName,
					      SA_TRUE, &ownerHandle),
		  SA_AIS_OK);
	safassert(nodangling_class_create(immOmHandle), SA_AIS_OK);
	safassert(object_create(immOmHandle, ownerHandle, nodanglingClassName,
				&parent, NULL, NULL),
		  SA_AIS_OK);
	safassert(object_create(immOmHandle, ownerHandle, nodanglingClassName,
				&obj, &parent, &refAttr),
		  SA_AIS_OK);

	safassert(immutil_saImmOmCcbInitialize(ownerHandle, 0, &ccbHandle), SA_AIS_OK);
	rc = immutil_saImmOmCcbObjectDelete(ccbHandle, &parent);
	if (rc == SA_AIS_OK) {
		rc = immutil_saImmOmCcbApply(ccbHandle);
	}

	safassert(immutil_saImmOmCcbFinalize(ccbHandle), SA_AIS_OK);
	object_delete(ownerHandle, &objDn, 0);
	object_delete(ownerHandle, &parent, 0);
	safassert(nodangling_class_delete(immOmHandle), SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerFinalize(ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);

	test_validate(rc, SA_AIS_OK);
}

void saImmOmCcbObjectDelete_08(void)
{
	/*
	 * Delete referred object
	 */
	const SaImmAdminOwnerNameT adminOwnerName =
	    (SaImmAdminOwnerNameT) __FUNCTION__;
	SaImmAdminOwnerHandleT ownerHandle;
	SaImmCcbHandleT ccbHandle;
	const SaNameT obj1 = {strlen("id=1"), "id=1"};
	const SaNameT obj2 = {strlen("id=2"), "id=2"};

	const SaNameT *refValues[] = {&obj1};
	SaImmAttrValuesT_2 refAttr = {"attr1", SA_IMM_ATTR_SANAMET, 1,
				      (void **)refValues};

	//	Create objects obj1 and obj2, where obj2 has a NO_DANGLING
	//reference to obj1
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
				&obj2, NULL, &refAttr),
		  SA_AIS_OK);

	safassert(immutil_saImmOmCcbInitialize(ownerHandle, 0, &ccbHandle), SA_AIS_OK);
	safassert(immutil_saImmOmCcbObjectDelete(ccbHandle, &obj1), SA_AIS_OK);
	rc = immutil_saImmOmCcbApply(ccbHandle);

	safassert(immutil_saImmOmCcbFinalize(ccbHandle), SA_AIS_OK);
	safassert(object_delete(ownerHandle, &obj2, 1), SA_AIS_OK);
	safassert(object_delete(ownerHandle, &obj1, 1), SA_AIS_OK);
	safassert(nodangling_class_delete(immOmHandle), SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerFinalize(ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);

	test_validate(rc, SA_AIS_ERR_FAILED_OPERATION);
}

void saImmOmCcbObjectDelete_09(void)
{
	/*
	 * Trying to delete referred object while an object with NO_DANGLING
	 * reference to the object is flagged for deleting by another CCB
	 */
	const SaImmAdminOwnerNameT adminOwnerName1 =
	    (SaImmAdminOwnerNameT) __FUNCTION__;
	const SaImmAdminOwnerNameT adminOwnerName2 =
	    (SaImmAdminOwnerNameT) "saImmOmCcbObjectDelete_08_2";
	SaImmHandleT immOmHandle1, immOmHandle2;
	SaImmAdminOwnerHandleT ownerHandle1, ownerHandle2;
	SaImmCcbHandleT ccbHandle1, ccbHandle2;
	const SaNameT obj1 = {strlen("id=1"), "id=1"};
	const SaNameT obj2 = {strlen("id=2"), "id=2"};
	const SaNameT *objectNames1[] = {&obj1, NULL};
	const SaNameT *objectNames2[] = {&obj2, NULL};

	const SaNameT *refValues[] = {&obj1};
	SaImmAttrValuesT_2 refAttr = {"attr1", SA_IMM_ATTR_SANAMET, 1,
				      (void **)refValues};

	//	Create objects obj1 and obj2, where obj2 has a NO_DANGLING
	//reference to obj1
	safassert(immutil_saImmOmInitialize(&immOmHandle, NULL, &immVersion),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerInitialize(immOmHandle, adminOwnerName1,
					      SA_TRUE, &ownerHandle1),
		  SA_AIS_OK);
	safassert(nodangling_class_create(immOmHandle), SA_AIS_OK);
	safassert(object_create(immOmHandle, ownerHandle1, nodanglingClassName,
				&obj1, NULL, NULL),
		  SA_AIS_OK);
	safassert(object_create(immOmHandle, ownerHandle1, nodanglingClassName,
				&obj2, NULL, &refAttr),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerFinalize(ownerHandle1), SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);

	safassert(immutil_saImmOmInitialize(&immOmHandle1, NULL, &immVersion),
		  SA_AIS_OK);
	safassert(immutil_saImmOmInitialize(&immOmHandle2, NULL, &immVersion),
		  SA_AIS_OK);

	safassert(immutil_saImmOmAdminOwnerInitialize(immOmHandle1, adminOwnerName1,
					      SA_TRUE, &ownerHandle1),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerSet(ownerHandle1, objectNames1, SA_IMM_ONE),
		  SA_AIS_OK);

	safassert(immutil_saImmOmAdminOwnerInitialize(immOmHandle2, adminOwnerName2,
					      SA_TRUE, &ownerHandle2),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerSet(ownerHandle2, objectNames2, SA_IMM_ONE),
		  SA_AIS_OK);

	safassert(immutil_saImmOmCcbInitialize(ownerHandle1, 0, &ccbHandle1),
		  SA_AIS_OK);
	safassert(immutil_saImmOmCcbInitialize(ownerHandle2, 0, &ccbHandle2),
		  SA_AIS_OK);

	safassert(immutil_saImmOmCcbObjectDelete(ccbHandle2, &obj2), SA_AIS_OK);
	rc = immutil_saImmOmCcbObjectDelete(ccbHandle1, &obj1);

	safassert(immutil_saImmOmCcbApply(ccbHandle2), SA_AIS_OK);

	safassert(immutil_saImmOmCcbFinalize(ccbHandle2), SA_AIS_OK);

	if (rc == SA_AIS_OK) {
		rc = immutil_saImmOmCcbObjectDelete(ccbHandle1, &obj1);
		if (rc == SA_AIS_OK) {
			safassert(immutil_saImmOmCcbApply(ccbHandle1), SA_AIS_OK);
		}
	}

	safassert(immutil_saImmOmCcbFinalize(ccbHandle1), SA_AIS_OK);

	safassert(immutil_saImmOmAdminOwnerFinalize(ownerHandle2), SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle2), SA_AIS_OK);

	object_delete(ownerHandle1, &obj1, 0);
	safassert(nodangling_class_delete(immOmHandle1), SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerFinalize(ownerHandle1), SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle1), SA_AIS_OK);

	test_validate(rc, SA_AIS_ERR_BUSY);
}

void saImmOmCcbObjectDelete_10(void)
{
	/*
	 * Delete object with bidirectional references in the same CCB
	 */
	const SaImmAdminOwnerNameT adminOwnerName =
	    (SaImmAdminOwnerNameT) __FUNCTION__;
	SaImmAdminOwnerHandleT ownerHandle;
	SaImmCcbHandleT ccbHandle;
	const SaNameT obj1 = {strlen("id=1"), "id=1"};
	const SaNameT obj2 = {strlen("id=2"), "id=2"};

	const SaNameT *refValues1[] = {&obj1};
	SaImmAttrValuesT_2 refAttr1 = {"attr1", SA_IMM_ATTR_SANAMET, 1,
				       (void **)refValues1};
	const SaNameT *refValues2[] = {&obj2};
	SaImmAttrValuesT_2 refAttr2 = {"attr1", SA_IMM_ATTR_SANAMET, 1,
				       (void **)refValues2};

	//	Create objects obj1 and obj2, where obj2 has a NO_DANGLING
	//reference to obj1
	safassert(immutil_saImmOmInitialize(&immOmHandle, NULL, &immVersion),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerInitialize(immOmHandle, adminOwnerName,
					      SA_TRUE, &ownerHandle),
		  SA_AIS_OK);
	safassert(nodangling_class_create(immOmHandle), SA_AIS_OK);
	safassert(immutil_saImmOmCcbInitialize(ownerHandle, 0, &ccbHandle), SA_AIS_OK);
	safassert(object_create_2(immOmHandle, ccbHandle, nodanglingClassName,
				  &obj1, NULL, &refAttr2),
		  SA_AIS_OK);
	safassert(object_create_2(immOmHandle, ccbHandle, nodanglingClassName,
				  &obj2, NULL, &refAttr1),
		  SA_AIS_OK);
	safassert(immutil_saImmOmCcbApply(ccbHandle), SA_AIS_OK);

	safassert(immutil_saImmOmCcbInitialize(ownerHandle, 0, &ccbHandle), SA_AIS_OK);
	safassert(immutil_saImmOmCcbObjectDelete(ccbHandle, &obj1), SA_AIS_OK);
	safassert(immutil_saImmOmCcbObjectDelete(ccbHandle, &obj2), SA_AIS_OK);
	rc = immutil_saImmOmCcbApply(ccbHandle);

	safassert(immutil_saImmOmCcbFinalize(ccbHandle), SA_AIS_OK);
	safassert(nodangling_class_delete(immOmHandle), SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerFinalize(ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);

	test_validate(rc, SA_AIS_OK);
}

void saImmOmCcbObjectDelete_11(void)
{
	/*
	 * Delete object with bidirectional references in individual CCB
	 * This testcase is related to saImmOmCcbObjectCreate_17
	 */
	const SaImmAdminOwnerNameT adminOwnerName =
	    (SaImmAdminOwnerNameT) __FUNCTION__;
	SaImmAdminOwnerHandleT ownerHandle;
	SaImmCcbHandleT ccbHandle;
	const SaNameT obj1 = {strlen("id=1"), "id=1"};
	const SaNameT obj2 = {strlen("id=2"), "id=2"};

	const SaNameT *refValues1[] = {&obj1};
	SaImmAttrValuesT_2 refAttr1 = {"attr1", SA_IMM_ATTR_SANAMET, 1,
				       (void **)refValues1};
	const SaNameT *refValues2[] = {&obj2};
	SaImmAttrValuesT_2 refAttr2 = {"attr1", SA_IMM_ATTR_SANAMET, 1,
				       (void **)refValues2};

	//  Create objects obj1 and obj2, where obj2 has a NO_DANGLING reference
	//  to obj1
	safassert(immutil_saImmOmInitialize(&immOmHandle, NULL, &immVersion),
		  SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerInitialize(immOmHandle, adminOwnerName,
					      SA_TRUE, &ownerHandle),
		  SA_AIS_OK);
	safassert(nodangling_class_create(immOmHandle), SA_AIS_OK);
	safassert(immutil_saImmOmCcbInitialize(ownerHandle, 0, &ccbHandle), SA_AIS_OK);
	safassert(object_create_2(immOmHandle, ccbHandle, nodanglingClassName,
				  &obj1, NULL, &refAttr2),
		  SA_AIS_OK);
	safassert(object_create_2(immOmHandle, ccbHandle, nodanglingClassName,
				  &obj2, NULL, &refAttr1),
		  SA_AIS_OK);
	safassert(immutil_saImmOmCcbApply(ccbHandle), SA_AIS_OK);

	safassert(immutil_saImmOmCcbInitialize(ownerHandle, 0, &ccbHandle), SA_AIS_OK);
	safassert(immutil_saImmOmCcbObjectDelete(ccbHandle, &obj1), SA_AIS_OK);
	rc = immutil_saImmOmCcbApply(ccbHandle);

	safassert(immutil_saImmOmCcbFinalize(ccbHandle), SA_AIS_OK);

	safassert(immutil_saImmOmCcbInitialize(ownerHandle, 0, &ccbHandle), SA_AIS_OK);
	object_delete_2(ccbHandle, &obj2, 0);
	object_delete_2(ccbHandle, &obj1, 0);
	saImmOmCcbApply(ccbHandle);
	safassert(immutil_saImmOmCcbFinalize(ccbHandle), SA_AIS_OK);

	safassert(nodangling_class_delete(immOmHandle), SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerFinalize(ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);

	test_validate(rc, SA_AIS_ERR_FAILED_OPERATION);
}

void saImmOmCcbObjectDelete_12(void)
{
	/*
	 * Set NO_DANGLING reference and then delete reffered object
	 * This testcase is related to saImmOmCcbObjectModify_2_15
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
				&obj1, NULL, NULL),
		  SA_AIS_OK);
	safassert(object_create(immOmHandle, ownerHandle, nodanglingClassName,
				&obj2, NULL, NULL),
		  SA_AIS_OK);
	safassert(immutil_saImmOmCcbInitialize(ownerHandle, 0, &ccbHandle), SA_AIS_OK);
	safassert(immutil_saImmOmCcbObjectModify_2(ccbHandle, &obj2, attrMods),
		  SA_AIS_OK);
	safassert(immutil_saImmOmCcbApply(ccbHandle), SA_AIS_OK);

	safassert(immutil_saImmOmCcbInitialize(ownerHandle, 0, &ccbHandle), SA_AIS_OK);
	safassert(immutil_saImmOmCcbObjectDelete(ccbHandle, &obj1), SA_AIS_OK);
	rc = immutil_saImmOmCcbApply(ccbHandle);

	safassert(immutil_saImmOmCcbFinalize(ccbHandle), SA_AIS_OK);
	safassert(object_delete(ownerHandle, &obj2, 1), SA_AIS_OK);
	safassert(object_delete(ownerHandle, &obj1, 1), SA_AIS_OK);
	safassert(nodangling_class_delete(immOmHandle), SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerFinalize(ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);

	test_validate(rc, SA_AIS_ERR_FAILED_OPERATION);
}

void saImmOmCcbObjectDelete_13(void)
{
	/*
	 * Replace NO_DANGLING reference and then delete reffered object
	 * This testcase is related to saImmOmCcbObjectModify_2_16
	 */
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
	safassert(immutil_saImmOmCcbObjectModify_2(ccbHandle, &obj3, attrMods),
		  SA_AIS_OK);
	safassert(immutil_saImmOmCcbApply(ccbHandle), SA_AIS_OK);

	safassert(immutil_saImmOmCcbInitialize(ownerHandle, 0, &ccbHandle), SA_AIS_OK);
	safassert(immutil_saImmOmCcbObjectDelete(ccbHandle, &obj1), SA_AIS_OK);
	rc = immutil_saImmOmCcbApply(ccbHandle);

	safassert(immutil_saImmOmCcbFinalize(ccbHandle), SA_AIS_OK);
	safassert(object_delete(ownerHandle, &obj3, 1), SA_AIS_OK);
	safassert(object_delete(ownerHandle, &obj2, 1), SA_AIS_OK);
	safassert(object_delete(ownerHandle, &obj1, 1), SA_AIS_OK);
	safassert(nodangling_class_delete(immOmHandle), SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerFinalize(ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);

	test_validate(rc, SA_AIS_ERR_FAILED_OPERATION);
}

void saImmOmCcbObjectDelete_14(void)
{
	/*
	 * Set NO_DANGLING reference to an object created in the same CCB and
	 * then delete reffered object This testcase is related to
	 * saImmOmCcbObjectModify_2_21
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
	safassert(immutil_saImmOmCcbApply(ccbHandle), SA_AIS_OK);

	safassert(immutil_saImmOmCcbInitialize(ownerHandle, 0, &ccbHandle), SA_AIS_OK);
	safassert(immutil_saImmOmCcbObjectDelete(ccbHandle, &obj1), SA_AIS_OK);
	rc = immutil_saImmOmCcbApply(ccbHandle);

	safassert(immutil_saImmOmCcbFinalize(ccbHandle), SA_AIS_OK);
	safassert(object_delete(ownerHandle, &obj2, 1), SA_AIS_OK);
	safassert(object_delete(ownerHandle, &obj1, 0), SA_AIS_OK);
	safassert(nodangling_class_delete(immOmHandle), SA_AIS_OK);
	safassert(immutil_saImmOmAdminOwnerFinalize(ownerHandle), SA_AIS_OK);
	safassert(immutil_saImmOmFinalize(immOmHandle), SA_AIS_OK);

	test_validate(rc, SA_AIS_ERR_FAILED_OPERATION);
}

__attribute__((constructor)) static void
saImmOmCcbObjectDelete_constructor(void)
{
	dnObj1.length = (SaUint16T)snprintf((char *)dnObj1.value,
					    sizeof(dnObj1.value), "%s,%s",
					    rdnObj1.value, rootObj.value);
	dnObj2.length = (SaUint16T)snprintf((char *)dnObj2.value,
					    sizeof(dnObj2.value), "%s,%s",
					    rdnObj2.value, rootObj.value);
}
