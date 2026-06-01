// tests/tls/test_verify_peer_t039.cpp
// T038 — seam #14: verify_peer cert-parameter validation + FR-020a
// short-circuit first-violation-hit order.
// + T020 (014 FR-012): real Ed25519 fixture cell — sigalg_disallowed via
// a cert loaded from leaf_ed25519.pem (T019 fixture) through file_cert_source.
//
// Design anchor: .specify/2g-tls.md v0.4 §6.5.1 + contracts/security_profile.hpp.
// Spec anchors: FR-019 (DoS bounds), FR-020 (full enforcement),
//               FR-020a (short-circuit order per Clarify Q3).
//
// Canonical 10-step order:
//   1. DER size      → tls_cert_der_too_large
//   2. RSA-low       → tls_handshake_failed "rsa_under_min"
//   3. RSA-high      → tls_rsa_key_too_large
//   4. ECDSA curve   → tls_handshake_failed "ecdsa_curve"
//   5. Chain depth   → tls_handshake_failed "chain_too_deep"
//   6. SAN card.     → tls_san_entries_exceeded
//   7. X.509 version → tls_handshake_failed "x509_v1"
//   8. Expiration    → tls_handshake_failed "expired"/"not_yet_valid"
//   9. Pinning       → tls_pin_mismatch
//  10. Cipher        → tls_cipher_not_allowed (stub; 2h pending)

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <fixpp/tls/pinset.hpp>
#include <fixpp/tls/security_profile.hpp>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

// For T020 Ed25519 fixture loading.
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <fixpp/tls/cert_source.hpp>
#include <fixpp/tls/file_cert_source.hpp>

using namespace fixpp::tls;
using fixpp::core::error;

namespace {

class stub_cs2 final : public cert_source {
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

class fixed_clock final : public fixpp::core::Clock {
public:
    explicit fixed_clock(std::chrono::system_clock::time_point t) : t_{t} {}
    fixpp::core::utc_time_point now() const noexcept override { return t_; }
    fixpp::core::steady_time_point steady_now() const noexcept override {
        return std::chrono::steady_clock::now();
    }
    asio::awaitable<void> sleep_until(fixpp::core::steady_time_point) override { co_return; }
    void cancel_sleeps() noexcept override {}

private:
    std::chrono::system_clock::time_point t_;
};

// Default DER buffer (small — within cap).
static std::array<std::byte, 512> g_der{};

SslCtxConfig make_mtls_ca(std::chrono::system_clock::time_point now) {
    SslCtxConfig cfg;
    cfg.profile = SecurityProfile::mtls_ca;
    cfg.cs = std::make_shared<stub_cs2>();
    cfg.clock = std::make_shared<fixed_clock>(now);
    return cfg;
}

Certificate make_valid(std::chrono::system_clock::time_point now) {
    Certificate c{};
    c.raw_der_ = std::span<const std::byte>{g_der};
    c.alg_ = signature_algorithm::ecdsa;
    c.curve_ = ecdsa_curve::p256;
    c.x509_version_ = 3;
    c.not_before_ = now - std::chrono::hours{24};
    c.not_after_ = now + std::chrono::hours{24};
    return c;
}

}  // namespace

// ── Individual step tests ─────────────────────────────────────────────────────

TEST(VerifyPeerT039, Step1DerTooLarge) {
    auto now = std::chrono::system_clock::now();
    auto cfg = make_mtls_ca(now);
    std::vector<std::byte> big(16 * 1024 + 1);
    Certificate c = make_valid(now);
    c.raw_der_ = std::span<const std::byte>{big};

    auto r = verify_peer(cfg, {&c, 1});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::tls_cert_der_too_large);
}

TEST(VerifyPeerT039, Step2RsaUnderMin) {
    auto now = std::chrono::system_clock::now();
    auto cfg = make_mtls_ca(now);
    Certificate c = make_valid(now);
    c.alg_ = signature_algorithm::rsa_pss;
    c.rsa_key_bits_ = 1024;

    auto r = verify_peer(cfg, {&c, 1});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::tls_handshake_failed);
    EXPECT_EQ(last_handshake_sub_reason(), "rsa_under_min");
}

TEST(VerifyPeerT039, Step3RsaTooLarge) {
    auto now = std::chrono::system_clock::now();
    auto cfg = make_mtls_ca(now);
    Certificate c = make_valid(now);
    c.alg_ = signature_algorithm::rsa_pss;
    c.rsa_key_bits_ = 16384;  // > 8192 default cap

    auto r = verify_peer(cfg, {&c, 1});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::tls_rsa_key_too_large);
}

