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
#include <list>
#include <string>
#include "stdio.h"
#include "base/logtrace.h"
#include "base/osaf_extended_name.h"
#include "smf/smfd/SmfRollback.h"
#include "smf/smfd/SmfImmOperation.h"
#include "smf/smfd/SmfCampaignThread.h"
#include "smf/smfd/SmfUtils.h"

#include <saAis.h>
#include <saImmOm.h>
#include "osaf/immutil/immutil.h"
#include <saImm.h>
#include "base/saf_error.h"

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

/* ========================================================================
 *   FUNCTION PROTOTYPES
 * ========================================================================
 */
SaAisErrorT smfCreateRollbackElement(const std::string& i_dn,
                                     SaImmOiHandleT i_oiHandle) {
  TRACE("Create rollback element '%s'", i_dn.c_str());

  SmfImmRTCreateOperation icoRollbackCcb;

  std::string parentDn = i_dn.substr(i_dn.find(',') + 1, std::string::npos);
  std::string rdnStr = i_dn.substr(0, i_dn.find(','));

  TRACE("Create rollback element, parent '%s', rdn '%s'", parentDn.c_str(),
        rdnStr.c_str());

  icoRollbackCcb.SetClassName("OpenSafSmfRollbackElement");
  icoRollbackCcb.SetParentDn(parentDn);
  icoRollbackCcb.SetImmHandle(i_oiHandle);

  SmfImmAttribute rdn;
  rdn.SetAttributeName("smfRollbackElement");
  rdn.SetAttributeType("SA_IMM_ATTR_SASTRINGT");
  rdn.AddAttributeValue(rdnStr);
  icoRollbackCcb.AddValue(rdn);

  return icoRollbackCcb.Execute();  // Create the object
}

//================================================================================
// Class SmfRollbackData
// Purpose:
// Comments:
//================================================================================

SmfRollbackData::SmfRollbackData(SmfRollbackCcb* i_ccb)
    : m_ccb(i_ccb), m_id(0) {}

// ------------------------------------------------------------------------------
// ~SmfRollbackData()
// ------------------------------------------------------------------------------
SmfRollbackData::~SmfRollbackData() {}

//------------------------------------------------------------------------------
// setType()
//------------------------------------------------------------------------------
void SmfRollbackData::setType(const std::string& i_type) { m_type = i_type; }

//------------------------------------------------------------------------------
// setDn()
//------------------------------------------------------------------------------
void SmfRollbackData::setDn(const std::string& i_dn) { m_dn = i_dn; }

//------------------------------------------------------------------------------
// setClass()
//------------------------------------------------------------------------------
void SmfRollbackData::setClass(const std::string& i_class) {
  m_class = i_class;
}

//------------------------------------------------------------------------------
// addAttrValue()
//------------------------------------------------------------------------------
void SmfRollbackData::addAttrValue(const std::string& i_attrName,
                                   const std::string& i_attrType,
                                   const std::string& i_attrValue) {
  m_attrValues.push_back(
      std::string(i_attrName + ":" + i_attrType + "#" + i_attrValue));
}

