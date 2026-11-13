// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/test_102_component_identity.cpp — 102-dict-width-multichar-type, spec US1.3,
// US1.4, SC-005, FR-001a, FR-002 (research D4 "component identity", "Overlay crossing"; tasks
// T011).
//
// Over tests/support/dict_width_fixtures_102.hpp's QuickFIX XML base of more than `kWrap`
// components (generator (a)): every component's `component_id`, `parent_component_id` and
// `component_fields`, and each component member's `FieldRef::component_index` through
// `message_fields`, equal the generator's values, which it computed from its own layout. The
// spec edge case "a child component whose parent sits at index 65,535 or beyond" asserts the
// parent's id + 1, not 0. Over generator (d), the XML overlay-crossing row: a base just under
// `kWrap` components plus an overlay at `kMaxNewComponents` new components, whose new
// components' ids and members the row asserts.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/field_ref.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <typeinfo>
#include <vector>

#include "overlay_merge.hpp"
#include "support/dict_width_fixtures_102.hpp"

namespace {

namespace w = fixpp::test102::width;
using fixpp::dict::Dictionary;
using fixpp::dict::FieldRef;

struct loaded {
    std::pmr::monotonic_buffer_resource arena;
    std::optional<Dictionary> dict;
    std::string error;
};

// Generator (a), loaded once per binary.
loaded const& xml_identity() {
    static loaded l;
    static bool const done = [] {
        try {
            l.dict.emplace(fixpp::dict::XmlLoader{}.load_from_string(
                w::xml_identity_fixture_text().text, &l.arena));
        } catch (std::exception const& e) {
            l.error = e.what();
        }
        return true;
    }();
    (void)done;
    return l;
}

std::vector<w::member> members_of(std::span<FieldRef const> run) {
    std::vector<w::member> out;
    for (auto const& fr : run) {
        out.push_back({.tag = fr.tag, .component_index = fr.component_index});
    }
    return out;
}

std::string render(std::vector<w::member> const& v) {
    std::string s;
    for (auto const& m : v) {
        s += " (" + std::to_string(m.tag) + ", " + std::to_string(m.component_index) + ")";
    }
    return s;
}

// Compares each expected component with the dictionary; returns the number that differ and
// describes the first.
std::size_t compare_components(Dictionary const& d,
                               std::vector<w::expected_component> const& expected,
                               std::string& first) {
    std::size_t wrong = 0;
    for (auto const& e : expected) {
        auto const cr = d.component(e.name);
        std::string why;
        if (!cr) {
            why = "not found";
        } else {
            std::uint64_t const id = cr->component_id;
            std::uint64_t const parent = cr->parent_component_id;
            auto const got = members_of(d.component_fields(e.name));
            if (id != e.id) {
                why += " component_id " + std::to_string(id) + " (expected " +
                       std::to_string(e.id) + ")";
            }
            if (parent != e.parent) {
                why += " parent_component_id " + std::to_string(parent) + " (expected " +
                       std::to_string(e.parent) + ")";
            }
            if (got != e.members) {
                why += " members" + render(got) + " (expected" + render(e.members) + ")";
            }
        }
        if (!why.empty()) {
            if (wrong == 0) {
                first = "component \"" + e.name + "\":" + why;
            }
            ++wrong;
        }
    }
    return wrong;
}

}  // namespace

TEST(ComponentIdentity102Xml, EveryComponentHasItsOwnIdentityAndMembers) {
    auto const& f = w::xml_identity_fixture_text();
    ASSERT_EQ(f.precondition_failure(), "");
    auto const& l = xml_identity();
    ASSERT_TRUE(l.dict.has_value()) << "the base did not load: " << l.error;
    std::string first;
    std::size_t const wrong = compare_components(*l.dict, f.components, first);
    EXPECT_EQ(wrong, 0U) << "components with a wrong identity or members; first: " << first;
}

// Spec Edge Cases: a child whose parent sits at index 65,535 (its one-based value is the first
// that wraps) and one whose parent sits beyond it.
TEST(ComponentIdentity102Xml, ParentAtAndPastTheWrapPointIsTheParentsIdPlusOne) {
    auto const& f = w::xml_identity_fixture_text();
    ASSERT_EQ(f.precondition_failure(), "");
    auto const& l = xml_identity();
    ASSERT_TRUE(l.dict.has_value()) << "the base did not load: " << l.error;
    for (char const* kid : {"Kid1", "Kid2"}) {
        auto const cr = l.dict->component(kid);
        ASSERT_TRUE(cr.has_value()) << kid;
        std::uint32_t expected = 0;
        for (auto const& e : f.components) {
            if (e.name == kid) {
                expected = e.parent;
            }
        }
        ASSERT_NE(expected, 0U) << kid << ": the fixture gives it a parent";
        std::uint64_t const got = cr->parent_component_id;
        EXPECT_EQ(got, expected) << kid;
    }
}

TEST(ComponentIdentity102Xml, MessageMembersCarryTheirComponentIndex) {
    auto const& f = w::xml_identity_fixture_text();
    ASSERT_EQ(f.precondition_failure(), "");
    auto const& l = xml_identity();
    ASSERT_TRUE(l.dict.has_value()) << "the base did not load: " << l.error;
    auto const run = l.dict->message_fields(f.msg_type);
    for (auto const& m : f.message_members) {
        FieldRef const* hit = nullptr;
        for (auto const& fr : run) {
            if (fr.tag == m.tag) {
                hit = &fr;
            }
        }
        if (hit == nullptr) {
            ADD_FAILURE() << "message " << f.msg_type << " lacks tag " << m.tag;
            continue;
        }
        std::uint64_t const got = hit->component_index;
        EXPECT_EQ(got, m.component_index) << "message " << f.msg_type << " tag " << m.tag;
    }
}

// Research D4 "Overlay crossing", QuickFIX XML: this row loads its own base and overlay.
TEST(ComponentIdentity102Xml, OverlayCrossingTheWrapPointGivesNewComponentsTheirIdentity) {
    auto const f = w::make_xml_crossing_fixture(fixpp::dict::detail::overlay::kMaxNewComponents);
    ASSERT_EQ(f.precondition_failure("component"), "");
    std::pmr::monotonic_buffer_resource arena;
    std::optional<Dictionary> d;
    try {
        d.emplace(
            fixpp::dict::XmlLoader{}.load_with_overlay_from_string(f.base, f.overlay, &arena));
    } catch (std::exception const& e) {
        FAIL() << "the merged dictionary did not load: " << typeid(e).name() << ": " << e.what();
    }
    std::string first;
    std::size_t const wrong = compare_components(*d, f.added, first);
    EXPECT_EQ(wrong, 0U) << "new components with a wrong identity or members; first: " << first;
}
