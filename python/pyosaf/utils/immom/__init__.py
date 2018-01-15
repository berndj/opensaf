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
from pyosaf.saAis import eSaAisErrorT

from pyosaf.utils import deprecate, SafException
from pyosaf.utils.immom import agent
from pyosaf.utils.immom.object import ImmObject
from pyosaf.utils.immom.accessor import ImmOmAccessor


# Decorate pure saImmOm* API's with error-handling retry and exception raising
saImmOmInitialize = agent.saImmOmInitialize
saImmOmSelectionObjectGet = agent.saImmOmSelectionObjectGet
saImmOmDispatch = agent.saImmOmDispatch
saImmOmFinalize = agent.saImmOmFinalize
saImmOmClassCreate_2 = agent.saImmOmClassCreate_2
saImmOmClassDescriptionGet_2 = agent.saImmOmClassDescriptionGet_2
saImmOmClassDescriptionMemoryFree_2 = \
    agent.saImmOmClassDescriptionMemoryFree_2
saImmOmClassDelete = agent.saImmOmClassDelete
saImmOmSearchInitialize_2 = agent.saImmOmSearchInitialize_2
saImmOmSearchNext_2 = agent.saImmOmSearchNext_2
saImmOmSearchFinalize = agent.saImmOmSearchFinalize
saImmOmAccessorInitialize = agent.saImmOmAccessorInitialize
saImmOmAccessorGet_2 = agent.saImmOmAccessorGet_2
saImmOmAccessorFinalize = agent.saImmOmAccessorFinalize
saImmOmAdminOwnerInitialize = agent.saImmOmAdminOwnerInitialize
saImmOmAdminOwnerSet = agent.saImmOmAdminOwnerSet
saImmOmAdminOwnerRelease = agent.saImmOmAdminOwnerRelease
saImmOmAdminOwnerFinalize = agent.saImmOmAdminOwnerFinalize
saImmOmAdminOwnerClear = agent.saImmOmAdminOwnerClear
saImmOmCcbInitialize = agent.saImmOmCcbInitialize
saImmOmCcbObjectCreate_2 = agent.saImmOmCcbObjectCreate_2
saImmOmCcbObjectDelete = agent.saImmOmCcbObjectDelete
saImmOmCcbObjectModify_2 = agent.saImmOmCcbObjectModify_2
saImmOmCcbApply = agent.saImmOmCcbApply
saImmOmCcbFinalize = agent.saImmOmCcbFinalize
saImmOmCcbGetErrorStrings = agent.saImmOmCcbGetErrorStrings
saImmOmAdminOperationInvoke_2 = agent.saImmOmAdminOperationInvoke_2
saImmOmAdminOperationInvokeAsync_2 = agent.saImmOmAdminOperationInvokeAsync_2
saImmOmAdminOperationContinue = agent.saImmOmAdminOperationContinue
saImmOmAdminOperationContinueAsync = agent.saImmOmAdminOperationContinueAsync
saImmOmAdminOperationContinuationClear = \
    agent.saImmOmAdminOperationContinuationClear

_om_agent = None


@deprecate
def initialize():
    """ Initialize the IMM OM library

    Raises:
        SafException: If any IMM OM API call did not return SA_AIS_OK
    """
    global _om_agent
    _om_agent = agent.ImmOmAgent()

    # Initialize IMM OM handle and return the API return code
    rc = _om_agent.init()
    if rc != eSaAisErrorT.SA_AIS_OK:
        raise SafException(rc)


@deprecate
def get(object_name, attr_name_list=None, class_name=None):
    """ Obtain values of some attributes of the specified object

    Args:
        object_name (str): Object name
        attr_name_list (list): List of attributes
        class_name (str): Class name

    Returns:
        ImmObject: Imm object

    Raises:
        SafException: If any IMM OM API call did not return SA_AIS_OK
    """
    _accessor = ImmOmAccessor()
    _accessor.init()
    rc, imm_object = _accessor.get(object_name, attr_name_list, class_name)

    if rc != eSaAisErrorT.SA_AIS_OK:
        raise SafException(rc)

    return imm_object


@deprecate
def class_description_get(class_name):
    """ Get class description as a Python list

    Args:
        class_name (str): Class name

    Returns:
        list: List of class attributes

    Raises:
        SafException: If any IMM OM API call did not return SA_AIS_OK
    """
    if _om_agent is None:
        initialize()

    rc, class_attrs = _om_agent.get_class_description(class_name)

    if rc != eSaAisErrorT.SA_AIS_OK:
        raise SafException(rc)

    return class_attrs


@deprecate
def admin_op_invoke(dn, op_id, params=None):
    """ Invoke admin op for dn

    Args:
        dn (str): Object dn
        op_id (str): Operation id
        params (list): List of parameters

    Raises:
        SafException: If any IMM OM API call did not return SA_AIS_OK
    """
    if _om_agent is None:
        initialize()

    rc = _om_agent.invoke_admin_operation(dn, op_id, params)

    if rc != eSaAisErrorT.SA_AIS_OK:
        raise SafException(rc)


@deprecate
def get_rdn_attribute_for_class(class_name):
    """ Return the RDN attribute for the given class
    This is safe to call from OI callbacks.

    Args:
        class_name (str): Class name

    Returns:
        str: RDN attribute of the class
    """
    if _om_agent is None:
        initialize()

    return _om_agent.get_rdn_attribute_for_class(class_name)


@deprecate
def get_class_category(class_name):
    """ Return the category of the given class

    Args:
        class_name (str): Class name

    Returns:
        SaImmClassCategoryT: Class category
    """
    if _om_agent is None:
        initialize()

    return _om_agent.get_class_category(class_name)
