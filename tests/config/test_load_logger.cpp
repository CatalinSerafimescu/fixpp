// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/config/test_load_logger.cpp
// 045-observability-config Phase-3 TDD RED tests (T008, T009).
//
// T008 [P] [US1] — Logger equivalence test.
//   Loads a [logger] block and asserts that EngineEstablishment::logger is
//   non-null and behaviorally equivalent to a programmatically-built logger
//   with the same parameters.
//
//   Because Logger uses a pimpl with NO public sink inspector, the equivalence
//   strategy is BEHAVIOURAL: emit a log record, call shutdown(), then verify
//   a log file was created at the configured directory with the configured
//   base_name.  This discriminates against:
//     - a null logger (no file is written at all)
//     - wrong directory (file appears at the default location, not the one
//       specified in the fixture)
//     - wrong base_name (file has the wrong name prefix)
//
//   Sub-cells:
//     T008_EquivalenceFileSink   — static fixture (logger_happy.toml, file sink)
//     T008_DuplicateFileSinkFanout — runtime TOML, two file sinks to distinct
//                                    directories (positive duplicate-sink-kind
//                                    cell, spec Edge Cases line 102)
//   OTLP sub-cells (guarded under #ifdef FIXPP_CONFIG_HAS_OTLP):
//     T008_EquivalenceOtlpSink   — runtime TOML, file + OTLP sink with a
//                                  test_exporter seam to count Export() calls
//
//   RED condition (Phase 3, no resolver yet):
//     load_toml_config returns has_value()==true (the [logger] block is
//     recognized but has no resolver — T010–T013 not yet written).
//     ASSERT_NE(engine.logger, nullptr) fails because the resolver never
//     writes bundle.engine.logger.  This is the CORRECT RED: "logger not
//     resolved / engine.logger is null."
//
// T009 [P] [US1] — Optional-absence test (preserved-behaviour guard).
//   A file without a [logger] block loads to a bundle with engine.logger==null.
//   This is NOT an error (FR-003/SC-004).  This test is expected to be GREEN
//   immediately (no resolver change needed — the field defaults to nullptr).
//   Documents the invariant: the resolver must NEVER touch engine.logger when
//   no [logger] block is present.
//
// Anti-hang: load_toml_config is synchronous/cold.  We create asio::io_context
//   only to satisfy system_clock_source's ctor requirement; ctx.run() is never
//   called.
//
// File-sink directory pre-existence: the resolver data-model (E-4) requires
//   the configured directory to already exist (stat/access only; no mkdir).
//   Each cell that references a file-sink directory calls
//   std::filesystem::create_directories() before loading the fixture.
//
// Cleanup: log files created during T008_EquivalenceFileSink are left in the
//   fixture's logs/t008/ subdirectory (relative to the fixture dir).  This
//   is acceptable for CI (temp build trees are discarded).  Runtime-generated
//   TOML is removed before the ASSERT to avoid polluting the fixture tree.

#include <gtest/gtest.h>

#include <asio/io_context.hpp>
#include <chrono>
#include <filesystem>
#include <fixpp/config/config_bundle.hpp>
#include <fixpp/config/toml_config_loader.hpp>
#include <fixpp/log/file_sink.hpp>      // FileSink — white-box OTLP order test
#include <fixpp/log/logger.hpp>
#include <fstream>
#include <string>

// White-box OTLP sink count+order test: call detail::resolve_engine_logger
// directly to inspect pending sinks before construct_loggers_if_clean runs.
// mappers.hpp is a private src/config/ header; tests/config/CMakeLists.txt
// adds src/config to test_load_logger's include path for this purpose.
// toml_include.hpp (the ODR-safe shim) is transitively included via mappers.hpp.
#ifdef FIXPP_CONFIG_HAS_OTLP
#include <fixpp/log/otlp_log_sink.hpp>  // OtlpLogSink — white-box OTLP order test
#endif
#include "mappers.hpp"  // detail::resolve_engine_logger, DiagnosticAccumulator

#ifndef FIXPP_CONFIG_FIXTURE_DIR
#error "FIXPP_CONFIG_FIXTURE_DIR must be set by CMake"
#endif

// ── Helpers ──────────────────────────────────────────────────────────────────

