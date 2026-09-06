// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/sync/test_destructor_release_death.cpp — Seam #5
//
// Destructor terminate precondition — release linkage death test.
//
// The async_mutex destructor fires std::terminate() in BOTH debug AND release
// if the mutex is held or has live waiters at destruction time. This seam
// exercises the precondition in both its triggering and non-triggering forms.
//
// Oracle: [2f §9 #5] — "Destructor terminate precondition (RC#3)".
//         [2f §4.7] — ~async_mutex() fires terminate if held or waiters present.
// RC#3: std::terminate() enforced in BOTH debug and release builds.
//
// Positive control: a properly drained-then-destroyed mutex does NOT terminate.

#include <gtest/gtest.h>

#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/cancellation_type.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fixpp/core/sync/async_mutex.hpp>
#include <memory>

#include "support/pump_until_ready.hpp"
#include "sync/sync_test_support.hpp"

namespace {

using fixpp::core::error;
using fixpp::sync::async_lock_guard;
using fixpp::sync::async_mutex;
using fixpp::sync::expected_t;

using fixpp::sync::test::yield_n;

// ─────────────────────────────────────────────────────────────────────────────
// Portable death-banner marker. The default std::terminate() banner is NOT
// portable — "terminate called ..." on libstdc++, "libc++abi: terminating" on
// libc++, and neither on MSVC — so matching it makes EXPECT_DEATH pass only on
// the libstdc++ lane (the trap that surfaced when Tier-2/Tier-3 first ran,
// 2026-07-03). The async_mutex destructor guard fires via the bare
// std::terminate() idiom (D-5) on ALL platforms; each death-test child installs
// its OWN terminate handler that emits a fixed, fixpp-owned marker then
// abort()s. Matching this marker is portable BY CONSTRUCTION (std::terminate
// dispatches to the current handler on every conforming stdlib, incl. MSVC) and
// still excludes an incidental SIGABRT (glibc malloc-corruption abort bypasses
// std::terminate → the handler is NOT invoked → the marker is absent → RED),
// preserving the original "exclude an unrelated crash" discriminator intent.
inline constexpr const char* kGuardTerminateMarker = "FIXPP_ASYNC_MUTEX_GUARD_TERMINATE";

inline void install_guard_terminate_marker() {
    std::set_terminate([] {
        std::fputs(kGuardTerminateMarker, stderr);
        std::fputc('\n', stderr);
        std::fflush(stderr);
        std::abort();
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper functions for death-test children.
// EXPECT_DEATH forks a child process and runs the statement there.
// We use static helper functions to avoid issues with braced initializer lists
// inside the macro argument (preprocessor limitation).
// ─────────────────────────────────────────────────────────────────────────────

// Trigger: holder acquires, waiter parks, mutex is destroyed → terminate.
static void destroy_with_live_waiter() {
    install_guard_terminate_marker();
    auto* mtx = new async_mutex{};
    asio::io_context ioc;

    // Holder: acquires and holds — leaks the guard intentionally (death child).
    auto holder_coro = [mtx]() -> asio::awaitable<void> {
        auto g = co_await mtx->async_lock();
        // Release the guard's ownership WITHOUT calling unlock().
        // This keeps the mutex in the "held" state.
        static_cast<void>(g->release());
        co_return;
    };

    // Waiter: parks behind the holder.
    auto waiter_coro = [mtx]() -> asio::awaitable<void> {
        co_await asio::post(co_await asio::this_coro::executor, asio::use_awaitable);
        // Suspends here — never resumes inside the death child.
        auto r = co_await mtx->async_lock();
        static_cast<void>(r.has_value());
    };

    // Run enough steps so the holder acquires and the waiter parks.
    asio::co_spawn(ioc, holder_coro(), asio::detached);
    asio::co_spawn(ioc, waiter_coro(), asio::detached);
    // Run two polling steps: holder grabs lock on step 1; waiter parks on step 2.
    for (int i = 0; i < 16; ++i) ioc.poll_one();

    // Destroy the mutex while a waiter is live => std::terminate().
    delete mtx;

    // Unreachable in normal flow (terminate fires above).
    std::exit(0);
}

// Trigger: holder acquires and never unlocks; mutex destroyed → terminate.
static void destroy_while_held() {
    install_guard_terminate_marker();
    auto* mtx = new async_mutex{};
    asio::io_context ioc;

    bool acquired = false;
    auto holder_coro = [mtx, &acquired]() -> asio::awaitable<void> {
        auto g = co_await mtx->async_lock();
        acquired = g.has_value();
        // Release the guard pointer WITHOUT unlocking. The mutex stays held.
        static_cast<void>(g->release());
        co_return;
    };

    asio::co_spawn(ioc, holder_coro(), asio::detached);
    ioc.run();
    // acquired == true; mutex is now held (lock granted, never unlocked).
    // Destroying it must terminate.
    delete mtx;
    std::exit(0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Death test 1: destroy a mutex with a live waiter → terminate.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamDestructorReleaseDeath, DestroyWithLiveWaiterTerminates) {
    EXPECT_DEATH(destroy_with_live_waiter(), kGuardTerminateMarker);
}

// ─────────────────────────────────────────────────────────────────────────────
// Death test 2: destroy a mutex that is held (no waiters) → terminate.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamDestructorReleaseDeath, DestroyWhileHeldTerminates) {
    EXPECT_DEATH(destroy_while_held(), kGuardTerminateMarker);
}

// ─────────────────────────────────────────────────────────────────────────────
// 058 T013/T014/T041 — research.md D-3 destructor-guard extension witnesses.
//
// The CURRENT destructor (`async_mutex.hpp:695-700`) checks only
// `state_ != not_locked` and `next_drain_head_ != nullptr`. It does NOT check
// `in_flight_resumers_` (D-3's load-bearing barrier: incremented BEFORE every
// posted resume, decremented as the LAST statement of the resume runner,
// AFTER the runner has finished dereferencing the mutex). T013/T014 construct
// the masked state deterministically, single-threaded, with NO seam and NO
// sleep(): schedule a resume (increments in_flight_resumers_, posts the
// delivery runner via asio::post) and then, WITHOUT ever polling the
// io_context again, drive state_/next_drain_head_ back to the "looks fully
// drained" values (not_locked / empty) via direct unlock() calls. The posted
// runner is left permanently un-run inside these death-test children (the
// child calls std::exit(0) right after `delete mtx`), so no UAF is actually
// triggered in-process — the mutex's own precondition check is what we are
// probing, not the downstream dereference.
//
// RED-for-the-right-reason discriminator: pre-fix, NEITHER OR-term is true
// at the manufactured destroy point, so `~async_mutex()` does not call
// std::terminate() at all — the child runs to `std::exit(0)` (a CLEAN exit).
// `EXPECT_DEATH` requires `ExitedUnsuccessfully` (nonzero exit OR killed by
// a signal); a clean exit(0) fails that predicate — RED, and RED because the
// guard did not fire (not because of an unrelated crash the matcher happened
// to accept). The death-banner matcher is the fixpp-owned `kGuardTerminateMarker`
// (installed by install_guard_terminate_marker in each child) rather than the
// stdlib's default terminate banner, which is NOT portable — see that helper's
// note — additionally excluding an incidental SIGABRT (e.g. glibc
// malloc-corruption abort) from being accepted as a false-positive match,
// WITHOUT claiming the marker identifies which OR-term fired (all three arms
// share the bare `std::terminate()` idiom post-T017 — D-5 — so stderr is
// byte-identical across arms; the discriminator is the manufactured
// PRECONDITION STATE, not the message text).
// ─────────────────────────────────────────────────────────────────────────────

// T013: cancel-delivered-then-destroy.
// A waiter is cancelled (on_cancel -> schedule_record_resume: in_flight_
// resumers_ == 1, its abort-delivery runner posted but never run). The
// holder's manual unlock() then walks the LIFO, drops the cancelled node,
// and restores state_ == not_locked / next_drain_head_ == nullptr -- the
// CURRENT guard reads only those two and sees a "fully drained" mutex.
static void cancel_delivered_then_destroy() {
    install_guard_terminate_marker();
    auto* mtx = new async_mutex{};
    asio::io_context ioc;
    asio::cancellation_signal cancel_sig;

    // Holder: acquires and releases guard ownership WITHOUT calling
    // unlock() -- we unlock manually below to control timing precisely
    // relative to the cancellation.
    auto holder_coro = [mtx]() -> asio::awaitable<void> {
        auto g = co_await mtx->async_lock();
        static_cast<void>(g->release());
        co_return;
    };

    // Waiter: parks behind the holder; its cancellation slot is bound via
    // the co_spawn token below so cancel_sig.emit() reaches on_cancel().
    auto waiter_coro = [mtx]() -> asio::awaitable<void> {
        co_await asio::post(co_await asio::this_coro::executor, asio::use_awaitable);
        auto r = co_await mtx->async_lock();
        static_cast<void>(r.has_value());
    };

    asio::co_spawn(ioc, holder_coro(), asio::detached);
    asio::co_spawn(ioc, waiter_coro(),
                   asio::bind_cancellation_slot(cancel_sig.slot(), asio::detached));

    // Holder acquires (fast path, synchronous); waiter posts once then
    // parks on the LIFO (registers on_cancel via inherited_slot.assign).
    for (int i = 0; i < 16; ++i) ioc.poll_one();

    // Fire cancellation SYNCHRONOUSLY (not via ioc.poll): on_cancel() marks
    // the waiter cancelled and calls schedule_record_resume(), which
    // increments in_flight_resumers_ and posts the abort-delivery runner.
    // asio::post() never runs its target inline -- the runner stays queued.
    cancel_sig.emit(asio::cancellation_type::total);

    // Unlock manually (bypassing the leaked guard): walks the LIFO, finds
    // the cancelled waiter, unlinks it (state_ -> not_locked; no residual
    // is ever pushed on the cancelled-drop path) -- but the abort-delivery
    // runner scheduled above is still queued, never executed.
    mtx->unlock();

    // state_ == not_locked, next_drain_head_ == nullptr,
    // in_flight_resumers_ == 1. Destroying here MUST terminate.
    delete mtx;

    // Pre-fix: unreachable only if the guard fires. Post-fix this line is
    // dead code. Pre-fix (today) the guard does NOT fire and execution
    // reaches here -- the child exits cleanly (RED for EXPECT_DEATH).
    std::exit(0);
}

// T014: grant-shaped sibling (Gate-A: Fable). Same in_flight_resumers_ != 0
// violation reached via the GRANT path, with NO cancellation involved.
// A queued waiter W is granted (schedule_record_resume: in_flight_
// resumers_ == 1, its guard-delivery runner posted but never run). We then
// simulate W's OWN immediate release -- exactly what W's guard destructor
// would do once its posted runner actually delivered the guard and ran to
// completion -- via a second direct unlock() call, WITHOUT ever running
// that posted runner. This reaches state_ == not_locked / next_drain_head_
// == nullptr while the FIRST grant's delivery runner is still queued,
// deterministically and without touching freed memory in-process (a
// literal nested `delete` inside the real runner would leave that same
// runner's tail dereferencing freed `this` afterward -- nondeterministic
// on this non-ASan debug lane; this construction avoids that entirely by
// never letting the posted runner run at all).
static void grant_delivered_then_destroy() {
    install_guard_terminate_marker();
    auto* mtx = new async_mutex{};
    asio::io_context ioc;

    auto holder_coro = [mtx]() -> asio::awaitable<void> {
        auto g = co_await mtx->async_lock();
        static_cast<void>(g->release());
        co_return;
    };

    auto waiter_coro = [mtx]() -> asio::awaitable<void> {
        co_await asio::post(co_await asio::this_coro::executor, asio::use_awaitable);
        auto r = co_await mtx->async_lock();
        static_cast<void>(r.has_value());
    };

    asio::co_spawn(ioc, holder_coro(), asio::detached);
    asio::co_spawn(ioc, waiter_coro(), asio::detached);

    // Holder acquires (fast path); waiter posts once then queues on the
    // LIFO behind the (leaked) holder.
    for (int i = 0; i < 16; ++i) ioc.poll_one();

    // Holder's unlock: grants the queued waiter W (schedule_record_resume
    // -> in_flight_resumers_ == 1, W's delivery runner posted, NOT
    // executed -- we never poll again). state_ stays locked_no_waiters
    // (W nominally holds now; no other waiters queued, so no residual).
    mtx->unlock();

    // Simulate W's own immediate release WITHOUT ever running its posted
    // delivery runner: state_ -> not_locked, next_drain_head_ stays empty
    // -- but the FIRST grant's delivery runner (posted above) is still
    // queued; its tail (release_ref + in_flight_resumers_ decrement) never
    // ran.
    mtx->unlock();

    // state_ == not_locked, next_drain_head_ == nullptr,
    // in_flight_resumers_ == 1. Destroying here MUST terminate.
    delete mtx;

    std::exit(0);
}

// T041: residual-chain coverage witness (NOT a RED-first defect test --
// the CURRENT guard already checks next_drain_head_, so this stays GREEN
// today and post-T017). Grant-with-tail: two waiters W1/W2 queue behind
// the holder; unlock() grants W1 and splices the tail W2 onto
// next_drain_head_. Destroying without draining trips BOTH OR-terms
// simultaneously (proved by the mutation-pair below): state_ ==
// locked_no_waiters != not_locked (W1 nominally holds, never having run)
// AND next_drain_head_ == ptr(W2) != nullptr. This is a STRUCTURAL
// invariant of the current unlock() control flow -- every code path that
// sets state_ == not_locked runs strictly after
// next_drain_head_.exchange(nullptr) and only on the no-grant fall-through
// (the grant-with-tail path returns immediately after push_residual,
// before the state_ CAS) -- so `next_drain_head_ != nullptr` can never be
// observed at destroy time together with `state_ == not_locked`: the
// residual term is always masked by the held/granted state_ term in this
// shape.
static void grant_with_tail_then_destroy() {
    install_guard_terminate_marker();
    auto* mtx = new async_mutex{};
    asio::io_context ioc;

    auto holder_coro = [mtx]() -> asio::awaitable<void> {
        auto g = co_await mtx->async_lock();
        static_cast<void>(g->release());
        co_return;
    };

    auto make_waiter = [mtx]() -> asio::awaitable<void> {
        co_await asio::post(co_await asio::this_coro::executor, asio::use_awaitable);
        auto r = co_await mtx->async_lock();
        static_cast<void>(r.has_value());
    };

    asio::co_spawn(ioc, holder_coro(), asio::detached);
    asio::co_spawn(ioc, make_waiter(), asio::detached);
    asio::co_spawn(ioc, make_waiter(), asio::detached);

    // Holder acquires (fast path); both waiters post once then queue,
    // LIFO order, behind the (leaked) holder.
    for (int i = 0; i < 32; ++i) ioc.poll_one();

    // Holder's unlock: reverses the 2-entry LIFO to FIFO order, grants the
    // first, and splices the second (the tail) onto next_drain_head_
    // (schedule_record_resume for the granted waiter -> in_flight_
    // resumers_ == 1, posted, NOT executed). state_ stays
    // locked_no_waiters (the granted waiter nominally holds now).
    mtx->unlock();

    // next_drain_head_ != nullptr (the spliced tail); state_ != not_locked
    // (the granted head). Destroying here trips the CURRENT guard already
    // (both OR-terms true) -- this is a coverage witness, expected GREEN
    // both pre- and post-T017.
    delete mtx;

    std::exit(0);
}

// ─────────────────────────────────────────────────────────────────────────────
// T013: cancel-delivered-then-destroy -> terminate (RED pre-fix).
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamDestructorReleaseDeath, CancelDeliveredThenDestroyTerminates) {
    EXPECT_DEATH(cancel_delivered_then_destroy(), kGuardTerminateMarker);
}

// ─────────────────────────────────────────────────────────────────────────────
// T014: grant-shaped sibling -> terminate (RED pre-fix).
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamDestructorReleaseDeath, GrantDeliveredThenDestroyTerminates) {
    EXPECT_DEATH(grant_delivered_then_destroy(), kGuardTerminateMarker);
}

// ─────────────────────────────────────────────────────────────────────────────
// T041: residual-chain coverage witness -> terminate (GREEN today).
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamDestructorReleaseDeath, GrantWithTailThenDestroyTerminates) {
    EXPECT_DEATH(grant_with_tail_then_destroy(), kGuardTerminateMarker);
}

// ─────────────────────────────────────────────────────────────────────────────
// Positive control: a properly drained-then-destroyed mutex does NOT terminate.
//
// cancel_and_drain() is currently unimplemented (TODO(T049)); this test
// documents the EXPECTED behaviour once it is implemented. With the stub it
// will fail — that is the correct TDD-red signal.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamDestructorReleaseDeath, ProperlyDrainedMutexDoesNotTerminate) {
    constexpr int N = 4;
    asio::io_context ioc;

