// tests/tls/test_tls_error_variants.cpp
// T037 — seam #12: exercise every error::tls_* variant (16 total).
//
// Design anchor: data-model.md E-15 / [2g §6.6] / contracts/security_profile.hpp.
// Spec anchors: FR-025 (named error variants).
//
// For tls_handshake_failed (GROUPING variant), also asserts the thread-local
// sub-reason carrier set by verify_peer per the pragmatic v0.1 design.

#include <gtest/gtest.h>

#include <array>
#include <asio/co_spawn.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <fixpp/core/error.hpp>
#include <fixpp/tls/pinset.hpp>
#include <fixpp/tls/security_profile.hpp>
#include <fixpp/tls/tls_errors.hpp>
#include <memory>
#include <memory_resource>
#include <span>

using namespace fixpp::tls;
using namespace fixpp::tls::errors;
using fixpp::core::error;

// ── Helpers ───────────────────────────────────────────────────────────────────

namespace {

class stub_cs final : public cert_source {
public:
    asio::awaitable<fixpp::core::expected_t<local_credentials>> load_credentials() override {
        co_return fixpp::core::expected_t<local_credentials>{std::unexpect,
                                                             error::tls_load_cancelled};
    }
    fixpp::core::expected_t<std::span<const Certificate>> load_trust_anchors()
        [[clang::lifetimebound]] override {
        return std::span<const Certificate>{};
    }
};

class stub_clock final : public fixpp::core::Clock {
public:
    explicit stub_clock(std::chrono::system_clock::time_point t) : now_{t} {}
    fixpp::core::utc_time_point now() const noexcept override { return now_; }
    fixpp::core::steady_time_point steady_now() const noexcept override {
        return std::chrono::steady_clock::now();
    }
    asio::awaitable<void> sleep_until(fixpp::core::steady_time_point) override { co_return; }
    void cancel_sleeps() noexcept override {}

private:
    std::chrono::system_clock::time_point now_;
};

// Build a minimal SslCtxConfig for mtls_ca (happy-path profile for testing
// verify_peer paths that don't involve pinning).
SslCtxConfig make_mtls_ca_cfg(std::shared_ptr<fixpp::core::Clock> clk = nullptr) {
    if (!clk) {
        clk = std::make_shared<stub_clock>(std::chrono::system_clock::now());
    }
    SslCtxConfig cfg;
    cfg.profile = SecurityProfile::mtls_ca;
    cfg.cs = std::make_shared<stub_cs>();
    cfg.clock = clk;
    return cfg;
}

// Build a Certificate that is VALID (ECDSA P-256, within validity window,
// within SAN/DER caps).
Certificate make_valid_ecdsa_cert(std::chrono::system_clock::time_point now) {
    Certificate cert{};
    // DER size well within cap.
    static const std::array<std::byte, 512> der_storage{};
    cert.raw_der_ = std::span<const std::byte>{der_storage};
    cert.alg_ = signature_algorithm::ecdsa;
    cert.curve_ = ecdsa_curve::p256;
    cert.x509_version_ = 3;
    cert.not_before_ = now - std::chrono::hours{24};
    cert.not_after_ = now + std::chrono::hours{24};
    // 0 SAN entries, within cap.
    return cert;
}

// Const storage buffer for DER (test lifetime).
static std::array<std::byte, 512> g_der_buf{};

}  // namespace

