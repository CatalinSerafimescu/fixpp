// SPDX-License-Identifier: AGPL-3.0-or-later
// src/dictionary/overlay_merge.cpp
//
// 098-dialect-overlay: each format's overlay guard stage, splice and Length+Data
// relation check (research R-3, R-4, R-5). See overlay_merge.hpp for the order of
// the steps; xml_loader.cpp and orchestra_loader.cpp call them. The QuickFIX-XML
// merge comes first; the Orchestra merge, at the end of the file, has its own
// header.
//
// The classifier. One pass over the overlay matches each overlay element to the
// base definition it names (research R-3's QuickFIX table) or marks it appended;
// non-element overlay nodes are dropped. Steps 5, 6, 7 and 8 all read that one
// classification, so they cannot disagree on what the splice will do.
//
// Step 6 walks a virtual merged document: a base node's children are its own
// children, as `load`'s flags parsed them, followed by the element children the
// classifier appends to it, in overlay order; an appended overlay node's children
// are its element children. That is the shape `splice` produces, so step 6 counts
// what `LoaderState::expand_field_list` will visit in the spliced document. Its
// model of `LoaderState::finalize` (re-derive it by reading `finalize`): messages
// are expanded sorted bytewise by msgtype, each as header, body, trailer; then
// every component in declaration order; then the first-seen group of each
// NumInGroup tag, registered when the expansion first meets a `<group>` of that
// tag. A step is one child node visited; a component reference or a group adds the
// steps of its target. Under either `unresolved_group_policy` a group is registered
// before its members are expanded, so the counts do not depend on the policy; the
// policy changes only whether `finalize` throws, which ends the load.
//
// Memory and work of step 7 (research R-5): a level set is a sorted vector of the
// tags a definition emits at its own level, each with an occurrence count capped
// at two. One is built per definition the check reaches, by merging the sets of
// the components it inlines, so each holds at most the document's declared field
// count. The bound is therefore (the base's reference count plus the overlay's) ×
// the merged document's declared field count (the base's fields plus the
// overlay's new fields).

#include "overlay_merge.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fixpp/dict/error.hpp>
#include <fstream>
#include <ios>
#include <limits>
#include <map>
#include <new>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "dictionary_internal.hpp"

namespace fixpp::dict::detail::overlay {

namespace {

using node_ptr = pugi::xml_node_struct*;

[[nodiscard]] node_ptr key_of(pugi::xml_node n) noexcept { return n.internal_object(); }

[[nodiscard]] std::string_view name_of(pugi::xml_node n) noexcept { return n.name(); }

[[nodiscard]] std::string attr_of(pugi::xml_node n, char const* a) {
    return std::string{n.attribute(a).as_string("")};
}

[[nodiscard]] std::uint64_t sat_add(std::uint64_t a, std::uint64_t b) noexcept {
    return a > std::numeric_limits<std::uint64_t>::max() - b
               ? std::numeric_limits<std::uint64_t>::max()
               : a + b;
}

[[nodiscard]] std::uint64_t sat_sub(std::uint64_t a, std::uint64_t b) noexcept {
    return a > b ? a - b : 0;
}

[[noreturn]] void conflict(std::string const& what) {
    throw overlay_conflict_error("dict::overlay_conflict_error: " + what);
}

[[noreturn]] void limit(std::string const& what) {
    throw overlay_limit_error("dict::overlay_limit_error: " + what);
}

[[nodiscard]] bool is_ref_kind(std::string_view k) noexcept {
    return k == "field" || k == "component" || k == "group";
}

// ── The match index (research R-3): the one helper every match index is built by.
// It records every occurrence of a key, so a duplicated base key is admitted here
// and refused only when an overlay element looks it up.
using match_index = std::unordered_map<std::string, std::vector<pugi::xml_node>>;

template <class KeyFn>
[[nodiscard]] match_index build_index(pugi::xml_node parent, KeyFn key) {
    match_index idx;
    for (pugi::xml_node c = parent.first_child(); c; c = c.next_sibling()) {
        if (c.type() != pugi::node_element) {
            continue;
        }
        if (std::optional<std::string> k = key(c); k.has_value()) {
            idx[*k].push_back(c);
        }
    }
    return idx;
}

[[nodiscard]] std::optional<std::string> message_key(pugi::xml_node c) {
    if (name_of(c) != "message") {
        return std::nullopt;
    }
    return attr_of(c, "msgtype");
}

[[nodiscard]] std::optional<std::string> component_def_key(pugi::xml_node c) {
    if (name_of(c) != "component") {
        return std::nullopt;
    }
    return attr_of(c, "name");
}

[[nodiscard]] std::optional<std::string> field_number_key(pugi::xml_node c) {
    if (name_of(c) != "field") {
        return std::nullopt;
    }
    auto const tag = parse_field_number(c.attribute("number").as_string(""));
    if (!tag) {
        return std::nullopt;
    }
    return std::to_string(*tag);
}

[[nodiscard]] std::optional<std::string> field_name_key(pugi::xml_node c) {
    if (name_of(c) != "field") {
        return std::nullopt;
    }
    return attr_of(c, "name");
}

[[nodiscard]] std::optional<std::string> value_key(pugi::xml_node c) {
    if (name_of(c) != "value") {
        return std::nullopt;
    }
    return attr_of(c, "enum");
}

[[nodiscard]] std::optional<std::string> child_key(pugi::xml_node c) {
    if (!is_ref_kind(name_of(c))) {
        return std::nullopt;
    }
    return std::string{name_of(c)} + "|" + attr_of(c, "name");
}

// "<message msgtype="D">", "<field name="X">" and the like, for error messages.
[[nodiscard]] std::string describe(pugi::xml_node n) {
    std::string const kind{name_of(n)};
    if (kind == "message") {
        return "<message msgtype=\"" + attr_of(n, "msgtype") + "\">";
    }
    if (kind == "field" && n.attribute("number")) {
        return "<field number=\"" + attr_of(n, "number") + "\" name=\"" + attr_of(n, "name") +
               "\">";
    }
    if (kind == "value") {
        return "<value enum=\"" + attr_of(n, "enum") + "\">";
    }
    if (n.attribute("name")) {
        return "<" + kind + " name=\"" + attr_of(n, "name") + "\">";
    }
    return "<" + kind + ">";
}

// The 1-based position of `n` among its parent's element children of its name.
[[nodiscard]] std::size_t ordinal_of(pugi::xml_node n) {
    std::size_t i = 1;
    for (pugi::xml_node s = n.previous_sibling(n.name()); s; s = s.previous_sibling(n.name())) {
        ++i;
    }
    return i;
}

[[nodiscard]] std::string where_of(pugi::xml_node parent) {
    return name_of(parent) == "fix" ? std::string{"<fix>"} : describe(parent);
}

[[nodiscard]] std::string occurrences(std::vector<pugi::xml_node> const& hits) {
    std::string s;
    for (std::size_t i = 0; i < hits.size(); ++i) {
        s += (i == 0 ? "" : " and ") + describe(hits[i]) + " (occurrence " +
             std::to_string(ordinal_of(hits[i])) + " of <" + std::string{name_of(hits[i])} +
             "> under " + where_of(hits[i].parent()) + ")";
    }
    return s;
}

// Deep copy of an overlay element into `parent`, appended after its last child:
// element children and attributes only (the classifier drops non-element overlay
// nodes), and of those only the ones `keep` admits, each with its subtree.
// Iterative. A failed pugixml allocation is `std::bad_alloc`, which the entry
// point turns into `xml_oom_error`.
template <class Keep>
void copy_element_if(pugi::xml_node parent, pugi::xml_node src, Keep const& keep) {
    struct item {
        pugi::xml_node dst_parent;
        pugi::xml_node src;
    };
    std::vector<item> stack{{.dst_parent = parent, .src = src}};
    while (!stack.empty()) {
        item it = stack.back();
        stack.pop_back();
        pugi::xml_node d = it.dst_parent.append_child(it.src.name());
        if (!d) {
            throw std::bad_alloc();
        }
        for (pugi::xml_attribute a : it.src.attributes()) {
            pugi::xml_attribute da = d.append_attribute(a.name());
            if (!da || !da.set_value(a.value())) {
                throw std::bad_alloc();
            }
        }
        // Pushed in reverse, so each parent receives its children in order.
        for (pugi::xml_node c = it.src.last_child(); c; c = c.previous_sibling()) {
            if (c.type() == pugi::node_element && keep(c)) {
                stack.push_back({.dst_parent = d, .src = c});
            }
        }
    }
}

void copy_element(pugi::xml_node parent, pugi::xml_node src) {
    copy_element_if(parent, src, [](pugi::xml_node) noexcept { return true; });
}

// The root containers an overlay `<fix>` may hold (data-model §A.1).
constexpr std::string_view kRootContainers[] = {"header", "trailer", "messages", "components",
                                                "fields"};

}  // namespace

// ── Step 2 ───────────────────────────────────────────────────────────────────

void throw_on_parse_failure(pugi::xml_parse_result const& result, source_format format) {
    if (result) {
        return;
    }
    if (result.status == pugi::status_out_of_memory) {
        throw xml_oom_error{};
    }
    if (format == source_format::orchestra) {
        throw orchestra_parse_error(std::string{"dict::orchestra_parse_error: "} +
                                    result.description());
    }
    throw xml_parse_error(std::string{"dict::xml_parse_error: "} + result.description());
}

// ── Step 1 ───────────────────────────────────────────────────────────────────

void check_overlay_size(std::size_t size) {
    if (size > kMaxOverlayBytes) {
        limit("overlay is larger than the byte cap of " + std::to_string(kMaxOverlayBytes) +
              " bytes");
    }
}

std::string read_overlay_file(std::filesystem::path const& path, source_format format) {
    auto const fail = [&](std::string const& what) {
        std::string const m = what + " overlay " + path.string();
        if (format == source_format::orchestra) {
            throw orchestra_parse_error("dict::orchestra_parse_error: " + m);
        }
        throw xml_parse_error("dict::xml_parse_error: " + m);
    };
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        fail("cannot open");
    }
    std::string buf(kMaxOverlayBytes + 1, '\0');
    in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
    if (in.bad()) {
        fail("cannot read");
    }
    auto const got = static_cast<std::size_t>(in.gcount());
    if (got == 0 && !in.eof()) {
        fail("cannot read");
    }
    check_overlay_size(got);
    buf.resize(got);
    return buf;
}

// ── Steps 3 and 4 ────────────────────────────────────────────────────────────

void check_no_doctype(pugi::xml_document const& overlay) {
    for (pugi::xml_node n = overlay.first_child(); n; n = n.next_sibling()) {
        if (n.type() == pugi::node_doctype) {
            limit("overlay carries a DOCTYPE; a DOCTYPE or entity declaration is refused");
        }
    }
}

