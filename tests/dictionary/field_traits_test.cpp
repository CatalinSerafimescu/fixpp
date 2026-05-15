// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/field_traits_test.cpp
//
// 003-dictionary-codegen — T005. AC-FT1..AC-FT3 (RC#1). The 003-owned NET-NEW
// dict::field_traits<T> primary + the listed specialisations + the inline
// decode_field<T> forwarder; AC-FT2 negative (decimal_t is NOT a field_traits
// specialisation — I-16). Oracle:
// specs/003-dictionary-codegen/contracts/field_traits.hpp; data-model
// Entity 11.

#include <gtest/gtest.h>

#include <cstdint>
#include <string_view>
#include <type_traits>

#include <fixpp/core/decimal_alias.hpp>  // fixpp::decimal_t (AC-FT2 negative)
#include <fixpp/core/error.hpp>
#include <fixpp/dict/field_traits.hpp>
#include <fixpp/wire/message_view_contract.hpp>

namespace {

using fixpp::core::error;
using fixpp::dict::decode_field;
using fixpp::dict::field_traits;

// Detection idiom: is field_traits<T> a DEFINED specialisation exposing
// from_field_view? The primary template is only declared, so an unspecialised
// T yields an incomplete type and the detector is false (SFINAE-friendly).
template <class T, class = void>
struct has_fft : std::false_type {};
template <class T>
struct has_fft<T, std::void_t<decltype(&field_traits<T>::from_field_view)>>
    : std::true_type {};

// AC-FT1 — primary + the listed specialisations are defined.
static_assert(has_fft<std::string_view>::value);
static_assert(has_fft<char>::value);
static_assert(has_fft<std::int32_t>::value);
static_assert(has_fft<std::int64_t>::value);
static_assert(has_fft<bool>::value);

// AC-FT2 (negative) — decimal_t is NOT a field_traits specialisation; the
// decimal route is the [2c §4.7] PMR accessor (AC-G4), not field_traits.
static_assert(!has_fft<fixpp::decimal_t>::value,
              "AC-FT2: field_traits<decimal_t> must NOT be a defined specialisation");

TEST(FieldTraitsAcFt3, ForwardsGetErrorWhenAbsent) {
    // AC-FT3 — decode_field returns the get<>() error unchanged on !fv
    // (this path does NOT call from_field_view, so it is wire-stub-safe).
    fixpp::core::expected_t<fixpp::wire::field_view> absent{
        std::unexpect, error::dict_xml_parse_failed};
    auto r = decode_field<std::string_view>(absent);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::dict_xml_parse_failed);
}

TEST(FieldTraitsAcFt3, DelegatesToFromFieldViewWhenPresent) {
    // AC-FT3 — when fv has a value, decode_field forwards to
    // field_traits<T>::from_field_view. With the frozen R6 wire stub
    // (no frame state) field_view::as_string() is empty, so string_view
    // decodes to "" — the success-VALUE path is R6-blocked until 2b lands
    // a frame-backed body; here we assert the DELEGATION shape only.
    fixpp::core::expected_t<fixpp::wire::field_view> present{
        fixpp::wire::field_view{}};
    auto r = decode_field<std::string_view>(present);
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(r->empty());  // stub field_view → empty string_view
}

}  // namespace
