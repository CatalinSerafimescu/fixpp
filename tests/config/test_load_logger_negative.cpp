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
#include <fixpp/log/file_sink.hpp>   // FileSink + current_path() — for E witness
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

// ── T026-verify coverage cells: sink-level reason_class arms ──────────────────
// These four cells close the logger_resolver.cpp §IX.1 coverage gap found by
// /speckit-verify — each exercises a reachable resolver arm the /implement
// battery missed, with a discriminating (exact reason + key_path) witness.

// missing_required: a [[logger.sinks]] entry with no "kind" field.
TEST(T014_NegBattery, SinkMissingKind)
{
    const auto result = full_load(neg_fixture("neg_logger_sink_missing_kind.toml"));
    ASSERT_FALSE(result.has_value());
    EXPECT_TRUE(has_diag(result.error(),
                          fixpp::config::reason_class::missing_required,
                          "logger.sinks[0].kind"))
        << "Expected missing_required on logger.sinks[0].kind";
}

// empty_required: a [[logger.sinks]] entry with kind = "".
TEST(T014_NegBattery, SinkEmptyKind)
{
    const auto result = full_load(neg_fixture("neg_logger_sink_empty_kind.toml"));
    ASSERT_FALSE(result.has_value());
    EXPECT_TRUE(has_diag(result.error(),
                          fixpp::config::reason_class::empty_required,
                          "logger.sinks[0].kind"))
        << "Expected empty_required on logger.sinks[0].kind";
}

// out_of_range: capacity > uint32 max (the v<0||v>max arm, distinct from the
// not-a-power-of-2 else branch that neg_logger_capacity_not_pow2 already covers).
TEST(T014_NegBattery, CapacityOutOfRange)
{
    const auto result = full_load(neg_fixture("neg_logger_capacity_oor.toml"));
    ASSERT_FALSE(result.has_value());
    EXPECT_TRUE(has_diag(result.error(),
                          fixpp::config::reason_class::out_of_range,
                          "logger.capacity"))
        << "Expected out_of_range on logger.capacity (value exceeds uint32)";
}

// malformed_value: a [logger].sinks array element that is not a TOML table.
TEST(T014_NegBattery, SinkElementNotATable)
{
    const auto result = full_load(neg_fixture("neg_logger_sink_not_table.toml"));
    ASSERT_FALSE(result.has_value());
    EXPECT_TRUE(has_diag(result.error(),
                          fixpp::config::reason_class::malformed_value,
                          "logger.sinks[0]"))
        << "Expected malformed_value on logger.sinks[0] (element is not a table)";
}

// out_of_range: capacity = -1 (the v<0 sub-branch, distinct from the >uint32-max
// sub-branch covered by CapacityOutOfRange).
TEST(T014_NegBattery, CapacityNegative)
{
    const auto result = full_load(neg_fixture("neg_logger_capacity_negative.toml"));
    ASSERT_FALSE(result.has_value());
    EXPECT_TRUE(has_diag(result.error(),
                          fixpp::config::reason_class::out_of_range,
                          "logger.capacity"))
        << "Expected out_of_range on logger.capacity (negative value)";
}

// missing_required: a sink "kind" that is present but NOT a string (the
// !is_string() sub-branch, distinct from the absent-kind sub-branch).
// NOTE (Gate B refinement candidate): data-model E-3/E-4 specifies absent ->
// missing_required but is SILENT on present-but-wrong-type. The resolver
// currently reuses missing_required (fail-closed — it rejects); malformed_value
// is a defensible alternative. This cell pins CURRENT behavior; the reason_class
// is flagged for Gate B, not silently blessed.
TEST(T014_NegBattery, SinkNonStringKind)
{
    const auto result = full_load(neg_fixture("neg_logger_sink_nonstring_kind.toml"));
    ASSERT_FALSE(result.has_value());
    EXPECT_TRUE(has_diag(result.error(),
                          fixpp::config::reason_class::missing_required,
                          "logger.sinks[0].kind"))
        << "Expected missing_required on logger.sinks[0].kind (non-string kind)";
}

#ifdef FIXPP_CONFIG_HAS_OTLP
// missing_required: an otlp endpoint present but NOT a string (the !is_string()
// sub-branch). OTLP-build only.
// NOTE (Gate B refinement candidate): same as SinkNonStringKind — E-4 specifies
// absent/empty endpoint -> missing_required/empty_required but is silent on
// wrong-type; current behavior reuses missing_required (fail-closed).
TEST(T014_NegBattery, OtlpNonStringEndpoint)
{
    const auto result = full_load(neg_fixture("neg_logger_otlp_nonstring_endpoint.toml"));
    ASSERT_FALSE(result.has_value());
    EXPECT_TRUE(has_diag(result.error(),
                          fixpp::config::reason_class::missing_required,
                          "logger.sinks[0].endpoint"))
        << "Expected missing_required on logger.sinks[0].endpoint (non-string endpoint)";
}
#endif  // FIXPP_CONFIG_HAS_OTLP

