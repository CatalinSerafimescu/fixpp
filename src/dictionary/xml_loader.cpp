// SPDX-License-Identifier: AGPL-3.0-or-later
// src/dictionary/xml_loader.cpp
//
// `fixpp::dict::XmlLoader::load` and `::load_from_string` impls. Walks a
// QuickFIX-XML-format dictionary using pugixml, emits sorted FieldRef[],
// ComponentRef[], GroupRef[], MessageEntry[] tables onto the caller-supplied
// `std::pmr::memory_resource`, and returns a frozen `Dictionary` by value.
//
// Per research.md D-15, pugixml is consumed ONLY in this TU; public headers
// in `include/fixpp/dict/` do not transitively expose it.
//
// Error translation (research.md D-4):
//   - filesystem access failure  → dict::xml_parse_error
//   - pugi xml_parse_result != ok → dict::xml_parse_error
//   - missing/non-numeric `<field number>` → dict::xml_parse_error
//   - duplicate `<field number="N">` → dict::xml_parse_error
//   - dangling `<component name="X">` ref → dict::xml_parse_error
//   - unknown `<field type="...">` → dict::xml_parse_error
//   - `<fix major minor>` outside v1.0-9 → dict::unknown_version_error
//   - PMR std::bad_alloc → dict::xml_oom_error (via trap_throw_or_throw)

#include <algorithm>
#include <cassert>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fixpp/core/decimal_helpers.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/error.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fstream>
#include <ios>
#include <memory>
#include <memory_resource>
#include <pugixml.hpp>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
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
// Field-type vocabulary per `[FIX50SP2 §3.3]` (research.md D-14 enum naming).
// ----------------------------------------------------------------------------

struct FieldTypeEntry {
    std::string_view xml_name;
    field_data_type enum_value;
};

constexpr FieldTypeEntry kFieldTypeTable[] = {
    {.xml_name = "INT", .enum_value = field_data_type::Int},
    {.xml_name = "LENGTH", .enum_value = field_data_type::Length},
    {.xml_name = "SEQNUM", .enum_value = field_data_type::SeqNum},
    {.xml_name = "NUMINGROUP", .enum_value = field_data_type::NumInGroup},
    {.xml_name = "DAYOFMONTH", .enum_value = field_data_type::DayOfMonth},
    {.xml_name = "PRICE", .enum_value = field_data_type::Price},
    {.xml_name = "QTY", .enum_value = field_data_type::Qty},
    {.xml_name = "AMT", .enum_value = field_data_type::Amt},
    {.xml_name = "PRICEOFFSET", .enum_value = field_data_type::PriceOffset},
    {.xml_name = "PERCENTAGE", .enum_value = field_data_type::Percentage},
    {.xml_name = "FLOAT", .enum_value = field_data_type::Float},
    {.xml_name = "CHAR", .enum_value = field_data_type::Char},
    {.xml_name = "BOOLEAN", .enum_value = field_data_type::Boolean},
    {.xml_name = "STRING", .enum_value = field_data_type::String},
    {.xml_name = "MULTIPLECHARVALUE", .enum_value = field_data_type::MultiCharValue},
    {.xml_name = "MULTIPLEVALUESTRING", .enum_value = field_data_type::MultiStringValue},
    {.xml_name = "MULTIPLESTRINGVALUE", .enum_value = field_data_type::MultiStringValue},
    {.xml_name = "CURRENCY", .enum_value = field_data_type::Currency},
    {.xml_name = "EXCHANGE", .enum_value = field_data_type::Exchange},
    {.xml_name = "COUNTRY", .enum_value = field_data_type::Country},
    {.xml_name = "MONTHYEAR", .enum_value = field_data_type::MonthYear},
    {.xml_name = "UTCTIMESTAMP", .enum_value = field_data_type::UtcTimestamp},
    {.xml_name = "UTCTIMEONLY", .enum_value = field_data_type::UtcTimeOnly},
    {.xml_name = "UTCDATEONLY", .enum_value = field_data_type::UtcDateOnly},
    {.xml_name = "UTCDATE", .enum_value = field_data_type::UtcDateOnly},
    {.xml_name = "LOCALMKTDATE", .enum_value = field_data_type::LocalMktDate},
    {.xml_name = "TZTIMEONLY", .enum_value = field_data_type::TzTimeOnly},
    {.xml_name = "TZTIMESTAMP", .enum_value = field_data_type::TzTimestamp},
    {.xml_name = "LANGUAGE", .enum_value = field_data_type::Language},
    {.xml_name = "DATA", .enum_value = field_data_type::Data},
    {.xml_name = "XMLDATA", .enum_value = field_data_type::XmlData},
    // FIX 5.0 / 5.0SP2 additions mapped to nearest representable type
    // (data-model.md Entity 1 keeps the enum locked to [FIX50SP2 §3.3]
    // canonical names; runtime-loaded XML that uses post-canonical names is
    // accepted via this collapse table — research.md D-14 typing carve-out).
    {.xml_name = "TAGNUM", .enum_value = field_data_type::Int},
    {.xml_name = "LOCALMKTTIME", .enum_value = field_data_type::LocalMktDate},
    {.xml_name = "XID", .enum_value = field_data_type::String},
    {.xml_name = "XIDREF", .enum_value = field_data_type::String},
    // FIX 4.0 / 4.1 pre-canonical legacy field types (064-fix4041-legacy-types /
    // D-004). Same collapse mechanism as the post-canonical rows above; the enum
    // stays locked to [FIX50SP2 §3.3]. TIME -> UtcTimestamp agrees with QuickFIX
    // DataDictionary::XMLTypeToType (research R2). DATE -> LocalMktDate is a
    // deliberate stronger-typing choice over QuickFIX (which has no DATE branch ->
    // TYPE::Unknown, no validation); metadata-only, since field_type_from_data_type
    // collapses both to field_type::String (research R3/R4, spec.md FR-009).
    {.xml_name = "TIME", .enum_value = field_data_type::UtcTimestamp},
    {.xml_name = "DATE", .enum_value = field_data_type::LocalMktDate},
};

