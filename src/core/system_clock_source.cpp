// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/core/system_clock_source.cpp
//
// fixpp::core::system_clock_source out-of-line impl ([2d §4.2] / E2 /
// FR-002/FR-003). The intrusive in-flight-waiter list + its std::mutex live
// here (NOT the asio::awaitable header — [const §XI.3]). Each in-flight
// sleep_until owns a shared_ptr<steady_timer>; the list holds weak_ptrs keyed
// by id. cancel_sleeps() posts ->cancel() onto each timer's OWN executor so
// the cancel is race-free against the timer's io thread (asio timers are not
// thread-safe), and the shared_ptr keeps the timer alive across the post.
// Completion (deadline reached OR operation_aborted) lands on the awaiter's
// bound executor because async_wait(use_awaitable) resumes the awaiting
// coroutine on its own executor (FR-003). Idempotent + re-entrant-safe
// (E1 / Edge Case).
#include <fixpp/core/system_clock_source.hpp>

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <system_error>
#include <utility>
#include <vector>

#include <asio/post.hpp>
#include <asio/steady_timer.hpp>
#include <asio/strand.hpp>
#include <asio/use_awaitable.hpp>

namespace fixpp::core {

struct system_clock_source::state {
    asio::any_io_executor                                 engine_exec;
    std::mutex                                            m;
    std::uint64_t                                         next_id{1};
    std::map<std::uint64_t, std::weak_ptr<asio::steady_timer>> inflight;
};

system_clock_source::system_clock_source(asio::any_io_executor exec) noexcept
    : impl_(std::make_unique<state>()) {
    impl_->engine_exec = std::move(exec);
}

system_clock_source::~system_clock_source() {
    // Well-formed shutdown has already drained sessions' sleeps (D-9 / root
    // cause #5); abort any straggler so no async_wait is leaked.
    cancel_sleeps();
}

utc_time_point system_clock_source::now() const noexcept {
    return std::chrono::system_clock::now();
}

steady_time_point system_clock_source::steady_now() const noexcept {
    return std::chrono::steady_clock::now();
}

asio::awaitable<void>
system_clock_source::sleep_until(steady_time_point deadline) {
    // Per-timer strand on engine_exec: asio timers are NOT thread-safe and the
    // contract ([2d §4.1.1]; I-09/I-17) permits concurrent cancel_sleeps() from
    // any thread. Without this wrap, engine_exec is a multi-threaded executor
    // (e.g. thread_pool), so async_wait completion on one worker can race a
    // posted cancel() on another worker — TSan-flagged in
    // deadline_timer_service::cancel (seam 10). The strand serialises all ops
    // (expires_at / async_wait / cancel) on this one timer; cancel_sleeps()
    // already posts cancel() via the timer's own executor (now this strand),
    // so the existing post is correct as-is.
    auto timer = std::make_shared<asio::steady_timer>(
        asio::make_strand(impl_->engine_exec));
    timer->expires_at(deadline);

    std::uint64_t id;
    {
        std::lock_guard<std::mutex> g(impl_->m);
        id = impl_->next_id++;
        impl_->inflight.emplace(id, std::weak_ptr<asio::steady_timer>(timer));
    }
    // RAII de-register (covers deadline-reached, cancel, and exception paths).
    struct dereg {
        system_clock_source::state* s;
        std::uint64_t               id;
        ~dereg() {
            std::lock_guard<std::mutex> g(s->m);
            s->inflight.erase(id);
        }
    } guard{impl_.get(), id};

    // Resumes the awaiting coroutine on ITS bound executor (FR-003); throws
    // std::system_error(operation_aborted) on cancellation (per-op slot OR
    // cancel_sleeps()) — E1 / I-09. The cancellable region converts it to
    // the contract return.
    co_await timer->async_wait(asio::use_awaitable);
    co_return;
}

void system_clock_source::cancel_sleeps() noexcept {
    std::vector<std::shared_ptr<asio::steady_timer>> live;
    {
        std::lock_guard<std::mutex> g(impl_->m);
        live.reserve(impl_->inflight.size());
        for (auto& [id, w] : impl_->inflight) {
            if (auto sp = w.lock()) live.push_back(std::move(sp));
        }
    }
    // Cancel each on its OWN executor (asio timers are not thread-safe); the
    // shared_ptr keeps it alive across the post. Lock released first, so a
    // completion handler that re-enters cancel_sleeps() is safe (E1).
    for (auto& sp : live) {
        auto ex = sp->get_executor();
        asio::post(ex, [sp] { sp->cancel(); });
    }
}

}  // namespace fixpp::core
