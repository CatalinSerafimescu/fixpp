// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// tests/transport/test_tls_handshake_pinset_rotation.cpp
// T014 — [2h §9 seam #7] — TLS handshake pinset rotation contract.
//
// 3 scenarios per T014 task description + [2g §6.5.1] BINDING CONTRACT:
//   (a) Pinned mode — handshake succeeds with peer cert matching pinset.
//   (b) Unpinned mtls_ca — handshake succeeds via CA chain (no pinset scan).
//   (c) Rotated mid-handshake — in-flight handshake observes pre-rotation
//       captured snapshot; NEXT handshake observes post-rotation snapshot.
//
// Verifies that captured_pinset in handshake_result is:
//   - Non-null under mtls_pinned profile (snapshot captured at handshake start).
//   - Null under mtls_ca / one_way_ca (no pinset consumed per [2g §4.5.1]).
//
// Also verifies FR-034a: transport_handshake_failed carries the tls_* sub-reason
// when verify_peer rejects (pin-mismatch scenario).
//
// Phase 3a — DISABLED cells require asio_tls_transport (T026) + loopback peer.
// Compile-time / Pinset-API-level tests run NOW.

#include <gtest/gtest.h>

#include <fixpp/transport/transport_errors.hpp>
#include <fixpp/transport/tls_transport.hpp>
#include <fixpp/tls/pinset.hpp>
#include <fixpp/tls/certificate.hpp>
#include <fixpp/tls/peer_identity.hpp>
#include <fixpp/tls/security_profile.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>

