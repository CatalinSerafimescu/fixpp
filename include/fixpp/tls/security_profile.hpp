#pragma once
// include/fixpp/tls/security_profile.hpp
// fixpp::tls::SecurityProfile — explicit-choice trust-mode enum + SslCtxConfig +
// make_ssl_ctx_config + verify_peer.
//
// Design anchor: .specify/2g-tls.md v0.4 §4.5 (SecurityProfile enum + adapter
// + verify_peer + peer_identity) + §4.5.1 (normative 4-row mapping table) +
// §6.5.1 (verify_peer-time pinset access BINDING CONTRACT).
// Spec anchors: FR-013 (enum), FR-014 (TLS-version posture), FR-015/016
// (frozen at session open), FR-019 (DoS bounds at verify_peer entry),
// FR-020 (full verify_peer enforcement), FR-020a (short-circuit order Q3),
// FR-022 ([[nodiscard]]), FR-025 (named error variants).
// Data-model: E-10 (SecurityProfile), E-11 (SslCtxConfig), E-14 (verify_peer).

#include <cstddef>
#include <cstdint>
#include <fixpp/core/clock.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/tls/cert_source.hpp>
#include <fixpp/tls/certificate.hpp>
#include <fixpp/tls/cipher_policy.hpp>
#include <fixpp/tls/peer_identity.hpp>
#include <fixpp/tls/pinset.hpp>
#include <memory>
#include <memory_resource>
#include <span>

namespace fixpp::tls {

// ── SecurityProfile ───────────────────────────────────────────────────────────
// Explicit-choice enum per [const §XII.5] / FR-013.
// Re-emitted VERBATIM from 2g §4.5 lines 650-656.
// The `unset = 0` sentinel is the no-implicit-default guard; make_ssl_ctx_config
// rejects it with tls_invalid_security_profile.
// The `one_way_ca [[deprecated]]` attribute is on the ENUMERATOR DECLARATION
// per FR-013 / contracts/security_profile.hpp assertion 1.
enum class SecurityProfile : std::uint8_t {
    unset = 0,  // sentinel — rejected by make_ssl_ctx_config + Session::open.
    mtls_ca = 1,
    mtls_pinned = 2,
    one_way_ca
    [[deprecated("one_way_ca is legacy interop; prefer mtls_pinned or mtls_ca per [FIXS RC1]")]] =
        3,
};

// ── CertSourceCaps ────────────────────────────────────────────────────────────
// DoS cap fields extracted from the concrete cert_source at make_ssl_ctx_config
// time. verify_peer reads these from SslCtxConfig::caps directly — this avoids
// dynamic_cast on the handshake hot path while satisfying the "caps reachable
// through cs" design intent (caps are sourced from cs at config time).
// Default values match file_cert_source::Config defaults per [2g §1.1].
struct CertSourceCaps {
    std::size_t max_chain_depth{8};
    std::size_t max_rsa_key_bits{8192};
    std::size_t max_cert_der_bytes{16 * 1024};
    std::size_t max_san_entries{64};
};

// ── SslCtxConfig ─────────────────────────────────────────────────────────────
// Value-type description of the SSL_CTX configuration the 2h-transport feature
// must apply. Re-emitted VERBATIM from 2g §4.5 lines 670-677.
//
// CRITICAL: `pinset_snapshot` is captured ONCE by 2h's wiring at handshake
// start per [2g §6.5.1] BINDING CONTRACT. make_ssl_ctx_config leaves this
// field NULL — pre-populating at config-time would fossilize the snapshot
// across handshakes and break FR-009 rotation semantics. verify_peer scans
// this field directly without calling cfg.pinset->find/contains/snapshot.
// Under SecurityProfile::mtls_pinned, a null pinset_snapshot at verify_peer
// time returns tls_pin_empty_at_open (fail-closed).
struct SslCtxConfig {
    SecurityProfile profile{SecurityProfile::unset};
    std::shared_ptr<cert_source> cs;
    std::shared_ptr<Pinset> pinset;  // null permitted under mtls_ca + one_way_ca.
    std::shared_ptr<const pin_snapshot>
        pinset_snapshot;  // NEW-P1-1: captured once; verify_peer scans this directly.
    std::shared_ptr<fixpp::core::Clock> clock;  // effective_clock per [2d §7.9].
    CipherPolicy ciphers{};                     // value-typed; constexpr-only members.
    std::pmr::memory_resource* mr{
        nullptr};  // for peer_identity's SAN allocation; null → engine default.

