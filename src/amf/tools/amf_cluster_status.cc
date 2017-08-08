/*      -*- OpenSAF  -*-
 *
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
 */

#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <getopt.h>

#include "mds/mds_papi.h"
#include "base/osaf_time.h"
#include "base/saf_error.h"
#include "base/osaf_extended_name.h"
#include "base/ncs_main_papi.h"
#include <saImmOm.h>
#include "osaf/immutil/immutil.h"

enum { AMF_CLUSTER_MDS_VERSION = 1, AMF_CLUSTER_MDS_SVC_ID = 501 };
class CLM_NODE {
 public:
  std::string clm_rdn;
  uint32_t saClmNodeIsMember;
  uint32_t saClmNodeID;
  CLM_NODE() : clm_rdn(""), saClmNodeIsMember(0), saClmNodeID(0) {}
};

class AMF_NODE {
 public:
  std::string amf_rdn;
  std::string clm_rdn;
  AMF_NODE() : amf_rdn(""), clm_rdn("") {}
};
static SaVersionT immVersion = {'A', 2, 1};
static MDS_HDL mds_hdl;
static bool is_avd_up = false;
static std::vector<NODE_ID> avnd_up_db;
static std::map<std::string, CLM_NODE> clm_db;
static std::map<std::string, AMF_NODE> amf_db;
static uint32_t width = strlen("NODE(AMF)");
static NODE_ID scs_node_id[2];
static uint32_t scs_count;
static bool print_also = true;

uint32_t mds_callback(struct ncsmds_callback_info *info) {
  uint32_t rc = NCSCC_RC_SUCCESS;
  if (info->i_op != MDS_CALLBACK_SVC_EVENT) {
    rc = NCSCC_RC_FAILURE;
    goto done;
  }
  if (info->info.svc_evt.i_your_id != AMF_CLUSTER_MDS_SVC_ID) {
    std::cerr << "Not my service id :" << info->info.svc_evt.i_your_id
              << std::endl;
    rc = NCSCC_RC_FAILURE;
    goto done;
  }

  if (info->info.svc_evt.i_change == NCSMDS_UP) {
    if (info->info.svc_evt.i_svc_id == NCSMDS_SVC_ID_AVD) {
      if (m_MDS_DEST_IS_AN_ADEST(info->info.svc_evt.i_dest)) {
        is_avd_up = true;
        if (scs_count < 2)
          scs_node_id[scs_count++] = info->info.svc_evt.i_node_id;
      }
    } else if (info->info.svc_evt.i_svc_id == NCSMDS_SVC_ID_AVND) {
      avnd_up_db.push_back(info->info.svc_evt.i_node_id);
    }
  }
done:
  return rc;
}

static uint32_t mds_get_handle() {
  NCSADA_INFO arg;

  memset(&arg, 0, sizeof(NCSADA_INFO));
  arg.req = NCSADA_GET_HDLS;
  uint32_t rc = ncsada_api(&arg);

  if (rc != NCSCC_RC_SUCCESS) {
    std::cerr << "MDS registration failed with :" << rc << std::endl;
    return rc;
  }
  mds_hdl = arg.info.adest_get_hdls.o_mds_pwe1_hdl;
  return rc;
}