//------------------------------------------------------------------------------
// execute()
//------------------------------------------------------------------------------
SaAisErrorT SmfRollbackData::execute(SaImmOiHandleT i_oiHandle) {
  SaAisErrorT result = SA_AIS_OK;

  /*
     1) Create rollback data object with the rollback CCB as parent
  */

  SmfImmRTCreateOperation icoRollbackData;

  icoRollbackData.SetClassName("OpenSafSmfRollbackData");
  icoRollbackData.SetParentDn(m_ccb->getDn());
  icoRollbackData.SetImmHandle(i_oiHandle);

  char idStr[16];
  snprintf(idStr, sizeof(idStr), "%08u", m_id);
  std::string rdnValue = "smfRollbackData=";
  rdnValue += idStr;

  SmfImmAttribute rdn;
  rdn.SetAttributeName("smfRollbackData");
  rdn.SetAttributeType("SA_IMM_ATTR_SASTRINGT");
  rdn.AddAttributeValue(rdnValue);
  icoRollbackData.AddValue(rdn);

  SmfImmAttribute typeAttr;
  typeAttr.SetAttributeName("smfRollbackType");
  typeAttr.SetAttributeType("SA_IMM_ATTR_SASTRINGT");
  typeAttr.AddAttributeValue(m_type);
  icoRollbackData.AddValue(typeAttr);

  SmfImmAttribute dnAttr;
  dnAttr.SetAttributeName("smfRollbackDn");
  dnAttr.SetAttributeType("SA_IMM_ATTR_SANAMET");
  dnAttr.AddAttributeValue(m_dn);
  icoRollbackData.AddValue(dnAttr);

  SmfImmAttribute classAttr;
  classAttr.SetAttributeName("smfRollbackClass");
  classAttr.SetAttributeType("SA_IMM_ATTR_SASTRINGT");
  classAttr.AddAttributeValue(m_class);
  icoRollbackData.AddValue(classAttr);

  SmfImmAttribute attrValueAttr;
  attrValueAttr.SetAttributeName("smfRollbackAttrValue");
  attrValueAttr.SetAttributeType("SA_IMM_ATTR_SASTRINGT");

  if (m_attrValues.size() == 0) {
    attrValueAttr.AddAttributeValue("");
  } else {
    for (auto& elem : m_attrValues) {
      attrValueAttr.AddAttributeValue((elem));
    }
  }

  icoRollbackData.AddValue(attrValueAttr);

  result = icoRollbackData.Execute();  // Create the object
  if (result != SA_AIS_OK) {
    LOG_ER(
        "SmfRollbackData::execute, Failed to create IMM rollback data object, %s",
        saf_error(result));
  }

  return result;
}

//------------------------------------------------------------------------------
// rollback()
//------------------------------------------------------------------------------
SaAisErrorT SmfRollbackData::rollback(
    const std::string& i_dn, std::list<SmfImmOperation*>& io_operationList) {
  SmfImmUtils immUtil;
  SaImmAttrValuesT_2** attributes;
  const char* typeAttr;

  /*
     1) Read IMM object representing this rollback data and create an IMM
     operation object
  */

  TRACE("Rollback of data in %s", i_dn.c_str());

  if (immUtil.getObject(i_dn, &attributes) == false) {
    LOG_ER("SmfRollbackData::rollback, fail to get rollback data imm object %s",
           i_dn.c_str());
    return SA_AIS_ERR_FAILED_OPERATION;
  }

  typeAttr = immutil_getStringAttr((const SaImmAttrValuesT_2**)attributes,
                                   "smfRollbackType", 0);

  if (typeAttr == NULL) {
    LOG_ER(
        "SmfRollbackData::rollback, could not find smfRollbackType attribute in %s",
        i_dn.c_str());
    return SA_AIS_ERR_FAILED_OPERATION;
  }

  if (!strcmp(typeAttr, "CREATE")) {
    return rollbackCreateOperation((const SaImmAttrValuesT_2**)attributes,
                                   io_operationList);
  } else if (!strcmp(typeAttr, "DELETE")) {
    return rollbackDeleteOperation((const SaImmAttrValuesT_2**)attributes,
                                   io_operationList);
  } else if (!strcmp(typeAttr, "MODIFY")) {
    return rollbackModifyOperation((const SaImmAttrValuesT_2**)attributes,
                                   io_operationList);
  } else {
    LOG_ER("SmfRollbackData::rollback, unknown type attribute value %s in %s",
           typeAttr, i_dn.c_str());
    return SA_AIS_ERR_FAILED_OPERATION;
  }

  return SA_AIS_OK;
}

