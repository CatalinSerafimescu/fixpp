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

#include "dictionary_internal.hpp"

#include <fixpp/core/decimal_helpers.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/error.hpp>
#include <fixpp/dict/xml_loader.hpp>

#include <pugixml.hpp>

#include <algorithm>
#include <cassert>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <memory>
#include <memory_resource>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fixpp::dict {

namespace {

// ----------------------------------------------------------------------------
// Field-type vocabulary per `[FIX50SP2 §3.3]` (research.md D-14 enum naming).
// ----------------------------------------------------------------------------

struct FieldTypeEntry {
    std::string_view xml_name;
    field_data_type  enum_value;
};

constexpr FieldTypeEntry kFieldTypeTable[] = {
    {"INT", field_data_type::Int},
    {"LENGTH", field_data_type::Length},
    {"SEQNUM", field_data_type::SeqNum},
    {"NUMINGROUP", field_data_type::NumInGroup},
    {"DAYOFMONTH", field_data_type::DayOfMonth},
    {"PRICE", field_data_type::Price},
    {"QTY", field_data_type::Qty},
    {"AMT", field_data_type::Amt},
    {"PRICEOFFSET", field_data_type::PriceOffset},
    {"PERCENTAGE", field_data_type::Percentage},
    {"FLOAT", field_data_type::Float},
    {"CHAR", field_data_type::Char},
    {"BOOLEAN", field_data_type::Boolean},
    {"STRING", field_data_type::String},
    {"MULTIPLECHARVALUE", field_data_type::MultiCharValue},
    {"MULTIPLEVALUESTRING", field_data_type::MultiStringValue},
    {"MULTIPLESTRINGVALUE", field_data_type::MultiStringValue},
    {"CURRENCY", field_data_type::Currency},
    {"EXCHANGE", field_data_type::Exchange},
    {"COUNTRY", field_data_type::Country},
    {"MONTHYEAR", field_data_type::MonthYear},
    {"UTCTIMESTAMP", field_data_type::UtcTimestamp},
    {"UTCTIMEONLY", field_data_type::UtcTimeOnly},
    {"UTCDATEONLY", field_data_type::UtcDateOnly},
    {"UTCDATE", field_data_type::UtcDateOnly},
    {"LOCALMKTDATE", field_data_type::LocalMktDate},
    {"TZTIMEONLY", field_data_type::TzTimeOnly},
    {"TZTIMESTAMP", field_data_type::TzTimestamp},
    {"LANGUAGE", field_data_type::Language},
    {"DATA", field_data_type::Data},
    {"XMLDATA", field_data_type::XmlData},
    // FIX 5.0 / 5.0SP2 additions mapped to nearest representable type
    // (data-model.md Entity 1 keeps the enum locked to [FIX50SP2 §3.3]
    // canonical names; runtime-loaded XML that uses post-canonical names is
    // accepted via this collapse table — research.md D-14 typing carve-out).
    {"TAGNUM", field_data_type::Int},
    {"LOCALMKTTIME", field_data_type::LocalMktDate},
    {"XID", field_data_type::String},
    {"XIDREF", field_data_type::String},
};

[[nodiscard]] bool resolve_field_type(std::string_view name,
                                      field_data_type& out) noexcept {
    for (auto const& row : kFieldTypeTable) {
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
    {"FIX",  4, 0, 0, session_version::v40},
    {"FIX",  4, 1, 0, session_version::v41},
    {"FIX",  4, 2, 0, session_version::v42},
    {"FIX",  4, 3, 0, session_version::v43},
    {"FIX",  4, 4, 0, session_version::v44},
    {"FIX",  5, 0, 0, session_version::v50},
    {"FIX",  5, 0, 1, session_version::v50sp1},
    {"FIX",  5, 0, 2, session_version::v50sp2},
    {"FIXT", 1, 1, 0, session_version::vt11},
};

[[nodiscard]] session_version resolve_version(std::string_view type_attr,
                                              int major,
                                              int minor,
                                              int servicepack) noexcept {
    for (auto const& row : kVersionTable) {
        if (row.type_attr == type_attr && row.major == major &&
            row.minor == minor && row.servicepack == servicepack) {
            return row.value;
        }
    }
    return session_version::Unknown;
}

// ----------------------------------------------------------------------------
// Bytewise (locale-independent) `unsigned char` comparator over `string_view`
// per research.md D-6.
// ----------------------------------------------------------------------------

[[nodiscard]] inline int bytewise_compare(std::string_view a,
                                          std::string_view b) noexcept {
    auto const n = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < n; ++i) {
        auto const lhs = static_cast<unsigned char>(a[i]);
        auto const rhs = static_cast<unsigned char>(b[i]);
        if (lhs != rhs) {
            return lhs < rhs ? -1 : 1;
        }
    }
    if (a.size() == b.size()) {
        return 0;
    }
    return a.size() < b.size() ? -1 : 1;
}

// ----------------------------------------------------------------------------
// Build-time scaffolding (NOT PMR — uses default allocators because pugixml
// itself goes through malloc per research.md D-1. We free this before the
// loader returns; the `Dictionary`'s metadata block is the only thing the
// caller observes, and that block lives on `mr`).
// ----------------------------------------------------------------------------

struct GlobalFieldInfo {
    std::uint16_t tag{0};
    std::string name;
    field_data_type type{};
    std::uint16_t length_pair_data_tag{0};  // populated post-parse
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
    [[nodiscard]] std::uint32_t intern_name_in_pool(
        detail::dict_metadata_handle& h, std::string_view name);

