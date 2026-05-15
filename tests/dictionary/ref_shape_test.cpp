// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/ref_shape_test.cpp — seam #4 — AC-F1..F5

#include <gtest/gtest.h>

#include <fixpp/dict/component_ref.hpp>
#include <fixpp/dict/field_ref.hpp>
#include <fixpp/dict/group_ref.hpp>
#include <fixpp/dict/version_profile.hpp>

#include <cstdint>
#include <type_traits>

// ---------------------------------------------------------------------------
// Compile-time assertions (ABI drift detection — mirrors the static_asserts
// already present in each header, but owned by the test TU so CI catches any
// divergence between the header guards and the test expectations).
// ---------------------------------------------------------------------------

// AC-F1
static_assert(sizeof(fixpp::dict::FieldRef) == 16);
static_assert(alignof(fixpp::dict::FieldRef) == 2);
static_assert(std::is_standard_layout_v<fixpp::dict::FieldRef>);
static_assert(std::is_trivially_copyable_v<fixpp::dict::FieldRef>);

// AC-F2
static_assert(sizeof(fixpp::dict::ComponentRef) == 12);
static_assert(std::is_standard_layout_v<fixpp::dict::ComponentRef>);
static_assert(std::is_trivially_copyable_v<fixpp::dict::ComponentRef>);

// AC-F3
static_assert(sizeof(fixpp::dict::GroupRef) == 12);
static_assert(std::is_standard_layout_v<fixpp::dict::GroupRef>);
static_assert(std::is_trivially_copyable_v<fixpp::dict::GroupRef>);

// Enum underlying-type invariants (AC-F5 + enum discipline)
static_assert(std::is_same_v<std::underlying_type_t<fixpp::dict::field_presence>, std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<fixpp::dict::field_data_type>, std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<fixpp::dict::session_version>, std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<fixpp::dict::application_version>, std::uint8_t>);

// ---------------------------------------------------------------------------
// RefShape — runtime re-assertion of sizes, alignment, and type traits
// ---------------------------------------------------------------------------

TEST(RefShape, FieldRefSize) {
    EXPECT_EQ(sizeof(fixpp::dict::FieldRef), 16u);
}

TEST(RefShape, FieldRefAlignment) {
    EXPECT_EQ(alignof(fixpp::dict::FieldRef), 2u);
}

TEST(RefShape, FieldRefIsStandardLayout) {
    EXPECT_TRUE(std::is_standard_layout_v<fixpp::dict::FieldRef>);
}

TEST(RefShape, FieldRefIsTriviallyCopyable) {
    EXPECT_TRUE(std::is_trivially_copyable_v<fixpp::dict::FieldRef>);
}

TEST(RefShape, ComponentRefSize) {
    EXPECT_EQ(sizeof(fixpp::dict::ComponentRef), 12u);
}

TEST(RefShape, ComponentRefIsStandardLayout) {
    EXPECT_TRUE(std::is_standard_layout_v<fixpp::dict::ComponentRef>);
}

TEST(RefShape, ComponentRefIsTriviallyCopyable) {
    EXPECT_TRUE(std::is_trivially_copyable_v<fixpp::dict::ComponentRef>);
}

TEST(RefShape, GroupRefSize) {
    EXPECT_EQ(sizeof(fixpp::dict::GroupRef), 12u);
}

TEST(RefShape, GroupRefIsStandardLayout) {
    EXPECT_TRUE(std::is_standard_layout_v<fixpp::dict::GroupRef>);
}

TEST(RefShape, GroupRefIsTriviallyCopyable) {
    EXPECT_TRUE(std::is_trivially_copyable_v<fixpp::dict::GroupRef>);
}

// ---------------------------------------------------------------------------
// ReservedDiscipline — AC-F4: _reserved must be zero after value-init
// ---------------------------------------------------------------------------

TEST(ReservedDiscipline, FieldRefReservedIsZeroOnValueInit) {
    fixpp::dict::FieldRef fr{};
    EXPECT_EQ(fr._reserved, 0u);
}

TEST(ReservedDiscipline, ComponentRefReservedIsZeroOnValueInit) {
    fixpp::dict::ComponentRef cr{};
    EXPECT_EQ(cr._reserved, 0u);
}

TEST(ReservedDiscipline, GroupRefReservedIsZeroOnValueInit) {
    fixpp::dict::GroupRef gr{};
    EXPECT_EQ(gr._reserved, 0u);
}

// ---------------------------------------------------------------------------
// EnumDiscipline — AC-F5: field_presence numeric values + underlying type
// ---------------------------------------------------------------------------

TEST(EnumDiscipline, FieldPresenceUnderlyingTypeIsUint8) {
    EXPECT_TRUE((std::is_same_v<std::underlying_type_t<fixpp::dict::field_presence>, std::uint8_t>));
}

TEST(EnumDiscipline, FieldPresenceNotDeclaredIsZero) {
    EXPECT_EQ(static_cast<std::uint8_t>(fixpp::dict::field_presence::NotDeclared), std::uint8_t{0});
}

TEST(EnumDiscipline, FieldPresenceOptionalIsOne) {
    EXPECT_EQ(static_cast<std::uint8_t>(fixpp::dict::field_presence::Optional), std::uint8_t{1});
}

TEST(EnumDiscipline, FieldPresenceRequiredIsTwo) {
    EXPECT_EQ(static_cast<std::uint8_t>(fixpp::dict::field_presence::Required), std::uint8_t{2});
}

TEST(EnumDiscipline, FieldPresenceConditionalIsThree) {
    EXPECT_EQ(static_cast<std::uint8_t>(fixpp::dict::field_presence::Conditional), std::uint8_t{3});
}

TEST(EnumDiscipline, FieldDataTypeUnderlyingTypeIsUint8) {
    EXPECT_TRUE((std::is_same_v<std::underlying_type_t<fixpp::dict::field_data_type>, std::uint8_t>));
}

TEST(EnumDiscipline, SessionVersionUnderlyingTypeIsUint8) {
    EXPECT_TRUE((std::is_same_v<std::underlying_type_t<fixpp::dict::session_version>, std::uint8_t>));
}

TEST(EnumDiscipline, ApplicationVersionUnderlyingTypeIsUint8) {
    EXPECT_TRUE((std::is_same_v<std::underlying_type_t<fixpp::dict::application_version>, std::uint8_t>));
}
