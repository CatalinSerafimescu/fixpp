// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/core/clock.hpp
//
// fixpp::core::Clock — the pluggable timing seam (NFR-015). EXACTLY 4
// pure-virtual methods ([const §XIV.2] 4/5, within the ≤5 cap; I-01).
// Instances are shared_ptr-owned by EngineConfig (or a per-SessionConfig
// clock_override) and MUST outlive every session that references them.
// [2d §4.1] / §6.6. Realizes specs/007-threading-clock/contracts/clock.hpp.
#pragma once

#include <asio/awaitable.hpp>
#include <chrono>

namespace fixpp::session {
class Session;  // opaque key for forget_session (defined in fixpp/session/session.hpp)
}

namespace fixpp::core {

using utc_time_point = std::chrono::time_point<std::chrono::system_clock>;
using steady_time_point = std::chrono::time_point<std::chrono::steady_clock>;

class Clock {
public:
    Clock() = default;
    Clock(const Clock&) = delete;
    Clock& operator=(const Clock&) = delete;
    Clock(Clock&&) = delete;
    Clock& operator=(Clock&&) = delete;
    virtual ~Clock() = default;

    // Wall-clock UTC; NOT promised monotonic (C-P2-5 / FR-004 / I-02) — used
    // only for wire-formatted and log/OTel timestamps.
    [[nodiscard]] virtual utc_time_point now() const noexcept = 0;

    // Monotonic; the SOLE source for elapsed / heartbeat / SendingTime-delta /
    // S-035 measurements (FR-004 / I-02).
    [[nodiscard]] virtual steady_time_point steady_now() const noexcept = 0;

    // Completes on the awaiter's bound executor; honours the awaiter's
    // cancellation slot. NO expected_t<T> here ([2d §4.1] note):
    // cancellation is reported via asio::error::operation_aborted (the
    // standard ASIO path, [const §XI.2]), NOT a returned expected_t. The
    // error::clock_sleeps_cancelled enum value is the OPTIONAL expected_t
    // projection for callers that prefer it ([2d §6.7]), not this return type.
    [[nodiscard]] virtual asio::awaitable<void> sleep_until(steady_time_point deadline) = 0;

    // Signals every in-flight sleep_until awaiter's cancellation slot.
    // Idempotent; safe to call concurrently AND re-entrantly (incl. from
    // inside a sleep_until completion handler) (FR-003 / E1).
    virtual void cancel_sleeps() noexcept = 0;

    // Optional hook: notify the clock that a session is about to be destroyed
    // so per-session state (e.g. system_clock_source's reusable timer slot
    // pool, T055 / D-8) can be released BEFORE the session's arena memory is
    // reclaimed. Idempotent; safe to call from Session::~Session() (which is
    // the canonical call site). Default no-op for clocks without per-session
    // state (mock_clock). Total virtual count = 5 (≤5 per I-01 / [const §XIV.2]
    // — 4 pure-virtual + this 1 default-implemented hook).
    virtual void forget_session(fixpp::session::Session* /*session*/) noexcept {}
};

}  // namespace fixpp::core
