// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/log/record.hpp
//
// ArgValue (24-byte trivially-copyable tagged union) and Record (256-byte
// trivially-copyable cache-aligned log record) for fixpp::log.
//
// Anchors:
//   [2k §4.2] — exact byte layout (locked; do not change without amendment)
//   data-model.md §ArgValue / §Record
//   contracts/log-core.md (sizeof/trivially-copyable static_assert obligations)
//   [arch §2.3] — log → {core} only
//
// SIZE MATH (post RC#5 ArgValue fix):
//   ArgValue: 1 (kind) + 7 (_pad) + 16 (union) = 24 bytes
//   Record header: 8 (timestamp) + 16 (trace_id) + 8 (span_id)
//                  + 1 (level) + 1 (flags) + 2 (category) + 4 (format_id)
//                  + 1 (arg_count) + 5 (_pad) = 48 bytes
//   Args: 6 × 24 = 144 bytes
//   Pad: 64 bytes (_cache_pad)
//   Total: 48 + 144 + 64 = 256 bytes = 4 cache lines ✓
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <type_traits>

// fix_time.hpp declares fixpp::core::utc_time_point without pulling in
// clock.hpp's asio/awaitable.hpp dependency ([const §XV.9] layer compliance:
// log → {core} must not transitively import asio on the INTERFACE target).
#include <fixpp/core/fix_time.hpp>
#include <fixpp/log/level.hpp>

namespace fixpp::log {

// ── ArgValue ──────────────────────────────────────────────────────────────────
//
// A 24-byte trivially-copyable tagged union that carries a single log argument
// across the MPSC ring without any heap allocation.
//
// String safety:
//   - InlineStr: up to 15 bytes of payload, NOT null-terminated; drain reads
//     exactly `len` bytes. Use for short runtime strings (CompIDs, MsgType…).
//     Truncated silently at 15 bytes.
//   - StaticStr: const char* with caller-asserted static (or arena) lifetime.
//     Use FIXPP_SLIT("literal") for string literals. Passing a stack-local
//     pointer is UB — the drain thread reads the pointer after the producer's
//     stack frame is gone.
//   - Never store a std::string_view: the pointed-to data can be destroyed
//     before the drain thread processes the record.
struct ArgValue {
    enum class Kind : uint8_t {
        empty      = 0,
        u64        = 1,
        i64        = 2,
        f64        = 3,
        bool_val   = 4,
        inline_str = 5,   // up to 15 bytes inline; NOT null-terminated; use len
        static_str = 6,   // const char* with caller-asserted stable lifetime
    };

    // Inline string storage: 15 bytes of payload + 1 length byte = 16 bytes.
    // data[0..len-1] holds the string payload. NOT null-terminated.
    // `len` is in [0, 15].
    struct InlineStr {
        char    data[15];
        uint8_t len;   // actual byte count (≤ 15)
    };

    // Layout: 1 (kind) + 7 (_pad) + 16 (union) = 24 bytes
    Kind    kind {};
    uint8_t _pad[7] {};
    union {
        uint64_t    u64;
        int64_t     i64;
        double      f64;
        bool        b;
        InlineStr   inl;         // 16 bytes (char[15] + uint8_t len)
        const char* static_ptr;  // StaticStr: caller-asserted stable lifetime
    };

    // ── Convenience constructors ──────────────────────────────────────────────

    static ArgValue from_u64(uint64_t v) noexcept {
        ArgValue a;
        a.kind = Kind::u64;
        a.u64  = v;
        return a;
    }

    static ArgValue from_i64(int64_t v) noexcept {
        ArgValue a;
        a.kind = Kind::i64;
        a.i64  = v;
        return a;
    }

    static ArgValue from_f64(double v) noexcept {
        ArgValue a;
        a.kind = Kind::f64;
        a.f64  = v;
        return a;
    }

    static ArgValue from_bool(bool v) noexcept {
        ArgValue a;
        a.kind = Kind::bool_val;
        a.b    = v;
        return a;
    }

    // Inline string: copies up to 15 bytes; safe for any runtime string_view.
    // Strings longer than 15 bytes are silently truncated to 15 bytes.
    // The stored bytes are NOT null-terminated; the drain thread uses len.
    static ArgValue from_inline(std::string_view sv) noexcept {
        ArgValue a;
        a.kind      = Kind::inline_str;
        auto n      = std::min(sv.size(), std::size_t{15});
        std::memcpy(a.inl.data, sv.data(), n);
        a.inl.len   = static_cast<uint8_t>(n);
        return a;
    }

