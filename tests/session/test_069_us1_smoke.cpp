// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/session/test_069_us1_smoke.cpp
//
// 069-v44-all-families T008 [US1] [TESTS-FIRST] — per-family smoke witness.
// Independent-Test criterion for US1 (tasks.md/spec.md): pick a previously-
// uncovered family (TradeCaptureReport, MsgType AE — NOT in kOfficial33),
// populate its typed Args, call build_TradeCaptureReport + validate_
// TradeCaptureReport, and confirm well-formed wire bytes carrying the
// seeded fields + required-field enforcement. Also discharges SC-005: the
// typed API is reachable without the generic runtime tag/value path.
//
// RED-vs-GREEN proof (data-model.md Entity "Coverage mode"): under
// `--families official` the symbol fixpp::v44::build_TradeCaptureReport is
// ABSENT (kOfficial33 has no "AE") so this TU cannot compile/link; under the
// default `all` mode (83 in-scope) it is present — see the phase report for
// the two `grep -c` counts against the regenerated build-tree header.
//
// Joins the existing test_067_builder_failclosed binary (Article VII §8 —
// no new executable, no gtest_discover_tests); its own LABELS carry
// "069;all_families;us1_smoke" alongside the host binary's 067 labels.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <fixpp/core/decimal_alias.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/v44/all.hpp>  // GENERATED — build_TradeCaptureReport is 069-new
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>

namespace {

using fixpp::decimal_t;

decimal_t make_decimal(std::string_view sv, std::pmr::memory_resource* mr) {
    auto bytes =
        std::span<const std::byte>{reinterpret_cast<const std::byte*>(sv.data()), sv.size()};
    auto r = decimal_t::parse(bytes, mr);
    EXPECT_TRUE(r.has_value()) << "make_decimal failed for: " << sv;
    return r.value_or(decimal_t{});
}

std::string bytes_to_string(std::span<const std::byte> b) {
    return std::string{reinterpret_cast<const char*>(b.data()), b.size()};
}

// TradeCaptureReport (35=AE) required fields (dictionaries/FIX44.xml:1435-
// 1493): TradeReportID(571), PreviouslyReported(570), LastQty(32),
// LastPx(31), TradeDate(75), TransactTime(60), and the TrdCapRptSideGrp
// NoSides(552) group (required, >=1 entry; entry-level Side(54)/OrderID(37)
// required, dictionaries/FIX44.xml:3536-3538).
fixpp::v44::TradeCaptureReportArgs make_valid_args(
    std::pmr::memory_resource* mr, std::span<const fixpp::v44::groups::G_552_1Args> sides) {
    fixpp::v44::TradeCaptureReportArgs args{};
    args.trade_report_id = "TRR-1";
    args.previously_reported = false;
    args.last_qty = make_decimal("100", mr);
    args.last_px = make_decimal("50.25", mr);
    args.trade_date = "20260711";
    args.transact_time = "20260711-12:00:00";
    args.sides = sides;
    return args;
}

}  // namespace

// ── build_TradeCaptureReport carries the seeded fields as well-formed wire
// bytes ────────────────────────────────────────────────────────────────────
TEST(US1Smoke069TradeCaptureReport, BuildCarriesSeededFieldsWellFormed) {
    std::pmr::monotonic_buffer_resource arena{4096};

    fixpp::v44::groups::G_552_1Args side_args{};
    side_args.side = '1';
    side_args.order_id = "ORD-9";
    std::array<fixpp::v44::groups::G_552_1Args, 1> sides{side_args};

    auto const args = make_valid_args(&arena, sides);

    std::array<std::byte, 2048> out{};
    auto r = fixpp::v44::build_TradeCaptureReport(std::span<std::byte>{out}, args);
    ASSERT_TRUE(r.has_value()) << "build_TradeCaptureReport failed";
    std::string const body = bytes_to_string(*r);

    EXPECT_NE(body.find("\x01"
                        "571=TRR-1\x01"),
              std::string::npos)
        << "TradeReportID(571) must carry the seeded value verbatim";
    EXPECT_NE(body.find("\x01"
                        "570=N\x01"),
              std::string::npos)
        << "PreviouslyReported(570) must serialize false as N (FR-007a Bool -> Y/N)";
    EXPECT_NE(body.find("\x01"
                        "32=100\x01"),
              std::string::npos)
        << "LastQty(32) must carry the seeded decimal";
    EXPECT_NE(body.find("\x01"
                        "31=50.25\x01"),
              std::string::npos)
        << "LastPx(31) must carry the seeded decimal";
    EXPECT_NE(body.find("\x01"
                        "75=20260711\x01"),
              std::string::npos)
        << "TradeDate(75) must carry the seeded value";
    EXPECT_NE(body.find("\x01"
                        "54=1\x01"),
              std::string::npos)
        << "the NoSides(552) entry's Side(54) must carry the seeded value";
    EXPECT_NE(body.find("\x01"
                        "37=ORD-9\x01"),
              std::string::npos)
        << "the NoSides(552) entry's OrderID(37) must carry the seeded value";

    auto const validated = fixpp::v44::validate_TradeCaptureReport(args);
    EXPECT_TRUE(validated.has_value()) << "a fully-populated TradeCaptureReportArgs must validate";
}

// ── required-field enforcement: an empty NoSides(552) (the REQUIRED
// TrdCapRptSideGrp) fails validate_ with wire_required_field_missing (mirrors
// test_067_builder_failclosed.cpp's RequiredGroupZero_ValidateRejects for
// NewOrderList's NoOrders(73)) ───────────────────────────────────────────────
TEST(US1Smoke069TradeCaptureReport, EmptyRequiredSidesGroup_ValidateRejects) {
    std::pmr::monotonic_buffer_resource arena{4096};

    auto args = make_valid_args(&arena, std::span<const fixpp::v44::groups::G_552_1Args>{});

    auto const r = fixpp::v44::validate_TradeCaptureReport(args);
    ASSERT_FALSE(r.has_value()) << "an empty REQUIRED NoSides(552) group must fail validate_";
    EXPECT_EQ(r.error(), fixpp::core::error::wire_required_field_missing);
}

// ── required-field enforcement: a missing required scalar (TradeReportID,
// 571) fails validate_ with wire_required_field_missing ─────────────────────
TEST(US1Smoke069TradeCaptureReport, MissingTradeReportID_ValidateRejects) {
    std::pmr::monotonic_buffer_resource arena{4096};

    fixpp::v44::groups::G_552_1Args side_args{};
    side_args.side = '1';
    side_args.order_id = "ORD-9";
    std::array<fixpp::v44::groups::G_552_1Args, 1> sides{side_args};

    auto args = make_valid_args(&arena, sides);
    args.trade_report_id = std::nullopt;

    auto const r = fixpp::v44::validate_TradeCaptureReport(args);
    ASSERT_FALSE(r.has_value()) << "a missing required TradeReportID(571) must fail validate_";
    EXPECT_EQ(r.error(), fixpp::core::error::wire_required_field_missing);
}
