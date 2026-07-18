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

#include <fixpp/v50sp2/all.hpp>  // GENERATED -- build_<Msg>/validate_<Msg>/builder_registry

#include "builder_completeness_common.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <iterator>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

}  // namespace

TEST(BuilderCompleteness077V50sp2, DefTableMatchesRawXmlWalk) {
    using namespace fixpp_test::builder_completeness;
    std::set<std::string> const expected =
        legacy_expected_msgtypes(std::string(FIXPP_DICT_DATA_DIR) + "/FIX50SP2.xml");
    EXPECT_EQ(expected.size(), 156U) << "raw-XML walk in-scope count";

    std::set<std::string> const table = def_msgtypes(kEntries);
    EXPECT_EQ(table.size(), 156U) << "committed .def table entry count";

    std::vector<std::string> only_expected, only_table;
    std::set_difference(expected.begin(), expected.end(), table.begin(), table.end(),
                         std::back_inserter(only_expected));
    std::set_difference(table.begin(), table.end(), expected.begin(), expected.end(),
                         std::back_inserter(only_table));
    EXPECT_TRUE(only_expected.empty()) << "msg_types in raw-XML walk but NOT in .def table (table is stale)";
    EXPECT_TRUE(only_table.empty()) << "msg_types in .def table but NOT in raw-XML walk (table is stale)";
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
    std::set<std::string> const registry = parse_registry_msgtypes(FIXPP_CODEGEN_V50SP2_BUILDERS_HPP);
    EXPECT_EQ(registry, expected);
}

TEST(BuilderCompleteness077V50sp2, BuildFnSignaturesMatchRawXmlWalk) {
    using namespace fixpp_test::builder_completeness;
    std::set<std::string> const expected =
        legacy_expected_msgtypes(std::string(FIXPP_DICT_DATA_DIR) + "/FIX50SP2.xml");
    std::set<std::string> const build_idents =
        parse_build_fn_identifiers(std::filesystem::path(FIXPP_CODEGEN_V50SP2_BUILDERS_HPP).parent_path().string());

    std::set<std::string> translated_msgtypes;
    for (auto const& ident : build_idents) {
        auto const it = std::find_if(kMsgtypeToIdent.begin(), kMsgtypeToIdent.end(),
                                      [&](auto const& p) { return p.second == ident; });
        ASSERT_NE(it, kMsgtypeToIdent.end())
            << "build_" << ident << "( found in " << FIXPP_CODEGEN_V50SP2_BUILDERS_HPP
            << " has no matching entry in the .def table -- an emitted builder outside the raw-XML scope";
        translated_msgtypes.emplace(it->first);
    }
    EXPECT_EQ(translated_msgtypes, expected);
}