// T044 (075-live-wire-enum-validation, /clarify disposition "mirror QuickFIX"):
// pre-FIX.4.2 dictionaries (FIX40.xml, FIX41.xml) declare `BeginString(8)` and
// `CheckSum(10)` — and, dictionary-wide, every other CHAR-typed field — as
// `CHAR` even though `8=FIX.4.1`/`10=047` are multi-byte. Our XML is a
// byte-exact vendored copy of QuickFIX's own `spec/FIX41.xml`; the fix is NOT
// in the data, it is in how the loader interprets `CHAR` for old dictionaries.
// QuickFIX applies the exact same dictionary-wide relaxation at load time —
// `DataDictionary::XMLTypeToType`, src/C++/DataDictionary.cpp:589-592:
//     if (m_beginString < "FIX.4.2" && type == "CHAR") return TYPE::String;
// `legacy_char_is_string` mirrors that condition. Do NOT "clean this up" —
// this is deliberate reference-engine parity, not a bug.
[[nodiscard]] bool resolve_field_type(std::string_view name, bool legacy_char_is_string,
                                      field_data_type& out) noexcept {
    if (legacy_char_is_string && name == "CHAR") {
        out = field_data_type::String;
        return true;
    }
    for (auto const& row : kFieldTypeTable) {
        // cppcheck-suppress useStlAlgorithm  // linear search over a small constexpr table;
        // std::find_if's predicate-wrap isn't a win here
        if (row.xml_name == name) {
            out = row.enum_value;
            return true;
        }
    }
    return false;
}

// ----------------------------------------------------------------------------
// Version-string parse table — the v1.0-supported nine.
// ----------------------------------------------------------------------------

struct VersionEntry {
    std::string_view type_attr;
    std::uint8_t major;
    std::uint8_t minor;
    std::uint8_t servicepack;
    session_version value;
};

constexpr VersionEntry kVersionTable[] = {
    {.type_attr = "FIX", .major = 4, .minor = 0, .servicepack = 0, .value = session_version::v40},
    {.type_attr = "FIX", .major = 4, .minor = 1, .servicepack = 0, .value = session_version::v41},
    {.type_attr = "FIX", .major = 4, .minor = 2, .servicepack = 0, .value = session_version::v42},
    {.type_attr = "FIX", .major = 4, .minor = 3, .servicepack = 0, .value = session_version::v43},
    {.type_attr = "FIX", .major = 4, .minor = 4, .servicepack = 0, .value = session_version::v44},
    {.type_attr = "FIX", .major = 5, .minor = 0, .servicepack = 0, .value = session_version::v50},
    {.type_attr = "FIX",
     .major = 5,
     .minor = 0,
     .servicepack = 1,
     .value = session_version::v50sp1},
    {.type_attr = "FIX",
     .major = 5,
     .minor = 0,
     .servicepack = 2,
     .value = session_version::v50sp2},
    {.type_attr = "FIXT", .major = 1, .minor = 1, .servicepack = 0, .value = session_version::vt11},
};

[[nodiscard]] session_version resolve_version(std::string_view type_attr, int major, int minor,
                                              int servicepack) noexcept {
    // cppcheck-suppress-begin useStlAlgorithm
    for (auto const& row : kVersionTable) {
        if (row.type_attr == type_attr && std::cmp_equal(row.major, major) &&
            std::cmp_equal(row.minor, minor) && std::cmp_equal(row.servicepack, servicepack)) {
            return row.value;
        }
    }
    // cppcheck-suppress-end useStlAlgorithm
    return session_version::Unknown;
}

// ----------------------------------------------------------------------------
// The bytewise name comparator (research.md D-6) is `detail::bytewise_compare`
// in dictionary_internal.hpp — shared with dictionary.cpp so the build-time
// sort key and the query-time lookup key are byte-identical.
// ----------------------------------------------------------------------------

// Helper for parse_version: parse a non-negative decimal int from `s`,
// requiring the whole span to be consumed. Returns false on any failure.
[[nodiscard]] bool parse_nonneg_int(std::string_view s, int& out) noexcept {
    auto const r = std::from_chars(s.data(), s.data() + s.size(), out);
    return r.ec == std::errc{} && r.ptr == s.data() + s.size() && out >= 0;
}

// ----------------------------------------------------------------------------
// Build-time scaffolding (NOT PMR — uses default allocators because pugixml
// itself goes through malloc per research.md D-1. We free this before the
// loader returns; the `Dictionary`'s metadata block is the only thing the
// caller observes, and that block lives on `mr`).
// ----------------------------------------------------------------------------

// 075-live-wire-enum-validation T012 (FR-001): one <value enum="X"
// description="Y"/> child of a global <field>, pre-dedupe (FR-017) and
// pre-intern. Plain std::string (not PMR) — matches the rest of this file's
// build-time scaffolding, which is freed before the loader returns; only the
// interned bytes in `dict_metadata_handle::name_pool_` survive.
struct PendingEnumCode {
    std::string value;
    std::string description;
};

struct GlobalFieldInfo {
    std::uint16_t tag{0};
    std::string name;
    field_data_type type{};
    std::uint16_t length_pair_data_tag{0};    // populated post-parse
    std::vector<PendingEnumCode> enum_codes;  // T012 — declaration-order, deduped by value
};

struct ComponentDef {
    std::string name;
    // Children: each entry is either a field (by name), a group (by no_tag
    // name), or a nested component (by name). We expand inline at usage time.
    pugi::xml_node node;  // root component XML node, walked at expand time
};

struct GroupDef {
    std::uint16_t no_tag{0};
    std::uint16_t first_field_tag{0};
    std::uint16_t first_field_index{0};  // patched post-emit
    std::uint16_t field_count{0};
    std::uint16_t parent_group_no_tag{0};
    pugi::xml_node node;  // the <group> XML node, stored at first-seen time
};

struct MessageDef {
    std::string msg_type;
    std::string name;
    pugi::xml_node node;
};

// ----------------------------------------------------------------------------
// Loader state: holds the DOM, global field lookups, parsed components, and
// the build vectors for the final metadata handle.
// ----------------------------------------------------------------------------

class LoaderState {
public:
    explicit LoaderState(std::pmr::memory_resource* mr) noexcept : mr_(mr) {}

    void parse_document(pugi::xml_document const& doc);