// ── build-conditional symmetry: syslog kind on a syslog/non-syslog build ─────
// On a POSIX build syslog_sink.hpp self-defines FIXPP_HAS_SYSLOG, so the library
// resolves kind="syslog" (the include is now unconditional — the old circular-
// #ifdef bug that disabled syslog on every build is fixed). The cell below
// asserts the available-branch accepts and the unavailable-branch rejects loudly.

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

// ── Gate B r1 A: noexcept boundary — throwing PMR resource must NOT terminate ──
//
// load_toml_config() is noexcept. resolve_engine_logger() uses opts.resource
// for a pmr::vector allocation (logger_resolver.cpp:485-486). If the resolve
// phase runs OUTSIDE the try/catch, a throwing resource → std::terminate.
//
// Design: pass a PMR resource that throws bad_alloc on the FIRST allocation so
// it fires during sinks.reserve() inside resolve_engine_logger. The function
// must return diagnostics (an error LoadResult), NOT terminate.
//
// Mutation discriminator: if the resolve phase is outside the try/catch, the
// throwing resource causes std::terminate and the process exits — the test
// binary crashes (RED). With the fix (resolve + construct both wrapped), the
// throw is caught and a diagnostic is returned (GREEN).

struct ThrowingResource : std::pmr::memory_resource {
    void* do_allocate(std::size_t, std::size_t) override {
        throw std::bad_alloc{};
    }
    void do_deallocate(void*, std::size_t, std::size_t) override {}
    bool do_is_equal(const std::pmr::memory_resource& o) const noexcept override {
        return this == &o;
    }
};

TEST(GateBR1A_NoexceptBoundary, ThrowingResourceReturnsErrorNotTerminates)
{
    // We need a full config file. Write one to a temp file so load_toml_config
    // can parse it. The [logger] sinks.reserve() fires before any other throw.
    const auto tmp = std::filesystem::temp_directory_path() /
                     "fixpp_gateb_r1a_throwing_resource.toml";
    {
        std::ofstream f(tmp);
        // Minimal valid structure + a [logger] with a file sink so the
        // resolve_engine_logger path is reached (sinks.reserve fires).
        f << R"(
[clock]
kind = "system"
[store]
kind = "memory"
[dictionary]
kind = "path"
path = "/tmp/nonexistent_for_test.xml"
[logger]
capacity = 65536
  [[logger.sinks]]
  kind      = "file"
  directory = "/tmp"
[[session]]
sender_comp_id = "CLIENT1"
target_comp_id = "SERVER1"
begin_string   = "FIX.4.4"
role           = "initiator"
[session.transport]
kind = "plain"
host = "fix.example.com"
port = 4321
[session.security_profile]
kind = "insecure_plain_tcp"
)";
    }
    struct Cleanup {
        std::filesystem::path p;
        ~Cleanup() { std::error_code ec; std::filesystem::remove(p, ec); }
    } cl{tmp};

    ThrowingResource throwing_mr;
    asio::io_context ctx;
    fixpp::config::LoadOptions opts;
    opts.engine_executor = ctx.get_executor();
    opts.resource = &throwing_mr;

    // Must NOT terminate — must return an error result.
    const auto result = fixpp::config::load_toml_config(tmp, opts);

    // With the fix: the throw is caught inside the noexcept boundary and
    // returns either a diagnostic or (if the TOML parse itself failed first
    // due to the dict path) a parse-level error. Either way: not a value.
    // The discriminating property is "process still running, result has error".
    EXPECT_FALSE(result.has_value())
        << "load_toml_config with a throwing PMR resource must return an error, "
           "not succeed (the throwing resource prevents valid logger resolution)";
    // Process survival IS the assertion — if terminate() fired, the binary
    // would have exited before reaching this line.
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

// ---------------------------------------------------------------------------
// Gate B r1 — Root cause B: present-but-non-table logger + wrong-type optionals
// ---------------------------------------------------------------------------
//
// #2 (P1): a present non-table `logger` must emit malformed_value, not silently
//          skip.  Three scopes: root [logger], session [[session]] logger,
//          [default] logger (which deep-merges into every session).
//
// Mutation discriminator for each: drop the `else` branch → the acc stays
// empty → result.has_value() becomes true → EXPECT_FALSE(result.has_value())
// goes RED.  (Alternatively: assert exact key_path — wrong path also RED.)

