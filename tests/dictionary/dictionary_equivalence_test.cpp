// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/dictionary_equivalence_test.cpp — W-0, the comparator's self-test
// (098-dialect-overlay, contracts/witnesses.md "Comparator self-test").
//
// Each seed is a pair of VALID small dictionaries (or one dictionary checked against
// a census of a different document) with the set of `kStoreList` entries it is
// expected to make the comparator report. The expected set of each seed is derived
// from the loader and `Dictionary::as_table_view` source, never copied from a run;
// a seed whose reported set differs fails, naming both sets. The union of the
// expected sets plus the reason-carrying entries must equal the declared list.
//
// The invariant seeds feed each invariant check hand-built store values that break
// its invariant only; a valid dictionary cannot reach them (contracts W-0), so they
// sit outside the union.

#include "dictionary_equivalence.hpp"

#include <gtest/gtest.h>

#include <exception>
#include <fixpp/dict/xml_loader.hpp>
#include <iostream>
#include <memory_resource>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace eq = fixpp::dict::equivalence;
namespace ids = fixpp::dict::equivalence::id;
using fixpp::dict::Dictionary;
using fixpp::dict::table_view_test_access;

// The base every seed edits. Its pieces each serve a seed: message `E` reuses
// NoAllocs(78) with a component inside (a context record the first-seen bare store
// does not describe); `Unused` is reachable from no message (component and group
// side tables only); CustLen(5010) and CustData(5011) form a non-standard
// Length+Data pair.
constexpr std::string_view kBase = R"(<fix type='FIX' major='4' minor='4' servicepack='0'>
<header>
<field name='BeginString' required='Y'/>
<field name='BodyLength' required='Y'/>
<field name='MsgType' required='Y'/>
</header>
<trailer>
<field name='CheckSum' required='Y'/>
</trailer>
<messages>
<message name='Heartbeat' msgtype='0' msgcat='admin'>
<field name='TestReqID' required='N'/>
</message>
<message name='NewOrderSingle' msgtype='D' msgcat='app'>
<field name='ClOrdID' required='Y'/>
<field name='Side' required='Y'/>
<field name='OrdType' required='N'/>
<field name='Text' required='N'/>
<field name='ExecInst' required='N'/>
<field name='RawDataLength' required='N'/>
<field name='RawData' required='N'/>
<component name='Parties' required='N'/>
<group name='NoAllocs' required='N'>
<field name='AllocAccount' required='Y'/>
<field name='AllocQty' required='N'/>
</group>
</message>
<message name='NewOrderList' msgtype='E' msgcat='app'>
<field name='ClOrdID' required='Y'/>
<group name='NoAllocs' required='Y'>
<component name='AllocC' required='Y'/>
<field name='AllocQty' required='N'/>
</group>
</message>
</messages>
<components>
<component name='Parties'>
<group name='NoPartyIDs' required='N'>
<field name='PartyID' required='N'/>
<field name='PartyIDSource' required='N'/>
<field name='PartyRole' required='N'/>
</group>
</component>
<component name='AllocC'>
<field name='AllocAccount' required='Y'/>
</component>
<component name='Unused'>
<field name='Price' required='N'/>
<group name='NoUnusedOuter' required='N'>
<field name='UnusedA' required='N'/>
<field name='CustLen' required='N'/>
<field name='CustData' required='N'/>
<group name='NoUnusedInner' required='N'>
<field name='UnusedB' required='N'/>
</group>
<field name='UnusedC' required='N'/>
</group>
</component>
</components>
<fields>
<field number='8' name='BeginString' type='STRING'/>
<field number='9' name='BodyLength' type='LENGTH'/>
<field number='10' name='CheckSum' type='STRING'/>
<field number='11' name='ClOrdID' type='STRING'/>
<field number='18' name='ExecInst' type='MULTIPLEVALUESTRING'>
<value enum='1' description='NOT_HELD'/>
<value enum='2' description='WORK'/>
</field>
<field number='35' name='MsgType' type='STRING'>
<value enum='0' description='HEARTBEAT'/>
<value enum='D' description='ORDER_SINGLE'/>
<value enum='E' description='ORDER_LIST'/>
</field>
<field number='40' name='OrdType' type='CHAR'>
<value enum='1' description='MARKET'/>
<value enum='2' description='LIMIT'/>
</field>
<field number='44' name='Price' type='PRICE'/>
<field number='54' name='Side' type='CHAR'>
<value enum='1' description='BUY'/>
<value enum='2' description='SELL'/>
</field>
<field number='58' name='Text' type='STRING'/>
<field number='78' name='NoAllocs' type='NUMINGROUP'/>
<field number='79' name='AllocAccount' type='STRING'/>
<field number='80' name='AllocQty' type='QTY'/>
<field number='95' name='RawDataLength' type='LENGTH'/>
<field number='96' name='RawData' type='DATA'/>
<field number='112' name='TestReqID' type='STRING'/>
<field number='447' name='PartyIDSource' type='CHAR'/>
<field number='448' name='PartyID' type='STRING'/>
<field number='452' name='PartyRole' type='INT'/>
<field number='453' name='NoPartyIDs' type='NUMINGROUP'/>
<field number='5010' name='CustLen' type='LENGTH'/>
<field number='5011' name='CustData' type='DATA'/>
<field number='9200' name='NoUnusedOuter' type='NUMINGROUP'/>
<field number='9201' name='UnusedA' type='STRING'/>
<field number='9202' name='NoUnusedInner' type='NUMINGROUP'/>
<field number='9203' name='UnusedB' type='STRING'/>
<field number='9204' name='UnusedC' type='STRING'/>
<field number='9300' name='NoCust' type='NUMINGROUP'/>
<field number='9301' name='CustMember' type='STRING'/>
</fields>
</fix>
)";

