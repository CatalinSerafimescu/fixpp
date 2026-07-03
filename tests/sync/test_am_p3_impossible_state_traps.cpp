// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/sync/test_am_p3_impossible_state_traps.cpp
//
// 058-async-mutex-hardening — US4 / AM-P3-1 + AM-P3-2 impossible-state
// terminate-trap witnesses (tasks.md T023/T024/T026; research.md D-5/D-6;
// spec.md FR-005/FR-006).
//
// The three arms trapped by T023/T024 (the two chain-walk `granted`-record
// `else` arms in `unlock()`, and the resume runner's null-`attached_awaiter_`
// arm) are PROVABLY UNREACHABLE via any legitimate `async_lock()`/`unlock()`/
// `on_cancel()` sequence — see the trap-site comments in async_mutex.hpp for
// the full invariant argument (single-phase-transition for `granted`;
// single-schedule for `attached_awaiter_` nulling; `unlock()` is
// holder-serialized so no concurrent walk can race another). No stress/fuzz
// harness can land these arms because there is no code path that produces the
// precondition.
//
// To witness the traps as REAL, RUN code (not just source-level reasoning),
// each test here drives the mutex through a genuine, legitimate
// `async_lock()`/`unlock()` sequence to place a REAL record into the exact
// list position the trap arm inspects, then uses the
// `FIXPP_ASYNC_MUTEX_TEST_SEAM`-gated `test_seam_mutable_slot()` accessor to
// directly corrupt EXACTLY the one field the trap's precondition depends on
// (`phase_` for T023, `attached_awaiter_` for T024) — simulating the
// otherwise-impossible race/corruption — and then drives the REAL,
// unmodified `unlock()` / resume-runner code into the trap. This is fault
// injection at the single point the invariant claims can never occur, not a
// rewrite of the trap logic itself.
//
// Mutation check (recorded for SC-007): reverting T023/T024 (restoring the
// silent `else { cur = cur->next_; }` / silent drop) turns every test below
// from a `std::terminate()`/`assert` death into a DIFFERENT observable
// failure downstream (verified by hand: the abandoned `granted`-but-never-
// scheduled record's dangling list membership corrupts subsequent pool
// traffic) — RED either way, never a clean `std::exit(0)` PASS. Coroutine
// spawns below are INLINE lambdas passed directly to `asio::co_spawn`,
// mirroring the established pattern in test_destructor_release_death.cpp
// (a helper function that itself calls `co_spawn` on a locally-constructed
// coroutine risks the coroutine frame's HALO placement outliving the helper
// — keep spawn sites inline in the test body).
//
// ODR (research.md D-7, reused verbatim): async_mutex.hpp is header-only with
// inline out-of-line bodies; a FIXPP_ASYNC_MUTEX_TEST_SEAM-enabled TU MUST
// NOT link any fixpp object/library that also instantiates those bodies
// (fixpp_sync compiles atomic_shared_ptr.cpp, which includes async_mutex.hpp
// without the macro). This target is therefore standalone — header include
// path + asio::asio (header-only) + GTest only, ZERO fixpp library linkage,
// mirroring test_async_mutex_aba_interleave / test_pool_exhaustion_reuse.
//
// Coroutine caveat (same as test_pool_exhaustion_reuse.cpp): gtest's fatal
// ASSERT_* macros expand to a bare `return;`, invalid inside a C++20
// coroutine body. Coroutine lambdas below use only plain control flow /
// EXPECT_* (non-fatal); the death-test HELPER functions (not coroutines) use
// plain `if (...) { std::exit(0); }` fail-closed-to-RED guards instead of
// GTest ASSERT_* so a broken setup manifests as a visible EXPECT_DEATH
// failure (clean exit(0) is NOT a death), never a false PASS.
//
// io_context caveat: `io_context::poll_one()` marks the context "stopped"
// once it runs out of ready work; a SEPARATE later poll phase (after new
// work is posted, e.g. by spawning B/C after H already drained, or by
// `unlock()` posting a resume runner after an earlier drain loop went idle)
// requires `ioc.restart()` first or the later `poll_one()` calls silently
// process nothing (asio's documented run()/poll() + restart() contract).
//
// Oracle: research.md D-5/D-6; spec.md FR-005/FR-006, SC-004.

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>

#include <cstdlib>
#include <fixpp/core/sync/async_mutex.hpp>

