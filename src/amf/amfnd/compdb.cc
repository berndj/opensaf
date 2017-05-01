/*      -*- OpenSAF  -*-
 *
 * (C) Copyright 2008 The OpenSAF Foundation
 * (C) Copyright 2017 Ericsson AB - All Rights Reserved.
 * Copyright (C) 2017, Oracle and/or its affiliates. All rights reserved.
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
 * Author(s): Emerson Network Power
 *
 */

/*****************************************************************************

..............................................................................

  DESCRIPTION:
   This module deals with the creation, accessing and deletion of the component
   database in the AVND.

..............................................................................

  FUNCTIONS:


******************************************************************************
*/

#include "imm/saf/saImmOm.h"
#include "amf/common/amf_util.h"
#include "osaf/immutil/immutil.h"
#include "base/logtrace.h"

#include "amf/amfnd/avnd.h"
#include "base/osaf_utility.h"

#include <string>

extern struct ImmutilWrapperProfile immutilWrapperProfile;
static pthread_mutex_t compdb_mutex = PTHREAD_MUTEX_INITIALIZER;
//
// TODO(HANO) Temporary use this function instead of strdup which uses malloc.
// Later on remove this function and use std::string instead
#include <cstring>
static char *StrDup(const char *s) {
  char *c = new char[strlen(s) + 1];
  std::strcpy(c, s);
  return c;
}

static int get_string_attr_from_imm(SaImmOiHandleT immOmHandle,
                                    SaImmAttrNameT attrName,
                                    const std::string &dn, SaStringT *str);
/* AMF Class SaAmfCompGlobalAttributes */
typedef struct {
  SaUint32T saAmfNumMaxInstantiateWithoutDelay;
  SaUint32T saAmfNumMaxInstantiateWithDelay;
  SaUint32T saAmfNumMaxAmStartAttempts;
  SaUint32T saAmfNumMaxAmStopAttempts;
  SaTimeT saAmfDelayBetweenInstantiateAttempts;
} amf_comp_global_attr_t;

static amf_comp_global_attr_t comp_global_attrs;

/* AMF Class SaAmfCompType */
typedef struct amf_comp_type {
  NCS_PATRICIA_NODE tree_node; /* name is key */
  std::string name;
  SaAmfCompCategoryT saAmfCtCompCategory;
  std::string saAmfCtSwBundle;
  SaStringT *saAmfCtDefCmdEnv;
  SaTimeT saAmfCtDefClcCliTimeout;
  SaTimeT saAmfCtDefCallbackTimeout;
  SaStringT saAmfCtRelPathInstantiateCmd;
  SaStringT *saAmfCtDefInstantiateCmdArgv;
  SaUint32T saAmfCtDefInstantiationLevel;
  SaStringT saAmfCtRelPathTerminateCmd;
  SaStringT *saAmfCtDefTerminateCmdArgv;
  SaStringT saAmfCtRelPathCleanupCmd;
  SaStringT *saAmfCtDefCleanupCmdArgv;
  SaStringT saAmfCtRelPathAmStartCmd;
  SaStringT *saAmfCtDefAmStartCmdArgv;
  SaStringT saAmfCtRelPathAmStopCmd;
  SaStringT *saAmfCtDefAmStopCmdArgv;
  SaStringT osafAmfCtRelPathHcCmd;
  SaStringT *osafAmfCtDefHcCmdArgv;
  SaTimeT saAmfCompQuiescingCompleteTimeout;
  SaAmfRecommendedRecoveryT saAmfCtDefRecoveryOnError;
  bool saAmfCtDefDisableRestart;
} amf_comp_type_t;

/*****************************************************************************
 ****  Component Part of AVND AMF Configuration Database Layout           ****
 *****************************************************************************

                   AVND_COMP
                   ----------------
   AVND_CB        | Stuff...       |
   -----------    | Attrs          |
  | COMP-Tree |-->|                |
  | ....      |   |                |
  | ....      |   |                |
   -----------    |                |
                  |                |
                  |                |
                  | Proxy ---------|-----> AVND_COMP (Proxy)
   AVND_SU        |                |
   -----------    |                |
  | Child     |-->|-SU-Comp-Next---|-----> AVND_COMP (Next)
  | Comp-List |   |                |
  |           |<--|-Parent SU      |       AVND_COMP_CSI_REC
   -----------    |                |       -------------
                  | CSI-Assign-List|----->|             |
                  |                |      |             |
                   ----------------        -------------

****************************************************************************/

static SaAisErrorT avnd_compglobalattrs_config_get(SaImmHandleT immOmHandle) {
  const SaImmAttrValuesT_2 **attributes;
  SaImmAccessorHandleT accessorHandle;
  std::string dn = "safRdn=compGlobalAttributes,safApp=safAmfService";

  TRACE_ENTER();

  amf_saImmOmAccessorInitialize(immOmHandle, accessorHandle);
  SaAisErrorT rc =
      amf_saImmOmAccessorGet_o2(immOmHandle, accessorHandle, dn, nullptr,
                                (SaImmAttrValuesT_2 ***)&attributes);
  if (rc != SA_AIS_OK) {
    LOG_ER("saImmOmAccessorGet_2 FAILED %u", rc);
    goto done;
  }

  TRACE_1("'%s'", dn.c_str());

  if (immutil_getAttr(
          const_cast<SaImmAttrNameT>("saAmfNumMaxInstantiateWithoutDelay"),
          attributes, 0,
          &comp_global_attrs.saAmfNumMaxInstantiateWithoutDelay) != SA_AIS_OK) {
    comp_global_attrs.saAmfNumMaxInstantiateWithoutDelay = 2;
  }

  if (immutil_getAttr(
          const_cast<SaImmAttrNameT>("saAmfNumMaxInstantiateWithDelay"),
          attributes, 0,
          &comp_global_attrs.saAmfNumMaxInstantiateWithDelay) != SA_AIS_OK) {
    comp_global_attrs.saAmfNumMaxInstantiateWithDelay = 0;
  }

  if (immutil_getAttr(
          const_cast<SaImmAttrNameT>("saAmfNumMaxAmStartAttempts"), attributes,
          0, &comp_global_attrs.saAmfNumMaxAmStartAttempts) != SA_AIS_OK) {
    comp_global_attrs.saAmfNumMaxAmStartAttempts = 2;
  }

  if (immutil_getAttr(
          const_cast<SaImmAttrNameT>("saAmfNumMaxAmStopAttempts"), attributes,
          0, &comp_global_attrs.saAmfNumMaxAmStopAttempts) != SA_AIS_OK) {
    comp_global_attrs.saAmfNumMaxAmStopAttempts = 2;
  }

  if (immutil_getAttr(
          const_cast<SaImmAttrNameT>("saAmfDelayBetweenInstantiateAttempts"),
          attributes, 0,
          &comp_global_attrs.saAmfDelayBetweenInstantiateAttempts) !=
      SA_AIS_OK) {
    comp_global_attrs.saAmfDelayBetweenInstantiateAttempts = 0;
  }

  immutil_saImmOmAccessorFinalize(accessorHandle);

  rc = SA_AIS_OK;

done:
  TRACE_LEAVE();
  return rc;
}

/****************************************************************************
  Name          : avnd_compdb_init

  Description   : This routine initializes the component database.

  Arguments     : cb  - ptr to the AvND control block

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : None.
******************************************************************************/
uint32_t avnd_compdb_init(AVND_CB *cb) {
  uint32_t rc = NCSCC_RC_SUCCESS;
  SaImmHandleT immOmHandle;
  SaVersionT immVersion = {'A', 2, 1};
  SaAisErrorT error;

  TRACE_ENTER();

  error = saImmOmInitialize_cond(&immOmHandle, nullptr, &immVersion);
  if (error != SA_AIS_OK) {
    LOG_CR("saImmOmInitialize failed: %u", error);
    rc = NCSCC_RC_FAILURE;
    goto done1;
  }

  if (avnd_compglobalattrs_config_get(immOmHandle) != SA_AIS_OK) {
    rc = NCSCC_RC_FAILURE;
    goto done;
  }

done:
  immutil_saImmOmFinalize(immOmHandle);
done1:
  TRACE_LEAVE();
  return rc;
}

