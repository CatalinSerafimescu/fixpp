// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/capi/toapp_alloc_guard_test.cpp — 051 Feature C (CA-US6), T020 / SC-003.
//
// §VIII.5 zero-global-alloc gate for the on-strand toApp trampoline
// (CapiApplication::toApp). The production path wraps the borrowed MessageView
// in a STACK fixpp_msg (framed read-only view, D-8), looks the session slot up in
// the (pre-populated) callback map, invokes the registered C send_cb — it MUST
// NOT touch the global heap ([const §VIII.5], data-model E-6).
//
// This drives that exact production path SYNCHRONOUSLY (no engine / no worker
// thread — the alloc-guard idiom requires the measured allocations to occur on the
// marker thread; cf. recv_alloc_guard_test.cpp / test_session_alloc_guard.cpp's
// run_sync pattern) and asserts ZERO global heap allocation between the
// mallocnesia guard markers.
//
// Binding gate = the mallocnesia LD_PRELOAD ctest entry
// (capi_toapp_alloc_guard_mallocnesia); a tracking-PMR/counting-resource alone
// would FALSE-PASS without global interception
// ([[feedback_tracking_pmr_resource_false_pass]]). No TU-local operator-new
// override is used, so the test is sanitizer-safe
// ([[feedback_operator_new_witness_breaks_sanitizers]]) and also runs as a plain
// test (markers no-op without the preload).
//
// Anchors: spec.md FR-013; tasks.md T020; research D-8; contracts/toapp-callback.md.

#include <gtest/gtest.h>

#include <cstdint>

#include "capi_internal.hpp"  // CapiApplication, fixpp_msg, SessionSlot, fixpp_send_cb

#include "support/alloc_guard_markers.hpp"

using fixpp::session::SessionId;
using fixpp::wire::MessageView;
using fixpp::wire::access_mode;

namespace {

// A non-capturing send callback (converts to fixpp_send_cb) that bumps a counter
// through userdata and returns FIXPP_TOAPP_SEND — it does NOT dereference the
// framed handle beyond what the trampoline itself does, so the measured path is
// purely the trampoline's framing + dispatch (user-callback allocations, if any,
// are the user's concern, not the §VIII.5 trampoline gate).
fixpp_toapp_verdict counting_send_cb(const fixpp_msg_t* /*outbound*/, void* ud) {
    ++*static_cast<std::uint64_t*>(ud);
    return FIXPP_TOAPP_SEND;
}

}  // namespace

// ── T020 (SC-003): the on-strand toApp trampoline allocates nothing on the heap ─
TEST(CapiToappAllocGuard, TrampolineToAppNoGlobalHeapAlloc) {
    fixpp_capi::detail::CapiApplication app;

    // Pre-populate the callback map OUTSIDE the guard window (slot_for inserts).
    SessionId id{};
    std::uint64_t fired = 0;
    fixpp_capi::detail::SessionSlot& slot = app.slot_for(id);
    slot.send_cb = &counting_send_cb;
    slot.send_userdata = &fired;

    // The trampoline only takes the view's address; a default view is sufficient
    // and keeps the gate independent of the parser (which is exercised elsewhere).
    MessageView<access_mode::Index> view{};

    // Warm-up: prime any first-call lazy-init outside the measured window.
    constexpr int kWarmup = 8;
    for (int i = 0; i < kWarmup; ++i) {
        (void)app.toApp(view, id);
    }

    constexpr int kCorpus = 10'000;
    if (alloc_guard_start) alloc_guard_start();
    for (int i = 0; i < kCorpus; ++i) {
        (void)app.toApp(view, id);
    }
    if (alloc_guard_end) alloc_guard_end();  // exits(1) under mallocnesia if any alloc fired

    EXPECT_EQ(fired, static_cast<std::uint64_t>(kWarmup + kCorpus))
        << "the trampoline did not dispatch the send callback on every toApp";
}
