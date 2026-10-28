// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/tail_rows_witness.hpp
//
// 096-fix-latest-tail-evidence (contracts W0): witness machinery shared by the tail-row witness
// TUs: the required-closure walk over a QuickFIX-XML dictionary, the parsed-body enumeration, the
// presence check, and check_witness (W3-W5 for one witness), a free function taking the walk, the
// parse dictionary, the read arena and the expected MsgType.
//
// The code between the clang-format markers is held to contracts W0 of
// specs/096-fix-latest-tail-evidence: edit it only together with that contract. Section 1 of that
// feature's quickstart.md re-checks it, run from the library root.

#pragma once

#include <gtest/gtest.h>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/wire/offset_table.hpp>
#include <fixpp/wire/parser.hpp>
#include <fixpp/wire/view.hpp>
#include <functional>
#include <map>
#include <memory_resource>
#include <optional>
#include <pugixml.hpp>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "support/app_message_read_scaffold.hpp"

namespace fixpp_test_support::tail_rows {

// clang-format off
using IndexView = fixpp::wire::MessageView<fixpp::wire::access_mode::Index>;

// ── The walk (data-model §B) ─────────────────────────────────────────────────

enum class walk_status { ok, nested_required_group, delimiter_chain, group_among_siblings };

inline char const* to_string(walk_status s) {
    switch (s) {
        case walk_status::ok:
            return "ok";
        case walk_status::nested_required_group:
            return "nested_required_group";
        case walk_status::delimiter_chain:
            return "delimiter_chain";
        case walk_status::group_among_siblings:
            return "group_among_siblings";
    }
    return "?";
}

struct RequiredGroup {
    std::uint16_t count_tag = 0;
    std::uint16_t delim_tag = 0;
    // Set when delim_tag is a nested group's count tag: (that count tag, its own first tag).
    std::optional<std::pair<std::uint16_t, std::uint16_t>> delim_group;
    std::vector<std::uint16_t> fields;  // one instance's required fields, dictionary order
};

struct WalkResult {
    walk_status status = walk_status::ok;
    std::string element;                // the element a non-ok status names
    std::vector<std::uint16_t> fields;  // required message-level fields, dictionary order
    std::vector<RequiredGroup> groups;  // required groups, dictionary order
};

using WalkMap = std::map<std::string, WalkResult, std::less<>>;

inline bool is_element(pugi::xml_node n) {
    std::string_view const name = n.name();
    return name == "field" || name == "component" || name == "group";
}

inline bool is_group(pugi::xml_node n) { return std::string_view{n.name()} == "group"; }

inline bool is_required(pugi::xml_node n) {
    return std::string_view{n.attribute("required").as_string()} == "Y";
}

inline std::vector<pugi::xml_node> element_children(pugi::xml_node n) {
    std::vector<pugi::xml_node> out;
    for (pugi::xml_node c : n.children()) {
        if (is_element(c)) out.push_back(c);
    }
    return out;
}

class Walker {
public:
    explicit Walker(pugi::xml_document const& doc) {
        pugi::xml_node const fix = doc.child("fix");
        for (pugi::xml_node f : fix.child("fields").children("field")) {
            tags_[f.attribute("name").as_string()] =
                static_cast<std::uint16_t>(f.attribute("number").as_uint());
        }
        for (pugi::xml_node c : fix.child("components").children("component")) {
            components_[c.attribute("name").as_string()] = c;
        }
        messages_ = fix.child("messages");
    }

    [[nodiscard]] WalkMap walk_all() const {
        WalkMap out;
        for (pugi::xml_node m : messages_.children("message")) {
            out[m.attribute("msgtype").as_string()] = walk_message(m);
        }
        return out;
    }

private:
    struct Failure {
        walk_status status = walk_status::ok;
        std::string element;
        void set(walk_status s, std::string e) {
            if (status == walk_status::ok) {
                status = s;
                element = std::move(e);
            }
        }
    };