// ── 1: tls_cert_load_failed (slot 78) ────────────────────────────────────────
// Triggered by make_file_cert_source with a non-existent path.
// We verify the variant value directly since we can't drive it through a real
// file_cert_source in this unit test (no fixture paths).
TEST(TlsErrorVariants, SlotNumbers) {
    // Verify all 16 slot assignments match error.hpp declarations.
    EXPECT_EQ(static_cast<int>(error::tls_cert_load_failed), 78);
    EXPECT_EQ(static_cast<int>(error::tls_cert_parse_failed), 79);
    EXPECT_EQ(static_cast<int>(error::tls_cipher_not_allowed), 80);
    EXPECT_EQ(static_cast<int>(error::tls_invalid_security_profile), 81);
    EXPECT_EQ(static_cast<int>(error::tls_sign_callback_unavailable), 82);
    EXPECT_EQ(static_cast<int>(error::tls_pin_empty_at_open), 83);
    EXPECT_EQ(static_cast<int>(error::tls_pin_not_found), 84);
    EXPECT_EQ(static_cast<int>(error::tls_pin_already_present), 85);
    EXPECT_EQ(static_cast<int>(error::tls_pinset_capacity_exhausted), 86);
    EXPECT_EQ(static_cast<int>(error::tls_pinset_alloc_failed), 87);
    EXPECT_EQ(static_cast<int>(error::tls_handshake_failed), 88);
    EXPECT_EQ(static_cast<int>(error::tls_rsa_key_too_large), 89);
    EXPECT_EQ(static_cast<int>(error::tls_cert_der_too_large), 90);
    EXPECT_EQ(static_cast<int>(error::tls_san_entries_exceeded), 91);
    EXPECT_EQ(static_cast<int>(error::tls_pin_mismatch), 92);
    EXPECT_EQ(static_cast<int>(error::tls_load_cancelled), 93);
}

// ── tls_errors.hpp aliases match core::error variants ────────────────────────
TEST(TlsErrorVariants, AliasesMatch) {
    EXPECT_EQ(tls_cert_load_failed, error::tls_cert_load_failed);
    EXPECT_EQ(tls_cert_parse_failed, error::tls_cert_parse_failed);
    EXPECT_EQ(tls_cipher_not_allowed, error::tls_cipher_not_allowed);
    EXPECT_EQ(tls_invalid_security_profile, error::tls_invalid_security_profile);
    EXPECT_EQ(tls_pin_empty_at_open, error::tls_pin_empty_at_open);
    EXPECT_EQ(tls_pin_not_found, error::tls_pin_not_found);
    EXPECT_EQ(tls_pin_already_present, error::tls_pin_already_present);
    EXPECT_EQ(tls_pinset_capacity_exhausted, error::tls_pinset_capacity_exhausted);
    EXPECT_EQ(tls_pinset_alloc_failed, error::tls_pinset_alloc_failed);
    EXPECT_EQ(tls_handshake_failed, error::tls_handshake_failed);
    EXPECT_EQ(tls_rsa_key_too_large, error::tls_rsa_key_too_large);
    EXPECT_EQ(tls_cert_der_too_large, error::tls_cert_der_too_large);
    EXPECT_EQ(tls_san_entries_exceeded, error::tls_san_entries_exceeded);
    EXPECT_EQ(tls_pin_mismatch, error::tls_pin_mismatch);
    EXPECT_EQ(tls_load_cancelled, error::tls_load_cancelled);
}

// ── tls_invalid_security_profile via make_ssl_ctx_config(unset) ──────────────
TEST(TlsErrorVariants, InvalidSecurityProfile) {
    auto result = make_ssl_ctx_config(
        SecurityProfile::unset, std::make_shared<stub_cs>(),
        std::make_shared<stub_clock>(std::chrono::system_clock::now()), nullptr, nullptr);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), error::tls_invalid_security_profile);
}

// ── tls_pin_empty_at_open via make_ssl_ctx_config(mtls_pinned, empty_pinset) ─
TEST(TlsErrorVariants, PinEmptyAtOpen) {
    auto pinset_r = Pinset::make_pinset(Pinset::Config{}, nullptr);
    ASSERT_TRUE(pinset_r.has_value());
    auto result = make_ssl_ctx_config(
        SecurityProfile::mtls_pinned, std::make_shared<stub_cs>(),
        std::make_shared<stub_clock>(std::chrono::system_clock::now()), *pinset_r, nullptr);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), error::tls_pin_empty_at_open);
}

// ── tls_cert_der_too_large via verify_peer (DER > max_cert_der_bytes) ─────────
TEST(TlsErrorVariants, CertDerTooLarge) {
    auto now = std::chrono::system_clock::now();
    auto clk = std::make_shared<stub_clock>(now);
    auto cfg = make_mtls_ca_cfg(clk);
    // Default cap is 16 KiB; present 16 KiB + 1.
    std::vector<std::byte> big_der(16 * 1024 + 1, std::byte{0});
    Certificate cert{};
    cert.raw_der_ = std::span<const std::byte>{big_der};
    cert.alg_ = signature_algorithm::ecdsa;
    cert.curve_ = ecdsa_curve::p256;
    cert.x509_version_ = 3;
    cert.not_before_ = now - std::chrono::hours{1};
    cert.not_after_ = now + std::chrono::hours{1};

    auto result = verify_peer(cfg, std::span<const Certificate>{&cert, 1});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), error::tls_cert_der_too_large);
}

