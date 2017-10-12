/*      -*- OpenSAF  -*-
 *
 * (C) Copyright 2010 The OpenSAF Foundation
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
 * Author(s): GoAhead Software
 *            Ericsson AB
 *
 */

#include "dtm/dtmnd/multicast.h"
#include <arpa/inet.h>
#include <endian.h>
#include <inttypes.h>
#include <net/if.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include "base/logtrace.h"

Multicast::Multicast(uint16_t cluster_id, uint32_t node_id,
                     in_port_t stream_port, in_port_t dgram_port,
                     sa_family_t address_family,
                     const std::string &stream_address,
                     const std::string &dgram_address,
                     const std::string &multicast_address,
                     const std::string &ifname, bool scope_link)
    : cluster_id_{cluster_id},
      node_id_{node_id},
      stream_port_{stream_port},
      dgram_port_{dgram_port},
      address_family_{address_family},
      stream_address_{stream_address},
      dgram_address_{dgram_address},
      multicast_address_{multicast_address},
      ifname_{ifname},
      scope_link_{scope_link},
      dest_addr_size_{},
      dgram_sock_sndr{-1},
      dgram_sock_rcvr{-1},
      dest_addr_{} {
  if (!multicast_address.empty()) {
    /*
       0
       restricted to the same host
       1
       restricted to the same subnet
       32
       restricted to the same site
       64
       restricted to the same region
       128
       restricted to the same continent
       255
       unrestricted
     */
    uint32_t rc = dtm_dgram_mcast_sender(64); /*TODO */
    if (rc == NCSCC_RC_SUCCESS) {
      rc = dtm_dgram_mcast_listener();
      if (rc != NCSCC_RC_SUCCESS) {
        LOG_ER("DTM:Set up the initial mcast  receiver socket failed");
      }
    } else {
      LOG_ER("DTM:Set up the initial mcast sender socket failed rc: %d", rc);
    }
  } else {
    uint32_t rc = dtm_dgram_bcast_sender();
    if (rc == NCSCC_RC_SUCCESS) {
      rc = dtm_dgram_bcast_listener();
      if (rc != NCSCC_RC_SUCCESS) {
        LOG_ER("DTM:Set up the initial bcast  receiver socket failed");
      }
    } else {
      LOG_ER("DTM:Set up the initial bcast  sender socket failed rc: %d", rc);
    }
  }
}

Multicast::~Multicast() {
  if (dgram_sock_sndr >= 0) close(dgram_sock_sndr);
  if (dgram_sock_rcvr >= 0) close(dgram_sock_rcvr);
}

/**
 * Function for sending the bcast
 *
 * @param dtms_cb
 *
 * @return NCSCC_RC_SUCCESS
 * @return NCSCC_RC_FAILURE
 *
 */
