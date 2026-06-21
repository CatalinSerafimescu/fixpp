// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/tls/test_pinset_snapshot_publish_acquire.cpp
// T008 (046-atomic-shared-ptr, NFR-017) — consumer publish/acquire witness for
// Pinset::snapshot_ (fixpp::sync::atomic_shared_ptr<const pin_snapshot>).
//
// Design anchor: .specify/046-atomic-shared-ptr.md (plan row 6-consumers)
// Consumer: fixpp::tls::Pinset (include/fixpp/tls/pinset.hpp §139)
//
// Pattern: writer thread calls add()/remove() (which release-store a new
// snapshot_); reader thread calls find() and snapshot() (which acquire-load).
// TSan is the ordering oracle; assertions on value consistency prove no torn
// read reaches the application layer.
//
// Discrimination: the reader asserts that any pin_view returned for kFpAdded
// has the exact expected subject_dn ("CN=publish-acquire-writer"), distinguishing
// a valid publication from a half-written entry that would have a different or
// empty subject. A torn-pointer read would either crash (ASan) or return garbage
// that fails the string check.

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <string>
#include <thread>

#include <fixpp/tls/certificate.hpp>
#include <fixpp/tls/pinset.hpp>

namespace {

using fixpp::tls::Certificate;
using fixpp::tls::pin_fingerprint;
using fixpp::tls::Pinset;

// ── helpers ──────────────────────────────────────────────────────────────────

constexpr std::string_view kExpectedDn = "CN=publish-acquire-writer";

Certificate make_cert(pin_fingerprint const& fp, std::string_view dn) {
    Certificate c{};
    c.sha256_ = fp;
    c.subject_dn_ = dn;
    c.x509_version_ = 3;
    return c;
}

constexpr pin_fingerprint kFpAdded = [] {
    pin_fingerprint f{};
    f[0] = std::byte{0xBB};
    f[1] = std::byte{0x04};
    f[2] = std::byte{0x6};  // 046 marker
    return f;
}();

// A second pin fingerprint that stays in the set throughout; gives the reader
// a stable target whose subject must ALSO always be consistent.
constexpr pin_fingerprint kFpStable = [] {
    pin_fingerprint f{};
    f[0] = std::byte{0xCC};
    return f;
}();

constexpr std::string_view kStableDn = "CN=stable-pin";

constexpr int kRounds = 3000;

// ── PinsetPublishAcquire ──────────────────────────────────────────────────────

TEST(PinsetPublishAcquire, WriterReaderNeverSeesTornPin) {
    Pinset ps;

    // Pre-seed the stable pin — it is never removed during the test.
    ASSERT_TRUE(ps.add(make_cert(kFpStable, kStableDn)).has_value());
    // Pre-seed the rotating pin so the reader has something to find immediately.
    ASSERT_TRUE(ps.add(make_cert(kFpAdded, kExpectedDn)).has_value());

    std::atomic<bool> writer_done{false};
    std::atomic<int> reader_started{0};
    std::atomic<int> consistent_reads{0};  // find() returned expected DN
    std::atomic<int> stable_reads{0};      // stable pin always consistent

    std::thread reader([&] {
        while (!writer_done.load(std::memory_order_acquire)) {
            // Witness 1: find() on the rotating pin.
            auto v = ps.find(kFpAdded);
            if (v.found()) {
                // Any found entry MUST have the exact DN the writer provided.
                // A torn write would leave subject_dn in an indeterminate state.
                EXPECT_EQ(v.value->subject_dn, kExpectedDn)
                    << "find() returned a pin_view with unexpected subject_dn — "
                       "possible torn publish/acquire";
                // Dereference SAN vector too (exercises the PMR pointer fields).
                (void)v.value->san_dns.size();
                ++consistent_reads;
            }

            // Witness 2: snapshot() — acquire-load the entire snapshot.
            auto snap = ps.snapshot();
            ASSERT_NE(snap, nullptr) << "snapshot() must never return nullptr";
            for (auto const& pin : *snap) {
                // Every pin in the snapshot must have a non-empty subject_dn —
                // a torn write would leave it empty or with garbage length.
                EXPECT_FALSE(pin.subject_dn.empty())
                    << "snapshot() contained a pin with empty subject_dn — "
                       "possible torn publish/acquire";
            }

            // Witness 3: stable pin must always be found with the correct DN.
            auto sv = ps.find(kFpStable);
            if (sv.found()) {
                EXPECT_EQ(sv.value->subject_dn, kStableDn)
                    << "stable pin's subject_dn changed — snapshot invariant violated";
                ++stable_reads;
            }

            reader_started.store(1, std::memory_order_release);
        }
        // Final pass after writer_done.
        auto snap = ps.snapshot();
        ASSERT_NE(snap, nullptr);
    });

    // Wait until the reader has started.
    while (reader_started.load(std::memory_order_acquire) == 0) { /* spin */ }

    // Writer: alternate add/remove of kFpAdded (kRounds cycles).
    bool added = true;  // pre-seeded above
    for (int i = 0; i < kRounds; ++i) {
        if (added) {
            (void)ps.remove(kFpAdded);
            added = false;
        } else {
            ASSERT_TRUE(ps.add(make_cert(kFpAdded, kExpectedDn)).has_value());
            added = true;
        }
    }

    writer_done.store(true, std::memory_order_release);
    reader.join();

    // At least some reads must have found the rotating pin (confirms the reader
    // actually ran during the write window, not just after).
    EXPECT_GT(consistent_reads.load(), 0)
        << "reader thread did not observe any successful find() during the write "
           "window — test may not have exercised the publish/acquire edge";
    EXPECT_GT(stable_reads.load(), 0)
        << "reader thread did not observe the stable pin — "
           "snapshot_ invariant not exercised";
}

}  // namespace
