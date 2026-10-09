// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/sync/test_544_lock_macro_drain.cpp
//
// fixpp#544 (B35; `.specify/544-hot-path-zero-alloc.md` §2.3, and §3 "E-5's drain"): the
// frameless lock op (FIXPP_DETAIL_CO_AWAIT_LOCK, include/fixpp/core/sync/async_mutex.hpp)
// against cancel_and_drain(). A lock op that meets draining_ completes with
// sync_lock_drained, and every waiter_record the scenario draws goes back to the pool.
//
//   AfterDrain_…    turns RED if the initiation's entry draining_ check is deleted: the
//                   fast path then grants a drained mutex.
//   DuringDrain_…   turns RED if the macro's part 5 (finish_lock) is deleted: the reaped
//                   waiter's record then keeps its attachment reference and never returns
//                   to the free list.
//
// The pool is read through the FIXPP_ASYNC_MUTEX_TEST_SEAM accessors, so this target is
// standalone, with no fixpp_sync linkage (the ODR reason is at the aba_interleave
// registration in tests/sync/CMakeLists.txt). The initiation's second draining_ check,
// after the record is drawn, is reachable only when another thread sets draining_
// between the two checks; the cross-thread SeamInFlightAcquirerCoverage cells
// (test_in_flight_acquirer_coverage.cpp) race it through async_lock(), which is the same
// initiation.

#ifndef FIXPP_ASYNC_MUTEX_TEST_SEAM
#error "test_544_lock_macro_drain.cpp reads FIXPP_ASYNC_MUTEX_TEST_SEAM accessors"
#endif

#include <gtest/gtest.h>

#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/deferred.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/this_coro.hpp>
#include <cstdint>
#include <fixpp/core/error.hpp>
#include <fixpp/core/sync/async_mutex.hpp>

