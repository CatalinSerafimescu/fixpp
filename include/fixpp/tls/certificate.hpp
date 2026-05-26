#pragma once
// include/fixpp/tls/certificate.hpp
// fixpp::tls::Certificate — value-typed VIEW over a parsed peer/leaf cert.
// Declared per data-model.md E-12 / [2g §4.5]. Storage is owned by the
// producing cert_source; Certificate fields are back-pointers (spans /
// string_views). Lifetime of any Certificate value is bounded by the
// cert_source instance that minted it.
//
// Every non-owning view accessor carries [[clang::lifetimebound]] at the
// declaration site per [arch §5.5] / [2b §6.4] precedent.
//
// [[nodiscard]] is on every accessor per [arch §5.3] (expected_t returns
// are always nodiscard; non-owning view accessors are nodiscard by
// convention — callers that ignore the returned span are bugs).

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/error.hpp>
#include <memory_resource>
#include <span>
#include <string_view>

namespace fixpp::tls {

// ── signature_algorithm ───────────────────────────────────────────────────────
// Per data-model.md E-12 / [2g §4.5]. Values map to OpenSSL EVP_PKEY_id().
enum class signature_algorithm : std::uint8_t {
    unspecified = 0,  // Not yet determined or unsupported algorithm.
    rsa_pss = 1,      // RSA-PSS (EVP_PKEY_RSA / EVP_PKEY_RSA_PSS).
    ecdsa = 2,        // ECDSA (EVP_PKEY_EC).
};

// ── ecdsa_curve ───────────────────────────────────────────────────────────────
// Per data-model.md E-12 / [2g §4.5]. Only p256 and p384 are permitted by
// CipherPolicy (verify_peer enforces at handshake time; cert parse sets this).
enum class ecdsa_curve : std::uint8_t {
    unspecified = 0,  // Not ECDSA, or curve not recognised.
    p256 = 1,         // secp256r1 / NID_X9_62_prime256v1.
    p384 = 2,         // secp384r1 / NID_secp384r1.
};

// ── Certificate ───────────────────────────────────────────────────────────────
// Value-typed view over one parsed X.509 certificate. Fields are back-pointers
// into cert_source-owned storage; the Certificate value itself is cheap to copy
// (≈ 256 bytes). All string/span fields are non-owning.
//
// Data-model E-12 field list is verbatim; see also [2g §4.5].
struct Certificate {
    // Raw DER bytes — the canonical representation, also the Pinset key source.
    [[nodiscard]] std::span<const std::byte> raw_der() const noexcept [[clang::lifetimebound]] {
        return raw_der_;
    }

    // Subject distinguished name (UTF-8 one-line representation).
    [[nodiscard]] std::string_view subject_dn() const noexcept [[clang::lifetimebound]] {
        return subject_dn_;
    }

    // Issuer distinguished name.
    [[nodiscard]] std::string_view issuer_dn() const noexcept [[clang::lifetimebound]] {
        return issuer_dn_;
    }

    // SAN DNS names (bounded by max_san_entries).
    [[nodiscard]] std::span<const std::string_view> san_dns_names() const noexcept
        [[clang::lifetimebound]] {
        return san_dns_names_;
    }

    // SAN URI names (bounded by max_san_entries together with san_dns_names).
    [[nodiscard]] std::span<const std::string_view> san_uris() const noexcept
        [[clang::lifetimebound]] {
        return san_uris_;
    }

    // SHA-256 of the raw DER bytes — the Pinset fingerprint key per /clarify Q4.
    [[nodiscard]] std::array<std::byte, 32> sha256() const noexcept { return sha256_; }

    // X.509 version (1, 2, or 3; v1 rejected by verify_peer).
    [[nodiscard]] int x509_version() const noexcept { return x509_version_; }

    // Certificate validity window — absolute wall-clock times parsed from DER.
    // Comparison against effective_clock->now() happens in verify_peer per
    // [2d §7.9] / data-model.md E-14 (NEW-P2-4 close).
    [[nodiscard]] std::chrono::system_clock::time_point not_before() const noexcept {
        return not_before_;
    }

    [[nodiscard]] std::chrono::system_clock::time_point not_after() const noexcept {
        return not_after_;
    }

    // Signature algorithm (rsa_pss / ecdsa / unspecified).
    [[nodiscard]] signature_algorithm alg() const noexcept { return alg_; }

    // RSA key size in bits — only meaningful when alg() == rsa_pss; else 0.
    [[nodiscard]] std::size_t rsa_key_bits() const noexcept { return rsa_key_bits_; }

    // ECDSA curve — only meaningful when alg() == ecdsa; else unspecified.
    [[nodiscard]] ecdsa_curve curve() const noexcept { return curve_; }

    // ── Storage (back-pointers; cert_source-owned) ────────────────────────────
    std::span<const std::byte> raw_der_;
    std::string_view subject_dn_;
    std::string_view issuer_dn_;
    std::span<const std::string_view> san_dns_names_;
    std::span<const std::string_view> san_uris_;
    std::array<std::byte, 32> sha256_{};
    int x509_version_ = 0;
    std::chrono::system_clock::time_point not_before_;
    std::chrono::system_clock::time_point not_after_;
    signature_algorithm alg_ = signature_algorithm::unspecified;
    std::size_t rsa_key_bits_ = 0;
    ecdsa_curve curve_ = ecdsa_curve::unspecified;
};

// ── parse_certificate_der ─────────────────────────────────────────────────────
// DER-bytes-in → Certificate-out. Allocates all string/span storage into `mr`.
// The raw DER bytes are NOT copied; the caller's storage must outlive the
// returned Certificate. Implemented in src/tls/certificate.cpp.
//
// On parse failure         → unexpected{error::tls_cert_parse_failed}.
// On PMR allocation failure → unexpected{error::tls_cert_parse_failed} (the
//   bad_alloc from mr.allocate() is routed through [2a §4.2] trap_throw per
//   [2g §1 item 7] / [2g §6.6]; it never escapes as a thrown exception.
[[nodiscard]] core::expected_t<Certificate> parse_certificate_der(
    std::span<const std::byte> der, std::pmr::memory_resource& mr);

}  // namespace fixpp::tls
