// SPDX-License-Identifier: AGPL-3.0-or-later
// tools/codegen/fixpp-codegen/ir.cpp — T014 (see ir.hpp banner).
#include "ir.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/field_ref.hpp>
#include <fixpp/dict/orchestra_loader.hpp>
#include <fixpp/dict/version_profile.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fstream>
#include <ios>
#include <memory_resource>
#include <pugixml.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fixpp::codegen {

namespace {

// ── 067-codegen-writer-emitter T008 (R9/RC#7) ───────────────────────────────
// Codegen-tool-local pugixml RE-PARSE of `xml_path`, populating
// MessageIR.group_order. `build_ir` already owns `xml_path` but XmlLoader
// yields only the tag-sorted/deduped runtime Dictionary, not the parsed
// tree — pugixml is TU-local to xml_loader.cpp (D-15), so this is a SECOND,
// codegen-tool-local parse (never a runtime Dictionary/GroupRef/C-ABI
// accessor — FR-009 intact). Mirrors xml_loader.cpp's
// LoaderState::expand_field_list component/group resolution shape, but does
// NOT tag-sort/dedup (that is exactly the information this walk exists to
// preserve — research R9).

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

// Walks `node`'s children in DECLARATION order, appending each direct
// <field> reference to `out_members` (tag resolved via
// `dict.field_by_name`), transparently expanding <component> refs inline
// (their members belong to the CURRENT level), and for each <group> child:
// (1) appends a single is_group=true marker to `out_members` at this level,
// (2) recurses to build that group's own GroupOrderEntry (its members, and
// any further-nested groups), appended to `out_groups`.
// NOLINTNEXTLINE(misc-no-recursion)
void walk_level(pugi::xml_node const& node, ComponentIndex const& comps,
                fixpp::dict::Dictionary const& dict, std::vector<std::uint16_t> const& parent_path,
                std::vector<GroupOrderMember>& out_members, std::vector<GroupOrderEntry>& out_groups) {
    for (auto const& child : node.children()) {
        std::string_view const tag_name{child.name()};
        if (tag_name == "field") {
            auto const fname = std::string{child.attribute("name").as_string("")};
            auto const tag_opt = dict.field_by_name(fname);
            if (!tag_opt) {
                continue;  // XmlLoader already validated the XML; defensive only.
            }
            out_members.push_back(GroupOrderMember{.tag = *tag_opt, .is_group = false});
        } else if (tag_name == "component") {
            auto const cname = std::string{child.attribute("name").as_string("")};
            auto const it = comps.by_name.find(cname);
            if (it == comps.by_name.end()) {
                continue;
            }
            walk_level(it->second, comps, dict, parent_path, out_members, out_groups);
        } else if (tag_name == "group") {
            auto const gname = std::string{child.attribute("name").as_string("")};
            auto const no_tag_opt = dict.field_by_name(gname);
            if (!no_tag_opt) {
                continue;
            }
            std::uint16_t const no_tag = *no_tag_opt;
            out_members.push_back(GroupOrderMember{.tag = no_tag, .is_group = true});

            GroupOrderEntry entry;
            entry.parent_path = parent_path;
            entry.no_tag = no_tag;
            std::vector<std::uint16_t> child_path = parent_path;
            child_path.push_back(no_tag);
            walk_level(child, comps, dict, child_path, entry.members, out_groups);
            if (!entry.members.empty()) {
                entry.delimiter_tag = entry.members.front().tag;
            }
            out_groups.push_back(std::move(entry));
        }
        // Ignore unknown child elements (forwards-compat, mirrors the loader).
    }
}

// Recursively collects every field/group tag reachable from `node` into
// `out` (component refs resolved via `comps`; a <group> child contributes
// its own NoXXX tag AND everything nested inside it). Used to flatten the
// top-level <header>/<trailer> elements into their full tag set — order is
// irrelevant here (unlike walk_level's group_order), only membership.
// NOLINTNEXTLINE(misc-no-recursion)
void collect_tags(pugi::xml_node const& node, ComponentIndex const& comps,
                  fixpp::dict::Dictionary const& dict, std::unordered_set<std::uint16_t>& out) {
    for (auto const& child : node.children()) {
        std::string_view const tag_name{child.name()};
        if (tag_name == "field") {
            auto const fname = std::string{child.attribute("name").as_string("")};
            auto const tag_opt = dict.field_by_name(fname);
            if (tag_opt) {
                out.insert(*tag_opt);
            }
        } else if (tag_name == "component") {
            auto const cname = std::string{child.attribute("name").as_string("")};
            auto const it = comps.by_name.find(cname);
            if (it != comps.by_name.end()) {
                collect_tags(it->second, comps, dict, out);
            }
        } else if (tag_name == "group") {
            auto const gname = std::string{child.attribute("name").as_string("")};
            auto const no_tag_opt = dict.field_by_name(gname);
            if (no_tag_opt) {
                out.insert(*no_tag_opt);
            }
            collect_tags(child, comps, dict, out);
        }
        // Ignore unknown child elements (forwards-compat, mirrors the loader).
    }
}

// 082-structural-group-detection: group-only sibling of `collect_tags` —
// recursively collects ONLY <group> count tags reachable from `node` (skips
// plain <field> members) into `out`. `populate_group_order`'s
// `group_order`/`VersionIR::group_tags` walk is deliberately body-only
// (<message>, NOT <header>/<trailer> — INV-2, the write-emitter exclusion),
// but the READ-tier emitters (emit_messages.cpp/emit_reify.cpp) test
// `is_group_tag()` against `m.fields`, which IS header/trailer-inclusive
// (`dict.message_fields()`, xml_loader.cpp:926-933 header/trailer merge).
// Without this, a header/trailer-declared group (e.g. NoHops(627) in
// FIX44's/FIXT11's own <header>) is silently demoted to a scalar accessor
// on the read tier — confirmed empirically against the v44 read golden.
// NOLINTNEXTLINE(misc-no-recursion)
void collect_group_tags(pugi::xml_node const& node, ComponentIndex const& comps,
                        fixpp::dict::Dictionary const& dict, std::unordered_set<std::uint16_t>& out) {
    for (auto const& child : node.children()) {
        std::string_view const tag_name{child.name()};
        if (tag_name == "component") {
            auto const cname = std::string{child.attribute("name").as_string("")};
            auto const it = comps.by_name.find(cname);
            if (it != comps.by_name.end()) {
                collect_group_tags(it->second, comps, dict, out);
            }
        } else if (tag_name == "group") {
            auto const gname = std::string{child.attribute("name").as_string("")};
            auto const no_tag_opt = dict.field_by_name(gname);
            if (no_tag_opt) {
                out.insert(*no_tag_opt);
            }
            collect_group_tags(child, comps, dict, out);
        }
        // "field" and unknown child elements carry no group tag of their own.
    }
}

// Populates `group_order` on every MessageIR in `ir`, rooted at each
// message's own <message> XML node (NOT header/trailer — the write emitter
// is body-only, INV-2), and `ir.header_trailer_tags` from the top-level
// <header>/<trailer> elements (recursively resolved through
// <component>/<group> refs — the header/trailer-provenance exclusion
// follow-up). Both are populated from the SAME parse pass (parse-once).
// Throws std::runtime_error if the re-parse of `xml_path` fails (should be
// unreachable: XmlLoader already parsed the same file successfully to build
// `dict`).
void populate_group_order(std::filesystem::path const& xml_path, fixpp::dict::Dictionary const& dict,
                          VersionIR& ir) {
    std::ifstream in(xml_path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("fixpp-codegen: group_order re-parse cannot open " +
                                 xml_path.string());
    }
    pugi::xml_document doc;
    auto const result = doc.load(in);
    if (!result) {
        throw std::runtime_error(std::string{"fixpp-codegen: group_order re-parse: "} +
                                 result.description());
    }
    auto const root = doc.child("fix");
    auto const comps = build_component_index(root);

    std::unordered_set<std::uint16_t> header_trailer;
    collect_tags(root.child("header"), comps, dict, header_trailer);
    collect_tags(root.child("trailer"), comps, dict, header_trailer);
    ir.header_trailer_tags.assign(header_trailer.begin(), header_trailer.end());
    std::sort(ir.header_trailer_tags.begin(), ir.header_trailer_tags.end());

    // 082 D-3 fix-up: header/trailer-declared groups (e.g. NoHops(627) in
    // FIX44's/FIXT11's own <header>) are outside group_order's body-only
    // walk but ARE reachable via `m.fields` (header/trailer-merged) at the
    // read-tier emitter sites — union them into group_tags directly here
    // (build_ir() sorts+dedupes the whole set after both populate_* calls).
    std::unordered_set<std::uint16_t> header_trailer_groups;
    collect_group_tags(root.child("header"), comps, dict, header_trailer_groups);
    collect_group_tags(root.child("trailer"), comps, dict, header_trailer_groups);
    ir.group_tags.insert(ir.group_tags.end(), header_trailer_groups.begin(),
                         header_trailer_groups.end());

    std::unordered_map<std::string, pugi::xml_node> msg_node_by_type;
    for (auto const& m : root.child("messages").children("message")) {
        msg_node_by_type.emplace(std::string{m.attribute("msgtype").as_string("")}, m);
    }

    for (auto& msg : ir.messages) {
        auto const it = msg_node_by_type.find(msg.msg_type);
        if (it == msg_node_by_type.end()) {
            continue;  // defensive; every ir.messages entry came from this same XML.
        }
        std::vector<GroupOrderMember> top_members;  // discarded — top-level order is R1 tag-sorted.
        walk_level(it->second, comps, dict, {}, top_members, msg.group_order);

        // 069-v44-all-families T005 (data-model.md Entity 1): msgcat is not
        // exposed by the runtime Dictionary (FR-009 — no runtime API change),
        // so it is read from this same codegen-tool-local re-parse. Fail
        // closed on a missing/unrecognized value — never default-guess the
        // category (that would be exactly the silent-drop/silent-add surface
        // the completeness pin exists to catch).
        std::string_view const msgcat{it->second.attribute("msgcat").as_string("")};
        if (msgcat == "app") {
            msg.is_application = true;
        } else if (msgcat == "admin") {
            msg.is_application = false;
        } else {
            throw fixpp::dict::xml_parse_error(
                "dict::xml_parse_error: <message msgtype=\"" + msg.msg_type +
                "\"> missing or unrecognized msgcat attribute (expected 'app' or 'admin')");
        }
    }
}

// ── 076-fix-latest-typed-codegen T005 (research R2b, RC-A) ──────────────────
// Orchestra-schema (`fixr:repository`) SIBLING to `populate_group_order` /
// `walk_level` / `collect_tags` above. `populate_group_order` is `<fix>`-
// schema-hardcoded (`doc.child("fix")`, `msgcat`) and NO-OPS on an Orchestra
// root (research R1) — every one of its lookups returns empty against
// `<fixr:message>`/`<fixr:structure>`/`fixr:fieldRef`/`groupRef`/
// `componentRef`, leaving group_order/header_trailer_tags/is_application
// empty/false for all 181 messages. This projection is the fix: a SECOND,
// independently-shaped codegen-tool-local pugixml re-parse of the SAME file
// `build_ir`'s loader dispatch (T003) already selected, producing all FOUR
// R2b outputs from one parse-once pass. Codegen-tool-local only: no runtime
// Dictionary/GroupRef/C-ABI change (FR-009 intact) — tag->type/name
// resolution is read from the already-loaded `dict` (which already fails
// closed on an unknown datatype at OrchestraLoader::load time, FR-010; this
// projection runs only after that succeeded, so it never needs its own
// datatype gate).
//
// componentRef/groupRef in the fixr: schema reference definitions by
// NUMERIC id (unlike the <fix> schema's by-name refs) — mirrors
// OrchestraLoaderState's component_index_by_xml_id_/group_by_xml_id_
// (src/dictionary/orchestra_loader.cpp), independently re-implemented here
// (this walker shares no code with that runtime loader; it only consumes
// the public Dictionary read surface for tag->type/name).

struct OrchestraComponentIndex {
    std::unordered_map<std::uint16_t, pugi::xml_node> by_id;
    std::unordered_map<std::string, pugi::xml_node> by_name;
};

struct OrchestraGroupIndex {
    std::unordered_map<std::uint16_t, pugi::xml_node> by_id;
};

[[nodiscard]] bool try_parse_orchestra_uint16(std::string_view s, std::uint16_t& out) {
    int val = 0;
    auto const r = std::from_chars(s.data(), s.data() + s.size(), val);
    if (r.ec != std::errc{} || r.ptr != s.data() + s.size() || val < 0 || val > 65535) {
        return false;
    }
    out = static_cast<std::uint16_t>(val);
    return true;
}

OrchestraComponentIndex build_orchestra_component_index(pugi::xml_node const& root) {
    OrchestraComponentIndex idx;
    for (auto const& c : root.child("fixr:components").children("fixr:component")) {
        std::uint16_t id = 0;
        if (!try_parse_orchestra_uint16(std::string_view{c.attribute("id").as_string("")}, id)) {
            continue;  // OrchestraLoader already validated the XML; defensive only.
        }
        idx.by_id.emplace(id, c);
        idx.by_name.emplace(std::string{c.attribute("name").as_string("")}, c);
    }
    return idx;
}

OrchestraGroupIndex build_orchestra_group_index(pugi::xml_node const& root) {
    OrchestraGroupIndex idx;
    for (auto const& g : root.child("fixr:groups").children("fixr:group")) {
        std::uint16_t id = 0;
        if (!try_parse_orchestra_uint16(std::string_view{g.attribute("id").as_string("")}, id)) {
            continue;
        }
        idx.by_id.emplace(id, g);
    }
    return idx;
}

// Recursively collects every field/count tag reachable from `node` into
// `out` (fixr:componentRef/fixr:groupRef resolved transitively) — mirrors
// `collect_tags` above but over the fixr: schema's numeric-id refs. Used to
// flatten the StandardHeader/StandardTrailer COMPONENTS (Orchestra has no
// separate <header>/<trailer> elements — those are ordinary componentRefs
// inside each message's own <fixr:structure>, orchestra_loader.cpp:730-732)
// into their full tag set. Order is irrelevant here (membership only).
// NOLINTNEXTLINE(misc-no-recursion)
void collect_orchestra_tags(pugi::xml_node const& node, OrchestraComponentIndex const& comps,
                            OrchestraGroupIndex const& groups,
                            std::unordered_set<std::uint16_t>& out) {
    for (auto const& child : node.children()) {
        std::string_view const tag_name{child.name()};
        if (tag_name == "fixr:fieldRef") {
            std::uint16_t tag = 0;
            if (try_parse_orchestra_uint16(std::string_view{child.attribute("id").as_string("")},
                                           tag)) {
                out.insert(tag);
            }
        } else if (tag_name == "fixr:componentRef") {
            std::uint16_t id = 0;
            if (try_parse_orchestra_uint16(std::string_view{child.attribute("id").as_string("")},
                                           id)) {
                if (auto const it = comps.by_id.find(id); it != comps.by_id.end()) {
                    collect_orchestra_tags(it->second, comps, groups, out);
                }
            }
        } else if (tag_name == "fixr:groupRef") {
            std::uint16_t id = 0;
            if (try_parse_orchestra_uint16(std::string_view{child.attribute("id").as_string("")},
                                           id)) {
                if (auto const git = groups.by_id.find(id); git != groups.by_id.end()) {
                    if (auto const ning = git->second.child("fixr:numInGroup"); ning) {
                        std::uint16_t no_tag = 0;
                        if (try_parse_orchestra_uint16(
                                std::string_view{ning.attribute("id").as_string("")}, no_tag)) {
                            out.insert(no_tag);
                        }
                    }
                    collect_orchestra_tags(git->second, comps, groups, out);
                }
            }
        }
        // Ignore fixr:numInGroup / fixr:annotation / other unknown children.
    }
}

// 082-structural-group-detection: group-only sibling of `collect_orchestra_tags`
// — see `collect_group_tags`'s banner (the <fix>-schema twin) for why this
// is needed: StandardHeader/StandardTrailer-declared groups are outside
// `walk_orchestra_level`'s per-message group_order but reachable via
// `m.fields` at the read-tier emitter sites.
// NOLINTNEXTLINE(misc-no-recursion)
void collect_orchestra_group_tags(pugi::xml_node const& node, OrchestraComponentIndex const& comps,
                                  OrchestraGroupIndex const& groups,
                                  std::unordered_set<std::uint16_t>& out) {
    for (auto const& child : node.children()) {
        std::string_view const tag_name{child.name()};
        if (tag_name == "fixr:componentRef") {
            std::uint16_t id = 0;
            if (try_parse_orchestra_uint16(std::string_view{child.attribute("id").as_string("")},
                                           id)) {
                if (auto const it = comps.by_id.find(id); it != comps.by_id.end()) {
                    collect_orchestra_group_tags(it->second, comps, groups, out);
                }
            }
        } else if (tag_name == "fixr:groupRef") {
            std::uint16_t id = 0;
            if (try_parse_orchestra_uint16(std::string_view{child.attribute("id").as_string("")},
                                           id)) {
                if (auto const git = groups.by_id.find(id); git != groups.by_id.end()) {
                    if (auto const ning = git->second.child("fixr:numInGroup"); ning) {
                        std::uint16_t no_tag = 0;
                        if (try_parse_orchestra_uint16(
                                std::string_view{ning.attribute("id").as_string("")}, no_tag)) {
                            out.insert(no_tag);
                        }
                    }
                    collect_orchestra_group_tags(git->second, comps, groups, out);
                }
            }
        }
        // "fixr:fieldRef" / "fixr:numInGroup" / "fixr:annotation" carry no
        // group tag of their own.
    }
}

// Walks `node`'s children in DECLARATION order (mirrors `walk_level` above,
// over the fixr: schema): appends each direct fieldRef to `out_members`
// (research R2b output 1 — group_order), transparently expands componentRef
// inline (its members belong to the CURRENT level), and for each groupRef:
// (1) appends the group's NumInGroup count tag as an is_group=true marker,
// (2) recurses into the group's own definition to build its GroupOrderEntry
// (appended to `out_groups`). In lock-step, appends ONE OccurrenceIR per
// fieldRef/groupRef-count-field visited (research R2b output 4 — the
// lossless occurrence list; NOT tag-deduped, unlike `dict.message_fields()`
// — a tag reused under two different group parents produces two entries
// here, each with its own `group_path`). Walks the message's FULL
// <fixr:structure> (header + body + trailer inline — Orchestra has no
// separate header/trailer elements, orchestra_loader.cpp:730-732), matching
// what `dict.message_fields()`/the shipped read classes actually carry
// (data-model.md Entity 2: "header + body + repeating-group members at all
// depths"); header/trailer EXCLUSION for the write-only builder surface is
// a separate, independent output (`header_trailer_tags`, computed below by
// `populate_orchestra_projection`), not applied here.
// NOLINTNEXTLINE(misc-no-recursion)
void walk_orchestra_level(pugi::xml_node const& node, OrchestraComponentIndex const& comps,
                          OrchestraGroupIndex const& groups, fixpp::dict::Dictionary const& dict,
                          std::string const& msg_type, std::vector<std::uint16_t> const& parent_path,
                          std::vector<GroupOrderMember>& out_members,
                          std::vector<GroupOrderEntry>& out_groups,
                          std::vector<OccurrenceIR>& out_occurrences) {
    for (auto const& child : node.children()) {
        std::string_view const tag_name{child.name()};
        if (tag_name == "fixr:fieldRef") {
            std::uint16_t tag = 0;
            if (!try_parse_orchestra_uint16(std::string_view{child.attribute("id").as_string("")},
                                            tag)) {
                continue;  // OrchestraLoader already validated the XML; defensive only.
            }
            out_members.push_back(GroupOrderMember{.tag = tag, .is_group = false});
            bool const required =
                std::string_view{child.attribute("presence").as_string("")} == "required";
            auto const fr = dict.field_ref(msg_type, tag);
            out_occurrences.push_back(OccurrenceIR{
                .group_path = parent_path,
                .tag = tag,
                .rule = required ? fixpp::dict::field_presence::Required
                                 : fixpp::dict::field_presence::Optional,
                .datatype = fr.type});
        } else if (tag_name == "fixr:componentRef") {
            std::uint16_t id = 0;
            if (!try_parse_orchestra_uint16(std::string_view{child.attribute("id").as_string("")},
                                            id)) {
                continue;
            }
            auto const it = comps.by_id.find(id);
            if (it == comps.by_id.end()) {
                continue;
            }
            walk_orchestra_level(it->second, comps, groups, dict, msg_type, parent_path, out_members,
                                 out_groups, out_occurrences);
        } else if (tag_name == "fixr:groupRef") {
            std::uint16_t id = 0;
            if (!try_parse_orchestra_uint16(std::string_view{child.attribute("id").as_string("")},
                                            id)) {
                continue;
            }
            auto const git = groups.by_id.find(id);
            if (git == groups.by_id.end()) {
                continue;
            }
            auto const group_node = git->second;
            auto const numing_node = group_node.child("fixr:numInGroup");
            if (!numing_node) {
                continue;
            }
            std::uint16_t no_tag = 0;
            if (!try_parse_orchestra_uint16(
                    std::string_view{numing_node.attribute("id").as_string("")}, no_tag)) {
                continue;
            }
            out_members.push_back(GroupOrderMember{.tag = no_tag, .is_group = true});
            bool const grequired =
                std::string_view{child.attribute("presence").as_string("")} == "required";
            auto const nfr = dict.field_ref(msg_type, no_tag);
            out_occurrences.push_back(OccurrenceIR{
                .group_path = parent_path,
                .tag = no_tag,
                .rule = grequired ? fixpp::dict::field_presence::Required
                                  : fixpp::dict::field_presence::Optional,
                .datatype = nfr.type});

            GroupOrderEntry entry;
            entry.parent_path = parent_path;
            entry.no_tag = no_tag;
            std::vector<std::uint16_t> child_path = parent_path;
            child_path.push_back(no_tag);
            walk_orchestra_level(group_node, comps, groups, dict, msg_type, child_path,
                                 entry.members, out_groups, out_occurrences);
            if (!entry.members.empty()) {
                entry.delimiter_tag = entry.members.front().tag;
            }
            out_groups.push_back(std::move(entry));
        }
        // Ignore fixr:numInGroup / fixr:annotation / other unknown children.
    }
}

// Populates group_order/header_trailer_tags/is_application/occurrences on
// every MessageIR in `ir` from a fresh codegen-tool-local re-parse of the
// SAME `fixr:repository` XML `build_ir`'s loader dispatch (T003) already
// selected for this file — the Orchestra-schema sibling to
// `populate_group_order`. Orchestra carries NO `msgcat`; it carries
// `category=` (data-model.md Entity 5), mapped via the verified
// single-category rule: `category=="Session"` -> admin (the exact 8-message
// set {0,1,2,3,4,5,A,n}, incl. XMLnonFIX(n)); everything else -> app
// (including `category=="Testing"`, which is an application category, NOT
// session admin). Fails closed (`dict::orchestra_parse_error` — Orchestra's
// own error family, provenance-classified rather than the generic
// `xml_parse_error` the <fix> path throws) on a missing/empty category,
// mirroring `populate_group_order`'s msgcat fail-closed disposition.
void populate_orchestra_projection(std::filesystem::path const& xml_path,
                                   fixpp::dict::Dictionary const& dict, VersionIR& ir) {
    std::ifstream in(xml_path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("fixpp-codegen: Orchestra projection re-parse cannot open " +
                                 xml_path.string());
    }
    pugi::xml_document doc;
    auto const result = doc.load(in);
    if (!result) {
        throw std::runtime_error(std::string{"fixpp-codegen: Orchestra projection re-parse: "} +
                                 result.description());
    }
    auto const root = doc.first_child();
    auto const comps = build_orchestra_component_index(root);
    auto const groups = build_orchestra_group_index(root);

    // header_trailer_tags (R2b output 2): resolve the named StandardHeader /
    // StandardTrailer components transitively — Orchestra has no separate
    // <header>/<trailer> elements (orchestra_loader.cpp:730-732).
    std::unordered_set<std::uint16_t> header_trailer;
    if (auto const it = comps.by_name.find("StandardHeader"); it != comps.by_name.end()) {
        collect_orchestra_tags(it->second, comps, groups, header_trailer);
    }
    if (auto const it = comps.by_name.find("StandardTrailer"); it != comps.by_name.end()) {
        collect_orchestra_tags(it->second, comps, groups, header_trailer);
    }
    ir.header_trailer_tags.assign(header_trailer.begin(), header_trailer.end());
    std::sort(ir.header_trailer_tags.begin(), ir.header_trailer_tags.end());

    // 082 D-3 fix-up (Orchestra sibling of populate_group_order's fix-up
    // above): union StandardHeader/StandardTrailer-declared group no_tags
    // into group_tags directly.
    std::unordered_set<std::uint16_t> header_trailer_groups;
    if (auto const it = comps.by_name.find("StandardHeader"); it != comps.by_name.end()) {
        collect_orchestra_group_tags(it->second, comps, groups, header_trailer_groups);
    }
    if (auto const it = comps.by_name.find("StandardTrailer"); it != comps.by_name.end()) {
        collect_orchestra_group_tags(it->second, comps, groups, header_trailer_groups);
    }
    ir.group_tags.insert(ir.group_tags.end(), header_trailer_groups.begin(),
                         header_trailer_groups.end());

    std::unordered_map<std::string, pugi::xml_node> msg_node_by_type;
    for (auto const& m : root.child("fixr:messages").children("fixr:message")) {
        msg_node_by_type.emplace(std::string{m.attribute("msgType").as_string("")}, m);
    }

    for (auto& msg : ir.messages) {
        auto const it = msg_node_by_type.find(msg.msg_type);
        if (it == msg_node_by_type.end()) {
            continue;  // defensive; every ir.messages entry came from this same XML.
        }

        auto const structure = it->second.child("fixr:structure");
        std::vector<GroupOrderMember> top_members;  // discarded — top-level order is R1 tag-sorted.
        if (structure) {
            walk_orchestra_level(structure, comps, groups, dict, msg.msg_type, {}, top_members,
                                 msg.group_order, msg.occurrences);
        }

        std::string_view const category{it->second.attribute("category").as_string("")};
        if (category.empty()) {
            throw fixpp::dict::orchestra_parse_error(
                "dict::orchestra_parse_error: <fixr:message msgType=\"" + msg.msg_type +
                "\"> missing or empty category attribute");
        }
        msg.is_application = (category != "Session");
    }
}

}  // namespace

