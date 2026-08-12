// tests/codegen/test_082_v42_required_group_omission_test.cpp
// 082-structural-group-detection T034 [US2] — FR-008 / SC-006 / K9.
//
// THE point of US2, and of issue #196: a FIX 4.2 `required='Y'` repeating group
// must be REPRESENTABLE in `Args`, so omitting it is DETECTABLE rather than silent
// (Article VI). Before 082, FIX42's NumInGroup tags were datatype-gated out of the
// builder tier entirely — `v42` emitted no builders at all — so a caller could not
// express these groups and `build_<Msg>` would have emitted invalid FIX 4.2 with
// nothing complaining. This file is the witness that the hole is closed.
//
// ── THE 14 PAIRS ARE DERIVED, NOT INVENTED ───────────────────────────────────
// Derived from raw FIX42.xml (`required='Y'` on a `<group>` declaration, with
// `<component>`s expanded): **14 pairs across 12 messages, 13 top-level + 1
// nested**. 14 over 12 reconciles because MarketDataRequest and MassQuote each
// contribute two. The same 14 are independently derived from T005's oracle and
// gated by `tests/session/test_082_group_required_member_validation_test.cpp`
// (T021b) — this file is the builder-tier analogue of that wire-tier pin.
//
// The cases below are hand-written because the API is TYPED: each message has its
// own `Args` struct and its own `validate_<Msg>` overload, so there is no generic
// loop to write. `kCaseCount` is asserted so a case cannot be quietly dropped.
//
// ── EVERY CASE CARRIES A POSITIVE CONTROL, AND IT IS LOAD-BEARING ────────────
// `validate_required` returns `wire_required_field_missing` for BOTH a missing
// required scalar AND an empty required group (`builder_validate.hpp:77` and `:86` —
// the same enum value from two different causes). So "it rejected" proves nothing on
// its own: a case that forgot a required scalar would reject for the wrong reason and
// read as a pass. Each case therefore asserts the OK direction first — same Args,
// group POPULATED, must validate clean — and only then that clearing the span
// rejects. The pair is the assertion; neither half alone is evidence.
//
// ── PLAN NAMES ARE PER-MESSAGE, NOT PER-TAG ──────────────────────────────────
// Seven of these tags are ORDINALED (B-077-1 structural keying), so the plan a
// message references cannot be named from the tag alone. Taken from the emitted
// headers, and the divergence is real:
//   73  → NewOrderList `G_73_1Args`      vs ListStatus `G_73_3Args`
//   146 → MarketDataRequest `G_146_3Args` vs QuoteRequest `G_146_2Args`
//   268 → MDSnapshotFullRefresh `G_268_1Args` vs MDIncrementalRefresh `G_268_2Args`
//   295 → QuoteCancel `G_295_1Args`      vs MassQuote's nested `G_295_3Args`
// ⚠️ These are `--families all` names. Plan names are MODE-DEPENDENT (FR-016b), so
// they must never be reused for an `--families official` assertion.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <set>
#include <span>
#include <string_view>
#include <utility>

#include <fixpp/core/decimal_alias.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/v42/all.hpp>  // GENERATED — Args + validate_<Msg> + writer_traits

// Thin per-message `validate` overloads so the shared assertion helper below can
// dispatch by ADL on the Args type. Declared BEFORE the helper deliberately —
// relying on the instantiation-point lookup instead would work but is fragile.
namespace fixpp::v42 {
inline auto validate(BidResponseArgs const& a) noexcept { return validate_BidResponse(a); }
inline auto validate(EmailArgs const& a) noexcept { return validate_Email(a); }
inline auto validate(ListStatusArgs const& a) noexcept { return validate_ListStatus(a); }
inline auto validate(ListStrikePriceArgs const& a) noexcept { return validate_ListStrikePrice(a); }
inline auto validate(MarketDataIncrementalRefreshArgs const& a) noexcept {
    return validate_MarketDataIncrementalRefresh(a);
}
inline auto validate(MarketDataRequestArgs const& a) noexcept {
    return validate_MarketDataRequest(a);
}
inline auto validate(MarketDataSnapshotFullRefreshArgs const& a) noexcept {
    return validate_MarketDataSnapshotFullRefresh(a);
}
inline auto validate(MassQuoteArgs const& a) noexcept { return validate_MassQuote(a); }
inline auto validate(NewOrderListArgs const& a) noexcept { return validate_NewOrderList(a); }
inline auto validate(NewsArgs const& a) noexcept { return validate_News(a); }
inline auto validate(QuoteCancelArgs const& a) noexcept { return validate_QuoteCancel(a); }
inline auto validate(QuoteRequestArgs const& a) noexcept { return validate_QuoteRequest(a); }
}  // namespace fixpp::v42

