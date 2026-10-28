/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE. See LICENSE in the root of
 * the software repository for the full text of the License.
 */

#include <gtest/gtest.h>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <type_traits>
#include <unistd.h>
#include <utility>
#include <thread>

#include "depends/ctrl_msg_test_helper.h"

#include "common/ctrl_msg_plugin.h"
#include "common/tls/tls_manager.h"
#include "cpp/hixl/common/tls/tls_test_helper.h"

#define private public
#include "adxl/channel_manager.h"
#undef private

namespace adxl {
namespace {
constexpr char kChannelId[] = "test_channel";
constexpr uint64_t kMaxControlMsgBodySizeInBytes = 4ULL * 1024ULL * 1024ULL;
constexpr size_t kMaxNotifyStorageSize = 4096;

class ChannelManagerUnitTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_EQ(tls_owner_.Initialize(), hixl::SUCCESS);
  }
  hixl::test::TlsOwner tls_owner_;

  ChannelPtr CreateChannelWithHeader(uint64_t body_size, uint32_t magic = kMagicNumber) {
    ChannelInfo channel_info{};
    channel_info.channel_type = ChannelType::kServer;
    channel_info.channel_id = kChannelId;
    auto channel = std::make_shared<CommChannel>(channel_info);
    ProtocolHeader header{magic, body_size};
    channel->recv_buffer_.resize(sizeof(header));
    memcpy_s(channel->recv_buffer_.data(), sizeof(header), &header, sizeof(header));
    channel->bytes_received_ = sizeof(header);
    channel->recv_state_ = RecvState::WAITING_FOR_HEADER;
    channel->expected_body_size_ = 0U;
    return channel;
  }

  std::string CreateNotifyMsgStr(uint64_t req_id, const std::string &name = "test_name",
                                 const std::string &msg = "test_msg") {
    NotifyMsg notify_msg{req_id, name, msg};
    std::string serialized_str;
    ControlMsgHandler::Serialize(notify_msg, serialized_str);
    return serialized_str;
  }

  ChannelManager manager_;
};

class ChannelManagerTlsTest : public ChannelManagerUnitTest {
 protected:
  void SetUp() override {
    dir_ = hixl::test::MakeTempDir();
    ASSERT_FALSE(dir_.empty());
    if (!hixl::test::GenerateSelfSignedCert(dir_)) {
      GTEST_SKIP() << "openssl is not available to generate test certificates";
    }
    ASSERT_EQ(hixl::TlsManager::GetInstance().Initialize(hixl::test::MakeTlsConfig(dir_)), hixl::SUCCESS);
    tls_initialized_ = true;
    ctx_ = hixl::TlsManager::GetInstance().GetContext();
    manager_.epoll_fd_ = epoll_create1(0);
    ASSERT_GE(manager_.epoll_fd_, 0);
  }

  void TearDown() override {
    for (const auto &channel : channels_) {
      channel->connection_ = nullptr;
    }
    manager_.fd_to_channel_map_.clear();
    for (const auto fd : fds_) {
      (void)close(fd);
    }
    ctx_.reset();
    if (tls_initialized_) {
      hixl::TlsManager::GetInstance().Finalize();
    }
    hixl::test::RemovePath(dir_ + "/cert.pem");
    hixl::test::RemovePath(dir_ + "/key.pem");
    hixl::test::RemovePath(dir_);
  }

