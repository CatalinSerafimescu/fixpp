// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/fix_time_roundtrip_test.cpp
//
// Seam #9 — FIX UTCTimestamp format↔parse roundtrip (005-session-establishment-fsm T009).
//
// Must FAIL until T010 (include/fixpp/core/fix_time.hpp) and T011
// (src/core/fix_time.cpp) are authored. After T011 this turns green.
//
// Corpus: epoch, leap-second-adjacent timestamps, and sub-second variants at
// ms (FIX 4.x default) and µs precision (FR-012, SC-007/SC-010, I-6, D-3).
// Grammar: YYYYMMDD-HH:MM:SS[.sss[sss]] — never coarser than seconds;
// ms default; µs where the version permits.
//
// Anchors: data-model.md E8; research D-3; contracts/fix_time.hpp;
// include/fixpp/core/fix_time.hpp (T010); src/core/fix_time.cpp (T011).
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string_view>

#include <fixpp/core/fix_time.hpp>

#include <gtest/gtest.h>

namespace fixpp::core::test {
namespace {

using namespace std::chrono_literals;
using namespace std::chrono;

// ── Helper: format a utc_time_point, then round-trip parse it ────────────────

struct RoundTripResult {
    bool        format_ok   = false;
    bool        parse_ok    = false;
    bool        equal       = false;
    std::string formatted;
    utc_time_point parsed_tp;
};

RoundTripResult roundtrip(utc_time_point tp, fix_time_precision prec) {
    RoundTripResult r;
    std::array<char, 32> buf{};
    auto fmtres = utc_time_to_fix_string(tp, prec, std::span<char>{buf});
    if (!fmtres) { return r; }
    r.format_ok  = true;
    r.formatted  = std::string(fmtres->data(), fmtres->size());

    auto parseres = fix_string_to_utc_time(
        std::span<const char>(fmtres->data(), fmtres->size()));
    if (!parseres) { return r; }
    r.parse_ok   = true;
    r.parsed_tp  = *parseres;

    // Lossless at emitted precision: the parsed value must equal the original
    // truncated to the emitted precision.
    auto trunc = [&](utc_time_point t) -> utc_time_point {
        switch (prec) {
            case fix_time_precision::seconds:
                return time_point_cast<seconds>(t);
            case fix_time_precision::millis:
                return time_point_cast<milliseconds>(t);
            case fix_time_precision::micros:
                return time_point_cast<microseconds>(t);
        }
        return t;
    };
    r.equal = (r.parsed_tp == trunc(tp));
    return r;
}

// ── Epoch ─────────────────────────────────────────────────────────────────────

TEST(FixTimeRoundtrip, EpochSeconds) {
    // Unix epoch: 1970-01-01T00:00:00Z → "19700101-00:00:00"
    utc_time_point ep{};
    auto r = roundtrip(ep, fix_time_precision::seconds);
    EXPECT_TRUE(r.format_ok) << "utc_time_to_fix_string failed at epoch/seconds";
    EXPECT_EQ(r.formatted, "19700101-00:00:00") << "unexpected format at epoch/seconds";
    EXPECT_TRUE(r.parse_ok)  << "fix_string_to_utc_time failed at epoch/seconds";
    EXPECT_TRUE(r.equal)     << "round-trip not lossless at epoch/seconds";
}

TEST(FixTimeRoundtrip, EpochMillis) {
    utc_time_point ep{};
    auto r = roundtrip(ep, fix_time_precision::millis);
    EXPECT_TRUE(r.format_ok) << "utc_time_to_fix_string failed at epoch/millis";
    EXPECT_EQ(r.formatted, "19700101-00:00:00.000") << "unexpected format at epoch/millis";
    EXPECT_TRUE(r.parse_ok)  << "fix_string_to_utc_time failed at epoch/millis";
    EXPECT_TRUE(r.equal)     << "round-trip not lossless at epoch/millis";
}

TEST(FixTimeRoundtrip, EpochMicros) {
    utc_time_point ep{};
    auto r = roundtrip(ep, fix_time_precision::micros);
    EXPECT_TRUE(r.format_ok) << "utc_time_to_fix_string failed at epoch/micros";
    EXPECT_EQ(r.formatted, "19700101-00:00:00.000000") << "unexpected format at epoch/micros";
    EXPECT_TRUE(r.parse_ok)  << "fix_string_to_utc_time failed at epoch/micros";
    EXPECT_TRUE(r.equal)     << "round-trip not lossless at epoch/micros";
}

// ── Known timestamp ──────────────────────────────────────────────────────────

TEST(FixTimeRoundtrip, KnownDateMillis) {
    // 2024-03-15T14:30:45.123 UTC
    // epoch offset: 54 years, computed precisely
    utc_time_point tp{
        system_clock::from_time_t(0) +
        duration_cast<system_clock::duration>(
            hours(24 * (54 * 365 + 13))  // 54 years with 13 leap years (approx)
            + hours(14 * 365 + 75)       // extra days for 2024-03-15
        )
    };
    // Use a fixed known string instead, with parse→format:
    const std::string_view known = "20240315-14:30:45.123";
    auto parsed = fix_string_to_utc_time(
        std::span<const char>(known.data(), known.size()));
    ASSERT_TRUE(parsed.has_value()) << "fix_string_to_utc_time failed on known millis string";

    std::array<char, 32> buf{};
    auto fmtres = utc_time_to_fix_string(*parsed, fix_time_precision::millis,
                                          std::span<char>{buf});
    ASSERT_TRUE(fmtres.has_value()) << "utc_time_to_fix_string failed on re-format";
    EXPECT_EQ(std::string(fmtres->data(), fmtres->size()), std::string(known))
        << "parse→format not identity for known millis timestamp";
}

TEST(FixTimeRoundtrip, KnownDateMicros) {
    const std::string_view known = "20231231-23:59:59.999999";
    auto parsed = fix_string_to_utc_time(
        std::span<const char>(known.data(), known.size()));
    ASSERT_TRUE(parsed.has_value()) << "fix_string_to_utc_time failed on known micros string";

    std::array<char, 32> buf{};
    auto fmtres = utc_time_to_fix_string(*parsed, fix_time_precision::micros,
                                          std::span<char>{buf});
    ASSERT_TRUE(fmtres.has_value()) << "utc_time_to_fix_string failed on re-format";
    EXPECT_EQ(std::string(fmtres->data(), fmtres->size()), std::string(known))
        << "parse→format not identity for known micros timestamp";
}

// ── Leap-second-adjacent ─────────────────────────────────────────────────────

TEST(FixTimeRoundtrip, LeapSecondAdjacentSeconds) {
    // 2016-12-31T23:59:59 UTC — the second just before the 2016 leap second.
    const std::string_view ts = "20161231-23:59:59";
    auto parsed = fix_string_to_utc_time(
        std::span<const char>(ts.data(), ts.size()));
    ASSERT_TRUE(parsed.has_value()) << "parse failed at leap-second-adjacent";

    std::array<char, 32> buf{};
    auto fmtres = utc_time_to_fix_string(*parsed, fix_time_precision::seconds,
                                          std::span<char>{buf});
    ASSERT_TRUE(fmtres.has_value()) << "format failed at leap-second-adjacent";
    EXPECT_EQ(std::string(fmtres->data(), fmtres->size()), std::string(ts));
}

// ── Sub-second precision corpus ──────────────────────────────────────────────

TEST(FixTimeRoundtrip, SubSecondMillisRoundtrip) {
    // Several sub-second ms values
    const std::string_view timestamps[] = {
        "20200101-12:00:00.001",
        "20200101-12:00:00.010",
        "20200101-12:00:00.100",
        "20200101-12:00:00.999",
    };
    for (const auto& ts : timestamps) {
        SCOPED_TRACE(ts);
        auto parsed = fix_string_to_utc_time(
            std::span<const char>(ts.data(), ts.size()));
        ASSERT_TRUE(parsed.has_value());

        std::array<char, 32> buf{};
        auto fmtres = utc_time_to_fix_string(*parsed, fix_time_precision::millis,
                                              std::span<char>{buf});
        ASSERT_TRUE(fmtres.has_value());
        EXPECT_EQ(std::string(fmtres->data(), fmtres->size()), std::string(ts));
    }
}

TEST(FixTimeRoundtrip, SubSecondMicrosRoundtrip) {
    const std::string_view timestamps[] = {
        "20200601-08:30:00.000001",
        "20200601-08:30:00.000100",
        "20200601-08:30:00.123456",
        "20200601-08:30:00.999999",
    };
    for (const auto& ts : timestamps) {
        SCOPED_TRACE(ts);
        auto parsed = fix_string_to_utc_time(
            std::span<const char>(ts.data(), ts.size()));
        ASSERT_TRUE(parsed.has_value());

        std::array<char, 32> buf{};
        auto fmtres = utc_time_to_fix_string(*parsed, fix_time_precision::micros,
                                              std::span<char>{buf});
        ASSERT_TRUE(fmtres.has_value());
        EXPECT_EQ(std::string(fmtres->data(), fmtres->size()), std::string(ts));
    }
}

// ── Precision truncation (millis truncates micros) ───────────────────────────

TEST(FixTimeRoundtrip, MillisTruncatesMicros) {
    // A timestamp with µs resolution formatted at ms precision should drop µs.
    const std::string_view micros_ts = "20210101-00:00:00.123456";
    auto parsed = fix_string_to_utc_time(
        std::span<const char>(micros_ts.data(), micros_ts.size()));
    ASSERT_TRUE(parsed.has_value());

    std::array<char, 32> buf{};
    auto fmtres = utc_time_to_fix_string(*parsed, fix_time_precision::millis,
                                          std::span<char>{buf});
    ASSERT_TRUE(fmtres.has_value());
    EXPECT_EQ(std::string(fmtres->data(), fmtres->size()),
              "20210101-00:00:00.123")
        << "millis format must truncate (not round) µs";
}

// ── Error cases ──────────────────────────────────────────────────────────────

TEST(FixTimeRoundtrip, ParseEmptyStringReturnsError) {
    auto r = fix_string_to_utc_time(std::span<const char>{});
    EXPECT_FALSE(r.has_value());
}

TEST(FixTimeRoundtrip, ParseBadGrammarReturnsError) {
    const std::string_view bad = "NOTADATE";
    auto r = fix_string_to_utc_time(
        std::span<const char>(bad.data(), bad.size()));
    EXPECT_FALSE(r.has_value());
}

TEST(FixTimeRoundtrip, ParseOnlyDateNoDashTimeReturnsError) {
    // Missing the time portion entirely
    const std::string_view bad = "20200101";
    auto r = fix_string_to_utc_time(
        std::span<const char>(bad.data(), bad.size()));
    EXPECT_FALSE(r.has_value());
}

TEST(FixTimeRoundtrip, ParseBadMonthReturnsError) {
    const std::string_view bad = "20201301-00:00:00";
    auto r = fix_string_to_utc_time(
        std::span<const char>(bad.data(), bad.size()));
    EXPECT_FALSE(r.has_value());
}

TEST(FixTimeRoundtrip, ParseBadHourReturnsError) {
    const std::string_view bad = "20200101-25:00:00";
    auto r = fix_string_to_utc_time(
        std::span<const char>(bad.data(), bad.size()));
    EXPECT_FALSE(r.has_value());
}

// ── Negative-grammar corpus: parse-error branch coverage (Phase 8 /simplify) ─

TEST(FixTimeRoundtrip, ParseNonDigitYearReturnsError) {
    const std::string_view bad = "AAAA0101-00:00:00";
    auto r = fix_string_to_utc_time(
        std::span<const char>(bad.data(), bad.size()));
    EXPECT_FALSE(r.has_value());
}

TEST(FixTimeRoundtrip, ParseMonthZeroReturnsError) {
    const std::string_view bad = "20200001-00:00:00";
    auto r = fix_string_to_utc_time(
        std::span<const char>(bad.data(), bad.size()));
    EXPECT_FALSE(r.has_value());
}

TEST(FixTimeRoundtrip, ParseDayZeroReturnsError) {
    const std::string_view bad = "20200100-00:00:00";
    auto r = fix_string_to_utc_time(
        std::span<const char>(bad.data(), bad.size()));
    EXPECT_FALSE(r.has_value());
}

// Feb 30 in a non-leap year → days_in_month rejection branch.
TEST(FixTimeRoundtrip, ParseDayBeyondMonthReturnsError) {
    const std::string_view bad = "20210230-00:00:00";
    auto r = fix_string_to_utc_time(
        std::span<const char>(bad.data(), bad.size()));
    EXPECT_FALSE(r.has_value());
}

TEST(FixTimeRoundtrip, ParseMissingDateTimeDashReturnsError) {
    const std::string_view bad = "20200101X00:00:00";
    auto r = fix_string_to_utc_time(
        std::span<const char>(bad.data(), bad.size()));
    EXPECT_FALSE(r.has_value());
}

TEST(FixTimeRoundtrip, ParseBadMinuteReturnsError) {
    const std::string_view bad = "20200101-00:60:00";
    auto r = fix_string_to_utc_time(
        std::span<const char>(bad.data(), bad.size()));
    EXPECT_FALSE(r.has_value());
}

// 61 > leap-second-permitted 60.
TEST(FixTimeRoundtrip, ParseBadSecondReturnsError) {
    const std::string_view bad = "20200101-00:00:61";
    auto r = fix_string_to_utc_time(
        std::span<const char>(bad.data(), bad.size()));
    EXPECT_FALSE(r.has_value());
}

TEST(FixTimeRoundtrip, ParseMissingHourColonReturnsError) {
    const std::string_view bad = "20200101-00X00:00";
    auto r = fix_string_to_utc_time(
        std::span<const char>(bad.data(), bad.size()));
    EXPECT_FALSE(r.has_value());
}

TEST(FixTimeRoundtrip, ParseMissingMinuteColonReturnsError) {
    const std::string_view bad = "20200101-00:00X00";
    auto r = fix_string_to_utc_time(
        std::span<const char>(bad.data(), bad.size()));
    EXPECT_FALSE(r.has_value());
}

// 21-char input with non-'.' at the ms separator slot.
TEST(FixTimeRoundtrip, ParseMissingMillisDotReturnsError) {
    const std::string_view bad = "20200101-00:00:00X123";
    auto r = fix_string_to_utc_time(
        std::span<const char>(bad.data(), bad.size()));
    EXPECT_FALSE(r.has_value());
}

// 24-char input with non-'.' at the µs separator slot.
TEST(FixTimeRoundtrip, ParseMissingMicrosDotReturnsError) {
    const std::string_view bad = "20200101-00:00:00X123456";
    auto r = fix_string_to_utc_time(
        std::span<const char>(bad.data(), bad.size()));
    EXPECT_FALSE(r.has_value());
}

TEST(FixTimeRoundtrip, ParseNonDigitMillisReturnsError) {
    const std::string_view bad = "20200101-00:00:00.A23";
    auto r = fix_string_to_utc_time(
        std::span<const char>(bad.data(), bad.size()));
    EXPECT_FALSE(r.has_value());
}

TEST(FixTimeRoundtrip, ParseNonDigitMicrosReturnsError) {
    const std::string_view bad = "20200101-00:00:00.A23456";
    auto r = fix_string_to_utc_time(
        std::span<const char>(bad.data(), bad.size()));
    EXPECT_FALSE(r.has_value());
}

// ── Format-side branches (Phase 8 /simplify finding 4 — branch coverage) ────

TEST(FixTimeRoundtrip, FormatBufferTooSmallSecondsReturnsError) {
    // 17-char minimum for seconds precision; pass 16.
    utc_time_point tp{};
    std::array<char, 16> buf{};
    auto r = utc_time_to_fix_string(tp, fix_time_precision::seconds, std::span<char>{buf});
    ASSERT_FALSE(r.has_value());
}

TEST(FixTimeRoundtrip, FormatBufferTooSmallMillisReturnsError) {
    // 21-char minimum for millis precision; pass 20.
    utc_time_point tp{};
    std::array<char, 20> buf{};
    auto r = utc_time_to_fix_string(tp, fix_time_precision::millis, std::span<char>{buf});
    ASSERT_FALSE(r.has_value());
}

TEST(FixTimeRoundtrip, FormatBufferTooSmallMicrosReturnsError) {
    // 24-char minimum for micros precision; pass 23.
    utc_time_point tp{};
    std::array<char, 23> buf{};
    auto r = utc_time_to_fix_string(tp, fix_time_precision::micros, std::span<char>{buf});
    ASSERT_FALSE(r.has_value());
}

// Negative-epoch handling: before-1970 timestamp should still format correctly
// (fix_time.cpp:133 `if (time_of_day < 0)` branch — the seconds-modulo-negative
// adjustment for dates before Unix epoch).
TEST(FixTimeRoundtrip, FormatPreEpochTimestampRoundtripsCorrectly) {
    // 1969-12-31T23:59:59Z — one second before epoch.
    utc_time_point tp{std::chrono::seconds{-1}};
    auto r = roundtrip(tp, fix_time_precision::seconds);
    EXPECT_TRUE(r.format_ok);
    EXPECT_TRUE(r.parse_ok);
    EXPECT_TRUE(r.equal) << "pre-epoch must roundtrip; got " << r.formatted;
}

TEST(FixTimeRoundtrip, FormatDeepPreEpochTimestampRoundtripsCorrectly) {
    // 1969-01-15T12:34:56Z — well before epoch, exercises the day-boundary
    // adjustment fully.
    utc_time_point tp{std::chrono::seconds{-30240244}};  // computed: about Jan 15 1969 12:34:56
    auto r = roundtrip(tp, fix_time_precision::seconds);
    EXPECT_TRUE(r.format_ok);
    EXPECT_TRUE(r.parse_ok);
    EXPECT_TRUE(r.equal);
}

TEST(FixTimeRoundtrip, FormatOutputBufferFitsIn32Chars) {
    // Maximum length: "YYYYMMDD-HH:MM:SS.ssssss" = 17+7 = 24 chars; well within 32.
    utc_time_point tp{};
    std::array<char, 32> buf{};
    auto r = utc_time_to_fix_string(tp, fix_time_precision::micros,
                                     std::span<char>{buf});
    ASSERT_TRUE(r.has_value());
    EXPECT_LE(r->size(), 32u);
}

}  // namespace
}  // namespace fixpp::core::test
