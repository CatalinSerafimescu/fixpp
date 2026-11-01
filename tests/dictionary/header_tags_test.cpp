// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/header_tags_test.cpp — 098-dialect-overlay, contracts/witnesses.md
// W-7 (FR-022, research R-7): the declared header store per vendored dictionary.
//
// STANDALONE binary (`dictionary_098_header_census_test`), per `[const §VII.8]`: both
// halves are exact-set censuses over the shipped dictionaries, and an exact-set
// completeness gate is isolation-sensitive.
//
// The expected tags come from a raw pugixml walk of each file, written here and
// independent of the loaders: QuickFIX XML from `<header>` (its fields, the members of
// each component it references, each group's NumInGroup tag and members, components
// nested to any depth, each component walked once); Orchestra from
// `fixr:component id="1024"` (fieldRefs, componentRefs, groupRefs with their
// `fixr:numInGroup` tag, each component and group walked once).
//
// Dictionary half: `table_view::is_header_tag` on `as_table_view()`'s product answers
// true exactly for the walk's tags, over every 16-bit tag, and false for 5020.
//
// Session half: the walk's tags unioned with the production fixed set
// (`src/session/send_header_tags.hpp`, read by including it, so no copy of the set
// lives here) equal the fixed set. A seeded FIX44 copy with 5020 in `<header>` makes
// that equality fail, which proves the instrument can report.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/orchestra_loader.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fstream>
#include <map>
#include <memory_resource>
#include <pugixml.hpp>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "reused_tag_census.hpp"  // fixpp_test_support::kRuntimeDicts
#include "send_header_tags.hpp"   // fixpp::session::detail::kSendHeaderTags

