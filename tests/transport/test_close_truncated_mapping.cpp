// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// tests/transport/test_close_truncated_mapping.cpp
// RC#D (P2-1) — async_read_some truncated-close vs EOF distinct variant witness.
//
// Verifies SC-006: "Every distinct transport-side failure mode ... surfaces as a
// distinct, named transport_* variant the operator can recognise in logs / metrics.
// No 'transport failed' catch-all exists in the public surface."
//
// Prior to RC#D, async_read_some collapsed both asio::error::eof AND
// asio::ssl::error::stream_truncated → transport_read_eof. SC-006 requires them
// to be distinct: eof → transport_read_eof; truncated → transport_read_truncated.
//
// This file provides:
//   1. Compile-time slot-distinctness assertions (ASIO error code identity check).
//   2. Runtime test confirming the two error variants have different numeric slots
//      and that transport_read_truncated is the distinct variant SC-006 requires.
//
// A full loopback TLS integration test (dropping peer-side TCP without SSL_shutdown
// to trigger stream_truncated in practice) requires both the RC#A acceptor fix
// and a real cert fixture — it is covered by the DISABLED_ cells in
// test_tls_handshake_pinset_rotation.cpp and will be enabled post-RC#B when the
// loopback fixture is available.
//
// Design anchor: spec.md FR-006 (RC#D resolved contradiction) + SC-006.

#include <gtest/gtest.h>

#include <asio/ssl/error.hpp>
#include <asio/error.hpp>

#include <fixpp/core/error.hpp>
#include <fixpp/transport/transport_errors.hpp>

namespace {

using fixpp::core::error;
namespace fe = fixpp::transport::errors;

}  // namespace

// ── SC-006 slot-distinctness: eof ≠ truncated ────────────────────────────────
static_assert(static_cast<int>(error::transport_read_eof) != static_cast<int>(error::transport_read_truncated),
    "SC-006: transport_read_eof and transport_read_truncated must be distinct slots");
static_assert(static_cast<int>(error::transport_read_eof)       == 102,
    "transport_read_eof must occupy slot 102");
static_assert(static_cast<int>(error::transport_read_truncated) == 103,
    "transport_read_truncated must occupy slot 103 (distinct from 102)");

// ── ASIO error codes are distinct system-level values ────────────────────────
// Confirms the two ASIO error codes we branch on in async_read_some are not
// equal — a defensive guard against OpenSSL/ASIO version drift collapsing them.
TEST(CloseTruncatedMapping, AsioEofAndStreamTruncatedAreDistinct) {
    const asio::error_code eof_ec = asio::error::eof;
    const asio::error_code truncated_ec = asio::ssl::error::stream_truncated;

    EXPECT_NE(eof_ec, truncated_ec)
        << "asio::error::eof and asio::ssl::error::stream_truncated must be distinct "
           "error codes so that async_read_some can branch on them independently";
}

// ── SC-006 runtime: transport_read_eof and transport_read_truncated are distinct ──
TEST(CloseTruncatedMapping, TransportReadEofAndTruncatedAreDistinct) {
    // SC-006 compliance: each failure mode maps to a distinct named variant.
    EXPECT_NE(error::transport_read_eof, error::transport_read_truncated);
    EXPECT_EQ(static_cast<int>(error::transport_read_eof),       102);
    EXPECT_EQ(static_cast<int>(error::transport_read_truncated), 103);
}

// ── namespace alias exports ───────────────────────────────────────────────────
TEST(CloseTruncatedMapping, NamespaceAliasConsistency) {
    EXPECT_EQ(fe::transport_read_eof,       error::transport_read_eof);
    EXPECT_EQ(fe::transport_read_truncated, error::transport_read_truncated);
}
