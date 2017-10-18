############################################################################
#
# (C) Copyright 2015 The OpenSAF Foundation
# (C) Copyright 2017 Ericsson AB. All rights reserved.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
# or FITNESS FOR A PARTICULAR PURPOSE. This file and program are licensed
# under the GNU Lesser General Public License Version 2.1, February 1999.
# The complete license can be accessed from the following location:
# http://opensource.org/licenses/lgpl-license.php
# See the Copying file included with the OpenSAF distribution for full
# licensing terms.
#
# Author(s): Ericsson
#
############################################################################
# pylint: disable=unused-argument
""" CLM common utilities """
from pyosaf.saAis import saAis, SaVersionT, eSaDispatchFlagsT
from pyosaf import saClm
from pyosaf.utils import decorate, initialize_decorate


# Decorate pure saClm* API's with error-handling retry and exception raising
saClmInitialize = initialize_decorate(saClm.saClmInitialize)
saClmInitialize_3 = initialize_decorate(saClm.saClmInitialize_3)
saClmInitialize_4 = initialize_decorate(saClm.saClmInitialize_4)
saClmSelectionObjectGet = decorate(saClm.saClmSelectionObjectGet)
saClmDispatch = decorate(saClm.saClmDispatch)
saClmFinalize = decorate(saClm.saClmFinalize)
saClmClusterTrack = decorate(saClm.saClmClusterTrack)
saClmClusterNodeGet = decorate(saClm.saClmClusterNodeGet)
saClmClusterNotificationFree = decorate(saClm.saClmClusterNotificationFree)
saClmClusterTrack_4 = decorate(saClm.saClmClusterTrack_4)
saClmClusterTrackStop = decorate(saClm.saClmClusterTrackStop)
saClmClusterNotificationFree_4 = decorate(saClm.saClmClusterNotificationFree_4)
saClmClusterNodeGet_4 = decorate(saClm.saClmClusterNodeGet_4)
saClmClusterNodeGetAsync = decorate(saClm.saClmClusterNodeGetAsync)
saClmResponse_4 = decorate(saClm.saClmResponse_4)

# Create the handle
handle = saClm.SaClmHandleT()
track_function = None


def track_callback(c_notification_buffer, c_number_of_members, c_invocation_id,
                   c_root_cause_entity, c_correlation_ids, c_step,
                   c_time_supervision, c_error):
    """ This callback is invoked to get information about cluster membership
    changes in the structure to which the notificationBuffer parameter points.

    Args:
        c_notification_buffer (SaClmClusterNotificationBufferT_4): Notification
            buffer
        c_number_of_members (SaUint32T): Number of members
        c_invocation_id (SaInvocationT): Invocation id
        c_root_cause_entity (SaNameT): Root cause entity
        c_correlation_ids (SaNtfCorrelationIdsT): Correlation ids
        c_step (SaClmChangeStepT): Change step
        c_time_supervision (SaTimeT): Time supervision
        c_error (SaAisErrorT): Return code
    """

    if track_function:
        added = []
        removed = []
        step = c_step

        if step == saClm.eSaClmChangeStepT.SA_CLM_CHANGE_COMPLETED:
            notification_buffer = c_notification_buffer.contents

            i = 0
            for notification in notification_buffer.notification:
                if i == notification_buffer.numberOfItems:
                    break
                else:
                    i = i + 1

                clm_cluster_node = notification.clusterNode
                cluster_node = \
                    create_cluster_node_instance(clm_cluster_node)

                if notification.clusterChange == \
                        saClm.eSaClmClusterChangesT.SA_CLM_NODE_JOINED:
                    added.append(cluster_node)
                elif notification.clusterChange == \
                        saClm.eSaClmClusterChangesT.SA_CLM_NODE_LEFT:
                    removed.append(cluster_node)

        track_function(added, removed)


def node_get_callback(*args):
    """ Dummy function used as a callback when no proper callbacks are set """
    pass


def create_cluster_node_instance(clm_cluster_node):
    """ Create ClusterNode object from cluster node information

    Args:
        clm_cluster_node (SaClmClusterNodeT): Cluster node information

    Returns:
        ClusterNode: An object containing cluster node information
    """
    return ClusterNode(
        node_id=clm_cluster_node.nodeId,
        node_address=clm_cluster_node.nodeAddress,
        node_name=clm_cluster_node.nodeName,
        execution_environment=clm_cluster_node.executionEnvironment,
        member=clm_cluster_node.member,
        boot_timestamp=clm_cluster_node.bootTimestamp,
        initial_view_number=clm_cluster_node.initialViewNumber)


class ClusterNode(object):
    """ Class representing a CLM cluster node """
    def __init__(self, node_id, node_address, node_name, execution_environment,
                 member, boot_timestamp, initial_view_number):
        """ Constructor for ClusterNode class

        Args:
            node_id (SaClmNodeIdT): Node identifier
            node_address (SaClmNodeAddressT): Node network communication
                address
            node_name (SaNameT): Node name
            execution_environment (SaNameT): DN of the PLM execution
                environment that hosts the node
            member (SaBoolT): Indicate whether this is a CLM member node (True)
                or not (False)
            boot_timestamp (SaTimeT): Timestamp at which the node was last
                booted
            initial_view_number (SaUint64T): View number of the latest
                membership transition when this configured node joined the
                cluster membership
        """
        self.node_id = node_id
        self.node_address_value = node_address.value
        self.node_address_family = node_address.family
        self.node_name = node_name.value
        self.execution_environment = execution_environment
        self.member = member
        self.boot_timestamp = boot_timestamp
        self.initial_view_number = initial_view_number


def initialize(track_func=None):
    """ Initialize the CLM library

    Args:
        track_func (callback): Cluster track callback function
    """
    global track_function
    track_function = track_func

    # Set up callbacks for cluster membership tracking
    callbacks = saClm.SaClmCallbacksT_4()
    callbacks.saClmClusterNodeGetCallback = \
        saClm.SaClmClusterNodeGetCallbackT_4(node_get_callback)
    callbacks.saClmClusterTrackCallback = \
        saClm.SaClmClusterTrackCallbackT_4(track_callback)

    # Define which version of the CLM API to use
    version = SaVersionT('B', 4, 1)

    # Initialize the CLM interface
    saClmInitialize_4(handle, callbacks, version)


def track(flags=saAis.SA_TRACK_CHANGES_ONLY):
    """ Start cluster membership tracking with specified flags

    Args:
        flags (SaUint8T): Type of cluster membership tracking
    """
    saClmClusterTrack_4(handle, flags, None)


def get_members():
    """ Get member notifications from notification buffer """
    notification_buffer = saClm.SaClmClusterNotificationBufferT_4()

    saClmClusterTrack_4(handle, saAis.SA_TRACK_CURRENT, notification_buffer)

    cluster_nodes = []

    for i in range(notification_buffer.numberOfItems):
        notification = notification_buffer.notification[i]
        clm_cluster_node = notification.clusterNode

        cluster_node = create_cluster_node_instance(clm_cluster_node)

        cluster_nodes.append(cluster_node)

    return cluster_nodes


def dispatch(flags=eSaDispatchFlagsT.SA_DISPATCH_ALL):
    """ Invoke CLM callbacks for queued events. The default is to dispatch all
    available events.

    Args:
        flags (eSaDispatchFlagsT): Flags specifying dispatch mode
    """
    saClmDispatch(handle, flags)