uint32_t Multicast::dtm_dgram_bcast_sender() {
  TRACE_ENTER();

  dgram_sock_sndr = -1;
  dest_addr_size_ = 0;
  memset(&dest_addr_, 0, sizeof(dest_addr_));

  if (address_family_ == AF_INET) {
    /* Holder for bcast_dest_address address */
    struct sockaddr_in *bcast_sender_addr_in =
        reinterpret_cast<struct sockaddr_in *>(&dest_addr_);
    bcast_sender_addr_in->sin_family = AF_INET;
    bcast_sender_addr_in->sin_port = htons(dgram_port_);
    TRACE("DTM:  IP address : %s  Bcast address : %s sa_family : %d ",
          stream_address_.c_str(), dgram_address_.c_str(), address_family_);
    int rc = inet_pton(AF_INET, dgram_address_.c_str(),
                       &bcast_sender_addr_in->sin_addr);
    if (rc != 1) {
      LOG_ER("DTM : inet_pton failed");
      TRACE_LEAVE2("rc :%d", rc);
      return NCSCC_RC_FAILURE;
    }

    memset(bcast_sender_addr_in->sin_zero, '\0',
           sizeof(bcast_sender_addr_in->sin_zero));
    dest_addr_size_ = sizeof(struct sockaddr_in);
  } else if (address_family_ == AF_INET6) {
    /* Holder for bcast_dest_address address */
    struct sockaddr_in6 *bcast_sender_addr_in6 =
        reinterpret_cast<struct sockaddr_in6 *>(&dest_addr_);
    bcast_sender_addr_in6->sin6_family = AF_INET6;
    bcast_sender_addr_in6->sin6_port = htons(dgram_port_);
    bcast_sender_addr_in6->sin6_flowinfo = 0;
    bcast_sender_addr_in6->sin6_scope_id = if_nametoindex(ifname_.c_str());
    TRACE("DTM:  IP address : %s  Bcast address : %s sa_family : %d ",
          stream_address_.c_str(), dgram_address_.c_str(), address_family_);
    inet_pton(AF_INET6, dgram_address_.c_str(),
              &bcast_sender_addr_in6->sin6_addr);
    dest_addr_size_ = sizeof(struct sockaddr_in6);
  } else {
    LOG_ER("DTM : dgram_enable_bcast failed");
    TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
    return NCSCC_RC_FAILURE;
  }

  /* Create socket for sending/receiving datagrams */
  dgram_sock_sndr =
      socket(dest_addr_.ss_family, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
             IPPROTO_UDP);
  if (dgram_sock_sndr == -1) {
    LOG_ER("DTM :socket create  failederr :%s", strerror(errno));
    TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
    return NCSCC_RC_FAILURE;
  }

  if (dgram_enable_bcast(dgram_sock_sndr) != NCSCC_RC_SUCCESS) {
    LOG_ER("DTM : dgram_enable_bcast failed");
    if (close(dgram_sock_sndr) != 0 && errno != EINTR) {
      LOG_ER("Failed to close socket, errno=%d", errno);
    }
    TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
    return NCSCC_RC_FAILURE;
  }

  if (address_family_ == AF_INET6) {
    struct sockaddr_in6 *bcast_sender_addr_in6 =
        reinterpret_cast<struct sockaddr_in6 *>(&dest_addr_);
    int yes = 1;
    if (setsockopt(dgram_sock_sndr, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &yes,
                   sizeof(yes)) < 0) {
      LOG_ER("DTM :setsockopt(IPV6_MULTICAST_HOPS) failed err :%s",
             strerror(errno));
      TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
      return NCSCC_RC_FAILURE;
    }

    if (setsockopt(dgram_sock_sndr, SOL_SOCKET, SO_REUSEADDR, &yes,
                   sizeof(yes)) < 0) {
      LOG_ER("DTM :setsockopt(SO_REUSEADDR) failed err :%s", strerror(errno));
      TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
      return NCSCC_RC_FAILURE;
    }

    unsigned int ifindex;
    ifindex = if_nametoindex(ifname_.c_str());
    if (setsockopt(dgram_sock_sndr, IPPROTO_IPV6, IPV6_MULTICAST_IF, &ifindex,
                   sizeof(ifindex)) < 0) {
      LOG_ER("DTM :setsockopt(IPV6_MULTICAST_IF) failed err :%s ifname :%d",
             strerror(errno), if_nametoindex(ifname_.c_str()));
      TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
      return NCSCC_RC_FAILURE;
    }

    struct ipv6_mreq maddr;
    memset(&maddr, 0, sizeof(maddr));
    maddr.ipv6mr_multiaddr = bcast_sender_addr_in6->sin6_addr;
    maddr.ipv6mr_interface = if_nametoindex(ifname_.c_str());
    if (setsockopt(dgram_sock_sndr, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP, &maddr,
                   sizeof(maddr)) < 0) {
      LOG_ER("DTM :setsockopt(IPV6_ADD_MEMBERSHIP) failed err :%s",
             strerror(errno));
      TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
      return NCSCC_RC_FAILURE;
    }
  }
  TRACE_LEAVE2("rc :%d", NCSCC_RC_SUCCESS);
  return NCSCC_RC_SUCCESS;
}

/**
 * Enable the dgram bcast
 *
 * @param sock_desc
 *
 * @return NCSCC_RC_SUCCESS
 * @return NCSCC_RC_FAILURE
 *
 */
