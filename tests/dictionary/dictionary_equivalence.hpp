#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/dictionary_equivalence.hpp — the dictionary comparator
// (098-dialect-overlay, research R-10 §1–§3; its self-test is W-0 in
// dictionary_equivalence_test.cpp).
//
// `compare()` takes two loaded `Dictionary` values, each with a raw census of the
// document it is checked against, and returns every difference it finds, keyed by
// an entry of `kStoreList`. The compared stores are DATA: every comparison below
// reports under one `kStoreList` id, and W-0 asserts that the ids its valid seeds
// report, plus the entries that carry a reason instead of a seed, equal the list.
//
// Re-derivation recipe for the list (it is never written out in a comment):
//   read `include/fixpp/dict/table_view.hpp`'s `private:` blocks, class-scoped, e.g.
//     awk '/^class table_view \{/{f=1} f&&/^(public|private|protected):/{print NR": "$0}
//     f&&/^\};/{exit}' include/fixpp/dict/table_view.hpp
//   plus the other classes' `private:` blocks in that header; the `Dictionary` query
//   declarations in `include/fixpp/dict/dictionary.hpp` (with `MessageEntry` and
//   `EnumValueRef`); and the fields of `FieldRef`, `ComponentRef` and `GroupRef`
//   (`include/fixpp/dict/{field_ref,component_ref,group_ref}.hpp`). Map each item to
//   a `kStoreList` entry; an unmapped item means the list is wrong.
//
// Where the keys come from (research R-10 §2). Keys for the `Dictionary` queries
// are the union, over both sides, of the raw census, the handle's indexes (read
// through `dictionary_test_access`) and the public API. `table_view` stores are read
// whole through `table_view_test_access`, so every key either side holds is seen.
//
// The census copies only the loader rules that decide what exists, read from
// `LoaderState::parse_global_fields`, `collect_components`, `collect_messages` and
// `finalize` in src/dictionary/xml_loader.cpp: the first container of each name
// counts; the first `<field>` of a name wins; messages are walked sorted bytewise
// by msgtype, each as header, body, trailer, then components in declaration order,
// and a group's definition is the first `<group>` node met on that walk. A
// duplicate key the census meets is written to `raw_census::notes`.
// `census_from_orchestra` is the same census of an Orchestra document, read from
// `OrchestraLoaderState`'s `collect_*` members and `finalize` in
// src/dictionary/orchestra_loader.cpp: fields, components and groups are keyed by
// `id`; messages are walked sorted bytewise by msgType, each as its
// `fixr:structure`; then components in declaration order; a group's definition is
// the definition the first `fixr:groupRef` to its NumInGroup tag names on that walk,
// and its `fixr:numInGroup` emits nothing.
//
// The side-table wrap condition (research R-10 §3): a component or group is
// compared with its census expansion only when its start and its count, computed
// from census sizes in `finalize`'s order (re-derive the order from the two
// `finalize` loops that set `first_idx`), both fit in 16 bits. Any other is
// listed as a wrap case in the report's notes and counted as excluded.
//
// The invariant checks (contracts W-0) are functions of store values, so a test
// can feed each one values that break its invariant only. `compare()` runs each
// on both sides' real stores.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/field_ref.hpp>
#include <fixpp/dict/table_view.hpp>
#include <map>
#include <optional>
#include <pugixml.hpp>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "support/dictionary_test_access.hpp"
#include "support/table_view_test_access.hpp"