    [[nodiscard]] std::uint16_t tag_of(pugi::xml_node n) const {
        auto const it = tags_.find(n.attribute("name").as_string());
        return it == tags_.end() ? std::uint16_t{0} : it->second;
    }

    [[nodiscard]] pugi::xml_node component_def(pugi::xml_node ref) const {
        auto const it = components_.find(ref.attribute("name").as_string());
        return it == components_.end() ? pugi::xml_node{} : it->second;
    }

    // The first emitted element (a field or a group) under `n`, components expanded.
    [[nodiscard]] pugi::xml_node first_emitted(pugi::xml_node n) const {
        for (pugi::xml_node c : element_children(n)) {
            if (std::string_view{c.name()} != "component") return c;
            if (pugi::xml_node const inner = first_emitted(component_def(c))) return inner;
        }
        return {};
    }

    // Collects the required fields and groups under `container`, reached along a path whose
    // elements so far are all required iff `path_required`.
    void collect(pugi::xml_node container, bool path_required, std::vector<std::uint16_t>& fields,
                 std::vector<pugi::xml_node>& groups, Failure& fail) const {
        for (pugi::xml_node c : element_children(container)) {
            bool const req = path_required && is_required(c);
            std::string_view const kind = c.name();
            if (kind == "field") {
                if (req && std::ranges::find(fields, tag_of(c)) == fields.end()) {
                    fields.push_back(tag_of(c));
                }
            } else if (kind == "group") {
                if (req) groups.push_back(c);
            } else {
                pugi::xml_node const def = component_def(c);
                std::vector<pugi::xml_node> const kids = element_children(def);
                if (kids.size() == 1 && is_group(kids.front())) {
                    // Only-child inheritance: the group takes the flag of the reference that wraps
                    // it.
                    if (req) groups.push_back(kids.front());
                    continue;
                }
                if (req && std::ranges::any_of(kids, is_group)) {
                    fail.set(walk_status::group_among_siblings, c.attribute("name").as_string());
                }
                collect(def, req, fields, groups, fail);
            }
        }
    }

    [[nodiscard]] WalkResult walk_message(pugi::xml_node m) const {
        WalkResult r;
        Failure fail;
        std::vector<pugi::xml_node> groups;
        collect(m, true, r.fields, groups, fail);
        for (pugi::xml_node g : groups) {
            RequiredGroup rg;
            rg.count_tag = tag_of(g);
            pugi::xml_node const first = first_emitted(g);
            rg.delim_tag = tag_of(first);
            if (is_group(first)) {
                pugi::xml_node const nested_first = first_emitted(first);
                if (is_group(nested_first)) {
                    fail.set(walk_status::delimiter_chain,
                             nested_first.attribute("name").as_string());
                }
                rg.delim_group = std::pair{tag_of(first), tag_of(nested_first)};
            }
            std::vector<pugi::xml_node> inner_groups;
            collect(g, true, rg.fields, inner_groups, fail);
            if (!inner_groups.empty()) {
                fail.set(walk_status::nested_required_group,
                         inner_groups.front().attribute("name").as_string());
            }
            r.groups.push_back(std::move(rg));
        }
        r.status = fail.status;
        r.element = fail.element;
        return r;
    }

