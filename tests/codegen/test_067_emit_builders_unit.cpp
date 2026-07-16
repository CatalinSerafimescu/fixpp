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

#include "emit.hpp"
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

// 069-v44-all-families T004 — msgcat fail-closed witness (data-model.md
// Entity 1). A synthetic <message> lacking the msgcat attribute must make
// build_ir() reject the dictionary (fail-closed loader error, NOT a
// default-guessed category) — mirrors the synthetic-XML discriminating-witness
// pattern above. build_ir() only accepts a filesystem path (not a string), so
// the synthetic XML is written to a temp file (determinism_test.cpp TempDir
// precedent, inlined here to keep this TU self-contained).
TEST(Group069MsgCat, MissingMsgcatFailsClosed) {
    static constexpr char kSyntheticXml[] =
        "<fix type='FIX' major='4' minor='4' servicepack='0'>"
        " <fields>"
        "  <field number='55' name='Symbol' type='STRING' />"
        " </fields>"
        " <messages>"
        "  <message name='NoMsgCat' msgtype='ZZ'>"
        "   <field name='Symbol' required='N' />"
        "  </message>"
        " </messages>"
        "</fix>";

    auto const tmp_path =
        std::filesystem::temp_directory_path() / "fixpp_codegen_069_missing_msgcat_test.xml";
    {
        std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(out)) << "cannot create " << tmp_path;
        out << kSyntheticXml;
    }

    std::pmr::monotonic_buffer_resource arena;
    try {
        (void)fixpp::codegen::build_ir(tmp_path, &arena);
        FAIL() << "build_ir() must fail closed on a <message> missing msgcat, not "
                  "default-guess its category";
    } catch (fixpp::dict::xml_parse_error const& e) {
        EXPECT_NE(std::string{e.what()}.find("ZZ"), std::string::npos)
            << "error should name the offending msgtype — what()=" << e.what();
    }

    std::error_code ec;
    std::filesystem::remove(tmp_path, ec);
}

// ---------------------------------------------------------------------------
// T014 [US1] [TESTS-FIRST] — emit_builders(ir) OUTPUT-TEXT unit tests.
//
// MUST FAIL FIRST: emit_builders() is T005 empty-output scaffolding today
// (returns "") — every assertion below finds nothing and goes RED. Phase 3b
// (T016/T017) makes these GREEN.
//
// Assertions are deliberately SEMANTIC-TOKEN, not exact-whitespace/format:
// numeric tag literals located as digit-boundary-safe substrings (immune to
// surrounding whitespace/formatting choices Phase 3b has not made yet), and
// call-shape substrings taken VERBATIM from data-model.md §1.2/quickstart.md
// (`bb.field(tag, ...)`, `bb.group_begin(no_tag, delimiter_tag)`) — not an
// invented format (feedback: "over-constraining the emitted text now means
// 3b can't turn it green later").
// ---------------------------------------------------------------------------
namespace {

// Locate the first digit-boundary-safe occurrence of `tag` (as a decimal
// literal) at or after `from`. Guards against a substring collision, e.g.
// searching "11" must not match inside "111" or "211".
std::size_t find_tag_token(std::string const& text, int tag, std::size_t from = 0) {
    std::string const needle = std::to_string(tag);
    while (from <= text.size()) {
        auto const pos = text.find(needle, from);
        if (pos == std::string::npos) {
            return pos;
        }
        bool const boundary_before =
            (pos == 0) || (std::isdigit(static_cast<unsigned char>(text[pos - 1])) == 0);
        bool const boundary_after = (pos + needle.size() >= text.size()) ||
                                     (std::isdigit(static_cast<unsigned char>(
                                          text[pos + needle.size()])) == 0);
        if (boundary_before && boundary_after) {
            return pos;
        }
        from = pos + 1;
    }
    return std::string::npos;
}

// Whitespace-collapsed copy of `text` (all ASCII whitespace removed), so a
// call-shape substring search (e.g. "group_begin(268,269") is immune to
// Phase 3b's eventual spacing/line-wrap choices.
std::string collapse_whitespace(std::string const& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        if (std::isspace(static_cast<unsigned char>(c)) == 0) {
            out.push_back(c);
        }
    }
    return out;
}

