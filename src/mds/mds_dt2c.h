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

  DESCRIPTION:  This file contains the API that are given by MDTM module to MCM
module

  ******************************************************************************
  */

#ifndef MDS_MDS_DT2C_H_
#define MDS_MDS_DT2C_H_

#include "base/ncssysf_tmr.h"
#include "base/ncsgl_defs.h"
#include "mds/mds_papi.h"

/* Specification of the interface between MCM and MDTM */

typedef uint8_t
    MDS_SVC_ARCHWORD_TYPE; /*MDS  app-svc arch and word_size combination */

#define MDS_WORD_SIZE_TYPE \
  ((sizeof(long) / 4) - 1) /* 0 for 32-bit, 1 for 64-bit */

/*
 * 4 bit ARCHWORD usage:
 * Bit 3 is wordsize
 * Bit 2:0 is a version field indicating capabilities.
 *    Version 0 uses 1400 bytes fragmentation.
 *    Version 1 uses TIPC max msg (66000 bytes) fragmentation.
 */
#define MDS_SELF_ARCHWORD \
  ((MDS_SVC_ARCHWORD_TYPE)((MDS_WORD_SIZE_TYPE << 3) | 2))

typedef enum { MDS_VIEW_NORMAL, MDS_VIEW_RED } MDS_VIEW;

typedef uint16_t MDS_PWE_ID;

typedef uint64_t MDS_SUBTN_REF_VAL;

typedef NCS_VDEST_TYPE MDS_POLICY;

typedef uint32_t MDS_VDEST_HDL; /* <0,vdestid> */
typedef uint64_t MDS_SVC_HDL;   /* <pweid,vdestid,svcid> */
typedef uint32_t MDS_PWE_HDL;   /* <pweid,vdestid> */
/* typedef uint32_t MDS_SUBTN_HDL; */
typedef uint64_t MDS_TX_HDL;
typedef enum { MDS_SUBTN_IMPLICIT, MDS_SUBTN_EXPLICIT } MDS_SUBTN_TYPE;

typedef enum {
  MDS_ENC_TYPE_CPY,
  MDS_ENC_TYPE_FLAT,
  MDS_ENC_TYPE_FULL,
  MDS_ENC_TYPE_DIRECT_BUFF,
} MDS_ENC_TYPES;

typedef struct mds_direct_buff_info {
  uint16_t len;
  MDS_DIRECT_BUFF buff;
} MDS_DIRECT_BUFF_INFO;

typedef struct mds_encoded_msg {
  MDS_ENC_TYPES encoding;
  union {
    NCSCONTEXT cpy_msg;

    NCS_UBAID flat_uba;
    NCS_UBAID fullenc_uba;
    MDS_DIRECT_BUFF_INFO buff_info;
  } data;
} MDS_ENCODED_MSG;

/* Structure format given to MCM from MDTM when the data is recd */

typedef struct mds_data_recv {
  MDS_ENCODED_MSG msg;
  uint16_t src_svc_id;
  uint16_t src_pwe_id;
  uint16_t src_vdest;
  uint32_t exchange_id;
  MDS_SVC_HDL
      dest_svc_hdl; /* Got from upper layer by passing SVC,PWE and VDEST */
  MDS_DEST src_adest;
  MDS_SENDTYPES snd_type;
  uint8_t ret_val; /* Valid only for ACK messages */
  MDS_SEND_PRIORITY_TYPE pri;
  MDS_CLIENT_MSG_FORMAT_VER
      msg_fmt_ver; /* message format version specification */
  MDS_SVC_PVT_SUB_PART_VER src_svc_sub_part_ver;
  MDS_SVC_ARCHWORD_TYPE msg_arch_word;
  uint8_t src_node_name_len;
  char src_node_name[_POSIX_HOST_NAME_MAX]; /* Node Name of the sender/source*/
  uint32_t src_seq_num;
  pid_t pid;
  uid_t uid;
  gid_t gid;

} MDS_DATA_RECV;

