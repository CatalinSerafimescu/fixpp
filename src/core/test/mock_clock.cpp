// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/core/test/mock_clock.cpp
//
// fixpp::core::mock_clock pimpl impl (D-10 / E3 / [2d §4.3]/§6.6). The
// std::mutex lives HERE, never in the asio::awaitable-including public header
// ([const §XI.3]). Determinism: advance()/step_to() wake every waiter with
// deadline <= new steady_now in deadline order; two identically-seeded
// instances driven by the same advance sequence produce identical
// now/steady_now/wake order (seam 1). sleep_until completion lands on the
// awaiter's bound executor (FR-003); cancellation (per-waiter slot OR
// cancel_sleeps()) completes the awaiter exactly once with
// asio::error::operation_aborted (E1 — NO expected_t return).
#include <fixpp/core/test/mock_clock.hpp>

#include <fixpp/core/clock.hpp>  // utc_time_point, steady_time_point

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <system_error>
#include <utility>
#include <vector>

#include <asio/any_completion_handler.hpp>
#include <asio/append.hpp>
#include <asio/associated_cancellation_slot.hpp>
#include <asio/async_result.hpp>
#include <asio/error.hpp>
#include <asio/post.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>

namespace fixpp::core {

struct mock_clock::state {
    std::mutex                m;
    utc_time_point            utc;            // wall-clock baseline
    steady_time_point         steady;         // monotonic
    std::chrono::nanoseconds  utc_skew{0};    // wall-only delta (NTP sim)
    asio::any_io_executor     engine_exec;

    struct waiter {
        steady_time_point                              deadline;
        asio::any_io_executor                          ex;       // awaiter's bound executor
        asio::any_completion_handler<void(std::error_code)> h;
    };
    std::uint64_t                          next_id{1};
    std::map<std::uint64_t, waiter>        waiters;   // id → waiter (stable)

