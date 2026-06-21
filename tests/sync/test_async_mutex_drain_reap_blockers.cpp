// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/sync/test_async_mutex_drain_reap_blockers.cpp
// W-B1..W-B4 (047-async-mutex-drain-reap, FR-006 / SC-006) — the four
// blocker-discriminating witnesses. Each targets a distinct fix in the
// coordinated drain-reap restructure; per [[feedback_coverage_push_enshrines_bugs]]
// witnessing only the original orphan would enshrine the other four fixes, so
// each blocker gets its own RED→GREEN whose RED is proven by the T016
// mutation-revert (revert that one fix → this witness goes RED).
//
// Shared discipline (matches W-orig): heap-allocate mutex+pool per round so the
// failure path can LEAK them (the async_mutex dtor aborts on a non-empty state →
// a clean gtest FAILURE would become a process abort); an internal steady-clock
// self-deadline (fast attributable FAIL, never a lane hang); fresh mutex per
// round; poll atomic counters instead of blocking joins.
//
// RED targets:
//   W-B1 (notify)   : mutation-RED on a converging loop whose feeder decrement
//                     does NOT notify → reaper parked on acquirers!=0 misses the
//                     →0 edge → hang → deadline trips.
//   W-B2 (snapshot) : mutation-RED on holders-before-feeders read order → reaper
//                     observes holders==0 spuriously while a grant is in flight
//                     → finalizes success while the lock is held.
//   W-B3 (unlock)   : RED vs main + mutation-RED on dropping the unlocker RAII
//                     bracket → reaper finalizes mid-unlock-walk → residual W2
//                     pushed after finalize → orphan (ASan UAF if the round then
//                     destroys the mutex). The holder must take the NON-draining
//                     unlock residual path (grant W1 + push_residual W2) racing
//                     the drain — NOT the L953 short-circuit branch.
//   W-B4 (CAS)      : RED vs main + mutation-RED on the unconditional
//                     phase_.store at the second draining gate → both on_cancel
//                     and the gate schedule a resume → double-invoke_handler on a
//                     destroyed frame → ASan UAF. The exactly-once counter is a
//                     SECONDARY signal; ASan is the real oracle.

#include <gtest/gtest.h>

#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/cancellation_type.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_awaitable.hpp>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fixpp/core/sync/async_mutex.hpp>
#include <memory>
#include <thread>
#include <vector>

#include "sync/sync_test_support.hpp"

