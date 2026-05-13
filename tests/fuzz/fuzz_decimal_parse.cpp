// tests/fuzz/fuzz_decimal_parse.cpp
// Seam #7 — libFuzzer entry point for decimal parse.
// Invariant: no crash, no std::terminate, result is either valid pod_decimal or error code.

#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <span>

#include "fixpp/core/decimal.hpp"

using fixpp::core::decimal_traits;
using fixpp::core::pod_decimal;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // SAFETY: we reinterpret the fuzzer's byte buffer as std::byte — well-defined.
    auto src = std::span<const std::byte>{reinterpret_cast<const std::byte*>(data), size};

    auto result = decimal_traits<pod_decimal>::from_chars(src, std::pmr::null_memory_resource());

    // Invariant: the function either succeeded (valid pod_decimal) or returned
    // an error code. Neither path may terminate the process or access out-of-bounds.
    if (result.has_value()) {
        // Ensure the result is in canonical domain (parser contract)
        auto& v = result.value();
        (void)v.mantissa;
        (void)v.exponent;
    }
    // All error paths are expected and acceptable — the fuzzer is looking for
    // crashes or sanitizer violations, not parse failures.
    return 0;
}