void check_depth(pugi::xml_document const& overlay) {
    struct item {
        pugi::xml_node node;
        std::size_t level;
    };
    std::vector<item> stack;
    for (pugi::xml_node n = overlay.first_child(); n; n = n.next_sibling()) {
        if (n.type() == pugi::node_element) {
            stack.push_back({.node = n, .level = 1});
        }
    }
    while (!stack.empty()) {
        item const it = stack.back();
        stack.pop_back();
        if (it.level > kMaxDepth) {
            limit("overlay element nesting is deeper than the depth cap of " +
                  std::to_string(kMaxDepth) + " levels");
        }
        for (pugi::xml_node c = it.node.first_child(); c; c = c.next_sibling()) {
            if (c.type() == pugi::node_element) {
                stack.push_back({.node = c, .level = it.level + 1});
            }
        }
    }
}

// ── Step 8a ──────────────────────────────────────────────────────────────────

void check_pair_relation(length_pair_relation const& base, length_pair_relation const& spliced) {
    auto const fwd = [](length_pair_relation const& r, std::uint16_t t) -> std::uint16_t {
        auto const it = r.forward.find(t);
        return it == r.forward.end() ? 0 : it->second;
    };
    auto const rev = [](length_pair_relation const& r, std::uint16_t t) {
        auto const it = r.reverse.find(t);
        return it == r.reverse.end() ? std::vector<std::uint16_t>{} : it->second;
    };
    auto const list = [](std::vector<std::uint16_t> const& v) {
        std::string s = "[";
        for (std::size_t i = 0; i < v.size(); ++i) {
            s += (i == 0 ? "" : ",") + std::to_string(v[i]);
        }
        return s + "]";
    };
    auto const is_base_tag = [&](std::uint16_t t) {
        return std::ranges::binary_search(base.declared_tags, t);
    };
    // Orchestra: a Length two Data fields name, one of the three tags the base's.
    for (auto const& [length, datas] : spliced.collisions) {
        if (is_base_tag(length) || std::ranges::any_of(datas, is_base_tag)) {
            conflict("Length+Data pair relation of base tag " +
                     std::to_string(
                         is_base_tag(length) ? length : *std::ranges::find_if(datas, is_base_tag)) +
                     " changes: Length " + std::to_string(length) +
                     " is named by the lengthId of Data fields " + list(datas) +
                     " (a pair touching a base tag is given a second partner)");
        }
    }
    for (std::uint16_t const t : base.declared_tags) {
        if (fwd(base, t) != fwd(spliced, t)) {
            conflict("Length+Data pair relation of base tag " + std::to_string(t) +
                     " changes: as a Length its Data is " + std::to_string(fwd(base, t)) +
                     " in the base and " + std::to_string(fwd(spliced, t)) +
                     " with the overlay (a pair touching a base tag is gained, lost or given "
                     "a second partner)");
        }
        auto const rb = rev(base, t);
        auto const rs = rev(spliced, t);
        if (rb != rs) {
            conflict("Length+Data pair relation of base tag " + std::to_string(t) +
                     " changes: as a Data its Lengths are " + list(rb) + " in the base and " +
                     list(rs) +
                     " with the overlay (a pair touching a base tag is gained, lost or given "
                     "a second partner)");
        }
    }
}

// ── The merge ────────────────────────────────────────────────────────────────

struct xml_overlay_merge::impl {
    pugi::xml_document const& overlay_doc;
    pugi::xml_node base_root;
    pugi::xml_node overlay_root;

    // ---- the classifier's output ----
    // overlay element → the base node it is merged into
    std::unordered_map<node_ptr, pugi::xml_node> match_of;
    // base node → the overlay elements merged into it, in overlay order
    std::unordered_map<node_ptr, std::vector<pugi::xml_node>> merged_into;
    // matched overlay element → its element children the splice appends, in order
    std::unordered_map<node_ptr, std::vector<pugi::xml_node>> appended;
    // overlay root containers the base lacks (created by the splice), by name
    std::map<std::string, pugi::xml_node, std::less<>> created;
    // a lookup that hit a duplicated base key, or an overlay key repeated in one
    // overlay container: refused at step 7, in this order
    std::vector<std::string> deferred_conflicts;
    // matched field definitions, values and messages: identifying attributes
    // compared at step 7
    std::vector<std::pair<pugi::xml_node, pugi::xml_node>> matched_field_defs;
    std::vector<std::pair<pugi::xml_node, pugi::xml_node>> matched_values;
    std::vector<std::pair<pugi::xml_node, pugi::xml_node>> matched_messages;
    std::vector<std::pair<pugi::xml_node, pugi::xml_node>> matched_children;  // refs and groups
    // overlay `<field>` definitions the splice appends to `<fields>`
    std::vector<pugi::xml_node> new_fields;

    // base indexes needed by step 7's new-field checks
    match_index base_fields_by_name;

    impl(pugi::xml_document& b, pugi::xml_document const& o)
        : overlay_doc(o), base_root(b.child("fix")), overlay_root(o.document_element()) {}

    // ---- step 4a ----
    void check_root() const;

    // ---- the classifier ----
    void classify();
    void classify_definition_children(pugi::xml_node o, pugi::xml_node b);
    void match(pugi::xml_node o, pugi::xml_node b) {
        match_of.emplace(key_of(o), b);
        merged_into[key_of(b)].push_back(o);
    }

    [[nodiscard]] std::vector<pugi::xml_node> const& appended_of(pugi::xml_node o) const {
        static std::vector<pugi::xml_node> const kNone;
        auto const it = appended.find(key_of(o));
        return it == appended.end() ? kNone : it->second;
    }

    // ---- step 5 ----
    void check_caps() const;

    // ---- step 6 ----
    struct walk_result;
    [[nodiscard]] walk_result walk(bool merged) const;

    // ---- step 7 ----
    void check_conflicts(walk_result const& merged, walk_result const& base) const;

    // ---- step 8 ----
    void splice();
};

// ---- step 4a ----------------------------------------------------------------

void xml_overlay_merge::impl::check_root() const {
    if (!overlay_root || name_of(overlay_root) != "fix") {
        conflict("overlay root <" + std::string{overlay_root ? overlay_root.name() : ""} +
                 "> is not <fix>: a QuickFIX-XML base takes an overlay in its own format");
    }
    struct version {
        std::string type;
        int major = -1;
        int minor = -1;
        int servicepack = 0;
        bool well_formed = true;
    };
    auto const read = [](pugi::xml_node r) {
        version v;
        v.type = r.attribute("type").as_string("FIX");
        auto const num = [&](char const* a, int& out, bool required) {
            pugi::xml_attribute const at = r.attribute(a);
            std::string_view const s = at.as_string("");
            if (!at || s.empty()) {
                v.well_formed = v.well_formed && !required;
                return;
            }
            int x = 0;
            if (!parse_nonneg_int(s, x)) {
                v.well_formed = false;
                return;
            }
            out = x;
        };
        num("major", v.major, true);
        num("minor", v.minor, true);
        num("servicepack", v.servicepack, false);
        return v;
    };
    version const b = read(base_root);
    version const o = read(overlay_root);
    if (!o.well_formed || o.type != b.type || o.major != b.major || o.minor != b.minor ||
        o.servicepack != b.servicepack) {
        conflict("version mismatch: the overlay's <fix type=\"" + attr_of(overlay_root, "type") +
                 "\" major=\"" + attr_of(overlay_root, "major") + "\" minor=\"" +
                 attr_of(overlay_root, "minor") + "\" servicepack=\"" +
                 attr_of(overlay_root, "servicepack") + "\"> does not name the base's version");
    }
    std::set<std::string_view> seen;
    for (pugi::xml_node c = overlay_root.first_child(); c; c = c.next_sibling()) {
        if (c.type() != pugi::node_element) {
            continue;
        }
        std::string_view const n = name_of(c);
        if (std::ranges::find(kRootContainers, n) == std::end(kRootContainers)) {
            conflict("root child <" + std::string{n} +
                     "> is not allowed in an overlay <fix> (it may hold header, trailer, "
                     "messages, components and fields)");
        }
        if (!seen.insert(n).second) {
            conflict("two root containers <" + std::string{n} +
                     "> in the overlay; each container name may appear once");
        }
    }
}

// ---- the classifier ---------------------------------------------------------

void xml_overlay_merge::impl::classify_definition_children(pugi::xml_node o, pugi::xml_node b) {
    // A definition is a message, component, header, trailer or group. Its direct
    // field, component and group children match by kind and name among the base
    // definition's direct children; a matched group's children merge in turn.
    struct item {
        pugi::xml_node o;
        pugi::xml_node b;
    };
    std::vector<item> stack{{.o = o, .b = b}};
    while (!stack.empty()) {
        item const it = stack.back();
        stack.pop_back();
        match_index const idx = build_index(it.b, child_key);
        std::set<std::string> seen;
        for (pugi::xml_node c = it.o.first_child(); c; c = c.next_sibling()) {
            if (c.type() != pugi::node_element) {
                continue;
            }
            std::optional<std::string> const k = child_key(c);
            if (!k) {
                appended[key_of(it.o)].push_back(c);
                continue;
            }
            if (!seen.insert(*k).second) {
                deferred_conflicts.push_back("duplicate within the overlay: " + describe(c) +
                                             " appears twice under the overlay's " +
                                             describe(it.o));
            }
            auto const hit = idx.find(*k);
            if (hit == idx.end()) {
                appended[key_of(it.o)].push_back(c);
                continue;
            }
            if (hit->second.size() > 1) {
                deferred_conflicts.push_back("repeated base key: the overlay's " + describe(c) +
                                             " under " + describe(it.o) + " matches " +
                                             occurrences(hit->second) +
                                             "; the overlay cannot say which one it extends");
            }
            match(c, hit->second.front());
            matched_children.emplace_back(c, hit->second.front());
            if (name_of(c) == "group") {
                stack.push_back({.o = c, .b = hit->second.front()});
            }
        }
    }
}

