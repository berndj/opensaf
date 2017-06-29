/*      OpenSAF
 *
 * (C) Copyright 2008 The OpenSAF Foundation
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

#ifndef SMF_SMFD_SMFD_H_
#define SMF_SMFD_SMFD_H_

/* ========================================================================
 *   INCLUDE FILES
 * ========================================================================
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <saAmf.h>

#include "base/ncsgl_defs.h"
#include "base/ncs_lib.h"
#include "base/ncs_util.h"
#include "mds/mds_papi.h"
#include "base/ncs_mda_pvt.h"
#include "base/logtrace.h"

/* SMF files */
#include "smfd_cb.h"
#include "smf/common/smfsv_evt.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 *   DEFINITIONS
 * ========================================================================
 */
#define IMM_CONFIG_OBJECT_DN "opensafImm=opensafImm,safApp=safImmService"
#define IMM_LONG_DN_CONFIG_ATTRIBUTE_NAME "longDnsAllowed"
#define SMF_SAF_APP_DN "safApp=safSmfService"
#define SMF_CAMP_RESTART_INDICATOR_RDN "smfCampaignRestartIndicator=smf"
#define SMF_CAMPAIGN_OI_NAME "safSmfCampaign"
#define SMF_PROC_OI_NAME_PREFIX "safSmfProc"
#define SMF_MERGED_SS_PROC_NAME "safSmfProc=SmfSSMergedProc"

/* SMF execution modes */
#define SMF_STANDARD_MODE 0
#define SMF_MERGE_TO_SINGLE_STEP 1
#define SMF_BALANCED_MODE 2

/* ========================================================================
 *   TYPE DEFINITIONS
 * ========================================================================
 */

#define ONE_SECOND 1

/* ========================================================================
 *   DATA DECLARATIONS
 * ========================================================================
 */
extern smfd_cb_t *smfd_cb;
extern const SaNameT *smfApplDN;
extern uint32_t initialize_for_assignment(smfd_cb_t *cb,
                                          SaAmfHAStateT ha_state);
extern SaAisErrorT smfd_amf_init(smfd_cb_t *cb);
extern uint32_t smfd_mds_init(smfd_cb_t *);
extern uint32_t smfd_mds_finalize(smfd_cb_t *);
extern uint32_t smfd_mds_change_role(smfd_cb_t *);
extern uint32_t smfd_mds_msg_send(smfd_cb_t *cb, SMFSV_EVT *evt, MDS_DEST *dest,
                                  MDS_SYNC_SND_CTXT *mds_ctxt,
                                  MDS_SEND_PRIORITY_TYPE prio);

uint32_t campaign_oi_activate(smfd_cb_t *cb);
uint32_t campaign_oi_deactivate(smfd_cb_t *cb);
uint32_t campaign_oi_init(smfd_cb_t *cb);
uint32_t read_config_and_set_control_block(smfd_cb_t *cb);
extern void smfd_coi_reinit_bg(smfd_cb_t *cb);
uint32_t updateImmAttr(const char *dn, SaImmAttrNameT attributeName,
                       SaImmValueTypeT attrValueType, void *value);

void smfd_cb_lock();
void smfd_cb_unlock();
void smfd_imm_lock();
void smfd_imm_unlock();
int smfd_imm_trylock();

#ifdef __cplusplus
}
#endif
#endif  // SMF_SMFD_SMFD_H_
