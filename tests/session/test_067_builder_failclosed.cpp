// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/session/test_067_builder_failclosed.cpp
//
// 067-codegen-writer-emitter T015 [US1] [TESTS-FIRST] (RC#6/G9) —
// generated-wrapper fail-closed witnesses. The 061 fail-closed seam
// (test_exemplar_build_failclosed.cpp) pins the HAND builders; this file
// pins the GENERATED builders, which are a NEW conversion surface (Args ->
// body_builder calls) needing direct coverage, per contracts/generated-
// builder.md G9:
//   - undersized `out` -> typed error, `out` byte-unchanged (INV-4);
//   - control-byte/SOH in a string value -> rejected before any byte
//     reaches `out`;
//   - Bool -> `Y`/`N`, never `1`/`0` (FR-007a);
//   - Length+Data coupling: clean-text Data auto-derives its Length, both
//     emitted, coupled (FR-007a);
//   - W-vs-X per-occurrence NoMDEntries(268) delimiter discrimination
//     (RC#1) — generated W and X builders emit DISTINCT delimiter/order
//     from the SAME no_tag.
//   - T023 [US3]: a REQUIRED group with size()==0 fails validate_* with
//     wire_required_field_missing; an OPTIONAL group nullopt omits No<G>
//     from the wire entirely (RC#2/G9).
//
// MUST FAIL FIRST: references `fixpp::v44::build_NewOrderSingle` /
// `NewOrderSingleArgs` / `build_MarketDataSnapshotFullRefresh` /
// `build_MarketDataIncrementalRefresh` etc., not yet generated (Phase 3b).
//
// Anchors: specs/067-codegen-writer-emitter/contracts/generated-builder.md
//          G9; data-model.md §1.1 (Bool/Length+Data Args semantics);
//          research.md R9/RC#1 (W/X delimiter divergence).

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <fixpp/core/decimal_alias.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/v44/Builders.hpp>  // GENERATED (Phase 3b)
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>

#include "support/test_067_seeds.hpp"

namespace {

using fixpp::decimal_t;

constexpr std::byte kSentinel{0xABU};

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

template <std::size_t N>
void assert_unchanged(std::array<std::byte, N> const& buf, char const* label) {
    SCOPED_TRACE(label);
    for (std::size_t i = 0; i < buf.size(); ++i) {
        if (buf[i] != kSentinel) {
            ADD_FAILURE() << "buf[" << i << "] modified on a fail-closed path (expected 0xAB, got 0x"
                          << std::hex << static_cast<unsigned>(buf[i]) << ")";
            return;
        }
    }
}

fixpp::v44::NewOrderSingleArgs make_valid_new_order_single_args(std::pmr::memory_resource* mr) {
    fixpp::v44::NewOrderSingleArgs args{};
    args.cl_ord_id = "ORD-1";
    args.symbol = "MSFT";
    args.side = '1';
    args.order_qty = make_decimal("10", mr);
    args.price = make_decimal("99.5", mr);
    return args;
}

}  // namespace

// ── undersized `out` untouched (INV-4) ───────────────────────────────────
TEST(BuilderFailClosed067, UndersizedOut_Untouched) {
    std::pmr::monotonic_buffer_resource arena{4096};
    auto args = make_valid_new_order_single_args(&arena);

    std::array<std::byte, 5> tiny{};
    tiny.fill(kSentinel);
    auto r = fixpp::v44::build_NewOrderSingle(std::span<std::byte>{tiny}, args);
    ASSERT_FALSE(r.has_value()) << "undersized out must fail-closed";
    EXPECT_EQ(r.error(), fixpp::core::error::wire_frame_too_large);
    assert_unchanged(tiny, "undersized out");
}

// ── control-byte/SOH in a string value rejected before any byte reaches
// `out` ───────────────────────────────────────────────────────────────────
TEST(BuilderFailClosed067, SohInValue_RejectedBeforeAnyByteReachesOut) {
    std::pmr::monotonic_buffer_resource arena{4096};
    auto args = make_valid_new_order_single_args(&arena);
    args.cl_ord_id = "ORD\x01"
                     "49=EVIL";  // session-tag injection attempt

    std::array<std::byte, 1024> out{};
    out.fill(kSentinel);
    auto r = fixpp::v44::build_NewOrderSingle(std::span<std::byte>{out}, args);
    ASSERT_FALSE(r.has_value()) << "SOH/control-byte in a value must fail-closed";
    EXPECT_EQ(r.error(), fixpp::core::error::wire_field_value_out_of_range);
    assert_unchanged(out, "SOH-in-value");
}

