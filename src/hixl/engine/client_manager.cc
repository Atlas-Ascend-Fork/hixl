/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <algorithm>
#include <cstdint>
#include <utility>

#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/scope_guard.h"
#include "common/hixl_utils.h"
#include "client_manager.h"

namespace hixl {
namespace {
constexpr int64_t kHeartbeatIntervalMs = 10000;  // 10 seconds
}

Status ClientManager::Initialize(bool auto_connect, std::optional<uint32_t> max_channel) {
  auto_connect_ = auto_connect;
  max_channel_ = max_channel;
  creating_count_ = 0U;
  stop_signal_.store(false);
  if (max_channel_.has_value()) {
    HIXL_EVENT(
        "[ClientManager] channel pool enabled, max_channel:%u, the least recently used idle channel is "
        "destroyed when the pool is full",
        max_channel_.value());
  }
  // 开启Auto Connect才会进行心跳检测
  if (!auto_connect_) {
    return SUCCESS;
  }
  return StartHeartbeat();
}

Status ClientManager::StartHeartbeat() {
  heartbeat_sender_ = std::thread([this]() -> Status {
    std::unique_lock<std::mutex> lock(cv_mutex_);
    while (!stop_signal_.load()) {
      SendHeartbeat();
      cv_.wait_for(lock, std::chrono::milliseconds(kHeartbeatIntervalMs), [this] { return stop_signal_.load(); });
    }
    return SUCCESS;
  });
  return SUCCESS;
}

Status ClientManager::CreateClient(const ClientConfig &config, ClientPtr &client_ptr) const {
  std::string ip;
  int32_t port = 0;
  HIXL_CHK_STATUS_RET(ParseListenInfo(config.remote_engine, ip, port), "Failed to parse ip, remote_engine:%s",
                      config.remote_engine.c_str());
  {
    std::lock_guard<std::mutex> lock(mutex_);
    HIXL_CHK_BOOL_RET_STATUS(!finalized_, FAILED, "ClientManager already finalized, cannot create new client");
  }
  client_ptr = MakeShared<HixlClient>(ip, static_cast<uint32_t>(port), config);
  HIXL_CHECK_NOTNULL(client_ptr, "Failed to create HixlClient, ip:%s, port:%u", ip.c_str(), port);
  HIXL_CHK_STATUS_RET(client_ptr->Initialize(config.endpoint_list, config.timeout_ms, config.is_lazy),
                      "Failed to initialize HixlClient, ip:%s, port:%u", ip.c_str(), port);
  return SUCCESS;
}

void ClientManager::TouchClientLocked(const std::string &remote_engine) {
  if (!max_channel_.has_value()) {
    return;
  }
  ++access_seq_;
  last_access_seq_[remote_engine] = access_seq_;
}

std::vector<std::string> ClientManager::SelectLruIdleClientsLocked(size_t evict_count) {
  std::vector<std::pair<uint64_t, std::string>> idle_clients;
  for (const auto &[remote_engine, client] : clients_) {
    if ((client == nullptr) || client->IsBusy()) {
      continue;
    }
    const auto &seq_it = last_access_seq_.find(remote_engine);
    const uint64_t seq = (seq_it == last_access_seq_.cend()) ? 0U : seq_it->second;
    idle_clients.emplace_back(seq, remote_engine);
  }
  std::sort(idle_clients.begin(), idle_clients.end());
  if (idle_clients.size() > evict_count) {
    idle_clients.resize(evict_count);
  }
  std::vector<std::string> victims;
  victims.reserve(idle_clients.size());
  for (auto &idle_client : idle_clients) {
    victims.emplace_back(std::move(idle_client.second));
  }
  return victims;
}

void ClientManager::ReleaseChannelSlot() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (creating_count_ > 0U) {
    --creating_count_;
  }
}

void ClientManager::ClaimTransferUseLocked(const ClientPtr &client, bool &transfer_use) {
  if (transfer_use || !max_channel_.has_value() || (client == nullptr)) {
    return;
  }
  client->AcquireUse();
  transfer_use = true;
}

