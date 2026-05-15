// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/reify_dispatch_test.cpp — T034 [P] [US3]
//
// Seam #15a (7 FIXT admin MsgTypes dispatch switch), #15b (application
// MsgTypes — AC-G12 curated must-include subset), #15c (dict_unresolved_
// application_version propagation), #10c (fail-loud default arm for
// runtime-XML-only resolved version). AC-D1..D3/D6/D7.
//
// What IS testable now under R6:
//   - resolve_application_version full wire→C++ map coverage (PURE function
//     — no wire state dependency). Reuses version_profile_test.cpp coverage
//     for completeness in dispatch context. AC-D6/D7.
//   - The generated dispatch switch SHAPE/COVERAGE at compile time (every
//     must-include (version, MsgType) from must_include_manifest.txt has a
//     case that compiles — bringing in the generated _dispatch/ headers and
//     using static_asserts). AC-D1/D3.
//   - The 7 FIXT admin MsgType cases exist and compile (seam #15a). AC-D2.
//   - dispatch::dispatch_fixt / dispatch::dispatch_application inline helpers
//     can be called — shape + error propagation visible at compile-time.
//   - The fail-loud default arm (I-11 / R3): feed a runtime-XML-only resolved
//     application_version (e.g. v43, which has no codegen owner) →
//     dict_reify_unknown_msg_type (AC-D5 / seam #10c).
//   - dict_unresolved_application_version propagation from
//     resolve_application_version with Unknown default (AC-D6 / seam #15c).
//   - dict_unknown_appl_ver_id propagation for a non-parsing ApplVerID string
//     (AC-D7).
//
// R6-blocked (documented, 2b-unblock):
//   - Behavioural round-trip: a real parsed frame → dict::reify() → correct
//     typed owner. dict::reify() peeks get<35>() which always returns
//     dict_xml_parse_failed (frozen stub). Full behavioural dispatch (FIXT
//     admin match → vt11::owning_<Msg>; application match → vXX owner) is
//     blocked until 2b swaps in the real wire body with frame state. The test
//     documents the intent with a comment and calls the dispatch helpers
//     directly to verify the switch shape fires correctly.
//   - owning_message_handle round-trip assertions (as<Msg>() returns non-null,
//     version()/msg_type() carry the resolved values): blocked on the full
//     owning_message_handle implementation which requires a real typed owner
//     from the dispatch switch. The handle shape (move-only, method signatures)
//     is compile-time-checked in reify_test.cpp (AC-R6).
//
// Oracle: specs/003-dictionary-codegen/contracts/reify_dispatch.hpp;
//         data-model Entity 8 / Invariant I-11; spec AC-D1..D7 / seam #15a/b/c/#10c.
//         must_include_manifest.txt (AC-G12 curated CI subset).
#include <gtest/gtest.h>

#include <fixpp/core/error.hpp>
#include <fixpp/dict/reify.hpp>
#include <fixpp/dict/version_profile.hpp>
#include <fixpp/wire/message_view_contract.hpp>
#include <memory_resource>
#include <string_view>
#include <type_traits>
#include <utility>

// Generated _dispatch/ headers — included ONCE by this dispatch-consuming TU
// (AC-D1 / contracts/reify_dispatch.hpp "included once per dispatch-consuming
// TU, never four times"). Defines dispatch::dispatch_fixt and
// dispatch::dispatch_application as INLINE helpers (Entity 8 / I-11).
// NOT included by the shipped include/fixpp/dict/reify.hpp (that would create
// a shipped→build-tree layering violation, NFR-003-8).
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

// ─────────────────────────────────────────────────────────────────────────────
// Compile-time shape: dispatch helpers exist and have correct signatures.
// AC-D1 — _dispatch/ headers emitted; AC-D2 — dispatch_fixt is callable.
// AC-D3 — dispatch_application is callable.
// ─────────────────────────────────────────────────────────────────────────────
static_assert(std::is_same_v<decltype(fixpp::dict::dispatch::dispatch_fixt(
                                 std::declval<MV const&>(), std::declval<char>(),
                                 std::declval<version_profile>(),
                                 std::declval<std::pmr::memory_resource*>())),
                             fixpp::core::expected_t<fixpp::dict::owning_message_handle>>,
              "AC-D2: dispatch_fixt must return expected_t<owning_message_handle>");

