// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_exemplar_build_failclosed.cpp
//
// 061-typed-app-messages (061-slim) T016 — the named home for the INV-4
// (atomicity) + INV-5 (group grammar) + AC-2 (input-validation) NEGATIVE
// witnesses across all 5 exemplar builders (contracts/builder-shape-oracle.md
// §C4): build_new_order_single (D), build_execution_report (8),
// build_order_cancel_reject (9), build_new_order_list (E),
// build_allocation_report (AS).
//
// Scope split (do not duplicate):
//   - wire::body_builder's own INV-5 group-grammar negatives (empty group
//     instance, wrong-delimiter-first, group-still-open, framing-tag reject,
//     over-cap / undersized-out untouched) are ALREADY pinned at the
//     body_builder layer in tests/wire/test_body_builder.cpp:
//       EmptyInstance_Reject, WrongDelimiterFirst_Reject, GroupStillOpen_Reject,
//       Inv2_FramingTagRejected, OverCap_BufferUntouched,
//       UndersizedOut_BufferUntouched.
//     None of the 5 exemplar builders can construct a malformed group tree
//     through their public API (each always closes every group it opens via
//     group_end()), so those legs are NOT reachable through this file's
//     surface and are cited, not duplicated, here.
//   - THIS file focuses on the EXEMPLAR-BUILDER input-validation negatives:
//     the per-exemplar hand-validation in src/session/business_messages.cpp
//     (empty required string, out-of-range side, malformed UTCTimestamp) PLUS
//     the shared body_builder-level value guards every exemplar's field()/
//     set_string()/set_decimal() calls route through (SOH/control-byte
//     rejection, unformattable-decimal rejection, undersized-out rejection) —
//     wire::body_builder::append_string_field/append_decimal_field
//     (src/wire/body_builder.cpp).
//   - D/8 already carry a large negative suite in
//     tests/session/test_business_messages_build.cpp (empty-string, SOH-
//     injection, out-of-range side, malformed timestamp, undersized buffer).
//     This file's D/8 cells are kept minimal (one witness per guard, to prove
//     the SAME guards hold post-061-refactor onto body_builder) and its
//     weight is on 9/E/AS, which had ZERO negative-build coverage before this
//     file.
//
// Each TEST asserts BOTH:
//   (a) EXPECT_FALSE(r.has_value()) — typed fail-closed error, and
//   (b) `out` is byte-for-byte unchanged from a 0xAB sentinel prefill
//       (INV-4 atomicity: wire::body_builder::commit() copies into `out`
//       only on full success — src/wire/body_builder.cpp:391-403 — so ANY
//       failure, at any exemplar validation site or inside body_builder
//       itself, must leave `out` untouched).
//
// Anti-fabrication note (AC-2 / §C4): NOT every guard is reachable through
// every builder's public API. Reachability was verified by grepping the call
// sites in src/session/business_messages.cpp before writing each assertion
// (cited per TEST below):
//   - is_valid_side (business_messages.cpp is_valid_side) is called by
//     build_new_order_single (D) and build_execution_report (8), AND by
//     build_new_order_list (E, business_messages.cpp:279) and
//     build_allocation_report (AS, business_messages.cpp:335) — 9 has no
//     side field at all, so it is not exercised. All four side-bearing
//     exemplars (D/8/E/AS) hand-validate the '1'/'2' domain before the
//     value ever reaches body_builder's printable/non-control guard; an
//     out-of-range-but-printable side char (e.g. '9') is rejected on every
//     one of them — see FailClosed_OutOfRangeSide below.
//   - is_valid_utc_timestamp (business_messages.cpp, called at line 139) is
//     used ONLY by build_new_order_single — no other exemplar has a
//     UTCTimestamp-typed field. FailClosed_MalformedTimestamp therefore
//     exercises D only.
//
// Anchors: specs/061-typed-app-messages/contracts/builder-shape-oracle.md C4;
//          specs/061-typed-app-messages/data-model.md §1/§2 (INV-2/3/4/5);
//          src/session/business_messages.cpp (per-exemplar hand-validation);
//          src/wire/body_builder.cpp (shared append_string_field/
//          append_decimal_field guards + commit() atomicity).

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <fixpp/core/decimal_alias.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/session/business_messages.hpp>
#include <memory_resource>
#include <span>
#include <string_view>

