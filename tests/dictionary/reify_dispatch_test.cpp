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

#include <algorithm>
#include <cstddef>
#include <fixpp/core/error.hpp>
#include <fixpp/dict/reify.hpp>
#include <fixpp/dict/version_profile.hpp>
#include <fixpp/wire/message_view_contract.hpp>
#include <memory_resource>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "support/failing_pmr_resource.hpp"  // 057: view()-OOM degrade witness
#include "support/msvc_debug_arena_skip.hpp"
#include "support/reify_test_frame.hpp"  // 057: make_*_frame() helpers (E-6)

// Generated _dispatch/ headers — included ONCE by this dispatch-consuming TU
// (AC-D1 / contracts/reify_dispatch.hpp "included once per dispatch-consuming
// TU, never four times"). Defines dispatch::dispatch_fixt and
// dispatch::dispatch_application as INLINE helpers (Entity 8 / I-11).
// NOT included by the shipped include/fixpp/dict/reify.hpp (that would create
// a shipped→build-tree layering violation, NFR-003-8).
#include <fixpp/_dispatch/reify_dispatch_application.hpp>
#include <fixpp/_dispatch/reify_dispatch_fixt.hpp>

// 057 US3: reify_as<Msg> needs the flyweight + owning_message_traits<Msg>
// specialization (the generated _dispatch headers no longer pull Reify.hpp
// after the D-4 emitter change, so include it directly here).
#include <fixpp/v44/Reify.hpp>

namespace {

using fixpp::core::error;
using fixpp::dict::application_version;
using fixpp::dict::resolve_application_version;
using fixpp::dict::resolved_message_version;
using fixpp::dict::session_version;
using fixpp::dict::version_profile;
using MV = fixpp::wire::MessageView<fixpp::wire::access_mode::Index>;

// 057: frames a test byte buffer into a live MessageView<Index> and keeps every
// dependency (bytes, arena, frame_view, MV) as a member so the MV — which is
// [[clang::lifetimebound]] to its frame_view — never dangles. Mirrors the
// Framer seam used by reify_oom_test.
class ReifyFixture {
public:
    explicit ReifyFixture(std::vector<std::byte> frame) : frame_(std::move(frame)) {
        fixpp::wire::pmr_carry_buffer carry{frame_.size(), &arena_};
        fixpp::wire::Framer framer{};
        auto framed = framer.feed(std::span<const std::byte>{frame_.data(), frame_.size()}, carry,
                                  std::span<fixpp::wire::frame_view>{fvs_, 1});
        if (framed && !framed->empty()) {
            mv_.emplace(fvs_[0], &arena_);
        }
    }
    [[nodiscard]] bool ok() const noexcept { return mv_.has_value(); }
    [[nodiscard]] MV const& view() const noexcept { return *mv_; }

private:
    std::vector<std::byte> frame_;
    std::pmr::monotonic_buffer_resource arena_;
    fixpp::wire::frame_view fvs_[1]{};
    std::optional<MV> mv_;
};

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
    // 057: each of the 7 FIXT.1.1 admin MsgTypes dispatches to a LIVE handle
    // whose version() is {session_admin, vt11, Unknown} — never the fail-loud
    // default arm. (Empty-MV metadata oracle; the discriminating header-field
    // read is FixtAdminReify.DiscriminatingHeaderField below.)
    std::pmr::monotonic_buffer_resource arena;
    MV mv;
    version_profile const fixt_profile{session_version::vt11, application_version::v50sp2, true, 0};
    constexpr char kAdminTypes[] = {'0', '1', '2', '3', '4', '5', 'A'};
    for (char mt : kAdminTypes) {
        auto r = fixpp::dict::dispatch::dispatch_fixt(mv, mt, fixt_profile, &arena);
        ASSERT_TRUE(r.has_value())
            << "057: dispatch_fixt on admin MsgType '" << mt << "' must return a live handle";
        EXPECT_EQ(r->version().k, resolved_message_version::kind::session_admin);
        EXPECT_EQ(r->version().session, session_version::vt11);
        EXPECT_EQ(r->version().application, application_version::Unknown);
    }
}

