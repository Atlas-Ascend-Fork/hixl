/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#ifndef CANN_HIXL_SRC_HIXL_PROXY_IBVERBS_PROXY_H_
#define CANN_HIXL_SRC_HIXL_PROXY_IBVERBS_PROXY_H_

#include <string>
#include <vector>
#include "hixl/hixl_types.h"

namespace hixl {

/**
 * @brief Proxy for libibverbs. Loads the library with dlopen and wraps the verbs
 *        APIs used to enumerate Host RoCE NICs and read IPv4-mapped GIDs.
 */
class IbverbsProxy {
 public:
  IbverbsProxy() = delete;

  /**
   * @brief List ibverbs device names on this host.
   * @param [out] nic_names Device names such as rocep185s0f0
   * @return SUCCESS when at least one name is returned, FAILED otherwise
   */
  static Status ListNicNames(std::vector<std::string> &nic_names);

  /**
   * @brief Read the first IPv4-mapped GID of a named ibverbs device.
   * @param [in] nic_name ibverbs device name
   * @param [out] ipv4 Dotted IPv4 address
   * @return SUCCESS when an IPv4 address is found, FAILED otherwise
   */
  static Status GetNicIpv4ByName(const std::string &nic_name, std::string &ipv4);
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_PROXY_IBVERBS_PROXY_H_
