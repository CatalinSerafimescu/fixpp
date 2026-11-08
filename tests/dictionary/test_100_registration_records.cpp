// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/test_100_registration_records.cpp — 100-wire-group-read-integrity, task
// T028 (US1; FR-001, FR-009; research D2, D9 "Context-key bitset" and "D2 regression sweep"):
// the registration predicate's stores and records, read through `table_view` and through the
// wire hooks `dict_hooks::for_table_view` installs.
//
// The oracle is an independent walk of each dictionary's own XML or Orchestra source with
// pugixml, the census scripts' walk: per occurrence, the direct members at the occurrence's own
// level, components inline, a nested group's count tag included and its members excluded; the
// first emitted tag is the delimiter. It never reads the loader's capture pool,
// `message_fields` or `immediate_parent`. Occurrences are enumerated per declared message from
// the message's own expansion (header, body, trailer for QuickFIX XML; the message's
// `fixr:structure` for Orchestra), and per dictionary from the header and trailer alone.
//
// Keys research D9 has the sweep report rather than assert are printed with the prefix
// `T028-REPORTED`. A sweep run with `shipped` set fails on any reported key: that is research
// D9's criterion (a reported key fails where T005's census did not predict it) applied to
// the shipped dictionaries, whose prediction tasks T005 re-derives. The divergent-definition
// dictionaries (the `seed_div.xml` shape of tests/capi/test_100_section_divergent.cpp) run
// without it.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/orchestra_loader.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/wire/parser.hpp>  // dict_hooks::for_table_view, group_context
#include <iostream>
#include <map>
#include <memory_resource>
#include <optional>
#include <pugixml.hpp>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "dictionary_internal.hpp"  // detail::SectionWalk
#include "support/divergent_dicts_100.hpp"
#include "support/table_view_test_access.hpp"

namespace {

using fixpp::dict::table_view;
using fixpp::dict::table_view_builder;
using fixpp::dict::table_view_test_access;
using tag_t = std::uint16_t;
using path_t = std::vector<tag_t>;

// ── Oracle ──────────────────────────────────────────────────────────────────

struct definition {
    tag_t delim = 0;
    std::vector<tag_t> members;  // emission order, no repeats
    [[nodiscard]] std::set<tag_t> member_set() const { return {members.begin(), members.end()}; }
    friend bool operator==(definition const& a, definition const& b) {
        return a.delim == b.delim && a.member_set() == b.member_set();
    }
};

struct occurrence {
    path_t path;  // enclosing count tags, outermost first
    tag_t no_tag = 0;
    definition def;
    char section = 'B';  // 'H' header, 'B' body, 'T' trailer
};

struct oracle {
    std::vector<std::string> msg_types;
    std::map<std::string, std::vector<occurrence>> per_msg;
    std::vector<occurrence> sections;  // header walk, then trailer walk, declaration order
    std::set<tag_t> all_count_tags;
};

void add_member(definition& d, tag_t t) {
    if (std::ranges::find(d.members, t) == d.members.end()) {
        d.members.push_back(t);
    }
    if (d.delim == 0) {
        d.delim = t;
    }
}

// QuickFIX XML.
struct xml_source {
    pugi::xml_document doc;
    std::map<std::string, tag_t> tag_by_name;
    std::map<std::string, pugi::xml_node> components;
    pugi::xml_node header, trailer;

    [[nodiscard]] std::optional<tag_t> tag_of(pugi::xml_node n) const {
        auto const it = tag_by_name.find(n.attribute("name").as_string(""));
        return it == tag_by_name.end() ? std::nullopt : std::optional<tag_t>{it->second};
    }
    void walk_def(pugi::xml_node node, definition& d, std::vector<std::string>& stack) const {
        for (auto const& ch : node.children()) {
            std::string_view const kind{ch.name()};
            if (kind == "field" || kind == "group") {
                if (auto const t = tag_of(ch)) {
                    add_member(d, *t);
                }
            } else if (kind == "component") {
                std::string const nm = ch.attribute("name").as_string("");
                auto const it = components.find(nm);
                if (it == components.end() || std::ranges::find(stack, nm) != stack.end()) {
                    continue;
                }
                stack.push_back(nm);
                walk_def(it->second, d, stack);
                stack.pop_back();
            }
        }
    }
    void walk_occ(pugi::xml_node node, path_t& path, char section, std::vector<occurrence>& out,
                  std::vector<std::string>& stack) const {
        for (auto const& ch : node.children()) {
            std::string_view const kind{ch.name()};
            if (kind == "group") {
                auto const t = tag_of(ch);
                if (!t) {
                    continue;
                }
                occurrence o{.path = path, .no_tag = *t, .def = {}, .section = section};
                std::vector<std::string> def_stack;
                walk_def(ch, o.def, def_stack);
                out.push_back(o);
                path.push_back(*t);
                walk_occ(ch, path, section, out, stack);
                path.pop_back();
            } else if (kind == "component") {
                std::string const nm = ch.attribute("name").as_string("");
                auto const it = components.find(nm);
                if (it == components.end() || std::ranges::find(stack, nm) != stack.end()) {
                    continue;
                }
                stack.push_back(nm);
                walk_occ(it->second, path, section, out, stack);
                stack.pop_back();
            }
        }
    }
};

oracle xml_oracle(std::string const& text) {
    xml_source src;
    EXPECT_TRUE(src.doc.load_string(text.c_str())) << "oracle: XML parse";
    auto const root = src.doc.child("fix");
    for (auto const& f : root.child("fields").children("field")) {
        src.tag_by_name[f.attribute("name").as_string("")] =
            static_cast<tag_t>(f.attribute("number").as_uint());
    }
    for (auto const& c : root.child("components").children("component")) {
        src.components[c.attribute("name").as_string("")] = c;
    }
    src.header = root.child("header");
    src.trailer = root.child("trailer");
    oracle o;
    auto const walk = [&](pugi::xml_node n, char section, std::vector<occurrence>& out) {
        if (!n) {
            return;
        }
        path_t path;
        std::vector<std::string> stack;
        src.walk_occ(n, path, section, out, stack);
    };
    for (auto const& m : root.child("messages").children("message")) {
        std::string const mt = m.attribute("msgtype").as_string("");
        o.msg_types.push_back(mt);
        auto& occs = o.per_msg[mt];
        walk(src.header, 'H', occs);
        walk(m, 'B', occs);
        walk(src.trailer, 'T', occs);
    }
    walk(src.header, 'H', o.sections);
    walk(src.trailer, 'T', o.sections);
    for (auto const& [mt, occs] : o.per_msg) {
        for (auto const& oc : occs) {
            o.all_count_tags.insert(oc.no_tag);
        }
    }
    for (auto const& oc : o.sections) {
        o.all_count_tags.insert(oc.no_tag);
    }
    return o;
}

// Orchestra.
struct orch_source {
    pugi::xml_document doc;
    std::set<tag_t> fields;
    std::map<unsigned, pugi::xml_node> components;
    std::map<unsigned, pugi::xml_node> groups;

