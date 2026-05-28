// SPDX-License-Identifier: AGPL-3.0-or-later
//
// fixpp — 013-session-reconnect-binding
// Contract: SessionEvent — NEW PUBLIC VARIANT UNION introduced by 013.
// Anchors: FR-018, FR-020, FR-021, FR-026, FR-027, FR-032, FR-035, D-10, D-12,
// D-15, D-16; shipped `include/fixpp/core/error.hpp:403-429` (6-cell
// master-enum surface); shipped `include/fixpp/tls/security_profile.hpp:121-144`
// (verify_peer + last_handshake_sub_reason); Clarifications Q2=A, Q3=A, Q4=A.
//
// NOTE: SessionEvent is a NEW 013-introduced public type — it is NOT an
// extension of any shipped 010 variant union. 010 F-04 shipped
// `Session::fsm_visit_history() const noexcept -> std::span<const fsm_state>`
// (`include/fixpp/session/session.hpp:237-250`), a fixed 16-entry std::array
// ring of FSM-state enum values (FSM-state observation; NOT a variant-event
// channel). The 010 F-04 accessor remains UNCHANGED and complementary.
//
// 013 introduces:
//   1. `using SessionEvent = std::variant<...>` — this header (5 initial
//      alternatives; future features append append-only).
//   2. `Session::recent_events() const noexcept -> std::span<const SessionEvent>` —
//      NEW 013 ring-buffer accessor (declared in session_ext.hpp / shipped
//      session.hpp).

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <variant>

#include "fixpp/core/error.hpp"                           // for error::code (master enum)
#include "fixpp/session/compid_authorization_policy.hpp"  // for bound_principal::source

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
// CHK015/CHK018 SPEC-FIXED: principal_source added for symmetric operator audit —
// carries which cert field was extracted as the principal before the compid
// mismatch was detected (mirrors peer_identity_bound::principal_source).
// expected_compids is empty if the principal was not found in the policy at all
// (unrecognized principal), or contains the authorized set if the principal was
// found but the asserted_compid was not in the set.
struct session_event_compid_authorization_failed {
    std::string_view cn;                              // [[clang::lifetimebound]]
    std::string_view asserted_compid;                 // [[clang::lifetimebound]]
    std::span<std::string_view const> expected_compids;  // empty if no binding for principal
    bound_principal::source principal_source;         // CHK015 — which cert field was extracted
};

// FR-026 / FR-027 — emitted when 011's verify_peer returns an error::tls_*
// master-enum variant; surfaced even when no Session ever opens (handshake
// fails before Logon) per FR-028. `code` is the precise master-enum value per
// shipped `include/fixpp/core/error.hpp:403-429` (6 cells: tls_handshake_failed
// GROUPING + tls_rsa_key_too_large + tls_cert_der_too_large +
// tls_san_entries_exceeded + tls_pin_mismatch + tls_load_cancelled). The 10+
// sub-reasons collapsed under tls_handshake_failed (RSA-low / ECDSA-curve /
// expired / sigalg-disallowed / etc.) surface via the `sub_reason` field
// captured from 011's thread-local last_handshake_sub_reason() at the moment
// verify_peer returned.
//
// FR-027 distinction: operator-config errors (e.g., sub_reason="tls_pin_empty_at_open")
// and peer-cert errors (e.g., code=tls_pin_mismatch or code=tls_handshake_failed
// with sub_reason="expired") are discriminated by the (code, sub_reason) pair;
// consumers triage by switching on `code` first, then on `sub_reason` when
// code == tls_handshake_failed.
//
// Sub-reason capture semantics: sub_reason is captured BY COPY into a session-
// arena string at event-emit time (NOT a view into 011's thread-local storage);
// the consumer may further copy if it needs to outlive the event-emit
// synchronous context.
struct session_event_tls_validation_failed {
    fixpp::core::error code;                          // precise master-enum variant (6 cells)
    std::string_view sub_reason;                      // [[clang::lifetimebound]] — 011 sub-reason
                                                      // diagnostic ("expired" / "rsa_under_min" /
                                                      // "sigalg_disallowed" / "tls_pin_empty_at_open" /
                                                      // ...); empty when code is one of the 5 specific
                                                      // tls_* variants
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

// SessionEvent — NEW 013-introduced public variant union. 5 initial
// alternatives; future features APPEND alternatives append-only (consumer-side
// std::visit fall-throughs are responsible for tolerating future variants).
//
// The shipped form lives at include/fixpp/session/session_event.hpp:
using SessionEvent = std::variant<
    session_event_peer_identity_bound,
    session_event_compid_authorization_failed,
    session_event_tls_validation_failed,
    session_event_credentials_rotated,
    session_event_sequence_numbers_reset
>;

// Companion accessor on Session (declared in session_ext.hpp / shipped
// session.hpp; mentioned here for cross-reference):
//
//   class Session {
//   public:
//       // 010 F-04 — UNCHANGED, complementary, FSM-state observation:
//       std::span<const fsm_state> fsm_visit_history() const noexcept;
//
//       // 013-introduced ring-buffer accessor for SessionEvent observability:
//       std::span<const SessionEvent> recent_events() const noexcept;
//   };
//
// Membership-witness semantics per 010 F-04 contract (NOT chronologically
// ordered; tests assert via Contains / std::find).

}  // namespace fixpp::session