void ClientManager::DestroyIdleClients(const std::vector<std::string> &victims, const std::string &new_remote_engine) {
  for (const auto &victim : victims) {
    bool removed = false;
    const Status ret = DestroyIdleClient(victim, removed);
    if (!removed) {
      continue;
    }
    if (ret != SUCCESS) {
      // DestroyIdleClient 在 Finalize 之前已经把链路从池里摘掉。这里只记失败，名额是否空出来由调用方重新检查。
      HIXL_LOGW("Failed to evict the least recently used channel, remote_engine:%s, ret:%u", victim.c_str(),
                static_cast<uint32_t>(ret));
      continue;
    }
    HIXL_EVENT("[ClientManager] evicted the least recently used channel, remote_engine:%s, new_remote_engine:%s",
               victim.c_str(), new_remote_engine.c_str());
  }
}

Status ClientManager::DestroyIdleClient(const std::string &remote_engine, bool &removed) {
  removed = false;
  ClientPtr client = nullptr;
  auto client_mutex = GetClientMutex(remote_engine);
  std::lock_guard<std::mutex> client_lock(*client_mutex);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = clients_.find(remote_engine);
    if (it == clients_.end()) {
      return NOT_CONNECTED;
    }
    // 名单生成后，传输可能已经在 manager 锁内把占用加上。这里再看一次，避免拆掉这次传输。
    if (it->second->IsBusy()) {
      return SUCCESS;
    }
    client = it->second;
    clients_.erase(it);
    last_access_seq_.erase(remote_engine);
    HIXL_LOGI("Erase client end, remote_engine=%s", remote_engine.c_str());
  }
  removed = true;
  EraseReqIndexByClient(client);
  const Status ret = client->Finalize();
  HIXL_LOGI("Destroy client end, remote_engine=%s", remote_engine.c_str());
  return ret;
}

void ClientManager::EvaluateChannelAdmit(const std::string &remote_engine, bool *transfer_use, ChannelAdmit &admit) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = clients_.find(remote_engine);
  if (it != clients_.cend()) {
    if (transfer_use != nullptr) {
      ClaimTransferUseLocked(it->second, *transfer_use);
    }
    TouchClientLocked(remote_engine);
    admit.client = it->second;
    admit.already_connected = true;
    return;
  }
  if (!max_channel_.has_value()) {
    return;
  }
  const size_t max_channel = static_cast<size_t>(max_channel_.value());
  const size_t occupied = clients_.size() + creating_count_;
  if (occupied < max_channel) {
    ++creating_count_;
    admit.reserved = true;
    return;
  }
  admit.evict_count = occupied - max_channel + 1U;
  admit.victims = SelectLruIdleClientsLocked(admit.evict_count);
  if (admit.victims.size() < admit.evict_count) {
    admit.client_count = clients_.size();
    admit.creating_count = creating_count_;
    admit.idle_count = admit.victims.size();
    admit.pool_exhausted = true;
  }
}

Status ClientManager::ReportPoolExhausted(const std::string &remote_engine, const ChannelAdmit &admit) const {
  HIXL_REPORT_ERR_MSG("E19999",
                      "Failed to reclaim channels, channels are transferring or connecting, max_channel:%u, "
                      "client_count:%zu, creating_count:%zu, idle_channel_count:%zu, "
                      "required_channel_count:%zu, remote_engine:%s",
                      max_channel_.value(), admit.client_count, admit.creating_count, admit.idle_count,
                      admit.evict_count, remote_engine.c_str());
  HIXL_LOGE(RESOURCE_EXHAUSTED,
            "Failed to reclaim channels, channels are transferring or connecting, max_channel:%u, "
            "client_count:%zu, creating_count:%zu, idle_channel_count:%zu, "
            "required_channel_count:%zu, remote_engine:%s",
            max_channel_.value(), admit.client_count, admit.creating_count, admit.idle_count, admit.evict_count,
            remote_engine.c_str());
  return RESOURCE_EXHAUSTED;
}