    // Returns a finished metadata handle (ownership transferred to caller).
    [[nodiscard]] detail::dict_metadata_handle_ptr finalize();

private:
    [[nodiscard]] std::uint32_t intern_name_in_pool(detail::dict_metadata_handle& h,
                                                    std::string_view name);

    void parse_version(pugi::xml_node const& root);
    void parse_global_fields(pugi::xml_node const& root);
    void collect_components(pugi::xml_node const& root);
    void collect_messages(pugi::xml_node const& root);

    // Expand a list of <field>/<component>/<group> child rows into a flat
    // FieldRef vector. Components are resolved recursively. Groups emit a
    // GroupRef AND their inner fields (the group's first field is the first
    // field declared inside it, per FIX semantics).
    // `in_group` (fixpp#201): true once expansion has descended into ANY
    // repeating <group>. Group-member `required='Y'` fields are queryable
    // per-group (via each FieldRef's own `rule`), but must NOT leak into the
    // MESSAGE-level `required_out` — QuickFIX composes message-level required-
    // ness top-level-scoped and checks group members per instance. `out` and
    // each FieldRef's `rule` are unaffected; only `required_out` membership is.
    // `component_required` (079-required-presence-scope T020 fix; census
    // T015 found this is NOT vacuous — 4 real sites): the running AND of
    // every enclosing <component required=.../> usage's own `required`
    // attribute (default "N"/optional, mirroring QuickFIX
    // `DataDictionary.cpp:401-403,510` `componentRequired`) on the path from
    // the message root to `parent`. A field/group's own `required='Y'` only
    // reaches message-level `required_out` when `component_required` is ALSO
    // true — an optional componentRef's contents must not leak into the
    // message-level required set even when the nested field/group itself says
    // `required='Y'`. Does NOT affect `FieldRef.rule` (per-field presence,
    // FR-008) — only `required_out` membership.
    // `group_scope_component_required` (Gate B r1 F1 — fixpp#201 escalation;
    // 081-strict-validation-residuals D-3 amends the reset value below): a
    // SEPARATE component-AND accumulator, distinct from `component_required`
    // above — RESET on entry to each `<group>` to THIS group's own
    // `required=` (081 D-3; was unconditionally `true` under 079) — unlike
    // `component_required`, which threads through group boundaries unchanged
    // because it feeds the MESSAGE-root `required_out`. ANDed with a
    // componentRef's own `required` at every componentRef reached inside the
    // CURRENT group's own subtree. Gates `group_required_pairs_out`: a field/
    // group's own `required='Y'` becomes a DIRECT group-required member of its
    // immediately-enclosing group only when this group-relative AND is ALSO
    // true — i.e. `required=="Y" && groupRequired` (QuickFIX `addXMLGroup`):
    // an optional componentRef nested INSIDE a group must not leak its
    // contents into that group's required-member set, AND the immediately-
    // enclosing group itself must be `required='Y'` (081 D-3 — NOT an AND
    // across ancestor groups; each group's own boundary reset discards the
    // outer accumulator, seeding fresh from ITS OWN `required=`). See Gate B
    // r1 triage F1 (NoMDStatistics(2474) over-include vs NoSides(552)
    // over-exclude counterexamples) + 081 research.md D-3.
    void expand_field_list(
        pugi::xml_node const& parent, std::vector<FieldRef>& out,
        std::vector<std::uint16_t>& required_out,
        std::vector<std::pair<std::uint16_t, std::uint16_t>>& group_required_pairs_out,
        std::uint16_t enclosing_group_no_tag, std::uint16_t enclosing_component_index,
        bool in_group = false, bool component_required = true,
        bool group_scope_component_required = true);

    void detect_length_pairs(pugi::xml_node const& root);

    std::pmr::memory_resource* mr_;

    pugi::xml_node header_node_;
    pugi::xml_node trailer_node_;

    std::unordered_map<std::string, GlobalFieldInfo> by_name_;
    std::unordered_map<std::uint16_t, std::string> by_tag_;
    std::vector<ComponentDef> components_;
    std::unordered_map<std::string, std::uint16_t> component_index_by_name_;
    // Reverse parent map: parent_of_[child_name] = enclosing component's 1-based
    // id (component_index + 1). Populated during collect_components() by walking
    // each component's XML body for nested <component> references. Used in
    // finalize() to set ComponentRef::parent_component_id per [2c §4.2]:
    // "0 if top-level; otherwise the enclosing component's 1-based id".
    // When a child component is referenced from multiple parents, the first
    // encountered in declaration order wins (FIX XML is typically acyclic and
    // single-parent; the tiebreak is documented here for clarity).
    std::unordered_map<std::string, std::uint16_t> parent_of_;
    std::vector<GroupDef> groups_;
    std::unordered_map<std::uint16_t, std::uint16_t> group_index_by_no_tag_;
    std::vector<MessageDef> messages_;

    session_version version_{session_version::Unknown};
};

// ----------------------------------------------------------------------------

void LoaderState::parse_version(pugi::xml_node const& root) {
    if (std::string(root.name()) != "fix") {
        throw xml_parse_error("dict::xml_parse_error: root element is not <fix>");
    }
    auto const type_attr = std::string_view{root.attribute("type").as_string("FIX")};
    auto const major_attr = root.attribute("major");
    auto const minor_attr = root.attribute("minor");
    auto const sp_attr = root.attribute("servicepack");
    if (!major_attr || !minor_attr) {
        throw xml_parse_error("dict::xml_parse_error: <fix> missing major/minor attributes");
    }
    int major = 0;
    int minor = 0;
    int sp = 0;
    if (std::string const s{major_attr.as_string("")}; !parse_nonneg_int(s, major)) {
        throw unknown_version_error("dict::unknown_version_error: non-numeric or negative major");
    }
    if (std::string const s{minor_attr.as_string("")}; !parse_nonneg_int(s, minor)) {
        throw unknown_version_error("dict::unknown_version_error: non-numeric or negative minor");
    }
    if (sp_attr != nullptr) {
        if (std::string const s{sp_attr.as_string("")}; !s.empty() && !parse_nonneg_int(s, sp)) {
            throw unknown_version_error("dict::unknown_version_error: non-numeric servicepack");
        }
    }
    auto const resolved =
        resolve_version(type_attr, static_cast<std::uint8_t>(major),
                        static_cast<std::uint8_t>(minor), static_cast<std::uint8_t>(sp));
    if (resolved == session_version::Unknown) {
        std::ostringstream oss;
        oss << "dict::unknown_version_error: <fix type=\"" << type_attr << "\" major=\"" << major
            << "\" minor=\"" << minor << "\" servicepack=\"" << sp
            << "\"> not in v1.0 supported nine";
        throw unknown_version_error(oss.str());
    }
    version_ = resolved;
}

