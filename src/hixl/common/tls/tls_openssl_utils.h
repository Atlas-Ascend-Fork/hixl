/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_COMMON_TLS_TLS_OPENSSL_UTILS_H_
#define CANN_HIXL_SRC_HIXL_COMMON_TLS_TLS_OPENSSL_UTILS_H_

#include <cstddef>
#include <string>

#include <openssl/err.h>

namespace hixl {

inline std::string OpensslErrorString() {
  constexpr size_t kMaxErrorStringLength = 512U;
  std::string errors;
  for (unsigned long err = ERR_get_error(); err != 0UL; err = ERR_get_error()) {
    if (errors.size() >= kMaxErrorStringLength) {
      continue;
    }
    char buf[256];
    ERR_error_string_n(err, buf, sizeof(buf));
    if (!errors.empty()) {
      errors += "; ";
    }
    errors += buf;
    if (errors.size() > kMaxErrorStringLength) {
      errors.resize(kMaxErrorStringLength);
    }
  }
  return errors.empty() ? "none" : errors;
}

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_COMMON_TLS_TLS_OPENSSL_UTILS_H_
