// contracts/session_config_consumed.hpp — shape oracle (NOT the build header)
// The [2d §4.5] SessionConfig fields 005 CONSUMES, plus the threshold
// VALUES 005 owns (D-8). 005 consumes the config shape; it does NOT
// redesign it (the struct is owned by 2d). SPDX: AGPL-3.0-or-later
#pragma once

namespace fixpp::session {

// CONSUMED from [2d §4.5] SessionConfig:
//   executor_override / mode (per_session_strand default) / locks
//   clock_override                 → effective_clock (E7, [2d §7.9])
//   sender_comp_id/target_comp_id/begin_string  (identity — owned here)
//   store_factory (unique_ptr<MessageStoreFactory>, [2e §4.4])
//   dictionary (required) / dialect_overlay (optional)
//
// VALUES 005 owns (2d declares the field, defers the value to the
// session-module spec — D-8). std::nullopt ⇒ engine substitutes these
// at Session::open:
//   heartbeat_interval        = 30 s          (QuickFIX/J default)
//   test_request_threshold    = 1 × HeartBtInt ([FIX-SL §4.5.5])
//   sending_time_threshold    = 120 s  (MaxLatency, QuickFIX default)
//   reject_policy             = strict_reject_then_logout  (matches Q3)
//
// HeartBtInt(108)=0 negotiated at Logon ⇒ heartbeating disabled, no
// heartbeat/test-request timers ([FIX-SL §4.3.4]).
// Bad SecurityProfile sentinel ⇒ session_invalid_config at open
// ([2d §4.5] N-P2-3).

}  // namespace fixpp::session