/* Sending data/messages to the destination */
typedef struct mdtm_send_req {
#define DESTINATION_SAME_PROCESS 1
#define DESTINATION_ON_NODE 2
#define DESTINATION_OFF_NODE 3

  uint8_t to;       /* Destination same node, process or off node */
  bool consume_buf; /* Can be false for broadcast.
                       Avoids ditto step.
                       Is there a better way?
                    */
  MDS_SVC_ID src_svc_id;
  PW_ENV_ID src_pwe_id;
  MDS_VDEST_ID src_vdest_id;
  MDS_DEST src_adest;
  MDS_SENDTYPES snd_type;
  uint32_t xch_id;
  uint32_t svc_seq_num;

  MDS_SVC_ID dest_svc_id;
  PW_ENV_ID dest_pwe_id;
  MDS_VDEST_ID dest_vdest_id;

  MDS_ENCODED_MSG msg; /* Mem released after send if
                          consume_buf = true- SOme
                          such scheme
                       */
  MDS_DEST adest;      /* MDTM to do local/remote routing, destination adest */
  char sub_adest_details
      [MDS_MAX_PROCESS_NAME_LEN]; /* <node[nodeid]:processname[pid]> */
  MDS_SEND_PRIORITY_TYPE pri;
  MDS_CLIENT_MSG_FORMAT_VER
      msg_fmt_ver; /* message format version specification */
  MDS_SVC_PVT_SUB_PART_VER src_svc_sub_part_ver;
  MDS_SVC_ARCHWORD_TYPE msg_arch_word;
} MDTM_SEND_REQ;

typedef struct mds_await_active_queue {
  MDTM_SEND_REQ req;

  struct mds_await_active_queue *next_msg;
} MDS_AWAIT_ACTIVE_QUEUE;

typedef struct mds_mdtm_query_adest_info {
  MDS_DEST i_adest;
  bool o_is_reachable; /* Putting JFK. Do not know use */
  bool o_is_in_mypcon;
  bool o_is_in_mynode;
  bool o_is_in_mychassis;
  MDS_ENC_TYPES o_enc_type; /* enc/enc-flat/cpy */
} MDS_MDTM_QUERY_ADEST_INFO;

typedef struct mdtm_svc_inst_info {
  PW_ENV_ID pwe_id;
  MDS_SVC_ID svc_id;
  NCSMDS_SCOPE_TYPE install_scope;
  V_DEST_RL role;
  MDS_VDEST_ID vdest_id;
  NCS_VDEST_TYPE vdest_policy;
  MDS_SVC_PVT_SUB_PART_VER svc_sub_part_ver;
} MDTM_SVC_INST_INFO;

typedef MDTM_SVC_INST_INFO MDTM_SVC_UNINST_INFO;

typedef struct mcm_svc_up_info {
  PW_ENV_ID pwe_id;
  MDS_SVC_ID svc_id;
  V_DEST_RL role;
  NCSMDS_SCOPE_TYPE scope;
  MDS_VDEST_ID vdest;
  NCS_VDEST_TYPE vdest_policy;
  MDS_DEST adest;
  bool my_pcon;
  MDS_SVC_HDL local_svc_hdl;
  MDS_SUBTN_REF_VAL subtn_ref_val;
  MDS_SVC_PVT_SUB_PART_VER sub_part_ver;
  MDS_SVC_ARCHWORD_TYPE arch_word;
} MCM_SVC_UP_INFO;

typedef MCM_SVC_UP_INFO MCM_SVC_DOWN_INFO;

uint32_t mds_dt2c_query_adest(MDS_MDTM_QUERY_ADEST_INFO *req);

/* ******************************************** */
/* ******************************************** */
/*                  SUB_SVC_ID                  */
/* ******************************************** */
/* ******************************************** */

typedef enum {
  MDS_MEM_MCM_CB = 1,
  MDS_MEM_ACTIVE_RESULT_INFO,
  MDS_MEM_VDEST_INFO,
  MDS_MEM_PWE_INFO,
  MDS_MEM_SVC_INFO,
  MDS_MEM_SUBTN_INFO,
  MDS_MEM_SUBTN_RESULT_INFO,
  MDS_MEM_SUBTN_ACTIVE_RESULT_INFO,
  MDS_MEM_AWAIT_ACTIVE_QUEUE_INFO,
  MDS_MEM_CALLBACK_INFO,
  MDS_DIRECT_BUFF_AL,
  MDS_MEM_TMR_INFO,
  MDS_MEM_MBX_EVT_INFO,

  MDS_MEM_DISC_QUEUE,
  MDS_MEM_SYNC_SEND_QUEUE,
  MDS_MEM_DIRECT_BUFF,
  MDS_MEM_AWAIT_ACTIVE,
  MDS_MEM_MSGELEM,
  MDS_MEM_ADEST_LIST,
  MDS_MEM_REASSEM_QUEUE,
  MDS_MEM_HDL_LIST,
  MDS_MEM_CACHED_EVENTS_LIST,
  MDS_MEM_BCAST_BUFF_LIST,
} MDS_MEM_SUB_ID;

/* ******************************************** */
/* ******************************************** */
/*                 Timer message                */
/* ******************************************** */
/* ******************************************** */

