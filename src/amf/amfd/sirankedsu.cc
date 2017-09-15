/*      -*- OpenSAF  -*-
 *
 * (C) Copyright 2008 The OpenSAF Foundation
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

  DESCRIPTION:This module deals with the creation, accessing and deletion of
  the SU SI relationship list on the AVD.

******************************************************************************
*/

/*
 * Module Inclusion Control...
 */

#include "amf/common/amf_util.h"
#include "amf/amfd/util.h"
#include "amf/amfd/susi.h"
#include "osaf/immutil/immutil.h"
#include "amf/amfd/imm.h"
#include "amf/amfd/csi.h"
#include "base/logtrace.h"
#include <algorithm>
#include <string>

AmfDb<std::pair<std::string, uint32_t>, AVD_SUS_PER_SI_RANK> *sirankedsu_db =
    nullptr;
static void avd_susi_namet_init(const std::string &object_name,
                                std::string &su_name, std::string &si_name);

static void avd_sirankedsu_db_add(AVD_SUS_PER_SI_RANK *sirankedsu) {
  unsigned int rc = sirankedsu_db->insert(
      make_pair(sirankedsu->indx.si_name, sirankedsu->indx.su_rank),
      sirankedsu);
  osafassert(rc == NCSCC_RC_SUCCESS);

  /* Find the si name. */
  AVD_SI *avd_si = avd_si_get(sirankedsu->indx.si_name);
  avd_si->add_rankedsu(sirankedsu->su_name, sirankedsu->indx.su_rank);

  /* Add sus_per_si_rank to si */
  sirankedsu->sus_per_si_rank_on_si = avd_si;
  sirankedsu->sus_per_si_rank_list_si_next =
      sirankedsu->sus_per_si_rank_on_si->list_of_sus_per_si_rank;
  sirankedsu->sus_per_si_rank_on_si->list_of_sus_per_si_rank = sirankedsu;
}

/*****************************************************************************
 * Function: avd_sirankedsu_create
 *
 * Purpose:  This function will create and add a AVD_SUS_PER_SI_RANK structure
 *to the tree if an element with given index value doesn't exist in the tree.
 *
 * Input: cb - the AVD control block
 *        indx - The key info  needs to add a element in the petricia tree
 *
 * Returns: The pointer to AVD_SUS_PER_SI_RANK structure allocated and added.
 *
 * NOTES:
 *
 *
 **************************************************************************/

static AVD_SUS_PER_SI_RANK *avd_sirankedsu_create(
    AVD_CL_CB *cb, const AVD_SUS_PER_SI_RANK_INDX &indx) {
  AVD_SUS_PER_SI_RANK *ranked_su_per_si;

  ranked_su_per_si = new AVD_SUS_PER_SI_RANK();

  ranked_su_per_si->indx.si_name = indx.si_name;
  ranked_su_per_si->indx.su_rank = indx.su_rank;

  return ranked_su_per_si;
}

/*****************************************************************************
 * Function: avd_sirankedsu_find
 *
 * Purpose:  This function will find a AVD_SUS_PER_SI_RANK structure in the
 * tree with indx value as key.
 *
 * Input: cb - the AVD control block
 *        indx - The key.
 *
 * Returns: The pointer to AVD_SUS_PER_SI_RANK structure found in the tree.
 *
 * NOTES:
 *
 *
 **************************************************************************/

static AVD_SUS_PER_SI_RANK *avd_sirankedsu_find(
    AVD_CL_CB *cb, const AVD_SUS_PER_SI_RANK_INDX &indx) {
  AVD_SUS_PER_SI_RANK_INDX rank_indx;

  rank_indx.si_name = indx.si_name;
  rank_indx.su_rank = indx.su_rank;

  AVD_SUS_PER_SI_RANK *ranked_su_per_si =
      sirankedsu_db->find(make_pair(rank_indx.si_name, rank_indx.su_rank));

  return ranked_su_per_si;
}

/*****************************************************************************
 * Function: avd_sirankedsu_delete
 *
 * Purpose:  This function will delete and free AVD_SUS_PER_SI_RANK structure
 *from the tree.
 *
 * Input: cb - the AVD control block
 *        ranked_su_per_si - The AVD_SUS_PER_SI_RANK structure that needs to be
 *deleted.
 *
 * Returns: NCSCC_RC_SUCCESS/NCSCC_RC_FAILURE
 *
 * NOTES:
 *
 *
 **************************************************************************/