static uint32_t mds_init() {
  NCSMDS_INFO mds_info;
  uint32_t rc;
  MDS_SVC_ID svc_id[2] = {NCSMDS_SVC_ID_AVD, NCSMDS_SVC_ID_AVND};

  memset(&mds_info, 0, sizeof(NCSMDS_INFO));
  mds_info.i_mds_hdl = mds_hdl;
  mds_info.i_svc_id = AMF_CLUSTER_MDS_SVC_ID;
  mds_info.i_op = MDS_INSTALL;
  mds_info.info.svc_install.i_yr_svc_hdl = 0;
  mds_info.info.svc_install.i_install_scope = NCSMDS_SCOPE_NONE;
  mds_info.info.svc_install.i_svc_cb = mds_callback;
  mds_info.info.svc_install.i_mds_q_ownership = false;
  mds_info.info.svc_install.i_mds_svc_pvt_ver = AMF_CLUSTER_MDS_VERSION;
  if (NCSCC_RC_SUCCESS != (rc = ncsmds_api(&mds_info))) {
    std::cerr << "MDS Install failed:" << rc << std::endl;
    goto done;
  }

  memset(&mds_info, 0, sizeof(NCSMDS_INFO));
  mds_info.i_mds_hdl = mds_hdl;
  mds_info.i_svc_id = AMF_CLUSTER_MDS_SVC_ID;
  mds_info.i_op = MDS_SUBSCRIBE;
  mds_info.info.svc_subscribe.i_num_svcs = 2;
  mds_info.info.svc_subscribe.i_scope = NCSMDS_SCOPE_NONE;
  mds_info.info.svc_subscribe.i_svc_ids = svc_id;
  if (NCSCC_RC_SUCCESS != (rc = ncsmds_api(&mds_info))) {
    std::cerr << "MDS Subscription failed" << rc << std::endl;
  }
done:
  return rc;
}

static uint32_t mds_finalize() {
  NCSMDS_INFO mds_info;

  memset(&mds_info, 0, sizeof(NCSMDS_INFO));
  mds_info.i_mds_hdl = mds_hdl;
  mds_info.i_svc_id = AMF_CLUSTER_MDS_SVC_ID;
  mds_info.i_op = MDS_UNINSTALL;
  uint32_t rc = ncsmds_api(&mds_info);
  if (rc != NCSCC_RC_SUCCESS) {
    std::cerr << "MDS uninstall failed:" << rc << std::endl;
  }
  return rc;
}

