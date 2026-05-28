// SPDX-License-Identifier: AGPL-3.0-or-later
//
// fixpp — 013-session-reconnect-binding
// Contract: error::session_* variant family extension.
// Anchors: FR-008, FR-017, FR-021, FR-033, D-7, D-9, D-13; [const §X.2] ABI append-only.
//
// NOTE: This contract documents the FOUR NEW slots 116..119 appended to
// include/fixpp/core/error.hpp after 012's transport_* block, plus the reuse of
// shipped slots 73 (session_logout_timeout) and 74 (session_test_request_unanswered)
// from 005-era for FR-008 and FR-004 emissions. (Slot 119, renumbered from 121,
// added 2026-05-28 per /speckit-analyze C3 resolution — `session_invalid_argument`
// has no semantic fit among the existing `session_invalid_*` family: 66 is the
// FIX-protocol Logon-shape error; 76 is operator config-time validation;
// 77 is FSM-state-wrong-for-send. None covers a runtime nullptr-to-public-API
// rejection. F1/D1 resolution 2026-05-28: slots 73 and 74 REUSED from 005-era
// instead of allocating new 118/119 slots.) The /speckit-implement-time rewriter
// MUST cross-check the actual file to confirm the boundary remains at 115
// (transport_accept_cancelled = 115 per [2h §6.6]:1199); future ±N adjustment
// is reconciled at /implement-time without re-running Gate A. NEVER renumber
// existing slots.

#pragma once

#include "fixpp/core/error.hpp"

namespace fixpp::core::error {

// FR-017 / US1 AC7 / D-7 — bilateral_strict mode: we sent Logon with
// ResetSeqNumFlag(141)=Y; peer's response Logon lacks 141=Y; we Logout +
// disconnect. SessionStatus(1409) value is set to a corresponding FIX-wire
// enum (slot allocation in the SessionStatus enum — separate from this C++
// error enum — TBD at /speckit-tasks-time per research.md §7).
//   slot 116: session_seqnum_reset_mismatch
//
// FR-021 / US2 AC2 / D-9 — CompIdAuthorizationPolicy::authorize returned
// unauthorized: principal not in bindings, OR principal exists but asserted
// CompID not in its allow-list, OR policy is empty (default-deny).
//   slot 117: session_compid_unauthorized
//
// FR-008 / US1 AC5 / D-13 — Logout(5) sent; peer's Logout reply did not arrive
// within SessionConfig::logout_disconnect_timeout_ms; we closed the Transport
// anyway and surfaced this error.
//   REUSES shipped 005-era slot 73: session_logout_timeout
//   (F1/D1 resolution 2026-05-28 per /speckit-analyze — same FIX-SL §4.5 event;
//   reference-engine sweep across QuickFIX-cpp / QuickFIX-J / Fix8 confirmed zero
//   precedent for typed code-level discrimination of logout timeout classes.)
//
// FR-004 / US1 AC4 — inbound traffic absent for 2× HeartBtInt; emitted Logout
// + closed the Transport.
//   REUSES shipped 005-era slot 74: session_test_request_unanswered
//   (F1/D1 resolution 2026-05-28 per /speckit-analyze — same FIX-SL §4.5.2 event.)
//
// FR-006 — inbound Heartbeat(0) carried TestReqID(112) that does NOT match
// the most recent outbound TestRequest's TestReqID; per FIX-SL §4.5.4 this is
// a session-level error.
//   slot 118: session_testreqid_mismatch
//
// FR-033 — runtime rejection of an invalid argument to a public API entry
// point (currently: `reload_credentials(nullptr)`). Returned via
// `expected_t::unexpected(session_invalid_argument)` from `noexcept` runtime
// boundaries per data-model.md §E-6/§E-7. Distinct from `session_invalid_config`
// (operator config-time errors at Session::open) and from
// `session_invalid_state_for_send` (FSM-state-wrong-for-operation).
//   slot 119: session_invalid_argument
//   (renumbered from 121 to 119 per /speckit-analyze F1/D1 2026-05-28)
//
// C-ABI coalescing (owned by 2i, not 013): the 4 new slots (116..119) join the
// existing FIXPP_ERR_SESSION group at the C-ABI boundary. Slots 73 and 74 are
// already in the group. NO new C-ABI symbol.

// Forward declarations for downstream re-export ergonomics (the canonical
// definitions land in include/fixpp/core/error.hpp at slots 116..119).
//
// constexpr inline auto session_seqnum_reset_mismatch       = error::session_seqnum_reset_mismatch;
// constexpr inline auto session_compid_unauthorized         = error::session_compid_unauthorized;
// constexpr inline auto session_logout_timeout              = error::session_logout_timeout;           // slot 73 (005-era, reused)
// constexpr inline auto session_test_request_unanswered     = error::session_test_request_unanswered;  // slot 74 (005-era, reused)
// constexpr inline auto session_testreqid_mismatch          = error::session_testreqid_mismatch;       // slot 118 (new)
// constexpr inline auto session_invalid_argument            = error::session_invalid_argument;         // slot 119 (new, renumbered from 121)
//
// (The re-export shape mirrors the 012 transport_errors.hpp precedent.)

}  // namespace fixpp::core::error
