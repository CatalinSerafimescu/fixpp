// SPDX-License-Identifier: AGPL-3.0-or-later
// SHAPE ORACLE — NOT the build header. [2d §4.3] deterministic test Clock.
// Real header: include/fixpp/core/test/mock_clock.hpp (PUBLIC test header).
#pragma once
#include "clock.hpp"
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
    mock_clock();
    ~mock_clock() override;

    [[nodiscard]] utc_time_point    now() const noexcept override;
    [[nodiscard]] steady_time_point steady_now() const noexcept override;
    asio::awaitable<expected_t<void>> sleep_until(steady_time_point) override;
    void cancel_sleeps() noexcept override;

    // Test driver — not part of the Clock interface.
    void advance(std::chrono::nanoseconds delta) noexcept;

private:
    struct state;                    // opaque mutable-state object
    std::unique_ptr<state> impl_;
};

}  // namespace fixpp::core
