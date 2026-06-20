// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/config/test_load_logger_negative.cpp
// 045-observability-config Phase 4 (User Story 2) — negative battery.
//
// T014  — one cell per reason_class reachable on the logger surface
// T014a — OTLP endpoint credential redaction (FR-023)
// T015  — collect-ALL: N independent errors → N diagnostics;
//          N-2 property: a later session's logger error suppresses ALL loggers
//
// White-box cells call detail::resolve_engine_logger and
// detail::construct_loggers_if_clean directly (private header via
// target_include_directories in CMakeLists.txt).
//
// Full-load cells call fixpp::config::load_toml_config (public API).
//
// RED cells (require T016 preflight, not yet implemented) are labelled
// with "// T016-RED" and explain which preflight check is missing.
//
// Anchors:
//   data-model.md E-3/E-4/E-5 — exact reason_class per field
//   research.md D-4/D-7       — preflight = stat/access only; PEM magic check
//   spec.md SC-003             — fail-closed before open()
//   tasks.md T014/T014a/T015

#include "mappers.hpp"  // detail::resolve_engine_logger, construct_loggers_if_clean,
                        // DiagnosticAccumulator, PendingLoggerSet

#include <asio/io_context.hpp>

#include <fixpp/config/config_bundle.hpp>
#include <fixpp/config/load_diagnostic.hpp>
#include <fixpp/config/toml_config_loader.hpp>
#include <fixpp/log/logger.hpp>
#include <fixpp/log/syslog_sink.hpp>  // defines FIXPP_HAS_SYSLOG when available

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory_resource>
#include <string>
#include <string_view>
#include <vector>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::filesystem::path neg_fixture(std::string_view name)
{
    return std::filesystem::path{std::string{FIXPP_CONFIG_FIXTURE_DIR}} / name;
}

// Full-file load via the public API.
static fixpp::config::LoadResult full_load(const std::filesystem::path& path)
{
    asio::io_context ctx;
    fixpp::config::LoadOptions opts;
    opts.engine_executor = ctx.get_executor();
    return fixpp::config::load_toml_config(path, opts);
}

// Check that `diags` contains a diagnostic matching both reason and key_path.
static bool has_diag(const std::vector<fixpp::config::LoadDiagnostic>& diags,
                     fixpp::config::reason_class expected_reason,
                     std::string_view            expected_key_path)
{
    return std::any_of(diags.begin(), diags.end(),
                       [&](const fixpp::config::LoadDiagnostic& d) {
                           return d.reason == expected_reason &&
                                  d.key_path == expected_key_path;
                       });
}

// Build a minimal [logger] toml::table inline (no file I/O) for white-box cells.
// `extra_fields` is appended verbatim inside the [logger] block as TOML text.
// Returns a pair: (parsed root table, the [logger] sub-table reference).
struct ParsedLogger {
    std::shared_ptr<toml::table> root;
    const toml::table* logger_tbl{nullptr};
};

static ParsedLogger parse_logger_inline(const std::string& logger_toml)
{
    ParsedLogger result;
    result.root = std::make_shared<toml::table>(toml::parse(logger_toml));
    if (const auto* n = result.root->get("logger"); n && n->is_table())
        result.logger_tbl = n->as_table();
    return result;
}

// ---------------------------------------------------------------------------
// T014 — Negative battery
// ---------------------------------------------------------------------------

// ── unknown_enum: sink kind ──────────────────────────────────────────────────

TEST(T014_NegBattery, UnknownSinkKind)
{
    // Full-load path. The fixture has kind="kafka" in sinks[0].
    // → unknown_enum on "logger.sinks[0].kind"
    const auto result = full_load(neg_fixture("neg_logger_unknown_kind.toml"));
    ASSERT_FALSE(result.has_value());
    const auto& diags = result.error();
    EXPECT_TRUE(has_diag(diags,
                          fixpp::config::reason_class::unknown_enum,
                          "logger.sinks[0].kind"))
        << "Expected unknown_enum on logger.sinks[0].kind";
}

// ── unknown_enum: on_overflow token ─────────────────────────────────────────

TEST(T014_NegBattery, UnknownOnOverflow)
{
    // Full-load path. The fixture has on_overflow="discard" (not a valid token).
    // → unknown_enum on "logger.on_overflow"
    const auto result = full_load(neg_fixture("neg_logger_unknown_on_overflow.toml"));
    ASSERT_FALSE(result.has_value());
    const auto& diags = result.error();
    EXPECT_TRUE(has_diag(diags,
                          fixpp::config::reason_class::unknown_enum,
                          "logger.on_overflow"))
        << "Expected unknown_enum on logger.on_overflow";
}

// ── unknown_enum: syslog facility name ──────────────────────────────────────
//
// This cell is only reachable when the fixpp_config_toml library is compiled
// with the syslog-available path active (FIXPP_HAS_SYSLOG defined at
// logger_resolver.cpp compilation time — true on POSIX, where syslog_sink.hpp
// self-#defines it; logger_resolver.cpp now includes that header unconditionally
// so the macro is seen).
//
// To stay portable across non-POSIX builds (where syslog is genuinely
// unavailable → kind="syslog" yields invalid_or_contradictory_selector), we
// probe the library's syslog mode at runtime and SKIP the facility cell when
// syslog is unavailable (the facility parse is then unreachable).