  void CreateTlsChannel(ChannelPtr &channel, hixl::CtrlMsgPluginPtr &client_connection) {
    int fds[2] = {-1, -1};
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

    auto server_connection = hixl::CtrlMsgPluginTestAccess::Own(fds[0]);
    client_connection = hixl::CtrlMsgPluginTestAccess::Own(fds[1]);
    std::shared_ptr<hixl::TlsSession> server_session;
    std::shared_ptr<hixl::TlsSession> client_session;
    ASSERT_EQ(
        hixl::TlsContext::CreateSession(hixl::TlsManager::GetInstance().GetContext(), fds[0], true, server_session),
        hixl::SUCCESS);
    ASSERT_EQ(
        hixl::TlsContext::CreateSession(hixl::TlsManager::GetInstance().GetContext(), fds[1], false, client_session),
        hixl::SUCCESS);
    hixl::Status server_ret = hixl::FAILED;
    std::thread server([&]() { server_ret = server_session->HandshakeAsServer(2000); });
    EXPECT_EQ(client_session->HandshakeAsClient(2000), hixl::SUCCESS);
    server.join();
    ASSERT_EQ(server_ret, hixl::SUCCESS);
    hixl::CtrlMsgPluginTestAccess::SetSession(server_connection, server_session);
    hixl::CtrlMsgPluginTestAccess::SetSession(client_connection, client_session);
    ChannelInfo info{};
    info.channel_type = ChannelType::kServer;
    info.channel_id = std::to_string(fds[0]);
    channel = std::make_shared<CommChannel>(info);
    channels_.push_back(channel);
    ASSERT_EQ(channel->SetSocketNonBlocking(server_connection), SUCCESS);
    ASSERT_EQ(manager_.AddSocketToEpoll(server_connection, channel), SUCCESS);
  }

  void SendLargeNotify(const hixl::CtrlMsgPluginPtr &client_connection) {
    const std::string body = CreateNotifyMsgStr(1U, "name", std::string(8192, 'x'));
    const ControlMsgType type = ControlMsgType::kNotify;
    const ProtocolHeader header{kMagicNumber, sizeof(type) + body.size()};
    std::string wire(sizeof(header) + sizeof(type) + body.size(), '\0');
    ASSERT_EQ(memcpy_s(wire.data(), wire.size(), &header, sizeof(header)), EOK);
    ASSERT_EQ(memcpy_s(wire.data() + sizeof(header), wire.size() - sizeof(header), &type, sizeof(type)), EOK);
    ASSERT_EQ(memcpy_s(wire.data() + sizeof(header) + sizeof(type), body.size(), body.data(), body.size()), EOK);
    ASSERT_EQ(hixl::test::Connection(client_connection)->Send(wire.data(), wire.size()), hixl::SUCCESS);
  }

  bool tls_initialized_ = false;
  std::string dir_;
  std::shared_ptr<hixl::TlsContext> ctx_;
  std::vector<int32_t> fds_;
  std::vector<ChannelPtr> channels_;
};

TEST_F(ChannelManagerTlsTest, EventLoopConsumesPendingWithoutAnotherKernelEvent) {
  ChannelPtr channel;
  hixl::CtrlMsgPluginPtr client_connection;
  CreateTlsChannel(channel, client_connection);
  ASSERT_NE(channel, nullptr);
  ASSERT_EQ(manager_.fd_to_channel_map_.count(channel->GetFd()), 1U);
  SendLargeNotify(client_connection);
  ASSERT_EQ(manager_.HandleEpoolEvents(), SUCCESS);
  ASSERT_TRUE(channel->Connection()->HasPending());
  epoll_event event{};
  ASSERT_EQ(epoll_wait(manager_.epoll_fd_, &event, 1, 0), 0);

  // Each pending scan performs one read; the next round continues without waiting for new peer data.
  EXPECT_EQ(manager_.HandleEpoolEvents(), SUCCESS);
  EXPECT_TRUE(channel->Connection()->HasPending());
  EXPECT_TRUE(channel->notify_messages_.empty());
  EXPECT_EQ(manager_.HandleEpoolEvents(), SUCCESS);
  ASSERT_EQ(channel->notify_messages_.size(), 1U);
  EXPECT_EQ(channel->notify_messages_[0].notify_msg, std::string(8192, 'x'));
  EXPECT_FALSE(channel->Connection()->HasPending());
  EXPECT_EQ(channel->bytes_received_, 0U);
}

