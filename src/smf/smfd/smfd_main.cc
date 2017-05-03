/*      OpenSAF
 *
 * (C) Copyright 2010 The OpenSAF Foundation
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
 *            Wind River Systems
 *
 */

/* ========================================================================
 *   INCLUDE FILES
 * ========================================================================
 */

#include "osaf/configmake.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>
#include <poll.h>

#include "base/daemon.h"
#include "base/ncs_main_papi.h"

#include "osaf/saf/saAis.h"
#include "imm/saf/saImmOm.h"
#include "osaf/immutil/immutil.h"
#include "base/saf_error.h"
#include "base/osaf_extended_name.h"

#include "smfd.h"
#include "smf/common/smfsv_defs.h"
#include "smfd_evt.h"
#include "smfd_long_dn.h"

/* ========================================================================
 *   DEFINITIONS
 * ========================================================================
 */
extern struct ImmutilWrapperProfile immutilWrapperProfile;

/* ========================================================================
 *   TYPE DEFINITIONS
 * ========================================================================
 */

/* ========================================================================
 *   DATA DECLARATIONS
 * ========================================================================
 */

static smfd_cb_t smfd_cb_instance;
smfd_cb_t *smfd_cb = &smfd_cb_instance;

/* Store for Long Dn monitoring applier object */
SmfLongDnApplier *SmfLongDnInfo;

static SaNameT smfApplDN_instance;

static void smfApplDN_instance_constructor(void) __attribute__((constructor));

static void smfApplDN_instance_constructor(void) {
  osaf_extended_name_lend(SMF_SAF_APP_DN, &smfApplDN_instance);
}

const SaNameT *smfApplDN = &smfApplDN_instance;

/* ========================================================================
 *   FUNCTION PROTOTYPES
 * ========================================================================
 */

void smfd_cb_lock() {
  if (pthread_mutex_lock(&(smfd_cb->lock)) != 0) {
    LOG_ER("Lock failed pthread_mutex_lock");
    abort();
  }
}

void smfd_cb_unlock() {
  if (pthread_mutex_unlock(&(smfd_cb->lock)) != 0) {
    LOG_ER("Unlock failed pthread_mutex_unlock");
    abort();
  }
}

int smfd_imm_trylock() {
  TRACE_ENTER();
  int err;

  err = pthread_mutex_trylock(&(smfd_cb->imm_lock));
  if (err != 0) {
    if (err == EBUSY) {
      LOG_WA("Lock failed eith EBUSY pthread_mutex_trylock for imm %d", err);
      return 1;
    } else {
      LOG_ER("Lock failed pthread_mutex_trylock for imm %d", err);
      abort();
    }
  }
  TRACE_LEAVE();
  return err;
}

void smfd_imm_lock() {
  TRACE_ENTER();

  if (pthread_mutex_lock(&(smfd_cb->imm_lock)) != 0) {
    LOG_ER("Lock failed pthread_mutex_lock for imm");
    abort();
  }
  TRACE_LEAVE();
}

void smfd_imm_unlock() {
  TRACE_ENTER();
  if (pthread_mutex_unlock(&(smfd_cb->imm_lock)) != 0) {
    LOG_ER("Unlock failed pthread_mutex_unlock for imm");
    abort();
  }
  TRACE_LEAVE();
}

/****************************************************************************
 * Name          : smfd_cb_init
 *
 * Description   : This function initializes the SMFD_CB.
 *
 *
 * Arguments     : smfd_cb * - Pointer to the SMFD_CB.
 *
 * Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE.
 *
 * Notes         : None.
 *****************************************************************************/