static_assert(
    std::is_same_v<decltype(fixpp::dict::dispatch::dispatch_application(
                       std::declval<MV const&>(), std::declval<std::string_view>(),
                       std::declval<application_version>(), std::declval<version_profile>(),
                       std::declval<std::pmr::memory_resource*>())),
                   fixpp::core::expected_t<fixpp::dict::owning_message_handle>>,
    "AC-D3: dispatch_application must return expected_t<owning_message_handle>");

// ─────────────────────────────────────────────────────────────────────────────
// Seam #15a — 7 FIXT admin MsgTypes. AC-D2.
// gate-b/r1 RC#3-B hardening: replace "not-the-default-error" with the EXACT
// R6 placeholder error code (dict_reify_wire_body_not_ready).
// This ensures misdispatch cannot stay green — if the dispatch hits the default
// arm (dict_reify_unknown_msg_type) OR returns success, this test fails.
//
// R6 oracle: dispatch_fixt on an admin MsgType → from_view → wire_body_not_ready.
//
// 2b-unblock: when 2b supplies real frame bytes, dispatch_fixt will return a
// valid owning_message_handle (has_value() == true). At that point:
//   □ Remove ASSERT_FALSE / EXPECT_EQ error checks.
//   □ Assert r.has_value() == true.
//   □ Assert r->version().k == resolved_message_version::kind::session_admin.
//   □ Assert r->version().session == session_version::vt11.
//   □ Assert r->msg_type() == the expected MsgType string_view.
// ─────────────────────────────────────────────────────────────────────────────
TEST(ReifyDispatchFixt, SevenAdminMsgTypesAllHit) {
    // AC-D2 / seam #15a / gate-b/r1 RC#3-B: 7 FIXT.1.1 admin MsgTypes must
    // return EXACTLY dict_reify_wire_body_not_ready — not success, and not
    // dict_reify_unknown_msg_type (that is the default arm / I-11 / AC-D5).
    // Any change to this error code breaks this oracle, preventing misdispatch
    // from silently passing.
    std::pmr::monotonic_buffer_resource arena;
    MV mv;
    version_profile const fixt_profile{session_version::vt11, application_version::v50sp2, true, 0};
    constexpr char kAdminTypes[] = {'0', '1', '2', '3', '4', '5', 'A'};
    for (char mt : kAdminTypes) {
        auto r = fixpp::dict::dispatch::dispatch_fixt(mv, mt, fixt_profile, &arena);
        ASSERT_FALSE(r.has_value())
            << "AC-D2: dispatch_fixt on admin MsgType '" << mt
            << "' must not return success under R6 (from_view: wire body not ready)";
        EXPECT_EQ(r.error(), error::dict_reify_wire_body_not_ready)
            << "AC-D2 R6 positive oracle: dispatch_fixt on admin MsgType '" << mt
            << "' must return dict_reify_wire_body_not_ready (NOT dict_reify_unknown_msg_type "
               "which would indicate a default-arm hit, NOR dict_xml_parse_failed)";
        // Redundant guard — belt + suspenders: also assert NOT the default arm.
        EXPECT_NE(r.error(), error::dict_reify_unknown_msg_type)
            << "AC-D2: FIXT admin MsgType '" << mt << "' must NOT hit the fail-loud default arm";
    }
}

// ─── #if R6 behavioural block — activates when 2b lands ─────────────────────
// 2b-unblock: this block compiles always but the tests skip under R6.
// Set FIXPP_R6_WIRE_BODY_READY=1 to remove the GTEST_SKIP().
// ─────────────────────────────────────────────────────────────────────────────
#ifndef FIXPP_R6_WIRE_BODY_READY

TEST(ReifyDispatchFixt, SevenAdminMsgTypesFullHandle_R6Deferred) {
    // AC-D2 behavioural: parsed-frame → dispatch_fixt → has_value() + version() exact.
    // R6-deferred (spec §5): from_view returns wire_body_not_ready.
    // 2b-unblock: remove GTEST_SKIP; assert has_value(), version().k == session_admin,
    //             version().session == vt11, msg_type() == expected string.
    GTEST_SKIP() << "R6-deferred (spec §5): full dispatch round-trip awaits 2b wire body";
}

#else  // FIXPP_R6_WIRE_BODY_READY