namespace {

using fixpp::decimal_t;
using fixpp::session::AllocationReportParams;
using fixpp::session::AllocationReportParty;
using fixpp::session::NewOrderListOrder;
using fixpp::session::NewOrderListParams;
using fixpp::session::NewOrderListParty;

constexpr std::byte kSentinel{0xABU};

// Parse a decimal_t from an ASCII literal (test_business_messages_build.cpp /
// test_exemplar_roundtrip.cpp precedent).
decimal_t make_decimal(std::string_view sv, std::pmr::memory_resource* mr) {
    auto bytes =
        std::span<const std::byte>{reinterpret_cast<const std::byte*>(sv.data()), sv.size()};
    auto r = decimal_t::parse(bytes, mr);
    EXPECT_TRUE(r.has_value()) << "make_decimal failed for: " << sv;
    return r.value_or(decimal_t{});
}

// The invalid-mantissa sentinel (fixpp::core::pod_decimal_invalid): the only
// value that trips decimal_traits<pod_decimal>::to_chars's AC-S1 sentinel
// check (src/core/decimal.cpp:143-145), returning decimal_invalid_input.
decimal_t make_invalid_decimal() { return decimal_t{fixpp::core::pod_decimal_invalid}; }

// INV-4 atomicity: assert every byte of `buf` is still the 0xAB sentinel.
template <std::size_t N>
void assert_unchanged(const std::array<std::byte, N>& buf, const char* label) {
    SCOPED_TRACE(label);
    for (std::size_t i = 0; i < buf.size(); ++i) {
        if (buf[i] != kSentinel) {
            ADD_FAILURE() << "buf[" << i << "] modified on a fail-closed path (expected 0xAB, got 0x"
                          << std::hex << static_cast<unsigned>(buf[i]) << ")";
            return;
        }
    }
}

constexpr std::size_t kBufSize = 1024;

}  // namespace

// ── AC-2 case 1: empty required string ───────────────────────────────────────
// Every exemplar's leading required-string check (business_messages.cpp:
// D 122-123, 8 171-173, 9 297-300, E 225-226, AS 320-325).
TEST(ExemplarBuildFailClosed, FailClosed_EmptyRequiredString) {
    std::pmr::monotonic_buffer_resource arena{4096};
    auto qty = make_decimal("10", &arena);
    auto px = make_decimal("50", &arena);
    auto zero = make_decimal("0", &arena);

    std::array<std::byte, kBufSize> buf{};

    // D: empty cl_ord_id.
    buf.fill(kSentinel);
    EXPECT_FALSE(fixpp::session::build_new_order_single(std::span<std::byte>{buf}, "", "MSFT", '1',
                                                          qty, px, "20240101-10:00:00")
                     .has_value())
        << "D: empty cl_ord_id must fail-closed";
    assert_unchanged(buf, "D empty cl_ord_id");

    // 8: empty order_id.
    buf.fill(kSentinel);
    EXPECT_FALSE(fixpp::session::build_execution_report(std::span<std::byte>{buf}, "", "EXEC1", 'F',
                                                          '2', "MSFT", '1', zero, qty, px)
                     .has_value())
        << "8: empty order_id must fail-closed";
    assert_unchanged(buf, "8 empty order_id");

    // 9: empty order_id (the FIRST required-string check, business_messages.cpp:297).
    buf.fill(kSentinel);
    EXPECT_FALSE(fixpp::session::build_order_cancel_reject(std::span<std::byte>{buf}, "", "CLORD1",
                                                             "CLORD0", '8', '1', 0)
                     .has_value())
        << "9: empty order_id must fail-closed";
    assert_unchanged(buf, "9 empty order_id");

    // E: empty list_id. orders left empty too (list_id is checked first, so
    // this alone determines the outcome — business_messages.cpp:225).
    buf.fill(kSentinel);
    NewOrderListParams e_params{"", 0, 0, std::span<const NewOrderListOrder>{}};
    EXPECT_FALSE(fixpp::session::build_new_order_list(std::span<std::byte>{buf}, e_params).has_value())
        << "E: empty list_id must fail-closed";
    assert_unchanged(buf, "E empty list_id");

    // AS: empty alloc_report_id (the FIRST required-string check,
    // business_messages.cpp:320). trade_date/symbol left non-empty so this
    // check alone determines the outcome.
    buf.fill(kSentinel);
    AllocationReportParams as_params{
        "",   '0', 9, 0, 0, '1', qty, px, "20240101", "MSFT", std::span<const AllocationReportParty>{}};
    EXPECT_FALSE(
        fixpp::session::build_allocation_report(std::span<std::byte>{buf}, as_params).has_value())
        << "AS: empty alloc_report_id must fail-closed";
    assert_unchanged(buf, "AS empty alloc_report_id");
}