struct edit {
    std::string_view from;
    std::string_view to;
};

// Applies each edit at its single occurrence; an anchor that matches zero places or
// more than one gives nullopt, which fails the seed before any comparison.
std::optional<std::string> apply(std::string_view base, std::vector<edit> const& edits) {
    std::string out{base};
    for (auto const& e : edits) {
        auto const pos = out.find(e.from);
        if (pos == std::string::npos || out.find(e.from, pos + 1) != std::string::npos) {
            return std::nullopt;
        }
        out.replace(pos, e.from.size(), e.to);
    }
    return out;
}

struct loaded {
    std::optional<Dictionary> dict;
    std::string error;
};

loaded load(std::string const& xml) {
    loaded l;
    try {
        l.dict.emplace(
            fixpp::dict::XmlLoader{}.load_from_string(xml, std::pmr::get_default_resource()));
    } catch (std::exception const& e) {
        l.error = e.what();
    }
    return l;
}

std::string render(std::set<std::string_view> const& s) {
    std::string out = "{";
    for (auto const& e : s) {
        out += "\n    " + std::string{e};
    }
    return out + "\n  }";
}

struct seed {
    std::string_view name;
    std::vector<edit> side_a;    // edits of kBase for side A's document
    std::vector<edit> side_b;    // edits of kBase for side B's document
    std::vector<edit> census_a;  // edits of kBase for side A's census document
    bool census_a_from_doc;      // true: side A's census is of side A's document
    std::set<std::string_view> expected;
};

