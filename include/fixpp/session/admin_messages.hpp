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
#include <span>
#include <string_view>

#include <fixpp/core/error.hpp>
#include <fixpp/session/seqnum.hpp>

namespace fixpp::session {

// ── Logon (35=A) ─────────────────────────────────────────────────────────────
// FR-002/003/004/011, [FIX-SL §4.2]/§4.3. S-001/S-015/S-016/S-020.
//
// Reentrancy: session-strand; called from open() (initiator) or from
// on_inbound_frame() (acceptor validate path). noexcept ([const §X.5]).

// Build an outbound Logon frame into `out`. Stamps SeqNum(34), SenderCompID(49)/
// TargetCompID(56), HeartBtInt(108), SendingTime(52).
// PLACEHOLDER — body lands T021 (Phase 3 / US1).
[[nodiscard]] fixpp::core::expected_t<std::span<std::byte>>
    build_logon(std::span<std::byte> out,
                seqnum_t seq,
                std::string_view sender_comp_id,
                std::string_view target_comp_id,
                std::string_view begin_string,
                int heartbt_int) noexcept;

// Interpret an inbound Logon frame; validate BeginString/CompID/HeartBtInt.
// Returns the negotiated HeartBtInt on success.
// PLACEHOLDER — body lands T024/T025 (Phase 3 / US1).
[[nodiscard]] fixpp::core::expected_t<int>
    interpret_logon(std::span<const std::byte> frame,
                    std::string_view expected_sender,
                    std::string_view expected_target,
                    std::string_view expected_begin) noexcept;

// ── Logout (35=5) ────────────────────────────────────────────────────────────
// FR-005, [FIX-SL §4.6]. S-002.

// Build an outbound Logout frame (optional text field).
// PLACEHOLDER — body lands T046 (Phase 6 / US4).
[[nodiscard]] fixpp::core::expected_t<std::span<std::byte>>
    build_logout(std::span<std::byte> out,
                 seqnum_t seq,
                 std::string_view sender_comp_id,
                 std::string_view target_comp_id,
                 std::string_view text = {}) noexcept;

// ── Heartbeat (35=0) ─────────────────────────────────────────────────────────
// FR-006, [FIX-SL §4.5.1]. S-003.

// Build an outbound Heartbeat (optionally echoing TestReqID(112)).
// PLACEHOLDER — body lands T040 (Phase 5 / US3).
[[nodiscard]] fixpp::core::expected_t<std::span<std::byte>>
    build_heartbeat(std::span<std::byte> out,
                    seqnum_t seq,
                    std::string_view sender_comp_id,
                    std::string_view target_comp_id,
                    std::string_view test_req_id = {}) noexcept;

// ── TestRequest (35=1) ───────────────────────────────────────────────────────
// FR-006, [FIX-SL §4.5.5]. S-004.

// Build an outbound TestRequest with a unique TestReqID(112).
// PLACEHOLDER — body lands T040 (Phase 5 / US3).
[[nodiscard]] fixpp::core::expected_t<std::span<std::byte>>
    build_test_request(std::span<std::byte> out,
                       seqnum_t seq,
                       std::string_view sender_comp_id,
                       std::string_view target_comp_id,
                       std::string_view test_req_id) noexcept;

// ── Reject (35=3) ────────────────────────────────────────────────────────────
// FR-007, [FIX-SL §4.5.4]. S-007.
// RefSeqNum(45)/RefTagID(371)/RefMsgType(372)/SessionRejectReason(373).
// No reject-loop: a malformed Reject/Logout is never itself rejected (I-5).

// Build an outbound session-level Reject.
// PLACEHOLDER — body lands T054 (Phase 7 / US5).
[[nodiscard]] fixpp::core::expected_t<std::span<std::byte>>
    build_reject(std::span<std::byte> out,
                 seqnum_t seq,
                 std::string_view sender_comp_id,
                 std::string_view target_comp_id,
                 seqnum_t ref_seq_num,
                 int ref_tag_id,
                 std::string_view ref_msg_type,
                 int session_reject_reason) noexcept;

}  // namespace fixpp::session
