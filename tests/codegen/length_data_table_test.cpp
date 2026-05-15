// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/codegen/length_data_table_test.cpp — T021 [US1] / seam #19
//
// AC-V4 / I-8: the emitted Validator.hpp Length+Data pair table is
// EXHAUSTIVE vs the source XML — every paired LENGTH tag the loaded
// Dictionary knows is in the generated table and vice versa, with matching
// data tags. The Dictionary (one XML truth, F1) is the oracle.
#include <gtest/gtest.h>

#include <cstdint>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/v44/Validator.hpp>
#include <map>
#include <memory_resource>
#include <string>

TEST(CodegenLengthDataTable, ExhaustiveVsSourceXml) {
    std::pmr::monotonic_buffer_resource arena;
    fixpp::dict::XmlLoader loader;
    auto dict = loader.load(std::string(FIXPP_DICT_DATA_DIR) + "/FIX44.xml", &arena);

    std::map<std::uint16_t, std::uint16_t> from_dict;
    for (std::uint16_t t = 1; t <= 2500; ++t) {
        std::uint16_t const d = dict.length_pair_data_tag(t);
        if (d != 0) from_dict[t] = d;
    }

    std::map<std::uint16_t, std::uint16_t> from_gen;
    for (auto const& p : fixpp::v44::validator::length_data_pairs) {
        from_gen[p.length_tag] = p.data_tag;
    }

    EXPECT_EQ(from_gen, from_dict);  // exhaustive + exact (AC-V4 / seam #19)
}
