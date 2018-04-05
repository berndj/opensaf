/*      -*- OpenSAF  -*-
 *
 * (C) Copyright 2008 The OpenSAF Foundation
 * Copyright Ericsson AB 2008, 2018 - All Rights Reserved.
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

#ifndef LGS_IMM_H
#define LGS_IMM_H

#include <time.h>
#include <string>

#include "ais/include/saImmOi.h"
#include "ais/include/saImmOm.h"

#include "log/logd/lgs_cb.h"

// Get the callback structure used when initializing an OI handle
const SaImmOiCallbacksT_2* getImmOiCallbacks(void);

void logRootDirectory_filemove(const std::string &new_logRootDirectory,
                                      const std::string &old_logRootDirectory,
                                      time_t *cur_time_in);
void logDataGroupname_fileown(const char *new_logDataGroupname);

SaAisErrorT lgs_imm_init_configStreams(lgs_cb_t *cb);

// Functions for recovery handling
void lgs_cleanup_abandoned_streams();
void lgs_delete_one_stream_object(const std::string &name_str);
void lgs_search_stream_objects();
SaUint32T *lgs_get_scAbsenceAllowed_attr(SaUint32T *attr_val);
int lgs_get_streamobj_attr(SaImmAttrValuesT_2 ***attrib_out,
                           const std::string &object_name,
                           SaImmHandleT *immOmHandle);
int lgs_free_streamobj_attr(SaImmHandleT immHandle);



#endif /* LGS_IMM_H */

