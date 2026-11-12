// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/dictionary/test_101_tag_defined.cpp
//
// 101-inbound-reject-fidelity, tasks.md T029 (W101-15): `table_view::tag_defined`, the defined-tag
// store of research D1. On a dictionary-built table a tag is defined when the dictionary declares
// it as a field, whether or not any message reaches it; on a hand-built table, when any
// population operation named it.
//
// The expected set of a shipped dictionary is read by an independent walk of its source file
// (a regex over the field definitions), never through the loader the code under test uses, so a
// field the fill drops or a tag it invents is caught across the whole tag space.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fixpp/core/length_data_pairs.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/field_type.hpp>
#include <fixpp/dict/load_any.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <memory_resource>
#include <ostream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "fixt_framing_table.hpp"  // kFixtFramingTable (src/dictionary)
#include "support/table_view_test_access.hpp"

namespace {

namespace dd = fixpp::dict;
using dd::field_type;
using dd::table_view;
using dd::table_view_builder;

std::string slurp(const std::filesystem::path& p) {
    std::ifstream in{p, std::ios::binary};
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

// The independent walk: every field definition's number in the source. QuickFIX XML writes a
// definition as `<field number=… name=… type=…>` (a message's reference has no number);
// Orchestra writes `<fixr:field id=…>` (a reference is `<fixr:fieldRef …>`).
std::set<std::uint16_t> field_definitions(const std::string& xml, bool orchestra) {
    const std::regex re = orchestra ? std::regex{R"(<fixr:field\b[^>]*\bid=['"](\d+)['"])"}
                                    : std::regex{R"(<field\s+number=['"](\d+)['"])"};
    std::set<std::uint16_t> out;
    for (std::sregex_iterator it{xml.begin(), xml.end(), re}, end; it != end; ++it) {
        out.insert(static_cast<std::uint16_t>(std::stoul((*it)[1].str())));
    }
    return out;
}

std::set<std::uint16_t> framing_tags() {
    std::set<std::uint16_t> out;
    for (auto const& e : dd::detail::kFixtFramingTable) {
        out.insert(e.tag);
    }
    return out;
}

// Every tag in [1, 65535] answers as `expected` says; the first mismatches are reported.
void expect_defined_set(const table_view& tv, const std::set<std::uint16_t>& expected) {
    ASSERT_FALSE(expected.empty()) << "the walk found no field definitions";
    std::vector<std::uint32_t> missing;
    std::vector<std::uint32_t> extra;
    for (std::uint32_t t = 1; t <= std::numeric_limits<std::uint16_t>::max(); ++t) {
        const auto tag = static_cast<std::uint16_t>(t);
        const bool want = expected.contains(tag);
        if (tv.tag_defined(tag) != want) {
            (want ? missing : extra).push_back(t);
        }
    }
    auto head = [](const std::vector<std::uint32_t>& v) {
        std::string s;
        for (std::size_t i = 0; i < v.size() && i < 10; ++i) {
            s += std::to_string(v[i]) + " ";
        }
        return s;
    };
    EXPECT_TRUE(missing.empty()) << missing.size()
                                 << " declared tags not defined, first: " << head(missing);
    EXPECT_TRUE(extra.empty()) << extra.size()
                               << " undeclared tags defined, first: " << head(extra);
}

const std::filesystem::path kDicts{FIXPP_DICT_DATA_DIR};
const std::filesystem::path kOrch{FIXPP_ORCHESTRA_DATA_DIR};

dd::Dictionary load(const std::filesystem::path& p) {
    static std::pmr::unsynchronized_pool_resource mr;
    return dd::load_any(p, &mr);
}

// Each shipped dictionary the cells read is loaded once for the binary, whichever cell reads it
// first; each view form below is built once from it; every view is `as_table_view`'s result as
// returned.
const dd::Dictionary& shipped(const std::filesystem::path& p) {
    static std::map<std::filesystem::path, std::unique_ptr<const dd::Dictionary>> cache;
    auto& slot = cache[p];
    if (!slot) {
        slot = std::make_unique<const dd::Dictionary>(load(p));
    }
    return *slot;
}
const dd::Dictionary& fix44() { return shipped(kDicts / "FIX44.xml"); }
const dd::Dictionary& fix50sp2() { return shipped(kDicts / "FIX50SP2.xml"); }
const dd::Dictionary& fix_latest() { return shipped(kOrch / "OrchestraFIXLatest.xml"); }
const table_view& fix44_view() {
    static const table_view tv = fix44().as_table_view();
    return tv;
}
const table_view& fix50sp2_route_view() {
    static const table_view tv = fix50sp2().as_table_view(dd::fixt_framing);
    return tv;
}
const table_view& fix_latest_view() {
    static const table_view tv = fix_latest().as_table_view();
    return tv;
}

// ── Dictionary-built tables: each as_table_view form ────────────────────────────────────

TEST(TagDefined101, Fix44_PlainForm_DefinesExactlyTheFieldDefinitions) {
    const auto defs = field_definitions(slurp(kDicts / "FIX44.xml"), false);
    ASSERT_TRUE(defs.contains(55));
    ASSERT_FALSE(defs.contains(1128)) << "precondition: FIX 4.4 declares no ApplVerID";
    expect_defined_set(fix44_view(), defs);
}

TEST(TagDefined101, Fix50sp2_RouteForm_DefinesTheFieldsAndTheFramingTags) {
    auto expected = field_definitions(slurp(kDicts / "FIX50SP2.xml"), false);
    const auto framing = framing_tags();
    expected.insert(framing.begin(), framing.end());
    expect_defined_set(fix50sp2_route_view(), expected);
}

TEST(TagDefined101, Fix50sp2_RouteFormWithExtra_DefinesTheExtraPairToo) {
    auto expected = field_definitions(slurp(kDicts / "FIX50SP2.xml"), false);
    ASSERT_FALSE(expected.contains(5030)) << "precondition: FIX50SP2 does not declare 5030";
    ASSERT_FALSE(expected.contains(5031)) << "precondition: FIX50SP2 does not declare 5031";
    const auto framing = framing_tags();
    expected.insert(framing.begin(), framing.end());
    expected.insert({5030, 5031});
    const std::array<fixpp::core::detail::length_data_pair, 1> extra{
        {{.length_tag = 5030, .data_tag = 5031}}};
    expect_defined_set(fix50sp2().as_table_view(dd::fixt_framing, extra), expected);
}

TEST(TagDefined101, FixLatest_DefinesExactlyTheFieldDefinitions) {
    const auto defs = field_definitions(slurp(kOrch / "OrchestraFIXLatest.xml"), true);
    ASSERT_TRUE(defs.contains(3115)) << "precondition: FIX Latest declares 3115";
    expect_defined_set(fix_latest_view(), defs);
}

// ── Every shipped dictionary, by a listing of the dictionary directories ────────────────
//
// The cells are the directories' listing, so a dictionary added to either directory is
// checked without a new cell. Each `*.xml` of `FIXPP_DICT_DATA_DIR` (FIXT11 included: it
// loads on its own, as a session-layer dictionary) and of `FIXPP_ORCHESTRA_DATA_DIR` is read
// in plain form; the 5.0 family (a QuickFIX XML file named `FIX50*`) also in route form. The
// 5.0 family's set, in both forms, is the field definitions plus the FIXT framing tags: the
// framing surface `Dictionary::build_table_view` adds for those versions marks its tags (its
// `which_session_version()` switch). This is the check that a registration-only record store
// (table_view.hpp's `add_occurrence_group`, `add_section_group`, which do not mark) loses no
// tag on a shipped dictionary: each cell holds only when the defined set equals the source's
// field definitions.

struct ShippedCase {
    std::filesystem::path file;
    bool orchestra = false;
    bool route = false;
};

void PrintTo(const ShippedCase& c, std::ostream* os) {
    *os << c.file.filename().string() << (c.route ? " (route form)" : "");
}

std::vector<ShippedCase> shipped_cases() {
    std::vector<ShippedCase> out;
    for (auto const& [dir, orchestra] : {std::pair{kDicts, false}, std::pair{kOrch, true}}) {
        std::vector<std::filesystem::path> files;
        for (auto const& e : std::filesystem::directory_iterator{dir}) {
            if (e.is_regular_file() && e.path().extension() == ".xml") {
                files.push_back(e.path());
            }
        }
        std::ranges::sort(files);
        for (auto const& f : files) {
            out.push_back({.file = f, .orchestra = orchestra});
            if (!orchestra && f.filename().string().starts_with("FIX50")) {
                out.push_back({.file = f, .orchestra = orchestra, .route = true});
            }
        }
    }
    return out;
}

// Known positive of the listing: it reaches the oldest and the newest QuickFIX dictionaries,
// FIXT11 and the Orchestra file, and the route form of FIX50SP2.
TEST(TagDefined101, ShippedListing_ReachesTheOldAndTheNewDictionaries) {
    std::set<std::string> plain;
    std::set<std::string> route;
    for (auto const& c : shipped_cases()) {
        (c.route ? route : plain).insert(c.file.filename().string());
    }
    for (const char* f : {"FIX40.xml", "FIX50SP2.xml", "FIXT11.xml", "OrchestraFIXLatest.xml"}) {
        EXPECT_TRUE(plain.contains(f)) << f << " is not in the listing";
    }
    EXPECT_TRUE(route.contains("FIX50SP2.xml")) << "FIX50SP2's route form is not in the listing";
    EXPECT_FALSE(route.contains("FIX44.xml")) << "only the 5.0 family is read in route form";
}

class TagDefined101Shipped : public testing::TestWithParam<ShippedCase> {};

TEST_P(TagDefined101Shipped, DefinesExactlyTheFieldDefinitions) {
    const ShippedCase& c = GetParam();
    auto expected = field_definitions(slurp(c.file), c.orchestra);
    if (!c.orchestra && c.file.filename().string().starts_with("FIX50")) {
        const auto framing = framing_tags();
        expected.insert(framing.begin(), framing.end());
    }
    const dd::Dictionary& d = shipped(c.file);
    expect_defined_set(c.route ? d.as_table_view(dd::fixt_framing) : d.as_table_view(), expected);
}

INSTANTIATE_TEST_SUITE_P(
    TagDefined101, TagDefined101Shipped, testing::ValuesIn(shipped_cases()),
    [](const testing::TestParamInfo<ShippedCase>& info) {
        std::string name = info.param.file.stem().string();
        std::ranges::replace_if(
            name, [](char ch) { return !std::isalnum(static_cast<unsigned char>(ch)); }, '_');
        return name + (info.param.route ? "_Route" : "_Plain");
    });

// ── The seal writes the store at exact size ─────────────────────────────────────────────
//
// A view returned by as_table_view() is moved out of the builder, so it keeps whatever
// capacity the seal leaves; a Session and each FIXT route hold that moved view. The seal
// writes the store in one allocation of the defined tags' count, so a dictionary-built view's
// store has capacity equal to its size.

void expect_store_compact(const table_view& tv) {
    const auto& store = dd::table_view_test_access::defined_tags(tv);
    ASSERT_FALSE(store.empty()) << "precondition: the store holds the dictionary's tags";
    EXPECT_EQ(store.capacity(), store.size())
        << "a moved-out view keeps the capacity the seal leaves";
}

TEST(TagDefined101, SealReleasesExcessCapacity_Fix44) { expect_store_compact(fix44_view()); }

TEST(TagDefined101, SealReleasesExcessCapacity_FixtRouteFix50sp2) {
    expect_store_compact(fix50sp2_route_view());
}

TEST(TagDefined101, SealReleasesExcessCapacity_FixLatest) {
    expect_store_compact(fix_latest_view());
}

// A field a 098 overlay adds without a message that reaches it, and one at a high tag.
TEST(TagDefined101, Fix44Overlay_AddedFieldsAreDefined) {
    static std::pmr::unsynchronized_pool_resource mr;
    const std::string base = slurp(kDicts / "FIX44.xml");
    const dd::Dictionary d = dd::XmlLoader{}.load_with_overlay_from_string(
        base,
        "<fix type='FIX' major='4' minor='4'><fields>"
        "<field number='5001' name='Custom101A' type='STRING' />"
        "<field number='65000' name='Custom101High' type='STRING' />"
        "</fields></fix>",
        &mr);
    auto expected = field_definitions(base, false);
    ASSERT_FALSE(expected.contains(5001)) << "precondition: the base does not declare 5001";
    ASSERT_FALSE(expected.contains(65000)) << "precondition: the base does not declare 65000";
    expected.insert({5001, 65000});
    expect_defined_set(d.as_table_view(), expected);
}

// ── Hand-built tables: every population operation defines the tags it names ─────────────

constexpr std::uint16_t kTag = 6050;
constexpr std::uint16_t kOther = 6051;
constexpr std::uint16_t kNoTag = 6100;

struct OpCase {
    const char* op;
    std::function<void(table_view_builder&)> apply;
};

void PrintTo(const OpCase& c, std::ostream* os) { *os << c.op; }

std::vector<OpCase> op_cases() {
    const std::array<std::uint16_t, 0> root{};
    return {
        {"a_add_valid", [](table_view_builder& b) { b.add_valid("Z", kTag); }},
        {"a_add_valid_tag", [](table_view_builder& b) { b.add_valid_tag("Z", kTag); }},
        {"b_add_required", [](table_view_builder& b) { b.add_required("Z", kTag); }},
        {"b_add_required_tag", [](table_view_builder& b) { b.add_required_tag("Z", kTag); }},
        {"c_set_field_type",
         [](table_view_builder& b) { b.set_field_type(kTag, field_type::String); }},
        {"c_set_type", [](table_view_builder& b) { b.set_type(kTag, field_type::String); }},
        {"d_add_enum", [](table_view_builder& b) { b.add_enum(kTag, "v"); }},
        {"e_set_multi_value", [](table_view_builder& b) { b.set_multi_value(kTag); }},
        {"f_add_group_member", [](table_view_builder& b) { b.add_group_member(kNoTag, kTag); }},
        {"f_add_group_required_member",
         [](table_view_builder& b) { b.add_group_required_member(kNoTag, kTag); }},
        {"f_set_group_first", [](table_view_builder& b) { b.set_group_first(kNoTag, kTag); }},
        {"g_add_group_member_ctx",
         [root](table_view_builder& b) { b.add_group_member_ctx("Z", root, kNoTag, kTag); }},
        {"g_add_group_required_member_ctx",
         [root](table_view_builder& b) {
             b.add_group_required_member_ctx("Z", root, kNoTag, kTag);
         }},
        {"g_set_group_first_ctx",
         [root](table_view_builder& b) { b.set_group_first_ctx("Z", root, kNoTag, kTag); }},
        {"h_add_header_tag", [](table_view_builder& b) { b.add_header_tag(kTag); }},
        {"i_add_fixt_framing_tag",
         [](table_view_builder& b) { b.add_fixt_framing_tag(kTag, field_type::String); }},
        {"j_set_length_pair_data_tag_length",
         [](table_view_builder& b) { b.set_length_pair_data_tag(kTag, kOther); }},
        {"k_set_length_pair_data_tag_data",
         [](table_view_builder& b) { b.set_length_pair_data_tag(kOther, kTag); }},
        {"l_add_defined_tag", [](table_view_builder& b) { b.add_defined_tag(kTag); }},
    };
}

class TagDefined101Op : public testing::TestWithParam<OpCase> {};

TEST_P(TagDefined101Op, TheOperationDefinesItsTag) {
    table_view_builder b;
    GetParam().apply(b);
    const table_view tv = std::move(b).build();
    EXPECT_TRUE(tv.tag_defined(kTag));
    EXPECT_FALSE(tv.tag_defined(kTag + 100)) << "a tag no operation named";
}

INSTANTIATE_TEST_SUITE_P(TagDefined101, TagDefined101Op, testing::ValuesIn(op_cases()),
                         [](const testing::TestParamInfo<OpCase>& info) {
                             return std::string(info.param.op);
                         });

TEST(TagDefined101, EmptyTable_DefinesNothing) {
    const table_view tv = table_view_builder{}.build();
    for (std::uint16_t t :
         {std::uint16_t{1}, std::uint16_t{55}, kTag, std::numeric_limits<std::uint16_t>::max()}) {
        EXPECT_FALSE(tv.tag_defined(t)) << t;
    }
}

// The seal: a population in descending order, each tag named twice, still answers by the store's
// binary search, and a copy carries the store.
TEST(TagDefined101, DescendingRepeatedPopulation_AnswersEveryTag) {
    table_view_builder b;
    std::set<std::uint16_t> named;
    for (int pass = 0; pass < 2; ++pass) {
        for (int t = 7000; t >= 6000; t -= 7) {
            b.add_valid("Z", static_cast<std::uint16_t>(t));
            named.insert(static_cast<std::uint16_t>(t));
        }
    }
    const table_view tv = std::move(b).build();
    const table_view copy{tv};  // NOLINT(performance-unnecessary-copy-initialization)
    int mismatches = 0;
    for (int t = 5990; t <= 7010; ++t) {
        const auto tag = static_cast<std::uint16_t>(t);
        if (tv.tag_defined(tag) != named.contains(tag)) {
            ++mismatches;
        }
        if (copy.tag_defined(tag) != named.contains(tag)) {
            ++mismatches;
        }
    }
    EXPECT_EQ(mismatches, 0);
}

}  // namespace
