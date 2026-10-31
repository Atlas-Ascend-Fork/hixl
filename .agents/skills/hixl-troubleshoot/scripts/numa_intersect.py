#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

import logging
import os
import re
import shutil
import struct
import subprocess

logger = logging.getLogger(__name__)

PAGE_SIZE = os.sysconf("SC_PAGE_SIZE")
KPF_BUDDY = 10

# Base physical addresses for target ranges (PFNs built in main).
# The four node-local host segments start 8TiB apart and each spans 682GiB, so they do not
# overlap today; intersections are still merged defensively in case a driver exposes segments that
# do overlap.
RANGES = [
    0x29580000000,
    0xa9580000000,
    0x129580000000,
    0x1a9580000000,
]

RANGE_SIZE = 682 * 1024 * 1024 * 1024  # 682GB

# FabricMem host memory layout differs between SoC generations:
#   A3 (Ascend910_93): driver carves the host heap from the four fixed segments in RANGES.
#   A5 (Ascend950/Ascend910_95): the v3 SVM driver allocates host memory from ordinary
#       buddy pages, so there are no fixed segments and every free segment counts.
SOC_A2 = "A2"
SOC_A3 = "A3"
SOC_A5 = "A5"
SOC_UNKNOWN = "UNKNOWN"


_NODE_DIR_RE = re.compile(r"node(\d+)")
_MEMORY_BLOCK_DIR_RE = re.compile(r"memory(\d+)")


def _parse_memory_block_ids(names):
    """Yield memory block indexes from directory entry names.

    A NUMA node directory may also contain entries that merely start with
    "memory" (for example "memory_failure" on newer kernels), so only names
    that fully match "memory<digits>" are treated as memory block directories.
    """
    for name in names:
        match = _MEMORY_BLOCK_DIR_RE.fullmatch(name)
        if match is not None:
            yield int(match.group(1))
        elif name.startswith("memory"):
            logger.debug("skip %s: not a memory block directory", name)


def get_all_nodes():
    base = os.path.join("/sys", "devices", "system", "node")
    nodes = []
    for name in os.listdir(base):
        match = _NODE_DIR_RE.fullmatch(name)
        if match is not None:
            nodes.append(int(match.group(1)))
    return sorted(nodes)


def get_memory_block_size():
    with open(os.path.join("/sys", "devices", "system", "memory", "block_size_bytes")) as f:
        return int(f.read().strip(), 16)


def _memory_block_ids_from_global_dir(node):
    """Fallback block lookup for kernels without memory<block> under the node dir."""
    base = os.path.join("/sys", "devices", "system", "memory")
    block_ids = []
    for name in os.listdir(base):
        match = _MEMORY_BLOCK_DIR_RE.fullmatch(name)
        if match is None:
            continue
        node_path = os.path.join(base, name, f"node{node}")
        if os.path.islink(node_path) or os.path.isdir(node_path):
            block_ids.append(int(match.group(1)))
    return block_ids


def get_node_blocks(node):
    path = os.path.join("/sys", "devices", "system", "node", f"node{node}")
    block_size = get_memory_block_size()
    block_ids = list(_parse_memory_block_ids(os.listdir(path)))
    if not block_ids:
        logger.info(
            "NUMA node %s: no memory<block> entries under %s, falling back to "
            "/sys/devices/system/memory",
            node,
            path,
        )
        block_ids = _memory_block_ids_from_global_dir(node)

    blocks = []
    for idx in block_ids:
        start = idx * block_size // PAGE_SIZE
        end = (idx * block_size + block_size) // PAGE_SIZE - 1
        blocks.append((start, end))

    return sorted(blocks)


def is_buddy(flags):
    return (flags >> KPF_BUDDY) & 1


def addr_to_pfn(addr):
    return addr // PAGE_SIZE


def pfn_to_addr(pfn):
    return pfn * PAGE_SIZE


def intersect(a_start, a_end, b_start, b_end):
    s = max(a_start, b_start)
    e = min(a_end, b_end)
    if s <= e:
        return s, e
    return None


def _reset_run_if_block_gap(prev_end, start_pfn, merged_start, prev_state):
    """Complete a pending free run before clearing state at a physical gap."""
    if prev_end is None or start_pfn <= prev_end + 1:
        return merged_start, prev_state, []
    completed = [(merged_start, prev_end)] if prev_state else []
    return None, None, completed


