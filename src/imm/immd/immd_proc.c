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

/*****************************************************************************
  FILE NAME: immd_proc.c

  DESCRIPTION: IMMD Event handling routines
******************************************************************************/

#include "immd.h"

void immd_proc_rebroadcast_fevs(IMMD_CB *cb, uint16_t back_count)
{
	IMMSV_EVT send_evt;
	IMMSV_FEVS *old_msg = NULL;
	assert(back_count > 0);
	TRACE_5("Re-broadcast the last %u fevs messages received over mbcpsv",
		back_count);
	do {
		old_msg = immd_db_get_fevs(cb, back_count);
		if (old_msg) {
			TRACE_5("Resend message no %llu",
				old_msg->sender_count);
			memset(&send_evt, 0, sizeof(IMMSV_EVT));
			send_evt.type = IMMSV_EVT_TYPE_IMMD;
			send_evt.info.immd.type = 0;
			send_evt.info.immd.info.fevsReq.sender_count =
			    old_msg->sender_count;
			send_evt.info.immd.info.fevsReq.reply_dest =
			    old_msg->reply_dest;
			send_evt.info.immd.info.fevsReq.client_hdl =
			    old_msg->client_hdl;
			send_evt.info.immd.info.fevsReq.msg.size =
			    old_msg->msg.size;
			send_evt.info.immd.info.fevsReq.msg.buf =
			    old_msg->msg.buf;

			if (immd_evt_proc_fevs_req(cb, &(send_evt.info.immd),
						   NULL,
						   false) != NCSCC_RC_SUCCESS) {
				LOG_ER("Failed to re-send FEVS message %llu",
				       old_msg->sender_count);
			}
		}

		--back_count;
	} while (back_count != 0);
}

void immd_proc_immd_reset(IMMD_CB *cb, bool active)
{
	IMMSV_EVT send_evt;
	IMMD_MBCSV_MSG mbcp_msg;
	uint32_t proc_rc;
	TRACE_ENTER();

	if (active) {
		LOG_ER(
		    "Active IMMD has to restart the IMMSv. All IMMNDs will restart");
		/* Make standby aware of IMMND reset order. */
		memset(&mbcp_msg, 0, sizeof(IMMD_MBCSV_MSG));
		mbcp_msg.type = IMMD_A2S_MSG_RESET;

		/*Checkpoint the dissapointing reset message to standby
		  director. Syncronous call=>waits for ack. We do not check
		  the result because we can not do anything about an error in
		  notifying IMMD standby here. The immd_mbcsv_sync_update
		  will also log the error.
		*/
		immd_mbcsv_sync_update(cb, &mbcp_msg);

		/*Restart any IMMNDs that are not in epoch 0. */
		memset(&send_evt, 0, sizeof(IMMSV_EVT));
		send_evt.type = IMMSV_EVT_TYPE_IMMND;
		send_evt.info.immnd.type = IMMND_EVT_D2ND_RESET;
		proc_rc =
		    immd_mds_bcast_send(cb, &send_evt, NCSMDS_SVC_ID_IMMND);
		if (proc_rc != NCSCC_RC_SUCCESS) {
			LOG_ER(
			    "Failed to broadcast RESET message to IMMNDs, exiting");
			exit(1);
		}
	} else {
		LOG_ER(
		    "Standby IMMD recieved reset message. All IMMNDs will restart.");
	}

	if (cb->mRulingEpoch <= 1) {
		/* The cluster has not yet successfully come up once => dont
		   exit the IMMD because the IMMD is not restart monitored in
		   gap between NID and AMF.
		*/
		LOG_NO("Cluster failed to load => IMMDs will not exit.");
		goto dont_exit;
	}

	/*immd_immnd_info_tree_cleanup(cb); */
	/*Let the immnd tree keep track of what is hapening. IMMNDs that have
	   just restarted may not need to restart and re-attach again. */

	if (cb->mRim !=
	    SA_IMM_KEEP_REPOSITORY /* && changed since last dump */) {
		// if(active) {
		//	sleep(3);
		//}
		LOG_ER(
		    "IMM RELOAD with NO persistent back end => ensure cluster restart by IMMD exit at both SCs, exiting");
		exit(1);
	} else {
		/*LOG_WA("IMM RELOAD with persistent back end => No need to
		 * restart cluster");*/
		LOG_ER(
		    "IMM RELOAD  => ensure cluster restart by IMMD exit at both SCs, exiting");
		/* In theory we should be able to reload from PBE without
		   cluster restart. But implementer info is lost and non
		   persistent runtime objects/attributes are lost, so we can not
		   hope to achieve transparent resurrect. Thus we must reject
		   resurrect attempts from IMMA clients after a reload, even if
		   the reload was from PBE. Until such resurrect rejection is
		   implemented, we also escalate reload from PBE to cluster
		   restart. The PBE still fullfills its major function, that of
		   not loosing any PERSISTENT state in the face of a cluster
		   restart.
		*/
		exit(1);
	}

dont_exit:

	/* Reset relevant parts of of IMMD CB so we get a cluster wide restart
	   of IMMNDs.
	 */

	cb->mRulingEpoch = 0;
	cb->immnd_coord = 0;
	cb->payload_coord_dest = 0L;
	cb->fevsSendCount = 0LL;

	cb->locPbe.epoch = 0;
	cb->locPbe.maxCcbId = 0;
	cb->locPbe.maxCommitTime = 0;
	cb->locPbe.maxWeakCcbId = 0LL;
	cb->locPbe.maxWeakCommitTime = 0;
	cb->remPbe = cb->locPbe;
	cb->m2PbeCanLoad = false;
	cb->m2PbeExtraWait = false;

	cb->mRim = SA_IMM_INIT_FROM_FILE; /* Reset to default since we are
					     reloading. */
	/* Discard immnd node info ? */

	TRACE_LEAVE();
}