namespace fixpp::dict::equivalence {

// ── the declared store list ──────────────────────────────────────────────────

enum class entry_kind : std::uint8_t {
    seeded,             // compared; W-0 has a valid seed that reports it
    compared_unseeded,  // compared; no valid seed can make it differ (reason stated)
    not_a_store,        // a member that holds no store; not compared (reason stated)
};

struct store_entry {
    std::string_view id;
    entry_kind kind;
    std::string_view reason;  // empty for `seeded`
};

namespace id {
// Dictionary queries
inline constexpr std::string_view kWhichSessionVersion = "Dictionary::which_session_version";
inline constexpr std::string_view kMsgType = "Dictionary::messages: MessageEntry::msg_type";
inline constexpr std::string_view kMsgName = "Dictionary::messages: MessageEntry::name";
inline constexpr std::string_view kFieldRef = "Dictionary::field_ref";
inline constexpr std::string_view kField = "Dictionary::field";
inline constexpr std::string_view kFieldValidFor = "Dictionary::field_valid_for";
inline constexpr std::string_view kRequiredFields = "Dictionary::required_fields";
inline constexpr std::string_view kGroupFirstField = "Dictionary::group_first_field";
inline constexpr std::string_view kLengthPairDataTag = "Dictionary::length_pair_data_tag";
inline constexpr std::string_view kFieldByName = "Dictionary::field_by_name";
inline constexpr std::string_view kFieldName = "Dictionary::field_name";
inline constexpr std::string_view kEnumValue = "Dictionary::enum_values: EnumValueRef::value";
inline constexpr std::string_view kEnumDescription =
    "Dictionary::enum_values: EnumValueRef::description";
inline constexpr std::string_view kComponent = "Dictionary::component";
inline constexpr std::string_view kCompId = "ComponentRef::component_id";
inline constexpr std::string_view kCompNameOffset = "ComponentRef::name_offset";
inline constexpr std::string_view kCompFirstFieldIndex = "ComponentRef::first_field_index";
inline constexpr std::string_view kCompFieldCount = "ComponentRef::field_count";
inline constexpr std::string_view kCompParent = "ComponentRef::parent_component_id";
inline constexpr std::string_view kCompReserved = "ComponentRef::_reserved";
inline constexpr std::string_view kGroup = "Dictionary::group";
inline constexpr std::string_view kGroupNoTag = "GroupRef::no_tag";
inline constexpr std::string_view kGroupFirstFieldTag = "GroupRef::first_field_tag";
inline constexpr std::string_view kGroupFirstFieldIndex = "GroupRef::first_field_index";
inline constexpr std::string_view kGroupFieldCount = "GroupRef::field_count";
inline constexpr std::string_view kGroupParent = "GroupRef::parent_group_no_tag";
inline constexpr std::string_view kGroupReserved = "GroupRef::_reserved";

// The FieldRef fields, once per query that returns FieldRef runs.
struct fieldref_ids {
    std::string_view tag;
    std::string_view type;
    std::string_view rule;
    std::string_view condition_index;
    std::string_view group_no_tag;
    std::string_view component_index;
    std::string_view enum_table_index;
    std::string_view length_pair_data_tag;
    std::string_view reserved;
};
inline constexpr fieldref_ids kMessageFields{
    .tag = "Dictionary::message_fields: FieldRef::tag",
    .type = "Dictionary::message_fields: FieldRef::type",
    .rule = "Dictionary::message_fields: FieldRef::rule",
    .condition_index = "Dictionary::message_fields: FieldRef::condition_index",
    .group_no_tag = "Dictionary::message_fields: FieldRef::group_no_tag",
    .component_index = "Dictionary::message_fields: FieldRef::component_index",
    .enum_table_index = "Dictionary::message_fields: FieldRef::enum_table_index",
    .length_pair_data_tag = "Dictionary::message_fields: FieldRef::length_pair_data_tag",
    .reserved = "Dictionary::message_fields: FieldRef::_reserved"};
inline constexpr fieldref_ids kComponentFields{
    .tag = "Dictionary::component_fields: FieldRef::tag",
    .type = "Dictionary::component_fields: FieldRef::type",
    .rule = "Dictionary::component_fields: FieldRef::rule",
    .condition_index = "Dictionary::component_fields: FieldRef::condition_index",
    .group_no_tag = "Dictionary::component_fields: FieldRef::group_no_tag",
    .component_index = "Dictionary::component_fields: FieldRef::component_index",
    .enum_table_index = "Dictionary::component_fields: FieldRef::enum_table_index",
    .length_pair_data_tag = "Dictionary::component_fields: FieldRef::length_pair_data_tag",
    .reserved = "Dictionary::component_fields: FieldRef::_reserved"};
inline constexpr fieldref_ids kGroupFields{
    .tag = "Dictionary::group_fields: FieldRef::tag",
    .type = "Dictionary::group_fields: FieldRef::type",
    .rule = "Dictionary::group_fields: FieldRef::rule",
    .condition_index = "Dictionary::group_fields: FieldRef::condition_index",
    .group_no_tag = "Dictionary::group_fields: FieldRef::group_no_tag",
    .component_index = "Dictionary::group_fields: FieldRef::component_index",
    .enum_table_index = "Dictionary::group_fields: FieldRef::enum_table_index",
    .length_pair_data_tag = "Dictionary::group_fields: FieldRef::length_pair_data_tag",
    .reserved = "Dictionary::group_fields: FieldRef::_reserved"};

// table_view stores
inline constexpr std::string_view kValid = "table_view::valid_";
inline constexpr std::string_view kRequired = "table_view::required_";
inline constexpr std::string_view kGroupFirstBare = "table_view::group_first_";
inline constexpr std::string_view kGroupMembersBare = "table_view::group_members_";
inline constexpr std::string_view kGroupRequiredBare = "table_view::group_required_members_";
inline constexpr std::string_view kGroupBits = "table_view::group_bits_";
inline constexpr std::string_view kGroupCtxKeys = "table_view::group_ctx_ (keys)";
inline constexpr std::string_view kGroupCtxFirst =
    "table_view::group_ctx_: group_ctx_entry::group_first";
inline constexpr std::string_view kGroupCtxMembers =
    "table_view::group_ctx_: group_ctx_entry::members";
inline constexpr std::string_view kGroupCtxRequired =
    "table_view::group_ctx_: group_ctx_entry::required_members";
inline constexpr std::string_view kTypes = "table_view::types_";
inline constexpr std::string_view kEnumBits = "table_view::enum_bits_";
inline constexpr std::string_view kEnumsKeys = "table_view::enums_ (keys)";
inline constexpr std::string_view kEnumCodes = "table_view::enums_: enum_domain::codes";
inline constexpr std::string_view kEnumMulti = "table_view::enums_: enum_domain::multi_value";
inline constexpr std::string_view kEnumAllSingle =
    "table_view::enums_: enum_domain::all_single_char";
inline constexpr std::string_view kEnumMask = "table_view::enums_: enum_domain::single_char_mask";
inline constexpr std::string_view kFramingTags = "table_view::fixt_framing_tags_";
inline constexpr std::string_view kFramingTypes = "table_view::fixt_framing_types_";
inline constexpr std::string_view kPairForward = "table_view::length_pair_data_tag_";
inline constexpr std::string_view kPairReverse = "table_view::data_pair_length_tag_";
inline constexpr std::string_view kNonstandardPair = "table_view::has_nonstandard_pair_";

// members of table_view.hpp that hold no store
inline constexpr std::string_view kValidTagSetViewSet = "valid_tag_set_view::set_";
inline constexpr std::string_view kBuilderTv = "table_view_builder::tv_";
inline constexpr std::string_view kPopulationMethods = "table_view population methods";
inline constexpr std::string_view kEnumHelpers =
    "table_view::code_less_t, code_less, code_declared, next_token";
inline constexpr std::string_view kBitHelpers =
    "table_view::group_bit, set_group_bit, enum_bit, set_enum_bit";

// census entries
inline constexpr std::string_view kCensusComponentFields = "census: Dictionary::component_fields";
inline constexpr std::string_view kCensusGroupFields = "census: Dictionary::group_fields";
inline constexpr std::string_view kCensusComponentStart = "census: ComponentRef::first_field_index";
inline constexpr std::string_view kCensusGroupStart = "census: GroupRef::first_field_index";

// invariant checks (outside the expected-set union)
inline constexpr std::string_view kInvPairInverse =
    "invariant: data_pair_length_tag_ is the inverse of length_pair_data_tag_";
inline constexpr std::string_view kInvGroupBits =
    "invariant: every key of group_first_, group_members_, group_required_members_ and "
    "group_ctx_ has its group_bits_ bit";
inline constexpr std::string_view kInvEnumBits =
    "invariant: every key of enums_ has its enum_bits_ bit";
}  // namespace id

inline constexpr std::string_view kNeverAssignedReason =
    "both loaders leave it at zero: every FieldRef is built as `FieldRef fr{}` / "
    "`FieldRef no_fr{}` in `LoaderState::expand_field_list` (xml_loader.cpp) and "
    "`OrchestraLoaderState::expand_field_list` (orchestra_loader.cpp), and neither "
    "assigns this field; a seed cannot differ it in a valid dictionary";
inline constexpr std::string_view kVersionReason =
    "a function of `which_session_version()`: `Dictionary::as_table_view` fills it only "
    "for v50/v50sp1/v50sp2, from the fixed `detail::kFixtFramingTable`; FR-006 makes the "
    "two sides' versions equal";

inline constexpr std::array kStoreList{
    store_entry{id::kWhichSessionVersion, entry_kind::compared_unseeded,
                "set from the `<fix>` version attributes; FR-006 makes the two sides' "
                "versions equal, so a version-matched pair cannot differ it"},
    store_entry{id::kMsgType, entry_kind::seeded, ""},
    store_entry{id::kMsgName, entry_kind::seeded, ""},
    store_entry{id::kMessageFields.tag, entry_kind::seeded, ""},
    store_entry{id::kMessageFields.type, entry_kind::seeded, ""},
    store_entry{id::kMessageFields.rule, entry_kind::seeded, ""},
    store_entry{id::kMessageFields.condition_index, entry_kind::compared_unseeded,
                kNeverAssignedReason},
    store_entry{id::kMessageFields.group_no_tag, entry_kind::seeded, ""},
    store_entry{id::kMessageFields.component_index, entry_kind::seeded, ""},
    store_entry{id::kMessageFields.enum_table_index, entry_kind::compared_unseeded,
                kNeverAssignedReason},
    store_entry{id::kMessageFields.length_pair_data_tag, entry_kind::seeded, ""},
    store_entry{id::kMessageFields.reserved, entry_kind::compared_unseeded, kNeverAssignedReason},
    store_entry{id::kFieldRef, entry_kind::seeded, ""},
    store_entry{id::kField, entry_kind::seeded, ""},
    store_entry{id::kFieldValidFor, entry_kind::seeded, ""},
    store_entry{id::kRequiredFields, entry_kind::seeded, ""},
    store_entry{id::kGroupFirstField, entry_kind::seeded, ""},
    store_entry{id::kLengthPairDataTag, entry_kind::seeded, ""},
    store_entry{id::kFieldByName, entry_kind::seeded, ""},
    store_entry{id::kFieldName, entry_kind::seeded, ""},
    store_entry{id::kEnumValue, entry_kind::seeded, ""},
    store_entry{id::kEnumDescription, entry_kind::seeded, ""},
    store_entry{id::kComponent, entry_kind::seeded, ""},
    store_entry{id::kCompId, entry_kind::seeded, ""},
    store_entry{id::kCompNameOffset, entry_kind::seeded, ""},
    store_entry{id::kCompFirstFieldIndex, entry_kind::seeded, ""},
    store_entry{id::kCompFieldCount, entry_kind::seeded, ""},
    store_entry{id::kCompParent, entry_kind::seeded, ""},
    store_entry{id::kCompReserved, entry_kind::compared_unseeded,
                "both loaders' `finalize` write `cr._reserved = 0`"},
    store_entry{id::kGroup, entry_kind::seeded, ""},
    store_entry{id::kGroupNoTag, entry_kind::compared_unseeded,
                "`dict_metadata_handle::group_impl` returns only a GroupRef whose `no_tag` "
                "equals the queried key, so it cannot differ while both sides hold the group "
                "(a group one side lacks is `Dictionary::group`)"},
    store_entry{id::kGroupFirstFieldTag, entry_kind::seeded, ""},
    store_entry{id::kGroupFirstFieldIndex, entry_kind::seeded, ""},
    store_entry{id::kGroupFieldCount, entry_kind::seeded, ""},
    store_entry{id::kGroupParent, entry_kind::seeded, ""},
    store_entry{id::kGroupReserved, entry_kind::compared_unseeded,
                "both loaders' `finalize` write `gr._reserved = 0`"},
    store_entry{id::kComponentFields.tag, entry_kind::seeded, ""},
    store_entry{id::kComponentFields.type, entry_kind::seeded, ""},
    store_entry{id::kComponentFields.rule, entry_kind::seeded, ""},
    store_entry{id::kComponentFields.condition_index, entry_kind::compared_unseeded,
                kNeverAssignedReason},
    store_entry{id::kComponentFields.group_no_tag, entry_kind::seeded, ""},
    store_entry{id::kComponentFields.component_index, entry_kind::seeded, ""},
    store_entry{id::kComponentFields.enum_table_index, entry_kind::compared_unseeded,
                kNeverAssignedReason},
    store_entry{id::kComponentFields.length_pair_data_tag, entry_kind::seeded, ""},
    store_entry{id::kComponentFields.reserved, entry_kind::compared_unseeded, kNeverAssignedReason},
    store_entry{id::kGroupFields.tag, entry_kind::seeded, ""},
    store_entry{id::kGroupFields.type, entry_kind::seeded, ""},
    store_entry{id::kGroupFields.rule, entry_kind::seeded, ""},
    store_entry{id::kGroupFields.condition_index, entry_kind::compared_unseeded,
                kNeverAssignedReason},
    store_entry{id::kGroupFields.group_no_tag, entry_kind::seeded, ""},
    store_entry{id::kGroupFields.component_index, entry_kind::seeded, ""},
    store_entry{id::kGroupFields.enum_table_index, entry_kind::compared_unseeded,
                kNeverAssignedReason},
    store_entry{id::kGroupFields.length_pair_data_tag, entry_kind::seeded, ""},
    store_entry{id::kGroupFields.reserved, entry_kind::compared_unseeded, kNeverAssignedReason},
    store_entry{id::kValid, entry_kind::seeded, ""},
    store_entry{id::kRequired, entry_kind::seeded, ""},
    store_entry{id::kGroupFirstBare, entry_kind::seeded, ""},
    store_entry{id::kGroupMembersBare, entry_kind::seeded, ""},
    store_entry{id::kGroupRequiredBare, entry_kind::seeded, ""},
    store_entry{id::kGroupBits, entry_kind::seeded, ""},
    store_entry{id::kGroupCtxKeys, entry_kind::seeded, ""},
    store_entry{id::kGroupCtxFirst, entry_kind::seeded, ""},
    store_entry{id::kGroupCtxMembers, entry_kind::seeded, ""},
    store_entry{id::kGroupCtxRequired, entry_kind::seeded, ""},
    store_entry{id::kTypes, entry_kind::seeded, ""},
    store_entry{id::kEnumBits, entry_kind::seeded, ""},
    store_entry{id::kEnumsKeys, entry_kind::seeded, ""},
    store_entry{id::kEnumCodes, entry_kind::seeded, ""},
    store_entry{id::kEnumMulti, entry_kind::seeded, ""},
    store_entry{id::kEnumAllSingle, entry_kind::seeded, ""},
    store_entry{id::kEnumMask, entry_kind::seeded, ""},
    store_entry{id::kFramingTags, entry_kind::compared_unseeded, kVersionReason},
    store_entry{id::kFramingTypes, entry_kind::compared_unseeded, kVersionReason},
    store_entry{id::kPairForward, entry_kind::seeded, ""},
    store_entry{id::kPairReverse, entry_kind::seeded, ""},
    store_entry{id::kNonstandardPair, entry_kind::seeded, ""},
    store_entry{id::kValidTagSetViewSet, entry_kind::not_a_store,
                "a pointer into `table_view::valid_` that `valid_tags_for` hands out; it holds "
                "no store of its own, and `table_view::valid_` is compared"},
    store_entry{id::kBuilderTv, entry_kind::not_a_store,
                "the builder's scaffold, moved out by `build() &&`; the comparison reads the "
                "built `table_view`"},
    store_entry{id::kPopulationMethods, entry_kind::not_a_store,
                "the private mutators (`add_valid_tag` through `set_length_pair_data_tag`) "
                "write the stores; they hold none, and `table_view_builder` is their only "
                "caller"},
    store_entry{id::kEnumHelpers, entry_kind::not_a_store,
                "stateless helpers of `enum_valid`; they hold no store"},
    store_entry{id::kBitHelpers, entry_kind::not_a_store,
                "accessors of `group_bits_` and `enum_bits_`, which are compared"},
    store_entry{id::kCensusComponentFields, entry_kind::seeded, ""},
    store_entry{id::kCensusGroupFields, entry_kind::seeded, ""},
    store_entry{id::kCensusComponentStart, entry_kind::seeded, ""},
    store_entry{id::kCensusGroupStart, entry_kind::seeded, ""},
};

// ── the raw census ────────────────────────────────────────────────────────────

struct raw_census {
    std::vector<std::string> msg_types;                         // document order
    std::vector<std::pair<std::string, std::uint16_t>> fields;  // name → number, first kept
    std::vector<std::pair<std::string, std::vector<std::uint16_t>>> components;  // decl order
    std::map<std::uint16_t, std::vector<std::uint16_t>> groups;  // no_tag → first-seen node
    std::vector<std::string> notes;
};

namespace detail_census {

// Bounds the walk of a cyclic document; a valid one never reaches it.
inline constexpr int kMaxDepth = 64;

struct walker {
    std::unordered_map<std::string, std::uint16_t> by_name;
    std::unordered_map<std::string, pugi::xml_node> components;
    std::map<std::uint16_t, pugi::xml_node> group_nodes;
    std::vector<std::string>* notes = nullptr;

