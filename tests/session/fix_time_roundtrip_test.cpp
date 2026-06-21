// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/fix_time_roundtrip_test.cpp
//
// Seam #9 — FIX UTCTimestamp format↔parse roundtrip (005-session-establishment-fsm T009).
//
// Corpus: epoch, leap-second-adjacent timestamps, and sub-second variants at
// ms (FIX 4.x default) and µs precision (FR-012, SC-007/SC-010, I-6, D-3).
// Grammar: YYYYMMDD-HH:MM:SS[.sss[sss]] — never coarser than seconds;
// ms default; µs where the version permits.
//
// Anchors: data-model.md E8; research D-3; contracts/fix_time.hpp;
// include/fixpp/core/fix_time.hpp (T010); src/core/fix_time.cpp (T011).
#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fixpp/core/fix_time.hpp>
#include <string_view>

namespace fixpp::core::test {
namespace {

using namespace std::chrono_literals;
using namespace std::chrono;

// ── Helper: format a utc_time_point, then round-trip parse it ────────────────

struct RoundTripResult {
    bool format_ok = false;
    bool parse_ok = false;
    bool equal = false;
    std::string formatted;
    utc_time_point parsed_tp;
};

RoundTripResult roundtrip(utc_time_point tp, fix_time_precision prec) {
    RoundTripResult r;
    std::array<char, 32> buf{};
    auto fmtres = utc_time_to_fix_string(tp, prec, std::span<char>{buf});
    if (!fmtres) {
        return r;
    }
    r.format_ok = true;
    r.formatted = std::string(fmtres->data(), fmtres->size());

    auto parseres = fix_string_to_utc_time(std::span<const char>(fmtres->data(), fmtres->size()));
    if (!parseres) {
        return r;
    }
    r.parse_ok = true;
    r.parsed_tp = *parseres;

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
            case fix_time_precision::nanos:
                return time_point_cast<utc_time_point::duration>(t);
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
    EXPECT_TRUE(r.parse_ok) << "fix_string_to_utc_time failed at epoch/seconds";
    EXPECT_TRUE(r.equal) << "round-trip not lossless at epoch/seconds";
}

TEST(FixTimeRoundtrip, EpochMillis) {
    utc_time_point ep{};
    auto r = roundtrip(ep, fix_time_precision::millis);
    EXPECT_TRUE(r.format_ok) << "utc_time_to_fix_string failed at epoch/millis";
    EXPECT_EQ(r.formatted, "19700101-00:00:00.000") << "unexpected format at epoch/millis";
    EXPECT_TRUE(r.parse_ok) << "fix_string_to_utc_time failed at epoch/millis";
    EXPECT_TRUE(r.equal) << "round-trip not lossless at epoch/millis";
}

TEST(FixTimeRoundtrip, EpochMicros) {
    utc_time_point ep{};
    auto r = roundtrip(ep, fix_time_precision::micros);
    EXPECT_TRUE(r.format_ok) << "utc_time_to_fix_string failed at epoch/micros";
    EXPECT_EQ(r.formatted, "19700101-00:00:00.000000") << "unexpected format at epoch/micros";
    EXPECT_TRUE(r.parse_ok) << "fix_string_to_utc_time failed at epoch/micros";
    EXPECT_TRUE(r.equal) << "round-trip not lossless at epoch/micros";
}

// ── Known timestamp ──────────────────────────────────────────────────────────

TEST(FixTimeRoundtrip, KnownDateMillis) {
    // 2024-03-15T14:30:45.123 UTC
    // epoch offset: 54 years, computed precisely
    utc_time_point tp{system_clock::from_time_t(0) +
                      duration_cast<system_clock::duration>(
                          hours(24 * (54 * 365 + 13))  // 54 years with 13 leap years (approx)
                          + hours(14 * 365 + 75)       // extra days for 2024-03-15
                          )};
    // Use a fixed known string instead, with parse→format:
    const std::string_view known = "20240315-14:30:45.123";
    auto parsed = fix_string_to_utc_time(std::span<const char>(known.data(), known.size()));
    ASSERT_TRUE(parsed.has_value()) << "fix_string_to_utc_time failed on known millis string";

    std::array<char, 32> buf{};
    auto fmtres = utc_time_to_fix_string(*parsed, fix_time_precision::millis, std::span<char>{buf});
    ASSERT_TRUE(fmtres.has_value()) << "utc_time_to_fix_string failed on re-format";
    EXPECT_EQ(std::string(fmtres->data(), fmtres->size()), std::string(known))
        << "parse→format not identity for known millis timestamp";
}

TEST(FixTimeRoundtrip, KnownDateMicros) {
    const std::string_view known = "20231231-23:59:59.999999";
    auto parsed = fix_string_to_utc_time(std::span<const char>(known.data(), known.size()));
    ASSERT_TRUE(parsed.has_value()) << "fix_string_to_utc_time failed on known micros string";

    std::array<char, 32> buf{};
    auto fmtres = utc_time_to_fix_string(*parsed, fix_time_precision::micros, std::span<char>{buf});
    ASSERT_TRUE(fmtres.has_value()) << "utc_time_to_fix_string failed on re-format";
    EXPECT_EQ(std::string(fmtres->data(), fmtres->size()), std::string(known))
        << "parse→format not identity for known micros timestamp";
}

// ── Leap-second-adjacent ─────────────────────────────────────────────────────

TEST(FixTimeRoundtrip, LeapSecondAdjacentSeconds) {
    // 2016-12-31T23:59:59 UTC — the second just before the 2016 leap second.
    const std::string_view ts = "20161231-23:59:59";
    auto parsed = fix_string_to_utc_time(std::span<const char>(ts.data(), ts.size()));
    ASSERT_TRUE(parsed.has_value()) << "parse failed at leap-second-adjacent";

    std::array<char, 32> buf{};
    auto fmtres =
        utc_time_to_fix_string(*parsed, fix_time_precision::seconds, std::span<char>{buf});
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
        auto parsed = fix_string_to_utc_time(std::span<const char>(ts.data(), ts.size()));
        ASSERT_TRUE(parsed.has_value());

        std::array<char, 32> buf{};
        auto fmtres =
            utc_time_to_fix_string(*parsed, fix_time_precision::millis, std::span<char>{buf});
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
        auto parsed = fix_string_to_utc_time(std::span<const char>(ts.data(), ts.size()));
        ASSERT_TRUE(parsed.has_value());

        std::array<char, 32> buf{};
        auto fmtres =
            utc_time_to_fix_string(*parsed, fix_time_precision::micros, std::span<char>{buf});
        ASSERT_TRUE(fmtres.has_value());
        EXPECT_EQ(std::string(fmtres->data(), fmtres->size()), std::string(ts));
    }
}

// ── Precision truncation (millis truncates micros) ───────────────────────────

TEST(FixTimeRoundtrip, MillisTruncatesMicros) {
    // A timestamp with µs resolution formatted at ms precision should drop µs.
    const std::string_view micros_ts = "20210101-00:00:00.123456";
    auto parsed = fix_string_to_utc_time(std::span<const char>(micros_ts.data(), micros_ts.size()));
    ASSERT_TRUE(parsed.has_value());

    std::array<char, 32> buf{};
    auto fmtres = utc_time_to_fix_string(*parsed, fix_time_precision::millis, std::span<char>{buf});
    ASSERT_TRUE(fmtres.has_value());
    EXPECT_EQ(std::string(fmtres->data(), fmtres->size()), "20210101-00:00:00.123")
        << "millis format must truncate (not round) µs";
}

// ── Error cases ──────────────────────────────────────────────────────────────

TEST(FixTimeRoundtrip, ParseEmptyStringReturnsError) {
    auto r = fix_string_to_utc_time(std::span<const char>{});
    EXPECT_FALSE(r.has_value());
}

TEST(FixTimeRoundtrip, ParseBadGrammarReturnsError) {
    const std::string_view bad = "NOTADATE";
    auto r = fix_string_to_utc_time(std::span<const char>(bad.data(), bad.size()));
    EXPECT_FALSE(r.has_value());
}

TEST(FixTimeRoundtrip, ParseOnlyDateNoDashTimeReturnsError) {
    // Missing the time portion entirely
    const std::string_view bad = "20200101";
    auto r = fix_string_to_utc_time(std::span<const char>(bad.data(), bad.size()));
    EXPECT_FALSE(r.has_value());
}

TEST(FixTimeRoundtrip, ParseBadMonthReturnsError) {
    const std::string_view bad = "20201301-00:00:00";
    auto r = fix_string_to_utc_time(std::span<const char>(bad.data(), bad.size()));
    EXPECT_FALSE(r.has_value());
}

TEST(FixTimeRoundtrip, ParseBadHourReturnsError) {
    const std::string_view bad = "20200101-25:00:00";
    auto r = fix_string_to_utc_time(std::span<const char>(bad.data(), bad.size()));
    EXPECT_FALSE(r.has_value());
}

// ── Negative-grammar corpus: parse-error branch coverage (Phase 8 /simplify) ─

TEST(FixTimeRoundtrip, ParseNonDigitYearReturnsError) {
    const std::string_view bad = "AAAA0101-00:00:00";
    auto r = fix_string_to_utc_time(std::span<const char>(bad.data(), bad.size()));
    EXPECT_FALSE(r.has_value());
}

TEST(FixTimeRoundtrip, ParseMonthZeroReturnsError) {
    const std::string_view bad = "20200001-00:00:00";
    auto r = fix_string_to_utc_time(std::span<const char>(bad.data(), bad.size()));
    EXPECT_FALSE(r.has_value());
}

TEST(FixTimeRoundtrip, ParseDayZeroReturnsError) {
    const std::string_view bad = "20200100-00:00:00";
    auto r = fix_string_to_utc_time(std::span<const char>(bad.data(), bad.size()));
    EXPECT_FALSE(r.has_value());
}

// Feb 30 in a non-leap year → days_in_month rejection branch.
TEST(FixTimeRoundtrip, ParseDayBeyondMonthReturnsError) {
    const std::string_view bad = "20210230-00:00:00";
    auto r = fix_string_to_utc_time(std::span<const char>(bad.data(), bad.size()));
    EXPECT_FALSE(r.has_value());
}

TEST(FixTimeRoundtrip, ParseMissingDateTimeDashReturnsError) {
    const std::string_view bad = "20200101X00:00:00";
    auto r = fix_string_to_utc_time(std::span<const char>(bad.data(), bad.size()));
    EXPECT_FALSE(r.has_value());
}

TEST(FixTimeRoundtrip, ParseBadMinuteReturnsError) {
    const std::string_view bad = "20200101-00:60:00";
    auto r = fix_string_to_utc_time(std::span<const char>(bad.data(), bad.size()));
    EXPECT_FALSE(r.has_value());
}

// 61 > leap-second-permitted 60.
TEST(FixTimeRoundtrip, ParseBadSecondReturnsError) {
    const std::string_view bad = "20200101-00:00:61";
    auto r = fix_string_to_utc_time(std::span<const char>(bad.data(), bad.size()));
    EXPECT_FALSE(r.has_value());
}

TEST(FixTimeRoundtrip, ParseMissingHourColonReturnsError) {
    const std::string_view bad = "20200101-00X00:00";
    auto r = fix_string_to_utc_time(std::span<const char>(bad.data(), bad.size()));
    EXPECT_FALSE(r.has_value());
}

TEST(FixTimeRoundtrip, ParseMissingMinuteColonReturnsError) {
    const std::string_view bad = "20200101-00:00X00";
    auto r = fix_string_to_utc_time(std::span<const char>(bad.data(), bad.size()));
    EXPECT_FALSE(r.has_value());
}

// 21-char input with non-'.' at the ms separator slot.
TEST(FixTimeRoundtrip, ParseMissingMillisDotReturnsError) {
    const std::string_view bad = "20200101-00:00:00X123";
    auto r = fix_string_to_utc_time(std::span<const char>(bad.data(), bad.size()));
    EXPECT_FALSE(r.has_value());
}

// 24-char input with non-'.' at the µs separator slot.
TEST(FixTimeRoundtrip, ParseMissingMicrosDotReturnsError) {
    const std::string_view bad = "20200101-00:00:00X123456";
    auto r = fix_string_to_utc_time(std::span<const char>(bad.data(), bad.size()));
    EXPECT_FALSE(r.has_value());
}

TEST(FixTimeRoundtrip, ParseNonDigitMillisReturnsError) {
    const std::string_view bad = "20200101-00:00:00.A23";
    auto r = fix_string_to_utc_time(std::span<const char>(bad.data(), bad.size()));
    EXPECT_FALSE(r.has_value());
}

TEST(FixTimeRoundtrip, ParseNonDigitMicrosReturnsError) {
    const std::string_view bad = "20200101-00:00:00.A23456";
    auto r = fix_string_to_utc_time(std::span<const char>(bad.data(), bad.size()));
    EXPECT_FALSE(r.has_value());
}

// ── Format-side branches (buffer-too-small + negative-epoch handling) ──────

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

// ── Leap-year branches (is_leap + days_in_month, all three sub-paths) ─────
//
// is_leap() has 3 sub-branches: y%4==0 && y%100!=0, y%400==0, fall-through.
// days_in_month(2, year) calls is_leap; only the non-leap path is exercised
// by the existing corpus. These three tests cover all three sub-branches +
// the days_in_month leap-year True arm.

// 2024 = ordinary leap year (y%4==0 && y%100!=0). Feb 29 2024 must parse OK.
TEST(FixTimeRoundtrip, ParseFeb29InOrdinaryLeapYearSucceeds) {
    const std::string_view ok = "20240229-00:00:00";
    auto r = fix_string_to_utc_time(std::span<const char>(ok.data(), ok.size()));
    EXPECT_TRUE(r.has_value()) << "Feb 29 2024 is valid (leap year)";
}

// 2000 = centennial leap year (y%400==0). Feb 29 2000 must parse OK.
TEST(FixTimeRoundtrip, ParseFeb29InCentennialLeapYearSucceeds) {
    const std::string_view ok = "20000229-00:00:00";
    auto r = fix_string_to_utc_time(std::span<const char>(ok.data(), ok.size()));
    EXPECT_TRUE(r.has_value()) << "Feb 29 2000 is valid (centennial leap year)";
}

// 1900 = centennial non-leap (y%100==0 && y%400!=0). Feb 29 1900 must fail.
TEST(FixTimeRoundtrip, ParseFeb29InCentennialNonLeapYearReturnsError) {
    const std::string_view bad = "19000229-00:00:00";
    auto r = fix_string_to_utc_time(std::span<const char>(bad.data(), bad.size()));
    EXPECT_FALSE(r.has_value()) << "Feb 29 1900 invalid (centennial non-leap)";
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
    auto r = utc_time_to_fix_string(tp, fix_time_precision::micros, std::span<char>{buf});
    ASSERT_TRUE(r.has_value());
    EXPECT_LE(r->size(), 32u);
}

// ── 026-nanosecond-sendingtime T004 witnesses ────────────────────────────────

// T004(1): format(t, nanos) returns span of length == 27, NOT 17.
// Run this target under UBSan: the min_size[3] OOB and the missing-arm 17-char
// fall-through BOTH fail RED before T007 lands. [data-model E2; contract C2;
// Gate A RC#5; SC-001]
TEST(FixTimeNanos, Format_Is27CharsUnderUBSan) {
    // A time point with a known non-zero nanosecond remainder so the 9 fraction
    // digits are non-trivially verifiable.
    // 2024-01-01T00:00:00.123456789 UTC (epoch + offset + ns component)
    using namespace std::chrono;
    const auto base = utc_time_point{seconds{1704067200}};  // 2024-01-01T00:00:00Z
    const auto tp = time_point_cast<utc_time_point::duration>(base + nanoseconds{123456789});

    std::array<char, 32> buf{};
    auto r = utc_time_to_fix_string(tp, fix_time_precision::nanos, std::span<char>{buf});

    // RED (pre-impl): r has a value BUT length == 17 (nanos falls through to seconds arm)
    // AND UBSan fires on min_size[3] OOB read.
    // GREEN (post-impl T007): length == 27.
    ASSERT_TRUE(r.has_value()) << "utc_time_to_fix_string must succeed with a 32-byte buffer";
    EXPECT_EQ(r->size(), 27u)
        << "nanos format must be 27 chars (YYYYMMDD-HH:MM:SS.sssssssss), got " << r->size()
        << "; [data-model E2 / contract C2 / Gate A RC#5]";

    // The 9 fraction digits (positions 18..26) must equal the ns remainder (123456789).
    // (Only meaningful once length == 27; wrapped in ASSERT so we don't OOB-read on failure.)
    ASSERT_EQ(r->size(), 27u);
    const std::string formatted(r->data(), r->size());
    const std::string_view frac = std::string_view(formatted).substr(18, 9);  // after "YYYYMMDD-HH:MM:SS."
    EXPECT_EQ(frac, "123456789")
        << "nanos fraction digits must equal ns_rem (123456789); got " << frac
        << "; [data-model E2 / contract C2]";
}

// T004(2): parse(format(t, nanos)) == time_point_cast<nanoseconds>(t).
// NOT bare == t (New-3 flake on coarser clocks). [data-model E3 / I-NST-2 / FR-005]
TEST(FixTimeNanos, RoundTrip_Lossless) {
    using namespace std::chrono;
    const auto base = utc_time_point{seconds{1704067200}};
    const auto tp = time_point_cast<utc_time_point::duration>(base + nanoseconds{987654321});

    auto r = roundtrip(tp, fix_time_precision::nanos);
    EXPECT_TRUE(r.format_ok) << "utc_time_to_fix_string failed for nanos";
    // RED pre-impl: length 17, parse returns millis-truncated value != nanos cast.
    EXPECT_TRUE(r.parse_ok)  << "fix_string_to_utc_time failed for nanos-formatted string: "
                              << r.formatted;
    // The oracle: parsed == time_point_cast to the native duration of utc_time_point (I-NST-2 / New-3).
    const auto expected = time_point_cast<utc_time_point::duration>(tp);
    EXPECT_EQ(r.parsed_tp, expected)
        << "nanos round-trip not lossless; parsed=" << r.parsed_tp.time_since_epoch().count()
        << " expected=" << expected.time_since_epoch().count()
        << "; [I-NST-2 / FR-005 / New-3]";
}

// T004(3): millis and micros format is byte-identical to pre-feature output.
// Seconds path unchanged. This witness must ALREADY PASS (unchanged paths).
// [FR-003 / SC-002 / I-NST-1]
TEST(FixTimeNanos, MillisMicros_ByteIdentical_NoRegression) {
    using namespace std::chrono;
    const auto tp = time_point_cast<utc_time_point::duration>(utc_time_point{seconds{1704067200}} + nanoseconds{123456789});

    std::array<char, 32> buf_ms{};
    std::array<char, 32> buf_us{};
    std::array<char, 32> buf_s{};

    auto r_ms = utc_time_to_fix_string(tp, fix_time_precision::millis, std::span<char>{buf_ms});
    auto r_us = utc_time_to_fix_string(tp, fix_time_precision::micros, std::span<char>{buf_us});
    auto r_s  = utc_time_to_fix_string(tp, fix_time_precision::seconds, std::span<char>{buf_s});

    ASSERT_TRUE(r_ms.has_value());
    ASSERT_TRUE(r_us.has_value());
    ASSERT_TRUE(r_s.has_value());

    // millis: 21 chars, fraction == 123 (truncated from 123456789 ns)
    EXPECT_EQ(r_ms->size(), 21u) << "millis must be 21 chars; [FR-003/SC-002]";
    EXPECT_EQ(std::string(r_ms->data(), r_ms->size()), "20240101-00:00:00.123")
        << "millis output must be byte-identical to pre-feature";

    // micros: 24 chars, fraction == 123456 (truncated from 123456789 ns)
    EXPECT_EQ(r_us->size(), 24u) << "micros must be 24 chars; [FR-003/SC-002]";
    EXPECT_EQ(std::string(r_us->data(), r_us->size()), "20240101-00:00:00.123456")
        << "micros output must be byte-identical to pre-feature";

    // seconds: 17 chars, no fraction
    EXPECT_EQ(r_s->size(), 17u) << "seconds must be 17 chars; [FR-003/SC-002]";
    EXPECT_EQ(std::string(r_s->data(), r_s->size()), "20240101-00:00:00")
        << "seconds output must be byte-identical to pre-feature";
}

// ── 026-nanosecond-sendingtime T011 witnesses (US2) ─────────────────────────
//
// T011(1): FixTimeParse_LenientWidths_1to9
// Any fraction width N=1..9 → ns-scaled instant (10^(9-N) factor).
// Bare 17-char (no dot) form still parses.
// [FR-004/SC-003; data-model E3; contract C3; I-NST-3]
//
// RED pre-impl: parser rejects any len != 17/21/24 → these return !has_value().
TEST(FixTimeParse, LenientWidths_1to9) {
    using namespace std::chrono;

    // Base time point: 20240101-00:00:00 (2024-01-01T00:00:00Z)
    // epoch seconds: 1704067200
    const std::int64_t base_sec = 1704067200LL;
    const auto base_tp = utc_time_point{std::chrono::duration_cast<utc_time_point::duration>(nanoseconds{base_sec * 1'000'000'000LL})};

    // Bare 17-char: no dot, no fraction — parses to base_tp (ns scale = seconds).
    {
        const std::string_view bare17 = "20240101-00:00:00";
        auto r = fix_string_to_utc_time(std::span<const char>(bare17.data(), bare17.size()));
        ASSERT_TRUE(r.has_value()) << "bare 17-char must parse; [FR-004/SC-003]";
        EXPECT_EQ(*r, base_tp) << "bare 17-char must parse to base instant";
    }

    // For each N=1..9, build "20240101-00:00:00.D...D" where D...D has N digits.
    // The fraction value is chosen as all-1s (e.g. "1", "12", ..., "123456789").
    // Expected ns offset = frac_val * 10^(9-N).
    // Scale factors for N=1..9:
    constexpr std::int64_t scale[10] = {
        0,           // unused (N=0)
        100'000'000, // N=1: *1e8
        10'000'000,  // N=2: *1e7
        1'000'000,   // N=3: *1e6
        100'000,     // N=4: *1e5
        10'000,      // N=5: *1e4
        1'000,       // N=6: *1e3
        100,         // N=7: *1e2
        10,          // N=8: *1e1
        1,           // N=9: *1e0
    };

    // Test each width with a known fraction.
    // frac string: "1", "12", "123", "1234", "12345", "123456", "1234567", "12345678", "123456789"
    const char* fracs[] = {
        "1",         // N=1 → +100_000_000 ns
        "12",        // N=2 → +120_000_000 ns
        "123",       // N=3 → +123_000_000 ns
        "1234",      // N=4 → +123_400_000 ns
        "12345",     // N=5 → +123_450_000 ns
        "123456",    // N=6 → +123_456_000 ns
        "1234567",   // N=7 → +123_456_700 ns
        "12345678",  // N=8 → +123_456_780 ns
        "123456789", // N=9 → +123_456_789 ns
    };

    for (int n = 1; n <= 9; ++n) {
        const std::string ts = std::string{"20240101-00:00:00."} + fracs[n - 1];
        SCOPED_TRACE("width=" + std::to_string(n) + " ts=" + ts);

        auto r = fix_string_to_utc_time(std::span<const char>(ts.data(), ts.size()));
        ASSERT_TRUE(r.has_value())
            << "width " << n << " must parse; [FR-004/SC-003]";

        // Expected: base + (frac_val * scale[n]) nanoseconds.
        // frac_val is the integer value of fracs[n-1].
        std::int64_t frac_val = 0;
        for (int i = 0; i < n; ++i) frac_val = frac_val * 10 + (fracs[n-1][i] - '0');
        const auto expected_ns = nanoseconds{base_sec * 1'000'000'000LL + frac_val * scale[n]};
        // utc_time_point::duration is nanoseconds (pinned for cross-stdlib portability);
        // no duration_cast needed — ns is the native precision of utc_time_point.
        const auto expected_tp = utc_time_point{duration_cast<utc_time_point::duration>(expected_ns)};
        EXPECT_EQ(*r, expected_tp)
            << "width " << n << " ns offset mismatch; [data-model E3 / I-NST-3]";
    }

    // Spot-check the documented oracle: ".1234" → +123_400_000 ns (data-model E3).
    {
        const std::string_view ts4 = "20240101-00:00:00.1234";
        auto r4 = fix_string_to_utc_time(std::span<const char>(ts4.data(), ts4.size()));
        ASSERT_TRUE(r4.has_value()) << ".1234 (4-digit) must parse";
        const auto expected4 = utc_time_point{std::chrono::duration_cast<utc_time_point::duration>(nanoseconds{base_sec * 1'000'000'000LL + 123'400'000LL})};
        EXPECT_EQ(*r4, expected4)
            << "oracle: parse('.1234') must yield +123_400_000 ns; [data-model E3 / contract C3]";
    }
}

// T011(2): FixTimeParse_RejectMalformed
// "…SS." (empty fraction, length-18), "…SS.12a" (non-digit), "…SS.1234567890" (10 digits)
// ALL → wire_invalid_field_format.
// The 10-digit case MUST be rejected by the WIDTH GATE (total length > 27 / N > 9),
// NOT by arithmetic (a 10-digit value 9_999_999_999 fits in int64).
// [FR-008/SC-005; Gate A RC#4; data-model E3; contract C3]
//
// RED pre-impl: currently the strict len-check rejects these for wrong reasons
// (length 18 returns error; "…SS.12a" len 21 triggers millis parse → non-digit fails;
// "…SS.1234567890" len 28 is rejected by len gate). The witness certifies that the
// POST-impl lenient parser still rejects them for the CORRECT reasons.
TEST(FixTimeParse, RejectMalformed) {
    // Case 1: empty fraction "…SS." — length 18, dot present, zero fraction digits.
    // Reject via the "at least 1 fraction digit" rule (length 18 = 17+dot+0 digits).
    {
        const std::string_view empty_frac = "20240101-00:00:00.";
        auto r = fix_string_to_utc_time(
            std::span<const char>(empty_frac.data(), empty_frac.size()));
        EXPECT_FALSE(r.has_value())
            << "empty fraction '…SS.' (length 18) must reject; [contract C3 / FR-008]";
    }

    // Case 2: non-digit fraction "…SS.12a" — length 21 (same as millis), but 'a' at index 20.
    // The lenient parser must scan each fraction char; a non-ASCII-digit char rejects.
    {
        const std::string_view nondigit = "20240101-00:00:00.12a";
        auto r = fix_string_to_utc_time(
            std::span<const char>(nondigit.data(), nondigit.size()));
        EXPECT_FALSE(r.has_value())
            << "non-digit fraction '…SS.12a' must reject; [contract C3 / FR-008]";
    }

    // Case 3: 10-digit fraction "…SS.1234567890" — length 28 = 17 + dot + 10 digits.
    // MUST be rejected by the WIDTH GATE (total length > 27) evaluated BEFORE digit parse.
    // A 10-digit value 1234567890 fits in int64 — the gate must NOT rely on overflow.
    // We certify the width gate by also feeding the maximum int64-safe value 9_999_999_999:
    // if it were accepted (arithmetic-based gate), it would fit; the width gate must catch it.
    {
        const std::string_view ten_digits = "20240101-00:00:00.1234567890";
        auto r = fix_string_to_utc_time(
            std::span<const char>(ten_digits.data(), ten_digits.size()));
        EXPECT_FALSE(r.has_value())
            << "10-digit fraction (length 28, width > 9) must reject via WIDTH GATE; "
               "[Gate A RC#4 / contract C3 / FR-008]";

        // Also test 9_999_999_999 — maximum safe-int64 10-digit value.
        // Rejected by width gate (len=28>27), NOT by arithmetic.
        const std::string_view max10 = "20240101-00:00:00.9999999999";
        auto r2 = fix_string_to_utc_time(
            std::span<const char>(max10.data(), max10.size()));
        EXPECT_FALSE(r2.has_value())
            << "max 10-digit value 9_999_999_999 (length 28) must reject via WIDTH GATE; "
               "[Gate A RC#4 / contract C3]";

        // 11 digits (length 29) also rejected by width gate.
        const std::string_view eleven = "20240101-00:00:00.12345678901";
        auto r3 = fix_string_to_utc_time(
            std::span<const char>(eleven.data(), eleven.size()));
        EXPECT_FALSE(r3.has_value())
            << "11-digit fraction (length 29) must reject via WIDTH GATE; [contract C3]";
    }
}

// T011(3): FixTimeParse_Nanos27_RoundTrip
// The 27-char nanos form parses to time_point_cast<nanoseconds>(t).
// [data-model E3 / I-NST-2 / New-3; contract C3]
//
// RED pre-impl: parser rejects length 27 → !has_value().
// This test overlaps with FixTimeNanos.RoundTrip_Lossless (T004(2)) and both must
// go GREEN after T014. Having both is intentional — T011(3) is a direct parse-only
// assertion; T004(2) tests the format→parse round-trip.
TEST(FixTimeParse, Nanos27_RoundTrip) {
    using namespace std::chrono;

    // Known nanos timestamp: "20240101-00:00:00.123456789"
    const std::string_view ts27 = "20240101-00:00:00.123456789";
    ASSERT_EQ(ts27.size(), 27u) << "sanity: ts27 must be 27 chars";

    auto r = fix_string_to_utc_time(std::span<const char>(ts27.data(), ts27.size()));
    ASSERT_TRUE(r.has_value())
        << "27-char nanos form must parse; [data-model E3 / contract C3]";

    // Expected: 2024-01-01T00:00:00.123456789Z
    const std::int64_t base_sec = 1704067200LL;
    const auto expected_ns = nanoseconds{base_sec * 1'000'000'000LL + 123'456'789LL};
    // utc_time_point::duration is nanoseconds; no system_clock truncation.
    const auto expected_tp = utc_time_point{duration_cast<utc_time_point::duration>(expected_ns)};
    EXPECT_EQ(*r, expected_tp)
        << "27-char nanos parse must yield +123_456_789 ns; [I-NST-2 / New-3 / contract C3]";

    // Also verify via the roundtrip() helper (tests the format→parse oracle).
    const auto tp = utc_time_point{std::chrono::duration_cast<utc_time_point::duration>(nanoseconds{base_sec * 1'000'000'000LL + 123'456'789LL})};
    auto rt = roundtrip(tp, fix_time_precision::nanos);
    EXPECT_TRUE(rt.format_ok) << "format for nanos must succeed";
    EXPECT_TRUE(rt.parse_ok)  << "parse of nanos-formatted string must succeed";
    EXPECT_EQ(rt.parsed_tp, time_point_cast<utc_time_point::duration>(tp))
        << "nanos roundtrip not lossless; [I-NST-2 / FR-005]";
}

}  // namespace
}  // namespace fixpp::core::test