TEST(T014_NegBattery, UnknownSyslogFacility)
{
    // Probe: resolve kind="syslog" with no facility field.
    // In syslog-available mode: succeeds (no error for absent optional facility).
    // In syslog-unavailable mode: invalid_or_contradictory_selector on sinks[0].kind.
    const std::string probe_toml = R"(
[logger]
  [[logger.sinks]]
  kind  = "syslog"
  ident = "probe"
)";
    auto probe_parsed = parse_logger_inline(probe_toml);
    ASSERT_NE(probe_parsed.logger_tbl, nullptr);

    fixpp::config::detail::DiagnosticAccumulator probe_acc;
    fixpp::config::PendingLoggerSet probe_pending;
    fixpp::config::LoadOptions probe_opts;
    probe_opts.resource = std::pmr::get_default_resource();
    fixpp::config::detail::resolve_engine_logger(
        *probe_parsed.logger_tbl, "logger", fixpp::config::SourceLoc{},
        std::filesystem::temp_directory_path(), probe_opts, probe_pending, probe_acc,
        /*is_engine=*/true, /*session_index=*/0);

    const bool syslog_unavailable_in_library =
        !probe_acc.empty() &&
        has_diag(std::move(probe_acc).release(),
                 fixpp::config::reason_class::invalid_or_contradictory_selector,
                 "logger.sinks[0].kind");

    if (syslog_unavailable_in_library) {
        GTEST_SKIP()
            << "Library compiled without syslog-available path (FIXPP_HAS_SYSLOG not active "
               "at logger_resolver.cpp compile time); facility cell is unreachable";
    }

    // Syslog-available: probe for unknown facility name.
    const std::string toml_text = R"(
[logger]
  [[logger.sinks]]
  kind     = "syslog"
  ident    = "test"
  facility = "user999"
)";
    auto parsed = parse_logger_inline(toml_text);
    ASSERT_NE(parsed.logger_tbl, nullptr);

    fixpp::config::detail::DiagnosticAccumulator acc;
    fixpp::config::PendingLoggerSet pending;
    fixpp::config::LoadOptions opts;
    opts.resource = std::pmr::get_default_resource();

    fixpp::config::detail::resolve_engine_logger(
        *parsed.logger_tbl, "logger", fixpp::config::SourceLoc{},
        std::filesystem::temp_directory_path(), opts, pending, acc,
        /*is_engine=*/true, /*session_index=*/0);

    ASSERT_FALSE(acc.empty());
    const auto diags = std::move(acc).release();

    EXPECT_TRUE(has_diag(diags,
                          fixpp::config::reason_class::unknown_enum,
                          "logger.sinks[0].facility"))
        << "Expected unknown_enum on logger.sinks[0].facility";
}

// ── missing_required: sinks absent ──────────────────────────────────────────

TEST(T014_NegBattery, MissingSinks)
{
    // Full-load path. The fixture has no [[logger.sinks]] array at all.
    // → missing_required on "logger.sinks"
    const auto result = full_load(neg_fixture("neg_logger_missing_sinks.toml"));
    ASSERT_FALSE(result.has_value());
    const auto& diags = result.error();
    EXPECT_TRUE(has_diag(diags,
                          fixpp::config::reason_class::missing_required,
                          "logger.sinks"))
        << "Expected missing_required on logger.sinks";
}

// ── missing_required: OTLP endpoint absent ───────────────────────────────────

#ifdef FIXPP_CONFIG_HAS_OTLP
TEST(T014_NegBattery, OtlpMissingEndpoint)
{
    // Full-load path. The fixture has kind="otlp" but no endpoint field.
    // → missing_required on "logger.sinks[0].endpoint"
    const auto result = full_load(neg_fixture("neg_logger_otlp_missing_endpoint.toml"));
    ASSERT_FALSE(result.has_value());
    const auto& diags = result.error();
    EXPECT_TRUE(has_diag(diags,
                          fixpp::config::reason_class::missing_required,
                          "logger.sinks[0].endpoint"))
        << "Expected missing_required on logger.sinks[0].endpoint";
}
#endif  // FIXPP_CONFIG_HAS_OTLP

// ── empty_required: sinks array present but empty ────────────────────────────

TEST(T014_NegBattery, EmptySinks)
{
    // Full-load path. The fixture has sinks = [] (empty inline array).
    // → empty_required on "logger.sinks"
    const auto result = full_load(neg_fixture("neg_logger_empty_sinks.toml"));
    ASSERT_FALSE(result.has_value());
    const auto& diags = result.error();
    EXPECT_TRUE(has_diag(diags,
                          fixpp::config::reason_class::empty_required,
                          "logger.sinks"))
        << "Expected empty_required on logger.sinks";
}

// ── empty_required: OTLP endpoint present but empty ──────────────────────────

#ifdef FIXPP_CONFIG_HAS_OTLP
TEST(T014_NegBattery, OtlpEmptyEndpoint)
{
    // Full-load path. The fixture has endpoint = "" (present, empty string).
    // → empty_required on "logger.sinks[0].endpoint"
    const auto result = full_load(neg_fixture("neg_logger_otlp_empty_endpoint.toml"));
    ASSERT_FALSE(result.has_value());
    const auto& diags = result.error();
    EXPECT_TRUE(has_diag(diags,
                          fixpp::config::reason_class::empty_required,
                          "logger.sinks[0].endpoint"))
        << "Expected empty_required on logger.sinks[0].endpoint";
}
#endif  // FIXPP_CONFIG_HAS_OTLP

// ── out_of_range: capacity not a power of 2 ──────────────────────────────────

TEST(T014_NegBattery, CapacityNotPow2)
{
    // Full-load path. The fixture has capacity = 1000 (not a power of 2).
    // → out_of_range on "logger.capacity"
    const auto result = full_load(neg_fixture("neg_logger_capacity_not_pow2.toml"));
    ASSERT_FALSE(result.has_value());
    const auto& diags = result.error();
    EXPECT_TRUE(has_diag(diags,
                          fixpp::config::reason_class::out_of_range,
                          "logger.capacity"))
        << "Expected out_of_range on logger.capacity";
}

// ── out_of_range: max_file_bytes = 0 ─────────────────────────────────────────