Status ClientManager::FinishEvict(const std::string &remote_engine, const std::vector<std::string> &victims) {
  DestroyIdleClients(victims, remote_engine);
  size_t still_present = 0U;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto &victim : victims) {
      if (clients_.find(victim) != clients_.cend()) {
        ++still_present;
      }
    }
  }
  if (still_present >= victims.size()) {
    HIXL_REPORT_ERR_MSG("E19999",
                        "Failed to reclaim channels, evict did not free a channel, max_channel:%u, remote_engine:%s",
                        max_channel_.value(), remote_engine.c_str());
    HIXL_LOGE(RESOURCE_EXHAUSTED,
              "Failed to reclaim channels, evict did not free a channel, max_channel:%u, remote_engine:%s",
              max_channel_.value(), remote_engine.c_str());
    return RESOURCE_EXHAUSTED;
  }
  return SUCCESS;
}

Status ClientManager::ReserveChannelSlot(const std::string &remote_engine, std::unique_lock<std::mutex> &client_lock,
                                         ClientPtr &client_ptr, bool &reserved, bool *transfer_use) {
  // max_channel_ 只在 Initialize 写入。未配置链路池时 EvaluateChannelAdmit 直接返回，不增加占用。
  reserved = false;
  for (;;) {
    if (client_lock.owns_lock()) {
      client_lock.unlock();
    }
    auto client_mutex = GetClientMutex(remote_engine);
    client_lock = std::unique_lock<std::mutex>(*client_mutex);
    ChannelAdmit admit;
    EvaluateChannelAdmit(remote_engine, transfer_use, admit);
    if (admit.already_connected) {
      client_ptr = admit.client;
      return ALREADY_CONNECTED;
    }
    if (admit.pool_exhausted) {
      client_lock.unlock();
      return ReportPoolExhausted(remote_engine, admit);
    }
    if (admit.reserved || admit.victims.empty()) {
      reserved = admit.reserved;
      return SUCCESS;
    }
    // 淘汰时不持有正在建的这条链路的锁，避免两个建链线程互相等对方要拆的链路。
    client_lock.unlock();
    const Status evict_status = FinishEvict(remote_engine, admit.victims);
    if (evict_status != SUCCESS) {
      return evict_status;
    }
  }
}

Status ClientManager::GetOrCreateClient(const ClientConfig &config, const std::vector<MemHandleInfo> &mem_info_list,
                                        int32_t timeout_in_millis, ClientPtr &client_ptr, bool *transfer_use) {
  bool slot_reserved = false;
  HIXL_DISMISSABLE_GUARD(slot_guard, ([this, &slot_reserved]() {
                           if (slot_reserved) {
                             ReleaseChannelSlot();
                             slot_reserved = false;
                           }
                         }));
  std::unique_lock<std::mutex> client_lock;
  const Status admit_status =
      ReserveChannelSlot(config.remote_engine, client_lock, client_ptr, slot_reserved, transfer_use);
  if (admit_status == ALREADY_CONNECTED) {
    HIXL_DISMISS_GUARD(slot_guard);
    return ALREADY_CONNECTED;
  }
  HIXL_CHK_STATUS_RET(admit_status, "Failed to make room in the channel pool, remote_engine:%s",
                      config.remote_engine.c_str());

  ClientPtr new_client = nullptr;
  HIXL_CHK_STATUS_RET(CreateClient(config, new_client), "Failed to create HixlClient, remote_engine:%s",
                      config.remote_engine.c_str());
  HIXL_CHECK_NOTNULL(new_client, "Created client is null, remote_engine:%s", config.remote_engine.c_str());

  HIXL_DISMISSABLE_GUARD(fail_guard, ([new_client]() { (void)new_client->Finalize(); }));
  HIXL_CHK_STATUS_RET(new_client->RegisterMem(mem_info_list), "Failed to register memory, remote_engine:%s",
                      config.remote_engine.c_str());
  HIXL_CHK_STATUS_RET(new_client->Connect(timeout_in_millis),
                      "Failed to connect client, remote_engine:%s, timeout:%d ms", config.remote_engine.c_str(),
                      timeout_in_millis);
  HIXL_DISMISS_GUARD(fail_guard);

  {
    std::lock_guard<std::mutex> lock(mutex_);
    clients_.emplace(config.remote_engine, new_client);
    if (transfer_use != nullptr) {
      ClaimTransferUseLocked(new_client, *transfer_use);
    }
    TouchClientLocked(config.remote_engine);
    // 建链前占的名额在这里换成池里的一条链路，clients_.size() + creating_count_ 不超过 max_channel。
    if (slot_reserved) {
      if (creating_count_ > 0U) {
        --creating_count_;
      }
      slot_reserved = false;
    }
  }
  client_ptr = new_client;
  HIXL_DISMISS_GUARD(slot_guard);
  return SUCCESS;
}