def _apply_pfn_buddy_state(pfn, free, merged_start, prev_state):
    """
    Merge consecutive buddy/free PFNs. On transition out of a free run, emit one closed segment.
    Returns (new_merged_start, new_prev_state, list of completed free (start_pfn, end_pfn)).
    """
    if prev_state is None:
        return pfn, free, []

    if free == prev_state:
        return merged_start, prev_state, []

    completed = []
    if prev_state:
        completed.append((merged_start, pfn - 1))
    return pfn, free, completed


def _iter_block_pfns(kpageflags_file, start_pfn, end_pfn):
    """Sequential reads from kpageflags; caller must seek to start_pfn * 8 before calling."""
    for pfn in range(start_pfn, end_pfn + 1):
        data = kpageflags_file.read(8)
        if not data:
            break
        flags = struct.unpack("Q", data)[0]
        yield pfn, is_buddy(flags)


def scan_node_free_segments(node):
    blocks = get_node_blocks(node)
    merged_start = None
    prev_state = None
    prev_end = None

    with open(os.path.join("/proc", "kpageflags"), "rb") as kpf:
        for start_pfn, end_pfn in blocks:
            merged_start, prev_state, done = _reset_run_if_block_gap(
                prev_end, start_pfn, merged_start, prev_state)
            yield from done

            kpf.seek(start_pfn * 8)
            for pfn, free in _iter_block_pfns(kpf, start_pfn, end_pfn):
                merged_start, prev_state, done = _apply_pfn_buddy_state(
                    pfn, free, merged_start, prev_state)
                yield from done

            prev_end = end_pfn

        if prev_state:
            yield (merged_start, prev_end)


def fmt(addr):
    return f"0x{addr:016x}"


def build_target_ranges():
    target_ranges = []
    for base in RANGES:
        target_ranges.append((addr_to_pfn(base), addr_to_pfn(base + RANGE_SIZE - 1)))
    return target_ranges


def merge_segments(segments):
    """Merge overlapping/adjacent PFN segments so each memory range is counted once.

    Kept as a safeguard for driver layouts whose fixed segments overlap; the current RANGES do not
    overlap, so for them this is a no-op.
    """
    merged = []
    for start, end in sorted(segments):
        if merged and start <= merged[-1][1] + 1:
            merged[-1] = (merged[-1][0], max(merged[-1][1], end))
        else:
            merged.append((start, end))
    return merged


_NPU_SMI_CANDIDATES = (
    "npu-smi",
    "/usr/local/Ascend/driver/tools/npu-smi",
    "/usr/local/sbin/npu-smi",
    "/usr/sbin/npu-smi",
    "/usr/bin/npu-smi",
)


def _soc_from_name(name):
    """Map a pyACL/npu-smi SoC name to a generation; return None when unknown.

    The npu-smi Name column may carry a bare model (for example "910B4" or "910_93xx"), so the
    matcher must not require the "Ascend" prefix.
    """
    if not name:
        return None
    if "910B" in name:
        return SOC_A2
    if "910_93" in name:
        return SOC_A3
    if "910_95" in name or "950PR" in name or "950DT" in name or "Ascend950" in name:
        return SOC_A5
    # Some npu-smi versions only print the Ascend910 series name; keep mapping it to A3 because
    # A2 and A3 are already distinguished by the 910B/910_93 tokens above.
    if "Ascend910" in name:
        return SOC_A3
    return None


def _find_npu_smi():
    for candidate in _NPU_SMI_CANDIDATES:
        if os.sep in candidate:
            if os.path.isfile(candidate) and os.access(candidate, os.X_OK):
                return candidate
            continue
        path = shutil.which(candidate)
        if path is not None and os.access(path, os.X_OK):
            return path
    return None


def _run_npu_smi(exe, args):
    try:
        result = subprocess.run([exe, *args], capture_output=True, text=True, timeout=15, check=False)
    except (OSError, subprocess.SubprocessError):
        return ""
    if result.returncode != 0:
        logger.debug("npu-smi %s exited with %d", " ".join(args), result.returncode)
        return ""
    return (result.stdout or "") + (result.stderr or "")