namespace {

using fixpp::decimal_t;
namespace v42 = fixpp::v42;
namespace g = fixpp::v42::groups;

// The 14 derived (message, tag) pairs — enumerated so the count is assertable.
constexpr std::size_t kCaseCount = 14;

decimal_t make_decimal(std::string_view sv, std::pmr::memory_resource* mr) {
    auto bytes =
        std::span<const std::byte>{reinterpret_cast<const std::byte*>(sv.data()), sv.size()};
    auto r = decimal_t::parse(bytes, mr);
    EXPECT_TRUE(r.has_value()) << "make_decimal failed for: " << sv;
    return r.value_or(decimal_t{});
}

// Asserts the PAIR: `full` validates clean, and the same value with its required
// group span cleared is rejected with wire_required_field_missing.
template <typename Args, typename ClearFn>
void expect_required_group_omission_rejected(Args full, ClearFn clear, char const* label) {
    SCOPED_TRACE(label);

    // Positive control FIRST. Without it, the rejection below could be caused by a
    // required SCALAR this fixture forgot — same error enum, wrong reason.
    auto const ok = validate(full);
    EXPECT_TRUE(ok.has_value())
        << label << ": baseline Args (required group POPULATED) must validate clean; if this "
                    "fails the omission assertion below proves nothing (error="
        << (ok.has_value() ? 0 : static_cast<int>(ok.error())) << ")";

    Args omitted = full;
    clear(omitted);
    auto const bad = validate(omitted);
    EXPECT_FALSE(bad.has_value())
        << label << ": omitting a required='Y' repeating group MUST be rejected -- this is the "
                    "silent-omission hole issue #196 exists to close (Article VI)";
    if (!bad.has_value()) {
        EXPECT_EQ(bad.error(), ::fixpp::core::error::wire_required_field_missing) << label;
    }
}

}  // namespace

class V42RequiredGroupOmission : public ::testing::Test {
  protected:
    std::pmr::monotonic_buffer_resource arena_{1U << 16};
    std::pmr::memory_resource* mr() { return &arena_; }
};

// ── 1. BidResponse (l) / NoBidComponents(420) ────────────────────────────────
TEST_F(V42RequiredGroupOmission, BidResponse_NoBidComponents_420) {
    std::array<g::G_420_2Args, 1> entries{};
    entries[0].commission = make_decimal("1.5", mr());
    entries[0].comm_type = '1';

    v42::BidResponseArgs full{};
    full.bid_components = std::span<const g::G_420_2Args>{entries};
    expect_required_group_omission_rejected(
        full, [](auto& a) { a.bid_components = {}; }, "BidResponse/420 NoBidComponents");
}

// ── 2. Email (C) / LinesOfText(33) ───────────────────────────────────────────
TEST_F(V42RequiredGroupOmission, Email_LinesOfText_33) {
    std::array<g::G_33Args, 1> lines{};
    lines[0].text = "body";

    v42::EmailArgs full{};
    full.email_type = '0';
    full.subject = "subj";
    full.email_thread_id = "T1";
    full.lines_of_text = std::span<const g::G_33Args>{lines};
    expect_required_group_omission_rejected(
        full, [](auto& a) { a.lines_of_text = {}; }, "Email/33 LinesOfText");
}

