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
# Author(s): Oracle, Ericsson
#
############################################################################
""" pyosaf common utils """
import time
from copy import deepcopy

from pyosaf.saAis import eSaAisErrorT


TRY_AGAIN_COUNT = 60


class SafException(Exception):
    """ SAF Exception that can be printed """
    def __init__(self, value, msg=None):
        Exception.__init__(self)
        self.value = value
        self.msg = msg

    def __str__(self):
        return eSaAisErrorT.whatis(self.value)


def raise_saf_exception(func, error):
    """ Raise an exception for a given SAF function, based on the given
    error code
    """
    error_string = "%s: %s" % (func.__name__, eSaAisErrorT.whatis(error))
    raise SafException(error, error_string)


def decorate(func):
    """ Decorate the given SAF function so that it retries a fixed number of
    times if needed and raises an exception if it encounters any fault other
    than SA_AIS_ERR_TRY_AGAIN.
    """

    def inner(*args):
        """ Call "function" in the lexical scope in a retry loop and raise an
        exception if it encounters any fault other than TRY_AGAIN
        """
        one_sec_sleeps = 0
        error = func(*args)

        while error == eSaAisErrorT.SA_AIS_ERR_TRY_AGAIN:
            if one_sec_sleeps == TRY_AGAIN_COUNT:
                break
            time.sleep(1)
            one_sec_sleeps += 1

            error = func(*args)

        if error != eSaAisErrorT.SA_AIS_OK:
            raise_saf_exception(func, error)

        return error

    return inner


def initialize_decorate(func):
    """ Decorate the given SAF sa<Service>Initialize() function so that it
    retries a fixed number of times if needed with the same arguments and
    raises an exception if it encounters any fault other than
    SA_AIS_ERR_TRY_AGAIN.
    """

    def inner(*args):
        """ Call "function" in the lexical scope in a retry loop and raise an
        exception if it encounters any fault other than TRY_AGAIN

        Args:
            args (tuple): Arguments of SAF Initialize() function with format
                (handle, callbacks, version)
        """
        # Backup current version
        backup_version = deepcopy(args[2])

        one_sec_sleeps = 0
        error = func(*args)

        while error == eSaAisErrorT.SA_AIS_ERR_TRY_AGAIN:
            if one_sec_sleeps == TRY_AGAIN_COUNT:
                break
            time.sleep(1)
            one_sec_sleeps += 1

            # If the SAF Initialize() function returns ERR_TRY_AGAIN, the
            # version (as output argument) will still be updated to the latest
            # supported service API version; thus we need to restore the
            # original backed-up version before next retry of initialization.
            version = deepcopy(backup_version)
            args = args[:2] + (version,)
            error = func(*args)

        if error != eSaAisErrorT.SA_AIS_OK:
            raise_saf_exception(func, error)

        return error

    return inner