// Extract the substring window starting at the first occurrence of
// `start_marker` (e.g. a message's class/function identifier) up to (but
// excluding) the next occurrence of `next_marker` at or after that point, or
// to the end of `text` if `next_marker` does not recur. Scopes a token
// search to ONE message's emitted region, avoiding cross-message
// contamination (tag 11/ClOrdID, e.g., recurs in many messages).
std::string extract_region(std::string const& text, std::string_view start_marker,
                            std::string_view next_marker) {
    auto const start = text.find(start_marker);
    if (start == std::string::npos) {
        return {};
    }
    auto const next = text.find(next_marker, start + start_marker.size());
    return text.substr(start, next == std::string::npos ? std::string::npos : next - start);
}

}  // namespace

// T014(a): top-level emission order is tag-ascending. D (NewOrderSingle)'s
// top-level tags ClOrdID(11) < OrderQty(38) < OrdType(40) < Price(44) <
// Side(54) < Symbol(55) < TransactTime(60) must be found in that byte
// order within the NewOrderSingle builder's emitted region.
TEST(Group067EmitBuilders, TopLevelEmissionOrderTagAscending) {
    std::string const out =
        fixpp::codegen::emit_builders(fix44_ir(), fixpp::codegen::CoverageMode::Official);
    std::string const region = extract_region(out, "NewOrderSingle", "NewOrderList");
    ASSERT_FALSE(region.empty()) << "no NewOrderSingle builder region found in emit_builders() output "
                                     "(empty until Phase 3b lands T016/T017)";

    static constexpr std::array<int, 7> kAscendingTags = {11, 38, 40, 44, 54, 55, 60};
    std::size_t last_pos = 0;
    for (int tag : kAscendingTags) {
        auto const pos = find_tag_token(region, tag, last_pos);
        ASSERT_NE(pos, std::string::npos) << "tag " << tag << " not found in NewOrderSingle region";
        EXPECT_GE(pos, last_pos) << "tag " << tag << " out of tag-ascending order";
        last_pos = pos;
    }
}

// T014(b): group-entry order is DECLARATION order (from group_order),
// NOT tag order. E (NewOrderList)'s NoOrders(73) entry must emit
// Symbol(55) BEFORE Side(54) (tag-sort would invert this to 54, 55 —
// research.md R1/R9, T007(b)'s IR-level pin, here at the EMITTED-TEXT
// level).
TEST(Group067EmitBuilders, GroupEntryOrderIsDeclarationOrderNotTagSorted) {
    std::string const out =
        fixpp::codegen::emit_builders(fix44_ir(), fixpp::codegen::CoverageMode::Official);
    std::string const region = extract_region(out, "NewOrderList", "OrderCancelRequest");
    ASSERT_FALSE(region.empty()) << "no NewOrderList builder region found in emit_builders() output";

    // Scope to AFTER the NoOrders(73) group tag is first mentioned, so the
    // 55/54 search targets the group-entry emission, not any top-level
    // occurrence.
    auto const group_pos = find_tag_token(region, 73);
    ASSERT_NE(group_pos, std::string::npos) << "NoOrders(73) not found in NewOrderList region";
    std::string const entry_region = region.substr(group_pos);

    auto const pos55 = find_tag_token(entry_region, 55);
    auto const pos54 = find_tag_token(entry_region, 54);
    ASSERT_NE(pos55, std::string::npos) << "Symbol(55) not found in NoOrders entry region";
    ASSERT_NE(pos54, std::string::npos) << "Side(54) not found in NoOrders entry region";
    EXPECT_LT(pos55, pos54) << "Symbol(55) must precede Side(54) in the NoOrders entry (declaration "
                                "order) — tag-sort would wrongly invert this to 54, 55";
}