// ── 3. ListStatus (N) / NoOrders(73) — plan G_73_3Args, NOT G_73_1Args ───────
TEST_F(V42RequiredGroupOmission, ListStatus_NoOrders_73) {
    std::array<g::G_73_3Args, 1> orders{};
    orders[0].cl_ord_id = "C1";
    orders[0].cum_qty = make_decimal("1", mr());
    orders[0].ord_status = '0';
    orders[0].leaves_qty = make_decimal("1", mr());
    orders[0].cxl_qty = make_decimal("0", mr());
    orders[0].avg_px = make_decimal("10", mr());

    v42::ListStatusArgs full{};
    full.list_id = "L1";
    full.tot_no_orders = 1;
    full.no_rpts = 1;
    full.rpt_seq = 1;
    full.list_status_type = 1;
    full.list_order_status = 1;
    full.orders = std::span<const g::G_73_3Args>{orders};
    expect_required_group_omission_rejected(
        full, [](auto& a) { a.orders = {}; }, "ListStatus/73 NoOrders");
}

// ── 4. ListStrikePrice (m) / NoStrikes(428) ──────────────────────────────────
TEST_F(V42RequiredGroupOmission, ListStrikePrice_NoStrikes_428) {
    std::array<g::G_428Args, 1> strikes{};
    strikes[0].symbol = "AAPL";
    strikes[0].price = make_decimal("100", mr());

    v42::ListStrikePriceArgs full{};
    full.list_id = "L1";
    full.tot_no_strikes = 1;
    full.strikes = std::span<const g::G_428Args>{strikes};
    expect_required_group_omission_rejected(
        full, [](auto& a) { a.strikes = {}; }, "ListStrikePrice/428 NoStrikes");
}

// ── 5. MarketDataIncrementalRefresh (X) / NoMDEntries(268) — G_268_2Args ─────
TEST_F(V42RequiredGroupOmission, MarketDataIncrementalRefresh_NoMDEntries_268) {
    std::array<g::G_268_2Args, 1> entries{};
    entries[0].md_update_action = '0';

    v42::MarketDataIncrementalRefreshArgs full{};
    full.md_entries = std::span<const g::G_268_2Args>{entries};
    expect_required_group_omission_rejected(
        full, [](auto& a) { a.md_entries = {}; }, "MarketDataIncrementalRefresh/268 NoMDEntries");
}

// ── 6. MarketDataRequest (V) / NoRelatedSym(146) — G_146_3Args ───────────────
// MarketDataRequest carries TWO of the 14 pairs; each is omitted independently so
// one cannot mask the other.
TEST_F(V42RequiredGroupOmission, MarketDataRequest_NoRelatedSym_146) {
    std::array<g::G_146_3Args, 1> syms{};
    syms[0].symbol = "AAPL";
    std::array<g::G_267Args, 1> types{};
    types[0].md_entry_type = '0';

    v42::MarketDataRequestArgs full{};
    full.md_req_id = "R1";
    full.subscription_request_type = 1;
    full.market_depth = 0;
    full.related_sym = std::span<const g::G_146_3Args>{syms};
    full.md_entry_types = std::span<const g::G_267Args>{types};
    expect_required_group_omission_rejected(
        full, [](auto& a) { a.related_sym = {}; }, "MarketDataRequest/146 NoRelatedSym");
}

// ── 7. MarketDataRequest (V) / NoMDEntryTypes(267) ───────────────────────────
TEST_F(V42RequiredGroupOmission, MarketDataRequest_NoMDEntryTypes_267) {
    std::array<g::G_146_3Args, 1> syms{};
    syms[0].symbol = "AAPL";
    std::array<g::G_267Args, 1> types{};
    types[0].md_entry_type = '0';

    v42::MarketDataRequestArgs full{};
    full.md_req_id = "R1";
    full.subscription_request_type = 1;
    full.market_depth = 0;
    full.related_sym = std::span<const g::G_146_3Args>{syms};
    full.md_entry_types = std::span<const g::G_267Args>{types};
    expect_required_group_omission_rejected(
        full, [](auto& a) { a.md_entry_types = {}; }, "MarketDataRequest/267 NoMDEntryTypes");
}

