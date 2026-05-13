// tests/core/decimal_capi_layout_test.cpp
// US3 — compile-time and runtime layout assertions for fixpp_decimal_t.
// AC-A1..A5b per spec.md §4.5.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <type_traits>

#include "fix/c_api/decimal.h"

// AC-A1: sizeof == 16
static_assert(sizeof(fixpp_decimal_t) == 16, "AC-A1");

// AC-A2: alignof == 8, offsets
static_assert(alignof(fixpp_decimal_t) == 8, "AC-A2 alignof");
static_assert(offsetof(fixpp_decimal_t, mantissa) == 0, "AC-A2 mantissa offset");
static_assert(offsetof(fixpp_decimal_t, exponent) == 8, "AC-A2 exponent offset");
static_assert(offsetof(fixpp_decimal_t, _reserved) == 9, "AC-A2 _reserved offset");

// AC-A3: is_standard_layout
static_assert(std::is_standard_layout_v<fixpp_decimal_t>, "AC-A3");

// AC-A4 runtime: _reserved ignored on read
TEST(DecimalCABILayout, ReservedIgnoredOnRead) {
    fixpp_decimal_t d = FIXPP_DECIMAL_INITIALIZER;
    d.mantissa = 12345;
    d.exponent = -2;
    // Set garbage _reserved bytes
    for (int i = 0; i < 7; ++i) d._reserved[i] = static_cast<int8_t>(0xFF);

    char buf[64]{};
    size_t written = 0;
    fixpp_error_t err = fixpp_decimal_format(d, buf, sizeof(buf), &written);
    ASSERT_EQ(err, FIXPP_ERR_OK);
    EXPECT_STREQ(buf, "123.45");
}

// AC-A5: FIXPP_DECIMAL_INITIALIZER and fixpp_decimal_init() zero _reserved
TEST(DecimalCABILayout, InitializerZerosReserved) {
    fixpp_decimal_t d = FIXPP_DECIMAL_INITIALIZER;
    for (int i = 0; i < 7; ++i) EXPECT_EQ(d._reserved[i], 0) << "index " << i;
}

TEST(DecimalCABILayout, InitFuncZerosReserved) {
    fixpp_decimal_t d{};
    std::memset(d._reserved, 0xFF, sizeof(d._reserved));
    fixpp_decimal_init(&d);
    for (int i = 0; i < 7; ++i) EXPECT_EQ(d._reserved[i], 0) << "index " << i;
}

// AC-A5b: non-zero _reserved is tolerated on parse output
TEST(DecimalCABILayout, NonZeroReservedTolerated) {
    // Build a fixpp_decimal_t with garbage _reserved and pass to format
    fixpp_decimal_t d{};
    d.mantissa = 1;
    d.exponent = 0;
    std::memset(d._reserved, 0xAB, sizeof(d._reserved));
    char buf[64]{};
    size_t written = 0;
    EXPECT_EQ(fixpp_decimal_format(d, buf, sizeof(buf), &written), FIXPP_ERR_OK);
    EXPECT_STREQ(buf, "1");
}