    void walk(pugi::xml_node const& parent, std::vector<std::uint16_t>& out, bool register_groups,
              int depth) {
        if (depth > kMaxDepth) {
            notes->push_back("census: walk deeper than kMaxDepth at <" +
                             std::string{parent.name()} + ">; stopped");
            return;
        }
        for (auto const& child : parent.children()) {
            std::string_view const kind{child.name()};
            std::string const name{child.attribute("name").as_string("")};
            if (kind == "field") {
                auto const it = by_name.find(name);
                if (it == by_name.end()) {
                    notes->push_back("census: <field name=\"" + name + "\"> is not declared");
                    continue;
                }
                out.push_back(it->second);
            } else if (kind == "component") {
                auto const it = components.find(name);
                if (it == components.end()) {
                    notes->push_back("census: <component name=\"" + name + "\"> is not defined");
                    continue;
                }
                walk(it->second, out, register_groups, depth + 1);
            } else if (kind == "group") {
                auto const it = by_name.find(name);
                if (it == by_name.end()) {
                    notes->push_back("census: <group name=\"" + name +
                                     "\"> has no <field> declaration");
                    continue;
                }
                out.push_back(it->second);
                if (register_groups) {
                    group_nodes.emplace(it->second, child);  // first-seen wins
                }
                walk(child, out, register_groups, depth + 1);
            }
        }
    }
};

}  // namespace detail_census

// Census of a QuickFIX-XML document. An unparseable document gives an empty census
// with a note.
[[nodiscard]] inline raw_census census_from_quickfix_xml(std::string_view xml) {
    raw_census c;
    pugi::xml_document doc;
    if (!doc.load_buffer(xml.data(), xml.size())) {
        c.notes.emplace_back("census: the document does not parse");
        return c;
    }
    auto const root = doc.child("fix");
    detail_census::walker w;
    w.notes = &c.notes;

    std::set<std::uint16_t> numbers;
    for (auto const& f : root.child("fields").children("field")) {
        std::string const name{f.attribute("name").as_string("")};
        auto const number = static_cast<std::uint16_t>(f.attribute("number").as_uint(0));
        if (!numbers.insert(number).second) {
            c.notes.push_back("census: duplicate <field number=\"" + std::to_string(number) +
                              "\">");
        }
        if (!w.by_name.emplace(name, number).second) {
            c.notes.push_back("census: duplicate <field name=\"" + name + "\">; the first is kept");
            continue;
        }
        c.fields.emplace_back(name, number);
    }

    std::vector<std::pair<std::string, pugi::xml_node>> comp_order;
    for (auto const& comp : root.child("components").children("component")) {
        std::string const name{comp.attribute("name").as_string("")};
        if (!w.components.emplace(name, comp).second) {
            c.notes.push_back("census: duplicate <component name=\"" + name +
                              "\">; the first is kept");
            continue;
        }
        comp_order.emplace_back(name, comp);
    }

    std::vector<std::pair<std::string, pugi::xml_node>> messages;
    std::set<std::string> seen_types;
    for (auto const& m : root.child("messages").children("message")) {
        std::string const mt{m.attribute("msgtype").as_string("")};
        if (!seen_types.insert(mt).second) {
            c.notes.push_back("census: msgtype \"" + mt +
                              "\" appears more than once; the loader's sort is not stable, so "
                              "which of them registers a shared group first is unspecified");
        }
        c.msg_types.push_back(mt);
        messages.emplace_back(mt, m);
    }

    // The message walk registers groups first, in finalize's order.
    std::ranges::stable_sort(messages, {}, &std::pair<std::string, pugi::xml_node>::first);
    auto const header = root.child("header");
    auto const trailer = root.child("trailer");
    for (auto const& [mt, node] : messages) {
        std::vector<std::uint16_t> scratch;
        w.walk(header, scratch, /*register_groups=*/true, 0);
        w.walk(node, scratch, /*register_groups=*/true, 0);
        w.walk(trailer, scratch, /*register_groups=*/true, 0);
    }
    for (auto const& [name, node] : comp_order) {
        std::vector<std::uint16_t> members;
        w.walk(node, members, /*register_groups=*/true, 0);
        c.components.emplace_back(name, std::move(members));
    }
    for (auto const& [no_tag, node] : w.group_nodes) {
        std::vector<std::uint16_t> members;
        w.walk(node, members, /*register_groups=*/false, 0);
        c.groups.emplace(no_tag, std::move(members));
    }
    return c;
}

namespace detail_census {

struct orchestra_walker {
    std::set<std::uint16_t> fields;
    std::unordered_map<std::uint16_t, pugi::xml_node> components;
    std::unordered_map<std::uint16_t, pugi::xml_node> groups;
    std::map<std::uint16_t, pugi::xml_node> group_nodes;
    std::vector<std::string>* notes = nullptr;

