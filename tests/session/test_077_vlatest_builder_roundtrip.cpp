// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/session/test_077_vlatest_builder_roundtrip.cpp
//
// 077-builder-args-dedup T012 [US1] -- vlatest deep-group round-trip smoke
// (specs/077-builder-args-dedup/tasks.md T012; quickstart.md V2; SC-003
// partial). The US1 MVP independent-test smoke: build ONE FIX Latest
// message carrying Instrument + Legs + Underlyings via the deduped typed
// builder (build_TradeCaptureReport over fixpp::vlatest::TradeCaptureReportArgs,
// referencing the shared fixpp::vlatest::groups::G_555_5Args/G_711_1Args/
// G_552_1Args plans -- G2), read the frame back field-for-field via the
// runtime dict-driven as_table_view() path, assert equality with zero skips.
// The EXHAUSTIVE 100%-of-messages SC-003 coverage for vlatest lands in T016
// (US2); this is one deep-group message.
//
// Message choice: TradeCaptureReport (AE) -- dictionaries/orchestra/
// OrchestraFIXLatest.xml. Instrument's fields are flattened directly onto
// TradeCaptureReportArgs (symbol/security_id/security_type, not a nested
// group -- Instrument is a scalar component, not a repeating group); Legs =
// NoLegs(555) -> groups::G_555_5Args (a multi-plan no_tag, ordinal 5 --
// G1a); Underlyings = NoUnderlyings(711) -> groups::G_711_1Args; NoSides(552)
// -> groups::G_552_1Args is REQUIRED on this message (top-level `sides` is a
// bare std::span, not optional) so it is populated too, mirroring
// tests/session/test_069_all_families_roundtrip.cpp's TradeCaptureReport
// case (the mandated nested exemplar there) -- extended here with the
// legs/underlyings groups 069 did not need.
//
// Dict loading differs from the 069/067 v44 pattern (which uses XmlLoader +
// FIX44.xml): vlatest is loaded via fixpp::dict::OrchestraLoader over
// dictionaries/orchestra/OrchestraFIXLatest.xml (074's native Orchestra
// reader) -- table_view()/Parser<Index>{tv} downstream are identical
// (dict::Dictionary is the same runtime type regardless of loader; see
// tests/dictionary/orchestra_loader_test.cpp:143). The wire BeginString
// label ("FIXT.1.1", the real-world FIX Latest session transport string) is
// inert to the dict-aware Parser<Index>{tv} ctor, which resolves solely from
// the explicit table_view argument, not from BeginString (mirrors
// tests/wire/vlatest_reify_roundtrip_test.cpp's make_minimal_frame comment).
//
// Anchors: specs/077-builder-args-dedup/tasks.md T012;
//          specs/077-builder-args-dedup/contracts/generated-builder-dedup.md
//          G1a/G2;
//          tests/session/test_069_all_families_roundtrip.cpp (build/parse/
//          group-entry-readback pattern, TradeCaptureReport case);
//          tests/dictionary/orchestra_loader_test.cpp (OrchestraLoader ->
//          table_view precedent);
//          tests/support/app_message_read_scaffold.hpp (make_frame/
//          parse_dict/make_decimal/bytes_to_string, version-agnostic).

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/decimal_alias.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/orchestra_loader.hpp>
#include <fixpp/vlatest/Builders.hpp>  // GENERATED (077) -- build_<Msg>/<Msg>Args/groups::G_...Args
#include <fixpp/vlatest/Messages.hpp>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "support/app_message_read_scaffold.hpp"

namespace {

using fixpp::decimal_t;
using fixpp_test_support::bytes_to_string;
using fixpp_test_support::make_decimal;
using fixpp_test_support::make_frame;
using fixpp_test_support::parse_dict;
using IndexView = fixpp::wire::MessageView<fixpp::wire::access_mode::Index>;

// Load the real FIX Latest (EP303) dictionary via the native Orchestra
// reader (074) -- the vlatest analog of app_message_read_scaffold.hpp's
// load_fix44(). Consuming target defines FIXPP_ORCHESTRA_DATA_DIR (mirrors
// tests/dictionary/CMakeLists.txt:96/129).
fixpp::dict::Dictionary load_vlatest(std::pmr::memory_resource* mr) {
    fixpp::dict::OrchestraLoader loader;
    return loader.load(std::string(FIXPP_ORCHESTRA_DATA_DIR) + "/OrchestraFIXLatest.xml", mr);
}

// Local group-entry scan (mirrors test_069_all_families_roundtrip.cpp's
// TU-local scan_slice_for_tag -- duplicated here since this is a standalone
// executable, not joined to that TU).
std::optional<std::string_view> scan_slice_for_tag(std::span<const std::byte> slice,
                                                    std::uint16_t tag) {
    std::string_view sv{reinterpret_cast<char const*>(slice.data()), slice.size()};
    std::size_t pos = 0;
    while (pos < sv.size()) {
        std::size_t const eq = sv.find('=', pos);
        if (eq == std::string_view::npos) break;
        std::string_view const tag_sv = sv.substr(pos, eq - pos);
        std::size_t const soh = sv.find('\x01', eq);
        std::size_t const value_end = (soh == std::string_view::npos) ? sv.size() : soh;
        std::uint16_t parsed_tag = 0;
        bool ok = !tag_sv.empty();
        for (char c : tag_sv) {
            if (c < '0' || c > '9') {
                ok = false;
                break;
            }
            parsed_tag = static_cast<std::uint16_t>(parsed_tag * 10 + (c - '0'));
        }
        if (ok && parsed_tag == tag) {
            return sv.substr(eq + 1, value_end - (eq + 1));
        }
        if (soh == std::string_view::npos) break;
        pos = soh + 1;
    }
    return std::nullopt;
}

}  // namespace

