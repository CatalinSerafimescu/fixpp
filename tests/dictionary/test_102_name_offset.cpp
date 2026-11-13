// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/test_102_name_offset.cpp — 102-dict-width-multichar-type, spec FR-001,
// FR-002, SC-001 (research D4 "name offset"; tasks T010).
//
// A component's own name offset is one at which the name-pool slice of the name's length equals
// the component's name. `ComponentRef::name_offset` has no public reader, so this reads the name
// pool through tests/support/dictionary_test_access.hpp. The expected name comes from the raw
// file: the `name` attribute of the component element at the record's `component_id`, which is
// the component's zero-based declaration index in both formats (spec FR-001a). It is never read
// from `Dictionary::component`'s lookup or from the pool itself.
//
// The bases are every dictionary file under dictionaries/ and dictionaries/orchestra/
// (re-derive: the `*.xml` files there); a base whose name pool holds its component names below
// 65536 bytes is a control for this cell.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/orchestra_loader.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <memory_resource>
#include <pugixml.hpp>
#include <string>
#include <string_view>
#include <vector>

#include "support/dictionary_test_access.hpp"

namespace {

struct base_file {
    char const* name;
    bool orchestra;
};

constexpr base_file kBases[] = {
    {"FIX40.xml", false},    {"FIX41.xml", false},    {"FIX42.xml", false},
    {"FIX43.xml", false},    {"FIX44.xml", false},    {"FIX50.xml", false},
    {"FIX50SP1.xml", false}, {"FIX50SP2.xml", false}, {"FIXT11.xml", false},
    {"OrchestraFIXLatest.xml", true},
};

std::filesystem::path path_of(base_file const& b) {
    return b.orchestra ? std::filesystem::path{FIXPP_ORCHESTRA_DATA_DIR} / b.name
                       : std::filesystem::path{FIXPP_DICT_DATA_DIR} / b.name;
}

// The component names in declaration order, read with pugixml from the raw file.
std::vector<std::string> raw_component_names(base_file const& b) {
    pugi::xml_document doc;
    EXPECT_TRUE(doc.load_file(path_of(b).c_str())) << b.name;
    std::vector<std::string> out;
    if (b.orchestra) {
        for (auto const& c :
             doc.child("fixr:repository").child("fixr:components").children("fixr:component")) {
            out.emplace_back(c.attribute("name").as_string(""));
        }
    } else {
        for (auto const& c : doc.child("fix").child("components").children("component")) {
            out.emplace_back(c.attribute("name").as_string(""));
        }
    }
    return out;
}

class NameOffset102 : public ::testing::TestWithParam<base_file> {};

}  // namespace

TEST_P(NameOffset102, EveryComponentNameSitsAtItsOffset) {
    base_file const& b = GetParam();
    std::pmr::monotonic_buffer_resource arena;
    fixpp::dict::Dictionary const d =
        b.orchestra ? fixpp::dict::OrchestraLoader{}.load(path_of(b), &arena)
                    : fixpp::dict::XmlLoader{}.load(path_of(b), &arena);
    auto const* h = fixpp::dict::dictionary_test_access::handle(d);
    ASSERT_NE(h, nullptr);
    std::vector<std::string> const names = raw_component_names(b);
    ASSERT_EQ(h->components_.size(), names.size()) << b.name << ": component count";

    std::size_t wrong = 0;
    std::string first_wrong;
    for (auto const& cr : h->components_) {
        std::uint64_t const id = cr.component_id;
        ASSERT_LT(id, names.size()) << b.name << ": component_id past the raw component list";
        std::string const& expected = names[id];
        std::uint64_t const offset = cr.name_offset;
        std::string_view got;
        if (offset + expected.size() <= h->name_pool_.size()) {
            got = std::string_view{h->name_pool_.data() + offset, expected.size()};
        }
        if (got != expected) {
            if (wrong == 0) {
                first_wrong = "component " + std::to_string(id) + " \"" + expected +
                              "\": the pool at offset " + std::to_string(offset) + " reads \"" +
                              std::string{got} + "\"";
            }
            ++wrong;
        }
    }
    EXPECT_EQ(wrong, 0U) << b.name << ": components whose name is not at their name_offset; first: "
                         << first_wrong;
}

INSTANTIATE_TEST_SUITE_P(VendoredBases, NameOffset102, ::testing::ValuesIn(kBases),
                         [](::testing::TestParamInfo<base_file> const& info) {
                             std::string n{info.param.name};
                             std::erase(n, '.');
                             return n;
                         });