    auto mtx = std::make_unique<async_mutex>();

    std::atomic<int> aborted{0};
    bool drain_ok = false;

    // Canonical §4.7.4 sequencing: drain runs concurrently while the holder
    // still holds; the holder's later unlock() short-circuits on draining_.
    auto holder_coro = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx->async_lock();
        EXPECT_TRUE(g.has_value());
        co_await yield_n(N * 4 + 8);
        // Guard dtor → unlock() (draining_ == true → short-circuit).
    };

    auto make_waiter = [&]() -> asio::awaitable<void> {
        co_await yield_n(1);
        auto r = co_await mtx->async_lock();
        if (!r.has_value() && r.error() == error::sync_lock_aborted)
            aborted.fetch_add(1, std::memory_order_acq_rel);
    };

    auto drainer_coro = [&]() -> asio::awaitable<void> {
        co_await yield_n(N * 2);
        auto d = co_await mtx->cancel_and_drain();
        drain_ok = d.has_value();
    };

    auto fh = asio::co_spawn(ioc, holder_coro(), asio::use_future);
    auto fd = asio::co_spawn(ioc, drainer_coro(), asio::use_future);
    std::vector<std::future<void>> futs;
    futs.reserve(N);
    for (int i = 0; i < N; ++i)
        futs.push_back(asio::co_spawn(ioc, make_waiter(), asio::use_future));
    if (!fixpp::test_support::run_to_exhaustion_or_report(
            ioc, fh, "SeamDestructorReleaseDeath::ProperlyDrainedMutexDoesNotTerminate/fh")) {
        return;
    }
    fh.get();
    if (!fixpp::test_support::run_to_exhaustion_or_report(
            ioc, fd, "SeamDestructorReleaseDeath::ProperlyDrainedMutexDoesNotTerminate/fd")) {
        return;
    }
    fd.get();
    for (auto& f : futs) f.get();

    // After drain, destroy the mutex — must NOT terminate.
    mtx.reset();

    EXPECT_TRUE(drain_ok) << "cancel_and_drain() must return success after drain";
    EXPECT_EQ(aborted.load(), N) << "All N waiters must be aborted";
}

}  // namespace