typedef enum {
  MDS_QUIESCED_TMR = 1,
  MDS_SUBTN_TMR,
  MDS_AWAIT_ACTIVE_TMR,
  MDS_REASSEMBLY_TMR,
  MDS_DOWN_TMR
} TMR_TYPE;

typedef struct mds_tmr_req_info {
  TMR_TYPE type;
  union {
    struct {
      MDS_VDEST_ID vdest_id;
    } quiesced_tmr_info;

    struct {
      MDS_SVC_HDL svc_hdl;
      MDS_SVC_ID sub_svc_id;
    } subtn_tmr_info;

    struct {
      MDS_SVC_HDL svc_hdl;
      MDS_SVC_ID sub_svc_id;
      MDS_VDEST_ID vdest_id;
    } await_active_tmr_info;

    struct {
      uint32_t seq_no;
      MDS_DEST id;
    } reassembly_tmr_info;

    struct {
      MDS_DEST adest;
      MDS_SVC_ID svc_id;
      tmr_t tmr_id;
    } down_event_tmr_info;

  } info;

} MDS_TMR_REQ_INFO;

typedef enum {
  MDS_MBX_EVT_TIMER_EXPIRY,
  MDS_MBX_EVT_DESTROY,
} MDS_MBX_EVT_TYPE;

typedef struct mds_mbx_evt_info {
  void *next;

  MDS_MBX_EVT_TYPE type;

  union {
    uint32_t tmr_info_hdl;       /* For use with MDS_MBX_EVT_TIMER_EXPIRY */
    NCS_SEL_OBJ destroy_ack_obj; /* For use with MDS_MBX_EVT_DESTROY      */
  } info;
} MDS_MBX_EVT_INFO;

#define m_MMGR_ALLOC_TMR_INFO                                   \
  (MDS_TMR_REQ_INFO *)m_NCS_MEM_ALLOC(sizeof(MDS_TMR_REQ_INFO), \
                                      NCS_MEM_REGION_TRANSIENT, \
                                      NCS_SERVICE_ID_MDS, MDS_MEM_TMR_INFO)

#define m_MMGR_FREE_TMR_INFO(p)                                   \
  m_NCS_MEM_FREE(p, NCS_MEM_REGION_TRANSIENT, NCS_SERVICE_ID_MDS, \
                 MDS_MEM_TMR_INFO)

#define m_MMGR_ALLOC_MBX_EVT_INFO                                             \
  (MDS_MBX_EVT_INFO *)m_NCS_MEM_ALLOC(                                        \
      sizeof(MDS_MBX_EVT_INFO), NCS_MEM_REGION_TRANSIENT, NCS_SERVICE_ID_MDS, \
      MDS_MEM_MBX_EVT_INFO)

#define m_MMGR_FREE_MBX_EVT_INFO(p)                               \
  m_NCS_MEM_FREE(p, NCS_MEM_REGION_TRANSIENT, NCS_SERVICE_ID_MDS, \
                 MDS_MEM_MBX_EVT_INFO)

/* ******************************************** */
/* ******************************************** */
/*        Default Timer Values Macros           */
/* ******************************************** */
/* ******************************************** */

/* extern gl_mds_checksum */
extern uint32_t gl_mds_checksum;

/* ******************************************** */
/* ******************************************** */
/*                MCM to MDTM                   */
/* ******************************************** */
/* ******************************************** */

/* Initialization of MDTM Module */
uint32_t (*mds_mdtm_init)(NODE_ID node_id, uint32_t *mds_tipc_ref);

/* Destroying the MDTM Module*/
uint32_t (*mds_mdtm_destroy)(void);

uint32_t (*mds_mdtm_send)(MDTM_SEND_REQ *req);

/* SVC Install */
uint32_t (*mds_mdtm_svc_install)(PW_ENV_ID pwe_id, MDS_SVC_ID svc_id,
                                 NCSMDS_SCOPE_TYPE install_scope,
                                 V_DEST_RL role, MDS_VDEST_ID vdest_id,
                                 NCS_VDEST_TYPE vdest_policy,
                                 MDS_SVC_PVT_SUB_PART_VER mds_svc_pvt_ver);

/* SVC Uninstall */
uint32_t (*mds_mdtm_svc_uninstall)(PW_ENV_ID pwe_id, MDS_SVC_ID svc_id,
                                   NCSMDS_SCOPE_TYPE install_scope,
                                   V_DEST_RL role, MDS_VDEST_ID vdest_id,
                                   NCS_VDEST_TYPE vdest_policy,
                                   MDS_SVC_PVT_SUB_PART_VER mds_svc_pvt_ver);

