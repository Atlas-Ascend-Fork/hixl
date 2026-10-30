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

import ctypes
import logging
import multiprocessing
from queue import Empty
import time
from dataclasses import dataclass

import hixl

from .utils import (
    CONNECT_TIMEOUT_MS,
    TRANSFER_SIZE,
    TRANSFER_TIMEOUT_MS,
    RegisteredMem,
    alloc_host_mem_pinned,
    build_op_descs,
    create_engine,
    get_device_lists,
    get_endpoint,
    set_device,
    suppress_noisy_loggers,
)

logger = logging.getLogger(__name__)

# 场景索引，用于端口分配
SCENARIO_IDX = 1
# 每次传输的内存大小
MEM_SIZE = TRANSFER_SIZE
# Remote Dummy 填充的数据模式
REMOTE_BYTE = 0xBB
# Local Dummy 填充的数据模式
LOCAL_BYTE = 0xAA
# Remote Real host memory 填充的数据模式
REMOTE_HOST_BYTE = 0xCC

# Values and ctypes layouts follow the public acl/acl_rt.h ABI.
ACL_MEM_HANDLE_TYPE_NONE = 0
ACL_MEM_ALLOCATION_TYPE_PINNED = 0
ACL_MEM_LOCATION_TYPE_DEVICE = 1
ACL_HBM_MEM_NORMAL = 5
ACL_RT_MEM_ALLOC_GRANULARITY_MINIMUM = 0
ACL_RT_MEM_ACCESS_FLAGS_READWRITE = 0x3
ACL_RT_VMM_EXPORT_FLAG_DISABLE_PID_VALIDATION = 0x1
ACL_MEMCPY_DEVICE_TO_HOST = 2


PHASE_TIMEOUT_S = 120
STARTUP_SETTLE_DELAY_S = 10

_ACL_LIB = None


def _init_acl():
    """初始化 ACL 运行时环境"""
    import acl

    acl.init()


class _MemLocation(ctypes.Structure):
    _fields_ = [("id", ctypes.c_uint32), ("type", ctypes.c_int)]


class _PhysicalMemProp(ctypes.Structure):
    _fields_ = [
        ("handleType", ctypes.c_int),
        ("allocationType", ctypes.c_int),
        ("memAttr", ctypes.c_int),
        ("location", _MemLocation),
        ("reserve", ctypes.c_uint64),
    ]


class _MemAccessDesc(ctypes.Structure):
    _fields_ = [
        ("flags", ctypes.c_int),
        ("location", _MemLocation),
        ("rsv", ctypes.c_uint8 * 12),
    ]


def _get_acl_lib():
    global _ACL_LIB
    if _ACL_LIB is None:
        lib = ctypes.CDLL("libascendcl.so")
        ptr = ctypes.c_void_p
        size = ctypes.c_size_t
        u64 = ctypes.c_uint64
        signatures = {
            "aclrtMemGetAllocationGranularity": [
                ctypes.POINTER(_PhysicalMemProp),
                ctypes.c_int,
                ctypes.POINTER(size),
            ],
            "aclrtMallocPhysical": [
                ctypes.POINTER(ptr),
                size,
                ctypes.POINTER(_PhysicalMemProp),
                u64,
            ],
            "aclrtReserveMemAddress": [ctypes.POINTER(ptr), size, size, ptr, u64],
            "aclrtMapMem": [ptr, size, size, ptr, u64],
            "aclrtMemSetAccess": [ptr, size, ctypes.POINTER(_MemAccessDesc), size],
            "aclrtMemExportToShareableHandle": [
                ptr,
                ctypes.c_int,
                u64,
                ctypes.POINTER(u64),
            ],
            "aclrtMemImportFromShareableHandle": [
                u64,
                ctypes.c_int32,
                ctypes.POINTER(ptr),
            ],
            "aclrtMemset": [ptr, size, ctypes.c_int32, size],
            "aclrtMemcpy": [ptr, size, ptr, size, ctypes.c_int],
            "aclrtUnmapMem": [ptr],
            "aclrtReleaseMemAddress": [ptr],
            "aclrtFreePhysical": [ptr],
        }
        for name, argtypes in signatures.items():
            try:
                function = getattr(lib, name)
            except AttributeError:
                raise RuntimeError(f"Required VMM API {name} is unavailable") from None
            function.argtypes = argtypes
            function.restype = ctypes.c_int
        _ACL_LIB = lib
    return _ACL_LIB


def _acl_call(name, *args, context=""):
    ret = getattr(_get_acl_lib(), name)(*args)
    if ret != 0:
        raise RuntimeError(f"{name} failed: ret={ret}, {context}")


@dataclass(frozen=True)
class _VmmShare:
    """Only serializable sharing metadata crosses the process boundary."""

    device_id: int
    data_size: int
    mapped_size: int
    shareable_handle: int


