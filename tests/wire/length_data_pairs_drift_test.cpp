// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/length_data_pairs_drift_test.cpp — fixpp#426
//
// `include/fixpp/wire/length_data_pairs.hpp` must equal the union of the
// Length+Data pairs every shipped dictionary declares, loaded with the real
// loaders. On mismatch this prints the replacement rows. The probe set for each
// dictionary is every Length-typed field of every message expansion, not a tag
// range.

#include <gtest/gtest.h>

#include <cstdint>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/field_ref.hpp>
#include <fixpp/dict/orchestra_loader.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/wire/length_data_pairs.hpp>
#include <map>
#include <memory_resource>
#include <sstream>
#include <string>
#include <vector>

namespace {

using pair_map = std::map<std::uint16_t, std::uint16_t>;

// Adds `dict`'s pairs to `acc`; records any Length tag already mapped elsewhere.
void add_pairs(fixpp::dict::Dictionary const& dict, std::string const& origin, pair_map& acc,
               std::vector<std::string>& conflicts) {
    for (auto const& m : dict.messages()) {
        for (auto const& fr : dict.message_fields(m.msg_type)) {
            if (fr.type != fixpp::dict::field_data_type::Length) {
                continue;
            }
            std::uint16_t const data = dict.length_pair_data_tag(fr.tag);
            if (data == 0) {
                continue;
            }
            auto const [it, inserted] = acc.emplace(fr.tag, data);
            if (!inserted && it->second != data) {
                conflicts.push_back(origin + ": " + std::to_string(fr.tag) + " -> " +
                                    std::to_string(data) + " vs " + std::to_string(it->second));
            }
        }
    }
}

}  // namespace

TEST(LengthDataPairs, HeaderEqualsShippedDictionaryUnion) {
    std::string const dir = FIXPP_DICT_DATA_DIR;
    pair_map from_dicts;
    std::vector<std::string> conflicts;
    for (char const* file : {"FIX40.xml", "FIX41.xml", "FIX42.xml", "FIX43.xml", "FIX44.xml",
                             "FIX50.xml", "FIX50SP1.xml", "FIX50SP2.xml", "FIXT11.xml"}) {
        std::pmr::monotonic_buffer_resource mr;
        add_pairs(fixpp::dict::XmlLoader{}.load(dir + "/" + file, &mr), file, from_dicts,
                  conflicts);
    }
    {
        std::pmr::monotonic_buffer_resource mr;
        add_pairs(
            fixpp::dict::OrchestraLoader{}.load(dir + "/orchestra/OrchestraFIXLatest.xml", &mr),
            "OrchestraFIXLatest.xml", from_dicts, conflicts);
    }
    EXPECT_TRUE(conflicts.empty()) << ::testing::PrintToString(conflicts);

    pair_map from_header;
    for (auto const& p : fixpp::wire::detail::standard_length_data_pairs) {
        from_header.emplace(p.length_tag, p.data_tag);
    }

    std::ostringstream rows;
    for (auto const& [len, data] : from_dicts) {
        rows << "    {.length_tag = " << len << ", .data_tag = " << data << "},\n";
    }
    EXPECT_EQ(from_header, from_dicts)
        << "length_data_pairs.hpp has drifted from the shipped dictionaries; replacement rows ("
        << from_dicts.size() << "):\n"
        << rows.str();
}

// Pins that do not need a dictionary. They name the cases a hand-written table has
// historically got wrong.
// A Length tag numbered above its Data tag.
static_assert(fixpp::wire::detail::standard_data_tag_for_length(93) == 89);
static_assert(fixpp::wire::detail::standard_data_tag_for_length(2372) == 2371);
// EncodedText, absent from the old six-pair table.
static_assert(fixpp::wire::detail::standard_data_tag_for_length(354) == 355);
// A pair whose tags are not adjacent.
static_assert(fixpp::wire::detail::standard_data_tag_for_length(1678) == 1697);
// The last row.
static_assert(fixpp::wire::detail::standard_data_tag_for_length(43111) == 42982);
// A Data tag is not a key; an unpaired tag maps to nothing.
static_assert(fixpp::wire::detail::standard_data_tag_for_length(89) == 0);
static_assert(fixpp::wire::detail::standard_data_tag_for_length(7) == 0);