TEST(GateBR1B_NonTableLogger, RootScopeLoggerNotTable)
{
    // logger = 123 at root must produce malformed_value on "logger".
    const auto result = full_load(neg_fixture("neg_logger_not_table_root.toml"));
    ASSERT_FALSE(result.has_value())
        << "root logger = 123 must produce diagnostics, not succeed";
    EXPECT_TRUE(has_diag(result.error(),
                          fixpp::config::reason_class::malformed_value,
                          "logger"))
        << "Expected malformed_value on \"logger\" for non-table root logger";
}

TEST(GateBR1B_NonTableLogger, SessionScopeLoggerNotTable)
{
    // session.logger = 456 (non-table) must produce malformed_value on "session[0].logger".
    const auto result = full_load(neg_fixture("neg_logger_not_table_session.toml"));
    ASSERT_FALSE(result.has_value())
        << "session logger = 456 must produce diagnostics, not succeed";
    EXPECT_TRUE(has_diag(result.error(),
                          fixpp::config::reason_class::malformed_value,
                          "session[0].logger"))
        << "Expected malformed_value on \"session[0].logger\" for non-table session logger";
}

TEST(GateBR1B_NonTableLogger, DefaultScopeLoggerNotTable)
{
    // [default].logger = 789 deep-merges into each session.
    // The merged session[0].logger = 789 (non-table) must produce malformed_value.
    const auto result = full_load(neg_fixture("neg_logger_not_table_default.toml"));
    ASSERT_FALSE(result.has_value())
        << "[default].logger = 789 must produce diagnostics (merges into session[0])";
    EXPECT_TRUE(has_diag(result.error(),
                          fixpp::config::reason_class::malformed_value,
                          "session[0].logger"))
        << "Expected malformed_value on \"session[0].logger\" "
           "for [default].logger non-table inherited via deep-merge";
}

// ── #6 (P2): wrong-type optional fields must emit malformed_value ─────────────
//
// Each cell: present field with the wrong type → malformed_value at exact key_path.
// Cells use white-box resolve_engine_logger / resolve_log_sink directly.
//
// CRITICAL field: cert_source=123 → wrong-type → silently leaves cfg.cert_source=""
// → OTLP exports plain HTTP instead of TLS (fail-open security downgrade).
// Mutation: drop the else → cert_source=123 is accepted → acc stays empty
// → ASSERT_FALSE(acc.empty()) goes RED. Discriminating.

TEST(GateBR1B_WrongTypeOptionals, AsyncFsyncNotBoolean)
{
    // async_fsync = "false" (string, not boolean) → malformed_value
    // Mutation: drop the else → accepted → acc empty after the right-type check.
    // Legit bool FIRST to confirm last-writer-wins doesn't hide the bad one.
    // (Here they are different fields so no ordering concern.)
    const std::string toml_text = R"(
[logger]
  [[logger.sinks]]
  kind        = "file"
  directory   = "/tmp"
  async_fsync = "false"
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

    ASSERT_FALSE(acc.empty())
        << "async_fsync = \"false\" (string) must produce a malformed_value diagnostic";
    EXPECT_TRUE(has_diag(std::move(acc).release(),
                          fixpp::config::reason_class::malformed_value,
                          "logger.sinks[0].async_fsync"))
        << "Expected malformed_value on logger.sinks[0].async_fsync";
}

TEST(GateBR1B_WrongTypeOptionals, BaseNameNotString)
{
    // base_name = 123 (integer, not string) → malformed_value
    const std::string toml_text = R"(
[logger]
  [[logger.sinks]]
  kind      = "file"
  directory = "/tmp"
  base_name = 123
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

    ASSERT_FALSE(acc.empty())
        << "base_name = 123 (integer) must produce a malformed_value diagnostic";
    EXPECT_TRUE(has_diag(std::move(acc).release(),
                          fixpp::config::reason_class::malformed_value,
                          "logger.sinks[0].base_name"))
        << "Expected malformed_value on logger.sinks[0].base_name";
}

TEST(GateBR1B_WrongTypeOptionals, FileSinkDirectoryNotString)
{
    // Gate B r2 #1: directory = 123 (integer, not string) → malformed_value.
    // Pre-fix: directory=123 hit neither the is_string() branch nor the absent
    // branch, leaving cfg.directory = "." which (being writable) silently passed
    // preflight → the wrong-type explicit selector was accepted as CWD/default
    // (FR-020 fail-closed / FR-018). With the fix: malformed_value emitted, the
    // bad node is NOT preflighted against the default, acc non-empty.
    // Mutation: drop the `else if (n && !n->is_string())` branch → directory=123
    // accepted, no diagnostic → ASSERT_FALSE(acc.empty()) goes RED.
    const std::string toml_text = R"(
[logger]
  [[logger.sinks]]
  kind      = "file"
  directory = 123
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

    ASSERT_FALSE(acc.empty())
        << "directory = 123 (integer) must produce a malformed_value diagnostic "
           "(silent default to \".\" = wrong-type selector accepted as CWD)";
    EXPECT_TRUE(has_diag(std::move(acc).release(),
                          fixpp::config::reason_class::malformed_value,
                          "logger.sinks[0].directory"))
        << "Expected malformed_value on logger.sinks[0].directory";
    EXPECT_FALSE(pending.engine.has_value())
        << "a sink with a wrong-type directory must not park a pending logger";
}

