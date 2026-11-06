// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/dictionary/test_099_fixt_header_tag_sets.cpp
//
// 099-fixt-appl-version-validation, contracts/witnesses.md W-21's dictionary half (tasks.md T025):
// the public tag sets the peek and the envelope read, each against an independent walk of the raw
// XML or against a literal the spec fixes. The walk uses pugixml, as
// tests/dictionary/fixt_header_merge_test.cpp does, never a production loader, so a loader that
// dropped a field could not agree with itself here.
//
// The peek's half is tests/session/test_099_peek_fixt_route.cpp (tasks.md T043). The helpers of
// data-model E-1a and E-2 that no witness row names, version_registry::transport() and
// session_to_application, are pinned here too.

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fixpp/core/error.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/load_any.hpp>
#include <fixpp/dict/version_profile.hpp>
#include <fixpp/dict/version_registry.hpp>
#include <map>
#include <memory>
#include <memory_resource>
#include <pugixml.hpp>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace {

namespace dd = fixpp::dict;
using tag_set = std::set<std::uint16_t>;

constexpr unsigned kMaxTag = 65535;

// `<header>` of a QuickFIX-schema file, by name through its `<fields>` catalogue: each field, and
// each group's own count field and members, recursively. A `<component>` reference is resolved,
// so a file that moves a header field into a component stays visible.
class HeaderWalk {
public:
    explicit HeaderWalk(const char* file) {
        const auto path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / file;
        const auto loaded = doc_.load_file(path.string().c_str());
        EXPECT_TRUE(loaded) << "failed to load " << path;
        fix_ = doc_.child("fix");
        for (const auto& f : fix_.child("fields").children("field")) {
            by_name_[f.attribute("name").value()] =
                static_cast<std::uint16_t>(f.attribute("number").as_uint());
        }
        EXPECT_FALSE(by_name_.empty()) << file << ": the walk read no <fields>";
    }

    [[nodiscard]] tag_set header() {
        tag_set out;
        walk(fix_.child("header"), out);
        return out;
    }

private:
    void add(std::string_view name, tag_set& out) {
        const auto it = by_name_.find(std::string{name});
        if (it == by_name_.end()) {
            ADD_FAILURE() << "name '" << name << "' is not in <fields>";
            return;
        }
        out.insert(it->second);
    }

    void walk(const pugi::xml_node& node, tag_set& out) {
        for (const auto& child : node.children()) {
            const std::string_view kind = child.name();
            if (kind == "field") {
                add(child.attribute("name").value(), out);
            } else if (kind == "group") {
                add(child.attribute("name").value(), out);
                walk(child, out);
            } else if (kind == "component") {
                const std::string_view wanted = child.attribute("name").value();
                bool found = false;
                for (const auto& c : fix_.child("components").children("component")) {
                    if (wanted == c.attribute("name").value()) {
                        walk(c, out);
                        found = true;
                    }
                }
                EXPECT_TRUE(found) << "component '" << wanted << "' is not defined";
            }
        }
    }

    pugi::xml_document doc_;
    pugi::xml_node fix_;
    std::map<std::string, std::uint16_t> by_name_;
};

// FIX Latest's StandardHeader, from the Orchestra repository: the component's fieldRefs and the
// numInGroup and fieldRefs of each group it references. An Orchestra id is the tag.
tag_set orchestra_standard_header() {
    const auto path =
        std::filesystem::path{FIXPP_DICT_DATA_DIR} / "orchestra" / "OrchestraFIXLatest.xml";
    pugi::xml_document doc;
    const auto loaded = doc.load_file(path.string().c_str());
    EXPECT_TRUE(loaded) << "failed to load " << path;
    const auto repo = doc.child("fixr:repository");
    const auto header =
        repo.child("fixr:components").find_child_by_attribute("name", "StandardHeader");
    EXPECT_TRUE(header) << "no StandardHeader component";

    tag_set out;
    for (const auto& child : header.children()) {
        const std::string_view kind = child.name();
        if (kind == "fixr:fieldRef") {
            out.insert(static_cast<std::uint16_t>(child.attribute("id").as_uint()));
        } else if (kind == "fixr:groupRef") {
            const auto group = repo.child("fixr:groups")
                                   .find_child_by_attribute("id", child.attribute("id").value());
            EXPECT_TRUE(group) << "group " << child.attribute("id").value() << " is not defined";
            out.insert(static_cast<std::uint16_t>(
                group.child("fixr:numInGroup").attribute("id").as_uint()));
            for (const auto& member : group.children("fixr:fieldRef")) {
                out.insert(static_cast<std::uint16_t>(member.attribute("id").as_uint()));
            }
        } else if (kind == "fixr:componentRef") {
            ADD_FAILURE() << "StandardHeader references a component: extend this walk";
        }
    }
    return out;
}

