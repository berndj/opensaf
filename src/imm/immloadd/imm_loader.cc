/*      -*- OpenSAF  -*-
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

#ifdef HAVE_CONFIG_H
#include "osaf/config.h"
#endif

#include "imm/immloadd/imm_loader.h"
#include "mds/mds_papi.h"
#include <iostream>
#include <set>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <syslog.h>
#include "osaf/configmake.h"
#include "base/logtrace.h"
#include <sys/stat.h>
#include <errno.h>
#include "base/ncsgl_defs.h"

#include <saAis.h>
#include "base/osaf_extended_name.h"

// Default value of accessControlMode attribute in the OpensafImm class
// Can be changed at build time using configure
#ifndef IMM_ACCESS_CONTROL_MODE
#define IMM_ACCESS_CONTROL_MODE ACCESS_CONTROL_DISABLED
#endif

#define MAX_DEPTH 10
#define MAX_CHAR_BUFFER_SIZE 8192  // 8k

static bool opensafClassCreated = false;
static bool opensafObjectCreated = false;
static bool opensafPbeRtClassCreated = false;

static char base64_dec_table[] =
    {
        62,                           /* +                    */
        (char)-1, (char)-1, (char)-1, /* 0x2c-0x2e    */
        63,                           /* /                    */
        52,       53,       54,       55,       56,
        57,       58,       59,       60,       61, /* 0-9                  */
        (char)-1, (char)-1, (char)-1, (char)-1, (char)-1,
        (char)-1, (char)-1, /* 0x3a-0x40    */
        0,        1,        2,        3,        4,
        5,        6,        7,        8,        9,
        10,       11,       12,       13,       14,
        15,       16,       17,       18,       19,
        20,       21,       22,       23,       24,
        25, /* A-Z                  */
        (char)-1, (char)-1, (char)-1, (char)-1, (char)-1,
        (char)-1, /* 0x5b-0x60    */
        26,       27,       28,       29,       30,
        31,       32,       33,       34,       35,
        36,       37,       38,       39,       40,
        41,       42,       43,       44,       45,
        46,       47,       48,       49,       50,
        51, /* a-z                  */
};

/* The possible states of the parser */
typedef enum {
  STARTED,
  DONE,
  CLASS,
  OBJECT,
  DN,
  ATTRIBUTE,
  NAME,
  VALUE,
  CATEGORY,
  RDN,
  DEFAULT_VALUE,
  TYPE,
  FLAG,
  NTFID,
  IMM_CONTENTS
} StatesEnum;

/* The state struct for the parser */
typedef struct ParserStateStruct {
  int immInit;
  int adminInit;
  int ccbInit;

  int doneParsingClasses;
  int depth;
  StatesEnum state[MAX_DEPTH];

  char *className;
  SaImmClassCategoryT classCategory;
  int classCategorySet;

  /* AttrDefinition parameters */
  char *attrName;
  SaImmValueTypeT attrValueType;
  SaImmAttrFlagsT attrFlags;
  SaUint32T attrNtfId;
  char *attrDefaultValueBuffer;

  int attrValueTypeSet;
  int attrNtfIdSet;
  int attrDefaultValueSet;
  int attrValueSet;

  std::list<SaImmAttrDefinitionT_2> attrDefinitions;
  std::map<std::string, std::map<std::string, SaImmValueTypeT> >
      classAttrTypeMap;

  /* Object parameters */
  char *objectClass;
  char *objectName;

  std::list<SaImmAttrValuesT_2> attrValuesList;
  std::list<char *> attrValueBuffers;
  int valueContinue;
  int isBase64Encoded;

  std::map<std::string, SaImmAttrValuesT_2> classRDNMap;

  SaImmHandleT immHandle;
  SaImmAdminOwnerHandleT ownerHandle;
  SaImmCcbHandleT ccbHandle;
  SaUint32T *preloadEpochPtr;
} ParserState;

bool isXsdLoaded;
std::string xsddir;
std::string xsd;
typedef std::set<std::string> AttrFlagSet;
std::set<std::string> runtimeClass;
bool isPersistentRTClass = false;
AttrFlagSet attrFlagSet;

/* Helper functions */

static void addToAttrTypeCache(ParserState *, SaImmValueTypeT);

static SaImmValueTypeT getClassAttrValueType(
    std::map<std::string, std::map<std::string, SaImmValueTypeT> >
        *classAttrTypeMap,
    const char *className, const char *attrName);

static void saveRDNAttribute(ParserState *parserState);

static void getRDNForClass(
    const char *objectName, const SaImmClassNameT className,
    std::map<std::string, SaImmAttrValuesT_2> *classRDNMap,
    SaImmAttrValuesT_2 *values);

static void charsToValueHelper(SaImmAttrValueT *, SaImmValueTypeT,
                               const char *);
static SaImmValueTypeT charsToTypeHelper(const xmlChar *str, size_t len);
static SaImmAttrFlagsT charsToFlagsHelper(const xmlChar *str, size_t len);

/* SAX callback handlers */
static char *getAttributeValue(const char *attr,
                               const xmlChar **const attrArray);

static void errorHandler(void *, const char *, ...);

static void warningHandler(void *, const char *, ...);

static void startElementHandler(void *, const xmlChar *, const xmlChar **);

static void endElementHandler(void *, const xmlChar *);

static void startDocumentHandler(void *userData);

static void endDocumentHandler(void *userData);

static void attributeDeclHandler(void *, const xmlChar *, const xmlChar *, int,
                                 int, const xmlChar *, xmlEnumerationPtr);

static void charactersHandler(void *, const xmlChar *, int);

static xmlEntityPtr getEntityHandler(void *, const xmlChar *);

/* Data declarations */
xmlSAXHandler my_handler = {
    NULL,  //   internalSubsetSAXFunc internalSubset;
    NULL,  //   isStandaloneSAXFunc isStandalone,
    NULL,  //   hasInternalSubsetSAXFunc hasInternalSubset,
    NULL,  //   hasExternalSubsetSAXFunc hasExternalSubset,
    NULL,  //   resolveEntitySAXFunc resolveEntity,
    getEntityHandler,
    NULL,  //   entityDeclSAXFunc entityDecl,
    NULL,  //   notationDeclSAXFunc notationDecl,
    attributeDeclHandler,
    NULL,  //   elementDeclSAXFunc elementDecl,
    NULL,  //   unparsedEntityDeclSAXFunc unparsedEntityDecl,
    NULL,  //   setDocumentLocatorSAXFunc setDocumentLocator,
    startDocumentHandler,
    endDocumentHandler,
    startElementHandler,
    endElementHandler,
    NULL,  //   referenceSAXFunc reference,
    charactersHandler,
    NULL,  //   ignorableWhitespaceSAXFunc ignorableWhitespace,
    NULL,  //   processingInstructionSAXFunc processingInstruction,
    NULL,  //   commentSAXFunc comment,
    warningHandler,
    errorHandler,
    NULL  //   fatalErrorSAXFunc fatalError;
};

/* Function bodies */

static int base64_decode(char *in, char *out) {
  char ch1, ch2, ch3, ch4;
  int len = 0;

  /* skip spaces */
  while (*in == 10 || *in == 13 || *in == 32) in++;

  while (*in && in[3] != '=') {
    if (*in < '+' || *in > 'z' || in[1] < '+' || in[1] > 'z' || in[2] < '+' ||
        in[2] > 'z' || in[3] < '+' || in[3] > 'z')
      return -1;

    ch1 = base64_dec_table[*in - '+'];
    ch2 = base64_dec_table[in[1] - '+'];
    ch3 = base64_dec_table[in[2] - '+'];
    ch4 = base64_dec_table[in[3] - '+'];

    if (((ch1 | ch2 | ch3 | ch4) & 0xc0) > 0) return -1;

    *out = (ch1 << 2) | (ch2 >> 4);
    out[1] = (ch2 << 4) | (ch3 >> 2);
    out[2] = (ch3 << 6) | ch4;

    in += 4;
    out += 3;
    len += 3;

    while (*in == 10 || *in == 13 || *in == 32) in++;
  }

  if (*in && in[3] == '=') {
    if (*in < '+' || *in > 'z' || in[1] < '+' || in[1] > 'z') return -1;

    ch1 = base64_dec_table[*in - '+'];
    ch2 = base64_dec_table[in[1] - '+'];

    if (in[2] == '=') {
      if (((ch1 | ch2) & 0xc0) > 0) return -1;

      *out = (ch1 << 2) | (ch2 >> 4);

      len++;
    } else {
      if (in[2] < '+' || in[2] > 'z') return -1;

      ch3 = base64_dec_table[in[2] - '+'];

      if (((ch1 | ch2 | ch3) & 0xc0) > 0) return -1;

      *out = (ch1 << 2) | (ch2 >> 4);
      out[1] = (ch2 << 4) | (ch3 >> 2);

      len += 2;
    }

    in += 4;

    /* Skip spaces from the end of the string */
    while (*in == 10 || *in == 13 || *in == 32) in++;
  }

  return (*in) ? -1 : len;
}