// One seed per row. Each row's expected set names every entry the edit changes,
// derived from the source the comment on the row cites.
std::vector<seed> const& seeds() {
    static std::vector<seed> const s{
        // Equal-length rename: only the message-name store sees it.
        {"message_name", {}, {{"name='Heartbeat'", "name='Heartbeet'"}}, {}, true, {ids::kMsgName}},
        // A new message: its run, its required set, its valid set; its msgtype and
        // name are interned before the components (`finalize`), so every
        // component's name_offset moves.
        {"extra_message",
         {},
         {{"</message>\n<message name='NewOrderSingle'",
           "</message>\n<message name='TestRequest' msgtype='1' msgcat='admin'>\n"
           "<field name='TestReqID' required='Y'/>\n</message>\n<message "
           "name='NewOrderSingle'"}},
         {},
         true,
         {ids::kMsgType, ids::kMessageFields.tag, ids::kFieldRef, ids::kField, ids::kFieldValidFor,
          ids::kRequiredFields, ids::kValid, ids::kRequired, ids::kCompNameOffset}},
        // Optional -> Required on a field already valid: `required_` without `valid_`.
        {"required_promotion",
         {},
         {{"<field name='Text' required='N'/>", "<field name='Text' required='Y'/>"}},
         {},
         true,
         {ids::kMessageFields.rule, ids::kFieldRef, ids::kField, ids::kRequiredFields,
          ids::kRequired}},
        // CHAR -> STRING changes the coarse field_type too.
        {"field_type_coarse",
         {},
         {{"name='OrdType' type='CHAR'", "name='OrdType' type='STRING'"}},
         {},
         true,
         {ids::kMessageFields.type, ids::kFieldRef, ids::kField, ids::kTypes}},
        // QTY -> PRICE: both collapse to field_type::Float, so `types_` is unchanged;
        // the field sits in D's NoAllocs node, the bare group's first-seen node.
        {"field_type_fine",
         {},
         {{"name='AllocQty' type='QTY'", "name='AllocQty' type='PRICE'"}},
         {},
         true,
         {ids::kMessageFields.type, ids::kFieldRef, ids::kField, ids::kGroupFields.type}},
        // A field only in `Unused`, which no message reaches.
        {"component_member_type",
         {},
         {{"name='Price' type='PRICE'", "name='Price' type='QTY'"}},
         {},
         true,
         {ids::kComponentFields.type}},
        {"component_member_rule",
         {},
         {{"<field name='Price' required='N'/>", "<field name='Price' required='Y'/>"}},
         {},
         true,
         {ids::kComponentFields.rule}},
        // A new single-byte code: codes and mask; two pool bytes before the
        // components shift every name_offset.
        {"enum_value_added",
         {},
         {{"<value enum='2' description='SELL'/>",
           "<value enum='2' description='SELL'/>\n<value enum='3' description='X'/>"}},
         {},
         true,
         {ids::kEnumValue, ids::kEnumCodes, ids::kEnumMask, ids::kCompNameOffset}},
        // A first value on a field with none: a new enums_ key and its bit.
        {"enum_first_value",
         {},
         {{"<field number='58' name='Text' type='STRING'/>",
           "<field number='58' name='Text' type='STRING'>\n<value enum='X' description='Y'/>\n"
           "</field>"}},
         {},
         true,
         {ids::kEnumValue, ids::kEnumsKeys, ids::kEnumBits, ids::kCompNameOffset}},
        {"enum_description",
         {},
         {{"description='BUY'", "description='BUX'"}},
         {},
         true,
         {ids::kEnumDescription}},
        // '2' -> 'AB' (description shortened by one, so the pool length holds):
        // a multi-byte code clears all_single_char and drops bit '2' from the mask.
        {"enum_multibyte_code",
         {},
         {{"<value enum='2' description='LIMIT'/>", "<value enum='AB' description='LIMI'/>"}},
         {},
         true,
         {ids::kEnumValue, ids::kEnumCodes, ids::kEnumAllSingle, ids::kEnumMask}},
        // MultiStringValue -> String: `set_multi_value` no longer runs; both collapse
        // to field_type::String.
        {"enum_multi_value",
         {},
         {{"name='ExecInst' type='MULTIPLEVALUESTRING'", "name='ExecInst' type='STRING'"}},
         {},
         true,
         {ids::kMessageFields.type, ids::kFieldRef, ids::kField, ids::kEnumMulti}},
        // A MultiCharValue field with no values: `set_multi_value` alone makes an
        // enums_ key with empty codes.
        {"enum_key_from_multi_value",
         {},
         {{"name='Text' type='STRING'", "name='Text' type='MULTIPLECHARVALUE'"}},
         {},
         true,
         {ids::kMessageFields.type, ids::kFieldRef, ids::kField, ids::kEnumsKeys, ids::kEnumBits}},
        // E's AllocQty leaves E's group: E's context record loses it; the bare store
        // is D's first-seen node.
        {"group_no_tag",
         {},
         {{"<component name='AllocC' required='Y'/>\n<field name='AllocQty' "
           "required='N'/>\n</group>",
           "<component name='AllocC' required='Y'/>\n</group>\n<field name='AllocQty' "
           "required='N'/>"}},
         {},
         true,
         {ids::kMessageFields.group_no_tag, ids::kFieldRef, ids::kField, ids::kGroupCtxMembers}},
        // AllocAccount inline instead of through AllocC: component_index only.
        {"component_index",
         {},
         {{"<component name='AllocC' required='Y'/>", "<field name='AllocAccount' required='Y'/>"}},
         {},
         true,
         {ids::kMessageFields.component_index, ids::kFieldRef, ids::kField}},
        // RawData no longer DATA: no pair is detected at all.
        {"length_pair_removed",
         {},
         {{"name='RawData' type='DATA'", "name='RawData' type='STRING'"}},
         {},
         true,
         {ids::kMessageFields.type, ids::kMessageFields.length_pair_data_tag, ids::kFieldRef,
          ids::kField, ids::kLengthPairDataTag, ids::kTypes, ids::kPairForward, ids::kPairReverse}},
        // The CustLen-CustData pair reaches a message: both directions and the flag.
        {"nonstandard_pair",
         {},
         {{"msgtype='E' msgcat='app'>\n<field name='ClOrdID' required='Y'/>",
           "msgtype='E' msgcat='app'>\n<field name='ClOrdID' required='Y'/>\n"
           "<field name='CustLen' required='N'/>\n<field name='CustData' required='N'/>"}},
         {},
         true,
         {ids::kMessageFields.tag, ids::kFieldRef, ids::kField, ids::kFieldValidFor, ids::kValid,
          ids::kTypes, ids::kLengthPairDataTag, ids::kPairForward, ids::kPairReverse,
          ids::kNonstandardPair}},
        // Key source: a component no message uses, declared after the others so it
        // moves none of their name pool offsets.
        {"unused_extra_component",
         {},
         {{"</component>\n</components>",
           "</component>\n<component name='Spare'>\n<field name='Price' "
           "required='N'/>\n</component>\n</components>"}},
         {},
         true,
         {ids::kComponent, ids::kComponentFields.tag}},
        // Parties and AllocC swap declaration order: ids, offsets, side-table starts
        // and every component_index of their members.
        {"component_order",
         {},
         {{"<component name='Parties'>\n<group name='NoPartyIDs' required='N'>\n"
           "<field name='PartyID' required='N'/>\n<field name='PartyIDSource' required='N'/>\n"
           "<field name='PartyRole' required='N'/>\n</group>\n</component>\n"
           "<component name='AllocC'>\n<field name='AllocAccount' required='Y'/>\n"
           "</component>",
           "<component name='AllocC'>\n<field name='AllocAccount' required='Y'/>\n"
           "</component>\n<component name='Parties'>\n<group name='NoPartyIDs' "
           "required='N'>\n<field name='PartyID' required='N'/>\n<field name='PartyIDSource' "
           "required='N'/>\n<field name='PartyRole' required='N'/>\n</group>\n</component>"}},
         {},
         true,
         {ids::kMessageFields.component_index, ids::kFieldRef, ids::kField, ids::kCompId,
          ids::kCompNameOffset, ids::kCompFirstFieldIndex, ids::kComponentFields.component_index}},
        // AllocC nested in Unused (the last component): a parent id and a longer run.
        {"component_parent",
         {},
         {{"<field name='UnusedC' required='N'/>\n</group>\n</component>",
           "<field name='UnusedC' required='N'/>\n</group>\n<component name='AllocC' "
           "required='N'/>\n</component>"}},
         {},
         true,
         {ids::kCompParent, ids::kCompFieldCount, ids::kComponentFields.tag}},
        // D's NoAllocs (the first-seen node) gets AllocQty first: the delimiter, bare
        // and context, and member order.
        {"group_delimiter",
         {},
         {{"<group name='NoAllocs' required='N'>\n<field name='AllocAccount' required='Y'/>\n"
           "<field name='AllocQty' required='N'/>",
           "<group name='NoAllocs' required='N'>\n<field name='AllocQty' required='N'/>\n"
           "<field name='AllocAccount' required='Y'/>"}},
         {},
         true,
         {ids::kGroupFirstFieldTag, ids::kGroupFirstField, ids::kGroupFirstBare,
          ids::kGroupMembersBare, ids::kGroupCtxFirst, ids::kGroupCtxMembers,
          ids::kGroupFields.tag}},
        // D's NoAllocs becomes required: the bare required set (first-seen node,
        // `g_own_required`) and D's context required set move together.
        {"group_required_bare",
         {},
         {{"<group name='NoAllocs' required='N'>", "<group name='NoAllocs' required='Y'>"}},
         {},
         true,
         {ids::kMessageFields.rule, ids::kFieldRef, ids::kField, ids::kRequiredFields,
          ids::kRequired, ids::kGroupRequiredBare, ids::kGroupCtxRequired}},
        // E's componentRef stops being required: only E's context required set moves
        // (group-relative AND); the bare one is D's.
        {"group_required_context",
         {},
         {{"<component name='AllocC' required='Y'/>", "<component name='AllocC' required='N'/>"}},
         {},
         true,
         {ids::kGroupCtxRequired}},
        // Key source: E without its NoAllocs group (78 as a plain field). The three-arg
        // accessors fall back to the bare store, which equals A's context record, so
        // only the context key reports it (side A is the row above's B, so the
        // required sets also agree).
        {"context_masked_by_bare",
         {{"<component name='AllocC' required='Y'/>", "<component name='AllocC' required='N'/>"}},
         {{"<component name='AllocC' required='Y'/>", "<component name='AllocC' required='N'/>"},
          {"<group name='NoAllocs' required='Y'>\n<component name='AllocC' required='N'/>\n"
           "<field name='AllocQty' required='N'/>\n</group>",
           "<field name='NoAllocs' required='Y'/>"}},
         {},
         true,
         {ids::kMessageFields.tag, ids::kFieldRef, ids::kField, ids::kFieldValidFor, ids::kValid,
          ids::kGroupCtxKeys}},
        // Key source: the census of a document with one component member different.
        {"census_component_member",
         {},
         {},
         {{"<field name='Price' required='N'/>\n<group name='NoUnusedOuter'",
           "<field name='Text' required='N'/>\n<group name='NoUnusedOuter'"}},
         false,
         {ids::kCensusComponentFields}},
        {"census_group_member",
         {},
         {},
         {{"<field name='AllocAccount' required='Y'/>\n<field name='AllocQty' required='N'/>",
           "<field name='AllocAccount' required='Y'/>\n<field name='Text' required='N'/>"}},
         false,
         {ids::kCensusGroupFields}},
        // One more census member in Parties shifts every later component's start.
        {"census_component_start",
         {},
         {},
         {{"<field name='PartyRole' required='N'/>\n</group>\n</component>",
           "<field name='PartyRole' required='N'/>\n</group>\n<field name='Text' "
           "required='N'/>\n</component>"}},
         false,
         {ids::kCensusComponentFields, ids::kCensusComponentStart}},
        {"census_group_start",
         {},
         {},
         {{"<field name='AllocAccount' required='Y'/>\n<field name='AllocQty' required='N'/>",
           "<field name='AllocAccount' required='Y'/>\n<field name='AllocQty' required='N'/>\n"
           "<field name='Text' required='N'/>"}},
         false,
         {ids::kCensusGroupFields, ids::kCensusGroupStart}},
        // Parties moved inside D's NoAllocs (D is first-seen for both groups):
        // NoPartyIDs gets a parent, NoAllocs a longer run, later starts shift.
        {"group_parent",
         {},
         {{"<component name='Parties' required='N'/>\n<group name='NoAllocs' required='N'>\n"
           "<field name='AllocAccount' required='Y'/>\n<field name='AllocQty' "
           "required='N'/>\n</group>",
           "<group name='NoAllocs' required='N'>\n<field name='AllocAccount' required='Y'/>\n"
           "<field name='AllocQty' required='N'/>\n<component name='Parties' "
           "required='N'/>\n</group>"}},
         {},
         true,
         {ids::kMessageFields.group_no_tag, ids::kFieldRef, ids::kField, ids::kGroupCtxKeys,
          ids::kGroupCtxMembers, ids::kGroupFields.tag, ids::kGroupFieldCount,
          ids::kGroupFirstFieldIndex, ids::kGroupParent, ids::kGroupMembersBare}},
        // A new group (no_tag above every other, so no start moves).
        {"new_group",
         {},
         {{"<field name='AllocQty' required='N'/>\n</group>\n</message>\n</messages>",
           "<field name='AllocQty' required='N'/>\n</group>\n<group name='NoCust' "
           "required='N'>\n<field name='CustMember' required='N'/>\n</group>\n</message>\n"
           "</messages>"}},
         {},
         true,
         {ids::kMessageFields.tag, ids::kFieldRef, ids::kField, ids::kFieldValidFor, ids::kValid,
          ids::kTypes, ids::kGroup, ids::kGroupFirstField, ids::kGroupFields.tag,
          ids::kGroupFirstBare, ids::kGroupMembersBare, ids::kGroupBits, ids::kGroupCtxKeys}},
        // Equal-length field rename, declaration and reference.
        {"field_rename",
         {},
         {{"name='Text' type='STRING'", "name='Txet' type='STRING'"},
          {"<field name='Text' required='N'/>", "<field name='Txet' required='N'/>"}},
         {},
         true,
         {ids::kFieldByName, ids::kFieldName}},
        // UnusedC moves into the inner group: same tag order, new group_no_tag.
        {"nested_group_no_tag",
         {},
         {{"<field name='UnusedB' required='N'/>\n</group>\n<field name='UnusedC' "
           "required='N'/>\n</group>",
           "<field name='UnusedB' required='N'/>\n<field name='UnusedC' "
           "required='N'/>\n</group>\n</group>"}},
         {},
         true,
         {ids::kComponentFields.group_no_tag, ids::kGroupFields.group_no_tag, ids::kGroupFields.tag,
          ids::kGroupFieldCount}},
        // CustData no longer DATA, inside Unused's outer group only.
        {"side_table_pair",
         {},
         {{"name='CustData' type='DATA'", "name='CustData' type='STRING'"}},
         {},
         true,
         {ids::kComponentFields.type, ids::kComponentFields.length_pair_data_tag,
          ids::kGroupFields.type, ids::kGroupFields.length_pair_data_tag}},
        {"group_member_rule",
         {},
         {{"<field name='AllocQty' required='N'/>\n</group>\n</message>\n<message "
           "name='NewOrderList'",
           "<field name='AllocQty' required='Y'/>\n</group>\n</message>\n<message "
           "name='NewOrderList'"}},
         {},
         true,
         {ids::kMessageFields.rule, ids::kFieldRef, ids::kField, ids::kGroupFields.rule}},
        {"group_member_component_index",
         {},
         {{"<group name='NoAllocs' required='N'>\n<field name='AllocAccount' required='Y'/>",
           "<group name='NoAllocs' required='N'>\n<component name='AllocC' required='Y'/>"}},
         {},
         true,
         {ids::kMessageFields.component_index, ids::kFieldRef, ids::kField,
          ids::kGroupFields.component_index}},
        // MsgType moves from <header> to <trailer>: every message run keeps the same
        // FieldRef for 35 (the header and trailer both expand at the message's top
        // level, `LoaderState::finalize`), so only the declared header store moves.
        {"header_tag",
         {},
         {{"<field name='MsgType' required='Y'/>\n</header>", "</header>"},
          {"<field name='CheckSum' required='Y'/>\n</trailer>",
           "<field name='CheckSum' required='Y'/>\n<field name='MsgType' "
           "required='Y'/>\n</trailer>"}},
         {},
         true,
         {ids::kHeaderBits}},
    };
    return s;
}

