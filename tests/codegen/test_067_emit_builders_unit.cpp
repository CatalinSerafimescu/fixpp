// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/codegen/test_067_emit_builders_unit.cpp
//
// 067-codegen-writer-emitter — Phase 2 (T007/T009) emitter unit tests.
// Links the codegen tool's ir.cpp directly (not via generated headers) to
// inspect MessageIR.group_order pre-emission.
//
// T007 — the RC#7 `group_order` discriminating pins (research.md R9):
//   (a) W (MarketDataSnapshotFullRefresh) NoMDEntries(268) delimiter is
//       MDEntryType(269); X (MarketDataIncrementalRefresh) NoMDEntries(268)
//       delimiter is MDUpdateAction(279) — SAME no_tag, DIFFERENT per-message
//       delimiter (NOT tag-sorted 269 for both).
//   (b) E (NewOrderList) NoOrders(73) member order has Symbol(55) BEFORE
//       Side(54) — declaration order (NOT tag-sorted 54 55).
//
// T009 — the N3 `append_run` tag-dedup collapse census (plan.md N3):
// enumerate the 33 OFFICIAL messages for any tag appearing at >=2 "levels"
// (top-level == enclosing group_no_tag 0, or a group's no_tag) within ONE
// message that xml_loader.cpp's append_run tag-sort+tag-dedup
// (xml_loader.cpp:695-702) could collapse to a single surviving FieldRef.
// This census is INDEPENDENT of MessageIR.group_order (its own small raw-XML
// declaration walk over group_no_tag context only, not full member order) —
// deliberately not reusing the group_order walk, so a bug in one does not
// mask a bug in the other.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fstream>
#include <gtest/gtest.h>
#include <ios>
#include <memory_resource>
#include <pugixml.hpp>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ir.hpp"

#ifndef FIXPP_DICT_DATA_DIR
#error "FIXPP_DICT_DATA_DIR must be set by CMake target_compile_definitions"
#endif

namespace {

using fixpp::codegen::GroupOrderEntry;
using fixpp::codegen::MessageIR;
using fixpp::codegen::VersionIR;

// The exact-set of 33 OFFICIAL distinct MsgTypes (research.md R6).
constexpr std::array<std::string_view, 33> kOfficial33 = {
    "D", "E", "F", "G", "H", "8", "9", "q", "r", "AF", "AC", "t", "u", "V", "W", "X",
    "Y", "c", "d", "e", "f", "g", "h", "i", "b", "S", "R", "AG", "Z", "a", "J", "P", "AS"};

VersionIR const& fix44_ir() {
    static VersionIR const ir = [] {
        std::pmr::monotonic_buffer_resource arena;
        auto const path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / "FIX44.xml";
        return fixpp::codegen::build_ir(path, &arena);
    }();
    return ir;
}

MessageIR const& find_message(VersionIR const& ir, std::string_view msg_type) {
    for (auto const& m : ir.messages) {
        if (m.msg_type == msg_type) {
            return m;
        }
    }
    ADD_FAILURE() << "message not found: " << msg_type;
    static MessageIR const empty{};
    return empty;
}

// Finds the top-level (empty parent_path) group occurrence for `no_tag`.
GroupOrderEntry const* find_top_group(MessageIR const& m, std::uint16_t no_tag) {
    for (auto const& g : m.group_order) {
        if (g.no_tag == no_tag && g.parent_path.empty()) {
            return &g;
        }
    }
    return nullptr;
}

// Finds the group occurrence nested exactly under `parent_path` for `no_tag`
// (recursion depth pin — T004/T008 require every nesting depth captured).
GroupOrderEntry const* find_nested_group(MessageIR const& m,
                                         std::vector<std::uint16_t> const& parent_path,
                                         std::uint16_t no_tag) {
    for (auto const& g : m.group_order) {
        if (g.no_tag == no_tag && g.parent_path == parent_path) {
            return &g;
        }
    }
    return nullptr;
}

}  // namespace