void opensafClassCreate(SaImmHandleT immHandle) {
  SaAisErrorT err = SA_AIS_OK;
  int retries = 0;
  SaImmAttrDefinitionT_2 d1, d2, d3, d4, d5, d6, d7, d8, d9, d10;
  SaImmAttrDefinitionT_2 d11, d12, d13, d14;
  SaUint32T nost_flags_default = 0;
  SaUint32T batch_size_default = IMMSV_DEFAULT_MAX_SYNC_BATCH_SIZE;
  SaUint32T extended_names_enabled_default = 0;
  SaUint32T access_control_mode_default = IMM_ACCESS_CONTROL_MODE;
  SaUint32T absent_scs_allowed_default = 0;
  SaUint32T max_classes_default = 1000;
  SaUint32T max_implementers_default = 3000;
  SaUint32T max_adminOwners_default = 2000;
  SaUint32T max_ccbs_default = 10000;
  SaUint32T min_applier_timeout = 0;

  d1.attrName = (char *)OPENSAF_IMM_ATTR_RDN;
  d1.attrValueType = SA_IMM_ATTR_SANAMET;
  d1.attrFlags = SA_IMM_ATTR_CONFIG | SA_IMM_ATTR_RDN | SA_IMM_ATTR_INITIALIZED;
  d1.attrDefaultValue = NULL;

  d2.attrName = (char *)OPENSAF_IMM_ATTR_EPOCH;
  d2.attrValueType = SA_IMM_ATTR_SAUINT32T;
  d2.attrFlags =
      SA_IMM_ATTR_RUNTIME | SA_IMM_ATTR_CACHED | SA_IMM_ATTR_PERSISTENT;
  d2.attrDefaultValue = NULL;

  d3.attrName = (char *)OPENSAF_IMM_ATTR_CLASSES;
  d3.attrValueType = SA_IMM_ATTR_SASTRINGT;
  d3.attrFlags = SA_IMM_ATTR_RUNTIME | SA_IMM_ATTR_CACHED |
                 SA_IMM_ATTR_PERSISTENT | SA_IMM_ATTR_MULTI_VALUE;
  d3.attrDefaultValue = NULL;

  d4.attrName = (char *)OPENSAF_IMM_ATTR_NOSTD_FLAGS;
  d4.attrValueType = SA_IMM_ATTR_SAUINT32T;
  d4.attrFlags = SA_IMM_ATTR_RUNTIME | SA_IMM_ATTR_CACHED;
  d4.attrDefaultValue = &nost_flags_default;

  d5.attrName = (char *)OPENSAF_IMM_SYNC_BATCH_SIZE;
  d5.attrValueType = SA_IMM_ATTR_SAUINT32T;
  d5.attrFlags = SA_IMM_ATTR_CONFIG | SA_IMM_ATTR_WRITABLE;
  d5.attrDefaultValue = &batch_size_default;

  d6.attrName = (char *)OPENSAF_IMM_LONG_DNS_ALLOWED;
  d6.attrValueType = SA_IMM_ATTR_SAUINT32T;
  d6.attrFlags = SA_IMM_ATTR_CONFIG | SA_IMM_ATTR_WRITABLE;
  d6.attrDefaultValue = &extended_names_enabled_default;

  d7.attrName = (char *)OPENSAF_IMM_ACCESS_CONTROL_MODE;
  d7.attrValueType = SA_IMM_ATTR_SAUINT32T;
  d7.attrFlags = SA_IMM_ATTR_CONFIG | SA_IMM_ATTR_WRITABLE;
  d7.attrDefaultValue = &access_control_mode_default;

  d8.attrName = (char *)OPENSAF_IMM_AUTHORIZED_GROUP;
  d8.attrValueType = SA_IMM_ATTR_SASTRINGT;
  d8.attrFlags = SA_IMM_ATTR_CONFIG | SA_IMM_ATTR_WRITABLE;
  d8.attrDefaultValue = NULL;

  d9.attrName = (char *)OPENSAF_IMM_SC_ABSENCE_ALLOWED;
  d9.attrValueType = SA_IMM_ATTR_SAUINT32T;
  d9.attrFlags =
      SA_IMM_ATTR_RUNTIME | SA_IMM_ATTR_CACHED | SA_IMM_ATTR_PERSISTENT;
  d9.attrDefaultValue = &absent_scs_allowed_default;

  d10.attrName = (char *)OPENSAF_IMM_MAX_CLASSES;
  d10.attrValueType = SA_IMM_ATTR_SAUINT32T;
  d10.attrFlags = SA_IMM_ATTR_CONFIG | SA_IMM_ATTR_WRITABLE;
  d10.attrDefaultValue = &max_classes_default;

  d11.attrName = (char *)OPENSAF_IMM_MAX_IMPLEMENTERS;
  d11.attrValueType = SA_IMM_ATTR_SAUINT32T;
  d11.attrFlags = SA_IMM_ATTR_CONFIG | SA_IMM_ATTR_WRITABLE;
  d11.attrDefaultValue = &max_implementers_default;

  d12.attrName = (char *)OPENSAF_IMM_MAX_ADMINOWNERS;
  d12.attrValueType = SA_IMM_ATTR_SAUINT32T;
  d12.attrFlags = SA_IMM_ATTR_CONFIG | SA_IMM_ATTR_WRITABLE;
  d12.attrDefaultValue = &max_adminOwners_default;

  d13.attrName = (char *)OPENSAF_IMM_MAX_CCBS;
  d13.attrValueType = SA_IMM_ATTR_SAUINT32T;
  d13.attrFlags = SA_IMM_ATTR_CONFIG | SA_IMM_ATTR_WRITABLE;
  d13.attrDefaultValue = &max_ccbs_default;

  d14.attrName = (char *)OPENSAF_IMM_MIN_APPLIER_TIMEOUT;
  d14.attrValueType = SA_IMM_ATTR_SAUINT32T;
  d14.attrFlags = SA_IMM_ATTR_CONFIG | SA_IMM_ATTR_WRITABLE;
  d14.attrDefaultValue = &min_applier_timeout;

  const SaImmAttrDefinitionT_2 *attrDefs[] = {
      &d1, &d2, &d3, &d4, &d5, &d6, &d7, &d8, &d9, &d10,
      &d11, &d12, &d13, &d14, 0};

  do { /* Create the class */

    if (err == SA_AIS_ERR_TRY_AGAIN) {
      TRACE_8("Got TRY_AGAIN (retries:%u) on class create", retries);
      usleep(200000);
      err = SA_AIS_OK;
    }
    err = saImmOmClassCreate_2(immHandle, (char *)OPENSAF_IMM_CLASS_NAME,
                               SA_IMM_CLASS_CONFIG, attrDefs);

  } while (err == SA_AIS_ERR_TRY_AGAIN && ++retries < 32);

  if (err != SA_AIS_OK) {
    LOG_ER("Failed to create the class %s", OPENSAF_IMM_CLASS_NAME);
    exit(1);
  }
}

bool opensafPbeRtClassCreate(SaImmHandleT immHandle) {
  SaAisErrorT err = SA_AIS_OK;
  int retries = 0;
  SaImmAttrDefinitionT_2 d1, d2, d3, d4;

  d1.attrName = (char *)OPENSAF_IMM_ATTR_PBE_RT_RDN;
  d1.attrValueType = SA_IMM_ATTR_SASTRINGT;
  d1.attrFlags = SA_IMM_ATTR_RUNTIME | SA_IMM_ATTR_RDN | SA_IMM_ATTR_CACHED;
  d1.attrDefaultValue = NULL;

  d2.attrName = (char *)OPENSAF_IMM_ATTR_PBE_RT_EPOCH;
  d2.attrValueType = SA_IMM_ATTR_SAUINT32T;
  d2.attrFlags = SA_IMM_ATTR_RUNTIME;
  d2.attrDefaultValue = NULL;

  d3.attrName = (char *)OPENSAF_IMM_ATTR_PBE_RT_CCB;
  d3.attrValueType = SA_IMM_ATTR_SAUINT64T;
  d3.attrFlags = SA_IMM_ATTR_RUNTIME;
  d3.attrDefaultValue = NULL;

  d4.attrName = (char *)OPENSAF_IMM_ATTR_PBE_RT_TIME;
  d4.attrValueType = SA_IMM_ATTR_SATIMET;
  d4.attrFlags = SA_IMM_ATTR_RUNTIME;
  d4.attrDefaultValue = NULL;

  const SaImmAttrDefinitionT_2 *attrDefs[5] = {&d1, &d2, &d3, &d4, 0};

  do { /* Create the class */

    if (err == SA_AIS_ERR_TRY_AGAIN) {
      TRACE_8("Got TRY_AGAIN (retries:%u) on class create", retries);
      usleep(200000);
      err = SA_AIS_OK;
    }
    err = saImmOmClassCreate_2(immHandle, (char *)OPENSAF_IMM_PBE_RT_CLASS_NAME,
                               SA_IMM_CLASS_RUNTIME, attrDefs);

  } while (err == SA_AIS_ERR_TRY_AGAIN && ++retries < 32);

  if (err != SA_AIS_OK) {
    LOG_WA("Failed to create the class %s err:%u",
           OPENSAF_IMM_PBE_RT_CLASS_NAME, err);
    return false;
  }
  LOG_IN("Class %s created", OPENSAF_IMM_PBE_RT_CLASS_NAME);
  return true;
}

static void opensafObjectCreate(SaImmCcbHandleT ccbHandle) {
  SaAisErrorT err = SA_AIS_OK;
  int retries = 0;
  SaNameT rdn;
  SaNameT parent;
  osaf_extended_name_lend(OPENSAF_IMM_OBJECT_RDN, &rdn);
  void *nameValues[1];
  nameValues[0] = &rdn;

  osaf_extended_name_lend(OPENSAF_IMM_OBJECT_PARENT, &parent);

  SaUint32T epochValue = 1;
  void *intValues[1];
  intValues[0] = &epochValue;

  SaImmAttrValuesT_2 v1, v2;
  v1.attrName = (char *)OPENSAF_IMM_ATTR_RDN;
  v1.attrValueType = SA_IMM_ATTR_SANAMET;
  v1.attrValuesNumber = 1;
  v1.attrValues = nameValues;

  v2.attrName = (char *)OPENSAF_IMM_ATTR_EPOCH;
  v2.attrValueType = SA_IMM_ATTR_SAUINT32T;
  v2.attrValuesNumber = 1;
  v2.attrValues = intValues;

  const SaImmAttrValuesT_2 *attrValues[3] = {&v1, &v2, 0};

  do { /* Create the object */

    if (err == SA_AIS_ERR_TRY_AGAIN) {
      TRACE_8("Got TRY_AGAIN (retries:%u) on class create", retries);
      usleep(200000);
      err = SA_AIS_OK;
    }

    err = saImmOmCcbObjectCreate_2(ccbHandle, (char *)OPENSAF_IMM_CLASS_NAME,
                                   &parent, attrValues);

  } while (err == SA_AIS_ERR_TRY_AGAIN && ++retries < 32);

  if (err != SA_AIS_OK) {
    LOG_ER("Failed to create the object %s", OPENSAF_IMM_OBJECT_DN);
    exit(1);
  }
}

/**
 * Creates an Imm Object through the ImmOm interface
 * Note: classRDNMap is NULL when loading from PBE.
 */
bool createImmObject(SaImmClassNameT className, char *objectName,
                     std::list<SaImmAttrValuesT_2> *attrValuesList,
                     SaImmCcbHandleT ccbHandle,
                     std::map<std::string, SaImmAttrValuesT_2> *classRDNMap,
                     bool *pbeCorrupted) {
  SaNameT parentName;
  SaImmAttrValuesT_2 **attrValues;
  SaAisErrorT errorCode = SA_AIS_OK;
  int i;
  size_t RDNlen;
  bool rc = true;

  TRACE_ENTER2("CREATE IMM OBJECT %s, %s", className, objectName);

  if (pbeCorrupted) *pbeCorrupted = true;
  TRACE("attrValuesList size:%zu clasRDNMap size:%zu", attrValuesList->size(),
        classRDNMap ? classRDNMap->size() : 0);

  /* Set the parent name */
  osaf_extended_name_lend("", &parentName);
  if (objectName != NULL) {
    char *parent;

    /* ',' is the delimeter */
    /* but '\' is the escape character, used for association objects */
    parent = objectName;
    do {
      parent = strchr(parent, ',');
      TRACE_8("PARENT: %s", parent);
    } while (parent && (*((++parent) - 2)) == '\\');

    if (parent && strlen(parent) <= 1) {
      parent = NULL;
    }

    if (parent != NULL) {
      osaf_extended_name_lend(parent, &parentName);
    }
  } else {
    LOG_ER("Empty DN for object");
    TRACE_LEAVE();
    return false;
  }

  /* Get the length of the RDN and truncate objectName */
  if (!osaf_is_extended_name_empty(&parentName)) {
    RDNlen = strlen(objectName) -
             (strlen(osaf_extended_name_borrow(&parentName)) + 1);
    objectName[RDNlen] = '\0';
  } else {
    RDNlen = strlen(objectName);
  }

  TRACE_8("OBJECT RDN: %s", objectName);

  /* Set the attribute values array, add space for the rdn attribute
   * and a NULL terminator */

  /* Freed at the bottom of the function */
  attrValues = (SaImmAttrValuesT_2 **)malloc(
      (attrValuesList->size() + (classRDNMap ? 2 : 1)) *
      sizeof(SaImmAttrValuesT_2 *));
  assert(attrValues);

  /* Add the NULL termination */
  attrValues[attrValuesList->size() + (classRDNMap ? 1 : 0)] =
      NULL; /* Conditionally adjust for RDN */

  std::list<SaImmAttrValuesT_2>::iterator it = attrValuesList->begin();

  i = 0;
  while (it != attrValuesList->end()) {
    attrValues[i] = &(*it);
    i++;
    ++it;
  }

  if (classRDNMap) {
    attrValues[i] = (SaImmAttrValuesT_2 *)malloc(sizeof(SaImmAttrValuesT_2));
    getRDNForClass(objectName, className, classRDNMap, attrValues[i]);
    TRACE("RDN attr value assigned for attrValues index %u", i);
  }

  int retries = 0;
  do { /* Do the object creation */

    if (errorCode == SA_AIS_ERR_TRY_AGAIN) {
      TRACE_8("Got TRY_AGAIN (retries:%u) on object create", retries);
      usleep(200000);
      errorCode = SA_AIS_OK;
    }

    errorCode =
        saImmOmCcbObjectCreate_2(ccbHandle, className, &parentName,
                                 (const SaImmAttrValuesT_2 **)attrValues);

  } while (errorCode == SA_AIS_ERR_TRY_AGAIN && ++retries < 32);

  if (SA_AIS_OK != errorCode) {
    if (pbeCorrupted &&
        ((errorCode != SA_AIS_ERR_INVALID_PARAM &&
          errorCode != SA_AIS_ERR_BAD_OPERATION &&
          errorCode != SA_AIS_ERR_NOT_EXIST && errorCode != SA_AIS_ERR_EXIST &&
          errorCode != SA_AIS_ERR_FAILED_OPERATION &&
          errorCode != SA_AIS_ERR_NAME_TOO_LONG) ||
         (errorCode == SA_AIS_ERR_FAILED_OPERATION &&
          !isValidationAborted(ccbHandle)))) {
      *pbeCorrupted = false;
    }
    LOG_ER(
        "Failed to create object err: %d, class: %s, dn: '%s'. "
        "Check for duplicate attributes, or trace osafimmloadd",
        errorCode, className, objectName);
    rc = false;
    goto freemem;
  }

  if (!opensafObjectCreated &&
      strcmp(className, OPENSAF_IMM_CLASS_NAME) ==
          0) { /*Assuming here that the instance is the one and only correct
                  instance.*/
    opensafObjectCreated = true;
  }

  TRACE_8("CREATE DONE");

freemem:
  /* Free the RDN attrName later since it's re-used */
  /*free(attrValues[i]->attrValues);*/
  free(attrValues[i]);
  free(attrValues);

  for (it = attrValuesList->begin(); it != attrValuesList->end(); ++it) {
    free(it->attrName);
    free(it->attrValues);
  }
  attrValuesList->clear();

  TRACE_LEAVE();

  return rc;
}

