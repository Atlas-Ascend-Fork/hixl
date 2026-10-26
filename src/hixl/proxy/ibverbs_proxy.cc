/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ibverbs_proxy.h"

#include <algorithm>
#include <cstdint>
#include <dlfcn.h>
#include <mutex>
#include <string>
#include <vector>

#include "common/hixl_log.h"
#include "common/scope_guard.h"
#include "securec.h"

namespace hixl {
namespace {

constexpr const char *kLibIbverbsSo1 = "libibverbs.so.1";
constexpr const char *kLibIbverbsSo = "libibverbs.so";
constexpr size_t kMaxGidQueryCount = 64U;
constexpr uint8_t kRocePortNum = 1U;
constexpr size_t kGidRawLen = 16U;

// Opaque complete types for libibverbs handles. Only pointers are passed across the
// dlopen boundary; the real layout stays inside libibverbs.
struct IbvDevice {
  IbvDevice() = delete;
  IbvDevice(const IbvDevice &) = delete;
  IbvDevice &operator=(const IbvDevice &) = delete;
};

struct IbvContext {
  IbvContext() = delete;
  IbvContext(const IbvContext &) = delete;
  IbvContext &operator=(const IbvContext &) = delete;
};

struct IbvGid {
  uint8_t raw[kGidRawLen];
};

using IbvGetDeviceListFn = IbvDevice **(*)(int *);
using IbvFreeDeviceListFn = void (*)(IbvDevice **);
using IbvGetDeviceNameFn = const char *(*)(IbvDevice *);
using IbvOpenDeviceFn = IbvContext *(*)(IbvDevice *);
using IbvCloseDeviceFn = int (*)(IbvContext *);
using IbvQueryGidFn = int (*)(IbvContext *, uint8_t, int, IbvGid *);

struct LibIbverbsLoader {
  void *handle = nullptr;
  IbvGetDeviceListFn ibv_get_device_list = nullptr;
  IbvFreeDeviceListFn ibv_free_device_list = nullptr;
  IbvGetDeviceNameFn ibv_get_device_name = nullptr;
  IbvOpenDeviceFn ibv_open_device = nullptr;
  IbvCloseDeviceFn ibv_close_device = nullptr;
  IbvQueryGidFn ibv_query_gid = nullptr;
  std::mutex mu;