TEST_F(ChannelManagerTlsTest, PendingScanReadsEachChannelOnce) {
  ChannelPtr first;
  ChannelPtr second;
  hixl::CtrlMsgPluginPtr first_client_connection;
  hixl::CtrlMsgPluginPtr second_client_connection;
  CreateTlsChannel(first, first_client_connection);
  CreateTlsChannel(second, second_client_connection);
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  SendLargeNotify(first_client_connection);
  SendLargeNotify(second_client_connection);
  ASSERT_EQ(manager_.HandleSocketEvent(first->Connection()->Fd()), SUCCESS);
  ASSERT_EQ(manager_.HandleSocketEvent(second->Connection()->Fd()), SUCCESS);
  const auto first_received = first->bytes_received_;
  const auto second_received = second->bytes_received_;

  EXPECT_TRUE(manager_.ProcessPendingMsg());
  EXPECT_EQ(first->bytes_received_, first_received + 4096U);
  EXPECT_EQ(second->bytes_received_, second_received + 4096U);
  EXPECT_TRUE(first->Connection()->HasPending());
  EXPECT_TRUE(second->Connection()->HasPending());
  EXPECT_TRUE(manager_.ProcessPendingMsg());
  EXPECT_EQ(first->notify_messages_.size(), 1U);
  EXPECT_EQ(second->notify_messages_.size(), 1U);
  EXPECT_FALSE(manager_.ProcessPendingMsg());
}

TEST_F(ChannelManagerTlsTest, WriteTimeoutInvalidatesSessionAndOwnerReleasesChannel) {
  ChannelPtr channel;
  hixl::CtrlMsgPluginPtr peer;
  ASSERT_NO_FATAL_FAILURE(CreateTlsChannel(channel, peer));
  auto connection = channel->Connection();
  const int fd = connection->Fd();
  manager_.channels_[{channel->GetChannelType(), channel->GetChannelId()}] = channel;
  const int buffer_size = 4096;
  ASSERT_EQ(setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buffer_size, sizeof(buffer_size)), 0);
  const std::string payload(1024U * 1024U, 'x');
  auto start = std::chrono::steady_clock::now();
  EXPECT_EQ(ControlMsgHandler::Write(connection, payload.data(), payload.size(), 100000U, start), TIMEOUT);
  EXPECT_FALSE(connection->IsUsable());
  EXPECT_EQ(connection->Fd(), fd);
  const char different[] = "next-message";
  start = std::chrono::steady_clock::now();
  EXPECT_EQ(ControlMsgHandler::Write(connection, different, sizeof(different), 1000000U, start), FAILED);
  EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(100));
  EXPECT_EQ(manager_.HandleEpoolEvents(), SUCCESS);
  EXPECT_TRUE(manager_.channels_.empty());
  EXPECT_TRUE(manager_.fd_to_channel_map_.empty());
  EXPECT_EQ(connection->Fd(), -1);
}

TEST_F(ChannelManagerTlsTest, IncompleteProtocolMessageInvalidatesTlsSession) {
  ChannelPtr channel;
  hixl::CtrlMsgPluginPtr peer;
  ASSERT_NO_FATAL_FAILURE(CreateTlsChannel(channel, peer));
  auto connection = channel->Connection();
  const int buffer_size = 4096;
  ASSERT_EQ(setsockopt(connection->Fd(), SOL_SOCKET, SO_SNDBUF, &buffer_size, sizeof(buffer_size)), 0);
  const std::string payload(1024U * 1024U, 'x');
  EXPECT_EQ(ControlMsgHandler::SendMsgByProtocol(connection, ControlMsgType::kNotify, payload, 100000U), TIMEOUT);
  EXPECT_FALSE(connection->IsUsable());
  EXPECT_EQ(ControlMsgHandler::SendMsgByProtocol(connection, ControlMsgType::kNotify, "different", 1000000U), FAILED);
}