uint32_t smfd_cb_init(smfd_cb_t *smfd_cb) {
  TRACE_ENTER();

  pthread_mutexattr_t mutex_attr, mutex_attr1;

  smfd_cb->amfSelectionObject = -1;
  smfd_cb->campaignSelectionObject = -1;
  smfd_cb->ha_state = SMFD_HA_INIT_STATE;
  smfd_cb->fully_initialized = false;

  /* TODO Assign Version. Currently, hardcoded, This will change later */
  smfd_cb->smf_version.releaseCode = SMF_RELEASE_CODE;
  smfd_cb->smf_version.majorVersion = SMF_MAJOR_VERSION;
  smfd_cb->smf_version.minorVersion = SMF_MINOR_VERSION;

  smfd_cb->amf_hdl = 0;

  smfd_cb->backupCreateCmd = NULL;
  smfd_cb->bundleCheckCmd = NULL;
  smfd_cb->nodeCheckCmd = NULL;
  smfd_cb->repositoryCheckCmd = NULL;
  smfd_cb->clusterRebootCmd = NULL;
  smfd_cb->smfnd_list = NULL;

  smfd_cb->smfNodeIdControllers[0] = 0x2010f;
  smfd_cb->smfNodeIdControllers[1] = 0x2020f;
  smfd_cb->smfControllersUp[0] = false;
  smfd_cb->smfControllersUp[1] = false;

  if (pthread_mutexattr_init(&mutex_attr) != 0) {
    LOG_ER("Failed pthread_mutexattr_init");
    return NCSCC_RC_FAILURE;
  }

  if (pthread_mutexattr_settype(&mutex_attr, PTHREAD_MUTEX_RECURSIVE) != 0) {
    LOG_ER("Failed pthread_mutexattr_settype");
    pthread_mutexattr_destroy(&mutex_attr);
    return NCSCC_RC_FAILURE;
  }

  if (pthread_mutex_init(&(smfd_cb->lock), &mutex_attr) != 0) {
    LOG_ER("Failed pthread_mutex_init");
    pthread_mutexattr_destroy(&mutex_attr);
    return NCSCC_RC_FAILURE;
  }

  if (pthread_mutexattr_destroy(&mutex_attr) != 0) {
    LOG_ER("Failed pthread_mutexattr_destroy");
    return NCSCC_RC_FAILURE;
  }

  if (pthread_mutexattr_init(&mutex_attr1) != 0) {
    LOG_ER("Failed pthread_mutexattr_init used for imm");
    return NCSCC_RC_FAILURE;
  }

  if (pthread_mutexattr_settype(&mutex_attr1, PTHREAD_MUTEX_RECURSIVE) != 0) {
    LOG_ER("Failed pthread_mutexattr_settype used for imm");
    pthread_mutexattr_destroy(&mutex_attr1);
    return NCSCC_RC_FAILURE;
  }

  if (pthread_mutex_init(&(smfd_cb->imm_lock), &mutex_attr1) != 0) {
    LOG_ER("Failed pthread_mutex_init used for imm");
    pthread_mutexattr_destroy(&mutex_attr1);
    return NCSCC_RC_FAILURE;
  }

  if (pthread_mutexattr_destroy(&mutex_attr1) != 0) {
    LOG_ER("Failed pthread_mutexattr_destroy used for imm");
    return NCSCC_RC_FAILURE;
  }

  TRACE_LEAVE();
  return NCSCC_RC_SUCCESS;
}

/**
 * Initialize smfd
 *
 * @return uns32
 */
