// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/sync/test_async_mutex_aba_interleave.cpp
//
// 058-async-mutex-hardening — D-7 deterministic ABA/reuse-race witnesses
// (research.md D-1, D-7; tasks.md T007/T008/T012).
//
// Both witnesses drive the REAL `waiter_pool_free_` pop/push through the
// public async_lock() API (no friend access, no header changes beyond the
// two seam call-sites T007/T008 wire in async_mutex.hpp). Genuinely
// multi-threaded: the seam-installed hook blocks the pinned thread with a
// `std::binary_semaphore` (FR-012 — no std::mutex/condition_variable).
//
//   - Part 2 (T008, `pop_pre_link_load`): T1 loads `free_head` and blocks
//     BEFORE reading `free_head->next_`. T2 independently pops the SAME
//     entry (T1 hasn't touched the atomic yet) and placement-news a fresh
//     `waiter_record` into it — the write that races T1's unblocked plain
//     `next_` read. TSan-RED against the pre-fix member-`next_` shape
//     (research.md D-1); clean once the link moves to the mutex-lifetime
//     slot `free_link` (T010).
//   - Part 1 (T007, `pop_pre_cas`): T1 loads `free_head` (P) and its stale
//     `next` (Q), then blocks before the CAS. T2 pops P, pops Q, then
//     cancels its P-waiter so `release_ref` pushes P back onto the free
//     list with a DIFFERENT next (recreating the ABA). T1's stale CAS
//     succeeds against the tagless head, corrupting the free list to point
//     at Q while Q is still a live, queued waiter — assertion-RED (not
//     TSan) against the pre-fix code per D-7 (the CAS window is
//     TSan-invisible); clean once the generation tag defeats it (T009).
//
// Oracle: research.md D-7 — "no slot returned twice / no parked record
// clobbered / exact expected completion count", TSan-clean for part 2.

#include <gtest/gtest.h>

#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <fixpp/core/sync/async_mutex.hpp>
#include <future>
#include <optional>
#include <semaphore>
#include <thread>

#include "sync_test_support.hpp"