TEST(VerifyPeerT039, Step4EcdsaCurve) {
    auto now = std::chrono::system_clock::now();
    auto cfg = make_mtls_ca(now);
    Certificate c = make_valid(now);
    c.curve_ = ecdsa_curve::unspecified;  // not P-256 or P-384

    auto r = verify_peer(cfg, {&c, 1});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::tls_handshake_failed);
    EXPECT_EQ(last_handshake_sub_reason(), "ecdsa_curve");
}

TEST(VerifyPeerT039, Step5ChainTooDeep) {
    auto now = std::chrono::system_clock::now();
    auto cfg = make_mtls_ca(now);
    // 9 certs > max_chain_depth (8).
    std::vector<Certificate> chain(9);
    for (auto& c : chain) c = make_valid(now);

    auto r = verify_peer(cfg, std::span<const Certificate>{chain});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::tls_handshake_failed);
    EXPECT_EQ(last_handshake_sub_reason(), "chain_too_deep");
}

TEST(VerifyPeerT039, Step6SanExceeded) {
    auto now = std::chrono::system_clock::now();
    auto cfg = make_mtls_ca(now);
    Certificate c = make_valid(now);
    static const std::array<std::string_view, 65> sans = []() {
        std::array<std::string_view, 65> a{};
        for (auto& s : a) s = "x.example.com";
        return a;
    }();
    c.san_dns_names_ = std::span<const std::string_view>{sans};

    auto r = verify_peer(cfg, {&c, 1});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::tls_san_entries_exceeded);
}

TEST(VerifyPeerT039, Step7X509V1) {
    auto now = std::chrono::system_clock::now();
    auto cfg = make_mtls_ca(now);
    Certificate c = make_valid(now);
    c.x509_version_ = 1;

    auto r = verify_peer(cfg, {&c, 1});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::tls_handshake_failed);
    EXPECT_EQ(last_handshake_sub_reason(), "x509_v1");
}

TEST(VerifyPeerT039, Step8Expired) {
    auto now = std::chrono::system_clock::now();
    auto cfg = make_mtls_ca(now);
    Certificate c = make_valid(now);
    c.not_after_ = now - std::chrono::hours{1};

    auto r = verify_peer(cfg, {&c, 1});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::tls_handshake_failed);
    EXPECT_EQ(last_handshake_sub_reason(), "expired");
}

TEST(VerifyPeerT039, Step8NotYetValid) {
    auto now = std::chrono::system_clock::now();
    auto cfg = make_mtls_ca(now);
    Certificate c = make_valid(now);
    c.not_before_ = now + std::chrono::hours{1};
    c.not_after_ = now + std::chrono::hours{48};

    auto r = verify_peer(cfg, {&c, 1});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::tls_handshake_failed);
    EXPECT_EQ(last_handshake_sub_reason(), "not_yet_valid");
}

TEST(VerifyPeerT039, Step9PinMismatch) {
    auto now = std::chrono::system_clock::now();
    auto pinset_r = Pinset::make_pinset(Pinset::Config{}, nullptr);
    ASSERT_TRUE(pinset_r.has_value());
    auto pinset = *pinset_r;
    Certificate pinned{};
    pinned.sha256_[0] = std::byte{0xCC};
    ASSERT_TRUE(pinset->add(pinned).has_value());

    auto cfg_r = make_ssl_ctx_config(SecurityProfile::mtls_pinned, std::make_shared<stub_cs2>(),
                                     std::make_shared<fixed_clock>(now), pinset, nullptr);
    ASSERT_TRUE(cfg_r.has_value());
    auto& cfg = *cfg_r;
    // Act as 2h's wiring: capture the snapshot at "handshake start" per
    // [2g §6.5.1] BINDING CONTRACT. make_ssl_ctx_config no longer pre-populates.
    cfg.pinset_snapshot = pinset->snapshot();

    Certificate peer = make_valid(now);
    peer.sha256_[0] = std::byte{0x11};  // Different from 0xCC.

    auto r = verify_peer(cfg, {&peer, 1});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::tls_pin_mismatch);
}

