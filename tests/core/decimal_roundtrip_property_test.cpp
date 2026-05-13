// tests/core/decimal_roundtrip_property_test.cpp
// Seam #2 — round-trip property: encode → parse → re-encode → compare equal.
// Covers AC-P5, AC-S2..S4. 10^4 generated canonical-domain samples +
// FIX50SP2 §3.3 example table entries.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <span>
#include <string_view>

#include "fixpp/core/decimal.hpp"

using fixpp::core::decimal_traits;
using fixpp::core::pod_decimal;

namespace {

struct RoundtripResult {
    bool ok{};
    std::string msg;
};

RoundtripResult check_roundtrip(pod_decimal orig) {
    // Step 1: format orig → bytes
    std::array<std::byte, 64> buf{};
    auto fmt = decimal_traits<pod_decimal>::to_chars(orig, std::span<std::byte>{buf});
    if (!fmt.has_value()) return {false, "to_chars failed on original"};

    // Step 2: parse bytes back
    auto parsed = decimal_traits<pod_decimal>::from_chars(
        std::span<const std::byte>{buf.data(), *fmt}, std::pmr::null_memory_resource());
    if (!parsed.has_value()) return {false, "from_chars failed on re-encoded bytes"};

    // Step 3: compare value-equal
    auto cmp = decimal_traits<pod_decimal>::compare(orig, *parsed);
    if (cmp != std::strong_ordering::equal) return {false, "re-parsed value differs from original"};

    return {true, {}};
}

}  // namespace

// 10^4 generated canonical-domain samples
TEST(DecimalRoundtrip, GeneratedSamples) {
    // Walk a deterministic grid over the canonical domain
    constexpr std::int64_t mantissa_steps[] = {1,
                                               7,
                                               13,
                                               37,
                                               100,
                                               999,
                                               1234,
                                               9999,
                                               99999,
                                               1000000,
                                               9999999,
                                               123456789,
                                               9999999999LL,
                                               99999999999LL,
                                               999999999999LL,
                                               9223372036854775806LL};
    constexpr std::int8_t exp_steps[] = {0, -1, -2, -5, -10, -18, -38};

    int count = 0;
    for (auto m : mantissa_steps) {
        for (auto e : exp_steps) {
            for (int sign : {1, -1}) {
                std::int64_t mantissa = sign * m;
                pod_decimal v{mantissa, e};
                auto r = check_roundtrip(v);
                EXPECT_TRUE(r.ok) << "mantissa=" << mantissa << " exp=" << (int)e
                                  << " reason: " << r.msg;
                ++count;
                if (count >= 10000) goto done;
            }
        }
    }
    // Fill remaining count to 10^4 with a simple stride
    for (std::int64_t m = 1; count < 10000; m += 97, ++count) {
        std::int8_t e = static_cast<std::int8_t>(-(m % 38));
        auto r = check_roundtrip(pod_decimal{m % 9223372036854775806LL + 1, e});
        EXPECT_TRUE(r.ok) << r.msg;
    }
done:;
}

// FIX50SP2 §3.3 example table entries
TEST(DecimalRoundtrip, FIX50SP2Examples) {
    // 1.5 → {15, -1}
    EXPECT_TRUE(check_roundtrip(pod_decimal{15, -1}).ok);
    // 0.0025 → {25, -4}
    EXPECT_TRUE(check_roundtrip(pod_decimal{25, -4}).ok);
    // 100 → {100, 0}
    EXPECT_TRUE(check_roundtrip(pod_decimal{100, 0}).ok);
    // 0 → {0, 0}
    EXPECT_TRUE(check_roundtrip(pod_decimal{0, 0}).ok);
    // -0.001 → {-1, -3}
    EXPECT_TRUE(check_roundtrip(pod_decimal{-1, -3}).ok);
}