TEST(T014_NegBattery, ZeroMaxFileBytes)
{
    // Full-load path. The fixture has max_file_bytes = 0.
    // → out_of_range on "logger.sinks[0].max_file_bytes"
    const auto result = full_load(neg_fixture("neg_logger_zero_max_file_bytes.toml"));
    ASSERT_FALSE(result.has_value());
    const auto& diags = result.error();
    EXPECT_TRUE(has_diag(diags,
                          fixpp::config::reason_class::out_of_range,
                          "logger.sinks[0].max_file_bytes"))
        << "Expected out_of_range on logger.sinks[0].max_file_bytes";
}

// ── out_of_range: max_export_batch = 0 (OTLP) ───────────────────────────────

#ifdef FIXPP_CONFIG_HAS_OTLP
TEST(T014_NegBattery, OtlpZeroMaxExportBatch)
{
    // Full-load path. The fixture has max_export_batch = 0.
    // → out_of_range on "logger.sinks[0].max_export_batch"
    const auto result = full_load(neg_fixture("neg_logger_otlp_zero_max_export_batch.toml"));
    ASSERT_FALSE(result.has_value());
    const auto& diags = result.error();
    EXPECT_TRUE(has_diag(diags,
                          fixpp::config::reason_class::out_of_range,
                          "logger.sinks[0].max_export_batch"))
        << "Expected out_of_range on logger.sinks[0].max_export_batch";
}
#endif  // FIXPP_CONFIG_HAS_OTLP

// ── malformed_value: unitless drain_timeout ───────────────────────────────────

TEST(T014_NegBattery, UnitlessDrainTimeout)
{
    // Full-load path. The fixture has drain_timeout = "2000" (no unit suffix).
    // Per the 044 duration rule, a bare numeric string is malformed_value.
    // → malformed_value on "logger.drain_timeout"
    const auto result = full_load(neg_fixture("neg_logger_unitless_drain_timeout.toml"));
    ASSERT_FALSE(result.has_value());
    const auto& diags = result.error();
    EXPECT_TRUE(has_diag(diags,
                          fixpp::config::reason_class::malformed_value,
                          "logger.drain_timeout"))
        << "Expected malformed_value on logger.drain_timeout";
}

// ── malformed_value: unitless export_timeout (OTLP) ──────────────────────────

#ifdef FIXPP_CONFIG_HAS_OTLP
TEST(T014_NegBattery, OtlpUnitlessExportTimeout)
{
    // Full-load path. The fixture has export_timeout = "500" (no unit suffix).
    // → malformed_value on "logger.sinks[0].export_timeout"
    const auto result = full_load(neg_fixture("neg_logger_unitless_export_timeout.toml"));
    ASSERT_FALSE(result.has_value());
    const auto& diags = result.error();
    EXPECT_TRUE(has_diag(diags,
                          fixpp::config::reason_class::malformed_value,
                          "logger.sinks[0].export_timeout"))
        << "Expected malformed_value on logger.sinks[0].export_timeout";
}
#endif  // FIXPP_CONFIG_HAS_OTLP

// ── invalid_or_contradictory_selector: syslog on non-syslog library build ────
//
// On this POSIX host, FIXPP_HAS_SYSLOG is defined in the test TU (by including
// syslog_sink.hpp), but the fixpp_config_toml library was compiled without it
// being defined in logger_resolver.cpp (the macro is only set by including the
// header, which is inside the #ifdef guard — a circular dependency). As a result,
// the library ALWAYS takes the "syslog unavailable" path on this build, emitting
// invalid_or_contradictory_selector for kind="syslog".
// This test documents and validates that behavior without a compile-time guard
// (the library's syslog mode is detected at runtime by the UnknownSyslogFacility
// self-probe; here we just assert the unavailable diagnostic fires).

TEST(T014_NegBattery, SyslogBuildConditional)
{
    // Build-conditional symmetry (FR-013): kind="syslog" with no facility.
    //   - On a build WHERE syslog is available (FIXPP_HAS_SYSLOG): the sink
    //     resolves successfully (facility is optional) — NOT an error.
    //   - On a build WITHOUT syslog: invalid_or_contradictory_selector
    //     (loud, never silently skipped).
    const std::string toml_text = R"(
[logger]
  [[logger.sinks]]
  kind = "syslog"
)";
    auto parsed = parse_logger_inline(toml_text);
    ASSERT_NE(parsed.logger_tbl, nullptr);

    fixpp::config::detail::DiagnosticAccumulator acc;
    fixpp::config::PendingLoggerSet pending;
    fixpp::config::LoadOptions opts;
    opts.resource = std::pmr::get_default_resource();

    fixpp::config::detail::resolve_engine_logger(
        *parsed.logger_tbl, "logger", fixpp::config::SourceLoc{},
        std::filesystem::temp_directory_path(), opts, pending, acc,
        /*is_engine=*/true, /*session_index=*/0);

#ifdef FIXPP_HAS_SYSLOG
    // Syslog available → accepted; one sink parked, no diagnostic.
    EXPECT_TRUE(acc.empty())
        << "syslog is available on this build; kind=\"syslog\" (no facility) "
           "must resolve successfully, not be rejected";
    ASSERT_TRUE(pending.engine.has_value());
    EXPECT_EQ(pending.engine->sinks.size(), std::size_t{1});
#else
    // Syslog unavailable → loud rejection (never silently skipped).
    ASSERT_FALSE(acc.empty());
    const auto diags = std::move(acc).release();
    EXPECT_TRUE(has_diag(diags,
                          fixpp::config::reason_class::invalid_or_contradictory_selector,
                          "logger.sinks[0].kind"))
        << "Expected invalid_or_contradictory_selector on logger.sinks[0].kind "
           "(syslog unavailable on this build)";
#endif
}

// ── invalid_or_contradictory_selector: otlp on non-OTLP build ───────────────