uint32_t Multicast::dgram_enable_bcast(int sock_desc) {
  TRACE_ENTER();
  /* If this fails, we'll hear about it when we try to send.  This will
   * allow */
  /* system that cannot bcast to continue if they don't plan to bcast */
  int bcast_permission = 1;
  if (setsockopt(sock_desc, SOL_SOCKET, SO_BROADCAST, &bcast_permission,
                 sizeof(bcast_permission)) < 0) {
    LOG_ER("DTM :setsockopt(SO_BROADCAST) failed err :%s ", strerror(errno));
    TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
    return NCSCC_RC_FAILURE;
  }

  TRACE_LEAVE2("rc :%d", NCSCC_RC_SUCCESS);
  return NCSCC_RC_SUCCESS;
}

/**
 * Function to send the mcast message
 *
 * @param dtms_cb mcast_ttl
 *
 * @return NCSCC_RC_SUCCESS
 * @return NCSCC_RC_FAILURE
 *
 */
uint32_t Multicast::dtm_dgram_mcast_sender(int mcast_ttl) {
  /* Construct the serv address structure */
  struct addrinfo addr_criteria;  // Criteria for address match
  char local_port_str[INET6_ADDRSTRLEN];
  int rv;
  TRACE_ENTER();

  dgram_sock_sndr = -1;

  TRACE("DTM :dgram_port_rcvr :%d", dgram_port_);
  snprintf(local_port_str, sizeof(local_port_str), "%d", (dgram_port_));

  memset(&addr_criteria, 0, sizeof(addr_criteria)); /* Zero out structure */
  addr_criteria.ai_family = AF_UNSPEC;              /* v4 or v6 is OK */
  addr_criteria.ai_socktype = SOCK_DGRAM;           /* Only datagram sockets */
  addr_criteria.ai_protocol = IPPROTO_UDP;          /* Only UDP please */
  addr_criteria.ai_flags |= AI_NUMERICHOST; /* Don't try to resolve address */

  struct addrinfo *mcast_sender_addr;
  /* For link-local address, need to set sin6_scope_id to match the
     device index of the network device on it has to connecct */
  if (scope_link_) {
    char mcast_addr_eth[INET6_ADDRSTRLEN + IFNAMSIZ];
    memset(mcast_addr_eth, 0, (INET6_ADDRSTRLEN + IFNAMSIZ));
    sprintf(mcast_addr_eth, "%s%%%s", multicast_address_.c_str(),
            ifname_.c_str());
    rv = getaddrinfo(mcast_addr_eth, local_port_str, &addr_criteria,
                     &mcast_sender_addr);
    TRACE("DTM :mcast_addr : %s local_port_str :%s", mcast_addr_eth,
          local_port_str);
  } else {
    rv = getaddrinfo(multicast_address_.c_str(), local_port_str, &addr_criteria,
                     &mcast_sender_addr);
    TRACE("DTM :mcast_addr : %s local_port_str :%s", multicast_address_.c_str(),
          local_port_str);
  }
  if (rv != 0) {
    LOG_ER("DTM:Unable to getaddrinfo() rtn_val :%d err :%s", rv,
           strerror(errno));
    TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
    return NCSCC_RC_FAILURE;
  }

  if (mcast_sender_addr == nullptr) {
    LOG_ER("DTM:Unable to getaddrinfo() rtn_val :%d err :%s", rv,
           strerror(errno));
    TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
    return NCSCC_RC_FAILURE;
  }

  TRACE("DTM :family : %d, socktype : %d, protocol :%d",
        mcast_sender_addr->ai_family, mcast_sender_addr->ai_socktype,
        mcast_sender_addr->ai_protocol);
  /* Create socket for sending multicast datagrams */
  if ((dgram_sock_sndr =
           socket(mcast_sender_addr->ai_family,
                  mcast_sender_addr->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC,
                  mcast_sender_addr->ai_protocol)) == -1) {
    LOG_ER("DTM:Socket creation failed (socket()) err :%s", strerror(errno));
    freeaddrinfo(mcast_sender_addr);
    TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
    return NCSCC_RC_FAILURE;
  }

  if (dgram_set_mcast_ttl(mcast_ttl, mcast_sender_addr->ai_family) !=
      NCSCC_RC_SUCCESS) {
    LOG_ER("DTM : dgram_set_mcast_ttl() failed");
    if (close(dgram_sock_sndr) != 0 && errno != EINTR) {
      LOG_ER("Failed to close socket, errno=%d", errno);
    }
    freeaddrinfo(mcast_sender_addr);
    TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
    return NCSCC_RC_FAILURE;
  }

  size_t addrlen = mcast_sender_addr->ai_addrlen;
  if (addrlen > sizeof(dest_addr_)) {
    LOG_ER("getaddrinfo() returned too large address: %zu", addrlen);
    addrlen = sizeof(dest_addr_);
  }
  memcpy(&dest_addr_, mcast_sender_addr->ai_addr, addrlen);

  freeaddrinfo(mcast_sender_addr);

  TRACE_LEAVE2("rc :%d", NCSCC_RC_SUCCESS);
  return NCSCC_RC_SUCCESS;
}