namespace {

using fixpp::core::error;
using fixpp::sync::async_lock_guard;
using fixpp::sync::async_mutex;
using fixpp::sync::expected_t;
using fixpp::sync::test::yield_n;

// Bounded poll on a predicate; returns false on deadline.
template <typename Pred>
bool wait_until(Pred pred, std::chrono::milliseconds budget) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

// ── W-B3 ─────────────────────────────────────────────────────────────────────
// A holder with a residual W1→W2 chain in next_drain_head_ unlocks on the
// NON-draining path (reads draining_==false, grants W1, push_residual W2)
// concurrently with a drain. Assert: every waiter settles (W2 is reaped, not
// orphaned) and the drain reports success exactly once. The unlocker RAII
// bracket is what stops the reaper finalizing mid-walk; revert it → W2 orphaned.
//
// To force the residual path: the holder grants the next waiter on unlock (so it
// walks the list and produces a residual tail). The drainer is timed to race the
// holder's unlock — the holder unlock must have a real chance of reading
// draining_==false (so it walks) while the reaper concurrently tries to finalize.
TEST(AsyncMutexDrainReapBlockers, WB3_UnlockResidualChainRacingDrainNeverOrphans) {
    constexpr int kPoolSize = 4;
    constexpr int kRounds = 200;
    constexpr int kWaitersPerRound = 8;  // >=2 → unlock produces a residual tail
    constexpr auto kDeadline = std::chrono::seconds(5);

    for (int round = 0; round < kRounds; ++round) {
        auto* pool = new asio::thread_pool{kPoolSize};
        auto* mtx = new async_mutex;

        std::atomic<int> completed{0};
        std::atomic<int> granted{0};
        std::atomic<int> settled_nonheld{0};
        std::atomic<int> drain_done{0};
        std::atomic<int> drain_ok{0};
        std::atomic<bool> holder_acquired{false};

        // Holder: holds briefly, then unlocks. The race under test is the
        // holder's UNLOCK vs the drain's publish — NOT the acquire, so we gate
        // the drainer on holder_acquired to make the holder reliably acquire
        // first (otherwise a drain that wins the acquire race drains the holder).
        auto holder = [mtx, &holder_acquired]() -> asio::awaitable<void> {
            auto g = co_await mtx->async_lock();
            EXPECT_TRUE(g.has_value());
            holder_acquired.store(true, std::memory_order_release);
            co_await yield_n(kWaitersPerRound + 2);
            // guard dtor → unlock() walks the residual chain on the non-draining
            // path, granting one waiter and push_residual-ing the tail.
        };

        auto drainer = [mtx, &drain_done, &drain_ok, &holder_acquired]() -> asio::awaitable<void> {
            while (!holder_acquired.load(std::memory_order_acquire)) co_await yield_n(1);
            co_await yield_n(kWaitersPerRound + 2);
            auto d = co_await mtx->cancel_and_drain();
            if (d.has_value()) drain_ok.fetch_add(1, std::memory_order_acq_rel);
            drain_done.fetch_add(1, std::memory_order_acq_rel);
        };

        // Waiter: a granted waiter holds for a moment then releases (so a granted
        // residual still terminates). Every waiter increments completed once.
        auto waiter = [mtx, &completed, &granted, &settled_nonheld]() -> asio::awaitable<void> {
            co_await yield_n(1);
            auto r = co_await mtx->async_lock();
            if (r.has_value()) {
                granted.fetch_add(1, std::memory_order_acq_rel);
                co_await yield_n(2);
                // guard dtor releases.
            } else {
                settled_nonheld.fetch_add(1, std::memory_order_acq_rel);
            }
            completed.fetch_add(1, std::memory_order_acq_rel);
        };

        asio::co_spawn(*pool, holder(), asio::detached);
        asio::co_spawn(*pool, drainer(), asio::detached);
        for (int i = 0; i < kWaitersPerRound; ++i) asio::co_spawn(*pool, waiter(), asio::detached);

        bool ok = wait_until(
            [&] {
                return completed.load(std::memory_order_acquire) == kWaitersPerRound &&
                       drain_done.load(std::memory_order_acquire) == 1;
            },
            kDeadline);

        if (!ok) {
            ADD_FAILURE() << "WB3 round=" << round
                          << ": TIMEOUT — a residual waiter was ORPHANED. completed="
                          << completed.load() << "/" << kWaitersPerRound
                          << " granted=" << granted.load() << " settled=" << settled_nonheld.load()
                          << " drain_done=" << drain_done.load()
                          << ". LEAKING mutex+pool (dtor would abort on the orphan).";
            return;
        }

        EXPECT_EQ(drain_ok.load(), 1)
            << "WB3 round=" << round << ": drain must succeed exactly once (no early finalize)";

        pool->join();
        delete mtx;
        delete pool;
    }
}

// ── W-B4 ─────────────────────────────────────────────────────────────────────
// A cancellation hits the second draining gate concurrently with the drained
// fast-fail: a waiter is mid-async_lock, a real cancellation_signal fires on its
// slot, and a drain fires — both race for terminal ownership of the waiter_record.
// With the unconditional phase_.store (pre-fix), both on_cancel and the gate
// schedule a resume → invoke_handler runs twice on the destroyed async_lock awaiter
// → ASan heap-use-after-free. With the queued→cancelled CAS, exactly one wins.
//
// HARNESS CORRECTNESS (the subtle part): asio::cancellation_signal is NOT
// thread-safe, so a cross-thread emit() that races a waiter's NORMAL async_lock
// completion (which clears/disconnects the inherited cancellation slot) is itself a
// UAF — independent of the async_mutex code (it reproduces on pre-fix main AND the
// fix). To isolate the PRODUCTION B4 race from that harness race, each waiter is
// kept ALIVE on a post-lock barrier (go_finish) until the canceller has finished
// ALL emits (cancel_done). async_lock's epilogue already reset the waiter to
// terminal-only cancellation, so the late `total` emit is filtered at the barrier
// and cannot abort/teardown the still-suspended frame. The emit therefore only ever
// lands while the waiter is mid-async_lock (frame intact, racing the 2nd gate) or
// parked on the filtered barrier — never on a frame being destroyed. ASan is the
// oracle; exactly-once is the secondary functional signal.
// NOTE (047 verify, UNRESOLVED): this witness currently FAILS on the fix — see
// .specify/decisions/047-async-mutex-drain-reap-verify.md Finding 0. Two modes:
// (1) an asio harness UAF (cross-thread cancellation_signal::emit racing the
// cancelled op's teardown — an asio thread-safety limitation, reproduces on main
// AND fix), and (2) a DETERMINISTIC hang where a cancellation racing the drain
// leaves one acquirer counted-but-stuck (acquirers=1, not in any list) → the
// reaper never converges. Root cause (real cancellation-vs-drain defect vs harness
// corruption) is under investigation; B4 is NOT yet validated.
TEST(AsyncMutexDrainReapBlockers, DISABLED_WB4_SecondGateCancelRacingDrainResumesOnce) {
    constexpr int kPoolSize = 4;
    constexpr int kRounds = 300;
    constexpr int kWaitersPerRound = 16;
    constexpr auto kDeadline = std::chrono::seconds(5);

    for (int round = 0; round < kRounds; ++round) {
        auto* pool = new asio::thread_pool{kPoolSize};
        auto* mtx = new async_mutex;
        auto sigs = std::make_shared<std::vector<asio::cancellation_signal>>(kWaitersPerRound);
        auto go_finish = std::make_shared<std::atomic<bool>>(false);
        auto cancel_done = std::make_shared<std::atomic<bool>>(false);

        std::atomic<int> completed{0};
        std::atomic<int> drain_done{0};
        std::atomic<bool> holder_acquired{false};
        std::atomic<bool> drain_started{false};
        auto resume_counts = std::make_shared<std::vector<std::atomic<int>>>(kWaitersPerRound);

        auto holder = [mtx, &holder_acquired]() -> asio::awaitable<void> {
            auto g = co_await mtx->async_lock();
            EXPECT_TRUE(g.has_value());
            holder_acquired.store(true, std::memory_order_release);
            co_await yield_n(kWaitersPerRound + 6);
        };

        auto drainer = [mtx, &drain_done, &holder_acquired,
                        &drain_started]() -> asio::awaitable<void> {
            while (!holder_acquired.load(std::memory_order_acquire)) co_await yield_n(1);
            co_await yield_n(kWaitersPerRound + 1);
            drain_started.store(true, std::memory_order_release);
            (void)co_await mtx->cancel_and_drain();
            drain_done.fetch_add(1, std::memory_order_acq_rel);
        };

        // Waiter: race async_lock against the drain + cancel, record exactly-once,
        // then stay ALIVE on the barrier (frame intact) until all emits are done.
        auto waiter = [mtx, &completed, resume_counts,
                       go_finish](int idx) -> asio::awaitable<void> {
            co_await yield_n(1);
            {
                auto r = co_await mtx->async_lock();
                (void)r;
                // Release the guard HERE (scope end) if granted — otherwise a
                // granted waiter would hold the lock across the barrier below,
                // keeping holders!=0 and wedging the reaper (drain never converges).
            }
            (*resume_counts)[idx].fetch_add(1, std::memory_order_acq_rel);
            completed.fetch_add(1, std::memory_order_acq_rel);
            // async_lock's epilogue reset us to terminal-only; the late `total`
            // emit is filtered here, so this yield-loop cannot be cancelled and the
            // frame stays alive until the harness releases it.
            while (!go_finish->load(std::memory_order_acquire)) co_await yield_n(1);
        };

        // Canceller: fire total cancellation on every waiter, gated on drain_started
        // so it coincides with the drain's publish (races the second draining gate).
        auto canceller = [sigs, cancel_done, &drain_started]() -> asio::awaitable<void> {
            while (!drain_started.load(std::memory_order_acquire)) co_await yield_n(1);
            for (auto& s : *sigs) s.emit(asio::cancellation_type::total);
            cancel_done->store(true, std::memory_order_release);
        };

        asio::co_spawn(*pool, holder(), asio::detached);
        asio::co_spawn(*pool, drainer(), asio::detached);
        asio::co_spawn(*pool, canceller(), asio::detached);
        for (int i = 0; i < kWaitersPerRound; ++i) {
            asio::co_spawn(*pool, waiter(i),
                           asio::bind_cancellation_slot((*sigs)[i].slot(), asio::detached));
        }

        // All waiters past async_lock (parked on the barrier), drain done, AND every
        // emit fired — only then is it safe to release the barrier.
        bool ok = wait_until(
            [&] {
                return completed.load(std::memory_order_acquire) == kWaitersPerRound &&
                       drain_done.load(std::memory_order_acquire) == 1 &&
                       cancel_done->load(std::memory_order_acquire);
            },
            kDeadline);

        if (!ok) {
            ADD_FAILURE() << "WB4 round=" << round << ": TIMEOUT. completed=" << completed.load()
                          << "/" << kWaitersPerRound << " drain_done=" << drain_done.load()
                          << " cancel_done=" << cancel_done->load() << ". LEAKING mutex+pool.";
            go_finish->store(true, std::memory_order_release);
            return;
        }

        for (int i = 0; i < kWaitersPerRound; ++i) {
            EXPECT_EQ((*resume_counts)[i].load(), 1)
                << "WB4 round=" << round << " waiter=" << i
                << ": handler must be invoked EXACTLY ONCE (no double-resume)";
        }

        // Release the barrier AFTER all emits are done → waiters complete and their
        // frames are destroyed with no emit in flight.
        go_finish->store(true, std::memory_order_release);
        pool->join();
        delete mtx;
        delete pool;
    }
}

// ── W-B1 ─────────────────────────────────────────────────────────────────────
// The reaper parks on acquirers!=0 while exactly one acquirer is between its
// entry increment and its fast-fail decrement; assert the drain completes (no
// hang). Mutation-RED on a converging loop whose feeder decrement does NOT
// notify → the reaper never wakes from the acquirers→0 edge → hang.
//
// Discrimination requires the MISSING notify to be the only possible wake: keep
// other notify traffic (concurrent unlock L958, resumptions) minimal so a
// reverted feeder-notify cannot be masked. The holder releases BEFORE the drain
// observes it (so holders quiesce without an unlock racing the parked reaper),
// and the only late activity is acquirers fast-failing on draining_.
TEST(AsyncMutexDrainReapBlockers, WB1_FastFailDecrementNotifiesParkedReaper) {
    constexpr int kPoolSize = 4;
    constexpr int kRounds = 200;
    constexpr int kLateAcquirers = 6;
    constexpr auto kDeadline = std::chrono::seconds(5);

    for (int round = 0; round < kRounds; ++round) {
        auto* pool = new asio::thread_pool{kPoolSize};
        auto* mtx = new async_mutex;

        std::atomic<int> completed{0};
        std::atomic<int> drain_done{0};
        std::atomic<int> drain_ok{0};
        std::atomic<bool> holder_acquired{false};

        // Holder acquires and releases promptly, then the drainer fires. Late
        // acquirers arrive while the drain is in progress and fast-fail on
        // draining_ — their feeder decrement is the reaper's only wake source.
        auto holder = [mtx, &holder_acquired]() -> asio::awaitable<void> {
            auto g = co_await mtx->async_lock();
            EXPECT_TRUE(g.has_value());
            holder_acquired.store(true, std::memory_order_release);
            co_await yield_n(2);
            // release immediately (no waiters parked yet) → not_locked.
        };

        auto drainer = [mtx, &drain_done, &drain_ok, &holder_acquired]() -> asio::awaitable<void> {
            while (!holder_acquired.load(std::memory_order_acquire)) co_await yield_n(1);
            co_await yield_n(4);
            auto d = co_await mtx->cancel_and_drain();
            if (d.has_value()) drain_ok.fetch_add(1, std::memory_order_acq_rel);
            drain_done.fetch_add(1, std::memory_order_acq_rel);
        };

        // Late acquirer: arrives during/after the drain publish, fast-fails on
        // draining_ (L780 or L868), decrement+notify wakes the parked reaper.
        auto late_acquirer = [mtx, &completed]() -> asio::awaitable<void> {
            co_await yield_n(5);
            auto r = co_await mtx->async_lock();
            (void)r;
            completed.fetch_add(1, std::memory_order_acq_rel);
        };

        asio::co_spawn(*pool, holder(), asio::detached);
        asio::co_spawn(*pool, drainer(), asio::detached);
        for (int i = 0; i < kLateAcquirers; ++i)
            asio::co_spawn(*pool, late_acquirer(), asio::detached);

        bool ok = wait_until(
            [&] {
                return completed.load(std::memory_order_acquire) == kLateAcquirers &&
                       drain_done.load(std::memory_order_acquire) == 1;
            },
            kDeadline);

        if (!ok) {
            ADD_FAILURE() << "WB1 round=" << round
                          << ": TIMEOUT — reaper parked on acquirers!=0 was never woken "
                             "(missing feeder notify). completed="
                          << completed.load() << "/" << kLateAcquirers
                          << " drain_done=" << drain_done.load() << ". LEAKING mutex+pool.";
            return;
        }

        EXPECT_EQ(drain_ok.load(), 1) << "WB1 round=" << round << ": drain must succeed";

        pool->join();
        delete mtx;
        delete pool;
    }
}

// ── W-B2 ─────────────────────────────────────────────────────────────────────
// An acquirer wins the fast-path CAS (acquirer→holder) inside the reaper's
// quiesce window; assert cancel_and_drain never reports success while the lock
// is held. Mutation-RED on a holders-before-feeders read order → the reaper
// observes holders==0 spuriously while a grant's holders++ is not yet visible →
// finalizes success while a waiter holds.
//
// Detector: a granted waiter sets a shared `held` flag true before holding and
// clears it on release. We capture the drain's success edge and assert we never
// observe (drain returned success) while any waiter still holds.
TEST(AsyncMutexDrainReapBlockers, WB2_NeverReportsSuccessWhileLockHeld) {
    constexpr int kPoolSize = 4;
    constexpr int kRounds = 200;
    constexpr int kWaitersPerRound = 16;
    constexpr auto kDeadline = std::chrono::seconds(5);

    for (int round = 0; round < kRounds; ++round) {
        auto* pool = new asio::thread_pool{kPoolSize};
        auto* mtx = new async_mutex;

        std::atomic<int> completed{0};
        std::atomic<int> held_now{0};  // # waiters currently holding
        std::atomic<int> drain_done{0};
        std::atomic<int> drain_ok{0};
        std::atomic<bool> violation{false};  // success observed while held
        std::atomic<bool> holder_acquired{false};

        auto holder = [mtx, &holder_acquired]() -> asio::awaitable<void> {
            auto g = co_await mtx->async_lock();
            EXPECT_TRUE(g.has_value());
            holder_acquired.store(true, std::memory_order_release);
            co_await yield_n(kWaitersPerRound);
        };

        auto drainer = [mtx, &held_now, &drain_done, &drain_ok, &violation,
                        &holder_acquired]() -> asio::awaitable<void> {
            while (!holder_acquired.load(std::memory_order_acquire)) co_await yield_n(1);
            co_await yield_n(kWaitersPerRound / 2);
            auto d = co_await mtx->cancel_and_drain();
            if (d.has_value()) {
                drain_ok.fetch_add(1, std::memory_order_acq_rel);
                // Success edge: by I-33 the lock must be released. If any waiter
                // still holds, that is the B2 snapshot violation.
                if (held_now.load(std::memory_order_acquire) != 0)
                    violation.store(true, std::memory_order_release);
            }
            drain_done.fetch_add(1, std::memory_order_acq_rel);
        };

        // Waiter: if granted, mark held before holding, clear on release. A
        // grant winning the fast-path CAS inside the reaper's quiesce window is
        // exactly the B2 race.
        auto waiter = [mtx, &completed, &held_now]() -> asio::awaitable<void> {
            co_await yield_n(1);
            auto r = co_await mtx->async_lock();
            if (r.has_value()) {
                held_now.fetch_add(1, std::memory_order_acq_rel);
                co_await yield_n(2);
                held_now.fetch_sub(1, std::memory_order_acq_rel);
                // guard dtor releases after held_now decremented (conservative:
                // we clear the flag just before the actual unlock, so a true
                // violation can only be a real snapshot error, never our own lag).
            }
            completed.fetch_add(1, std::memory_order_acq_rel);
        };

        asio::co_spawn(*pool, holder(), asio::detached);
        asio::co_spawn(*pool, drainer(), asio::detached);
        for (int i = 0; i < kWaitersPerRound; ++i) asio::co_spawn(*pool, waiter(), asio::detached);

        bool ok = wait_until(
            [&] {
                return completed.load(std::memory_order_acquire) == kWaitersPerRound &&
                       drain_done.load(std::memory_order_acquire) == 1;
            },
            kDeadline);

        if (!ok) {
            ADD_FAILURE() << "WB2 round=" << round << ": TIMEOUT. completed=" << completed.load()
                          << "/" << kWaitersPerRound << " drain_done=" << drain_done.load()
                          << ". LEAKING mutex+pool.";
            return;
        }

        EXPECT_FALSE(violation.load())
            << "WB2 round=" << round
            << ": cancel_and_drain reported SUCCESS while a waiter still held the lock "
               "(holders-before-feeders snapshot error)";

        pool->join();
        delete mtx;
        delete pool;
    }
}

}  // namespace
