// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/sync/test_pool_exhaustion_reuse.cpp
//
// 058-async-mutex-hardening — AM-P2-3 bounded exhaustion witnesses
// (research.md D-4; tasks.md T020/T021/T022).
//
// Defect (pre-fix): the bump-allocator's `waiter_pool_next_.fetch_add(1)` is
// UNCONDITIONAL — it increments even on capacity-check-FAILING attempts (the
// `>= capacity` check runs AFTER the fetch_add). A sustained exhaustion storm
// (2^32 failing attempts) wraps the u32 counter back into [0, capacity) and
// RE-ISSUES an already-live slot: a second, unrelated waiter's placement-new
// clobbers a still-parked waiter's memory in place.
//
// Fix (T021): a bounded CAS loop that loads first and refuses to increment
// once `cur >= waiter_pool_capacity_` — the counter can never advance past
// capacity, so it can never wrap.
//
// Three tests:
//   1. ExhaustionAt513thWaiterFailsClosed — ordinary FR-009 boundary: the
//      513th contended waiter (511 slots already given to 512 real parked
//      waiters) fails closed with sync_lock_alloc_failed. This is NOT a
//      pre/post-fix discriminator (512 is nowhere near the u32 wrap) — it is
//      baseline coverage that the bounded counter still behaves correctly at
//      ordinary capacity.
//   2. FreedSlotReusedViaFreeListAfterExhaustion (T022 composition) — once
//      the pool is capacity-exhausted, cancelling a parked waiter returns its
//      slot to the free list (T009/T010); the NEXT contended attempt must
//      succeed by POPPING that freed slot, not by touching the (already
//      capacity-bound) bump allocator. Proves the free-list pop path and the
//      bounded bump-allocator fail-closed path compose correctly.
//   3. BoundedCounterPreventsWrapAndReissue (T020 primary witness) — the
//      discriminating RED/GREEN oracle. Presets `waiter_pool_next_` to
//      UINT32_MAX via the FIXPP_ASYNC_MUTEX_TEST_SEAM test-only accessor
//      (2^32 real attempts is impractical), then drives two more contended
//      attempts. Pre-fix: the first attempt (C) wraps the counter to 0
//      (fail-closed on the wrapped value, but the counter is now back at 0);
//      the second attempt (D) then succeeds, reissuing pool slot 0 — the SAME
//      memory a still-parked waiter B occupies — and D's placement-new
//      clobbers B's live `attached_awaiter_` identity in place. Post-fix:
//      both C and D see `cur >= capacity` and fail closed WITHOUT
//      incrementing; the counter never returns below capacity; slot 0's
//      identity is provably UNCHANGED (asserted directly via the seam
//      accessor's before/after pointer comparison, not merely "an allocation
//      failed" — the non-discriminating proxy).
//
// Coroutine caveat: gtest's ASSERT_* macros expand to a bare `return;`, which
// is NOT valid inside a C++20 coroutine body (must be `co_return;`). Every
// coroutine lambda below therefore uses EXPECT_* only (non-fatal) plus plain
// `if (...) co_return;` for early-exit control flow; the real fatal
// assertions run in the TEST function body itself, AFTER `ioc.run()`/
// `f.get()`, over captured state.
//
// ODR (research.md D-7, reused verbatim for T020): async_mutex.hpp is
// header-only with inline out-of-line bodies. This target MUST link no
// fixpp object/library that also instantiates those bodies (fixpp_sync
// compiles atomic_shared_ptr.cpp, which itself includes async_mutex.hpp) —
// linking it here alongside a macro-enabled TU risks two divergent
// definitions of the same class. This target is therefore standalone,
// mirroring test_async_mutex_aba_interleave's registration: header include
// path + asio::asio (header-only) + GTest only, ZERO fixpp library linkage.
//
// Oracle: research.md D-4; spec.md FR-004/AM-P2-3, SC-004.

#include <gtest/gtest.h>

#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>

#include <chrono>
#include <cstdint>
#include <fixpp/core/sync/async_mutex.hpp>
#include <future>
#include <optional>
#include <vector>

#include "sync_test_support.hpp"

