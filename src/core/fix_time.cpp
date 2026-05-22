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
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) — internal helper; arg order matches the printf-like (value, width) convention every call site uses.
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
    return date_t{.year=y, .month=m, .day=d};
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

    // Minimum output sizes per precision level.
    // YYYYMMDD-HH:MM:SS = 8+1+2+1+2+1+2 = 17 chars (seconds)
    // YYYYMMDD-HH:MM:SS.sss = 17+1+3 = 21 chars (millis)
    // YYYYMMDD-HH:MM:SS.ssssss = 17+1+6 = 24 chars (micros)
    constexpr std::size_t min_size[3] = {17, 21, 24};
    const std::size_t needed = min_size[static_cast<std::uint8_t>(prec)];
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

    if (prec == fix_time_precision::millis) {
        // .sss — truncate nanoseconds to milliseconds.
        const auto ms = static_cast<std::uint32_t>(ns_rem.count() / 1'000'000);
        *p++ = '.';
        p = write_digits(p, ms, 3);
    } else if (prec == fix_time_precision::micros) {
        // .ssssss — truncate nanoseconds to microseconds.
        const auto us = static_cast<std::uint32_t>(ns_rem.count() / 1'000);
        *p++ = '.';
        p = write_digits(p, us, 6);
    }
    // fix_time_precision::seconds — no sub-second suffix.

    return std::span<char>{out.data(), static_cast<std::size_t>(p - out.data())};
}

// ── Parse ────────────────────────────────────────────────────────────────────

[[nodiscard]] expected_t<utc_time_point> fix_string_to_utc_time(std::span<const char> s) noexcept {
    using namespace std::chrono;

    // Accepted lengths: 17 (seconds), 21 (millis), 24 (micros).
    // YYYYMMDD-HH:MM:SS = 17; +.sss = 21; +.ssssss = 24.
    const std::size_t len = s.size();
    if (len != 17 && len != 21 && len != 24) {
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
    if (std::cmp_greater(day_i , days_in_month(year, month))) {
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

    // Sub-second.
    std::int64_t ns_sub = 0;
    if (len == 21) {
        if (*p++ != '.') {
            return std::unexpected(error::wire_invalid_field_format);
        }
        const std::int64_t ms_i = parse_digits(p, 3);
        if (ms_i < 0 || ms_i > 999) {
            return std::unexpected(error::wire_invalid_field_format);
        }
        ns_sub = ms_i * 1'000'000LL;
    } else if (len == 24) {
        if (*p++ != '.') {
            return std::unexpected(error::wire_invalid_field_format);
        }
        const std::int64_t us_i = parse_digits(p, 6);
        if (us_i < 0 || us_i > 999'999) {
            return std::unexpected(error::wire_invalid_field_format);
        }
        ns_sub = us_i * 1'000LL;
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
