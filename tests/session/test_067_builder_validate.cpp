// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/session/test_067_builder_validate.cpp
//
// 067-codegen-writer-emitter T022 [US3] [TESTS-FIRST] — validate_<Msg>
// required-presence witnesses (contracts/generated-builder.md G5;
// data-model.md §1.3/§1.4; spec.md SC-004):
//   (a) D NewOrderSingle missing a top-level required field -> failure;
//   (b) E NewOrderList missing ClOrdID(11) in a NoOrders entry -> failure
//       (recursive group depth);
//   (c) W vs X NoMDEntries per-occurrence required-set divergence (269
//       required in W, 279 required in X);
//   (d) fully-populated message validates clean AND commit() output is
//       unaffected by calling validate() (validation OFF the serialize
//       path);
//   (e) zero-required-field no-op: SecurityStatus (35=f) has zero required
//       application fields at any level; an EMPTY Args validates clean.
//
// MUST FAIL FIRST: references fixpp::v44::validate_NewOrderSingle/
// validate_NewOrderList/validate_MarketDataSnapshotFullRefresh/
// validate_MarketDataIncrementalRefresh/validate_SecurityStatus, none of
// which exist before T024 (builder_validate.hpp)/T025 (emitted
// writer_traits<T>/validate_<Msg>) land — Builders.hpp today (US1/US2)
// carries build_<Msg>/<Msg>Args/builder_registry only.
//
// ── (a)'s tag choice — ESCALATION, no anchor pins this exact tag ──────────
// spec.md SC-004 / tasks.md T022(a) both name "D NewOrderSingle missing
// Symbol(55)" as the top-level representative. Verified empirically against
// this shipped dictionary that claim does NOT hold: Symbol is declared
// `required='N'` INSIDE the `Instrument` component's own definition
// (dictionaries/FIX44.xml:2387), and the loader's expand_field_list
// (src/dictionary/xml_loader.cpp:425-533) reads a <field>/<group>'s OWN
// `required` attribute only -- the enclosing `<component name='Instrument'
// required='Y'/>` usage tag's `required` attribute
// (dictionaries/FIX44.xml:352) is never inspected. Confirmed against the
// generated build/linux-clang-debug/_codegen/include/fixpp/v44/
// Validator.hpp NewOrderSingle_rules row: `{55, field_presence::Optional}`
// (`static_cast<field_presence>(1)`). Symbol(55) is therefore genuinely
// OPTIONAL for NewOrderSingle in this dictionary; a required-presence table
// correctly DERIVED from `FieldRef.rule` (as this feature mandates, R3) must
// exclude it -- asserting rejection on a missing Symbol would be asserting
// the table is WRONG. ClOrdID(11) IS genuinely Required
// (`{11, field_presence::Required}`, dictionaries/FIX44.xml:327) and serves
// as the discriminating substitute for the SAME top-level-required-field
// intent. Flagged to the orchestrator; not a design re-derivation (the
// generation MECHANISM is unchanged) -- just a factual tag correction.
//
// Anchors: specs/067-codegen-writer-emitter/contracts/generated-builder.md
//          G5; data-model.md §1.3/§1.4/§2; research.md R3;
//          dictionaries/FIX44.xml (cited inline per assertion).

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <fixpp/core/decimal_alias.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/v44/Builders.hpp>  // GENERATED (Phase 5) — validate_<Msg>/writer_traits<T>
#include <memory_resource>
#include <optional>
#include <span>
#include <string_view>

#include "support/test_067_seeds.hpp"