// ── AC-2 case 1b: LATER required-field guards ────────────────────────────────
// FailClosed_EmptyRequiredString above exercises each message's FIRST required
// check (which short-circuits). This pins the SUBSEQUENT guards, each reached
// only when every earlier required field is valid: E `orders.empty()`
// (business_messages.cpp:227-228), 9 `orig_cl_ord_id` (:302-303), AS
// `trade_date` (:325-326) and AS `symbol` (:327-328).
TEST(ExemplarBuildFailClosed, FailClosed_EmptyRequiredString_LaterFields) {
    std::pmr::monotonic_buffer_resource arena{4096};
    auto qty = make_decimal("10", &arena);
    auto px = make_decimal("50", &arena);

    std::array<std::byte, kBufSize> buf{};

    // E: valid list_id, EMPTY orders span -> the second required guard fires.
    buf.fill(kSentinel);
    NewOrderListParams e_no_orders{"LIST1", 0, 0, std::span<const NewOrderListOrder>{}};
    EXPECT_FALSE(
        fixpp::session::build_new_order_list(std::span<std::byte>{buf}, e_no_orders).has_value())
        << "E: empty orders must fail-closed";
    assert_unchanged(buf, "E empty orders");

    // 9: valid order_id + cl_ord_id, EMPTY orig_cl_ord_id -> the third guard fires.
    buf.fill(kSentinel);
    EXPECT_FALSE(fixpp::session::build_order_cancel_reject(std::span<std::byte>{buf}, "ORD1",
                                                             "CLORD1", "", '8', '1', 0)
                     .has_value())
        << "9: empty orig_cl_ord_id must fail-closed";
    assert_unchanged(buf, "9 empty orig_cl_ord_id");

    // AS: valid alloc_report_id + symbol, EMPTY trade_date -> the second guard fires.
    buf.fill(kSentinel);
    AllocationReportParams as_no_td{
        "RPT1", '0', 9, 0, 0, '1', qty, px, "", "MSFT", std::span<const AllocationReportParty>{}};
    EXPECT_FALSE(
        fixpp::session::build_allocation_report(std::span<std::byte>{buf}, as_no_td).has_value())
        << "AS: empty trade_date must fail-closed";
    assert_unchanged(buf, "AS empty trade_date");

    // AS: valid alloc_report_id + trade_date, EMPTY symbol -> the third guard fires.
    buf.fill(kSentinel);
    AllocationReportParams as_no_sym{
        "RPT1", '0', 9, 0, 0, '1', qty, px, "20240101", "", std::span<const AllocationReportParty>{}};
    EXPECT_FALSE(
        fixpp::session::build_allocation_report(std::span<std::byte>{buf}, as_no_sym).has_value())
        << "AS: empty symbol must fail-closed";
    assert_unchanged(buf, "AS empty symbol");
}