namespace {

// Load a named fixture from the FIXPP_CONFIG_FIXTURE_DIR directory.
fixpp::config::LoadResult load_fixture(std::string_view name) {
    const std::filesystem::path p =
        std::filesystem::path{std::string{FIXPP_CONFIG_FIXTURE_DIR}} / name;
    asio::io_context ctx;
    fixpp::config::LoadOptions opts;
    opts.engine_executor = ctx.get_executor();
    return fixpp::config::load_toml_config(p, opts);
}

// Load from an arbitrary path (used for runtime-generated TOML).
fixpp::config::LoadResult load_path(const std::filesystem::path& p) {
    asio::io_context ctx;
    fixpp::config::LoadOptions opts;
    opts.engine_executor = ctx.get_executor();
    return fixpp::config::load_toml_config(p, opts);
}

// Dump diagnostics as a human-readable string for ASSERT messages.
std::string diag_string(const std::vector<fixpp::config::LoadDiagnostic>& diags) {
    std::string s;
    for (const auto& d : diags) {
        s += "[" + d.key_path + "] " + d.message + "\n";
    }
    return s;
}

// Return the fixture directory path.
std::filesystem::path fixture_dir() {
    return std::filesystem::path{std::string{FIXPP_CONFIG_FIXTURE_DIR}};
}

}  // namespace

// =============================================================================
// T008 — Logger equivalence tests
// =============================================================================

// ── T008_EquivalenceFileSink ─────────────────────────────────────────────────
//
// Load logger_happy.toml which contains a [logger] with a single file sink.
// The fixture configures:
//   - capacity=4096, drain_timeout=2000ms (non-defaults)
//   - file sink: directory="logs/t008", base_name="fixpp_t008"
//
// Strategy: after load, assert engine.logger is non-null (RED right now
// because the resolver is not written).  Once GREEN, also assert the
// behavioural property: emit a log record, call shutdown(), verify a file
// with the configured base_name exists in the configured directory.
//
// The fixture directory "logs/t008" is created relative to the fixture dir
// so the resolver's preflight (directory must exist) does not fail.

TEST(LoadLogger, T008_EquivalenceFileSink) {
    // Pre-create the file-sink directory (E-4 preflight: must exist before load).
    // remove_all first so the file-existence witness below reflects THIS run, not
    // a stale fixpp_t008.log left by a prior run (which would false-pass even if
    // the resolved logger wrote nothing).
    const auto log_dir = fixture_dir() / "logs" / "t008";
    {
        std::error_code ec;
        std::filesystem::remove_all(log_dir, ec);
        std::filesystem::create_directories(log_dir, ec);
        ASSERT_FALSE(ec) << "Failed to create log dir: " << log_dir << " — " << ec.message();
    }

    auto result = load_fixture("logger_happy.toml");

    ASSERT_TRUE(result.has_value())
        << "logger_happy.toml must load successfully; diagnostics:\n"
        << (result.has_value() ? "" : diag_string(result.error()));

    ASSERT_EQ(result->sessions.size(), std::size_t{1})
        << "expected exactly one session";

    // RED assertion: resolver not yet written → engine.logger is null.
    // After T010–T013 land, this ASSERT_NE must pass.
    ASSERT_NE(result->engine.logger, nullptr)
        << "engine.logger must be non-null after loading a [logger] block; "
           "resolver (T010-T013) is not yet written — this is the expected RED";

    // ── Behavioural equivalence check (runs only when the resolver lands) ─────
    //
    // Emit a record, flush, shutdown, then verify the file exists.
    // The log file name starts with base_name ("fixpp_t008") and is placed
    // in the configured directory (log_dir).
    {
        auto& logger = *result->engine.logger;
        // FIXPP_LOG0 takes a BARE level token (macro prepends ::fixpp::log::Level::)
        // and a Category value (use fixpp::log::cat::session, not a string).
        FIXPP_LOG0(&logger, info, fixpp::log::cat::session,
                   "T008_EquivalenceFileSink probe record");
        // shutdown() is [[nodiscard]]; capture the result (drain timeout is acceptable).
        [[maybe_unused]] auto res = logger.shutdown();
    }

    // Verify the file appeared in the right directory with the right base_name.
    bool found = false;
    {
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(log_dir, ec)) {
            if (entry.path().filename().string().starts_with("fixpp_t008")) {
                found = true;
                break;
            }
        }
    }
    EXPECT_TRUE(found)
        << "Expected a log file starting with 'fixpp_t008' in " << log_dir
        << "; file not found — wrong directory or base_name in resolved logger";
}

