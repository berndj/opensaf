############################################################################
#
# (C) Copyright 2014 The OpenSAF Foundation
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
""" IMM OM common utilities """
import os
from ctypes import pointer, POINTER

from pyosaf.saAis import saAis, SaVersionT, SaNameT, SaStringT, SaAisErrorT, \
    eSaAisErrorT, eSaBoolT, unmarshalNullArray
from pyosaf import saImmOm, saImm
from pyosaf.saImm import eSaImmScopeT, unmarshalSaImmValue, SaImmAttrNameT, \
    SaImmAttrValuesT_2
from pyosaf.saImmOm import SaImmAccessorHandleT
from pyosaf.utils import decorate, initialize_decorate
from pyosaf.utils import SafException
from pyosaf.utils.immom.object import ImmObject


handle = saImmOm.SaImmHandleT()
accessor_handle = SaImmAccessorHandleT()

# Decorate pure saImmOm* API's with error-handling retry and exception raising
saImmOmInitialize = initialize_decorate(saImmOm.saImmOmInitialize)
saImmOmSelectionObjectGet = decorate(saImmOm.saImmOmSelectionObjectGet)
saImmOmDispatch = decorate(saImmOm.saImmOmDispatch)
saImmOmFinalize = decorate(saImmOm.saImmOmFinalize)
saImmOmClassCreate_2 = decorate(saImmOm.saImmOmClassCreate_2)
saImmOmClassDescriptionGet_2 = decorate(saImmOm.saImmOmClassDescriptionGet_2)
saImmOmClassDescriptionMemoryFree_2 = \
    decorate(saImmOm.saImmOmClassDescriptionMemoryFree_2)
saImmOmClassDelete = decorate(saImmOm.saImmOmClassDelete)
saImmOmSearchInitialize_2 = decorate(saImmOm.saImmOmSearchInitialize_2)
saImmOmSearchNext_2 = decorate(saImmOm.saImmOmSearchNext_2)
saImmOmSearchFinalize = decorate(saImmOm.saImmOmSearchFinalize)
saImmOmAccessorInitialize = decorate(saImmOm.saImmOmAccessorInitialize)
saImmOmAccessorGet_2 = decorate(saImmOm.saImmOmAccessorGet_2)
saImmOmAccessorFinalize = decorate(saImmOm.saImmOmAccessorFinalize)
saImmOmAdminOwnerInitialize = decorate(saImmOm.saImmOmAdminOwnerInitialize)
saImmOmAdminOwnerSet = decorate(saImmOm.saImmOmAdminOwnerSet)
saImmOmAdminOwnerRelease = decorate(saImmOm.saImmOmAdminOwnerRelease)
saImmOmAdminOwnerFinalize = decorate(saImmOm.saImmOmAdminOwnerFinalize)
saImmOmAdminOwnerClear = decorate(saImmOm.saImmOmAdminOwnerClear)
saImmOmCcbInitialize = decorate(saImmOm.saImmOmCcbInitialize)
saImmOmCcbObjectCreate_2 = decorate(saImmOm.saImmOmCcbObjectCreate_2)
saImmOmCcbObjectDelete = decorate(saImmOm.saImmOmCcbObjectDelete)
saImmOmCcbObjectModify_2 = decorate(saImmOm.saImmOmCcbObjectModify_2)
saImmOmCcbApply = decorate(saImmOm.saImmOmCcbApply)
saImmOmCcbFinalize = decorate(saImmOm.saImmOmCcbFinalize)
saImmOmCcbGetErrorStrings = decorate(saImmOm.saImmOmCcbGetErrorStrings)
saImmOmAdminOperationInvoke_2 = decorate(saImmOm.saImmOmAdminOperationInvoke_2)
saImmOmAdminOperationInvokeAsync_2 = \
    decorate(saImmOm.saImmOmAdminOperationInvokeAsync_2)
saImmOmAdminOperationContinue = decorate(saImmOm.saImmOmAdminOperationContinue)
saImmOmAdminOperationContinueAsync = \
    decorate(saImmOm.saImmOmAdminOperationContinueAsync)
saImmOmAdminOperationContinuationClear = \
    decorate(saImmOm.saImmOmAdminOperationContinuationClear)