static uint32_t initialize_smfd(void) {
  uint32_t rc;

  TRACE_ENTER();

  /* Set the behaviour of SMF-IMM interactions */
  immutilWrapperProfile.errorsAreFatal = 0;   /* False, no reboot when fail */
  immutilWrapperProfile.nTries = 600;         /* Times */
  immutilWrapperProfile.retryInterval = 1000; /* MS */

  if (ncs_agents_startup() != NCSCC_RC_SUCCESS) {
    LOG_ER("ncs_agents_startup FAILED");
    rc = NCSCC_RC_FAILURE;
    goto done;
  }

  osaf_extended_name_init();

  /* Initialize smfd control block */
  if (smfd_cb_init(smfd_cb) != NCSCC_RC_SUCCESS) {
    TRACE("smfd_cb_init FAILED");
    rc = NCSCC_RC_FAILURE;
    goto done;
  }

  /* Create the mailbox used for communication with SMFND */
  if ((rc = m_NCS_IPC_CREATE(&smfd_cb->mbx)) != NCSCC_RC_SUCCESS) {
    LOG_ER("m_NCS_IPC_CREATE FAILED %d", rc);
    goto done;
  }

  /* Attach mailbox to this thread */
  if (((rc = m_NCS_IPC_ATTACH(&smfd_cb->mbx)) != NCSCC_RC_SUCCESS)) {
    LOG_ER("m_NCS_IPC_ATTACH FAILED %d", rc);
    goto done;
  }

  /* Init with AMF */
  if ((rc = smfd_amf_init(smfd_cb)) != NCSCC_RC_SUCCESS) {
    LOG_ER("init amf failed");
    goto done;
  }

  if ((rc = initialize_for_assignment(smfd_cb, smfd_cb->ha_state)) !=
      NCSCC_RC_SUCCESS) {
    LOG_ER("initialize_for_assignment FAILED %u", (unsigned)rc);
    goto done;
  }

  /* Create a long dn monitoring applier */
  SmfLongDnInfo = new SmfLongDnApplier(IMM_CONFIG_OBJECT_DN,
                                       IMM_LONG_DN_CONFIG_ATTRIBUTE_NAME);
  TRACE("%s: Create applier for long Dn setting", __FUNCTION__);
  SmfLongDnInfo->Create();

done:
  TRACE_LEAVE();
  return (rc);
}

uint32_t initialize_for_assignment(smfd_cb_t *cb, SaAmfHAStateT ha_state) {
  TRACE_ENTER2("ha_state = %d", (int)ha_state);
  uint32_t rc = NCSCC_RC_SUCCESS;
  if (cb->fully_initialized || ha_state == SA_AMF_HA_QUIESCED) {
    goto done;
  }
  if ((rc = smfd_mds_init(cb)) != NCSCC_RC_SUCCESS) {
    LOG_ER("clms_mds_init FAILED %d", rc);
    goto done;
  }
  cb->fully_initialized = true;
done:
  TRACE_LEAVE2("rc = %u", rc);
  return rc;
}

typedef enum {
  SMFD_TERM_FD,
  SMFD_AMF_FD,
  SMFD_MBX_FD,
  SMFD_COI_FD,
  SMFD_MAX_FD
} smfd_pollfd_t;

struct pollfd fds[SMFD_MAX_FD];
static nfds_t nfds = SMFD_MAX_FD;

/**
 * Forever wait on events and process them.
 */