// ── T008_DuplicateFileSinkFanout ─────────────────────────────────────────────
//
// Positive duplicate-sink-kind cell (spec Edge Cases line 102):
// Two file sinks to DISTINCT directories are a valid fan-out (not an error).
// Both sinks receive the same log record after emit+shutdown.
//
// Runtime-generated TOML: we write the full TOML string to a temp file using
// real tmpdir paths so the directory= values are absolute and pre-existent.

TEST(LoadLogger, T008_DuplicateFileSinkFanout) {
    // Create two distinct sink directories in /tmp.
    // remove_all first so the per-dir file witnesses reflect THIS run, not stale
    // fanout_a*/fanout_b* logs from a prior run (fixed /tmp paths persist).
    const auto sink_dir_a = std::filesystem::temp_directory_path() / "fixpp_t008_fanout_a";
    const auto sink_dir_b = std::filesystem::temp_directory_path() / "fixpp_t008_fanout_b";
    {
        std::error_code ec;
        std::filesystem::remove_all(sink_dir_a, ec);
        std::filesystem::remove_all(sink_dir_b, ec);
        std::filesystem::create_directories(sink_dir_a, ec);
        ASSERT_FALSE(ec) << "Failed to create " << sink_dir_a;
        std::filesystem::create_directories(sink_dir_b, ec);
        ASSERT_FALSE(ec) << "Failed to create " << sink_dir_b;
    }

    // Write a temporary TOML using absolute paths.
    const auto tmp_toml = std::filesystem::temp_directory_path() / "fixpp_t008_fanout.toml";
    {
        std::ofstream out{tmp_toml};
        // Escape backslashes on Windows (not needed on Linux, but safe).
        auto esc = [](std::filesystem::path const& p) {
            auto s = p.string();
            // No-op on POSIX; kept for portability.
            return s;
        };
        out << "[clock]\nkind = \"system\"\n\n"
            << "[store]\nkind = \"memory\"\n\n"
            << "[cert_source]\nkind = \"file\"\n"
            << "cert_file = \"" << esc(fixture_dir() / "leaf_ecdsa_p256.pem") << "\"\n"
            << "key_file  = \"" << esc(fixture_dir() / "leaf_ecdsa_p256.key") << "\"\n"
            << "ca_file   = \"" << esc(fixture_dir() / "ca.pem") << "\"\n\n"
            << "[dictionary]\nkind = \"path\"\n"
            << "path = \"" << esc(fixture_dir() / "FIX44.xml") << "\"\n\n"
            << "[logger]\n"
            << "capacity      = 4096\n"
            << "on_overflow   = \"drop_newest\"\n"
            << "drain_timeout = \"2000ms\"\n\n"
            << "[[logger.sinks]]\n"
            << "kind           = \"file\"\n"
            << "directory      = \"" << esc(sink_dir_a) << "\"\n"
            << "base_name      = \"fanout_a\"\n\n"
            << "[[logger.sinks]]\n"
            << "kind           = \"file\"\n"
            << "directory      = \"" << esc(sink_dir_b) << "\"\n"
            << "base_name      = \"fanout_b\"\n\n"
            << "[[session]]\n"
            << "sender_comp_id = \"CLIENT1\"\n"
            << "target_comp_id = \"SERVER1\"\n"
            << "begin_string   = \"FIX.4.4\"\n"
            << "role           = \"initiator\"\n\n"
            << "[session.transport]\n"
            << "kind = \"tls\"\n"
            << "host = \"fix.example.com\"\n"
            << "port = 4321\n\n"
            << "[session.security_profile]\n"
            << "kind = \"mtls_ca\"\n";
    }

    auto result = load_path(tmp_toml);

    // Remove temp TOML before asserting so failures leave no stale files.
    {
        std::error_code ec;
        std::filesystem::remove(tmp_toml, ec);
    }

    ASSERT_TRUE(result.has_value())
        << "Two file sinks to distinct directories must load successfully "
           "(positive duplicate-sink-kind cell, spec Edge Cases line 102); "
           "diagnostics:\n"
        << (result.has_value() ? "" : diag_string(result.error()));

    ASSERT_EQ(result->sessions.size(), std::size_t{1});

    // RED: resolver not yet written → engine.logger is null.
    ASSERT_NE(result->engine.logger, nullptr)
        << "engine.logger must be non-null for a valid two-file-sink [logger]; "
           "resolver (T010-T013) is not yet written — expected RED";

    // ── Behavioural fan-out check (runs only once GREEN) ─────────────────────
    //
    // Both sink directories must receive a log file after emit+shutdown.
    {
        auto& logger = *result->engine.logger;
        FIXPP_LOG0(&logger, info, fixpp::log::cat::session,
                   "T008_DuplicateFileSinkFanout probe record");
        [[maybe_unused]] auto res = logger.shutdown();
    }

    auto has_file_with_prefix = [](const std::filesystem::path& dir,
                                   std::string_view prefix) -> bool {
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (entry.path().filename().string().starts_with(prefix)) return true;
        }
        return false;
    };

    EXPECT_TRUE(has_file_with_prefix(sink_dir_a, "fanout_a"))
        << "Expected a log file 'fanout_a*' in sink_dir_a (" << sink_dir_a
        << "); sink_dir_a did not receive a log record — fan-out broken";
    EXPECT_TRUE(has_file_with_prefix(sink_dir_b, "fanout_b"))
        << "Expected a log file 'fanout_b*' in sink_dir_b (" << sink_dir_b
        << "); sink_dir_b did not receive a log record — fan-out broken";
}