    void parse_version(pugi::xml_node const& root);
    void parse_global_fields(pugi::xml_node const& root);
    void collect_components(pugi::xml_node const& root);
    void collect_messages(pugi::xml_node const& root);

    // Expand a list of <field>/<component>/<group> child rows into a flat
    // FieldRef vector. Components are resolved recursively. Groups emit a
    // GroupRef AND their inner fields (the group's first field is the first
    // field declared inside it, per FIX semantics).
    void expand_field_list(pugi::xml_node const& parent,
                           std::vector<FieldRef>& out,
                           std::vector<std::uint16_t>& required_out,
                           std::uint16_t enclosing_group_no_tag,
                           std::uint16_t enclosing_component_index);

    void detect_length_pairs(pugi::xml_node const& root);

    std::pmr::memory_resource* mr_;

    pugi::xml_node header_node_;
    pugi::xml_node trailer_node_;

    std::unordered_map<std::string, GlobalFieldInfo> by_name_;
    std::unordered_map<std::uint16_t, std::string> by_tag_;
    std::vector<ComponentDef> components_;
    std::unordered_map<std::string, std::uint16_t> component_index_by_name_;
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
        throw xml_parse_error(
            "dict::xml_parse_error: <fix> missing major/minor attributes");
    }
    int major = 0;
    int minor = 0;
    int sp = 0;
    {
        std::string const s{major_attr.as_string("")};
        auto const r = std::from_chars(s.data(), s.data() + s.size(), major);
        if (r.ec != std::errc{} || r.ptr != s.data() + s.size() || major < 0) {
            throw unknown_version_error(
                "dict::unknown_version_error: non-numeric or negative major");
        }
    }
    {
        std::string const s{minor_attr.as_string("")};
        auto const r = std::from_chars(s.data(), s.data() + s.size(), minor);
        if (r.ec != std::errc{} || r.ptr != s.data() + s.size() || minor < 0) {
            throw unknown_version_error(
                "dict::unknown_version_error: non-numeric or negative minor");
        }
    }
    if (sp_attr) {
        std::string const s{sp_attr.as_string("")};
        if (!s.empty()) {
            auto const r = std::from_chars(s.data(), s.data() + s.size(), sp);
            if (r.ec != std::errc{} || r.ptr != s.data() + s.size() || sp < 0) {
                throw unknown_version_error(
                    "dict::unknown_version_error: non-numeric servicepack");
            }
        }
    }
    auto const resolved = resolve_version(type_attr,
                                          static_cast<std::uint8_t>(major),
                                          static_cast<std::uint8_t>(minor),
                                          static_cast<std::uint8_t>(sp));
    if (resolved == session_version::Unknown) {
        std::ostringstream oss;
        oss << "dict::unknown_version_error: <fix type=\"" << type_attr
            << "\" major=\"" << major << "\" minor=\"" << minor
            << "\" servicepack=\"" << sp << "\"> not in v1.0 supported nine";
        throw unknown_version_error(oss.str());
    }
    version_ = resolved;
}

