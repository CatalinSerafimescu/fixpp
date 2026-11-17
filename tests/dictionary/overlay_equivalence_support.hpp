// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/overlay_equivalence_support.hpp — test-only. The helpers the overlay
// equivalence cells of 098-dialect-overlay share (the comparison, its report, the loaders),
// moved out of
// overlay_equivalence_test.cpp when its slowest suites moved to files and ctest entries of
// their own (103 tasks T161). Each TU brings them in with `using namespace overlay_eq;`
// inside its anonymous namespace.
#pragma once

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/error.hpp>
#include <fixpp/dict/load_any.hpp>
#include <fixpp/dict/orchestra_loader.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <iostream>
#include <iterator>
#include <memory_resource>
#include <optional>
#include <pugixml.hpp>
#include <set>
#include <string>
#include <string_view>
#include <typeinfo>
#include <utility>
#include <vector>

#include "dictionary_equivalence.hpp"
#include "overlay_test_support.hpp"
#include "reused_tag_census.hpp"
#include "support/temp_dir.hpp"

namespace overlay_eq {

namespace eq = fixpp::dict::equivalence;
namespace ids = fixpp::dict::equivalence::id;
namespace ot = overlay_test;
namespace ov = fixpp::dict::detail::overlay;
using fixpp::dict::Dictionary;
using fixpp::dict::OrchestraLoader;
using fixpp::dict::unresolved_group_policy;
using fixpp::dict::XmlLoader;

// The format of a row's documents: it picks the loader, the seam and the census.
enum class fmt : std::uint8_t { quickfix_xml, orchestra };

inline std::pmr::memory_resource* mr() { return std::pmr::new_delete_resource(); }

// ── comparisons ──────────────────────────────────────────────────────────────

// `a` (the overlay side) against `b` (the reference), each checked against the
// census of the reference text.
inline eq::comparison_report compare_against(Dictionary const& a, Dictionary const& b,
                                             std::string_view reference_text,
                                             fmt format = fmt::quickfix_xml) {
    eq::raw_census const census = format == fmt::orchestra
                                      ? eq::census_from_orchestra(reference_text)
                                      : eq::census_from_quickfix_xml(reference_text);
    return eq::compare(eq::side{.dict = a, .census = census},
                       eq::side{.dict = b, .census = census});
}

inline void report_counts(std::string const& row, eq::comparison_report const& r) {
    std::cout << "[098 " << row << "] census compared " << r.census_compared << '\n';
}

inline void expect_equivalent(std::string const& row, eq::comparison_report const& r) {
    report_counts(row, r);
    EXPECT_TRUE(r.differences.empty()) << row << ":\n" << r.describe();
    EXPECT_TRUE(r.invariant_violations.empty()) << row << ":\n" << r.describe();
}

inline void expect_reports(std::string const& row, eq::comparison_report const& r,
                           std::set<std::string_view> const& expected) {
    auto const got = r.reported_entries();
    std::cout << "[098 " << row << " seeded mismatch] reported stores:";
    for (auto const e : got) {
        std::cout << " [" << e << "]";
    }
    std::cout << '\n';
    for (auto const e : expected) {
        EXPECT_TRUE(got.contains(e)) << row << ": the seeded mismatch must report [" << e << "]:\n"
                                     << r.describe();
    }
}

// The text with the one line equal to `anchor` removed; the anchor must match
// exactly once.
inline std::string without_line(std::string_view text, std::string_view anchor) {
    auto lines = ot::lines_of(text);
    auto const hits = ot::find_line(lines, anchor);
    EXPECT_EQ(hits.size(), 1U) << "anchor '" << anchor << "' must match exactly once";
    if (hits.size() == 1) {
        lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(hits.front()));
    }
    return ot::join_lines(lines);
}

inline Dictionary load_text(std::string_view text,
                            unresolved_group_policy policy = unresolved_group_policy::fail_closed) {
    return XmlLoader{}.load_from_string(text, mr(), policy);
}

inline Dictionary load_orch_text(
    std::string_view text, unresolved_group_policy policy = unresolved_group_policy::fail_closed) {
    return OrchestraLoader{}.load_from_string(text, mr(), policy);
}

inline Dictionary load_text_as(fmt format, std::string_view text) {
    return format == fmt::orchestra ? load_orch_text(text) : load_text(text);
}

}  // namespace overlay_eq