void xml_overlay_merge::impl::classify() {
    base_fields_by_name = build_index(base_root.child("fields"), field_name_key);
    for (pugi::xml_node oc = overlay_root.first_child(); oc; oc = oc.next_sibling()) {
        if (oc.type() != pugi::node_element) {
            continue;
        }
        std::string const cname{name_of(oc)};
        pugi::xml_node const bc = base_root.child(cname.c_str());
        if (!bc) {
            created.emplace(cname, oc);
            if (cname == "fields") {
                for (pugi::xml_node f = oc.child("field"); f; f = f.next_sibling("field")) {
                    new_fields.push_back(f);
                }
            }
            continue;
        }
        match(oc, bc);
        if (cname == "header" || cname == "trailer") {
            classify_definition_children(oc, bc);
            continue;
        }
        auto const key_fn = cname == "messages"     ? message_key
                            : cname == "components" ? component_def_key
                                                    : field_number_key;
        match_index const idx = build_index(bc, key_fn);
        std::set<std::string> seen;
        for (pugi::xml_node c = oc.first_child(); c; c = c.next_sibling()) {
            if (c.type() != pugi::node_element) {
                continue;
            }
            std::optional<std::string> const k = key_fn(c);
            if (k && !seen.insert(*k).second) {
                deferred_conflicts.push_back("duplicate within the overlay: " + describe(c) +
                                             " appears twice under the overlay's <" + cname + ">");
            }
            auto const hit = k ? idx.find(*k) : idx.end();
            if (hit == idx.end()) {
                appended[key_of(oc)].push_back(c);
                if (cname == "fields" && name_of(c) == "field") {
                    new_fields.push_back(c);
                }
                continue;
            }
            if (hit->second.size() > 1) {
                deferred_conflicts.push_back("repeated base key: the overlay's " + describe(c) +
                                             " matches " + occurrences(hit->second) +
                                             "; the overlay cannot say which one it extends");
            }
            pugi::xml_node const b = hit->second.front();
            match(c, b);
            if (cname == "messages") {
                matched_messages.emplace_back(c, b);
                classify_definition_children(c, b);
            } else if (cname == "components") {
                classify_definition_children(c, b);
            } else {
                matched_field_defs.emplace_back(c, b);
                match_index const values = build_index(b, value_key);
                std::set<std::string> seen_values;
                for (pugi::xml_node v = c.first_child(); v; v = v.next_sibling()) {
                    if (v.type() != pugi::node_element) {
                        continue;
                    }
                    std::optional<std::string> const vk = value_key(v);
                    if (vk && !seen_values.insert(*vk).second) {
                        deferred_conflicts.push_back(
                            "duplicate within the overlay: " + describe(v) +
                            " appears twice under " + describe(c));
                    }
                    auto const vhit = vk ? values.find(*vk) : values.end();
                    if (vhit == values.end()) {
                        appended[key_of(c)].push_back(v);
                        continue;
                    }
                    if (vhit->second.size() > 1) {
                        deferred_conflicts.push_back(
                            "repeated base key: the overlay's " + describe(v) + " under " +
                            describe(c) + " matches " + occurrences(vhit->second) +
                            "; the overlay cannot say which one it extends");
                    }
                    match(v, vhit->second.front());
                    matched_values.emplace_back(v, vhit->second.front());
                }
            }
        }
    }
}

// ---- step 5 -----------------------------------------------------------------

void xml_overlay_merge::impl::check_caps() const {
    std::size_t fields = new_fields.size();
    std::size_t components = 0;
    std::size_t messages = 0;
    std::size_t values = 0;
    std::size_t references = 0;

    auto const appended_children_of = [&](pugi::xml_node container) {
        std::vector<pugi::xml_node> out;
        if (created.contains(std::string{name_of(container)}) &&
            created.at(std::string{name_of(container)}) == container) {
            for (pugi::xml_node c = container.first_child(); c; c = c.next_sibling()) {
                if (c.type() == pugi::node_element) {
                    out.push_back(c);
                }
            }
            return out;
        }
        return appended_of(container);
    };
    // Every reference element below `n` (not `n` itself), whether the splice
    // appends it or merges it in place.
    auto const count_references = [&](pugi::xml_node n) {
        std::vector<pugi::xml_node> stack;
        for (pugi::xml_node c = n.first_child(); c; c = c.next_sibling()) {
            stack.push_back(c);
        }
        while (!stack.empty()) {
            pugi::xml_node const e = stack.back();
            stack.pop_back();
            if (e.type() != pugi::node_element) {
                continue;
            }
            if (is_ref_kind(name_of(e))) {
                ++references;
            }
            for (pugi::xml_node c = e.first_child(); c; c = c.next_sibling()) {
                stack.push_back(c);
            }
        }
    };

    for (pugi::xml_node oc = overlay_root.first_child(); oc; oc = oc.next_sibling()) {
        if (oc.type() != pugi::node_element) {
            continue;
        }
        std::string_view const cname = name_of(oc);
        if (cname == "header" || cname == "trailer") {
            count_references(oc);
        } else if (cname == "messages" || cname == "components") {
            std::string_view const def = cname == "messages" ? "message" : "component";
            std::size_t& count = cname == "messages" ? messages : components;
            for (pugi::xml_node const c : appended_children_of(oc)) {
                count += name_of(c) == def ? 1 : 0;
            }
            for (pugi::xml_node c = oc.first_child(); c; c = c.next_sibling()) {
                count_references(c);
            }
        } else {  // fields: every value the splice appends, of a new or a matched field
            for (pugi::xml_node const f : new_fields) {
                for (pugi::xml_node v = f.child("value"); v; v = v.next_sibling("value")) {
                    ++values;
                }
            }
            for (auto const& [o, b] : matched_field_defs) {
                for (pugi::xml_node const v : appended_of(o)) {
                    values += name_of(v) == "value" ? 1 : 0;
                }
            }
        }
    }
    auto const check = [](std::size_t n, std::size_t cap, char const* what) {
        if (n > cap) {
            limit("overlay " + std::string{what} + " exceed the " + std::string{what} + " cap of " +
                  std::to_string(cap));
        }
    };
    check(fields, kMaxNewFields, "new field definitions");
    check(components, kMaxNewComponents, "new component definitions");
    check(messages, kMaxNewMessages, "new message definitions");
    check(values, kMaxNewEnumValues, "added enum values");
    check(references, kMaxReferences, "reference elements");
}

// ---- step 6 -----------------------------------------------------------------

namespace {

// A node of the virtual merged document: a base node (with whatever the overlay
// merges into it) or an appended overlay node.
struct vnode {
    pugi::xml_node base;
    pugi::xml_node fresh;
    [[nodiscard]] node_ptr key() const noexcept { return base ? key_of(base) : key_of(fresh); }
    [[nodiscard]] pugi::xml_node node() const noexcept { return base ? base : fresh; }
    explicit operator bool() const noexcept { return base || fresh; }
};

// One child of a definition as the walk sees it.
struct child_rec {
    enum class kind : std::uint8_t { field, group, component, other };
    kind k = kind::other;
    std::uint16_t tag = 0;  // field or group: its tag; 0 when the name is not declared
    vnode target;           // group: the group node; component: its definition, if resolved
};

struct def_info {
    std::uint64_t steps = 0;
    std::uint64_t entries = 0;
};

// The tags a definition emits at its own level, sorted by tag, each counted to at
// most two.
using level_set = std::vector<std::pair<std::uint16_t, std::uint8_t>>;

// Two occurrence counts summed, saturated at two.
[[nodiscard]] std::uint8_t level_count_add(std::uint8_t a, std::uint8_t b) noexcept {
    return static_cast<std::uint8_t>(std::min(2, int{a} + int{b}));
}

// Makes a level set of `s`, which holds (tag, count) pairs in any order with any
// repeats: one sort, then each run of one tag coalesced into one entry.
void level_coalesce(level_set& s) {
    std::ranges::sort(s, {}, &std::pair<std::uint16_t, std::uint8_t>::first);
    std::size_t w = 0;
    for (auto const& [t, n] : s) {
        if (w > 0 && s[w - 1].first == t) {
            s[w - 1].second = level_count_add(s[w - 1].second, n);
        } else {
            s[w++] = {t, level_count_add(0, n)};
        }
    }
    s.resize(w);
}

// The level set of both inputs' occurrences, by a two-pointer merge.
[[nodiscard]] level_set level_merge(level_set const& a, level_set const& b) {
    level_set out;
    out.reserve(a.size() + b.size());
    auto ia = a.begin();
    auto ib = b.begin();
    while (ia != a.end() || ib != b.end()) {
        if (ib == b.end() || (ia != a.end() && ia->first < ib->first)) {
            out.push_back(*ia++);
        } else if (ia == a.end() || ib->first < ia->first) {
            out.push_back(*ib++);
        } else {
            out.emplace_back(ia->first, level_count_add(ia->second, ib->second));
            ++ia;
            ++ib;
        }
    }
    return out;
}

using child_map = std::unordered_map<node_ptr, std::vector<child_rec>>;

enum class colour : std::uint8_t { grey, black };

// The part of step 6's walk both formats share: the definitions visited, their
// child records and post-order memo, the first-seen group of each NumInGroup tag,
// and the first cycle met.
struct graph_walk {
    std::unordered_map<node_ptr, def_info> memo;
    child_map children;              // per definition visited
    std::vector<vnode> visit_order;  // definitions in the order the walk first met them
    std::map<std::uint16_t, vnode> first_seen_groups;
    std::optional<std::string> cycle;  // the first cycle met, its definitions named
    std::unordered_map<node_ptr, colour> colours;
};

// Iterative depth-first walk from `root` with three colours and a post-order memo.
// `records_of(v)` gives a definition's children as the build visits them, and
// `label(v)` names a definition in a cycle message. A definition already visited
// is not walked again, which keeps the order of first encounters.
template <class RecordsOf, class Label>
def_info visit_definition(graph_walk& r, vnode root, RecordsOf const& records_of,
                          Label const& label) {
    if (auto const it = r.memo.find(root.key()); it != r.memo.end()) {
        return it->second;
    }
    struct frame {
        vnode v;
        std::size_t next = 0;
        def_info acc;
    };
    std::vector<frame> stack;
    auto const open = [&](vnode v) {
        r.colours[v.key()] = colour::grey;
        r.children.emplace(v.key(), records_of(v));
        r.visit_order.push_back(v);
        stack.push_back({.v = v, .next = 0, .acc = {}});
    };
    open(root);
    while (!stack.empty()) {
        frame& f = stack.back();
        auto const& kids = r.children.at(f.v.key());
        if (f.next == kids.size()) {
            def_info const done = f.acc;
            r.memo[f.v.key()] = done;
            r.colours[f.v.key()] = colour::black;
            stack.pop_back();
            if (!stack.empty()) {
                frame& p = stack.back();
                p.acc.steps = sat_add(p.acc.steps, done.steps);
                p.acc.entries = sat_add(p.acc.entries, done.entries);
            }
            continue;
        }
        child_rec const& c = kids[f.next++];
        f.acc.steps = sat_add(f.acc.steps, 1);
        if (c.k == child_rec::kind::field) {
            f.acc.entries = sat_add(f.acc.entries, 1);
            continue;
        }
        if (c.k == child_rec::kind::group && c.tag != 0) {
            f.acc.entries = sat_add(f.acc.entries, 1);
            r.first_seen_groups.emplace(c.tag, c.target);
        }
        if (!c.target) {
            continue;
        }
        auto const col = r.colours.find(c.target.key());
        if (col == r.colours.end()) {
            open(c.target);
            continue;
        }
        if (col->second == colour::grey) {
            // A back edge: record the cycle, and treat the edge as a leaf so the
            // walk still ends and the memo is complete.
            if (!r.cycle) {
                std::string s;
                bool on = false;
                for (frame const& g : stack) {
                    on = on || g.v.key() == c.target.key();
                    if (on) {
                        s += label(g.v) + " -> ";
                    }
                }
                r.cycle = s + label(c.target);
            }
            continue;
        }
        def_info const& m = r.memo.at(c.target.key());
        f.acc.steps = sat_add(f.acc.steps, m.steps);
        f.acc.entries = sat_add(f.acc.entries, m.entries);
    }
    return r.memo.at(root.key());
}

// Side-table fit (research R-5 step 6): every start `finalize` passes to its
// `std::uint16_t` cast, and every count it clamps, is at most 65535. `counts` are
// the definitions' entry counts in the order `finalize` assigns their starts.
[[nodiscard]] bool side_table_fits(std::vector<std::uint64_t> const& counts) noexcept {
    std::uint64_t start = 0;
    for (std::uint64_t const n : counts) {
        if (start > kSideTableMax || n > kSideTableMax) {
            return false;
        }
        start = sat_add(start, n);
    }
    return true;
}

// Step 7's level set of definition `root` in a walk's records, memoized per
// definition; iterative over component edges, a back edge read as a leaf.
level_set const& level_of(child_map const& children, std::unordered_map<node_ptr, level_set>& memo,
                          node_ptr root) {
    static level_set const kEmpty;
    if (!children.contains(root)) {
        return kEmpty;
    }
    std::unordered_set<node_ptr> open;
    std::vector<std::pair<node_ptr, std::size_t>> stack{{root, 0}};
    open.insert(root);
    while (!stack.empty()) {
        auto& [d, i] = stack.back();
        auto const& kids = children.at(d);
        bool descended = false;
        while (i < kids.size()) {
            child_rec const& c = kids[i];
            if (c.k == child_rec::kind::component && c.target &&
                children.contains(c.target.key()) && !memo.contains(c.target.key()) &&
                !open.contains(c.target.key())) {
                open.insert(c.target.key());
                stack.emplace_back(c.target.key(), 0);
                descended = true;
                break;
            }
            ++i;
        }
        if (descended) {
            continue;
        }
        level_set s;
        for (child_rec const& c : children.at(d)) {
            if ((c.k == child_rec::kind::field || c.k == child_rec::kind::group) && c.tag != 0) {
                s.emplace_back(c.tag, 1);
            } else if (c.k == child_rec::kind::component && c.target) {
                if (auto const it = memo.find(c.target.key()); it != memo.end()) {
                    s.insert(s.end(), it->second.begin(), it->second.end());
                }
            }
        }
        level_coalesce(s);
        open.erase(d);
        memo[d] = std::move(s);
        stack.pop_back();
    }
    return memo.at(root);
}

// A tag the merged level `m` holds twice where the base level `b` holds it at most
// once.
[[nodiscard]] std::optional<std::uint16_t> twice(level_set const& m, level_set const& b) {
    for (auto const& [t, n] : m) {
        if (n < 2) {
            continue;
        }
        auto const it =
            std::ranges::lower_bound(b, t, {}, &std::pair<std::uint16_t, std::uint8_t>::first);
        if (it == b.end() || it->first != t || it->second < 2) {
            return t;
        }
    }
    return std::nullopt;
}

// Step 7's set of definitions whose own level the overlay changes (research R-3):
// `changed` holds the ones the overlay extends or appends on entry, and gains every
// definition that inlines one of them through component references in `children`.
void close_upward(child_map const& children, std::unordered_set<node_ptr>& changed) {
    std::unordered_map<node_ptr, std::vector<node_ptr>> inlined_by;
    for (auto const& [d, kids] : children) {
        for (child_rec const& c : kids) {
            if (c.k == child_rec::kind::component && c.target) {
                inlined_by[c.target.key()].push_back(d);
            }
        }
    }
    std::vector<node_ptr> work(changed.begin(), changed.end());
    while (!work.empty()) {
        node_ptr const d = work.back();
        work.pop_back();
        if (auto const it = inlined_by.find(d); it != inlined_by.end()) {
            for (node_ptr const p : it->second) {
                if (changed.insert(p).second) {
                    work.push_back(p);
                }
            }
        }
    }
}

}  // namespace