TEST(GateBR1B_WrongTypeOptionals, OnOverflowNotString)
{
    // on_overflow = true (boolean, not string) → malformed_value
    // The string path already handles unknown enum values. The wrong-type path
    // (n && !n->is_string() → i.e., n is present but not a string) is the gap.
    const std::string toml_text = R"(
[logger]
on_overflow = true
  [[logger.sinks]]
  kind      = "file"
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

    ASSERT_FALSE(acc.empty())
        << "on_overflow = true (boolean) must produce a malformed_value diagnostic";
    EXPECT_TRUE(has_diag(std::move(acc).release(),
                          fixpp::config::reason_class::malformed_value,
                          "logger.on_overflow"))
        << "Expected malformed_value on logger.on_overflow";
}

#ifdef FIXPP_HAS_SYSLOG
TEST(GateBR1B_WrongTypeOptionals, SyslogFacilityNotString)
{
    // facility = 3 (integer, not string) → malformed_value
    // The string path handles the facility name mapping. Wrong type is the gap.
    const std::string toml_text = R"(
[logger]
  [[logger.sinks]]
  kind     = "syslog"
  facility = 3
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

    ASSERT_FALSE(acc.empty())
        << "syslog facility = 3 (integer) must produce a malformed_value diagnostic";
    EXPECT_TRUE(has_diag(std::move(acc).release(),
                          fixpp::config::reason_class::malformed_value,
                          "logger.sinks[0].facility"))
        << "Expected malformed_value on logger.sinks[0].facility (syslog build)";
}
#endif  // FIXPP_HAS_SYSLOG

// ---------------------------------------------------------------------------
// Gate B r1 — Root cause D: negative/oversized numerics wrap to huge unsigned
// ---------------------------------------------------------------------------
//
// Fields that cast a raw int64 to uint/size_t without a v<0 guard silently
// accept negative values. The existing `capacity` guard (range check v<0||v>max)
// is the correct pattern; apply it to max_file_bytes, max_keep_count,
// max_export_batch, max_export_retries, drain_cpu_affinity.
//
// Mutation discriminator for each: remove the v<0 guard → -1 cast to
// UINT64_MAX/SIZE_MAX is assigned to cfg → acc stays empty → ASSERT_FALSE
// goes RED.

TEST(GateBR1D_NumericOutOfRange, NegativeMaxFileBytes)
{
    // max_file_bytes = -1 → without guard: cast to UINT64_MAX (18446744073709551615).
    const std::string toml_text = R"(
[logger]
  [[logger.sinks]]
  kind           = "file"
  directory      = "/tmp"
  max_file_bytes = -1
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

    ASSERT_FALSE(acc.empty())
        << "max_file_bytes = -1 must produce out_of_range diagnostic (wraps to UINT64_MAX)";
    EXPECT_TRUE(has_diag(std::move(acc).release(),
                          fixpp::config::reason_class::out_of_range,
                          "logger.sinks[0].max_file_bytes"))
        << "Expected out_of_range on logger.sinks[0].max_file_bytes";
}

TEST(GateBR1D_NumericOutOfRange, NegativeMaxKeepCount)
{
    // max_keep_count = -1 → without guard: cast to UINT32_MAX.
    const std::string toml_text = R"(
[logger]
  [[logger.sinks]]
  kind           = "file"
  directory      = "/tmp"
  max_keep_count = -1
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

    ASSERT_FALSE(acc.empty())
        << "max_keep_count = -1 must produce out_of_range diagnostic (wraps to UINT32_MAX)";
    EXPECT_TRUE(has_diag(std::move(acc).release(),
                          fixpp::config::reason_class::out_of_range,
                          "logger.sinks[0].max_keep_count"))
        << "Expected out_of_range on logger.sinks[0].max_keep_count";
}

#ifdef FIXPP_CONFIG_HAS_OTLP
TEST(GateBR1D_NumericOutOfRange, NegativeMaxExportBatch)
{
    // max_export_batch = -1 → without guard: cast to SIZE_MAX.
    const std::string toml_text = R"(
[logger]
  [[logger.sinks]]
  kind             = "otlp"
  endpoint         = "http://collector:4318/v1/logs"
  max_export_batch = -1
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

    ASSERT_FALSE(acc.empty())
        << "max_export_batch = -1 must produce out_of_range (wraps to SIZE_MAX)";
    EXPECT_TRUE(has_diag(std::move(acc).release(),
                          fixpp::config::reason_class::out_of_range,
                          "logger.sinks[0].max_export_batch"))
        << "Expected out_of_range on logger.sinks[0].max_export_batch";
}

