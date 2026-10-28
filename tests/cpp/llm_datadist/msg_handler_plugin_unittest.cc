/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE. See LICENSE
 * in the root of the software repository for the full text of the License.
 */

#include <arpa/inet.h>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "depends/ctrl_msg_test_helper.h"
#include "common/scope_guard.h"
#include "cpp/hixl/common/tls/tls_test_helper.h"

#define private public
#include "common/msg_handler_plugin.h"
#include "link_mgr/link_msg_handler.h"
#undef private

namespace {
constexpr int32_t kReadTimeoutMs = 1000;
int g_accepted_fd = -1;

extern "C" int __real_accept(int socket_fd, struct sockaddr *address, socklen_t *address_length);

extern "C" int __wrap_accept(int socket_fd, struct sockaddr *address, socklen_t *address_length) {
  g_accepted_fd = __real_accept(socket_fd, address, address_length);
  return g_accepted_fd;
}

int CreateListener(uint16_t &port) {
  const int listener_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (listener_fd < 0) {
    return -1;
  }

  int reuse = 1;
  if (setsockopt(listener_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0) {
    close(listener_fd);
    return -1;
  }

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(listener_fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0 || listen(listener_fd, 1) != 0) {
    close(listener_fd);
    return -1;
  }

  socklen_t address_length = sizeof(address);
  if (getsockname(listener_fd, reinterpret_cast<sockaddr *>(&address), &address_length) != 0) {
    close(listener_fd);
    return -1;
  }
  port = ntohs(address.sin_port);
  return listener_fd;
}

int ConnectClient(const uint16_t port) {
  const int client_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (client_fd < 0) {
    return -1;
  }

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(port);
  if (connect(client_fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) {
    close(client_fd);
    return -1;
  }
  return client_fd;
}

bool IsOpen(const int fd) {
  errno = 0;
  return fd >= 0 && fcntl(fd, F_GETFD) != -1;
}
}  // namespace

class MsgHandlerPluginTest : public hixl::test::PlainTlsTest {};

TEST_F(MsgHandlerPluginTest, ClosesAcceptedFdWhenTaskSubmissionFails) {
  uint16_t port = 0;
  const int listener_fd = CreateListener(port);
  ASSERT_GE(listener_fd, 0);
  const int client_fd = ConnectClient(port);
  ASSERT_GE(client_fd, 0);

  llm::MsgHandlerPlugin plugin;
  plugin.thread_pool_ = std::make_unique<llm::LLMThreadPool>("test", 1U);
  plugin.thread_pool_->Destroy();
  plugin.listener_connection_ = hixl::CtrlMsgPluginTestAccess::Own(dup(listener_fd));
  plugin.listener_running_ = true;
  g_accepted_fd = -1;

  EXPECT_EQ(plugin.DoAccept(), ge::FAILED);
  ASSERT_GE(g_accepted_fd, 0);
  EXPECT_FALSE(IsOpen(g_accepted_fd));

  plugin.listener_running_ = false;
  plugin.listener_connection_.reset();
  close(client_fd);
  close(listener_fd);
}

TEST_F(MsgHandlerPluginTest, PreservesMessageWireFormat) {
  int fds[2] = {-1, -1};
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  const int32_t expected_type = 7;
  const std::string expected_body = "control message";
  EXPECT_EQ(llm::MsgHandlerPlugin::SendMsg(hixl::test::Connection(fds[0]), expected_type, expected_body), ge::SUCCESS);

  uint32_t magic = 0;
  uint64_t length = 0;
  int32_t type = 0;
  std::string body(expected_body.size(), '\0');
  EXPECT_EQ(read(fds[1], &magic, sizeof(magic)), static_cast<ssize_t>(sizeof(magic)));
  EXPECT_EQ(read(fds[1], &length, sizeof(length)), static_cast<ssize_t>(sizeof(length)));
  EXPECT_EQ(read(fds[1], &type, sizeof(type)), static_cast<ssize_t>(sizeof(type)));
  EXPECT_EQ(read(fds[1], body.data(), body.size()), static_cast<ssize_t>(body.size()));
  EXPECT_EQ(magic, 0xA4B3C2D1U);
  EXPECT_EQ(length, expected_body.size() + sizeof(expected_type));
  EXPECT_EQ(type, expected_type);
  EXPECT_EQ(body, expected_body);

  close(fds[0]);
  close(fds[1]);
}

TEST_F(MsgHandlerPluginTest, RejectsMissingAndClosedConnections) {
  int32_t type = 0;
  std::vector<char> body;
  EXPECT_EQ(llm::MsgHandlerPlugin::SendMsg(nullptr, 7, "message"), ge::LLM_PARAM_INVALID);
  EXPECT_EQ(llm::MsgHandlerPlugin::RecvMsg(nullptr, type, body, kReadTimeoutMs), ge::FAILED);
  const auto connection = hixl::test::Connection(-1);
  ASSERT_NE(connection, nullptr);
  EXPECT_EQ(llm::MsgHandlerPlugin::SendMsg(connection, 7, "message"), ge::FAILED);
  EXPECT_EQ(llm::MsgHandlerPlugin::RecvMsg(connection, type, body, kReadTimeoutMs), ge::FAILED);
}

TEST_F(MsgHandlerPluginTest, ClosesConnectionAfterCallbackWithoutEofRead) {
  uint16_t port = 0;
  const int listener_fd = CreateListener(port);
  ASSERT_GE(listener_fd, 0);
  const int client_fd = ConnectClient(port);
  ASSERT_GE(client_fd, 0);
  const int accepted_fd = accept(listener_fd, nullptr, nullptr);
  ASSERT_GE(accepted_fd, 0);

  llm::MsgHandlerPlugin plugin;
  bool called = false;
  plugin.RegisterConnectedProcess(
      [&called, accepted_fd](const hixl::CtrlMsgPluginPtr &connection, bool &keep_connection) {
        EXPECT_EQ(connection->Fd(), accepted_fd);
        called = true;
        keep_connection = false;
      });
  EXPECT_EQ(plugin.DoConnectedProcess(hixl::CtrlMsgPluginTestAccess::Own(accepted_fd)), ge::SUCCESS);
  EXPECT_TRUE(called);
  EXPECT_FALSE(IsOpen(accepted_fd));
  close(client_fd);
  close(listener_fd);
}

TEST(LinkMsgHandlerLifecycleTest, DestructorReleasesTlsOwnerAfterListenerCloses) {
  auto &manager = hixl::TlsManager::GetInstance();
  EXPECT_FALSE(manager.IsInitialized());
  {
    llm::LinkMsgHandler handler(0, 0, nullptr, nullptr);
    ASSERT_EQ(handler.InitializeControlListener({}), ge::SUCCESS);
    EXPECT_TRUE(manager.IsInitialized());
    handler.handler_plugin_.Finalize();
    EXPECT_TRUE(manager.IsInitialized());
  }
  EXPECT_FALSE(manager.IsInitialized());
}

TEST(LinkMsgHandlerLifecycleTest, RepeatedFinalizeDoesNotReleaseAnotherOwner) {
  auto &manager = hixl::TlsManager::GetInstance();
  {
    ASSERT_EQ(manager.Initialize({}), hixl::SUCCESS);
    HIXL_MAKE_GUARD(finalize, ([&]() { manager.Finalize(); }));
    {
      llm::LinkMsgHandler handler(0, 0, nullptr, nullptr);
      ASSERT_EQ(handler.InitializeControlListener({}), ge::SUCCESS);
      handler.Finalize();
      handler.Finalize();
      EXPECT_TRUE(manager.IsInitialized());
    }
    EXPECT_TRUE(manager.IsInitialized());
  }
  EXPECT_FALSE(manager.IsInitialized());
}

TEST(LinkMsgHandlerLifecycleTest, LinkAndUnlinkPreserveConnectionFailureAndReleaseOwner) {
  const int fd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(fd, 0);
  HIXL_MAKE_GUARD(close_fd, ([fd]() { (void)close(fd); }));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ASSERT_EQ(bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)), 0);
  socklen_t size = sizeof(addr);
  ASSERT_EQ(getsockname(fd, reinterpret_cast<sockaddr *>(&addr), &size), 0);
  auto &manager = hixl::TlsManager::GetInstance();
  ASSERT_FALSE(manager.IsInitialized());
  {
    llm::LinkMsgHandler handler(0, 0, nullptr, nullptr);
    ASSERT_EQ(handler.InitializeControlListener({}), ge::SUCCESS);
    handler.local_comm_res_ = "configured-local-comm-res";
    llm::ClusterInfo cluster;
    cluster.remote_ip_infos.push_back({0x0100007FU, ntohs(addr.sin_port)});
    EXPECT_EQ(handler.LinkCluster(cluster, kReadTimeoutMs), ge::LLM_LINK_FAILED);
    EXPECT_EQ(handler.UnlinkCluster(cluster, kReadTimeoutMs, false), ge::LLM_UNLINK_FAILED);
    EXPECT_TRUE(manager.IsInitialized());
  }
  EXPECT_FALSE(manager.IsInitialized());
}