//------------------------------------------------------------------------------
// rollbackCreateOperation()
//------------------------------------------------------------------------------
SaAisErrorT SmfRollbackData::rollbackCreateOperation(
    const SaImmAttrValuesT_2** i_attributes,
    std::list<SmfImmOperation*>& io_operationList) {
  const SaNameT* dnAttr;
  const char* classAttr;
  SaAisErrorT result;
  SaUint32T noOfAttrValues;

  /*
     1) Read rollback data for a create operation and create corresponding IMM
     operation
  */

  /* TODO not finished yet */

  dnAttr = immutil_getNameAttr(i_attributes, "smfRollbackDn", 0);
  if (dnAttr == NULL) {
    LOG_ER(
        "SmfRollbackData::rollbackCreateOperation, could not find smfRollbackDn attribute");
    return SA_AIS_ERR_FAILED_OPERATION;
  }

  classAttr = immutil_getStringAttr(i_attributes, "smfRollbackClass", 0);
  if (classAttr == NULL) {
    LOG_ER(
        "SmfRollbackData::rollbackCreateOperation, could not find smfRollbackClass attribute");
    return SA_AIS_ERR_FAILED_OPERATION;
  }

  result = immutil_getAttrValuesNumber((char*)"smfRollbackAttrValue",
                                       i_attributes, &noOfAttrValues);

  if (result != SA_AIS_OK) {
    LOG_ER(
        "SmfRollbackData::rollbackCreateOperation, could not find smfRollbackAttrValue attribute");
    return SA_AIS_ERR_FAILED_OPERATION;
  }

  TRACE("Rollback create object %s no of attributes %d",
        osaf_extended_name_borrow(dnAttr), noOfAttrValues);

  if (noOfAttrValues == 0) {
    LOG_ER(
        "SmfRollbackData::rollbackCreateOperation, at least one attribute (rdn) is needed when createing object");
    return SA_AIS_ERR_FAILED_OPERATION;
  }

  SmfImmCreateOperation* immOp = new (std::nothrow) SmfImmCreateOperation();
  if (immOp == NULL) {
    LOG_ER(
        "SmfRollbackData::rollbackCreateOperation, could not create SmfImmCreateOperation");
    return SA_AIS_ERR_FAILED_OPERATION;
  }

  immOp->SetParentDn(osaf_extended_name_borrow(dnAttr));
  immOp->SetClassName(classAttr);

  for (unsigned int index = 0; index < noOfAttrValues; index++) {
    const char* attrValueString =
        immutil_getStringAttr(i_attributes, "smfRollbackAttrValue", index);
    if (attrValueString == NULL) {
      LOG_ER("Could not get smfRollbackAttrValue index %u, no of values %u",
             index, noOfAttrValues);
      delete immOp;
      return SA_AIS_ERR_FAILED_OPERATION;
    }

    TRACE("Rollback create object attribute value string %s", attrValueString);
    std::string attrValueStr(attrValueString);
    unsigned int colonPos = attrValueStr.find(':');
    unsigned int hashPos = attrValueStr.find('#');
    std::string attrName = attrValueStr.substr(0, colonPos);
    std::string attrType =
        attrValueStr.substr(colonPos + 1, hashPos - colonPos - 1);
    std::string attrValue = attrValueStr.substr(hashPos + 1, std::string::npos);

    TRACE("Rollback create object attribute name %s, type %s, value '%s'",
          attrName.c_str(), attrType.c_str(), attrValue.c_str());
    immOp->AddOrUpdateAttribute(attrName, attrType, attrValue);
  }

  io_operationList.push_back(immOp);

  return SA_AIS_OK;
}

