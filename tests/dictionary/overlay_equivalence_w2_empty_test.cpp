// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/overlay_equivalence_w2_empty_test.cpp — W-2 of 098-dialect-overlay, the empty
// overlay on every runtime base. Moved whole out of overlay_equivalence_test.cpp, whose header
// describes the witnesses, so that one ctest entry holds this suite and stays within CI's per-test
// guard (103 tasks T161).

#include "overlay_equivalence_support.hpp"

namespace {

using namespace overlay_eq;

// ── W-2, the empty overlay on every runtime base ─────────────────────────────

// An empty `<fix>` overlay naming the base's version.
std::string empty_overlay_for(std::string_view base_text) {
    pugi::xml_document doc;
    EXPECT_TRUE(doc.load_buffer(base_text.data(), base_text.size()));
    pugi::xml_node const r = doc.child("fix");
    return std::string{"<fix type='"} + r.attribute("type").as_string("FIX") + "' major='" +
           r.attribute("major").as_string("") + "' minor='" + r.attribute("minor").as_string("") +
           "' servicepack='" + r.attribute("servicepack").as_string("0") + "'/>\n";
}

// The seeded removal (contracts W-2): the field number of the first `<field>`
// definition in document order that lists two or more `<value>`s. Empty when none.
std::string first_multi_valued_field(std::string_view base_text) {
    pugi::xml_document doc;
    EXPECT_TRUE(doc.load_buffer(base_text.data(), base_text.size()));
    for (pugi::xml_node f = doc.child("fix").child("fields").child("field"); f;
         f = f.next_sibling("field")) {
        pugi::xml_node const v = f.child("value");
        if (v && v.next_sibling("value")) {
            return f.attribute("number").as_string("");
        }
    }
    return {};
}

// The base text without that definition's last `<value>`. The anchor is the
// definition's opening `<field number='N'` line, which must match exactly once;
// the removed line is the value line right before the definition's `</field>`.
// Empty on failure.
std::string without_last_value(std::string_view base_text, std::string const& number) {
    auto lines = ot::lines_of(base_text);
    auto const trimmed = [](std::string const& l) {
        auto const lead = l.find_first_not_of(" \t");
        return lead == std::string::npos ? std::string_view{} : std::string_view{l}.substr(lead);
    };
    std::vector<std::size_t> hits;
    for (char const q : {'\'', '"'}) {
        std::string const head = std::string{"<field number="} + q + number + q;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            std::string_view const t = trimmed(lines[i]);
            if (t.starts_with(head) && t.size() > head.size() &&
                (t[head.size()] == ' ' || t[head.size()] == '>')) {
                hits.push_back(i);
            }
        }
    }
    EXPECT_EQ(hits.size(), 1U) << "the anchor of field " << number << " must match exactly once";
    if (hits.size() != 1) {
        return {};
    }
    std::size_t close = hits.front() + 1;
    while (close < lines.size() && trimmed(lines[close]) != "</field>") {
        ++close;
    }
    EXPECT_LT(close, lines.size()) << "field " << number << " has no closing line";
    if (close == lines.size()) {
        return {};
    }
    std::string_view const last = trimmed(lines[close - 1]);
    EXPECT_TRUE(last.starts_with("<value ") && last.ends_with("/>"))
        << "field " << number << ": the line before </field> is not one value element: " << last;
    if (!last.starts_with("<value ") || !last.ends_with("/>")) {
        return {};
    }
    lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(close) - 1);
    return ot::join_lines(lines);
}

class OverlayEquivalenceW2Empty : public ::testing::TestWithParam<std::string_view> {};

TEST_P(OverlayEquivalenceW2Empty, EmptyOverlayEqualsTheBase) {
    std::filesystem::path const base_path = ot::dict_dir() / std::string{GetParam()};
    std::string const base_text = ot::read_file(base_path);
    ot::temp_file const overlay{empty_overlay_for(base_text)};
    auto const p = ov::probe_xml_overlay(base_path, overlay.path(), mr());
    std::string const row = "W-2 empty " + std::string{GetParam()};
    ot::expect_loaded_at_prediction(p, row);
    ASSERT_TRUE(p.dictionary.has_value()) << ot::describe(p);
    Dictionary const reference = XmlLoader{}.load(base_path, mr());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): the ASSERT_TRUE above checks it
    expect_equivalent(row, compare_against(*p.dictionary, reference, base_text));
}

// Seeded mismatch: `load(base)` against a copy of the base without one enum value
// (the recipe above); the comparator must report the enum stores.
TEST_P(OverlayEquivalenceW2Empty, SeededMismatchWithoutOneEnumValueIsReported) {
    std::filesystem::path const base_path = ot::dict_dir() / std::string{GetParam()};
    std::string const base_text = ot::read_file(base_path);
    std::string const number = first_multi_valued_field(base_text);
    ASSERT_FALSE(number.empty()) << GetParam() << " has no field definition with two values";
    std::string const copy = without_last_value(base_text, number);
    ASSERT_FALSE(copy.empty());
    Dictionary const base = XmlLoader{}.load(base_path, mr());
    std::optional<Dictionary> seeded;
    ASSERT_NO_THROW(seeded.emplace(load_text(copy)))
        << "the copy without one value of field " << number << " must load";
    eq::raw_census const census_base = eq::census_from_quickfix_xml(base_text);
    eq::raw_census const census_copy = eq::census_from_quickfix_xml(copy);
    // NOLINTBEGIN(bugprone-unchecked-optional-access): ASSERT_NO_THROW above set it
    auto const r = eq::compare(eq::side{.dict = base, .census = census_base},
                               eq::side{.dict = *seeded, .census = census_copy});
    // NOLINTEND(bugprone-unchecked-optional-access)
    std::cout << "[098 W-2 empty " << GetParam() << "] seeded removal: the last value of field "
              << number << '\n';
    expect_reports("W-2 empty " + std::string{GetParam()}, r, {ids::kEnumValue, ids::kEnumCodes});
}

INSTANTIATE_TEST_SUITE_P(RuntimeDicts, OverlayEquivalenceW2Empty,
                         ::testing::ValuesIn(fixpp_test_support::kRuntimeDicts),
                         [](auto const& info) {
                             std::string n{info.param};
                             std::erase(n, '.');
                             return n;
                         });

}  // namespace
