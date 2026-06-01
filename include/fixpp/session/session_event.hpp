// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/session/session_event.hpp
//
// fixpp::session::SessionEvent — NEW PUBLIC VARIANT UNION introduced by 013.
// Anchors: FR-018, FR-020, FR-021, FR-026, FR-027, FR-032, FR-035, D-10, D-12,
// D-15, D-16; data-model.md §E-5; contracts/session_event.hpp;
// Clarifications Q2=A, Q3=A, Q4=A.
//
// NOTE: SessionEvent is a NEW 013-introduced public type — it is NOT an
// extension of any shipped 010 variant union. 010 F-04 shipped
// `Session::fsm_visit_history() const noexcept -> std::span<const fsm_state>`
// (`include/fixpp/session/session.hpp`), a fixed 16-entry std::array ring of
// FSM-state enum values (FSM-state observation; NOT a variant-event channel).
// The 010 F-04 accessor remains UNCHANGED and complementary.
//
// 013 introduces:
//   1. `using SessionEvent = std::variant<...>` — this header (5 initial
//      alternatives; future features append append-only).
//   2. `Session::recent_events() const noexcept -> std::span<const SessionEvent>`
//      — NEW 013 ring-buffer accessor (declared in session.hpp by T013a).
//
// [[clang::lifetimebound]] discipline: every string_view / span field that views
// into caller-managed storage carries the attribute. Consumers MUST copy if
// they need to outlive the event-emit synchronous context. [data-model §E-5]
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

// Capacity constant for the Session's recent_events() ring and the
// ListenerEvents ring. Published here so both consumers share the same
// literal. [FR-035 v1.0 contract]
inline constexpr std::size_t kSessionEventRingCapacity = 16;
static_assert(kSessionEventRingCapacity == 16,
              "FR-035 v1.0 contract: ring capacity is 16 entries; future "
              "capacity changes require a spec amendment + Gate A re-review");

// FR-020 / D-10 — emitted before the first application message of an Active
// session whose Logon successfully bound a CompID to a peer_identity. All
// string_view / span fields carry [[clang::lifetimebound]] discipline — they
// view into peer_identity material (lifetime = session lifetime per the 011
// peer_identity owning-by-value contract); consumer copies if it needs to
// outlive the event-emit synchronous context.
struct session_event_peer_identity_bound {
    std::string_view cn;  // lifetimebound: view into peer_identity (session lifetime)
    std::span<std::string_view const> sans;        // lifetimebound: view into peer_identity
    std::array<std::byte, 32> sha256_fingerprint;  // by-value
    std::string_view cipher;                       // lifetimebound: view into peer_identity
    std::string_view bound_compid;                 // lifetimebound: view into peer_identity
    bound_principal::source principal_source;      // FR-022 — which field bound
};

// FR-021 — emitted when CompIdAuthorizationPolicy::authorize returns
// session_compid_unauthorized; surfaces BEFORE the Transport closes so the
// operator's event handler can correlate with the Logout reject reason.
// principal_source records which cert field was extracted as the principal
// before the compid mismatch was detected (mirrors peer_identity_bound).
// expected_compids is empty if the principal was not found in the policy at
// all (unrecognized principal), or contains the authorized set if the
// principal was found but the asserted_compid was not in the set.
struct session_event_compid_authorization_failed {
    std::string_view cn;               // lifetimebound: view into peer_identity
    std::string_view asserted_compid;  // lifetimebound: view into caller's compid string
    std::span<std::string_view const>
        expected_compids;  // lifetimebound: empty if unrecognized principal
    bound_principal::source principal_source;
};

// FR-026 / FR-027 — emitted when 011's verify_peer returns an error::tls_*
// master-enum variant; surfaced even when no Session ever opens (handshake
// fails before Logon) per FR-028. `code` is the precise master-enum value per
// shipped `include/fixpp/core/error.hpp` (6 cells: tls_handshake_failed
// GROUPING + tls_rsa_key_too_large + tls_cert_der_too_large +
// tls_san_entries_exceeded + tls_pin_mismatch + tls_load_cancelled). The 10+
// sub-reasons collapsed under tls_handshake_failed surface via the `sub_reason`
// field captured from 011's thread-local last_handshake_sub_reason() at the
// moment verify_peer returned.
//
// sub_reason capture semantics: sub_reason is captured BY COPY into a session-
// arena string at event-emit time (NOT a view into 011's thread-local storage);
// the consumer may further copy if it needs to outlive the event-emit
// synchronous context.
struct session_event_tls_validation_failed {
    fixpp::core::error code;         // precise master-enum variant (6 tls_* cells)
    std::string_view sub_reason;     // lifetimebound: 011 sub-reason diagnostic
    std::string_view peer_endpoint;  // lifetimebound: "host:port"
    std::string_view reason_string;  // lifetimebound: operator-readable
};

// FR-032 / D-12 — emitted BEFORE the first handshake on the rotated cert_source
// (not at the reload_credentials call-site). Operator can correlate with the
// specific re-Logon in the session log. Both fields are by-value SHA-256.
struct session_event_credentials_rotated {
    std::array<std::byte, 32> old_sha256;  // by-value
    std::array<std::byte, 32> new_sha256;  // by-value
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
// [data-model §E-5]
using SessionEvent =
    std::variant<session_event_peer_identity_bound, session_event_compid_authorization_failed,
                 session_event_tls_validation_failed, session_event_credentials_rotated,
                 session_event_sequence_numbers_reset>;

}  // namespace fixpp::session
