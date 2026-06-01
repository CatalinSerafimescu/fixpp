// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/sync/test_reentrant_drain_uaf_window.cpp — Seam #31
//
// F-3-residual (gate-b/r1): in-flight-acquirer + reentrant-drain UAF window.
//
// This seam verifies the I-6 escape-hatch remedy now works correctly after the
// F-2 fix: a consumer that follows the documented discipline
//   (a) re-calls cancel_and_drain() after an abort,
//   (b) observes sync_lock_aborted (not false success),
//   (c) waits until the mutex state is quiescent (holder releases),
//   (d) then destroys the mutex
// does NOT trigger a use-after-free or std::terminate.
//
// Pre-F-2-fix this seam would exercise the UAF path:
//   1. Reaper aborts (in-flight resumptions still pending).
//   2. Reentrant cancel_and_drain() returns false success (line 1170).
//   3. Caller destroys mutex — posted resumption's release_ref dereferences
//      destroyed mutex → UAF of waiter_pool_storage_.
//
// Post-F-2-fix:
//   2. Reentrant returns unexpected{sync_lock_aborted}.
//   3. Caller waits for holder to release (state_ → not_locked).
//   4. Destroy — clean; no UAF.
//
// Test 3 (gate-b/r2 P2 — RC-α in-flight-acquirer death seam):
// Adds the round-1 residual P2 seam: an acquirer starts async_lock() AFTER
// draining_=true is set (inside the active_acquirers_count_++ → drained-bypass
// window at async_mutex.hpp:822-828), while the reaper has already been aborted
// and a reentrant cancel_and_drain() is in flight. The acquirer takes the
// drained-bypass path (hpp:827-833 → sync_lock_drained). Destroy is only after
// all coroutines resolve (I-6(b) discipline). ASan/TSan clean.
//
// Oracle: [2f §4.7.3] I-6 (partial-drain destructor discipline), I-5, I-7.
//         F-3-residual (gate-b/r1): missing seam for the in-flight-acquirer
//         + reentrant-drain UAF window identified in Opus triage.
//         gate-b/r2 P2: RC-α in-flight-acquirer window (§4.7.2 step 2,
//         active_acquirers_count_ at async_mutex.hpp:822-828).
//
// TSan/ASan: must be clean. ASan will detect the UAF if the F-2 fix regresses.
//
// HARNESS NOTE: ioc is declared BEFORE async_mutex so that the mutex destructs
// first (reverse C++ order), keeping the io_context service registry alive
// when drain_latch_state's concurrent_channel destructs (TSan-clean).

#include <gtest/gtest.h>

#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/cancellation_type.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <cstddef>
#include <fixpp/core/sync/async_mutex.hpp>
#include <future>
#include <memory>
#include <memory_resource>
#include <vector>

#include "sync/sync_test_support.hpp"

namespace {

using fixpp::core::error;
using fixpp::sync::async_lock_guard;
using fixpp::sync::async_mutex;
using fixpp::sync::expected_t;

using fixpp::sync::test::yield_n;

class counting_memory_resource final : public std::pmr::memory_resource {
public:
    std::atomic<int> alloc_count{0};

private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override {
        alloc_count.fetch_add(1, std::memory_order_acq_rel);
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }

    void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override {
        std::pmr::new_delete_resource()->deallocate(p, bytes, alignment);
    }

    bool do_is_equal(std::pmr::memory_resource const& other) const noexcept override {
        return this == &other;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Test 1: I-6 escape-hatch (a) works after F-2 fix.
//
// Sequence:
//  (1) Hold mutex; park a waiter.
//  (2) Abort the reaper (first cancel_and_drain).
//  (3) Reentrant cancel_and_drain() returns unexpected{sync_lock_aborted}.
//  (4) Release the holder → state_ → not_locked via unlock() under draining_.
//  (5) Destroy the mutex → must not terminate or UAF.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamReentrantDrainUAFWindow, I6EscapeHatchWorksAfterAbort) {
    // ioc declared BEFORE mtx: mtx destructs before ioc (reverse C++ order).
    asio::io_context ioc;
    auto mtx = std::make_unique<async_mutex>();

    asio::cancellation_signal reaper_sig;

    std::atomic<int> reaper_result{-1};
    std::atomic<int> reentrant_result{-1};
    std::atomic<bool> holder_release{false};
    std::atomic<bool> waiter_done{false};

    // Holder: keeps mutex locked until explicitly released.
    auto holder_coro = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx->async_lock();
        EXPECT_TRUE(g.has_value());
        while (!holder_release.load(std::memory_order_acquire)) co_await yield_n(1);
        // guard dtor → unlock() → state_ CAS locked_no_waiters→not_locked
        // (because draining_==true, the short-circuit path runs).
    };