/****************************************************************************
  Name          : avnd_compdb_rec_del

  Description   : This routine deletes a component record from the component
                  database.

  Arguments     : cb       - ptr to the AvND control block
                  name - ptr to the comp-name (in n/w order)

  Return Values : NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE

  Notes         : This routine expects a NULL comp-csi list.
******************************************************************************/
uint32_t avnd_compdb_rec_del(AVND_CB *cb, const std::string &name) {
  AVND_COMP *comp;
  AVND_SU *su = 0;
  std::string su_name;
  uint32_t rc = NCSCC_RC_SUCCESS;

  TRACE_ENTER2("%s", name.c_str());

  /* get the comp */
  comp = avnd_compdb_rec_get(cb->compdb, name);
  if (!comp) {
    LOG_ER("%s: %s not found", __FUNCTION__, name.c_str());
    rc = AVND_ERR_NO_COMP;
    goto done;
  }

  /* comp should not be attached to any csi when it is being deleted */
  osafassert(comp->csi_list.n_nodes == 0);

  /*
   * Determine if the SU is present
   */
  /* extract the su-name from comp dn */
  avnd_cpy_SU_DN_from_DN(su_name, name);

  /* get the su record */
  su = avnd_sudb_rec_get(cb->sudb, su_name);
  if (!su) {
    LOG_ER("%s: %s not found", __FUNCTION__, su_name.c_str());
    rc = AVND_ERR_NO_SU;
    goto done;
  }

  /*
   * Remove from the comp-list (maintained by su).
   */
  rc = m_AVND_SUDB_REC_COMP_REM(*su, *comp);
  if (NCSCC_RC_SUCCESS != rc) {
    LOG_ER("%s: %s remove failed", __FUNCTION__, name.c_str());
    rc = AVND_ERR_DLL;
    goto done;
  }

  /*
   * Remove from compdb
   */
  osaf_mutex_lock_ordie(&compdb_mutex);
  cb->compdb.erase(name);
  osaf_mutex_unlock_ordie(&compdb_mutex);

  /*
   * Delete the various lists (hc, pm, pg, cbk etc) maintained by this comp.
   */
  avnd_comp_hc_rec_del_all(cb, comp);
  avnd_comp_cbq_del(cb, comp, false);
  avnd_comp_pm_rec_del_all(cb, comp);

  /* remove the association with hdl mngr */
  ncshm_destroy_hdl(NCS_SERVICE_ID_AVND, comp->comp_hdl);

  /* comp should not be attached to any hc when it is being deleted */
  osafassert(comp->hc_list.n_nodes == 0);

  LOG_IN("Deleted '%s'", name.c_str());
  /* free the memory */
  avnd_comp_delete(comp);

  TRACE_LEAVE();
  return rc;

done:
  if (rc == NCSCC_RC_SUCCESS)
    LOG_IN("Deleted '%s'", name.c_str());
  else
    LOG_ER("Delete of '%s' failed", name.c_str());

  TRACE_LEAVE();
  return rc;
}

/****************************************************************************
  Name          : avnd_compdb_csi_rec_get

  Description   : This routine gets the comp-csi relationship record from
                  the csi-list (maintained on comp).

  Arguments     : cb            - ptr to AvND control block
                  comp_name - ptr to the comp-name (n/w order)
                  csi_name      - ptr to the CSI name

  Return Values : ptr to the comp-csi record (if any)

  Notes         : None
******************************************************************************/
AVND_COMP_CSI_REC *avnd_compdb_csi_rec_get(AVND_CB *cb,
                                           const std::string &comp_name,
                                           const std::string &csi_name) {
  AVND_COMP_CSI_REC *csi_rec = 0;
  AVND_COMP *comp = 0;

  /* get the comp & csi records */
  comp = avnd_compdb_rec_get(cb->compdb, comp_name);
  if (comp) csi_rec = m_AVND_COMPDB_REC_CSI_GET(*comp, csi_name.c_str());

  return csi_rec;
}

uint32_t avnd_comp_oper_req(AVND_CB *cb, AVSV_PARAM_INFO *param) {
  uint32_t rc = NCSCC_RC_FAILURE;
  const std::string param_name = Amf::to_string(&param->name);

  TRACE_ENTER2("Op %u, %s", param->act, param_name.c_str());

  switch (param->act) {
    case AVSV_OBJ_OPR_MOD: {
      AVND_COMP *comp = 0;
      AVND_SU *su = 0;
      std::string su_name = "";

      comp = avnd_compdb_rec_get(cb->compdb, param_name);
      if (!comp) {
        LOG_ER("failed to get %s", param_name.c_str());
        goto done;
      }
      /* extract the su-name from comp dn */
      avnd_cpy_SU_DN_from_DN(su_name, param_name);

      /* get the su record */
      su = avnd_sudb_rec_get(cb->sudb, su_name);
      if (!su) {
        LOG_ER("no su in database for the comp %s", param_name.c_str());
        goto done;
      }

      switch (param->attr_id) {
        case saAmfCompInstantiateCmd_ID:
        case saAmfCompTerminateCmd_ID:
        case saAmfCompCleanupCmd_ID:
        case saAmfCompAmStartCmd_ID:
        case saAmfCompAmStopCmd_ID:
        case saAmfCompCmdEnv_ID:
          comp->config_is_valid = 0;
          break;
        case saAmfCompInstantiateTimeout_ID:
          osafassert(sizeof(SaTimeT) == param->value_len);
          comp->clc_info.cmds[AVND_COMP_CLC_CMD_TYPE_INSTANTIATE - 1].timeout =
              m_NCS_OS_NTOHLL_P(param->value);
          break;

        case saAmfCompDelayBetweenInstantiateAttempts_ID:
          break;

        case saAmfCompTerminateTimeout_ID:
          osafassert(sizeof(SaTimeT) == param->value_len);
          comp->clc_info.cmds[AVND_COMP_CLC_CMD_TYPE_TERMINATE - 1].timeout =
              m_NCS_OS_NTOHLL_P(param->value);
          break;

        case saAmfCompCleanupTimeout_ID:
          osafassert(sizeof(SaTimeT) == param->value_len);
          comp->clc_info.cmds[AVND_COMP_CLC_CMD_TYPE_CLEANUP - 1].timeout =
              m_NCS_OS_NTOHLL_P(param->value);
          break;

        case saAmfCompAmStartTimeout_ID:
          osafassert(sizeof(SaTimeT) == param->value_len);
          comp->clc_info.cmds[AVND_COMP_CLC_CMD_TYPE_AMSTART - 1].timeout =
              m_NCS_OS_NTOHLL_P(param->value);
          break;

        case saAmfCompAmStopTimeout_ID:
          osafassert(sizeof(SaTimeT) == param->value_len);
          comp->clc_info.cmds[AVND_COMP_CLC_CMD_TYPE_AMSTOP - 1].timeout =
              m_NCS_OS_NTOHLL_P(param->value);
          break;

        case saAmfCompTerminateCallbackTimeOut_ID:
          osafassert(sizeof(SaTimeT) == param->value_len);
          comp->term_cbk_timeout = m_NCS_OS_NTOHLL_P(param->value);
          break;

        case saAmfCompCSISetCallbackTimeout_ID:
          osafassert(sizeof(SaTimeT) == param->value_len);
          comp->csi_set_cbk_timeout = m_NCS_OS_NTOHLL_P(param->value);
          break;

        case saAmfCompQuiescingCompleteTimeout_ID:
          osafassert(sizeof(SaTimeT) == param->value_len);
          comp->quies_complete_cbk_timeout = m_NCS_OS_NTOHLL_P(param->value);
          break;

        case saAmfCompCSIRmvCallbackTimeout_ID:
          osafassert(sizeof(SaTimeT) == param->value_len);
          comp->csi_rmv_cbk_timeout = m_NCS_OS_NTOHLL_P(param->value);
          break;

        case saAmfCompProxiedCompInstantiateCallbackTimeout_ID:
          osafassert(sizeof(SaTimeT) == param->value_len);
          comp->pxied_inst_cbk_timeout = m_NCS_OS_NTOHLL_P(param->value);
          break;

        case saAmfCompProxiedCompCleanupCallbackTimeout_ID:
          osafassert(sizeof(SaTimeT) == param->value_len);
          comp->pxied_clean_cbk_timeout = m_NCS_OS_NTOHLL_P(param->value);
          break;

        case saAmfCompNodeRebootCleanupFail_ID:
          break;

        case saAmfCompInstantiationLevel_ID:
          osafassert(sizeof(uint32_t) == param->value_len);
          comp->inst_level = *(uint32_t *)(param->value);

          /* Remove from the comp-list (maintained by su) */
          rc = m_AVND_SUDB_REC_COMP_REM(*su, *comp);
          if (NCSCC_RC_SUCCESS != rc) {
            LOG_ER("%s: %s remove failed", __FUNCTION__, comp->name.c_str());
            goto done;
          }

          (&comp->su_dll_node)->prev = nullptr;
          (&comp->su_dll_node)->next = nullptr;

          /* Add to the comp-list (maintained by su) */
          m_AVND_SUDB_REC_COMP_ADD(*su, *comp, rc);

          break;
        case saAmfCompRecoveryOnError_ID:
          osafassert(sizeof(uint32_t) == param->value_len);
          comp->err_info.def_rec = static_cast<SaAmfRecommendedRecoveryT>(
              m_NCS_OS_NTOHL(*(uint32_t *)(param->value)));
          break;

        case saAmfCompNumMaxInstantiate_ID:
          osafassert(sizeof(uint32_t) == param->value_len);
          comp->clc_info.inst_retry_max =
              m_NCS_OS_NTOHL(*(uint32_t *)(param->value));
          break;

        case saAmfCompAMEnable_ID:
          osafassert(1 == param->value_len);
          comp->is_am_en = (bool)param->value[0];
          comp->clc_info.am_start_retry_cnt = 0;
          rc = avnd_comp_am_oper_req_process(cb, comp);
          break;

        case saAmfCompNumMaxInstantiateWithDelay_ID:
          break;
        case saAmfCompNumMaxAmStartAttempts_ID:
          break;
        case saAmfCompNumMaxAmStopAttempts_ID:
          break;
        case saAmfCompDisableRestart_ID: {
          uint32_t disable_restart;
          osafassert(sizeof(uint32_t) == param->value_len);
          disable_restart = ntohl(*(uint32_t *)(param->value));
          osafassert(disable_restart <= 1);
          comp->is_restart_en = (disable_restart == 1) ? false : true;
          LOG_NO("saAmfCompDisableRestart changed to %u for '%s'",
                 disable_restart, comp->name.c_str());
          break;
        }
        case saAmfCompType_ID: {
          comp->saAmfCompType = Amf::to_string(&param->name_sec);
          /*
          ** Indicate that comp config is no longer valid and have to be
          ** refreshed from IMM. We cannot refresh here since it is probably
          ** not yet in IMM.
          */
          comp->config_is_valid = 0;
          LOG_NO("saAmfCompType changed to '%s' for '%s'",
                 comp->saAmfCompType.c_str(), comp->name.c_str());
          break;
        }
        default:
          LOG_NO("%s: Unsupported attribute %u", __FUNCTION__, param->attr_id);
          goto done;
      }
    } break;

    case AVSV_OBJ_OPR_DEL: {
      /* This request comes when any component is being
         deleted. When this request comes from Amfd,
         component can be in instantiated or uninstantiated
         state and in that case, Amfnd will uninstantiate the
         component and delete the record from its data base.
         This is kind of automatic termination of a component
         when comp is getting deleted. */
      AVND_COMP *comp = avnd_compdb_rec_get(cb->compdb, param_name);
      if (comp == nullptr) {
        LOG_ER("%s: Comp '%s' not found", __FUNCTION__, param_name.c_str());
        goto done;
      }
      if (comp->su->is_ncs == true) {
        /* Terminate the pi comp. It will terminate the
           component and delete the comp record. */
        if (m_AVND_COMP_TYPE_IS_PREINSTANTIABLE(comp) &&
            (comp->pres == SA_AMF_PRESENCE_INSTANTIATED)) {
          comp->pending_delete = true;
          rc = avnd_comp_clc_fsm_run(cb, comp, AVND_COMP_CLC_PRES_FSM_EV_TERM);
          goto done;
        }

        /* Terminate the Npi comp. After deleting the csi, comp
           will be already terminated, so we just want to delete
           the comp record.*/
        if (!m_AVND_COMP_TYPE_IS_PREINSTANTIABLE(comp)) {
          rc = avnd_compdb_rec_del(cb, param_name);
          goto done;
        }
      }
      /* Delete the component in case, it is in term failed or so. */
      rc = avnd_compdb_rec_del(cb, param_name);

    } break;

    default:
      LOG_NO("%s: Unsupported action %u", __FUNCTION__, param->act);
      goto done;
  }

  rc = NCSCC_RC_SUCCESS;

done:
  TRACE_LEAVE();
  return rc;
}