void LoaderState::parse_global_fields(pugi::xml_node const& root) {
    auto const fields = root.child("fields");
    if (!fields) {
        throw xml_parse_error("dict::xml_parse_error: missing <fields> block under <fix>");
    }
    // T044: `version_` (set by parse_version(), which always runs first —
    // see parse_document()) is resolved via kVersionTable's exact
    // type_attr+major+minor+servicepack match, so this equality check
    // already accounts for the "FIX" vs "FIXT" family split that a raw
    // major/minor-only comparison would get wrong (FIXT.1.1 has major=1,
    // minor=1 and must NOT be treated as pre-FIX.4.2). Scoped to exactly the
    // two pre-FIX.4.2 dictionaries QuickFIX's own rule targets.
    bool const legacy_char_is_string =
        version_ == session_version::v40 || version_ == session_version::v41;
    for (auto const& f : fields.children("field")) {
        auto const number_attr = f.attribute("number");
        if (!number_attr) {
            throw xml_parse_error("dict::xml_parse_error: <field> missing number attribute");
        }
        std::string const num_s{number_attr.as_string("")};
        int tag_i = 0;
        auto const r = std::from_chars(num_s.data(), num_s.data() + num_s.size(), tag_i);
        if (r.ec != std::errc{} || r.ptr != num_s.data() + num_s.size() || tag_i < 0 ||
            tag_i > 65535) {
            throw xml_parse_error("dict::xml_parse_error: <field number=\"" + num_s +
                                  "\"> non-numeric or out-of-range");
        }
        auto const tag = static_cast<std::uint16_t>(tag_i);
        auto const name = std::string{f.attribute("name").as_string("")};
        auto const type_s = std::string{f.attribute("type").as_string("")};
        if (name.empty()) {
            throw xml_parse_error("dict::xml_parse_error: <field number=\"" + num_s +
                                  "\"> missing name attribute");
        }
        field_data_type ft{};
        if (!resolve_field_type(type_s, legacy_char_is_string, ft)) {
            throw xml_parse_error("dict::xml_parse_error: <field type=\"" + type_s +
                                  "\"> outside [FIX50SP2 §3.3] vocabulary");
        }
        if (by_tag_.contains(tag)) {
            throw xml_parse_error("dict::xml_parse_error: duplicate <field number=\"" + num_s +
                                  "\">");
        }
        GlobalFieldInfo info{.tag = tag, .name = name, .type = ft, .length_pair_data_tag = 0};

        // T012/T013 (FR-001/FR-017): parse <value enum="X" description="Y"/>
        // children into the enum-domain store. Duplicate `enum` values on one
        // field are DEDUPED (union semantics, no error — QuickFIX's std::set
        // membership is idempotent, research.md R-5); a `<value>` missing its
        // `enum` attribute is a load failure (fail-closed, mirroring
        // QuickFIX's ConfigError); a missing `description` is legal and
        // yields an empty description view (diagnostics-only).
        std::unordered_set<std::string> seen_codes;
        for (auto const& v : f.children("value")) {
            auto const enum_attr = v.attribute("enum");
            if (!enum_attr) {
                throw xml_parse_error("dict::xml_parse_error: <value> under <field number=\"" +
                                      num_s + "\"> missing enum attribute");
            }
            std::string code_val{enum_attr.as_string("")};
            if (!seen_codes.insert(code_val).second) {
                continue;  // FR-017 dedupe
            }
            std::string desc{v.attribute("description").as_string("")};
            info.enum_codes.push_back(
                {.value = std::move(code_val), .description = std::move(desc)});
        }

        by_tag_.emplace(tag, name);
        by_name_.emplace(name, std::move(info));
    }
}

void LoaderState::collect_components(pugi::xml_node const& root) {
    auto const comps = root.child("components");
    if (!comps) {
        return;
    }
    std::uint16_t next_id = 0;
    for (auto const& c : comps.children("component")) {
        auto const name = std::string{c.attribute("name").as_string("")};
        if (name.empty()) {
            throw xml_parse_error("dict::xml_parse_error: <component> missing name attribute");
        }
        if (component_index_by_name_.contains(name)) {
            throw xml_parse_error("dict::xml_parse_error: duplicate <component name=\"" + name +
                                  "\">");
        }
        components_.push_back({.name = name, .node = c});
        component_index_by_name_.emplace(name, next_id);
        ++next_id;
    }

    // Build the reverse parent map: for each top-level component P, walk its
    // XML body for nested <component name="C" /> references and record
    // parent_of_[C] = index_of(P) + 1 (1-based id, matching the convention
    // used in ComponentRef::parent_component_id per [2c §4.2]).
    // Only the first-seen enclosing parent is recorded (FIX XML is acyclic and
    // typically single-parent; the "first declaration order" tiebreak is
    // documented on the parent_of_ member comment above).
    for (auto const& def : components_) {
        auto const pit = component_index_by_name_.find(def.name);
        if (pit == component_index_by_name_.end()) {
            continue;
        }
        auto const parent_1based = static_cast<std::uint16_t>(pit->second + 1);
        for (auto const& child : def.node.children()) {
            if (std::string_view{child.name()} == "component") {
                auto const cname = std::string{child.attribute("name").as_string("")};
                // Only record the first parent encountered (declaration order).
                parent_of_.emplace(cname, parent_1based);
            }
        }
    }
}

void LoaderState::collect_messages(pugi::xml_node const& root) {
    auto const msgs = root.child("messages");
    if (!msgs) {
        throw xml_parse_error("dict::xml_parse_error: missing <messages> block under <fix>");
    }
    for (auto const& m : msgs.children("message")) {
        auto const msg_type = std::string{m.attribute("msgtype").as_string("")};
        auto const name = std::string{m.attribute("name").as_string("")};
        if (msg_type.empty()) {
            throw xml_parse_error("dict::xml_parse_error: <message> missing msgtype attribute");
        }
        if (name.empty()) {
            throw xml_parse_error("dict::xml_parse_error: <message> missing name attribute");
        }
        messages_.push_back({.msg_type = msg_type, .name = name, .node = m});
    }
}