// ── Multi-violation chain: steps 2+5+8 simultaneously — ONLY step 1 fires ────
// This test places violations at step 2 (RSA < 2048) AND step 5 (chain depth > 8)
// AND step 8 (expired). The chain is also length 9. The LEAF cert has RSA < 2048.
// Step 2 must fire first (before step 5, which requires checking chain length,
// which is checked AFTER individual leaf checks at steps 1-4).
//
// Per canonical order: DER(1) → RSA-low(2) → RSA-high(3) → ECDSA(4) →
//   chain-depth(5) → SAN(6) → x509(7) → expiry(8) → pinning(9).
//
// So with RSA-low(2) + chain-depth(5) + expiry(8) all violated,
// step 2 fires first.
TEST(VerifyPeerT039, MultiViolationFirstInOrderFires) {
    auto now = std::chrono::system_clock::now();
    auto cfg = make_mtls_ca(now);

    // Build chain of 9 (step 5 violation).
    std::vector<Certificate> chain(9);
    for (auto& c : chain) {
        c = make_valid(now);
        // Leaf cert (chain[0]) has RSA-low (step 2 violation).
    }
    // Leaf violates step 2: RSA < 2048.
    chain[0].alg_ = signature_algorithm::rsa_pss;
    chain[0].rsa_key_bits_ = 512;
    // All certs expired (step 8 violation).
    for (auto& c : chain) {
        c.not_before_ = now - std::chrono::hours{48};
        c.not_after_ = now - std::chrono::hours{1};
    }

    auto r = verify_peer(cfg, std::span<const Certificate>{chain});
    ASSERT_FALSE(r.has_value());
    // Step 2 (RSA-low) must fire, NOT step 5 (chain_too_deep) or step 8 (expired).
    EXPECT_EQ(r.error(), error::tls_handshake_failed)
        << "Expected tls_handshake_failed for RSA-low (step 2)";
    EXPECT_EQ(last_handshake_sub_reason(), "rsa_under_min")
        << "First violation in order (step 2) must fire; got sub-reason: "
        << last_handshake_sub_reason();
}

// ── DER-too-large (step 1) fires before RSA-low (step 2) ─────────────────────
TEST(VerifyPeerT039, DerBeforeRsaLow) {
    auto now = std::chrono::system_clock::now();
    auto cfg = make_mtls_ca(now);

    std::vector<std::byte> big(16 * 1024 + 1);
    Certificate c = make_valid(now);
    c.raw_der_ = std::span<const std::byte>{big};
    c.alg_ = signature_algorithm::rsa_pss;
    c.rsa_key_bits_ = 512;  // also violates step 2

    auto r = verify_peer(cfg, {&c, 1});
    ASSERT_FALSE(r.has_value());
    // Step 1 fires before step 2.
    EXPECT_EQ(r.error(), error::tls_cert_der_too_large);
}

// ── Valid RSA-2048 cert passes all steps ──────────────────────────────────────
TEST(VerifyPeerT039, ValidRsa2048Accepted) {
    auto now = std::chrono::system_clock::now();
    auto cfg = make_mtls_ca(now);

    Certificate c = make_valid(now);
    c.alg_ = signature_algorithm::rsa_pss;
    c.rsa_key_bits_ = 2048;

    auto r = verify_peer(cfg, {&c, 1});
    ASSERT_TRUE(r.has_value()) << "RSA-2048 valid cert should be accepted";
}

// ── Valid ECDSA P-384 cert passes ────────────────────────────────────────────
TEST(VerifyPeerT039, ValidEcdsaP384Accepted) {
    auto now = std::chrono::system_clock::now();
    auto cfg = make_mtls_ca(now);

    Certificate c = make_valid(now);
    c.curve_ = ecdsa_curve::p384;

    auto r = verify_peer(cfg, {&c, 1});
    ASSERT_TRUE(r.has_value()) << "ECDSA P-384 valid cert should be accepted";
}

// ── Happy-path with SAN entries → peer_identity carries owned SAN copies ─────
// Lifts the SAN-copy hot path inside verify_peer (emplace_back into
// peer_identity.san_dns_names_owned + san_uris_owned) which the SAN-less
// happy-paths above skip.
TEST(VerifyPeerT039, ValidCertWithSanCopiesOwnedSans) {
    auto now = std::chrono::system_clock::now();
    auto cfg = make_mtls_ca(now);

    static const std::array<std::string_view, 3> dns_sans = {{
        "fixpp.example.invalid",
        "alt-1.fixpp.example.invalid",
        "alt-2.fixpp.example.invalid",
    }};
    static const std::array<std::string_view, 2> uri_sans = {{
        "spiffe://fixpp.example.invalid/order-gateway",
        "spiffe://fixpp.example.invalid/admin",
    }};

    Certificate c = make_valid(now);
    c.san_dns_names_ = std::span<const std::string_view>{dns_sans};
    c.san_uris_ = std::span<const std::string_view>{uri_sans};

    auto r = verify_peer(cfg, {&c, 1});
    ASSERT_TRUE(r.has_value()) << "valid cert with SAN entries must be accepted";
    EXPECT_EQ(r->san_dns_names_owned.size(), dns_sans.size());
    EXPECT_EQ(r->san_uris_owned.size(), uri_sans.size());
    EXPECT_EQ(r->san_dns_names_owned[0], dns_sans[0]);
    EXPECT_EQ(r->san_uris_owned[1], uri_sans[1]);
}

