// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/codegen/vlatest_dispatch_exclusion_test.cpp
//
// 076-fix-latest-typed-codegen T011 [US1] -- V-3 dispatch-exclusion witness
// (specs/076-fix-latest-typed-codegen/tasks.md T011; contract V-3; FR-009/
// SC-005). The single generated cross-version dispatch_application(...)
// surface (fixpp/_dispatch/reify_dispatch_application.hpp) is keyed on
// fixpp::dict::application_version, which has NO `vlatest` enumerator (074's
// disposition: FIX Latest's wire application-version resolves to the
// existing v50sp2 slot, no distinct ApplVerID/application_version) -- so a
// raw FIX-Latest wire message routed through dispatch_application(...) can
// NEVER yield a vlatest-typed owner, by construction. This TU pins BOTH
// halves of the resulting behaviour empirically found in the generated
// switch (mirrors reify_dispatch_test.cpp's shape/call conventions):
//
//   (a1) mis-resolve-to-v50sp2: a FIX-Latest MsgType that v50sp2.xml ALSO
//        carries (e.g. "D" NewOrderSingle) dispatches successfully but the
//        returned handle is version().application == v50sp2 -- never a
//        vlatest-labeled owner. This is the ApplExtID(1156)=303
//        differentiation gap (074 L-074-1) -- deferred, PINNED here as a
//        regression anchor so a future fix is deliberate, not accidental.
//   (a2) fail-loud: a FIX-Latest-ONLY MsgType absent from v50sp2.xml's own
//        message set (e.g. "EC" SettlementStatusRequest -- EP303-new,
//        verified absent from the v50sp2 dispatch case list) returns
//        dict_reify_unknown_msg_type -- neither misdispatch nor a silent
//        vlatest owner.
//   (b)  the v50sp2 case-arm count in the outer (resolved_application)
//        switch stays exactly ONE (injective wire-application-version ->
//        C++-enum map preserved; a second v50sp2 case label would be a
//        compile error regardless, so this is a textual regression pin over
//        the generated artifact, not a novel proof).
//
// No fixpp::vlatest header is included (no Fields/Messages/Validator/Reify/
// Builders) -- this witness only needs the EXISTING v42/v44/v50sp2 dispatch
// surface + application_version enum, both already compiled by
// reify_dispatch_test.cpp. Cheap by construction (no read/write-tier vlatest
// compile cost).
//
// Anchors: specs/076-fix-latest-typed-codegen/tasks.md T011;
//          tests/dictionary/reify_dispatch_test.cpp (call-shape precedent);
//          build/.../fixpp/_dispatch/reify_dispatch_application.hpp
//          (generated switch under test).

#include <gtest/gtest.h>

#include <fixpp/core/error.hpp>
#include <fixpp/dict/reify.hpp>
#include <fixpp/dict/version_profile.hpp>
#include <fixpp/wire/message_view_contract.hpp>
#include <fstream>
#include <memory_resource>
#include <sstream>
#include <string>

// Generated _dispatch/ header -- included ONCE by this dispatch-consuming TU
// (AC-D1 precedent), same convention as reify_dispatch_test.cpp.
#include <fixpp/_dispatch/reify_dispatch_application.hpp>

#ifndef FIXPP_DISPATCH_APPLICATION_HEADER_PATH
#error "FIXPP_DISPATCH_APPLICATION_HEADER_PATH must be set by CMake target_compile_definitions"
#endif

namespace {

using fixpp::core::error;
using fixpp::dict::application_version;
using fixpp::dict::session_version;
using fixpp::dict::version_profile;
using MV = fixpp::wire::MessageView<fixpp::wire::access_mode::Index>;

}  // namespace

