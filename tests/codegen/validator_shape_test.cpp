// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/codegen/validator_shape_test.cpp — T020 [US1]
//
// AC-V1/V2/V6 (Fields.hpp constexpr FieldRef arrays — static storage,
// _reserved zero), AC-V3 (Validator.hpp per-message rule tables), AC-V5
// (NormativeReferences.md per-message citations). Shape/structure only;
// behavioural validation is out of scope (spec §5).
#include <array>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <type_traits>

#include <gtest/gtest.h>

#include <fixpp/dict/field_ref.hpp>

#include <fixpp/v44/Fields.hpp>
#include <fixpp/v44/Validator.hpp>

TEST(CodegenValidatorShape, FieldsArrayIsConstexprAndReservedZero) {
    constexpr auto const& fr = fixpp::v44::fields::NewOrderSingle_fields;  // AC-V1
    static_assert(std::size(fr) > 0);
    static_assert(std::is_same_v<std::remove_cvref_t<decltype(fr[0])>,
                                 fixpp::dict::FieldRef>);
    for (auto const& e : fr) {
        EXPECT_EQ(e._reserved, 0);  // AC-V6 / I-7
    }
}

TEST(CodegenValidatorShape, PerMessageRuleTable) {
    constexpr auto const& rules = fixpp::v44::validator::NewOrderSingle_rules;  // AC-V3
    static_assert(std::size(rules) > 0);
    EXPECT_GT(rules[0].tag, 0);
    // field_presence is the 002-shipped enum.
    static_assert(std::is_same_v<decltype(rules[0].rule),
                                 fixpp::dict::field_presence>);
}

TEST(CodegenValidatorShape, NormativeReferencesPerMessageCitations) {
    std::ifstream f(std::string(FIXPP_CODEGEN_INC) +
                    "/fixpp/v44/NormativeReferences.md");                       // AC-V5
    ASSERT_TRUE(f.good());
    std::stringstream ss;
    ss << f.rdbuf();
    std::string const md = ss.str();
    EXPECT_NE(md.find("## Per-message citations"), std::string::npos);
    EXPECT_NE(md.find("[FIX44]"), std::string::npos);
    EXPECT_NE(md.find("NewOrderSingle"), std::string::npos);
}
