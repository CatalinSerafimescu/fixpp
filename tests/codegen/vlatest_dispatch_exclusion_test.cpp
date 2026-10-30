// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/codegen/vlatest_dispatch_exclusion_test.cpp
//
// 097-fix-latest-applverid-10 (FR-009, FR-012(c); contracts W-9). Rewritten from
// 076-fix-latest-typed-codegen T011, whose premise (no application_version for
// FIX Latest) 097 retires.
//
// The rule. FIX Latest is its own application version, `vlatest` (wire
// ApplVerID "10"). The generated dispatch_application(...) in
// fixpp/_dispatch/reify_dispatch_application.hpp has one outer case per
// emit_dispatch.cpp `kAppVersions` row whose IR the codegen run built. This
// binary is built only with the FIX Latest tier ON, so the vlatest IR is
// present and the outer switch has a `vlatest` arm. Each arm's inner switch
// holds exactly the MsgTypes of that version's IR, and its handle is labelled
// with that version.
//
// The derivation. The vlatest IR is built from OrchestraLoader's
// Dictionary::messages() over dictionaries/orchestra/OrchestraFIXLatest.xml
// (tools/codegen/fixpp-codegen/ir.cpp), so the (a3) census walks that same list:
// every MsgType it holds, other than the FIXT admin MsgTypes dict::reify
// intercepts before application dispatch, must reify under `vlatest` with a
// handle labelled `vlatest`. The census guards the emitter loop, not the loader.
//
// Halves:
//   (a1)/(a2) under a v50sp2 resolution the v50sp2 arm is unchanged: a MsgType
//             FIX50SP2.xml carries is labelled v50sp2, and one it lacks fails loud.
//   (a3)      under a vlatest resolution: the census over the FIX Latest
//             dictionary, plus samples (`EC`, `D` reify as vlatest; `ZZ`, which
//             no dictionary defines, fails loud).
//   (a4)      XMLnonFIX (35=n) is not an admin MsgType dict::reify intercepts,
//             so it reaches application dispatch and reifies as vlatest.
//   (b)       the outer switch has exactly one v50sp2 case and exactly one
//             vlatest case (a textual pin over the generated header).
// The admin-interception and in-frame ApplVerID halves go through dict::reify
// on framed messages and live in tests/dictionary/reify_dispatch_test.cpp.
//
// Anchors: specs/097-fix-latest-applverid-10/contracts/witnesses.md W-9;
//          tests/dictionary/reify_dispatch_test.cpp (call-shape precedent).

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <fixpp/core/error.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/orchestra_loader.hpp>
#include <fixpp/dict/reify.hpp>
#include <fixpp/dict/version_profile.hpp>
#include <fixpp/wire/message_view_contract.hpp>
#include <fstream>
#include <memory_resource>
#include <sstream>
#include <string>
#include <string_view>

// Generated _dispatch/ header -- included ONCE by this dispatch-consuming TU
// (AC-D1 precedent), same convention as reify_dispatch_test.cpp.
#include <fixpp/_dispatch/reify_dispatch_application.hpp>

#ifndef FIXPP_DISPATCH_APPLICATION_HEADER_PATH
#error "FIXPP_DISPATCH_APPLICATION_HEADER_PATH must be set by CMake target_compile_definitions"
#endif
#ifndef FIXPP_DICT_DATA_DIR
#error "FIXPP_DICT_DATA_DIR must be set by CMake target_compile_definitions"
#endif

