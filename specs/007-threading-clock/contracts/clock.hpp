// SPDX-License-Identifier: AGPL-3.0-or-later
// SHAPE ORACLE — NOT the build header. Pins the [2d §4.1] surface for Gate A
// fidelity. The real header is include/fixpp/core/clock.hpp. expected_t /
// asio::awaitable are forward-declared the same way the build header will.
#pragma once
#include <chrono>
#include <asio/awaitable.hpp>
#include <asio/cancellation_type.hpp>

namespace fixpp::core {

template <class T> class expected_t;  // fwd — fixpp::core::expected_t

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
    // cancellation slot; cancelled completion → error::clock_sleeps_cancelled
    // (operation_aborted at the awaitable level).
    virtual asio::awaitable<expected_t<void>> sleep_until(steady_time_point) = 0;

    // Signals every in-flight sleep_until awaiter's slot. Idempotent; safe to
    // call concurrently and re-entrantly (incl. from a sleep_until completion).
    virtual void cancel_sleeps() noexcept = 0;
};

}  // namespace fixpp::core