class _VmmBuffer:
    def __init__(self, device_id, data_size, role):
        self.device_id = device_id
        self.data_size = data_size
        self.role = role
        self.mapped_size = 0
        self.handle = ctypes.c_void_p()
        self.va = ctypes.c_void_p()
        self.mapped = False

    @property
    def addr(self):
        return self.va.value

    @property
    def context(self):
        return (
            f"role={self.role}, device_id={self.device_id}, "
            f"va=0x{self.addr or 0:x}, data_size={self.data_size} bytes, "
            f"mapped_size={self.mapped_size} bytes"
        )

    def call(self, name, *args):
        # IDs are process-visible logical IDs, including when visible cards are 6,7.
        set_device(self.device_id)
        _acl_call(name, *args, context=self.context)

    @classmethod
    def allocate(cls, device_id, data_size, role):
        if data_size <= 0:
            raise ValueError(f"Invalid VMM data_size={data_size} bytes")
        buffer = cls(device_id, data_size, role)
        prop = _PhysicalMemProp(
            handleType=ACL_MEM_HANDLE_TYPE_NONE,
            allocationType=ACL_MEM_ALLOCATION_TYPE_PINNED,
            memAttr=ACL_HBM_MEM_NORMAL,
            location=_MemLocation(device_id, ACL_MEM_LOCATION_TYPE_DEVICE),
        )
        granularity = ctypes.c_size_t()
        try:
            buffer.call(
                "aclrtMemGetAllocationGranularity",
                ctypes.byref(prop),
                ACL_RT_MEM_ALLOC_GRANULARITY_MINIMUM,
                ctypes.byref(granularity),
            )
            if granularity.value == 0:
                raise RuntimeError(
                    f"VMM allocation granularity is zero: {buffer.context}"
                )
            unit = granularity.value
            buffer.mapped_size = (data_size + unit - 1) // unit * unit
            buffer.call(
                "aclrtMallocPhysical",
                ctypes.byref(buffer.handle),
                buffer.mapped_size,
                ctypes.byref(prop),
                0,
            )
            buffer._map()
            logger.info("[VMM] allocated: %s", buffer.context)
            return buffer
        except Exception:
            buffer.close()
            raise

    @classmethod
    def import_shared(cls, share, device_id, role):
        if share.device_id != device_id:
            raise ValueError(
                f"VMM device mismatch: exported={share.device_id}, imported={device_id}"
            )
        if share.data_size != MEM_SIZE or share.mapped_size < share.data_size:
            raise ValueError("Invalid VMM shared allocation size")
        buffer = cls(device_id, share.data_size, role)
        buffer.mapped_size = share.mapped_size
        try:
            buffer.call(
                "aclrtMemImportFromShareableHandle",
                share.shareable_handle,
                device_id,
                ctypes.byref(buffer.handle),
            )
            buffer._map()
            logger.info("[VMM] imported: %s", buffer.context)
            return buffer
        except Exception:
            buffer.close()
            raise

    def _map(self):
        self.call(
            "aclrtReserveMemAddress",
            ctypes.byref(self.va),
            self.mapped_size,
            0,
            None,
            0,
        )
        self.call("aclrtMapMem", self.va, self.mapped_size, 0, self.handle, 0)
        self.mapped = True
        access = _MemAccessDesc(
            flags=ACL_RT_MEM_ACCESS_FLAGS_READWRITE,
            location=_MemLocation(self.device_id, ACL_MEM_LOCATION_TYPE_DEVICE),
        )
        self.call(
            "aclrtMemSetAccess", self.va, self.mapped_size, ctypes.byref(access), 1
        )

    def export(self):
        shareable = ctypes.c_uint64()
        self.call(
            "aclrtMemExportToShareableHandle",
            self.handle,
            ACL_MEM_HANDLE_TYPE_NONE,
            ACL_RT_VMM_EXPORT_FLAG_DISABLE_PID_VALIDATION,
            ctypes.byref(shareable),
        )
        logger.info("[VMM] exported: %s", self.context)
        return _VmmShare(
            self.device_id, self.data_size, self.mapped_size, shareable.value
        )

    def fill(self, value):
        self.call("aclrtMemset", self.va, self.data_size, value, self.data_size)

    def verify(self, expected_byte):
        host_buffer = ctypes.create_string_buffer(self.data_size)
        self.call(
            "aclrtMemcpy",
            ctypes.addressof(host_buffer),
            self.data_size,
            self.va,
            self.data_size,
            ACL_MEMCPY_DEVICE_TO_HOST,
        )
        observed = host_buffer.raw
        expected = bytes([expected_byte]) * self.data_size
        if observed != expected:
            offset = next(
                i for i, value in enumerate(observed) if value != expected_byte
            )
            raise AssertionError(
                f"VMM data mismatch: {self.context}, offset={offset} bytes, "
                f"expected=0x{expected_byte:02X}, got=0x{observed[offset]:02X}"
            )

    def close(self):
        """Release acquired resources in order; do not hide the original error."""
        errors = []
        if self.mapped:
            try:
                self.call("aclrtUnmapMem", self.va)
                self.mapped = False
            except Exception as exc:
                errors.append(str(exc))
        if self.va.value is not None and not self.mapped:
            try:
                self.call("aclrtReleaseMemAddress", self.va)
                self.va = ctypes.c_void_p()
            except Exception as exc:
                errors.append(str(exc))
        if self.handle.value is not None and not self.mapped:
            try:
                self.call("aclrtFreePhysical", self.handle)
                self.handle = ctypes.c_void_p()
            except Exception as exc:
                errors.append(str(exc))
        for error in errors:
            logger.error("[VMM] cleanup failed: %s", error)
        return errors


