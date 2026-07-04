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
        // Parser contract: a successful parse MUST be in the canonical domain —
        // exponent ∈ [-38, 0] and mantissa ≠ the INT64_MIN sentinel. Trap loudly if
        // from_chars ever regresses to emit an out-of-domain value. Unconditional
        // (__builtin_trap, not assert) so it fires even under NDEBUG fuzz builds
        // (TV-1: the prior harness named this invariant but asserted nothing).
        const auto& v = result.value();
        if (v.mantissa == INT64_MIN || v.exponent < -38 || v.exponent > 0) {
            __builtin_trap();
        }
    }
    // All error paths are expected and acceptable — the fuzzer is looking for
    // crashes or sanitizer violations, not parse failures.
    return 0;
}
