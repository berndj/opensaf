/*      -*- OpenSAF  -*-
 *
 * (C) Copyright 2010,2015 The OpenSAF Foundation
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
 * Author(s):  Emerson Network Power
 *             Ericsson AB
 */

#ifndef CLM_CLMD_CLMS_CB_H_
#define CLM_CLMD_CLMS_CB_H_

#ifdef HAVE_CONFIG_H
#include "osaf/config.h"
#endif
#include <pthread.h>
#include <saImm.h>
#include <saImmOi.h>
#include <saPlm.h>
#include "base/ncssysf_ipc.h"
#include "mbc/mbcsv_papi.h"
#include "mds/mds_papi.h"
#include "rde/agent/rda_papi.h"

#define IMPLEMENTER_NAME "safClmService"
#define CLMS_HA_INIT_STATE 0

/* The maximum number of nodes that can be queued for scale-out while the
   scale-out script is executing */
#define MAX_PENDING_NODES 32

/* The value to put in the PATH environment variable when calling the
   scale-out script */
#define SCALE_OUT_PATH_ENV                    \
  "/usr/local/sbin:/usr/local/bin:/usr/sbin:" \
  "/usr/bin:/sbin:/bin"

/* Full path to the scale-out script. */
#define SCALE_OUT_SCRIPT PKGLIBDIR "/opensaf_scale_out"

typedef enum clms_tmr_type_t {
  CLMS_TMR_BASE,
  CLMS_CLIENT_RESP_TMR = CLMS_TMR_BASE,
  CLMS_NODE_LOCK_TMR,
  CLMS_TMR_MAX
} CLMS_TMR_TYPE;

typedef enum {
  PLM = 1,
  IMM_LOCK = 2,
  IMM_SHUTDOWN = 3,
  IMM_UNLOCK = 4,
  IMM_RECONFIGURED = 5
} ADMIN_OP;

typedef enum { CHECKPOINT_PROCESSED = 1, MDS_DOWN_PROCESSED } NODE_DOWN_STATUS;

/* Cluster Properties */
typedef struct cluster_db_t {
  SaNameT name;
  SaUint32T num_nodes;
  SaTimeT init_time;
  SaBoolT
      rtu_pending; /* Flag to indicate whether an RTU failed and is pending */
  /*struct cluster_db_t *next; */ /* Multiple cluster is not supported as of now
                                   */
} CLMS_CLUSTER_INFO;

/* A CLM node properties record */
typedef struct cluster_node_t {
  NCS_PATRICIA_NODE pat_node_eename;
  NCS_PATRICIA_NODE pat_node_name;
  NCS_PATRICIA_NODE pat_node_id;
  SaClmNodeIdT node_id;
  SaClmNodeAddressT node_addr;
  SaNameT node_name;
  SaNameT ee_name;
  SaBoolT member;
  SaTimeT boot_time;
  SaUint64T init_view;
  SaBoolT disable_reboot;
  SaTimeT lck_cbk_timeout;
  SaClmAdminStateT admin_state;
  SaClmClusterChangesT change;
#ifdef ENABLE_AIS_PLM
  SaPlmReadinessStateT ee_red_state;
#endif
  SaBoolT nodeup; /*Check for the connectivity */
  SaInvocationT curr_admin_inv;
  NCS_PATRICIA_TREE trackresp;
  SaBoolT stat_change;       /*Required to check for the number of nodes on which
                                change has occured */
  ADMIN_OP admin_op;         /*plm or clm operation */
  timer_t lock_timerid;      /*Timer id for admin lock operation */
  SaInvocationT plm_invid;   /*plmtrack callback invocation id */
  SaBoolT rtu_pending;       /* Flag to mark whether an IMM RunTime attribute Update
                                is pending and to be retried */
  SaBoolT admin_rtu_pending; /* Flag to mark whether an IMM RunTime attribute
                                Update is pending and to be retried */
  struct cluster_node_t
      *dep_node_list; /*Dependent nodes list - in case of plm operation */
  struct cluster_node_t *next;
} CLMS_CLUSTER_NODE;

