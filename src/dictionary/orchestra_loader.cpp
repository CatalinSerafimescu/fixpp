// SPDX-License-Identifier: AGPL-3.0-or-later
// src/dictionary/orchestra_loader.cpp
// 074-orchestra-native-reader — native FIX Orchestra (fixr:repository) reader.
//
// `fixpp::dict::OrchestraLoader::load` / `::load_from_string` impls. Mirrors
// the shape of `xml_loader.cpp` (`XmlLoader`) but walks the `fixr:repository`
// (Orchestra) grammar via pugixml instead of the QuickFIX-XML grammar. Per
// research.md D-15, pugixml is consumed ONLY in this TU; public headers in
// `include/fixpp/dict/` do not transitively expose it.
//
// Scaffold (T009/T010): root check + version resolver + the datatype-token
// table + an empty-but-valid `finalize()`. The full `fixr:repository` walk
// (fields/datatypes -> components -> groups -> messages -> header/trailer)
// lands in T013/T014/T016.
//
// Error translation (contracts/orchestra_loader.md / research.md D-2/D-3/D-4):
//   - root element != <fixr:repository>        -> dict::orchestra_parse_error
//   - <fixr:repository version="..."> not FIX Latest -> dict::unknown_version_error
//   - pugi xml_parse_result != ok / cannot open file -> dict::orchestra_parse_error
//   - unknown <fixr:datatype> token used by a field   -> dict::orchestra_parse_error
//   - PMR std::bad_alloc                        -> dict::xml_oom_error (via trap_throw_or_throw)

#include <algorithm>
#include <cassert>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fixpp/core/decimal_helpers.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/error.hpp>
#include <fixpp/dict/orchestra_loader.hpp>
#include <fstream>
#include <ios>
#include <memory>
#include <memory_resource>
#include <pugixml.hpp>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include "dictionary_internal.hpp"
#include "fixpp/dict/component_ref.hpp"
#include "fixpp/dict/field_ref.hpp"
#include "fixpp/dict/group_ref.hpp"
#include "fixpp/dict/version_profile.hpp"

