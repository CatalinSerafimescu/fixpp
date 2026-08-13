// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/required_scope_oracle.hpp
//
// 079-required-presence-scope T015-T019 (fixpp#201) — the SHARED,
// non-circular independent oracle extracted out of
// `required_scope_census_test.cpp` (Contract 1/1a, T015-T017) so
// `required_scope_parity_test.cpp` (Contract 2, T018/T019) can reuse the
// SAME QuickFIX-XML walker rather than forking it
// ([[feedback_witness_asserts_named_postcondition_not_proxy]]-adjacent single-
// oracle guarantee named explicitly in the T019 task text: "do NOT
// duplicate/fork the walker logic, that would break the single-oracle
// guarantee").
//
// Anchors:
//   tasks:     specs/079-required-presence-scope/tasks.md T015-T019
//   contracts: specs/079-required-presence-scope/contracts/census-and-agreement.md
//              Contract 1 + Contract 1a + Contract 2
//
// ============================================================================
// NON-CIRCULARITY BANNER (Contract 1 / Contract 1a / Contract 2): everything
// in this header (`qfix_walk`/`orch_walk`/`build_quickfix_oracle`/
// `build_orchestra_oracle`) is a from-scratch pugixml walk of the VENDORED
// XML. It MUST NOT call `fixpp::dict::XmlLoader`, `fixpp::dict::OrchestraLoader`,
// or `fixpp::codegen::build_ir()`. This ban applies ONLY to this file (the
// walker); the "actual side" legs in the consuming test TUs legitimately
// construct a `Dictionary` via the real loaders and (for the census) call
// `fixpp::codegen::build_ir()` directly — that is the SHIPPED side under
// test, not the walker.
// ============================================================================

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <pugixml.hpp>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace fixpp_test::required_scope_oracle {

// StandardHeader/StandardTrailer fields (Contract 1): structurally-always-
// required, NEVER dropped even when a message references the header/trailer
// componentRef with default (optional) presence. See
// `required_scope_census_test.cpp`'s original banner (now here) for the
// Orchestra `PayManagementReportAck`/StandardTrailer justification.
inline constexpr std::array<std::uint16_t, 8> kHeaderTrailerTags{8, 9, 34, 35, 49, 52, 56, 10};

inline bool is_header_trailer_tag(std::uint16_t tag) {
    return std::find(kHeaderTrailerTags.begin(), kHeaderTrailerTags.end(), tag) !=
           kHeaderTrailerTags.end();
}

// One (msg_type, outer-to-inner ancestor no_tag path, this group's own
// no_tag) coordinate — mirrors table_view.hpp's group_ctx_key convention
// (path EXCLUDES no_tag itself).
struct GroupContextKey {
    std::string msg_type;
    std::vector<std::uint16_t> path;
    std::uint16_t no_tag = 0;

    friend bool operator<(GroupContextKey const& a, GroupContextKey const& b) {
        return std::tie(a.msg_type, a.path, a.no_tag) < std::tie(b.msg_type, b.path, b.no_tag);
    }
};

// The independent oracle's full census result for one dictionary.
struct DictOracle {
    // Contract 1: msg_type -> message-level required tag set.
    std::map<std::string, std::set<std::uint16_t>> message_required;
    // Contract 1a: every REAL per-context group (non-empty direct members,
    // mirroring the loader's "if (members.empty()) continue" skip) -> its
    // direct member tags (used only to enumerate which contexts exist).
    std::map<GroupContextKey, std::set<std::uint16_t>> group_members;
    // Contract 1a: the same contexts' DIRECT required='Y' member tags.
    std::map<GroupContextKey, std::set<std::uint16_t>> group_required;
    // FR-002: the declaration-order FIRST member emitted at each group's own
    // level — i.e. the group's delimiter tag — captured from the first
    // emission of the existing document-order walk below, keyed identically
    // to `group_members`.
    std::map<GroupContextKey, std::uint16_t> group_delims;
    // 082-structural-group-detection T005/T006 (contracts/group-detection.md
    // C1/C2): every no_tag for which a `<group>`/`<fixr:groupRef>` element was
    // encountered while walking a message's own field run (header/trailer
    // merged in, per-message component expansion) — i.e. C2's "registered
    // after" column (struct-set ∩ reachable-set), populated UNCONDITIONALLY
    // (independent of members/required=) directly inside qfix_walk/orch_walk's
    // existing recursion, so this reproduces the reachability restriction by
    // construction rather than by a second walker.
    std::set<std::uint16_t> group_tags;
};

// ─────────────────────────── independent oracle (QuickFIX-XML) ────────────

// 081-strict-validation-residuals D-3 (Concern B, immediate-enclosing
// group-gating — supersedes the 079 "required-once-present" rule below):
// `group_scope_and` is a SEPARATE component-AND accumulator from
// `component_and` — the latter is the message-root accumulator (never reset
// at a group boundary, feeds `msg_required`, untouched by 081). On entry to
// each `<group>`, `group_scope_and` now RESETS to that group's OWN
// `required=` (`own_req`) — NOT unconditionally `true` (079's rule) — so a
// direct `required='Y'` member is recorded in `oracle.group_required` only
// when `own_req && group_scope_and`, i.e. iff the member's own required=Y
// AND the immediate enclosing group's own required=Y (QuickFIX
// `addXMLGroup`'s `required=="Y" && groupRequired`), mirroring
// `LoaderState::expand_field_list`'s `group_scope_component_required` fix
// (xml_loader.cpp / orchestra_loader.cpp). This is NOT an AND across
// ancestor groups — each group entry independently resets from its OWN
// `required=`, discarding whatever value the enclosing accumulator carried
// (research.md D-3). The reset-at-group-boundary multiplicative AND with
// intra-group nested optional components (079 Gate B r1 fix) is preserved:
// e.g. FIX50SP2 NoMDStatistics(2474) (required='N') -> optional
// MDStatisticParameters's required='Y' members must not be group-required
// (excluded either way — the group itself is optional); FIX50SP1
// NoSides(552) (required='Y') direct member Side(54) must stay
// group-required despite NoSides sitting behind an unrelated optional
// TrdCapRptAckSideGrp component (the group's own boundary reset seeds
// `true` regardless of the outer component's optionality).
//
// NOLINTNEXTLINE(misc-no-recursion) — recursive XML walk by design, mirrors
// (but shares no code with) LoaderState::expand_field_list.
inline void qfix_walk(pugi::xml_node parent, std::unordered_map<std::string, std::uint16_t> const& tag_by_name,
                       std::unordered_map<std::string, pugi::xml_node> const& components_by_name,
                       std::string const& msg_type, std::vector<std::uint16_t>& group_path, bool component_and,
                       bool group_scope_and, std::set<std::uint16_t>& msg_required, DictOracle& oracle) {
    for (auto const& child : parent.children()) {
        std::string_view const name{child.name()};
        if (name == "field") {
            auto const fname = std::string{child.attribute("name").as_string("")};
            auto const it = tag_by_name.find(fname);
            if (it == tag_by_name.end()) {
                throw std::runtime_error("required_scope_oracle: <field name=\"" + fname +
                                         "\"> not declared in <fields> block");
            }
            auto const tag = it->second;
            bool const own_req = std::string_view{child.attribute("required").as_string("N")} == "Y";
            bool const msg_final = own_req && component_and && group_path.empty();
            if (msg_final || is_header_trailer_tag(tag)) {
                msg_required.insert(tag);
            }
            if (!group_path.empty()) {
                GroupContextKey key{msg_type, {group_path.begin(), group_path.end() - 1},
                                    group_path.back()};
                oracle.group_members[key].insert(tag);
                oracle.group_delims.try_emplace(key, tag);
                if (own_req && group_scope_and) {
                    oracle.group_required[key].insert(tag);
                }
            }
        } else if (name == "group") {
            auto const gname = std::string{child.attribute("name").as_string("")};
            auto const it = tag_by_name.find(gname);
            if (it == tag_by_name.end()) {
                throw std::runtime_error("required_scope_oracle: <group name=\"" + gname +
                                         "\"> has no matching <field> declaration");
            }
            auto const no_tag = it->second;
            // 082 T005/T006: unconditional — this `<group>` element was
            // reached from a message's own field run (this walk only visits
            // header/trailer/message subtrees, so insertion here already
            // carries the reachability restriction; independent of own_req/
            // group_path/members, matching C1's predicate exactly).
            oracle.group_tags.insert(no_tag);
            bool const own_req = std::string_view{child.attribute("required").as_string("N")} == "Y";
            bool const msg_final = own_req && component_and && group_path.empty();
            if (msg_final || is_header_trailer_tag(no_tag)) {
                msg_required.insert(no_tag);
            }
            if (!group_path.empty()) {
                GroupContextKey key{msg_type, {group_path.begin(), group_path.end() - 1},
                                    group_path.back()};
                oracle.group_members[key].insert(no_tag);
                oracle.group_delims.try_emplace(key, no_tag);
                if (own_req && group_scope_and) {
                    oracle.group_required[key].insert(no_tag);
                }
            }
            group_path.push_back(no_tag);
            // 081 D-3: reset to THIS group's own `required=` (own_req), not
            // unconditionally true — immediate-enclosing group-gating.
            qfix_walk(child, tag_by_name, components_by_name, msg_type, group_path, component_and,
                      /*group_scope_and=*/own_req, msg_required, oracle);
            group_path.pop_back();
        } else if (name == "component") {
            auto const cname = std::string{child.attribute("name").as_string("")};
            auto const cit = components_by_name.find(cname);
            if (cit == components_by_name.end()) {
                throw std::runtime_error("required_scope_oracle: <component name=\"" + cname +
                                         "\"> not defined in <components> block");
            }
            bool const comp_req = std::string_view{child.attribute("required").as_string("N")} == "Y";
            qfix_walk(cit->second, tag_by_name, components_by_name, msg_type, group_path,
                      component_and && comp_req, group_scope_and && comp_req, msg_required, oracle);
        }
        // Ignore unknown child elements (forwards-compat; mirrors the loader).
    }
}

// `include_header_trailer` (default true, T015-T017's existing behavior):
// when false, the header/trailer nodes are never walked at all, so
// `message_required` carries ONLY the message-body component-AND set (no
// StandardHeader/Trailer carve-out tags) — used by Contract 2 (T019), which
// corroborates ONLY the component-AND rule against QuickFIX's own
// `isRequiredField`. `isRequiredField` has no header/trailer-required
// surface at all (`DataDictionary.cpp` never calls
// `addRequiredField(msgType, ...)` for header/trailer fields — they populate
// only `m_headerFields`/`m_trailerFields`, keyed independently of msgType),
// so a body-only comparison is the only surface QuickFIX's API can
// corroborate; the header/trailer carve-out itself stays pinned by Contract
// 1's census only ([[feedback_parity_corpus_row_needs_a_surface_the_reference_engine_has]]).
inline DictOracle build_quickfix_oracle(std::filesystem::path const& xml_path,
                                         bool include_header_trailer = true) {
    pugi::xml_document doc;
    auto const result = doc.load_file(xml_path.c_str());
    if (!result) {
        throw std::runtime_error("required_scope_oracle: failed to load " + xml_path.string());
    }
    auto const root = doc.child("fix");

    std::unordered_map<std::string, std::uint16_t> tag_by_name;
    for (auto const& f : root.child("fields").children("field")) {
        auto const name = std::string{f.attribute("name").as_string("")};
        auto const tag = static_cast<std::uint16_t>(f.attribute("number").as_uint());
        tag_by_name.emplace(name, tag);
    }
    std::unordered_map<std::string, pugi::xml_node> components_by_name;
    for (auto const& c : root.child("components").children("component")) {
        components_by_name.emplace(std::string{c.attribute("name").as_string("")}, c);
    }
    auto const header = root.child("header");
    auto const trailer = root.child("trailer");

    DictOracle oracle;
    for (auto const& m : root.child("messages").children("message")) {
        auto const msg_type = std::string{m.attribute("msgtype").as_string("")};
        auto& msg_required = oracle.message_required[msg_type];
        std::vector<std::uint16_t> group_path;
        if (include_header_trailer && header) {
            qfix_walk(header, tag_by_name, components_by_name, msg_type, group_path, true, true,
                      msg_required, oracle);
        }
        qfix_walk(m, tag_by_name, components_by_name, msg_type, group_path, true, true,
                  msg_required, oracle);
        if (include_header_trailer && trailer) {
            qfix_walk(trailer, tag_by_name, components_by_name, msg_type, group_path, true, true,
                      msg_required, oracle);
        }
    }
    return oracle;
}

// ─────────────────────────── independent oracle (Orchestra) ───────────────

// Gate B r1 F1 (fixpp#201 escalation): `group_scope_and` mirrors qfix_walk's
// separate reset-at-group-boundary accumulator above (Orchestra twin).
//
// NOLINTNEXTLINE(misc-no-recursion)
inline void orch_walk(pugi::xml_node parent, std::unordered_map<std::uint32_t, pugi::xml_node> const& components_by_id,
                       std::unordered_map<std::uint32_t, pugi::xml_node> const& groups_by_id,
                       std::string const& msg_type, std::vector<std::uint16_t>& group_path, bool component_and,
                       bool group_scope_and, std::set<std::uint16_t>& msg_required, DictOracle& oracle) {
    for (auto const& child : parent.children()) {
        std::string_view const name{child.name()};
        if (name == "fixr:fieldRef") {
            auto const tag = static_cast<std::uint16_t>(child.attribute("id").as_uint());
            bool const own_req =
                std::string_view{child.attribute("presence").as_string("")} == "required";
            bool const msg_final = own_req && component_and && group_path.empty();
            if (msg_final || is_header_trailer_tag(tag)) {
                msg_required.insert(tag);
            }
            if (!group_path.empty()) {
                GroupContextKey key{msg_type, {group_path.begin(), group_path.end() - 1},
                                    group_path.back()};
                oracle.group_members[key].insert(tag);
                oracle.group_delims.try_emplace(key, tag);
                if (own_req && group_scope_and) {
                    oracle.group_required[key].insert(tag);
                }
            }
        } else if (name == "fixr:groupRef") {
            auto const gid = child.attribute("id").as_uint();
            auto const git = groups_by_id.find(gid);
            if (git == groups_by_id.end()) {
                throw std::runtime_error("required_scope_oracle: <fixr:groupRef id=\"" +
                                         std::to_string(gid) + "\"> not defined in <fixr:groups>");
            }
            auto const numin = git->second.child("fixr:numInGroup");
            if (!numin) {
                throw std::runtime_error("required_scope_oracle: <fixr:group id=\"" +
                                         std::to_string(gid) + "\"> missing <fixr:numInGroup>");
            }
            auto const no_tag = static_cast<std::uint16_t>(numin.attribute("id").as_uint());
            // 082 T005/T006: unconditional reachable struct-set entry —
            // symmetric with qfix_walk's group branch above.
            oracle.group_tags.insert(no_tag);
            bool const own_req =
                std::string_view{child.attribute("presence").as_string("")} == "required";
            bool const msg_final = own_req && component_and && group_path.empty();
            if (msg_final || is_header_trailer_tag(no_tag)) {
                msg_required.insert(no_tag);
            }
            if (!group_path.empty()) {
                GroupContextKey key{msg_type, {group_path.begin(), group_path.end() - 1},
                                    group_path.back()};
                oracle.group_members[key].insert(no_tag);
                oracle.group_delims.try_emplace(key, no_tag);
                if (own_req && group_scope_and) {
                    oracle.group_required[key].insert(no_tag);
                }
            }
            group_path.push_back(no_tag);
            // 081 D-3: reset to THIS group's own `presence='required'`
            // (own_req), not unconditionally true — symmetric with qfix_walk.
            orch_walk(git->second, components_by_id, groups_by_id, msg_type, group_path,
                      component_and, /*group_scope_and=*/own_req, msg_required, oracle);
            group_path.pop_back();
        } else if (name == "fixr:componentRef") {
            auto const cid = child.attribute("id").as_uint();
            auto const cit = components_by_id.find(cid);
            if (cit == components_by_id.end()) {
                throw std::runtime_error("required_scope_oracle: <fixr:componentRef id=\"" +
                                         std::to_string(cid) + "\"> not defined in <fixr:components>");
            }
            bool const comp_req =
                std::string_view{child.attribute("presence").as_string("")} == "required";
            orch_walk(cit->second, components_by_id, groups_by_id, msg_type, group_path,
                      component_and && comp_req, group_scope_and && comp_req, msg_required, oracle);
        }
        // Ignore fixr:annotation / other unknown child elements.
    }
}

inline DictOracle build_orchestra_oracle(std::filesystem::path const& xml_path) {
    pugi::xml_document doc;
    auto const result = doc.load_file(xml_path.c_str());
    if (!result) {
        throw std::runtime_error("required_scope_oracle: failed to load " + xml_path.string());
    }
    auto const root = doc.child("fixr:repository");

    std::unordered_map<std::uint32_t, pugi::xml_node> components_by_id;
    for (auto const& c : root.child("fixr:components").children("fixr:component")) {
        components_by_id.emplace(c.attribute("id").as_uint(), c);
    }
    std::unordered_map<std::uint32_t, pugi::xml_node> groups_by_id;
    for (auto const& g : root.child("fixr:groups").children("fixr:group")) {
        groups_by_id.emplace(g.attribute("id").as_uint(), g);
    }

    DictOracle oracle;
    for (auto const& m : root.child("fixr:messages").children("fixr:message")) {
        auto const msg_type = std::string{m.attribute("msgType").as_string("")};
        if (msg_type.empty()) {
            continue;
        }
        auto& msg_required = oracle.message_required[msg_type];
        std::vector<std::uint16_t> group_path;
        auto const structure = m.child("fixr:structure");
        orch_walk(structure, components_by_id, groups_by_id, msg_type, group_path, true, true,
                  msg_required, oracle);
    }
    return oracle;
}

// ─────────────────────── zero-member-<group> report (T007) ────────────────
//
// FR-023/K11 no-regression evidence: the standing measurement behind P1-NON
// ("no vendored dictionary declares a member-less <group>"). Literal
// definition, matching `contracts/predicate_census.py`'s `empty_groups`
// exactly per schema — NOT reachability-restricted (FR-023 must reject a
// member-less <group> wherever it is declared, including inside an
// unreferenced <component>, so this deliberately scans the WHOLE document
// rather than reusing qfix_walk/orch_walk's reachable-subtree recursion).
// This is a trivial element-count query with none of qfix_walk/orch_walk's
// group_scope_and/component_and semantics, so it is not "a third walker" in
// the 079 banner's sense — it shares no logic with the required-scope walk.
//
// NOLINTNEXTLINE(misc-no-recursion)
inline void count_zero_member_groups_quickfix_walk(pugi::xml_node node, std::size_t& count) {
    for (auto const& child : node.children()) {
        if (std::string_view{child.name()} == "group") {
            bool has_element_child = false;
            for (auto const& gc : child.children()) {
                if (gc.type() == pugi::node_element) {
                    has_element_child = true;
                    break;
                }
            }
            if (!has_element_child) {
                ++count;
            }
        }
        count_zero_member_groups_quickfix_walk(child, count);
    }
}

inline std::size_t count_zero_member_groups_quickfix(std::filesystem::path const& xml_path) {
    pugi::xml_document doc;
    auto const result = doc.load_file(xml_path.c_str());
    if (!result) {
        throw std::runtime_error("required_scope_oracle: failed to load " + xml_path.string());
    }
    std::size_t count = 0;
    count_zero_member_groups_quickfix_walk(doc.document_element(), count);
    return count;
}

// Orchestra: a <fixr:group> always structurally nests its own
// <fixr:numInGroup> (the count-tag declaration), so "zero members" excludes
// that mandatory child (and <fixr:annotation>) — mirrors
// `predicate_census.py::census_orchestra`'s `members` computation exactly.
//
// NOLINTNEXTLINE(misc-no-recursion)
inline void count_zero_member_groups_orchestra_walk(pugi::xml_node node, std::size_t& count) {
    for (auto const& child : node.children()) {
        if (std::string_view{child.name()} == "fixr:group") {
            bool has_member = false;
            for (auto const& gc : child.children()) {
                std::string_view const gn{gc.name()};
                if (gc.type() == pugi::node_element && gn != "fixr:numInGroup" &&
                    gn != "fixr:annotation") {
                    has_member = true;
                    break;
                }
            }
            if (!has_member) {
                ++count;
            }
        }
        count_zero_member_groups_orchestra_walk(child, count);
    }
}

inline std::size_t count_zero_member_groups_orchestra(std::filesystem::path const& xml_path) {
    pugi::xml_document doc;
    auto const result = doc.load_file(xml_path.c_str());
    if (!result) {
        throw std::runtime_error("required_scope_oracle: failed to load " + xml_path.string());
    }
    std::size_t count = 0;
    count_zero_member_groups_orchestra_walk(doc.document_element(), count);
    return count;
}

// ═══════════════════════════ end of independent oracle ════════════════════

}  // namespace fixpp_test::required_scope_oracle
