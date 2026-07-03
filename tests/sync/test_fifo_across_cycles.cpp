// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/sync/test_fifo_across_cycles.cpp — 058-async-mutex-hardening T032
// (US5, T-7)
//
// FIFO fairness ACROSS drain cycles — the residual-vs-later-arrival straddle.
//
// test_fifo_fairness.cpp::DrainCycleReversesLIFO already proves FIFO WITHIN a
// single drain cycle (unlock()'s LIFO->FIFO reversal, async_mutex.hpp:1387-
// 1397). It does NOT prove the property T-7 names: that unlock()'s residual
// list (`next_drain_head_`, spliced from the tail of a granting walk,
// async_mutex.hpp:1339-1341/1409-1411) is drained to completion BEFORE any
// waiter that first parks on the FRESH `state_` LIFO after the residual was
// created — even though, wall-clock, the later arrival can park well before
// the residual is exhausted.
//
// async_mutex.hpp:1310 (unlock()): every call checks `next_drain_head_`
// FIRST (:1326) and returns immediately after granting exactly one waiter
// from it; only once the residual is fully empty does it fall through to
// drain the fresh `state_` LIFO (:1375-1436). A regression that checked
// `state_` before (or interleaved with) the residual would let a
// later-arriving waiter jump a still-pending residual — silently breaking
// the "requests are served in arrival order" fairness contract, with no
// existing test to catch it (T-7: "today FIFO order is asserted nowhere
// across cycles").
//
// Threading model (deliberate deviation from the T027-T030/T031 genuinely-MT
// idiom, per orchestrator brief + advisor review): this is a DETERMINISM
// contract, not a race — the whole point is to construct waiters that arrive
// in a KNOWN order and assert they are granted in that order. A genuinely
// concurrent, unsynchronized driver would make arrival order unobservable/
// uncontrollable, defeating the witness. So this test uses ONE io_context,
// driven ONLY by test-controlled `poll_one()` calls (never `ioc.run()`), with
// explicit boolean "release gates" the test flips between polls to pin the
// EXACT moment each acquire/release happens — no yield-count timing guesses,
// no std::mutex/condition_variable (FR-012), and no real concurrency (single
// OS thread throughout, so plain bool/vector state is safe without atomics).
//
// Scenario:
//   H holds. W1, W2, W3 park (in that arrival order) while H holds.
//   H releases (cycle 1) -> drain reverses LIFO to FIFO [W1,W2,W3], grants
//     W1 (new holder), splices the residual [W2,W3] onto next_drain_head_.
//   W4 arrives strictly AFTER W1 becomes holder (chronologically after the
//     [W2,W3] residual already exists) -> parks on the FRESH state_ LIFO.
//   W1 releases (cycle 2) -> residual non-empty -> MUST grant W2, not W4.
//   W2 releases (cycle 3) -> residual [W3] -> grants W3.
//   W3 releases (cycle 4) -> residual now empty -> falls through to the
//     fresh state_ LIFO -> finally grants W4.
//
// Discriminating assertion: the granted sequence is exactly [W1,W2,W3,W4].
// A FIFO-across-cycles violation (W4 jumping W2 or W3) reorders this
// sequence — the assertion is on the SEQUENCE, not a completion tally
// (feedback_witness_asserts_named_postcondition_not_proxy shape (b)/(d)).
//
// Mutation-tested (transient, hand-reverted): temporarily swapping unlock()'s
// residual-list check (:1326) to run AFTER the fresh state_ drain (:1375)
// turns this test's core assertion red (W4 gets granted in cycle 2, ahead of
// W2/W3) while leaving test_fifo_fairness.cpp's within-cycle tests green —
// confirming this witness is the one that actually covers the cross-cycle
// half of the FIFO contract. Reverted before landing (async_mutex.hpp is
// UNTOUCHED by this feature's test-hardening scope).
//
// Oracle: [2f §9 #3] "FIFO fairness across drain cycles". spec.md US5 /
// Acceptance Scenario 3, T-7.

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <fixpp/core/sync/async_mutex.hpp>
#include <future>
#include <vector>

