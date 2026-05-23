// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Contract shape oracle for 011-tls-policy FR-018 + Key Entity E-12 +
// T-041 cross-cut handoff to the session/ Phase-4 module.
//
// Design anchor: .specify/2g-tls.md v0.4 §4.5 lines 706-724 (peer_identity
// value type — owning per RC#1 / N-P2-2 close) + Appendix A row T-041.
// Shape re-emitted to mirror 2g §4.5 verbatim — owning std::pmr::string
// subject_dn + owning vectors for SAN DNS names / URIs + 32-byte
// leaf_fingerprint.
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
//     // Owning storage — PMR-allocated so caller controls the allocator
//     // through cfg.mr at verify_peer time. SAN vectors are bounded ≤
//     // max_san_entries (D-10 default 64).
//     //
//     // Field shape mirrors 2g §4.5 lines 706-710 verbatim.
//     std::pmr::string                            subject_dn;            // owned, PMR-allocated.
//     std::pmr::vector<std::pmr::string>          san_dns_names_owned;   // owned, PMR-allocated; bounded by max_san_entries.
//     std::pmr::vector<std::pmr::string>          san_uris_owned;        // owned, PMR-allocated; bounded by max_san_entries.
//     std::array<std::byte, 32>                   leaf_fingerprint;      // owned (32 bytes); SHA-256 of the leaf's DER.
//
//     // Convenience views — bounded by *this. The owning vectors above are
//     // the storage; these accessors carry [[clang::lifetimebound]] at the
//     // declaration site mirroring the [2c §4.8] owning_message_t<>
//     // accessor pattern (2g §4.5 lines 714-723 verbatim).
//     [[nodiscard]] std::string_view
//         subject_dn_view() const noexcept [[clang::lifetimebound]];
//
//     [[nodiscard]] std::span<const std::pmr::string>
//         san_dns_names() const noexcept [[clang::lifetimebound]];
//
//     [[nodiscard]] std::span<const std::pmr::string>
//         san_uris() const noexcept [[clang::lifetimebound]];
//
//     // Carried for the session FSM's effective-clock-aware expiry checks
//     // (D-9 / [2d §7.9]). Not in 2g §4.5 verbatim but additive — does not
//     // contradict the upstream surface.
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