uint32_t avnd_comptype_oper_req(AVND_CB *cb, AVSV_PARAM_INFO *param) {
  uint32_t rc = NCSCC_RC_FAILURE;

  AVND_COMP *comp;
  std::string comp_type_name;

  TRACE_ENTER2("Op %u, %s", param->act,
               osaf_extended_name_borrow(&param->name));

  switch (param->act) {
    case AVSV_OBJ_OPR_MOD: {
      // 1. find component from componentType,
      // input example, param->name.value = safVersion=1,safCompType=AmfDemo1
      comp_type_name = osaf_extended_name_borrow(&param->name);
      TRACE("comp_type_name: %s", comp_type_name.c_str());
      osafassert(comp_type_name.empty() == false);
      // 2. search each component for a matching compType

      for (comp = avnd_compdb_rec_get_next(cb->compdb, ""); comp != nullptr;
           comp = avnd_compdb_rec_get_next(cb->compdb, comp->name)) {
        if (comp->saAmfCompType.compare(comp_type_name) == 0) {
          // 3. comptype found, check if component uses this comptype attribute
          // value or if component has specialized this attribute value.

          AVND_SU *su = avnd_sudb_rec_get(cb->sudb, comp->su->name);
          if (!su) {
            LOG_ER("no su in database for the comp %s", comp->name.c_str());
            goto done;
          }

          TRACE("su name: %s , comp name: %s , comp_type: %s",
                comp->su->name.c_str(), comp->name.c_str(),
                comp->saAmfCompType.c_str());

          switch (param->attr_id) {
            case saAmfCtDefCallbackTimeout_ID: {
              SaTimeT saAmfCtDefCallbackTimeout = *((SaTimeT *)param->value);
              osafassert(sizeof(SaTimeT) == param->value_len);
              if (comp->use_comptype_attr->test(PxiedInstCallbackTimeout)) {
                comp->pxied_inst_cbk_timeout = saAmfCtDefCallbackTimeout;
                TRACE("comp->pxied_inst_cbk_timeout modified to '%llu'",
                      comp->pxied_inst_cbk_timeout);
              }
              if (comp->use_comptype_attr->test(TerminateCallbackTimeout)) {
                comp->term_cbk_timeout = saAmfCtDefCallbackTimeout;
                TRACE("comp->term_cbk_timeout modified to '%llu'",
                      comp->term_cbk_timeout);
              }
              if (comp->use_comptype_attr->test(PxiedCleanupCallbackTimeout)) {
                comp->pxied_clean_cbk_timeout = saAmfCtDefCallbackTimeout;
                TRACE("comp->pxied_clean_cbk_timeout modified to '%llu'",
                      comp->pxied_clean_cbk_timeout);
              }
              if (comp->use_comptype_attr->test(CsiSetCallbackTimeout)) {
                comp->csi_set_cbk_timeout = saAmfCtDefCallbackTimeout;
                TRACE("comp->csi_set_cbk_timeout modified to '%llu'",
                      comp->csi_set_cbk_timeout);
              }
              if (comp->use_comptype_attr->test(CsiRemoveCallbackTimeout)) {
                comp->csi_rmv_cbk_timeout = saAmfCtDefCallbackTimeout;
                TRACE("comp->csi_rmv_cbk_timeout modified to '%llu'",
                      comp->csi_rmv_cbk_timeout);
              }
              break;
            }
            case saAmfCtDefClcCliTimeout_ID: {
              AVND_COMP_CLC_CMD_PARAM *cmd;
              SaTimeT saAmfCtDefClcCliTimeout = *((SaTimeT *)param->value);
              osafassert(sizeof(SaTimeT) == param->value_len);
              if (comp->use_comptype_attr->test(CompInstantiateTimeout)) {
                cmd = &comp->clc_info
                           .cmds[AVND_COMP_CLC_CMD_TYPE_INSTANTIATE - 1];
                cmd->timeout = saAmfCtDefClcCliTimeout;
                TRACE("cmd->timeout (Instantiate) modified to '%llu'",
                      cmd->timeout);
              }
              if (comp->use_comptype_attr->test(CompTerminateTimeout)) {
                cmd =
                    &comp->clc_info.cmds[AVND_COMP_CLC_CMD_TYPE_TERMINATE - 1];
                cmd->timeout = saAmfCtDefClcCliTimeout;
                TRACE("cmd->timeout (Terminate) modified to '%llu'",
                      cmd->timeout);
              }
              if (comp->use_comptype_attr->test(CompCleanupTimeout)) {
                cmd = &comp->clc_info.cmds[AVND_COMP_CLC_CMD_TYPE_CLEANUP - 1];
                cmd->timeout = saAmfCtDefClcCliTimeout;
                TRACE("cmd->timeout (Cleanup) modified to '%llu'",
                      cmd->timeout);
              }
              if (comp->use_comptype_attr->test(CompAmStartTimeout)) {
                cmd = &comp->clc_info.cmds[AVND_COMP_CLC_CMD_TYPE_AMSTART - 1];
                cmd->timeout = saAmfCtDefClcCliTimeout;
                TRACE("cmd->timeout (AM Start) modified to '%llu'",
                      cmd->timeout);
              }
              if (comp->use_comptype_attr->test(CompAmStopTimeout)) {
                cmd = &comp->clc_info.cmds[AVND_COMP_CLC_CMD_TYPE_AMSTOP - 1];
                cmd->timeout = saAmfCtDefClcCliTimeout;
                TRACE("cmd->timeout (AM Stop) modified to '%llu'",
                      cmd->timeout);
              }
              break;
            }
            case saAmfCtDefQuiescingCompleteTimeout_ID: {
              SaTimeT saAmfCtDefQuiescingCompleteTimeout =
                  *((SaTimeT *)param->value);
              osafassert(sizeof(SaTimeT) == param->value_len);
              if (comp->use_comptype_attr->test(DefQuiescingCompleteTimeout)) {
                comp->quies_complete_cbk_timeout =
                    saAmfCtDefQuiescingCompleteTimeout;
                TRACE("comp->quies_complete_cbk_timeout modified to '%llu'",
                      comp->quies_complete_cbk_timeout);
              }
              break;
            }
            case saAmfCtDefInstantiationLevel_ID: {
              osafassert(sizeof(SaUint32T) == param->value_len);

              if (comp->use_comptype_attr->test(CompInstantiationLevel)) {
                comp->inst_level = *(SaUint32T *)(param->value);
                /* Remove from the comp-list (maintained by su) */
                rc = m_AVND_SUDB_REC_COMP_REM(*su, *comp);
                if (NCSCC_RC_SUCCESS != rc) {
                  LOG_ER("%s: %s remove failed", __FUNCTION__,
                         comp->name.c_str());
                  goto done;
                }

                (&comp->su_dll_node)->prev = nullptr;
                (&comp->su_dll_node)->next = nullptr;

                /* Add to the comp-list (maintained by su) */
                m_AVND_SUDB_REC_COMP_ADD(*su, *comp, rc);
                TRACE("comp->inst_level modified to '%u'", comp->inst_level);
              }
              break;
            }
            case saAmfCtDefRecoveryOnError_ID: {
              SaAmfRecommendedRecoveryT saAmfCtDefRecoveryOnError =
                  *((SaAmfRecommendedRecoveryT *)param->value);
              osafassert(sizeof(SaAmfRecommendedRecoveryT) == param->value_len);
              if (comp->use_comptype_attr->test(DefRecoveryOnError)) {
                comp->err_info.def_rec = saAmfCtDefRecoveryOnError;
                TRACE("comp->err_info.def_rec modified to '%u'",
                      comp->err_info.def_rec);
              }
              break;
            }
            case saAmfCtDefDisableRestart_ID: {
              SaBoolT saAmfCtDefDisableRestart = *((SaBoolT *)param->value);
              osafassert(sizeof(SaBoolT) == param->value_len);
              if (comp->use_comptype_attr->test(DefDisableRestart)) {
                comp->is_restart_en =
                    (saAmfCtDefDisableRestart == true) ? false : true;
                TRACE("comp->is_restart_en modified to '%u'",
                      comp->is_restart_en);
              }
              break;
            }
            default:
              LOG_WA("Unexpected attribute id: %d", param->attr_id);
          }
        }
      }
    }
    case AVSV_OBJ_OPR_DEL: {
      // Do nothing
      break;
    }
    default:
      LOG_NO("%s: Unsupported action %u", __FUNCTION__, param->act);
      goto done;
  }

done:
  rc = NCSCC_RC_SUCCESS;

  TRACE_LEAVE();

  return rc;
}

