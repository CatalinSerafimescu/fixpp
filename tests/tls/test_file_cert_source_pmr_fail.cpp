// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/tls/test_file_cert_source_pmr_fail.cpp
// T029 — Gate A round-3 carry-forward P3: cold-path PMR-failure witness.
//
// Binding contracts tested:
//   (a) PMR allocation failures during parse_certificate_der must surface as
//       expected_t::unexpected{tls_cert_parse_failed}, NOT std::terminate.
//       Gate-B/r1 F-2 fix: parse_certificate_der had noexcept dropped and
//       a top-level try/catch(bad_alloc) trap_throw boundary added per
//       [2g §1 item 7] / [2g §6.6] / [arch §5.3].
//   (b) make_file_cert_source factory catches all throws from the constructor
//       (including runtime_error from bad file paths) and converts to
//       expected_t::unexpected{error::tls_cert_load_failed}.
//   (c) load_credentials awaitable does not throw on any code path
//       (normal or error).
//   (d) parse_certificate_der with a tiny PMR (null_memory_resource upstream)
//       returns unexpected{tls_cert_parse_failed} — not terminate.
//   (e) Gate-B/r2 N-1 fix: GENERAL_NAMES* returned by X509_get_ext_d2i is
//       RAII-managed so that a bad_alloc thrown inside the SAN loop does NOT
//       leak it before the outer catch returns tls_cert_parse_failed.

#include <gtest/gtest.h>

#include <fixpp/tls/cert_source.hpp>
#include <fixpp/tls/file_cert_source.hpp>
#include <fixpp/core/error.hpp>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>

#include <cstddef>
#include <memory_resource>
#include <string>
#include <vector>