namespace {

using fixpp::core::error;
using fixpp::sync::async_lock_guard;
using fixpp::sync::async_mutex;
using fixpp::sync::expected_t;
using fixpp::sync::test::yield_n;

constexpr std::size_t kCapacity = 512;

// Drives `ioc` until `f` is ready or a 5s deadline elapses (mirrors the
// bounded-drain idiom in test_drain_predrain_holder.cpp). Needed ONLY for
// the pre-fix mutation-RED run of the wrap/reissue witness below: a
// reissued live slot leaves the clobbered waiter's coroutine frame
// permanently suspended (never resumed — its `attached_awaiter_` now
// points elsewhere), and asio's awaitable machinery counts a suspended
// frame as outstanding executor work, so a blind `ioc.run()` would block
// forever even though `run()` ITSELF has already returned (co_return) —
// checking `f`'s readiness directly (not the io_context's work count)
// sidesteps that. Returns true iff `f` became ready before the deadline.
template <typename T>
bool run_until_ready(asio::io_context& ioc, std::future<T>& f) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        ioc.run_for(std::chrono::milliseconds(50));
        if (f.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) return true;
    }
    if (f.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) return true;
    ioc.stop();
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 1 — ordinary capacity boundary (FR-009 baseline; not a discriminator).
// ─────────────────────────────────────────────────────────────────────────────

