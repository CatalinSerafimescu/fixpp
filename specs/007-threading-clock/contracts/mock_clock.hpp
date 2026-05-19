// SPDX-License-Identifier: AGPL-3.0-or-later
// SHAPE ORACLE — NOT the build header. [2d §4.3] deterministic test Clock.
// Real header: include/fixpp/core/test/mock_clock.hpp (PUBLIC test header).
#pragma once
#include "clock.hpp"
#include <asio/any_io_executor.hpp>
#include <chrono>
#include <memory>

namespace fixpp::core {

// Pimpl over an OPAQUE mutable-state object (D-10 / [const §XI.3]: no
// std::mutex in a header that includes asio::awaitable<...> — the sync is
// hidden behind the pimpl). advance(delta) walks a per-deadline ordered map
// and wakes every awaiter with deadline <= new steady_now, DETERMINISTICALLY
// across runs (two identically-seeded instances + same advance sequence ⇒
// identical now/steady_now/wake-up order — seam 1). Test-only; may outlive
// the engine when held by a fixture shared_ptr.
class mock_clock final : public Clock {
public:
    // Seeded construction ([2d §4.3]): independent initial UTC + steady
    // wall-time + the engine executor. Both clocks step independently via
    // advance()/step_to()/set_utc_skew() so a test can simulate wall-clock
    // skew vs monotonic time (NTP step) deterministically.
    mock_clock(utc_time_point initial_utc,
               steady_time_point initial_steady,
               asio::any_io_executor exec);
    ~mock_clock() override;

    [[nodiscard]] utc_time_point    now() const noexcept override;
    [[nodiscard]] steady_time_point steady_now() const noexcept override;
    // NO expected_t — cancellation via asio::error::operation_aborted
    // ([2d §4.1] / §4.3): the awaiter is removed from the per-deadline list
    // and completes with operation_aborted.
    [[nodiscard]] asio::awaitable<void> sleep_until(steady_time_point) override;
    void cancel_sleeps() noexcept override;

    // ── Test-only API ([2d §4.3]) — not part of the Clock interface ──────
    // Step monotonic time by delta; wakes any sleep_until awaiter whose
    // deadline <= new steady_now. Wall-clock now() moves by the same delta
    // unless a wall-clock skew was set via set_utc_skew().
    void advance(std::chrono::nanoseconds delta) noexcept;
    // Force monotonic time to point; wakes any awaiter whose deadline <=
    // point ("fast-forward to next heartbeat").
    void step_to(steady_time_point point) noexcept;
    // Set a wall-clock-only delta that does NOT affect steady_now (NTP-step
    // simulation; SendingTime-threshold tests).
    void set_utc_skew(std::chrono::nanoseconds skew) noexcept;

private:
    struct state;                    // opaque mutable-state object
    std::unique_ptr<state> impl_;
};

}  // namespace fixpp::core