TEST_F(ChannelManagerTlsTest, DeadlineBeforeFirstWriteKeepsSessionUsable) {
  ChannelPtr channel;
  hixl::CtrlMsgPluginPtr peer;
  ASSERT_NO_FATAL_FAILURE(CreateTlsChannel(channel, peer));
  auto connection = channel->Connection();
  const char byte = 'x';
  auto start = std::chrono::steady_clock::now() - std::chrono::seconds(1);
  EXPECT_EQ(ControlMsgHandler::Write(connection, &byte, 1U, 1U, start), TIMEOUT);
  EXPECT_TRUE(connection->IsUsable());
  start = std::chrono::steady_clock::now();
  EXPECT_EQ(ControlMsgHandler::Write(connection, &byte, 1U, 1000000U, start), SUCCESS);
  char received = 0;
  EXPECT_EQ(peer->Recv(&received, 1U, 1000U), hixl::SUCCESS);
  EXPECT_EQ(received, byte);
}

TEST_F(ChannelManagerTlsTest, OldFailureCannotDestroyReplacementChannel) {
  ChannelPtr old_channel, replacement;
  hixl::CtrlMsgPluginPtr old_peer, new_peer;
  ASSERT_NO_FATAL_FAILURE(CreateTlsChannel(old_channel, old_peer));
  ASSERT_NO_FATAL_FAILURE(CreateTlsChannel(replacement, new_peer));
  const auto id = old_channel->GetChannelId();
  replacement->channel_info_.channel_id = id;
  manager_.channels_[{ChannelType::kServer, id}] = replacement;
  ASSERT_EQ(old_channel->Connection()->InvalidateTlsSession(), hixl::SUCCESS);
  manager_.CleanupUnusableChannel(old_channel);
  EXPECT_EQ(manager_.GetChannel(ChannelType::kServer, id), replacement);
  EXPECT_TRUE(replacement->Connection()->IsUsable());
}

TEST_F(ChannelManagerTlsTest, PendingReadFailureRemovesFdAndContinuesScanning) {
  ChannelPtr bad;
  ChannelPtr good;
  hixl::CtrlMsgPluginPtr bad_client_connection;
  hixl::CtrlMsgPluginPtr good_client_connection;
  CreateTlsChannel(bad, bad_client_connection);
  CreateTlsChannel(good, good_client_connection);
  ASSERT_NE(bad, nullptr);
  ASSERT_NE(good, nullptr);
  std::string wire(8192, '\0');
  const ControlMsgType type = ControlMsgType::kHeartBeat;
  const ProtocolHeader header{kMagicNumber, 4096U - sizeof(ProtocolHeader)};
  ASSERT_EQ(memcpy_s(wire.data(), wire.size(), &header, sizeof(header)), EOK);
  ASSERT_EQ(memcpy_s(wire.data() + sizeof(header), wire.size() - sizeof(header), &type, sizeof(type)), EOK);
  ASSERT_EQ(hixl::test::Connection(bad_client_connection)->Send(wire.data(), wire.size()), hixl::SUCCESS);
  SendLargeNotify(good_client_connection);
  ASSERT_EQ(manager_.HandleSocketEvent(bad->Connection()->Fd()), SUCCESS);
  ASSERT_EQ(manager_.HandleSocketEvent(good->Connection()->Fd()), SUCCESS);

  EXPECT_TRUE(manager_.ProcessPendingMsg());
  EXPECT_EQ(manager_.fd_to_channel_map_.count(bad->Connection()->Fd()), 0U);
  EXPECT_EQ(manager_.fd_to_channel_map_.count(good->Connection()->Fd()), 1U);
  EXPECT_TRUE(manager_.ProcessPendingMsg());
  EXPECT_EQ(good->notify_messages_.size(), 1U);
}