    [[nodiscard]] std::optional<tag_t> group_no_tag(pugi::xml_node group_ref) const {
        auto const it = groups.find(group_ref.attribute("id").as_uint());
        if (it == groups.end()) {
            return std::nullopt;
        }
        auto const n =
            static_cast<tag_t>(it->second.child("fixr:numInGroup").attribute("id").as_uint());
        return fields.contains(n) ? std::optional<tag_t>{n} : std::nullopt;
    }
    void walk_def(pugi::xml_node node, definition& d, std::vector<unsigned>& stack) const {
        for (auto const& ch : node.children()) {
            std::string_view const kind{ch.name()};
            if (kind == "fixr:fieldRef") {
                auto const t = static_cast<tag_t>(ch.attribute("id").as_uint());
                if (fields.contains(t)) {
                    add_member(d, t);
                }
            } else if (kind == "fixr:groupRef") {
                if (auto const t = group_no_tag(ch)) {
                    add_member(d, *t);
                }
            } else if (kind == "fixr:componentRef") {
                unsigned const id = ch.attribute("id").as_uint();
                auto const it = components.find(id);
                if (it == components.end() || std::ranges::find(stack, id) != stack.end()) {
                    continue;
                }
                stack.push_back(id);
                walk_def(it->second, d, stack);
                stack.pop_back();
            }
        }
    }
    void walk_occ(pugi::xml_node node, path_t& path, char section, std::vector<occurrence>& out,
                  std::vector<unsigned>& stack) const {
        for (auto const& ch : node.children()) {
            std::string_view const kind{ch.name()};
            if (kind == "fixr:groupRef") {
                auto const t = group_no_tag(ch);
                if (!t) {
                    continue;
                }
                unsigned const gid = 0x10000U + ch.attribute("id").as_uint();
                if (std::ranges::find(stack, gid) != stack.end()) {
                    continue;
                }
                auto const gnode = groups.at(ch.attribute("id").as_uint());
                occurrence o{.path = path, .no_tag = *t, .def = {}, .section = section};
                std::vector<unsigned> def_stack;
                walk_def(gnode, o.def, def_stack);
                out.push_back(o);
                path.push_back(*t);
                stack.push_back(gid);
                walk_occ(gnode, path, section, out, stack);
                stack.pop_back();
                path.pop_back();
            } else if (kind == "fixr:componentRef") {
                unsigned const id = ch.attribute("id").as_uint();
                auto const it = components.find(id);
                if (it == components.end() || std::ranges::find(stack, id) != stack.end()) {
                    continue;
                }
                // Inside a message's structure, the header and trailer components set the
                // occurrence's section.
                char const sec = id == 1024U ? 'H' : id == 1025U ? 'T' : section;
                stack.push_back(id);
                walk_occ(it->second, path, sec, out, stack);
                stack.pop_back();
            }
        }
    }
};

oracle orch_oracle(std::string const& text) {
    orch_source src;
    EXPECT_TRUE(src.doc.load_string(text.c_str())) << "oracle: Orchestra parse";
    auto const root = src.doc.child("fixr:repository");
    for (auto const& f : root.child("fixr:fields").children("fixr:field")) {
        src.fields.insert(static_cast<tag_t>(f.attribute("id").as_uint()));
    }
    for (auto const& c : root.child("fixr:components").children("fixr:component")) {
        src.components[c.attribute("id").as_uint()] = c;
    }
    for (auto const& g : root.child("fixr:groups").children("fixr:group")) {
        src.groups[g.attribute("id").as_uint()] = g;
    }
    oracle o;
    for (auto const& m : root.child("fixr:messages").children("fixr:message")) {
        std::string const mt = m.attribute("msgType").as_string("");
        o.msg_types.push_back(mt);
        path_t path;
        std::vector<unsigned> stack;
        src.walk_occ(m.child("fixr:structure"), path, 'B', o.per_msg[mt], stack);
    }
    for (unsigned const id : {1024U, 1025U}) {
        auto const it = src.components.find(id);
        if (it == src.components.end()) {
            continue;
        }
        path_t path;
        std::vector<unsigned> stack{id};
        src.walk_occ(it->second, path, id == 1024U ? 'H' : 'T', o.sections, stack);
    }
    for (auto const& [mt, occs] : o.per_msg) {
        for (auto const& oc : occs) {
            o.all_count_tags.insert(oc.no_tag);
        }
    }
    for (auto const& oc : o.sections) {
        o.all_count_tags.insert(oc.no_tag);
    }
    return o;
}

// ── Reads through table_view and the wire hooks ─────────────────────────────

fixpp::wire::group_context ctx_of(std::string_view mt, path_t const& path) {
    fixpp::wire::group_context c{.msg_type = mt};
    for (auto const t : path) {
        c = c.pushed(t);
    }
    return c;
}

using key_t = std::pair<path_t, tag_t>;

std::vector<key_t> store_keys(table_view_test_access::record_store const& store,
                              std::string_view mt) {
    std::vector<key_t> out;
    for (auto const& [k, rec] : store) {
        if (k.msg_type == mt) {
            out.emplace_back(path_t(k.parent_path.begin(), k.parent_path.begin() + k.depth),
                             k.no_tag);
        }
    }
    std::ranges::sort(out);
    return out;
}

std::optional<definition> stored_record(table_view_test_access::record_store const& store,
                                        std::string_view mt, path_t const& path, tag_t no_tag) {
    auto const it = store.find(fixpp::dict::make_ctx_query(mt, path, no_tag));
    if (it == store.end()) {
        return std::nullopt;
    }
    return definition{.delim = it->second.delimiter, .members = it->second.members};
}

// The definition the wire hooks answer at `(mt, path, no_tag)` equals `want`: the delimiter
// callback's answer, and the member set the membership callback accepts over `universe`.
void expect_wire_definition(fixpp::wire::dict_hooks const& h, std::string_view mt,
                            path_t const& path, tag_t no_tag, definition const& want,
                            std::set<tag_t> const& universe, std::string const& what,
                            std::size_t& mismatches) {
    auto const ctx = ctx_of(mt, path);
    auto const delim = h.group_delim_fn()(h.opaque_dict(), ctx, no_tag);
    bool bad = delim != want.delim;
    std::set<tag_t> accepted;
    for (auto const t : universe) {
        if (h.group_member_fn()(h.opaque_dict(), ctx, no_tag, t)) {
            accepted.insert(t);
        }
    }
    bad = bad || accepted != want.member_set();
    if (bad && ++mismatches <= 5) {
        ADD_FAILURE() << what << ": the wire hooks answer delimiter " << delim << " and "
                      << accepted.size() << " member(s); the source declares delimiter "
                      << want.delim << " and " << want.members.size() << " member(s)";
    }
}

// T071 fix (one lookup per read): the resolver `for_table_view` installs answers what the
// three lookups it replaces answer at the same key: `group_registered`,
// `wire_group_first_field` and `wire_group_member_tags`, and it names the definition.
void expect_resolve_is_the_three_lookups(table_view const& tv,
                                         fixpp::wire::dict_hooks const& hooks,
                                         std::string_view mt, path_t const& path, tag_t no_tag,
                                         std::string const& what, std::size_t& mismatches) {
    auto const r = hooks.group_resolve_fn()(hooks.opaque_dict(), ctx_of(mt, path), no_tag);
    auto const members = tv.wire_group_member_tags(mt, path, no_tag);
    bool const same = r.definition_known && r.registered == tv.group_registered(mt, path, no_tag) &&
                      r.delimiter == tv.wire_group_first_field(mt, path, no_tag) &&
                      std::ranges::equal(r.members, members);
    if (!same && ++mismatches <= 5) {
        ADD_FAILURE() << what << ": the resolver answers registered=" << r.registered
                      << " delimiter=" << r.delimiter << " members=" << r.members.size()
                      << " known=" << r.definition_known
                      << "; the three lookups answer registered="
                      << tv.group_registered(mt, path, no_tag)
                      << " delimiter=" << tv.wire_group_first_field(mt, path, no_tag)
                      << " members=" << members.size();
    }
}

void report(std::string const& dict_name, std::string const& what, std::string_view mt,
            path_t const& path, tag_t no_tag) {
    std::cout << "T028-REPORTED " << dict_name << " " << what << " msg=" << mt << " path=[";
    for (auto const t : path) {
        std::cout << t << ' ';
    }
    std::cout << "] no_tag=" << no_tag << "\n";
}

// research D9 "D2 regression sweep", all four bullets, over one table built from one
// dictionary whose source `o` re-derives. `shipped`: a reported key fails.
void sweep(std::string const& name, table_view const& tv, oracle const& o, bool route_table,
           bool shipped) {
    auto const hooks = fixpp::wire::dict_hooks::for_table_view(tv);
    ASSERT_NE(hooks.group_resolve_fn(), nullptr) << name << ": for_table_view installs it";
    auto const& occ_store = table_view_test_access::occurrence_records(tv);
    auto const& sec_store = table_view_test_access::section_records(tv);
    std::size_t mismatches = 0;
    std::size_t reported = 0;

    // Every definition of each count tag the source declares, for the membership universe.
    std::map<tag_t, std::set<tag_t>> any_member_of;
    auto const note = [&](occurrence const& oc) {
        any_member_of[oc.no_tag].insert(oc.def.members.begin(), oc.def.members.end());
    };
    for (auto const& [mt, occs] : o.per_msg) {
        std::ranges::for_each(occs, note);
    }
    std::ranges::for_each(o.sections, note);
    auto const universe = [&](std::string_view mt, path_t const& path, tag_t no_tag,
                              definition const& want) {
        std::set<tag_t> u = any_member_of[no_tag];
        u.insert(want.members.begin(), want.members.end());
        for (auto const t : tv.group_member_tags(no_tag)) {
            u.insert(t);
        }
        for (auto const t : tv.group_member_tags(mt, path, no_tag)) {
            u.insert(t);
        }
        return u;
    };

    // Bullets 1 and 3 (and the arm-1 store's key set), per declared message.
    for (auto const& mt : o.msg_types) {
        auto const& occs = o.per_msg.at(mt);
        std::map<key_t, std::vector<definition>> defs_at;
        std::map<tag_t, std::set<path_t>> paths_of;
        for (auto const& oc : occs) {
            auto& v = defs_at[{oc.path, oc.no_tag}];
            if (std::ranges::find(v, oc.def) == v.end()) {
                v.push_back(oc.def);
            }
            paths_of[oc.no_tag].insert(oc.path);
        }
        std::vector<key_t> want_keys;
        for (auto const& [k, defs] : defs_at) {
            want_keys.push_back(k);
        }
        // A count tag this message reaches under two parents: the parent whose
        // `group_ctx_` members lost the nested count tag to the other (research D2 "What it
        // does not fix", first bullet) is a declared message's second parent.
        std::set<key_t> second_parent;
        for (auto const& [no_tag, paths] : paths_of) {
            if (paths.size() < 2) {
                continue;
            }
            for (auto const& path : paths) {
                if (path.empty()) {
                    continue;
                }
                path_t const outer(path.begin(), path.end() - 1);
                auto const pm = tv.group_member_tags(mt, outer, path.back());
                if (std::ranges::find(pm, no_tag) == pm.end()) {
                    second_parent.insert({outer, path.back()});
                    second_parent.insert({path, no_tag});
                }
            }
        }
        EXPECT_EQ(store_keys(occ_store, mt), want_keys)
            << name << " msg " << mt
            << ": the per-occurrence store holds one key per occurrence the expansion reaches";

        for (auto const& [k, defs] : defs_at) {
            auto const& [path, no_tag] = k;
            expect_resolve_is_the_three_lookups(
                tv, hooks, mt, path, no_tag,
                name + " msg " + mt + " no_tag " + std::to_string(no_tag), mismatches);
            // Record shape: the stored record is one of the definitions at its key.
            auto const rec = stored_record(occ_store, mt, path, no_tag);
            EXPECT_TRUE(rec && std::ranges::find(defs, *rec) != defs.end())
                << name << " msg " << mt << " no_tag " << no_tag
                << ": the per-occurrence record is the occurrence's own definition";

            bool const two_defs = defs.size() > 1;
            bool multipath_reported = false;
            if (paths_of[no_tag].size() > 1) {
                bool const ctx_key = tv.group_first_field_exact(mt, path, no_tag).has_value() &&
                                     tv.group_first_field_exact(mt, path, no_tag) != 0;
                bool same_def = true;
                for (auto const& other : occs) {
                    if (other.no_tag == no_tag && !(other.def == defs.front())) {
                        same_def = false;
                    }
                }
                multipath_reported = ctx_key && !same_def;
            }
            bool const second = second_parent.contains(k);
            if (two_defs || multipath_reported || second) {
                ++reported;
                report(name,
                       two_defs             ? "two-definitions-at-one-key"
                       : multipath_reported ? "union-group-ctx-key"
                                            : "second-parent",
                       mt, path, no_tag);
                continue;
            }
            EXPECT_TRUE(tv.group_registered(mt, path, no_tag))
                << name << " msg " << mt << " no_tag " << no_tag
                << ": every occurrence is registered under its own parent path";
            expect_wire_definition(
                hooks, mt, path, no_tag, defs.front(), universe(mt, path, no_tag, defs.front()),
                name + " msg " + mt + " no_tag " + std::to_string(no_tag), mismatches);
        }
    }

    // Bullet 4: the bare-only arm fires only where the survey names it.
    std::set<tag_t> arm3;
    for (unsigned t = 1; t <= 0xFFFFU; ++t) {
        auto const tag = static_cast<tag_t>(t);
        if (table_view_test_access::group_bit(tv, tag) &&
            !table_view_test_access::ctx_key_bit(tv, tag)) {
            arm3.insert(tag);
        }
    }
    bool const header_declares_nohops =
        std::ranges::any_of(o.sections, [](occurrence const& s) { return s.no_tag == 627; });
    std::set<tag_t> const want_arm3 =
        (route_table && !header_declares_nohops) ? std::set<tag_t>{627} : std::set<tag_t>{};
    EXPECT_EQ(arm3, want_arm3) << name
                               << ": the bare-only arm fires only for 099's NoHops on a route "
                                  "table whose header does not declare it";

    // Bullet 4: the section store equals the header and trailer declarations, every depth.
    std::map<key_t, std::vector<definition>> sec_defs;
    for (auto const& s : o.sections) {
        auto& v = sec_defs[{s.path, s.no_tag}];
        if (std::ranges::find(v, s.def) == v.end()) {
            v.push_back(s.def);
        }
    }
    std::vector<key_t> want_sec;
    for (auto const& [k, defs] : sec_defs) {
        want_sec.push_back(k);
    }
    EXPECT_EQ(store_keys(sec_store, ""), want_sec)
        << name << ": the section store holds one key per header or trailer group occurrence";
    for (auto const& [k, defs] : sec_defs) {
        auto const rec = stored_record(sec_store, "", k.first, k.second);
        EXPECT_TRUE(rec && *rec == defs.front())
            << name << " section no_tag " << k.second
            << ": the section record is the first walked definition at its key";
    }

    // Every count tag holding a record has its group bit set (research D2 "Lookup order").
    for (auto const* store : {&occ_store, &sec_store}) {
        for (auto const& [key, rec] : *store) {
            EXPECT_TRUE(table_view_test_access::group_bit(tv, key.no_tag))
                << name << " no_tag " << key.no_tag
                << ": a record whose count tag has a clear group_bit is hidden by the pre-filter";
        }
    }

    // Bullet 2: one synthetic undeclared MsgType.
    std::string const undeclared = "ZZ100";
    ASSERT_EQ(std::ranges::find(o.msg_types, undeclared), o.msg_types.end());
    for (auto const& [k, defs] : sec_defs) {
        auto const& [path, no_tag] = k;
        if (defs.size() > 1) {
            ++reported;
            report(name, "two-definitions-at-one-section-key", undeclared, path, no_tag);
            continue;
        }
        EXPECT_TRUE(tv.group_registered(undeclared, path, no_tag))
            << name << " section no_tag " << no_tag
            << ": a header or trailer group is registered in an undeclared type";
        expect_wire_definition(hooks, undeclared, path, no_tag, defs.front(),
                               universe(undeclared, path, no_tag, defs.front()),
                               name + " section no_tag " + std::to_string(no_tag), mismatches);
    }
    std::set<key_t> body_keys;
    for (auto const& [mt, occs] : o.per_msg) {
        for (auto const& oc : occs) {
            if (oc.section == 'B' && !sec_defs.contains({oc.path, oc.no_tag})) {
                body_keys.insert({oc.path, oc.no_tag});
            }
        }
    }
    for (auto const& [k, defs] : sec_defs) {
        expect_resolve_is_the_three_lookups(tv, hooks, undeclared, k.first, k.second,
                                            name + " section no_tag " + std::to_string(k.second),
                                            mismatches);
    }
    // Every tag at the root of an undeclared type: count tags, bare-only tags and tags that
    // are no group's.
    for (unsigned t = 1; t <= 0xFFFFU; ++t) {
        expect_resolve_is_the_three_lookups(tv, hooks, undeclared, {}, static_cast<tag_t>(t),
                                            name + " root tag " + std::to_string(t), mismatches);
    }
    for (auto const& [path, no_tag] : body_keys) {
        expect_resolve_is_the_three_lookups(tv, hooks, undeclared, path, no_tag,
                                            name + " body no_tag " + std::to_string(no_tag),
                                            mismatches);
        EXPECT_EQ(tv.group_registered(undeclared, path, no_tag), arm3.contains(no_tag))
            << name << " body no_tag " << no_tag
            << ": a body group is not registered in an undeclared type unless bare-only";
    }

    EXPECT_EQ(mismatches, 0U) << name << ": wire definitions that differ from the source";
    if (shipped) {
        EXPECT_EQ(reported, 0U) << name
                                << ": a reported key on a shipped dictionary, which T005's "
                                   "census did not predict (research D9's failure criterion)";
    }
}

std::string read_file(std::string const& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) {
        ADD_FAILURE() << "setup: open " << path;
        return {};
    }
    std::string s;
    char buf[65536];
    for (std::size_t n = 0; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) {
        s.append(buf, n);
    }
    std::fclose(f);
    return s;
}

