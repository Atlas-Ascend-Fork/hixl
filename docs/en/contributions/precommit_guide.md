# pre-commit User Guide

[TOC]
---

## 1 Background

This guide describes how to use the pre-commit capabilities deployed in the local code repository. These capabilities mainly include code formatting, spell check, and Open Source Audit Tool (OAT) scan. For more usage details, see the [official pre-commit documentation](https://pre-commit.com/).

### 1.1 GitCode Mirror Repository Notice

**Important**: Due to network environment in China, GitHub access may be restricted. CANN community has mirrored commonly used pre-commit hooks repositories to GitCode.

**Mirror Repository Mapping Table**:

| GitHub Original Repository | GitCode Mirror Repository | Description |
|--------------|-----------------|------|
| `https://github.com/pre-commit/pre-commit-hooks` | `https://gitcode.com/pre-commit/pre-commit-hooks` | Basic check hooks |
| `https://github.com/pre-commit/mirrors-clang-format` | `https://gitcode.com/pre-commit-clang/mirrors-clang-format` | C++ formatting |
| `https://github.com/astral-sh/ruff-pre-commit` | `https://gitcode.com/gh_mirrors/ru/ruff-pre-commit` | Python lint/format |
| `https://github.com/codespell-project/codespell` | `https://gitcode.com/gh_mirrors/co/codespell` | Spell check |
| `https://github.com/crate-ci/typos` | `https://gitcode.com/gh_mirrors/ty/typos` | typos check |

**Recommendation**: When developing in China, recommend using GitCode mirror repositories to avoid network access issues.

## 2 Feature Overview

1. After pre-commit is installed, code formatting, spell check, and OAT check are automatically triggered before code is committed with Git.

2. Compliance issues block the commit and prompt you to make changes. Blocking does not force a change. You can ignore the change if needed.

## 3 Use pre-commit as a Community Contributor

### 3.1 Install pre-commit

Step 1: Install the pre-commit framework. Make sure Python and pip are installed.

```bash
# Use pip (recommended)
pip install pre-commit

# Verify the installation
pre-commit --version
# Output: pre-commit 3.x.x
```

Step 2: Enter the project directory.

```bash
cd /path/to/your/project

# Example
cd d:\complianceRepo\CANN
```

Step 3: Install Git Hooks.

```bash
# Run this command in the project root directory
pre-commit install
```

Step 4: Verify the installation (optional).

```bash
# Test the hook. This command does not create a real commit.
git commit --allow-empty -m "test pre-commit"
```

After this setup, code formatting and OAT check are automatically triggered before you commit code.

#### 3.1.1 Log Specification Check

pre-commit checks the modified C/C++ files for deterministic logging issues, including mismatched formatting argument
counts, invalid conversion specifiers, non-ASCII text or punctuation, and invalid arguments passed to `*_NOLOG`
macros. If the check fails, it reports the file name and line number. Fix the issue and commit the changes again.

```bash
# Check a specified file
python3 scripts/check_log_spec.py src/hixl/cs/endpoint.cc

# Scan the src and include directories
python3 scripts/check_log_spec.py

# Run only the log specification check configured in pre-commit
pre-commit run log-spec-check --all-files
```

The completeness of external API failure context, log levels, and logging frequency on performance-sensitive paths
must still be reviewed manually according to
`docs/en/contributions/coding_standards/docs_specification.md`.

### 3.2 OAT User Guide

**Open Source Audit Tool (OAT)** is an open-source compliance check tool. It is automatically integrated into the Git commit workflow.

This repository references OAT through a pre-commit remote hook: the hook is hosted in the [HiAgent/compliance](https://gitcode.com/HiAgent/compliance) repository (`oat-check`, Python implementation). No local check script is kept in this repository, and Java/Maven installation is not required.

#### 3.2.1 Check Items

- **File type check** - Blocks binary files such as .so, .dll, and .exe.
- **License header check** - Verifies that source code files contain compliant license statements.

#### 3.2.2 Core Features

- **Incremental check** - Checks only files involved in the current commit and completes quickly.
- **Automatic trigger** - Runs automatically for every `git commit`.
- **Remote hook with zero local scripts** - No Java/Maven installation or additional repository cloning is required; pre-commit automatically installs the hook environment on the first run.
- **Detailed report** - Automatically generates a summary report at `oat_reports/result.txt`.
- **Cross-platform support** - Fully supports Windows, Linux, and macOS.

#### 3.2.3 Required Software

| Software | Version Requirement | Purpose | Installation Method |
|------|---------|------|----------|
| **Python** | 3.7+ | Hook runtime | Manual installation |
| **Git** | 2.0+ | Version control | Usually already installed |
| **pre-commit** | 2.0+ | Hook framework | `pip install pre-commit` |

When `oat-check` runs for the first time, pre-commit automatically pulls the hook repository from gitcode.com and creates an isolated Python virtual environment (automatically installing the `lxml` and `chardet` dependencies). No manual configuration is required.

#### 3.2.4 Run the Check Manually

```bash
# Check staged files (consistent with the automatic trigger of git commit)
pre-commit run oat-check

# Check specified files
pre-commit run oat-check --files src/foo.cc

# Check a range (for example, all changes of a local branch relative to master)
pre-commit run oat-check --from-ref origin/master --to-ref HEAD
```

#### 3.2.5 Compliance Issues (Block the Commit)

**Important**: The following issues **block the commit** and must be fixed.

**1) Invalid File Type Found**

**Scenario**: You try to commit a binary file such as .so, .dll, or .exe.

**Output**:
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

**Behavior**: **Block the commit. You must fix the issue.**

**View Details**:
```bash
cat oat_reports/result.txt
```

**Solution**:
```bash
# Method 1: Remove the binary file
git reset HEAD lib/libtest.so

# Method 2: Add binary files to .gitignore
echo "*.so" >> .gitignore
echo "*.dll" >> .gitignore
echo "*.exe" >> .gitignore

# Commit again
git add .gitignore
git commit -m "update: add binary files to gitignore"
```

**2) Invalid License Header**

