// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/replay_gate_test.cpp — test-only.
//
// Feature 103 (specs/103-resend-logon-reset-fidelity/), R-588-g (research D5′; FR-015; tasks
// T054): detail::replay_gate alone, on a session strand (make_session_strand over an
// io_context the test thread runs), with no Session. The cells drive the gate the way the
// Session does: a shared acquirer tests `exclusive`, and while it is set applies the
// cancellation pre-check in its own frame and awaits wait_for_wake(); the walk takes an
// exclusive_guard and waits for `shared == 0`.
//
// The allocation cell counts global operator new in this binary (it replaces it), except in a
// sanitizer build, whose runtime owns the allocator; its count is evidence, printed, not
// asserted (research D5′, "Allocation").
#include <gtest/gtest.h>

#include <asio/as_tuple.hpp>
#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/cancellation_type.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <fixpp/session/detail/replay_gate.hpp>
#include <new>
#include <optional>
#include <system_error>

#include "session/session_strand.hpp"
#include "support/pump_until_ready.hpp"

// ── Sanitizer-detection guard (tests/alloc_guard/test_544_run_thread_windows.cpp) ──
#ifdef __has_feature
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) || \
    __has_feature(memory_sanitizer)
#define FIXPP_SANITIZER_REPLACES_NEW 1
#endif
#endif
#if !defined(FIXPP_SANITIZER_REPLACES_NEW) && \
    (defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__))
#define FIXPP_SANITIZER_REPLACES_NEW 1
#endif
#ifndef FIXPP_SANITIZER_REPLACES_NEW
#define FIXPP_SANITIZER_REPLACES_NEW 0
#endif

#if !FIXPP_SANITIZER_REPLACES_NEW
static std::atomic<std::size_t> g_new_count{0};
static std::atomic<bool> g_counting{false};