void sweep_xml_file(char const* file) {
    std::string const path = std::string{FIXPP_DICT_DATA_DIR} + "/" + file;
    auto const text = read_file(path);
    auto const o = xml_oracle(text);
    ASSERT_FALSE(o.msg_types.empty()) << file << ": the oracle found the messages";
    auto const d = fixpp::dict::XmlLoader{}.load(path, std::pmr::get_default_resource());
    sweep(file, d.as_table_view(), o, false, true);
    sweep(std::string{file} + " (route table)", d.as_table_view(fixpp::dict::fixt_framing), o, true,
          true);
}

TEST(RegistrationRecords100, Sweep_FIX40) { sweep_xml_file("FIX40.xml"); }
TEST(RegistrationRecords100, Sweep_FIX41) { sweep_xml_file("FIX41.xml"); }
TEST(RegistrationRecords100, Sweep_FIX42) { sweep_xml_file("FIX42.xml"); }
TEST(RegistrationRecords100, Sweep_FIX43) { sweep_xml_file("FIX43.xml"); }
TEST(RegistrationRecords100, Sweep_FIX44) { sweep_xml_file("FIX44.xml"); }
TEST(RegistrationRecords100, Sweep_FIX50) { sweep_xml_file("FIX50.xml"); }
TEST(RegistrationRecords100, Sweep_FIX50SP1) { sweep_xml_file("FIX50SP1.xml"); }
TEST(RegistrationRecords100, Sweep_FIX50SP2) { sweep_xml_file("FIX50SP2.xml"); }
TEST(RegistrationRecords100, Sweep_FIXT11) { sweep_xml_file("FIXT11.xml"); }
TEST(RegistrationRecords100, Sweep_OrchestraFIXLatest) {
    std::string const path = std::string{FIXPP_ORCHESTRA_DATA_DIR} + "/OrchestraFIXLatest.xml";
    auto const text = read_file(path);
    auto const o = orch_oracle(text);
    ASSERT_FALSE(o.msg_types.empty()) << "the oracle found the messages";
    auto const d = fixpp::dict::OrchestraLoader{}.load(path, std::pmr::get_default_resource());
    sweep("OrchestraFIXLatest.xml", d.as_table_view(), o, false, true);
    sweep("OrchestraFIXLatest.xml (route table)", d.as_table_view(fixpp::dict::fixt_framing), o,
          true, true);
}

