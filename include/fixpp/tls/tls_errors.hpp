#pragma once
// include/fixpp/tls/tls_errors.hpp
// Re-export aliases for the 16 tls_* error variants defined in
// include/fixpp/core/error.hpp (slots 78–93). All definitions live in
// core/error.hpp; this header provides the fixpp::tls::errors namespace
// convenience aliases per data-model.md E-15 / [2g §6.6] lines 1006-1013.
//
// C-ABI coalescing groups (owned by 2i; no extern "C" surface added here):
//   FIXPP_ERR_TLS_CONFIG     ← tls_cert_load_failed, tls_cert_parse_failed,
//                               tls_cipher_not_allowed, tls_invalid_security_profile,
//                               tls_sign_callback_unavailable, tls_pin_empty_at_open
//   FIXPP_ERR_TLS_PINSET    ← tls_pin_not_found, tls_pin_already_present,
//                               tls_pinset_capacity_exhausted
//   FIXPP_ERR_TLS_RUNTIME   ← tls_pinset_alloc_failed
//   FIXPP_ERR_TLS_HANDSHAKE ← tls_handshake_failed, tls_rsa_key_too_large,
//                               tls_cert_der_too_large, tls_san_entries_exceeded,
//                               tls_pin_mismatch
//   FIXPP_ERR_CANCELLED     ← tls_load_cancelled  (reuses the shared cancellation
//                               group with dispatch_aborted / clock_sleeps_cancelled
//                               / store_cancelled)

#include <fixpp/core/error.hpp>

namespace fixpp::tls::errors {

// ── FIXPP_ERR_TLS_CONFIG ─────────────────────────────────────────────────────
inline constexpr auto tls_cert_load_failed        = ::fixpp::core::error::tls_cert_load_failed;
inline constexpr auto tls_cert_parse_failed       = ::fixpp::core::error::tls_cert_parse_failed;
inline constexpr auto tls_cipher_not_allowed      = ::fixpp::core::error::tls_cipher_not_allowed;
inline constexpr auto tls_invalid_security_profile = ::fixpp::core::error::tls_invalid_security_profile;
inline constexpr auto tls_sign_callback_unavailable = ::fixpp::core::error::tls_sign_callback_unavailable;
inline constexpr auto tls_pin_empty_at_open       = ::fixpp::core::error::tls_pin_empty_at_open;

// ── FIXPP_ERR_TLS_PINSET ─────────────────────────────────────────────────────
inline constexpr auto tls_pin_not_found           = ::fixpp::core::error::tls_pin_not_found;
inline constexpr auto tls_pin_already_present     = ::fixpp::core::error::tls_pin_already_present;
inline constexpr auto tls_pinset_capacity_exhausted = ::fixpp::core::error::tls_pinset_capacity_exhausted;

// ── FIXPP_ERR_TLS_RUNTIME ────────────────────────────────────────────────────
inline constexpr auto tls_pinset_alloc_failed     = ::fixpp::core::error::tls_pinset_alloc_failed;

// ── FIXPP_ERR_TLS_HANDSHAKE ──────────────────────────────────────────────────
inline constexpr auto tls_handshake_failed        = ::fixpp::core::error::tls_handshake_failed;
inline constexpr auto tls_rsa_key_too_large       = ::fixpp::core::error::tls_rsa_key_too_large;
inline constexpr auto tls_cert_der_too_large      = ::fixpp::core::error::tls_cert_der_too_large;
inline constexpr auto tls_san_entries_exceeded    = ::fixpp::core::error::tls_san_entries_exceeded;
inline constexpr auto tls_pin_mismatch            = ::fixpp::core::error::tls_pin_mismatch;

// ── FIXPP_ERR_CANCELLED (reused) ─────────────────────────────────────────────
inline constexpr auto tls_load_cancelled          = ::fixpp::core::error::tls_load_cancelled;

}  // namespace fixpp::tls::errors