// T014(c): header/framing exclusion set {8,9,10,34,35,49,52,56} — the
// generated builders never call `bb.field(<tag>, ...)` for a framing tag
// (data-model.md §1.2's documented call shape verbatim). A body-only
// builder that DID emit e.g. `field(8, ...)` would violate body_builder's
// own INV-2 framing-tag reject unconditionally.
TEST(Group067EmitBuilders, HeaderFramingTagsNeverPassedToFieldCall) {
    std::string const out =
        fixpp::codegen::emit_builders(fix44_ir(), fixpp::codegen::CoverageMode::Official);
    ASSERT_FALSE(out.empty()) << "emit_builders() output is empty (Phase 3b not landed yet)";

    for (int tag : {8, 9, 10, 34, 35, 49, 52, 56}) {
        std::string const needle = "field(" + std::to_string(tag) + ",";
        EXPECT_EQ(out.find(needle), std::string::npos)
            << "found forbidden framing-tag field() call for tag " << tag;
    }
}

// Header/trailer-provenance exclusion follow-up (data-model.md §2.1,
// contracts/generated-builder.md G9): the loader merges the ENTIRE
// <header>/<trailer> into every message's m.fields, but the write emitter
// is body-only — a body field lookalike derived from a header/trailer tag
// (Signature(89), SecureData(91), SignatureLength(93) — none of which are
// in the 8-tag kFramingTags floor) must NOT surface as a settable
// NewOrderSingleArgs member / `bb.field(<tag>, ...)` call. A genuine BODY
// field (ClOrdID(11)) is the positive control — it MUST still be present,
// proving the exclusion is provenance-scoped, not a body-emptying overreach.
TEST(Group067EmitBuilders, HeaderTrailerFieldsExcludedFromBodyOnlyArgs) {
    std::string const out =
        fixpp::codegen::emit_builders(fix44_ir(), fixpp::codegen::CoverageMode::Official);
    ASSERT_FALSE(out.empty()) << "emit_builders() output is empty (Phase 3b not landed yet)";
    std::string const region = extract_region(out, "NewOrderSingle", "NewOrderList");
    ASSERT_FALSE(region.empty()) << "no NewOrderSingle builder region found in emit_builders() output";

    for (int tag : {89, 91, 93}) {
        std::string const needle = "field(" + std::to_string(tag) + ",";
        EXPECT_EQ(region.find(needle), std::string::npos)
            << "found forbidden header/trailer-provenance field() call for tag " << tag
            << " (Signature/SecureData/SignatureLength) in the NewOrderSingle body-only region";
    }

    EXPECT_NE(region.find("field(11,"), std::string::npos)
        << "positive control: ClOrdID(11) is a genuine BODY field and MUST remain a "
           "NewOrderSingleArgs member — its absence would mean the exclusion overreached";
}

// T014(d): RC#1 per-message planner pin — ONE no_tag (268) yields DISTINCT
// per-message group_begin(no_tag, delimiter_tag) calls in W (delimiter 269)
// vs X (delimiter 279); a version-wide MemberMap-style plan would collapse
// both to the same (wrong-for-one) delimiter. Call shape verbatim from
// data-model.md §1.2 ("bb.group_begin(no_tag, delimiter_tag)").
TEST(Group067EmitBuilders, RC1PerMessagePlannerDistinctDelimiterWvsX) {
    std::string const out =
        fixpp::codegen::emit_builders(fix44_ir(), fixpp::codegen::CoverageMode::Official);
    ASSERT_FALSE(out.empty()) << "emit_builders() output is empty (Phase 3b not landed yet)";
    std::string const collapsed = collapse_whitespace(out);

    EXPECT_NE(collapsed.find("group_begin(268,269"), std::string::npos)
        << "W (MarketDataSnapshotFullRefresh) must open NoMDEntries(268) with delimiter "
           "MDEntryType(269)";
    EXPECT_NE(collapsed.find("group_begin(268,279"), std::string::npos)
        << "X (MarketDataIncrementalRefresh) must open NoMDEntries(268) with delimiter "
           "MDUpdateAction(279) — DISTINCT from W's 269 despite the SAME no_tag";
}