// ── tls_handshake_failed sub-reason "rsa_under_min" (RSA < 2048) ─────────────
TEST(TlsErrorVariants, HandshakeFailedRsaUnderMin) {
    auto now = std::chrono::system_clock::now();
    auto cfg = make_mtls_ca_cfg(std::make_shared<stub_clock>(now));

    Certificate cert = make_valid_ecdsa_cert(now);
    // Override to RSA with small key bits.
    cert.alg_ = signature_algorithm::rsa_pss;
    cert.rsa_key_bits_ = 1024;  // below 2048

    auto result = verify_peer(cfg, std::span<const Certificate>{&cert, 1});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), error::tls_handshake_failed);
    EXPECT_EQ(last_handshake_sub_reason(), "rsa_under_min");
}

// ── tls_rsa_key_too_large (RSA > max_rsa_key_bits) ───────────────────────────
TEST(TlsErrorVariants, RsaKeyTooLarge) {
    auto now = std::chrono::system_clock::now();
    auto cfg = make_mtls_ca_cfg(std::make_shared<stub_clock>(now));

    Certificate cert = make_valid_ecdsa_cert(now);
    cert.alg_ = signature_algorithm::rsa_pss;
    cert.rsa_key_bits_ = 16384;  // above default 8192

    auto result = verify_peer(cfg, std::span<const Certificate>{&cert, 1});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), error::tls_rsa_key_too_large);
}

// ── tls_handshake_failed sub-reason "ecdsa_curve" (P-521) ────────────────────
TEST(TlsErrorVariants, HandshakeFailedEcdsaCurve) {
    auto now = std::chrono::system_clock::now();
    auto cfg = make_mtls_ca_cfg(std::make_shared<stub_clock>(now));

    Certificate cert = make_valid_ecdsa_cert(now);
    cert.curve_ = ecdsa_curve::unspecified;  // Not P-256 or P-384 → rejected.

    auto result = verify_peer(cfg, std::span<const Certificate>{&cert, 1});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), error::tls_handshake_failed);
    EXPECT_EQ(last_handshake_sub_reason(), "ecdsa_curve");
}

// ── tls_handshake_failed sub-reason "chain_too_deep" ─────────────────────────
TEST(TlsErrorVariants, HandshakeFailedChainTooDeep) {
    auto now = std::chrono::system_clock::now();
    auto cfg = make_mtls_ca_cfg(std::make_shared<stub_clock>(now));

    // Build 9 valid certs (depth > 8 = max_chain_depth).
    static std::array<std::byte, 512> der_buf{};
    std::vector<Certificate> chain(9);
    for (auto& c : chain) {
        c = make_valid_ecdsa_cert(now);
        c.raw_der_ = std::span<const std::byte>{der_buf};
    }

    auto result = verify_peer(cfg, std::span<const Certificate>{chain});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), error::tls_handshake_failed);
    EXPECT_EQ(last_handshake_sub_reason(), "chain_too_deep");
}

// ── tls_san_entries_exceeded (SAN count > 64) ────────────────────────────────
TEST(TlsErrorVariants, SanEntriesExceeded) {
    auto now = std::chrono::system_clock::now();
    auto cfg = make_mtls_ca_cfg(std::make_shared<stub_clock>(now));

    Certificate cert = make_valid_ecdsa_cert(now);
    // Build 65 dummy string_view SAN entries.
    static const std::array<std::string_view, 65> sans_storage = []() {
        std::array<std::string_view, 65> a{};
        for (auto& s : a) s = "example.com";
        return a;
    }();
    cert.san_dns_names_ = std::span<const std::string_view>{sans_storage};

    auto result = verify_peer(cfg, std::span<const Certificate>{&cert, 1});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), error::tls_san_entries_exceeded);
}