// ─── FIXT-admin full-handle behavioural test (057: activated) ───────────────
// dispatch_fixt on each admin MsgType now returns a LIVE handle; version()
// carries {session_admin, vt11, Unknown}. (Empty-MV metadata oracle; the
// discriminating FIXT header-field read is the US2 witness, T016.)
TEST(ReifyDispatchFixt, SevenAdminMsgTypesFullHandle) {
    std::pmr::monotonic_buffer_resource arena;
    MV mv;
    version_profile const fixt_profile{session_version::vt11, application_version::v50sp2, true, 0};
    constexpr char kAdminTypes[] = {'0', '1', '2', '3', '4', '5', 'A'};
    for (char mt : kAdminTypes) {
        auto r = fixpp::dict::dispatch::dispatch_fixt(mv, mt, fixt_profile, &arena);
        ASSERT_TRUE(r.has_value())
            << "057: dispatch_fixt on admin MsgType '" << mt << "' must return a valid handle";
        EXPECT_EQ(r->version().k, resolved_message_version::kind::session_admin)
            << "AC-D2: FIXT admin handle must have kind == session_admin";
        EXPECT_EQ(r->version().session, session_version::vt11)
            << "AC-D2: FIXT admin handle must have session == vt11";
        EXPECT_EQ(r->version().application, application_version::Unknown)
            << "AC-D2: FIXT admin handle must have application == Unknown";
    }
}

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
// AC-D1 / AC-D3. 057: each (version, MsgType) now dispatches to a LIVE handle
// whose version().application matches the dispatched version. (Empty-MV
// metadata oracle; the discriminating per-field body reads are in T013.)
// ─────────────────────────────────────────────────────────────────────────────
TEST(ReifyDispatchApplication, MustIncludeSubsetAllHit) {
    // AC-G12 curated must-include subset. Each entry returns a live handle with
    // version().application == the dispatched version (never a default-arm miss).
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
        ASSERT_TRUE(r.has_value())
            << c.label << ": 057 dispatch_application must return a live handle";
        EXPECT_EQ(r->version().application, c.version)
            << "AC-D3: " << c.label
            << " handle.version().application must match the dispatched version";
    }
}

// ─── application full-handle behavioural test (057: activated) ──────────────
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
    // 057 (dict::reify() path): reify() on an EMPTY view has no MsgType(35), so
    // it fails at the get<35>-absent branch with dict_reify_unknown_msg_type
    // (FR-009 remap of the retired dict_reify_wire_body_not_ready) BEFORE the
    // application-version resolution runs. The dict_unresolved_application_version
    // propagation is exercised as a PURE function above (UnknownDefaultPropagates)
    // and end-to-end on a REAL frame in the T014 reify() error-contract witness.
    std::pmr::monotonic_buffer_resource arena;
    MV mv;
    version_profile const unknown_default{session_version::vt11, application_version::Unknown, true,
                                          0};
    auto r = fixpp::dict::reify(mv, unknown_default, &arena);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::dict_reify_unknown_msg_type)
        << "reify() on an empty view (absent tag 35) must return dict_reify_unknown_msg_type";
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

// ═════════════════════════════════════════════════════════════════════════════
// 057 US1 (T013) — discriminating per-field reify() round-trip witnesses. Each
// reads a REAL body field from the live handle (not header-only / empty-view
// green) and is mutation-tested: reverting the dispatch arm to the placeholder
// turns each RED (SC-003). FR-001/003/004/005/014; SC-001/002/003.
// ═════════════════════════════════════════════════════════════════════════════

constexpr version_profile kProfileV42{session_version::v42, application_version::v42, false, 0};
constexpr version_profile kProfileV44{session_version::v44, application_version::v44, false, 0};
constexpr version_profile kProfileV50sp2{session_version::v50sp2, application_version::v50sp2,
                                         false, 0};

TEST(ReifyRoundTrip, V44NewOrderSingleClOrdId) {
    ReifyFixture f{fixpp::test_support::make_nos_frame()};
    ASSERT_TRUE(f.ok());
    std::pmr::monotonic_buffer_resource mr;
    auto r = fixpp::dict::reify(f.view(), kProfileV44, &mr);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->version().k, resolved_message_version::kind::application);
    EXPECT_EQ(r->version().application, application_version::v44);
    auto clord = r->field_value(11);
    ASSERT_TRUE(clord.has_value());
    EXPECT_EQ(clord->as_string(), "ORD1")
        << "FR-004: typed read must return the exact wire ClOrdID";
}