namespace {

using fixpp::decimal_t;

decimal_t make_decimal(std::string_view sv, std::pmr::memory_resource* mr) {
    auto bytes =
        std::span<const std::byte>{reinterpret_cast<const std::byte*>(sv.data()), sv.size()};
    auto r = decimal_t::parse(bytes, mr);
    EXPECT_TRUE(r.has_value()) << "make_decimal failed for: " << sv;
    return r.value_or(decimal_t{});
}

// A fully valid D NewOrderSingle Args (all top-level required fields set:
// ClOrdID(11)/OrdType(40)/Side(54)/TransactTime(60), plus Symbol(55) which
// -- though optional per the dictionary -- is populated for realism).
fixpp::v44::NewOrderSingleArgs make_valid_new_order_single_args(std::pmr::memory_resource* mr) {
    fixpp::v44::NewOrderSingleArgs args{};
    args.cl_ord_id = "ORD-1";
    args.symbol = "MSFT";
    args.side = '1';
    args.order_qty = make_decimal("10", mr);
    args.price = make_decimal("99.5", mr);
    args.ord_type = '2';
    args.transact_time = "20240101-10:00:00";
    return args;
}

}  // namespace

// ── (a) D NewOrderSingle missing a top-level required field (ClOrdID(11) --
// see the file-level ESCALATION note re: Symbol(55)) ───────────────────────
TEST(BuilderValidate067, NewOrderSingle_MissingClOrdID_TopLevelRequiredFails) {
    std::pmr::monotonic_buffer_resource arena{4096};
    auto args = make_valid_new_order_single_args(&arena);
    args.cl_ord_id = std::nullopt;  // the discriminating missing field

    auto r = fixpp::v44::validate_NewOrderSingle(args);
    ASSERT_FALSE(r.has_value()) << "missing top-level required ClOrdID(11) must fail validate_";
    EXPECT_EQ(r.error(), fixpp::core::error::wire_required_field_missing);

    // Positive control: same shape, ClOrdID present -> validates clean.
    auto valid_args = make_valid_new_order_single_args(&arena);
    auto r_ok = fixpp::v44::validate_NewOrderSingle(valid_args);
    EXPECT_TRUE(r_ok.has_value()) << "with ClOrdID present the identical shape must validate clean";
}

// ── (b) E NewOrderList missing ClOrdID(11) in a NoOrders entry (recursive
// group depth) ──────────────────────────────────────────────────────────
TEST(BuilderValidate067, NewOrderList_MissingClOrdIdInNoOrdersEntry_GroupDepthFails) {
    using namespace fixpp_test_support::seeds067;
    auto const& seed = kNewOrderListSeed;

    fixpp::v44::groups::G_73_2Args order_args{};
    // cl_ord_id intentionally left unset -- the discriminating missing
    // GROUP-ENTRY required field (list_seq_no/side ARE the entry's other
    // two required members, dictionaries/FIX44.xml:2945-2947; both set so
    // the failure is unambiguously the ClOrdID omission).
    order_args.list_seq_no = seed.order.list_seq_no;
    order_args.side = seed.order.side;
    order_args.symbol = seed.order.symbol;
    std::array<fixpp::v44::groups::G_73_2Args, 1> orders{order_args};

    fixpp::v44::NewOrderListArgs args{};
    args.list_id = seed.list_id;
    args.bid_type = seed.bid_type;
    args.tot_no_orders = seed.tot_no_orders;
    args.orders = std::span<const fixpp::v44::groups::G_73_2Args>{orders};  // REQUIRED group

    auto r = fixpp::v44::validate_NewOrderList(args);
    ASSERT_FALSE(r.has_value())
        << "missing ClOrdID(11) inside a NoOrders entry must fail validate_ at group depth";
    EXPECT_EQ(r.error(), fixpp::core::error::wire_required_field_missing);

    // Positive control: same shape, ClOrdID present -> validates clean --
    // proves the failure above is specifically the group-entry ClOrdID
    // check, not a top-level omission (list_id/bid_type/tot_no_orders are
    // unchanged and correct in both legs).
    order_args.cl_ord_id = seed.order.cl_ord_id;
    orders[0] = order_args;
    args.orders = std::span<const fixpp::v44::groups::G_73_2Args>{orders};
    auto r_ok = fixpp::v44::validate_NewOrderList(args);
    EXPECT_TRUE(r_ok.has_value())
        << "with ClOrdID present at group depth the identical shape must validate clean";
}

