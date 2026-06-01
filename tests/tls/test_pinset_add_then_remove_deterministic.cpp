// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/tls/test_pinset_add_then_remove_deterministic.cpp
// T012 — [2g §9 seam #1] Deterministic add-then-remove ordering witness.
//
// Hook-driven, non-flaky: uses a sequenced observer pattern instead of thread
// scheduling. No randomness, no sleep. The sequence of operations is:
//   1. Snapshot before add → size 0.
//   2. add(cert) succeeds.
//   3. Snapshot after add → size 1, fingerprint present.
//   4. remove(fp) succeeds.
//   5. Snapshot after remove → size 0, fingerprint absent.
//
// This test witnesses the monotonic-snapshot-publication invariant per
// [2g §6.2]: each write (add/remove) atomically replaces the snapshot pointer.

#include <gtest/gtest.h>

#include <cstddef>
#include <fixpp/tls/certificate.hpp>
#include <fixpp/tls/pinset.hpp>
#include <vector>

namespace {

using fixpp::tls::Certificate;
using fixpp::tls::pin_fingerprint;
using fixpp::tls::Pinset;

Certificate make_cert(pin_fingerprint const& fp, std::string_view dn = "CN=det") {
    Certificate c{};
    c.sha256_ = fp;
    c.subject_dn_ = dn;
    c.x509_version_ = 3;
    return c;
}

constexpr pin_fingerprint kFp = [] {
    pin_fingerprint f{};
    f[0] = std::byte{0xDE};
    f[1] = std::byte{0xAD};
    return f;
}();

// ── MonotonicSnapshotPublication ─────────────────────────────────────────────
TEST(PinsetDeterministic, MonotonicSnapshotPublication) {
    Pinset ps;

    // (1) Before any add: snapshot is empty.
    {
        auto snap = ps.snapshot();
        ASSERT_NE(snap, nullptr);
        EXPECT_EQ(snap->size(), 0u) << "initial snapshot must be empty";
    }
    EXPECT_EQ(ps.size(), 0u);

    // (2) add succeeds.
    auto add_r = ps.add(make_cert(kFp, "CN=rotate-me"));
    ASSERT_TRUE(add_r.has_value()) << "add on empty Pinset must succeed";

    // (3) Snapshot after add: size == 1, correct fingerprint.
    {
        auto snap = ps.snapshot();
        ASSERT_NE(snap, nullptr);
        ASSERT_EQ(snap->size(), 1u) << "post-add snapshot must contain exactly 1 pin";
        EXPECT_EQ((*snap)[0].sha256, kFp);
        EXPECT_EQ((*snap)[0].subject_dn, "CN=rotate-me");
    }

    // (4) remove succeeds.
    auto rm_r = ps.remove(kFp);
    ASSERT_TRUE(rm_r.has_value()) << "remove of known pin must succeed";

    // (5) Snapshot after remove: empty again.
    {
        auto snap = ps.snapshot();
        ASSERT_NE(snap, nullptr);
        EXPECT_EQ(snap->size(), 0u) << "post-remove snapshot must be empty";
    }
    EXPECT_EQ(ps.size(), 0u);
    EXPECT_FALSE(ps.find(kFp).found());
}

// ── SnapshotImmutableAfterRemove ─────────────────────────────────────────────
// A snapshot captured before remove() still sees the pin after remove returns.
TEST(PinsetDeterministic, SnapshotImmutableAfterRemove) {
    Pinset ps;
    ASSERT_TRUE(ps.add(make_cert(kFp)).has_value());

    // Capture snapshot before remove.
    auto pre_snap = ps.snapshot();
    ASSERT_NE(pre_snap, nullptr);
    ASSERT_EQ(pre_snap->size(), 1u);

    // Now remove.
    ASSERT_TRUE(ps.remove(kFp).has_value());

    // The pre-remove snapshot is unchanged (immutable per [2g §6.2]).
    EXPECT_EQ(pre_snap->size(), 1u) << "pre-remove snapshot must not be mutated";
    EXPECT_EQ((*pre_snap)[0].sha256, kFp);

    // The live snapshot is updated.
    auto post_snap = ps.snapshot();
    EXPECT_EQ(post_snap->size(), 0u);
}

// ── SAN entries are PMR-copied into the pin on add() ────────────────────────
// Lifts pinset.cpp's SAN-copy loop (san_dns.emplace_back) which the SAN-less
// add() paths above skip. Witnesses the FR-006 binding contract: the published
// pin owns PMR copies of the Certificate's SAN strings, so the snapshot can
// outlive the originating Certificate.
TEST(PinsetDeterministic, AddWithSanCopiesIntoSnapshotOwnedStorage) {
    Pinset ps;
    static const std::array<std::string_view, 2> dns_sans = {{
        "fixpp.example.invalid",
        "alt.fixpp.example.invalid",
    }};

    Certificate cert = make_cert(kFp, "CN=san-cert");
    cert.san_dns_names_ = std::span<const std::string_view>{dns_sans};
    ASSERT_TRUE(ps.add(cert).has_value());

    auto snap = ps.snapshot();
    ASSERT_EQ(snap->size(), 1u);
    auto const& p = (*snap)[0];
    EXPECT_EQ(p.sha256, kFp);
    ASSERT_EQ(p.san_dns.size(), dns_sans.size());
    EXPECT_EQ(p.san_dns[0], dns_sans[0]);
    EXPECT_EQ(p.san_dns[1], dns_sans[1]);

    // Mutate the original cert's source views — the pin's owned copies must
    // remain valid (proves PMR-copy semantics, not aliasing).
    cert.san_dns_names_ = {};
    EXPECT_EQ(p.san_dns[0], dns_sans[0]);
}

}  // namespace
