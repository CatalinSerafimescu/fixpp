// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/overlay_equivalence_w2_test.cpp — W-2 of 098-dialect-overlay, the vendored
// bases. Moved whole out of overlay_equivalence_test.cpp, whose header describes the witnesses, so
// that one ctest entry holds this suite and stays within CI's per-test guard (103 tasks T161).

#include "overlay_equivalence_support.hpp"

namespace {

using namespace overlay_eq;

// ── W-2, the vendored bases ──────────────────────────────────────────────────

struct vendored_row {
    char const* name = nullptr;
    char const* base = nullptr;         // under dictionaries/
    char const* overlay = nullptr;      // under fixtures/098/real/
    char const* patch = nullptr;        // under fixtures/098/real/
    char const* seeded_hunk = nullptr;  // the hunk the seeded mismatch drops (its label)
    std::set<std::string_view> seeded_expect;
    fmt format = fmt::quickfix_xml;
};

std::vector<vendored_row> const& vendored_rows() {
    static std::vector<vendored_row> const rows{
        {.name = "FIX44",
         .base = "FIX44.xml",
         .overlay = "FIX44_overlay.xml",
         .patch = "FIX44.patch",
         .seeded_hunk = "field 40: value Z appended",
         .seeded_expect = {ids::kEnumValue, ids::kEnumCodes}},
        {.name = "FIX50SP2",
         .base = "FIX50SP2.xml",
         .overlay = "FIX50SP2_overlay.xml",
         .patch = "FIX50SP2.patch",
         .seeded_hunk = "field 40: value Z appended",
         .seeded_expect = {ids::kEnumValue, ids::kEnumCodes}},
        {.name = "FIXT11",
         .base = "FIXT11.xml",
         .overlay = "FIXT11_overlay.xml",
         .patch = "FIXT11.patch",
         .seeded_hunk = "header: Custom098HeaderTag (5020) appended to <header>",
         .seeded_expect = {ids::kMessageFields.tag, ids::kValid}},
        {.name = "Orchestra",
         .base = "orchestra/OrchestraFIXLatest.xml",
         .overlay = "Orchestra_overlay.xml",
         .patch = "Orchestra.patch",
         .seeded_hunk = "codeSet MsgTypeCodeSet (35): code U1 appended",
         .seeded_expect = {ids::kEnumValue, ids::kEnumCodes},
         .format = fmt::orchestra},
    };
    return rows;
}

class OverlayEquivalenceW2 : public ::testing::TestWithParam<std::size_t> {};

std::string patched_text(vendored_row const& row, char const* drop_label) {
    std::string const base = ot::read_file(ot::dict_dir() / row.base);
    auto parsed = ot::parse_patch(ot::read_file(ot::fixture_dir() / "real" / row.patch));
    EXPECT_TRUE(parsed.error.empty()) << row.name << ": " << parsed.error;
    if (drop_label != nullptr) {
        auto const before = parsed.hunks.size();
        std::erase_if(parsed.hunks, [&](ot::hunk const& h) { return h.label == drop_label; });
        EXPECT_EQ(parsed.hunks.size() + 1, before)
            << row.name << ": the seeded hunk '" << drop_label << "' must exist exactly once";
    }
    auto const a = ot::apply_patch(base, parsed.hunks);
    EXPECT_TRUE(a.error.empty()) << row.name << ": " << a.error;
    return a.text;
}

ov::xml_overlay_probe probe_vendored(vendored_row const& row) {
    std::filesystem::path const base = ot::dict_dir() / row.base;
    std::filesystem::path const overlay = ot::fixture_dir() / "real" / row.overlay;
    return row.format == fmt::orchestra ? ov::probe_orchestra_overlay(base, overlay, mr())
                                        : ov::probe_xml_overlay(base, overlay, mr());
}

TEST_P(OverlayEquivalenceW2, OverlayEqualsThePatchedBase) {
    vendored_row const& row = vendored_rows().at(GetParam());
    std::string const patched = patched_text(row, nullptr);
    ASSERT_FALSE(patched.empty()) << row.name << ": the patch did not apply";
    auto const p = probe_vendored(row);
    ot::expect_loaded_at_prediction(p, std::string{"W-2 "} + row.name);
    ASSERT_TRUE(p.dictionary.has_value()) << ot::describe(p);
    Dictionary const reference = load_text_as(row.format, patched);
    // NOLINTBEGIN(bugprone-unchecked-optional-access): the ASSERT_TRUE above checks it
    expect_equivalent(std::string{"W-2 "} + row.name,
                      compare_against(*p.dictionary, reference, patched, row.format));
    // NOLINTEND(bugprone-unchecked-optional-access)
}

TEST_P(OverlayEquivalenceW2, SeededMismatchWithOneHunkDroppedIsReported) {
    vendored_row const& row = vendored_rows().at(GetParam());
    std::string const seeded = patched_text(row, row.seeded_hunk);
    ASSERT_FALSE(seeded.empty()) << row.name << ": the patch did not apply";
    auto const p = probe_vendored(row);
    ASSERT_TRUE(p.dictionary.has_value()) << ot::describe(p);
    Dictionary const reference = load_text_as(row.format, seeded);
    // NOLINTBEGIN(bugprone-unchecked-optional-access): the ASSERT_TRUE above checks it
    expect_reports(std::string{"W-2 "} + row.name,
                   compare_against(*p.dictionary, reference, seeded, row.format),
                   row.seeded_expect);
    // NOLINTEND(bugprone-unchecked-optional-access)
}

INSTANTIATE_TEST_SUITE_P(Vendored, OverlayEquivalenceW2, ::testing::Values(0U, 1U, 2U, 3U),
                         [](auto const& info) {
                             return std::string{vendored_rows().at(info.param).name};
                         });

}  // namespace
