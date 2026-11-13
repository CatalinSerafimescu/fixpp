// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/test_102_overlay_ids.cpp — 102-dict-width-multichar-type, spec US1.3, US1.5,
// FR-001b (research D4 "Overlay crossing", the Orchestra component overlay row, rows (d), (e)
// and (f), "the alias-pair rule"; tasks T014).
//
// Orchestra overlays whose component and group ids pass 65535, loaded through research R-5's
// read seam (`probe_orchestra_overlay_from_string`), so each loading row asserts the build's
// step counter equals the pre-walk's prediction. Each base declares, for the overlay graph
// build's `component_by_id` and `group_by_id`, a high id and its alias (high id - 65536), both
// referenced from a message with distinct members, and each row asserts both members of the
// pair keep their own definition. Every row loads its own base and overlay.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/error.hpp>
#include <fixpp/dict/field_ref.hpp>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <typeinfo>
#include <vector>

#include "overlay_merge.hpp"
#include "overlay_test_support.hpp"
#include "support/dict_width_fixtures_102.hpp"

namespace {

namespace ot = overlay_test;
namespace ov = fixpp::dict::detail::overlay;
namespace w = fixpp::test102::width;
using fixpp::dict::Dictionary;
using fixpp::dict::FieldRef;

std::pmr::memory_resource* mr() { return std::pmr::new_delete_resource(); }

ov::xml_overlay_probe oprobe(std::string_view base, std::string_view overlay) {
    return ov::probe_orchestra_overlay_from_string(base, overlay, mr());
}

std::vector<std::uint16_t> tags_of(std::span<FieldRef const> run) {
    std::vector<std::uint16_t> out;
    for (auto const& fr : run) {
        out.push_back(fr.tag);
    }
    return out;
}

bool has_tag(std::span<FieldRef const> run, std::uint16_t tag) {
    for (auto const& fr : run) {
        if (fr.tag == tag) {
            return true;
        }
    }
    return false;
}

bool has_tag(std::span<std::uint16_t const> run, std::uint16_t tag) {
    for (auto const t : run) {
        if (t == tag) {
            return true;
        }
    }
    return false;
}

// A small Orchestra base: the header (1024), then `components`, `groups`, `fields` and message
// D's structure after the header reference.
std::string small_base(std::string const& fields, std::string const& components,
                       std::string const& groups, std::string const& d_body) {
    return std::string{w::kOrchOpen} + "<fixr:fields>\n" +
           w::o_field_def(8, "BeginString", "String") + w::o_field_def(9, "BodyLength", "Length") +
           w::o_field_def(10, "CheckSum", "String") + w::o_field_def(35, "MsgType", "String") +
           fields + "</fixr:fields>\n<fixr:components>\n" +
           w::o_comp(
               1024, "StandardHeader",
               w::o_fref(8, "required") + w::o_fref(9, "required") + w::o_fref(35, "required")) +
           components + "</fixr:components>\n<fixr:groups>\n" + groups +
           "</fixr:groups>\n<fixr:messages>\n" +
           w::o_msg(1, "NewOrderSingle", "D", w::o_cref(1024, "required") + d_body) +
           "</fixr:messages>\n" + std::string{w::kOrchClose};
}

std::string overlay_of(std::string const& body) {
    return std::string{w::kOrchOpen} + body + std::string{w::kOrchClose};
}

}  // namespace

// Research D4 "Overlay crossing", Orchestra (generator (d)): a base just under 65,536
// components plus an overlay at `kMaxNewComponents` new components.
TEST(OverlayIds102, OrchestraOverlayCrossingTheWrapPointGivesNewComponentsTheirIdentity) {
    auto const f = w::make_orch_crossing_fixture(ov::kMaxNewComponents);
    ASSERT_EQ(f.precondition_failure("fixr:component"), "");
    auto const p = oprobe(f.base, f.overlay);
    ot::expect_loaded_at_prediction(p, "Orchestra overlay crossing");
    ASSERT_TRUE(p.dictionary.has_value()) << ot::describe(p);
    // NOLINTBEGIN(bugprone-unchecked-optional-access): the ASSERT_TRUE above checks it
    for (auto const& e : f.added) {
        auto const cr = p.dictionary->component(e.name);
        ASSERT_TRUE(cr.has_value()) << e.name;
        std::uint64_t const id = cr->component_id;
        EXPECT_EQ(id, e.id) << e.name;
        std::vector<w::member> got;
        for (auto const& fr : p.dictionary->component_fields(e.name)) {
            got.push_back({.tag = fr.tag, .component_index = fr.component_index});
        }
        EXPECT_TRUE(got == e.members) << e.name << ": members are not its own";
    }
    // NOLINTEND(bugprone-unchecked-optional-access)
}

