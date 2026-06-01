// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// tests/transport/test_compid_tls_identity_binding.cpp
// T016 — [2h §9 seam #9] — CompID ↔ TLS identity binding, transport side.
//
// Acceptor scenario: peer cert CN → handshake_result.peer_id.subject_dn_view()
// must equal the cert's Subject DN. The FSM CompID-binding policy (owned by
// session Phase-4 spec) maps peer DN → TARGETCOMPID. This test verifies the
// TRANSPORT-DELIVERY side only: that handshake_result.peer_id carries the
// correct identity by value.
//
// For the binding policy itself (session Phase-4) we mock a simple lookup step
// that reads peer_id.subject_dn_view() and asserts the expected DN.
//
// Cross-cut: [2h §9 seam #9], T-041, FR-013, FR-041.
//
// Phase 3a — DISABLED cells require asio_tls_transport acceptor-side (T026/T037).
// Compile-time/unit tests on peer_identity run NOW.

#include <gtest/gtest.h>

#include <fixpp/tls/peer_identity.hpp>
#include <fixpp/transport/tls_transport.hpp>
#include <fixpp/transport/transport_errors.hpp>
#include <string>
#include <string_view>

namespace {

using fixpp::tls::peer_identity;
using fixpp::transport::handshake_result;
using namespace fixpp::core;

// ─────────────────────────────────────────────────────────────────────────────
// peer_identity unit-level tests (Phase 3a).
// peer_identity stores subject_dn as a pmr::string. We construct it directly
// using the default constructor and assign subject_dn manually (test-only path).
// In production, peer_identity is populated by verify_peer.
// ─────────────────────────────────────────────────────────────────────────────

// Helper: build a peer_identity with a given subject DN (test stub).
// NOTE: In production, peer_identity is constructed exclusively by verify_peer.
// Here we use the default constructor + direct field assignment (test-only).
inline peer_identity make_pid_with_dn(std::string_view dn) {
    peer_identity pid{};
    pid.subject_dn = std::pmr::string{dn, std::pmr::new_delete_resource()};
    return pid;
}

// Verify that peer_identity.subject_dn_view() returns the assigned DN.
TEST(CompidTlsIdentityBinding, SubjectDnViewReturnsExpectedDn) {
    auto pid = make_pid_with_dn("CN=peer42.example.com,O=FixppTest,C=US");
    std::string_view dn = pid.subject_dn_view();
    EXPECT_EQ(dn, "CN=peer42.example.com,O=FixppTest,C=US");
}

// Verify that the FSM-side binding step (mock) can extract TARGETCOMPID from DN.
// This is the "mock the FSM-side binding step" required by T016 description.
TEST(CompidTlsIdentityBinding, MockFsmBindingStepExtractsPeer) {
    // Simulate the transport-delivery: handshake_result.peer_id holds the cert.
    handshake_result hr{};
    hr.peer_id = make_pid_with_dn("CN=peer42.example.com");

    // Mock FSM binding policy: extract CN from subject_dn_view().
    std::string_view dn = hr.peer_id.subject_dn_view();
    ASSERT_FALSE(dn.empty());

    // Simple CN extraction (in production, the session Phase-4 policy owns this).
    auto cn_pos = dn.find("CN=");
    ASSERT_NE(cn_pos, std::string_view::npos);
    std::string_view cn_field = dn.substr(cn_pos + 3);
    // Strip trailing fields after comma (if any).
    auto comma_pos = cn_field.find(',');
    if (comma_pos != std::string_view::npos) {
        cn_field = cn_field.substr(0, comma_pos);
    }
    EXPECT_EQ(cn_field, "peer42.example.com");

    // Mock binding policy: TARGETCOMPID = raw peer CN.
    // (The real policy is session Phase-4 FR-013 / FR-041.)
    EXPECT_EQ(std::string(cn_field), "peer42.example.com");
}

// Verify that a mismatch in the mock binding step is detectable.
TEST(CompidTlsIdentityBinding, MockFsmBindingRejectsMismatch) {
    handshake_result hr{};
    hr.peer_id = make_pid_with_dn("CN=unknown.example.com");

    // Mock policy: only "peer42.example.com" is allowed.
    std::string_view dn = hr.peer_id.subject_dn_view();
    EXPECT_FALSE(dn.empty());
    EXPECT_EQ(dn.find("peer42"), std::string_view::npos)
        << "Unknown CN must not match the allowed peer42 binding";
}

// handshake_result.captured_pinset is null for mtls_ca / one_way_ca profiles
// per [2g §4.5.1]. Verify the transport-delivery shape.
TEST(CompidTlsIdentityBinding, CapturedPinsetNullForUnpinnedProfile) {
    handshake_result hr{};
    EXPECT_EQ(hr.captured_pinset, nullptr)
        << "Default-constructed handshake_result must have null captured_pinset";
}

// Verify that handshake_result.peer_id is movable (FSM holds it by value
// across session lifetime per [2c §4.8] owning_message_t<> precedent).
TEST(CompidTlsIdentityBinding, HandshakeResultPeerIdMovable) {
    handshake_result hr1{};
    hr1.peer_id = make_pid_with_dn("CN=peer99.example.com");

    // Move into FSM-held value.
    handshake_result hr2 = std::move(hr1);
    EXPECT_EQ(hr2.peer_id.subject_dn_view(), "CN=peer99.example.com");
}

// ─────────────────────────────────────────────────────────────────────────────
// DISABLED integration cells — require asio_tls_transport acceptor (T026/T037).
// ─────────────────────────────────────────────────────────────────────────────

// Acceptor scenario: real TLS handshake → peer_id carries correct cert identity.
TEST(DISABLED_CompidTlsIdentityBinding, AcceptorHandshakeDeliversPeerIdentity) {
    // Flow:
    //   1. Stand up asio_listener on 127.0.0.1:0 (OS port).
    //   2. Connect asio_tls_transport (initiator) with leaf cert CN=peer42.example.com.
    //   3. asio_listener::async_accept → TlsTransport* for accepted connection.
    //   4. acceptor-side: async_handshake (requesting client cert via mtls_*).
    //   5. ASSERT: handshake_result.peer_id.subject_dn_view() contains CN=peer42.
    //   6. Mock FSM binding: verify the mapping works on the real identity.
    //   7. Verify mismatch scenario: present wrong cert → mock rejects.
    GTEST_SKIP() << "Requires asio_tls_transport acceptor (T026/T037)";
}

}  // namespace