    void walk(pugi::xml_node const& parent, std::vector<std::uint16_t>& out, bool register_groups,
              int depth) {
        if (depth > kMaxDepth) {
            notes->push_back("census: walk deeper than kMaxDepth at <" +
                             std::string{parent.name()} + ">; stopped");
            return;
        }
        for (auto const& child : parent.children()) {
            std::string_view const kind{child.name()};
            auto const id = static_cast<std::uint16_t>(child.attribute("id").as_uint(0));
            if (kind == "fixr:fieldRef") {
                if (!fields.contains(id)) {
                    notes->push_back("census: <fixr:fieldRef id=\"" + std::to_string(id) +
                                     "\"> is not declared");
                    continue;
                }
                out.push_back(id);
            } else if (kind == "fixr:componentRef") {
                auto const it = components.find(id);
                if (it == components.end()) {
                    notes->push_back("census: <fixr:componentRef id=\"" + std::to_string(id) +
                                     "\"> is not defined");
                    continue;
                }
                walk(it->second, out, register_groups, depth + 1);
            } else if (kind == "fixr:groupRef") {
                auto const it = groups.find(id);
                if (it == groups.end()) {
                    notes->push_back("census: <fixr:groupRef id=\"" + std::to_string(id) +
                                     "\"> is not defined");
                    continue;
                }
                auto const no_tag = static_cast<std::uint16_t>(
                    it->second.child("fixr:numInGroup").attribute("id").as_uint(0));
                if (!fields.contains(no_tag)) {
                    notes->push_back("census: <fixr:group id=\"" + std::to_string(id) +
                                     "\"> has no declared fixr:numInGroup");
                    continue;
                }
                out.push_back(no_tag);
                if (register_groups) {
                    group_nodes.emplace(no_tag, it->second);  // first-seen wins
                }
                walk(it->second, out, register_groups, depth + 1);
            }
        }
    }
};

}  // namespace detail_census

// Census of an Orchestra document. An unparseable document gives an empty census
// with a note.
[[nodiscard]] inline raw_census census_from_orchestra(std::string_view xml) {
    raw_census c;
    pugi::xml_document doc;
    if (!doc.load_buffer(xml.data(), xml.size())) {
        c.notes.emplace_back("census: the document does not parse");
        return c;
    }
    auto const root = doc.child("fixr:repository");
    detail_census::orchestra_walker w;
    w.notes = &c.notes;

    std::set<std::string> names;
    for (auto const& f : root.child("fixr:fields").children("fixr:field")) {
        std::string const name{f.attribute("name").as_string("")};
        auto const id = static_cast<std::uint16_t>(f.attribute("id").as_uint(0));
        if (!w.fields.insert(id).second) {
            c.notes.push_back("census: duplicate <fixr:field id=\"" + std::to_string(id) + "\">");
            continue;
        }
        if (!names.insert(name).second) {
            c.notes.push_back("census: duplicate <fixr:field name=\"" + name +
                              "\">; the first is kept");
            continue;
        }
        c.fields.emplace_back(name, id);
    }

    std::vector<std::pair<std::string, pugi::xml_node>> comp_order;
    std::set<std::string> comp_names;
    for (auto const& comp : root.child("fixr:components").children("fixr:component")) {
        std::string const name{comp.attribute("name").as_string("")};
        auto const id = static_cast<std::uint16_t>(comp.attribute("id").as_uint(0));
        if (!w.components.emplace(id, comp).second) {
            c.notes.push_back("census: duplicate <fixr:component id=\"" + std::to_string(id) +
                              "\">; the first is kept");
            continue;
        }
        if (!comp_names.insert(name).second) {
            c.notes.push_back("census: duplicate <fixr:component name=\"" + name + "\">");
        }
        comp_order.emplace_back(name, comp);
    }
    for (auto const& g : root.child("fixr:groups").children("fixr:group")) {
        auto const id = static_cast<std::uint16_t>(g.attribute("id").as_uint(0));
        if (!w.groups.emplace(id, g).second) {
            c.notes.push_back("census: duplicate <fixr:group id=\"" + std::to_string(id) +
                              "\">; the first is kept");
        }
    }

    std::vector<std::pair<std::string, pugi::xml_node>> messages;
    std::set<std::string> seen_types;
    for (auto const& m : root.child("fixr:messages").children("fixr:message")) {
        std::string const mt{m.attribute("msgType").as_string("")};
        if (!seen_types.insert(mt).second) {
            c.notes.push_back("census: msgType \"" + mt +
                              "\" appears more than once; the loader's sort is not stable, so "
                              "which of them registers a shared group first is unspecified");
        }
        c.msg_types.push_back(mt);
        messages.emplace_back(mt, m.child("fixr:structure"));
    }

    // The message walk registers groups first, in finalize's order.
    std::ranges::stable_sort(messages, {}, &std::pair<std::string, pugi::xml_node>::first);
    for (auto const& [mt, structure] : messages) {
        std::vector<std::uint16_t> scratch;
        w.walk(structure, scratch, /*register_groups=*/true, 0);
    }
    for (auto const& [name, node] : comp_order) {
        std::vector<std::uint16_t> members;
        w.walk(node, members, /*register_groups=*/true, 0);
        c.components.emplace_back(name, std::move(members));
    }
    for (auto const& [no_tag, node] : w.group_nodes) {
        std::vector<std::uint16_t> members;
        w.walk(node, members, /*register_groups=*/false, 0);
        c.groups.emplace(no_tag, std::move(members));
    }
    return c;
}

// ── the report ───────────────────────────────────────────────────────────────

struct difference {
    std::string_view entry;
    std::string detail;
};

struct invariant_violation {
    std::string_view invariant;
    std::string detail;
};

struct comparison_report {
    std::vector<difference> differences;
    std::vector<invariant_violation> invariant_violations;
    std::vector<std::string> notes;
    std::size_t census_compared = 0;
    std::size_t census_excluded = 0;