// ── Bool -> Y/N, never 1/0 (FR-007a) ─────────────────────────────────────
TEST(BuilderFailClosed067, BoolLocateReqd_SerializesYN_NeverOneZero) {
    std::pmr::monotonic_buffer_resource arena{4096};

    {
        auto args = make_valid_new_order_single_args(&arena);
        args.locate_reqd = true;
        std::array<std::byte, 1024> out{};
        auto r = fixpp::v44::build_NewOrderSingle(std::span<std::byte>{out}, args);
        ASSERT_TRUE(r.has_value()) << "build_NewOrderSingle failed";
        std::string body = bytes_to_string(*r);
        EXPECT_NE(body.find("\x01" "114=Y\x01"), std::string::npos)
            << "LocateReqd(114)=true must serialize as 114=Y";
        EXPECT_EQ(body.find("\x01" "114=1\x01"), std::string::npos)
            << "LocateReqd(114) must NEVER serialize as the int64 path (114=1)";
    }
    {
        auto args = make_valid_new_order_single_args(&arena);
        args.locate_reqd = false;
        std::array<std::byte, 1024> out{};
        auto r = fixpp::v44::build_NewOrderSingle(std::span<std::byte>{out}, args);
        ASSERT_TRUE(r.has_value()) << "build_NewOrderSingle failed";
        std::string body = bytes_to_string(*r);
        EXPECT_NE(body.find("\x01" "114=N\x01"), std::string::npos)
            << "LocateReqd(114)=false must serialize as 114=N";
        EXPECT_EQ(body.find("\x01" "114=0\x01"), std::string::npos)
            << "LocateReqd(114) must NEVER serialize as the int64 path (114=0)";
    }
}

// ── Length+Data coupling: clean-text Data auto-derives its Length, both
// emitted (FR-007a) ────────────────────────────────────────────────────────
TEST(BuilderFailClosed067, LengthDataCoupling_AutoDerivedLengthBothEmitted) {
    std::pmr::monotonic_buffer_resource arena{4096};
    auto args = make_valid_new_order_single_args(&arena);
    args.encoded_text = "hello world";  // 11 bytes, clean text

    std::array<std::byte, 1024> out{};
    auto r = fixpp::v44::build_NewOrderSingle(std::span<std::byte>{out}, args);
    ASSERT_TRUE(r.has_value()) << "build_NewOrderSingle failed";
    std::string body = bytes_to_string(*r);

    // EncodedTextLen(354) auto-derived == 11, EncodedText(355) verbatim,
    // Length BEFORE Data (354 < 355, tag-ascending top-level regime).
    EXPECT_NE(body.find("\x01" "354=11\x01"), std::string::npos)
        << "EncodedTextLen(354) must auto-derive to the byte length (11)";
    EXPECT_NE(body.find("\x01" "355=hello world\x01"), std::string::npos)
        << "EncodedText(355) must carry the value verbatim";
    auto const pos354 = body.find("\x01" "354=");
    auto const pos355 = body.find("\x01" "355=");
    ASSERT_NE(pos354, std::string::npos);
    ASSERT_NE(pos355, std::string::npos);
    EXPECT_LT(pos354, pos355) << "Length(354) must be emitted BEFORE Data(355), coupled";
}