/* SVC Subscribe */
uint32_t (*mds_mdtm_svc_subscribe)(PW_ENV_ID pwe_id, MDS_SVC_ID svc_id,
                                   NCSMDS_SCOPE_TYPE subscribe_scope,
                                   MDS_SVC_HDL local_svc_hdl,
                                   MDS_SUBTN_REF_VAL *subtn_ref_val);

/*  added svc_hdl */
/* SVC Unsubscribe */
uint32_t (*mds_mdtm_svc_unsubscribe)(PW_ENV_ID pwe_id, MDS_SVC_ID svc_id,
                                     NCSMDS_SCOPE_TYPE subscribe_scope,
                                     MDS_SUBTN_REF_VAL subtn_ref_val);

/* VDEST Install */
uint32_t (*mds_mdtm_vdest_install)(MDS_VDEST_ID vdest_id);

/* VDEST Uninstall */
uint32_t (*mds_mdtm_vdest_uninstall)(MDS_VDEST_ID vdest_id);

/* VDEST Subscribe */
uint32_t (*mds_mdtm_vdest_subscribe)(MDS_VDEST_ID vdest_id,
                                     MDS_SUBTN_REF_VAL *subtn_ref_val);

/* VDEST Unsubscribe */
uint32_t (*mds_mdtm_vdest_unsubscribe)(MDS_VDEST_ID vdest_id,
                                       MDS_SUBTN_REF_VAL subtn_ref_val);

/* Tx Register (For incrementing the use count) */
uint32_t (*mds_mdtm_tx_hdl_register)(MDS_DEST adest);

/* Tx Unregister (For decrementing the use count) */
uint32_t (*mds_mdtm_tx_hdl_unregister)(MDS_DEST adest);

/* Node subscription */
uint32_t (*mds_mdtm_node_subscribe)(MDS_SVC_HDL svc_hdl,
                                    MDS_SUBTN_REF_VAL *subtn_ref_val);

/* Node unsubscription */
uint32_t (*mds_mdtm_node_unsubscribe)(MDS_SUBTN_REF_VAL subtn_ref_val);

/* ******************************************** */
/* ******************************************** */
/*                MDTM to MCM                   */
/* ******************************************** */
/* ******************************************** */

/* Data receive from mdtm layer*/
extern uint32_t mds_mcm_ll_data_rcv(MDS_DATA_RECV *recv);

/* SVC UP */
extern uint32_t mds_mcm_svc_up(PW_ENV_ID pwe_id, MDS_SVC_ID svc_id,
                               V_DEST_RL role, NCSMDS_SCOPE_TYPE scope,
                               MDS_VDEST_ID vdest, NCS_VDEST_TYPE vdest_policy,
                               MDS_DEST adest, bool my_pcon,
                               MDS_SVC_HDL local_svc_hdl,
                               MDS_SUBTN_REF_VAL subtn_ref_val,
                               MDS_SVC_PVT_SUB_PART_VER svc_sub_part_ver,
                               MDS_SVC_ARCHWORD_TYPE archword_type);

/* SVC DOWN */
extern uint32_t mds_mcm_svc_down(PW_ENV_ID pwe_id, MDS_SVC_ID svc_id,
                                 V_DEST_RL role, NCSMDS_SCOPE_TYPE scope,
                                 MDS_VDEST_ID vdest,
                                 NCS_VDEST_TYPE vdest_policy, MDS_DEST adest,
                                 bool my_pcon, MDS_SVC_HDL local_svc_hdl,
                                 MDS_SUBTN_REF_VAL subtn_ref_val,
                                 MDS_SVC_PVT_SUB_PART_VER svc_sub_part_ver,
                                 MDS_SVC_ARCHWORD_TYPE archword_type);

/* NODE UP */
extern uint32_t mds_mcm_node_up(MDS_SVC_HDL local_svc_hdl, NODE_ID node_id,
                                char *node_ip, uint16_t addr_family,
                                char *node_name);

/* NODE DOWN */
extern uint32_t mds_mcm_node_down(MDS_SVC_HDL local_svc_hdl, NODE_ID node_id,
                                  uint16_t addr_family);

/* VDEST UP */
extern uint32_t mds_mcm_vdest_up(MDS_VDEST_ID vdest_id, MDS_DEST adest);

/* VDEST DOWN */ /* Presently Discarded */
extern uint32_t mds_mcm_vdest_down(MDS_VDEST_ID vdest_id, MDS_DEST adest);

