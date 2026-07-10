// SPDX-License-Identifier: AGPL-3.0-or-later
// tools/codegen/fixpp-codegen/ir.cpp — T014 (see ir.hpp banner).
#include "ir.hpp"

#include <cstdint>
#include <filesystem>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/field_ref.hpp>
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

// Populates `group_order` on every MessageIR in `ir`, rooted at each
// message's own <message> XML node (NOT header/trailer — the write emitter
// is body-only, INV-2). Throws std::runtime_error if the re-parse of
// `xml_path` fails (should be unreachable: XmlLoader already parsed the same
// file successfully to build `dict`).
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
    fixpp::dict::XmlLoader loader;
    fixpp::dict::Dictionary dict = loader.load(xml_path, mr);  // throws on bad XML

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
                      .group_order = {}};
        for (fixpp::dict::FieldRef const& fr : dict.message_fields(m.msg_type)) {
            msg.fields.push_back(FieldIR{.ref = fr, .name = std::string(dict.field_name(fr.tag))});
        }
        ir.messages.push_back(std::move(msg));
    }

    // 067 T008/R9: codegen-tool-local declaration-order group plan (delimiter
    // + member order) — NOT derivable from the tag-sorted/deduped `fields`
    // run above (xml_loader.cpp:695-702). Codegen-tool-local only: no
    // runtime Dictionary/GroupRef/C-ABI change (FR-009 intact).
    populate_group_order(xml_path, dict, ir);

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