    // Collect+erase every waiter whose deadline <= steady (caller holds m).
    std::vector<waiter> due_locked() {
        std::vector<waiter> out;
        for (auto it = waiters.begin(); it != waiters.end();) {
            if (it->second.deadline <= steady) {
                out.push_back(std::move(it->second));
                it = waiters.erase(it);
            } else {
                ++it;
            }
        }
        return out;
    }
};

namespace {
// Land the completion on the awaiter's bound executor (FR-003), exactly
// once. Takes the executor + handler (NOT the private nested waiter type) so
// it stays a free helper.
void complete(asio::any_io_executor ex,
              asio::any_completion_handler<void(std::error_code)> h,
              std::error_code ec) {
    asio::post(ex,  // NOLINT(performance-move-const-arg) - post accepts const ref
               asio::append(
                   [](asio::any_completion_handler<void(std::error_code)> hh,
                      std::error_code e) { std::move(hh)(e); },
                   std::move(h), ec));
}
}  // namespace

mock_clock::mock_clock(utc_time_point initial_utc,
                       steady_time_point initial_steady,
                       asio::any_io_executor exec)
    : impl_(std::make_unique<state>()) {
    impl_->utc         = initial_utc;
    impl_->steady      = initial_steady;
    impl_->engine_exec = std::move(exec);
}

mock_clock::~mock_clock() {
    // Well-formed shutdown drains waiters first (seam 14); any stragglers
    // are aborted so no handler is leaked.
    cancel_sleeps();
}

utc_time_point mock_clock::now() const noexcept {
    std::scoped_lock g(impl_->m);
    return impl_->utc + impl_->utc_skew;
}

steady_time_point mock_clock::steady_now() const noexcept {
    std::scoped_lock g(impl_->m);
    return impl_->steady;
}

asio::awaitable<void> mock_clock::sleep_until(steady_time_point deadline) {
    auto bound_ex = co_await asio::this_coro::executor;

    // use_awaitable with a void(error_code) signature auto-throws
    // std::system_error on a non-zero code and yields void — so a deadline
    // reach (error_code{}) returns normally and a cancel
    // (operation_aborted, per-op slot OR cancel_sleeps()) throws
    // operation_aborted out of this coroutine (E1 / I-09; the cancellable
    // region converts it to the contract return).
    co_await asio::async_initiate<
        decltype(asio::use_awaitable), void(std::error_code)>(
        [this, deadline, bound_ex](auto handler) {
            auto cs = asio::get_associated_cancellation_slot(handler);

            std::uint64_t id{0};
            bool fire_now = false;
            {
                std::scoped_lock g(impl_->m);
                if (deadline <= impl_->steady) {
                    fire_now = true;
                } else {
                    id = impl_->next_id++;
                    impl_->waiters.emplace(
                        id, state::waiter{deadline, asio::any_io_executor{bound_ex},
                                          std::move(handler)});
                }
            }
            if (fire_now) {
                // Deadline already in the past — complete immediately on the
                // bound executor (deterministic, no parking).
                // handler was NOT moved into waiters when fire_now==true (mutually exclusive paths):
                // fire_now=true iff deadline<=steady in lock block, in which case the else-branch
                // that moves handler into the map is skipped — so handler is still valid here.
                // NOLINTBEGIN(bugprone-use-after-move,hicpp-invalid-access-moved,clang-analyzer-core.StackAddressEscape)
                asio::post(bound_ex,
                           asio::append(
                               [](auto h, std::error_code e) { std::move(h)(e); },
                               std::move(handler), std::error_code{}));
                // NOLINTEND(bugprone-use-after-move,hicpp-invalid-access-moved,clang-analyzer-core.StackAddressEscape)
                return;
            }
            if (cs.is_connected()) {
                cs.assign([this, id](asio::cancellation_type) {
                    state::waiter w;
                    bool found = false;
                    {
                        std::scoped_lock g(impl_->m);
                        auto it = impl_->waiters.find(id);
                        if (it != impl_->waiters.end()) {
                            w = std::move(it->second);
                            impl_->waiters.erase(it);
                            found = true;
                        }
                    }
                    if (found) {
                        complete(std::move(w.ex), std::move(w.h),
                                 asio::error::operation_aborted);
                    }
                });
            }
        },
        asio::use_awaitable);
    co_return;
}

// NOLINTNEXTLINE(bugprone-exception-escape)
void mock_clock::cancel_sleeps() noexcept {
    std::vector<state::waiter> all;
    {
        std::scoped_lock g(impl_->m);
        for (auto& [id, w] : impl_->waiters) { all.push_back(std::move(w)); }
        impl_->waiters.clear();
    }
    // Idempotent + re-entrant-safe: invoked with the lock released, so a
    // completion handler that re-enters cancel_sleeps() finds an empty map
    // (E1 / FR-003 / Edge Case).
    for (auto& w : all) {
        complete(std::move(w.ex), std::move(w.h),
                 asio::error::operation_aborted);
    }
}

// NOLINTNEXTLINE(bugprone-exception-escape)
void mock_clock::advance(std::chrono::nanoseconds delta) noexcept {
    std::vector<state::waiter> due;
    {
        std::scoped_lock g(impl_->m);
        impl_->steady += delta;
        impl_->utc    += delta;          // wall moves with steady unless skewed
        due = impl_->due_locked();
    }
    for (auto& w : due) {
        complete(std::move(w.ex), std::move(w.h), std::error_code{});
    }
}

// NOLINTNEXTLINE(bugprone-exception-escape)
void mock_clock::step_to(steady_time_point point) noexcept {
    std::vector<state::waiter> due;
    {
        std::scoped_lock g(impl_->m);
        if (point > impl_->steady) {
            const auto d = point - impl_->steady;
            impl_->steady = point;
            impl_->utc   += d;
        }
        due = impl_->due_locked();
    }
    for (auto& w : due) {
        complete(std::move(w.ex), std::move(w.h), std::error_code{});
    }
}

void mock_clock::set_utc_skew(std::chrono::nanoseconds skew) noexcept {
    std::scoped_lock g(impl_->m);
    impl_->utc_skew = skew;   // wall-only; steady_now() unaffected (US2 AC-3)
}

}  // namespace fixpp::core
