// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/capi/error_block_test.cpp — 050 Feature B (CA-006), T022/T023.
// Per-row error oracle for the session/send reachable arms + the consumer-minor
// forward-compat downgrade (SC-004), reframed for the L-050-4 descope.
//
// L-050-4 descope: Feature B publishes NO FIXPP_ERR_SESSION_* block ([2i §4.3]
// has none, and editing the signed-off [2i] is forbidden — CHK030). So the
// reachable session/app arms (77/119/129/130/131) STAY at FIXPP_ERR_UNKNOWN.
// There is therefore NO newly-published arm to flip off UNKNOWN, hence no
// "flip-off-UNKNOWN → RED" oracle (contrast Feature A's error_surface_test,
// which owns the full 116-arm correctness oracle — NOT duplicated here).
//
// This TU is the FOCUSED send-path oracle: it pins (a) the deferred arms at
// UNKNOWN [by design, not a gap], (b) each EXISTING-published reachable arm at
// its existing code (not silently re-pointed by Feature B), and (c) the
// send-path return composition translate_for_consumer(translate(e), minor)
// that fixpp_session_send actually applies (T026), exercising the live
// downgrade machinery the engine wires via the recorded consumer_minor (D-9,
// L-049-1).
//
// SC-004 minor-3 NOTE: the spec's "downgrade a minor-3 session code" witness
// presumes Feature B published session codes at C-ABI minor 3. The L-050-4
// descope deferred that block, so NO minor-3 code exists; translate_for_consumer
// is uniform introducing_minor==2 (error.cpp:230). The live downgrade machinery
// is witnessed here at the real minor-2 boundary (the engine records
// consumer_minor and applies it on every fallible return); the minor-3-specific
// arm is deferred WITH the block (L-050-4) — flagged for Gate A as an SC-004
// reframe parallel to the SC-005 deferral.

#include <gtest/gtest.h>

#include <cstdint>

#include "fix/c_api/error.h"
#include "fixpp/core/error.hpp"

// Engine-internal coalescing under test (declared, not exported).
namespace fixpp_capi::detail {
fixpp_error_t translate(fixpp::core::error e) noexcept;
fixpp_error_t translate_for_consumer(fixpp_error_t code, std::uint16_t consumer_minor) noexcept;
}  // namespace fixpp_capi::detail

using fixpp::core::error;
using fixpp_capi::detail::translate;
using fixpp_capi::detail::translate_for_consumer;

// ── T022: deferred session/app arms STAY at UNKNOWN (L-050-4, by design) ──────
//
// These are the 5 send/register-path-reachable session/app arms (data-model
// E-4): session_invalid_state_for_send (77), session_invalid_argument (119),
// app_do_not_send (129), app_callback_threw (130), app_payload_malformed (131).
// They are reachable from Engine::send / register_session but map to UNKNOWN
// because the published block is deferred. A future Feature-B amendment that
// publishes FIXPP_ERR_SESSION_* will flip these — until then, UNKNOWN is the
// CONTRACT, and this test guards against an accidental premature re-point.
TEST(CapiErrorBlock, DeferredSessionAppArmsStayUnknown) {
    EXPECT_EQ(translate(error::session_invalid_state_for_send), FIXPP_ERR_UNKNOWN);  // 77
    EXPECT_EQ(translate(error::session_invalid_argument), FIXPP_ERR_UNKNOWN);        // 119
    EXPECT_EQ(translate(error::app_do_not_send), FIXPP_ERR_UNKNOWN);                 // 129
    EXPECT_EQ(translate(error::app_callback_threw), FIXPP_ERR_UNKNOWN);              // 130
    EXPECT_EQ(translate(error::app_payload_malformed), FIXPP_ERR_UNKNOWN);           // 131
}

// ── T022: existing-published reachable arms keep their existing codes ─────────
//
// The send/lifecycle path can also surface these EXISTING-published arms
// (data-model E-4). Feature B must NOT silently re-point them. NO store_io_failure
// row — it is I-07-swallowed on the send path (L-050-3), never a send return.
TEST(CapiErrorBlock, ExistingPublishedReachableArms) {
    // Oversized opaque payload → wire_frame_too_large (30).
    EXPECT_EQ(translate(error::wire_frame_too_large), FIXPP_ERR_WIRE_LIMIT_EXCEEDED);
    // Outbound counter at seqnum_max → store_seqnum_overflow (60).
    EXPECT_EQ(translate(error::store_seqnum_overflow), FIXPP_ERR_STORE_RUNTIME);
    // send-vs-close TOCTOU (async_lock fail) → session_already_closed (52).
    EXPECT_EQ(translate(error::session_already_closed), FIXPP_ERR_THREAD_SESSION_LIFECYCLE);
    // register_session validate gate → invalid_session_config (53).
    EXPECT_EQ(translate(error::invalid_session_config), FIXPP_ERR_THREAD_CONFIG);
    // engine_start null clock → clock_not_set (54).
    EXPECT_EQ(translate(error::clock_not_set), FIXPP_ERR_THREAD_CONFIG);
    // Uniform cancellation (FR-010) — every cancel outcome coalesces to CANCELLED.
    EXPECT_EQ(translate(error::dispatch_aborted), FIXPP_ERR_CANCELLED);
    EXPECT_EQ(translate(error::store_cancelled), FIXPP_ERR_CANCELLED);
    EXPECT_EQ(translate(error::clock_sleeps_cancelled), FIXPP_ERR_CANCELLED);
}

// ── T023 / SC-004: live consumer-minor downgrade on the send-path composition ─
//
// fixpp_session_send returns translate_for_consumer(translate(e), consumer_minor)
// (T026). This witnesses that composition end-to-end: a consumer that declares a
// minor BELOW a code's introducing_minor sees UNKNOWN (forward-compat downgrade,
// L-049-1); a consumer at/above sees the real code. All current codes have
// introducing_minor==2, so the live boundary is minor<2 → downgrade.
TEST(CapiErrorBlock, Sc004DowngradeMachineryLiveOnSendComposition) {
    // A reachable published send-path code: store_seqnum_overflow → STORE_RUNTIME.
    const fixpp_error_t coalesced = translate(error::store_seqnum_overflow);
    ASSERT_EQ(coalesced, FIXPP_ERR_STORE_RUNTIME);

    // consumer below introducing_minor (0, 1) → downgraded to UNKNOWN.
    EXPECT_EQ(translate_for_consumer(coalesced, 0), FIXPP_ERR_UNKNOWN);
    EXPECT_EQ(translate_for_consumer(coalesced, 1), FIXPP_ERR_UNKNOWN);

    // consumer at/above introducing_minor (2, 3) → real code preserved.
    EXPECT_EQ(translate_for_consumer(coalesced, 2), FIXPP_ERR_STORE_RUNTIME);
    EXPECT_EQ(translate_for_consumer(coalesced, 3), FIXPP_ERR_STORE_RUNTIME);

    // A deferred arm composes too: UNKNOWN downgrades to UNKNOWN at any minor
    // (idempotent), and is preserved (still UNKNOWN) at/above — so a consumer
    // can never mistake a deferred session arm for a published code.
    const fixpp_error_t deferred = translate(error::session_invalid_argument);
    ASSERT_EQ(deferred, FIXPP_ERR_UNKNOWN);
    EXPECT_EQ(translate_for_consumer(deferred, 0), FIXPP_ERR_UNKNOWN);
    EXPECT_EQ(translate_for_consumer(deferred, 3), FIXPP_ERR_UNKNOWN);
}