namespace {

using fixpp::core::error;
using fixpp::dict::application_version;
using fixpp::dict::resolved_message_version;
using fixpp::dict::session_version;
using fixpp::dict::version_profile;
using MV = fixpp::wire::MessageView<fixpp::wire::access_mode::Index>;

constexpr version_profile kFixtV50sp2Default{.session = session_version::vt11,
                                             .default_appl = application_version::v50sp2,
                                             .has_per_message_override = true,
                                             ._reserved = 0};
constexpr version_profile kFixtVlatestDefault{.session = session_version::vt11,
                                              .default_appl = application_version::vlatest,
                                              .has_per_message_override = true,
                                              ._reserved = 0};

// The FIXT admin MsgTypes dict::reify intercepts before application dispatch
// (src/dictionary/reify.cpp is_fixt_admin; that predicate is TU-local, so the set
// is spelled out here independently).
constexpr std::array kFixtAdminMsgTypes{'0', '1', '2', '3', '4', '5', 'A'};

[[nodiscard]] std::string read_dispatch_header() {
    std::ifstream in{FIXPP_DISPATCH_APPLICATION_HEADER_PATH};
    if (!in.is_open()) {
        return {};
    }
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

[[nodiscard]] std::size_t count_occurrences(std::string const& text, std::string_view needle) {
    std::size_t count = 0;
    std::size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos) {
        ++count;
        pos += needle.size();
    }
    return count;
}

// Dispatches `msg_type` under a vlatest resolution with an empty MV and checks the
// handle is an application handle labelled vlatest. Non-fatal throughout, so a
// caller looping over MsgTypes reports every failing MsgType.
void expect_reifies_as_vlatest(std::string_view msg_type) {
    std::pmr::monotonic_buffer_resource arena;
    MV mv;
    auto r = fixpp::dict::dispatch::dispatch_application(mv, msg_type, application_version::vlatest,
                                                         kFixtVlatestDefault, &arena);
    EXPECT_TRUE(r.has_value()) << "097: MsgType \"" << msg_type
                               << "\" under a vlatest resolution must reify; got error "
                               << fixpp::core::to_string(r.error());
    if (!r.has_value()) {
        return;
    }
    EXPECT_EQ(r->version().k, resolved_message_version::kind::application)
        << "097: MsgType \"" << msg_type << "\" must reify as an application handle";
    EXPECT_EQ(r->version().application, application_version::vlatest)
        << "097: MsgType \"" << msg_type << "\" must be labelled vlatest";
}

}  // namespace

// ─── (a1) under a v50sp2 resolution, a MsgType FIX Latest and FIX50SP2.xml share
// dispatches through the v50sp2 arm and is labelled v50sp2. 097: the vlatest
// label belongs to a vlatest resolution (ApplVerID "10"), witnessed by (a3). ──
TEST(VlatestDispatchExclusion076, SharedMsgTypeResolvesToV50sp2NotVlatest) {
    std::pmr::monotonic_buffer_resource arena;
    MV mv;

    auto r = fixpp::dict::dispatch::dispatch_application(mv, "D", application_version::v50sp2,
                                                         kFixtV50sp2Default, &arena);
    ASSERT_TRUE(r.has_value())
        << "a FIX-Latest MsgType also present in v50sp2.xml (NewOrderSingle/D) must still "
           "dispatch via the existing v50sp2 arm";
    EXPECT_EQ(r->version().application, application_version::v50sp2)
        << "097: a v50sp2 resolution labels the handle v50sp2; only a vlatest resolution "
           "(ApplVerID \"10\") labels it vlatest";
}

// ─── (a2) under a v50sp2 resolution, a MsgType FIX Latest defines and FIX50SP2.xml
// does not fails loud (dict_reify_unknown_msg_type). 097: the v50sp2 arm holds the
// FIX50SP2.xml MsgTypes only; such a MsgType reifies under vlatest instead (a3). ─
TEST(VlatestDispatchExclusion076, FixLatestOnlyMsgTypeHitsFailLoudDefault) {
    // "EC" (SettlementStatusRequest) is defined by FIX Latest and absent from the
    // v50sp2 arm. Re-derive the set of such MsgTypes: take the case labels of the
    // `application_version::v50sp2` arm in the generated
    // _dispatch/reify_dispatch_application.hpp, and subtract them from the MsgTypes
    // of OrchestraLoader's Dictionary::messages() over
    // dictionaries/orchestra/OrchestraFIXLatest.xml.
    std::pmr::monotonic_buffer_resource arena;
    MV mv;

    auto r = fixpp::dict::dispatch::dispatch_application(mv, "EC", application_version::v50sp2,
                                                         kFixtV50sp2Default, &arena);
    ASSERT_FALSE(r.has_value())
        << "097: a MsgType FIX50SP2.xml lacks (SettlementStatusRequest/EC) must NOT dispatch "
           "under a v50sp2 resolution";
    EXPECT_EQ(r.error(), error::dict_reify_unknown_msg_type)
        << "I-11/AC-D5: must hit the fail-loud default arm, never a silent success";
}