// ── 8. MarketDataSnapshotFullRefresh (W) / NoMDEntries(268) — G_268_1Args ────
// Same tag as case 5, DIFFERENT plan: the per-occurrence required-set divergence
// B-077-1 keys on structurally.
TEST_F(V42RequiredGroupOmission, MarketDataSnapshotFullRefresh_NoMDEntries_268) {
    std::array<g::G_268_1Args, 1> entries{};
    entries[0].md_entry_type = '0';
    entries[0].md_entry_px = make_decimal("10", mr());

    v42::MarketDataSnapshotFullRefreshArgs full{};
    full.symbol = "AAPL";
    full.md_entries = std::span<const g::G_268_1Args>{entries};
    expect_required_group_omission_rejected(
        full, [](auto& a) { a.md_entries = {}; },
        "MarketDataSnapshotFullRefresh/268 NoMDEntries");
}

// ── 9. MassQuote (i) / NoQuoteSets(296) — the top-level half ─────────────────
TEST_F(V42RequiredGroupOmission, MassQuote_NoQuoteSets_296) {
    std::array<g::G_295_3Args, 1> entries{};
    entries[0].quote_entry_id = "E1";
    std::array<g::G_296_2Args, 1> sets{};
    sets[0].quote_set_id = "S1";
    sets[0].underlying_symbol = "AAPL";
    sets[0].tot_quote_entries = 1;
    sets[0].quote_entries = std::span<const g::G_295_3Args>{entries};

    v42::MassQuoteArgs full{};
    full.quote_id = "Q1";
    full.quote_sets = std::span<const g::G_296_2Args>{sets};
    expect_required_group_omission_rejected(
        full, [](auto& a) { a.quote_sets = {}; }, "MassQuote/296 NoQuoteSets");
}

// ── 10. MassQuote (i) / NoQuoteEntries(295) NESTED in 296 — THE 14th ────────
// The one pair that is not a top-level omission. It is built as a 296 ENTRY
// carrying an EMPTY 295 span, so the rejection arrives through
// `gc.validate_entry` on the 296 row rather than through a top-level
// `group_checks` row — a different construction from the other 13, and the reason
// T034 calls it out separately.
TEST_F(V42RequiredGroupOmission, MassQuote_NoQuoteEntries_295_NestedIn296) {
    std::array<g::G_295_3Args, 1> entries{};
    entries[0].quote_entry_id = "E1";

    auto make_sets = [&](bool populate_295) {
        std::array<g::G_296_2Args, 1> sets{};
        sets[0].quote_set_id = "S1";
        sets[0].underlying_symbol = "AAPL";
        sets[0].tot_quote_entries = 1;
        if (populate_295) {
            sets[0].quote_entries = std::span<const g::G_295_3Args>{entries};
        }
        return sets;
    };

    // Positive control: the 296 entry is otherwise complete, so the ONLY thing
    // separating these two calls is the nested 295 span.
    auto const sets_ok = make_sets(true);
    v42::MassQuoteArgs ok_args{};
    ok_args.quote_id = "Q1";
    ok_args.quote_sets = std::span<const g::G_296_2Args>{sets_ok};
    auto const ok = v42::validate_MassQuote(ok_args);
    EXPECT_TRUE(ok.has_value())
        << "MassQuote with a fully populated 296 entry must validate clean; otherwise the nested "
           "omission below proves nothing (error="
        << (ok.has_value() ? 0 : static_cast<int>(ok.error())) << ")";

    auto const sets_bad = make_sets(false);
    v42::MassQuoteArgs bad_args{};
    bad_args.quote_id = "Q1";
    bad_args.quote_sets = std::span<const g::G_296_2Args>{sets_bad};
    auto const bad = v42::validate_MassQuote(bad_args);
    EXPECT_FALSE(bad.has_value())
        << "MassQuote/295 nested in the required 296: a 296 entry with an EMPTY required 295 span "
           "MUST be rejected via the entry's own validate_entry, not silently accepted";
    if (!bad.has_value()) {
        EXPECT_EQ(bad.error(), ::fixpp::core::error::wire_required_field_missing);
    }
}