namespace {

// session_version -> (codegen application_version, namespace tag). Only the
// four codegen-target versions ([2c §1.3]); anything else is rejected here
// (runtime-XML-only versions get NO typed namespace — AC-D5 boundary).
struct VersionMap {
    fixpp::dict::session_version s;
    fixpp::dict::application_version a;
    char const* ns;
};

constexpr VersionMap kCodegenVersions[] = {
    {.s = fixpp::dict::session_version::v42,
     .a = fixpp::dict::application_version::v42,
     .ns = "v42"},
    {.s = fixpp::dict::session_version::v44,
     .a = fixpp::dict::application_version::v44,
     .ns = "v44"},
    {.s = fixpp::dict::session_version::v50sp2,
     .a = fixpp::dict::application_version::v50sp2,
     .ns = "v50sp2"},
    // FIXT.1.1 session layer: vt11 namespace, application axis Unknown
    // (admin frames resolve {session_admin, vt11, Unknown} — [2c §4.3]).
    {.s = fixpp::dict::session_version::vt11,
     .a = fixpp::dict::application_version::Unknown,
     .ns = "vt11"},
    // 076-fix-latest-typed-codegen T002 (data-model.md Entity 1 / research
    // R2): FIX Latest (Orchestra EP303). `.a = v50sp2` mirrors 074's
    // session_to_application(vlatest) -> v50sp2 (truthful; inert for
    // namespace selection, which keys on `.ns`). `ns = "vlatest"` is
    // distinct from "v50sp2" — namespace disjointness (uniqueness rule).
    {.s = fixpp::dict::session_version::vlatest,
     .a = fixpp::dict::application_version::v50sp2,
     .ns = "vlatest"},
};

// Highest tag the v1.0 locked set uses; the Length+Data scan walks [1, kMaxTag]
// in ascending order so the emitted pair table is bytewise-stable (A4).
constexpr std::uint16_t kMaxTag = 2500;

}  // namespace

