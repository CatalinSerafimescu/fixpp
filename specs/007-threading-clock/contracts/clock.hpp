// SPDX-License-Identifier: AGPL-3.0-or-later
// SHAPE ORACLE — NOT the build header. Pins the [2d §4.1] surface for Gate A
// fidelity. The real header is include/fixpp/core/clock.hpp. asio::awaitable
// is forward-declared the same way the build header will. NO expected_t fwd —
// the [2d §4.1] frozen block does not use expected_t (see sleep_until note).
#pragma once
#include <chrono>
#include <asio/awaitable.hpp>
#include <asio/cancellation_type.hpp>

namespace fixpp::core {

using utc_time_point    = std::chrono::time_point<std::chrono::system_clock>;
using steady_time_point = std::chrono::time_point<std::chrono::steady_clock>;

// EXACTLY 4 pure-virtual ([const §XIV.2] 4/5, within cap). Owned by EngineConfig
// (or per-SessionConfig override) via shared_ptr; must outlive every session
// that references it. [2d §4.1] / §6.6.
class Clock {
public:
    virtual ~Clock() = default;

    [[nodiscard]] virtual utc_time_point    now() const noexcept = 0;        // wall UTC; NOT monotonic (C-P2-5)
    [[nodiscard]] virtual steady_time_point steady_now() const noexcept = 0; // monotonic; only elapsed source

    // Completes on the awaiter's bound executor; honours the awaiter's
    // cancellation slot. NO expected_t<T> here ([2d §4.1] note — "No
    // expected_t<T> here"): cancellation is reported through
    // asio::error::operation_aborted (the standard ASIO path, [const §XI.2]),
    // NOT a returned expected_t. The error::clock_sleeps_cancelled enum value
    // is the OPTIONAL expected_t projection for callers that prefer it
    // ([2d §6.7]) — it is NOT the interface return type. [[nodiscard]] by
    // ASIO convention; re-marked for clarity per [2d §4.1].
    [[nodiscard]] virtual asio::awaitable<void> sleep_until(steady_time_point) = 0;

    // Signals every in-flight sleep_until awaiter's slot. Idempotent; safe to
    // call concurrently and re-entrantly (incl. from a sleep_until completion).
    virtual void cancel_sleeps() noexcept = 0;
};

}  // namespace fixpp::core