**Scenario**: A source code file is missing a license header or has an incorrectly formatted license header.

**Output**:
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

**Behavior**: **Block the commit. You must fix the issue.**

**View Details**:
```bash
cat oat_reports/result.txt
```

**Solution**:

Add a license header at the top of the file, for example CANN-2.0:

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

**Commit Again**:
```bash
git add src/main.cpp src/utils.cpp
git commit -m "fix: add license headers"
```

---

#### 3.2.6 View Reports

**Report File Location**

| Report Type | File Path | Content |
|---------|---------|------|
| **Summary report** | `oat_reports/result.txt` | Key issue summary  |

The `oat_reports/` directory is ignored by `.gitignore`, so reports will not be accidentally committed to the repository.

**View Commands**

```bash
# View the report
cat oat_reports/result.txt

# View the report with an editor
code oat_reports/result.txt
vim oat_reports/result.txt
```

**Summary Report Content Example**

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

#### 3.2.7 Environment Issues

**Note**: Unlike the legacy local script, the remote hook **does not silently skip** the check when the hook environment installation fails or a scan error occurs. Instead, the hook fails and blocks the commit, preventing compliance checks from being bypassed. To bypass it temporarily when necessary, use `git commit --no-verify`.

**1) Hook Environment Installation Failure**

**Scenario**: gitcode.com is inaccessible when `oat-check` runs for the first time, or a dependency (`lxml`, `chardet`) fails to install.

**Behavior**: pre-commit reports an error, the hook fails, and the commit is blocked.

**Solution**:
```bash
# 1. Check the network connectivity to gitcode.com
ping gitcode.com

# 2. Retry (pre-commit automatically rebuilds the hook environment)
pre-commit run oat-check --files README.md

# 3. Bypass temporarily if necessary
git commit --no-verify
```

**2) OAT Scan Execution Failure**

**Scenario**: An internal error occurs while the hook is running.

**Output**:
```
[OAT] [ERROR] OAT scan failed: <error message>
```

**Behavior**: The hook fails and the commit is blocked.

**Solution**:
```bash
# Retry to check whether the issue is transient
pre-commit run oat-check --files <file>

# If the failure persists, provide feedback in the hook repository:
# https://gitcode.com/HiAgent/compliance
```