// NOLINTBEGIN(misc-no-recursion,bugprone-easily-swappable-parameters) — recursive XML walk by
// design (component refs expand inline); the two uint16_t params name different domains (group's
// no_tag vs component index) and aren't interchangeable.
void LoaderState::expand_field_list(
    pugi::xml_node const& parent, std::vector<FieldRef>& out,
    std::vector<std::uint16_t>& required_out,
    std::vector<std::pair<std::uint16_t, std::uint16_t>>& group_required_pairs_out,
    std::uint16_t enclosing_group_no_tag, std::uint16_t enclosing_component_index, bool in_group,
    bool component_required, bool group_scope_component_required) {
    for (auto const& child : parent.children()) {
        std::string_view const tag_name{child.name()};
        if (tag_name == "field") {
            auto const fname = std::string{child.attribute("name").as_string("")};
            if (fname.empty()) {
                throw xml_parse_error("dict::xml_parse_error: <field> reference missing name");
            }
            auto const it = by_name_.find(fname);
            if (it == by_name_.end()) {
                throw xml_parse_error("dict::xml_parse_error: <field name=\"" + fname +
                                      "\"> not declared in <fields> block");
            }
            auto const& info = it->second;
            bool const req = std::string_view{child.attribute("required").as_string("N")} == "Y";
            FieldRef fr{};
            fr.tag = info.tag;
            fr.type = info.type;
            fr.rule = req ? field_presence::Required : field_presence::Optional;
            fr.group_no_tag = enclosing_group_no_tag;
            fr.component_index = enclosing_component_index;
            fr.length_pair_data_tag = info.length_pair_data_tag;
            out.push_back(fr);
            if (req && !in_group && component_required) {  // fixpp#201: group-member requireds
                required_out.push_back(info.tag);  // stay per-group; 079: optional-component AND
            }
            // Gate B r1 F1 (fixpp#201): a DIRECT group-required member of the
            // CURRENT immediately-enclosing group — group-relative component-AND
            // (reset at THIS group's own boundary), independent of the
            // message-root `component_required` above.
            if (req && group_scope_component_required && enclosing_group_no_tag != 0) {
                group_required_pairs_out.emplace_back(enclosing_group_no_tag, info.tag);
            }
        } else if (tag_name == "component") {
            auto const cname = std::string{child.attribute("name").as_string("")};
            auto const cit = component_index_by_name_.find(cname);
            if (cit == component_index_by_name_.end()) {
                throw xml_parse_error("dict::xml_parse_error: <component name=\"" + cname +
                                      "\"> not defined in <components> block");
            }
            auto const& def = components_[cit->second];
            // 079-required-presence-scope T020: componentRef's own `required`
            // (default "N", QuickFIX `DataDictionary.cpp:401-403` parity) ANDs
            // into the running component_required for the recursion.
            bool const comp_req =
                std::string_view{child.attribute("required").as_string("N")} == "Y";
            expand_field_list(def.node, out, required_out, group_required_pairs_out,
                              enclosing_group_no_tag, static_cast<std::uint16_t>(cit->second + 1),
                              in_group, component_required && comp_req,
                              group_scope_component_required && comp_req);
        } else if (tag_name == "group") {
            auto const gname = std::string{child.attribute("name").as_string("")};
            auto const nit = by_name_.find(gname);
            if (nit == by_name_.end()) {
                throw xml_parse_error("dict::xml_parse_error: <group name=\"" + gname +
                                      "\"> has no matching <field> declaration (NoXxx tag)");
            }
            auto const no_tag = nit->second.tag;
            // Emit the NoXxx field itself first per QuickFIX convention.
            bool const greq = std::string_view{child.attribute("required").as_string("N")} == "Y";
            FieldRef no_fr{};
            no_fr.tag = no_tag;
            no_fr.type = nit->second.type;
            no_fr.rule = greq ? field_presence::Required : field_presence::Optional;
            no_fr.group_no_tag = enclosing_group_no_tag;
            no_fr.component_index = enclosing_component_index;
            no_fr.length_pair_data_tag = nit->second.length_pair_data_tag;
            out.push_back(no_fr);
            if (greq && !in_group && component_required) {  // fixpp#201: a nested group's count
                required_out.push_back(no_tag);  // field is not message-level required (per-
            }  // instance instead); 079: optional-component AND
            // Gate B r1 F1: the nested group's own NoXxx count-tag is a DIRECT
            // member of the OUTER group (enclosing_group_no_tag), gated by the
            // OUTER group's own group-relative AND (this group's own `greq` is
            // handled separately, once we recurse into ITS scope below).
            if (greq && group_scope_component_required && enclosing_group_no_tag != 0) {
                group_required_pairs_out.emplace_back(enclosing_group_no_tag, no_tag);
            }
            // Record the GroupRef (deduplicated by no_tag — first-seen wins).
            if (!group_index_by_no_tag_.contains(no_tag)) {
                std::uint16_t first_field_tag = 0;
                for (auto const& gc : child.children()) {
                    std::string_view const tn{gc.name()};
                    if (tn == "field" || tn == "group") {
                        auto const cn = std::string{gc.attribute("name").as_string("")};
                        auto const fi = by_name_.find(cn);
                        if (fi != by_name_.end()) {
                            first_field_tag = fi->second.tag;
                            break;
                        }
                    } else if (tn == "component") {
                        auto const cn = std::string{gc.attribute("name").as_string("")};
                        auto const ci = component_index_by_name_.find(cn);
                        if (ci != component_index_by_name_.end()) {
                            // Use first <field> of the resolved component.
                            for (auto const& cf : components_[ci->second].node.children()) {
                                std::string_view const cfn{cf.name()};
                                if (cfn == "field") {
                                    auto const fn = std::string{cf.attribute("name").as_string("")};
                                    auto const fi2 = by_name_.find(fn);
                                    if (fi2 != by_name_.end()) {
                                        first_field_tag = fi2->second.tag;
                                        break;
                                    }
                                }
                            }
                            if (first_field_tag != 0) {
                                break;
                            }
                        }
                    }
                }
                GroupDef gd{};
                gd.no_tag = no_tag;
                gd.first_field_tag = first_field_tag;
                gd.parent_group_no_tag = enclosing_group_no_tag;
                gd.node = child;  // store for group_fields_ expansion in finalize()
                auto const idx = static_cast<std::uint16_t>(groups_.size());
                group_index_by_no_tag_.emplace(no_tag, idx);
                groups_.push_back(gd);
            }
            // Recurse into group children with the group-context set. fixpp#201:
            // in_group=true so descendant requireds do NOT leak to message level.
            // 079: component_required carries through unchanged (a group ref
            // itself has no `required`-usage semantics on this axis). 081-
            // strict-validation-residuals D-3 (Concern B — supersedes the Gate
            // B r1 F1 "RESETS to true" rule below): group_scope_component_
            // required RESETS to `greq` — THIS group's own `required=` — not
            // unconditionally `true`. A direct member of an OPTIONAL group must
            // NOT enter `group_required_pairs_out` even when its own
            // `required='Y'` (QuickFIX `addXMLGroup`'s
            // `required=="Y" && groupRequired`; research.md D-3). This is NOT
            // an AND across ancestor groups — each group's own boundary reset
            // discards whatever the enclosing accumulator carried, seeding
            // fresh from ITS OWN `required=` (matches QuickFIX's per-group
            // sub-DataDictionary semantics, and the reworked independent
            // oracle in required_scope_oracle.hpp).
            expand_field_list(child, out, required_out, group_required_pairs_out, no_tag,
                              enclosing_component_index,
                              /*in_group=*/true, component_required,
                              /*group_scope_component_required=*/greq);
        }
        // Ignore unknown child elements (forwards-compat).
    }
}
// NOLINTEND(misc-no-recursion,bugprone-easily-swappable-parameters)