//------------------------------------------------------------------------------
// rollbackDeleteOperation()
//------------------------------------------------------------------------------
SaAisErrorT SmfRollbackData::rollbackDeleteOperation(
    const SaImmAttrValuesT_2** i_attributes,
    std::list<SmfImmOperation*>& io_operationList) {
  const SaNameT* dnAttr;

  /*
     1) Read rollback data for a delete operation and create corresponding IMM
     operation
  */

  dnAttr = immutil_getNameAttr(i_attributes, "smfRollbackDn", 0);
  if (dnAttr == NULL) {
    LOG_ER(
        "SmfRollbackData::rollbackDeleteOperation, could not find smfRollbackDn attribute");
    return SA_AIS_ERR_FAILED_OPERATION;
  }

  SmfImmDeleteOperation* immOp = new (std::nothrow) SmfImmDeleteOperation();
  if (immOp == NULL) {
    LOG_ER(
        "SmfRollbackData::rollbackDeleteOperation, could not create SmfImmDeleteOperation");
    return SA_AIS_ERR_FAILED_OPERATION;
  }

  TRACE("Rollback delete object %s", osaf_extended_name_borrow(dnAttr));

  immOp->SetDn(osaf_extended_name_borrow(dnAttr));

  io_operationList.push_back(immOp);

  return SA_AIS_OK;
}

//------------------------------------------------------------------------------
// rollbackModifyOperation()
//------------------------------------------------------------------------------
SaAisErrorT SmfRollbackData::rollbackModifyOperation(
    const SaImmAttrValuesT_2** i_attributes,
    std::list<SmfImmOperation*>& io_operationList) {
  const SaNameT* dnAttr;
  SaAisErrorT result;
  SaUint32T noOfAttrValues;

  /*
     1) Read rollback data for a modify replace operation and create
     corresponding IMM operation
  */

  /* TODO not finished yet */

  dnAttr = immutil_getNameAttr(i_attributes, "smfRollbackDn", 0);
  if (dnAttr == NULL) {
    LOG_ER(
        "SmfRollbackData::rollbackModifyOperation, could not find smfRollbackDn attribute");
    return SA_AIS_ERR_FAILED_OPERATION;
  }

  result = immutil_getAttrValuesNumber((char*)"smfRollbackAttrValue",
                                       i_attributes, &noOfAttrValues);

  if (result != SA_AIS_OK) {
    LOG_ER(
        "SmfRollbackData::rollbackModifyOperation, could not find smfRollbackAttrValue attribute");
    return SA_AIS_ERR_FAILED_OPERATION;
  }

  if (noOfAttrValues == 0) {
    LOG_ER(
        "SmfRollbackData::rollbackModifyOperation, at least one attribute needs to be modified");
    return SA_AIS_ERR_FAILED_OPERATION;
  }

  SmfImmModifyOperation* immOp = new (std::nothrow) SmfImmModifyOperation();
  if (immOp == NULL) {
    LOG_ER(
        "SmfRollbackData::rollbackModifyOperation, could not create SmfImmModifyOperation");
    return SA_AIS_ERR_FAILED_OPERATION;
  }

  TRACE("Rollback modify object %s, no of attributes %d",
        osaf_extended_name_borrow(dnAttr), noOfAttrValues);

  immOp->SetObjectDn(osaf_extended_name_borrow(dnAttr));
  immOp->SetModificationType("SA_IMM_ATTR_VALUES_REPLACE");

  for (unsigned int index = 0; index < noOfAttrValues; index++) {
    const char* attrValueString =
        immutil_getStringAttr(i_attributes, "smfRollbackAttrValue", index);
    if (attrValueString == NULL) {
      LOG_ER("Could not get smfRollbackAttrValue index %u, no of values %u",
             index, noOfAttrValues);
      delete immOp;
      return SA_AIS_ERR_FAILED_OPERATION;
    }

    TRACE("Rollback modify object attribute value string %s", attrValueString);
    std::string attrValueStr(attrValueString);
    unsigned int colonPos = attrValueStr.find(':');
    unsigned int hashPos = attrValueStr.find('#');
    std::string attrName = attrValueStr.substr(0, colonPos);
    std::string attrType =
        attrValueStr.substr(colonPos + 1, hashPos - colonPos - 1);
    std::string attrValue = attrValueStr.substr(hashPos + 1, std::string::npos);

    TRACE("Rollback modify object attribute name %s, type %s, value '%s'",
          attrName.c_str(), attrType.c_str(), attrValue.c_str());

    immOp->AddOrUpdateAttribute(attrName, attrType, attrValue);
  }

  io_operationList.push_back(immOp);

  return SA_AIS_OK;
}