def _close_vmm_buffers(buffers):
    errors = []
    for buffer in reversed(buffers):
        try:
            errors.extend(buffer.close())
        except Exception as exc:
            errors.append(f"VMM cleanup failed: {exc}")
    return errors


def _cleanup_real(dev_mems, host_mems, engines, buffers):
    errors = []
    for resource in [*dev_mems, *host_mems]:
        try:
            resource.deregister()
        except Exception as exc:
            errors.append(f"Real resource cleanup failed: {exc}")
    for engine in engines:
        try:
            ret = engine.finalize()
            if ret is not None and ret != hixl.SUCCESS:
                errors.append(f"Engine finalize failed: ret={ret}")
        except Exception as exc:
            errors.append(f"Engine finalize failed: {exc}")
    errors.extend(_close_vmm_buffers(buffers))
    return errors


def _report_phase(queue, role, phase, error=None):
    queue.put((role, phase, error is None, error))


def _wait_for_phase(event, aborted, phase):
    deadline = time.monotonic() + PHASE_TIMEOUT_S
    while not aborted.is_set():
        remaining = deadline - time.monotonic()
        assert remaining > 0, f"Transfer phase timeout: phase={phase}"
        if event.wait(timeout=min(0.1, remaining)):
            assert not aborted.is_set(), f"Transfer aborted: phase={phase}"
            return
    raise AssertionError(f"Transfer aborted: phase={phase}")


def _wait_phase_ack(queue, phase):
    try:
        acknowledged, error = queue.get(timeout=PHASE_TIMEOUT_S)
    except Empty:
        raise AssertionError(
            f"Verification acknowledgement timeout: phase={phase}"
        ) from None
    assert error is None, f"Verification aborted: phase={phase}, error={error}"
    assert acknowledged == phase, (
        f"Unexpected acknowledgement: {acknowledged}, expected={phase}"
    )


def _verify_phases(results, acknowledgements):
    for role, phase in (
        ("R-DUMMY", "D2RD WRITE"),
        ("L-DUMMY", "D2RD READ"),
        ("R-REAL", "D2RH WRITE"),
        ("L-DUMMY", "D2RH READ"),
    ):
        try:
            actual_role, actual_phase, ok, error = results.get(timeout=PHASE_TIMEOUT_S)
        except Empty:
            raise AssertionError(
                f"Verification result timeout: role={role}, phase={phase}"
            ) from None
        assert ok, (
            f"Verification failed: role={actual_role}, phase={actual_phase}, error={error}"
        )
        assert (actual_role, actual_phase) == (role, phase), (
            f"Unexpected verification result: role={actual_role}, phase={actual_phase}"
        )
        acknowledgements.put((phase, None))


def _check_worker_results(results, processes):
    expected = {
        role
        for role, process, _ in processes
        if process is not None and process.pid is not None
    }
    errors = []
    deadline = time.monotonic() + 10
    received = set()
    while received != expected:
        try:
            role, worker_errors = results.get(
                timeout=max(0, deadline - time.monotonic())
            )
        except Empty:
            errors.append(f"Missing worker results: {sorted(expected - received)}")
            break
        if role not in expected or role in received:
            errors.append(f"Unexpected final worker result: role={role}")
            break
        received.add(role)
        errors.extend(f"{role}: {error}" for error in worker_errors)
    for role, process, _ in processes:
        if role in expected and process.exitcode != 0:
            errors.append(
                f"Worker exited abnormally: role={role}, exitcode={process.exitcode}"
            )
    return errors