void immd_proc_abort_sync(IMMD_CB *cb, IMMD_IMMND_INFO_NODE *coord)
{
	MDS_DEST key;
	IMMD_IMMND_INFO_NODE *immnd_info_node = NULL;
	TRACE_ENTER();
	memset(&key, 0, sizeof(MDS_DEST));

	if (!coord) { /* coord is NULL => first check if there was ongoing sync.
		       */
		immd_immnd_info_node_getnext(&cb->immnd_tree, &key,
					     &immnd_info_node);
		TRACE_5("RulingEpoch(%u)", cb->mRulingEpoch);

		while (immnd_info_node) {
			key = immnd_info_node->immnd_dest;
			TRACE_5("immnd_info_node->epoch(%u) syncStarted:%u",
				immnd_info_node->epoch,
				immnd_info_node->syncStarted);
			if (immnd_info_node->syncStarted) {
				if (immnd_info_node->isCoord) {
					coord = immnd_info_node;
					break; /* out of while */
				} else {
					LOG_ER(
					    "Non coord has sync marker, removing it");
					immnd_info_node->syncStarted = false;
				}
			}
			immd_immnd_info_node_getnext(&cb->immnd_tree, &key,
						     &immnd_info_node);
		}
	}

	if (coord && coord->syncStarted) {
		memset(&key, 0, sizeof(MDS_DEST));
		immd_immnd_info_node_getnext(&cb->immnd_tree, &key,
					     &immnd_info_node);

		while (immnd_info_node) {
			key = immnd_info_node->immnd_dest;
			TRACE_5(
			    "cb->mRulingEpoch(%u) == immnd_info_node->epoch(%u) + 1)",
			    cb->mRulingEpoch, immnd_info_node->epoch);
			if (cb->mRulingEpoch == (immnd_info_node->epoch + 1)) {
				++(immnd_info_node->epoch);
				TRACE_5("Incremented epoch to %u for a node %x",
					immnd_info_node->epoch,
					immnd_info_node->immnd_key);
			}
			immd_immnd_info_node_getnext(&cb->immnd_tree, &key,
						     &immnd_info_node);
		}
		coord->syncStarted = false;
	}

	TRACE_LEAVE();
}