TEST_F(ChannelManagerUnitTest, PendingScanDoesNotReadPlaintextSocket) {
  int fds[2] = {-1, -1};
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  manager_.epoll_fd_ = epoll_create1(0);
  ASSERT_GE(manager_.epoll_fd_, 0);
  auto channel = CreateChannelWithHeader(sizeof(ControlMsgType));
  channel->recv_buffer_.clear();
  channel->bytes_received_ = 0U;
  ASSERT_EQ(channel->SetSocketNonBlocking(hixl::test::Connection(fds[0])), SUCCESS);
  ASSERT_EQ(manager_.AddSocketToEpoll(channel->Connection(), channel), SUCCESS);
  const ProtocolHeader header{kMagicNumber, sizeof(ControlMsgType)};
  const ControlMsgType type = ControlMsgType::kHeartBeat;
  ASSERT_EQ(write(fds[1], &header, sizeof(header)), static_cast<ssize_t>(sizeof(header)));
  ASSERT_EQ(write(fds[1], &type, sizeof(type)), static_cast<ssize_t>(sizeof(type)));
  EXPECT_FALSE(manager_.ProcessPendingMsg());
  EXPECT_EQ(channel->bytes_received_, 0U);
  EXPECT_EQ(manager_.HandleEpoolEvents(), SUCCESS);
  EXPECT_EQ(channel->bytes_received_, 0U);
  channel->connection_ = nullptr;
  manager_.fd_to_channel_map_.clear();
  close(fds[0]);
  close(fds[1]);
}

TEST_F(ChannelManagerUnitTest, ProcessReceivedDataAcceptsBodySizeAtLimit) {
  auto channel = CreateChannelWithHeader(kMaxControlMsgBodySizeInBytes);

  EXPECT_EQ(manager_.ProcessReceivedData(channel), SUCCESS);
  EXPECT_EQ(channel->recv_state_, RecvState::WAITING_FOR_BODY);
  EXPECT_EQ(channel->expected_body_size_, kMaxControlMsgBodySizeInBytes);
  EXPECT_EQ(channel->bytes_received_, 0U);
}

TEST_F(ChannelManagerUnitTest, ProcessReceivedDataRejectsOversizedBodySize) {
  auto channel = CreateChannelWithHeader(kMaxControlMsgBodySizeInBytes + 1U);

  EXPECT_EQ(manager_.ProcessReceivedData(channel), FAILED);
  EXPECT_EQ(channel->recv_state_, RecvState::WAITING_FOR_HEADER);
  EXPECT_EQ(channel->expected_body_size_, 0U);
  EXPECT_EQ(channel->bytes_received_, 0U);
  EXPECT_TRUE(channel->recv_buffer_.empty());
}

TEST_F(ChannelManagerUnitTest, ProcessReceivedDataRejectsInvalidMagicNumber) {
  auto channel = CreateChannelWithHeader(kMaxControlMsgBodySizeInBytes, kMagicNumber + 1U);

  EXPECT_EQ(manager_.ProcessReceivedData(channel), FAILED);
  EXPECT_EQ(channel->recv_state_, RecvState::WAITING_FOR_HEADER);
  EXPECT_EQ(channel->expected_body_size_, 0U);
  EXPECT_EQ(channel->bytes_received_, 0U);
  EXPECT_TRUE(channel->recv_buffer_.empty());
}

TEST_F(ChannelManagerUnitTest, HandleNotifyMessage_WhenStorageLimitExceeded_ReturnsFailed) {
  ChannelInfo channel_info{};
  channel_info.channel_type = ChannelType::kServer;
  channel_info.channel_id = kChannelId;
  auto channel = std::make_shared<CommChannel>(channel_info);

  // Fill notify messages to reach limit
  for (size_t i = 0; i < kMaxNotifyStorageSize; ++i) {
    EXPECT_EQ(manager_.HandleNotifyMessage(channel, CreateNotifyMsgStr(i)), SUCCESS);
  }
  EXPECT_EQ(channel->notify_messages_.size(), kMaxNotifyStorageSize);

  // Attempt to add one more notify message
  EXPECT_EQ(manager_.HandleNotifyMessage(channel, CreateNotifyMsgStr(kMaxNotifyStorageSize)), FAILED);
  EXPECT_EQ(channel->notify_messages_.size(), kMaxNotifyStorageSize);
}