// ── AC-2 case 2: control byte / SOH in a value ────────────────────────────────
// The shared guard (is_clean_field_value) lives in wire::body_builder::
// append_string_field (src/wire/body_builder.cpp:72-74, invoked from field()/
// set_string() — src/wire/body_builder.cpp:172-178,295-297), so EVERY exemplar
// that emits the tainted field through body_builder rejects it, regardless of
// whether the exemplar ALSO hand-validates it (D/8 hand-validate too —
// business_messages.cpp:129-132,178-183 — already witnessed extensively in
// test_business_messages_build.cpp; 9/E/AS have no hand-validation of their
// own and rely entirely on this shared body_builder guard, which is the new
// coverage this file adds).
TEST(ExemplarBuildFailClosed, FailClosed_SohInValue) {
    std::pmr::monotonic_buffer_resource arena{4096};
    auto qty = make_decimal("10", &arena);
    auto px = make_decimal("50", &arena);

    std::array<std::byte, kBufSize> buf{};

    // 9: order_id containing SOH + session-tag injection attempt.
    buf.fill(kSentinel);
    EXPECT_FALSE(fixpp::session::build_order_cancel_reject(std::span<std::byte>{buf},
                                                             "ORD\x01"
                                                             "49=EVIL",
                                                             "CLORD1", "CLORD0", '8', '1', 0)
                     .has_value())
        << "9: order_id with SOH+49= injection must fail-closed (body_builder guard)";
    assert_unchanged(buf, "9 order_id SOH-inject");

    // E: order.cl_ord_id containing SOH (entry->set_string(11,...) ->
    // append_string_field). list_id valid, one order with empty parties
    // (present-but-empty is a valid, already-witnessed shape).
    buf.fill(kSentinel);
    NewOrderListOrder e_order{
        "ORD\x01"
        "58=x",
        1, '1', "MSFT", make_decimal("10", &arena), std::span<const NewOrderListParty>{}};
    NewOrderListParams e_params{"LIST1", 3, 1, std::span<const NewOrderListOrder>{&e_order, 1}};
    EXPECT_FALSE(fixpp::session::build_new_order_list(std::span<std::byte>{buf}, e_params).has_value())
        << "E: order.cl_ord_id with SOH injection must fail-closed (body_builder guard)";
    assert_unchanged(buf, "E cl_ord_id SOH-inject");

    // AS: symbol containing SOH (bb.field(55, params.symbol) ->
    // append_string_field). alloc_report_id/trade_date valid.
    buf.fill(kSentinel);
    AllocationReportParams as_params{"ALLOCRPT1",
                                      '0',
                                      9,
                                      0,
                                      0,
                                      '1',
                                      qty,
                                      px,
                                      "20240101",
                                      "MSFT\x01"
                                      "58=x",
                                      std::span<const AllocationReportParty>{}};
    EXPECT_FALSE(
        fixpp::session::build_allocation_report(std::span<std::byte>{buf}, as_params).has_value())
        << "AS: symbol with SOH injection must fail-closed (body_builder guard)";
    assert_unchanged(buf, "AS symbol SOH-inject");
}

// ── AC-2 case 3: out-of-range char / side ─────────────────────────────────────
// is_valid_side (business_messages.cpp:73) is called by build_new_order_single
// (D), build_execution_report (8), build_new_order_list (E,
// business_messages.cpp:279), and build_allocation_report (AS,
// business_messages.cpp:335) — 9 has no side field, so it is not exercised.
// A printable-but-out-of-range side (e.g. '9') is rejected on all four
// side-bearing exemplars: is_valid_side runs before the value ever reaches
// body_builder's printable/non-control guard.
TEST(ExemplarBuildFailClosed, FailClosed_OutOfRangeSide) {
    std::pmr::monotonic_buffer_resource arena{4096};
    auto qty = make_decimal("10", &arena);
    auto px = make_decimal("50", &arena);
    auto zero = make_decimal("0", &arena);

    std::array<std::byte, kBufSize> buf{};

    // D: side = '9' (only '1'/'2' accepted).
    buf.fill(kSentinel);
    EXPECT_FALSE(fixpp::session::build_new_order_single(std::span<std::byte>{buf}, "ORD1", "MSFT",
                                                          '9', qty, px, "20240101-10:00:00")
                     .has_value())
        << "D: side '9' must fail-closed";
    assert_unchanged(buf, "D side '9'");

    // 8: side = 'X'.
    buf.fill(kSentinel);
    EXPECT_FALSE(fixpp::session::build_execution_report(std::span<std::byte>{buf}, "ORD1", "EXEC1",
                                                          'F', '2', "MSFT", 'X', zero, qty, px)
                     .has_value())
        << "8: side 'X' must fail-closed";
    assert_unchanged(buf, "8 side 'X'");

    // E: one order valid except side='9' (FR-003 per-exemplar char-domain guard).
    buf.fill(kSentinel);
    const std::array<fixpp::session::NewOrderListOrder, 1> e_orders{
        fixpp::session::NewOrderListOrder{"ORD1", 1, '9', "MSFT", qty,
                                          std::span<const fixpp::session::NewOrderListParty>{}}};
    const fixpp::session::NewOrderListParams e_params{
        "LIST1", 3, 1, std::span<const fixpp::session::NewOrderListOrder>{e_orders}};
    EXPECT_FALSE(fixpp::session::build_new_order_list(std::span<std::byte>{buf}, e_params).has_value())
        << "E: order side '9' must fail-closed";
    assert_unchanged(buf, "E order side '9'");

    // AS: valid except side='9'.
    buf.fill(kSentinel);
    const fixpp::session::AllocationReportParams as_params{
        "ALLOCRPT1", '0', 9, 0, 0, '9', qty, px, "20240101", "MSFT",
        std::span<const fixpp::session::AllocationReportParty>{}};
    EXPECT_FALSE(
        fixpp::session::build_allocation_report(std::span<std::byte>{buf}, as_params).has_value())
        << "AS: side '9' must fail-closed";
    assert_unchanged(buf, "AS side '9'");
}

