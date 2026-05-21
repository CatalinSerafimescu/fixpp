// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/session/errors.hpp
//
// fixpp::session — helper wrappers over fixpp::core::error for the
// session_* variants (005-session-establishment-fsm, T006).
//
// These thin forwarding helpers construct the typed std::unexpected value
// so callers co_return make_<error>() without spelling out the core::error
// enum value numerically. No C++ type crosses the C ABI ([const §X.2]);
// the C-ABI coalescing surface (FIXPP_ERR_SESSION_*) is 2i-owned and NOT
// added here. Every function is constexpr noexcept so optimisers can
// constant-fold them (inline in a header, zero cost on the hot path).
//
// Anchor: data-model.md "Error mapping" §66..76; core/error.hpp slots 66..76;
// contracts/session_errors.hpp (shape oracle). FR-003/004/005/006/007/008/
// 013/017.
#pragma once

#include <fixpp/core/error.hpp>  // fixpp::core::error, expected_t

namespace fixpp::session {

// ── Refusal errors (session never reaches Active) ───────────────────────────

/// Inbound Logon refused: first-message-not-Logon, or generic validation
/// failure not better described by compid_mismatch / begin_string_unsupported.
/// Slots: core::error::session_invalid_logon (66). FR-003/004, [FIX-SL §4.3].
[[nodiscard]] inline constexpr
    fixpp::core::expected_t<void> make_session_invalid_logon() noexcept {
    return std::unexpected(fixpp::core::error::session_invalid_logon);
}

/// SenderCompID/TargetCompID mismatch against the configured counterparty.
/// Slot: core::error::session_compid_mismatch (67). FR-004, [FIX-SL §4.2.2].
[[nodiscard]] inline constexpr
    fixpp::core::expected_t<void> make_session_compid_mismatch() noexcept {
    return std::unexpected(fixpp::core::error::session_compid_mismatch);
}

/// BeginString(8) not in the supported version set for 005 (FIX.4.2/4.4).
/// Slot: core::error::session_begin_string_unsupported (68). FR-003, [FIX-SL §4.2.1].
[[nodiscard]] inline constexpr
    fixpp::core::expected_t<void> make_session_begin_string_unsupported() noexcept {
    return std::unexpected(fixpp::core::error::session_begin_string_unsupported);
}

// ── Session-fatal seqnum errors ─────────────────────────────────────────────

/// Inbound MsgSeqNum too-low (no PossDupFlag=Y): session-fatal Logout+disconnect.
/// Slot: core::error::session_seqnum_too_low (69). FR-008, [FIX-SL §4.1].
[[nodiscard]] inline constexpr
    fixpp::core::expected_t<void> make_session_seqnum_too_low() noexcept {
    return std::unexpected(fixpp::core::error::session_seqnum_too_low);
}

/// Inbound MsgSeqNum too-high: session-fatal; no ResendRequest (recovery deferred).
/// Slot: core::error::session_seqnum_gap_unrecoverable (70). FR-008/FR-001.
[[nodiscard]] inline constexpr
    fixpp::core::expected_t<void> make_session_seqnum_gap_unrecoverable() noexcept {
    return std::unexpected(fixpp::core::error::session_seqnum_gap_unrecoverable);
}

// ── Session-level Reject triggers ───────────────────────────────────────────

/// Inbound SendingTime(52) exceeds MaxLatency. Q3/FR-013, [FIX-SL §4.2.3].
/// Slot: core::error::session_sending_time_accuracy (71).
[[nodiscard]] inline constexpr
    fixpp::core::expected_t<void> make_session_sending_time_accuracy() noexcept {
    return std::unexpected(fixpp::core::error::session_sending_time_accuracy);
}

/// Message type not valid in the current FSM state. FR-007, [FIX-SL §4.5.4].
/// Slot: core::error::session_msg_type_invalid_for_state (72).
[[nodiscard]] inline constexpr
    fixpp::core::expected_t<void> make_session_msg_type_invalid_for_state() noexcept {
    return std::unexpected(fixpp::core::error::session_msg_type_invalid_for_state);
}

/// Deferred admin type (ResendRequest/SequenceReset) received: bounded reject.
/// Slot: core::error::session_admin_not_supported (75). FR-017, [FIX-SL §4.10].
[[nodiscard]] inline constexpr
    fixpp::core::expected_t<void> make_session_admin_not_supported() noexcept {
    return std::unexpected(fixpp::core::error::session_admin_not_supported);
}

// ── Lifecycle errors ─────────────────────────────────────────────────────────

/// Graceful-close (Logout exchange) timed out; force-disconnect. FR-005.
/// Slot: core::error::session_logout_timeout (73). D-8: 2 s default timeout.
[[nodiscard]] inline constexpr
    fixpp::core::expected_t<void> make_session_logout_timeout() noexcept {
    return std::unexpected(fixpp::core::error::session_logout_timeout);
}

/// Inbound silence exceeded test_request_threshold; peer unresponsive.
/// Slot: core::error::session_test_request_unanswered (74). FR-006, [FIX-SL §4.5.5].
[[nodiscard]] inline constexpr
    fixpp::core::expected_t<void> make_session_test_request_unanswered() noexcept {
    return std::unexpected(fixpp::core::error::session_test_request_unanswered);
}

/// Session-level config error (missing CompID, null begin_string, etc.)
/// Distinct from core::error::invalid_session_config (slot 53 — threading config).
/// Slot: core::error::session_invalid_config (76). [2d §4.5] N-P2-3.
[[nodiscard]] inline constexpr
    fixpp::core::expected_t<void> make_session_invalid_config() noexcept {
    return std::unexpected(fixpp::core::error::session_invalid_config);
}

}  // namespace fixpp::session