void LoaderState::detect_length_pairs(pugi::xml_node const& root) {
    // Primary detection path (Option B per triage §R2): walk the global
    // <fields> block in declaration order. Whenever a LENGTH-typed entry is
    // immediately followed by a DATA- or XMLDATA-typed entry, record the pair.
    // This is the canonical FIX source per [FIX50SP2 §3.3] and captures
    // component-internal pairs (e.g., EncodedLegIssuerLen(618)→
    // EncodedLegIssuer(619) in InstrumentLeg) that were previously missed
    // because the old per-container walk never descended into <component> nodes.
    //
    // Secondary path: walk header, trailer, and every <message> container for
    // any LENGTH/DATA adjacency NOT present in the global <fields> block
    // (e.g., inline field reordering in message bodies). In practice the
    // global-fields path already captures all standard pairs; the secondary
    // walk retains the original coverage so no existing pair detection regresses.
    auto const mark_pair = [&](std::uint16_t length_tag, std::uint16_t data_tag) {
        auto const lit = by_tag_.find(length_tag);
        if (lit != by_tag_.end()) {
            auto const nit = by_name_.find(lit->second);
            if (nit != by_name_.end() && nit->second.length_pair_data_tag == 0) {
                nit->second.length_pair_data_tag = data_tag;
            }
        }
    };

    // Primary: global <fields> declaration order.
    {
        std::uint16_t prev_tag = 0;
        bool prev_is_length = false;
        for (auto const& f : root.child("fields").children("field")) {
            auto const fname = std::string{f.attribute("name").as_string("")};
            auto const it = by_name_.find(fname);
            if (it == by_name_.end()) {
                prev_is_length = false;
                continue;
            }
            auto const& info = it->second;
            bool const is_data =
                (info.type == field_data_type::Data || info.type == field_data_type::XmlData);
            if (prev_is_length && is_data) {
                mark_pair(prev_tag, info.tag);
            }
            prev_tag = info.tag;
            prev_is_length = (info.type == field_data_type::Length);
        }
    }

    // Secondary: per-container walk (header, trailer, messages) for any pairs
    // only visible in usage context (edge case; retains original coverage).
    auto walk = [&](pugi::xml_node const& container) {
        std::uint16_t prev_tag = 0;
        bool prev_is_length = false;
        for (auto const& c : container.children("field")) {
            auto const cname = std::string{c.attribute("name").as_string("")};
            auto const it = by_name_.find(cname);
            if (it == by_name_.end()) {
                prev_is_length = false;
                continue;
            }
            auto const& info = it->second;
            bool const is_data =
                (info.type == field_data_type::Data || info.type == field_data_type::XmlData);
            if (prev_is_length && is_data) {
                mark_pair(prev_tag, info.tag);
            }
            prev_tag = info.tag;
            prev_is_length = (info.type == field_data_type::Length);
        }
    };
    walk(root.child("header"));
    walk(root.child("trailer"));
    for (auto const& m : root.child("messages").children("message")) {
        walk(m);
    }
}

