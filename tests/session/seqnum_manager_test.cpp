// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/seqnum_manager_test.cpp
//
// Seam #3 — SeqnumManager unit tests (005-session-establishment-fsm T027 / US2 Phase 4).
//
// TDD red-first: authored BEFORE T031 (seqnum_manager.cpp body).
// Must FAIL until T031 wires the implementation. After T031/T036 must be GREEN.
//
// Scenarios:
//   1. Increment-by-one inbound: many accepted messages advance counter exactly
//      (I-2 zero-drift over a long run).
//   2. Increment-by-one outbound: assign_outbound() returns sequentially 1,2,...
//      and the counter advances by exactly 1 per call (I-2).
//   3. Too-low inbound → session_seqnum_too_low (slot 69, session-fatal).
//      No PossDup path (S-010 out of scope).
//   4. Too-high inbound → session_seqnum_gap_unrecoverable (slot 70, session-fatal).
//      I-4: no ResendRequest.
//   5. seqnum_max overflow: next_outbound_ == seqnum_max → assign_outbound()
//      returns store_seqnum_overflow (slot 60, I-8). No wrap.
//   6. In-seq after too-low returns error (counter not advanced on error).
//   7. Long-run drift check: 100 sequential in/out pairs, counters match expected.
//
// Anchors: data-model.md E3/E4; invariants I-2/I-4/I-8; spec FR-008; error slots 60/69/70.
// [FIX-SL §4.1] ordered-sequence integrity.
// Seam #3: tests/session/seqnum_manager_test.cpp.
//
// Infrastructure: asio::io_context for awaitable execution.
#include <cstdint>
#include <future>
#include <memory>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <fixpp/core/error.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/seqnum_manager.hpp>

#include <gtest/gtest.h>