namespace fixpp::dict {

namespace {

// ----------------------------------------------------------------------------
// T010 — Orchestra datatype-token table: `<fixr:datatype name=...>` tokens ->
// the existing `field_data_type` enum (research.md D-3). Mirrors the shape of
// `kFieldTypeTable` (xml_loader.cpp) but the token spellings differ (Orchestra
// uses mixed-case ISO/FIXML-derived names, not QuickFIX's uppercase tokens),
// so rows are NOT reused verbatim. Includes the spike collapse rows:
// LocalMktTime -> LocalMktDate (no dedicated enum), XID/XIDREF -> String,
// TagNum -> Int. The five Orchestra-only tokens (Pattern, Reserved100Plus,
// Reserved1000Plus, Reserved4000Plus, Tenor) are deliberately absent per
// FR-006 / research.md D-3 — a field that references one of them fails
// closed via `resolve_datatype` below.
// ----------------------------------------------------------------------------

struct OrchestraTypeEntry {
    std::string_view name;
    field_data_type value;
};

constexpr OrchestraTypeEntry kOrchestraTypeTable[] = {
    {.name = "Amt", .value = field_data_type::Amt},
    {.name = "Boolean", .value = field_data_type::Boolean},
    {.name = "Country", .value = field_data_type::Country},
    {.name = "Currency", .value = field_data_type::Currency},
    {.name = "DayOfMonth", .value = field_data_type::DayOfMonth},
    {.name = "Exchange", .value = field_data_type::Exchange},
    {.name = "Language", .value = field_data_type::Language},
    {.name = "Length", .value = field_data_type::Length},
    {.name = "LocalMktDate", .value = field_data_type::LocalMktDate},
    {.name = "LocalMktTime", .value = field_data_type::LocalMktDate},  // spike collapse
    {.name = "MonthYear", .value = field_data_type::MonthYear},
    {.name = "MultipleCharValue", .value = field_data_type::MultiCharValue},
    {.name = "MultipleStringValue", .value = field_data_type::MultiStringValue},
    {.name = "NumInGroup", .value = field_data_type::NumInGroup},
    {.name = "Percentage", .value = field_data_type::Percentage},
    {.name = "Price", .value = field_data_type::Price},
    {.name = "PriceOffset", .value = field_data_type::PriceOffset},
    {.name = "Qty", .value = field_data_type::Qty},
    {.name = "SeqNum", .value = field_data_type::SeqNum},
    {.name = "String", .value = field_data_type::String},
    {.name = "TZTimeOnly", .value = field_data_type::TzTimeOnly},
    {.name = "TZTimestamp", .value = field_data_type::TzTimestamp},
    {.name = "TagNum", .value = field_data_type::Int},  // spike collapse
    {.name = "UTCDateOnly", .value = field_data_type::UtcDateOnly},
    {.name = "UTCTimeOnly", .value = field_data_type::UtcTimeOnly},
    {.name = "UTCTimestamp", .value = field_data_type::UtcTimestamp},
    {.name = "XID", .value = field_data_type::String},     // spike collapse
    {.name = "XIDREF", .value = field_data_type::String},  // spike collapse
    {.name = "XMLData", .value = field_data_type::XmlData},
    {.name = "char", .value = field_data_type::Char},
    {.name = "data", .value = field_data_type::Data},
    {.name = "float", .value = field_data_type::Float},
    {.name = "int", .value = field_data_type::Int},
};

// Fail-closed linear scan (FR-006): an unmapped token throws
// `orchestra_parse_error`. Wired into the field-datatype resolution path in
// T013/T014.
[[nodiscard]] field_data_type resolve_datatype(std::string_view name) {
    // cppcheck-suppress useStlAlgorithm  // linear search over a small constexpr table
    for (auto const& row : kOrchestraTypeTable) {
        if (row.name == name) {
            return row.value;
        }
    }
    throw orchestra_parse_error("dict::orchestra_parse_error: <fixr:datatype name=\"" +
                                std::string{name} + "\"> outside the Orchestra EP303 vocabulary");
}

// ----------------------------------------------------------------------------
// Non-throwing uint16 id parse — used by the best-effort `first_member_tag`
// delimiter scan (mirrors xml_loader.cpp's tolerant "skip on lookup miss"
// style for that specific helper; skip-on-malformed there is not a
// correctness path, unlike fieldRef/componentRef/groupRef resolution below).
// ----------------------------------------------------------------------------

[[nodiscard]] bool try_parse_uint16(std::string_view s, std::uint16_t& out) noexcept {
    int val = 0;
    auto const r = std::from_chars(s.data(), s.data() + s.size(), val);
    if (r.ec != std::errc{} || r.ptr != s.data() + s.size() || val < 0 || val > 65535) {
        return false;
    }
    out = static_cast<std::uint16_t>(val);
    return true;
}

// Strict/throwing id parse for real reference resolution (T013/T014/T016
// fail-closed contract: a malformed/missing `id=` on a structural element is
// a malformed dictionary).
[[nodiscard]] std::uint16_t parse_orchestra_id(pugi::xml_attribute const& attr, char const* what) {
    std::uint16_t out = 0;
    if (!attr || !try_parse_uint16(std::string_view{attr.as_string("")}, out)) {
        throw orchestra_parse_error(std::string{"dict::orchestra_parse_error: missing/invalid "} +
                                    what + " id attribute");
    }
    return out;
}

// ----------------------------------------------------------------------------
// Build-time scaffolding (NOT PMR — mirrors xml_loader.cpp's rationale:
// pugixml itself goes through malloc; freed before the loader returns).
// ----------------------------------------------------------------------------

struct OrchestraCode {
    std::string value;
    std::string name;  // description
};

struct OrchestraCodeSet {
    std::string base_type;  // datatype token, resolved via kOrchestraTypeTable
    std::vector<OrchestraCode> codes;
};

struct OrchestraFieldInfo {
    std::uint16_t tag{0};
    std::string name;
    field_data_type type{};
    bool has_enum{false};
    std::string enum_codeset_name;  // valid iff has_enum
};

struct OrchestraComponentDef {
    std::string name;
    pugi::xml_node node;  // root <fixr:component> XML node, walked at expand time
};

struct OrchestraGroupDef {
    std::uint16_t no_tag{0};
    std::uint16_t first_field_tag{0};
    std::uint16_t parent_group_no_tag{0};
    pugi::xml_node node;  // the <fixr:group> XML node, stored at first-seen time
};

struct OrchestraMessageDef {
    std::string msg_type;
    std::string name;
    pugi::xml_node node;  // the <fixr:structure> XML node (may be null/empty)
};

// ----------------------------------------------------------------------------
// Loader state: holds the DOM walk state and the build vectors for the final
// metadata handle. Parallel to `LoaderState` (xml_loader.cpp) but walks the
// Orchestra `fixr:repository` grammar: fieldRef/componentRef/groupRef
// reference DEFINITIONS BY NUMERIC id (not by name, unlike QuickFIX-XML), so
// the collect-phase maps below are keyed by that id.
// ----------------------------------------------------------------------------

class OrchestraLoaderState {
public:
    explicit OrchestraLoaderState(std::pmr::memory_resource* mr) noexcept : mr_(mr) {}

    void parse_document(pugi::xml_document const& doc);

    // Returns a finished metadata handle (ownership transferred to caller).
    [[nodiscard]] detail::dict_metadata_handle_ptr finalize();

private:
    void parse_root_and_version(pugi::xml_node const& root);
    void collect_codesets(pugi::xml_node const& root);
    void collect_fields(pugi::xml_node const& root);
    void collect_components(pugi::xml_node const& root);
    void collect_groups(pugi::xml_node const& root);
    void collect_messages(pugi::xml_node const& root);

    [[nodiscard]] std::uint32_t intern_name_in_pool(detail::dict_metadata_handle& h,
                                                    std::string_view name);

    // Expand a list of <fixr:fieldRef>/<fixr:componentRef>/<fixr:groupRef>
    // child rows into a flat FieldRef vector. componentRefs are resolved
    // recursively (inline). groupRefs emit a GroupRef (first-seen wins per
    // no_tag) AND the count field into the PARENT run, then recurse into the
    // group's own members with the group-context set (T016).
    // NOLINTBEGIN(misc-no-recursion,bugprone-easily-swappable-parameters) — recursive XML walk by
    // design; the two uint16_t params name different domains and are not interchangeable.
    void expand_field_list(pugi::xml_node const& parent, std::vector<FieldRef>& out,
                           std::vector<std::uint16_t>& required_out,
                           std::uint16_t enclosing_group_no_tag,
                           std::uint16_t enclosing_component_index);
    // NOLINTEND(misc-no-recursion,bugprone-easily-swappable-parameters)