void LoaderState::parse_document(pugi::xml_document const& doc) {
    auto const root = doc.child("fix");
    if (!root) {
        throw xml_parse_error("dict::xml_parse_error: root <fix> element missing");
    }
    parse_version(root);
    parse_global_fields(root);
    detect_length_pairs(root);
    collect_components(root);
    collect_messages(root);
    header_node_ = root.child("header");
    trailer_node_ = root.child("trailer");
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
std::uint32_t LoaderState::intern_name_in_pool(detail::dict_metadata_handle& h,
                                               std::string_view name) {
    auto const offset = static_cast<std::uint32_t>(h.name_pool_.size());
    h.name_pool_.insert(h.name_pool_.end(), name.begin(), name.end());
    return offset;
}

detail::dict_metadata_handle_ptr LoaderState::finalize() {
    using pmr_alloc = std::pmr::polymorphic_allocator<detail::dict_metadata_handle>;
    auto handle = std::allocate_shared<detail::dict_metadata_handle>(pmr_alloc{mr_}, mr_);

    auto& h = *handle;
    h.version_ = version_;

    // Sort messages bytewise by msg_type — research.md D-6.
    std::ranges::sort(messages_, [](MessageDef const& a, MessageDef const& b) noexcept {
        return detail::bytewise_compare(a.msg_type, b.msg_type) < 0;
    });

    // First pass: estimate name pool size.
    std::size_t pool_estimate = 0;
    for (auto const& md : messages_) {
        // cppcheck-suppress useStlAlgorithm  // two-field accumulation; std::accumulate with binary
        // op + projection is less readable
        pool_estimate += md.msg_type.size() + md.name.size();
    }
    for (auto const& cd : components_) {
        // cppcheck-suppress useStlAlgorithm  // see comment above
        pool_estimate += cd.name.size();
    }
    for (auto const& [name, info] : by_name_) {
        pool_estimate += name.size();
        // T014 (research.md R-4/R-10): the enum store's value+description
        // bytes are interned into this same pool below — must be counted
        // here or the reserve is short on every dictionary. Reserved TIGHT
        // (exact byte count, not a loose upper bound) so a fixed/bounded
        // upstream memory_resource is not over-committed
        // ([[feedback_fixed_arena_over_reserve_silent_loss_larger_stl]]).
        for (auto const& code : info.enum_codes) {
            pool_estimate += code.value.size() + code.description.size();
        }
    }
    h.name_pool_.reserve(pool_estimate + 1);

    // Emit messages and per-message field runs.
    h.messages_.reserve(messages_.size());
    h.per_msg_field_offsets_.reserve(messages_.size());
    h.per_msg_required_offsets_.reserve(messages_.size());

    // Field-name pool entries (deferred to after pool finalization, but slices
    // are computed up-front since they encode offsets, not pointers).
    struct PendingField {
        detail::NameSlice slice{};
        std::uint16_t tag{0};
    };
    std::vector<PendingField> pending_fields;
    pending_fields.reserve(by_name_.size());
    for (auto const& [name, info] : by_name_) {
        detail::NameSlice ns{};
        ns.offset = intern_name_in_pool(h, name);
        ns.length = static_cast<std::uint32_t>(name.size());
        pending_fields.push_back({.slice = ns, .tag = info.tag});
    }

    struct PendingComponent {
        detail::NameSlice slice{};
        std::uint16_t index{0};
    };
    std::vector<PendingComponent> pending_components;
    pending_components.reserve(components_.size());

    // T012 — enum store (research.md R-4): intern each code's value +
    // description via offsets NOW (the pool may still reallocate — offsets
    // are stable across that, pointers are not); the actual EnumValueRef
    // string_views are bound only in the post-shrink_to_fit() pass below,
    // mirroring orchestra_loader.cpp's PendingEnumRun/PendingEnumCode shape.
    struct PendingEnumCodeSlice {
        detail::NameSlice value{};
        detail::NameSlice desc{};
    };
    struct PendingEnumRun {
        std::uint16_t tag{0};
        std::vector<PendingEnumCodeSlice> codes;
    };
    std::vector<PendingEnumRun> pending_enum_runs;
    for (auto const& [name, info] : by_name_) {
        if (info.enum_codes.empty()) {
            continue;
        }
        PendingEnumRun per{};
        per.tag = info.tag;
        per.codes.reserve(info.enum_codes.size());
        for (auto const& code : info.enum_codes) {
            detail::NameSlice vs{};
            vs.offset = intern_name_in_pool(h, code.value);
            vs.length = static_cast<std::uint32_t>(code.value.size());
            detail::NameSlice ds{};
            ds.offset = intern_name_in_pool(h, code.description);
            ds.length = static_cast<std::uint32_t>(code.description.size());
            per.codes.push_back({.value = vs, .desc = ds});
        }
        pending_enum_runs.push_back(std::move(per));
    }

    // Build the flat fields_ vector and the per-message offset table.
    auto append_run = [&](std::vector<FieldRef>& msg_fields,
                          std::vector<std::uint16_t>& msg_required)
        -> std::pair<detail::MsgFieldsRun, detail::MsgFieldsRun> {
        // Stable sort by tag for per-MsgType binary search (AC-D1 / AC-D5 D-6).
        // QuickFIX XML can have the same tag appear from both <header>/<trailer>
        // and <messages>; dedupe by tag (first-seen wins).
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
        // (no_tag,tag) pairs, accumulated across header+body+trailer exactly
        // like msg_fields/msg_required above.
        std::vector<std::pair<std::uint16_t, std::uint16_t>> msg_group_required;
        // Header fields first, then message-specific, then trailer.
        if (header_node_ != nullptr) {
            expand_field_list(header_node_, msg_fields, msg_required, msg_group_required, 0, 0);
        }
        expand_field_list(md.node, msg_fields, msg_required, msg_group_required, 0, 0);
        if (trailer_node_ != nullptr) {
            expand_field_list(trailer_node_, msg_fields, msg_required, msg_group_required, 0, 0);
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
    }

    // Emit components (PMR ComponentRef array).
    // For each component, walk its fields via expand_field_list to populate
    // the per-component flat field table (component_fields_), recording
    // first_field_index and field_count on each ComponentRef per [2c §4.2] /
    // data-model.md Entity 2.
    h.components_.reserve(components_.size());
    for (std::size_t i = 0; i < components_.size(); ++i) {
        auto const& def = components_[i];
        detail::NameSlice ns{};
        ns.offset = intern_name_in_pool(h, def.name);
        ns.length = static_cast<std::uint32_t>(def.name.size());
        pending_components.push_back({.slice = ns, .index = static_cast<std::uint16_t>(i)});

        // Expand the component's field list into a scratch vector, then
        // append to the flat component_fields_ table.
        std::vector<FieldRef> comp_fields;
        std::vector<std::uint16_t> comp_required;  // discard — component required-sets unused
        std::vector<std::pair<std::uint16_t, std::uint16_t>> comp_group_required;  // discard —
        // any group reached here is ALSO reached (and correctly captured) via
        // its own standalone GroupDef walk below.
        expand_field_list(def.node, comp_fields, comp_required, comp_group_required,
                          /*enclosing_group_no_tag=*/0,
                          /*enclosing_component_index=*/static_cast<std::uint16_t>(i + 1));

        auto const first_idx = static_cast<std::uint16_t>(h.component_fields_.size());
        auto const cnt =
            static_cast<std::uint16_t>(std::min<std::size_t>(comp_fields.size(), 65535U));
        h.component_fields_.insert(h.component_fields_.end(), comp_fields.begin(),
                                   comp_fields.end());

        // Derive parent_component_id: look up the pre-built reverse map from
        // collect_components(). 0 = top-level (no enclosing component); otherwise
        // the enclosing component's 1-based id per [2c §4.2] / data-model.md
        // Entity 2 / contracts/component_ref.hpp:27–29.
        std::uint16_t parent_comp_id = 0;
        if (auto const pit = parent_of_.find(def.name); pit != parent_of_.end()) {
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

    // Load-time delimiter guard (072-nested-group-hardening FR-003 / L-063-4):
    // reject a dialect in which any nested group's delimiter (`first_field_tag`)
    // equals its immediate parent group's delimiter — a layout the flat instance
    // splitter mis-splits, silently corrupting the outer group's boundaries.
    // Runs here, BEFORE `groups_` is sorted by no_tag (which would stale
    // `group_index_by_no_tag_`, whose indices are pre-sort), and before any
    // `table_view` is built (that happens later in `as_table_view()`, which stays
    // allocation-only / non-throwing). The whole load fails on a collision. This
    // is the guard's first-seen `groups_` seam (one GroupDef per no_tag); the
    // strictly-stronger all-contexts census lives in the test suite (FR-001).
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

    // Emit groups sorted by no_tag.
    // For each group, walk its fields via expand_field_list to populate the
    // per-group flat field table (group_fields_), recording first_field_index
    // and field_count on each GroupRef per [2c §4.2] / data-model.md Entity 3.
    std::ranges::sort(
        groups_, [](GroupDef const& a, GroupDef const& b) noexcept { return a.no_tag < b.no_tag; });
    h.groups_.reserve(groups_.size());
    h.group_required_offsets_.reserve(groups_.size());
    for (auto const& g : groups_) {
        std::uint16_t first_idx = 0;
        std::uint16_t cnt = 0;
        // Gate B r1 F1 (fixpp#201): `grp_required` is the GROUP-RELATIVE direct
        // required-member set for THIS group — this standalone call starts
        // fresh at the group's own boundary, so `!in_group && component_
        // required`-gated pushes to `required_out` are exactly "own_req AND
        // component-AND since this group's boundary" for direct members
        // (nested-group descendants are excluded by the `!in_group` gate once
        // a deeper <group> is entered). No separate `group_scope_component_
        // required` accumulator is needed for THIS call — see Gate B r1
        // triage F1 mechanism note. 081-strict-validation-residuals D-3
        // (Concern B, E-3's "bare" backing — symmetric with the context-store
        // fix above): `component_required` now SEEDS from THIS group's own
        // first-seen `required=` (`g_own_required`) instead of the unconditional
        // `true` default — a bare-store fallback for an optional group's node
        // must report empty (no direct member is group-required), matching
        // the immediate-enclosing-group-gating rule the context store now
        // enforces (Contract 1a leg 2 — the bare value must remain a valid
        // observed per-context variant).
        bool const g_own_required =
            std::string_view{g.node.attribute("required").as_string("N")} == "Y";
        std::vector<FieldRef> grp_fields;
        std::vector<std::uint16_t> grp_required;
        if (g.node) {
            // Expand the group's inner fields into the flat group_fields_ table.
            std::vector<std::pair<std::uint16_t, std::uint16_t>> grp_group_required;  // discard
            expand_field_list(g.node, grp_fields, grp_required, grp_group_required,
                              /*enclosing_group_no_tag=*/g.no_tag,
                              /*enclosing_component_index=*/0,
                              /*in_group=*/false,
                              /*component_required=*/g_own_required);
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

    // Pool is now finalized — but we may have grown it past pool_estimate.
    // shrink_to_fit to lock the data pointer.
    h.name_pool_.shrink_to_fit();

    // Bind string_views in MessageEntry now that name_pool_.data() is stable.
    auto* data = h.name_pool_.data();
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

    // T012 — enum store (mirrors orchestra_loader.cpp's 074 shape). MUST run
    // here, AFTER shrink_to_fit() has locked name_pool_.data() (research.md
    // R-4) — binding these string_views during the parse would dangle on the
    // next pool reallocation. `pending_enum_runs` was built while iterating
    // `by_name_` (an unordered_map — NOT tag-ordered), so the explicit sort
    // below is required for `enum_values_impl`'s binary-search precondition
    // (unlike OrchestraLoader, which iterates a pre-sorted tag list).
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
// becomes `dict::xml_oom_error` (AC-L9 / research.md D-3).
// ----------------------------------------------------------------------------

[[nodiscard]] detail::dict_metadata_handle_ptr build_handle_from_doc(
    pugi::xml_document const& doc, std::pmr::memory_resource* mr) {
    LoaderState st{mr};
    st.parse_document(doc);
    return st.finalize();
}

}  // namespace

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
Dictionary XmlLoader::load(std::filesystem::path const& xml_path, std::pmr::memory_resource* mr) {
    assert(mr != nullptr && "XmlLoader::load: mr must not be null");
    return fixpp::core::detail::trap_throw_or_throw<xml_oom_error>([&] {
        std::ifstream in(xml_path, std::ios::binary);
        if (!in) {
            throw xml_parse_error("dict::xml_parse_error: cannot open " + xml_path.string());
        }
        pugi::xml_document doc;
        auto const result = doc.load(in);
        if (!result) {
            throw xml_parse_error(std::string{"dict::xml_parse_error: "} + result.description());
        }
        return Dictionary{build_handle_from_doc(doc, mr)};
    });
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
Dictionary XmlLoader::load_from_string(std::string_view xml_text, std::pmr::memory_resource* mr) {
    assert(mr != nullptr && "XmlLoader::load_from_string: mr must not be null");
    return fixpp::core::detail::trap_throw_or_throw<xml_oom_error>([&] {
        pugi::xml_document doc;
        auto const result = doc.load_buffer(xml_text.data(), xml_text.size());
        if (!result) {
            throw xml_parse_error(std::string{"dict::xml_parse_error: "} + result.description());
        }
        return Dictionary{build_handle_from_doc(doc, mr)};
    });
}

}  // namespace fixpp::dict
