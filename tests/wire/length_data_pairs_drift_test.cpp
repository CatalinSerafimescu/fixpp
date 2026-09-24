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
#include <iostream>
#include <map>
#include <memory_resource>
#include <set>
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

// ── Per-dictionary drift arm (091, FR-018 / C-2.5) ─────────────────────────
//
// For one shipped dictionary: every standard pair whose Length tag is a
// Length-typed field of some message expansion and whose Data tag also occurs
// in some message expansion must be paired by that dictionary's own loader.
// The probe is the `message_fields()` walk `add_pairs` uses. The probed set is
// printed on every leg and must be non-empty, so a leg cannot pass by probing
// nothing.

namespace {

struct drift_leg {
    char const* name;  // gtest parameter name
    char const* file;  // relative to FIXPP_DICT_DATA_DIR
    bool orchestra;
};

void PrintTo(drift_leg const& leg, std::ostream* os) { *os << leg.name; }

fixpp::dict::Dictionary load_leg(drift_leg const& leg, std::pmr::memory_resource* mr) {
    std::string const path = std::string{FIXPP_DICT_DATA_DIR} + "/" + leg.file;
    if (leg.orchestra) {
        return fixpp::dict::OrchestraLoader{}.load(path, mr);
    }
    return fixpp::dict::XmlLoader{}.load(path, mr);
}

class LengthDataPairsPerDictionary : public ::testing::TestWithParam<drift_leg> {};

}  // namespace

TEST_P(LengthDataPairsPerDictionary, EveryProbedStandardPairIsPairedByTheLoader) {
    drift_leg const& leg = GetParam();
    std::pmr::monotonic_buffer_resource mr;
    fixpp::dict::Dictionary const dict = load_leg(leg, &mr);

    std::set<std::uint16_t> length_tags;
    std::set<std::uint16_t> all_tags;
    for (auto const& m : dict.messages()) {
        for (auto const& fr : dict.message_fields(m.msg_type)) {
            all_tags.insert(fr.tag);
            if (fr.type == fixpp::dict::field_data_type::Length) {
                length_tags.insert(fr.tag);
            }
        }
    }

    std::ostringstream probed;
    std::ostringstream unpaired;
    bool any_probed = false;
    for (auto const& p : fixpp::wire::detail::standard_length_data_pairs) {
        if (!length_tags.contains(p.length_tag) || !all_tags.contains(p.data_tag)) {
            continue;
        }
        any_probed = true;
        probed << ' ' << p.length_tag << "->" << p.data_tag;
        std::uint16_t const got = dict.length_pair_data_tag(p.length_tag);
        if (got != p.data_tag) {
            unpaired << ' ' << p.length_tag << "->" << p.data_tag << " (loader: " << got << ')';
        }
    }
    std::cout << "[" << leg.name << "] probed standard pairs:" << probed.str() << '\n';

    EXPECT_TRUE(any_probed) << leg.name << ": the probed standard-pair set is empty";
    EXPECT_TRUE(unpaired.str().empty())
        << leg.name << ": standard pairs present in the message expansions but not paired by "
        << "the loader:" << unpaired.str();
}

INSTANTIATE_TEST_SUITE_P(
    ShippedDictionaries, LengthDataPairsPerDictionary,
    ::testing::Values(drift_leg{"FIX40", "FIX40.xml", false}, drift_leg{"FIX41", "FIX41.xml", false},
                      drift_leg{"FIX42", "FIX42.xml", false}, drift_leg{"FIX43", "FIX43.xml", false},
                      drift_leg{"FIX44", "FIX44.xml", false}, drift_leg{"FIX50", "FIX50.xml", false},
                      drift_leg{"FIX50SP1", "FIX50SP1.xml", false},
                      drift_leg{"FIX50SP2", "FIX50SP2.xml", false},
                      drift_leg{"FIXT11", "FIXT11.xml", false},
                      drift_leg{"OrchestraFIXLatest", "orchestra/OrchestraFIXLatest.xml", true}),
    [](::testing::TestParamInfo<drift_leg> const& info) { return std::string{info.param.name}; });

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