// ── (c) W vs X NoMDEntries(268) per-occurrence required-set divergence:
// MDEntryType(269) required in W, MDUpdateAction(279) required in X --
// dictionaries/FIX44.xml:3023-3024 (W) vs :3060-3061 (X) ───────────────────
TEST(BuilderValidate067, WvsXPerOccurrenceRequiredSetDivergence) {
    using namespace fixpp_test_support::seeds067;

    // W: entry WITH MDEntryType(269) -> validates clean.
    {
        auto const& seed = kMarketDataSnapshotFullRefreshSeed;
        fixpp::v44::groups::G_268_1Args entry_args{};
        entry_args.md_entry_type = seed.entry.md_entry_type;
        std::array<fixpp::v44::groups::G_268_1Args, 1> entries{entry_args};
        fixpp::v44::MarketDataSnapshotFullRefreshArgs args{};
        args.symbol = seed.symbol;
        args.md_req_id = seed.md_req_id;
        args.md_entries =
            std::span<const fixpp::v44::groups::G_268_1Args>{entries};
        auto r = fixpp::v44::validate_MarketDataSnapshotFullRefresh(args);
        EXPECT_TRUE(r.has_value()) << "W entry WITH MDEntryType(269) must validate clean";
    }
    // W: entry MISSING MDEntryType(269) -> fails (269 required in W).
    {
        auto const& seed = kMarketDataSnapshotFullRefreshSeed;
        (void)seed;
        fixpp::v44::groups::G_268_1Args entry_args{};
        // md_entry_type intentionally left unset.
        std::array<fixpp::v44::groups::G_268_1Args, 1> entries{entry_args};
        fixpp::v44::MarketDataSnapshotFullRefreshArgs args{};
        args.symbol = "MSFT";
        args.md_req_id = "MDR-W1";
        args.md_entries =
            std::span<const fixpp::v44::groups::G_268_1Args>{entries};
        auto r = fixpp::v44::validate_MarketDataSnapshotFullRefresh(args);
        ASSERT_FALSE(r.has_value())
            << "W entry MISSING MDEntryType(269) must fail (269 required in W)";
        EXPECT_EQ(r.error(), fixpp::core::error::wire_required_field_missing);
    }
    // X: entry WITHOUT MDEntryType(269) (optional in X) but WITH
    // MDUpdateAction(279) -> validates clean -- proves 269 is NOT required
    // in X (the divergence from W).
    {
        auto const& seed = kMarketDataIncrementalRefreshSeed;
        fixpp::v44::groups::G_268_2Args entry_args{};
        entry_args.md_update_action = seed.entry.md_update_action;
        // md_entry_type intentionally left unset (optional in X).
        std::array<fixpp::v44::groups::G_268_2Args, 1> entries{entry_args};
        fixpp::v44::MarketDataIncrementalRefreshArgs args{};
        args.md_req_id = seed.md_req_id;
        args.md_entries =
            std::span<const fixpp::v44::groups::G_268_2Args>{entries};
        auto r = fixpp::v44::validate_MarketDataIncrementalRefresh(args);
        EXPECT_TRUE(r.has_value())
            << "X entry WITHOUT MDEntryType(269) but WITH MDUpdateAction(279) must validate "
               "clean -- 269 is NOT required in X (divergence from W)";
    }
    // X: entry MISSING MDUpdateAction(279) -> fails (279 required in X).
    {
        fixpp::v44::groups::G_268_2Args entry_args{};
        entry_args.md_entry_type = '0';
        // md_update_action intentionally left unset.
        std::array<fixpp::v44::groups::G_268_2Args, 1> entries{entry_args};
        fixpp::v44::MarketDataIncrementalRefreshArgs args{};
        args.md_req_id = "MDR-X1";
        args.md_entries =
            std::span<const fixpp::v44::groups::G_268_2Args>{entries};
        auto r = fixpp::v44::validate_MarketDataIncrementalRefresh(args);
        ASSERT_FALSE(r.has_value())
            << "X entry MISSING MDUpdateAction(279) must fail (279 required in X)";
        EXPECT_EQ(r.error(), fixpp::core::error::wire_required_field_missing);
    }
}