void LoaderState::parse_global_fields(pugi::xml_node const& root) {
    auto const fields = root.child("fields");
    if (!fields) {
        throw xml_parse_error(
            "dict::xml_parse_error: missing <fields> block under <fix>");
    }
    for (auto const& f : fields.children("field")) {
        auto const number_attr = f.attribute("number");
        if (!number_attr) {
            throw xml_parse_error(
                "dict::xml_parse_error: <field> missing number attribute");
        }
        std::string const num_s{number_attr.as_string("")};
        int tag_i = 0;
        auto const r = std::from_chars(num_s.data(), num_s.data() + num_s.size(),
                                       tag_i);
        if (r.ec != std::errc{} || r.ptr != num_s.data() + num_s.size() ||
            tag_i < 0 || tag_i > 65535) {
            throw xml_parse_error(
                "dict::xml_parse_error: <field number=\"" + num_s +
                "\"> non-numeric or out-of-range");
        }
        auto const tag = static_cast<std::uint16_t>(tag_i);
        auto const name = std::string{f.attribute("name").as_string("")};
        auto const type_s = std::string{f.attribute("type").as_string("")};
        if (name.empty()) {
            throw xml_parse_error(
                "dict::xml_parse_error: <field number=\"" + num_s +
                "\"> missing name attribute");
        }
        field_data_type ft{};
        if (!resolve_field_type(type_s, ft)) {
            throw xml_parse_error(
                "dict::xml_parse_error: <field type=\"" + type_s +
                "\"> outside [FIX50SP2 §3.3] vocabulary");
        }
        if (by_tag_.find(tag) != by_tag_.end()) {
            throw xml_parse_error(
                "dict::xml_parse_error: duplicate <field number=\"" + num_s +
                "\">");
        }
        GlobalFieldInfo info{tag, name, ft, 0};
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
            throw xml_parse_error(
                "dict::xml_parse_error: <component> missing name attribute");
        }
        if (component_index_by_name_.find(name) != component_index_by_name_.end()) {
            throw xml_parse_error(
                "dict::xml_parse_error: duplicate <component name=\"" + name +
                "\">");
        }
        components_.push_back({name, c});
        component_index_by_name_.emplace(name, next_id);
        ++next_id;
    }
}

void LoaderState::collect_messages(pugi::xml_node const& root) {
    auto const msgs = root.child("messages");
    if (!msgs) {
        throw xml_parse_error(
            "dict::xml_parse_error: missing <messages> block under <fix>");
    }
    for (auto const& m : msgs.children("message")) {
        auto const msg_type = std::string{m.attribute("msgtype").as_string("")};
        auto const name = std::string{m.attribute("name").as_string("")};
        if (msg_type.empty()) {
            throw xml_parse_error(
                "dict::xml_parse_error: <message> missing msgtype attribute");
        }
        if (name.empty()) {
            throw xml_parse_error(
                "dict::xml_parse_error: <message> missing name attribute");
        }
        messages_.push_back({msg_type, name, m});
    }
}

