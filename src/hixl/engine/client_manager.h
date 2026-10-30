/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HIXL_SRC_HIXL_ENGINE_CLIENT_MANAGER_H_
#define HIXL_SRC_HIXL_ENGINE_CLIENT_MANAGER_H_

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include "hixl_client.h"
#include "common/hixl_inner_types.h"

namespace hixl {
using ClientPtr = std::shared_ptr<HixlClient>;
struct TransferReqInfo {
  TransferReq req = nullptr;
  ClientPtr client = nullptr;
  const void *user_data = nullptr;
};

class ClientManager {
 public:
  ClientManager() = default;
  ~ClientManager() = default;
  /**
   * @brief 初始化client管理器
   * @param [in] auto_connect 是否使能自动建链，使能后启动心跳线程
   * @param [in] max_channel 链路池允许的最大链路个数，未配置时不限制链路个数
   * @return 成功:SUCCESS, 失败:其它.
   */
  Status Initialize(bool auto_connect, std::optional<uint32_t> max_channel = std::nullopt);
  Status Finalize();
  Status GetOrCreateClient(const ClientConfig &config, const std::vector<MemHandleInfo> &mem_info_list,
                           int32_t timeout_in_millis, ClientPtr &client_ptr, bool *transfer_use = nullptr);
  ClientPtr GetClient(const std::string &remote_engine);
  // 链路池开启时的传输取链。命中后在 manager 锁内给这条链路的占用加一。
  ClientPtr GetClientForUse(const std::string &remote_engine, bool &transfer_use);
  ClientPtr GetClientByReq(const TransferReq &req);
  Status DestroyClient(const std::string &remote_engine);
  void RegisterTransferReq(const TransferReq &req, const ClientPtr &client, const void *user_data);
  void EraseTransferReq(const TransferReq &req);
  std::vector<TransferReqInfo> GetOrderedReqs(size_t max_count);
  bool IsEmpty() const;
  Status DeregisterMem(MemHandle mem_handle);

 private:
  struct ReqOwner {
    std::weak_ptr<HixlClient> client;
    const void *user_data = nullptr;
    std::list<TransferReq>::iterator order_it;
  };

  Status StartHeartbeat();
  Status CreateClient(const ClientConfig &config, ClientPtr &client_ptr) const;
  // 未配置 max_channel 时不占名额。占到名额时 reserved 为 true，并且持有该远端的
  // client_lock，调用方建链结束后释放名额。
  // transfer_use 非空时，把即将交给本次传输的链路占用加一。
  Status ReserveChannelSlot(const std::string &remote_engine, std::unique_lock<std::mutex> &client_lock,
                            ClientPtr &client_ptr, bool &reserved, bool *transfer_use = nullptr);
  void ReleaseChannelSlot();
  void DestroyIdleClients(const std::vector<std::string> &victims, const std::string &new_remote_engine);
  // 淘汰前再确认一次。占用中的链路留在池里，removed 为 false。
  Status DestroyIdleClient(const std::string &remote_engine, bool &removed);
  std::vector<std::string> SelectLruIdleClientsLocked(size_t evict_count);
  void TouchClientLocked(const std::string &remote_engine);
  void ClaimTransferUseLocked(const ClientPtr &client, bool &transfer_use);
  struct ChannelAdmit {
    ClientPtr client;
    bool already_connected = false;
    bool reserved = false;
    bool pool_exhausted = false;
    std::vector<std::string> victims;
    size_t client_count = 0U;
    size_t creating_count = 0U;
    size_t idle_count = 0U;
    size_t evict_count = 0U;
  };
  void EvaluateChannelAdmit(const std::string &remote_engine, bool *transfer_use, ChannelAdmit &admit);
  Status ReportPoolExhausted(const std::string &remote_engine, const ChannelAdmit &admit) const;
  Status FinishEvict(const std::string &remote_engine, const std::vector<std::string> &victims);
  std::shared_ptr<std::mutex> GetClientMutex(const std::string &remote_engine);
  void DestroyClientMutex(const std::string &remote_engine);
  void SendHeartbeat();
  void EraseTransferReqLocked(const TransferReq &req);
  void EraseReqIndexByClient(const ClientPtr &client);

  mutable std::mutex mutex_;
  std::map<std::string, ClientPtr> clients_;
  // Access order of clients_, used to pick the least recently used link when the channel pool is full.
  std::unordered_map<std::string, uint64_t> last_access_seq_;
  uint64_t access_seq_ = 0U;
  std::optional<uint32_t> max_channel_;
  // In-flight connects already counted against max_channel_. Occupancy is clients_.size() + creating_count_.
  size_t creating_count_ = 0U;
  bool finalized_ = false;
  std::mutex client_mutexes_mutex_;
  std::unordered_map<std::string, std::shared_ptr<std::mutex>> client_mutexes_;

  std::thread heartbeat_sender_;
  std::atomic<bool> stop_signal_{false};
  std::mutex cv_mutex_;
  std::condition_variable cv_;
  bool auto_connect_{false};

  std::mutex req_index_mutex_;
  std::list<TransferReq> ordered_reqs_;
  std::unordered_map<TransferReq, ReqOwner> req_to_client_;
};
}  // namespace hixl

#endif  // HIXL_SRC_HIXL_ENGINE_CLIENT_MANAGER_H_