/**
 * Creates an ImmClass through the ImmOm interface
 */
bool createImmClass(SaImmHandleT immHandle, const SaImmClassNameT className,
                    SaImmClassCategoryT classCategory,
                    std::list<SaImmAttrDefinitionT_2> *attrDefinitions,
                    bool *pbeCorrupted) {
  SaImmAttrDefinitionT_2 **attrDefinition;
  SaAisErrorT errorCode = SA_AIS_OK;
  int i;
  bool rc = true;

  TRACE_ENTER2("CREATING IMM CLASS %s", className);

  if (pbeCorrupted) *pbeCorrupted = true;
  /* Set the attrDefinition array */
  attrDefinition = (SaImmAttrDefinitionT_2 **)calloc(
      (attrDefinitions->size() + 1), sizeof(SaImmAttrDefinitionT_2 *));
  assert(attrDefinition);

  // attrDefinition[attrDefinitions->size()] = NULL;
  // calloc zeroes.

  std::list<SaImmAttrDefinitionT_2>::iterator it = attrDefinitions->begin();

  i = 0;
  while (it != attrDefinitions->end()) {
    attrDefinition[i] = &(*it);

    i++;
    ++it;
  }

  int retries = 0;
  do { /* Create the class */

    if (errorCode == SA_AIS_ERR_TRY_AGAIN) {
      TRACE_8("Got TRY_AGAIN (retries:%u) on class create", retries);
      usleep(200000);
      errorCode = SA_AIS_OK;
    }
    errorCode =
        saImmOmClassCreate_2(immHandle, className, classCategory,
                             (const SaImmAttrDefinitionT_2 **)attrDefinition);

  } while (errorCode == SA_AIS_ERR_TRY_AGAIN && ++retries < 32);

  if (SA_AIS_OK != errorCode) {
    if (pbeCorrupted && errorCode != SA_AIS_ERR_INVALID_PARAM &&
        errorCode != SA_AIS_ERR_EXIST) {
      *pbeCorrupted = false;
    }
    LOG_ER("FAILED to create IMM class %s, err:%d", className, errorCode);
    rc = false;
    goto freemem;
  }

  if (!opensafClassCreated && strcmp(className, OPENSAF_IMM_CLASS_NAME) == 0) {
    opensafClassCreated = true;
  }

  if (!opensafPbeRtClassCreated &&
      strcmp(className, OPENSAF_IMM_PBE_RT_CLASS_NAME) == 0) {
    opensafPbeRtClassCreated = true;
  }

  TRACE_8("CREATED IMM CLASS %s", className);

freemem:
  /* Free each attrDefinition */
  it = attrDefinitions->begin();

  while (it != attrDefinitions->end()) {
    if (it->attrDefaultValue) {
      TRACE("Freeing default value for attribute %s", it->attrName);
      free(it->attrDefaultValue);
      it->attrDefaultValue = NULL;
    }
    free(it->attrName);
    it->attrName = NULL;
    ++it;
  }

  /* Free the attrDefinition array and empty the list */
  free(attrDefinition);
  attrDefinitions->clear();

  TRACE_LEAVE();
  return rc;
}

/**
 * Sets RDN value for the SaImmAttrValueT struct representing the RDN
 */
static void getRDNForClass(
    const char *objectName, const SaImmClassNameT className,
    std::map<std::string, SaImmAttrValuesT_2> *classRDNMap,
    SaImmAttrValuesT_2 *values) {
  TRACE_ENTER();
  std::string classNameString(className);

  if (classRDNMap->find(classNameString) == classRDNMap->end()) {
    LOG_ER("CLASS %s NOT FOUND", className);
    exit(1);
  }

  *values = (*classRDNMap)[classNameString];

  values->attrValues = (SaImmAttrValueT *)malloc(sizeof(SaImmAttrValueT));

  values->attrValuesNumber = 1;

  charsToValueHelper(values->attrValues, values->attrValueType, objectName);
  TRACE_LEAVE();
}

static void errorHandler(void *userData, const char *format, ...) {
  va_list ap;
  char *errMsg = NULL;
  TRACE("Libxml2 error format: %s", format);
  va_start(ap, format);
  if (*(++format) == 's') {
    errMsg = va_arg(ap, char *);
    LOG_ER("Error occured during XML parsing libxml2: %s", errMsg);
  } else {
    LOG_ER("Error (unknown) occured during XML parsing");
  }
  va_end(ap);
  exit(1);
}

static void warningHandler(void *userData, const char *format, ...) {
  va_list ap;
  char *errMsg = NULL;
  TRACE("Libxml2 warning format: %s", format);
  va_start(ap, format);
  if (*(++format) == 's') {
    errMsg = va_arg(ap, char *);
    LOG_WA("Warning occured during XML parsing libxml2: %s", errMsg);
  } else {
    LOG_WA("Warning (unknonw) occured during XML parsing");
  }
  va_end(ap);
}

static const xmlChar *getAttributeValue(const xmlChar **attrs,
                                        const xmlChar *attr) {
  if (!attrs) return NULL;

  while (*attrs) {
    if (!strcmp((char *)*attrs, (char *)attr)) return (xmlChar *)*(++attrs);

    attrs++;
    attrs++;
  }

  return NULL;
}

static inline bool isBase64Encoded(const xmlChar **attrs) {
  char *encoding;
  bool isB64 = false;

  if ((encoding = (char *)getAttributeValue(attrs, (xmlChar *)"xsi:type")))
    isB64 = !strcmp(encoding, "xs:base64Binary");

  /* This verification has been left for backward compatibility */
  if (!isB64 &&
      (encoding = (char *)getAttributeValue(attrs, (xmlChar *)"encoding")))
    isB64 = !strcmp(encoding, "base64");

  return isB64;
}

/**
 * This is the handler for start tags
 */
static void startElementHandler(void *userData, const xmlChar *name,
                                const xmlChar **attrs) {
  ParserState *state;

  // TRACE_8("TAG %s", name);

  state = (ParserState *)userData;
  state->depth++;

  if (state->depth >= MAX_DEPTH) {
    LOG_ER("The document is too deply nested");
    exit(1);
  }

  /* <class ...> */
  if (strcmp((const char *)name, "class") == 0) {
    char *nameAttr;

    if (state->doneParsingClasses) {
      LOG_ER("CLASS TAG AT INVALID PLACE IN XML");
      exit(1);
    }

    state->state[state->depth] = CLASS;
    nameAttr = getAttributeValue("name", attrs);

    if (nameAttr != NULL) {
      size_t len;

      len = strlen(nameAttr);

      state->className = (char *)malloc(len + 1);
      strcpy(state->className, nameAttr);
      state->className[len] = '\0';

      TRACE_8("CLASS NAME: %s", state->className);
    } else {
      LOG_ER("NAME ATTRIBUTE IS NULL");
      exit(1);
    }
    /* <object ...> */
  } else if (strcmp((const char *)name, "object") == 0) {
    char *classAttr;

    state->attrFlags = 0;

    state->state[state->depth] = OBJECT;

    /* Get the class attribute */
    classAttr = getAttributeValue("class", attrs);

    if (classAttr != NULL) {
      size_t len;

      len = strlen(classAttr);

      state->objectClass = (char *)malloc(len + 1);
      strncpy(state->objectClass, classAttr, len);
      state->objectClass[len] = '\0';

      TRACE_8("OBJECT CLASS NAME: %s", state->objectClass);
    } else {
      LOG_ER("OBJECT %s HAS NO CLASS ATTRIBUTE", state->objectName);
      exit(1);
    }

    /* <dn> */
  } else if (strcmp((const char *)name, "dn") == 0) {
    assert(state->depth > 0);
    assert(state->objectName == NULL);

    if (state->state[state->depth - 1] != OBJECT) {
      LOG_ER("DN not immediately inside an object tag");
      exit(1);
    }

    state->state[state->depth] = DN;
    /* <attr> */
  } else if (strcmp((const char *)name, "attr") == 0) {
    state->state[state->depth] = ATTRIBUTE;

    state->attrFlags = 0x0;
    state->attrName = NULL;
    state->attrDefaultValueBuffer = NULL;

    state->attrValueTypeSet = 0;
    state->attrNtfIdSet = 0;
    state->attrDefaultValueSet = 0;
    /* <name> */
  } else if (strcmp((const char *)name, "name") == 0) {
    assert(state->depth > 0);
    assert(state->attrName == NULL);

    if (state->state[state->depth - 1] != ATTRIBUTE &&
        state->state[state->depth - 1] != RDN) {
      LOG_ER("Name not immediately inside an attribute tag");
      exit(1);
    }

    state->state[state->depth] = NAME;
    /* <value> */
  } else if (strcmp((const char *)name, "value") == 0) {
    state->state[state->depth] = VALUE;
    state->valueContinue = 0;
    state->isBase64Encoded = isBase64Encoded(attrs);
    /* <category> */
  } else if (strcmp((const char *)name, "category") == 0) {
    state->state[state->depth] = CATEGORY;
    /* <rdn> */
  } else if (strcmp((const char *)name, "rdn") == 0) {
    state->attrFlags = SA_IMM_ATTR_RDN;

    state->attrName = NULL;

    state->attrDefaultValueBuffer = NULL;

    state->attrValueTypeSet = 0;
    state->attrDefaultValueSet = 0;
    state->state[state->depth] = RDN;
    /* <default-value> */
  } else if (strcmp((const char *)name, "default-value") == 0) {
    state->state[state->depth] = DEFAULT_VALUE;
    state->isBase64Encoded = isBase64Encoded(attrs);
    /* <type> */
  } else if (strcmp((const char *)name, "type") == 0) {
    state->state[state->depth] = TYPE;
    /* <flag> */
  } else if (strcmp((const char *)name, "flag") == 0) {
    state->state[state->depth] = FLAG;
    /* <ntfid> */
  } else if (strcmp((const char *)name, "ntfid") == 0) {
    state->state[state->depth] = NTFID;
    /* <imm:IMM-contents> */
  } else if (strcmp((const char *)name, "imm:IMM-contents") == 0) {
    state->state[state->depth] = IMM_CONTENTS;
    char *schema = (char *)getAttributeValue(
        attrs, (xmlChar *)"noNamespaceSchemaLocation");
    if (!schema) {
      schema = (char *)getAttributeValue(
          attrs, (xmlChar *)"xsi:noNamespaceSchemaLocation");
    }
    if (schema) {
      xsd = schema;
    }
  } else {
    LOG_ER("UNKNOWN TAG! (%s)", name);
    exit(1);
  }
}

/**
 * Called when an end-tag is reached
 */