  ~LibIbverbsLoader() {
    std::lock_guard<std::mutex> lock(mu);
    if (handle != nullptr) {
      HIXL_LOGI("[IbverbsProxy] LibIbverbsLoader destruct, dlclose libibverbs");
      (void)dlclose(handle);
      handle = nullptr;
      ibv_get_device_list = nullptr;
      ibv_free_device_list = nullptr;
      ibv_get_device_name = nullptr;
      ibv_open_device = nullptr;
      ibv_close_device = nullptr;
      ibv_query_gid = nullptr;
    }
  }
};

LibIbverbsLoader &LibIbverbs() {
  static LibIbverbsLoader inst;
  return inst;
}

void *DlOpenIbverbs() {
  void *handle = dlopen(kLibIbverbsSo1, RTLD_NOW);
  if (handle != nullptr) {
    return handle;
  }
  return dlopen(kLibIbverbsSo, RTLD_NOW);
}

bool BindRequiredSymbols(void *handle, LibIbverbsLoader &ldr) {
  ldr.ibv_get_device_list = reinterpret_cast<IbvGetDeviceListFn>(dlsym(handle, "ibv_get_device_list"));
  ldr.ibv_free_device_list = reinterpret_cast<IbvFreeDeviceListFn>(dlsym(handle, "ibv_free_device_list"));
  ldr.ibv_get_device_name = reinterpret_cast<IbvGetDeviceNameFn>(dlsym(handle, "ibv_get_device_name"));
  ldr.ibv_open_device = reinterpret_cast<IbvOpenDeviceFn>(dlsym(handle, "ibv_open_device"));
  ldr.ibv_close_device = reinterpret_cast<IbvCloseDeviceFn>(dlsym(handle, "ibv_close_device"));
  ldr.ibv_query_gid = reinterpret_cast<IbvQueryGidFn>(dlsym(handle, "ibv_query_gid"));
  return ldr.ibv_get_device_list != nullptr && ldr.ibv_free_device_list != nullptr &&
         ldr.ibv_get_device_name != nullptr && ldr.ibv_open_device != nullptr && ldr.ibv_close_device != nullptr &&
         ldr.ibv_query_gid != nullptr;
}

Status EnsureLibIbverbsLoaded() {
  LibIbverbsLoader &ldr = LibIbverbs();
  std::lock_guard<std::mutex> lock(ldr.mu);
  if (ldr.handle != nullptr) {
    return SUCCESS;
  }
  void *handle = DlOpenIbverbs();
  if (handle == nullptr) {
    const char *err = dlerror();
    HIXL_EVENT("[IbverbsProxy] Call api:dlopen failed, lib:%s, msg:%s", kLibIbverbsSo1,
               err == nullptr ? "unknown error" : err);
    return FAILED;
  }
  HIXL_DISMISSABLE_GUARD(handle_guard, ([handle]() { (void)dlclose(handle); }));
  if (!BindRequiredSymbols(handle, ldr)) {
    HIXL_EVENT("[IbverbsProxy] Call api:dlsym failed, missing required ibverbs symbols");
    return FAILED;
  }
  ldr.handle = handle;
  HIXL_DISMISS_GUARD(handle_guard);
  return SUCCESS;
}

bool IsIpv4MappedGid(const IbvGid &gid) {
  for (size_t i = 0; i < 10U; ++i) {
    if (gid.raw[i] != 0U) {
      return false;
    }
  }
  return gid.raw[10] == 0xffU && gid.raw[11] == 0xffU;
}

bool IsAllZeroGid(const IbvGid &gid) {
  return std::all_of(gid.raw, gid.raw + kGidRawLen, [](uint8_t byte) { return byte == 0U; });
}

std::string FormatIpv4FromGid(const IbvGid &gid) {
  char ip[16] = {0};
  (void)sprintf_s(ip, sizeof(ip), "%u.%u.%u.%u", gid.raw[12], gid.raw[13], gid.raw[14], gid.raw[15]);
  return ip;
}

IbvDevice *FindDeviceByName(LibIbverbsLoader &ldr, IbvDevice **list, int num, const std::string &nic_name) {
  for (int i = 0; i < num; ++i) {
    const char *name = ldr.ibv_get_device_name(list[i]);
    if (name != nullptr && nic_name == name) {
      return list[i];
    }
  }
  return nullptr;
}

Status QueryIpv4FromDevice(LibIbverbsLoader &ldr, IbvDevice *device, std::string &ipv4) {
  IbvContext *ctx = ldr.ibv_open_device(device);
  if (ctx == nullptr) {
    return FAILED;
  }
  Status ret = FAILED;
  for (size_t index = 0; index < kMaxGidQueryCount; ++index) {
    IbvGid gid{};
    if (ldr.ibv_query_gid(ctx, kRocePortNum, static_cast<int>(index), &gid) != 0) {
      break;
    }
    if (IsAllZeroGid(gid) || !IsIpv4MappedGid(gid)) {
      continue;
    }
    ipv4 = FormatIpv4FromGid(gid);
    ret = SUCCESS;
    break;
  }
  (void)ldr.ibv_close_device(ctx);
  return ret;
}

}  // namespace

Status IbverbsProxy::ListNicNames(std::vector<std::string> &nic_names) {
  nic_names.clear();
  if (EnsureLibIbverbsLoaded() != SUCCESS) {
    return FAILED;
  }
  LibIbverbsLoader &ldr = LibIbverbs();
  std::lock_guard<std::mutex> lock(ldr.mu);
  int num = 0;
  IbvDevice **list = ldr.ibv_get_device_list(&num);
  if (list == nullptr || num <= 0) {
    if (list != nullptr) {
      ldr.ibv_free_device_list(list);
    }
    return FAILED;
  }
  for (int i = 0; i < num; ++i) {
    const char *name = ldr.ibv_get_device_name(list[i]);
    if (name != nullptr && name[0] != '\0') {
      nic_names.emplace_back(name);
    }
  }
  ldr.ibv_free_device_list(list);
  return nic_names.empty() ? FAILED : SUCCESS;
}

Status IbverbsProxy::GetNicIpv4ByName(const std::string &nic_name, std::string &ipv4) {
  if (EnsureLibIbverbsLoaded() != SUCCESS) {
    return FAILED;
  }
  LibIbverbsLoader &ldr = LibIbverbs();
  std::lock_guard<std::mutex> lock(ldr.mu);
  int num = 0;
  IbvDevice **list = ldr.ibv_get_device_list(&num);
  if (list == nullptr || num <= 0) {
    if (list != nullptr) {
      ldr.ibv_free_device_list(list);
    }
    return FAILED;
  }
  IbvDevice *matched = FindDeviceByName(ldr, list, num, nic_name);
  Status ret = FAILED;
  if (matched != nullptr) {
    ret = QueryIpv4FromDevice(ldr, matched, ipv4);
  }
  ldr.ibv_free_device_list(list);
  return ret;
}

}  // namespace hixl
