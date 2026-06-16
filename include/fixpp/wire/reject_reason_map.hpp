// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/wire/reject_reason_map.hpp
//
// 041-validation-gate-wiring T011 — constexpr wire_* → SessionRejectReason(373)
// mapping (data-model E-4 / R-3). Called from the Session validate gate
// (on_inbound_frame) to map the error returned by
// dictionary_driven_validator::validate() to the FIX SessionRejectReason value
// for the Reject(35=3, 373=…) frame.
//
// Anchors: specs/041-validation-gate-wiring/data-model.md E-4;
//          research.md R-3; [FIX50SP2 §2.1] (SessionRejectReason taxonomy).
//
// Mapping (faithful to what validate() actually emits):
//   wire_header_out_of_order(39)   → 14  (tag out of required order)
//   wire_unexpected_tag(42)        →  2  (tag not defined for msg type)
//   wire_required_field_missing(38)→  1  (required tag missing; also group-
//                                         structure failures, Phase-1 — the
//                                         validator surfaces group errors as
//                                         wire_required_field_missing)
//   wire_field_value_out_of_range(40)→ 5 (value not type-conformant;
//                                         type-arm only in Phase-1 since the
//                                         enum arm is dead — enum_valid→true)
//   wire_field_value_truncated(41) →  6  (Float/decimal precision-loss ONLY;
//                                         fired only on decimal_precision_loss
//                                         remap, NOT a generic bad-format)
//
// Default arm: returns 3 (other, invalid tag) for any slot outside the 5
// validator-emitted slots. Post-T009a validate() emits ONLY wire_* slots, so
// this arm is structurally unreachable in production — fail-closed, not UB.
//
// Header-only, no new transitive includes (plain <cstdint>).
// [const §XV.9]: no std::mutex or std::shared_mutex.
#pragma once

#include <cstdint>
#include <fixpp/core/error.hpp>  // core::error enum

namespace fixpp::wire {

/// Map a core::error wire_* slot from dictionary_driven_validator::validate()
/// to the corresponding FIX SessionRejectReason(373) integer value.
///
/// Returns the SessionRejectReason int (1/2/5/6/14), or 3 (other/invalid tag)
/// for any error outside the five validator-emitted wire_* slots.
///
/// [[nodiscard]]: callers always use the result.
[[nodiscard]] constexpr int wire_error_to_session_reject_reason(
    fixpp::core::error e) noexcept {
    using fixpp::core::error;
    switch (e) {
        case error::wire_required_field_missing:
            // Reason 1: required tag missing (and group-structure failures in
            // Phase-1 — the validator surfaces group errors as
            // wire_required_field_missing; no distinct group reason).
            return 1;
        case error::wire_unexpected_tag:
            // Reason 2: tag not defined for this message type.
            return 2;
        case error::wire_field_value_out_of_range:
            // Reason 5: value is incorrect (not type-conformant — type arm;
            // enum arm is dead Phase-1).
            return 5;
        case error::wire_field_value_truncated:
            // Reason 6: incorrect data format (Float/decimal precision-loss
            // ONLY — only decimal_precision_loss remaps to this slot).
            return 6;
        case error::wire_header_out_of_order:
            // Reason 14: tag out of required order.
            return 14;
        default:
            // Post-T009a: validate() emits ONLY the 5 wire_* slots above.
            // This arm is structurally unreachable in production. Fail-closed
            // to reason 3 (other) rather than UB.
            return 3;
    }
}

}  // namespace fixpp::wire