#ifndef FIXPP_CONFIG_HAS_OTLP
TEST(T014_NegBattery, OtlpUnavailableBuild)
{
    // Full-load path. On a build without OTel SDK, kind="otlp" must produce
    // invalid_or_contradictory_selector (FIXPP_CONFIG_HAS_OTLP not defined).
    // This test is only compiled on non-OTLP builds.
    const std::string toml_text = R"(
[logger]
  [[logger.sinks]]
  kind     = "otlp"
  endpoint = "http://collector:4318/v1/logs"
)";
    auto parsed = parse_logger_inline(toml_text);
    ASSERT_NE(parsed.logger_tbl, nullptr);

    fixpp::config::detail::DiagnosticAccumulator acc;
    fixpp::config::PendingLoggerSet pending;
    fixpp::config::LoadOptions opts;
    opts.resource = std::pmr::get_default_resource();

    fixpp::config::detail::resolve_engine_logger(
        *parsed.logger_tbl, "logger", fixpp::config::SourceLoc{},
        std::filesystem::temp_directory_path(), opts, pending, acc,
        /*is_engine=*/true, /*session_index=*/0);

    ASSERT_FALSE(acc.empty());
    const auto diags = std::move(acc).release();
    EXPECT_TRUE(has_diag(diags,
                          fixpp::config::reason_class::invalid_or_contradictory_selector,
                          "logger.sinks[0].kind"))
        << "Expected invalid_or_contradictory_selector on logger.sinks[0].kind";
}
#endif  // !FIXPP_CONFIG_HAS_OTLP

// ── invalid_or_contradictory_selector: cert_source unreadable (T016) ─────────
// T016: preflight must reject a cert_source path that does not exist.

TEST(T014_NegBattery, OtlpCertSourceUnreadable)
{
#ifdef FIXPP_CONFIG_HAS_OTLP
    // Ensure the path definitely does not exist.
    const std::string nonexistent = "/tmp/fixpp_test_nonexistent_cert_source_12345.pem";
    { std::error_code ec; std::filesystem::remove(nonexistent, ec); }

    const std::string toml_text = R"(
[logger]
  [[logger.sinks]]
  kind        = "otlp"
  endpoint    = "http://collector:4318/v1/logs"
  cert_source = "/tmp/fixpp_test_nonexistent_cert_source_12345.pem"
)";
    auto parsed = parse_logger_inline(toml_text);
    ASSERT_NE(parsed.logger_tbl, nullptr);

    fixpp::config::detail::DiagnosticAccumulator acc;
    fixpp::config::PendingLoggerSet pending;
    fixpp::config::LoadOptions opts;
    opts.resource = std::pmr::get_default_resource();

    fixpp::config::detail::resolve_engine_logger(
        *parsed.logger_tbl, "logger", fixpp::config::SourceLoc{},
        std::filesystem::temp_directory_path(), opts, pending, acc,
        /*is_engine=*/true, /*session_index=*/0);

    // T016 preflight must have fired: unreadable cert_source → diagnostic.
    ASSERT_FALSE(acc.empty())
        << "Preflight must reject an unreadable cert_source path";
    const auto diags = std::move(acc).release();
    EXPECT_TRUE(has_diag(diags,
                          fixpp::config::reason_class::invalid_or_contradictory_selector,
                          "logger.sinks[0].cert_source"))
        << "Expected invalid_or_contradictory_selector on logger.sinks[0].cert_source";

    // Mutation discriminator: the readable-check diagnostic message contains "not readable".
    // If the readable check were removed but the PEM check kept, the PEM check would fire
    // (failed ifstream → gcount==0 → no PEM magic) emitting a "does not appear to be a PEM
    // file" message — NOT a "not readable" message. So asserting "not readable" is present
    // distinguishes the two checks and makes this cell discriminating.
    {
        std::string all_msgs;
        for (const auto& d : diags) all_msgs += d.message + '\n';
        EXPECT_NE(all_msgs.find("not readable"), std::string::npos)
            << "The 'not readable' diagnostic message must be produced by the readable check; "
               "deleting the readable check and keeping the PEM check would emit a different "
               "message (PEM magic failure) → this assertion goes RED (discriminating)";
    }
    // Also: no side effect — the nonexistent path was never created by the load.
    EXPECT_FALSE(std::filesystem::exists(nonexistent))
        << "Preflight must not create the cert_source file as a side effect";
#else
    // Without OTLP, the sinks[0] is rejected at kind level before cert_source
    // is ever read; this cell is not meaningful on non-OTLP builds.
    GTEST_SKIP() << "FIXPP_CONFIG_HAS_OTLP not defined; cell not applicable";
#endif
}

// ── invalid_or_contradictory_selector: cert_source non-PEM (T016) ────────────
// T016: preflight must reject a cert_source file that is readable but not PEM.