// The component overlay row (generator (f)): an overlay adds a field to OvHigh (id past
// 65535); its alias OvAlias keeps its own members.
TEST(OverlayIds102, OverlayAddsAFieldToABaseComponentPast65535) {
    auto const& f = w::component_overlay_fixture_text();
    ASSERT_EQ(f.precondition_failure(), "");
    auto const p = oprobe(f.base, f.overlay);
    ot::expect_loaded_at_prediction(p, "component overlay");
    ASSERT_TRUE(p.dictionary.has_value()) << ot::describe(p);
    // NOLINTBEGIN(bugprone-unchecked-optional-access): the ASSERT_TRUE above checks it
    EXPECT_EQ(tags_of(p.dictionary->component_fields(f.high_name)),
              (std::vector<std::uint16_t>{f.high_member, f.added}));
    EXPECT_EQ(tags_of(p.dictionary->component_fields(f.alias_name)),
              (std::vector<std::uint16_t>{f.alias_member}));
    EXPECT_TRUE(has_tag(p.dictionary->message_fields("D"), f.added));
    // NOLINTEND(bugprone-unchecked-optional-access)
}

// Row (d): an overlay adds a field to a base group GH (id past 65535, NumInGroup 9711) whose
// alias group GHa (NumInGroup 9713) also exists.
TEST(OverlayIds102, RowDOverlayAddsAFieldToABaseGroupPast65535) {
    constexpr std::uint32_t kGH = 70030;
    constexpr std::uint32_t kGHa = kGH - w::kWrap;
    std::string const base = small_base(
        w::o_field_def(9711, "NoGH", "NumInGroup") + w::o_field_def(9712, "GHM", "String") +
            w::o_field_def(9713, "NoGHa", "NumInGroup") + w::o_field_def(9714, "GHaM", "String"),
        {},
        w::o_group(kGH, "GH", 9711, w::o_fref(9712)) +
            w::o_group(kGHa, "GHa", 9713, w::o_fref(9714)),
        w::o_gref(kGH) + w::o_gref(kGHa));
    std::string const overlay =
        overlay_of("<fixr:fields>\n" + w::o_field_def(9715, "GHAdded", "String") +
                   "</fixr:fields>\n<fixr:groups>\n" +
                   w::o_group(kGH, "GH", 9711, w::o_fref(9715)) + "</fixr:groups>\n");
    ASSERT_EQ(w::pairs_failure(base, {{"fixr:group", "fixr:groupRef", kGH}}), "");
    auto const p = oprobe(base, overlay);
    ot::expect_loaded_at_prediction(p, "row (d)");
    ASSERT_TRUE(p.dictionary.has_value()) << ot::describe(p);
    // NOLINTBEGIN(bugprone-unchecked-optional-access): the ASSERT_TRUE above checks it
    EXPECT_EQ(tags_of(p.dictionary->group_fields(9711)), (std::vector<std::uint16_t>{9712, 9715}));
    EXPECT_EQ(tags_of(p.dictionary->group_fields(9713)), (std::vector<std::uint16_t>{9714}));
    // NOLINTEND(bugprone-unchecked-optional-access)
}

// Row (e): an overlay restates, in place, message D's componentRef and groupRef whose ids pass
// 65535, once identically and once with presence="required". The restatement is matched, not
// appended: the merged expansion adds no step to the base's, and the promotion applies.
namespace {

constexpr std::uint32_t kCH = 70040;
constexpr std::uint32_t kGE = 70041;

std::string row_e_base() {
    return small_base(
        w::o_field_def(9721, "CHF", "String") + w::o_field_def(9722, "CHaF", "String") +
            w::o_field_def(9723, "NoGE", "NumInGroup") + w::o_field_def(9724, "GEM", "String") +
            w::o_field_def(9725, "NoGEa", "NumInGroup") + w::o_field_def(9726, "GEaM", "String"),
        w::o_comp(kCH, "CH", w::o_fref(9721, "required")) +
            w::o_comp(kCH - w::kWrap, "CHa", w::o_fref(9722, "required")),
        w::o_group(kGE, "GE", 9723, w::o_fref(9724)) +
            w::o_group(kGE - w::kWrap, "GEa", 9725, w::o_fref(9726)),
        w::o_cref(kCH) + w::o_cref(kCH - w::kWrap) + w::o_gref(kGE) + w::o_gref(kGE - w::kWrap));
}

std::string row_e_overlay(std::string_view presence) {
    return overlay_of(
        "<fixr:messages>\n" +
        w::o_msg(1, "NewOrderSingle", "D", w::o_cref(kCH, presence) + w::o_gref(kGE, presence)) +
        "</fixr:messages>\n");
}

}  // namespace