namespace {

using fixpp::sync::async_mutex;

// Reposts on its own executor until `gate` becomes true. Combined with
// test-driven `poll_one()` pumping, this gives the test exact, deterministic
// control of when each coroutine is allowed to proceed past this point —
// the mechanism that makes "arrivals interleaved between unlocks" reproducible
// without yield-count guesswork.
asio::awaitable<void> wait_gate(const bool& gate) {
    auto ex = co_await asio::this_coro::executor;
    while (!gate) co_await asio::post(ex, asio::use_awaitable);
}

// Pumps `ioc` up to `max_iters` times or until `pred()` is true, whichever
// comes first. Returns pred()'s final value so callers can ASSERT on it.
template <typename Pred>
bool pump_until(asio::io_context& ioc, Pred&& pred, int max_iters) {
    for (int i = 0; i < max_iters && !pred(); ++i) ioc.poll_one();
    return pred();
}

TEST(SeamFifoAcrossCycles, ResidualExhaustedBeforeLaterArrival) {
    async_mutex mtx;
    asio::io_context ioc;

    std::vector<int> granted_order;

    bool holder_acquired = false;
    bool release_h = false;
    auto holder = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx.async_lock();
        holder_acquired = g.has_value();
        EXPECT_TRUE(holder_acquired);
        co_await wait_gate(release_h);
        // guard dtor -> unlock(): cycle 1 -> grants W1, residual = [W2, W3].
    };

    bool release_w[5] = {false, false, false, false, false};  // index 1..4
    auto make_waiter = [&](int idx) -> asio::awaitable<void> {
        auto ex = co_await asio::this_coro::executor;
        co_await asio::post(ex, asio::use_awaitable);
        auto r = co_await mtx.async_lock();
        EXPECT_TRUE(r.has_value()) << "waiter " << idx << " must eventually be granted";
        granted_order.push_back(idx);
        co_await wait_gate(release_w[idx]);
        // guard dtor -> unlock() once release_w[idx] flips.
    };

    auto fh = asio::co_spawn(ioc, holder(), asio::use_future);
    ASSERT_TRUE(pump_until(ioc, [&] { return holder_acquired; }, 16))
        << "setup: holder failed to acquire";

    // W1, W2, W3 park (in this arrival order) while H holds.
    auto f1 = asio::co_spawn(ioc, make_waiter(1), asio::use_future);
    auto f2 = asio::co_spawn(ioc, make_waiter(2), asio::use_future);
    auto f3 = asio::co_spawn(ioc, make_waiter(3), asio::use_future);
    for (int i = 0; i < 48 && granted_order.empty(); ++i) ioc.poll_one();
    ASSERT_TRUE(granted_order.empty())
        << "setup: a waiter was granted before the holder released — W1..W3 "
           "must all be parked on the fresh state_ LIFO first";

    // Cycle 1: release H -> grants W1; splices residual [W2, W3].
    release_h = true;
    ASSERT_TRUE(pump_until(ioc, [&] { return granted_order.size() >= 1; }, 48))
        << "cycle 1: W1 must be granted";
    ASSERT_EQ(granted_order.size(), 1u);
    EXPECT_EQ(granted_order[0], 1) << "cycle 1 must grant W1 (holder's uncontended-then-drained slot)";

    // W4 arrives strictly AFTER W1 became holder — parks on the FRESH
    // state_ LIFO, chronologically after the [W2, W3] residual already
    // exists in next_drain_head_.
    auto f4 = asio::co_spawn(ioc, make_waiter(4), asio::use_future);
    for (int i = 0; i < 48 && granted_order.size() < 2; ++i) ioc.poll_one();
    ASSERT_EQ(granted_order.size(), 1u)
        << "setup: W4 must park without being granted while W1 still holds";

    // Cycle 2: release W1 -> residual [W2, W3] is non-empty -> MUST grant
    // W2, NOT the later-arriving-but-already-parked W4. This is the core
    // discriminating step.
    release_w[1] = true;
    ASSERT_TRUE(pump_until(ioc, [&] { return granted_order.size() >= 2; }, 48))
        << "cycle 2: someone must be granted";
    ASSERT_EQ(granted_order.size(), 2u);
    EXPECT_EQ(granted_order[1], 2)
        << "FIFO-across-cycles violation: the cycle-1 residual (W2) must be "
           "granted before W4, even though W4 has already parked by now";

    // Cycle 3: release W2 -> residual [W3] -> grants W3.
    release_w[2] = true;
    ASSERT_TRUE(pump_until(ioc, [&] { return granted_order.size() >= 3; }, 48))
        << "cycle 3: someone must be granted";
    ASSERT_EQ(granted_order.size(), 3u);
    EXPECT_EQ(granted_order[2], 3) << "residual must exhaust W3 before the fresh-arrival W4";

    // Cycle 4: release W3 -> residual now empty -> falls through to the
    // fresh state_ LIFO -> finally grants W4.
    release_w[3] = true;
    ASSERT_TRUE(pump_until(ioc, [&] { return granted_order.size() >= 4; }, 48))
        << "cycle 4: W4 must finally be granted";
    ASSERT_EQ(granted_order.size(), 4u);
    EXPECT_EQ(granted_order[3], 4);

