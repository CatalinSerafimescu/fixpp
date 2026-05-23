// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Contract shape oracle for 011-tls-policy FR-025 + Key Entity E-14.
//
// Design anchor: .specify/2g-tls.md v0.4 §6.6 (tls_handshake_failed grouping
// + tls_load_failed grouping) + §6.7 (full variant enumeration).
// Spec anchors: spec.md FR-025 (minimum 13 variants incl. tls_pin_empty_at_open
// from Clarify Q2), FR-020a sub-reasons.
//
// The actual enum-variant declarations are APPENDED to
// include/fixpp/core/error.hpp at the next free contiguous slot range after
// slot 77 (010-session-cfg-lifetime's last addition). This file is a
// CONTRACT ORACLE listing the binding variants + the per-doc-prefix
// FIXPP_ERR_TLS_* coalescing rule the 2i C-ABI bridge consumes.

#pragma once

// ============================================================================
// error::tls_* variants — appended to fixpp::core::error enum.
//
// Slot allocation is at /speckit-implement time; the rule is "next free
// contiguous range after slot 77, NEVER renumber prior slots". Per
// [[project_2e_design_doc_only_seqnum_handoff]] precedent.
// ============================================================================
//
// namespace fixpp::core {
//
// enum class error : std::uint8_t {
//     // ... existing variants 0..77 unchanged (decimal / wire / codegen /
//     //     session / msgstore / async_mutex / threading / message_store /
//     //     session-fsm-finalize / session-cfg-lifetime slots) ...
//
//     // ── 011-tls-policy: error::tls_* variants (slots TBD at /implement) ──
//
//     tls_load_failed              = 78,   // cert_source::load_credentials cold-path failure (file I/O, parse, chain build, PMR throw via trap_throw)
//     tls_load_cancelled           = 79,   // ASIO cancellation slot fired (FR-021)
//
//     tls_cert_invalid             = 80,   // generic parse-time / structural rejection (X.509 v1, ECDSA curve outside P-256/P-384, etc.); diagnostic field carries sub-reason
//     tls_cert_expired             = 81,   // notBefore > now OR notAfter < now against effective_clock
//     tls_cert_chain_too_deep      = 82,   // chain.size() + 1 > caps.max_chain_depth
//     tls_cert_der_too_large       = 83,   // per-cert DER > caps.max_cert_der_bytes (DoS bound)
//
//     tls_rsa_key_too_small        = 84,   // RSA key bits < 2048 ([FIXS §3.4] lower bound)
//     tls_rsa_key_too_large        = 85,   // RSA key bits > caps.max_rsa_key_bits (DoS bound; default 8192)
//
//     tls_san_entries_exceeded     = 86,   // DNS+URI SAN cardinality > caps.max_san_entries (DoS bound; default 64)
//
//     tls_cert_pin_mismatch        = 87,   // mtls_pinned + leaf SHA-256 not in published Pinset snapshot
//     tls_pin_empty_at_open        = 88,   // (Clarify Q2) mtls_pinned + non-null empty Pinset at session open — distinct from tls_cert_pin_mismatch
//
//     tls_invalid_security_profile = 89,   // SecurityProfile::unset OR mtls_pinned+null-pinset OR one_way_ca+non-null-pinset OR null clock
//
//     tls_cipher_not_allowed       = 90,   // CipherPolicy::is_allowed returned false for negotiated suite (runtime path; compile-time path fails earlier)
//
//     pin_cap_exceeded             = 91,   // Pinset::add() at max_pins
//     pin_not_present              = 92,   // Pinset::remove() of an absent fingerprint
//
//     // ... end of 011-tls-policy variants ...
// };
//
// }  // namespace fixpp::core
//
// ============================================================================
// Per-doc-prefix C-ABI coalescing rule (owned by 2i; declared here)
//
// The 2i C-ABI bridge projects fixpp::core::error::tls_* variants into a
// per-doc-prefix FIXPP_ERR_TLS_* group of C codes. Coalescing rule:
//
//   fixpp::core::error::tls_<variant>   →   FIXPP_ERR_TLS_<UPPER_VARIANT>
//
// Example projections (final code numbers assigned by 2i):
//
//   tls_load_failed              →   FIXPP_ERR_TLS_LOAD_FAILED
//   tls_load_cancelled           →   FIXPP_ERR_TLS_LOAD_CANCELLED
//   tls_cert_invalid             →   FIXPP_ERR_TLS_CERT_INVALID
//   tls_cert_pin_mismatch        →   FIXPP_ERR_TLS_CERT_PIN_MISMATCH
//   tls_pin_empty_at_open        →   FIXPP_ERR_TLS_PIN_EMPTY_AT_OPEN
//   tls_invalid_security_profile →   FIXPP_ERR_TLS_INVALID_SECURITY_PROFILE
//   tls_cipher_not_allowed       →   FIXPP_ERR_TLS_CIPHER_NOT_ALLOWED
//   pin_cap_exceeded             →   FIXPP_ERR_PIN_CAP_EXCEEDED          (no "tls" prefix; this is a Pinset op error, not a TLS error)
//   pin_not_present              →   FIXPP_ERR_PIN_NOT_PRESENT           (same)
//
// 2i's contracts/ will mirror this list with the final C code numbers when
// the 2i Phase-4 feature ships.
//
// ============================================================================
// Contract assertions (verified at /speckit-verify):
//
//   1. include/fixpp/core/error.hpp contains all 15 listed variants (FR-025
//      minimum was 13; tls_pin_empty_at_open from Clarify Q2 + the two
//      Pinset op errors bring the total to 15).
//   2. NO renumbering of pre-slot-77 variants (precedent rule per
//      [[project_2e_design_doc_only_seqnum_handoff]]).
//   3. Each new variant has a corresponding `tests/tls/test_*` test that
//      witnesses ITS specific rejection path (FR-020a / FR-025).
//   4. The `tls_handshake_failed` grouping (cipher / cert_invalid /
//      cert_expired / cert_chain_too_deep / cert_der_too_large /
//      rsa_key_too_* / san_entries_exceeded / cert_pin_mismatch /
//      cipher_not_allowed) is the verify_peer-time return surface;
//      tls_load_failed / tls_load_cancelled / tls_invalid_security_profile /
//      tls_pin_empty_at_open are the construction-time return surface
//      (return from make_ssl_ctx_config or make_file_cert_source).