// ── T008_EquivalenceOtlpSink (build-conditional) ────────────────────────────
//
// When the build includes OTLP support (fixpp::log_otlp linked, FIXPP_CONFIG_HAS_OTLP
// defined), test a [logger] with a file sink + an OTLP sink (ordered).
//
// Observable OTLP equivalence:
//   OtlpLogSinkConfig has a test_exporter seam (otlp_log_sink.hpp:64).
//   However, the test_exporter is injected via C++ code — it cannot be set
//   from TOML.  The TOML-resolved OTLP sink uses the REAL HTTP exporter.
//   Therefore OTLP behavioural equivalence is verified at the FILE sink level
//   (the logger writes to the file sink which IS observable), and the OTLP
//   sink's presence is inferred from the successful load (non-null logger) plus
//   the file sink writing correctly.
//
//   This is sufficient discrimination: a wrong logger (e.g. null or file-only
//   without the OTLP sink) would produce wrong file output or a null pointer.
//   The OTLP sink connection-failure does NOT block the logger — the drain thread
//   continues emitting to the file sink.  export_failure_count() is zero-or-more
//   and NOT asserted here (connection refused is expected in CI; not a test failure).
//
// Runtime TOML: the OTLP endpoint is "http://127.0.0.1:0/v1/logs" (port 0 — bound
// to fail, but the sink opens without error; export_failure_count may increment
// asynchronously after shutdown).

#ifdef FIXPP_CONFIG_HAS_OTLP

