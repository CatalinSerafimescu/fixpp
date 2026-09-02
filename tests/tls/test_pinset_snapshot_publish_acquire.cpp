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
#include <chrono>
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
    std::atomic<int> stable_reads{0};      // stable pin always consistent
    std::atomic<int> consistent_reads{0};  // rotating pin found at least once, ever

    // Issue #287 (shape of #283): `consistent_reads > 0` / `stable_reads > 0`
    // read after the join, gated only by reader_started, was a START barrier —
    // entering the loop is not observing the write window, and kRounds is even so
    // the rotating pin is PRESENT when the writer stops and every late find
    // succeeds.
    //
    // The closure is that the reader observed the rotating pin ABSENT. Absent
    // exists only between a remove and the next add, and the writer is pinned to
    // stop in the PRESENT state (see the re-add after the witness loop), so no
    // read taken after the window can see it. An absent observation is therefore
    // necessarily in-window.
    //
    // A weaker form was tried first and REJECTED by mutation: "the reader saw the
    // state change between two of its own consecutive reads". That proves the two
    // reads BRACKET a mutation, not that either landed inside the window — a
    // reader can read the pre-seeded pin, be descheduled through the whole burst,
    // and read again afterwards. With the witness loop deleted it stayed GREEN
    // 40/40 under starvation; the absent form REDs.
    std::atomic<bool> observed_absent{false};

    std::thread reader([&] {
        while (!writer_done.load(std::memory_order_acquire)) {
            // Witness 1: find() on the rotating pin.
            auto v = ps.find(kFpAdded);
            const bool now_found = v.found();
            if (now_found) {
                // Any found entry MUST have the exact DN the writer provided.
                // A torn write would leave subject_dn in an indeterminate state.
                EXPECT_EQ(v.value->subject_dn, kExpectedDn)
                    << "find() returned a pin_view with unexpected subject_dn — "
                       "possible torn publish/acquire";
                // Dereference SAN vector too (exercises the PMR pointer fields).
                (void)v.value->san_dns.size();
                ++consistent_reads;
            }
            if (!now_found) {
                observed_absent.store(true, std::memory_order_release);
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

    // Keep rotating until the reader has actually witnessed the pin absent.
    // Throttling (not yield()) is required: on a saturated core the writer
    // would otherwise starve the very reader it is waiting for. Bounded by a
    // wall-clock deadline, because what is being waited on is a thread being
    // SCHEDULED, and the throttle also bounds total allocation over the
    // deadline (see the loop body below).
    const auto witness_until = std::chrono::steady_clock::now() + std::chrono::seconds{10};
    while (!observed_absent.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < witness_until) {
        if (added) {
            (void)ps.remove(kFpAdded);
            added = false;
        } else {
            // Non-fatal + break, NOT ASSERT_*: a fatal assertion here returns from
            // the test body with `reader` still joinable, which is std::terminate.
            // (The kRounds loop above has that shape already; left as-is —
            // pre-existing, and out of this change's scope.)
            if (!ps.add(make_cert(kFpAdded, kExpectedDn)).has_value()) {
                ADD_FAILURE() << "add() must succeed during the witness loop";
                break;
            }
            added = true;
        }
        // Throttle, do NOT spin. An ITERATION ceiling cannot bound this loop:
        // it is waiting for a descheduled reader, so it must be able to span the
        // whole deadline, and 2000 spun iterations exhaust in milliseconds — a
        // count-bounded net was proven not to rescue a 400 ms-delayed reader.
        // Bounding the RATE instead bounds total allocation over the deadline
        // while leaving the deadline the operative bound.
        //
        // ⚠️ Do not restate that rate as an iteration count: a sub-granularity
        // sleep sleeps for the GRANULARITY, so the delivered period is a
        // platform property, not the argument below. The allocation bound
        // survives that — a coarser period allocates less (issue #327).
        std::this_thread::sleep_for(std::chrono::microseconds{200});
    }

    // Leave the rotating pin PRESENT. Load-bearing, not tidiness: if the writer
    // could stop with it absent, a find taken long after the window would see
    // absent and satisfy the assertion having overlapped nothing.
    if (!added) {
        if (!ps.add(make_cert(kFpAdded, kExpectedDn)).has_value()) {
            ADD_FAILURE() << "add() must succeed when restoring the terminal state";
        }
    }

    // stable_reads is a COUNT, so it keeps growing after the window closes and
    // must be sampled HERE to say anything about in-window reads. The absent
    // witness is the opposite: it cannot be produced once the window closes, and
    // sampling it early would risk a FALSE RED (a reader can complete the read
    // that sets it after the sample). So: count early, witness late.
    //
    // consistent_reads is sampled here too, alongside stable_reads, for
    // consistency with its neighbour — but unlike stable_reads it is NOT an
    // in-window count. It asserts a purely functional fact ("the reader's
    // find(kFpAdded) succeeded at least once, ever"), which is equally true
    // read before or after the join; sampling late (post-join) would be just
    // as valid.
    const int witnessed_stable = stable_reads.load(std::memory_order_acquire);
    const int witnessed_consistent = consistent_reads.load(std::memory_order_acquire);

    writer_done.store(true, std::memory_order_release);
    reader.join();

    // The reader must have raced the writer, not merely run before or after it.
    EXPECT_TRUE(observed_absent.load(std::memory_order_acquire))
        << "the reader never observed the rotating pin ABSENT. It is present before "
           "the window opens and present after it closes, so an absent read is the "
           "proof that a find landed between a remove and the next add — without one, "
           "the publish/acquire edge is not exercised";
    EXPECT_GT(witnessed_stable, 0)
        << "reader thread did not observe the stable pin — "
           "snapshot_ invariant not exercised";
    EXPECT_GT(witnessed_consistent, 0)
        << "reader thread never observed the rotating pin FOUND — "
           "find(kFpAdded) succeeding is not implied by observed_absent, which is "
           "satisfiable even if the rotating pin were never findable at all";

    // Nothing mutates the pinset after the re-add above, so this is equally
    // valid post-join and avoids the fatal-assert-with-joinable-thread hazard
    // documented at the ADD_FAILURE sites above.
    EXPECT_TRUE(ps.find(kFpAdded).found())
        << "rotating pin must be findable in its terminal (re-added) state";
}

}  // namespace
