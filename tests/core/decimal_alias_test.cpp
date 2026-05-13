// tests/core/decimal_alias_test.cpp
// US2 — build-time alias macro AC-B1..B4

#include "fixpp/core/decimal_alias.hpp"

#include <gtest/gtest.h>

#include <type_traits>

using fixpp::core::decimal;
using fixpp::core::pod_decimal;

// AC-B1: default fixpp::decimal_t == decimal<pod_decimal>
static_assert(std::is_same_v<fixpp::decimal_t, decimal<pod_decimal>>,
              "AC-B1: fixpp::decimal_t must default to decimal<pod_decimal>");

// AC-B3 (positive case): sentinel symbol links (no unresolved symbol when alias matches)
// The fact that this TU compiles and links is the positive case.
// The link error case is tested by tests/link/decimal_alias_mismatch_test.cmake.
TEST(DecimalAlias, SentinelLinksPositiveCase) {
    // Accessing the lock forces the linker to resolve the sentinel symbol.
    auto* lock = fixpp::detail::fixpp_decimal_alias_lock;
    EXPECT_NE(lock, nullptr);
}

// AC-B4: switching alias does not change fixpp_decimal_t C-ABI shape
// The C-ABI struct fixpp_decimal_t is independent of FIXPP_DECIMAL_T.
// Verified by static_asserts in decimal_capi_layout_test.cpp (US3).
TEST(DecimalAlias, AliasDoesNotAffectCABIShape) {
    // This is a compile-time property verified in US3.
    SUCCEED();
}

// Additional: decimal_t usable as a value type
TEST(DecimalAlias, DecimalTUsable) {
    fixpp::decimal_t d{};
    EXPECT_EQ(d.value().mantissa, 0);
    EXPECT_EQ(d.value().exponent, 0);
}