    // Static string: caller MUST guarantee the pointed-to bytes outlive the
    // drain thread processing this record. Use FIXPP_SLIT for string literals.
    static ArgValue from_static(const char* p) noexcept {
        ArgValue a;
        a.kind       = Kind::static_str;
        a.static_ptr = p;
        return a;
    }
};

// ArgValue must be exactly 24 bytes: 1 (kind) + 7 (_pad) + 16 (union).
// The == form is required (not <=) because Record sizing depends on it:
//   48 header + 6 × 24 = 192 + 64 pad = 256 bytes.
static_assert(sizeof(ArgValue) == 24,
              "ArgValue must be exactly 24 bytes for cache-line efficiency");
static_assert(std::is_trivially_copyable_v<ArgValue>,
              "ArgValue must be trivially copyable (placed on MPSC ring via memcpy)");

// ── FIXPP_SLIT ────────────────────────────────────────────────────────────────
//
// Helper macro for static-lifetime string arguments.
// Expands to ArgValue::from_static("literal").
// String literals have static storage duration per the C++ standard.
// Do NOT use with non-literal expressions.
#define FIXPP_SLIT(s) (::fixpp::log::ArgValue::from_static(s))

// ── Record ────────────────────────────────────────────────────────────────────
//
// Maximum number of variadic args captured per log call.
inline constexpr std::size_t k_max_args = 6;

// A Record is a fixed-size POD placed directly onto the MPSC ring buffer.
// Sized to exactly 4 cache lines (256 bytes) to eliminate false-sharing.
// alignas(64) ensures each Record starts on a cache-line boundary.
//
// Layout (see SIZE MATH at top of file):
//   Offset  0: timestamp    (8)
//   Offset  8: trace_id     (16)
//   Offset 24: span_id      (8)
//   Offset 32: level        (1)
//   Offset 33: flags        (1)  reserved
//   Offset 34: category     (2)
//   Offset 36: format_id    (4)
//   Offset 40: arg_count    (1)
//   Offset 41: _pad[5]      (5)
//   ---- 48 bytes ----
//   Offset 48: args[6]      (144)  = 6 × ArgValue × 24 bytes
//   Offset 192: _cache_pad  (64)
//   Total: 256 bytes
struct alignas(64) Record {
    // Wall-clock UTC timestamp from effective_clock.now() per [2d §7.9].
    fixpp::core::utc_time_point  timestamp;           //  8 bytes  [offset  0]

    // OTel correlation (LOG-003). Zeroed when no session context is available
    // (FIXPP_LOG0 / context-free code paths — see [2k §6.4]; not a bug).
    std::array<std::uint8_t, 16> trace_id {};         // 16 bytes  [offset  8]
    std::uint64_t                span_id  {};         //  8 bytes  [offset 24]

    Level         level;                              //  1 byte   [offset 32]
    std::uint8_t  flags    {};                        //  1 byte   [offset 33]  reserved
    Category      category;                           //  2 bytes  [offset 34]
    std::uint32_t format_id;                          //  4 bytes  [offset 36]  CRC32 of fmt
    std::uint8_t  arg_count;                          //  1 byte   [offset 40]
    std::uint8_t  _pad[5]  {};                        //  5 bytes  [offset 41]
    // ── 48 bytes ──────────────────────────────────────────────────────────────

    // Captured args (up to k_max_args = 6 at exactly 24 bytes each).
    std::array<ArgValue, k_max_args> args {};         // 144 bytes [offset 48]

    // Padding to 256 bytes (4 cache lines). Prevents false-sharing with the
    // ring slot sequence counter placed adjacent to the record in the ring.
    std::uint8_t _cache_pad[64] {};                   //  64 bytes [offset 192]
};

// Record must be exactly 256 bytes and trivially copyable.
// The ring places it via a single CAS + 256-byte memcpy (zero alloc).
static_assert(sizeof(Record) == 256,
              "Record must be exactly 256 bytes (4 cache lines)");
static_assert(std::is_trivially_copyable_v<Record>,
              "Record must be trivially copyable (placed on MPSC ring via memcpy)");
static_assert(alignof(Record) == 64,
              "Record must be aligned to 64 bytes (cache-line boundary)");

}  // namespace fixpp::log
