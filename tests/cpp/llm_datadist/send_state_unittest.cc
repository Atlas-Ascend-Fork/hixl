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

#include <cstdint>

#define private public
#include "fsm/send_state.h"
#undef private

namespace llm {
namespace {
void BindRequest(CommEntity &entity, CacheManager &cache_manager, TransferCacheReq &request) {
  entity.SetCacheManager(&cache_manager);
  entity.info_.local_req_ptr = &request;
}

void IndexBlocksCache(CacheManager &cache_manager, uint64_t req_id, uint64_t model_id, int64_t cache_id) {
  CacheEntry entry{};
  entry.num_blocks = 64U;
  entry.batch_size = 1U;
  entry.stride = 128U;
  entry.placement = CachePlacement::DEVICE;
  cache_manager.cache_id_to_entry_[cache_id] = entry;
  cache_manager.cache_key_to_id_[std::make_pair(req_id, model_id)] = cache_id;
}

void FillOrdinaryPull(TransferCacheReq &request, uint64_t req_id, uint64_t model_id) {
  request.is_pull_block = 0U;
  request.cache_id = -1;
  request.req_id = req_id;
  request.model_id = model_id;
  request.prefix_id = UINT64_MAX;
}
}  // namespace

TEST(SendStateTest, OrdinaryPullOfBlocksCacheReturnsParamInvalid) {
  constexpr uint64_t kReqId = 101U;
  constexpr uint64_t kModelId = 1U;
  CacheManager cache_manager;
  IndexBlocksCache(cache_manager, kReqId, kModelId, 1);
  TransferCacheReq request{};
  FillOrdinaryPull(request, kReqId, kModelId);
  CommEntity entity(0U, 0U, 0U, 0U, 0U);
  BindRequest(entity, cache_manager, request);

  CacheEntry cache_entry{};
  uint64_t offset = 0U;
  EXPECT_EQ(SendState::QueryCacheEntryAndOffset(entity, cache_entry, offset), ge::LLM_PARAM_INVALID);
}

TEST(SendStateTest, PullBlocksOfBlocksCacheSkipsBatchIndexLookup) {
  constexpr uint64_t kReqId = 101U;
  constexpr uint64_t kModelId = 1U;
  CacheManager cache_manager;
  IndexBlocksCache(cache_manager, kReqId, kModelId, 1);
  TransferCacheReq request{};
  FillOrdinaryPull(request, kReqId, kModelId);
  request.is_pull_block = 1U;
  request.block_size = 128U;
  CommEntity entity(0U, 0U, 0U, 0U, 0U);
  BindRequest(entity, cache_manager, request);

  CacheEntry cache_entry{};
  uint64_t offset = 1U;
  EXPECT_EQ(SendState::QueryCacheEntryAndOffset(entity, cache_entry, offset), ge::SUCCESS);
  EXPECT_EQ(offset, 0U);
}

TEST(SendStateTest, MissingBatchIndexReturnsCacheNotExist) {
  constexpr uint64_t kReqId = 7U;
  constexpr uint64_t kModelId = 1U;
  constexpr int64_t kCacheId = 3;
  CacheManager cache_manager;
  CacheEntry entry{};
  entry.num_blocks = 0U;
  entry.stride = 64U;
  cache_manager.cache_id_to_entry_[kCacheId] = entry;
  cache_manager.cache_key_to_id_[std::make_pair(kReqId, kModelId)] = kCacheId;
  TransferCacheReq request{};
  FillOrdinaryPull(request, kReqId, kModelId);
  CommEntity entity(0U, 0U, 0U, 0U, 0U);
  BindRequest(entity, cache_manager, request);

  CacheEntry cache_entry{};
  uint64_t offset = 0U;
  EXPECT_EQ(SendState::QueryCacheEntryAndOffset(entity, cache_entry, offset), ge::LLM_KV_CACHE_NOT_EXIST);
}

TEST(SendStateTest, OrdinaryPullUsesBatchIndexAsOffset) {
  constexpr uint64_t kReqId = 9U;
  constexpr uint64_t kModelId = 2U;
  constexpr int64_t kCacheId = 4;
  constexpr uint32_t kBatchIndex = 3U;
  constexpr uint64_t kStride = 128U;
  CacheManager cache_manager;
  CacheEntry entry{};
  entry.num_blocks = 0U;
  entry.stride = kStride;
  entry.id_to_batch_index_and_size[kReqId] = std::make_pair(kBatchIndex, kStride);
  cache_manager.cache_id_to_entry_[kCacheId] = entry;
  cache_manager.cache_key_to_id_[std::make_pair(kReqId, kModelId)] = kCacheId;
  TransferCacheReq request{};
  FillOrdinaryPull(request, kReqId, kModelId);
  CommEntity entity(0U, 0U, 0U, 0U, 0U);
  BindRequest(entity, cache_manager, request);

  CacheEntry cache_entry{};
  uint64_t offset = 0U;
  EXPECT_EQ(SendState::QueryCacheEntryAndOffset(entity, cache_entry, offset), ge::SUCCESS);
  EXPECT_EQ(offset, static_cast<uint64_t>(kBatchIndex) * kStride);
}
}  // namespace llm