TEST(OverlayIds102, RowEIdenticalRestatementOfRefsPast65535IsMatched) {
    std::string const base = row_e_base();
    ASSERT_EQ(w::pairs_failure(base, {{"fixr:component", "fixr:componentRef", kCH},
                                      {"fixr:group", "fixr:groupRef", kGE}}),
              "");
    auto const p = oprobe(base, row_e_overlay({}));
    ot::expect_loaded_at_prediction(p, "row (e) identical");
    ASSERT_TRUE(p.dictionary.has_value()) << ot::describe(p);
    ASSERT_TRUE(p.guards.has_value());
    // NOLINTBEGIN(bugprone-unchecked-optional-access): the ASSERT_TRUEs above check them
    EXPECT_EQ(p.guards->merged_total, p.guards->base_total)
        << "the restated refs were appended, not matched";
    auto const required = p.dictionary->required_fields("D");
    EXPECT_FALSE(has_tag(required, 9721)) << "an identical restatement promotes nothing";
    EXPECT_FALSE(has_tag(required, 9723));
    // NOLINTEND(bugprone-unchecked-optional-access)
}

TEST(OverlayIds102, RowERequiredRestatementOfRefsPast65535IsMatchedAndPromoted) {
    auto const p = oprobe(row_e_base(), row_e_overlay("required"));
    ot::expect_loaded_at_prediction(p, "row (e) required");
    ASSERT_TRUE(p.dictionary.has_value()) << ot::describe(p);
    ASSERT_TRUE(p.guards.has_value());
    // NOLINTBEGIN(bugprone-unchecked-optional-access): the ASSERT_TRUEs above check them
    EXPECT_EQ(p.guards->merged_total, p.guards->base_total)
        << "the restated refs were appended, not matched";
    auto const required = p.dictionary->required_fields("D");
    EXPECT_TRUE(has_tag(required, 9721)) << "the componentRef past 65535 was not promoted";
    EXPECT_TRUE(has_tag(required, 9723)) << "the groupRef past 65535 was not promoted";
    EXPECT_FALSE(has_tag(required, 9722)) << "the alias componentRef was promoted";
    EXPECT_FALSE(has_tag(required, 9725)) << "the alias groupRef was promoted";
    // NOLINTEND(bugprone-unchecked-optional-access)
}

// Row (f): a cycle closed through a componentRef past 65535 is the pre-walk's refusal, naming
// the components past 65535. Base: CY (id 70050) and its alias CYa, both referenced from D.
TEST(OverlayIds102, RowFCycleThroughAComponentRefPast65535IsThePreWalksRefusal) {
    constexpr std::uint32_t kCY = 70050;
    constexpr std::uint32_t kCZ = 70051;
    std::string const base = small_base(
        w::o_field_def(9731, "CYF", "String") + w::o_field_def(9732, "CYaF", "String"),
        w::o_comp(kCY, "CY", w::o_fref(9731)) + w::o_comp(kCY - w::kWrap, "CYa", w::o_fref(9732)),
        {}, w::o_cref(kCY) + w::o_cref(kCY - w::kWrap));
    std::string const overlay =
        overlay_of("<fixr:components>\n" + w::o_comp(kCY, "CY", w::o_cref(kCZ)) +
                   w::o_comp(kCZ, "CZ", w::o_cref(kCY)) + "</fixr:components>\n");
    ASSERT_EQ(w::pairs_failure(base, {{"fixr:component", "fixr:componentRef", kCY}}), "");
    auto const p = oprobe(base, overlay);
    ot::refusal const r = ot::refusal_of(p);
    ASSERT_TRUE(r.refused) << "expected the pre-walk's cycle refusal; " << ot::describe(p);
    EXPECT_EQ(r.type, typeid(fixpp::dict::orchestra_parse_error).name()) << ot::describe(p);
    for (std::string_view const part :
         {std::string_view{"component reference cycle"}, std::string_view{"component 70050 CY"},
          std::string_view{"component 70051 CZ"}}) {
        EXPECT_NE(r.what.find(part), std::string::npos)
            << "the message must name '" << part << "': " << r.what;
    }
}