    // One waiter (gets aborted by the reaper).
    auto waiter_coro = [&]() -> asio::awaitable<void> {
        co_await yield_n(1);
        auto r = co_await mtx->async_lock();
        (void)r;
        waiter_done.store(true, std::memory_order_release);
    };

    // Reaper: parked on wait() while holder is active.
    auto reaper_coro = [&]() -> asio::awaitable<void> {
        co_await yield_n(4);
        auto d = co_await mtx->cancel_and_drain();
        if (d.has_value())
            reaper_result.store(0, std::memory_order_release);
        else if (d.error() == error::sync_lock_aborted)
            reaper_result.store(1, std::memory_order_release);
        else
            reaper_result.store(2, std::memory_order_release);
    };

    // Reentrant + orderly shutdown: observes abort, then releases holder, then
    // waits for quiescence before returning (so the ioc drains before
    // mtx.reset()).
    auto reentrant_coro = [&]() -> asio::awaitable<void> {
        while (reaper_result.load(std::memory_order_acquire) == -1) co_await yield_n(1);

        // I-6 re-call — must return unexpected{sync_lock_aborted} (not success).
        auto d = co_await mtx->cancel_and_drain();
        if (d.has_value())
            reentrant_result.store(0, std::memory_order_release);  // BAD
        else if (d.error() == error::sync_lock_aborted)
            reentrant_result.store(1, std::memory_order_release);  // GOOD
        else
            reentrant_result.store(2, std::memory_order_release);

        // Release the holder → unlock() → state_==not_locked.
        holder_release.store(true, std::memory_order_release);
        // Let the unlock + waiter resumption quiesce.
        co_await yield_n(16);
    };

    // Canceller: fires reaper's slot while it is parked on wait().
    auto canceller = [&]() -> asio::awaitable<void> {
        co_await yield_n(12);
        reaper_sig.emit(asio::cancellation_type::total);
    };

    auto fh = asio::co_spawn(ioc, holder_coro(), asio::use_future);
    auto fw = asio::co_spawn(ioc, waiter_coro(), asio::use_future);
    auto fr = asio::co_spawn(ioc, reaper_coro(),
                             asio::bind_cancellation_slot(reaper_sig.slot(), asio::use_future));
    auto frt = asio::co_spawn(ioc, reentrant_coro(), asio::use_future);
    auto fcn = asio::co_spawn(ioc, canceller(), asio::use_future);

    ioc.run();
    fh.get();
    fw.get();
    fr.get();
    frt.get();
    fcn.get();

    EXPECT_EQ(reaper_result.load(), 1) << "Reaper must return sync_lock_aborted";
    EXPECT_EQ(reentrant_result.load(), 1)
        << "I-6 re-call must return unexpected{sync_lock_aborted} (not success). "
           "Got: "
        << reentrant_result.load() << " (0=false-success/UAF-risk, 1=aborted/OK)";

    // Destroy: state_==not_locked (holder released via draining_ short-circuit).
    // ASan monitors for any release_ref touching the destroyed mutex memory.
    // mtx.reset() here — ioc is still alive, so the channel destructs cleanly.
    EXPECT_NO_FATAL_FAILURE(mtx.reset());
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2: Multiple reentrant calls after abort all return sync_lock_aborted.
//
// Verifies the aborted latch stays observable across multiple reentrant callers
// — the latch shared_ptr is non-expiring (the mutex holds it) so every
// subscriber gets the abort outcome.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamReentrantDrainUAFWindow, MultipleReentrantCallsAllReturnAbort) {
    constexpr int R = 3;  // reentrant callers after the abort

    // ioc declared BEFORE mtx: mtx destructs before ioc (reverse C++ order).
    asio::io_context ioc;
    auto mtx = std::make_unique<async_mutex>();
    asio::cancellation_signal reaper_sig;

    std::atomic<int> reaper_result{-1};
    std::atomic<int> reentrant_aborted{0};
    std::atomic<int> reentrant_success{0};
    std::atomic<bool> holder_release{false};

    auto holder_coro = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx->async_lock();
        EXPECT_TRUE(g.has_value());
        while (!holder_release.load(std::memory_order_acquire)) co_await yield_n(1);
    };

    auto reaper_coro = [&]() -> asio::awaitable<void> {
        co_await yield_n(4);
        auto d = co_await mtx->cancel_and_drain();
        if (d.has_value())
            reaper_result.store(0, std::memory_order_release);
        else if (d.error() == error::sync_lock_aborted)
            reaper_result.store(1, std::memory_order_release);
        else
            reaper_result.store(2, std::memory_order_release);
    };

