// tests/core/decimal_reserved_tolerance_test.cpp
// Seam #10 — AC-A4 + AC-A5b regression guard.
// Construct fixpp_decimal_t with garbage _reserved bytes; verify parse and format
// produce identical results to the zero-_reserved case. No UB under ASan.

#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "fix/c_api/decimal.h"

TEST(DecimalReservedTolerance, ParseGarbageReservedSameAsZero) {
    const char* input = "3.14";
    size_t len = 4;

    // Parse into a struct with zero _reserved
    fixpp_decimal_t zero_reserved = FIXPP_DECIMAL_INITIALIZER;
    ASSERT_EQ(fixpp_decimal_parse(input, len, &zero_reserved), FIXPP_ERR_OK);

    // Parse into a struct with pre-existing garbage _reserved
    fixpp_decimal_t garbage_reserved{};
    std::memset(garbage_reserved._reserved, 0x7E, sizeof(garbage_reserved._reserved));
    ASSERT_EQ(fixpp_decimal_parse(input, len, &garbage_reserved), FIXPP_ERR_OK);

    // mantissa and exponent must be identical; _reserved is zeroed by fixpp_decimal_parse
    EXPECT_EQ(zero_reserved.mantissa, garbage_reserved.mantissa);
    EXPECT_EQ(zero_reserved.exponent, garbage_reserved.exponent);
    // After parse, _reserved should be zeroed (AC-A5)
    for (int i = 0; i < 7; ++i) EXPECT_EQ(garbage_reserved._reserved[i], 0) << "index " << i;
}

TEST(DecimalReservedTolerance, FormatGarbageReservedSameAsZero) {
    auto make = [](int8_t reserved_fill) {
        fixpp_decimal_t d{};
        d.mantissa = 314;
        d.exponent = -2;
        std::memset(d._reserved, reserved_fill, sizeof(d._reserved));
        return d;
    };

    char buf_zero[64]{}, buf_garbage[64]{};
    size_t wz = 0, wg = 0;

    ASSERT_EQ(fixpp_decimal_format(make(0x00), buf_zero, sizeof(buf_zero), &wz), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_decimal_format(make(0x7E), buf_garbage, sizeof(buf_garbage), &wg),
              FIXPP_ERR_OK);

    EXPECT_EQ(wz, wg);
    EXPECT_EQ(std::string(buf_zero, wz), std::string(buf_garbage, wg));
}

TEST(DecimalReservedTolerance, CompareGarbageReservedSameAsZero) {
    auto make = [](int8_t reserved_fill) {
        fixpp_decimal_t d{};
        d.mantissa = 100;
        d.exponent = -2;
        std::memset(d._reserved, reserved_fill, sizeof(d._reserved));
        return d;
    };

    fixpp_decimal_t a_zero = make(0x00);
    fixpp_decimal_t b_garbage = make(0xFF);

    // Both represent 1.00; compare should be equal regardless of _reserved
    EXPECT_EQ(fixpp_decimal_compare(a_zero, b_garbage), 0);
    EXPECT_EQ(fixpp_decimal_equal(a_zero, b_garbage), 1);
}