namespace {

using fixpp::sync::async_mutex;
using fixpp::sync::detail::waiter_phase;
using fixpp::sync::detail::waiter_record;

// ─────────────────────────────────────────────────────────────────────────────
// T023 (research.md D-5) — chain-walk `granted`-record trap, RESIDUAL-list
// arm (the walk over `next_drain_head_` inside `unlock()`).
//
// Setup: an uncontended holder H, plus two contended waiters B and C queue
// behind it. H's `unlock()` grants B (FIFO order) and pushes the remaining
// waiter C onto the residual list `next_drain_head_` (still `queued`). We
// then corrupt whichever pool slot is STILL `queued` (that is C, by
// construction — B was just transitioned to `granted` by the real code) to
// `granted` directly, and call `unlock()` again (simulating B's own release,
// driven manually — matching the established manual-unlock() pattern in
// test_destructor_release_death.cpp): the real code walks the residual list,
// loads C's corrupted `granted` phase, and — since it is neither `queued` nor
// `cancelled` — falls into the T023 trap.
// ─────────────────────────────────────────────────────────────────────────────
void chain_walk_residual_granted_traps() {
    auto* mtx = new async_mutex{};
    asio::io_context ioc;

    // H: uncontended fast-path acquire (does not touch the waiter pool at
    // all — see async_lock()'s outer fast CAS). Leak the guard pointer; we
    // drive unlock() manually below to control timing precisely.
    auto holder = [mtx]() -> asio::awaitable<void> {
        auto g = co_await mtx->async_lock();
        static_cast<void>(g->release());
        co_return;
    };
    asio::co_spawn(ioc, holder(), asio::detached);
    // Drain H to completion FIRST (nothing else exists yet to contend with
    // it) so its uncontended fast-path grab is deterministic, before B/C are
    // even spawned.
    for (int i = 0; i < 8; ++i) ioc.poll_one();
    ioc.restart();  // see io_context caveat above

    auto waiter_b = [mtx]() -> asio::awaitable<void> {
        co_await asio::post(co_await asio::this_coro::executor, asio::use_awaitable);
        auto r = co_await mtx->async_lock();
        static_cast<void>(r.has_value());
    };
    auto waiter_c = [mtx]() -> asio::awaitable<void> {
        co_await asio::post(co_await asio::this_coro::executor, asio::use_awaitable);
        auto r = co_await mtx->async_lock();
        static_cast<void>(r.has_value());
    };
    asio::co_spawn(ioc, waiter_b(), asio::detached);
    asio::co_spawn(ioc, waiter_c(), asio::detached);

    for (int i = 0; i < 32; ++i) ioc.poll_one();
    // H holds; B and C are queued behind it (state_ LIFO: C -> B).

    mtx->unlock();  // H releases: grants B (FIFO), pushes C to next_drain_head_.

    // Find the still-`queued` pool slot (C) — robust to allocation order,
    // does not assume which of {0,1} is B vs C.
    waiter_record* target = nullptr;
    for (std::size_t idx = 0; idx < 2; ++idx) {
        auto* rec = mtx->test_seam_mutable_slot(idx);
        if (rec->phase_.load(std::memory_order_acquire) == waiter_phase::queued) {
            target = rec;
            break;
        }
    }
    if (target == nullptr) {
        // Setup did not reach the expected precondition — fail CLOSED to a
        // clean exit so EXPECT_DEATH reports a visible RED, never a
        // false-positive PASS.
        std::exit(0);
    }

    // Fault-inject the impossible state: C is corrupted to `granted` while
    // still linked into the residual list.
    target->phase_.store(waiter_phase::granted, std::memory_order_release);

    // B (the new holder) releases -> unlock() walks next_drain_head_ (C) ->
    // T023 trap.
    mtx->unlock();

    // Unreachable if the trap fired.
    std::exit(0);
}

// ─────────────────────────────────────────────────────────────────────────────
// T023 (research.md D-5) — chain-walk `granted`-record trap, FRESH
// LIFO-reversed-to-FIFO-list arm (the second walk inside `unlock()`, over
// `state_` once `next_drain_head_` is empty).
//
// Setup: an uncontended holder H, one contended waiter B queues behind it
// (`state_` LIFO: single node B). We corrupt B's phase to `granted` directly
// while it is still linked in `state_` (never yet touched by any walk), then
// call `unlock()`: no residual list exists, so the real code proceeds
// straight to the fresh LIFO->FIFO walk, loads B's corrupted `granted` phase,
// and falls into the T023 trap (sibling arm).
// ─────────────────────────────────────────────────────────────────────────────
void chain_walk_fresh_fifo_granted_traps() {
    auto* mtx = new async_mutex{};
    asio::io_context ioc;

    auto holder = [mtx]() -> asio::awaitable<void> {
        auto g = co_await mtx->async_lock();
        static_cast<void>(g->release());
        co_return;
    };
    asio::co_spawn(ioc, holder(), asio::detached);
    for (int i = 0; i < 8; ++i) ioc.poll_one();  // drain H's uncontended grab first
    ioc.restart();  // see io_context caveat above

    auto waiter_b = [mtx]() -> asio::awaitable<void> {
        co_await asio::post(co_await asio::this_coro::executor, asio::use_awaitable);
        auto r = co_await mtx->async_lock();
        static_cast<void>(r.has_value());
    };
    asio::co_spawn(ioc, waiter_b(), asio::detached);  // -> pool slot 0 (only contended waiter)

    for (int i = 0; i < 32; ++i) ioc.poll_one();
    // H holds; B is queued (state_ = ptr(B), single node).

    auto* b_rec = mtx->test_seam_mutable_slot(0);
    if (b_rec->phase_.load(std::memory_order_acquire) != waiter_phase::queued) {
        std::exit(0);  // setup didn't reach the expected precondition -> RED
    }

    // Fault-inject: B is corrupted to `granted` while still queued in state_.
    b_rec->phase_.store(waiter_phase::granted, std::memory_order_release);

    // H releases -> unlock(): next_drain_head_ empty (skip residual walk) ->
    // fresh LIFO->FIFO walk over state_ -> loads B's corrupted `granted`
    // phase -> T023 trap (fresh-walk sibling arm).
    mtx->unlock();

    std::exit(0);  // unreachable if the trap fired.
}

// ─────────────────────────────────────────────────────────────────────────────
// T024 (research.md D-6) — resume runner null-`attached_awaiter_` trap.
//
// Setup: an uncontended holder H, one contended waiter B queues behind it. H
// releases -> unlock() grants B via the real FIFO walk and calls
// schedule_record_resume(B), which increments in_flight_resumers_ and POSTS
// (but does not yet run) B's resume runner. Before polling the io_context
// again, we corrupt B's `attached_awaiter_` to null directly (the impossible
// state — legitimately it is nulled only AFTER this exact runner invokes the
// handler, strictly later than this point). Polling the io_context now runs
// the posted runner, which loads the corrupted null awaiter and falls into
// the T024 trap.
// ─────────────────────────────────────────────────────────────────────────────
void resume_runner_null_awaiter_traps() {
    auto* mtx = new async_mutex{};
    asio::io_context ioc;

    auto holder = [mtx]() -> asio::awaitable<void> {
        auto g = co_await mtx->async_lock();
        static_cast<void>(g->release());
        co_return;
    };
    asio::co_spawn(ioc, holder(), asio::detached);
    for (int i = 0; i < 8; ++i) ioc.poll_one();  // drain H's uncontended grab first
    ioc.restart();  // see io_context caveat above

    auto waiter_b = [mtx]() -> asio::awaitable<void> {
        co_await asio::post(co_await asio::this_coro::executor, asio::use_awaitable);
        auto r = co_await mtx->async_lock();
        static_cast<void>(r.has_value());
    };
    asio::co_spawn(ioc, waiter_b(), asio::detached);  // -> pool slot 0

    for (int i = 0; i < 32; ++i) ioc.poll_one();
    // H holds; B is queued.

    mtx->unlock();  // H releases -> grants B -> schedules (posts) B's resume runner.

    auto* b_rec = mtx->test_seam_mutable_slot(0);
    if (b_rec->phase_.load(std::memory_order_acquire) != waiter_phase::granted) {
        std::exit(0);  // setup didn't reach the expected precondition -> RED
    }
    if (b_rec->attached_awaiter_.load(std::memory_order_acquire) == nullptr) {
        std::exit(0);  // already null for an unrelated reason -> RED, not a false PASS
    }

    // Fault-inject: null the awaiter BEFORE the posted runner ever runs.
    b_rec->attached_awaiter_.store(nullptr, std::memory_order_release);

    ioc.restart();  // see io_context caveat above (unlock() just posted the runner)

    // Run the posted runner -> loads the corrupted null awaiter -> T024 trap.
    for (int i = 0; i < 8; ++i) ioc.poll_one();

    std::exit(0);  // unreachable if the trap fired.
}

}  // namespace

