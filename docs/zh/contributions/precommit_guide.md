# pre-commit使用指导书

[TOC]
---

## 1 使用背景

本指导书主要用于指导如何在本地使用代码仓中部署的pre-commit能力（主要包括代码格式化、单词拼写检查及OAT扫描能力），更多使用方式请参考 [pre-commit官方文档](https://pre-commit.com/)。

### 1.1 GitCode 镜像仓库说明

**重要**：由于国内网络环境，访问 GitHub 可能受限，CANN 社区已将常用的 pre-commit hooks 仓库镜像到 GitCode。

**常用镜像仓库对照表**：

| GitHub 原仓库 | GitCode 镜像仓库 | 说明 |
|--------------|-----------------|------|
| `https://github.com/pre-commit/pre-commit-hooks` | `https://gitcode.com/pre-commit/pre-commit-hooks` | 基础检查 hooks |
| `https://github.com/pre-commit/mirrors-clang-format` | `https://gitcode.com/pre-commit-clang/mirrors-clang-format` | C++ 格式化 |
| `https://github.com/astral-sh/ruff-pre-commit` | `https://gitcode.com/gh_mirrors/ru/ruff-pre-commit` | Python lint/format |
| `https://github.com/codespell-project/codespell` | `https://gitcode.com/gh_mirrors/co/codespell` | 拼写检查 |
| `https://github.com/crate-ci/typos` | `https://gitcode.com/gh_mirrors/ty/typos` | typos 检查 |

**使用建议**：在国内开发时，推荐使用 GitCode 镜像仓库，避免网络访问问题。

## 2 功能概述

1. 安装pre-commit后，git提交代码前会自动进行代码格式化处理、单词拼写扫描及触发OAT检查。

2. 合规性问题会阻止提交并提示修改，阻止并非强制修改，可以忽略修改。

## 3 社区贡献者使用pre-commit能力

### 3.1 pre-commit安装步骤

步骤 1：安装 pre-commit 框架（请确保已安装 python 和 pip）

```bash
# 使用 pip（推荐）
pip install pre-commit

# 验证安装
pre-commit --version
# 输出：pre-commit 3.x.x
```

步骤 2：进入项目目录

```bash
cd /path/to/your/project

# 例如
cd d:\complianceRepo\CANN
```

步骤 3：安装 Git Hooks

```bash
# 在项目根目录运行
pre-commit install
```

步骤 4：验证安装（可选）

```bash
# 测试 hook（不会真正提交）
git commit --allow-empty -m "test pre-commit"
```

后续在提交代码前会自动进行代码格式化处理及触发OAT检查。

#### 3.1.1 日志规范检查

pre-commit会对本次修改的C/C++文件执行日志规范检查，检查日志宏的格式化参数数量、非法占位符、
非英文字符或符号，以及向`*_NOLOG`宏传入无效日志参数等确定性问题。检查失败时会输出文件名和行号，
修复后重新执行提交即可。

```bash
# 检查指定文件
python3 scripts/check_log_spec.py src/hixl/cs/endpoint.cc

# 扫描src和include目录
python3 scripts/check_log_spec.py

# 只执行pre-commit中的日志规范检查
pre-commit run log-spec-check --all-files
```

外部API失败日志的上下文完整性、日志级别和性能敏感路径的打印频率仍需按照
`docs/zh/contributions/coding_standards/docs_specification.md`进行人工检视。

### 3.2 OAT使用指导

**OAT（Open Source Audit Tool）** 是一个开源合规性检查工具，自动集成到 Git 提交流程中。

本仓库通过 pre-commit 远程 hook 引用 OAT：hook 托管在 [HiAgent/compliance](https://gitcode.com/HiAgent/compliance) 仓库（`oat-check`，Python 实现），仓库内不保留本地检查脚本，无需安装 Java/Maven。

#### 3.2.1 检查内容

- **文件类型检查** - 禁止提交二进制文件（.so, .dll, .exe等）
- **许可证头检查** - 验证源代码文件包含合规的许可证声明

#### 3.2.2 核心特点

- **增量检查** - 仅检查本次提交涉及的文件，速度快
- **自动触发** - 每次 `git commit` 自动运行
- **远程 hook、零本地脚本** - 无需 Java/Maven、无需克隆额外仓库；首次运行时 pre-commit 自动安装 hook 运行环境
- **详细报告** - 自动生成 `oat_reports/result.txt` 摘要报告
- **跨平台** - Windows/Linux/macOS 全支持

#### 3.2.3 必需软件

| 软件 | 版本要求 | 用途 | 安装方式 |
|------|---------|------|----------|
| **Python** | 3.7+ | Hook 运行环境 | 手动安装 |
| **Git** | 2.0+ | 版本控制 | 通常已安装 |
| **pre-commit** | 2.0+ | Hook 框架 | `pip install pre-commit` |

首次运行 `oat-check` 时，pre-commit 会自动从 gitcode.com 拉取 hook 仓库并创建隔离的 Python 虚拟环境（自动安装 `lxml`、`chardet` 依赖），无需手动配置。

#### 3.2.4 手动运行检查

```bash
# 检查暂存区文件（与 git commit 自动触发一致）
pre-commit run oat-check

# 检查指定文件
pre-commit run oat-check --files src/foo.cc

# 检查一段范围（例如本地分支相对 master 的全部改动）
pre-commit run oat-check --from-ref origin/master --to-ref HEAD
```

#### 3.2.5 合规性问题（阻止提交）

**重要**：以下问题会**阻止提交**，必须修复。

**1) 发现无效文件类型**

**场景**：尝试提交二进制文件（.so, .dll, .exe 等）。

**输出**：
```
====================================================================
  OAT: Compliance issues found. Commit blocked.
====================================================================

[OAT] Cumulative scan: 1 file(s) checked, 1 issue(s).

[OAT] Report:
---
===================================
OAT Scan Result Summary
===================================
Scan Time: 2026-10-26 14:30:15
Project: hixl
Files Checked: 1

-----------------------------------
Invalid File Type Total Count: 1
Name:	Invalid File Type	Content:	binary	Line:	0	Project:	hixl	File:	/path/to/hixl/lib/libtest.so

-----------------------------------
License Header Invalid Total Count: 0
===================================
---

Fix the issues and recommit, or skip with:
  git commit --no-verify
```

**行为**：**阻止提交，必须修复**

**查看详情**：
```bash
cat oat_reports/result.txt
```

**解决方案**：
```bash
# 方法 1：移除二进制文件
git reset HEAD lib/libtest.so

# 方法 2：将二进制文件添加到 .gitignore
echo "*.so" >> .gitignore
echo "*.dll" >> .gitignore
echo "*.exe" >> .gitignore

# 重新提交
git add .gitignore
git commit -m "update: add binary files to gitignore"
```

**2) 许可证头无效**