// gtest prints a parameter on failure; the name is what identifies the row.
void PrintTo(seed const& s, std::ostream* os) { *os << s.name; }

class ComparatorSeed : public ::testing::TestWithParam<seed> {};

eq::comparison_report run_seed(seed const& s, std::set<std::string_view>* reported) {
    auto const doc_a = apply(kBase, s.side_a);
    auto const doc_b = apply(kBase, s.side_b);
    auto const census_doc_a = s.census_a_from_doc ? doc_a : apply(kBase, s.census_a);
    EXPECT_TRUE(doc_a && doc_b && census_doc_a) << "an edit anchor did not match exactly once";
    if (!doc_a || !doc_b || !census_doc_a) {
        return {};
    }
    auto const a = load(*doc_a);
    auto const b = load(*doc_b);
    EXPECT_TRUE(a.dict.has_value()) << "side A does not load: " << a.error;
    EXPECT_TRUE(b.dict.has_value()) << "side B does not load: " << b.error;
    if (!a.dict || !b.dict) {
        return {};
    }
    auto const ca = eq::census_from_quickfix_xml(*census_doc_a);
    auto const cb = eq::census_from_quickfix_xml(*doc_b);
    auto r = eq::compare({.dict = *a.dict, .census = ca}, {.dict = *b.dict, .census = cb});
    *reported = r.reported_entries();
    return r;
}