    std::map<std::string, std::uint16_t, std::less<>> tags_;
    std::map<std::string, pugi::xml_node, std::less<>> components_;
    pugi::xml_node messages_;
};

// The MsgType of `<message name='<generated_name>' msgtype=…>`, or empty when no message has
// that name. The census and every witness's tag-35 check share this one lookup.
inline std::string msgtype_for_generated_name(pugi::xml_document const& doc,
                                       std::string_view generated_name) {
    for (pugi::xml_node m : doc.child("fix").child("messages").children("message")) {
        if (generated_name == m.attribute("name").as_string()) {
            return m.attribute("msgtype").as_string();
        }
    }
    return {};
}

// `<RowId>_<GeneratedName>` → its two halves.
inline std::pair<std::string, std::string> split_test_name(std::string_view test_name) {
    std::size_t const us = test_name.find('_');
    if (us == std::string_view::npos) return {std::string{test_name}, {}};
    return {std::string{test_name.substr(0, us)}, std::string{test_name.substr(us + 1)}};
}

// ── The body enumeration (research R-4, contracts W4) ───────────────────────

struct BodyEntry {
    std::string path;
    std::uint16_t tag = 0;
    std::string value;
    int group = -1;  // index into WalkResult::groups, -1 at message level
    std::size_t instance = 0;
};

struct BodyEnumeration {
    std::vector<BodyEntry> entries;           // document order
    std::vector<std::size_t> instance_count;  // per required group: group_slices(count_tag).size()
};

inline bool is_framing_or_msgtype(std::uint16_t tag) {
    return tag == 8 || tag == 9 || tag == 10 || tag == 35;
}

inline std::string instance_path(std::uint16_t count_tag, std::size_t k, std::uint16_t tag) {
    return std::to_string(count_tag) + "[" + std::to_string(k) + "]." + std::to_string(tag);
}

// Every parsed body entry but the framing tags and tag 35, each attributed by extent to message
// level or to the instance of the outermost required group whose slice contains it.
inline BodyEnumeration enumerate_body(IndexView const& mv, WalkResult const& wr) {
    BodyEnumeration out;
    std::vector<std::span<fixpp::wire::group_slice const>> slices;
    for (RequiredGroup const& g : wr.groups) {
        slices.push_back(mv.offsets().group_slices(g.count_tag));
        out.instance_count.push_back(slices.back().size());
    }
    std::byte const* const base = mv.bytes().data();
    for (auto const& e : mv.offsets().entries()) {
        if (is_framing_or_msgtype(e.tag)) continue;
        std::byte const* const value_ptr = base + e.offset;
        BodyEntry be;
        be.tag = e.tag;
        be.value.assign(reinterpret_cast<char const*>(value_ptr), e.length);
        for (std::size_t gi = 0; gi < slices.size() && be.group < 0; ++gi) {
            for (std::size_t k = 0; k < slices[gi].size(); ++k) {
                auto const& s = slices[gi][k];
                if (value_ptr >= s.data && value_ptr < s.data + s.len) {
                    be.group = static_cast<int>(gi);
                    be.instance = k;
                    break;
                }
            }
        }
        be.path = be.group < 0
                      ? std::to_string(e.tag)
                      : instance_path(wr.groups[static_cast<std::size_t>(be.group)].count_tag,
                                      be.instance, e.tag);
        out.entries.push_back(std::move(be));
    }
    return out;
}

inline std::optional<std::string> value_at(BodyEnumeration const& body, int group, std::size_t instance,
                                    std::uint16_t tag) {
    for (BodyEntry const& e : body.entries) {
        if (e.group == group && (group < 0 || e.instance == instance) && e.tag == tag) {
            return e.value;
        }
    }
    return std::nullopt;
}

inline long long parse_count(std::string const& v) {
    long long n = -1;
    auto const [p, ec] = std::from_chars(v.data(), v.data() + v.size(), n);
    return (ec == std::errc{} && p == v.data() + v.size()) ? n : -1;
}

// The tag a path names: `<tag>` or `<count_tag>[<k>].<tag>`.
inline std::uint16_t tag_of_path(std::string_view path) {
    std::size_t const dot = path.rfind('.');
    std::string_view const t = dot == std::string_view::npos ? path : path.substr(dot + 1);
    unsigned v = 0;
    std::from_chars(t.data(), t.data() + t.size(), v);
    return static_cast<std::uint16_t>(v);
}

// The closure-required members the parsed frame lacks, as an ordered sequence with duplicates
// kept (data-model §B's leg order). The MsgType is the frame's own tag 35.
inline std::vector<std::string> required_members_missing(WalkMap const& walk, IndexView const& mv) {
    std::string const mt{mv.msg_type()};
    auto const it = walk.find(mt);
    if (it == walk.end()) return {"35=" + mt + ": no such message in the walk"};
    WalkResult const& wr = it->second;
    if (wr.status != walk_status::ok) {
        return {"35=" + mt + ": walk status " + to_string(wr.status) + " at " + wr.element};
    }
    BodyEnumeration const body = enumerate_body(mv, wr);
    std::vector<std::string> out;
    std::size_t const ng = wr.groups.size();
    auto const count_of = [&](std::size_t gi) {
        return value_at(body, -1, 0, wr.groups[gi].count_tag);
    };
    // Leg 1: message-level fields.
    for (std::uint16_t f : wr.fields) {
        if (!value_at(body, -1, 0, f)) out.push_back(std::to_string(f));
    }
    // Leg 2: required groups absent.
    for (std::size_t gi = 0; gi < ng; ++gi) {
        if (!count_of(gi)) out.push_back(std::to_string(wr.groups[gi].count_tag));
    }
    // Leg 3: count value below 2.
    for (std::size_t gi = 0; gi < ng; ++gi) {
        if (auto const c = count_of(gi); c && parse_count(*c) < 2) {
            out.push_back(std::to_string(wr.groups[gi].count_tag));
        }
    }
    // Leg 4: instance count differs from the count value.
    for (std::size_t gi = 0; gi < ng; ++gi) {
        if (auto const c = count_of(gi);
            c && parse_count(*c) != static_cast<long long>(body.instance_count[gi])) {
            out.push_back(std::to_string(wr.groups[gi].count_tag));
        }
    }
    // Leg 5 (delimiter leg): each instance's delimiter.
    for (std::size_t gi = 0; gi < ng; ++gi) {
        RequiredGroup const& g = wr.groups[gi];
        for (std::size_t k = 0; k < body.instance_count[gi]; ++k) {
            if (!value_at(body, static_cast<int>(gi), k, g.delim_tag)) {
                out.push_back(instance_path(g.count_tag, k, g.delim_tag));
            }
        }
    }
    // Leg 6 (field leg): each instance's required fields; the delimiter is the delimiter leg's.
    for (std::size_t gi = 0; gi < ng; ++gi) {
        RequiredGroup const& g = wr.groups[gi];
        for (std::size_t k = 0; k < body.instance_count[gi]; ++k) {
            for (std::uint16_t f : g.fields) {
                if (f == g.delim_tag) continue;
                if (!value_at(body, static_cast<int>(gi), k, f)) {
                    out.push_back(instance_path(g.count_tag, k, f));
                }
            }
        }
    }
    // Leg 7 (delim_group leg): where the nested count tag is present in the instance, its value
    // must be 1 and the nested delimiter present. An absent nested count is the delimiter leg's.
    for (std::size_t gi = 0; gi < ng; ++gi) {
        RequiredGroup const& g = wr.groups[gi];
        if (!g.delim_group) continue;
        auto const [nested_count, nested_delim] = *g.delim_group;
        for (std::size_t k = 0; k < body.instance_count[gi]; ++k) {
            auto const n = value_at(body, static_cast<int>(gi), k, nested_count);
            if (!n) continue;
            if (parse_count(*n) != 1) out.push_back(instance_path(g.count_tag, k, nested_count));
            if (!value_at(body, static_cast<int>(gi), k, nested_delim)) {
                out.push_back(instance_path(g.count_tag, k, nested_delim));
            }
        }
    }
    return out;
}

inline std::string join(std::vector<std::string> const& v) {
    std::string s = "[";
    for (std::size_t i = 0; i < v.size(); ++i) s += (i ? ", " : "") + v[i];
    return s + "]";
}

inline std::string join_tags(std::set<std::uint16_t> const& v) {
    std::string s = "{";
    bool first = true;
    for (std::uint16_t t : v) {
        s += (first ? "" : " ") + std::to_string(t);
        first = false;
    }
    return s + "}";
}

inline std::string drop_hyphen(std::string_view row) {
    std::string s{row};
    std::erase(s, '-');
    return s;
}

// parse_dict reports a refusal as a gtest failure; `refused` says whether this parse added one.
inline IndexView parse_noting_refusal(std::vector<std::byte> const& frame,
                               fixpp::dict::table_view const& tv, std::pmr::memory_resource* mr,
                               bool& refused) {
    ::testing::TestResult const* const result =
        ::testing::UnitTest::GetInstance()->current_test_info()->result();
    int const parts_before = result->total_part_count();
    IndexView mv = parse_dict(frame, tv, mr);
    refused = result->total_part_count() != parts_before;
    return mv;
}

using Intent = std::vector<std::pair<std::string, std::string>>;

inline void check_witness(WalkMap const& walk, fixpp::dict::table_view const& tv, std::pmr::memory_resource* read_arena, std::string_view expected_mt, std::string const& body, Intent const& intent) {
    ASSERT_GE(intent.size(), 2U) << "W4 intent-size: the intent list holds " << intent.size()
                                 << " entries; a witness reads back at least two";
    std::vector<std::byte> const frame = make_frame("FIX.5.0SP2", body);
    bool refused = false;
    auto const mv = parse_noting_refusal(frame, tv, read_arena, refused);
    ASSERT_FALSE(refused)
        << "W3: the parser refused the witness frame (a builder/parser disagreement, FR-020)";

    std::string const test_name =
        ::testing::UnitTest::GetInstance()->current_test_info()->name();
    EXPECT_EQ(std::string{mv.msg_type()}, expected_mt)
        << "W1 tag 35: the frame carries 35=" << mv.msg_type() << ", the test name "
        << test_name << " maps to 35=" << expected_mt;

    auto const it = walk.find(mv.msg_type());
    ASSERT_NE(it, walk.end()) << "35=" << mv.msg_type() << " is not in the walk";
    WalkResult const& wr = it->second;
    ASSERT_STREQ(to_string(wr.status), "ok")
        << "walk status at " << wr.element << " for 35=" << mv.msg_type();
    for (std::uint16_t f : wr.fields) {
        EXPECT_FALSE(is_framing_or_msgtype(f))
            << "W3: closure-required tag " << f << " is one the framing writes";
    }

    // W4: the intent list against the parsed body, as a multiset, both directions.
    BodyEnumeration const parsed = enumerate_body(mv, wr);
    std::vector<bool> used(parsed.entries.size(), false);
    for (auto const& [path, literal] : intent) {
        if (is_framing_or_msgtype(tag_of_path(path))) continue;
        std::optional<std::size_t> same_path;
        bool matched = false;
        for (std::size_t i = 0; i < parsed.entries.size(); ++i) {
            if (used[i] || parsed.entries[i].path != path) continue;
            if (parsed.entries[i].value == literal) {
                used[i] = true;
                matched = true;
                break;
            }
            if (!same_path) same_path = i;
        }
        if (matched) continue;
        if (same_path) {
            used[*same_path] = true;
            ADD_FAILURE() << "W4 value mismatch at " << path << ": intended '" << literal
                          << "', parsed '" << parsed.entries[*same_path].value << "'";
        } else {
            ADD_FAILURE() << "W4 intended, not parsed: " << path << "=" << literal;
        }
    }
    for (std::size_t i = 0; i < parsed.entries.size(); ++i) {
        if (!used[i]) {
            ADD_FAILURE() << "W4 parsed, not intended: " << parsed.entries[i].path << "="
                          << parsed.entries[i].value;
        }
    }

    // W5: the dictionary-derived presence check, on the parsed frame.
    std::vector<std::string> const missing = required_members_missing(walk, mv);
    EXPECT_TRUE(missing.empty()) << "W5 required members missing: " << join(missing);
}
// clang-format on

}  // namespace fixpp_test_support::tail_rows