Multicast::Message::Message(uint16_t i_cluster_id, uint32_t i_node_id,
                            bool i_use_multicast, in_port_t i_stream_port,
                            sa_family_t i_address_family,
                            const std::string &i_stream_address)
    : message_size{htobe16(sizeof(Message))},
      cluster_id{htobe16(i_cluster_id)},
      node_id{htobe32(i_node_id)},
      use_multicast{i_use_multicast ? uint8_t{1} : uint8_t{0}},
      stream_port{htobe16(i_stream_port)},
      address_family{static_cast<uint8_t>(i_address_family)},
      stream_address{} {
  size_t len = i_stream_address.size();
  if (len >= sizeof(stream_address) - 1) len = sizeof(stream_address) - 1;
  memcpy(stream_address, i_stream_address.data(), len);
  memset(stream_address + len, 0, sizeof(stream_address) - len);
}

/**
 * Set the mcast ttl
 *
 * @param dtms_cb mcast_ttl
 *
 * @return NCSCC_RC_SUCCESS
 * @return NCSCC_RC_FAILURE
 *
 */
uint32_t Multicast::dgram_set_mcast_ttl(int mcast_ttl, int family) {
  TRACE_ENTER();
  /* Set TTL of mcast packet. Unfortunately this requires */
  /* address-family-specific code */
  if (family == AF_INET6) {  // v6-specific
    /* The v6 mcast TTL socket option requires that the value be */
    /* passed in as an integer */
    if (setsockopt(dgram_sock_sndr, IPPROTO_IPV6, IPV6_MULTICAST_HOPS,
                   &mcast_ttl, sizeof(mcast_ttl)) < 0) {
      LOG_ER("DTM : setsockopt(IPV6_MULTICAST_HOPS) failed err :%s",
             strerror(errno));
      TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
      return NCSCC_RC_FAILURE;
    }
    unsigned int ifindex;
    ifindex = if_nametoindex(ifname_.c_str());
    if (setsockopt(dgram_sock_sndr, IPPROTO_IPV6, IPV6_MULTICAST_IF, &ifindex,
                   sizeof(ifindex)) < 0) {
      LOG_ER("DTM :setsockopt(IPV6_MULTICAST_IF) failed err :%s ifname :%d",
             strerror(errno), if_nametoindex(ifname_.c_str()));
      TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
      return NCSCC_RC_FAILURE;
    }
  } else if (family == AF_INET) { /* v4 specific */
    if (setsockopt(dgram_sock_sndr, IPPROTO_IP, IP_MULTICAST_TTL, &mcast_ttl,
                   sizeof(mcast_ttl)) < 0) {
      LOG_ER("DTM :setsockopt(IP_MULTICAST_TTL) failed err :%s",
             strerror(errno));
      TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
      return NCSCC_RC_FAILURE;
    }
  } else {
    LOG_ER("DTM: AF not supported :%d", family);
    TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
    return NCSCC_RC_FAILURE;
  }

  TRACE_LEAVE2("rc :%d", NCSCC_RC_SUCCESS);
  return NCSCC_RC_SUCCESS;
}