static uint32_t avd_sirankedsu_delete(AVD_CL_CB *cb,
                                      AVD_SUS_PER_SI_RANK *ranked_su_per_si) {
  if (ranked_su_per_si == nullptr) return NCSCC_RC_FAILURE;

  sirankedsu_db->erase(make_pair(ranked_su_per_si->indx.si_name,
                                 ranked_su_per_si->indx.su_rank));
  delete ranked_su_per_si;
  return NCSCC_RC_SUCCESS;
}

/*****************************************************************************
 * Function: avd_sirankedsu_ccb_apply_create_hdlr
 *
 * Purpose: This routine handles create operations on SaAmfSIRankedSU objects.
 *
 *
 * Input  : Control Block, Ccb Util Oper Data.
 *
 * Returns: None.
 *
 * NOTES  : None.
 *
 *
 **************************************************************************/
static AVD_SUS_PER_SI_RANK *avd_sirankedsu_ccb_apply_create_hdlr(
    SaNameT *dn, const SaImmAttrValuesT_2 **attributes) {
  uint32_t rank = 0;
  std::string su_name;
  std::string si_name;
  AVD_SUS_PER_SI_RANK_INDX indx;

  immutil_getAttr(const_cast<SaImmAttrNameT>("saAmfRank"), attributes, 0,
                  &rank);

  avd_susi_namet_init(Amf::to_string(dn), su_name, si_name);

  /* Find the avd_sus_per_si_rank name. */
  indx.si_name = si_name;
  indx.su_rank = rank;

  AVD_SUS_PER_SI_RANK *avd_sus_per_si_rank =
      avd_sirankedsu_create(avd_cb, indx);
  osafassert(avd_sus_per_si_rank);

  avd_sus_per_si_rank->su_name = su_name;

  return avd_sus_per_si_rank;
}

/*****************************************************************************
 * Function: avd_sirankedsu_del_si_list
 *
 * Purpose:  This function will del the given sus_per_si_rank from si list.
 *
 * Input: cb - the AVD control block
 *        sus_per_si_rank - The sus_per_si_rank pointer
 *
 * Returns: None.
 *
 * NOTES: None
 *
 *
 **************************************************************************/
static void avd_sirankedsu_del_si_list(AVD_CL_CB *cb,
                                       AVD_SUS_PER_SI_RANK *sus_per_si_rank) {
  AVD_SUS_PER_SI_RANK *i_sus_per_si_rank = nullptr;
  AVD_SUS_PER_SI_RANK *prev_sus_per_si_rank = nullptr;

  if (sus_per_si_rank->sus_per_si_rank_on_si != nullptr) {
    i_sus_per_si_rank =
        sus_per_si_rank->sus_per_si_rank_on_si->list_of_sus_per_si_rank;

    while ((i_sus_per_si_rank != nullptr) &&
           (i_sus_per_si_rank != sus_per_si_rank)) {
      prev_sus_per_si_rank = i_sus_per_si_rank;
      i_sus_per_si_rank = i_sus_per_si_rank->sus_per_si_rank_list_si_next;
    }

    if (i_sus_per_si_rank != sus_per_si_rank) {
      LOG_CR(
          "SI '%s' having SU '%s' with rank %u, does not exist in sirankedsu link list",
          sus_per_si_rank->indx.si_name.c_str(),
          sus_per_si_rank->su_name.c_str(), sus_per_si_rank->indx.su_rank);
    } else {
      if (prev_sus_per_si_rank == nullptr) {
        sus_per_si_rank->sus_per_si_rank_on_si->list_of_sus_per_si_rank =
            sus_per_si_rank->sus_per_si_rank_list_si_next;
      } else {
        prev_sus_per_si_rank->sus_per_si_rank_list_si_next =
            sus_per_si_rank->sus_per_si_rank_list_si_next;
      }
    }
    sus_per_si_rank->sus_per_si_rank_on_si->remove_rankedsu(
        sus_per_si_rank->su_name);

    sus_per_si_rank->sus_per_si_rank_list_si_next = nullptr;
    sus_per_si_rank->sus_per_si_rank_on_si = nullptr;
  }

  return;
}