TEST(ReifyRoundTrip, V42NewOrderSingleClOrdId) {
    ReifyFixture f{fixpp::test_support::make_nos_frame_v42()};
    ASSERT_TRUE(f.ok());
    std::pmr::monotonic_buffer_resource mr;
    auto r = fixpp::dict::reify(f.view(), kProfileV42, &mr);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->version().application, application_version::v42);
    ASSERT_TRUE(r->field_value(11).has_value());
    EXPECT_EQ(r->field_value(11)->as_string(), "ORD1");
}

TEST(ReifyRoundTrip, V50sp2NewOrderSingleClOrdId) {
    ReifyFixture f{fixpp::test_support::make_nos_frame_v50sp2()};
    ASSERT_TRUE(f.ok());
    std::pmr::monotonic_buffer_resource mr;
    auto r = fixpp::dict::reify(f.view(), kProfileV50sp2, &mr);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->version().application, application_version::v50sp2);
    ASSERT_TRUE(r->field_value(11).has_value());
    EXPECT_EQ(r->field_value(11)->as_string(), "ORD1");
}

TEST(ReifyRoundTrip, MultiCharAllocationReportBodyField) {
    // Two-char MsgType "AS" (v44 AllocationReport). Reads a real BODY field
    // (70=AllocID), so the assertion is discriminating, not a header-only read.
    ReifyFixture f{fixpp::test_support::make_allocation_report_frame()};
    ASSERT_TRUE(f.ok());
    std::pmr::monotonic_buffer_resource mr;
    auto r = fixpp::dict::reify(f.view(), kProfileV44, &mr);
    ASSERT_TRUE(r.has_value()) << "multi-char AS must dispatch to a live v44 handle";
    EXPECT_EQ(r->version().application, application_version::v44);
    EXPECT_EQ(r->msg_type(), "AS");
    auto alloc = r->field_value(70);
    ASSERT_TRUE(alloc.has_value());
    EXPECT_EQ(alloc->as_string(), "ALLOC1")
        << "FR-014: two-char dispatch must round-trip the AllocID body field";
}

TEST(ReifyRoundTrip, HandleSurvivesSourceBufferReuse) {
    // FR-005: reify() deep-copies the frame, so the handle stays valid after the
    // SOURCE buffer is overwritten. Overwrite BEFORE the first field read to
    // prove the copy (an aliasing bug would read the clobbered bytes).
    auto frame = fixpp::test_support::make_nos_frame();
    std::pmr::monotonic_buffer_resource src_arena;
    fixpp::wire::pmr_carry_buffer carry{frame.size(), &src_arena};
    fixpp::wire::Framer framer{};
    fixpp::wire::frame_view fvs[1]{};
    auto framed = framer.feed(std::span<const std::byte>{frame.data(), frame.size()}, carry,
                              std::span<fixpp::wire::frame_view>{fvs, 1});
    ASSERT_TRUE(framed.has_value());
    ASSERT_FALSE(framed->empty());
    MV mv{fvs[0], &src_arena};
    std::pmr::monotonic_buffer_resource mr;
    auto r = fixpp::dict::reify(mv, kProfileV44, &mr);
    ASSERT_TRUE(r.has_value());
    // Clobber the source frame buffer; the handle's own bytes_ must be intact.
    std::fill(frame.begin(), frame.end(), std::byte{'X'});
    auto clord = r->field_value(11);
    ASSERT_TRUE(clord.has_value());
    EXPECT_EQ(clord->as_string(), "ORD1")
        << "FR-005: handle must survive (deep-copied, not aliased) source-buffer reuse";
}