bool immd_proc_elect_coord(IMMD_CB *cb, bool new_active)
{
	IMMSV_EVT send_evt;
	IMMD_MBCSV_MSG mbcp_msg;
	MDS_DEST key;
	IMMD_IMMND_INFO_NODE *immnd_info_node = NULL;
	bool self_re_elect = false;

	TRACE_ENTER();
	memset(&key, 0, sizeof(MDS_DEST));
	unsigned int maxEpoch = 0;

	/*First verify that we dont have any coord. */
	immd_immnd_info_node_getnext(&cb->immnd_tree, &key, &immnd_info_node);
	while (immnd_info_node) {
		key = immnd_info_node->immnd_dest;
		if (maxEpoch < immnd_info_node->epoch) {
			maxEpoch = immnd_info_node->epoch;
		}
		if (immnd_info_node->isCoord) {
			if (maxEpoch > immnd_info_node->epoch) {
				/* TODO: this check is not even reliable since
				   maxEpoch is not obtained first. */
				LOG_ER(
				    "Epoch(%u) for current coord is less than "
				    "the epoch of some other node (%u)",
				    immnd_info_node->epoch, maxEpoch);
				immd_proc_immd_reset(cb, true);
			}

			if (new_active) {
				/*When new_active is true, this immd has just
				   been appointed active. In this case re-elect
				   the local coord to bypass the problem of the
				   old IMMD having elected the same coord, but
				   only managed to send the mbcp message and not
				   the new-coord message to the IMMND. EXCEPT
				   when there is an ongoing sync and the coord
				   is colocated with new active. Then we are (a)
				   sure there is no missed new-coord message and
				   (b) we want to avoid disturbing the already
				   started sync, if possible. And EXCEPT for
				   switch-over/si-swap, since there is no reason
				   for the old immnd to have gone down.
				*/
				if ((cb->immnd_coord == cb->node_id) &&
				    immnd_info_node->syncStarted) {
					osafassert(immnd_info_node->immnd_key ==
						   cb->node_id);
				} else if (cb->immd_remote_up) {
					/* The standby IMMD is still up See
					 * #1773 and #1819 */
					LOG_WA(
					    "IMMD not re-electing coord for switch-over (si-swap) coord at (%x)",
					    cb->immnd_coord);
				} else {
					/* Re-elect local coord. See #578 */
					if (immnd_info_node->immnd_key !=
					    cb->node_id) {
						if (cb->mScAbsenceAllowed) {
							LOG_WA(
							    "ScAbsenceAllowed(%u), failover after SC-absence => coord at payload",
							    cb->mScAbsenceAllowed);
							break;
						}
						LOG_ER(
						    "Changing IMMND coord while old coord is still up!");
						/* Could theoretically happen if
						   remote IMMD is down, i.e.
						   failover, but MDS has not yet
						   provided IMMND DOWN for that
						   node. Should never happen in
						   a TIPC system. Could perhaps
						   happen in other systems. If
						   so we need to implement a way
						   to "shoot down" the old IMMND
						   coord, which is doomed anyway
						   since the IMMD
						   (nonrestartable) has crashed
						   on that node.
						 */
					}
					self_re_elect = true;
					immnd_info_node->isCoord = false;
					immnd_info_node =
					    NULL; /*Force new coord election. */
				}
			}
			break; // out of while
		}
		immd_immnd_info_node_getnext(&cb->immnd_tree, &key,
					     &immnd_info_node);
	}

	if (immnd_info_node) {
		TRACE_5("Elect coord: Coordinator node already exists: %x",
			immnd_info_node->immnd_key);
		osafassert(
		    (cb->mRulingEpoch == immnd_info_node->epoch) ||
		    (immnd_info_node->syncStarted &&
		     (cb->mRulingEpoch == (immnd_info_node->epoch + 1))));
		/* If ruling epoch != coord epoch, there must be an on-going
		   sync and the ruling epoch is one step higher than the coord
		   epoch.
		 */
	} else {
		/* Try to elect a new coord. */
		IMMD_IMMND_INFO_NODE *candidate_coord_node = NULL;
		cb->payload_coord_dest = 0LL;
		memset(&key, 0, sizeof(MDS_DEST));
		immd_immnd_info_node_getnext(&cb->immnd_tree, &key,
					     &immnd_info_node);

		// Election priority:
		// 1) Coordinator on active node
		// 2) Coordinator on standby node
		// 3) Coordinator on PL node if SC absence is allowed.
		while (immnd_info_node) {
			key = immnd_info_node->immnd_dest;
			if ((immnd_info_node->isOnController) &&
			    (immnd_info_node->epoch == cb->mRulingEpoch)) {
				candidate_coord_node = immnd_info_node;
				immnd_info_node->isCoord = true;
				if (immnd_info_node->immnd_key == cb->node_id) {
					/* Found a new candidate on active SC */
					break;
				}
			}

			immd_immnd_info_node_getnext(&cb->immnd_tree, &key,
						     &immnd_info_node);
		}

		immnd_info_node = candidate_coord_node;
		if (!immnd_info_node && cb->mScAbsenceAllowed) {
			/* If SC absence is allowed and no SC based IMMND is
			   available then elect an IMMND coord at a payload.
			   Note this means that an IMMND at a payload may be
			   elected coord even if one or both SCs are available,
			   but no synced IMMND is avaialble at any SC.
			*/
			memset(&key, 0, sizeof(MDS_DEST));
			immd_immnd_info_node_getnext(&cb->immnd_tree, &key,
						     &immnd_info_node);
			while (immnd_info_node) {
				key = immnd_info_node->immnd_dest;
				if (immnd_info_node->epoch ==
				    cb->mRulingEpoch) {
					/*We found a new candidate for
					 * cordinator */
					immnd_info_node->isCoord = true;
					cb->payload_coord_dest =
					    immnd_info_node->immnd_dest;
					LOG_NO("Coord elected at payload:%x",
					       immnd_info_node->immnd_key);
					break;
				} else {
					LOG_IN(
					    "Payload %x rejected as coord, epoch(%u) != rulingEpoch(%u)",
					    immnd_info_node->immnd_key,
					    immnd_info_node->epoch,
					    cb->mRulingEpoch);
				}
				immd_immnd_info_node_getnext(
				    &cb->immnd_tree, &key, &immnd_info_node);
			}
		}

		if (!immnd_info_node) {
			LOG_ER(
			    "Failed to find candidate for new IMMND coordinator (ScAbsenceAllowed:%u RulingEpoch:%u",
			    cb->mScAbsenceAllowed, cb->mRulingEpoch);

			TRACE_LEAVE();
			immd_proc_immd_reset(cb, true);
			return false;
		}

		if (self_re_elect) {
			/* Ensure we re-elected ourselves. */
			osafassert(immnd_info_node->immnd_key == cb->node_id);
			LOG_NO("Coord re-elected, resides at %x",
			       immnd_info_node->immnd_key);

		} else {
			LOG_NO("New coord elected, resides at %x",
			       immnd_info_node->immnd_key);
		}

		cb->immnd_coord = immnd_info_node->immnd_key;
		if (!cb->is_rem_immnd_up) {
			if (cb->immd_remote_id &&
			    m_IMMND_IS_ON_SCXB(
				cb->immd_remote_id,
				immd_get_node_id_from_mds_dest(
				    immnd_info_node->immnd_dest))) {
				cb->is_rem_immnd_up =
				    true; /*ABT BUGFIX 080905 */
				cb->rem_immnd_dest =
				    immnd_info_node->immnd_dest;
				TRACE_5(
				    "Corrected: IMMND process is on STANDBY Controller");
			}
		}

		memset(&send_evt, 0, sizeof(IMMSV_EVT));
		memset(&mbcp_msg, 0, sizeof(IMMD_MBCSV_MSG));
		send_evt.type = IMMSV_EVT_TYPE_IMMND;
		/*This event type should be given a more appropriate name. */
		send_evt.info.immnd.type = IMMND_EVT_D2ND_INTRO_RSP;
		send_evt.info.immnd.info.ctrl.nodeId =
		    immnd_info_node->immnd_key;
		send_evt.info.immnd.info.ctrl.rulingEpoch = cb->mRulingEpoch;
		send_evt.info.immnd.info.ctrl.canBeCoord =
		    (immnd_info_node->isOnController)
			? 1
			: (cb->mScAbsenceAllowed) ? 4 : 0;
		send_evt.info.immnd.info.ctrl.ndExecPid =
		    immnd_info_node->immnd_execPid;
		send_evt.info.immnd.info.ctrl.isCoord = true;
		send_evt.info.immnd.info.ctrl.fevsMsgStart = cb->fevsSendCount;
		send_evt.info.immnd.info.ctrl.syncStarted = false;
		send_evt.info.immnd.info.ctrl.nodeEpoch =
		    immnd_info_node->epoch;
		send_evt.info.immnd.info.ctrl.pbeEnabled =
		    (cb->mRim == SA_IMM_KEEP_REPOSITORY);

		mbcp_msg.type = IMMD_A2S_MSG_INTRO_RSP;
		mbcp_msg.info.ctrl = send_evt.info.immnd.info.ctrl;
		/*Checkpoint the new coordinator message to standby director.
		   Syncronous call=>wait for ack */
		if (immd_mbcsv_sync_update(cb, &mbcp_msg) != NCSCC_RC_SUCCESS) {
			LOG_ER(
			    "Mbcp message designating new IMMD coord failed => "
			    "standby IMMD could not be notified. Failover to STBY, exiting");
			exit(1);
		}

		/*Notify the remining controller based IMMND that it is the new
		   IMMND coordinator. */
		if (immd_mds_msg_send(cb, NCSMDS_SVC_ID_IMMND,
				      immnd_info_node->immnd_dest,
				      &send_evt) != NCSCC_RC_SUCCESS) {
			LOG_ER(
			    "Failed to send MDS message designating new IMMND coord, exiting");
			exit(1);
		}
	}

	immd_cb_dump();
	TRACE_LEAVE();
	return true;
}

