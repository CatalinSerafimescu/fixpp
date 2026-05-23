// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/session/sending_time.hpp
//
// fixpp::session SendingTime(52) stamp/validate declarations (005, T017).
//
// OUTBOUND: every emitted message carries SendingTime(52) =
//   effective_clock.now() formatted via core::utc_time_to_fix_string
//   ([2d §6.6] now() for wire timestamps). Round-trips losslessly (I-6).
//
// INBOUND (S-019, FR-013, [FIX-SL §4.2.3]): compare SendingTime(52) to
//   effective_clock.now(); if |divergence| > MaxLatency (default 120 s, D-8):
//     - established session: Reject(SessionRejectReason=10, ref tag 52) →
//       Logout → disconnect.
//     - offending message IS the Logon: logout-with-error, no standalone Reject.
//   Surfaced as session_sending_time_accuracy (slot 71). Q3 (Clarification).
//
// Bodies land per user story: T022 (Phase 3 / US1) outbound stamping;
// T055 (Phase 7 / US5) inbound MaxLatency check.
//
// Anchors: data-model.md E8; research D-3/D-5/D-8; contracts/sending_time.hpp;
// spec FR-011/FR-013; [FIX-SL §4.2.3].
//
// No asio::awaitable. No std::mutex. ([const §XV.9] grep gate: safe.)
#pragma once

#include <chrono>
#include <fixpp/core/error.hpp>
#include <fixpp/core/fix_time.hpp>
#include <span>

namespace fixpp::session {

// ── Outbound SendingTime stamp ────────────────────────────────────────────────

// Format the current utc_time_point into a SendingTime(52) value using the
// FIX UTCTimestamp grammar at millis precision (FIX 4.x default, D-3).
// Fills `buf` (caller must supply ≥ 19 bytes). Returns the written view.
// noexcept; no heap (uses the caller-supplied buffer, [const §VIII.5]).
// PLACEHOLDER — outbound stamping wired T022 (Phase 3 / US1).
[[nodiscard]] fixpp::core::expected_t<std::span<char>> stamp_sending_time(
    fixpp::core::utc_time_point now, std::span<char> buf) noexcept;

// ── Inbound SendingTime check (MaxLatency) ────────────────────────────────────

// Check that |inbound_sending_time - effective_now| <= max_latency.
// Returns ok if within range; returns session_sending_time_accuracy if
// the divergence exceeds max_latency.
// The CALLER is responsible for the Q3 downstream action (Reject → Logout →
// disconnect, or Logon-path → logout-with-error) — this function only
// evaluates the time condition. noexcept; no heap.
// PLACEHOLDER — inbound check wired T055 (Phase 7 / US5).
[[nodiscard]] fixpp::core::expected_t<void> check_sending_time(
    fixpp::core::utc_time_point inbound_sending_time, fixpp::core::utc_time_point effective_now,
    std::chrono::seconds max_latency) noexcept;

}  // namespace fixpp::session
