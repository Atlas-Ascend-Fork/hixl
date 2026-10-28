/**
 * Copyright (c) 2025-2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_MSG_HANDLER_PLUGIN_H_
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_MSG_HANDLER_PLUGIN_H_

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "acl/acl.h"
#include "common/ctrl_msg_plugin.h"
#include "llm_datadist/llm_error_codes.h"
#include "common/llm_thread_pool.h"

namespace llm {
constexpr int32_t kServerReadTimeoutMs = 60000;

using ConnectedProcess = std::function<void(const hixl::CtrlMsgPluginPtr &connection, bool &keep_connection)>;
class MsgHandlerPlugin {
 public:
  MsgHandlerPlugin() = default;
  ~MsgHandlerPlugin();
  ge::Status Initialize();
  void Finalize();
  ge::Status StartDaemon(const std::string &ip, uint32_t listen_port);
  void RegisterConnectedProcess(ConnectedProcess proc);
  static ge::Status SendMsg(const hixl::CtrlMsgPluginPtr &connection, int32_t msg_type, const std::string &msg_str);
  static ge::Status RecvMsg(const hixl::CtrlMsgPluginPtr &connection, int32_t &msg_type, std::vector<char> &msg,
                            int32_t timeout_ms);

 private:
  void ListenClose();
  ge::Status DoConnectedProcess(const hixl::CtrlMsgPluginPtr &connection);
  ge::Status DoAccept();

  hixl::CtrlMsgPluginPtr listener_connection_;
  std::atomic<bool> listener_running_{false};
  std::thread listener_;
  std::unique_ptr<LLMThreadPool> thread_pool_ = nullptr;
  ConnectedProcess connected_process_;
  aclrtContext aclrt_context_ = nullptr;
};
}  // namespace llm
#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_MSG_HANDLER_PLUGIN_H_
