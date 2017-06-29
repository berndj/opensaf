/*
 *
 * (C) Copyright 2009 The OpenSAF Foundation
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

/* ========================================================================
 *   INCLUDE FILES
 * ========================================================================
 */
#include <saAis.h>
#include "osaf/immutil/immutil.h"
#include "base/saf_error.h"

#include "base/logtrace.h"
#include "base/osaf_extended_name.h"
#include "smf/smfd/SmfUpgradeCampaign.h"
#include "smf/smfd/SmfUpgradeProcedure.h"
#include "smf/smfd/SmfUpgradeStep.h"
#include "smf/smfd/SmfProcState.h"
#include "smf/smfd/SmfStepState.h"
#include "smf/smfd/SmfCampaignThread.h"
#include "smf/smfd/SmfProcedureThread.h"
#include "smf/smfd/SmfUpgradeAction.h"
#include "smf/smfd/SmfRollback.h"
#include "smf/smfd/SmfUtils.h"
#include "smfd.h"
#include "SmfExecControl.h"

/* ========================================================================
 *   DEFINITIONS
 * ========================================================================
 */

/* ========================================================================
 *   TYPE DEFINITIONS
 * ========================================================================
 */

/* ========================================================================
 *   DATA DECLARATIONS
 * ========================================================================
 */

/* ========================================================================
 *   FUNCTION PROTOTYPES
 * ========================================================================
 */

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
// ------Base class SmfProcState------------------------------------------------
//
// SmfProcState default implementations
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// getClassName()
//------------------------------------------------------------------------------
std::string SmfProcState::getClassName() const { return "SmfProcState"; }

//------------------------------------------------------------------------------
// execute()
//------------------------------------------------------------------------------
SmfProcResultT SmfProcState::execute(SmfUpgradeProcedure *i_proc) {
  TRACE_ENTER();
  LOG_NO(
      "SmfProcState::execute default implementation, should NEVER be executed.");
  TRACE_LEAVE();
  return SMF_PROC_DONE;
}

//------------------------------------------------------------------------------
// executeStep()
//------------------------------------------------------------------------------
SmfProcResultT SmfProcState::executeStep(SmfUpgradeProcedure *i_proc) {
  TRACE_ENTER();
  LOG_NO(
      "SmfProcState:: executeStep default implementation, should NEVER be executed.");
  TRACE_LEAVE();
  return SMF_PROC_DONE;
}

//------------------------------------------------------------------------------
// rollbackStep()
//------------------------------------------------------------------------------
SmfProcResultT SmfProcState::rollbackStep(SmfUpgradeProcedure *i_proc) {
  TRACE_ENTER();
  LOG_NO(
      "SmfProcState:: rollbackStep default implementation, should NEVER be executed.");
  TRACE_LEAVE();
  return SMF_PROC_DONE;
}

//------------------------------------------------------------------------------
// rollback()
//------------------------------------------------------------------------------
SmfProcResultT SmfProcState::rollback(SmfUpgradeProcedure *i_proc) {
  TRACE_ENTER();
  LOG_NO(
      "SmfProcState::rollback default implementation, should NEVER be executed.");
  TRACE_LEAVE();
  return SMF_PROC_DONE;
}

//------------------------------------------------------------------------------
// suspend()
//------------------------------------------------------------------------------
SmfProcResultT SmfProcState::suspend(SmfUpgradeProcedure *i_proc) {
  TRACE_ENTER();
  /* All procedures should always respond to suspend */
  TRACE("SmfProcState::suspend default implementation, send response.");
  TRACE_LEAVE();
  return SMF_PROC_SUSPENDED;
}

//------------------------------------------------------------------------------
// commit()
//------------------------------------------------------------------------------
SmfProcResultT SmfProcState::commit(SmfUpgradeProcedure *i_proc) {
  SaNameT objectName;
  SmfImmUtils immUtil;

  TRACE_ENTER();

  /* Remove upgrade procedure object (and the whole subtree) from IMM
     The children objects in the subtree are:
     -SaSmfStep
     -SaSmfActivationUnit,
     -SaSmfDeactivationUnit
     -SaSmfImageNodes
     -OpenSafSmfSingleStepInfo
  */

  osaf_extended_name_lend(i_proc->getDn().c_str(), &objectName);

  SaAisErrorT rc = immutil_saImmOiRtObjectDelete(
      i_proc->getProcThread()->getImmHandle(),  // The OI handle
      &objectName);

  if (rc != SA_AIS_OK) {
    LOG_NO("immutil_saImmOiRtObjectDelete returned %u for %s", rc,
           i_proc->getDn().c_str());
  }

  TRACE_LEAVE();
  return SMF_PROC_DONE;
}

