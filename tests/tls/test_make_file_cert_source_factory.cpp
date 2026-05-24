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

#include <filesystem>
#include <memory>
#include <memory_resource>
#include <string>

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

}  // namespace