// ── tls_handshake_failed sub-reason "x509_v1" ────────────────────────────────
TEST(TlsErrorVariants, HandshakeFailedX509V1) {
    auto now = std::chrono::system_clock::now();
    auto cfg = make_mtls_ca_cfg(std::make_shared<stub_clock>(now));

    Certificate cert = make_valid_ecdsa_cert(now);
    cert.x509_version_ = 1;  // v1 is rejected.

    auto result = verify_peer(cfg, std::span<const Certificate>{&cert, 1});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), error::tls_handshake_failed);
    EXPECT_EQ(last_handshake_sub_reason(), "x509_v1");
}

// ── tls_handshake_failed sub-reason "expired" ────────────────────────────────
TEST(TlsErrorVariants, HandshakeFailedExpired) {
    auto now = std::chrono::system_clock::now();
    auto cfg = make_mtls_ca_cfg(std::make_shared<stub_clock>(now));

    Certificate cert = make_valid_ecdsa_cert(now);
    // Make cert expired: not_after in the past.
    cert.not_before_ = now - std::chrono::hours{48};
    cert.not_after_ = now - std::chrono::hours{1};

    auto result = verify_peer(cfg, std::span<const Certificate>{&cert, 1});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), error::tls_handshake_failed);
    EXPECT_EQ(last_handshake_sub_reason(), "expired");
}

// ── tls_handshake_failed sub-reason "not_yet_valid" ──────────────────────────
TEST(TlsErrorVariants, HandshakeFailedNotYetValid) {
    auto now = std::chrono::system_clock::now();
    auto cfg = make_mtls_ca_cfg(std::make_shared<stub_clock>(now));

    Certificate cert = make_valid_ecdsa_cert(now);
    // Make cert not-yet-valid: not_before in the future.
    cert.not_before_ = now + std::chrono::hours{1};
    cert.not_after_ = now + std::chrono::hours{48};

    auto result = verify_peer(cfg, std::span<const Certificate>{&cert, 1});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), error::tls_handshake_failed);
    EXPECT_EQ(last_handshake_sub_reason(), "not_yet_valid");
}

// ── tls_pin_mismatch (fingerprint not in pinset_snapshot) ────────────────────
TEST(TlsErrorVariants, PinMismatch) {
    auto now = std::chrono::system_clock::now();

    // Build a pinset with ONE pin.
    auto pinset_r = Pinset::make_pinset(Pinset::Config{}, nullptr);
    ASSERT_TRUE(pinset_r.has_value());
    auto pinset = *pinset_r;
    Certificate pinned_cert{};
    pinned_cert.sha256_[0] = std::byte{0xAB};
    ASSERT_TRUE(pinset->add(pinned_cert).has_value());

    auto cfg_r = make_ssl_ctx_config(SecurityProfile::mtls_pinned, std::make_shared<stub_cs>(),
                                     std::make_shared<stub_clock>(now), pinset, nullptr);
    ASSERT_TRUE(cfg_r.has_value());
    auto& cfg = *cfg_r;
    // Act as 2h's wiring: capture the snapshot per [2g §6.5.1] BINDING CONTRACT.
    cfg.pinset_snapshot = pinset->snapshot();

    // Present a cert with a DIFFERENT fingerprint.
    Certificate peer_cert = make_valid_ecdsa_cert(now);
    static std::array<std::byte, 512> der_buf2{};
    peer_cert.raw_der_ = std::span<const std::byte>{der_buf2};
    peer_cert.sha256_[0] = std::byte{0x00};  // different from 0xAB

    auto result = verify_peer(cfg, std::span<const Certificate>{&peer_cert, 1});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), error::tls_pin_mismatch);
}

// ── Happy path: valid cert under mtls_ca returns peer_identity ────────────────
TEST(TlsErrorVariants, HappyPathReturnsIdentity) {
    auto now = std::chrono::system_clock::now();
    auto cfg = make_mtls_ca_cfg(std::make_shared<stub_clock>(now));

    static std::array<std::byte, 512> der_buf3{};
    Certificate cert = make_valid_ecdsa_cert(now);
    cert.raw_der_ = std::span<const std::byte>{der_buf3};
    cert.subject_dn_ = "CN=test";

    auto result = verify_peer(cfg, std::span<const Certificate>{&cert, 1});
    ASSERT_TRUE(result.has_value()) << "Valid cert should be accepted";
    EXPECT_EQ(result->subject_dn_view(), "CN=test");
}