static int is_config_valid(const std::string &dn,
                           const SaImmAttrValuesT_2 **attributes,
                           CcbUtilOperationData_t *opdata) {
  std::string su_name;
  std::string si_name;
  uint32_t rank = 0;
  AVD_SUS_PER_SI_RANK_INDX indx;
  AVD_SU *avd_su = nullptr;

  avd_susi_namet_init(dn, su_name, si_name);

  /* Find the si name. */
  AVD_SI *avd_si = avd_si_get(si_name);

  if (avd_si == nullptr) {
    /* SI does not exist in current model, check CCB */
    if (opdata == nullptr) {
      report_ccb_validation_error(opdata, "'%s' does not exist in model",
                                  si_name.c_str());
      return 0;
    }

    const SaNameTWrapper tmp_si_name(si_name);
    if (ccbutil_getCcbOpDataByDN(opdata->ccbId, tmp_si_name) == nullptr) {
      report_ccb_validation_error(
          opdata, "'%s' does not exist in existing model or in CCB",
          si_name.c_str());
      return 0;
    }
  }

  /* Find the su name. */
  avd_su = su_db->find(su_name);
  if (avd_su == nullptr) {
    /* SU does not exist in current model, check CCB */
    if (opdata == nullptr) {
      report_ccb_validation_error(opdata, "'%s' does not exist in model",
                                  su_name.c_str());
      return 0;
    }

    const SaNameTWrapper tmp_su_name(su_name);
    if (ccbutil_getCcbOpDataByDN(opdata->ccbId, tmp_su_name) == nullptr) {
      report_ccb_validation_error(
          opdata, "'%s' does not exist in existing model or in CCB",
          su_name.c_str());
      return 0;
    }
  }

  if (immutil_getAttr(const_cast<SaImmAttrNameT>("saAmfRank"), attributes, 0,
                      &rank) != SA_AIS_OK) {
    report_ccb_validation_error(opdata, "saAmfRank not found for %s",
                                dn.c_str());
    return 0;
  }

  indx.si_name = si_name;
  indx.su_rank = rank;
  if ((avd_sirankedsu_find(avd_cb, indx)) != nullptr) {
    if (opdata != nullptr) {
      report_ccb_validation_error(opdata,
                                  "saAmfRankedSu exists %s, si'%s', rank'%u'",
                                  dn.c_str(), si_name.c_str(), rank);
      return 0;
    }
    return SA_AIS_OK;
  }

  return SA_AIS_OK;
}

static void avd_susi_namet_init(const std::string &object_name,
                                std::string &su_name, std::string &si_name) {
  std::string::size_type pos;
  std::string::size_type equal_pos;

  // DN looks like: safRankedSu=safSu=SuName\,safSg=SgName\,
  //      safApp=AppName,safSi=SiName,safApp=AppName */

  // set si_name
  pos = object_name.find("safSi=");
  si_name = object_name.substr(pos);

  // set su_name
  equal_pos = object_name.find('=');
  su_name = object_name.substr(equal_pos + 1, pos - equal_pos - 2);
  su_name.erase(std::remove(su_name.begin(), su_name.end(), '\\'),
                su_name.end());
}

static void sirankedsu_ccb_apply_cb(CcbUtilOperationData_t *opdata) {
  AVD_SUS_PER_SI_RANK *avd_sirankedsu = nullptr;

  TRACE_ENTER2("CCB ID %llu, '%s'", opdata->ccbId,
               osaf_extended_name_borrow(&opdata->objectName));

  switch (opdata->operationType) {
    case CCBUTIL_CREATE:
      avd_sirankedsu = avd_sirankedsu_ccb_apply_create_hdlr(
          &opdata->objectName, opdata->param.create.attrValues);
      avd_sirankedsu_db_add(avd_sirankedsu);

      break;
    case CCBUTIL_DELETE:
      if (opdata->userData == nullptr && avd_cb->is_active () == false) {
        break;
      }
      osafassert(opdata->userData != nullptr);
      /* delete and free the structure */
      avd_sirankedsu_del_si_list(
          avd_cb, static_cast<AVD_SUS_PER_SI_RANK *>(opdata->userData));
      avd_sirankedsu_delete(
          avd_cb, static_cast<AVD_SUS_PER_SI_RANK *>(opdata->userData));
      break;
    default:
      osafassert(0);
      break;
  }
  TRACE_LEAVE();
}

static int avd_sirankedsu_ccb_complete_delete_hdlr(
    CcbUtilOperationData_t *opdata) {
  AVD_SI *si = nullptr;
  std::string su_name;
  std::string si_name;
  AVD_SUS_PER_SI_RANK *su_rank_rec = 0;
  bool found = false;

  TRACE_ENTER2("CCB ID %llu, '%s'", opdata->ccbId,
               osaf_extended_name_borrow(&opdata->objectName));

  avd_susi_namet_init(Amf::to_string(opdata->param.delete_.objectName), su_name,
                      si_name);

  /* determine if the su is ranked per si */
  for (const auto &value : *sirankedsu_db) {
    su_rank_rec = value.second;

    if (su_rank_rec->indx.si_name.compare(si_name) == 0 &&
        su_rank_rec->su_name.compare(su_name) == 0) {
      found = true;
      break;
    }
  }

  if (false == found) {
    LOG_ER("'%s' not found", osaf_extended_name_borrow(&opdata->objectName));
    goto error;
  }

  /* Find the si name. */
  si = avd_si_get(si_name);

  if (si == nullptr) {
    LOG_ER("SI '%s' not found", si_name.c_str());
    goto error;
  }

  if (si != nullptr) {
    /* SI should not be assigned while SI ranked SU needs to be deleted */
    if (si->list_of_sisu != nullptr) {
      TRACE("Parent SI is in assigned state '%s'", si->name.c_str());
      goto error;
    }
  }
  opdata->userData = su_rank_rec; /* Save for later use in apply */

  TRACE_LEAVE2("%u", 1);
  return 1;
error:
  TRACE_LEAVE2("%u", 0);
  return 0;
}