TEST(LinkMsgHandlerLifecycleTest, InitializationFailurePreservesLinkAndUnlinkErrorCodes) {
  auto &manager = hixl::TlsManager::GetInstance();
  ASSERT_FALSE(manager.IsInitialized());
  llm::LinkMsgHandler handler(0, 0, nullptr, nullptr);
  handler.local_comm_res_ = "configured-local-comm-res";
  llm::ClusterInfo cluster;
  cluster.remote_ip_infos.push_back({0x0100007FU, 28101});
  EXPECT_EQ(handler.LinkCluster(cluster, kReadTimeoutMs), ge::LLM_LINK_FAILED);
  EXPECT_EQ(handler.UnlinkCluster(cluster, kReadTimeoutMs, false), ge::LLM_UNLINK_FAILED);
  EXPECT_FALSE(manager.IsInitialized());
}

class MsgHandlerReadTimeoutTest : public ::testing::TestWithParam<bool> {
 protected:
  void SetUp() override {
    int fds[2] = {-1, -1};
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    HIXL_MAKE_GUARD(close_fds, ([&]() {
                      (void)close(fds[0]);
                      (void)close(fds[1]);
                    }));
    hixl::TlsConfig config;
    if (GetParam()) {
      dir_ = hixl::test::MakeTempDir();
      ASSERT_FALSE(dir_.empty());
      if (!hixl::test::GenerateSelfSignedCert(dir_)) {
        GTEST_SKIP() << "openssl is not available to generate test certificates";
      }
      config = hixl::test::MakeTlsConfig(dir_);
    }
    ASSERT_EQ(hixl::TlsManager::GetInstance().Initialize(config), hixl::SUCCESS);
    tls_initialized_ = true;
    connection_ = hixl::CtrlMsgPluginTestAccess::Own(dup(fds[0]));
    peer_ = hixl::CtrlMsgPluginTestAccess::Own(dup(fds[1]));
    ASSERT_NE(connection_, nullptr);
    ASSERT_NE(peer_, nullptr);
    ASSERT_GE(connection_->Fd(), 0);
    ASSERT_GE(peer_->Fd(), 0);
    if (GetParam()) {
      ASSERT_NO_FATAL_FAILURE(Handshake());
    }
  }

