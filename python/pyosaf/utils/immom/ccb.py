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
""" Class representing an IMM CCB """
from __future__ import print_function
import sys
from ctypes import c_void_p, pointer, cast

from pyosaf.saAis import eSaAisErrorT, eSaBoolT, SaNameT, SaStringT, \
    SaDoubleT, SaTimeT, SaUint64T, SaInt64T, SaUint32T, SaInt32T, SaFloatT
from pyosaf.saImm import eSaImmScopeT, eSaImmValueTypeT, SaImmAttrValuesT_2
from pyosaf import saImm
from pyosaf import saImmOm
from pyosaf.utils import immom
from pyosaf.utils import SafException


def _value_to_ctype_ptr(value_type, value):
    """ Convert a value to a ctypes value ptr

    Args:
        value_type (eSaImmValueTypeT): Value type
        value (ptr): Value in pointer

    Returns:
        c_void_p: ctype pointer which points to value
    """
    if sys.version_info > (3,):
        long = int
    if value_type is eSaImmValueTypeT.SA_IMM_ATTR_SAINT32T:
        ctypeptr = cast(pointer(SaInt32T(value)), c_void_p)
    elif value_type is eSaImmValueTypeT.SA_IMM_ATTR_SAUINT32T:
        ctypeptr = cast(pointer(SaUint32T(long(value))), c_void_p)
    elif value_type is eSaImmValueTypeT.SA_IMM_ATTR_SAINT64T:
        ctypeptr = cast(pointer(SaInt64T(long(value))), c_void_p)
    elif value_type is eSaImmValueTypeT.SA_IMM_ATTR_SAUINT64T:
        ctypeptr = cast(pointer(SaUint64T(long(value))), c_void_p)
    elif value_type is eSaImmValueTypeT.SA_IMM_ATTR_SATIMET:
        ctypeptr = cast(pointer(SaTimeT(long(value))), c_void_p)
    elif value_type is eSaImmValueTypeT.SA_IMM_ATTR_SANAMET:
        ctypeptr = cast(pointer(SaNameT(value)), c_void_p)
    elif value_type is eSaImmValueTypeT.SA_IMM_ATTR_SAFLOATT:
        ctypeptr = cast(pointer(SaFloatT(value)), c_void_p)
    elif value_type is eSaImmValueTypeT.SA_IMM_ATTR_SADOUBLET:
        ctypeptr = cast(pointer(SaDoubleT(value)), c_void_p)
    elif value_type is eSaImmValueTypeT.SA_IMM_ATTR_SASTRINGT:
        ctypeptr = cast(pointer(SaStringT(value)), c_void_p)
    elif value_type is eSaImmValueTypeT.SA_IMM_ATTR_SAANYT:
        assert 0
    else:
        assert 0

    return ctypeptr


def marshal_c_array(value_type, value_list):
    """ Create a C array initialized with values from value_list

    Args:
        value_type (eSaImmValueTypeT): Value type
        value_list (list): List of values

    Returns:
        c_void_p: ctype pointer which points to array
    """
    c_array_type = saImm.SaImmAttrValueT * len(value_list)
    c_array = c_array_type()
    for i, value in enumerate(value_list):
        c_array[i] = _value_to_ctype_ptr(value_type, value)
    return c_array


