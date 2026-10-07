// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
#pragma once

#include <asio/awaitable.hpp>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/clock.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <memory>
#include <utility>

namespace fixpp::test_support {

class scripted_clock final : public fixpp::core::Clock {
public:
    explicit scripted_clock(std::shared_ptr<fixpp::core::mock_clock> inner)
        : inner_{std::move(inner)} {}

    [[nodiscard]] fixpp::core::utc_time_point now() const noexcept override {
        return inner_->now();
    }

    [[nodiscard]] fixpp::core::steady_time_point steady_now() const noexcept override {
        using duration = fixpp::core::steady_time_point::duration;
        switch (mode_.load(std::memory_order_acquire)) {
        case mode_set:
            return fixpp::core::steady_time_point{
                duration{set_count_.load(std::memory_order_acquire)}};
        case mode_armed: {
            int expected = phase_first;
            if (phase_.compare_exchange_strong(expected, phase_then, std::memory_order_acq_rel,
                                               std::memory_order_acquire)) {
                return fixpp::core::steady_time_point{
                    duration{first_count_.load(std::memory_order_acquire)}};
            }
            return fixpp::core::steady_time_point{
                duration{then_count_.load(std::memory_order_acquire)}};
        }
        default:
            return inner_->steady_now();
        }
    }

    [[nodiscard]] asio::awaitable<void> sleep_until(
        fixpp::core::steady_time_point deadline) override {
        sleeps_.fetch_add(1, std::memory_order_release);
        co_await inner_->sleep_until(deadline);
    }

    void cancel_sleeps() noexcept override { inner_->cancel_sleeps(); }

    void arm(fixpp::core::steady_time_point first,
             fixpp::core::steady_time_point then) noexcept {
        first_count_.store(first.time_since_epoch().count(), std::memory_order_release);
        then_count_.store(then.time_since_epoch().count(), std::memory_order_release);
        phase_.store(phase_first, std::memory_order_release);
        mode_.store(mode_armed, std::memory_order_release);
    }

    void set(fixpp::core::steady_time_point value) noexcept {
        set_count_.store(value.time_since_epoch().count(), std::memory_order_release);
        mode_.store(mode_set, std::memory_order_release);
    }

    [[nodiscard]] std::size_t sleeps_observed() const noexcept {
        return sleeps_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::shared_ptr<fixpp::core::mock_clock> inner() const noexcept {
        return inner_;
    }

private:
    static constexpr int mode_inner = 0;
    static constexpr int mode_armed = 1;
    static constexpr int mode_set = 2;
    static constexpr int phase_first = 0;
    static constexpr int phase_then = 1;

    std::shared_ptr<fixpp::core::mock_clock> inner_;
    mutable std::atomic<int> mode_{mode_inner};
    mutable std::atomic<int> phase_{phase_then};
    mutable std::atomic<std::int64_t> first_count_{0};
    mutable std::atomic<std::int64_t> then_count_{0};
    mutable std::atomic<std::int64_t> set_count_{0};
    mutable std::atomic<std::size_t> sleeps_{0};
};

}  // namespace fixpp::test_support