bool Multicast::Send() {
  TRACE_ENTER();
  Message msg{cluster_id_,  node_id_,        !multicast_address_.empty(),
              stream_port_, address_family_, stream_address_};
  bool success = true;
  ssize_t num_bytes =
      sendto(dgram_sock_sndr, &msg, sizeof(msg), 0,
             reinterpret_cast<struct sockaddr *>(&dest_addr_), dest_addr_size_);
  if (num_bytes < 0) {
    LOG_ER("sendto() failed: %s ", strerror(errno));
    success = false;
  } else if (static_cast<size_t>(num_bytes) != sizeof(msg)) {
    LOG_ER("sendto() sent unexpected number of bytes %zd", num_bytes);
    success = false;
  }

  TRACE_LEAVE2("success: %d", success ? 1 : 0);
  return success;
}

/**
 * Function to rcv the bcast message
 *
 * @param dtms_cb node_ip buffer buffer_len
 *
 * @return NCSCC_RC_SUCCESS
 * @return NCSCC_RC_FAILURE
 *
 */
ssize_t Multicast::Receive(void *buffer, size_t buffer_len) {
  TRACE_ENTER();

  ssize_t rtn;
  do {
    rtn = recv(dgram_sock_rcvr, buffer, buffer_len, MSG_DONTWAIT);
  } while (rtn < 0 && errno == EINTR);
  if (rtn < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
    LOG_ER("DTM:Receive failed (recv()) err :%s", strerror(errno));
  }

  TRACE_LEAVE2("rc :%zd", rtn);
  return rtn;
}

/**
 * Join the mcast group
 *
 * @param dtms_cb mcast_receiver_addr
 *
 * @return NCSCC_RC_SUCCESS
 * @return NCSCC_RC_FAILURE
 *
 */
uint32_t Multicast::dgram_join_mcast_group(
    struct addrinfo *mcast_receiver_addr) {
  TRACE_ENTER();

  /*  we need some address-family-specific pieces */
  if (mcast_receiver_addr->ai_family == AF_INET6) {
    /* Now join the mcast "group" (address) */
    struct ipv6_mreq join_request;
    memcpy(
        &join_request.ipv6mr_multiaddr,
        &(reinterpret_cast<struct sockaddr_in6 *>(mcast_receiver_addr->ai_addr))
             ->sin6_addr,
        sizeof(struct in6_addr));
    join_request.ipv6mr_interface = if_nametoindex(ifname_.c_str());
    TRACE("DTM :Joining IPv6 mcast group...");
    if (setsockopt(dgram_sock_rcvr, IPPROTO_IPV6, IPV6_JOIN_GROUP,
                   &join_request, sizeof(join_request)) < 0) {
      LOG_ER("DTM :setsockopt(IPV6_JOIN_GROUP) failed err :%s",
             strerror(errno));
      TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
      return NCSCC_RC_FAILURE;
    }
  } else if (mcast_receiver_addr->ai_family == AF_INET) {
    /* Now join the mcast "group" */
    struct ip_mreq join_request;
    memset(&join_request, 0, sizeof(join_request));
    join_request.imr_multiaddr =
        reinterpret_cast<struct sockaddr_in *>(mcast_receiver_addr->ai_addr)
            ->sin_addr;
    if (inet_aton(stream_address_.c_str(), &join_request.imr_interface) == 0) {
      LOG_ER("Invalid IPv4 address '%s'", stream_address_.c_str());
      TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
      return NCSCC_RC_FAILURE;
    }
    TRACE("DTM :Joining IPv4 mcast group...");
    if (setsockopt(dgram_sock_rcvr, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                   &join_request, sizeof(join_request)) < 0) {
      LOG_ER("DTM :setsockopt(IP_ADD_MEMBERSHIP) failed err :%s ",
             strerror(errno));
      TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
      return NCSCC_RC_FAILURE;
    }
  } else {
    LOG_ER("DTM: AF not supported :%d", mcast_receiver_addr->ai_family);
    TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
    return NCSCC_RC_FAILURE;
  }

  TRACE_LEAVE2("rc :%d", NCSCC_RC_SUCCESS);
  return NCSCC_RC_SUCCESS;
}