TEST(GateBR1D_NumericOutOfRange, NegativeMaxExportRetries)
{
    // max_export_retries = -1 → without guard: cast to SIZE_MAX.
    const std::string toml_text = R"(
[logger]
  [[logger.sinks]]
  kind               = "otlp"
  endpoint           = "http://collector:4318/v1/logs"
  max_export_retries = -1
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

    ASSERT_FALSE(acc.empty())
        << "max_export_retries = -1 must produce out_of_range (wraps to SIZE_MAX)";
    EXPECT_TRUE(has_diag(std::move(acc).release(),
                          fixpp::config::reason_class::out_of_range,
                          "logger.sinks[0].max_export_retries"))
        << "Expected out_of_range on logger.sinks[0].max_export_retries";
}
#endif  // FIXPP_CONFIG_HAS_OTLP

TEST(GateBR1D_NumericOutOfRange, DrainCpuAffinityOverflow)
{
    // drain_cpu_affinity = 2147483648 (INT_MAX+1, overflows int)
    // Without guard: static_cast<int>(2147483648LL) = UB/implementation-defined.
    const std::string toml_text = R"(
[logger]
drain_cpu_affinity = 2147483648
  [[logger.sinks]]
  kind      = "file"
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

    ASSERT_FALSE(acc.empty())
        << "drain_cpu_affinity = 2147483648 (INT_MAX+1) must produce out_of_range";
    EXPECT_TRUE(has_diag(std::move(acc).release(),
                          fixpp::config::reason_class::out_of_range,
                          "logger.drain_cpu_affinity"))
        << "Expected out_of_range on logger.drain_cpu_affinity";
}

// ---------------------------------------------------------------------------
// Gate B r1 — Root cause E: default file-sink directory not base_dir-resolved
// ---------------------------------------------------------------------------
//
// data-model E-4: directory defaults to "." and relative paths — including the
// default — must resolve against the config-file directory (FR-018).
// Pre-fix: only explicitly-set directory was resolved against base_dir; the
// default "." stayed CWD-relative.
//
// Discriminating behavioral witness:
//   After resolve_engine_logger, the pending FileSink owns the resolved path.
//   Call FileSink::open() and check current_path().parent_path() == base_dir.
//   Pre-fix: cfg.directory="." → file lands in CWD → parent_path()!=base_dir RED.
//   Post-fix: cfg.directory=base_dir → file lands in base_dir → GREEN.
//
// Mutation discriminator: remove the `else if (!sink_tbl.get("directory"))` branch
// in logger_resolver.cpp (line ~151-158) → cfg.directory stays "." → open() writes
// to CWD → current_path().parent_path() != base_dir → assertion RED.

TEST(GateBR1E_DefaultDirBaseDir, DefaultDirectoryResolvesAgainstBaseDir)
{
    // Create two distinct dirs: base_dir (to be used as the config-file dir)
    // and CWD-sentinel (ensure CWD != base_dir so we can discriminate).
    // We must MAKE CWD something that is NOT base_dir for this test.
    const auto base_dir = std::filesystem::temp_directory_path() /
                          "fixpp_gateb_r1e_basedir";
    {
        std::error_code ec;
        std::filesystem::create_directories(base_dir, ec);
        ASSERT_FALSE(ec) << "Could not create base_dir: " << ec.message();
    }
    struct Cleanup {
        std::filesystem::path p;
        ~Cleanup() { std::error_code e; std::filesystem::remove_all(p, e); }
    } cl{base_dir};

    // Precondition: base_dir must differ from the process CWD for discrimination.
    // The test is vacuous otherwise (we'd assert parent == CWD which equals base_dir).
    if (std::filesystem::current_path() == base_dir) {
        GTEST_SKIP() << "Test precondition: CWD must not equal base_dir";
    }

    // Logger with a file sink and NO explicit directory.
    // Default FileSinkConfig::directory = "." which must resolve to base_dir.
    const std::string toml_text = R"(
[logger]
capacity = 65536
  [[logger.sinks]]
  kind = "file"
)";
    auto parsed = parse_logger_inline(toml_text);
    ASSERT_NE(parsed.logger_tbl, nullptr);

    fixpp::config::detail::DiagnosticAccumulator acc;
    fixpp::config::PendingLoggerSet pending;
    fixpp::config::LoadOptions opts;
    opts.resource = std::pmr::get_default_resource();

    fixpp::config::detail::resolve_engine_logger(
        *parsed.logger_tbl, "logger", fixpp::config::SourceLoc{},
        base_dir,
        opts, pending, acc,
        /*is_engine=*/true, /*session_index=*/0);

    ASSERT_TRUE(acc.empty())
        << "Unexpected diagnostics: resolve with default directory + valid base_dir";
    ASSERT_TRUE(pending.engine.has_value()) << "PendingLogger must be parked";
    ASSERT_EQ(pending.engine->sinks.size(), std::size_t{1});

    // The behavioral discriminator: open the FileSink and check the live-file path.
    // FileSink::open() writes to cfg.directory (the resolved path).
    // current_path().parent_path() reveals whether the directory is base_dir or CWD.
    fixpp::log::Sink* raw_sink = pending.engine->sinks[0].get();
    ASSERT_NE(raw_sink, nullptr);
    auto* fs = dynamic_cast<fixpp::log::FileSink*>(raw_sink);
    ASSERT_NE(fs, nullptr) << "Sink must be a FileSink (kind=\"file\")";

    // open() creates the live log file in cfg.directory.
    const auto open_result = fs->open();
    ASSERT_TRUE(open_result.has_value())
        << "FileSink::open() must succeed (base_dir exists and is writable)";
    struct CloseGuard {
        fixpp::log::FileSink* s;
        ~CloseGuard() noexcept { s->close(); }
    } cg{fs};

    // The live file must reside under base_dir, not under CWD.
    // Mutation discriminator: without the default-dir resolution, cfg.directory="."
    // → current_path() for the live file is CWD/fixpp-*.log → parent != base_dir.
    const std::filesystem::path live = fs->current_path();
    EXPECT_TRUE(live.is_absolute()) << "Live log path must be absolute after open()";
    std::error_code canon_ec;
    const auto canonical_live_parent = std::filesystem::canonical(live.parent_path(), canon_ec);
    ASSERT_FALSE(canon_ec) << "canonical(live.parent_path()) failed: " << canon_ec.message();
    EXPECT_EQ(canonical_live_parent, std::filesystem::canonical(base_dir))
        << "Log file must land under base_dir (the config-file directory), not CWD.\n"
           "Pre-fix cfg.directory=\".\" (CWD-relative); post-fix cfg.directory=base_dir.\n"
           "live=" << live << "\nbase_dir=" << base_dir;
}