// ── The divergent-definition dictionaries (the seeded input of the definition check) ─────
// research D9's (A), (B) and (C), per loader: tests/support/divergent_dicts_100.hpp.
using fixpp::test100::div::div_a_orch;
using fixpp::test100::div::div_a_xml;
using fixpp::test100::div::div_b_orch;
using fixpp::test100::div::div_b_xml;
using fixpp::test100::div::div_c_orch;
using fixpp::test100::div::div_c_xml;

void sweep_divergent_xml(char const* shape, std::string const& text) {
    auto const d =
        fixpp::dict::XmlLoader{}.load_from_string(text, std::pmr::get_default_resource());
    sweep(std::string{"divergent "} + shape + " XML", d.as_table_view(), xml_oracle(text), false,
          false);
}
void sweep_divergent_orch(char const* shape, std::string const& text) {
    auto const d =
        fixpp::dict::OrchestraLoader{}.load_from_string(text, std::pmr::get_default_resource());
    sweep(std::string{"divergent "} + shape + " Orchestra", d.as_table_view(), orch_oracle(text),
          false, false);
}
TEST(RegistrationRecords100, Sweep_DivergentA_Xml) { sweep_divergent_xml("(A)", div_a_xml()); }
TEST(RegistrationRecords100, Sweep_DivergentB_Xml) { sweep_divergent_xml("(B)", div_b_xml()); }
TEST(RegistrationRecords100, Sweep_DivergentC_Xml) { sweep_divergent_xml("(C)", div_c_xml()); }
TEST(RegistrationRecords100, Sweep_DivergentA_Orchestra) {
    sweep_divergent_orch("(A)", div_a_orch());
}
TEST(RegistrationRecords100, Sweep_DivergentB_Orchestra) {
    sweep_divergent_orch("(B)", div_b_orch());
}
TEST(RegistrationRecords100, Sweep_DivergentC_Orchestra) {
    sweep_divergent_orch("(C)", div_c_orch());
}