//================================================================================
// Class SmfRollbackCcb
// Purpose:
// Comments:
//================================================================================

SmfRollbackCcb::SmfRollbackCcb(const std::string& i_dn,
                               SaImmOiHandleT i_oiHandle)
    : m_dn(i_dn), m_dataId(1), m_oiHandle(i_oiHandle) {}

// ------------------------------------------------------------------------------
// ~SmfRollbackCcb()
// ------------------------------------------------------------------------------
SmfRollbackCcb::~SmfRollbackCcb() {
  for (const auto& elem : m_rollbackData) delete (elem);
}

//------------------------------------------------------------------------------
// execute()
//------------------------------------------------------------------------------
SaAisErrorT SmfRollbackCcb::execute() {
  SaAisErrorT result = SA_AIS_OK;
  /*
     1) For each rollback data : call execute
  */

  for (auto& dataElem : m_rollbackData) {
    if ((result = (*dataElem).execute(m_oiHandle)) != SA_AIS_OK) {
      LOG_ER("SmfRollbackCcb::execute, fail to execute rollback data, rc=%s",
             saf_error(result));
      break;
    }
  }

  return result;
}

//------------------------------------------------------------------------------
// rollback()
//------------------------------------------------------------------------------
SaAisErrorT SmfRollbackCcb::rollback() {
  SaAisErrorT result = SA_AIS_OK;

  /*
     1) Search for all rollback data IMM objects below this ccb object
  */

  std::list<std::string> rollbackData;
  SmfImmUtils immUtil;
  (void)immUtil.getChildren(m_dn, rollbackData, SA_IMM_SUBLEVEL,
                            "OpenSafSmfRollbackData");

  if (rollbackData.size() == 0) {
    LOG_NO("SmfRollbackCcb::rollback, no rollback data found for ccb %s",
           m_dn.c_str());
    return SA_AIS_OK;
  }

  TRACE("Rollback %zu operations in CCB %s", rollbackData.size(), m_dn.c_str());

  /* Sort the rollback data list */
  rollbackData.sort();

  /*
     2) For each rollback data object (in reverse order) :
            create a new SmfRollbackData and call rollback
  */

  std::list<SmfImmOperation*> operationList;
  std::list<std::string>::reverse_iterator iter;

  /* Loop through the rollback data list in reverse order */
  for (iter = rollbackData.rbegin(); iter != rollbackData.rend(); ++iter) {
    SmfRollbackData rollbackData(this);
    if ((result = rollbackData.rollback((*iter), operationList)) != SA_AIS_OK) {
      LOG_ER("SmfRollbackCcb::rollback, rollback of %s failed, rc=%s",
             (*iter).c_str(), saf_error(result));
      break;
    }
  }
  /*
     3) Use SmfImmUtils object and call doImmOperations
            with list of IMM operations created by rollback data objects
  */

  if (result == SA_AIS_OK) {
    if ((result = immUtil.doImmOperations(operationList)) != SA_AIS_OK) {
      LOG_ER("Rollback ccb operations failed for %s, rc=%s", m_dn.c_str(),
             saf_error(result));
    }
  }

  /* Remove all operations */
  for (auto& opElem : operationList) {
    delete (opElem);
  }

  return result;
}

//------------------------------------------------------------------------------
// addCcbData()
//------------------------------------------------------------------------------
void SmfRollbackCcb::addCcbData(SmfRollbackData* i_data) {
  i_data->setId(m_dataId++);
  m_rollbackData.push_back(i_data);
}