static void endElementHandler(void *userData, const xmlChar *name) {
  ParserState *state;

  state = (ParserState *)userData;

  // TRACE_8("END %s", name);

  /* </value> */
  if (strcmp((const char *)name, "value") == 0) {
    if (state->attrValueBuffers.empty()) {
      char *str = (char *)malloc(1);

      str[0] = '\0';
      state->attrValueBuffers.push_front(str);
    } else if (state->isBase64Encoded) {
      char *value = state->attrValueBuffers.front();
      int len = strlen(value);

      /* count the length of the decoded string */
      int newlen = (len / 4) * 3 - (value[len - 1] == '=' ? 1 : 0) -
                   (value[len - 2] == '=' ? 1 : 0);
      char *newvalue = (char *)malloc(newlen + 1);

      newlen = base64_decode(value, newvalue);
      if (newlen == -1) {
        LOG_ER("Failed to decode base64 value attribute %s (value)",
               state->attrName);
        exit(1);
      }
      newvalue[newlen] = 0;

      state->attrValueBuffers.pop_front();
      state->attrValueBuffers.push_front(newvalue);
      free(value);
    }

    state->valueContinue = 0; /* Actually redundant. */
    state->isBase64Encoded = 0;

    /* </default-value> */
  } else if (strcmp((const char *)name, "default-value") == 0) {
    if (state->attrDefaultValueBuffer == NULL || !state->attrDefaultValueSet) {
      state->attrDefaultValueBuffer = (char *)malloc(1);

      state->attrDefaultValueBuffer[0] = '\0';
      state->attrDefaultValueSet = 1;
      TRACE_8("EMPTY DEFAULT-VALUE TAG");
      TRACE_8("Attribute: %s", state->attrName);
    } else if (state->isBase64Encoded) {
      char *value = state->attrDefaultValueBuffer;
      int len = strlen(value);

      int newlen = (len / 4) * 3 - (value[len - 1] == '=' ? 1 : 0) -
                   (value[len - 2] == '=' ? 1 : 0);
      char *newvalue = (char *)malloc(newlen + 1);

      newlen = base64_decode(value, newvalue);
      if (newlen == -1) {
        LOG_ER(
            "Failed to decode base64 default-value attribute %s (default-value)",
            state->attrName);
        exit(1);
      }
      newvalue[newlen] = 0;

      free(state->attrDefaultValueBuffer);
      state->attrDefaultValueBuffer = newvalue;
    }

    state->isBase64Encoded = 0;
    /* </class> */
  } else if (strcmp((const char *)name, "class") == 0) {
    if (state->doneParsingClasses) {
      LOG_ER("INVALID CLASS PLACEMENT");
      exit(1);
    } else if (!(state->preloadEpochPtr)) {
      if (!createImmClass(state->immHandle, state->className,
                          state->classCategory, &(state->attrDefinitions))) {
        LOG_ER("Failed to create class %s - exiting", state->className);
        exit(1);
      }
      /* Insert only runtime persistant class */
      if ((state->classCategory == SA_IMM_CLASS_RUNTIME) &&
          isPersistentRTClass) {
        runtimeClass.insert(state->className);
        isPersistentRTClass = false;
      }
      state->attrFlags = 0;

      state->attrValueTypeSet = 0;
      state->attrNtfIdSet = 0;
      state->attrDefaultValueSet = 0;
    }
    /* </attr> or </rdn> */
  } else if (strcmp((const char *)name, "attr") == 0 ||
             strcmp((const char *)name, "rdn") == 0) {
    if (state->state[state->depth - 1] == CLASS) {
      /* Save the attribute definition in classRDNMap
         if the RDN flag is set
      */
      saveRDNAttribute(state);

      // addClassAttributeDefinition(state);
      assert(state->attrValueTypeSet);
      addClassAttributeDefinition(
          state->attrName, state->attrValueType, state->attrFlags,
          state->attrDefaultValueBuffer, &(state->attrDefinitions));

      if ((state->attrFlags & SA_IMM_ATTR_RDN) &&
          (state->attrFlags & SA_IMM_ATTR_PERSISTENT)) {
        isPersistentRTClass = true;
      }

      /* Free the default value */
      free(state->attrDefaultValueBuffer);
      state->attrDefaultValueBuffer = NULL;
    } else {
      // addObjectAttributeDefinition(state);
      if ((strcmp(state->attrName, "SaImmAttrImplementerName") == 0)) {
        std::set<std::string>::iterator clit =
            runtimeClass.find(state->objectClass);
        if (clit != runtimeClass.end()) {
          addObjectAttributeDefinition(
              state->objectClass, state->attrName, &(state->attrValueBuffers),
              getClassAttrValueType(&(state->classAttrTypeMap),
                                    state->objectClass, state->attrName),
              &(state->attrValuesList));
        }
      } else {
        addObjectAttributeDefinition(
            state->objectClass, state->attrName, &(state->attrValueBuffers),
            getClassAttrValueType(&(state->classAttrTypeMap),
                                  state->objectClass, state->attrName),
            &(state->attrValuesList));
      }
    }
    /* </object> */
  } else if (strcmp((const char *)name, "object") == 0) {
    std::list<SaImmAttrValuesT_2>::iterator it;
    // TRACE_8("END OBJECT");
    if (!state->doneParsingClasses) {
      TRACE_8("CCB INIT");
      SaAisErrorT errorCode;

      state->doneParsingClasses = 1;

      if (!opensafClassCreated && !(state->preloadEpochPtr)) {
        opensafClassCreate(state->immHandle);
        LOG_NO(
            "The class %s has been created since it was missing from the "
            "imm.xml load file",
            OPENSAF_IMM_CLASS_NAME);
      }

      if (!opensafPbeRtClassCreated && !(state->preloadEpochPtr)) {
        if (opensafPbeRtClassCreate(state->immHandle)) {
          LOG_NO(
              "The class %s has been created since it was missing from the "
              "imm.xml load file",
              OPENSAF_IMM_PBE_RT_CLASS_NAME);
        } else {
          LOG_ER("Failed to create the class %s - exiting",
                 OPENSAF_IMM_PBE_RT_CLASS_NAME);
          exit(1);
        }
      }

      /* First time, initialize the imm object api */
      errorCode = saImmOmAdminOwnerInitialize(
          state->immHandle, (char *)"IMMLOADER", SA_FALSE, &state->ownerHandle);
      if (errorCode != SA_AIS_OK) {
        LOG_ER("Failed on saImmOmAdminOwnerInitialize %d", errorCode);
        exit(1);
      }
      state->adminInit = 1;

      if (!state->preloadEpochPtr) {
        /* ... and initialize the imm ccb api  */
        errorCode = saImmOmCcbInitialize(state->ownerHandle,
                                         0
                                         /*(SaImmCcbFlagsT)
                              SA_IMM_CCB_REGISTERED_OI*/,
                                         &state->ccbHandle);

        if (errorCode != SA_AIS_OK) {
          LOG_ER("Failed to initialize ImmOmCcb %d", errorCode);
          exit(1);
        }
        state->ccbInit = 1;
      }
    }

    if (state->preloadEpochPtr) {
      if (strcmp(state->objectName, OPENSAF_IMM_OBJECT_DN) == 0) {
        for (it = state->attrValuesList.begin();
             it != state->attrValuesList.end(); ++it) {
          if (!strcmp(it->attrName, OPENSAF_IMM_ATTR_EPOCH)) {
            osafassert(it->attrValuesNumber == 1);
            osafassert(it->attrValueType == SA_IMM_ATTR_SAUINT32T);
            (*(state->preloadEpochPtr)) = (*((SaUint32T *)it->attrValues[0]));
            LOG_NO("Preload-epoch %u found in XML file:",
                   *(state->preloadEpochPtr));
          }
        }
      }

      for (it = state->attrValuesList.begin();
           it != state->attrValuesList.end(); ++it) {
        free(it->attrName);
        free(it->attrValues);
      }
      state->attrValuesList.clear();
      /* Free used parameters */
      free(state->objectClass);
      state->objectClass = NULL;
      free(state->objectName);
      state->objectName = NULL;

      goto done;
    }

    /* Create the object */
    if (!createImmObject(state->objectClass, state->objectName,
                         &(state->attrValuesList), state->ccbHandle,
                         &(state->classRDNMap))) {
      LOG_NO("Failed to create object - exiting");
      exit(1);
    }

    /* Free used parameters */
    free(state->objectClass);
    state->objectClass = NULL;
    free(state->objectName);
    state->objectName = NULL;
  } else if (strcmp((const char *)name, "imm:IMM-contents") == 0) {
    SaAisErrorT errorCode;

    if (state->preloadEpochPtr) {
      goto done;
    }

    if (!opensafObjectCreated) {
      opensafObjectCreate(state->ccbHandle);
      LOG_NO(
          "The %s object of class %s has been created since it was missing "
          "from the imm.xml load file",
          OPENSAF_IMM_OBJECT_DN, OPENSAF_IMM_CLASS_NAME);
    }

    /* Apply the object creations */
    if (state->ccbInit) {
      errorCode = saImmOmCcbApply(state->ccbHandle);
      if (SA_AIS_OK != errorCode) {
        LOG_ER("Failed to apply object creations %d", errorCode);
        exit(1);
      }
    }

    /* Finalize the ccb connection*/
    if (state->ccbInit) {
      errorCode = saImmOmCcbFinalize(state->ccbHandle);
      if (SA_AIS_OK != errorCode) {
        LOG_WA("Failed to finalize the ccb object connection %d", errorCode);
      } else {
        state->ccbInit = 0;
      }
    }

    /* Finalize the owner connection */
    if (state->adminInit) {
      errorCode = saImmOmAdminOwnerFinalize(state->ownerHandle);
      if (SA_AIS_OK != errorCode) {
        LOG_WA("Failed on saImmOmAdminOwnerFinalize (%d)", errorCode);
      } else {
        state->adminInit = 0;
      }
    }

    /* Finalize the imm connection */
    if (state->immInit) {
      errorCode = saImmOmFinalize(state->immHandle);
      if (SA_AIS_OK != errorCode) {
        LOG_WA("Failed on saImmOmFinalize (%d)", errorCode);
      }

      state->immInit = 0;
    }
  }

done:

  ((ParserState *)userData)->depth--;
}

static void startDocumentHandler(void *userData) {
  ParserState *state;
  state = (ParserState *)userData;

  state->depth = 0;
  state->state[0] = STARTED;
}

static void endDocumentHandler(void *userData) {
  if (((ParserState *)userData)->depth != 0) {
    LOG_ER("Document ends too early\n");
    exit(1);
  }
  TRACE_8("endDocument occured\n");
}

static void attributeDeclHandler(void *ctx, const xmlChar *elem,
                                 const xmlChar *fullname, int type, int def,
                                 const xmlChar *defaultValue,
                                 xmlEnumerationPtr tree) {
  TRACE_8("attributeDecl occured\n");
}