// ── research D9 "Context-key bitset": one fixture per `_ctx` setter ─────────────────────

constexpr tag_t kNo = 700;
constexpr tag_t kMember = 701;
std::array<tag_t, 0> const kRoot{};

void expect_registered_in_one_context_only(table_view const& tv, char const* setter) {
    EXPECT_TRUE(tv.group_registered("A", kRoot, kNo))
        << setter << ": registered in the context it was registered in";
    EXPECT_FALSE(tv.group_registered("B", kRoot, kNo))
        << setter << ": not registered under a second context (arm 3 must not fire)";
}
TEST(RegistrationRecords100, CtxKeyBit_AddGroupMemberCtx) {
    table_view_builder b;
    b.add_group_member_ctx("A", kRoot, kNo, kMember);
    expect_registered_in_one_context_only(std::move(b).build(), "add_group_member_ctx");
}
TEST(RegistrationRecords100, CtxKeyBit_AddGroupRequiredMemberCtx) {
    table_view_builder b;
    b.add_group_required_member_ctx("A", kRoot, kNo, kMember);
    expect_registered_in_one_context_only(std::move(b).build(), "add_group_required_member_ctx");
}
TEST(RegistrationRecords100, CtxKeyBit_SetGroupFirstCtx) {
    table_view_builder b;
    b.set_group_first_ctx("A", kRoot, kNo, kMember);
    expect_registered_in_one_context_only(std::move(b).build(), "set_group_first_ctx");
}

