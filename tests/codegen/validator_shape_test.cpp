// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/codegen/validator_shape_test.cpp — T020 [US1]
//
// AC-V1/V2 (Fields.hpp constexpr FieldRef arrays — static storage; a member
// outside a component has `component_index` 0, the row 102 re-points AC-V6's
// `_reserved` check at, since 102-dict-width-multichar-type D1 removes
// `FieldRef::_reserved`), AC-V3 (Validator.hpp per-message rule tables), AC-V5
// (NormativeReferences.md per-message citations). Shape/structure only;
// behavioural validation is out of scope (spec §5).
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <fixpp/dict/field_ref.hpp>
#include <fixpp/v44/Fields.hpp>
#include <fixpp/v44/Validator.hpp>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <type_traits>

TEST(CodegenValidatorShape, FieldsArrayIsConstexprAndReservedZero) {
    constexpr auto const& fr = fixpp::v44::fields::NewOrderSingle_fields;  // AC-V1
    static_assert(std::size(fr) > 0);
    static_assert(std::is_same_v<std::remove_cvref_t<decltype(fr[0])>, fixpp::dict::FieldRef>);
    // ClOrdID(11) is a direct member of NewOrderSingle, in no component.
    auto const* const cl_ord_id =
        std::ranges::find(fr, std::uint16_t{11}, &fixpp::dict::FieldRef::tag);
    ASSERT_NE(cl_ord_id, std::end(fr));
    EXPECT_EQ(cl_ord_id->component_index, 0U);
}

TEST(CodegenValidatorShape, PerMessageRuleTable) {
    constexpr auto const& rules = fixpp::v44::validator::NewOrderSingle_rules;  // AC-V3
    static_assert(std::size(rules) > 0);
    EXPECT_GT(rules[0].tag, 0);
    // field_presence is the 002-shipped enum.
    static_assert(std::is_same_v<decltype(rules[0].rule), fixpp::dict::field_presence>);
}

TEST(CodegenValidatorShape, NormativeReferencesPerMessageCitations) {
    std::ifstream f(std::string(FIXPP_CODEGEN_INC) + "/fixpp/v44/NormativeReferences.md");  // AC-V5
    ASSERT_TRUE(f.good());
    std::stringstream ss;
    ss << f.rdbuf();
    std::string const md = ss.str();
    EXPECT_NE(md.find("## Per-message citations"), std::string::npos);
    EXPECT_NE(md.find("[FIX44]"), std::string::npos);
    EXPECT_NE(md.find("NewOrderSingle"), std::string::npos);
}
