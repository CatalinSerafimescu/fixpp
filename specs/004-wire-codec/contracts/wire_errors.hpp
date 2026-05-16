// SPDX-License-Identifier: AGPL-3.0-or-later
// CONTRACT SHAPE ORACLE — extract of [2b §6.7]. Authority: 2b-wire.md v0.2.
// The 13 wire_* variants are APPENDED to fixpp::core::error (core/error.hpp),
// non-renumbering ([const §X.4]). Current max occupied slot = 29
// (dict_reify_wire_body_not_ready). Wire appends at slots 30..42; the exact
// base is re-confirmed against core/error.hpp at /implement before the edit.
#pragma once

namespace fixpp::core {
// enum class error : ... {  /* ... existing 1..29 unchanged ... */
//   wire_frame_too_large        = 30,  // [2b §6.1.3]
//   wire_invalid_body_length    = 31,  // [2b §6.1.3]
//   wire_checksum_mismatch      = 32,  // [2b §6.1.5]
//   wire_framing_resync         = 33,  // [2b §6.1.2]
//   wire_invalid_field_format   = 34,  // [2b §6.2]
//   wire_offset_table_full      = 35,  // [2b §1.2/§4.4]
//   wire_group_too_large        = 36,  // [2b §1.2/§4.4]
//   wire_tag_out_of_range       = 37,  // [2b §1.2]
//   wire_required_field_missing = 38,  // [2b §6.5.4]
//   wire_header_out_of_order    = 39,  // [2b §6.5.1]
//   wire_field_value_out_of_range = 40,// [2b §6.5.3]
//   wire_field_value_truncated  = 41,  // from 2a §6.4, surfaced unchanged
//   wire_unexpected_tag         = 42,  // [2b §6.5.5] SessionRejectReason=2
// };
// DELETED vs v0.1: wire_tag_count_exceeded (dropped distinct-tag cap, RC#1).
// 2i C-ABI coalescing target (NOT implemented here): INVALID_FRAME /
// LIMIT_EXCEEDED / CONFORMANCE / reuse DECIMAL_PRECISION_LOSS.
}  // namespace fixpp::core
