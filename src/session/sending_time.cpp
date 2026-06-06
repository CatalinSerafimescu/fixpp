// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/session/sending_time.cpp
//
// fixpp::session SendingTime(52) stamp/validate out-of-line bodies (005, T018 stub).
//
// Phase 2 ships STUBS that link cleanly (T018). Full implementations land
// per user story:
//   T022 (Phase 3 / US1): stamp_sending_time (outbound stamping via
//         effective_clock.now() + core::utc_time_to_fix_string, millis precision).
//   T055 (Phase 7 / US5): check_sending_time (inbound MaxLatency check;
//         Q3 disposition Reject → Logout → disconnect or Logon logout-with-error).
//
// Anchors: data-model.md E8; research D-3/D-5/D-8; contracts/sending_time.hpp;
// spec FR-011/FR-013; [FIX-SL §4.2.3].
#include <chrono>
#include <compare>  // NOLINT(misc-include-cleaner) — IWYU: strong_ordering/operator> via chrono spaceship
#include <expected>
#include <fixpp/session/sending_time.hpp>
#include <span>

#include "fixpp/core/error.hpp"
#include "fixpp/core/fix_time.hpp"

namespace fixpp::session {

[[nodiscard]] fixpp::core::expected_t<std::span<char>> stamp_sending_time(
    fixpp::core::utc_time_point now, fixpp::core::fix_time_precision prec,
    std::span<char> buf) noexcept {
    // Format effective_clock.now() as FIX UTCTimestamp at the configured precision.
    // prec is caller-supplied (non-defaulted, I-NST-6); callers pass
    // cfg_.sending_time_precision. Default millis = FIX 4.x parity (FR-003).
    return fixpp::core::utc_time_to_fix_string(now, prec, buf);
}

[[nodiscard]] fixpp::core::expected_t<void> check_sending_time(
    fixpp::core::utc_time_point inbound_sending_time, fixpp::core::utc_time_point effective_now,
    std::chrono::seconds max_latency) noexcept {
    // T055 (Phase 7 / US5): check |inbound_sending_time - effective_now| <= max_latency.
    // Compute the absolute difference (handles both future and past sending times).
    // Uses std::chrono arithmetic only — noexcept; no heap.
    // Returns ok if within range; returns session_sending_time_accuracy (slot 71)
    // on breach. The CALLER is responsible for the Q3 downstream action.
    auto delta = inbound_sending_time - effective_now;
    // Take absolute value of the duration without relying on std::abs overload.
    if (delta.count() < 0) {
        delta = -delta;
    }
    // Convert max_latency to the same duration type for comparison.
    auto max_dur = std::chrono::duration_cast<decltype(delta)>(max_latency);
    if (delta > max_dur) {
        return std::unexpected(fixpp::core::error::session_sending_time_accuracy);
    }
    return {};
}

}  // namespace fixpp::session