void LoaderState::expand_field_list(pugi::xml_node const& parent,
                                    std::vector<FieldRef>& out,
                                    std::vector<std::uint16_t>& required_out,
                                    std::uint16_t enclosing_group_no_tag,
                                    std::uint16_t enclosing_component_index) {
    for (auto const& child : parent.children()) {
        std::string_view const tag_name{child.name()};
        if (tag_name == "field") {
            auto const fname = std::string{child.attribute("name").as_string("")};
            if (fname.empty()) {
                throw xml_parse_error(
                    "dict::xml_parse_error: <field> reference missing name");
            }
            auto const it = by_name_.find(fname);
            if (it == by_name_.end()) {
                throw xml_parse_error(
                    "dict::xml_parse_error: <field name=\"" + fname +
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
            if (req) {
                required_out.push_back(info.tag);
            }
        } else if (tag_name == "component") {
            auto const cname = std::string{child.attribute("name").as_string("")};
            auto const cit = component_index_by_name_.find(cname);
            if (cit == component_index_by_name_.end()) {
                throw xml_parse_error(
                    "dict::xml_parse_error: <component name=\"" + cname +
                    "\"> not defined in <components> block");
            }
            auto const& def = components_[cit->second];
            expand_field_list(def.node, out, required_out,
                              enclosing_group_no_tag,
                              static_cast<std::uint16_t>(cit->second + 1));
        } else if (tag_name == "group") {
            auto const gname = std::string{child.attribute("name").as_string("")};
            auto const nit = by_name_.find(gname);
            if (nit == by_name_.end()) {
                throw xml_parse_error(
                    "dict::xml_parse_error: <group name=\"" + gname +
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
            if (greq) {
                required_out.push_back(no_tag);
            }
            // Record the GroupRef (deduplicated by no_tag — first-seen wins).
            if (group_index_by_no_tag_.find(no_tag) == group_index_by_no_tag_.end()) {
                std::uint16_t first_field_tag = 0;
                for (auto const& gc : child.children()) {
                    std::string_view const tn{gc.name()};
                    if (tn == "field") {
                        auto const cn =
                            std::string{gc.attribute("name").as_string("")};
                        auto const fi = by_name_.find(cn);
                        if (fi != by_name_.end()) {
                            first_field_tag = fi->second.tag;
                            break;
                        }
                    } else if (tn == "group") {
                        auto const cn =
                            std::string{gc.attribute("name").as_string("")};
                        auto const fi = by_name_.find(cn);
                        if (fi != by_name_.end()) {
                            first_field_tag = fi->second.tag;
                            break;
                        }
                    } else if (tn == "component") {
                        auto const cn =
                            std::string{gc.attribute("name").as_string("")};
                        auto const ci = component_index_by_name_.find(cn);
                        if (ci != component_index_by_name_.end()) {
                            // Use first <field> of the resolved component.
                            for (auto const& cf : components_[ci->second].node.children()) {
                                std::string_view const cfn{cf.name()};
                                if (cfn == "field") {
                                    auto const fn = std::string{
                                        cf.attribute("name").as_string("")};
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
                auto const idx = static_cast<std::uint16_t>(groups_.size());
                group_index_by_no_tag_.emplace(no_tag, idx);
                groups_.push_back(gd);
            }
            // Recurse into group children with the group-context set.
            expand_field_list(child, out, required_out, no_tag,
                              enclosing_component_index);
        }
        // Ignore unknown child elements (forwards-compat).
    }
}

void LoaderState::detect_length_pairs(pugi::xml_node const& root) {
    // Walk every message and trailer/header; whenever a LENGTH-typed field is
    // immediately followed by a DATA-typed field, mark the LENGTH's
    // length_pair_data_tag.
    auto walk = [&](pugi::xml_node const& container) {
        std::string_view prev_name;
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
            if (prev_is_length && info.type == field_data_type::Data) {
                auto const lit = by_tag_.find(prev_tag);
                if (lit != by_tag_.end()) {
                    auto const nit = by_name_.find(lit->second);
                    if (nit != by_name_.end()) {
                        nit->second.length_pair_data_tag = info.tag;
                    }
                }
            }
            prev_name = cname;
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
        throw xml_parse_error(
            "dict::xml_parse_error: root <fix> element missing");
    }
    parse_version(root);
    parse_global_fields(root);
    detect_length_pairs(root);
    collect_components(root);
    collect_messages(root);
    header_node_ = root.child("header");
    trailer_node_ = root.child("trailer");
}

std::uint32_t LoaderState::intern_name_in_pool(detail::dict_metadata_handle& h,
                                               std::string_view name) {
    auto const offset = static_cast<std::uint32_t>(h.name_pool_.size());
    h.name_pool_.insert(h.name_pool_.end(), name.begin(), name.end());
    return offset;
}

detail::dict_metadata_handle_ptr LoaderState::finalize() {
    using pmr_alloc = std::pmr::polymorphic_allocator<detail::dict_metadata_handle>;
    auto handle = std::allocate_shared<detail::dict_metadata_handle>(
        pmr_alloc{mr_}, mr_);

    auto& h = *handle;
    h.version_ = version_;

    // Sort messages bytewise by msg_type — research.md D-6.
    std::ranges::sort(messages_, [](MessageDef const& a, MessageDef const& b) noexcept {
        return bytewise_compare(a.msg_type, b.msg_type) < 0;
    });

    // First pass: estimate name pool size.
    std::size_t pool_estimate = 0;
    for (auto const& md : messages_) {
        pool_estimate += md.msg_type.size() + md.name.size();
    }
    for (auto const& cd : components_) {
        pool_estimate += cd.name.size();
    }
    for (auto const& [name, info] : by_name_) {
        pool_estimate += name.size();
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
        pending_fields.push_back({ns, info.tag});
    }

    struct PendingComponent {
        detail::NameSlice slice{};
        std::uint16_t index{0};
    };
    std::vector<PendingComponent> pending_components;
    pending_components.reserve(components_.size());

    // Build the flat fields_ vector and the per-message offset table.
    auto append_run = [&](std::vector<FieldRef>& msg_fields,
                          std::vector<std::uint16_t>& msg_required)
        -> std::pair<detail::MsgFieldsRun, detail::MsgFieldsRun> {
        // Stable sort by tag for per-MsgType binary search (AC-D1 / AC-D5 D-6).
        // QuickFIX XML can have the same tag appear from both <header>/<trailer>
        // and <messages>; dedupe by tag (first-seen wins).
        std::ranges::sort(msg_fields,
                          [](FieldRef const& a, FieldRef const& b) noexcept {
                              return a.tag < b.tag;
                          });
        auto const last = std::ranges::unique(
            msg_fields,
            [](FieldRef const& a, FieldRef const& b) noexcept {
                return a.tag == b.tag;
            }).begin();
        msg_fields.erase(last, msg_fields.end());

        std::ranges::sort(msg_required);
        auto const rlast = std::ranges::unique(msg_required).begin();
        msg_required.erase(rlast, msg_required.end());

        detail::MsgFieldsRun field_run{
            static_cast<std::uint32_t>(h.fields_.size()),
            static_cast<std::uint32_t>(msg_fields.size())};
        h.fields_.insert(h.fields_.end(), msg_fields.begin(), msg_fields.end());

        detail::MsgFieldsRun req_run{
            static_cast<std::uint32_t>(h.required_fields_pool_.size()),
            static_cast<std::uint32_t>(msg_required.size())};
        h.required_fields_pool_.insert(h.required_fields_pool_.end(),
                                       msg_required.begin(),
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
        // Header fields first, then message-specific, then trailer.
        if (header_node_) {
            expand_field_list(header_node_, msg_fields, msg_required, 0, 0);
        }
        expand_field_list(md.node, msg_fields, msg_required, 0, 0);
        if (trailer_node_) {
            expand_field_list(trailer_node_, msg_fields, msg_required, 0, 0);
        }
        auto const [field_run, req_run] = append_run(msg_fields, msg_required);
        h.per_msg_field_offsets_.push_back(field_run);
        h.per_msg_required_offsets_.push_back(req_run);
    }

    // Emit components (PMR ComponentRef array).
    h.components_.reserve(components_.size());
    for (std::size_t i = 0; i < components_.size(); ++i) {
        auto const& def = components_[i];
        detail::NameSlice ns{};
        ns.offset = intern_name_in_pool(h, def.name);
        ns.length = static_cast<std::uint32_t>(def.name.size());
        pending_components.push_back({ns, static_cast<std::uint16_t>(i)});
        ComponentRef cr{};
        cr.component_id = static_cast<std::uint16_t>(i);
        cr.name_offset = static_cast<std::uint16_t>(
            std::min<std::uint32_t>(ns.offset, 65535));
        cr.first_field_index = 0;
        cr.field_count = 0;
        cr.parent_component_id = 0;
        h.components_.push_back(cr);
    }

    // Emit groups sorted by no_tag.
    std::ranges::sort(groups_, [](GroupDef const& a, GroupDef const& b) noexcept {
        return a.no_tag < b.no_tag;
    });
    h.groups_.reserve(groups_.size());
    for (auto const& g : groups_) {
        GroupRef gr{};
        gr.no_tag = g.no_tag;
        gr.first_field_tag = g.first_field_tag;
        gr.first_field_index = 0;
        gr.field_count = 0;
        gr.parent_group_no_tag = g.parent_group_no_tag;
        h.groups_.push_back(gr);
    }

    // Pool is now finalized — but we may have grown it past pool_estimate.
    // shrink_to_fit to lock the data pointer.
    h.name_pool_.shrink_to_fit();

    // Bind string_views in MessageEntry now that name_pool_.data() is stable.
    auto data = h.name_pool_.data();
    for (std::size_t i = 0; i < messages_.size(); ++i) {
        MessageEntry me{};
        me.msg_type = std::string_view{data + msg_msg_type_slices[i].offset,
                                       msg_msg_type_slices[i].length};
        me.name = std::string_view{data + msg_name_slices[i].offset,
                                   msg_name_slices[i].length};
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
                      [&](detail::NamedIndex const& a,
                          detail::NamedIndex const& b) noexcept {
                          return bytewise_compare(h.name_at(a.name),
                                                  h.name_at(b.name)) < 0;
                      });

    // Build field-name index, sorted bytewise.
    h.field_by_name_.reserve(pending_fields.size());
    for (auto const& pf : pending_fields) {
        detail::FieldNameEntry e{};
        e.name = pf.slice;
        e.tag = pf.tag;
        h.field_by_name_.push_back(e);
    }
    std::ranges::sort(h.field_by_name_,
                      [&](detail::FieldNameEntry const& a,
                          detail::FieldNameEntry const& b) noexcept {
                          return bytewise_compare(h.name_at(a.name),
                                                  h.name_at(b.name)) < 0;
                      });

    return handle;
}

// ----------------------------------------------------------------------------
// Top-level load implementation factored so both `load` and `load_from_string`
// share the post-parse path. Wraps the body in
// `core::detail::trap_throw_or_throw<xml_oom_error>` so PMR `bad_alloc`
// becomes `dict::xml_oom_error` (AC-L9 / research.md D-3).
// ----------------------------------------------------------------------------

[[nodiscard]] detail::dict_metadata_handle_ptr build_handle_from_doc(
    pugi::xml_document& doc, std::pmr::memory_resource* mr) {
    LoaderState st{mr};
    st.parse_document(doc);
    return st.finalize();
}

}  // namespace

Dictionary XmlLoader::load(std::filesystem::path const& xml_path,
                           std::pmr::memory_resource* mr) {
    assert(mr != nullptr && "XmlLoader::load: mr must not be null");
    return fixpp::core::detail::trap_throw_or_throw<xml_oom_error>([&] {
        std::ifstream in(xml_path, std::ios::binary);
        if (!in) {
            throw xml_parse_error(
                "dict::xml_parse_error: cannot open " + xml_path.string());
        }
        pugi::xml_document doc;
        auto const result = doc.load(in);
        if (!result) {
            throw xml_parse_error(
                std::string{"dict::xml_parse_error: "} + result.description());
        }
        return Dictionary{build_handle_from_doc(doc, mr)};
    });
}

Dictionary XmlLoader::load_from_string(std::string_view xml_text,
                                       std::pmr::memory_resource* mr) {
    assert(mr != nullptr && "XmlLoader::load_from_string: mr must not be null");
    return fixpp::core::detail::trap_throw_or_throw<xml_oom_error>([&] {
        pugi::xml_document doc;
        auto const result = doc.load_buffer(xml_text.data(), xml_text.size());
        if (!result) {
            throw xml_parse_error(
                std::string{"dict::xml_parse_error: "} + result.description());
        }
        return Dictionary{build_handle_from_doc(doc, mr)};
    });
}

}  // namespace fixpp::dict