TEST(ReifyRoundTrip, HandleMoveAssignPreservesTargetAndResetsSource) {
    // Value semantics on the TYPE-ERASED owning_message_handle (reify()'s return),
    // mirroring ReifyMoveTest.* which covers the TYPED owning_<Msg> sibling: the
    // target adopts the source's owned bytes; the moved-from source is a valid
    // null-pimpl husk whose noexcept accessors return defaults (never UB/terminate).
    ReifyFixture fa{fixpp::test_support::make_nos_frame()};               // v44 NOS,  11=ORD1
    ReifyFixture fb{fixpp::test_support::make_allocation_report_frame()};  // v44 AS,  70=ALLOC1
    ASSERT_TRUE(fa.ok());
    ASSERT_TRUE(fb.ok());
    std::pmr::monotonic_buffer_resource mr;
    auto a = fixpp::dict::reify(fa.view(), kProfileV44, &mr);
    auto b = fixpp::dict::reify(fb.view(), kProfileV44, &mr);
    ASSERT_TRUE(a.has_value());
    ASSERT_TRUE(b.has_value());
    (void)a->view();  // populate a's lazy view_cache_ so a non-trivial pimpl moves

    *b = std::move(*a);

    // Target now reads a's NewOrderSingle content, NOT its prior AllocationReport.
    EXPECT_EQ(b->version().application, application_version::v44);
    auto cl = b->field_value(11);
    ASSERT_TRUE(cl.has_value());
    EXPECT_EQ(cl->as_string(), "ORD1")
        << "move-assign target adopts the source's owned bytes";
    EXPECT_FALSE(b->field_value(70).has_value())
        << "target's prior AllocationReport body (70=ALLOC1) is discarded on move-assign";

    // Moved-from source: valid null-pimpl husk; accessors return the neutral
    // defaults (session_admin/Unknown, empty view → empty msg_type / absent field).
    EXPECT_EQ(a->version().k, resolved_message_version::kind::session_admin);
    EXPECT_EQ(a->version().application, application_version::Unknown);
    EXPECT_TRUE(a->msg_type().empty());
    EXPECT_FALSE(a->field_value(11).has_value());
}

TEST(ReifyRoundTrip, ApplVerIdInFrameDrivesResolution) {
    // US1 Acceptance Scenario 4 (FR-003): a FIXT application frame carrying
    // ApplVerID(1128)="9" reifies as v50sp2 EVEN WITH a profile whose default_appl
    // is Unknown — resolution derives from the in-frame 1128, not the profile
    // default. The only end-to-end witness that a frame-borne 1128 drives
    // resolution through live dispatch.
    ReifyFixture f{fixpp::test_support::make_fixt_app_applverid_frame()};
    ASSERT_TRUE(f.ok());
    std::pmr::monotonic_buffer_resource mr;
    version_profile const fixt_unknown_default{session_version::vt11, application_version::Unknown,
                                               true, 0};
    auto r = fixpp::dict::reify(f.view(), fixt_unknown_default, &mr);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->version().application, application_version::v50sp2)
        << "FR-003: in-frame ApplVerID(1128)=9 must resolve to v50sp2, not the Unknown default";
    ASSERT_TRUE(r->field_value(11).has_value());
    EXPECT_EQ(r->field_value(11)->as_string(), "ORD1");
}

// ═════════════════════════════════════════════════════════════════════════════
// 057 US1 (T014) — preserved error-contract witnesses (FR-009, SC-004). Each
// asserts the EXACT error code, not merely !has_value().
// ═════════════════════════════════════════════════════════════════════════════

TEST(ReifyErrorContract, UnknownSingleCharMsgType) {
    // 35=!: single-char MsgType that no version emits (matches the '!' sentinel
    // used by UnknownMsgTypeInKnownVersionHitsInnerDefault) → inner default.
    ReifyFixture f{fixpp::test_support::assemble_frame(
        "8=FIX.4.4\x01", std::string("35=!\x01") + "34=1\x01" + "49=S\x01" + "56=T\x01")};
    ASSERT_TRUE(f.ok());
    std::pmr::monotonic_buffer_resource mr;
    auto r = fixpp::dict::reify(f.view(), kProfileV44, &mr);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::dict_reify_unknown_msg_type);
}

TEST(ReifyErrorContract, UnknownMultiCharMsgType) {
    // 35=ZZ: two-char MsgType with no v44 arm → two-char switch default.
    ReifyFixture f{fixpp::test_support::assemble_frame(
        "8=FIX.4.4\x01", std::string("35=ZZ\x01") + "34=1\x01" + "49=S\x01" + "56=T\x01")};
    ASSERT_TRUE(f.ok());
    std::pmr::monotonic_buffer_resource mr;
    auto r = fixpp::dict::reify(f.view(), kProfileV44, &mr);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::dict_reify_unknown_msg_type);
}