  void TearDown() override {
    connection_.reset();
    peer_.reset();
    if (tls_initialized_) {
      hixl::TlsManager::GetInstance().Finalize();
    }
    if (!dir_.empty()) {
      hixl::test::RemovePath(dir_ + "/cert.pem");
      hixl::test::RemovePath(dir_ + "/key.pem");
      hixl::test::RemovePath(dir_);
    }
  }

  void Handshake() {
    std::shared_ptr<hixl::TlsSession> server_session, client_session;
    ASSERT_EQ(hixl::TlsContext::CreateSession(hixl::TlsManager::GetInstance().GetContext(), connection_->Fd(), true,
                                              server_session),
              hixl::SUCCESS);
    ASSERT_EQ(hixl::TlsContext::CreateSession(hixl::TlsManager::GetInstance().GetContext(), peer_->Fd(), false,
                                              client_session),
              hixl::SUCCESS);
    hixl::Status server_ret = hixl::FAILED;
    std::thread server([&]() { server_ret = server_session->HandshakeAsServer(kReadTimeoutMs); });
    const auto client_ret = client_session->HandshakeAsClient(kReadTimeoutMs);
    server.join();
    ASSERT_EQ(client_ret, hixl::SUCCESS);
    ASSERT_EQ(server_ret, hixl::SUCCESS);
    hixl::CtrlMsgPluginTestAccess::SetSession(connection_, server_session);
    hixl::CtrlMsgPluginTestAccess::SetSession(peer_, client_session);
  }

  bool tls_initialized_ = false;
  hixl::CtrlMsgPluginPtr connection_, peer_;
  std::string dir_;
};

TEST_P(MsgHandlerReadTimeoutTest, ConnectionReceiveUsesCallerTimeout) {
  std::thread sender([&]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    const char byte = 'x';
    EXPECT_EQ(peer_->Send(&byte, sizeof(byte)), hixl::SUCCESS);
  });
  char byte = 0;
  const auto start = std::chrono::steady_clock::now();
  const auto result = connection_->Recv(&byte, sizeof(byte), 50);
  const auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
  sender.join();
  EXPECT_EQ(result, hixl::TIMEOUT);
  EXPECT_GE(elapsed, 40);
  EXPECT_LT(elapsed, 2000);
}

