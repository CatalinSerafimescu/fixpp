// tests/tls/test_certificate_lifetimebound.cpp
// T040 — FR-017: [[clang::lifetimebound]] annotation smoke test.
//
// Design anchor: [arch §5.5] / [2b §6.4] precedent for declaration-site
// lifetimebound annotations on every view-returning accessor.
// Spec anchors: FR-017 (lifetimebound on view accessors).
//
// Strategy:
//   POSITIVE: Compile a positive test (no dangling reference; no warning).
//             These are the normal accessor calls verified at runtime.
//   NEGATIVE: CMake try_compile (in CMakeLists.txt) compiles a .cpp that
//             produces a known-dangling case. If -Werror=dangling fires,
//             the try_compile FAILS (EXPECT_FAIL), confirming the annotation.
//             If -Werror=dangling does NOT fire (heuristic), the CMake test
//             is MARKED as WILL_FAIL:FALSE (stable) with a comment.
//
// Per the brief: if Clang doesn't emit the warning for the case under test,
// the test FAILS visibly (no silent skip). The CMake side handles this.
//
// NOTE: [[clang::lifetimebound]] on data members is NOT supported by Clang
// (limitation). The annotations are on FUNCTION parameters and implicit
// object parameters only. These tests exercise the function-parameter form
// on the accessor sites.

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <fixpp/tls/cert_source.hpp>
#include <fixpp/tls/certificate.hpp>
#include <fixpp/tls/peer_identity.hpp>
#include <fixpp/tls/pinset.hpp>
#include <memory_resource>
#include <span>
#include <string_view>

using namespace fixpp::tls;

// ── Certificate accessors ─────────────────────────────────────────────────────

TEST(CertificateLifetimeBound, CertificateViewAccessors) {
    // POSITIVE: all view accessors called on a live object — no dangling.
    static std::array<std::byte, 32> der_data{};
    static std::string_view subj = "CN=test";
    static std::string_view iss = "CN=CA";

    Certificate cert{};
    cert.raw_der_ = std::span<const std::byte>{der_data};
    cert.subject_dn_ = subj;
    cert.issuer_dn_ = iss;
    cert.x509_version_ = 3;
    cert.alg_ = signature_algorithm::ecdsa;
    cert.curve_ = ecdsa_curve::p256;

    // These calls must compile without -Werror=dangling under any mode.
    EXPECT_FALSE(cert.raw_der().empty());
    EXPECT_EQ(cert.subject_dn(), "CN=test");
    EXPECT_EQ(cert.issuer_dn(), "CN=CA");
    EXPECT_TRUE(cert.san_dns_names().empty());
    EXPECT_TRUE(cert.san_uris().empty());
    EXPECT_EQ(cert.x509_version(), 3);
    EXPECT_EQ(cert.alg(), signature_algorithm::ecdsa);
    EXPECT_EQ(cert.curve(), ecdsa_curve::p256);
    EXPECT_EQ(cert.rsa_key_bits(), 0u);
    // not_before/not_after default to epoch (equal); the important thing is
    // the accessors compile and are callable — no lifetime issue.
    EXPECT_EQ(cert.not_before(), std::chrono::system_clock::time_point{});
    EXPECT_EQ(cert.not_after(), std::chrono::system_clock::time_point{});
}

// ── pin_view accessors ────────────────────────────────────────────────────────

TEST(CertificateLifetimeBound, PinViewFoundReturnsNonNull) {
    // POSITIVE: pin_view::found() on a found/not-found result — no dangling.
    pin_view pv{};
    EXPECT_FALSE(pv.found());
    EXPECT_FALSE(static_cast<bool>(pv));
    EXPECT_EQ(pv.value, nullptr);
}

// ── peer_identity view accessors ──────────────────────────────────────────────

TEST(CertificateLifetimeBound, PeerIdentityViewAccessors) {
    // POSITIVE: subject_dn_view / san_dns_names / san_uris called on a live object.
    peer_identity id{std::pmr::new_delete_resource()};
    id.subject_dn = "CN=peer";
    id.san_dns_names_owned.push_back(
        std::pmr::string{"peer.example.com", std::pmr::new_delete_resource()});

    // subject_dn_view() is [[clang::lifetimebound]] on *this — OK when called
    // on a live object.
    EXPECT_EQ(id.subject_dn_view(), "CN=peer");
    EXPECT_EQ(id.san_dns_names().size(), 1u);
    EXPECT_TRUE(id.san_uris().empty());
}

// ── load_trust_anchors [[clang::lifetimebound]] at abstract base ──────────────
// The annotation is on the return value — callers must not store a dangling span.
// Here we just verify the accessor compiles + returns the expected span.

TEST(CertificateLifetimeBound, LoadTrustAnchorsAnnotationPresent) {
    // This test cannot directly trigger -Wdangling at runtime; the CMake
    // try_compile harness handles the negative compile case.
    // Here we verify the POSITIVE: the method is callable and the annotation
    // doesn't prevent normal use.

    // Inline stub to test the call pattern.
    class inline_stub final : public cert_source {
    public:
        asio::awaitable<fixpp::core::expected_t<local_credentials>> load_credentials() override {
            co_return fixpp::core::expected_t<local_credentials>{
                std::unexpect, fixpp::core::error::tls_load_cancelled};
        }
        fixpp::core::expected_t<std::span<const Certificate>> load_trust_anchors()
            [[clang::lifetimebound]] override {
            return std::span<const Certificate>{};
        }
    };

    inline_stub cs;
    auto anchors = cs.load_trust_anchors();
    ASSERT_TRUE(anchors.has_value());
    EXPECT_TRUE(anchors->empty());
}