def initialize():
    """ Initialize the IMM OM library """
    version = SaVersionT('A', 2, 15)
    # Initialize IMM OM handle
    saImmOmInitialize(handle, None, version)
    # Initialize Accessor Handle
    saImmOmAccessorInitialize(handle, accessor_handle)


def get(object_name, attr_name_list=None, class_name=None):
    """ Obtain values of some attributes of the specified object

    Args:
        object_name (str): Object name
        attr_name_list (list): List of attributes
        class_name (str): Class name

    Returns:
        ImmObject: Imm object
    """
    # Always request the SaImmAttrClassName attribute if needed
    if attr_name_list and not class_name \
            and 'SaImmAttrClassName' not in attr_name_list \
            and not attr_name_list == ['SA_IMM_SEARCH_GET_CONFIG_ATTR']:
        attr_name_list.append('SaImmAttrClassName')

    attr_names = [SaImmAttrNameT(attr) for attr in attr_name_list] \
        if attr_name_list else None

    attributes = pointer(pointer(SaImmAttrValuesT_2()))

    try:
        saImmOmAccessorGet_2(accessor_handle, SaNameT(object_name),
                             attr_names, attributes)
    except SafException as err:
        if err.value == eSaAisErrorT.SA_AIS_ERR_NOT_EXIST:
            return None
        else:
            raise err

    attrs = {}
    attr_list = unmarshalNullArray(attributes)
    for attr in attr_list:
        attr_range = list(range(attr.attrValuesNumber))
        attrs[attr.attrName] = [attr.attrValueType,
                                [unmarshalSaImmValue(attr.attrValues[val],
                                                     attr.attrValueType)
                                 for val in attr_range]]
    if 'SaImmAttrClassName' not in attrs and class_name:
        attrs['SaImmAttrClassName'] = class_name

    return ImmObject(object_name, attrs)


def class_description_get(class_name):
    """ Get class description as a Python list

    Args:
        class_name (str): Class name

    Returns:
        list: List of class attributes
    """
    attr_defs = pointer(pointer(saImm.SaImmAttrDefinitionT_2()))
    category = saImm.SaImmClassCategoryT()
    saImmOmClassDescriptionGet_2(handle, class_name, category, attr_defs)

    return unmarshalNullArray(attr_defs)


def admin_op_invoke(dn, op_id, params=None):
    """ Invoke admin op for dn

    Args:
        dn (str): Object dn
        op_id (str): Operation id
        params (list): List of parameters
    """
    owner_handle = saImmOm.SaImmAdminOwnerHandleT()
    owner_name = saImmOm.SaImmAdminOwnerNameT(os.getlogin())
    saImmOmAdminOwnerInitialize(handle, owner_name, eSaBoolT.SA_TRUE,
                                owner_handle)
    index = dn.rfind(",")
    parent_name = SaNameT(dn[index+1:])
    object_names = [parent_name]
    saImmOmAdminOwnerSet(owner_handle, object_names,
                         eSaImmScopeT.SA_IMM_SUBTREE)
    if params is None:
        params = []

    object_dn = SaNameT(dn)
    retval = SaAisErrorT()

    saImmOmAdminOperationInvoke_2(owner_handle, object_dn, 0, op_id, params,
                                  retval, saAis.SA_TIME_ONE_SECOND * 10)

    if retval.value != eSaAisErrorT.SA_AIS_OK:
        print("saImmOmAdminOperationInvoke_2: %s" %
              eSaAisErrorT.whatis(retval.value))
        raise SafException(retval.value)

    saImmOmAdminOwnerFinalize(owner_handle)


def get_error_strings(ccb_handle):
    """ Return the current CCB error strings

    Args:
        ccb_handle (str): CCB handle

    Returns:
        list: List of error strings
    """
    c_strings = POINTER(SaStringT)()

    saImmOmCcbGetErrorStrings(ccb_handle, c_strings)

    return unmarshalNullArray(c_strings)


def get_rdn_attribute_for_class(class_name):
    """ Return the RDN attribute for the given class
    This is safe to call from OI callbacks.

    Args:
        class_name (str): Class name

    Returns:
        str: RDN attribute of the class
    """
    desc = class_description_get(class_name)
    for attr_desc in desc:
        if attr_desc.attrFlags & saImm.saImm.SA_IMM_ATTR_RDN:
            return attr_desc.attrName
    return None
