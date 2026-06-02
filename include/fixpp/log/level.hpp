// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/log/level.hpp
//
// Log Level enum and Category type alias for the fixpp::log module.
//
// Anchors:
//   [2k §4.1] — Level/Category definitions (locked surface)
//   data-model.md §Level / §Category
//   contracts/log-core.md (compile-time obligations)
//   [arch §2.3] — log → {core} only (no transport/session/otel includes)
//
// FIXPP_LOG_MIN_LEVEL is a build-time integer (0=trace..5=fatal) propagated
// via the fixpp_log CMake INTERFACE target. This header uses it only in
// the FIXPP_LOG_CATEGORY collision static_assert — the macro-level if constexpr
// gate lives in logger.hpp (slice 3b).
#pragma once

#include <cstdint>

namespace fixpp::log {

// ── Level ─────────────────────────────────────────────────────────────────────
//
// Numeric values are STABLE: never reorder, extend only at the high end.
// uint8_t backing matches [2k §4.1] and keeps sizeof(Record) at 256.
enum class Level : std::uint8_t {
    trace = 0,
    debug = 1,
    info  = 2,
    warn  = 3,
    error = 4,
    fatal = 5,
};

// ── Category ──────────────────────────────────────────────────────────────────
//
// A uint16_t interned at compile time from a string literal via CRC32.
// No heap allocation. The value is stored in Record::category.
//
// Runtime filter: Logger maintains an atomic<uint64_t> enabled_categories_mask_.
// The bit index for category C is (C & 63u) — valid for any uint16 value (no
// 1ull << n UB when n >= 64). Built-in categories (values 1–8) occupy bit
// indices 1–8 and are collision-free by construction.
using Category = std::uint16_t;

namespace cat {
    // Pre-defined compile-time category constants.
    // Values 0x0001..0x0008 ⇒ mask-bit indices 1..8.
    inline constexpr Category session   = 0x0001;
    inline constexpr Category wire      = 0x0002;
    inline constexpr Category transport = 0x0003;
    inline constexpr Category tls       = 0x0004;
    inline constexpr Category store     = 0x0005;
    inline constexpr Category otel      = 0x0006;
    inline constexpr Category control   = 0x0007;
    inline constexpr Category user      = 0x0008;
}  // namespace cat

// ── Compile-time CRC32 ────────────────────────────────────────────────────────
//
// A constexpr CRC32 computation over a string literal, used by
// FIXPP_LOG_CATEGORY to intern user-defined category names.
namespace detail {

// CRC32 lookup table entry computed at constexpr time.
constexpr std::uint32_t crc32_poly = 0xEDB88320u;

constexpr std::uint32_t crc32_byte(std::uint32_t crc, unsigned char byte) noexcept {
    crc ^= static_cast<std::uint32_t>(byte);
    for (int i = 0; i < 8; ++i) {
        crc = (crc >> 1) ^ ((crc & 1u) ? crc32_poly : 0u);
    }
    return crc;
}

// Compute CRC32 of a null-terminated string literal at compile time.
// Returns the 32-bit CRC (IEEE 802.3 / PKzip convention, init=0xFFFFFFFF,
// final XOR=0xFFFFFFFF), truncated to uint16_t for use as a Category.
constexpr std::uint32_t crc32_str(const char* s) noexcept {
    std::uint32_t crc = 0xFFFFFFFFu;
    while (*s != '\0') {
        crc = crc32_byte(crc, static_cast<unsigned char>(*s));
        ++s;
    }
    return crc ^ 0xFFFFFFFFu;
}

// Low-6-bit mask used for the category → bit-index mapping.
// Built-in categories 1..8 occupy bit indices 1..8.
// The FORBIDDEN zone is bits 1..8 (category & 63u in {1..8}).
constexpr bool is_builtin_bit_index(std::uint16_t cat_value) noexcept {
    std::uint16_t idx = static_cast<std::uint16_t>(cat_value & 63u);
    return idx >= 1u && idx <= 8u;
}

}  // namespace detail

// ── FIXPP_LOG_CATEGORY ────────────────────────────────────────────────────────
//
// Defines a compile-time Category constant for a user-defined log category.
// Usage: constexpr Category my_cat = FIXPP_LOG_CATEGORY("my_cat");
//
// The macro performs a build-time collision check: if the CRC32 of the name
// produces a low-6-bit value (bit index) that collides with any built-in
// category (bits 1–8), the build fails with a diagnostic.
//
// Two user categories whose CRC32 values have the same low-6-bits share the
// same mask bit; this is documented and the user is responsible for avoiding
// such aliases (or treating them as an intentional grouping).
//
// [2k §4.1] / data-model.md §Category / contracts/log-core.md.
#define FIXPP_LOG_CATEGORY(name)                                                \
    ([]() constexpr -> ::fixpp::log::Category {                                 \
        constexpr auto _crc   = ::fixpp::log::detail::crc32_str(name);         \
        constexpr auto _cat16 = static_cast<::fixpp::log::Category>(           \
            static_cast<std::uint16_t>(_crc));                                  \
        static_assert(                                                           \
            !::fixpp::log::detail::is_builtin_bit_index(_cat16),               \
            "FIXPP_LOG_CATEGORY(\"" name "\"): CRC32 low-6-bits collide with " \
            "a built-in category bit (indices 1–8). Choose a different name."); \
        return _cat16;                                                           \
    }())

}  // namespace fixpp::log