// table_view (tv) must outlive every MessageView derived from it (same
// lifetime contract as test_069_all_families_roundtrip.cpp's build_and_parse
// -- MessageView::opaque_dict_ aliases the table_view OBJECT).
class VlatestBuilderRoundtrip077 : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        dict_arena_ = new std::pmr::monotonic_buffer_resource(65536);
        dict_ = new fixpp::dict::Dictionary(load_vlatest(dict_arena_));
        tv_ = new fixpp::dict::table_view(dict_->as_table_view());
    }

    static void TearDownTestSuite() {
        delete tv_;
        delete dict_;
        delete dict_arena_;
        tv_ = nullptr;
        dict_ = nullptr;
        dict_arena_ = nullptr;
    }

    std::array<std::byte, 8192> out{};
    std::pmr::monotonic_buffer_resource read_arena{16384};

    static std::pmr::monotonic_buffer_resource* dict_arena_;
    static fixpp::dict::Dictionary* dict_;
    static fixpp::dict::table_view* tv_;
};

std::pmr::monotonic_buffer_resource* VlatestBuilderRoundtrip077::dict_arena_ = nullptr;
fixpp::dict::Dictionary* VlatestBuilderRoundtrip077::dict_ = nullptr;
fixpp::dict::table_view* VlatestBuilderRoundtrip077::tv_ = nullptr;