static void avnd_comptype_delete(amf_comp_type_t *compt) {
  int arg_counter;
  char *argv;
  TRACE_ENTER();

  if (!compt) {
    TRACE_LEAVE();
    return;
  }

  TRACE("'%s'", compt->name.c_str());
  /* Free saAmfCtDefCmdEnv[i] before freeing saAmfCtDefCmdEnv */
  if (compt->saAmfCtDefCmdEnv != nullptr) {
    arg_counter = 0;
    while ((argv = compt->saAmfCtDefCmdEnv[arg_counter++]) != nullptr)
      delete[] argv;
    delete[] compt->saAmfCtDefCmdEnv;
  }

  delete[] compt->saAmfCtRelPathInstantiateCmd;

  if (compt->saAmfCtDefInstantiateCmdArgv != nullptr) {
    /* Free saAmfCtDefInstantiateCmdArgv[i] before freeing
     * saAmfCtDefInstantiateCmdArgv */
    arg_counter = 0;
    while ((argv = compt->saAmfCtDefInstantiateCmdArgv[arg_counter++]) !=
            nullptr)
      delete[] argv;
    delete[] compt->saAmfCtDefInstantiateCmdArgv;
  }

  delete[] compt->saAmfCtRelPathTerminateCmd;

  if (compt->saAmfCtDefTerminateCmdArgv != nullptr) {
    /* Free saAmfCtDefTerminateCmdArgv[i] before freeing
     * saAmfCtDefTerminateCmdArgv */
    arg_counter = 0;
    while ((argv = compt->saAmfCtDefTerminateCmdArgv[arg_counter++]) != nullptr)
      delete[] argv;
    delete[] compt->saAmfCtDefTerminateCmdArgv;
  }

  delete[] compt->saAmfCtRelPathCleanupCmd;

  if (compt->saAmfCtDefCleanupCmdArgv != nullptr) {
    /* Free saAmfCtDefCleanupCmdArgv[i] before freeing
     * saAmfCtDefCleanupCmdArgv */
    arg_counter = 0;
    while ((argv = compt->saAmfCtDefCleanupCmdArgv[arg_counter++]) != nullptr)
      delete[] argv;
    delete[] compt->saAmfCtDefCleanupCmdArgv;
  }

  delete[] compt->saAmfCtRelPathAmStartCmd;

  if (compt->saAmfCtDefAmStartCmdArgv != nullptr) {
    /* Free saAmfCtDefAmStartCmdArgv[i] before freeing
     *  saAmfCtDefAmStartCmdArgv */
    arg_counter = 0;
    while ((argv = compt->saAmfCtDefAmStartCmdArgv[arg_counter++]) != nullptr)
      delete[] argv;
    delete[] compt->saAmfCtDefAmStartCmdArgv;
  }

  delete[] compt->saAmfCtRelPathAmStopCmd;

  if (compt->saAmfCtDefAmStopCmdArgv != nullptr) {
    /* Free saAmfCtDefAmStopCmdArgv[i] before freeing saAmfCtDefAmStopCmdArgv */
    arg_counter = 0;
    while ((argv = compt->saAmfCtDefAmStopCmdArgv[arg_counter++]) != nullptr)
      delete[] argv;
    delete[] compt->saAmfCtDefAmStopCmdArgv;
  }

  delete[] compt->osafAmfCtRelPathHcCmd;

  if (compt->osafAmfCtDefHcCmdArgv != nullptr) {
    arg_counter = 0;
    while ((argv = compt->osafAmfCtDefHcCmdArgv[arg_counter++]) != nullptr)
      delete[] argv;
    delete[] compt->osafAmfCtDefHcCmdArgv;
  }

  delete compt;

  TRACE_LEAVE();
}