// ── Step 9 happy path: mtls_pinned leaf matches a pin → accepted ─────────────
// FR-020a step 9 match→accept branch (no other test covers this; existing
// Step9PinMismatch only covers the reject branch).
TEST(VerifyPeerT039, MtlsPinnedLeafMatchesAccepted) {
    auto now = std::chrono::system_clock::now();
    auto pinset_r = Pinset::make_pinset(Pinset::Config{}, nullptr);
    ASSERT_TRUE(pinset_r.has_value());
    auto pinset = *pinset_r;

    Certificate pinned_leaf = make_valid(now);
    pinned_leaf.sha256_[0] = std::byte{0xCC};
    ASSERT_TRUE(pinset->add(pinned_leaf).has_value());

    auto cfg_r = make_ssl_ctx_config(SecurityProfile::mtls_pinned, std::make_shared<stub_cs2>(),
                                     std::make_shared<fixed_clock>(now), pinset, nullptr);
    ASSERT_TRUE(cfg_r.has_value());
    auto& cfg = *cfg_r;
    cfg.pinset_snapshot = pinset->snapshot();  // 2h's wiring

    auto r = verify_peer(cfg, {&pinned_leaf, 1});
    ASSERT_TRUE(r.has_value()) << "pinned leaf must be accepted";
}

// ── mtls_pinned with null snapshot → tls_pin_empty_at_open (fail-closed) ─────
// 2h's wiring forgot to capture; verify_peer must NOT silently accept.
TEST(VerifyPeerT039, MtlsPinnedNullSnapshotFailsClosed) {
    auto now = std::chrono::system_clock::now();
    auto pinset_r = Pinset::make_pinset(Pinset::Config{}, nullptr);
    ASSERT_TRUE(pinset_r.has_value());
    auto pinset = *pinset_r;
    Certificate dummy{};
    dummy.sha256_[0] = std::byte{0x55};
    ASSERT_TRUE(pinset->add(dummy).has_value());

    auto cfg_r = make_ssl_ctx_config(SecurityProfile::mtls_pinned, std::make_shared<stub_cs2>(),
                                     std::make_shared<fixed_clock>(now), pinset, nullptr);
    ASSERT_TRUE(cfg_r.has_value());
    auto& cfg = *cfg_r;
    // INTENTIONALLY leave cfg.pinset_snapshot null (simulate 2h forgetting).

    Certificate peer = make_valid(now);
    auto r = verify_peer(cfg, {&peer, 1});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::tls_pin_empty_at_open);
}

// ── Unspecified sig_alg → tls_handshake_failed sub-reason "sigalg_disallowed" ─
// FR-020 envelope: a leaf whose key algorithm is neither RSA nor ECDSA
// (Ed25519/Ed448/unknown EVP_PKEY) must be rejected. Without this, step 10
// (cipher gate, TODO 2h) is the only hardening barrier and the cert passes.
//
// This cell uses a SYNTHETIC Certificate with alg_=unspecified to test the
// verify_peer logic in isolation. The companion cell below (Ed25519Fixture)
// tests the FULL path: real OpenSSL cert parsing → alg_ field → verify_peer.
TEST(VerifyPeerT039, UnspecifiedSigAlgRejected) {
    auto now = std::chrono::system_clock::now();
    auto cfg = make_mtls_ca(now);
    Certificate c = make_valid(now);
    c.alg_ = signature_algorithm::unspecified;

    auto r = verify_peer(cfg, {&c, 1});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::tls_handshake_failed);
    EXPECT_EQ(last_handshake_sub_reason(), "sigalg_disallowed");
}

