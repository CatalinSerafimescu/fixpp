// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/session/business_messages.cpp
//
// 020-g2-business-messages T008 — builder implementations.
// 061-typed-app-messages (061-slim) T013/T014 — build_new_order_single and
// build_execution_report refactored onto fixpp::wire::body_builder.
//
// Five typed FIX 4.4 application-body builders:
//   build_new_order_single  — 35=D (NewOrderSingle, Limit-only, A-001)
//   build_execution_report  — 35=8 (ExecutionReport, fully-filled, A-006)
//   build_new_order_list, build_order_cancel_reject, build_allocation_report
//     — 35=E/9/AS (061-typed-app-messages exemplars)
//
// Design (contracts/business-messages.md + data-model.md E1/E2 + INV-2/3/4;
// 061 data-model.md §1/§2):
//   - Emit the app BODY ONLY: lead with "35=MsgType\x01", then business fields.
//   - NO session header tags (8/9/34/49/52/56) and NO "10=" trailer (INV-2).
//     These are engine-stamped — this is intentional and differs from the admin
//     builders (which use wire::Writer and produce complete frames).
//   - Built on fixpp::wire::body_builder (include/fixpp/wire/body_builder.hpp):
//     accumulates fields internally (own zero-global-heap arena) and copies
//     into caller `out` only on full success at commit() (INV-4 atomicity).
//     On any failure, returns std::unexpected and does NOT touch `out`.
//   - Per-exemplar hand-validation (empty required string, SOH/control-byte
//     injection, out-of-range side/enum char, ill-formed UTCTimestamp) runs
//     BEFORE body_builder.field() calls (FR-003) — body_builder itself has no
//     dictionary/semantic knowledge of individual tags.
//   - Numeric fields: decimal_t::format(span) for canonical serialisation
//     (FR-007/INV-3, locale-independent, no scientific notation), applied by
//     body_builder at field()/commit() time.
//   - Field order for D/8/9/AS is the QuickFIX-golden ascending-tag order
//     (tests/session/golden/*.fix, PROVENANCE.md); E's is FIX44 dictionary
//     order within each group entry (see build_new_order_list below).
//
// Wire::Writer is NOT used here (nor by body_builder). Writer always injects
// "8=BeginString\x01 9=...\x01" on the first append_raw call and appends
// "10=CheckSum\x01" at commit(), so it has no body-only mode.
//
// Anchors: contracts/business-messages.md; data-model.md E1/E2; research.md D3/D4;
//          spec FR-001..008; INV-2/3/4; specs/061-typed-app-messages/data-model.md §1/§2.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/error.hpp>
#include <fixpp/session/business_messages.hpp>
#include <fixpp/wire/body_builder.hpp>
#include <span>
#include <string_view>