// ---------------------------------------------------------------------------
// T007(a): W vs X NoMDEntries(268) delimiter divergence.
// ---------------------------------------------------------------------------
TEST(Group067GroupOrder, WMarketDataSnapshotDelimiterIs269) {
    auto const& w = find_message(fix44_ir(), "W");
    auto const* g = find_top_group(w, 268);
    ASSERT_NE(g, nullptr) << "W has no top-level NoMDEntries(268) group occurrence";
    EXPECT_EQ(g->delimiter_tag, 269) << "W's NoMDEntries delimiter must be MDEntryType(269)";
}

TEST(Group067GroupOrder, XMarketDataIncrementalDelimiterIs279) {
    auto const& x = find_message(fix44_ir(), "X");
    auto const* g = find_top_group(x, 268);
    ASSERT_NE(g, nullptr) << "X has no top-level NoMDEntries(268) group occurrence";
    EXPECT_EQ(g->delimiter_tag, 279) << "X's NoMDEntries delimiter must be MDUpdateAction(279)";
}

TEST(Group067GroupOrder, WAndXShareNoTagButDivergeOnDelimiter) {
    // The discriminating pin: ONE no_tag (268) must yield DISTINCT
    // per-message delimiters — a version-wide plan (or a delimiter derived
    // from the tag-sorted m.fields, which would surface 269 for BOTH since
    // 269 < 279) would collapse these to the same wrong answer for one side.
    auto const& w = find_message(fix44_ir(), "W");
    auto const& x = find_message(fix44_ir(), "X");
    auto const* gw = find_top_group(w, 268);
    auto const* gx = find_top_group(x, 268);
    ASSERT_NE(gw, nullptr);
    ASSERT_NE(gx, nullptr);
    EXPECT_NE(gw->delimiter_tag, gx->delimiter_tag);
    EXPECT_EQ(gw->delimiter_tag, 269);
    EXPECT_EQ(gx->delimiter_tag, 279);
}

// ---------------------------------------------------------------------------
// T007(b): E (NewOrderList) NoOrders(73) member order — Symbol(55) BEFORE
// Side(54), declaration order, NOT tag-sorted (54 55).
// ---------------------------------------------------------------------------
TEST(Group067GroupOrder, ENewOrderListSymbolBeforeSide) {
    auto const& e = find_message(fix44_ir(), "E");
    auto const* g = find_top_group(e, 73);
    ASSERT_NE(g, nullptr) << "E has no top-level NoOrders(73) group occurrence";

    auto index_of = [&](std::uint16_t tag) -> std::ptrdiff_t {
        for (std::size_t i = 0; i < g->members.size(); ++i) {
            if (g->members[i].tag == tag) {
                return static_cast<std::ptrdiff_t>(i);
            }
        }
        return -1;
    };
    auto const idx_symbol = index_of(55);
    auto const idx_side = index_of(54);
    ASSERT_GE(idx_symbol, 0) << "Symbol(55) not found in NoOrders member list";
    ASSERT_GE(idx_side, 0) << "Side(54) not found in NoOrders member list";
    EXPECT_LT(idx_symbol, idx_side)
        << "Symbol(55) must precede Side(54) in declaration order (tag-sort would invert this: "
           "54 55)";
}

// ---------------------------------------------------------------------------
// Nested-depth insurance (T004/T008 require recursion at EVERY nesting
// depth, not just top-level groups — the T007(a)/(b) pins above only cover
// depth 1). MassQuote (35=i): top-level NoQuoteSets(296), delimiter
// QuoteSetID(302); nested one level down, NoQuoteEntries(295), delimiter
// QuoteEntryID(299) (data-model.md T010 finding: both are Required).
// ---------------------------------------------------------------------------
TEST(Group067GroupOrder, MassQuoteNestedNoQuoteEntriesDelimiterIs299) {
    auto const& mq = find_message(fix44_ir(), "i");
    auto const* outer = find_top_group(mq, 296);
    ASSERT_NE(outer, nullptr) << "MassQuote has no top-level NoQuoteSets(296) group occurrence";
    EXPECT_EQ(outer->delimiter_tag, 302) << "NoQuoteSets delimiter must be QuoteSetID(302)";

    auto const* inner = find_nested_group(mq, {296}, 295);
    ASSERT_NE(inner, nullptr)
        << "MassQuote has no NoQuoteEntries(295) group occurrence nested under NoQuoteSets(296)";
    EXPECT_EQ(inner->delimiter_tag, 299)
        << "nested NoQuoteEntries delimiter must be QuoteEntryID(299)";
}

