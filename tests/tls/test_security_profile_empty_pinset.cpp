// tests/tls/test_security_profile_empty_pinset.cpp
// T039 — /clarify Q2: make_ssl_ctx_config(mtls_pinned, cs, clock, empty_pinset)
// returns unexpected{tls_pin_empty_at_open}.
//
// Design anchor: contracts/security_profile.hpp assertion 4 + [2g §4.5] Q2.
// Spec anchors: FR-025 (tls_pin_empty_at_open).
//
// Semantics:
//   - empty_pinset (size() == 0) → tls_pin_empty_at_open (config error)
//   - null pinset under mtls_pinned → tls_invalid_security_profile (different error)
//   - These are DISTINCT variants per the /clarify Q2 amendment:
//     "distinct from tls_pin_mismatch so operator logs separate config problem
//     from peer-cert problem".

#include <fixpp/tls/security_profile.hpp>
#include <fixpp/tls/pinset.hpp>
#include <fixpp/core/error.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <span>

using namespace fixpp::tls;
using fixpp::core::error;

namespace {

class stub_cs3 final : public cert_source {
 public:
    asio::awaitable<fixpp::core::expected_t<local_credentials>>
    load_credentials() override {
        co_return fixpp::core::expected_t<local_credentials>{
            std::unexpect, error::tls_load_cancelled};
    }
    fixpp::core::expected_t<std::span<const Certificate>>
    load_trust_anchors() [[clang::lifetimebound]] override {
        return std::span<const Certificate>{};
    }
};

class stub_clk3 final : public fixpp::core::Clock {
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

}  // namespace

// ── Empty Pinset → tls_pin_empty_at_open ─────────────────────────────────────

TEST(SecurityProfileEmptyPinset, EmptyPinsetReturnsPinEmptyAtOpen) {
    auto pinset_r = Pinset::make_pinset(Pinset::Config{}, nullptr);
    ASSERT_TRUE(pinset_r.has_value()) << "make_pinset should succeed";
    auto& pinset = *pinset_r;

    // Confirm the Pinset IS empty.
    ASSERT_EQ(pinset->size(), 0u) << "Pinset must be empty for this test";

    auto result = make_ssl_ctx_config(
        SecurityProfile::mtls_pinned,
        std::make_shared<stub_cs3>(),
        std::make_shared<stub_clk3>(),
        pinset, nullptr);

    ASSERT_FALSE(result.has_value())
        << "make_ssl_ctx_config with empty Pinset must fail";
    EXPECT_EQ(result.error(), error::tls_pin_empty_at_open)
        << "Empty Pinset must return tls_pin_empty_at_open (not tls_invalid_security_profile)";
}

// ── Null Pinset → tls_invalid_security_profile (DIFFERENT from empty) ─────────

TEST(SecurityProfileEmptyPinset, NullPinsetReturnsInvalidProfile) {
    auto result = make_ssl_ctx_config(
        SecurityProfile::mtls_pinned,
        std::make_shared<stub_cs3>(),
        std::make_shared<stub_clk3>(),
        nullptr, nullptr);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), error::tls_invalid_security_profile)
        << "Null Pinset must return tls_invalid_security_profile (not tls_pin_empty_at_open)";
}

// ── Non-empty Pinset under mtls_pinned → accepted ────────────────────────────

TEST(SecurityProfileEmptyPinset, NonEmptyPinsetAccepted) {
    auto pinset_r = Pinset::make_pinset(Pinset::Config{}, nullptr);
    ASSERT_TRUE(pinset_r.has_value());
    auto& pinset = *pinset_r;

    // Add one pin.
    Certificate cert{};
    cert.sha256_[0] = std::byte{0x01};
    ASSERT_TRUE(pinset->add(cert).has_value());
    ASSERT_EQ(pinset->size(), 1u);

    auto result = make_ssl_ctx_config(
        SecurityProfile::mtls_pinned,
        std::make_shared<stub_cs3>(),
        std::make_shared<stub_clk3>(),
        pinset, nullptr);

    ASSERT_TRUE(result.has_value())
        << "mtls_pinned with non-empty Pinset should succeed";
}

// ── Session never opens (the SslCtxConfig is not returned on failure) ─────────

TEST(SecurityProfileEmptyPinset, FailureYieldsNoSslCtxConfig) {
    auto pinset_r = Pinset::make_pinset(Pinset::Config{}, nullptr);
    ASSERT_TRUE(pinset_r.has_value());

    auto result = make_ssl_ctx_config(
        SecurityProfile::mtls_pinned,
        std::make_shared<stub_cs3>(),
        std::make_shared<stub_clk3>(),
        *pinset_r, nullptr);

    // The SslCtxConfig is NOT returned; the session never proceeds to open.
    ASSERT_FALSE(result.has_value());
    // This verifies the "session never opens" contract assertion.
    EXPECT_EQ(result.error(), error::tls_pin_empty_at_open);
}
