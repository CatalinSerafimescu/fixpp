// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/conformance/test_fixs_rotation.cpp
// T020 — [2g §9 seam #3] FIXS-RC1 §5 recorded rotation cross-handshake atomicity.
//
// Drives the MockTransport replay harness from T020a against a Pinset rotation
// scenario. Verifies the binding contract from [2g §6.5.1]:
//
//   "TLS handshake-time pinset access MUST use Pinset::snapshot() captured
//    once at handshake start, and scan the captured snapshot, not call
//    find() repeatedly."
//
// NOTE: byte streams are SYNTHESISED (not a real OpenSSL capture). The
// contract under test is Pinset cross-handshake atomicity, NOT TLS wire format.
// See mock_transport.hpp for the explicit note.
//
// Scenario:
//   Handshake 1 (old cert):
//     - Capture snapshot; OLD pin present; scan succeeds.
//     - Transport replays old-cert exchange.
//     - Rotation: add NEW, remove OLD (concurrent with "in-flight" handshake).
//       The handshake's captured snapshot is unaffected.
//
//   Handshake 2 (new cert):
//     - Capture fresh snapshot; NEW pin present, OLD absent; scan succeeds.
//     - Transport replays new-cert exchange.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <fixpp/tls/certificate.hpp>
#include <fixpp/tls/pinset.hpp>
#include <memory>

#include "mock_transport.hpp"

namespace {

using fixpp::tls::Certificate;
using fixpp::tls::pin_fingerprint;
using fixpp::tls::pin_snapshot;
using fixpp::tls::Pinset;
using fixpp::tls::test::direction;
using fixpp::tls::test::make_handshake_script;
using fixpp::tls::test::MockTransport;

Certificate make_cert(pin_fingerprint const& fp, std::string_view dn) {
    Certificate c{};
    c.sha256_ = fp;
    c.subject_dn_ = dn;
    c.x509_version_ = 3;
    return c;
}

constexpr pin_fingerprint kOld = [] {
    pin_fingerprint f{};
    f[0] = std::byte{0xA1};
    return f;
}();

constexpr pin_fingerprint kNew = [] {
    pin_fingerprint f{};
    f[0] = std::byte{0xA2};
    return f;
}();

// Simulate "handshake-time" Pinset scan: capture snapshot once, scan for fp.
// Returns true if fp is found in the captured snapshot.
bool handshake_verify(std::shared_ptr<const pin_snapshot> snap, pin_fingerprint const& peer_fp) {
    for (auto const& p : *snap) {
        if (p.sha256 == peer_fp) return true;
    }
    return false;
}

// ── CrossHandshakeAtomicity ───────────────────────────────────────────────────
TEST(FixsRotation, CrossHandshakeAtomicity) {
    Pinset ps;
    ASSERT_TRUE(ps.add(make_cert(kOld, "CN=old-cert")).has_value());

    // ── Handshake 1: old cert ────────────────────────────────────────────────
    // Step 1: capture snapshot at handshake start.
    auto snap1 = ps.snapshot();
    ASSERT_NE(snap1, nullptr);
    ASSERT_EQ(snap1->size(), 1u);

    // Step 2: verify old cert against captured snapshot.
    EXPECT_TRUE(handshake_verify(snap1, kOld))
        << "Handshake 1: OLD pin must be present in snapshot";
    EXPECT_FALSE(handshake_verify(snap1, kNew))
        << "Handshake 1: NEW pin must not yet be in snapshot";

    // Step 3: run the mock transport (simulates TLS ClientHello/ServerHello).
    MockTransport transport1{make_handshake_script("old-cert")};
    {
        auto hello_bytes = make_handshake_script("old-cert");
        transport1.send(hello_bytes[0].bytes);
        auto peer_reply = transport1.recv();
        (void)peer_reply;
    }
    EXPECT_TRUE(transport1.done()) << "all scripted frames must be consumed";

    // ── Rotation happens between handshake 1 and 2 ───────────────────────────
    ASSERT_TRUE(ps.add(make_cert(kNew, "CN=new-cert")).has_value());
    ASSERT_TRUE(ps.remove(kOld).has_value());

    // The snap1 captured above is UNAFFECTED by the rotation.
    EXPECT_EQ(snap1->size(), 1u) << "snap1 must be immutable after rotation";
    EXPECT_TRUE(handshake_verify(snap1, kOld))
        << "snap1 still sees OLD (immutable snapshot invariant)";

    // ── Handshake 2: new cert ────────────────────────────────────────────────
    // Fresh snapshot for the next handshake.
    auto snap2 = ps.snapshot();
    ASSERT_NE(snap2, nullptr);
    ASSERT_EQ(snap2->size(), 1u);

    // snap1 and snap2 must be DIFFERENT objects (rotation replaced the pointer).
    EXPECT_NE(snap1.get(), snap2.get()) << "rotation must have published a new snapshot pointer";

    EXPECT_TRUE(handshake_verify(snap2, kNew))
        << "Handshake 2: NEW pin must be present in fresh snapshot";
    EXPECT_FALSE(handshake_verify(snap2, kOld))
        << "Handshake 2: OLD pin must be absent from fresh snapshot";

    MockTransport transport2{make_handshake_script("new-cert")};
    {
        auto hello_bytes = make_handshake_script("new-cert");
        transport2.send(hello_bytes[0].bytes);
        auto peer_reply = transport2.recv();
        (void)peer_reply;
    }
    EXPECT_TRUE(transport2.done()) << "all scripted frames must be consumed";
}

}  // namespace