// ── (d) fully-populated message validates clean AND commit() output is
// byte-identical whether or not validate() was called first (validation OFF
// the serialize path) ───────────────────────────────────────────────────
TEST(BuilderValidate067, FullyPopulated_ValidatesClean_CommitUnaffectedByValidate) {
    std::pmr::monotonic_buffer_resource arena{4096};
    auto args = make_valid_new_order_single_args(&arena);

    auto validated = fixpp::v44::validate_NewOrderSingle(args);
    ASSERT_TRUE(validated.has_value()) << "fully-populated NewOrderSingle must validate clean";

    std::array<std::byte, 1024> out_before{};
    auto before = fixpp::v44::build_NewOrderSingle(std::span<std::byte>{out_before}, args);
    ASSERT_TRUE(before.has_value()) << "build_NewOrderSingle failed";

    // Re-validate (again) then re-build: commit() output must be identical
    // regardless of how many times / whether validate_() was called.
    auto validated_again = fixpp::v44::validate_NewOrderSingle(args);
    ASSERT_TRUE(validated_again.has_value());

    std::array<std::byte, 1024> out_after{};
    auto after = fixpp::v44::build_NewOrderSingle(std::span<std::byte>{out_after}, args);
    ASSERT_TRUE(after.has_value()) << "build_NewOrderSingle failed";

    ASSERT_TRUE(std::ranges::equal(*before, *after))
        << "commit() output must be byte-identical whether or not validate_() was called first";
}

// ── (e) zero-required-field no-op: SecurityStatus (35=f) has zero required
// application fields at any level (dictionaries/FIX44.xml:889-915 -- no
// field-level required='Y' in the message body; the Instrument/
// UndInstrmtGrp/InstrmtLegGrp components it references are entirely
// optional at every one of their own field/group definitions). An EMPTY
// Args must validate clean -- the no-op required-presence path exercised,
// not merely assumed. ────────────────────────────────────────────────────
TEST(BuilderValidate067, ZeroRequiredFieldMessage_EmptyArgs_ValidatesSuccess) {
    fixpp::v44::SecurityStatusArgs args{};  // fully default -- every field/group unset
    auto r = fixpp::v44::validate_SecurityStatus(args);
    EXPECT_TRUE(r.has_value())
        << "SecurityStatus has zero required application fields; an EMPTY Args must validate "
           "successfully (spec.md Edge Case, no-op required-presence path)";
}

// ── (f) Quote (35=S): an ENGAGED-EMPTY OPTIONAL group must validate clean --
// distinct from a REQUIRED empty group, which IS rejected ((b) above /
// RequiredGroupZero_ValidateRejects in test_067_builder_failclosed.cpp).
// party_i_ds is an OPTIONAL group on QuoteArgs (writer_traits<QuoteArgs>::
// group_checks: {false, &QuoteArgs_count_party_i_ds, ...}); engaging it with
// a zero-length span drives `gc.count(args)` to `optional<size_t>(0)` (NOT
// nullopt -- that would take the disengaged-skip branch instead) so
// builder_validate.hpp:85's `gc.required && *n == 0` guard evaluates its
// `gc.required` (false) leg -- the previously-uncovered BRDA:85,0 outcome --
// and falls through to the (zero-iteration) entry loop -- clean. This kills
// the mutant that drops the `gc.required &&` guard (`if (*n == 0) return
// reject`), which would wrongly reject a legitimately engaged-empty optional
// group. Anchors: spec.md G5 / Edge Case (engaged-empty optional group is
// allowed); builder_validate.hpp:85.
TEST(BuilderValidate067, Quote_EngagedEmptyOptionalGroup_ValidatesClean) {
    fixpp::v44::QuoteArgs args{};
    args.quote_id = "Q1";  // the sole required top-level field (117)
    args.party_i_ds =
        std::span<const fixpp::v44::groups::G_453Args>{};  // engaged (has_value), count()==0
    auto r = fixpp::v44::validate_Quote(args);
    EXPECT_TRUE(r.has_value())
        << "engaged-empty OPTIONAL group must validate clean (G5) -- distinct from a REQUIRED "
           "empty group, which is rejected";
}