void immd_proc_arbitrate_2pbe_preload(IMMD_CB *cb)
{
	IMMSV_EVT send_evt;
	IMMD_MBCSV_MSG mbcp_msg;
	IMMD_IMMND_INFO_NODE *local_immnd_sc = NULL;
	IMMD_IMMND_INFO_NODE *remote_immnd_sc = NULL;
	IMMD_IMMND_INFO_NODE *immnd_info_node = NULL;
	MDS_DEST key;
	bool chooseLoc = false;
	bool chooseRem = false;
	TRACE_ENTER();

	osafassert(cb->m2PbeCanLoad);
	if (cb->immnd_coord) {
		LOG_WA(
		    "immd_proc_arbitrate_2pbe_preload: Coord already selected.");
		return;
	}

	memset(&key, 0, sizeof(MDS_DEST));
	immd_immnd_info_node_getnext(&cb->immnd_tree, &key, &immnd_info_node);
	while (immnd_info_node) {
		key = immnd_info_node->immnd_dest;

		if (immnd_info_node->immnd_dest == cb->loc_immnd_dest) {
			local_immnd_sc = immnd_info_node;
		} else if (immnd_info_node->immnd_dest == cb->rem_immnd_dest) {
			remote_immnd_sc = immnd_info_node;
		}
		immd_immnd_info_node_getnext(&cb->immnd_tree, &key,
					     &immnd_info_node);
	}

	if (!remote_immnd_sc) {
		LOG_WA(
		    "REMOTE IMMND is unavailable or down, 2PBE arbitration defaults to local IMMND");
		cb->remPbe.epoch = 0;
		cb->remPbe.maxCcbId = 0;
		cb->remPbe.maxCommitTime = 0;
		/*return;*/
	} else if (!local_immnd_sc) {
		LOG_ER("LOCAL IMMND is down!,  can not arbitrate");
		cb->locPbe.epoch = 0;
		cb->locPbe.maxCcbId = 0;
		cb->locPbe.maxCommitTime = 0;
		cb->m2PbeCanLoad = false;
		return;
	}

	if (cb->locPbe.epoch > cb->remPbe.epoch) {
		chooseLoc = true;
		LOG_NO(
		    "2PBE choosing LOCAL immnd as coord due to higher epoch %u > %u",
		    cb->locPbe.epoch, cb->remPbe.epoch);
		goto decided;
	}

	if (cb->locPbe.epoch < cb->remPbe.epoch) {
		chooseRem = true;
		LOG_NO(
		    "2PBE choosing REMOTE immnd as coord due to higher epoch %u > %u",
		    cb->remPbe.epoch, cb->locPbe.epoch);
		goto decided;
	}

	/* Epochs identical, check regular ccbs */
	if (cb->locPbe.maxCcbId > cb->remPbe.maxCcbId) {
		chooseLoc = true;
		LOG_NO(
		    "2PBE choosing LOCAL immnd as coord due to higher ccbId %u > %u",
		    cb->locPbe.maxCcbId, cb->remPbe.maxCcbId);
		goto decided;
	}

	if (cb->locPbe.maxCcbId < cb->remPbe.maxCcbId) {
		chooseRem = true;
		LOG_NO(
		    "2PBE choosing REMOTE immnd as coord due to higher ccbId %u > %u",
		    cb->remPbe.maxCcbId, cb->locPbe.maxCcbId);
		goto decided;
	}

	/* Epochs and regular ccbs identical, check PRTOs & classes */
	if (cb->locPbe.maxWeakCcbId > cb->remPbe.maxWeakCcbId) {
		chooseLoc = true;
		LOG_NO(
		    "2PBE choosing LOCAL immnd as coord due to higher weak ccbId %llu > %llu",
		    cb->locPbe.maxWeakCcbId, cb->remPbe.maxWeakCcbId);
		goto decided;
	}

	if (cb->locPbe.maxWeakCcbId < cb->remPbe.maxWeakCcbId) {
		chooseRem = true;
		LOG_NO(
		    "2PBE choosing REMOTE immnd as coord due to higher weak ccbId %llu > %llu",
		    cb->remPbe.maxWeakCcbId, cb->locPbe.maxWeakCcbId);
		goto decided;
	}

	/* Epochs and all ccbs identical check comit times. */

	if (cb->locPbe.maxCommitTime > cb->remPbe.maxCommitTime) {
		chooseLoc = true;
		LOG_NO(
		    "2PBE choosing LOCAL immnd as coord due to higher commitTime %u > %u",
		    cb->locPbe.maxCommitTime, cb->remPbe.maxCommitTime);
		goto decided;
	}

	if (cb->locPbe.maxCommitTime < cb->remPbe.maxCommitTime) {
		chooseRem = true;
		LOG_NO(
		    "2PBE choosing REMOTE immnd as coord due to higher commitTime %u > %u",
		    cb->remPbe.maxCommitTime, cb->locPbe.maxCommitTime);
		goto decided;
	}

	if (cb->locPbe.maxWeakCommitTime > cb->remPbe.maxWeakCommitTime) {
		chooseLoc = true;
		LOG_NO(
		    "2PBE choosing LOCAL immnd as coord due to higher commitTime %u > %u",
		    cb->locPbe.maxWeakCommitTime, cb->remPbe.maxWeakCommitTime);
		goto decided;
	}

	if (cb->locPbe.maxWeakCommitTime < cb->remPbe.maxWeakCommitTime) {
		chooseRem = true;
		LOG_NO(
		    "2PBE choosing REMOTE immnd as coord due to higher commitTime %u > %u",
		    cb->remPbe.maxWeakCommitTime, cb->locPbe.maxWeakCommitTime);
		goto decided;
	}

	LOG_NO(
	    "2PBE choosing REMOTE immnd as coord Equal stats epoch:%u Ccb:%u Time:%u",
	    cb->remPbe.epoch, cb->remPbe.maxCcbId, cb->remPbe.maxCommitTime);
	chooseRem = true;

decided:

	if (chooseRem) {
		if (!remote_immnd_sc) {
			LOG_WA(
			    "Stats dictate remote immnd/pbe should load, but it is down");
			return;
		}
	} else if (chooseLoc) {
		if (!local_immnd_sc) {
			LOG_WA(
			    "Stats dictate local immnd/pbe should load, but it is down");
			return;
		}
	}

	/* First ack intro to node not chosen as coord.
	   This increases chances that it will be ready as loading client.
	*/

	immnd_info_node = (chooseRem) ? local_immnd_sc : remote_immnd_sc;

	if (immnd_info_node) {
		memset(&send_evt, 0, sizeof(IMMSV_EVT));
		memset(&mbcp_msg, 0, sizeof(IMMD_MBCSV_MSG));
		send_evt.type = IMMSV_EVT_TYPE_IMMND;
		send_evt.info.immnd.type = IMMND_EVT_D2ND_INTRO_RSP;
		send_evt.info.immnd.info.ctrl.nodeId =
		    immnd_info_node->immnd_key;
		send_evt.info.immnd.info.ctrl.rulingEpoch = cb->mRulingEpoch;
		send_evt.info.immnd.info.ctrl.canBeCoord = true;
		send_evt.info.immnd.info.ctrl.ndExecPid =
		    immnd_info_node->immnd_execPid;
		send_evt.info.immnd.info.ctrl.isCoord = false;
		send_evt.info.immnd.info.ctrl.fevsMsgStart = cb->fevsSendCount;
		send_evt.info.immnd.info.ctrl.syncStarted = false;
		send_evt.info.immnd.info.ctrl.nodeEpoch =
		    immnd_info_node->epoch;
		send_evt.info.immnd.info.ctrl.pbeEnabled =
		    (cb->mRim == SA_IMM_KEEP_REPOSITORY);
		mbcp_msg.type = IMMD_A2S_MSG_INTRO_RSP;
		mbcp_msg.info.ctrl = send_evt.info.immnd.info.ctrl;
		/*Checkpoint the non coordinator message to standby director. */
		if (immd_mbcsv_sync_update(cb, &mbcp_msg) != NCSCC_RC_SUCCESS) {
			LOG_ER("Mbcp message about NON chosen IMMND  failed");
			/* ABT This case is not handled well. */
		}

		if (immd_mds_msg_send(cb, NCSMDS_SVC_ID_IMMND,
				      immnd_info_node->immnd_dest,
				      &send_evt) != NCSCC_RC_SUCCESS) {
			LOG_ER(
			    "Failed to send MDS message about NON chosen IMMND failed");
		}

		usleep(250000); /* Just to increase chances of non-coord being
				   ready to load. */
	}

	/* Elect coord */
	immnd_info_node = (chooseRem) ? remote_immnd_sc : local_immnd_sc;
	osafassert(immnd_info_node);
	immnd_info_node->isCoord = true;
	cb->immnd_coord = immnd_info_node->immnd_key;

	memset(&send_evt, 0, sizeof(IMMSV_EVT));
	memset(&mbcp_msg, 0, sizeof(IMMD_MBCSV_MSG));
	send_evt.type = IMMSV_EVT_TYPE_IMMND;
	send_evt.info.immnd.type = IMMND_EVT_D2ND_INTRO_RSP;
	send_evt.info.immnd.info.ctrl.nodeId = immnd_info_node->immnd_key;
	send_evt.info.immnd.info.ctrl.rulingEpoch = cb->mRulingEpoch;
	send_evt.info.immnd.info.ctrl.canBeCoord = true;
	send_evt.info.immnd.info.ctrl.ndExecPid =
	    immnd_info_node->immnd_execPid;
	send_evt.info.immnd.info.ctrl.isCoord = true;
	send_evt.info.immnd.info.ctrl.fevsMsgStart = cb->fevsSendCount;
	send_evt.info.immnd.info.ctrl.syncStarted = false;
	send_evt.info.immnd.info.ctrl.nodeEpoch = immnd_info_node->epoch;
	send_evt.info.immnd.info.ctrl.pbeEnabled =
	    (cb->mRim == SA_IMM_KEEP_REPOSITORY);

	mbcp_msg.type = IMMD_A2S_MSG_INTRO_RSP;
	mbcp_msg.info.ctrl = send_evt.info.immnd.info.ctrl;
	/*Checkpoint the new coordinator message to standby director.
	  Syncronous call=>wait for ack */
	if (immd_mbcsv_sync_update(cb, &mbcp_msg) != NCSCC_RC_SUCCESS) {
		LOG_ER("Mbcp message about NON chosen IMMND  failed");
		/* This case is not handled well. */
	}

	if (immd_mds_msg_send(cb, NCSMDS_SVC_ID_IMMND,
			      immnd_info_node->immnd_dest,
			      &send_evt) != NCSCC_RC_SUCCESS) {
		LOG_ER(
		    "Failed to send MDS message about NON chosen IMMND failed");
	}

	TRACE_LEAVE();
}

