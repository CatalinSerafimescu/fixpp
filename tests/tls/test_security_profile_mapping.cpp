// tests/tls/test_security_profile_mapping.cpp
// T036 — seam #11: SecurityProfile-to-OpenSSL-mode 4-row mapping table +
// rejection paths + deprecated-enumerator try_compile witness.
//
// Design anchor: .specify/2g-tls.md v0.4 §4.5 / §4.5.1 + contracts/security_profile.hpp.
// Spec anchors: FR-013 (enum + [[deprecated]] enumerator), FR-014 (TLS-version posture),
//               FR-022 ([[nodiscard]]), FR-025 (named error variants).

#include <gtest/gtest.h>

#include <chrono>
#include <fixpp/tls/file_cert_source.hpp>
#include <fixpp/tls/pinset.hpp>
#include <fixpp/tls/security_profile.hpp>
#include <memory>
#include <memory_resource>
#include <string>

#ifndef FIXPP_TLS_FIXTURE_DIR
#define FIXPP_TLS_FIXTURE_DIR ""
#endif

using namespace fixpp::tls;
using fixpp::core::error;

// ── Minimal cert_source stub (no-op; we only test make_ssl_ctx_config) ───────
namespace {

class stub_cert_source final : public cert_source {
public:
    asio::awaitable<fixpp::core::expected_t<local_credentials>> load_credentials() override {
        co_return fixpp::core::expected_t<local_credentials>{std::unexpect,
                                                             error::tls_load_cancelled};
    }

    fixpp::core::expected_t<std::span<const Certificate>> load_trust_anchors()
        [[clang::lifetimebound]] override {
        return fixpp::core::expected_t<std::span<const Certificate>>{
            std::span<const Certificate>{}};
    }
};

// Minimal Clock stub.
class stub_clock final : public fixpp::core::Clock {
public:
    fixpp::core::utc_time_point now() const noexcept override {
        return std::chrono::system_clock::now();
    }
    fixpp::core::steady_time_point steady_now() const noexcept override {
        return std::chrono::steady_clock::now();
    }
    asio::awaitable<void> sleep_until(fixpp::core::steady_time_point) override { co_return; }
    void cancel_sleeps() noexcept override {}
};

std::shared_ptr<cert_source> make_cs() { return std::make_shared<stub_cert_source>(); }
std::shared_ptr<fixpp::core::Clock> make_clock() { return std::make_shared<stub_clock>(); }

}  // namespace

// ── Row 1: mtls_ca — accepts cs + clock + null pinset ────────────────────────

TEST(SecurityProfileMapping, MtlsCaAccepted) {
    auto result =
        make_ssl_ctx_config(SecurityProfile::mtls_ca, make_cs(), make_clock(), nullptr, nullptr);
    ASSERT_TRUE(result.has_value()) << "mtls_ca with valid cs+clock should succeed";
    EXPECT_EQ(result->profile, SecurityProfile::mtls_ca);
    EXPECT_NE(result->cs, nullptr);
    EXPECT_NE(result->clock, nullptr);
    EXPECT_EQ(result->pinset, nullptr);
}

// ── Row 1: mtls_ca with optional non-null Pinset — accepted ──────────────────

TEST(SecurityProfileMapping, MtlsCaWithNonNullPinsetAccepted) {
    auto pinset_result = Pinset::make_pinset(Pinset::Config{}, nullptr);
    ASSERT_TRUE(pinset_result.has_value());
    auto pinset = *pinset_result;
    // Add at least one pin so we have a populated pinset (empty pinset allowed for mtls_ca).
    auto result =
        make_ssl_ctx_config(SecurityProfile::mtls_ca, make_cs(), make_clock(), pinset, nullptr);
    ASSERT_TRUE(result.has_value()) << "mtls_ca + non-null Pinset should succeed";
    EXPECT_EQ(result->pinset, pinset);
}

// ── Row 2: mtls_pinned — acceptor/initiator role split noted ─────────────────

TEST(SecurityProfileMapping, MtlsPinnedWithPopulatedPinsetAccepted) {
    // Build a Pinset with at least one pin.
    auto pinset_result = Pinset::make_pinset(Pinset::Config{}, nullptr);
    ASSERT_TRUE(pinset_result.has_value());
    auto pinset = *pinset_result;

    // Construct a Certificate with a non-zero sha256 to add to the Pinset.
    Certificate cert{};
    cert.sha256_[0] = std::byte{0x42};
    auto add_res = pinset->add(cert);
    ASSERT_TRUE(add_res.has_value()) << "Pinset::add should succeed";

    auto result =
        make_ssl_ctx_config(SecurityProfile::mtls_pinned, make_cs(), make_clock(), pinset, nullptr);
    ASSERT_TRUE(result.has_value()) << "mtls_pinned with non-empty Pinset should succeed";
    EXPECT_EQ(result->profile, SecurityProfile::mtls_pinned);
    // pinset_snapshot is INTENTIONALLY null at make_ssl_ctx_config time per
    // [2g §6.5.1] BINDING CONTRACT; 2h's wiring captures it at handshake start.
    EXPECT_EQ(result->pinset_snapshot, nullptr);
}

// ── Row 3 (deprecated): one_way_ca + null Pinset — accepted ──────────────────

