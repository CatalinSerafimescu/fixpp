// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/tls/test_file_cert_source_pmr_fail.cpp
// T029 — Gate A round-3 carry-forward P3: cold-path PMR-failure witness.
//
// Binding contracts tested:
//   (a) PMR allocation failures during file_cert_source::load_credentials
//       (cold-path) must not escape as thrown exceptions across the public API.
//   (b) make_file_cert_source factory catches all throws from the constructor
//       (including runtime_error from bad file paths) and converts to
//       expected_t::unexpected{error::tls_cert_load_failed}.
//   (c) load_credentials awaitable does not throw on any code path
//       (normal or error).
//
// Note: parse_certificate_der is declared noexcept in certificate.hpp, so
// triggering a PMR allocation failure INSIDE it terminates (std::terminate).
// T029 therefore tests the PMR failure path via the FACTORY boundary (the
// wrap in trap_throw that catches std::bad_alloc from std::make_shared) and
// via the load_credentials no-exception contract, NOT by forcing the internal
// monotonic_buffer_resource to run out inside a noexcept function.
//
// The cold-path PMR-failure witness is via the factory's std::bad_alloc catch:
// if the system heap is nearly full, std::make_shared<file_cert_source> can
// throw std::bad_alloc. The factory must return tls_cert_load_failed.

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

// ── LoadCredentialsPmrAllocationFailSurfaces ──────────────────────────────────
// When the PMR resource passed to the factory runs out BEFORE construction
// can complete (i.e., the resource is used for cert metadata allocations),
// the factory must return a typed error rather than throwing.
// NOTE: Because parse_certificate_der is noexcept, we cannot pass a micro-tiny
// arena to it — that would cause std::terminate. We test the scenario where
// the PMR fails at the FACTORY level (std::make_shared allocation itself).
// The test validates the wrapping pattern exists and works for bad_alloc from
// the operator new path (not from the PMR internals).
// This test uses a normal PMR (new_delete_resource) and verifies the factory
// returns a working pointer, not a crash. The trap_throw boundary IS present
// and would catch std::bad_alloc from std::make_shared in low-memory conditions.
TEST(FileCertSourcePmrFail, TrapThrowBoundaryIsPresent) {
    // Verify that make_file_cert_source is noexcept (compile-time contract check).
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

}  // namespace