TEST_P(ComparatorSeed, ReportsExactlyItsExpectedSet) {
    seed const& s = GetParam();
    std::set<std::string_view> reported;
    auto const r = run_seed(s, &reported);
    std::cout << "[W-0] seed " << s.name << " reported " << render(reported) << '\n';
    EXPECT_EQ(reported, s.expected)
        << "expected " << render(s.expected) << "\nreported " << render(reported) << "\n"
        << r.describe();
    EXPECT_TRUE(r.invariant_violations.empty()) << r.describe();
    EXPECT_EQ(r.census_excluded, 0U) << r.describe();
    EXPECT_GT(r.census_compared, 0U);
}

INSTANTIATE_TEST_SUITE_P(W0, ComparatorSeed, ::testing::ValuesIn(seeds()),
                         [](::testing::TestParamInfo<seed> const& info) {
                             return std::string{info.param.name};
                         });

TEST(ComparatorW0, IdenticalLoadsReportNothing) {
    std::string const doc{kBase};
    auto const a = load(doc);
    auto const b = load(doc);
    ASSERT_TRUE(a.dict.has_value()) << a.error;
    ASSERT_TRUE(b.dict.has_value()) << b.error;
    auto const c = eq::census_from_quickfix_xml(doc);
    auto const r = eq::compare({.dict = *a.dict, .census = c}, {.dict = *b.dict, .census = c});
    EXPECT_TRUE(r.differences.empty()) << r.describe();
    EXPECT_TRUE(r.invariant_violations.empty()) << r.describe();
    EXPECT_TRUE(r.notes.empty()) << r.describe();
    EXPECT_EQ(r.census_excluded, 0U);
    EXPECT_GT(r.census_compared, 0U);
}

