// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/dictionary/test_099_fixt_framing_table_view.cpp
//
// 099-fixt-appl-version-validation, contracts/witnesses.md W-17. Its no-argument half (tasks.md
// T006): a canonical dump of every store `table_view_test_access` reads, hashed per vendored file
// against a literal. SC-005 and NFR-001's "plain as_table_view() unchanged" rest on it, and M-43 is
// its mutant. The dump is sorted text, so it is the same bytes on every platform, and the hash
// is FNV-1a 64, whose value its definition fixes, unlike std::hash.
//
// To re-derive a literal, run with FIXPP_099_PRINT_DUMP=<dir>: each file's dump lands there. A
// literal moves only by the task that says so (research R-17's last row, for FIXT11.xml).
//
// Its overload half (tasks.md T024): data-model E-3's preservation contract on every vendored file,
// and FIX.4.4 strict validation still rejecting 1128. Its mutants are M-42, M-43, M-48 and M-73.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fixpp/core/error.hpp>
#include <fixpp/core/length_data_pairs.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/load_any.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/dict/version_profile.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/wire/parser.hpp>
#include <fixpp/wire/validator.hpp>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <memory_resource>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "dictionary_internal.hpp"  // as_table_view_call_count, the seam W-15 also reads
#include "fixt_framing_table.hpp"   // kFixtFramingTable
#include "support/frame_view_factory.hpp"
#include "support/table_view_test_access.hpp"