static amf_comp_type_t *avnd_comptype_create(SaImmHandleT immOmHandle,
                                             const std::string &dn) {
  SaImmAccessorHandleT accessorHandle;
  amf_comp_type_t *compt = nullptr;
  unsigned int i;
  unsigned int j;
  const char *str;
  const SaImmAttrValuesT_2 **attributes;
  SaNameT saAmfCtSwBundle;

  TRACE_ENTER2("'%s'", dn.c_str());

  (void)amf_saImmOmAccessorInitialize(immOmHandle, accessorHandle);

  if (amf_saImmOmAccessorGet_o2(immOmHandle, accessorHandle, dn, nullptr,
                                (SaImmAttrValuesT_2 ***)&attributes) !=
      SA_AIS_OK) {
    LOG_ER("amf_saImmOmAccessorGet_o2 FAILED for '%s'", dn.c_str());
    goto done;
  }

  compt = new amf_comp_type_t();

  compt->name = dn;

  if (immutil_getAttr(const_cast<SaImmAttrNameT>("saAmfCtCompCategory"),
                      attributes, 0, &compt->saAmfCtCompCategory) != SA_AIS_OK)
    osafassert(0);

  if (IS_COMP_LOCAL(compt->saAmfCtCompCategory)) {
    // Ignore if bundle not found, commands can be absolute path
    immutil_getAttr(const_cast<SaImmAttrNameT>("saAmfCtSwBundle"), attributes,
                    0, &saAmfCtSwBundle);
    compt->saAmfCtSwBundle = Amf::to_string(&saAmfCtSwBundle);
  }

  immutil_getAttrValuesNumber(const_cast<SaImmAttrNameT>("saAmfCtDefCmdEnv"),
                              attributes, &j);
  compt->saAmfCtDefCmdEnv = new SaStringT[j + 1]();
  osafassert(compt->saAmfCtDefCmdEnv);
  for (i = 0; i < j; i++) {
    str = immutil_getStringAttr(attributes, "saAmfCtDefCmdEnv", i);
    osafassert(str);
    compt->saAmfCtDefCmdEnv[i] = StrDup(str);
    osafassert(compt->saAmfCtDefCmdEnv[i]);
  }

  (void)immutil_getAttr(const_cast<SaImmAttrNameT>("saAmfCtDefClcCliTimeout"),
                        attributes, 0, &compt->saAmfCtDefClcCliTimeout);

  (void)immutil_getAttr(const_cast<SaImmAttrNameT>("saAmfCtDefCallbackTimeout"),
                        attributes, 0, &compt->saAmfCtDefCallbackTimeout);

  if ((str = immutil_getStringAttr(attributes, "saAmfCtRelPathInstantiateCmd",
                                   0)) != nullptr)
    compt->saAmfCtRelPathInstantiateCmd = StrDup(str);

  immutil_getAttrValuesNumber(
      const_cast<SaImmAttrNameT>("saAmfCtDefInstantiateCmdArgv"), attributes,
      &j);
  compt->saAmfCtDefInstantiateCmdArgv = new SaStringT[j + 1]();
  osafassert(compt->saAmfCtDefInstantiateCmdArgv);
  for (i = 0; i < j; i++) {
    str = immutil_getStringAttr(attributes, "saAmfCtDefInstantiateCmdArgv", i);
    osafassert(str);
    compt->saAmfCtDefInstantiateCmdArgv[i] = StrDup(str);
    osafassert(compt->saAmfCtDefInstantiateCmdArgv[i]);
  }

  if (immutil_getAttr(
          const_cast<SaImmAttrNameT>("saAmfCtDefInstantiationLevel"),
          attributes, 0, &compt->saAmfCtDefInstantiationLevel) != SA_AIS_OK)
    compt->saAmfCtDefInstantiationLevel = 0;

  if ((str = immutil_getStringAttr(attributes, "saAmfCtRelPathTerminateCmd",
                                   0)) != nullptr)
    compt->saAmfCtRelPathTerminateCmd = StrDup(str);

  immutil_getAttrValuesNumber(
      const_cast<SaImmAttrNameT>("saAmfCtDefTerminateCmdArgv"), attributes, &j);
  compt->saAmfCtDefTerminateCmdArgv = new SaStringT[j + 1]();
  osafassert(compt->saAmfCtDefTerminateCmdArgv);
  for (i = 0; i < j; i++) {
    str = immutil_getStringAttr(attributes, "saAmfCtDefTerminateCmdArgv", i);
    osafassert(str);
    compt->saAmfCtDefTerminateCmdArgv[i] = StrDup(str);
    osafassert(compt->saAmfCtDefTerminateCmdArgv[i]);
  }

  if ((str = immutil_getStringAttr(attributes, "saAmfCtRelPathCleanupCmd",
                                   0)) != nullptr)
    compt->saAmfCtRelPathCleanupCmd = StrDup(str);

  immutil_getAttrValuesNumber(
      const_cast<SaImmAttrNameT>("saAmfCtDefCleanupCmdArgv"), attributes, &j);
  compt->saAmfCtDefCleanupCmdArgv = new SaStringT[j + 1]();
  osafassert(compt->saAmfCtDefCleanupCmdArgv);
  for (i = 0; i < j; i++) {
    str = immutil_getStringAttr(attributes, "saAmfCtDefCleanupCmdArgv", i);
    osafassert(str);
    compt->saAmfCtDefCleanupCmdArgv[i] = StrDup(str);
    osafassert(compt->saAmfCtDefCleanupCmdArgv[i]);
  }

  if ((str = immutil_getStringAttr(attributes, "saAmfCtRelPathAmStartCmd",
                                   0)) != nullptr)
    compt->saAmfCtRelPathAmStartCmd = StrDup(str);

  immutil_getAttrValuesNumber(
      const_cast<SaImmAttrNameT>("saAmfCtDefAmStartCmdArgv"), attributes, &j);
  compt->saAmfCtDefAmStartCmdArgv = new SaStringT[j + 1]();
  osafassert(compt->saAmfCtDefAmStartCmdArgv);
  for (i = 0; i < j; i++) {
    str = immutil_getStringAttr(attributes, "saAmfCtDefAmStartCmdArgv", i);
    osafassert(str);
    compt->saAmfCtDefAmStartCmdArgv[i] = StrDup(str);
    osafassert(compt->saAmfCtDefAmStartCmdArgv[i]);
  }

  if ((str = immutil_getStringAttr(attributes, "saAmfCtRelPathAmStopCmd", 0)) !=
      nullptr)
    compt->saAmfCtRelPathAmStopCmd = StrDup(str);

  immutil_getAttrValuesNumber(
      const_cast<SaImmAttrNameT>("saAmfCtDefAmStopCmdArgv"), attributes, &j);
  compt->saAmfCtDefAmStopCmdArgv = new SaStringT[j + 1]();
  osafassert(compt->saAmfCtDefAmStopCmdArgv);
  for (i = 0; i < j; i++) {
    str = immutil_getStringAttr(attributes, "saAmfCtDefAmStopCmdArgv", i);
    osafassert(str);
    compt->saAmfCtDefAmStopCmdArgv[i] = StrDup(str);
    osafassert(compt->saAmfCtDefAmStopCmdArgv[i]);
  }

  if ((str = immutil_getStringAttr(attributes, "osafAmfCtRelPathHcCmd", 0)) !=
      nullptr)
    compt->osafAmfCtRelPathHcCmd = StrDup(str);

  immutil_getAttrValuesNumber(
      const_cast<SaImmAttrNameT>("osafAmfCtDefHcCmdArgv"), attributes, &j);
  compt->osafAmfCtDefHcCmdArgv = new SaStringT[j + 1]();
  osafassert(compt->osafAmfCtDefHcCmdArgv);
  for (i = 0; i < j; i++) {
    str = immutil_getStringAttr(attributes, "osafAmfCtDefHcCmdArgv", i);
    osafassert(str);
    compt->osafAmfCtDefHcCmdArgv[i] = StrDup(str);
    osafassert(compt->osafAmfCtDefHcCmdArgv[i]);
  }

  if ((IS_COMP_SAAWARE(compt->saAmfCtCompCategory) ||
       IS_COMP_PROXIED_PI(compt->saAmfCtCompCategory)) &&
      (immutil_getAttr(
           const_cast<SaImmAttrNameT>("saAmfCtDefQuiescingCompleteTimeout"),
           attributes, 0,
           &compt->saAmfCompQuiescingCompleteTimeout) != SA_AIS_OK)) {
    compt->saAmfCompQuiescingCompleteTimeout = compt->saAmfCtDefCallbackTimeout;
    LOG_NO(
        "saAmfCtDefQuiescingCompleteTimeout for '%s' initialized with saAmfCtDefCallbackTimeout",
        dn.c_str());
  }

  if (immutil_getAttr(const_cast<SaImmAttrNameT>("saAmfCtDefRecoveryOnError"),
                      attributes, 0,
                      &compt->saAmfCtDefRecoveryOnError) != SA_AIS_OK)
    osafassert(0);

  if (compt->saAmfCtDefRecoveryOnError == SA_AMF_NO_RECOMMENDATION) {
    compt->saAmfCtDefRecoveryOnError = SA_AMF_COMPONENT_FAILOVER;
    LOG_NO(
        "COMPONENT_FAILOVER(%u) used instead of NO_RECOMMENDATION(%u) for '%s'",
        SA_AMF_COMPONENT_FAILOVER, SA_AMF_NO_RECOMMENDATION, dn.c_str());
  }

  if (immutil_getAttr(const_cast<SaImmAttrNameT>("saAmfCtDefDisableRestart"),
                      attributes, 0,
                      &compt->saAmfCtDefDisableRestart) != SA_AIS_OK)
    compt->saAmfCtDefDisableRestart = false;

done:
  (void)immutil_saImmOmAccessorFinalize(accessorHandle);

  TRACE_LEAVE();
  return compt;
}

static void init_comp_category(AVND_COMP *comp, SaAmfCompCategoryT category) {
  TRACE_ENTER2("'%s', %u", comp->name.c_str(), category);
  AVSV_COMP_TYPE_VAL comptype = avsv_amfcompcategory_to_avsvcomptype(category);

  osafassert(comptype != AVSV_COMP_TYPE_INVALID);

  switch (comptype) {
    case AVSV_COMP_TYPE_SA_AWARE:
      m_AVND_COMP_TYPE_SET(comp, AVND_COMP_TYPE_LOCAL);
      m_AVND_COMP_TYPE_SET(comp, AVND_COMP_TYPE_SAAWARE);
      m_AVND_COMP_TYPE_SET(comp, AVND_COMP_TYPE_PREINSTANTIABLE);
      break;

    case AVSV_COMP_TYPE_PROXIED_LOCAL_PRE_INSTANTIABLE:
      m_AVND_COMP_TYPE_SET(comp, AVND_COMP_TYPE_LOCAL);
      m_AVND_COMP_TYPE_SET(comp, AVND_COMP_TYPE_SAAWARE);
      m_AVND_COMP_TYPE_SET(comp, AVND_COMP_TYPE_PREINSTANTIABLE);
      m_AVND_COMP_TYPE_SET(comp, AVND_COMP_TYPE_PROXIED);
      break;

    case AVSV_COMP_TYPE_PROXIED_LOCAL_NON_PRE_INSTANTIABLE:
      m_AVND_COMP_TYPE_SET(comp, AVND_COMP_TYPE_LOCAL);
      m_AVND_COMP_TYPE_SET(comp, AVND_COMP_TYPE_SAAWARE);
      m_AVND_COMP_TYPE_SET(comp, AVND_COMP_TYPE_PROXIED);
      break;

    case AVSV_COMP_TYPE_EXTERNAL_PRE_INSTANTIABLE:
      m_AVND_COMP_TYPE_SET(comp, AVND_COMP_TYPE_PREINSTANTIABLE);
      m_AVND_COMP_TYPE_SET(comp, AVND_COMP_TYPE_PROXIED);
      break;

    case AVSV_COMP_TYPE_EXTERNAL_NON_PRE_INSTANTIABLE:
      m_AVND_COMP_TYPE_SET(comp, AVND_COMP_TYPE_PROXIED);
      break;

    case AVSV_COMP_TYPE_NON_SAF:
      m_AVND_COMP_TYPE_SET(comp, AVND_COMP_TYPE_LOCAL);
      break;

    default:
      osafassert(0);
      break;
  }
  TRACE_LEAVE();
}

static int get_string_attr_from_imm(SaImmOiHandleT immOmHandle,
                                    SaImmAttrNameT attrName,
                                    const std::string &dn, SaStringT *str) {
  int rc = -1;
  const SaImmAttrValuesT_2 **attributes;
  SaImmAccessorHandleT accessorHandle;
  SaImmAttrNameT attributeNames[2] = {attrName, nullptr};
  const char *s;
  SaAisErrorT error;
  TRACE_ENTER();

  amf_saImmOmAccessorInitialize(immOmHandle, accessorHandle);

  if ((error = amf_saImmOmAccessorGet_o2(
           immOmHandle, accessorHandle, dn, attributeNames,
           (SaImmAttrValuesT_2 ***)&attributes)) != SA_AIS_OK) {
    TRACE("amf_saImmOmAccessorGet_o2 FAILED %u for %s", error, dn.c_str());
    goto done;
  }

  if ((s = immutil_getStringAttr(attributes, attrName, 0)) == nullptr) {
    TRACE("Get %s FAILED for '%s'", attrName, dn.c_str());
    goto done;
  }

  *str = StrDup(s);
  rc = 0;

done:
  immutil_saImmOmAccessorFinalize(accessorHandle);
  TRACE_LEAVE();
  return rc;
}