static SaAisErrorT sirankedsu_ccb_completed_cb(CcbUtilOperationData_t *opdata) {
  SaAisErrorT rc = SA_AIS_ERR_BAD_OPERATION;

  TRACE_ENTER2("CCB ID %llu, '%s'", opdata->ccbId,
               osaf_extended_name_borrow(&opdata->objectName));

  switch (opdata->operationType) {
    case CCBUTIL_CREATE:
      if (is_config_valid(Amf::to_string(&opdata->objectName),
                          opdata->param.create.attrValues, opdata))
        rc = SA_AIS_OK;
      break;
    case CCBUTIL_MODIFY:
      report_ccb_validation_error(
          opdata, "Modification of SaAmfSIRankedSU not supported");
      break;
    case CCBUTIL_DELETE: {
      if (avd_sirankedsu_ccb_complete_delete_hdlr(opdata)) rc = SA_AIS_OK;
    } break;
    default:
      osafassert(0);
      break;
  }

  TRACE_LEAVE2("%u", rc);
  return rc;
}

SaAisErrorT avd_sirankedsu_config_get(const std::string &si_name, AVD_SI *si) {
  SaAisErrorT error = SA_AIS_ERR_FAILED_OPERATION;
  SaImmSearchHandleT searchHandle;
  SaImmSearchParametersT_2 searchParam;
  const SaImmAttrValuesT_2 **attributes;
  const char *className = "SaAmfSIRankedSU";
  AVD_SUS_PER_SI_RANK_INDX indx;
  SaNameT dn;
  AVD_SUS_PER_SI_RANK *avd_sirankedsu = nullptr;

  TRACE_ENTER();

  searchParam.searchOneAttr.attrName =
      const_cast<SaImmAttrNameT>("SaImmAttrClassName");
  searchParam.searchOneAttr.attrValueType = SA_IMM_ATTR_SASTRINGT;
  searchParam.searchOneAttr.attrValue = &className;

  if (immutil_saImmOmSearchInitialize_o2(
          avd_cb->immOmHandle, si_name.c_str(), SA_IMM_SUBTREE,
          SA_IMM_SEARCH_ONE_ATTR | SA_IMM_SEARCH_GET_ALL_ATTR, &searchParam,
          nullptr, &searchHandle) != SA_AIS_OK) {
    LOG_ER("No objects found (1)");
    goto done1;
  }

  while (immutil_saImmOmSearchNext_2(searchHandle, &dn,
                                     (SaImmAttrValuesT_2 ***)&attributes) ==
         SA_AIS_OK) {
    LOG_NO("'%s'", osaf_extended_name_borrow(&dn));

    indx.si_name = si_name;
    if (immutil_getAttr(const_cast<SaImmAttrNameT>("saAmfRank"), attributes, 0,
                        &indx.su_rank) != SA_AIS_OK) {
      LOG_ER("Get saAmfRank FAILED for '%s'", osaf_extended_name_borrow(&dn));
      goto done1;
    }

    if (!is_config_valid(Amf::to_string(&dn), attributes, nullptr)) goto done2;

    if ((avd_sirankedsu = avd_sirankedsu_find(avd_cb, indx)) == nullptr) {
      if ((avd_sirankedsu = avd_sirankedsu_ccb_apply_create_hdlr(
               &dn, attributes)) == nullptr)
        goto done2;

      avd_sirankedsu_db_add(avd_sirankedsu);
    }
  }

  error = SA_AIS_OK;

done2:
  (void)immutil_saImmOmSearchFinalize(searchHandle);
done1:
  TRACE_LEAVE2("%u", error);
  return error;
}

void avd_sirankedsu_constructor(void) {
  sirankedsu_db =
      new AmfDb<std::pair<std::string, uint32_t>, AVD_SUS_PER_SI_RANK>;
  avd_class_impl_set("SaAmfSIRankedSU", nullptr, nullptr,
                     sirankedsu_ccb_completed_cb, sirankedsu_ccb_apply_cb);
}
