// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/tls/test_pinset_snapshot_outlives_pinset.cpp
// T016 — [2g §9 seam #18] Post-~Pinset() snapshot lifetime.
//
// BINDING CONTRACT ([2g §4.6] / round-3 P1-1 close):
//   Config::mr MUST outlive every shared_ptr<const pin_snapshot> the Pinset
//   ever publishes — NOT merely the Pinset instance itself.
//
// POSITIVE test: Pinset built against scope-outer monotonic_buffer_resource;
// add 2 pins, snapshot once, drop Pinset, read pin fields from the snapshot.
// Under ASan + TSan: no UAF.
//
// NEGATIVE companion: if the MR scope is INSIDE the Pinset scope, accessing
// the snapshot after MR teardown is a UAF. This is expressed as a manual ASan
// witness gated by FIXPP_TEST_NEGATIVE_MR_LIFETIME env-var so CI skips it.
// To run manually:
//   FIXPP_TEST_NEGATIVE_MR_LIFETIME=1 ./test_pinset_snapshot_outlives_pinset
// (Expected: ASan reports heap-use-after-free.)

#include <gtest/gtest.h>
#include <fixpp/tls/pinset.hpp>
#include <fixpp/tls/certificate.hpp>

#include <cstddef>
#include <cstdlib>
#include <memory_resource>
#include <optional>
#include <string>

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

constexpr pin_fingerprint kFp1 = [] {
    pin_fingerprint f{};
    f[0] = std::byte{0x10};
    return f;
}();

constexpr pin_fingerprint kFp2 = [] {
    pin_fingerprint f{};
    f[0] = std::byte{0x20};
    return f;
}();

// ── Positive: snapshot outlives Pinset when MR outlives both ─────────────────
TEST(PinsetSnapshotOutlivesPinset, PositiveSnapshotValidAfterDrop) {
    // Arena lives in this scope — outlives Pinset + snapshot.
    std::array<std::byte, 4096> buf{};
    std::pmr::monotonic_buffer_resource arena{buf.data(), buf.size(),
                                              std::pmr::null_memory_resource()};

    std::shared_ptr<const fixpp::tls::pin_snapshot> snap;

    {
        Pinset::Config cfg;
        cfg.mr = &arena;
        Pinset ps{cfg};

        ASSERT_TRUE(ps.add(make_cert(kFp1, "CN=first")).has_value());
        ASSERT_TRUE(ps.add(make_cert(kFp2, "CN=second")).has_value());

        snap = ps.snapshot();
        ASSERT_NE(snap, nullptr);
        ASSERT_EQ(snap->size(), 2u);
    }
    // Pinset is destroyed here — arena is NOT.

    // Reading from the snapshot must be safe (MR still alive).
    EXPECT_EQ(snap->size(), 2u);
    EXPECT_EQ((*snap)[0].sha256, kFp1);
    EXPECT_EQ((*snap)[0].subject_dn, "CN=first");
    EXPECT_EQ((*snap)[1].sha256, kFp2);
    EXPECT_EQ((*snap)[1].subject_dn, "CN=second");
}

// ── Manual negative witness (MANUAL ASan witness — skipped by default) ───────
// See file-level comment. Gated by FIXPP_TEST_NEGATIVE_MR_LIFETIME env-var.
TEST(PinsetSnapshotOutlivesPinset, NegativeManualAsan) {
    const char* env = std::getenv("FIXPP_TEST_NEGATIVE_MR_LIFETIME");
    if (!env || std::string(env) != "1") {
        GTEST_SKIP() << "Skipped in CI; set FIXPP_TEST_NEGATIVE_MR_LIFETIME=1 to run the ASan UAF witness";
    }

    // MR scope INSIDE the block that holds the snapshot — UAF on access.
    std::shared_ptr<const fixpp::tls::pin_snapshot> snap;
    {
        std::array<std::byte, 4096> buf{};
        std::pmr::monotonic_buffer_resource arena{buf.data(), buf.size(),
                                                  std::pmr::null_memory_resource()};
        Pinset::Config cfg;
        cfg.mr = &arena;
        Pinset ps{cfg};
        ASSERT_TRUE(ps.add(make_cert(kFp1, "CN=dangling")).has_value());
        snap = ps.snapshot();
        // Pinset + arena both go out of scope here.
    }
    // Accessing snap here is a UAF. ASan should catch it.
    (void)(*snap)[0].subject_dn.size();  // intentional UAF
}

}  // namespace
