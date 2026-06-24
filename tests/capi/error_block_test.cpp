// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/capi/error_block_test.cpp — 051 Feature C (CA-008/010 + [2i §4.3]
// amendment), T004 (foundational error.cpp unit witness) + T022 (US5 live
// end-to-end witness, appended later).
//
// 051 flips the L-050-4 descope: the [2i §4.3] amendment publishes a dedicated
// Phase-4 session/app + message-construction block [1400,1499] and re-points
// the FIVE reachable C++ core::error arms off FIXPP_ERR_UNKNOWN:
//   session_invalid_state_for_send (77)  -> FIXPP_ERR_SESSION_INVALID_STATE   (1401)
//   session_invalid_argument       (119) -> FIXPP_ERR_SESSION_INVALID_ARGUMENT(1400)
//   app_do_not_send                (129) -> FIXPP_ERR_APP_DO_NOT_SEND         (1402)
//   app_callback_threw             (130) -> FIXPP_ERR_APP_CALLBACK_THREW      (1403)
//   app_payload_malformed          (131) -> FIXPP_ERR_APP_PAYLOAD_MALFORMED   (1404)
// plus FIXPP_ERR_MSG_FRAMING_TAG_FORBIDDEN (1405), a pure C-ABI construction
// reject with NO C++ ordinal (never produced by translate()).
//
// This TU is the FOUNDATIONAL error.cpp-unit oracle (T004): it pins (a) the
// translate() re-point of the 5 arms, (b) a non-empty fixpp_strerror for all 6
// new codes, and (c) the PER-CODE introducing-minor downgrade — a new minor-4
// code downgrades at consumer_minor=3 while an EXISTING minor-2 code survives
// (the per-code-table witness a scalar bump-to-4 would have broken, FR-017).
// The live pure-C end-to-end 5-arm + toApp witness (SC-004) is appended in T022.
//
// RED->GREEN note (TDD, T004): against the pre-051 error.cpp the 5 arms returned
// UNKNOWN and translate_for_consumer was a uniform scalar minor-2 — every
// assertion below would FAIL. The 051 co-update (error.h block + error.cpp
// translate re-point + per-code introducing_minor) greens it.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>

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

// ── T004: the five reachable session/app arms now map to the published block ──
//
// 051 [2i §4.3] amendment: these arms are no longer UNKNOWN. This guards against
// a regression that re-points them back, AND against a wrong numeric mapping.
TEST(CapiErrorBlock, SessionAppArmsPublished) {
    EXPECT_EQ(translate(error::session_invalid_state_for_send), FIXPP_ERR_SESSION_INVALID_STATE);     // 77 -> 1401
    EXPECT_EQ(translate(error::session_invalid_argument), FIXPP_ERR_SESSION_INVALID_ARGUMENT);        // 119 -> 1400
    EXPECT_EQ(translate(error::app_do_not_send), FIXPP_ERR_APP_DO_NOT_SEND);                          // 129 -> 1402
    EXPECT_EQ(translate(error::app_callback_threw), FIXPP_ERR_APP_CALLBACK_THREW);                    // 130 -> 1403
    EXPECT_EQ(translate(error::app_payload_malformed), FIXPP_ERR_APP_PAYLOAD_MALFORMED);              // 131 -> 1404
}

// ── T004: the OTHER 15 session arms stay UNKNOWN (no published code, by design)
//
// Only the two REACHABLE send-path arms were published; the rest of the session
// enum keeps mapping to UNKNOWN. Guards against an over-broad re-point.
TEST(CapiErrorBlock, NonReachableSessionArmsStayUnknown) {
    EXPECT_EQ(translate(error::session_invalid_logon), FIXPP_ERR_UNKNOWN);
    EXPECT_EQ(translate(error::session_compid_mismatch), FIXPP_ERR_UNKNOWN);
    EXPECT_EQ(translate(error::session_admin_not_supported), FIXPP_ERR_UNKNOWN);
    EXPECT_EQ(translate(error::session_unknown_acceptor_session), FIXPP_ERR_UNKNOWN);
}