static void charactersHandler(void *userData, const xmlChar *chars, int len) {
  ParserState *state;
  std::list<char *>::iterator it;

  state = (ParserState *)userData;

  if (len > MAX_CHAR_BUFFER_SIZE) {
    LOG_ER("The character field is too big len(%d) > max(%d)", len,
           MAX_CHAR_BUFFER_SIZE);
    exit(1);
  } else if (len < 0) {
    LOG_ER("The character array length is negative %d", len);
    exit(1);
  }

  /* Treat each tag type separately */
  switch (state->state[state->depth]) {
    case IMM_CONTENTS:
      break;
    case STARTED:
      break;
    case DONE:
      break;
    case CLASS:
      break;
    case OBJECT:
      break;
    case ATTRIBUTE:
      break;
    case DN:
      if (len > kOsafMaxDnLength) {
        LOG_ER("DN is too long (%d characters)", len);
        exit(1);
      }

      /* Copy the distinguished name */
      if (state->objectName) {
        state->objectName = (char *)realloc(
            state->objectName, strlen(state->objectName) + len + 1);
        if (state->objectName == NULL) {
          LOG_ER("Failed to realloc state->objectName");
          exit(1);
        }
        strncat(state->objectName, (const char *)chars, (size_t)len);
      } else {
        state->objectName = (char *)malloc((size_t)len + 1);
        if (state->objectName == NULL) {
          LOG_ER("Failed to malloc state->objectName");
          exit(1);
        }
        strncpy(state->objectName, (const char *)chars, (size_t)len);
        state->objectName[len] = '\0';
      }

      break;
    case NAME:
      if (!state->attrName) {
        state->attrName = (char *)malloc((size_t)len + 1);
        if (state->attrName == NULL) {
          LOG_ER("Failed to malloc state->attrName");
          exit(1);
        }

        strncpy(state->attrName, (const char *)chars, (size_t)len);
        state->attrName[len] = '\0';
      } else {
        state->attrName =
            (char *)realloc(state->attrName, strlen(state->attrName) + len + 1);
        if (state->attrName == NULL) {
          LOG_ER("Failed to realloc state->attrName");
          exit(1);
        }

        strncat(state->attrName, (const char *)chars, len);
      }
      break;
    case VALUE:
      if (state->state[state->depth - 1] == ATTRIBUTE) {
        char *str;

        if (!state->valueContinue) {
          /* Start of value, also end of value for 99.999% of cases */
          state->valueContinue = 1;
          str = (char *)malloc((size_t)len + 1);
          if (str == NULL) {
            LOG_ER("Failed to malloc value");
            exit(1);
          }

          strncpy(str, (const char *)chars, (size_t)len);
          str[len] = '\0';

          state->attrValueBuffers.push_front(str);
        } else {
          /* CONTINUATION of CURRENT value, typically only happens for loooong
           * strings. */
          TRACE_8("APPEND TO CURRENT VALUE");

          size_t oldsize = strlen(state->attrValueBuffers.front());
          TRACE_8("APPEND VALUE newsize:%u", (unsigned int)oldsize + len + 1);

          str = (char *)malloc(oldsize + len + 1);
          if (str == NULL) {
            LOG_ER("Failed to malloc value");
            exit(1);
          }
          strncpy(str, state->attrValueBuffers.front(), oldsize + 1);
          TRACE_8("COPIED OLD VALUE %u %s", (unsigned int)oldsize, str);

          strncpy(str + oldsize, (const char *)chars, (size_t)len + 1);
          str[oldsize + len] = '\0';
          TRACE_8("APPENDED NEW VALUE newsize %u %s",
                  (unsigned int)oldsize + len + 1, str);

          /* Remove the old string */
          free(state->attrValueBuffers.front());
          state->attrValueBuffers.pop_front();
          /* state->attrValueBuffers.clear();
             clear not appropriate since we could ALSO have several values!
             We are here only operating on the front value in the list.
          */
          state->attrValueBuffers.push_front(str);
        }
      } else {
        LOG_ER("UNKNOWN PLACEMENT OF VALUE");
        exit(1);
      }
      break;
    case CATEGORY:
      if (state->state[state->depth - 1] == CLASS) {
        SaImmClassCategoryT category;

        /* strlen("SA_CONFIG") == 9, strlen("SA_RUNTIME") == 10 */
        if (len == 9 &&
            strncmp((const char *)chars, "SA_CONFIG", (size_t)len) == 0) {
          category = SA_IMM_CLASS_CONFIG;
        } else if (len == 10 && strncmp((const char *)chars, "SA_RUNTIME",
                                        (size_t)len) == 0) {
          category = SA_IMM_CLASS_RUNTIME;
        } else {
          LOG_ER("Unknown class category");
          exit(1);
        }

        state->classCategorySet = 1;
        state->classCategory = category;
      } else if (state->state[state->depth - 1] == ATTRIBUTE ||
                 state->state[state->depth - 1] == RDN) {
        SaImmAttrFlagsT category;

        /* strlen("SA_CONFIG") == 9, strlen("SA_RUNTIME") == 10 */
        if (len == 9 &&
            strncmp((const char *)chars, "SA_CONFIG", (size_t)len) == 0) {
          category = SA_IMM_ATTR_CONFIG;
        } else if (len == 10 && strncmp((const char *)chars, "SA_RUNTIME",
                                        (size_t)len) == 0) {
          category = SA_IMM_ATTR_RUNTIME;
        } else {
          LOG_ER("Unknown attribute category");
          exit(1);
        }
        state->attrFlags = state->attrFlags | category;
      }

      break;
    case DEFAULT_VALUE:
      if (state->state[state->depth - 1] == ATTRIBUTE) {
        if (state->attrDefaultValueBuffer == NULL) {
          state->attrDefaultValueBuffer = (char *)malloc((size_t)len + 1);
          strncpy(state->attrDefaultValueBuffer, (const char *)chars,
                  (size_t)len);
          state->attrDefaultValueBuffer[len] = '\0';
          state->attrDefaultValueSet = 1;
        } else {
          /* The defaultValueBuffer contains data from previous
           * call for same value */
          assert(state->attrDefaultValueSet);
          int newlen = strlen(state->attrDefaultValueBuffer) + len;
          state->attrDefaultValueBuffer = (char *)realloc(
              (void *)state->attrDefaultValueBuffer, (size_t)newlen + 1);
          strncat(state->attrDefaultValueBuffer, (const char *)chars,
                  (size_t)len);
          state->attrDefaultValueBuffer[newlen] = '\0';
        }
      } else {
        LOG_ER("UNKNOWN PLACEMENT OF DEFAULT VALUE");
        exit(1);
      }
      break;
    case TYPE:
      state->attrValueType = charsToTypeHelper(chars, (size_t)len);
      state->attrValueTypeSet = 1;

      addToAttrTypeCache(state, state->attrValueType);

      break;
    case RDN:

      break;
    case FLAG:
      state->attrFlags =
          state->attrFlags | charsToFlagsHelper(chars, (size_t)len);
      break;
    case NTFID:
      if (atoi((const char *)chars) < 0) {
        LOG_ER("NtfId can not be negative");
        exit(1);
      }

      state->attrNtfId = (SaUint32T)atoi((const char *)chars);
      state->attrNtfIdSet = 1;
      break;
    default:
      LOG_ER("Unknown state");
      exit(1);
  }
}

static xmlEntityPtr getEntityHandler(void *user_data, const xmlChar *name) {
  return xmlGetPredefinedEntity(name);
}

static inline char *getAttrValue(xmlAttributePtr attr) {
  if (!attr || !attr->children) {
    return NULL;
  }

  return (char *)attr->children->content;
}

static bool loadXsd(const char *xsdFile) {
  struct stat st;
  if (stat(xsdFile, &st)) {
    if (errno == ENOENT) {
      LOG_ER("%s does not exist", xsdFile);
    } else {
      LOG_ER("stat of %s return error: %d", xsdFile, errno);
    }

    return false;
  }
  // It should be a file or a directory
  if (!S_ISREG(st.st_mode)) {
    LOG_ER("%s is not a file", xsdFile);
    return false;
  }

  xmlNodePtr xsdDocRoot;
  xmlDocPtr xsdDoc = xmlParseFile(xsdFile);
  if (!xsdDoc) {
    return false;
  }

  bool rc = true;
  xmlXPathContextPtr ctx = xmlXPathNewContext(xsdDoc);
  if (!ctx) {
    rc = false;
    goto freedoc;
  }

  // Add namespace of the first element
  xsdDocRoot = xmlDocGetRootElement(xsdDoc);
  if (xsdDocRoot->ns) {
    ctx->namespaces = (xmlNsPtr *)malloc(sizeof(xmlNsPtr));
    ctx->namespaces[0] = xsdDocRoot->ns;
    ctx->nsNr = 1;
  }

  xmlXPathObjectPtr xpathObj;
  xpathObj = xmlXPathEval((const xmlChar*)"/xs:schema/xs:simpleType[@name=\"attr-flags\"]/xs:restriction/xs:enumeration", ctx);
  if (!xpathObj || !xpathObj->nodesetval) {
    rc = false;
    goto freectx;
  }

  xmlElementPtr element;
  xmlAttributePtr attr;
  char *value;
  int size;

  size = xpathObj->nodesetval->nodeNr;
  for (int i = 0; i < size; i++) {
    value = NULL;
    element = (xmlElementPtr)xpathObj->nodesetval->nodeTab[i];
    attr = element->attributes;
    while (attr) {
      if (!strcmp((char *)attr->name, "value")) {
        value = getAttrValue(attr);
      }

      if (value) {
        break;
      }

      attr = (xmlAttributePtr)attr->next;
    }

    if (value) {
      if (strcmp(value, "SA_RUNTIME") && strcmp(value, "SA_CONFIG") &&
          strcmp(value, "SA_MULTI_VALUE") && strcmp(value, "SA_WRITABLE") &&
          strcmp(value, "SA_INITIALIZED") && strcmp(value, "SA_PERSISTENT") &&
          strcmp(value, "SA_CACHED") && strcmp(value, "SA_NOTIFY") &&
          strcmp(value, "SA_NO_DUPLICATES") &&
          strcmp(value, "SA_NO_DANGLING") && strcmp(value, "SA_DN") &&
          strcmp(value, "SA_DEFAULT_REMOVED") &&
          strcmp(value, "SA_STRONG_DEFAULT")) {
        attrFlagSet.insert(value);
      }
    }
  }

  isXsdLoaded = true;

  xmlXPathFreeObject(xpathObj);
freectx:
  if (ctx->nsNr) {
    free(ctx->namespaces);
  }
  xmlXPathFreeContext(ctx);
freedoc:
  xmlFreeDoc(xsdDoc);

  return rc;
}

/**
 * Takes a string and returns the corresponding flag
 */
static SaImmAttrFlagsT charsToFlagsHelper(const xmlChar *str, size_t len) {
  if (len == strlen("SA_MULTI_VALUE") &&
      strncmp((const char *)str, "SA_MULTI_VALUE", len) == 0) {
    return SA_IMM_ATTR_MULTI_VALUE;
  } else if (len == strlen("SA_RDN") &&
             strncmp((const char *)str, "SA_RDN", len) == 0) {
    return SA_IMM_ATTR_RDN;
  } else if (len == strlen("SA_CONFIG") &&
             strncmp((const char *)str, "SA_CONFIG", len) == 0) {
    return SA_IMM_ATTR_CONFIG;
  } else if (len == strlen("SA_WRITABLE") &&
             strncmp((const char *)str, "SA_WRITABLE", len) == 0) {
    return SA_IMM_ATTR_WRITABLE;
  } else if (len == strlen("SA_INITIALIZED") &&
             strncmp((const char *)str, "SA_INITIALIZED", len) == 0) {
    return SA_IMM_ATTR_INITIALIZED;
  } else if (len == strlen("SA_RUNTIME") &&
             strncmp((const char *)str, "SA_RUNTIME", len) == 0) {
    return SA_IMM_ATTR_RUNTIME;
  } else if (len == strlen("SA_PERSISTENT") &&
             strncmp((const char *)str, "SA_PERSISTENT", len) == 0) {
    return SA_IMM_ATTR_PERSISTENT;
  } else if (len == strlen("SA_CACHED") &&
             strncmp((const char *)str, "SA_CACHED", len) == 0) {
    return SA_IMM_ATTR_CACHED;
  } else if (len == strlen("SA_NOTIFY") &&
             strncmp((const char *)str, "SA_NOTIFY", len) == 0) {
    return SA_IMM_ATTR_NOTIFY;
  } else if (len == strlen("SA_NO_DUPLICATES") &&
             strncmp((const char *)str, "SA_NO_DUPLICATES", len) == 0) {
    return SA_IMM_ATTR_NO_DUPLICATES;
  } else if (len == strlen("SA_NO_DANGLING") &&
             strncmp((const char *)str, "SA_NO_DANGLING", len) == 0) {
    return SA_IMM_ATTR_NO_DANGLING;
  } else if (len == strlen("SA_DN") &&
             strncmp((const char *)str, "SA_DN", len) == 0) {
    return SA_IMM_ATTR_DN;
  } else if (len == strlen("SA_DEFAULT_REMOVED") &&
             strncmp((const char *)str, "SA_DEFAULT_REMOVED", len) == 0) {
    return SA_IMM_ATTR_DEFAULT_REMOVED;
  } else if (len == strlen("SA_STRONG_DEFAULT") &&
             strncmp((const char *)str, "SA_STRONG_DEFAULT", len) == 0) {
    return SA_IMM_ATTR_STRONG_DEFAULT;
  }

  std::string unflag((char *)str, len);
  if (!isXsdLoaded) {
    std::string xsdPath = xsddir;
    if (xsdPath.size() > 0) xsdPath.append("/");
    xsdPath.append(xsd);
    LOG_WA("Found unknown flag (%s). Trying to load a schema %s",
           unflag.c_str(), xsdPath.c_str());
    loadXsd(xsdPath.c_str());
  }

  if (isXsdLoaded) {
    AttrFlagSet::iterator it = attrFlagSet.find(unflag);
    if (it != attrFlagSet.end()) {
      return 0;
    }
  }

  LOG_ER("UNKNOWN FLAGS, %s", unflag.c_str());

  exit(1);
}

