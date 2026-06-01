// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/tls/test_pinset_single_thread_ordering.cpp
// T011 — [2g §9 seam #9] Pinset single-thread ordering contract.
//
// Covers:
//  • add / add / remove sequencing on one thread.
//  • duplicate add → tls_pin_already_present.
//  • remove absent → tls_pin_not_found.
//  • pin_view::found() true / false correctness.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <fixpp/core/error.hpp>
#include <fixpp/tls/certificate.hpp>
#include <fixpp/tls/pinset.hpp>

namespace {

using fixpp::core::error;
using fixpp::tls::Certificate;
using fixpp::tls::pin_fingerprint;
using fixpp::tls::Pinset;

// Build a minimal Certificate with a given SHA-256 fingerprint and subject_dn.
// san_dns_names_ / san_uris_ left empty (zero-entry spans are fine).
Certificate make_cert(pin_fingerprint const& fp, std::string_view dn = "CN=test") {
    Certificate c{};
    c.sha256_ = fp;
    c.subject_dn_ = dn;
    c.x509_version_ = 3;
    return c;
}

// Two distinct fingerprints used across tests.
constexpr pin_fingerprint kFp1 = [] {
    pin_fingerprint f{};
    f[0] = std::byte{0x01};
    return f;
}();

constexpr pin_fingerprint kFp2 = [] {
    pin_fingerprint f{};
    f[0] = std::byte{0x02};
    return f;
}();

constexpr pin_fingerprint kAbsent = [] {
    pin_fingerprint f{};
    f[0] = std::byte{0xFF};
    return f;
}();

// ── AddThenFind ───────────────────────────────────────────────────────────────
TEST(PinsetSingleThread, AddThenFind) {
    Pinset ps;
    auto r = ps.add(make_cert(kFp1, "CN=alpha"));
    ASSERT_TRUE(r.has_value()) << "add should succeed on empty Pinset";

    auto v = ps.find(kFp1);
    EXPECT_TRUE(v.found()) << "find must return found after add";
    ASSERT_NE(v.value, nullptr);
    EXPECT_EQ(v.value->subject_dn, "CN=alpha");
}

// ── AddTwoPinsRemoveOne ───────────────────────────────────────────────────────
TEST(PinsetSingleThread, AddTwoPinsRemoveOne) {
    Pinset ps;
    ASSERT_TRUE(ps.add(make_cert(kFp1)).has_value());
    ASSERT_TRUE(ps.add(make_cert(kFp2)).has_value());
    EXPECT_EQ(ps.size(), 2u);

    // Remove first
    auto r = ps.remove(kFp1);
    ASSERT_TRUE(r.has_value()) << "remove of known pin must succeed";

    // kFp1 gone
    EXPECT_FALSE(ps.find(kFp1).found());
    // kFp2 still present
    EXPECT_TRUE(ps.find(kFp2).found());
    EXPECT_EQ(ps.size(), 1u);
}

// ── DuplicateAdd ─────────────────────────────────────────────────────────────
TEST(PinsetSingleThread, DuplicateAdd) {
    Pinset ps;
    ASSERT_TRUE(ps.add(make_cert(kFp1)).has_value());

    auto r2 = ps.add(make_cert(kFp1));
    ASSERT_FALSE(r2.has_value()) << "duplicate add must fail";
    EXPECT_EQ(r2.error(), error::tls_pin_already_present);

    // Size unchanged
    EXPECT_EQ(ps.size(), 1u);
}

// ── RemoveAbsent ─────────────────────────────────────────────────────────────
TEST(PinsetSingleThread, RemoveAbsent) {
    Pinset ps;
    ASSERT_TRUE(ps.add(make_cert(kFp1)).has_value());

    auto r = ps.remove(kAbsent);
    ASSERT_FALSE(r.has_value()) << "remove of absent pin must fail";
    EXPECT_EQ(r.error(), error::tls_pin_not_found);

    // Existing pin unaffected
    EXPECT_EQ(ps.size(), 1u);
    EXPECT_TRUE(ps.find(kFp1).found());
}

// ── PinViewFoundFalseOnMiss ───────────────────────────────────────────────────
TEST(PinsetSingleThread, PinViewFoundFalseOnMiss) {
    Pinset ps;
    auto v = ps.find(kAbsent);
    EXPECT_FALSE(v.found());
    EXPECT_EQ(v.value, nullptr);
    EXPECT_FALSE(static_cast<bool>(v));
}

// ── CapacityExhausted ─────────────────────────────────────────────────────────
TEST(PinsetSingleThread, CapacityExhausted) {
    Pinset::Config cfg;
    cfg.max_pins = 2;
    Pinset ps(cfg);

    ASSERT_TRUE(ps.add(make_cert(kFp1)).has_value());
    ASSERT_TRUE(ps.add(make_cert(kFp2)).has_value());

    pin_fingerprint kFp3{};
    kFp3[0] = std::byte{0x03};
    auto r = ps.add(make_cert(kFp3));
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::tls_pinset_capacity_exhausted);
}

// ── ContainsMatchesFind ───────────────────────────────────────────────────────
TEST(PinsetSingleThread, ContainsMatchesFind) {
    Pinset ps;
    ASSERT_TRUE(ps.add(make_cert(kFp1)).has_value());
    EXPECT_TRUE(ps.contains(kFp1));
    EXPECT_FALSE(ps.contains(kFp2));
}

}  // namespace
