/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <string>
#include <vector>

#include "gtest/gtest.h"

#include "depends/ibverbs/src/ibverbs_stub.h"
#include "hixl/hixl_types.h"
#include "proxy/ibverbs_proxy.h"

namespace hixl {

class IbverbsProxyUTest : public ::testing::Test {
 protected:
  void SetUp() override {
    IbverbsStubReset();
  }
  void TearDown() override {
    IbverbsStubReset();
  }
};

TEST_F(IbverbsProxyUTest, ListNicNamesEmptyFails) {
  std::vector<std::string> names;
  EXPECT_EQ(IbverbsProxy::ListNicNames(names), FAILED);
  EXPECT_TRUE(names.empty());
}

TEST_F(IbverbsProxyUTest, ListNicNamesAndQueryIpv4) {
  IbverbsStubAddIpv4Device("mlx0", 10, 1, 2, 3);
  IbverbsStubAddIpv4Device("mlx1", 10, 1, 2, 4);
  std::vector<std::string> names;
  EXPECT_EQ(IbverbsProxy::ListNicNames(names), SUCCESS);
  ASSERT_EQ(names.size(), 2U);

  std::string ipv4;
  EXPECT_EQ(IbverbsProxy::GetNicIpv4ByName("mlx0", ipv4), SUCCESS);
  EXPECT_EQ(ipv4, "10.1.2.3");
  EXPECT_EQ(IbverbsProxy::GetNicIpv4ByName("mlx1", ipv4), SUCCESS);
  EXPECT_EQ(ipv4, "10.1.2.4");
  EXPECT_EQ(IbverbsProxy::GetNicIpv4ByName("missing", ipv4), FAILED);
}

}  // namespace hixl