**场景**：源代码文件缺少或许可证头格式不正确。

**输出**：
```
====================================================================
  OAT: Compliance issues found. Commit blocked.
====================================================================

[OAT] Cumulative scan: 2 file(s) checked, 2 issue(s).

[OAT] Report:
---
===================================
OAT Scan Result Summary
===================================
Scan Time: 2026-10-26 14:30:15
Project: hixl
Files Checked: 2

-----------------------------------
Invalid File Type Total Count: 0

-----------------------------------
License Header Invalid Total Count: 2
Name:	License Header Invalid	Content:	NoLicenseHeader	Line:	0	Project:	hixl	File:	/path/to/hixl/src/main.cpp
Name:	License Header Invalid	Content:	NoLicenseHeader	Line:	0	Project:	hixl	File:	/path/to/hixl/src/utils.cpp
===================================
---

Fix the issues and recommit, or skip with:
  git commit --no-verify
```

**行为**：**阻止提交，必须修复**

**查看详情**：
```bash
cat oat_reports/result.txt
```

**解决方案**：

在文件顶部添加许可证头，例如 CANN-2.0：

```cpp
/**
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

```

**重新提交**：
```bash
git add src/main.cpp src/utils.cpp
git commit -m "fix: add license headers"
```

---

#### 3.2.6 报告查看

**报告文件位置**

| 报告类型 | 文件路径 | 内容 |
|---------|---------|------|
| **摘要报告** | `oat_reports/result.txt` | 关键问题汇总  |

`oat_reports/` 目录已被 `.gitignore` 忽略，报告不会误提交到仓库。

**查看命令**

```bash
# 查看报告
cat oat_reports/result.txt

# 使用编辑器查看
code oat_reports/result.txt
vim oat_reports/result.txt
```

**摘要报告内容示例**

```
===================================
OAT Scan Result Summary
===================================
Scan Time: 2026-10-26 14:30:15
Project: hixl
Files Checked: 3

-----------------------------------
Invalid File Type Total Count: 0

-----------------------------------
License Header Invalid Total Count: 0
===================================
```

#### 3.2.7 环境问题

**说明**：与旧版本地脚本不同，远程 hook 在环境安装失败或扫描异常时**不会静默跳过**，而是使 hook 失败并阻止提交，避免合规检查被绕过。确需临时绕过时，可使用 `git commit --no-verify`。

**1) hook 环境安装失败**

**场景**：首次运行 `oat-check` 时无法访问 gitcode.com，或依赖（`lxml`、`chardet`）安装失败。

**行为**：pre-commit 报错，hook 失败，提交被阻止。

**解决方案**：
```bash
# 1. 检查网络与 gitcode.com 连通性
ping gitcode.com

# 2. 重试（pre-commit 会自动重建 hook 环境）
pre-commit run oat-check --files README.md

# 3. 确需临时绕过
git commit --no-verify
```

**2) OAT 扫描执行失败**

**场景**：hook 运行过程中发生内部错误。

**输出**：
```
[OAT] [ERROR] OAT scan failed: <错误信息>
```

**行为**：hook 返回失败，提交被阻止。

**解决方案**：
```bash
# 重试，确认是否为偶发问题
pre-commit run oat-check --files <文件>

# 若持续失败，可到 hook 仓库反馈：
# https://gitcode.com/HiAgent/compliance
```
