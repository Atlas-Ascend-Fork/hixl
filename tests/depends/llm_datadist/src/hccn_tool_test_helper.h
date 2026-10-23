/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HIXL_TESTS_DEPENDS_LLM_DATADIST_HCCN_TOOL_TEST_HELPER_H_
#define HIXL_TESTS_DEPENDS_LLM_DATADIST_HCCN_TOOL_TEST_HELPER_H_

#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace llm {
// Controls PATH only; the fixture's system-call mock must also hide the fixed driver tool path.
class HccnToolPathGuard {
 public:
  explicit HccnToolPathGuard(const std::string &test_name) : temp_dir_template_("/tmp/hixl_" + test_name + "_XXXXXX") {
    const char *path = getenv("PATH");
    had_path_ = path != nullptr;
    if (had_path_) {
      old_path_ = path;
    }
  }

  ~HccnToolPathGuard() {
    if (path_changed_) {
      if (had_path_) {
        (void)setenv("PATH", old_path_.c_str(), 1);
      } else {
        (void)unsetenv("PATH");
      }
    }
    if (!temp_dir_.empty()) {
      const std::string tool_path = temp_dir_ + "/hccn_tool";
      (void)unlink(tool_path.c_str());
      (void)rmdir(temp_dir_.c_str());
    }
  }

  HccnToolPathGuard(const HccnToolPathGuard &) = delete;
  HccnToolPathGuard &operator=(const HccnToolPathGuard &) = delete;

  bool UseEmptyPath() {
    if (!CreateTempDir()) {
      return false;
    }
    const std::string tool_path = temp_dir_ + "/hccn_tool";
    if (unlink(tool_path.c_str()) != 0 && errno != ENOENT) {
      return false;
    }
    return SetPath();
  }

  bool UseFakeHccnTool(const std::string &output) {
    return UseScript(
        "#!/bin/sh\n"
        "if [ \"$3\" = \"-link\" ]; then\n"
        "  printf 'link status: UP\\n'\n"
        "else\n"
        "  printf '%s\\n' '" +
        output +
        "'\n"
        "fi\n");
  }

  bool UseScript(const std::string &script) {
    if (!CreateTempDir()) {
      return false;
    }
    const std::string tool_path = temp_dir_ + "/hccn_tool";
    std::ofstream tool_file(tool_path);
    if (!tool_file.is_open()) {
      return false;
    }
    tool_file << script;
    tool_file.close();
    if (!tool_file || chmod(tool_path.c_str(), S_IRWXU) != 0) {
      return false;
    }
    return SetPath();
  }

 private:
  bool CreateTempDir() {
    if (!temp_dir_.empty()) {
      return true;
    }
    char *dir = mkdtemp(temp_dir_template_.data());
    if (dir == nullptr) {
      return false;
    }
    temp_dir_ = dir;
    return true;
  }

  bool SetPath() {
    if (setenv("PATH", temp_dir_.c_str(), 1) != 0) {
      return false;
    }
    path_changed_ = true;
    return true;
  }

  std::string old_path_;
  std::string temp_dir_template_;
  std::string temp_dir_;
  bool had_path_ = false;
  bool path_changed_ = false;
};
}  // namespace llm

#endif  // HIXL_TESTS_DEPENDS_LLM_DATADIST_HCCN_TOOL_TEST_HELPER_H_