std::string render(const tag_set& s) {
    std::string out;
    for (const auto t : s) {
        out += std::to_string(t) + " ";
    }
    return out;
}

TEST(W21StandardHeaderSet, ExactAgainstFixt11HeaderWith370TheOneNamedException) {
    const tag_set xml = HeaderWalk{"FIXT11.xml"}.header();
    ASSERT_TRUE(xml.contains(8) && xml.contains(35) && xml.contains(1128) && xml.contains(628))
        << "the walk did not reach the header: " << render(xml);
    ASSERT_FALSE(xml.contains(370)) << "370 joined FIXT11's header: it is no longer an exception";

    tag_set in_function_not_xml;
    tag_set in_xml_not_function;
    for (unsigned t = 0; t <= kMaxTag; ++t) {
        const auto tag = static_cast<std::uint16_t>(t);
        const bool answer = dd::is_fixt_standard_header_tag(tag);
        if (answer && !xml.contains(tag)) {
            in_function_not_xml.insert(tag);
        }
        if (!answer && xml.contains(tag)) {
            in_xml_not_function.insert(tag);
        }
    }
    EXPECT_EQ(in_function_not_xml, (tag_set{370})) << render(in_function_not_xml);
    EXPECT_EQ(in_xml_not_function, tag_set{}) << render(in_xml_not_function);
}

TEST(W21StandardHeaderSet, TrailerTagsAreOutOfIt) {
    EXPECT_FALSE(dd::is_fixt_standard_header_tag(10));
    EXPECT_FALSE(dd::is_fixt_standard_header_tag(89));
    EXPECT_FALSE(dd::is_fixt_standard_header_tag(93));
}

TEST(W21StandardHeaderSet, Fix4xHeadersUnionMinusFixt11IsExactly370) {
    tag_set fix4x;
    for (const char* file : {"FIX40.xml", "FIX41.xml", "FIX42.xml", "FIX43.xml", "FIX44.xml"}) {
        const tag_set one = HeaderWalk{file}.header();
        ASSERT_TRUE(one.contains(8) && one.contains(35)) << file << ": " << render(one);
        fix4x.insert(one.begin(), one.end());
    }
    const tag_set fixt = HeaderWalk{"FIXT11.xml"}.header();
    tag_set difference;
    for (const auto t : fix4x) {
        if (!fixt.contains(t)) {
            difference.insert(t);
        }
    }
    EXPECT_EQ(difference, (tag_set{370})) << render(difference);
}

TEST(W21StandardHeaderSet, FixLatestHeaderEqualsFixt11Header) {
    const tag_set latest = orchestra_standard_header();
    ASSERT_TRUE(latest.contains(8) && latest.contains(1128) && latest.contains(628))
        << "the walk did not reach the header: " << render(latest);
    EXPECT_EQ(latest, HeaderWalk{"FIXT11.xml"}.header())
        << "FIX Latest: " << render(latest)
        << "\nFIXT11: " << render(HeaderWalk{"FIXT11.xml"}.header());
}

