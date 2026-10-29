/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE. See LICENSE
 * in the root of the software repository for the full text of the License.
 */

#include <cstddef>
#include <cstdint>
#include <memory>

#include <gtest/gtest.h>

#define private public
#include "llm_datadist_v2_wrapper.h"
#undef private

namespace {
void PoisonStack() {
  volatile uint32_t poison[256];
  for (size_t i = 0U; i < 256U; ++i) {
    poison[i] = 0xAAAAAAAAU;
  }
}
}  // namespace

TEST(LLMDataDistV2WrapperTest, ReturnsZeroStatusWhenQueryRegisterMemStatusFails) {
  llm::LLMDataDistV2Wrapper::llm_data_dist = std::make_unique<llm::LLMDataDistV2>(0UL);
  PoisonStack();

  const auto ret = llm::LLMDataDistV2Wrapper::QueryRegisterMemStatus(0UL);
  EXPECT_EQ(ret.first, ge::FAILED);
  EXPECT_EQ(ret.second, 0U);

  llm::LLMDataDistV2Wrapper::llm_data_dist.reset();
}