TEST(SyncPoolExhaustionReuse, ExhaustionAt513thWaiterFailsClosed) {
    asio::io_context ioc;
    async_mutex mtx;

    std::vector<std::optional<expected_t<async_lock_guard>>> results(kCapacity + 1);
    bool drain_ok = false;

    auto run = [&]() -> asio::awaitable<void> {
        auto holder = co_await mtx.async_lock();
        EXPECT_TRUE(holder.has_value());
        if (!holder.has_value()) co_return;

        auto ex = co_await asio::this_coro::executor;
        for (std::size_t i = 0; i < kCapacity + 1; ++i) {
            asio::co_spawn(
                ex,
                [&, i]() -> asio::awaitable<void> {
                    co_await yield_n(1);
                    results[i] = co_await mtx.async_lock();
                },
                asio::detached);
        }

        // Let every contended attempt run its synchronous park-or-fail step.
        co_await yield_n(static_cast<int>(kCapacity) + 8);

        // The first 512 must still be PARKED (unresolved — never granted,
        // A hasn't unlocked yet); only the 513th resolves synchronously.
        for (std::size_t i = 0; i < kCapacity; ++i) {
            EXPECT_FALSE(results[i].has_value())
                << "waiter " << i << " must still be parked, not resolved";
        }

        // Start the drain CONCURRENTLY (co_spawn), yield to let it set
        // `draining_`, THEN release A. unlock() checks `draining_` and
        // short-circuits (no auto-grant of the next queued waiter) once it
        // is set — releasing A BEFORE the drain starts would instead grant
        // the LIFO head as a new holder, which then never itself unlocks
        // and hangs the drain waiting on active_holders_count_==0 (mirrors
        // the pre-drain-holder idiom in test_drain_predrain_holder.cpp).
        auto fd = asio::co_spawn(
            ex,
            [&]() -> asio::awaitable<void> {
                auto d = co_await mtx.cancel_and_drain();
                drain_ok = d.has_value();
            },
            asio::use_future);
        co_await yield_n(4);

        holder = expected_t<async_lock_guard>{};
        co_await yield_n(static_cast<int>(kCapacity) + 16);
        fd.get();
    };

    auto f = asio::co_spawn(ioc, run(), asio::use_future);
    ioc.run();
    f.get();

    // waiters 0..511's parked-state was checked inside the coroutine
    // (EXPECT_FALSE, above) before the drain ran. The 513th's boundary
    // result is the discriminating oracle, checked here.
    ASSERT_TRUE(results[kCapacity].has_value())
        << "the 513th contended attempt must resolve synchronously (fail-"
           "closed), not park";
    EXPECT_FALSE(results[kCapacity]->has_value());
    if (!results[kCapacity]->has_value()) {
        EXPECT_EQ(results[kCapacity]->error(), error::sync_lock_alloc_failed);
    }
    EXPECT_TRUE(drain_ok);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2 (T022 composition) — a freed slot is reused via the free-list pop
// even once the bump allocator is already capacity-bound.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SyncPoolExhaustionReuse, FreedSlotReusedViaFreeListAfterExhaustion) {
    asio::io_context ioc;
    async_mutex mtx;

    std::vector<std::optional<expected_t<async_lock_guard>>> results(kCapacity);
    std::optional<expected_t<async_lock_guard>> saturated_probe;
    std::optional<expected_t<async_lock_guard>> extra_result;
    asio::cancellation_signal cancel_sig;
    bool drain_ok = false;
    // Snapshotted BEFORE the drain starts — the drain later resolves every
    // still-parked waiter (including a successfully-reused `extra_result`)
    // via cancellation, so checking `extra_result` AFTER ioc.run() would
    // always observe it resolved regardless of whether it parked or failed.
    bool extra_was_parked_pre_drain = false;

    auto run = [&]() -> asio::awaitable<void> {
        auto holder = co_await mtx.async_lock();
        EXPECT_TRUE(holder.has_value());
        if (!holder.has_value()) co_return;

        auto ex = co_await asio::this_coro::executor;

        // Saturate the pool with 512 real parked waiters (waiter 0 is the
        // one we'll cancel below to free its slot back to the free list).
        for (std::size_t i = 0; i < kCapacity; ++i) {
            if (i == 0) {
                asio::co_spawn(
                    ex,
                    [&]() -> asio::awaitable<void> {
                        co_await yield_n(1);
                        results[0] = co_await mtx.async_lock();
                    },
                    asio::bind_cancellation_slot(cancel_sig.slot(), asio::detached));
            } else {
                asio::co_spawn(
                    ex,
                    [&, i]() -> asio::awaitable<void> {
                        co_await yield_n(1);
                        results[i] = co_await mtx.async_lock();
                    },
                    asio::detached);
            }
        }
        co_await yield_n(static_cast<int>(kCapacity) + 8);

        // Confirm the pool is genuinely saturated: one more contended
        // attempt must fail closed (bump allocator at capacity, free list
        // still empty).
        asio::co_spawn(
            ex,
            [&]() -> asio::awaitable<void> {
                co_await yield_n(1);
                saturated_probe = co_await mtx.async_lock();
            },
            asio::detached);
        co_await yield_n(8);

        // Cancel waiter 0 (the FIFO-order head — it was submitted first, so
        // it sits at the tail of the LIFO / head of the reversed-FIFO order
        // unlock() walks). A cancelled waiter's slot is NOT actually
        // returned to the free list by on_cancel() alone — on_cancel only
        // marks phase_=cancelled and resumes the waiter's own coroutine;
        // the record stays reachable from `state_`'s chain (holding the
        // "list membership" ref) until something WALKS the chain and
        // unlinks it. That walk happens in unlock()'s reversed-FIFO grant
        // loop (or cancel_and_drain()'s reap) — so releasing A below is
        // what actually recycles waiter 0's slot, in addition to granting
        // waiter 1 as the new holder.
        cancel_sig.emit(asio::cancellation_type::total);
        co_await yield_n(8);

        // Release A. unlock() walks the reversed-FIFO chain: waiter 0
        // (head, cancelled) is unlinked — releasing its list-membership ref
        // to 0, pushing its slot onto the free list (T009/T010) — then
        // waiter 1 (queued) is granted, becoming the new holder. The mutex
        // stays LOCKED throughout (ownership transfers to waiter 1), so a
        // subsequent acquire attempt is still CONTENDED.
        holder = expected_t<async_lock_guard>{};
        co_await yield_n(8);

        // A NEW contended attempt (against waiter 1, the new holder) must
        // now succeed — via the free-list pop (T009/T010), NOT via the bump
        // allocator (which is still saturated at exactly `kCapacity`, and
        // per the bounded-CAS fix (T021) can never advance past it). A
        // successful park leaves `extra_result` unresolved (nullopt) since
        // waiter 1 never unlocks until teardown below.
        asio::co_spawn(
            ex,
            [&]() -> asio::awaitable<void> {
                co_await yield_n(1);
                extra_result = co_await mtx.async_lock();
            },
            asio::detached);
        co_await yield_n(8);
        extra_was_parked_pre_drain = !extra_result.has_value();

        // Safe teardown: start the drain BEFORE releasing waiter 1's guard
        // (so `draining_` is set before its unlock() runs — otherwise
        // unlock() auto-grants the next queued waiter instead of deferring
        // to the drain, and the drain then hangs waiting on
        // active_holders_count_==0; mirrors test_drain_predrain_holder.cpp
        // / test 1 above).
        auto fd = asio::co_spawn(
            ex,
            [&]() -> asio::awaitable<void> {
                auto d = co_await mtx.cancel_and_drain();
                drain_ok = d.has_value();
            },
            asio::use_future);
        co_await yield_n(4);

        if (results[1].has_value()) {
            results[1] = expected_t<async_lock_guard>{};  // releases waiter 1's guard
        }
        co_await yield_n(static_cast<int>(kCapacity) + 16);
        fd.get();
    };

    auto f = asio::co_spawn(ioc, run(), asio::use_future);
    ioc.run();
    f.get();

    ASSERT_TRUE(saturated_probe.has_value())
        << "pool must be genuinely saturated before the cancel+reuse probe";
    EXPECT_FALSE(saturated_probe->has_value());

    ASSERT_TRUE(results[0].has_value()) << "cancelled waiter 0 must resolve";
    EXPECT_FALSE(results[0]->has_value());
    if (!results[0]->has_value()) {
        EXPECT_EQ(results[0]->error(), error::sync_lock_aborted);
    }

    // The reused-via-free-list waiter must have PARKED (not failed closed)
    // at the pre-drain snapshot point — checked there because the drain
    // subsequently resolves it too (via cancellation), which would mask a
    // "failed closed" defect if checked only after ioc.run().
    EXPECT_TRUE(extra_was_parked_pre_drain)
        << "the reused-via-free-list waiter must have parked successfully "
           "(not failed closed) even though the bump allocator is saturated";
    EXPECT_TRUE(drain_ok);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 3 (T020 primary witness) — bounded counter prevents wrap-and-reissue.
//
// Uses the FIXPP_ASYNC_MUTEX_TEST_SEAM test-only accessors
// (test_seam_preset_waiter_pool_next / test_seam_waiter_pool_next /
// test_seam_slot_attached_awaiter) to reach the u32 wrap boundary without
// 2^32 real attempts (research.md D-4/D-7).
// ─────────────────────────────────────────────────────────────────────────────

TEST(SyncPoolExhaustionReuse, BoundedCounterPreventsWrapAndReissue) {
    asio::io_context ioc;
    async_mutex mtx;

    std::optional<expected_t<async_lock_guard>> b_result;
    std::optional<expected_t<async_lock_guard>> c_result;
    std::optional<expected_t<async_lock_guard>> d_result;
    std::uint32_t pool_next_after_b = 0;
    void const* b_identity_before = nullptr;
    void const* b_identity_after = nullptr;
    std::uint32_t pool_next_final = 0;
    bool corruption_detected = false;
    bool drain_ok = false;
    bool drain_attempted = false;
    // Snapshotted BEFORE C/D run — the drain (post-fix teardown path) later
    // resolves B via cancellation, so checking `b_result` AFTER ioc.run()
    // would always observe it resolved regardless of the setup precondition.
    bool setup_precondition_ok = false;

    auto run = [&]() -> asio::awaitable<void> {
        // A: fast-path holder — does NOT touch the pool.
        auto holder = co_await mtx.async_lock();
        EXPECT_TRUE(holder.has_value());
        if (!holder.has_value()) co_return;

        auto ex = co_await asio::this_coro::executor;

        // B: first contended waiter — parks via the bump allocator, taking
        // slot 0 (waiter_pool_next_ 0 -> 1). Stays LIVE (parked, unresolved)
        // for the rest of this test — A never unlocks until safe teardown.
        asio::co_spawn(
            ex,
            [&]() -> asio::awaitable<void> {
                co_await yield_n(1);
                b_result = co_await mtx.async_lock();
            },
            asio::detached);
        co_await yield_n(4);

        pool_next_after_b = mtx.test_seam_waiter_pool_next();
        b_identity_before = mtx.test_seam_slot_attached_awaiter(0);
        setup_precondition_ok =
            !b_result.has_value() && pool_next_after_b == 1u && b_identity_before != nullptr;
        if (!setup_precondition_ok) {
            // Setup did not reach the expected precondition; nothing further
            // to safely probe. Skip straight to teardown attempt.
            co_return;
        }

        // Preset the counter to the u32 wrap boundary (research.md D-4).
        mtx.test_seam_preset_waiter_pool_next(0xFFFFFFFFu);

        // Attempt C: pre-fix, fetch_add(1) returns 0xFFFFFFFF (>= capacity,
        // fails closed) but WRAPS the counter to 0 as a side effect.
        // Post-fix, the bounded CAS observes `cur >= capacity` and refuses
        // to increment at all — counter stays at 0xFFFFFFFF.
        asio::co_spawn(
            ex,
            [&]() -> asio::awaitable<void> {
                co_await yield_n(1);
                c_result = co_await mtx.async_lock();
            },
            asio::detached);
        co_await yield_n(4);

        // Attempt D: the critical reissue probe. Pre-fix, the counter was
        // wrapped to 0 by C, so D's fetch_add(1) returns 0 (< capacity) and
        // SUCCEEDS — reissuing pool slot 0, the SAME memory B still occupies
        // as a live parked waiter. D's placement-new clobbers B's
        // `attached_awaiter_` identity in place. Post-fix, the counter never
        // moved off capacity, so D also fails closed like C.
        asio::co_spawn(
            ex,
            [&]() -> asio::awaitable<void> {
                co_await yield_n(1);
                d_result = co_await mtx.async_lock();
            },
            asio::detached);
        co_await yield_n(4);

        b_identity_after = mtx.test_seam_slot_attached_awaiter(0);
        pool_next_final = mtx.test_seam_waiter_pool_next();
        corruption_detected = (b_identity_after != b_identity_before);

        if (corruption_detected) {
            // DO NOT attempt cancel_and_drain(): pre-fix, D's placement-new
            // over B's still-linked memory can leave the mutex's internal
            // wait-list self-referential (D pushed the SAME node B already
            // occupies). The destructor's guard fires on the un-drained
            // `state_ != not_locked` alone (a simple atomic check, no list
            // traversal) — safe. Walking the corrupted list would not be.
            co_return;
        }

        // Reaching here: no corruption occurred (post-fix). Safe to drain
        // B (still parked) and release A — start the drain BEFORE releasing
        // A so unlock() defers to it instead of auto-granting B (mirrors
        // test 1/2's teardown and test_drain_predrain_holder.cpp).
        drain_attempted = true;
        auto fd = asio::co_spawn(
            ex,
            [&]() -> asio::awaitable<void> {
                auto d = co_await mtx.cancel_and_drain();
                drain_ok = d.has_value();
            },
            asio::use_future);
        co_await yield_n(4);

        holder = expected_t<async_lock_guard>{};
        co_await yield_n(16);
        fd.get();
    };

    auto f = asio::co_spawn(ioc, run(), asio::use_future);
    // Bounded drive (see run_until_ready's comment): pre-fix, a detected
    // reissue leaves B's coroutine permanently suspended, which would make
    // a blind ioc.run() hang forever even though `run()` itself already
    // returned. `run()`'s own completion (not the io_context's outstanding
    // work count) is the signal we actually need.
    bool completed = run_until_ready(ioc, f);
    ASSERT_TRUE(completed)
        << "test driver did not observe run() completion within the bounded "
           "deadline — see captured diagnostics below";
    f.get();

    ASSERT_TRUE(setup_precondition_ok)
        << "B must have parked and taken bump-allocator slot 0 before C/D ran "
           "(pool_next_after_b=" << pool_next_after_b << ")";

    ASSERT_TRUE(c_result.has_value()) << "attempt C must resolve synchronously";
    EXPECT_FALSE(c_result->has_value());
    if (!c_result->has_value()) {
        EXPECT_EQ(c_result->error(), error::sync_lock_alloc_failed);
    }

    // THE discriminating oracle: slot 0's live-waiter identity must be
    // UNCHANGED by attempt D. This is asserted directly (pointer identity),
    // not via the "D failed closed" proxy — a reissue that happened to ALSO
    // fail for some unrelated reason would not be caught by an
    // alloc_failed-only check, and conversely a fail-closed result alone
    // does not prove no other slot was corrupted. Pointer identity is the
    // direct proof of "still live, never touched again".
    ASSERT_FALSE(corruption_detected)
        << "waiter_pool_next_ wrapped and REISSUED a still-live slot "
           "(AM-P2-3 pre-fix defect): pool slot 0 was still occupied by "
           "parked waiter B, but a second, unrelated contended attempt D "
           "was handed the SAME slot and placement-new clobbered B's live "
           "attached_awaiter_ identity.";

    // Corollary: the counter must never have returned below capacity.
    EXPECT_GE(pool_next_final, kCapacity)
        << "bounded counter must never wrap below capacity once saturated";

    // D must ALSO have failed closed (symmetric with C, post-fix).
    ASSERT_TRUE(d_result.has_value());
    EXPECT_FALSE(d_result->has_value());
    if (!d_result->has_value()) {
        EXPECT_EQ(d_result->error(), error::sync_lock_alloc_failed);
    }

    EXPECT_TRUE(drain_attempted) << "post-fix run must reach the safe-teardown branch";
    EXPECT_TRUE(drain_ok);
}

}  // namespace