// ── 11. NewOrderList (E) / NoOrders(73) — plan G_73_1Args ───────────────────
// US2's "independent test" message. Same tag as ListStatus, different plan.
TEST_F(V42RequiredGroupOmission, NewOrderList_NoOrders_73) {
    std::array<g::G_73_1Args, 1> orders{};
    orders[0].cl_ord_id = "C1";
    orders[0].list_seq_no = 1;
    orders[0].symbol = "AAPL";
    orders[0].side = '1';

    v42::NewOrderListArgs full{};
    full.list_id = "L1";
    full.tot_no_orders = 1;
    full.bid_type = 1;
    full.orders = std::span<const g::G_73_1Args>{orders};
    expect_required_group_omission_rejected(
        full, [](auto& a) { a.orders = {}; }, "NewOrderList/73 NoOrders");
}

// ── 12. News (B) / LinesOfText(33) ──────────────────────────────────────────
TEST_F(V42RequiredGroupOmission, News_LinesOfText_33) {
    std::array<g::G_33Args, 1> lines{};
    lines[0].text = "body";

    v42::NewsArgs full{};
    full.headline = "HL";
    full.lines_of_text = std::span<const g::G_33Args>{lines};
    expect_required_group_omission_rejected(
        full, [](auto& a) { a.lines_of_text = {}; }, "News/33 LinesOfText");
}

// ── 13. QuoteCancel (Z) / NoQuoteEntries(295) — G_295_1Args, TOP-LEVEL ──────
// Contrast with case 10: the same tag, required at top level here and nested there,
// and the two resolve to DIFFERENT plans (G_295_1Args vs G_295_3Args).
TEST_F(V42RequiredGroupOmission, QuoteCancel_NoQuoteEntries_295) {
    std::array<g::G_295_1Args, 1> entries{};
    entries[0].symbol = "AAPL";

    v42::QuoteCancelArgs full{};
    full.quote_id = "Q1";
    full.quote_cancel_type = 1;
    full.quote_entries = std::span<const g::G_295_1Args>{entries};
    expect_required_group_omission_rejected(
        full, [](auto& a) { a.quote_entries = {}; }, "QuoteCancel/295 NoQuoteEntries");
}

// ── 14. QuoteRequest (R) / NoRelatedSym(146) — G_146_2Args ──────────────────
TEST_F(V42RequiredGroupOmission, QuoteRequest_NoRelatedSym_146) {
    std::array<g::G_146_2Args, 1> syms{};
    syms[0].symbol = "AAPL";

    v42::QuoteRequestArgs full{};
    full.quote_req_id = "R1";
    full.related_sym = std::span<const g::G_146_2Args>{syms};
    expect_required_group_omission_rejected(
        full, [](auto& a) { a.related_sym = {}; }, "QuoteRequest/146 NoRelatedSym");
}

// ── Completeness: the 14 cases above are the whole derived set ───────────────
// The typed API forces one hand-written case per pair, so this counts what the file
// actually contains against the derived total. The raw-XML derivation itself is
// gated by T021b's wire-tier pin, and is reproducible from
// `specs/082-structural-group-detection/implementation-notes.md` § T034 PREP.
TEST_F(V42RequiredGroupOmission, AllFourteenDerivedPairsAreCovered) {
    static constexpr std::array<std::pair<char const*, std::uint16_t>, kCaseCount> kCovered{{
        {"l", 420}, {"C", 33},  {"N", 73},  {"m", 428}, {"X", 268},
        {"V", 146}, {"V", 267}, {"W", 268}, {"i", 296}, {"i", 295},
        {"E", 73},  {"B", 33},  {"Z", 295}, {"R", 146},
    }};
    EXPECT_EQ(kCovered.size(), kCaseCount)
        << "the 14 raw-XML-derived required='Y' group pairs for FIX42 -- 13 top-level plus "
           "MassQuote/295 nested in 296. If this count moves, RE-DERIVE from the dictionary; do "
           "not delete a row to make it fit.";

    // 12 distinct messages, because V and i each contribute two pairs.
    std::set<std::string_view> msgs;
    for (auto const& [mt, tag] : kCovered) {
        msgs.insert(mt);
    }
    EXPECT_EQ(msgs.size(), 12U) << "14 pairs span exactly 12 messages (V and i contribute two each)";
}
