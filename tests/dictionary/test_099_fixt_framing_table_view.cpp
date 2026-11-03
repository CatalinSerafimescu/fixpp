// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/dictionary/test_099_fixt_framing_table_view.cpp
//
// 099-fixt-appl-version-validation, contracts/witnesses.md W-17's no-argument half (tasks.md T006):
// a canonical dump of every store `table_view_test_access` reads, hashed per vendored file against
// a literal. SC-005 and NFR-001's "plain as_table_view() unchanged" rest on it, and M-43 is its
// mutant. The dump is sorted text, so it is the same bytes on every platform, and the hash
// is FNV-1a 64, whose value its definition fixes, unlike std::hash.
//
// To re-derive a literal, run with FIXPP_099_PRINT_DUMP=<dir>: each file's dump lands there. A
// literal moves only by the task that says so (research R-17's last row, for FIXT11.xml).
//
// The overload's half joins this file with the overload (tasks.md T024).

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/load_any.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fstream>
#include <memory_resource>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

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
    for (auto const& [key, value] : store) {
        lines.push_back(std::string(key) + ":" + join_sorted(value));
    }
    return lines;
}

template <class Store>
std::vector<std::string> msg_list_lines(Store const& store) {
    std::vector<std::string> lines;
    for (auto const& [key, value] : store) {
        lines.push_back(std::string(key) + ":" + join_in_order(value));
    }
    return lines;
}

template <class Store>
std::vector<std::string> tag_value_lines(Store const& store) {
    std::vector<std::string> lines;
    for (auto const& [key, value] : store) {
        lines.push_back(std::to_string(key) + ":" + std::to_string(static_cast<unsigned>(value)));
    }
    return lines;
}

template <class Store>
std::vector<std::string> tag_list_lines(Store const& store) {
    std::vector<std::string> lines;
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
    {"FIX40.xml", 0xe84640c9433acba2ULL},
    {"FIX41.xml", 0x7c58f6abfe64a108ULL},
    {"FIX42.xml", 0xe5739dda716d8732ULL},
    {"FIX43.xml", 0x6e4beab588e9e5d1ULL},
    {"FIX44.xml", 0x645442c49557393aULL},
    {"FIX50.xml", 0xe6be16b585a12850ULL},
    {"FIX50SP1.xml", 0x33af764d534a765dULL},
    {"FIX50SP2.xml", 0x85e0014214253c0bULL},
    {"FIXT11.xml", 0x99a62ff60a54c244ULL},
    {"orchestra/OrchestraFIXLatest.xml", 0xd55ac82e9e1883baULL},
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

}  // namespace