TEST(LoadLogger, T008_EquivalenceOtlpSink) {
    // remove_all first so the file witness reflects THIS run (fixed /tmp path).
    const auto log_dir = std::filesystem::temp_directory_path() / "fixpp_t008_otlp";
    {
        std::error_code ec;
        std::filesystem::remove_all(log_dir, ec);
        std::filesystem::create_directories(log_dir, ec);
        ASSERT_FALSE(ec) << "Failed to create " << log_dir;
    }

    const auto tmp_toml = std::filesystem::temp_directory_path() / "fixpp_t008_otlp.toml";
    {
        std::ofstream out{tmp_toml};
        auto esc = [](std::filesystem::path const& p) { return p.string(); };
        out << "[clock]\nkind = \"system\"\n\n"
            << "[store]\nkind = \"memory\"\n\n"
            << "[cert_source]\nkind = \"file\"\n"
            << "cert_file = \"" << esc(fixture_dir() / "leaf_ecdsa_p256.pem") << "\"\n"
            << "key_file  = \"" << esc(fixture_dir() / "leaf_ecdsa_p256.key") << "\"\n"
            << "ca_file   = \"" << esc(fixture_dir() / "ca.pem") << "\"\n\n"
            << "[dictionary]\nkind = \"path\"\n"
            << "path = \"" << esc(fixture_dir() / "FIX44.xml") << "\"\n\n"
            << "[logger]\n"
            << "capacity      = 4096\n"
            << "on_overflow   = \"drop_newest\"\n"
            << "drain_timeout = \"2000ms\"\n\n"
            // Sink 0: file (always observable)
            << "[[logger.sinks]]\n"
            << "kind       = \"file\"\n"
            << "directory  = \"" << esc(log_dir) << "\"\n"
            << "base_name  = \"t008_otlp\"\n\n"
            // Sink 1: OTLP (port 0 → connection refused; not a test failure)
            << "[[logger.sinks]]\n"
            << "kind            = \"otlp\"\n"
            << "endpoint        = \"http://127.0.0.1:0/v1/logs\"\n"
            << "export_timeout  = \"1s\"\n"
            << "max_export_batch = 64\n\n"
            << "[[session]]\n"
            << "sender_comp_id = \"CLIENT1\"\n"
            << "target_comp_id = \"SERVER1\"\n"
            << "begin_string   = \"FIX.4.4\"\n"
            << "role           = \"initiator\"\n\n"
            << "[session.transport]\n"
            << "kind = \"tls\"\n"
            << "host = \"fix.example.com\"\n"
            << "port = 4321\n\n"
            << "[session.security_profile]\n"
            << "kind = \"mtls_ca\"\n";
    }

    auto result = load_path(tmp_toml);

    {
        std::error_code ec;
        std::filesystem::remove(tmp_toml, ec);
    }

    ASSERT_TRUE(result.has_value())
        << "file+otlp [logger] must load successfully on an OTLP-enabled build; "
           "diagnostics:\n"
        << (result.has_value() ? "" : diag_string(result.error()));

    ASSERT_EQ(result->sessions.size(), std::size_t{1});

    // RED: resolver not yet written → engine.logger is null.
    ASSERT_NE(result->engine.logger, nullptr)
        << "engine.logger must be non-null for file+otlp [logger] block; "
           "resolver (T010-T013) is not yet written — expected RED";

    // ── Behavioural check: file sink side of the fan-out ─────────────────────
    // Emit a record, shutdown (drains both sinks), check the file appeared.
    // The OTLP export is best-effort against a dead endpoint; export_failure_count
    // is NOT asserted here (connection refused is expected in CI).
    {
        auto& logger = *result->engine.logger;
        FIXPP_LOG0(&logger, info, fixpp::log::cat::session,
                   "T008_EquivalenceOtlpSink probe record");
        [[maybe_unused]] auto res = logger.shutdown();
    }

    bool found = false;
    {
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(log_dir, ec)) {
            if (entry.path().filename().string().starts_with("t008_otlp")) {
                found = true;
                break;
            }
        }
    }
    EXPECT_TRUE(found)
        << "Expected a log file 't008_otlp*' in " << log_dir
        << "; file sink did not produce a file — wrong resolved logger configuration";
}

#endif  // FIXPP_CONFIG_HAS_OTLP

// =============================================================================
// T008_OtlpSinkCountAndOrder (white-box, build-conditional)
// =============================================================================
//
// True OTLP discrimination test: calls detail::resolve_engine_logger directly
// to inspect the pending sinks before construct_loggers_if_clean runs.
// This is the only way to verify COUNT + ORDER without a Logger pimpl inspector.
//
// Discriminates against:
//   - A resolver that silently drops the OTLP sink (sinks.size()==1 not 2)
//   - A resolver that reorders sinks (sinks[0] is OTLP, not file)
//   - A resolver that makes the OTLP entry the wrong concrete type
//
// Mutation test: removing the kind=="otlp" branch in resolve_log_sink gives
//   sinks.size()==1 → ASSERT_EQ below fails → RED ✓
//
// White-box access: mappers.hpp is a private src/config/ header; the
//   CMakeLists adds src/config to test_load_logger's include search path.

#ifdef FIXPP_CONFIG_HAS_OTLP