typedef struct clms_rem_reboot_t {
  char *str;
  CLMS_CLUSTER_NODE *node;
} CLMS_REM_REBOOT_INFO;

/* List to track the responses from CLMAs.
 * A node is added to this list when a track callback
 * is sent to a CLMA.
 * A node is deleted from this list when a saClmResponse()
 * is received from the CLMA.
 */
typedef struct clms_track_info_t {
  NCS_PATRICIA_NODE pat_node;
  MDS_DEST client_dest;
  uint32_t client_id;
  uint32_t client_id_net;
  SaInvocationT inv_id;
  /* How about Invocation id!? Check the current CLMA and accordingly add */
} CLMS_TRACK_INFO;

/* CLM client information record */
typedef struct client_info_t {
  NCS_PATRICIA_NODE pat_node;
  uint32_t client_id; /* Changes to 32 -bit from 64-bit */
  uint32_t
      client_id_net; /* dude client_id_net it was 32 bit now got changed to 64
                        bit, see counter effects */
  MDS_DEST mds_dest;
  SaUint8T track_flags;
  SaInvocationT inv_id;
} CLMS_CLIENT_INFO;

/* Confirmation to be data to be checkpointed*/
/* CHECKPOINT status */
typedef enum checkpoint_status {
  COLD_SYNC_IDLE = 0,
  REG_REC_SENT,
  SUBSCRIPTION_REC_SENT,
  COLD_SYNC_COMPLETE,
} CHECKPOINT_STATE;

typedef struct clma_down_list_tag {
  MDS_DEST mds_dest;
  struct clma_down_list_tag *next;
} CLMA_DOWN_LIST;

typedef struct node_down_list_tag {
  SaClmNodeIdT node_id;
  NODE_DOWN_STATUS ndown_status;
  struct node_down_list_tag *next;
} NODE_DOWN_LIST;

/* A list temporarily maintained to store ip information.
 * This list will be populated whenever a MDS NODE_UP event arrives.
 * The values in this list will be used to update the NODE_DB whenever
 * a NODE_JOIN request is received.
 */
typedef struct temp_iplist_tag {
  NCS_PATRICIA_NODE pat_node_id;
  SaClmNodeIdT node_id;
  SaClmNodeAddressT addr;
  struct temp_iplist_tag *next;
} IPLIST;

