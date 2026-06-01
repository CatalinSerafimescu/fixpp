// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/tls/test_pinset_per_counterparty_sharing.cpp
// T017 — FR-009a / Q5: per-counterparty shared_ptr<Pinset>.
//
// Two shared_ptr<Pinset> aliases (same instance) see rotation atomically
// across both readers on the next find/snapshot.
//
// Models the recommended pattern from contracts/pinset.hpp §FR-009a:
//   auto pinset = std::make_shared<Pinset>();
//   SessionConfig session_a; session_a.pinset = pinset;
//   SessionConfig session_b; session_b.pinset = pinset;
//   // Rotation: both sessions observe the new snapshot atomically.

#include <gtest/gtest.h>

#include <cstddef>
#include <fixpp/tls/certificate.hpp>
#include <fixpp/tls/pinset.hpp>
#include <memory>

namespace {

using fixpp::tls::Certificate;
using fixpp::tls::pin_fingerprint;
using fixpp::tls::Pinset;

Certificate make_cert(pin_fingerprint const& fp, std::string_view dn) {
    Certificate c{};
    c.sha256_ = fp;
    c.subject_dn_ = dn;
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

TEST(PinsetPerCounterparty, TwoAliasesObserveRotationAtomically) {
    // Both sessions share the same Pinset instance.
    auto pinset = std::make_shared<Pinset>();

    std::shared_ptr<Pinset> session_a_pins = pinset;
    std::shared_ptr<Pinset> session_b_pins = pinset;

    // Pre-state: add OLD pin.
    ASSERT_TRUE(pinset->add(make_cert(kOld, "CN=old")).has_value());

    // Both readers see OLD.
    EXPECT_TRUE(session_a_pins->find(kOld).found()) << "session_a must find OLD before rotation";
    EXPECT_TRUE(session_b_pins->find(kOld).found()) << "session_b must find OLD before rotation";
    EXPECT_FALSE(session_a_pins->find(kNew).found());
    EXPECT_FALSE(session_b_pins->find(kNew).found());

    // Rotation: add NEW, remove OLD (through the Pinset owner).
    ASSERT_TRUE(pinset->add(make_cert(kNew, "CN=new")).has_value());
    ASSERT_TRUE(pinset->remove(kOld).has_value());

    // Both readers immediately observe the new snapshot (same instance).
    EXPECT_FALSE(session_a_pins->find(kOld).found())
        << "session_a must not find OLD after rotation";
    EXPECT_FALSE(session_b_pins->find(kOld).found())
        << "session_b must not find OLD after rotation";
    EXPECT_TRUE(session_a_pins->find(kNew).found()) << "session_a must find NEW after rotation";
    EXPECT_TRUE(session_b_pins->find(kNew).found()) << "session_b must find NEW after rotation";

    // Snapshot view is the same object for both aliases.
    auto snap_a = session_a_pins->snapshot();
    auto snap_b = session_b_pins->snapshot();
    EXPECT_EQ(snap_a.get(), snap_b.get())
        << "both aliases must observe the same snapshot pointer (atomic publication)";
    ASSERT_EQ(snap_a->size(), 1u);
    EXPECT_EQ((*snap_a)[0].sha256, kNew);
}

}  // namespace