void* operator new(std::size_t n) {
    if (g_counting.load(std::memory_order_relaxed)) g_new_count.fetch_add(1);
    void* p = std::malloc(n == 0 ? 1 : n);
    if (!p) throw std::bad_alloc{};
    return p;
}
void* operator new[](std::size_t n) {
    if (g_counting.load(std::memory_order_relaxed)) g_new_count.fetch_add(1);
    void* p = std::malloc(n == 0 ? 1 : n);
    if (!p) throw std::bad_alloc{};
    return p;
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
#endif

namespace {

using fixpp::session::detail::replay_gate;

constexpr std::chrono::seconds kBudget{10};

// A gate bound to a session strand over `ioc`.
struct Rig {
    asio::io_context ioc;  // first, so destroyed last: the gate's timer is on it
    asio::any_io_executor strand = fixpp::session::detail::make_session_strand(ioc.get_executor());
    replay_gate gate;

    Rig() { gate.bind(strand, &fixpp::session::detail::session_strand_check); }

    template <class Pred>
    [[nodiscard]] bool run_until(Pred pred) {
        return fixpp::test_support::pump_until(ioc, std::move(pred), kBudget,
                                               fixpp::test_support::kPumpSlice, nullptr);
    }
    // Runs every ready handler.
    void settle() {
        ioc.restart();
        while (ioc.poll() > 0) ioc.restart();
        ioc.restart();
    }
};

// A test-controlled hold: waits until the cell cancels `release`.
asio::awaitable<void> wait_released(asio::steady_timer& release) {
    (void)co_await release.async_wait(asio::as_tuple(asio::use_awaitable));
}

// The shared side as the Session runs it: while exclusive, the pre-check in this frame, then
// one wait; then the hold until `release` is cancelled. `aborted` is set when the caller's
// cancellation ended a wait; `pending_after` reads the caller's cancellation state then.
struct SharedReadings {
    bool held = false;
    bool aborted = false;
    bool pending_after = false;
    bool done = false;
};
asio::awaitable<void> shared_acquirer(replay_gate& g, asio::steady_timer& release,
                                      SharedReadings& out) {
    while (g.exclusive) {
        if ((co_await asio::this_coro::throw_if_cancelled()) &&
            (co_await asio::this_coro::cancellation_state).cancelled() !=
                asio::cancellation_type::none) {
            out.aborted = true;
            break;
        }
        co_await g.wait_for_wake();
        if ((co_await asio::this_coro::cancellation_state).cancelled() !=
            asio::cancellation_type::none) {
            out.aborted = true;
            break;
        }
    }
    if (out.aborted) {
        out.pending_after = (co_await asio::this_coro::cancellation_state).cancelled() !=
                            asio::cancellation_type::none;
        out.done = true;
        co_return;
    }
    {
        replay_gate::shared_hold const h{g};
        out.held = true;
        co_await wait_released(release);
    }
    out.done = true;
}

// The walk's side: the exclusive guard, the wait for open holds, then its own hold until
// `release` is cancelled.
struct WalkReadings {
    bool granted = false;
    bool done = false;
};
asio::awaitable<void> walk(replay_gate& g, asio::steady_timer& release, WalkReadings& out) {
    replay_gate::exclusive_guard const guard{g};
    while (g.shared != 0) co_await g.wait_for_wake();
    out.granted = true;
    co_await wait_released(release);
    out.done = true;
}

asio::steady_timer held_timer(Rig& r) {
    return asio::steady_timer{r.strand, asio::steady_timer::time_point::max()};
}

}  // namespace

// Control: the uncontended shared path takes and releases a hold with no wait: no timer is
// built and no async_wait is made.
TEST(ReplayGate, Control_UncontendedSharedHold_NoTimerAndNoWait) {
    Rig r;
    auto release = held_timer(r);
    SharedReadings s;
    asio::co_spawn(r.strand, shared_acquirer(r.gate, release, s), asio::detached);
    ASSERT_TRUE(r.run_until([&] { return s.held; }));
    EXPECT_EQ(r.gate.shared, 1U);
    release.cancel();
    ASSERT_TRUE(r.run_until([&] { return s.done; }));
    EXPECT_EQ(r.gate.shared, 0U);
    EXPECT_EQ(r.gate.timer_waits, 0U);
    EXPECT_FALSE(r.gate.wake.has_value());
}

// Shared and exclusive, with writer preference: a walk waits for the open shared hold; a
// shared acquirer that arrives while the walk waits waits behind it, not beside the open hold;
// the last shared release wakes the walk; the walk's end wakes the shared acquirer.
TEST(ReplayGate, WalkWaitsForOpenHolds_NewSharedWaitsBehindIt_ReleasesWakeEachInTurn) {
    Rig r;
    auto release_a = held_timer(r);
    auto release_w = held_timer(r);
    auto release_b = held_timer(r);
    SharedReadings a;
    SharedReadings b;
    WalkReadings w;
    asio::co_spawn(r.strand, shared_acquirer(r.gate, release_a, a), asio::detached);
    ASSERT_TRUE(r.run_until([&] { return a.held; }));
    asio::co_spawn(r.strand, walk(r.gate, release_w, w), asio::detached);
    ASSERT_TRUE(r.run_until([&] { return r.gate.waiters == 1U; }));
    EXPECT_TRUE(r.gate.exclusive);
    EXPECT_FALSE(w.granted) << "the walk ran beside an open shared hold";
    asio::co_spawn(r.strand, shared_acquirer(r.gate, release_b, b), asio::detached);
    ASSERT_TRUE(r.run_until([&] { return r.gate.waiters == 2U; }));
    EXPECT_FALSE(b.held) << "a shared hold was taken while the walk waits (no writer preference)";

    release_a.cancel();  // the last shared release
    ASSERT_TRUE(r.run_until([&] { return w.granted; }))
        << "the last shared release did not wake the walk";
    r.settle();
    EXPECT_FALSE(b.held) << "a shared hold was taken during the walk";
    EXPECT_EQ(r.gate.waiters, 1U);

    release_w.cancel();  // the walk's end
    ASSERT_TRUE(r.run_until([&] { return b.held; }))
        << "the walk's end did not wake the shared acquirer";
    EXPECT_FALSE(r.gate.exclusive);
    EXPECT_EQ(r.gate.waiters, 0U);
    release_b.cancel();
    ASSERT_TRUE(r.run_until([&] { return b.done; }));
    EXPECT_EQ(r.gate.shared, 0U);
}

// The last shared release wakes any waiter, not only a walk (the reconnect drain's use: it
// waits for `shared == 0` without taking the gate exclusive).
TEST(ReplayGate, LastSharedRelease_WakesANonExclusiveWaiter) {
    Rig r;
    auto release = held_timer(r);
    SharedReadings s;
    bool drained = false;
    asio::co_spawn(r.strand, shared_acquirer(r.gate, release, s), asio::detached);
    ASSERT_TRUE(r.run_until([&] { return s.held; }));
    asio::co_spawn(
        r.strand,
        [&]() -> asio::awaitable<void> {
            while (r.gate.shared != 0) co_await r.gate.wait_for_wake();
            drained = true;
        },
        asio::detached);
    ASSERT_TRUE(r.run_until([&] { return r.gate.waiters == 1U; }));
    EXPECT_FALSE(r.gate.exclusive);
    release.cancel();
    EXPECT_TRUE(r.run_until([&] { return drained; })) << "the last shared release did not wake it";
    EXPECT_EQ(r.gate.waiters, 0U);
}

// A contended wait its caller's own cancellation ends: the waiter count is back to 0, the
// caller does not take the hold, and the caller's cancellation is still pending (no
// reset_cancellation_state; FR-015, N5).
TEST(ReplayGate, CancelledContendedWait_WaitersBackToZero_CancellationStillPending) {
    Rig r;
    auto release_w = held_timer(r);
    auto release_c = held_timer(r);
    WalkReadings w;
    SharedReadings c;
    asio::cancellation_signal sig;
    asio::co_spawn(r.strand, walk(r.gate, release_w, w), asio::detached);
    ASSERT_TRUE(r.run_until([&] { return w.granted; }));
    asio::co_spawn(r.strand, shared_acquirer(r.gate, release_c, c),
                   asio::bind_cancellation_slot(sig.slot(), asio::detached));
    ASSERT_TRUE(r.run_until([&] { return r.gate.waiters == 1U; }));
    sig.emit(asio::cancellation_type::terminal);
    ASSERT_TRUE(r.run_until([&] { return c.done; })) << "the cancelled wait did not end";
    EXPECT_TRUE(c.aborted);
    EXPECT_FALSE(c.held);
    EXPECT_TRUE(c.pending_after) << "the caller's cancellation was cleared by the wait";
    EXPECT_EQ(r.gate.waiters, 0U);
    release_w.cancel();
    ASSERT_TRUE(r.run_until([&] { return w.done; }));
}

// The filter witness (#583): after a contended wait that ends normally, the caller's
// cancellation filter is the one it set. A total emission on its slot then aborts its next
// timer wait.
TEST(ReplayGate, AfterAContendedWait_ATotalEmitStillAbortsTheNextWait) {
    Rig r;
    auto release_w = held_timer(r);
    WalkReadings w;
    asio::cancellation_signal sig;
    bool waited = false;
    bool next_aborted = false;
    bool done = false;
    asio::co_spawn(r.strand, walk(r.gate, release_w, w), asio::detached);
    ASSERT_TRUE(r.run_until([&] { return w.granted; }));
    asio::co_spawn(
        r.strand,
        [&]() -> asio::awaitable<void> {
            co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
            while (r.gate.exclusive) co_await r.gate.wait_for_wake();
            waited = true;
            asio::steady_timer t{r.strand, asio::steady_timer::time_point::max()};
            auto [ec] = co_await t.async_wait(asio::as_tuple(asio::use_awaitable));
            next_aborted = ec == asio::error::operation_aborted;
            done = true;
        },
        asio::bind_cancellation_slot(sig.slot(), asio::detached));
    ASSERT_TRUE(r.run_until([&] { return r.gate.waiters == 1U; }));
    release_w.cancel();
    ASSERT_TRUE(r.run_until([&] { return waited; })) << "the contended wait did not end";
    r.settle();
    sig.emit(asio::cancellation_type::total);
    EXPECT_TRUE(r.run_until([&] { return done; }))
        << "a total emission did not reach the wait after the contended wait";
    EXPECT_TRUE(next_aborted);
}

// A gate that is built, held shared, released and destroyed, with no wait ever made: no
// timer, nothing to unlock before destruction, no drain.
TEST(ReplayGate, NoWaiter_BuildHoldReleaseDestroy) {
    asio::io_context ioc;
    auto const strand = fixpp::session::detail::make_session_strand(ioc.get_executor());
    bool done = false;
    {
        replay_gate g;
        g.bind(strand, &fixpp::session::detail::session_strand_check);
        asio::co_spawn(
            strand,
            [&]() -> asio::awaitable<void> {
                {
                    replay_gate::shared_hold const h{g};
                }
                done = true;
                co_return;
            },
            asio::detached);
        ASSERT_TRUE(fixpp::test_support::pump_until(
            ioc, [&] { return done; }, kBudget, fixpp::test_support::kPumpSlice, nullptr));
        EXPECT_FALSE(g.wake.has_value());
        EXPECT_EQ(g.shared, 0U);
    }
}

// The strand check: an acquire on the strand is checked and sees the strand (positive
// control); an acquire from a thread that is not running the strand aborts with the check's
// message.
TEST(ReplayGate, StrandCheck_OnTheStrandCounted_OffTheStrandAborts) {
#ifdef NDEBUG
    GTEST_SKIP() << "the strand check is compiled only in a build with assertions";
#else
    Rig r;
    auto release = held_timer(r);
    SharedReadings s;
    asio::co_spawn(r.strand, shared_acquirer(r.gate, release, s), asio::detached);
    ASSERT_TRUE(r.run_until([&] { return s.held; }));
    EXPECT_GT(r.gate.strand_checks_on_a_strand, 0U) << "an acquire on the strand saw no strand";
    release.cancel();
    ASSERT_TRUE(r.run_until([&] { return s.done; }));
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    EXPECT_DEATH(
        { replay_gate::shared_hold const h{r.gate}; }, "replay_gate used off the session strand");
#endif
}

// Allocation evidence for the contended path (research D5′, "Allocation"; evidence only):
// one warm-up contended exclusive wait and one contended shared wait, then kRounds more of
// each, counted. The count is printed and recorded as a property.
TEST(ReplayGate, ContendedWaits_AllocationCountIsRecorded) {
#if FIXPP_SANITIZER_REPLACES_NEW
    GTEST_SKIP() << "a sanitizer runtime owns operator new here";
#else
    constexpr int kRounds = 32;
    Rig r;
    // One round: a walk waits behind an open shared hold, and a shared acquirer waits behind
    // the walk; each wait is one contended wait.
    auto const round = [&]() -> bool {
        auto release_a = held_timer(r);
        auto release_w = held_timer(r);
        auto release_b = held_timer(r);
        SharedReadings a;
        SharedReadings b;
        WalkReadings w;
        asio::co_spawn(r.strand, shared_acquirer(r.gate, release_a, a), asio::detached);
        bool ok = r.run_until([&] { return a.held; });
        asio::co_spawn(r.strand, walk(r.gate, release_w, w), asio::detached);
        ok = ok && r.run_until([&] { return r.gate.waiters == 1U; });
        asio::co_spawn(r.strand, shared_acquirer(r.gate, release_b, b), asio::detached);
        ok = ok && r.run_until([&] { return r.gate.waiters == 2U; });
        // Counted: from the last shared release to the shared acquirer's grant, the two
        // waits' resumptions and everything the wakes run.
        g_counting.store(true);
        release_a.cancel();
        ok = ok && r.run_until([&] { return w.granted; });
        release_w.cancel();
        ok = ok && r.run_until([&] { return b.held; });
        g_counting.store(false);
        release_b.cancel();
        ok = ok && r.run_until([&] { return b.done; });
        return ok;
    };
    ASSERT_TRUE(round()) << "the warm-up round did not complete";
    g_new_count.store(0);
    int completed = 0;
    for (int i = 0; i < kRounds; ++i) completed += round() ? 1 : 0;
    EXPECT_EQ(completed, kRounds) << "a counted round did not complete";
    auto const n = g_new_count.load();
    std::printf("replay_gate contended waits: %d rounds (2 waits each), operator new count %zu\n",
                kRounds, n);
    ::testing::Test::RecordProperty("contended_wait_new_count", static_cast<int>(n));
#endif
}