struct xml_overlay_merge::impl::walk_result : graph_walk {
    std::vector<std::pair<std::string, vnode>> messages;  // finalize's sorted order
    vnode header;
    vnode trailer;
    std::vector<vnode> components;  // declaration order
    std::uint64_t total = 0;
    bool components_fit = true;
    bool groups_fit = true;
};

xml_overlay_merge::impl::walk_result xml_overlay_merge::impl::walk(bool merged) const {
    walk_result r;

    // The merged view of a base root container (or the created one).
    auto const container = [&](char const* name) -> vnode {
        if (pugi::xml_node const b = base_root.child(name)) {
            return vnode{.base = b, .fresh = {}};
        }
        if (merged) {
            if (auto const it = created.find(name); it != created.end()) {
                return vnode{.base = {}, .fresh = it->second};
            }
        }
        return {};
    };
    // A virtual node's children, in the order `expand_field_list` visits them.
    auto const children_of = [&](vnode v, auto&& fn) {
        if (v.base) {
            for (pugi::xml_node c = v.base.first_child(); c; c = c.next_sibling()) {
                fn(vnode{.base = c, .fresh = {}});
            }
            if (!merged) {
                return;
            }
            if (auto const it = merged_into.find(key_of(v.base)); it != merged_into.end()) {
                for (pugi::xml_node const o : it->second) {
                    for (pugi::xml_node const c : appended_of(o)) {
                        fn(vnode{.base = {}, .fresh = c});
                    }
                }
            }
            return;
        }
        for (pugi::xml_node c = v.fresh.first_child(); c; c = c.next_sibling()) {
            if (c.type() == pugi::node_element) {
                fn(vnode{.base = {}, .fresh = c});
            }
        }
    };

    // Field names to tags, the first `<field>` of a name winning (the loader's
    // `by_name_.emplace`); component names to definitions, the first winning.
    std::unordered_map<std::string, std::uint16_t> tag_by_name;
    std::unordered_map<std::string, vnode> component_by_name;
    if (vnode const fields = container("fields")) {
        children_of(fields, [&](vnode f) {
            pugi::xml_node const n = f.node();
            if (n.type() != pugi::node_element || name_of(n) != "field") {
                return;
            }
            if (auto const tag = parse_field_number(n.attribute("number").as_string(""))) {
                tag_by_name.emplace(attr_of(n, "name"), *tag);
            }
        });
    }
    if (vnode const comps = container("components")) {
        children_of(comps, [&](vnode c) {
            pugi::xml_node const n = c.node();
            if (n.type() != pugi::node_element || name_of(n) != "component") {
                return;
            }
            r.components.push_back(c);
            component_by_name.emplace(attr_of(n, "name"), c);
        });
    }
    if (vnode const msgs = container("messages")) {
        children_of(msgs, [&](vnode m) {
            pugi::xml_node const n = m.node();
            if (n.type() == pugi::node_element && name_of(n) == "message") {
                r.messages.emplace_back(attr_of(n, "msgtype"), m);
            }
        });
    }
    // `finalize` sorts its messages with this call and comparator; the same
    // algorithm over the same key sequence gives the same order, ties included.
    std::ranges::sort(r.messages, [](auto const& a, auto const& b) noexcept {
        return detail::bytewise_compare(a.first, b.first) < 0;
    });
    r.header = container("header");
    r.trailer = container("trailer");

    auto const records_of = [&](vnode v) {
        std::vector<child_rec> out;
        children_of(v, [&](vnode c) {
            pugi::xml_node const n = c.node();
            child_rec rec;
            std::string_view const k = n.type() == pugi::node_element ? name_of(n) : "";
            if (k == "field" || k == "group") {
                auto const it = tag_by_name.find(attr_of(n, "name"));
                rec.k = k == "field" ? child_rec::kind::field : child_rec::kind::group;
                rec.tag = it == tag_by_name.end() ? 0 : it->second;
                if (k == "group" && rec.tag != 0) {
                    rec.target = c;
                }
            } else if (k == "component") {
                rec.k = child_rec::kind::component;
                if (auto const it = component_by_name.find(attr_of(n, "name"));
                    it != component_by_name.end()) {
                    rec.target = it->second;
                }
            }
            out.push_back(rec);
        });
        return out;
    };
    auto const label = [](vnode v) {
        pugi::xml_node const n = v.node();
        return std::string{name_of(n)} + " " + attr_of(n, "name");
    };

    auto const visit = [&](vnode root) { return visit_definition(r, root, records_of, label); };

    std::uint64_t total = 0;
    for (auto const& [mt, m] : r.messages) {
        if (r.header) {
            total = sat_add(total, visit(r.header).steps);
        }
        total = sat_add(total, visit(m).steps);
        if (r.trailer) {
            total = sat_add(total, visit(r.trailer).steps);
        }
    }
    std::vector<std::uint64_t> comp_counts;
    for (vnode const c : r.components) {
        def_info const d = visit(c);
        total = sat_add(total, d.steps);
        comp_counts.push_back(d.entries);
    }
    std::vector<std::uint64_t> group_counts;
    for (auto const& [tag, g] : r.first_seen_groups) {
        def_info const d = visit(g);
        total = sat_add(total, d.steps);
        group_counts.push_back(d.entries);
    }
    r.total = total;
    r.components_fit = side_table_fits(comp_counts);
    r.groups_fit = side_table_fits(group_counts);
    return r;
}

// ---- step 7 -----------------------------------------------------------------