ClientPtr ClientManager::GetClient(const std::string &remote_engine) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto &it = clients_.find(remote_engine);
  if (it != clients_.cend()) {
    TouchClientLocked(remote_engine);
    return it->second;
  }
  return nullptr;
}

ClientPtr ClientManager::GetClientForUse(const std::string &remote_engine, bool &transfer_use) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = clients_.find(remote_engine);
  if (it == clients_.cend()) {
    return nullptr;
  }
  ClaimTransferUseLocked(it->second, transfer_use);
  TouchClientLocked(remote_engine);
  return it->second;
}

ClientPtr ClientManager::GetClientByReq(const TransferReq &req) {
  std::lock_guard<std::mutex> lock(req_index_mutex_);
  auto it = req_to_client_.find(req);
  if (it == req_to_client_.end()) {
    return nullptr;
  }
  ClientPtr client = it->second.client.lock();
  if (client == nullptr) {
    ordered_reqs_.erase(it->second.order_it);
    req_to_client_.erase(it);
  }
  return client;
}

void ClientManager::EraseTransferReqLocked(const TransferReq &req) {
  auto it = req_to_client_.find(req);
  if (it == req_to_client_.end()) {
    return;
  }
  ordered_reqs_.erase(it->second.order_it);
  req_to_client_.erase(it);
}

void ClientManager::RegisterTransferReq(const TransferReq &req, const ClientPtr &client, const void *user_data) {
  if (req == nullptr || client == nullptr) {
    return;
  }
  std::lock_guard<std::mutex> lock(req_index_mutex_);
  EraseTransferReqLocked(req);
  ordered_reqs_.push_back(req);
  req_to_client_[req] = ReqOwner{client, user_data, std::prev(ordered_reqs_.end())};
}

void ClientManager::EraseTransferReq(const TransferReq &req) {
  std::lock_guard<std::mutex> lock(req_index_mutex_);
  EraseTransferReqLocked(req);
}

std::vector<TransferReqInfo> ClientManager::GetOrderedReqs(size_t max_count) {
  std::vector<TransferReqInfo> reqs;
  std::lock_guard<std::mutex> lock(req_index_mutex_);
  for (auto it = ordered_reqs_.begin(); it != ordered_reqs_.end();) {
    const TransferReq req = *it;
    auto owner_it = req_to_client_.find(req);
    if (owner_it == req_to_client_.end()) {
      it = ordered_reqs_.erase(it);
      continue;
    }

    ClientPtr client = owner_it->second.client.lock();
    if (client == nullptr) {
      req_to_client_.erase(owner_it);
      it = ordered_reqs_.erase(it);
      continue;
    }

    reqs.emplace_back(TransferReqInfo{req, client, owner_it->second.user_data});
    ++it;
    if (max_count > 0 && reqs.size() >= max_count) {
      break;
    }
  }
  return reqs;
}

void ClientManager::EraseReqIndexByClient(const ClientPtr &client) {
  std::lock_guard<std::mutex> lock(req_index_mutex_);
  for (auto it = req_to_client_.begin(); it != req_to_client_.end();) {
    ClientPtr owner = it->second.client.lock();
    if (owner == nullptr || owner == client) {
      ordered_reqs_.erase(it->second.order_it);
      it = req_to_client_.erase(it);
    } else {
      ++it;
    }
  }
}