namespace fixpp::session::test {

namespace {

// Run a single awaitable synchronously on an io_context.
template <class Awaitable>
auto run_sync(asio::io_context& ioc, Awaitable&& aw) {
    auto fut = asio::co_spawn(ioc, std::forward<Awaitable>(aw), asio::use_future);
    ioc.run();
    ioc.restart();
    return fut.get();
}

}  // namespace

// ── Fixture ───────────────────────────────────────────────────────────────────

class SeqnumManagerTest : public ::testing::Test {
protected:
    asio::io_context ioc;
};

// ── T1: Inbound increment-by-one ────────────────────────────────────────────

TEST_F(SeqnumManagerTest, InboundIncrementByOne) {
    SeqnumManager mgr;

    // Initial state: next_inbound_ == 1.
    EXPECT_EQ(mgr.next_inbound_unsafe(), seqnum_min);

    // check_inbound(1) → ok, counter advances to 2.
    auto r1 = run_sync(ioc, mgr.check_inbound(1));
    EXPECT_TRUE(r1.has_value()) << "check_inbound(1) should succeed";
    EXPECT_EQ(mgr.next_inbound_unsafe(), 2u);

    // check_inbound(2) → ok, counter advances to 3.
    auto r2 = run_sync(ioc, mgr.check_inbound(2));
    EXPECT_TRUE(r2.has_value()) << "check_inbound(2) should succeed";
    EXPECT_EQ(mgr.next_inbound_unsafe(), 3u);

    // Drain before destruction.
    run_sync(ioc, mgr.drain());
}

// ── T2: Outbound increment-by-one ────────────────────────────────────────────

TEST_F(SeqnumManagerTest, OutboundAssignSequential) {
    SeqnumManager mgr;
    EXPECT_EQ(mgr.next_outbound_unsafe(), seqnum_min);

    // First assign returns 1.
    auto r1 = run_sync(ioc, mgr.assign_outbound());
    ASSERT_TRUE(r1.has_value()) << "assign_outbound() #1 should succeed";
    EXPECT_EQ(*r1, 1u);
    EXPECT_EQ(mgr.next_outbound_unsafe(), 2u);

    // Second assign returns 2.
    auto r2 = run_sync(ioc, mgr.assign_outbound());
    ASSERT_TRUE(r2.has_value()) << "assign_outbound() #2 should succeed";
    EXPECT_EQ(*r2, 2u);
    EXPECT_EQ(mgr.next_outbound_unsafe(), 3u);

    run_sync(ioc, mgr.drain());
}

// ── T3: Too-low inbound (no PossDup) → session_seqnum_too_low ───────────────

TEST_F(SeqnumManagerTest, TooLowInboundIsSessionFatal) {
    SeqnumManager mgr;

    // Advance inbound counter to 5.
    for (seqnum_t s = 1; s <= 4; ++s) {
        auto r = run_sync(ioc, mgr.check_inbound(s));
        ASSERT_TRUE(r.has_value()) << "setup: check_inbound(" << s << ") should succeed";
    }
    ASSERT_EQ(mgr.next_inbound_unsafe(), 5u);

    // seq=3 is too low (expected 5).
    auto r = run_sync(ioc, mgr.check_inbound(3));
    ASSERT_FALSE(r.has_value()) << "Too-low must return error";
    EXPECT_EQ(r.error(), fixpp::core::error::session_seqnum_too_low)
        << "Expected session_seqnum_too_low (slot 69)";

    // Counter must NOT advance on error (I-2).
    EXPECT_EQ(mgr.next_inbound_unsafe(), 5u)
        << "Counter must not advance on too-low error";

    run_sync(ioc, mgr.drain());
}

// ── T4: Too-high inbound → session_seqnum_gap_unrecoverable, no ResendRequest ─

TEST_F(SeqnumManagerTest, TooHighInboundIsSessionFatal) {
    SeqnumManager mgr;

    // Initial next_inbound_ = 1; seq=5 is too high (gap 1→5).
    auto r = run_sync(ioc, mgr.check_inbound(5));
    ASSERT_FALSE(r.has_value()) << "Too-high must return error";
    EXPECT_EQ(r.error(), fixpp::core::error::session_seqnum_gap_unrecoverable)
        << "Expected session_seqnum_gap_unrecoverable (slot 70); recovery deferred I-4";

    // Counter must NOT advance on error.
    EXPECT_EQ(mgr.next_inbound_unsafe(), 1u)
        << "Counter must not advance on too-high error";

    run_sync(ioc, mgr.drain());
}

// ── T5: seqnum_max outbound overflow → store_seqnum_overflow, NO wrap ────────

TEST_F(SeqnumManagerTest, OutboundOverflowAtSeqnumMax) {
    // To avoid running seqnum_max assign calls we directly hack the test:
    // We need a SeqnumManager with next_outbound_ == seqnum_max.
    // The test double approach: assign enough to reach seqnum_max-1, then
    // the next assign returns seqnum_max (ok), then overflow on the next one.
    //
    // BUT seqnum_max == UINT32_MAX which is ~4 billion calls — too slow.
    // The SeqnumManager exposes next_outbound_unsafe() for inspection; we
    // need a way to set the counter to seqnum_max for testing.
    //
    // Design decision: SeqnumManager provides a test-only constructor that
    // accepts initial counter values. This is gated by FIXPP_TEST_HOOKS
    // (same pattern as store_seqnum_out_of_order uses FIXPP_TEST_HOOKS).
    // If FIXPP_TEST_HOOKS is not defined this test validates what is possible.
    //
    // For T027/T036: SeqnumManager provides set_counters_for_test() behind
    // FIXPP_TEST_HOOKS. This test uses it.

#ifdef FIXPP_TEST_HOOKS
    SeqnumManager mgr;
    // Set next_outbound_ to seqnum_max (the last valid value to assign).
    mgr.set_counters_for_test(seqnum_min, seqnum_max);

    // assign_outbound() with next_outbound_==seqnum_max: this WOULD return
    // seqnum_max. But then incrementing would overflow. Per I-8: at seqnum_max,
    // the assign itself is the overflow-detection point — we refuse.
    auto r = run_sync(ioc, mgr.assign_outbound());
    ASSERT_FALSE(r.has_value()) << "Overflow at seqnum_max must return error";
    EXPECT_EQ(r.error(), fixpp::core::error::store_seqnum_overflow)
        << "Expected store_seqnum_overflow (slot 60, reuse [2e §6.7]). I-8 no wrap.";

    // Counter must NOT wrap (stays at seqnum_max).
    EXPECT_EQ(mgr.next_outbound_unsafe(), seqnum_max)
        << "Counter must not wrap past seqnum_max";

    run_sync(ioc, mgr.drain());
#else
    GTEST_SKIP() << "FIXPP_TEST_HOOKS not defined; overflow test requires test counter seeding";
#endif
}

// ── T6: Too-low error does not advance counter; next in-seq succeeds ─────────

TEST_F(SeqnumManagerTest, TooLowDoesNotCorruptCounterSubsequentInSeqOk) {
    SeqnumManager mgr;

    // Advance to 3.
    run_sync(ioc, mgr.check_inbound(1));
    run_sync(ioc, mgr.check_inbound(2));
    ASSERT_EQ(mgr.next_inbound_unsafe(), 3u);

    // Too-low attempt: returns error, counter stays at 3.
    auto err = run_sync(ioc, mgr.check_inbound(1));
    EXPECT_FALSE(err.has_value());
    EXPECT_EQ(mgr.next_inbound_unsafe(), 3u);

    // In-seq attempt (3) still succeeds.
    auto ok = run_sync(ioc, mgr.check_inbound(3));
    EXPECT_TRUE(ok.has_value()) << "In-seq after error must still succeed";
    EXPECT_EQ(mgr.next_inbound_unsafe(), 4u);

    run_sync(ioc, mgr.drain());
}

// ── T7: Long-run drift check — 100 in/out pairs ──────────────────────────────

TEST_F(SeqnumManagerTest, LongRunZeroDrift) {
    SeqnumManager mgr;
    constexpr int N = 100;

    for (int i = 0; i < N; ++i) {
        // Outbound: assign returns i+1.
        auto out_r = run_sync(ioc, mgr.assign_outbound());
        ASSERT_TRUE(out_r.has_value()) << "assign_outbound at i=" << i;
        EXPECT_EQ(*out_r, static_cast<seqnum_t>(i + 1))
            << "outbound seqnum mismatch at i=" << i;

        // Inbound: check in-seq.
        auto in_r = run_sync(ioc, mgr.check_inbound(static_cast<seqnum_t>(i + 1)));
        ASSERT_TRUE(in_r.has_value()) << "check_inbound at i=" << i;
    }

    // Final state: both counters at N+1.
    EXPECT_EQ(mgr.next_inbound_unsafe(),  static_cast<seqnum_t>(N + 1));
    EXPECT_EQ(mgr.next_outbound_unsafe(), static_cast<seqnum_t>(N + 1));

    run_sync(ioc, mgr.drain());
}

// ── Drained-mutex paths (Phase 8 /simplify finding 3) ────────────────────────
//
// After drain() the async_mutex's next async_lock() resolves to the unexpected
// branch — the seqnum manager surfaces session_already_closed on both
// check_inbound and assign_outbound. These two arms (seqnum_manager.cpp:51
// and :91) are not exercised by the in-seq tests above.

TEST_F(SeqnumManagerTest, CheckInboundAfterDrainReturnsSessionAlreadyClosed) {
    SeqnumManager mgr;
    run_sync(ioc, mgr.drain());

    auto r = run_sync(ioc, mgr.check_inbound(seqnum_min));
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), fixpp::core::error::session_already_closed);
}

TEST_F(SeqnumManagerTest, AssignOutboundAfterDrainReturnsSessionAlreadyClosed) {
    SeqnumManager mgr;
    run_sync(ioc, mgr.drain());

    auto r = run_sync(ioc, mgr.assign_outbound());
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), fixpp::core::error::session_already_closed);
}

}  // namespace fixpp::session::test