TEST(ReifyErrorContract, AbsentMsgTypeTag35) {
    // A frame with NO tag 35 → the get<35>-absent branch. Discriminating: assert
    // the exact remapped error (distinct from the empty-view path).
    ReifyFixture f{fixpp::test_support::assemble_frame(
        "8=FIX.4.4\x01", std::string("34=1\x01") + "49=S\x01" + "56=T\x01" + "11=ORD1\x01")};
    ASSERT_TRUE(f.ok());
    std::pmr::monotonic_buffer_resource mr;
    auto r = fixpp::dict::reify(f.view(), kProfileV44, &mr);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::dict_reify_unknown_msg_type)
        << "FR-009: absent tag 35 must return dict_reify_unknown_msg_type";
}

TEST(ReifyErrorContract, DeepCopyOomYieldsReifyOom) {
    // A memory_resource that cannot satisfy the byte deep-copy → dict_reify_oom.
    // MSVC debug/asan STL allocates a hidden _Container_proxy per pmr container
    // from this null-backed arena during a noexcept ctor → terminate, not a
    // catchable bad_alloc. Behaviour is covered on msvc-release + all Linux lanes.
    FIXPP_SKIP_ON_MSVC_DEBUG_ARENA();
    ReifyFixture f{fixpp::test_support::make_nos_frame()};
    ASSERT_TRUE(f.ok());
    std::pmr::monotonic_buffer_resource oom{std::pmr::null_memory_resource()};
    auto r = fixpp::dict::reify(f.view(), kProfileV44, &oom);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::dict_reify_oom);
}

TEST(ReifyErrorContract, ViewRebuildOomDegradesNotTerminate) {
    // 057 / 004-T059 hardening: the deep-copy (alloc #1) succeeds so reify()
    // returns a live handle, but the LAZY view() OffsetTable build (alloc #2)
    // then OOMs. owning_message_handle::view() is noexcept, so a throwing mr at
    // first field access must NOT terminate — the OffsetTable ctor degrades to
    // an empty table (out_of_memory) and field_value() reports field-absent.
    FIXPP_SKIP_ON_MSVC_DEBUG_ARENA();
    ReifyFixture f{fixpp::test_support::make_nos_frame()};
    ASSERT_TRUE(f.ok());
    std::array<std::byte, 1024 * 64> buf{};
    std::pmr::monotonic_buffer_resource upstream{buf.data(), buf.size()};
    fixpp::test_support::failing_pmr_resource fail{&upstream, /*fail_on_call_n=*/2};

    auto r = fixpp::dict::reify(f.view(), kProfileV44, &fail);
    ASSERT_TRUE(r.has_value()) << "deep-copy (alloc #1) must succeed → live handle";
    // First field access triggers the lazy view() rebuild → OffsetTable alloc #2
    // fails. Must degrade, not terminate.
    auto clord = r->field_value(11);
    EXPECT_FALSE(clord.has_value())
        << "OOM during OffsetTable build → field-absent (graceful degrade)";
    auto st = r->view().offsets().build_status();
    ASSERT_FALSE(st.has_value());
    EXPECT_EQ(st.error(), fixpp::core::error::out_of_memory)
        << "OffsetTable must record out_of_memory (graceful), not std::terminate";
}

TEST(ReifyErrorContract, UnresolvedApplicationVersion) {
    // 35=D app frame, absent 1128, profile default_appl == Unknown →
    // dict_unresolved_application_version (AC-D6).
    ReifyFixture f{fixpp::test_support::make_nos_frame()};
    ASSERT_TRUE(f.ok());
    std::pmr::monotonic_buffer_resource mr;
    version_profile const unknown_default{session_version::vt11, application_version::Unknown,
                                          false, 0};
    auto r = fixpp::dict::reify(f.view(), unknown_default, &mr);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::dict_unresolved_application_version);
}

