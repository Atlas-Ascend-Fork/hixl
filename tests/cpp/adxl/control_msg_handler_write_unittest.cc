/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cerrno>
#include <cstring>
#include <limits>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "depends/ctrl_msg_test_helper.h"

#include "adxl/control_msg_handler.h"

namespace adxl {
namespace {
constexpr uint64_t kWriteTimeoutUs = 5000000U;

class ControlMsgHandlerWriteTest : public hixl::test::PlainTlsTest {
 protected:
  void SetUp() override {
    ASSERT_NO_FATAL_FAILURE(hixl::test::PlainTlsTest::SetUp());
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds_), 0);
    write_fd_ = fds_[0];
    read_fd_ = fds_[1];
  }

  void TearDown() override {
    if (write_fd_ >= 0) {
      close(write_fd_);
      write_fd_ = -1;
    }
    if (read_fd_ >= 0) {
      close(read_fd_);
      read_fd_ = -1;
    }
  }

  int fds_[2] = {-1, -1};
  int write_fd_ = -1;
  int read_fd_ = -1;
};

TEST_F(ControlMsgHandlerWriteTest, WriteCompletesFullPayload) {
  const std::string payload = "control-msg-payload";
  auto start = std::chrono::steady_clock::now();
  EXPECT_EQ(ControlMsgHandler::Write(hixl::test::Connection(write_fd_), payload.data(), payload.size(), kWriteTimeoutUs,
                                     start),
            SUCCESS);

  std::vector<char> received(payload.size());
  ASSERT_EQ(read(read_fd_, received.data(), received.size()), static_cast<ssize_t>(payload.size()));
  EXPECT_EQ(std::string(received.begin(), received.end()), payload);
}

TEST_F(ControlMsgHandlerWriteTest, WriteHandlesPartialWriteAndEagain) {
  const std::string payload = "partial-write-payload";
  std::thread reader([this, &payload]() {
    char buffer[4] = {};
    ASSERT_EQ(read(read_fd_, buffer, sizeof(buffer)), static_cast<ssize_t>(sizeof(buffer)));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    std::vector<char> remaining(payload.size() - sizeof(buffer));
    ASSERT_EQ(read(read_fd_, remaining.data(), remaining.size()), static_cast<ssize_t>(remaining.size()));
  });

  auto start = std::chrono::steady_clock::now();
  EXPECT_EQ(ControlMsgHandler::Write(hixl::test::Connection(write_fd_), payload.data(), payload.size(), kWriteTimeoutUs,
                                     start),
            SUCCESS);
  reader.join();
}

TEST_F(ControlMsgHandlerWriteTest, WriteUsesRemainingMessageBudgetUnderBackpressure) {
  const int buffer_size = 4096;
  ASSERT_EQ(setsockopt(write_fd_, SOL_SOCKET, SO_SNDBUF, &buffer_size, sizeof(buffer_size)), 0);
  const std::string payload(1024U * 1024U, 'x');
  auto connection = hixl::test::Connection(write_fd_);
  ASSERT_NE(connection, nullptr);
  const auto write_start = std::chrono::steady_clock::now();
  auto message_start = write_start - std::chrono::seconds(1);
  EXPECT_EQ(ControlMsgHandler::Write(connection, payload.data(), payload.size(), 1100000U, message_start), TIMEOUT);
  const auto elapsed = std::chrono::steady_clock::now() - write_start;
  EXPECT_GE(elapsed, std::chrono::milliseconds(80));
  EXPECT_LT(elapsed, std::chrono::milliseconds(1000));
}

TEST_F(ControlMsgHandlerWriteTest, SubMillisecondBudgetCanSend) {
  auto connection = hixl::test::Connection(write_fd_);
  ASSERT_NE(connection, nullptr);
  auto start = std::chrono::steady_clock::now();
  const char byte = 'x';
  EXPECT_EQ(ControlMsgHandler::Write(connection, &byte, 1U, 999U, start), SUCCESS);
  char received = 0;
  EXPECT_EQ(recv(read_fd_, &received, 1U, MSG_DONTWAIT), 1);
  EXPECT_EQ(received, byte);
}

TEST_F(ControlMsgHandlerWriteTest, ExpiredBudgetDoesNotWrite) {
  auto connection = hixl::test::Connection(write_fd_);
  ASSERT_NE(connection, nullptr);
  auto start = std::chrono::steady_clock::now() - std::chrono::seconds(1);
  const char byte = 'x';
  EXPECT_EQ(ControlMsgHandler::Write(connection, &byte, 1U, 1000U, start), TIMEOUT);
  char received = 0;
  EXPECT_EQ(recv(read_fd_, &received, 1U, MSG_DONTWAIT), -1);
  EXPECT_TRUE(errno == EAGAIN || errno == EWOULDBLOCK);
}

TEST_F(ControlMsgHandlerWriteTest, ClosedPeerDoesNotNeedRetry) {
  ASSERT_EQ(close(read_fd_), 0);
  read_fd_ = -1;
  auto start = std::chrono::steady_clock::now();
  const char byte = 'x';
  EXPECT_EQ(ControlMsgHandler::Write(hixl::test::Connection(write_fd_), &byte, 1U, kWriteTimeoutUs, start), 1U);
}

TEST_F(ControlMsgHandlerWriteTest, InvalidConnectionReturnsFailure) {
  auto start = std::chrono::steady_clock::now();
  const char byte = 'x';
  EXPECT_EQ(ControlMsgHandler::Write(hixl::test::Connection(-1), &byte, 1U, kWriteTimeoutUs, start), FAILED);
}

TEST_F(ControlMsgHandlerWriteTest, LargeTimeoutDoesNotOverflow) {
  auto start = std::chrono::steady_clock::now();
  const char byte = 'x';
  EXPECT_EQ(ControlMsgHandler::Write(hixl::test::Connection(write_fd_), &byte, 1U, std::numeric_limits<uint64_t>::max(),
                                     start),
            SUCCESS);
  char received = 0;
  EXPECT_EQ(recv(read_fd_, &received, 1U, MSG_DONTWAIT), 1);
  EXPECT_EQ(received, byte);
}

TEST_F(ControlMsgHandlerWriteTest, EmptyWriteSucceedsAfterDeadline) {
  auto start = std::chrono::steady_clock::now() - std::chrono::seconds(1);
  EXPECT_EQ(ControlMsgHandler::Write(hixl::test::Connection(write_fd_), nullptr, 0U, 0U, start), SUCCESS);
}
}  // namespace
}  // namespace adxl
