// SPDX-License-Identifier: AGPL-3.0-or-later
//
// fixpp — 013-session-reconnect-binding
// Contract: SessionEvent variant extensions (5 new variants on the 010 F-04
// ring-buffer accessor). NO new event channel; NO breaking change to existing
// 010 variants.
// Anchors: FR-018, FR-020, FR-021, FR-026, FR-027, FR-032, FR-035, D-10, D-12,
// D-15, D-16; [2g §6.6]:986-1004; Clarifications Q2=A, Q3=A, Q4=A.
//
// NOTE: This contract documents the FIVE NEW variants. The shipped form merges
// these into the existing 010-F-04 SessionEvent variant union; the merge
// preserves all existing 010 variants verbatim (no renumbering, no reordering
// that would break operator-side switch coverage).

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "fixpp/session/compid_authorization_policy.hpp"  // for bound_principal::source
#include "fixpp/tls/tls_verify_error.hpp"                 // for tls_verify_error (15 variants)

namespace fixpp::session {

// FR-020 / D-10 — emitted before the first application message of an Active
// session whose Logon successfully bound a CompID to a peer_identity. All
// string_view / span fields carry [[clang::lifetimebound]] discipline — they
// view into peer_identity material (lifetime = session lifetime per the 011
// peer_identity owning-by-value contract); consumer copies if it needs to
// outlive the event-emit synchronous context.
struct session_event_peer_identity_bound {
    std::string_view cn;                              // [[clang::lifetimebound]]
    std::span<std::string_view const> sans;           // [[clang::lifetimebound]]
    std::array<std::byte, 32> sha256_fingerprint;     // by-value
    std::string_view cipher;                          // [[clang::lifetimebound]]
    std::string_view bound_compid;                    // [[clang::lifetimebound]]
    bound_principal::source principal_source;         // FR-022 — which field bound
};

// FR-021 — emitted when CompIdAuthorizationPolicy::authorize returns
// session_compid_unauthorized; surfaces BEFORE the Transport closes so the
// operator's event handler can correlate with the Logout reject reason.
struct session_event_compid_authorization_failed {
    std::string_view cn;                              // [[clang::lifetimebound]]
    std::string_view asserted_compid;                 // [[clang::lifetimebound]]
    std::span<std::string_view const> expected_compids;  // empty if no binding for principal
};

// FR-026 / FR-027 — emitted when 011's verify_peer returns a tls_verify_error;
// surfaced even when no Session ever opens (handshake fails before Logon) per
// FR-028. variant is the precise [2g §6.6]:986-1004 enum value, NOT coalesced.
// FR-027 distinction: operator_config_error variants (e.g., tls_pin_empty_at_open)
// vs peer-cert errors (e.g., cert_expired) are different enum values; the consumer
// switches on variant for triage.
struct session_event_tls_validation_failed {
    fixpp::tls::tls_verify_error variant;             // precise [2g §6.6] variant
    std::string_view peer_endpoint;                   // [[clang::lifetimebound]] — "host:port"
    std::string_view reason_string;                   // [[clang::lifetimebound]] — operator-readable
};

// FR-032 / D-12 — emitted BEFORE the first handshake on the rotated cert_source
// (not at the reload_credentials call-site). Operator can correlate with the
// specific re-Logon in the session log.
struct session_event_credentials_rotated {
    std::array<std::byte, 32> old_sha256;             // by-value
    std::array<std::byte, 32> new_sha256;             // by-value
};

// FR-018 — emitted when ResetSeqNumFlag(141)=Y handshake (any of the 3 modes
// per FR-017) successfully resets both sides' sequence numbers to 1.
// by_peer_request=true → peer sent 141=Y; false → we sent 141=Y.
struct session_event_sequence_numbers_reset {
    bool by_peer_request;
};

// SessionEvent (the existing 010-F-04 variant union) gains these 5 alternatives.
// The shipped form lives at include/fixpp/session/session_event.hpp; this
// contract documents only the 013 delta.
//
// Approximate shipped shape (010 variants elided):
//   using SessionEvent = std::variant<
//       /* existing 010 variants */,
//       session_event_peer_identity_bound,
//       session_event_compid_authorization_failed,
//       session_event_tls_validation_failed,
//       session_event_credentials_rotated,
//       session_event_sequence_numbers_reset
//   >;

}  // namespace fixpp::session