    // Best-effort one-level delimiter scan for a group/component body: mirrors
    // xml_loader.cpp's first_field_tag computation (fieldRef/groupRef ->
    // direct tag; a leading componentRef -> drill into ITS direct fieldRef
    // children only, no further recursion). Returns 0 if none found.
    [[nodiscard]] std::uint16_t first_member_tag(pugi::xml_node const& container) const;

    std::pmr::memory_resource* mr_;

    std::unordered_map<std::string, OrchestraCodeSet> codesets_by_name_;
    std::unordered_map<std::uint16_t, OrchestraFieldInfo> fields_by_tag_;
    std::vector<OrchestraComponentDef> components_;
    std::unordered_map<std::uint16_t, std::uint16_t> component_index_by_xml_id_;
    // Reverse parent map: parent_of_[child component's vector index] = enclosing
    // component's 1-based id (component_index + 1). First-seen wins (mirrors
    // xml_loader.cpp's parent_of_ / [2c §4.2]).
    std::unordered_map<std::uint16_t, std::uint16_t> parent_of_;
    // Collect-phase map: <fixr:group id=G> -> the group's own XML node.
    // groupRef references G (the group element's id), NOT the no_tag — a
    // reused no_tag (e.g. NoLegs=555) has MULTIPLE distinct `<fixr:group>`
    // definitions, each with its own G, so this map is required to resolve
    // WHICH specific group definition a given `<groupRef id=G>` points to.
    std::unordered_map<std::uint16_t, pugi::xml_node> group_by_xml_id_;
    // Output-shape state (deduped by no_tag, first-seen wins during the
    // expand walk below) — mirrors xml_loader.cpp's groups_/group_index_by_no_tag_.
    std::vector<OrchestraGroupDef> groups_;
    std::unordered_map<std::uint16_t, std::uint16_t> group_index_by_no_tag_;
    std::vector<OrchestraMessageDef> messages_;