namespace fixpp::session {

namespace {

// Validate that a char is a printable non-control ASCII character (0x20..0x7E).
// Used for exec_type and ord_status to reject NUL/control bytes.
[[nodiscard]] bool is_printable(char c) noexcept {
    return static_cast<unsigned char>(c) >= 0x20U && static_cast<unsigned char>(c) <= 0x7EU;
}

// Validate that every byte of a string field value is a printable non-control
// ASCII character (0x20..0x7E). Rejects SOH (0x01), NUL (0x00), DEL (0x7F),
// and all other control bytes. This prevents SOH injection via string fields
// (INV-2 / FR-004): a value containing SOH would forge a field boundary inside
// the body, allowing injection of arbitrary FIX tags after the SOH.
// [RC#1: gate-b/r1 business_messages.cpp control-byte sanitization]
[[nodiscard]] bool is_clean_field_value(std::string_view sv) noexcept {
    return std::ranges::all_of(sv, is_printable);
}

// Validate side: only '1' (Buy) or '2' (Sell) are accepted.
[[nodiscard]] bool is_valid_side(char c) noexcept { return c == '1' || c == '2'; }

// Validate UTCTimestamp shape (FR-008).
// Accepts exactly 17 chars ("YYYYMMDD-HH:MM:SS") or 21 chars
// ("YYYYMMDD-HH:MM:SS.sss"). Checks digit/separator positions.
[[nodiscard]] bool is_valid_utc_timestamp(std::string_view sv) noexcept {
    if (sv.size() != 17 && sv.size() != 21) return false;

    // Positions: 01234567 8 9 10 11 12 13 14 15 16
    //            YYYYMMDD - HH :  MM :  SS
    // Position 8 must be '-'
    if (sv[8] != '-') return false;
    // Position 11 must be ':'
    if (sv[11] != ':') return false;
    // Position 14 must be ':'
    if (sv[14] != ':') return false;

    // Digits at positions 0..7, 9..10, 12..13, 15..16
    auto is_digit = [](char c) { return c >= '0' && c <= '9'; };
    for (int i : {0, 1, 2, 3, 4, 5, 6, 7, 9, 10, 12, 13, 15, 16}) {
        if (!is_digit(sv[static_cast<std::size_t>(i)])) return false;
    }

    // For 21-char form: position 17 must be '.', positions 18..20 must be digits
    if (sv.size() == 21) {
        if (sv[17] != '.') return false;
        if (!is_digit(sv[18]) || !is_digit(sv[19]) || !is_digit(sv[20])) return false;
    }

    return true;
}

}  // namespace

// ── NewOrderSingle (35=D) ────────────────────────────────────────────────────
// Built on fixpp::wire::body_builder (data-model.md §1/§2), T013 refactor.
// Field order matches the QuickFIX-authored golden
// (tests/session/golden/new_order_single.fix — ascending tag order,
// group-free message): 11,38,40,44,54,55,60 (see tasks.md T013 CORRECTION
// note: this supersedes the legacy 020 byte order 11,55,54,38,40,44,60).
//
// INV-2: NO 8/9/34/49/52/56 and NO 10= (those are engine-stamped).
// INV-4: body_builder builds into its own internal scratch; copy to `out`
// only on full success at commit().
[[nodiscard]] fixpp::core::expected_t<std::span<std::byte>> build_new_order_single(
    std::span<std::byte> out, std::string_view cl_ord_id, std::string_view symbol, char side,
    const fixpp::decimal_t& order_qty, const fixpp::decimal_t& price,
    std::string_view transact_time) noexcept {
    // Validate required string fields.
    if (cl_ord_id.empty()) return std::unexpected(fixpp::core::error::wire_required_field_missing);
    if (symbol.empty()) return std::unexpected(fixpp::core::error::wire_required_field_missing);

    // Reject control bytes (incl. SOH) in string fields to prevent SOH injection
    // (INV-2 / FR-004): a SOH inside cl_ord_id or symbol would forge a field
    // boundary inside the body. [RC#1: gate-b/r1] (hand-validated here, per
    // exemplar, BEFORE body_builder.field() — FR-003.)
    if (!is_clean_field_value(cl_ord_id))
        return std::unexpected(fixpp::core::error::wire_field_value_out_of_range);
    if (!is_clean_field_value(symbol))
        return std::unexpected(fixpp::core::error::wire_field_value_out_of_range);

    // Validate side.
    if (!is_valid_side(side))
        return std::unexpected(fixpp::core::error::wire_field_value_out_of_range);

    // Validate transact_time shape (FR-008).
    if (!is_valid_utc_timestamp(transact_time))
        return std::unexpected(fixpp::core::error::wire_invalid_field_format);

    fixpp::wire::body_builder bb{"D"};

    // Ascending-tag order (golden): 11,38,40,44,54,55,60.
    if (auto r = bb.field(11, cl_ord_id); !r.has_value()) return std::unexpected(r.error());
    if (auto r = bb.field(38, order_qty); !r.has_value())
        return std::unexpected(fixpp::core::error::decimal_invalid_input);
    // 40=2 (OrdType fixed Limit)
    if (auto r = bb.field(40, "2"); !r.has_value()) return std::unexpected(r.error());
    if (auto r = bb.field(44, price); !r.has_value())
        return std::unexpected(fixpp::core::error::decimal_invalid_input);
    if (auto r = bb.field(54, side); !r.has_value()) return std::unexpected(r.error());
    if (auto r = bb.field(55, symbol); !r.has_value()) return std::unexpected(r.error());
    if (auto r = bb.field(60, transact_time); !r.has_value()) return std::unexpected(r.error());

    return bb.commit(out);
}

// ── ExecutionReport (35=8) ───────────────────────────────────────────────────
// Built on fixpp::wire::body_builder (data-model.md §1/§2), T014 refactor.
// Field order matches the QuickFIX-authored golden
// (tests/session/golden/execution_report.fix — ascending tag order,
// group-free message): 6,14,17,37,39,54,55,150,151 (see tasks.md T014
// CORRECTION note: this supersedes the legacy 020 byte order
// 37,17,150,39,55,54,151,14,6).
[[nodiscard]] fixpp::core::expected_t<std::span<std::byte>> build_execution_report(
    std::span<std::byte> out, std::string_view order_id, std::string_view exec_id, char exec_type,
    char ord_status, std::string_view symbol, char side, const fixpp::decimal_t& leaves_qty,
    const fixpp::decimal_t& cum_qty, const fixpp::decimal_t& avg_px) noexcept {
    // Validate required string fields.
    if (order_id.empty()) return std::unexpected(fixpp::core::error::wire_required_field_missing);
    if (exec_id.empty()) return std::unexpected(fixpp::core::error::wire_required_field_missing);
    if (symbol.empty()) return std::unexpected(fixpp::core::error::wire_required_field_missing);

    // Reject control bytes (incl. SOH) in string fields to prevent SOH injection
    // (INV-2 / FR-004). [RC#1: gate-b/r1] (hand-validated here, per exemplar,
    // BEFORE body_builder.field() — FR-003.)
    if (!is_clean_field_value(order_id))
        return std::unexpected(fixpp::core::error::wire_field_value_out_of_range);
    if (!is_clean_field_value(exec_id))
        return std::unexpected(fixpp::core::error::wire_field_value_out_of_range);
    if (!is_clean_field_value(symbol))
        return std::unexpected(fixpp::core::error::wire_field_value_out_of_range);

    // Validate enum chars: reject NUL and non-printable/control bytes.
    if (!is_printable(exec_type))
        return std::unexpected(fixpp::core::error::wire_field_value_out_of_range);
    if (!is_printable(ord_status))
        return std::unexpected(fixpp::core::error::wire_field_value_out_of_range);

    // Validate side.
    if (!is_valid_side(side))
        return std::unexpected(fixpp::core::error::wire_field_value_out_of_range);

    fixpp::wire::body_builder bb{"8"};

    // Ascending-tag order (golden): 6,14,17,37,39,54,55,150,151.
    if (auto r = bb.field(6, avg_px); !r.has_value())
        return std::unexpected(fixpp::core::error::decimal_invalid_input);
    if (auto r = bb.field(14, cum_qty); !r.has_value())
        return std::unexpected(fixpp::core::error::decimal_invalid_input);
    if (auto r = bb.field(17, exec_id); !r.has_value()) return std::unexpected(r.error());
    if (auto r = bb.field(37, order_id); !r.has_value()) return std::unexpected(r.error());
    if (auto r = bb.field(39, ord_status); !r.has_value()) return std::unexpected(r.error());
    if (auto r = bb.field(54, side); !r.has_value()) return std::unexpected(r.error());
    if (auto r = bb.field(55, symbol); !r.has_value()) return std::unexpected(r.error());
    if (auto r = bb.field(150, exec_type); !r.has_value()) return std::unexpected(r.error());
    if (auto r = bb.field(151, leaves_qty); !r.has_value())
        return std::unexpected(fixpp::core::error::decimal_invalid_input);

    return bb.commit(out);
}

// ── NewOrderList (35=E) ──────────────────────────────────────────────────────
// Built on fixpp::wire::body_builder (data-model.md §1/§2) — same primitive
// D/8 were refactored onto by T013/T014. Field order matches the
// QuickFIX-authored golden (tests/session/golden/new_order_list.fix): root
// fields in ascending-tag order (66,68,73,394 — BidType(394) lands AFTER the
// NoOrders group content because the group's entries are emitted in place of
// tag 73's ascending position), order-entry fields in FIX44 dictionary order
// (11,67,453,55,54,38 — NOT tag-ascending), party-entry fields (448,447,452,
// 802), sub-entry fields (523,803).
[[nodiscard]] fixpp::core::expected_t<std::span<std::byte>> build_new_order_list(
    std::span<std::byte> out, const NewOrderListParams& params) noexcept {
    if (params.list_id.empty())
        return std::unexpected(fixpp::core::error::wire_required_field_missing);
    if (params.orders.empty())
        return std::unexpected(fixpp::core::error::wire_required_field_missing);

    fixpp::wire::body_builder bb{"E"};

    if (auto r = bb.field(66, params.list_id); !r.has_value()) return std::unexpected(r.error());
    if (auto r = bb.field(68, params.tot_no_orders); !r.has_value())
        return std::unexpected(r.error());

    auto orders_g = bb.group_begin(73, 11);
    if (!orders_g.has_value()) return std::unexpected(orders_g.error());

    for (const auto& order : params.orders) {
        auto entry = orders_g->add_entry();
        if (!entry.has_value()) return std::unexpected(entry.error());
        if (order.cl_ord_id.empty())
            return std::unexpected(fixpp::core::error::wire_required_field_missing);
        if (auto r = entry->set_string(11, order.cl_ord_id); !r.has_value())
            return std::unexpected(r.error());
        if (auto r = entry->set_int(67, order.list_seq_no); !r.has_value())
            return std::unexpected(r.error());

        // NoPartyIDs(453) -> NoPartySubIDs(802): always opened by this
        // builder (an empty `parties` span emits 453=0, present-but-empty —
        // data-model §3.1 "Count-of-zero witness target").
        auto parties_g = entry->group_begin(453, 448);
        if (!parties_g.has_value()) return std::unexpected(parties_g.error());
        for (const auto& party : order.parties) {
            auto pentry = parties_g->add_entry();
            if (!pentry.has_value()) return std::unexpected(pentry.error());
            if (party.party_id.empty())
                return std::unexpected(fixpp::core::error::wire_required_field_missing);
            if (auto r = pentry->set_string(448, party.party_id); !r.has_value())
                return std::unexpected(r.error());
            if (auto r = pentry->set_char(447, party.party_id_source); !r.has_value())
                return std::unexpected(r.error());
            if (auto r = pentry->set_int(452, party.party_role); !r.has_value())
                return std::unexpected(r.error());

            auto subs_g = pentry->group_begin(802, 523);
            if (!subs_g.has_value()) return std::unexpected(subs_g.error());
            for (const auto& sub : party.sub_ids) {
                auto sentry = subs_g->add_entry();
                if (!sentry.has_value()) return std::unexpected(sentry.error());
                if (sub.party_sub_id.empty())
                    return std::unexpected(fixpp::core::error::wire_required_field_missing);
                if (auto r = sentry->set_string(523, sub.party_sub_id); !r.has_value())
                    return std::unexpected(r.error());
                if (auto r = sentry->set_int(803, sub.party_sub_id_type); !r.has_value())
                    return std::unexpected(r.error());
            }
            if (auto r = bb.group_end(*subs_g); !r.has_value()) return std::unexpected(r.error());
        }
        if (auto r = bb.group_end(*parties_g); !r.has_value()) return std::unexpected(r.error());

        if (order.symbol.empty())
            return std::unexpected(fixpp::core::error::wire_required_field_missing);
        if (auto r = entry->set_string(55, order.symbol); !r.has_value())
            return std::unexpected(r.error());
        // Validate side per-exemplar (FR-003 hand-validated char domain, mirrors D/8).
        if (!is_valid_side(order.side))
            return std::unexpected(fixpp::core::error::wire_field_value_out_of_range);
        if (auto r = entry->set_char(54, order.side); !r.has_value())
            return std::unexpected(r.error());
        if (auto r = entry->set_decimal(38, order.order_qty); !r.has_value())
            return std::unexpected(r.error());
    }
    if (auto r = bb.group_end(*orders_g); !r.has_value()) return std::unexpected(r.error());

    if (auto r = bb.field(394, params.bid_type); !r.has_value()) return std::unexpected(r.error());

    return bb.commit(out);
}

// ── OrderCancelReject (35=9) ──────────────────────────────────────────────────
// Field order (matches tests/session/golden/order_cancel_reject.fix — ascending
// tag order at root, T011): 11,37,39,41,102,434. Group-free.
[[nodiscard]] fixpp::core::expected_t<std::span<std::byte>> build_order_cancel_reject(
    std::span<std::byte> out, std::string_view order_id, std::string_view cl_ord_id,
    std::string_view orig_cl_ord_id, char ord_status, char cxl_rej_response_to,
    std::int64_t cxl_rej_reason) noexcept {
    if (order_id.empty()) return std::unexpected(fixpp::core::error::wire_required_field_missing);
    if (cl_ord_id.empty()) return std::unexpected(fixpp::core::error::wire_required_field_missing);
    if (orig_cl_ord_id.empty())
        return std::unexpected(fixpp::core::error::wire_required_field_missing);

    fixpp::wire::body_builder bb{"9"};

    if (auto r = bb.field(11, cl_ord_id); !r.has_value()) return std::unexpected(r.error());
    if (auto r = bb.field(37, order_id); !r.has_value()) return std::unexpected(r.error());
    if (auto r = bb.field(39, ord_status); !r.has_value()) return std::unexpected(r.error());
    if (auto r = bb.field(41, orig_cl_ord_id); !r.has_value()) return std::unexpected(r.error());
    if (auto r = bb.field(102, cxl_rej_reason); !r.has_value()) return std::unexpected(r.error());
    if (auto r = bb.field(434, cxl_rej_response_to); !r.has_value())
        return std::unexpected(r.error());

    return bb.commit(out);
}

// ── AllocationReport (35=AS) ─────────────────────────────────────────────────
// Field order (matches tests/session/golden/allocation_report.fix — ascending
// tag order at root, T012): 6,53,54,55,71,75,87,453(group+entries),755,794,857.
[[nodiscard]] fixpp::core::expected_t<std::span<std::byte>> build_allocation_report(
    std::span<std::byte> out, const AllocationReportParams& params) noexcept {
    if (params.alloc_report_id.empty())
        return std::unexpected(fixpp::core::error::wire_required_field_missing);
    if (params.trade_date.empty())
        return std::unexpected(fixpp::core::error::wire_required_field_missing);
    if (params.symbol.empty())
        return std::unexpected(fixpp::core::error::wire_required_field_missing);

    fixpp::wire::body_builder bb{"AS"};

    if (auto r = bb.field(6, params.avg_px); !r.has_value()) return std::unexpected(r.error());
    if (auto r = bb.field(53, params.quantity); !r.has_value()) return std::unexpected(r.error());
    // Validate side per-exemplar (FR-003 hand-validated char domain, mirrors D/8).
    if (!is_valid_side(params.side))
        return std::unexpected(fixpp::core::error::wire_field_value_out_of_range);
    if (auto r = bb.field(54, params.side); !r.has_value()) return std::unexpected(r.error());
    if (auto r = bb.field(55, params.symbol); !r.has_value()) return std::unexpected(r.error());
    if (auto r = bb.field(71, params.alloc_trans_type); !r.has_value())
        return std::unexpected(r.error());
    if (auto r = bb.field(75, params.trade_date); !r.has_value()) return std::unexpected(r.error());
    if (auto r = bb.field(87, params.alloc_status); !r.has_value())
        return std::unexpected(r.error());

    // NoPartyIDs(453) -> NoPartySubIDs(802): always opened by this builder
    // (an empty `parties` span emits 453=0, present-but-empty — the count-of-
    // zero case is already witnessed by E's ORD2, data-model §3.1 "Count-of-
    // zero witness target").
    auto parties_g = bb.group_begin(453, 448);
    if (!parties_g.has_value()) return std::unexpected(parties_g.error());
    for (const auto& party : params.parties) {
        auto pentry = parties_g->add_entry();
        if (!pentry.has_value()) return std::unexpected(pentry.error());
        if (party.party_id.empty())
            return std::unexpected(fixpp::core::error::wire_required_field_missing);
        if (auto r = pentry->set_string(448, party.party_id); !r.has_value())
            return std::unexpected(r.error());
        if (auto r = pentry->set_char(447, party.party_id_source); !r.has_value())
            return std::unexpected(r.error());
        if (auto r = pentry->set_int(452, party.party_role); !r.has_value())
            return std::unexpected(r.error());

        auto subs_g = pentry->group_begin(802, 523);
        if (!subs_g.has_value()) return std::unexpected(subs_g.error());
        for (const auto& sub : party.sub_ids) {
            auto sentry = subs_g->add_entry();
            if (!sentry.has_value()) return std::unexpected(sentry.error());
            if (sub.party_sub_id.empty())
                return std::unexpected(fixpp::core::error::wire_required_field_missing);
            if (auto r = sentry->set_string(523, sub.party_sub_id); !r.has_value())
                return std::unexpected(r.error());
            if (auto r = sentry->set_int(803, sub.party_sub_id_type); !r.has_value())
                return std::unexpected(r.error());
        }
        if (auto r = bb.group_end(*subs_g); !r.has_value()) return std::unexpected(r.error());
    }
    if (auto r = bb.group_end(*parties_g); !r.has_value()) return std::unexpected(r.error());

    if (auto r = bb.field(755, params.alloc_report_id); !r.has_value())
        return std::unexpected(r.error());
    if (auto r = bb.field(794, params.alloc_report_type); !r.has_value())
        return std::unexpected(r.error());
    if (auto r = bb.field(857, params.alloc_no_orders_type); !r.has_value())
        return std::unexpected(r.error());

    return bb.commit(out);
}

}  // namespace fixpp::session