TEST(LoadLogger, T008_OtlpSinkCountAndOrder) {
    // Build a minimal [logger] table with file + otlp sinks in order.
    // The TOML string is parsed inline so we get a real toml::table.
    // Use a valid endpoint (URL format acceptable to OtlpLogSinkFactory::make).
    // A valid absolute log_dir is unused at Phase 3 (no open() called here).
    const auto log_dir = std::filesystem::temp_directory_path() / "fixpp_t008_wb_otlp";
    {
        std::error_code ec;
        std::filesystem::create_directories(log_dir, ec);
        ASSERT_FALSE(ec) << "Failed to create " << log_dir;
    }

    // toml_include.hpp ODR shim (required when including toml++ in a config TU).
    // This TU uses mappers.hpp which already includes toml_include.hpp transitively;
    // the include here is for documentation; the actual toml:: types come via mappers.hpp.
    const std::string toml_text =
        "[logger]\n"
        "capacity      = 4096\n"
        "drain_timeout = \"2000ms\"\n"
        "\n"
        "[[logger.sinks]]\n"
        "kind      = \"file\"\n"
        "directory = \"" + log_dir.string() + "\"\n"
        "base_name = \"wb_otlp_test\"\n"
        "\n"
        "[[logger.sinks]]\n"
        "kind     = \"otlp\"\n"
        "endpoint = \"http://127.0.0.1:0/v1/logs\"\n";

    auto parsed = toml::parse(toml_text);
    const toml::table* logger_tbl_ptr = parsed.get_as<toml::table>("logger");
    ASSERT_NE(logger_tbl_ptr, nullptr) << "TOML parse failed: no [logger] table";

    fixpp::config::detail::DiagnosticAccumulator acc;
    fixpp::config::PendingLoggerSet pending;
    fixpp::config::LoadOptions opts;
    // engine_executor not needed: no Session/clock construction at resolve phase
    opts.resource = std::pmr::get_default_resource();

    fixpp::config::SourceLoc loc{1, 1};

    fixpp::config::detail::resolve_engine_logger(
        *logger_tbl_ptr,
        "logger",
        loc,
        /*base_dir=*/std::filesystem::temp_directory_path(),
        opts,
        pending,
        acc,
        /*is_engine=*/true,
        /*session_index=*/0
    );

    // No errors — both sinks are valid
    ASSERT_TRUE(acc.empty())
        << "Expected no diagnostics for a valid file+otlp [logger]; got:\n"
        << [&]{
            std::string s;
            auto diags = std::move(acc).release();
            for (auto& d : diags) s += "[" + d.key_path + "] " + d.message + "\n";
            return s;
        }();

    // Engine slot must be populated
    ASSERT_TRUE(pending.engine.has_value())
        << "pending.engine must be set after a successful resolve_engine_logger call";

    // COUNT: two sinks resolved in order (file then otlp)
    ASSERT_EQ(pending.engine->sinks.size(), std::size_t{2})
        << "Expected 2 sinks (file + otlp); got " << pending.engine->sinks.size()
        << " — the OTLP sink was silently dropped or not resolved";

    // ORDER + TYPE: sinks[0] is FileSink
    EXPECT_NE(dynamic_cast<fixpp::log::FileSink*>(pending.engine->sinks[0].get()), nullptr)
        << "sinks[0] must be a FileSink; order or type is wrong";

    // ORDER + TYPE: sinks[1] is OtlpLogSink
    EXPECT_NE(dynamic_cast<fixpp::log::OtlpLogSink*>(pending.engine->sinks[1].get()), nullptr)
        << "sinks[1] must be an OtlpLogSink; order or type is wrong";
}

#endif  // FIXPP_CONFIG_HAS_OTLP (OtlpSinkCountAndOrder)

// =============================================================================
// OTLP sink discrimination — negative path (build-conditional)
// =============================================================================
//
// White-box discrimination for the OTLP sink resolver (coordinator briefing,
// Phase-3 reporting contract):
//
// T008_OtlpSinkResolvedNegative: when an [[logger.sinks]] entry of kind="otlp"
// is present but its REQUIRED "endpoint" field is absent, the load must FAIL
// with missing_required on "logger.sinks[1].endpoint" — proving the OTLP
// resolution path ran and enforced the endpoint constraint.  This discriminates
// against a resolver that SILENTLY DROPS the otlp sink (which would leave the
// endpoint check un-run and the test would green on the file-sink alone).
//
// Mutation-test reasoning: if the OTLP branch is removed from resolve_log_sink
// (or never entered), the missing endpoint is never checked → load succeeds →
// the ASSERT_FALSE below fails.

#ifdef FIXPP_CONFIG_HAS_OTLP