Status ClientManager::DestroyClient(const std::string &remote_engine) {
  auto ret = NOT_CONNECTED;
  ClientPtr client = nullptr;
  auto client_mutex = GetClientMutex(remote_engine);
  std::lock_guard<std::mutex> client_lock(*client_mutex);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto &it = clients_.find(remote_engine);
    if (it != clients_.end()) {
      client = it->second;
      clients_.erase(it);
      last_access_seq_.erase(remote_engine);
      HIXL_LOGI("Erase client end, remote_engine=%s", remote_engine.c_str());
    }
  }

  if (client != nullptr) {
    EraseReqIndexByClient(client);
    ret = client->Finalize();
    HIXL_LOGI("Destroy client end, remote_engine=%s", remote_engine.c_str());
  }
  client_mutex.reset();
  DestroyClientMutex(remote_engine);
  return ret;
}

std::shared_ptr<std::mutex> ClientManager::GetClientMutex(const std::string &remote_engine) {
  std::lock_guard<std::mutex> lock(client_mutexes_mutex_);
  auto it = client_mutexes_.find(remote_engine);
  if (it != client_mutexes_.end()) {
    return it->second;
  }

  auto client_mutex = std::make_shared<std::mutex>();
  client_mutexes_.emplace(remote_engine, client_mutex);
  return client_mutex;
}

void ClientManager::DestroyClientMutex(const std::string &remote_engine) {
  std::lock_guard<std::mutex> lock(client_mutexes_mutex_);
  auto it = client_mutexes_.find(remote_engine);
  if (it != client_mutexes_.end()) {
    client_mutexes_.erase(it);
  }
}

bool ClientManager::IsEmpty() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return clients_.empty();
}

Status ClientManager::DeregisterMem(MemHandle mem_handle) {
  std::map<std::string, ClientPtr> clients_copy;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    clients_copy = clients_;
  }
  HIXL_LOGI("[ClientManager] deregister mem for %zu clients, mem_handle:%p", clients_copy.size(), mem_handle);
  for (const auto &[name, client] : clients_copy) {
    HIXL_CHK_STATUS_RET(client->DeregisterMem(mem_handle),
                        "Failed to deregister memory for client, remote_engine:%s, mem_handle:%p", name.c_str(),
                        mem_handle);
  }
  return SUCCESS;
}

void ClientManager::SendHeartbeat() {
  std::map<std::string, ClientPtr> clients_copy;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    clients_copy = clients_;
  }

  for (const auto &[name, client] : clients_copy) {
    Status ret = client->CheckAlive();
    if (ret != SUCCESS) {
      HIXL_LOGW("CheckAlive failed for remote_engine:%s, ret:%u", name.c_str(), static_cast<uint32_t>(ret));
      (void)DestroyClient(name);
    }
  }
}

Status ClientManager::Finalize() {
  stop_signal_.store(true);
  cv_.notify_all();
  if (heartbeat_sender_.joinable()) {
    heartbeat_sender_.join();
  }
  std::map<std::string, ClientPtr> clients;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    finalized_ = true;
    clients = std::move(clients_);
    last_access_seq_.clear();
  }

  Status ret = SUCCESS;
  for (auto &it : clients) {
    EraseReqIndexByClient(it.second);
    Status client_ret = it.second->Finalize();
    if (client_ret != SUCCESS) {
      HIXL_LOGE(client_ret, "Failed to finalize client, remote_engine:%s", it.first.c_str());
      if (ret == SUCCESS) {
        ret = client_ret;
      }
    }
  }
  clients_.clear();
  {
    std::lock_guard<std::mutex> req_lock(req_index_mutex_);
    ordered_reqs_.clear();
    req_to_client_.clear();
  }
  {
    std::lock_guard<std::mutex> client_mutexes_lock(client_mutexes_mutex_);
    client_mutexes_.clear();
  }
  return ret;
}
}  // namespace hixl
