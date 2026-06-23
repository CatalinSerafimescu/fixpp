// tests/alloc_guard/decimal_alloc_guard_test.cpp
// Seam #6 — parse-format loop that must not allocate between parse and format.
// Run under mallocnesia interceptor via tools/check_alloc.py.
// Zero heap allocations between the two marked boundaries is the invariant.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <memory_resource>
#include <span>

#include "fixpp/core/decimal.hpp"

using fixpp::core::decimal;
using fixpp::core::decimal_traits;
using fixpp::core::pod_decimal;

// The alloc-guard interceptor watches for malloc/free between
// the ALLOC_GUARD_START / ALLOC_GUARD_END markers below.
// These are defined as no-ops here; mallocnesia replaces them.
#include "support/alloc_guard_markers.hpp"

TEST(DecimalAllocGuard, ParseFormatLoopNoAlloc) {
    constexpr const char kInput[] = "12345.678";
    constexpr std::size_t kLen = sizeof(kInput) - 1;

    std::array<std::byte, kLen> src{};
    for (std::size_t i = 0; i < kLen; ++i) src[i] = static_cast<std::byte>(kInput[i]);

    std::array<std::byte, 64> dst{};
    // Repeat 1000 times to give the interceptor a statistical chance to catch leaks
    if (alloc_guard_start) alloc_guard_start();
    for (int i = 0; i < 1000; ++i) {
        auto parsed = decimal<pod_decimal>::parse(std::span<const std::byte>{src},
                                                  std::pmr::null_memory_resource());
        ASSERT_TRUE(parsed.has_value());

        auto written = parsed->format(std::span<std::byte>{dst});
        ASSERT_TRUE(written.has_value());
    }
    if (alloc_guard_end) alloc_guard_end();
}