/**
 * Initializes a single CLC-CLI command for a component.
 *
 * If path in comptype is absolute it is used, else (it is relative) it is
 * prepended with path prefix.
 *
 * @param cmd
 * @param clc_cmd
 * @param type_cmd_argv
 * @param path_prefix
 * @param attributes
 * @param attr_name
 */
static void init_clc_cli_command(AVND_COMP_CLC_CMD_PARAM *cmd,
                                 const char *clc_cmd, char **clc_cmd_argv,
                                 const char *path_prefix,
                                 const SaImmAttrValuesT_2 **attributes,
                                 const char *attr_name) {
  char *buf = cmd->cmd;
  int i, j;
  const char *argv;

  // prepend with path prefix if available
  if (path_prefix == nullptr)
    i = snprintf(buf, sizeof(cmd->cmd), "%s", clc_cmd);
  else
    i = snprintf(buf, sizeof(cmd->cmd), "%s/%s", path_prefix, clc_cmd);

  // append argv from comp type
  j = 0;
  while ((argv = clc_cmd_argv[j++]) != nullptr)
    i += snprintf(&buf[i], sizeof(cmd->cmd) - i, " %s", argv);

  // append argv from comp instance
  j = 0;
  while ((argv = immutil_getStringAttr(attributes, attr_name, j++)) != nullptr)
    i += snprintf(&buf[i], sizeof(cmd->cmd) - i, " %s", argv);

  cmd->len = i;

  /* Check for truncation, should alloc these strings dynamically instead */
  osafassert((cmd->len > 0) && (cmd->len < sizeof(cmd->cmd)));
  TRACE("cmd=%s", cmd->cmd);
}

/**
 * Initializes the CLC-CLI attributes (commands and timeout) for a component.
 *
 * @param comp
 * @param comptype
 * @param path_prefix
 * @param attributes
 */
static void init_clc_cli_attributes(AVND_COMP *comp,
                                    const amf_comp_type_t *comptype,
                                    const char *path_prefix,
                                    const SaImmAttrValuesT_2 **attributes) {
  AVND_COMP_CLC_CMD_PARAM *cmd;

  TRACE_ENTER();

  cmd = &comp->clc_info.cmds[AVND_COMP_CLC_CMD_TYPE_INSTANTIATE - 1];
  if (comptype->saAmfCtRelPathInstantiateCmd != nullptr) {
    init_clc_cli_command(cmd, comptype->saAmfCtRelPathInstantiateCmd,
                         comptype->saAmfCtDefInstantiateCmdArgv, path_prefix,
                         attributes, "saAmfCompInstantiateCmdArgv");
  }

  if (immutil_getAttr(const_cast<SaImmAttrNameT>("saAmfCompInstantiateTimeout"),
                      attributes, 0, &cmd->timeout) != SA_AIS_OK) {
    cmd->timeout = comptype->saAmfCtDefClcCliTimeout;
    comp->pxied_inst_cbk_timeout = comptype->saAmfCtDefCallbackTimeout;
    comp->use_comptype_attr->set(PxiedInstCallbackTimeout);
    comp->use_comptype_attr->set(CompInstantiateTimeout);
  } else {
    comp->pxied_inst_cbk_timeout = cmd->timeout;
  }

  cmd = &comp->clc_info.cmds[AVND_COMP_CLC_CMD_TYPE_TERMINATE - 1];
  if (comptype->saAmfCtRelPathTerminateCmd != nullptr) {
    init_clc_cli_command(cmd, comptype->saAmfCtRelPathTerminateCmd,
                         comptype->saAmfCtDefTerminateCmdArgv, path_prefix,
                         attributes, "saAmfCompTerminateCmdArgv");
  }

  if (immutil_getAttr(const_cast<SaImmAttrNameT>("saAmfCompTerminateTimeout"),
                      attributes, 0, &cmd->timeout) != SA_AIS_OK) {
    if (m_AVND_COMP_TYPE_IS_PREINSTANTIABLE(comp)) {
      cmd->timeout = comptype->saAmfCtDefCallbackTimeout;
      comp->term_cbk_timeout = cmd->timeout;
      comp->use_comptype_attr->set(TerminateCallbackTimeout);
      comp->use_comptype_attr->set(CompTerminateTimeout);
    } else
      cmd->timeout = comptype->saAmfCtDefClcCliTimeout;
  }

  cmd = &comp->clc_info.cmds[AVND_COMP_CLC_CMD_TYPE_CLEANUP - 1];
  if (comptype->saAmfCtRelPathCleanupCmd != nullptr) {
    init_clc_cli_command(cmd, comptype->saAmfCtRelPathCleanupCmd,
                         comptype->saAmfCtDefCleanupCmdArgv, path_prefix,
                         attributes, "saAmfCompCleanupCmdArgv");
  }

  if (immutil_getAttr(const_cast<SaImmAttrNameT>("saAmfCompCleanupTimeout"),
                      attributes, 0, &cmd->timeout) != SA_AIS_OK) {
    cmd->timeout = comptype->saAmfCtDefClcCliTimeout;
    comp->pxied_clean_cbk_timeout = comptype->saAmfCtDefCallbackTimeout;
    comp->use_comptype_attr->set(PxiedCleanupCallbackTimeout);
    comp->use_comptype_attr->set(CompCleanupTimeout);
  } else {
    comp->pxied_clean_cbk_timeout = cmd->timeout;
  }

  cmd = &comp->clc_info.cmds[AVND_COMP_CLC_CMD_TYPE_AMSTART - 1];
  if (comptype->saAmfCtRelPathAmStartCmd != nullptr) {
    init_clc_cli_command(cmd, comptype->saAmfCtRelPathAmStartCmd,
                         comptype->saAmfCtDefAmStartCmdArgv, path_prefix,
                         attributes, "saAmfCompAmStartCmdArgv");
    comp->is_am_en = true;
  }

  if (immutil_getAttr(const_cast<SaImmAttrNameT>("saAmfCompAmStartTimeout"),
                      attributes, 0, &cmd->timeout) != SA_AIS_OK) {
    cmd->timeout = comptype->saAmfCtDefClcCliTimeout;
    comp->use_comptype_attr->set(CompAmStartTimeout);
  }

  cmd = &comp->clc_info.cmds[AVND_COMP_CLC_CMD_TYPE_AMSTOP - 1];
  if (comptype->saAmfCtRelPathAmStopCmd != nullptr) {
    init_clc_cli_command(cmd, comptype->saAmfCtRelPathAmStopCmd,
                         comptype->saAmfCtDefAmStopCmdArgv, path_prefix,
                         attributes, "saAmfCompAmStopCmdArgv");
  }

  if (immutil_getAttr(const_cast<SaImmAttrNameT>("saAmfCompAmStopTimeout"),
                      attributes, 0, &cmd->timeout) != SA_AIS_OK) {
    cmd->timeout = comptype->saAmfCtDefClcCliTimeout;
    comp->use_comptype_attr->set(CompAmStopTimeout);
  }

  cmd = &comp->clc_info.cmds[AVND_COMP_CLC_CMD_TYPE_HC - 1];
  if (comptype->osafAmfCtRelPathHcCmd != nullptr) {
    init_clc_cli_command(cmd, comptype->osafAmfCtRelPathHcCmd,
                         comptype->osafAmfCtDefHcCmdArgv, path_prefix,
                         attributes, "osafAmfCompHcCmdArgv");
    comp->is_hc_cmd_configured = true;
  }

  TRACE_LEAVE();
}

/**
 * Initialize the members of the comp object with the configuration attributes
 * from IMM.
 *
 * @param comp
 * @param attributes
 *
 * @return int
 */
