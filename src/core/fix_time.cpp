// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/core/fix_time.cpp
//
// fixpp::core FIX UTCTimestamp formatter + parser implementation (T011).
//
// Anchors: data-model.md E8; research D-3; contracts/fix_time.hpp;
// include/fixpp/core/fix_time.hpp. FR-012 / SC-007 / SC-010 / I-6.
//
// No snprintf, no strftime, no heap. Only integer arithmetic on chrono types.
// All functions are noexcept.
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <fixpp/core/error.hpp>
#include <fixpp/core/fix_time.hpp>
#include <span>
#include <utility>

namespace fixpp::core {

namespace {

// ── Integer serialisation helpers ─────────────────────────────────────────────

// Write exactly `width` decimal digits of `value` into `buf[0..width-1]`.
// Returns buf advanced by `width`.
// Precondition: value < 10^width.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) — internal helper; arg order matches the
// printf-like (value, width) convention every call site uses.
constexpr char* write_digits(char* buf, std::uint32_t value, int width) noexcept {
    // Write right-to-left for correctness, then done — avoids a reverse pass.
    for (int i = width - 1; i >= 0; --i) {
        buf[i] = static_cast<char>('0' + (value % 10));
        value /= 10;
    }
    return buf + width;
}

// Parse exactly `width` decimal digits from `s[0..width-1]`.
// Returns the value, or -1 on non-digit character.
constexpr std::int64_t parse_digits(const char* s, int width) noexcept {
    std::int64_t v = 0;
    for (int i = 0; i < width; ++i) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c < '0' || c > '9') {
            return -1;
        }
        v = (v * 10) + (c - '0');
    }
    return v;
}

// ── Calendar helpers ─────────────────────────────────────────────────────────
// Days since Unix epoch (1970-01-01) → calendar (year, month 1-12, day 1-31).
// Algorithm: proleptic Gregorian per [POSIX/ISO 8601]; valid for the FIX
// wire date range [19700101, 20991231] in practice.

struct date_t {
    std::int32_t year;
    std::uint8_t month;
    std::uint8_t day;
};

constexpr bool is_leap(std::int32_t y) noexcept {
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

constexpr std::uint8_t days_in_month(std::int32_t year, std::uint8_t month) noexcept {
    static constexpr std::uint8_t dom[13] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && is_leap(year)) {
        return 29;
    }
    return dom[month];
}

// Days since epoch → date.
constexpr date_t days_to_date(std::int32_t days) noexcept {
    // Algorithm from: http://howardhinnant.github.io/date_algorithms.html
    // "civil_from_days" — public domain, Howard Hinnant.
    std::int32_t z = days + 719468;
    std::int32_t era = (z >= 0 ? z : z - 146096) / 146097;
    auto doe = static_cast<std::uint32_t>(z - (era * 146097));
    std::uint32_t yoe = (doe - (doe / 1460) + (doe / 36524) - (doe / 146096)) / 365;
    std::int32_t y = static_cast<std::int32_t>(yoe) + (era * 400);
    std::uint32_t doy = doe - ((365 * yoe) + (yoe / 4) - (yoe / 100));
    std::uint32_t mp = ((5 * doy) + 2) / 153;
    auto d = static_cast<std::uint8_t>(doy - (((153 * mp) + 2) / 5) + 1);
    auto m = static_cast<std::uint8_t>(mp < 10 ? mp + 3 : mp - 9);
    y += static_cast<int>(m <= 2);
    return date_t{.year = y, .month = m, .day = d};
}

// Date → days since epoch.
constexpr std::int32_t date_to_days(std::int32_t y, std::uint8_t m, std::uint8_t d) noexcept {
    // Hinnant "days_from_civil".
    y -= static_cast<int>(m <= 2);
    std::int32_t era = (y >= 0 ? y : y - 399) / 400;
    auto yoe = static_cast<std::uint32_t>(y - (era * 400));
    std::uint32_t doy = (((153 * (m > 2 ? m - 3 : m + 9)) + 2) / 5) + d - 1;
    std::uint32_t doe = (yoe * 365) + (yoe / 4) - (yoe / 100) + doy;
    return (era * 146097) + static_cast<std::int32_t>(doe) - 719468;
}

}  // namespace

// ── Format ───────────────────────────────────────────────────────────────────