TEST(ReifyErrorContract, BadApplVerIdYieldsUnknownApplVerId) {
    // 35=D with a non-parsing 1128=99 → dict_unknown_appl_ver_id (AC-D7).
    ReifyFixture f{fixpp::test_support::assemble_frame(
        "8=FIXT.1.1\x01", std::string("35=D\x01") + "34=1\x01" + "1128=99\x01" + "49=S\x01" +
                              "56=T\x01" + "11=ORD1\x01")};
    ASSERT_TRUE(f.ok());
    std::pmr::monotonic_buffer_resource mr;
    version_profile const fixt{session_version::vt11, application_version::v50sp2, true, 0};
    auto r = fixpp::dict::reify(f.view(), fixt, &mr);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::dict_unknown_appl_ver_id);
}

// ═════════════════════════════════════════════════════════════════════════════
// 057 US2 (T016) — FIXT-admin reify() discriminating witness. A real admin
// frame (35=A Logon) reifies as session_admin AND round-trips a header field.
// FR-002/003/004; SC-002. Mutation-tested (revert arm → placeholder → RED).
// ═════════════════════════════════════════════════════════════════════════════

TEST(FixtAdminReify, DiscriminatingHeaderField) {
    ReifyFixture f{fixpp::test_support::make_fixt_admin_frame()};
    ASSERT_TRUE(f.ok());
    std::pmr::monotonic_buffer_resource mr;
    version_profile const fixt_profile{session_version::vt11, application_version::v50sp2, true, 0};
    auto r = fixpp::dict::reify(f.view(), fixt_profile, &mr);
    ASSERT_TRUE(r.has_value()) << "FR-002: FIXT-admin frame must reify to a live handle";
    EXPECT_EQ(r->version().k, resolved_message_version::kind::session_admin);
    EXPECT_EQ(r->version().session, session_version::vt11);
    EXPECT_EQ(r->version().application, application_version::Unknown);
    EXPECT_EQ(r->msg_type(), "A");
    auto sender = r->field_value(49);
    ASSERT_TRUE(sender.has_value());
    EXPECT_EQ(sender->as_string(), "SENDER")
        << "FR-004: FIXT-admin handle must round-trip the exact SenderCompID";
}

// ═════════════════════════════════════════════════════════════════════════════
// 057 US3 (T018) — compile-time typed reify_as<Msg> (FR-006). No runtime bridge:
// Msg is compile-time known; matching MsgType → populated owning_<Msg>, mismatch
// or absent tag 35 → dict_reify_msg_type_mismatch.
// ═════════════════════════════════════════════════════════════════════════════

TEST(ReifyAsTyped, MatchingFrameReadsField) {
    ReifyFixture f{fixpp::test_support::make_nos_frame()};  // 35=D, 11=ORD1 (v44)
    ASSERT_TRUE(f.ok());
    std::pmr::monotonic_buffer_resource mr;
    auto r = fixpp::dict::reify_as<fixpp::v44::NewOrderSingle>(f.view(), &mr);
    ASSERT_TRUE(r.has_value()) << "matching MsgType must produce a populated owning_<Msg>";
    auto clord = r->field_value(11);
    ASSERT_TRUE(clord.has_value());
    EXPECT_EQ(clord->as_string(), "ORD1") << "FR-006: typed reify_as must round-trip ClOrdID";
}

TEST(ReifyAsTyped, MismatchedMsgTypeRejected) {
    // 35=AS (AllocationReport) reified as NewOrderSingle (35=D) → mismatch.
    ReifyFixture f{fixpp::test_support::make_allocation_report_frame()};
    ASSERT_TRUE(f.ok());
    std::pmr::monotonic_buffer_resource mr;
    auto r = fixpp::dict::reify_as<fixpp::v44::NewOrderSingle>(f.view(), &mr);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::dict_reify_msg_type_mismatch);
}

TEST(ReifyAsTyped, AbsentMsgTypeRejected) {
    // A frame with no tag 35 → dict_reify_msg_type_mismatch (mirrors !mt_fv).
    ReifyFixture f{fixpp::test_support::assemble_frame(
        "8=FIX.4.4\x01", std::string("34=1\x01") + "49=S\x01" + "56=T\x01" + "11=ORD1\x01")};
    ASSERT_TRUE(f.ok());
    std::pmr::monotonic_buffer_resource mr;
    auto r = fixpp::dict::reify_as<fixpp::v44::NewOrderSingle>(f.view(), &mr);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::dict_reify_msg_type_mismatch);
}

}  // namespace