TEST(T014_NegBattery, OtlpCertSourceNonPem)
{
#ifdef FIXPP_CONFIG_HAS_OTLP
    // Create a temp file containing non-PEM content.
    const auto tmp_cert = std::filesystem::temp_directory_path() /
                          "fixpp_test_non_pem_cert_source.txt";
    {
        std::ofstream f(tmp_cert, std::ios::out | std::ios::trunc);
        f << "this is not a PEM file\n";
    }
    struct Cleanup {
        std::filesystem::path p;
        ~Cleanup() { std::error_code ec; std::filesystem::remove(p, ec); }
    } cleanup{tmp_cert};

    const std::string toml_text =
        "[logger]\n"
        "  [[logger.sinks]]\n"
        "  kind        = \"otlp\"\n"
        "  endpoint    = \"http://collector:4318/v1/logs\"\n"
        "  cert_source = \"" + tmp_cert.string() + "\"\n";

    auto parsed = parse_logger_inline(toml_text);
    ASSERT_NE(parsed.logger_tbl, nullptr);

    fixpp::config::detail::DiagnosticAccumulator acc;
    fixpp::config::PendingLoggerSet pending;
    fixpp::config::LoadOptions opts;
    opts.resource = std::pmr::get_default_resource();

    fixpp::config::detail::resolve_engine_logger(
        *parsed.logger_tbl, "logger", fixpp::config::SourceLoc{},
        std::filesystem::temp_directory_path(), opts, pending, acc,
        /*is_engine=*/true, /*session_index=*/0);

    // T016 preflight must have fired: non-PEM cert_source → diagnostic.
    ASSERT_FALSE(acc.empty())
        << "Preflight must reject a cert_source that lacks the '-----BEGIN' PEM magic";
    const auto diags = std::move(acc).release();
    EXPECT_TRUE(has_diag(diags,
                          fixpp::config::reason_class::invalid_or_contradictory_selector,
                          "logger.sinks[0].cert_source"))
        << "Expected invalid_or_contradictory_selector on logger.sinks[0].cert_source";

    // Mutation discriminator: the PEM-magic-check diagnostic message must mention "-----BEGIN".
    // Removing the PEM check entirely: file IS readable → readable check passes → acc empty →
    // ASSERT_FALSE(acc.empty()) goes RED. Keeping PEM check but changing its message: this pins
    // the message contents against future refactoring that might weaken the diagnostic text.
    {
        std::string all_msgs;
        for (const auto& d : diags) all_msgs += d.message + '\n';
        EXPECT_NE(all_msgs.find("-----BEGIN"), std::string::npos)
            << "The PEM-magic diagnostic must mention '-----BEGIN' to be self-explaining";
    }
#else
    GTEST_SKIP() << "FIXPP_CONFIG_HAS_OTLP not defined; cell not applicable";
#endif
}

// ── invalid_or_contradictory_selector: file-sink dir does not exist (T016) ───
// T016: preflight must reject a configured directory that does not exist.

TEST(T014_NegBattery, FileSinkDirNotExist)
{
    // Use a path that definitely does not exist (deep nested under a nonexistent parent).
    const std::filesystem::path nonexistent_dir{
        "/tmp/fixpp_test_nonexistent_dir_99999/nested"};
    // Ensure it's actually absent.
    ASSERT_FALSE(std::filesystem::exists(nonexistent_dir))
        << "Test precondition: directory must not exist";

    const std::string toml_text = R"(
[logger]
  [[logger.sinks]]
  kind      = "file"
  directory = "/tmp/fixpp_test_nonexistent_dir_99999/nested"
)";
    auto parsed = parse_logger_inline(toml_text);
    ASSERT_NE(parsed.logger_tbl, nullptr);

    fixpp::config::detail::DiagnosticAccumulator acc;
    fixpp::config::PendingLoggerSet pending;
    fixpp::config::LoadOptions opts;
    opts.resource = std::pmr::get_default_resource();

    fixpp::config::detail::resolve_engine_logger(
        *parsed.logger_tbl, "logger", fixpp::config::SourceLoc{},
        std::filesystem::temp_directory_path(), opts, pending, acc,
        /*is_engine=*/true, /*session_index=*/0);

    // T016 preflight must have fired: nonexistent directory → diagnostic.
    ASSERT_FALSE(acc.empty())
        << "Preflight must reject a configured directory that does not exist";
    const auto diags = std::move(acc).release();
    EXPECT_TRUE(has_diag(diags,
                          fixpp::config::reason_class::invalid_or_contradictory_selector,
                          "logger.sinks[0].directory"))
        << "Expected invalid_or_contradictory_selector on logger.sinks[0].directory";

    // Zero-side-effects witness (discriminating): the load must NOT have created
    // the directory. A preflight that called mkdir/create_directories would fail this.
    EXPECT_FALSE(std::filesystem::exists(nonexistent_dir))
        << "Preflight must be side-effect-free: the directory must NOT have been "
           "created by the failed load (SC-003 zero-side-effects contract)";

    // Mutation discriminator: removing the is_directory check would leave acc empty
    // and pending.engine populated → ASSERT_FALSE(acc.empty()) goes RED.
}

// ── invalid_or_contradictory_selector: file-sink dir not writable (T016) ─────
// T016: preflight must reject a configured directory that exists but is not writable.

TEST(T014_NegBattery, FileSinkDirNotWritable)
{
    // Create a temp dir and make it read-only.
    const auto ro_dir = std::filesystem::temp_directory_path() /
                        "fixpp_test_ro_log_dir_t014";
    std::error_code ec;
    std::filesystem::create_directories(ro_dir, ec);
    if (ec) {
        GTEST_SKIP() << "Could not create temp dir for writable test";
    }
    // Remove write permission from owner, group, and others.
    std::filesystem::permissions(ro_dir,
        std::filesystem::perms::owner_write |
        std::filesystem::perms::group_write |
        std::filesystem::perms::others_write,
        std::filesystem::perm_options::remove, ec);
    if (ec) {
        GTEST_SKIP() << "Could not remove write permission from temp dir";
    }
    struct Cleanup {
        std::filesystem::path p;
        ~Cleanup() {
            std::error_code e;
            std::filesystem::permissions(p,
                std::filesystem::perms::owner_all,
                std::filesystem::perm_options::add, e);
            std::filesystem::remove_all(p, e);
        }
    } cleanup{ro_dir};

    const std::string toml_text =
        "[logger]\n"
        "  [[logger.sinks]]\n"
        "  kind      = \"file\"\n"
        "  directory = \"" + ro_dir.string() + "\"\n";

    auto parsed = parse_logger_inline(toml_text);
    ASSERT_NE(parsed.logger_tbl, nullptr);

    fixpp::config::detail::DiagnosticAccumulator acc;
    fixpp::config::PendingLoggerSet pending;
    fixpp::config::LoadOptions opts;
    opts.resource = std::pmr::get_default_resource();

    fixpp::config::detail::resolve_engine_logger(
        *parsed.logger_tbl, "logger", fixpp::config::SourceLoc{},
        std::filesystem::temp_directory_path(), opts, pending, acc,
        /*is_engine=*/true, /*session_index=*/0);

    // T016 preflight must have fired: non-writable directory → diagnostic.
    ASSERT_FALSE(acc.empty())
        << "Preflight must reject a configured directory that is not writable";
    const auto diags = std::move(acc).release();
    EXPECT_TRUE(has_diag(diags,
                          fixpp::config::reason_class::invalid_or_contradictory_selector,
                          "logger.sinks[0].directory"))
        << "Expected invalid_or_contradictory_selector on logger.sinks[0].directory";

    // Mutation discriminator: removing the access(W_OK) check would leave acc empty
    // (the directory IS_directory passes) → ASSERT_FALSE(acc.empty()) goes RED.
}