std::vector<FieldIR const*> collect_top_fields(MessageIR const& m) {
    std::vector<FieldIR const*> top;
    std::unordered_set<std::uint16_t> seen;
    for (auto const& f : m.fields) {
        if (f.ref.group_no_tag != 0) {
            continue;
        }
        if (seen.insert(f.ref.tag).second) {
            top.push_back(&f);
        }
    }
    return top;
}

VersionIR build_ir(std::filesystem::path const& xml_path, std::pmr::memory_resource* mr) {
    // 076-fix-latest-typed-codegen T003 (research R1): route by root-element
    // sniff, not a CMake-supplied flag or filename heuristic — the loader
    // choice is intrinsic to the file's schema. <fixr:repository> ->
    // 074's dict::OrchestraLoader (pugixml, fixr: schema); <fix> -> the
    // existing dict::XmlLoader. Fail closed (never silently pick a default)
    // on an unrecognized root, mirroring the existing xml_parse_error family.
    std::ifstream sniff_in(xml_path, std::ios::binary);
    if (!sniff_in) {
        throw std::runtime_error("fixpp-codegen: cannot open " + xml_path.string());
    }
    pugi::xml_document sniff_doc;
    auto const sniff_result = sniff_doc.load(sniff_in);
    if (!sniff_result) {
        throw std::runtime_error(std::string{"fixpp-codegen: root-element sniff parse: "} +
                                 sniff_result.description());
    }
    std::string_view const root_name{sniff_doc.first_child().name()};

    fixpp::dict::Dictionary dict = [&]() -> fixpp::dict::Dictionary {
        if (root_name == "fixr:repository") {
            fixpp::dict::OrchestraLoader orchestra_loader;
            return orchestra_loader.load(xml_path, mr);  // throws on bad XML
        }
        if (root_name == "fix") {
            fixpp::dict::XmlLoader loader;
            return loader.load(xml_path, mr);  // throws on bad XML
        }
        throw std::runtime_error("fixpp-codegen: unrecognized XML root element \"" +
                                 std::string(root_name) + "\" in " + xml_path.string());
    }();

    VersionIR ir;
    ir.session = dict.which_session_version();

    bool mapped = false;
    for (auto const& vm : kCodegenVersions) {
        if (vm.s == ir.session) {
            ir.application = vm.a;
            ir.ns = vm.ns;
            mapped = true;
            break;
        }
    }
    if (!mapped) {
        throw std::runtime_error(
            "fixpp-codegen: XML version is not a codegen-target version "
            "(v42/v44/v50sp2/vt11); runtime-XML-only versions get no typed "
            "namespace per [2c §1.3]");
    }

    // Messages — Dictionary::messages() is bytewise-sorted by MsgType
    // (002 research D-6), so iteration preserves determinism (A4 / NFR-003-7).
    // Per message, the RC#5 additive accessors give the full field run +
    // tag→name (D-24); message_fields() preserves the deterministic
    // per-MsgType concatenated order.
    for (auto const& m : dict.messages()) {
        MessageIR msg{.msg_type = std::string(m.msg_type),
                      .name = std::string(m.name),
                      .fields = {},
                      .group_order = {},
                      .occurrences = {}};
        for (fixpp::dict::FieldRef const& fr : dict.message_fields(m.msg_type)) {
            msg.fields.push_back(FieldIR{.ref = fr, .name = std::string(dict.field_name(fr.tag))});
        }
        ir.messages.push_back(std::move(msg));
    }

    // 067 T008/R9: codegen-tool-local declaration-order group plan (delimiter
    // + member order) — NOT derivable from the tag-sorted/deduped `fields`
    // run above (xml_loader.cpp:695-702). Codegen-tool-local only: no
    // runtime Dictionary/GroupRef/C-ABI change (FR-009 intact).
    //
    // 076-fix-latest-typed-codegen T005 (research R1/R2b): dispatch on the
    // SAME root-element sniff T003 used to pick the loader above —
    // `populate_group_order` is `<fix>`-schema-hardcoded and NO-OPS on an
    // Orchestra root (group_order/header_trailer_tags/is_application would
    // stay empty/false for all 181 messages); `populate_orchestra_projection`
    // is its Orchestra-schema sibling, producing the same four outputs (plus
    // the lossless occurrence list, R2b output 4) from the fixr: grammar.
    if (root_name == "fixr:repository") {
        populate_orchestra_projection(xml_path, dict, ir);
    } else {
        populate_group_order(xml_path, dict, ir);
    }

    // 082-structural-group-detection D-3/C1.2: group_tags = sorted, unique
    // union of {e.no_tag : e ∈ m.group_order} over every message — the
    // structural set every emitter discovery/branch site consults instead
    // of `FieldRef::type == NumInGroup` (D-7). Populated on BOTH schema
    // paths, since group_order itself is (populate_group_order for <fix>,
    // populate_orchestra_projection for Orchestra, above).
    for (auto const& m : ir.messages) {
        for (auto const& entry : m.group_order) {
            ir.group_tags.push_back(entry.no_tag);
        }
    }
    std::sort(ir.group_tags.begin(), ir.group_tags.end());
    ir.group_tags.erase(std::unique(ir.group_tags.begin(), ir.group_tags.end()),
                        ir.group_tags.end());

    // Length+Data pairs — ascending tag scan (deterministic order). AC-V4 is
    // verified exhaustively against source XML in seam #19; here we project
    // every paired LENGTH tag the loaded Dictionary knows.
    for (std::uint16_t t = 1; t <= kMaxTag; ++t) {
        std::uint16_t data_tag = dict.length_pair_data_tag(t);
        if (data_tag != 0) {
            ir.length_pairs.push_back(LengthPairIR{.length_tag = t, .data_tag = data_tag});
        }
    }

    return ir;
}

}  // namespace fixpp::codegen