def _remote_dummy_worker(
    ready_event,
    key_queue,
    d2rd_write_done_event,
    stop_event,
    phase_results,
    worker_results,
    aborted,
):
    """Remote Dummy 进程工作函数

    职责：
    1. 为每个 remote 设备分配 device memory
    2. 填充 REMOTE_BYTE 模式数据
    3. 导出 VMM shared handle（关闭 PID 白名单校验）
    4. 通过 Queue 传递 VMM shared handles 给 Remote Real
    5. 等待 D2RD WRITE 完成后验证 Remote device memory 包含 LOCAL_BYTE

    参数:
        ready_event: 就绪信号
        key_queue: 传递 VMM shared handles 的队列
        d2rd_write_done_event: D2RD WRITE 完成信号
        stop_event: 停止信号
    """
    logging.basicConfig(
        format="%(asctime)s [S2-R-DUMMY] %(message)s", level=logging.INFO
    )
    suppress_noisy_loggers()
    _, remote_devs = get_device_lists()
    keys = []
    bufs = []
    errors = []
    try:
        _init_acl()
        # 为每个 remote 设备分配并导出 VMM shared handle
        for dev_id in remote_devs:
            set_device(dev_id)
            buf = _VmmBuffer.allocate(dev_id, MEM_SIZE, "R-DUMMY")
            bufs.append(buf)
            buf.fill(REMOTE_BYTE)
            key = buf.export()
            keys.append(key)

        key_queue.put(keys)
        ready_event.set()

        # 等待 D2RD WRITE 完成并验证 Remote device memory 被写入 LOCAL_BYTE
        _wait_for_phase(d2rd_write_done_event, aborted, "D2RD WRITE")
        for buf in bufs:
            buf.verify(LOCAL_BYTE)

        _report_phase(phase_results, "R-DUMMY", "D2RD WRITE")

        # 等待停止信号
        stop_event.wait(timeout=300)
    except Exception as e:
        errors.append(str(e))
        _report_phase(phase_results, "R-DUMMY", "D2RD WRITE", str(e))
        logger.error("Remote dummy error: %s", e)
        ready_event.set()
        key_queue.put([])
    finally:
        # Keep exported physical memory alive until the importing Real exits.
        stop_event.wait()
        logger.info("[R-DUMMY] release VMM buffers, count=%d", len(bufs))
        errors.extend(_close_vmm_buffers(bufs))
        worker_results.put(("R-DUMMY", errors))
        logger.info("[R-DUMMY] cleanup done")


def _remote_real_worker(
    dummy_keys,
    ready_event,
    addr_queue,
    d2rh_write_done_event,
    stop_event,
    phase_results,
    worker_results,
    aborted,
):
    """Remote Real 进程工作函数

    职责：
    1. 使能 P2P 访问（local_devs <-> remote_devs）
    2. 分配一大块 pinned host memory（MEM_SIZE * N），填充 REMOTE_HOST_BYTE
    3. 导入 Remote Dummy 的 device memory（通过 VMM shared handle）
    4. 创建 N 个 HIXL engines，每个 engine 注册：
       - 导入的 device memory（对应的一段）
       - 整块 host memory
    5. 通过 Queue 传递 device_addrs 和 host_base_addr 给 Local Real
    6. 等待 D2RH WRITE 完成后验证 host memory 包含 LOCAL_BYTE

    参数:
        dummy_keys: Remote Dummy 导出的 VMM shared handles
        ready_event: 就绪信号
        addr_queue: 传递地址信息的队列
        d2rh_write_done_event: D2RH WRITE 完成信号
        stop_event: 停止信号
    """
    logging.basicConfig(
        format="%(asctime)s [S2-R-REAL] %(message)s", level=logging.INFO
    )
    suppress_noisy_loggers()
    local_devs, _ = get_device_lists()
    _, remote_devs = get_device_lists()
    n = len(remote_devs)
    engines = []
    dev_mems = []
    host_mems = []
    host_buf = None
    imported_bufs = []
    errors = []
    try:
        _init_acl()
        # 分配一大块 pinned host memory
        total_size = MEM_SIZE * n
        host_base_addr, _, host_buf = alloc_host_mem_pinned(total_size)
        host_buf.fill_(REMOTE_HOST_BYTE)

        # 为每个 remote 设备创建 engine 并注册内存
        imported_addrs = []
        for i, dev_id in enumerate(remote_devs):
            ep = get_endpoint(SCENARIO_IDX, i, "server")
            eng = create_engine(dev_id, ep)
            engines.append(eng)

            # 导入 Remote Dummy 的 device memory
            buf = _VmmBuffer.import_shared(dummy_keys[i], dev_id, "R-REAL")
            imported_bufs.append(buf)
            mapped_addr = buf.addr
            imported_addrs.append(mapped_addr)

            # 注册导入的 device memory
            dm = RegisteredMem(eng, mapped_addr, MEM_SIZE, hixl.MEM_DEVICE)
            dev_mems.append(dm)

            # 注册整块 host memory
            hm = RegisteredMem(eng, host_base_addr, total_size, hixl.MEM_HOST)
            host_mems.append(hm)

        # 传递地址信息给 Local Real
        addr_queue.put(
            {
                "device_addrs": {i: imported_addrs[i] for i in range(n)},
                "host_base_addr": host_base_addr,
            }
        )
        ready_event.set()

        # 等待 D2RH WRITE 完成并验证 host memory 被写入 LOCAL_BYTE
        _wait_for_phase(d2rh_write_done_event, aborted, "D2RH WRITE")
        for i in range(n):
            region = host_buf[i * MEM_SIZE : (i + 1) * MEM_SIZE]  # noqa: E203
            assert (region == LOCAL_BYTE).all().item(), (
                f"D2RH WRITE verify failed: host[{i}] expected 0x{LOCAL_BYTE:02X}, got 0x{region[0].item():02X}"
            )

        _report_phase(phase_results, "R-REAL", "D2RH WRITE")
        stop_event.wait(timeout=300)
    except Exception as e:
        errors.append(str(e))
        _report_phase(phase_results, "R-REAL", "D2RH WRITE", str(e))
        logger.error("Remote real error: %s", e)
        ready_event.set()
        addr_queue.put({})
    finally:
        stop_event.wait()
        errors.extend(_cleanup_real(dev_mems, host_mems, engines, imported_bufs))
        worker_results.put(("R-REAL", errors))
        logger.info("[R-REAL] cleanup done")