// Helper function to create RequestDisconnectMsg string
std::string CreateRequestDisconnectMsgStr(uint64_t req_id, const std::string &channel_id, uint64_t timeout = 1000) {
  RequestDisconnectMsg msg;
  msg.req_id = req_id;
  msg.channel_id = channel_id;
  msg.timeout = timeout;
  std::string serialized_str;
  ControlMsgHandler::Serialize(msg, serialized_str);
  return serialized_str;
}

TEST_F(ChannelManagerUnitTest, HandleRequestDisconnectMessage_WhenDisconnectCallbackFails_LogsWarning) {
  // Set up a mock disconnect callback that returns failure
  bool callback_invoked = false;
  manager_.SetDisconnectCallback([&callback_invoked](const ChannelPtr &channel, int32_t timeout_ms) -> Status {
    callback_invoked = true;
    (void)channel;
    (void)timeout_ms;
    return FAILED;  // Return failure to trigger the LLMLOGW line
  });

  // Create a channel with zero transfer count so can_disconnect will be true
  ChannelInfo channel_info{};
  channel_info.channel_type = ChannelType::kServer;
  channel_info.channel_id = kChannelId;
  auto channel = std::make_shared<CommChannel>(channel_info);

  // Verify transfer count is 0
  EXPECT_EQ(channel->GetTransferCount(), 0);

  // Create and handle the disconnect request message
  std::string msg_str = CreateRequestDisconnectMsgStr(1U, kChannelId, 1000);
  EXPECT_EQ(manager_.HandleRequestDisconnectMessage(channel, msg_str), SUCCESS);
  EXPECT_TRUE(callback_invoked);
}

TEST_F(ChannelManagerUnitTest, HandleRequestDisconnectMessage_RejectsTimeoutExceedingInt32Max) {
  // A peer-controlled timeout above INT32_MAX must be rejected, otherwise it is narrowed to a negative int32_t
  // which corrupts the downstream socket timeout and evicts the channel with a half-open connection.
  bool callback_invoked = false;
  manager_.SetDisconnectCallback([&callback_invoked](const ChannelPtr &channel, int32_t timeout_ms) -> Status {
    callback_invoked = true;
    (void)channel;
    (void)timeout_ms;
    return SUCCESS;
  });
  ChannelInfo channel_info{};
  channel_info.channel_type = ChannelType::kServer;
  channel_info.channel_id = kChannelId;
  auto channel = std::make_shared<CommChannel>(channel_info);

  std::string msg_str = CreateRequestDisconnectMsgStr(1U, kChannelId, static_cast<uint64_t>(INT32_MAX) + 1U);
  EXPECT_EQ(manager_.HandleRequestDisconnectMessage(channel, msg_str), PARAM_INVALID);
  EXPECT_FALSE(callback_invoked);
}

TEST_F(ChannelManagerUnitTest, HandleRequestDisconnectMessage_AcceptsTimeoutAtInt32MaxBoundary) {
  int32_t received_timeout = 0;
  manager_.SetDisconnectCallback([&received_timeout](const ChannelPtr &channel, int32_t timeout_ms) -> Status {
    (void)channel;
    received_timeout = timeout_ms;
    return SUCCESS;
  });

  ChannelInfo channel_info{};
  channel_info.channel_type = ChannelType::kServer;
  channel_info.channel_id = kChannelId;
  auto channel = std::make_shared<CommChannel>(channel_info);

  std::string msg_str = CreateRequestDisconnectMsgStr(1U, kChannelId, static_cast<uint64_t>(INT32_MAX));
  EXPECT_EQ(manager_.HandleRequestDisconnectMessage(channel, msg_str), SUCCESS);
  EXPECT_EQ(received_timeout, INT32_MAX);
}

