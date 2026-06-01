// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// tests/transport/test_cancellation_propagation.cpp
// T013 — [2h §9 seam #5] — cancellation propagation contract.
//
// Every async method on asio_tls_transport (async_connect / async_read_some /
// async_write / async_handshake) cancels cleanly with the matching
// transport_*_cancelled variant per [2h §6.6]. Tested under BOTH executor
// modes per [2d §4.8]:
//   (a) per_session_strand  — asio::make_strand(pool.get_executor())
//   (b) direct_executor     — bare pool.get_executor()
//
// Applies [[feedback_asio_cospawn_total_cancellation_default]] D-17: the
// coroutine-under-test resets cancellation_type to total so co_spawn's
// terminal-only default does not silently swallow total-cancel.
//
// Applies [[feedback_asio_post_resume_bounces_to_spawn_executor]] D-18: uses
// nested asio::co_spawn to hop executors, NOT co_await asio::post.
//
// NOTE: This test is WIRED-BLOCKED on asio_tls_transport (Phase 3b T026).
// It does NOT compile until the concrete impl exists. Listed here as a
// comment-block target for T029 wiring.
//
// Phase 3a: SOURCE FILE ONLY — wired by T029 once asio_tls_transport ships.

#include <gtest/gtest.h>

// Compile-time shape check: the abstract types from Phase 2 are sufficient
// to verify the error code contract and the cancellation_type reset pattern.
#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/cancellation_type.hpp>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/strand.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <fixpp/transport/tls_transport.hpp>
#include <fixpp/transport/transport.hpp>
#include <fixpp/transport/transport_errors.hpp>
#include <future>
#include <optional>

namespace {

using namespace fixpp::core;
using namespace fixpp::transport;

// ─────────────────────────────────────────────────────────────────────────────
// DISABLED cells — asio_tls_transport ships, but exercising each cancellation
// variant on the production impl requires a server+client SslCtxConfig fixture
// pair (same blocker as test_listener_acceptor's DISABLED_ Logon round-trip).
// Pending the post-MVP fixture pair per /simplify Agent-2 audit + the
// `[[feedback_simplify_pass_catches_9th_burn]]` 13th-burn lesson.
// ─────────────────────────────────────────────────────────────────────────────

// Cell 1 (per_session_strand): async_connect cancelled → transport_connect_cancelled
// [2h §6.6]:1194; D-17 total cancellation reset; [2h §9 seam #5]
TEST(DISABLED_CancellationPropagation, ConnectCancelledStrand) {
    // REQUIRES: make_asio_tls_transport (T026) + real SslCtxConfig (T027).
    // The test structure:
    //   1. Construct asio_tls_transport on per_session_strand.
    //   2. co_spawn async_connect to a non-routable address (192.0.2.1:7777 —
    //      TEST-NET-1 per RFC 5737, guaranteed to time out quickly under cancel).
    //   3. Immediately fire cancellation_signal with cancellation_type::total.
    //   4. Assert result == unexpected{transport_connect_cancelled}.
    GTEST_SKIP() << "Pending server+client SslCtxConfig fixture pair (post-MVP)";
}

// Cell 2 (direct_executor): async_connect cancelled → transport_connect_cancelled
TEST(DISABLED_CancellationPropagation, ConnectCancelledDirect) {
    GTEST_SKIP() << "Pending server+client SslCtxConfig fixture pair (post-MVP)";
}

// Cell 3 (per_session_strand): async_read_some cancelled → transport_read_cancelled
// Requires a connected+handshaken transport; cancellation_type::total.
TEST(DISABLED_CancellationPropagation, ReadCancelledStrand) {
    GTEST_SKIP() << "Pending server+client SslCtxConfig fixture pair (post-MVP)";
}

// Cell 4 (direct_executor): async_read_some cancelled → transport_read_cancelled
TEST(DISABLED_CancellationPropagation, ReadCancelledDirect) {
    GTEST_SKIP() << "Pending server+client SslCtxConfig fixture pair (post-MVP)";
}

// Cell 5 (per_session_strand): async_write cancelled → transport_write_cancelled
// Per [2h §6.6] the persisted frame is NOT rolled back; cancel is write-side only.
TEST(DISABLED_CancellationPropagation, WriteCancelledStrand) {
    GTEST_SKIP() << "Pending server+client SslCtxConfig fixture pair (post-MVP)";
}

// Cell 6 (direct_executor): async_write cancelled → transport_write_cancelled
TEST(DISABLED_CancellationPropagation, WriteCancelledDirect) {
    GTEST_SKIP() << "Pending server+client SslCtxConfig fixture pair (post-MVP)";
}

// Cell 7 (per_session_strand): async_handshake cancelled → transport_handshake_cancelled
// The SSL* state is broken after cancellation; caller MUST close().
TEST(DISABLED_CancellationPropagation, HandshakeCancelledStrand) {
    GTEST_SKIP() << "Pending server+client SslCtxConfig fixture pair (post-MVP)";
}

// Cell 8 (direct_executor): async_handshake cancelled → transport_handshake_cancelled
TEST(DISABLED_CancellationPropagation, HandshakeCancelledDirect) {
    GTEST_SKIP() << "Pending server+client SslCtxConfig fixture pair (post-MVP)";
}

// ─────────────────────────────────────────────────────────────────────────────
// Static/compile-time assertions: error codes used in seam #5 exist.
// These run NOW (Phase 3a) and confirm the Phase 2 foundation.
// ─────────────────────────────────────────────────────────────────────────────

TEST(CancellationPropagation, ErrorCodesPresent) {
    // Verify that every cancellation variant required by seam #5 has a slot.
    EXPECT_EQ(static_cast<int>(error::transport_connect_cancelled), 111);
    EXPECT_EQ(static_cast<int>(error::transport_read_cancelled), 112);
    EXPECT_EQ(static_cast<int>(error::transport_write_cancelled), 113);
    EXPECT_EQ(static_cast<int>(error::transport_handshake_cancelled), 114);
}

// Verify D-17 pattern: asio::cancellation_type::total is NOT terminal-only.
// The co_spawn default is terminal; tests that depend on total MUST reset.
// This assertion documents the requirement without requiring the concrete impl.
TEST(CancellationPropagation, TotalVsTerminalDistinct) {
    // cancellation_type::total must differ from cancellation_type::terminal.
    EXPECT_NE(static_cast<int>(asio::cancellation_type::total),
              static_cast<int>(asio::cancellation_type::terminal));
    // and must differ from cancellation_type::none.
    EXPECT_NE(static_cast<int>(asio::cancellation_type::total),
              static_cast<int>(asio::cancellation_type::none));
}

// Verify the namespace alias re-export for ergonomic at-site use.
TEST(CancellationPropagation, NamespaceAliasConsistency) {
    EXPECT_EQ(errors::transport_connect_cancelled, error::transport_connect_cancelled);
    EXPECT_EQ(errors::transport_read_cancelled, error::transport_read_cancelled);
    EXPECT_EQ(errors::transport_write_cancelled, error::transport_write_cancelled);
    EXPECT_EQ(errors::transport_handshake_cancelled, error::transport_handshake_cancelled);
}

}  // namespace
