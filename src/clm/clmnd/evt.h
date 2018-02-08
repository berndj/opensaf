/*      -*- OpenSAF  -*-
 *
 * (C) Copyright 2010,2015 The OpenSAF Foundation
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
 * Author(s):  GoAhead Software
 *             Ericsson AB
 *
 */

#ifndef CLM_CLMND_EVT_H_
#define CLM_CLMND_EVT_H_

typedef NCS_IPC_MSG CLMNA_MBX_MSG;
typedef enum clmna_evt_type {
  CLMNA_EVT_INVALID = 0,
  CLMNA_EVT_CHANGE_MSG,
  CLMNA_EVT_JOIN_RESPONSE,
  CLMNA_EVT_MAX_MSG
} CLMNA_EVT_TYPE;

struct Change {
  bool caused_by_timer_expiry;
  NCSMDS_CHG change;
  NODE_ID node_id;
  MDS_SVC_ID svc_id;
};

struct JoinResponse {
  SaAisErrorT rc;
  SaNameT node_name;
};

typedef struct clmna_evt_tags {
  CLMNA_MBX_MSG next;
  CLMNA_EVT_TYPE type;
  union {
    struct Change change;
    struct JoinResponse join_response;
  };
} CLMNA_EVT;

#endif  // CLM_CLMND_EVT_H_
