/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "tls_manager.h"

#include <openssl/ssl.h>

#include "common/hixl_checker.h"
#include "common/tls/tls_cert_watcher.h"

namespace hixl {
namespace {
bool SameConfig(const TlsConfig &lhs, const TlsConfig &rhs) {
  return lhs.configured == rhs.configured && lhs.cert_file == rhs.cert_file && lhs.key_file == rhs.key_file &&
         lhs.ca_file == rhs.ca_file && lhs.ciphers == rhs.ciphers && lhs.verify_peer == rhs.verify_peer &&
         lhs.enable_cert_refresh == rhs.enable_cert_refresh;
}
}  // namespace

TlsManager &TlsManager::GetInstance() {
  static TlsManager instance;
  return instance;
}

TlsManager::~TlsManager() {
  if (watcher_ != nullptr) {
    watcher_->Stop();
  }
}

Status TlsManager::EnableTlsLocked(const TlsConfig &config) {
  const int ret = OPENSSL_init_ssl(0, nullptr);
  HIXL_CHK_BOOL_RET_STATUS(ret == 1, FAILED, "Call api:OPENSSL_init_ssl failed, ret:%d", ret);
  std::shared_ptr<TlsContext> ctx;
  HIXL_CHK_STATUS_RET(TlsContext::Create(config, ctx), "[TlsManager] Failed to create TLS context");
  std::unique_ptr<TlsCertWatcher> watcher;
  if (config.enable_cert_refresh) {
    HIXL_CHK_STATUS_RET(
        TlsCertWatcher::Create(
            config, [this](std::shared_ptr<TlsContext> updated) { SetContext(std::move(updated)); }, watcher),
        "[TlsManager] Failed to create TLS file watcher");
    HIXL_CHK_STATUS_RET(watcher->Start(), "[TlsManager] Failed to start TLS file watcher");
  }
  ctx_ = std::move(ctx);
  watcher_ = std::move(watcher);
  return SUCCESS;
}

Status TlsManager::Initialize(const TlsConfig &config) {
  std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
  std::unique_lock<std::shared_mutex> lock(mutex_);
  HIXL_CHK_STATUS_RET(ValidateTlsConfig(config), "[TlsManager] Invalid TLS configuration");
  if (users_ != 0U) {
    HIXL_CHK_BOOL_RET_STATUS(SameConfig(config_, config), PARAM_INVALID,
                             "[TlsManager] TLS configuration conflicts with active process configuration");
    ++users_;
    return SUCCESS;
  }
  if (config.Enabled()) {
    HIXL_CHK_STATUS_RET(EnableTlsLocked(config), "[TlsManager] Failed to initialize TLS resources");
  }
  config_ = config;
  users_ = 1U;
  return SUCCESS;
}

void TlsManager::Finalize() {
  std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
  std::unique_ptr<TlsCertWatcher> watcher;
  {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (users_ == 0U) {
      return;
    }
    --users_;
    if (users_ != 0U) {
      return;
    }
    watcher = std::move(watcher_);
    ctx_.reset();
    config_ = TlsConfig{};
  }
  // Do not hold mutex_ while joining a watcher that may be publishing a context.
  if (watcher != nullptr) {
    watcher->Stop();
  }
}

Status TlsManager::GetTlsEnabled(bool &enabled) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  enabled = false;
  HIXL_CHK_BOOL_RET_STATUS(users_ != 0U, FAILED, "[TlsManager] TLS manager must be initialized by its owner first");
  enabled = config_.Enabled();
  return SUCCESS;
}

bool TlsManager::IsInitialized() const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  return users_ != 0U;
}

std::shared_ptr<TlsContext> TlsManager::GetContext() const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  return ctx_;
}

TlsConfig TlsManager::GetConfig() const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  return config_;
}

void TlsManager::SetContext(std::shared_ptr<TlsContext> ctx) {
  std::unique_lock<std::shared_mutex> lock(mutex_);
  if (users_ == 0U || ctx == nullptr) {
    return;
  }
  ctx_ = std::move(ctx);
  HIXL_LOGI("[TlsManager] Replaced shared TLS context");
}
}  // namespace hixl
