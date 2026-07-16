// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/codegen/test_077_v42_vt11_completeness_and_c4.cpp
//
// 077-builder-args-dedup T026 [US4] -- vt11 AND v42 completeness (C3c) +
// the C4 structural-key safety pin (specs/077-builder-args-dedup/tasks.md
// T026; contracts/builder-completeness.md C3c/C4). Sibling of the
// v44/v50sp2/vlatest census TUs (test_077_<ns>_builder_completeness.cpp) --
// see builder_completeness_common.hpp for the shared raw-XML-walk
// machinery. This TU does NOT #include any Builders.hpp (cheap,
// isolation-safe, ungated -- mirrors test_077_builder_no_emit.cpp's
// filesystem-existence pattern).
//
// vt11 vs v42 asymmetry (research.md R4; tasks.md T026 note):
//   - vt11 is genuinely admin-only: the raw-XML walk over FIXT11.xml itself
//     returns an EMPTY application-message set (8/8 messages are
//     msgcat="admin") -- expected(vt11)=∅ is DERIVED, not asserted by fiat.
//   - v42 is NOT admin-only: the raw-XML walk over FIX42.xml returns 39
//     application messages (msgcat="app"). expected(v42)=∅ for THIS
//     census is a POLICY exclusion (issue #196 / L-063-1: FIX 4.2 types
//     NumInGroup as legacy INT, so emit_builders would materialize zero
//     typed groups for it and silently omit required='Y' groups -- see
//     main.cpp:93-111's driver-level `if (ir.ns != "v42")` skip). This test
//     asserts BOTH numbers (39 real app messages, 0 builder-completeness
//     expectation) so the distinction is pinned, not conflated.
//
// C4 (structural-key safety pin): a future dictionary bump that introduces
// a new structural variant of an existing shared `no_tag` group is absorbed
// automatically as a new ordinaled `G_<no_tag>_<ordinal>Args` struct (G1a
// naming, emit_builders.cpp `assign_plan_names`) rather than silently
// mis-sharing one plan across two structurally different call sites. This
// TU pins the naming INVARIANT is real and already firing in the shipped
// v50sp2 golden (558 distinct plans, research.md R3): for every no_tag that
// has more than one distinct structural signature, the ordinaled struct
// bodies are texually DISTINCT (proving the ordinal split corresponds to a
// genuine structural difference, not spurious duplication), and no no_tag
// has BOTH a bare `G_<no_tag>Args` name AND an ordinaled
// `G_<no_tag>_<n>Args` variant (G1a: bare name is reserved for the
// exactly-one-signature case).
//
// Anchors: specs/077-builder-args-dedup/tasks.md T026;
//          contracts/builder-completeness.md C3c/C4; research.md R2/R3/R4.

#include "builder_completeness_common.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>

#ifndef FIXPP_DICT_DATA_DIR
#error "FIXPP_DICT_DATA_DIR must be set by CMake target_compile_definitions"
#endif
#ifndef FIXPP_CODEGEN_V42_BUILDERS_HPP
#error "FIXPP_CODEGEN_V42_BUILDERS_HPP must be set by CMake target_compile_definitions"
#endif
#ifndef FIXPP_CODEGEN_VT11_BUILDERS_HPP
#error "FIXPP_CODEGEN_VT11_BUILDERS_HPP must be set by CMake target_compile_definitions"
#endif
#ifndef FIXPP_CODEGEN_V50SP2_BUILDERS_HPP
#error "FIXPP_CODEGEN_V50SP2_BUILDERS_HPP must be set by CMake target_compile_definitions"
#endif

// C3c -- vt11: expected = ∅ (genuinely admin-only), no Builders.hpp emitted.
TEST(BuilderCompleteness077Vt11V42, Vt11ExpectedEmptyNoFileEmitted) {
    using namespace fixpp_test::builder_completeness;
    std::set<std::string> const expected = legacy_expected_msgtypes(std::string(FIXPP_DICT_DATA_DIR) + "/FIXT11.xml");
    EXPECT_TRUE(expected.empty()) << "vt11 raw-XML walk should have zero application messages";
    EXPECT_FALSE(std::filesystem::exists(FIXPP_CODEGEN_VT11_BUILDERS_HPP))
        << "vt11 is admin-only; no Builders.hpp should be emitted for it";
}