TEST_F(ChannelManagerUnitTest, HandleRequestDisconnectMessage_WhenChannelIdMismatched_RejectsWithoutCallback) {
  bool callback_invoked = false;
  manager_.SetDisconnectCallback([&callback_invoked](const ChannelPtr &channel, int32_t timeout_ms) -> Status {
    callback_invoked = true;
    (void)channel;
    (void)timeout_ms;
    return SUCCESS;
  });
  ChannelInfo channel_info{};
  channel_info.channel_type = ChannelType::kServer;
  channel_info.channel_id = kChannelId;
  auto channel = std::make_shared<CommChannel>(channel_info);

  // Forged target id: a disconnect request must only ever act on the channel it arrives on, so it is rejected
  // without invoking the disconnect callback.
  std::string forged = CreateRequestDisconnectMsgStr(2U, "127.0.0.1:28999", 1000);
  EXPECT_EQ(manager_.HandleRequestDisconnectMessage(channel, forged), SUCCESS);
  EXPECT_FALSE(callback_invoked);
}

// A successful enqueue must still reply with error_code=0: the server always waits for a
// kRequestDisconnectResp, staying silent costs it a full kWaitRespTime timeout per evicted channel (issue 959).
TEST_F(ChannelManagerUnitTest, HandleRequestDisconnectMessage_WhenCallbackSucceeds_SendsZeroErrorCodeResp) {
  manager_.SetDisconnectCallback([](const ChannelPtr &channel, int32_t timeout_ms) -> Status {
    (void)channel;
    (void)timeout_ms;
    return SUCCESS;  // Callback success means the eviction is enqueued, the actual disconnect is asynchronous
  });

  ChannelInfo channel_info{};
  channel_info.channel_type = ChannelType::kServer;
  channel_info.channel_id = kChannelId;
  auto channel = std::make_shared<CommChannel>(channel_info);

  int socket_pair[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, socket_pair), 0);
  channel->connection_ = hixl::test::Connection(socket_pair[0]);

  constexpr uint64_t kReqId = 7U;
  std::string msg_str = CreateRequestDisconnectMsgStr(kReqId, kChannelId, 1000);
  ASSERT_EQ(manager_.HandleRequestDisconnectMessage(channel, msg_str), SUCCESS);

  // The response is sent synchronously inside the call above, so the full frame is already buffered on the peer
  // end: ProtocolHeader + ControlMsgType + JSON body.
  ProtocolHeader header{};
  ASSERT_EQ(read(socket_pair[1], &header, sizeof(header)), static_cast<ssize_t>(sizeof(header)));
  EXPECT_EQ(header.magic, kMagicNumber);
  ASSERT_GE(header.body_size, sizeof(ControlMsgType));

  int32_t msg_type = 0;
  ASSERT_EQ(read(socket_pair[1], &msg_type, sizeof(msg_type)), static_cast<ssize_t>(sizeof(msg_type)));
  EXPECT_EQ(msg_type, static_cast<int32_t>(ControlMsgType::kRequestDisconnectResp));

  std::string body(header.body_size - sizeof(ControlMsgType), '\0');
  ASSERT_EQ(read(socket_pair[1], body.data(), body.size()), static_cast<ssize_t>(body.size()));

  RequestDisconnectResp resp{};
  ASSERT_EQ(ControlMsgHandler::Deserialize(body.c_str(), resp), SUCCESS);
  EXPECT_EQ(resp.req_id, kReqId);
  EXPECT_EQ(resp.channel_id, kChannelId);
  EXPECT_EQ(resp.error_code, 0U);
  EXPECT_TRUE(resp.can_disconnect);
  EXPECT_FALSE(resp.disconnected);
  EXPECT_TRUE(resp.error_message.empty());

  close(socket_pair[0]);
  close(socket_pair[1]);
}

