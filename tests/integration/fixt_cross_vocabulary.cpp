// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/integration/fixt_cross_vocabulary.cpp — T035 [P] [US3]
//
// Seam #10b, AC-D4: the [2c §6.3] worked-example resolution end-to-end.
//
// Worked example from [2c §6.3]:
//   1. Logon (MsgType=A)       → FIXT admin → vt11 {session_admin, vt11, Unknown}
//   2. NOS ApplVerID=9         → application → v50sp2 {application, vt11, v50sp2}
//   3. NOS ApplVerID=6         → application override → v44 {application, vt11, v44}
//   4. OCR no ApplVerID        → application (session default) → v50sp2 {application, vt11, v50sp2}
//   5. Heartbeat (MsgType=0)   → FIXT admin → vt11 {session_admin, vt11, Unknown}
//
// What this test covers (057 — R6 lifted, live dispatch):
//   - The resolution DECISIONS (step 1: is_fixt_admin → session_admin;
//     step 2: resolve_application_version → correct application_version).
//     These are PURE functions (no wire state dependency) and ARE runtime-testable.
//   - The dispatch SWITCH entries: dispatch_fixt(mv, mt, profile) for FIXT
//     admin types; dispatch_application(mv, mt_sv, resolved, profile) for app
//     messages now return LIVE typed owning_message_handle values (not a
//     placeholder error) for a matching MsgType, and
//     dict_reify_unknown_msg_type for an unrecognised/empty view (FR-009).
//   - resolve_application_version with the exact AC-D4 ApplVerID wire values:
//     "9" → v50sp2; "6" → v44; "" (absent) → v50sp2 (session default).
//
// A real byte frame → dict::reify(view, profile, mr) → a typed
// owning_message_handle round-trip (MsgType read from real frame bytes,
// handle.version() reflecting the resolved application/session version) is
// covered separately in reify_dispatch_test.cpp (ReifyRoundTrip.*); this file
// stays scoped to the AC-D4 [2c §6.3] worked-example resolution/dispatch
// decisions using empty MessageView probes.
//
// Oracle: specs/003-dictionary-codegen/spec.md AC-D4 / seam #10b;
//         [2c §6.3] worked example; contracts/reify_dispatch.hpp.
#include <gtest/gtest.h>

#include <fixpp/core/error.hpp>
#include <fixpp/dict/reify.hpp>
#include <fixpp/dict/version_profile.hpp>
#include <fixpp/wire/message_view_contract.hpp>
#include <memory_resource>
#include <string_view>
#include <type_traits>

// Generated dispatch headers — included ONCE by this dispatch-consuming TU.
// AC-D1 / contracts/reify_dispatch.hpp "included once per dispatch-consuming TU".
#include <fixpp/_dispatch/reify_dispatch_application.hpp>
#include <fixpp/_dispatch/reify_dispatch_fixt.hpp>