static void main_process(void) {
  NCS_SEL_OBJ mbx_fd;
  SaAisErrorT error = SA_AIS_OK;
  int term_fd;

  TRACE_ENTER();

  /*
    The initialization of the IMM OM handle below is done to use a feature in
    the IMM implementation. As long as one handle is initialized, IMM will not
    release the MDS subscription just reused it for new handles. When SMF uses
    SmfImmUtils new handles are created and released during the execution. If
    the campaign is big the MDS system limit of max number of subscriptions may
    be exceeded i.e. "ERR    |MDTM: SYSTEM has crossed the max =500
    subscriptions" The code below will ensure there is always one IMM OM handle
    initialized.
  */
  SaImmHandleT omHandle = 0;
  SaVersionT immVersion = {'A', 2, 17};
  SaAisErrorT rc = immutil_saImmOmInitialize(&omHandle, NULL, &immVersion);
  if (rc != SA_AIS_OK) {
    LOG_ER("immutil_saImmOmInitialize faild, rc=%s", saf_error(rc));
    return;
  }
  /* end of IMM featue code */

  mbx_fd = ncs_ipc_get_sel_obj(&smfd_cb->mbx);
  daemon_sigterm_install(&term_fd);

  /* Set up all file descriptors to listen to */
  fds[SMFD_TERM_FD].fd = term_fd;
  fds[SMFD_TERM_FD].events = POLLIN;
  fds[SMFD_AMF_FD].fd = smfd_cb->amfSelectionObject;
  fds[SMFD_AMF_FD].events = POLLIN;
  fds[SMFD_MBX_FD].fd = mbx_fd.rmv_obj;
  fds[SMFD_MBX_FD].events = POLLIN;
  fds[SMFD_COI_FD].fd = smfd_cb->campaignSelectionObject;
  fds[SMFD_COI_FD].events = POLLIN;

  while (1) {
    if (smfd_cb->campaignOiHandle != 0) {
      fds[SMFD_COI_FD].fd = smfd_cb->campaignSelectionObject;
      fds[SMFD_COI_FD].events = POLLIN;
      nfds = SMFD_MAX_FD;
    } else {
      nfds = SMFD_MAX_FD - 1;
    }

    int ret = poll(fds, nfds, -1);

    if (ret == -1) {
      if (errno == EINTR) continue;

      LOG_ER("poll failed - %s", strerror(errno));
      break;
    }

    if (fds[SMFD_TERM_FD].revents & POLLIN) {
      daemon_exit();
    }

    /* Process all the AMF messages */
    if (fds[SMFD_AMF_FD].revents & POLLIN) {
      /* dispatch all the AMF pending function */
      if ((error = saAmfDispatch(smfd_cb->amf_hdl, SA_DISPATCH_ALL)) !=
          SA_AIS_OK) {
        LOG_ER("saAmfDispatch failed: %u", error);
        break;
      }
    }

    /* Process all the Mail box events */
    if (fds[SMFD_MBX_FD].revents & POLLIN) {
      /* dispatch all the MBX events */
      smfd_process_mbx(&smfd_cb->mbx);
    }

    /* Process all the Imm callback events */
    if (fds[SMFD_COI_FD].revents & POLLIN) {
      if (smfd_imm_trylock() == 0) {
        /* In the case of IMMND restart, If trylock succedes then the oiDispatch
         * is called and IMM OI will get resurrected. Otherwise, the campign
         * thread has taken the imm_lock. Campign thread will resurrect the IMM
         * OI handle. trylock is used to unlock the main thread, if the
         * immlock is taken by campaign thread.
         */

        error = saImmOiDispatch(smfd_cb->campaignOiHandle, SA_DISPATCH_ALL);
        smfd_imm_unlock();
      }

      if (error != SA_AIS_OK) {
        /*
         ** BAD_HANDLE is interpreted as an IMM service restart. Try
         ** reinitialize the IMM OI API in a background thread and let
         ** this thread do business as usual especially handling write
         ** requests.
         **
         ** All other errors are treated as non-recoverable (fatal) and will
         ** cause an exit of the process.
         */
        if (error == SA_AIS_ERR_BAD_HANDLE) {
          TRACE("main: saImmOiDispatch returned BAD_HANDLE");

          /*
           ** Invalidate the IMM OI handle, this info is used in other
           ** locations. E.g. giving TRY_AGAIN responses to a create and
           ** close app stream requests. That is needed since the IMM OI
           ** is used in context of these functions.
           */
          saImmOiFinalize(smfd_cb->campaignOiHandle);
          smfd_cb->campaignOiHandle = 0;

          /* Initiate IMM reinitializtion in the background */
          smfd_coi_reinit_bg(smfd_cb);

        } else if (error != SA_AIS_OK) {
          LOG_ER("main: saImmOiDispatch FAILED %u", error);
          break;
        }
      }
    }
  }

  rc = immutil_saImmOmAdminOwnerFinalize(omHandle);
  if (rc != SA_AIS_OK) {
    LOG_ER("immutil_saImmOmAdminOwnerFinalize faild, rc=%s", saf_error(rc));
  }
}

/**
 * The main routine for the smfd daemon.
 * @param argc
 * @param argv
 *
 * @return int
 */
int main(int argc, char *argv[]) {
  if (setenv("SA_ENABLE_EXTENDED_NAMES", "1", 1) != 0)
    LOG_WA("smfd_main(): failed to setenv SA_ENABLE_EXTENDED_NAMES - %s",
           strerror(errno));
  daemonize(argc, argv);

  if (ncs_agents_startup() != NCSCC_RC_SUCCESS) {
    LOG_ER("ncs_agents_startup failed");
    goto done;
  }

  if (initialize_smfd() != NCSCC_RC_SUCCESS) {
    LOG_ER("initialize_smfd failed");
    goto done;
  }

  main_process();

  exit(EXIT_SUCCESS);

done:
  LOG_ER("SMFD failed, exiting...");
  exit(EXIT_FAILURE);
}
