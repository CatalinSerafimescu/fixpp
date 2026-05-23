// SPDX-License-Identifier: AGPL-3.0-or-later
// Shape oracle for 010-session-cfg-lifetime FR-005 (E1 from PR #82 Gate B): the
// new `session_invalid_state_for_send` error enum variant.
//
// This is a CONTRACT SHAPE ORACLE, not an implementation header. The actual
// declaration lands in include/fixpp/core/error.hpp during /speckit-implement;
// this file documents the binding contract that implementation must satisfy.
//
// Source: spec.md FR-005 + AC3; research.md D-3; PR #82 Gate B waiver F-07/E1
// in library/.specify/decisions/009-session-fsm-finalize-gateb.md.

#pragma once

// No #include needed — this file is a contract shape oracle composed
// entirely of comments documenting the binding form of the variant. The
// actual declaration (with `: std::uint8_t` underlying type) is in
// include/fixpp/core/error.hpp.

namespace fixpp::core {

// Excerpt of the `error` enum (defined in include/fixpp/core/error.hpp:14).
// 010 appends ONE new variant at the next free slot (77) immediately after
// `session_invalid_config = 76`. All other variants are unchanged.
//
// enum class error : std::uint8_t {
//     ...
//     session_invalid_config           = 76,  // existing; [2d §4.5] N-P2-3
//     session_invalid_state_for_send   = 77,  // NEW (010 FR-005)
//     ...
// };
//
// ============================================================================
// Variant semantics (binding contract):
// ============================================================================
//
// session_invalid_state_for_send = 77
//
//   Returned by Session::send(...) when the FSM is NOT in `fsm_state::Active`
//   (e.g. NotConnected, LogonSent, LogonReceived-transient, Disconnected).
//
//   Replaces the 005-era reuse of `session_invalid_logon` at the one outbound
//   send site in src/session/session.cpp:1181 (Session::send, /speckit-analyze
//   verified exactly one site). The reuse was a semantic near-fit: `invalid_logon` is
//   meant for FSM-side Logon-refusal handling, but the outbound-send not-Active
//   case is a *caller* error (the integrator called send before the session
//   reached Active), not a Logon refusal.
//
//   Contract:
//     - Session state on return: unchanged. The FSM does NOT transition.
//     - Outbound side effects: NONE. No assign_outbound is called; no store
//       commit; no transport.send invocation.
//     - Cancellation interaction: the error is returned *before* any
//       awaitable suspension point in the send pipeline, so no cancellation
//       cleanup is required.
//     - Idempotency: a subsequent call to send(...) when the FSM is still
//       not-Active returns the same variant. No reject loop (I-5).
//     - C-ABI prefix-group mapping (per pre-D-12 documentation at
//       include/fixpp/core/error.hpp:248-258): FIXPP_ERR_SESSION_REJECT.
//       Grouped with `session_msg_type_invalid_for_state = 72`, which
//       documents the analogous *inbound* event-in-wrong-state case.
//
//   Tests that asserted `error::session_invalid_logon` at the one
//   Session::send non-Active site MUST be updated to assert
//   `error::session_invalid_state_for_send` (FR-005 AC3). File list pinned
//   at /speckit-tasks (grep candidates: tests/session/send_path_test.cpp +
//   any FSM-state-mismatch test that drives send pre-Logon).
//
// ============================================================================
// Doc-comment to paste at the new variant site in error.hpp:
// ============================================================================
//
// session_invalid_state_for_send = 77,       // FR-005 (010), [FIX-SL §4.5.4] — Session::send(...) called
//                                             //   while the FSM is not in `Active` (e.g. NotConnected,
//                                             //   LogonSent, Disconnected). Replaces the 005-era reuse
//                                             //   of `session_invalid_logon` at this site (semantic
//                                             //   near-fit; the caller's state mismatch is distinct
//                                             //   from a Logon refusal). No reject loop (I-5).
//                                             //   → FIXPP_ERR_SESSION_REJECT
//
// ============================================================================
// Forwards-compatibility (`[const §X.4]`):
// ============================================================================
//
// Appended at the next free slot (77); no existing slot reassignment; no
// existing variant removed. C-ABI consumers (none today — the C-ABI surface
// is itself deferred per `[const §X.2]` + 2i feature) will see the new
// integer value when the C-ABI surface lands. The C-ABI prefix-group mapping
// table (D-12 deferred) will pick this annotation up at that time.
//
// No external compatibility break today.

}  // namespace fixpp::core