// ── T070 (`[const §IX.1]`): the record stores' edge arms ───────────────────────────────

constexpr tag_t kDelimA = 701;
constexpr tag_t kDelimB = 703;

[[nodiscard]] std::vector<tag_t> as_vec(std::span<tag_t const> s) { return {s.begin(), s.end()}; }

// `wire_group_first_field` / `wire_group_member_tags` answer nothing for a tag whose
// `group_bit` is clear, even when a record holds it; with the bit set the record answers.
TEST(RegistrationRecords100, WireLookup_RecordWithoutGroupBit_AnswersNothing) {
    table_view_builder b;
    b.add_occurrence_group("A", kRoot, kNo,
                           fixpp::dict::group_record{.delimiter = kDelimA, .members = {kDelimA}});
    auto const tv = std::move(b).build();
    ASSERT_FALSE(table_view_test_access::group_bit(tv, kNo)) << "precondition: bit clear";
    EXPECT_EQ(tv.wire_group_first_field("A", kRoot, kNo), 0U);
    EXPECT_TRUE(tv.wire_group_member_tags("A", kRoot, kNo).empty());
    // T071 fix: the resolver registers the key and names no members, as the three do.
    auto const r = tv.wire_group_resolve("A", kRoot, kNo);
    EXPECT_TRUE(r.registered);
    EXPECT_EQ(r.delimiter, 0U);
    EXPECT_TRUE(r.members.empty());
}
TEST(RegistrationRecords100, WireLookup_RecordWithGroupBit_AnswersTheRecord) {
    table_view_builder b;
    b.set_group_first(kNo, kDelimB);
    b.add_occurrence_group("A", kRoot, kNo,
                           fixpp::dict::group_record{.delimiter = kDelimA, .members = {kDelimA}});
    auto const tv = std::move(b).build();
    EXPECT_EQ(tv.wire_group_first_field("A", kRoot, kNo), kDelimA);
    EXPECT_EQ(as_vec(tv.wire_group_member_tags("A", kRoot, kNo)), (std::vector<tag_t>{kDelimA}));
    auto const r = tv.wire_group_resolve("A", kRoot, kNo);
    std::vector<tag_t> const want_members{kDelimA};
    EXPECT_TRUE(r.registered);
    EXPECT_EQ(r.delimiter, kDelimA);
    EXPECT_EQ(as_vec(r.members), want_members);
}

