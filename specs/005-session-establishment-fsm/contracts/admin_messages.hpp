// contracts/admin_messages.hpp — shape oracle (NOT the build header)
// Session semantics of the 5 admin messages over the merged wire/dict
// surfaces. 005 interprets semantics; wire/ (PR #68) frames/parses/
// serializes, dictionary/ (PR #66/#67) gives typed access.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

namespace fixpp::session {

// Logon(35=A) — S-001/S-015/S-016/S-020. Initiator emits (seq=1, configured
//   CompIDs, requested HeartBtInt(108), SendingTime(52) from effective_clock).
//   Acceptor validates BeginString(8) version gate (FIX.4.2/4.4 codegen
//   namespaces; FIXT.1.1/5.0SP2 NOT claimed by 005 — Gate A round 1
//   version-coverage ledger, [FIX-SL §4.2.1]) + SenderCompID(49)/
//   TargetCompID(56) point-to-point only ([FIX-SL §4.2.2]; S-016
//   OnBehalfOfCompID(115)/DeliverToCompID(128) third-party addressing is
//   deferred-with-traceability — 005 owns the 49/56 portion of S-016 only)
//   + negotiates HeartBtInt ([FIX-SL §4.3.4]); refusal ⇒ session_invalid_
//   logon / _compid_mismatch / _begin_string_unsupported, no Active.
//   First message not Logon ⇒ refuse ([FIX-SL §4.3], US1#4).
// Logout(35=5) — S-002. Initiate/accept ([FIX-SL §4.6]); graceful-close
//   bounded by effective_clock timeout (session_logout_timeout) → force
//   disconnect; non-Active Logout follows the [FIX-SL §4.6] transition.
// Heartbeat(35=0) — S-003. Emit on outbound idle == HeartBtInt; reply to a
//   peer TestRequest echoing TestReqID(112). HeartBtInt=0 disables timers
//   ([FIX-SL §4.3.4]).
// TestRequest(35=1) — S-004. Emit on inbound silence > test_request_
//   threshold; unanswered past grace ⇒ session_test_request_unanswered →
//   unhealthy → disconnect ([FIX-SL §4.5.5]).
// Reject(35=3) — S-007. Session-level reject ([FIX-SL §4.5.4]) carrying
//   RefSeqNum(45)/RefTagID(371)/RefMsgType(372)/SessionRejectReason(373).
//   A Reject/Logout is NEVER itself rejected (no reject loop, I-5).

}  // namespace fixpp::session
