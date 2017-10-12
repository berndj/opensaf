/*      -*- OpenSAF  -*-
 *
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

#ifndef SRC_LOG_IMMWRP_TESTS_COMMON_H_
#define SRC_LOG_IMMWRP_TESTS_COMMON_H_

//>
// Help macros to form testing data - simulated output data which has
// same format as one returned by C IMM APIs.
//<

// Help macro to convert input type to string
#define TO_STRING(input)            (#input)

// Help macro to form attribute name for given AIS data type
// E.g: ATTR_NAME(SaUint32T) will get string `S_SaUint32T`
#define ATTR_NAME(type)             (TO_STRING(S_##type))

// Help macro to get attribute variable name for given AIS data type
// e.g: ATTR_VALUE(SaInt32T)  --> will get `v_SaInt32T` variable
//      &ATTR_VALUE(SaInt32T) --> is the same as &v_SaInt32T
#define ATTR_VALUE(type)            (v_##type)
#define ATTR_VALUE1(type)           (v_##type##_1)

// Help macro to get `SaImmAttrDefinitionT_2` variable for given AIS data type
// e.g: ATTR_DEFINE_VALUE(SaUint32T)  --> is the same as `cfg_attr_SaUint32T`
//      M_ATTR_DEFINE_VALUE(SaUint32T) --> is the same as `m_cfg_attr_SaUint32T`
#define ATTR_DEFINE_VALUE(type)    (cfg_attr_##type)
#define M_ATTR_DEFINE_VALUE(type)  (m_cfg_attr_##type)

// Help macro to get SaImmAttrValueT** for given AIS data type
// e.g: MULTIPLE_VALUE(SaInt32T) --> will get `m_SaInt32T`
#define MULTIPLE_VALUE(type)       (m_##type)

#endif  // SRC_LOG_IMMWRP_TESTS_COMMON_H__
