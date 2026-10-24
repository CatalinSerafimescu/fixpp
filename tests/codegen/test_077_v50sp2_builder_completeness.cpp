// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/codegen/test_077_v50sp2_builder_completeness.cpp
//
// 077-builder-args-dedup T023 [US4] -- v50sp2 builder-completeness census.
// Sibling of test_077_v44_builder_completeness.cpp -- see that file and
// builder_completeness_common.hpp for the four-leg design and the
// non-circularity rationale. v50sp2 has NO in-scope exclusion (research.md
// R4: v44's {BE,BF,BW,BX,BY} set is v44-specific; other versions emit their
// full application set) -- 156 in-scope messages.
//
// This is its OWN executable (not joined to the v44 test above): it
// #includes the ~75MB deduped fixpp::v50sp2::all.hpp; keeping it
// separate mirrors tests/session/CMakeLists.txt's
// test_077_allversions_builder_roundtrip split rationale (RSS discipline).
//
// Anchors: specs/077-builder-args-dedup/tasks.md T023;
//          contracts/builder-completeness.md C1-C4.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <fixpp/v50sp2/all.hpp>  // GENERATED -- build_<Msg>/validate_<Msg>/builder_registry
#include <iterator>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "builder_completeness_common.hpp"

#ifndef FIXPP_DICT_DATA_DIR
#error "FIXPP_DICT_DATA_DIR must be set by CMake target_compile_definitions"
#endif
#ifndef FIXPP_CODEGEN_V50SP2_BUILDERS_HPP
#error "FIXPP_CODEGEN_V50SP2_BUILDERS_HPP must be set by CMake target_compile_definitions"
#endif