// The unused-component seed with both censuses of the base, so the census cannot
// supply the extra component's key: only the handle's `components_by_name_` can.
TEST(ComparatorW0, ExtraComponentFoundThroughTheHandleIndexAlone) {
    std::string const base{kBase};
    auto const doc_b = apply(kBase, {{"</component>\n</components>",
                                      "</component>\n<component name='Spare'>\n<field "
                                      "name='Price' required='N'/>\n</component>\n</components>"}});
    ASSERT_TRUE(doc_b.has_value());
    auto const a = load(base);
    auto const b = load(*doc_b);
    ASSERT_TRUE(a.dict.has_value()) << a.error;
    ASSERT_TRUE(b.dict.has_value()) << b.error;
    auto const c = eq::census_from_quickfix_xml(base);
    for (auto const& [name, members] : c.components) {
        ASSERT_NE(name, "Spare") << "the census must not supply the key in this arm";
    }
    auto const r = eq::compare({.dict = *a.dict, .census = c}, {.dict = *b.dict, .census = c});
    std::set<std::string_view> const expected{ids::kComponent, ids::kComponentFields.tag};
    EXPECT_EQ(r.reported_entries(), expected) << r.describe();
}

// The context required_members seed leaves the bare required store equal.
TEST(ComparatorW0, ContextRequiredSeedKeepsTheBareStoreEqual) {
    auto const doc_b = apply(kBase, {{"<component name='AllocC' required='Y'/>",
                                      "<component name='AllocC' required='N'/>"}});
    ASSERT_TRUE(doc_b.has_value());
    auto const a = load(std::string{kBase});
    auto const b = load(*doc_b);
    ASSERT_TRUE(a.dict.has_value()) << a.error;
    ASSERT_TRUE(b.dict.has_value()) << b.error;
    auto const tva = a.dict->as_table_view();
    auto const tvb = b.dict->as_table_view();
    EXPECT_TRUE(table_view_test_access::group_required_members(tva) ==
                table_view_test_access::group_required_members(tvb));
    EXPECT_TRUE(std::ranges::equal(tva.group_required_members(78), tvb.group_required_members(78)));
    std::span<std::uint16_t const> const root{};
    EXPECT_FALSE(std::ranges::equal(tva.group_required_members("E", root, 78),
                                    tvb.group_required_members("E", root, 78)));
}

