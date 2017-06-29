/*       OpenSAF
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

#ifndef SMF_SMFD_SMFD_CB_H_
#define SMF_SMFD_SMFD_CB_H_

#include <stdbool.h>
#include <saImmOm.h>
#include <saImmOi.h>
#include "smf/smfd/smfd_smfnd.h"

/* Default HA state assigned locally during smfd initialization */
#define SMFD_HA_INIT_STATE SA_AMF_HA_QUIESCED

#define SMFD_MDS_PVT_SUBPART_VERSION 1

typedef struct smfd_cb {
  SYSF_MBX mbx;       /* SMFD mailbox                           */
  V_DEST_RL mds_role; /* Current MDS role - ACTIVE/STANDBY      */
  uint32_t mds_handle;
  MDS_DEST mds_dest;      /* My destination in MDS                  */
  SaVersionT smf_version; /* The version currently supported        */
  SaNameT comp_name;      /* Components's name SMFD                 */
  SaAmfHandleT amf_hdl;   /* AMF handle, obtained thru AMF init     */
  SaInvocationT
      amf_invocation_id; /* AMF InvocationID - needed to handle Quiesed state */
  bool is_quiesced_set;
  SaSelectionObjectT
      amfSelectionObject; /* Selection Object to wait for amf events          */
  SaImmOiHandleT campaignOiHandle;            /* IMM Campaign OI handle            */
  SaSelectionObjectT campaignSelectionObject; /* Selection Object to wait for
                                                 campaign IMM events */
  SaAmfHAStateT ha_state; /* present AMF HA state of the component            */
  bool fully_initialized;
  char *backupCreateCmd;       /* Backup create cmd string     */
  char *bundleCheckCmd;        /* Bundle check cmd string      */
  char *nodeCheckCmd;          /* Node check cmd string        */
  char *repositoryCheckCmd;    /* Repository check cmd string  */
  char *clusterRebootCmd;      /* Cluster reboot cmd string    */
  SaTimeT adminOpTimeout;      /* Timeout for admin operations */
  SaTimeT cliTimeout;          /* Timeout for cli commands     */
  SaTimeT rebootTimeout;       /* Timeout for reboot to finish */
  char *nodeBundleActCmd;      /* Command for activation of bundles on a node */
  char *smfSiSwapSiName;       /* SI name for swap operation */
  SaUint32T smfSiSwapMaxRetry; /* Max number of SI_SWAP retries */
  SaUint32T smfCampMaxRestart; /* Max number of campaign restarts */
  char *smfImmPersistCmd;      /* Command for IMM persistance */
  char *smfNodeRebootCmd;      /* Command for node reboot */
  SaUint32T smfInactivatePbeDuringUpgrade; /* True (1) if PBE shall be
                                              deactivated during upgrade */
  SaUint32T
      smfVerifyEnable;      /* dis/enable pre-campaign verification callbacks */
  SaTimeT smfVerifyTimeout; /* pre-campaign verification timeout */
  NODE_ID smfNodeIdControllers[2]; /* NODE_ID of the nodes where amfd is
                                      executing */
  bool smfControllersUp[2]; /* True if the smfNodeIdControllers node is up */
  char *smfClusterControllers[2]; /* list of nodes where amfd is execting */
  SaUint32T smfKeepDuState;       /* Keep DU state in an upgrade if true (>0) */
  SaInvocationT cbk_inv_id;       /* Invocation ID of the callback */
  SMFD_SMFND_ADEST_INVID_MAP
      *smfnd_list; /* SMFNDs need to respond to the callback. */
  uint32_t no_of_smfnd;
  pthread_mutex_t lock;     /* Used by smfd_cb_t lock/unlock functions */
  pthread_mutex_t imm_lock; /* Used when IMM OI handle is shared between
                               campaign thread and main thread*/

} smfd_cb_t;

#ifdef __cplusplus
extern "C" {
#endif

extern uint32_t smfd_cb_init(smfd_cb_t *);

#ifdef __cplusplus
}
#endif
#endif  // SMF_SMFD_SMFD_CB_H_
