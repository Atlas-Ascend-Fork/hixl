/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "tls_config.h"

#include "common/hixl_checker.h"

namespace hixl {
namespace {
constexpr const char *kCertFile = "tls_config.cert_file";
constexpr const char *kKeyFile = "tls_config.key_file";
constexpr const char *kCaFile = "tls_config.ca_file";
constexpr const char *kCiphers = "tls_config.ciphers";
constexpr const char *kVerifyPeer = "tls_config.verify_peer";
constexpr const char *kEnableCertRefresh = "tls_config.enable_cert_refresh";

Status ParseStringField(const nlohmann::json &json, const char *key, std::string &out, bool &configured) {
  const auto it = json.find(key);
  if (it == json.end()) {
    return SUCCESS;
  }
  configured = true;
  HIXL_CHK_BOOL_RET_STATUS(it->is_string(), PARAM_INVALID, "[TlsConfig] %s must be a string", key);
  out = it->get<std::string>();
  return SUCCESS;
}

Status ParseBoolField(const nlohmann::json &json, const char *key, bool &out, bool &configured) {
  const auto it = json.find(key);
  if (it == json.end()) {
    return SUCCESS;
  }
  configured = true;
  if (it->is_boolean()) {
    out = it->get<bool>();
    return SUCCESS;
  }
  HIXL_CHK_BOOL_RET_STATUS(it->is_string(), PARAM_INVALID, "[TlsConfig] %s must be a bool", key);
  const std::string value = it->get<std::string>();
  if (value == "true" || value == "1") {
    out = true;
    return SUCCESS;
  }
  HIXL_CHK_BOOL_RET_STATUS(value == "false" || value == "0", PARAM_INVALID, "[TlsConfig] %s has invalid bool string:%s",
                           key, value.c_str());
  out = false;
  return SUCCESS;
}
}  // namespace

Status ValidateTlsConfig(const TlsConfig &config) {
  if (!config.configured) {
    return SUCCESS;
  }
  const bool has_cert = !config.cert_file.empty();
  const bool has_key = !config.key_file.empty();
  HIXL_CHK_BOOL_RET_STATUS(has_cert && has_key, PARAM_INVALID,
                           "[TlsConfig] cert_file and key_file must be provided as a pair, has_cert:%s, has_key:%s",
                           has_cert ? "true" : "false", has_key ? "true" : "false");
  if (config.verify_peer) {
    HIXL_CHK_BOOL_RET_STATUS(!config.ca_file.empty(), PARAM_INVALID,
                             "[TlsConfig] ca_file is required when verify_peer is true");
  }
  return SUCCESS;
}

Status ParseTlsConfig(const nlohmann::json &json, TlsConfig &config) {
  TlsConfig parsed;
  HIXL_CHK_STATUS_RET(ParseStringField(json, kCertFile, parsed.cert_file, parsed.configured),
                      "[TlsConfig] Failed to parse cert_file");
  HIXL_CHK_STATUS_RET(ParseStringField(json, kKeyFile, parsed.key_file, parsed.configured),
                      "[TlsConfig] Failed to parse key_file");
  HIXL_CHK_STATUS_RET(ParseStringField(json, kCaFile, parsed.ca_file, parsed.configured),
                      "[TlsConfig] Failed to parse ca_file");
  HIXL_CHK_STATUS_RET(ParseStringField(json, kCiphers, parsed.ciphers, parsed.configured),
                      "[TlsConfig] Failed to parse ciphers");
  HIXL_CHK_STATUS_RET(ParseBoolField(json, kVerifyPeer, parsed.verify_peer, parsed.configured),
                      "[TlsConfig] Failed to parse verify_peer");
  HIXL_CHK_STATUS_RET(ParseBoolField(json, kEnableCertRefresh, parsed.enable_cert_refresh, parsed.configured),
                      "[TlsConfig] Failed to parse enable_cert_refresh");
  HIXL_CHK_STATUS_RET(ValidateTlsConfig(parsed), "[TlsConfig] Invalid tls_config");
  HIXL_LOGI("[TlsConfig] Parsed configuration, enabled:%s, verify_peer:%s, cert_refresh:%s, custom_ciphers:%s",
            parsed.Enabled() ? "true" : "false", parsed.verify_peer ? "true" : "false",
            parsed.enable_cert_refresh ? "true" : "false", parsed.ciphers.empty() ? "false" : "true");
  config = std::move(parsed);
  return SUCCESS;
}
}  // namespace hixl