// ---------------------------------------------------------------------------
// 077-builder-args-dedup T007 — dedup-soundness discriminating witness.
//
// MUST FAIL FIRST: the current emitter (pre-T008) names group Args structs by
// message-rooted path (`resolve_level`, emit_builders.cpp:415-416) and never
// emits a `groups::G_<no_tag>[_ord]Args` name at all — every "found" assertion
// below is RED until T008 lands (data-model.md Entity 1; contracts
// generated-builder-dedup.md G1/G1a/G1b).
//
// Synthetic IR (built by hand, not via XmlLoader) so the three cases below are
// exact and independent of any real dictionary's structure:
//   (a) DISCRIMINATION — no_tag 9001 occurs in two messages with the SAME
//       delimiter (9101) but a DIFFERENT required-ness on that sole member
//       -> two distinct recursive signatures -> BOTH must be ordinaled, bare
//       name forbidden (G1a).
//   (b) COLLAPSE — no_tag 9002 occurs in two messages with a BYTE-IDENTICAL
//       signature (member 9201, same required-ness) -> ONE bare struct,
//       emitted exactly once (G1b).
//   (c) CROSS-no_tag SEPARATION — no_tag 9003 vs 9004, each with a single
//       occurrence, coincidentally identical bodies (member 9301, same
//       required-ness) -> stay SEPARATE bare plans; `no_tag` is part of the
//       dedup key (data-model.md Entity 1).
//
// Mutation-grade: a wrong `no_tag`-ALONE key would wrongly COLLAPSE (a)'s two
// distinct signatures into one struct — case (a) kills it. A wrong
// SIGNATURE-ONLY key (dropping no_tag) would wrongly COLLAPSE (c)'s two
// different no_tags into one struct — case (c) kills it. Together they pin
// the exact `(no_tag, signature)` key; (b) additionally guards against an
// over-eager key that never collapses (spurious ordinals on identical
// bodies).
// ---------------------------------------------------------------------------
namespace {

fixpp::codegen::FieldIR make_synth_field(std::uint16_t tag, std::string name,
                                         fixpp::dict::field_data_type type,
                                         fixpp::dict::field_presence rule,
                                         std::uint16_t group_no_tag) {
    fixpp::codegen::FieldIR f;
    f.name = std::move(name);
    f.ref.tag = tag;
    f.ref.type = type;
    f.ref.rule = rule;
    f.ref.group_no_tag = group_no_tag;
    return f;
}

// One message with a single top-level repeating group `no_tag`, containing
// one scalar member `member_tag` (String) with the given required-ness. The
// member is the group's delimiter (its sole/first declared member).
MessageIR make_synth_group_message(std::string msg_type, std::uint16_t no_tag,
                                   std::uint16_t member_tag, bool member_required) {
    MessageIR m;
    m.msg_type = msg_type;
    m.name = "Synthetic" + msg_type;
    m.is_application = true;

    m.fields.push_back(make_synth_field(no_tag, "NoSynth" + std::to_string(no_tag),
                                        fixpp::dict::field_data_type::NumInGroup,
                                        fixpp::dict::field_presence::Required,
                                        /*group_no_tag=*/0));
    m.fields.push_back(make_synth_field(
        member_tag, "SynthField" + std::to_string(member_tag), fixpp::dict::field_data_type::String,
        member_required ? fixpp::dict::field_presence::Required
                        : fixpp::dict::field_presence::Optional,
        /*group_no_tag=*/no_tag));

    GroupOrderEntry g;
    g.no_tag = no_tag;
    g.delimiter_tag = member_tag;
    g.members = {fixpp::codegen::GroupOrderMember{.tag = member_tag, .is_group = false}};
    m.group_order.push_back(std::move(g));
    return m;
}

VersionIR build_dedup_soundness_ir() {
    VersionIR ir;
    ir.ns = "v44";
    // (a) DISCRIMINATION — no_tag 9001, two distinct signatures (required
    // differs).
    ir.messages.push_back(
        make_synth_group_message("ZDEDUP9001A", 9001, 9101, /*member_required=*/true));
    ir.messages.push_back(
        make_synth_group_message("ZDEDUP9001B", 9001, 9101, /*member_required=*/false));
    // (b) COLLAPSE — no_tag 9002, byte-identical signature.
    ir.messages.push_back(
        make_synth_group_message("ZDEDUP9002A", 9002, 9201, /*member_required=*/true));
    ir.messages.push_back(
        make_synth_group_message("ZDEDUP9002B", 9002, 9201, /*member_required=*/true));
    // (c) CROSS-no_tag SEPARATION — coincidentally identical bodies, different
    // no_tags.
    ir.messages.push_back(
        make_synth_group_message("ZDEDUP9003A", 9003, 9301, /*member_required=*/true));
    ir.messages.push_back(
        make_synth_group_message("ZDEDUP9004A", 9004, 9301, /*member_required=*/true));
    return ir;
}

VersionIR const& dedup_soundness_ir() {
    static VersionIR const ir = build_dedup_soundness_ir();
    return ir;
}

std::size_t count_occurrences(std::string const& text, std::string const& needle) {
    std::size_t count = 0;
    std::size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos) {
        ++count;
        pos += needle.size();
    }
    return count;
}

}  // namespace

