// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/tls/test_make_file_cert_source_factory.cpp
// T026 — [2g §9 seam #17] RC#2 close: make_file_cert_source factory parity.
//
// Binding contracts tested:
//   (a) Factory returns expected_t<shared_ptr<cert_source>> — no exception
//       thrown across the session-handling boundary (FR-005 / [arch §5.3]).
//   (b) Parse-failure (malformed content) → tls_cert_parse_failed.
//   (c) Load-failure (missing file / wrong password) → tls_cert_load_failed.
//   (d) Success: returned shared_ptr's load_trust_anchors() works.
//   Companion: static_assert(noexcept(make_file_cert_source(cfg, nullptr)))
//              positive-compile witness that factory is noexcept.

#include <gtest/gtest.h>

#include <fixpp/tls/cert_source.hpp>
#include <fixpp/tls/file_cert_source.hpp>
#include <fixpp/core/error.hpp>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>

#include <filesystem>
#include <memory>
#include <memory_resource>
#include <string>
#include <variant>

namespace {

using fixpp::tls::file_cert_source;
using fixpp::tls::cert_source;
using fixpp::core::error;

// Companion positive-compile witness: make_file_cert_source must be noexcept
// so 2i's C-ABI bridge can call it without an exception handler.
// Per FR-005 / [arch §5.3]: factory NEVER throws.
static_assert(
    noexcept(file_cert_source::make_file_cert_source(file_cert_source::Config{}, nullptr)),
    "make_file_cert_source must be noexcept — 2i C-ABI bridge requirement (FR-005)");

// Path to the fixture directory (compiled-in at test-build time via CMake
// definition FIXPP_TLS_FIXTURE_DIR).
#ifndef FIXPP_TLS_FIXTURE_DIR
#define FIXPP_TLS_FIXTURE_DIR ""
#endif

static std::string fixture(const char* name) {
    return std::string(FIXPP_TLS_FIXTURE_DIR) + "/" + name;
}

// ── (a) No exception escapes the factory ─────────────────────────────────────
TEST(FileCertSourceFactory, ReturnsExpectedNotThrows) {
    file_cert_source::Config cfg;
    cfg.leaf_path    = fixture("leaf_rsa2048.pem");
    cfg.chain_path   = fixture("chain_depth_8.pem");
    cfg.private_key_path = fixture("leaf_rsa2048.key");
    cfg.ca_bundle_path   = fixture("ca.pem");

    // The call itself must not throw (noexcept).
    auto result = file_cert_source::make_file_cert_source(cfg, nullptr);

    // Result is either success or a typed error — never a thrown exception.
    // (If fixture files are missing this may be tls_cert_load_failed — that's
    //  tested separately below; here we just confirm no exception.)
    static_assert(std::is_same_v<
        decltype(result),
        fixpp::core::expected_t<std::shared_ptr<cert_source>>>,
        "make_file_cert_source must return expected_t<shared_ptr<cert_source>>");

    // On success, shared_ptr is non-null.
    if (result.has_value()) {
        ASSERT_NE(*result, nullptr) << "Success must yield a non-null shared_ptr";
    } else {
        // A typed error is acceptable; untyped throw is NOT (static_assert above).
        EXPECT_TRUE(result.error() == error::tls_cert_load_failed ||
                    result.error() == error::tls_cert_parse_failed)
            << "Failure must be a typed error, not a thrown exception";
    }
}

// ── (b) Parse-failure → tls_cert_parse_failed ────────────────────────────────
TEST(FileCertSourceFactory, MalformedPemSurfacesCertParseFailed) {
    // Write a temp file with garbage content to trigger a parse error.
    auto tmp = std::filesystem::temp_directory_path() / "fixpp_test_malformed.pem";
    {
        FILE* f = fopen(tmp.c_str(), "w");
        ASSERT_NE(f, nullptr);
        fputs("-----BEGIN CERTIFICATE-----\nNOT_BASE64!!!\n-----END CERTIFICATE-----\n", f);
        fclose(f);
    }

    file_cert_source::Config cfg;
    cfg.leaf_path        = tmp.string();
    cfg.private_key_path = fixture("leaf_rsa2048.key");

    auto result = file_cert_source::make_file_cert_source(cfg, nullptr);

    ASSERT_FALSE(result.has_value()) << "Malformed PEM must not succeed";
    EXPECT_EQ(result.error(), error::tls_cert_parse_failed)
        << "Malformed PEM must surface as tls_cert_parse_failed";

    std::filesystem::remove(tmp);
}

// ── (c) Load-failure — missing file → tls_cert_load_failed ───────────────────
TEST(FileCertSourceFactory, MissingFileSurfacesCertLoadFailed) {
    file_cert_source::Config cfg;
    cfg.leaf_path        = "/nonexistent/path/leaf.pem";
    cfg.private_key_path = "/nonexistent/path/leaf.key";

    auto result = file_cert_source::make_file_cert_source(cfg, nullptr);

    ASSERT_FALSE(result.has_value()) << "Missing file must not succeed";
    EXPECT_EQ(result.error(), error::tls_cert_load_failed)
        << "Missing file must surface as tls_cert_load_failed";
}

// ── (c) Load-failure — wrong passphrase → tls_cert_load_failed ───────────────
TEST(FileCertSourceFactory, WrongPasswordSurfacesCertLoadFailed) {
    file_cert_source::Config cfg;
    cfg.leaf_path        = fixture("leaf_encrypted_pem.pem");
    cfg.private_key_path = fixture("leaf_encrypted_pem.key");
    cfg.password_cb      = []() -> std::string { return "wrong_password"; };

    auto result = file_cert_source::make_file_cert_source(cfg, nullptr);

    ASSERT_FALSE(result.has_value()) << "Wrong passphrase must not succeed";
    EXPECT_EQ(result.error(), error::tls_cert_load_failed)
        << "Wrong passphrase must surface as tls_cert_load_failed";
}

// ── (d) Success: load_trust_anchors mirrors the throwing constructor ──────────
TEST(FileCertSourceFactory, SuccessReturnsUsableSharedPtr) {
    file_cert_source::Config cfg;
    cfg.leaf_path        = fixture("leaf_rsa2048.pem");
    cfg.private_key_path = fixture("leaf_rsa2048.key");
    cfg.ca_bundle_path   = fixture("ca.pem");

    auto result = file_cert_source::make_file_cert_source(cfg, nullptr);

    // Fixtures live at FIXPP_TLS_FIXTURE_DIR (compile-time baked) and are
    // checked into the repo — any factory failure here is a real test
    // failure, NOT a fixture-staging skip per [[feedback_simplify_pass_catches_9th_burn]].
    ASSERT_TRUE(result.has_value())
        << "make_file_cert_source must succeed against checked-in fixtures; "
        << "if this fires, FIXPP_TLS_FIXTURE_DIR is wrong or the fixtures Makefile drifted";

    auto cs = *result;
    ASSERT_NE(cs, nullptr);

    // load_trust_anchors must return a span (possibly empty for no ca_bundle).
    auto trust = cs->load_trust_anchors();
    ASSERT_TRUE(trust.has_value())
        << "load_trust_anchors must not error when ca_bundle loaded at construction";
    // At least the CA cert should be present.
    EXPECT_GE(trust->size(), 1u)
        << "Loaded ca_bundle_path must yield at least one trust anchor";
}

// ── (d) Success — ECDSA P-256 leaf (exercises certificate.cpp EC branch) ─────
// FR-020 covers ECDSA P-256/P-384 curves; the RSA happy-path above leaves
// parse_certificate_der's EVP_PKEY_EC branch + EVP_PKEY_get_group_name curve
// detection uncovered. This test fires those.
TEST(FileCertSourceFactory, SuccessLoadsEcdsaP256Leaf) {
    file_cert_source::Config cfg;
    cfg.leaf_path        = fixture("leaf_ecdsa_p256.pem");
    cfg.private_key_path = fixture("leaf_ecdsa_p256.key");
    cfg.ca_bundle_path   = fixture("ca.pem");

    auto result = file_cert_source::make_file_cert_source(cfg, nullptr);
    ASSERT_TRUE(result.has_value())
        << "make_file_cert_source must succeed against ECDSA P-256 fixture";
    ASSERT_NE(*result, nullptr);
}

// ── (d) Success — large SAN list (exercises certificate.cpp SAN parse loop) ──
// FR-019's max_san_entries default is 64; the leaf_san_64 fixture has exactly
// 64 DNS SANs. This exercises the SAN-collection loop + pmr_copy_str path in
// parse_certificate_der which the leaf_rsa2048 single-SAN happy-path skips.
TEST(FileCertSourceFactory, SuccessLoadsLeafWithLargeSanList) {
    file_cert_source::Config cfg;
    cfg.leaf_path        = fixture("leaf_san_64.pem");
    cfg.private_key_path = fixture("leaf_san_64.key");
    cfg.ca_bundle_path   = fixture("ca.pem");

    auto result = file_cert_source::make_file_cert_source(cfg, nullptr);
    ASSERT_TRUE(result.has_value())
        << "make_file_cert_source must succeed for 64-SAN leaf";
    ASSERT_NE(*result, nullptr);
}

// ── (d) Success — encrypted PEM with correct password ────────────────────────
// Companion to WrongPasswordSurfacesCertLoadFailed: confirms the happy-path
// (correct password via Config::password_cb) routes through pem_passwd_cb,
// PEM_read_bio_PrivateKey, and parse_certificate_der successfully — covering
// the file_cert_source.cpp encrypted-key load branch that the wrong-password
// test only short-circuits past.
TEST(FileCertSourceFactory, SuccessLoadsEncryptedPemWithCorrectPassword) {
    file_cert_source::Config cfg;
    cfg.leaf_path        = fixture("leaf_encrypted_pem.pem");
    cfg.private_key_path = fixture("leaf_encrypted_pem.key");
    cfg.ca_bundle_path   = fixture("ca.pem");
    cfg.password_cb      = []() -> std::string { return "test"; };  // matches fixtures/Makefile

    auto result = file_cert_source::make_file_cert_source(cfg, nullptr);
    ASSERT_TRUE(result.has_value())
        << "make_file_cert_source must succeed with correct encrypted-PEM password";
    ASSERT_NE(*result, nullptr);
}

// ── (d) Success — DER input format (exercises auto-detect non-PEM path) ─────
// file_cert_source auto-detects PEM vs DER by first-bytes magic. The PEM
// fixtures cover the "----BEGIN" PEM branch; this test fires the binary DER
// branch (read_file_bytes + d2i_AutoPrivateKey + d2i_X509 path in Impl::load).
TEST(FileCertSourceFactory, SuccessLoadsDerInputFormat) {
    file_cert_source::Config cfg;
    cfg.leaf_path        = fixture("leaf_rsa2048.der");
    cfg.private_key_path = fixture("leaf_rsa2048_pkcs8.der");
    cfg.ca_bundle_path   = fixture("ca.pem");

    auto result = file_cert_source::make_file_cert_source(cfg, nullptr);
    ASSERT_TRUE(result.has_value())
        << "make_file_cert_source must succeed for DER-encoded inputs";
    ASSERT_NE(*result, nullptr);
}

// ── (d) Success — multi-cert chain (exercises chain dedupe + intermediates) ──
// The chain_depth_8 fixture is an 8-cert chain (leaf + 7 intermediates). Loads
// trigger file_cert_source's chain parsing loop in Impl::load() that the
// single-cert ca.pem trust-anchor happy-path bypasses.
TEST(FileCertSourceFactory, SuccessLoadsChainDepth8) {
    file_cert_source::Config cfg;
    cfg.leaf_path        = fixture("chain_depth_8.pem");
    cfg.private_key_path = fixture("chain_depth_8.key");
    cfg.ca_bundle_path   = fixture("ca.pem");

    auto result = file_cert_source::make_file_cert_source(cfg, nullptr);
    ASSERT_TRUE(result.has_value())
        << "make_file_cert_source must succeed for chain_depth_8 fixture";
    ASSERT_NE(*result, nullptr);
}

// ── Gate-B/r1 F-3 negative tests + success-path signer assertion ─────────────
// [2g §4.2] line 376: file_cert_source::load_credentials returns local_credentials
// whose signer is software_key_ref{handle = key_, ...} unconditionally on the
// success path. data-model E-2: "default-construction is NOT permitted" for signer.
// [2g §6.6] tls_sign_callback_unavailable: null-handle signer is the exact
// broken state the variant exists to refuse.
//
// F-3 fix: Impl::load() now fails-closed when leaf_path or private_key_path is
// empty, surfacing tls_cert_load_failed before returning a null-handle bundle.

// ── Empty leaf_path must fail closed ─────────────────────────────────────────
TEST(FileCertSourceFactory, EmptyLeafPathSurfacesCertLoadFailed) {
    file_cert_source::Config cfg;
    cfg.leaf_path        = "";  // intentionally empty
    cfg.private_key_path = fixture("leaf_rsa2048.key");

    auto result = file_cert_source::make_file_cert_source(cfg, nullptr);
    ASSERT_FALSE(result.has_value())
        << "Empty leaf_path must not succeed";
    EXPECT_EQ(result.error(), error::tls_cert_load_failed)
        << "Empty leaf_path must surface as tls_cert_load_failed";
}

// ── Empty private_key_path must fail closed ───────────────────────────────────
TEST(FileCertSourceFactory, EmptyPrivateKeyPathSurfacesCertLoadFailed) {
    file_cert_source::Config cfg;
    cfg.leaf_path        = fixture("leaf_rsa2048.pem");
    cfg.private_key_path = "";  // intentionally empty

    auto result = file_cert_source::make_file_cert_source(cfg, nullptr);
    ASSERT_FALSE(result.has_value())
        << "Empty private_key_path must not succeed";
    EXPECT_EQ(result.error(), error::tls_cert_load_failed)
        << "Empty private_key_path must surface as tls_cert_load_failed";
}

// ── Both paths empty must fail closed ────────────────────────────────────────
TEST(FileCertSourceFactory, BothPathsEmptySurfacesCertLoadFailed) {
    file_cert_source::Config cfg;
    cfg.leaf_path        = "";  // intentionally empty
    cfg.private_key_path = "";  // intentionally empty

    auto result = file_cert_source::make_file_cert_source(cfg, nullptr);
    ASSERT_FALSE(result.has_value())
        << "Both empty paths must not succeed";
    EXPECT_EQ(result.error(), error::tls_cert_load_failed)
        << "Both empty paths must surface as tls_cert_load_failed";
}

// ── Success path: software_key_ref handle must be non-null ────────────────────
// Verifies [2g §4.2] line 376 contract: the success path always yields a
// software_key_ref with a non-null ossl_pkey handle. Previously, empty paths
// would silently produce a null-handle signer (data-model E-2 violation).
TEST(FileCertSourceFactory, SuccessPathSignerHandleIsNonNull) {
    file_cert_source::Config cfg;
    cfg.leaf_path        = fixture("leaf_rsa2048.pem");
    cfg.private_key_path = fixture("leaf_rsa2048.key");

    auto result = file_cert_source::make_file_cert_source(cfg, nullptr);
    ASSERT_TRUE(result.has_value())
        << "make_file_cert_source must succeed against checked-in fixtures";

    auto cs_ptr = *result;
    ASSERT_NE(cs_ptr, nullptr);

    // Drive load_credentials() to get the actual local_credentials bundle.
    asio::io_context ioc;
    fixpp::tls::local_credentials creds{};
    bool got_creds = false;
    bool creds_ok  = false;

    auto fut = asio::co_spawn(
        ioc,
        [&]() -> asio::awaitable<void> {
            auto r = co_await cs_ptr->load_credentials();
            if (r.has_value()) {
                creds = *r;
                got_creds = true;
                creds_ok  = true;
            }
        },
        asio::use_future);
    ioc.run();
    fut.get();

    ASSERT_TRUE(got_creds) << "load_credentials must succeed and coroutine must complete";
    ASSERT_TRUE(creds_ok) << "load_credentials must return a valid result";

    // [2g §4.2] line 376: signer MUST be software_key_ref with non-null handle.
    ASSERT_TRUE(std::holds_alternative<fixpp::tls::software_key_ref>(creds.signer))
        << "Success path signer must be software_key_ref variant (not async_signer_ref)";
    auto const& ref = std::get<fixpp::tls::software_key_ref>(creds.signer);
    EXPECT_NE(ref.handle.ossl_pkey, nullptr)
        << "software_key_ref handle must not be null on the success path";
}

}  // namespace