// ---------------------------------------------------------------------------
// Gate B r1 — Root cause C: sink-local collect-ALL truncation
// ---------------------------------------------------------------------------
//
// Within a single sink, the first error early-returns before later preflights
// are checked. Fix: parse all fields, accumulate diagnostics, return nullptr
// at the END iff new diagnostics were added (acc.size() > sink_acc_before).
//
// Witness: file sink with TWO independent errors in the same sink (max_file_bytes=0
// AND a non-existent directory). The pre-fix code returns early on max_file_bytes=0
// and never checks the directory → only ONE diagnostic.
//
// Mutation discriminator: remove the collect-ALL fix (restore the early return on
// max_file_bytes=0) → only max_file_bytes diagnostic is emitted, directory
// diagnostic is absent → the EXPECT_TRUE(has_diag(…directory…)) goes RED.

TEST(GateBR1C_SinkCollectAll, TwoIndependentFileSinkErrors)
{
    // File sink: max_file_bytes=0 (out_of_range) AND directory that does not exist.
    // Both must be reported in a single pass.
    const std::string nonexistent_dir = "/tmp/fixpp_test_nonexistent_for_collect_all_c";
    { std::error_code ec; std::filesystem::remove_all(nonexistent_dir, ec); }

    const std::string toml_text =
        "[logger]\n"
        "  [[logger.sinks]]\n"
        "  kind           = \"file\"\n"
        "  directory      = \"" + nonexistent_dir + "\"\n"
        "  max_file_bytes = 0\n";

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
        << "Expected ≥2 diagnostics (collect-ALL): max_file_bytes=0 AND nonexistent directory. "
           "Pre-fix: early return on max_file_bytes=0 skips the directory check → 1 diagnostic.";

    EXPECT_TRUE(has_diag(diags,
                          fixpp::config::reason_class::out_of_range,
                          "logger.sinks[0].max_file_bytes"))
        << "Missing out_of_range on logger.sinks[0].max_file_bytes";

    EXPECT_TRUE(has_diag(diags,
                          fixpp::config::reason_class::invalid_or_contradictory_selector,
                          "logger.sinks[0].directory"))
        << "Missing invalid_or_contradictory_selector on logger.sinks[0].directory "
           "(directory does not exist)";
}