namespace {

using fixpp::core::error;
using fixpp::dict::application_version;
using fixpp::dict::resolve_application_version;
using fixpp::dict::resolved_message_version;
using fixpp::dict::session_version;
using fixpp::dict::version_profile;
using MV = fixpp::wire::MessageView<fixpp::wire::access_mode::Index>;

// ─── [2c §6.3] session profile ───────────────────────────────────────────────
// FIXT.1.1 session. The DefaultApplVerID was negotiated at Logon time as
// FIX.5.0SP2 (ApplVerID wire value "9" → application_version::v50sp2).
constexpr version_profile kSessionProfile{
    session_version::vt11,        // FIXT.1.1 session layer
    application_version::v50sp2,  // DefaultApplVerID negotiated at Logon
    true,                         // per-message ApplVerID(1128) allowed
    0                             // _reserved
};

// ─────────────────────────────────────────────────────────────────────────────
// AC-D4 Resolution Decision Tests — PURE (no wire-frame dependency).
// Verify the resolution algorithm gives the correct application_version for
// each AC-D4 worked-example message.
// ─────────────────────────────────────────────────────────────────────────────

TEST(FixtCrossVocabulary, AcD4_Logon_IsFixtAdmin) {
    // [2c §6.3] Frame 1: Logon (MsgType=A) → FIXT admin.
    // 057: dispatch_fixt on 'A' must return a live session_admin handle
    // (not dict_reify_unknown_msg_type).
    std::pmr::monotonic_buffer_resource arena;
    MV mv;
    auto r = fixpp::dict::dispatch::dispatch_fixt(mv, 'A', kSessionProfile, &arena);
    ASSERT_TRUE(r.has_value()) << "057: Logon dispatch must return a live session_admin handle";
    EXPECT_EQ(r->version().k, resolved_message_version::kind::session_admin);
    EXPECT_EQ(r->version().session, session_version::vt11);
    EXPECT_EQ(r->version().application, application_version::Unknown);
}

TEST(FixtCrossVocabulary, AcD4_NOS_ApplVerID9_ResolvesToV50sp2) {
    // [2c §6.3] Frame 2: NOS with ApplVerID=9 → application v50sp2.
    // Resolution decision: resolve_application_version(profile, "9") → v50sp2.
    auto resolved = resolve_application_version(kSessionProfile, "9");
    ASSERT_TRUE(resolved.has_value()) << "AC-D4 Frame 2: ApplVerID='9' must parse successfully";
    EXPECT_EQ(*resolved, application_version::v50sp2)
        << "AC-D4 Frame 2: ApplVerID='9' → v50sp2 per [2c §4.3] wire→C++ map";

    // Dispatch decision: dispatch_application with resolved v50sp2 + MsgType 'D'.
    std::pmr::monotonic_buffer_resource arena;
    MV mv;
    auto r =
        fixpp::dict::dispatch::dispatch_application(mv, "D", *resolved, kSessionProfile, &arena);
    ASSERT_TRUE(r.has_value()) << "057: v50sp2 NOS dispatch must return a live handle";
    EXPECT_EQ(r->version().application, application_version::v50sp2);
}

TEST(FixtCrossVocabulary, AcD4_NOS_ApplVerID6_ResolvesToV44Override) {
    // [2c §6.3] Frame 3: NOS with per-message ApplVerID=6 → v44 override.
    // Resolution: resolve_application_version(profile, "6") → v44 (per-message
    // override; the session default v50sp2 is ignored).
    auto resolved = resolve_application_version(kSessionProfile, "6");
    ASSERT_TRUE(resolved.has_value()) << "AC-D4 Frame 3: ApplVerID='6' must parse successfully";
    EXPECT_EQ(*resolved, application_version::v44)
        << "AC-D4 Frame 3: per-message ApplVerID='6' → v44 override";

    // Dispatch decision: dispatch_application with v44 + MsgType 'D'.
    std::pmr::monotonic_buffer_resource arena;
    MV mv;
    auto r =
        fixpp::dict::dispatch::dispatch_application(mv, "D", *resolved, kSessionProfile, &arena);
    ASSERT_TRUE(r.has_value()) << "057: v44 NOS dispatch must return a live handle";
    EXPECT_EQ(r->version().application, application_version::v44);
}

TEST(FixtCrossVocabulary, AcD4_OCR_NoApplVerID_UsesSessionDefault) {
    // [2c §6.3] Frame 4: OrderCancelRequest with no ApplVerID → session default v50sp2.
    // Resolution: resolve_application_version(profile, "") → v50sp2 (default_appl).
    auto resolved = resolve_application_version(kSessionProfile, "");
    ASSERT_TRUE(resolved.has_value())
        << "AC-D4 Frame 4: empty ApplVerID with v50sp2 default must resolve";
    EXPECT_EQ(*resolved, application_version::v50sp2)
        << "AC-D4 Frame 4: absent ApplVerID → session default v50sp2";

    // Dispatch decision: dispatch_application with v50sp2 + MsgType 'F'.
    std::pmr::monotonic_buffer_resource arena;
    MV mv;
    auto r =
        fixpp::dict::dispatch::dispatch_application(mv, "F", *resolved, kSessionProfile, &arena);
    ASSERT_TRUE(r.has_value()) << "057: v50sp2 OCR dispatch must return a live handle";
    EXPECT_EQ(r->version().application, application_version::v50sp2);
}

TEST(FixtCrossVocabulary, AcD4_Heartbeat_IsFixtAdmin) {
    // [2c §6.3] Frame 5: Heartbeat (MsgType=0) → FIXT admin.
    // Resolution decision: is_fixt_admin('0') == true.
    // Resolved: {session_admin, vt11, Unknown}.
    std::pmr::monotonic_buffer_resource arena;
    MV mv;
    auto r = fixpp::dict::dispatch::dispatch_fixt(mv, '0', kSessionProfile, &arena);
    ASSERT_TRUE(r.has_value()) << "057: Heartbeat dispatch must return a live session_admin handle";
    EXPECT_EQ(r->version().k, resolved_message_version::kind::session_admin);
    EXPECT_EQ(r->version().session, session_version::vt11);
    EXPECT_EQ(r->version().application, application_version::Unknown);
}

// ─────────────────────────────────────────────────────────────────────────────
// Full dict::reify() call shape — AC-D4 worked example, empty-view boundary.
// dict::reify() is callable and, on an empty MessageView (no MsgType(35)
// present), returns dict_reify_unknown_msg_type — the FR-009 remap of the
// retired R6 placeholder (dict_reify_wire_body_not_ready is no longer
// returned by any path; the resolution path did not incorrectly fall through
// either). The real-frame success round-trip (a live typed handle) is covered
// in reify_dispatch_test.cpp (ReifyRoundTrip.*).
// ─────────────────────────────────────────────────────────────────────────────
TEST(FixtCrossVocabulary, AcD4_FullReifyCallable_EmptyViewUnknownMsgType) {
    // AC-D4 worked-example full dict::reify() call on an empty view.
    std::pmr::monotonic_buffer_resource arena;
    MV mv;  // empty view — no MsgType(35)
    auto r = fixpp::dict::reify(mv, kSessionProfile, &arena);
    ASSERT_FALSE(r.has_value()) << "empty view has no MsgType → reify() must error";
    // 057: an empty view has no MsgType(35), so reify() returns
    // dict_reify_unknown_msg_type from the get<35>-absent branch (FR-009 remap
    // of the retired placeholder), never dict_xml_parse_failed. A REAL-frame
    // success path is witnessed in reify_dispatch_test.cpp (ReifyRoundTrip.*).
    EXPECT_EQ(r.error(), error::dict_reify_unknown_msg_type)
        << "reify() on an empty view must return dict_reify_unknown_msg_type";

    // The resolution DECISIONS are verified by the pure-function tests above,
    // which are NOT R6-blocked (resolve_application_version is PURE).
}

// ─────────────────────────────────────────────────────────────────────────────
// Boundary: dict::reify() on a profile where the resolution would propagate
// dict_unresolved_application_version for a non-FIXT frame (AC-D6 path).
// Verified at the resolve_application_version level (this decision is a PURE
// function with no wire-frame dependency).
// ─────────────────────────────────────────────────────────────────────────────
TEST(FixtCrossVocabulary, UnknownDefaultProfileResolutionDecision) {
    // Profile with Unknown DefaultApplVerID + absent ApplVerID(1128) →
    // dict_unresolved_application_version (AC-D6 propagation path).
    // Verified at the resolve_application_version level (PURE function).
    version_profile const unknown_default{session_version::vt11, application_version::Unknown, true,
                                          0};
    auto res = resolve_application_version(unknown_default, "");
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), error::dict_unresolved_application_version)
        << "AC-D6: Unknown DefaultApplVerID + no per-message ApplVerID → "
           "dict_unresolved_application_version (not dict_reify_unknown_msg_type)";
}

}  // namespace
