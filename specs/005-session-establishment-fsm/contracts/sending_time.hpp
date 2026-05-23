// contracts/sending_time.hpp — shape oracle (NOT the build header)
// SendingTime(52) stamp/validate + the Clarification Q3 disposition (D-3).
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

namespace fixpp::session {

// OUTBOUND: every emitted message carries SendingTime(52) =
//   effective_clock.now() formatted via core::utc_time_to_fix_string
//   ([2d §6.6] now() for wire timestamp). Round-trips losslessly (I-6).
//
// INBOUND (S-019, FR-013, [FIX-SL §4.2.3]): compare SendingTime(52) to
//   effective_clock.now(); if |divergence| > MaxLatency (default 120 s,
//   D-8) ⇒ Clarification Q3 disposition:
//     - established session: emit Reject(SessionRejectReason=10, ref tag 52)
//       → Logout → disconnect.
//     - offending message IS the Logon: logout-with-error, NO standalone
//       Reject (no session established yet).
//   Surfaced as session_sending_time_accuracy. Engine-unanimous
//   (QuickFIX C++/J doBadTime, fix8 BadSendingTime). Oracle:
//   1d_InvalidLogonBadSendingTime.def vs 2o_SendingTimeValueOutOfRange.def.
//
// Missing/unparseable inbound SendingTime ⇒ session-level Reject
// ([FIX-SL §4.5.4], spec edge case).

}  // namespace fixpp::session