void xml_overlay_merge::impl::check_conflicts(walk_result const& merged,
                                              walk_result const& base) const {
    // Vocabulary: `required` is Y, N or absent on every overlay element.
    {
        std::vector<pugi::xml_node> stack{overlay_root};
        while (!stack.empty()) {
            pugi::xml_node const e = stack.back();
            stack.pop_back();
            if (pugi::xml_attribute const a = e.attribute("required")) {
                std::string_view const v = a.value();
                if (v != "Y" && v != "N") {
                    conflict("invalid required value: " + describe(e) + " has required=\"" +
                             std::string{v} + "\"; an overlay's required is Y, N or absent");
                }
            }
            for (pugi::xml_node c = e.first_child(); c; c = c.next_sibling()) {
                if (c.type() == pugi::node_element) {
                    stack.push_back(c);
                }
            }
        }
    }
    // Repeated base keys and keys repeated within the overlay.
    if (!deferred_conflicts.empty()) {
        conflict(deferred_conflicts.front());
    }
    // A matched field definition keeps the base's name and type.
    for (auto const& [o, b] : matched_field_defs) {
        for (char const* a : {"name", "type"}) {
            if (attr_of(o, a) != attr_of(b, a)) {
                conflict("identifying attribute changed: the overlay's " + describe(o) + " has " +
                         a + "=\"" + attr_of(o, a) + "\" where the base's field " +
                         attr_of(b, "number") + " has \"" + attr_of(b, a) + "\"");
            }
        }
    }
    // Refused: a new field whose name a base field or another new field already uses.
    {
        std::set<std::string> new_names;
        for (pugi::xml_node const f : new_fields) {
            std::string const n = attr_of(f, "name");
            if (auto const it = base_fields_by_name.find(n); it != base_fields_by_name.end()) {
                if (it->second.size() > 1) {
                    conflict("repeated base key: the overlay's " + describe(f) + " names " +
                             occurrences(it->second) +
                             "; the overlay cannot say which one it extends");
                }
                conflict("field name already used by another number: the overlay's " + describe(f) +
                         " reuses the name of the base's " + describe(it->second.front()));
            }
            if (!new_names.insert(n).second) {
                conflict("two new fields with one name: the overlay declares name \"" + n +
                         "\" twice");
            }
        }
    }
    // A matched enum value keeps its description.
    for (auto const& [o, b] : matched_values) {
        if (attr_of(o, "description") != attr_of(b, "description")) {
            conflict("redescribed enum value: the overlay's " + describe(o) + " under field " +
                     attr_of(b.parent(), "number") + " has description \"" +
                     attr_of(o, "description") + "\" where the base has \"" +
                     attr_of(b, "description") + "\"");
        }
    }
    // A matched message keeps its name.
    for (auto const& [o, b] : matched_messages) {
        if (attr_of(o, "name") != attr_of(b, "name")) {
            conflict("identifying attribute changed: the overlay's " + describe(o) +
                     " has name=\"" + attr_of(o, "name") + "\" where the base's has \"" +
                     attr_of(b, "name") + "\"");
        }
    }
    // The overlay cannot relax a Required child.
    for (auto const& [o, b] : matched_children) {
        if (attr_of(o, "required") == "N" && attr_of(b, "required") == "Y") {
            conflict("required relaxed: the overlay's " + describe(o) + " under " +
                     describe(b.parent()) + " has required=\"N\" where the base has \"Y\"");
        }
    }

    // One tag twice at one level (research R-3), from the walks' records. The
    // parents checked are the definitions whose own level the overlay changes:
    // each one the overlay appends children to, each appended definition, every
    // definition that inlines one of those through components, and every message
    // when the header or trailer changes.
    std::unordered_set<node_ptr> changed;
    for (auto const& [b, os] : merged_into) {
        for (pugi::xml_node const o : os) {
            if (!appended_of(o).empty()) {
                changed.insert(b);
            }
        }
    }
    for (vnode const v : merged.visit_order) {
        if (!v.base) {
            changed.insert(v.key());
        }
    }
    close_upward(merged.children, changed);  // upward through component references
    bool const frame_changed = (merged.header && changed.contains(merged.header.key())) ||
                               (merged.trailer && changed.contains(merged.trailer.key()));

    // Level sets, memoized per definition and per walk.
    std::unordered_map<node_ptr, level_set> merged_levels;
    std::unordered_map<node_ptr, level_set> base_levels;
    auto const refuse_twice = [](std::uint16_t tag, std::string const& where) {
        conflict("tag occurs twice at one level: tag " + std::to_string(tag) +
                 " occurs twice at "
                 "the level of " +
                 where + ", where the base has it at most once");
    };

    // Messages: header, body and trailer fold into one level.
    for (std::size_t i = 0; i < merged.messages.size(); ++i) {
        vnode const m = merged.messages[i].second;
        if (!frame_changed && !changed.contains(m.key())) {
            continue;
        }
        level_set ml;
        level_set bl;
        for (vnode const part : {merged.header, m, merged.trailer}) {
            if (part) {
                ml = level_merge(ml, level_of(merged.children, merged_levels, part.key()));
            }
        }
        if (m.base) {
            for (vnode const part : {base.header, m, base.trailer}) {
                if (part) {
                    bl = level_merge(bl, level_of(base.children, base_levels, part.key()));
                }
            }
        }
        if (auto const t = twice(ml, bl)) {
            refuse_twice(*t, describe(m.node()));
        }
    }
    // Components and groups: their own level.
    static level_set const kNoBaseLevel;
    for (vnode const v : merged.visit_order) {
        std::string_view const k = name_of(v.node());
        if ((k != "component" && k != "group") || !changed.contains(v.key())) {
            continue;
        }
        level_set const& ml = level_of(merged.children, merged_levels, v.key());
        level_set const& bl = v.base ? level_of(base.children, base_levels, v.key()) : kNoBaseLevel;
        if (auto const t = twice(ml, bl)) {
            refuse_twice(*t, describe(v.node()));
        }
    }
}

// ---- step 8 -----------------------------------------------------------------

void xml_overlay_merge::impl::splice() {
    for (pugi::xml_node oc = overlay_root.first_child(); oc; oc = oc.next_sibling()) {
        if (oc.type() != pugi::node_element) {
            continue;
        }
        pugi::xml_node const bc = base_root.child(oc.name());
        if (!bc) {
            copy_element(base_root, oc);  // a container the base lacks
            continue;
        }
        // Each matched element: its promotion, then its appended children.
        std::vector<std::pair<pugi::xml_node, pugi::xml_node>> stack{{oc, bc}};
        while (!stack.empty()) {
            auto [o, b] = stack.back();
            stack.pop_back();
            if (attr_of(o, "required") == "Y" && attr_of(b, "required") != "Y") {
                pugi::xml_attribute a = b.attribute("required");
                if (!a) {
                    a = b.append_attribute("required");
                }
                if (!a || !a.set_value("Y")) {
                    throw std::bad_alloc();
                }
            }
            for (pugi::xml_node const c : appended_of(o)) {
                copy_element(b, c);
            }
            for (pugi::xml_node c = o.last_child(); c; c = c.previous_sibling()) {
                if (auto const it = match_of.find(key_of(c)); it != match_of.end()) {
                    stack.emplace_back(c, it->second);
                }
            }
        }
    }
}

// ── The public half ──────────────────────────────────────────────────────────

xml_overlay_merge::xml_overlay_merge(pugi::xml_document& base, pugi::xml_document const& overlay)
    : impl_(std::make_unique<impl>(base, overlay)) {}

xml_overlay_merge::~xml_overlay_merge() = default;

std::uint64_t xml_overlay_merge::guard(overlay_trace& trace) {
    // No policy parameter: the counts step 6 models do not depend on
    // `unresolved_group_policy` (see the file header).
    impl& m = *impl_;
    check_no_doctype(m.overlay_doc);  // step 3
    check_depth(m.overlay_doc);       // step 4
    m.check_root();                   // step 4a
    m.classify();
    m.check_caps();  // step 5

    // Step 6: the merged-graph walk, then the base's own, then its refusals.
    auto const merged = m.walk(/*merged=*/true);
    auto const base = m.walk(/*merged=*/false);
    guard_outcome g;
    g.base_total = base.total;
    g.merged_total = merged.total;
    g.arm_limit = std::min(merged.total, sat_add(base.total, kExpansionBudget));
    g.base_components_fit = base.components_fit;
    g.base_groups_fit = base.groups_fit;
    g.merged_components_fit = merged.components_fit;
    g.merged_groups_fit = merged.groups_fit;
    trace.guards = g;
    if (merged.cycle) {
        throw xml_parse_error("dict::xml_parse_error: component reference cycle: " + *merged.cycle);
    }
    if (sat_sub(merged.total, base.total) > kExpansionBudget) {
        trace.origin = refusal_origin::budget;
        limit("the overlay adds more expansion steps than the expansion budget of " +
              std::to_string(kExpansionBudget));
    }
    if (base.components_fit && !merged.components_fit) {
        trace.origin = refusal_origin::side_table;
        limit(
            "the components side table would stop fitting 16 bits (a start or a count "
            "above 65535)");
    }
    if (base.groups_fit && !merged.groups_fit) {
        trace.origin = refusal_origin::side_table;
        limit(
            "the groups side table would stop fitting 16 bits (a start or a count above "
            "65535)");
    }

    m.check_conflicts(merged, base);  // step 7
    return g.arm_limit;
}

void xml_overlay_merge::splice() { impl_->splice(); }

// ══ The Orchestra merge (research R-3's Orchestra table) ═════════════════════
//
// The same shape as the QuickFIX-XML merge above: one classifier that steps 5, 6,
// 7 and 8 read, step 6 over the virtual merged document, step 7 from step 6's
// records. What differs is the model of `OrchestraLoaderState::finalize`
// (re-derive it by reading `finalize`): messages are expanded sorted bytewise by
// msgType, each as its `fixr:structure` only (the header and trailer are the
// ordinary component references 1024 and 1025 inside it); then every component in
// declaration order; then the first-seen group definition of each NumInGroup tag,
// registered when an expansion first meets a `fixr:groupRef` to a definition of
// that tag, standalone in NumInGroup tag order. Every child node of a structure,
// component or group definition is one step (`fixr:numInGroup` and
// `fixr:annotation` included); a `fixr:componentRef` or `fixr:groupRef` adds the
// steps of the definition its `id` names. A group is registered before its
// members are expanded under either `unresolved_group_policy`, so the counts do
// not depend on the policy. The splice drops `fixr:annotation` and
// `fixr:documentation` at any depth and a matched group's `fixr:numInGroup`
// (`orch_dropped`, and the classifier's group rule); step 6 counts what it keeps.

namespace {

// The root containers an overlay `fixr:repository` may hold (data-model §A.1).
constexpr std::string_view kOrchContainers[] = {"fixr:codeSets", "fixr:fields", "fixr:components",
                                                "fixr:groups", "fixr:messages"};

// Dropped by the splice wherever they appear inside an overlay definition.
[[nodiscard]] bool orch_dropped(pugi::xml_node n) noexcept {
    std::string_view const k = name_of(n);
    return k == "fixr:annotation" || k == "fixr:documentation";
}

[[nodiscard]] bool is_orch_ref(std::string_view k) noexcept {
    return k == "fixr:fieldRef" || k == "fixr:componentRef" || k == "fixr:groupRef";
}

[[nodiscard]] std::optional<std::string> orch_id_key_of(pugi::xml_node c, std::string_view kind) {
    if (name_of(c) != kind) {
        return std::nullopt;
    }
    std::uint16_t id = 0;
    if (!try_parse_uint16(c.attribute("id").as_string(""), id)) {
        return std::nullopt;
    }
    return std::to_string(id);
}

[[nodiscard]] std::optional<std::string> orch_attr_key_of(pugi::xml_node c, std::string_view kind,
                                                          char const* attr) {
    if (name_of(c) != kind) {
        return std::nullopt;
    }
    return attr_of(c, attr);
}

[[nodiscard]] std::optional<std::string> orch_def_key(std::string_view container,
                                                      pugi::xml_node c) {
    if (container == "fixr:codeSets") {
        return orch_attr_key_of(c, "fixr:codeSet", "name");
    }
    if (container == "fixr:fields") {
        return orch_id_key_of(c, "fixr:field");
    }
    if (container == "fixr:components") {
        return orch_id_key_of(c, "fixr:component");
    }
    if (container == "fixr:groups") {
        return orch_id_key_of(c, "fixr:group");
    }
    return orch_attr_key_of(c, "fixr:message", "msgType");
}

// The definition element each root container holds.
[[nodiscard]] std::string_view orch_def_kind(std::string_view container) noexcept {
    if (container == "fixr:codeSets") {
        return "fixr:codeSet";
    }
    if (container == "fixr:fields") {
        return "fixr:field";
    }
    if (container == "fixr:components") {
        return "fixr:component";
    }
    if (container == "fixr:groups") {
        return "fixr:group";
    }
    return "fixr:message";
}

[[nodiscard]] std::optional<std::string> orch_child_key(pugi::xml_node c) {
    if (!is_orch_ref(name_of(c))) {
        return std::nullopt;
    }
    std::uint16_t id = 0;
    if (!try_parse_uint16(c.attribute("id").as_string(""), id)) {
        return std::nullopt;
    }
    return std::string{name_of(c)} + "|" + std::to_string(id);
}

[[nodiscard]] std::optional<std::string> orch_code_key(pugi::xml_node c) {
    return orch_attr_key_of(c, "fixr:code", "value");
}

[[nodiscard]] std::optional<std::string> orch_field_name_key(pugi::xml_node c) {
    return orch_attr_key_of(c, "fixr:field", "name");
}

// `<fixr:field id="11" name="ClOrdID">`, `<fixr:message msgType="D">` and the like.
[[nodiscard]] std::string orch_describe(pugi::xml_node n) {
    std::string const kind{name_of(n)};
    if (kind == "fixr:message") {
        return "<fixr:message msgType=\"" + attr_of(n, "msgType") + "\">";
    }
    if (kind == "fixr:codeSet") {
        return "<fixr:codeSet name=\"" + attr_of(n, "name") + "\">";
    }
    if (kind == "fixr:code") {
        return "<fixr:code value=\"" + attr_of(n, "value") + "\">";
    }
    if (n.attribute("id")) {
        return "<" + kind + " id=\"" + attr_of(n, "id") + "\"" +
               (n.attribute("name") ? " name=\"" + attr_of(n, "name") + "\"" : std::string{}) + ">";
    }
    return "<" + kind + ">";
}

[[nodiscard]] std::string orch_where_of(pugi::xml_node parent) {
    return name_of(parent) == "fixr:repository" ? std::string{"<fixr:repository>"}
                                                : orch_describe(parent);
}

[[nodiscard]] std::string orch_occurrences(std::vector<pugi::xml_node> const& hits) {
    std::string s;
    for (std::size_t i = 0; i < hits.size(); ++i) {
        s += (i == 0 ? "" : " and ") + orch_describe(hits[i]) + " (occurrence " +
             std::to_string(ordinal_of(hits[i])) + " of <" + std::string{name_of(hits[i])} +
             "> under " + orch_where_of(hits[i].parent()) + ")";
    }
    return s;
}

}  // namespace