/* CLM Server control block */
typedef struct clms_cb_t {
  /* MDS, MBX & thread related defs */
  SYSF_MBX mbx;          /* CLM Server's Mail Box */
  MDS_HDL mds_hdl;       /* handle to interact with MDS */
  MDS_HDL mds_vdest_hdl; /* Not Needed. can be removed */
  V_DEST_RL mds_role;    /* Current MDS VDEST role */
  MDS_DEST vaddr;        /* CLMS' MDS address */
  uint32_t my_hdl;       /* Handle Manager Handle */
  PCS_RDA_ROLE my_role;  /* RDA provided role */
  SaVersionT clm_ver;    /*Currently Supported CLM Version */
  /* AMF related defs */
  SaNameT comp_name;              /* My AMF name */
  SaAmfHandleT amf_hdl;           /* Handle obtained from AMF */
  SaInvocationT amf_inv;          /* AMF Invocation Id */
  bool is_quiesced_set;           /* */
  SaSelectionObjectT amf_sel_obj; /* AMF provided selection object */
  NCS_SEL_OBJ sighdlr_sel_obj;    /* Selection object to handle SIGUSR1 */
  SaAmfHAStateT ha_state;         /* My current AMF HA state */
  bool fully_initialized;
  NCS_MBCSV_HDL mbcsv_hdl;
  SaSelectionObjectT
      mbcsv_sel_obj; /* MBCSv Selection Object to maintain a HotStandBy CLMS */
  NCS_MBCSV_CKPT_HDL mbcsv_ckpt_hdl;
  CHECKPOINT_STATE ckpt_state;
  SaImmOiHandleT immOiHandle;     /* IMM OI Handle */
  SaSelectionObjectT imm_sel_obj; /* Selection object to wait for IMM events */
  NODE_ID node_id;                /* My CLM Node Id */
  SaUint64T cluster_view_num; /* the current cluster membership view number */

  SaUint32T async_upd_cnt; /* Async Update Count for Warmsync */
  NCS_PATRICIA_TREE
      nodes_db; /* CLMS_NODE_DB storing information of Cluster Nodes & Clients
                 * and the client's cluster track subscription information.
                 * Indexed by Node Name.
                 */
  NCS_PATRICIA_TREE
      ee_lookup; /* To lookup the CLMS_NODE_DB with ee_name as key,
                  * for PLM track callback.
                  */
  NCS_PATRICIA_TREE
      id_lookup;               /* To lookup the CLMS_NODE_DB with node_id as key,
                                * for saClmNodeGet() API.
                                */
  NCS_PATRICIA_TREE client_db; /* Client DataBase */
  uint32_t curr_invid;
  uint32_t last_client_id; /* Value of last client_id assigned */
#ifdef ENABLE_AIS_PLM
  SaPlmHandleT plm_hdl;                  /* Handle obtained from PLM */
  SaPlmEntityGroupHandleT ent_group_hdl; /* PLM Entity Group Handle */
#endif
  SaSelectionObjectT plm_sel_obj; /* PLMSv selection object */
  SaNtfHandleT ntf_hdl;           /* Handled obtained from NTFSv */
  SaBoolT reg_with_plm;           /*plm present in system */
  SaBoolT rtu_pending;            /* Global flag to determine a pending RTU update and the
                                     poll timeout */
  CLMA_DOWN_LIST
      *clma_down_list_head; /* CLMA down reccords - Fix for Failover missed
                               down events Processing */
  CLMA_DOWN_LIST *clma_down_list_tail;
  NODE_DOWN_LIST *
      node_down_list_head; /*NODE_DOWN record - Fix when active node goes down
                            */
  NODE_DOWN_LIST *node_down_list_tail;
  bool is_impl_set;
  bool nid_started;         /**< true if started by NID */
  NCS_PATRICIA_TREE iplist; /* To temporarily store ipaddress information
                               recieved in MDS NODE_UP */

  /* Mutex protecting shared data used by the scale-out functionality */
  pthread_mutex_t scale_out_data_mutex;
  /* Number of occupied indices in the vectors pending_nodes[] and
   * pending_node_ids[] */
  size_t no_of_pending_nodes;
  /* Number of occupied indices in the vector inprogress_node_ids[] */
  size_t no_of_inprogress_nodes;
  /* Names of the nodes to be added in the next run of the scale-out
   * script */
  char *pending_nodes[MAX_PENDING_NODES + 1];
  /* Node ids of the nodes to be added in the next run of the the
   * scale-out script */
  SaClmNodeIdT pending_node_ids[MAX_PENDING_NODES + 1];
  /* Node ids of the nodes that are being added by the currently executing
   * instance of the scale-out script */
  SaClmNodeIdT inprogress_node_ids[MAX_PENDING_NODES + 1];
  /* True if the scale-out thread is currently running */
  bool is_scale_out_thread_running;
  /* Full path to the scale-out script, or nullptr if feature is disabled */
  char *scale_out_script;
  /* internal field separator */
  char ifs;
} CLMS_CB;

typedef struct clms_lock_tmr_t { SaNameT node_name; } CLMS_LOCK_TMR;

uint32_t clm_snd_track_changes(CLMS_CB *cb, CLMS_CLUSTER_NODE *node,
                               CLMS_CLIENT_INFO *client,
                               SaImmAdminOperationIdT opId,
                               SaClmChangeStepT step);
void clms_track_send_node_down(CLMS_CLUSTER_NODE *node);
void clms_reboot_remote_node(CLMS_CLUSTER_NODE *op_node, const char *str);
void *clms_rem_reboot(void *_rem_reboot);
#define m_CLMSV_PACK_INV(inv, nodeid) ((((SaUint64T)inv) << 32) | nodeid)
#define m_CLMSV_INV_UNPACK_INVID(inv) ((inv) >> 32)
#define m_CLMSV_INV_UNPACK_NODEID(inv) ((inv)&0x00000000ffffffff)

#endif  // CLM_CLMD_CLMS_CB_H_