@dataclass
class LocalDummyArgs:
    """Local Dummy 进程参数"""

    ready_event: multiprocessing.Event
    key_queue: multiprocessing.Queue
    d2rd_done_event: multiprocessing.Event
    d2rh_done_event: multiprocessing.Event
    phase_results: multiprocessing.Queue
    worker_results: multiprocessing.Queue
    aborted: multiprocessing.Event
    stop_event: multiprocessing.Event


@dataclass
class LocalRealArgs:
    """Local Real 进程参数"""

    dummy_keys: list
    remote_info: dict
    d2rd_write_done_event: multiprocessing.Event
    d2rd_done_event: multiprocessing.Event
    d2rh_write_done_event: multiprocessing.Event
    d2rh_done_event: multiprocessing.Event
    phase_results: multiprocessing.Queue
    worker_results: multiprocessing.Queue
    phase_acknowledgements: multiprocessing.Queue


def _local_dummy_worker(args: LocalDummyArgs):
    """Local Dummy 进程工作函数

    职责：
    1. 为每个 local 设备分配 device memory
    2. 填充 LOCAL_BYTE 模式数据
    3. 导出 VMM shared handle（关闭 PID 白名单校验）
    4. 通过 Queue 传递 VMM shared handles 给 Local Real
    5. 等待 D2RD/D2RH 完成后验证数据

    验证逻辑：
    - D2RD: WRITE 后 Local 清零，READ 回 LOCAL_BYTE
    - D2RH: WRITE 后 Local 清零，READ 回 LOCAL_BYTE
    """
    logging.basicConfig(
        format="%(asctime)s [S2-L-DUMMY] %(message)s", level=logging.INFO
    )
    suppress_noisy_loggers()
    local_devs, _ = get_device_lists()
    keys = []
    bufs = []
    errors = []
    phase = "D2RD READ"
    try:
        _init_acl()
        for dev_id in local_devs:
            set_device(dev_id)
            buf = _VmmBuffer.allocate(dev_id, MEM_SIZE, "L-DUMMY")
            bufs.append(buf)
            buf.fill(LOCAL_BYTE)
            key = buf.export()
            keys.append(key)

        args.key_queue.put(keys)
        args.ready_event.set()

        _wait_for_phase(args.d2rd_done_event, args.aborted, phase)
        for buf in bufs:
            buf.verify(LOCAL_BYTE)
        _report_phase(args.phase_results, "L-DUMMY", phase)
        phase = "D2RH READ"

        _wait_for_phase(args.d2rh_done_event, args.aborted, phase)
        for buf in bufs:
            buf.verify(LOCAL_BYTE)
        _report_phase(args.phase_results, "L-DUMMY", phase)

        args.stop_event.wait(timeout=30)
    except Exception as e:
        errors.append(str(e))
        _report_phase(args.phase_results, "L-DUMMY", phase, str(e))
        logger.error("Local dummy error: %s", e)
        args.ready_event.set()
        args.key_queue.put([])
    finally:
        # Keep exported physical memory alive until the importing Real exits.
        args.stop_event.wait()
        logger.info("[L-DUMMY] release VMM buffers, count=%d", len(bufs))
        errors.extend(_close_vmm_buffers(bufs))
        args.worker_results.put(("L-DUMMY", errors))
        logger.info("[L-DUMMY] cleanup done")


