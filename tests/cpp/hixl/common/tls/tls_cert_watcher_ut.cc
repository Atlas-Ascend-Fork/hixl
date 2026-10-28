/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <gtest/gtest.h>

#include <chrono>
#include <fstream>
#include <thread>

#include "common/tls/tls_manager.h"
#include "common/tls/tls_test_helper.h"
#include "hixl/hixl_types.h"

namespace hixl {

class TlsCertWatcherUt : public ::testing::Test {
 protected:
  TlsManager &manager_ = TlsManager::GetInstance();
  bool initialized_ = false;
  void TearDown() override {
    if (initialized_) {
      manager_.Finalize();
    }
    test::RemovePath(dir_ + "/cert.pem");
    test::RemovePath(dir_ + "/key.pem");
    test::RemovePath(dir_);
  }

  std::string dir_;
};

TEST_F(TlsCertWatcherUt, InvalidReloadKeepsOldContext) {
  dir_ = test::MakeTempDir();
  ASSERT_FALSE(dir_.empty());
  if (!test::GenerateSelfSignedCert(dir_)) {
    GTEST_SKIP() << "openssl is not available to generate test certificates";
  }
  TlsConfig cfg = test::MakeTlsConfig(dir_, false, true);
  ASSERT_EQ(manager_.Initialize(cfg), SUCCESS);
  initialized_ = true;
  auto old_ctx = manager_.GetContext();
  ASSERT_NE(old_ctx, nullptr);

  std::ofstream broken(dir_ + "/cert.pem", std::ios::trunc);
  broken << "not-a-cert";
  broken.close();
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  auto cur_ctx = manager_.GetContext();
  EXPECT_EQ(old_ctx.get(), cur_ctx.get());
}
}  // namespace hixl