/**
 * Takes a string and returns the corresponding valueType
 */
static SaImmValueTypeT charsToTypeHelper(const xmlChar *str, size_t len) {
  if (len == strlen("SA_NAME_T") &&
      strncmp((const char *)str, "SA_NAME_T", len) == 0) {
    return SA_IMM_ATTR_SANAMET;
  } else if (len == strlen("SA_INT32_T") &&
             strncmp((const char *)str, "SA_INT32_T", len) == 0) {
    return SA_IMM_ATTR_SAINT32T;
  } else if (len == strlen("SA_UINT32_T") &&
             strncmp((const char *)str, "SA_UINT32_T", len) == 0) {
    return SA_IMM_ATTR_SAUINT32T;
  } else if (len == strlen("SA_INT64_T") &&
             strncmp((const char *)str, "SA_INT64_T", len) == 0) {
    return SA_IMM_ATTR_SAINT64T;
  } else if (len == strlen("SA_UINT64_T") &&
             strncmp((const char *)str, "SA_UINT64_T", len) == 0) {
    return SA_IMM_ATTR_SAUINT64T;
  } else if (len == strlen("SA_TIME_T") &&
             strncmp((const char *)str, "SA_TIME_T", len) == 0) {
    return SA_IMM_ATTR_SATIMET;
  } else if (len == strlen("SA_FLOAT_T") &&
             strncmp((const char *)str, "SA_FLOAT_T", len) == 0) {
    return SA_IMM_ATTR_SAFLOATT;
  } else if (len == strlen("SA_DOUBLE_T") &&
             strncmp((const char *)str, "SA_DOUBLE_T", len) == 0) {
    return SA_IMM_ATTR_SADOUBLET;
  } else if (len == strlen("SA_STRING_T") &&
             strncmp((const char *)str, "SA_STRING_T", len) == 0) {
    return SA_IMM_ATTR_SASTRINGT;
  } else if (len == strlen("SA_ANY_T") &&
             strncmp((const char *)str, "SA_ANY_T", len) == 0) {
    return SA_IMM_ATTR_SAANYT;
  }

  LOG_ER("UNKNOWN VALUE TYPE, %s", str);

  exit(1);
}

/**
 * Returns the value for a given attribute name in the xml attribute array
 */
static char *getAttributeValue(const char *attr,
                               const xmlChar **const attrArray) {
  int i;

  if (attrArray == NULL) {
    LOG_ER("The document is TOO DEEPLY NESTED");
    exit(1);
  }

  for (i = 0; attrArray != NULL && attrArray[i * 2] != NULL; i++) {
    if (strcmp(attr, (const char *)attrArray[i * 2]) == 0) {
      return (char *)attrArray[i * 2 + 1];
    }
  }

  LOG_WA("RETURNING NULL");
  return NULL;
}

/**
 * Adds an object attr definition to the attrValuesList
 */
void addObjectAttributeDefinition(
    SaImmClassNameT objectClass, SaImmAttrNameT attrName,
    std::list<char *> *attrValueBuffers, SaImmValueTypeT attrType,
    std::list<SaImmAttrValuesT_2> *attrValuesList) {
  std::list<char *>::iterator it;
  SaImmAttrValuesT_2 attrValues;
  int i;
  size_t len;
  TRACE_ENTER2("attrValueBuffers size:%u",
               (unsigned int)attrValueBuffers->size());
  /* The attrName must be set */
  assert(attrName);

  /* The value array can not be empty */
  assert(!attrValueBuffers->empty());

  /* The object class must be set */
  assert(objectClass);

  /* Set the valueType */
  attrValues.attrValueType = attrType;

  TRACE_8("addObjectAttributeDefinition %s, %s, %d", objectClass, attrName,
          attrType);

  /* For each value, convert from char* to SaImmAttrValuesT_2 and
     store an array pointing to all in attrValues */
  attrValues.attrValuesNumber = attrValueBuffers->size();
  attrValues.attrValues = (SaImmAttrValueT *)malloc(
      sizeof(SaImmAttrValuesT_2) * attrValues.attrValuesNumber + 1);

  attrValues.attrValues[attrValues.attrValuesNumber] = NULL;

  it = attrValueBuffers->begin();
  i = 0;
  while (it != attrValueBuffers->end()) {
    charsToValueHelper(&attrValues.attrValues[i], attrValues.attrValueType,
                       *it);
    i++;
    ++it;
  }

  /* Assign the name */
  len = strlen(attrName);
  attrValues.attrName = (char *)malloc(len + 1);
  if (attrValues.attrName == NULL) {
    LOG_ER("Failed to malloc attrValues.attrName");
    exit(1);
  }
  strncpy(attrValues.attrName, attrName, len);
  attrValues.attrName[len] = '\0';

  /* Add attrValues to the list */
  attrValuesList->push_front(attrValues);
  TRACE("Value added size:%u", (unsigned int)attrValuesList->size());

  /* Free unneeded data */
  for (it = attrValueBuffers->begin(); it != attrValueBuffers->end(); ++it) {
    free(*it);
  }

  attrValueBuffers->clear();
  assert(attrValueBuffers->empty());
  TRACE_LEAVE();
}

/**
 * Saves the rdn attribute for a class in a map
 */
static void saveRDNAttribute(ParserState *state) {
  if (state->attrFlags & SA_IMM_ATTR_RDN &&
      state->classRDNMap.find(std::string(state->className)) ==
          state->classRDNMap.end()) {
    SaImmAttrValuesT_2 values;
    size_t len;

    /* Set the RDN name */
    len = strlen(state->attrName);
    values.attrName = (char *)malloc(len + 1);
    if (values.attrName == NULL) {
      LOG_ER("Failed to malloc values.attrName");
      exit(1);
    }

    strncpy(values.attrName, state->attrName, len);
    values.attrName[len] = '\0';

    /* Set the valueType */
    values.attrValueType = state->attrValueType;

    /* Set the number of attr values */
    values.attrValuesNumber = 0; /* will be set to 1 when RDN is created */

    values.attrValues = NULL;

    TRACE_8("ADDED CLASS TO RDN MAP");
    state->classRDNMap[std::string(state->className)] = values;
  }
}

/**
 * Adds the given valueType to the a mapping in the state
 *
 * Mapping: class -> attribute name -> type
 */
static void addToAttrTypeCache(ParserState *state, SaImmValueTypeT valueType) {
  /* className and attrName can not be NULL */
  assert(state->className && state->attrName);

  std::string classString(state->className);
  std::string attrNameString(state->attrName);

  state->classAttrTypeMap[classString][attrNameString] = valueType;
}

/**
 * Returns the valueType for a given state, classname and attribute name
 */
static SaImmValueTypeT getClassAttrValueType(
    std::map<std::string, std::map<std::string, SaImmValueTypeT> >
        *classAttrTypeMap,
    const char *className, const char *attrName) {
  std::string classNameString(className);
  std::string attrNameString(attrName);

  if (classAttrTypeMap->find(classNameString) == classAttrTypeMap->end()) {
    LOG_ER("NO CORRESPONDING CLASS %s", className);
    exit(1);
  }

  if ((*classAttrTypeMap)[classNameString].find(attrNameString) ==
      (*classAttrTypeMap)[classNameString].end()) {
    LOG_ER("NO CORRESPONDING ATTRIBUTE %s in class %s", attrName, className);
    exit(1);
  }

  return (*classAttrTypeMap)[classNameString][attrNameString];
}

/**
 * Adds an class attribute definition to the list
 */
void addClassAttributeDefinition(
    SaImmAttrNameT attrName, SaImmValueTypeT attrValueType,
    SaImmAttrFlagsT attrFlags, SaImmAttrValueT attrDefaultValueBuffer,
    std::list<SaImmAttrDefinitionT_2> *attrDefinitions) {
  SaImmAttrDefinitionT_2 attrDefinition;

  /* Set the name */
  assert(attrName);
  attrDefinition.attrName = attrName;

  /* Set attrValueType */
  attrDefinition.attrValueType = attrValueType;

  /* Set the flags */
  attrDefinition.attrFlags = attrFlags;

  /* Set the default value */
  if (attrDefaultValueBuffer) {
    charsToValueHelper(&attrDefinition.attrDefaultValue, attrValueType,
                       (const char *)attrDefaultValueBuffer);
  } else {
    attrDefinition.attrDefaultValue = NULL;
  }
  /* Add to the list of attrDefinitions */
  attrDefinitions->push_front(attrDefinition);
}

/**
 * Converts an array of chars to an SaImmAttrValueT
 */
static void charsToValueHelper(SaImmAttrValueT *value, SaImmValueTypeT type,
                               const char *str) {
  size_t len;
  unsigned int i;
  char byte[5];
  char *endMark;

  switch (type) {
    case SA_IMM_ATTR_SAINT32T:
      *value = malloc(sizeof(SaInt32T));
      *((SaInt32T *)*value) = (SaInt32T)strtol(str, NULL, 0);
      break;
    case SA_IMM_ATTR_SAUINT32T:
      *value = malloc(sizeof(SaUint32T));
      *((SaUint32T *)*value) = (SaUint32T)strtoul(str, NULL, 0);
      break;
    case SA_IMM_ATTR_SAINT64T:
      *value = malloc(sizeof(SaInt64T));
      *((SaInt64T *)*value) = (SaInt64T)strtoll(str, NULL, 0);
      break;
    case SA_IMM_ATTR_SAUINT64T:
      *value = malloc(sizeof(SaUint64T));
      *((SaUint64T *)*value) = (SaUint64T)strtoull(str, NULL, 0);
      break;
    case SA_IMM_ATTR_SATIMET:  // Int64T
      *value = malloc(sizeof(SaInt64T));
      *((SaTimeT *)*value) = (SaTimeT)strtoll(str, NULL, 0);
      break;
    case SA_IMM_ATTR_SANAMET:
      *value = malloc(sizeof(SaNameT));
      osaf_extended_name_alloc(str, (SaNameT *)*value);
      break;
    case SA_IMM_ATTR_SAFLOATT:
      *value = malloc(sizeof(SaFloatT));
      *((SaFloatT *)*value) = (SaFloatT)atof(str);
      break;
    case SA_IMM_ATTR_SADOUBLET:
      *value = malloc(sizeof(SaDoubleT));
      *((SaDoubleT *)*value) = (SaDoubleT)atof(str);
      break;
    case SA_IMM_ATTR_SASTRINGT:
      len = strlen(str);
      *value = malloc(sizeof(SaStringT));
      *((SaStringT *)*value) = (SaStringT)malloc(len + 1);
      strncpy(*((SaStringT *)*value), str, len);
      (*((SaStringT *)*value))[len] = '\0';
      break;
    case SA_IMM_ATTR_SAANYT:
      len = strlen(str) / 2;
      *value = malloc(sizeof(SaAnyT));
      ((SaAnyT *)*value)->bufferAddr =
          (SaUint8T *)malloc(sizeof(SaUint8T) * len);
      ((SaAnyT *)*value)->bufferSize = len;

      byte[0] = '0';
      byte[1] = 'x';
      byte[4] = '\0';

      endMark = byte + 4;

      for (i = 0; i < len; i++) {
        byte[2] = str[2 * i];
        byte[3] = str[2 * i + 1];

        ((SaAnyT *)*value)->bufferAddr[i] = (SaUint8T)strtod(byte, &endMark);
      }

      TRACE_8("SIZE: %d", (int)((SaAnyT *)*value)->bufferSize);
      break;
    default:
      LOG_ER("BAD VALUE TYPE");
      exit(1);
  }
}

