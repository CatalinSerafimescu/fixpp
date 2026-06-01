// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/tls/test_pin_view_lifetime_under_rotation.cpp
// T015 — [2g §9 seam #16] pin_view lifetime under concurrent rotation.
//
// FR-008 invariant: pin_view::value dereference remains valid for as long as
// the caller holds the pin_view, regardless of concurrent removes. The
// shared_ptr<const pin_snapshot> inside pin_view keeps the snapshot alive.
//
// Thread A: holds pin_view from find(); repeatedly reads value->subject_dn.
// Thread B: add + remove loop (rotation), N rounds.
//
// Under ASan + TSan: no UAF, no data race.

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <fixpp/tls/certificate.hpp>
#include <fixpp/tls/pinset.hpp>
#include <string_view>
#include <thread>

namespace {

using fixpp::tls::Certificate;
using fixpp::tls::pin_fingerprint;
using fixpp::tls::pin_view;
using fixpp::tls::Pinset;

Certificate make_cert(pin_fingerprint const& fp, std::string_view dn) {
    Certificate c{};
    c.sha256_ = fp;
    c.subject_dn_ = dn;
    c.x509_version_ = 3;
    return c;
}

constexpr pin_fingerprint kFp = [] {
    pin_fingerprint f{};
    f[0] = std::byte{0xBB};
    return f;
}();

constexpr int kRounds = 200;

TEST(PinViewLifetime, DereferenceValidDuringConcurrentRotation) {
    Pinset ps;
    ASSERT_TRUE(ps.add(make_cert(kFp, "CN=lifetime-test")).has_value());

    // Capture a pin_view BEFORE starting the rotation thread.
    pin_view held_view = ps.find(kFp);
    ASSERT_TRUE(held_view.found()) << "initial find must succeed";

    const std::string_view expected_dn = "CN=lifetime-test";
    ASSERT_EQ(std::string_view{held_view.value->subject_dn}, expected_dn);

    std::atomic<bool> rotation_done{false};
    std::atomic<int> read_count{0};

    // Thread A: read held_view->subject_dn while rotation is happening.
    std::thread reader([&] {
        while (!rotation_done.load(std::memory_order_acquire)) {
            // Dereference value — must not UAF even as rotation replaces snapshot.
            EXPECT_EQ(std::string_view{held_view.value->subject_dn}, expected_dn);
            ++read_count;
        }
        // One final read after rotation completes.
        EXPECT_EQ(std::string_view{held_view.value->subject_dn}, expected_dn);
    });

    // Thread B (main): add/remove loop — replaces the snapshot N times.
    for (int i = 0; i < kRounds; ++i) {
        (void)ps.remove(kFp);
        (void)ps.add(make_cert(kFp, "CN=lifetime-test"));
    }

    rotation_done.store(true, std::memory_order_release);
    reader.join();

    EXPECT_GT(read_count.load(), 0) << "reader must have executed at least once";

    // held_view still valid after all rotation (shared_ptr keeps snapshot alive).
    EXPECT_EQ(std::string_view{held_view.value->subject_dn}, expected_dn)
        << "held pin_view must remain valid after rotation completes";
}

}  // namespace