/****************************************************************************
 * Name          : immd_process_immnd_down
 *
 * Description   : This routine will process the IMMND down event.
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 * Notes         : None.
 *****************************************************************************/
uint32_t immd_process_immnd_down(IMMD_CB *cb, IMMD_IMMND_INFO_NODE *immnd_info,
				 bool active)
{
	IMMSV_EVT send_evt;
	NCS_UBAID uba;
	char *tmpData = NULL;
	bool coord_exists = true; /* Assumption at this point.*/
	TRACE_ENTER();

	TRACE_5("immd_process_immnd_down pid:%u on-active:%u "
		"cb->immnd_coord:%x",
		immnd_info->immnd_execPid, active, cb->immnd_coord);
	if (active) {
		uba.start = NULL;

		if (immnd_info->isCoord) {
			coord_exists = false;
			LOG_WA("IMMND coordinator at %x apparently crashed => "
			       "electing new coord",
			       immnd_info->immnd_key);
			if (immnd_info->syncStarted) {
				/* Sync was started, but never completed by
				   coord. Bump up epoch for all nodes that where
				   already members. Broadcast sync abort
				   message. (Also resets:
				   immnd_info->syncStarted = false;) */
				immd_proc_abort_sync(cb, immnd_info);
			}
			immnd_info->isCoord = 0;
			immnd_info->isOnController = 0;
			immnd_info->epoch = 0; /* needed ? */
			cb->immnd_coord = 0;
			cb->payload_coord_dest = 0L;
			coord_exists = immd_proc_elect_coord(cb, false);
		}
	} else {
		/* Check if it was the IMMND on the active controller that went
		 * down. */
		if (immnd_info->immnd_key == cb->immd_remote_id) {
			LOG_WA("IMMND DOWN on active controller %x "
			       "detected at standby immd!! %x. "
			       "Possible failover",
			       immnd_info->immnd_key, cb->immd_self_id);

			if (immnd_info->isCoord && immnd_info->syncStarted) {
				immd_proc_abort_sync(cb, immnd_info);
			}

			immd_proc_rebroadcast_fevs(cb, 2);
		}
	}

	if (active || !cb->immd_remote_up) {
		/*
		 ** HAFE - Let IMMND subscribe for IMMND up/down events instead?
		 ** ABT - Not for now. IMMND up/down are only subscribed by
		 *IMMD. * Active IMMD will inform other IMMNDs about the down
		 *IMMND. * If the active is down, the standby (has not become
		 *active yet) * will do the job.
		 */
		if (coord_exists) {

			TRACE_5(
			    "Notify all remaining IMMNDs of the departed IMMND");
			memset(&send_evt, 0, sizeof(IMMSV_EVT));
			send_evt.type = IMMSV_EVT_TYPE_IMMND;
			send_evt.info.immnd.type = IMMND_EVT_D2ND_DISCARD_NODE;
			send_evt.info.immnd.info.ctrl.nodeId =
			    immnd_info->immnd_key;
			send_evt.info.immnd.info.ctrl.ndExecPid =
			    immnd_info->immnd_execPid;

			osafassert(ncs_enc_init_space(&uba) ==
				   NCSCC_RC_SUCCESS);
			osafassert(immsv_evt_enc(&send_evt, &uba) ==
				   NCSCC_RC_SUCCESS);

			int32_t size = uba.ttl;
			tmpData = malloc(size);
			osafassert(tmpData);
			char *data =
			    m_MMGR_DATA_AT_START(uba.start, size, tmpData);

			memset(&send_evt, 0,
			       sizeof(IMMSV_EVT)); /*No ponters=>no leak */
			send_evt.type = IMMSV_EVT_TYPE_IMMD;
			send_evt.info.immd.type = 0;
			send_evt.info.immd.info.fevsReq.msg.size = size;
			send_evt.info.immd.info.fevsReq.msg.buf = data;

			if (immd_evt_proc_fevs_req(cb, &(send_evt.info.immd),
						   NULL,
						   false) != NCSCC_RC_SUCCESS) {
				LOG_ER(
				    "Failed to send discard node message over FEVS");
			}

			free(tmpData);
		}
	} else {
		/* Standby NOT immediately sending D2ND_DISCARD_NODE. But will
		 * record any IMMND down event. The active IMMD may never have
		 * time to generate the discard node message for the IMMND if
		 * the active IMMD goes down at the same time with the IMMND.
		 * This will be detected if this (standby) IMMD becomes active
		 * in close time proximity. See immd_pending_payload_discards()
		 * below.
		 */

		LOG_IN("Standby IMMD recording IMMND DOWN for node %x",
		       immnd_info->immnd_key);
		IMMD_IMMND_DETACHED_NODE *detached_node =
		    calloc(1, sizeof(IMMD_IMMND_DETACHED_NODE));
		osafassert(detached_node);
		detached_node->node_id = immnd_info->immnd_key;
		detached_node->immnd_execPid = immnd_info->immnd_execPid;
		detached_node->epoch = immnd_info->epoch;
		detached_node->next = cb->detached_nodes;
		cb->detached_nodes = detached_node;
	}

	/*We remove the node for the lost IMMND on both active and standby. */
	TRACE_5("Removing node id:%x", immnd_info->immnd_key);
	immd_immnd_info_node_delete(cb, immnd_info);
	immd_cb_dump();
	TRACE_LEAVE();
	return NCSCC_RC_SUCCESS;
}