static int comp_init(AVND_COMP *comp, const SaImmAttrValuesT_2 **attributes) {
  int res = -1;
  amf_comp_type_t *comptype;
  SaNameT nodeswbundle_name;
  bool disable_restart;
  char *path_prefix = nullptr;
  unsigned int i;
  unsigned int num_of_comp_env = 0;
  unsigned int num_of_ct_env = 0;
  unsigned int env_cntr = 0;
  const char *str;
  SaStringT env;
  SaImmHandleT immOmHandle;
  SaVersionT immVersion = {'A', 2, 15};
  SaNameT node_name;
  SaNameT saAmfCtSwBundle;
  SaAisErrorT error;

  TRACE_ENTER2("%s", comp->name.c_str());
  osaf_extended_name_lend(avnd_cb->amf_nodeName.c_str(), &node_name);

  error = saImmOmInitialize_cond(&immOmHandle, nullptr, &immVersion);
  if (error != SA_AIS_OK) {
    LOG_CR("saImmOmInitialize failed: %u", error);
    goto done1;
  }

  if ((comptype = avnd_comptype_create(immOmHandle, comp->saAmfCompType)) ==
      nullptr) {
    LOG_ER("%s: avnd_comptype_create FAILED for '%s'", __FUNCTION__,
           comp->saAmfCompType.c_str());
    goto done;
  }

  osaf_extended_name_lend(comptype->saAmfCtSwBundle.c_str(), &saAmfCtSwBundle);
  avsv_create_association_class_dn(&saAmfCtSwBundle, &node_name,
                                   "safInstalledSwBundle", &nodeswbundle_name);

  (void)get_string_attr_from_imm(
      immOmHandle, const_cast<SaImmAttrNameT>("saAmfNodeSwBundlePathPrefix"),
      Amf::to_string(&nodeswbundle_name), &path_prefix);

  if (immutil_getAttr(const_cast<SaImmAttrNameT>("saAmfCompInstantiationLevel"),
                      attributes, 0, &comp->inst_level) != SA_AIS_OK) {
    comp->inst_level = comptype->saAmfCtDefInstantiationLevel;
    comp->use_comptype_attr->set(CompInstantiationLevel);
  }

  if (immutil_getAttr(
          const_cast<SaImmAttrNameT>("saAmfCompNumMaxInstantiateWithoutDelay"),
          attributes, 0, &comp->clc_info.inst_retry_max) != SA_AIS_OK)
    comp->clc_info.inst_retry_max =
        comp_global_attrs.saAmfNumMaxInstantiateWithoutDelay;

#if 0
        //  TODO
        if (immutil_getAttr("saAmfCompNumMaxInstantiateWithDelay", attributes,
                            0, &comp->max_num_inst_delay) != SA_AIS_OK)
                comp->comp_info.max_num_inst = comp_global_attrs.saAmfNumMaxInstantiateWithDelay;

        if (immutil_getAttr("saAmfCompDelayBetweenInstantiateAttempts", attributes,
                            0, &comp->inst_retry_delay) != SA_AIS_OK)
                comp->inst_retry_delay = comp_global_attrs.saAmfDelayBetweenInstantiateAttempts;
#endif

  if (immutil_getAttr(
          const_cast<SaImmAttrNameT>("saAmfCompNumMaxAmStartAttempts"),
          attributes, 0, &comp->clc_info.am_start_retry_max) != SA_AIS_OK)
    comp->clc_info.am_start_retry_max =
        comp_global_attrs.saAmfNumMaxAmStartAttempts;

  if (immutil_getAttr(
          const_cast<SaImmAttrNameT>("saAmfCompNumMaxAmStopAttempts"),
          attributes, 0,
          &comp->clc_info.saAmfCompNumMaxAmStopAttempts) != SA_AIS_OK)
    comp->clc_info.saAmfCompNumMaxAmStopAttempts =
        comp_global_attrs.saAmfNumMaxAmStopAttempts;

  if (immutil_getAttr(
          const_cast<SaImmAttrNameT>("saAmfCompCSISetCallbackTimeout"),
          attributes, 0, &comp->csi_set_cbk_timeout) != SA_AIS_OK) {
    comp->csi_set_cbk_timeout = comptype->saAmfCtDefCallbackTimeout;
    comp->use_comptype_attr->set(CsiSetCallbackTimeout);
  }

  if (immutil_getAttr(
          const_cast<SaImmAttrNameT>("saAmfCompCSIRmvCallbackTimeout"),
          attributes, 0, &comp->csi_rmv_cbk_timeout) != SA_AIS_OK) {
    comp->csi_rmv_cbk_timeout = comptype->saAmfCtDefCallbackTimeout;
    comp->use_comptype_attr->set(CsiRemoveCallbackTimeout);
  }

  if (immutil_getAttr(
          const_cast<SaImmAttrNameT>("saAmfCompQuiescingCompleteTimeout"),
          attributes, 0, &comp->quies_complete_cbk_timeout) != SA_AIS_OK) {
    comp->quies_complete_cbk_timeout =
        comptype->saAmfCompQuiescingCompleteTimeout;
    comp->use_comptype_attr->set(DefQuiescingCompleteTimeout);
  }

  if (immutil_getAttr(const_cast<SaImmAttrNameT>("saAmfCompRecoveryOnError"),
                      attributes, 0, &comp->err_info.def_rec) != SA_AIS_OK) {
    comp->err_info.def_rec = comptype->saAmfCtDefRecoveryOnError;
    comp->use_comptype_attr->set(DefRecoveryOnError);
  } else {
    if ((SaAmfRecommendedRecoveryT)comp->err_info.def_rec ==
        SA_AMF_NO_RECOMMENDATION) {
      comp->err_info.def_rec = SA_AMF_COMPONENT_FAILOVER;
      LOG_NO(
          "COMPONENT_FAILOVER(%u) used instead of NO_RECOMMENDATION(%u) for '%s'",
          SA_AMF_COMPONENT_FAILOVER, SA_AMF_NO_RECOMMENDATION,
          comp->name.c_str());
    }
  }

  if (immutil_getAttr(const_cast<SaImmAttrNameT>("saAmfCompDisableRestart"),
                      attributes, 0, &disable_restart) != SA_AIS_OK) {
    disable_restart = comptype->saAmfCtDefDisableRestart;
    comp->use_comptype_attr->set(DefDisableRestart);
  }

  comp->is_restart_en = (disable_restart == true) ? false : true;

  init_comp_category(comp, comptype->saAmfCtCompCategory);
  init_clc_cli_attributes(comp, comptype, path_prefix, attributes);

  /* Set oper status to enable irrespective of comp category PI or NPI. */
  m_AVND_COMP_OPER_STATE_SET(comp, SA_AMF_OPERATIONAL_ENABLED);

  /* Remove any previous environment variables */
  if (comp->saAmfCompCmdEnv != nullptr) {
    env_cntr = 0;
    while ((env = comp->saAmfCompCmdEnv[env_cntr++]) != nullptr) delete env;
    delete[] comp->saAmfCompCmdEnv;
    comp->saAmfCompCmdEnv = nullptr;
  }

  /* Find out how many environment variables there are in our comp type */
  num_of_ct_env = 0;
  if (comptype->saAmfCtDefCmdEnv != nullptr) {
    env_cntr = 0;
    while ((comptype->saAmfCtDefCmdEnv[env_cntr++]) != nullptr) num_of_ct_env++;
  }

  /* Find out how many environment variables in our comp */
  immutil_getAttrValuesNumber(const_cast<SaImmAttrNameT>("saAmfCompCmdEnv"),
                              attributes, &num_of_comp_env);

  /* Store the total number of env variables */
  comp->numOfCompCmdEnv = num_of_ct_env + num_of_comp_env;

  /* Allocate total number of environment variables */
  comp->saAmfCompCmdEnv = new SaStringT[comp->numOfCompCmdEnv + 1]();
  osafassert(comp->saAmfCompCmdEnv);

  /* Copy environment variables from our comp type */
  env_cntr = 0;
  while ((comptype->saAmfCtDefCmdEnv[env_cntr]) != nullptr) {
    comp->saAmfCompCmdEnv[env_cntr] =
        StrDup(comptype->saAmfCtDefCmdEnv[env_cntr]);
    env_cntr++;
  }

  /* Get environment variables from our IMM comp object */
  for (i = 0; i < num_of_comp_env; i++, env_cntr++) {
    str = immutil_getStringAttr(attributes, "saAmfCompCmdEnv", i);
    osafassert(str);
    comp->saAmfCompCmdEnv[env_cntr] = StrDup(str);
    osafassert(comp->saAmfCompCmdEnv[env_cntr]);
  }

  /* The env string array will be terminated by zero due to the c++
   * value-initialized new above */

  /* if we are missing path_prefix we need to refresh the config later */
  if (path_prefix != nullptr) comp->config_is_valid = 1;

  res = 0;

done:
  delete[] path_prefix;
  osaf_extended_name_free(&nodeswbundle_name);
  avnd_comptype_delete(comptype);
  immutil_saImmOmFinalize(immOmHandle);
done1:
  TRACE_LEAVE();
  return res;
}

/**
 * Delete an avnd component object.
 * @param comp
 *
 * @return
 */
void avnd_comp_delete(AVND_COMP *comp) {
  SaStringT env;

  /* Free saAmfCompCmdEnv[i] before freeing saAmfCompCmdEnv */
  if (comp->saAmfCompCmdEnv != nullptr) {
    int env_counter = 0;
    while ((env = comp->saAmfCompCmdEnv[env_counter++]) != nullptr)
      delete[] env;
    delete[] comp->saAmfCompCmdEnv;
  }

  delete comp->use_comptype_attr;
  delete comp;
  return;
}

/**
 * Create an avnd component object.
 * Validation has been done by avd => simple error handling (osafasserts).
 * Comp type argv and comp argv augments each other.
 * @param comp_name
 * @param attributes
 * @param su
 *
 * @return AVND_COMP*
 */