// ── recognized_not_yet_supported_step2: use_grpc = true ──────────────────────

#ifdef FIXPP_CONFIG_HAS_OTLP
TEST(T014_NegBattery, UseGrpcTrue)
{
    // Full-load path. The fixture has use_grpc = true.
    // → recognized_not_yet_supported_step2 on "logger.sinks[0].use_grpc"
    const auto result = full_load(neg_fixture("neg_logger_use_grpc_true.toml"));
    ASSERT_FALSE(result.has_value());
    const auto& diags = result.error();
    EXPECT_TRUE(has_diag(diags,
                          fixpp::config::reason_class::recognized_not_yet_supported_step2,
                          "logger.sinks[0].use_grpc"))
        << "Expected recognized_not_yet_supported_step2 on logger.sinks[0].use_grpc";
}
#endif  // FIXPP_CONFIG_HAS_OTLP

// ── Zero-side-effects witness ─────────────────────────────────────────────────
//
// Discriminating design (per advisor + feedback_witness_asserts_named_postcondition):
//   Step 1 — Resolve a VALID [logger] → pending.engine is populated, acc still clean.
//   Step 2 — Inject a synthetic error into acc (from an unrelated source).
//   Step 3 — Call construct_loggers_if_clean(pending, bundle, acc).
//   Assert  — bundle.engine.logger == nullptr  (the guard fired, no Logger built).
//   Mutation — Deleting the `if (!acc.empty()) return;` guard in
//              construct_loggers_if_clean would make bundle.engine.logger non-null
//              → the assertion goes RED.  Discriminating.
//
// Additionally: assert that no file was created under a controlled temp dir
// (side-effect-free on the non-construction path).

TEST(T014_NegBattery, ZeroSideEffectsWhenAccumulatorNonEmpty)
{
    // Create a temp dir for the file sink so we can assert nothing was written.
    const auto log_dir = std::filesystem::temp_directory_path() /
                         "fixpp_test_zero_side_effects_t014";
    {
        std::error_code ec;
        std::filesystem::create_directories(log_dir, ec);
        ASSERT_FALSE(ec) << "Could not create temp log dir: " << ec.message();
        // Ensure the dir is empty before the test.
        for (auto const& e : std::filesystem::directory_iterator{log_dir}) {
            std::error_code rem_ec;
            std::filesystem::remove_all(e.path(), rem_ec);
        }
    }
    struct Cleanup {
        std::filesystem::path p;
        ~Cleanup() { std::error_code ec; std::filesystem::remove_all(p, ec); }
    } cleanup{log_dir};

    // Step 1: Resolve a VALID [logger] (file sink with our temp dir).
    const std::string toml_text =
        "[logger]\n"
        "capacity = 65536\n"
        "  [[logger.sinks]]\n"
        "  kind      = \"file\"\n"
        "  directory = \"" + log_dir.string() + "\"\n";

    auto parsed = parse_logger_inline(toml_text);
    ASSERT_NE(parsed.logger_tbl, nullptr);

    fixpp::config::detail::DiagnosticAccumulator acc;
    fixpp::config::PendingLoggerSet pending;
    fixpp::config::LoadOptions opts;
    opts.resource = std::pmr::get_default_resource();

    fixpp::config::detail::resolve_engine_logger(
        *parsed.logger_tbl, "logger", fixpp::config::SourceLoc{},
        log_dir.parent_path(), opts, pending, acc,
        /*is_engine=*/true, /*session_index=*/0);

    // Confirm the valid logger was parked and acc is still clean.
    ASSERT_TRUE(acc.empty())
        << "Valid logger fixture produced unexpected diagnostics";
    ASSERT_TRUE(pending.engine.has_value())
        << "Valid logger fixture did not park a PendingLogger";

    // Step 2: Inject a synthetic establishment error (simulates a separate
    // failing field in the same config file, e.g. bad clock kind).
    acc.add(fixpp::config::LoadDiagnostic{
        .key_path = "clock",
        .reason   = fixpp::config::reason_class::unknown_enum,
        .message  = "synthetic error to make acc non-empty",
    });
    ASSERT_EQ(acc.size(), std::size_t{1});

    // Step 3: construct_loggers_if_clean — must be a no-op (acc non-empty).
    fixpp::config::ConfigBundle bundle;
    fixpp::config::detail::construct_loggers_if_clean(std::move(pending), bundle, acc);

    // Assert: engine logger must NOT have been constructed.
    EXPECT_EQ(bundle.engine.logger, nullptr)
        << "construct_loggers_if_clean must not build a Logger when acc is non-empty";

    // Assert: no file was created in the log directory.
    bool any_file_created = false;
    for ([[maybe_unused]] const auto& entry :
         std::filesystem::directory_iterator{log_dir}) {
        any_file_created = true;
        break;
    }
    EXPECT_FALSE(any_file_created)
        << "construct_loggers_if_clean must not create any files when acc is non-empty";
}