/****************************************************************************
 * Name          : immd_pending_discards
 *
 * Description   : Send possibly redundant discard-node message to IMMNDs for
 *                 IMMNDs that have departed and not returned.
 *                 This is needed to plug the small hole that exists in the
 *                 handling of IMMND node down, when the current active IMMD
 *                 is being taken down.
 *
 *                 The active IMMD may then be pulled down after having
 *                 received the IMMND MDS down event for the IMMNDs, but
 *                 before having created or sent the fevs message broadcasting
 *                 each node down to the IMMND cluster.
 *
 *                 The list of detached nodes is screened for having occcurred
 *                 in the current epoch. This is an extra guard against the new
 *                 active shooting down a recently restarted payload. The list
 *                 is also pruned in immd_sbevt.c when it receives info about
 *                 a IMMND having re-joined.
 *
 *                 This function should only be invoked by the just recently
 *                 newly active IMMD. It is only relevant for fail-over, not
 *                 for switch-over (si-swap) since for a switch-over the old
 *                 active would never drop sending node-down messages.
 *
 * Return Values : -
 *
 * Notes         : None.
 *****************************************************************************/
void immd_pending_discards(IMMD_CB *cb)
{
	IMMSV_EVT send_evt;
	char *tmpData = NULL;
	NCS_UBAID uba;
	TRACE_ENTER();

	osafassert(cb->ha_state == SA_AMF_HA_ACTIVE);

	IMMD_IMMND_DETACHED_NODE *detached_node = cb->detached_nodes;

	while (detached_node) {
		if (!cb->immd_remote_up &&
		    detached_node->epoch == cb->mRulingEpoch) {
			LOG_NO(
			    "Old active NOT present => send discard node payload %x",
			    detached_node->node_id);

			memset(&send_evt, 0, sizeof(IMMSV_EVT));
			send_evt.type = IMMSV_EVT_TYPE_IMMND;
			send_evt.info.immnd.type = IMMND_EVT_D2ND_DISCARD_NODE;
			send_evt.info.immnd.info.ctrl.nodeId =
			    detached_node->node_id;
			send_evt.info.immnd.info.ctrl.ndExecPid =
			    detached_node->immnd_execPid;

			osafassert(ncs_enc_init_space(&uba) ==
				   NCSCC_RC_SUCCESS);
			osafassert(immsv_evt_enc(&send_evt, &uba) ==
				   NCSCC_RC_SUCCESS);

			int32_t size = uba.ttl;
			tmpData = malloc(size);
			osafassert(tmpData);
			char *data =
			    m_MMGR_DATA_AT_START(uba.start, size, tmpData);

			memset(&send_evt, 0, sizeof(IMMSV_EVT));
			send_evt.type = IMMSV_EVT_TYPE_IMMD;
			send_evt.info.immd.type = 0;
			send_evt.info.immd.info.fevsReq.msg.size = size;
			send_evt.info.immd.info.fevsReq.msg.buf = data;

			if (immd_evt_proc_fevs_req(cb, &(send_evt.info.immd),
						   NULL,
						   false) != NCSCC_RC_SUCCESS) {
				LOG_ER(
				    "Failed to send discard node message over FEVS");
			}

			free(tmpData);
		}

		LOG_IN("Removing pending discard for node:%x epoch:%u",
		       detached_node->node_id, detached_node->epoch);
		cb->detached_nodes = detached_node->next;
		detached_node->next = NULL;
		free(detached_node);
		detached_node = cb->detached_nodes;
	}
}