/**
 * Function to listen to mcast message
 *
 * @param dtms_cb
 *
 * @return NCSCC_RC_SUCCESS
 * @return NCSCC_RC_FAILURE
 *
 */
uint32_t Multicast::dtm_dgram_mcast_listener() {
  /* Construct the serv address structure */
  struct addrinfo addr_criteria; /* Criteria for address match */
  char local_port_str[INET6_ADDRSTRLEN];
  int rv;
  struct addrinfo *addr_list; /* Holder serv address */
  TRACE_ENTER();

  TRACE("DTM :dgram_port_rcvr :%d", dgram_port_);
  snprintf(local_port_str, sizeof(local_port_str), "%" PRIu16, dgram_port_);

  dgram_sock_rcvr = -1;

  memset(&addr_criteria, 0, sizeof(addr_criteria)); /* Zero out structure */
  addr_criteria.ai_family = AF_UNSPEC;              /* v4 or v6 is OK */
  addr_criteria.ai_socktype = SOCK_DGRAM;           /* Only datagram sockets */
  addr_criteria.ai_protocol = IPPROTO_UDP;          /* Only UDP protocol */
  addr_criteria.ai_flags |= AI_NUMERICHOST; /* Don't try to resolve address */

  /* For link-local address, need to set sin6_scope_id to match the
     device index of the network device on it has to connecct */
  if (scope_link_) {
    char mcast_addr_eth[INET6_ADDRSTRLEN + IFNAMSIZ];
    memset(mcast_addr_eth, 0, (INET6_ADDRSTRLEN + IFNAMSIZ));
    sprintf(mcast_addr_eth, "%s%%%s", multicast_address_.c_str(),
            ifname_.c_str());
    rv =
        getaddrinfo(mcast_addr_eth, local_port_str, &addr_criteria, &addr_list);
    TRACE("DTM:mcast_addr_eth : %s local_port_str :%s", mcast_addr_eth,
          local_port_str);
  } else {
    rv = getaddrinfo(multicast_address_.c_str(), local_port_str, &addr_criteria,
                     &addr_list);
    TRACE("DTM :mcast_addr : %s local_port_str :%s", multicast_address_.c_str(),
          local_port_str);
  }
  if (rv != 0) {
    LOG_ER("DTM:Unable to getaddrinfo() rtn_val :%d err :%s", rv,
           strerror(errno));
    TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
    return NCSCC_RC_FAILURE;
  }

  if (addr_list == nullptr) {
    LOG_ER("DTM:Unable to getaddrinfo() rtn_val :%d err :%s", rv,
           strerror(errno));
    TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
    return NCSCC_RC_FAILURE;
  }

  TRACE("DTM :family : %d, socktype : %d, protocol :%d", addr_list->ai_family,
        addr_list->ai_socktype, addr_list->ai_protocol);
  /* Create socket for sending multicast datagrams */
  if ((dgram_sock_rcvr =
           socket(addr_list->ai_family,
                  addr_list->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC,
                  addr_list->ai_protocol)) == -1) {
    LOG_ER("DTM:Socket creation failed (socket()) err :%s", strerror(errno));
    freeaddrinfo(addr_list);
    TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
    return NCSCC_RC_FAILURE;
  }

  if (dgram_sock_rcvr == -1) {
    LOG_ER("DTM:Socket creation failed (socket())");
    freeaddrinfo(addr_list);
    TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
    return NCSCC_RC_FAILURE;
  }

  if (bind(dgram_sock_rcvr, addr_list->ai_addr, addr_list->ai_addrlen) < 0) {
    LOG_ER("DTM : bind() failed err :%s ", strerror(errno));
    if (close(dgram_sock_rcvr) != 0 && errno != EINTR)
      LOG_ER("close() failed, errno=%d", errno);
    freeaddrinfo(addr_list);
    TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
    return NCSCC_RC_FAILURE;
  }

  if (dgram_join_mcast_group(addr_list) != NCSCC_RC_SUCCESS) {
    LOG_ER("DTM : dgram_join_mcast_group() failed");
    if (close(dgram_sock_rcvr) != 0 && errno != EINTR)
      LOG_ER("close() failed, errno=%d", errno);
    freeaddrinfo(addr_list);
    TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
    return NCSCC_RC_FAILURE;
  }

  /* Free address structure(s) allocated by getaddrinfo() */
  freeaddrinfo(addr_list);
  TRACE_LEAVE2("rc :%d", NCSCC_RC_SUCCESS);
  return NCSCC_RC_SUCCESS;
}