static SaAisErrorT get_amf_nodes(void) {
  SaImmSearchHandleT searchHandle;
  SaImmSearchParametersT_2 searchParam;
  const SaImmAttrValuesT_2 **attributes;
  const char *className = "SaAmfNode";
  SaImmHandleT immOmHandle;
  const char *rdn;
  SaNameT node_name;

  searchParam.searchOneAttr.attrName =
      const_cast<SaImmAttrNameT>("SaImmAttrClassName");
  searchParam.searchOneAttr.attrValueType = SA_IMM_ATTR_SASTRINGT;
  searchParam.searchOneAttr.attrValue = &className;

  SaAisErrorT rc =
      immutil_saImmOmInitialize(&immOmHandle, nullptr, &immVersion);
  if (rc != SA_AIS_OK) {
    std::cerr << "saImmOmInitialize() failed with:" << saf_error(rc) << (rc)
              << std::endl;
    goto done;
  }
  rc = immutil_saImmOmSearchInitialize_2(
      immOmHandle, nullptr, SA_IMM_SUBTREE,
      SA_IMM_SEARCH_ONE_ATTR | SA_IMM_SEARCH_GET_ALL_ATTR, &searchParam,
      nullptr, &searchHandle);
  if (rc != SA_AIS_OK) {
    std::cerr << "saImmOmSearchInitialize_2() failed with:" << saf_error(rc)
              << (rc) << std::endl;
    goto done;
  }

  while ((rc = immutil_saImmOmSearchNext_2(
              searchHandle, &node_name, (SaImmAttrValuesT_2 ***)&attributes)) ==
         SA_AIS_OK) {
    std::string::size_type pos;
    std::string::size_type pos1;
    AMF_NODE node;
    SaNameT saAmfNodeClmNode;
    std::string safAmfNode;
    // std::cout<<"AMF Node:"<<osaf_extended_name_borrow(&node_name)<<std::endl;
    if (immutil_getAttr("saAmfNodeClmNode", attributes, 0, &saAmfNodeClmNode) ==
        SA_AIS_OK) {
      std::string tmp_string(osaf_extended_name_borrow(&saAmfNodeClmNode));
      // std::cout<<"    CLM node:"<<tmp_string.c_str()<<std::endl;
      pos = tmp_string.find("=");
      pos1 = tmp_string.find(",");
      node.clm_rdn = tmp_string.substr(pos + 1, pos1 - pos - 1);
      // std::cout<<"    CLM Rdn:"<<node.clm_rdn.c_str()<<std::endl;
    }
    if ((rdn = immutil_getStringAttr(attributes, "safAmfNode", 0)) != nullptr) {
      safAmfNode = std::string(rdn);
      pos = safAmfNode.find("=");
      node.amf_rdn = safAmfNode.substr(pos + 1);
      // std::cout<<"    AMF Rdn:"<<node.amf_rdn.c_str()<<std::endl;
      if (width < node.amf_rdn.length()) width = node.amf_rdn.length();
    }
    amf_db[node.clm_rdn] = node;
  }
  // std::cout<<"num of amf nodes :"<<clm_db.size()<<std::endl;
  if (rc != SA_AIS_ERR_NOT_EXIST)
    std::cerr << "saImmOmSearchNext_2() failed with:" << rc << std::endl;
  else
    rc = SA_AIS_OK;
  (void)immutil_saImmOmSearchFinalize(searchHandle);
  (void)immutil_saImmOmFinalize(immOmHandle);
done:
  return rc;
}
static uint32_t get_clm_nodes() {
  SaImmSearchParametersT_2 searchParam;
  SaNameT dn;
  const SaImmAttrValuesT_2 **attributes;
  const char *className = "SaClmNode";
  SaImmHandleT imm_om_hdl;
  SaImmSearchHandleT search_hdl;
  const char *rdn;

  searchParam.searchOneAttr.attrName =
      const_cast<SaImmAttrNameT>("SaImmAttrClassName");
  searchParam.searchOneAttr.attrValueType = SA_IMM_ATTR_SASTRINGT;
  searchParam.searchOneAttr.attrValue = &className;

  SaAisErrorT rc = immutil_saImmOmInitialize(&imm_om_hdl, nullptr, &immVersion);
  if (rc != SA_AIS_OK) {
    std::cerr << "saImmOmInitialize() failed with:" << saf_error(rc) << (rc)
              << std::endl;
    goto done;
  }

  rc = immutil_saImmOmSearchInitialize_2(
      imm_om_hdl, nullptr, SA_IMM_SUBTREE,
      SA_IMM_SEARCH_ONE_ATTR | SA_IMM_SEARCH_GET_ALL_ATTR, &searchParam,
      nullptr, &search_hdl);
  if (rc != SA_AIS_OK) {
    std::cerr << "saImmOmSearchInitialize_2() failed with:" << saf_error(rc)
              << (rc) << std::endl;
    goto done;
  }
  while ((rc = immutil_saImmOmSearchNext_2(
              search_hdl, &dn, (SaImmAttrValuesT_2 ***)&attributes)) ==
         SA_AIS_OK) {
    std::string::size_type pos;
    std::string safNode;
    CLM_NODE node;
    // std::cout<<"CLM node:"<<osaf_extended_name_borrow(&dn)<<std::endl;
    if ((rdn = immutil_getStringAttr(attributes, "safNode", 0)) != nullptr) {
      safNode = std::string(rdn);
      pos = safNode.find("=");
      node.clm_rdn = safNode.substr(pos + 1);
      // std::cout<<"    Rdn:"<<node.clm_rdn.c_str()<<std::endl;
    }
    immutil_getAttr("saClmNodeID", attributes, 0, &node.saClmNodeID);
    // std::cout<<"      nodeId:"<<node.saClmNodeID<<std::endl;

    immutil_getAttr("saClmNodeIsMember", attributes, 0,
                    &node.saClmNodeIsMember);
    // std::cout<<"      saClmNodeIsMember:"<<node.saClmNodeIsMember<<std::endl;
    clm_db[node.clm_rdn] = node;
  }
  if (rc != SA_AIS_ERR_NOT_EXIST)
    std::cerr << "saImmOmSearchNext_2() failed with:" << rc << std::endl;
  else
    rc = SA_AIS_OK;
  // std::cout<<"num of clm nodes :"<<clm_db.size()<<std::endl;
  (void)immutil_saImmOmSearchFinalize(search_hdl);
  (void)immutil_saImmOmFinalize(imm_om_hdl);
done:
  return rc;
}
static void print_cluster_info() {
  std::cout << std::setw(width) << std::left << "NODE(AMF)"
            << "     "
            << "STATUS" << std::endl;
  std::string filler(width + strlen("     STATUS"), '=');
  std::cout << std::setw(width) << filler.c_str() << std::endl;
  for (const auto &tmp_amf : amf_db) {
    const auto &amf_node = tmp_amf.second;
    auto it = clm_db.find(amf_node.clm_rdn);
    const auto &clm_node = it->second;
    if (std::find(avnd_up_db.begin(), avnd_up_db.end(), clm_node.saClmNodeID) !=
        avnd_up_db.end())
      std::cout << std::setw(width) << std::left << amf_node.amf_rdn.c_str()
                << "     " << (is_avd_up ? "UP" : "SAM") << std::endl;
    else
      std::cout << std::setw(width) << std::left << amf_node.amf_rdn.c_str()
                << "     "
                << "DOWN" << std::endl;
  }
}

