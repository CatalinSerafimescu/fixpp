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
//   fixpp#509: check_sending_time compares without a signed subtraction.
//
// Anchors: data-model.md E8; research D-3/D-5/D-8; contracts/sending_time.hpp;
// spec FR-011/FR-013; [FIX-SL §4.2.3].
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <expected>
#include <fixpp/session/sending_time.hpp>
#include <limits>
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

// The distance between two time points is held as an unsigned nanosecond count.
static_assert(std::numeric_limits<fixpp::core::utc_time_point::rep>::digits <=
              std::numeric_limits<std::uint64_t>::digits);

[[nodiscard]] fixpp::core::expected_t<void> check_sending_time(
    fixpp::core::utc_time_point inbound_sending_time, fixpp::core::utc_time_point effective_now,
    std::chrono::seconds max_latency) noexcept {
    // T055 (Phase 7 / US5): check |inbound_sending_time - effective_now| <= max_latency.
    // Returns ok if within range; returns session_sending_time_accuracy (slot 71)
    // on breach. The CALLER is responsible for the Q3 downstream action.
    //
    // fixpp#509: the distance between two representable time points, and
    // max_latency in nanoseconds, can each exceed a signed nanosecond count. Both
    // are compared as unsigned magnitudes, so no step can overflow. noexcept; no heap.
    if (max_latency.count() < 0) {
        return std::unexpected(fixpp::core::error::session_sending_time_accuracy);
    }
    const auto in_ns = inbound_sending_time.time_since_epoch().count();
    const auto now_ns = effective_now.time_since_epoch().count();
    // Larger minus smaller, in modular unsigned arithmetic, is the exact distance.
    const auto hi_ns = std::max(in_ns, now_ns);
    const auto lo_ns = std::min(in_ns, now_ns);
    const std::uint64_t delta_ns =
        static_cast<std::uint64_t>(hi_ns) - static_cast<std::uint64_t>(lo_ns);
    // A max_latency whose nanosecond count does not fit is wider than any distance.
    constexpr std::uint64_t kNsPerSec = 1'000'000'000;
    constexpr std::uint64_t kMaxU64 = std::numeric_limits<std::uint64_t>::max();
    const auto max_s = static_cast<std::uint64_t>(max_latency.count());
    const std::uint64_t max_ns = max_s > kMaxU64 / kNsPerSec ? kMaxU64 : max_s * kNsPerSec;
    if (delta_ns > max_ns) {
        return std::unexpected(fixpp::core::error::session_sending_time_accuracy);
    }
    return {};
}

}  // namespace fixpp::session
