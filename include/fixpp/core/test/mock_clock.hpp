// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/core/test/mock_clock.hpp  (PUBLIC test header)
//
// fixpp::core::mock_clock — deterministic test Clock ([2d §4.3] / E3 / D-10).
// Pimpl over an OPAQUE mutable-state object so the internal synchronisation
// (a std::mutex) stays OUT of this asio::awaitable-including header
// ([const §XI.3]). advance()/step_to()/set_utc_skew() walk a per-deadline
// ordered map and wake every awaiter with deadline <= new steady_now,
// deterministically across runs (two identically-seeded instances + the same
// advance sequence ⇒ identical now/steady_now/wake-up order — seam 1).
// Test-only; may outlive the engine when held by a fixture shared_ptr
// (cancel_sleeps() has no live waiters post-teardown — seam 14 variant).
#pragma once

#include <asio/any_io_executor.hpp>
#include <chrono>
#include <fixpp/core/clock.hpp>
#include <memory>

namespace fixpp::core {

class mock_clock final : public Clock {
public:
    // Seeded construction ([2d §4.3]): independent initial UTC + steady +
    // the engine executor. Both clocks step independently.
    mock_clock(utc_time_point initial_utc, steady_time_point initial_steady,
               asio::any_io_executor exec);

    mock_clock(const mock_clock&) = delete;
    mock_clock& operator=(const mock_clock&) = delete;
    mock_clock(mock_clock&&) = delete;
    mock_clock& operator=(mock_clock&&) = delete;
    ~mock_clock() override;

    [[nodiscard]] utc_time_point now() const noexcept override;
    [[nodiscard]] steady_time_point steady_now() const noexcept override;
    // NO expected_t — cancellation via asio::error::operation_aborted
    // ([2d §4.1]/§4.3): the awaiter is removed from the per-deadline list and
    // completes with operation_aborted.
    [[nodiscard]] asio::awaitable<void> sleep_until(steady_time_point deadline) override;
    void cancel_sleeps() noexcept override;

    // ── Test-only API ([2d §4.3]) — not part of the Clock interface ──────
    // Step monotonic time by delta; wakes any sleep_until awaiter whose
    // deadline <= new steady_now. now() moves by the same delta unless a
    // wall-clock skew was set via set_utc_skew().
    void advance(std::chrono::nanoseconds delta) noexcept;
    // Force monotonic time to point (fast-forward to next heartbeat).
    void step_to(steady_time_point point) noexcept;
    // Wall-clock-only delta that does NOT affect steady_now (NTP-step /
    // SendingTime-threshold simulation — US2 AC-3 / seam 1).
    void set_utc_skew(std::chrono::nanoseconds skew) noexcept;

private:
    struct state;  // opaque mutable-state object (in .cpp)
    std::unique_ptr<state> impl_;
};

}  // namespace fixpp::core