TEST(ReifyDispatchFixt, SevenAdminMsgTypesFullHandle) {
    std::pmr::monotonic_buffer_resource arena;
    MV mv;
    version_profile const fixt_profile{session_version::vt11, application_version::v50sp2, true, 0};
    constexpr char kAdminTypes[] = {'0', '1', '2', '3', '4', '5', 'A'};
    for (char mt : kAdminTypes) {
        auto r = fixpp::dict::dispatch::dispatch_fixt(mv, mt, fixt_profile, &arena);
        ASSERT_TRUE(r.has_value())
            << "2b: dispatch_fixt on admin MsgType '" << mt << "' must return a valid handle";
        EXPECT_EQ(r->version().k, resolved_message_version::kind::session_admin)
            << "AC-D2: FIXT admin handle must have kind == session_admin";
        EXPECT_EQ(r->version().session, session_version::vt11)
            << "AC-D2: FIXT admin handle must have session == vt11";
        EXPECT_EQ(r->version().application, application_version::Unknown)
            << "AC-D2: FIXT admin handle must have application == Unknown";
    }
}

#endif  // FIXPP_R6_WIRE_BODY_READY

TEST(ReifyDispatchFixt, NonAdminMsgTypeHitsDefault) {
    // AC-D7 / I-11 / seam #15a: a MsgType not in the 7 admin set returns
    // dict_reify_unknown_msg_type from the fail-loud default arm.
    std::pmr::monotonic_buffer_resource arena;
    MV mv;
    version_profile const fixt_profile{session_version::vt11, application_version::v50sp2, true, 0};
    // 'D' (NewOrderSingle) is application, not FIXT admin.
    auto r = fixpp::dict::dispatch::dispatch_fixt(mv, 'D', fixt_profile, &arena);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::dict_reify_unknown_msg_type)
        << "I-11 / AC-D7: non-admin MsgType must return dict_reify_unknown_msg_type";
}

// ─────────────────────────────────────────────────────────────────────────────
// Seam #15b — application MsgTypes (AC-G12 curated must-include subset).
// AC-D1 / AC-D3. gate-b/r1 RC#3-B: replace "not-the-default-error" with the
// EXACT R6 placeholder error code (dict_reify_wire_body_not_ready).
// This ensures misdispatch (wrong version/MsgType combo hitting the default
// arm and returning dict_reify_unknown_msg_type) cannot stay green.
//
// R6-unblock (2b): results change to valid owning_message_handle with correct
// version().application and msg_type() — activate via FIXPP_R6_WIRE_BODY_READY.
// ─────────────────────────────────────────────────────────────────────────────
TEST(ReifyDispatchApplication, MustIncludeSubsetAllHit) {
    // AC-G12 curated must-include subset / gate-b/r1 RC#3-B positive oracle.
    // Each entry MUST return dict_reify_wire_body_not_ready — not
    // dict_reify_unknown_msg_type (default arm) and not success.
    struct Case {
        application_version version;
        std::string_view msg_type;
        const char* label;
    };
    constexpr Case kCases[] = {
        {application_version::v42, "D", "v42 NewOrderSingle"},
        {application_version::v44, "D", "v44 NewOrderSingle"},
        {application_version::v50sp2, "D", "v50sp2 NewOrderSingle"},
        {application_version::v44, "8", "v44 ExecutionReport"},
        {application_version::v50sp2, "8", "v50sp2 ExecutionReport"},
        {application_version::v50sp2, "W", "v50sp2 MarketDataSnapshotFullRefresh"},
        {application_version::v50sp2, "F", "v50sp2 OrderCancelRequest"},
        {application_version::v44, "A", "v44 Logon (app, not FIXT admin)"},
    };

    std::pmr::monotonic_buffer_resource arena;
    MV mv;
    version_profile const profile{session_version::vt11, application_version::v50sp2, true, 0};

    for (auto const& c : kCases) {
        auto r =
            fixpp::dict::dispatch::dispatch_application(mv, c.msg_type, c.version, profile, &arena);
        ASSERT_FALSE(r.has_value())
            << c.label << ": dispatch_application must not return success under R6";
        EXPECT_EQ(r.error(), error::dict_reify_wire_body_not_ready)
            << "AC-D3 R6 positive oracle: " << c.label
            << " (version=" << static_cast<int>(c.version) << ", MsgType=" << c.msg_type
            << ") must return dict_reify_wire_body_not_ready "
               "(not dict_reify_unknown_msg_type which would indicate a default-arm hit)";
        // Belt + suspenders: also assert NOT the fail-loud outer default arm.
        EXPECT_NE(r.error(), error::dict_reify_unknown_msg_type)
            << "AC-D3 / I-11: " << c.label << " must NOT hit the fail-loud outer default arm";
    }
}