namespace {

using fixpp::dict::Dictionary;
using fixpp_test_support::kRuntimeDicts;
using tag_set = std::set<std::uint16_t>;

constexpr std::uint16_t kCustomTag = 5020;

std::string read_file(std::filesystem::path const& p) {
    std::ifstream in(p, std::ios::binary);
    EXPECT_TRUE(in.good()) << "cannot open " << p;
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::filesystem::path dict_path(std::string_view file) {
    return std::filesystem::path{FIXPP_DICT_DATA_DIR} / std::string{file};
}

std::filesystem::path orchestra_path() {
    return std::filesystem::path{FIXPP_ORCHESTRA_DATA_DIR} / "OrchestraFIXLatest.xml";
}

// ── the raw walks ────────────────────────────────────────────────────────────

// QuickFIX XML: `<header>` of `<fix>`. A name the walk cannot resolve is reported
// through `unresolved`, so a walk that silently loses a tag cannot pass.
tag_set quickfix_header_walk(std::string const& xml, std::vector<std::string>& unresolved) {
    pugi::xml_document doc;
    EXPECT_TRUE(doc.load_string(xml.c_str())) << "the document must parse";
    auto const root = doc.child("fix");
    std::map<std::string, std::uint16_t> numbers;
    for (auto const& f : root.child("fields").children("field")) {
        // cppcheck-suppress useStlAlgorithm  // the key and the value are each read from the node
        numbers.emplace(f.attribute("name").as_string(),
                        static_cast<std::uint16_t>(f.attribute("number").as_uint()));
    }
    std::map<std::string, pugi::xml_node> components;
    for (auto const& c : root.child("components").children("component")) {
        // cppcheck-suppress useStlAlgorithm  // the key and the value are each read from the node
        components.emplace(c.attribute("name").as_string(), c);
    }
    tag_set out;
    std::set<std::string> walked;
    std::vector<pugi::xml_node> stack{root.child("header")};
    auto const number_of = [&](std::string const& name) {
        auto const it = numbers.find(name);
        if (it == numbers.end()) {
            unresolved.push_back(name);
            return;
        }
        out.insert(it->second);
    };
    while (!stack.empty()) {
        pugi::xml_node const node = stack.back();
        stack.pop_back();
        for (auto const& child : node.children()) {
            std::string_view const kind{child.name()};
            std::string const name{child.attribute("name").as_string()};
            if (kind == "field") {
                number_of(name);
            } else if (kind == "group") {
                number_of(name);
                stack.push_back(child);
            } else if (kind == "component") {
                auto const it = components.find(name);
                if (it == components.end()) {
                    unresolved.push_back(name);
                } else if (walked.insert(name).second) {
                    stack.push_back(it->second);
                }
            }
        }
    }
    return out;
}

// Orchestra: `fixr:component id="1024"` (StandardHeader).
tag_set orchestra_header_walk(std::string const& xml, std::vector<std::string>& unresolved) {
    pugi::xml_document doc;
    EXPECT_TRUE(doc.load_string(xml.c_str())) << "the document must parse";
    auto const root = doc.child("fixr:repository");
    std::set<unsigned> field_ids;
    for (auto const& f : root.child("fixr:fields").children("fixr:field")) {
        // cppcheck-suppress useStlAlgorithm  // one attribute read per node of a pugixml range
        field_ids.insert(f.attribute("id").as_uint());
    }
    std::map<unsigned, pugi::xml_node> components;
    for (auto const& c : root.child("fixr:components").children("fixr:component")) {
        // cppcheck-suppress useStlAlgorithm  // the key and the value are each read from the node
        components.emplace(c.attribute("id").as_uint(), c);
    }
    std::map<unsigned, pugi::xml_node> groups;
    for (auto const& g : root.child("fixr:groups").children("fixr:group")) {
        // cppcheck-suppress useStlAlgorithm  // the key and the value are each read from the node
        groups.emplace(g.attribute("id").as_uint(), g);
    }
    tag_set out;
    auto const field_of = [&](unsigned id) {
        if (!field_ids.contains(id)) {
            unresolved.push_back("field " + std::to_string(id));
            return;
        }
        out.insert(static_cast<std::uint16_t>(id));
    };
    auto const header = components.find(1024U);
    if (header == components.end()) {
        unresolved.emplace_back("component 1024");
        return out;
    }
    std::set<unsigned> walked_components{1024U};
    std::set<unsigned> walked_groups;
    std::vector<pugi::xml_node> stack{header->second};
    while (!stack.empty()) {
        pugi::xml_node const node = stack.back();
        stack.pop_back();
        for (auto const& child : node.children()) {
            std::string_view const kind{child.name()};
            unsigned const id = child.attribute("id").as_uint();
            if (kind == "fixr:fieldRef") {
                field_of(id);
            } else if (kind == "fixr:componentRef") {
                auto const it = components.find(id);
                if (it == components.end()) {
                    unresolved.push_back("component " + std::to_string(id));
                } else if (walked_components.insert(id).second) {
                    stack.push_back(it->second);
                }
            } else if (kind == "fixr:groupRef") {
                auto const it = groups.find(id);
                if (it == groups.end()) {
                    unresolved.push_back("group " + std::to_string(id));
                } else if (walked_groups.insert(id).second) {
                    field_of(it->second.child("fixr:numInGroup").attribute("id").as_uint());
                    stack.push_back(it->second);
                }
            }
        }
    }
    return out;
}

// ── the seeded FIX44 copy ────────────────────────────────────────────────────

// FIX44.xml with 5020 declared in `<header>` and in `<fields>`. Each anchor must
// match exactly one place, or the seed is empty and the cell fails.
std::string seeded_fix44(std::string const& base) {
    auto const insert_before = [](std::string text, std::string_view anchor,
                                  std::string_view lines) -> std::string {
        auto const pos = text.find(anchor);
        if (pos == std::string::npos || text.find(anchor, pos + 1) != std::string::npos) {
            return {};
        }
        text.insert(pos, lines);
        return text;
    };
    std::string const with_header =
        insert_before(base, " </header>", "  <field name='Custom098HeaderTag' required='N' />\n");
    if (with_header.empty()) {
        return {};
    }
    return insert_before(with_header, " </fields>",
                         "  <field number='5020' name='Custom098HeaderTag' type='STRING' />\n");
}

// ── the two halves' predicates ───────────────────────────────────────────────

// Tags where `is_header_tag` and the walk disagree, over every 16-bit tag.
tag_set disagreements(fixpp::dict::table_view const& tv, tag_set const& walk) {
    tag_set out;
    for (std::uint32_t t = 0; t <= 0xFFFFU; ++t) {
        auto const tag = static_cast<std::uint16_t>(t);
        if (tv.is_header_tag(tag) != walk.contains(tag)) {
            out.insert(tag);
        }
    }
    return out;
}

// The walk's tags outside the production fixed set: empty exactly when the union
// of the two equals the fixed set.
tag_set outside_fixed_set(tag_set const& walk) {
    tag_set out;
    for (auto const t : walk) {
        if (!fixpp::session::detail::is_send_header_tag(t)) {
            out.insert(t);
        }
    }
    return out;
}

std::string render(tag_set const& s) {
    std::string out = "{";
    for (auto const t : s) {
        out += ' ' + std::to_string(t);
    }
    return out + " }";
}

// A FIX 5.0 QuickFIX-XML file has an empty `<header>` (research R-7): its transport
// header is FIXT11's.
bool header_is_empty_by_design(std::string_view file) {
    return file == "FIX50.xml" || file == "FIX50SP1.xml" || file == "FIX50SP2.xml";
}

constexpr std::size_t kArenaBytes = 32UZ * 1024UZ * 1024UZ;

// ── dictionary half ──────────────────────────────────────────────────────────

TEST(HeaderTagsW7Dictionary, QuickFixIsHeaderTagEqualsTheRawWalk) {
    for (auto const file : kRuntimeDicts) {
        SCOPED_TRACE(std::string{file});
        std::string const xml = read_file(dict_path(file));
        std::vector<std::string> unresolved;
        tag_set const walk = quickfix_header_walk(xml, unresolved);
        EXPECT_TRUE(unresolved.empty()) << "the walk met names it cannot resolve";
        EXPECT_EQ(walk.empty(), header_is_empty_by_design(file)) << "walk " << render(walk);
        std::pmr::monotonic_buffer_resource mr{kArenaBytes};
        Dictionary const d = fixpp::dict::XmlLoader{}.load(dict_path(file), &mr);
        auto const tv = d.as_table_view();
        EXPECT_EQ(disagreements(tv, walk), tag_set{}) << "walk " << render(walk);
        EXPECT_FALSE(tv.is_header_tag(kCustomTag));
    }
}

TEST(HeaderTagsW7Dictionary, OrchestraIsHeaderTagEqualsTheRawWalk) {
    std::string const xml = read_file(orchestra_path());
    std::vector<std::string> unresolved;
    tag_set const walk = orchestra_header_walk(xml, unresolved);
    EXPECT_TRUE(unresolved.empty()) << "the walk met ids it cannot resolve";
    EXPECT_FALSE(walk.empty());
    std::pmr::monotonic_buffer_resource mr{kArenaBytes};
    Dictionary const d = fixpp::dict::OrchestraLoader{}.load(orchestra_path(), &mr);
    auto const tv = d.as_table_view();
    EXPECT_EQ(disagreements(tv, walk), tag_set{}) << "walk " << render(walk);
    EXPECT_FALSE(tv.is_header_tag(kCustomTag));
}

// The seeded copy: the walk sees 5020, and so does `is_header_tag`.
TEST(HeaderTagsW7Dictionary, SeededFix44CopyDeclares5020) {
    std::string const xml = seeded_fix44(read_file(dict_path("FIX44.xml")));
    ASSERT_FALSE(xml.empty()) << "an anchor did not match exactly once";
    std::vector<std::string> unresolved;
    tag_set const walk = quickfix_header_walk(xml, unresolved);
    EXPECT_TRUE(unresolved.empty());
    ASSERT_TRUE(walk.contains(kCustomTag)) << "walk " << render(walk);
    std::pmr::monotonic_buffer_resource mr{kArenaBytes};
    Dictionary const d = fixpp::dict::XmlLoader{}.load_from_string(xml, &mr);
    auto const tv = d.as_table_view();
    EXPECT_EQ(disagreements(tv, walk), tag_set{}) << "walk " << render(walk);
    EXPECT_TRUE(tv.is_header_tag(kCustomTag));
}

// ── session half ─────────────────────────────────────────────────────────────

TEST(HeaderTagsW7Session, QuickFixWalkUnionFixedSetIsTheFixedSet) {
    for (auto const file : kRuntimeDicts) {
        SCOPED_TRACE(std::string{file});
        std::vector<std::string> unresolved;
        tag_set const walk = quickfix_header_walk(read_file(dict_path(file)), unresolved);
        EXPECT_TRUE(unresolved.empty());
        EXPECT_EQ(outside_fixed_set(walk), tag_set{}) << "walk " << render(walk);
    }
}

TEST(HeaderTagsW7Session, OrchestraWalkUnionFixedSetIsTheFixedSet) {
    std::vector<std::string> unresolved;
    tag_set const walk = orchestra_header_walk(read_file(orchestra_path()), unresolved);
    EXPECT_TRUE(unresolved.empty());
    EXPECT_FALSE(walk.empty());
    EXPECT_EQ(outside_fixed_set(walk), tag_set{}) << "walk " << render(walk);
}

// The instrument's proof: the seeded copy makes the union differ from the fixed set,
// by exactly the seeded tag.
TEST(HeaderTagsW7Session, SeededFix44CopyMakesTheUnionDiffer) {
    std::string const xml = seeded_fix44(read_file(dict_path("FIX44.xml")));
    ASSERT_FALSE(xml.empty()) << "an anchor did not match exactly once";
    std::vector<std::string> unresolved;
    tag_set const walk = quickfix_header_walk(xml, unresolved);
    EXPECT_TRUE(unresolved.empty());
    EXPECT_EQ(outside_fixed_set(walk), tag_set{kCustomTag}) << "walk " << render(walk);
}

}  // namespace
