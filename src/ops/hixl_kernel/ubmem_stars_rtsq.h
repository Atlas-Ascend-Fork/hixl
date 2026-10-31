/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_OPS_HIXL_KERNEL_UBMEM_STARS_RTSQ_H_
#define CANN_HIXL_SRC_OPS_HIXL_KERNEL_UBMEM_STARS_RTSQ_H_

#include <cstdint>

namespace hixl {

// STARS SQE layouts for the entries the UB_MEM AICPU kernel writes directly into a worker
// RTSQ. A2/A3 (SocType Gen1/Gen2) use the stars v1 layout below, A5 uses stars v2 further
// down; the host selects the layout through UbMemStarsSqeFormat. The minimal HIXL-owned forms
// mirror HComm's AICPU SQE builders and contain no HComm/runtime ABI.

// ===== stars v1 (A2/A3) =====
constexpr uint8_t kUbMemA3SdmaSqeType = 11U;
constexpr uint8_t kUbMemA3SdmaKernelCredit = 240U;
// Matches HComm LinkType::LINK_ONCHIP for same-device / LocalCopy SDMA.
constexpr uint8_t kUbMemA3SdmaLinkOnChip = 0U;
constexpr uint8_t kUbMemA3SdmaQosDefault = 6U;
constexpr uint8_t kUbMemA3NotifyRecordSqeType = 6U;
constexpr uint8_t kUbMemA3NotifyKernelCredit = 254U;
constexpr uint32_t kUbMemA3NotifyIdLimit = 1U << 13U;
constexpr uint32_t kUbMemA3RtsqEntryBytes = 64U;

#pragma pack(push, 1)
// Keep the public STARS SQE header bit layout locally so the AICPU package does
// not depend on runtime_headers / rt_external_stars_define.h.
struct UbMemA3StarsSqeHeader {
  uint8_t type : 6;
  uint8_t l1_lock : 1;
  uint8_t l1_unlock : 1;
  uint8_t ie : 2;
  uint8_t pre_p : 2;
  uint8_t post_p : 2;
  uint8_t wr_cqe : 1;
  uint8_t reserved : 1;
  uint16_t block_dim;
  uint16_t rt_stream_id;
  uint16_t task_id;
};
static_assert(sizeof(UbMemA3StarsSqeHeader) == 8U, "A3 STARS SQE header must stay 8 bytes.");

struct UbMemA3SdmaSqe {
  UbMemA3StarsSqeHeader header;
  uint32_t reserved0;
  uint16_t reserved1;
  uint8_t kernel_credit;
  uint8_t ptr_mode : 1;
  uint8_t reserved2 : 7;
  uint32_t opcode : 8;
  uint32_t ie2 : 1;
  uint32_t sssv : 1;
  uint32_t dssv : 1;
  uint32_t sns : 1;
  uint32_t dns : 1;
  uint32_t qos : 4;
  uint32_t sro : 1;
  uint32_t dro : 1;
  uint32_t partid : 8;
  uint32_t mpam : 1;
  uint32_t reserved3 : 4;
  uint16_t src_stream_id;
  uint16_t src_substream_id;
  uint16_t dst_stream_id;
  uint16_t dst_substream_id;
  uint32_t length;
  uint32_t src_addr_low;
  uint32_t src_addr_high;
  uint32_t dst_addr_low;
  uint32_t dst_addr_high;
  uint8_t link_type;
  uint8_t reserved4[3];
  uint32_t reserved5[3];
};

// A3 NotifyRecord SQE layout mirrored from HComm's rtStarsNotifySqeV1_t.
// The whole entry is zero-initialized before these fields are assigned, so
// wr_cqe and all interrupt/order bits remain disabled.
struct UbMemA3NotifySqe {
  UbMemA3StarsSqeHeader header;
  uint32_t notify_id : 13;
  uint32_t reserved0 : 19;
  uint16_t reserved1;
  uint8_t kernel_credit;
  uint8_t reserved2;
  uint32_t timeout;
  uint32_t reserved3[11];
};
#pragma pack(pop)

static_assert(sizeof(UbMemA3SdmaSqe) == kUbMemA3RtsqEntryBytes, "UbMem A3 SDMA SQE ABI unexpectedly changed.");
static_assert(sizeof(UbMemA3NotifySqe) == kUbMemA3RtsqEntryBytes,
              "UbMem A3 NotifyRecord SQE ABI unexpectedly changed.");

// ===== stars v2 (A5) =====
// Stars v2 keeps the 64-byte entry but moves several words: word 5 carries sqe_id/mapam_part_id/qos,
// source stream ids occupy word 6, destination stream ids word 7 and the transfer length word 12,
// while stars v1 keeps the length in word 7. This mirrors HComm's A5 AICPU SQE builders
// (Rt91095StarsMemcpySqe / Rt91095StarsNotifySqe).

constexpr uint8_t kUbMemA5SdmaSqeType = 11U;
constexpr uint8_t kUbMemA5NotifyRecordSqeType = 6U;
// Same-device SDMA default credit on A5 (runtime maps stars default 254 down to 253 for the chip).
constexpr uint8_t kUbMemA5SdmaKernelCredit = 253U;
constexpr uint8_t kUbMemA5NotifyKernelCredit = 253U;
constexpr uint8_t kUbMemA5SdmaQosDefault = 6U;
constexpr uint8_t kUbMemA5NotifySubTypeSingleRecord = 0U;
// The A5 notify SQE carries a 17-bit notify id.
constexpr uint32_t kUbMemA5NotifyIdLimit = 1U << 17U;
constexpr uint32_t kUbMemA5RtsqEntryBytes = 64U;

#pragma pack(push, 1)
// Keep the A5 STARS SQE header bit layout locally so the AICPU package does not depend on
// runtime_headers. Note that the 32-bit word at offset 4 is a single task id on stars v2; the
// field names below only mirror the legacy hardware split.
struct UbMemA5StarsSqeHeader {
  uint8_t type : 6;
  uint8_t lock : 1;
  uint8_t unlock : 1;
  uint8_t ie : 1;
  uint8_t pre_p : 1;
  uint8_t post_p : 1;
  uint8_t wr_cqe : 1;
  uint8_t ptr_mode : 1;
  uint8_t rtt_mode : 1;
  uint8_t head_update : 1;
  uint8_t sqe_prof : 1;
  uint16_t block_dim;
  uint16_t rt_stream_id;
  uint16_t task_id;
};
static_assert(sizeof(UbMemA5StarsSqeHeader) == 8U, "A5 STARS SQE header must stay 8 bytes.");

struct UbMemA5SdmaSqe {
  UbMemA5StarsSqeHeader header;
  uint32_t reserved0;
  uint16_t reserved1;
  uint8_t kernel_credit;
  uint8_t reserved2;
  uint32_t opcode : 8;
  uint32_t sssv : 1;
  uint32_t dssv : 1;
  uint32_t sns : 1;
  uint32_t dns : 1;
  uint32_t sro : 1;
  uint32_t dro : 1;
  uint32_t stride : 2;
  uint32_t ie2 : 1;
  uint32_t comp_en : 1;
  uint32_t reserved3 : 14;
  uint16_t sqe_id;
  uint8_t mapam_part_id;
  uint8_t mpamns : 1;
  uint8_t pmg : 2;
  uint8_t qos : 4;
  uint8_t d2d_offset_flag : 1;
  uint16_t src_stream_id;
  uint16_t src_substream_id;
  uint16_t dst_stream_id;
  uint16_t dst_substream_id;
  uint32_t src_addr_low;
  uint32_t src_addr_high;
  uint32_t dst_addr_low;
  uint32_t dst_addr_high;
  uint32_t length;
  uint32_t src_offset_low;
  uint32_t dst_offset_low;
  uint16_t src_offset_high;
  uint16_t dst_offset_high;
};

// A5 NotifyRecord SQE layout mirrored from Rt91095StarsNotifySqe. The whole entry is
// zero-initialized before these fields are assigned, so the count/wait mode bits stay disabled.
struct UbMemA5NotifySqe {
  UbMemA5StarsSqeHeader header;
  uint32_t notify_id : 17;
  uint32_t reserved0 : 13;
  uint32_t cnt_flag : 1;
  uint32_t clr_flag : 1;
  uint16_t sub_type;
  uint8_t kernel_credit;
  uint8_t reserved1 : 5;
  uint8_t sqe_length : 3;
  uint32_t cnt_value;
  uint32_t wait_mode_bit : 2;
  uint32_t record_mode_bit : 3;
  uint32_t bitmap : 1;
  uint32_t reserved2 : 26;
  uint32_t timeout;
  uint32_t exe_result;
  uint32_t reserved3[8];
};
#pragma pack(pop)

// HIXL only fills the fields the same-device copy path consumes. In the A5 SDMA SQE, sqe_id,
// mapam_part_id, mpamns/pmg (qos itself is set to 6), d2d_offset_flag, src/dst_substream_id,
// src/dst_offset_*, stride and comp_en stay zero; in the A5 NotifyRecord SQE, sqe_length stays
// zero. HIXL does not use the AICPU DFX map or strided/compression modes, but keep this field list
// in mind when re-verifying the layout against HComm/runtime on real A5 hardware.
static_assert(sizeof(UbMemA5SdmaSqe) == kUbMemA5RtsqEntryBytes, "UbMem A5 SDMA SQE ABI unexpectedly changed.");
static_assert(sizeof(UbMemA5NotifySqe) == kUbMemA5RtsqEntryBytes,
              "UbMem A5 NotifyRecord SQE ABI unexpectedly changed.");

}  // namespace hixl

#endif  // CANN_HIXL_SRC_OPS_HIXL_KERNEL_UBMEM_STARS_RTSQ_H_
