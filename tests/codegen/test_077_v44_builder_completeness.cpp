// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/codegen/test_077_v44_builder_completeness.cpp
//
// 077-builder-args-dedup T023 [US4] -- v44 builder-completeness census
// (specs/077-builder-args-dedup/tasks.md T023; contracts/
// builder-completeness.md C1-C4; data-model.md Entity 6). See
// builder_completeness_common.hpp for the four-leg design and the
// non-circularity rationale (shared across the v44/v50sp2/vlatest siblings).
//
// This TU is the ONLY leg that needs a compile-time ODR-use: it #includes
// the generated fixpp::v44::Builders.hpp AND the committed
// generated/v44_builder_completeness_entries.def (83 (msgtype, identifier)
// pairs, produced once by a standalone raw-XML script -- see the .def
// file's own header) via the FIXPP_BC_ENTRY X-macro, taking the address of
// every expected `build_<Msg>`/`validate_<Msg>`. If T017's emission ever
// dropped one of these 83 entries, this FILE FAILS TO COMPILE -- that is
// the C2 "expected ⊆ actual" proof; there is no runtime assertion for it
// (the language's ODR-use requirement IS the gate). The .def is included a
// SECOND time (with FIXPP_BC_ENTRY redefined to stringize the identifier
// instead of taking its address) to build a runtime (msg_type, identifier)
// lookup table -- used only by the Gate-A round-3 residual-closure test
// below, never by the address-of leg itself.
//
// Anchors: specs/077-builder-args-dedup/tasks.md T023;
//          contracts/builder-completeness.md C1-C4.

#include <fixpp/v44/Builders.hpp>  // GENERATED -- build_<Msg>/validate_<Msg>/builder_registry

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
#ifndef FIXPP_CODEGEN_V44_BUILDERS_HPP
#error "FIXPP_CODEGEN_V44_BUILDERS_HPP must be set by CMake target_compile_definitions"
#endif

namespace {

using fixpp_test::builder_completeness::Entry;

// C2 -- compile-time address-of census. FIXPP_BC_ENTRY(msgtype, ident)
// expands to one Entry literal per line of the committed .def table.
#define FIXPP_BC_ENTRY(msgtype, ident)                                          \
    Entry{msgtype, reinterpret_cast<void const*>(&::fixpp::v44::build_##ident), \
          reinterpret_cast<void const*>(&::fixpp::v44::validate_##ident)},
std::vector<Entry> const kEntries = {
#include "generated/v44_builder_completeness_entries.def"
};
#undef FIXPP_BC_ENTRY

// Second inclusion of the SAME .def, stringizing the identifier instead of
// taking its address -- a runtime (msg_type, identifier) table for the
// residual-closure test only (never consulted by the address-of leg
// above).
#define FIXPP_BC_ENTRY(msgtype, ident) {msgtype, #ident},
std::vector<std::pair<std::string_view, std::string_view>> const kMsgtypeToIdent = {
#include "generated/v44_builder_completeness_entries.def"
};
#undef FIXPP_BC_ENTRY

}  // namespace

// C1/C3a -- exact-set equality between the independent raw-XML walk
// (research.md R4: v44 `all` = 85 app minus {BE,BF,BW,BX,BY} = 83) and the
// committed .def table's msg_type set. This is the staleness self-check
// (builder_completeness_common.hpp leg-2 note): a future dictionary bump
// that the .def table wasn't regenerated for fails HERE, loudly, not
// silently.
TEST(BuilderCompleteness077V44, DefTableMatchesRawXmlWalk) {
    using namespace fixpp_test::builder_completeness;
    std::set<std::string> const expected =
        legacy_expected_msgtypes(std::string(FIXPP_DICT_DATA_DIR) + "/FIX44.xml", {"BE", "BF", "BW", "BX", "BY"});
    EXPECT_EQ(expected.size(), 83U) << "raw-XML walk in-scope count";

    std::set<std::string> const table = def_msgtypes(kEntries);
    EXPECT_EQ(table.size(), 83U) << "committed .def table entry count";

    std::vector<std::string> only_expected, only_table;
    std::set_difference(expected.begin(), expected.end(), table.begin(), table.end(),
                         std::back_inserter(only_expected));
    std::set_difference(table.begin(), table.end(), expected.begin(), expected.end(),
                         std::back_inserter(only_table));
    EXPECT_TRUE(only_expected.empty()) << "msg_types in raw-XML walk but NOT in .def table (table is stale)";
    EXPECT_TRUE(only_table.empty()) << "msg_types in .def table but NOT in raw-XML walk (table is stale)";
}

// C2 -- every Entry's build_addr/validate_addr is non-null (the address-of
// itself already proves existence at compile time via ODR-use; this is a
// cheap sanity assertion that the array was actually populated, not an
// empty macro expansion).
TEST(BuilderCompleteness077V44, AllEntryPointsResolved) {
    ASSERT_EQ(kEntries.size(), 83U);
    for (auto const& e : kEntries) {
        EXPECT_NE(e.build_addr, nullptr) << "msg_type=" << e.msg_type;
        EXPECT_NE(e.validate_addr, nullptr) << "msg_type=" << e.msg_type;
    }
}

// C2 secondary / C3a "actual ⊆ expected" -- the emitted builder_registry
// array's msg_type set must equal expected(V) exactly (no emitted builder
// outside the raw-XML-derived scope).
TEST(BuilderCompleteness077V44, RegistryExactSetEqualsRawXmlWalk) {
    using namespace fixpp_test::builder_completeness;
    std::set<std::string> const expected =
        legacy_expected_msgtypes(std::string(FIXPP_DICT_DATA_DIR) + "/FIX44.xml", {"BE", "BF", "BW", "BX", "BY"});
    std::set<std::string> const registry = parse_registry_msgtypes(FIXPP_CODEGEN_V44_BUILDERS_HPP);
    EXPECT_EQ(registry, expected);
}

// Gate-A round-3 residual closure (tasks.md T023 note) -- an INDEPENDENT
// text scan for `build_<Msg>(` definitions (not sourced from the
// registry-array parse above) is translated, through the .def table's
// (msg_type, identifier) map, back to a msg_type set and compared against
// expected(V). Catches a co-emission split where the registry array and
// the build_<Msg> definitions diverged (emit_builders.cpp currently emits
// both from ONE loop -- an unproven invariant this test independently
// pins).
TEST(BuilderCompleteness077V44, BuildFnSignaturesMatchRawXmlWalk) {
    using namespace fixpp_test::builder_completeness;
    std::set<std::string> const expected =
        legacy_expected_msgtypes(std::string(FIXPP_DICT_DATA_DIR) + "/FIX44.xml", {"BE", "BF", "BW", "BX", "BY"});
    std::set<std::string> const build_idents = parse_build_fn_identifiers(FIXPP_CODEGEN_V44_BUILDERS_HPP);

    std::set<std::string> translated_msgtypes;
    for (auto const& ident : build_idents) {
        auto const it = std::find_if(kMsgtypeToIdent.begin(), kMsgtypeToIdent.end(),
                                      [&](auto const& p) { return p.second == ident; });
        ASSERT_NE(it, kMsgtypeToIdent.end())
            << "build_" << ident << "( found in " << FIXPP_CODEGEN_V44_BUILDERS_HPP
            << " has no matching entry in the .def table -- an emitted builder outside the raw-XML scope";
        translated_msgtypes.emplace(it->first);
    }
    EXPECT_EQ(translated_msgtypes, expected);
}