// T071 fix: the resolver's other arms, each spelt out. The bare-only arm (no context key
// anywhere) registers and answers the bare store; a context key answers its own entry and
// leaves another context unregistered with the bare store's definition; a tag with no
// group bit answers nothing.
TEST(RegistrationRecords100, WireResolve_BareOnly_RegistersFromTheBareStore) {
    table_view_builder b;
    b.set_group_first(kNo, kDelimB);
    auto const tv = std::move(b).build();
    auto const r = tv.wire_group_resolve("A", kRoot, kNo);
    std::vector<tag_t> const want_members{kDelimB};
    EXPECT_TRUE(r.registered);
    EXPECT_EQ(r.delimiter, kDelimB);
    EXPECT_EQ(as_vec(r.members), want_members);
}
TEST(RegistrationRecords100, WireResolve_ContextKey_AnswersItsEntryAndNotAnotherContext) {
    table_view_builder b;
    b.set_group_first(kNo, kDelimB);
    b.set_group_first_ctx("A", kRoot, kNo, kDelimA);
    auto const tv = std::move(b).build();
    auto const in_a = tv.wire_group_resolve("A", kRoot, kNo);
    std::vector<tag_t> const want_a{kDelimA};
    EXPECT_TRUE(in_a.registered);
    EXPECT_EQ(in_a.delimiter, kDelimA);
    EXPECT_EQ(as_vec(in_a.members), want_a);
    auto const in_b = tv.wire_group_resolve("B", kRoot, kNo);
    std::vector<tag_t> const want_b{kDelimB};
    EXPECT_FALSE(in_b.registered);
    EXPECT_EQ(in_b.delimiter, kDelimB);
    EXPECT_EQ(as_vec(in_b.members), want_b);
}
TEST(RegistrationRecords100, WireResolve_NoGroupBit_AnswersNothing) {
    table_view_builder b;
    b.set_group_first(kNo, kDelimB);
    auto const tv = std::move(b).build();
    auto const r = tv.wire_group_resolve("A", kRoot, kMember);
    EXPECT_FALSE(r.registered);
    EXPECT_EQ(r.delimiter, 0U);
    EXPECT_TRUE(r.members.empty());
}

// A second record at a key the store holds is dropped: the first record answers.
TEST(RegistrationRecords100, AddRecord_SecondRecordAtAKey_FirstWins) {
    table_view_builder b;
    b.set_group_first(kNo, kDelimA);
    b.add_occurrence_group(
        "A", kRoot, kNo,
        fixpp::dict::group_record{.delimiter = kDelimA, .members = {kDelimA, 702}});
    b.add_occurrence_group("A", kRoot, kNo,
                           fixpp::dict::group_record{.delimiter = kDelimB, .members = {kDelimB}});
    b.add_section_group(kRoot, 705,
                        fixpp::dict::group_record{.delimiter = kDelimA, .members = {kDelimA}});
    b.add_section_group(kRoot, 705,
                        fixpp::dict::group_record{.delimiter = kDelimB, .members = {kDelimB}});
    b.set_group_first(705, kDelimA);
    auto const tv = std::move(b).build();
    EXPECT_EQ(tv.wire_group_first_field("A", kRoot, kNo), kDelimA);
    EXPECT_EQ(as_vec(tv.wire_group_member_tags("A", kRoot, kNo)),
              (std::vector<tag_t>{kDelimA, 702}));
    EXPECT_EQ(tv.wire_group_first_field("A", kRoot, 705), kDelimA);
    EXPECT_EQ(as_vec(tv.wire_group_member_tags("A", kRoot, 705)), (std::vector<tag_t>{kDelimA}));
}

// The loaders' section walks over malformed custom dictionaries. Each fixture puts a
// resolvable section group AFTER the arm it exercises, so an arm that ended the walk
// instead of skipping one child would lose that group's record.
using fixpp::test100::div::kDivFields;
using fixpp::test100::div::ocomp;
using fixpp::test100::div::ocompref;
using fixpp::test100::div::ofield;
using fixpp::test100::div::ogroup;
using fixpp::test100::div::ogroupref;
using fixpp::test100::div::omsg;
using fixpp::test100::div::orch_dict;
using fixpp::test100::div::orch_types;
using fixpp::test100::div::xcomp;
using fixpp::test100::div::xfield;
using fixpp::test100::div::xgroup;
using fixpp::test100::div::xml_dict;
using fixpp::test100::div::xmsg;

[[nodiscard]] table_view load_xml_tv(std::string const& text,
                                     fixpp::dict::unresolved_group_policy policy =
                                         fixpp::dict::unresolved_group_policy::fail_closed) {
    return fixpp::dict::XmlLoader{}
        .load_from_string(text, std::pmr::get_default_resource(), policy)
        .as_table_view();
}
[[nodiscard]] table_view load_orch_tv(std::string const& text,
                                      fixpp::dict::unresolved_group_policy policy =
                                          fixpp::dict::unresolved_group_policy::fail_closed) {
    return fixpp::dict::OrchestraLoader{}
        .load_from_string(text, std::pmr::get_default_resource(), policy)
        .as_table_view();
}
// The section record at `(path, no_tag)`, or nullopt.
[[nodiscard]] std::optional<fixpp::dict::group_record> section_record(table_view const& tv,
                                                                      std::span<tag_t const> path,
                                                                      tag_t no_tag) {
    auto const& sec = table_view_test_access::section_records(tv);
    auto const it = sec.find(fixpp::dict::make_ctx_query({}, path, no_tag));
    if (it == sec.end()) {
        return std::nullopt;
    }
    return it->second;
}
void expect_record(table_view const& tv, std::span<tag_t const> path, tag_t no_tag, tag_t delim,
                   std::vector<tag_t> const& members, char const* what) {
    auto const r = section_record(tv, path, no_tag);
    ASSERT_TRUE(r.has_value()) << what << ": section record at no_tag " << no_tag;
    EXPECT_EQ(r->delimiter, delim) << what;
    EXPECT_EQ(r->members, members) << what;
}