namespace {

using fixpp_test::builder_completeness::Entry;

#define FIXPP_BC_ENTRY(msgtype, ident)                                             \
    Entry{msgtype, reinterpret_cast<void const*>(&::fixpp::v50sp2::build_##ident), \
          reinterpret_cast<void const*>(&::fixpp::v50sp2::validate_##ident)},
std::vector<Entry> const kEntries = {
#include "generated/v50sp2_builder_completeness_entries.def"
};
#undef FIXPP_BC_ENTRY

#define FIXPP_BC_ENTRY(msgtype, ident) {msgtype, #ident},
std::vector<std::pair<std::string_view, std::string_view>> const kMsgtypeToIdent = {
#include "generated/v50sp2_builder_completeness_entries.def"
};
#undef FIXPP_BC_ENTRY

// 095-tail-evidence-pass FR-007 (research R-5): the N-002 / N-003 per-row pins.
// A key is "registered" when it is a msg_type of the compiled builder_registry
// array; the registry entry carries no callable, so each pin then calls that
// MsgType's build_<Msg> and checks the body opens with the literal 35=<mt><SOH>.
bool registered(std::string_view mt) {
    return std::ranges::any_of(::fixpp::v50sp2::builder_registry,
                               [&](auto const& e) { return e.msg_type == mt; });
}

std::string as_string(std::span<std::byte const> bytes) {
    return {reinterpret_cast<char const*>(bytes.data()), bytes.size()};
}

}  // namespace

TEST(BuilderCompleteness077V50sp2, DefTableMatchesRawXmlWalk) {
    using namespace fixpp_test::builder_completeness;
    std::set<std::string> const expected =
        legacy_expected_msgtypes(std::string(FIXPP_DICT_DATA_DIR) + "/FIX50SP2.xml");
    EXPECT_EQ(expected.size(), 156U) << "raw-XML walk in-scope count";

    std::set<std::string> const table = def_msgtypes(kEntries);
    EXPECT_EQ(table.size(), 156U) << "committed .def table entry count";

    std::vector<std::string> only_expected;
    std::vector<std::string> only_table;
    std::ranges::set_difference(expected, table, std::back_inserter(only_expected));
    std::ranges::set_difference(table, expected, std::back_inserter(only_table));
    EXPECT_TRUE(only_expected.empty())
        << "msg_types in raw-XML walk but NOT in .def table (table is stale)";
    EXPECT_TRUE(only_table.empty())
        << "msg_types in .def table but NOT in raw-XML walk (table is stale)";
}

TEST(BuilderCompleteness077V50sp2, AllEntryPointsResolved) {
    ASSERT_EQ(kEntries.size(), 156U);
    for (auto const& e : kEntries) {
        EXPECT_NE(e.build_addr, nullptr) << "msg_type=" << e.msg_type;
        EXPECT_NE(e.validate_addr, nullptr) << "msg_type=" << e.msg_type;
    }
}

TEST(BuilderCompleteness077V50sp2, RegistryExactSetEqualsRawXmlWalk) {
    using namespace fixpp_test::builder_completeness;
    std::set<std::string> const expected =
        legacy_expected_msgtypes(std::string(FIXPP_DICT_DATA_DIR) + "/FIX50SP2.xml");
    std::set<std::string> const registry =
        parse_registry_msgtypes(FIXPP_CODEGEN_V50SP2_BUILDERS_HPP);
    EXPECT_EQ(registry, expected);
}

TEST(BuilderCompleteness077V50sp2, BuildFnSignaturesMatchRawXmlWalk) {
    using namespace fixpp_test::builder_completeness;
    std::set<std::string> const expected =
        legacy_expected_msgtypes(std::string(FIXPP_DICT_DATA_DIR) + "/FIX50SP2.xml");
    std::set<std::string> const build_idents = parse_build_fn_identifiers(
        std::filesystem::path(FIXPP_CODEGEN_V50SP2_BUILDERS_HPP).parent_path().string());

    std::set<std::string> translated_msgtypes;
    for (auto const& ident : build_idents) {
        auto const it =
            std::ranges::find_if(kMsgtypeToIdent, [&](auto const& p) { return p.second == ident; });
        ASSERT_NE(it, kMsgtypeToIdent.end())
            << "build_" << ident << "( found in " << FIXPP_CODEGEN_V50SP2_BUILDERS_HPP
            << " has no matching entry in the .def table -- an emitted builder outside the raw-XML "
               "scope";
        translated_msgtypes.emplace(it->first);
    }
    EXPECT_EQ(translated_msgtypes, expected);
}

TEST(BuilderCompleteness077V50sp2, N002_UserRequestUserResponseRegistered) {
    EXPECT_TRUE(registered("BE")) << "\"BE\" (UserRequest) is not a builder_registry key";
    EXPECT_TRUE(registered("BF")) << "\"BF\" (UserResponse) is not a builder_registry key";

    std::array<std::byte, 1024> out{};
    ::fixpp::v50sp2::UserRequestArgs be{};
    be.user_request_id = "N002_pin_user_request_id";
    be.user_request_type = 1;
    be.username = "N002_pin_username";
    auto const built_be = ::fixpp::v50sp2::build_UserRequest(out, be);
    ASSERT_TRUE(built_be.has_value()) << "build_UserRequest refused the pin build";
    EXPECT_TRUE(as_string(*built_be).starts_with("35=BE\x01"))
        << "build_UserRequest body: " << as_string(*built_be);

    ::fixpp::v50sp2::UserResponseArgs bf{};
    bf.user_request_id = "N002_pin_user_request_id";
    bf.username = "N002_pin_username";
    auto const built_bf = ::fixpp::v50sp2::build_UserResponse(out, bf);
    ASSERT_TRUE(built_bf.has_value()) << "build_UserResponse refused the pin build";
    EXPECT_TRUE(as_string(*built_bf).starts_with("35=BF\x01"))
        << "build_UserResponse body: " << as_string(*built_bf);
}

TEST(BuilderCompleteness077V50sp2, N003_ApplicationMessageRequestAckReportRegistered) {
    EXPECT_TRUE(registered("BW"))
        << "\"BW\" (ApplicationMessageRequest) is not a builder_registry key";
    EXPECT_TRUE(registered("BX"))
        << "\"BX\" (ApplicationMessageRequestAck) is not a builder_registry key";
    EXPECT_TRUE(registered("BY"))
        << "\"BY\" (ApplicationMessageReport) is not a builder_registry key";

    std::array<std::byte, 1024> out{};
    ::fixpp::v50sp2::ApplicationMessageRequestArgs bw{};
    bw.appl_req_id = "N003_pin_appl_req_id";
    bw.appl_req_type = 0;
    auto const built_bw = ::fixpp::v50sp2::build_ApplicationMessageRequest(out, bw);
    ASSERT_TRUE(built_bw.has_value()) << "build_ApplicationMessageRequest refused the pin build";
    EXPECT_TRUE(as_string(*built_bw).starts_with("35=BW\x01"))
        << "build_ApplicationMessageRequest body: " << as_string(*built_bw);

    ::fixpp::v50sp2::ApplicationMessageRequestAckArgs bx{};
    bx.appl_response_id = "N003_pin_appl_response_id";
    auto const built_bx = ::fixpp::v50sp2::build_ApplicationMessageRequestAck(out, bx);
    ASSERT_TRUE(built_bx.has_value()) << "build_ApplicationMessageRequestAck refused the pin build";
    EXPECT_TRUE(as_string(*built_bx).starts_with("35=BX\x01"))
        << "build_ApplicationMessageRequestAck body: " << as_string(*built_bx);

    ::fixpp::v50sp2::ApplicationMessageReportArgs by{};
    by.appl_report_id = "N003_pin_appl_report_id";
    by.appl_report_type = 0;
    auto const built_by = ::fixpp::v50sp2::build_ApplicationMessageReport(out, by);
    ASSERT_TRUE(built_by.has_value()) << "build_ApplicationMessageReport refused the pin build";
    EXPECT_TRUE(as_string(*built_by).starts_with("35=BY\x01"))
        << "build_ApplicationMessageReport body: " << as_string(*built_by);
}
