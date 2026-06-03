// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/session/msgtype_classifier.hpp
//
// Session-INTERNAL helper: admin-vs-application MsgType(35) classifier.
//
// Extracted from the inline `is_session_admin` bool in session.cpp ~:1924
// (019-app-callbacks T006 / research D8). This is the single source of truth
// for the admin MsgType allow-list; both inbound routing and the outbound
// emit split call this helper.
//
// NO behaviour change at the T006 call site — the existing Reject branch in
// session.cpp is preserved; this file only makes the logic reusable.
//
// Anchor: specs/019-app-callbacks/research.md D8; tasks.md T006.
// [arch §5.3] — src/ internal header; NOT exported to consumers.
#pragma once

#include <string_view>

namespace fixpp::session::detail {

// is_admin_msgtype — returns true for the 6 session-admin MsgTypes that the
// FIX session layer handles internally (after FSM processing).
//
// Admin allow-list (source: research.md D8 + session.cpp:1920–1930 comments):
//   "0" Heartbeat        — handled in Active; forwarded to fromAdmin
//   "1" TestRequest      — handled in Active; forwarded to fromAdmin
//   "2" ResendRequest    — handled ABOVE the Reject branch (recovery); in allow-
//                          list so the Reject branch does not re-handle it
//   "3" Reject           — handled in Active; forwarded to fromAdmin
//   "4" SequenceReset    — handled ABOVE the Reject branch (recovery); same note
//   "5" Logout           — handled in Active; forwarded to fromAdmin
//
// Explicitly EXCLUDED (application MsgType, or deliberate Reject path):
//   "A" dup-Logon-in-Active — per 005 data-model row 22 / FR-017 "never silent
//       no-op"; falls through to Reject (defensive divergence from QuickFIX).
//   Any other MsgType  — application message; US1 (T011) routes to fromApp when
//       an Application is registered, otherwise today's Reject.
//
// [[nodiscard]] — the boolean result is always consumed; discarding it is a
// programming error.
[[nodiscard]] constexpr bool is_admin_msgtype(std::string_view msg_type) noexcept {
    return (msg_type == "0" ||  // Heartbeat
            msg_type == "1" ||  // TestRequest
            msg_type == "2" ||  // ResendRequest (handled above; in list for completeness)
            msg_type == "3" ||  // Reject
            msg_type == "4" ||  // SequenceReset (handled above; in list for completeness)
            msg_type == "5");   // Logout
}

}  // namespace fixpp::session::detail
