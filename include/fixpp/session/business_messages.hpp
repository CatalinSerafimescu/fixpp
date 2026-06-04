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
// expected_t / wire::Writer / stack-scratch-then-copy atomicity. BUT — unlike the
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
#include <fixpp/core/decimal_alias.hpp>  // fixpp::decimal_t
#include <fixpp/core/error.hpp>          // fixpp::core::expected_t
#include <span>
#include <string_view>

namespace fixpp::session {

// ── NewOrderSingle (35=D), catalogue A-001 ───────────────────────────────────
// Limit-only (OrdType fixed to 2). Emits the app body:
//   35=D 11=<cl_ord_id> 55=<symbol> 54=<side> 38=<order_qty> 40=2
//   44=<price> 60=<transact_time>
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
    std::span<std::byte> out,
    std::string_view cl_ord_id,
    std::string_view symbol,
    char side,
    const fixpp::decimal_t& order_qty,
    const fixpp::decimal_t& price,
    std::string_view transact_time) noexcept;

// ── ExecutionReport (35=8), catalogue A-006 ──────────────────────────────────
// Emits the app body:
//   35=8 37=<order_id> 17=<exec_id> 150=<exec_type> 39=<ord_status>
//   55=<symbol> 54=<side> 151=<leaves_qty> 14=<cum_qty> 6=<avg_px>
//
// Round-trip fully-filled: exec_type='F' (Trade — the FIX 4.4 fill wire value),
// ord_status='2' (Filled), leaves_qty=0, cum_qty=order_qty, avg_px=price (all
// caller-supplied). Same error / required / atomicity contract as above.
//
// FR-001/002/003/004/005/007/008; INV-2/3/4; [const §VIII.5] (no heap).
[[nodiscard]] fixpp::core::expected_t<std::span<std::byte>> build_execution_report(
    std::span<std::byte> out,
    std::string_view order_id,
    std::string_view exec_id,
    char exec_type,
    char ord_status,
    std::string_view symbol,
    char side,
    const fixpp::decimal_t& leaves_qty,
    const fixpp::decimal_t& cum_qty,
    const fixpp::decimal_t& avg_px) noexcept;

}  // namespace fixpp::session