// ── W-vs-X per-occurrence NoMDEntries(268) delimiter discrimination
// (RC#1) ────────────────────────────────────────────────────────────────
TEST(BuilderFailClosed067, WvsXPerOccurrenceDelimiterDiscrimination) {
    using namespace fixpp_test_support::seeds067;
    std::pmr::monotonic_buffer_resource arena{4096};

    // W: MDFullGrp NoMDEntries(268) delimiter MDEntryType(269).
    {
        auto const& seed = kMarketDataSnapshotFullRefreshSeed;
        fixpp::v44::groups::G_268_1Args entry_args{};
        entry_args.md_entry_type = seed.entry.md_entry_type;
        entry_args.md_entry_px = make_decimal(seed.entry.md_entry_px, &arena);
        entry_args.md_entry_size = make_decimal(seed.entry.md_entry_size, &arena);
        std::array<fixpp::v44::groups::G_268_1Args, 1> entries{entry_args};

        fixpp::v44::MarketDataSnapshotFullRefreshArgs args{};
        args.symbol = seed.symbol;
        args.md_req_id = seed.md_req_id;
        args.md_entries =
            std::span<const fixpp::v44::groups::G_268_1Args>{entries};

        std::array<std::byte, 2048> out{};
        auto r = fixpp::v44::build_MarketDataSnapshotFullRefresh(std::span<std::byte>{out}, args);
        ASSERT_TRUE(r.has_value()) << "build_MarketDataSnapshotFullRefresh failed";
        std::string body = bytes_to_string(*r);
        // W's entry opens on MDEntryType(269); MDUpdateAction(279) is not a
        // W group member at all.
        EXPECT_NE(body.find("\x01" "269="), std::string::npos) << "W entry must contain MDEntryType(269)";
        EXPECT_EQ(body.find("\x01" "279="), std::string::npos)
            << "W's NoMDEntries has no MDUpdateAction(279) member at all";
    }

    // X: MDIncGrp NoMDEntries(268) delimiter MDUpdateAction(279) — SAME
    // no_tag as W, DIFFERENT delimiter (the discriminating pin: a version-
    // wide/tag-sorted plan would wrongly emit 269 first for X too, since
    // 269 < 279).
    {
        auto const& seed = kMarketDataIncrementalRefreshSeed;
        fixpp::v44::groups::G_268_2Args entry_args{};
        entry_args.md_update_action = seed.entry.md_update_action;
        entry_args.md_entry_type = seed.entry.md_entry_type;
        entry_args.md_entry_px = make_decimal(seed.entry.md_entry_px, &arena);
        std::array<fixpp::v44::groups::G_268_2Args, 1> entries{entry_args};

        fixpp::v44::MarketDataIncrementalRefreshArgs args{};
        args.md_req_id = seed.md_req_id;
        args.md_entries =
            std::span<const fixpp::v44::groups::G_268_2Args>{entries};

        std::array<std::byte, 2048> out{};
        auto r = fixpp::v44::build_MarketDataIncrementalRefresh(std::span<std::byte>{out}, args);
        ASSERT_TRUE(r.has_value()) << "build_MarketDataIncrementalRefresh failed";
        std::string body = bytes_to_string(*r);
        auto const pos279 = body.find("\x01" "279=");
        auto const pos269 = body.find("\x01" "269=");
        ASSERT_NE(pos279, std::string::npos) << "X entry must contain MDUpdateAction(279)";
        ASSERT_NE(pos269, std::string::npos) << "X entry must contain MDEntryType(269)";
        EXPECT_LT(pos279, pos269)
            << "X's NoMDEntries delimiter must be MDUpdateAction(279) FIRST — a version-wide plan "
               "sharing W's delimiter would wrongly put MDEntryType(269) first here too";
    }
}

// ── T023 [US3]/RC#2/G9 — a REQUIRED group (non-optional span) with
// size()==0 fails validate_* with wire_required_field_missing ─────────────
TEST(BuilderFailClosed067, RequiredGroupZero_ValidateRejects) {
    using namespace fixpp_test_support::seeds067;
    auto const& seed = kNewOrderListSeed;

    fixpp::v44::NewOrderListArgs args{};
    args.list_id = seed.list_id;
    args.bid_type = seed.bid_type;
    args.tot_no_orders = seed.tot_no_orders;
    // orders left as a zero-length span: E's NoOrders(73) group is REQUIRED
    // (dictionaries/FIX44.xml:2944 `<group name='NoOrders' required='Y'>`).
    args.orders = std::span<const fixpp::v44::groups::G_73_2Args>{};

    auto r = fixpp::v44::validate_NewOrderList(args);
    ASSERT_FALSE(r.has_value()) << "a REQUIRED group with size()==0 must fail validate_";
    EXPECT_EQ(r.error(), fixpp::core::error::wire_required_field_missing);
}

// ── T023 [US3]/RC#2/G9 — an OPTIONAL group nullopt omits No<G> from the
// wire entirely (build_-path discriminating byte witness) ─────────────────
TEST(BuilderFailClosed067, OptionalGroupNullopt_OmitsNoGroupTagEntirely) {
    using namespace fixpp_test_support::seeds067;
    auto const& seed = kNewOrderListSeed;

    fixpp::v44::groups::G_73_2Args order_args{};
    order_args.cl_ord_id = seed.order.cl_ord_id;
    order_args.list_seq_no = seed.order.list_seq_no;
    order_args.side = seed.order.side;
    order_args.symbol = seed.order.symbol;
    // party_i_ds (NoPartyIDs(453), OPTIONAL at order depth) intentionally
    // left nullopt (its default).
    std::array<fixpp::v44::groups::G_73_2Args, 1> orders{order_args};

    fixpp::v44::NewOrderListArgs args{};
    args.list_id = seed.list_id;
    args.bid_type = seed.bid_type;
    args.tot_no_orders = seed.tot_no_orders;
    args.orders = std::span<const fixpp::v44::groups::G_73_2Args>{orders};

    std::array<std::byte, 2048> out{};
    auto r = fixpp::v44::build_NewOrderList(std::span<std::byte>{out}, args);
    ASSERT_TRUE(r.has_value()) << "build_NewOrderList failed";
    std::string body = bytes_to_string(*r);
    EXPECT_EQ(body.find("\x01" "453="), std::string::npos)
        << "an OPTIONAL group left nullopt must omit its No<G> tag (453=NoPartyIDs) from the "
           "wire entirely";
}