// ─── (a3) census: every MsgType the FIX Latest dictionary defines, other than the
// FIXT admin MsgTypes, reifies under a vlatest resolution with a vlatest label. ──
TEST(VlatestDispatch097, EveryFixLatestMsgTypeReifiesAsVlatest) {
    std::pmr::monotonic_buffer_resource dict_mr;
    fixpp::dict::OrchestraLoader loader;
    auto const dict = loader.load(
        std::filesystem::path{FIXPP_DICT_DATA_DIR} / "orchestra" / "OrchestraFIXLatest.xml",
        &dict_mr);
    auto const messages = dict.messages();

    std::array<bool, kFixtAdminMsgTypes.size()> admin_found{};
    std::size_t skipped = 0;
    std::size_t dispatched = 0;
    for (auto const& m : messages) {
        bool is_admin = false;
        for (std::size_t i = 0; i < kFixtAdminMsgTypes.size(); ++i) {
            if (m.msg_type.size() == 1 && m.msg_type.front() == kFixtAdminMsgTypes[i]) {
                admin_found[i] = true;
                is_admin = true;
            }
        }
        if (is_admin) {
            ++skipped;
            continue;
        }
        ++dispatched;
        expect_reifies_as_vlatest(m.msg_type);
    }

    // Non-vacuity: every admin MsgType was found in the dictionary and skipped, and
    // every other MsgType was dispatched (dispatched = messages().size() minus the
    // admin MsgTypes skipped).
    for (std::size_t i = 0; i < kFixtAdminMsgTypes.size(); ++i) {
        EXPECT_TRUE(admin_found[i]) << "097: admin MsgType '" << kFixtAdminMsgTypes[i]
                                    << "' must be defined by the FIX Latest dictionary";
    }
    EXPECT_EQ(skipped, kFixtAdminMsgTypes.size())
        << "097: each admin MsgType must be skipped exactly once";
    EXPECT_EQ(dispatched, messages.size() - kFixtAdminMsgTypes.size())
        << "097: every non-admin MsgType of the FIX Latest dictionary must be dispatched";
}

// ─── (a3) samples: `EC` (FIX-Latest-only) and `D` reify as vlatest; `ZZ`, which no
// dictionary defines, fails loud under the vlatest arm's two-character default. ──
TEST(VlatestDispatch097, SampleMsgTypesUnderVlatest) {
    expect_reifies_as_vlatest("EC");
    expect_reifies_as_vlatest("D");

    // `ZZ` is absent from every dictionary: re-derive with
    // `grep -c 'msgType="ZZ"' dictionaries/orchestra/OrchestraFIXLatest.xml` and
    // `grep -c "msgtype='ZZ'" dictionaries/*.xml` (0 for every file; the same greps
    // for `D` are non-zero).
    std::pmr::monotonic_buffer_resource arena;
    MV mv;
    auto r = fixpp::dict::dispatch::dispatch_application(mv, "ZZ", application_version::vlatest,
                                                         kFixtVlatestDefault, &arena);
    EXPECT_FALSE(r.has_value())
        << "097: a MsgType FIX Latest does not define (ZZ) must not reify under vlatest";
    if (!r.has_value()) {
        EXPECT_EQ(r.error(), error::dict_reify_unknown_msg_type)
            << "097: ZZ under vlatest must fail loud with dict_reify_unknown_msg_type";
    }
}

// ─── (a4) XMLnonFIX (35=n): dict::reify does not intercept it (it is not a FIXT
// admin MsgType), so it reaches application dispatch and reifies as vlatest. ────
TEST(VlatestDispatch097, XmlNonFixReifiesAsVlatestApplication) { expect_reifies_as_vlatest("n"); }

// ─── (b) exactly one v50sp2 case-arm in the outer (resolved_application)
// switch -- injective wire-application-version -> C++-enum map preserved.
// A textual regression pin over the generated artifact: a duplicate case
// label for the SAME enum value in the SAME switch is a compile error
// regardless, so successful compilation of the #include above already
// proves at most one; this asserts exactly one (not zero -- i.e. the arm
// was not accidentally deleted/renamed either). ────────────────────────────
TEST(VlatestDispatchExclusion076, OuterSwitchHasExactlyOneV50sp2CaseArm) {
    std::string const text = read_dispatch_header();
    ASSERT_FALSE(text.empty()) << "failed to read " << FIXPP_DISPATCH_APPLICATION_HEADER_PATH;
    EXPECT_EQ(count_occurrences(text, "case ::fixpp::dict::application_version::v50sp2:"), 1U)
        << "outer switch must carry exactly one application_version::v50sp2 case arm";
}

// ─── (b) 097: exactly one vlatest case-arm in the same outer switch (the FIX
// Latest tier is ON for this binary, so the vlatest IR is present). ───────────
TEST(VlatestDispatch097, OuterSwitchHasExactlyOneVlatestCaseArm) {
    std::string const text = read_dispatch_header();
    ASSERT_FALSE(text.empty()) << "failed to read " << FIXPP_DISPATCH_APPLICATION_HEADER_PATH;
    EXPECT_EQ(count_occurrences(text, "case ::fixpp::dict::application_version::vlatest:"), 1U)
        << "097: outer switch must carry exactly one application_version::vlatest case arm";
}