struct orchestra_overlay_merge::impl {
    pugi::xml_document const& overlay_doc;
    pugi::xml_node base_root;
    pugi::xml_node overlay_root;

    // ---- the classifier's output ----
    std::unordered_map<node_ptr, pugi::xml_node> match_of;
    std::unordered_map<node_ptr, std::vector<pugi::xml_node>> merged_into;
    std::unordered_map<node_ptr, std::vector<pugi::xml_node>> appended;
    std::map<std::string, pugi::xml_node, std::less<>> created;
    std::vector<std::string> deferred_conflicts;
    std::vector<std::pair<pugi::xml_node, pugi::xml_node>> matched_codesets;
    std::vector<std::pair<pugi::xml_node, pugi::xml_node>> matched_codes;
    std::vector<std::pair<pugi::xml_node, pugi::xml_node>> matched_fields;
    std::vector<std::pair<pugi::xml_node, pugi::xml_node>> matched_components;
    std::vector<std::pair<pugi::xml_node, pugi::xml_node>> matched_groups;
    std::vector<std::pair<pugi::xml_node, pugi::xml_node>> matched_messages;
    std::vector<std::pair<pugi::xml_node, pugi::xml_node>> matched_children;  // references
    // a matched overlay group → its `fixr:numInGroup` children, which are not spliced
    std::unordered_map<node_ptr, std::vector<pugi::xml_node>> group_numingroups;
    // new definitions, by kind, in overlay order
    std::vector<pugi::xml_node> new_codesets;
    std::vector<pugi::xml_node> new_fields;
    std::vector<pugi::xml_node> new_components;
    std::vector<pugi::xml_node> new_groups;
    std::vector<pugi::xml_node> new_messages;

    match_index base_fields_by_name;

    impl(pugi::xml_document& b, pugi::xml_document const& o)
        : overlay_doc(o), base_root(b.first_child()), overlay_root(o.document_element()) {}

    void match(pugi::xml_node o, pugi::xml_node b) {
        match_of.emplace(key_of(o), b);
        merged_into[key_of(b)].push_back(o);
    }

    [[nodiscard]] std::vector<pugi::xml_node> const& appended_of(pugi::xml_node o) const {
        static std::vector<pugi::xml_node> const kNone;
        auto const it = appended.find(key_of(o));
        return it == appended.end() ? kNone : it->second;
    }

    void note_new(std::string_view container, pugi::xml_node c);
    void check_root() const;
    void classify();
    void classify_definition_children(pugi::xml_node o, pugi::xml_node b, bool is_group);
    void classify_codes(pugi::xml_node o, pugi::xml_node b);
    void classify_message(pugi::xml_node o, pugi::xml_node b);
    void check_caps() const;

    struct walk_result;
    [[nodiscard]] walk_result walk(bool merged) const;

    void check_conflicts(walk_result const& merged, walk_result const& base) const;
    void splice();
};

// ---- step 4a ----------------------------------------------------------------

void orchestra_overlay_merge::impl::check_root() const {
    if (!overlay_root || name_of(overlay_root) != "fixr:repository") {
        conflict("overlay root <" + std::string{overlay_root ? overlay_root.name() : ""} +
                 "> is not <fixr:repository>: an Orchestra base takes an overlay in its own "
                 "format");
    }
    if (attr_of(overlay_root, "version") != attr_of(base_root, "version")) {
        conflict("version mismatch: the overlay's <fixr:repository version=\"" +
                 attr_of(overlay_root, "version") + "\"> does not name the base's version \"" +
                 attr_of(base_root, "version") + "\"");
    }
    std::set<std::string_view> seen;
    for (pugi::xml_node c = overlay_root.first_child(); c; c = c.next_sibling()) {
        if (c.type() != pugi::node_element) {
            continue;
        }
        std::string_view const n = name_of(c);
        if (std::ranges::find(kOrchContainers, n) == std::end(kOrchContainers)) {
            conflict("root child <" + std::string{n} +
                     "> is not allowed in an overlay <fixr:repository> (it may hold "
                     "fixr:codeSets, fixr:fields, fixr:components, fixr:groups and "
                     "fixr:messages)");
        }
        if (!seen.insert(n).second) {
            conflict("two root containers <" + std::string{n} +
                     "> in the overlay; each container name may appear once");
        }
    }
}

// ---- the classifier ---------------------------------------------------------

void orchestra_overlay_merge::impl::note_new(std::string_view container, pugi::xml_node c) {
    if (name_of(c) != orch_def_kind(container)) {
        return;
    }
    if (container == "fixr:codeSets") {
        new_codesets.push_back(c);
    } else if (container == "fixr:fields") {
        new_fields.push_back(c);
    } else if (container == "fixr:components") {
        new_components.push_back(c);
    } else if (container == "fixr:groups") {
        new_groups.push_back(c);
    } else {
        new_messages.push_back(c);
    }
}

void orchestra_overlay_merge::impl::classify_definition_children(pugi::xml_node o, pugi::xml_node b,
                                                                 bool is_group) {
    // A structure, component or group: its references match by kind and `id` among
    // the base definition's direct children. A matched group's `fixr:numInGroup` is
    // checked at step 7 and never spliced.
    match_index const idx = build_index(b, orch_child_key);
    std::set<std::string> seen;
    for (pugi::xml_node c = o.first_child(); c; c = c.next_sibling()) {
        if (c.type() != pugi::node_element || orch_dropped(c)) {
            continue;
        }
        if (is_group && name_of(c) == "fixr:numInGroup") {
            group_numingroups[key_of(o)].push_back(c);
            continue;
        }
        std::optional<std::string> const k = orch_child_key(c);
        if (!k) {
            appended[key_of(o)].push_back(c);
            continue;
        }
        if (!seen.insert(*k).second) {
            deferred_conflicts.push_back("duplicate within the overlay: " + orch_describe(c) +
                                         " appears twice under the overlay's " + orch_describe(o));
        }
        auto const hit = idx.find(*k);
        if (hit == idx.end()) {
            appended[key_of(o)].push_back(c);
            continue;
        }
        if (hit->second.size() > 1) {
            deferred_conflicts.push_back("repeated base key: the overlay's " + orch_describe(c) +
                                         " under " + orch_describe(o) + " matches " +
                                         orch_occurrences(hit->second) +
                                         "; the overlay cannot say which one it extends");
        }
        match(c, hit->second.front());
        matched_children.emplace_back(c, hit->second.front());
        for (pugi::xml_node rc = c.first_child(); rc; rc = rc.next_sibling()) {
            if (rc.type() == pugi::node_element && !orch_dropped(rc)) {
                appended[key_of(c)].push_back(rc);
            }
        }
    }
}

void orchestra_overlay_merge::impl::classify_codes(pugi::xml_node o, pugi::xml_node b) {
    match_index const idx = build_index(b, orch_code_key);
    std::set<std::string> seen;
    for (pugi::xml_node v = o.first_child(); v; v = v.next_sibling()) {
        if (v.type() != pugi::node_element || orch_dropped(v)) {
            continue;
        }
        std::optional<std::string> const k = orch_code_key(v);
        if (k && !seen.insert(*k).second) {
            deferred_conflicts.push_back("duplicate within the overlay: " + orch_describe(v) +
                                         " appears twice under " + orch_describe(o));
        }
        auto const hit = k ? idx.find(*k) : idx.end();
        if (hit == idx.end()) {
            appended[key_of(o)].push_back(v);
            continue;
        }
        if (hit->second.size() > 1) {
            deferred_conflicts.push_back("repeated base key: the overlay's " + orch_describe(v) +
                                         " under " + orch_describe(o) + " matches " +
                                         orch_occurrences(hit->second) +
                                         "; the overlay cannot say which one it extends");
        }
        match(v, hit->second.front());
        matched_codes.emplace_back(v, hit->second.front());
    }
}

void orchestra_overlay_merge::impl::classify_message(pugi::xml_node o, pugi::xml_node b) {
    // A message's `fixr:structure` merges with the base's (the loader reads the
    // first); any other kept child is appended.
    pugi::xml_node const bs = b.child("fixr:structure");
    bool structure_seen = false;
    for (pugi::xml_node c = o.first_child(); c; c = c.next_sibling()) {
        if (c.type() != pugi::node_element || orch_dropped(c)) {
            continue;
        }
        if (name_of(c) == "fixr:structure") {
            if (structure_seen) {
                deferred_conflicts.push_back(
                    "duplicate within the overlay: <fixr:structure> "
                    "appears twice under the overlay's " +
                    orch_describe(o));
            }
            structure_seen = true;
            if (bs) {
                match(c, bs);
                classify_definition_children(c, bs, /*is_group=*/false);
                continue;
            }
        }
        appended[key_of(o)].push_back(c);
    }
}

