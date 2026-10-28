/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HIXL_TESTS_DEPENDS_CTRL_MSG_TEST_HELPER_H_
#define HIXL_TESTS_DEPENDS_CTRL_MSG_TEST_HELPER_H_

#include <fcntl.h>
#include <mutex>
#include <shared_mutex>
#include <gtest/gtest.h>
#include "common/tls/tls_manager.h"

#define private public
#include "common/ctrl_msg_plugin.h"
#undef private

namespace hixl {
struct CtrlMsgPluginTestAccess {
  static CtrlMsgPluginPtr Own(int fd) {
    auto connection = std::make_shared<CtrlMsgPlugin>();
    if (connection->Initialize() != SUCCESS) {
      if (fd >= 0) {
        (void)close(fd);
      }
      return nullptr;
    }
    connection->fd_ = fd;
    return connection;
  }
  static std::shared_ptr<TlsSession> Session(const CtrlMsgPluginPtr &connection) {
    return connection->tls_session_;
  }
  static void SetSession(const CtrlMsgPluginPtr &connection, std::shared_ptr<TlsSession> session) {
    connection->tls_session_ = std::move(session);
  }
  static bool IsInitialized(const CtrlMsgPluginPtr &connection) {
    return connection->initialized_;
  }
  static bool IsTlsEnabled(const CtrlMsgPluginPtr &connection) {
    return connection->tls_enabled_;
  }
};
namespace test {
class TlsOwner {
 public:
  TlsOwner() = default;
  TlsOwner(const TlsOwner &) = delete;
  TlsOwner &operator=(const TlsOwner &) = delete;
  ~TlsOwner() {
    if (initialized_) {
      TlsManager::GetInstance().Finalize();
    }
  }
  Status Initialize(const TlsConfig &config = {}) {
    if (initialized_) {
      return FAILED;
    }
    const auto ret = TlsManager::GetInstance().Initialize(config);
    initialized_ = ret == SUCCESS;
    return ret;
  }

 private:
  bool initialized_ = false;
};

// The base owns the process mode before derived fixture members create connections, and releases it after them.
class PlainTlsTest : public ::testing::Test {
 protected:
  PlainTlsTest() : status_(owner_.Initialize()) {}
  void SetUp() override {
    ASSERT_EQ(status_, SUCCESS);
  }

 private:
  TlsOwner owner_;
  Status status_;
};

// Duplicate borrowed fixture sockets so plugin destruction never closes the fixture's descriptor.
inline CtrlMsgPluginPtr Connection(int fd) {
  return CtrlMsgPluginTestAccess::Own(fd < 0 ? -1 : dup(fd));
}
inline CtrlMsgPluginPtr Connection(const CtrlMsgPluginPtr &connection) {
  return connection;
}
inline Status Connect(const std::string &ip, uint32_t port, int32_t &fd, int32_t timeout) {
  auto connection = std::make_shared<CtrlMsgPlugin>();
  const auto init = connection->Initialize();
  if (init != SUCCESS) {
    return init;
  }
  const auto ret = connection->Connect(ip, port, timeout);
  fd = ret == SUCCESS ? dup(connection->Fd()) : -1;
  return ret;
}
}  // namespace test
}  // namespace hixl
#endif