// ---------------------------------------------------------------------------
// T009: N3 append_run dedup-collapse census.
//
// SCOPE: this census walks each message's BODY only (rooted at the
// <message> node, same root as MessageIR.group_order/T008), NOT the merged
// header+body+trailer run xml_loader.cpp's append_run actually dedups
// (xml_loader.cpp:736-746 concatenates header, message, trailer before the
// single sort+unique pass). A tag shared between the STANDARD header/trailer
// and a message body/group is out of this census's detection range. Risk is
// low in practice (header/trailer fields are framing tags, already excluded
// from the write emitter's body-only tables per data-model.md §2.1), but the
// census result below is a body-level claim, not a full-run claim.
// ---------------------------------------------------------------------------
namespace {

struct ComponentIndex {
    std::unordered_map<std::string, pugi::xml_node> by_name;
};

ComponentIndex build_component_index(pugi::xml_node const& root) {
    ComponentIndex idx;
    for (auto const& c : root.child("components").children("component")) {
        idx.by_name.emplace(std::string{c.attribute("name").as_string("")}, c);
    }
    return idx;
}

// Raw declaration-order walk (NOT tag-sorted, NOT deduped — independent of
// ir.cpp's group_order production walk) recording, for every field/group tag
// reachable from `node`, the SET of enclosing group_no_tag contexts it
// appears in (0 == top-level of the message body). Mirrors
// xml_loader.cpp's expand_field_list enclosing_group_no_tag propagation
// (component refs transparent, group children get their own no_tag as the
// new enclosing context) WITHOUT the append_run tag-sort+dedup step that
// collapses same-tag multi-level entries in the shipped Dictionary.
// NOLINTNEXTLINE(misc-no-recursion)
void census_walk(pugi::xml_node const& node, std::uint16_t enclosing_group_no_tag,
                  ComponentIndex const& comps, fixpp::dict::Dictionary const& dict,
                  std::unordered_map<std::uint16_t, std::unordered_set<std::uint16_t>>&
                      levels_by_tag) {
    for (auto const& child : node.children()) {
        std::string_view const tn{child.name()};
        if (tn == "field") {
            auto const fname = std::string{child.attribute("name").as_string("")};
            if (auto const tag = dict.field_by_name(fname)) {
                levels_by_tag[*tag].insert(enclosing_group_no_tag);
            }
        } else if (tn == "component") {
            auto const cname = std::string{child.attribute("name").as_string("")};
            auto const it = comps.by_name.find(cname);
            if (it != comps.by_name.end()) {
                census_walk(it->second, enclosing_group_no_tag, comps, dict, levels_by_tag);
            }
        } else if (tn == "group") {
            auto const gname = std::string{child.attribute("name").as_string("")};
            auto const no_tag_opt = dict.field_by_name(gname);
            if (!no_tag_opt) {
                continue;
            }
            levels_by_tag[*no_tag_opt].insert(enclosing_group_no_tag);
            census_walk(child, *no_tag_opt, comps, dict, levels_by_tag);
        }
        // Ignore unknown child elements.
    }
}

}  // namespace