// ── AC-2 case 4: unformattable / invalid decimal ──────────────────────────────
// decimal_traits<pod_decimal>::to_chars's AC-S1 sentinel check
// (src/core/decimal.cpp:143-145) is reached from body_builder::
// append_decimal_field (src/wire/body_builder.cpp:150-168), invoked by every
// exemplar's field(tag, decimal_t)/set_decimal() calls: D (order_qty/price),
// 8 (leaves_qty/cum_qty/avg_px), E (order.order_qty), AS (quantity/avg_px).
// 9 has no decimal-typed field — not exercised.
TEST(ExemplarBuildFailClosed, FailClosed_UnformattableDecimal) {
    std::pmr::monotonic_buffer_resource arena{4096};
    auto qty = make_decimal("10", &arena);
    auto px = make_decimal("50", &arena);
    auto invalid = make_invalid_decimal();

    std::array<std::byte, kBufSize> buf{};

    // D: order_qty = invalid sentinel.
    buf.fill(kSentinel);
    EXPECT_FALSE(fixpp::session::build_new_order_single(std::span<std::byte>{buf}, "ORD1", "MSFT",
                                                          '1', invalid, px, "20240101-10:00:00")
                     .has_value())
        << "D: unformattable order_qty must fail-closed with decimal_invalid_input";
    assert_unchanged(buf, "D invalid order_qty");

    // 8: avg_px = invalid sentinel (the first field() call in build_execution_
    // report is field(6, avg_px) — business_messages.cpp:198).
    buf.fill(kSentinel);
    auto er = fixpp::session::build_execution_report(std::span<std::byte>{buf}, "ORD1", "EXEC1", 'F',
                                                       '2', "MSFT", '1', qty, qty, invalid);
    EXPECT_FALSE(er.has_value())
        << "8: unformattable avg_px must fail-closed with decimal_invalid_input";
    assert_unchanged(buf, "8 invalid avg_px");

    // E: order.order_qty = invalid sentinel.
    buf.fill(kSentinel);
    NewOrderListOrder e_order{"ORD1", 1, '1', "MSFT", invalid, std::span<const NewOrderListParty>{}};
    NewOrderListParams e_params{"LIST1", 3, 1, std::span<const NewOrderListOrder>{&e_order, 1}};
    EXPECT_FALSE(fixpp::session::build_new_order_list(std::span<std::byte>{buf}, e_params).has_value())
        << "E: unformattable order.order_qty must fail-closed with decimal_invalid_input";
    assert_unchanged(buf, "E invalid order_qty");

    // AS: avg_px = invalid sentinel (the first field() call in
    // build_allocation_report is field(6, avg_px) — business_messages.cpp:329).
    buf.fill(kSentinel);
    AllocationReportParams as_params{"ALLOCRPT1",         '0', 9,     0,   0,   '1',
                                      qty,                 invalid, "20240101", "MSFT",
                                      std::span<const AllocationReportParty>{}};
    EXPECT_FALSE(
        fixpp::session::build_allocation_report(std::span<std::byte>{buf}, as_params).has_value())
        << "AS: unformattable avg_px must fail-closed with decimal_invalid_input";
    assert_unchanged(buf, "AS invalid avg_px");
}

