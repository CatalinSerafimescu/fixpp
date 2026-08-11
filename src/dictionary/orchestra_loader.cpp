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

// 079-required-presence-scope T020 (component-AND symmetry with xml_loader):
// StandardHeader/StandardTrailer framing tags are structurally always-required
// and MUST stay in the message-level required set even when a message
// references the header/trailer componentRef with default (optional) presence
// (Orchestra's default componentRef presence is optional — e.g.
// PayManagementReportAck references StandardHeader with no presence attr). This
// mirrors the census oracle's `is_header_trailer_tag` carve-out
// (tests/dictionary/required_scope_oracle.hpp) so the component-AND gate below
// never drops framing tags.
[[nodiscard]] constexpr bool is_header_trailer_tag(std::uint16_t tag) noexcept {
    switch (tag) {
        case 8:
        case 9:
        case 34:
        case 35:
        case 49:
        case 52:
        case 56:
        case 10:
            return true;
        default:
            return false;
    }
}

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
// Non-throwing uint16 id parse — tolerant "skip on lookup miss" style, used
// by the id-attribute reads below. (It previously also served the
// `first_member_tag` delimiter scan, which 083 T030 removed.)
// Skip-on-malformed here is not a correctness path, unlike
// fieldRef/componentRef/groupRef resolution below.
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
    pugi::xml_node node;  // the <fixr:group> DEFINITION XML node, stored at first-seen time
    // 081-strict-validation-residuals D-3: unlike xml_loader.cpp's inline
    // <group required=.../> (definition == usage, so the required attribute
    // lives on `node` itself), Orchestra's group DEFINITION (<fixr:group>,
    // referenced by id) carries no presence — presence lives on the
    // first-seen <fixr:groupRef presence=.../> USAGE. Recorded separately so
    // the bare-store fallback (finalize()) can seed `component_required`
    // from the first-seen usage's own required-ness, symmetric with
    // xml_loader.cpp's `g.node.attribute("required")`.
    bool first_seen_required{false};
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
    explicit OrchestraLoaderState(
        std::pmr::memory_resource* mr,
        unresolved_group_policy policy = unresolved_group_policy::fail_closed) noexcept
        : mr_(mr), unresolved_policy_(policy) {}

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
    // `in_group` (fixpp#201): once inside any repeating group, member
    // `presence='required'` fields stay per-group (queryable via each
    // FieldRef's `rule`) and do NOT leak into the MESSAGE-level `required_out`.
    // `group_scope_component_required` (Gate B r1 F1 — fixpp#201 escalation,
    // symmetric with xml_loader.cpp; 081-strict-validation-residuals D-3
    // amends the reset value): a SEPARATE component-AND accumulator, RESET on
    // entry to each `<fixr:groupRef>` to THAT groupRef's own
    // `presence='required'` (081 D-3; was unconditionally `true` under 079),
    // gating `group_required_pairs_out` — see xml_loader.cpp's
    // expand_field_list doc-comment for the full mechanism note.
    void expand_field_list(
        pugi::xml_node const& parent, std::vector<FieldRef>& out,
        std::vector<std::uint16_t>& required_out,
        std::vector<std::pair<std::uint16_t, std::uint16_t>>& group_required_pairs_out,
        std::uint16_t enclosing_group_no_tag, std::uint16_t enclosing_component_index,
        // 083 T030: Entity-2 capture, symmetric with xml_loader.cpp. NULL at
        // the component-cache and group-cache call sites (C-1.1).
        detail::DelimCapture* delim_cap = nullptr, bool in_group = false,
        bool component_required = true, bool group_scope_component_required = true);
    // NOLINTEND(misc-no-recursion,bugprone-easily-swappable-parameters)

    // 083 T030: `first_member_tag()` — the best-effort one-level delimiter scan
    // — was REMOVED here, not merely bypassed. Its only caller set
    // `gd.first_field_tag`, and the delimiter is now captured from declaration
    // order during `expand_field_list` (Entity 2), with the global repopulated
    // afterwards as a first-seen projection. Leaving the function defined but
    // uncalled is the orphan a `/simplify` pass is supposed to clean up, and
    // cppcheck flagged it as `unusedPrivateFunction`. `try_parse_uint16` is
    // NOT removed — it has other callers.

    std::pmr::memory_resource* mr_;

    // 083 T036/T037 (FR-006 / FR-006a): load-time only (C-6.5). SAME option
    // and SAME semantics as XmlLoader's (C-6.4) -- shared enum, not a twin.
    unresolved_group_policy unresolved_policy_ = unresolved_group_policy::fail_closed;

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
        if (codesets_by_name_.contains(name)) {
            throw orchestra_parse_error(
                "dict::orchestra_parse_error: duplicate <fixr:codeSet name=\"" + name + "\">");
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

void OrchestraLoaderState::expand_field_list(
    pugi::xml_node const& parent, std::vector<FieldRef>& out,
    std::vector<std::uint16_t>& required_out,
    std::vector<std::pair<std::uint16_t, std::uint16_t>>& group_required_pairs_out,
    std::uint16_t enclosing_group_no_tag, std::uint16_t enclosing_component_index,
    detail::DelimCapture* delim_cap, bool in_group, bool component_required,
    bool group_scope_component_required) {
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
            // 083 D-1: first emission at the open group's level = its delimiter.
            detail::capture_first_emission(delim_cap, tag);
            // 079 T020 (component-AND, symmetric with xml_loader): a field
            // `presence='required'` only inside an OPTIONAL componentRef must
            // not enter the message-level required set — EXCEPT the structural
            // header/trailer framing tags, which are never dropped.
            if (req && !in_group && (component_required || is_header_trailer_tag(tag))) {
                required_out.push_back(tag);
            }
            // Gate B r1 F1 (fixpp#201, symmetric with xml_loader.cpp): a DIRECT
            // group-required member of the CURRENT immediately-enclosing group.
            if (req && group_scope_component_required && enclosing_group_no_tag != 0) {
                group_required_pairs_out.emplace_back(enclosing_group_no_tag, tag);
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
            // 079 T020: Orchestra's default componentRef presence is OPTIONAL
            // (absent attr != "required"), so AND the ref's own presence into
            // the running component_required (mirrors xml_loader + the oracle).
            bool const comp_req =
                std::string_view{child.attribute("presence").as_string("")} == "required";
            // 083 C-1.2: a componentRef expands INLINE at the enclosing level,
            // so the same capture frame carries through (FR-004).
            expand_field_list(def.node, out, required_out, group_required_pairs_out,
                              enclosing_group_no_tag, static_cast<std::uint16_t>(cit->second + 1),
                              delim_cap, in_group, component_required && comp_req,
                              group_scope_component_required && comp_req);
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
            // 083 D-1 / FR-003: the nested group's own count tag is pushed at
            // the OUTER level, BEFORE the descent below — so it is eligible to
            // be the outer group's delimiter exactly as a scalar field is.
            detail::capture_first_emission(delim_cap, no_tag);
            // fixpp#201: nested-group count field is per-instance, not
            // message-level required; 079 T020: nor is a group inside an
            // optional component (a NumInGroup count is never a header/trailer
            // framing tag, so no carve-out needed here).
            if (greq && !in_group && component_required) {
                required_out.push_back(no_tag);
            }
            // Gate B r1 F1: the nested group's own count-tag is a DIRECT member
            // of the OUTER group, gated by the OUTER group's own group-relative
            // AND (symmetric with xml_loader.cpp).
            if (greq && group_scope_component_required && enclosing_group_no_tag != 0) {
                group_required_pairs_out.emplace_back(enclosing_group_no_tag, no_tag);
            }

            // FR-023 (082) is NOT implemented here — the Orchestra sibling of
            // the same removal in xml_loader.cpp. It is satisfied by 083's
            // T036 `captured == 0` disposition below (:659-680), which rejects
            // the same input class with the policy layering FR-023 owes
            // (fail-closed default / tolerant skip / zero-context exempt).
            // See implementation-notes.md § RESUMED 2026-08-11 and spec.md FR-023.
            // Record the GroupRef (deduplicated by no_tag — first-seen wins).
            if (!group_index_by_no_tag_.contains(no_tag)) {
                OrchestraGroupDef gd{};
                gd.no_tag = no_tag;
                // 083 T030 (research D-10 / C-1.4b, symmetric with
                // xml_loader.cpp): the best-effort one-level `first_member_tag`
                // scan no longer decides the delimiter. Left 0 here and
                // REPOPULATED after the message loop as a first-seen projection
                // of Entity 2 — never left 0, since the global doubles as an
                // is-this-tag-a-group predicate behind the GA-frozen C ABI.
                gd.parent_group_no_tag = enclosing_group_no_tag;
                gd.node = group_node;
                gd.first_seen_required = greq;  // 081 D-3: first-seen usage's own presence
                auto const idx = static_cast<std::uint16_t>(groups_.size());
                group_index_by_no_tag_.emplace(no_tag, idx);
                groups_.push_back(gd);
            }

            // Recurse into the group's own children (its <fixr:numInGroup> child
            // is skipped automatically — it matches none of the three branches
            // above) with the group-context set. 081 D-3 (Concern B —
            // supersedes the Gate B r1 F1 "RESETS to true" rule):
            // group_scope_component_required RESETS to `greq` — THIS
            // groupRef's own `presence='required'` — at this new group's own
            // boundary (symmetric with xml_loader.cpp). A direct member of an
            // OPTIONAL group must not enter `group_required_pairs_out` even
            // when its own `presence='required'` (QuickFIX `addXMLGroup`'s
            // `required=="Y" && groupRequired`); NOT an AND across ancestor
            // groups — each group's own boundary reset discards whatever the
            // enclosing accumulator carried.
            // 083 T030: open this group's capture frame. `path` carries its own
            // no_tag only for the DESCENDANTS' keys — popped again before this
            // group's own record is emitted, so that record's `parent_path`
            // EXCLUDES no_tag (C-1.3 / Entity 1).
            if (delim_cap != nullptr) {
                delim_cap->path.push_back(no_tag);
                delim_cap->pending.push_back(0);
            }
            expand_field_list(group_node, out, required_out, group_required_pairs_out, no_tag,
                              enclosing_component_index, delim_cap,
                              /*in_group=*/true, component_required,
                              /*group_scope_component_required=*/greq);  // 081 D-3
            if (delim_cap != nullptr) {
                std::uint16_t const captured = delim_cap->pending.back();
                delim_cap->pending.pop_back();
                delim_cap->path.pop_back();
                if (captured != 0) {
                    delim_cap->out.push_back(
                        detail::make_group_ctx_delim(delim_cap->path, no_tag, captured));
                }
                // 083 T036 (FR-006 / C-6.1), symmetric with xml_loader.cpp:
                // `captured == 0` means this group emitted no first member, so
                // its delimiter is unresolvable. Fail-closed by default.
                // C-6.1b / FR-006c: the type is the DERIVED
                // `orchestra_parse_error`, not the base -- the Orchestra fuzz
                // harness catches the derived type, so a base xml_parse_error
                // would escape to its terminal rethrow and crash the fuzzer.
                // C-6.1a / FR-006d holds STRUCTURALLY: this branch runs only
                // under a non-null sink, i.e. only in the message-scoped walk.
                // 082 FR-023: the diagnostic MUST name the group's `name`
                // attribute as well as its `no_tag` — "the facts an operator
                // needs to fix the offending dialect" (`error.hpp:73`). The
                // `<fix>` twin already names it (`xml_loader.cpp`'s
                // `<group name="...">`); this one did not, which is the ONE
                // gap found when FR-023's own pins were re-pointed onto this
                // disposition. `name` is optional on `<fixr:group>`, so an
                // absent attribute degrades to the id-only form rather than
                // printing an empty `name=""`.
                else if (unresolved_policy_ == unresolved_group_policy::fail_closed) {
                    std::string_view const gname{group_node.attribute("name").as_string("")};
                    throw orchestra_parse_error(
                        "dict::orchestra_parse_error: <fixr:group" +
                        (gname.empty() ? std::string{} : " name=\"" + std::string{gname} + "\"") +
                        "> with <fixr:numInGroup id=\"" + std::to_string(no_tag) +
                        "\"> declares no first member, so its delimiter cannot be resolved; "
                        "pass unresolved_group_policy::tolerant to skip it instead");
                }
                // FR-006a tolerant mode: skip, leaving the group UNREGISTERED
                // rather than half-registered (FR-023a).
            }
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
        // Gate B r1 F1 (fixpp#201): context-exact group-relative required
        // (no_tag,tag) pairs (mirrors xml_loader.cpp).
        std::vector<std::pair<std::uint16_t, std::uint16_t>> msg_group_required;
        // No separate header/trailer nodes in Orchestra — StandardHeader
        // (component 1024) / StandardTrailer (1025) are ordinary componentRefs
        // inside each message's own <fixr:structure> (brief).
        // 083 T030 (C-1.1): the ONLY message-scoped expansion, and therefore
        // the only one that may emit Entity-2 records.
        detail::DelimCapture delim_cap;
        if (md.node) {
            expand_field_list(md.node, msg_fields, msg_required, msg_group_required, 0, 0,
                              &delim_cap);
        }
        auto const [field_run, req_run] = append_run(msg_fields, msg_required);
        h.per_msg_field_offsets_.push_back(field_run);
        h.per_msg_required_offsets_.push_back(req_run);

        std::ranges::sort(msg_group_required);
        auto const grlast = std::ranges::unique(msg_group_required).begin();
        msg_group_required.erase(grlast, msg_group_required.end());
        detail::MsgFieldsRun group_req_run{
            .start = static_cast<std::uint32_t>(h.msg_group_required_pool_.size()),
            .count = static_cast<std::uint32_t>(msg_group_required.size())};
        h.msg_group_required_pool_.insert(h.msg_group_required_pool_.end(),
                                          msg_group_required.begin(), msg_group_required.end());
        h.per_msg_group_required_offsets_.push_back(group_req_run);

        // 083 T030 (Entity 2): flush this message's records — same sort, same
        // key-dedup and same run shape as xml_loader.cpp.
        detail::flush_group_ctx_delims(h, delim_cap);
    }

    // ── 083 T030 (research D-10 / C-1.4b / C-7.2's write-order leg) ─────────
    // Repopulate the global `first_field_tag` as a FIRST-SEEN PROJECTION of
    // Entity 2, symmetric with xml_loader.cpp. WRITE ORDER IS LOAD-BEARING:
    // the 072 nested/parent delimiter collision guard below reads
    // `first_field_tag` and THROWS on a match, so it must see post-projection
    // values rather than a half-populated global.
    for (auto const& rec : h.group_ctx_delim_pool_) {
        auto const git = group_index_by_no_tag_.find(rec.no_tag);
        if (git == group_index_by_no_tag_.end()) {
            continue;
        }
        auto& gd = groups_[git->second];
        if (gd.first_field_tag == 0) {
            gd.first_field_tag = rec.delimiter;  // first-seen wins
        }
    }

    // ── 083 T041 (FR-023 / C-3.4): Entity-2 completeness invariant ──────────
    // Every context `as_table_view()` will register must have a record. Runs
    // AFTER the projection above (so `first_field_tag` is final) and at
    // finalize() rather than at as_table_view(), which is contractually
    // non-throwing and stays so -- that is what makes a consumer-side miss
    // unreachable by construction instead of merely unobserved.
    //
    // NO silent fallback is available here by design: falling back to
    // `group_first_field(no_tag)` would reinstate this feature's own defect,
    // and to `members.front()` a worse one already fixed and pinned
    // (ValidatorProductionTableView.GroupDelimiterFromWireNotTagSortedMember).
    //
    // FR-023a: a tolerantly-skipped group never reaches here as a violation --
    // `captured == 0` means no FieldRef was emitted at that group's level, so
    // no FieldRef carries its `group_no_tag`, so the `!members.empty()` leg
    // excludes it from the registered set in the first place.
    detail::maybe_drop_first_group_ctx_delim_run_for_testing(h);  // Gate B r1 F1 test seam
    if (auto const bad = detail::find_incomplete_group_context(h); bad) {
        throw orchestra_parse_error(
            "dict::orchestra_parse_error: group context for NumInGroup tag " +
            std::to_string(bad->second) + " in message '" +
            std::string{messages_[bad->first].msg_type} +
            "' is registered by as_table_view() but has no per-context delimiter "
            "record (FR-023 completeness invariant)");
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
        std::vector<std::pair<std::uint16_t, std::uint16_t>> comp_group_required;  // discard —
        // any group reached here is ALSO reached (and correctly captured) via
        // its own standalone OrchestraGroupDef walk below.
        expand_field_list(def.node, comp_fields, comp_required, comp_group_required,
                          /*enclosing_group_no_tag=*/0,
                          /*enclosing_component_index=*/static_cast<std::uint16_t>(i + 1),
                          // 083 C-1.1: not message-scoped — emits nothing.
                          /*delim_cap=*/nullptr);

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
    h.group_required_offsets_.reserve(groups_.size());
    for (auto const& g : groups_) {
        std::uint16_t first_idx = 0;
        std::uint16_t cnt = 0;
        // Gate B r1 F1 (fixpp#201, mirrors xml_loader.cpp): `grp_required` is
        // the GROUP-RELATIVE direct required-member set for THIS group — this
        // standalone call starts fresh at the group's own boundary. 081-
        // strict-validation-residuals D-3 (symmetric with xml_loader.cpp):
        // `component_required` seeds from `g.first_seen_required` (the
        // first-seen groupRef's own presence) instead of the unconditional
        // `true` default, so an optional group's bare fallback reports no
        // direct group-required members (Contract 1a leg 2).
        std::vector<FieldRef> grp_fields;
        std::vector<std::uint16_t> grp_required;
        if (g.node) {
            std::vector<std::pair<std::uint16_t, std::uint16_t>> grp_group_required;  // discard
            expand_field_list(g.node, grp_fields, grp_required, grp_group_required,
                              /*enclosing_group_no_tag=*/g.no_tag,
                              /*enclosing_component_index=*/0,
                              // 083 C-1.1: not message-scoped — emits nothing.
                              /*delim_cap=*/nullptr,
                              /*in_group=*/false,
                              /*component_required=*/g.first_seen_required);
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

        // Parallel to h.groups_ (SAME index — pushed in lockstep above).
        std::ranges::sort(grp_required);
        auto const grplast = std::ranges::unique(grp_required).begin();
        grp_required.erase(grplast, grp_required.end());
        detail::MsgFieldsRun greq_run{
            .start = static_cast<std::uint32_t>(h.group_required_pool_.size()),
            .count = static_cast<std::uint32_t>(grp_required.size())};
        h.group_required_pool_.insert(h.group_required_pool_.end(), grp_required.begin(),
                                      grp_required.end());
        h.group_required_offsets_.push_back(greq_run);
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
    pugi::xml_document const& doc, std::pmr::memory_resource* mr, unresolved_group_policy policy) {
    OrchestraLoaderState st{mr, policy};
    st.parse_document(doc);
    return st.finalize();
}

}  // namespace

Dictionary OrchestraLoader::load(std::filesystem::path const& path, std::pmr::memory_resource* mr,
                                 unresolved_group_policy policy) {
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
        return Dictionary{build_handle_from_doc(doc, mr, policy)};
    });
}

Dictionary OrchestraLoader::load_from_string(std::string_view xml, std::pmr::memory_resource* mr,
                                             unresolved_group_policy policy) {
    assert(mr != nullptr && "OrchestraLoader::load_from_string: mr must not be null");
    return fixpp::core::detail::trap_throw_or_throw<xml_oom_error>([&] {
        pugi::xml_document doc;
        auto const result = doc.load_buffer(xml.data(), xml.size());
        if (!result) {
            throw orchestra_parse_error(std::string{"dict::orchestra_parse_error: "} +
                                        result.description());
        }
        return Dictionary{build_handle_from_doc(doc, mr, policy)};
    });
}

}  // namespace fixpp::dict
