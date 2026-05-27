// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// tests/transport/test_inflight_exclusivity.cpp
// T017 — [2h §9 seam #15] — in-flight exclusivity contract (RC#3 / Codex P1 #2).
//
// From 2 coroutines bound to the same session strand:
//
//   Cell 1: async_write overlap → second call returns IMMEDIATELY with
//           unexpected{transport_write_in_progress}. The second call MUST NOT
//           race into the underlying ASIO async_write_some initiation.
//
//   Cell 2: async_read_some overlap → second call returns IMMEDIATELY with
//           unexpected{transport_read_in_progress}.
//
//   Cell 3: async_connect overlap → second call returns IMMEDIATELY with
//           unexpected{transport_already_connected}.
//
//   Cell 4: async_handshake overlap → second call returns IMMEDIATELY with
//           unexpected{transport_already_connected}.
//
// All four cells verify per [2h §4.1] normative API-level exclusivity contract
// + spec FR-007.
//
// Phase 3a — DISABLED cells require asio_tls_transport (T026). Compile-time
// error code assertions run NOW.

#include <gtest/gtest.h>

#include <fixpp/transport/transport_errors.hpp>
#include <fixpp/transport/transport.hpp>
#include <fixpp/transport/tls_transport.hpp>

namespace {

namespace fc = fixpp::core;
namespace fe = fixpp::transport::errors;

// ─────────────────────────────────────────────────────────────────────────────
// Compile-time contract: error codes for the exclusivity contract.
// ─────────────────────────────────────────────────────────────────────────────

TEST(InflightExclusivity, ErrorCodesPresent) {
    // transport_write_in_progress: slot 100.
    EXPECT_EQ(static_cast<int>(fc::error::transport_write_in_progress), 100);
    // transport_read_in_progress: slot 99.
    EXPECT_EQ(static_cast<int>(fc::error::transport_read_in_progress),  99);
    // transport_already_connected: slot 97 (one-shot guard for connect/handshake).
    EXPECT_EQ(static_cast<int>(fc::error::transport_already_connected), 97);
}

// Verify these are DISTINCT codes (overlap detection → different diagnostics).
TEST(InflightExclusivity, ExclusivityCodesDistinct) {
    EXPECT_NE(fc::error::transport_write_in_progress,
              fc::error::transport_read_in_progress);
    EXPECT_NE(fc::error::transport_write_in_progress,
              fc::error::transport_already_connected);
    EXPECT_NE(fc::error::transport_read_in_progress,
              fc::error::transport_already_connected);
}

// Namespace alias consistency.
TEST(InflightExclusivity, NamespaceAliasConsistency) {
    EXPECT_EQ(fe::transport_write_in_progress,
              fc::error::transport_write_in_progress);
    EXPECT_EQ(fe::transport_read_in_progress,
              fc::error::transport_read_in_progress);
    EXPECT_EQ(fe::transport_already_connected,
              fc::error::transport_already_connected);
}

// ─────────────────────────────────────────────────────────────────────────────
// DISABLED integration cells — require asio_tls_transport (T026).
// ─────────────────────────────────────────────────────────────────────────────

// Cell 1: write-overlap.
// From 2 coroutines on the same session strand:
//   - Coroutine A: co_await transport.async_write(buf_a)  [suspends — peer slow]
//   - Coroutine B: result = co_await transport.async_write(buf_b) [runs immediately]
//   - ASSERT: B.result == unexpected{transport_write_in_progress}
//   - ASSERT: B completed WITHOUT waiting for A (IMMEDIATE return by contract)
//   - B MUST NOT initiate a second underlying async_write_some operation.
TEST(DISABLED_InflightExclusivity, WriteOverlapReturnImmediately) {
    // Implementation note: create a "slow" loopback peer that introduces
    // artificial back-pressure. Strand serialisation ensures B runs only after
    // A suspends; A's awaitable is still pending when B's exclusivity guard
    // fires. Use asio::strand<pool executor> for the session strand.
    GTEST_SKIP() << "Requires asio_tls_transport (T026)";
}

// Cell 2: read-overlap.
// Same structure as Cell 1 for async_read_some.
TEST(DISABLED_InflightExclusivity, ReadOverlapReturnImmediately) {
    GTEST_SKIP() << "Requires asio_tls_transport (T026)";
}

// Cell 3: connect-overlap (one-shot guard).
// Issue async_connect twice on the same transport instance.
// Second call → unexpected{transport_already_connected}.
TEST(DISABLED_InflightExclusivity, ConnectOneShot) {
    GTEST_SKIP() << "Requires asio_tls_transport (T026)";
}

// Cell 4: handshake-overlap (one-shot guard).
// Issue async_handshake twice on the same transport instance after connect.
// Second call → unexpected{transport_already_connected}.
TEST(DISABLED_InflightExclusivity, HandshakeOneShot) {
    GTEST_SKIP() << "Requires asio_tls_transport (T026)";
}

}  // namespace
