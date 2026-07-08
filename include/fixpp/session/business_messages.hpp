// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/session/business_messages.hpp
//
// fixpp::session minimal typed business-message builders (020-g2-business-messages).
//
// Two FIX 4.4 application messages: NewOrderSingle(35=D, catalogue A-001) and
// ExecutionReport(35=8, catalogue A-006). v1.0 is a MINIMAL slice — Limit-only
// NOS, fully-filled ExecRpt, minimal field set; full-field + all-protocol-version
// coverage are deferred (FR-015a/FR-015b).
//
// READ side is NOT here: it is the already-generated fixpp::v44::{NewOrderSingle,
// ExecutionReport} flyweights (003 codegen). These builders are the genuine new
// WRITE surface (the codegen emits no writer; owning_<Msg> is read-only).
//
// House build-shape (shared with admin_messages.hpp): span-in / noexcept /
// expected_t / body-only field append (not wire::Writer) — the app builders
// delegate to fixpp::wire::body_builder, which accumulates internally and
// copies into `out` only on commit() (stack-scratch-then-copy atomicity). BUT
// — unlike the
// admin builders, which emit COMPLETE self-contained frames (8/9/34/35/…) — these
// emit the app BODY ONLY: they lead with 35=MsgType then business fields, and emit
// NO session header tags (8/9/34/49/52/56) and NO 10= trailer. The engine stamps
// those: the output feeds 019's Engine::send(SessionId, payload), whose send path
// places MsgType in wire field-3, writes a digit-only BodyLength, and validates the
// opaque payload fail-closed (FR-004a/FR-016; INV-1/INV-2/INV-8).
//
// Numeric fields are fixpp::decimal_t (= core::decimal<FIXPP_DECIMAL_T>), serialized
// via decimal_t::format(span) (canonical, locale-independent — FR-007/INV-3).
//
// Anchors: contracts/business-messages.md; data-model.md E1/E2; spec FR-001..008/016.
// No asio::awaitable in this header. No std::mutex. ([const §XV.9]: safe.)
#pragma once

#include <cstddef>
#include <cstdint>
#include <fixpp/core/decimal_alias.hpp>  // fixpp::decimal_t
#include <fixpp/core/error.hpp>          // fixpp::core::expected_t
#include <span>
#include <string_view>