// TradeCaptureReport (AE) -- Instrument (flattened scalars) + Legs (NoLegs/
// 555 -> groups::G_555_5Args) + Underlyings (NoUnderlyings/711 ->
// groups::G_711_1Args) + required Sides (NoSides/552 -> groups::G_552_1Args).
// Field tags confirmed against dictionaries/orchestra/OrchestraFIXLatest.xml:
//   55 Symbol, 48 SecurityID, 167 SecurityType (Instrument, flattened)
//   571 TradeReportID, 570 PreviouslyReported, 32 LastQty, 31 LastPx,
//   75 TradeDate, 60 TransactTime (own scalars)
//   54 Side, 37 OrderID (NoSides entry)
//   600 LegSymbol, 602 LegSecurityID (NoLegs entry)
//   311 UnderlyingSymbol, 309 UnderlyingSecurityID (NoUnderlyings entry)
TEST_F(VlatestBuilderRoundtrip077, TradeCaptureReportInstrumentLegsUnderlyings) {
    std::pmr::monotonic_buffer_resource arena{4096};

    fixpp::vlatest::TradeCaptureReportArgs args{};
    args.symbol = "AE_symbol";
    args.security_id = "AE_security_id";
    args.security_type = "AE_security_type";
    args.trade_report_id = "AE_trade_report_id";
    args.previously_reported = true;
    args.last_qty = make_decimal("10.5", &arena);
    args.last_px = make_decimal("101.25", &arena);
    args.trade_date = "AE_trade_date";
    args.transact_time = "AE_transact_time";

    fixpp::vlatest::groups::G_552_1Args side_entry{};
    side_entry.side = '1';
    side_entry.order_id = "AE_side_order_id";
    std::array<fixpp::vlatest::groups::G_552_1Args, 1> sides_arr{side_entry};
    args.sides = std::span<const fixpp::vlatest::groups::G_552_1Args>{sides_arr};

    fixpp::vlatest::groups::G_555_5Args leg_entry{};
    leg_entry.leg_symbol = "AE_leg_symbol";
    leg_entry.leg_security_id = "AE_leg_security_id";
    std::array<fixpp::vlatest::groups::G_555_5Args, 1> legs_arr{leg_entry};
    args.legs = std::span<const fixpp::vlatest::groups::G_555_5Args>{legs_arr};

    fixpp::vlatest::groups::G_711_1Args underlying_entry{};
    underlying_entry.underlying_symbol = "AE_underlying_symbol";
    underlying_entry.underlying_security_id = "AE_underlying_security_id";
    std::array<fixpp::vlatest::groups::G_711_1Args, 1> underlyings_arr{underlying_entry};
    args.underlyings = std::span<const fixpp::vlatest::groups::G_711_1Args>{underlyings_arr};

    auto built = fixpp::vlatest::build_TradeCaptureReport(out, args);
    ASSERT_TRUE(built.has_value()) << "build_TradeCaptureReport failed";

    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    // parse_dict() returns the MessageView directly (ADD_FAILURE + a default
    // MessageView on failure, not an optional) -- unlike
    // test_069_all_families_roundtrip.cpp's local build_and_parse() wrapper,
    // which wraps it in std::optional. Bail out on that ADD_FAILURE (a
    // default-constructed MessageView is not a safe base for the get()/
    // offsets() calls below) via HasFatalFailure()-style ASSERT gating.
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built TradeCaptureReport frame failed (see ADD_FAILURE above)";

    // ── Top-level scalars (Instrument + own fields) ─────────────────────────
    auto expect_text = [&](std::uint16_t tag, std::string_view expected, char const* label) {
        SCOPED_TRACE(label);
        auto fv = mv.get(tag);
        ASSERT_TRUE(fv.has_value()) << "tag " << tag << " not found in parsed frame";
        EXPECT_EQ(fv->as_string(), expected);
    };
    expect_text(55, "AE_symbol", "symbol");
    expect_text(48, "AE_security_id", "security_id");
    expect_text(167, "AE_security_type", "security_type");
    expect_text(571, "AE_trade_report_id", "trade_report_id");
    expect_text(570, "Y", "previously_reported");
    expect_text(75, "AE_trade_date", "trade_date");
    expect_text(60, "AE_transact_time", "transact_time");

    auto got_last_qty = mv.get_decimal(32, &read_arena);
    ASSERT_TRUE(got_last_qty.has_value()) << "last_qty(32) not found/decodable";
    EXPECT_EQ(*got_last_qty, make_decimal("10.5", &read_arena));
    auto got_last_px = mv.get_decimal(31, &read_arena);
    ASSERT_TRUE(got_last_px.has_value()) << "last_px(31) not found/decodable";
    EXPECT_EQ(*got_last_px, make_decimal("101.25", &read_arena));

    // ── NoSides(552) entry-level readback ────────────────────────────────────
    auto side_slices = mv.offsets().group_slices(552);
    ASSERT_EQ(side_slices.size(), 1u) << "NoSides(552) must carry exactly 1 entry";
    std::span<const std::byte> const side_entry0{side_slices[0].data, side_slices[0].len};
    auto side_val = scan_slice_for_tag(side_entry0, 54);
    ASSERT_TRUE(side_val.has_value()) << "Side(54) not found in NoSides entry";
    EXPECT_EQ(*side_val, "1");
    auto side_order_id = scan_slice_for_tag(side_entry0, 37);
    ASSERT_TRUE(side_order_id.has_value()) << "OrderID(37) not found in NoSides entry";
    EXPECT_EQ(*side_order_id, "AE_side_order_id");

    // ── NoLegs(555) entry-level readback ─────────────────────────────────────
    auto leg_slices = mv.offsets().group_slices(555);
    ASSERT_EQ(leg_slices.size(), 1u) << "NoLegs(555) must carry exactly 1 entry";
    std::span<const std::byte> const leg_entry0{leg_slices[0].data, leg_slices[0].len};
    auto leg_symbol = scan_slice_for_tag(leg_entry0, 600);
    ASSERT_TRUE(leg_symbol.has_value()) << "LegSymbol(600) not found in NoLegs entry";
    EXPECT_EQ(*leg_symbol, "AE_leg_symbol");
    auto leg_security_id = scan_slice_for_tag(leg_entry0, 602);
    ASSERT_TRUE(leg_security_id.has_value()) << "LegSecurityID(602) not found in NoLegs entry";
    EXPECT_EQ(*leg_security_id, "AE_leg_security_id");

    // ── NoUnderlyings(711) entry-level readback ──────────────────────────────
    auto underlying_slices = mv.offsets().group_slices(711);
    ASSERT_EQ(underlying_slices.size(), 1u) << "NoUnderlyings(711) must carry exactly 1 entry";
    std::span<const std::byte> const underlying_entry0{underlying_slices[0].data,
                                                        underlying_slices[0].len};
    auto underlying_symbol = scan_slice_for_tag(underlying_entry0, 311);
    ASSERT_TRUE(underlying_symbol.has_value()) << "UnderlyingSymbol(311) not found in NoUnderlyings entry";
    EXPECT_EQ(*underlying_symbol, "AE_underlying_symbol");
    auto underlying_security_id = scan_slice_for_tag(underlying_entry0, 309);
    ASSERT_TRUE(underlying_security_id.has_value())
        << "UnderlyingSecurityID(309) not found in NoUnderlyings entry";
    EXPECT_EQ(*underlying_security_id, "AE_underlying_security_id");
}