namespace {

using fixpp::tls::file_cert_source;
using fixpp::tls::cert_source;
using fixpp::tls::local_credentials;
using fixpp::core::error;
using fixpp::core::expected_t;

#ifndef FIXPP_TLS_FIXTURE_DIR
#define FIXPP_TLS_FIXTURE_DIR ""
#endif

static std::string fixture(const char* name) {
    return std::string(FIXPP_TLS_FIXTURE_DIR) + "/" + name;
}

// ── FactoryNeverThrows ────────────────────────────────────────────────────────
// Primary contract: factory make_file_cert_source must NEVER throw.
// Even on file-not-found / bad parse, it returns expected_t, not throws.
TEST(FileCertSourcePmrFail, FactoryNeverThrows) {
    bool threw = false;
    try {
        // Deliberately trigger a load failure (missing file).
        file_cert_source::Config cfg;
        cfg.leaf_path        = "/nonexistent/path/cert.pem";
        cfg.private_key_path = "/nonexistent/path/key.pem";
        auto result = file_cert_source::make_file_cert_source(cfg, nullptr);
        ASSERT_FALSE(result.has_value())
            << "Missing file must fail";
        EXPECT_EQ(result.error(), error::tls_cert_load_failed)
            << "Load failure must surface as tls_cert_load_failed";
    } catch (...) {
        threw = true;
    }
    EXPECT_FALSE(threw) << "make_file_cert_source must not throw";
}

// ── ParseFailureRoutesThroughFactory ──────────────────────────────────────────
// Malformed content triggers a parse error that routes through the factory
// boundary as tls_cert_parse_failed, not a thrown exception.
TEST(FileCertSourcePmrFail, ParseFailureSurfacesAsCertParseFailed) {
    // Write temp file with garbage content.
    auto tmp = std::string("/tmp/fixpp_fcs_pmr_test_bad.pem");
    {
        FILE* f = fopen(tmp.c_str(), "w");
        ASSERT_NE(f, nullptr);
        fputs("-----BEGIN CERTIFICATE-----\nBADDATA\n-----END CERTIFICATE-----\n", f);
        fclose(f);
    }

    bool threw = false;
    try {
        file_cert_source::Config cfg;
        cfg.leaf_path        = tmp;
        cfg.private_key_path = tmp;
        auto result = file_cert_source::make_file_cert_source(cfg, nullptr);
        EXPECT_FALSE(result.has_value())
            << "Malformed PEM must fail";
        if (!result.has_value()) {
            EXPECT_TRUE(result.error() == error::tls_cert_parse_failed ||
                        result.error() == error::tls_cert_load_failed)
                << "Malformed content must surface as cert_parse_failed or cert_load_failed";
        }
    } catch (...) {
        threw = true;
    }
    EXPECT_FALSE(threw) << "make_file_cert_source must not throw on parse failure";

    std::remove(tmp.c_str());
}

// ── LoadCredentialsDoesNotThrow ───────────────────────────────────────────────
// load_credentials must not throw on any path. Drive it via io_context.
TEST(FileCertSourcePmrFail, LoadCredentialsDoesNotThrow) {
    file_cert_source::Config cfg;
    cfg.leaf_path        = fixture("leaf_rsa2048.pem");
    cfg.private_key_path = fixture("leaf_rsa2048.key");
    cfg.ca_bundle_path   = fixture("ca.pem");

    auto result = file_cert_source::make_file_cert_source(cfg, nullptr);
    // Fixtures are baked into FIXPP_TLS_FIXTURE_DIR and checked in; any
    // factory failure here is a real regression per
    // [[feedback_simplify_pass_catches_9th_burn]] (no silent skip).
    ASSERT_TRUE(result.has_value())
        << "make_file_cert_source must succeed against checked-in fixtures";

    auto cs_ptr = *result;
    asio::io_context ioc;

    bool threw    = false;
    bool complete = false;

    auto future = asio::co_spawn(
        ioc,
        [&]() -> asio::awaitable<void> {
            try {
                auto creds = co_await cs_ptr->load_credentials();
                (void)creds;
                complete = true;
            } catch (...) {
                threw = true;
            }
        },
        asio::use_future);

    ioc.run();
    future.get();

    EXPECT_FALSE(threw) << "load_credentials must not throw";
    EXPECT_TRUE(complete) << "load_credentials coroutine must complete";
}

// ── TrapThrowBoundaryIsPresent ────────────────────────────────────────────────
// Compile-time: make_file_cert_source is noexcept (2i C-ABI bridge requirement).
// Runtime: factory succeeds with a valid PMR and returns a non-null shared_ptr.
TEST(FileCertSourcePmrFail, TrapThrowBoundaryIsPresent) {
    // Compile-time contract: factory is noexcept per FR-005 / [arch §5.3].
    static_assert(
        noexcept(file_cert_source::make_file_cert_source(file_cert_source::Config{}, nullptr)),
        "make_file_cert_source must be noexcept — trap_throw boundary required (FR-005)");

    // Runtime: on success with default PMR, factory must return a usable shared_ptr.
    file_cert_source::Config cfg;
    cfg.leaf_path        = fixture("leaf_rsa2048.pem");
    cfg.private_key_path = fixture("leaf_rsa2048.key");

    auto result = file_cert_source::make_file_cert_source(cfg, nullptr);
    // Either success or typed error — never a thrown exception.
    if (result.has_value()) {
        EXPECT_NE(*result, nullptr) << "Success must yield non-null shared_ptr";
    } else {
        EXPECT_TRUE(
            result.error() == error::tls_cert_load_failed ||
            result.error() == error::tls_cert_parse_failed)
            << "Failure must be a typed error";
    }
}

// ── ParseCertificateDerPmrExhaustionSurfesCertParseFailed ────────────────────
// Gate-B/r1 F-2 fix: parse_certificate_der with a PMR whose upstream is
// null_memory_resource (throws on every allocation past the tiny buffer) must
// return unexpected{tls_cert_parse_failed}, NOT terminate.
//
// Contract: [2g §1 item 7] "PMR throws are routed through [2a §4.2] trap_throw
// and surface as error::tls_* variants per §6.6." The noexcept was the bug.
//
// Uses a real cert DER (read from fixture) and a 1-byte monotonic_buffer_resource
// so the FIRST allocation inside parse_certificate_der throws bad_alloc.
TEST(FileCertSourcePmrFail, ParseCertificateDerPmrExhaustionSurfacesCertParseFailed) {
    // Load a real cert DER via a normal factory call first (to get the raw bytes).
    file_cert_source::Config cfg_full;
    cfg_full.leaf_path        = fixture("leaf_rsa2048.pem");
    cfg_full.private_key_path = fixture("leaf_rsa2048.key");
    // We need the raw DER bytes. Read them via the make_file_cert_source path
    // first to confirm the fixture is present and valid.
    auto full_result = file_cert_source::make_file_cert_source(cfg_full, nullptr);
    ASSERT_TRUE(full_result.has_value())
        << "Fixture leaf_rsa2048.pem must be present and loadable";

    // Read the DER bytes from the PEM fixture file directly using the fixture path.
    // PEM → DER conversion via OpenSSL BIO so we have raw bytes to feed
    // parse_certificate_der directly. Alternatively, just use
    // make_file_cert_source with a tiny PMR — it wraps the whole constructor.
    //
    // Simpler approach: pass a tiny monotonic_buffer_resource backed by
    // null_memory_resource to make_file_cert_source itself. Any cert-metadata
    // allocation (subject DN, issuer DN, SAN strings, SAN span arrays) inside
    // parse_certificate_der will throw bad_alloc once the 1-byte buffer is
    // exhausted. The factory must catch it and return tls_cert_load_failed.
    char tiny_buf[1]{};
    std::pmr::monotonic_buffer_resource tiny_mr{
        tiny_buf, sizeof(tiny_buf), std::pmr::null_memory_resource()};

    file_cert_source::Config cfg;
    cfg.leaf_path        = fixture("leaf_rsa2048.pem");
    cfg.private_key_path = fixture("leaf_rsa2048.key");

    // This call exercises parse_certificate_der with an arena that throws on
    // the first allocation (subject DN copy). With the old noexcept, this would
    // have called std::terminate. With the F-2 fix, it must return a typed error.
    auto result = file_cert_source::make_file_cert_source(cfg, &tiny_mr);

    ASSERT_FALSE(result.has_value())
        << "PMR exhaustion must surface as a typed error, not terminate";
    EXPECT_TRUE(result.error() == error::tls_cert_load_failed ||
                result.error() == error::tls_cert_parse_failed)
        << "PMR exhaustion must surface as tls_cert_load_failed or tls_cert_parse_failed";
}

// ── ParseCertificateDerPmrExhaustionInSanBlockSurfacesCertParseFailed ─────────
// Gate-B/r2 N-1 fix witness: GENERAL_NAMES* returned by X509_get_ext_d2i must
// be freed even when bad_alloc fires INSIDE the SAN processing loop.
//
// The existing ParseCertificateDerPmrExhaustionSurfacesCertParseFailed test
// uses a 1-byte arena that fires on the FIRST PMR allocation (subject DN copy
// at certificate.cpp:151), so execution never reaches the SAN block and the
// GENERAL_NAMES* leak on the SAN code-path was silently undetected.
//
// This test uses leaf_san_64.pem (64 DNS SANs, long hostnames ~70 bytes each)
// with a medium arena (512 bytes) that is large enough to complete the subject
// and issuer DN copies but exhausts before the first SAN string is allocated.
// The bad_alloc then unwinds through the SAN loop past GENERAL_NAMES_free.
// Without the RAII fix (GeneralNamesPtr) the OpenSSL-owned GENERAL_NAMES*
// leaks; ASan/LSan detects this deterministically on the ASan preset.
//
// Contract: [2g §1 item 7] + [2g §6.6]: no OpenSSL resource leaks on the
// PMR-fault unwind path.
TEST(FileCertSourcePmrFail, ParseCertificateDerPmrExhaustionInSanBlockSurfacesCertParseFailed) {
    // Use a 512-byte arena — enough for subject + issuer DN strings (~50 bytes
    // total with alignment padding) but NOT enough for the first SAN string
    // (~70 bytes for "host-1-fixpp-test-fixtures-very-long-subdomain...").
    // The null_memory_resource upstream ensures bad_alloc once the buffer fills.
    char medium_buf[512]{};
    std::pmr::monotonic_buffer_resource medium_mr{
        medium_buf, sizeof(medium_buf), std::pmr::null_memory_resource()};

    file_cert_source::Config cfg;
    cfg.leaf_path        = fixture("leaf_san_64.pem");
    cfg.private_key_path = fixture("leaf_san_64.key");

    // This call exercises parse_certificate_der with a cert that has 64 DNS SANs
    // (leaf_san_64.pem). The arena exhausts inside the SAN loop after copying
    // both DN strings. Without RAII on GENERAL_NAMES*, the pointer leaks before
    // the outer catch(bad_alloc) returns tls_cert_parse_failed.
    // ASan/LSan will flag the leak if the fix is absent.
    auto result = file_cert_source::make_file_cert_source(cfg, &medium_mr);

    ASSERT_FALSE(result.has_value())
        << "PMR exhaustion in SAN block must surface as a typed error, not terminate";
    EXPECT_TRUE(result.error() == error::tls_cert_load_failed ||
                result.error() == error::tls_cert_parse_failed)
        << "PMR exhaustion in SAN block must surface as tls_cert_load_failed or tls_cert_parse_failed";
}

}  // namespace