// ── T020 (014 FR-012): real Ed25519 fixture → sigalg_disallowed ──────────────
// Loads the leaf_ed25519.pem fixture (T019) via file_cert_source, which parses
// the cert with OpenSSL. OpenSSL sets alg_ based on EVP_PKEY_base_id():
//   EVP_PKEY_ED25519 → not RSA_PSS, not ECDSA → alg_ stays unspecified.
//
// This cell witnesses the REAL cert-parsing path, not a synthetic Certificate:
//   file_cert_source::load_credentials() → local_credentials.leaf (Certificate)
//   → verify_peer(cfg, chain) → step 1.5 sigalg check fires → sigalg_disallowed.
//
// [const §XII.3] allow-list: {rsa_pss, ecdsa} only. Ed25519 is outside it.
// FR-012; 014 data-model E-5; tasks.md T020.
//
// Why unit-level (not integration/taxonomy): the full TLS handshake path cannot
// deliver an Ed25519 cert to verify_peer_trampoline — OpenSSL TLS 1.3 filters
// Ed25519 certs at the CertificateRequest protocol level (server's sigalg list
// excludes Ed25519). The unit-level test is the appropriate witness location.
// See test_tls_validation_failed_taxonomy.cpp T020 comment for the full analysis.
//
// GTEST_SKIP if FIXPP_TLS_FIXTURE_DIR is absent (CI without fixtures).
TEST(VerifyPeerT039, Ed25519FixtureRejectedAsSigalgDisallowed) {
#ifdef FIXPP_TLS_FIXTURE_DIR
    static const char* kFixtureDir = FIXPP_TLS_FIXTURE_DIR;
#else
    static const char* kFixtureDir = nullptr;
#endif
    const char* dir_env = std::getenv("FIXPP_TLS_FIXTURE_DIR");
    const char* fixture_dir = dir_env ? dir_env : kFixtureDir;
    if (!fixture_dir || fixture_dir[0] == '\0') {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }

    const std::string ed25519_pem = std::string(fixture_dir) + "/leaf_ed25519.pem";
    const std::string ed25519_key = std::string(fixture_dir) + "/leaf_ed25519.key";
    const std::string ca_pem = std::string(fixture_dir) + "/ca.pem";

    // Load the real Ed25519 cert via file_cert_source (same path as
    // make_asio_tls_transport_factory → prepare_ssl_ctx_ → load_credentials).
    fixpp::tls::file_cert_source::Config cs_cfg;
    cs_cfg.leaf_path = ed25519_pem;
    cs_cfg.private_key_path = ed25519_key;
    cs_cfg.ca_bundle_path = ca_pem;

    auto cs_result = fixpp::tls::file_cert_source::make_file_cert_source(
        cs_cfg, std::pmr::new_delete_resource());
    if (!cs_result.has_value()) {
        GTEST_SKIP() << "leaf_ed25519.pem not available or failed to load";
    }
    auto cs = std::move(*cs_result);

    // Synchronously run load_credentials() — same pattern as prepare_ssl_ctx_.
    std::optional<fixpp::tls::local_credentials> creds_opt;
    {
        asio::io_context tmp_io;
        std::optional<fixpp::core::expected_t<fixpp::tls::local_credentials>> creds_result;
        asio::co_spawn(
            tmp_io.get_executor(),
            [&]() -> asio::awaitable<void> { creds_result = co_await cs->load_credentials(); },
            asio::detached);
        tmp_io.run();
        ASSERT_TRUE(creds_result.has_value()) << "load_credentials did not complete";
        ASSERT_TRUE(creds_result->has_value())
            << "load_credentials failed: " << static_cast<int>(creds_result->error());
        creds_opt = std::move(**creds_result);
    }

    // The leaf Certificate has alg_ set by OpenSSL parsing. For Ed25519,
    // EVP_PKEY_base_id() returns EVP_PKEY_ED25519 — not RSA_PSS, not ECDSA —
    // so alg_ stays at signature_algorithm::unspecified (the default).
    const Certificate& leaf = creds_opt->leaf;

    // Build a minimal SslCtxConfig (mtls_ca, no expiry check).
    // cfg.clock = nullptr → step 8 (expiry) is skipped; the fixture has a valid
    // date range (2025-01-01 .. 2027-12-31) but we skip for simplicity.
    SslCtxConfig cfg;
    cfg.profile = SecurityProfile::mtls_ca;
    cfg.clock = nullptr;
    cfg.caps = CertSourceCaps{};
    cfg.cs = cs;  // cert_source must remain alive (leaf views its storage)

    // Call verify_peer with the real Ed25519 leaf.
    // Step 1.5 (sigalg check): alg_ != rsa_pss && alg_ != ecdsa → fires.
    auto r = verify_peer(cfg, {&leaf, 1});

    ASSERT_FALSE(r.has_value())
        << "verify_peer must REJECT Ed25519 cert (sigalg not in [const §XII.3] allow-list)";
    EXPECT_EQ(r.error(), error::tls_handshake_failed)
        << "Ed25519 cert must return tls_handshake_failed (GROUPING variant)";
    EXPECT_EQ(last_handshake_sub_reason(), "sigalg_disallowed")
        << "sub_reason must be 'sigalg_disallowed' for Ed25519 cert "
           "(EVP_PKEY_base_id = EVP_PKEY_ED25519, not RSA_PSS/ECDSA)";
}
