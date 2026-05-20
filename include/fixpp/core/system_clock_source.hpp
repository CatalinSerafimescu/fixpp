// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/core/system_clock_source.hpp
//
// fixpp::core::system_clock_source — default Clock impl ([2d §4.2] / E2 /
// FR-002/FR-003). now()→system_clock, steady_now()→steady_clock,
// sleep_until()→ASIO steady_timer; cancel_sleeps() walks an intrusive
// in-flight-awaiter list (O(N); v1.0 worst case O(2×sessions)) and signals
// each waiter. now()/steady_now() are thread-safe, non-blocking, noexcept.
// ~system_clock_source drains the list (no live waiters in a well-formed
// shutdown — D-9 / root cause #5).
//
// The in-flight list + its std::mutex live behind a pimpl (in the .cpp) so
// they stay OUT of this asio::awaitable-including header ([const §XI.3]),
// same discipline as mock_clock.
//
// sleep_until has TWO paths (T055 / D-8 — shipped, not future):
//   • Session-scoped: the awaiter's bound executor IS a session_executor;
//     a per-session steady_timer slot is allocated ONCE from session_arena
//     at the first sleep, keyed by Session*, and re-armed via expires_at
//     thereafter (ZERO global heap in steady state).
//   • Engine-scoped fallback: no session_executor on the awaiter (e.g.
//     close-timeout machinery, third-party-clock conformance harness) →
//     per-call shared_ptr<steady_timer> from the global heap (acceptable
//     cold path; not a §6.2 hot path).
// Per-timer strand over engine_exec on both paths (D-19 — asio timers are
// not thread-safe; cancel_sleeps posts cancel() onto the timer's strand).
// forget_session() (called from Session::~Session) releases a session's
// slot BEFORE its arena is reclaimed.
#pragma once

#include <memory>

#include <asio/any_io_executor.hpp>

#include <fixpp/core/clock.hpp>

namespace fixpp::core {

class system_clock_source final : public Clock {
public:
    // Construct with the ENGINE-LEVEL executor (EngineConfig::executor) — NOT
    // the session strand ([2d §4.2] note). NOT noexcept: allocates the pimpl
    // state.
    explicit system_clock_source(asio::any_io_executor exec);

    system_clock_source(const system_clock_source&)            = delete;
    system_clock_source& operator=(const system_clock_source&) = delete;
    system_clock_source(system_clock_source&&)                 = delete;
    system_clock_source& operator=(system_clock_source&&)      = delete;
    ~system_clock_source() override;

    [[nodiscard]] utc_time_point    now() const noexcept override;
    [[nodiscard]] steady_time_point steady_now() const noexcept override;
    // NO expected_t — cancellation via asio::error::operation_aborted
    // ([2d §4.1]/§4.2).
    [[nodiscard]] asio::awaitable<void> sleep_until(steady_time_point deadline) override;
    void cancel_sleeps() noexcept override;

    // Release per-session timer-slot state under impl_->m, BEFORE the
    // session's arena memory is reclaimed by ~Session. Idempotent; called
    // from Session::~Session (D-23). No-op if this session never slept.
    void forget_session(fixpp::session::Session* session) noexcept override;

private:
    struct state;                     // opaque (intrusive list + mutex; in .cpp)
    std::unique_ptr<state> impl_;
};

}  // namespace fixpp::core