[[nodiscard]] expected_t<std::span<char>> utc_time_to_fix_string(utc_time_point tp,
                                                                 fix_time_precision prec,
                                                                 std::span<char> out) noexcept {
    using namespace std::chrono;

    // Minimum output size per precision. A `switch` with no `default` makes the
    // compiler (-Wswitch) flag a missing case if a precision is ever added — an
    // indexed size table would instead silently OOB-read on a new enumerator
    // (the exact pre-feature `nanos` bug this feature fixed).
    std::size_t needed = 0;
    switch (prec) {
        case fix_time_precision::seconds:
            needed = 17;
            break;  // YYYYMMDD-HH:MM:SS
        case fix_time_precision::millis:
            needed = 21;
            break;  // +.sss
        case fix_time_precision::micros:
            needed = 24;
            break;  // +.ssssss
        case fix_time_precision::nanos:
            needed = 27;
            break;  // +.sssssssss
    }
    if (out.size() < needed) {
        return std::unexpected(error::decimal_buffer_too_small);
    }

    // Decompose tp into seconds + sub-second remainder.
    const auto since_epoch = tp.time_since_epoch();
    // Duration in seconds (floor toward negative infinity for negative epochs).
    const auto sec_total = duration_cast<seconds>(since_epoch);
    // Sub-second nanoseconds (always non-negative: 0 ≤ ns < 1e9).
    const auto ns_rem = duration_cast<nanoseconds>(since_epoch - sec_total);

    // Days and time-of-day.
    const auto day_count = static_cast<std::int32_t>(sec_total.count() / 86400);
    std::int64_t time_of_day = sec_total.count() % 86400;
    // Handle negative epoch (before 1970) correctly.
    std::int32_t epoch_days = day_count;
    if (time_of_day < 0) {
        time_of_day += 86400;
        epoch_days -= 1;
    }

    const auto HH = static_cast<std::uint8_t>(time_of_day / 3600);
    const auto MM = static_cast<std::uint8_t>((time_of_day % 3600) / 60);
    const auto SS = static_cast<std::uint8_t>(time_of_day % 60);

    const date_t dt = days_to_date(epoch_days);

    char* p = out.data();
    // YYYYMMDD-HH:MM:SS
    p = write_digits(p, static_cast<std::uint32_t>(dt.year), 4);
    p = write_digits(p, dt.month, 2);
    p = write_digits(p, dt.day, 2);
    *p++ = '-';
    p = write_digits(p, HH, 2);
    *p++ = ':';
    p = write_digits(p, MM, 2);
    *p++ = ':';
    p = write_digits(p, SS, 2);

    // Sub-second suffix. A `switch` with no `default` makes the compiler
    // (-Wswitch) flag a missing case if a precision is ever added — the missing
    // `nanos` arm in the old if/else-if chain silently fell through to the
    // 17-char seconds form (the second bug this feature fixed).
    switch (prec) {
        case fix_time_precision::seconds:
            break;  // no sub-second suffix
        case fix_time_precision::millis:
            // .sss — truncate nanoseconds to milliseconds.
            *p++ = '.';
            p = write_digits(p, static_cast<std::uint32_t>(ns_rem.count() / 1'000'000), 3);
            break;
        case fix_time_precision::micros:
            // .ssssss — truncate nanoseconds to microseconds.
            *p++ = '.';
            p = write_digits(p, static_cast<std::uint32_t>(ns_rem.count() / 1'000), 6);
            break;
        case fix_time_precision::nanos:
            // .sssssssss — full nanosecond precision (9 digits, zero-padded).
            // ns_rem < 1e9 < 2^32, so the cast to uint32_t is safe.
            *p++ = '.';
            p = write_digits(p, static_cast<std::uint32_t>(ns_rem.count()), 9);
            break;
    }

    return std::span<char>{out.data(), static_cast<std::size_t>(p - out.data())};
}

// ── Parse ────────────────────────────────────────────────────────────────────

[[nodiscard]] expected_t<utc_time_point> fix_string_to_utc_time(std::span<const char> s) noexcept {
    using namespace std::chrono;

    // Lenient grammar (contract C3, data-model E3, Gate A RC#4):
    //   Accept bare length-17 (YYYYMMDD-HH:MM:SS, no dot) OR
    //   dot at index 17 + 1..9 ASCII digits (total length 19..27).
    //   Reject:
    //     - length 18 (dot + 0 fraction digits — empty fraction)
    //     - total length > 27 (fraction width > 9) — checked BEFORE digit parse
    //       (a 10-digit value 9,999,999,999 fits in int64 and must be caught by
    //       the width gate, NOT by arithmetic overflow — contract C3)
    //     - a '.' anywhere other than index 17
    //     - any non-digit fraction character
    //     - malformed base (handled by field-by-field checks below)
    const std::size_t len = s.size();
    // Width gate: total length must be 17 (bare) or 19..27 (dot + 1..9 digits).
    if (len < 17 || len == 18 || len > 27) {
        return std::unexpected(error::wire_invalid_field_format);
    }
    // If length > 17, index 17 must be '.'. (Fraction digits are validated in the
    // single accumulation pass below — no separate validation walk.)
    if (len > 17 && s[17] != '.') {
        return std::unexpected(error::wire_invalid_field_format);
    }

    const char* p = s.data();

    // YYYY
    const std::int64_t year_i = parse_digits(p, 4);
    if (year_i < 0) {
        return std::unexpected(error::wire_invalid_field_format);
    }
    const auto year = static_cast<std::int32_t>(year_i);
    p += 4;

    // MM
    const std::int64_t month_i = parse_digits(p, 2);
    if (month_i < 1 || month_i > 12) {
        return std::unexpected(error::wire_invalid_field_format);
    }
    const auto month = static_cast<std::uint8_t>(month_i);
    p += 2;

    // DD
    const std::int64_t day_i = parse_digits(p, 2);
    if (day_i < 1 || day_i > 31) {
        return std::unexpected(error::wire_invalid_field_format);
    }
    const auto day = static_cast<std::uint8_t>(day_i);
    p += 2;

    // Validate day against actual month/year.
    if (std::cmp_greater(day_i, days_in_month(year, month))) {
        return std::unexpected(error::wire_invalid_field_format);
    }

    // '-'
    if (*p++ != '-') {
        return std::unexpected(error::wire_invalid_field_format);
    }

    // HH
    const std::int64_t hh_i = parse_digits(p, 2);
    if (hh_i < 0 || hh_i > 23) {
        return std::unexpected(error::wire_invalid_field_format);
    }
    p += 2;

    // ':'
    if (*p++ != ':') {
        return std::unexpected(error::wire_invalid_field_format);
    }

    // MM
    const std::int64_t mm_i = parse_digits(p, 2);
    if (mm_i < 0 || mm_i > 59) {
        return std::unexpected(error::wire_invalid_field_format);
    }
    p += 2;

    // ':'
    if (*p++ != ':') {
        return std::unexpected(error::wire_invalid_field_format);
    }

    // SS
    const std::int64_t ss_i = parse_digits(p, 2);
    if (ss_i < 0 || ss_i > 60) {  // allow 60 for leap-second representation
        return std::unexpected(error::wire_invalid_field_format);
    }
    p += 2;

    // Sub-second (lenient: 0..9 fraction digits, scaled to nanoseconds).
    // The width gate (len) and dot-presence are verified above; each fraction
    // char is validated here in the same pass that accumulates it (fail-closed —
    // a non-digit returns before `frac` is ever scaled/used).
    // fraction_width = len - 18 (0 for bare length-17, 1..9 for dot + N digits).
    std::int64_t ns_sub = 0;
    if (len > 17) {
        // p currently points at the dot (index 17); skip it.
        ++p;                                          // skip '.'
        const std::size_t fraction_width = len - 18;  // N = number of fraction digits
        std::int64_t frac = 0;
        for (std::size_t i = 0; i < fraction_width; ++i) {
            const char c = *p++;
            if (c < '0' || c > '9') {
                return std::unexpected(error::wire_invalid_field_format);
            }
            frac = (frac * 10) + (c - '0');
        }
        // Scale to nanoseconds: multiply by 10^(9 - fraction_width).
        // Scale factors for N=0..9 (N=0 is bare/no-dot — unreachable here since len>17 → N≥1):
        static constexpr std::int64_t scale[10] = {
            1'000'000'000LL,  // N=0 (unused — bare 17-char takes len==17 branch)
            100'000'000LL,    // N=1
            10'000'000LL,     // N=2
            1'000'000LL,      // N=3 (millis)
            100'000LL,        // N=4
            10'000LL,         // N=5
            1'000LL,          // N=6 (micros)
            100LL,            // N=7
            10LL,             // N=8
            1LL,              // N=9 (nanos)
        };
        ns_sub = frac * scale[fraction_width];
    }

    // Build the epoch nanosecond offset.
    const std::int32_t epoch_days = date_to_days(year, month, day);
    const std::int64_t epoch_sec =
        (static_cast<std::int64_t>(epoch_days) * 86400LL) + (hh_i * 3600LL) + (mm_i * 60LL) + ss_i;

    // Compose as nanoseconds and convert to system_clock duration.
    const auto ns_total = std::chrono::nanoseconds{(epoch_sec * 1'000'000'000LL) + ns_sub};

    // system_clock::duration is typically nanoseconds on Linux; truncate to
    // the clock's native resolution if coarser.
    return utc_time_point{duration_cast<system_clock::duration>(ns_total)};
}

}  // namespace fixpp::core
