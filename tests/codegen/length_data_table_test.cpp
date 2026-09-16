// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/codegen/length_data_table_test.cpp — T021 [US1] / seam #19
//
// AC-V4 / I-8: the emitted Validator.hpp Length+Data pair table is
// EXHAUSTIVE vs the source XML — every paired LENGTH tag the loaded
// Dictionary knows is in the generated table and vice versa, with matching
// data tags. The Dictionary (one XML truth, F1) is the oracle.
//
// fixpp#427: the probe set is every Length-typed tag of every message's
// expansion, plus every key the generator emitted. It is not a tag range: a
// fixed ceiling once hid every pair above it in both this test and the
// generator.
#include <gtest/gtest.h>

#include <cstdint>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/field_ref.hpp>
#include <fixpp/dict/orchestra_loader.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/v44/Validator.hpp>
#include <fixpp/v50sp2/Validator.hpp>
#include <map>
#include <memory_resource>
#include <set>
#include <string>
#if __has_include(<fixpp/vlatest/Validator.hpp>)
#include <fixpp/vlatest/Validator.hpp>
#endif

namespace {

template <class Pairs>
void expect_table_matches_dictionary(fixpp::dict::Dictionary const& dict, Pairs const& generated) {
    std::map<std::uint16_t, std::uint16_t> from_gen;
    std::set<std::uint16_t> probes;
    for (auto const& p : generated) {
        from_gen[p.length_tag] = p.data_tag;
        probes.insert(p.length_tag);
    }
    for (auto const& m : dict.messages()) {
        for (auto const& fr : dict.message_fields(m.msg_type)) {
            if (fr.type == fixpp::dict::field_data_type::Length) {
                probes.insert(fr.tag);
            }
        }
    }

    std::map<std::uint16_t, std::uint16_t> from_dict;
    for (auto const t : probes) {
        if (std::uint16_t const d = dict.length_pair_data_tag(t); d != 0) {
            from_dict[t] = d;
        }
    }

    EXPECT_FALSE(from_dict.empty());
    EXPECT_EQ(from_gen, from_dict);  // exhaustive + exact (AC-V4 / seam #19)
}

}  // namespace

TEST(CodegenLengthDataTable, ExhaustiveVsSourceXml) {
    std::pmr::monotonic_buffer_resource arena;
    auto dict =
        fixpp::dict::XmlLoader{}.load(std::string(FIXPP_DICT_DATA_DIR) + "/FIX44.xml", &arena);
    expect_table_matches_dictionary(dict, fixpp::v44::validator::length_data_pairs);
}

TEST(CodegenLengthDataTable, V50sp2ExhaustiveVsSourceXml) {
    std::pmr::monotonic_buffer_resource arena;
    auto dict =
        fixpp::dict::XmlLoader{}.load(std::string(FIXPP_DICT_DATA_DIR) + "/FIX50SP2.xml", &arena);
    expect_table_matches_dictionary(dict, fixpp::v50sp2::validator::length_data_pairs);
}

#if __has_include(<fixpp/vlatest/Validator.hpp>)
TEST(CodegenLengthDataTable, VlatestExhaustiveVsOrchestraXml) {
    std::pmr::monotonic_buffer_resource arena;
    auto dict = fixpp::dict::OrchestraLoader{}.load(
        std::string(FIXPP_DICT_DATA_DIR) + "/orchestra/OrchestraFIXLatest.xml", &arena);
    expect_table_matches_dictionary(dict, fixpp::vlatest::validator::length_data_pairs);
}
#endif
