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
"""
IMM OI common utilities

Supported functions:
- Set/clear/release implementer for class/object
- Create/delete/update runtime object
- Get class/object attributes
- Get/set IMM error strings
- Get parent for dn
- Get class for dn
- Get objects for class
- Get available classes of IMM
"""
from __future__ import print_function

from pyosaf.saAis import SaSelectionObjectT, eSaAisErrorT
from pyosaf.utils import deprecate, SafException
from pyosaf.utils.immom.object import ImmObject
from pyosaf.utils.immoi.agent import OiAgent


_oi_agent = None


# Keep old user interfaces of OI pyosaf utils
@deprecate
def initialize(callbacks=None):
    """ Initialize the IMM OI library

    Args:
        callbacks (SaImmOiCallbacksT_2): OI callbacks to register with IMM

    Raises:
        SafException: If the return code of the corresponding OI API call(s)
            is not SA_AIS_OK
    """
    global _oi_agent
    _oi_agent = OiAgent()

    rc = _oi_agent.initialize(callbacks=callbacks)
    if rc != eSaAisErrorT.SA_AIS_OK:
        raise SafException(rc)


@deprecate
def get_selection_object():
    """ Get an selection object for event polling

    Returns:
        SaSelectionObjectT: Return code of selection object get

    Raises:
        SafException: If the OI agent is not initialized
    """
    if _oi_agent is None:
        # SA_AIS_ERR_INIT is returned if user calls this function without first
        # calling initialize()
        raise SafException(eSaAisErrorT.SA_AIS_ERR_INIT)

    return _oi_agent.get_selection_object()


@deprecate
def create_rt_object(class_name, parent_name, runtime_obj):
    """ Create a runtime object

    Args:
        class_name (str): Class name
        parent_name (str): Parent name
        runtime_obj (ImmObject): Runtime object to create

    Raises:
        SafException: If the return code of the corresponding OI API call(s)
            is not SA_AIS_OK
    """
    if _oi_agent is None:
        initialize()

    rc = _oi_agent.create_runtime_object(class_name, parent_name,
                                         runtime_obj)

    if rc != eSaAisErrorT.SA_AIS_OK:
        raise SafException(rc)


@deprecate
def delete_rt_object(dn):
    """ Delete a runtime object

    Args:
        dn (str): Runtime object dn

    Raises:
        SafException: If the return code of the corresponding OI API call(s)
            is not SA_AIS_OK
    """
    if _oi_agent is None:
        initialize()

    rc = _oi_agent.delete_runtime_object(dn)

    if rc != eSaAisErrorT.SA_AIS_OK:
        raise SafException(rc)


@deprecate
def update_rt_object(dn, attributes):
    """ Update the specified object with the requested attribute
    modifications

    Args:
        dn (str): Object dn
        attributes (dict): Dictionary of attribute modifications

    Raises:
        SafException: If the return code of the corresponding OI API call(s)
            is not SA_AIS_OK
    """
    if _oi_agent is None:
        initialize()

    rc = _oi_agent.update_runtime_object(dn, attributes)

    if rc != eSaAisErrorT.SA_AIS_OK:
        raise SafException(rc)


@deprecate
def report_admin_operation_result(invocation_id, result):
    """ Report the result of an administrative operation

    Args:
        invocation_id (SaInvocationT): Invocation id
        result (SaAisErrorT): Result of admin operation

    Raises:
        SafException: If the return code of the corresponding OI API call(s)
            is not SA_AIS_OK
    """
    if _oi_agent is None:
        initialize()

    rc = _oi_agent.report_admin_operation_result(invocation_id, result)

    if rc != eSaAisErrorT.SA_AIS_OK:
        raise SafException(rc)


@deprecate
def set_error_string(ccb_id, error_string):
    """ Set the error string
    This can only be called from within OI callbacks of a real implementer.

    Args:
        ccb_id (SaImmOiCcbIdT): CCB id
        error_string (str): Error string

    Raises:
        SafException: If the return code of the corresponding OI API call(s)
            is not SA_AIS_OK
    """
    if _oi_agent is None:
        initialize()

    rc = _oi_agent.set_error_string(ccb_id, error_string)

    if rc != eSaAisErrorT.SA_AIS_OK:
        raise SafException(rc)


@deprecate
def get_parent_name_for_dn(dn):
    """ Return the parent's dn of the given object's dn

    Args:
        dn (str): Object dn

    Returns:
        str: DN of the object's parent
    """
    if _oi_agent is None:
        initialize()

    return _oi_agent.get_parent_name_for_dn(dn)


@deprecate
def get_object_names_for_class(class_name):
    """ Return instances of the given class

    Args:
        class_name (str): Class name

    Returns:
        list: List of object names
    """
    if _oi_agent is None:
        initialize()

    return _oi_agent.get_object_names_for_class(class_name)


@deprecate
def get_class_name_for_dn(dn):
    """ Return the class name for an instance with the given dn

    Args:
        dn (str): Object dn

    Returns:
        str: Class name
    """
    if _oi_agent is None:
        initialize()

    return _oi_agent.get_class_name_for_dn(dn)


@deprecate
def get_object_no_runtime(dn):
    """ Return the IMM object with the given dn

    Args:
        dn (str): Object dn

    Returns:
        ImmObject: Imm object
    """
    if _oi_agent is None:
        initialize()

    return _oi_agent.get_object_no_runtime(dn)


@deprecate
def get_attribute_type(attribute, class_name):
    """ Return the type of the attribute in the given class

    Args:
        attribute (str): Attribute name
        class_name (str): Class name

    Returns:
        str: Attribute type
    """
    if _oi_agent is None:
        initialize()

    return _oi_agent.get_attribute_type(attribute, class_name)


@deprecate
def get_available_classes_in_imm():
    """ Return a list of all available classes in IMM

    Returns:
        list: List of available classes
    """
    if _oi_agent is None:
        initialize()

    return _oi_agent.get_available_classes_in_imm()


@deprecate
def create_non_existing_imm_object(class_name, parent_name,
                                   attributes):
    """ Create an ImmObject instance for an object that has not yet existed
    in IMM

    Args:
        class_name (str): Class name
        parent_name (str): Parent name
        attributes (dict): Dictionary of class attributes

    Returns:
        ImmObject: Imm object
    """
    if _oi_agent is None:
        initialize()

    return _oi_agent.create_non_existing_imm_object(class_name, parent_name,
                                                    attributes)
