// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/tls/test_pinset_add_then_remove_stress.cpp
// T013 — [2g §9 seam #2] TSan stress: concurrent add/remove + find.
//
// Thread A: alternating add/remove loop (N rounds).
// Thread B: continuous find loop reading concurrently.
//
// Under TSan: no data race must be reported.
// Under ASan: no use-after-free on the pin_view returned during rotation.
//
// The pin_view lifetime invariant (FR-008): find() returns a pin_view that
// holds the snapshot shared_ptr; the view's value pointer is valid for as long
// as the caller holds the pin_view — regardless of concurrent removes.

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <fixpp/tls/certificate.hpp>
#include <fixpp/tls/pinset.hpp>
#include <thread>

namespace {

using fixpp::tls::Certificate;
using fixpp::tls::pin_fingerprint;
using fixpp::tls::Pinset;

Certificate make_cert(pin_fingerprint const& fp, std::string_view dn = "CN=stress") {
    Certificate c{};
    c.sha256_ = fp;
    c.subject_dn_ = dn;
    c.x509_version_ = 3;
    return c;
}

constexpr pin_fingerprint kFp = [] {
    pin_fingerprint f{};
    f[0] = std::byte{0xAA};
    return f;
}();

constexpr int kRounds = 2000;

// ── ConcurrentAddRemoveAndFind ────────────────────────────────────────────────
TEST(PinsetStress, ConcurrentAddRemoveAndFind) {
    Pinset ps;

    // Pre-add so finder has something to find during the first iteration.
    ASSERT_TRUE(ps.add(make_cert(kFp)).has_value());

    std::atomic<bool> writer_done{false};
    // finder_ready: the finder sets this to 1 after its first loop iteration,
    // ensuring the main thread waits until the finder has actually started.
    std::atomic<int> finder_started{0};
    std::atomic<int> find_count{0};

    // Thread B: finder — runs concurrently with writer.
    std::thread finder([&] {
        while (!writer_done.load(std::memory_order_acquire)) {
            auto v = ps.find(kFp);
            if (v.found()) {
                // Dereference value: must be safe (snapshot keeps it alive).
                // Touch subject_dn to exercise the reference under ASan.
                (void)v.value->subject_dn.size();
                ++find_count;
            }
            // Also exercise snapshot() path.
            auto snap = ps.snapshot();
            (void)snap->size();
            // Signal that the finder thread is running.
            finder_started.store(1, std::memory_order_release);
        }
        // One final pass after writer_done.
        auto snap = ps.snapshot();
        (void)snap->size();
    });

    // Wait until the finder thread has started at least one iteration.
    while (finder_started.load(std::memory_order_acquire) == 0) {
        // Spin — the finder will signal quickly since there is no contention yet.
    }

    // Thread A (main): add/remove loop.
    bool added = true;  // we pre-added above
    for (int i = 0; i < kRounds; ++i) {
        if (added) {
            // Remove current pin.
            (void)ps.remove(kFp);
            added = false;
        } else {
            // Re-add.
            (void)ps.add(make_cert(kFp));
            added = true;
        }
    }

    writer_done.store(true, std::memory_order_release);
    finder.join();

    // The test is a data-race oracle (TSan catches violations).
    // Additionally verify find_count > 0 to confirm finder actually ran.
    EXPECT_GT(find_count.load(), 0)
        << "finder thread must have completed at least one successful find";
}

}  // namespace
