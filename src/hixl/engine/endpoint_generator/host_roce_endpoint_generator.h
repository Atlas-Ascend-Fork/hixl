/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_ENGINE_HOST_ROCE_ENDPOINT_GENERATOR_H_
#define CANN_HIXL_SRC_HIXL_ENGINE_HOST_ROCE_ENDPOINT_GENERATOR_H_

#include <vector>

#include "common/hixl_inner_types.h"
#include "engine/hixl_options.h"
#include "hixl/hixl_types.h"

namespace hixl {

/**
 * @brief Generator for Host RoCE endpoints on A5.
 *        Encapsulates nic-topo validation, protocol_desc dispatch, and endpoint construction.
 */
class HostRoceEndpointGenerator {
 public:
  HostRoceEndpointGenerator() = delete;

  /**
   * @brief Generate Host RoCE endpoints and append them to endpoint_list.
   *        Returns SUCCESS without appending when protocol_desc does not contain roce:host.
   * @param [in] options Parsed HixlOptions
   * @param [in,out] endpoint_list Endpoint list to append to
   * @return SUCCESS when Host RoCE is not requested or endpoints are appended, PARAM_INVALID on failure
   */
  static Status GenerateHostRoceEndpoints(const HixlOptions &options, std::vector<EndpointConfig> &endpoint_list);
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_ENGINE_HOST_ROCE_ENDPOINT_GENERATOR_H_
