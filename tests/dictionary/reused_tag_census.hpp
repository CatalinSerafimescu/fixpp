// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/reused_tag_census.hpp — 063 shared census support
//
// Extracted from reused_tag_census_test.cpp (T016) so
// collision_membership_guards_test.cpp (T017 / SC-002 completeness fix) can
// derive its parameterized guard cases from the SAME loader-faithful
// membership computation the census uses, instead of a second hand-rolled
// copy that could silently drift from what the census actually finds.
//
// Mirrors Dictionary::as_table_view()'s Defect-A membership derivation
// (src/dictionary/dictionary.cpp:369-422) field-for-field — see
// reused_tag_census_test.cpp's file header for the full rationale.
//
// 082-structural-group-detection T008 (FR-018): `census_for`'s own group-tag
// gate is re-pointed from the datatype test (`fr.type ==
// field_data_type::NumInGroup`) onto `required_scope_oracle.hpp`'s T005
// group-tag census (`DictOracle::group_tags`) — the from-scratch,
// reachability-restricted structural set (contracts/group-detection.md C1/
// C2), passed in by the caller. NOT `Dictionary::group_first_field()`: that
// accessor's backing `groups_` table is populated once per DECLARED
// `<component>` regardless of message-reachability (xml_loader.cpp:954-970),
// so in isolation it is the unrestricted *struct* set (confirmed
// empirically: it disagrees with the oracle on FIX50/FIX50SP1 by exactly
// {384, 627}, the two declared-but-unreachable groups C2 documents), and it
// also still carries the not-yet-fixed component-recursion gap on FIX50SP2
// (differs by {1499, 1669, 1919} until T012b lands) — using it here would
// launder that pre-existing defect into this census's witness rather than
// exposing it. The independent oracle keeps this census non-circular per the
// FR-018/D-6 single-oracle guarantee: it is unaffected by both the T023
// predicate change AND the T012b loader fix.

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/field_ref.hpp>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace fixpp_test_support {

using fixpp::dict::Dictionary;

// One (msg_type, outer-to-inner parent-no_tag path) coordinate.
struct GroupContext {
    std::string msg_type;
    std::vector<std::uint16_t> path;
};

// A distinct member-set observed for a given no_tag, plus one representative
// context that produced it and how many distinct (msg_type,path) contexts
// share this exact set.
struct Variant {
    std::vector<std::uint16_t> members;  // sorted
    GroupContext example_context;
    std::size_t context_count = 0;
};

struct DictCensus {
    std::string name;
    // no_tag -> distinct member-set variants observed anywhere in the dict.
    std::map<std::uint16_t, std::vector<Variant>> per_tag;
    // Total distinct (msg_type, path, no_tag) triples registered — mirrors
    // exactly what Dictionary::as_table_view() would insert into
    // table_view::group_ctx_ (the census-derived table-size report).
    std::size_t total_context_entries = 0;
};

// Mirrors Dictionary::as_table_view()'s Defect-A membership derivation
// (src/dictionary/dictionary.cpp:369-422) field-for-field, except the
// group-tag gate itself: `group_tags` (082 T005/T008) replaces the datatype
// test with the independent oracle's reachability-restricted structural set
// — see the file header for why.
inline DictCensus census_for(Dictionary const& dict, std::string name,
                              std::set<std::uint16_t> const& group_tags) {
    DictCensus dc;
    dc.name = std::move(name);

    // Distinct (msg_type,path,no_tag) triples seen (for total_context_entries).
    std::set<std::tuple<std::string, std::vector<std::uint16_t>, std::uint16_t>> seen_contexts;

    for (auto const& msg_entry : dict.messages()) {
        auto const mt = msg_entry.msg_type;
        auto const all_fields = dict.message_fields(mt);

        std::unordered_map<std::uint16_t, std::uint16_t> immediate_parent;
        for (auto const& fr : all_fields) {
            if (group_tags.contains(fr.tag)) {
                immediate_parent[fr.tag] = fr.group_no_tag;
            }
        }

        for (auto const& fr : all_fields) {
            if (!group_tags.contains(fr.tag)) {
                continue;
            }
            std::uint16_t const no_tag = fr.tag;

            std::vector<std::uint16_t> members;
            for (auto const& m : all_fields) {
                if (m.group_no_tag == no_tag) {
                    members.push_back(m.tag);
                }
            }
            if (members.empty()) {
                continue;  // not a real group in this message
            }
            std::sort(members.begin(), members.end());

            std::vector<std::uint16_t> path;
            std::uint16_t cur = fr.group_no_tag;
            while (cur != 0) {
                path.push_back(cur);
                auto const pit = immediate_parent.find(cur);
                cur = (pit != immediate_parent.end()) ? pit->second : std::uint16_t{0};
            }
            std::reverse(path.begin(), path.end());

            auto const ctx_key = std::make_tuple(std::string{mt}, path, no_tag);
            if (seen_contexts.insert(ctx_key).second) {
                ++dc.total_context_entries;
            }

            auto& variants = dc.per_tag[no_tag];
            bool matched = false;
            for (auto& v : variants) {
                if (v.members == members) {
                    ++v.context_count;
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                variants.push_back(Variant{members, GroupContext{std::string{mt}, path}, 1});
            }
        }
    }
    return dc;
}

// The nine runtime-loadable dict XMLs (research.md census methodology).
// Shared so the census and its derived collision guards always enumerate
// exactly the same dict set.
inline constexpr std::array<std::string_view, 9> kRuntimeDicts{
    "FIX40.xml", "FIX41.xml", "FIX42.xml",   "FIX43.xml",    "FIX44.xml",
    "FIX50.xml", "FIX50SP1.xml", "FIX50SP2.xml", "FIXT11.xml",
};

}  // namespace fixpp_test_support
