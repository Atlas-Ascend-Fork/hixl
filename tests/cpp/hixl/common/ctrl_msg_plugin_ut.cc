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
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <gtest/gtest.h>
#include "common/ctrl_msg_plugin.h"
#include "common/scope_guard.h"

namespace hixl {
TEST(CtrlMsgPluginTest, RejectsLengthThatCannotBeRepresentedBySsizeT) {
  if (std::numeric_limits<size_t>::digits <= std::numeric_limits<ssize_t>::digits) {
    GTEST_SKIP() << "size_t does not provide a value outside the ssize_t range";
  }

  const size_t first_invalid_length = static_cast<size_t>(std::numeric_limits<ssize_t>::max()) + 1U;
  const std::array<size_t, 2> invalid_lengths = {first_invalid_length, std::numeric_limits<size_t>::max()};
  char byte = 0;
  for (const size_t len : invalid_lengths) {
    int32_t err_no = -1;
    EXPECT_EQ(CtrlMsgPlugin::Send(-1, &byte, len, CtrlMsgPlugin::kDefaultCtrlTimeoutMs, err_no), PARAM_INVALID)
        << "len=" << len;
    EXPECT_EQ(CtrlMsgPlugin::Recv(-1, &byte, len, 0), PARAM_INVALID) << "len=" << len;
  }
}

TEST(CtrlMsgPluginTest, AcceptsRepresentableBoundaryLengths) {
  if (std::numeric_limits<size_t>::digits < std::numeric_limits<ssize_t>::digits) {
    GTEST_SKIP() << "size_t cannot represent ssize_t max";
  }

  const size_t max_length = static_cast<size_t>(std::numeric_limits<ssize_t>::max());
  char byte = 0;
  int32_t err_no = -1;
  EXPECT_EQ(CtrlMsgPlugin::Send(-1, &byte, 0, CtrlMsgPlugin::kDefaultCtrlTimeoutMs, err_no), SUCCESS);
  EXPECT_EQ(CtrlMsgPlugin::Send(-1, &byte, max_length, CtrlMsgPlugin::kDefaultCtrlTimeoutMs, err_no), FAILED);
  EXPECT_EQ(CtrlMsgPlugin::Recv(-1, &byte, 0, 0), SUCCESS);
  EXPECT_EQ(CtrlMsgPlugin::Recv(-1, &byte, max_length, 0), TIMEOUT);
}

TEST(CtrlMsgPluginTest, SendsSmallBuffer) {
  int32_t fds[2] = {-1, -1};
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  HIXL_MAKE_GUARD(close_fds, ([&fds]() {
                    (void)close(fds[0]);
                    (void)close(fds[1]);
                  }));

  const std::array<char, 7> expected = {'h', 'i', 'x', 'l', '-', 'u', 't'};
  ASSERT_EQ(CtrlMsgPlugin::Send(fds[0], expected.data(), expected.size()), SUCCESS);

  std::array<char, 7> actual = {};
  ASSERT_EQ(read(fds[1], actual.data(), actual.size()), static_cast<ssize_t>(actual.size()));
  EXPECT_EQ(actual, expected);
}

TEST(CtrlMsgPluginTest, SendWithExplicitTimeoutCompletes) {
  int32_t fds[2] = {-1, -1};
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  HIXL_MAKE_GUARD(close_fds, ([&fds]() {
                    (void)close(fds[0]);
                    (void)close(fds[1]);
                  }));

  const char payload = 's';
  ASSERT_EQ(CtrlMsgPlugin::Send(fds[0], &payload, sizeof(payload), 1000U), SUCCESS);
  char actual = 0;
  ASSERT_EQ(read(fds[1], &actual, sizeof(actual)), static_cast<ssize_t>(sizeof(actual)));
  EXPECT_EQ(actual, payload);
}

TEST(CtrlMsgPluginTest, SendTimesOutWhenPeerDoesNotRead) {
  int32_t fds[2] = {-1, -1};
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  HIXL_MAKE_GUARD(close_fds, ([&fds]() {
                    (void)close(fds[0]);
                    (void)close(fds[1]);
                  }));

  const int32_t flags = fcntl(fds[0], F_GETFL, 0);
  ASSERT_GE(flags, 0);
  ASSERT_EQ(fcntl(fds[0], F_SETFL, flags | O_NONBLOCK), 0);
  const std::vector<char> chunk(4096, 'x');
  bool buffer_full = false;
  for (;;) {
    const ssize_t n = write(fds[0], chunk.data(), chunk.size());
    if (n < 0) {
      ASSERT_TRUE(errno == EAGAIN || errno == EWOULDBLOCK);
      buffer_full = true;
      break;
    }
    ASSERT_GT(n, 0);
  }
  ASSERT_TRUE(buffer_full);
  ASSERT_EQ(fcntl(fds[0], F_SETFL, flags), 0);

  struct timeval send_timeout = {};
  send_timeout.tv_sec = 0;
  send_timeout.tv_usec = 100 * 1000;
  ASSERT_EQ(setsockopt(fds[0], SOL_SOCKET, SO_SNDTIMEO, &send_timeout, sizeof(send_timeout)), 0);

  constexpr uint32_t kTimeoutMs = 300U;
  alarm(3);
  const auto start = std::chrono::steady_clock::now();
  const Status ret = CtrlMsgPlugin::Send(fds[0], chunk.data(), chunk.size(), kTimeoutMs);
  const auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
  alarm(0);

  EXPECT_EQ(ret, TIMEOUT);
  EXPECT_GE(elapsed_ms, 200);
  EXPECT_LT(elapsed_ms, 2000);
}

TEST(CtrlMsgPluginTest, SetSendTimeoutRejectsInvalidFdAndStoresTimeout) {
  EXPECT_EQ(CtrlMsgPlugin::SetSendTimeout(-1, CtrlMsgPlugin::kDefaultCtrlTimeoutMs), FAILED);

  int32_t fds[2] = {-1, -1};
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  HIXL_MAKE_GUARD(close_fds, ([&fds]() {
                    (void)close(fds[0]);
                    (void)close(fds[1]);
                  }));

  ASSERT_EQ(CtrlMsgPlugin::SetSendTimeout(fds[0], 1500U), SUCCESS);
  struct timeval actual = {};
  socklen_t actual_len = sizeof(actual);
  ASSERT_EQ(getsockopt(fds[0], SOL_SOCKET, SO_SNDTIMEO, &actual, &actual_len), 0);
  EXPECT_EQ(actual.tv_sec, 1);
  EXPECT_EQ(actual.tv_usec, 500 * 1000);
}

TEST(CtrlMsgPluginTest, FailedListenPreservesOutputFd) {
  int32_t listen_fd = -1;
  ASSERT_EQ(CtrlMsgPlugin::Listen("127.0.0.1", 0, 8, listen_fd), SUCCESS);
  HIXL_MAKE_GUARD(close_listen_fd, ([listen_fd]() { (void)close(listen_fd); }));
  sockaddr_in addr{};
  socklen_t addr_len = sizeof(addr);
  ASSERT_EQ(getsockname(listen_fd, reinterpret_cast<sockaddr *>(&addr), &addr_len), 0);

  int32_t output_fd = listen_fd;
  EXPECT_EQ(CtrlMsgPlugin::Listen("127.0.0.1", ntohs(addr.sin_port), 8, output_fd), FAILED);
  EXPECT_EQ(output_fd, listen_fd);
}
}  // namespace hixl