// The masked-context seed: the three-arg accessors answer identically on both
// sides, so only a read of the context store's keys can see the difference.
TEST(ComparatorW0, MaskedContextIsInvisibleToTheThreeArgAccessors) {
    auto const doc_a = apply(kBase, {{"<component name='AllocC' required='Y'/>",
                                      "<component name='AllocC' required='N'/>"}});
    ASSERT_TRUE(doc_a.has_value());
    auto const doc_b =
        apply(*doc_a, {{"<group name='NoAllocs' required='Y'>\n<component name='AllocC' "
                        "required='N'/>\n<field name='AllocQty' required='N'/>\n</group>",
                        "<field name='NoAllocs' required='Y'/>"}});
    ASSERT_TRUE(doc_b.has_value());
    auto const a = load(*doc_a);
    auto const b = load(*doc_b);
    ASSERT_TRUE(a.dict.has_value()) << a.error;
    ASSERT_TRUE(b.dict.has_value()) << b.error;
    auto const tva = a.dict->as_table_view();
    auto const tvb = b.dict->as_table_view();
    std::span<std::uint16_t const> const root{};
    EXPECT_EQ(tva.group_first_field("E", root, 78), tvb.group_first_field("E", root, 78));
    EXPECT_TRUE(std::ranges::equal(tva.group_member_tags("E", root, 78),
                                   tvb.group_member_tags("E", root, 78)));
    EXPECT_TRUE(std::ranges::equal(tva.group_required_members("E", root, 78),
                                   tvb.group_required_members("E", root, 78)));
    EXPECT_TRUE(tva.group_first_field_exact("E", root, 78).has_value());
    EXPECT_FALSE(tvb.group_first_field_exact("E", root, 78).has_value());
}

TEST(ComparatorW0, CensusWritesADuplicateFieldNameToItsNotes) {
    auto const doc = apply(kBase, {{"<field number='9301' name='CustMember' type='STRING'/>",
                                    "<field number='9301' name='CustMember' type='STRING'/>\n"
                                    "<field number='9302' name='CustMember' type='STRING'/>"}});
    ASSERT_TRUE(doc.has_value());
    auto const c = eq::census_from_quickfix_xml(*doc);
    bool found = false;
    for (auto const& n : c.notes) {
        found = found || n.find("duplicate <field name=\"CustMember\">") != std::string::npos;
    }
    EXPECT_TRUE(found) << "the census must write the duplicate name to its notes";
}

// ── the union assertion ──────────────────────────────────────────────────────

TEST(ComparatorW0, ExpectedSetsPlusReasonEntriesEqualTheDeclaredList) {
    std::set<std::string_view> declared;
    std::set<std::string_view> seeded;
    std::set<std::string_view> with_reason;
    for (auto const& e : eq::kStoreList) {
        EXPECT_TRUE(declared.insert(e.id).second) << "duplicate id " << e.id;
        if (e.kind == eq::entry_kind::seeded) {
            seeded.insert(e.id);
            EXPECT_TRUE(e.reason.empty()) << e.id;
        } else {
            with_reason.insert(e.id);
            EXPECT_FALSE(e.reason.empty()) << e.id << " has no stated reason";
        }
    }
    std::set<std::string_view> expected_union;
    for (auto const& s : seeds()) {
        expected_union.insert(s.expected.begin(), s.expected.end());
    }
    for (auto const& e : expected_union) {
        EXPECT_FALSE(with_reason.contains(e)) << e << " is seeded but carries a reason";
        EXPECT_TRUE(declared.contains(e)) << e << " is expected by a seed but not declared";
    }
    EXPECT_EQ(expected_union, seeded) << "seeded entries without a seed, or the reverse";
    std::set<std::string_view> all = expected_union;
    all.insert(with_reason.begin(), with_reason.end());
    EXPECT_EQ(all, declared);
}