// ─── #if R6 behavioural block — activates when 2b lands ─────────────────────
#ifndef FIXPP_R6_WIRE_BODY_READY

TEST(ReifyDispatchApplication, MustIncludeSubsetFullHandles_R6Deferred) {
    // AC-D3 behavioural: dispatch_application → has_value() + version() + msg_type() exact.
    // R6-deferred (spec §5): from_view returns wire_body_not_ready.
    // 2b-unblock: assert has_value(), version().application == c.version, msg_type() == c.msg_type.
    GTEST_SKIP() << "R6-deferred (spec §5): full dispatch round-trip awaits 2b wire body";
}

#else  // FIXPP_R6_WIRE_BODY_READY

TEST(ReifyDispatchApplication, MustIncludeSubsetFullHandles) {
    struct Case {
        application_version version;
        std::string_view msg_type;
        const char* label;
    };
    constexpr Case kCases[] = {
        {application_version::v42, "D", "v42 NewOrderSingle"},
        {application_version::v44, "D", "v44 NewOrderSingle"},
        {application_version::v50sp2, "D", "v50sp2 NewOrderSingle"},
        {application_version::v44, "8", "v44 ExecutionReport"},
        {application_version::v50sp2, "8", "v50sp2 ExecutionReport"},
        {application_version::v50sp2, "W", "v50sp2 MarketDataSnapshotFullRefresh"},
        {application_version::v50sp2, "F", "v50sp2 OrderCancelRequest"},
        {application_version::v44, "A", "v44 Logon (app, not FIXT admin)"},
    };
    std::pmr::monotonic_buffer_resource arena;
    MV mv;
    version_profile const profile{session_version::vt11, application_version::v50sp2, true, 0};
    for (auto const& c : kCases) {
        auto r =
            fixpp::dict::dispatch::dispatch_application(mv, c.msg_type, c.version, profile, &arena);
        ASSERT_TRUE(r.has_value()) << c.label << ": must return valid handle";
        EXPECT_EQ(r->version().application, c.version)
            << c.label << ": handle.version().application must match the dispatched version";
    }
}

#endif  // FIXPP_R6_WIRE_BODY_READY