    // Caps extracted from cs at make_ssl_ctx_config time so verify_peer can
    // read them without dynamic_cast or virtual dispatch on the hot path.
    CertSourceCaps caps{};
};

// ── make_ssl_ctx_config ───────────────────────────────────────────────────────
// Validates the SecurityProfile + cert_source + clock + Pinset combination
// and returns a configured SslCtxConfig ready for 2h to consume.
//
// Parameter order per 2g §4.5 lines 681-686 (NEW-P1-2 close):
//   (profile, cs, clock, pinset = nullptr, mr = nullptr)
// NO `validation_caps` parameter per NEW-P1-2.
//
// Rejection table per [2g §4.5.1]:
//   SecurityProfile::unset                → tls_invalid_security_profile
//   null cs                               → tls_invalid_security_profile
//   null clock                            → tls_invalid_security_profile
//   mtls_pinned + null pinset             → tls_invalid_security_profile
//   mtls_pinned + non-null EMPTY pinset   → tls_pin_empty_at_open (Clarify Q2)
//   one_way_ca  + non-null pinset         → tls_invalid_security_profile
//
// Implemented in src/tls/security_profile.cpp.
[[nodiscard]] core::expected_t<SslCtxConfig> make_ssl_ctx_config(
    SecurityProfile profile, std::shared_ptr<cert_source> cs,
    std::shared_ptr<fixpp::core::Clock> clock, std::shared_ptr<Pinset> pinset = nullptr,
    std::pmr::memory_resource* mr = nullptr) noexcept;

// ── verify_peer ──────────────────────────────────────────────────────────────
// Validation predicate that 2h's SSL_VERIFY_PEER callback calls after OpenSSL
// delivers the peer's certificate chain. Returns an owning peer_identity on
// accept, or an error variant on reject.
//
// SHORT-CIRCUIT evaluation: first violation hit in the FR-020a canonical order
// (Clarify Q3) — see contracts/security_profile.hpp for the full 10-step table.
// The leaf is peer_chain[0]; the chain is the full OpenSSL chain.
//
// Pinning consumes cfg.pinset_snapshot per [2g §6.5.1] BINDING CONTRACT —
// NEVER calls cfg.pinset->find/contains/snapshot during the verification window.
//
// Implemented in src/tls/verify_peer.cpp.
[[nodiscard]] core::expected_t<peer_identity> verify_peer(
    SslCtxConfig const& cfg, std::span<const Certificate> peer_chain) noexcept;

// ── last_handshake_sub_reason ─────────────────────────────────────────────────
// Thread-local string_view set by verify_peer when it returns
// tls_handshake_failed (the GROUPING variant). Carries the specific sub-reason
// for diagnostic purposes:
//   "rsa_under_min"     — RSA key < 2048 bits
//   "ecdsa_curve"       — ECDSA curve not P-256 or P-384
//   "sigalg_disallowed" — leaf key algorithm is neither RSA nor ECDSA (e.g.
//                         Ed25519/Ed448/unknown EVP_PKEY) — FR-020 envelope
//   "chain_too_deep"    — chain depth > max_chain_depth
//   "x509_v1"           — X.509 version 1 certificate
//   "expired"           — cert's not_after < clock->now()
//   "not_yet_valid"     — cert's not_before > clock->now()
//   "empty_chain"       — OpenSSL delivered zero certs to verify_peer
//
// Pragmatic v0.1 per T037 brief: enum-only return + thread-local sub-reason
// carrier (matching how dispatch_aborted doesn't carry diagnostics inline).
// The thread-local's storage is a string literal (static lifetime) — safe to
// read across call boundaries within the same thread.
//
// Implemented in src/tls/verify_peer.cpp.
[[nodiscard]] std::string_view last_handshake_sub_reason() noexcept;

}  // namespace fixpp::tls