/* Msg loss */
extern void mds_mcm_msg_loss(MDS_SVC_HDL local_svc_hdl, MDS_DEST rem_adest,
                             MDS_SVC_ID rem_svc_id, MDS_VDEST_ID rem_vdest_id);
extern void mds_incr_subs_res_recvd_msg_cnt(MDS_SVC_HDL svc_hdl,
                                            MDS_SVC_ID subscr_svc_id,
                                            MDS_VDEST_ID vdest_id,
                                            MDS_DEST adest,
                                            uint32_t src_seq_num);

/* Timer expiry functions of MCM */

/* Quiesced timer expiry */
extern uint32_t mds_mcm_quiesced_tmr_expiry(MDS_VDEST_ID vdest_id);

/* Subscription timer expiry */
extern uint32_t mds_mcm_subscription_tmr_expiry(MDS_SVC_HDL svc_hdl,
                                                MDS_SVC_ID sub_svc_id);

/* Await Active timer expiry */
extern uint32_t mds_mcm_await_active_tmr_expiry(MDS_SVC_HDL svc_hdl,
                                                MDS_SVC_ID sub_svc_id,
                                                MDS_VDEST_ID vdest_id);

/* MDS Destroy event */
extern uint32_t mds_destroy_event(NCS_SEL_OBJ destroy_ack_obj);
/* Timer callback event */
extern uint32_t mds_tmr_callback(NCSCONTEXT hdl);

/* for pcon check for bcast send*/
extern uint32_t mdtm_check_pcon(MDS_DEST adest);

/* SVC TABLE Operations */
extern uint32_t mds_svc_tbl_get(MDS_PWE_HDL pwe_hdl, MDS_SVC_ID svc_id,
                                NCSCONTEXT *svc_cb);
extern uint32_t mds_svc_tbl_get_role(MDS_SVC_HDL svc_hdl); /*  returns 0 or 1 */
extern uint32_t mds_svc_tbl_get_svc_hdl(MDS_PWE_HDL pwe_hdl, MDS_SVC_ID svc_id,
                                        MDS_SVC_HDL *svc_hdl);
/* AWAIT ACTIVE TABLE Operations */
extern uint32_t mds_await_active_tbl_send(MDS_AWAIT_ACTIVE_QUEUE *queue,
                                          MDS_DEST adest, MDS_SVC_HDL svc_hdl);
extern uint32_t mds_await_active_tbl_del(MDS_AWAIT_ACTIVE_QUEUE *queue);

/* Adding Subscription */
extern uint32_t mds_mcm_subtn_add(MDS_SVC_HDL svc_hdl, MDS_SVC_ID subscr_svc_id,
                                  NCSMDS_SCOPE_TYPE scope, MDS_VIEW view,
                                  MDS_SUBTN_TYPE subtn_type);

#define m_MDS_GET_PWE_HDL_FROM_VDEST_HDL_AND_PWE_ID(vdest_hdl, pwe_id) \
  ((uint32_t)pwe_id << 16 | (uint32_t)vdest_hdl)
#define m_MDS_GET_SVC_HDL_FROM_PWE_ID_VDEST_ID_AND_SVC_ID(pwe_id, vdest_id, \
                                                          svc_id)           \
  (((uint64_t)pwe_id << 48) | ((uint64_t)vdest_id << 32) | ((uint64_t)svc_id))

/* Get VDEST id from MDSDEST INTERNAL */
extern MDS_VDEST_ID ncs_get_internal_vdest_id_from_mds_dest(MDS_DEST mdsdest);
#define m_MDS_GET_INTERNAL_VDEST_ID_FROM_MDS_DEST(mdsdest) \
  (ncs_get_internal_vdest_id_from_mds_dest(mdsdest))

/* Macros to Get NODE_ID and PROCESS_ID from ADEST */
#define m_MDS_GET_NODE_ID_FROM_ADEST(adest) (NODE_ID)((uint64_t)adest >> 32)
#define m_MDS_GET_PROCESS_ID_FROM_ADEST(adest) \
  (uint32_t)((uint64_t)adest & 0x00000000ffffffff)

/* Macros to get SVC_ID from SVC_HDL */
#define m_MDS_GET_SVC_ID_FROM_SVC_HDL(svc_hdl) \
  (MDS_SVC_ID)(((uint64_t)svc_hdl & 0x00000000ffffffff))

/* for defining the MDS internal return values */
typedef enum {
  MDS_INT_RC_MIN = 400,
  MDS_INT_RC_DIRECT_SEND_FAIL,
  /* Add some more here , Fix me */
} MDS_INT_RETURN_TYPES;

#endif  // MDS_MDS_DT2C_H_