//------------------------------------------------------------------------------
// changeState()
//------------------------------------------------------------------------------
void SmfProcState::changeState(SmfUpgradeProcedure *i_proc,
                               SmfProcState *i_state) {
  TRACE_ENTER();
  TRACE("SmfProcState::changeState");

  std::string newState = i_state->getClassName();
  std::string oldState = i_proc->m_state->getClassName();

  TRACE("SmfProcState::changeState old state=%s , new state=%s",
        oldState.c_str(), newState.c_str());
  i_proc->changeState(i_state);

  TRACE_LEAVE();
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
// SmfProcStateInitial implementations
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------

SmfProcState *SmfProcStateInitial::s_instance = NULL;

SmfProcState *SmfProcStateInitial::instance(void) {
  if (s_instance == NULL) {
    s_instance = new SmfProcStateInitial;
  }

  return s_instance;
}

//------------------------------------------------------------------------------
// getClassName()
//------------------------------------------------------------------------------
std::string SmfProcStateInitial::getClassName() const {
  return "SmfProcStateInitial";
}

//------------------------------------------------------------------------------
// execute()
//------------------------------------------------------------------------------
SmfProcResultT SmfProcStateInitial::execute(SmfUpgradeProcedure *i_proc) {
  TRACE_ENTER();

  TRACE("SmfProcStateInitial::execute implementation");

  LOG_NO("PROC: Start upgrade procedure %s", i_proc->getProcName().c_str());

  changeState(i_proc, SmfProcStateExecuting::instance());
  SmfProcResultT initResult = executeInit(i_proc);

  TRACE_LEAVE();
  return initResult;
}

//------------------------------------------------------------------------------
// executeInit()
//------------------------------------------------------------------------------
SmfProcResultT SmfProcStateInitial::executeInit(SmfUpgradeProcedure *i_proc) {
  TRACE_ENTER();
  LOG_NO("PROC: Start procedure init actions");
  auto camp = SmfCampaignThread::instance()->campaign();
  int procExecMode = camp->getUpgradeCampaign()->getProcExecutionMode();
  if (procExecMode == SMF_MERGE_TO_SINGLE_STEP) {
    LOG_NO(
        "SmfProcStateInitial::executeInit, Merge procedures into single step");
    if (!i_proc->mergeStepIntoSingleStep(i_proc)) {
      changeState(i_proc, SmfProcStateExecFailed::instance());
      LOG_NO(
          "SmfProcStateExecuting::executeInit:Rolling to single merging failes");
      TRACE_LEAVE();
      return SMF_PROC_FAILED;
    }
  } else if (procExecMode == SMF_BALANCED_MODE) {
    if (i_proc->getBalancedGroup().empty()) {
      TRACE(
          "SmfProcStateInitial::executeInit, Calculate steps for original procedure");
      if (!i_proc->calculateSteps()) {
        LOG_NO("SmfProcStateExecuting::Step calculation failed");
        return SMF_PROC_FAILED;
      }
      execctrl::disableMergeSteps(i_proc);
    } else {
      LOG_NO(
          "SmfProcStateInitial::executeInit, create step for balanced procedure");
      if (!execctrl::createStepForBalancedProc(i_proc)) {
        changeState(i_proc, SmfProcStateExecFailed::instance());
        LOG_NO(
            "SmfProcStateExecuting::executeInit: failed to create balanced steps");
        TRACE_LEAVE();
        return SMF_PROC_FAILED;
      }
    }
  } else {
    TRACE("SmfProcStateInitial::executeInit, Calculate steps");
    if (!i_proc->calculateSteps()) {
      changeState(i_proc, SmfProcStateExecFailed::instance());
      LOG_NO("SmfProcStateExecuting::executeInit:Step calculation failes");
      TRACE_LEAVE();
      return SMF_PROC_FAILED;
    }
  }

  TRACE("SmfProcStateInitial::executeInit, Create step objects in IMM");
  if (!i_proc->createImmSteps()) {
    changeState(i_proc, SmfProcStateExecFailed::instance());
    LOG_NO("SmfProcStateInitial::executeInit:createImmSteps in IMM failes");
    TRACE_LEAVE();
    return SMF_PROC_FAILED;
  }

  TRACE("SmfProcStateInitial::executeInit: Execute init actions");
  std::string initRollbackDn;
  SaAisErrorT result;
  initRollbackDn = "smfRollbackElement=ProcInit,";
  initRollbackDn += i_proc->getDn();

  if ((result = smfCreateRollbackElement(
           initRollbackDn, i_proc->getProcThread()->getImmHandle())) !=
      SA_AIS_OK) {
    changeState(i_proc, SmfProcStateExecFailed::instance());
    LOG_NO("SmfProcStateInitial failed to create init rollback element %s, %s",
           initRollbackDn.c_str(), saf_error(result));
    TRACE_LEAVE();
    return SMF_PROC_FAILED;
  }
  const std::vector<SmfUpgradeAction *> &initActions = i_proc->getInitActions();
  std::vector<SmfUpgradeAction *>::const_iterator iter;

  for (iter = initActions.begin(); iter != initActions.end(); ++iter) {
    if ((result = (*iter)->execute(i_proc->getProcThread()->getImmHandle(),
                                   &initRollbackDn)) != SA_AIS_OK) {
      changeState(i_proc, SmfProcStateExecFailed::instance());
      LOG_NO("Init action %d failed, rc=%s", (*iter)->getId(),
             saf_error(result));
      TRACE_LEAVE();
      return SMF_PROC_FAILED;
    }
  }

  LOG_NO("PROC: Procedure init actions completed");
  LOG_NO("PROC: Start executing the steps");
  return SMF_PROC_CONTINUE;
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
// SmfProcStateExecuting implementations
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
SmfProcState *SmfProcStateExecuting::s_instance = NULL;

SmfProcState *SmfProcStateExecuting::instance(void) {
  if (s_instance == NULL) {
    s_instance = new SmfProcStateExecuting;
  }

  return s_instance;
}

//------------------------------------------------------------------------------
// getClassName()
//------------------------------------------------------------------------------
std::string SmfProcStateExecuting::getClassName() const {
  return "SmfProcStateExecuting";
}

//------------------------------------------------------------------------------
// execute()
//------------------------------------------------------------------------------
SmfProcResultT SmfProcStateExecuting::execute(SmfUpgradeProcedure *i_proc) {
  TRACE("SmfProcStateExecuting::execute, Do some checking");

  /* Execute init actions */
  return this->executeStep(i_proc);
}

//------------------------------------------------------------------------------
// executeStep()
//------------------------------------------------------------------------------
SmfProcResultT SmfProcStateExecuting::executeStep(SmfUpgradeProcedure *i_proc) {
  TRACE_ENTER();
  TRACE("SmfProcStateExecuting::executeStep: Procedure=%s",
        i_proc->getProcName().c_str());

  /* Find and execute first step in state Initial. */
  const std::vector<SmfUpgradeStep *> &procSteps = i_proc->getProcSteps();

  for (const auto &elem : procSteps) {
    SmfStepResultT stepResult;

    /* Try executing the step */
    stepResult = (*elem).execute();

    /* Check step result */
    switch (stepResult) {
      case SMF_STEP_NULL: { /* The step didn't do something */
        continue;           /* with next step */
      }
      case SMF_STEP_SWITCHOVER: {
        LOG_NO("PROC: Step %s needs switchover, let other controller take over",
               (*elem).getRdn().c_str());

        i_proc->switchOver();

        TRACE_LEAVE();
        return SMF_PROC_DONE;
      }
      case SMF_STEP_COMPLETED: {
        TRACE("Step %s completed, sending PROCEDURE_EVT_EXECUTE_STEP",
              (*elem).getRdn().c_str());

        /* Send message to ourself to handle possible waiting suspend messages
         */
        PROCEDURE_EVT *evt = new PROCEDURE_EVT();
        evt->type = PROCEDURE_EVT_EXECUTE_STEP;
        i_proc->getProcThread()->send(evt);
        TRACE_LEAVE();
        return SMF_PROC_DONE;
      }
      case SMF_STEP_UNDONE: {
        LOG_NO("PROC: Step %s is undone", (*elem).getRdn().c_str());

        changeState(i_proc, SmfProcStateStepUndone::instance());
        TRACE_LEAVE();
        return SMF_PROC_STEPUNDONE;
      }
      default: {
        changeState(i_proc, SmfProcStateExecFailed::instance());

        LOG_NO("Step %s in procedure %s failed, step result %u",
               (*elem).getRdn().c_str(), i_proc->getProcName().c_str(),
               stepResult);

        TRACE_LEAVE();
        return SMF_PROC_FAILED;
      }
    }
  }

  // Execute the online remove commands.
  // If the OpenSAF proprietary step actions was selected (by the
  // nodeBundleActCmd attribute in the SmfConfig class) the offline remove
  // scripts was run within the steps. Check if the proprietary step actions was
  // choosen.

  if ((smfd_cb->nodeBundleActCmd == NULL) ||
      (strcmp(smfd_cb->nodeBundleActCmd, "") == 0)) {
    // Run all online remove scripts for all bundles (which does not require
    // restart) listed in the upgrade steps

    for (const auto &elem : procSteps) {
      TRACE(
          "SmfProcStateExecuting::executeStep: Execute OnlineRemove for the bundles to remove");
      /* Online uninstallation of old software */
      LOG_NO("PROC: Online uninstallation of old software");

      /* Run only remove scripts for those bundles which does NOT require reboot
       * to online uninstall */
      /* Find out which bundles to be removed here. Bundles which requires
       * reboot have already has   */
      /* their online remove scripts executed in the step. */

      /* Create the list of bundles NOT restarted */
      SmfImmUtils immutil;
      SaImmAttrValuesT_2 **attributes;
      std::list<SmfBundleRef> nonRestartBundles;
      const std::list<SmfBundleRef> &removeList = (*elem).getSwRemoveList();
      for (const auto &bundleElem : removeList) {
        /* Read the saSmfBundleRemoveOfflineScope to detect if the bundle shall
         * be included */
        if (immutil.getObject((bundleElem).getBundleDn(), &attributes) ==
            false) {
          LOG_NO("Could not find software bundle  %s",
                 (bundleElem).getBundleDn().c_str());
          changeState(i_proc, SmfProcStateExecFailed::instance());
          TRACE_LEAVE();
          return SMF_PROC_FAILED;
        }
        const SaUint32T *scope =
            immutil_getUint32Attr((const SaImmAttrValuesT_2 **)attributes,
                                  "saSmfBundleRemoveOfflineScope", 0);
        /* Include only bundles not need reboot */
        if ((scope == NULL) || (*scope != SA_SMF_CMD_SCOPE_PLM_EE)) {
          TRACE(
              "SmfProcStateExecuting::executeStep:Include the SW bundle %s to remove list",
              (bundleElem).getBundleDn().c_str());

          nonRestartBundles.push_back((bundleElem));
        }
      }

      /* Run the online remove scripts for the bundles NOT restarted */
      if ((*elem).onlineRemoveBundlesUserList((*elem).getSwNode(),
                                              nonRestartBundles) == false) {
        changeState(i_proc, SmfProcStateExecFailed::instance());
        LOG_NO(
            "SmfProcStateExecuting::executeStep:Failed to online remove bundles");
        TRACE_LEAVE();
        return SMF_PROC_FAILED;
      }

      /* Delete SaAmfNodeSwBundle objects for ALL old bundles in the step*/
      LOG_NO("PROC: Delete SaAmfNodeSwBundle objects");
      if ((*elem).deleteSaAmfNodeSwBundlesOld() == false) {
        changeState(i_proc, SmfProcStateExecFailed::instance());
        LOG_NO(
            "SmfProcStateExecuting::executeStep:Failed to delete old SaAmfNodeSwBundle objects");
        TRACE_LEAVE();
        return SMF_PROC_FAILED;
      }
    }
  }

  TRACE(
      "SmfProcStateExecuting::executeStep, All steps in procedure %s executed",
      i_proc->getProcName().c_str());

  LOG_NO("PROC: All steps has been executed");
  return this->executeWrapup(i_proc);
}

//------------------------------------------------------------------------------
// executeWrapup()
//------------------------------------------------------------------------------
SmfProcResultT SmfProcStateExecuting::executeWrapup(
    SmfUpgradeProcedure *i_proc) {
  TRACE_ENTER();
  TRACE("SmfProcStateExecuting::executeWrapup actions");

  LOG_NO("PROC: Start procedure wrapup actions");

  std::string wrapupRollbackDn;
  SaAisErrorT result;
  wrapupRollbackDn = "smfRollbackElement=ProcWrapup,";
  wrapupRollbackDn += i_proc->getDn();

  if ((result = smfCreateRollbackElement(
           wrapupRollbackDn, i_proc->getProcThread()->getImmHandle())) !=
      SA_AIS_OK) {
    LOG_NO(
        "SmfProcStateExecuting failed to create wrapup rollback element %s, rc=%s",
        wrapupRollbackDn.c_str(), saf_error(result));

    changeState(i_proc, SmfProcStateExecFailed::instance());
    TRACE_LEAVE();
    return SMF_PROC_FAILED;
  }

  const std::vector<SmfUpgradeAction *> &wrapupActions =
      i_proc->getWrapupActions();
  std::vector<SmfUpgradeAction *>::const_iterator iter;

  for (iter = wrapupActions.begin(); iter != wrapupActions.end(); ++iter) {
    if ((result = (*iter)->execute(i_proc->getProcThread()->getImmHandle(),
                                   &wrapupRollbackDn)) != SA_AIS_OK) {
      changeState(i_proc, SmfProcStateExecFailed::instance());
      LOG_NO("wrapup action %d failed, rc=%s", (*iter)->getId(),
             saf_error(result));
      TRACE_LEAVE();
      return SMF_PROC_FAILED;
    }
  }

  LOG_NO("PROC: Procedure wrapup actions completed");

  LOG_NO("PROC: Upgrade procedure completed %s", i_proc->getProcName().c_str());
  changeState(i_proc, SmfProcStateExecutionCompleted::instance());

  TRACE_LEAVE();
  return SMF_PROC_COMPLETED;
}

//------------------------------------------------------------------------------
// suspend()
//------------------------------------------------------------------------------
SmfProcResultT SmfProcStateExecuting::suspend(SmfUpgradeProcedure *i_proc) {
  TRACE_ENTER();
  TRACE("SmfProcStateExecuting::suspend implementation");

  changeState(i_proc, SmfProcStateExecSuspended::instance());
  LOG_NO("PROC: Suspended procedure %s", i_proc->getDn().c_str());

  TRACE_LEAVE();
  return SMF_PROC_SUSPENDED;
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
// SmfProcStateExecutionCompleted implementations
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
SmfProcState *SmfProcStateExecutionCompleted::s_instance = NULL;

SmfProcState *SmfProcStateExecutionCompleted::instance(void) {
  if (s_instance == NULL) {
    s_instance = new SmfProcStateExecutionCompleted;
  }

  return s_instance;
}

//------------------------------------------------------------------------------
// getClassName()
//------------------------------------------------------------------------------
std::string SmfProcStateExecutionCompleted::getClassName() const {
  return "SmfProcStateExecutionCompleted";
}

//------------------------------------------------------------------------------
// rollback()
//------------------------------------------------------------------------------
SmfProcResultT SmfProcStateExecutionCompleted::rollback(
    SmfUpgradeProcedure *i_proc) {
  TRACE_ENTER();

  TRACE("SmfProcStateExecutionCompleted::rollback implementation");
  changeState(i_proc, SmfProcStateRollingBack::instance());
  SmfProcResultT wrapupResult = rollbackWrapup(i_proc);

  TRACE_LEAVE();
  return wrapupResult;
}

//------------------------------------------------------------------------------
// rollbackWrapup()
//------------------------------------------------------------------------------
SmfProcResultT SmfProcStateExecutionCompleted::rollbackWrapup(
    SmfUpgradeProcedure *i_proc) {
  TRACE_ENTER();
  TRACE("SmfProcStateExecutionCompleted::rollbackWrapup actions");

  LOG_NO("PROC: Rollback procedure wrapup actions");

  SaAisErrorT result;
  std::string wrapupRollbackDn;
  wrapupRollbackDn = "smfRollbackElement=ProcWrapup,";
  wrapupRollbackDn += i_proc->getDn();

  /* Rollback the wrapup actions in reverse order */
  const std::vector<SmfUpgradeAction *> &wrapupActions =
      i_proc->getWrapupActions();
  std::vector<SmfUpgradeAction *>::const_reverse_iterator iter;

  for (iter = wrapupActions.rbegin(); iter != wrapupActions.rend(); ++iter) {
    if ((result = (*iter)->rollback(wrapupRollbackDn)) != SA_AIS_OK) {
      changeState(i_proc, SmfProcStateRollbackFailed::instance());
      LOG_NO("Rollback of wrapup action %d failed, rc=%s", (*iter)->getId(),
             saf_error(result));
      TRACE_LEAVE();
      return SMF_PROC_ROLLBACKFAILED;
    }
  }

  TRACE(
      "SmfProcStateExecutionCompleted::rollbackWrapup, Rollback of wrapup actions finished for procedure %s ",
      i_proc->getProcName().c_str());

  LOG_NO("PROC: Rollback procedure wrapup actions completed");
  LOG_NO("PROC: Start rolling back all the steps");
  return SMF_PROC_CONTINUE; /* Continue in rolling back state */
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
// SmfProcStateExecSuspended implementations
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
SmfProcState *SmfProcStateExecSuspended::s_instance = NULL;

SmfProcState *SmfProcStateExecSuspended::instance(void) {
  if (s_instance == NULL) {
    s_instance = new SmfProcStateExecSuspended;
  }

  return s_instance;
}

//------------------------------------------------------------------------------
// getClassName()
//------------------------------------------------------------------------------
std::string SmfProcStateExecSuspended::getClassName() const {
  return "SmfProcStateExecSuspended";
}

//------------------------------------------------------------------------------
// execute()
//------------------------------------------------------------------------------
SmfProcResultT SmfProcStateExecSuspended::execute(SmfUpgradeProcedure *i_proc) {
  TRACE_ENTER();
  TRACE("SmfProcStateExecSuspended::execute implementation");
  changeState(i_proc, SmfProcStateExecuting::instance());

  LOG_NO("PROC: Continue executing suspended procedure %s",
         i_proc->getDn().c_str());

  PROCEDURE_EVT *evt = new PROCEDURE_EVT();
  evt->type = PROCEDURE_EVT_EXECUTE_STEP;
  i_proc->getProcThread()->send(evt);

  TRACE_LEAVE();
  return SMF_PROC_DONE;
}

//------------------------------------------------------------------------------
// rollback()
//------------------------------------------------------------------------------
SmfProcResultT SmfProcStateExecSuspended::rollback(
    SmfUpgradeProcedure *i_proc) {
  TRACE_ENTER();

  TRACE("SmfProcStateExecSuspended::rollback implementation");
  changeState(i_proc, SmfProcStateRollingBack::instance());

  LOG_NO("PROC: Start rollback of suspended procedure %s",
         i_proc->getDn().c_str());

  PROCEDURE_EVT *evt = new PROCEDURE_EVT();
  evt->type = PROCEDURE_EVT_ROLLBACK_STEP;
  i_proc->getProcThread()->send(evt);

  TRACE_LEAVE();
  return SMF_PROC_DONE;
}

//------------------------------------------------------------------------------
// executeStep()
//------------------------------------------------------------------------------
SmfProcResultT SmfProcStateExecSuspended::executeStep(
    SmfUpgradeProcedure *i_proc) {
  /* We will get executeStep at suspend so just ignore it */
  return SMF_PROC_DONE;
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
// SmfProcStateExecFailed implementations
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
SmfProcState *SmfProcStateExecFailed::s_instance = NULL;

SmfProcState *SmfProcStateExecFailed::instance(void) {
  if (s_instance == NULL) {
    s_instance = new SmfProcStateExecFailed;
  }

  return s_instance;
}

//------------------------------------------------------------------------------
// getClassName()
//------------------------------------------------------------------------------
std::string SmfProcStateExecFailed::getClassName() const {
  return "SmfProcStateExecFailed";
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
// SmfProcStateStepUndone implementations
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
SmfProcState *SmfProcStateStepUndone::s_instance = NULL;

SmfProcState *SmfProcStateStepUndone::instance(void) {
  if (s_instance == NULL) {
    s_instance = new SmfProcStateStepUndone;
  }

  return s_instance;
}

//------------------------------------------------------------------------------
// getClassName()
//------------------------------------------------------------------------------
std::string SmfProcStateStepUndone::getClassName() const {
  return "SmfProcStateStepUndone";
}

//------------------------------------------------------------------------------
// execute()
//------------------------------------------------------------------------------
SmfProcResultT SmfProcStateStepUndone::execute(SmfUpgradeProcedure *i_proc) {
  TRACE_ENTER();

  changeState(i_proc, SmfProcStateExecuting::instance());

  LOG_NO("PROC: Retry executing undone procedure %s", i_proc->getDn().c_str());

  TRACE_LEAVE();
  return SMF_PROC_CONTINUE;
}

//------------------------------------------------------------------------------
// rollback()
//------------------------------------------------------------------------------
SmfProcResultT SmfProcStateStepUndone::rollback(SmfUpgradeProcedure *i_proc) {
  TRACE_ENTER();

  changeState(i_proc, SmfProcStateRollingBack::instance());

  LOG_NO("PROC: Start rollback of undone procedure %s",
         i_proc->getDn().c_str());

  TRACE_LEAVE();
  return SMF_PROC_CONTINUE;
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
// SmfProcStateRollingBack implementations
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
SmfProcState *SmfProcStateRollingBack::s_instance = NULL;

SmfProcState *SmfProcStateRollingBack::instance(void) {
  if (s_instance == NULL) {
    s_instance = new SmfProcStateRollingBack;
  }

  return s_instance;
}

//------------------------------------------------------------------------------
// getClassName()
//------------------------------------------------------------------------------
std::string SmfProcStateRollingBack::getClassName() const {
  return "SmfProcStateRollingBack";
}

//------------------------------------------------------------------------------
// rollback()
//------------------------------------------------------------------------------
SmfProcResultT SmfProcStateRollingBack::rollback(SmfUpgradeProcedure *i_proc) {
  return this->rollbackStep(i_proc);
}

//------------------------------------------------------------------------------
// rollbackStep()
//------------------------------------------------------------------------------
SmfProcResultT SmfProcStateRollingBack::rollbackStep(
    SmfUpgradeProcedure *i_proc) {
  TRACE_ENTER();
  TRACE("SmfProcStateRollingBack::rollbackStep: Procedure=%s",
        i_proc->getProcName().c_str());

  /* Find (in reverse order) and rollback steps. */
  const std::vector<SmfUpgradeStep *> &procSteps = i_proc->getProcSteps();
  std::vector<SmfUpgradeStep *>::const_reverse_iterator iter;

  /* Rollback steps in reverse order */
  for (iter = procSteps.rbegin(); iter != procSteps.rend(); ++iter) {
    SmfStepResultT stepResult;

    /* Try rollback the step */
    stepResult = (*iter)->rollback();

    /* Check step result */
    switch (stepResult) {
      case SMF_STEP_NULL: { /* The step didn't do something */
        continue;           /* with next step */
      }
      case SMF_STEP_SWITCHOVER: {
        LOG_NO("PROC: Step %s needs switchover, let other controller take over",
               (*iter)->getRdn().c_str());

        i_proc->switchOver();

        TRACE_LEAVE();
        return SMF_PROC_DONE;
      }
      case SMF_STEP_ROLLEDBACK: {
        TRACE("Step %s rolled back, sending PROCEDURE_EVT_ROLLBACK_STEP",
              (*iter)->getRdn().c_str());

        /* Send message to ourself to handle possible waiting suspend messages
         */
        PROCEDURE_EVT *evt = new PROCEDURE_EVT();
        evt->type = PROCEDURE_EVT_ROLLBACK_STEP;
        i_proc->getProcThread()->send(evt);

        TRACE_LEAVE();
        return SMF_PROC_DONE;
      }
      case SMF_STEP_ROLLBACKUNDONE: {
        changeState(i_proc, SmfProcStateRollbackFailed::instance());

        LOG_NO("PROC: Step %s returned rollback undone",
               (*iter)->getRdn().c_str());

        TRACE_LEAVE();
        return SMF_PROC_ROLLBACKFAILED;
      }
      default: {
        changeState(i_proc, SmfProcStateRollbackFailed::instance());

        LOG_NO("Rollback Step %s in procedure %s failed, step result %u",
               (*iter)->getRdn().c_str(), i_proc->getProcName().c_str(),
               stepResult);

        TRACE_LEAVE();
        return SMF_PROC_ROLLBACKFAILED;
      }
    }
  }

  // Execute the online remove commands.
  // If the OpenSAF proprietary step actions was selected (by the
  // nodeBundleActCmd attribute in the SmfConfig class) the offline remove
  // scripts was run within the steps. Check if the proprietary step actions was
  // choosen. This is done here instead of in the steps to handle several
  // components on the same node. We can't remove the software until all
  // components on a node has been upgraded.

  if ((smfd_cb->nodeBundleActCmd == NULL) ||
      (strcmp(smfd_cb->nodeBundleActCmd, "") == 0)) {
    // Run all online remove scripts for all bundles (which does not require
    // restart) listed in the upgrade steps

    for (iter = procSteps.rbegin(); iter != procSteps.rend(); ++iter) {
      TRACE(
          "SmfProcStateRollingBack::rollbackStep: Rollback OnlineRemove for the new bundles");
      /* Online uninstallation of new software */
      LOG_NO("PROC: Online uninstallation of new software");

      /* Run only remove scripts for those bundles which does NOT require reboot
       * to online uninstall */
      /* Find out which bundles to be removed here. Bundles which requires
       * reboot have already has   */
      /* their online remove scripts executed in the step. */

      /* Create the list of bundles NOT restarted */
      SmfImmUtils immutil;
      SaImmAttrValuesT_2 **attributes;
      std::list<SmfBundleRef> nonRestartBundles;
      const std::list<SmfBundleRef> &removeList = (*iter)->getSwAddList();
      for (const auto &bundleIter : removeList) {
        /* Read the saSmfBundleRemoveOfflineScope to detect if the bundle shall
         * be included */
        if (immutil.getObject((bundleIter).getBundleDn(), &attributes) ==
            false) {
          LOG_NO("Could not find software bundle  %s",
                 (bundleIter).getBundleDn().c_str());
          changeState(i_proc, SmfProcStateExecFailed::instance());
          TRACE_LEAVE();
          return SMF_PROC_FAILED;
        }
        const SaUint32T *scope =
            immutil_getUint32Attr((const SaImmAttrValuesT_2 **)attributes,
                                  "saSmfBundleRemoveOfflineScope", 0);
        /* Include only bundles not need reboot */
        if ((scope == NULL) || (*scope != SA_SMF_CMD_SCOPE_PLM_EE)) {
          TRACE(
              "SmfProcStateRollingBack::rollbackStep:Include the SW bundle %s to remove list",
              (bundleIter).getBundleDn().c_str());

          nonRestartBundles.push_back((bundleIter));
        }
      }

      /* Run the online remove scripts for the bundles NOT restarted */
      if ((*iter)->onlineRemoveBundlesUserList((*iter)->getSwNode(),
                                               nonRestartBundles) == false) {
        changeState(i_proc, SmfProcStateExecFailed::instance());
        LOG_NO(
            "SmfProcStateRollingBack::rollbackStep:Failed to online remove new bundles");
        TRACE_LEAVE();
        return SMF_PROC_FAILED;
      }

      /* Delete SaAmfNodeSwBundle objects for ALL new bundles in the step*/
      LOG_NO("PROC: Delete SaAmfNodeSwBundle objects");
      if ((*iter)->deleteSaAmfNodeSwBundlesNew() == false) {
        changeState(i_proc, SmfProcStateExecFailed::instance());
        LOG_NO(
            "SmfProcStateRollingBack::rollbackStep:Failed to delete new SaAmfNodeSwBundle objects");
        TRACE_LEAVE();
        return SMF_PROC_FAILED;
      }
    }
  }

  LOG_NO("PROC: All steps has been rolled back");
  return this->rollbackInit(i_proc);
}

//------------------------------------------------------------------------------
// rollbackInit()
//------------------------------------------------------------------------------
SmfProcResultT SmfProcStateRollingBack::rollbackInit(
    SmfUpgradeProcedure *i_proc) {
  TRACE_ENTER();
  LOG_NO("PROC: Rollback of procedure init actions");

  SaAisErrorT result;
  std::string initRollbackDn;
  initRollbackDn = "smfRollbackElement=ProcInit,";
  initRollbackDn += i_proc->getDn();

  /* Rollback the init actions in reverse order */
  const std::vector<SmfUpgradeAction *> &initActions = i_proc->getInitActions();
  std::vector<SmfUpgradeAction *>::const_reverse_iterator iter;

  for (iter = initActions.rbegin(); iter != initActions.rend(); ++iter) {
    if ((result = (*iter)->rollback(initRollbackDn)) != SA_AIS_OK) {
      changeState(i_proc, SmfProcStateRollbackFailed::instance());
      LOG_NO(
          "SmfProcStateExecuting::rollbackInit: rollback of init action %d failed, rc=%s",
          (*iter)->getId(), saf_error(result));
      TRACE_LEAVE();
      return SMF_PROC_ROLLBACKFAILED;
    }
  }

  TRACE(
      "SmfProcStateExecuting::rollbackInit, Procedure init actions rolled back");

  LOG_NO("PROC: Rollback of upgrade procedure completed %s",
         i_proc->getProcName().c_str());
  changeState(i_proc, SmfProcStateRolledBack::instance());

  TRACE_LEAVE();
  return SMF_PROC_ROLLEDBACK;
}

//------------------------------------------------------------------------------
// suspend()
//------------------------------------------------------------------------------
SmfProcResultT SmfProcStateRollingBack::suspend(SmfUpgradeProcedure *i_proc) {
  TRACE_ENTER();
  changeState(i_proc, SmfProcStateRollbackSuspended::instance());
  TRACE_LEAVE();
  return SMF_PROC_SUSPENDED;
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
// SmfProcStateRollbackSuspended implementations
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
SmfProcState *SmfProcStateRollbackSuspended::s_instance = NULL;

SmfProcState *SmfProcStateRollbackSuspended::instance(void) {
  if (s_instance == NULL) {
    s_instance = new SmfProcStateRollbackSuspended;
  }

  return s_instance;
}

//------------------------------------------------------------------------------
// getClassName()
//------------------------------------------------------------------------------
std::string SmfProcStateRollbackSuspended::getClassName() const {
  return "SmfProcStateRollbackSuspended";
}

//------------------------------------------------------------------------------
// rollbackStep()
//------------------------------------------------------------------------------
SmfProcResultT SmfProcStateRollbackSuspended::rollbackStep(
    SmfUpgradeProcedure *i_proc) {
  /* We will get rollbackStep at suspend so just ignore it */
  return SMF_PROC_DONE;
}

//------------------------------------------------------------------------------
// rollback()
//------------------------------------------------------------------------------
SmfProcResultT SmfProcStateRollbackSuspended::rollback(
    SmfUpgradeProcedure *i_proc) {
  TRACE_ENTER();
  changeState(i_proc, SmfProcStateRollingBack::instance());
  TRACE_LEAVE();
  return SMF_PROC_CONTINUE;
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
// SmfProcStateRolledBack implementations
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
SmfProcState *SmfProcStateRolledBack::s_instance = NULL;

SmfProcState *SmfProcStateRolledBack::instance(void) {
  if (s_instance == NULL) {
    s_instance = new SmfProcStateRolledBack;
  }

  return s_instance;
}

//------------------------------------------------------------------------------
// getClassName()
//------------------------------------------------------------------------------
std::string SmfProcStateRolledBack::getClassName() const {
  return "SmfProcStateRolledBack";
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
// SmfProcStateRollbackFailed implementations
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
SmfProcState *SmfProcStateRollbackFailed::s_instance = NULL;

SmfProcState *SmfProcStateRollbackFailed::instance(void) {
  if (s_instance == NULL) {
    s_instance = new SmfProcStateRollbackFailed;
  }

  return s_instance;
}

//------------------------------------------------------------------------------
// getClassName()
//------------------------------------------------------------------------------
std::string SmfProcStateRollbackFailed::getClassName() const {
  return "SmfProcStateRollbackFailed";
}
