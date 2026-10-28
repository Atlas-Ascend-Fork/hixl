/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_COMMON_TLS_TLS_CERT_WATCHER_H_
#define CANN_HIXL_SRC_HIXL_COMMON_TLS_TLS_CERT_WATCHER_H_

#include <atomic>
#include <memory>
#include <functional>
#include <string>
#include <thread>

#include "hixl/hixl_types.h"
#include "common/tls/tls_config.h"

namespace hixl {
class TlsContext;

class TlsCertWatcher {
 public:
  using ContextUpdate = std::function<void(std::shared_ptr<TlsContext>)>;
  static Status Create(const TlsConfig &config, ContextUpdate update, std::unique_ptr<TlsCertWatcher> &watcher);
  ~TlsCertWatcher();
  TlsCertWatcher(const TlsCertWatcher &) = delete;
  TlsCertWatcher &operator=(const TlsCertWatcher &) = delete;

  Status Start();
  void Stop();

 private:
  TlsCertWatcher() = default;
  void WatchLoop();
  Status ReloadCertKey();
  Status ReloadCa();
  Status InitWatchFds();
  Status AddDirWatch(const std::string &dir, int &watch_desc);
  void CloseWatchFds();

  TlsConfig config_;
  ContextUpdate update_;
  std::atomic<bool> running_{false};
  std::thread watch_thread_;
  int inotify_fd_ = -1;
  int wake_fd_[2] = {-1, -1};
  int cert_wd_ = -1;
  int key_wd_ = -1;
  int ca_wd_ = -1;
};
}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_COMMON_TLS_TLS_CERT_WATCHER_H_