/****************************************************************************
 * Name          : immd_cb_dump
 *
 * Description   : This routine is used for debugging.
 *
 * Return Values : NCSCC_RC_SUCCESS/Error.
 *
 * Notes         : None.
 *****************************************************************************/
void immd_cb_dump(void)
{
	IMMD_CB *cb = immd_cb;

	TRACE_5("*****************Printing IMMD CB Dump****************");
	TRACE_5("  MDS Handle:             %x", (uint32_t)cb->mds_handle);
	TRACE_5("  IMMD State:  %d IMMND-coord-Id: %x", cb->ha_state,
		cb->immnd_coord);
	TRACE_5("  Sync update count %u", cb->immd_sync_cnt);
	TRACE_5("  Fevs send count %llu", cb->fevsSendCount);
	TRACE_5("  RULING EPOCH: %u RIM:%s", cb->mRulingEpoch,
		(cb->mRim == SA_IMM_KEEP_REPOSITORY) ? "KEEP_REP" : "FILE");

	if (cb->is_immnd_tree_up) {
		TRACE_5("+++++++++++++IMMND Tree is UP++++++++++++++++++++");
		TRACE_5("Number of nodes in IMMND Tree:  %u",
			cb->immnd_tree.n_nodes);

		/* Print the IMMND Details */
		if (cb->immnd_tree.n_nodes > 0) {
			MDS_DEST key;
			IMMD_IMMND_INFO_NODE *immnd_info_node;
			memset(&key, 0, sizeof(MDS_DEST));

			immd_immnd_info_node_getnext(&cb->immnd_tree, &key,
						     &immnd_info_node);
			while (immnd_info_node) {
				key = immnd_info_node->immnd_dest;
				TRACE_5(
				    "   -------------------------------------------");
				TRACE_5("   MDS Node ID:  = %x",
					m_NCS_NODE_ID_FROM_MDS_DEST(
					    immnd_info_node->immnd_dest));
				TRACE_5(
				    "   Pid:%u Epoch:%u syncR:%u syncS:%u onCtrlr:%u isCoord:%u",
				    immnd_info_node->immnd_execPid,
				    immnd_info_node->epoch,
				    immnd_info_node->syncRequested,
				    immnd_info_node->syncStarted,
				    immnd_info_node->isOnController,
				    immnd_info_node->isCoord);

				immd_immnd_info_node_getnext(
				    &cb->immnd_tree, &key, &immnd_info_node);
			}
			TRACE_5(" End of IMMND Info");
		}
		TRACE_5(" End of IMMND info nodes");
	}

	TRACE_5("*****************End of IMMD CB Dump******************");
}
