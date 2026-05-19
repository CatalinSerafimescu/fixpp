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
// same discipline as mock_clock. The per-Session* reusable steady_timer slot
// pool keyed by Session* (D-8) is layered on by US6 (T055); T028 ships a
// correct per-sleep timer.
#pragma once

#include <memory>

#include <asio/any_io_executor.hpp>

#include <fixpp/core/clock.hpp>

namespace fixpp::core {

class system_clock_source final : public Clock {
public:
    // Construct with the ENGINE-LEVEL executor (EngineConfig::executor) — NOT
    // the session strand ([2d §4.2] note).
    explicit system_clock_source(asio::any_io_executor exec) noexcept;

    system_clock_source(const system_clock_source&)            = delete;
    system_clock_source& operator=(const system_clock_source&) = delete;
    system_clock_source(system_clock_source&&)                 = delete;
    system_clock_source& operator=(system_clock_source&&)      = delete;
    ~system_clock_source() override;

    [[nodiscard]] utc_time_point    now() const noexcept override;
    [[nodiscard]] steady_time_point steady_now() const noexcept override;
    // NO expected_t — cancellation via asio::error::operation_aborted
    // ([2d §4.1]/§4.2).
    [[nodiscard]] asio::awaitable<void> sleep_until(steady_time_point) override;
    void cancel_sleeps() noexcept override;

private:
    struct state;                     // opaque (intrusive list + mutex; in .cpp)
    std::unique_ptr<state> impl_;
};

}  // namespace fixpp::core