#ifdef FIXPP_CONFIG_HAS_OTLP
// CRITICAL: cert_source wrong type → silent plain-HTTP instead of TLS (fail-open).
// Legit string cert_source FIRST (to rule out last-writer-wins pass), then the
// wrong-type cert_source=123 in a separate call below.
TEST(GateBR1B_WrongTypeOptionals, OtlpCertSourceNotString)
{
    // cert_source = 123 (integer, not string) → malformed_value
    // Without the fix: cfg.cert_source stays "" → OTLP uses plain HTTP → security downgrade.
    // With the fix: malformed_value emitted → sink rejected → acc non-empty.
    // Mutation: drop the else → cert_source=123 accepted → acc empty → ASSERT_FALSE RED.
    const std::string toml_text = R"(
[logger]
  [[logger.sinks]]
  kind        = "otlp"
  endpoint    = "https://collector:4317/v1/logs"
  cert_source = 123
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

    ASSERT_FALSE(acc.empty())
        << "cert_source = 123 (integer) must produce a malformed_value diagnostic "
           "(silent default = plain HTTP = TLS fail-open security downgrade)";
    EXPECT_TRUE(has_diag(std::move(acc).release(),
                          fixpp::config::reason_class::malformed_value,
                          "logger.sinks[0].cert_source"))
        << "Expected malformed_value on logger.sinks[0].cert_source";
}

TEST(GateBR1B_WrongTypeOptionals, OtlpExportTimeoutNotString)
{
    // export_timeout = 10 (integer, not a duration string) → malformed_value
    const std::string toml_text = R"(
[logger]
  [[logger.sinks]]
  kind           = "otlp"
  endpoint       = "http://collector:4318/v1/logs"
  export_timeout = 10
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

    ASSERT_FALSE(acc.empty())
        << "export_timeout = 10 (integer) must produce a malformed_value diagnostic";
    EXPECT_TRUE(has_diag(std::move(acc).release(),
                          fixpp::config::reason_class::malformed_value,
                          "logger.sinks[0].export_timeout"))
        << "Expected malformed_value on logger.sinks[0].export_timeout";
}
#endif  // FIXPP_CONFIG_HAS_OTLP

// ---------------------------------------------------------------------------
// Gate B r1 — Root cause F: unconditional #include <unistd.h> + ::access(W_OK)
// ---------------------------------------------------------------------------
//
// logger_resolver.cpp included <unistd.h> unconditionally and called ::access()
// unconditionally. Both are POSIX-only APIs not present on Windows/MSVC.
// Fix: guard both behind #ifndef _WIN32 (the same guard already used in
// file_store_factory.cpp:46 for the same reason).
//
// The behavioral test for the POSIX path already exists as
//   T014_NegBattery.FileSinkDirNotWritable (tests the access(W_OK) POSIX arm)
//   T014_NegBattery.FileSinkDirNotExist   (tests the is_directory() arm without access)
//
// This test provides a portability-contract witness that documents the Windows
// fallback path: when an explicit directory is provided on a non-POSIX platform,
// the resolver uses is_directory() only (no write-check) — this is the #else
// branch that the #ifndef guard now activates on Windows.
//
// On Linux (POSIX): This test verifies the #ifndef _WIN32 guard is active
// (i.e. the POSIX path still runs on Linux), confirming the guard compiles
// correctly with the POSIX arm selected.  The mutation discriminator: without
// the guard there would be a compile failure on MSVC (unresolved unistd.h);
// since we can't test MSVC here, the test asserts the code COMPILES and the
// POSIX arm remains behaviorally active on POSIX builds.

#ifndef _WIN32
// On POSIX builds: the access(W_OK) path must be selected.
// This test is the POSIX arm of the #ifndef _WIN32 guard witness:
// it confirms that the guarded code still executes on POSIX after the fix.
// (The FileSinkDirNotWritable test is the full behavioral witness on POSIX.)
TEST(GateBR1F_Portability, PosixAccessCheckStillActiveOnPosix)
{
    // Create a readable-but-not-writable directory.
    const auto ro_dir = std::filesystem::temp_directory_path() /
                        "fixpp_gateb_r1f_posix_portability_test";
    {
        std::error_code ec;
        std::filesystem::create_directories(ro_dir, ec);
        if (ec) { GTEST_SKIP() << "Could not create temp dir"; }
        std::filesystem::permissions(ro_dir,
            std::filesystem::perms::owner_write |
            std::filesystem::perms::group_write |
            std::filesystem::perms::others_write,
            std::filesystem::perm_options::remove, ec);
        if (ec) { GTEST_SKIP() << "Could not remove write permission"; }
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
    } cl{ro_dir};

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

    // The access(W_OK) check must have fired: non-writable dir → diagnostic.
    // Mutation discriminator: drop access(W_OK) check → acc stays empty (dir exists,
    // is_directory() passes) → ASSERT_FALSE goes RED.
    ASSERT_FALSE(acc.empty())
        << "POSIX access(W_OK) preflight must reject a non-writable explicit directory "
           "(Gate B r1 F: the #ifndef _WIN32 guard must keep POSIX path active on Linux)";
    const auto diags = std::move(acc).release();
    EXPECT_TRUE(has_diag(diags,
                          fixpp::config::reason_class::invalid_or_contradictory_selector,
                          "logger.sinks[0].directory"))
        << "Expected invalid_or_contradictory_selector on logger.sinks[0].directory";
}
#endif  // !_WIN32

