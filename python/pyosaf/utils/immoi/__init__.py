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
- Get IMM error strings
"""
from __future__ import print_function

from ctypes import c_char_p, c_void_p, cast, pointer

from pyosaf.saAis import SaStringT, SaVersionT, SaNameT, SaSelectionObjectT, \
    unmarshalSaStringTArray, eSaDispatchFlagsT
from pyosaf import saImm, saImmOi
from pyosaf.saImm import unmarshalSaImmValue, SaImmAttrNameT,      \
    SaImmAttrValuesT_2, SaImmClassNameT, SaImmSearchParametersT_2, \
    eSaImmValueTypeT, SaImmAttrDefinitionT_2, SaImmClassCategoryT, \
    SaImmAttrModificationT_2, eSaImmAttrModificationTypeT
from pyosaf.saImmOi import SaImmOiHandleT, SaImmOiImplementerNameT
from pyosaf.utils import immom
from pyosaf.utils.immom.object import ImmObject
from pyosaf.utils.immom.ccb import marshal_c_array
from pyosaf.utils.immom.iterator import SearchIterator
from pyosaf.utils import decorate, initialize_decorate


selection_object = SaSelectionObjectT()
handle = SaImmOiHandleT()
TRY_AGAIN_CNT = 60
OPENSAF_IMM_OBJECT = "opensafImm=opensafImm,safApp=safImmService"

# Decorate pure saImmOi* API's with error-handling retry and exception raising
saImmOiInitialize_2 = initialize_decorate(saImmOi.saImmOiInitialize_2)
saImmOiSelectionObjectGet = decorate(saImmOi.saImmOiSelectionObjectGet)
saImmOiDispatch = decorate(saImmOi.saImmOiDispatch)
saImmOiFinalize = decorate(saImmOi.saImmOiFinalize)
saImmOiImplementerSet = decorate(saImmOi.saImmOiImplementerSet)
saImmOiImplementerClear = decorate(saImmOi.saImmOiImplementerClear)
saImmOiClassImplementerSet = decorate(saImmOi.saImmOiClassImplementerSet)
saImmOiClassImplementerRelease = \
    decorate(saImmOi.saImmOiClassImplementerRelease)
saImmOiObjectImplementerSet = decorate(saImmOi.saImmOiObjectImplementerSet)
saImmOiObjectImplementerRelease = \
    decorate(saImmOi.saImmOiObjectImplementerRelease)
saImmOiRtObjectCreate_2 = decorate(saImmOi.saImmOiRtObjectCreate_2)
saImmOiRtObjectDelete = decorate(saImmOi.saImmOiRtObjectDelete)
saImmOiRtObjectUpdate_2 = decorate(saImmOi.saImmOiRtObjectUpdate_2)
saImmOiAdminOperationResult = decorate(saImmOi.saImmOiAdminOperationResult)
saImmOiAdminOperationResult_o2 = \
    decorate(saImmOi.saImmOiAdminOperationResult_o2)
saImmOiAugmentCcbInitialize = decorate(saImmOi.saImmOiAugmentCcbInitialize)
saImmOiCcbSetErrorString = decorate(saImmOi.saImmOiCcbSetErrorString)


def initialize(callbacks=None):
    """ Initialize the IMM OI library

    Args:
        callbacks (SaImmOiCallbacksT_2): OI callbacks to register with IMM
    """
    version = SaVersionT('A', 2, 15)
    saImmOiInitialize_2(handle, callbacks, version)


def register_applier(app_name):
    """ Register as an applier OI

    Args:
        app_name (str): Applier name
    """
    applier_name = "@" + app_name
    register_implementer(applier_name)


def register_implementer(oi_name):
    """ Register as an implementer

    Args:
        oi_name (str): Implementer name
    """
    implementer_name = SaImmOiImplementerNameT(oi_name)
    saImmOiImplementerSet(handle, implementer_name)


def get_selection_object():
    """ Get an selection object for event polling """
    global selection_object
    saImmOiSelectionObjectGet(handle, selection_object)


def implement_class(class_name):
    """ Register the implementer as an OI for the given class

    Args:
        class_name (str): Name of the class to implement
    """
    c_class_name = SaImmClassNameT(class_name)
    saImmOiClassImplementerSet(handle, c_class_name)


def dispatch(flags=eSaDispatchFlagsT.SA_DISPATCH_ALL):
    """ Dispatch all queued callbacks

    Args:
        flags (eSaDispatchFlagsT): Flags specifying dispatch mode
    """
    saImmOiDispatch(handle, flags)


def create_rt_object(class_name, parent_name, runtime_obj):
    """ Create a runtime object

    Args:
        class_name (str): Class name
        parent_name (str): Parent name
        runtime_obj (ImmObject): Runtime object to create
    """
    # Marshall parameters
    c_class_name = SaImmClassNameT(class_name)
    if parent_name:
        c_parent_name = SaNameT(parent_name)
    else:
        c_parent_name = None

    c_attr_values = []

    for name, (c_attr_type, values) in runtime_obj.attrs.items():
        if values is None:
            values = []
        elif values == [None]:
            values = []

        # Make sure all values are in lists
        if not isinstance(values, list):
            values = [values]

        # Create the values struct
        c_attr = SaImmAttrValuesT_2()
        c_attr.attrName = SaImmAttrNameT(name)
        c_attr.attrValueType = c_attr_type
        c_attr.attrValuesNumber = len(values)

        if not values:
            c_attr.attrValues = None
        else:
            c_attr.attrValues = marshal_c_array(c_attr_type, values)

        c_attr_values.append(c_attr)

    saImmOiRtObjectCreate_2(handle, c_class_name, c_parent_name, c_attr_values)


def delete_rt_object(dn):
    """ Delete a runtime object

    Args:
        dn (str): Runtime object dn
    """
    # Marshall the parameter
    c_dn = SaNameT(dn)
    saImmOiRtObjectDelete(handle, c_dn)


def update_rt_object(dn, attributes):
    """ Update the specified object with the requested attribute modifications

    Args:
        dn (str): Object dn
        attributes (dict): Dictionary of attribute modifications
    """
    # Get the class name for the object
    class_name = get_class_name_for_dn(dn)

    # Create and marshall attribute modifications
    attr_mods = []

    for name, values in attributes.items():
        if values is None:
            print("WARNING: Received no values for %s in %s" % (name, dn))
            continue
        if not isinstance(values, list):
            values = [values]

        attr_type = get_attribute_type(name, class_name)
        c_attr_mod = SaImmAttrModificationT_2()
        c_attr_mod.modType = \
            eSaImmAttrModificationTypeT.SA_IMM_ATTR_VALUES_REPLACE
        c_attr_mod.modAttr = SaImmAttrValuesT_2()
        c_attr_mod.modAttr.attrName = SaImmAttrNameT(name)
        c_attr_mod.modAttr.attrValueType = attr_type
        c_attr_mod.modAttr.attrValuesNumber = len(values)
        c_attr_mod.modAttr.attrValues = marshal_c_array(attr_type, values)
        attr_mods.append(c_attr_mod)

    saImmOiRtObjectUpdate_2(handle, SaNameT(dn), attr_mods)


def report_admin_operation_result(invocation_id, result):
    """ Report the result of an administrative operation

    Args:
        invocation_id (SaInvocationT): Invocation id
        result (SaAisErrorT): Result of admin operation
    """
    saImmOiAdminOperationResult(handle, invocation_id, result)


def set_error_string(ccb_id, error_string):
    """ Set the error string
    This can only be called from within OI callbacks of a real implementer.

    Args:
        ccb_id (SaImmOiCcbIdT): CCB id
        error_string (str): Error string
    """

    c_error_string = SaStringT(error_string)

    saImmOiCcbSetErrorString(handle, ccb_id, c_error_string)


def get_class_category(class_name):
    """ Return the category of the given class

    Args:
        class_name (str): Class name

    Returns:
        SaAisErrorT: Return code of class category get
    """
    c_attr_defs = pointer(pointer(SaImmAttrDefinitionT_2()))
    c_category = SaImmClassCategoryT()
    c_class_name = SaImmClassNameT(class_name)

    immom.saImmOmClassDescriptionGet_2(immom.handle, c_class_name, c_category,
                                       c_attr_defs)
    return c_category.value


def get_parent_name_for_dn(dn):
    """ Return the parent's dn of the given object's dn

    Args:
        dn (str): Object dn

    Returns:
        str: DN of the object's parent
    """
    if ',' in dn:
        return dn.split(',', 1)[1]

    return None


def get_object_names_for_class(class_name):
    """ Return instances of the given class
    This is safe to call from OI callbacks.

    Args:
        class_name (str): Class name

    Returns:
        list: List of object names
    """
    # Marshall the search parameter
    c_class_name = c_char_p(class_name)
    c_search_param = SaImmSearchParametersT_2()
    c_search_param.searchOneAttr.attrName = "SaImmAttrClassName"
    c_search_param.searchOneAttr.attrValueType = \
        eSaImmValueTypeT.SA_IMM_ATTR_SASTRINGT
    c_search_param.searchOneAttr.attrValue = \
        cast(pointer(c_class_name), c_void_p)

    # Create the search iterator
    found_objs = SearchIterator(search_param=c_search_param,
                                attribute_names=['SaImmAttrClassName'])

    # Return the dn's of found objects
    return [obj.dn for obj in found_objs]


def get_class_name_for_dn(dn):
    """ Return the class name for an instance with the given dn

    Args:
        dn (str): Object dn

    Returns:
        str: Class name
    """
    obj = immom.get(dn, ["SaImmAttrClassName"])
    if not obj:
        return None

    return obj.SaImmAttrClassName


def get_object_no_runtime(dn):
    """ Return the IMM object with the given dn

    This is safe to call from OI callbacks as only the config attributes will
    be read.
    The function needs to query the class to find config attributes. If the
    class name is known by the caller it can be passed in. Otherwise, it will
    be looked up.

    Args:
        dn (str): Object dn

    Returns:
        ImmObject: Imm object
    """
    return immom.get(dn, ['SA_IMM_SEARCH_GET_CONFIG_ATTR'])


def get_attribute_type(attribute, class_name):
    """ Return the type of the attribute in the given class
    This is safe to use from OI callbacks.

    Args:
        attribute (str): Attribute name
        class_name (str): Class name

    Returns:
        str: Attribute type
    """
    class_desc = immom.class_description_get(class_name)
    attr_desc = [attr for attr in class_desc if attr.attrName == attribute][0]

    return attr_desc.attrValueType


def get_rdn_attribute_for_class(class_name):
    """ Return the RDN attribute for the given class
    This is safe to call from OI callbacks.

    Args:
        class_name (str): Class name

    Returns:
        str: RDN of the class
    """
    desc = immom.class_description_get(class_name)

    for attr_desc in desc:
        if attr_desc.attrFlags & saImm.saImm.SA_IMM_ATTR_RDN:
            return attr_desc.attrName

    return None


def unmarshal_len_array(c_array, length, value_type):
    """ Convert C array with a known length to a Python list

    Args:
        c_array (C array): Array in C
        length (int): Length of array
        value_type (str): Element type in array

    Returns:
        list: The list converted from c_array
    """
    if not c_array:
        return []
    ctype = c_array[0].__class__
    if ctype is str:
        return unmarshalSaStringTArray(c_array)
    val_list = []
    i = 0
    for ptr in c_array:
        if i == length:
            break
        if not ptr:
            break
        val = unmarshalSaImmValue(ptr, value_type)
        val_list.append(val)
        i = i + 1

    return val_list


def get_available_classes_in_imm():
    """ Return a list of all available classes in IMM
    This is safe to call from OI callbacks.

    Returns:
        list: List of available classes
    """
    opensaf_imm = immom.get(OPENSAF_IMM_OBJECT)

    return opensaf_imm.opensafImmClassNames


def create_non_existing_imm_object(class_name, parent_name, attributes):
    """ Create an ImmObject instance for an object that has not yet existed
    in IMM

    Args:
        class_name (str): Class name
        parent_name (str): Parent name
        attributes (dict): Dictionary of class attributes

    Returns:
        ImmObject: Imm object
    """
    rdn_attribute = get_rdn_attribute_for_class(class_name)
    rdn_value = attributes[rdn_attribute][0]

    if parent_name:
        dn = '%s,%s' % (rdn_value, parent_name)
    else:
        dn = rdn_value

    obj = ImmObject(class_name=class_name, dn=dn)

    for name, values in attributes.items():
        obj.__setattr__(name, values)

    obj.__setattr__('SaImmAttrClassName', class_name)
    obj.__setattr__('dn', dn)

    return obj
