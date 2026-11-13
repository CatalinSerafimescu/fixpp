// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/ref_shape_test.cpp — seam #4 — AC-F1..F5

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <fixpp/dict/component_ref.hpp>
#include <fixpp/dict/field_ref.hpp>
#include <fixpp/dict/group_ref.hpp>
#include <fixpp/dict/version_profile.hpp>
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
static_assert(
    std::is_same_v<std::underlying_type_t<fixpp::dict::application_version>, std::uint8_t>);

// ---------------------------------------------------------------------------
// RefShape — runtime re-assertion of sizes, alignment, and type traits
// ---------------------------------------------------------------------------

TEST(RefShape, FieldRefSize) { EXPECT_EQ(sizeof(fixpp::dict::FieldRef), 16U); }

TEST(RefShape, FieldRefAlignment) { EXPECT_EQ(alignof(fixpp::dict::FieldRef), 2U); }

TEST(RefShape, FieldRefIsStandardLayout) {
    EXPECT_TRUE(std::is_standard_layout_v<fixpp::dict::FieldRef>);
}

TEST(RefShape, FieldRefIsTriviallyCopyable) {
    EXPECT_TRUE(std::is_trivially_copyable_v<fixpp::dict::FieldRef>);
}

TEST(RefShape, ComponentRefSize) { EXPECT_EQ(sizeof(fixpp::dict::ComponentRef), 12U); }

TEST(RefShape, ComponentRefIsStandardLayout) {
    EXPECT_TRUE(std::is_standard_layout_v<fixpp::dict::ComponentRef>);
}

TEST(RefShape, ComponentRefIsTriviallyCopyable) {
    EXPECT_TRUE(std::is_trivially_copyable_v<fixpp::dict::ComponentRef>);
}

TEST(RefShape, GroupRefSize) { EXPECT_EQ(sizeof(fixpp::dict::GroupRef), 12U); }

TEST(RefShape, GroupRefIsStandardLayout) {
    EXPECT_TRUE(std::is_standard_layout_v<fixpp::dict::GroupRef>);
}

TEST(RefShape, GroupRefIsTriviallyCopyable) {
    EXPECT_TRUE(std::is_trivially_copyable_v<fixpp::dict::GroupRef>);
}

// ---------------------------------------------------------------------------
// RefShape102 — 102-dict-width-multichar-type (data-model.md, the three "after" tables):
// `sizeof`, `alignof`, the offset and the width of every member, checked at run time so a
// layout that differs is a failing assertion, not a compile error. The `static_assert`s and
// `ReservedDiscipline` above keep the layout they were written for until the layout change
// itself moves them. Re-derive the expected values from data-model.md's tables.
// ---------------------------------------------------------------------------

namespace {

template <class Member>
constexpr std::size_t width_of() {
    return sizeof(Member);
}

}  // namespace

TEST(RefShape102, FieldRefLayout) {
    using fixpp::dict::FieldRef;
    EXPECT_EQ(sizeof(FieldRef), 16U);
    EXPECT_EQ(alignof(FieldRef), 4U);
    EXPECT_EQ(offsetof(FieldRef, tag), 0U);
    EXPECT_EQ(offsetof(FieldRef, type), 2U);
    EXPECT_EQ(offsetof(FieldRef, rule), 3U);
    EXPECT_EQ(offsetof(FieldRef, condition_index), 4U);
    EXPECT_EQ(offsetof(FieldRef, group_no_tag), 6U);
    EXPECT_EQ(offsetof(FieldRef, component_index), 8U);
    EXPECT_EQ(offsetof(FieldRef, enum_table_index), 12U);
    EXPECT_EQ(offsetof(FieldRef, length_pair_data_tag), 14U);
    EXPECT_EQ(width_of<decltype(FieldRef::tag)>(), 2U);
    EXPECT_EQ(width_of<decltype(FieldRef::type)>(), 1U);
    EXPECT_EQ(width_of<decltype(FieldRef::rule)>(), 1U);
    EXPECT_EQ(width_of<decltype(FieldRef::condition_index)>(), 2U);
    EXPECT_EQ(width_of<decltype(FieldRef::group_no_tag)>(), 2U);
    EXPECT_EQ(width_of<decltype(FieldRef::component_index)>(), 4U);
    EXPECT_EQ(width_of<decltype(FieldRef::enum_table_index)>(), 2U);
    EXPECT_EQ(width_of<decltype(FieldRef::length_pair_data_tag)>(), 2U);
    EXPECT_TRUE((std::is_same_v<decltype(FieldRef::component_index), std::uint32_t>));
}

