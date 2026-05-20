// SPDX-License-Identifier: AGPL-3.0-or-later
// SHAPE ORACLE — NOT the build header. [2d §4.2] default Clock impl.
#pragma once
#include "clock.hpp"
#include <asio/any_io_executor.hpp>

namespace fixpp::core {

// Default impl: now()→system_clock, steady_now()→steady_clock,
// sleep_until()→ASIO steady_timer drawn from a per-session reusable slot
// pool KEYED BY Session* (round 2 root cause #1), allocated ONCE from
// session_arena (D-8 / N-P1-1 — zero per-cycle heap on the heartbeat path).
// cancel_sleeps() walks an intrusive in-flight-awaiter list (O(N);
// v1.0 worst case O(2 × sessions)). now()/steady_now() are thread-safe,
// non-blocking, noexcept. ~system_clock_source drains the intrusive list
// (no live waiters in a well-formed shutdown — D-9 / root cause #5).
class system_clock_source final : public Clock {
public:
    // Construct with the ENGINE-LEVEL executor (EngineConfig::executor) —
    // NOT the session strand ([2d §4.2] note). The per-session reusable
    // timer slot pool keyed by Session* is allocated lazily at first
    // sleep_until and reset (not destroyed) between cycles.
    explicit system_clock_source(asio::any_io_executor exec) noexcept;
    ~system_clock_source() override;

    [[nodiscard]] utc_time_point    now() const noexcept override;
    [[nodiscard]] steady_time_point steady_now() const noexcept override;
    // NO expected_t — cancellation via asio::error::operation_aborted
    // ([2d §4.1] / §4.2).
    [[nodiscard]] asio::awaitable<void> sleep_until(steady_time_point) override;
    void cancel_sleeps() noexcept override;
};

}  // namespace fixpp::core
