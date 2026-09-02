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
#include <chrono>
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

    // Issue #287 (shape of #283): the previous witness was `find_count > 0`
    // read after the join, gated only by finder_started — a START barrier.
    // Entering the loop is not observing the write window, and a post-join total
    // counts finds that overlapped nothing: kRounds is even, so the pin is
    // PRESENT when the writer stops and every late find succeeds.
    //
    // The closure is that the finder observed the pin ABSENT. Absent exists only
    // between a remove and the next add, and the writer is pinned to stop in the
    // PRESENT state (see the re-add after the witness loop), so no read taken
    // after the window can see it. An absent observation is therefore necessarily
    // in-window.
    //
    // A weaker form was tried first and REJECTED by mutation: "the finder saw the
    // state change between two of its own consecutive reads". That proves the two
    // reads BRACKET a mutation, not that either landed inside the window — a
    // finder can read the pre-seeded pin, be descheduled through the whole burst,
    // and read again afterwards. With the witness loop deleted it stayed GREEN
    // 40/40 under starvation; the absent form REDs. This is #286's own lesson
    // (a fix for a vacuous witness can itself be vacuous), re-earned here.
    std::atomic<bool> observed_absent{false};

    // Thread B: finder — runs concurrently with writer.
    std::thread finder([&] {
        while (!writer_done.load(std::memory_order_acquire)) {
            auto v = ps.find(kFp);
            const bool now_found = v.found();
            if (now_found) {
                // Dereference value: must be safe (snapshot keeps it alive).
                // Touch subject_dn to exercise the reference under ASan.
                (void)v.value->subject_dn.size();
            } else {
                observed_absent.store(true, std::memory_order_release);
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

    // Keep mutating until the finder has actually witnessed the pin absent.
    // Throttling (not yield()) is required: on a saturated core the writer
    // would otherwise starve the very finder it is waiting for. Bounded by a
    // wall-clock deadline, because what is being waited on is a thread being
    // SCHEDULED, and the throttle also bounds total allocation over the
    // deadline (see the loop body below).
    const auto witness_until = std::chrono::steady_clock::now() + std::chrono::seconds{10};
    while (!observed_absent.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < witness_until) {
        if (added) {
            (void)ps.remove(kFp);
            added = false;
        } else {
            (void)ps.add(make_cert(kFp));
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

    // Leave the pin PRESENT. This is what makes the witness sound, not tidiness:
    // if the writer could stop with the pin absent, a find taken long after the
    // window would see absent and satisfy the assertion having overlapped
    // nothing. The loop exits at an arbitrary point in the alternation, so the
    // terminal state has to be restored explicitly.
    if (!added) {
        (void)ps.add(make_cert(kFp));
        added = true;
    }

    writer_done.store(true, std::memory_order_release);
    finder.join();

    // Checked after join(), not before: a fatal assertion here with `finder`
    // still joinable would be std::terminate (see the sibling publish/acquire
    // file's documentation of this hazard). Nothing mutates the pinset between
    // the re-add above and here, so post-join is semantically identical.
    ASSERT_TRUE(ps.find(kFp).found()) << "the pin must be PRESENT when the write window closes";

    // The test is a data-race oracle (TSan catches violations). The witness below
    // is what makes that oracle non-vacuous: it proves the finder read
    // concurrently with the mutations, not merely before or after them.
    EXPECT_TRUE(observed_absent.load(std::memory_order_acquire))
        << "the finder never observed the pin ABSENT. The pin is present before the "
           "window opens and present after it closes, so an absent read is the proof "
           "that a find landed between a remove and the next add — without one, the "
           "concurrent add/remove/find window was not exercised";
}

}  // namespace