    [[nodiscard]] std::set<std::string_view> reported_entries() const {
        std::set<std::string_view> out;
        for (auto const& d : differences) {
            out.insert(d.entry);
        }
        return out;
    }

    [[nodiscard]] std::string describe() const {
        std::ostringstream os;
        for (auto const& d : differences) {
            os << "  [" << d.entry << "] " << d.detail << '\n';
        }
        for (auto const& v : invariant_violations) {
            os << "  [" << v.invariant << "] " << v.detail << '\n';
        }
        for (auto const& n : notes) {
            os << "  note: " << n << '\n';
        }
        os << "  census: compared " << census_compared << ", excluded " << census_excluded << '\n';
        return os.str();
    }

    void add(std::string_view entry, std::string detail) {
        differences.push_back({.entry = entry, .detail = std::move(detail)});
    }
};

// ── the invariant checks ─────────────────────────────────────────────────────

using tva = table_view_test_access;

[[nodiscard]] inline bool bit_set(tva::bit_store const& bits, std::uint16_t key) noexcept {
    std::size_t const w = static_cast<std::size_t>(key) >> 6U;
    return w < bits.size() && ((bits[w] >> (static_cast<std::size_t>(key) & 63U)) & 1U) != 0U;
}

// Every forward pair has its inverse, and every reverse pair its forward.
[[nodiscard]] inline std::vector<std::string> check_pair_inverse(tva::pair_store const& forward,
                                                                 tva::pair_store const& reverse) {
    std::vector<std::string> out;
    for (auto const& [length, data] : forward) {
        auto const it = reverse.find(data);
        if (it == reverse.end() || it->second != length) {
            out.push_back("forward " + std::to_string(length) + " -> " + std::to_string(data) +
                          " has no reverse " + std::to_string(data) + " -> " +
                          std::to_string(length));
        }
    }
    for (auto const& [data, length] : reverse) {
        auto const it = forward.find(length);
        if (it == forward.end() || it->second != data) {
            out.push_back("reverse " + std::to_string(data) + " -> " + std::to_string(length) +
                          " has no forward " + std::to_string(length) + " -> " +
                          std::to_string(data));
        }
    }
    return out;
}

// The group stores contracts W-0 names: group_first_, group_members_,
// group_required_members_, group_ctx_.
[[nodiscard]] inline std::vector<std::string> check_group_bits(
    tva::bit_store const& bits, tva::group_first_store const& group_first,
    tva::group_members_store const& group_members, tva::group_required_store const& group_required,
    tva::group_ctx_store const& group_ctx) {
    std::vector<std::string> out;
    for (auto const& [no_tag, first] : group_first) {
        if (!bit_set(bits, no_tag)) {
            out.push_back("group_first_ key " + std::to_string(no_tag) + " has a clear bit");
        }
    }
    for (auto const& [no_tag, members] : group_members) {
        if (!bit_set(bits, no_tag)) {
            out.push_back("group_members_ key " + std::to_string(no_tag) + " has a clear bit");
        }
    }
    for (auto const& [no_tag, required] : group_required) {
        if (!bit_set(bits, no_tag)) {
            out.push_back("group_required_members_ key " + std::to_string(no_tag) +
                          " has a clear bit");
        }
    }
    for (auto const& [key, entry] : group_ctx) {
        if (!bit_set(bits, key.no_tag)) {
            out.push_back("group_ctx_ key (" + key.msg_type + ", no_tag " +
                          std::to_string(key.no_tag) + ") has a clear bit");
        }
    }
    return out;
}

[[nodiscard]] inline std::vector<std::string> check_enum_bits(tva::bit_store const& bits,
                                                              tva::enums_store const& enums) {
    std::vector<std::string> out;
    for (auto const& [tag, domain] : enums) {
        if (!bit_set(bits, tag)) {
            out.push_back("enums_ key " + std::to_string(tag) + " has a clear bit");
        }
    }
    return out;
}

// ── the comparison ───────────────────────────────────────────────────────────

struct side {
    Dictionary const& dict;
    raw_census const& census;
};

namespace detail_compare {

inline std::string join(std::span<std::uint16_t const> tags) {
    std::string s = "[";
    for (std::size_t i = 0; i < tags.size(); ++i) {
        s += (i == 0 ? "" : ",") + std::to_string(tags[i]);
    }
    return s + "]";
}

inline std::vector<std::uint16_t> tags_of(std::span<FieldRef const> run) {
    std::vector<std::uint16_t> out;
    out.reserve(run.size());
    for (auto const& f : run) {
        out.push_back(f.tag);
    }
    return out;
}

inline bool fieldref_equal(FieldRef const& a, FieldRef const& b) noexcept {
    return a.tag == b.tag && a.type == b.type && a.rule == b.rule &&
           a.condition_index == b.condition_index && a.group_no_tag == b.group_no_tag &&
           a.component_index == b.component_index && a.enum_table_index == b.enum_table_index &&
           a.length_pair_data_tag == b.length_pair_data_tag && a._reserved == b._reserved;
}

// A FieldRef run: the tag sequence first; each other field only where the tag
// sequences agree, position by position.
inline void compare_runs(comparison_report& r, id::fieldref_ids const& ids,
                         std::string const& where, std::span<FieldRef const> a,
                         std::span<FieldRef const> b) {
    auto const ta = tags_of(a);
    auto const tb = tags_of(b);
    if (ta != tb) {
        r.add(ids.tag, where + ": " + join(ta) + " vs " + join(tb));
        return;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        auto const& x = a[i];
        auto const& y = b[i];
        std::string const at = where + " tag " + std::to_string(x.tag);
        auto check = [&](std::string_view entry, auto lhs, auto rhs) {
            if (lhs != rhs) {
                r.add(entry, at + ": " + std::to_string(static_cast<long long>(lhs)) + " vs " +
                                 std::to_string(static_cast<long long>(rhs)));
            }
        };
        check(ids.type, x.type, y.type);
        check(ids.rule, x.rule, y.rule);
        check(ids.condition_index, x.condition_index, y.condition_index);
        check(ids.group_no_tag, x.group_no_tag, y.group_no_tag);
        check(ids.component_index, x.component_index, y.component_index);
        check(ids.enum_table_index, x.enum_table_index, y.enum_table_index);
        check(ids.length_pair_data_tag, x.length_pair_data_tag, y.length_pair_data_tag);
        check(ids.reserved, x._reserved, y._reserved);
    }
}

inline std::set<std::uint16_t> bits_of(tva::bit_store const& bits) {
    std::set<std::uint16_t> out;
    for (std::size_t w = 0; w < bits.size(); ++w) {
        for (std::size_t b = 0; b < 64; ++b) {
            if (((bits[w] >> b) & 1U) != 0U) {
                out.insert(static_cast<std::uint16_t>(w * 64 + b));
            }
        }
    }
    return out;
}

inline std::string ctx_key_text(group_ctx_key const& k) {
    std::string s = "(" + k.msg_type + ", [";
    for (std::uint8_t i = 0; i < k.depth; ++i) {
        s += (i == 0 ? "" : ",") + std::to_string(k.parent_path[i]);
    }
    return s + "], " + std::to_string(k.no_tag) + ")";
}

// Map stores with equality-comparable values: one difference per key that differs
// or that one side lacks.
template <class Map, class Render>
void compare_map(comparison_report& r, std::string_view entry, Map const& a, Map const& b,
                 Render render_key) {
    for (auto const& [k, v] : a) {
        auto const it = b.find(k);
        if (it == b.end()) {
            r.add(entry, "key " + render_key(k) + " only on side A");
        } else if (!(it->second == v)) {
            r.add(entry, "key " + render_key(k) + " differs");
        }
    }
    for (auto const& [k, v] : b) {
        if (a.find(k) == a.end()) {
            r.add(entry, "key " + render_key(k) + " only on side B");
        }
    }
}

inline std::string num_key(std::uint16_t k) { return std::to_string(k); }
inline std::string str_key(std::string const& k) { return "\"" + k + "\""; }

inline void compare_table_views(comparison_report& r, table_view const& a, table_view const& b) {
    compare_map(r, id::kValid, tva::valid(a), tva::valid(b), str_key);
    compare_map(r, id::kRequired, tva::required(a), tva::required(b), str_key);
    compare_map(r, id::kGroupFirstBare, tva::group_first(a), tva::group_first(b), num_key);
    compare_map(r, id::kGroupMembersBare, tva::group_members(a), tva::group_members(b), num_key);
    compare_map(r, id::kGroupRequiredBare, tva::group_required_members(a),
                tva::group_required_members(b), num_key);
    if (bits_of(tva::group_bits(a)) != bits_of(tva::group_bits(b))) {
        r.add(id::kGroupBits, "set bits differ");
    }

    auto const& ca = tva::group_ctx(a);
    auto const& cb = tva::group_ctx(b);
    for (auto const& [k, e] : ca) {
        auto const it = cb.find(k);
        if (it == cb.end()) {
            r.add(id::kGroupCtxKeys, ctx_key_text(k) + " only on side A");
            continue;
        }
        if (e.group_first != it->second.group_first) {
            r.add(id::kGroupCtxFirst, ctx_key_text(k) + ": " + std::to_string(e.group_first) +
                                          " vs " + std::to_string(it->second.group_first));
        }
        if (e.members != it->second.members) {
            r.add(id::kGroupCtxMembers,
                  ctx_key_text(k) + ": " + join(e.members) + " vs " + join(it->second.members));
        }
        if (e.required_members != it->second.required_members) {
            r.add(id::kGroupCtxRequired, ctx_key_text(k) + ": " + join(e.required_members) +
                                             " vs " + join(it->second.required_members));
        }
    }
    for (auto const& [k, e] : cb) {
        if (ca.find(k) == ca.end()) {
            r.add(id::kGroupCtxKeys, ctx_key_text(k) + " only on side B");
        }
    }

    compare_map(r, id::kTypes, tva::types(a), tva::types(b), num_key);
    if (bits_of(tva::enum_bits(a)) != bits_of(tva::enum_bits(b))) {
        r.add(id::kEnumBits, "set bits differ");
    }
    auto const& ea = tva::enums(a);
    auto const& eb = tva::enums(b);
    for (auto const& [tag, d] : ea) {
        auto const it = eb.find(tag);
        if (it == eb.end()) {
            r.add(id::kEnumsKeys, std::to_string(tag) + " only on side A");
            continue;
        }
        std::string const at = "tag " + std::to_string(tag);
        if (d.codes != it->second.codes) {
            r.add(id::kEnumCodes, at);
        }
        if (d.multi_value != it->second.multi_value) {
            r.add(id::kEnumMulti, at);
        }
        if (d.all_single_char != it->second.all_single_char) {
            r.add(id::kEnumAllSingle, at);
        }
        if (d.single_char_mask != it->second.single_char_mask) {
            r.add(id::kEnumMask, at);
        }
    }
    for (auto const& [tag, d] : eb) {
        if (ea.find(tag) == ea.end()) {
            r.add(id::kEnumsKeys, std::to_string(tag) + " only on side B");
        }
    }

    if (tva::fixt_framing_tags(a) != tva::fixt_framing_tags(b)) {
        r.add(id::kFramingTags, "sets differ");
    }
    compare_map(r, id::kFramingTypes, tva::fixt_framing_types(a), tva::fixt_framing_types(b),
                num_key);
    compare_map(r, id::kPairForward, tva::length_pair_data_tag(a), tva::length_pair_data_tag(b),
                num_key);
    compare_map(r, id::kPairReverse, tva::data_pair_length_tag(a), tva::data_pair_length_tag(b),
                num_key);
    if (tva::has_nonstandard_pair(a) != tva::has_nonstandard_pair(b)) {
        r.add(id::kNonstandardPair, std::string{tva::has_nonstandard_pair(a) ? "true" : "false"} +
                                        " vs " + (tva::has_nonstandard_pair(b) ? "true" : "false"));
    }
}

inline void run_invariants(comparison_report& r, char const* side_name, table_view const& tv) {
    for (auto& v :
         check_pair_inverse(tva::length_pair_data_tag(tv), tva::data_pair_length_tag(tv))) {
        r.invariant_violations.push_back({id::kInvPairInverse, std::string{side_name} + ": " + v});
    }
    for (auto& v :
         check_group_bits(tva::group_bits(tv), tva::group_first(tv), tva::group_members(tv),
                          tva::group_required_members(tv), tva::group_ctx(tv))) {
        r.invariant_violations.push_back({id::kInvGroupBits, std::string{side_name} + ": " + v});
    }
    for (auto& v : check_enum_bits(tva::enum_bits(tv), tva::enums(tv))) {
        r.invariant_violations.push_back({id::kInvEnumBits, std::string{side_name} + ": " + v});
    }
}

// Census comparison of one side with its own census.
inline void compare_with_census(comparison_report& r, char const* side_name, Dictionary const& d,
                                raw_census const& c) {
    std::string const who{side_name};
    std::size_t start = 0;
    for (auto const& [name, members] : c.components) {
        std::size_t const count = members.size();
        if (start > 0xFFFFU || count > 0xFFFFU) {
            r.notes.push_back(who + ": wrap case, component \"" + name + "\" not compared");
            ++r.census_excluded;
            start += count;
            continue;
        }
        ++r.census_compared;
        auto const cr = d.component(name);
        if (!cr) {
            r.add(id::kCensusComponentFields,
                  who + ": component \"" + name + "\" is in the census, not in the dictionary");
        } else {
            if (cr->first_field_index != start) {
                r.add(id::kCensusComponentStart, who + ": component \"" + name + "\": loader " +
                                                     std::to_string(cr->first_field_index) +
                                                     ", census " + std::to_string(start));
            }
            auto const got = tags_of(d.component_fields(name));
            if (got != members) {
                r.add(id::kCensusComponentFields, who + ": component \"" + name + "\": loader " +
                                                      join(got) + ", census " + join(members));
            }
        }
        start += count;
    }
    start = 0;
    for (auto const& [no_tag, members] : c.groups) {
        std::size_t const count = members.size();
        if (start > 0xFFFFU || count > 0xFFFFU) {
            r.notes.push_back(who + ": wrap case, group " + std::to_string(no_tag) +
                              " not compared");
            ++r.census_excluded;
            start += count;
            continue;
        }
        ++r.census_compared;
        auto const gr = d.group(no_tag);
        if (!gr) {
            r.add(id::kCensusGroupFields, who + ": group " + std::to_string(no_tag) +
                                              " is in the census, not in the dictionary");
        } else {
            if (gr->first_field_index != start) {
                r.add(id::kCensusGroupStart, who + ": group " + std::to_string(no_tag) +
                                                 ": loader " +
                                                 std::to_string(gr->first_field_index) +
                                                 ", census " + std::to_string(start));
            }
            auto const got = tags_of(d.group_fields(no_tag));
            if (got != members) {
                r.add(id::kCensusGroupFields, who + ": group " + std::to_string(no_tag) +
                                                  ": loader " + join(got) + ", census " +
                                                  join(members));
            }
        }
        start += count;
    }
}

}  // namespace detail_compare

[[nodiscard]] inline comparison_report compare(side const& a, side const& b) {
    using namespace detail_compare;
    comparison_report r;
    for (auto const& n : a.census.notes) {
        r.notes.push_back("side A " + n);
    }
    for (auto const& n : b.census.notes) {
        r.notes.push_back("side B " + n);
    }
    auto const* ha = dictionary_test_access::handle(a.dict);
    auto const* hb = dictionary_test_access::handle(b.dict);

    // ---- key sources ----
    std::set<std::string> msg_types;
    std::set<std::uint16_t> tags;
    std::set<std::string> field_names;
    std::set<std::string> comp_names;
    std::set<std::uint16_t> pair_tags;  // tags with a non-zero FieldRef pair in fields_
    for (raw_census const* c : {&a.census, &b.census}) {
        msg_types.insert(c->msg_types.begin(), c->msg_types.end());
        for (auto const& [name, number] : c->fields) {
            field_names.insert(name);
            tags.insert(number);
        }
        for (auto const& [name, members] : c->components) {
            comp_names.insert(name);
        }
        for (auto const& [no_tag, members] : c->groups) {
            tags.insert(no_tag);
        }
    }
    for (auto const* h : {ha, hb}) {
        if (h == nullptr) {
            continue;
        }
        for (auto const& m : h->messages_) {
            msg_types.emplace(m.msg_type);
        }
        for (auto const& e : h->field_by_name_) {
            field_names.emplace(h->name_at(e.name));
            tags.insert(e.tag);
        }
        for (auto const& ni : h->components_by_name_) {
            comp_names.emplace(h->name_at(ni.name));
        }
        for (auto const& g : h->groups_) {
            tags.insert(g.no_tag);
        }
        for (auto const& run : h->enum_runs_) {
            tags.insert(run.tag);
        }
        for (auto const& f : h->fields_) {
            tags.insert(f.tag);
            // `length_pair_data_tag_impl` answers non-zero only for a tag some
            // FieldRef of `fields_` carries a pair for, so these are its only keys.
            if (f.length_pair_data_tag != 0) {
                pair_tags.insert(f.tag);
            }
        }
    }

    // ---- Dictionary queries ----
    if (a.dict.which_session_version() != b.dict.which_session_version()) {
        r.add(id::kWhichSessionVersion,
              std::to_string(static_cast<int>(a.dict.which_session_version())) + " vs " +
                  std::to_string(static_cast<int>(b.dict.which_session_version())));
    }

    std::map<std::string, std::vector<std::string>> names_a;
    std::map<std::string, std::vector<std::string>> names_b;
    std::vector<std::string> types_a;
    std::vector<std::string> types_b;
    for (auto const& m : a.dict.messages()) {
        types_a.emplace_back(m.msg_type);
        names_a[std::string{m.msg_type}].emplace_back(m.name);
    }
    for (auto const& m : b.dict.messages()) {
        types_b.emplace_back(m.msg_type);
        names_b[std::string{m.msg_type}].emplace_back(m.name);
    }
    if (types_a != types_b) {
        r.add(id::kMsgType, "msgtype sequences differ");
    }
    for (auto const& [mt, na] : names_a) {
        auto const it = names_b.find(mt);
        if (it != names_b.end() && it->second != na) {
            r.add(id::kMsgName, "msgtype \"" + mt + "\"");
        }
    }

    for (auto const& mt : msg_types) {
        std::string const where = "msgtype \"" + mt + "\"";
        compare_runs(r, id::kMessageFields, where, a.dict.message_fields(mt),
                     b.dict.message_fields(mt));
        auto const ra = a.dict.required_fields(mt);
        auto const rb = b.dict.required_fields(mt);
        if (!std::ranges::equal(ra, rb)) {
            r.add(id::kRequiredFields, where + ": " + join(ra) + " vs " + join(rb));
        }
        for (auto const tag : tags) {
            std::string const at = where + " tag " + std::to_string(tag);
            if (!fieldref_equal(a.dict.field_ref(mt, tag), b.dict.field_ref(mt, tag))) {
                r.add(id::kFieldRef, at);
            }
            auto const fa = a.dict.field(mt, tag);
            auto const fb = b.dict.field(mt, tag);
            if (fa.has_value() != fb.has_value() || (fa && !fieldref_equal(*fa, *fb))) {
                r.add(id::kField, at);
            }
            if (a.dict.field_valid_for(mt, tag) != b.dict.field_valid_for(mt, tag)) {
                r.add(id::kFieldValidFor, at);
            }
        }
    }

    for (auto const tag : tags) {
        std::string const at = "tag " + std::to_string(tag);
        if (a.dict.group_first_field(tag) != b.dict.group_first_field(tag)) {
            r.add(id::kGroupFirstField, at + ": " + std::to_string(a.dict.group_first_field(tag)) +
                                            " vs " + std::to_string(b.dict.group_first_field(tag)));
        }
        if (a.dict.field_name(tag) != b.dict.field_name(tag)) {
            r.add(id::kFieldName, at);
        }
        auto const va = a.dict.enum_values(tag);
        auto const vb = b.dict.enum_values(tag);
        std::vector<std::string_view> vals_a;
        std::vector<std::string_view> vals_b;
        for (auto const& e : va) {
            vals_a.push_back(e.value);
        }
        for (auto const& e : vb) {
            vals_b.push_back(e.value);
        }
        if (vals_a != vals_b) {
            r.add(id::kEnumValue, at);
        }
        for (auto const& x : va) {  // descriptions, aligned by value
            for (auto const& y : vb) {
                if (x.value == y.value && x.description != y.description) {
                    r.add(id::kEnumDescription, at + " value \"" + std::string{x.value} + "\"");
                }
            }
        }
        auto const ga = a.dict.group(tag);
        auto const gb = b.dict.group(tag);
        if (ga.has_value() != gb.has_value()) {
            r.add(id::kGroup, at);
        } else if (ga) {
            auto check = [&](std::string_view entry, std::uint16_t x, std::uint16_t y) {
                if (x != y) {
                    r.add(entry, at + ": " + std::to_string(x) + " vs " + std::to_string(y));
                }
            };
            check(id::kGroupNoTag, ga->no_tag, gb->no_tag);
            check(id::kGroupFirstFieldTag, ga->first_field_tag, gb->first_field_tag);
            check(id::kGroupFirstFieldIndex, ga->first_field_index, gb->first_field_index);
            check(id::kGroupFieldCount, ga->field_count, gb->field_count);
            check(id::kGroupParent, ga->parent_group_no_tag, gb->parent_group_no_tag);
            check(id::kGroupReserved, ga->_reserved, gb->_reserved);
        }
        compare_runs(r, id::kGroupFields, "group " + std::to_string(tag), a.dict.group_fields(tag),
                     b.dict.group_fields(tag));
    }
    for (auto const tag : pair_tags) {
        if (a.dict.length_pair_data_tag(tag) != b.dict.length_pair_data_tag(tag)) {
            r.add(id::kLengthPairDataTag, "tag " + std::to_string(tag));
        }
    }

    for (auto const& name : field_names) {
        if (a.dict.field_by_name(name) != b.dict.field_by_name(name)) {
            r.add(id::kFieldByName, "\"" + name + "\"");
        }
    }

    for (auto const& name : comp_names) {
        std::string const at = "component \"" + name + "\"";
        auto const ca = a.dict.component(name);
        auto const cb = b.dict.component(name);
        if (ca.has_value() != cb.has_value()) {
            r.add(id::kComponent, at);
        } else if (ca) {
            auto check = [&](std::string_view entry, std::uint16_t x, std::uint16_t y) {
                if (x != y) {
                    r.add(entry, at + ": " + std::to_string(x) + " vs " + std::to_string(y));
                }
            };
            check(id::kCompId, ca->component_id, cb->component_id);
            check(id::kCompNameOffset, ca->name_offset, cb->name_offset);
            check(id::kCompFirstFieldIndex, ca->first_field_index, cb->first_field_index);
            check(id::kCompFieldCount, ca->field_count, cb->field_count);
            check(id::kCompParent, ca->parent_component_id, cb->parent_component_id);
            check(id::kCompReserved, ca->_reserved, cb->_reserved);
        }
        compare_runs(r, id::kComponentFields, at, a.dict.component_fields(name),
                     b.dict.component_fields(name));
    }

    // ---- table_view stores, and the invariants on each side ----
    table_view const tva_view = a.dict.as_table_view();
    table_view const tvb_view = b.dict.as_table_view();
    compare_table_views(r, tva_view, tvb_view);
    run_invariants(r, "side A", tva_view);
    run_invariants(r, "side B", tvb_view);

    // ---- each side against its census ----
    compare_with_census(r, "side A", a.dict, a.census);
    compare_with_census(r, "side B", b.dict, b.census);
    return r;
}

}  // namespace fixpp::dict::equivalence