namespace {

using fixpp::core::error;
using fixpp::sync::async_mutex;

// The most posts a wait below makes before it gives up, so that a scenario that never
// reaches its next step fails the cell instead of hanging.
constexpr int kMaxYields = 10000;

// ── After the drain ─────────────────────────────────────────────────────────────────

struct after_drain_outcome {
    bool done = false;
    bool drain_ok = false;
    bool drained = false;
    bool granted = false;
    std::uint32_t pool_next_before = 0;
    std::uint32_t pool_next_after = 0;
};

asio::awaitable<void> drain_then_lock(async_mutex& m, after_drain_outcome& out) {
    auto d = co_await m.cancel_and_drain();
    out.drain_ok = d.has_value();
    out.pool_next_before = m.test_seam_waiter_pool_next();
    {
        FIXPP_DETAIL_CO_AWAIT_LOCK(lock_storage, r, m, nullptr);
        out.granted = r.has_value();
        out.drained = !r.has_value() && r.error() == error::sync_lock_drained;
    }
    out.pool_next_after = m.test_seam_waiter_pool_next();
    out.done = true;
}

TEST(B35LockMacroDrain, AfterDrain_LockOpCompletesDrainedAndDrawsNoRecord) {
    asio::io_context ioc;
    async_mutex m;
    after_drain_outcome out;
    asio::co_spawn(ioc, drain_then_lock(m, out), asio::detached);
    ioc.run();
    ASSERT_TRUE(out.done);
    EXPECT_TRUE(out.drain_ok);
    EXPECT_FALSE(out.granted) << "a lock op on a drained mutex was granted";
    EXPECT_TRUE(out.drained);
    EXPECT_EQ(out.pool_next_after, out.pool_next_before) << "the drained lock op drew a record";
}

// ── During the drain ────────────────────────────────────────────────────────────────
//
// A holder holds the lock. A waiter queues behind it, drawing the first pool record. The
// drain starts: it reaps the waiter, which completes with sync_lock_aborted, and keeps
// yielding while the holder is active. A lock op issued then completes with
// sync_lock_drained. The holder releases, and the drain finishes. The waiter's record
// must then be back on the free list, and no other record drawn.

struct during_drain_outcome {
    bool holder_granted = false;
    bool holder_done = false;
    bool release = false;
    bool waiter_done = false;
    bool waiter_aborted = false;
    bool drain_done = false;
    bool drain_ok = false;
    bool late_drained = false;
    bool stalled = false;
    bool done = false;
    std::uint32_t pool_next = 0;
    std::uint32_t free_head = 0;
};

asio::awaitable<void> hold_until_released(async_mutex& m, during_drain_outcome& out) {
    FIXPP_DETAIL_CO_AWAIT_LOCK(lock_storage, guard, m, nullptr);
    out.holder_granted = guard.has_value();
    auto ex = co_await asio::this_coro::executor;
    for (int y = 0; !out.release && y < kMaxYields; ++y) co_await asio::post(ex, asio::deferred);
    out.holder_done = true;
}

asio::awaitable<void> wait_for_lock(async_mutex& m, during_drain_outcome& out) {
    FIXPP_DETAIL_CO_AWAIT_LOCK(lock_storage, r, m, nullptr);
    out.waiter_aborted = !r.has_value() && r.error() == error::sync_lock_aborted;
    out.waiter_done = true;
}

asio::awaitable<void> drain(async_mutex& m, during_drain_outcome& out) {
    auto d = co_await m.cancel_and_drain();
    out.drain_ok = d.has_value();
    out.drain_done = true;
}

// Yields until `ready()`, or records a stall.
template <class Ready>
asio::awaitable<void> yield_until(Ready ready, during_drain_outcome& out) {
    auto ex = co_await asio::this_coro::executor;
    for (int y = 0; !ready(); ++y) {
        if (y == kMaxYields) {
            out.stalled = true;
            co_return;
        }
        co_await asio::post(ex, asio::deferred);
    }
}

asio::awaitable<void> conduct_during_drain(async_mutex& m, during_drain_outcome& out) {
    auto ex = co_await asio::this_coro::executor;
    asio::co_spawn(ex, hold_until_released(m, out), asio::detached);
    co_await yield_until([&] { return out.holder_granted; }, out);
    asio::co_spawn(ex, wait_for_lock(m, out), asio::detached);
    co_await yield_until([&] { return m.test_seam_waiter_pool_next() == 1U; }, out);
    asio::co_spawn(ex, drain(m, out), asio::detached);
    co_await yield_until([&] { return out.waiter_done; }, out);
    {
        FIXPP_DETAIL_CO_AWAIT_LOCK(lock_storage, late, m, nullptr);
        out.late_drained = !late.has_value() && late.error() == error::sync_lock_drained;
    }
    out.release = true;
    co_await yield_until([&] { return out.drain_done && out.holder_done; }, out);
    out.pool_next = m.test_seam_waiter_pool_next();
    out.free_head = m.test_seam_free_list_head_slot_index();
    out.done = true;
}

TEST(B35LockMacroDrain, DuringDrain_ReapedWaiterAndLateOpReturnEveryRecord) {
    asio::io_context ioc;
    async_mutex m;
    during_drain_outcome out;
    asio::co_spawn(ioc, conduct_during_drain(m, out), asio::detached);
    ioc.run();
    ASSERT_TRUE(out.done);
    ASSERT_FALSE(out.stalled) << "a step of the scenario never happened";
    EXPECT_TRUE(out.holder_granted);
    EXPECT_TRUE(out.waiter_aborted) << "the drain did not reap the queued waiter";
    EXPECT_TRUE(out.late_drained) << "a lock op during the drain did not complete drained";
    EXPECT_TRUE(out.drain_ok);
    EXPECT_EQ(out.pool_next, 1U) << "the scenario drew a record other than the waiter's";
    EXPECT_EQ(out.free_head, 0U) << "the waiter's record (pool slot 0) is not on the free list";
}

}  // namespace