static AVND_COMP *avnd_comp_create(const std::string &comp_name,
                                   const SaImmAttrValuesT_2 **attributes,
                                   AVND_SU *su) {
  uint32_t rc = NCSCC_RC_SUCCESS;
  AVND_COMP *comp;
  SaAisErrorT error;
  SaNameT comp_type;

  TRACE_ENTER2("%s", comp_name.c_str());

  comp = new AVND_COMP();
  comp->use_comptype_attr = new std::bitset<NumAttrs>;

  comp->name = comp_name;

  error = immutil_getAttr(const_cast<SaImmAttrNameT>("saAmfCompType"),
                          attributes, 0, &comp_type);
  osafassert(error == SA_AIS_OK);
  comp->saAmfCompType = Amf::to_string(&comp_type);

  if (comp_init(comp, attributes) != 0) goto done;

  /* create the association with hdl-mngr */
  comp->comp_hdl =
      ncshm_create_hdl(avnd_cb->pool_id, NCS_SERVICE_ID_AVND, comp);
  if (0 == comp->comp_hdl) {
    LOG_ER("%s: ncshm_create_hdl FAILED for '%s'", __FUNCTION__,
           comp_name.c_str());
    goto done;
  }

  comp->avd_updt_flag = false;

  //      comp->cap = info->cap;
  comp->node_id = avnd_cb->node_info.nodeId;
  comp->pres = SA_AMF_PRESENCE_UNINSTANTIATED;

  /* Initialize the comp-hc list. */
  comp->hc_list.order = NCS_DBLIST_ANY_ORDER;
  comp->hc_list.cmp_cookie = avnd_dblist_hc_rec_cmp;
  comp->hc_list.free_cookie = 0;

  /* Initialize the comp-csi list. */
  comp->csi_list.order = NCS_DBLIST_ASSCEND_ORDER;
  comp->csi_list.cmp_cookie = avsv_dblist_sastring_cmp;
  comp->csi_list.free_cookie = 0;

  avnd_pm_list_init(comp);

  /* initialize proxied list */
  avnd_pxied_list_init(comp);

  /* Add to compdb. */
  osaf_mutex_lock_ordie(&compdb_mutex);
  rc = avnd_cb->compdb.insert(comp->name.c_str(), comp);
  if (rc != NCSCC_RC_SUCCESS) {
    LOG_ER("ncs_patricia_tree_add FAILED for '%s'", comp_name.c_str());
    osaf_mutex_unlock_ordie(&compdb_mutex);
    goto done;
  }
  osaf_mutex_unlock_ordie(&compdb_mutex);

  /* Add to the comp-list (maintained by su) */
  m_AVND_SUDB_REC_COMP_ADD(*su, *comp, rc);

  comp->su = su;
  comp->error_report_sent = false;
  comp->admin_oper = false;
  // Default SAF version.
  comp->version.releaseCode = 'B';
  comp->version.majorVersion = 0x01;
  comp->version.minorVersion = 0x01;

  if (true == su->su_is_external) {
    m_AVND_COMP_TYPE_SET_EXT_CLUSTER(comp);
  } else
    m_AVND_COMP_TYPE_SET_LOCAL_NODE(comp);

  /* determine if su is pre-instantiable */
  if (m_AVND_COMP_TYPE_IS_PREINSTANTIABLE(comp)) {
    m_AVND_SU_PREINSTANTIABLE_SET(comp->su);
    //                      m_AVND_SU_OPER_STATE_SET_AND_SEND_NTF(avnd_cb,
    //                      comp->su, SA_AMF_OPERATIONAL_DISABLED);
  }

  /* determine if su is restart capable */
  if (m_AVND_COMP_IS_RESTART_DIS(comp)) {
    m_AVND_SU_RESTART_DIS_SET(comp->su);
  }

  rc = 0;
done:
  if (rc != 0) {
    /* remove the association with hdl mngr */
    ncshm_destroy_hdl(NCS_SERVICE_ID_AVND, comp->comp_hdl);
    avnd_comp_delete(comp);
    comp = nullptr;
  }
  TRACE_LEAVE2("%u", rc);
  return comp;
}

/**
 * Get configuration for all AMF Comp objects related to the
 * specified SU from IMM and create internal objects.
 *
 * @param su
 *
 * @return SaAisErrorT
 */
unsigned int avnd_comp_config_get_su(AVND_SU *su) {
  unsigned int rc = NCSCC_RC_FAILURE;
  SaAisErrorT error;
  SaImmSearchHandleT searchHandle;
  SaImmSearchParametersT_2 searchParam;
  SaNameT comp_name;
  const SaImmAttrValuesT_2 **attributes;
  const char *className = "SaAmfComp";
  AVND_COMP *comp;
  SaImmHandleT immOmHandle;
  SaVersionT immVersion = {'A', 2, 15};
  uint32_t no_of_retries = 0;
  const uint32_t MAX_NO_RETRIES = immutilWrapperProfile.nTries;
  TRACE_ENTER2("SU'%s'", su->name.c_str());

  // this loop construct is to handle BAD_HANDLE from
  // immutil_saImmOmSearchNext_2()
  while (++no_of_retries < MAX_NO_RETRIES) {
    error = saImmOmInitialize_cond(&immOmHandle, nullptr, &immVersion);
    if (error != SA_AIS_OK) {
      LOG_CR("saImmOmInitialize failed: %u", error);
      rc = NCSCC_RC_OUT_OF_MEM;
      goto done;
    }
    searchParam.searchOneAttr.attrName =
        const_cast<SaImmAttrNameT>("SaImmAttrClassName");
    searchParam.searchOneAttr.attrValueType = SA_IMM_ATTR_SASTRINGT;
    searchParam.searchOneAttr.attrValue = &className;

    if ((error = amf_saImmOmSearchInitialize_o2(
             immOmHandle, su->name, SA_IMM_SUBTREE,
             SA_IMM_SEARCH_ONE_ATTR | SA_IMM_SEARCH_GET_CONFIG_ATTR,
             &searchParam, nullptr, searchHandle)) != SA_AIS_OK) {
      LOG_ER("amf_saImmOmSearchInitialize_o2 failed: %u", error);
      goto done1;
    }

    while (error == SA_AIS_OK) {
      error = immutil_saImmOmSearchNext_2(searchHandle, &comp_name,
                                          (SaImmAttrValuesT_2 ***)&attributes);

      if (error != SA_AIS_OK) {
        break;
      }

      const std::string cn = Amf::to_string(&comp_name);

      TRACE_1("'%s'", cn.c_str());
      if (avnd_compdb_rec_get(avnd_cb->compdb, cn) == nullptr) {
        if ((comp = avnd_comp_create(cn, attributes, su)) == nullptr)
          goto done2;

        avnd_hc_config_get(comp);
      }
    }

    if (error == SA_AIS_ERR_BAD_HANDLE) {
      immutil_saImmOmSearchFinalize(searchHandle);
      immutil_saImmOmFinalize(immOmHandle);
      continue;
    }

    rc = NCSCC_RC_SUCCESS;
    break;
  }

done2:
  (void)immutil_saImmOmSearchFinalize(searchHandle);
done1:
  immutil_saImmOmFinalize(immOmHandle);
done:
  TRACE_LEAVE();
  return rc;
}

/**
 * Reinitialize a comp object with configuration data from IMM.
 *
 * @param comp
 *
 * @return int
 */
int avnd_comp_config_reinit(AVND_COMP *comp) {
  int res = -1;
  SaImmAccessorHandleT accessorHandle;
  const SaImmAttrValuesT_2 **attributes;
  SaImmHandleT immOmHandle;
  SaVersionT immVersion = {'A', 2, 15};
  SaAisErrorT error;

  TRACE_ENTER2("'%s'", comp->name.c_str());

  /*
  ** If the component configuration is not valid (e.g. comptype has been
  ** changed by an SMF upgrade), refresh it from IMM.
  ** At first time instantiation of OpenSAF components we cannot go
  ** to IMM since we would deadloack.
  */
  if (comp->config_is_valid) {
    res = 0;
    goto done1;
  }

  TRACE_1("%s", comp->name.c_str());

  error = saImmOmInitialize_cond(&immOmHandle, nullptr, &immVersion);
  /* If this is a case of headless, then proceed without reading the
     configuration. */
  if (error != SA_AIS_OK) {
    if (avnd_cb->is_avd_down == true) {
      LOG_WA(
          "saImmOmInitialize FAILED(error: '%u') for '%s' in"
          " headless state, continuing with old configuration",
          error, comp->name.c_str());
      res = 0;
      goto done1;
    } else {
      LOG_CR("saImmOmInitialize FAILED for '%s', error %u", comp->name.c_str(),
             error);
      goto done1;
    }
  }
  error = amf_saImmOmAccessorInitialize(immOmHandle, accessorHandle);
  if (error != SA_AIS_OK) {
    LOG_CR("amf_saImmOmAccessorInitialize FAILED for '%s'", comp->name.c_str());
    goto done2;
  }
  if (amf_saImmOmAccessorGet_o2(immOmHandle, accessorHandle, comp->name,
                                nullptr, (SaImmAttrValuesT_2 ***)&attributes) !=
      SA_AIS_OK) {
    LOG_ER("amf_saImmOmAccessorGet_o2 FAILED for '%s'", comp->name.c_str());
    goto done3;
  }

  res = comp_init(comp, attributes);
  if (res == 0) TRACE("'%s' configuration reread from IMM", comp->name.c_str());

  /* need to get HC type configuration also if that has been recently created */
  avnd_hctype_config_get(immOmHandle, comp->saAmfCompType);

done3:
  immutil_saImmOmAccessorFinalize(accessorHandle);
done2:
  immutil_saImmOmFinalize(immOmHandle);
done1:
  TRACE_LEAVE2("%u", res);
  return res;
}

/**
 * This function return Comp rec.
 * @param Pointer to COMPDB.
 * @param Pointer to Comp name.
 * @return Pointer to Comp.
 */
AVND_COMP *avnd_compdb_rec_get(AmfDb<std::string, AVND_COMP> &compdb,
                               const std::string &name) {
  osaf_mutex_lock_ordie(&compdb_mutex);
  AVND_COMP *comp = compdb.find(name);
  osaf_mutex_unlock_ordie(&compdb_mutex);
  return comp;
}
AVND_COMP *avnd_compdb_rec_get_next(AmfDb<std::string, AVND_COMP> &compdb,
                                    const std::string &name) {
  osaf_mutex_lock_ordie(&compdb_mutex);
  AVND_COMP *comp = nullptr;
  if (compdb.size() == 0) goto done;

  if (name == "") {
    std::map<std::string, AVND_COMP *>::iterator it = compdb.begin();
    comp = it->second;
  } else {
    comp = compdb.findNext(name);
  }

done:
  osaf_mutex_unlock_ordie(&compdb_mutex);
  return comp;
}