namespace {

using namespace fixpp::core;
using namespace fixpp::transport;
using fixpp::tls::Certificate;
using fixpp::tls::Pinset;
using fixpp::tls::pin_fingerprint;
using fixpp::tls::pin_snapshot;

// Build a Certificate with a given SHA-256 fingerprint and subject DN.
// Uses the Certificate's public field shape per certificate.hpp.
inline Certificate make_cert(pin_fingerprint const& fp, std::string_view dn) {
    Certificate c{};
    c.sha256_ = fp;
    c.subject_dn_ = dn;
    c.x509_version_ = 3;
    return c;
}

// Helper: build a pin_fingerprint with first byte = v.
inline pin_fingerprint make_fp(std::byte v) {
    pin_fingerprint fp{};
    fp[0] = v;
    return fp;
}

// ─────────────────────────────────────────────────────────────────────────────
// Phase 3a: Pinset rotation contract at the TYPE LEVEL.
// Verifies [2g §6.5.1] captured-once contract using the Pinset API.
// ─────────────────────────────────────────────────────────────────────────────

// Test: Pinset::snapshot() captures a stable view (add then rotate).
TEST(PinsetRotationContract, SnapshotStableAfterRotation) {
    Pinset ps;
    auto fp_old = make_fp(std::byte{0xA1});
    auto fp_new = make_fp(std::byte{0xB2});

    auto cert_old = make_cert(fp_old, "CN=old.example.com");
    auto cert_new = make_cert(fp_new, "CN=new.example.com");

    auto r = ps.add(cert_old);
    ASSERT_TRUE(r.has_value()) << "add(cert_old) must succeed";

    // Capture snapshot BEFORE rotation (mirrors handshake-time capture).
    auto snap = ps.snapshot();
    ASSERT_NE(snap, nullptr);

    // Rotate: add new, remove old.
    r = ps.add(cert_new);
    ASSERT_TRUE(r.has_value()) << "add(cert_new) must succeed";
    auto r2 = ps.remove(fp_old);
    ASSERT_TRUE(r2.has_value()) << "remove(fp_old) must succeed";

    // The Pinset post-rotation: contains fp_new only.
    EXPECT_FALSE(ps.contains(fp_old))
        << "Pinset must not contain fp_old after rotation";
    EXPECT_TRUE(ps.contains(fp_new))
        << "Pinset must contain fp_new after rotation";

    // Snapshot captured BEFORE rotation still contains fp_old.
    // pin_snapshot = std::pmr::vector<pin>. Scan by sha256 field.
    ASSERT_NE(snap, nullptr);
    bool snap_has_old = std::any_of(snap->begin(), snap->end(),
        [&fp_old](const fixpp::tls::pin& p) { return p.sha256 == fp_old; });
    EXPECT_TRUE(snap_has_old)
        << "Pre-rotation snapshot must contain fp_old even after rotation";

    bool snap_has_new = std::any_of(snap->begin(), snap->end(),
        [&fp_new](const fixpp::tls::pin& p) { return p.sha256 == fp_new; });
    EXPECT_FALSE(snap_has_new)
        << "Pre-rotation snapshot must NOT contain fp_new";

    // A fresh snapshot reflects post-rotation state.
    auto snap2 = ps.snapshot();
    ASSERT_NE(snap2, nullptr);
    bool snap2_has_old = std::any_of(snap2->begin(), snap2->end(),
        [&fp_old](const fixpp::tls::pin& p) { return p.sha256 == fp_old; });
    bool snap2_has_new = std::any_of(snap2->begin(), snap2->end(),
        [&fp_new](const fixpp::tls::pin& p) { return p.sha256 == fp_new; });
    EXPECT_FALSE(snap2_has_old)
        << "Post-rotation snapshot must NOT contain fp_old";
    EXPECT_TRUE(snap2_has_new)
        << "Post-rotation snapshot must contain fp_new";
}

// Test: two snapshots are independent.
TEST(PinsetRotationContract, TwoSnapshotsIndependent) {
    Pinset ps;
    auto fp1 = make_fp(std::byte{0x11});
    auto fp2 = make_fp(std::byte{0x22});

    ps.add(make_cert(fp1, "CN=cert1.example.com"));
    auto s1 = ps.snapshot();

    ps.add(make_cert(fp2, "CN=cert2.example.com"));
    ps.remove(fp1);
    auto s2 = ps.snapshot();

    ASSERT_NE(s1, nullptr);
    ASSERT_NE(s2, nullptr);

    bool s1_has_fp1 = std::any_of(s1->begin(), s1->end(),
        [&fp1](const fixpp::tls::pin& p) { return p.sha256 == fp1; });
    bool s1_has_fp2 = std::any_of(s1->begin(), s1->end(),
        [&fp2](const fixpp::tls::pin& p) { return p.sha256 == fp2; });
    bool s2_has_fp1 = std::any_of(s2->begin(), s2->end(),
        [&fp1](const fixpp::tls::pin& p) { return p.sha256 == fp1; });
    bool s2_has_fp2 = std::any_of(s2->begin(), s2->end(),
        [&fp2](const fixpp::tls::pin& p) { return p.sha256 == fp2; });

    EXPECT_TRUE(s1_has_fp1);
    EXPECT_FALSE(s1_has_fp2);
    EXPECT_FALSE(s2_has_fp1);
    EXPECT_TRUE(s2_has_fp2);
}

// Test: empty pinset snapshot returns non-null empty vector.
TEST(PinsetRotationContract, EmptySnapshotNonNull) {
    Pinset ps;
    auto snap = ps.snapshot();
    EXPECT_NE(snap, nullptr);
    EXPECT_EQ(snap->size(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// handshake_result type-level assertions (Phase 3a).
// ─────────────────────────────────────────────────────────────────────────────

TEST(HandshakeResultContract, CapturedPinsetFieldExists) {
    handshake_result r{};
    // null IFF mtls_ca / one_way_ca per [2g §4.5.1].
    EXPECT_EQ(r.captured_pinset, nullptr);
    EXPECT_TRUE(r.negotiated_cipher.empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// DISABLED integration cells — require asio_tls_transport (T026) + real TLS.
// ─────────────────────────────────────────────────────────────────────────────

// Scenario (a): mtls_pinned — handshake succeeds, captured_pinset non-null.
TEST(DISABLED_TlsHandshakePinsetRotation, PinnedModeHandshakeSucceeds) {
    GTEST_SKIP() << "Requires asio_tls_transport (T026) + loopback TLS peer";
}

// Scenario (b): mtls_ca — handshake succeeds, captured_pinset is null.
TEST(DISABLED_TlsHandshakePinsetRotation, UnpinnedMtlsCaModeHandshakeSucceeds) {
    GTEST_SKIP() << "Requires asio_tls_transport (T026) + loopback TLS peer";
}

// Scenario (c): mid-handshake rotation.
TEST(DISABLED_TlsHandshakePinsetRotation, RotatedMidHandshakeSnapshotStable) {
    GTEST_SKIP() << "Requires asio_tls_transport (T026) + loopback TLS peer";
}

// FR-034a: transport_handshake_failed carries tls_* sub-reason on verify_peer reject.
TEST(DISABLED_TlsHandshakePinsetRotation, HandshakeFailedCarriesTlsSubReason) {
    GTEST_SKIP() << "Requires asio_tls_transport (T026) + loopback TLS peer";
}

}  // namespace
