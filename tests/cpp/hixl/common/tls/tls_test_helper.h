/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_TESTS_CPP_HIXL_COMMON_TLS_TLS_TEST_HELPER_H_
#define CANN_HIXL_TESTS_CPP_HIXL_COMMON_TLS_TLS_TEST_HELPER_H_

#include <cstdlib>
#include <string>
#include <unistd.h>
#include <vector>
#include <sys/stat.h>

#include "common/tls/tls_config.h"

namespace hixl {
namespace test {

inline std::string MakeTempDir() {
  char tmpl[] = "/tmp/hixl_tls_XXXXXX";
  char *dir = mkdtemp(tmpl);
  if (dir == nullptr) {
    return "";
  }
  return dir;
}

inline bool RunOpenssl(const std::string &cmd) {
  return std::system(cmd.c_str()) == 0;
}

inline bool GenerateSelfSignedCert(const std::string &dir, const std::string &cert_name = "cert.pem",
                                   const std::string &key_name = "key.pem") {
  const std::string cert = dir + "/" + cert_name;
  const std::string key = dir + "/" + key_name;
  const std::string cmd = "openssl req -x509 -newkey rsa:2048 -keyout " + key + " -out " + cert +
                          " -days 1 -nodes -subj /CN=localhost >/dev/null 2>&1";
  return RunOpenssl(cmd);
}

inline TlsConfig MakeTlsConfig(const std::string &dir, bool verify_peer = false, bool refresh = false) {
  TlsConfig cfg;
  cfg.configured = true;
  cfg.cert_file = dir + "/cert.pem";
  cfg.key_file = dir + "/key.pem";
  cfg.ca_file = verify_peer ? dir + "/cert.pem" : "";
  cfg.verify_peer = verify_peer;
  cfg.enable_cert_refresh = refresh;
  return cfg;
}

inline void RemovePath(const std::string &path) {
  (void)unlink(path.c_str());
  (void)rmdir(path.c_str());
}

}  // namespace test
}  // namespace hixl

#endif  // CANN_HIXL_TESTS_CPP_HIXL_COMMON_TLS_TLS_TEST_HELPER_H_
