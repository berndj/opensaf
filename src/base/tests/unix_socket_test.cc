/*      -*- OpenSAF  -*-
 *
 * (C) Copyright 2016 The OpenSAF Foundation
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
 * Author(s): Ericsson AB
 *
 */

#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include "base/time.h"
#include "base/unix_client_socket.h"
#include "base/unix_server_socket.h"
#include "gtest/gtest.h"

class UnixSocketTest : public ::testing::Test {
 protected:
  UnixSocketTest() {}

  virtual ~UnixSocketTest() {}

  std::string SocketName() { return tmpdir_ + "/sock"; }

  bool SocketExists() {
    struct stat buf;
    int result = stat(SocketName().c_str(), &buf);
    return result == 0 && S_ISSOCK(buf.st_mode);
  }

  virtual void SetUp() {
    char dir_tmpl[] = "/tmp/unix_socket_testXXXXXX";
    char* dir_name = mkdtemp(dir_tmpl);
    ASSERT_NE(dir_name, nullptr);
    tmpdir_ = std::string{dir_name};
  }

  virtual void TearDown() {
    if (!tmpdir_.empty()) {
      unlink(SocketName().c_str());
      rmdir(tmpdir_.c_str());
    }
  }

  static void ServerThread();

  std::string tmpdir_;
  static base::UnixServerSocket* server_;
};

base::UnixServerSocket* UnixSocketTest::server_ = nullptr;

void UnixSocketTest::ServerThread() {
  char buf[256] = {};
  const timespec kOneMicrosecond{0, 100};
  for (int i = 0; i != 10; ++i) {
    base::Sleep(kOneMicrosecond);
    for (int j = 0; j != 100; ++j) {
      ssize_t result = server_->Recv(buf, sizeof(buf));
      EXPECT_GE(result, 0);
      EXPECT_EQ(static_cast<size_t>(result), sizeof(buf));
    }
  }
}

TEST_F(UnixSocketTest, DestructorShallUnlinkSocket) {
  ASSERT_FALSE(SocketExists());
  base::UnixServerSocket* sock =
      new base::UnixServerSocket(SocketName(), base::UnixSocket::kNonblocking);
  EXPECT_FALSE(SocketExists());
  char buf[32];
  sock->Recv(buf, sizeof(buf));
  EXPECT_TRUE(SocketExists());
  delete sock;
  EXPECT_FALSE(SocketExists());
}

TEST_F(UnixSocketTest, OpenShallUnlinkExistingSocket) {
  ASSERT_FALSE(SocketExists());
  int sock = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  ASSERT_GE(sock, 0);
  struct sockaddr_un addr {
    AF_UNIX, {}
  };
  memcpy(addr.sun_path, SocketName().c_str(), SocketName().size() + 1);
  ASSERT_EQ(
      bind(sock, reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)),
      0);
  ASSERT_EQ(close(sock), 0);
  EXPECT_TRUE(SocketExists());
  base::UnixServerSocket* server =
      new base::UnixServerSocket(SocketName(), base::UnixSocket::kNonblocking);
  char buf[32];
  ssize_t result = server->Recv(buf, sizeof(buf));
  int saved_errno = errno;
  EXPECT_EQ(result, -1);
  EXPECT_EQ(saved_errno, EAGAIN);
  EXPECT_GE(server->fd(), 0);
  delete server;
}

TEST_F(UnixSocketTest, DontUnlinkExistingSocketIfServerStillAlive) {
  ASSERT_FALSE(SocketExists());
  base::UnixServerSocket* sock =
      new base::UnixServerSocket(SocketName(), base::UnixSocket::kNonblocking);
  char buf[32];
  ssize_t result = sock->Recv(buf, sizeof(buf));
  EXPECT_EQ(result, -1);
  EXPECT_EQ(errno, EAGAIN);
  base::UnixServerSocket* sock2 =
      new base::UnixServerSocket(SocketName(), base::UnixSocket::kNonblocking);
  result = sock2->Recv(buf, sizeof(buf));
  EXPECT_EQ(result, -1);
  EXPECT_EQ(errno, EADDRINUSE);
  delete sock2;
  EXPECT_TRUE(SocketExists());
  delete sock;
  EXPECT_FALSE(SocketExists());
}

TEST_F(UnixSocketTest, FdMethodShallOpenTheSocket) {
  ASSERT_FALSE(SocketExists());
  base::UnixServerSocket* sock =
      new base::UnixServerSocket(SocketName(), base::UnixSocket::kNonblocking);
  int fd = sock->fd();
  EXPECT_GE(fd, 0);
  EXPECT_TRUE(SocketExists());
  delete sock;
}

TEST_F(UnixSocketTest, BlockingSend) {
  ASSERT_FALSE(SocketExists());
  server_ =
      new base::UnixServerSocket(SocketName(), base::UnixSocket::kBlocking);
  int server_fd = server_->fd();
  EXPECT_GE(server_fd, 0);
  EXPECT_TRUE(SocketExists());
  std::thread server_thread{ServerThread};
  base::UnixClientSocket* client =
      new base::UnixClientSocket(SocketName(), base::UnixSocket::kBlocking);
  char buf[256] = {};
  for (int i = 0; i != 10; ++i) {
    for (int j = 0; j != 100; ++j) {
      ssize_t result = client->Send(buf, sizeof(buf));
      EXPECT_GE(result, 0);
      EXPECT_EQ(static_cast<size_t>(result), sizeof(buf));
    }
  }
  delete client;
  EXPECT_TRUE(SocketExists());
  server_thread.join();
  delete server_;
  EXPECT_FALSE(SocketExists());
}

TEST_F(UnixSocketTest, SendToRecvFromAbstract) {
  std::string abstract_name = std::string{"\0", 1} +
                              std::string{"unix_socket_test."} +
                              std::to_string(getpid());
  base::UnixServerSocket* abstract =
      new base::UnixServerSocket(abstract_name, base::UnixSocket::kNonblocking);
  int abstract_fd = abstract->fd();
  EXPECT_GE(abstract_fd, 0);
  ASSERT_FALSE(SocketExists());
  server_ =
      new base::UnixServerSocket(SocketName(), base::UnixSocket::kNonblocking);
  int server_fd = server_->fd();
  EXPECT_GE(server_fd, 0);
  EXPECT_TRUE(SocketExists());
  char buf[256] = {};
  struct sockaddr_un dest_addr;
  socklen_t dest_addrlen =
      base::UnixSocket::SetAddress(SocketName(), &dest_addr);
  EXPECT_GT(dest_addrlen, 0u);
  EXPECT_LT(dest_addrlen, sizeof(dest_addr));
  ssize_t result = abstract->SendTo(buf, sizeof(buf), &dest_addr, dest_addrlen);
  EXPECT_GE(result, 0);
  EXPECT_EQ(static_cast<size_t>(result), sizeof(buf));
  struct sockaddr_un src_addr;
  socklen_t src_addrlen = sizeof(src_addr);
  result = server_->RecvFrom(buf, sizeof(buf), &src_addr, &src_addrlen);
  EXPECT_GE(result, 0);
  EXPECT_EQ(static_cast<size_t>(result), sizeof(buf));
  EXPECT_EQ(src_addrlen, sizeof(src_addr.sun_family) + abstract_name.size());
  std::string path = base::UnixSocket::GetAddress(src_addr, src_addrlen);
  EXPECT_EQ(path, abstract_name);
  delete abstract;
  EXPECT_TRUE(SocketExists());
  delete server_;
  EXPECT_FALSE(SocketExists());
}