void orchestra_overlay_merge::impl::classify() {
    base_fields_by_name = build_index(base_root.child("fixr:fields"), orch_field_name_key);
    for (pugi::xml_node oc = overlay_root.first_child(); oc; oc = oc.next_sibling()) {
        if (oc.type() != pugi::node_element) {
            continue;
        }
        std::string const cname{name_of(oc)};
        pugi::xml_node const bc = base_root.child(cname.c_str());
        if (!bc) {
            created.emplace(cname, oc);
            for (pugi::xml_node c = oc.first_child(); c; c = c.next_sibling()) {
                if (c.type() == pugi::node_element && !orch_dropped(c)) {
                    note_new(cname, c);
                }
            }
            continue;
        }
        match(oc, bc);
        match_index const idx =
            build_index(bc, [&](pugi::xml_node c) { return orch_def_key(cname, c); });
        std::set<std::string> seen;
        for (pugi::xml_node c = oc.first_child(); c; c = c.next_sibling()) {
            if (c.type() != pugi::node_element || orch_dropped(c)) {
                continue;
            }
            std::optional<std::string> const k = orch_def_key(cname, c);
            if (k && !seen.insert(*k).second) {
                deferred_conflicts.push_back("duplicate within the overlay: " + orch_describe(c) +
                                             " appears twice under the overlay's <" + cname + ">");
            }
            auto const hit = k ? idx.find(*k) : idx.end();
            if (hit == idx.end()) {
                appended[key_of(oc)].push_back(c);
                note_new(cname, c);
                continue;
            }
            if (hit->second.size() > 1) {
                deferred_conflicts.push_back("repeated base key: the overlay's " +
                                             orch_describe(c) + " matches " +
                                             orch_occurrences(hit->second) +
                                             "; the overlay cannot say which one it extends");
            }
            pugi::xml_node const b = hit->second.front();
            match(c, b);
            if (cname == "fixr:codeSets") {
                matched_codesets.emplace_back(c, b);
                classify_codes(c, b);
            } else if (cname == "fixr:fields") {
                matched_fields.emplace_back(c, b);
                for (pugi::xml_node fc = c.first_child(); fc; fc = fc.next_sibling()) {
                    if (fc.type() == pugi::node_element && !orch_dropped(fc)) {
                        appended[key_of(c)].push_back(fc);
                    }
                }
            } else if (cname == "fixr:components") {
                matched_components.emplace_back(c, b);
                classify_definition_children(c, b, /*is_group=*/false);
            } else if (cname == "fixr:groups") {
                matched_groups.emplace_back(c, b);
                classify_definition_children(c, b, /*is_group=*/true);
            } else {
                matched_messages.emplace_back(c, b);
                classify_message(c, b);
            }
        }
    }
}

// ---- step 5 -----------------------------------------------------------------

void orchestra_overlay_merge::impl::check_caps() const {
    std::size_t codes = 0;
    for (pugi::xml_node const cs : new_codesets) {
        for (pugi::xml_node v = cs.child("fixr:code"); v; v = v.next_sibling("fixr:code")) {
            ++codes;
        }
    }
    for (auto const& [o, b] : matched_codesets) {
        for (pugi::xml_node const v : appended_of(o)) {
            codes += name_of(v) == "fixr:code" ? 1 : 0;
        }
    }
    // Every reference element of the overlay, whether the splice appends it or
    // merges it in place.
    std::size_t references = 0;
    std::vector<pugi::xml_node> stack{overlay_root};
    while (!stack.empty()) {
        pugi::xml_node const e = stack.back();
        stack.pop_back();
        for (pugi::xml_node c = e.first_child(); c; c = c.next_sibling()) {
            if (c.type() == pugi::node_element) {
                references += is_orch_ref(name_of(c)) ? 1 : 0;
                stack.push_back(c);
            }
        }
    }
    auto const check = [](std::size_t n, std::size_t cap, char const* what) {
        if (n > cap) {
            limit("overlay " + std::string{what} + " exceed the " + std::string{what} + " cap of " +
                  std::to_string(cap));
        }
    };
    check(new_fields.size(), kMaxNewFields, "new field definitions");
    check(new_components.size(), kMaxNewComponents, "new component definitions");
    check(new_messages.size(), kMaxNewMessages, "new message definitions");
    check(new_groups.size(), kMaxNewGroups, "new group definitions");
    check(new_codesets.size(), kMaxNewCodeSets, "new code set definitions");
    check(codes, kMaxNewEnumValues, "added codes");
    check(references, kMaxReferences, "reference elements");
}

// ---- step 6 -----------------------------------------------------------------

struct orchestra_overlay_merge::impl::walk_result : graph_walk {
    std::vector<std::pair<std::string, vnode>> messages;  // msgType, structure; sorted
    std::vector<vnode> components;                        // declaration order
    std::uint64_t total = 0;
    bool components_fit = true;
    bool groups_fit = true;
};

orchestra_overlay_merge::impl::walk_result orchestra_overlay_merge::impl::walk(bool merged) const {
    walk_result r;

    auto const container = [&](char const* name) -> vnode {
        if (pugi::xml_node const b = base_root.child(name)) {
            return vnode{.base = b, .fresh = {}};
        }
        if (merged) {
            if (auto const it = created.find(name); it != created.end()) {
                return vnode{.base = {}, .fresh = it->second};
            }
        }
        return {};
    };
    // A virtual node's children, in the order `expand_field_list` visits them.
    auto const children_of = [&](vnode v, auto&& fn) {
        if (v.base) {
            for (pugi::xml_node c = v.base.first_child(); c; c = c.next_sibling()) {
                fn(vnode{.base = c, .fresh = {}});
            }
            if (!merged) {
                return;
            }
            if (auto const it = merged_into.find(key_of(v.base)); it != merged_into.end()) {
                for (pugi::xml_node const o : it->second) {
                    for (pugi::xml_node const c : appended_of(o)) {
                        fn(vnode{.base = {}, .fresh = c});
                    }
                }
            }
            return;
        }
        for (pugi::xml_node c = v.fresh.first_child(); c; c = c.next_sibling()) {
            if (c.type() == pugi::node_element && !orch_dropped(c)) {
                fn(vnode{.base = {}, .fresh = c});
            }
        }
    };
    auto const first_child_named = [&](vnode v, std::string_view name) -> vnode {
        vnode out;
        children_of(v, [&](vnode c) {
            if (!out && c.node().type() == pugi::node_element && name_of(c.node()) == name) {
                out = c;
            }
        });
        return out;
    };

    // Declared field ids; component and group definitions by id, the first of an id
    // winning (the loader refuses a repeated one).
    std::unordered_set<std::uint16_t> declared;
    std::unordered_map<std::uint16_t, vnode> component_by_id;
    std::unordered_map<std::uint16_t, vnode> group_by_id;
    auto const each_def = [&](char const* cname, std::string_view kind, auto&& fn) {
        if (vnode const cont = container(cname)) {
            children_of(cont, [&](vnode d) {
                pugi::xml_node const n = d.node();
                if (n.type() == pugi::node_element && name_of(n) == kind) {
                    fn(d, n);
                }
            });
        }
    };
    each_def("fixr:fields", "fixr:field", [&](vnode, pugi::xml_node n) {
        if (std::uint16_t id = 0; try_parse_uint16(n.attribute("id").as_string(""), id)) {
            declared.insert(id);
        }
    });
    each_def("fixr:components", "fixr:component", [&](vnode d, pugi::xml_node n) {
        r.components.push_back(d);
        if (std::uint16_t id = 0; try_parse_uint16(n.attribute("id").as_string(""), id)) {
            component_by_id.emplace(id, d);
        }
    });
    each_def("fixr:groups", "fixr:group", [&](vnode d, pugi::xml_node n) {
        if (std::uint16_t id = 0; try_parse_uint16(n.attribute("id").as_string(""), id)) {
            group_by_id.emplace(id, d);
        }
    });
    each_def("fixr:messages", "fixr:message", [&](vnode d, pugi::xml_node n) {
        r.messages.emplace_back(attr_of(n, "msgType"), first_child_named(d, "fixr:structure"));
    });
    // `finalize` sorts its messages with this call and comparator; the same
    // algorithm over the same key sequence gives the same order, ties included.
    std::ranges::sort(r.messages, [](auto const& a, auto const& b) noexcept {
        return detail::bytewise_compare(a.first, b.first) < 0;
    });

    // A group definition's NumInGroup tag: its first `fixr:numInGroup`'s id, when
    // that names a declared field; 0 otherwise (the loader refuses such a group
    // when it is referenced).
    auto const group_tag = [&](vnode g) -> std::uint16_t {
        vnode const nig = first_child_named(g, "fixr:numInGroup");
        if (!nig) {
            return 0;
        }
        std::uint16_t id = 0;
        return try_parse_uint16(nig.node().attribute("id").as_string(""), id) &&
                       declared.contains(id)
                   ? id
                   : 0;
    };
    auto const records_of = [&](vnode v) {
        std::vector<child_rec> out;
        children_of(v, [&](vnode c) {
            pugi::xml_node const n = c.node();
            child_rec rec;
            std::string_view const k = n.type() == pugi::node_element ? name_of(n) : "";
            std::uint16_t id = 0;
            bool const has_id = try_parse_uint16(n.attribute("id").as_string(""), id);
            if (k == "fixr:fieldRef") {
                rec.k = child_rec::kind::field;
                rec.tag = has_id && declared.contains(id) ? id : 0;
            } else if (k == "fixr:groupRef") {
                rec.k = child_rec::kind::group;
                if (has_id) {
                    if (auto const it = group_by_id.find(id); it != group_by_id.end()) {
                        rec.tag = group_tag(it->second);
                        if (rec.tag != 0) {
                            rec.target = it->second;
                        }
                    }
                }
            } else if (k == "fixr:componentRef") {
                rec.k = child_rec::kind::component;
                if (has_id) {
                    if (auto const it = component_by_id.find(id); it != component_by_id.end()) {
                        rec.target = it->second;
                    }
                }
            }
            out.push_back(rec);
        });
        return out;
    };
    auto const label = [](vnode v) {
        pugi::xml_node const n = v.node();
        std::string_view const k = name_of(n);
        return std::string{k == "fixr:group" ? "group " : "component "} + attr_of(n, "id") + " " +
               attr_of(n, "name");
    };
    auto const visit = [&](vnode root) { return visit_definition(r, root, records_of, label); };

    std::uint64_t total = 0;
    for (auto const& [mt, s] : r.messages) {
        if (s) {
            total = sat_add(total, visit(s).steps);
        }
    }
    std::vector<std::uint64_t> comp_counts;
    for (vnode const c : r.components) {
        def_info const d = visit(c);
        total = sat_add(total, d.steps);
        comp_counts.push_back(d.entries);
    }
    std::vector<std::uint64_t> group_counts;
    for (auto const& [tag, g] : r.first_seen_groups) {
        def_info const d = visit(g);
        total = sat_add(total, d.steps);
        group_counts.push_back(d.entries);
    }
    r.total = total;
    r.components_fit = side_table_fits(comp_counts);
    r.groups_fit = side_table_fits(group_counts);
    return r;
}