static void usage(const char *prog_name) {
  std::cout << std::endl << "NAME" << std::endl;
  std::cout << "\t" << prog_name << std::endl;
  std::cout << std::endl << "SYNOPSIS" << std::endl;
  std::cout << "\t" << prog_name << " [option]" << std::endl;

  std::cout << std::endl << "DESCRIPTION" << std::endl;
  std::cout << "\t"
            << "Use this command:"<<std::endl;
  std::cout << "\t"
            << "- to check if any System Controller(SC) is up."<<std::endl;
  std::cout << "\t"
            << "- to check if a node is System Controller(SC)."<<std::endl;
  std::cout << "\t"
            << "- to lists RDNs of AMF nodes with their status: " << std::endl;
  std::cout << "\t  "
            << "   UP:   Node is up (OpenSAF running)." << std::endl;
  std::cout << "\t  "
            << "   DOWN: Node is down (OpenSAF not running)." << std::endl;
  std::cout << "\t  "
            << "   SAM:  Stands for SCs Absence Mode. Node is up without any SCs up in cluster."
            << std::endl;
  std::cout << "\t"
            << "Note: a CLM locked node is shown up if OpenSAF is running on that node."
            << std::endl;

  std::cout << std::endl << "OPTIONS" << std::endl;
  std::cout << "\t"
            << "-s or --controller-status      returns 0 if at least one SC is up"<<std::endl
            << "\t"
            << "                               returns 1 if SCs are absent"<<std::endl<<std::endl;
  std::cout << "\t"
            << "-c or --is-controller          returns 0 if this is a SC node"<<std::endl
            << "\t"
            << "                               returns 1 if this is not a SC node"<<std::endl
            << "\t"
            << "                               If nodeid is passed as optional argument"<<std::endl
            << "\t"
            << "                               then nodeid is checked for SC."<<std::endl;
  std::cout << "\t"
            << "                               Here SC node means active or standby SC"<<std::endl
            <<std::endl;
  std::cout << "\t"
            << "-u or --node-up                returns 0 if this node is up"<<std::endl
            << "\t"
            << "                               returns 1 if this node is not up"<<std::endl
            << "\t"
            << "                               If nodeid is passed as optional argument"<<std::endl
            << "\t"
            << "                               then nodeid is checked for up status."<<std::endl<<std::endl;

  std::cout << "\t"
            << "-q or --quiet                  suppresses prints." <<std::endl <<std::endl;
  std::cout << "\t"
            << "-h or --help                   display this help and exit" <<std::endl<<std::endl;
  std::cout << "\t"
            << "Note: Without any option, prints AMF nodes and their status" <<std::endl;

  std::cout << std::endl << "EXAMPLE" << std::endl;
  std::cout << "\t"
            << "amfclusterstatus " << std::endl;
  std::cout << "\t"
            << "amfclusterstatus -h " << std::endl;
  std::cout << "\t"
            << "amfclusterstatus -c -q" << std::endl;
  std::cout << "\t"
            << "amfclusterstatus -c 0x2030f -q" << std::endl;
  std::cout << "\t"
            << "amfclusterstatus -u -q" << std::endl;
  std::cout << "\t"
            << "amfclusterstatus -u 0x2030f -q" << std::endl;
  std::cout << "\t"
            << "amfclusterstatus -s -q" << std::endl;
}