// The XML loader resolves names in message expansions, so a dictionary that declares no
// message reaches the walk with names it cannot resolve. An unknown field, component or
// group name adds nothing, as does an element of another kind, and the walk goes on to the
// next child.
TEST(RegistrationRecords100, SectionWalkXml_UnresolvedNames_AreSkipped) {
    std::string const nope = "<field name='Nope' required='N'/>";
    auto const tv =
        load_xml_tv(xml_dict(kDivFields,
                             xcomp("Nope") + xgroup("NopeGroup", xfield("A1")) + nope + "<other/>" +
                                 xgroup("NoA", xfield("A1") + nope + xcomp("Nope") + xfield("X1")),
                             "", ""));
    expect_record(tv, kRoot, 601, 602, {602, 610}, "XML unresolved names");
}
// A section group with no member has no delimiter and stores no record (the tolerant
// policy loads it).
TEST(RegistrationRecords100, SectionWalkXml_GroupWithoutMember_StoresNoRecord) {
    auto const tv = load_xml_tv(
        xml_dict(kDivFields,
                 "<group name='NoZ' required='N'></group>" + xgroup("NoA", xfield("A1")), "",
                 xmsg("M", xfield("Text"))),
        fixpp::dict::unresolved_group_policy::tolerant);
    EXPECT_FALSE(section_record(tv, kRoot, 606).has_value());
    expect_record(tv, kRoot, 601, 602, {602}, "XML group after the empty one");
}
// The same key in the header and the trailer: the header's record is kept.
TEST(RegistrationRecords100, SectionWalkXml_SameKeyInHeaderAndTrailer_HeaderWins) {
    auto const tv = load_xml_tv(xml_dict(kDivFields, xgroup("NoT", xfield("T1") + xfield("T2")),
                                         xgroup("NoT", xfield("T3") + xfield("T4")),
                                         xmsg("M", xfield("Text"))));
    expect_record(tv, kRoot, 630, 631, {631, 632}, "XML header before trailer");
}
// A component referenced twice at one path is walked once. Line coverage of the
// visited refusal: the sink's first-wins rule gives the same store without it.
TEST(RegistrationRecords100, SectionWalkXml_ComponentTwiceAtOnePath_RecordedOnce) {
    auto const tv = load_xml_tv(
        xml_dict(kDivFields, xcomp("CA") + xcomp("CA"), "", xmsg("M", xfield("Text")),
                 "<component name='CA'>" + xgroup("NoA", xfield("A1")) + "</component>"));
    expect_record(tv, kRoot, 601, 602, {602}, "XML component twice");
}

TEST(RegistrationRecords100, SectionWalkOrchestra_GroupWithoutMember_StoresNoRecord) {
    auto const tv = load_orch_tv(
        orch_dict(orch_types(kDivFields), ogroupref(2606) + ogroupref(2601), "", "",
                  ogroup(2606, 606, "") + ogroup(2601, 601, ofield(602)), omsg("M", ofield(58))),
        fixpp::dict::unresolved_group_policy::tolerant);
    EXPECT_FALSE(section_record(tv, kRoot, 606).has_value());
    expect_record(tv, kRoot, 601, 602, {602}, "Orchestra group after the empty one");
}
// A group referenced twice at one path, and the trailer reached from inside the header:
// each definition is walked once at a path. Line coverage of the refusals, as above.
TEST(RegistrationRecords100, SectionWalkOrchestra_DefinitionTwiceAtOnePath_RecordedOnce) {
    auto const tv = load_orch_tv(orch_dict(
        orch_types(kDivFields),
        ogroupref(2601) + ogroupref(2601) + ocompref(3002) + ocompref(3002) + ocompref(1025),
        ogroupref(2630), ocomp(3002, ogroupref(2606)),
        ogroup(2601, 601, ofield(602)) + ogroup(2630, 630, ofield(631)) +
            ogroup(2606, 606, ofield(607)),
        omsg("M", ofield(58))));
    expect_record(tv, kRoot, 601, 602, {602}, "Orchestra group twice");
    expect_record(tv, kRoot, 606, 607, {607}, "Orchestra component twice");
    expect_record(tv, kRoot, 630, 631, {631}, "Orchestra trailer reached from the header");
}
// `detail::SectionWalk::enter` directly: a definition already on the current walk is
// refused whatever the path, which ends a cycle; one visited at a path is refused at that
// path only. Both loaders' base load paths crash on a component cycle before the walk
// runs (fixpp#650), so no loader fixture reaches the cycle arm.
TEST(RegistrationRecords100, SectionWalk_Enter_RefusesOnPathAndVisited) {
    fixpp::dict::detail::SectionWalk w;
    int const a = 0;
    int const b = 0;
    ASSERT_TRUE(w.enter(&a));
    EXPECT_FALSE(w.enter(&a)) << "on the current walk, same path";
    w.path.push_back(601);
    EXPECT_FALSE(w.enter(&a)) << "on the current walk, another path";
    EXPECT_TRUE(w.enter(&b)) << "another definition";
    w.leave();
    w.leave();
    EXPECT_TRUE(w.enter(&a)) << "off the walk, a path not visited";
    w.leave();
    EXPECT_FALSE(w.enter(&a)) << "visited at this path";
    w.path.pop_back();
    EXPECT_FALSE(w.enter(&a)) << "visited at the empty path";
}

}  // namespace