// ── T004: existing-published reachable arms keep their existing codes ─────────
//
// The send/lifecycle path can also surface these EXISTING-published arms; 051
// must NOT silently re-point them.
TEST(CapiErrorBlock, ExistingPublishedReachableArms) {
    EXPECT_EQ(translate(error::wire_frame_too_large), FIXPP_ERR_WIRE_LIMIT_EXCEEDED);
    EXPECT_EQ(translate(error::store_seqnum_overflow), FIXPP_ERR_STORE_RUNTIME);
    EXPECT_EQ(translate(error::session_already_closed), FIXPP_ERR_THREAD_SESSION_LIFECYCLE);
    EXPECT_EQ(translate(error::invalid_session_config), FIXPP_ERR_THREAD_CONFIG);
    EXPECT_EQ(translate(error::clock_not_set), FIXPP_ERR_THREAD_CONFIG);
    // Uniform cancellation (FR-010) — every cancel outcome coalesces to CANCELLED.
    EXPECT_EQ(translate(error::dispatch_aborted), FIXPP_ERR_CANCELLED);
    EXPECT_EQ(translate(error::store_cancelled), FIXPP_ERR_CANCELLED);
    EXPECT_EQ(translate(error::clock_sleeps_cancelled), FIXPP_ERR_CANCELLED);
}

// ── T004: fixpp_strerror is non-empty + static for all six new codes ──────────
TEST(CapiErrorBlock, StrerrorNonEmptyForNewCodes) {
    const fixpp_error_t codes[] = {
        FIXPP_ERR_SESSION_INVALID_ARGUMENT, FIXPP_ERR_SESSION_INVALID_STATE,
        FIXPP_ERR_APP_DO_NOT_SEND, FIXPP_ERR_APP_CALLBACK_THREW,
        FIXPP_ERR_APP_PAYLOAD_MALFORMED, FIXPP_ERR_MSG_FRAMING_TAG_FORBIDDEN,
    };
    for (fixpp_error_t code : codes) {
        const char* s = fixpp_strerror(code);
        ASSERT_NE(s, nullptr);
        EXPECT_GT(std::strlen(s), 0u) << "code " << code << " has an empty strerror";
        // Must NOT fall through to the generic "unknown error".
        EXPECT_STRNE(s, "unknown error") << "code " << code << " missing a strerror entry";
        // Stable pointer (static storage): same address on a second call.
        EXPECT_EQ(s, fixpp_strerror(code));
    }
}

// ── T004 / FR-017: PER-CODE introducing-minor downgrade, witnessed BOTH ways ──
//
// The 051 amendment replaced the scalar kIntroducingMinor=2 with a per-code
// lookup (the six [1400,1499] codes are minor 4). A literal scalar-bump to 4
// would have silently downgraded every existing 0.2/0.3 code at consumer_minor=3.
// This is the discriminating witness that forbids that regression.
TEST(CapiErrorBlock, Sc004PerCodeMinorDowngradeBothWays) {
    // (a) a NEW minor-4 code downgrades for a sub-4 consumer, surfaces at >=4.
    const fixpp_error_t newCode = translate(error::session_invalid_argument);
    ASSERT_EQ(newCode, FIXPP_ERR_SESSION_INVALID_ARGUMENT);  // 1400
    EXPECT_EQ(translate_for_consumer(newCode, 3), FIXPP_ERR_UNKNOWN);                 // minor 3 < 4 -> downgrade
    EXPECT_EQ(translate_for_consumer(newCode, 4), FIXPP_ERR_SESSION_INVALID_ARGUMENT);// minor 4 -> real code

    // (b) an EXISTING minor-2 code SURVIVES unchanged at consumer_minor=3 — the
    //     per-code-table witness (a scalar-bump-to-4 would have broken this).
    EXPECT_EQ(translate_for_consumer(FIXPP_ERR_DICT_CONFIG, 3), FIXPP_ERR_DICT_CONFIG);
    const fixpp_error_t existing = translate(error::store_seqnum_overflow);
    ASSERT_EQ(existing, FIXPP_ERR_STORE_RUNTIME);
    EXPECT_EQ(translate_for_consumer(existing, 3), FIXPP_ERR_STORE_RUNTIME);

    // sub-introducing-minor downgrades existing codes too (the real <2 boundary).
    EXPECT_EQ(translate_for_consumer(FIXPP_ERR_DICT_CONFIG, 1), FIXPP_ERR_UNKNOWN);
}