/**
 * Function to listen to the bcast message
 *
 * @param dtms_cb
 *
 * @return NCSCC_RC_SUCCESS
 * @return NCSCC_RC_FAILURE
 *
 */
uint32_t Multicast::dtm_dgram_bcast_listener() {
  struct addrinfo addr_criteria, *addr_list = nullptr,
                                 *p;  // Criteria for address
  char local_port_str[INET6_ADDRSTRLEN];
  int rv;
  char bcast_addr_eth[INET6_ADDRSTRLEN + IFNAMSIZ];
  TRACE_ENTER();

  TRACE("DTM :dgram_port_rcvr :%d", dgram_port_);
  snprintf(local_port_str, sizeof(local_port_str), "%d", (dgram_port_));

  dgram_sock_rcvr = -1;
  /*  Construct the server address structure */
  memset(&addr_criteria, 0, sizeof(addr_criteria));  // Zero out structure
  addr_criteria.ai_family = AF_UNSPEC;               // Any address family
  addr_criteria.ai_flags = AI_PASSIVE;      // Accept on any address/port
  addr_criteria.ai_socktype = SOCK_DGRAM;   // Only datagram socket
  addr_criteria.ai_protocol = IPPROTO_UDP;  // Only UDP socket
  addr_criteria.ai_flags |= AI_NUMERICHOST;

  TRACE("DTM :ip_addr : %s local_port_str :%s", stream_address_.c_str(),
        local_port_str);
  if (address_family_ == AF_INET) {
    if ((rv = getaddrinfo(dgram_address_.c_str(), local_port_str,
                          &addr_criteria, &addr_list)) != 0) {
      LOG_ER("DTM:Unable to getaddrinfo() rtn_val :%d err :%s", rv,
             strerror(errno));
      TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
      return NCSCC_RC_FAILURE;
    }
  } else if (address_family_ == AF_INET6) {
    if (scope_link_) {
      memset(bcast_addr_eth, 0, (INET6_ADDRSTRLEN + IFNAMSIZ));
      sprintf(bcast_addr_eth, "%s%%%s", dgram_address_.c_str(),
              ifname_.c_str());
      if ((rv = getaddrinfo(bcast_addr_eth, local_port_str, &addr_criteria,
                            &addr_list)) != 0) {
        LOG_ER("DTM:Unable to getaddrinfo() rtn_val :%d err :%s", rv,
               strerror(errno));
        TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
        return NCSCC_RC_FAILURE;
      }
    } else {
      if ((rv = getaddrinfo(dgram_address_.c_str(), local_port_str,
                            &addr_criteria, &addr_list)) != 0) {
        LOG_ER("DTM:Unable to getaddrinfo() rtn_val :%d err :%s", rv,
               strerror(errno));
        TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
        return NCSCC_RC_FAILURE;
      }
    }
  }

  if (addr_list == nullptr) {
    LOG_ER("DTM:Unable to get addr_list ");
    TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
    return NCSCC_RC_FAILURE;
  }

  /* results and bind to the first we can */
  p = addr_list;
  bool binded = false;
  unsigned int ifindex;

  for (; p; p = p->ai_next) {
    TRACE("DTM :family : %d, socktype : %d, protocol :%d", p->ai_family,
          p->ai_socktype, p->ai_protocol);
    if (address_family_ != p->ai_family) {
      continue;
    }

    if (p->ai_family == AF_INET) {
      void *addr;
      char ipstr[INET6_ADDRSTRLEN];
      struct sockaddr_in *ipv4 =
          reinterpret_cast<struct sockaddr_in *>(p->ai_addr);
      addr = &(ipv4->sin_addr);
      inet_ntop(p->ai_family, addr, ipstr, sizeof(ipstr));
      if (strcasecmp(ipstr, dgram_address_.c_str()) != 0) {
        continue;
      } else
        TRACE("DTM: DGRAM Socket binded to  = %s\n", ipstr);
    } else if (p->ai_family == AF_INET6) {
      void *addr;
      char ipstr[INET6_ADDRSTRLEN];
      struct sockaddr_in6 *ipv6 =
          reinterpret_cast<struct sockaddr_in6 *>(p->ai_addr);
      addr = &(ipv6->sin6_addr);
      inet_ntop(p->ai_family, addr, ipstr, sizeof(ipstr));
      if (strcasecmp(ipstr, dgram_address_.c_str()) != 0) {
        continue;
      } else
        TRACE("DTM: DGRAM Socket binded to  = %s\n", ipstr);
    }
    if ((dgram_sock_rcvr =
             socket(p->ai_family, p->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC,
                    p->ai_protocol)) == -1) {
      LOG_ER("DTM:Socket creation failed (socket()) err :%s", strerror(errno));
      TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
      continue;
    }

    int smode = 1;
    if ((setsockopt(dgram_sock_rcvr, SOL_SOCKET, SO_REUSEADDR, &smode,
                    sizeof(smode)) == -1)) {
      LOG_ER("DTM : Error setsockpot: err :%s", strerror(errno));
      freeaddrinfo(addr_list);
      TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
      return NCSCC_RC_FAILURE;
    }
    if (p->ai_family == AF_INET6) {
      struct sockaddr_in6 *ipv6 =
          reinterpret_cast<struct sockaddr_in6 *>(p->ai_addr);
      ifindex = if_nametoindex(ifname_.c_str());
      ipv6->sin6_scope_id = ifindex;
      if (setsockopt(dgram_sock_rcvr, IPPROTO_IPV6, IPV6_MULTICAST_IF, &ifindex,
                     sizeof(ifindex)) < 0) {
        LOG_ER("DTM :setsockopt(IPV6_MULTICAST_IF) failed err :%s",
               strerror(errno));
        freeaddrinfo(addr_list);
        TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
        return NCSCC_RC_FAILURE;
      }

      struct ipv6_mreq maddr;
      struct sockaddr_in6 *ipv6_mr =
          reinterpret_cast<struct sockaddr_in6 *>(p->ai_addr);
      memset(&maddr, 0, sizeof(maddr));
      maddr.ipv6mr_multiaddr = ipv6_mr->sin6_addr;
      maddr.ipv6mr_interface = if_nametoindex(ifname_.c_str());
      if (setsockopt(dgram_sock_rcvr, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP, &maddr,
                     sizeof(maddr)) < 0) {
        LOG_ER("DTM :setsockopt(IPV6_ADD_MEMBERSHIP) failed err :%s",
               strerror(errno));
        freeaddrinfo(addr_list);
        TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
        return NCSCC_RC_FAILURE;
      }
    }
    if (bind(dgram_sock_rcvr, p->ai_addr, p->ai_addrlen) == -1) {
      LOG_ER("DTM:Socket bind failed  err :%s", strerror(errno));
      close(dgram_sock_rcvr);
      TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
      perror("listener: bind");
      freeaddrinfo(addr_list);
      return NCSCC_RC_FAILURE;
    } else {
      binded = true;
      break;
    }
  }

  /* Free address structure(s) allocated by getaddrinfo() */
  freeaddrinfo(addr_list);
  if (binded != true) {
    TRACE_LEAVE2("rc :%d", NCSCC_RC_FAILURE);
    return NCSCC_RC_FAILURE;
  } else {
    TRACE_LEAVE2("rc :%d", NCSCC_RC_SUCCESS);
    return NCSCC_RC_SUCCESS;
  }
}