TEST(Group067Census, N3DedupCollapseCensus) {
    auto const path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / "FIX44.xml";

    std::pmr::monotonic_buffer_resource arena;
    fixpp::dict::XmlLoader loader;
    fixpp::dict::Dictionary const dict = loader.load(path, &arena);

    std::ifstream in(path, std::ios::binary);
    ASSERT_TRUE(static_cast<bool>(in)) << "cannot open " << path;
    pugi::xml_document doc;
    auto const parse_result = doc.load(in);
    ASSERT_TRUE(static_cast<bool>(parse_result)) << parse_result.description();

    auto const root = doc.child("fix");
    auto const comps = build_component_index(root);

    std::vector<std::string> collapsed;
    for (auto const& m : root.child("messages").children("message")) {
        std::string const msg_type{m.attribute("msgtype").as_string("")};
        bool const is_official =
            std::find(kOfficial33.begin(), kOfficial33.end(), msg_type) != kOfficial33.end();
        if (!is_official) {
            continue;
        }

        std::unordered_map<std::uint16_t, std::unordered_set<std::uint16_t>> levels_by_tag;
        census_walk(m, 0, comps, dict, levels_by_tag);

        for (auto const& [tag, levels] : levels_by_tag) {
            if (levels.size() >= 2) {
                collapsed.push_back(msg_type + ":" + std::to_string(tag));
            }
        }
    }

    // Confirm all 33 OFFICIAL messages were actually visited (a silently
    // missing message would make this census vacuously pass).
    std::size_t visited = 0;
    for (auto const& m : root.child("messages").children("message")) {
        std::string const msg_type{m.attribute("msgtype").as_string("")};
        if (std::find(kOfficial33.begin(), kOfficial33.end(), msg_type) != kOfficial33.end()) {
            ++visited;
        }
    }
    ASSERT_EQ(visited, kOfficial33.size())
        << "N3 census must visit exactly the 33 OFFICIAL messages";

    // Record the census result explicitly. If this ever goes non-empty, the
    // T025 required-presence tables MUST source the affected message's
    // required set from the extended declaration walk (carrying per-member
    // `rule`), NOT from `m.fields` filtered by group_no_tag alone (plan.md
    // N3 / data-model.md §2 Caveat) — see the phase report for disposition.
    std::string detail;
    for (auto const& s : collapsed) {
        detail += s;
        detail += ' ';
    }
    EXPECT_TRUE(collapsed.empty())
        << "N3 census found cross-level tag collapse(s) in: " << detail;
}

// Discriminating-witness proof that `census_walk` actually detects a
// cross-level collapse (not a walk that silently fails to recurse into
// <group>/<component> and vacuously reports "clean" on the real FIX44.xml
// census above — feedback_sanitizer_canary_must_be_proven_red /
// feedback_witness_asserts_named_postcondition_not_proxy discipline). A
// synthetic minimal dictionary declares Symbol(55) BOTH at the message's
// top level (enclosing group_no_tag 0) AND inside a NoOrders(73) group
// entry — exactly the N3 collapse shape — and the walk must observe BOTH
// levels for tag 55.
TEST(Group067Census, CensusWalkDetectsSyntheticCrossLevelCollapse) {
    static constexpr char kSyntheticXml[] =
        "<fix type='FIX' major='4' minor='4' servicepack='0'>"
        " <fields>"
        "  <field number='55' name='Symbol' type='STRING' />"
        "  <field number='73' name='NoOrders' type='NUMINGROUP' />"
        " </fields>"
        " <messages>"
        "  <message name='SyntheticCollapse' msgtype='ZZ' msgcat='app'>"
        "   <field name='Symbol' required='N' />"
        "   <group name='NoOrders' required='N'>"
        "    <field name='Symbol' required='N' />"
        "   </group>"
        "  </message>"
        " </messages>"
        "</fix>";

    std::pmr::monotonic_buffer_resource arena;
    fixpp::dict::XmlLoader loader;
    fixpp::dict::Dictionary const dict = loader.load_from_string(kSyntheticXml, &arena);

    pugi::xml_document doc;
    auto const parse_result = doc.load_string(kSyntheticXml);
    ASSERT_TRUE(static_cast<bool>(parse_result)) << parse_result.description();
    auto const root = doc.child("fix");
    auto const comps = build_component_index(root);
    auto const msg = root.child("messages").child("message");
    ASSERT_TRUE(static_cast<bool>(msg));

    std::unordered_map<std::uint16_t, std::unordered_set<std::uint16_t>> levels_by_tag;
    census_walk(msg, 0, comps, dict, levels_by_tag);

    auto const it = levels_by_tag.find(55);
    ASSERT_NE(it, levels_by_tag.end()) << "Symbol(55) not observed at all — walk did not descend";
    EXPECT_GE(it->second.size(), 2u)
        << "census_walk failed to detect Symbol(55) at both top-level (0) and NoOrders(73) — "
           "the N3 'clean' result on the real FIX44.xml census above would be a false negative "
           "if this failed";
    EXPECT_TRUE(it->second.contains(0));
    EXPECT_TRUE(it->second.contains(73));
}