// ── the invariant seeds (outside the union) ──────────────────────────────────

using tva_t = table_view_test_access;

void set_bit(tva_t::bit_store& bits, std::uint16_t key) {
    std::size_t const w = static_cast<std::size_t>(key) >> 6U;
    if (w >= bits.size()) {
        bits.resize(w + 1, 0);
    }
    bits[w] |= std::uint64_t{1} << (static_cast<std::size_t>(key) & 63U);
}

void clear_bit(tva_t::bit_store& bits, std::uint16_t key) {
    bits[static_cast<std::size_t>(key) >> 6U] &= ~(std::uint64_t{1} << (key & 63U));
}

bool any_names(std::vector<std::string> const& v, std::string const& needle) {
    for (auto const& s : v) {
        if (s.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

TEST(ComparatorW0Invariant, PairInverse) {
    tva_t::pair_store const forward{{95, 96}, {5010, 5011}};
    tva_t::pair_store const reverse{{96, 95}, {5011, 5010}};
    EXPECT_TRUE(eq::check_pair_inverse(forward, reverse).empty()) << "control";
    tva_t::pair_store const broken{{96, 95}};  // lacks the CustLen pair's reverse
    auto const v = eq::check_pair_inverse(forward, broken);
    ASSERT_EQ(v.size(), 1U);
    EXPECT_TRUE(any_names(v, "forward 5010 -> 5011")) << v.front();
}

TEST(ComparatorW0Invariant, GroupBits) {
    tva_t::bit_store bits;
    set_bit(bits, 78);
    set_bit(bits, 453);
    set_bit(bits, 9200);
    set_bit(bits, 9300);
    tva_t::group_first_store const first{{78, 79}};
    tva_t::group_members_store const members{{453, {448, 447}}};
    tva_t::group_required_store const required{{9200, {9201}}};
    tva_t::group_ctx_store ctx;
    ctx[fixpp::dict::make_group_ctx_key("E", {}, 9300)] = fixpp::dict::group_ctx_entry{
        .group_first = 9301, .members = {9301}, .required_members = {}};
    EXPECT_TRUE(eq::check_group_bits(bits, first, members, required, ctx).empty()) << "control";

    struct arm {
        std::uint16_t key;
        char const* names;
    };
    for (auto const& [key, names] :
         {arm{78, "group_first_ key 78"}, arm{453, "group_members_ key 453"},
          arm{9200, "group_required_members_ key 9200"},
          arm{9300, "group_ctx_ key (E, no_tag 9300)"}}) {
        auto broken = bits;
        clear_bit(broken, key);
        auto const v = eq::check_group_bits(broken, first, members, required, ctx);
        ASSERT_EQ(v.size(), 1U) << "key " << key;
        EXPECT_TRUE(any_names(v, names)) << v.front();
    }
}

// Both clauses of the header invariant, each broken alone: a store tag's bit
// cleared, and a bit set for a tag the store does not hold.
TEST(ComparatorW0Invariant, HeaderBits) {
    std::vector<std::uint16_t> const store{8, 9, 35};
    tva_t::bit_store bits;
    set_bit(bits, 8);
    set_bit(bits, 9);
    set_bit(bits, 35);
    EXPECT_TRUE(eq::check_header_bits(bits, store).empty()) << "control";

    auto cleared = bits;
    clear_bit(cleared, 35);
    auto const v1 = eq::check_header_bits(cleared, store);
    ASSERT_EQ(v1.size(), 1U);
    EXPECT_TRUE(any_names(v1, "header store tag 35 has a clear bit")) << v1.front();

    auto extra = bits;
    set_bit(extra, 34);
    auto const v2 = eq::check_header_bits(extra, store);
    ASSERT_EQ(v2.size(), 1U);
    EXPECT_TRUE(any_names(v2, "header_bits_ bit 34 is set but not in the header store"))
        << v2.front();
}

TEST(ComparatorW0Invariant, EnumBits) {
    tva_t::bit_store bits;
    set_bit(bits, 40);
    set_bit(bits, 54);
    tva_t::enums_store enums;
    enums[40].codes = {"1", "2"};
    enums[54].codes = {"1", "2"};
    EXPECT_TRUE(eq::check_enum_bits(bits, enums).empty()) << "control";
    auto broken = bits;
    clear_bit(broken, 54);
    auto const v = eq::check_enum_bits(broken, enums);
    ASSERT_EQ(v.size(), 1U);
    EXPECT_TRUE(any_names(v, "enums_ key 54")) << v.front();
}

}  // namespace