TEST(Group077DedupSoundness, DiscriminatesDistinctSignaturesUnderSameNoTag) {
    std::string const out =
        fixpp::codegen::emit_builders(dedup_soundness_ir(), fixpp::codegen::CoverageMode::All);

    EXPECT_NE(out.find("struct G_9001_1Args"), std::string::npos)
        << "no_tag 9001 has TWO distinct recursive signatures (member 9101 required vs "
           "optional) — expected ordinaled groups::G_9001_1Args (G1a)";
    EXPECT_NE(out.find("struct G_9001_2Args"), std::string::npos)
        << "expected ordinaled groups::G_9001_2Args for the second distinct signature";
    EXPECT_EQ(out.find("G_9001Args"), std::string::npos)
        << "no_tag 9001 has >=2 distinct signatures — G1a forbids a bare G_9001Args name "
           "anywhere in the output (a wrong no_tag-ONLY dedup key would wrongly collapse "
           "this)";
    EXPECT_NE(out.find("groups::G_9001_1Args"), std::string::npos)
        << "expected a qualified reference to groups::G_9001_1Args from its owning message";
    EXPECT_NE(out.find("groups::G_9001_2Args"), std::string::npos)
        << "expected a qualified reference to groups::G_9001_2Args from its owning message";
}

TEST(Group077DedupSoundness, CollapsesByteIdenticalSignaturesToOneStruct) {
    std::string const out =
        fixpp::codegen::emit_builders(dedup_soundness_ir(), fixpp::codegen::CoverageMode::All);

    EXPECT_NE(out.find("struct G_9002Args"), std::string::npos)
        << "no_tag 9002's two occurrences share a byte-identical signature — expected ONE "
           "bare groups::G_9002Args (G1b)";
    EXPECT_EQ(count_occurrences(out, "struct G_9002Args"), 1u)
        << "the shared plan must be emitted EXACTLY ONCE, not once per referencing message";
    EXPECT_EQ(out.find("G_9002_1Args"), std::string::npos)
        << "identical signatures must NOT be spuriously ordinaled";
    EXPECT_EQ(out.find("G_9002_2Args"), std::string::npos)
        << "identical signatures must NOT be spuriously ordinaled";
    EXPECT_GE(count_occurrences(out, "groups::G_9002Args"), 2u)
        << "both ZDEDUP9002A and ZDEDUP9002B must reference the SAME shared plan by "
           "qualified name";
}

TEST(Group077DedupSoundness, KeepsDifferentNoTagsSeparateDespiteIdenticalBodies) {
    std::string const out =
        fixpp::codegen::emit_builders(dedup_soundness_ir(), fixpp::codegen::CoverageMode::All);

    EXPECT_NE(out.find("struct G_9003Args"), std::string::npos)
        << "no_tag 9003's sole occurrence must emit a bare groups::G_9003Args";
    EXPECT_NE(out.find("struct G_9004Args"), std::string::npos)
        << "no_tag 9004's sole occurrence must emit a bare groups::G_9004Args — its body is "
           "coincidentally identical to 9003's, but no_tag is part of the dedup key, so a "
           "signature-ONLY key would wrongly collapse these into one struct";
    EXPECT_EQ(out.find("G_9003_1Args"), std::string::npos)
        << "no_tag 9003 has only one signature — no ordinal expected";
    EXPECT_EQ(out.find("G_9004_1Args"), std::string::npos)
        << "no_tag 9004 has only one signature — no ordinal expected";
}
