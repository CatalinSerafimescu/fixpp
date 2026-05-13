// tests/core/decimal_capi_error_test.cpp
// Coverage for defensive paths in src/capi/decimal.cpp:
//   - null-pointer guards in parse/format/init
//   - map_error() via invalid-input and buffer-too-small paths
// Complements decimal_capi_layout_test.cpp (happy-path CAPI coverage).

#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>

#include "fix/c_api/decimal.h"

// ── fixpp_decimal_parse null guards ──────────────────────────────────────────

TEST(DecimalCAPIErrorPaths, ParseNullSrc) {
    fixpp_decimal_t out = FIXPP_DECIMAL_INITIALIZER;
    EXPECT_EQ(fixpp_decimal_parse(nullptr, 0, &out), FIXPP_ERR_DECIMAL_INVALID);
}

TEST(DecimalCAPIErrorPaths, ParseNullOut) {
    EXPECT_EQ(fixpp_decimal_parse("1.0", 3, nullptr), FIXPP_ERR_DECIMAL_INVALID);
}

// ── fixpp_decimal_parse error propagation (exercises map_error) ──────────────

TEST(DecimalCAPIErrorPaths, ParseInvalidInputCallsMapError) {
    fixpp_decimal_t out = FIXPP_DECIMAL_INITIALIZER;
    // Non-numeric input → error::decimal_invalid_input → FIXPP_ERR_DECIMAL_INVALID
    EXPECT_EQ(fixpp_decimal_parse("abc", 3, &out), FIXPP_ERR_DECIMAL_INVALID);
}

TEST(DecimalCAPIErrorPaths, ParseEmptyInput) {
    fixpp_decimal_t out = FIXPP_DECIMAL_INITIALIZER;
    EXPECT_EQ(fixpp_decimal_parse("", 0, &out), FIXPP_ERR_DECIMAL_INVALID);
}

// ── fixpp_decimal_format null guards ─────────────────────────────────────────

TEST(DecimalCAPIErrorPaths, FormatNullDst) {
    fixpp_decimal_t d{1, 0, {}};
    size_t written = 0;
    EXPECT_EQ(fixpp_decimal_format(d, nullptr, 0, &written), FIXPP_ERR_DECIMAL_INVALID);
}

TEST(DecimalCAPIErrorPaths, FormatNullWritten) {
    fixpp_decimal_t d{1, 0, {}};
    char buf[16]{};
    EXPECT_EQ(fixpp_decimal_format(d, buf, sizeof(buf), nullptr), FIXPP_ERR_DECIMAL_INVALID);
}

// ── fixpp_decimal_format error propagation (exercises map_error) ─────────────

TEST(DecimalCAPIErrorPaths, FormatBufferTooSmallCallsMapError) {
    fixpp_decimal_t d{123456789, -5, {}};  // "1234.56789" — 10 chars + NUL
    char buf[4]{};                          // way too small
    size_t written = 0;
    EXPECT_EQ(fixpp_decimal_format(d, buf, sizeof(buf), &written), FIXPP_ERR_BUFFER_TOO_SMALL);
}

// ── fixpp_decimal_init null guard ─────────────────────────────────────────────

TEST(DecimalCAPIErrorPaths, InitNullIsNoop) {
    // Must not crash; no return value to check.
    fixpp_decimal_init(nullptr);
}