TEST(RefShape102, ComponentRefLayout) {
    using fixpp::dict::ComponentRef;
    EXPECT_EQ(sizeof(ComponentRef), 20U);
    EXPECT_EQ(alignof(ComponentRef), 4U);
    EXPECT_EQ(offsetof(ComponentRef, component_id), 0U);
    EXPECT_EQ(offsetof(ComponentRef, name_offset), 4U);
    EXPECT_EQ(offsetof(ComponentRef, first_field_index), 8U);
    EXPECT_EQ(offsetof(ComponentRef, field_count), 12U);
    EXPECT_EQ(offsetof(ComponentRef, parent_component_id), 16U);
    EXPECT_TRUE((std::is_same_v<decltype(ComponentRef::component_id), std::uint32_t>));
    EXPECT_TRUE((std::is_same_v<decltype(ComponentRef::name_offset), std::uint32_t>));
    EXPECT_TRUE((std::is_same_v<decltype(ComponentRef::first_field_index), std::uint32_t>));
    EXPECT_TRUE((std::is_same_v<decltype(ComponentRef::field_count), std::uint32_t>));
    EXPECT_TRUE((std::is_same_v<decltype(ComponentRef::parent_component_id), std::uint32_t>));
}

TEST(RefShape102, GroupRefLayout) {
    using fixpp::dict::GroupRef;
    EXPECT_EQ(sizeof(GroupRef), 16U);
    EXPECT_EQ(alignof(GroupRef), 4U);
    EXPECT_EQ(offsetof(GroupRef, no_tag), 0U);
    EXPECT_EQ(offsetof(GroupRef, first_field_tag), 2U);
    EXPECT_EQ(offsetof(GroupRef, first_field_index), 4U);
    EXPECT_EQ(offsetof(GroupRef, field_count), 8U);
    EXPECT_EQ(offsetof(GroupRef, parent_group_no_tag), 12U);
    EXPECT_EQ(offsetof(GroupRef, _reserved), 14U);
    EXPECT_EQ(width_of<decltype(GroupRef::no_tag)>(), 2U);
    EXPECT_EQ(width_of<decltype(GroupRef::first_field_tag)>(), 2U);
    EXPECT_EQ(width_of<decltype(GroupRef::first_field_index)>(), 4U);
    EXPECT_EQ(width_of<decltype(GroupRef::field_count)>(), 4U);
    EXPECT_EQ(width_of<decltype(GroupRef::parent_group_no_tag)>(), 2U);
    EXPECT_EQ(width_of<decltype(GroupRef::_reserved)>(), 2U);
    EXPECT_TRUE((std::is_same_v<decltype(GroupRef::first_field_index), std::uint32_t>));
    EXPECT_TRUE((std::is_same_v<decltype(GroupRef::field_count), std::uint32_t>));
}

// ---------------------------------------------------------------------------
// ReservedDiscipline — AC-F4: _reserved must be zero after value-init
// ---------------------------------------------------------------------------

TEST(ReservedDiscipline, FieldRefReservedIsZeroOnValueInit) {
    fixpp::dict::FieldRef fr{};
    EXPECT_EQ(fr._reserved, 0U);
}

TEST(ReservedDiscipline, ComponentRefReservedIsZeroOnValueInit) {
    fixpp::dict::ComponentRef cr{};
    EXPECT_EQ(cr._reserved, 0U);
}

TEST(ReservedDiscipline, GroupRefReservedIsZeroOnValueInit) {
    fixpp::dict::GroupRef gr{};
    EXPECT_EQ(gr._reserved, 0U);
}

// ---------------------------------------------------------------------------
// EnumDiscipline — AC-F5: field_presence numeric values + underlying type
// ---------------------------------------------------------------------------

TEST(EnumDiscipline, FieldPresenceUnderlyingTypeIsUint8) {
    EXPECT_TRUE(
        (std::is_same_v<std::underlying_type_t<fixpp::dict::field_presence>, std::uint8_t>));
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
    EXPECT_TRUE(
        (std::is_same_v<std::underlying_type_t<fixpp::dict::field_data_type>, std::uint8_t>));
}

TEST(EnumDiscipline, SessionVersionUnderlyingTypeIsUint8) {
    EXPECT_TRUE(
        (std::is_same_v<std::underlying_type_t<fixpp::dict::session_version>, std::uint8_t>));
}

TEST(EnumDiscipline, ApplicationVersionUnderlyingTypeIsUint8) {
    EXPECT_TRUE(
        (std::is_same_v<std::underlying_type_t<fixpp::dict::application_version>, std::uint8_t>));
}