int loadImmXML(std::string xmldir, std::string file,
               SaUint32T *preloadEpochPtr) {
  ParserState state;
  SaVersionT version;
  SaAisErrorT errorCode = SA_AIS_OK;
  std::string filename;
  int error = -1;
  struct stat stat_buf;

  version.releaseCode = 'A';
  version.majorVersion = 2;
  version.minorVersion = 17;

  TRACE("Loading from %s/%s", xmldir.c_str(), file.c_str());

  errorCode = saImmOmInitialize(&(state.immHandle), NULL, &version);
  if (SA_AIS_OK != errorCode) {
    LOG_ER("Failed to initialize the IMM OM interface (%d)", errorCode);
    exit(1);
  }
  state.immInit = 1;

  /* Initialize the state */
  state.attrDefaultValueBuffer = NULL;
  state.objectClass = NULL;
  state.objectName = NULL;
  state.className = NULL;
  state.attrName = NULL;
  state.classCategorySet = 0;
  state.attrValueTypeSet = 0;
  state.attrNtfIdSet = 0;
  state.attrDefaultValueSet = 0;
  state.attrValueSet = 0;
  state.doneParsingClasses = 0;
  state.depth = 0;
  state.attrFlags = 0;

  state.immInit = 0;
  state.adminInit = 0;
  state.ccbInit = 0;
  // state.immHandle = 0LL; state. immHandle initialized above.
  state.ownerHandle = 0LL;
  state.ccbHandle = 0LL;
  state.preloadEpochPtr = preloadEpochPtr;

  isXsdLoaded = false;
  xsddir = xmldir;
  xsd = "";
  attrFlagSet.clear();

  /* Build the filename */
  filename = xmldir;
  filename.append("/");
  filename.append(file);

  if (stat(filename.c_str(), &stat_buf) != 0) {
    LOG_ER("***** FILE:%s IS NOT ACCESSIBLE ***********", filename.c_str());
    goto bailout;
  }

  error = xmlSAXUserParseFile(&my_handler, &state, filename.c_str());

  if (preloadEpochPtr && !error) {
    LOG_IN("Obtained epoch:%u", (*preloadEpochPtr));
    sendPreloadParams(state.immHandle, state.ownerHandle, (*preloadEpochPtr), 0,
                      0, 0LL, 0);
  }

  /* Make sure to finalize the imm connections */
  /* Finalize the ccb connection*/
  if (state.ccbInit) {
    errorCode = saImmOmCcbFinalize(state.ccbHandle);
    if (SA_AIS_OK != errorCode) {
      LOG_WA("Failed to finalize the ccb object connection %d", errorCode);
    }
  }

  /* Finalize the owner connection */
  if (state.adminInit) {
    errorCode = saImmOmAdminOwnerFinalize(state.ownerHandle);
    if (SA_AIS_OK != errorCode) {
      LOG_WA("Failed on saImmOmAdminOwnerFinalize (%d)", errorCode);
    }
  }

  /* Finalize the imm connection */
  if (state.immInit) {
    errorCode = saImmOmFinalize(state.immHandle);
    if (SA_AIS_OK != errorCode) {
      LOG_WA("Failed on saImmOmFinalize (%d)", errorCode);
    }
  }

bailout:
  return !error;
}

int getClassNames(SaImmHandleT &immHandle,
                  std::list<std::string> &classNamesList) {
  // std::list<std::string>::iterator it;
  // SaImmSearchHandleT searchHandle;
  SaImmAccessorHandleT accessorHandle;
  SaImmAttrValuesT_2 **attributes;
  SaImmAttrValuesT_2 **p;
  SaNameT tspSaObjectName;
  SaAisErrorT err = SA_AIS_OK;

  if (saImmOmAccessorInitialize(immHandle, &accessorHandle) != SA_AIS_OK) {
    LOG_ER("saImmOmAccessorInitialize failed");
    exit(1);
  }

  osaf_extended_name_lend(OPENSAF_IMM_OBJECT_DN, &tspSaObjectName);

  SaImmAttrNameT attNames[2] = {(char *)OPENSAF_IMM_ATTR_CLASSES, 0};

  err = saImmOmAccessorGet_2(accessorHandle, &tspSaObjectName, attNames,
                             &attributes);
  if (err != SA_AIS_OK) {
    if (err == SA_AIS_ERR_NOT_EXIST) {
      LOG_ER("The IMM meta-object (%s) does not exist!", OPENSAF_IMM_OBJECT_DN);
    } else {
      LOG_ER("Failed to fetch the IMM meta-object (%s) error code:%u",
             OPENSAF_IMM_OBJECT_DN, err);
    }
    exit(1);
  }

  /* Find the classes attribute */
  for (p = attributes; ((p != NULL) && (*p != NULL)); p++) {
    if (strcmp((*p)->attrName, OPENSAF_IMM_ATTR_CLASSES) == 0) {
      attributes = p;
      break;
    }
  }

  if (p == NULL) {
    LOG_ER("Failed to find the %s attribute", OPENSAF_IMM_ATTR_CLASSES);
    exit(1);
  }

  /* Iterate through the class names */
  for (SaUint32T i = 0; i < (*attributes)->attrValuesNumber; i++) {
    if ((*attributes)->attrValueType == SA_IMM_ATTR_SASTRINGT) {
      std::string classNameString =
          std::string(*(SaStringT *)(*attributes)->attrValues[i]);

      classNamesList.push_front(classNameString);
    } else if ((*attributes)->attrValueType == SA_IMM_ATTR_SANAMET) {
      std::string classNameString(
          osaf_extended_name_borrow((SaNameT *)(*attributes)->attrValues + i));

      classNamesList.push_front(classNameString);
    } else {
      LOG_ER("Invalid type for attribute %s in class %s",
             OPENSAF_IMM_ATTR_CLASSES, OPENSAF_IMM_CLASS_NAME);
      exit(1);
    }
  }

  saImmOmAccessorFinalize(accessorHandle);

  return classNamesList.size();
}

void syncClassDescription(std::string className, SaImmHandleT &immHandle) {
  SaImmClassCategoryT classCategory;
  SaImmAttrDefinitionT_2 **attrDefinitions;
  TRACE("Syncing class description for %s", className.c_str());

  const SaImmClassNameT cln = (char *)className.c_str();

  SaAisErrorT err = saImmOmClassDescriptionGet_2(immHandle, cln, &classCategory,
                                                 &attrDefinitions);
  TRACE_8("Back from classDescriptionGet_2 for %s ", cln);

  if (err != SA_AIS_OK) {
    LOG_ER("Failed to fetch class description for %s, error code:%u", cln, err);
    exit(1);
  }

  int retries = 0;
  do {
    if (retries) {
      usleep(200000);
      TRACE("Retry %u", retries);
    }
    err =
        saImmOmClassCreate_2(immHandle, cln, classCategory,
                             (const SaImmAttrDefinitionT_2 **)attrDefinitions);
  } while (err == SA_AIS_ERR_TRY_AGAIN && (++retries < 32));

  if (err != SA_AIS_OK) {
    LOG_ER("Failed to sync-create class %s, error code:%u", cln, err);
    exit(1);
  }

  saImmOmClassDescriptionMemoryFree_2(immHandle, attrDefinitions);
}

int syncObjectsOfClass(std::string className, SaImmHandleT &immHandle,
                       int maxBatchSize) {
  SaImmClassCategoryT classCategory;
  SaImmAttrDefinitionT_2 **attrDefinitions;
  TRACE("Syncing instances of class %s", className.c_str());

  if (maxBatchSize <= 0) {
    maxBatchSize = IMMSV_DEFAULT_MAX_SYNC_BATCH_SIZE;
  }

  TRACE("maxBatchSize set to %d", maxBatchSize);

  const SaImmClassNameT cln = (char *)className.c_str();

  SaAisErrorT err = saImmOmClassDescriptionGet_2(immHandle, cln, &classCategory,
                                                 &attrDefinitions);
  if (err != SA_AIS_OK) {
    LOG_ER("Failed to fetch class description for %s, error code:%u", cln, err);
    exit(1);
  }

  // Determine how many attributes the class defines.
  int nrofAtts;
  for (nrofAtts = 0; attrDefinitions[nrofAtts]; ++nrofAtts) {
    if (nrofAtts > 999) {
      LOG_ER("Class apparently has too many attributes");
      exit(1);
    }
  }

  if (!nrofAtts) {  // Classes with no attributes are not possible
    LOG_ER("Class apparently has NO attributes ?");
    exit(1);
  }

  SaImmAttrNameT *attrNames = (SaImmAttrNameT *)calloc(
      nrofAtts + 1, sizeof(SaImmAttrNameT));  // one added for null-term

  // Then pick all attributes except runtimes that are neither cached
  // nor persistent. These "pure" runtime atts will not be synced.
  SaImmAttrDefinitionT_2 *attd;
  int ix = 0;
  for (nrofAtts = 0; attrDefinitions[nrofAtts]; ++nrofAtts) {
    attd = attrDefinitions[nrofAtts];
    if ((attd->attrFlags & SA_IMM_ATTR_RUNTIME) &&
        !(attd->attrFlags & SA_IMM_ATTR_PERSISTENT) &&
        !(attd->attrFlags & SA_IMM_ATTR_CACHED)) {
      continue;
    }
    attrNames[ix++] = attd->attrName;
  }

  SaImmSearchParametersT_2 param;  // Filter objects on class-name attribute.

  param.searchOneAttr.attrName = (char *)SA_IMM_ATTR_CLASS_NAME;
  param.searchOneAttr.attrValueType = SA_IMM_ATTR_SASTRINGT;
  param.searchOneAttr.attrValue = (void *)&cln;  // Pointer TO char*

  SaImmSearchHandleT searchHandle;
  unsigned int retries = 0;
  do {
    if (retries) {
      usleep(200000);
    }
    err = saImmOmSearchInitialize_2(
        immHandle,
        NULL,  // rootname
        SA_IMM_SUBTREE,
        (SaImmSearchOptionsT)(SA_IMM_SEARCH_ONE_ATTR |
                              SA_IMM_SEARCH_GET_SOME_ATTR |
                              SA_IMM_SEARCH_SYNC_CACHED_ATTRS),
        &param, attrNames, &searchHandle);
    retries++;
  } while (err == SA_AIS_ERR_TRY_AGAIN && (retries < 32));

  free(attrNames);
  attrNames = NULL;
  if (err != SA_AIS_OK) {
    LOG_ER(
        "Failed to initialize search for instances "
        "of class %s, error code:%u",
        cln, err);
    exit(1);
  }

  err = saImmOmClassDescriptionMemoryFree_2(immHandle, attrDefinitions);
  if (err != SA_AIS_OK) {
    LOG_ER("Failed to release description of class %s, error code:%u", cln,
           err);
    exit(1);
  }

  // Iterate over objects
  SaNameT objectName;
  osaf_extended_name_clear(&objectName);
  SaImmAttrValuesT_2 **attributes = NULL;
  bool message_buffered = false;
  void *batch = NULL;
  int remainingSpace = maxBatchSize;
  int objsInBatch = 0;

  int nrofObjs = 0;
  while (err == SA_AIS_OK) {
    int retries = 0;
    useconds_t usec = 10000;

    do {
      if (retries) {
        /* TRY_AGAIN while sync is in progress means *this* IMMND most likely
           has reached IMMSV_DEFAULT_FEVS_MAX_PENDING. This means that *this*
           IMMND has sent its quota of fevs messages to IMMD without having
           received them back via broadcast from IMMD.

           Thus fevs_replies_pending will be hit in the case of the messages
           accumulating either in local send queues at the MDS/TIPC/TCP level;
           OR at the IMMDs receivingg queue; OR at this IMMNDs MDS/TIPC_/TCP
           receiving queue (when the fevs message comes back). The most likely
           case is the IMMD receive buffers.

           IMMD is in general the fevs bottleneck in the system. This since it
           is one process serving an open ended number of IMMND clients. The
           larger the cluster the higher the risk of hitting fevs_max_pending at
           IMMNDs.
        */
        usleep(usec);
        if (usec < 500000) {
          usec = usec * 2;
        } /* Increase wait time exponentially up to 0.5 seconds */
      }
      /* Synchronous for throttling sync */
      err = saImmOmSearchNext_2(searchHandle, &objectName, &attributes);

      retries++;
    } while (err == SA_AIS_ERR_TRY_AGAIN && (++retries < 320));

    if (err == SA_AIS_ERR_NOT_EXIST) {
      goto done;
    }

    if (err == SA_AIS_ERR_TRY_AGAIN) {
      LOG_ER("Too many TRY_AGAIN on saImmOmSearchNext - aborting");
      exit(1);
    }

    if (err != SA_AIS_OK) {
      LOG_ER("Error %u from saImmOmSearchNext - aborting", err);
      exit(1);
    }

    size_t objectNameLength = strlen(osaf_extended_name_borrow(&objectName));
    if (objectNameLength <= 1) {
      LOG_ER("syncObjectsOfClass: objectName.length <= 1");
      exit(1);
    }

    if (objectNameLength > kOsafMaxDnLength) {
      LOG_ER("syncObjectsOfClass: objectName.length(%zu) > %zu",
             objectNameLength, static_cast<size_t>(kOsafMaxDnLength));
      exit(1);
    }

    ++nrofObjs;
    ++objsInBatch;
    /* Asyncronous */
    err = immsv_sync(immHandle, cln, &objectName,
                     (const SaImmAttrValuesT_2 **)attributes, &batch,
                     &remainingSpace, objsInBatch);

    if (err == SA_AIS_OK) {
      TRACE("SA_AIS_OK => sync sent message");
      message_buffered = false;
      remainingSpace = maxBatchSize;
      objsInBatch = 0;
    } else if (err == SA_AIS_ERR_NOT_READY) {
      TRACE("SA_AIS_ERR_NOT_READY => BUFFERED");
      message_buffered = true;
      err = SA_AIS_OK;
    } else {
      LOG_ER("Object sync failed with error error:%u", err);
      exit(1);
    }

    TRACE("Synced object: %s", osaf_extended_name_borrow(&objectName));

    attributes = NULL;
    osaf_extended_name_clear(&objectName);
  }

done:

  if (err != SA_AIS_ERR_NOT_EXIST) {
    LOG_ER("Sync failed with error retries:%u error:%u", retries, err);
    exit(1);
  }

  if (message_buffered) {
    TRACE(
        "SA_AIS_ERR_NOT_EXIST => BUFFERED & end of iter => send unfilled batch");
    err = immsv_sync(immHandle, cln, &objectName, NULL, &batch, &remainingSpace,
                     objsInBatch);
    if (err != SA_AIS_OK) {
      LOG_ER("Object sync failed with error error:%u", err);
      exit(1);
    }
  }

  saImmOmSearchFinalize(searchHandle);
  return nrofObjs;
}