def _local_real_worker(args: LocalRealArgs):
    """Local Real 进程工作函数

    职责：
    1. 使能 P2P 访问（local_devs <-> remote_devs）
    2. 分配一大块 pinned host memory（MEM_SIZE * N）
    3. 导入 Local Dummy 的 device memory（通过 VMM shared handle）
    4. 创建 N 个 HIXL engines，每个 engine 注册：
       - 导入的 device memory（对应的一段）
       - 整块 host memory
    5. 连接 Remote Real
    6. D2RD WRITE: Local device → Remote device，然后清零 Local device
    7. D2RD READ: Remote device → Local device
    8. D2RH WRITE: Local device → Remote host，然后清零 Local device
    9. D2RH READ: Remote host → Local device
    """
    logging.basicConfig(
        format="%(asctime)s [S2-L-REAL] %(message)s", level=logging.INFO
    )
    suppress_noisy_loggers()
    local_devs, _ = get_device_lists()
    _, remote_devs = get_device_lists()
    n = len(local_devs)
    engines = []
    dev_mems = []
    host_mems = []
    imported_addrs = []
    imported_bufs = []
    host_buf = None
    errors = []
    phase = "D2RD WRITE"
    try:
        _init_acl()
        total_size = MEM_SIZE * n
        host_base_addr, _, host_buf = alloc_host_mem_pinned(total_size)

        for i, dev_id in enumerate(local_devs):
            ep = get_endpoint(SCENARIO_IDX, i, "client")
            eng = create_engine(dev_id, ep)
            engines.append(eng)

            buf = _VmmBuffer.import_shared(args.dummy_keys[i], dev_id, "L-REAL")
            imported_bufs.append(buf)
            mapped_addr = buf.addr
            imported_addrs.append(mapped_addr)

            dm = RegisteredMem(eng, mapped_addr, MEM_SIZE, hixl.MEM_DEVICE)
            dev_mems.append(dm)

            hm = RegisteredMem(eng, host_base_addr, total_size, hixl.MEM_HOST)
            host_mems.append(hm)

        for i in range(n):
            remote_ep = get_endpoint(SCENARIO_IDX, i, "server")
            ret = engines[i].connect(remote_ep, timeout_in_millis=CONNECT_TIMEOUT_MS)
            logger.info(
                f"[L-REAL] connect[{i}] dev{local_devs[i]}->remote[{i}](dev{remote_devs[i]}): ret={ret}"
            )
            assert ret == hixl.SUCCESS, f"Connect local[{i}]->remote[{i}] failed: {ret}"

        remote_device_addrs = args.remote_info["device_addrs"]

        for i in range(n):
            remote_ep = get_endpoint(SCENARIO_IDX, i, "server")
            op_descs = build_op_descs(
                imported_addrs[i], remote_device_addrs[i], MEM_SIZE
            )
            ret = engines[i].transfer_sync(
                remote_ep,
                hixl.TransferOp.WRITE,
                op_descs,
                timeout_in_millis=TRANSFER_TIMEOUT_MS,
            )
            logger.info(
                f"[L-REAL] D2RD WRITE[{i}] dev{local_devs[i]}->remote[{i}](dev{remote_devs[i]}): ret={ret}"
            )
            assert ret == hixl.SUCCESS, f"D2RD WRITE local[{i}] failed: {ret}"

        args.d2rd_write_done_event.set()
        _wait_phase_ack(args.phase_acknowledgements, "D2RD WRITE")

        phase = "D2RD READ"
        for i in range(n):
            imported_bufs[i].fill(0x00)
            imported_bufs[i].verify(0x00)
            remote_ep = get_endpoint(SCENARIO_IDX, i, "server")
            op_descs = build_op_descs(
                imported_addrs[i], remote_device_addrs[i], MEM_SIZE
            )
            ret = engines[i].transfer_sync(
                remote_ep,
                hixl.TransferOp.READ,
                op_descs,
                timeout_in_millis=TRANSFER_TIMEOUT_MS,
            )
            logger.info(
                f"[L-REAL] D2RD READ[{i}] dev{local_devs[i]}<-remote[{i}](dev{remote_devs[i]}): ret={ret}"
            )
            assert ret == hixl.SUCCESS, f"D2RD READ local[{i}] failed: {ret}"

        args.d2rd_done_event.set()
        _wait_phase_ack(args.phase_acknowledgements, "D2RD READ")

        phase = "D2RH WRITE"
        remote_host_base = args.remote_info["host_base_addr"]

        for i in range(n):
            remote_ep = get_endpoint(SCENARIO_IDX, i, "server")
            remote_addr = remote_host_base + i * MEM_SIZE
            op_descs = build_op_descs(imported_addrs[i], remote_addr, MEM_SIZE)
            ret = engines[i].transfer_sync(
                remote_ep,
                hixl.TransferOp.WRITE,
                op_descs,
                timeout_in_millis=TRANSFER_TIMEOUT_MS,
            )
            logger.info(
                f"[L-REAL] D2RH WRITE[{i}] dev{local_devs[i]}->remote[{i}](dev{remote_devs[i]}): ret={ret}"
            )
            assert ret == hixl.SUCCESS, f"D2RH WRITE local[{i}] failed: {ret}"

        args.d2rh_write_done_event.set()
        _wait_phase_ack(args.phase_acknowledgements, "D2RH WRITE")

        phase = "D2RH READ"
        for i in range(n):
            imported_bufs[i].fill(0x00)
            imported_bufs[i].verify(0x00)
            remote_ep = get_endpoint(SCENARIO_IDX, i, "server")
            remote_addr = remote_host_base + i * MEM_SIZE
            op_descs = build_op_descs(imported_addrs[i], remote_addr, MEM_SIZE)
            ret = engines[i].transfer_sync(
                remote_ep,
                hixl.TransferOp.READ,
                op_descs,
                timeout_in_millis=TRANSFER_TIMEOUT_MS,
            )
            logger.info(
                f"[L-REAL] D2RH READ[{i}] dev{local_devs[i]}<-remote[{i}](dev{remote_devs[i]}): ret={ret}"
            )
            assert ret == hixl.SUCCESS, f"D2RH READ local[{i}] failed: {ret}"

        args.d2rh_done_event.set()
        _wait_phase_ack(args.phase_acknowledgements, "D2RH READ")

        for i in range(n):
            remote_ep = get_endpoint(SCENARIO_IDX, i, "server")
            ret = engines[i].disconnect(remote_ep, timeout_in_millis=CONNECT_TIMEOUT_MS)
            logger.info(
                f"[L-REAL] disconnect[{i}] dev{local_devs[i]}->remote[{i}](dev{remote_devs[i]}): ret={ret}"
            )

    except Exception as e:
        errors.append(str(e))
        _report_phase(args.phase_results, "L-REAL", phase, str(e))
        logger.error("Local real error: %s", e)
        logger.info(f"[L-REAL] disconnect engines, count={len(engines)}")
        for i, eng in enumerate(engines):
            try:
                remote_ep = get_endpoint(SCENARIO_IDX, i, "server")
                eng.disconnect(remote_ep, timeout_in_millis=CONNECT_TIMEOUT_MS)
            except Exception as disconnect_err:
                logger.warning(
                    "Failed to disconnect engine[%d] in error handler: %s",
                    i,
                    disconnect_err,
                )
    finally:
        errors.extend(_cleanup_real(dev_mems, host_mems, engines, imported_bufs))
        args.worker_results.put(("L-REAL", errors))
        logger.info("[L-REAL] cleanup done")


