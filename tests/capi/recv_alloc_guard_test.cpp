// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/capi/recv_alloc_guard_test.cpp — 050 Feature B (CA-007), T034a / FR-013.
//
// §VIII.5 zero-global-alloc gate for the on-strand receive trampoline. The
// production CapiApplication::fromApp hot path wraps the borrowed MessageView in a
// STACK fixpp_msg, looks the session slot up in the (pre-populated) callback map,
// and invokes the registered C callback — it MUST NOT touch the global heap
// (D-6 / [const §VIII.5]). This drives that exact production path SYNCHRONOUSLY
// (no engine / no worker thread — the alloc-guard idiom requires the measured
// allocations to occur on the marker thread; cf. tests/alloc_guard/
// test_session_alloc_guard.cpp's run_sync pattern) and asserts ZERO global heap
// allocation between the mallocnesia guard markers.
//
// Binding gate = the mallocnesia LD_PRELOAD ctest entry (capi_recv_alloc_guard_
// mallocnesia); a tracking-PMR/counting-resource alone would FALSE-PASS without
// global interception (memory feedback_tracking_pmr_resource_false_pass). No
// TU-local operator-new override is used, so the test is sanitizer-safe (memory
// feedback_operator_new_witness_breaks_sanitizers) and also runs as a plain test
// (markers no-op without the preload).
//
// Anchors: spec.md FR-013; tasks.md T034a; research D-6; contracts/send-and-receive.md.

#include <gtest/gtest.h>

#include <cstdint>

#include "capi_internal.hpp"  // CapiApplication, fixpp_msg, SessionSlot, fixpp_msg_t

#include "support/alloc_guard_markers.hpp"

using fixpp::session::SessionId;
using fixpp::wire::MessageView;
using fixpp::wire::access_mode;

namespace {

// A non-capturing callback (converts to fixpp_recv_cb) that only bumps a counter
// through userdata — it does NOT dereference the inbound handle, so the measured
// path is purely the trampoline's framing + dispatch (the user callback's own
// allocations, if any, are the user's concern, not the §VIII.5 trampoline gate).
void counting_cb(const fixpp_msg_t* /*inbound*/, void* ud) {
    ++*static_cast<std::uint64_t*>(ud);
}

}  // namespace

// ── T034a (FR-013): the on-strand trampoline allocates nothing on the heap ────
TEST(CapiRecvAllocGuard, TrampolineFromAppNoGlobalHeapAlloc) {
    fixpp_capi::detail::CapiApplication app;

    // Pre-populate the callback map OUTSIDE the guard window (slot_for inserts).
    SessionId id{};
    std::uint64_t fired = 0;
    fixpp_capi::detail::SessionSlot& slot = app.slot_for(id);
    slot.cb = &counting_cb;
    slot.userdata = &fired;

    // The trampoline only takes the view's address; a default view is sufficient
    // and keeps the gate independent of the parser (which is exercised elsewhere).
    MessageView<access_mode::Index> view{};

    // Warm-up: prime any first-call lazy-init outside the measured window.
    constexpr int kWarmup = 8;
    for (int i = 0; i < kWarmup; ++i) {
        (void)app.fromApp(view, id);
    }

    constexpr int kCorpus = 10'000;
    if (alloc_guard_start) alloc_guard_start();
    for (int i = 0; i < kCorpus; ++i) {
        (void)app.fromApp(view, id);
    }
    if (alloc_guard_end) alloc_guard_end();  // exits(1) under mallocnesia if any alloc fired

    EXPECT_EQ(fired, static_cast<std::uint64_t>(kWarmup + kCorpus))
        << "the trampoline did not dispatch the C callback on every fromApp";
}