namespace {

using fixpp::core::error;
using fixpp::sync::async_lock_guard;
using fixpp::sync::async_mutex;
using fixpp::sync::detail::async_mutex_seam_phase;
using fixpp::sync::test::yield_n;

// ─────────────────────────────────────────────────────────────────────────────
// Part 2 (T008) — pop_pre_link_load reuse race.
//
// Shared seam-hook state. `g_ctx` is set/cleared per-TEST (this file has no
// concurrent TEST execution — gtest runs tests sequentially within a binary).
// ─────────────────────────────────────────────────────────────────────────────

struct LinkLoadRaceCtx {
    std::atomic<std::thread::id> t1_tid{};
    std::atomic<bool> t1_already_parked{false};
    std::atomic<bool> release_already_fired{false};
    std::binary_semaphore t1_parked{0};
    std::binary_semaphore t1_release{0};
    std::binary_semaphore other_entered{0};
};

LinkLoadRaceCtx* g_link_load_ctx = nullptr;

// Pinned at `pop_pre_link_load` — BEFORE `free_head->next_` is read. T1
// (identified by thread id) parks exactly once; any OTHER thread's entry at
// this phase (T2, popping the same still-unmodified head) is the trigger
// that releases T1 — never T2's write-completion — so T1's subsequent read
// and T2's placement-new write race with no happens-before edge between
// them (feedback_forged_token_before_real_tag_self_heals_witness sibling
// concern: a release keyed off T2's *write* would serialize the accesses
// and silently defeat the witness).
void link_load_race_hook(async_mutex_seam_phase phase) noexcept {
    if (phase != async_mutex_seam_phase::pop_pre_link_load) return;
    auto* ctx = g_link_load_ctx;
    if (ctx == nullptr) return;

    if (std::this_thread::get_id() == ctx->t1_tid.load(std::memory_order_acquire)) {
        if (!ctx->t1_already_parked.exchange(true, std::memory_order_acq_rel)) {
            ctx->t1_parked.release();
            ctx->t1_release.acquire();
        }
        return;
    }

    if (!ctx->release_already_fired.exchange(true, std::memory_order_acq_rel)) {
        ctx->other_entered.release();
        ctx->t1_release.release();
    }
}

TEST(AsyncMutexAbaInterleave, PopPreLinkLoadReuseRaceIsDataRace) {
    LinkLoadRaceCtx ctx;
    g_link_load_ctx = &ctx;
    fixpp::sync::detail::async_mutex_test_seam = &link_load_race_hook;
    struct SeamReset {
        ~SeamReset() {
            fixpp::sync::detail::async_mutex_test_seam = nullptr;
            g_link_load_ctx = nullptr;
        }
    } seam_reset;

    asio::io_context ioc;
    async_mutex mtx;

    // ---- Setup: a "holder1" acquires, Wa bump-allocates a waiter record and
    // parks (contended), holder1 cancels Wa from within its own body (the
    // test_race_cancel_during_resume idiom). Cancellation alone does NOT
    // reclaim Wa's record — on_cancel only marks it `cancelled` and resolves
    // Wa's own future; the record's *list-membership* ref is released only
    // when a subsequent unlock() drain walk steps over it (async_mutex.hpp
    // unlock(), the `phase == cancelled` arms). So holder1 explicitly
    // unlocks (reassigning its guard to a disengaged one), which drains the
    // now-empty-of-live-waiters chain, steps over Wa's cancelled record, and
    // pushes it onto the free list — THEN holder1 re-acquires (fast path,
    // now the sole entrant) and hands that second guard to `holder_guard`
    // (surviving past this coroutine's completion) so the free list holds
    // exactly one entry while the mutex is held again for the T1/T2 race.
    std::optional<async_lock_guard> holder_guard;
    asio::cancellation_signal sig_wa;
    bool wa_aborted = false;

    auto waiter_a = [&]() -> asio::awaitable<void> {
        auto r = co_await mtx.async_lock();
        if (!r.has_value()) {
            EXPECT_EQ(r.error(), error::sync_lock_aborted);
            wa_aborted = true;
        }
    };

    auto holder = [&]() -> asio::awaitable<void> {
        auto g1 = co_await mtx.async_lock();
        EXPECT_TRUE(g1.has_value());
        if (!g1.has_value()) co_return;
        co_await yield_n(3);  // let Wa reach the parked/contended state
        sig_wa.emit(asio::cancellation_type::total);
        co_await yield_n(3);  // let Wa's own async_lock() coroutine complete (aborted)

        *g1 = async_lock_guard{};  // move-assign disengaged -> unlocks; drain reclaims Wa's record
        co_await yield_n(1);

        auto g2 = co_await mtx.async_lock();  // fast path: sole entrant now
        EXPECT_TRUE(g2.has_value());
        if (!g2.has_value()) co_return;
        holder_guard.emplace(std::move(*g2));  // survives past this coroutine's completion
    };

    auto fh = asio::co_spawn(ioc, holder(), asio::use_future);
    auto fwa = asio::co_spawn(ioc, waiter_a(),
                               asio::bind_cancellation_slot(sig_wa.slot(), asio::use_future));
    ioc.run();
    fh.get();
    fwa.get();
    ASSERT_TRUE(wa_aborted);
    ASSERT_TRUE(holder_guard.has_value());
    // Free list now holds exactly Wa's reclaimed record; holder holds again.

    // ---- T1: dedicated thread/io_context, pinned at pop_pre_link_load. ----
    asio::io_context ioc_a;
    bool t1_ok = false;
    auto t1_coro = [&]() -> asio::awaitable<void> {
        auto r = co_await mtx.async_lock();
        t1_ok = r.has_value();
    };
    auto ft1 = asio::co_spawn(ioc_a, t1_coro(), asio::use_future);
    std::thread thread_a([&] {
        ctx.t1_tid.store(std::this_thread::get_id(), std::memory_order_release);
        ioc_a.run();  // blocks through T1's whole lifecycle (asio tracks the
                       // spawned coroutine as outstanding work until it completes).
    });

    ctx.t1_parked.acquire();  // T1 has loaded free_head and is blocked pre-read.

    // ---- T2: dedicated thread/io_context, reuses the SAME entry. Its own
    // pop_pre_link_load entry (any non-T1 thread) releases T1 directly from
    // inside the hook — see link_load_race_hook.
    asio::io_context ioc_b;
    bool t2_ok = false;
    auto t2_coro = [&]() -> asio::awaitable<void> {
        auto r = co_await mtx.async_lock();
        t2_ok = r.has_value();
    };
    auto ft2 = asio::co_spawn(ioc_b, t2_coro(), asio::use_future);
    std::thread thread_b([&] { ioc_b.run(); });

    ctx.other_entered.acquire();  // T2 confirmed entry (T1 is now released too).

    // Both T1 and T2 are now committed to their (racing) pop sequences and
    // will each park behind the still-held lock. Release the holder so both
    // eventually get granted and their io_context::run() calls can return.
    holder_guard.reset();

    thread_a.join();
    thread_b.join();
    ft1.get();
    ft2.get();
    EXPECT_TRUE(t1_ok);
    EXPECT_TRUE(t2_ok);
}

// ─────────────────────────────────────────────────────────────────────────────
// Part 1 (T007) — pop_pre_cas ABA-of-the-CAS.
//
// T1 caches (free_head=P, next=Q) then blocks BEFORE the CAS. T2
// (single-threaded, orchestrated via explicit `ioc.poll()` pumps so nothing
// races the release below) pops P, pops Q, cancels the P-waiter, then drains
// (steps over the cancelled P-waiter, pushing P back onto the free list with
// a next of EMPTY — Q is still held by the not-yet-resumed Q-waiter). T1's
// stale CAS(expected=P, desired=Q) then matches the current head (P) and
// installs the STALE `next=Q` as the new free-list head — corrupting it to
// alias Q, which is still live. `ioc` (T2's io_context) is driven ONLY via
// explicit `poll()` calls from the main thread — no background thread — so
// nothing can process Q's already-posted grant/resume until we choose to.
// ─────────────────────────────────────────────────────────────────────────────

struct PreCasAbaCtx {
    std::atomic<std::thread::id> t1_tid{};
    std::atomic<bool> t1_already_parked{false};
    std::binary_semaphore t1_parked{0};
    std::binary_semaphore t1_release{0};
};

PreCasAbaCtx* g_pre_cas_ctx = nullptr;

// Pinned at `pop_pre_cas` — AFTER the free-link load, BEFORE the CAS. Only
// T1 (identified by thread id) ever blocks here; every other thread's
// firing (including T1's own harmless retries once already parked) is a
// no-op. T1 is released by an explicit main-thread signal (see the TEST
// body) rather than by another thread's entry, because T007's exploit needs
// a specific *sequenced* state (P reclaimed, Q still outstanding) that only
// the orchestrating thread can set up deterministically.
void pre_cas_aba_hook(async_mutex_seam_phase phase) noexcept {
    if (phase != async_mutex_seam_phase::pop_pre_cas) return;
    auto* ctx = g_pre_cas_ctx;
    if (ctx == nullptr) return;
    if (std::this_thread::get_id() != ctx->t1_tid.load(std::memory_order_acquire)) return;
    if (ctx->t1_already_parked.exchange(true, std::memory_order_acq_rel)) return;  // park once
    ctx->t1_parked.release();
    ctx->t1_release.acquire();
}

TEST(AsyncMutexAbaInterleave, PopPreCasAbaCorruptionDetected) {
    PreCasAbaCtx ctx;
    g_pre_cas_ctx = &ctx;
    fixpp::sync::detail::async_mutex_test_seam = &pre_cas_aba_hook;
    struct SeamReset {
        ~SeamReset() {
            fixpp::sync::detail::async_mutex_test_seam = nullptr;
            g_pre_cas_ctx = nullptr;
        }
    } seam_reset;

    asio::io_context ioc;
    async_mutex mtx;

    // ---- Setup: build free_list = [P(=Wb) -> Q(=Wa)]. Wa and Wb both
    // bump-allocate (spawned+parked before either is cancelled, so neither
    // touches the still-empty free list), then are both cancelled and the
    // records reclaimed via a holder unlock+reacquire cycle (same idiom as
    // part 2's setup, extended to two waiters). Drain processes the wait
    // chain in FIFO (arrival) order, so Wa (queued first) is reclaimed
    // (pushed) first, then Wb — giving head=Wb(P), P->next_=Wa(Q).
    std::optional<async_lock_guard> holder_guard;
    asio::cancellation_signal sig_wa;
    asio::cancellation_signal sig_wb;
    bool wa_aborted = false;
    bool wb_aborted = false;

    auto waiter_a = [&]() -> asio::awaitable<void> {
        auto r = co_await mtx.async_lock();
        if (!r.has_value()) {
            EXPECT_EQ(r.error(), error::sync_lock_aborted);
            wa_aborted = true;
        }
    };
    auto waiter_b = [&]() -> asio::awaitable<void> {
        auto r = co_await mtx.async_lock();
        if (!r.has_value()) {
            EXPECT_EQ(r.error(), error::sync_lock_aborted);
            wb_aborted = true;
        }
    };

    auto holder = [&]() -> asio::awaitable<void> {
        auto g1 = co_await mtx.async_lock();
        EXPECT_TRUE(g1.has_value());
        if (!g1.has_value()) co_return;
        co_await yield_n(3);  // let Wa AND Wb both bump-allocate + park
        sig_wa.emit(asio::cancellation_type::total);
        sig_wb.emit(asio::cancellation_type::total);
        co_await yield_n(3);  // let both cancellations resolve (results set, coroutines complete)

        *g1 = async_lock_guard{};  // unlock() -> drain reclaims Wa then Wb (FIFO)
        co_await yield_n(1);

        auto g2 = co_await mtx.async_lock();  // fast path: sole entrant now
        EXPECT_TRUE(g2.has_value());
        if (!g2.has_value()) co_return;
        holder_guard.emplace(std::move(*g2));
    };

    auto fh = asio::co_spawn(ioc, holder(), asio::use_future);
    auto fwa = asio::co_spawn(ioc, waiter_a(),
                               asio::bind_cancellation_slot(sig_wa.slot(), asio::use_future));
    auto fwb = asio::co_spawn(ioc, waiter_b(),
                               asio::bind_cancellation_slot(sig_wb.slot(), asio::use_future));
    ioc.run();
    fh.get();
    fwa.get();
    fwb.get();
    ASSERT_TRUE(wa_aborted);
    ASSERT_TRUE(wb_aborted);
    ASSERT_TRUE(holder_guard.has_value());
    // Free list now holds exactly [P(Wb) -> Q(Wa)]; holder holds again.

    // ---- T1: dedicated thread/io_context, pinned at pop_pre_cas with its
    // stale (free_head=P, next=Q) pair cached.
    asio::io_context ioc_a;
    bool t1_ok = false;
    auto t1_coro = [&]() -> asio::awaitable<void> {
        auto r = co_await mtx.async_lock();
        t1_ok = r.has_value();
    };
    auto ft1 = asio::co_spawn(ioc_a, t1_coro(), asio::use_future);
    std::thread thread_a([&] {
        ctx.t1_tid.store(std::this_thread::get_id(), std::memory_order_release);
        ioc_a.run();
    });

    ctx.t1_parked.acquire();  // T1 has cached (P, next=Q) and is blocked pre-CAS.

    // ---- T2: pop P, pop Q, cancel the P-waiter — driven ONLY by explicit
    // `ioc.poll()` pumps from this thread. `ioc` is otherwise completely
    // inert (no background thread), so nothing here can race T1's eventual
    // release below. Bounded at 100000 iterations as a belt-and-suspenders
    // guard against an unexpected runaway (never observed; each pump here
    // drains in 1 iteration) rather than looping forever.
    auto pump = [&]() {
        ioc.restart();
        int iter = 0;
        while (ioc.poll() > 0) {
            if (++iter > 100000) break;
        }
    };

    asio::cancellation_signal sig_t2a;
    bool t2a_aborted = false;
    auto t2a_coro = [&]() -> asio::awaitable<void> {
        auto r = co_await mtx.async_lock();
        if (!r.has_value()) {
            EXPECT_EQ(r.error(), error::sync_lock_aborted);
            t2a_aborted = true;
        }
    };
    bool t2b_ok = false;
    auto t2b_coro = [&]() -> asio::awaitable<void> {
        auto r = co_await mtx.async_lock();
        t2b_ok = r.has_value();
    };

    auto ft2a = asio::co_spawn(ioc, t2a_coro(),
                                asio::bind_cancellation_slot(sig_t2a.slot(), asio::use_future));
    pump();  // T2a pops P (T1 has only READ it so far) and parks (contended).

    auto ft2b = asio::co_spawn(ioc, t2b_coro(), asio::use_future);
    pump();  // T2b pops Q and parks (contended). Free list is now EMPTY.

    sig_t2a.emit(asio::cancellation_type::total);
    pump();  // T2a's cancellation resolves; its coroutine completes (aborted).
    ASSERT_TRUE(ft2a.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    ft2a.get();
    EXPECT_TRUE(t2a_aborted);

    // unlock(): drain steps over T2a's now-cancelled record, reclaiming it
    // (pushed with next_ = the CURRENT head, which is EMPTY — Q is still
    // outstanding), and — in the SAME call — marks T2b `granted` and POSTS
    // its resume. `ioc` is not being pumped right now, so that post is
    // inert until we explicitly pump it again below.
    *holder_guard = async_lock_guard{};
    holder_guard.reset();

    // Release T1 and get a HARD confirmation that its synchronous
    // CAS+placement-new+park sequence has fully executed before `ioc` is
    // pumped again. asio's single-threaded handler execution is
    // non-preemptive and FIFO: a probe posted onto `ioc_a` from this thread
    // AFTER releasing T1 cannot run until T1's CURRENTLY-EXECUTING handler
    // (the one blocked inside the seam hook, now unblocked) returns control
    // to `ioc_a`'s run loop — i.e., until the whole CAS+park sequence is
    // done. This is a real synchronization point, not a timing guess.
    ctx.t1_release.release();
    std::promise<void> t1_committed;
    auto t1_committed_future = t1_committed.get_future();
    asio::post(ioc_a.get_executor(), [&t1_committed]() { t1_committed.set_value(); });
    ASSERT_EQ(t1_committed_future.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    t1_committed_future.get();

    // T1's CAS has now definitely executed — either it landed on the stale
    // (P, next=Q) pair (the ABA: free-list head corrupted to alias the
    // still-live Q) or it failed the compare and fell back to a fresh
    // read/bump-alloc. Only NOW let T2b's already-posted resume (and
    // everything downstream) run.
    pump();
    ASSERT_TRUE(ft2b.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    ft2b.get();
    EXPECT_TRUE(t2b_ok);

    // T1w (queued behind T2b's grant, on `ioc_a`/thread_a) must ALSO fully
    // resolve — and release its guard — before the exploit phase assumes
    // the mutex is free for a fast-path re-acquire. `ft2b` becoming ready
    // only proves T2b's OWN coroutine (and its unlock()) ran; T1w's grant
    // (posted cross-executor onto `ioc_a`) is independently driven by
    // thread_a and can still be in flight at this point — waiting on it
    // here (bounded) avoids a hang below on a holder2 acquire that would
    // otherwise silently take the CONTENDED path instead of the fast path.
    ASSERT_TRUE(ft1.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    ft1.get();
    EXPECT_TRUE(t1_ok);
    thread_a.join();

    // ---- Oracle: acquire a fresh holder (fast path — the mutex is empty
    // again) and issue three more contended calls under it. Post-fix
    // (generation-tagged CAS) the free list is a clean, ABA-safe LIFO and
    // all three resolve. Pre-fix, if T1's stale CAS corrupted the list into
    // a cycle (a self-loop at Q, or an alternating Q<->P 2-cycle — the
    // exact shape depends on grant order, so three pops are enough to force
    // a repeat regardless), at least two of the three pops alias the SAME
    // physical slot: the loser's `waiter_record` gets placement-new'd over
    // by the winner, silently losing the loser's continuation — its future
    // never becomes ready. A bounded `wait_for` turns that into a clean,
    // discriminating count instead of a hang.
    std::optional<async_lock_guard> holder2_guard;
    auto holder2 = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx.async_lock();
        EXPECT_TRUE(g.has_value());
        if (!g.has_value()) co_return;
        holder2_guard.emplace(std::move(*g));
    };
    auto fh2 = asio::co_spawn(ioc, holder2(), asio::use_future);
    pump();
    ASSERT_TRUE(fh2.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    fh2.get();
    ASSERT_TRUE(holder2_guard.has_value());

    std::array<bool, 3> exploit_ok{false, false, false};
    auto make_exploit = [&](bool& flag) -> asio::awaitable<void> {
        auto r = co_await mtx.async_lock();
        flag = r.has_value();
    };
    auto fe0 = asio::co_spawn(ioc, make_exploit(exploit_ok[0]), asio::use_future);
    pump();
    auto fe1 = asio::co_spawn(ioc, make_exploit(exploit_ok[1]), asio::use_future);
    pump();
    auto fe2 = asio::co_spawn(ioc, make_exploit(exploit_ok[2]), asio::use_future);
    pump();

    holder2_guard.reset();  // release -> drain grants the exploit waiters in turn.
    pump();

    int ready_count = 0;
    std::future<void>* exploit_futures[] = {&fe0, &fe1, &fe2};
    for (auto* f : exploit_futures) {
        if (f->wait_for(std::chrono::seconds(2)) == std::future_status::ready) {
            f->get();
            ++ready_count;
        }
    }
    EXPECT_EQ(ready_count, 3)
        << "expected all 3 post-corruption waiters to resolve; a smaller count means the "
           "tagless CAS aliased two of them onto the same physical waiter_record slot "
           "(research.md D-1 ABA — a stale successor was installed as the free-list head)";
}

}  // namespace