    // Drain everyone and confirm clean completion (no hang, no terminate).
    release_w[4] = true;
    for (int i = 0; i < 128 && ioc.poll_one(); ++i) {
    }

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    auto all_ready = [&](std::future<void>& f) {
        return f.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
    };
    while (std::chrono::steady_clock::now() < deadline &&
           !(all_ready(fh) && all_ready(f1) && all_ready(f2) && all_ready(f3) && all_ready(f4))) {
        ioc.poll_one();
    }
    ASSERT_TRUE(all_ready(fh) && all_ready(f1) && all_ready(f2) && all_ready(f3) && all_ready(f4))
        << "final drain did not complete — possible hang";

    fh.get();
    f1.get();
    f2.get();
    f3.get();
    f4.get();

    // The whole-sequence assertion — the property this test exists for.
    EXPECT_EQ(granted_order, (std::vector<int>{1, 2, 3, 4}))
        << "Full FIFO-across-cycles order must be [W1, W2, W3, W4]";
    // mtx destructs here — must not std::terminate.
}

// Repeats the deterministic scenario a handful of times as a cheap harness-
// robustness check. Not a stress/reps requirement (unlike the race-probing
// T027-T031 conversions): this scenario is deterministic by construction —
// there is no non-determinism to shake out — so a modest repeat count
// suffices to catch any accidental flakiness in the poll_one()/gate driver
// itself, not to probe a race window.
TEST(SeamFifoAcrossCycles, RepeatedScenarioIsDeterministic) {
    for (int rep = 0; rep < 8; ++rep) {
        SCOPED_TRACE(rep);

        async_mutex mtx;
        asio::io_context ioc;
        std::vector<int> granted_order;

        bool holder_acquired = false;
        bool release_h = false;
        auto holder = [&]() -> asio::awaitable<void> {
            auto g = co_await mtx.async_lock();
            holder_acquired = g.has_value();
            co_await wait_gate(release_h);
        };

        bool release_w[5] = {false, false, false, false, false};
        auto make_waiter = [&](int idx) -> asio::awaitable<void> {
            auto ex = co_await asio::this_coro::executor;
            co_await asio::post(ex, asio::use_awaitable);
            auto r = co_await mtx.async_lock();
            if (r.has_value()) granted_order.push_back(idx);
            co_await wait_gate(release_w[idx]);
        };

        auto fh = asio::co_spawn(ioc, holder(), asio::use_future);
        ASSERT_TRUE(pump_until(ioc, [&] { return holder_acquired; }, 16));

        auto f1 = asio::co_spawn(ioc, make_waiter(1), asio::use_future);
        auto f2 = asio::co_spawn(ioc, make_waiter(2), asio::use_future);
        auto f3 = asio::co_spawn(ioc, make_waiter(3), asio::use_future);
        for (int i = 0; i < 48 && granted_order.empty(); ++i) ioc.poll_one();
        ASSERT_TRUE(granted_order.empty());

        release_h = true;
        ASSERT_TRUE(pump_until(ioc, [&] { return granted_order.size() >= 1; }, 48));

        auto f4 = asio::co_spawn(ioc, make_waiter(4), asio::use_future);
        for (int i = 0; i < 48 && granted_order.size() < 2; ++i) ioc.poll_one();
        ASSERT_EQ(granted_order.size(), 1u);

        release_w[1] = true;
        ASSERT_TRUE(pump_until(ioc, [&] { return granted_order.size() >= 2; }, 48));
        release_w[2] = true;
        ASSERT_TRUE(pump_until(ioc, [&] { return granted_order.size() >= 3; }, 48));
        release_w[3] = true;
        ASSERT_TRUE(pump_until(ioc, [&] { return granted_order.size() >= 4; }, 48));
        release_w[4] = true;

        for (int i = 0; i < 128 && ioc.poll_one(); ++i) {
        }
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        auto all_ready = [&](std::future<void>& f) {
            return f.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
        };
        while (std::chrono::steady_clock::now() < deadline &&
               !(all_ready(fh) && all_ready(f1) && all_ready(f2) && all_ready(f3) && all_ready(f4))) {
            ioc.poll_one();
        }
        ASSERT_TRUE(all_ready(fh) && all_ready(f1) && all_ready(f2) && all_ready(f3) && all_ready(f4));
        fh.get();
        f1.get();
        f2.get();
        f3.get();
        f4.get();

        ASSERT_EQ(granted_order, (std::vector<int>{1, 2, 3, 4}));
        if (::testing::Test::HasFatalFailure()) break;
    }
}

}  // namespace
