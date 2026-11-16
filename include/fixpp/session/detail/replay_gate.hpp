// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/session/detail/replay_gate.hpp — internal detail header (no public symbol).
//
// Feature 103 (specs/103-resend-logon-reset-fidelity/, research D5′; fixpp#588): the gate that
// serialises outbound frames against a resend answer. A resend answer (the walk,
// Session::replay_outbound_range_) holds it exclusive from its entry to its end; every emitter
// outside the inbound chain holds it shared from before its SendingTime stamp and number peek
// until its store_then_emit returns. No outbound number is assigned during an answer, and a
// frame numbered before it is written before its first frame.
//
// Its permission is the Article XI §3 amendment (constitution v4.4, feature 103): the one
// mutex shape besides fixpp::sync::async_mutex that a coroutine may use, scoped to the
// resend-answer serialisation and the in-place reconnect's wait. A further use needs its own
// amendment (Article XX §1).
//
// Strand-local: every acquirer's coroutine runs on the session's executor, so the counters are
// plain members — no atomic and no OS synchronisation primitive. In a build with assertions,
// every acquire, and wake_all() when it acts, checks that it runs on the session's strand when
// the bound executor is one (bind()'s check function); a destructor's bare decrement is not
// checked.
//
// The wake timer is lazy: the first waiter builds it on its own coroutine's executor, with
// expiry time_point::max(), and wake_all() cancels it, which ends every pending wait. Once it
// exists, the Session that owns the gate must be destroyed before that executor's execution
// context, as a transport's socket already requires (research D1's lifetime rule). The gate
// has no destruction precondition and no drain.
//
// A contended wait is wait_for_wake(), a child coroutine. asio's await_transform of an
// awaitable throws operation_aborted when the caller's cancellation is already recorded and
// throw_if_cancelled is on, so the caller tests that itself, in its own frame, immediately
// before the co_await (research D5′.2, "Placement rule"). The gate never resets a cancellation
// state: a wait its caller's cancellation ends leaves that cancellation pending.
#pragma once

#include <asio/any_io_executor.hpp>
#include <asio/as_tuple.hpp>
#include <asio/awaitable.hpp>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <cassert>
#include <cstdint>
#include <fixpp/core/detail/asio_recycler_config.hpp>
#include <optional>
#include <utility>

namespace fixpp::session::detail {

class replay_gate {
public:
    // Whether the calling thread runs `ex`'s serialisation: 1 when `ex` is a strand and it does,
    // 0 when `ex` is a strand and it does not, -1 when `ex` is not a strand (nothing to check).
    using strand_check_fn = int (*)(asio::any_io_executor const& ex) noexcept;

    std::uint32_t shared = 0;   // open shared holds
    bool exclusive = false;     // a walk is waiting or running
    std::uint32_t waiters = 0;  // coroutines suspended in wait_for_wake()
    std::optional<asio::steady_timer> wake;

    // Test hooks: the waits that reached the timer, and the strand checks that saw a strand.
    std::uint64_t timer_waits = 0;
    std::uint64_t strand_checks_on_a_strand = 0;

    replay_gate() = default;
    replay_gate(replay_gate const&) = delete;
    replay_gate& operator=(replay_gate const&) = delete;

    // The session's executor and the check that recognises its strand.
    void bind(asio::any_io_executor ex, strand_check_fn check) noexcept {
        ex_ = std::move(ex);
        check_ = check;
    }

    void assert_on_strand() noexcept {
#ifndef NDEBUG
        if (check_ == nullptr) return;
        int const r = check_(ex_);
        if (r == 1) ++strand_checks_on_a_strand;
        assert(r != 0 && "replay_gate used off the session strand");
#endif
    }

    // Ends every pending wait. A no-op when nobody waits.
    void wake_all() noexcept {
        if (waiters == 0) return;
        assert_on_strand();
        if (wake) wake->cancel();
    }

    // A shared hold, taken on the fast path: the caller has tested `!exclusive` with no
    // suspension since.
    class shared_hold {
    public:
        explicit shared_hold(replay_gate& g) noexcept : g_{&g} {
            g.assert_on_strand();
            assert(!g.exclusive && "replay_gate shared hold taken while a walk holds the gate");
            ++g.shared;
        }
        ~shared_hold() {
            --g_->shared;
            if (g_->shared == 0) g_->wake_all();
        }
        shared_hold(shared_hold const&) = delete;
        shared_hold& operator=(shared_hold const&) = delete;

    private:
        replay_gate* g_;
    };

    // The walk's hold, from its entry to every ending.
    class exclusive_guard {
    public:
        explicit exclusive_guard(replay_gate& g) noexcept : g_{&g} {
            g.assert_on_strand();
            assert(!g.exclusive && "replay_gate: two walks overlap");
            g.exclusive = true;
        }
        ~exclusive_guard() {
            g_->exclusive = false;
            g_->wake_all();
        }
        exclusive_guard(exclusive_guard const&) = delete;
        exclusive_guard& operator=(exclusive_guard const&) = delete;

    private:
        replay_gate* g_;
    };

    // One contended wait: returns when wake_all() runs or the caller's own cancellation ends
    // the wait. The caller re-tests what it waits for, and its cancellation, after it returns.
    asio::awaitable<void> wait_for_wake() {
        assert_on_strand();
        if (!wake) {
            wake.emplace(co_await asio::this_coro::executor, asio::steady_timer::time_point::max());
        }
        waiter_count const w{*this};
        ++timer_waits;
        (void)co_await wake->async_wait(asio::as_tuple(asio::use_awaitable));
    }

private:
    struct waiter_count {
        explicit waiter_count(replay_gate& g) noexcept : g_{&g} { ++g.waiters; }
        ~waiter_count() { --g_->waiters; }
        waiter_count(waiter_count const&) = delete;
        waiter_count& operator=(waiter_count const&) = delete;
        replay_gate* g_;
    };

    asio::any_io_executor ex_;
    strand_check_fn check_ = nullptr;
};

}  // namespace fixpp::session::detail
