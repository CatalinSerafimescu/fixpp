// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Contract shape oracle for 011-tls-policy FR-018 + Key Entity E-12 +
// T-041 cross-cut handoff to the session/ Phase-4 module.
//
// Design anchor: .specify/2g-tls.md v0.4 §4.5 (peer_identity value type) +
// Appendix A row T-041.
// Spec anchors: spec.md FR-018 (this feature owns the VALUE; session
// Phase-4 owns the CompID-to-TLS-identity BINDING); data-model.md E-12.

#pragma once

// ============================================================================
// peer_identity — owning value type derived from a verified peer cert.
//
// Built by verify_peer (contracts/security_profile.hpp) on accept; consumed
// by the session-FSM Phase-4 feature for the T-041 CompID binding (e.g.,
// session refuses Logon if peer_identity.subject_dn or any
// san_dns_names entry doesn't match the configured TLS identity for the
// counterparty's CompID).
//
// 2g owns:   the peer_identity VALUE TYPE.
// session/:  the CompID-to-peer_identity BINDING POLICY + the
//            error::session_identity_mismatch refusal.
// ============================================================================
//
// namespace fixpp::tls {
//
// struct peer_identity {
//     // Owning storage — PMR-allocated so caller controls the allocator.
//     // SAN vectors are bounded ≤ max_san_entries (D-10 default 64).
//
//     std::pmr::string                            subject_dn_owned;
//     std::pmr::vector<std::pmr::string>          san_dns_names_owned;
//     std::pmr::vector<std::pmr::string>          san_uris_owned;
//
//     // The leaf's SHA-256-of-DER fingerprint (the Pinset key per Clarify
//     // Q4) — carried so the session FSM can log / record / re-pin if it
//     // chooses (the rotation API itself is on Pinset, not on peer_identity).
//     pin_fingerprint                             sha256;
//
//     // Carried for the session FSM's effective-clock-aware expiry checks
//     // (D-9 / [2d §7.9]).
//     std::chrono::system_clock::time_point       not_after;
// };
//
// }  // namespace fixpp::tls
//
// ============================================================================
// Contract assertions (verified at /speckit-verify):
//
//   1. peer_identity is constructed EXCLUSIVELY by verify_peer (no public
//      constructor that takes raw DER; this guarantees the value has been
//      validated against the full FR-020a chain).
//   2. subject_dn_owned + SAN vectors are PMR-allocated; the allocator is
//      passed through from the cert_source::Config / cfg path.
//   3. san_dns_names_owned.size() + san_uris_owned.size() ≤ max_san_entries.
//   4. sha256 == Certificate::sha256() of the leaf passed to verify_peer
//      (Pinset key consistency).
//   5. peer_identity is move-constructible and copy-constructible across
//      allocator boundaries (PMR uses std::scoped_allocator_adaptor pattern
//      implicit in std::pmr containers).
//   6. The session/ Phase-4 feature consumes peer_identity through the
//      `SessionConfig::tls_identity_predicate` hook (declared in session/);
//      THAT predicate's contract is owned by session Phase-4, NOT here.
