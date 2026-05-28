// SPDX-License-Identifier: AGPL-3.0-or-later
//
// fixpp — 013-session-reconnect-binding
// Contract: error::session_* variant family extension.
// Anchors: FR-008, FR-017, FR-021, D-7, D-9, D-13; [const §X.2] ABI append-only.
//
// NOTE: This contract documents the FIVE NEW slots 116..120 appended to
// include/fixpp/core/error.hpp after 012's transport_* block. The /speckit-implement-time
// rewriter MUST cross-check the actual file to confirm the boundary remains at
// 115 (transport_accept_cancelled = 115 per [2h §6.6]:1199); future ±N
// adjustment is reconciled at /implement-time without re-running Gate A.
// NEVER renumber existing slots.

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
//   slot 118: session_logout_disconnect_timeout
//
// FR-004 / US1 AC4 — inbound traffic absent for 2× HeartBtInt; emitted Logout
// + closed the Transport.
//   slot 119: session_heartbeat_timeout
//
// FR-006 — inbound Heartbeat(0) carried TestReqID(112) that does NOT match
// the most recent outbound TestRequest's TestReqID; per FIX-SL §4.5.4 this is
// a session-level error.
//   slot 120: session_testreqid_mismatch
//
// C-ABI coalescing (owned by 2i, not 013): all 5 slots join the existing
// FIXPP_ERR_SESSION group at the C-ABI boundary. NO new C-ABI symbol.

// Forward declarations for downstream re-export ergonomics (the canonical
// definitions land in include/fixpp/core/error.hpp at slots 116..120).
//
// constexpr inline auto session_seqnum_reset_mismatch       = error::session_seqnum_reset_mismatch;
// constexpr inline auto session_compid_unauthorized         = error::session_compid_unauthorized;
// constexpr inline auto session_logout_disconnect_timeout   = error::session_logout_disconnect_timeout;
// constexpr inline auto session_heartbeat_timeout           = error::session_heartbeat_timeout;
// constexpr inline auto session_testreqid_mismatch          = error::session_testreqid_mismatch;
//
// (The re-export shape mirrors the 012 transport_errors.hpp precedent.)

}  // namespace fixpp::core::error