// ─── (a1) mis-resolve-to-v50sp2: a MsgType shared between FIX Latest and
// v50sp2.xml dispatches successfully but is labeled v50sp2, never vlatest
// (there is no application_version::vlatest to compare against -- its
// absence IS the pin). ────────────────────────────────────────────────────
TEST(VlatestDispatchExclusion076, SharedMsgTypeResolvesToV50sp2NotVlatest) {
    std::pmr::monotonic_buffer_resource arena;
    MV mv;
    version_profile const profile{session_version::vt11, application_version::v50sp2, true, 0};

    auto r = fixpp::dict::dispatch::dispatch_application(mv, "D", application_version::v50sp2,
                                                          profile, &arena);
    ASSERT_TRUE(r.has_value())
        << "a FIX-Latest MsgType also present in v50sp2.xml (NewOrderSingle/D) must still "
           "dispatch via the existing v50sp2 arm";
    EXPECT_EQ(r->version().application, application_version::v50sp2)
        << "074/L-074-1 regression anchor: the resolved owner is labeled v50sp2, not a distinct "
           "vlatest application_version (which does not exist) -- FIX-Latest-vs-v50sp2 wire "
           "differentiation (ApplExtID(1156)=303) is deferred post-v1.0";
}

// ─── (a2) fail-loud: a FIX-Latest-ONLY MsgType (absent from v50sp2.xml's own
// message set, verified via the codegen dispatch case list) is neither
// misdispatched nor silently accepted -- dict_reify_unknown_msg_type. ──────
TEST(VlatestDispatchExclusion076, FixLatestOnlyMsgTypeHitsFailLoudDefault) {
    // "EC" (SettlementStatusRequest) is one of 17 EP303-new application
    // MsgTypes present in the 181-message vlatest set but ABSENT from the
    // v50sp2 dispatch arm's own case list. Verified by mechanical
    // set-difference: extracting all case-label MsgTypes from the v50sp2
    // block of build/.../fixpp/_dispatch/reify_dispatch_application.hpp
    // (156 codes) against the 181 vlatest MsgTypes (from Reify.hpp) yields
    // exactly 25 absent codes -- 8 are the admin complement {0,1,2,3,4,5,A,n}
    // (handled by dispatch_fixt, not this surface) and the remaining 17 are
    // "EC".."ES" (SettlementStatusRequest..TradingSessionStatusAck).
    std::pmr::monotonic_buffer_resource arena;
    MV mv;
    version_profile const profile{session_version::vt11, application_version::v50sp2, true, 0};

    auto r = fixpp::dict::dispatch::dispatch_application(mv, "EC", application_version::v50sp2,
                                                          profile, &arena);
    ASSERT_FALSE(r.has_value())
        << "a FIX-Latest-only MsgType (SettlementStatusRequest/EC, absent from v50sp2.xml) must "
           "NOT dispatch successfully -- neither a v50sp2 misdispatch nor a vlatest owner";
    EXPECT_EQ(r.error(), error::dict_reify_unknown_msg_type)
        << "I-11/AC-D5: must hit the fail-loud default arm, never a silent success";
}

// ─── (b) exactly one v50sp2 case-arm in the outer (resolved_application)
// switch -- injective wire-application-version -> C++-enum map preserved.
// A textual regression pin over the generated artifact: a duplicate case
// label for the SAME enum value in the SAME switch is a compile error
// regardless, so successful compilation of the #include above already
// proves at most one; this asserts exactly one (not zero -- i.e. the arm
// was not accidentally deleted/renamed either). ────────────────────────────
TEST(VlatestDispatchExclusion076, OuterSwitchHasExactlyOneV50sp2CaseArm) {
    std::ifstream in{FIXPP_DISPATCH_APPLICATION_HEADER_PATH};
    ASSERT_TRUE(in.is_open()) << "failed to open " << FIXPP_DISPATCH_APPLICATION_HEADER_PATH;
    std::stringstream ss;
    ss << in.rdbuf();
    std::string const text = ss.str();

    std::string const needle = "case ::fixpp::dict::application_version::v50sp2:";
    std::size_t count = 0;
    std::size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos) {
        ++count;
        pos += needle.size();
    }
    EXPECT_EQ(count, 1U) << "outer switch must carry exactly one "
                            "application_version::v50sp2 case arm";
}
