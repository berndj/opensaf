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
 * Author(s): Emerson Network Power
 *
 */

/*****************************************************************************
..............................................................................

..............................................................................

  DESCRIPTION:

  This file contains the prototypes for utility operations.

******************************************************************************
*/

#ifndef AMF_COMMON_AMF_UTIL_H_
#define AMF_COMMON_AMF_UTIL_H_

#include <saAmf.h>
#include "base/ncsgl_defs.h"
#include "amf/common/amf_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IS_COMP_SAAWARE(category) (((category)&SA_AMF_COMP_SA_AWARE))

#define IS_COMP_PROXY(category) (((category)&SA_AMF_COMP_PROXY))

#define IS_COMP_PROXIED(category) (((category)&SA_AMF_COMP_PROXIED))

#define IS_COMP_PROXIED_PI(category) \
  ((category) == (SA_AMF_COMP_LOCAL | SA_AMF_COMP_PROXIED))

#define IS_COMP_PROXIED_NPI(category) (((category)&SA_AMF_COMP_PROXIED_NPI))

#define IS_COMP_LOCAL(category) \
  (((category)&SA_AMF_COMP_SA_AWARE) || ((category)&SA_AMF_COMP_LOCAL))

#define IS_COMP_CONTAINER(category) (((category)&SA_AMF_COMP_CONTAINER))

#define IS_COMP_CONTAINED(category) (((category)&SA_AMF_COMP_CONTAINED))

/* macro to determine if name is null */
#define m_AVSV_SA_NAME_IS_NULL(n) avsv_sa_name_is_null(&(n))

extern int avsv_cmp_horder_sanamet(const SaNameT *, const SaNameT *);
extern unsigned int avsv_dblist_uns32_cmp(unsigned char *, unsigned char *);
extern unsigned int avsv_dblist_uns64_cmp(unsigned char *, unsigned char *);
extern unsigned int avsv_dblist_saname_net_cmp(unsigned char *,
                                               unsigned char *);
extern unsigned int avsv_dblist_saname_cmp(unsigned char *, unsigned char *);
extern unsigned int avsv_dblist_sahckey_cmp(unsigned char *, unsigned char *);
extern unsigned int avsv_dblist_sastring_cmp(unsigned char *, unsigned char *);

extern bool avsv_sa_name_is_null(SaNameT *);

extern void avsv_create_association_class_dn(const SaNameT *child_dn,
                                             const SaNameT *parent_dn,
                                             const char *rdn_tag, SaNameT *dn);
extern void avsv_sanamet_init_from_association_dn(const SaNameT *haystack,
                                                  SaNameT *dn,
                                                  const char *needle,
                                                  const char *parent);
extern AVSV_COMP_TYPE_VAL avsv_amfcompcategory_to_avsvcomptype(
    SaAmfCompCategoryT saf_comp_category);

#ifdef __cplusplus
}
#endif

#endif  // AMF_COMMON_AMF_UTIL_H_