// ── AC-2 case 5: malformed UTCTimestamp (FR-008) ──────────────────────────────
// is_valid_utc_timestamp (business_messages.cpp:78) is called ONLY from
// build_new_order_single (line 139) — the sole exemplar with a
// UTCTimestamp-typed field. Not reachable via 8/9/E/AS.
TEST(ExemplarBuildFailClosed, FailClosed_MalformedTimestamp) {
    std::pmr::monotonic_buffer_resource arena{4096};
    auto qty = make_decimal("10", &arena);
    auto px = make_decimal("50", &arena);

    std::array<std::byte, kBufSize> buf{};

    // Wrong-length (16 chars, not 17 or 21).
    buf.fill(kSentinel);
    EXPECT_FALSE(fixpp::session::build_new_order_single(std::span<std::byte>{buf}, "ORD1", "MSFT",
                                                          '1', qty, px, "20240101-10:00:0")
                     .has_value())
        << "D: 16-char transact_time must fail-closed";
    assert_unchanged(buf, "D 16-char transact_time");

    // Wrong-shape (dash not at position 8).
    buf.fill(kSentinel);
    EXPECT_FALSE(fixpp::session::build_new_order_single(std::span<std::byte>{buf}, "ORD1", "MSFT",
                                                          '1', qty, px, "20240601X09:30:00")
                     .has_value())
        << "D: non-dash-at-8 transact_time must fail-closed";
    assert_unchanged(buf, "D wrong-shape transact_time");
}

// ── AC-2 case 6: undersized output buffer (INV-4) ─────────────────────────────
// out.size() < total is the LAST check in wire::body_builder::commit()
// (src/wire/body_builder.cpp:389) — reached identically by all 5 exemplars,
// each of which routes its final assembly through bb.commit(out).
TEST(ExemplarBuildFailClosed, FailClosed_UndersizedBuffer_Untouched) {
    std::pmr::monotonic_buffer_resource arena{4096};
    auto qty = make_decimal("10", &arena);
    auto px = make_decimal("50", &arena);
    auto zero = make_decimal("0", &arena);

    std::array<std::byte, 5> tiny{};

    // D.
    tiny.fill(kSentinel);
    EXPECT_FALSE(fixpp::session::build_new_order_single(std::span<std::byte>{tiny}, "ORD1", "MSFT",
                                                          '1', qty, px, "20240101-10:00:00")
                     .has_value())
        << "D: undersized out must fail-closed with wire_frame_too_large";
    assert_unchanged(tiny, "D undersized out");

    // 8.
    tiny.fill(kSentinel);
    EXPECT_FALSE(fixpp::session::build_execution_report(std::span<std::byte>{tiny}, "ORD1", "EXEC1",
                                                          'F', '2', "MSFT", '1', zero, qty, px)
                     .has_value())
        << "8: undersized out must fail-closed with wire_frame_too_large";
    assert_unchanged(tiny, "8 undersized out");

    // 9.
    tiny.fill(kSentinel);
    EXPECT_FALSE(fixpp::session::build_order_cancel_reject(std::span<std::byte>{tiny}, "ORD1",
                                                             "CLORD1", "CLORD0", '8', '1', 0)
                     .has_value())
        << "9: undersized out must fail-closed with wire_frame_too_large";
    assert_unchanged(tiny, "9 undersized out");

    // E.
    tiny.fill(kSentinel);
    NewOrderListOrder e_order{"ORD1", 1, '1', "MSFT", qty, std::span<const NewOrderListParty>{}};
    NewOrderListParams e_params{"LIST1", 3, 1, std::span<const NewOrderListOrder>{&e_order, 1}};
    EXPECT_FALSE(fixpp::session::build_new_order_list(std::span<std::byte>{tiny}, e_params).has_value())
        << "E: undersized out must fail-closed with wire_frame_too_large";
    assert_unchanged(tiny, "E undersized out");

    // AS.
    tiny.fill(kSentinel);
    AllocationReportParams as_params{"ALLOCRPT1",
                                      '0',
                                      9,
                                      0,
                                      0,
                                      '1',
                                      qty,
                                      px,
                                      "20240101",
                                      "MSFT",
                                      std::span<const AllocationReportParty>{}};
    EXPECT_FALSE(
        fixpp::session::build_allocation_report(std::span<std::byte>{tiny}, as_params).has_value())
        << "AS: undersized out must fail-closed with wire_frame_too_large";
    assert_unchanged(tiny, "AS undersized out");
}