TEST(SecurityProfileMapping, OneWayCaAccepted) {
    // one_way_ca is deprecated but still functional.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    auto result =
        make_ssl_ctx_config(SecurityProfile::one_way_ca, make_cs(), make_clock(), nullptr, nullptr);
    ASSERT_TRUE(result.has_value()) << "one_way_ca with null Pinset should succeed";
    EXPECT_EQ(result->profile, SecurityProfile::one_way_ca);
#pragma GCC diagnostic pop
}

// ── Rejection: SecurityProfile::unset → tls_invalid_security_profile ─────────

TEST(SecurityProfileMapping, RejectUnset) {
    auto result =
        make_ssl_ctx_config(SecurityProfile::unset, make_cs(), make_clock(), nullptr, nullptr);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), error::tls_invalid_security_profile);
}

// ── Rejection: null cs → tls_invalid_security_profile ────────────────────────

TEST(SecurityProfileMapping, RejectNullCertSource) {
    auto result =
        make_ssl_ctx_config(SecurityProfile::mtls_ca, nullptr, make_clock(), nullptr, nullptr);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), error::tls_invalid_security_profile);
}

// ── Rejection: null clock → tls_invalid_security_profile ─────────────────────

TEST(SecurityProfileMapping, RejectNullClock) {
    auto result =
        make_ssl_ctx_config(SecurityProfile::mtls_ca, make_cs(), nullptr, nullptr, nullptr);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), error::tls_invalid_security_profile);
}

// ── Rejection: mtls_pinned + null Pinset → tls_invalid_security_profile ──────

TEST(SecurityProfileMapping, MtlsPinnedNullPinsetRejected) {
    auto result = make_ssl_ctx_config(SecurityProfile::mtls_pinned, make_cs(), make_clock(),
                                      nullptr, nullptr);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), error::tls_invalid_security_profile);
}

// ── Rejection: one_way_ca + non-null Pinset → tls_invalid_security_profile ───

TEST(SecurityProfileMapping, OneWayCaWithPinsetRejected) {
    auto pinset_result = Pinset::make_pinset(Pinset::Config{}, nullptr);
    ASSERT_TRUE(pinset_result.has_value());
    auto pinset = *pinset_result;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    auto result =
        make_ssl_ctx_config(SecurityProfile::one_way_ca, make_cs(), make_clock(), pinset, nullptr);
#pragma GCC diagnostic pop
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), error::tls_invalid_security_profile);
}

// ── SecurityProfile enumerator count ─────────────────────────────────────────

// ── extract_caps reads operator-configured file_cert_source::Config (FR-019) ─
// /simplify F-04 wired make_ssl_ctx_config's extract_caps to read the real
// Config via cert_source::dynamic_cast → file_cert_source::config(). All other
// tests in this file use the stub_cert_source which returns default caps; this
// test exercises the dynamic_cast happy-path and asserts the operator's tightened
// caps flow into cfg.caps (vs being silently dropped — the pre-F-04 drift).
TEST(SecurityProfileMapping, ExtractCapsReadsFileCertSourceConfig) {
    file_cert_source::Config fcs_cfg;
    fcs_cfg.leaf_path = std::string(FIXPP_TLS_FIXTURE_DIR) + "/leaf_rsa2048.pem";
    fcs_cfg.private_key_path = std::string(FIXPP_TLS_FIXTURE_DIR) + "/leaf_rsa2048.key";
    fcs_cfg.ca_bundle_path = std::string(FIXPP_TLS_FIXTURE_DIR) + "/ca.pem";
    // Tightened DoS caps vs CertSourceCaps{} defaults of 8/8192/16384/64.
    fcs_cfg.max_chain_depth = 4;
    fcs_cfg.max_rsa_key_bits = 4096;
    fcs_cfg.max_cert_der_bytes = 8 * 1024;
    fcs_cfg.max_san_entries = 16;

    auto fcs_r = file_cert_source::make_file_cert_source(fcs_cfg, nullptr);
    ASSERT_TRUE(fcs_r.has_value())
        << "file_cert_source factory must succeed against checked-in fixtures";

    auto cfg_r =
        make_ssl_ctx_config(SecurityProfile::mtls_ca, *fcs_r, make_clock(), nullptr, nullptr);
    ASSERT_TRUE(cfg_r.has_value());
    auto& cfg = *cfg_r;

    EXPECT_EQ(cfg.caps.max_chain_depth, 4u);
    EXPECT_EQ(cfg.caps.max_rsa_key_bits, 4096u);
    EXPECT_EQ(cfg.caps.max_cert_der_bytes, 8u * 1024u);
    EXPECT_EQ(cfg.caps.max_san_entries, 16u);
}

TEST(SecurityProfileMapping, EnumHasFourValues) {
    // Contract assertion 1: four enumerators including unset sentinel.
    static_assert(static_cast<uint8_t>(SecurityProfile::unset) == 0);
    static_assert(static_cast<uint8_t>(SecurityProfile::mtls_ca) == 1);
    static_assert(static_cast<uint8_t>(SecurityProfile::mtls_pinned) == 2);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    static_assert(static_cast<uint8_t>(SecurityProfile::one_way_ca) == 3);
#pragma GCC diagnostic pop
    SUCCEED();
}