def _detect_soc_by_npu_smi():
    """Read the chip model from npu-smi, which does not need pyACL to be importable."""
    exe = _find_npu_smi()
    if exe is None:
        return None
    text = _run_npu_smi(exe, ["info"])
    soc = _soc_from_name(text)
    if soc is not None:
        return soc
    text += _run_npu_smi(exe, ["info", "-t", "board"])
    return _soc_from_name(text)


def detect_soc(soc):
    """Resolve the SoC generation used to pick the memory accounting mode.

    Only A3 uses the four fixed host segments; on A5 the v3 SVM driver allocates
    host memory from regular buddy pages, so free memory of every NUMA node can
    be used by FabricMem, while A2 does not support FabricMem at all.
    """
    if soc != "auto":
        return soc.upper()

    try:
        import acl

        name = acl.get_soc_name()
    except Exception as exc:
        logger.debug("pyACL is not available for SoC detection: %s", exc)
        name = None

    soc_type = _soc_from_name(name)
    if soc_type is not None:
        logger.info("Detected SoC type by pyACL: %s (%s)", soc_type, name)
        return soc_type

    soc_type = _detect_soc_by_npu_smi()
    if soc_type is not None:
        logger.info("Detected SoC type by npu-smi: %s", soc_type)
        return soc_type

    logger.warning(
        "Failed to detect SoC type from pyACL/npu-smi; falling back to the A3 fixed ranges. "
        "If this machine is A5, rerun with --soc A5 (the A5 driver no longer uses the fixed segments)."
    )
    return SOC_UNKNOWN


def run_for_node(node, min_mb, target_ranges):
    logger.info("")
    logger.info("===== NUMA node %s =====", node)

    total_mb = 0
    label = "FREE∩RANGE" if target_ranges is not None else "FREE     "

    for free_start, free_end in scan_node_free_segments(node):
        if target_ranges is None:
            segments = [(free_start, free_end)]
        else:
            intersections = []
            for r_start, r_end in target_ranges:
                inter = intersect(free_start, free_end, r_start, r_end)
                if inter is not None:
                    intersections.append(inter)
            segments = merge_segments(intersections)

        for s, e in segments:
            size_mb = (e - s + 1) * PAGE_SIZE / 1024 / 1024

            if size_mb < min_mb:
                continue

            total_mb += size_mb

            logger.info(
                "%s : %s - %s  size=%.2f MB",
                label,
                fmt(pfn_to_addr(s)),
                fmt(pfn_to_addr(e)),
                size_mb,
            )

    if target_ranges is not None:
        logger.info("Total intersect FREE: %.2f MB", total_mb)
    else:
        logger.info("Total FREE: %.2f MB", total_mb)


def main():
    import argparse

    parser = argparse.ArgumentParser(description="NUMA free memory intersect tool (all nodes supported)")
    parser.add_argument("-n", "--node", type=int, help="NUMA node ID (default: all)")
    parser.add_argument("-m", "--min-mb", type=float, default=2048)
    parser.add_argument(
        "-s",
        "--soc",
        default="auto",
        choices=["auto", "A2", "A3", "A5"],
        help="SoC type that selects the memory accounting mode (default: auto)",
    )
    parser.add_argument(
        "-v",
        "--verbose",
        action="store_true",
        help="Enable debug logging",
    )
    args = parser.parse_args()

    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(message)s",
    )

    soc = detect_soc(args.soc)
    if soc == SOC_A5:
        target_ranges = None
        logger.info(
            "Detected SoC: %s. The A5 driver does not use the four fixed host segments; "
            "counting free memory of every NUMA node.",
            soc,
        )
    elif soc == SOC_A2:
        target_ranges = None
        logger.info("Detected SoC: %s. FabricMem is not supported on A2; counting free memory of every NUMA node.", soc)
    else:
        target_ranges = build_target_ranges()
        logger.info("Detected SoC: %s. Intersecting free memory with the four fixed A3 host segments.", soc)

    # Select NUMA nodes to scan.
    if args.node is not None:
        nodes = [args.node]
    else:
        nodes = get_all_nodes()

    logger.info("Scanning NUMA nodes...\n")

    for node in nodes:
        run_for_node(node, args.min_mb, target_ranges)


if __name__ == "__main__":
    main()