// ─────────────────────────────────────────────────────────────────────────────
// Seam #10c — fail-loud default arm for runtime-XML-only resolved version.
// AC-D5 / I-11 / R3.
// ─────────────────────────────────────────────────────────────────────────────
TEST(ReifyDispatchApplication, RuntimeXmlOnlyVersionHitsOuterDefault) {
    // AC-D5 / seam #10c: runtime-XML-only versions (v40/v41/v43/v50/v50sp1/Unknown)
    // have no codegen-emitted owner. The outer default arm in the application
    // switch must return dict_reify_unknown_msg_type (never misdispatch — I-11 / R3).
    // No FIX43.xml dependency — synthetic fixture with a hand-built MV.
    std::pmr::monotonic_buffer_resource arena;
    MV mv;
    version_profile const profile{session_version::v43, application_version::v43, false, 0};

    constexpr application_version kRuntimeOnlyVersions[] = {
        application_version::v40, application_version::v41,    application_version::v43,
        application_version::v50, application_version::v50sp1, application_version::Unknown,
    };
    for (auto av : kRuntimeOnlyVersions) {
        auto r = fixpp::dict::dispatch::dispatch_application(mv, "D", av, profile, &arena);
        ASSERT_FALSE(r.has_value())
            << "dispatch_application must not succeed for runtime-XML-only version";
        EXPECT_EQ(r.error(), error::dict_reify_unknown_msg_type)
            << "AC-D5 / I-11: runtime-XML-only version " << static_cast<int>(av)
            << " must return dict_reify_unknown_msg_type from fail-loud outer default";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Seam #15c — dict_unresolved_application_version propagation. AC-D6.
// ─────────────────────────────────────────────────────────────────────────────
TEST(ReifyDispatchApplicationResolution, UnknownDefaultPropagates) {
    // AC-D6 / seam #15c: a FIXT.1.1 profile with default_appl == Unknown + a
    // message lacking ApplVerID(1128) (empty appl_ver_id) must yield
    // dict_unresolved_application_version from resolve_application_version,
    // NOT dict_reify_unknown_msg_type (the v1.0 misdiagnosis sentinel
    // fall-through is CLOSED per RC#1).
    version_profile const unknown_default{session_version::vt11, application_version::Unknown, true,
                                          0};
    auto r = resolve_application_version(unknown_default, "");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::dict_unresolved_application_version)
        << "AC-D6: empty ApplVerID + Unknown default_appl must yield "
           "dict_unresolved_application_version (NOT dict_reify_unknown_msg_type)";
}

TEST(ReifyDispatchApplicationResolution, UnknownDefaultPropagation_ViaReify) {
    // AC-D6 (dict::reify() path): dict::reify with an Unknown-default profile
    // and a frozen-stub MV returns dict_xml_parse_failed (get<35>() frozen).
    // R6-blocked: the dict_unresolved_application_version propagation through
    // dict::reify() is tested at the resolve_application_version level above
    // (PURE function, fully testable). dict::reify() itself is MsgType-read-
    // blocked under R6 (get<35>() returns dict_xml_parse_failed).
    // 2b-unblock: when get<35>() returns a real MsgType, dict::reify() will
    // propagate dict_unresolved_application_version for a non-FIXT MsgType
    // with an Unknown default_appl and absent ApplVerID. This test confirms
    // the dict::reify() function is callable and returns an error under R6.
    std::pmr::monotonic_buffer_resource arena;
    MV mv;
    version_profile const unknown_default{session_version::vt11, application_version::Unknown, true,
                                          0};
    auto r = fixpp::dict::reify(mv, unknown_default, &arena);
    ASSERT_FALSE(r.has_value()) << "dict::reify must return an error under R6 (frozen stub)";
    // R6: error is dict_xml_parse_failed (get<35>() frozen) — NOT the
    // dict_unresolved_application_version we'd get with real bytes.
    // The resolution propagation is verified via resolve_application_version
    // directly (test above). 2b-unblock: this test will need updating.
    (void)r.error();  // error code is R6-implementation-defined (frozen stub)
}

// ─────────────────────────────────────────────────────────────────────────────
// AC-D7 — dict_unknown_appl_ver_id for a non-parsing ApplVerID string.
// ─────────────────────────────────────────────────────────────────────────────
TEST(ReifyDispatchApplicationResolution, BadApplVerIdYieldsUnknownApplVerId) {
    // AC-D7: resolve_application_version("x", profile) → dict_unknown_appl_ver_id.
    // Not the outer dispatch default (dict_reify_unknown_msg_type) — distinct
    // error for "parse failure" vs "no codegen owner for resolved version".
    version_profile const profile{session_version::vt11, application_version::v50sp2, true, 0};
    for (std::string_view bad : {"0", "1", "A", "x", "10", "99"}) {
        auto r = resolve_application_version(profile, bad);
        ASSERT_FALSE(r.has_value()) << "bad ApplVerID=" << bad;
        EXPECT_EQ(r.error(), error::dict_unknown_appl_ver_id)
            << "AC-D7: non-parsing ApplVerID '" << bad << "' must yield dict_unknown_appl_ver_id";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// dict_reify_unknown_msg_type for an unknown MsgType in a known version (seam #10c).
// AC-D7 (inner default arm, distinct from outer version-not-found default).
// ─────────────────────────────────────────────────────────────────────────────
TEST(ReifyDispatchApplication, UnknownMsgTypeInKnownVersionHitsInnerDefault) {
    // AC-D7 / I-11 inner default: a MsgType with no codegen entry in an otherwise
    // known version (e.g. a FIX-Latest or A-014..A-034 MsgType) returns
    // dict_reify_unknown_msg_type from the inner switch default arm.
    // We use '!' as a clearly invalid MsgType that no version emits.
    std::pmr::monotonic_buffer_resource arena;
    MV mv;
    version_profile const profile{session_version::vt11, application_version::v50sp2, true, 0};

    for (application_version av :
         {application_version::v42, application_version::v44, application_version::v50sp2}) {
        auto r = fixpp::dict::dispatch::dispatch_application(mv, "!", av, profile, &arena);
        ASSERT_FALSE(r.has_value());
        EXPECT_EQ(r.error(), error::dict_reify_unknown_msg_type)
            << "AC-D7 inner default: unknown MsgType '!' in version " << static_cast<int>(av)
            << " must return dict_reify_unknown_msg_type";
    }
}

}  // namespace
