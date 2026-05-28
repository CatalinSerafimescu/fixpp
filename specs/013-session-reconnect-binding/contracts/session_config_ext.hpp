// SPDX-License-Identifier: AGPL-3.0-or-later
//
// fixpp — 013-session-reconnect-binding
// Contract: SessionConfig extensions (3 new fields).
// Anchors: FR-008, FR-017, FR-023, D-6, D-9, D-13; [arch §5.6] frozen-at-open;
// Clarifications Q1=A, Q3=A, Q5=A.
//
// NOTE: This is the Phase-1 contract documenting the SessionConfig DELTA. The
// shipped form lands at include/fixpp/session/session_config.hpp post-/speckit-implement
// — the 3 fields below are APPENDED to 010's shipped SessionConfig struct
// (preserves operator API + ABI per [arch §5.6] frozen-at-open carve-out).

#pragma once

#include <chrono>
#include <cstdint>

#include "fixpp/session/compid_authorization_policy.hpp"

namespace fixpp::session {

// FR-017 / D-6 — operator-configured per-session policy for ResetSeqNumFlag(141)=Y
// handshake. Default = bilateral_strict per [const §VI] security-default-deny +
// 2-of-3 industry convergence (QFC + QFJ favour bilateral; Fix8 unilateral is the
// outlier). Three modes:
//   - bilateral_strict — refuse Logon if peer's response lacks 141=Y (when we
//     sent 141=Y); QFJ-style.
//   - bilateral_lenient — auto-mirror 141=Y in our response Logon when peer
//     sends 141=Y; QFC-style.
//   - unilateral — honour any received 141=Y regardless of our outbound flag;
//     Fix8-style.
// Per-session granularity (not engine-wide) so multi-tenant acceptors can pair
// counterparties running different engines.
enum class reset_seqnum_policy : std::uint8_t {
    bilateral_strict = 0,
    bilateral_lenient = 1,
    unilateral = 2,
};

// SessionConfig — extends 010's shipped form. The 3 new fields are APPENDED;
// existing 010 fields are UNCHANGED in shape, name, type, and offset.
struct SessionConfig {
    // ... existing 010 fields (operator-set at SessionConfig-build time) ...
    // (cert_source / message_store_factory / executor / lock_policy / dialect_overlay
    //  / heartbeat_interval / store_factory / ... — see include/fixpp/session/session_config.hpp)

    // FR-017 / Clarifications Q1=A — per-session ResetSeqNumFlag policy.
    // Default bilateral_strict.
    fixpp::session::reset_seqnum_policy reset_seqnum_policy{
        fixpp::session::reset_seqnum_policy::bilateral_strict};

    // FR-008 / Clarifications Q5=A — initiator-graceful Logout disconnect
    // timeout. Default 2000 ms (matches QuickFIX/J SessionState.logoutTimeoutMs=2000L).
    // Operator MAY override; validation at SessionConfig-build time rejects
    // value == 0 ms per [arch §5.3] construction-time carve-out.
    std::uint32_t logout_disconnect_timeout_ms{2000};

    // FR-023 / Clarifications Q3=A — operator-supplied allow-list of
    // {principal → {compid_set}} bindings. Default-constructed = empty
    // allow-list = default-deny (rejects ALL Logons; operator MUST enumerate
    // bindings before opening any session).
    fixpp::session::CompIdAuthorizationPolicy compid_authorization_policy{};
};

}  // namespace fixpp::session