namespace {

namespace dd = fixpp::dict;
using access = dd::table_view_test_access;

// FNV-1a 64: offset basis and prime as the algorithm defines them.
[[nodiscard]] std::uint64_t fnv1a64(std::string_view bytes) {
    std::uint64_t h = 0xcbf29ce484222325ULL;
    for (const unsigned char c : bytes) {
        h ^= c;
        h *= 0x00000100000001b3ULL;
    }
    return h;
}

std::string hex64(std::uint64_t v) {
    std::ostringstream os;
    os << std::hex << v;
    return os.str();
}

// A sorted list of a store's rendered entries, one per line, prefixed by the store's name, so
// that an entry moved between stores moves the dump. Sorting removes the stores' own orders.
void emit(std::string& out, std::string_view store, std::vector<std::string> lines) {
    std::ranges::sort(lines);
    out += store;
    out += " count=";
    out += std::to_string(lines.size());
    out += '\n';
    for (const auto& l : lines) {
        out += l;
        out += '\n';
    }
}

std::string join_sorted(auto const& tags) {
    std::vector<unsigned> v(tags.begin(), tags.end());
    std::ranges::sort(v);
    std::string s;
    for (unsigned t : v) {
        s += std::to_string(t);
        s += ',';
    }
    return s;
}

// A list store's order is part of what it says, so it stays as stored.
std::string join_in_order(auto const& tags) {
    std::string s;
    for (unsigned t : tags) {
        s += std::to_string(t);
        s += ',';
    }
    return s;
}

template <class Store>
std::vector<std::string> msg_set_lines(Store const& store) {
    std::vector<std::string> lines;
    lines.reserve(store.size());
    for (auto const& [key, value] : store) {
        lines.push_back(std::string(key) + ":" + join_sorted(value));
    }
    return lines;
}

template <class Store>
std::vector<std::string> msg_list_lines(Store const& store) {
    std::vector<std::string> lines;
    lines.reserve(store.size());
    for (auto const& [key, value] : store) {
        lines.push_back(std::string(key) + ":" + join_in_order(value));
    }
    return lines;
}

template <class Store>
std::vector<std::string> tag_value_lines(Store const& store) {
    std::vector<std::string> lines;
    lines.reserve(store.size());
    for (auto const& [key, value] : store) {
        lines.push_back(std::to_string(key) + ":" + std::to_string(static_cast<unsigned>(value)));
    }
    return lines;
}

template <class Store>
std::vector<std::string> tag_list_lines(Store const& store) {
    std::vector<std::string> lines;
    lines.reserve(store.size());
    for (auto const& [key, value] : store) {
        lines.push_back(std::to_string(key) + ":" + join_in_order(value));
    }
    return lines;
}

std::vector<std::string> word_lines(std::vector<std::uint64_t> const& words) {
    std::vector<std::string> lines;
    for (std::size_t i = 0; i < words.size(); ++i) {
        if (words[i] != 0) {
            lines.push_back(std::to_string(i) + ":" + hex64(words[i]));
        }
    }
    return lines;
}

template <class Store>
std::vector<std::string> enum_lines(Store const& store) {
    std::vector<std::string> lines;
    for (auto const& [tag, domain] : store) {
        std::string s = std::to_string(tag) + ":multi=" + (domain.multi_value ? "1" : "0") + ":";
        for (auto const& code : domain.codes) {
            s += code;
            s += ',';
        }
        lines.push_back(std::move(s));
    }
    return lines;
}

template <class Store>
std::vector<std::string> group_ctx_lines(Store const& store) {
    std::vector<std::string> lines;
    for (auto const& [key, entry] : store) {
        std::string s = key.msg_type + "/depth=" + std::to_string(key.depth) + "/path=";
        for (std::size_t i = 0; i < key.depth; ++i) {
            s += std::to_string(key.parent_path[i]);
            s += '.';
        }
        s += "/no=" + std::to_string(key.no_tag) + " first=" + std::to_string(entry.group_first) +
             " members=" + join_in_order(entry.members) +
             " required=" + join_in_order(entry.required_members);
        lines.push_back(std::move(s));
    }
    return lines;
}

// The canonical dump of a table: every store `table_view_test_access` reads, one call each. Add a
// store here when that header gains one.
std::string canonical_dump(dd::table_view const& tv) {
    std::string out;
    emit(out, "valid", msg_set_lines(access::valid(tv)));
    emit(out, "required", msg_list_lines(access::required(tv)));
    emit(out, "group_first", tag_value_lines(access::group_first(tv)));
    emit(out, "group_members", tag_list_lines(access::group_members(tv)));
    emit(out, "group_required_members", tag_list_lines(access::group_required_members(tv)));
    emit(out, "group_bits", word_lines(access::group_bits(tv)));
    emit(out, "group_ctx", group_ctx_lines(access::group_ctx(tv)));
    emit(out, "types", tag_value_lines(access::types(tv)));
    emit(out, "enum_bits", word_lines(access::enum_bits(tv)));
    emit(out, "enums", enum_lines(access::enums(tv)));
    {
        std::vector<std::string> tags;
        for (const auto t : access::fixt_framing_tags(tv)) {
            tags.push_back(std::to_string(t));
        }
        emit(out, "fixt_framing_tags", std::move(tags));
    }
    emit(out, "fixt_framing_types", tag_value_lines(access::fixt_framing_types(tv)));
    emit(out, "length_pair_data_tag", tag_value_lines(access::length_pair_data_tag(tv)));
    emit(out, "data_pair_length_tag", tag_value_lines(access::data_pair_length_tag(tv)));
    emit(out, "has_nonstandard_pair", {access::has_nonstandard_pair(tv) ? "1" : "0"});
    emit(out, "header_bits", word_lines(access::header_bits(tv)));
    return out;
}

struct VendoredHash {
    const char* file;
    std::uint64_t literal;
};

// One row per file of dictionaries/, every file a loader reads. A literal is its file's
// canonical dump's FNV-1a 64, recorded by tasks.md T006.
const VendoredHash kVendored[] = {
    {.file = "FIX40.xml", .literal = 0xe84640c9433acba2ULL},
    {.file = "FIX41.xml", .literal = 0x7c58f6abfe64a108ULL},
    {.file = "FIX42.xml", .literal = 0xe5739dda716d8732ULL},
    {.file = "FIX43.xml", .literal = 0x6e4beab588e9e5d1ULL},
    {.file = "FIX44.xml", .literal = 0x645442c49557393aULL},
    {.file = "FIX50.xml", .literal = 0xe6be16b585a12850ULL},
    {.file = "FIX50SP1.xml", .literal = 0x33af764d534a765dULL},
    {.file = "FIX50SP2.xml", .literal = 0x85e0014214253c0bULL},
    {.file = "FIXT11.xml", .literal = 0x99a62ff60a54c244ULL},
    {.file = "orchestra/OrchestraFIXLatest.xml", .literal = 0xd55ac82e9e1883baULL},
};

class W17NoArgument : public testing::TestWithParam<VendoredHash> {};

TEST_P(W17NoArgument, CanonicalDumpHash) {
    const VendoredHash& c = GetParam();
    std::pmr::monotonic_buffer_resource mr;
    const dd::Dictionary d = dd::load_any(std::filesystem::path{FIXPP_DICT_DATA_DIR} / c.file, &mr);
    const dd::table_view tv = d.as_table_view();
    const std::string dump = canonical_dump(tv);
    ASSERT_GT(dump.size(), 1000U) << "the dump read nothing, so its hash would say nothing";
    if (const char* dir = std::getenv("FIXPP_099_PRINT_DUMP")) {
        const std::filesystem::path p =
            std::filesystem::path{dir} / std::filesystem::path{c.file}.filename();
        std::ofstream{p, std::ios::binary} << dump;
    }
    EXPECT_EQ(hex64(fnv1a64(dump)), hex64(c.literal)) << c.file;
}

INSTANTIATE_TEST_SUITE_P(W17, W17NoArgument, testing::ValuesIn(kVendored),
                         [](const testing::TestParamInfo<VendoredHash>& i) {
                             std::string n = std::filesystem::path{i.param.file}.stem().string();
                             return n;
                         });

// The row set is every file of the directory: a vendored file added later fails here until it
// has a literal.
TEST(W17NoArgumentRows, EveryVendoredFileHasARow) {
    std::set<std::string> on_disk;
    const std::filesystem::path root{FIXPP_DICT_DATA_DIR};
    for (auto const& e : std::filesystem::recursive_directory_iterator(root)) {
        if (e.path().extension() == ".xml") {
            on_disk.insert(std::filesystem::relative(e.path(), root).generic_string());
        }
    }
    std::set<std::string> rows;
    for (auto const& r : kVendored) {
        rows.insert(r.file);
    }
    EXPECT_EQ(on_disk, rows);
}

// ── The overload's half (tasks.md T024): data-model E-3's preservation contract ──────────────

// One load per vendored file per process: each cell below builds its own tables from it.
const dd::Dictionary& vendored(const char* file) {
    static std::pmr::monotonic_buffer_resource mr;
    static std::map<std::string, std::unique_ptr<const dd::Dictionary>> cache;
    auto& slot = cache[file];
    if (!slot) {
        slot = std::make_unique<const dd::Dictionary>(
            dd::load_any(std::filesystem::path{FIXPP_DICT_DATA_DIR} / file, &mr));
    }
    return *slot;
}

// A canonical dump's sections, keyed by store, so that every store it covers is compared and a
// store added to it later is compared with no edit here.
std::map<std::string, std::string> by_store(const std::string& dump) {
    std::map<std::string, std::string> out;
    std::istringstream in{dump};
    std::string line;
    std::string current;
    while (std::getline(in, line)) {
        if (const auto at = line.find(" count="); at != std::string::npos) {
            current = line.substr(0, at);
        }
        out[current] += line;
        out[current] += '\n';
    }
    return out;
}

constexpr std::uint16_t kNoHops = 627;

const std::set<std::string>& kFramingStores() {
    static const std::set<std::string> value{"fixt_framing_tags", "fixt_framing_types"};
    return value;
}
const std::set<std::string>& kBareGroupStores() {
    static const std::set<std::string> value{"group_first", "group_members", "group_bits"};
    return value;
}
const std::set<std::string>& kPairStores() {
    static const std::set<std::string> value{"length_pair_data_tag", "data_pair_length_tag",
                                             "has_nonstandard_pair"};
    return value;
}

std::vector<std::uint64_t> without_bit(std::vector<std::uint64_t> words, std::uint16_t tag) {
    if (const std::size_t w = tag >> 6U; w < words.size()) {
        words[w] &= ~(std::uint64_t{1} << (tag & 63U));
    }
    return words;
}

bool has_bit(const std::vector<std::uint64_t>& words, std::uint16_t tag) {
    const std::size_t w = tag >> 6U;
    return w < words.size() && ((words[w] >> (tag & 63U)) & 1U) != 0;
}

class W17Overload : public testing::TestWithParam<VendoredHash> {};

TEST_P(W17Overload, EveryStoreEqualButTheNamedExceptions) {
    const dd::Dictionary& d = vendored(GetParam().file);
    const dd::table_view plain = d.as_table_view();
    const dd::table_view framed = d.as_table_view(dd::fixt_framing);
    const auto plain_stores = by_store(canonical_dump(plain));
    const auto framed_stores = by_store(canonical_dump(framed));
    ASSERT_GT(plain_stores.size(), 10U) << "the section split read nothing";
    ASSERT_EQ(plain_stores.size(), framed_stores.size());

    const bool own_header_declares = plain.is_header_tag(kNoHops);
    for (const auto& [store, text] : plain_stores) {
        if (kFramingStores().contains(store)) {
            continue;
        }
        if (!own_header_declares && kBareGroupStores().contains(store)) {
            continue;  // the next cell reads these, entry by entry
        }
        EXPECT_EQ(framed_stores.at(store), text) << GetParam().file << ": store " << store;
    }
}

TEST_P(W17Overload, NoHopsMetadata_ExactlyOnFilesWhoseHeaderDoesNotDeclareIt) {
    const dd::Dictionary& d = vendored(GetParam().file);
    const dd::table_view plain = d.as_table_view();
    const dd::table_view framed = d.as_table_view(dd::fixt_framing);
    if (plain.is_header_tag(kNoHops)) {
        EXPECT_EQ(access::group_first(framed), access::group_first(plain));
        EXPECT_EQ(access::group_members(framed), access::group_members(plain));
        EXPECT_EQ(access::group_bits(framed), access::group_bits(plain));
        return;
    }
    ASSERT_FALSE(access::group_first(plain).contains(kNoHops));
    ASSERT_FALSE(access::group_members(plain).contains(kNoHops));
    ASSERT_FALSE(has_bit(access::group_bits(plain), kNoHops));

    ASSERT_TRUE(access::group_first(framed).contains(kNoHops))
        << GetParam().file << ": no NoHops metadata, so a bad count would pass";
    EXPECT_EQ(access::group_first(framed).at(kNoHops), 628);
    ASSERT_TRUE(access::group_members(framed).contains(kNoHops));
    EXPECT_EQ(std::vector<std::uint16_t>(access::group_members(framed).at(kNoHops).begin(),
                                         access::group_members(framed).at(kNoHops).end()),
              (std::vector<std::uint16_t>{628, 629, 630}));
    EXPECT_TRUE(has_bit(access::group_bits(framed), kNoHops));
    EXPECT_TRUE(framed.group_required_members(kNoHops).empty()) << "none required";

    auto first = access::group_first(framed);
    first.erase(kNoHops);
    EXPECT_EQ(first, access::group_first(plain));
    auto members = access::group_members(framed);
    members.erase(kNoHops);
    EXPECT_EQ(members, access::group_members(plain));
    EXPECT_EQ(word_lines(without_bit(access::group_bits(framed), kNoHops)),
              word_lines(access::group_bits(plain)));
}

TEST_P(W17Overload, FramingStores_ByVersion) {
    const dd::Dictionary& d = vendored(GetParam().file);
    const dd::table_view plain = d.as_table_view();
    const dd::table_view framed = d.as_table_view(dd::fixt_framing);
    // FIX 5.0's own surface is the comparand: as_table_view() fills it, unchanged by this feature.
    const dd::table_view fix50 = vendored("FIX50.xml").as_table_view();
    ASSERT_FALSE(access::fixt_framing_tags(fix50).empty());

    using sv = dd::session_version;
    switch (d.which_session_version()) {
        case sv::vt11:
        case sv::vlatest:
            EXPECT_TRUE(access::fixt_framing_tags(plain).empty());
            EXPECT_TRUE(access::fixt_framing_tags(framed).empty());
            EXPECT_TRUE(access::fixt_framing_types(plain).empty());
            EXPECT_TRUE(access::fixt_framing_types(framed).empty());
            break;
        case sv::v40:
        case sv::v41:
        case sv::v42:
        case sv::v43:
        case sv::v44:
            EXPECT_TRUE(access::fixt_framing_tags(plain).empty());
            EXPECT_TRUE(access::fixt_framing_types(plain).empty());
            EXPECT_EQ(access::fixt_framing_tags(framed), access::fixt_framing_tags(fix50));
            EXPECT_EQ(access::fixt_framing_types(framed), access::fixt_framing_types(fix50));
            break;
        case sv::v50:
        case sv::v50sp1:
        case sv::v50sp2:
            EXPECT_EQ(access::fixt_framing_tags(framed), access::fixt_framing_tags(plain));
            EXPECT_EQ(access::fixt_framing_types(framed), access::fixt_framing_types(plain));
            EXPECT_EQ(access::fixt_framing_tags(plain), access::fixt_framing_tags(fix50));
            break;
        case sv::Unknown:
            FAIL() << GetParam().file << ": a vendored file with no version";
    }
}

TEST_P(W17Overload, ExtraPairsFormDiffersInThePairStoresOnly) {
    const dd::Dictionary& d = vendored(GetParam().file);
    const dd::table_view framed = d.as_table_view(dd::fixt_framing);
    const std::array<fixpp::core::detail::length_data_pair, 1> extra{
        {{.length_tag = 5030, .data_tag = 5031}}};
    const dd::table_view paired = d.as_table_view(dd::fixt_framing, extra);

    const auto framed_stores = by_store(canonical_dump(framed));
    const auto paired_stores = by_store(canonical_dump(paired));
    ASSERT_EQ(framed_stores.size(), paired_stores.size());
    for (const auto& [store, text] : framed_stores) {
        if (!kPairStores().contains(store)) {
            EXPECT_EQ(paired_stores.at(store), text) << GetParam().file << ": store " << store;
        }
    }

    EXPECT_FALSE(framed.has_nonstandard_pair());
    EXPECT_TRUE(paired.has_nonstandard_pair());
    auto forward = access::length_pair_data_tag(framed);
    forward[5030] = 5031;
    EXPECT_EQ(access::length_pair_data_tag(paired), forward);
    auto inverse = access::data_pair_length_tag(framed);
    inverse[5031] = 5030;
    EXPECT_EQ(access::data_pair_length_tag(paired), inverse);

    const dd::table_view no_extra =
        d.as_table_view(dd::fixt_framing, std::span<const fixpp::core::detail::length_data_pair>{});
    EXPECT_EQ(canonical_dump(no_extra), canonical_dump(framed));
}

// data-model E-3: every form bumps the seam W-15 counts; spec.md NFR-003: header_length_pairs is
// not a walk.
TEST_P(W17Overload, EveryFormIsOneWalk) {
    namespace dt = dd::detail;
    const dd::Dictionary& d = vendored(GetParam().file);
    const std::array<fixpp::core::detail::length_data_pair, 1> extra{
        {{.length_tag = 5030, .data_tag = 5031}}};

    dt::reset_as_table_view_call_count();
    (void)d.as_table_view();
    EXPECT_EQ(dt::as_table_view_call_count(), 1U);
    (void)d.as_table_view(dd::fixt_framing);
    EXPECT_EQ(dt::as_table_view_call_count(), 2U);
    (void)d.as_table_view(dd::fixt_framing, extra);
    EXPECT_EQ(dt::as_table_view_call_count(), 3U);
    (void)dd::header_length_pairs(d);
    EXPECT_EQ(dt::as_table_view_call_count(), 3U);
}

// No vendored file declares a pair outside the standard table, so none pays for a route's table.
TEST_P(W17Overload, VendoredFileDeclaresNoHeaderPair) {
    EXPECT_TRUE(dd::header_length_pairs(vendored(GetParam().file)).empty());
}

INSTANTIATE_TEST_SUITE_P(W17, W17Overload, testing::ValuesIn(kVendored),
                         [](const testing::TestParamInfo<VendoredHash>& i) {
                             return std::filesystem::path{i.param.file}.stem().string();
                         });

// The overload leaves vt11 and FIX Latest as the plain form has them: every framing tag, plain
// against the overload, through the two accessors the validator reads.
TEST(W17Overload, FieldTypeWithFraming_UnchangedOnFilesThatDeclareTheirOwnHeader) {
    for (const char* file : {"FIXT11.xml", "orchestra/OrchestraFIXLatest.xml"}) {
        const dd::table_view plain = vendored(file).as_table_view();
        const dd::table_view framed = vendored(file).as_table_view(dd::fixt_framing);
        for (const auto& e : dd::detail::kFixtFramingTable) {
            EXPECT_EQ(framed.field_type_of_with_framing(e.tag),
                      plain.field_type_of_with_framing(e.tag))
                << file << " tag " << e.tag;
            EXPECT_EQ(framed.is_fixt_framing_tag(e.tag), plain.is_fixt_framing_tag(e.tag))
                << file << " tag " << e.tag;
        }
    }
}

// ── data-model E-3's `extra`, and dict::header_length_pairs ─────────────────────────────────

const char* const kPairMembers =
    "<field name='VenuePairLen' required='N' /><field name='VenuePairData' required='N' />";

std::unique_ptr<const dd::Dictionary> with_overlay(const char* file, const std::string& overlay) {
    static std::pmr::monotonic_buffer_resource mr;
    std::ifstream in{std::filesystem::path{FIXPP_DICT_DATA_DIR} / file, std::ios::binary};
    const std::string base{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    return std::make_unique<const dd::Dictionary>(
        dd::XmlLoader{}.load_with_overlay_from_string(base, overlay, &mr));
}

// A routed dictionary whose NewOrderSingle body pairs 5030 with 5032.
std::unique_ptr<const dd::Dictionary> fix50sp2_with_body_pair() {
    return with_overlay("FIX50SP2.xml",
                        "<fix type='FIX' major='5' minor='0' servicepack='2'><messages>"
                        "<message name='NewOrderSingle' msgtype='D' msgcat='app'>"
                        "<field name='VenuePairLen' required='N' />"
                        "<field name='VenuePairOther' required='N' /></message></messages><fields>"
                        "<field number='5030' name='VenuePairLen' type='LENGTH' />"
                        "<field number='5032' name='VenuePairOther' type='DATA' /></fields></fix>");
}

TEST(W17ExtraPairs, SPairWinsACollisionOnThatTable) {
    const auto routed = fix50sp2_with_body_pair();
    ASSERT_EQ(routed->as_table_view(dd::fixt_framing).length_pair_data_tag(5030), 5032)
        << "the routed dictionary must pair the tag itself, or nothing collides";
    const std::array<fixpp::core::detail::length_data_pair, 1> extra{
        {{.length_tag = 5030, .data_tag = 5031}}};
    const dd::table_view tv = routed->as_table_view(dd::fixt_framing, extra);
    EXPECT_EQ(tv.length_pair_data_tag(5030), 5031);
    EXPECT_EQ(tv.data_pair_length_tag(5031), 5030);
    EXPECT_EQ(tv.data_pair_length_tag(5032), 0);
}

TEST(W17HeaderLengthPairs, AnOverlaysHeaderPairIsAnswered) {
    const std::string fields =
        "<fields><field number='5030' name='VenuePairLen' type='LENGTH' />"
        "<field number='5031' name='VenuePairData' type='DATA' /></fields></fix>";
    for (const auto& [file, head] :
         {std::pair{"FIXT11.xml", "<fix type='FIXT' major='1' minor='1' servicepack='0'>"},
          std::pair{"FIX50SP2.xml", "<fix type='FIX' major='5' minor='0' servicepack='2'>"}}) {
        const auto d = with_overlay(
            file, std::string(head) + "<header>" + kPairMembers + "</header>" + fields);
        ASSERT_TRUE(d->as_table_view().is_header_tag(5030)) << file;
        const auto pairs = dd::header_length_pairs(*d);
        ASSERT_EQ(pairs.size(), 1U) << file;
        EXPECT_EQ(pairs[0].length_tag, 5030) << file;
        EXPECT_EQ(pairs[0].data_tag, 5031) << file;
    }
}

// research R-20's scope: a body pair S's overlay adds is not composed.
TEST(W17HeaderLengthPairs, ABodyPairIsNotAHeaderPair) {
    const auto d = fix50sp2_with_body_pair();
    ASSERT_EQ(d->as_table_view().length_pair_data_tag(5030), 5032);
    EXPECT_TRUE(dd::header_length_pairs(*d).empty());
}

// ── W-17's first clause: a non-FIXT table still rejects 1128 ─────────────────────────────────

std::vector<std::byte> fix44_nos_with_1128() {
    const std::string body =
        "35=D\x01"
        "1128=6\x01"
        "34=2\x01"
        "49=A\x01"
        "52=20240101-00:00:00\x01"
        "56=B\x01"
        "11=C\x01"
        "21=1\x01"
        "55=X\x01"
        "54=1\x01"
        "60=20240101-00:00:00\x01"
        "40=1\x01"
        "38=1\x01";
    const std::string pre =
        "8=FIX.4.4\x01"
        "9=" +
        std::to_string(body.size()) + "\x01" + body;
    unsigned sum = 0;
    for (const unsigned char c : pre) {
        sum += c;
    }
    std::string chk = std::to_string(sum % 256U);
    chk.insert(0, 3 - chk.size(), '0');
    const std::string full = pre + "10=" + chk + "\x01";
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

struct StrictVerdict {
    bool ok = false;
    std::uint16_t ref_tag = 0;
    fixpp::core::error err{};
};

StrictVerdict validate_fix44_nos(dd::table_view table) {
    const fixpp::wire::dictionary_driven_validator v{std::move(table)};
    const std::vector<std::byte> buf = fix44_nos_with_1128();
    std::array<std::byte, 8192> stack{};
    std::pmr::monotonic_buffer_resource arena{stack.data(), stack.size(),
                                              std::pmr::null_memory_resource()};
    auto fv = fixpp::wire::test::make_frame_view(buf);
    EXPECT_TRUE(fv.has_value());
    fixpp::wire::Parser<fixpp::wire::access_mode::Index> parser{v.table()};
    auto mv = parser.parse(*fv, &arena);
    EXPECT_TRUE(mv.has_value());
    StrictVerdict out;
    const auto r = v.validate(*mv, &arena, &out.ref_tag);
    out.ok = r.has_value();
    if (!r) {
        out.err = r.error();
    }
    return out;
}

TEST(W17Fix44, PlainTableStrictValidationRejects1128) {
    const StrictVerdict v = validate_fix44_nos(vendored("FIX44.xml").as_table_view());
    EXPECT_FALSE(v.ok);
    EXPECT_EQ(v.ref_tag, 1128);
    EXPECT_EQ(v.err, fixpp::core::error::wire_unexpected_tag);
}

// The control: the frame is otherwise valid, so the refusal above is 1128's.
TEST(W17Fix44, FramedTablePassesTheSameFrame) {
    const StrictVerdict v =
        validate_fix44_nos(vendored("FIX44.xml").as_table_view(dd::fixt_framing));
    EXPECT_TRUE(v.ok) << "ref_tag=" << v.ref_tag << " err=" << static_cast<int>(v.err);
}

}  // namespace
