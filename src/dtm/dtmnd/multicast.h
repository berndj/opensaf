/*      -*- OpenSAF  -*-
 *
 * Copyright Ericsson AB 2017 - All Rights Reserved.
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

#ifndef DTM_DTMND_MULTICAST_H_
#define DTM_DTMND_MULTICAST_H_

#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <cstddef>
#include <cstdint>
#include <string>

// The Multicast class implements a mechanism to send the DTM node discovery
// datagram message to all OpenSAF nodes on the network, using either broadcast,
// multicast or unicast.
class Multicast {
 public:
  struct __attribute__((__packed__)) Message {
    static constexpr size_t kInet6AddrStrlen = 48;
    Message(uint16_t i_cluster_id, uint32_t i_node_id, bool i_use_multicast,
            in_port_t i_stream_port, sa_family_t i_address_family,
            const std::string& i_stream_address);
    uint16_t message_size;
    uint16_t cluster_id;
    uint32_t node_id;
    uint8_t use_multicast;
    uint16_t stream_port;
    uint8_t address_family;
    char stream_address[kInet6AddrStrlen];
  };

  // Construct an instance of Multicast, using the supplied configuration parameters.
  Multicast(uint16_t cluster_id, uint32_t node_id, in_port_t stream_port,
            in_port_t dgram_port, sa_family_t address_family,
            const std::string& stream_address, const std::string& dgram_address,
            const std::string& multicast_address, const std::string& ifname,
            bool scope_link);
  ~Multicast();
  // Send the DTM node discovery message to all OpenSAF nodes. Returns true if
  // sending was successful, and false otherwise.
  bool Send();
  // Receive in non-blocking mode a DTM node discovery message sent by any
  // OpenSAF node on the network (possibly sent by ourselves). Returns the size
  // of the received message, or -1 in case of an error or if no message is
  // available. The parameters, the return value, and the error reporting in
  // errno are similar to the recv() libc function, except that this method will
  // never fail due to EINTR.
  ssize_t Receive(void* buffer, size_t buffer_len);
  // Returns the file descriptor used to receive messages. The intended use is
  // to include the returned file descriptor in a call to poll() or
  // epoll_wait(), so that you can suspend execution of the calling thread until
  // a new incoming message can be received by calling the Receive() method. Do
  // not use the returned file descriptor for any other purpose!
  int fd() { return dgram_sock_rcvr; }

 private:
  uint32_t dtm_dgram_bcast_sender();
  uint32_t dgram_enable_bcast(int sock_desc);
  uint32_t dtm_dgram_mcast_sender(int mcast_ttl);
  uint32_t dgram_set_mcast_ttl(int mcast_ttl, int family);
  uint32_t dgram_join_mcast_group(struct addrinfo* mcast_receiver_addr);
  uint32_t dtm_dgram_mcast_listener();
  uint32_t dtm_dgram_bcast_listener();

  uint16_t cluster_id_;
  uint32_t node_id_;
  in_port_t stream_port_;
  in_port_t dgram_port_;
  sa_family_t address_family_;
  std::string stream_address_;
  std::string dgram_address_;
  std::string multicast_address_;
  std::string ifname_;
  bool scope_link_;

  socklen_t dest_addr_size_;  // Holder for bcast_dest_address size ip v4 or v6
                              // address
  int dgram_sock_sndr;
  int dgram_sock_rcvr;
  struct sockaddr_storage dest_addr_;
};

#endif  // DTM_DTMND_MULTICAST_H_