TEST(W21AdminMsgType, TheSevenAdminAndNothingElse) {
    for (const char* mt : {"0", "1", "2", "3", "4", "5", "A"}) {
        EXPECT_TRUE(dd::is_fixt_admin_msgtype(mt)) << mt;
    }
    EXPECT_FALSE(dd::is_fixt_admin_msgtype("n")) << "XMLnonFIX is application";
    EXPECT_FALSE(dd::is_fixt_admin_msgtype("D"));
    EXPECT_FALSE(dd::is_fixt_admin_msgtype(""));
    EXPECT_FALSE(dd::is_fixt_admin_msgtype("AA"));
    EXPECT_FALSE(dd::is_fixt_admin_msgtype("0A"));

    std::string single(1, '\0');
    int admin = 0;
    for (int c = 0; c < 256; ++c) {
        single[0] = static_cast<char>(c);
        admin += dd::is_fixt_admin_msgtype(single) ? 1 : 0;
    }
    EXPECT_EQ(admin, 7);
}

TEST(W21RouteAnsweredHeaderTag, ExactlyTheFour) {
    tag_set answered;
    for (unsigned t = 0; t <= kMaxTag; ++t) {
        const auto tag = static_cast<std::uint16_t>(t);
        if (dd::is_route_answered_header_tag(tag)) {
            answered.insert(tag);
        }
    }
    EXPECT_EQ(answered, (tag_set{35, 1128, 1129, 1156})) << render(answered);
}

// data-model E-2: vt11 and Unknown map to Unknown, and each other version to its own.
TEST(W21SessionToApplication, EveryEnumerator) {
    using sv = dd::session_version;
    using av = dd::application_version;
    constexpr std::array<std::pair<sv, av>, 11> kRows{{
        {sv::Unknown, av::Unknown},
        {sv::v40, av::v40},
        {sv::v41, av::v41},
        {sv::v42, av::v42},
        {sv::v43, av::v43},
        {sv::v44, av::v44},
        {sv::v50, av::v50},
        {sv::v50sp1, av::v50sp1},
        {sv::v50sp2, av::v50sp2},
        {sv::vt11, av::Unknown},
        {sv::vlatest, av::vlatest},
    }};
    for (const auto& [from, to] : kRows) {
        EXPECT_EQ(dd::session_to_application(from), to) << static_cast<int>(from);
    }
}

// data-model E-1a: transport() answers the FIXT.1.1 dictionary, last writer wins, and get() is
// unchanged. The two FIXT11 loads are distinct objects, so the pointer tells them apart.
TEST(E1aTransport, TheFixtDictionaryLastWriterWins) {
    std::pmr::monotonic_buffer_resource mr;
    const auto path = std::filesystem::path{FIXPP_DICT_DATA_DIR};
    const auto fixt_a =
        std::make_shared<const dd::Dictionary>(dd::load_any(path / "FIXT11.xml", &mr));
    const auto fixt_b =
        std::make_shared<const dd::Dictionary>(dd::load_any(path / "FIXT11.xml", &mr));
    const auto fix44 =
        std::make_shared<const dd::Dictionary>(dd::load_any(path / "FIX44.xml", &mr));

    const dd::version_registry registry{{fixt_a, fix44, fixt_b}};
    ASSERT_TRUE(registry.transport().has_value());
    EXPECT_EQ(*registry.transport(), fixt_b.get());
    ASSERT_TRUE(registry.get(dd::application_version::v44).has_value());
    EXPECT_EQ(*registry.get(dd::application_version::v44), fix44.get());
    EXPECT_FALSE(registry.get(dd::application_version::Unknown).has_value())
        << "a vt11 dictionary is not an application version";
}

TEST(E1aTransport, NoFixtDictionaryIsTheNoDictionaryError) {
    std::pmr::monotonic_buffer_resource mr;
    const auto fix44 = std::make_shared<const dd::Dictionary>(
        dd::load_any(std::filesystem::path{FIXPP_DICT_DATA_DIR} / "FIX44.xml", &mr));
    const dd::version_registry with_fix44{{fix44}};
    ASSERT_FALSE(with_fix44.transport().has_value());
    EXPECT_EQ(with_fix44.transport().error(),
              fixpp::core::error::dict_no_dictionary_for_application_version);
    const dd::version_registry empty;
    ASSERT_FALSE(empty.transport().has_value());
    EXPECT_EQ(empty.transport().error(),
              fixpp::core::error::dict_no_dictionary_for_application_version);
}

}  // namespace