int main(int argc, char *argv[]) {
  char opt_char = 'z';
  char *ptr = NULL;
  NODE_ID node_id = 0;
  bool user_node = false;

  struct option long_options[] = {
    {"help",                no_argument,       0, 'h'},
    {"controller-status",   no_argument,       0, 's'},
    {"is-controller",       optional_argument, 0, 'c'},
    {"node-up",             optional_argument, 0, 'u'},
    {"quiet",               no_argument,       0, 'q'},
    {0, 0, 0, 0}
  };
  while (1) {
     int option = getopt_long(argc, argv, "sc::u::qh", long_options,NULL);
     if (option == -1)
       break;

     switch (option) {
     case 's':
             opt_char = option;
             break;
     case 'c':
     case 'u':
             opt_char = option;
	     if (optarg == NULL) {
	       if ((argv[optind] != NULL) &&
	           (argv[optind][0] != '-')) {
		 node_id = strtoul(argv[optind], &ptr, 0);
		 ++optind;
                 user_node = true;
               }
	     } else {
	       node_id = strtoul(optarg, &ptr, 0);
               user_node = true;
             }
             break;
     case 'q':
             print_also = false;
             break;
     case 'h':
             usage(basename(argv[0]));
             exit(EXIT_SUCCESS);
     case '?':
	     std::cout << "Try "<< basename(argv[0]) << "-h or --help for more information" << std::endl;
             exit(EXIT_FAILURE);
             break;
     }
  }

  setenv("SA_ENABLE_EXTENDED_NAMES", "1", 1);
  if (ncs_agents_startup() != NCSCC_RC_SUCCESS) {
    std::cerr << "ncs_core_agents_startup FAILED" << std::endl;
    exit(EXIT_FAILURE);
  }

  if (user_node == false)
    node_id = m_NCS_GET_NODE_ID; //Get local node_id

  if (mds_get_handle() != NCSCC_RC_SUCCESS) exit(EXIT_FAILURE);
  if (mds_init() != NCSCC_RC_SUCCESS) exit(EXIT_FAILURE);
  if (get_clm_nodes() != SA_AIS_OK) exit(EXIT_FAILURE);
  if (get_amf_nodes() != SA_AIS_OK) exit(EXIT_FAILURE);
  if (mds_finalize() != NCSCC_RC_SUCCESS) exit(EXIT_FAILURE);

  switch (opt_char) {
  case 's':
          if (is_avd_up == true) {
            if (print_also == true)
               std::cout << "At least one controller is up"<<std::endl;
            exit(EXIT_SUCCESS);
          } else {
            if (print_also == true)
              std::cout << "No controller is up"<<std::endl;
            exit(EXIT_FAILURE);
          }
          break;
  case 'c':
          if ((node_id == scs_node_id[0]) || (node_id == scs_node_id[1])) {
            if (print_also == true) {
              if (user_node == false)
                std::cout << "This is a Controller node"<<std::endl;
              else
                std::cout << std::hex << node_id <<" is a Controller node"<<std::endl;
            }
	    exit(EXIT_SUCCESS);
          } else {
            if (print_also == true) {
              if (user_node == false)
                std::cout << "This is not a Controller node"<<std::endl;
              else
                std::cout << std::hex << node_id <<" is not a Controller node"<<std::endl;
            }
	    exit(EXIT_FAILURE);
          }
          break;
  case 'u':
          if (std::find(avnd_up_db.begin(), avnd_up_db.end(), node_id) !=
              avnd_up_db.end()) {
            if (print_also == true) {
             if (user_node == false)
               std::cout << "This node is up"<<std::endl;
             else
               std::cout << std::hex << node_id <<" is up"<<std::endl;
	    }
	    exit(EXIT_SUCCESS);
          } else {
            if (print_also == true) {
                std::cout << std::hex << node_id <<" is not up"<<std::endl;
            }
            exit(EXIT_FAILURE);
          }
          break;
  default:
          if (print_also == true)
          print_cluster_info();
          break;
  }
  std::cout << std::endl;
  ncs_agents_shutdown();
  return EXIT_SUCCESS;
}