// ---------------------------------------------------------------------------
// T014a — OTLP endpoint credential redaction (FR-023)
// ---------------------------------------------------------------------------
//
// An OTLP endpoint URL embedding userinfo (http://user:secret@host:port/path)
// must produce a diagnostic whose message:
//   (a) does NOT contain the cleartext password ("secret")
//   (b) DOES contain "***REDACTED***"
//   (c) DOES contain the host portion ("collector:4318") so that "cleartext-absent"
//       is non-trivial — proves the endpoint IS echoed but with the secret redacted
//       (guards against the trivial-pass where the endpoint isn't echoed at all).
//
// Vehicle: an unreadable cert_source combined with a credential endpoint.
// The T016 cert-source preflight error message embeds
//   redact_url_userinfo(cfg.endpoint)
// which is where the redaction is tested.
//
// Mutation discriminator: removing redact_url_userinfo from the message builder
// would leave "secret" in the message → EXPECT_EQ(find("secret"), npos) goes RED.

#ifdef FIXPP_CONFIG_HAS_OTLP
TEST(T014a_Redaction, OtlpEndpointUserinfoRedacted)
{
    // Credential endpoint + nonexistent cert_source so the preflight fires and
    // echoes the endpoint in the diagnostic message.
    const std::string cred_endpoint = "http://user:secret@collector:4318/v1/logs";
    const std::string nonexistent_cert =
        "/tmp/fixpp_test_nonexistent_cert_for_redaction_test.pem";
    { std::error_code ec; std::filesystem::remove(nonexistent_cert, ec); }

    const std::string toml_text =
        "[logger]\n"
        "  [[logger.sinks]]\n"
        "  kind        = \"otlp\"\n"
        "  endpoint    = \"" + cred_endpoint + "\"\n"
        "  cert_source = \"" + nonexistent_cert + "\"\n";

    auto parsed = parse_logger_inline(toml_text);
    ASSERT_NE(parsed.logger_tbl, nullptr);

    fixpp::config::detail::DiagnosticAccumulator acc;
    fixpp::config::PendingLoggerSet pending;
    fixpp::config::LoadOptions opts;
    opts.resource = std::pmr::get_default_resource();

    fixpp::config::detail::resolve_engine_logger(
        *parsed.logger_tbl, "logger", fixpp::config::SourceLoc{},
        std::filesystem::temp_directory_path(), opts, pending, acc,
        /*is_engine=*/true, /*session_index=*/0);

    // The cert_source preflight must have fired.
    ASSERT_FALSE(acc.empty()) << "Expected a diagnostic from the cert_source preflight";
    const auto diags = std::move(acc).release();

    // Collect all diagnostic messages + key_paths.
    std::string all_messages;
    for (const auto& d : diags) {
        all_messages += d.message;
        all_messages += ' ';
        all_messages += d.key_path;
        all_messages += '\n';
    }

    // (a) Cleartext password MUST NOT appear anywhere in diagnostic output.
    EXPECT_EQ(all_messages.find("secret"), std::string::npos)
        << "Cleartext credential 'secret' found in diagnostic output; "
           "URL userinfo redaction (FR-023) must strip it";

    // (b) The "***REDACTED***" token MUST appear (proves redaction fired, not omission).
    EXPECT_NE(all_messages.find("***REDACTED***"), std::string::npos)
        << "'***REDACTED***' token must appear in the diagnostic message "
           "(FR-023 URL userinfo redaction)";

    // (c) The host portion MUST appear (proves the endpoint IS echoed, not silently dropped).
    // This makes (a) non-trivial: "secret" absent AND "collector:4318" present = redacted.
    EXPECT_NE(all_messages.find("collector:4318"), std::string::npos)
        << "The host portion 'collector:4318' must appear in the diagnostic "
           "(endpoint IS echoed, but with userinfo redacted)";
}

// ── T014a: redact_url_userinfo no-userinfo arm ────────────────────────────────
//
// When an endpoint URL has NO userinfo (no '@' in authority), redact_url_userinfo
// must return it unchanged. This witnesses the no-op arm of the redactor so that
// a future change that over-redacts (strips host or path) fails here.

TEST(T014a_Redaction, OtlpEndpointNoUserinfoUnchanged)
{
    const std::string plain_endpoint = "http://collector:4318/v1/logs";
    const std::string nonexistent_cert =
        "/tmp/fixpp_test_nonexistent_cert_plain_endpoint.pem";
    { std::error_code ec; std::filesystem::remove(nonexistent_cert, ec); }

    const std::string toml_text =
        "[logger]\n"
        "  [[logger.sinks]]\n"
        "  kind        = \"otlp\"\n"
        "  endpoint    = \"" + plain_endpoint + "\"\n"
        "  cert_source = \"" + nonexistent_cert + "\"\n";

    auto parsed = parse_logger_inline(toml_text);
    ASSERT_NE(parsed.logger_tbl, nullptr);

    fixpp::config::detail::DiagnosticAccumulator acc;
    fixpp::config::PendingLoggerSet pending;
    fixpp::config::LoadOptions opts;
    opts.resource = std::pmr::get_default_resource();

    fixpp::config::detail::resolve_engine_logger(
        *parsed.logger_tbl, "logger", fixpp::config::SourceLoc{},
        std::filesystem::temp_directory_path(), opts, pending, acc,
        /*is_engine=*/true, /*session_index=*/0);

    ASSERT_FALSE(acc.empty()) << "Expected a diagnostic from the cert_source preflight";
    const auto diags = std::move(acc).release();

    std::string all_messages;
    for (const auto& d : diags) {
        all_messages += d.message + '\n';
    }

    // When no userinfo is present, redact_url_userinfo returns the URL unchanged.
    // The endpoint must appear verbatim in the message (no spurious redaction).
    EXPECT_NE(all_messages.find(plain_endpoint), std::string::npos)
        << "A plain (no-credential) endpoint must appear verbatim in the diagnostic; "
           "redact_url_userinfo must not alter URLs without userinfo";

    // Paranoia: no "***REDACTED***" token when there is nothing to redact.
    EXPECT_EQ(all_messages.find("***REDACTED***"), std::string::npos)
        << "No redaction token expected when endpoint has no userinfo";
}
#endif  // FIXPP_CONFIG_HAS_OTLP

