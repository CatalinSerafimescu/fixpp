// tests/fuzz/fuzz_decimal_compare.cpp
// 060-int128-decimal-compare — T010.
//
// Differential libFuzzer target: decodes two pod_decimal values from the
// input bytes and asserts decimal_traits<pod_decimal>::compare (the
// production comparator, post-T005 wide-integer swap) agrees bit-for-bit
// with reference_compare (the FROZEN pre-T005 digit-string comparator,
// shared with tests/core/decimal_compare_diff_oracle_test.cpp via
// decimal_reference_compare.hpp). A mismatch traps immediately (SC-006).

#include <cstdint>
#include <cstring>

#include "fixpp/core/decimal.hpp"
#include "core/decimal_reference_compare.hpp"

using fixpp::core::decimal_traits;
using fixpp::core::pod_decimal;

namespace {

// Test-local sign-inversion helper for the antisymmetry check below —
// independent of any production helper.
std::strong_ordering invert_order(std::strong_ordering o) noexcept {
    if (o == std::strong_ordering::less) {
        return std::strong_ordering::greater;
    }
    if (o == std::strong_ordering::greater) {
        return std::strong_ordering::less;
    }
    return std::strong_ordering::equal;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // Two pod_decimals: int64 mantissa + int8 exponent each, 9 bytes apiece.
    constexpr size_t kPodDecimalBytes = sizeof(std::int64_t) + sizeof(std::int8_t);  // 9
    constexpr size_t kNeeded = 2 * kPodDecimalBytes;                                 // 18
    if (size < kNeeded) {
        return 0;
    }

    std::int64_t mantissa_a = 0;
    std::int64_t mantissa_b = 0;
    std::int8_t exponent_a = 0;
    std::int8_t exponent_b = 0;

    // SAFETY: memcpy from the fuzzer's raw byte buffer into POD scalars —
    // well-defined, no strict-aliasing/alignment UB.
    size_t off = 0;
    std::memcpy(&mantissa_a, data + off, sizeof(mantissa_a));
    off += sizeof(mantissa_a);
    std::memcpy(&exponent_a, data + off, sizeof(exponent_a));
    off += sizeof(exponent_a);
    std::memcpy(&mantissa_b, data + off, sizeof(mantissa_b));
    off += sizeof(mantissa_b);
    std::memcpy(&exponent_b, data + off, sizeof(exponent_b));

    pod_decimal const a{mantissa_a, exponent_a};
    pod_decimal const b{mantissa_b, exponent_b};

    auto const production_ab = decimal_traits<pod_decimal>::compare(a, b);
    auto const reference_ab = reference_compare(a, b);
    if (production_ab != reference_ab) {
        __builtin_trap();
    }

    // Antisymmetry: compare(a,b) == invert(compare(b,a)) for the production
    // comparator — cheap extra soundness check, independent of the
    // reference oracle.
    auto const production_ba = decimal_traits<pod_decimal>::compare(b, a);
    if (production_ab != invert_order(production_ba)) {
        __builtin_trap();
    }

    return 0;
}