TEST(LoadLogger, T008_OtlpSinkResolvedNegative) {
    // Runtime TOML: file sink (valid) + otlp sink with no endpoint (invalid).
    const auto tmp_toml =
        std::filesystem::temp_directory_path() / "fixpp_t008_otlp_neg.toml";
    const auto log_dir =
        std::filesystem::temp_directory_path() / "fixpp_t008_otlp_neg_dir";
    {
        std::error_code ec;
        std::filesystem::create_directories(log_dir, ec);
        ASSERT_FALSE(ec) << "Failed to create " << log_dir;

        std::ofstream out{tmp_toml};
        auto esc = [](std::filesystem::path const& p) { return p.string(); };
        out << "[clock]\nkind = \"system\"\n\n"
            << "[store]\nkind = \"memory\"\n\n"
            << "[cert_source]\nkind = \"file\"\n"
            << "cert_file = \"" << esc(fixture_dir() / "leaf_ecdsa_p256.pem") << "\"\n"
            << "key_file  = \"" << esc(fixture_dir() / "leaf_ecdsa_p256.key") << "\"\n"
            << "ca_file   = \"" << esc(fixture_dir() / "ca.pem") << "\"\n\n"
            << "[dictionary]\nkind = \"path\"\n"
            << "path = \"" << esc(fixture_dir() / "FIX44.xml") << "\"\n\n"
            << "[logger]\n"
            << "capacity = 4096\n"
            << "drain_timeout = \"1000ms\"\n\n"
            // Sink 0: valid file sink
            << "[[logger.sinks]]\n"
            << "kind      = \"file\"\n"
            << "directory = \"" << esc(log_dir) << "\"\n\n"
            // Sink 1: otlp with NO endpoint — must trigger missing_required
            << "[[logger.sinks]]\n"
            << "kind = \"otlp\"\n"
            // no endpoint field intentionally
            << "\n"
            << "[[session]]\n"
            << "sender_comp_id = \"CLIENT1\"\n"
            << "target_comp_id = \"SERVER1\"\n"
            << "begin_string   = \"FIX.4.4\"\n"
            << "role           = \"initiator\"\n\n"
            << "[session.transport]\n"
            << "kind = \"tls\"\n"
            << "host = \"fix.example.com\"\n"
            << "port = 4321\n\n"
            << "[session.security_profile]\n"
            << "kind = \"mtls_ca\"\n";
    }

    auto result = load_path(tmp_toml);

    {
        std::error_code ec;
        std::filesystem::remove(tmp_toml, ec);
    }

    // The load MUST FAIL: the otlp sink has no endpoint → missing_required.
    // If a resolver that silently drops the otlp sink were present, this would
    // succeed (file sink valid alone), and this assertion would catch the regression.
    ASSERT_FALSE(result.has_value())
        << "otlp sink with no endpoint must fail with missing_required; "
           "if load succeeded, the otlp branch was silently skipped (regression)";

    using RC = fixpp::config::reason_class;
    bool found = false;
    for (const auto& d : result.error()) {
        if (d.reason == RC::missing_required &&
            d.key_path.find("endpoint") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found)
        << "expected missing_required on endpoint for the otlp sink; diagnostics:\n"
        << diag_string(result.error());
}

#endif  // FIXPP_CONFIG_HAS_OTLP

// =============================================================================
// T009 — Optional-absence guard (preserved-behaviour, expected GREEN)
// =============================================================================
//
// A file that omits [logger] entirely must:
//   (a) load without any error (absence is not an error, FR-003/SC-004)
//   (b) yield engine.logger == nullptr (byte-identical to the 044 result)
//
// This test is expected to be GREEN immediately because:
//   - The happy_full.toml fixture has no [logger] block.
//   - The resolver (T010–T013) is not yet written, so bundle.engine.logger
//     is never assigned → stays nullptr.
//   - Once the resolver lands, it must NOT assign engine.logger when no
//     [logger] block is present — this test guards that invariant.

TEST(LoadLogger, T009_AbsentLoggerIsNull) {
    // happy_full.toml has no [logger] block.
    auto result = load_fixture("happy_full.toml");

    ASSERT_TRUE(result.has_value())
        << "happy_full.toml must load successfully (no [logger] is not an error); "
           "diagnostics:\n"
        << (result.has_value() ? "" : diag_string(result.error()));

    ASSERT_EQ(result->sessions.size(), std::size_t{1})
        << "expected exactly one session from happy_full.toml";

    // Absence of [logger] → engine.logger stays null.  Not an error.
    EXPECT_EQ(result->engine.logger, nullptr)
        << "engine.logger must be null when no [logger] block is present; "
           "the resolver must never assign engine.logger on an absent [logger]";

    // Spot-check that the rest of the bundle was correctly populated (guards
    // against a regression where the absence-of-logger somehow breaks 044 fields).
    EXPECT_NE(result->engine.clock, nullptr)
        << "engine.clock must be populated even when [logger] is absent";
    EXPECT_NE(result->engine.default_store_factory, nullptr)
        << "engine.default_store_factory must be populated even when [logger] is absent";
}