// ── GateBR2_WrongTypeClass — complete the FR-020 wrong-type guard class ──────
//
// Round-2 Codex re-review found `directory=123` silently accepted; a census of
// EVERY optional field read then found the whole class incomplete — only 7 of 16
// fields had the wrong-type→malformed_value guard. This cell pins the 9 that the
// r1 fixer + the r2 directory fix had left silently-defaulting.
//
// CRITICAL discriminator: each case uses a WRONG-TYPE value that is NOT
// negative/out-of-range — e.g. max_file_bytes = "big" (a STRING). A negative or
// oversized integer would hit the EXISTING r1 range check (out_of_range), not the
// NEW `else if (!is_integer())` branch. So these values exercise exactly the new
// guard. Mutation: drop any `else if (n && !n->is_TYPE())` branch → that field's
// wrong-type value is silently accepted → no malformed_value → EXPECT RED.
//
// kind/endpoint/sinks are intentionally NOT here: their wrong-type maps to
// missing_required (required-field semantics) — a split Codex r1 #6 called
// "defensible" and explicitly blessed; only silent OPTIONAL defaulting was the bug.
TEST(GateBR2_WrongTypeClass, AllRemainingOptionalFieldsRejectWrongType)
{
    struct Case {
        std::string toml;
        std::string key_path;
    };
    std::vector<Case> cases = {
        // file-sink numerics — STRING where integer expected
        {R"(
[logger]
  [[logger.sinks]]
  kind           = "file"
  directory      = "/tmp"
  max_file_bytes = "big"
)",
         "logger.sinks[0].max_file_bytes"},
        {R"(
[logger]
  [[logger.sinks]]
  kind           = "file"
  directory      = "/tmp"
  max_keep_count = "many"
)",
         "logger.sinks[0].max_keep_count"},
        // logger-level scalars
        {R"(
[logger]
capacity = "huge"
  [[logger.sinks]]
  kind      = "file"
  directory = "/tmp"
)",
         "logger.capacity"},
        {R"(
[logger]
drain_timeout = 5000
  [[logger.sinks]]
  kind      = "file"
  directory = "/tmp"
)",
         "logger.drain_timeout"},
        {R"(
[logger]
drain_cpu_affinity = "two"
  [[logger.sinks]]
  kind      = "file"
  directory = "/tmp"
)",
         "logger.drain_cpu_affinity"},
#ifdef FIXPP_CONFIG_HAS_OTLP
        {R"(
[logger]
  [[logger.sinks]]
  kind     = "otlp"
  endpoint = "https://collector:4317/v1/logs"
  use_grpc = "yes"
)",
         "logger.sinks[0].use_grpc"},
        {R"(
[logger]
  [[logger.sinks]]
  kind             = "otlp"
  endpoint         = "https://collector:4317/v1/logs"
  max_export_batch = "lots"
)",
         "logger.sinks[0].max_export_batch"},
        {R"(
[logger]
  [[logger.sinks]]
  kind               = "otlp"
  endpoint           = "https://collector:4317/v1/logs"
  max_export_retries = "few"
)",
         "logger.sinks[0].max_export_retries"},
#endif  // FIXPP_CONFIG_HAS_OTLP
#ifdef FIXPP_HAS_SYSLOG
        {R"(
[logger]
  [[logger.sinks]]
  kind  = "syslog"
  ident = 123
)",
         "logger.sinks[0].ident"},
#endif  // FIXPP_HAS_SYSLOG
    };

    for (const auto& c : cases) {
        auto parsed = parse_logger_inline(c.toml);
        ASSERT_NE(parsed.logger_tbl, nullptr) << "parse failed for " << c.key_path;

        fixpp::config::detail::DiagnosticAccumulator acc;
        fixpp::config::PendingLoggerSet pending;
        fixpp::config::LoadOptions opts;
        opts.resource = std::pmr::get_default_resource();
        fixpp::config::detail::resolve_engine_logger(
            *parsed.logger_tbl, "logger", fixpp::config::SourceLoc{},
            std::filesystem::temp_directory_path(), opts, pending, acc,
            /*is_engine=*/true, /*session_index=*/0);

        ASSERT_FALSE(acc.empty())
            << "wrong-type field at " << c.key_path << " must produce a diagnostic";
        const auto diags = std::move(acc).release();
        EXPECT_TRUE(has_diag(diags, fixpp::config::reason_class::malformed_value, c.key_path))
            << "Expected malformed_value at " << c.key_path
            << " (present-but-wrong-type optional field must fail closed, FR-020)";
        EXPECT_FALSE(pending.engine.has_value())
            << "a wrong-type field must prevent the logger from being parked: " << c.key_path;
    }
}
