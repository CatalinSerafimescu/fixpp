// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/session/admin_messages.hpp
//
// fixpp::session admin-message build/interpret declarations (005, T017).
//
// The 5 FIX session admin messages: Logon(35=A), Logout(35=5),
// Heartbeat(35=0), TestRequest(35=1), Reject(35=3).
//
// 005 interprets SESSION SEMANTICS only. Wire framing/parsing is the merged
// wire/ surface (PR #68); typed field access is the merged dictionary/ surface
// (PR #66/#67). These headers declare the session-semantic entry points;
// bodies land per user story (Phase 3–6 / T021/T040/T054).
//
// Anchors: data-model.md E5; contracts/admin_messages.hpp; spec FR-002..007.
// [FIX-SL §4.2]–§4.6.
//
// No asio::awaitable in this header. No std::mutex. ([const §XV.9]: safe.)
#pragma once

#include <cstddef>
#include <fixpp/core/error.hpp>
#include <fixpp/session/seqnum.hpp>
#include <optional>
#include <span>
#include <string_view>

namespace fixpp::session {

// ── Logon (35=A) ─────────────────────────────────────────────────────────────
// FR-002/003/004/011, [FIX-SL §4.2]/§4.3. S-001/S-015/S-016/S-020.
//
// Reentrancy: session-strand; called from open() (initiator) or from
// on_inbound_frame() (acceptor validate path). noexcept ([const §X.5]).

// Build an outbound Logon frame into `out`. Stamps SeqNum(34), SenderCompID(49)/
// TargetCompID(56), HeartBtInt(108), SendingTime(52=sending_time).
// begin_string: negotiated FIX version string (e.g. "FIX.4.4") for tag 8.
// sending_time: pre-formatted UTCTimestamp string from effective_clock.now()
//               (e.g. "20240101-00:00:00.000") for tag 52.
// reset_seqnum: when true, emits ResetSeqNumFlag(141)=Y.
//   bilateral_strict mode sets this to request mutual seqnum reset [FR-017].
// FR-002/FR-003/RC#4: kBeginStringDefault + kSendingTimePlaceholder REMOVED.
// RC#C (gate-b/r1): added reset_seqnum parameter for 141=Y support [FR-017].
[[nodiscard]] fixpp::core::expected_t<std::span<std::byte>> build_logon(
    std::span<std::byte> out, seqnum_t seq, std::string_view sender_comp_id,
    std::string_view target_comp_id, std::string_view begin_string, int heartbt_int,
    std::string_view sending_time, bool reset_seqnum = false,
    std::optional<seqnum_t> next_expected_seq = std::nullopt) noexcept;

// Interpret an inbound Logon frame; validate BeginString/CompID/HeartBtInt.
// Returns the negotiated HeartBtInt on success.
// PLACEHOLDER — body lands T024/T025 (Phase 3 / US1).
[[nodiscard]] fixpp::core::expected_t<int> interpret_logon(
    std::span<const std::byte> frame, std::string_view expected_sender,
    std::string_view expected_target, std::string_view expected_begin) noexcept;

// ── Logout (35=5) ────────────────────────────────────────────────────────────
// FR-005, [FIX-SL §4.6]. S-002.

// Build an outbound Logout frame (optional text field).
// begin_string: negotiated FIX version string for tag 8 (FR-002/RC#4).
// sending_time: pre-formatted UTCTimestamp from effective_clock.now() (FR-003/RC#4).
[[nodiscard]] fixpp::core::expected_t<std::span<std::byte>> build_logout(
    std::span<std::byte> out, seqnum_t seq, std::string_view sender_comp_id,
    std::string_view target_comp_id, std::string_view text, std::string_view begin_string,
    std::string_view sending_time) noexcept;

// ── Heartbeat (35=0) ─────────────────────────────────────────────────────────
// FR-006, [FIX-SL §4.5.1]. S-003.

// Build an outbound Heartbeat (optionally echoing TestReqID(112)).
// begin_string: negotiated FIX version string for tag 8 (FR-002/RC#4).
// sending_time: pre-formatted UTCTimestamp from effective_clock.now() (FR-003/RC#4).
[[nodiscard]] fixpp::core::expected_t<std::span<std::byte>> build_heartbeat(
    std::span<std::byte> out, seqnum_t seq, std::string_view sender_comp_id,
    std::string_view target_comp_id, std::string_view test_req_id, std::string_view begin_string,
    std::string_view sending_time) noexcept;

// ── TestRequest (35=1) ───────────────────────────────────────────────────────
// FR-006, [FIX-SL §4.5.5]. S-004.

// Build an outbound TestRequest with a unique TestReqID(112).
// begin_string: negotiated FIX version string for tag 8 (FR-002/RC#4).
// sending_time: pre-formatted UTCTimestamp from effective_clock.now() (FR-003/RC#4).
[[nodiscard]] fixpp::core::expected_t<std::span<std::byte>> build_test_request(
    std::span<std::byte> out, seqnum_t seq, std::string_view sender_comp_id,
    std::string_view target_comp_id, std::string_view test_req_id, std::string_view begin_string,
    std::string_view sending_time) noexcept;

// ── Reject (35=3) ────────────────────────────────────────────────────────────
// FR-007, [FIX-SL §4.5.4]. S-007.
// RefSeqNum(45)/RefTagID(371)/RefMsgType(372)/SessionRejectReason(373).
// No reject-loop: a malformed Reject/Logout is never itself rejected (I-5).

// Build an outbound session-level Reject.
// begin_string: negotiated FIX version string for tag 8 (FR-002/RC#4).
// sending_time: pre-formatted UTCTimestamp from effective_clock.now() (FR-003/RC#4).
[[nodiscard]] fixpp::core::expected_t<std::span<std::byte>> build_reject(
    std::span<std::byte> out, seqnum_t seq, std::string_view sender_comp_id,
    std::string_view target_comp_id, seqnum_t ref_seq_num, int ref_tag_id,
    std::string_view ref_msg_type, int session_reject_reason, std::string_view begin_string,
    std::string_view sending_time) noexcept;

// ── ResendRequest (35=2) ─────────────────────────────────────────────────────
// FR-009, [FIX-SL §4.3.2]. 013 recovery sub-protocol.
//
// Build an outbound ResendRequest with BeginSeqNo(7)/EndSeqNo(16).
// end_seqno=0 means "through current last outbound" per [FIX-SL §4.3.2].
// begin_string: negotiated FIX version string for tag 8 (FR-002/RC#4).
// sending_time: pre-formatted UTCTimestamp from effective_clock.now() (FR-003/RC#4).
[[nodiscard]] fixpp::core::expected_t<std::span<std::byte>> build_resend_request(
    std::span<std::byte> out, seqnum_t seq, std::string_view sender_comp_id,
    std::string_view target_comp_id, seqnum_t begin_seqno, seqnum_t end_seqno,
    std::string_view begin_string, std::string_view sending_time) noexcept;

// ── BusinessMessageReject (35=j) ─────────────────────────────────────────────
// 019-app-callbacks T010; research D4; [FIX50SP2] Infrastructure / Business
// Rejects (catalogue A-014). Emitted when fromApp() returns an error.
//
// Required fields per FIX spec:
//   RefSeqNum(45)            — MsgSeqNum of the rejected app message
//   RefMsgType(372)          — MsgType of the rejected app message
//   BusinessRejectReason(380)— reason code (fixed/default for slice 1)
//
// Same stack-buffer discipline as build_reject: zero-alloc per [const §VIII.5].
// begin_string: negotiated FIX version string for tag 8 (FR-002/RC#4).
// sending_time: pre-formatted UTCTimestamp from effective_clock.now() (FR-003/RC#4).
// business_reject_reason: default 0 (Other) for slice 1; per-error-code reasons deferred.
[[nodiscard]] fixpp::core::expected_t<std::span<std::byte>> build_business_message_reject(
    std::span<std::byte> out, seqnum_t seq, std::string_view sender_comp_id,
    std::string_view target_comp_id, seqnum_t ref_seq_num, std::string_view ref_msg_type,
    int business_reject_reason, std::string_view begin_string,
    std::string_view sending_time) noexcept;

// ── SequenceReset (35=4) — GapFill mode ──────────────────────────────────────
// FR-009, [FIX-SL §4.4]. 013 recovery sub-protocol (reply to inbound ResendRequest).
//
// Build an outbound SequenceReset with GapFillFlag(123)=Y and NewSeqNo(36).
// begin_string: negotiated FIX version string for tag 8 (FR-002/RC#4).
// sending_time: pre-formatted UTCTimestamp from effective_clock.now() (FR-003/RC#4).
[[nodiscard]] fixpp::core::expected_t<std::span<std::byte>> build_sequence_reset_gapfill(
    std::span<std::byte> out, seqnum_t seq, std::string_view sender_comp_id,
    std::string_view target_comp_id, seqnum_t new_seqno, std::string_view begin_string,
    std::string_view sending_time) noexcept;

}  // namespace fixpp::session