class Ccb(object):
    """ Class representing an ongoing CCB """
    def __init__(self, flags=saImm.saImm.SA_IMM_CCB_REGISTERED_OI):
        self.owner_handle = saImmOm.SaImmAdminOwnerHandleT()
        owner_name = saImmOm.SaImmAdminOwnerNameT("DummyName")

        immom.saImmOmAdminOwnerInitialize(immom.handle, owner_name,
                                          eSaBoolT.SA_TRUE, self.owner_handle)
        self.ccb_handle = saImmOm.SaImmCcbHandleT()

        if flags:
            ccb_flags = saImmOm.SaImmCcbFlagsT(flags)
        else:
            ccb_flags = saImmOm.SaImmCcbFlagsT(0)

        immom.saImmOmCcbInitialize(self.owner_handle, ccb_flags,
                                   self.ccb_handle)

    def __enter__(self):
        """ Called when Ccb is used in a 'with' statement as follows:
            with Ccb() as ccb:

        The call is invoked before any code within is run.
        """
        return self

    def __exit__(self, exit_type, value, traceback):
        """ Called when Ccb is used in a 'with' statement, just before it exits

        exit_type, value and traceback are only set if the with statement was
        left via an exception
        """
        if exit_type or value or traceback:
            self.__del__()
        else:
            self.apply()
            self.__del__()

    def __del__(self):
        """ Finalize the CCB """
        immom.saImmOmAdminOwnerFinalize(self.owner_handle)

    def create(self, obj, parent_name=None):
        """ Create the CCB object

        Args:
            obj (ImmObject): Imm object
            parent_name (str): Parent name
        """
        if parent_name is not None:
            parent_name = SaNameT(parent_name)
            object_names = [parent_name]
            immom.saImmOmAdminOwnerSet(self.owner_handle, object_names,
                                       eSaImmScopeT.SA_IMM_SUBTREE)
        else:
            parent_name = None

        attr_values = []
        for attr_name, type_values in obj.attrs.items():
            values = type_values[1]
            attr = SaImmAttrValuesT_2()
            attr.attrName = attr_name
            attr.attrValueType = type_values[0]
            attr.attrValuesNumber = len(values)
            attr.attrValues = marshal_c_array(attr.attrValueType, values)
            attr_values.append(attr)

        immom.saImmOmCcbObjectCreate_2(self.ccb_handle,
                                       obj.class_name,
                                       parent_name,
                                       attr_values)

    def delete(self, object_name):
        """ Add a delete operation of the object with the given DN to the CCB

        Args:
            object_name (str): Object name
        """
        if object_name is None:
            raise SafException(eSaAisErrorT.SA_AIS_ERR_NOT_EXIST)

        object_name = SaNameT(object_name)
        object_names = [object_name]

        immom.saImmOmAdminOwnerSet(self.owner_handle, object_names,
                                   eSaImmScopeT.SA_IMM_SUBTREE)
        immom.saImmOmCcbObjectDelete(self.ccb_handle, object_name)

    def modify_value_add(self, object_name, attr_name, values):
        """ Add to the CCB an ADD modification of an existing object

        Args:
            object_name (str): Object name
            attr_name (str): Attribute name
            values (list): List of attribute values
        """
        assert object_name

        # Make sure the values field is a list
        if not isinstance(values, list):
            values = [values]

        # First try to get the attribute value type by reading
        # the object's class description
        try:
            obj = immom.get(object_name)
        except SafException as err:
            print("failed: %s" % err)
            return

        object_name = SaNameT(object_name)
        object_names = [object_name]
        class_name = obj.SaImmAttrClassName
        value_type = None
        attr_def_list = immom.class_description_get(class_name)
        for attr_def in attr_def_list:
            if attr_def.attrName == attr_name:
                value_type = attr_def.attrValueType
                break

        if value_type is None:
            raise SafException(eSaAisErrorT.SA_AIS_ERR_NOT_EXIST,
                               "attribute '%s' does not exist" % attr_name)

        immom.saImmOmAdminOwnerSet(self.owner_handle, object_names,
                                   eSaImmScopeT.SA_IMM_ONE)
        attr_mods = []

        attr_mod = saImmOm.SaImmAttrModificationT_2()
        attr_mod.modType = \
            saImm.eSaImmAttrModificationTypeT.SA_IMM_ATTR_VALUES_ADD
        attr_mod.modAttr = SaImmAttrValuesT_2()
        attr_mod.modAttr.attrName = attr_name
        attr_mod.modAttr.attrValueType = value_type
        attr_mod.modAttr.attrValuesNumber = len(values)
        attr_mod.modAttr.attrValues = marshal_c_array(value_type, values)

        attr_mods.append(attr_mod)

        immom.saImmOmCcbObjectModify_2(self.ccb_handle, object_name, attr_mods)

    def modify_value_replace(self, object_name, attr_name, values):
        """ Add to the CCB an REPLACE modification of an existing object

        Args:
            object_name (str): Object name
            attr_name (str): Attribute name
            values (list): List of attribute values
        """
        assert object_name

        # Make sure the values field is a list
        if not isinstance(values, list):
            values = [values]

        # First try to get the attribute value type by reading
        # the object's class description
        try:
            obj = immom.get(object_name)
        except SafException as err:
            print("failed: %s" % err)
            return

        object_name = SaNameT(object_name)
        object_names = [object_name]
        class_name = obj.SaImmAttrClassName
        value_type = None
        attr_def_list = immom.class_description_get(class_name)
        for attr_def in attr_def_list:
            if attr_def.attrName == attr_name:
                value_type = attr_def.attrValueType
                break

        if value_type is None:
            raise SafException(eSaAisErrorT.SA_AIS_ERR_NOT_EXIST,
                               "attribute '%s' does not exist" % attr_name)

        immom.saImmOmAdminOwnerSet(self.owner_handle, object_names,
                                   eSaImmScopeT.SA_IMM_ONE)
        attr_mods = []

        attr_mod = saImmOm.SaImmAttrModificationT_2()
        attr_mod.modType = \
            saImm.eSaImmAttrModificationTypeT.SA_IMM_ATTR_VALUES_REPLACE
        attr_mod.modAttr = SaImmAttrValuesT_2()
        attr_mod.modAttr.attrName = attr_name
        attr_mod.modAttr.attrValueType = value_type
        attr_mod.modAttr.attrValuesNumber = len(values)
        attr_mod.modAttr.attrValues = marshal_c_array(value_type, values)

        attr_mods.append(attr_mod)

        immom.saImmOmCcbObjectModify_2(self.ccb_handle, object_name, attr_mods)

    def modify_value_delete(self, object_name, attr_name, values):
        """ Add to the CCB an DELETE modification of an existing object

        Args:
            object_name (str): Object name
            attr_name (str): Attribute name
            values (list): List of attribute values
        """
        assert object_name

        # Make sure the values field is a list
        if not isinstance(values, list):
            values = [values]

        # First try to get the attribute value type by reading
        # the object's class description
        try:
            obj = immom.get(object_name)
        except SafException as err:
            print("failed: %s" % err)
            return

        object_name = SaNameT(object_name)
        object_names = [object_name]
        class_name = obj.SaImmAttrClassName
        value_type = None
        attr_def_list = immom.class_description_get(class_name)
        for attr_def in attr_def_list:
            if attr_def.attrName == attr_name:
                value_type = attr_def.attrValueType
                break

        if value_type is None:
            raise SafException(eSaAisErrorT.SA_AIS_ERR_NOT_EXIST,
                               "attribute '%s' does not exist" % attr_name)

        immom.saImmOmAdminOwnerSet(self.owner_handle, object_names,
                                   eSaImmScopeT.SA_IMM_ONE)
        attr_mods = []

        attr_mod = saImmOm.SaImmAttrModificationT_2()
        attr_mod.modType = \
            saImm.eSaImmAttrModificationTypeT.SA_IMM_ATTR_VALUES_DELETE
        attr_mod.modAttr = SaImmAttrValuesT_2()
        attr_mod.modAttr.attrName = attr_name
        attr_mod.modAttr.attrValueType = value_type
        attr_mod.modAttr.attrValuesNumber = len(values)
        attr_mod.modAttr.attrValues = marshal_c_array(value_type, values)

        attr_mods.append(attr_mod)

        immom.saImmOmCcbObjectModify_2(self.ccb_handle, object_name, attr_mods)

    def apply(self):
        """ Apply the CCB """
        immom.saImmOmCcbApply(self.ccb_handle)


def test():
    """ A simple function to test the usage of Ccb class """
    ccb = Ccb()
    ccb.modify_value_replace("safAmfCluster=myAmfCluster",
                             "saAmfClusterStartupTimeout",
                             [10000000000])
    ccb.apply()
    del ccb


if __name__ == '__main__':
    test()