    // R concurrent reentrant callers, all after reaper returns.
    auto make_reentrant = [&](int idx) -> asio::awaitable<void> {
        while (reaper_result.load(std::memory_order_acquire) == -1) co_await yield_n(1);
        co_await yield_n(idx);  // stagger
        auto d = co_await mtx->cancel_and_drain();
        if (d.has_value())
            reentrant_success.fetch_add(1, std::memory_order_acq_rel);
        else if (d.error() == error::sync_lock_aborted)
            reentrant_aborted.fetch_add(1, std::memory_order_acq_rel);
    };

    // Shutdown: after all reentrant calls, release holder.
    auto shutdown_coro = [&]() -> asio::awaitable<void> {
        while (reentrant_aborted.load(std::memory_order_acquire) +
                   reentrant_success.load(std::memory_order_acquire) <
               R)
            co_await yield_n(2);
        holder_release.store(true, std::memory_order_release);
        co_await yield_n(8);
    };

    auto canceller = [&]() -> asio::awaitable<void> {
        co_await yield_n(12);
        reaper_sig.emit(asio::cancellation_type::total);
    };

    auto fh = asio::co_spawn(ioc, holder_coro(), asio::use_future);
    auto fr = asio::co_spawn(ioc, reaper_coro(),
                             asio::bind_cancellation_slot(reaper_sig.slot(), asio::use_future));
    std::vector<std::future<void>> rfuts;
    rfuts.reserve(R);
    for (int i = 0; i < R; ++i)
        rfuts.push_back(asio::co_spawn(ioc, make_reentrant(i), asio::use_future));
    auto fsd = asio::co_spawn(ioc, shutdown_coro(), asio::use_future);
    auto fcn = asio::co_spawn(ioc, canceller(), asio::use_future);

    ioc.run();
    fh.get();
    fr.get();
    for (auto& f : rfuts) f.get();
    fsd.get();
    fcn.get();

    EXPECT_EQ(reaper_result.load(), 1) << "Reaper must return sync_lock_aborted";
    EXPECT_EQ(reentrant_success.load(), 0)
        << "No reentrant call must return success after abort (UAF risk)";
    EXPECT_EQ(reentrant_aborted.load(), R)
        << "All R reentrant calls must return sync_lock_aborted. "
           "aborted="
        << reentrant_aborted.load() << " success=" << reentrant_success.load();