def _stop_process(process, stop_event):
    """Stop importers before releasing the physical memory owned by Dummies."""
    if process is None or process.pid is None:
        return
    if stop_event is not None:
        stop_event.set()
    process.join(timeout=30)
    if process.is_alive():
        logger.warning("Worker %s did not stop within 30 s; terminating", process.name)
        process.terminate()
        process.join(timeout=10)
    if process.is_alive():
        process.kill()
        process.join(timeout=10)
    if process.is_alive():
        raise RuntimeError(f"Unable to stop importer/owner process {process.name}")


class TestMooncakeDummyReal:
    """Mooncake dummy-real 部署模式的端到端测试

    模拟 Dummy + Real 跨进程 device memory 共享场景：
    - Dummy 分配 device memory，通过 ACL VMM shared handle 导出
    - Real 通过 VMM shared handle 导入 device memory，注册到 HIXL engine
    - 设备均分：Local 使用前半，Remote 使用后半
    - 每个 Real 分配一大块 host memory，所有 engine 注册整块

    测试 D2RD（Device to Remote Device）和 D2RH（Device to Remote Host）
    两种传输路径的 WRITE + READ 完整流程。
    """

    @staticmethod
    def test_normal_full_flow_d2rd_d2rh_read():
        """测试 dummy-real D2RD + D2RH WRITE + READ 完整流程

        场景说明：
        模拟 Mooncake dummy-real 部署模式，验证跨进程 device memory 共享和 HIXL 传输。
        4 个进程协作完成 D2RD（Device to Remote Device）和 D2RH（Device to Remote Host）
        两种传输路径的端到端测试。

        进程架构：
        - Remote Dummy: 分配 device memory，填充 REMOTE_BYTE，导出 VMM shared handle
        - Remote Real: 导入 VMM shared handle，创建 engines，注册 host + device memory
        - Local Dummy: 分配 device memory，填充 LOCAL_BYTE，导出 VMM shared handle
        - Local Real: 导入 VMM shared handle，创建 engines，执行 D2RD/D2RH WRITE + READ

        测试步骤：
        1. 启动 Remote Dummy，分配 device memory 并导出 VMM shared handle
        2. 启动 Remote Real，导入 VMM shared handle，创建 engines，注册内存
        3. 启动 Local Dummy，分配 device memory 并导出 VMM shared handle
        4. 启动 Local Real，导入 VMM shared handle，创建 engines，连接 Remote Real
        5. D2RD WRITE: Local Real 将 Local device memory WRITE 到 Remote device memory
        6. Remote Dummy 验证 device memory 包含 LOCAL_BYTE
        7. Local Real 清零 Local device memory
        8. D2RD READ: Local Real 从 Remote device memory READ 到 Local device memory
        9. Local Dummy 验证 device memory 包含 LOCAL_BYTE
        10. D2RH WRITE: Local Real 将 Local device memory WRITE 到 Remote host memory
        11. Remote Real 验证 host memory 包含 LOCAL_BYTE
        12. Local Real 清零 Local device memory
        13. D2RH READ: Local Real 从 Remote host memory READ 到 Local device memory
        14. Local Dummy 验证 device memory 包含 LOCAL_BYTE
        15. 清理所有进程资源
        """
        ctx = multiprocessing.get_context("spawn")
        r_dummy = r_real = l_dummy = l_real = None
        r_dummy_stop = r_real_stop = l_dummy_stop = None
        phase_results = ctx.Queue()
        worker_results = ctx.Queue()
        phase_acknowledgements = ctx.Queue()
        aborted = ctx.Event()
        errors = []
        try:
            # 启动 Remote Dummy 进程
            r_dummy_ready = ctx.Event()
            r_dummy_keys_q = ctx.Queue()
            r_dummy_stop = ctx.Event()
            d2rd_write_done = ctx.Event()
            r_dummy = ctx.Process(
                target=_remote_dummy_worker,
                args=(
                    r_dummy_ready,
                    r_dummy_keys_q,
                    d2rd_write_done,
                    r_dummy_stop,
                    phase_results,
                    worker_results,
                    aborted,
                ),
            )
            r_dummy.start()
            assert r_dummy_ready.wait(timeout=120), "Remote dummy startup timeout"
            r_dummy_keys = r_dummy_keys_q.get(timeout=10)
            assert r_dummy_keys, "Remote dummy failed to export VMM shared handles"

            # 启动 Remote Real 进程
            r_real_ready = ctx.Event()
            r_real_addrs_q = ctx.Queue()
            r_real_stop = ctx.Event()
            d2rh_write_done = ctx.Event()
            r_real = ctx.Process(
                target=_remote_real_worker,
                args=(
                    r_dummy_keys,
                    r_real_ready,
                    r_real_addrs_q,
                    d2rh_write_done,
                    r_real_stop,
                    phase_results,
                    worker_results,
                    aborted,
                ),
            )
            r_real.start()
            assert r_real_ready.wait(timeout=120), "Remote real startup timeout"
            remote_info = r_real_addrs_q.get(timeout=10)
            assert remote_info, "Remote real failed"

            time.sleep(STARTUP_SETTLE_DELAY_S)

            # 启动 Local Dummy 进程
            l_dummy_ready = ctx.Event()
            l_dummy_keys_q = ctx.Queue()
            l_dummy_stop = ctx.Event()
            d2rd_done = ctx.Event()
            d2rh_done = ctx.Event()
            l_dummy_args = LocalDummyArgs(
                ready_event=l_dummy_ready,
                key_queue=l_dummy_keys_q,
                d2rd_done_event=d2rd_done,
                d2rh_done_event=d2rh_done,
                phase_results=phase_results,
                worker_results=worker_results,
                aborted=aborted,
                stop_event=l_dummy_stop,
            )
            l_dummy = ctx.Process(
                target=_local_dummy_worker,
                args=(l_dummy_args,),
            )
            l_dummy.start()
            assert l_dummy_ready.wait(timeout=120), "Local dummy startup timeout"
            l_dummy_keys = l_dummy_keys_q.get(timeout=10)
            assert l_dummy_keys, "Local dummy failed to export VMM shared handles"

            # 启动 Local Real 进程
            l_real_args = LocalRealArgs(
                dummy_keys=l_dummy_keys,
                remote_info=remote_info,
                d2rd_write_done_event=d2rd_write_done,
                d2rd_done_event=d2rd_done,
                d2rh_write_done_event=d2rh_write_done,
                d2rh_done_event=d2rh_done,
                phase_results=phase_results,
                worker_results=worker_results,
                phase_acknowledgements=phase_acknowledgements,
            )
            l_real = ctx.Process(
                target=_local_real_worker,
                args=(l_real_args,),
            )
            l_real.start()
            _verify_phases(phase_results, phase_acknowledgements)
            l_real.join(timeout=180)
            assert l_real.exitcode == 0, (
                f"Local real failed with exit code {l_real.exitcode}"
            )
        except Exception as exc:
            errors.append(str(exc))
        finally:
            aborted.set()
            phase_acknowledgements.put((None, "Parent is stopping workers"))
            processes = (
                ("L-REAL", l_real, None),
                ("R-REAL", r_real, r_real_stop),
                ("L-DUMMY", l_dummy, l_dummy_stop),
                ("R-DUMMY", r_dummy, r_dummy_stop),
            )
            for role, process, stop in processes:
                try:
                    _stop_process(process, stop)
                except Exception as exc:
                    errors.append(f"{role}: {exc}")
                    # Do not release owners while an importer may still be alive.
                    if process is not None and process.is_alive():
                        break
            errors.extend(_check_worker_results(worker_results, processes))
        assert not errors, "VMM flow failed: " + "; ".join(errors)