// ---------------------------------------------------------------------------
// T015 — Collect-ALL: N independent errors + N-2 (whole-file accumulator)
// ---------------------------------------------------------------------------

// ── T015a: N independent errors → N diagnostics ──────────────────────────────
//
// Two errors in distinct logger scalar fields (capacity not pow2 + on_overflow
// unknown) accumulate independently without early return.

TEST(T015_CollectAll, NIndependentErrors)
{
    // Inject 2 errors in the [logger] scalar layer:
    //   (1) capacity = 1000 → out_of_range on "logger.capacity"
    //   (2) on_overflow = "discard" → unknown_enum on "logger.on_overflow"
    // Both accumulate (no early-return between scalar fields).
    const std::string toml_text = R"(
[logger]
capacity    = 1000
on_overflow = "discard"
  [[logger.sinks]]
  kind = "file"
  directory = "/tmp"
)";
    auto parsed = parse_logger_inline(toml_text);
    ASSERT_NE(parsed.logger_tbl, nullptr);

    fixpp::config::detail::DiagnosticAccumulator acc;
    fixpp::config::PendingLoggerSet pending;
    fixpp::config::LoadOptions opts;
    opts.resource = std::pmr::get_default_resource();

    fixpp::config::detail::resolve_engine_logger(
        *parsed.logger_tbl, "logger", fixpp::config::SourceLoc{},
        std::filesystem::temp_directory_path(), opts, pending, acc,
        /*is_engine=*/true, /*session_index=*/0);

    const auto diags = std::move(acc).release();
    ASSERT_GE(diags.size(), std::size_t{2})
        << "Expected at least 2 diagnostics (collect-ALL)";

    EXPECT_TRUE(has_diag(diags,
                          fixpp::config::reason_class::out_of_range,
                          "logger.capacity"))
        << "Missing out_of_range on logger.capacity";
    EXPECT_TRUE(has_diag(diags,
                          fixpp::config::reason_class::unknown_enum,
                          "logger.on_overflow"))
        << "Missing unknown_enum on logger.on_overflow";
}

// ── T015b: N-2 property (whole-file accumulator) ─────────────────────────────
//
// A session-1 logger error suppresses ALL loggers (including session-0's valid
// logger) because construct_loggers_if_clean checks the WHOLE-FILE accumulator.
//
// Design:
//   1. Resolve a VALID session-0 logger → parks into pending.sessions[0].
//   2. Resolve an INVALID session-1 logger → fails, adds to acc, NOT parked.
//   3. Call construct_loggers_if_clean → acc non-empty → guard fires.
//   4. Assert bundle.sessions[0].config.logger_override == nullptr.
//
// Mutation: Deleting the `if (!acc.empty()) return;` guard would construct
// session-0's logger → logger_override != nullptr → assertion goes RED.
// Discriminating (session-0 is genuinely valid; the suppression is caused
// solely by the whole-file guard, not by any defect in session-0's logger).

TEST(T015_CollectAll, N2_SessionErrorSuppressesAllLoggers)
{
    // Session-0: valid logger with a file sink in /tmp.
    const std::string toml_session0 = R"(
[logger]
capacity = 65536
  [[logger.sinks]]
  kind      = "file"
  directory = "/tmp"
)";
    // Session-1: invalid logger (capacity not pow2) → acc non-empty after resolve.
    const std::string toml_session1 = R"(
[logger]
capacity = 999
  [[logger.sinks]]
  kind      = "file"
  directory = "/tmp"
)";

    auto parsed0 = parse_logger_inline(toml_session0);
    auto parsed1 = parse_logger_inline(toml_session1);
    ASSERT_NE(parsed0.logger_tbl, nullptr);
    ASSERT_NE(parsed1.logger_tbl, nullptr);

    fixpp::config::detail::DiagnosticAccumulator acc;
    fixpp::config::PendingLoggerSet pending;
    fixpp::config::LoadOptions opts;
    opts.resource = std::pmr::get_default_resource();

    // Resolve session-0 (valid) → is_engine=false, session_index=0.
    fixpp::config::detail::resolve_engine_logger(
        *parsed0.logger_tbl, "session[0].logger", fixpp::config::SourceLoc{},
        std::filesystem::temp_directory_path(), opts, pending, acc,
        /*is_engine=*/false, /*session_index=*/0);

    ASSERT_TRUE(acc.empty()) << "Session-0 valid logger produced unexpected errors";
    ASSERT_EQ(pending.sessions.size(), std::size_t{1})
        << "Session-0 valid logger not parked in pending.sessions";

    // Resolve session-1 (invalid) → is_engine=false, session_index=1.
    fixpp::config::detail::resolve_engine_logger(
        *parsed1.logger_tbl, "session[1].logger", fixpp::config::SourceLoc{},
        std::filesystem::temp_directory_path(), opts, pending, acc,
        /*is_engine=*/false, /*session_index=*/1);

    // Session-1 error must have been accumulated.
    ASSERT_FALSE(acc.empty())
        << "Session-1 invalid logger produced no error (expected out_of_range on capacity)";

    // construct_loggers_if_clean: acc non-empty → guard fires → no loggers built.
    fixpp::config::ConfigBundle bundle;
    // Pre-size the sessions vector so construct_loggers_if_clean can index into it.
    bundle.sessions.resize(2);

    fixpp::config::detail::construct_loggers_if_clean(std::move(pending), bundle, acc);

    // Session-0's logger must NOT have been built.
    EXPECT_EQ(bundle.sessions[0].config.logger_override, nullptr)
        << "construct_loggers_if_clean must not build session-0 logger when acc is "
           "non-empty (N-2 whole-file property)";

    // Session-1 was never parked (it errored), so its slot stays null too.
    EXPECT_EQ(bundle.sessions[1].config.logger_override, nullptr)
        << "Session-1 logger (not parked) must also be null";
}