    EXPECT_NO_FATAL_FAILURE(mtx.reset());
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 3 (gate-b/r3 P2): RC-α in-flight-acquirer window — the acquirer is
// spawned before the reentrant drain resolves, then released via deterministic
// single-threaded detached ordering only after the aborted reentrant
// cancel_and_drain() returns. The load-bearing check is that async_lock(&mr)
// returns sync_lock_drained WITHOUT allocating waiter storage.
//
// Sequence:
//  (1) Holder acquires the mutex (fast-path CAS → active_holders_count_=1).
//  (2) Reaper calls cancel_and_drain(): sets draining_=true, sweeps empty
//      lists, enters wait loop (active_holders_count_==1 → parks).
//  (3) Canceller fires reaper_sig → signal_abort() → reaper returns
//      unexpected{sync_lock_aborted} (I-5/I-7). draining_ stays true;
//      drain_latch_ptr_ stays published (aborted_=true).
//  (4) Reentrant_coro: spawns the detached acquirer, then calls
//      cancel_and_drain() — sees draining_=true, loads the aborted latch, and
//      returns unexpected{sync_lock_aborted}. Only then does it set
//      acquirer_go=true.
//  (5) Acquirer_coro: was already spawned and waiting on acquirer_go; once
//      released, it calls async_lock(&counting_mr). The correct path is the
//      early drained-bypass at async_mutex.hpp:827-833, so alloc_count stays 0
//      and the result is sync_lock_drained. If :827-833 regresses, the same
//      acquire falls into waiter-record allocation at :861 before the second
//      draining_ check at :925-933, and alloc_count becomes 1+ even though the
//      final error code is still sync_lock_drained.
//  (6) Reentrant_coro: waits for the acquirer to resolve, then holder_release=true.
//      unlock() under draining_: state_ CAS → not_locked; latch->notify()
//      (no-op: channel already closed). active_holders_count_=0.
//  (7) All futures joined; mtx.reset() — state_==not_locked, all counts 0.
//      ASan monitors for any access to the destroyed mutex (UAF regression).
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamReentrantDrainUAFWindow, RcAlphaInFlightAcquirerDeathSeam) {
    // ioc declared BEFORE mtx: mtx destructs before ioc (reverse C++ order).
    asio::io_context ioc;
    auto mtx = std::make_unique<async_mutex>();
    asio::cancellation_signal reaper_sig;

    std::atomic<int> reaper_result{-1};
    std::atomic<int> reentrant_result{-1};
    std::atomic<int> acquirer_result{-1};
    counting_memory_resource counting_mr;
    std::atomic<bool> holder_release{false};
    std::atomic<bool> acquirer_go{false};
    std::atomic<bool> acquirer_done{false};

    // Holder: keeps mutex locked until signalled.
    auto holder_coro = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx->async_lock();
        EXPECT_TRUE(g.has_value());
        while (!holder_release.load(std::memory_order_acquire)) co_await yield_n(1);
        // guard dtor → unlock() → draining_ path → state_ CAS + latch notify
    };

    // Reaper: starts cancel_and_drain(); parks on active_holders_count_ > 0.
    auto reaper_coro = [&]() -> asio::awaitable<void> {
        co_await yield_n(4);
        auto d = co_await mtx->cancel_and_drain();
        if (d.has_value())
            reaper_result.store(0, std::memory_order_release);
        else if (d.error() == error::sync_lock_aborted)
            reaper_result.store(1, std::memory_order_release);
        else
            reaper_result.store(2, std::memory_order_release);
    };

    // Canceller: aborts the reaper while it is parked on the wait loop.
    auto canceller = [&]() -> asio::awaitable<void> {
        co_await yield_n(16);
        reaper_sig.emit(asio::cancellation_type::total);
    };

    // Reentrant + gate: after reaper returns sync_lock_aborted, spawns the
    // detached acquirer, observes the aborted latch via reentrant
    // cancel_and_drain(), then releases the acquirer and waits for it to finish
    // before letting the holder unlock.
    auto reentrant_coro = [&]() -> asio::awaitable<void> {
        auto ex = co_await asio::this_coro::executor;

        // Wait for reaper to finish.
        while (reaper_result.load(std::memory_order_acquire) == -1) co_await yield_n(1);

        asio::co_spawn(
            ex,
            [&]() -> asio::awaitable<void> {
                while (!acquirer_go.load(std::memory_order_acquire)) co_await yield_n(1);

                auto r = co_await mtx->async_lock(&counting_mr);
                if (r.has_value())
                    acquirer_result.store(0, std::memory_order_release);  // WRONG: granted
                else if (r.error() == error::sync_lock_drained)
                    acquirer_result.store(1, std::memory_order_release);  // CORRECT
                else if (r.error() == error::sync_lock_aborted)
                    acquirer_result.store(2, std::memory_order_release);  // WRONG
                else
                    acquirer_result.store(3, std::memory_order_release);  // WRONG
                acquirer_done.store(true, std::memory_order_release);
            },
            asio::detached);

        // I-5/I-7 reentrant subscribe: must observe aborted latch.
        auto d = co_await mtx->cancel_and_drain();
        if (d.has_value())
            reentrant_result.store(0, std::memory_order_release);  // BAD
        else if (d.error() == error::sync_lock_aborted)
            reentrant_result.store(1, std::memory_order_release);  // GOOD
        else
            reentrant_result.store(2, std::memory_order_release);

        // Open the RC-α gate only after the reentrant drain has returned.
        acquirer_go.store(true, std::memory_order_release);

        while (!acquirer_done.load(std::memory_order_acquire)) co_await yield_n(1);

        holder_release.store(true, std::memory_order_release);
        co_await yield_n(4);
    };

    auto fh = asio::co_spawn(ioc, holder_coro(), asio::use_future);
    auto fr = asio::co_spawn(ioc, reaper_coro(),
                             asio::bind_cancellation_slot(reaper_sig.slot(), asio::use_future));
    auto fcn = asio::co_spawn(ioc, canceller(), asio::use_future);
    auto frt = asio::co_spawn(ioc, reentrant_coro(), asio::use_future);

    ioc.run();
    fh.get();
    fr.get();
    fcn.get();
    frt.get();

    EXPECT_EQ(reaper_result.load(), 1) << "Reaper must return sync_lock_aborted";
    EXPECT_EQ(reentrant_result.load(), 1)
        << "Reentrant cancel_and_drain() after abort must return "
           "unexpected{sync_lock_aborted} (I-5/I-7). Got: "
        << reentrant_result.load() << " (0=false-success/BUG, 1=aborted/OK)";
    EXPECT_EQ(acquirer_result.load(), 1) << "RC-α acquirer must return sync_lock_drained. "
                                            "Got: "
                                         << acquirer_result.load();
    EXPECT_EQ(counting_mr.alloc_count.load(std::memory_order_acquire), 0)
        << "RC-α acquirer must bypass waiter allocation while draining_ is true; "
           "alloc_count="
        << counting_mr.alloc_count.load(std::memory_order_acquire);

    // I-6(b) discipline: all coroutines fully resolved before destroy.
    // ASan monitors for any waiter_pool_storage_ access after mutex death.
    // TSan verifies no data races on the drain_latch_state channel destructor.
    EXPECT_NO_FATAL_FAILURE(mtx.reset());
}

}  // namespace