    session_version version_{session_version::Unknown};
};

// ----------------------------------------------------------------------------

void OrchestraLoaderState::parse_root_and_version(pugi::xml_node const& root) {
    if (!root || std::string_view{root.name()} != "fixr:repository") {
        throw orchestra_parse_error(
            "dict::orchestra_parse_error: root element is not <fixr:repository>");
    }
    auto const version_attr = std::string_view{root.attribute("version").as_string("")};
    if (version_attr != "FIX.Latest_EP303") {
        throw unknown_version_error(std::string{"dict::unknown_version_error: <fixr:repository "} +
                                    "version=\"" + std::string{version_attr} +
                                    "\"> is not FIX Latest");
    }
    version_ = session_version::vlatest;
}

void OrchestraLoaderState::collect_codesets(pugi::xml_node const& root) {
    auto const codesets_node = root.child("fixr:codeSets");
    if (!codesets_node) {
        return;  // permissive: a codeset-free dialect is structurally valid (no enum fields).
    }
    for (auto const& cs : codesets_node.children("fixr:codeSet")) {
        std::string const name{cs.attribute("name").as_string("")};
        if (name.empty()) {
            throw orchestra_parse_error("dict::orchestra_parse_error: <fixr:codeSet> missing name");
        }
        OrchestraCodeSet info{};
        info.base_type = std::string{cs.attribute("type").as_string("")};
        for (auto const& code : cs.children("fixr:code")) {
            OrchestraCode c{};
            c.value = std::string{code.attribute("value").as_string("")};
            c.name = std::string{code.attribute("name").as_string("")};
            info.codes.push_back(std::move(c));
        }
        codesets_by_name_.emplace(name, std::move(info));
    }
}

void OrchestraLoaderState::collect_fields(pugi::xml_node const& root) {
    auto const fields_node = root.child("fixr:fields");
    if (!fields_node) {
        throw orchestra_parse_error("dict::orchestra_parse_error: missing <fixr:fields> block");
    }
    for (auto const& f : fields_node.children("fixr:field")) {
        auto const tag = parse_orchestra_id(f.attribute("id"), "<fixr:field>");
        if (fields_by_tag_.contains(tag)) {
            throw orchestra_parse_error("dict::orchestra_parse_error: duplicate <fixr:field id=\"" +
                                        std::to_string(tag) + "\">");
        }
        OrchestraFieldInfo info{};
        info.tag = tag;
        info.name = std::string{f.attribute("name").as_string("")};
        // `type=` is EITHER a datatype token OR a codeSet name (research.md D-15
        // / brief). `unionDataType=`, if present, is deliberately ignored — the
        // PRIMARY `type=` governs (spike collapse; the 5 Orchestra-only
        // secondary-arm tokens are never a field's primary type in EP303).
        auto const type_attr = std::string_view{f.attribute("type").as_string("")};
        if (auto const csit = codesets_by_name_.find(std::string{type_attr});
            csit != codesets_by_name_.end()) {
            info.type = resolve_datatype(csit->second.base_type);
            info.has_enum = !csit->second.codes.empty();
            info.enum_codeset_name = type_attr;
        } else {
            info.type = resolve_datatype(type_attr);  // throws orchestra_parse_error on unknown
        }
        fields_by_tag_.emplace(tag, std::move(info));
    }
}

void OrchestraLoaderState::collect_components(pugi::xml_node const& root) {
    auto const comps = root.child("fixr:components");
    if (!comps) {
        return;
    }
    for (auto const& c : comps.children("fixr:component")) {
        auto const xml_id = parse_orchestra_id(c.attribute("id"), "<fixr:component>");
        if (component_index_by_xml_id_.contains(xml_id)) {
            throw orchestra_parse_error(
                "dict::orchestra_parse_error: duplicate <fixr:component id=\"" +
                std::to_string(xml_id) + "\">");
        }
        std::string const name{c.attribute("name").as_string("")};
        auto const idx = static_cast<std::uint16_t>(components_.size());
        components_.push_back({.name = name, .node = c});
        component_index_by_xml_id_.emplace(xml_id, idx);
    }

    // Reverse parent map (first-seen wins), analogous to xml_loader.cpp's
    // collect_components(): for each component P (by vector index), walk its
    // DIRECT componentRef children and record parent_of_[index_of(C)] =
    // index_of(P) + 1.
    for (std::size_t i = 0; i < components_.size(); ++i) {
        auto const parent_1based = static_cast<std::uint16_t>(i + 1);
        for (auto const& child : components_[i].node.children("fixr:componentRef")) {
            std::uint16_t child_xml_id = 0;
            if (!try_parse_uint16(std::string_view{child.attribute("id").as_string("")},
                                  child_xml_id)) {
                continue;  // resolved (or rejected) properly at expand-time below
            }
            auto const cit = component_index_by_xml_id_.find(child_xml_id);
            if (cit == component_index_by_xml_id_.end()) {
                continue;
            }
            parent_of_.emplace(cit->second, parent_1based);  // first-seen wins
        }
    }
}

void OrchestraLoaderState::collect_groups(pugi::xml_node const& root) {
    auto const groups_node = root.child("fixr:groups");
    if (!groups_node) {
        return;
    }
    for (auto const& g : groups_node.children("fixr:group")) {
        auto const xml_id = parse_orchestra_id(g.attribute("id"), "<fixr:group>");
        if (group_by_xml_id_.contains(xml_id)) {
            throw orchestra_parse_error("dict::orchestra_parse_error: duplicate <fixr:group id=\"" +
                                        std::to_string(xml_id) + "\">");
        }
        group_by_xml_id_.emplace(xml_id, g);
    }
}

void OrchestraLoaderState::collect_messages(pugi::xml_node const& root) {
    auto const msgs = root.child("fixr:messages");
    if (!msgs) {
        throw orchestra_parse_error("dict::orchestra_parse_error: missing <fixr:messages> block");
    }
    for (auto const& m : msgs.children("fixr:message")) {
        std::string const msg_type{m.attribute("msgType").as_string("")};
        if (msg_type.empty()) {
            throw orchestra_parse_error(
                "dict::orchestra_parse_error: <fixr:message> missing msgType attribute");
        }
        std::string const name{m.attribute("name").as_string("")};
        messages_.push_back(
            {.msg_type = msg_type, .name = name, .node = m.child("fixr:structure")});
    }
}

void OrchestraLoaderState::parse_document(pugi::xml_document const& doc) {
    auto const root = doc.first_child();
    parse_root_and_version(root);
    collect_codesets(root);
    collect_fields(root);
    collect_components(root);
    collect_groups(root);
    collect_messages(root);
}

std::uint16_t OrchestraLoaderState::first_member_tag(pugi::xml_node const& container) const {
    std::uint16_t first_field_tag = 0;
    for (auto const& gc : container.children()) {
        std::string_view const tn{gc.name()};
        if (tn == "fixr:fieldRef") {
            std::uint16_t tag = 0;
            if (try_parse_uint16(std::string_view{gc.attribute("id").as_string("")}, tag) &&
                fields_by_tag_.contains(tag)) {
                first_field_tag = tag;
                break;
            }
        } else if (tn == "fixr:groupRef") {
            // A leading nested group's OWN count-field tag is the delimiter
            // surrogate (mirrors xml_loader.cpp treating a leading <group
            // name=X> exactly like a <field name=X> reference).
            std::uint16_t xml_id = 0;
            if (try_parse_uint16(std::string_view{gc.attribute("id").as_string("")}, xml_id)) {
                if (auto const git = group_by_xml_id_.find(xml_id); git != group_by_xml_id_.end()) {
                    if (auto const ning = git->second.child("fixr:numInGroup"); ning) {
                        std::uint16_t no_tag = 0;
                        if (try_parse_uint16(std::string_view{ning.attribute("id").as_string("")},
                                             no_tag)) {
                            first_field_tag = no_tag;
                            break;
                        }
                    }
                }
            }
        } else if (tn == "fixr:componentRef") {
            std::uint16_t xml_id = 0;
            if (try_parse_uint16(std::string_view{gc.attribute("id").as_string("")}, xml_id)) {
                if (auto const cit = component_index_by_xml_id_.find(xml_id);
                    cit != component_index_by_xml_id_.end()) {
                    for (auto const& cf : components_[cit->second].node.children("fixr:fieldRef")) {
                        std::uint16_t cf_tag = 0;
                        if (try_parse_uint16(std::string_view{cf.attribute("id").as_string("")},
                                             cf_tag) &&
                            fields_by_tag_.contains(cf_tag)) {
                            first_field_tag = cf_tag;
                            break;
                        }
                    }
                    if (first_field_tag != 0) {
                        break;
                    }
                }
            }
        }
        // Ignore fixr:numInGroup / fixr:annotation / other unknown children.
    }
    return first_field_tag;
}

void OrchestraLoaderState::expand_field_list(pugi::xml_node const& parent,
                                             std::vector<FieldRef>& out,
                                             std::vector<std::uint16_t>& required_out,
                                             std::uint16_t enclosing_group_no_tag,
                                             std::uint16_t enclosing_component_index) {
    for (auto const& child : parent.children()) {
        std::string_view const tag_name{child.name()};
        if (tag_name == "fixr:fieldRef") {
            auto const tag = parse_orchestra_id(child.attribute("id"), "<fixr:fieldRef>");
            auto const it = fields_by_tag_.find(tag);
            if (it == fields_by_tag_.end()) {
                throw orchestra_parse_error("dict::orchestra_parse_error: <fixr:fieldRef id=\"" +
                                            std::to_string(tag) +
                                            "\"> not declared in <fixr:fields>");
            }
            auto const& info = it->second;
            bool const req =
                std::string_view{child.attribute("presence").as_string("")} == "required";
            FieldRef fr{};
            fr.tag = tag;
            fr.type = info.type;
            fr.rule = req ? field_presence::Required : field_presence::Optional;
            fr.group_no_tag = enclosing_group_no_tag;
            fr.component_index = enclosing_component_index;
            fr.length_pair_data_tag = 0;  // out of scope for 074 (not requested by tasks.md)
            out.push_back(fr);
            if (req) {
                required_out.push_back(tag);
            }
        } else if (tag_name == "fixr:componentRef") {
            auto const xml_id = parse_orchestra_id(child.attribute("id"), "<fixr:componentRef>");
            auto const cit = component_index_by_xml_id_.find(xml_id);
            if (cit == component_index_by_xml_id_.end()) {
                throw orchestra_parse_error(
                    "dict::orchestra_parse_error: <fixr:componentRef id=\"" +
                    std::to_string(xml_id) + "\"> not defined in <fixr:components>");
            }
            auto const& def = components_[cit->second];
            expand_field_list(def.node, out, required_out, enclosing_group_no_tag,
                              static_cast<std::uint16_t>(cit->second + 1));
        } else if (tag_name == "fixr:groupRef") {
            auto const xml_id = parse_orchestra_id(child.attribute("id"), "<fixr:groupRef>");
            auto const git = group_by_xml_id_.find(xml_id);
            if (git == group_by_xml_id_.end()) {
                throw orchestra_parse_error("dict::orchestra_parse_error: <fixr:groupRef id=\"" +
                                            std::to_string(xml_id) +
                                            "\"> not defined in <fixr:groups>");
            }
            auto const group_node = git->second;
            auto const numing_node = group_node.child("fixr:numInGroup");
            if (!numing_node) {
                throw orchestra_parse_error("dict::orchestra_parse_error: <fixr:group id=\"" +
                                            std::to_string(xml_id) +
                                            "\"> missing <fixr:numInGroup>");
            }
            auto const no_tag =
                parse_orchestra_id(numing_node.attribute("id"), "<fixr:numInGroup>");
            auto const nit = fields_by_tag_.find(no_tag);
            if (nit == fields_by_tag_.end()) {
                throw orchestra_parse_error("dict::orchestra_parse_error: <fixr:numInGroup id=\"" +
                                            std::to_string(no_tag) +
                                            "\"> not declared in <fixr:fields>");
            }

            // Emit the count field into the PARENT run (T016 — type==NumInGroup).
            bool const greq =
                std::string_view{child.attribute("presence").as_string("")} == "required";
            FieldRef no_fr{};
            no_fr.tag = no_tag;
            no_fr.type = nit->second.type;
            no_fr.rule = greq ? field_presence::Required : field_presence::Optional;
            no_fr.group_no_tag = enclosing_group_no_tag;
            no_fr.component_index = enclosing_component_index;
            out.push_back(no_fr);
            if (greq) {
                required_out.push_back(no_tag);
            }

            // Record the GroupRef (deduplicated by no_tag — first-seen wins).
            if (!group_index_by_no_tag_.contains(no_tag)) {
                OrchestraGroupDef gd{};
                gd.no_tag = no_tag;
                gd.first_field_tag = first_member_tag(group_node);
                gd.parent_group_no_tag = enclosing_group_no_tag;
                gd.node = group_node;
                auto const idx = static_cast<std::uint16_t>(groups_.size());
                group_index_by_no_tag_.emplace(no_tag, idx);
                groups_.push_back(gd);
            }

            // Recurse into the group's own children (its <fixr:numInGroup> child
            // is skipped automatically — it matches none of the three branches
            // above) with the group-context set.
            expand_field_list(group_node, out, required_out, no_tag, enclosing_component_index);
        }
        // Ignore fixr:annotation / other unknown child elements (forwards-compat).
    }
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
std::uint32_t OrchestraLoaderState::intern_name_in_pool(detail::dict_metadata_handle& h,
                                                        std::string_view name) {
    auto const offset = static_cast<std::uint32_t>(h.name_pool_.size());
    h.name_pool_.insert(h.name_pool_.end(), name.begin(), name.end());
    return offset;
}

detail::dict_metadata_handle_ptr OrchestraLoaderState::finalize() {
    using pmr_alloc = std::pmr::polymorphic_allocator<detail::dict_metadata_handle>;
    auto handle = std::allocate_shared<detail::dict_metadata_handle>(pmr_alloc{mr_}, mr_);

    auto& h = *handle;
    h.version_ = version_;

    // Sort messages bytewise by msg_type — research.md D-6.
    std::ranges::sort(messages_,
                      [](OrchestraMessageDef const& a, OrchestraMessageDef const& b) noexcept {
                          return detail::bytewise_compare(a.msg_type, b.msg_type) < 0;
                      });

    // Deterministic tag-ascending iteration order — doubles as the ordering
    // that makes enum_runs_ come out sorted-by-tag "for free" (T014 store
    // contract) without a separate final sort.
    std::vector<std::uint16_t> sorted_field_tags;
    sorted_field_tags.reserve(fields_by_tag_.size());
    for (auto const& [tag, info] : fields_by_tag_) {
        sorted_field_tags.push_back(tag);
    }
    std::ranges::sort(sorted_field_tags);

    // First pass: name-pool byte budget — msgType + message name + field name
    // + component name + every codeset value + every codeset description that
    // will be interned below. MUST be computed before any string_view is
    // bound (074 brief) — EnumValueRef/MessageEntry/etc. alias name_pool_.data(),
    // which is stable only once the pool never reallocates past this point.
    std::size_t pool_estimate = 0;
    for (auto const& md : messages_) {
        pool_estimate += md.msg_type.size() + md.name.size();
    }
    for (auto const& cd : components_) {
        pool_estimate += cd.name.size();
    }
    for (auto const tag : sorted_field_tags) {
        auto const& info = fields_by_tag_.at(tag);
        pool_estimate += info.name.size();
        if (info.has_enum) {
            auto const& cs = codesets_by_name_.at(info.enum_codeset_name);
            for (auto const& code : cs.codes) {
                pool_estimate += code.value.size() + code.name.size();
            }
        }
    }
    h.name_pool_.reserve(pool_estimate + 1);

    h.messages_.reserve(messages_.size());
    h.per_msg_field_offsets_.reserve(messages_.size());
    h.per_msg_required_offsets_.reserve(messages_.size());

    struct PendingField {
        detail::NameSlice slice{};
        std::uint16_t tag{0};
    };
    std::vector<PendingField> pending_fields;
    pending_fields.reserve(sorted_field_tags.size());
    for (auto const tag : sorted_field_tags) {
        auto const& info = fields_by_tag_.at(tag);
        detail::NameSlice ns{};
        ns.offset = intern_name_in_pool(h, info.name);
        ns.length = static_cast<std::uint32_t>(info.name.size());
        pending_fields.push_back({.slice = ns, .tag = tag});
    }

    struct PendingComponent {
        detail::NameSlice slice{};
        std::uint16_t index{0};
    };
    std::vector<PendingComponent> pending_components;
    pending_components.reserve(components_.size());

    struct PendingEnumCode {
        detail::NameSlice value{};
        detail::NameSlice desc{};
    };
    struct PendingEnumRun {
        std::uint16_t tag{0};
        std::vector<PendingEnumCode> codes;
    };
    std::vector<PendingEnumRun> pending_enum_runs;
    for (auto const tag : sorted_field_tags) {
        auto const& info = fields_by_tag_.at(tag);
        if (!info.has_enum) {
            continue;
        }
        auto const& cs = codesets_by_name_.at(info.enum_codeset_name);
        PendingEnumRun per{};
        per.tag = tag;
        per.codes.reserve(cs.codes.size());
        for (auto const& code : cs.codes) {
            detail::NameSlice vs{};
            vs.offset = intern_name_in_pool(h, code.value);
            vs.length = static_cast<std::uint32_t>(code.value.size());
            detail::NameSlice ds{};
            ds.offset = intern_name_in_pool(h, code.name);
            ds.length = static_cast<std::uint32_t>(code.name.size());
            per.codes.push_back({.value = vs, .desc = ds});
        }
        pending_enum_runs.push_back(std::move(per));
    }

    // Build the flat fields_ vector and the per-message offset table
    // (identical shape to xml_loader.cpp's append_run).
    auto append_run = [&](std::vector<FieldRef>& msg_fields,
                          std::vector<std::uint16_t>& msg_required)
        -> std::pair<detail::MsgFieldsRun, detail::MsgFieldsRun> {
        std::ranges::sort(msg_fields, [](FieldRef const& a, FieldRef const& b) noexcept {
            return a.tag < b.tag;
        });
        auto const last =
            std::ranges::unique(msg_fields, [](FieldRef const& a, FieldRef const& b) noexcept {
                return a.tag == b.tag;
            }).begin();
        msg_fields.erase(last, msg_fields.end());

        std::ranges::sort(msg_required);
        auto const rlast = std::ranges::unique(msg_required).begin();
        msg_required.erase(rlast, msg_required.end());

        detail::MsgFieldsRun field_run{.start = static_cast<std::uint32_t>(h.fields_.size()),
                                       .count = static_cast<std::uint32_t>(msg_fields.size())};
        h.fields_.insert(h.fields_.end(), msg_fields.begin(), msg_fields.end());

        detail::MsgFieldsRun req_run{
            .start = static_cast<std::uint32_t>(h.required_fields_pool_.size()),
            .count = static_cast<std::uint32_t>(msg_required.size())};
        h.required_fields_pool_.insert(h.required_fields_pool_.end(), msg_required.begin(),
                                       msg_required.end());

        return {field_run, req_run};
    };

    std::vector<detail::NameSlice> msg_msg_type_slices;
    std::vector<detail::NameSlice> msg_name_slices;
    msg_msg_type_slices.reserve(messages_.size());
    msg_name_slices.reserve(messages_.size());

    for (auto const& md : messages_) {
        detail::NameSlice mts{};
        mts.offset = intern_name_in_pool(h, md.msg_type);
        mts.length = static_cast<std::uint32_t>(md.msg_type.size());
        detail::NameSlice mns{};
        mns.offset = intern_name_in_pool(h, md.name);
        mns.length = static_cast<std::uint32_t>(md.name.size());
        msg_msg_type_slices.push_back(mts);
        msg_name_slices.push_back(mns);

        std::vector<FieldRef> msg_fields;
        std::vector<std::uint16_t> msg_required;
        // No separate header/trailer nodes in Orchestra — StandardHeader
        // (component 1024) / StandardTrailer (1025) are ordinary componentRefs
        // inside each message's own <fixr:structure> (brief).
        if (md.node) {
            expand_field_list(md.node, msg_fields, msg_required, 0, 0);
        }
        auto const [field_run, req_run] = append_run(msg_fields, msg_required);
        h.per_msg_field_offsets_.push_back(field_run);
        h.per_msg_required_offsets_.push_back(req_run);
    }

    // Emit components (PMR ComponentRef array) — mirrors xml_loader.cpp:751-795.
    h.components_.reserve(components_.size());
    for (std::size_t i = 0; i < components_.size(); ++i) {
        auto const& def = components_[i];
        detail::NameSlice ns{};
        ns.offset = intern_name_in_pool(h, def.name);
        ns.length = static_cast<std::uint32_t>(def.name.size());
        pending_components.push_back({.slice = ns, .index = static_cast<std::uint16_t>(i)});

        std::vector<FieldRef> comp_fields;
        std::vector<std::uint16_t> comp_required;  // discard — component required-sets unused
        expand_field_list(def.node, comp_fields, comp_required,
                          /*enclosing_group_no_tag=*/0,
                          /*enclosing_component_index=*/static_cast<std::uint16_t>(i + 1));

        auto const first_idx = static_cast<std::uint16_t>(h.component_fields_.size());
        auto const cnt =
            static_cast<std::uint16_t>(std::min<std::size_t>(comp_fields.size(), 65535U));
        h.component_fields_.insert(h.component_fields_.end(), comp_fields.begin(),
                                   comp_fields.end());

        std::uint16_t parent_comp_id = 0;
        if (auto const pit = parent_of_.find(static_cast<std::uint16_t>(i));
            pit != parent_of_.end()) {
            parent_comp_id = pit->second;
        }

        ComponentRef cr{};
        cr.component_id = static_cast<std::uint16_t>(i);
        cr.name_offset = static_cast<std::uint16_t>(std::min<std::uint32_t>(ns.offset, 65535));
        cr.first_field_index = first_idx;
        cr.field_count = cnt;
        cr.parent_component_id = parent_comp_id;
        cr._reserved = 0;
        h.components_.push_back(cr);
    }

    // T017 — load-time delimiter guard (mirrors xml_loader.cpp:797-819 /
    // 072-nested-group-hardening FR-003): reject a dialect in which a nested
    // group's delimiter (first_field_tag) equals its immediate parent group's
    // delimiter. Runs BEFORE groups_ is sorted by no_tag (which would stale
    // group_index_by_no_tag_, whose indices are pre-sort).
    for (auto const& g : groups_) {
        if (g.parent_group_no_tag == 0) {
            continue;
        }
        auto const pit = group_index_by_no_tag_.find(g.parent_group_no_tag);
        if (pit == group_index_by_no_tag_.end()) {
            continue;
        }
        auto const& parent = groups_[pit->second];
        if (g.first_field_tag != 0 && g.first_field_tag == parent.first_field_tag) {
            throw group_delimiter_collision_error::make(g.no_tag, g.first_field_tag, parent.no_tag);
        }
    }

    // Emit groups sorted by no_tag — mirrors xml_loader.cpp:821-851.
    std::ranges::sort(groups_, [](OrchestraGroupDef const& a, OrchestraGroupDef const& b) noexcept {
        return a.no_tag < b.no_tag;
    });
    h.groups_.reserve(groups_.size());
    for (auto const& g : groups_) {
        std::uint16_t first_idx = 0;
        std::uint16_t cnt = 0;
        if (g.node) {
            std::vector<FieldRef> grp_fields;
            std::vector<std::uint16_t> grp_required;
            expand_field_list(g.node, grp_fields, grp_required,
                              /*enclosing_group_no_tag=*/g.no_tag,
                              /*enclosing_component_index=*/0);
            first_idx = static_cast<std::uint16_t>(h.group_fields_.size());
            cnt = static_cast<std::uint16_t>(std::min<std::size_t>(grp_fields.size(), 65535U));
            h.group_fields_.insert(h.group_fields_.end(), grp_fields.begin(), grp_fields.end());
        }

        GroupRef gr{};
        gr.no_tag = g.no_tag;
        gr.first_field_tag = g.first_field_tag;
        gr.first_field_index = first_idx;
        gr.field_count = cnt;
        gr.parent_group_no_tag = g.parent_group_no_tag;
        gr._reserved = 0;
        h.groups_.push_back(gr);
    }

    // Pool is now finalized — lock the data pointer.
    h.name_pool_.shrink_to_fit();
    auto* data = h.name_pool_.data();

    // Bind string_views in MessageEntry now that name_pool_.data() is stable.
    for (std::size_t i = 0; i < messages_.size(); ++i) {
        MessageEntry me{};
        me.msg_type =
            std::string_view{data + msg_msg_type_slices[i].offset, msg_msg_type_slices[i].length};
        me.name = std::string_view{data + msg_name_slices[i].offset, msg_name_slices[i].length};
        h.messages_.push_back(me);
    }

    // Build component-name index, sorted bytewise.
    h.components_by_name_.reserve(pending_components.size());
    for (auto const& pc : pending_components) {
        detail::NamedIndex ni{};
        ni.name = pc.slice;
        ni.index = pc.index;
        h.components_by_name_.push_back(ni);
    }
    std::ranges::sort(h.components_by_name_,
                      [&](detail::NamedIndex const& a, detail::NamedIndex const& b) noexcept {
                          return detail::bytewise_compare(h.name_at(a.name), h.name_at(b.name)) < 0;
                      });

    // Build field-name index, sorted bytewise.
    h.field_by_name_.reserve(pending_fields.size());
    for (auto const& pf : pending_fields) {
        detail::FieldNameEntry e{};
        e.name = pf.slice;
        e.tag = pf.tag;
        h.field_by_name_.push_back(e);
    }
    std::ranges::sort(h.field_by_name_, [&](detail::FieldNameEntry const& a,
                                            detail::FieldNameEntry const& b) noexcept {
        return detail::bytewise_compare(h.name_at(a.name), h.name_at(b.name)) < 0;
    });

    // T014 — enum store. pending_enum_runs was built while iterating
    // sorted_field_tags (ascending), so enum_runs_ already comes out
    // sorted-by-tag; the explicit sort below enforces enum_values_impl's
    // binary-search precondition LOCALLY (matching every sibling table —
    // messages_/groups_/components_by_name_/field_by_name_ — rather than
    // relying on the upstream sorted_field_tags ordering). start/count are
    // absolute indices into enum_values_, so reordering the runs is safe.
    std::size_t total_codes = 0;
    for (auto const& per : pending_enum_runs) {
        total_codes += per.codes.size();
    }
    h.enum_values_.reserve(total_codes);
    h.enum_runs_.reserve(pending_enum_runs.size());
    for (auto const& per : pending_enum_runs) {
        detail::EnumRun run{};
        run.tag = per.tag;
        run.start = static_cast<std::uint32_t>(h.enum_values_.size());
        run.count = static_cast<std::uint32_t>(per.codes.size());
        for (auto const& code : per.codes) {
            h.enum_values_.push_back(EnumValueRef{
                .value = std::string_view{data + code.value.offset, code.value.length},
                .description = std::string_view{data + code.desc.offset, code.desc.length}});
        }
        h.enum_runs_.push_back(run);
    }
    std::ranges::sort(h.enum_runs_, {}, &detail::EnumRun::tag);

    return handle;
}

// ----------------------------------------------------------------------------
// Top-level load implementation factored so both `load` and `load_from_string`
// share the post-parse path. Wraps the body in
// `core::detail::trap_throw_or_throw<xml_oom_error>` so PMR `bad_alloc`
// becomes `dict::xml_oom_error`, mirroring `xml_loader.cpp`.
// ----------------------------------------------------------------------------

[[nodiscard]] detail::dict_metadata_handle_ptr build_handle_from_doc(
    pugi::xml_document const& doc, std::pmr::memory_resource* mr) {
    OrchestraLoaderState st{mr};
    st.parse_document(doc);
    return st.finalize();
}

}  // namespace

Dictionary OrchestraLoader::load(std::filesystem::path const& path, std::pmr::memory_resource* mr) {
    assert(mr != nullptr && "OrchestraLoader::load: mr must not be null");
    return fixpp::core::detail::trap_throw_or_throw<xml_oom_error>([&] {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            throw orchestra_parse_error("dict::orchestra_parse_error: cannot open " +
                                        path.string());
        }
        pugi::xml_document doc;
        auto const result = doc.load(in);
        if (!result) {
            throw orchestra_parse_error(std::string{"dict::orchestra_parse_error: "} +
                                        result.description());
        }
        return Dictionary{build_handle_from_doc(doc, mr)};
    });
}

Dictionary OrchestraLoader::load_from_string(std::string_view xml, std::pmr::memory_resource* mr) {
    assert(mr != nullptr && "OrchestraLoader::load_from_string: mr must not be null");
    return fixpp::core::detail::trap_throw_or_throw<xml_oom_error>([&] {
        pugi::xml_document doc;
        auto const result = doc.load_buffer(xml.data(), xml.size());
        if (!result) {
            throw orchestra_parse_error(std::string{"dict::orchestra_parse_error: "} +
                                        result.description());
        }
        return Dictionary{build_handle_from_doc(doc, mr)};
    });
}

}  // namespace fixpp::dict