// Regex: "" (matches any death), NOT "terminate called" — the trap idiom is
// `assert(false && "...") + std::terminate()` (research.md D-5/D-6): on a
// debug build (NDEBUG unset, e.g. this linux-clang-debug lane) the `assert`
// fires FIRST and aborts via a libc assertion-failure banner (never reaching
// the `std::terminate()` line); on a release build (NDEBUG defined) the
// `assert` compiles out entirely and `std::terminate()` produces libstdc++'s
// "terminate called" banner instead. Both are valid discharges of the SAME
// trap per FR-005/FR-006 (loud in BOTH debug AND release) — matching "" (the
// same pattern test_destructor_release_death.cpp's first two death tests
// use) accepts either without over-claiming which one text-matches. The
// setup's `if (... == nullptr/expected) std::exit(0)` fail-closed guards
// (see the helper functions above) are what rules out a false-positive
// PASS: any setup failure exits CLEANLY (status 0, not a death), which
// EXPECT_DEATH's default predicate (ExitedUnsuccessfully / killed-by-signal)
// rejects regardless of the "" regex.
TEST(AmP3ImpossibleStateTraps, ChainWalkResidualGrantedTraps) {
    EXPECT_DEATH(chain_walk_residual_granted_traps(), "");
}

TEST(AmP3ImpossibleStateTraps, ChainWalkFreshFifoGrantedTraps) {
    EXPECT_DEATH(chain_walk_fresh_fifo_granted_traps(), "");
}

TEST(AmP3ImpossibleStateTraps, ResumeRunnerNullAwaiterTraps) {
    EXPECT_DEATH(resume_runner_null_awaiter_traps(), "");
}