// C3c-analog (T026 override, issue #196 / L-063-1) -- v42: 39 real
// application messages exist (raw-XML walk, no `in_scope` exclusion
// applied), but the builder-completeness census's expected(v42) is DEFINED
// as ∅ because v42 is a non-builder-bearing version by policy. No
// Builders.hpp is emitted for it.
TEST(BuilderCompleteness077Vt11V42, V42HasAppMessagesButIsNonBuilderBearingByPolicy) {
    using namespace fixpp_test::builder_completeness;
    std::set<std::string> const raw_app_messages =
        legacy_expected_msgtypes(std::string(FIXPP_DICT_DATA_DIR) + "/FIX42.xml");
    EXPECT_EQ(raw_app_messages.size(), 39U)
        << "v42 DOES have application messages (unlike vt11) -- the ∅ census "
           "expectation below is a policy exclusion, not a raw-XML fact";

    // Policy: v42 builder-completeness expected set is defined as empty
    // (issue #196 / L-063-1), independent of the nonzero raw app count
    // above.
    std::set<std::string> const expected_for_census;
    EXPECT_TRUE(expected_for_census.empty());
    EXPECT_FALSE(std::filesystem::exists(FIXPP_CODEGEN_V42_BUILDERS_HPP))
        << "v42 is DESCOPED (issue #196); no Builders.hpp should be emitted for it "
           "despite having application messages";
}

// C4 -- structural-key safety pin, verified against the shipped v50sp2
// golden (558 distinct plans, 75 with an ordinal suffix -- research.md R3).
TEST(BuilderCompleteness077C4, OrdinalVariantsAreStructurallyDistinctAndBareNamesAreExclusive) {
    std::string const text = fixpp_test::builder_completeness::read_file(FIXPP_CODEGEN_V50SP2_BUILDERS_HPP);

    // Collect every `struct G_<no_tag>[_<ordinal>]Args { ... };` body,
    // keyed by its full struct name.
    static std::regex const kStructStart(R"(^struct (G_\d+(?:_\d+)?Args) \{$)");
    std::istringstream lines(text);
    std::string line;
    std::map<std::string, std::string> body_by_name;
    std::string cur_name;
    std::string cur_body;
    bool in_struct = false;
    while (std::getline(lines, line)) {
        if (!in_struct) {
            // Cheap prefilter: only `struct G_...` lines can match kStructStart,
            // so skip the multi-MB build_<Msg>/registry tail without invoking
            // the regex (~558 matches instead of ~1M). Mirrors the idiom in
            // test_077_builder_dedup_count.cpp.
            if (line.rfind("struct G_", 0) != 0) {
                continue;
            }
            std::smatch m;
            if (std::regex_match(line, m, kStructStart)) {
                cur_name = m[1].str();
                cur_body.clear();
                in_struct = true;
            }
        } else if (line == "};") {
            body_by_name.emplace(cur_name, cur_body);
            in_struct = false;
        } else {
            cur_body += line;
            cur_body += '\n';
        }
    }
    ASSERT_FALSE(body_by_name.empty());

    // Group by no_tag (the part between "G_" and either "_<ordinal>Args" or
    // "Args").
    static std::regex const kBareRe(R"(^G_(\d+)Args$)");
    static std::regex const kOrdRe(R"(^G_(\d+)_(\d+)Args$)");
    std::map<std::string, std::set<std::string>> bare_by_no_tag;      // no_tag -> {struct name} (0 or 1)
    std::map<std::string, std::vector<std::string>> ordinals_by_no_tag;  // no_tag -> {struct names}
    for (auto const& [name, body] : body_by_name) {
        std::smatch m;
        if (std::regex_match(name, m, kOrdRe)) {
            ordinals_by_no_tag[m[1].str()].push_back(name);
        } else if (std::regex_match(name, m, kBareRe)) {
            bare_by_no_tag[m[1].str()].insert(name);
        }
    }

    // G1a: bare name and ordinaled variants are mutually exclusive per
    // no_tag.
    for (auto const& [no_tag, ords] : ordinals_by_no_tag) {
        EXPECT_EQ(bare_by_no_tag.count(no_tag), 0U)
            << "no_tag=" << no_tag << " has BOTH a bare G_" << no_tag
            << "Args name and ordinaled variants -- G1a naming violation";
    }

    // Ordinaled variants of the same no_tag must be textually DISTINCT
    // bodies (the ordinal split is real, not spurious duplication).
    for (auto const& [no_tag, names] : ordinals_by_no_tag) {
        ASSERT_GE(names.size(), 2U) << "no_tag=" << no_tag << " has a lone ordinal variant (should be >= 2)";
        for (std::size_t i = 0; i < names.size(); ++i) {
            for (std::size_t j = i + 1; j < names.size(); ++j) {
                EXPECT_NE(body_by_name.at(names[i]), body_by_name.at(names[j]))
                    << "no_tag=" << no_tag << " ordinal variants " << names[i] << " and " << names[j]
                    << " have byte-identical bodies -- spurious ordinal split";
            }
        }
    }

    EXPECT_GT(ordinals_by_no_tag.size(), 0U) << "sanity: v50sp2 should have at least one multi-signature no_tag";
}