TEST_F(ChannelManagerUnitTest, HandleControlMessageRejectsBodySmallerThanMsgType) {
  ChannelInfo channel_info{};
  channel_info.channel_type = ChannelType::kServer;
  channel_info.channel_id = kChannelId;
  auto channel = std::make_shared<CommChannel>(channel_info);
  channel->recv_buffer_.resize(sizeof(ControlMsgType));
  // A body shorter than ControlMsgType must be rejected so the payload length cannot underflow into a huge value.
  channel->expected_body_size_ = sizeof(ControlMsgType) - 1U;
  EXPECT_EQ(manager_.HandleControlMessage(channel), FAILED);
}

TEST_F(ChannelManagerUnitTest, CloseEpollFdReleasesDescriptor) {
  const int fd = epoll_create1(0);
  ASSERT_GE(fd, 0);
  manager_.epoll_fd_ = fd;
  manager_.CloseEpollFd();
  EXPECT_EQ(manager_.epoll_fd_, -1);
  errno = 0;
  EXPECT_EQ(fcntl(fd, F_GETFD), -1);
  EXPECT_EQ(errno, EBADF);
}

TEST_F(ChannelManagerUnitTest, CloseEpollFdIsIdempotent) {
  manager_.epoll_fd_ = -1;
  manager_.CloseEpollFd();
  EXPECT_EQ(manager_.epoll_fd_, -1);
}

TEST_F(ChannelManagerUnitTest, FinalizeClosesEpollFd) {
  const int fd = epoll_create1(0);
  ASSERT_GE(fd, 0);
  manager_.epoll_fd_ = fd;
  EXPECT_EQ(manager_.Finalize(), SUCCESS);
  EXPECT_EQ(manager_.epoll_fd_, -1);
  errno = 0;
  EXPECT_EQ(fcntl(fd, F_GETFD), -1);
  EXPECT_EQ(errno, EBADF);
}

TEST_F(ChannelManagerUnitTest, DestructorClosesEpollFd) {
  int fd = -1;
  {
    ChannelManager mgr;
    fd = epoll_create1(0);
    ASSERT_GE(fd, 0);
    mgr.epoll_fd_ = fd;
  }
  errno = 0;
  EXPECT_EQ(fcntl(fd, F_GETFD), -1);
  EXPECT_EQ(errno, EBADF);
}

TEST_F(ChannelManagerUnitTest, DestructorDoesNotPropagateExceptions) {
  // Finalize() allocates while collecting channels, so the destructor must swallow any exception it may throw.
  static_assert(std::is_nothrow_destructible<ChannelManager>::value,
                "ChannelManager destructor must not propagate exceptions");
  EXPECT_NO_THROW({ ChannelManager mgr; });
}

TEST_F(ChannelManagerUnitTest, DestructorExceptionsStillCloseEpollFd) {
  // 析构函数的 catch 分支会在 bad_alloc 下调用 CloseEpollFd()，它自身必须不抛异常，
  // 否则异常会从 noexcept 析构函数漏出并触发 std::terminate。
  static_assert(noexcept(std::declval<ChannelManager &>().CloseEpollFd()),
                "CloseEpollFd must be noexcept so the destructor's catch path cannot throw");
  // 模拟 Finalize() 被异常中断后的状态：epoll_fd_ 仍被持有，catch 分支需要兜底关闭它。
  const int fd = epoll_create1(0);
  ASSERT_GE(fd, 0);
  manager_.epoll_fd_ = fd;
  manager_.CloseEpollFd();
  EXPECT_EQ(manager_.epoll_fd_, -1);
  errno = 0;
  EXPECT_EQ(fcntl(fd, F_GETFD), -1);
  EXPECT_EQ(errno, EBADF);
  // Finalize() 已关闭 fd 时 catch 仍可能再调一次，重复调用必须安全。
  manager_.CloseEpollFd();
  EXPECT_EQ(manager_.epoll_fd_, -1);
}
}  // namespace
}  // namespace adxl