namespace fixpp::session {

// ── NewOrderSingle (35=D), catalogue A-001 ───────────────────────────────────
// Limit-only (OrdType fixed to 2). Emits the app body in QuickFIX-golden
// ascending-tag order (T013 refactor onto body_builder; supersedes the legacy
// 020 order):
//   35=D 11=<cl_ord_id> 38=<order_qty> 40=2 44=<price> 54=<side>
//   55=<symbol> 60=<transact_time>
//
// side: '1'=Buy, '2'=Sell (out-of-range char rejected).
// transact_time: caller-pre-formatted UTCTimestamp (same contract as 52=
//   SendingTime); the builder validates its length+shape fail-closed (FR-008).
//
// Returns the written body span on success. On ANY failure (empty cl_ord_id/
// symbol, out-of-range side, unformattable order_qty/price decimal, ill-formed
// transact_time, or too-small out) returns std::unexpected(error::…); the builder
// builds into a local stack scratch buffer and copies into `out` only on full
// success, so on failure the returned span is absent and `out` is UNSPECIFIED —
// the caller MUST NOT inspect it (INV-4 atomicity). Too-small out →
// error::wire_frame_too_large (parity with build_logon).
//
// FR-001/002/003/004/005/007/008; INV-2/3/4; [const §VIII.5] (no heap).
[[nodiscard]] fixpp::core::expected_t<std::span<std::byte>> build_new_order_single(
    std::span<std::byte> out, std::string_view cl_ord_id, std::string_view symbol, char side,
    const fixpp::decimal_t& order_qty, const fixpp::decimal_t& price,
    std::string_view transact_time) noexcept;

// ── ExecutionReport (35=8), catalogue A-006 ──────────────────────────────────
// Emits the app body in QuickFIX-golden ascending-tag order (T014 refactor onto
// body_builder; supersedes the legacy 020 order):
//   35=8 6=<avg_px> 14=<cum_qty> 17=<exec_id> 37=<order_id> 39=<ord_status>
//   54=<side> 55=<symbol> 150=<exec_type> 151=<leaves_qty>
//
// Round-trip fully-filled: exec_type='F' (Trade — the FIX 4.4 fill wire value),
// ord_status='2' (Filled), leaves_qty=0, cum_qty=order_qty, avg_px=price (all
// caller-supplied). Same error / required / atomicity contract as above.
//
// FR-001/002/003/004/005/007/008; INV-2/3/4; [const §VIII.5] (no heap).
[[nodiscard]] fixpp::core::expected_t<std::span<std::byte>> build_execution_report(
    std::span<std::byte> out, std::string_view order_id, std::string_view exec_id, char exec_type,
    char ord_status, std::string_view symbol, char side, const fixpp::decimal_t& leaves_qty,
    const fixpp::decimal_t& cum_qty, const fixpp::decimal_t& avg_px) noexcept;

// ── NewOrderList (35=E), catalogue A-01x — 061-typed-app-messages T010 ──────
// The pivotal grouped/nested exemplar: `NoOrders(73)` -> `NoPartyIDs(453)` ->
// `NoPartySubIDs(802)` (3-level). Built on fixpp::wire::body_builder (data-model
// §1/§2), NOT the 020 stack-scratch wfield/wchar/wdecimal helpers used by D/8.
//
// Emits the app body (field order matches the QuickFIX-authored golden,
// tests/session/golden/new_order_list.fix — dictionary order within each group
// entry, NOT tag-ascending):
//   35=E 66=<list_id> 68=<tot_no_orders>
//   73=<N> { 11=<cl_ord_id> 67=<list_seq_no>
//            453=<M> { 448=<party_id> 447=<party_id_source> 452=<party_role>
//                      802=<K> { 523=<party_sub_id> 803=<party_sub_id_type> }* }*
//            55=<symbol> 54=<side> 38=<order_qty> }*
//   394=<bid_type>
//
// `NewOrderListOrder::parties` MAY be an empty span: `body_builder` still emits
// `453=0` (present-but-empty, data-model §3.1 "Count-of-zero witness target"),
// distinct from never opening the group at all (not exercised by this builder —
// every order always opens `NoPartyIDs`).
//
// Delimiters (author-supplied to body_builder, no wire->dictionary edge):
// `73`->`11`, `453`->`448`, `802`->`523` (data-model §3.1 "Delimiter tags").
//
// Same fail-closed / atomicity contract as the other builders (INV-2/3/4/5):
// empty required string, control-byte/SOH in a value, out-of-range char,
// unformattable decimal, undersized/over-cap output -> typed error, `out`
// UNSPECIFIED on failure (caller MUST NOT inspect it).
//
// FR-001/002/003/004/005/006/007; INV-2/3/4/5.
struct NewOrderListPartySubId {
    std::string_view party_sub_id;
    std::int64_t party_sub_id_type = 0;
};

struct NewOrderListParty {
    std::string_view party_id;
    char party_id_source = 0;
    std::int64_t party_role = 0;
    std::span<const NewOrderListPartySubId> sub_ids;
};

struct NewOrderListOrder {
    std::string_view cl_ord_id;
    std::int64_t list_seq_no = 0;
    char side = 0;
    std::string_view symbol;
    fixpp::decimal_t order_qty;
    // Empty span -> NoPartyIDs(453)=0 (present-but-empty); the group is always
    // opened, never omitted, by this builder.
    std::span<const NewOrderListParty> parties;
};

struct NewOrderListParams {
    std::string_view list_id;
    std::int64_t bid_type = 0;
    std::int64_t tot_no_orders = 0;
    std::span<const NewOrderListOrder> orders;
};

[[nodiscard]] fixpp::core::expected_t<std::span<std::byte>> build_new_order_list(
    std::span<std::byte> out, const NewOrderListParams& params) noexcept;

// ── OrderCancelReject (35=9), catalogue A-01x — 061-typed-app-messages T011 ──
// Group-free exemplar. Built on fixpp::wire::body_builder (data-model §1/§2).
//
// Emits the app body (field order matches the QuickFIX-authored golden,
// tests/session/golden/order_cancel_reject.fix — ascending tag order, the
// same root-level shape observed for E's root fields):
//   35=9 11=<cl_ord_id> 37=<order_id> 39=<ord_status> 41=<orig_cl_ord_id>
//   102=<cxl_rej_reason> 434=<cxl_rej_response_to>
//
// `cxl_rej_reason` is `CxlRejReason(102)`, XML `required='N'` (data-model
// §3.1 "9" row, "Optional (type-exercise)" column) — this v1.0 builder always
// emits it (no conditional-omission mechanism), mirroring how
// NewOrderListParams::bid_type is always emitted despite being an XML
// component-optional field on E.
//
// Same fail-closed / atomicity contract as the other builders (INV-2/3/4):
// empty required string, control-byte/SOH in a value, out-of-range char,
// undersized/over-cap output -> typed error, `out` UNSPECIFIED on failure.
//
// FR-001/002/003/004/005/007; INV-2/3/4.
[[nodiscard]] fixpp::core::expected_t<std::span<std::byte>> build_order_cancel_reject(
    std::span<std::byte> out, std::string_view order_id, std::string_view cl_ord_id,
    std::string_view orig_cl_ord_id, char ord_status, char cxl_rej_response_to,
    std::int64_t cxl_rej_reason) noexcept;

// ── AllocationReport (35=AS), catalogue A-01x — 061-typed-app-messages T012 ─
// Multi-char MsgType + a single top-level 2-level nested chain: `Parties
// NoPartyIDs(453)` -> `NoPartySubIDs(802)`. Built on fixpp::wire::body_builder
// (data-model §1/§2).
//
// Emits the app body (field order matches the QuickFIX-authored golden,
// tests/session/golden/allocation_report.fix — ascending tag order at root,
// so `755/794/857` land AFTER the `453` group content, mirroring how E's
// `394` lands after `73`'s content):
//   35=AS 6=<avg_px> 53=<quantity> 54=<side> 55=<symbol> 71=<alloc_trans_type>
//   75=<trade_date> 87=<alloc_status>
//   453=<N> { 448=<party_id> 447=<party_id_source> 452=<party_role>
//             802=<K> { 523=<party_sub_id> 803=<party_sub_id_type> }* }*
//   755=<alloc_report_id> 794=<alloc_report_type> 857=<alloc_no_orders_type>
//
// `AllocNoOrdersType(857)` MUST be seeded `0` (*NotSpecified*) by the caller
// so the golden's shape holds (data-model §3.1 "AS optional-group note"): a
// non-zero value would (in a real QuickFIX counterparty) require the
// conditionally-required `OrdAllocGrp NoOrders(73)` group, which this builder
// does not emit.
//
// `AllocationReportParams::parties` is always opened (like
// `NewOrderListOrder::parties`); the count-of-zero present-but-empty case is
// already witnessed by E's ORD2 (data-model §3.1 "Count-of-zero witness
// target") so AS's seed carries exactly one non-empty party.
//
// Delimiters: `453`->`448`, `802`->`523` (data-model §3.1 "Delimiter tags").
//
// Same fail-closed / atomicity contract as the other builders (INV-2/3/4/5).
//
// FR-001/002/003/004/005/006/007; INV-2/3/4/5.
struct AllocationReportPartySubId {
    std::string_view party_sub_id;
    std::int64_t party_sub_id_type = 0;
};

struct AllocationReportParty {
    std::string_view party_id;
    char party_id_source = 0;
    std::int64_t party_role = 0;
    std::span<const AllocationReportPartySubId> sub_ids;
};

struct AllocationReportParams {
    std::string_view alloc_report_id;
    char alloc_trans_type = 0;
    std::int64_t alloc_report_type = 0;
    std::int64_t alloc_status = 0;
    std::int64_t alloc_no_orders_type = 0;
    char side = 0;
    fixpp::decimal_t quantity;
    fixpp::decimal_t avg_px;
    std::string_view trade_date;
    std::string_view symbol;
    // Empty span -> NoPartyIDs(453)=0 (present-but-empty); the group is always
    // opened, never omitted, by this builder (mirrors NewOrderListOrder::parties).
    std::span<const AllocationReportParty> parties;
};

[[nodiscard]] fixpp::core::expected_t<std::span<std::byte>> build_allocation_report(
    std::span<std::byte> out, const AllocationReportParams& params) noexcept;

}  // namespace fixpp::session