// ---- step 7 -----------------------------------------------------------------

void orchestra_overlay_merge::impl::check_conflicts(walk_result const& merged,
                                                    walk_result const& base) const {
    // Vocabulary: `presence` is required, optional or absent on every overlay element.
    {
        std::vector<pugi::xml_node> stack{overlay_root};
        while (!stack.empty()) {
            pugi::xml_node const e = stack.back();
            stack.pop_back();
            if (pugi::xml_attribute const a = e.attribute("presence")) {
                std::string_view const v = a.value();
                if (v != "required" && v != "optional") {
                    conflict("invalid presence value: " + orch_describe(e) + " has presence=\"" +
                             std::string{v} +
                             "\"; an overlay's presence is required, optional or absent");
                }
            }
            for (pugi::xml_node c = e.first_child(); c; c = c.next_sibling()) {
                if (c.type() == pugi::node_element) {
                    stack.push_back(c);
                }
            }
        }
    }
    // Repeated base keys and keys repeated within the overlay.
    if (!deferred_conflicts.empty()) {
        conflict(deferred_conflicts.front());
    }
    auto const changed_attr = [](pugi::xml_node o, pugi::xml_node b, char const* a,
                                 std::string const& base_desc) {
        if (attr_of(o, a) != attr_of(b, a)) {
            conflict("identifying attribute changed: the overlay's " + orch_describe(o) + " has " +
                     a + "=\"" + attr_of(o, a) + "\" where " + base_desc + " has \"" +
                     attr_of(b, a) + "\"");
        }
    };
    // A matched code set keeps the base's id (N-6: the name is the match key) and
    // type; a matched code keeps its name.
    for (auto const& [o, b] : matched_codesets) {
        if (attr_of(o, "id") != attr_of(b, "id")) {
            conflict(
                "code-set name already used by another id: the overlay's <fixr:codeSet "
                "name=\"" +
                attr_of(o, "name") + "\" id=\"" + attr_of(o, "id") +
                "\"> names the base's code set of id \"" + attr_of(b, "id") + "\"");
        }
        changed_attr(o, b, "type", "the base's code set");
    }
    for (auto const& [o, b] : matched_codes) {
        if (attr_of(o, "name") != attr_of(b, "name")) {
            conflict("redescribed code: the overlay's " + orch_describe(o) + " under code set " +
                     attr_of(b.parent(), "name") + " has name=\"" + attr_of(o, "name") +
                     "\" where the base has \"" + attr_of(b, "name") + "\"");
        }
    }
    // A matched field keeps the base's name, type and lengthId.
    for (auto const& [o, b] : matched_fields) {
        for (char const* a : {"name", "type", "lengthId"}) {
            changed_attr(o, b, a, "the base's field " + attr_of(b, "id"));
        }
    }
    // Refused: a new field whose name a base field or another new field already uses.
    {
        std::set<std::string> new_names;
        for (pugi::xml_node const f : new_fields) {
            std::string const n = attr_of(f, "name");
            if (auto const it = base_fields_by_name.find(n); it != base_fields_by_name.end()) {
                if (it->second.size() > 1) {
                    conflict("repeated base key: the overlay's " + orch_describe(f) + " names " +
                             orch_occurrences(it->second) +
                             "; the overlay cannot say which one it extends");
                }
                conflict("field name already used by another number: the overlay's " +
                         orch_describe(f) + " reuses the name of the base's " +
                         orch_describe(it->second.front()));
            }
            if (!new_names.insert(n).second) {
                conflict("two new fields with one name: the overlay declares name \"" + n +
                         "\" twice");
            }
        }
    }
    for (auto const& [o, b] : matched_components) {
        changed_attr(o, b, "name", "the base's component " + attr_of(b, "id"));
    }
    // A matched group keeps its name and its NumInGroup; a new group has exactly one.
    for (auto const& [o, b] : matched_groups) {
        changed_attr(o, b, "name", "the base's group " + attr_of(b, "id"));
        std::string const base_nig = attr_of(b.child("fixr:numInGroup"), "id");
        auto const it = group_numingroups.find(key_of(o));
        std::vector<pugi::xml_node> const none;
        auto const& nigs = it == group_numingroups.end() ? none : it->second;
        if (nigs.size() != 1 || attr_of(nigs.front(), "id") != base_nig) {
            conflict("group numInGroup differs: the overlay's " + orch_describe(o) + " carries " +
                     (nigs.empty()
                          ? std::string{"no <fixr:numInGroup>"}
                          : std::to_string(nigs.size()) + " <fixr:numInGroup>, the first id=\"" +
                                attr_of(nigs.front(), "id") + "\"") +
                     " where the base's has id=\"" + base_nig + "\"");
        }
    }
    for (pugi::xml_node const g : new_groups) {
        std::size_t n = 0;
        for (pugi::xml_node c = g.child("fixr:numInGroup"); c;
             c = c.next_sibling("fixr:numInGroup")) {
            ++n;
        }
        if (n != 1) {
            conflict("new group without exactly one numInGroup: the overlay's " + orch_describe(g) +
                     " has " + std::to_string(n) + " <fixr:numInGroup>");
        }
    }
    // A matched message keeps its id and name.
    for (auto const& [o, b] : matched_messages) {
        for (char const* a : {"id", "name"}) {
            changed_attr(o, b, a, "the base's");
        }
    }
    // The overlay cannot relax a Required child.
    for (auto const& [o, b] : matched_children) {
        if (attr_of(o, "presence") == "optional" && attr_of(b, "presence") == "required") {
            conflict("required relaxed: the overlay's " + orch_describe(o) + " under " +
                     orch_describe(b.parent().name() == std::string_view{"fixr:structure"}
                                       ? b.parent().parent()
                                       : b.parent()) +
                     " has presence=\"optional\" where the base has \"required\"");
        }
    }

    // One tag twice at one level (research R-3), as in the QuickFIX-XML check; a
    // message's level is its structure's.
    std::unordered_set<node_ptr> changed;
    for (auto const& [b, os] : merged_into) {
        for (pugi::xml_node const o : os) {
            if (!appended_of(o).empty()) {
                changed.insert(b);
            }
        }
    }
    for (vnode const v : merged.visit_order) {
        if (!v.base) {
            changed.insert(v.key());
        }
    }
    close_upward(merged.children, changed);
    std::unordered_map<node_ptr, level_set> merged_levels;
    std::unordered_map<node_ptr, level_set> base_levels;
    auto const refuse_twice = [](std::uint16_t tag, std::string const& where) {
        conflict("tag occurs twice at one level: tag " + std::to_string(tag) +
                 " occurs twice at the level of " + where + ", where the base has it at most once");
    };
    static level_set const kNoBaseLevel;
    for (vnode const v : merged.visit_order) {
        if (!changed.contains(v.key())) {
            continue;
        }
        pugi::xml_node const n = v.node();
        level_set const& ml = level_of(merged.children, merged_levels, v.key());
        level_set const& bl = v.base ? level_of(base.children, base_levels, v.key()) : kNoBaseLevel;
        if (auto const t = twice(ml, bl)) {
            refuse_twice(*t, orch_describe(name_of(n) == "fixr:structure" ? n.parent() : n));
        }
    }
}

// ---- step 8 -----------------------------------------------------------------

void orchestra_overlay_merge::impl::splice() {
    auto const keep = [](pugi::xml_node c) { return !orch_dropped(c); };
    for (pugi::xml_node oc = overlay_root.first_child(); oc; oc = oc.next_sibling()) {
        if (oc.type() != pugi::node_element) {
            continue;
        }
        pugi::xml_node const bc = base_root.child(oc.name());
        if (!bc) {
            copy_element_if(base_root, oc, keep);  // a container the base lacks
            continue;
        }
        // Each matched element: its promotion, then its appended children.
        std::vector<std::pair<pugi::xml_node, pugi::xml_node>> stack{{oc, bc}};
        while (!stack.empty()) {
            auto [o, b] = stack.back();
            stack.pop_back();
            if (attr_of(o, "presence") == "required" && attr_of(b, "presence") != "required") {
                pugi::xml_attribute a = b.attribute("presence");
                if (!a) {
                    a = b.append_attribute("presence");
                }
                if (!a || !a.set_value("required")) {
                    throw std::bad_alloc();
                }
            }
            for (pugi::xml_node const c : appended_of(o)) {
                copy_element_if(b, c, keep);
            }
            for (pugi::xml_node c = o.last_child(); c; c = c.previous_sibling()) {
                if (auto const it = match_of.find(key_of(c)); it != match_of.end()) {
                    stack.emplace_back(c, it->second);
                }
            }
        }
    }
}

// ── The public half ──────────────────────────────────────────────────────────

orchestra_overlay_merge::orchestra_overlay_merge(pugi::xml_document& base,
                                                 pugi::xml_document const& overlay)
    : impl_(std::make_unique<impl>(base, overlay)) {}

orchestra_overlay_merge::~orchestra_overlay_merge() = default;

std::uint64_t orchestra_overlay_merge::guard(overlay_trace& trace) {
    // No policy parameter: the counts step 6 models do not depend on
    // `unresolved_group_policy` (see the Orchestra section's header).
    impl& m = *impl_;
    check_no_doctype(m.overlay_doc);  // step 3
    check_depth(m.overlay_doc);       // step 4
    m.check_root();                   // step 4a
    m.classify();
    m.check_caps();  // step 5

    // Step 6: the merged-graph walk, then the base's own, then its refusals.
    auto const merged = m.walk(/*merged=*/true);
    auto const base = m.walk(/*merged=*/false);
    guard_outcome g;
    g.base_total = base.total;
    g.merged_total = merged.total;
    g.arm_limit = std::min(merged.total, sat_add(base.total, kExpansionBudget));
    g.base_components_fit = base.components_fit;
    g.base_groups_fit = base.groups_fit;
    g.merged_components_fit = merged.components_fit;
    g.merged_groups_fit = merged.groups_fit;
    trace.guards = g;
    if (merged.cycle) {
        throw orchestra_parse_error("dict::orchestra_parse_error: component reference cycle: " +
                                    *merged.cycle);
    }
    if (sat_sub(merged.total, base.total) > kExpansionBudget) {
        trace.origin = refusal_origin::budget;
        limit("the overlay adds more expansion steps than the expansion budget of " +
              std::to_string(kExpansionBudget));
    }
    if (base.components_fit && !merged.components_fit) {
        trace.origin = refusal_origin::side_table;
        limit(
            "the components side table would stop fitting 16 bits (a start or a count "
            "above 65535)");
    }
    if (base.groups_fit && !merged.groups_fit) {
        trace.origin = refusal_origin::side_table;
        limit(
            "the groups side table would stop fitting 16 bits (a start or a count above "
            "65535)");
    }

    m.check_conflicts(merged, base);  // step 7
    return g.arm_limit;
}

void orchestra_overlay_merge::splice() { impl_->splice(); }

}  // namespace fixpp::dict::detail::overlay
