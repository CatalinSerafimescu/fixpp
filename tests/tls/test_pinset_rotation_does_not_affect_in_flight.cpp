// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/tls/test_pinset_rotation_does_not_affect_in_flight.cpp
// T014 — [2g §9 seam #15] Mid-session rotation does NOT affect in-flight handshake.
//
// BINDING CONTRACT ([2g §6.5.1]): the TLS handshake captures Pinset::snapshot()
// ONCE before per-peer-cert lookup and scans the captured snapshot for the entire
// handshake. Concurrent add/remove on a separate thread must NOT change the
// snapshot observed by the in-flight handshake.
//
// Witness:
//   1. Pinset has pin OLD.
//   2. In-flight "handshake" captures snapshot (holds shared_ptr<const snapshot>).
//   3. Rotation thread: add NEW, remove OLD (concurrent with in-flight).
//   4. In-flight snapshot still sees OLD, not NEW.
//   5. Fresh find() / snapshot() sees NEW, not OLD.

#include <gtest/gtest.h>
#include <fixpp/tls/pinset.hpp>
#include <fixpp/tls/certificate.hpp>

#include <atomic>
#include <cstddef>
#include <thread>

namespace {

using fixpp::tls::Certificate;
using fixpp::tls::Pinset;
using fixpp::tls::pin_fingerprint;

Certificate make_cert(pin_fingerprint const& fp, std::string_view dn) {
    Certificate c{};
    c.sha256_       = fp;
    c.subject_dn_   = dn;
    c.x509_version_ = 3;
    return c;
}

constexpr pin_fingerprint kOld = [] {
    pin_fingerprint f{};
    f[0] = std::byte{0x01};
    return f;
}();

constexpr pin_fingerprint kNew = [] {
    pin_fingerprint f{};
    f[0] = std::byte{0x02};
    return f;
}();

TEST(PinsetRotation, InFlightSnapshotUnaffectedByRotation) {
    Pinset ps;
    ASSERT_TRUE(ps.add(make_cert(kOld, "CN=old")).has_value());

    // Step 2: capture snapshot — simulates handshake-start snapshot acquisition.
    auto in_flight_snap = ps.snapshot();
    ASSERT_NE(in_flight_snap, nullptr);
    ASSERT_EQ(in_flight_snap->size(), 1u);

    std::atomic<bool> rotation_done{false};

    // Step 3: rotation thread — add NEW then remove OLD.
    std::thread rotator([&] {
        auto r1 = ps.add(make_cert(kNew, "CN=new"));
        EXPECT_TRUE(r1.has_value()) << "add(new) must succeed";
        auto r2 = ps.remove(kOld);
        EXPECT_TRUE(r2.has_value()) << "remove(old) must succeed";
        rotation_done.store(true, std::memory_order_release);
    });

    rotator.join();
    ASSERT_TRUE(rotation_done.load());

    // Step 4: in-flight snapshot still sees OLD.
    ASSERT_EQ(in_flight_snap->size(), 1u)
        << "in-flight snapshot must not be mutated by rotation";
    EXPECT_EQ((*in_flight_snap)[0].sha256, kOld)
        << "in-flight snapshot must still contain OLD pin";

    // Step 5: fresh snapshot sees NEW, not OLD.
    auto fresh_snap = ps.snapshot();
    ASSERT_NE(fresh_snap, nullptr);
    ASSERT_EQ(fresh_snap->size(), 1u)
        << "post-rotation snapshot must contain exactly 1 pin";
    EXPECT_EQ((*fresh_snap)[0].sha256, kNew)
        << "post-rotation snapshot must contain NEW pin";

    // find() also reflects the rotation.
    EXPECT_FALSE(ps.find(kOld).found()) << "OLD pin must be absent after rotation";
    EXPECT_TRUE(ps.find(kNew).found())  << "NEW pin must be present after rotation";
}

}  // namespace
