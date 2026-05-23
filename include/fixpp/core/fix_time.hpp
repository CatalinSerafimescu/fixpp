// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/core/fix_time.hpp
//
// fixpp::core time-helper #4 — FIX UTCTimestamp grammar formatter + parser.
//
// FOLDED core/ module-exit row #4 (D-3): this closes the core/ module-exit
// by providing the first real consumer — outbound SendingTime(52) stamping
// and inbound MaxLatency check.
//
// Anchors: data-model.md E8; research D-3; contracts/fix_time.hpp;
// [2d §4.1] (utc_time_point reuse). FR-012 / SC-007 / SC-010 / I-6 / US5.
//
// Grammar: FIX UTCTimestamp
//   YYYYMMDD-HH:MM:SS              (fix_time_precision::seconds)
//   YYYYMMDD-HH:MM:SS.sss          (fix_time_precision::millis — FIX 4.x default)
//   YYYYMMDD-HH:MM:SS.ssssss       (fix_time_precision::micros — where version permits)
//
// Precision: never coarser than seconds; format→parse is lossless at the
// emitted precision (truncation, not rounding, for sub-second surplus).
// The out-buffer overload avoids heap allocation ([const §VIII.5] / I-7).
//
// No `asio::awaitable` in this header — no std::mutex in this header.
// ([const §XV.9] mutex-in-awaitable-header grep gate: this is a pure-value
// synchronous helper; it is safe to include from awaitable coroutine headers.)
#pragma once

#include <chrono>
#include <cstdint>
#include <fixpp/core/error.hpp>  // expected_t / error
#include <span>

namespace fixpp::core {

// Reuse [2d §4.1] alias: utc_time_point is already declared in clock.hpp.
// Re-declare here independently so fix_time.hpp is self-contained (no forced
// pull of clock.hpp and its asio dependency from the session/core path).
using utc_time_point = std::chrono::time_point<std::chrono::system_clock>;

// ── Precision selector ───────────────────────────────────────────────────────

enum class fix_time_precision : std::uint8_t {
    seconds = 0,  // YYYYMMDD-HH:MM:SS          (15 chars)
    millis = 1,   // YYYYMMDD-HH:MM:SS.sss       (19 chars, FIX 4.x default)
    micros = 2,   // YYYYMMDD-HH:MM:SS.ssssss    (23 chars, where version permits)
};

// ── Format ───────────────────────────────────────────────────────────────────

// Format `tp` into the FIX UTCTimestamp grammar at the requested precision.
// Returns a sub-span of `out` covering the written characters (the rest of
// `out` is unmodified). The caller-supplied buffer must be at least:
//   - seconds:  17 bytes  (YYYYMMDD-HH:MM:SS)
//   - millis:   21 bytes  (YYYYMMDD-HH:MM:SS.sss)
//   - micros:   25 bytes  (YYYYMMDD-HH:MM:SS.ssssss)
// A 32-byte buffer covers all precisions.
//
// If `out` is too small, returns std::unexpected(error::decimal_buffer_too_small)
// (reuses the pre-existing capacity-error variant — no new error slot).
// All arithmetic is on the chrono types; no snprintf / no heap.
// noexcept: all chrono arithmetic + integer division is noexcept on all
// supported platforms (no IEEE 754 exceptions from integers).
[[nodiscard]] expected_t<std::span<char>> utc_time_to_fix_string(utc_time_point tp,
                                                                 fix_time_precision prec,
                                                                 std::span<char> out) noexcept;

// ── Parse ────────────────────────────────────────────────────────────────────

// Parse a FIX UTCTimestamp string (YYYYMMDD-HH:MM:SS[.sss[sss]]) into a
// utc_time_point. Returns unexpected on any grammar error (wrong length,
// out-of-range field, bad delimiter).
//
// Accepted lengths:
//   17 — seconds only      (YYYYMMDD-HH:MM:SS)
//   21 — milliseconds      (YYYYMMDD-HH:MM:SS.sss)
//   25 — microseconds      (YYYYMMDD-HH:MM:SS.ssssss)
// Any other length returns unexpected (currently mapped to
// error::wire_invalid_field_format — reuses the pre-existing wire-domain
// parse-error slot; no new error slot needed for the grammar error path).
//
// Round-trip guarantee: fix_string_to_utc_time(utc_time_to_fix_string(tp, P, buf))
// == time_point_cast<duration_for(P)>(tp) — lossless at emitted precision.
[[nodiscard]] expected_t<utc_time_point> fix_string_to_utc_time(std::span<const char> s) noexcept;

}  // namespace fixpp::core