/* 1=>OK, 0=>FAIL */
int immsync(int maxBatchSize) {
  std::list<std::string> classNamesList;
  std::list<std::string>::iterator it;
  SaVersionT version;
  SaAisErrorT errorCode;
  SaImmHandleT immHandle;

  version.releaseCode = 'A';
  version.majorVersion = 2;
  version.minorVersion = 17;

  int retries = 0;
  do {
    errorCode = saImmOmInitialize(&immHandle, NULL, &version);
    if (retries) {
      usleep(100000);
      TRACE_8("IMM-SYNC initialize retry %u", retries);
    }
  } while ((errorCode == SA_AIS_ERR_TRY_AGAIN) && (++retries < 32));

  if (SA_AIS_OK != errorCode) {
    LOG_ER("Failed to initialize the IMM OM interface (%d)", errorCode);
    return 0;
  }

  int nrofClasses = getClassNames(immHandle, classNamesList);
  if (nrofClasses <= 0) {
    LOG_ER("No classes ??");
    return 0;
  }

  it = classNamesList.begin();

  while (it != classNamesList.end()) {
    syncClassDescription(*it, immHandle);
    ++it;
  }
  TRACE("Sync'ed %u class-descriptions", nrofClasses);

  it = classNamesList.begin();

  int nrofObjects = 0;
  while (it != classNamesList.end()) {
    int objects = syncObjectsOfClass(*it, immHandle, maxBatchSize);
    TRACE("Synced %u objects of class %s", objects, (*it).c_str());
    nrofObjects += objects;
    ++it;
  }
  LOG_IN("Synced %u objects in total", nrofObjects);

  retries = 0;
  do {
    errorCode = immsv_finalize_sync(immHandle);
    if (retries) {
      usleep(100000);
      TRACE_8("IMM-SYNC FINALIZE RETRY %u", retries);
    }
  } while ((errorCode == SA_AIS_ERR_TRY_AGAIN) && (++retries < 100));

  if (SA_AIS_OK != errorCode) {
    LOG_ER("immsv_finalize_sync failed!");
    return 0;
  }

  saImmOmFinalize(immHandle);
  return 1;
}

/**
 * immloader main entry point.
 *
 * @param argc
 * @param argv
 *
 * @return
 */
int main(int argc, char *argv[]) {
  bool preload = false;
  SaUint32T preloadEpoch = 0;
  const char *defaultLog = "osafimmnd";
  const char *logPath;
  unsigned int category_mask = 0;
  void *pbeHandle = NULL;
  const char *pbe_file = getenv("IMMSV_PBE_FILE");
  const char *pbe_file_suffix = getenv("IMMSV_PBE_FILE_SUFFIX");
  std::string pbeFile;
  bool pbeCorrupted = false;
  if (pbe_file) {
    pbeFile.append(pbe_file);
  }
  if (pbe_file_suffix) {
    pbeFile.append(pbe_file_suffix);
  }

  if (argc < 3) {
    syslog(LOG_ERR, "Too few arguments. Please provide a path and a file");
    exit(1);
  }

  if ((logPath = getenv("IMMSV_TRACE_FILENAME"))) {
    category_mask = 0xffffffff; /* TODO: set using env variable ? */
  } else {
    logPath = defaultLog;
  }

  std::string xmldir(argv[1]);
  std::string xml_file(argv[2]);

  if (argc > 3) {
    std::string syncMarker("sync");
    if (syncMarker == std::string(argv[3])) {
      int maxBatchSize = 0;
      if (logtrace_init("immsync", logPath, category_mask) == -1) {
        syslog(LOG_ERR, "logtrace_init FAILED");
        /* We allow the sync to execute anyway. */
      }

      if (argc > 4) {
        maxBatchSize = (unsigned int)strtoul(argv[4], NULL, 0);
        TRACE("maxBatchSize:%u %s", maxBatchSize, argv[4]);
      }

      LOG_NO("Sync starting");
      if (immsync(maxBatchSize)) {
        LOG_NO("Sync ending normally");
        exit(0);
      } else {
        LOG_ER("Sync ending ABNORMALLY");
        exit(1);
      }
    }

    std::string preloadMarker("preload");
    if (preloadMarker == std::string(argv[3])) {
      preload = true;
    }
  }

  if (preload) {
    if (logtrace_init("preload", logPath, category_mask) == -1) {
      syslog(LOG_ERR, "preload: logtrace_init FAILED");
    }
    LOG_NO("2PBE pre-load starting");
  } else {
    if (logtrace_init("osafimmloadd", logPath, category_mask) == -1) {
      syslog(LOG_ERR, "osafimmloadd: logtrace_init FAILED");
    }
    LOG_NO("Load starting");
  }

  if (pbe_file != NULL) {
    LOG_NO(
        "IMMSV_PBE_FILE is defined (%s) check it for existence "
        "and SaImmRepositoryInitModeT",
        pbeFile.c_str());
    pbeHandle = checkPbeRepositoryInit(xmldir, pbeFile);

    if (!pbeHandle) {
      LOG_WA("Could not open repository:%s", pbeFile.c_str());
      if (pbe_file_suffix) {
        LOG_NO("Trying without suffix");
        pbeFile = std::string(pbe_file);
        pbeHandle = checkPbeRepositoryInit(xmldir, pbeFile);
      }
    }
  }

  if (pbeHandle) {
    if (preload) {
      LOG_NO("2PBE Pre-loading from PBE file %s at %s", pbeFile.c_str(),
             argv[1]);
    } else {
      LOG_NO("***** Loading from PBE file %s at %s *****", pbeFile.c_str(),
             argv[1]);
    }

    if (!loadImmFromPbe(pbeHandle, preload, &pbeCorrupted)) {
      LOG_ER("Load from PBE ending ABNORMALLY dir:%s file:%s", argv[1],
             pbeFile.c_str());
      /* Try to prevent cyclic restart. Escalation will only work if
         xmldir is writable.
      */
      if (!preload &&
          pbeCorrupted) { /* Should perhaps escalate also if preload or return
                             <0, 0, 0>*/
        escalatePbe(xmldir, pbeFile);
      }
      goto err;
    }
  } else {
    if (preload) {
      LOG_NO("2PBE: Pre-loading from XML file %s at %s", argv[2], argv[1]);
      if (!loadImmXML(xmldir, xml_file, &preloadEpoch)) {
        LOG_ER("Load from imm.xml file ending ABNORMALLY dir:%s file:%s",
               argv[1], argv[2]);
        goto err;
      }

      LOG_NO("2PBE preload completed epoch: %u", preloadEpoch);
    } else {
      LOG_NO("***** Loading from XML file %s at %s *****", argv[2], argv[1]);
      if (!loadImmXML(xmldir, xml_file, NULL)) {
        LOG_ER("Load from imm.xml file ending ABNORMALLY dir:%s file:%s",
               argv[1], argv[2]);
        goto err;
      }
    }
  }

  if (preload) {
    LOG_IN("Pre-load ending normally");
  } else {
    LOG_NO("Load ending normally");
  }
  return 0;

err:
  exit(1);
  return 0;
}

void sendPreloadParams(SaImmHandleT immHandle,
                       SaImmAdminOwnerHandleT ownerHandle, SaUint32T epoch,
                       SaUint32T maxCcbId, SaUint32T maxCommitTime,
                       SaUint64T maxWeakCcbId, SaUint32T maxWeakCommitTime) {
  SaStringT epoch_string = (SaStringT) "epoch";
  SaStringT commit_time_string = (SaStringT) "commit_time";
  SaStringT ccb_id_string = (SaStringT) "ccb_id";
  SaStringT weak_commit_time_string = (SaStringT) "weak_commit_time";
  SaStringT weak_ccb_id_string = (SaStringT) "weak_ccb_id";
  SaNameT objectName;
  osaf_extended_name_lend(OPENSAF_IMM_OBJECT_DN, &objectName);
  SaAisErrorT operationReturnValue = SA_AIS_OK;
  SaAisErrorT errorCode = SA_AIS_OK;

  if (epoch == 0) {
    epoch = 1;
  } /* Set epoch to one to avoid confusion with no epoch found */

  const SaImmAdminOperationParamsT_2 param0 = {epoch_string,
                                               SA_IMM_ATTR_SAUINT32T, &epoch};

  const SaImmAdminOperationParamsT_2 param1 = {
      commit_time_string, SA_IMM_ATTR_SAUINT32T, &maxCommitTime};

  const SaImmAdminOperationParamsT_2 param2 = {
      ccb_id_string, SA_IMM_ATTR_SAUINT32T, &maxCcbId};

  const SaImmAdminOperationParamsT_2 param3 = {
      weak_commit_time_string, SA_IMM_ATTR_SAUINT32T, &maxWeakCommitTime};

  const SaImmAdminOperationParamsT_2 param4 = {
      weak_ccb_id_string, SA_IMM_ATTR_SAUINT64T, &maxWeakCcbId};

  const SaImmAdminOperationParamsT_2 param5 = {
      (char *)"Dummy", SA_IMM_ATTR_SAUINT64T, &maxWeakCcbId};

  const SaImmAdminOperationParamsT_2 *params[] = {
      &param0, &param1, &param2, &param3, &param4, &param5, NULL};

  errorCode = saImmOmAdminOperationInvoke_2(
      ownerHandle, &objectName, 0, OPENSAF_IMM_2PBE_PRELOAD_STAT, params,
      &operationReturnValue, SA_TIME_ONE_SECOND * 5);

  /* TRY AGAIN handling ? Probably not, imm is not even up yet. */

  if (errorCode != SA_AIS_OK) {
    LOG_WA("Error: %u from immsv on admin-op 2PBE_PRELOAD_STAT", errorCode);
  }

  if (operationReturnValue != SA_AIS_OK) {
    LOG_WA("Error: %u returned from admin-op 2PBE_PRELOAD_STAT",
           operationReturnValue);
  }
}