TEST_P(MsgHandlerReadTimeoutTest, RecvMsgUsesCallerTimeoutForEveryField) {
  const uint32_t magic = 0xA4B3C2D1U;
  const uint64_t length = sizeof(int32_t) + 1U;
  const int32_t type = 7;
  for (int field = 0; field < 4; ++field) {
    SCOPED_TRACE(field);
    if (field > 0) {
      ASSERT_EQ(peer_->Send(&magic, sizeof(magic)), hixl::SUCCESS);
    }
    if (field > 1) {
      ASSERT_EQ(peer_->Send(&length, sizeof(length)), hixl::SUCCESS);
    }
    if (field > 2) {
      ASSERT_EQ(peer_->Send(&type, sizeof(type)), hixl::SUCCESS);
    }
    int32_t received_type = 0;
    std::vector<char> body;
    const auto start = std::chrono::steady_clock::now();
    EXPECT_EQ(llm::MsgHandlerPlugin::RecvMsg(connection_, received_type, body, 50), ge::FAILED);
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    EXPECT_GE(elapsed, 40);
    EXPECT_LT(elapsed, 2000);
  }
}

TEST_P(MsgHandlerReadTimeoutTest, RejectsNonPositiveTimeout) {
  int32_t type = 0;
  std::vector<char> body;
  for (const int32_t timeout_ms : {0, -1}) {
    EXPECT_EQ(llm::MsgHandlerPlugin::RecvMsg(connection_, type, body, timeout_ms), ge::FAILED);
  }
}

TEST_P(MsgHandlerReadTimeoutTest, RejectsInvalidMagicAndMessageLengths) {
  const uint32_t invalid_magic = 0U;
  ASSERT_EQ(peer_->Send(&invalid_magic, sizeof(invalid_magic)), hixl::SUCCESS);
  int32_t type = -1;
  std::vector<char> body;
  EXPECT_EQ(llm::MsgHandlerPlugin::RecvMsg(connection_, type, body, kReadTimeoutMs), ge::FAILED);
  const uint32_t magic = 0xA4B3C2D1U;
  for (const uint64_t length : {0ULL, 3ULL, (1ULL << 20) + 1ULL}) {
    SCOPED_TRACE(length);
    ASSERT_EQ(peer_->Send(&magic, sizeof(magic)), hixl::SUCCESS);
    ASSERT_EQ(peer_->Send(&length, sizeof(length)), hixl::SUCCESS);
    EXPECT_EQ(llm::MsgHandlerPlugin::RecvMsg(connection_, type, body, kReadTimeoutMs), ge::FAILED);
    EXPECT_EQ(type, -1);
    EXPECT_TRUE(body.empty());
  }
}

TEST_P(MsgHandlerReadTimeoutTest, ReceivesNormalAndEmptyMessages) {
  for (const auto &payload : {std::string("message"), std::string()}) {
    ASSERT_EQ(llm::MsgHandlerPlugin::SendMsg(peer_, 7, payload), ge::SUCCESS);
    int32_t type = 0;
    std::vector<char> body;
    ASSERT_EQ(llm::MsgHandlerPlugin::RecvMsg(connection_, type, body, kReadTimeoutMs), ge::SUCCESS);
    EXPECT_EQ(type, 7);
    EXPECT_EQ(std::string(body.data()), payload);
  }
}

TEST_P(MsgHandlerReadTimeoutTest, LinkServerReceivesDelayedRequest) {
  llm::LinkMsgHandler handler(0, 0, nullptr, nullptr);
  std::thread sender([&]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(llm::MsgHandlerPlugin::SendMsg(peer_, static_cast<int32_t>(llm::LinkMsgType::kEnd), ""), ge::SUCCESS);
  });
  bool keep_connection = true;
  const auto result = handler.ConnectedProcess(connection_, keep_connection);
  sender.join();
  // An unsupported type is rejected after the delayed frame is received, before accessing link resources.
  EXPECT_EQ(result, ge::LLM_PARAM_INVALID);
  EXPECT_FALSE(keep_connection);
}

INSTANTIATE_TEST_SUITE_P(PlaintextAndTls, MsgHandlerReadTimeoutTest, ::testing::Bool());
