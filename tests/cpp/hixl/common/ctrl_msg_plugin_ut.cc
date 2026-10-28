/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <limits>
#include <vector>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <gtest/gtest.h>

#include "depends/ctrl_msg_test_helper.h"

#include "common/ctrl_msg_plugin.h"
#include "common/scope_guard.h"

namespace hixl {
namespace {
bool IsIpv6LoopbackBindable() {
  const int fd = socket(AF_INET6, SOCK_STREAM, 0);
  if (fd < 0) {
    return false;
  }
  sockaddr_in6 address{};
  address.sin6_family = AF_INET6;
  address.sin6_addr = in6addr_loopback;
  const bool bindable = bind(fd, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) == 0;
  (void)close(fd);
  return bindable;
}
}  // namespace

class CtrlMsgPluginTest : public test::PlainTlsTest {};

TEST_F(CtrlMsgPluginTest, ReceiveOverloadsSeparateByteCountAndTimeout) {
  int fds[2] = {-1, -1};
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  HIXL_MAKE_GUARD(close_fds, ([&fds]() {
                    (void)close(fds[0]);
                    (void)close(fds[1]);
                  }));
  auto connection = test::Connection(fds[1]);
  ASSERT_NE(connection, nullptr);
  const char expected = 'x';
  ASSERT_EQ(write(fds[0], &expected, sizeof(expected)), static_cast<ssize_t>(sizeof(expected)));
  std::array<char, 2> buffer{};
  ssize_t received = -1;
  ASSERT_EQ(connection->Recv(buffer.data(), buffer.size(), received), SUCCESS);
  EXPECT_EQ(received, 1);
  EXPECT_EQ(buffer[0], expected);
  uint32_t timeout_ms = 0;
  EXPECT_EQ(connection->Recv(buffer.data(), buffer.size(), timeout_ms), TIMEOUT);
  EXPECT_EQ(timeout_ms, 0U);
  int32_t signed_timeout_ms = 0;
  EXPECT_EQ(connection->Recv(buffer.data(), buffer.size(), signed_timeout_ms), TIMEOUT);
  EXPECT_EQ(signed_timeout_ms, 0);
  EXPECT_EQ(connection->Recv(buffer.data(), buffer.size(), 0U), TIMEOUT);
}

TEST_F(CtrlMsgPluginTest, DescriptionHandlesUnopenedConnections) {
  auto connection = std::make_shared<CtrlMsgPlugin>();
  EXPECT_EQ(connection->Desc(), "fd:-1, local:unknown, peer:unknown");
  ASSERT_EQ(connection->Initialize(), SUCCESS);
  errno = EIO;
  EXPECT_EQ(connection->Desc(), "fd:-1, local:unknown, peer:unknown");
  EXPECT_EQ(errno, EIO);
}

class CtrlMsgPluginDescTest : public test::PlainTlsTest, public ::testing::WithParamInterface<int> {};

TEST_P(CtrlMsgPluginDescTest, DescribesBothTcpEndpointsAndRetainsAddressesAfterClose) {
  const int family = GetParam();
  const std::string ip = family == AF_INET ? "127.0.0.1" : "::1";
  const std::string address_prefix = family == AF_INET ? "127.0.0.1:" : "[::1]:";
  if (family == AF_INET6 && !IsIpv6LoopbackBindable()) {
    GTEST_SKIP() << "IPv6 loopback is not bindable";
  }
  CtrlMsgPlugin listener;
  ASSERT_EQ(listener.Initialize(), SUCCESS);
  ASSERT_EQ(listener.Listen(ip, 0, 1), SUCCESS);
  sockaddr_storage address{};
  socklen_t size = sizeof(address);
  ASSERT_EQ(getsockname(listener.Fd(), reinterpret_cast<sockaddr *>(&address), &size), 0);
  const uint16_t port = family == AF_INET ? ntohs(reinterpret_cast<sockaddr_in *>(&address)->sin_port)
                                          : ntohs(reinterpret_cast<sockaddr_in6 *>(&address)->sin6_port);
  const std::string server_address = address_prefix + std::to_string(port);
  EXPECT_EQ(listener.Desc(), "fd:" + std::to_string(listener.Fd()) + ", local:" + server_address + ", peer:unknown");
  CtrlMsgPlugin client;
  ASSERT_EQ(client.Initialize(), SUCCESS);
  ASSERT_EQ(client.Connect(ip, port, 1000), SUCCESS);
  CtrlMsgPluginPtr accepted;
  ASSERT_EQ(listener.Accept(accepted), SUCCESS);
  ASSERT_NE(accepted, nullptr);
  size = sizeof(address);
  ASSERT_EQ(getsockname(client.Fd(), reinterpret_cast<sockaddr *>(&address), &size), 0);
  const uint16_t client_port = family == AF_INET ? ntohs(reinterpret_cast<sockaddr_in *>(&address)->sin_port)
                                                 : ntohs(reinterpret_cast<sockaddr_in6 *>(&address)->sin6_port);
  const std::string client_address = address_prefix + std::to_string(client_port);
  const std::string client_addresses = ", local:" + client_address + ", peer:" + server_address;
  const std::string server_addresses = ", local:" + server_address + ", peer:" + client_address;
  EXPECT_EQ(client.Desc(), "fd:" + std::to_string(client.Fd()) + client_addresses);
  EXPECT_EQ(accepted->Desc(), "fd:" + std::to_string(accepted->Fd()) + server_addresses);
  ASSERT_EQ(client.Close(), SUCCESS);
  ASSERT_EQ(accepted->Close(), SUCCESS);
  EXPECT_EQ(client.Desc(), "fd:-1" + client_addresses);
  EXPECT_EQ(accepted->Desc(), "fd:-1" + server_addresses);
  ASSERT_EQ(client.Initialize(), SUCCESS);
  EXPECT_EQ(client.Desc(), "fd:-1, local:unknown, peer:unknown");
}

INSTANTIATE_TEST_SUITE_P(SocketFamilies, CtrlMsgPluginDescTest, ::testing::Values(AF_INET, AF_INET6));

TEST_F(CtrlMsgPluginTest, DescriptionAddressDiscoveryDoesNotChangeErrno) {
  int fds[2] = {-1, -1};
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  HIXL_MAKE_GUARD(close_fds, ([&fds]() {
                    (void)close(fds[0]);
                    (void)close(fds[1]);
                  }));
  auto connection = test::Connection(fds[0]);
  ASSERT_NE(connection, nullptr);
  errno = EIO;
  connection->UpdateSocketAddresses();
  EXPECT_EQ(errno, EIO);
  EXPECT_EQ(connection->Desc(), "fd:" + std::to_string(connection->Fd()) + ", local:unknown, peer:unknown");
}

TEST_F(CtrlMsgPluginTest, RejectsLengthThatCannotBeRepresentedBySsizeT) {
  if (std::numeric_limits<size_t>::digits <= std::numeric_limits<ssize_t>::digits) {
    GTEST_SKIP() << "size_t does not provide a value outside the ssize_t range";
  }

  const size_t first_invalid_length = static_cast<size_t>(std::numeric_limits<ssize_t>::max()) + 1U;
  const std::array<size_t, 2> invalid_lengths = {first_invalid_length, std::numeric_limits<size_t>::max()};
  char byte = 0;
  for (const size_t len : invalid_lengths) {
    int32_t err_no = -1;
    EXPECT_EQ(test::Connection(-1)->Send(&byte, len, CtrlMsgPlugin::kDefaultCtrlTimeoutMs, err_no), PARAM_INVALID)
        << "len=" << len;
    EXPECT_EQ(test::Connection(-1)->Recv(&byte, len, 0), PARAM_INVALID) << "len=" << len;
  }
}

TEST_F(CtrlMsgPluginTest, AcceptsRepresentableBoundaryLengths) {
  if (std::numeric_limits<size_t>::digits < std::numeric_limits<ssize_t>::digits) {
    GTEST_SKIP() << "size_t cannot represent ssize_t max";
  }

  const size_t max_length = static_cast<size_t>(std::numeric_limits<ssize_t>::max());
  char byte = 0;
  int32_t err_no = -1;
  EXPECT_EQ(test::Connection(-1)->Send(&byte, 0, CtrlMsgPlugin::kDefaultCtrlTimeoutMs, err_no), FAILED);
  EXPECT_EQ(test::Connection(-1)->Send(&byte, max_length, CtrlMsgPlugin::kDefaultCtrlTimeoutMs, err_no), FAILED);
  EXPECT_EQ(test::Connection(-1)->Recv(&byte, 0, 0), FAILED);
  EXPECT_EQ(test::Connection(-1)->Recv(&byte, max_length, 0), FAILED);
}

TEST_F(CtrlMsgPluginTest, SendsSmallBuffer) {
  int32_t fds[2] = {-1, -1};
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  HIXL_MAKE_GUARD(close_fds, ([&fds]() {
                    (void)close(fds[0]);
                    (void)close(fds[1]);
                  }));

  const std::array<char, 7> expected = {'h', 'i', 'x', 'l', '-', 'u', 't'};
  ASSERT_EQ(test::Connection(fds[0])->Send(expected.data(), expected.size()), SUCCESS);

  std::array<char, 7> actual = {};
  ASSERT_EQ(read(fds[1], actual.data(), actual.size()), static_cast<ssize_t>(actual.size()));
  EXPECT_EQ(actual, expected);
}
TEST_F(CtrlMsgPluginTest, SendWithExplicitTimeoutCompletes) {
  int fds[2] = {-1, -1};
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  HIXL_MAKE_GUARD(close_fds, ([&]() {
                    (void)close(fds[0]);
                    (void)close(fds[1]);
                  }));
  auto connection = test::Connection(fds[0]);
  ASSERT_NE(connection, nullptr);
  const char byte = 'x';
  ASSERT_EQ(connection->Send(&byte, 1, 100U), SUCCESS);
  char received = 0;
  ASSERT_EQ(read(fds[1], &received, 1), 1);
  EXPECT_EQ(received, byte);
  EXPECT_EQ(connection->Send(nullptr, 0, 0U), SUCCESS);
}

TEST_F(CtrlMsgPluginTest, SendUsesTotalTimeoutWhenPeerDoesNotRead) {
  for (const uint32_t timeout_ms : {50U, 150U, CtrlMsgPlugin::kDefaultCtrlTimeoutMs}) {
    SCOPED_TRACE(timeout_ms);
    int fds[2] = {-1, -1};
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    HIXL_MAKE_GUARD(close_fds, ([&]() {
                      (void)close(fds[0]);
                      (void)close(fds[1]);
                    }));
    auto connection = test::Connection(fds[0]);
    ASSERT_NE(connection, nullptr);
    ASSERT_EQ(connection->SetSendTimeout(5000U), SUCCESS);
    const int send_buffer_size = 4096;
    ASSERT_EQ(setsockopt(fds[0], SOL_SOCKET, SO_SNDBUF, &send_buffer_size, sizeof(send_buffer_size)), 0);
    const std::vector<char> payload(1024U * 1024U, 'x');
    int32_t err_no = EIO;
    const auto start = std::chrono::steady_clock::now();
    const auto ret = timeout_ms == CtrlMsgPlugin::kDefaultCtrlTimeoutMs
                         ? connection->Send(payload.data(), payload.size())
                         : connection->Send(payload.data(), payload.size(), timeout_ms, err_no);
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    EXPECT_EQ(ret, TIMEOUT);
    EXPECT_GE(elapsed, timeout_ms - 10);
    EXPECT_LT(elapsed, timeout_ms + 1000);
    if (timeout_ms != CtrlMsgPlugin::kDefaultCtrlTimeoutMs) {
      EXPECT_EQ(err_no, 0);
    }
    EXPECT_GE(connection->Fd(), 0);
  }
}

TEST_F(CtrlMsgPluginTest, ZeroSendTimeoutDoesNotWrite) {
  int fds[2] = {-1, -1};
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  HIXL_MAKE_GUARD(close_fds, ([&]() {
                    (void)close(fds[0]);
                    (void)close(fds[1]);
                  }));
  auto connection = test::Connection(fds[0]);
  ASSERT_NE(connection, nullptr);
  char byte = 'x';
  int32_t err_no = EIO;
  EXPECT_EQ(connection->Send(&byte, 1, 0U, err_no), TIMEOUT);
  EXPECT_EQ(err_no, 0);
  EXPECT_EQ(recv(fds[1], &byte, 1, MSG_DONTWAIT), -1);
  EXPECT_TRUE(errno == EAGAIN || errno == EWOULDBLOCK);
}

TEST_F(CtrlMsgPluginTest, SetSendTimeoutRejectsInvalidFdAndStoresTimeout) {
  EXPECT_EQ(test::Connection(-1)->SetSendTimeout(CtrlMsgPlugin::kDefaultCtrlTimeoutMs), FAILED);
  int fds[2] = {-1, -1};
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  HIXL_MAKE_GUARD(close_fds, ([&]() {
                    (void)close(fds[0]);
                    (void)close(fds[1]);
                  }));
  auto connection = test::Connection(fds[0]);
  ASSERT_NE(connection, nullptr);
  ASSERT_EQ(connection->SetSendTimeout(1500U), SUCCESS);
  timeval timeout{};
  socklen_t size = sizeof(timeout);
  ASSERT_EQ(getsockopt(fds[0], SOL_SOCKET, SO_SNDTIMEO, &timeout, &size), 0);
  EXPECT_EQ(timeout.tv_sec, 1);
  EXPECT_EQ(timeout.tv_usec, 500000);
}

TEST_F(CtrlMsgPluginTest, EpollEventCarriesSocketFd) {
  int32_t fds[2] = {-1, -1};
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  int32_t epoll_fd = -1;
  HIXL_MAKE_GUARD(close_fds, ([&]() {
                    (void)close(fds[0]);
                    (void)close(fds[1]);
                    if (epoll_fd >= 0) {
                      (void)close(epoll_fd);
                    }
                  }));
  auto connection = test::Connection(fds[0]);
  ASSERT_NE(connection, nullptr);
  ASSERT_EQ(connection->AddToEpoll(epoll_fd), SUCCESS);
  const char byte = 'x';
  ASSERT_EQ(write(fds[1], &byte, sizeof(byte)), sizeof(byte));
  epoll_event event{};
  ASSERT_EQ(epoll_wait(epoll_fd, &event, 1, 1000), 1);
  EXPECT_EQ(event.data.fd, connection->Fd());
  EXPECT_NE(event.events & EPOLLIN, 0U);
}

TEST_F(CtrlMsgPluginTest, RejectsSocketCreationBeforeInitialization) {
  CtrlMsgPlugin listener;
  ASSERT_EQ(listener.Initialize(), SUCCESS);
  ASSERT_EQ(listener.Listen("127.0.0.1", 0, 1), SUCCESS);
  sockaddr_in addr{};
  socklen_t size = sizeof(addr);
  ASSERT_EQ(getsockname(listener.Fd(), reinterpret_cast<sockaddr *>(&addr), &size), 0);

  CtrlMsgPlugin connection;
  EXPECT_EQ(connection.Connect("127.0.0.1", ntohs(addr.sin_port), 100), FAILED);
  EXPECT_EQ(connection.Listen("127.0.0.1", 0, 1), FAILED);
  EXPECT_EQ(connection.Fd(), -1);
  EXPECT_EQ(connection.HandshakeServer(), FAILED);
  CtrlMsgPluginPtr accepted;
  EXPECT_EQ(connection.Accept(accepted), FAILED);
  EXPECT_EQ(accepted, nullptr);
}

TEST(CtrlMsgPluginLifecycleTest, RequiresOwnerInitializationAndCanRetry) {
  test::TlsOwner owner;
  CtrlMsgPlugin connection;
  EXPECT_EQ(connection.Initialize(), FAILED);
  EXPECT_EQ(connection.Listen("127.0.0.1", 0, 1), FAILED);
  EXPECT_EQ(connection.Fd(), -1);
  TlsConfig invalid;
  invalid.configured = true;
  EXPECT_EQ(owner.Initialize(invalid), PARAM_INVALID);
  EXPECT_EQ(connection.Initialize(), FAILED);
  ASSERT_EQ(owner.Initialize(), SUCCESS);
  ASSERT_EQ(connection.Initialize(), SUCCESS);
  ASSERT_EQ(connection.Listen("127.0.0.1", 0, 1), SUCCESS);
  const int fd = connection.Fd();
  EXPECT_EQ(connection.Initialize(), FAILED);
  EXPECT_EQ(connection.Fd(), fd);
  EXPECT_EQ(connection.Close(), SUCCESS);
}

TEST(CtrlMsgPluginLifecycleTest, ConnectionsBorrowPlainModeWithoutOwnerReferences) {
  auto &manager = TlsManager::GetInstance();
  ASSERT_FALSE(manager.IsInitialized());
  {
    test::TlsOwner owner;
    ASSERT_EQ(owner.Initialize(), SUCCESS);
    for (size_t i = 0; i < 3U; ++i) {
      auto connection = std::make_shared<CtrlMsgPlugin>();
      ASSERT_EQ(connection->Initialize(), SUCCESS);
      EXPECT_FALSE(CtrlMsgPluginTestAccess::IsTlsEnabled(connection));
      ASSERT_EQ(connection->Close(), SUCCESS);
      EXPECT_EQ(connection->Close(), SUCCESS);
      EXPECT_TRUE(manager.IsInitialized());
    }
  }
  EXPECT_FALSE(manager.IsInitialized());
}

TEST_F(CtrlMsgPluginTest, ClosedConnectionRejectsSocketCreationAndIo) {
  CtrlMsgPlugin connection;
  ASSERT_EQ(connection.Initialize(), SUCCESS);
  ASSERT_EQ(connection.Listen("127.0.0.1", 0, 1), SUCCESS);
  ASSERT_EQ(connection.Close(), SUCCESS);
  EXPECT_EQ(connection.Close(), SUCCESS);
  EXPECT_EQ(connection.Listen("127.0.0.1", 0, 1), FAILED);
  EXPECT_EQ(connection.Connect("127.0.0.1", 0, 100), FAILED);
  EXPECT_EQ(connection.Fd(), -1);
  char byte = 0;
  ssize_t n = 0;
  EXPECT_EQ(connection.Send(&byte, 1), FAILED);
  EXPECT_EQ(connection.Recv(&byte, 1, 1), FAILED);
  EXPECT_EQ(connection.Recv(&byte, 1, n), FAILED);
  EXPECT_EQ(n, -1);
  EXPECT_FALSE(connection.HasPending());
}

TEST_F(CtrlMsgPluginTest, FailedSocketCreationFinalizesConnection) {
  CtrlMsgPlugin client;
  ASSERT_EQ(client.Initialize(), SUCCESS);
  EXPECT_EQ(client.Connect("invalid-ip", 0, 100), PARAM_INVALID);
  EXPECT_EQ(client.Fd(), -1);
  EXPECT_EQ(client.Listen("127.0.0.1", 0, 1), FAILED);

  CtrlMsgPlugin listener;
  ASSERT_EQ(listener.Initialize(), SUCCESS);
  EXPECT_EQ(listener.Listen("invalid-ip", 0, 1), PARAM_INVALID);
  EXPECT_EQ(listener.Fd(), -1);
  EXPECT_EQ(listener.Listen("127.0.0.1", 0, 1), FAILED);
}

TEST_F(CtrlMsgPluginTest, FailedListenPreservesExistingSocket) {
  CtrlMsgPlugin listener;
  ASSERT_EQ(listener.Initialize(), SUCCESS);
  ASSERT_EQ(listener.Listen("127.0.0.1", 0, 8), SUCCESS);
  const int32_t listen_fd = listener.Fd();
  sockaddr_in addr{};
  socklen_t addr_len = sizeof(addr);
  ASSERT_EQ(getsockname(listen_fd, reinterpret_cast<sockaddr *>(&addr), &addr_len), 0);

  EXPECT_EQ(listener.Listen("127.0.0.1", ntohs(addr.sin_port), 8), FAILED);
  EXPECT_EQ(listener.Fd(), listen_fd);
  EXPECT_NE(fcntl(listen_fd, F_GETFD), -1);
}

}  // namespace hixl
